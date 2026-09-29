/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartcolumndetector.h"

#include <algorithm>
#include <cmath>

#include <QHash>

namespace {

//! Minimum horizontal gap (points) for that gap to count as a real
//! separation -- either a column gutter, or the boundary between two
//! different lines/columns that happen to share a Y position. A newspaper
//! column gutter is typically 8-20pt; normal inter-word spacing at body
//! text sizes is usually under 5pt. Fixed heuristic constant, not measured
//! from the document -- see the class comment in the header for the
//! false-positive/false-negative risk that implies.
constexpr double kGutterThreshold = 8.0;

//! How close two words' vertical positions need to be to count as the same
//! line, as a fraction of the taller word's height. Baseline text within
//! one line varies a little (super/subscripts, rounding) but not by
//! anywhere near a full line height.
constexpr double kLineYToleranceFactor = 0.5;

//! A line segment wider than this multiple of the page's median line width
//! is treated as a full-width headline/standfirst rather than column body
//! text. In an N-column layout a headline spanning 2 columns plus their
//! gutter is a bit over 2x a single column's width -- the narrowest
//! realistic case -- safely above this; ordinary single-column line-wrap
//! keeps almost every line within a small margin of the median (no line
//! can exceed its own column's width), safely below it.
constexpr double kWideOutlierMultiplier = 1.6;

struct Interval
{
	double left { 0.0 };
	double right { 0.0 };
};

//! One visual line: the words on it (x-sorted) plus its combined bounding
//! box. Two segments can share the same Y range if they came from
//! different columns -- see buildLineSegments().
struct LineSegment
{
	QList<PdfSmartWordBox> words; // x-sorted
	double left { 0.0 };
	double right { 0.0 };
	double top { 0.0 };
	double bottom { 0.0 };
};

//! Groups every word on the page into line segments in two passes: first by
//! Y-proximity (which can and often does merge two columns' lines into one
//! group, when both columns share the same leading), then, within each of
//! those groups, split on any X-gap wider than kGutterThreshold -- which is
//! exactly what separates two columns' same-height lines back into their
//! own segments, while leaving one genuine line (column-width or
//! full-width) intact, since ordinary text has no gap that wide.
QList<LineSegment> buildLineSegments(const QList<PdfSmartWordBox>& words)
{
	QList<PdfSmartWordBox> sorted = words;
	std::sort(sorted.begin(), sorted.end(), [](const PdfSmartWordBox& a, const PdfSmartWordBox& b) {
		return a.y < b.y;
	});

	QList<QList<PdfSmartWordBox>> yBands;
	for (const PdfSmartWordBox& w : sorted)
	{
		bool startNewBand = yBands.isEmpty();
		if (!startNewBand)
		{
			const PdfSmartWordBox& last = yBands.last().last();
			double tolerance = std::max(w.height, last.height) * kLineYToleranceFactor;
			startNewBand = std::abs(w.y - last.y) > tolerance;
		}
		if (startNewBand)
			yBands.append(QList<PdfSmartWordBox>{ w });
		else
			yBands.last().append(w);
	}

	QList<LineSegment> segments;
	for (QList<PdfSmartWordBox>& band : yBands)
	{
		std::sort(band.begin(), band.end(), [](const PdfSmartWordBox& a, const PdfSmartWordBox& b) {
			return a.x < b.x;
		});

		LineSegment current;
		for (const PdfSmartWordBox& w : band)
		{
			bool startNewSegment = current.words.isEmpty();
			if (!startNewSegment)
			{
				const PdfSmartWordBox& lastWord = current.words.last();
				startNewSegment = (w.x - (lastWord.x + lastWord.width)) > kGutterThreshold;
			}
			if (startNewSegment)
			{
				if (!current.words.isEmpty())
					segments.append(current);
				current = LineSegment();
			}
			current.words.append(w);
		}
		if (!current.words.isEmpty())
			segments.append(current);
	}

	for (LineSegment& seg : segments)
	{
		bool haveBox = false;
		for (const PdfSmartWordBox& w : seg.words)
		{
			if (!haveBox)
			{
				seg.left = w.x;
				seg.right = w.x + w.width;
				seg.top = w.y;
				seg.bottom = w.y + w.height;
				haveBox = true;
			}
			else
			{
				seg.left = std::min(seg.left, w.x);
				seg.right = std::max(seg.right, w.x + w.width);
				seg.top = std::min(seg.top, w.y);
				seg.bottom = std::max(seg.bottom, w.y + w.height);
			}
		}
	}
	return segments;
}

//! Sorts segment intervals and merges any two closer together than
//! kGutterThreshold, giving a set of disjoint, left-to-right column bands.
QList<Interval> buildColumnBands(const QList<LineSegment>& segments)
{
	QList<Interval> intervals;
	intervals.reserve(segments.size());
	for (const LineSegment& seg : segments)
		intervals.append(Interval{ seg.left, seg.right });

	std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
		return a.left < b.left;
	});

	QList<Interval> bands;
	for (const Interval& interval : intervals)
	{
		if (bands.isEmpty() || interval.left - bands.last().right > kGutterThreshold)
			bands.append(interval);
		else
			bands.last().right = std::max(bands.last().right, interval.right);
	}
	return bands;
}

