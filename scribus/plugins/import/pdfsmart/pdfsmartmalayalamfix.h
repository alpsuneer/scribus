/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTMALAYALAMFIX_H
#define PDFSMARTMALAYALAMFIX_H

#include <QString>

//! \brief Post-processes text extracted from a PDF to repair Malayalam
//! damage PDF text extraction commonly introduces (Feature 8: Malayalam/
//! Indic Text Correction).
//!
//! Session 6 implements two passes, both mechanical and independently
//! verified against the Unicode Malayalam block chart (U+0D00-U+0D7F),
//! rather than reordering logic that would need real Malayalam-encoded
//! test PDFs and native-reader visual verification this session had
//! neither of:
//!   1. Unicode NFC normalization.
//!   2. Legacy chillu-sequence collapsing: a chillu letter (a consonant
//!      with no inherent vowel, e.g. the ന്‍ in "അവന്‍") has had its own
//!      atomic Unicode code point since Unicode 5.1/9.0, but the older,
//!      still very commonly produced fallback spelling -- base consonant,
//!      then VIRAMA, then ZERO WIDTH JOINER -- is collapsed to that atomic
//!      code point. See the .cpp for the exact table and why NFC alone
//!      does not already do this.
//!
//! Explicitly NOT attempted this session, and why:
//!   - Visual-to-logical reordering: legacy (pre-Unicode) Malayalam DTP
//!     workflows commonly used ASCII-mapped "hack" fonts (ML-TTKarthika,
//!     ML-TTIndulekha, and others), each with its own, incompatible
//!     glyph-order convention. Reproducing one correctly needs that
//!     specific font's actual mapping table, not a generic rule.
//!   - Missing ZWJ/ZWNJ insertion for broken conjuncts: needs knowing
//!     which consonant clusters should ligate, which is font-rendering
//!     dependent, not deducible from the bare code points alone.
//! Both would need real sample PDFs from the actual production workflow,
//! and a native Malayalam reader checking actual rendered output, neither
//! of which this session had. Per the original spec: "skip if links
//! unclear" -- guessing here risks corrupting already-correct text, which
//! is worse than leaving it unfixed.
class PdfSmartMalayalamFix
{
public:
	PdfSmartMalayalamFix() = default;

	//! \retval true if \a text contains any codepoint in the Malayalam
	//! Unicode block (U+0D00-U+0D7F).
	static bool containsMalayalam(const QString& text);

	//! Applies NFC normalization, then collapses legacy chillu sequences
	//! to their atomic code points (see the class comment). Safe to call
	//! on non-Malayalam or already-correctly-encoded text: NFC is
	//! idempotent and universal, and the chillu substitution only ever
	//! matches the specific three-code-point legacy sequence, never an
	//! already-atomic chillu letter.
	static QString correct(const QString& text);
};

#endif // PDFSMARTMALAYALAMFIX_H
