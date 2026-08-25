/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CONTOURDETECTTESTS_H
#define CONTOURDETECTTESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for boundary tracing of eraser masks (ScContour).
 */
class ContourDetectTests : public QObject
{
	Q_OBJECT
public:
	ContourDetectTests() {}

private slots:
	void testSquareGivesFourNodes();
	void testLShapeGivesSixNodes();
	void testCircleApproximation();
	void testToleranceBoundsChordError();
	void testToleranceControlsNodeCount();
	void testShapeWithHole();
	void testDiagonalRegionsStaySeparate();
	void testModeSelection();
	void testEmptyMask();
	void testFullyOpaqueMask();
	void testThresholdIsHonoured();
	void testRingsAreClosedAndNonRepeating();
	void testSignedAreaWinding();
};

#endif
