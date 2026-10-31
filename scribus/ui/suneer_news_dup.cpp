/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/*
 * Suneer News Browser: duplicate news and duplicate photos.
 *
 *  - the list: a "DUP" badge on a story that repeats another story of the list or one already in
 *    the document (text: the Duplicate News Checker's engine, DupNews; photos: SuneerPhotoHash), or
 *    that the workflow says is used on another page; "Show: no DUP" hides the repeats;
 *  - placing: "This story / photo is already on the page (module X)" [Place anyway] [Skip] [Show];
 *  - "Check repetition on page" (suneer_news_repeat.cpp) reuses scanPairs for its stories and photos.
 * Text and photos are compared on a worker thread (QtConcurrent); the GUI thread only snapshots.
 */
#include "suneer_news_panel.h"
#include "suneer_news_dup_p.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "pageitem.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "commonstrings.h"
#include "scpage.h"
#include "selection.h"
#include "suneerduplicatenews.h"
#include "suneerphotohash.h"
#include "text/specialchars.h"

namespace
{
	using DupItem = SuneerDupItem;
	using DupPair = SuneerDupPair;
	using SuneerDupScan::scanPairs;

	// "(73)" / "73 വയസ്സ്" - the age an obituary states; two different ages are two different people
	QSet<int> agesOf(const QString& text)
	{
		static const QRegularExpression re(QStringLiteral("\\((\\d{1,3})\\)|(\\d{1,3})\\s*വയസ്സ"));
		QSet<int> ages;
		auto it = re.globalMatch(text);
		while (it.hasNext())
		{
			const auto m = it.next();
			const int age = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).toInt();
			if (age > 0 && age < 130)
				ages.insert(age);
		}
		return ages;
	}
	int dupThreshold()
	{
		return SuneerDupScan::threshold();
	}
}

// The worker: every duplicate pair of \a items. Thread-safe (no GUI, no document).
QList<SuneerDupPair> SuneerDupScan::scanPairs(const QList<SuneerDupItem>& items, int threshold, int photoBits)
{
	QList<SuneerDupPair> out;
	QSet<QPair<int, int>> seen;
	auto add = [&](int a, int b, const QString& kind, int score) {
		if (a > b)
			std::swap(a, b);
		const QPair<int, int> key(a, b);
		if (seen.contains(key))
			return;
		seen.insert(key);
		out.append(SuneerDupPair { a, b, kind, score });
	};
	// 1. the same workflow story twice
	QHash<QString, int> firstOfSid;
	for (int i = 0; i < items.size(); ++i)
	{
		if (items[i].sid.isEmpty())
			continue;
		const auto it = firstOfSid.constFind(items[i].sid);
		if (it != firstOfSid.constEnd())
			add(*it, i, QStringLiteral("story"), 100);
		else
			firstOfSid.insert(items[i].sid, i);
	}
	// 2. the text: the Duplicate News Checker's engine (normalised Malayalam, 7-char shingles)
	DupNews::Job job;
	job.options.threshold = threshold;
	job.options.includePasteboard = true;
	job.options.includeCaptions = true;
	DupNews::Source src;
	src.label = QStringLiteral("news");
	src.live = true;
	job.sources.append(src);
	for (const SuneerDupItem& d : items)
	{
		DupNews::Story s;
		s.source = 0;
		s.name = d.module;
		DupNews::FrameRef f;
		f.name = d.module;
		f.page = 0;
		s.frames.append(f);
		QString title = d.title;
		title.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(SpecialChars::PARSEP, QLatin1Char(' '));
		s.text = title;
		DupNews::Para head;
		head.start = 0;
		head.end = title.size();
		head.styles << QStringLiteral("head");
		s.paras.append(head);
		if (!d.body.isEmpty())
		{
			s.text += SpecialChars::PARSEP;
			DupNews::Para body;
			body.start = s.text.size();
			s.text += d.body;
			body.end = s.text.size();
			body.styles << QStringLiteral("body");
			s.paras.append(body);
		}
		job.stories.append(s);
	}
	const DupNews::Result r = DupNews::run(job, nullptr);
	for (const DupNews::Group& g : r.groups)
	{
		for (int x = 0; x < g.members.size(); ++x)
			for (int y = x + 1; y < g.members.size(); ++y)
			{
				const int a = g.members[x].story, b = g.members[y].story;
				if (a < 0 || b < 0 || a >= items.size() || b >= items.size())
					continue;
				// a headline alone (same name) is not the same obituary when both have a body
				if (g.kind == DupNews::Kind::Headline && r.stories[a].bodyNorm.size() >= 60 && r.stories[b].bodyNorm.size() >= 60)
					continue;
				const QSet<int> agesA = agesOf(items[a].title + QLatin1Char(' ') + items[a].body);
				const QSet<int> agesB = agesOf(items[b].title + QLatin1Char(' ') + items[b].body);
				if (!agesA.isEmpty() && !agesB.isEmpty() && !agesA.intersects(agesB))
					continue;                                  // two different ages: two different people
				add(a, b, QStringLiteral("news"), qMin(g.members[x].score, g.members[y].score));
			}
	}
	// 3. the photos
	QVector<SuneerPhotoHash::Hash> hashes(items.size());
	for (int i = 0; i < items.size(); ++i)
		if (!items[i].photo.isEmpty())
			hashes[i] = SuneerPhotoHash::ofFile(items[i].photo);
	for (int i = 0; i < items.size(); ++i)
		for (int j = i + 1; j < items.size(); ++j)
		{
			if (!hashes[i].ok || !hashes[j].ok)
				continue;
			int d = 64;
			if (SuneerPhotoHash::samePhoto(hashes[i], hashes[j], photoBits, &d))
			{
				QSet<QPair<int, int>> dummy;
				const QPair<int, int> key(i, j);
				if (seen.contains(key))
				{
					// already a text / id pair: the photo is part of the same duplicate
					continue;
				}
				add(i, j, QStringLiteral("photo"), SuneerPhotoHash::percent(d));
			}
		}
	return out;
}

