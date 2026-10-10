/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneercursor.h"

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>

#include <cstdio>

#include "canvas.h"
#include "scribusdoc.h"
#include "scribusview.h"

static int s_guardDepth = 0;

static bool debugOn()
{
	static const bool on = !qEnvironmentVariableIsEmpty("SCRIBUS_DEBUG_CURSOR");
	return on;
}

SuneerCursorGuard::SuneerCursorGuard(Qt::CursorShape shape)
{
	QApplication::setOverrideCursor(QCursor(shape));
	++s_guardDepth;
	m_active = true;
}

SuneerCursorGuard::~SuneerCursorGuard()
{
	release();
}

void SuneerCursorGuard::release()
{
	if (!m_active)
		return;
	m_active = false;
	--s_guardDepth;
	QApplication::restoreOverrideCursor();
}

int SuneerCursorGuard::depth()
{
	return s_guardDepth;
}

void suneerCursorDebug(const char* where, ScribusView* view)
{
	if (!debugOn())
		return;
	const QCursor* ov = QApplication::overrideCursor();
	QWidget* canvas = view ? view->widget() : nullptr;
	const int mode = (view && view->m_doc) ? view->m_doc->appMode : -1;
	fprintf(stderr, "[CURSOR] %s: override=%s guards=%d appMode=%d canvasOwnCursor=%s(%d) viewCursor=%d\n",
	        where, ov ? "YES" : "no", s_guardDepth, mode,
	        canvas && canvas->testAttribute(Qt::WA_SetCursor) ? "YES" : "no",
	        canvas ? int(canvas->cursor().shape()) : -1,
	        view ? int(view->cursor().shape()) : -1);
	fflush(stderr);
}

void suneerCursorSettle(ScribusView* view, const char* where)
{
	// 1. An override cursor left behind while nobody holds a guard is stuck:
	//    pop it (all of them), and say so in debug mode.
	if (s_guardDepth == 0)
	{
		int popped = 0;
		while (QApplication::overrideCursor() && popped < 16)
		{
			QApplication::restoreOverrideCursor();
			++popped;
		}
		if (popped && debugOn())
			fprintf(stderr, "[CURSOR] %s: cleared %d stuck override cursor(s)\n", where, popped);
	}
	if (!view)
		return;
	// 2. A cursor set on the Canvas widget itself overrides the view's
	//    cursor (the one every canvas mode sets) for as long as it exists.
	QWidget* canvas = view->widget();
	if (canvas && canvas->testAttribute(Qt::WA_SetCursor))
	{
		canvas->unsetCursor();
		if (debugOn())
			fprintf(stderr, "[CURSOR] %s: removed the canvas widget's own cursor\n", where);
	}
	// 3. Re-run the hover logic of the current mode for where the mouse is,
	//    so the resize / move cursor shows at once instead of after the
	//    next real mouse move.
	if (canvas && canvas->isVisible())
	{
		const QPoint global = QCursor::pos();
		const QPoint local = canvas->mapFromGlobal(global);
		if (canvas->rect().contains(local))
		{
			QMouseEvent ev(QEvent::MouseMove, QPointF(local), QPointF(local), QPointF(global), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(canvas, &ev);
		}
		else
			view->setCursor(QCursor(Qt::ArrowCursor));
	}
	suneerCursorDebug(where, view);
}
