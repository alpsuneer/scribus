/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEFEATHER_H
#define SCIMAGEFEATHER_H

#include <QByteArray>
#include <QImage>
#include <QString>

#include "scribusapi.h"
#include "scimagestructs.h"

/*!
 \brief Non-destructive edge feather for placed images.

 Carried in the item's ScImageEffectList as an ImageEffect::EF_FEATHER entry
 whose effectParameters are "<amount in image pixels> <direction>", direction
 one of all/top/bottom/left/right. Like the eraser mask it is a data carrier:
 ScImage::applyEffect leaves it alone (on export ScImage is CMYK and qAlpha()
 is the black plate), and each renderer multiplies the mask it builds here
 into whatever alpha it has - the canvas pixmap, the PDF soft mask, the
 PostScript mask array.

 The mask depends on the image's own alpha:
  - a cut-out (anything transparent, e.g. after Remove Background): the alpha
    is eroded by half the amount and blurred by the amount, so the fade sits
    just inside the outline of the subject, not at the frame edge;
  - an opaque picture: a linear ramp from the chosen edge(s) inward, which is
    the frame-edge feather the tool always had.
 Amounts are in pixels of the full-resolution image; callers pass the scale
 of the pixel grid they hold (preview pixmap, resampled export) so the fade
 is the same width on every output.
 */
namespace ScFeatherMask
{
	//! Index of the feather entry in \a list, or -1.
	SCRIBUS_API int indexIn(const ScImageEffectList& list);
	//! Parse "<px> <direction>". False when \a list has no (usable) entry.
	SCRIBUS_API bool paramsOf(const ScImageEffectList& list, double& px, QString& direction);
	//! Store the feather in \a list (replace or append). px <= 0 removes it.
	SCRIBUS_API void set(ScImageEffectList& list, double px, const QString& direction);
	//! Drop the feather entry from \a list.
	SCRIBUS_API void removeFrom(ScImageEffectList& list);
	//! Encode the parameter string.
	SCRIBUS_API QString encode(double px, const QString& direction);

	//! True when \a alpha (w*h bytes) has any pixel that is not fully opaque.
	SCRIBUS_API bool hasTransparency(const QByteArray& alpha, int w, int h);

	/*! \brief Build the 8-bit mask (255 = keep) for an alpha plane.
	    \a alpha may be empty (opaque image). \a px is already scaled to this
	    pixel grid. */
	SCRIBUS_API QImage maskFor(const QByteArray& alpha, int w, int h, double px, const QString& direction);

	//! Multiply the feather into an ARGB image's alpha, in place.
	SCRIBUS_API void applyToImage(QImage& image, double px, const QString& direction);
	/*! \brief Multiply the feather into a w*h alpha byte array (grown to
	    opaque when empty), the PDF/PostScript soft-mask form. */
	SCRIBUS_API void mergeIntoAlphaBytes(QByteArray& alpha, int w, int h, double px, const QString& direction);
}

#endif