int SuneerDupScan::threshold()
{
	QSettings s(QStringLiteral("Faircode"), QStringLiteral("ScribusNews"));
	return qBound(50, s.value(QStringLiteral("dup/threshold"), 85).toInt(), 100);
}

// The stories and photos of the document: News Browser stories by their news.story, every other
// text chain and image frame on its own. Each item remembers its frames (index-matched) for jumping.
void SuneerNewsPanel::dupCollectDocument(ScribusDoc* doc, QList<SuneerDupItem>& items, QList<QList<PageItem*>>& frames)
{
	if (!doc)
		return;
	// items may already hold the list's stories: frames[k] belongs to items[base + k]
	const int base = items.size();
	QHash<QString, int> byStory;
	QList<PageItem*> all;
	for (int i = 0; i < doc->Items->count(); ++i)
	{
		PageItem* it = doc->Items->at(i);
		all << it;
		if (it->isGroup())
			all << it->getAllChildren();
	}
	auto textOf = [](PageItem* f, QString& title, QString& body) {
		const QStringList paras = f->itemText.text(0, f->itemText.length()).split(SpecialChars::PARSEP);
		title = paras.value(0).trimmed();
		body = paras.mid(1).join(SpecialChars::PARSEP);
	};
	for (PageItem* it : std::as_const(all))
	{
		if (it->isGroup())
			continue;
		const QString story = attr(it, "news.story");
		if (!story.isEmpty())
		{
			int idx = byStory.value(story, -1);
			if (idx < 0)
			{
				idx = items.size();
				byStory.insert(story, idx);
				DupItem d;
				d.module = story;
				d.sid = attr(it, "news.serverId");
				d.onPage = true;
				items.append(d);
				frames.append(QList<PageItem*>());
			}
			frames[idx - base] << it;
			const QString role = attr(it, "news.role");
			if (it->isTextFrame() && (role == QLatin1String("text") || items[idx].title.isEmpty()) && role != QLatin1String("caption"))
				textOf(it, items[idx].title, items[idx].body);
			else if (it->isImageFrame() && items[idx].photo.isEmpty())
				items[idx].photo = SuneerPhotoHash::sourceFileOf(it);
			continue;
		}
		if (it->isTextFrame() && it->prevInChain() == nullptr && it->itemText.length() > 0 && !it->itemName().startsWith(QLatin1String("caption_")))
		{
			DupItem d;
			d.module = it->itemName();
			d.onPage = true;
			textOf(it, d.title, d.body);
			items.append(d);
			frames.append(QList<PageItem*> { it });
		}
		else if (it->isImageFrame() && !it->Pfile.isEmpty())
		{
			DupItem d;
			d.module = it->itemName();
			d.onPage = true;
			d.photo = SuneerPhotoHash::sourceFileOf(it);
			items.append(d);
			frames.append(QList<PageItem*> { it });
		}
	}
}

