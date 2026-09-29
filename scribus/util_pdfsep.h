/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UTIL_PDFSEP_H
#define UTIL_PDFSEP_H

#include <QImage>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "scribusapi.h"

/**
 * @brief Metadata for an arbitrary external PDF, as reported by poppler's pdfinfo.
 */
struct SCRIBUS_API PDFSepInfo
{
	bool    valid { false };
	QString errorMessage;
	int     pageCount { 0 };
	QSizeF  pageSizePts;   //!< size in points of the page that was queried
};

/**
 * @brief One CMYK plate rendered out of an external PDF page.
 */
struct SCRIBUS_API PDFSepPlate
{
	QString name;              //!< "Cyan", "Magenta", "Yellow" or "Black"
	QString tiffPath;          //!< grayscale separation TIFF Ghostscript wrote
	double  coveragePercent { 0.0 };
};

/**
 * @brief Result of rendering one page of an external PDF to CMYK separations.
 */
struct SCRIBUS_API PDFSepResult
{
	bool    ok { false };
	QString errorMessage;
	QString compositeTiffPath;      //!< composite CMYK preview Ghostscript wrote
	QVector<PDFSepPlate> plates;    //!< Cyan, Magenta, Yellow, Black in this order
	double  totalCoveragePercent { 0.0 };
};

/**
 * @brief Query page count and the size of one page of an arbitrary PDF file.
 * Shells out to poppler's `pdfinfo`; does not touch the current document.
 */
bool SCRIBUS_API pdfSepGetInfo(const QString& pdfPath, int pageNumber, PDFSepInfo& info);

/**
 * @brief Render one page of an arbitrary PDF to CMYK separation TIFFs plus a
 * composite, using Ghostscript's tiffsep device, and compute the average ink
 * coverage of each plate.
 * @param tempDir Directory to write the generated TIFFs into; caller owns
 * cleanup.
 * @param cancel Optional flag polled while Ghostscript runs (via System()'s
 * own event-pumping wait loop); set it from a UI thread to abort a
 * still-running render. On cancellation this returns false with
 * result.errorMessage left empty -- callers should check *cancel rather than
 * treating an empty message as "no error".
 */
bool SCRIBUS_API pdfSepGenerate(const QString& pdfPath, int pageNumber, int resolutionDPI, const QString& tempDir, PDFSepResult& result, const bool* cancel = nullptr);

/**
 * @brief Load one of the grayscale separation TIFFs Ghostscript wrote, as an
 * 8-bit grayscale QImage (255 = no ink, 0 = full ink for that plate).
 *
 * Goes through ScImage::loadPicture() rather than the plain QImage/Qt-plugin
 * TIFF path: plain QImage(path) silently returns a null image for these
 * files unless Qt's own tiff plugin happens to be installed, with no error
 * surfaced (coverage then reads as a uniform 0% and the preview as blank).
 * ScImage is what Scribus's own CMYK Print Preview already relies on to read
 * this exact kind of Ghostscript-generated separation TIFF reliably.
 */
QImage SCRIBUS_API pdfSepLoadPlateImage(const QString& tiffPath);

#endif
