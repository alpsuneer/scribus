/*
 News Browser - "Adjust gaps" on placed stories.

 Member functions of SuneerNewsPanel. A placed story is one text frame (kicker,
 headline, body as paragraphs) with its photo / caption frames wrapped in it,
 so its gaps are:

   headline -> body, kicker -> headline   space after those paragraphs (style + extra)
   body column gutter                      the text frame's column gap
   photo -> text beside / above-below      wrap offsets / the photo stack (layoutStory)
   photo -> caption, caption -> text       the photo stack (layoutStory)
   story to story, horizontal / vertical   the grid gutter / the stacking (page only)

 applyGaps() re-lays the stories in place with layoutStory() (the routine the
 placement uses) under m_gapOverride, then makes sure nothing runs into another
 frame: a story that has grown stops where the next frame (or the page's type
 area) begins - its text frame gets shorter and shows the overflow marker.
 With the page gaps every story goes to the grid with the horizontal gap and the
 modules are stacked with the vertical gap; a story with no room left goes to the
 pasteboard (listed). The gaps are remembered on the items (layout.model,
 layout.gaps, layout.gapsUser) so a re-layout keeps them.
*/
#include "suneer_news_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerlayoutengine.h"
#include "suneerlayoutmodel.h"
#include "text/specialchars.h"
#include "undomanager.h"
#include "undostate.h"
#include "undotransaction.h"

namespace {
const double kMm = 72.0 / 25.4;
const double kTol = 0.5;

QRectF rectOf(const PageItem* it)
{
	return it->isGroup() || it->rotation() != 0.0 ? it->getBoundingRect() : QRectF(it->xPos(), it->yPos(), it->width(), it->height());
}

bool overlapsH(const QRectF& a, const QRectF& b)
{
	return a.right() > b.left() + kTol && a.left() < b.right() - kTol;
}

bool overlaps(const QRectF& a, const QRectF& b)
{
	return overlapsH(a, b) && a.bottom() > b.top() + kTol && a.top() < b.bottom() - kTol;
}

// paragraph [start, end] around pos (end = the PARSEP, or the story length)
int paraStart(const StoryText& st, int pos)
{
	int s = qMin(pos, st.length());
	while (s > 0 && st.text(s - 1) != SpecialChars::PARSEP)
		--s;
	return s;
}

int paraEnd(const StoryText& st, int pos)
{
	int e = pos;
	while (e < st.length() && st.text(e) != SpecialChars::PARSEP)
		++e;
	return qMin(e, st.length());
}
/*
 The live preview of "Adjust gaps" runs with undo off and is taken back from this
 snapshot (not with UndoManager::undo(): with "Item Action Mode" on, undo works on
 the selected item only, and the undo toolbar list would not match - that crashed in
 UndoWidget::updateUndo). Everything applyGaps() may change on the page is in it.
 */
struct GapSnapshot
{
	struct Item
	{
		QPointer<PageItem> item;
		double x { 0 }, y { 0 }, w { 0 }, h { 0 };
		double ixs { 1 }, iys { 1 }, ixo { 0 }, iyo { 0 };
		int cols { 1 };
		double colGap { 0 };
		double dist[4] { 0, 0, 0, 0 };
		double wrap[4] { 0, 0, 0, 0 };
		int flow { 0 };
		int ownPage { 0 };
		ObjAttrVector attrs;
		QList<QPointer<PageItem>> welded;
	};
	struct Para { QPointer<PageItem> text; int pos { 0 }; ParagraphStyle style; };
	QList<Item> items;
	QList<Para> paras;
	MarginStruct scratch;
	bool taken { false };

	void take(ScribusDoc* doc, const QList<PageItem*>& list)
	{
		items.clear();
		paras.clear();
		QSet<PageItem*> seen;
		QList<PageItem*> all;
		for (PageItem* it : list)
		{
			if (!it || seen.contains(it))
				continue;
			seen.insert(it);
			all << it;
			if (it->isGroup())
				for (PageItem* c : it->getAllChildren())
					if (!seen.contains(c))
					{
						seen.insert(c);
						all << c;
					}
		}
		for (PageItem* it : all)
		{
			Item s;
			s.item = it;
			s.x = it->xPos(); s.y = it->yPos(); s.w = it->width(); s.h = it->height();
			s.ixs = it->imageXScale(); s.iys = it->imageYScale(); s.ixo = it->imageXOffset(); s.iyo = it->imageYOffset();
			s.cols = it->columns(); s.colGap = it->columnGap();
			s.dist[0] = it->textToFrameDistLeft(); s.dist[1] = it->textToFrameDistRight(); s.dist[2] = it->textToFrameDistTop(); s.dist[3] = it->textToFrameDistBottom();
			s.wrap[0] = it->wrapOffsetLeft(); s.wrap[1] = it->wrapOffsetRight(); s.wrap[2] = it->wrapOffsetTop(); s.wrap[3] = it->wrapOffsetBottom();
			s.flow = (int) it->textFlowMode();
			s.ownPage = it->OwnPage;
			s.attrs = *it->getObjectAttributes();
			for (const PageItem::WeldingInfo& wi : it->weldList)
				s.welded << QPointer<PageItem>(wi.weldItem);
			items << s;
			if (it->isTextFrame())
			{
				// the head paragraphs (kicker, headline ...): their space after changes
				const StoryText& st = it->itemText;
				int pos = 0;
				for (int n = 0; n < 6 && pos < st.length(); ++n)
				{
					const int end = paraEnd(st, pos);
					paras << Para { QPointer<PageItem>(it), end, st.paragraphStyle(end) };
					pos = end + 1;
				}
			}
		}
		scratch = *doc->scratch();
		taken = true;
	}

