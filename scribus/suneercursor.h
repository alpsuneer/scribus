/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERCURSOR_H
#define SUNEERCURSOR_H

#include <QCursor>

#include "scribusapi.h"

class ScribusView;

/*!
 \brief Override cursors that cannot get stuck, and a way to put the canvas
 cursor right after one of our actions.

 QApplication's override cursors are a stack: every setOverrideCursor() needs
 exactly one restoreOverrideCursor(), on every exit path. SuneerCursorGuard
 pushes in its constructor and pops in its destructor, so an early return, an
 error or a cancel cannot leave one behind. suneerCursorSettle() runs after
 BG / crop / resample / feather / contour work: it drops any override left
 while no guard is alive, clears a cursor set on the Canvas widget itself
 (that would hide the view's resize cursors for good), and re-runs the
 normal mode's hover logic for the mouse position.

 SCRIBUS_DEBUG_CURSOR=1 prints the state on stderr at each of these points.
 */
class SCRIBUS_API SuneerCursorGuard
{
public:
	explicit SuneerCursorGuard(Qt::CursorShape shape = Qt::WaitCursor);
	~SuneerCursorGuard();
	SuneerCursorGuard(const SuneerCursorGuard&) = delete;
	SuneerCursorGuard& operator=(const SuneerCursorGuard&) = delete;
	//! Pop early (idempotent).
	void release();
	//! Guards alive right now.
	static int depth();
private:
	bool m_active { false };
};

//! Print the cursor state (override present, guard depth, app mode, canvas
//! and view cursors) when SCRIBUS_DEBUG_CURSOR is set.
SCRIBUS_API void suneerCursorDebug(const char* where, ScribusView* view);

//! Put the cursor right after an action: see the class comment.
SCRIBUS_API void suneerCursorSettle(ScribusView* view, const char* where);

#endif
