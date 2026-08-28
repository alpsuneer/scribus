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
 \brief Regenerate the pixels a mask covers from the rest of the picture.

 Two methods, because one method cannot do both jobs:

 - **Fast marching** (Alexandru Telea, "An Image Inpainting Technique Based on
   the Fast Marching Method", Journal of Graphics Tools 9(1), 2004). Walks
   inwards from the mask boundary in order of distance and gives each pixel a
   weighted average of the known pixels near it. It is a diffusion: it
   reconstructs colour and gradient, and it cannot reconstruct texture, because
   an average of pixels has less detail than the pixels it averaged. Ideal for
   something thin - a wire, an aerial, a scratch, a speck of dust - where every
   filled pixel is a pixel or two from real data and there is no room for the
   averaging to flatten anything. Cheap.

 - **Exemplar** (Antonio Criminisi, Patrick Perez, Kentaro Toyama, "Region
   Filling and Object Removal by Exemplar-Based Image Inpainting", IEEE Trans.
   Image Processing 13(9), 2004). Fills the hole a patch at a time by *copying*
   the best-matching patch from elsewhere in the same picture, choosing which
   patch to fill next so that edges arriving at the hole are continued before
   flat areas are touched. Because it copies real pixels it produces real
   texture, and because it never averages two things together it cannot make
   the muddy brown that a diffusion makes of a person standing against grass.
   What a wide hole needs; slower.

 Both are implemented here from the papers, with no external dependencies and
 no Scribus headers, so the algorithms can be tested without a document, a
 painter or a running application.

 Method::Auto picks between them from the shape of the mask and the busyness of
 what surrounds it. See inpaint().
 */
namespace Inpaint
{
	//! Default neighbourhood radius, in pixels. Telea's own suggestion, and
	//! used only by the fast marching method: the exemplar method sizes its
	//! patches and its search from the mask itself, because the right patch
	//! size is a property of the hole and of the texture around it rather
	//! than something a caller is in a position to know.
	constexpr int DefaultRadius = 5;

	//! Coverage above which a painted overlay counts as "remove this". The
	//! kernel itself treats any non-zero mask pixel as masked; this is the
	//! threshold callers use to turn a soft brush overlay into that binary
	//! mask, kept here so everything agrees on one value.
	constexpr int MaskThreshold = 32;

	enum class Method
	{
		//! Choose per mask. Anything thin, or anything surrounded by flat
		//! colour, goes to the fast marching method because it is much cheaper
		//! and loses nothing there; everything else goes to the exemplar
		//! method. This is what callers should use.
		Auto,
		//! Force Telea fast marching. Cheap, smooth, no texture.
		FastMarching,
		//! Force Criminisi exemplar filling. Slower, keeps texture and edges.
		Exemplar
	};

	struct Options
	{
		//! Neighbourhood radius in pixels, for the fast marching method.
		//! Clamped to at least 1. See DefaultRadius.
		int radius = DefaultRadius;

		//! Which method to use; see Method.
		Method method = Method::Auto;

		/*! \brief Called with 0..100 as the masked pixels are filled.

		    Called from whichever thread runs inpaint(), at most about once per
		    percent, and always once with 100 before a successful return. May
		    be empty. */
		std::function<void(int percent)> progress;

		/*! \brief Polled during the fill; when it becomes true the run is
		    abandoned and inpaint() returns a null QImage. May be null. */
		std::atomic<bool>* cancel = nullptr;

		//! Set to the method that actually ran. Useful to tests and to anyone
		//! wondering why a particular removal was slow.
		Method* chosenMethod = nullptr;
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
