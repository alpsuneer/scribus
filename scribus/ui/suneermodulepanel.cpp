/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneermodulepanel.h"

#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "canvas.h"
#include "pageitem.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "ui/suneer_news_panel.h"

QPointer<SuneerModulePanel> SuneerModulePanel::s_panel;

namespace {
QColor kindColour(const SuneerLayout::Module& m)
{
	if (m.fixed) return QColor(120, 120, 120);
	if (m.kind == QLatin1String("news")) return QColor(46, 125, 50);
	if (m.kind == QLatin1String("obit")) return QColor(123, 31, 162);
	if (m.rigid) return QColor(230, 81, 0);
	return QColor(21, 101, 192);
}

QString kindLabel(const SuneerLayout::Module& m)
{
	if (m.fixed) return QObject::tr("fixed (%1)").arg(m.reason.isEmpty() ? QObject::tr("user") : m.reason);
	if (m.kind == QLatin1String("news")) return QObject::tr("News Browser story");
	if (m.kind == QLatin1String("obit")) return QObject::tr("obituary");
	if (m.rigid) return QObject::tr("block (moved as it is)");
	return QObject::tr("story");
}
} // namespace

SuneerModulePanel* SuneerModulePanel::openFor(ScribusMainWindow* mw, ScribusDoc* doc, SuneerNewsPanel* news)
{
	if (!doc || !doc->currentPage())
		return nullptr;
	if (s_panel && s_panel->m_doc == doc)
	{
		s_panel->detect(false);
		s_panel->show();
		s_panel->raise();
		return s_panel;
	}
	if (s_panel)
		s_panel->close();
	s_panel = new SuneerModulePanel(mw, doc, news);
	s_panel->setAttribute(Qt::WA_DeleteOnClose);
	s_panel->show();
	return s_panel;
}

bool SuneerModulePanel::showing(ScribusDoc* doc)
{
	return s_panel && s_panel->isVisible() && s_panel->m_doc == doc;
}

SuneerModulePanel::SuneerModulePanel(ScribusMainWindow* mw, ScribusDoc* doc, SuneerNewsPanel* news)
    : QDialog(mw, Qt::Tool), m_mw(mw), m_doc(doc), m_news(news)
{
	setWindowTitle(tr("Page Modules"));
	resize(420, 620);
	auto* vl = new QVBoxLayout(this);
	m_count = new QLabel(this);
	m_count->setWordWrap(true);
	vl->addWidget(m_count);
	auto* help = new QLabel(tr("Select frames on the page, then: Merge = one module, Split = each frame its own module, Keep fixed = never moved. Changes are written on the frames."), this);
	help->setWordWrap(true);
	help->setStyleSheet(QStringLiteral("color:#555; font-size:11px;"));
	vl->addWidget(help);
	m_list = new QListWidget(this);
	vl->addWidget(m_list, 1);
	auto* g = new QGridLayout();
	auto mk = [this](const QString& t, const QString& tip) { auto* b = new QPushButton(t, this); b->setToolTip(tip); b->setAutoDefault(false); return b; };
	QPushButton* merge = mk(tr("Merge selected"), tr("The frames selected on the page (and the modules they belong to) become one module"));
	QPushButton* split = mk(tr("Split selected"), tr("Each frame selected on the page becomes a module of its own"));
	QPushButton* fix = mk(tr("Keep fixed"), tr("The modules of the selected frames are never moved by a layout model"));
	QPushButton* unfix = mk(tr("Not fixed"), tr("The modules of the selected frames may be arranged again"));
	QPushButton* redetect = mk(tr("Detect again"), tr("Forget what was written on the frames of this page and detect the modules from scratch"));
	QPushButton* save = mk(tr("Save"), tr("Write the modules on the frames (layout.module ...) so the next re-layout uses them"));
	QPushButton* apply = mk(tr("Apply Layout Model..."), tr("Save, then open the Layout Gallery for this page"));
	apply->setStyleSheet(QStringLiteral("font-weight:bold;"));
	g->addWidget(merge, 0, 0);
	g->addWidget(split, 0, 1);
	g->addWidget(fix, 1, 0);
	g->addWidget(unfix, 1, 1);
	g->addWidget(redetect, 2, 0);
	g->addWidget(save, 2, 1);
	g->addWidget(apply, 3, 0, 1, 2);
	vl->addLayout(g);
	connect(merge, &QPushButton::clicked, this, &SuneerModulePanel::mergeSelected);
	connect(split, &QPushButton::clicked, this, &SuneerModulePanel::splitSelected);
	connect(fix, &QPushButton::clicked, this, [this]() { setFixed(true); });
	connect(unfix, &QPushButton::clicked, this, [this]() { setFixed(false); });
	connect(redetect, &QPushButton::clicked, this, [this]() { detect(true); });
	connect(save, &QPushButton::clicked, this, &SuneerModulePanel::save);
	connect(apply, &QPushButton::clicked, this, &SuneerModulePanel::apply);
	connect(m_list, &QListWidget::currentRowChanged, this, &SuneerModulePanel::selectModule);
	detect(false);
}

