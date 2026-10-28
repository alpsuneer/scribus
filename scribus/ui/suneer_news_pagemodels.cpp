/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
// Page Models on the current page (SuneerNewsPanel members):
//  - makePageSkeleton(): one placeholder frame per slot (+ ad outlines) on the non-printing
//    layer "Page model"; nothing already on the page is moved or deleted.
//  - fillSlot(): a News Browser story built exactly inside one placeholder (drag and drop on
//    the canvas, or the placeholder selected + Place Selected); the placeholder goes.
//  - removeEmptySlots(): every placeholder / ad outline left on the page.

#include "suneer_news_panel.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QTextStream>
#include <QTimer>

#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "sccolor.h"
#include "sclayer.h"
#include "ui/layers.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneerlayoutengine.h"
#include "suneerlayoutmodel.h"
#include "suneerpagemodel.h"
#include "suneerpagemodels.h"
#include "undomanager.h"
#include "undotransaction.h"

namespace
{
const double kPmMm = 72.0 / 25.4;
const char* const kSlotAttr = "pagemodel.slot";     //!< placeholder: JSON of the slot
const char* const kAdAttr = "pagemodel.ad";         //!< ad outline
const char* const kFlagAttr = "pagemodel.flag";     //!< overflow / short mark
const char* const kLayerName = "Page model";

QString ensureColour(ScribusDoc* doc, const QString& name, int c, int m, int y, int k)   // 0..255 per channel
{
	if (!doc->PageColors.contains(name))
		doc->PageColors.insert(name, ScColor(c, m, y, k));
	return name;
}

//! The non-printing layer the skeleton lives on (made when missing). The main window's layer
//! actions must know it: an undo that re-selects an item on an unknown layer crashed in
//! updateItemLayerList (null QAction).
int skeletonLayer(ScribusDoc* doc, ScribusMainWindow* mw)
{
	int id = doc->layerIDFromName(QString::fromLatin1(kLayerName));
	if (id < 0)
	{
		id = doc->addLayer(QString::fromLatin1(kLayerName), false);
		doc->setLayerPrintable(id, false);
		doc->setLayerSelectable(id, true);         // placeholders can be clicked without making the layer active
		if (mw)
		{
			mw->rebuildLayersList();
			if (mw->layerPalette)
				mw->layerPalette->rebuildList();
		}
	}
	return id;
}

QJsonObject slotJson(const SuneerPageSlot& s)
{
	QJsonObject o;
	o["n"] = s.n;
	o["x"] = s.rect.x();
	o["y"] = s.rect.y();
	o["w"] = s.rect.width();
	o["h"] = s.rect.height();
	o["cols"] = s.cols;
	o["bodyCols"] = s.bodyCols;
	o["headStyle"] = s.headStyle;
	o["headSize"] = s.headSize;
	o["headLines"] = s.headLines;
	if (!s.photos.isEmpty())
	{
		o["photoCols"] = s.photos.first().cols;
		o["photoPos"] = s.photos.first().pos;
		o["photoH"] = s.photos.first().rect.height();
	}
	return o;
}

QString slotLabel(const SuneerPageSlot& s)
{
	QString t = QObject::tr("Slot %1 – %2 col").arg(s.n).arg(s.cols);
	if (!s.photos.isEmpty())
		t += QObject::tr(" + photo %1 col").arg(s.photos.first().cols);
	if (s.n == 1)
		t += QObject::tr(" (lead)");
	return t;
}
}

bool SuneerNewsPanel::isSlotPlaceholder(const PageItem* item)
{
	return item && !attr(item, kSlotAttr).isEmpty();
}

