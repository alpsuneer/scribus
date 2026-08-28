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
#include <limits>
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

	//! \name Method selection
	//@{
	/*! \brief Mask half-width, in pixels, at or below which the fast marching
	    method is used regardless of content.

	    A hole this thin has real pixels within a pixel or two of every point
	    inside it, so the averaging has no room to flatten anything and the
	    exemplar method would cost a great deal for no visible gain. Wires,
	    aerials, scratches and dust all land here. */
	constexpr double ThinMaskThickness = 4.0;

	/*! \brief Median ring gradient below which the fast marching method is
	    used whatever the size of the hole.

	    Sky, a studio backdrop, a painted wall: there is no texture to
	    reproduce, a diffusion is exactly right, and it is several times
	    cheaper. Measured on real photographs, this statistic reads about 0.5
	    in open sky and 4 to 6 in a crowd, so the threshold sits with a margin
	    on both sides. It is set above a smooth ramp as well - a steep gradient
	    with no texture in it, a studio backdrop or a clear sky at dusk, reads
	    as a constant 2-ish here and a diffusion reproduces it exactly, which
	    no amount of patch copying will. Erring towards the exemplar method
	    would be the worse mistake in the other direction too: run on flat sky
	    it *invents* texture that was never there. */
	constexpr double FlatRingBusyness = 3.0;

	//! How far out from the mask the flatness test looks.
	constexpr int RingWidth = 12;
	//@}

	//! \name Exemplar filling
	//@{
	//! Confidence never reaches exactly zero, so that priorities keep ordering
	//! the front sensibly even very deep inside a large hole.
	constexpr float MinConfidence = 1.0e-4f;

	//! Floor under Criminisi's data term. Without it a patch in a perfectly
	//! flat part of the front has priority zero and is never chosen.
	constexpr double MinDataTerm = 1.0e-3;

	//! Criminisi's alpha: the normalising constant for the data term, the
	//! maximum value a channel can take.
	constexpr double DataNormalisation = 255.0;

	/*! \brief How strongly a nearby source patch is preferred over a distant
	    one of the same quality.

	    Pure best-match search happily takes a patch from the far side of the
	    picture, which is how exemplar filling ends up pasting a face into a
	    hedge. Nearby patches are far more likely to belong to the same surface
	    under the same light. */
	constexpr double ProximityPenalty = 0.35;

	//! Search-window span per unit of coarse step: a window this wide is swept
	//! every other pixel, twice this wide every third, and so on up to a step
	//! of four. The winner is then refined at full resolution.
	constexpr int CoarseSearchThreshold = 40;

	/*! \brief How much an area of the picture is penalised for having already
	    been copied from.

	    Exemplar filling has a well known way of going wrong: it copies a patch,
	    that patch becomes the context the next match is measured against, so it
	    matches the same place again, and a recognisable thing gets stamped
	    across the hole several times over. Charging a source for each time it
	    has been used breaks the loop and makes the fill reach for its second
	    and third choices, which are usually just as good and not a copy of
	    something the eye has already seen. */
	constexpr double SourceUsagePenalty = 0.30;

	//! Fraction of the compared window that has to be real photograph before a
	//! candidate's score means anything. Without a floor, a candidate that
	//! happens to overlap the fill almost entirely could win on the three
	//! pixels it did match.
	constexpr double MinMatchCoverage = 0.6;
	//@}

	inline double clampd(double v, double lo, double hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	/*! \brief Working picture: one float per channel per pixel,
	    un-premultiplied, in R G B A order.

	    Kept as floats because in the fast marching method every filled pixel is
	    a weighted average that later pixels average again, and rounding to 8
	    bits at every step visibly bands a long fill. */
	struct Plane
	{
		int w {0};
		int h {0};
		std::vector<float> v;   //!< w * h * 4

		inline const float* at(int x, int y) const { return &v[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
		inline float* at(int x, int y) { return &v[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
		//! Rec. 601 luma, which is what the structure terms are computed on.
		inline double luma(int x, int y) const
		{
			const float* p = at(x, y);
			return 0.299 * double(p[0]) + 0.587 * double(p[1]) + 0.114 * double(p[2]);
		}
	};

	// ---------------------------------------------------------------- Telea

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

	/*! \brief Telea fast marching fill over \a plane.

	    \param masked one byte per pixel, non-zero where the hole is.
	    \returns false if the caller cancelled. */
	bool runFastMarching(Plane& plane, const std::vector<uint8_t>& masked, int radius,
	                     const Inpaint::Options& opts, size_t total, size_t& filled, int& lastPercent)
	{
		const int w = plane.w;
		const int h = plane.h;

		Field field;
		field.w = w;
		field.h = h;
		field.t.assign(size_t(w) * size_t(h), 0.0f);
		field.state.assign(size_t(w) * size_t(h), StKnown);
		field.settled.assign(size_t(w) * size_t(h), 0);
		field.original.assign(size_t(w) * size_t(h), 1);

		for (size_t i = 0; i < masked.size(); ++i)
		{
			if (!masked[i])
				continue;
			field.state[i] = StInside;
			field.original[i] = 0;
			field.t[i] = InfiniteT;
		}

		// Seed the march from the known pixels that touch the hole. Their
		// arrival time is zero by definition, so the heap starts there and
		// works inwards.
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
					return false;
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
					// arrival time, so it can be given a colour. Marking it
					// BAND keeps that colour out of later averages until it
					// settles.
					field.state[n] = StBand;
					fillPixel(plane, field, nxp, nyp, radius);
					++filled;

					const int percent = total ? int((filled * 100) / total) : 100;
					if (opts.progress && percent != lastPercent)
					{
						lastPercent = percent;
						opts.progress(percent);
					}
				}
			}
		}

		return !(opts.cancel && opts.cancel->load());
	}

	// ------------------------------------------------------------- Exemplar

	/*! \brief Criminisi exemplar filling.

	    Fills the hole one patch at a time, and the whole method is in the two
	    choices it makes each round:

	    - **which patch to fill next.** Priority is confidence times data term.
	      Confidence is how much of the patch is real photograph rather than
	      earlier guesswork, so the fill works inwards from what it is sure of.
	      The data term is how strongly an edge in the picture runs into the
	      hole at that point, so edges are continued *before* flat areas are
	      touched, and an edge arriving from one side meets the one arriving
	      from the other instead of both being smeared away first. This
	      ordering is the reason the method keeps structure at all.
	    - **what to fill it with.** The best matching patch of real photograph
	      elsewhere in the picture, copied in whole. Copied, never averaged -
	      which is the entire difference from the fast marching method, and the
	      reason a fill can come out with texture in it.

	    Pixels this fill has already invented are never used as source patches.
	    They take part in matching, weighted by their confidence, but the
	    colours that get copied are always real photograph, so a guess cannot be
	    laundered into evidence for the next guess. */
	struct Exemplar
	{
		Plane* plane {nullptr};
		int w {0};
		int h {0};
		//! Half-width of the block that gets copied.
		int patchR {4};
		/*! \brief Half-width of the block that gets *compared*, which is
		    deliberately larger.

		    Matching over exactly the piece being copied leaves a patch with no
		    distinguishing feature in it - a plain stretch of a repeating
		    pattern, say - free to match anywhere in that pattern, including
		    somewhere a whole half-period out of step, and the fill comes back
		    subtly but visibly misaligned. Comparing a wider ring of the
		    surroundings settles which alignment is actually meant, while
		    still copying a small block so that detail stays local. */
		int matchR {8};

		//! Has a colour: originally known, or filled by this run.
		std::vector<uint8_t> filled;
		//! Never masked. Only these may be *copied from*.
		std::vector<uint8_t> original;
		//! Criminisi's C(p): 1 for photograph, less for reconstruction.
		std::vector<float> conf;
		//! Which copied patch each pixel came from; -1 for real photograph.
		//! Only used afterwards, to find where two patches meet.
		std::vector<int32_t> source;
		//! Summed-area table over `original`, so "is this whole candidate
		//! window real photograph?" is one subtraction rather than a scan.
		std::vector<int32_t> integral;

		//! How often each coarse cell of the picture has been copied from; see
		//! SourceUsagePenalty. One cell per patch radius is fine - the point is
		//! to notice an area being mined repeatedly, not an exact position.
		std::vector<uint16_t> usage;
		int usageW {0};
		int usageH {0};

		//! The fill front, kept incrementally: rebuilding it from scratch each
		//! round is what makes naive implementations quadratic.
		std::vector<uint8_t> onFront;
		std::vector<float> prio;
		std::vector<int> frontList;
		size_t staleFront {0};

		inline int idx(int x, int y) const { return y * w + x; }
		inline bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
		inline bool isFilled(int x, int y) const { return contains(x, y) && filled[idx(x, y)] != 0; }

		inline int usageCell(int x, int y) const
		{
			const int cx = std::min(usageW - 1, std::max(0, x / patchR));
			const int cy = std::min(usageH - 1, std::max(0, y / patchR));
			return cy * usageW + cx;
		}

		void buildIntegral()
		{
			const int stride = w + 1;
			integral.assign(size_t(stride) * size_t(h + 1), 0);
			for (int y = 0; y < h; ++y)
			{
				int32_t rowSum = 0;
				for (int x = 0; x < w; ++x)
				{
					rowSum += original[idx(x, y)] ? 1 : 0;
					integral[size_t(y + 1) * stride + (x + 1)] =
						integral[size_t(y) * stride + (x + 1)] + rowSum;
				}
			}
		}

		//! True when every pixel of the closed rectangle is real photograph.
		bool windowAllOriginal(int x0, int y0, int x1, int y1) const
		{
			if (x0 < 0 || y0 < 0 || x1 >= w || y1 >= h)
				return false;
			const int stride = w + 1;
			const int32_t s =
				  integral[size_t(y1 + 1) * stride + (x1 + 1)]
				- integral[size_t(y0) * stride + (x1 + 1)]
				- integral[size_t(y1 + 1) * stride + x0]
				+ integral[size_t(y0) * stride + x0];
			return s == int32_t(x1 - x0 + 1) * int32_t(y1 - y0 + 1);
		}

		//! Criminisi's C(p): the mean confidence over the patch, counting the
		//! part still missing as zero.
		double confidenceAt(int x, int y) const
		{
			const int x0 = std::max(0, x - patchR);
			const int y0 = std::max(0, y - patchR);
			const int x1 = std::min(w - 1, x + patchR);
			const int y1 = std::min(h - 1, y + patchR);
			double sum = 0.0;
			int n = 0;
			for (int yy = y0; yy <= y1; ++yy)
			{
				for (int xx = x0; xx <= x1; ++xx)
				{
					const int i = idx(xx, yy);
					if (filled[i])
						sum += double(conf[i]);
					++n;
				}
			}
			return n ? sum / double(n) : 0.0;
		}

		/*! \brief Criminisi's D(p): how strongly an edge runs into the hole.

		    The isophote - the direction a line of constant brightness travels,
		    i.e. the image gradient turned through a right angle - dotted with
		    the normal of the fill front. Large where an edge meets the hole
		    head on, near zero where the front runs along an edge or where
		    there is no edge at all.

		    The gradient is taken over the patch and the strongest one kept,
		    rather than read at p itself: p is by definition an empty pixel on
		    the boundary, so a difference centred there usually has a hole on
		    one side and no gradient to report. Only filled pixels are ever
		    read - the hole still contains the object being removed, and
		    differentiating that would let it steer its own removal. */
		double dataTermAt(int x, int y) const
		{
			// Normal of the fill front: the gradient of "which pixels have a
			// colour". Outside the rectangle counts as filled, because beyond
			// it the picture really does continue.
			auto indicator = [this](int xx, int yy) -> double {
				if (!contains(xx, yy))
					return 1.0;
				return filled[idx(xx, yy)] ? 1.0 : 0.0;
			};
			double nx = (indicator(x + 1, y) - indicator(x - 1, y)) * 0.5;
			double ny = (indicator(x, y + 1) - indicator(x, y - 1)) * 0.5;
			const double nlen = std::sqrt(nx * nx + ny * ny);
			if (nlen < 1.0e-9)
				return 0.0;
			nx /= nlen;
			ny /= nlen;

			const int x0 = std::max(1, x - patchR);
			const int y0 = std::max(1, y - patchR);
			const int x1 = std::min(w - 2, x + patchR);
			const int y1 = std::min(h - 2, y + patchR);

			double bestMag = -1.0;
			double bestGx = 0.0;
			double bestGy = 0.0;
			for (int yy = y0; yy <= y1; ++yy)
			{
				for (int xx = x0; xx <= x1; ++xx)
				{
					if (!filled[idx(xx, yy)])
						continue;
					if (!isFilled(xx - 1, yy) || !isFilled(xx + 1, yy))
						continue;
					if (!isFilled(xx, yy - 1) || !isFilled(xx, yy + 1))
						continue;
					const double gx = (plane->luma(xx + 1, yy) - plane->luma(xx - 1, yy)) * 0.5;
					const double gy = (plane->luma(xx, yy + 1) - plane->luma(xx, yy - 1)) * 0.5;
					const double mag = gx * gx + gy * gy;
					if (mag > bestMag)
					{
						bestMag = mag;
						bestGx = gx;
						bestGy = gy;
					}
				}
			}
			if (bestMag <= 0.0)
				return 0.0;

			// Isophote: the gradient turned a right angle.
			const double isoX = -bestGy;
			const double isoY = bestGx;
			return std::fabs(isoX * nx + isoY * ny) / DataNormalisation;
		}

		//! Recompute front membership and priority over a rectangle. Called
		//! only on what a patch copy can have changed.
		void refresh(int x0, int y0, int x1, int y1)
		{
			x0 = std::max(0, x0);
			y0 = std::max(0, y0);
			x1 = std::min(w - 1, x1);
			y1 = std::min(h - 1, y1);
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const int i = idx(x, y);
					const bool wanted = !filled[i] &&
						(isFilled(x - 1, y) || isFilled(x + 1, y) ||
						 isFilled(x, y - 1) || isFilled(x, y + 1));
					if (wanted)
					{
						const double c = std::max(confidenceAt(x, y), double(MinConfidence));
						const double d = std::max(dataTermAt(x, y), MinDataTerm);
						prio[i] = float(c * d);
						if (!onFront[i])
						{
							onFront[i] = 1;
							frontList.push_back(i);
						}
					}
					else if (onFront[i])
					{
						onFront[i] = 0;
						++staleFront;
					}
				}
			}
		}

		/*! \brief The best matching patch of real photograph near \a px, \a py.

		    Sum of squared differences over the part of the target patch that
		    already has a colour, each pixel weighted by its confidence so that
		    this fill's own earlier guesses steer the match less than real
		    photograph does. Candidates must be *entirely* photograph, which the
		    summed-area table settles in constant time and which throws out most
		    of the window near a large hole immediately. */
		bool bestSource(int px, int py, int searchR, int& outX, int& outY,
		                std::vector<float>& scratch) const
		{
			// Target samples: dx, dy, weight, r, g, b - packed flat because
			// this is the innermost loop of the whole method.
			scratch.clear();
			double weightSum = 0.0;
			for (int dy = -matchR; dy <= matchR; ++dy)
			{
				for (int dx = -matchR; dx <= matchR; ++dx)
				{
					const int tx = px + dx;
					const int ty = py + dy;
					if (!contains(tx, ty))
						continue;
					const int i = idx(tx, ty);
					if (!filled[i])
						continue;
					const float* c = plane->at(tx, ty);
					const float wgt = std::max(conf[i], MinConfidence);
					scratch.push_back(float(dx));
					scratch.push_back(float(dy));
					scratch.push_back(wgt);
					scratch.push_back(c[0]);
					scratch.push_back(c[1]);
					scratch.push_back(c[2]);
					weightSum += double(wgt);
				}
			}
			if (weightSum <= 0.0 || scratch.empty())
				return false;

			const size_t sampleCount = scratch.size() / 6;
			const float* samples = scratch.data();

			double best = std::numeric_limits<double>::max();
			int bestX = -1;
			int bestY = -1;

			// A candidate must be able to supply the whole block that will be
			// copied, and the wider window it is compared over must at least
			// lie inside the picture.
			const double minCoverage = MinMatchCoverage * weightSum;
			auto consider = [&](int qx, int qy) {
				if (!windowAllOriginal(qx - patchR, qy - patchR, qx + patchR, qy + patchR))
					return;
				if (qx - matchR < 0 || qy - matchR < 0 || qx + matchR >= w || qy + matchR >= h)
					return;
				const double ddx = double(qx - px);
				const double ddy = double(qy - py);
				const double prox = (1.0 + ProximityPenalty *
					std::sqrt(ddx * ddx + ddy * ddy) / double(searchR)) *
					(1.0 + SourceUsagePenalty * double(usage[usageCell(qx, qy)]));
				// Everything still to be added is non-negative and the divisor
				// can only shrink, so a running sum past this bound can never
				// come back under the best score. Safe to give up on.
				const double cutoff = (best >= std::numeric_limits<double>::max() / 2.0)
					? std::numeric_limits<double>::max()
					: best * weightSum / prox;
				double ssd = 0.0;
				double used = 0.0;
				for (size_t s = 0; s < sampleCount; ++s)
				{
					const float* smp = samples + s * 6;
					const int cx = qx + int(smp[0]);
					const int cy = qy + int(smp[1]);
					// Outside the copied block the candidate may overlap
					// ground this fill has already touched. Those pixels are
					// simply not evidence, so they are skipped rather than
					// disqualifying an otherwise good near neighbour - which
					// is what excluding them wholesale did, pushing the fill
					// out to distant patches of the wrong material.
					if (!original[idx(cx, cy)])
						continue;
					const float* c = plane->at(cx, cy);
					const double dr = double(smp[3]) - double(c[0]);
					const double dg = double(smp[4]) - double(c[1]);
					const double db = double(smp[5]) - double(c[2]);
					ssd += double(smp[2]) * (dr * dr + dg * dg + db * db);
					used += double(smp[2]);
					if (ssd >= cutoff)
						return;
				}
				// Judged on too little to be worth trusting.
				if (used < minCoverage)
					return;
				const double score = (ssd / used) * prox;
				if (score < best)
				{
					best = score;
					bestX = qx;
					bestY = qy;
				}
			};

			/* A wide window is swept coarsely and the winner then refined at
			   full resolution.

			   The step grows with the window, because the window grows with the
			   hole and the cost of sweeping it grows as the square. A big
			   removal from a newspaper-sized photograph is the case that
			   matters: at a fixed step of two it took half a minute, and almost
			   all of that was spent distinguishing between candidates one pixel
			   apart, which the refinement pass settles anyway. */
			const int span = 2 * searchR + 1;
			const int step = (span > CoarseSearchThreshold)
				? int(clampd(double(span) / double(CoarseSearchThreshold), 2.0, 4.0))
				: 1;
			for (int qy = py - searchR; qy <= py + searchR; qy += step)
				for (int qx = px - searchR; qx <= px + searchR; qx += step)
					consider(qx, qy);

			if (step > 1 && bestX >= 0)
			{
				// Everything the coarse sweep stepped over, around its winner.
				const int cx = bestX;
				const int cy = bestY;
				const int fine = step - 1;
				for (int qy = cy - fine; qy <= cy + fine; ++qy)
					for (int qx = cx - fine; qx <= cx + fine; ++qx)
						consider(qx, qy);
			}

			if (bestX < 0)
				return false;
			outX = bestX;
			outY = bestY;
			return true;
		}

		//! Copy the missing part of the patch at \a px,\a py out of the patch
		//! at \a qx,\a qy. Returns how many pixels were filled.
		int copyPatch(int px, int py, int qx, int qy, float newConf, int32_t patchId)
		{
			int n = 0;
			for (int dy = -patchR; dy <= patchR; ++dy)
			{
				for (int dx = -patchR; dx <= patchR; ++dx)
				{
					const int tx = px + dx;
					const int ty = py + dy;
					if (!contains(tx, ty))
						continue;
					const int i = idx(tx, ty);
					if (filled[i])
						continue;
					const float* s = plane->at(qx + dx, qy + dy);
					float* d = plane->at(tx, ty);
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = s[3];
					filled[i] = 1;
					conf[i] = newConf;
					source[i] = patchId;
					++n;
				}
			}
			if (n > 0)
				++usage[usageCell(qx, qy)];
			return n;
		}

		/*! \brief Soften the joins between patches, and nothing else.

		    Patches are copied whole and butt up against each other, so where
		    two of them meet there is a step that belongs to neither piece of
		    photograph - the blockiness that gives an exemplar fill away. Only
		    pixels that actually sit on such a join are touched, and only once,
		    so the texture inside each patch is left exactly as it was copied.
		    Blurring the whole fill instead would undo the entire point of
		    copying patches. */
		void softenSeams()
		{
			std::vector<int> seam;
			for (int y = 0; y < h; ++y)
			{
				for (int x = 0; x < w; ++x)
				{
					const int i = idx(x, y);
					if (source[i] < 0)
						continue;
					bool join = false;
					if (x > 0 && source[i - 1] != source[i]) join = true;
					if (!join && x < w - 1 && source[i + 1] != source[i]) join = true;
					if (!join && y > 0 && source[i - w] != source[i]) join = true;
					if (!join && y < h - 1 && source[i + w] != source[i]) join = true;
					if (join)
						seam.push_back(i);
				}
			}

			// Gathered first, applied second, so a softened pixel does not
			// become the input to its neighbour and spread the softening.
			std::vector<float> updated(seam.size() * 4);
			for (size_t k = 0; k < seam.size(); ++k)
			{
				const int i = seam[k];
				const int x = i % w;
				const int y = i / w;
				double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
				int n = 0;
				const int dx[4] = { -1, 1, 0, 0 };
				const int dy[4] = { 0, 0, -1, 1 };
				for (int q = 0; q < 4; ++q)
				{
					const int nx = x + dx[q];
					const int ny = y + dy[q];
					if (!contains(nx, ny) || !filled[idx(nx, ny)])
						continue;
					const float* c = plane->at(nx, ny);
					for (int ch = 0; ch < 4; ++ch)
						acc[ch] += double(c[ch]);
					++n;
				}
				const float* self = plane->at(x, y);
				for (int ch = 0; ch < 4; ++ch)
				{
					updated[k * 4 + ch] = n
						? float(0.6 * double(self[ch]) + 0.4 * (acc[ch] / double(n)))
						: self[ch];
				}
			}
			for (size_t k = 0; k < seam.size(); ++k)
			{
				float* d = plane->at(seam[k] % w, seam[k] / w);
				for (int ch = 0; ch < 4; ++ch)
					d[ch] = updated[k * 4 + ch];
			}
		}

		/*! \brief Last resort when the window holds no complete patch of
		    photograph at all, which happens deep inside a very large hole.

		    A plain distance-weighted average of what is around it. This is the
		    fast marching method's behaviour and it will look like it, but a
		    soft patch is better than a hole, and by this depth there is no
		    real information left to preserve anyway. */
		int averagePatch(int px, int py, float newConf)
		{
			int n = 0;
			for (int dy = -patchR; dy <= patchR; ++dy)
			{
				for (int dx = -patchR; dx <= patchR; ++dx)
				{
					const int tx = px + dx;
					const int ty = py + dy;
					if (!contains(tx, ty))
						continue;
					const int i = idx(tx, ty);
					if (filled[i])
						continue;

					double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
					double sw = 0.0;
					for (int yy = std::max(0, ty - patchR); yy <= std::min(h - 1, ty + patchR); ++yy)
					{
						for (int xx = std::max(0, tx - patchR); xx <= std::min(w - 1, tx + patchR); ++xx)
						{
							const int j = idx(xx, yy);
							if (!filled[j])
								continue;
							const double rx = double(tx - xx);
							const double ry = double(ty - yy);
							const double d2 = rx * rx + ry * ry;
							if (d2 <= 0.0)
								continue;
							const double wgt = double(std::max(conf[j], MinConfidence)) / d2;
							const float* c = plane->at(xx, yy);
							for (int k = 0; k < 4; ++k)
								acc[k] += wgt * double(c[k]);
							sw += wgt;
						}
					}
					if (sw <= 0.0)
						continue;
					float* d = plane->at(tx, ty);
					for (int k = 0; k < 4; ++k)
						d[k] = float(clampd(acc[k] / sw, 0.0, 255.0));
					filled[i] = 1;
					conf[i] = newConf;
					++n;
				}
			}
			return n;
		}
	};

	/*! \brief Criminisi exemplar fill over \a plane.
	    \returns false if the caller cancelled. */
	bool runExemplar(Plane& plane, const std::vector<uint8_t>& masked, int patchR, int searchR,
	                 const Inpaint::Options& opts, size_t total, size_t& filledCount, int& lastPercent)
	{
		const int w = plane.w;
		const int h = plane.h;
		const size_t n = size_t(w) * size_t(h);

		Exemplar ex;
		ex.plane = &plane;
		ex.w = w;
		ex.h = h;
		ex.patchR = patchR;
		// Three pixels of extra context all round. Measured on a regular grid
		// and a crowd photograph together: matching over exactly the copied
		// block loses the phase of a repeating pattern completely (56% of the
		// grid came back in step, worse than guessing), while widening it
		// further than this starts costing time without buying anything.
		ex.matchR = patchR + 3;
		ex.filled.assign(n, 1);
		ex.original.assign(n, 1);
		ex.conf.assign(n, 1.0f);
		ex.source.assign(n, -1);
		ex.onFront.assign(n, 0);
		ex.prio.assign(n, 0.0f);
		ex.usageW = (w + patchR - 1) / patchR;
		ex.usageH = (h + patchR - 1) / patchR;
		ex.usage.assign(size_t(ex.usageW) * size_t(ex.usageH), 0);

		for (size_t i = 0; i < masked.size(); ++i)
		{
			if (!masked[i])
				continue;
			ex.filled[i] = 0;
			ex.original[i] = 0;
			ex.conf[i] = 0.0f;
		}

		ex.buildIntegral();
		ex.refresh(0, 0, w - 1, h - 1);

		std::vector<float> scratch;
		scratch.reserve(size_t((2 * ex.matchR + 1) * (2 * ex.matchR + 1)) * 6);
		int32_t patchId = 0;

		while (filledCount < total)
		{
			if (opts.cancel && opts.cancel->load())
				return false;

			// Stale entries pile up as the front moves; sweeping them out when
			// they are the majority keeps this scan proportional to the real
			// front rather than to everything the front has ever been.
			if (ex.staleFront > ex.frontList.size() / 2 && ex.staleFront > 64)
			{
				std::vector<int> live;
				live.reserve(ex.frontList.size() - ex.staleFront + 16);
				for (int i : ex.frontList)
					if (ex.onFront[i])
						live.push_back(i);
				ex.frontList.swap(live);
				ex.staleFront = 0;
			}

			int best = -1;
			float bestPrio = -1.0f;
			for (int i : ex.frontList)
			{
				if (!ex.onFront[i])
					continue;
				if (ex.prio[i] > bestPrio)
				{
					bestPrio = ex.prio[i];
					best = i;
				}
			}
			if (best < 0)
				break;   // nothing reachable is still empty

			const int px = best % w;
			const int py = best / w;
			const float newConf = float(std::max(ex.confidenceAt(px, py), double(MinConfidence)));

			int filledHere = 0;
			int qx = 0;
			int qy = 0;
			if (ex.bestSource(px, py, searchR, qx, qy, scratch))
				filledHere = ex.copyPatch(px, py, qx, qy, newConf, patchId++);
			if (filledHere == 0)
				filledHere = ex.averagePatch(px, py, MinConfidence);
			if (filledHere == 0)
			{
				// Nothing worked for this pixel at all. Drop it off the front
				// so the loop cannot spin on it.
				ex.onFront[best] = 0;
				++ex.staleFront;
				continue;
			}

			filledCount += size_t(filledHere);

			const int reach = 2 * patchR + 2;
			ex.refresh(px - reach, py - reach, px + reach, py + reach);

			const int percent = total ? int((filledCount * 100) / total) : 100;
			if (opts.progress && percent != lastPercent)
			{
				lastPercent = percent;
				opts.progress(percent);
			}
		}

		ex.softenSeams();
		return true;
	}

	// ------------------------------------------------------- Mask analysis

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

	/*! \brief Chamfer distance from every pixel of \a region to the nearest
	    seed, in pixels.

	    Two passes with 1 / sqrt(2) steps. Approximate, and much cheaper than an
	    exact transform - both things it feeds only need to be roughly right.
	    \param seed one byte per pixel over the region, non-zero where distance
	           is zero. */
	std::vector<float> chamfer(const std::vector<uint8_t>& seed, int w, int h)
	{
		constexpr float Big = 1.0e9f;
		constexpr float Diag = 1.41421356f;
		std::vector<float> d(size_t(w) * size_t(h));
		for (size_t i = 0; i < d.size(); ++i)
			d[i] = seed[i] ? 0.0f : Big;

		auto relax = [](float& v, float other, float cost) {
			if (other + cost < v)
				v = other + cost;
		};

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				float& v = d[size_t(y) * w + x];
				if (v == 0.0f)
					continue;
				if (y > 0)
				{
					relax(v, d[size_t(y - 1) * w + x], 1.0f);
					if (x > 0)
						relax(v, d[size_t(y - 1) * w + (x - 1)], Diag);
					if (x < w - 1)
						relax(v, d[size_t(y - 1) * w + (x + 1)], Diag);
				}
				if (x > 0)
					relax(v, d[size_t(y) * w + (x - 1)], 1.0f);
			}
		}
		for (int y = h - 1; y >= 0; --y)
		{
			for (int x = w - 1; x >= 0; --x)
			{
				float& v = d[size_t(y) * w + x];
				if (v == 0.0f)
					continue;
				if (y < h - 1)
				{
					relax(v, d[size_t(y + 1) * w + x], 1.0f);
					if (x > 0)
						relax(v, d[size_t(y + 1) * w + (x - 1)], Diag);
					if (x < w - 1)
						relax(v, d[size_t(y + 1) * w + (x + 1)], Diag);
				}
				if (x < w - 1)
					relax(v, d[size_t(y) * w + (x + 1)], 1.0f);
			}
		}
		return d;
	}

	//! What the two analyses below need to know about a mask.
	struct MaskShape
	{
		//! How far the deepest point of the hole is from real pixels. This,
		//! rather than the area, is what says whether a diffusion can cope: a
		//! long scratch has a large area and a thickness of one, and the fast
		//! marching method handles it perfectly.
		double thickness {0.0};

		/*! \brief Median gradient magnitude of the real picture in a narrow
		    ring around the hole - how busy its actual surroundings are.

		    The median rather than the mean, and a ring rather than the padded
		    bounding box, because both of the obvious versions get the answer
		    wrong in the same direction. A tall mask's bounding box is mostly
		    far-away picture, and a mean is dragged upwards by a handful of
		    strong edges, so a hole sitting in open sky next to one bright kite
		    reads as "busy" and gets an expensive treatment that makes it worse.
		    Measured on real photographs: open sky sits at about 0.5, a crowd at
		    4 to 6. */
		double busyness {0.0};
	};

	MaskShape analyseMask(const QImage& image, const QImage& maskGrey, const QRect& bbox)
	{
		MaskShape shape;

		// Work over the bounding box grown by the ring width, so both the
		// distance into the hole and the ring outside it fit.
		const QRect r = bbox.adjusted(-RingWidth - 1, -RingWidth - 1, RingWidth + 1, RingWidth + 1)
		                    .intersected(QRect(QPoint(0, 0), image.size()));
		const int w = r.width();
		const int h = r.height();
		if (w <= 2 || h <= 2)
			return shape;

		std::vector<uint8_t> maskedSeed(size_t(w) * size_t(h), 0);
		std::vector<uint8_t> clearSeed(size_t(w) * size_t(h), 0);
		for (int y = 0; y < h; ++y)
		{
			const uchar* line = maskGrey.constScanLine(r.top() + y) + r.left();
			for (int x = 0; x < w; ++x)
			{
				const bool m = line[x] != 0;
				maskedSeed[size_t(y) * w + x] = m ? 1 : 0;
				clearSeed[size_t(y) * w + x] = m ? 0 : 1;
			}
		}

		// Distance from inside the hole out to real pixels.
		const std::vector<float> toClear = chamfer(clearSeed, w, h);
		for (size_t i = 0; i < toClear.size(); ++i)
		{
			if (maskedSeed[i] && toClear[i] < 1.0e9f && double(toClear[i]) > shape.thickness)
				shape.thickness = double(toClear[i]);
		}

		// Distance from real pixels in to the hole, which picks out the ring.
		const std::vector<float> toMask = chamfer(maskedSeed, w, h);

		auto luma = [&](int x, int y) {
			const QRgb p = image.pixel(r.left() + x, r.top() + y);
			return 0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p);
		};

		std::vector<float> ring;
		ring.reserve(size_t(w) * 8);
		for (int y = 1; y < h - 1; ++y)
		{
			for (int x = 1; x < w - 1; ++x)
			{
				const size_t i = size_t(y) * w + x;
				if (maskedSeed[i])
					continue;
				const double dist = double(toMask[i]);
				// Skip the pixel right against the mask: its difference
				// stencil would reach into the hole and measure the edge of
				// the object being removed rather than the surroundings.
				if (dist < 2.0 || dist > double(RingWidth))
					continue;
				if (maskedSeed[i - 1] || maskedSeed[i + 1] ||
				    maskedSeed[i - w] || maskedSeed[i + w])
					continue;
				const double gx = (luma(x + 1, y) - luma(x - 1, y)) * 0.5;
				const double gy = (luma(x, y + 1) - luma(x, y - 1)) * 0.5;
				ring.push_back(float(std::sqrt(gx * gx + gy * gy)));
			}
		}

		if (!ring.empty())
		{
			const size_t mid = ring.size() / 2;
			std::nth_element(ring.begin(), ring.begin() + mid, ring.end());
			shape.busyness = double(ring[mid]);
		}
		return shape;
	}
}