	void restore(ScribusDoc* doc)
	{
		if (!taken)
			return;
		UndoBlocker blocker;
		for (const Item& s : items)
			if (s.item && s.item->isWelded())
				s.item->unWeld();
		for (const Item& s : items)
		{
			PageItem* it = s.item.data();
			if (!it)
				continue;
			it->setXYPos(s.x, s.y, true);
			it->setWidthHeight(s.w, s.h, true);
			it->setImageXYScale(s.ixs, s.iys);
			it->setImageXYOffset(s.ixo, s.iyo);
			if (it->isTextFrame())
			{
				it->setColumns(s.cols);
				it->setColumnGap(s.colGap);
				it->setTextToFrameDist(s.dist[0], s.dist[1], s.dist[2], s.dist[3]);
			}
			it->setWrapOffsets(s.wrap[2], s.wrap[3], s.wrap[0], s.wrap[1]);   // (top, bottom, left, right)
			it->setTextFlowMode((PageItem::TextFlowMode) s.flow);
			it->OwnPage = s.ownPage;
			ObjAttrVector a = s.attrs;
			it->setObjectAttributes(&a);
		}
		for (const Para& p : paras)
		{
			if (!p.text || p.pos > p.text->itemText.length())
				continue;
			StoryText& st = p.text->itemText;
			ParagraphStyle gap;
			gap.setGapAfter(0.0);
			st.eraseStyle(p.pos, gap);
			st.applyStyle(p.pos, p.style);
		}
		for (const Item& s : items)
			for (const QPointer<PageItem>& w : s.welded)
				if (s.item && w)
					s.item->weldTo(w.data());
		for (const Item& s : items)
		{
			PageItem* it = s.item.data();
			if (!it)
				continue;
			if (it->isGroup())
				it->asGroupFrame()->adjustXYPosition();
			it->updateClip();
			it->setRedrawBounding();
			if (it->isTextFrame())
				it->invalidateLayout();
		}
		*doc->scratch() = scratch;
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
};
} // namespace

SuneerSpacing::Values SuneerNewsPanel::spacingValues() const
{
	if (m_gapOverride)
		return *m_gapOverride;
	return SuneerSpacing::forModel(spacingModel());
}

SuneerSpacing::Values SuneerNewsPanel::storyGaps(const PageItem* storyText, QString* modelId) const
{
	QString id = storyText ? attr(storyText, QStringLiteral("layout.model")) : QString();
	if (id.isEmpty() || !SuneerLayoutModels::exists(id))
		id = activeModelId();
	if (modelId)
		*modelId = id;
	SuneerSpacing::Values v = SuneerSpacing::forModel(id);
	if (storyText && attr(storyText, QStringLiteral("layout.gapsUser")) == QLatin1String("1"))
	{
		SuneerSpacing::Values own = v;
		if (SuneerSpacing::gapsFromString(attr(storyText, QStringLiteral("layout.gaps")), own))
			for (SuneerSpacing::Key k : SuneerSpacing::gapKeys())
				if (SuneerSpacing::isStoryGap(k))
					v.v[k] = own.v[k];
	}
	return v;
}

void SuneerNewsPanel::rememberGaps(const QList<PageItem*>& items, const QString& modelId, const SuneerSpacing::Values& v, bool user)
{
	const QString gaps = SuneerSpacing::gapsToString(v);
	const QString userFlag = user ? QStringLiteral("1") : QString();
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		const QList<QPair<QString, QString>> values = { {QStringLiteral("layout.model"), modelId}, {QStringLiteral("layout.gaps"), gaps},
		                                                {QStringLiteral("layout.gapsUser"), user ? userFlag : attr(it, QStringLiteral("layout.gapsUser"))} };
		for (const auto& nv : values)
		{
			const QString old = attr(it, nv.first);
			if (old == nv.second)
				continue;
			if (UndoManager::undoEnabled())
			{
				auto* ss = new SimpleState(tr("Gaps"), nv.first, Um::IGroup);
				ss->set("SUNEER_ATTR");
				ss->set("NAME", nv.first);
				ss->set("OLD", old);
				ss->set("NEW", nv.second);
				UndoManager::instance()->action(it, ss);
			}
			setAttr(it, nv.first, nv.second);
		}
	}
}

void SuneerNewsPanel::applyHeadGaps(PageItem* text, const SuneerSpacing::Values& v)
{
	if (!text || !text->isTextFrame() || !m_doc)
		return;
	StoryText& st = text->itemText;
	const SuneerLayout::StyleNames names = SuneerLayout::StyleNames::fromSettings(panelTitleStyle());
	int headlinePos = -1;
	SuneerLayout::storyHead(st, names, &headlinePos);
	if (headlinePos < 0)
		return;
	// the gaps set last time (placement / Adjust gaps): a space after that is neither the style's nor
	// the one we set was typed by hand and stays
	SuneerSpacing::Values prev {};
	const bool havePrev = SuneerSpacing::gapsFromString(attr(text, QStringLiteral("layout.gaps")), prev);
	// space after = the paragraph style's own space after + the gap (the placement adds it the same way)
	auto setGapAfter = [&](int pos, double extraPt, SuneerSpacing::Key key) {
		const int end = paraEnd(st, pos);
		const ParagraphStyle cur = st.paragraphStyle(pos);
		double base = 0.0;
		const QString parent = cur.parent();
		if (!parent.isEmpty() && m_doc->paragraphStyles().contains(parent))
			base = m_doc->paragraphStyles().get(parent).gapAfter();
		else
			base = (m_doc->paragraphStyles().getDefault() ? m_doc->paragraphStyles().getDefault()->gapAfter() : 0.0);
		const double want = base + extraPt;
		if (qAbs(cur.gapAfter() - want) < 0.01)
			return;
		const bool ours = qAbs(cur.gapAfter() - base) < 0.01 || (havePrev && qAbs(cur.gapAfter() - base - prev.pt(key)) < 0.01);
		if (!ours)
		{
			apiLog(QStringLiteral("adjust gaps: \"%1\" keeps its hand-set space after (%2 pt, style %3 pt)").arg(text->itemName()).arg(cur.gapAfter(), 0, 'f', 2).arg(base, 0, 'f', 2));
			return;
		}
		ParagraphStyle ps;
		ps.setGapAfter(want);
		if (UndoManager::undoEnabled())
		{
			auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
			is->set("APPLY_PARASTYLE");
			is->set("POS", end);
			is->setStates(st.paragraphStyle(end), ps);
			UndoManager::instance()->action(text, is);
		}
		st.applyStyle(end, ps, false);
		text->invalidateLayout();
	};
	setGapAfter(headlinePos, v.pt(SuneerSpacing::HeadlineBody), SuneerSpacing::HeadlineBody);
	// the subheading / kicker: the head paragraph right before the headline
	const int hs = paraStart(st, headlinePos);
	if (hs > 0)
		setGapAfter(paraStart(st, hs - 1), v.pt(SuneerSpacing::KickerHeadline), SuneerSpacing::KickerHeadline);
}

