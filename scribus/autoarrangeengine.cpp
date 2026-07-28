/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "autoarrangeengine.h"

#include <QDebug>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <algorithm>

#include "guidemanagercore.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "sclayer.h"
#include "scpage.h"
#include "scribusdoc.h"
#include "undomanager.h"
#include "units.h"

namespace
{
	// Short frame descriptor for logging.
	QString desc(PageItem* f)
	{
		return QString("%1 [type=%2 textLen=%3]")
			.arg(f->itemName())
			.arg(static_cast<int>(f->itemType()))
			.arg(f->isTextFrame() ? f->itemText.length() : 0);
	}
}

// ── Protection & gathering ──────────────────────────────────────────────────

bool AutoArrangeEngine::canArrangeFrame(ScribusDoc* doc, PageItem* item, const ArrangeOptions& opts)
{
	if (!item)
		return false;
	// Master-page items (page border, masthead, page number, section headers)
	// carry a non-empty OnMasterPage and live in the master-item list, so they
	// can never be reached here — reject them explicitly too.
	if (opts.protectMasterPageItems && !item->OnMasterPage.isEmpty())
		return false;
	if (opts.protectLockedFrames && item->locked())
		return false;
	if (opts.protectLockedLayers && doc)
	{
		const ScLayer* layer = doc->Layers.layerByID(item->m_layerID);
		if (layer && (!layer->isEditable || !layer->isViewable))
			return false;
	}
	const PageItem::ItemType t = item->itemType();
	if (t == PageItem::TextFrame)
		return opts.includeTextFrames;
	if (t == PageItem::ImageFrame)
		return opts.includeImageFrames;
	if (t == PageItem::Group)
		return opts.includeGroups;   // grouped article = one movable unit
	return false;
}

QList<PageItem*> AutoArrangeEngine::gatherFrames(ScribusDoc* doc, ScPage* page, const ArrangeOptions& opts)
{
	QList<PageItem*> result;
	if (!doc || !page)
		return result;
	const int pnr = page->pageNr();
	int onPage = 0, skippedGroupChild = 0, skippedProtected = 0, skippedType = 0;
	int nText = 0, nImage = 0, nGroup = 0;
	for (PageItem* it : doc->DocItems)   // regular-page items only; masters are separate
	{
		if (!it || it->OwnPage != pnr)
			continue;
		++onPage;
		if (it->isGroupChild())
		{
			++skippedGroupChild;   // moves with its parent group
			continue;
		}
		if (opts.protectMasterPageItems && !it->OnMasterPage.isEmpty())
		{
			++skippedProtected;
			continue;
		}
		if (!canArrangeFrame(doc, it, opts))
		{
			++skippedType;   // wrong type / locked / locked layer
			continue;
		}
		if (it->isTextFrame())  ++nText;
		else if (it->isImageFrame()) ++nImage;
		else if (it->isGroup()) ++nGroup;
		result.append(it);
	}
	qDebug() << "[AutoArrange] page" << pnr << "top-level items:" << onPage
	         << "| arrangeable:" << result.size()
	         << QString("(text=%1 image=%2 group=%3)").arg(nText).arg(nImage).arg(nGroup)
	         << "| skipped: groupChildren=" << skippedGroupChild
	         << "protected/master=" << skippedProtected
	         << "wrongType/locked=" << skippedType;
	if (result.isEmpty())
		qWarning() << "[AutoArrange] Nothing to arrange on this page. If your articles are GROUPED,"
		           << "keep 'Include grouped articles' enabled; grouped article contents are moved as a unit.";
	return result;
}

// ── Boundary & column detection ─────────────────────────────────────────────

