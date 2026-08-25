/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef ERASERMASKTESTS_H
#define ERASERMASKTESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for the non-destructive image eraser mask (ScEraserMask).
 *
 * The stroke tests are regressions: both scalloping bugs they pin were real,
 * and both were only visible on a soft brush dragged across a real canvas.
 */
class EraserMaskTests : public QObject
{
	Q_OBJECT
public:
	EraserMaskTests() {}

private slots:
	void testCreateForCapsLongEdge();
	void testEmptyMaskIsNotStored();
	void testHardBrush();
	void testSoftBrushProfile();
	void testRestoreIsTheInverse();
	void testStrokeIndependentOfEventSplit();
	void testStrokeEdgeHasNoScallops();
	void testCodecRoundTrip();
	void testCodecRejectsBadInput();
	void testDigestIsShort();
	void testEffectListPlumbing();
	void testMergeIntoAlphaBytes();
	void testMergeIntoPdfImageMask();
	void testApplyToAlpha();
};

#endif
