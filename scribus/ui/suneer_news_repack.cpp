/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/*
 suneer 2026-10-28: Layout > Re-pack Page (Ctrl+Alt+R, Edit toolbar, page context menu, the
 News Browser's Re-pack button) and Layout > Re-pack Automatically.

 1. Every movable module's bottom text frame is fitted to its last line (it grows when it
    overflows); photos and captions keep their size (the caption stays attached by its weld).
 2. The modules are moved so that no gap is wider than the model's module gap and nothing
    overlaps, in reading order (top, then left):
    - obituary page: an obit module goes to the highest free slot of its OWN column (never
      another column - user decision 2026-10-29); 2-column news only in the model's news
      columns (9-10 / 1-2), in its own columns.
    - normal page: only News Browser stories (news.story) move, straight up in their own
      columns; ads, hand-made frames, locked and pinned frames never move.
    A module that no longer fits above the bottom margin goes to the pasteboard (listed).
 3. One undo step. A welded story moves as one (moveGroup moves each weld cluster once), so an
    attached caption stays under its photo.
 Re-pack Automatically (off by default): a deleted obituary module, or the end of text editing
 in one, re-packs only those columns ~1 s later, as its own undo step "Auto re-pack".
*/

#include "suneer_news_panel.h"

#include <cmath>
#include <functional>

#include "suneer_obit_model.h"
#include "suneerspacing.h"
#include "suneertextclean.h"
#include "suneerweld.h"
#include "suneerlayoutmodel.h"

#include "appmodes.h"
#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scraction.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "undogui.h"
#include "undomanager.h"
#include "undotransaction.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>

namespace {

const double kMm = 72.0 / 25.4;
const double kTol = 0.5;

bool isObitItem(const PageItem* it)
{
	if (!it)
		return false;
	if (SuneerWeld::attr(it, QStringLiteral("news.model")).startsWith(QLatin1String("obituary")))
		return true;
	if (it->isGroup())
		for (PageItem* c : const_cast<PageItem*>(it)->getAllChildren())
			if (SuneerWeld::attr(c, QStringLiteral("news.model")).startsWith(QLatin1String("obituary")))
				return true;
	return false;
}

QString storyKeyOf(const PageItem* it)
{
	QString k = SuneerWeld::attr(it, QStringLiteral("news.story"));
	if (k.isEmpty() && it->isGroup())
		k = SuneerNewsPanel::groupStoryId(it);
	return k;
}

double xOverlap(const QRectF& a, const QRectF& b)
{
	return qMin(a.right(), b.right()) - qMax(a.left(), b.left());
}

bool hits(const QRectF& a, const QRectF& b)
{
	const QRectF ai = a.adjusted(kTol, kTol, -kTol, -kTol);
	return ai.isValid() && ai.intersects(b);
}

// The frame itself (what placement measures): getBoundingRect() adds the text-wrap offsets
// (2 mm around a photo / caption), which put every obituary-news module "off the grid".
QRectF geomRect(const PageItem* it)
{
	if (qAbs(it->rotation()) > 0.01)
		return it->getBoundingRect();
	return QRectF(it->xPos(), it->yPos(), it->width(), it->height());
}

QRectF unionRect(const QList<PageItem*>& items)
{
	QRectF r;
	for (PageItem* it : items)
		if (it)
			r = r.isNull() ? geomRect(it) : r.united(geomRect(it));
	return r;
}

// A text frame the fit may change: plain, not linked, not a table cell, not a caption / flag.
bool fittableText(PageItem* t)
{
	if (!t || !t->isTextFrame() || t->isGroupChild() || t->asTextFrame()->isTableItem || t->itemText.length() == 0)
		return false;
	if (t->nextInChain() || t->prevInChain())
		return false;
	const QString role = SuneerWeld::attr(t, QStringLiteral("news.role"));
	if (role == QLatin1String("caption") || role == QLatin1String("flag") || t->itemName().startsWith(QLatin1String("check_")))
		return false;
	return SuneerWeld::attr(t, QStringLiteral("news.captionOf")).isEmpty();
}

} // namespace

bool SuneerNewsPanel::repackAutoPref()
{
	QSettings s("Faircode", "ScribusNews");
	return s.value("news/repackAuto", false).toBool();
}

