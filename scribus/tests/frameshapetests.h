/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef FRAMESHAPETESTS_H
#define FRAMESHAPETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for the frame-shape generators in util_math, plus the
 * interactive Frame Shape crop overlay's geometry in util_shapecrop
 * (CanvasMode_ShapeCrop).
 *
 * These cover the geometry the Frame Shape menu generates: regular polygons,
 * stars, the cross, and the conversion from a QPainterPath into the flat
 * percent-of-frame array that PageItem::SetFrameShape() reads; and the pure
 * maths behind the crop overlay: canvas/doc coordinate transforms, handle
 * hit-testing, and resize/rotate arithmetic.
 *
 * Rectangle, Ellipse, Triangle and Heart are deliberately absent: those come
 * from Scribus's own shape data (PageItem::SetRectFrame/SetOvalFrame and the
 * AutoformButtonGroup tables) rather than from anything written here, and
 * reaching them would mean linking Qt Widgets and IconManager into a pure
 * maths test. testConverterMatchesSetRectFrame() pins the *format* those
 * shapes share instead, which is the part a change here could break.
 */
class FrameShapeTests : public QObject
{
	Q_OBJECT
public:
	FrameShapeTests() {}

private slots:
	// The QPainterPath -> percent-array conversion
	void testConverterMatchesSetRectFrame();
	void testConverterKeepsSegmentsStraight();
	void testConverterTranslatesSubpathMarkers();

	// Regular polygons
	void testPolygonVertexCount();
	void testPolygonApexAtTop();
	void testPolygonAngularSpacingBeforeFitting();
	void testPolygonFillsFrame();
	void testPolygonIsSymmetricAboutVerticalAxis();
	void testPolygonRejectsFewerThanThreeCorners();
	void testGeneratedTriangleMatchesScribusOwn();

	// Stars
	void testStarVertexCountAlternates();
	void testStarInnerRadiusRatio();
	void testStarApexAtTop();
	void testStarFillsFrame();
	void testStarIsSymmetricAboutVerticalAxis();
	void testStarRejectsFewerThanThreePoints();

	// Cross
	void testCrossHasTwelveVertices();
	void testCrossArmWidthIsHonoured();
	void testCrossFillsFrame();
	void testCrossIsSymmetricOnBothAxes();
	void testCrossClampsOutOfRangeArms();

	// Properties every generated shape has to have
	void testEveryShapeIsQuadAligned();
	void testEveryShapeFillsAndCentresInTheFrame();
	void testEveryShapeIsScaleInvariant();

	// Shape-crop overlay: canvas <-> doc coordinate transforms
	void testShapeCropCanvasDocRoundTrip();
	void testShapeCropDocToCanvasRectScalesAndOffsets();

	// Shape-crop overlay: rotation maths
	void testShapeCropRotatePointCardinalAngles();
	void testShapeCropRotatePointRoundTrip();
	void testShapeCropAngleFromCenterMatchesRotatePoint();

	// Shape-crop overlay: handle layout and hit-testing
	void testShapeCropHandlePositionsLayout();
	void testShapeCropHitTestFindsEachHandle();
	void testShapeCropHitTestBodyAndOutside();
	void testShapeCropHitTestRespectsRotation();

	// Shape-crop overlay: resize maths
	void testShapeCropResizeEdgeIsSingleAxis();
	void testShapeCropResizeCornerFreeVsLockedAspect();
	void testShapeCropResizeAnchorsOppositeCorner();
	void testShapeCropResizeClampsToMinSize();

	// Shape-crop overlay: shape generation at arbitrary (non-frame-filling) bounds
	void testShapeCropPathFromPercentValuesFillsArbitraryBox();
	void testShapeCropPathFromPercentValuesAtNonSquareSize();
};

#endif
