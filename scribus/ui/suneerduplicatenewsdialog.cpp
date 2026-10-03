/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/suneerduplicatenewsdialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMultiHash>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "canvas.h"
#include "pageitem.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "scribuswin.h"
#include "styles/paragraphstyle.h"
#include "text/boxes.h"
#include "text/specialchars.h"

namespace
{
	enum { RoleStory = Qt::UserRole + 1, RoleGroup };

	struct DocMarks
	{
		QPointer<ScribusDoc> doc;
		QList<SuneerDupHighlights::Mark> marks;
	};

	QList<DocMarks>& markStore()
	{
		static QList<DocMarks> store;
		return store;
	}

	void repaint(ScribusDoc* doc)
	{
		if (doc && doc->view())
			doc->view()->m_canvas->update();
	}

	//! Group 1 orange, group 2 purple, then colours that stay apart from both.
	QColor groupColor(int group)
	{
		static const QList<QColor> colors {
			QColor(255, 140, 0), QColor(142, 68, 173), QColor(0, 150, 136), QColor(30, 136, 229),
			QColor(67, 160, 71), QColor(216, 27, 96), QColor(141, 110, 99), QColor(175, 180, 43),
			QColor(0, 172, 193), QColor(229, 57, 53)
		};
		return colors.at(group % colors.size());
	}

	QIcon swatch(const QColor& c)
	{
		QPixmap pm(14, 14);
		pm.fill(c);
		QPainter p(&pm);
		p.setPen(c.darker(150));
		p.drawRect(0, 0, 13, 13);
		return QIcon(pm);
	}

	bool stillInDocument(const ScribusDoc* doc, const PageItem* item)
	{
		const PageItem* top = item;
		while (top->Parent)
			top = top->Parent;
		return doc->DocItems.contains(const_cast<PageItem*>(top));
	}

	int pageOf(const PageItem* item)
	{
		const PageItem* top = item;
		while (top->Parent)
			top = top->Parent;
		return top->OwnPage;
	}

	bool hits(const Box* glyph, const QList<QPair<int, int>>& ranges)
	{
		for (const auto& r : ranges)
		{
			if (glyph->firstChar() < r.second && glyph->lastChar() >= r.first)
				return true;
		}
		return false;
	}

	//! Rectangles (frame coordinates) covering the glyph clusters of a text frame
	//! whose characters fall in the ranges, one rectangle per run on a line.
	void passageRects(const Box* box, QPointF origin, const QList<QPair<int, int>>& ranges, QVector<QRectF>& out)
	{
		const QPointF o = origin + QPointF(box->x(), box->y());
		if (box->type() == Box::T_Line || box->type() == Box::T_PathLine)
		{
			QRectF run;
			for (const Box* glyph : box->boxes())
			{
				if (hits(glyph, ranges))
				{
					const QRectF r(o.x() + glyph->x(), o.y(), glyph->width(), box->height());
					run = run.isNull() ? r : run.united(r);
				}
				else if (!run.isNull())
				{
					out.append(run);
					run = QRectF();
				}
			}
			if (!run.isNull())
				out.append(run);
			return;
		}
		for (const Box* child : box->boxes())
			passageRects(child, o, ranges, out);
	}
}

void SuneerDupHighlights::setMarks(ScribusDoc* doc, const QList<Mark>& marks)
{
	QList<DocMarks>& store = markStore();
	for (int i = store.size() - 1; i >= 0; --i)
	{
		if (store[i].doc.isNull() || store[i].doc == doc)
			store.removeAt(i);
	}
	if (doc && !marks.isEmpty())
	{
		DocMarks dm;
		dm.doc = doc;
		dm.marks = marks;
		store.append(dm);
	}
	repaint(doc);
}

void SuneerDupHighlights::clearAll()
{
	QList<DocMarks> old = markStore();
	markStore().clear();
	for (const DocMarks& dm : old)
		repaint(dm.doc);
}

bool SuneerDupHighlights::isEmpty()
{
	return markStore().isEmpty();
}