// The ads on a page now (mm, page relative): modules the detection calls adverts, and
// every printable frame on a layer named "Advt" that is not part of a story.
QList<QRectF> SuneerNewsPanel::pageAdRectsMm(int pageIdx) const
{
	QList<QRectF> out;
	if (!m_doc || pageIdx < 0 || pageIdx >= m_doc->DocPages.count())
		return out;
	const ScPage* page = m_doc->DocPages.at(pageIdx);
	const QString titleStyle = panelTitleStyle();
	const QList<SuneerLayout::Module> mods = SuneerLayout::detectModules(m_doc, pageIdx, SuneerLayout::StyleNames::fromSettings(titleStyle));
	QSet<PageItem*> storyItems;
	for (const SuneerLayout::Module& m : mods)
		if (!m.fixed)
			for (const QPointer<PageItem>& p : m.items)
				if (p)
					storyItems.insert(p.data());
	auto mm = [&](const QRectF& r) { return QRectF((r.x() - page->xOffset()) / kPmMm, (r.y() - page->yOffset()) / kPmMm, r.width() / kPmMm, r.height() / kPmMm); };
	QSet<PageItem*> done;
	for (const SuneerLayout::Module& m : mods)
		if (m.fixed && m.reason == QLatin1String("advert"))
			for (const QPointer<PageItem>& p : m.items)
				if (p && !done.contains(p.data()) && p->printEnabled())
				{
					done.insert(p.data());
					out << mm(p->getBoundingRect());
				}
	const double mTop = page->Margins.top() / kPmMm, mBottom = (page->height() - page->Margins.bottom()) / kPmMm;
	for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIdx))
	{
		if (done.contains(it) || storyItems.contains(it))
			continue;
		if (!it->printEnabled() || !attr(it, kFlagAttr).isEmpty() || !attr(it, kSlotAttr).isEmpty() || !attr(it, kAdAttr).isEmpty())
			continue;                                     // page model skeleton / marks, non-printing helpers
		const QString layer = m_doc->layerName(it->m_layerID);
		const bool advtLayer = layer.contains(QLatin1String("advt"), Qt::CaseInsensitive);
		const bool advtName = it->itemName().contains(QLatin1String("advt"), Qt::CaseInsensitive);
		if (!advtLayer && !advtName)
			continue;
		const QRectF r = mm(it->getBoundingRect());
		if (r.width() * r.height() < 400.0)
			continue;                                     // logos, folio bits
		if (r.bottom() <= mTop + 2.0 || r.top() >= mBottom - 2.0)
			continue;                                     // masthead / folio on the Advt layer
		done.insert(it);
		out << r;
	}
	return out;
}

QList<PageItem*> SuneerNewsPanel::skeletonItems(int pageIdx) const
{
	QList<PageItem*> out;
	if (!m_doc)
		return out;
	for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIdx))
		if (!attr(it, kSlotAttr).isEmpty() || !attr(it, kAdAttr).isEmpty())
			out << it;
	return out;
}

void SuneerNewsPanel::openPageModelGallery()
{
	SuneerPageModelGallery::Source src;
	src.doc = m_doc;
	src.pageIndex = (m_doc && m_doc->currentPage()) ? m_doc->currentPageNumber() : -1;
	if (m_doc)
	{
		src.docFile = m_doc->documentFileName();
		if (m_doc->currentPage())
			src.savedGroup = m_doc->currentPage()->suneerGroup();
		if (src.pageIndex >= 0)
			src.pageAds = pageAdRectsMm(src.pageIndex);
	}
	src.apply = [this](const SuneerPageModel& model, const SuneerPageGroup& group) {
		return makePageSkeleton(model, group, true);
	};
	src.removeEmptySlots = [this]() { return removeEmptySlots(true); };
	SuneerPageModelGallery dlg(src, this);
	dlg.exec();
}

/*!
 The skeleton of a page model on the current page: one placeholder text frame per slot (light
 grey, thin dashed border, "Slot 3 - 4 col + photo 3 col" inside) and one outline per ad space,
 all on the non-printing layer "Page model". Nothing already on the page is moved or deleted.
 With a skeleton on the page already, \a interactive asks "Replace skeleton?". One undo step.
 Returns the report; empty when cancelled.
 */