void SuneerModulePanel::closeEvent(QCloseEvent* e)
{
	m_page = -1;                 // no outlines from now on
	if (m_doc && m_doc->view())
		m_doc->view()->DrawNew();
	QDialog::closeEvent(e);
}

void SuneerModulePanel::detect(bool fromScratch)
{
	if (!m_doc || !m_doc->currentPage())
		return;
	m_page = m_doc->currentPageNumber();
	if (fromScratch)
		SuneerLayout::forgetModules(m_doc, m_page);
	m_modules = SuneerLayout::detectModules(m_doc, m_page, SuneerLayout::StyleNames::fromSettings(QString()));
	refreshList();
	repaintCanvas();
}

void SuneerModulePanel::refreshList()
{
	m_list->blockSignals(true);
	m_list->clear();
	int movable = 0, fixed = 0, guessed = 0;
	for (int i = 0; i < m_modules.size(); ++i)
	{
		const SuneerLayout::Module& m = m_modules[i];
		(m.fixed ? fixed : movable)++;
		if (!m.remembered)
			++guessed;
		auto* it = new QListWidgetItem(QStringLiteral("%1. %2 — %3 (%4 frame(s))").arg(i + 1).arg(m.title.isEmpty() ? tr("(no text)") : m.title, kindLabel(m)).arg(m.items.size()), m_list);
		it->setForeground(kindColour(m));
	}
	m_list->blockSignals(false);
	m_count->setText(tr("<b>Page %1: %2 module(s)</b> to arrange, %3 fixed. %4")
	                     .arg(m_page + 1).arg(movable).arg(fixed)
	                     .arg(guessed > 0 ? tr("%n found by detection, not saved yet.", "", guessed) : tr("All saved on the frames.")));
}

void SuneerModulePanel::repaintCanvas()
{
	if (m_doc && m_doc->view())
	{
		m_doc->view()->DrawNew();
	}
}

QList<int> SuneerModulePanel::selectedModules() const
{
	QList<int> out;
	if (!m_doc)
		return out;
	QSet<PageItem*> sel;
	for (int i = 0; i < m_doc->m_Selection->count(); ++i)
	{
		PageItem* it = m_doc->m_Selection->itemAt(i);
		while (it && it->isGroupChild())
			it = it->Parent;
		sel.insert(it);
	}
	for (int i = 0; i < m_modules.size(); ++i)
		for (const QPointer<PageItem>& it : m_modules[i].items)
			if (it && sel.contains(it.data()) && !out.contains(i))
				out << i;
	return out;
}

void SuneerModulePanel::mergeSelected()
{
	const QList<int> idx = selectedModules();
	if (idx.size() < 2)
	{
		m_count->setText(tr("Select frames of two or more modules on the page first."));
		return;
	}
	SuneerLayout::Module merged;
	merged.id = SuneerLayout::newModuleId();
	merged.kind = QStringLiteral("story");
	merged.remembered = true;
	for (int i : idx)
	{
		const SuneerLayout::Module& m = m_modules[i];
		merged.items << m.items;
		merged.photos << m.photos;
		merged.captions << m.captions;
		merged.heads << m.heads;
		if (m.body && (!merged.body || m.body->itemText.length() > merged.body->itemText.length()))
		{
			if (merged.body)
				merged.rigid = true;        // two body frames: one block
			merged.body = m.body;
		}
		else if (m.body)
			merged.rigid = true;
		if (merged.title.isEmpty())
			merged.title = m.title;
	}
	if (!merged.body)
		merged.rigid = true;
	QList<SuneerLayout::Module> rest;
	for (int i = 0; i < m_modules.size(); ++i)
		if (!idx.contains(i))
			rest << m_modules[i];
	rest << merged;
	m_modules = rest;
	SuneerLayout::rememberModules(m_modules);
	detect(false);
}