void SuneerDupHighlights::paint(QPainter* painter, const Canvas* canvas, ScribusDoc* doc)
{
	const QList<DocMarks>& store = markStore();
	if (store.isEmpty() || !doc || doc->drawAsPreview || doc->masterPageMode())
		return;
	for (const DocMarks& dm : store)
	{
		if (dm.doc != doc)
			continue;
		QHash<const PageItem*, int> badges;
		for (const Mark& m : dm.marks)
		{
			const PageItem* item = m.item.data();
			if (!item || !stillInDocument(doc, item) || !doc->layerVisible(item->m_layerID))
				continue;
			const QRectF frame(0, 0, item->width(), item->height());
			painter->save();
			painter->scale(canvas->scale(), canvas->scale());
			painter->translate(-doc->minCanvasCoordinate.x(), -doc->minCanvasCoordinate.y());
			painter->setTransform(item->getTransform(), true);

			QColor tint = m.color;
			tint.setAlpha(36);
			painter->fillRect(frame, tint);
			if (!m.ranges.isEmpty() && item->isTextFrame() && item->textLayout.box())
			{
				QVector<QRectF> rects;
				passageRects(item->textLayout.box(), QPointF(), m.ranges, rects);
				QColor passage = m.color;
				passage.setAlpha(90);
				for (const QRectF& r : rects)
					painter->fillRect(r, passage);
			}
			QPen pen(m.color, 3);
			pen.setCosmetic(true);
			pen.setJoinStyle(Qt::MiterJoin);
			painter->setPen(pen);
			painter->setBrush(Qt::NoBrush);
			painter->drawRect(frame);

			// the group number, top left, at screen size
			const QPointF topLeft = painter->transform().map(QPointF(0, 0));
			painter->resetTransform();
			const int slot = badges[item]++;
			const QRectF badge(topLeft.x() + 3 + slot * 24, topLeft.y() + 3, 22, 16);
			painter->fillRect(badge, m.color);
			QFont f = painter->font();
			f.setBold(true);
			f.setPixelSize(11);
			painter->setFont(f);
			painter->setPen(Qt::white);
			painter->drawText(badge, Qt::AlignCenter, QString::number(m.group));
			painter->restore();
		}
	}
}