QImage Inpaint::inpaint(const QImage& image, const QImage& mask, const Options& opts)
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

	const QRect bbox(QPoint(bx0, by0), QPoint(bx1, by1));
	const int radius = std::max(1, opts.radius);

	// Which method, and how much real picture the chosen one needs around the
	// hole to work with.
	Method method = opts.method;
	int patchR = 4;
	int searchR = 64;
	if (method == Method::Auto || method == Method::Exemplar)
	{
		const MaskShape shape = analyseMask(image, maskGrey, bbox);
		if (method == Method::Auto)
		{
			// Thin enough that a diffusion cannot flatten anything, or sitting
			// in flat colour where there is no texture to reproduce: the fast
			// marching method is both right and far cheaper. Otherwise the
			// hole is wide and its surroundings have detail in them, which is
			// the case a diffusion turns to mud.
			method = (shape.thickness <= ThinMaskThickness || shape.busyness < FlatRingBusyness)
				? Method::FastMarching
				: Method::Exemplar;
		}
		if (method == Method::Exemplar)
		{
			// A patch wants to be a little larger than the texture it is
			// reproducing, and larger again for a thick hole because structure
			// has further to travel across it. Searching further than that
			// buys nothing and costs a great deal.
			patchR = int(clampd(3.0 + shape.thickness / 20.0, 3.0, 7.0));
			searchR = int(clampd(8.0 * patchR + shape.thickness, 48.0, 200.0));
		}
	}
	if (opts.chosenMethod)
		*opts.chosenMethod = method;

	// The fast marching method only ever reads within its radius of the hole.
	// The exemplar method has to have somewhere to copy from, so it needs a
	// whole search window of real picture around it.
	const int pad = (method == Method::Exemplar)
		? (searchR + patchR + 6)   // + 4 for the wider match window, + 2 slack
		: (2 * radius + 2);

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

	Plane plane;
	plane.w = w;
	plane.h = h;
	std::vector<uint8_t> masked;
	try
	{
		plane.v.assign(size_t(w) * size_t(h) * 4, 0.0f);
		masked.assign(size_t(w) * size_t(h), 0);
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
				masked[size_t(y) * w + x] = 1;
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

	size_t filledCount = 0;
	int lastPercent = -1;
	bool ok = false;
	try
	{
		ok = (method == Method::Exemplar)
			? runExemplar(plane, masked, patchR, searchR, opts, total, filledCount, lastPercent)
			: runFastMarching(plane, masked, radius, opts, total, filledCount, lastPercent);
	}
	catch (const std::bad_alloc&)
	{
		return QImage();
	}
	if (!ok)
		return QImage();

	// Only the pixels that were masked are written back, so nothing else in the
	// picture can drift by a rounding step.
	for (int y = 0; y < h; ++y)
	{
		QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(ry0 + y)) + rx0;
		for (int x = 0; x < w; ++x)
		{
			if (!masked[size_t(y) * w + x])
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