void SuneerModulePanel::splitSelected()
{
	if (!m_doc)
		return;
	QSet<PageItem*> sel;
	for (int i = 0; i < m_doc->m_Selection->count(); ++i)
	{
		PageItem* it = m_doc->m_Selection->itemAt(i);
		while (it && it->isGroupChild())
			it = it->Parent;
		sel.insert(it);
	}
	if (sel.isEmpty())
	{
		m_count->setText(tr("Select the frames to split off on the page first."));
		return;
	}
	QList<SuneerLayout::Module> out;
	for (const SuneerLayout::Module& m : m_modules)
	{
		SuneerLayout::Module rest = m;
		rest.items.clear();
		for (const QPointer<PageItem>& it : m.items)
		{
			if (!it)
				continue;
			if (!sel.contains(it.data()))
			{
				rest.items << it;
				continue;
			}
			SuneerLayout::Module own;
			own.id = SuneerLayout::newModuleId();
			own.remembered = true;
			own.items << it;
			if (it->isTextFrame())
			{
				own.kind = QStringLiteral("story");
				own.body = it;
			}
			else
			{
				own.kind = QStringLiteral("block");
				own.rigid = true;
			}
			out << own;
		}
		if (rest.items.isEmpty())
			continue;
		if (rest.body && !rest.items.contains(rest.body))
		{
			rest.body = nullptr;
			rest.rigid = true;
		}
		out << rest;
	}
	m_modules = out;
	SuneerLayout::rememberModules(m_modules);
	detect(false);
}

void SuneerModulePanel::setFixed(bool fixed)
{
	const QList<int> idx = selectedModules();
	if (idx.isEmpty())
	{
		m_count->setText(tr("Select frames on the page first."));
		return;
	}
	for (int i : idx)
	{
		m_modules[i].fixed = fixed;
		m_modules[i].reason = fixed ? QStringLiteral("user") : QString();
		if (!fixed && m_modules[i].kind == QLatin1String("fixed"))
		{
			m_modules[i].kind = m_modules[i].body ? QStringLiteral("story") : QStringLiteral("block");
			m_modules[i].rigid = !m_modules[i].body;
		}
	}
	SuneerLayout::rememberModules(m_modules);
	detect(false);
}

void SuneerModulePanel::save()
{
	SuneerLayout::rememberModules(m_modules);
	detect(false);
	if (m_doc)
		m_doc->changed();
}

void SuneerModulePanel::apply()
{
	save();
	if (m_news)
		m_news->openLayoutGallery();
	detect(false);
}

void SuneerModulePanel::selectModule(int row)
{
	if (!m_doc || row < 0 || row >= m_modules.size() || !m_doc->view())
		return;
	m_doc->m_Selection->clear();
	for (const QPointer<PageItem>& it : m_modules[row].items)
		if (it)
			m_doc->m_Selection->addItem(it.data());
	m_doc->view()->DrawNew();
}

void SuneerModulePanel::paintOverlay(QPainter* painter, const Canvas* canvas, ScribusDoc* doc)
{
	if (!painter || !canvas || !doc || !showing(doc) || doc->drawAsPreview)
		return;
	SuneerModulePanel* p = s_panel;
	if (p->m_page < 0 || p->m_page >= doc->Pages->count())
		return;
	const ScPage* page = doc->Pages->at(p->m_page);
	QTransform t;
	t.scale(canvas->scale(), canvas->scale());
	t.translate(-doc->minCanvasCoordinate.x(), -doc->minCanvasCoordinate.y());
	painter->save();
	painter->resetTransform();
	painter->setRenderHint(QPainter::Antialiasing, false);
	QFont f = painter->font();
	f.setPixelSize(11);
	f.setBold(true);
	painter->setFont(f);
	const QFontMetrics fm(f);
	for (int i = 0; i < p->m_modules.size(); ++i)
	{
		const SuneerLayout::Module& m = p->m_modules[i];
		const QRectF r = t.mapRect(m.rect.translated(page->xOffset(), page->yOffset())).adjusted(1, 1, -1, -1);
		const QColor c = kindColour(m);
		painter->setPen(QPen(c, m.fixed ? 1.5 : 2.5, m.fixed ? Qt::DashLine : (m.remembered ? Qt::SolidLine : Qt::DashDotLine)));
		QColor fill = c;
		fill.setAlpha(m.fixed ? 18 : 28);
		painter->setBrush(fill);
		painter->drawRect(r);
		const QString label = QStringLiteral("%1 %2").arg(i + 1).arg(m.fixed ? tr("fixed") : m.kind == QLatin1String("news") ? tr("news") : m.kind);
		const QRectF badge(r.left(), r.top(), fm.horizontalAdvance(label) + 8, fm.height() + 2);
		painter->setPen(Qt::NoPen);
		painter->setBrush(c);
		painter->drawRect(badge);
		painter->setPen(Qt::white);
		painter->drawText(badge, Qt::AlignCenter, label);
	}
	painter->restore();
}