SuneerDuplicateNewsDialog::SuneerDuplicateNewsDialog(ScribusMainWindow* mainWindow)
	: QDialog(mainWindow),
	  m_mainWindow(mainWindow)
{
	setWindowTitle(tr("Duplicate News Checker"));
	setModal(false);
	resize(760, 560);

	QVBoxLayout* layout = new QVBoxLayout(this);
	QGridLayout* options = new QGridLayout();
	int row = 0;

	m_scope = new QComboBox(this);
	m_scope->addItem(tr("Current page"), ScopePage);
	m_scope->addItem(tr("Whole document"), ScopeDocument);
	m_scope->addItem(tr("All open documents"), ScopeOpenDocuments);
	m_scope->addItem(tr("All .sla files in a folder"), ScopeFolder);
	options->addWidget(new QLabel(tr("Check:"), this), row, 0);
	options->addWidget(m_scope, row++, 1, 1, 2);

	m_folder = new QLineEdit(this);
	m_folder->setPlaceholderText(tr("Folder with today's pages"));
	m_browse = new QPushButton(tr("Browse..."), this);
	options->addWidget(new QLabel(tr("Folder:"), this), row, 0);
	options->addWidget(m_folder, row, 1);
	options->addWidget(m_browse, row++, 2);

	m_threshold = new QSpinBox(this);
	m_threshold->setRange(50, 100);
	m_threshold->setSuffix(QStringLiteral(" %"));
	m_threshold->setToolTip(tr("Two headlines or two stories count as the same news from this similarity up.\n"
	                           "Spaces, punctuation, hyphenation, ZWJ/ZWNJ and old/new chillu forms are ignored."));
	options->addWidget(new QLabel(tr("Similarity threshold:"), this), row, 0);
	options->addWidget(m_threshold, row++, 1, Qt::AlignLeft);

	m_pasteboard = new QCheckBox(tr("Include frames on the pasteboard"), this);
	options->addWidget(m_pasteboard, row++, 1, 1, 2);
	m_captions = new QCheckBox(tr("Include captions, pull quotes and highlight boxes"), this);
	m_captions->setToolTip(tr("Captions usually repeat a sentence of their story, so they are left out by default."));
	options->addWidget(m_captions, row++, 1, 1, 2);
	options->setColumnStretch(1, 1);
	layout->addLayout(options);

	QHBoxLayout* runRow = new QHBoxLayout();
	m_check = new QPushButton(tr("Check"), this);
	m_check->setDefault(true);
	m_cancel = new QPushButton(tr("Cancel"), this);
	m_progressBar = new QProgressBar(this);
	m_progressBar->setRange(0, 1000);
	m_progressBar->setTextVisible(false);
	runRow->addWidget(m_check);
	runRow->addWidget(m_cancel);
	runRow->addWidget(m_progressBar, 1);
	layout->addLayout(runRow);

	m_status = new QLabel(tr("Choose what to check and press Check."), this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(4);
	m_tree->setHeaderLabels({ tr("News"), tr("Similarity"), tr("Where"), tr("Frame") });
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setStretchLastSection(false);
	m_tree->setRootIsDecorated(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	layout->addWidget(m_tree, 1);

	QHBoxLayout* bottom = new QHBoxLayout();
	m_pin = new QCheckBox(tr("Keep highlights after closing"), this);
	m_highlightButton = new QPushButton(tr("Clear highlights"), this);
	m_ignoredLabel = new QLabel(this);
	m_forgetIgnored = new QPushButton(tr("Forget ignored"), this);
	QPushButton* close = new QPushButton(tr("Close"), this);
	bottom->addWidget(m_pin);
	bottom->addWidget(m_highlightButton);
	bottom->addStretch(1);
	bottom->addWidget(m_ignoredLabel);
	bottom->addWidget(m_forgetIgnored);
	bottom->addWidget(close);
	layout->addLayout(bottom);

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(60);

	QSettings settings(QStringLiteral("Faircode"), QStringLiteral("ScribusDuplicateNews"));
	m_threshold->setValue(settings.value(QStringLiteral("threshold"), 85).toInt());
	m_folder->setText(settings.value(QStringLiteral("folder")).toString());
	m_pasteboard->setChecked(settings.value(QStringLiteral("pasteboard"), false).toBool());
	m_captions->setChecked(settings.value(QStringLiteral("captions"), false).toBool());
	m_pin->setChecked(settings.value(QStringLiteral("pin"), false).toBool());
	const QStringList ignored = settings.value(QStringLiteral("ignored")).toStringList();
	for (const QString& fp : ignored)
		m_ignored.insert(fp.toLatin1());
	const int scope = settings.value(QStringLiteral("scope"), m_mainWindow->HaveDoc ? ScopeDocument : ScopeFolder).toInt();
	m_scope->setCurrentIndex(qBound(0, scope, m_scope->count() - 1));

	setRunning(false);
	scopeChanged();
	updateIgnoredLabel();
	m_highlightButton->setEnabled(false);

	connect(m_scope, &QComboBox::currentIndexChanged, this, &SuneerDuplicateNewsDialog::scopeChanged);
	connect(m_browse, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::browseFolder);
	connect(m_check, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::startCheck);
	connect(m_cancel, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::cancelCheck);
	connect(m_pollTimer, &QTimer::timeout, this, &SuneerDuplicateNewsDialog::pollProgress);
	connect(&m_watcher, &QFutureWatcher<DupNews::Result>::finished, this, &SuneerDuplicateNewsDialog::checkFinished);
	connect(m_tree, &QTreeWidget::itemClicked, this, &SuneerDuplicateNewsDialog::itemClicked);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SuneerDuplicateNewsDialog::showContextMenu);
	connect(m_highlightButton, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::toggleHighlights);
	connect(m_forgetIgnored, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::forgetIgnored);
	connect(m_pin, &QCheckBox::toggled, this, [this]() { saveSettings(); });
	connect(close, &QPushButton::clicked, this, &SuneerDuplicateNewsDialog::reject);
}

SuneerDuplicateNewsDialog::~SuneerDuplicateNewsDialog()
{
	if (m_progress)
		m_progress->cancel.store(true);
}

void SuneerDuplicateNewsDialog::closeEvent(QCloseEvent* event)
{
	reject();
	event->accept();
}

void SuneerDuplicateNewsDialog::reject()
{
	if (m_watcher.isRunning())
		cancelCheck();
	if (!m_pin->isChecked())
		clearMarks();
	saveSettings();
	QDialog::reject();
}

void SuneerDuplicateNewsDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (m_haveResult && !m_marksShown && !m_watcher.isRunning())
	{
		applyMarks();
		m_status->setText(tr("These are the results of the last check. Press Check to check again."));
	}
}