QString SuneerNewsPanel::makePageSkeleton(const SuneerPageModel& model, const SuneerPageGroup& group, bool interactive)
{
	if (!m_doc || !m_doc->currentPage())
		return QString();
	ScPage* page = m_doc->currentPage();
	const int pageIdx = m_doc->currentPageNumber();
	const QPointF off(page->xOffset(), page->yOffset());
	const QList<PageItem*> old = skeletonItems(pageIdx);
	if (!old.isEmpty() && interactive)
	{
		const int r = QMessageBox::question(m_mw ? static_cast<QWidget*>(m_mw) : this, tr("Replace skeleton?"),
		                                    tr("This page already has a page model skeleton (%n placeholder(s)). Replace it with \"%1\"?", "", old.size()).arg(model.name),
		                                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
		if (r != QMessageBox::Yes)
			return QString();
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Page model skeleton: %1").arg(model.name), QString(), Um::ICreate);
	m_doc->m_Selection->clear();
	if (!old.isEmpty())
	{
		Selection sel(this, false);
		for (PageItem* it : old)
			sel.addItem(it);
		m_doc->itemSelection_DeleteItem(&sel);
	}
	const int layer = skeletonLayer(m_doc, m_mw);
	const int activeBefore = m_doc->activeLayer();
	const QString fill = ensureColour(m_doc, QStringLiteral("PM Slot Fill"), 0, 0, 0, 18);
	const QString line = ensureColour(m_doc, QStringLiteral("PM Slot"), 0, 0, 0, 110);
	const QString adLine = ensureColour(m_doc, QStringLiteral("PM Ad"), 0, 120, 200, 0);
	auto frameAt = [&](const QRectF& mm, double lineW, const QString& fillC, const QString& lineC) -> PageItem* {
		const QRectF r(off.x() + mm.x() * kPmMm, off.y() + mm.y() * kPmMm, mm.width() * kPmMm, mm.height() * kPmMm);
		const int idx = m_doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, r.x(), r.y(), r.width(), r.height(), lineW, fillC, lineC);
		PageItem* it = idx >= 0 ? m_doc->Items->at(idx) : nullptr;
		if (!it)
			return nullptr;
		it->setFillColor(fillC);
		it->setLineColor(lineC);
		it->setLineWidth(lineW);
		it->setLineStyle(Qt::DashLine);
		it->setPrintEnabled(false);
		it->setTextFlowMode(PageItem::TextFlowDisabled);
		it->m_layerID = layer;
		return it;
	};
	int nSlots = 0, ads = 0;
	for (const SuneerPageSlot& s : model.storySlots)
	{
		PageItem* it = frameAt(s.rect, 0.5, fill, line);
		if (!it)
			continue;
		it->setItemName(QStringLiteral("PageModel slot %1").arg(s.n));
		setAttr(it, kSlotAttr, QString::fromUtf8(QJsonDocument(slotJson(s)).toJson(QJsonDocument::Compact)));
		setAttr(it, "pagemodel.model", model.name);
		it->setTextToFrameDist(4, 4, 4, 4);
		it->itemText.insertChars(0, slotLabel(s));
		it->invalidateLayout();
		it->layout();
		++nSlots;
	}
	for (const SuneerPageModel::Box& b : model.ads)
	{
		PageItem* it = frameAt(b.rect, 0.8, CommonStrings::None, adLine);
		if (!it)
			continue;
		it->setItemName(QStringLiteral("PageModel ad space"));
		setAttr(it, kAdAttr, QStringLiteral("%1x%2").arg(qRound(b.rect.width())).arg(qRound(b.rect.height())));
		it->setTextToFrameDist(3, 3, 3, 3);
		it->itemText.insertChars(0, tr("Ad space %1 x %2 mm").arg(qRound(b.rect.width())).arg(qRound(b.rect.height())));
		it->invalidateLayout();
		it->layout();
		++ads;
	}
	m_doc->setActiveLayer(activeBefore);
	page->setSuneerGroup(group.id);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	const QString report = tr("Skeleton of \"%1\" (%2) on page %3: %n placeholder slot(s)", "", nSlots).arg(model.name, group.name).arg(pageIdx + 1)
	                       + (ads ? tr(", %n ad outline(s)", "", ads) : QString())
	                       + (old.isEmpty() ? QString() : tr("; the earlier skeleton (%n frame(s)) was replaced", "", old.size()))
	                       + tr(".\nDrag a story from the News Browser onto a slot, or select a slot and press Place Selected with one story ticked. "
	                            "Layout > Remove Empty Slots deletes what is left; the layer \"Page model\" never prints.");
	apiLog(QStringLiteral("page model skeleton %1/%2: %3 slots, %4 ads, %5 old frames replaced").arg(group.id, model.id).arg(nSlots).arg(ads).arg(old.size()));
	return report;
}

/*!
 Builds \a news exactly inside the placeholder \a placeholder: headline style from the slot width
 (the house rules of the Normal / Obituary layout model), photo columns and position from the
 slot, text fitted (trailing empty lines removed by placeNews); a story taller than the slot is cut
 to it and marked (red outline; the text stays, overflowing), one much shorter is marked yellow.
 The placeholder is removed. One undo step. Returns the report line; empty on failure.
 */
QString SuneerNewsPanel::fillSlot(PageItem* placeholder, QJsonObject news)
{
	if (!m_doc || !placeholder || !isSlotPlaceholder(placeholder))
		return QString();
	const QJsonObject slot = QJsonDocument::fromJson(attr(placeholder, kSlotAttr).toUtf8()).object();
	if (slot.isEmpty())
		return QString();
	if (!ensureFullStory(news))
		return QString();
	const int pageIdx = placeholder->OwnPage >= 0 ? placeholder->OwnPage : m_doc->currentPageNumber();
	if (pageIdx < 0 || pageIdx >= m_doc->DocPages.count())
		return QString();
	ScPage* page = m_doc->DocPages.at(pageIdx);
	const QString groupId = page->suneerGroup();
	const SuneerPageGroup group = SuneerPageModels::group(groupId);
	const QString gridModelId = group.isObituary() ? QStringLiteral("obituary") : QStringLiteral("normal");
	const SuneerLayoutModel gridModel = SuneerLayoutModels::byId(gridModelId);
	const int n = slot["n"].toInt();
	// columns from the placeholder's width in the group's grid (the slot may come from another grid, e.g. an
	// 8-column slot on the 10-column obituary grid): the widest module that stays inside the slot
	const SuneerLayout::Grid grid = SuneerLayout::gridFor(gridModel, page->width(), page->height(), page->Margins.top(), page->Margins.bottom(),
	                                                      page->Margins.left(), page->Margins.right(), page->guides.verticalAutoCount() > 0 ? page->guides.verticalAutoCount() + 1 : 0);
	int cols = 1;
	for (int c = 2; c <= qMax(1, grid.cols); ++c)
		if (grid.width(c) <= placeholder->width() + 1.0)
			cols = c;
	const int photoCols = slot["photoCols"].toInt(0);
	const QString pos = photoCols > 0 ? slot["photoPos"].toString(QStringLiteral("right")) : QStringLiteral("none");
	// the placeholder's rectangle is the slot (the user may have moved / resized it)
	const double x = placeholder->xPos(), y = placeholder->yPos();
	const double slotW = placeholder->width(), slotH = placeholder->height();
	const double slotBottom = y + slotH;
	const int phLayer = placeholder->m_layerID;
	const QString title = collapseSpaces(news["title"].toString()).left(40);

	m_modelOverride = gridModelId;
	// the photo no taller than the slot's photo: the grid model with that maximum photo height, for this placement only
	SuneerLayoutModel slotModel = gridModel;
	const double photoH = slot["photoH"].toDouble();
	if (photoCols > 0 && photoH > 5.0)
		slotModel.photoMaxHeightMm = photoH;
	SuneerLayoutModels::setEditing(&slotModel);
	struct Reset
	{
		SuneerNewsPanel* p;
		~Reset() { SuneerLayoutModels::setEditing(nullptr); p->m_modelOverride.clear(); p->m_forceCols = 0; p->m_forceImgCols = 0; p->m_forcePos.clear(); p->m_forceTitle.clear(); }
	} reset { this };
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Slot %1: %2").arg(n).arg(title), QString(), Um::ICreate);
	const int layerBefore = m_doc->activeLayer();
	if (phLayer == m_doc->activeLayer())
	{
		// never onto the non-printing skeleton layer: the first printable layer
		for (const ScLayer& l : m_doc->Layers)
			if (l.isPrintable && l.ID != phLayer)
			{
				m_doc->setActiveLayer(l.ID);
				break;
			}
	}
	m_forceCols = cols;
	m_forceImgCols = qBound(0, photoCols, cols);
	m_forcePos = pos;
	m_forceTitle = gridModel.headlineStyleFor(cols, n == 1);     // headline size from the slot width (house rules)
	const QList<PageItem*> items = placeNews(news, x, y, false);
	m_forceCols = 0;
	m_forceImgCols = 0;
	m_forcePos.clear();
	m_forceTitle.clear();
	m_doc->setActiveLayer(layerBefore);
	if (items.isEmpty())
	{
		if (tx)
			tx.cancel();
		return QString();
	}
	for (PageItem* it : items)
		setAttr(it, "pagemodel.slotNo", QString::number(n));
	// never outside the slot rectangle
	const QRectF real = itemsRect(items);
	const QString red = ensureColour(m_doc, QStringLiteral("PM Overflow"), 0, 255, 255, 0);
	const QString yellow = ensureColour(m_doc, QStringLiteral("PM Short"), 0, 0, 255, 0);
	auto mark = [&](const QString& colour, const QString& kind) {
		const int idx = m_doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, x, y, slotW, slotH, 1.5, CommonStrings::None, colour);
		PageItem* it = idx >= 0 ? m_doc->Items->at(idx) : nullptr;
		if (!it)
			return;
		it->setFillColor(CommonStrings::None);
		it->setLineColor(colour);
		it->setLineWidth(1.5);
		it->setPrintEnabled(false);
		it->setTextFlowMode(PageItem::TextFlowDisabled);
		it->m_layerID = phLayer;
		it->setItemName(QStringLiteral("PageModel %1 %2").arg(kind).arg(n));
		setAttr(it, kFlagAttr, kind);
	};
	QString verdict;
	if (real.bottom() > slotBottom + 1.0)
	{
		PageItem* text = nullptr;
		for (PageItem* it : items)
			if (attr(it, "news.role") == QLatin1String("text") && it->isTextFrame())
				text = it;
		bool cut = false;
		if (text && text->yPos() < slotBottom - 20.0)
		{
			m_doc->sizeItem(text->width(), slotBottom - text->yPos(), text, false, true, false);
			text->invalidateLayout();
			text->layout();
			cut = true;
		}
		// photos / captions below the slot are pulled up to its bottom edge
		for (PageItem* it : items)
			if (it != text && it->yPos() + it->height() > slotBottom + 0.5)
				moveStory({ it }, 0, slotBottom - (it->yPos() + it->height()));
		verdict = cut ? tr("overflow: %1 mm more text than the slot (frame cut to the slot, text kept, red)").arg(qRound((real.bottom() - slotBottom) / kPmMm))
		              : tr("%1 mm taller than the slot (red)").arg(qRound((real.bottom() - slotBottom) / kPmMm));
		mark(red, QStringLiteral("overflow"));
	}
	else if (slotBottom - real.bottom() > 8.0 * kPmMm)
	{
		verdict = tr("%1 mm short (yellow)").arg(qRound((slotBottom - real.bottom()) / kPmMm));
		mark(yellow, QStringLiteral("short"));
	}
	else
		verdict = tr("fits");
	{
		Selection sel(this, false);
		sel.addItem(placeholder);
		m_doc->itemSelection_DeleteItem(&sel);
	}
	recordPlaceUndo({ news });
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	markPlaced({ news });
	const QString line = tr("Slot %1 ← %2: %3").arg(n).arg(title, verdict);
	m_lastSlotReport = line;
	setStatus(line);
	apiLog(QStringLiteral("page model slot %1: '%2' %3").arg(n).arg(title, verdict));
	return line;
}