void SuneerNewsPanel::setRepackAutoPref(bool on)
{
	QSettings s("Faircode", "ScribusNews");
	s.setValue("news/repackAuto", on);
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

SuneerNewsPanel::RepackResult SuneerNewsPanel::repackCore(const RepackScope& scope)
{
	RepackResult res;
	if (!m_doc || scope.pageIndex < 0 || scope.pageIndex >= m_doc->Pages->count())
		return res;
	ScPage* page = m_doc->Pages->at(scope.pageIndex);
	const double pageX = page->xOffset(), pageY = page->yOffset();
	const QRectF pageRect(pageX, pageY, page->width(), page->height());
	const double bottom = pageY + page->height() - page->Margins.bottom();

	// -- the items of the page ------------------------------------------------
	QList<PageItem*> tops;
	for (PageItem* it : m_doc->DocItems)
	{
		if (!it || it->isGroupChild() || (it->isAutoText && it->itemText.length() == 0))
			continue;
		if (!it->getBoundingRect().intersects(pageRect))
			continue;
		if (attr(it, "news.rule") == QLatin1String("1") || attr(it, "news.parked") == QLatin1String("1"))
			continue;          // rules are drawn again after the moves; parked modules are not on the page
		if (it->isTextFrame() && it->itemText.length() == 0 && it->fillColor() == CommonStrings::None && it->lineColor() == CommonStrings::None)
			continue;          // an empty invisible frame prints nothing
		tops << it;
	}
	bool obit = false;
	for (PageItem* it : tops)
		if (isObitItem(it))
			obit = true;
	res.obituary = obit;

	// -- the model: grid and gaps ----------------------------------------------
	SuneerObitModel om = obitModel();
	QString spacingId = obit ? activeObitModelId() : pageSpacingModel();
	const SuneerSpacing::Values sp = SuneerSpacing::forModel(spacingId);
	const double gap = sp.pt(SuneerSpacing::ModuleGapV);
	const double usableW = page->width() - page->Margins.left() - page->Margins.right();
	const int gridCols = qMax(1, om.gridColumns);
	const double gutter = sp.pt(SuneerSpacing::ModuleGapH);
	const double colW = (usableW - gutter * (gridCols - 1)) / gridCols;
	auto colX = [&](int col1) { return pageX + page->Margins.left() + (col1 - 1) * (colW + gutter); };
	const double top = obit ? pageY + om.contentTopMm * kMm : pageY + page->Margins.top();

	// -- movable units: a story (news.story) with every frame welded to it ---
	// obituary page: the obituary engine's modules; normal page: News Browser stories (never an
	// untagged frame - ads, hand-made stories, masthead). A unit with a locked / pinned / fixed frame stays.
	auto wantedItem = [&](PageItem* it) -> bool {
		if (obit)
			return isObitItem(it);
		return !storyKeyOf(it).isEmpty() && !isObitItem(it);
	};
	auto pinnedItem = [&](PageItem* it) -> bool {
		return it->locked() || attr(it, "news.manual") == QLatin1String("1") || !attr(it, "layout.fixed").isEmpty() || isSlotPlaceholder(it);
	};
	QHash<PageItem*, int> unitOf;
	QList<QList<PageItem*>> units;
	QHash<QString, int> byKey;
	const QSet<PageItem*> onPage(tops.begin(), tops.end());
	for (PageItem* it : tops)
	{
		if (!wantedItem(it))
			continue;
		const QString key = storyKeyOf(it);
		int u = key.isEmpty() ? -1 : byKey.value(key, -1);
		if (u < 0)
		{
			u = units.size();
			units << QList<PageItem*>();
			if (!key.isEmpty())
				byKey.insert(key, u);
		}
		units[u] << it;
		unitOf.insert(it, u);
	}
	// welds join units (and pull in frames welded to a story, an orange check flag for example)
	QSet<int> pinnedUnits;
	for (int u = 0; u < units.size(); ++u)
	{
		const QList<PageItem*> base = units[u];
		for (PageItem* it : base)
		{
			if (!it->isWelded())
				continue;
			for (PageItem* w : SuneerWeld::cluster(it))
			{
				if (!w || w == it || !onPage.contains(w))
					continue;
				const int other = unitOf.value(w, -1);
				if (other == u)
					continue;
				if (other >= 0)
				{
					for (PageItem* m : units[other])
						unitOf.insert(m, u);
					units[u] << units[other];
					units[other].clear();
					if (pinnedUnits.contains(other))
						pinnedUnits.insert(u);
				}
				else
				{
					unitOf.insert(w, u);
					units[u] << w;
				}
			}
		}
	}
	for (int u = 0; u < units.size(); ++u)
		for (PageItem* it : units[u])
			if (pinnedItem(it))
				pinnedUnits.insert(u);

	struct Mod
	{
		QList<PageItem*> items;
		QRectF r, orig;
		QString title;
		int col { 0 }, span { 0 };
	};
	QList<Mod> mods;
	QList<QRectF> obstacles;
	QList<QPair<QRectF, QString>> fixedMods;     // story modules that stay (for the gap count only)
	for (int u = 0; u < units.size(); ++u)
	{
		if (units[u].isEmpty())
			continue;
		Mod m;
		m.items = units[u];
		m.r = m.orig = unionRect(m.items);
		for (PageItem* it : m.items)
			if (attr(it, "news.role") == QLatin1String("text") || (m.title.isEmpty() && it->isTextFrame()))
				m.title = it->itemText.plainText().simplified().left(28);
		if (m.title.isEmpty())
			m.title = m.items.first()->itemName();
		bool stays = pinnedUnits.contains(u);
		QString why = stays ? tr("locked / pinned") : QString();
		if (obit && !stays)
		{
			// on the grid, 1 .. n columns wide (a module widened or moved off the grid by hand stays)
			m.col = qRound((m.r.left() - colX(1)) / (colW + gutter)) + 1;
			m.span = qRound((m.r.width() + gutter) / (colW + gutter));
			if (m.col < 1 || m.span < 1 || m.col + m.span - 1 > gridCols || qAbs(m.r.left() - colX(m.col)) > 1.5 * kMm
			    || qAbs(m.r.width() - (m.span * colW + (m.span - 1) * gutter)) > 1.5 * kMm)
			{
				stays = true;
				why = tr("not on the column grid (x %1 mm, %2 mm wide)").arg((m.r.left() - pageX) / kMm, 0, 'f', 1).arg(m.r.width() / kMm, 0, 'f', 1);
			}
		}
		// an automatic run works on the columns of what was deleted / edited only
		if (!stays && !scope.xRanges.isEmpty())
		{
			bool in = false;
			for (const auto& xr : scope.xRanges)
				if (qMin(m.r.right(), xr.second) - qMax(m.r.left(), xr.first) > kTol)
					in = true;
			if (!in)
				stays = true;
		}
		if (stays)
		{
			obstacles << m.r;
			fixedMods << qMakePair(m.r, m.title);
			if (!why.isEmpty())
				res.details << tr("stays: \"%1\" - %2").arg(m.title, why);
			continue;
		}
		mods << m;
	}
	// the other frames of the page and the master page's (masthead, folio)
	for (PageItem* it : tops)
		if (!unitOf.contains(it))
			obstacles << geomRect(it);
	if (m_doc->MasterNames.contains(page->masterPageName()))
	{
		const ScPage* mp = m_doc->MasterPages.at(m_doc->MasterNames[page->masterPageName()]);
		for (PageItem* it : m_doc->MasterItems)
		{
			if (!it || it->isGroupChild() || it->OnMasterPage != page->masterPageName())
				continue;
			if (it->isTextFrame() && it->itemText.length() == 0)
				continue;
			QRectF r = geomRect(it);
			if (mp)
				r.translate(pageX - mp->xOffset(), pageY - mp->yOffset());
			obstacles << r;
		}
	}
	res.modules = mods.size();

	// overlaps that were there before (a frame laid over a story by hand): left alone, never "repaired"
	QSet<QPair<int, int>> preMM;        // module, module
	QSet<QPair<int, int>> preMO;        // module, obstacle
	for (int a = 0; a < mods.size(); ++a)
	{
		for (int b = a + 1; b < mods.size(); ++b)
			if (hits(mods[a].orig, mods[b].orig))
			{
				preMM.insert(qMakePair(a, b));
				preMM.insert(qMakePair(b, a));
			}
		for (int o = 0; o < obstacles.size(); ++o)
			if (hits(mods[a].orig, obstacles[o]))
				preMO.insert(qMakePair(a, o));
	}

	QVector<bool> settled(mods.size(), false), parked(mods.size(), false);
	// the gap check: the space above each module beyond the module gap
	// every story module of the page counts, the ones that stay too (their gap cannot be closed: "left")
	auto measureGaps = [&](int* count, double* mm, QStringList* lines) {
		*count = 0;
		*mm = 0;
		QList<QPair<QRectF, QString>> rows = fixedMods;
		for (int a = 0; a < mods.size(); ++a)
			if (!parked[a])
				rows << qMakePair(mods[a].r, mods[a].title);
		for (int a = 0; a < rows.size(); ++a)
		{
			const QRectF r = rows[a].first;
			if (r.top() > bottom + kTol)
				continue;
			double expect = top;
			auto consider = [&](const QRectF& o) {
				if (o != r && xOverlap(o, r) > kTol && o.top() < r.top())       // a frame above that overlaps it: no gap
					expect = qMax(expect, o.bottom() + gap);
			};
			for (const QRectF& o : obstacles)
				consider(o);
			for (int b = 0; b < rows.size(); ++b)
				if (b != a)
					consider(rows[b].first);
			const double excess = r.top() - expect;
			if (excess > 1.0 * kMm)
			{
				++*count;
				*mm += excess / kMm;
				if (lines)
					*lines << tr("gap %1 mm above \"%2\" (at %3 mm)").arg(excess / kMm, 0, 'f', 1).arg(rows[a].second).arg((r.top() - pageY) / kMm, 0, 'f', 0);
			}
		}
	};

	const bool progress = m_mw && m_mw->mainWindowProgressBar && mods.size() > 25;
	auto step = [&](int i, int n) {
		if (!progress)
			return;
		m_mw->mainWindowProgressBar->setMaximum(qMax(1, n));
		m_mw->mainWindowProgressBar->setValue(i);
		if (i % 5 == 0)
			qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
	};

	// -- 1. fit: the bottom text frame of each module to its last line ---------
	QSet<PageItem*> fitOnly;
	for (const QPointer<PageItem>& p : scope.fitOnly)
		if (p)
			fitOnly.insert(p.data());
	QList<PageItem*> fittedFrames;
	for (int a = 0; a < mods.size(); ++a)
	{
		step(a, mods.size() * 2);
		Mod& m = mods[a];
		if (scope.autoRun)
		{
			bool mine = false;
			for (PageItem* it : m.items)
				if (fitOnly.contains(it))
					mine = true;
			if (!mine)
				continue;
		}
		bool grouped = false;
		for (PageItem* it : m.items)
			if (it->isGroup())
				grouped = true;
		if (grouped)
		{
			++res.notFitted;          // an old grouped module (Convert story groups to weld first)
			continue;
		}
		for (PageItem* t : m.items)
		{
			if (!fittableText(t))
				continue;
			if (scope.autoRun && !fitOnly.contains(t))
				continue;
			const QRectF tr0(t->xPos(), t->yPos(), t->width(), t->height());
			// nothing of the module below it (the frame at the module's foot), and never shorter than a
			// photo / frame lying inside it (a photo the text wraps around)
			bool atFoot = true;
			double minH = 1.0;
			for (PageItem* o : m.items)
			{
				if (o == t)
					continue;
				const QRectF orr(o->xPos(), o->yPos(), o->width(), o->height());
				if (xOverlap(orr, tr0) <= kTol)
					continue;
				if (orr.top() >= tr0.bottom() - 1.0)
					atFoot = false;
				else if (orr.top() >= tr0.top() - kTol && orr.top() < tr0.bottom())
					minH = qMax(minH, orr.bottom() - tr0.top());
			}
			if (!atFoot)
				continue;
			const double hBefore = t->height();
			// the search never records undo (setUndoEnabled is a counter: one false, one true)
			UndoManager::instance()->setUndoEnabled(false);
			bool over = false;
			const double hNew = SuneerTextClean::fitHeight(t->asTextFrame(), minH, 20000.0, &over);
			t->setHeight(hBefore);
			t->updateClip();
			UndoManager::instance()->setUndoEnabled(true);
			if (over || qAbs(hNew - hBefore) < 0.3)
			{
				t->invalidateLayout();
				t->layout();
				continue;
			}
			t->oldXpos = t->xPos(); t->oldYpos = t->yPos(); t->oldWidth = t->width(); t->oldHeight = hBefore;
			m_doc->sizeItem(t->width(), hNew, t, false, true, false);
			t->checkChanges(true);
			t->updateClip();
			t->invalidateLayout();
			t->layout();
			fittedFrames << t;
			++res.fitted;
			res.details << tr("fitted \"%1\": %2 -> %3 mm").arg(m.title).arg(hBefore / kMm, 0, 'f', 1).arg(hNew / kMm, 0, 'f', 1);
		}
		m.r = unionRect(m.items);
	}

	// the gaps before the moves: measured after the fit, so the blank space a shorter text left at the
	// foot of its frame counts as the gap it is
	QStringList gapLinesBefore;
	measureGaps(&res.gapsBefore, &res.gapsBeforeMm, &gapLinesBefore);

	// -- 2. pack --------------------------------------------------------------
	QList<int> order;
	for (int i = 0; i < mods.size(); ++i)
		order << i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		const double ta = std::floor(mods[a].orig.top()), tb = std::floor(mods[b].orig.top());
		return ta < tb || (ta == tb && mods[a].orig.left() < mods[b].orig.left());
	});
	auto blockers = [&](int self) {
		QList<QRectF> l;
		for (int o = 0; o < obstacles.size(); ++o)
			if (!preMO.contains(qMakePair(self, o)))
				l << obstacles[o];
		for (int b = 0; b < mods.size(); ++b)
		{
			if (b == self || parked[b] || preMM.contains(qMakePair(self, b)))
				continue;
			if (settled[b])
				l << mods[b].r;
		}
		return l;
	};
	// the first free y from y0 down for a w x h rect at x (pushed below whatever it meets)
	auto scanFree = [&](double x, double w, double h, double y0, const QList<QRectF>& list) -> double {
		double y = y0;
		for (int guard = 0; guard < 500; ++guard)
		{
			const QRectF r(x, y, w, h);
			double push = -1;
			for (const QRectF& o : list)
				if (hits(r, o))
					push = qMax(push, o.bottom() + gap);
			if (push < 0)
				return y;
			y = push;
		}
		return 1e9;
	};
	QList<QList<PageItem*>> toPark;
	QSet<int> movedFirst;
	int done = 0;
	for (int k : order)
	{
		step(mods.size() + done++, mods.size() * 2);
		Mod& m = mods[k];
		const double w = m.r.width(), h = m.r.height();
		double y0 = top;
		if (!obit)
		{
			// a normal page: straight under the lowest frame above it in its own columns (a story above that
			// grew pushes it down); scanFree then steps over anything it would meet on the way
			for (int o = 0; o < obstacles.size(); ++o)
				if (!preMO.contains(qMakePair(k, o)) && xOverlap(obstacles[o], m.orig) > kTol && obstacles[o].top() < m.orig.top() - kTol)
					y0 = qMax(y0, obstacles[o].bottom() + gap);
			for (int b = 0; b < mods.size(); ++b)
				if (settled[b] && !preMM.contains(qMakePair(k, b)) && xOverlap(mods[b].r, m.orig) > kTol)
					y0 = qMax(y0, mods[b].r.bottom() + gap);
		}
		double bestY = scanFree(m.r.left(), w, h, y0, blockers(k));
		// a module never goes down to make a gap wider (a masthead 3 mm above stays 3 mm above): only
		// something that now overlaps its place (a story above that grew) pushes it down
		if (bestY > m.r.top() + kTol)
		{
			bool free = true;
			for (const QRectF& o : blockers(k))
				if (hits(m.r, o))
					free = false;
			if (free)
				bestY = m.r.top();
		}
		const bool fits = bestY + h <= bottom + kTol;
		if (!fits)
		{
			parked[k] = true;
			toPark << m.items;
			res.parked << m.title;
			continue;
		}
		const double dy = bestY - m.r.top();
		if (qAbs(dy) > 0.3)       // under 0.1 mm is not a move
		{
			moveStory(m.items, 0.0, dy);
			m.r.translate(0.0, dy);
			++res.moved;
			movedFirst.insert(k);
			res.details << tr("\"%1\": %2 mm %3").arg(m.title).arg(qAbs(dy) / kMm, 0, 'f', 1).arg(dy < 0 ? tr("up") : tr("down"));
		}
		settled[k] = true;
	}
	// a module can settle below one handled later that then moved up or was parked:
	// more passes move each one straight up under whatever is above it in its own columns (order kept)
	QSet<int> movedIdx;
	for (int pass = 0; pass < 5; ++pass)
	{
		QList<int> now;
		for (int i = 0; i < mods.size(); ++i)
			if (settled[i])
				now << i;
		std::stable_sort(now.begin(), now.end(), [&](int a, int b) { return mods[a].r.top() < mods[b].r.top(); });
		bool any = false;
		for (int k : now)
		{
			Mod& m = mods[k];
			double y0 = top;
			for (int o = 0; o < obstacles.size(); ++o)
				if (!preMO.contains(qMakePair(k, o)) && xOverlap(obstacles[o], m.r) > kTol && obstacles[o].top() < m.r.top() - kTol)
					y0 = qMax(y0, obstacles[o].bottom() + gap);
			for (int b = 0; b < mods.size(); ++b)
				if (b != k && settled[b] && !preMM.contains(qMakePair(k, b)) && xOverlap(mods[b].r, m.r) > kTol && mods[b].r.top() < m.r.top() - kTol)
					y0 = qMax(y0, mods[b].r.bottom() + gap);
			if (y0 >= m.r.top() - 0.3)
				continue;
			bool clear = true;
			for (const QRectF& o : blockers(k))
				if (hits(QRectF(m.r.left(), y0, m.r.width(), m.r.height()), o))
					clear = false;
			if (!clear)
				continue;
			const double dy = y0 - m.r.top();
			moveStory(m.items, 0.0, dy);
			m.r.translate(0.0, dy);
			if (!movedIdx.contains(k))
				res.details << tr("\"%1\": %2 mm up (second pass)").arg(m.title).arg(-dy / kMm, 0, 'f', 1);
			movedIdx.insert(k);
			any = true;
		}
		if (!any)
			break;
	}
	for (int k : movedIdx)
		if (!movedFirst.contains(k))
			++res.moved;
	if (!toPark.isEmpty() && scope.pageIndex == m_doc->currentPageNumber())
		parkOnPasteboard(toPark);
	if (progress)
		m_mw->mainWindowProgressBar->reset();

	// -- 3. after: rules, gaps, the overlap check, text that overflows now -------
	// separator rules drawn again for the new places - only on a page that has News rules (respaceRules
	// works on the page in view; a page without rules never gets new ones)
	bool hadRules = false;
	for (PageItem* it : m_doc->DocItems)
		if (it && attr(it, "news.rule") == QLatin1String("1") && pageRect.intersects(it->getBoundingRect()))
			hadRules = true;
	if (hadRules && (res.moved > 0 || res.fitted > 0 || !toPark.isEmpty()) && sp.v[SuneerSpacing::RuleWidth] > 0.0 && scope.pageIndex == m_doc->currentPageNumber())
		respaceRules(sp);
	for (int a = 0; a < mods.size(); ++a)
		if (parked[a])
			mods[a].r = QRectF(-1e7, -1e7, 1, 1);
	QStringList gapLines;
	measureGaps(&res.gapsAfter, &res.gapsAfterMm, &gapLines);
	for (int a = 0; a < mods.size(); ++a)
	{
		if (parked[a])
			continue;
		for (int b = a + 1; b < mods.size(); ++b)
			if (!parked[b] && !preMM.contains(qMakePair(a, b)) && hits(mods[a].r, mods[b].r))
			{
				++res.overlapsAfter;
				res.details << tr("OVERLAP \"%1\" / \"%2\"").arg(mods[a].title, mods[b].title);
			}
		for (int o = 0; o < obstacles.size(); ++o)
			if (!preMO.contains(qMakePair(a, o)) && hits(mods[a].r, obstacles[o]))
			{
				++res.overlapsAfter;
				res.details << tr("OVERLAP \"%1\" / a fixed frame").arg(mods[a].title);
			}
	}
	for (PageItem* t : fittedFrames)
		if (t->asTextFrame()->frameOverflows())
			res.details << tr("overflows after the re-pack: %1").arg(t->itemName());
	for (const QString& g : gapLinesBefore)
		res.details << tr("before: ") + g;
	for (const QString& g : gapLines)
		res.details << tr("after: ") + g;
	return res;
}

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

