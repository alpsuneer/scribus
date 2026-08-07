/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "autoarrangeengine.h"

#include <QMap>
#include <QObject>
#include <QVector>
#include <algorithm>
#include <functional>

#include "pageitem.h"
#include "sclayer.h"
#include "scribusdoc.h"
#include "selection.h"
#include "undomanager.h"
#include "undotransaction.h"

namespace
{
	// Frames this thin are treated as separator rules rather than content.
	const double RuleHeight = 5.0;
	// Slack when testing containment, so a frame nudged a hair outside its
	// parent still counts as inside it.
	const double ContainPad = 2.0;
	// A rule is only adopted by a block directly beneath it and no further.
	const double RuleGap = 12.0;
	// ...and only when their widths agree this closely. In the sample a story's
	// own rule matched its block exactly (456.1 vs 456.1) while a page-spanning
	// divider 15% wider belonged to something larger and correctly stayed put.
	const double RuleWidthTolerance = 0.05;

	QRectF boxOf(PageItem* f)
	{
		return QRectF(f->xPos(), f->yPos(), f->width(), f->height());
	}

	bool contains(const QRectF& outer, const QRectF& inner)
	{
		return inner.left()   >= outer.left()   - ContainPad
		    && inner.top()    >= outer.top()    - ContainPad
		    && inner.right()  <= outer.right()  + ContainPad
		    && inner.bottom() <= outer.bottom() + ContainPad;
	}

	bool overlapsHorizontally(const QRectF& a, const QRectF& b)
	{
		return !(a.right() < b.left() + 2.0 || a.left() > b.right() - 2.0);
	}
}

bool AutoArrangeEngine::canArrangeFrame(ScribusDoc* doc, PageItem* item)
{
	if (!item)
		return false;
	// Master-page furniture (border, masthead, folio) must never move.
	if (!item->OnMasterPage.isEmpty())
		return false;
	if (item->locked())
		return false;
	if (doc)
	{
		const ScLayer* layer = doc->Layers.layerByID(item->m_layerID);
		if (layer && (!layer->isEditable || !layer->isViewable))
			return false;
	}
	// Group children move with their group control, which is what gets selected.
	if (item->isGroupChild())
		return false;
	return true;
}

QList<ArrangeBlock> AutoArrangeEngine::findBlocks(const QList<PageItem*>& frames)
{
	QList<PageItem*> solid, rules;
	for (PageItem* f : frames)
	{
		if (f->width() <= 3.0)
			continue;
		if (f->height() <= RuleHeight)
			rules.append(f);
		else
			solid.append(f);
	}

	// Union-find over CONTAINMENT, not overlap. Plain overlap chains
	// transitively — A touches B, B touches C — and fuses most of a dense
	// newspaper page into one block; containment forms a tree and cannot.
	QVector<int> parent(solid.size());
	for (int i = 0; i < solid.size(); ++i)
		parent[i] = i;
	std::function<int(int)> find = [&](int i) {
		while (parent[i] != i)
		{
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	};
	auto unite = [&](int i, int j) {
		const int a = find(i), b = find(j);
		if (a != b)
			parent[b] = a;
	};

	for (int i = 0; i < solid.size(); ++i)
		for (int j = 0; j < solid.size(); ++j)
			if (i != j && contains(boxOf(solid[i]), boxOf(solid[j])))
				unite(i, j);

	QMap<int, QList<PageItem*>> buckets;
	for (int i = 0; i < solid.size(); ++i)
		buckets[find(i)].append(solid[i]);

	QList<ArrangeBlock> blocks;
	for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it)
	{
		ArrangeBlock b;
		b.members = it.value();
		QRectF box = boxOf(b.members.first());
		for (PageItem* f : b.members)
			box = box.united(boxOf(f));
		b.box = box;
		blocks.append(b);
	}

	// Adopt each separator rule into the block directly beneath it. A rule left
	// behind while its story moves is an obvious visual error.
	for (PageItem* r : rules)
	{
		int best = -1;
		double bestGap = RuleGap;
		for (int i = 0; i < blocks.size(); ++i)
		{
			const double bw = blocks[i].box.width();
			if (bw <= 0.0 || qAbs(r->width() - bw) / bw > RuleWidthTolerance)
				continue;
			const double gap = blocks[i].box.top() - (r->yPos() + r->height());
			if (gap >= 0.0 && gap < bestGap)
			{
				best = i;
				bestGap = gap;
			}
		}
		if (best >= 0)
		{
			blocks[best].members.append(r);
			blocks[best].box = blocks[best].box.united(boxOf(r));
		}
	}

	std::sort(blocks.begin(), blocks.end(), [](const ArrangeBlock& a, const ArrangeBlock& b) {
		if (qAbs(a.box.top() - b.box.top()) > 0.5)
			return a.box.top() < b.box.top();
		return a.box.left() < b.box.left();
	});
	return blocks;
}

