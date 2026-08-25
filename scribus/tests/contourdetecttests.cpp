/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <cmath>

#include "contourdetecttests.h"
#include "util_contour.h"

namespace
{
	//! An 8-bit mask, 255 = visible (solid), 0 = erased.
	QImage makeMask(int w, int h, int fill = 0)
	{
		QImage m(w, h, QImage::Format_Grayscale8);
		m.fill(fill);
		return m;
	}

	void fillRect(QImage& m, int x0, int y0, int w, int h, int value)
	{
		for (int y = y0; y < y0 + h; ++y)
		{
			uchar* line = m.scanLine(y);
			for (int x = x0; x < x0 + w; ++x)
				line[x] = uchar(value);
		}
	}

	void fillDisc(QImage& m, double cx, double cy, double r, int value)
	{
		for (int y = 0; y < m.height(); ++y)
		{
			uchar* line = m.scanLine(y);
			for (int x = 0; x < m.width(); ++x)
			{
				double dx = x + 0.5 - cx;
				double dy = y + 0.5 - cy;
				if (dx * dx + dy * dy <= r * r)
					line[x] = uchar(value);
			}
		}
	}

	/*! Largest distance from the circle to the ring's *boundary*, sampled
	    along each edge.

	    Measuring the vertices alone would be useless: simplification only ever
	    keeps points that were already on the traced boundary, so a vertex-only
	    metric reads about half a pixel however coarse the polygon gets. What
	    the tolerance actually bounds is how far the chords sag away from the
	    shape. */
	double maxChordError(const QPolygonF& ring, double cx, double cy, double r)
	{
		double worst = 0.0;
		const int n = ring.size();
		for (int i = 0; i < n; ++i)
		{
			const QPointF& a = ring[i];
			const QPointF& b = ring[(i + 1) % n];
			const int steps = 32;
			for (int s = 0; s <= steps; ++s)
			{
				double t = double(s) / steps;
				double x = a.x() + (b.x() - a.x()) * t;
				double y = a.y() + (b.y() - a.y()) * t;
				worst = qMax(worst, std::fabs(std::hypot(x - cx, y - cy) - r));
			}
		}
		return worst;
	}
}

void ContourDetectTests::testSquareGivesFourNodes()
{
	QImage m = makeMask(40, 40);
	fillRect(m, 10, 10, 20, 20, 255);

	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 2.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	QCOMPARE(rings[0].points.size(), 4);

	// Boundary follows pixel edges, so the ring is the outer corners of the
	// filled cells: 10..30, not 10..29.
	QRectF bounds = rings[0].points.boundingRect();
	QCOMPARE(bounds, QRectF(10, 10, 20, 20));
}

void ContourDetectTests::testLShapeGivesSixNodes()
{
	QImage m = makeMask(40, 40);
	fillRect(m, 5, 5, 10, 25, 255);    // vertical arm
	fillRect(m, 5, 20, 25, 10, 255);   // horizontal arm

	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 2.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	QCOMPARE(rings[0].points.size(), 6);
}

void ContourDetectTests::testCircleApproximation()
{
	const double cx = 60.0, cy = 60.0, r = 40.0;
	QImage m = makeMask(120, 120);
	fillDisc(m, cx, cy, r, 255);

	const double tolerance = 2.0;
	QList<ScContour::Ring> rings = ScContour::detect(m, 128, tolerance, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);

	const QPolygonF& ring = rings[0].points;
	// The property that matters is that the polygon still is a circle to
	// within the tolerance, plus the half-pixel the rasterised boundary itself
	// contributes. The node count follows from that, it is not the spec.
	// The traced boundary is a pixel staircase, which is itself up to about a
	// pixel off the ideal circle, so allow that on top of the tolerance.
	double err = maxChordError(ring, cx, cy, r);
	QVERIFY2(err <= tolerance + 1.5,
	         qPrintable(QString("chord error %1 for tolerance %2").arg(err).arg(tolerance)));

	// For an RDP tolerance e on radius r the chord count is about
	// pi*sqrt(r/(2e)); allow a generous band around it.
	double expected = M_PI * std::sqrt(r / (2.0 * tolerance));
	QVERIFY2(ring.size() > expected * 0.4 && ring.size() < expected * 4.0,
	         qPrintable(QString("%1 nodes, expected around %2").arg(ring.size()).arg(expected)));
}

void ContourDetectTests::testToleranceBoundsChordError()
{
	const double cx = 60.0, cy = 60.0, r = 40.0;
	QImage m = makeMask(120, 120);
	fillDisc(m, cx, cy, r, 255);

	for (double tolerance : { 1.0, 2.0, 4.0, 8.0 })
	{
		QList<ScContour::Ring> rings = ScContour::detect(m, 128, tolerance, ScContour::Mode::LargestOnly);
		QCOMPARE(rings.size(), 1);
		double err = maxChordError(rings[0].points, cx, cy, r);
		QVERIFY2(err <= tolerance + 1.5,
		         qPrintable(QString("tolerance %1 gave chord error %2 with %3 nodes")
		                    .arg(tolerance).arg(err).arg(rings[0].points.size())));
	}
}

void ContourDetectTests::testToleranceControlsNodeCount()
{
	QImage m = makeMask(120, 120);
	fillDisc(m, 60, 60, 40, 255);

	QList<ScContour::Ring> coarse = ScContour::detect(m, 128, 8.0, ScContour::Mode::LargestOnly);
	QList<ScContour::Ring> fine   = ScContour::detect(m, 128, 0.5, ScContour::Mode::LargestOnly);
	QCOMPARE(coarse.size(), 1);
	QCOMPARE(fine.size(), 1);
	QVERIFY2(coarse[0].points.size() < fine[0].points.size(),
	         qPrintable(QString("coarse %1 vs fine %2")
	                    .arg(coarse[0].points.size()).arg(fine[0].points.size())));
}

