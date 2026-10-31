/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERTEXTCLEAN_H
#define SUNEERTEXTCLEAN_H

#include <QString>
#include <QStringList>

#include "scribusapi.h"

class PageItem;
class PageItem_TextFrame;
class StoryText;

/*!
 \brief Empty lines at the start / end of news text, and frames fitted to the
 last line.

 The API text often ends in "\n​\n​" (lines with only a zero-width
 space), a trailing Enter, spaces or HTML leftovers (<br>, <p>&nbsp;</p>).
 QString::trimmed() keeps U+200B, so those lines became empty paragraphs at
 the bottom of the story; the placement also ended every story with a
 paragraph mark, which is one more empty line. Every placement path (News
 Browser, Obituary, Layout Model engine / Gallery cards, re-layout) cleans
 here, and "Clean empty lines & fit frames" does the same for existing pages.

 Blank = white space, NBSP, U+2028/U+2029, the zero-width characters
 (U+200B, U+200C, U+200D, U+2060, U+FEFF, U+200E/F) and the Scribus line
 break. Removed only at the ends of paragraphs: soft hyphens are never
 removed, and a ZWJ right after a vowel sign / virama (the chillu sequence
 ണ്‍) is kept even there.
 */
namespace SuneerTextClean
{
	//! \a c prints nothing at the end of a paragraph (\a prev is the character before it, or 0).
	SCRIBUS_API bool isBlank(QChar c, QChar prev = QChar());

	//! Blank characters removed from both ends.
	SCRIBUS_API QString trimBlank(const QString& s);

	//! API text (plain or HTML) -> paragraphs: HTML decoded, every paragraph
	//! trimmed, empty paragraphs dropped (a run of them is one paragraph break).
	//! Single line breaks inside a paragraph stay.
	SCRIBUS_API QStringList paragraphs(const QString& raw);

	//! One-line field (title, kicker, byline, place, caption): paragraphs joined by a space,
	//! runs of white space collapsed, blank ends removed.
	SCRIBUS_API QString oneLine(const QString& raw);

	//! What the raw field had that the cleaning removes, for the log
	//! ("end: LF U+200B LF U+200B; 2 empty-line runs inside"); empty when nothing.
	SCRIBUS_API QString describeJunk(const QString& raw);

	struct StoryReport
	{
		int leadingParas = 0;     //!< blank paragraphs removed at the start
		int trailingParas = 0;    //!< blank paragraphs (and the last paragraph mark) removed at the end
		int collapsedParas = 0;   //!< blank paragraphs removed inside (runs of 2+)
		int trimmedChars = 0;     //!< blank characters removed at paragraph ends
		bool changed() const { return leadingParas + trailingParas + collapsedParas + trimmedChars > 0; }
		QString text() const;
	};

	//! Cleans the story of \a item: blank paragraphs at the start and end, the last paragraph mark,
	//! runs of 2+ blank paragraphs inside (-> one paragraph break), blank characters at the end of
	//! every paragraph and at the very start. The last paragraph keeps its style. With
	//! \a withUndo every deletion is an undo action of \a item (call inside a transaction).
	SCRIBUS_API StoryReport cleanStory(PageItem* item, bool withUndo);

	//! The same on a story without a frame (a scratch copy); no undo.
	SCRIBUS_API StoryReport cleanStoryText(StoryText& story);

	//! The last paragraph mark only, the last paragraph keeping its style (placement, no undo).
	SCRIBUS_API void dropTrailingParSep(StoryText& story);

	//! Height of \a t fitted to its last line: the smallest height in [minH, maxH] without
	//! overflow (bottom text distance and baseline grid included, they are part of the layout),
	//! raised in 0.25 pt steps (at most 2 pt) until every line is laid out exactly as with 2 pt
	//! more - a centred last line at the exact height fell back to justified. Sets the height
	//! and returns it. Overflow at maxH: maxH, *overflow = true. Linked frames are not touched.
	SCRIBUS_API double fitHeight(PageItem_TextFrame* t, double minH, double maxH, bool* overflow = nullptr);

	//! Blank paragraphs at the END of the story only (text pasted from elsewhere ends in them): the last
	//! paragraph with text keeps its style; nothing inside or at the start is touched. Returns the
	//! characters removed. With \a withUndo each deletion is an undo action (call inside a transaction).
	SCRIBUS_API int trimTrailingBlankParas(PageItem* item, bool withUndo);

	//! Every line of the frame and every glyph run in it (start, width), and the overflow flag: two
	//! layouts are the same when this is.
	SCRIBUS_API QVector<qint64> layoutSignature(PageItem_TextFrame* t);

	//! Only the blank space under the lowest line goes: the smallest height at which every line
	//! stays exactly where it is now. A multi-column frame is NOT re-balanced (its text keeps its
	//! columns and its place beside a wrapped photo); a single-column frame ends at its last line.
	//! Never taller than now; overflowing now: kept, *overflow = true.
	SCRIBUS_API double trimHeight(PageItem_TextFrame* t, bool* overflow = nullptr);
}

#endif
