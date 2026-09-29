/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTFONTMAPPER_H
#define PDFSMARTFONTMAPPER_H

#include <QString>

class ScribusDoc;

//! \brief Registers an already-extracted embedded font file with Scribus's
//! own font system (Feature 7: Convert Embedded Fonts).
//!
//! mapToScribusFont() (PDF font name -> best installed substitute, Feature
//! 2) is NOT implemented here -- that job is already done, inline, by
//! findMatchingFont() in importpdfsmart.cpp, written before this class was
//! revisited; duplicating it here was not worth the risk of the two
//! drifting apart. This class now only covers the part Feature 2 doesn't:
//! using the PDF's own embedded font instead of substituting an installed
//! one, when PdfSmartFontExtractor managed to pull it out.
class PdfSmartFontMapper
{
public:
	PdfSmartFontMapper() = default;

	//! Unimplemented -- see the class comment. Currently always returns
	//! \a pdfFontName unchanged.
	QString mapToScribusFont(const QString& pdfFontName, bool isMalayalamText);

	//! Registers the font file at \a fontFilePath (produced by
	//! PdfSmartFontExtractor) with \a doc's font system, via
	//! SCFonts::addScalableFonts() -- the same directory-scanning
	//! mechanism Scribus's own startup font scan uses (the single-file
	//! addScalableFont() is private, so this is the only public entry
	//! point), pointed at fontFilePath's own containing directory.
	//!
	//! The font becomes usable immediately, but is NOT truly scoped to
	//! just this document (addScalableFonts()'s "DocName" filter matches
	//! against ScribusDoc::documentFileName(), which is empty for the
	//! brand-new, not-yet-saved document this plugin creates -- passing an
	//! empty DocName here to match makes the font a plain global font, one
	//! that stays visible/usable in the font list for the rest of this
	//! Scribus session, in any document, until Scribus is restarted). This
	//! is accepted as a minor, low-risk side effect rather than a reason
	//! not to use the extracted font at all.
	//!
	//! \retval the resulting font's lookup name (the key it was inserted
	//! into \a doc's AllFonts under -- pass this straight to
	//! CharStyle::setFont() via doc->AllFonts->value(name)) on success, or
	//! an empty string if \a doc is null or FreeType could not load the
	//! file as a font (a malformed or unsupported embedded subset).
	static QString registerEmbeddedFont(ScribusDoc* doc, const QString& fontFilePath);
};

#endif // PDFSMARTFONTMAPPER_H