QList<PageItem*> SuneerNewsPanel::storyTextsOnPage() const
{
	QList<PageItem*> out;
	if (!m_doc || !m_doc->currentPage())
		return out;
	const ScPage* page = m_doc->currentPage();
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	for (PageItem* it : m_doc->DocItems)
	{
		if (!it || it->isGroupChild() || it->locked() || !it->isTextFrame())
			continue;
		if (attr(it, QStringLiteral("news.role")) != QLatin1String("text") || attr(it, QStringLiteral("news.story")).isEmpty())
			continue;
		if (attr(it, QStringLiteral("news.manual")) == QLatin1String("1") || attr(it, QStringLiteral("news.parked")) == QLatin1String("1"))
			continue;
		if (!pageRect.contains(rectOf(it).center()))
			continue;
		out << it;
	}
	return out;
}

SuneerNewsPanel::GapResult SuneerNewsPanel::applyGaps(const QList<PageItem*>& storyTexts, const SuneerSpacing::Values& v, bool pageGaps, bool remember)
{
	GapResult res;
	if (!m_doc || !m_doc->currentPage())
		return res;
	ScPage* page = m_doc->currentPage();
	const int pageIndex = m_doc->currentPageNumber();
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	const QRectF typeArea = pageRect.adjusted(page->Margins.left(), page->Margins.top(), -page->Margins.right(), -page->Margins.bottom());
	const SuneerLayout::StyleNames names = SuneerLayout::StyleNames::fromSettings(panelTitleStyle());

	struct Story
	{
		PageItem* text { nullptr };
		QString id, modelId, title;
		QList<PageItem*> items;
		QRectF before;
	};
	QList<Story> stories;
	QSet<PageItem*> storyItemSet;
	for (PageItem* t : storyTexts)
	{
		if (!t || !t->isTextFrame())
			continue;
		Story s;
		s.text = t;
		s.id = attr(t, QStringLiteral("news.story"));
		s.items = storyItems(s.id);
		if (s.items.isEmpty())
			s.items << t;
		s.title = t->itemName();
		storyGaps(t, &s.modelId);
		s.before = itemsRect(s.items);
		for (PageItem* it : s.items)
			storyItemSet.insert(it);
		stories << s;
	}
	if (stories.isEmpty())
		return res;

	// everything else on the page (and its master items) is where a story must stop
	QList<QRectF> fixed;
	for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIndex))
	{
		if (storyItemSet.contains(it) || attr(it, QStringLiteral("news.rule")) == QLatin1String("1"))
			continue;
		fixed << rectOf(it);
	}
	for (const SuneerLayout::PageIn::Kept& k : SuneerLayout::masterKept(m_doc, pageIndex))
		fixed << k.rect.translated(pageRect.topLeft());

	// the grid the stories sit on now, and with the new horizontal gap
	const SuneerLayoutModel model = activeModel();
	const int guideCols = page->guides.verticalAutoCount() > 0 ? page->guides.verticalAutoCount() + 1 : 0;
	const SuneerLayout::Grid oldGrid = SuneerLayout::gridFor(model, page->width(), page->height(), page->Margins.top(), page->Margins.bottom(),
	                                                         page->Margins.left(), page->Margins.right(), guideCols);
	SuneerLayoutModel nm = model;
	nm.gutterMm = v.v[SuneerSpacing::ModuleGapH];
	const SuneerLayout::Grid newGrid = SuneerLayout::gridFor(nm, page->width(), page->height(), page->Margins.top(), page->Margins.bottom(),
	                                                         page->Margins.left(), page->Margins.right(), guideCols);

	const std::optional<SuneerSpacing::Values> savedOverride = m_gapOverride;
	m_gapOverride = v;
	struct Restore { SuneerNewsPanel* p; std::optional<SuneerSpacing::Values> v; ~Restore() { p->m_gapOverride = v; } } restore { this, savedOverride };

	// 1. every story re-laid in place with the gaps (layoutStory, as the placement does)
	for (Story& s : stories)
	{
		PageItem* text = s.text;
		QString pos = attr(text, QStringLiteral("news.photoPos"));
		if (pos.isEmpty())
			pos = QStringLiteral("right");
		int imgCols = attr(text, QStringLiteral("news.photoCols")).toInt();
		if (imgCols <= 0)
			imgCols = 2;
		int cols = attr(text, QStringLiteral("news.cols")).toInt();
		if (cols <= 0)
			cols = qMax(1, text->columns());
		StoryFrames f;
		f.text = text;
		QMap<int, PageItem*> photos, captions;
		for (PageItem* it : s.items)
		{
			const QString role = attr(it, QStringLiteral("news.role"));
			if (role == QLatin1String("photo"))
				photos.insert(attr(it, QStringLiteral("news.index")).toInt(), it);
			else if (role == QLatin1String("caption"))
				captions.insert(attr(it, QStringLiteral("news.index")).toInt(), it);
		}
		for (auto it = photos.constBegin(); it != photos.constEnd(); ++it)
		{
			f.photos << it.value();
			f.captions << captions.value(it.key(), nullptr);
		}
		const bool pasteboard = (text->OwnPage == -1);
		double x = text->xPos();
		const double y = s.before.top();          // the module's top (photos above the headline included)
		if (pageGaps && oldGrid.colW > 0)
		{
			const int col = qBound(0, qRound((x - pageRect.left() - oldGrid.content.left()) / (oldGrid.colW + oldGrid.gutter)), newGrid.cols - 1);
			x = pageRect.left() + newGrid.x(col);
		}
		applyHeadGaps(text, v);
		// the head block height at the story width (the photo starts under it)
		double totalW = 0, colGap = 0;
		storyGeometry(cols, totalW, colGap);
		text->setXYPos(x, y);
		text->setWidth(totalW);
		text->setColumns(qMax(1, cols));
		text->setColumnGap(v.pt(SuneerSpacing::ColumnGap));
		text->setHeight(qMax(text->height(), 200.0 * kMm));
		text->updateClip();
		text->invalidateLayout();
		text->layout();
		f.headH = attr(text, QStringLiteral("news.headH")).toDouble();
		const int firstBody = SuneerLayout::storyHead(text->itemText, names, nullptr);
		if (firstBody > 0 && firstBody < text->itemText.length())
		{
			const QLineF caret = text->textLayout.positionToPoint(firstBody);
			f.headH = qMax(0.0, qMin(caret.y1(), caret.y2()));
		}
		layoutStory(f, x, y, cols, imgCols, pos);
		setAttr(text, QStringLiteral("news.headH"), QString::number(f.headH, 'f', 3));
		if (pasteboard)
			for (PageItem* it : s.items)
				it->OwnPage = -1;
		++res.stories;
	}

	// a story stops where the next frame below it (or the type area) begins: shorter text frame, overflow marker
	const double gapV = v.pt(SuneerSpacing::ModuleGapV);
	auto stopAbove = [&](Story& s, const QList<QRectF>& others, double originalBottom) -> bool {
		const QRectF r = itemsRect(s.items);
		double limit = typeArea.bottom();
		for (const QRectF& o : others)
			if (overlapsH(o, r) && o.top() >= r.top() - kTol)
				limit = qMin(limit, o.top() - gapV);
		limit = qMax(limit, qMin(originalBottom, typeArea.bottom()));   // never less room than it had
		if (r.bottom() <= limit + kTol)
			return true;
		const double d = r.bottom() - limit;
		PageItem* text = s.text;
		const double textBottom = text->yPos() + text->height();
		const double newH = text->height() - d;
		if (newH < 12.0)
			return false;                      // not even the headline fits here
		text->setHeight(newH);
		text->updateClip();
		text->invalidateLayout();
		text->layout();
		// photos / captions under the text ("bottom") come up with it
		for (PageItem* it : s.items)
			if (it != text && it->yPos() >= textBottom - kTol)
				it->moveBy(0.0, -d);
		return true;
	};

	if (!pageGaps)
	{
		for (Story& s : stories)
		{
			QList<QRectF> others = fixed;
			for (const Story& o : stories)
				if (&o != &s)
					others << itemsRect(o.items);
			if (!stopAbove(s, others, s.before.bottom()))
				res.overflow << s.title;
		}
	}
	else
	{
		// 2. stacking: each module the vertical gap under the nearest module above it in the same columns;
		//    a module with none above keeps its top. Other modules (obit groups ...) are moved as they are.
		struct Unit { Story* story { nullptr }; QList<PageItem*> items; QRectF before; };
		QList<Unit> units;
		for (Story& s : stories)
			units << Unit { &s, s.items, s.before };
		for (PageItem* it : SuneerLayout::pageTopItems(m_doc, pageIndex))
		{
			if (storyItemSet.contains(it) || it->locked())
				continue;
			if (attr(it, QStringLiteral("news.model")).startsWith(QLatin1String("obituary")) && attr(it, QStringLiteral("news.manual")) != QLatin1String("1"))
			{
				units << Unit { nullptr, { it }, rectOf(it) };
				fixed.removeOne(rectOf(it));
			}
		}
		std::stable_sort(units.begin(), units.end(), [&](const Unit& a, const Unit& b) {
			const QRectF ra = itemsRect(a.items), rb = itemsRect(b.items);
			return qAbs(ra.top() - rb.top()) > kTol ? ra.top() < rb.top() : ra.left() < rb.left();
		});
		QList<QRectF> settled;
		QList<QList<PageItem*>> park;
		for (Unit& u : units)
		{
			QRectF r = itemsRect(u.items);
			double above = -1e9;
			for (const QRectF& s : settled)
				if (overlapsH(s, r) && s.top() < r.top() + kTol)
					above = qMax(above, s.bottom());
			if (above > -1e8)
			{
				const double dy = above + gapV - r.top();
				if (qAbs(dy) > 0.05)
				{
					moveStory(u.items, 0.0, dy);
					++res.moved;
				}
			}
			r = itemsRect(u.items);
			// a fixed frame (advert, masthead ...) in the way: under it
			for (const QRectF& o : fixed)
				if (overlaps(o, r) && o.top() <= r.top() + kTol)
				{
					moveStory(u.items, 0.0, o.bottom() + gapV - r.top());
					r = itemsRect(u.items);
				}
			bool fits = true;
			if (u.story)
				fits = stopAbove(*u.story, fixed, u.before.bottom() > typeArea.bottom() ? u.before.bottom() : r.top());
			r = itemsRect(u.items);
			if (!fits || r.top() > typeArea.bottom() - 12.0)
			{
				park << u.items;
				if (u.story)
					res.parked << u.story->title;
				continue;
			}
			settled << r;
		}
		if (!park.isEmpty())
			parkOnPasteboard(park);
		// separator rules are deleted and drawn again: only when it is the real (undoable) change, not in a preview
		if (UndoManager::undoEnabled())
			respaceRules(v);
	}

	for (Story& s : stories)
	{
		if (s.text->isTextFrame())
		{
			s.text->invalidateLayout();
			s.text->layout();
			if (s.text->frameOverflows() && !res.overflow.contains(s.title) && !res.parked.contains(s.title))
				res.overflow << s.title;
		}
		if (remember)
			rememberGaps(s.items, s.modelId, v, true);
	}
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	apiLog(QStringLiteral("adjust gaps: %1 stories%2, %3 moved, %4 overflow, %5 to the pasteboard; %6")
	       .arg(res.stories).arg(pageGaps ? QStringLiteral(" (page)") : QString()).arg(res.moved).arg(res.overflow.size()).arg(res.parked.size())
	       .arg(SuneerSpacing::gapsToString(v)));
	return res;
}

