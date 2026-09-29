/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <cmath>

#include "frameshapetests.h"
#include "util_math.h"
#include "util_shapecrop.h"

namespace
{
	constexpr double kEps = 1e-6;

	//! One entry of the flat array, read the way SetFrameShape() reads it.
	struct Quad
	{
		QPointF p0, c0, p1, c1;
		bool    marker { false };
	};

	/*! Decode a percent array into segments exactly as PageItem::SetFrameShape()
	    plus FPointArray::toQPainterPath() would: four doubles to a point pair,
	    two pairs to a cubic segment, a negative leading value for a break. */
	QList<Quad> decode(const QList<double>& vals)
	{
		QList<QPointF> pts;
		QList<int> markerAt;
		for (int i = 0; i + 3 < vals.size(); i += 4)
		{
			if (vals[i] < 0)
			{
				markerAt << pts.size();
				continue;
			}
			pts << QPointF(vals[i], vals[i + 1]) << QPointF(vals[i + 2], vals[i + 3]);
		}
		QList<Quad> quads;
		for (int i = 0; i + 3 < pts.size(); i += 4)
		{
			Quad q;
			q.p0 = pts[i];
			q.c0 = pts[i + 1];
			q.p1 = pts[i + 2];
			q.c1 = pts[i + 3];
			q.marker = markerAt.contains(i);
			quads << q;
		}
		return quads;
	}

	//! The corner points a pen actually visits, in order.
	QList<QPointF> vertices(const QList<double>& vals)
	{
		QList<QPointF> v;
		const QList<Quad> quads = decode(vals);
		for (const Quad& q : quads)
			v << q.p0;
		return v;
	}

	QRectF bounds(const QList<double>& vals)
	{
		const QList<Quad> quads = decode(vals);
		if (quads.isEmpty())
			return QRectF();
		double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
		for (const Quad& q : quads)
		{
			for (const QPointF& p : { q.p0, q.c0, q.p1, q.c1 })
			{
				minX = qMin(minX, p.x());
				maxX = qMax(maxX, p.x());
				minY = qMin(minY, p.y());
				maxY = qMax(maxY, p.y());
			}
		}
		return QRectF(minX, minY, maxX - minX, maxY - minY);
	}

	//! Every segment is a plain line: both controls sit on their own endpoint.
	bool allSegmentsStraight(const QList<double>& vals)
	{
		const QList<Quad> quads = decode(vals);
		for (const Quad& q : quads)
		{
			if (qAbs(q.c0.x() - q.p0.x()) > kEps || qAbs(q.c0.y() - q.p0.y()) > kEps)
				return false;
			if (qAbs(q.c1.x() - q.p1.x()) > kEps || qAbs(q.c1.y() - q.p1.y()) > kEps)
				return false;
		}
		return true;
	}

	//! The path is joined up: each segment starts where the last one ended.
	bool isContinuous(const QList<double>& vals)
	{
		const QList<Quad> quads = decode(vals);
		for (int i = 1; i < quads.size(); ++i)
		{
			if (quads[i].marker)
				continue;
			if (qAbs(quads[i].p0.x() - quads[i - 1].p1.x()) > kEps)
				return false;
			if (qAbs(quads[i].p0.y() - quads[i - 1].p1.y()) > kEps)
				return false;
		}
		return true;
	}

	bool closesOnItself(const QList<double>& vals)
	{
		const QList<Quad> quads = decode(vals);
		if (quads.isEmpty())
			return false;
		return qAbs(quads.last().p1.x() - quads.first().p0.x()) < kEps
		    && qAbs(quads.last().p1.y() - quads.first().p0.y()) < kEps;
	}

	bool hasVertexNear(const QList<double>& vals, double x, double y, double tol = 1e-4)
	{
		const QList<QPointF> v = vertices(vals);
		for (const QPointF& p : v)
		{
			if (qAbs(p.x() - x) < tol && qAbs(p.y() - y) < tol)
				return true;
		}
		return false;
	}