QString SuneerNewsPanel::repackPage(bool interactive)
{
	if (!m_doc || !m_doc->currentPage() || m_repacking)
		return QString();
	if (m_mw && m_mw->view && m_doc->appMode == modeEdit)
		m_mw->view->requestMode(modeNormal);
	m_doc->m_Selection->clear();
	m_repacking = true;
	QElapsedTimer clock;
	clock.start();
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Re-pack page"), QString(), Um::IMove);
	RepackScope scope;
	scope.pageIndex = m_doc->currentPageNumber();
	const RepackResult r = repackCore(scope);
	if (tx)
		tx.commit();
	m_repacking = false;
	if (r.moved || r.fitted || !r.parked.isEmpty())
	{
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
		if (m_mw)
			m_mw->emitUpdateRequest(0);
	}
	const int closed = qMax(0, r.gapsBefore - r.gapsAfter);
	const double closedMm = qMax(0.0, r.gapsBeforeMm - r.gapsAfterMm);
	QString line = tr("Re-packed %n module(s)", "", r.modules) + QStringLiteral(", ") + tr("closed %n gap(s) (%1 mm)", "", closed).arg(closedMm, 0, 'f', 0);
	if (r.fitted)
		line += QStringLiteral(", ") + tr("%n text frame(s) fitted", "", r.fitted);
	if (!r.parked.isEmpty())
		line += QStringLiteral(", ") + tr("%n parked on the pasteboard", "", r.parked.size());
	if (r.gapsAfter)
		line += QStringLiteral(", ") + tr("%n gap(s) left (%1 mm)", "", r.gapsAfter).arg(r.gapsAfterMm, 0, 'f', 0);
	if (r.overlapsAfter)
		line += QStringLiteral(", ") + tr("%n OVERLAP(S)", "", r.overlapsAfter);
	if (r.modules == 0)
		line = r.obituary ? tr("Re-pack: no movable obituary module on this page (pinned, locked or off the grid)")
		                  : tr("Re-pack: no News Browser story on this page to move (ads and hand-made frames stay)");
	QStringList details { line };
	if (r.notFitted)
		details << tr("%n old grouped module(s) moved but not fitted - right-click the page > Convert story groups to weld", "", r.notFitted);
	if (!r.parked.isEmpty())
		details << tr("No room any more - parked on the pasteboard right of the page:") + QStringLiteral("\n• ") + r.parked.join(QStringLiteral("\n• "));
	details << r.details;
	apiLog(QStringLiteral("re-pack page %1 (%2, %3 ms): %4").arg(scope.pageIndex + 1).arg(r.obituary ? QStringLiteral("obituary") : QStringLiteral("normal")).arg(clock.elapsed()).arg(details.join(QStringLiteral(" | "))));
	setStatus(line, !r.parked.isEmpty() || r.overlapsAfter > 0);
	setStatusDetails(details.join(QStringLiteral("\n")));
	if (m_mw && m_mw->statusBar())
		m_mw->statusBar()->showMessage(line, 10000);
	if (interactive && !r.parked.isEmpty())
	{
		QMessageBox box(QMessageBox::Warning, tr("Re-pack Page"), line, QMessageBox::Ok, m_mw ? static_cast<QWidget*>(m_mw) : this);
		box.setInformativeText(tr("These no longer fit on the page and were moved to the pasteboard (Undo puts them back):") + QStringLiteral("\n• ")
		                       + r.parked.join(QStringLiteral("\n• ")));
		box.setDetailedText(details.join(QStringLiteral("\n")));
		box.exec();
	}
	return line;
}

