/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTTEXTEXTRACTOR_H
#define PDFSMARTTEXTEXTRACTOR_H

#include <QList>
#include <QSizeF>
#include <QString>

#include <memory>

namespace poppler {
	class document;
}

//! \brief One page's worth of extracted text, positioned and (best-effort)
//! sized to where the text actually sits on the page, before it becomes a
//! Scribus PageItem_TextFrame.
//!
//! Session 2 scope: one block per page (poppler's own physical-layout text
//! extraction is used for the content, so line/paragraph structure within
//! the block is whatever poppler decided). Splitting a page into several
//! independently-positioned blocks -- one per paragraph or column -- is
//! Feature 5 (column detection) / Feature 6 (text flow) territory, not this
//! session's.
struct PdfSmartTextBlock
{
	QString text;
	double x { 0.0 };      // points, top-left origin, page-relative
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
	QString fontName;      // dominant font on the page; empty if unknown
	double fontSize { 0.0 }; // dominant font size in points; 0 if unknown
};

//! One word's position and text, for Feature 5 (column detection) --
//! PdfSmartColumnDetector needs word-level data to find column gutters and
//! rebuild per-column text, which the single whole-page PdfSmartTextBlock
//! above can't provide (poppler's own physical_layout text() already
//! interleaves columns into one reading order before we ever see it).
struct PdfSmartWordBox
{
	QString text;
	double x { 0.0 };      // points, top-left origin, page-relative
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
	bool hasSpaceAfter { false }; // false means the next word should NOT get a space before it (e.g. a hyphenated split)
	QString fontName;      // this word's own font; empty if unknown
	double fontSize { 0.0 };
};

//! \brief Extracts text from a PDF via poppler-cpp (Feature 1: Import Text
//! as Editable Text Frames; Feature 2: Preserve Text Formatting -- font name
//! and size only, see the .cpp for why color/bold/italic aren't available
//! through this API).
//!
//! Coordinates returned are in points -- PDF's native unit, which is also
//! Scribus's internal unit (see importpdfsmart.cpp for how that was
//! verified), so no unit conversion is needed before feeding them to
//! ScribusDoc::itemAdd(); the caller still needs to add the target page's
//! own xOffset()/yOffset() to place the block in document/canvas space.
class PdfSmartTextExtractor
{
public:
	PdfSmartTextExtractor();
	~PdfSmartTextExtractor();

	//! Opens \a pdfFileName with poppler. \retval false if the file could
	//! not be opened (missing, corrupt, or locked with no password) --
	//! callers must check this before calling the methods below.
	bool open(const QString& pdfFileName);

	//! Valid only after a successful open().
	int pageCount() const;

	//! Page size in points. Valid only after a successful open(); returns
	//! an empty QSizeF for an out-of-range \a pageNumber.
	QSizeF pageSizePoints(int pageNumber) const;

	//! Extract page \a pageNumber's text (0-based, like poppler's own
	//! indexing). \retval an empty list if the page has no extractable
	//! text (e.g. a scanned/image-only page) or is out of range; otherwise
	//! a single-element list holding that page's text block.
	QList<PdfSmartTextBlock> extractFromPage(int pageNumber) const;

	//! Word-level version of extractFromPage(), for PdfSmartColumnDetector.
	//! Same page, same underlying poppler-cpp text_list() call, but without
	//! collapsing the page into one block first. \retval an empty list for
	//! a page with no extractable text or an out-of-range \a pageNumber.
	QList<PdfSmartWordBox> extractWordsFromPage(int pageNumber) const;

private:
	std::unique_ptr<poppler::document> m_document;
};

#endif // PDFSMARTTEXTEXTRACTOR_H
