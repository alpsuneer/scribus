/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerduplicatenews.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <vector>

namespace
{
	constexpr int kShingle = 7;          // characters per body shingle (~2-3 Malayalam syllables)
	constexpr int kMinBody = 60;         // normalised chars before a body is compared at all
	constexpr int kMinContained = 120;   // the shorter body must be this long to count as "contained"
	constexpr int kMinHead = 6;          // normalised chars before a headline is compared
	constexpr int kMinHeadContained = 15;
	constexpr int kMaxFallbackHead = 150; // a first paragraph longer than this is body, not a headline
	constexpr int kMaxHeadParas = 3;

	const QChar kParSep(0x2029);
	const QChar kLineBreak(0x2028);

	inline void put(QString& out, QVector<int>* map, char16_t c, int raw)
	{
		out.append(QChar(c));
		if (map)
			map->append(raw);
	}

	bool hasLetters(const QString& text, int from, int to)
	{
		for (int i = from; i < to; ++i)
		{
			const QChar c = text.at(i);
			if (c.isLetterOrNumber())
				return true;
		}
		return false;
	}

	//! Only the paragraph's own named style counts: listing styles such as
	//! "cinema" or "31 Mash News" inherit from the size styles but are not headlines.
	bool ownStyleIs(const QStringList& styles, bool (*test)(const QString&))
	{
		return !styles.isEmpty() && test(styles.first());
	}

	std::vector<size_t> shingles(const QString& s)
	{
		std::vector<size_t> out;
		if (s.size() < kShingle)
			return out;
		out.reserve(s.size() - kShingle + 1);
		const QStringView v(s);
		for (int i = 0; i + kShingle <= s.size(); ++i)
			out.push_back(qHash(v.mid(i, kShingle)));
		return out;
	}

	int intersectCount(const std::vector<size_t>& a, const std::vector<size_t>& b)
	{
		int n = 0;
		auto ia = a.cbegin();
		auto ib = b.cbegin();
		while (ia != a.cend() && ib != b.cend())
		{
			if (*ia < *ib)
				++ia;
			else if (*ib < *ia)
				++ib;
			else
			{
				++n;
				++ia;
				++ib;
			}
		}
		return n;
	}

	int levenshtein(const QString& a, const QString& b)
	{
		const int n = a.size();
		const int m = b.size();
		std::vector<int> prev(m + 1);
		std::vector<int> cur(m + 1);
		for (int j = 0; j <= m; ++j)
			prev[j] = j;
		for (int i = 1; i <= n; ++i)
		{
			cur[0] = i;
			for (int j = 1; j <= m; ++j)
			{
				const int sub = prev[j - 1] + (a.at(i - 1) == b.at(j - 1) ? 0 : 1);
				cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, sub });
			}
			std::swap(prev, cur);
		}
		return prev[m];
	}

	//! Fraction of a body's characters that lie inside a shingle the other body also has.
	double coverage(const std::vector<size_t>& pos, const std::vector<size_t>& otherSet, int len)
	{
		if (len <= 0)
			return 0.0;
		int covered = 0;
		int reach = 0; // first position not yet counted
		for (size_t i = 0; i < pos.size(); ++i)
		{
			if (!std::binary_search(otherSet.cbegin(), otherSet.cend(), pos[i]))
				continue;
			const int from = qMax<int>(reach, static_cast<int>(i));
			const int to = qMin<int>(len, static_cast<int>(i) + kShingle);
			if (to > from)
				covered += to - from;
			reach = qMax(reach, to);
		}
		return static_cast<double>(covered) / len;
	}

	struct UnionFind
	{
		explicit UnionFind(int n) : parent(n) { for (int i = 0; i < n; ++i) parent[i] = i; }
		int find(int x)
		{
			while (parent[x] != x)
			{
				parent[x] = parent[parent[x]];
				x = parent[x];
			}
			return x;
		}
		void unite(int a, int b) { parent[find(a)] = find(b); }
		QVector<int> parent;
	};

	struct Pair
	{
		int a { -1 };
		int b { -1 };
		int score { 0 };
		bool contained { false };
		int shorter { -1 };
	};

	struct Prepared
	{
		std::vector<size_t> pos; // body shingle hash at each body position
		std::vector<size_t> set; // the same, sorted and unique
		bool headUsable { false };
		bool bodyUsable { false };
	};

	//! A one-word headline ("ന്യൂജേഴ്സി", a page label) says nothing on its own,
	//! and a story without a body to compare is usually page furniture (a date
	//! line, the masthead, "NEWS PAPER") repeated on every page: it needs three
	//! words and few digits before its headline counts.
	bool headlineUsable(const DupNews::Story& s, bool hasBody)
	{
		if (s.headNorm.size() < kMinHead)
			return false;
		const QString raw = s.text.mid(s.head.first, s.head.second - s.head.first);
		static const QRegularExpression ws(QStringLiteral("[\\s\\x{2028}\\x{2029}\\x{00A0}|]+"));
		const int words = raw.split(ws, Qt::SkipEmptyParts).size();
		if (words < 2)
			return false;
		if (hasBody)
			return true;
		int digits = 0;
		for (const QChar c : s.headNorm)
		{
			if (c.isDigit())
				++digits;
		}
		return words >= 3 && digits * 5 < s.headNorm.size();
	}

	int percentFloor(double x)
	{
		return static_cast<int>(x * 100.0 + 1e-9);
	}
}