// ---------------------------------------------------------------------------
// Re-pack Automatically
// ---------------------------------------------------------------------------

void SuneerNewsPanel::noteRepackDelete(const QList<PageItem*>& items)
{
	if (!m_doc || m_repacking || m_removing || m_preview || !repackAutoPref() || m_doc->isLoading())
		return;
	int pageIdx = -1;
	QList<QPair<double, double>> ranges;
	for (PageItem* it : items)
	{
		if (!it || !isObitItem(it) || attr(it, "news.parked") == QLatin1String("1") || attr(it, "news.role") == QLatin1String("flag"))
			continue;
		const QRectF r = it->getBoundingRect();
		const int p = m_doc->OnPage(r.center().x(), r.center().y());
		if (p < 0)
			continue;
		pageIdx = p;
		ranges << qMakePair(r.left(), r.right());
	}
	if (pageIdx < 0)
		return;
	if (m_repackPending.pageIndex != pageIdx)
		m_repackPending = RepackScope();
	m_repackPending.pageIndex = pageIdx;
	m_repackPending.autoRun = true;
	m_repackPending.xRanges << ranges;
	if (!m_repackTimer)
	{
		m_repackTimer = new QTimer(this);
		m_repackTimer->setSingleShot(true);
		connect(m_repackTimer, &QTimer::timeout, this, &SuneerNewsPanel::runAutoRepack);
	}
	m_repackTimer->start(1200);
}

