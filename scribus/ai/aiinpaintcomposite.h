/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AIINPAINTCOMPOSITE_H
#define AIINPAINTCOMPOSITE_H

#include <QBuffer>
#include <QByteArray>
#include <QImage>

/*!
 \brief Turning a picture and a mask into the one image a vision model can be
 given.

 The hosted image models do not take a mask channel, so the mask has to be
 painted onto the picture and described in the prompt (see aiinpaintprompts.h).
 Every service that does that has to do it identically: the red the prompt
 talks about is this red, and a model given a different red or a different
 alpha is being asked a subtly different question. Two copies of this that
 drifted apart would mean the same removal came out differently depending on
 which provider the user picked.

 The region of interest is not chosen here. By the time a service is called the
 caller has already cropped to the mask bounds plus padding and scaled the
 result down to the long edge the services accept; these functions take what
 they are given.
 */
namespace AIInpaintComposite
{
	//! Alpha-blended over the area to be removed. Bright, saturated and quite
	//! opaque on purpose: the model has to see it as a deliberate marking and
	//! not as something that was in the photograph.
	constexpr int MaskR = 255;
	constexpr int MaskG = 30;
	constexpr int MaskB = 30;
	constexpr int MaskA = 220;

	//! Quality 92 is where JPEG stops adding visible artefacts around a hard
	//! edge like the mask boundary, without the size of a lossless encode.
	constexpr int JpegQuality = 92;

	/*! \brief Paint the mask onto the picture in red.

	    \param image the region being repaired.
	    \param mask 8-bit, non-zero where pixels are to be regenerated - the
	           same sense the built-in kernel and the LaMa client use.
	    \return the composited picture, or a null image if the two do not go
	            together. */
	inline QImage maskOverlay(const QImage& image, const QImage& mask)
	{
		QImage out = image.convertToFormat(QImage::Format_RGB32);
		if (out.isNull() || mask.isNull())
			return QImage();

		QImage grey = mask;
		if (grey.format() != QImage::Format_Grayscale8)
			grey = grey.convertToFormat(QImage::Format_Grayscale8);
		if (grey.isNull() || grey.size() != out.size())
			return QImage();

		for (int y = 0; y < out.height(); ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
			const uchar* m = grey.constScanLine(y);
			for (int x = 0; x < out.width(); ++x)
			{
				if (!m[x])
					continue;
				const QRgb px = line[x];
				// Straight source-over with a constant alpha. Integer maths
				// with a rounding term, because this runs over every masked
				// pixel of a picture up to 2048 on its long edge.
				const int r = (qRed(px)   * (255 - MaskA) + MaskR * MaskA + 127) / 255;
				const int g = (qGreen(px) * (255 - MaskA) + MaskG * MaskA + 127) / 255;
				const int b = (qBlue(px)  * (255 - MaskA) + MaskB * MaskA + 127) / 255;
				line[x] = qRgb(r, g, b);
			}
		}
		return out;
	}

	//! JPEG-encode for the wire. Empty on failure.
	inline QByteArray toJpeg(const QImage& image)
	{
		if (image.isNull())
			return QByteArray();
		QByteArray jpeg;
		QBuffer buffer(&jpeg);
		if (!buffer.open(QIODevice::WriteOnly))
			return QByteArray();
		if (!image.save(&buffer, "JPEG", JpegQuality))
			return QByteArray();
		buffer.close();
		return jpeg;
	}
}

#endif
