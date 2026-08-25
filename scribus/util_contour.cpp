/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "util_contour.h"

#include <QPointF>
#include <QStack>

#include <cmath>

namespace
{
	// Directions, in y-down raster coordinates.
	// 0 = +x (right), 1 = +y (down), 2 = -x (left), 3 = -y (up)
	const int DX[4] = { 1, 0, -1, 0 };
	const int DY[4] = { 0, 1, 0, -1 };

	inline int cornerId(int x, int y, int stride) { return y * stride + x; }

	//! Perpendicular distance from \a p to the line through \a a and \a b.
	double pointLineDistance(const QPointF& p, const QPointF& a, const QPointF& b)
	{
		double dx = b.x() - a.x();
		double dy = b.y() - a.y();
		double len2 = dx * dx + dy * dy;
		if (len2 <= 0.0)
			return std::hypot(p.x() - a.x(), p.y() - a.y());
		double num = std::fabs(dy * p.x() - dx * p.y() + b.x() * a.y() - b.y() * a.x());
		return num / std::sqrt(len2);
	}

	/*! Iterative Ramer-Douglas-Peucker over the open polyline
	    \a pts[first..last], marking survivors in \a keep. Iterative rather
	    than recursive: a traced boundary can be tens of thousands of points
	    long, and the worst case recurses once per point. */
	void rdpOpen(const QPolygonF& pts, int first, int last, double tolerance, QVector<bool>& keep)
	{
		if (last <= first + 1)
			return;

		QStack<QPair<int, int>> work;
		work.push(qMakePair(first, last));

		while (!work.isEmpty())
		{
			QPair<int, int> range = work.pop();
			int a = range.first;
			int b = range.second;
			if (b <= a + 1)
				continue;

			double worst = -1.0;
			int worstIdx = -1;
			for (int i = a + 1; i < b; ++i)
			{
				double d = pointLineDistance(pts[i], pts[a], pts[b]);
				if (d > worst)
				{
					worst = d;
					worstIdx = i;
				}
			}

			if (worst > tolerance && worstIdx > 0)
			{
				keep[worstIdx] = true;
				work.push(qMakePair(a, worstIdx));
				work.push(qMakePair(worstIdx, b));
			}
		}
	}
}

double ScContour::signedArea(const QPolygonF& ring)
{
	int n = ring.size();
	if (n < 3)
		return 0.0;
	double sum = 0.0;
	for (int i = 0; i < n; ++i)
	{
		const QPointF& p = ring[i];
		const QPointF& q = ring[(i + 1) % n];
		sum += p.x() * q.y() - q.x() * p.y();
	}
	return sum * 0.5;
}

QVector<bool> ScContour::binarize(const QImage& mask, int threshold)
{
	QVector<bool> grid;
	if (mask.isNull())
		return grid;

	QImage src = mask;
	if (src.format() != QImage::Format_Grayscale8)
		src = src.convertToFormat(QImage::Format_Grayscale8);

	const int w = src.width();
	const int h = src.height();
	grid.resize(w * h);
	for (int y = 0; y < h; ++y)
	{
		const uchar* line = src.constScanLine(y);
		bool* out = grid.data() + qsizetype(y) * w;
		for (int x = 0; x < w; ++x)
			out[x] = (int(line[x]) >= threshold);
	}
	return grid;
}

QList<ScContour::Ring> ScContour::traceBoundaries(const QVector<bool>& grid, int w, int h)
{
	QList<Ring> rings;
	if (w <= 0 || h <= 0 || grid.size() < qsizetype(w) * h)
		return rings;

	auto solid = [&](int x, int y) -> bool {
		if (x < 0 || y < 0 || x >= w || y >= h)
			return false;
		return grid[qsizetype(y) * w + x];
	};

	// One nibble per grid corner: bit d set means "a boundary edge leaves this
	// corner heading in direction d". A directed unit edge from a given corner
	// in a given direction is unique, so a bitmask is enough.
	const int stride = w + 1;
	QVector<quint8> outgoing(qsizetype(stride) * (h + 1), 0);

	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			if (!solid(x, y))
				continue;
			// Oriented with the solid cell on the right of the direction of
			// travel; that is what makes outer rings come out positive and
			// holes negative under the shoelace formula.
			if (!solid(x, y - 1))                                  // top
				outgoing[cornerId(x, y, stride)] |= (1 << 0);      //   -> +x
			if (!solid(x + 1, y))                                  // right
				outgoing[cornerId(x + 1, y, stride)] |= (1 << 1);  //   -> +y
			if (!solid(x, y + 1))                                  // bottom
				outgoing[cornerId(x + 1, y + 1, stride)] |= (1 << 2); // -> -x
			if (!solid(x - 1, y))                                  // left
				outgoing[cornerId(x, y + 1, stride)] |= (1 << 3);  //   -> -y
		}
	}

	for (int sy = 0; sy <= h; ++sy)
	{
		for (int sx = 0; sx <= w; ++sx)
		{
			int startId = cornerId(sx, sy, stride);
			while (outgoing[startId] != 0)
			{
				int dir = -1;
				for (int d = 0; d < 4; ++d)
				{
					if (outgoing[startId] & (1 << d)) { dir = d; break; }
				}
				if (dir < 0)
					break;

				QPolygonF ring;
				int cx = sx;
				int cy = sy;
				int cid = startId;

				while (true)
				{
					ring << QPointF(cx, cy);
					outgoing[cid] &= ~(1 << dir);
					cx += DX[dir];
					cy += DY[dir];
					cid = cornerId(cx, cy, stride);
					if (cid == startId)
						break;

					// Sharpest clockwise turn first. This is the saddle
					// tie-break: it keeps two diagonally touching regions as
					// two rings instead of merging them into a figure eight.
					int next = -1;
					const int order[4] = { (dir + 1) % 4, dir, (dir + 3) % 4, (dir + 2) % 4 };
					for (int i = 0; i < 4; ++i)
					{
						if (outgoing[cid] & (1 << order[i])) { next = order[i]; break; }
					}
					if (next < 0)
						break;   // malformed input; abandon this ring
					dir = next;
				}

				if (ring.size() >= 4)
				{
					Ring r;
					r.points = ring;
					r.signedArea = signedArea(ring);
					rings.append(r);
				}
			}
		}
	}

	return rings;
}

