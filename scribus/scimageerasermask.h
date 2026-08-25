/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEERASERMASK_H
#define SCIMAGEERASERMASK_H

#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>

#include "scribusapi.h"
#include "scimagestructs.h"

/*!
 \brief Non-destructive per-pixel eraser mask for placed images.

 The mask is an 8-bit greyscale image: 255 keeps the pixel, 0 erases it fully,
 values in between are partial (soft brush edges). It is carried in the item's
 ScImageEffectList as an ImageEffect::EF_ERASERMASK entry whose
 effectParameters hold a base64 PNG, which is what buys .sla persistence,
 copy/paste and undo without touching the file format plugins.

 The effect is a *data carrier only*. ScImage::applyEffect must never act on
 it: on the export paths ScImage is CMYK, where qAlpha() is the black plate
 rather than an alpha channel, so writing alpha there would knock holes in the
 K separation of every printed page. Each renderer composites the mask itself
 (alpha on canvas, a soft mask on the PDF and PostScript paths).
 */
namespace ScEraserMask
{
	//! Longest edge a stored mask may have; larger images get a scaled mask.
	//! Keeps the base64 payload in the .sla to a sane size for newspaper photos.
	constexpr int MaxEdge = 2048;

	//! Mask value meaning "pixel is untouched".
	constexpr int Keep = 255;

	//! \name Parameter codec
	//@{
	//! Serialise to the "1 <base64 png>" form stored in effectParameters.
	SCRIBUS_API QString encode(const QImage& mask);
	//! Inverse of encode(). Returns a null QImage when params are absent or bad.
	SCRIBUS_API QImage decode(const QString& params);
	//! Short stable digest of params, for use in image cache keys. The raw
	//! base64 must never reach a cache key: it is hundreds of KB per stroke.
	SCRIBUS_API QString digest(const QString& params);
	//@}

	//! \name Effect list access
	//@{
	//! Index of the eraser entry in \a list, or -1.
	SCRIBUS_API int indexIn(const ScImageEffectList& list);
	//! Decoded mask carried by \a list, or a null QImage.
	SCRIBUS_API QImage maskOf(const ScImageEffectList& list);
	//! Raw parameter string carried by \a list, or an empty string.
	SCRIBUS_API QString paramsOf(const ScImageEffectList& list);
	//! Store \a mask in \a list, replacing or appending as needed. A null or
	//! fully-opaque mask removes the entry instead, so an undone erase leaves
	//! no trace in the saved document.
	SCRIBUS_API void setMask(ScImageEffectList& list, const QImage& mask);
	//! Drop the eraser entry from \a list.
	SCRIBUS_API void removeFrom(ScImageEffectList& list);
	//! True when \a mask is null or leaves every pixel untouched.
	SCRIBUS_API bool isEmptyMask(const QImage& mask);
	//@}

	//! \name Construction
	//@{
	//! A fully-opaque mask sized for an image of \a imageW x \a imageH,
	//! honouring MaxEdge.
	SCRIBUS_API QImage createFor(int imageW, int imageH);
	//@}

	//! \name Painting
	//@{
	/*! \brief Stamp one soft brush dab into a coverage buffer.
	    \param coverage 8-bit buffer accumulating this stroke, 0 = untouched.
	    \param centre   dab centre in coverage-buffer pixels.
	    \param radius   dab radius in coverage-buffer pixels.
	    \param hardness 0.0 fully feathered .. 1.0 hard edged.
	    Accumulates with max(), so overlapping dabs inside one stroke do not
	    pile up into a darker core the way repeated alpha blending would.
	    \returns the touched rectangle. */
	SCRIBUS_API QRect stamp(QImage& coverage, const QPointF& centre, double radius, double hardness);

	/*! \brief Stamp a line of dabs from \a a to \a b, spaced for a continuous
	    stroke.

	    \param carry distance already travelled since the last dab, carried
	    across calls and updated in place. Without it the dab phase would reset
	    at every mouse-move event, so the same gesture would come out slightly
	    different depending on how many events the pointer happened to generate.
	    Reset it to 0 when a stroke begins. */
	SCRIBUS_API QRect stampLine(QImage& coverage, const QPointF& a, const QPointF& b, double radius, double hardness, double& carry);

	/*! \brief Rebuild \a out as \a base combined with the stroke's \a coverage.

	    Always recomputed from \a base rather than folded into \a out
	    cumulatively. Coverage accumulates across the whole stroke with max(),
	    so a pixel touched by twenty dabs must end up exactly as erased as one
	    touched by a single dab of the same strength; applying the difference
	    once per mouse-move event instead would let overlapping feathers stack
	    up and leave a scalloped edge at every event boundary.

	    \param restore true un-erases instead of erasing.
	    Restricted to \a region when it is valid. */
	SCRIBUS_API void applyStroke(QImage& out, const QImage& base, const QImage& coverage, bool restore, const QRect& region = QRect());
	//@}

	//! \name Applying
	//@{
	//! Multiply \a mask into the alpha channel of \a image, scaling the mask to
	//! the image. \a image is converted to ARGB32 if it carries no alpha.
	SCRIBUS_API void applyToAlpha(QImage& image, const QImage& mask);

	/*! \brief Mask resampled to \a w x \a h as one byte per pixel, row-major.
	    This is the shape both the PostScript writer (interleaved with CMYK)
	    and the PDF SMask stream want. */
	SCRIBUS_API QByteArray toAlphaBytes(const QImage& mask, int w, int h);

	/*! \brief Multiply \a mask into an existing \a w x \a h alpha byte array.
	    Used where a renderer already loaded the file's own alpha and the
	    erasure has to combine with it rather than replace it. Grows \a alpha
	    from an opaque default when it is empty. */
	SCRIBUS_API void mergeIntoAlphaBytes(QByteArray& alpha, const QImage& mask, int w, int h);

	/*! \brief Merge \a mask into a PDF 1-bit /ImageMask stencil.

	    The PDF versions that cannot carry transparency (1.3, PDF/X-1a and
	    PDF/X-3 - which is what a press-bound newspaper export normally uses)
	    get a 1-bit stencil instead of an 8-bit soft mask, so the erasure still
	    happens but a feathered edge quantises to a hard one. Rows are packed
	    MSB-first and padded to whole bytes, and the sense is inverted the way
	    ScImage::getAlpha writes it for PDF: bit 1 means "masked out". */
	SCRIBUS_API void mergeIntoPdfImageMask(QByteArray& stencil, const QImage& mask, int w, int h);
	//@}
}

#endif