void SuneerDuplicateNewsDialog::scopeChanged()
{
	const bool folder = m_scope->currentData().toInt() == ScopeFolder;
	m_folder->setEnabled(folder);
	m_browse->setEnabled(folder);
}

void SuneerDuplicateNewsDialog::browseFolder()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder with the pages to check"), m_folder->text());
	if (!dir.isEmpty())
		m_folder->setText(QDir::toNativeSeparators(dir));
}

void SuneerDuplicateNewsDialog::saveSettings() const
{
	QSettings settings(QStringLiteral("Faircode"), QStringLiteral("ScribusDuplicateNews"));
	settings.setValue(QStringLiteral("threshold"), m_threshold->value());
	settings.setValue(QStringLiteral("scope"), m_scope->currentData().toInt());
	settings.setValue(QStringLiteral("folder"), m_folder->text());
	settings.setValue(QStringLiteral("pasteboard"), m_pasteboard->isChecked());
	settings.setValue(QStringLiteral("captions"), m_captions->isChecked());
	settings.setValue(QStringLiteral("pin"), m_pin->isChecked());
	QStringList ignored;
	for (const QByteArray& fp : m_ignored)
		ignored.append(QString::fromLatin1(fp));
	settings.setValue(QStringLiteral("ignored"), ignored);
}

void SuneerDuplicateNewsDialog::updateIgnoredLabel()
{
	m_ignoredLabel->setText(m_ignored.isEmpty() ? QString() : tr("Ignored texts: %1").arg(m_ignored.size()));
	m_forgetIgnored->setVisible(!m_ignored.isEmpty());
}

void SuneerDuplicateNewsDialog::forgetIgnored()
{
	m_ignored.clear();
	saveSettings();
	updateIgnoredLabel();
	m_status->setText(tr("Ignored texts forgotten. Press Check to check again."));
}

void SuneerDuplicateNewsDialog::setRunning(bool running)
{
	m_check->setEnabled(!running);
	m_cancel->setVisible(running);
	m_progressBar->setVisible(running);
	m_scope->setEnabled(!running);
	m_threshold->setEnabled(!running);
	m_pasteboard->setEnabled(!running);
	m_captions->setEnabled(!running);
	if (running)
		m_progressBar->setValue(0);
	else
		scopeChanged();
	if (running)
	{
		m_folder->setEnabled(false);
		m_browse->setEnabled(false);
	}
}

QList<ScribusDoc*> SuneerDuplicateNewsDialog::openDocuments() const
{
	QList<ScribusDoc*> docs;
	const QList<QMdiSubWindow*> windows = m_mainWindow->mdiArea->subWindowList();
	for (QMdiSubWindow* w : windows)
	{
		ScribusWin* sw = dynamic_cast<ScribusWin*>(w->widget());
		if (sw && sw->doc() && !docs.contains(sw->doc()))
			docs.append(sw->doc());
	}
	return docs;
}

ScribusDoc* SuneerDuplicateNewsDialog::openDocumentFor(const QString& filePath) const
{
	const QString wanted = QFileInfo(filePath).canonicalFilePath();
	if (wanted.isEmpty())
		return nullptr;
	const QList<ScribusDoc*> docs = openDocuments();
	for (ScribusDoc* doc : docs)
	{
		if (doc->hasName && QFileInfo(doc->documentFileName()).canonicalFilePath() == wanted)
			return doc;
	}
	return nullptr;
}