QPolygonF ScContour::removeCollinear(const QPolygonF& ring)
{
	int n = ring.size();
	if (n < 3)
		return ring;

	QPolygonF out;
	for (int i = 0; i < n; ++i)
	{
		const QPointF& prev = ring[(i + n - 1) % n];
		const QPointF& cur  = ring[i];
		const QPointF& next = ring[(i + 1) % n];
		double cross = (cur.x() - prev.x()) * (next.y() - prev.y())
		             - (cur.y() - prev.y()) * (next.x() - prev.x());
		if (std::fabs(cross) > 1e-9)
			out << cur;
	}
	// A ring that is entirely collinear is degenerate; keep the original
	// rather than handing back something with no area at all.
	return (out.size() >= 3) ? out : ring;
}

QPolygonF ScContour::simplifyClosed(const QPolygonF& ring, double tolerance)
{
	int n = ring.size();
	if (n < 4 || tolerance <= 0.0)
		return ring;

	// A closed ring has no endpoints for RDP to anchor on. Split it at two
	// far-apart points so both halves are ordinary open polylines: the point
	// farthest from the centroid, and the point farthest from that one.
	QPointF centroid(0, 0);
	for (const QPointF& p : ring)
		centroid += p;
	centroid /= double(n);

	int a = 0;
	double best = -1.0;
	for (int i = 0; i < n; ++i)
	{
		double d = QLineF(centroid, ring[i]).length();
		if (d > best) { best = d; a = i; }
	}

	int b = a;
	best = -1.0;
	for (int i = 0; i < n; ++i)
	{
		double d = QLineF(ring[a], ring[i]).length();
		if (d > best) { best = d; b = i; }
	}
	if (a == b)
		return ring;

	// Roll the ring so it starts at a, then treat a..b and b..a as two chains.
	QPolygonF rolled;
	for (int i = 0; i < n; ++i)
		rolled << ring[(a + i) % n];
	int split = (b - a + n) % n;

	QVector<bool> keep(n, false);
	keep[0] = true;
	keep[split] = true;
	rdpOpen(rolled, 0, split, tolerance, keep);

	// Second chain runs split..n and wraps to index 0; append the start point
	// so the recursion has a closing anchor.
	QPolygonF tail;
	for (int i = split; i < n; ++i)
		tail << rolled[i];
	tail << rolled[0];
	QVector<bool> keepTail(tail.size(), false);
	keepTail[0] = true;
	keepTail[tail.size() - 1] = true;
	rdpOpen(tail, 0, tail.size() - 1, tolerance, keepTail);
	for (int i = 1; i < tail.size() - 1; ++i)
	{
		if (keepTail[i])
			keep[split + i] = true;
	}

	QPolygonF out;
	for (int i = 0; i < n; ++i)
	{
		if (keep[i])
			out << rolled[i];
	}
	return (out.size() >= 3) ? out : ring;
}

QList<ScContour::Ring> ScContour::detect(const QImage& mask, int threshold, double tolerance, Mode mode)
{
	QList<Ring> result;
	if (mask.isNull())
		return result;

	QVector<bool> grid = binarize(mask, threshold);
	QList<Ring> rings = traceBoundaries(grid, mask.width(), mask.height());
	if (rings.isEmpty())
		return result;

	QList<Ring> outers;
	QList<Ring> holes;
	for (const Ring& r : rings)
	{
		if (r.isHole())
			holes.append(r);
		else
			outers.append(r);
	}
	if (outers.isEmpty())
		return result;

	if (mode == Mode::LargestOnly)
	{
		int bestIdx = 0;
		for (int i = 1; i < outers.size(); ++i)
		{
			if (outers[i].signedArea > outers[bestIdx].signedArea)
				bestIdx = i;
		}
		result.append(outers[bestIdx]);
	}
	else
	{
		result = outers;
		if (mode == Mode::IncludeHoles)
			result.append(holes);
	}

	for (Ring& r : result)
	{
		r.points = removeCollinear(r.points);
		r.points = simplifyClosed(r.points, tolerance);
		// Simplification can move points; recompute so callers still get a
		// winding they can trust.
		r.signedArea = signedArea(r.points);
	}
	return result;
}
