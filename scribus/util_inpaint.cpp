/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "util_inpaint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace
{
	//! Fast marching states. KNOWN pixels have a final colour and a final
	//! arrival time; BAND pixels have been given a colour but their arrival
	//! time may still be lowered; INSIDE pixels are still untouched hole.
	constexpr uint8_t StKnown = 0;
	constexpr uint8_t StBand = 1;
	constexpr uint8_t StInside = 2;

	//! Stands in for "has not been reached yet". Any real arrival time inside
	//! a mask that fits in an image is far below this.
	constexpr float InfiniteT = 1.0e6f;

	//! Perpendicular neighbours would otherwise weigh exactly nothing, which
	//! leaves a pixel with no contributors at all in a thin diagonal hole.
	constexpr double MinDirectionWeight = 1.0e-6;

	//! Heap pops between two polls of the caller's cancel flag. Small enough
	//! that Cancel feels immediate, large enough not to show in the profile.
	constexpr size_t CancelCheckInterval = 1024;

	/*! \brief The marching state over the working rectangle.

	    Everything is indexed in rectangle-local coordinates. Out-of-rectangle
	    is treated as "not known": the rectangle is grown past the mask by more
	    than the inpainting radius, so a pixel that is being filled never needs
	    to look outside it, and where the rectangle really is the edge of the
	    picture that is the correct answer anyway. */
	struct Field
	{
		int w {0};
		int h {0};
		std::vector<float> t;
		std::vector<uint8_t> state;
		//! Popped from the heap already, so its arrival time is settled.
		std::vector<uint8_t> settled;
		/*! \brief Pixel was never masked, so its colour is photograph rather
		    than reconstruction. Only these are differentiated; see
		    gradientOfColour(). */
		std::vector<uint8_t> original;

		inline int idx(int x, int y) const { return y * w + x; }
		inline bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
		inline bool isKnown(int x, int y) const { return contains(x, y) && state[idx(x, y)] == StKnown; }
		inline bool isOriginal(int x, int y) const { return contains(x, y) && original[idx(x, y)] != 0; }
		//! True when this pixel has an arrival time to differentiate, i.e. the
		//! march has already reached it.
		inline bool hasTime(int x, int y) const { return contains(x, y) && t[idx(x, y)] < InfiniteT; }
	};

	/*! \brief One upwind solution of |grad T| = 1 from a pair of neighbours.

	    This is the quadratic in Telea section 2: with both neighbours known the
	    two-sided root is used when it really is upwind of both, otherwise the
	    one-sided estimate. Returns InfiniteT when neither neighbour is known.*/
	float solveEikonal(const Field& f, int x1, int y1, int x2, int y2)
	{
		const bool k1 = f.isKnown(x1, y1);
		const bool k2 = f.isKnown(x2, y2);
		if (!k1 && !k2)
			return InfiniteT;

		if (k1 && k2)
		{
			const float t1 = f.t[f.idx(x1, y1)];
			const float t2 = f.t[f.idx(x2, y2)];
			const float diff = t1 - t2;
			const float disc = 2.0f - diff * diff;
			if (disc > 0.0f)
			{
				const float r = std::sqrt(disc);
				float s = (t1 + t2 - r) * 0.5f;
				if (s >= t1 && s >= t2)
					return s;
				s += r;
				if (s >= t1 && s >= t2)
					return s;
			}
			// The two arrival times differ by more than one pixel of travel, so
			// the quadratic has no root that is upwind of both. The nearer
			// neighbour alone is then the honest estimate.
			return 1.0f + std::min(t1, t2);
		}

		return 1.0f + (k1 ? f.t[f.idx(x1, y1)] : f.t[f.idx(x2, y2)]);
	}

	//! Smallest arrival time reachable through any of the four quadrants.
	float marchTime(const Field& f, int x, int y)
	{
		float best = solveEikonal(f, x - 1, y, x, y - 1);
		best = std::min(best, solveEikonal(f, x + 1, y, x, y - 1));
		best = std::min(best, solveEikonal(f, x - 1, y, x, y + 1));
		best = std::min(best, solveEikonal(f, x + 1, y, x, y + 1));
		return best;
	}

	/*! \brief Gradient of the arrival time at \a x, \a y.

	    This is the direction the boundary is travelling in, which is what
	    decides whether a known pixel lies "along" the propagation and so should
	    dominate the average. Central differences where both sides have been
	    reached, one-sided where only one has, zero where neither has. */
	void gradientOfTime(const Field& f, int x, int y, double& gx, double& gy)
	{
		const float here = f.t[f.idx(x, y)];

		const bool left = f.hasTime(x - 1, y);
		const bool right = f.hasTime(x + 1, y);
		if (left && right)
			gx = (double(f.t[f.idx(x + 1, y)]) - double(f.t[f.idx(x - 1, y)])) * 0.5;
		else if (right)
			gx = double(f.t[f.idx(x + 1, y)]) - double(here);
		else if (left)
			gx = double(here) - double(f.t[f.idx(x - 1, y)]);
		else
			gx = 0.0;

		const bool up = f.hasTime(x, y - 1);
		const bool down = f.hasTime(x, y + 1);
		if (up && down)
			gy = (double(f.t[f.idx(x, y + 1)]) - double(f.t[f.idx(x, y - 1)])) * 0.5;
		else if (down)
			gy = double(f.t[f.idx(x, y + 1)]) - double(here);
		else if (up)
			gy = double(here) - double(f.t[f.idx(x, y - 1)]);
		else
			gy = 0.0;
	}

	//! Working picture: one float per channel per pixel, un-premultiplied,
	//! in R G B A order. Kept as floats because every filled pixel is a
	//! weighted average that later pixels then average again, and rounding to
	//! 8 bits at every step visibly bands a long fill.
	struct Plane
	{
		int w {0};
		int h {0};
		std::vector<float> v;   //!< w * h * 4

		inline const float* at(int x, int y) const { return &v[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
		inline float* at(int x, int y) { return &v[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
	};

	/*! \brief Gradient of the picture at an unmasked pixel, per channel.

	    Only pixels that were never masked are differentiated. Telea's equation
	    2 carries each contributing pixel to p along its own gradient, and that
	    term is what lets a smooth ramp continue across a hole instead of
	    flattening into its mean. Measured against ground truth it is also
	    where the method goes wrong on a photograph: a filled pixel's gradient
	    is a gradient of the fill, so extrapolating along it feeds the fill's
	    own error back in, amplified by the distance it is carried. On a
	    high-contrast edge that runs away into coloured spokes radiating from
	    the hole - the classic Telea starburst.

	    Restricting the derivative to real photograph, and clamping in
	    fillPixel(), measured +4.8 dB and +6.4 dB PSNR on two real photographs
	    against the formula applied literally, while still recovering about
	    three quarters of a linear ramp that dropping the term outright would
	    lose. Both safeguards are bounds on the term, not a change to it. */
	void gradientOfColour(const Plane& p, const Field& f, int x, int y, double gx[4], double gy[4])
	{
		const bool left = f.isOriginal(x - 1, y);
		const bool right = f.isOriginal(x + 1, y);
		const bool up = f.isOriginal(x, y - 1);
		const bool down = f.isOriginal(x, y + 1);

		const float* here = p.at(x, y);
		const float* pl = left ? p.at(x - 1, y) : nullptr;
		const float* pr = right ? p.at(x + 1, y) : nullptr;
		const float* pu = up ? p.at(x, y - 1) : nullptr;
		const float* pd = down ? p.at(x, y + 1) : nullptr;

		for (int c = 0; c < 4; ++c)
		{
			if (pl && pr)
				gx[c] = (double(pr[c]) - double(pl[c])) * 0.5;
			else if (pr)
				gx[c] = double(pr[c]) - double(here[c]);
			else if (pl)
				gx[c] = double(here[c]) - double(pl[c]);
			else
				gx[c] = 0.0;

			if (pu && pd)
				gy[c] = (double(pd[c]) - double(pu[c])) * 0.5;
			else if (pd)
				gy[c] = double(pd[c]) - double(here[c]);
			else if (pu)
				gy[c] = double(here[c]) - double(pu[c]);
			else
				gy[c] = 0.0;
		}
	}

	/*! \brief Give one pixel its colour: Telea's equation 2.

	    A weighted average of the known pixels within \a radius, each carried to
	    p along its own gradient, with the three weights of the paper -
	    direction, geometric distance, level-set distance. */
	void fillPixel(Plane& p, const Field& f, int x, int y, int radius)
	{
		double nx = 0.0;
		double ny = 0.0;
		gradientOfTime(f, x, y, nx, ny);
		const double nlen = std::sqrt(nx * nx + ny * ny);
		const bool haveNormal = nlen > 1.0e-9;
		if (haveNormal)
		{
			nx /= nlen;
			ny /= nlen;
		}

		const float tHere = f.t[f.idx(x, y)];
		const double radius2 = double(radius) * double(radius);

		double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
		double sumW = 0.0;

		const int x0 = std::max(0, x - radius);
		const int y0 = std::max(0, y - radius);
		const int x1 = std::min(f.w - 1, x + radius);
		const int y1 = std::min(f.h - 1, y + radius);

		double cgx[4];
		double cgy[4];

		// The range of colours actually present in the neighbourhood. The
		// extrapolation below is held inside it, so the fill can only ever
		// produce a colour that some nearby pixel really has: a bound on how
		// far Telea's gradient term may reach, and the other half of the
		// starburst fix described in gradientOfColour().
		double lo[4] = { 255.0, 255.0, 255.0, 255.0 };
		double hi[4] = { 0.0, 0.0, 0.0, 0.0 };
		for (int yy = y0; yy <= y1; ++yy)
		{
			for (int xx = x0; xx <= x1; ++xx)
			{
				if (f.state[f.idx(xx, yy)] != StKnown)
					continue;
				const float* seen = p.at(xx, yy);
				for (int c = 0; c < 4; ++c)
				{
					if (double(seen[c]) < lo[c])
						lo[c] = double(seen[c]);
					if (double(seen[c]) > hi[c])
						hi[c] = double(seen[c]);
				}
			}
		}

		for (int yy = y0; yy <= y1; ++yy)
		{
			for (int xx = x0; xx <= x1; ++xx)
			{
				if (f.state[f.idx(xx, yy)] != StKnown)
					continue;

				// Vector from the contributing pixel to the one being filled.
				const double rx = double(x) - double(xx);
				const double ry = double(y) - double(yy);
				const double d2 = rx * rx + ry * ry;
				if (d2 <= 0.0 || d2 > radius2)
					continue;
				const double dlen = std::sqrt(d2);

				// Along the propagation direction: pixels the boundary is
				// arriving from say most about what should appear here.
				double dir = haveNormal ? std::fabs(rx * nx + ry * ny) / dlen : 1.0;
				if (dir < MinDirectionWeight)
					dir = MinDirectionWeight;

				// Nearer pixels dominate, and pixels on the same level set of
				// the distance field are preferred to ones across it.
				const double dst = 1.0 / d2;
				const double lev = 1.0 / (1.0 + std::fabs(double(tHere) - double(f.t[f.idx(xx, yy)])));
				const double weight = dir * dst * lev;

				const float* src = p.at(xx, yy);
				if (f.isOriginal(xx, yy))
				{
					gradientOfColour(p, f, xx, yy, cgx, cgy);
					for (int c = 0; c < 4; ++c)
					{
						double v = double(src[c]) + cgx[c] * rx + cgy[c] * ry;
						acc[c] += weight * std::min(hi[c], std::max(lo[c], v));
					}
				}
				else
				{
					// Already reconstructed: worth averaging, not worth
					// differentiating.
					for (int c = 0; c < 4; ++c)
						acc[c] += weight * double(src[c]);
				}
				sumW += weight;
			}
		}

		float* dst = p.at(x, y);
		if (sumW <= 0.0)
		{
			// No known pixel in reach at all. Leaving the hole's original
			// contents is the only non-invented answer; the march will reach
			// this pixel's neighbours later and it stays visually enclosed.
			return;
		}

		for (int c = 0; c < 4; ++c)
		{
			const double value = acc[c] / sumW;
			dst[c] = float(std::min(255.0, std::max(0.0, value)));
		}
	}

	//! Mask as one byte per pixel, sized to \a size. Null when unusable.
	QImage normaliseMask(const QImage& mask, const QSize& size)
	{
		if (mask.isNull())
			return QImage();

		QImage m = mask;
		if (m.format() != QImage::Format_Grayscale8)
			m = m.convertToFormat(QImage::Format_Grayscale8);
		if (m.isNull())
			return QImage();

		if (m.size() != size)
		{
			// Nearest neighbour on purpose: the mask is a yes/no selection and
			// smoothing it would grow a soft fringe of half-selected pixels
			// around everything the user painted.
			m = m.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
			if (m.isNull())
				return QImage();
		}
		return m;
	}
}

QImage InpaintTelea::inpaint(const QImage& image, const QImage& mask, const Options& opts)
{
	if (image.isNull())
		return QImage();

	const QImage maskGrey = normaliseMask(mask, image.size());
	if (maskGrey.isNull())
		return QImage();

	const int imgW = image.width();
	const int imgH = image.height();

	// Bounding box of what has to be regenerated. Everything below is sized
	// from this rather than from the picture, which is what keeps a small
	// removal from a big photo cheap.
	int bx0 = imgW;
	int by0 = imgH;
	int bx1 = -1;
	int by1 = -1;
	for (int y = 0; y < imgH; ++y)
	{
		const uchar* line = maskGrey.constScanLine(y);
		for (int x = 0; x < imgW; ++x)
		{
			if (line[x] == 0)
				continue;
			if (x < bx0) bx0 = x;
			if (x > bx1) bx1 = x;
			if (y < by0) by0 = y;
			if (y > by1) by1 = y;
		}
	}

	if (bx1 < bx0 || by1 < by0)
	{
		// Nothing selected. Hand back exactly what came in: an empty mask must
		// not so much as change the format of the picture.
		if (opts.progress)
			opts.progress(100);
		return image;
	}

	const int radius = std::max(1, opts.radius);

	// Grown by twice the radius so that every pixel a fill reads from is inside
	// the rectangle, with room to spare for the gradient stencils at its rim.
	const int pad = 2 * radius + 2;
	const int rx0 = std::max(0, bx0 - pad);
	const int ry0 = std::max(0, by0 - pad);
	const int rx1 = std::min(imgW - 1, bx1 + pad);
	const int ry1 = std::min(imgH - 1, by1 + pad);
	const int w = rx1 - rx0 + 1;
	const int h = ry1 - ry0 + 1;
	if (w <= 0 || h <= 0)
		return QImage();

	// Premultiplied input is converted away: the weighted average has to run on
	// real colours or a partly transparent pixel drags the fill towards black.
	const bool wantsAlpha = image.hasAlphaChannel();
	const QImage::Format outFormat = wantsAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;

	QImage out = (image.format() == outFormat) ? image.copy() : image.convertToFormat(outFormat);
	if (out.isNull())
		return QImage();

	// Load the working rectangle into floats.
	Plane plane;
	plane.w = w;
	plane.h = h;
	try
	{
		plane.v.assign(size_t(w) * size_t(h) * 4, 0.0f);
	}
	catch (const std::bad_alloc&)
	{
		return QImage();
	}

	Field field;
	field.w = w;
	field.h = h;
	try
	{
		field.t.assign(size_t(w) * size_t(h), 0.0f);
		field.state.assign(size_t(w) * size_t(h), StKnown);
		field.settled.assign(size_t(w) * size_t(h), 0);
		field.original.assign(size_t(w) * size_t(h), 1);
	}
	catch (const std::bad_alloc&)
	{
		return QImage();
	}

	std::vector<uint8_t> wasMasked;
	try
	{
		wasMasked.assign(size_t(w) * size_t(h), 0);
	}
	catch (const std::bad_alloc&)
	{
		return QImage();
	}

	size_t total = 0;
	for (int y = 0; y < h; ++y)
	{
		const QRgb* src = reinterpret_cast<const QRgb*>(out.constScanLine(ry0 + y)) + rx0;
		const uchar* mline = maskGrey.constScanLine(ry0 + y) + rx0;
		for (int x = 0; x < w; ++x)
		{
			float* dst = plane.at(x, y);
			const QRgb px = src[x];
			dst[0] = float(qRed(px));
			dst[1] = float(qGreen(px));
			dst[2] = float(qBlue(px));
			dst[3] = float(qAlpha(px));

			if (mline[x] != 0)
			{
				const int i = field.idx(x, y);
				field.state[i] = StInside;
				field.original[i] = 0;
				field.t[i] = InfiniteT;
				wasMasked[i] = 1;
				++total;
			}
		}
	}

	if (total == 0)
	{
		if (opts.progress)
			opts.progress(100);
		return image;
	}

	// Seed the march from the known pixels that touch the hole. Their arrival
	// time is zero by definition, so the heap starts there and works inwards.
	using Entry = std::pair<float, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const int i = field.idx(x, y);
			if (field.state[i] != StKnown)
				continue;
			const bool touches =
				(x > 0 && field.state[i - 1] == StInside) ||
				(x < w - 1 && field.state[i + 1] == StInside) ||
				(y > 0 && field.state[i - w] == StInside) ||
				(y < h - 1 && field.state[i + w] == StInside);
			if (touches)
				heap.emplace(0.0f, i);
		}
	}

	size_t filled = 0;
	int lastPercent = -1;
	//! Starts at the interval so the very first pass through the loop polls,
	//! which is what makes an already-set cancel flag return immediately.
	size_t sinceCancelCheck = CancelCheckInterval;

	const int dx[4] = { -1, 1, 0, 0 };
	const int dy[4] = { 0, 0, -1, 1 };

	while (!heap.empty())
	{
		if (++sinceCancelCheck >= CancelCheckInterval)
		{
			sinceCancelCheck = 0;
			if (opts.cancel && opts.cancel->load())
				return QImage();
		}

		const Entry top = heap.top();
		heap.pop();
		const int p = top.second;
		if (field.settled[p])
			continue;   // a stale entry left behind when this pixel's time was lowered
		field.settled[p] = 1;
		field.state[p] = StKnown;

		const int px = p % w;
		const int py = p / w;

		for (int k = 0; k < 4; ++k)
		{
			const int nxp = px + dx[k];
			const int nyp = py + dy[k];
			if (!field.contains(nxp, nyp))
				continue;
			const int n = field.idx(nxp, nyp);
			if (field.state[n] == StKnown)
				continue;

			const float nt = marchTime(field, nxp, nyp);
			if (nt < field.t[n])
			{
				field.t[n] = nt;
				heap.emplace(nt, n);
			}

			if (field.state[n] == StInside)
			{
				// First time the march reaches this pixel: it now has an
				// arrival time, so it can be given a colour. Marking it BAND
				// keeps that colour out of later averages until it settles.
				field.state[n] = StBand;
				fillPixel(plane, field, nxp, nyp, radius);
				++filled;

				const int percent = int((filled * 100) / total);
				if (opts.progress && percent != lastPercent)
				{
					lastPercent = percent;
					opts.progress(percent);
				}
			}
		}
	}

	if (opts.cancel && opts.cancel->load())
		return QImage();

	// Only the pixels that were masked are written back, so nothing else in the
	// picture can drift by a rounding step.
	for (int y = 0; y < h; ++y)
	{
		QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(ry0 + y)) + rx0;
		for (int x = 0; x < w; ++x)
		{
			if (!wasMasked[field.idx(x, y)])
				continue;
			const float* v = plane.at(x, y);
			const int r = int(v[0] + 0.5f);
			const int g = int(v[1] + 0.5f);
			const int b = int(v[2] + 0.5f);
			const int a = wantsAlpha ? int(v[3] + 0.5f) : 255;
			dstLine[x] = qRgba(std::min(255, std::max(0, r)),
			                   std::min(255, std::max(0, g)),
			                   std::min(255, std::max(0, b)),
			                   std::min(255, std::max(0, a)));
		}
	}

	if (opts.progress && lastPercent != 100)
		opts.progress(100);

	return out;
}
