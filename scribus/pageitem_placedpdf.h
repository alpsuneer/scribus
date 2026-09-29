/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PAGEITEM_PLACEDPDF_H
#define PAGEITEM_PLACEDPDF_H

#include <QString>

#include "scribusapi.h"
#include "pageitem.h"
#include "pageitem_imageframe.h"

class ScribusDoc;

/**
 * @brief An image frame whose content is a placed PDF page.
 *
 * This is deliberately a thin subclass of PageItem_ImageFrame: file path,
 * page number, embed-in-SLA and canvas preview all come from the ImageFrame
 * base for free (Pfile / pixm.imgInfo.actualPageNumber / isInlineImage), the
 * same fields "Get Image" already uses when Pfile happens to be a PDF.
 *
 * The one thing that is genuinely different is PDF export: pdflib_core.cpp
 * checks isPlacedPDF() to require vector re-embedding of the source PDF's
 * content stream (PDF_EmbeddedPDF(), already implemented for any ImageFrame
 * whose Pfile is a PDF, but normally gated behind the "Embed PDF" export
 * option and silent about falling back to a raster copy). For a PlacedPDF
 * item that embedding is not optional, and a fallback to raster is reported
 * to the user rather than done silently, because the fidelity guarantee is
 * the entire point of Place vs. the reconstruction-based PDF import.
 */
class SCRIBUS_API PageItem_PlacedPDF : public PageItem_ImageFrame
{
	Q_OBJECT

public:
	PageItem_PlacedPDF(ScribusDoc *pa, double x, double y, double w, double h, double w2, const QString& fill, const QString& outline);
	~PageItem_PlacedPDF() override = default;

	PageItem_PlacedPDF * asPlacedPDF() override { return this; }
	bool isPlacedPDF() const override { return true; }

	ItemType realItemType() const override { return PageItem::PlacedPDF; }

	QString infoDescription() const override;

	/// Crop box the source PDF page was placed with: "CropBox", "MediaBox",
	/// "TrimBox", "BleedBox" or "ArtBox". Phase 1 always uses CropBox at
	/// place time; the field exists so a re-place/relink can remember the
	/// choice and so the export path could honour it later.
	QString cropBoxMode { QStringLiteral("CropBox") };
};

#endif