QRectF AutoArrangeEngine::detectContentArea(ScPage* page)
{
	QRectF area(page->xOffset() + page->Margins.left(),
	            page->yOffset() + page->Margins.top(),
	            page->width()  - page->Margins.left() - page->Margins.right(),
	            page->height() - page->Margins.top()  - page->Margins.bottom());

	// Tighten top/bottom to the innermost horizontal guides near the margins
	// (the "blue rectangle" content boundary in a newspaper layout).
	Guides h = page->guides.horizontals(GuideManagerCore::Standard);
	if (h.size() >= 2)
	{
		std::sort(h.begin(), h.end());
		const double topGuide = page->yOffset() + h.first();
		const double botGuide = page->yOffset() + h.last();
		if (topGuide > area.top() && topGuide < area.top() + 50.0)
			area.setTop(topGuide);
		if (botGuide < area.bottom() && botGuide > area.bottom() - 50.0)
			area.setBottom(botGuide);
	}
	qDebug() << "[AutoArrange] content area (pt): left" << area.left() << "top" << area.top()
	         << "right" << area.right() << "bottom" << area.bottom()
	         << "size" << area.width() << "x" << area.height();
	return area;
}

QList<ArrangeColumn> AutoArrangeEngine::detectColumns(ScribusDoc* doc, ScPage* page, const QRectF& contentArea)
{
	QList<ArrangeColumn> columns;
	if (!page)
		return columns;

	// Guides are stored relative to the page origin; convert to document coords.
	const double xo = page->xOffset();
	QList<double> raw;
	for (double g : page->guides.verticals(GuideManagerCore::Standard))
		raw.append(xo + g);

	// Also take the assigned master page's vertical (column) guides.
	if (doc && !page->masterPageName().isEmpty() && doc->MasterNames.contains(page->masterPageName()))
	{
		const int idx = doc->MasterNames.value(page->masterPageName());
		if (idx >= 0 && idx < doc->MasterPages.count())
		{
			ScPage* mp = doc->MasterPages.at(idx);
			for (double g : mp->guides.verticals(GuideManagerCore::Standard))
				raw.append(xo + g);
		}
	}

	// Keep only guides strictly inside the content area.
	QList<double> interior;
	for (double x : raw)
		if (x > contentArea.left() + 0.5 && x < contentArea.right() - 0.5)
			interior.append(x);
	std::sort(interior.begin(), interior.end());
	interior.erase(std::unique(interior.begin(), interior.end(), [](double a, double b) { return qAbs(a - b) < 0.5; }), interior.end());

	qDebug() << "[AutoArrange] vertical guides found:" << raw.size()
	         << "-> interior guides:" << interior.size();
	if (interior.isEmpty())
	{
		qDebug() << "[AutoArrange] no interior vertical guides -> no columns.";
		return columns;   // caller warns "add column guides"
	}

	// Build all spans between content edges and interior guides.
	QList<double> bounds;
	bounds.append(contentArea.left());
	bounds += interior;
	bounds.append(contentArea.right());
	QList<QPair<double, double>> spans;
	double maxW = 0.0;
	for (int i = 0; i < bounds.size() - 1; ++i)
	{
		const double w = bounds[i + 1] - bounds[i];
		if (w > 1.0)
		{
			spans.append(qMakePair(bounds[i], bounds[i + 1]));
			maxW = qMax(maxW, w);
		}
	}
	// Newspaper guides usually come in PAIRS bracketing narrow gutters, so drop
	// the thin gutter spans — a real column is at least half the widest span.
	const double minColWidth = maxW * 0.5;
	for (const auto& s : spans)
	{
		const double w = s.second - s.first;
		if (w >= minColWidth)
		{
			ArrangeColumn c;
			c.left = s.first;
			c.right = s.second;
			columns.append(c);
		}
	}

	qDebug() << "[AutoArrange] detected" << columns.size() << "columns (gutter threshold <" << minColWidth << "pt dropped):";
	for (int i = 0; i < columns.size(); ++i)
		qDebug() << "   col" << i << "left:" << columns[i].left << "right:" << columns[i].right << "width:" << columns[i].width();
	return columns;
}

// ── Assignment & stacking ───────────────────────────────────────────────────