void ContourDetectTests::testShapeWithHole()
{
	QImage m = makeMask(60, 60);
	fillRect(m, 10, 10, 40, 40, 255);   // solid block
	fillRect(m, 25, 25, 10, 10, 0);     // erased hole in the middle

	QList<ScContour::Ring> largest = ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly);
	QCOMPARE(largest.size(), 1);
	QVERIFY(!largest[0].isHole());

	QList<ScContour::Ring> withHoles = ScContour::detect(m, 128, 1.0, ScContour::Mode::IncludeHoles);
	QCOMPARE(withHoles.size(), 2);

	int outers = 0, holes = 0;
	for (const ScContour::Ring& r : withHoles)
		r.isHole() ? ++holes : ++outers;
	QCOMPARE(outers, 1);
	QCOMPARE(holes, 1);

	// Winding is what tells the two apart, and it must be opposite.
	for (const ScContour::Ring& r : withHoles)
	{
		QCOMPARE(r.points.size(), 4);
		if (r.isHole())
			QCOMPARE(r.points.boundingRect(), QRectF(25, 25, 10, 10));
		else
			QCOMPARE(r.points.boundingRect(), QRectF(10, 10, 40, 40));
	}
}

void ContourDetectTests::testDiagonalRegionsStaySeparate()
{
	// Two cells touching only at a corner: the saddle tie-break must keep them
	// as two rings rather than tracing one figure eight.
	QImage m = makeMask(10, 10);
	fillRect(m, 2, 2, 1, 1, 255);
	fillRect(m, 3, 3, 1, 1, 255);

	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 0.0, ScContour::Mode::AllRegions);
	QCOMPARE(rings.size(), 2);
	for (const ScContour::Ring& r : rings)
	{
		QCOMPARE(r.points.size(), 4);
		QVERIFY(!r.isHole());
		QCOMPARE(std::fabs(r.signedArea), 1.0);
	}
}

void ContourDetectTests::testModeSelection()
{
	QImage m = makeMask(80, 40);
	fillRect(m, 5, 5, 30, 30, 255);    // big region, area 900
	fillRect(m, 50, 15, 10, 10, 255);  // small region, area 100

	QCOMPARE(ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly).size(), 1);
	QCOMPARE(ScContour::detect(m, 128, 1.0, ScContour::Mode::AllRegions).size(), 2);

	QList<ScContour::Ring> largest = ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly);
	QCOMPARE(largest[0].points.boundingRect(), QRectF(5, 5, 30, 30));
}

void ContourDetectTests::testEmptyMask()
{
	QImage m = makeMask(30, 30, 0);   // everything erased
	QVERIFY(ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly).isEmpty());
	QVERIFY(ScContour::detect(m, 128, 1.0, ScContour::Mode::AllRegions).isEmpty());
	QVERIFY(ScContour::detect(QImage(), 128, 1.0, ScContour::Mode::LargestOnly).isEmpty());
}

void ContourDetectTests::testFullyOpaqueMask()
{
	QImage m = makeMask(25, 17, 255);   // nothing erased
	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	QCOMPARE(rings[0].points.size(), 4);
	QCOMPARE(rings[0].points.boundingRect(), QRectF(0, 0, 25, 17));
}

void ContourDetectTests::testThresholdIsHonoured()
{
	QImage m = makeMask(20, 20, 0);
	fillRect(m, 5, 5, 10, 10, 100);   // partially erased block

	// Below the threshold the block is not solid at all.
	QVERIFY(ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly).isEmpty());

	QList<ScContour::Ring> rings = ScContour::detect(m, 50, 1.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	QCOMPARE(rings[0].points.boundingRect(), QRectF(5, 5, 10, 10));
}

void ContourDetectTests::testRingsAreClosedAndNonRepeating()
{
	QImage m = makeMask(60, 60);
	fillDisc(m, 30, 30, 20, 255);

	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 1.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	const QPolygonF& ring = rings[0].points;

	QVERIFY(ring.size() >= 3);
	// Closure is implicit: the first and last point must not be duplicated.
	QVERIFY(ring.first() != ring.last());
	// And no point may repeat, which a figure eight or a doubled-back trace
	// would produce.
	for (int i = 0; i < ring.size(); ++i)
	{
		for (int j = i + 1; j < ring.size(); ++j)
			QVERIFY(ring[i] != ring[j]);
	}
}

void ContourDetectTests::testSignedAreaWinding()
{
	QPolygonF square;
	square << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);
	QCOMPARE(ScContour::signedArea(square), 100.0);

	QPolygonF reversed;
	for (int i = square.size() - 1; i >= 0; --i)
		reversed << square[i];
	QCOMPARE(ScContour::signedArea(reversed), -100.0);

	// A traced solid region must match the positive convention.
	QImage m = makeMask(20, 20);
	fillRect(m, 4, 4, 8, 8, 255);
	QList<ScContour::Ring> rings = ScContour::detect(m, 128, 0.0, ScContour::Mode::LargestOnly);
	QCOMPARE(rings.size(), 1);
	QCOMPARE(rings[0].signedArea, 64.0);
}

QTEST_APPLESS_MAIN(ContourDetectTests)