void SuneerNewsPanel::noteRepackTextEdit(PageItem* item)
{
	if (!m_doc || !item || m_repacking || m_preview || !repackAutoPref() || !item->isTextFrame() || item->isGroupChild() || !isObitItem(item))
		return;
	const QRectF r = item->getBoundingRect();
	const int p = m_doc->OnPage(r.center().x(), r.center().y());
	if (p < 0)
		return;
	if (m_repackPending.pageIndex != p)
		m_repackPending = RepackScope();
	m_repackPending.pageIndex = p;
	m_repackPending.autoRun = true;
	m_repackPending.xRanges << qMakePair(r.left(), r.right());
	m_repackPending.fitOnly << QPointer<PageItem>(item);
	if (!m_repackTimer)
	{
		m_repackTimer = new QTimer(this);
		m_repackTimer->setSingleShot(true);
		connect(m_repackTimer, &QTimer::timeout, this, &SuneerNewsPanel::runAutoRepack);
	}
	m_repackTimer->start(1200);
}

void SuneerNewsPanel::runAutoRepack()
{
	if (!m_doc || m_repackPending.pageIndex < 0)
	{
		m_repackPending = RepackScope();
		return;
	}
	// busy (typing again, dragging, an undo being replayed): later
	const bool busy = m_doc->isLoading() || m_doc->isUndoRedoOngoing() || m_doc->appMode == modeEdit || m_repacking || m_removing
	                  || (m_doc->view() && m_doc->view()->mousePressed());
	if (busy)
	{
		m_repackTimer->start(800);
		return;
	}
	const RepackScope scope = m_repackPending;
	m_repackPending = RepackScope();
	if (scope.pageIndex >= m_doc->Pages->count())
		return;
	// the pasteboard of another page than the one in view would be wrong: only the page in view
	if (scope.pageIndex != m_doc->currentPageNumber())
	{
		apiLog(QStringLiteral("auto re-pack: page %1 is not the current page - skipped").arg(scope.pageIndex + 1));
		return;
	}
	m_repacking = true;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Auto re-pack"), QString(), Um::IMove);
	const RepackResult r = repackCore(scope);
	if (tx)
		tx.commit();
	m_repacking = false;
	if (!(r.moved || r.fitted || !r.parked.isEmpty()))
		return;
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	QString line = tr("Auto re-pack: %n module(s) moved", "", r.moved);
	if (r.fitted)
		line += QStringLiteral(", ") + tr("%n text frame(s) fitted", "", r.fitted);
	if (!r.parked.isEmpty())
		line += QStringLiteral(", ") + tr("parked on the pasteboard: %1").arg(r.parked.join(QStringLiteral(", ")));
	line += QStringLiteral(" (") + tr("Ctrl+Z undoes only the re-pack") + QStringLiteral(")");
	apiLog(QStringLiteral("auto re-pack page %1: %2 | %3").arg(scope.pageIndex + 1).arg(line, r.details.join(QStringLiteral(" | "))));
	setStatus(line, !r.parked.isEmpty());
	setStatusDetails((QStringList{line} + r.details).join(QStringLiteral("\n")));
	if (m_mw && m_mw->statusBar())
		m_mw->statusBar()->showMessage(line, 10000);
}

