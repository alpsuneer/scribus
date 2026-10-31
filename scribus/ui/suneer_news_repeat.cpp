/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/*
 * Suneer: "Check repetition on page" (toolbar, page right-click, Ctrl+Alt+D, News Browser button).
 *
 * On the current page (or the whole document) it lists
 *   - the same news twice: the same workflow id, or a near copy of the text (DupNews, the age guard
 *     for obituaries) - suneer_news_dup.cpp's scanPairs;
 *   - the same photo twice: SuneerPhotoHash (resized, recompressed, a little cropped; an edited or
 *     background-removed photo is hashed by its original);
 *   - repeated text: a paragraph or a sentence twice in one story, a long sentence in two stories,
 *     a word typed twice ("അന്തരിച്ചു അന്തരിച്ചു").
 * Click an entry: both are selected, zoomed to and marked yellow (SuneerDupHighlights: drawn on the
 * canvas widget only, never printed or exported). [Ignore] hides an entry until Scribus quits;
 * [Delete] removes one of the two - one undo step each. Nothing changes without a click.
 *
 * Limits measured on 34 real pages (tmp-repeat/proto.py): a word >= 4 letters, in one paragraph,
 * spaces only between (initials "പി പി", a dateline "കൊച്ചി ¶ കൊച്ചി ..." stay out); a sentence >= 20
 * letters inside a story, >= 60 across stories (obituary formulas, ad headings stay out); a
 * paragraph >= 40. Frames on the pasteboard, on hidden or non-printing layers are not checked, and a
 * frame repeating its own story (a pull quote) is not a repetition.
 */
#include "suneer_news_panel.h"
#include "suneer_news_dup_p.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "canvas.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "prefsmanager.h"
#include "scpage.h"
#include "scraction.h"
#include "commonstrings.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerphotohash.h"
#include "text/specialchars.h"
#include "ui/suneerduplicatenewsdialog.h"
#include "undomanager.h"

// one text chain of a checked item: the story text with its positions
struct SuneerRepeatUnit
{
	int item { -1 };
	QPointer<PageItem> frame;   // the first frame of the chain (it holds the text)
	QString text;
};

struct SuneerRepeatFinding
{
	QString kind;               // story, news, photo, paragraph, sentence, text, word
	int a { -1 };               // items
	int b { -1 };
	int unitA { -1 };           // text kinds: the units (the same one for a repeat inside a story)
	int unitB { -1 };
	QList<QPair<int, int>> rangesA;   // story positions [from, to) to mark
	QList<QPair<int, int>> rangesB;
	QList<QPair<int, int>> del;       // what "Delete the repeat" removes from unitB, back to front
	int score { 100 };
	int count { 1 };            // "text": how many sentences the two stories share
	QString sample;             // the repeated text as written
	QString key;                // for Ignore
};

struct SuneerRepeatState
{
	QPointer<ScribusDoc> doc;
	int page { -1 };            // -1: the whole document
	QList<SuneerDupItem> items;
	QList<QList<QPointer<PageItem>>> frames;
	QList<SuneerRepeatUnit> units;
	QList<SuneerRepeatFinding> findings;
	std::shared_ptr<DupNews::Progress> progress;
	int gen { 0 };
	bool running { false };
	qint64 ms { 0 };
	QPointer<QDialog> dlg;
	QComboBox* scope { nullptr };
	QPushButton* checkBtn { nullptr };
	QProgressBar* bar { nullptr };
	QLabel* status { nullptr };
	QTreeWidget* tree { nullptr };
	int shown { -1 };           // the finding marked on the page
	QStringList log;            // self-test
};

namespace
{
	using Finding = SuneerRepeatFinding;
	using Range = QPair<int, int>;

	const int kMinWord = 4;
	const int kMinSentenceInStory = 20;
	const int kMinSentenceAcross = 60;
	const int kMinParagraph = 40;
	const double kMinPhotoSide = 40.0;   // pt (14 mm)

	struct Seg
	{
		int from { 0 };
		int to { 0 };
		QString norm;
	};

	QString norm(const QString& t, int from, int to)
	{
		return DupNews::normalise(t, from, to, nullptr);
	}

	bool isBreak(QChar c)
	{
		return c == SpecialChars::PARSEP || c == SpecialChars::LINEBREAK || c == SpecialChars::COLBREAK || c == SpecialChars::FRAMEBREAK;
	}

	// a space between two words of one line (no line / paragraph / column break)
	bool isGap(QChar c)
	{
		return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == SpecialChars::NBSPACE || c == QChar(0x2009) || c == QChar(0x202F);
	}

	void trim(const QString& t, int& from, int& to)
	{
		while (from < to && (t.at(from).isSpace() || isBreak(t.at(from))))
			++from;
		while (to > from && (t.at(to - 1).isSpace() || isBreak(t.at(to - 1))))
			--to;
	}

	QList<Seg> paragraphs(const QString& t)
	{
		QList<Seg> out;
		int start = 0;
		for (int i = 0; i <= t.size(); ++i)
		{
			if (i < t.size() && t.at(i) != SpecialChars::PARSEP)
				continue;
			int f = start, e = i;
			trim(t, f, e);
			if (e > f)
				out.append(Seg { f, e, norm(t, f, e) });
			start = i + 1;
		}
		return out;
	}

	// sentences: up to . ? ! । (kept) or a break (not kept)
	QList<Seg> sentences(const QString& t)
	{
		QList<Seg> out;
		int start = 0;
		for (int i = 0; i <= t.size(); ++i)
		{
			const bool end = i == t.size() || isBreak(t.at(i));
			const bool stop = !end && (t.at(i) == QLatin1Char('.') || t.at(i) == QLatin1Char('?') || t.at(i) == QLatin1Char('!') || t.at(i) == QChar(0x0964));
			if (!end && !stop)
				continue;
			int f = start, e = stop ? i + 1 : i;
			trim(t, f, e);
			if (e > f)
				out.append(Seg { f, e, norm(t, f, e) });
			start = i + 1;
		}
		return out;
	}

	bool inside(const Seg& s, const QList<Range>& covered)
	{
		for (const Range& r : covered)
			if (s.from >= r.first && s.to <= r.second)
				return true;
		return false;
	}

	// a sentence to delete: with the spaces after it, so the text closes up
	Range sentenceCut(const QString& t, const Seg& s)
	{
		int e = s.to;
		while (e < t.size() && isGap(t.at(e)))
			++e;
		return Range(s.from, e);
	}

	// a paragraph to delete: with one paragraph break
	Range paragraphCut(const QString& t, const Seg& s)
	{
		int f = s.from, e = s.to;
		while (e < t.size() && t.at(e) != SpecialChars::PARSEP)
			++e;
		while (f > 0 && t.at(f - 1) != SpecialChars::PARSEP)
			--f;
		if (e < t.size())
			++e;                                  // its own break
		else if (f > 0)
			--f;                                  // the last paragraph: the break before it
		return Range(f, e);
	}

	// words that Malayalam doubles on purpose ("പതുക്കെ പതുക്കെ", "വീണ്ടും വീണ്ടും")
	const QSet<QString>& doubledOnPurpose()
	{
		static const QSet<QString> words = [] {
			QSet<QString> s;
			for (const char* w : { "പതുക്കെ", "മെല്ലെ", "വേഗം", "ചെറിയ", "വലിയ", "പുതിയ", "തമ്മിൽ", "വീണ്ടും", "ഇടയ്ക്ക്",
			                       "അടുത്ത്", "പിന്നെ", "ഓരോന്നായി", "കൂടുതൽ", "ഒന്നൊന്നായി", "പലപ്പോഴും" })
			{
				const QString u = QString::fromUtf8(w);
				s.insert(norm(u, 0, u.size()));
			}
			return s;
		}();
		return words;
	}