//! Finds which band \a seg belongs to. Every input segment's own interval
//! was one of buildColumnBands()'s inputs, and a band only ever grows to
//! cover its inputs, so the segment's center is always within the band it
//! contributed to -- the nearest-band fallback below is defensive, not
//! expected to trigger.
int bandForSegment(const QList<Interval>& bands, const LineSegment& seg)
{
	double center = (seg.left + seg.right) / 2.0;
	int best = 0;
	double bestDistance = -1.0;
	for (int i = 0; i < bands.size(); ++i)
	{
		if (center >= bands.at(i).left && center <= bands.at(i).right)
			return i;
		double distance = std::min(std::abs(center - bands.at(i).left), std::abs(center - bands.at(i).right));
		if (bestDistance < 0.0 || distance < bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

//! Builds one text block from one or more line segments (top-to-bottom,
//! joined with a single space -- not a hard line break; see the header's
//! "Known limitations"), plus a dominant-font vote using the same scheme
//! PdfSmartTextExtractor::extractFromPage() uses for its single block.
//! \a segments must not be empty.
PdfSmartTextBlock buildBlockFromSegments(QList<LineSegment> segments)
{
	std::sort(segments.begin(), segments.end(), [](const LineSegment& a, const LineSegment& b) {
		return a.top < b.top;
	});

	double minX = segments.first().left;
	double minY = segments.first().top;
	double maxX = segments.first().right;
	double maxY = segments.first().bottom;
	for (const LineSegment& seg : segments)
	{
		minX = std::min(minX, seg.left);
		minY = std::min(minY, seg.top);
		maxX = std::max(maxX, seg.right);
		maxY = std::max(maxY, seg.bottom);
	}

	QString text;
	QHash<QString, int> fontVotes;
	QHash<QString, QString> fontNameOf;
	QHash<QString, double> fontSizeOf;

	for (int si = 0; si < segments.size(); ++si)
	{
		const LineSegment& seg = segments.at(si);
		if (si > 0)
			text += QLatin1Char(' ');
		for (int wi = 0; wi < seg.words.size(); ++wi)
		{
			const PdfSmartWordBox& w = seg.words.at(wi);
			text += w.text;
			if (wi + 1 < seg.words.size() && w.hasSpaceAfter)
				text += QLatin1Char(' ');

			if (!w.fontName.isEmpty())
			{
				QString key = w.fontName + QLatin1Char('|') + QString::number(w.fontSize, 'f', 2);
				fontVotes[key]++;
				fontNameOf[key] = w.fontName;
				fontSizeOf[key] = w.fontSize;
			}
		}
	}

	PdfSmartTextBlock block;
	block.text = text;
	block.x = minX;
	block.y = minY;
	block.width = maxX - minX;
	block.height = maxY - minY;

	QString bestKey;
	int bestVotes = 0;
	for (auto it = fontVotes.constBegin(); it != fontVotes.constEnd(); ++it)
	{
		if (it.value() > bestVotes)
		{
			bestVotes = it.value();
			bestKey = it.key();
		}
	}
	if (!bestKey.isEmpty())
	{
		block.fontName = fontNameOf.value(bestKey);
		block.fontSize = fontSizeOf.value(bestKey);
	}
	return block;
}

//! Maximum gap between two blocks, as a fraction of the narrower block's
//! own width, for that gap to still look like a plausible column gutter
//! rather than two unrelated blocks that happen to sit near each other.
//! Deliberately looser than kGutterThreshold's absolute 8pt: this check
//! runs on already-detected blocks (not raw words), so there's less need
//! to worry about ordinary inter-word spacing being mistaken for a gutter.
constexpr double kFlowColumnGapMaxFraction = 0.3;

//! How close two blocks' top edges need to be, in points, to count as
//! sitting in the same row of columns rather than e.g. a headline above an
//! unrelated column.
constexpr double kFlowSameRowYTolerance = 10.0;

//! How close a block's bottom edge needs to be to the deepest extent of
//! any block being considered, in points, to count as "likely cut off by
//! running out of space" rather than "ended with room to spare and
//! probably just finished naturally".
constexpr double kFlowDepthTolerance = 20.0;

//! Punctuation that can trail a real sentence-ending mark (closing quotes,
//! parentheses) and should be looked past when checking for one.
const QString& flowTrailingPunctuationToSkip()
{
	static const QString chars = QStringLiteral("\"'”’)»");
	return chars;
}

//! Whether \a text, after trimming and skipping trailing closing
//! punctuation, ends in something that reads as a complete sentence. Pure
//! punctuation-shape matching, not language understanding -- see the
//! header's "false-positive/false-negative risk" note.
bool endsAtSentenceBoundary(const QString& text)
{
	QString trimmed = text.trimmed();
	const QString& toSkip = flowTrailingPunctuationToSkip();
	while (!trimmed.isEmpty() && toSkip.contains(trimmed.back()))
		trimmed.chop(1);
	if (trimmed.isEmpty())
		return true; // nothing left to continue
	QChar last = trimmed.back();
	return last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?') || last == QChar(0x2026); // U+2026 HORIZONTAL ELLIPSIS
}

//! Whether \a b sits immediately to the right of \a a, at roughly the same
//! vertical position -- i.e. looks like the next column over in the same
//! row, not an unrelated block.
bool looksLikeAdjacentColumn(const PdfSmartTextBlock& a, const PdfSmartTextBlock& b)
{
	if (std::abs(a.y - b.y) > kFlowSameRowYTolerance)
		return false;
	double gap = b.x - (a.x + a.width);
	if (gap < 0.0)
		return false; // b doesn't actually start after a ends
	double narrowerWidth = std::min(a.width, b.width);
	if (narrowerWidth <= 0.0)
		return false;
	return gap <= narrowerWidth * kFlowColumnGapMaxFraction;
}

} // namespace

QList<PdfSmartTextBlock> PdfSmartColumnDetector::detectColumnBlocks(const QList<PdfSmartWordBox>& words) const
{
	QList<PdfSmartTextBlock> result;
	if (words.isEmpty())
		return result;

	QList<LineSegment> segments = buildLineSegments(words);
	if (segments.isEmpty())
		return result;

	// "Wide" is relative to this page's own median line width, not its
	// overall text extent -- a genuine single-column page's lines cluster
	// tightly around that column's own width no matter how wide the column
	// is (most lines nearly fill it; wrapping can't make one wider than the
	// column), so comparing to the page's overall extent would misclassify
	// most of a normal single-column page's lines as "wide" and shatter it
	// into one frame per line. A real full-width headline stands out from
	// the median regardless of how wide the columns underneath it are.
	QList<double> widths;
	widths.reserve(segments.size());
	for (const LineSegment& seg : segments)
		widths.append(seg.right - seg.left);
	std::sort(widths.begin(), widths.end());
	double medianWidth = widths.at(widths.size() / 2);

	QList<LineSegment> narrowSegments;
	QList<LineSegment> wideSegments;
	for (const LineSegment& seg : segments)
	{
		bool isWide = medianWidth > 0.0 && (seg.right - seg.left) > medianWidth * kWideOutlierMultiplier;
		(isWide ? wideSegments : narrowSegments).append(seg);
	}

	QList<Interval> bands = buildColumnBands(narrowSegments);

	if (bands.isEmpty())
	{
		// No column-width content to find a gutter in -- single-column
		// fallback: one block for everything on the page, matching
		// PdfSmartTextExtractor::extractFromPage()'s behavior for this case.
		result.append(buildBlockFromSegments(segments));
		return result;
	}

	QList<QList<LineSegment>> columnSegments;
	columnSegments.reserve(bands.size());
	for (int i = 0; i < bands.size(); ++i)
		columnSegments.append(QList<LineSegment>());
	for (const LineSegment& seg : narrowSegments)
		columnSegments[bandForSegment(bands, seg)].append(seg);

	// bands is left-to-right (built from x-sorted intervals, merged in
	// order), so iterating it in order gives left-to-right column output.
	for (int i = 0; i < bands.size(); ++i)
	{
		if (!columnSegments.at(i).isEmpty())
			result.append(buildBlockFromSegments(columnSegments.at(i)));
	}

	// Full-width lines (headlines/standfirsts) each become their own block
	// -- see the header comment for why multi-line headlines aren't
	// grouped into one block this session.
	for (const LineSegment& seg : wideSegments)
		result.append(buildBlockFromSegments(QList<LineSegment>{ seg }));

	return result;
}

QList<QPair<int, int>> PdfSmartColumnDetector::detectTextFlow(const QList<PdfSmartTextBlock>& blocks)
{
	QList<QPair<int, int>> result;
	if (blocks.size() < 2)
		return result;

	double deepestBottom = blocks.first().y + blocks.first().height;
	for (const PdfSmartTextBlock& b : blocks)
		deepestBottom = std::max(deepestBottom, b.y + b.height);

	for (int i = 0; i < blocks.size(); ++i)
	{
		const PdfSmartTextBlock& from = blocks.at(i);

		if (endsAtSentenceBoundary(from.text))
			continue; // looks complete on its own -- don't guess a continuation

		if ((from.y + from.height) < deepestBottom - kFlowDepthTolerance)
			continue; // ended with room to spare -- more likely a natural end than a cutoff

		for (int j = 0; j < blocks.size(); ++j)
		{
			if (i == j)
				continue;
			if (looksLikeAdjacentColumn(from, blocks.at(j)))
			{
				result.append(qMakePair(i, j));
				break; // at most one continuation target per source block
			}
		}
	}

	return result;
}
