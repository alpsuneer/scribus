/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTCOLUMNDETECTOR_H
#define PDFSMARTCOLUMNDETECTOR_H

#include <QList>
#include <QPair>

#include "pdfsmarttextextractor.h" // PdfSmartWordBox, PdfSmartTextBlock

//! \brief Groups a page's words into column-shaped text blocks (Feature 5:
//! Detect Columns Automatically), by finding vertical "gutters" -- X-ranges
//! with no text anywhere on the page -- and treating the ink between two
//! gutters as one column.
//!
//! This works on PdfSmartWordBox data (see that struct's comment), not the
//! single whole-page PdfSmartTextBlock PdfSmartTextExtractor::
//! extractFromPage() produces: that method already asks poppler's
//! physical_layout text() to interleave columns into one reading order,
//! which can't be reliably un-interleaved afterward. Column detection
//! instead rebuilds text itself from word positions, using poppler's
//! per-word text_list() data.
//!
//! The algorithm works in two stages specifically to handle the case this
//! plugin exists for -- a headline or standfirst spanning the full page
//! width, sitting above a multi-column body:
//!  1. Words are grouped into per-line segments (Y-proximity, then split on
//!     any X-gap wider than the gutter threshold -- the same threshold
//!     used for column detection below, which is what correctly separates
//!     two columns' lines that happen to share a Y position, a very common
//!     case when both columns use the same leading).
//!  2. Each line segment is classified as "column-width" or "full-width" by
//!     comparing its width to the page's median line width, NOT to the
//!     page's overall text extent -- a genuinely single-column page's lines
//!     cluster tightly around that column's own width no matter how wide
//!     the column is (wrapping can't make a line wider than its column), so
//!     comparing to the overall extent would misclassify most of a normal
//!     single-column page's lines as headlines and shatter it into one
//!     frame per line. Column bands are computed from the column-width
//!     segments only. Full-width segments (headlines) are excluded from
//!     that computation -- otherwise a single full-width line would bridge
//!     the gutter and merge what should be several column bands into one,
//!     badly interleaving the columns' text by Y position. Each full-width
//!     segment becomes its own block instead.
//!
//! Known limitations (documented rather than silently wrong):
//!  - The gutter-width threshold and the full-width outlier multiplier
//!    (see the .cpp) are fixed heuristic constants, not measured from the
//!    document. Pages with unusually wide inter-word spacing could trigger
//!    a false column split; an unusually narrow real gutter could fail to
//!    split at all; a genuine column noticeably wider than its neighbors
//!    (e.g. one holding a captioned image) could be misclassified as a
//!    full-width headline; a page with very few lines makes for a noisy
//!    median and an unreliable classification either way. The multiplier
//!    is tuned for the common case of equal-width columns (a headline
//!    spanning two of them is comfortably over the threshold either way);
//!    a headline over strongly asymmetric columns (e.g. a narrow sidebar
//!    plus a much wider main column) may land close enough to the
//!    dominant column's own median width to go undetected as a headline,
//!    falling back to the old gutter-bridging behavior for that page.
//!  - A multi-line headline becomes several separate one-line blocks
//!    (one frame per line) rather than being grouped into a single block --
//!    correct text and position, just more frames than ideal.
//!  - Falls back to one block spanning all the words -- matching
//!    PdfSmartTextExtractor::extractFromPage()'s single-column behavior --
//!    when no column-width segment produces a real gutter (including a
//!    genuinely single-column page).
//!  - Reading order across columns is always left-to-right; right-to-left
//!    and non-Latin column conventions are not handled.
//!  - Within one column, line breaks are not preserved as paragraph
//!    breaks -- lines are joined with a single space, the same choice
//!    PdfSmartTextExtractor::extractFromPage() makes and for the same
//!    reason: a reconstructed line boundary is at least as likely to be
//!    ordinary word-wrap as a real paragraph break.
class PdfSmartColumnDetector
{
public:
	PdfSmartColumnDetector() = default;

	//! \retval one PdfSmartTextBlock per detected column (left-to-right)
	//! plus one per full-width line (headline/standfirst), each built from
	//! its own words in top-to-bottom, left-to-right order -- not from
	//! poppler's page-wide reading order. A single block spanning every
	//! word's combined bounding box when no clear column gutter is found.
	//! Empty if \a words is empty.
	QList<PdfSmartTextBlock> detectColumnBlocks(const QList<PdfSmartWordBox>& words) const;

	//! \brief Feature 6: Recreate Text Flow -- DETECTION ONLY, deliberately
	//! not wired to PageItem::link().
	//!
	//! Scribus's real frame linking is a content-merging operation, not a
	//! "these are related" marker: if the target frame already has text,
	//! link() appends it onto the source frame's story, then overwrites
	//! every frame in the chain with that combined text, letting the
	//! layout engine decide which characters land in which frame purely by
	//! frame size. Every block here already holds its own independently
	//! extracted, correctly positioned text (from detectColumnBlocks() or
	//! PdfSmartTextExtractor::extractFromPage()), so actually linking two
	//! of them would re-flow that combined text according to frame
	//! capacity -- and since Feature 2's font substitution doesn't
	//! reproduce the original PDF's exact metrics, the resulting overflow
	//! point routinely will not land back on the real column boundary,
	//! shifting text into the wrong frame or leaving a gap. So this method
	//! only identifies likely continuations; the caller decides what, if
	//! anything, to do with that (this session: report it, don't link).
	//!
	//! A candidate pair (i, j) requires ALL of:
	//!  - block j sits immediately to the right of block i (a gap no wider
	//!    than a plausible gutter, relative to the narrower block's width)
	//!    at roughly the same vertical position -- i.e. they look like two
	//!    columns in the same row, not a headline and an unrelated column;
	//!  - block i's text does not end at a sentence boundary (informed by
	//!    the visible punctuation, not real language understanding);
	//!  - block i's text reaches close to the deepest extent of any block
	//!    in \a blocks, rather than ending well short of it -- evidence it
	//!    was cut off by running out of space, not that it simply ended.
	//! Both the position and the text signal must hold; either alone is
	//! not enough to call it a likely continuation. This is intentionally
	//! conservative -- see the .cpp for the exact thresholds and their
	//! false-positive/false-negative risk. \retval pairs of (fromIndex,
	//! toIndex) into \a blocks; empty when \a blocks has fewer than two
	//! entries or nothing looks like a continuation.
	QList<QPair<int, int>> detectTextFlow(const QList<PdfSmartTextBlock>& blocks);
};

#endif // PDFSMARTCOLUMNDETECTOR_H