void SuneerDuplicateNewsDialog::collectDocument(ScribusDoc* doc, int source, int onlyPage, QList<DupNews::Story>& stories, QList<LiveStory>& live) const
{
	const QList<PageItem*> items = doc->getAllItems(doc->DocItems);
	for (PageItem* head : items)
	{
		if (!head->isTextFrame() || head->isNoteFrame() || head->prevInChain() != nullptr)
			continue;
		DupNews::Story story;
		LiveStory ls;
		ls.doc = doc;
		story.source = source;
		story.name = head->itemName();
		bool wanted = onlyPage < 0;
		QSet<PageItem*> seen;
		for (PageItem* f = head; f && !seen.contains(f); f = f->nextInChain())
		{
			seen.insert(f);
			DupNews::FrameRef ref;
			ref.name = f->itemName();
			ref.page = pageOf(f);
			story.frames.append(ref);
			ls.frames.append(f);
			if (ref.page == onlyPage)
				wanted = true;
		}
		if (!wanted)
			continue;

		const StoryText& text = head->itemText;
		const int len = text.length();
		story.text = text.text(0, len);
		int start = 0;
		for (int i = 0; i <= len; ++i)
		{
			if (i < len && story.text.at(i) != SpecialChars::PARSEP)
				continue;
			DupNews::Para para;
			para.start = start;
			para.end = i;
			const ParagraphStyle& pstyle = text.paragraphStyle(start);
			if (!pstyle.name().isEmpty())
				para.styles.append(pstyle.name());
			for (const BaseStyle* parent = pstyle.parentStyle(); parent && !para.styles.contains(parent->name()); parent = parent->parentStyle())
			{
				if (!parent->name().isEmpty())
					para.styles.append(parent->name());
			}
			para.fontSize = ((start < len) ? text.charStyle(start).fontSize() : pstyle.charStyle().fontSize()) / 10.0;
			story.paras.append(para);
			start = i + 1;
		}
		stories.append(story);
		live.append(ls);
	}
}

void SuneerDuplicateNewsDialog::startCheck()
{
	if (m_watcher.isRunning())
		return;
	clearMarks();
	m_tree->clear();
	m_live.clear();
	m_pendingLive.clear();
	m_hiddenGroups.clear();
	m_haveResult = false;
	m_highlightButton->setEnabled(false);

	DupNews::Job job;
	job.options.threshold = m_threshold->value();
	job.options.includePasteboard = m_pasteboard->isChecked();
	job.options.includeCaptions = m_captions->isChecked();
	job.options.ignored = m_ignored;
	QList<LiveStory> live;

	auto addOpenDocument = [&](ScribusDoc* doc, int onlyPage) {
		DupNews::Source src;
		src.live = true;
		src.filePath = doc->hasName ? doc->documentFileName() : QString();
		src.label = QFileInfo(doc->documentFileName()).fileName();
		job.sources.append(src);
		collectDocument(doc, job.sources.size() - 1, onlyPage, job.stories, live);
	};

	const int scope = m_scope->currentData().toInt();
	ScribusDoc* current = m_mainWindow->HaveDoc ? m_mainWindow->doc : nullptr;
	if (scope == ScopePage || scope == ScopeDocument)
	{
		if (!current)
		{
			m_status->setText(tr("No document is open."));
			return;
		}
		addOpenDocument(current, scope == ScopePage ? current->currentPageNumber() : -1);
	}
	else if (scope == ScopeOpenDocuments)
	{
		const QList<ScribusDoc*> docs = openDocuments();
		for (ScribusDoc* doc : docs)
			addOpenDocument(doc, -1);
		if (docs.isEmpty())
		{
			m_status->setText(tr("No document is open."));
			return;
		}
	}
	else
	{
		const QDir dir(m_folder->text());
		if (m_folder->text().isEmpty() || !dir.exists())
		{
			m_status->setText(tr("Choose the folder with the pages first."));
			return;
		}
		const QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.sla"), QStringLiteral("*.SLA") }, QDir::Files | QDir::Readable, QDir::Name);
		for (const QFileInfo& fi : files)
		{
			if (fi.fileName().contains(QLatin1String("_autosave"), Qt::CaseInsensitive))
				continue;
			if (ScribusDoc* doc = openDocumentFor(fi.absoluteFilePath()))
			{
				addOpenDocument(doc, -1); // an open page is checked as it is now, unsaved edits included
				continue;
			}
			DupNews::Source src;
			src.filePath = fi.absoluteFilePath();
			src.label = fi.fileName();
			job.sources.append(src);
			job.slaSources.append(job.sources.size() - 1);
		}
		if (job.sources.isEmpty())
		{
			m_status->setText(tr("There are no .sla files in that folder."));
			return;
		}
	}

	for (int i = 0; i < live.size(); ++i)
		m_pendingLive.insert(i, live[i]);
	saveSettings();
	m_progress = std::make_shared<DupNews::Progress>();
	std::shared_ptr<DupNews::Progress> progress = m_progress;
	m_watcher.setFuture(QtConcurrent::run([job = std::move(job), progress]() mutable {
		return DupNews::run(std::move(job), progress.get());
	}));
	setRunning(true);
	m_status->setText(tr("Checking..."));
	m_pollTimer->start();
}