void AutoArrangeEngine::fitTextFrameHeight(PageItem* frame)
{
	if (!frame || !frame->isTextFrame())
		return;
	if (frame->itemText.length() <= 0)   // nothing to fit
		return;
	// autoFitFrameHeight() binary-searches the height (re-laying out each step)
	// until no text overflows — robust for Malayalam text at any frame width,
	// unlike setTextFrameHeight() which needs a pre-computed valid layout.
	frame->asTextFrame()->autoFitFrameHeight();
}

namespace
{
	// One frame plus the column track it occupies (start column + how many it spans).
	struct FrameInfo
	{
		PageItem* frame { nullptr };
		int startColumn { 0 };
		int columnSpan { 1 };
		double originalY { 0.0 };
	};

	// Assign a frame to its start column (by LEFT edge, so column choice never
	// depends on width) and measure how many columns its width spans.
	FrameInfo analyzeFrame(PageItem* f, const QList<ArrangeColumn>& columns)
	{
		FrameInfo info;
		info.frame = f;
		info.originalY = f->yPos();
		const double left = f->xPos();
		const double right = f->xPos() + f->width();
		const double TOL = value2pts(3.0, SC_MM);

		// Method 1: left edge falls within a column's range (most reliable).
		info.startColumn = -1;
		for (int i = 0; i < columns.size(); ++i)
		{
			if (left >= columns[i].left - TOL && left < columns[i].right - TOL)
			{
				info.startColumn = i;
				break;
			}
		}
		// Method 2: fall back to the column containing the frame centre.
		if (info.startColumn < 0)
		{
			const double center = (left + right) / 2.0;
			for (int i = 0; i < columns.size(); ++i)
			{
				if (center >= columns[i].left && center <= columns[i].right)
				{
					info.startColumn = i;
					break;
				}
			}
		}
		// Method 3: last resort — column whose left edge is closest to the frame's.
		if (info.startColumn < 0)
		{
			double best = 1.0e18;
			for (int i = 0; i < columns.size(); ++i)
			{
				const double d = qAbs(columns[i].left - left);
				if (d < best) { best = d; info.startColumn = i; }
			}
		}
		if (info.startColumn < 0)
			info.startColumn = 0;

		// Column span: every column whose left edge lies within the frame's width.
		info.columnSpan = 1;
		for (int i = info.startColumn + 1; i < columns.size(); ++i)
		{
			if (columns[i].left < right - TOL)
				++info.columnSpan;
			else
				break;
		}
		return info;
	}

	// A headline = a short text frame whose first character is >20pt. It should
	// keep its (small) height in Fill Column mode, not be stretched.
	bool isHeadline(PageItem* f)
	{
		if (!f || !f->isTextFrame() || f->itemText.length() <= 0)
			return false;
		const double thirtyMM = value2pts(30.0, SC_MM);
		if (f->height() > thirtyMM)
			return false;
		return f->itemText.charStyle(0).fontSize() > 200.0;   // fontSize is 1/10 pt → 20pt
	}
}

// ── Orchestration ───────────────────────────────────────────────────────────

