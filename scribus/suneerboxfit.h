/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERBOXFIT_H
#define SUNEERBOXFIT_H

#include <QList>
#include <QRectF>
#include <QString>

#include "scribusapi.h"

class PageItem;
class PageItem_TextFrame;
class ScribusDoc;
class ScribusView;
struct SuneerBorderSettings;

/*!
 \brief A box (top toolbar "Frame Border") that looks even on all four sides.

 One frame: the box is the frame's own border. The frame gets the same
 inner space on every side, the first line sits "real glyph height" below
 the top inset (the space above the letters is the inset), trailing empty
 paragraphs are removed (SuneerTextClean::cleanStory) and the height is set
 from the INK of the last line: last letter bottom + inner space. The bottom
 text distance is lowered by what the font's descent hangs below the ink, so
 the last line still fits (otherwise the empty descent showed as extra space).

 A story of several frames (welded, grouped or linked): one separate box
 item ("SuneerStoryBox") around the visible content of all of them (text
 ink, other frames' outlines) plus the inner space. No frame is resized, so
 nothing else moves. The box is welded to the story (or put into its group)
 so it moves with it.

 "Fit box to text": after editing the text of such a frame (leaving text
 edit mode) the frame / the story box is fitted again, as one undo step.
 */
namespace SuneerBoxFit
{
	//! Frame-local rectangle of the ink of all laid-out lines (empty when there is none).
	SCRIBUS_API QRectF inkRect(PageItem_TextFrame* tf);

	//! The frames of \a item's story: its group (top level), its welded cluster and the
	//! linked frames of the text frames in it. Story box items are left out.
	SCRIBUS_API QList<PageItem*> storyMembers(PageItem* item);

	//! Fits \a tf as described above (records undo; call inside a transaction).
	//! False for a linked frame or one without text.
	SCRIBUS_API bool fitFrame(PageItem_TextFrame* tf, double inset);

	//! Page rectangle of the visible content of \a members (document coordinates).
	SCRIBUS_API QRectF storyContentRect(const QList<PageItem*>& members);

	//! The story box of \a members (nullptr when none).
	SCRIBUS_API PageItem* storyBoxOf(ScribusDoc* doc, const QList<PageItem*>& members);

	//! The Frame Border popup's Apply on \a items: one frame -> its own border (+ fit), a story of
	//! several frames with \a s.wholeStory -> one story box. Records undo (call inside a transaction).
	SCRIBUS_API void apply(ScribusDoc* doc, const QList<PageItem*>& items, const SuneerBorderSettings& s);

	//! Fits the story box \a box to its members again (records undo).
	SCRIBUS_API bool refitStoryBox(ScribusDoc* doc, PageItem* box);

	//! Text edit mode left on \a item: when it is a fitted frame or in a story with a fitted box,
	//! the fit runs right afterwards as its own undo step "Fit box to text".
	SCRIBUS_API void noteTextEdit(PageItem* item);

	//! SUNEER_BOXFIT_SELFTEST=<prefix> (tests/suneer/box_fit.sh).
	SCRIBUS_API void runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc);
}

#endif