void SuneerDuplicateNewsDialog::cancelCheck()
{
	if (m_progress)
		m_progress->cancel.store(true);
	m_status->setText(tr("Cancelling..."));
}

void SuneerDuplicateNewsDialog::pollProgress()
{
	if (m_progress)
		m_progressBar->setValue(m_progress->permille.load());
}

void SuneerDuplicateNewsDialog::checkFinished()
{
	m_pollTimer->stop();
	setRunning(false);
	m_result = m_watcher.result();
	if (m_result.cancelled)
	{
		m_pendingLive.clear();
		m_status->setText(tr("Check cancelled."));
		return;
	}
	m_live = m_pendingLive;
	m_pendingLive.clear();
	for (auto it = m_live.begin(); it != m_live.end(); ++it)
		it->analysed = m_result.stories[it.key()];
	m_haveResult = true;
	fillResults();
	if (isVisible())
		applyMarks();

	QString status;
	if (m_result.groups.isEmpty())
		status = tr("No duplicate news found in %1 stories (%2 s).").arg(m_result.storiesChecked).arg(m_result.elapsedMs / 1000.0, 0, 'f', 1);
	else
		status = tr("%1 groups of duplicate news found in %2 stories (%3 s). Click an item to go to it.")
		             .arg(m_result.groups.size()).arg(m_result.storiesChecked).arg(m_result.elapsedMs / 1000.0, 0, 'f', 1);
	if (!m_result.warnings.isEmpty())
		status += QLatin1Char('\n') + tr("Not read: %1").arg(m_result.warnings.join(QStringLiteral("; ")));
	m_status->setText(status);
}

QString SuneerDuplicateNewsDialog::whereText(const DupNews::Story& story) const
{
	const int page = story.firstPage();
	const QString where = (page < 0) ? tr("pasteboard") : tr("page %1").arg(page + 1);
	if (m_result.sources.size() < 2)
		return where;
	return m_result.sources[story.source].label + QStringLiteral(" · ") + where;
}

void SuneerDuplicateNewsDialog::fillResults()
{
	m_tree->clear();
	for (int g = 0; g < m_result.groups.size(); ++g)
	{
		if (m_hiddenGroups.contains(g))
			continue;
		const DupNews::Group& grp = m_result.groups[g];
		QTreeWidgetItem* top = new QTreeWidgetItem(m_tree);
		QString title = tr("Group %1: %2").arg(g + 1)
		                    .arg(grp.kind == DupNews::Kind::Headline ? tr("Headline duplicate") : tr("Content duplicate"));
		if (grp.contained)
			title += QLatin1Char(' ') + tr("(short story inside a longer one)");
		top->setText(0, title);
		top->setText(1, (grp.minScore == grp.maxScore) ? QStringLiteral("%1%").arg(grp.maxScore)
		                                                : QStringLiteral("%1–%2%").arg(grp.minScore).arg(grp.maxScore));
		top->setIcon(0, swatch(groupColor(g)));
		top->setData(0, RoleGroup, g);
		top->setData(0, RoleStory, -1);
		QFont bold = top->font(0);
		bold.setBold(true);
		top->setFont(0, bold);
		for (const DupNews::Member& m : grp.members)
		{
			const DupNews::Story& story = m_result.stories[m.story];
			QTreeWidgetItem* child = new QTreeWidgetItem(top);
			QString words = DupNews::firstWords(story);
			if (m.contained)
				words += QStringLiteral("  ") + tr("[inside a longer story]");
			child->setText(0, words);
			child->setToolTip(0, DupNews::firstWords(story, 300));
			child->setText(1, QStringLiteral("%1%").arg(m.score));
			child->setText(2, whereText(story));
			child->setText(3, story.name.isEmpty() ? tr("(unnamed)") : story.name);
			child->setData(0, RoleGroup, g);
			child->setData(0, RoleStory, m.story);
		}
		top->setExpanded(true);
	}
	for (int c = 1; c < m_tree->columnCount(); ++c)
		m_tree->resizeColumnToContents(c);
}