	// similarity of two whole texts (normalised, 4-letter shingles, Dice): a headline, a caption or a small
	// ad that repeats the start of a long story is not that story again - the sizes differ too much
	int wholeTextScore(const SuneerDupItem& x, const SuneerDupItem& y)
	{
		auto shingles = [](const SuneerDupItem& d) {
			const QString all = d.title + QLatin1Char(' ') + d.body;
			const QString n = norm(all, 0, all.size());
			QSet<QString> s;
			for (int i = 0; i + 4 <= n.size(); ++i)
				s.insert(n.mid(i, 4));
			return s;
		};
		const QSet<QString> a = shingles(x), b = shingles(y);
		if (a.isEmpty() || b.isEmpty())
			return 0;
		const int common = QSet<QString>(a).intersect(b).size();
		return 200 * common / (a.size() + b.size());
	}

	QString keyOf(const QString& kind, const QString& a, const QString& b, const QString& text)
	{
		return kind + QLatin1Char('|') + qMin(a, b) + QLatin1Char('|') + qMax(a, b) + QLatin1Char('|') + text.left(80);
	}

	// The worker. \a texts: (item, text) per unit. Thread-safe: no GUI, no document.
	QList<Finding> scanRepeats(const QList<SuneerDupItem>& items, const QList<QPair<int, QString>>& texts, int threshold, int photoBits,
	                           DupNews::Progress* progress)
	{
		QList<Finding> out;
		auto cancelled = [progress]() { return progress && progress->cancel.load(); };
		// 1. photos first: hashing is the slow part on a new page (cached per file afterwards)
		int withPhoto = 0;
		for (const SuneerDupItem& d : items)
			withPhoto += d.photo.isEmpty() ? 0 : 1;
		int done = 0;
		for (const SuneerDupItem& d : items)
		{
			if (cancelled())
				return out;
			if (d.photo.isEmpty())
				continue;
			if (!qEnvironmentVariableIsEmpty("SUNEER_REPEAT_DEBUG"))
				fprintf(stderr, "REPEAT hash %s\n", qPrintable(d.photo));
			SuneerPhotoHash::ofFile(d.photo);
			if (progress)
				progress->permille = 700 * ++done / qMax(1, withPhoto);
		}
		// 2. stories and photos: the same id, a near copy, the same photo
		const QList<SuneerDupPair> pairs = SuneerDupScan::scanPairs(items, threshold, photoBits);
		QSet<QPair<int, int>> paired;
		for (const SuneerDupPair& p : pairs)
		{
			int score = p.score;
			if (p.kind == QLatin1String("news"))
			{
				score = wholeTextScore(items[p.a], items[p.b]);
				if (score < threshold)
					continue;
			}
			Finding f;
			f.kind = p.kind;
			f.a = p.a;
			f.b = p.b;
			f.score = score;
			f.key = keyOf(p.kind, items[p.a].sid + items[p.a].module, items[p.b].sid + items[p.b].module, QString());
			out.append(f);
			if (p.kind != QLatin1String("photo"))
				paired.insert(qMakePair(qMin(p.a, p.b), qMax(p.a, p.b)));
		}
		if (progress)
			progress->permille = 850;
		if (cancelled())
			return out;
		// 3. repeated text
		QHash<QString, QList<QPair<int, Seg>>> across;   // long sentence -> where
		for (int u = 0; u < texts.size(); ++u)
		{
			const int item = texts[u].first;
			const QString& t = texts[u].second;
			const QString module = items[item].module;
			QList<Range> covered;
			// one entry per repeated paragraph / sentence: the first, then every later copy
			auto repeatsOf = [&](const QString& kind, const QList<Seg>& segs, int minLen, bool isPara) {
				QHash<QString, QList<Seg>> where;
				QStringList order;
				for (const Seg& g : segs)
				{
					if (g.norm.size() < minLen || (!isPara && inside(g, covered)))
						continue;
					if (!where.contains(g.norm))
						order << g.norm;
					where[g.norm].append(g);
				}
				for (const QString& n : std::as_const(order))
				{
					const QList<Seg>& all = where[n];
					if (all.size() < 2)
						continue;
					Finding f;
					f.kind = kind;
					f.a = f.b = item;
					f.unitA = f.unitB = u;
					f.count = all.size();
					f.rangesA << Range(all[0].from, all[0].to);
					for (int k = 1; k < all.size(); ++k)
					{
						f.rangesB << Range(all[k].from, all[k].to);
						f.del << (isPara ? paragraphCut(t, all[k]) : sentenceCut(t, all[k]));
						if (isPara)
							covered << Range(all[k].from, all[k].to);
					}
					if (isPara)
						covered << Range(all[0].from, all[0].to);
					f.sample = t.mid(all[0].from, all[0].to - all[0].from);
					f.key = keyOf(kind, module, module, n);
					out.append(f);
				}
			};
			repeatsOf(QStringLiteral("paragraph"), paragraphs(t), kMinParagraph, true);
			const QList<Seg> sents = sentences(t);
			repeatsOf(QStringLiteral("sentence"), sents, kMinSentenceInStory, false);
			// long sentences for the comparison between stories
			QSet<QString> seenHere;
			for (const Seg& g : sents)
				if (g.norm.size() >= kMinSentenceAcross && !seenHere.contains(g.norm))
				{
					seenHere.insert(g.norm);
					across[g.norm].append(qMakePair(u, g));
				}
			// a word typed twice: one paragraph, spaces only between
			static const QRegularExpression word(QStringLiteral("[^\\s\\x{2028}\\x{2029}\\x{1a}\\x{1b}]+"));
			auto m = word.globalMatch(t);
			int prevFrom = -1, prevTo = -1;
			QString prevNorm;
			while (m.hasNext())
			{
				const auto w = m.next();
				const int from = w.capturedStart(), to = w.capturedEnd();
				const QString n = norm(t, from, to);
				bool gapOnly = prevTo >= 0;
				for (int i = prevTo; gapOnly && i < from; ++i)
					gapOnly = isGap(t.at(i));
				const QChar lastOfPrev = prevTo > 0 ? t.at(prevTo - 1) : QChar();
				if (gapOnly && !n.isEmpty() && n == prevNorm && n.size() >= kMinWord && !lastOfPrev.isPunct()
				    && std::any_of(n.cbegin(), n.cend(), [](QChar c) { return c.isLetter(); }) && !doubledOnPurpose().contains(n))
				{
					Finding f;
					f.kind = QStringLiteral("word");
					f.a = f.b = item;
					f.unitA = f.unitB = u;
					f.rangesA << Range(prevFrom, prevTo);
					f.rangesB << Range(from, to);
					f.del << Range(prevTo, to);           // the space and the second word
					f.sample = t.mid(prevFrom, to - prevFrom);
					f.key = keyOf(f.kind, module, module, n + QString::number(from));
					out.append(f);
				}
				prevFrom = from;
				prevTo = to;
				prevNorm = n;
			}
			if (cancelled())
				return out;
		}
		// a long sentence in two stories (not two parts of one story, not a pair found above)
		QMap<QPair<int, int>, Finding> shared;
		for (auto it = across.cbegin(); it != across.cend(); ++it)
		{
			const auto& where = it.value();
			for (int x = 0; x < where.size(); ++x)
				for (int y = x + 1; y < where.size(); ++y)
				{
					const int ua = where[x].first, ub = where[y].first;
					const int ia = texts[ua].first, ib = texts[ub].first;
					if (ia == ib || paired.contains(qMakePair(qMin(ia, ib), qMax(ia, ib))))
						continue;
					const QPair<int, int> key(ua, ub);
					Finding& f = shared[key];
					if (f.kind.isEmpty())
					{
						f.kind = QStringLiteral("text");
						f.a = ia;
						f.b = ib;
						f.unitA = ua;
						f.unitB = ub;
						f.count = 0;
						f.sample = texts[ua].second.mid(where[x].second.from, where[x].second.to - where[x].second.from);
						f.key = keyOf(f.kind, items[ia].module, items[ib].module, it.key());
					}
					++f.count;
					f.rangesA << Range(where[x].second.from, where[x].second.to);
					f.rangesB << Range(where[y].second.from, where[y].second.to);
					f.del << sentenceCut(texts[ub].second, where[y].second);
				}
		}
		for (Finding f : std::as_const(shared))
		{
			std::sort(f.del.begin(), f.del.end(), [](const Range& p, const Range& q) { return p.first > q.first; });
			out.append(f);
		}
		if (progress)
			progress->permille = 1000;
		return out;
	}

