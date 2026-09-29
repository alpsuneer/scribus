/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTFONTEXTRACTOR_H
#define PDFSMARTFONTEXTRACTOR_H

#include <QHash>
#include <QString>

#include <memory>

class PDFDoc;

//! \brief Extracts embedded font program data from a PDF and saves it to
//! files, keyed by the font's PDF name with any subset tag stripped
//! (Feature 7: Convert Embedded Fonts).
//!
//! Uses poppler's low-level API (GfxFont::readEmbFontFile(), fired from
//! inside an OutputDev::updateFont() override -- confirmed against
//! poppler 25.03.0's Gfx::opShowText(), which calls updateFont()
//! unconditionally whenever the font changed, before checking
//! useDrawChar() at all, so this fires for every font actually used to
//! draw text regardless of that setting). This is a separate, independent
//! low-level pass from PdfSmartGraphicsExtractor (Session 3) -- both walk
//! a page via OutputDev, but kept apart so this feature can't regress
//! that already-working one.
//!
//! Only TrueType/OpenType-TT and CFF/OpenType-CFF (Type1C) embedded fonts
//! are extracted -- the overwhelming majority of real-world PDF font
//! embedding. NOT extracted, and why:
//!  - Type 3 fonts: procedural (glyphs are arbitrary PDF content-stream
//!    programs, not outline data) -- there is no font file to extract.
//!  - Raw Type 1 (PFA/PFB): readEmbFontFile() returns the same
//!    (sometimes still partially encrypted) byte stream regardless of
//!    subtype, and correctly reconstructing a loadable PFA/PFB from it
//!    needs format-specific handling not attempted this session.
//! Fonts of either kind fall back to Feature 2's name-based substitution
//! against already-installed fonts instead (see importpdfsmart.cpp).
//! A font actually saved here is still not guaranteed to load -- FreeType
//! may reject a malformed or unsupported subset; see
//! PdfSmartFontMapper::registerEmbeddedFont() for that failure path.
class PdfSmartFontExtractor
{
public:
	PdfSmartFontExtractor();
	~PdfSmartFontExtractor();

	//! Opens \a pdfFileName with poppler's low-level API -- a separate
	//! handle from every other extractor's (see PdfSmartGraphicsExtractor's
	//! header for why the low-level and poppler-cpp APIs can't share one).
	bool open(const QString& pdfFileName);

	//! Extracts every embedded, extractable font actually used to draw
	//! text on page \a pageNumber (0-based) that hasn't already been
	//! extracted by an earlier call on this same instance (a font used on
	//! several pages is only ever saved once), saving each to its own file
	//! under \a outputDir (created if it doesn't exist).
	//! \retval PDF font name (subset tag stripped) -> saved file path, for
	//! fonts newly extracted by THIS call only. Fonts found on an earlier
	//! call are not repeated here, but remain in allExtractedFonts().
	QHash<QString, QString> extractFromPage(int pageNumber, const QString& outputDir);

	//! Every font extracted so far across all extractFromPage() calls on
	//! this instance: PDF font name (subset tag stripped) -> file path.
	const QHash<QString, QString>& allExtractedFonts() const { return m_extracted; }

private:
	std::unique_ptr<PDFDoc> m_pdfDoc;
	QHash<QString, QString> m_extracted;
};

#endif // PDFSMARTFONTEXTRACTOR_H