ArrangePlan AutoArrangeEngine::planForSelection(ScribusDoc* doc, double gutter)
{
	ArrangePlan plan;
	plan.gutter = gutter;
	if (!doc || !doc->m_Selection)
	{
		plan.note = QObject::tr("No document.");
		return plan;
	}

	QList<PageItem*> frames;
	const int count = doc->m_Selection->count();
	for (int i = 0; i < count; ++i)
	{
		PageItem* it = doc->m_Selection->itemAt(i);
		if (!it)
			continue;
		++plan.framesConsidered;
		if (!canArrangeFrame(doc, it))
		{
			++plan.framesSkipped;
			continue;
		}
		frames.append(it);
	}

	if (frames.isEmpty())
	{
		plan.note = (plan.framesConsidered == 0)
			? QObject::tr("Nothing is selected. Select the frames you want tidied.")
			: QObject::tr("Every selected frame is protected (master page, locked, "
			              "or on a locked layer), so there is nothing to move.");
		return plan;
	}

	plan.blocks = findBlocks(frames);
	if (plan.blocks.size() < 2)
	{
		plan.note = QObject::tr("The selection forms a single block, so there is no "
		                        "gap between blocks to close. Select at least two "
		                        "stories to stack.");
		return plan;
	}

	// Compact: each block sits `gutter` below the lowest already-placed block
	// that overlaps it horizontally. This closes voids and also pushes a block
	// down when it started out overlapping the one above. The topmost block in
	// each track is never moved, so nothing is yanked to the top margin.
	QList<QRectF> placed;
	for (int i = 0; i < plan.blocks.size(); ++i)
	{
		QRectF box = plan.blocks[i].box;
		double lowest = 0.0;
		bool haveAbove = false;
		for (const QRectF& p : placed)
		{
			if (!overlapsHorizontally(p, box))
				continue;
			if (!haveAbove || p.bottom() > lowest)
			{
				lowest = p.bottom();
				haveAbove = true;
			}
		}
		if (haveAbove)
		{
			const double dy = (lowest + gutter) - box.top();
			if (qAbs(dy) > 0.5)
			{
				for (PageItem* f : plan.blocks[i].members)
				{
					ArrangeMove mv;
					mv.item  = f;
					mv.from  = QPointF(f->xPos(), f->yPos());
					mv.to    = QPointF(f->xPos(), f->yPos() + dy);
					mv.block = i;
					plan.moves.append(mv);
				}
				box.moveTop(box.top() + dy);
				plan.blocks[i].box = box;
			}
		}
		placed.append(box);
	}

	if (plan.moves.isEmpty())
		plan.note = QObject::tr("The selected blocks are already stacked with a "
		                        "%1 pt gutter — nothing to do.").arg(gutter);
	return plan;
}

int AutoArrangeEngine::applyPlan(ScribusDoc* doc, const ArrangePlan& plan)
{
	if (!doc || plan.moves.isEmpty())
		return 0;

	UndoTransaction trans;
	if (UndoManager::undoEnabled())
		trans = UndoManager::instance()->beginTransaction(
			Um::Selection, Um::IGroup, QObject::tr("Auto Arrange Frames"), QString(), Um::IMove);

	int moved = 0;
	for (const ArrangeMove& mv : plan.moves)
	{
		if (!mv.item)
			continue;
		// Only Y changes. X is deliberately never written: horizontal motion is
		// what made the previous engine destructive.
		mv.item->setYPos(mv.to.y());
		mv.item->invalid = true;
		++moved;
	}

	if (trans)
		trans.commit();

	doc->changed();
	doc->regionsChanged()->update(QRectF());
	return moved;
}