//! Drop of a News Browser row (its m_newsData index) on the canvas at a placeholder.
bool SuneerNewsPanel::dropStoryOnItem(PageItem* item, int newsIndex)
{
	if (!m_doc || !isSlotPlaceholder(item) || newsIndex < 0 || newsIndex >= m_newsData.size())
		return false;
	const QJsonObject news = m_newsData[newsIndex].toObject();
	if (findStoryItemInDoc(newsServerId(news)))
	{
		setStatus(tr("\"%1\" is already placed in this document").arg(collapseSpaces(news["title"].toString()).left(40)));
		return false;
	}
	return !fillSlot(item, news).isEmpty();
}

//! Place Selected with one placeholder selected: the one ticked story goes into it. False: not that case.
bool SuneerNewsPanel::placeIntoSelectedSlot(const QList<QJsonObject>& stories)
{
	if (!m_doc || m_doc->m_Selection->count() != 1)
		return false;
	PageItem* ph = m_doc->m_Selection->itemAt(0);
	if (!isSlotPlaceholder(ph))
		return false;
	if (stories.size() != 1)
	{
		setStatus(tr("A slot is selected: tick exactly one story to place into it (%n ticked)", "", stories.size()));
		return true;
	}
	fillSlot(ph, stories.first());
	return true;
}