	int kindOrder(const QString& k)
	{
		static const QStringList order { "story", "news", "photo", "paragraph", "sentence", "text", "word" };
		return order.indexOf(k);
	}

	PageItem* topOf(PageItem* it)
	{
		while (it && it->Parent)
			it = it->Parent;
		return it;
	}

	bool alive(ScribusDoc* doc, PageItem* it)
	{
		PageItem* top = topOf(it);
		return doc && top && doc->Items->contains(top);
	}

	// the frames of a text chain that show [from, to)
	QList<PageItem*> framesShowing(PageItem* first, int from, int to)
	{
		QList<PageItem*> out;
		for (PageItem* f = first; f; f = f->nextInChain())
		{
			if (f->firstInFrame() > f->lastInFrame())
				continue;
			if (f->firstInFrame() < to && f->lastInFrame() >= from)
				out << f;
		}
		if (out.isEmpty() && first)
			out << first;
		return out;
	}
}

void SuneerNewsPanel::checkDuplicatesOnPage()
{
	checkRepetition(false);
}

void SuneerNewsPanel::checkRepetition(bool wholeDocument)
{
	if (!m_doc)
		return;
	if (!m_repeat)
		m_repeat = std::make_shared<SuneerRepeatState>();
	SuneerRepeatState& st = *m_repeat;
	if (!st.dlg)
	{
		auto* dlg = new QDialog(this);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->setWindowTitle(tr("Repetition on the page"));
		dlg->setObjectName(QStringLiteral("suneerRepetitionPanel"));
		auto* lay = new QVBoxLayout(dlg);
		auto* top = new QHBoxLayout();
		st.scope = new QComboBox(dlg);
		st.scope->addItem(tr("This page"), 0);
		st.scope->addItem(tr("Whole document"), 1);
		st.scope->setToolTip(tr("Check only the page you are on, or every page (frames on the pasteboard and on hidden or non-printing layers are never checked)"));
		top->addWidget(st.scope);
		st.checkBtn = new QPushButton(tr("Check again"), dlg);
		top->addWidget(st.checkBtn);
		st.bar = new QProgressBar(dlg);
		st.bar->setRange(0, 1000);
		st.bar->setTextVisible(false);
		st.bar->setMaximumHeight(10);
		top->addWidget(st.bar, 1);
		lay->addLayout(top);
		st.status = new QLabel(dlg);
		st.status->setWordWrap(true);
		lay->addWidget(st.status);
		st.tree = new QTreeWidget(dlg);
		st.tree->setColumnCount(5);
		st.tree->setHeaderLabels({ tr("Repetition"), tr("Score"), tr("First"), tr("Second"), QString() });
		st.tree->setRootIsDecorated(false);
		st.tree->setUniformRowHeights(true);
		st.tree->header()->setStretchLastSection(false);
		lay->addWidget(st.tree, 1);
		auto* hint = new QLabel(tr("Click an entry: both are selected, zoomed to and marked yellow (on screen only, never printed). Nothing changes until you press Delete; each Delete is one undo step."), dlg);
		hint->setWordWrap(true);
		hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
		lay->addWidget(hint);
		connect(st.checkBtn, &QPushButton::clicked, this, [this]() { startRepeatCheck(); });
		connect(st.scope, QOverload<int>::of(&QComboBox::activated), this, [this](int) { startRepeatCheck(); });
		connect(st.tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* row, QTreeWidgetItem*) {
			if (row)
				showRepeat(row->data(0, Qt::UserRole).toInt());
		});
		connect(st.tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* row, int) {
			if (row)
				showRepeat(row->data(0, Qt::UserRole).toInt());
		});
		connect(dlg, &QObject::destroyed, this, [this, doc = QPointer<ScribusDoc>(m_doc)]() {
			if (doc)
				SuneerDupHighlights::setMarks(doc, {});
			if (m_repeat)
			{
				m_repeat->gen++;
				if (m_repeat->progress)
					m_repeat->progress->cancel = true;
				m_repeat->running = false;
				m_repeat->shown = -1;
			}
		});
		dlg->resize(900, 420);
		st.dlg = dlg;
	}
	st.scope->setCurrentIndex(wholeDocument ? 1 : 0);
	st.dlg->show();
	st.dlg->raise();
	startRepeatCheck();
}