// ---------------------------------------------------------------------------
// SUNEER_REPACK_SELFTEST=<report>: tests/suneer/repack_page.sh
//   SUNEER_REPACK_SAVEAS=<sla>   the page after the re-pack
//   SUNEER_REPACK_AUTO=<story ids, comma separated>  instead: Re-pack Automatically after deleting those stories
// Checks independently of the engine: no overlap between modules / with fixed frames, nothing
// below the bottom margin, undo back to the start, redo to the result.
// ---------------------------------------------------------------------------

void SuneerNewsPanel::runRepackSelfTest(const QString& outPath)
{
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) || !m_doc)
	{
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	QTextStream ts(&out);
	auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "REPACK-SELFTEST %s\n", qPrintable(l)); };
	// the undo widgets keep their own lists; a code-driven undo underflows them
	if (m_mw)
		for (UndoGui* g : m_mw->findChildren<UndoGui*>())
			UndoManager::instance()->removeGui(g);
	ScPage* page = m_doc->currentPage();
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	struct Geo { QPointer<PageItem> it; QRectF r; };
	auto snap = [&]() {
		QList<Geo> l;
		for (PageItem* it : m_doc->DocItems)
			if (it && !it->isGroupChild() && attr(it, "news.rule") != QLatin1String("1"))
				l << Geo{ it, QRectF(it->xPos(), it->yPos(), it->width(), it->height()) };
		return l;
	};
	auto diff = [&](const QList<Geo>& a) {
		int n = 0;
		for (const Geo& g : a)
		{
			if (!g.it)
			{
				++n;
				continue;
			}
			const QRectF r(g.it->xPos(), g.it->yPos(), g.it->width(), g.it->height());
			if (qAbs(r.x() - g.r.x()) > 0.05 || qAbs(r.y() - g.r.y()) > 0.05 || qAbs(r.width() - g.r.width()) > 0.05 || qAbs(r.height() - g.r.height()) > 0.05)
			{
				if (n < 6)
					line(QStringLiteral("  differs: %1 %2,%3 %4x%5 -> %6,%7 %8x%9").arg(g.it->itemName()).arg(g.r.x(), 0, 'f', 2).arg(g.r.y(), 0, 'f', 2).arg(g.r.width(), 0, 'f', 2)
					     .arg(g.r.height(), 0, 'f', 2).arg(r.x(), 0, 'f', 2).arg(r.y(), 0, 'f', 2).arg(r.width(), 0, 'f', 2).arg(r.height(), 0, 'f', 2));
				++n;
			}
		}
		return n;
	};
	// independent check: story modules (by news.story / weld cluster) vs each other and vs every other frame
	auto audit = [&](const QString& tag) {
		QHash<QString, QRectF> mod;
		QList<QRectF> other;
		const double bottom = page->yOffset() + page->height() - page->Margins.bottom();
		for (PageItem* it : m_doc->DocItems)
		{
			if (!it || it->isGroupChild() || !it->getBoundingRect().intersects(pageRect) || attr(it, "news.rule") == QLatin1String("1"))
				continue;
			if (it->isTextFrame() && it->itemText.length() == 0 && it->fillColor() == CommonStrings::None && it->lineColor() == CommonStrings::None)
				continue;
			QString k = storyKeyOf(it);
			if (k.isEmpty() && it->isWelded())
				for (PageItem* w : SuneerWeld::cluster(it))
					if (!storyKeyOf(w).isEmpty())
						k = storyKeyOf(w);
			if (k.isEmpty())
				other << geomRect(it);
			else
				mod[k] = mod.contains(k) ? mod[k].united(geomRect(it)) : geomRect(it);
		}
		const QList<QString> keys = mod.keys();
		int ov = 0, low = 0;
		for (int a = 0; a < keys.size(); ++a)
		{
			if (mod[keys[a]].bottom() > bottom + kTol)
				++low;
			for (int b = a + 1; b < keys.size(); ++b)
				if (hits(mod[keys[a]], mod[keys[b]]))
					++ov;
			for (const QRectF& o : other)
				if (hits(mod[keys[a]], o))
					++ov;
		}
		line(QStringLiteral("%1: %2 modules, %3 overlaps (module/module + module/other frame), %4 below the bottom margin").arg(tag).arg(keys.size()).arg(ov).arg(low));
		return ov;
	};

	// SUNEER_REPACK_SHOT=<prefix>: <prefix>-<tag>.png, the page as Scribus draws it (1600 px)
	const QString shot = qEnvironmentVariable("SUNEER_REPACK_SHOT");
	auto picture = [&](const QString& tag) {
		if (shot.isEmpty() || !m_mw || !m_mw->view)
			return;
		const QImage img = m_mw->view->PageToPixmap(m_doc->currentPageNumber(), 1600);
		line(QStringLiteral("picture %1-%2.png: %3").arg(shot, tag, img.save(shot + QLatin1Char('-') + tag + QStringLiteral(".png")) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
	};
	if (m_mw && m_mw->scrActions.contains(QStringLiteral("suneerRepackPage")))
		line(QStringLiteral("Layout > Re-pack Page key: '%1'").arg(m_mw->scrActions[QStringLiteral("suneerRepackPage")]->shortcut().toString()));
	const QString autoIds = qEnvironmentVariable("SUNEER_REPACK_AUTO");
	int fails = 0;
	picture(QStringLiteral("before"));
	const int ovStart = audit(QStringLiteral("start"));
	const QList<Geo> before = snap();
	if (autoIds.isEmpty())
	{
		const QString res = repackPage(false);
		line(QStringLiteral("result: ") + res);
		for (const QString& d : (m_statusLog.isEmpty() ? QString() : m_statusLog.last()).split('\n'))
			line(QStringLiteral("  ") + d);
		const QList<Geo> after = snap();
		picture(QStringLiteral("after"));
		if (audit(QStringLiteral("after")) > ovStart)
		{
			++fails;
			line(QStringLiteral("FAIL new overlaps"));
		}
		const QString saveAs = qEnvironmentVariable("SUNEER_REPACK_SAVEAS");
		if (!saveAs.isEmpty())
			line(QStringLiteral("saved %1: %2").arg(saveAs, m_doc->save(saveAs) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
		UndoManager::instance()->undo(1);
		const int u = diff(before);
		line(QStringLiteral("undo: %1 frame(s) differ from the start - %2").arg(u).arg(u ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
		fails += u ? 1 : 0;
		UndoManager::instance()->redo(1);
		const int rd = diff(after);
		line(QStringLiteral("redo: %1 frame(s) differ from the re-packed page - %2").arg(rd).arg(rd ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
		fails += rd ? 1 : 0;
	}
	else
	{
		// Re-pack Automatically: delete the stories, wait, check, undo the re-pack only, undo the delete
		setRepackAutoPref(true);
		QList<PageItem*> del;
		for (const QString& id : autoIds.split(',', Qt::SkipEmptyParts))
			for (PageItem* it : m_doc->DocItems)
				if (it && !it->isGroupChild() && storyKeyOf(it) == id.trimmed())
					del << it;
		line(QStringLiteral("auto: deleting %1 frame(s) of %2").arg(del.size()).arg(autoIds));
		m_doc->m_Selection->clear();
		for (PageItem* it : del)
			m_doc->m_Selection->addItem(it);
		m_doc->itemSelection_DeleteItem();
		QElapsedTimer wait;
		wait.start();
		while (wait.elapsed() < 4000)
			qApp->processEvents(QEventLoop::AllEvents, 50);
		const QList<Geo> afterAuto = snap();
		picture(QStringLiteral("after-auto"));
		line(QStringLiteral("auto result: ") + statusText());
		for (const QString& d : (m_statusLog.isEmpty() ? QString() : m_statusLog.last()).split('\n'))
			line(QStringLiteral("  ") + d);
		if (audit(QStringLiteral("after auto")) > ovStart)
		{
			++fails;
			line(QStringLiteral("FAIL new overlaps"));
		}
		const QString saveAs = qEnvironmentVariable("SUNEER_REPACK_SAVEAS");
		if (!saveAs.isEmpty())
			line(QStringLiteral("saved %1: %2").arg(saveAs, m_doc->save(saveAs) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
		// undo 1 = the re-pack only: every frame left on the page is back where it was before (the deleted ones stay gone)
		UndoManager::instance()->undo(1);
		QList<Geo> survivors;
		for (const Geo& g : before)
			if (g.it && !del.contains(g.it.data()))
				survivors << g;
		const int u1 = diff(survivors);
		line(QStringLiteral("undo 1 (re-pack only): %1 frame(s) differ - %2").arg(u1).arg(u1 ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
		fails += u1 ? 1 : 0;
		while (wait.elapsed() < 7000)
			qApp->processEvents(QEventLoop::AllEvents, 50);
		line(QStringLiteral("no new auto re-pack after the undo: %1").arg(diff(survivors) ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
		UndoManager::instance()->undo(1);
		int back = 0;
		for (PageItem* it : m_doc->DocItems)
			if (it && !it->isGroupChild())
				for (const QString& id : autoIds.split(',', Qt::SkipEmptyParts))
					if (storyKeyOf(it) == id.trimmed())
						++back;
		line(QStringLiteral("undo 2 (the delete): %1 frame(s) of the deleted stories back - %2").arg(back).arg(back == del.size() ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
		fails += back == del.size() ? 0 : 1;
		// redo both: the delete, then the re-pack - the page of the automatic run again, and no third run
		UndoManager::instance()->redo(1);
		UndoManager::instance()->redo(1);
		while (wait.elapsed() < 10000)
			qApp->processEvents(QEventLoop::AllEvents, 50);
		const int r2 = diff(afterAuto);
		line(QStringLiteral("redo 2: %1 frame(s) differ from the automatic result - %2").arg(r2).arg(r2 ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
		fails += r2 ? 1 : 0;
		setRepackAutoPref(false);
	}
	line(fails ? QStringLiteral("FAILED: %1").arg(fails) : QStringLiteral("ALL PASS"));
	const int hold = qEnvironmentVariable("SUNEER_REPACK_HOLD").toInt();
	QTimer::singleShot(qMax(0, hold) * 1000, qApp, [fails]() { qApp->exit(fails ? 1 : 0); });
}