//! Right-click on a placeholder: select it and run Place Selected (the one ticked story goes into it).
void SuneerNewsPanel::placeTickedStoryInto(PageItem* placeholder)
{
	if (!m_doc || !isSlotPlaceholder(placeholder))
		return;
	m_doc->m_Selection->clear();
	m_doc->m_Selection->addItem(placeholder);
	onPlaceSelected();
}

// ---------------------------------------------------------------------------
// The draggable News Browser list
// ---------------------------------------------------------------------------

SuneerNewsListWidget::SuneerNewsListWidget(QWidget* parent) : QListWidget(parent)
{
	setDragEnabled(true);
	setDragDropMode(QAbstractItemView::DragOnly);
	setDefaultDropAction(Qt::CopyAction);
}

QStringList SuneerNewsListWidget::mimeTypes() const
{
	return { QString::fromLatin1(mimeType()), QStringLiteral("text/plain") };
}

QMimeData* SuneerNewsListWidget::mimeData(const QList<QListWidgetItem*>& items) const
{
	if (items.isEmpty())
		return nullptr;
	auto* md = new QMimeData();
	md->setData(QString::fromLatin1(mimeType()), QByteArray::number(items.first()->data(Qt::UserRole).toInt()));
	md->setText(items.first()->text());
	return md;
}