// Snapshot the page on the GUI thread, compare on a worker; the panel shows progress meanwhile.
void SuneerNewsPanel::startRepeatCheck()
{
	if (!m_doc || !m_repeat || !m_repeat->dlg)
		return;
	SuneerRepeatState& st = *m_repeat;
	if (st.progress)
		st.progress->cancel = true;                      // a check still running is dropped
	const int gen = ++st.gen;
	st.doc = m_doc;
	st.page = st.scope->currentData().toInt() == 1 ? -1 : m_doc->currentPageNumber();
	st.shown = -1;
	SuneerDupHighlights::setMarks(m_doc, {});
	SuneerPhotoHash::loadCache(SuneerPhotoHash::cacheFileFor(m_doc));

	// every story / text chain / photo of the document, then those on the page that print
	QList<SuneerDupItem> all;
	QList<QList<PageItem*>> allFrames;
	dupCollectDocument(m_doc, all, allFrames);
	auto checked = [this, &st](PageItem* f) {
		PageItem* top = topOf(f);
		if (!top || top->OwnPage < 0 || (st.page >= 0 && top->OwnPage != st.page))
			return false;
		return m_doc->layerVisible(f->m_layerID) && m_doc->layerPrintable(f->m_layerID) && f->printEnabled();
	};
	st.items.clear();
	st.frames.clear();
	st.units.clear();
	st.findings.clear();
	for (int i = 0; i < all.size(); ++i)
	{
		QList<PageItem*> fr = allFrames.value(i);
		// a text chain: all its frames (dupCollectDocument keeps the first)
		for (int k = 0; k < fr.size(); ++k)
			if (fr[k]->isTextFrame())
				for (PageItem* n = fr[k]->nextInChain(); n; n = n->nextInChain())
					if (!fr.contains(n))
						fr << n;
		if (!std::any_of(fr.cbegin(), fr.cend(), checked))
			continue;
		// an icon, a bullet, a rule or a strip is not a photo (measured: 10x11 pt bullets, 937x19 pt strips)
		if (!all[i].photo.isEmpty())
		{
			bool photoSized = false;
			for (PageItem* f : std::as_const(fr))
				photoSized = photoSized || (f->isImageFrame() && f->width() >= kMinPhotoSide && f->height() >= kMinPhotoSide);
			if (!photoSized)
				all[i].photo.clear();
		}
		const int idx = st.items.size();
		st.items << all[i];
		QList<QPointer<PageItem>> keep;
		for (PageItem* f : fr)
			keep << QPointer<PageItem>(f);
		st.frames << keep;
		for (PageItem* f : std::as_const(fr))
			if (f->isTextFrame() && f->prevInChain() == nullptr && f->itemText.length() > 0)
				st.units << SuneerRepeatUnit { idx, QPointer<PageItem>(f), f->itemText.text(0, f->itemText.length()) };
	}
	QList<QPair<int, QString>> texts;
	for (const SuneerRepeatUnit& u : std::as_const(st.units))
		texts << qMakePair(u.item, u.text);

	st.running = true;
	st.progress = std::make_shared<DupNews::Progress>();
	st.bar->setValue(0);
	st.bar->show();
	st.tree->clear();
	st.checkBtn->setEnabled(false);
	const QString where = st.page >= 0 ? tr("page %1").arg(st.page + 1) : tr("the whole document");
	st.status->setText(tr("Checking %1: %n story(ies) / photo(s) ...", "", st.items.size()).arg(where));
	auto clock = std::make_shared<QElapsedTimer>();
	clock->start();
	auto* poll = new QTimer(st.dlg);
	poll->setInterval(100);
	std::weak_ptr<DupNews::Progress> prog = st.progress;
	connect(poll, &QTimer::timeout, this, [this, prog]() {
		if (auto p = prog.lock(); p && m_repeat)
			m_repeat->bar->setValue(p->permille.load());
	});
	poll->start();
	auto* w = new QFutureWatcher<QList<SuneerRepeatFinding>>(this);
	QPointer<ScribusDoc> doc = m_doc;
	connect(w, &QFutureWatcher<QList<SuneerRepeatFinding>>::finished, this, [this, w, gen, doc, clock, poll, where]() {
		w->deleteLater();
		poll->deleteLater();
		SuneerPhotoHash::saveCache(SuneerPhotoHash::cacheFileFor(doc));
		if (!m_repeat || gen != m_repeat->gen || !m_repeat->dlg)
			return;                                         // a newer check, or the panel closed
		SuneerRepeatState& st = *m_repeat;
		st.running = false;
		st.checkBtn->setEnabled(true);
		st.bar->hide();
		if (!doc || doc != m_doc)
		{
			st.status->setText(tr("The document changed - check again."));
			return;
		}
		QList<SuneerRepeatFinding> found = w->result();
		std::stable_sort(found.begin(), found.end(), [](const SuneerRepeatFinding& p, const SuneerRepeatFinding& q) {
			return kindOrder(p.kind) < kindOrder(q.kind);
		});
		st.findings = found;
		st.ms = clock->elapsed();
		fillRepeatResults(where);
	});
	const int threshold = SuneerDupScan::threshold(), bits = SuneerPhotoHash::maxDistance();
	const QList<SuneerDupItem> items = st.items;
	DupNews::Progress* p = st.progress.get();
	std::shared_ptr<DupNews::Progress> hold = st.progress;
	w->setFuture(QtConcurrent::run([items, texts, threshold, bits, p, hold]() { return scanRepeats(items, texts, threshold, bits, p); }));
}

void SuneerNewsPanel::fillRepeatResults(const QString& where)
{
	SuneerRepeatState& st = *m_repeat;
	st.tree->clear();
	st.log.clear();
	auto pageOf = [&st](int item) {
		QSet<int> pages;
		for (const QPointer<PageItem>& f : st.frames.value(item))
			if (f && topOf(f)->OwnPage >= 0)
				pages.insert(topOf(f)->OwnPage + 1);
		QList<int> l(pages.begin(), pages.end());
		std::sort(l.begin(), l.end());
		QStringList s;
		for (int p : l)
			s << QString::number(p);
		return s.join(QLatin1Char(','));
	};
	auto label = [&st, &pageOf](int i) {
		const SuneerDupItem& d = st.items[i];
		const QString name = d.title.isEmpty() ? d.module : collapseSpaces(d.title).left(36);
		return tr("%1 (p. %2)").arg(name, pageOf(i));
	};
	auto kindText = [](const QString& k) {
		if (k == QLatin1String("story")) return tr("Same story twice");
		if (k == QLatin1String("news")) return tr("Same news (other id)");
		if (k == QLatin1String("photo")) return tr("Same photo");
		if (k == QLatin1String("paragraph")) return tr("Paragraph repeated");
		if (k == QLatin1String("sentence")) return tr("Sentence repeated");
		if (k == QLatin1String("text")) return tr("Same text in two stories");
		return tr("Word twice");
	};
	int shown = 0, ignored = 0;
	for (int i = 0; i < st.findings.size(); ++i)
	{
		const SuneerRepeatFinding& f = st.findings[i];
		if (m_repeatIgnored.contains(f.key))
		{
			++ignored;
			continue;
		}
		const bool inStory = f.unitA >= 0 && f.unitA == f.unitB;
		QString first = label(f.a), second = inStory ? tr("the same story") : label(f.b);
		const QString quoted = f.sample.isEmpty() ? QString()
		        : QStringLiteral(" \"%1\"").arg(collapseSpaces(QString(f.sample).replace(SpecialChars::PARSEP, QLatin1Char(' ')).remove(SpecialChars::SHYPHEN)).left(50));
		if (inStory)
			first += (f.count > 2 ? tr(" - %1 times").arg(f.count) : QString()) + quoted;
		else if (f.kind == QLatin1String("text"))
			first += tr(" - %n sentence(s)", "", f.count) + quoted;
		auto* row = new QTreeWidgetItem(st.tree, { kindText(f.kind), QStringLiteral("%1 %").arg(f.score), first, second, QString() });
		row->setData(0, Qt::UserRole, i);
		row->setToolTip(2, f.sample.isEmpty() ? first : QString(f.sample).replace(SpecialChars::PARSEP, QLatin1Char('\n')).remove(SpecialChars::SHYPHEN));
		// [Ignore] [Delete ...]
		auto* cell = new QWidget(st.tree);
		auto* h = new QHBoxLayout(cell);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(2);
		auto* ign = new QToolButton(cell);
		ign->setText(tr("Ignore"));
		ign->setToolTip(tr("Hide this entry (until Scribus is closed)"));
		connect(ign, &QToolButton::clicked, this, [this, i]() { ignoreRepeat(i); });
		h->addWidget(ign);
		auto* del = new QToolButton(cell);
		del->setText(tr("Delete this one"));
		del->setPopupMode(QToolButton::InstantPopup);
		auto* menu = new QMenu(del);
		if (f.unitB >= 0 && !f.del.isEmpty())
		{
			const QString what = f.kind == QLatin1String("word") ? tr("Delete the second word")
			                   : f.kind == QLatin1String("paragraph") ? (f.count > 2 ? tr("Delete the %1 later copies of the paragraph").arg(f.count - 1) : tr("Delete the second paragraph"))
			                   : f.kind == QLatin1String("text") ? tr("Delete the repeated sentence(s) in \"%1\"").arg(label(f.b))
			                   : (f.count > 2 ? tr("Delete the %1 later copies of the sentence").arg(f.count - 1) : tr("Delete the second sentence"));
			menu->addAction(what, this, [this, i]() { deleteRepeat(i, 0); });
		}
		if (!inStory)
		{
			const QString what = f.kind == QLatin1String("photo") ? tr("Delete the photo of \"%1\"") : tr("Delete \"%1\"");
			menu->addAction(what.arg(label(f.a)), this, [this, i]() { deleteRepeat(i, 1); });
			menu->addAction(what.arg(label(f.b)), this, [this, i]() { deleteRepeat(i, 2); });
		}
		del->setMenu(menu);
		h->addWidget(del);
		st.tree->setItemWidget(row, 4, cell);
		st.log << QStringLiteral("%1 | %2 | %3 | %4").arg(f.kind, first, second).arg(f.score);
		++shown;
	}
	for (int c = 0; c < 5; ++c)
		st.tree->resizeColumnToContents(c);
	st.tree->setColumnWidth(2, qMin(st.tree->columnWidth(2), 330));
	st.tree->setColumnWidth(3, qMin(st.tree->columnWidth(3), 220));
	QString head = shown == 0 ? tr("No repetition on %1").arg(where) : tr("%n repetition(s) on %1", "", shown).arg(where);
	head += tr(" - %1 stories / photos checked in %2 ms").arg(st.items.size()).arg(st.ms);
	if (ignored)
		head += tr(", %n ignored", "", ignored);
	st.status->setText(head);
	m_dupLog = head;
	m_dupPagePairs = st.log;
	m_dupDialog = st.dlg;
	apiLog(QStringLiteral("check repetition: ") + head);
	setStatus(head);
}

