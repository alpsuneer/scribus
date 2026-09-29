/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTGRAPHICSEXTRACTOR_H
#define PDFSMARTGRAPHICSEXTRACTOR_H

#include <QList>
#include <QString>

#include <memory>

#include "fpointarray.h"

class PDFDoc;

//! One color pulled from a PDF fill/stroke paint, converted to either RGB
//! or CMYK -- the same two branches
//! plugins/import/pdf/slaoutput.cpp::SlaOutputDev::getColor() treats as the
//! common cases. Separation, Indexed, Pattern, ICCBased and Lab color
//! spaces are not handled this session; see the extractor class comment.
struct PdfSmartColor
{
	enum Model { None, Rgb, Cmyk };
	Model model { None };
	double c1 { 0.0 };
	double c2 { 0.0 };
	double c3 { 0.0 };
	double c4 { 0.0 }; // CMYK only
};

//! One embedded image's on-page position -- points, page-relative,
//! top-left origin, the same convention PdfSmartTextExtractor's blocks use.
//! No pixel data here; PdfSmartImageExtractor turns this into an actual
//! file.
struct PdfSmartImageRegion
{
	double x { 0.0 };
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
};

//! One filled or stroked vector path. \a points is already CTM-mapped into
//! the same page-relative point space as PdfSmartImageRegion and
//! PdfSmartTextBlock -- see importpdfsmart.cpp for how that gets placed on
//! an actual Scribus page.
struct PdfSmartVectorShape
{
	FPointArray points;
	bool closed { false };
	PdfSmartColor fillColor;
	PdfSmartColor strokeColor;
	double lineWidth { 0.0 };
};

//! \brief Finds embedded images and vector paths on a PDF page by walking
//! its content stream through poppler's low-level OutputDev interface --
//! the same mechanism plugins/import/pdf/slaoutput.cpp uses for the
//! standard PDF importer, independently reimplemented here (no shared
//! code, no dependency on that plugin) so this plugin never touches it.
//!
//! poppler-cpp (what PdfSmartTextExtractor uses) has no API for either of
//! these -- it can only rasterize a whole page, and exposes no vector path
//! data at all -- which is why images and vectors need this separate,
//! lower-level pass instead of extending PdfSmartTextExtractor.
//!
//! Known limitations (documented rather than silently wrong):
//!  - Line dash pattern, cap/join style, opacity and blend mode are not
//!    captured -- shapes come out solid, fully opaque, default-joined.
//!  - Fill rule (nonzero vs even-odd) is not distinguished.
//!  - Tiling patterns, shading/gradient fills and soft masks are not
//!    understood; such a fill falls back to solid black (see the .cpp)
//!    rather than being invisible.
//!  - A shape that is both filled and stroked becomes two separate Scribus
//!    items (one from fill(), one from stroke()) instead of one item with
//!    both -- slaoutput.cpp merges these via a "same path as the last
//!    item" check that is not reproduced here.
//!  - Image regions are the axis-aligned bounding box of the image's
//!    CTM-mapped unit square -- a rotated image gets a larger, unrotated
//!    frame rather than a correctly rotated one.
class PdfSmartGraphicsExtractor
{
public:
	PdfSmartGraphicsExtractor();
	~PdfSmartGraphicsExtractor();

	//! Opens \a pdfFileName with poppler's low-level API -- a second,
	//! independent handle on the file alongside PdfSmartTextExtractor's own
	//! poppler-cpp one; the two APIs can't share a document instance.
	//! \retval false if the file could not be opened, or is encrypted (no
	//! password UI this session, same choice PdfSmartTextExtractor makes).
	bool open(const QString& pdfFileName);

	//! Valid only after a successful open().
	int pageCount() const;

	//! Extract page \a pageNumber's (0-based) images and/or vector shapes
	//! in a single render pass. Pass \a images or \a vectors as nullptr to
	//! skip capturing that kind -- this skips the matching per-primitive
	//! CTM/path work entirely, not just filtering results afterward, so
	//! it's worth doing when a checkbox is unchecked. Both lists (if
	//! non-null) are cleared first.
	void extractFromPage(int pageNumber, QList<PdfSmartImageRegion>* images, QList<PdfSmartVectorShape>* vectors) const;

private:
	std::unique_ptr<PDFDoc> m_pdfDoc;
};

#endif // PDFSMARTGRAPHICSEXTRACTOR_H
