/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartmalayalamfix.h"

bool PdfSmartMalayalamFix::containsMalayalam(const QString& text)
{
	for (const QChar& ch : text)
	{
		ushort u = ch.unicode();
		if (u >= 0x0D00 && u <= 0x0D7F)
			return true;
	}
	return false;
}

QString PdfSmartMalayalamFix::correct(const QString& text)
{
	QString result = text.normalized(QString::NormalizationForm_C);

	// Legacy chillu sequence -> atomic chillu code point. A chillu letter
	// is a consonant with no inherent vowel; the fallback spelling for
	// renderers that don't support the atomic forms is <base consonant>
	// <U+0D4D MALAYALAM SIGN VIRAMA> <U+200D ZERO WIDTH JOINER>, which is
	// what most Malayalam DTP output (including PDF) still commonly
	// produces. This is NOT something NFC normalization already handles
	// above: the atomic chillu code points carry no Unicode canonical
	// decomposition to this sequence, so the two are only related by this
	// script-specific, historical fallback convention, not by general
	// Unicode equivalence -- hence the explicit table.
	//
	// Every (base, chillu) pair below was checked against the official
	// character name in the Unicode Malayalam block chart (U+0D00-U+0D7F),
	// not reconstructed from memory alone: an initial, partial reference
	// consulted while building this had CHILLU L (U+0D7D) mislabeled as
	// "CHILLU R", which a second, complete pass over the whole block
	// caught and corrected. Only the standard virama (U+0D4D) is handled;
	// the rare alternate forms (U+0D3B VERTICAL BAR VIRAMA, U+0D3C
	// CIRCULAR VIRAMA) are not.
	struct ChilluPattern
	{
		ushort base;
		ushort chillu;
	};
	static const ChilluPattern kChilluPatterns[] = {
		{ 0x0D23, 0x0D7A }, // NNA  + virama + ZWJ -> CHILLU NN
		{ 0x0D28, 0x0D7B }, // NA   + virama + ZWJ -> CHILLU N
		{ 0x0D31, 0x0D7C }, // RRA  + virama + ZWJ -> CHILLU RR
		{ 0x0D32, 0x0D7D }, // LA   + virama + ZWJ -> CHILLU L
		{ 0x0D33, 0x0D7E }, // LLA  + virama + ZWJ -> CHILLU LL
		{ 0x0D15, 0x0D7F }, // KA   + virama + ZWJ -> CHILLU K
		{ 0x0D2E, 0x0D54 }, // MA   + virama + ZWJ -> CHILLU M
		{ 0x0D2F, 0x0D55 }, // YA   + virama + ZWJ -> CHILLU Y
		{ 0x0D34, 0x0D56 }, // LLLA + virama + ZWJ -> CHILLU LLL
	};

	const QChar virama(0x0D4D);
	const QChar zwj(0x200D);

	for (const ChilluPattern& pattern : kChilluPatterns)
	{
		QString legacySequence;
		legacySequence += QChar(pattern.base);
		legacySequence += virama;
		legacySequence += zwj;
		result.replace(legacySequence, QString(QChar(pattern.chillu)));
	}

	return result;
}