// Select both, zoom so both are in view, mark them (and the repeated text) yellow.
void SuneerNewsPanel::showRepeat(int index)
{
	if (!m_repeat || !m_doc || m_repeat->doc != m_doc || index < 0 || index >= m_repeat->findings.size() || !m_mw)
		return;
	SuneerRepeatState& st = *m_repeat;
	const SuneerRepeatFinding& f = st.findings[index];
	const QColor yellow(255, 196, 0);
	QList<SuneerDupHighlights::Mark> marks;
	QList<PageItem*> sel;
	auto addFrames = [&](int item, int group, int unit, const QList<QPair<int, int>>& ranges) {
		QList<PageItem*> fr;
		if (unit >= 0 && st.units[unit].frame && alive(m_doc, st.units[unit].frame))
		{
			for (const auto& r : ranges)
				for (PageItem* x : framesShowing(st.units[unit].frame, r.first, r.second))
					if (!fr.contains(x))
						fr << x;
		}
		else
		{
			for (const QPointer<PageItem>& x : st.frames.value(item))
				if (x && alive(m_doc, x) && (f.kind != QLatin1String("photo") || x->isImageFrame()))
					fr << x;
		}
		for (PageItem* x : std::as_const(fr))
		{
			SuneerDupHighlights::Mark m;
			m.item = x;
			m.color = yellow;
			m.group = group;
			if (unit >= 0 && x->isTextFrame())
				m.ranges = ranges;
			marks << m;
			if (!sel.contains(x))
				sel << x;
		}
	};
	addFrames(f.a, 1, f.unitA, f.rangesA);
	addFrames(f.b, 2, f.unitB, f.rangesB);
	if (sel.isEmpty())
	{
		st.status->setText(tr("These frames are no longer in the document - check again."));
		return;
	}
	st.shown = index;
	// select: the first through the outline path (switches page / layer), then the rest
	m_mw->selectItemsFromOutlines(sel.first());
	for (PageItem* x : std::as_const(sel))
		if (!m_doc->m_Selection->containsItem(x))
			m_doc->m_Selection->addItem(x);
	// zoom: both in view with a margin
	QRectF box;
	for (PageItem* x : std::as_const(sel))
		box = box.united(topOf(x)->getVisualBoundingRect());
	ScribusView* view = m_mw->view;
	if (view && box.isValid())
	{
		const QSize vp = view->viewport()->size();
		// never closer than 300 % (one repeated word would fill the screen)
		const double maxScale = 3.0 * PrefsManager::instance().appPrefs.displayPrefs.displayScale;
		const double scale = qBound(0.05, 0.85 * qMin(vp.width() / qMax(1.0, box.width()), vp.height() / qMax(1.0, box.height())), maxScale);
		view->zoom(qRound(box.center().x()), qRound(box.center().y()), scale, false);
	}
	SuneerDupHighlights::setMarks(m_doc, marks);
	QString text = f.sample.isEmpty() ? tr("1 and 2 are marked yellow on the page.")
	                                  : tr("Marked yellow: \"%1\"").arg(collapseSpaces(QString(f.sample).replace(SpecialChars::PARSEP, QLatin1Char(' ')).remove(SpecialChars::SHYPHEN)).left(120));
	// a repeat pasted at the end is often in the overflow: nothing on the page shows it
	if (f.unitB >= 0 && st.units[f.unitB].frame && !f.rangesB.isEmpty())
	{
		PageItem* last = st.units[f.unitB].frame;
		while (last->nextInChain())
			last = last->nextInChain();
		if (f.rangesB.last().first > last->lastInFrame())
			text += tr(" - the repeat is in the overflow (not shown in the frame)");
	}
	st.status->setText(text);
}

void SuneerNewsPanel::ignoreRepeat(int index)
{
	if (!m_repeat || index < 0 || index >= m_repeat->findings.size())
		return;
	m_repeatIgnored.insert(m_repeat->findings[index].key);
	if (m_repeat->shown == index)
		SuneerDupHighlights::setMarks(m_doc, {});
	fillRepeatResults(m_repeat->page >= 0 ? tr("page %1").arg(m_repeat->page + 1) : tr("the whole document"));
}

// which: 0 = the repeated text in unitB, 1 = item a, 2 = item b (a photo pair: its image frames only).
// One undo step; then the check runs again (positions have moved).
void SuneerNewsPanel::deleteRepeat(int index, int which)
{
	if (!m_repeat || !m_doc || m_repeat->doc != m_doc || index < 0 || index >= m_repeat->findings.size())
		return;
	SuneerRepeatState& st = *m_repeat;
	const SuneerRepeatFinding f = st.findings[index];
	SuneerDupHighlights::setMarks(m_doc, {});
	if (which == 0)
	{
		const SuneerRepeatUnit& u = st.units.value(f.unitB);
		PageItem* frame = u.frame;
		if (!frame || !alive(m_doc, frame) || !frame->isTextFrame()
		    || frame->itemText.length() != u.text.size() || frame->itemText.text(0, frame->itemText.length()) != u.text)
		{
			st.status->setText(tr("The text has changed since the check - check again."));
			return;
		}
		UndoTransaction trans;
		if (UndoManager::undoEnabled())
			trans = UndoManager::instance()->beginTransaction(frame->getUName(), frame->getUPixmap(), tr("Delete repeated text"), QString(), Um::IDelete);
		QList<QPair<int, int>> cuts = f.del;
		std::sort(cuts.begin(), cuts.end(), [](const QPair<int, int>& p, const QPair<int, int>& q) { return p.first > q.first; });
		for (const auto& c : std::as_const(cuts))
		{
			if (c.second <= c.first || c.second > frame->itemText.length())
				continue;
			frame->itemText.deselectAll();
			frame->itemText.setCursorPosition(c.first);
			frame->itemText.select(c.first, c.second - c.first);
			frame->HasSel = true;
			frame->asTextFrame()->deleteSelectedTextFromFrame();
		}
		frame->itemText.deselectAll();
		frame->HasSel = false;
		trans.commit();                           // no-op when undo is off
		for (PageItem* x = frame; x; x = x->nextInChain())
			x->invalidateLayout();
		frame->layout();
		st.status->setText(tr("Deleted the repeat - Ctrl+Z brings it back."));
		st.log << QStringLiteral("deleted text %1").arg(f.kind);
	}
	else
	{
		const int item = which == 1 ? f.a : f.b;
		Selection doomed(this, false);
		for (const QPointer<PageItem>& x : st.frames.value(item))
			if (x && alive(m_doc, x) && (f.kind != QLatin1String("photo") || x->isImageFrame()))
				doomed.addItem(x);
		if (doomed.isEmpty())
		{
			st.status->setText(tr("These frames are no longer in the document - check again."));
			return;
		}
		m_doc->m_Selection->clear();
		m_doc->itemSelection_DeleteItem(&doomed);
		st.status->setText(tr("Deleted - Ctrl+Z brings it back."));
		st.log << QStringLiteral("deleted item %1").arg(which);
	}
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	QTimer::singleShot(0, this, [this]() { startRepeatCheck(); });
}

