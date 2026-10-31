/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERPHOTOBADGES_H
#define SUNEERPHOTOBADGES_H

#include <QPointF>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

class Canvas;
class QPainter;
class ScribusDoc;
class ScribusView;

/*! The marks on image frames: the orange LINK / red MISSING badge, the obituary
    check flag ("low res: 187 ppi (548x548 px)", "check BG", "no face") and the
    blue "M" of a manual background mask.

    All of them are a canvas overlay, painted on the widget over the page
    buffer, so they never reach print, PDF, export or page thumbnails. Their size
    follows the photo on screen: at most the configured badge size and about a
    fifth of the photo, with shorter labels as the photo gets smaller, and only a
    small coloured dot (or nothing, Preferences > Item Tools) below about 40 px
    or 35 % zoom. A selected photo shows the full labels; the tooltip over a
    photo gives the full details. View > Image Frames > Show Photo Badges
    (SuneerImageLinks::badgesShown) hides them all. */
namespace SuneerPhotoBadges
{
	//! What a photo too small for a label shows.
	enum SmallMode { SmallDot = 0, SmallNothing = 1 };
	SCRIBUS_API int smallMode();
	SCRIBUS_API void setSmallMode(int mode);

	//! Paints the badges of the visible image frames (Canvas::paintEvent).
	SCRIBUS_API void paint(QPainter* painter, const Canvas* canvas, ScribusDoc* doc);
	//! The full details of the badged photo under \a widgetPos, empty when none.
	SCRIBUS_API QString toolTipAt(const Canvas* canvas, ScribusDoc* doc, const QPointF& widgetPos);
	//! One line per badged frame at the current zoom: name, size on screen, what is drawn. For the self-test.
	SCRIBUS_API QStringList describe(const Canvas* canvas, ScribusDoc* doc);

	/*! SUNEER_BADGE_SELFTEST=<png prefix>: canvas pictures at 25 / 50 / 100 / 200 %,
	    one with a photo selected, one with the badges off, the dot and nothing
	    modes, and <prefix>-report.txt; then quits unless SUNEER_BADGE_SELFTEST_HOLD. */
	SCRIBUS_API void runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc);
}

#endif