namespace DupNews
{

bool Story::onPasteboard() const
{
	if (frames.isEmpty())
		return false;
	for (const FrameRef& f : frames)
	{
		if (f.page >= 0)
			return false;
	}
	return true;
}

int Story::firstPage() const
{
	for (const FrameRef& f : frames)
	{
		if (f.page >= 0)
			return f.page;
	}
	return -1;
}

QString normalise(const QString& text, int from, int to, QVector<int>* map)
{
	QString out;
	out.reserve(qMax(0, to - from));
	if (map)
	{
		map->clear();
		map->reserve(qMax(0, to - from));
	}
	for (int i = from; i < to; ++i)
	{
		const char16_t u = text.at(i).unicode();
		// Atomic chillus become consonant + virama, the same as the old
		// consonant + virama + ZWJ spelling once the ZWJ is dropped below.
		char16_t base = 0;
		switch (u)
		{
			case 0x0D7A: base = 0x0D23; break; // ൺ
			case 0x0D7B: base = 0x0D28; break; // ൻ
			case 0x0D7C: base = 0x0D30; break; // ർ
			case 0x0D7D: base = 0x0D32; break; // ൽ
			case 0x0D7E: base = 0x0D33; break; // ൾ
			case 0x0D7F: base = 0x0D15; break; // ൿ
			case 0x0D54: base = 0x0D2E; break; // ൔ
			case 0x0D55: base = 0x0D2F; break; // ൕ
			case 0x0D56: base = 0x0D34; break; // ൖ
			case 0x0D4E: base = 0x0D30; break; // dot reph = old ര് before a consonant
			default: break;
		}
		if (base)
		{
			put(out, map, base, i);
			put(out, map, 0x0D4D, i);
			continue;
		}
		// Two-part vowel signs in their decomposed (canonical) order.
		if (u == 0x0D4A || u == 0x0D4B || u == 0x0D4C)
		{
			put(out, map, u == 0x0D4B ? 0x0D47 : 0x0D46, i);
			put(out, map, u == 0x0D4C ? 0x0D57 : 0x0D3E, i);
			continue;
		}
		if (u == 0x0D3B || u == 0x0D3C) // vertical-bar / circular viramas
		{
			put(out, map, 0x0D4D, i);
			continue;
		}
		if (u >= 0x0D66 && u <= 0x0D6F) // Malayalam digits
		{
			put(out, map, u'0' + (u - 0x0D66), i);
			continue;
		}
		const QChar c(u);
		// Spaces, punctuation, hyphens, soft hyphens, ZWJ/ZWNJ/ZWSP and Scribus'
		// control characters are not letters, numbers or marks: all dropped.
		if (c.isLetterOrNumber() || c.isMark())
			put(out, map, c.toCaseFolded().unicode(), i);
	}
	return out;
}

bool isHeadlineStyle(const QString& name)
{
	// Size styles ("16 M", "72 B", "14 M (2)", "Headings:13 M"), kickers, anything named a head.
	static const QRegularExpression re(QStringLiteral("^(headings:)?\\s*\\d+(\\.\\d+)?\\s*[mb](\\s*\\(\\d+\\))?$|kicker|head"),
	                                   QRegularExpression::CaseInsensitiveOption);
	return re.match(name.trimmed()).hasMatch();
}

bool isBylineStyle(const QString& name)
{
	static const QRegularExpression re(QStringLiteral("byline|dateline"), QRegularExpression::CaseInsensitiveOption);
	return re.match(name).hasMatch();
}

bool isCaptionStyle(const QString& name)
{
	static const QRegularExpression re(QStringLiteral("caption|cutline|quote|highlight"),
	                                   QRegularExpression::CaseInsensitiveOption);
	return re.match(name).hasMatch();
}

void analyse(Story& s)
{
	s.head = s.body = Range(0, 0);
	s.headByStyle = false;
	s.headNorm.clear();
	s.bodyNorm.clear();
	s.headMap.clear();
	s.bodyMap.clear();

	QList<int> filled; // paragraphs with any letter or number in them
	for (int i = 0; i < s.paras.size(); ++i)
	{
		if (hasLetters(s.text, s.paras[i].start, s.paras[i].end))
			filled.append(i);
	}
	if (filled.isEmpty())
		return;

	bool allCaption = true;
	for (int i : filled)
		allCaption = allCaption && ownStyleIs(s.paras[i].styles, isCaptionStyle);
	// caption_/highlight_ frames are named so by the image and story tools
	static const QRegularExpression captionName(QStringLiteral("^(caption|cutline|highlight|quote)_"),
	                                            QRegularExpression::CaseInsensitiveOption);
	if (allCaption || captionName.match(s.name).hasMatch())
		s.captionLike = true;

	// 1. Leading paragraphs in a headline style (kicker + headline).
	int nHead = 0;
	while (nHead < filled.size() && nHead < kMaxHeadParas && ownStyleIs(s.paras[filled[nHead]].styles, isHeadlineStyle))
		++nHead;
	s.headByStyle = nHead > 0;

	// 2. Leading paragraphs set clearly larger than the body text.
	if (nHead == 0 && filled.size() > 1)
	{
		QHash<int, int> weight; // font size in 1/10 pt -> characters
		for (int i : filled)
		{
			const Para& p = s.paras[i];
			if (p.fontSize > 0)
				weight[qRound(p.fontSize * 10)] += p.end - p.start;
		}
		int bodySize = 0;
		int best = -1;
		for (auto it = weight.cbegin(); it != weight.cend(); ++it)
		{
			if (it.value() > best)
			{
				best = it.value();
				bodySize = it.key();
			}
		}
		if (bodySize > 0)
		{
			while (nHead < filled.size() - 1 && nHead < kMaxHeadParas
			       && s.paras[filled[nHead]].fontSize * 10 >= bodySize * 1.25
			       && !ownStyleIs(s.paras[filled[nHead]].styles, isBylineStyle))
				++nHead;
		}
	}

	// 3. Otherwise the first paragraph, as long as it is short enough to be one.
	if (nHead == 0)
	{
		const Para& first = s.paras[filled.first()];
		if (normalise(s.text, first.start, first.end, nullptr).size() <= kMaxFallbackHead)
			nHead = 1;
	}

	if (nHead > 0)
	{
		s.head = Range(s.paras[filled.first()].start, s.paras[filled[nHead - 1]].end);
		s.headNorm = normalise(s.text, s.head.first, s.head.second, &s.headMap);
	}
	const int bodyFrom = (nHead > 0) ? s.paras[filled[nHead - 1]].end : s.paras[filled.first()].start;
	s.body = Range(bodyFrom, s.text.size());
	s.bodyNorm = normalise(s.text, s.body.first, s.body.second, &s.bodyMap);
}

bool readSla(const QString& path, int source, QList<Story>& out, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	const QByteArray data = file.readAll();
	if (data.startsWith("\x1f\x8b"))
	{
		if (error)
			*error = QStringLiteral("compressed .sla files are not read");
		return false;
	}

	struct StyleDef
	{
		QString parent;
		double size { 0.0 };
	};
	struct RawPara
	{
		QString style;
		double ownSize { 0.0 };
		double charSize { 0.0 };
		int start { 0 };
		int end { 0 };
	};
	struct RawFrame
	{
		QString name;
		qint64 id { -1 };
		qint64 next { -1 };
		int page { -1 };
		bool text { false };
		bool hasStory { false };
		QString chars;
		QList<RawPara> paras;
		QString defStyle;
		double defSize { 0.0 };
	};

	QHash<QString, StyleDef> styleDefs;
	QList<RawFrame> frames;
	QList<int> open;    // PageObjects being read, outermost first
	int owner = -1;     // frame whose StoryText is being read
	int paraStart = 0;
	double paraCharSize = 0.0;
	bool paraHasChars = false;

	auto attr = [](const QXmlStreamAttributes& a, const char* name, const char* oldName) {
		const QString n = QString::fromLatin1(name);
		if (a.hasAttribute(n))
			return a.value(n).toString();
		return a.value(QString::fromLatin1(oldName)).toString();
	};
	auto closePara = [&](const QXmlStreamAttributes* a, bool parSep) {
		RawFrame& f = frames[owner];
		RawPara p;
		if (a)
		{
			p.style = attr(*a, "Parent", "PARENT");
			p.ownSize = a->value(QLatin1String("FontSize")).toDouble();
		}
		p.charSize = paraCharSize;
		p.start = paraStart;
		p.end = f.chars.size();
		f.paras.append(p);
		if (parSep)
			f.chars.append(kParSep);
		paraStart = f.chars.size();
		paraCharSize = 0.0;
		paraHasChars = false;
	};

	QXmlStreamReader xml(data);
	while (!xml.atEnd())
	{
		const QXmlStreamReader::TokenType tok = xml.readNext();
		if (tok == QXmlStreamReader::StartElement)
		{
			const QStringView tag = xml.name();
			const QXmlStreamAttributes attrs = xml.attributes();
			if (owner >= 0)
			{
				RawFrame& f = frames[owner];
				if (tag == u"DefaultStyle")
				{
					f.defStyle = attr(attrs, "Parent", "PARENT");
					f.defSize = attrs.value(QLatin1String("FontSize")).toDouble();
				}
				else if (tag == u"Content" || tag == u"ITEXT")
				{
					const QString chars = attr(attrs, "Chars", "CH");
					if (!paraHasChars && !chars.isEmpty())
					{
						paraCharSize = attrs.value(QLatin1String("FontSize")).toDouble();
						paraHasChars = true;
					}
					f.chars.append(chars);
				}
				else if (tag == u"para")
					closePara(&attrs, true);
				else if (tag == u"trail")
					closePara(&attrs, false);
				else if (tag == u"breakline")
					f.chars.append(kLineBreak);
				else if (tag == u"tab")
					f.chars.append(QChar(u'\t'));
				else if (tag == u"nbspace")
					f.chars.append(QChar(0x00A0));
				else if (tag == u"nbhyphen")
					f.chars.append(QChar(0x2011));
				continue;
			}
			if (tag == u"ParagraphStyle" && open.isEmpty())
			{
				StyleDef d;
				d.parent = attr(attrs, "Parent", "PARENT");
				d.size = attrs.value(QLatin1String("FontSize")).toDouble();
				styleDefs.insert(attr(attrs, "Name", "NAME"), d);
			}
			else if (tag == u"PageObject" || tag == u"PAGEOBJECT")
			{
				RawFrame f;
				f.text = attr(attrs, "ItemType", "PTYPE").toInt() == 4;
				f.name = attr(attrs, "AutoName", "ANNAME");
				bool ok = false;
				f.id = attrs.value(QLatin1String("ItemID")).toLongLong(&ok);
				if (!ok)
					f.id = -1;
				const QString next = attr(attrs, "NextItem", "NEXTITEM");
				f.next = next.isEmpty() ? -1 : next.toLongLong();
				// a group's children are on the page of the outermost group
				f.page = open.isEmpty() ? attr(attrs, "OwnPage", "OwnPage").toInt() : frames[open.first()].page;
				frames.append(f);
				open.append(frames.size() - 1);
			}
			else if (tag == u"StoryText" && !open.isEmpty() && frames[open.last()].text)
			{
				owner = open.last();
				frames[owner].hasStory = true;
				paraStart = 0;
				paraCharSize = 0.0;
				paraHasChars = false;
			}
		}
		else if (tok == QXmlStreamReader::EndElement)
		{
			const QStringView tag = xml.name();
			if (owner >= 0)
			{
				if (tag == u"StoryText")
				{
					if (paraStart < frames[owner].chars.size())
						closePara(nullptr, false);
					owner = -1;
				}
			}
			else if ((tag == u"PageObject" || tag == u"PAGEOBJECT") && !open.isEmpty())
				open.removeLast();
		}
	}
	if (xml.hasError())
	{
		if (error)
			*error = QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
		return false;
	}

	auto styleChain = [&](QString name) {
		QStringList chain;
		while (!name.isEmpty() && !chain.contains(name))
		{
			chain.append(name);
			name = styleDefs.value(name).parent;
		}
		return chain;
	};
	auto chainSize = [&](const QStringList& chain) {
		for (const QString& n : chain)
		{
			const double sz = styleDefs.value(n).size;
			if (sz > 0)
				return sz;
		}
		return 0.0;
	};

	QHash<qint64, int> byId;
	for (int i = 0; i < frames.size(); ++i)
	{
		if (frames[i].id >= 0)
			byId.insert(frames[i].id, i);
	}
	for (int i = 0; i < frames.size(); ++i)
	{
		const RawFrame& f = frames[i];
		if (!f.text || !f.hasStory)
			continue;
		Story s;
		s.source = source;
		s.name = f.name;
		s.text = f.chars;
		QSet<int> seen;
		for (int cur = i; cur >= 0 && !seen.contains(cur); )
		{
			seen.insert(cur);
			FrameRef ref;
			ref.name = frames[cur].name;
			ref.page = frames[cur].page;
			s.frames.append(ref);
			cur = (frames[cur].next >= 0) ? byId.value(frames[cur].next, -1) : -1;
		}
		const QStringList defChain = styleChain(f.defStyle);
		for (const RawPara& rp : f.paras)
		{
			Para p;
			p.start = rp.start;
			p.end = rp.end;
			p.styles = rp.style.isEmpty() ? defChain : styleChain(rp.style);
			p.fontSize = rp.ownSize;
			if (p.fontSize <= 0)
				p.fontSize = rp.charSize;
			if (p.fontSize <= 0)
				p.fontSize = chainSize(p.styles);
			if (p.fontSize <= 0)
				p.fontSize = f.defSize;
			if (p.fontSize <= 0)
				p.fontSize = chainSize(defChain);
			s.paras.append(p);
		}
		out.append(s);
	}
	return true;
}

QByteArray fingerprint(const QString& norm)
{
	return QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
}

QList<Range> toRaw(const QList<Range>& normRanges, const QVector<int>& map)
{
	QList<Range> raw;
	for (const Range& r : normRanges)
	{
		if (r.first < 0 || r.second > map.size() || r.first >= r.second)
			continue;
		const int from = map[r.first];
		const int to = map[r.second - 1] + 1;
		if (!raw.isEmpty() && raw.last().second >= from)
			raw.last().second = qMax(raw.last().second, to);
		else
			raw.append(Range(from, to));
	}
	return raw;
}

QString firstWords(const Story& s, int maxChars)
{
	Range r = (s.head.second > s.head.first) ? s.head : s.body;
	if (r.second <= r.first)
		r = Range(0, s.text.size());
	QString t;
	t.reserve(maxChars + 1);
	for (int i = r.first; i < r.second && t.size() <= maxChars; ++i)
	{
		const QChar c = s.text.at(i);
		const char16_t u = c.unicode();
		if (u == 0x00AD || u == 0x200B || u == 0xFEFF || u == 0xFFFC)
			continue;
		if (c.isSpace() || u == 0x2028 || u == 0x2029 || u == u'\t')
		{
			if (!t.isEmpty() && !t.endsWith(QLatin1Char(' ')))
				t.append(QLatin1Char(' '));
			continue;
		}
		t.append(c);
	}
	t = t.trimmed();
	if (t.size() > maxChars)
		t = t.left(maxChars).trimmed() + QChar(0x2026);
	return t;
}

Result run(Job job, Progress* progress)
{
	QElapsedTimer timer;
	timer.start();
	Result r;
	r.sources = job.sources;
	r.stories = std::move(job.stories);
	const Options& opt = job.options;
	const double threshold = opt.threshold / 100.0;
	auto cancelled = [&]() {
		if (progress && progress->cancel.load())
		{
			r.cancelled = true;
			r.groups.clear();
			r.elapsedMs = timer.elapsed();
			return true;
		}
		return false;
	};
	auto setProgress = [&](int permille) {
		if (progress)
			progress->permille.store(qBound(0, permille, 1000));
	};

	// 1. Read the folder's files (0-40%).
	const int fileSpan = job.slaSources.isEmpty() ? 0 : 400;
	for (int k = 0; k < job.slaSources.size(); ++k)
	{
		if (cancelled())
			return r;
		const int src = job.slaSources[k];
		QString err;
		if (!readSla(r.sources[src].filePath, src, r.stories, &err))
			r.warnings.append(QStringLiteral("%1: %2").arg(r.sources[src].label, err));
		setProgress(fileSpan * (k + 1) / job.slaSources.size());
	}

	// 2. Split and normalise (next 10%).
	const int n = r.stories.size();
	QList<int> use;
	std::vector<Prepared> prep(n);
	for (int i = 0; i < n; ++i)
	{
		Story& s = r.stories[i];
		analyse(s);
		if (!opt.includePasteboard && s.onPasteboard())
			continue;
		if (!opt.includeCaptions && s.captionLike)
			continue;
		Prepared& p = prep[i];
		p.bodyUsable = s.bodyNorm.size() >= kMinBody && !opt.ignored.contains(fingerprint(s.bodyNorm));
		p.headUsable = headlineUsable(s, p.bodyUsable) && !opt.ignored.contains(fingerprint(s.headNorm));
		if (p.bodyUsable)
		{
			p.pos = shingles(s.bodyNorm);
			p.set = p.pos;
			std::sort(p.set.begin(), p.set.end());
			p.set.erase(std::unique(p.set.begin(), p.set.end()), p.set.end());
		}
		if (p.bodyUsable || p.headUsable)
			use.append(i);
		if (cancelled())
			return r;
	}
	r.storiesChecked = use.size();
	const int compareFrom = fileSpan + 100;
	setProgress(compareFrom);

	// 3. Compare every pair (the rest).
	QVector<Pair> contentPairs;
	QVector<Pair> headPairs;
	for (int ui = 0; ui < use.size(); ++ui)
	{
		if (cancelled())
			return r;
		const int a = use[ui];
		const Story& sa = r.stories[a];
		const Prepared& pa = prep[a];
		for (int uj = ui + 1; uj < use.size(); ++uj)
		{
			const int b = use[uj];
			const Story& sb = r.stories[b];
			const Prepared& pb = prep[b];

			if (pa.bodyUsable && pb.bodyUsable)
			{
				const int na = static_cast<int>(pa.set.size());
				const int nb = static_cast<int>(pb.set.size());
				const int common = intersectCount(pa.set, pb.set);
				// Shingle overlap is only the prefilter: a changed word removes ~15
				// shingles, which undersells a short story with a few edits. The score
				// is the share of characters inside passages both bodies have.
				if (common > 0 && qMax(2.0 * common / (na + nb), static_cast<double>(common) / qMin(na, nb)) >= threshold * 0.6)
				{
					const int la = sa.bodyNorm.size();
					const int lb = sb.bodyNorm.size();
					const double covA = coverage(pa.pos, pb.set, la);
					const double covB = coverage(pb.pos, pa.set, lb);
					const double similarity = (covA * la + covB * lb) / (la + lb);
					const bool aShorter = la <= lb;
					const double inside = aShorter ? covA : covB;
					Pair p;
					p.a = a;
					p.b = b;
					if (similarity >= threshold)
						p.score = percentFloor(similarity);
					else if (inside >= threshold && qMin(la, lb) >= kMinContained && qMin(la, lb) * 5 <= qMax(la, lb) * 4)
					{
						p.score = percentFloor(inside);
						p.contained = true;
						p.shorter = aShorter ? a : b;
					}
					if (p.score > 0)
						contentPairs.append(p);
				}
			}

			if (pa.headUsable && pb.headUsable)
			{
				const QString& ha = sa.headNorm;
				const QString& hb = sb.headNorm;
				const bool aShorter = ha.size() <= hb.size();
				const QString& shortH = aShorter ? ha : hb;
				const QString& longH = aShorter ? hb : ha;
				Pair p;
				p.a = a;
				p.b = b;
				if (ha == hb)
					p.score = 100;
				else if (shortH.size() >= kMinHeadContained && longH.contains(shortH))
				{
					p.score = 100;
					p.contained = true;
					p.shorter = aShorter ? a : b;
				}
				else if (static_cast<double>(shortH.size()) / longH.size() >= threshold)
				{
					const double ratio = 1.0 - static_cast<double>(levenshtein(ha, hb)) / longH.size();
					if (ratio >= threshold)
						p.score = percentFloor(ratio);
				}
				if (p.score > 0)
					headPairs.append(p);
			}
		}
		const qint64 rowsDone = static_cast<qint64>(ui + 1) * (2 * use.size() - ui - 2);
		const qint64 rowsAll = qMax<qint64>(1, static_cast<qint64>(use.size()) * (use.size() - 1));
		setProgress(compareFrom + static_cast<int>((1000 - compareFrom) * rowsDone / rowsAll));
	}

	// 4. Group. Content first; a headline match between two stories that are
	//    already content duplicates of each other only marks their headlines.
	UnionFind ufContent(n);
	QVector<bool> inContent(n, false);
	for (const Pair& p : contentPairs)
	{
		ufContent.unite(p.a, p.b);
		inContent[p.a] = inContent[p.b] = true;
	}
	UnionFind ufHead(n);
	QVector<Pair> headKept;
	QSet<int> headInContent;
	for (const Pair& p : headPairs)
	{
		if (inContent[p.a] && inContent[p.b] && ufContent.find(p.a) == ufContent.find(p.b))
		{
			headInContent.insert(p.a);
			headInContent.insert(p.b);
			continue;
		}
		ufHead.unite(p.a, p.b);
		headKept.append(p);
	}

	auto build = [&](UnionFind& uf, const QVector<Pair>& pairs, Kind kind) {
		QList<Group> groups;
		QHash<int, int> groupOfRoot;
		QList<QHash<int, int>> memberIndex;
		for (const Pair& p : pairs)
		{
			const int root = uf.find(p.a);
			int g = groupOfRoot.value(root, -1);
			if (g < 0)
			{
				g = groups.size();
				groupOfRoot.insert(root, g);
				Group grp;
				grp.kind = kind;
				grp.minScore = p.score;
				grp.maxScore = p.score;
				groups.append(grp);
				memberIndex.append(QHash<int, int>());
			}
			Group& grp = groups[g];
			grp.minScore = qMin(grp.minScore, p.score);
			grp.maxScore = qMax(grp.maxScore, p.score);
			for (int st : { p.a, p.b })
			{
				int m = memberIndex[g].value(st, -1);
				if (m < 0)
				{
					m = grp.members.size();
					memberIndex[g].insert(st, m);
					Member mem;
					mem.story = st;
					grp.members.append(mem);
				}
				Member& mem = grp.members[m];
				mem.score = qMax(mem.score, p.score);
				if (p.contained && p.shorter == st)
					mem.contained = true;
			}
			if (p.contained)
				grp.contained = true;
		}
		return groups;
	};
	QList<Group> contentGroups = build(ufContent, contentPairs, Kind::Content);
	QList<Group> headGroups = build(ufHead, headKept, Kind::Headline);

	// 5. Matching passages: the body shingles a member shares with the rest of its group.
	for (Group& g : contentGroups)
	{
		for (Member& m : g.members)
		{
			std::vector<size_t> others;
			for (const Member& o : g.members)
			{
				if (o.story != m.story)
					others.insert(others.end(), prep[o.story].set.cbegin(), prep[o.story].set.cend());
			}
			std::sort(others.begin(), others.end());
			others.erase(std::unique(others.begin(), others.end()), others.end());
			const std::vector<size_t>& pos = prep[m.story].pos;
			const int len = r.stories[m.story].bodyNorm.size();
			std::vector<char> marked(len, 0);
			for (size_t i = 0; i < pos.size(); ++i)
			{
				if (std::binary_search(others.cbegin(), others.cend(), pos[i]))
					std::fill(marked.begin() + i, marked.begin() + qMin<int>(len, static_cast<int>(i) + kShingle), 1);
			}
			for (int i = 0; i < len; )
			{
				if (!marked[i])
				{
					++i;
					continue;
				}
				int j = i;
				while (j < len && marked[j])
					++j;
				m.bodyNormRanges.append(Range(i, j));
				i = j;
			}
			m.headMatched = headInContent.contains(m.story);
		}
	}
	for (Group& g : headGroups)
	{
		for (Member& m : g.members)
			m.headMatched = true;
	}

	auto byScore = [](const Group& x, const Group& y) { return x.maxScore > y.maxScore; };
	std::stable_sort(contentGroups.begin(), contentGroups.end(), byScore);
	std::stable_sort(headGroups.begin(), headGroups.end(), byScore);
	r.groups = contentGroups + headGroups;
	setProgress(1000);
	r.elapsedMs = timer.elapsed();
	return r;
}

}