SuneerDupItem SuneerNewsPanel::dupListItem(const QJsonObject& n, int index, std::function<QString(const QString&)> cached)
{
	DupItem d;
	d.sid = newsServerId(n);
	d.title = collapseSpaces(n["title"].toString());
	d.body = contentToParagraphs(n["content"].toString()).join(SpecialChars::PARSEP);
	d.module = d.title.left(40);
	d.listIndex = index;
	const QJsonArray media = mediaList(n);
	if (!media.isEmpty())
	{
		const QString url = media[0].toObject()["media"].toObject()["fileUrl"].toString();
		d.photo = (url.startsWith(QLatin1Char('/')) && QFileInfo(url).isFile()) ? url : cached(url);   // only what is downloaded already
	}
	return d;
}

// After Fetch News: compare the list with itself and with the document, in the background.
void SuneerNewsPanel::startDuplicateScan()
{
	if (!m_newsList)
		return;
	const int gen = ++m_dupGen;
	SuneerPhotoHash::loadCache(SuneerPhotoHash::cacheFileFor(m_doc));
	QList<DupItem> items;
	QList<QList<PageItem*>> frames;
	for (int i = 0; i < m_newsData.size(); ++i)
		items << dupListItem(m_newsData[i].toObject(), i, [this](const QString& u) { return cachedImagePath(u); });
	const int listCount = items.size();
	dupCollectDocument(m_doc, items, frames);
	const int threshold = dupThreshold(), bits = SuneerPhotoHash::maxDistance();
	auto* w = new QFutureWatcher<QList<DupPair>>(this);
	auto t = std::make_shared<QElapsedTimer>();
	t->start();
	connect(w, &QFutureWatcher<QList<DupPair>>::finished, this, [this, w, gen, items, listCount, t]() {
		w->deleteLater();
		if (gen != m_dupGen)
			return;                                         // a newer list: this answer is old
		applyDuplicateBadges(items, listCount, w->result(), t->elapsed());
		SuneerPhotoHash::saveCache(SuneerPhotoHash::cacheFileFor(m_doc));
	});
	w->setFuture(QtConcurrent::run([items, threshold, bits]() { return scanPairs(items, threshold, bits); }));
}