// SUNEER_REPEAT_SELFTEST=<report prefix> (tests/suneer/repeat_on_page.sh).
//  SUNEER_REPEAT_MODE=scan: the whole document once, every entry written (false positives on real pages).
//  otherwise, on a real obituary page: plant, through Scribus' own operations, one obituary again with
//  another id (Item > Duplicate, text retyped with soft hyphens / spaces), one placed twice with the same
//  id, a photo again as a separate frame (a resized / cropped / recompressed copy), a repeated
//  paragraph, sentence and word; then check, click, Ignore, Delete (text, story) + Undo, page scope.
void SuneerNewsPanel::runRepeatSelfTest(const QString& report)
{
	auto lines = std::make_shared<QStringList>();
	auto fails = std::make_shared<int>(0);
	auto check = [lines, fails](const QString& what, bool ok) {
		*lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		*fails += ok ? 0 : 1;
	};
	auto note = [lines](const QString& l) { *lines << QStringLiteral("  ") + l; };
	auto finish = [this, lines, fails, report]() {
		*lines << (*fails ? QStringLiteral("FAILED: %1").arg(*fails) : QStringLiteral("ALL PASS"));
		QFile f(report + QStringLiteral("-report.txt"));
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			f.write(lines->join('\n').toUtf8() + "\n");
		f.close();
		fprintf(stderr, "REPEAT-SELFTEST done\n");
		if (m_repeat && m_repeat->dlg)
			m_repeat->dlg->close();
		if (m_doc)
			m_doc->setModified(false);
		QTimer::singleShot(300, this, [this]() { if (m_mw) m_mw->close(); });
	};
	auto waitFor = [this](std::function<bool()> cond, int ms, std::function<void(bool)> done) {
		auto* t = new QTimer(this);
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		t->setInterval(50);
		connect(t, &QTimer::timeout, this, [t, el, cond, ms, done]() {
			const bool ok = cond();
			if (!ok && el->elapsed() < ms)
				return;
			t->stop();
			t->deleteLater();
			done(ok);
		});
		t->start();
	};
	if (!m_doc || !m_mw)
	{
		check(QStringLiteral("a document is open"), false);
		finish();
		return;
	}
	// the UI keeps answering while a check runs
	auto maxGap = std::make_shared<qint64>(0);
	auto beat = new QTimer(this);
	auto last = std::make_shared<QElapsedTimer>();
	last->start();
	beat->setInterval(20);
	connect(beat, &QTimer::timeout, this, [last, maxGap]() { *maxGap = qMax(*maxGap, last->restart()); });
	auto runCheck = [this, waitFor, beat, last, maxGap](bool whole, std::function<void()> then) {
		*maxGap = 0;
		last->restart();
		beat->start();
		checkRepetition(whole);
		waitFor([this]() { return m_repeat && !m_repeat->running; }, 120000, [beat, then](bool) {
			beat->stop();
			then();
		});
	};

	if (qEnvironmentVariable("SUNEER_REPEAT_MODE") == QLatin1String("scan"))
	{
		runCheck(true, [=]() {
			note(m_dupLog);
			for (int i = 0; i < m_repeat->log.size(); ++i)
			{
				note(QStringLiteral("entry: ") + m_repeat->log[i]);
				const SuneerRepeatFinding& f = m_repeat->findings.value(i);
				for (int it : { f.a, f.b })
				{
					if (it < 0)
						continue;
					const SuneerDupItem& d = m_repeat->items[it];
					QStringList geo;
					for (const QPointer<PageItem>& x : m_repeat->frames.value(it))
						if (x)
							geo << QStringLiteral("%1 %2,%3 %4x%5 layer %6").arg(x->itemName()).arg(x->xPos() - m_doc->Pages->at(qMax(0, topOf(x)->OwnPage))->xOffset(), 0, 'f', 0)
							       .arg(x->yPos() - m_doc->Pages->at(qMax(0, topOf(x)->OwnPage))->yOffset(), 0, 'f', 0).arg(x->width(), 0, 'f', 0).arg(x->height(), 0, 'f', 0).arg(m_doc->layerName(x->m_layerID));
					note(QStringLiteral("    %1 | photo %2 | %3 | \"%4\"").arg(d.module, d.photo.section(QLatin1Char('/'), -2), geo.join(QStringLiteral("; ")),
					     collapseSpaces(QString(d.title + QLatin1Char(' ') + d.body).replace(SpecialChars::PARSEP, QLatin1Char(' ')).remove(SpecialChars::SHYPHEN)).left(110)));
				}
			}
			for (int i = 0; i < m_repeat->findings.size(); ++i)
				if (m_repeat->findings[i].kind == QLatin1String("photo"))
				{
					QFile::copy(m_repeat->items[m_repeat->findings[i].a].photo, report + QStringLiteral("-photo%1-a.%2").arg(i).arg(QFileInfo(m_repeat->items[m_repeat->findings[i].a].photo).suffix()));
					QFile::copy(m_repeat->items[m_repeat->findings[i].b].photo, report + QStringLiteral("-photo%1-b.%2").arg(i).arg(QFileInfo(m_repeat->items[m_repeat->findings[i].b].photo).suffix()));
				}
			*lines << QStringLiteral("ENTRIES %1 ITEMS %2 MS %3 UIPAUSE %4").arg(m_repeat->log.size()).arg(m_repeat->items.size()).arg(m_repeat->ms).arg(*maxGap);
			finish();
		});
		return;
	}

	// ---- plant the repetitions ----
	QList<SuneerDupItem> items;
	QList<QList<PageItem*>> frames;
	dupCollectDocument(m_doc, items, frames);
	QList<int> stories;                                   // obituaries with a photo and a body
	for (int i = 0; i < items.size(); ++i)
		if (!items[i].sid.isEmpty() && !items[i].photo.isEmpty() && items[i].body.size() > 120)
			stories << i;
	if (stories.size() < 6)
	{
		check(QStringLiteral("the page has 6 obituaries with photos (%1)").arg(stories.size()), false);
		finish();
		return;
	}
	auto textFrameOf = [&](int i) -> PageItem* {
		for (PageItem* f : frames[i])
			if (f->isTextFrame() && attr(f, "news.role") == QLatin1String("text"))
				return f;
		return nullptr;
	};
	const int X = stories[0], Y = stories[1], P = stories[2], W = stories[3], S = stories[4], Z = stories[5];
	note(QStringLiteral("X (again, other id) %1 | Y (photo again) %2 | P (paragraph) %3 | W (word) %4 | S (sentence) %5 | Z (same id twice) %6")
	     .arg(items[X].title, items[Y].title, items[P].title, items[W].title, items[S].title, items[Z].title));
	auto duplicate = [this](const QList<PageItem*>& fr) {
		m_doc->m_Selection->clear();
		for (PageItem* f : fr)
			m_doc->m_Selection->addItem(f);
		m_mw->scrActions["itemDuplicate"]->trigger();
		QList<PageItem*> copies;
		for (int k = 0; k < m_doc->m_Selection->count(); ++k)
			copies << m_doc->m_Selection->itemAt(k);
		m_doc->m_Selection->clear();
		return copies;
	};
	// X again with another id, the text retyped (soft hyphens, a double space): a near copy
	const QList<PageItem*> copyX = duplicate(frames[X]);
	for (PageItem* c : copyX)
	{
		setAttr(c, QStringLiteral("news.serverId"), QStringLiteral("rep-copy-x"));
		setAttr(c, QStringLiteral("news.story"), QStringLiteral("rep-story-copy-x"));
		if (c->isTextFrame() && attr(c, "news.role") == QLatin1String("text") && c->prevInChain() == nullptr)
		{
			for (int pos = c->itemText.length() - 7; pos > 2; pos -= 9)
				c->itemText.insertChars(pos, QString(SpecialChars::SHYPHEN));
			const int sp = c->itemText.text(0, c->itemText.length()).indexOf(QLatin1Char(' '));
			if (sp > 0)
				c->itemText.insertChars(sp, QStringLiteral(" "));
			c->invalidateLayout();
		}
	}
	// Z placed twice with the same id (another news.story, as a second placing gives it)
	const QList<PageItem*> copyZ = duplicate(frames[Z]);
	for (PageItem* c : copyZ)
		setAttr(c, QStringLiteral("news.story"), QStringLiteral("rep-story-copy-z"));
	// Y's photo again, a resized / recompressed copy in a frame of its own
	PageItem* photoCopy = nullptr;
	// a copy of Y's photo: 55 %, 4 % cut from each side, JPEG quality 50, another name
	const QString photoFile = report + QStringLiteral("-photo-copy.jpg");
	{
		QImage img(items[Y].photo);
		const QImage small = img.scaled(img.size() * 0.55, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		const int cx = small.width() * 4 / 100, cy = small.height() * 4 / 100;
		small.copy(cx, cy, small.width() - 2 * cx, small.height() - 2 * cy).save(photoFile, "JPEG", 50);
		note(QStringLiteral("photo copy: %1 %2x%3 -> %4").arg(QFileInfo(items[Y].photo).fileName()).arg(img.width()).arg(img.height()).arg(QFileInfo(photoFile).fileName()));
	}
	{
		ScPage* pg = m_doc->Pages->at(0);
		const int k = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, pg->xOffset() + 20, pg->yOffset() + pg->height() - 140, 90, 110, 0,
		                             CommonStrings::None, CommonStrings::None);
		photoCopy = m_doc->Items->at(k);
		photoCopy->setItemName(QStringLiteral("rep_photo_copy"));
		m_doc->loadPict(photoFile, photoCopy);
		photoCopy->setImageScalingMode(false, true);
		photoCopy->adjustPictScale();
	}
	// repeated paragraph / sentence / word
	PageItem* tP = textFrameOf(P);
	PageItem* tW = textFrameOf(W);
	PageItem* tS = textFrameOf(S);
	const QString beforeP = tP ? tP->itemText.text(0, tP->itemText.length()) : QString();
	QString paraText, sentText, wordText;
	if (tP)
	{
		const QList<Seg> ps = paragraphs(beforeP);
		for (const Seg& p : ps)
			if (p.norm.size() >= kMinParagraph)
			{
				paraText = beforeP.mid(p.from, p.to - p.from);
				break;
			}
		tP->itemText.insertChars(tP->itemText.length(), QString(SpecialChars::PARSEP) + paraText, true);
		tP->invalidateLayout();
	}
	if (tS)
	{
		const QString t = tS->itemText.text(0, tS->itemText.length());
		for (const Seg& s : sentences(t))
			if (s.norm.size() >= kMinSentenceInStory + 5 && s.to < t.size())
			{
				sentText = t.mid(s.from, s.to - s.from);
				tS->itemText.insertChars(s.to, QStringLiteral(" ") + sentText, true);
				break;
			}
		tS->invalidateLayout();
	}
	if (tW)
	{
		const QString t = tW->itemText.text(0, tW->itemText.length());
		int at = t.indexOf(QStringLiteral("അന്തരിച്ചു"));
		if (at >= 0)
			wordText = QStringLiteral("അന്തരിച്ചു");
		else
		{
			static const QRegularExpression w(QStringLiteral("[\\x{0D00}-\\x{0D7F}]{6,}"));
			const auto m = w.match(t, t.indexOf(SpecialChars::PARSEP) + 1);
			at = m.capturedStart();
			wordText = m.captured(0);
		}
		if (at >= 0)
			tW->itemText.insertChars(at + wordText.size(), QStringLiteral(" ") + wordText, true);
		tW->invalidateLayout();
	}
	m_doc->changed();
	m_mw->view->DrawNew();
	note(QStringLiteral("planted: paragraph \"%1\" | sentence \"%2\" | word \"%3\" | photo %4")
	     .arg(paraText.left(40), sentText.left(40), wordText, QFileInfo(photoFile).fileName()));

	auto findKind = [this](const QString& kind, std::function<bool(const SuneerRepeatFinding&)> also = nullptr) {
		for (int i = 0; i < m_repeat->findings.size(); ++i)
			if (m_repeat->findings[i].kind == kind && (!also || also(m_repeat->findings[i])))
				return i;
		return -1;
	};
	auto isItem = [this](int item, const QString& story) {
		for (const QPointer<PageItem>& f : m_repeat->frames.value(item))
			if (f && attr(f, "news.story") == story)
				return true;
		return false;
	};
	auto itemIs = [this](int item, PageItem* frame) {
		for (const QPointer<PageItem>& f : m_repeat->frames.value(item))
			if (f == frame)
				return true;
		return false;
	};
	const QString storyX = attr(frames[X].first(), "news.story"), storyZ = attr(frames[Z].first(), "news.story");
	const QString sidZ = items[Z].sid;

	// ---- 1. the check on this page ----
	runCheck(false, [=]() {
		note(m_dupLog);
		for (const QString& l : std::as_const(m_repeat->log))
			note(QStringLiteral("entry: ") + l);
		check(QStringLiteral("check ran in the background (longest UI pause %1 ms, %2 ms)").arg(*maxGap).arg(m_repeat->ms), *maxGap < 400);
		const int fNews = findKind("news", [&](const SuneerRepeatFinding& f) {
			return (isItem(f.a, storyX) && isItem(f.b, QStringLiteral("rep-story-copy-x"))) || (isItem(f.b, storyX) && isItem(f.a, QStringLiteral("rep-story-copy-x"))); });
		const int fStory = findKind("story", [&](const SuneerRepeatFinding& f) {
			return (isItem(f.a, storyZ) && isItem(f.b, QStringLiteral("rep-story-copy-z"))) || (isItem(f.b, storyZ) && isItem(f.a, QStringLiteral("rep-story-copy-z"))); });
		const int fPhoto = findKind("photo", [&](const SuneerRepeatFinding& f) { return itemIs(f.a, photoCopy) || itemIs(f.b, photoCopy); });
		const int fPara = findKind("paragraph", [&](const SuneerRepeatFinding& f) { return f.unitA >= 0 && m_repeat->units[f.unitA].frame == tP; });
		const int fSent = findKind("sentence", [&](const SuneerRepeatFinding& f) { return f.unitA >= 0 && m_repeat->units[f.unitA].frame == tS; });
		const int fWord = findKind("word", [&](const SuneerRepeatFinding& f) { return f.unitA >= 0 && m_repeat->units[f.unitA].frame == tW; });
		check(QStringLiteral("same obituary with another id found (news %1 %)").arg(fNews >= 0 ? m_repeat->findings[fNews].score : -1), fNews >= 0);
		check(QStringLiteral("same story id placed twice found"), fStory >= 0);
		check(QStringLiteral("the photo used again (resized copy, own frame) found (%1 %)").arg(fPhoto >= 0 ? m_repeat->findings[fPhoto].score : -1), fPhoto >= 0);
		check(QStringLiteral("repeated paragraph found"), fPara >= 0);
		check(QStringLiteral("repeated sentence found"), fSent >= 0);
		check(QStringLiteral("repeated word \"%1\" found").arg(wordText), fWord >= 0);
		const QSet<int> expected { fNews, fStory, fPhoto, fPara, fSent, fWord };
		QStringList extra;
		for (int i = 0; i < m_repeat->findings.size(); ++i)
			if (!expected.contains(i))
				extra << m_repeat->log.value(i);
		check(QStringLiteral("no other entry on the real page (false positives: %1)").arg(extra.size()), extra.isEmpty());
		for (const QString& e : std::as_const(extra))
			note(QStringLiteral("other: ") + e);

		m_repeat->dlg->grab().save(report + QStringLiteral("-panel.png"));
		m_mw->grab().save(report + QStringLiteral("-window.png"));
		// ---- 2. click: both selected, in view, yellow; the mark never reaches a render of the page ----
		const QImage plain = m_mw->view->PageToPixmap(0, 700);
		auto clickCheck = [&](int idx, const QString& what, int minSel) {
			if (idx < 0)
				return;
			const double before = m_mw->view->m_canvas->scale();
			showRepeat(idx);
			QCoreApplication::processEvents();
			const QRectF vis = m_mw->view->visibleCanvasRect();
			bool inView = true;
			for (int k = 0; k < m_doc->m_Selection->count(); ++k)
				inView = inView && vis.contains(topOf(m_doc->m_Selection->itemAt(k))->getVisualBoundingRect().center());
			check(QStringLiteral("click %1: %2 frame(s) selected, all in view, marked (zoom %3 -> %4)")
			      .arg(what).arg(m_doc->m_Selection->count()).arg(before, 0, 'f', 2).arg(m_mw->view->m_canvas->scale(), 0, 'f', 2),
			      m_doc->m_Selection->count() >= minSel && inView && !SuneerDupHighlights::isEmpty());
			m_mw->view->m_canvas->repaint();
			m_mw->view->viewport()->grab().save(report + QStringLiteral("-click-%1.png").arg(what));
		};
		clickCheck(fNews, QStringLiteral("news"), 2);
		clickCheck(fPhoto, QStringLiteral("photo"), 2);
		clickCheck(fPara, QStringLiteral("paragraph"), 1);
		clickCheck(fWord, QStringLiteral("word"), 1);
		const QImage marked = m_mw->view->PageToPixmap(0, 700);
		check(QStringLiteral("the yellow mark is not in a render of the page (%1x%2, identical with and without)").arg(marked.width()).arg(marked.height()),
		      !marked.isNull() && marked == plain);

		// ---- 3. Ignore: gone, and stays gone on the next check ----
		const int rowsBefore = m_repeat->tree->topLevelItemCount();
		ignoreRepeat(fWord);
		check(QStringLiteral("Ignore hides the word entry (%1 -> %2 rows)").arg(rowsBefore).arg(m_repeat->tree->topLevelItemCount()),
		      m_repeat->tree->topLevelItemCount() == rowsBefore - 1);

		// ---- 4. Delete the repeated paragraph: the text is as before; Undo brings it back ----
		const QString withRepeat = tP->itemText.text(0, tP->itemText.length());
		deleteRepeat(fPara, 0);
		waitFor([this]() { return m_repeat && !m_repeat->running; }, 60000, [=](bool) {
			const QString now = tP->itemText.text(0, tP->itemText.length());
			check(QStringLiteral("Delete the second paragraph: the story is as before the paste (%1 -> %2 chars), the entry is gone; Ignore kept (%3)")
			      .arg(withRepeat.size()).arg(now.size()).arg(m_dupLog.contains(QStringLiteral("1 ignored"))),
			      now == beforeP && findKind("paragraph") < 0 && m_dupLog.contains(QStringLiteral("1 ignored")));
			UndoManager::instance()->undo(1);
			QCoreApplication::processEvents();
			check(QStringLiteral("Undo brings the paragraph back"), tP->itemText.text(0, tP->itemText.length()) == withRepeat);

			// ---- 5. Delete one of a story placed twice (same id): no Unused mark for it ----
			startRepeatCheck();
			waitFor([this]() { return m_repeat && !m_repeat->running; }, 60000, [=](bool) {
				const int fs = findKind("story");
				if (fs < 0)
				{
					check(QStringLiteral("story entry still there"), false);
					finish();
					return;
				}
				const int copyIs = isItem(m_repeat->findings[fs].a, QStringLiteral("rep-story-copy-z")) ? 1 : 2;
				deleteRepeat(fs, copyIs);
				// the delete hook waits 2 s before marking
				QTimer::singleShot(3500, this, [=]() {
					QFile logf(QDir::homePath() + QStringLiteral("/scribus-crashlogs/news-sync.log"));
					const QString log = logf.open(QIODevice::ReadOnly) ? QString::fromUtf8(logf.readAll()) : QString();
					int copiesLeft = 0, origLeft = 0;
					for (int i = 0; i < m_doc->Items->count(); ++i)
					{
						copiesLeft += attr(m_doc->Items->at(i), "news.story") == QLatin1String("rep-story-copy-z") ? 1 : 0;
						origLeft += attr(m_doc->Items->at(i), "news.story") == storyZ ? 1 : 0;
					}
					check(QStringLiteral("Delete the copy of a story placed twice: copy gone (%1 frames left), original kept (%2), no Unused mark for %3")
					      .arg(copiesLeft).arg(origLeft).arg(sidZ.right(6)),
					      copiesLeft == 0 && origLeft > 0 && !log.contains(sidZ));
					// the near copy has its own id: deleting it marks that id (the fix does not block real marks)
					const int fn = findKind("news");
					if (fn >= 0)
						deleteRepeat(fn, isItem(m_repeat->findings[fn].a, QStringLiteral("rep-story-copy-x")) ? 1 : 2);
					QTimer::singleShot(3500, this, [=]() {
						QFile lf(QDir::homePath() + QStringLiteral("/scribus-crashlogs/news-sync.log"));
						const QString l2 = lf.open(QIODevice::ReadOnly) ? QString::fromUtf8(lf.readAll()) : QString();
						check(QStringLiteral("deleting the only copy of an id still marks it Unused (rep-copy-x in the log)"), l2.contains(QStringLiteral("rep-copy-x")));
						// ---- 6. page scope: the photo copy on page 2 is not "this page"; whole document finds it ----
						m_doc->addPage(1);
						m_doc->reformPages();
						ScPage* p2 = m_doc->Pages->at(1);
						photoCopy->setXYPos(p2->xOffset() + 30, p2->yOffset() + 40);
						photoCopy->setOwnerPage(1);
						m_mw->view->DrawNew();
						m_doc->setCurrentPage(m_doc->Pages->at(0));
						runCheck(false, [=]() {
							const bool onThis = findKind("photo") >= 0;
							runCheck(true, [=]() {
								check(QStringLiteral("scope: the photo copy moved to page 2 - this page: %1, whole document: %2")
								      .arg(onThis ? "found" : "not listed").arg(findKind("photo") >= 0 ? "found" : "not listed"),
								      !onThis && findKind("photo") >= 0);
								photoCopy->setXYPos(m_doc->Pages->at(0)->xOffset() - 400, p2->yOffset() + 40);
								photoCopy->setOwnerPage(-1);
								runCheck(true, [=]() {
									check(QStringLiteral("a frame on the pasteboard is never checked (photo entry: %1)").arg(findKind("photo") >= 0 ? "listed" : "none"),
									      findKind("photo") < 0);
									beat->deleteLater();
									finish();
								});
							});
						});
					});
				});
			});
		});
	});
}