	//! Mirroring x about the frame's centre line maps the vertex set onto itself.
	bool symmetricAboutVerticalAxis(const QList<double>& vals, double tol = 1e-4)
	{
		const QList<QPointF> v = vertices(vals);
		for (const QPointF& p : v)
		{
			bool found = false;
			for (const QPointF& q : v)
			{
				if (qAbs((100.0 - p.x()) - q.x()) < tol && qAbs(p.y() - q.y()) < tol)
				{
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
		return true;
	}

	bool symmetricAboutHorizontalAxis(const QList<double>& vals, double tol = 1e-4)
	{
		const QList<QPointF> v = vertices(vals);
		for (const QPointF& p : v)
		{
			bool found = false;
			for (const QPointF& q : v)
			{
				if (qAbs(p.x() - q.x()) < tol && qAbs((100.0 - p.y()) - q.y()) < tol)
				{
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
		return true;
	}

	//! Every shape the Frame Shape menu generates rather than reuses.
	QList<QPair<QString, QList<double>>> generatedShapes()
	{
		return {
			{ "pentagon", polygonFrameShape(5) },
			{ "hexagon",  polygonFrameShape(6) },
			{ "octagon",  polygonFrameShape(8) },
			{ "star5",    starFrameShape(5, 0.4) },
			{ "star6",    starFrameShape(6, 0.4) },
			{ "cross",    crossFrameShape(0.30) }
		};
	}
}

/* ------------------------------------------------------------------ */
/* The QPainterPath -> percent-array conversion                        */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testConverterMatchesSetRectFrame()
{
	/* PageItem::SetRectFrame()'s own table, copied here as the format's
	   reference. A rectangle drawn as a closed path has to come out of the
	   converter identical to it, or every shape below is being handed to
	   SetFrameShape() in a layout it does not read the same way. */
	static const double expected[32] = {
		  0.0,   0.0,   0.0,   0.0,
		100.0,   0.0, 100.0,   0.0,
		100.0,   0.0, 100.0,   0.0,
		100.0, 100.0, 100.0, 100.0,
		100.0, 100.0, 100.0, 100.0,
		  0.0, 100.0,   0.0, 100.0,
		  0.0, 100.0,   0.0, 100.0,
		  0.0,   0.0,   0.0,   0.0 };

	QPainterPath path;
	path.moveTo(0.0, 0.0);
	path.lineTo(100.0, 0.0);
	path.lineTo(100.0, 100.0);
	path.lineTo(0.0, 100.0);
	path.closeSubpath();

	const QList<double> vals = frameShapeValuesFromPath(path);
	QCOMPARE(vals.size(), 32);
	for (int i = 0; i < 32; ++i)
		QVERIFY2(qAbs(vals[i] - expected[i]) < kEps,
		         qPrintable(QString("index %1: got %2, want %3").arg(i).arg(vals[i]).arg(expected[i])));
}

void FrameShapeTests::testConverterKeepsSegmentsStraight()
{
	/* Guards a real trap. FPointArray::svgClosePath() reuses the *previous*
	   segment's outgoing control for the closing edge, which would bow the last
	   side of every shape. It is only reached when the path arrives unclosed;
	   the generators all call closeSubpath(), so the closing edge comes through
	   as an explicit lineTo and the branch never fires. This test fails if a
	   generator ever stops closing its path. */
	const QList<QPair<QString, QList<double>>> shapes = generatedShapes();
	for (const auto& s : shapes)
	{
		QVERIFY2(allSegmentsStraight(s.second), qPrintable(s.first + " has a bowed edge"));
		QVERIFY2(isContinuous(s.second), qPrintable(s.first + " is not continuous"));
		QVERIFY2(closesOnItself(s.second), qPrintable(s.first + " does not close"));
	}
}

void FrameShapeTests::testConverterTranslatesSubpathMarkers()
{
	// Two subpaths: a frame with a hole in it. FPointArray marks the break with
	// four points at DBL_MAX/2; the percent array spells it as four negatives.
	QPainterPath path;
	path.moveTo(0.0, 0.0);
	path.lineTo(100.0, 0.0);
	path.lineTo(100.0, 100.0);
	path.lineTo(0.0, 100.0);
	path.closeSubpath();
	path.moveTo(25.0, 25.0);
	path.lineTo(75.0, 25.0);
	path.lineTo(75.0, 75.0);
	path.lineTo(25.0, 75.0);
	path.closeSubpath();

	const QList<double> vals = frameShapeValuesFromPath(path);

	int markers = 0;
	double biggest = 0.0;
	for (int i = 0; i + 3 < vals.size(); i += 4)
	{
		if (vals[i] < 0)
			++markers;
		for (int k = 0; k < 4; ++k)
			biggest = qMax(biggest, vals[i + k]);
	}
	QCOMPARE(markers, 1);
	// Nothing survived as a DBL_MAX point pretending to be geometry.
	QVERIFY(biggest <= 100.0 + kEps);
	// Both rectangles are still there, on either side of the break.
	QVERIFY(hasVertexNear(vals, 0.0, 0.0));
	QVERIFY(hasVertexNear(vals, 25.0, 25.0));
}

/* ------------------------------------------------------------------ */
/* Regular polygons                                                    */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testPolygonVertexCount()
{
	QCOMPARE(vertices(polygonFrameShape(5)).size(), 5);
	QCOMPARE(vertices(polygonFrameShape(6)).size(), 6);
	QCOMPARE(vertices(polygonFrameShape(8)).size(), 8);
	QCOMPARE(vertices(polygonFrameShape(3)).size(), 3);
}

void FrameShapeTests::testPolygonApexAtTop()
{
	for (uint n : { 5u, 6u, 8u })
		QVERIFY2(hasVertexNear(polygonFrameShape(n), 50.0, 0.0),
		         qPrintable(QString("%1-gon has no apex at top centre").arg(n)));
}

void FrameShapeTests::testPolygonAngularSpacingBeforeFitting()
{
	/* Regularity is a property of regularPolygonPath(), the upstream generator
	   being reused. polygonFrameShape() then stretches the result to fill the
	   frame, which is an anisotropic scale and does not preserve angles - so
	   the 360/n spacing is checked here, on the shape as it comes out of
	   Scribus, and the fitted version is checked for fill and symmetry below. */
	for (uint n : { 5u, 6u, 8u })
	{
		const QPainterPath p = regularPolygonPath(100.0, 100.0, n, false, 0.0, 0.0);
		QList<QPointF> corners;
		for (int i = 0; i < p.elementCount(); ++i)
		{
			const QPainterPath::Element& e = p.elementAt(i);
			if (e.type == QPainterPath::MoveToElement || e.type == QPainterPath::LineToElement)
				corners << QPointF(e.x, e.y);
		}
		// closeSubpath() repeats the first corner; drop it.
		while (corners.size() > int(n))
			corners.removeLast();
		QCOMPARE(corners.size(), int(n));

		const double step = 360.0 / n;
		double radius = -1.0;
		for (int i = 0; i < corners.size(); ++i)
		{
			const double dx = corners[i].x() - 50.0;
			const double dy = corners[i].y() - 50.0;
			const double r = std::sqrt(dx * dx + dy * dy);
			if (radius < 0.0)
				radius = r;
			QVERIFY2(qAbs(r - radius) < 1e-6, "vertices are not all on one circle");

			// regularPolygonPath() walks anticlockwise on screen (y points down),
			// so the angle is measured that way too.
			double got = std::atan2(-dx, -dy) * 180.0 / M_PI;   // 0 = straight up
			double want = step * i;
			double diff = std::fmod(got - want + 720.0, 360.0);
			if (diff > 180.0)
				diff -= 360.0;
			QVERIFY2(qAbs(diff) < 1e-6,
			         qPrintable(QString("vertex %1 of %2-gon is %3 deg off").arg(i).arg(n).arg(diff)));
		}
		QCOMPARE(radius, 50.0);
	}
}

void FrameShapeTests::testPolygonFillsFrame()
{
	for (uint n : { 3u, 5u, 6u, 8u })
	{
		const QRectF r = bounds(polygonFrameShape(n));
		QVERIFY2(qAbs(r.left()) < 1e-4 && qAbs(r.top()) < 1e-4, "not anchored at the frame origin");
		QVERIFY2(qAbs(r.width() - 100.0) < 1e-4 && qAbs(r.height() - 100.0) < 1e-4, "does not fill the frame");
	}
}

void FrameShapeTests::testPolygonIsSymmetricAboutVerticalAxis()
{
	for (uint n : { 3u, 5u, 6u, 8u })
		QVERIFY2(symmetricAboutVerticalAxis(polygonFrameShape(n)),
		         qPrintable(QString("%1-gon is lopsided").arg(n)));
}

void FrameShapeTests::testPolygonRejectsFewerThanThreeCorners()
{
	QVERIFY(polygonFrameShape(0).isEmpty());
	QVERIFY(polygonFrameShape(1).isEmpty());
	QVERIFY(polygonFrameShape(2).isEmpty());
}

void FrameShapeTests::testGeneratedTriangleMatchesScribusOwn()
{
	/* The Frame Shape menu takes its Triangle from Scribus's own Autoform table
	   (entry 14) rather than from polygonFrameShape(3), so that the frame gets
	   the identical outline the Properties Palette would give it and the frame
	   type stored on the item honestly names that table entry.

	   This pins the claim that the two are the same shape. If the generator
	   ever stops agreeing with the table, the choice of which to use stops
	   being free and somebody should be told. Compared as vertex sets: the two
	   start at different corners and wind opposite ways, which changes nothing
	   about the triangle. */
	static const double autoformTriangle14[24] = {
		  0.0, 100.0,   0.0, 100.0,
		 50.0,   0.0,  50.0,   0.0,
		 50.0,   0.0,  50.0,   0.0,
		100.0, 100.0, 100.0, 100.0,
		100.0, 100.0, 100.0, 100.0,
		  0.0, 100.0,   0.0, 100.0 };

	QList<double> table;
	for (double v : autoformTriangle14)
		table << v;

	QList<QPointF> fromTable = vertices(table);
	QList<QPointF> generated = vertices(polygonFrameShape(3));
	QCOMPARE(fromTable.size(), 3);
	QCOMPARE(generated.size(), 3);

	for (const QPointF& want : fromTable)
	{
		bool found = false;
		for (const QPointF& got : generated)
		{
			if (qAbs(got.x() - want.x()) < 1e-6 && qAbs(got.y() - want.y()) < 1e-6)
			{
				found = true;
				break;
			}
		}
		QVERIFY2(found, qPrintable(QString("generated triangle has no vertex at %1,%2")
		                           .arg(want.x()).arg(want.y())));
	}
}

/* ------------------------------------------------------------------ */
/* Stars                                                               */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testStarVertexCountAlternates()
{
	QCOMPARE(vertices(starFrameShape(5, 0.4)).size(), 10);
	QCOMPARE(vertices(starFrameShape(6, 0.4)).size(), 12);
}

void FrameShapeTests::testStarInnerRadiusRatio()
{
	/* Measured before the fit-to-frame stretch, for the same reason the polygon
	   angles are: the stretch is anisotropic and a radius ratio is not
	   preserved by it. */
	for (uint n : { 5u, 6u })
	{
		const double ratio = 0.4;
		const QPainterPath p = regularPolygonPath(100.0, 100.0, n, true, ratio, 0.0);
		/* The star comes out as cubics, so the corners are the segment endpoints:
		   the MoveTo, then elementAt(i + 2) of every CurveToElement triple. */
		QList<QPointF> corners;
		for (int i = 0; i < p.elementCount(); ++i)
		{
			const QPainterPath::Element& e = p.elementAt(i);
			if (e.type == QPainterPath::MoveToElement)
				corners << QPointF(e.x, e.y);
			else if (e.type == QPainterPath::CurveToElement && i + 2 < p.elementCount())
				corners << QPointF(p.elementAt(i + 2).x, p.elementAt(i + 2).y);
		}
		// closeSubpath() repeats the first corner; drop it.
		while (corners.size() > int(n) * 2)
			corners.removeLast();
		QCOMPARE(corners.size(), int(n) * 2);

		double outer = 0.0, inner = 0.0;
		for (int i = 0; i < corners.size(); ++i)
		{
			const double dx = corners[i].x() - 50.0;
			const double dy = corners[i].y() - 50.0;
			const double r = std::sqrt(dx * dx + dy * dy);
			if (i % 2 == 0)
			{
				if (outer == 0.0) outer = r;
				QVERIFY2(qAbs(r - outer) < 1e-6, "outer points are not all at one radius");
			}
			else
			{
				if (inner == 0.0) inner = r;
				QVERIFY2(qAbs(r - inner) < 1e-6, "inner points are not all at one radius");
			}
		}
		QCOMPARE(outer, 50.0);
		QVERIFY2(qAbs(inner / outer - ratio) < 1e-6,
		         qPrintable(QString("inner/outer is %1, want %2").arg(inner / outer).arg(ratio)));
	}
}

void FrameShapeTests::testStarApexAtTop()
{
	QVERIFY(hasVertexNear(starFrameShape(5, 0.4), 50.0, 0.0));
	QVERIFY(hasVertexNear(starFrameShape(6, 0.4), 50.0, 0.0));
}

void FrameShapeTests::testStarFillsFrame()
{
	for (uint n : { 5u, 6u })
	{
		const QRectF r = bounds(starFrameShape(n, 0.4));
		QVERIFY2(qAbs(r.left()) < 1e-4 && qAbs(r.top()) < 1e-4, "not anchored at the frame origin");
		QVERIFY2(qAbs(r.width() - 100.0) < 1e-4 && qAbs(r.height() - 100.0) < 1e-4, "does not fill the frame");
	}
}

void FrameShapeTests::testStarIsSymmetricAboutVerticalAxis()
{
	QVERIFY(symmetricAboutVerticalAxis(starFrameShape(5, 0.4)));
	QVERIFY(symmetricAboutVerticalAxis(starFrameShape(6, 0.4)));
}

void FrameShapeTests::testStarRejectsFewerThanThreePoints()
{
	QVERIFY(starFrameShape(0, 0.4).isEmpty());
	QVERIFY(starFrameShape(2, 0.4).isEmpty());
}

/* ------------------------------------------------------------------ */
/* Cross                                                               */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testCrossHasTwelveVertices()
{
	QCOMPARE(vertices(crossFrameShape(0.30)).size(), 12);
}

void FrameShapeTests::testCrossArmWidthIsHonoured()
{
	// 30% arms put the inner edges at 35 and 65.
	const QList<double> vals = crossFrameShape(0.30);
	QVERIFY(hasVertexNear(vals, 35.0, 0.0));
	QVERIFY(hasVertexNear(vals, 65.0, 0.0));
	QVERIFY(hasVertexNear(vals, 100.0, 35.0));
	QVERIFY(hasVertexNear(vals, 0.0, 65.0));
	QVERIFY(hasVertexNear(vals, 35.0, 35.0));

	// Half-width arms are the classic plus sign, inner edges at 25 and 75.
	const QList<double> half = crossFrameShape(0.50);
	QVERIFY(hasVertexNear(half, 25.0, 0.0));
	QVERIFY(hasVertexNear(half, 75.0, 25.0));
	QCOMPARE(vertices(half).size(), 12);
}

void FrameShapeTests::testCrossFillsFrame()
{
	const QRectF r = bounds(crossFrameShape(0.30));
	QCOMPARE(r.left(), 0.0);
	QCOMPARE(r.top(), 0.0);
	QCOMPARE(r.width(), 100.0);
	QCOMPARE(r.height(), 100.0);
}

void FrameShapeTests::testCrossIsSymmetricOnBothAxes()
{
	const QList<double> vals = crossFrameShape(0.30);
	QVERIFY(symmetricAboutVerticalAxis(vals));
	QVERIFY(symmetricAboutHorizontalAxis(vals));
}

void FrameShapeTests::testCrossClampsOutOfRangeArms()
{
	/* The clamp exists so the function keeps its promise at any input: a zero
	   or negative arm would collapse the cross to a line, and an arm of the
	   full frame width would swallow the notches and leave a plain rectangle -
	   Qt drops the zero-length edges and four vertices come back instead of
	   twelve. Both ends are clamped just inside those, so a cross is always a
	   cross. */
	for (double arm : { -1.0, 0.0, 1.0, 5.0 })
	{
		const QList<double> vals = crossFrameShape(arm);
		QVERIFY2(vertices(vals).size() == 12,
		         qPrintable(QString("arm %1 gave %2 vertices").arg(arm).arg(vertices(vals).size())));
		const QRectF r = bounds(vals);
		QVERIFY(qAbs(r.width() - 100.0) < 1e-4);
		QVERIFY(qAbs(r.height() - 100.0) < 1e-4);
		QVERIFY(symmetricAboutVerticalAxis(vals));
	}
}

/* ------------------------------------------------------------------ */
/* Properties every generated shape has to have                        */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testEveryShapeIsQuadAligned()
{
	/* SetFrameShape() walks the array four doubles at a time and stops at
	   count-3, so a length that is not a multiple of four silently drops the
	   tail of the shape. */
	const QList<QPair<QString, QList<double>>> shapes = generatedShapes();
	for (const auto& s : shapes)
	{
		QVERIFY2(s.second.size() % 4 == 0, qPrintable(s.first + " has a ragged array"));
		QVERIFY2(s.second.size() % 8 == 0, qPrintable(s.first + " has a half segment"));
		QVERIFY2(!s.second.isEmpty(), qPrintable(s.first + " is empty"));
	}
}

void FrameShapeTests::testEveryShapeFillsAndCentresInTheFrame()
{
	const QList<QPair<QString, QList<double>>> shapes = generatedShapes();
	for (const auto& s : shapes)
	{
		const QRectF r = bounds(s.second);
		QVERIFY2(r.left() >= -1e-4 && r.top() >= -1e-4, qPrintable(s.first + " overflows the frame"));
		QVERIFY2(r.right() <= 100.0 + 1e-4 && r.bottom() <= 100.0 + 1e-4,
		         qPrintable(s.first + " overflows the frame"));
		QVERIFY2(qAbs(r.center().x() - 50.0) < 1e-4 && qAbs(r.center().y() - 50.0) < 1e-4,
		         qPrintable(s.first + " is not centred"));
		QVERIFY2(qAbs(r.width() - 100.0) < 1e-4 && qAbs(r.height() - 100.0) < 1e-4,
		         qPrintable(s.first + " does not fill the frame"));
	}
}

void FrameShapeTests::testEveryShapeIsScaleInvariant()
{
	/* The values are percentages, so applying one to a frame is a multiply.
	   Repeating SetFrameShape()'s arithmetic at two very different frame sizes
	   has to land on that frame's own bounds both times - that is what lets a
	   shape survive a resize instead of needing to be reapplied. */
	const QList<QPair<QString, QList<double>>> shapes = generatedShapes();
	const QList<QSizeF> frames = { QSizeF(120.0, 120.0), QSizeF(600.0, 45.0), QSizeF(11.5, 803.25) };
	for (const auto& s : shapes)
	{
		for (const QSizeF& f : frames)
		{
			QList<double> scaled;
			for (int i = 0; i + 1 < s.second.size(); i += 2)
				scaled << s.second[i] * f.width() / 100.0 << s.second[i + 1] * f.height() / 100.0;
			const QRectF r = bounds(scaled);
			QVERIFY2(qAbs(r.width() - f.width()) < 1e-6 && qAbs(r.height() - f.height()) < 1e-6,
			         qPrintable(QString("%1 at %2x%3 came out %4x%5")
			                    .arg(s.first).arg(f.width()).arg(f.height()).arg(r.width()).arg(r.height())));
			QVERIFY2(qAbs(r.left()) < 1e-6 && qAbs(r.top()) < 1e-6,
			         qPrintable(s.first + " drifted off the frame origin"));
		}
	}
}

/* ------------------------------------------------------------------ */
/* Shape-crop overlay: canvas <-> doc coordinate transforms            */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testShapeCropCanvasDocRoundTrip()
{
	const double scale = 2.0;
	const QPointF minCanvas(5.0, 7.0);
	const QPointF canvasPt(50.0, 80.0);

	const QPointF doc = ShapeCrop::canvasToDoc(canvasPt, scale, minCanvas);
	QVERIFY2(qAbs(doc.x() - 30.0) < kEps && qAbs(doc.y() - 47.0) < kEps,
	         qPrintable(QString("canvasToDoc gave %1,%2, want 30,47").arg(doc.x()).arg(doc.y())));

	const QPointF back = ShapeCrop::docToCanvas(doc, scale, minCanvas);
	QVERIFY2(qAbs(back.x() - canvasPt.x()) < kEps && qAbs(back.y() - canvasPt.y()) < kEps,
	         "docToCanvas(canvasToDoc(p)) did not round-trip");
}

void FrameShapeTests::testShapeCropDocToCanvasRectScalesAndOffsets()
{
	const double scale = 2.0;
	const QPointF minCanvas(5.0, 7.0);
	const QRectF docRect(30.0, 47.0, 20.0, 10.0);

	const QRectF canvasRect = ShapeCrop::docToCanvas(docRect, scale, minCanvas);
	QVERIFY2(qAbs(canvasRect.left() - 50.0) < kEps && qAbs(canvasRect.top() - 80.0) < kEps,
	         "wrong canvas-space origin");
	QVERIFY2(qAbs(canvasRect.width() - 40.0) < kEps && qAbs(canvasRect.height() - 20.0) < kEps,
	         "doc-space size was not scaled by the view scale");
}

/* ------------------------------------------------------------------ */
/* Shape-crop overlay: rotation maths                                  */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testShapeCropRotatePointCardinalAngles()
{
	// A point to the right of the origin, rotated clockwise on screen (Qt's QPainter::rotate()
	// convention, y down): 90 deg sends "right" to "down", 180 to "left", 270 to "up".
	const QPointF center(0.0, 0.0);
	const QPointF right(1.0, 0.0);

	const QPointF at90 = ShapeCrop::rotatePoint(right, center, 90.0);
	QVERIFY2(qAbs(at90.x() - 0.0) < kEps && qAbs(at90.y() - 1.0) < kEps,
	         qPrintable(QString("90deg gave %1,%2, want 0,1").arg(at90.x()).arg(at90.y())));

	const QPointF at180 = ShapeCrop::rotatePoint(right, center, 180.0);
	QVERIFY2(qAbs(at180.x() - (-1.0)) < kEps && qAbs(at180.y() - 0.0) < kEps,
	         qPrintable(QString("180deg gave %1,%2, want -1,0").arg(at180.x()).arg(at180.y())));

	const QPointF at270 = ShapeCrop::rotatePoint(right, center, 270.0);
	QVERIFY2(qAbs(at270.x() - 0.0) < kEps && qAbs(at270.y() - (-1.0)) < kEps,
	         qPrintable(QString("270deg gave %1,%2, want 0,-1").arg(at270.x()).arg(at270.y())));
}

void FrameShapeTests::testShapeCropRotatePointRoundTrip()
{
	const QPointF center(5.0, 5.0);
	const QPointF pt(12.0, -3.0);
	for (double angle : { 10.0, 37.0, 90.0, 123.5, 200.0, 359.0 })
	{
		const QPointF rotated = ShapeCrop::rotatePoint(pt, center, angle);
		const QPointF back = ShapeCrop::rotatePoint(rotated, center, -angle);
		QVERIFY2(qAbs(back.x() - pt.x()) < 1e-9 && qAbs(back.y() - pt.y()) < 1e-9,
		         qPrintable(QString("angle %1: round trip landed on %2,%3").arg(angle).arg(back.x()).arg(back.y())));
	}
}

void FrameShapeTests::testShapeCropAngleFromCenterMatchesRotatePoint()
{
	// angleFromCenter() is rotatePoint()'s inverse question ("what angle got me here" rather
	// than "where does this angle put me") - the crop overlay's rotation handle relies on the
	// two agreeing, or dragging the handle would rotate the shape by the wrong amount.
	const QPointF center(20.0, -10.0);
	const QPointF straightUp(20.0, -110.0); // 100 above centre
	for (double angle : { 0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0 })
	{
		const QPointF pt = ShapeCrop::rotatePoint(straightUp, center, angle);
		const double got = ShapeCrop::angleFromCenter(center, pt);
		double diff = std::fmod(got - angle + 720.0, 360.0);
		if (diff > 180.0)
			diff -= 360.0;
		QVERIFY2(qAbs(diff) < 1e-6,
		         qPrintable(QString("angle %1: angleFromCenter said %2").arg(angle).arg(got)));
	}
}

/* ------------------------------------------------------------------ */
/* Shape-crop overlay: handle layout and hit-testing                   */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testShapeCropHandlePositionsLayout()
{
	const QRectF rect(0.0, 0.0, 100.0, 50.0);
	const QList<QPointF> h = ShapeCrop::handlePositions(rect, 10.0);
	QCOMPARE(h.size(), 9);

	const QList<QPointF> expected = {
		QPointF(0.0, 0.0), QPointF(50.0, 0.0), QPointF(100.0, 0.0),
		QPointF(100.0, 25.0), QPointF(100.0, 50.0), QPointF(50.0, 50.0),
		QPointF(0.0, 50.0), QPointF(0.0, 25.0), QPointF(50.0, -10.0)
	};
	for (int i = 0; i < expected.size(); ++i)
	{
		QVERIFY2(qAbs(h[i].x() - expected[i].x()) < kEps && qAbs(h[i].y() - expected[i].y()) < kEps,
		         qPrintable(QString("handle %1: got %2,%3 want %4,%5")
		                    .arg(i).arg(h[i].x()).arg(h[i].y()).arg(expected[i].x()).arg(expected[i].y())));
	}
}

void FrameShapeTests::testShapeCropHitTestFindsEachHandle()
{
	const QRectF rect(0.0, 0.0, 100.0, 50.0);
	const QList<QPointF> h = ShapeCrop::handlePositions(rect, 10.0);
	for (int i = 0; i < h.size(); ++i)
	{
		const ShapeCrop::Handle got = ShapeCrop::hitTest(h[i], rect, 0.0, 5.0, 10.0);
		QVERIFY2(got == static_cast<ShapeCrop::Handle>(i),
		         qPrintable(QString("handle %1: hitTest returned %2").arg(i).arg(int(got))));
	}
}

void FrameShapeTests::testShapeCropHitTestBodyAndOutside()
{
	const QRectF rect(0.0, 0.0, 100.0, 50.0);
	QCOMPARE(int(ShapeCrop::hitTest(QPointF(50.0, 25.0), rect, 0.0, 5.0, 10.0)), int(ShapeCrop::HandleBody));
	// 10 doc units from the Right handle (100,25), well past a 5-unit tolerance, and outside the
	// rect too: nothing here for a Photoshop-style crop to grab, so a click has to fall through
	// to "cancel", not silently do nothing.
	QCOMPARE(int(ShapeCrop::hitTest(QPointF(110.0, 25.0), rect, 0.0, 5.0, 10.0)), int(ShapeCrop::HandleNone));
	QCOMPARE(int(ShapeCrop::hitTest(QPointF(1000.0, 1000.0), rect, 0.0, 5.0, 10.0)), int(ShapeCrop::HandleNone));
}

void FrameShapeTests::testShapeCropHitTestRespectsRotation()
{
	/* The overlay's Right handle, once the overlay is rotated 90 degrees clockwise about its own
	   centre, is where the unrotated Bottom handle used to be. hitTest() has to undo that rotation
	   before comparing against the unrotated handle table, or a rotated overlay would offer the
	   wrong handle - or none - under the user's cursor. */
	const QRectF rect(0.0, 0.0, 100.0, 50.0);
	const QPointF center = rect.center();
	const QPointF rightHandleLocal(100.0, 25.0);
	const QPointF rotatedPos = ShapeCrop::rotatePoint(rightHandleLocal, center, 90.0);

	const ShapeCrop::Handle got = ShapeCrop::hitTest(rotatedPos, rect, 90.0, 5.0, 10.0);
	QCOMPARE(int(got), int(ShapeCrop::HandleRight));

	// The same canvas point, asked against the *unrotated* overlay, is not a handle at all - it
	// is off the bottom edge of the rect (bottom is at y=50, this point is at y=75).
	const ShapeCrop::Handle gotUnrotated = ShapeCrop::hitTest(rotatedPos, rect, 0.0, 5.0, 10.0);
	QCOMPARE(int(gotUnrotated), int(ShapeCrop::HandleNone));
}

/* ------------------------------------------------------------------ */
/* Shape-crop overlay: resize maths                                    */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testShapeCropResizeEdgeIsSingleAxis()
{
	const QRectF start(0.0, 0.0, 100.0, 50.0);
	// A huge y component on an edge handle must be ignored - only the edge's own axis moves.
	const QRectF r = ShapeCrop::resizeRect(start, ShapeCrop::HandleRight, QPointF(20.0, 999.0), false);
	QVERIFY2(qAbs(r.width() - 120.0) < kEps && qAbs(r.height() - 50.0) < kEps,
	         qPrintable(QString("Right handle gave %1x%2, want 120x50").arg(r.width()).arg(r.height())));
	QVERIFY2(qAbs(r.left()) < kEps && qAbs(r.top()) < kEps, "left/top edges must stay put");
}

void FrameShapeTests::testShapeCropResizeCornerFreeVsLockedAspect()
{
	const QRectF start(0.0, 0.0, 100.0, 50.0); // 2:1 aspect
	const QPointF delta(20.0, 0.0); // only x moves

	const QRectF free = ShapeCrop::resizeRect(start, ShapeCrop::HandleBottomRight, delta, false);
	QVERIFY2(qAbs(free.width() - 120.0) < kEps && qAbs(free.height() - 50.0) < kEps,
	         qPrintable(QString("free resize gave %1x%2, want 120x50").arg(free.width()).arg(free.height())));

	const QRectF locked = ShapeCrop::resizeRect(start, ShapeCrop::HandleBottomRight, delta, true);
	QVERIFY2(qAbs(locked.width() - 120.0) < kEps && qAbs(locked.height() - 60.0) < kEps,
	         qPrintable(QString("locked resize gave %1x%2, want 120x60 (2:1 preserved)")
	                    .arg(locked.width()).arg(locked.height())));
}

void FrameShapeTests::testShapeCropResizeAnchorsOppositeCorner()
{
	const QRectF start(0.0, 0.0, 100.0, 50.0);
	const QRectF r = ShapeCrop::resizeRect(start, ShapeCrop::HandleTopLeft, QPointF(10.0, 5.0), false);
	QVERIFY2(qAbs(r.left() - 10.0) < kEps && qAbs(r.top() - 5.0) < kEps, "top-left corner did not move with the drag");
	QVERIFY2(qAbs(r.right() - 100.0) < kEps && qAbs(r.bottom() - 50.0) < kEps,
	         "bottom-right corner (the anchor) must not move for a top-left drag");
}

void FrameShapeTests::testShapeCropResizeClampsToMinSize()
{
	const QRectF start(0.0, 0.0, 100.0, 50.0);
	const QRectF r = ShapeCrop::resizeRect(start, ShapeCrop::HandleBottomRight, QPointF(-1000.0, -1000.0), false, 4.0);
	QVERIFY2(qAbs(r.width() - 4.0) < kEps && qAbs(r.height() - 4.0) < kEps,
	         qPrintable(QString("dragging past the anchor gave %1x%2, want the 4x4 floor")
	                    .arg(r.width()).arg(r.height())));
	QVERIFY2(qAbs(r.left()) < kEps && qAbs(r.top()) < kEps, "anchor corner drifted while clamping");
}

/* ------------------------------------------------------------------ */
/* Shape-crop overlay: shape generation at arbitrary bounds             */
/* ------------------------------------------------------------------ */

void FrameShapeTests::testShapeCropPathFromPercentValuesFillsArbitraryBox()
{
	QPainterPath src;
	src.moveTo(0.0, 0.0);
	src.lineTo(100.0, 0.0);
	src.lineTo(100.0, 100.0);
	src.lineTo(0.0, 100.0);
	src.closeSubpath();
	const QList<double> percent = frameShapeValuesFromPath(src);

	// The overlay is rarely frame-sized (100x100-equivalent) or square - this is its everyday
	// case, the user having dragged it to some arbitrary w x h.
	const QPainterPath box = ShapeCrop::pathFromPercentValues(percent, 40.0, 20.0);
	const QRectF r = box.boundingRect();
	QVERIFY2(qAbs(r.left()) < kEps && qAbs(r.top()) < kEps, "not anchored at the box origin");
	QVERIFY2(qAbs(r.width() - 40.0) < kEps && qAbs(r.height() - 20.0) < kEps,
	         qPrintable(QString("came out %1x%2, want 40x20").arg(r.width()).arg(r.height())));
}

void FrameShapeTests::testShapeCropPathFromPercentValuesAtNonSquareSize()
{
	// A pentagon evaluated at a size that is neither square nor 100x100 - an ordinary size for a
	// freely-resized crop overlay.
	const QList<double> percent = polygonFrameShape(5);
	const QPainterPath box = ShapeCrop::pathFromPercentValues(percent, 300.0, 45.0);
	const QRectF r = box.boundingRect();
	QVERIFY2(qAbs(r.width() - 300.0) < 1e-3 && qAbs(r.height() - 45.0) < 1e-3,
	         qPrintable(QString("pentagon at 300x45 came out %1x%2").arg(r.width()).arg(r.height())));
}

QTEST_APPLESS_MAIN(FrameShapeTests)