void SuneerNewsPanel::applyDuplicateBadges(const QList<SuneerDupItem>& items, int listCount, const QList<SuneerDupPair>& pairs, qint64 ms)
{
	// list index -> lines of "why", and whether the row is a repeat (hidden by "Show: no DUP")
	QHash<int, QStringList> why;
	QHash<int, bool> repeat;
	auto line = [](const DupPair& p, const DupItem& other, bool onPage) {
		const QString what = p.kind == QLatin1String("photo") ? QObject::tr("Same photo as") : QObject::tr("Same as");
		return QStringLiteral("%1%2: %3 (%4 %)").arg(what, onPage ? QObject::tr(" on the page") : QString(), other.title.left(50)).arg(p.score);
	};
	for (const DupPair& p : pairs)
	{
		const bool aList = p.a < listCount, bList = p.b < listCount;
		if (aList && bList)
		{
			why[p.a] << line(p, items[p.b], false);
			why[p.b] << line(p, items[p.a], false);
			repeat[p.b] = true;                             // the later one is the repeat
		}
		else if (aList || bList)
		{
			const int l = aList ? p.a : p.b, o = aList ? p.b : p.a;
			if (p.kind == QLatin1String("story"))
				continue;                                   // the story itself, placed: its own USD status says it
			why[l] << line(p, items[o], true);
			repeat[l] = true;
		}
	}
	// the workflow: used on another page (its own record)
	for (int i = 0; i < m_newsData.size(); ++i)
	{
		const QJsonObject n = m_newsData[i].toObject();
		QJsonArray other = n["otherPages"].toArray();
		for (const QJsonValue& v : n["newsPages"].toArray())
			other.append(v);
		for (const QJsonValue& v : std::as_const(other))
		{
			const QJsonObject p = v.toObject();
			const QString pid = p["pageId"].toString().isEmpty() ? p["page"].toObject()["id"].toString() : p["pageId"].toString();
			if (!pid.isEmpty() && pid != m_fetchPageId && p["dtpStatus"].toString().toUpper() == QLatin1String("USED"))
			{
				const QString name = p["page"].toObject()["name"].toString();
				why[i] << tr("Already used on %1 (workflow)").arg(name.isEmpty() ? pid.right(6) : name);
				repeat[i] = true;
				break;
			}
		}
	}
	int badged = 0;
	for (int r = 0; r < m_newsList->count(); ++r)
	{
		QListWidgetItem* row = m_newsList->item(r);
		const int idx = row->data(Qt::UserRole).toInt();
		QStringList lines = why.value(idx);
		lines.removeDuplicates();
		row->setData(Qt::UserRole + 8, lines.mid(0, 4).join(QLatin1Char('\n')));
		row->setData(Qt::UserRole + 9, repeat.value(idx, false));
		badged += lines.isEmpty() ? 0 : 1;
	}
	m_dupLog = QStringLiteral("duplicates: %1 stories in the list, %2 in the document, %3 pairs, %4 badged, %5 ms")
	           .arg(listCount).arg(items.size() - listCount).arg(pairs.size()).arg(badged).arg(ms);
	apiLog(m_dupLog);
	refreshPlacedMarks();                                   // tooltips; then the filter
	m_newsList->viewport()->update();
	m_dupScanDone = true;
}

