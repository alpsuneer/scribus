/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTIMAGEEXTRACTOR_H
#define PDFSMARTIMAGEEXTRACTOR_H

#include <QList>
#include <QString>

#include <memory>

#include "pdfsmartgraphicsextractor.h" // PdfSmartImageRegion

namespace poppler {
	class document;
}

//! One image ready to become a Scribus PageItem_ImageFrame: a saved file on
//! disk plus its on-page position (points, page-relative, top-left origin
//! -- the same convention PdfSmartImageRegion and PdfSmartTextBlock use).
struct PdfSmartImageBlock
{
	QString extractedFilePath;
	double x { 0.0 };
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
};

//! \brief Turns the image regions PdfSmartGraphicsExtractor finds into
//! actual image files, by rendering the whole page once via poppler-cpp
//! (same API PdfSmartTextExtractor uses) and cropping out each region --
//! NOT by decoding the original embedded image bytes.
//!
//! That's a deliberate simplification: decoding a PDF image XObject
//! correctly needs its own color space, bit depth, compression filter
//! (JPEG/CCITT/JBIG2/...) and mask handling -- real work, and exactly what
//! plugins/import/pdf/slaoutput.cpp's drawImage() spends hundreds of lines
//! on. Re-rendering the page region instead means the saved file is a
//! fresh raster at a chosen resolution, not the original bytes -- lower
//! fidelity for an image that was itself low-resolution and upscaled in
//! the PDF, but correct-looking for the common case, and far lower risk to
//! get right without a compiler on hand. Overlapping image regions on the
//! same page will each include whatever else happens to be under them --
//! a known limitation, not handled this session.
class PdfSmartImageExtractor
{
public:
	PdfSmartImageExtractor();
	~PdfSmartImageExtractor();

	//! Opens \a pdfFileName with poppler-cpp -- a separate handle from
	//! PdfSmartTextExtractor's and PdfSmartGraphicsExtractor's own (see
	//! PdfSmartGraphicsExtractor's header for why the low-level and
	//! poppler-cpp APIs can't share one document instance).
	//! \retval false if the file could not be opened.
	bool open(const QString& pdfFileName);

	//! Render page \a pageNumber (0-based) at \a dpi, crop out each of
	//! \a regions, and save the crops as PNG files under \a outputDir
	//! (created if it doesn't already exist).
	//! \retval one block per region that could be saved; a region that
	//! fails to crop or save is skipped rather than failing the whole page.
	QList<PdfSmartImageBlock> extractFromPage(int pageNumber, const QList<PdfSmartImageRegion>& regions, const QString& outputDir, double dpi = 150.0) const;

private:
	std::unique_ptr<poppler::document> m_document;
};

#endif // PDFSMARTIMAGEEXTRACTOR_H
