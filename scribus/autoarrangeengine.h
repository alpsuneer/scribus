/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOARRANGEENGINE_H
#define AUTOARRANGEENGINE_H

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "scribusapi.h"

class ScribusDoc;
class PageItem;

/*!
 \brief "Auto Arrange Frames" — selection-scoped vertical compaction.

 This replaces an earlier engine that re-laid out whole pages by assigning every
 frame to a guide column and re-stacking each column from the top. That approach
 destroyed a production broadsheet: frames overlapped, an advertisement was
 relocated to mid-page, and the lower half emptied.

 The rule implemented here was derived by diffing an operator's messy page
 against the same page tidied by hand (see NOTES.md):

   * Frames are NEVER resized. In the sample not one of 78 frames changed width
     or height — the frames are already exact column multiples, so translation
     alone is enough. That also means no text reflow and no linked-chain risk.
   * Horizontal position is NEVER changed. The sideways moves in the sample were
     editorial judgement, confirmed as such by the operator.
   * Only frames the user SELECTED are considered. Whole-page arrangement is
     deferred: a global gap-closing rule moved 9 of 11 blocks where the operator
     moved 3, because the hand-tidy was mostly editorial placement rather than
     mechanical compaction. Do not fit a heuristic to a single page.
 */

/*! One frame's planned relocation. */
struct SCRIBUS_API ArrangeMove
{
	PageItem* item { nullptr };
	QPointF   from;
	QPointF   to;
	int       block { 0 };     //!< index of the block this frame belongs to
};

/*! A block: frames that must travel together, and its bounding box. */
struct SCRIBUS_API ArrangeBlock
{
	QList<PageItem*> members;
	QRectF box;
};

/*!
 \brief A computed plan. Producing one never modifies the document.
 */
struct SCRIBUS_API ArrangePlan
{
	QList<ArrangeMove>  moves;
	QList<ArrangeBlock> blocks;
	int     framesConsidered { 0 };
	int     framesSkipped { 0 };     //!< master page / locked / locked layer
	double  gutter { 12.0 };
	QString note;                    //!< why the plan is empty, when it is

	bool isEmpty() const { return moves.isEmpty(); }
};

class SCRIBUS_API AutoArrangeEngine
{
public:
	//! Default vertical gutter between stacked blocks, in points.
	static constexpr double DefaultGutter = 12.0;

	/*! Group the selected frames into blocks and compute their compaction.
	    Read-only: the document is untouched. */
	static ArrangePlan planForSelection(ScribusDoc* doc, double gutter = DefaultGutter);

	/*! Apply a previously computed plan as ONE undo step named "Auto Arrange
	    Frames". Returns the number of frames actually moved. */
	static int applyPlan(ScribusDoc* doc, const ArrangePlan& plan);

	/*! True when \a item may be moved (not master-page, locked, or on a locked
	    or hidden layer). */
	static bool canArrangeFrame(ScribusDoc* doc, PageItem* item);

	/*! Group \a frames into blocks by containment plus matching-width rules.
	    Exposed for testing. */
	static QList<ArrangeBlock> findBlocks(const QList<PageItem*>& frames);
};

#endif // AUTOARRANGEENGINE_H