//! Layout > Remove Empty Slots: every placeholder and ad outline left on the current page. One undo step.
int SuneerNewsPanel::removeEmptySlots(bool interactive)
{
	if (!m_doc || !m_doc->currentPage())
		return 0;
	const QList<PageItem*> items = skeletonItems(m_doc->currentPageNumber());
	if (items.isEmpty())
	{
		if (interactive)
			setStatus(tr("No empty slot on this page"));
		return 0;
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Remove empty slots"), QString(), Um::IDelete);
	Selection sel(this, false);
	for (PageItem* it : items)
		sel.addItem(it);
	m_doc->itemSelection_DeleteItem(&sel);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	if (interactive)
		setStatus(tr("%n empty slot(s) / ad outline(s) removed", "", items.size()));
	return items.size();
}

// ---------------------------------------------------------------------------
// Regression tests (tests/suneer/page_models_import.sh, page_models_apply.sh)
// ---------------------------------------------------------------------------

void SuneerNewsPanel::runPageModelImportTest(const QString& outPath)
{
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		fprintf(stderr, "PAGEMODEL-IMPORT cannot write %s\n", qPrintable(outPath));
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	QTextStream ts(&out);
	auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "PAGEMODEL-IMPORT %s\n", qPrintable(l)); };
	const int hold = qEnvironmentVariable("SUNEER_PAGEMODEL_HOLD").toInt();
	const QStringList files = qEnvironmentVariable("SUNEER_PAGEMODEL_IMPORT").split(';', Qt::SkipEmptyParts);
	auto* dlg = new SuneerPageModelImportDialog(QStringLiteral("page-1"), m_mw ? static_cast<QWidget*>(m_mw) : this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setFiles(files);
	dlg->show();
	dlg->scan();
	const bool finished = dlg->waitForScan(180000);
	line(QStringLiteral("reader %1 for %2 file(s)/folder(s): %3 page row(s)").arg(finished ? QStringLiteral("finished") : QStringLiteral("TIMED OUT")).arg(files.size()).arg(dlg->rows().size()));
	int overlaps = 0;
	for (const SuneerPageModelImportDialog::Row& r : dlg->rows())
	{
		if (!r.error.isEmpty())
		{
			line(QStringLiteral("  %1 page %2: ERROR %3").arg(QFileInfo(r.file).fileName()).arg(r.page + 1).arg(r.error));
			continue;
		}
		const QList<SuneerPageSlot>& sl = r.model.storySlots;
		for (int i = 0; i < sl.size(); ++i)
			for (int j = i + 1; j < sl.size(); ++j)
			{
				const QRectF ov = sl[i].rect.intersected(sl[j].rect);
				if (ov.width() > 0.5 && ov.height() > 0.5)
					++overlaps;
			}
		line(QStringLiteral("  %1 page %2/%3 -> group %4 (%5); name '%6'; %7; grid %8")
		         .arg(QFileInfo(r.file).fileName()).arg(r.page + 1).arg(r.pages).arg(r.group ? r.group->currentData().toString() : r.suggested, r.why, r.name ? r.name->text() : r.model.name, r.model.summary(), r.model.gridType));
	}
	line(QStringLiteral("overlapping slot pairs over all pages: %1").arg(overlaps));
	const int n = dlg->importTicked(false);
	line(QStringLiteral("imported %1 model(s) into %2; dir %3").arg(n).arg(dlg->importedGroups().join(QStringLiteral(", ")), SuneerPageModels::dir()));
	for (const SuneerPageGroup& g : SuneerPageModels::groups())
		if (SuneerPageModels::modelCount(g.id) > 0)
			line(QStringLiteral("  group %1 (%2): %3 model(s)").arg(g.id, g.name).arg(SuneerPageModels::modelCount(g.id)));
	line(QStringLiteral("RESULT %1").arg(finished && n > 0 && overlaps == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
	QTimer::singleShot(qMax(0, hold) * 1000, qApp, [finished, n, overlaps]() { qApp->exit(finished && n > 0 && overlaps == 0 ? 0 : 1); });
}

// SUNEER_PAGEMODEL_SELFTEST=<report> SUNEER_PAGEMODEL=<group>/<id>: skeleton on the opened page (twice: the
// second replaces the first), the first SUNEER_PAGEMODEL_TICK fixture stories into the first slots (fillSlot,
// the drop / Place Selected path), save as SUNEER_PAGEMODEL_SAVEAS, Remove Empty Slots, undo back to the start.
void SuneerNewsPanel::runPageModelSelfTest(const QString& outPath)
{
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		fprintf(stderr, "PAGEMODEL-SELFTEST cannot write %s\n", qPrintable(outPath));
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	QTextStream ts(&out);
	auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "PAGEMODEL-SELFTEST %s\n", qPrintable(l)); };
	const int hold = qEnvironmentVariable("SUNEER_PAGEMODEL_HOLD").toInt();
	auto finish = [hold](int code) { QTimer::singleShot(qMax(0, hold) * 1000, qApp, [code]() { qApp->exit(code); }); };
	if (!m_doc || !m_doc->currentPage())
	{
		line(QStringLiteral("FAIL no document"));
		finish(2);
		return;
	}
	const QString spec = qEnvironmentVariable("SUNEER_PAGEMODEL");
	const QString groupId = spec.section('/', 0, 0), modelId = spec.section('/', 1);
	bool ok = false;
	const SuneerPageModel model = SuneerPageModels::load(groupId, modelId, &ok);
	if (!ok)
	{
		line(QStringLiteral("FAIL no page model %1 (%2)").arg(spec, SuneerPageModels::filePath(groupId, modelId)));
		finish(2);
		return;
	}
	const SuneerPageGroup group = SuneerPageModels::group(groupId);
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		onFetchNews();
	const int pageIdx = m_doc->currentPageNumber();
	const int itemsBefore = m_doc->Items->count();
	const QList<PageItem*> origItems = SuneerLayout::pageTopItems(m_doc, pageIdx);
	auto origSig = [&]() {
		QStringList l;
		for (PageItem* it : origItems)
			l << QStringLiteral("%1,%2,%3,%4").arg(it->xPos(), 0, 'f', 1).arg(it->yPos(), 0, 'f', 1).arg(it->width(), 0, 'f', 1).arg(it->height(), 0, 'f', 1);
		return l;
	};
	const QStringList before = origSig();
	line(QStringLiteral("document %1 page %2, %3 item(s), saved group '%4'; model %5/%6 '%7' (%8 slots, %9 obits, %10 ads)")
	         .arg(m_doc->documentFileName()).arg(pageIdx + 1).arg(itemsBefore).arg(m_doc->currentPage()->suneerGroup()).arg(groupId, modelId, model.name)
	         .arg(model.storySlots.size()).arg(model.obits.size()).arg(model.ads.size()));
	// 1. skeleton, twice: the second replaces the first
	QString rep = makePageSkeleton(model, group, false);
	line(QStringLiteral("skeleton 1: %1").arg(rep.section('\n', 0, 0)));
	rep = makePageSkeleton(model, group, false);
	line(QStringLiteral("skeleton 2: %1").arg(rep.section('\n', 0, 0)));
	const QList<PageItem*> skel = skeletonItems(pageIdx);
	int placeholders = 0, adOutlines = 0, printable = 0;
	for (PageItem* it : skel)
	{
		if (!attr(it, kSlotAttr).isEmpty()) ++placeholders;
		if (!attr(it, kAdAttr).isEmpty()) ++adOutlines;
		if (it->printEnabled() || m_doc->layerPrintable(it->m_layerID)) ++printable;
	}
	const bool untouched = origSig() == before;
	line(QStringLiteral("skeleton on page: %1 placeholders, %2 ad outlines, %3 printable (must be 0), layer '%4', page group '%5', existing frames %6")
	         .arg(placeholders).arg(adOutlines).arg(printable).arg(skel.isEmpty() ? QString() : m_doc->layerName(skel.first()->m_layerID), m_doc->currentPage()->suneerGroup())
	         .arg(untouched ? QStringLiteral("all where they were") : QStringLiteral("MOVED")));
	// 2. the first N free fixture stories into slots 1..N (the drop path)
	const int fillN = qMax(0, qEnvironmentVariable("SUNEER_PAGEMODEL_TICK").toInt());
	QList<int> freeIdx;
	for (int i = 0; m_newsList && i < m_newsList->count(); ++i)
	{
		QListWidgetItem* row = m_newsList->item(i);
		if (row->isHidden() || isPlacedRow(row))
			continue;
		freeIdx << row->data(Qt::UserRole).toInt();
	}
	int filled = 0, mlChars = 0;
	for (int k = 0; k < fillN && k < freeIdx.size(); ++k)
	{
		PageItem* ph = nullptr;
		for (PageItem* it : skeletonItems(pageIdx))
			if (QJsonDocument::fromJson(attr(it, kSlotAttr).toUtf8()).object()["n"].toInt() == k + 1)
				ph = it;
		if (!ph)
			break;
		const bool okDrop = dropStoryOnItem(ph, freeIdx[k]);
		qApp->processEvents();
		line(QStringLiteral("  drop on slot %1: %2").arg(k + 1).arg(okDrop ? m_lastSlotReport : QStringLiteral("FAILED")));
		if (okDrop)
			++filled;
	}
	int flags = 0, left = 0;
	QSet<QString> storyIds;
	for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIdx))
	{
		if (!attr(it, kFlagAttr).isEmpty()) ++flags;
		if (!attr(it, kSlotAttr).isEmpty()) ++left;
		const QString sid = attr(it, "news.story");
		if (!sid.isEmpty()) storyIds.insert(sid);
		if (it->isTextFrame())
			for (int i = 0; i < it->itemText.length(); ++i)
			{
				const ushort u = it->itemText.text(i).unicode();
				if (u >= 0x0D00 && u <= 0x0D7F) ++mlChars;
			}
	}
	line(QStringLiteral("after filling %1 slot(s): %2 news stories on the page, %3 placeholders left, %4 marks, Malayalam codepoints %5")
	         .arg(filled).arg(storyIds.size()).arg(left).arg(flags).arg(mlChars));
	// every frame of a filled slot inside the slot rectangle?
	int outside = 0;
	const ScPage* page = m_doc->currentPage();
	for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIdx))
	{
		const int sn = attr(it, "pagemodel.slotNo").toInt();
		if (sn <= 0 || sn > model.storySlots.size())
			continue;
		const QRectF s = model.storySlots[sn - 1].rect;
		const QRectF slotR(page->xOffset() + s.x() * kPmMm, page->yOffset() + s.y() * kPmMm, s.width() * kPmMm, s.height() * kPmMm);
		const QRectF r(it->xPos(), it->yPos(), it->width(), it->height());
		if (r.left() < slotR.left() - 1 || r.right() > slotR.right() + 1 || r.top() < slotR.top() - 1 || r.bottom() > slotR.bottom() + 1)
		{
			++outside;
			line(QStringLiteral("  OUTSIDE slot %1: %2 at %3,%4 %5x%6 pt vs slot %7,%8 %9x%10").arg(sn).arg(it->itemName()).arg(r.x(), 0, 'f', 0).arg(r.y(), 0, 'f', 0)
			         .arg(r.width(), 0, 'f', 0).arg(r.height(), 0, 'f', 0).arg(slotR.x(), 0, 'f', 0).arg(slotR.y(), 0, 'f', 0).arg(slotR.width(), 0, 'f', 0).arg(slotR.height(), 0, 'f', 0));
		}
	}
	line(QStringLiteral("frames outside their slot rectangle: %1").arg(outside));
	const QString saveAs = qEnvironmentVariable("SUNEER_PAGEMODEL_SAVEAS");
	if (!saveAs.isEmpty())
		line(QStringLiteral("saved %1: %2").arg(saveAs, m_doc->save(saveAs) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
	// 3. the empty slots go
	const int removed = removeEmptySlots(false);
	line(QStringLiteral("Remove Empty Slots: %1 removed, %2 placeholders left").arg(removed).arg(skeletonItems(pageIdx).size()));
	// 4. undo it all: remove, each fill, skeleton 2, skeleton 1
	if (!qEnvironmentVariableIsEmpty("SUNEER_PAGEMODEL_UNDO"))
	{
		const int steps = (removed ? 1 : 0) + filled + 2;
		for (int i = 0; i < steps; ++i)
		{
			UndoManager::instance()->undo(1);
			qApp->processEvents();
		}
		line(QStringLiteral("after %1 undo steps: %2 items (before %3), original frames %4").arg(steps).arg(m_doc->Items->count()).arg(itemsBefore)
		         .arg(origSig() == before ? QStringLiteral("as before") : QStringLiteral("DIFFER")));
	}
	const bool pass = placeholders == model.storySlots.size() && printable == 0 && untouched && outside == 0 && filled == qMin(fillN, freeIdx.size());
	line(QStringLiteral("RESULT %1").arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
	finish(pass ? 0 : 1);
}
