/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERFITGAP_H
#define SUNEERFITGAP_H

#include "scribusapi.h"

class ScribusDoc;
class ScribusView;

/*!
 \brief Suneer: test of every "fit the frame to its text" path (tests/suneer/fit_gap.sh).

 SUNEER_FITGAP_SELFTEST=<prefix>: on a document made by tests/suneer/fixtures/fit_gap_make.py, the frames
 of pages 2N-1, 2N are fitted by path N (menu Adjust Frame Height to Text, toolbar Fit, News Browser placement,
 Fit Caption Frame, Frame Border with inner space); the frame rectangles are written for the script that
 measures the space above and below the ink in the exported PDF.
 */
namespace SuneerFitGap
{
	SCRIBUS_API void runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc);
}

#endif
