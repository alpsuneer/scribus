/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartfontmapper.h"

#include <QFileInfo>

#include "scfonts.h"
#include "scribusdoc.h"

QString PdfSmartFontMapper::mapToScribusFont(const QString& pdfFontName, bool /* isMalayalamText */)
{
	// TODO (later session): look up pdfFontName in a PDF-font -> system-font
	// table, with a Malayalam-specific table (Anjali Old Lipi, Rachana,
	// Meera, ...) consulted when isMalayalamText is true, falling back to
	// the nearest available system font.
	return pdfFontName;
}

QString PdfSmartFontMapper::registerEmbeddedFont(ScribusDoc* doc, const QString& fontFilePath)
{
	if (!doc || !doc->AllFonts)
		return QString();

	// SCFonts has no public "add just this one file" method -- the one
	// that takes a single file path, addScalableFont() (singular), is
	// private. Only the directory-scanning addScalableFonts() (plural) is
	// public, so that's what's used here, pointed at fontFilePath's own
	// containing directory. Safe to call repeatedly, including on a
	// directory containing fonts already registered by an earlier call:
	// addScalableFont() (called internally, once per file found) detects
	// an existing entry by name and just skips it rather than duplicating
	// or corrupting anything -- confirmed by reading its own duplicate-
	// handling branch in scfonts.cpp. PdfSmartFontExtractor gives every
	// extracted font its own file in this directory, so scanning it never
	// picks up anything unrelated. This also means this plugin never calls
	// FreeType directly -- addScalableFonts() manages its own FT_Library
	// internally.
	QFileInfo info(fontFilePath);
	doc->AllFonts->addScalableFonts(info.absolutePath(), QString());

	// addScalableFonts() doesn't hand back the ScFace or its lookup key
	// directly, so find it by the one thing we know for certain: the exact
	// file path just passed in. ScFace::fontPath() appends "(faceIndex+1)"
	// for a multi-face file (e.g. a TTC); startsWith() matches either way
	// since our own file path is always a strict prefix of that.
	SCFontsIterator it(*doc->AllFonts);
	for ( ; it.hasNext(); it.next())
	{
		if (it.current().fontPath().startsWith(fontFilePath) && it.current().usable())
			return it.currentKey();
	}
	return QString();
}
