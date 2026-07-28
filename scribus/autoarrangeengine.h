/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOARRANGEENGINE_H
#define AUTOARRANGEENGINE_H

#include <QList>
#include <QRectF>

#include "scribusapi.h"

class ScribusDoc;
class ScPage;
class PageItem;

/*!
 \brief Parameters for a one-click "Auto Arrange Frames" operation.

 The layout is column-preserving: every content frame stays in the column its
 centre falls into (defined by the page's vertical guides) and is re-stacked
 vertically with zero gap. Master-page items, locked frames, and frames on
 locked/hidden layers are never touched.
 */
struct SCRIBUS_API ArrangeOptions
{
	bool preserveColumn { true };      //!< keep each frame in its own column (by centre X)
	bool zeroGap { true };             //!< stack frames with no vertical gap
	bool dryRun { false };             //!< log the plan to the console, modify nothing

	//! How each frame's height is set when stacking.
	enum HeightMode
	{
		KeepOriginal,   //!< preserve each frame's original height
		FillColumn,     //!< distribute the column height among its frames (default)
		FitToText       //!< shrink each frame to fit its own text (may leave gaps)
	};
	HeightMode heightMode { FillColumn };

	bool includeTextFrames { true };
	bool includeImageFrames { true };
	bool includeGroups { true };       //!< arrange grouped articles as whole units

	bool protectMasterPageItems { true };
	bool protectLockedFrames { true };
	bool protectLockedLayers { true };

	enum Scope
	{
		CurrentPage,
		AllPages
	};
	Scope scope { CurrentPage };
};

/*!
 \brief One newspaper column: a horizontal span plus the frames assigned to it.
 */
struct SCRIBUS_API ArrangeColumn
{
	double left { 0.0 };
	double right { 0.0 };
	double width() const { return right - left; }
	QList<PageItem*> frames;
};

/*!
 \brief Outcome of an arrange run, used to give the user feedback.
 */
struct SCRIBUS_API ArrangeResult
{
	int pagesProcessed { 0 };
	int columnsFound { 0 };
	int framesArranged { 0 };
	int framesOverflowed { 0 };
};

/*!
 \brief Stateless engine: stacks page frames within their guide-defined columns,
        never touching master-page items. The whole run is one undo transaction.
 */
class SCRIBUS_API AutoArrangeEngine
{
public:
	//! Arrange the current page or all pages per \a opts (single undo step).
	static ArrangeResult arrange(ScribusDoc* doc, const ArrangeOptions& opts);

	//! The content rectangle (page margins, tightened by horizontal guides).
	static QRectF detectContentArea(ScPage* page);
	//! Columns from the page's (and its master page's) vertical guides.
	static QList<ArrangeColumn> detectColumns(ScribusDoc* doc, ScPage* page, const QRectF& contentArea);
	//! Frames on \a page eligible to arrange (type filter + protection rules).
	static QList<PageItem*> gatherFrames(ScribusDoc* doc, ScPage* page, const ArrangeOptions& opts);
	//! True if \a item may be moved (not master/locked/locked-or-hidden-layer, right type).
	static bool canArrangeFrame(ScribusDoc* doc, PageItem* item, const ArrangeOptions& opts);

private:
	static void fitTextFrameHeight(PageItem* frame);
	static ArrangeResult arrangePage(ScribusDoc* doc, ScPage* page, const ArrangeOptions& opts);
};

#endif // AUTOARRANGEENGINE_H
