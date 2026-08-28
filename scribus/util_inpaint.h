/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UTIL_INPAINT_H
#define UTIL_INPAINT_H

#include <atomic>
#include <functional>

#include <QImage>

/*!
 \brief Telea Fast Marching Method image inpainting.

 Regenerates the pixels a mask covers from the pixels around them, following
 Alexandru Telea, "An Image Inpainting Technique Based on the Fast Marching
 Method", Journal of Graphics Tools 9(1), 2004. Implemented from the paper;
 nothing is taken from another implementation.

 The method walks the masked region inwards from its boundary in order of
 distance to that boundary (the fast marching order), and gives each pixel a
 weighted average of the already-known pixels within a small radius, weighted
 by how well each one lies along the direction the boundary is travelling. It
 is a *smooth continuation* method: it reconstructs colour and gradient well,
 and texture not at all, so it suits removing a small object from a photo and
 does not suit filling a large hole in a patterned background.

 This header deliberately pulls in nothing from Scribus: the algorithm is
 standalone so it can be unit-tested without a document, a painter or a
 running application.
 */
namespace InpaintTelea
{
	//! Default radius of the neighbourhood each unknown pixel is averaged
	//! from, in pixels. Telea's own suggestion; large enough to bridge the
	//! usual retouching mask, small enough to stay local.
	constexpr int DefaultRadius = 5;

	//! Coverage above which a painted overlay counts as "remove this". The
	//! kernel itself treats any non-zero mask pixel as masked; this is the
	//! threshold callers use to turn a soft brush overlay into that binary
	//! mask, kept here so everything agrees on one value.
	constexpr int MaskThreshold = 32;

	struct Options
	{
		//! Neighbourhood radius in pixels. Clamped to at least 1.
		int radius = DefaultRadius;

		/*! \brief Called with 0..100 as the masked pixels are filled.

		    Called from whichever thread runs inpaint(), at most about once per
		    percent, and always once with 100 before a successful return. May
		    be empty. */
		std::function<void(int percent)> progress;

		/*! \brief Polled during the march; when it becomes true the run is
		    abandoned and inpaint() returns a null QImage. May be null. */
		std::atomic<bool>* cancel = nullptr;
	};

	/*! \brief Inpaint the masked part of \a image.

	    \param image the picture to repair. Format_RGB32 and Format_ARGB32 are
	           handled directly; anything else is converted to ARGB32 first and
	           the result comes back in that format. Premultiplied input is
	           un-premultiplied so the weighted average works on real colours.
	    \param mask coverage the same size as \a image (it is rescaled if not).
	           Every non-zero pixel is regenerated. Anything that is not
	           Format_Grayscale8 is converted to it first, so a mask carrying
	           its coverage in an alpha channel must be flattened to grey by
	           the caller (see MaskThreshold) rather than passed as-is.
	    \returns the repaired copy, or a null QImage if the run was cancelled
	             or the input was unusable.

	    An empty mask returns \a image itself, unchanged and bit-identical.

	    Work is confined to the mask's bounding box grown by the radius, so the
	    cost follows the size of what is being removed rather than the size of
	    the picture: a small removal from a large newspaper photo is quick.

	    Thread-safe: no static state, and \a image is not modified. */
	QImage inpaint(const QImage& image, const QImage& mask, const Options& opts = Options());
}

#endif