void SuneerNewsPanel::adjustGaps(PageItem* item)
{
	if (!m_doc || !item || !isNewsStory(item))
		return;
	PageItem* text = nullptr;
	for (PageItem* it : storyItems(attr(item, QStringLiteral("news.story"))))
		if (attr(it, QStringLiteral("news.role")) == QLatin1String("text") && it->isTextFrame())
			text = it;
	if (!text)
	{
		setStatus(tr("Error: this story has no story frame"), true);
		return;
	}
	QString modelId;
	const SuneerSpacing::Values start = storyGaps(text, &modelId);
	const SuneerLayoutModel model = SuneerLayoutModels::byId(modelId);
	const SuneerSpacing::Values modelValues = model.spacing();
	const SuneerSpacing::Values house = SuneerSpacing::houseDefaults();

	QDialog dlg(m_mw);
	dlg.setWindowTitle(tr("Adjust gaps — %1").arg(text->itemName()));
	auto* lay = new QVBoxLayout(&dlg);
	auto* head = new QLabel(tr("Model: <b>%1</b>. Changes show on the page at once; OK keeps them (one undo step), Cancel puts the page back.").arg(model.name.toHtmlEscaped()), &dlg);
	head->setWordWrap(true);
	lay->addWidget(head);
	auto* grid = new QGridLayout();
	QHash<int, QDoubleSpinBox*> spins;
	int row = 0;
	for (SuneerSpacing::Key k : SuneerSpacing::gapKeys())
	{
		if (k == SuneerSpacing::ModuleGapH)
		{
			auto* sep = new QLabel(tr("<b>Page</b> (with \"Apply to all stories on this page\")"), &dlg);
			grid->addWidget(sep, row++, 0, 1, 3);
		}
		auto* lbl = new QLabel(SuneerSpacing::gapLabel(k), &dlg);
		auto* sp = new QDoubleSpinBox(&dlg);
		sp->setRange(0.0, 50.0);
		sp->setDecimals(1);
		sp->setSingleStep(0.1);
		sp->setSuffix(QStringLiteral(" mm"));
		sp->setKeyboardTracking(false);
		sp->setValue(start.v[k]);
		auto* reset = new QToolButton(&dlg);
		reset->setText(QStringLiteral("↺"));
		reset->setToolTip(tr("Back to the model's %1 mm (house default %2 mm)").arg(modelValues.v[k], 0, 'f', 1).arg(house.v[k], 0, 'f', 1));
		QObject::connect(reset, &QToolButton::clicked, sp, [sp, k, modelValues]() { sp->setValue(modelValues.v[k]); });
		grid->addWidget(lbl, row, 0);
		grid->addWidget(sp, row, 1);
		grid->addWidget(reset, row, 2);
		spins.insert(k, sp);
		++row;
	}
	lay->addLayout(grid);
	auto* allChk = new QCheckBox(tr("Apply to all stories on this page (%n stories, with the story-to-story gaps)", "", storyTextsOnPage().size()), &dlg);
	auto* saveChk = new QCheckBox(tr("Save these gaps to the model \"%1\"").arg(model.name), &dlg);
	lay->addWidget(allChk);
	lay->addWidget(saveChk);
	auto* houseBtn = new QPushButton(tr("All to house defaults"), &dlg);
	houseBtn->setToolTip(tr("Measured from our own pages (autolayout-rules.json); a gap the rules do not measure takes the built-in default."));
	auto* modelBtn = new QPushButton(tr("All to the model"), &dlg);
	auto* btnRow = new QHBoxLayout();
	btnRow->addWidget(modelBtn);
	btnRow->addWidget(houseBtn);
	btnRow->addStretch(1);
	lay->addLayout(btnRow);
	auto* info = new QLabel(&dlg);
	info->setWordWrap(true);
	lay->addWidget(info);
	auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	lay->addWidget(box);
	QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	auto pageRows = [&]() {
		for (SuneerSpacing::Key k : { SuneerSpacing::ModuleGapH, SuneerSpacing::ModuleGapV })
			spins[k]->setEnabled(allChk->isChecked());
	};
	pageRows();
	auto values = [&]() {
		SuneerSpacing::Values v = start;
		for (auto it = spins.constBegin(); it != spins.constEnd(); ++it)
			v.v[it.key()] = it.value()->value();
		if (!allChk->isChecked())
		{
			// one story: its width and place on the grid stay
			v.v[SuneerSpacing::ModuleGapH] = start.v[SuneerSpacing::ModuleGapH];
			v.v[SuneerSpacing::ModuleGapV] = start.v[SuneerSpacing::ModuleGapV];
		}
		return v;
	};

	// live preview: undo off, the page taken back from a snapshot before every new try (see GapSnapshot)
	GapResult last;
	GapSnapshot snap;
	const QString storyId = attr(text, QStringLiteral("news.story"));
	auto storyText = [&]() -> PageItem* {
		for (PageItem* it : storyItems(storyId))
			if (attr(it, QStringLiteral("news.role")) == QLatin1String("text"))
				return it;
		return nullptr;
	};
	auto targets = [&]() {
		if (allChk->isChecked())
			return storyTextsOnPage();
		PageItem* t = storyText();
		return t ? QList<PageItem*>{ t } : QList<PageItem*>();
	};
	auto showResult = [&]() {
		QString msg = tr("%n story/stories re-laid", "", last.stories);
		if (last.moved)
			msg += tr(", %n moved", "", last.moved);
		if (!last.overflow.isEmpty())
			msg += QStringLiteral("<br><span style='color:#C62828'>") + tr("Text no longer fits (overflow marker): %1").arg(last.overflow.join(QStringLiteral(", ")).toHtmlEscaped()) + QStringLiteral("</span>");
		if (!last.parked.isEmpty())
			msg += QStringLiteral("<br><span style='color:#C62828'>") + tr("No room left - moved to the pasteboard: %1").arg(last.parked.join(QStringLiteral(", ")).toHtmlEscaped()) + QStringLiteral("</span>");
		info->setText(msg);
	};
	auto preview = [&]() {
		if (!snap.taken)
		{
			QList<PageItem*> everything = SuneerLayout::pageTopItems(m_doc, m_doc->currentPageNumber());
			for (PageItem* t : storyTextsOnPage())
				everything << storyItems(attr(t, QStringLiteral("news.story")));
			everything << storyItems(storyId);
			snap.take(m_doc, everything);
		}
		else
			snap.restore(m_doc);
		{
			UndoBlocker blocker;
			last = applyGaps(targets(), values(), allChk->isChecked(), true);
		}
		showResult();
		if (m_mw && m_mw->view)
			m_mw->view->DrawNew();
	};
	QTimer timer;
	timer.setSingleShot(true);
	timer.setInterval(250);
	QObject::connect(&timer, &QTimer::timeout, &dlg, preview);
	for (QDoubleSpinBox* sp : spins)
		QObject::connect(sp, &QDoubleSpinBox::valueChanged, &dlg, [&timer]() { timer.start(); });
	QObject::connect(allChk, &QCheckBox::toggled, &dlg, [&]() { pageRows(); timer.start(); });
	QObject::connect(modelBtn, &QPushButton::clicked, &dlg, [&]() {
		for (auto it = spins.begin(); it != spins.end(); ++it)
			it.value()->setValue(modelValues.v[it.key()]);
	});
	QObject::connect(houseBtn, &QPushButton::clicked, &dlg, [&]() {
		for (auto it = spins.begin(); it != spins.end(); ++it)
			it.value()->setValue(house.v[it.key()]);
	});

	// at the right edge of the window, so the story stays in view while the gaps change
	dlg.adjustSize();
	if (m_mw)
	{
		const QRect mw = m_mw->geometry();
		dlg.move(qMax(mw.left(), mw.right() - dlg.width() - 24), mw.top() + 90);
	}
	const bool ok = dlg.exec() == QDialog::Accepted;
	timer.stop();
	snap.restore(m_doc);              // the page as it was; OK applies the gaps once more, undoably
	if (!ok)
	{
		if (m_mw && m_mw->view)
			m_mw->view->DrawNew();
		return;
	}
	{
		// recorded in the global undo mode, so the Undo menu / toolbar list gets the step even with
		// "Item Action Mode" on; that mode comes back afterwards
		UndoManager* um = UndoManager::instance();
		const bool objectMode = m_mw && m_mw->isObjectSpecificUndo();
		if (objectMode)
			um->showObject(UndoManager::GLOBAL_UNDO_MODE);
		UndoTransaction tx;
		if (UndoManager::undoEnabled())
			tx = um->beginTransaction(Um::Selection, Um::IGroup, tr("Adjust gaps"), QString(), Um::IGroup);
		last = applyGaps(targets(), values(), allChk->isChecked(), true);
		if (tx)
			tx.commit();
		if (objectMode)
		{
			const int n = m_doc->m_Selection->count();
			um->showObject(n == 1 ? (int) m_doc->m_Selection->itemAt(0)->getUId() : n == 0 ? (int) m_doc->currentPage()->getUId() : (int) UndoManager::NO_UNDO_STACK);
		}
	}
	if (m_mw && m_mw->view)
		m_mw->view->DrawNew();
	if (saveChk->isChecked())
	{
		SuneerLayoutModel m = SuneerLayoutModels::byId(modelId);
		SuneerSpacing::Values mv = m.spacing();
		const SuneerSpacing::Values v = values();
		for (SuneerSpacing::Key k : SuneerSpacing::gapKeys())
			if (SuneerSpacing::isStoryGap(k) || allChk->isChecked())
				mv.v[k] = v.v[k];
		m.setSpacing(mv);
		QString err;
		if (!SuneerLayoutModels::save(m, &err))
			QMessageBox::warning(m_mw, tr("Adjust gaps"), tr("The model could not be saved: %1").arg(err));
	}
	setStatus(tr("Gaps adjusted: %n story/stories", "", last.stories)
	          + (last.overflow.isEmpty() ? QString() : tr(" - text no longer fits: %1").arg(last.overflow.join(QStringLiteral(", "))))
	          + (last.parked.isEmpty() ? QString() : tr(" - to the pasteboard: %1").arg(last.parked.join(QStringLiteral(", ")))),
	          !last.overflow.isEmpty() || !last.parked.isEmpty());
}