ArrangeResult AutoArrangeEngine::arrangePage(ScribusDoc* doc, ScPage* page, const ArrangeOptions& opts)
{
	ArrangeResult res;
	if (!doc || !page)
		return res;

	qDebug() << "[AutoArrange] === page" << page->pageNr() << (opts.dryRun ? "(DRY RUN)" : "") << "===";
	const QRectF area = detectContentArea(page);
	QList<ArrangeColumn> columns = detectColumns(doc, page, area);
	res.columnsFound = columns.size();
	if (columns.isEmpty())
		return res;   // caller warns "no column guides"

	const QList<PageItem*> frames = gatherFrames(doc, page, opts);
	const char* modeName = (opts.heightMode == ArrangeOptions::FitToText) ? "FitToText (auto-sizing preserved)"
	                     : (opts.heightMode == ArrangeOptions::FillColumn) ? "FillColumn (fixed height)"
	                     : "KeepOriginal (fixed height)";
	qDebug() << "[AutoArrange] arrangeable frames on page:" << frames.size() << "| height mode:" << modeName;

	// Analyse each frame → (start column, span). Frames whose centre is outside
	// the content area (masthead/footer) are left untouched.
	QList<FrameInfo> infos;
	for (PageItem* f : frames)
	{
		const QPointF center(f->xPos() + f->width() / 2.0, f->yPos() + f->height() / 2.0);
		if (opts.preserveColumn && !area.contains(center))
		{
			qDebug() << "[AutoArrange] frame" << desc(f) << "centre" << center << "outside content area -> skipped";
			continue;
		}
		FrameInfo info = analyzeFrame(f, columns);
		infos.append(info);
		qDebug() << "[AutoArrange] frame" << desc(f) << "leftX" << f->xPos() << "w" << f->width()
		         << "-> startColumn" << info.startColumn << "span" << info.columnSpan
		         << "origY" << info.originalY;
	}

	// Group frames by their start column, keeping reading order (Y) within each.
	std::sort(infos.begin(), infos.end(), [](const FrameInfo& a, const FrameInfo& b) {
		if (a.startColumn != b.startColumn)
			return a.startColumn < b.startColumn;
		return a.originalY < b.originalY;
	});
	QMap<int, QList<FrameInfo>> groupsByCol;
	for (const FrameInfo& info : infos)
		groupsByCol[info.startColumn].append(info);

	const double gap  = opts.zeroGap ? 0.0 : value2pts(2.0, SC_MM);
	const double minH = value2pts(15.0, SC_MM);

	for (auto it = groupsByCol.constBegin(); it != groupsByCol.constEnd(); ++it)
	{
		const QList<FrameInfo>& group = it.value();

		// Fill Column: scale the flexible (non-headline, non-image, non-group) text
		// frames so the column's original fullness is restored after gaps close.
		double flexOrigTotal = 0.0, fixedTotal = 0.0;
		int lastFlexIdx = -1;
		for (int i = 0; i < group.size(); ++i)
		{
			PageItem* f = group[i].frame;
			const bool fixed = f->isImageFrame() || f->isGroup() || isHeadline(f);
			if (fixed) fixedTotal += f->height();
			else       { flexOrigTotal += f->height(); lastFlexIdx = i; }
		}
		const double avail = area.height() - fixedTotal;
		const double scale = (flexOrigTotal > 1.0) ? qMax(0.1, avail / flexOrigTotal) : 1.0;
		const bool scaleNearOne = (scale >= 0.95 && scale <= 1.05);

		double y = area.top();
		for (int gidx = 0; gidx < group.size(); ++gidx)
		{
			const FrameInfo& info = group[gidx];
			PageItem* f  = info.frame;
			const int sc = info.startColumn;
			const int ec = qMin(sc + info.columnSpan - 1, columns.size() - 1);
			const double newX = columns[sc].left;
			const double newW = columns[ec].right - columns[sc].left;   // span incl. gutters

			const double ox = f->xPos(), oy = f->yPos(), ow = f->width(), oh = f->height();
			const int oTextLen = f->isTextFrame() ? f->itemText.length() : 0;
			const bool keepH  = f->isImageFrame() || f->isGroup();   // never resized
			const bool headln = isHeadline(f);
			qDebug() << "[AutoArrange] BEFORE" << desc(f) << "X,Y,W,H:" << ox << oy << ow << oh
			         << (keepH ? "[keepH]" : (headln ? "[headline]" : ""));

			// Decide the target height for this frame from the height mode.
			double newH = oh;
			bool autoFit = false;
			switch (opts.heightMode)
			{
				case ArrangeOptions::KeepOriginal:
					newH = oh;
					break;
				case ArrangeOptions::FitToText:
					if (f->isTextFrame()) autoFit = true; else newH = oh;
					break;
				case ArrangeOptions::FillColumn:
					if (keepH || headln)
						newH = oh;                               // fixed height
					else
					{
						newH = scaleNearOne ? oh : qMax(oh * scale, minH);
						if (gidx == lastFlexIdx)                  // last flex frame closes the gap
							newH = qMax(minH, area.bottom() - y);
					}
					break;
			}

			if (!opts.dryRun)
			{
				if (f->isGroup())
				{
					f->setXPos(newX);
					f->setYPos(y);   // groups keep their own size
				}
				else
				{
					f->setXPos(newX);
					f->setYPos(y);
					f->setWidth(newW);
					if (autoFit)
						fitTextFrameHeight(f);
					else
						f->setHeight(newH);
				}
			}

			const double placedH = opts.dryRun ? (autoFit ? oh : newH) : f->height();
			qDebug() << "[AutoArrange]   AFTER " << desc(f) << "X,Y,W,H:"
			         << (opts.dryRun ? ox : f->xPos()) << y
			         << (opts.dryRun ? newW : f->width()) << placedH
			         << (opts.dryRun ? "(dry run — not modified)" : "");

			if (!opts.dryRun && f->isTextFrame() && f->itemText.length() != oTextLen)
				qWarning() << "[AutoArrange] TEXT LENGTH CHANGED on" << f->itemName()
				           << "from" << oTextLen << "to" << f->itemText.length() << "— possible content loss!";

			y += placedH + gap;   // zero-gap: bottom of one frame == top of the next
			++res.framesArranged;
			if (y > area.bottom() + value2pts(10.0, SC_MM))
				++res.framesOverflowed;
		}
	}

	// Validation: confirm no frame landed in the wrong column.
	if (!opts.dryRun)
	{
		for (const FrameInfo& info : infos)
		{
			const double expectedX = columns[info.startColumn].left;
			if (qAbs(info.frame->xPos() - expectedX) > 1.0)
				qWarning() << "[AutoArrange] VALIDATION FAILED: frame" << info.frame->itemName()
				           << "expected X" << expectedX << "actual X" << info.frame->xPos();
		}
		// Auto-fit state report. Scribus has NO persistent per-frame "adjust height
		// to text" flag, so nothing is stored or destroyed. In FitToText mode each
		// text frame is fitted with autoFitFrameHeight() (the exact call the menu /
		// on-load hook use), so it keeps auto-sizing on edit. Fill/Keep set a fixed
		// height, which the user can restore with Adjust Frame Height to Text.
		const bool autoSizing = (opts.heightMode == ArrangeOptions::FitToText);
		for (const FrameInfo& info : infos)
		{
			if (info.frame->isTextFrame())
				qDebug() << "[AutoArrange] text frame" << info.frame->itemName()
				         << (autoSizing ? "-> fitted to text (auto-sizing preserved)"
				                        : "-> fixed height (re-run Adjust Frame Height to Text to auto-size)");
		}
	}

	res.pagesProcessed = 1;
	qDebug() << "[AutoArrange] page done: columns" << res.columnsFound
	         << "arranged" << res.framesArranged << "overflow" << res.framesOverflowed;
	return res;
}

ArrangeResult AutoArrangeEngine::arrange(ScribusDoc* doc, const ArrangeOptions& opts)
{
	ArrangeResult total;
	if (!doc)
		return total;

	QList<ScPage*> pages;
	if (opts.scope == ArrangeOptions::AllPages)
		pages = doc->DocPages;
	else if (doc->currentPage())
		pages.append(doc->currentPage());
	if (pages.isEmpty())
		return total;

	// A dry run must not create an undo step or touch the document.
	UndoManager* um = UndoManager::instance();
	UndoTransaction trans;
	if (!opts.dryRun && UndoManager::undoEnabled())
		trans = um->beginTransaction(Um::Selection, Um::IGroup, QObject::tr("Auto Arrange Frames"), QString(), Um::IMove);

	for (ScPage* p : pages)
	{
		const ArrangeResult r = arrangePage(doc, p, opts);
		total.pagesProcessed  += r.pagesProcessed;
		total.columnsFound     = qMax(total.columnsFound, r.columnsFound);
		total.framesArranged  += r.framesArranged;
		total.framesOverflowed += r.framesOverflowed;
	}

	if (trans)
		trans.commit();

	if (!opts.dryRun)
	{
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
	return total;
}
