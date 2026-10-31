/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneertextclean.h"

#include <functional>

#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QVector>

#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scribusdoc.h"
#include "text/boxes.h"
#include "text/specialchars.h"
#include "text/storytext.h"
#include "undomanager.h"
#include "undostate.h"

namespace SuneerTextClean
{

bool isBlank(QChar c, QChar prev)
{
	const char16_t u = c.unicode();
	switch (u)
	{
		case 0x200B: case 0x200C: case 0x2060: case 0xFEFF: case 0x200E: case 0x200F:
			return true;
		case 0x200D:
			// ZWJ after a vowel sign / virama is part of the letter (chillu ണ്‍ = ണ ് ZWJ)
			return !(prev.isMark());
		case 0x00AD:
			return false;                    // soft hyphen: hyphenation, never removed
		default:
			break;
	}
	if (c == SpecialChars::PARSEP || c == SpecialChars::LINEBREAK || c == SpecialChars::OLD_LINEBREAK
	    || c == SpecialChars::NBSPACE || c == SpecialChars::NNBSPACE || c == SpecialChars::TAB)
		return true;
	if (c == SpecialChars::OBJECT || c == SpecialChars::COLBREAK || c == SpecialChars::FRAMEBREAK)
		return false;
	return c.isSpace();
}

QString trimBlank(const QString& s)
{
	int b = 0;
	int e = s.length();
	while (b < e && isBlank(s[b], b > 0 ? s[b - 1] : QChar()))
		++b;
	while (e > b && isBlank(s[e - 1], e - 2 >= 0 ? s[e - 2] : QChar()))
		--e;
	return s.mid(b, e - b);
}

namespace {

//! Plain text with '\n' between paragraphs: line ends normalised, HTML decoded.
QString decode(const QString& raw)
{
	QString text = raw;
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QChar('\r'), QChar('\n'));
	text.replace(QChar::ParagraphSeparator, QChar('\n'));
	static const QRegularExpression tagRe(QStringLiteral("<\\s*/?\\s*[A-Za-z][^>]*>"));
	static const QRegularExpression entRe(QStringLiteral("&(#\\d+|#x[0-9A-Fa-f]+|[A-Za-z]+);"));
	if (text.contains(tagRe) || text.contains(entRe))
	{
		// Block ends and <br> become paragraph breaks before the HTML parser
		// sees the text, so a run of inline-only markup still splits sensibly.
		static const QRegularExpression brRe(QStringLiteral("<\\s*br\\s*/?\\s*>"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression blockEndRe(QStringLiteral("<\\s*/\\s*(p|div|li|h[1-6]|tr|blockquote)\\s*>"), QRegularExpression::CaseInsensitiveOption);
		text.replace(brRe, QStringLiteral("\n"));
		text.replace(blockEndRe, QStringLiteral("\n"));
		// QTextDocumentFragment decodes entities and drops the remaining tags.
		// It does not touch the characters themselves.
		text.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
		text = QTextDocumentFragment::fromHtml(text).toPlainText();
		text.replace(QChar::ParagraphSeparator, QChar('\n'));
		text.replace(QChar::LineSeparator, QChar('\n'));
		text.replace(QChar::Nbsp, QChar(' '));
	}
	return text;
}

QString charName(QChar c)
{
	switch (c.unicode())
	{
		case '\n': return QStringLiteral("LF");
		case '\r': return QStringLiteral("CR");
		case ' ':  return QStringLiteral("SP");
		case '\t': return QStringLiteral("TAB");
		default:   return QStringLiteral("U+") + QString::number(c.unicode(), 16).toUpper().rightJustified(4, QChar('0'));
	}
}

//! "LF U+200B ×3" from a run of characters / HTML leftovers
QString describeRun(const QString& run)
{
	static const QRegularExpression tok(QStringLiteral("<[^>]*>|&[A-Za-z#0-9]+;|."), QRegularExpression::DotMatchesEverythingOption);
	QStringList names;
	auto it = tok.globalMatch(run);
	while (it.hasNext())
	{
		const QString t = it.next().captured(0);
		names << (t.length() == 1 ? charName(t[0]) : t.toLower());
	}
	// "LF U+200B LF U+200B LF U+200B" -> "(LF U+200B) x3": repeat of a pair or a single name
	QStringList out;
	int i = 0;
	while (i < names.size())
	{
		int n = 1;
		if (i + 3 < names.size() && names[i] != names[i + 1] && names[i] == names[i + 2] && names[i + 1] == names[i + 3])
		{
			n = 1;
			int j = i + 2;
			while (j + 1 < names.size() && names[j] == names[i] && names[j + 1] == names[i + 1])
			{
				++n;
				j += 2;
			}
			out << QStringLiteral("(%1 %2)x%3").arg(names[i], names[i + 1]).arg(n);
			i = j;
			continue;
		}
		while (i + n < names.size() && names[i + n] == names[i])
			++n;
		out << (n > 1 ? QStringLiteral("%1x%2").arg(names[i]).arg(n) : names[i]);
		i += n;
	}
	return out.join(QChar(' '));
}

//! A blank-looking prefix / suffix of the raw field, HTML leftovers included.
int blankPrefix(const QString& s)
{
	static const QRegularExpression htmlBlank(QStringLiteral("^(<\\s*/?\\s*(br|p|div|span)[^>]*>|&nbsp;|&#160;|&#xa0;)"), QRegularExpression::CaseInsensitiveOption);
	int i = 0;
	while (i < s.length())
	{
		if (isBlank(s[i], i > 0 ? s[i - 1] : QChar()))
		{
			++i;
			continue;
		}
		const QRegularExpressionMatch m = htmlBlank.match(s.mid(i, 40));
		if (m.hasMatch())
		{
			i += m.capturedLength(0);
			continue;
		}
		break;
	}
	return i;
}

int blankSuffix(const QString& s)
{
	static const QRegularExpression htmlBlank(QStringLiteral("(<\\s*/?\\s*(br|p|div|span)[^>]*>|&nbsp;|&#160;|&#xa0;)$"), QRegularExpression::CaseInsensitiveOption);
	int e = s.length();
	while (e > 0)
	{
		if (isBlank(s[e - 1], e >= 2 ? s[e - 2] : QChar()))
		{
			--e;
			continue;
		}
		const QRegularExpressionMatch m = htmlBlank.match(s.left(e).right(40));
		if (m.hasMatch())
		{
			e -= m.capturedLength(0);
			continue;
		}
		break;
	}
	return s.length() - e;
}

struct Para
{
	int start = 0;
	int end = 0;       //!< position of the paragraph mark, or the story length
	bool mark = false;
};

QVector<Para> storyParas(const StoryText& st)
{
	QVector<Para> out;
	const int len = st.length();
	int start = 0;
	for (int i = 0; i < len; ++i)
		if (st.text(i) == SpecialChars::PARSEP)
		{
			out.append({ start, i, true });
			start = i + 1;
		}
	if (start < len)
		out.append({ start, len, false });
	return out;
}

bool blankAt(const StoryText& st, int i, int from)
{
	return isBlank(st.text(i), i - 1 >= from ? st.text(i - 1) : QChar());
}

//! Ranges to delete (ascending, not overlapping) and the report.
StoryReport plan(const StoryText& st, QList<QPair<int, int>>& ranges, int* lastParaWithMark)
{
	StoryReport r;
	*lastParaWithMark = -1;
	const QVector<Para> paras = storyParas(st);
	const int n = paras.size();
	if (n == 0)
		return r;
	auto isBlankPara = [&](const Para& p) {
		for (int i = p.start; i < p.end; ++i)
			if (!blankAt(st, i, p.start))
				return false;
		return true;
	};
	auto trailStart = [&](const Para& p) {
		int t = p.end;
		while (t > p.start && blankAt(st, t - 1, p.start))
			--t;
		return t;
	};
	int first = -1, last = -1;
	for (int i = 0; i < n; ++i)
		if (!isBlankPara(paras[i]))
		{
			if (first < 0)
				first = i;
			last = i;
		}
	if (first < 0)
	{
		// nothing but blank paragraphs
		ranges.append({ 0, st.length() });
		r.trailingParas = n;
		return r;
	}
	// start of the text: blank paragraphs and blank characters before the first letter
	int p = paras[first].start;
	while (p < paras[first].end && blankAt(st, p, paras[first].start))
		++p;
	if (p > 0)
	{
		ranges.append({ 0, p });
		r.leadingParas = first;
		r.trimmedChars += p - paras[first].start;
	}
	int i = first;
	while (i < last)
	{
		const Para& pa = paras[i];
		if (!isBlankPara(pa))
		{
			const int t = qMax(trailStart(pa), i == first ? p : pa.start);
			if (t < pa.end)
			{
				ranges.append({ t, pa.end - t });
				r.trimmedChars += pa.end - t;
			}
			++i;
			continue;
		}
		int k = i;
		while (k + 1 < last && isBlankPara(paras[k + 1]))
			++k;
		if (k > i)
		{
			// 2+ blank paragraphs: one paragraph break remains (the mark before them)
			ranges.append({ paras[i].start, paras[k + 1].start - paras[i].start });
			r.collapsedParas += k - i + 1;
		}
		else if (pa.end > pa.start)
		{
			// one blank paragraph stays, as an empty one
			ranges.append({ pa.start, pa.end - pa.start });
			r.trimmedChars += pa.end - pa.start;
		}
		i = k + 1;
	}
	// end of the text: blank characters of the last paragraph, its mark and everything after
	const Para& pl = paras[last];
	const int t = qMax(trailStart(pl), last == first ? p : pl.start);
	if (t < st.length())
	{
		ranges.append({ t, st.length() - t });
		r.trimmedChars += pl.end - t;
		r.trailingParas = (n - 1 - last) + (paras.last().mark ? 1 : 0);
		if (pl.mark)
			*lastParaWithMark = last;
	}
	return r;
}

StoryReport apply(StoryText& st, const std::function<void(int, int)>& removeRange, const std::function<void(int, const ParagraphStyle&)>& setStyle)
{
	QList<QPair<int, int>> ranges;
	int lastWithMark = -1;
	StoryReport r = plan(st, ranges, &lastWithMark);
	if (ranges.isEmpty())
		return r;
	// the last paragraph's style lives on its mark, which goes
	ParagraphStyle lastStyle;
	if (lastWithMark >= 0)
	{
		const QVector<Para> paras = storyParas(st);
		lastStyle = st.paragraphStyle(paras[lastWithMark].start);
	}
	for (int k = ranges.size() - 1; k >= 0; --k)
		if (ranges[k].second > 0)
			removeRange(ranges[k].first, ranges[k].second);
	if (lastWithMark >= 0 && st.length() > 0)
		setStyle(st.length() - 1, lastStyle);
	return r;
}

} // namespace

QStringList paragraphs(const QString& raw)
{
	QStringList out;
	const QString text = decode(raw);
	for (const QString& line : text.split(QChar('\n')))
	{
		const QString t = trimBlank(line);
		if (!t.isEmpty())
			out << t;
	}
	return out;
}

QString oneLine(const QString& raw)
{
	static const QRegularExpression ws(QStringLiteral("\\s+"));
	return trimBlank(paragraphs(raw).join(QChar(' ')).replace(ws, QStringLiteral(" ")));
}

QString describeJunk(const QString& raw)
{
	if (raw.isEmpty())
		return QString();
	QStringList parts;
	const int pre = blankPrefix(raw);
	if (pre >= raw.length())
		return QStringLiteral("only blank: %1").arg(describeRun(raw.left(80)));
	const int suf = blankSuffix(raw);
	if (pre > 0)
		parts << QStringLiteral("start: %1").arg(describeRun(raw.left(pre)));
	if (suf > 0)
		parts << QStringLiteral("end: %1").arg(describeRun(raw.right(suf)));
	// empty lines inside (after decoding): runs of 2+ in a row
	QString text = decode(raw.mid(pre, raw.length() - pre - suf));
	const QStringList lines = text.split(QChar('\n'));
	int runs = 0, run = 0;
	for (const QString& l : lines)
	{
		if (trimBlank(l).isEmpty())
			++run;
		else
		{
			if (run >= 2)
				++runs;
			run = 0;
		}
	}
	if (runs > 0)
		parts << QStringLiteral("%1 run(s) of 2+ empty lines inside").arg(runs);
	return parts.join(QStringLiteral("; "));
}

QString StoryReport::text() const
{
	QStringList p;
	if (leadingParas)
		p << QStringLiteral("%1 empty line(s) at the start").arg(leadingParas);
	if (trailingParas)
		p << QStringLiteral("%1 empty line(s) at the end").arg(trailingParas);
	if (collapsedParas)
		p << QStringLiteral("%1 empty line(s) inside").arg(collapsedParas);
	if (trimmedChars)
		p << QStringLiteral("%1 blank character(s)").arg(trimmedChars);
	return p.join(QStringLiteral(", "));
}

StoryReport cleanStoryText(StoryText& story)
{
	return apply(story,
		[&](int pos, int len) { story.removeChars(pos, len); },
		[&](int pos, const ParagraphStyle& ps) { story.setStyle(pos, ps); });
}

StoryReport cleanStory(PageItem* item, bool withUndo)
{
	if (!item || !item->isTextFrame() || item->itemText.length() == 0)
		return StoryReport();
	PageItem_TextFrame* tf = item->asTextFrame();
	StoryText& st = item->itemText;
	// deleteSelectedTextFromFrame() needs the main window (a Gallery card's scratch copy has none)
	const bool undo = withUndo && UndoManager::undoEnabled() && item->doc() && item->doc()->scMW();
	const StoryReport r = apply(st,
		[&](int pos, int len) {
			if (undo)
			{
				// the frame's own delete: every removed run and paragraph mark is an undo action
				st.deselectAll();
				st.select(pos, len);
				tf->HasSel = true;
				tf->deleteSelectedTextFromFrame();
			}
			else
				st.removeChars(pos, len);
		},
		[&](int pos, const ParagraphStyle& ps) {
			if (undo)
			{
				auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
				is->set("SET_PARASTYLE");
				is->set("POS", pos);
				is->setStates(st.paragraphStyle(pos), ps);
				UndoManager::instance()->action(item, is);
			}
			st.setStyle(pos, ps);
		});
	if (r.changed())
	{
		st.deselectAll();
		st.setCursorPosition(qMin(st.cursorPosition(), st.length()));
		item->invalidateLayout();
	}
	return r;
}

int trimTrailingBlankParas(PageItem* item, bool withUndo)
{
	if (!item || !item->isTextFrame() || item->itemText.length() == 0)
		return 0;
	StoryText& st = item->itemText;
	const int len = st.length();
	int lastText = len - 1;
	while (lastText >= 0 && (st.text(lastText) == SpecialChars::PARSEP || isBlank(st.text(lastText), lastText > 0 ? st.text(lastText - 1) : QChar())))
		--lastText;
	if (lastText < 0)
		return 0;                                   // only blanks: nothing to keep a style for, left alone
	// only when a paragraph mark follows the last text: spaces at the end of the last line take no room
	bool parMark = false;
	for (int i = lastText + 1; i < len && !parMark; ++i)
		parMark = st.text(i) == SpecialChars::PARSEP;
	if (!parMark)
		return 0;
	const int from = lastText + 1, count = len - from;
	const ParagraphStyle keep = st.paragraphStyle(lastText);
	PageItem_TextFrame* tf = item->asTextFrame();
	const bool undo = withUndo && UndoManager::undoEnabled() && item->doc() && item->doc()->scMW();
	if (undo)
	{
		st.deselectAll();
		st.select(from, count);
		tf->HasSel = true;
		tf->deleteSelectedTextFromFrame();
		st.deselectAll();
		tf->HasSel = false;
	}
	else
		st.removeChars(from, count);
	if (st.length() > 0 && !st.paragraphStyle(st.length() - 1).equiv(keep))
	{
		if (undo)
		{
			auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
			is->set("SET_PARASTYLE");
			is->set("POS", st.length() - 1);
			is->setStates(st.paragraphStyle(st.length() - 1), keep);
			UndoManager::instance()->action(item, is);
		}
		st.setStyle(st.length() - 1, keep);
	}
	st.setCursorPosition(qMin(st.cursorPosition(), st.length()));
	item->invalidateLayout();
	return count;
}

void dropTrailingParSep(StoryText& story)
{
	const int len = story.length();
	if (len == 0 || story.text(len - 1) != SpecialChars::PARSEP)
		return;
	const ParagraphStyle ps = story.paragraphStyle(len - 1);
	story.removeChars(len - 1, 1);
	if (story.length() > 0)
		story.setStyle(story.length() - 1, ps);
}

//! Every line of the frame and every glyph run in it: where it starts and how wide it is.
QVector<qint64> layoutSignature(PageItem_TextFrame* t)
{
	QVector<qint64> s;
	const uint n = t->textLayout.lines();
	s.reserve(int(n) * 8 + 1);
	// y relative to the first line: a frame aligned to the middle / bottom moves all lines together
	const double y0 = n > 0 ? t->textLayout.line(0)->y() : 0.0;
	for (uint i = 0; i < n; ++i)
	{
		const LineBox* l = t->textLayout.line(i);
		s << l->firstChar() << l->lastChar() << qRound64(l->x() * 100) << qRound64((l->y() - y0) * 100) << qRound64(l->width() * 100);
		for (const Box* b : l->boxes())
			s << qRound64(b->x() * 100) << qRound64(b->width() * 100);
	}
	s << (t->frameOverflows() ? 1 : 0);
	return s;
}

double fitHeight(PageItem_TextFrame* t, double minH, double maxH, bool* overflow)
{
	if (overflow)
		*overflow = false;
	if (!t)
		return 0.0;
	if (t->nextInChain() || t->prevInChain())
		return t->height();
	// suneer 2026-10-31: the one fit (PageItem_TextFrame::suneerFitToInk) - the space under the last
	// line's ink equals the space above the first line's ink, blank paragraphs at the end dropped.
	// Placement, Layout Models / Gallery, obituary modules and Re-pack all come here.
	PageItem_TextFrame::SuneerFitOptions o;
	o.withUndo = false;
	o.minH = qMax(1.0, minH);
	o.maxH = qMax(o.minH, maxH);
	t->suneerFitToInk(o, overflow);
	return t->height();
}

double trimHeight(PageItem_TextFrame* t, bool* overflow)
{
	if (overflow)
		*overflow = false;
	if (!t)
		return 0.0;
	const double oldH = t->height();
	if (t->nextInChain() || t->prevInChain() || t->itemText.length() == 0)
		return oldH;
	auto layoutAt = [t](double h) {
		t->setHeight(h);
		t->updateClip();
		t->invalidateLayout();
		t->layout();
	};
	layoutAt(oldH);
	if (t->frameOverflows())
	{
		if (overflow)
			*overflow = true;
		return oldH;
	}
	const QVector<qint64> ref = layoutSignature(t);
	auto same = [&](double h) {
		layoutAt(h);
		return !t->frameOverflows() && layoutSignature(t) == ref;
	};
	double lo = 1.0, hi = oldH;
	if (same(lo))
		hi = lo;
	else
	{
		while (hi - lo > 0.05)
		{
			const double mid = (lo + hi) / 2.0;
			if (same(mid))
				hi = mid;
			else
				lo = mid;
		}
	}
	if (!same(hi))
		hi = oldH;               // not monotonic after all: leave the frame as it is
	layoutAt(hi);
	return hi;
}

} // namespace SuneerTextClean