// ---------------------------------------------------------------------------
// SUNEER_GAPS_SELFTEST
// ---------------------------------------------------------------------------

namespace {
// overlapping pairs of top-level frames on the page, parts of one story not counted
int overlapCount(ScribusDoc* doc, int pageIndex, QStringList* pairs)
{
	const QList<PageItem*> items = SuneerLayout::pageTopItems(doc, pageIndex);
	auto storyOf = [](const PageItem* it) {
		const QList<ObjectAttribute> l = it->getObjectAttributes(QStringLiteral("news.story"));
		return l.isEmpty() ? QString() : l.first().value;
	};
	auto isRule = [](const PageItem* it) {
		const QList<ObjectAttribute> l = it->getObjectAttributes(QStringLiteral("news.rule"));
		return !l.isEmpty() && l.first().value == QLatin1String("1");
	};
	int n = 0;
	for (int i = 0; i < items.size(); ++i)
		for (int j = i + 1; j < items.size(); ++j)
		{
			PageItem* a = items[i];
			PageItem* b = items[j];
			if (isRule(a) || isRule(b))
				continue;
			const QString sa = storyOf(a), sb = storyOf(b);
			if (!sa.isEmpty() && sa == sb)
				continue;
			if (overlaps(rectOf(a).adjusted(0.5, 0.5, -0.5, -0.5), rectOf(b).adjusted(0.5, 0.5, -0.5, -0.5)))
			{
				++n;
				if (pairs)
					*pairs << QStringLiteral("%1 / %2").arg(a->itemName(), b->itemName());
			}
		}
	return n;
}
} // namespace