// Before placing: each story against the document (and the others placed with it).
bool SuneerNewsPanel::confirmDuplicatesBeforePlacing(QList<QJsonObject>& stories)
{
	if (!m_doc || stories.isEmpty())
		return true;
	QList<DupItem> items;
	QList<QList<PageItem*>> frames;
	for (int i = 0; i < stories.size(); ++i)
	{
		DupItem d = dupListItem(stories[i], i, [this](const QString& u) { QString why; return downloadImage(u, &why); });
		items << d;
	}
	const int count = items.size();
	dupCollectDocument(m_doc, items, frames);
	const QList<DupPair> pairs = scanPairs(items, dupThreshold(), SuneerPhotoHash::maxDistance());
	QSet<int> skip;
	for (int s = 0; s < count; ++s)
	{
		if (skip.contains(s))
			continue;
		QStringList lines;
		QList<PageItem*> show;
		for (const DupPair& p : pairs)
		{
			if (p.a != s && p.b != s)
				continue;
			const int o = p.a == s ? p.b : p.a;
			if (o < count)
			{
				if (!skip.contains(o))
					lines << tr("It repeats \"%1\" placed with it (%2 %)").arg(items[o].title.left(50)).arg(p.score);
				continue;
			}
			const QString module = items[o].module;
			if (p.kind == QLatin1String("story"))
				lines << tr("This story is already on the page (module %1)").arg(module);
			else if (p.kind == QLatin1String("photo"))
				lines << tr("Its photo is already on the page (module %1, %2 %)").arg(module).arg(p.score);
			else
				lines << tr("This story is already on the page as \"%1\" (module %2, %3 %)").arg(items[o].title.left(50), module).arg(p.score);
			if (show.isEmpty())
				show = frames.value(o - count);
		}
		if (lines.isEmpty())
			continue;
		QString answer = m_dupTestAnswer;
		const QString text = tr("\"%1\"").arg(items[s].title.left(60)) + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n'));
		m_dupTestLog << QStringLiteral("warning: ") + text.simplified();
		if (answer.isEmpty())
		{
			QMessageBox box(QMessageBox::Warning, tr("Duplicate"), text, QMessageBox::NoButton, this);
			QPushButton* place = box.addButton(tr("Place anyway"), QMessageBox::AcceptRole);
			QPushButton* skipBtn = box.addButton(tr("Skip"), QMessageBox::RejectRole);
			QPushButton* showBtn = show.isEmpty() ? nullptr : box.addButton(tr("Show"), QMessageBox::ActionRole);
			box.setDefaultButton(skipBtn);
			box.exec();
			answer = box.clickedButton() == place ? QStringLiteral("place") : (showBtn && box.clickedButton() == showBtn) ? QStringLiteral("show") : QStringLiteral("skip");
		}
		apiLog(QStringLiteral("duplicate on placing \"%1\": %2 -> %3").arg(items[s].title.left(30), lines.join(QStringLiteral("; ")), answer));
		if (answer == QLatin1String("place"))
			continue;
		skip.insert(s);
		if (answer == QLatin1String("show") && !show.isEmpty() && m_mw)
		{
			m_mw->selectItemsFromOutlines(show.first());
			for (PageItem* f : std::as_const(show))
				m_doc->m_Selection->addItem(f);
		}
	}
	if (skip.isEmpty())
		return true;
	QList<QJsonObject> kept;
	for (int i = 0; i < stories.size(); ++i)
		if (!skip.contains(i))
			kept << stories[i];
	setStatus(tr("%n duplicate(s) not placed", "", skip.size()));
	stories = kept;
	return !stories.isEmpty();
}

// A row dragged onto a page model slot: the same question as Place Selected. Run after the drop event
// has returned (a modal box inside QDrag's own loop is asking for trouble); the slot is looked up again.
void SuneerNewsPanel::handleStoryDrop(PageItem* slot, int newsIndex)
{
	QPointer<ScribusDoc> doc = m_doc;
	QTimer::singleShot(0, this, [this, doc, slot, newsIndex]() {
		if (!doc || doc != m_doc || newsIndex < 0 || newsIndex >= m_newsData.size())
			return;
		bool alive = false;
		for (int i = 0; i < m_doc->Items->count() && !alive; ++i)
			alive = m_doc->Items->at(i) == slot;
		if (!alive || !isSlotPlaceholder(slot))
			return;
		QList<QJsonObject> one { m_newsData[newsIndex].toObject() };
		if (!confirmDuplicatesBeforePlacing(one))
		{
			m_dupTestLog << QStringLiteral("drop: not placed");
			return;
		}
		const bool ok = dropStoryOnItem(slot, newsIndex);
		m_dupTestLog << QStringLiteral("drop: placed %1").arg(ok);
	});
}