void SuneerDuplicateNewsDialog::applyMarks()
{
	QHash<ScribusDoc*, QList<SuneerDupHighlights::Mark>> perDoc;
	for (int g = 0; g < m_result.groups.size(); ++g)
	{
		if (m_hiddenGroups.contains(g))
			continue;
		for (const DupNews::Member& m : m_result.groups[g].members)
		{
			auto it = m_live.constFind(m.story);
			if (it == m_live.constEnd() || it->doc.isNull())
				continue;
			const DupNews::Story& found = m_result.stories[m.story];
			const DupNews::Story& here = it->analysed;
			// The passages are in the found story's head+body normalised text,
			// which equals the open story's; its own maps place them.
			const QVector<int> whole = here.headMap + here.bodyMap;
			QList<DupNews::Range> norm;
			const int offset = found.headNorm.size();
			for (const DupNews::Range& r : m.bodyNormRanges)
				norm.append(DupNews::Range(r.first + offset, r.second + offset));
			QList<DupNews::Range> ranges = DupNews::toRaw(norm, whole);
			if (m.headMatched && here.head.second > here.head.first)
				ranges.prepend(here.head);
			for (const QPointer<PageItem>& frame : it->frames)
			{
				if (frame.isNull())
					continue;
				SuneerDupHighlights::Mark mark;
				mark.item = frame;
				mark.color = groupColor(g);
				mark.group = g + 1;
				mark.ranges = ranges;
				perDoc[it->doc.data()].append(mark);
			}
		}
	}
	SuneerDupHighlights::clearAll();
	for (auto it = perDoc.cbegin(); it != perDoc.cend(); ++it)
		SuneerDupHighlights::setMarks(it.key(), it.value());
	m_marksShown = true;
	m_highlightButton->setText(tr("Clear highlights"));
	m_highlightButton->setEnabled(true);
}

void SuneerDuplicateNewsDialog::clearMarks()
{
	SuneerDupHighlights::clearAll();
	m_marksShown = false;
	m_highlightButton->setText(tr("Show highlights"));
	m_highlightButton->setEnabled(m_haveResult);
}

void SuneerDuplicateNewsDialog::toggleHighlights()
{
	if (m_marksShown)
		clearMarks();
	else if (m_haveResult)
		applyMarks();
}

bool SuneerDuplicateNewsDialog::resolveSource(int source)
{
	const QString path = QFileInfo(m_result.sources[source].filePath).canonicalFilePath();
	ScribusDoc* doc = openDocumentFor(path);
	if (!doc)
		return false;
	QSet<int> members;
	for (const DupNews::Group& g : std::as_const(m_result.groups))
	{
		for (const DupNews::Member& m : g.members)
			members.insert(m.story);
	}

	QList<DupNews::Story> stories;
	QList<LiveStory> live;
	collectDocument(doc, source, -1, stories, live);
	QMultiHash<QString, int> byText;
	for (int i = 0; i < stories.size(); ++i)
	{
		DupNews::analyse(stories[i]);
		live[i].analysed = stories[i];
		byText.insert(stories[i].headNorm + stories[i].bodyNorm, i);
	}
	// A story is found again by its normalised text, so unnamed frames and
	// frames renamed on load are found as well.
	QSet<int> used;
	for (int s : std::as_const(members))
	{
		const DupNews::Story& found = m_result.stories[s];
		if (QFileInfo(m_result.sources[found.source].filePath).canonicalFilePath() != path)
			continue;
		const QString key = found.headNorm + found.bodyNorm;
		const QList<int> candidates = byText.values(key);
		for (int c : candidates)
		{
			if (used.contains(c))
				continue;
			used.insert(c);
			m_live.insert(s, live[c]);
			// unnamed frames in the file got their names when it was loaded
			const QString name = live[c].frames.first() ? live[c].frames.first()->itemName() : QString();
			if (!name.isEmpty() && name != found.name)
			{
				m_result.stories[s].name = name;
				for (QTreeWidgetItemIterator ti(m_tree); *ti; ++ti)
				{
					if ((*ti)->data(0, RoleStory).toInt() == s)
						(*ti)->setText(3, name);
				}
			}
			break;
		}
	}
	return true;
}