void SuneerNewsPanel::runGapSelfTest(const QString& outPath)
{
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) || !m_doc)
	{
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	QTextStream ts(&out);
	bool allOk = true;
	const int pageIndex = m_doc->currentPageNumber();
	auto signature = [&]() {
		QStringList sig = SuneerLayout::resultSignature(m_doc, pageIndex, SuneerLayout::pageTopItems(m_doc, pageIndex));
		for (PageItem* t : storyTextsOnPage())
		{
			QString heads;
			const StoryText& st = t->itemText;
			for (int p = 0; p < st.length() && p < 2000; p = paraEnd(st, p) + 1)
				heads += QString::number(st.paragraphStyle(p).gapAfter(), 'f', 2) + QLatin1Char(',');
			sig << t->itemName() + QStringLiteral(" gaps ") + heads.left(60) + QStringLiteral(" cg") + QString::number(t->columnGap(), 'f', 2)
			     + QStringLiteral(" a:") + attr(t, QStringLiteral("layout.gaps")) + attr(t, QStringLiteral("layout.gapsUser"));
		}
		return sig;
	};
	auto check = [&](bool ok, const QString& what) {
		ts << (ok ? "PASS " : "FAIL ") << what << "\n";
		allOk = allOk && ok;
	};
	auto diffLines = [&](const QStringList& a, const QStringList& b) {
		int shown = 0;
		for (int i = 0; i < qMax(a.size(), b.size()) && shown < 8; ++i)
		{
			const QString x = i < a.size() ? a[i] : QString(), y = i < b.size() ? b[i] : QString();
			if (x != y)
			{
				ts << "    before: " << x << "\n    after:  " << y << "\n";
				++shown;
			}
		}
	};
	const QList<PageItem*> texts = storyTextsOnPage();
	ts << "page " << (pageIndex + 1) << ": " << texts.size() << " placed stories\n";
	QStringList pairs0;
	const int overlap0 = overlapCount(m_doc, pageIndex, &pairs0);
	ts << "overlapping frame pairs before: " << overlap0 << "\n";
	const QStringList sig0 = signature();
	UndoManager* um = UndoManager::instance();
	const SuneerLayout::StyleNames names = SuneerLayout::StyleNames::fromSettings(panelTitleStyle());

	// A. each story alone: bigger head gaps, wider gutter, photo / caption gaps
	int n = 0;
	for (PageItem* t : texts)
	{
		if (++n > qEnvironmentVariableIntValue("SUNEER_GAPS_SELFTEST_MAX") && qEnvironmentVariableIsSet("SUNEER_GAPS_SELFTEST_MAX"))
			break;
		if (n > 12)
			break;
		SuneerSpacing::Values v = storyGaps(t);
		v.v[SuneerSpacing::HeadlineBody] += 3.0;
		v.v[SuneerSpacing::KickerHeadline] += 2.0;
		v.v[SuneerSpacing::ColumnGap] = 5.0;
		v.v[SuneerSpacing::WrapPad] = 3.5;
		v.v[SuneerSpacing::CaptionGap] = 1.5;
		v.v[SuneerSpacing::CaptionText] = 3.0;
		UndoTransaction tx = um->beginTransaction(Um::Selection, Um::IGroup, QStringLiteral("Adjust gaps (test)"), QString(), Um::IGroup);
		const GapResult r = applyGaps({ t }, v, false, true);
		tx.commit();
		int headlinePos = -1;
		SuneerLayout::storyHead(t->itemText, names, &headlinePos);
		double base = 0.0, got = 0.0;
		if (headlinePos >= 0)
		{
			const QString parent = t->itemText.paragraphStyle(headlinePos).parent();
			base = m_doc->paragraphStyles().contains(parent) ? m_doc->paragraphStyles().get(parent).gapAfter() : 0.0;
			got = t->itemText.paragraphStyle(headlinePos).gapAfter();
		}
		QStringList pairs;
		const int ov = overlapCount(m_doc, pageIndex, &pairs);
		const bool headOk = headlinePos < 0 || qAbs(got - base - v.pt(SuneerSpacing::HeadlineBody)) < 0.05;
		const bool colOk = qAbs(t->columnGap() - 5.0 * kMm) < 0.05;
		const bool attrOk = attr(t, QStringLiteral("layout.gapsUser")) == QLatin1String("1") && !attr(t, QStringLiteral("layout.gaps")).isEmpty();
		check(headOk && colOk && attrOk && ov <= overlap0,
		      QStringLiteral("story \"%1\": headline space after %2 pt (style %3 + %4), column gap %5 mm, remembered %6, overlaps %7 (before %8)%9")
		      .arg(t->itemName()).arg(got, 0, 'f', 2).arg(base, 0, 'f', 2).arg(v.pt(SuneerSpacing::HeadlineBody), 0, 'f', 2)
		      .arg(t->columnGap() / kMm, 0, 'f', 2).arg(attrOk ? "yes" : "NO").arg(ov).arg(overlap0)
		      .arg(r.overflow.isEmpty() ? QString() : QStringLiteral(", overflow marker: ") + r.overflow.join(QStringLiteral(", "))));
		if (ov > overlap0)
			ts << "    new overlaps: " << pairs.join(QStringLiteral("; ")) << "\n";
		um->undo(1);
		const QStringList sig1 = signature();
		if (sig1 != sig0)
			diffLines(sig0, sig1);
		check(sig1 == sig0, QStringLiteral("  undo puts the page back (%1 of %2 entries differ)").arg(SuneerLayout::signatureDiff(sig0, sig1)).arg(sig0.size()));
	}

	// B. the whole page with the story-to-story gaps
	if (!texts.isEmpty())
	{
		SuneerSpacing::Values v = storyGaps(texts.first());
		v.v[SuneerSpacing::ModuleGapV] = 8.0;
		v.v[SuneerSpacing::ModuleGapH] = qMax(2.0, v.v[SuneerSpacing::ModuleGapH] - 2.0);
		UndoTransaction tx = um->beginTransaction(Um::Selection, Um::IGroup, QStringLiteral("Adjust gaps (test page)"), QString(), Um::IGroup);
		const GapResult r = applyGaps(storyTextsOnPage(), v, true, true);
		tx.commit();
		QStringList pairs;
		const int ov = overlapCount(m_doc, pageIndex, &pairs);
		// stacked story pairs in the same columns: the vertical gap between them
		QList<QRectF> rects;
		for (PageItem* t : storyTextsOnPage())
			rects << itemsRect(storyItems(attr(t, QStringLiteral("news.story"))));
		double gmin = 1e9, gmax = -1e9;
		int stacked = 0;
		for (const QRectF& a : rects)
		{
			double best = 1e9;
			for (const QRectF& b : rects)
				if (&a != &b && overlapsH(a, b) && b.top() >= a.bottom() - kTol)
					best = qMin(best, b.top() - a.bottom());
			if (best < 1e8)
			{
				++stacked;
				gmin = qMin(gmin, best);
				gmax = qMax(gmax, best);
			}
		}
		check(ov <= overlap0, QStringLiteral("page: %1 stories, %2 moved, horizontal gap %3 mm, vertical 8 mm; overlaps %4 (before %5); %6 stacked pairs, gap %7..%8 mm; overflow %9; pasteboard %10")
		      .arg(r.stories).arg(r.moved).arg(v.v[SuneerSpacing::ModuleGapH], 0, 'f', 1).arg(ov).arg(overlap0).arg(stacked)
		      .arg(stacked ? gmin / kMm : 0.0, 0, 'f', 2).arg(stacked ? gmax / kMm : 0.0, 0, 'f', 2).arg(r.overflow.size()).arg(r.parked.size()));
		if (ov > overlap0)
			ts << "    new overlaps: " << pairs.join(QStringLiteral("; ")) << "\n";
		um->undo(1);
		const QStringList sig1 = signature();
		if (sig1 != sig0)
			diffLines(sig0, sig1);
		check(sig1 == sig0, QStringLiteral("  undo puts the page back (%1 of %2 entries differ)").arg(SuneerLayout::signatureDiff(sig0, sig1)).arg(sig0.size()));
	}

	// D. the dialog's live preview: undo off, then the page back from the snapshot (one story, then the page)
	for (int mode = 0; mode < 2 && !texts.isEmpty(); ++mode)
	{
		GapSnapshot snap;
		QList<PageItem*> everything = SuneerLayout::pageTopItems(m_doc, pageIndex);
		for (PageItem* t : storyTextsOnPage())
			everything << storyItems(attr(t, QStringLiteral("news.story")));
		snap.take(m_doc, everything);
		SuneerSpacing::Values v = storyGaps(texts.first());
		v.v[SuneerSpacing::HeadlineBody] += 5.0;
		v.v[SuneerSpacing::CaptionGap] = 2.0;
		v.v[SuneerSpacing::ModuleGapV] = 7.0;
		GapResult r;
		{
			UndoBlocker blocker;
			r = applyGaps(mode == 0 ? QList<PageItem*>{ texts.first() } : storyTextsOnPage(), v, mode == 1, true);
		}
		const QStringList mid = signature();
		snap.restore(m_doc);
		const QStringList sig1 = signature();
		if (sig1 != sig0)
			diffLines(sig0, sig1);
		check(mid != sig0 && sig1 == sig0, QStringLiteral("preview %1 (%2 stories re-laid, page changed: %3) taken back from the snapshot (%4 of %5 entries differ)")
		      .arg(mode == 0 ? QStringLiteral("one story") : QStringLiteral("whole page")).arg(r.stories).arg(mid != sig0 ? "yes" : "NO")
		      .arg(SuneerLayout::signatureDiff(sig0, sig1)).arg(sig0.size()));
	}

	// C. a re-layout with the model keeps a story's own gaps
	if (!texts.isEmpty())
	{
		PageItem* t = texts.first();
		const QString storyId = attr(t, QStringLiteral("news.story"));
		QString modelId;
		SuneerSpacing::Values v = storyGaps(t, &modelId);
		v.v[SuneerSpacing::HeadlineBody] += 4.0;
		v.v[SuneerSpacing::ColumnGap] = 6.0;
		UndoTransaction tx = um->beginTransaction(Um::Selection, Um::IGroup, QStringLiteral("Adjust gaps (test relayout)"), QString(), Um::IGroup);
		applyGaps({ t }, v, false, true);
		tx.commit();
		const QString report = arrangePage(modelId, (int) SuneerLayout::Keep::PlacedFrames, false);
		PageItem* t2 = nullptr;
		for (PageItem* it : storyItems(storyId))
			if (attr(it, QStringLiteral("news.role")) == QLatin1String("text"))
				t2 = it;
		bool ok = false;
		QString what = QStringLiteral("story gone");
		if (t2)
		{
			int headlinePos = -1;
			SuneerLayout::storyHead(t2->itemText, names, &headlinePos);
			const QString parent = headlinePos >= 0 ? t2->itemText.paragraphStyle(headlinePos).parent() : QString();
			const double base = m_doc->paragraphStyles().contains(parent) ? m_doc->paragraphStyles().get(parent).gapAfter() : 0.0;
			const double got = headlinePos >= 0 ? t2->itemText.paragraphStyle(headlinePos).gapAfter() : 0.0;
			ok = qAbs(t2->columnGap() - 6.0 * kMm) < 0.05 && (headlinePos < 0 || qAbs(got - base - v.pt(SuneerSpacing::HeadlineBody)) < 0.05);
			what = QStringLiteral("column gap %1 mm, headline space after %2 pt (style %3 + %4)").arg(t2->columnGap() / kMm, 0, 'f', 2)
			       .arg(got, 0, 'f', 2).arg(base, 0, 'f', 2).arg(v.pt(SuneerSpacing::HeadlineBody), 0, 'f', 2);
		}
		check(ok, QStringLiteral("re-layout with \"%1\" keeps the story's own gaps: %2").arg(modelId, what));
		um->undo(1);   // the re-layout
		um->undo(1);   // the gaps
		const QStringList sig1 = signature();
		check(sig1 == sig0, QStringLiteral("  undo puts the page back (%1 of %2 entries differ)").arg(SuneerLayout::signatureDiff(sig0, sig1)).arg(sig0.size()));
	}
	ts << (allOk ? "RESULT PASS" : "RESULT FAIL") << "\n";
	out.close();
	m_doc->setModified(false);
	// from the main event loop (an exit inside a nested loop only leaves that loop)
	QTimer::singleShot(0, qApp, [allOk]() { qApp->exit(allOk ? 0 : 1); });
}