// SUNEER_DUP_SELFTEST=<report prefix> (tests/suneer/news_duplicates.sh, fixture mode): a real obituary
// page with its photos, and a list of its 30 stories + a repeated obituary (new id, same text with
// spaces / soft hyphens / ZWNJ changed, its photo resized + recompressed + cropped + renamed), a story
// whose photo repeats one on the page, a new near-duplicate pair made from a page obituary with
// another name and age, and 120 filler stories. SUNEER_DUP_EXPECT = the ids that must get DUP.
void SuneerNewsPanel::runDupSelfTest(const QString& report)
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
		fprintf(stderr, "DUP-SELFTEST done\n");
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
	const QSet<QString> expect = [] {
		const QStringList l = qEnvironmentVariable("SUNEER_DUP_EXPECT").split(QLatin1Char(','), Qt::SkipEmptyParts);
		return QSet<QString>(l.begin(), l.end());
	}();
	for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
		if (auto* sw = qobject_cast<QStackedWidget*>(w->parentWidget()))
			sw->setCurrentWidget(w);
	// 1. the list -> the background scan -> badges; the UI keeps answering meanwhile
	auto maxGap = std::make_shared<qint64>(0);
	auto beat = new QTimer(this);
	auto last = std::make_shared<QElapsedTimer>();
	last->start();
	beat->setInterval(20);
	connect(beat, &QTimer::timeout, this, [last, maxGap]() { *maxGap = qMax(*maxGap, last->restart()); });
	onFetchNews();
	last->restart();
	beat->start();
	waitFor([this]() { return m_dupScanDone; }, 120000, [=](bool done) {
		beat->stop();
		beat->deleteLater();
		note(m_dupLog);
		QSet<QString> badged;
		QStringList shown;
		for (int r = 0; r < m_newsList->count(); ++r)
		{
			QListWidgetItem* row = m_newsList->item(r);
			const QString tip = row->data(Qt::UserRole + 8).toString();
			if (tip.isEmpty())
				continue;
			const QString sid = newsServerId(m_newsData[row->data(Qt::UserRole).toInt()].toObject());
			badged.insert(sid);
			shown << QStringLiteral("%1 \"%2\": %3").arg(sid, collapseSpaces(m_newsData[row->data(Qt::UserRole).toInt()].toObject()["title"].toString()).left(30), tip.simplified());
		}
		for (const QString& l : std::as_const(shown))
			note(QStringLiteral("DUP ") + l);
		const QSet<QString> missed = expect - badged, extra = badged - expect;
		check(QStringLiteral("list scan done in the background (longest UI pause %1 ms, %2 stories)").arg(*maxGap).arg(m_newsData.size()), done && *maxGap < 400);
		check(QStringLiteral("DUP badges: all %1 expected found (missed: %2)").arg(expect.size()).arg(QStringList(missed.values()).join(',')), missed.isEmpty());
		check(QStringLiteral("no false DUP among the other %1 stories (extra: %2)").arg(m_newsData.size() - expect.size()).arg(QStringList(extra.values()).join(',')), extra.isEmpty());
		{
			// the News tab on screen (the test profile has it off), the planted repeats in view
			m_mw->suneerSetNewsBrowserTabVisible(true);
			for (QWidget* x = m_newsList; x && x->parentWidget(); x = x->parentWidget())
				if (auto* tw = qobject_cast<QTabWidget*>(x->parentWidget()->parentWidget()); tw && tw->indexOf(x) >= 0)
					tw->setCurrentWidget(x);
			for (int r = 0; r < m_newsList->count(); ++r)
				if (newsServerId(m_newsData[m_newsList->item(r)->data(Qt::UserRole).toInt()].toObject()) == QLatin1String("dup-1"))
					m_newsList->scrollToItem(m_newsList->item(r), QAbstractItemView::PositionAtCenter);
			for (int i = 0; i < 20; ++i)
				QCoreApplication::processEvents();
			m_mw->grab().save(qEnvironmentVariable("SUNEER_DUP_SELFTEST") + QStringLiteral("-list.png"));
		}
		// 2. Show: no DUP hides the repeats, keeps the first of a pair
		m_filterCombo->setCurrentIndex(m_filterCombo->findData("NODUP"));
		applyListFilter(false);
		QStringList hidden;
		for (int r = 0; r < m_newsList->count(); ++r)
			if (m_newsList->item(r)->isHidden())
				hidden << newsServerId(m_newsData[m_newsList->item(r)->data(Qt::UserRole).toInt()].toObject());
		hidden.sort();
		check(QStringLiteral("Show: no DUP hides %1 (the repeats; dup-3, the first of its pair, stays)").arg(hidden.join(',')),
		      hidden == QStringList({ QStringLiteral("dup-1"), QStringLiteral("dup-1b"), QStringLiteral("dup-2"), QStringLiteral("dup-4") }));
		m_filterCombo->setCurrentIndex(m_filterCombo->findData("ALL"));
		applyListFilter(false);
		// 3. placing the repeated obituary: warned (story + photo on the page); Skip -> not placed, Place anyway -> placed
		auto tick = [this](const QString& sid) {
			for (int r = 0; r < m_newsList->count(); ++r)
			{
				QListWidgetItem* row = m_newsList->item(r);
				const bool want = newsServerId(m_newsData[row->data(Qt::UserRole).toInt()].toObject()) == sid;
				row->setCheckState(want ? Qt::Checked : Qt::Unchecked);
			}
		};
		auto framesOf = [this](const QString& sid) {
			int n = 0;
			for (int i = 0; i < m_doc->Items->count(); ++i)
				n += attr(m_doc->Items->at(i), "news.serverId") == sid ? 1 : 0;
			return n;
		};
		tick(QStringLiteral("dup-1"));
		m_dupTestAnswer = QStringLiteral("skip");
		m_dupTestLog.clear();
		onPlaceSelected();
		note(m_dupTestLog.join(QStringLiteral(" | ")));
		const QString warn = m_dupTestLog.join(' ');
		check(QStringLiteral("placing dup-1: warned \"already on the page\" (text %1, photo %2); Skip -> not placed (%3 frames)")
		      .arg(warn.contains(QStringLiteral("already on the page as"))).arg(warn.contains(QStringLiteral("photo is already on the page"))).arg(framesOf("dup-1")),
		      warn.contains(QStringLiteral("already on the page")) && framesOf("dup-1") == 0);
		m_dupTestAnswer = QStringLiteral("place");
		m_dupTestLog.clear();
		onPlaceSelected();
		check(QStringLiteral("Place anyway -> placed (%1 frames)").arg(framesOf("dup-1")), framesOf("dup-1") > 0);
		// a story of its own whose PHOTO is on the page already
		tick(QStringLiteral("dup-2"));
		m_dupTestAnswer = QStringLiteral("skip");
		m_dupTestLog.clear();
		onPlaceSelected();
		note(m_dupTestLog.join(QStringLiteral(" | ")));
		check(QStringLiteral("placing dup-2: warned \"its photo is already on the page\"; Skip -> not placed (%1 frames)").arg(framesOf("dup-2")),
		      m_dupTestLog.join(' ').contains(QStringLiteral("photo is already on the page")) && framesOf("dup-2") == 0);
		m_dupTestAnswer = QStringLiteral("place");
		onPlaceSelected();
		m_dupTestAnswer.clear();
		// 3b. the same by drag and drop onto a page model slot (handleStoryDrop, what ScribusView::dropEvent calls):
		//     dup-1b, a third copy of the obituary already on the page
		auto makeSlot = [this](double y) {
			const int i = m_doc->itemAdd(PageItem::TextFrame, PageItem::Unspecified, m_doc->Pages->at(0)->xOffset() + 600,
			                             m_doc->Pages->at(0)->yOffset() + y, 180, 140, 1, CommonStrings::None, CommonStrings::None);
			PageItem* ph = m_doc->Items->at(i);
			setAttr(ph, QStringLiteral("pagemodel.slot"), QStringLiteral("{\"n\":9,\"cols\":2,\"photoCols\":0}"));
			return ph;
		};
		auto indexOf = [this](const QString& sid) {
			for (int i = 0; i < m_newsData.size(); ++i)
				if (newsServerId(m_newsData[i].toObject()) == sid)
					return i;
			return -1;
		};
		PageItem* slotA = makeSlot(900);
		m_dupTestAnswer = QStringLiteral("skip");
		m_dupTestLog.clear();
		handleStoryDrop(slotA, indexOf(QStringLiteral("dup-1b")));
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		note(m_dupTestLog.join(QStringLiteral(" | ")));
		check(QStringLiteral("drag and drop of a repeated obituary onto a slot: warned, Skip -> not placed (%1 frames)").arg(framesOf("dup-1b")),
		      m_dupTestLog.join(' ').contains(QStringLiteral("already on the page")) && m_dupTestLog.contains(QStringLiteral("drop: not placed")) && framesOf("dup-1b") == 0);
		m_dupTestAnswer = QStringLiteral("place");
		m_dupTestLog.clear();
		handleStoryDrop(slotA, indexOf(QStringLiteral("dup-1b")));
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		check(QStringLiteral("drop, Place anyway -> in the slot (%1 frames; %2)").arg(framesOf("dup-1b")).arg(m_dupTestLog.join(' ')), framesOf("dup-1b") > 0);
		// a story that repeats nothing: dropped without a question
		PageItem* slotB = makeSlot(1080);
		m_dupTestAnswer = QStringLiteral("skip");
		m_dupTestLog.clear();
		handleStoryDrop(slotB, indexOf(QStringLiteral("f-7")));
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		check(QStringLiteral("drop of a story that repeats nothing: no question, placed (%1 frames; %2)").arg(framesOf("f-7")).arg(m_dupTestLog.join(' ')),
		      !m_dupTestLog.join(' ').contains(QStringLiteral("warning:")) && framesOf("f-7") > 0);
		m_dupTestAnswer.clear();
		// 4. Check duplicates on page: the placed repeat and its original; nothing else
		// Place Selected put dup-1 / dup-2 on the pasteboard (the page is full); the page check never
		// looks there, so they go onto the page first, side by side over the right-hand columns
		{
			ScPage* pg = m_doc->Pages->at(0);
			double x = pg->xOffset() + pg->width() - 260;
			for (const char* sid : { "dup-1", "dup-2" })
			{
				for (int i = 0; i < m_doc->Items->count(); ++i)
				{
					PageItem* it = m_doc->Items->at(i);
					if (attr(it, "news.serverId") != QLatin1String(sid) || it->OwnPage >= 0)
						continue;
					it->setXYPos(x, pg->yOffset() + (it->isImageFrame() ? 40 : 160));
					it->setOwnerPage(0);
				}
				x += 125;
			}
		}
		m_dupPagePairs.clear();
		checkDuplicatesOnPage();
		waitFor([this]() { return !m_dupPagePairs.isEmpty() || m_dupDialog; }, 60000, [=](bool) {
			note(m_dupLog);
			for (const QString& l : std::as_const(m_dupPagePairs))
				note(QStringLiteral("pair: ") + l);
			const QString origTitle = qEnvironmentVariable("SUNEER_DUP_ORIG").left(12);
			int withDup = 0, photoPairs = 0, others = 0;
			for (const QString& l : std::as_const(m_dupPagePairs))
			{
				if (l.contains(origTitle))
					++withDup;
				else if (l.startsWith(tr("photo")))
					++photoPairs;
				else
					++others;
			}
			check(QStringLiteral("Check duplicates on page: the repeated obituary + its original (%1), the repeated photo (%2); other pairs: %3").arg(withDup).arg(photoPairs).arg(others),
			      withDup >= 1 && photoPairs >= 1 && others == 0);
			note(QStringLiteral("other pairs on the real page (judge: false positives or real repeats): %1").arg(others));
			if (m_dupDialog)
			{
				m_dupDialog->grab().save(qEnvironmentVariable("SUNEER_DUP_SELFTEST") + QStringLiteral("-page-pairs.png"));
				m_dupDialog->close();
			}
			// 5. photos: the hash of the processed (background-removed) frame is its original's
			int processed = 0, mapped = 0;
			for (int i = 0; i < m_doc->Items->count(); ++i)
			{
				PageItem* f = m_doc->Items->at(i);
				if (!f->isImageFrame() || attr(f, "obit.out").isEmpty())
					continue;
				++processed;
				mapped += SuneerPhotoHash::sourceFileOf(f) == attr(f, "obit.src") ? 1 : 0;
			}
			check(QStringLiteral("background-removed photos hash their original: %1 of %2").arg(mapped).arg(processed), processed > 0 && mapped == processed);
			finish();
		});
	});
}