bool SuneerDuplicateNewsDialog::activateDocument(ScribusDoc* doc)
{
	const QList<QMdiSubWindow*> windows = m_mainWindow->mdiArea->subWindowList();
	for (QMdiSubWindow* w : windows)
	{
		ScribusWin* sw = dynamic_cast<ScribusWin*>(w->widget());
		if (!sw || sw->doc() != doc)
			continue;
		if (w->isMinimized() || w->isShaded())
			w->showNormal();
		m_mainWindow->mdiArea->setActiveSubWindow(w);
		return m_mainWindow->doc == doc;
	}
	return false;
}

void SuneerDuplicateNewsDialog::jumpTo(int story)
{
	auto usable = [this](int s) {
		auto it = m_live.constFind(s);
		return it != m_live.constEnd() && !it->doc.isNull() && !it->frames.isEmpty()
		       && !it->frames.first().isNull() && stillInDocument(it->doc, it->frames.first());
	};
	if (!usable(story))
	{
		const DupNews::Source& src = m_result.sources[m_result.stories[story].source];
		if (src.filePath.isEmpty() || !QFileInfo::exists(src.filePath))
		{
			m_status->setText(tr("That document is closed and has no file to open again."));
			return;
		}
		if (!openDocumentFor(src.filePath))
		{
			m_status->setText(tr("Opening %1...").arg(src.label));
			if (!m_mainWindow->loadDoc(src.filePath) || !openDocumentFor(src.filePath))
			{
				m_status->setText(tr("Could not open %1.").arg(src.label));
				return;
			}
		}
		for (int s = 0; s < m_result.sources.size(); ++s)
		{
			if (QFileInfo(m_result.sources[s].filePath).canonicalFilePath() == QFileInfo(src.filePath).canonicalFilePath())
				resolveSource(s);
		}
		if (m_marksShown)
			applyMarks();
		if (!usable(story))
		{
			m_status->setText(tr("This story is no longer in %1 as it was checked. Press Check to check again.").arg(src.label));
			return;
		}
	}
	const LiveStory& ls = m_live[story];
	if (!activateDocument(ls.doc))
		return;
	m_mainWindow->selectItemsFromOutlines(ls.frames.first());
	m_status->setText(tr("%1, %2").arg(whereText(m_result.stories[story]), ls.frames.first()->itemName()));
}

void SuneerDuplicateNewsDialog::itemClicked(QTreeWidgetItem* item, int /*column*/)
{
	if (!item || m_watcher.isRunning())
		return;
	const int story = item->data(0, RoleStory).toInt();
	if (story >= 0)
		jumpTo(story);
}

void SuneerDuplicateNewsDialog::showContextMenu(const QPoint& pos)
{
	QTreeWidgetItem* item = m_tree->itemAt(pos);
	if (!item || !m_haveResult)
		return;
	const int g = item->data(0, RoleGroup).toInt();
	if (g < 0 || g >= m_result.groups.size())
		return;
	const DupNews::Group& grp = m_result.groups[g];
	QMenu menu(this);
	const int story = item->data(0, RoleStory).toInt();
	QAction* go = (story >= 0) ? menu.addAction(tr("Go to this frame")) : nullptr;
	QAction* ignore = menu.addAction(grp.kind == DupNews::Kind::Headline ? tr("Ignore this headline from now on")
	                                                                    : tr("Ignore this text from now on"));
	ignore->setToolTip(tr("For page furniture that repeats on every page, such as the masthead or the imprint."));
	QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
	if (chosen && chosen == go)
		jumpTo(story);
	else if (chosen == ignore)
	{
		for (const DupNews::Member& m : grp.members)
		{
			const DupNews::Story& s = m_result.stories[m.story];
			m_ignored.insert(DupNews::fingerprint(grp.kind == DupNews::Kind::Headline ? s.headNorm : s.bodyNorm));
		}
		m_hiddenGroups.insert(g);
		saveSettings();
		updateIgnoredLabel();
		fillResults();
		if (m_marksShown)
			applyMarks();
	}
}
