/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerpagemodel.h"

#include <algorithm>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>

#include "prefsmanager.h"
#include "scpaths.h"

namespace
{

QString prefsDir()
{
	QString base = PrefsManager::instance().preferencesLocation();
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.config/scribus");
	while (base.endsWith('/'))
		base.chop(1);
	return base;
}

QJsonObject readJson(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return QJsonObject();
	return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeJson(const QString& path, const QJsonObject& o, QString* error)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly))
	{
		if (error)
			*error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
		return false;
	}
	f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
	if (!f.commit())
	{
		if (error)
			*error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
		return false;
	}
	return true;
}

QJsonObject rectJson(const QRectF& r)
{
	return QJsonObject { { "x", r.x() }, { "y", r.y() }, { "w", r.width() }, { "h", r.height() } };
}

QRectF rectFrom(const QJsonObject& o)
{
	return QRectF(o["x"].toDouble(), o["y"].toDouble(), o["w"].toDouble(), o["h"].toDouble());
}

QJsonObject boxJson(const SuneerPageModel::Box& b)
{
	QJsonObject o = rectJson(b.rect);
	if (!b.role.isEmpty())
		o["role"] = b.role;
	if (!b.name.isEmpty())
		o["name"] = b.name;
	if (b.chars)
		o["chars"] = b.chars;
	if (b.type)
		o["type"] = b.type;
	return o;
}

SuneerPageModel::Box boxFrom(const QJsonObject& o)
{
	SuneerPageModel::Box b;
	b.rect = rectFrom(o);
	b.role = o["role"].toString();
	b.name = o["name"].toString();
	b.chars = o["chars"].toInt();
	b.type = o["type"].toInt();
	return b;
}

QList<SuneerPageGroup> s_groups;
bool s_groupsLoaded = false;

QString groupsFile() { return SuneerPageModels::dir() + QStringLiteral("/groups.json"); }

void loadGroups()
{
	if (s_groupsLoaded)
		return;
	s_groupsLoaded = true;
	s_groups.clear();
	const QJsonObject root = readJson(groupsFile());
	const QJsonArray arr = root["groups"].toArray();
	for (const QJsonValue& v : arr)
	{
		const QJsonObject o = v.toObject();
		SuneerPageGroup g;
		g.id = o["id"].toString();
		g.name = o["name"].toString();
		g.note = o["note"].toString();
		g.grid = o["grid"].toString(QStringLiteral("normal"));
		g.order = s_groups.size();
		if (!g.id.isEmpty())
			s_groups << g;
	}
	if (s_groups.isEmpty())
	{
		s_groups = SuneerPageModels::defaultGroups(SuneerPageModels::defaultPageCount());
		SuneerPageModels::saveGroups(s_groups);
	}
	// group folders made by hand (or an import) without an entry
	const QStringList dirs = QDir(SuneerPageModels::dir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	bool added = false;
	for (const QString& d : dirs)
	{
		if (SuneerPageModels::hasGroup(d))
			continue;
		if (QDir(SuneerPageModels::groupDir(d)).entryList({ QStringLiteral("*.json") }, QDir::Files).isEmpty())
			continue;
		SuneerPageGroup g;
		g.id = d;
		g.name = d;
		g.order = s_groups.size();
		s_groups << g;
		added = true;
	}
	if (added)
		SuneerPageModels::saveGroups(s_groups);
}

} // namespace

// ---------------------------------------------------------------- SuneerPageModel

int SuneerPageModel::photoCount() const
{
	int n = images.size();
	for (const SuneerPageSlot& s : storySlots)
		n += s.photos.size();
	return n;
}

double SuneerPageModel::adAreaMm2() const
{
	double a = 0;
	for (const Box& b : ads)
		a += b.rect.width() * b.rect.height();
	return a;
}

QString SuneerPageModel::adSummary() const
{
	if (ads.isEmpty())
		return QObject::tr("no ads");
	if (ads.size() == 1)
		return QObject::tr("ad %1 x %2 mm").arg(qRound(ads[0].rect.width())).arg(qRound(ads[0].rect.height()));
	return QObject::tr("%n ad(s), %1 cm²", "", ads.size()).arg(qRound(adAreaMm2() / 100.0));
}

QString SuneerPageModel::summary() const
{
	QStringList parts;
	parts << QObject::tr("%n story(ies)", "", storySlots.size());
	parts << QObject::tr("%n photo(s)", "", photoCount());
	if (!obits.isEmpty())
		parts << QObject::tr("%n obituary(ies)", "", obits.size());
	parts << adSummary();
	return parts.join(QStringLiteral(" · "));
}

QJsonObject SuneerPageModel::toJson() const
{
	QJsonObject root;
	root["format"] = QStringLiteral("faircode-page-model");
	root["version"] = 1;
	root["id"] = id;
	root["name"] = name;
	root["group"] = group;
	root["note"] = note;
	root["created"] = created;
	root["source"] = QJsonObject { { "file", sourceFile }, { "page", sourcePage }, { "pages", sourcePages }, { "pageNum", sourcePageNum }, { "version", sourceVersion } };
	root["page"] = QJsonObject { { "w", pageW }, { "h", pageH }, { "margins", QJsonObject { { "t", mTop }, { "b", mBottom }, { "l", mLeft }, { "r", mRight } } } };
	root["grid"] = QJsonObject { { "cols", gridCols }, { "colW", gridColW }, { "gutter", gridGutter }, { "type", gridType } };
	if (obitCols > 0)
		root["obitGrid"] = QJsonObject { { "cols", obitCols }, { "colW", obitColW }, { "gap", obitGap }, { "top", obitTop } };
	root["styles"] = QJsonArray::fromStringList(styles);
	QJsonArray sl;
	for (const SuneerPageSlot& s : storySlots)
	{
		QJsonObject o = rectJson(s.rect);
		o["n"] = s.n;
		o["kind"] = s.kind;
		o["col0"] = s.col0;
		o["cols"] = s.cols;
		o["bodyCols"] = s.bodyCols;
		o["headline"] = QJsonObject { { "style", s.headStyle }, { "size", s.headSize }, { "lines", s.headLines }, { "chars", s.headChars } };
		o["kicker"] = s.kicker;
		o["byline"] = s.byline;
		o["dateline"] = s.dateline;
		o["split"] = s.split;
		o["bodyChars"] = s.bodyChars;
		QJsonArray ph;
		for (const SuneerPageSlot::Photo& p : s.photos)
		{
			QJsonObject po = rectJson(p.rect);
			po["cols"] = p.cols;
			po["pos"] = p.pos;
			po["captionH"] = p.captionH;
			po["caption"] = p.caption;
			ph << po;
		}
		o["photos"] = ph;
		sl << o;
	}
	root["slots"] = sl;
	QJsonArray ob;
	for (const Obit& o : obits)
	{
		QJsonObject j = rectJson(o.rect);
		j["photo"] = o.photo;
		ob << j;
	}
	root["obits"] = ob;
	auto boxes = [](const QList<Box>& l) { QJsonArray a; for (const Box& b : l) a << boxJson(b); return a; };
	root["ads"] = boxes(ads);
	root["fixed"] = boxes(fixed);
	root["rules"] = boxes(rules);
	root["boxes"] = boxes(this->boxes);
	root["images"] = boxes(images);
	root["stats"] = QJsonObject { { "stories", storySlots.size() }, { "photos", photoCount() }, { "obits", obits.size() }, { "ads", ads.size() }, { "adArea", adAreaMm2() } };
	return root;
}

SuneerPageModel SuneerPageModel::fromJson(const QJsonObject& root)
{
	SuneerPageModel m;
	m.id = root["id"].toString();
	m.name = root["name"].toString();
	m.group = root["group"].toString();
	m.note = root["note"].toString();
	m.created = root["created"].toString();
	const QJsonObject src = root["source"].toObject();
	m.sourceFile = src["file"].toString();
	m.sourcePage = src["page"].toInt();
	m.sourcePages = src["pages"].toInt(1);
	m.sourcePageNum = src["pageNum"].toInt();
	m.sourceVersion = src["version"].toString();
	const QJsonObject pg = root["page"].toObject();
	m.pageW = pg["w"].toDouble(350);
	m.pageH = pg["h"].toDouble(538);
	const QJsonObject mg = pg["margins"].toObject();
	m.mTop = mg["t"].toDouble(26);
	m.mBottom = mg["b"].toDouble(15);
	m.mLeft = mg["l"].toDouble(10);
	m.mRight = mg["r"].toDouble(10);
	const QJsonObject g = root["grid"].toObject();
	m.gridCols = g["cols"].toInt(8);
	m.gridColW = g["colW"].toDouble(34.25);
	m.gridGutter = g["gutter"].toDouble(8.0);
	m.gridType = g["type"].toString(QStringLiteral("normal"));
	const QJsonValue og = root["obitGrid"];
	if (og.isObject())
	{
		m.obitCols = og["cols"].toInt();
		m.obitColW = og["colW"].toDouble();
		m.obitGap = og["gap"].toDouble();
		m.obitTop = og["top"].toDouble();
	}
	for (const QJsonValue& v : root["styles"].toArray())
		m.styles << v.toString();
	for (const QJsonValue& v : root["slots"].toArray())
	{
		const QJsonObject o = v.toObject();
		SuneerPageSlot s;
		s.rect = rectFrom(o);
		s.n = o["n"].toInt(m.storySlots.size() + 1);
		s.kind = o["kind"].toString(QStringLiteral("story"));
		s.col0 = o["col0"].toInt();
		s.cols = qMax(1, o["cols"].toInt(1));
		s.bodyCols = qMax(1, o["bodyCols"].toInt(s.cols));
		const QJsonObject h = o["headline"].toObject();
		s.headStyle = h["style"].toString();
		s.headSize = h["size"].toDouble();
		s.headLines = qMax(1, h["lines"].toInt(1));
		s.headChars = h["chars"].toInt();
		s.kicker = o["kicker"].toBool();
		s.byline = o["byline"].toBool();
		s.dateline = o["dateline"].toBool();
		s.split = o["split"].toBool();
		s.bodyChars = o["bodyChars"].toInt();
		for (const QJsonValue& pv : o["photos"].toArray())
		{
			const QJsonObject po = pv.toObject();
			SuneerPageSlot::Photo p;
			p.rect = rectFrom(po);
			p.cols = qMax(1, po["cols"].toInt(1));
			p.pos = po["pos"].toString(QStringLiteral("right"));
			p.captionH = po["captionH"].toDouble();
			p.caption = po["caption"].toBool();
			s.photos << p;
		}
		m.storySlots << s;
	}
	for (const QJsonValue& v : root["obits"].toArray())
	{
		Obit o;
		o.rect = rectFrom(v.toObject());
		o.photo = v.toObject()["photo"].toBool(true);
		m.obits << o;
	}
	auto boxes = [](const QJsonValue& v) { QList<Box> l; for (const QJsonValue& b : v.toArray()) l << boxFrom(b.toObject()); return l; };
	m.ads = boxes(root["ads"]);
	m.fixed = boxes(root["fixed"]);
	m.rules = boxes(root["rules"]);
	m.boxes = boxes(root["boxes"]);
	m.images = boxes(root["images"]);
	return m;
}

double SuneerPageModel::similarity(const SuneerPageModel& a, const SuneerPageModel& b)
{
	if (a.storySlots.isEmpty() && b.storySlots.isEmpty())
	{
		// obituary-only pages: compare obit and ad counts
		if (a.obits.isEmpty() && b.obits.isEmpty())
			return a.ads.size() == b.ads.size() ? 1.0 : 0.5;
		const double oa = a.obits.size(), ob = b.obits.size();
		return qMin(oa, ob) / qMax(1.0, qMax(oa, ob)) * (a.ads.size() == b.ads.size() ? 1.0 : 0.8);
	}
	if (a.storySlots.size() != b.storySlots.size())
		return qMin(a.storySlots.size(), b.storySlots.size()) / double(qMax(a.storySlots.size(), b.storySlots.size())) * 0.6;
	QList<bool> used;
	for (int i = 0; i < b.storySlots.size(); ++i)
		used << false;
	double score = 0;
	for (const SuneerPageSlot& s : a.storySlots)
	{
		double best = 0;
		int bi = -1;
		for (int i = 0; i < b.storySlots.size(); ++i)
		{
			if (used[i])
				continue;
			const SuneerPageSlot& t = b.storySlots[i];
			double v = 0;
			if (t.cols == s.cols)
				v += 0.3;
			const double dx = qAbs(t.rect.x() - s.rect.x()), dy = qAbs(t.rect.y() - s.rect.y()), dh = qAbs(t.rect.height() - s.rect.height());
			v += 0.25 * qMax(0.0, 1.0 - dx / 20.0);
			v += 0.25 * qMax(0.0, 1.0 - dy / 30.0);
			v += 0.1 * qMax(0.0, 1.0 - dh / 40.0);
			if (t.photos.size() == s.photos.size())
				v += 0.1;
			if (v > best)
			{
				best = v;
				bi = i;
			}
		}
		if (bi >= 0)
			used[bi] = true;
		score += best;
	}
	score /= a.storySlots.size();
	// ads and obituaries: a different ad shape is a different page
	const double adA = a.adAreaMm2(), adB = b.adAreaMm2();
	if (qMax(adA, adB) > 1.0)
		score *= 0.8 + 0.2 * (qMin(adA, adB) / qMax(adA, adB));
	if (a.obits.size() != b.obits.size())
		score *= 0.95;
	return qBound(0.0, score, 1.0);
}

QImage SuneerPageModel::thumbnail(int widthPx, bool dark) const
{
	const double pw = pageW > 1 ? pageW : 350.0, ph = pageH > 1 ? pageH : 538.0;
	const int w = qMax(40, widthPx);
	const int h = qMax(40, qRound(w * ph / pw));
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	const QColor paper = dark ? QColor(48, 48, 50) : QColor(252, 252, 250);
	const QColor ink = dark ? QColor(210, 210, 210) : QColor(70, 70, 70);
	const QColor light = dark ? QColor(95, 95, 98) : QColor(205, 205, 205);
	const QColor photo = dark ? QColor(120, 120, 124) : QColor(165, 165, 165);
	const QColor adCol = dark ? QColor(130, 110, 70) : QColor(235, 215, 170);
	const QColor fixedCol = dark ? QColor(85, 85, 90) : QColor(190, 190, 195);
	const QColor obitCol = dark ? QColor(110, 118, 124) : QColor(182, 184, 185);
	img.fill(paper);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, true);
	const double s = w / pw;
	auto R = [&](const QRectF& mm) { return QRectF(mm.x() * s, mm.y() * s, mm.width() * s, mm.height() * s); };
	// margins
	p.setPen(QPen(light, 0.6, Qt::DashLine));
	p.setBrush(Qt::NoBrush);
	p.drawRect(R(QRectF(mLeft, mTop, pw - mLeft - mRight, ph - mTop - mBottom)));
	// fixed items
	p.setPen(Qt::NoPen);
	p.setBrush(fixedCol);
	for (const Box& b : fixed)
		p.drawRect(R(b.rect));
	// ads
	for (const Box& b : ads)
	{
		const QRectF r = R(b.rect);
		p.setBrush(adCol);
		p.setPen(Qt::NoPen);
		p.drawRect(r);
		p.setPen(QPen(ink, 0.5));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
		p.drawLine(r.topLeft(), r.bottomRight());
		p.drawLine(r.topRight(), r.bottomLeft());
	}
	// boxes / loose images
	for (const Box& b : boxes)
	{
		p.setPen(QPen(light, 0.6));
		p.setBrush(Qt::NoBrush);
		p.drawRect(R(b.rect));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(photo);
	for (const Box& b : images)
		p.drawRect(R(b.rect));
	// obituaries
	for (const Obit& o : obits)
	{
		const QRectF r = R(o.rect);
		if (o.photo)
		{
			p.setBrush(obitCol);
			p.setPen(Qt::NoPen);
			p.drawRect(QRectF(r.x(), r.y(), r.width(), qMin(r.height(), r.width())));
		}
		p.setPen(QPen(light, 0.5));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
	}
	// story storySlots
	for (const SuneerPageSlot& sl : storySlots)
	{
		const QRectF r = R(sl.rect);
		p.setPen(QPen(light, 0.7));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
		// photos
		for (const SuneerPageSlot::Photo& f : sl.photos)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(photo);
			p.drawRect(R(f.rect));
		}
		// headline bars: one per line, thickness by size
		const double lineH = qBound(1.2, sl.headSize * 0.42 * s * 25.4 / 72.0, r.height() / 3.0);
		double y = r.top() + 1.0;
		QRectF headArea = r;
		for (const SuneerPageSlot::Photo& f : sl.photos)
			if (f.pos == QLatin1String("topAbove"))
				y = qMax(y, R(f.rect).bottom() + 1.0);
		p.setPen(Qt::NoPen);
		p.setBrush(ink);
		const double headW = r.width() - 2.0;
		for (int i = 0; i < sl.headLines && y + lineH < r.bottom(); ++i)
		{
			p.drawRect(QRectF(r.left() + 1.0, y, headW * (i == sl.headLines - 1 && sl.headLines > 1 ? 0.7 : 1.0), lineH * 0.7));
			y += lineH;
		}
		// body: placeholder lines in columns, around the photos
		const int cols = qMax(1, sl.bodyCols);
		const double gap = 2.5 * s;
		const double colW = (r.width() - 2.0 - gap * (cols - 1)) / cols;
		p.setPen(QPen(light, qMax(0.5, 0.9 * s)));
		const double step = qMax(1.6, 3.4 * s);
		for (int c = 0; c < cols; ++c)
		{
			const double x0 = r.left() + 1.0 + c * (colW + gap);
			for (double ly = y + step; ly < r.bottom() - 1.0; ly += step)
			{
				bool blocked = false;
				for (const SuneerPageSlot::Photo& f : sl.photos)
				{
					const QRectF fr = R(f.rect).adjusted(-1, -1, 1, 1);
					if (fr.contains(QPointF(x0 + colW / 2, ly)) || (fr.top() <= ly && fr.bottom() >= ly && fr.left() < x0 + colW && fr.right() > x0))
						blocked = true;
				}
				if (!blocked)
					p.drawLine(QPointF(x0, ly), QPointF(x0 + colW, ly));
			}
		}
		// slot number
		if (r.width() > 14 && r.height() > 12)
		{
			QFont f = p.font();
			f.setPixelSize(qBound(7, qRound(r.height() / 4), 14));
			f.setBold(true);
			p.setFont(f);
			p.setPen(dark ? QColor(255, 200, 90) : QColor(200, 90, 20));
			p.drawText(r.adjusted(2, 1, -2, -1), Qt::AlignBottom | Qt::AlignRight, QString::number(sl.n));
		}
	}
	// rules
	p.setPen(QPen(ink, 0.6));
	for (const Box& b : rules)
	{
		const QRectF r = R(b.rect);
		if (r.width() >= r.height())
			p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
		else
			p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));
	}
	p.setPen(QPen(light, 1.0));
	p.setBrush(Qt::NoBrush);
	p.drawRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0));
	p.end();
	return img;
}

// ---------------------------------------------------------------- namespace

namespace SuneerPageModels
{

QString dir() { return prefsDir() + QStringLiteral("/layout-models/pages"); }
QString groupDir(const QString& groupId) { return dir() + QStringLiteral("/") + groupId; }
QString filePath(const QString& groupId, const QString& modelId) { return groupDir(groupId) + QStringLiteral("/") + modelId + QStringLiteral(".json"); }
QString thumbPath(const QString& groupId, const QString& modelId) { return groupDir(groupId) + QStringLiteral("/") + modelId + QStringLiteral(".png"); }

void reload()
{
	s_groupsLoaded = false;
	s_groups.clear();
}

QString slug(const QString& name)
{
	QString s = name.toLower().trimmed();
	static const QRegularExpression bad(QStringLiteral("[^a-z0-9\\p{L}]+"));
	s.replace(bad, QStringLiteral("-"));
	while (s.startsWith('-'))
		s.remove(0, 1);
	while (s.endsWith('-'))
		s.chop(1);
	if (s.isEmpty())
		s = QStringLiteral("model");
	return s.left(60);
}

int defaultPageCount()
{
	const QJsonObject root = readJson(groupsFile());
	return root["pageCount"].toInt(24);
}

void setDefaultPageCount(int n)
{
	loadGroups();
	n = qBound(1, n, 200);
	QList<SuneerPageGroup> gs = s_groups;
	// add missing Page-N groups, keep everything else
	QStringList have;
	for (const SuneerPageGroup& g : gs)
		have << g.id;
	int insertAt = 0;
	for (int i = 0; i < gs.size(); ++i)
		if (gs[i].id.startsWith(QLatin1String("page-")))
			insertAt = i + 1;
	for (int i = 1; i <= n; ++i)
	{
		const QString id = QStringLiteral("page-%1").arg(i);
		if (have.contains(id))
			continue;
		SuneerPageGroup g;
		g.id = id;
		g.name = QStringLiteral("Page-%1").arg(i);
		gs.insert(qMin(insertAt, gs.size()), g);
		++insertAt;
	}
	// remove empty Page-N groups above n
	for (int i = gs.size() - 1; i >= 0; --i)
	{
		static const QRegularExpression re(QStringLiteral("^page-(\\d+)$"));
		const QRegularExpressionMatch m = re.match(gs[i].id);
		if (m.hasMatch() && m.captured(1).toInt() > n && modelCount(gs[i].id) == 0)
			gs.removeAt(i);
	}
	s_groups = gs;
	QJsonObject root = readJson(groupsFile());
	root["pageCount"] = n;
	saveGroups(gs);
}

QList<SuneerPageGroup> defaultGroups(int pageCount)
{
	QList<SuneerPageGroup> gs;
	for (int i = 1; i <= qBound(1, pageCount, 200); ++i)
	{
		SuneerPageGroup g;
		g.id = QStringLiteral("page-%1").arg(i);
		g.name = QStringLiteral("Page-%1").arg(i);
		g.note = QObject::tr("8-column grid (34.25 mm, 8 mm gutter)");
		gs << g;
	}
	auto add = [&](const QString& id, const QString& name, const QString& grid, const QString& note) {
		SuneerPageGroup g;
		g.id = id;
		g.name = name;
		g.grid = grid;
		g.note = note;
		gs << g;
	};
	add(QStringLiteral("obituary"), QObject::tr("Obituary"), QStringLiteral("obituary"), QObject::tr("10 x 30.3 mm obituary grid (the Obituary module rules)"));
	add(QStringLiteral("editorial"), QObject::tr("Editorial"), QStringLiteral("normal"), QString());
	add(QStringLiteral("sports"), QObject::tr("Sports"), QStringLiteral("normal"), QString());
	add(QStringLiteral("business"), QObject::tr("Business"), QStringLiteral("normal"), QString());
	add(QStringLiteral("supplement"), QObject::tr("Supplement"), QStringLiteral("normal"), QString());
	for (int i = 0; i < gs.size(); ++i)
		gs[i].order = i;
	return gs;
}

QList<SuneerPageGroup> groups()
{
	loadGroups();
	return s_groups;
}

bool saveGroups(const QList<SuneerPageGroup>& groups, QString* error)
{
	QJsonObject root = readJson(groupsFile());
	root["format"] = QStringLiteral("faircode-page-model-groups");
	if (!root.contains("pageCount"))
		root["pageCount"] = 24;
	QJsonArray arr;
	for (const SuneerPageGroup& g : groups)
		arr << QJsonObject { { "id", g.id }, { "name", g.name }, { "note", g.note }, { "grid", g.grid } };
	root["groups"] = arr;
	s_groups = groups;
	for (int i = 0; i < s_groups.size(); ++i)
		s_groups[i].order = i;
	s_groupsLoaded = true;
	for (const SuneerPageGroup& g : groups)
		QDir().mkpath(groupDir(g.id));
	return writeJson(groupsFile(), root, error);
}

SuneerPageGroup group(const QString& id)
{
	loadGroups();
	for (const SuneerPageGroup& g : s_groups)
		if (g.id == id)
			return g;
	SuneerPageGroup g;
	g.id = id;
	g.name = id;
	return g;
}

bool hasGroup(const QString& id)
{
	loadGroups();
	for (const SuneerPageGroup& g : s_groups)
		if (g.id == id)
			return true;
	return false;
}

QString addGroup(const QString& name, const QString& grid, const QString& note, QString* error)
{
	loadGroups();
	if (name.trimmed().isEmpty())
	{
		if (error)
			*error = QObject::tr("The group needs a name");
		return QString();
	}
	QString id = slug(name);
	int n = 2;
	while (hasGroup(id) || QDir(groupDir(id)).exists())
		id = slug(name) + QStringLiteral("-%1").arg(n++);
	SuneerPageGroup g;
	g.id = id;
	g.name = name.trimmed();
	g.grid = grid.isEmpty() ? QStringLiteral("normal") : grid;
	g.note = note;
	QList<SuneerPageGroup> gs = s_groups;
	gs << g;
	return saveGroups(gs, error) ? id : QString();
}

bool renameGroup(const QString& id, const QString& name, QString* error)
{
	loadGroups();
	QList<SuneerPageGroup> gs = s_groups;
	for (SuneerPageGroup& g : gs)
		if (g.id == id)
		{
			g.name = name.trimmed();
			return saveGroups(gs, error);
		}
	if (error)
		*error = QObject::tr("No group %1").arg(id);
	return false;
}

bool setGroupInfo(const QString& id, const QString& note, const QString& grid, QString* error)
{
	loadGroups();
	QList<SuneerPageGroup> gs = s_groups;
	for (SuneerPageGroup& g : gs)
		if (g.id == id)
		{
			g.note = note;
			if (!grid.isEmpty())
				g.grid = grid;
			return saveGroups(gs, error);
		}
	if (error)
		*error = QObject::tr("No group %1").arg(id);
	return false;
}

bool moveGroup(const QString& id, int delta, QString* error)
{
	loadGroups();
	QList<SuneerPageGroup> gs = s_groups;
	int i = -1;
	for (int k = 0; k < gs.size(); ++k)
		if (gs[k].id == id)
			i = k;
	if (i < 0)
		return false;
	const int j = qBound(0, i + delta, gs.size() - 1);
	if (i == j)
		return true;
	gs.move(i, j);
	return saveGroups(gs, error);
}

bool removeGroup(const QString& id, const QString& moveTo, QString* error)
{
	loadGroups();
	if (!hasGroup(id))
	{
		if (error)
			*error = QObject::tr("No group %1").arg(id);
		return false;
	}
	const QList<SuneerPageModel> ms = models(id);
	if (!ms.isEmpty())
	{
		if (moveTo.isEmpty() || moveTo == id || !hasGroup(moveTo))
		{
			if (error)
				*error = QObject::tr("The group still holds %n model(s); move them to another group first.", "", ms.size());
			return false;
		}
		for (const SuneerPageModel& m : ms)
			if (!moveModel(id, m.id, moveTo, error))
				return false;
	}
	QDir d(groupDir(id));
	if (d.exists())
	{
		for (const QString& f : d.entryList(QDir::Files))
			d.remove(f);
		QDir().rmdir(d.absolutePath());
	}
	QList<SuneerPageGroup> gs = s_groups;
	for (int k = gs.size() - 1; k >= 0; --k)
		if (gs[k].id == id)
			gs.removeAt(k);
	return saveGroups(gs, error);
}

QList<SuneerPageModel> models(const QString& groupId)
{
	QList<SuneerPageModel> out;
	const QDir d(groupDir(groupId));
	for (const QString& f : d.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
	{
		const QJsonObject root = readJson(d.absoluteFilePath(f));
		if (root["format"].toString() != QLatin1String("faircode-page-model"))
			continue;
		SuneerPageModel m = SuneerPageModel::fromJson(root);
		m.id = QFileInfo(f).completeBaseName();
		m.group = groupId;
		if (m.name.isEmpty())
			m.name = m.id;
		out << m;
	}
	std::stable_sort(out.begin(), out.end(), [](const SuneerPageModel& a, const SuneerPageModel& b) { return a.name.localeAwareCompare(b.name) < 0; });
	return out;
}

int modelCount(const QString& groupId)
{
	return QDir(groupDir(groupId)).entryList({ QStringLiteral("*.json") }, QDir::Files).size();
}

SuneerPageModel load(const QString& groupId, const QString& modelId, bool* ok)
{
	const QJsonObject root = readJson(filePath(groupId, modelId));
	if (ok)
		*ok = root["format"].toString() == QLatin1String("faircode-page-model");
	SuneerPageModel m = SuneerPageModel::fromJson(root);
	m.id = modelId;
	m.group = groupId;
	if (m.name.isEmpty())
		m.name = modelId;
	return m;
}

QString newId(const QString& groupId, const QString& name)
{
	const QString base = slug(name);
	QString id = base;
	int n = 2;
	while (QFileInfo::exists(filePath(groupId, id)))
		id = base + QStringLiteral("-%1").arg(n++);
	return id;
}

bool save(SuneerPageModel& m, QString* error)
{
	if (m.group.isEmpty())
	{
		if (error)
			*error = QObject::tr("The model has no group");
		return false;
	}
	if (m.name.trimmed().isEmpty())
		m.name = m.sourceFile.isEmpty() ? QObject::tr("Page model") : QFileInfo(m.sourceFile).completeBaseName();
	if (m.id.isEmpty())
		m.id = newId(m.group, m.name);
	if (m.created.isEmpty())
		m.created = QDateTime::currentDateTime().toString(Qt::ISODate);
	if (!writeJson(filePath(m.group, m.id), m.toJson(), error))
		return false;
	m.thumbnail(300).save(thumbPath(m.group, m.id), "PNG");
	return true;
}

bool remove(const QString& groupId, const QString& modelId, QString* error)
{
	const QString f = filePath(groupId, modelId);
	if (QFileInfo::exists(f) && !QFile::remove(f))
	{
		if (error)
			*error = QObject::tr("Cannot delete %1").arg(f);
		return false;
	}
	QFile::remove(thumbPath(groupId, modelId));
	return true;
}

bool rename(const QString& groupId, const QString& modelId, const QString& name, QString* error)
{
	bool ok = false;
	SuneerPageModel m = load(groupId, modelId, &ok);
	if (!ok)
	{
		if (error)
			*error = QObject::tr("Cannot read %1").arg(filePath(groupId, modelId));
		return false;
	}
	m.name = name.trimmed();
	return save(m, error);
}

bool moveModel(const QString& groupId, const QString& modelId, const QString& toGroup, QString* error)
{
	if (toGroup == groupId)
		return true;
	bool ok = false;
	SuneerPageModel m = load(groupId, modelId, &ok);
	if (!ok)
	{
		if (error)
			*error = QObject::tr("Cannot read %1").arg(filePath(groupId, modelId));
		return false;
	}
	m.group = toGroup;
	if (QFileInfo::exists(filePath(toGroup, m.id)))
		m.id = newId(toGroup, m.name);
	if (!save(m, error))
		return false;
	return remove(groupId, modelId, error);
}

QString duplicate(const QString& groupId, const QString& modelId, QString* error)
{
	bool ok = false;
	SuneerPageModel m = load(groupId, modelId, &ok);
	if (!ok)
	{
		if (error)
			*error = QObject::tr("Cannot read %1").arg(filePath(groupId, modelId));
		return QString();
	}
	m.name = QObject::tr("%1 (copy)").arg(m.name);
	m.id.clear();
	m.created.clear();
	if (!save(m, error))
		return QString();
	return m.id;
}

double mostAlike(const SuneerPageModel& m, const QString& groupId, QString* otherName, QString* otherId)
{
	double best = 0;
	for (const SuneerPageModel& o : models(groupId))
	{
		if (o.id == m.id && !m.id.isEmpty())
			continue;
		const double s = SuneerPageModel::similarity(m, o);
		if (s > best)
		{
			best = s;
			if (otherName)
				*otherName = o.name;
			if (otherId)
				*otherId = o.id;
		}
	}
	return best;
}

bool exportGroups(const QStringList& groupIds, const QString& path, QString* error)
{
	loadGroups();
	QJsonObject root;
	root["format"] = QStringLiteral("faircode-page-models");
	root["version"] = 1;
	root["exported"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	QJsonArray gs;
	for (const SuneerPageGroup& g : s_groups)
	{
		if (!groupIds.isEmpty() && !groupIds.contains(g.id))
			continue;
		QJsonObject go { { "id", g.id }, { "name", g.name }, { "note", g.note }, { "grid", g.grid } };
		QJsonArray ms;
		for (const SuneerPageModel& m : models(g.id))
		{
			QJsonObject mo = m.toJson();
			QFile t(thumbPath(g.id, m.id));
			if (t.open(QIODevice::ReadOnly))
				mo["thumbnail"] = QString::fromLatin1(t.readAll().toBase64());
			ms << mo;
		}
		go["models"] = ms;
		gs << go;
	}
	root["groups"] = gs;
	return writeJson(path, root, error);
}

int importFile(const QString& path, QString* error, QStringList* report)
{
	loadGroups();
	const QJsonObject root = readJson(path);
	int count = 0;
	if (root["format"].toString() == QLatin1String("faircode-page-model"))
	{
		// a single model file
		SuneerPageModel m = SuneerPageModel::fromJson(root);
		if (m.group.isEmpty() || !hasGroup(m.group))
			m.group = m.isObituary() ? QStringLiteral("obituary") : (hasGroup(QStringLiteral("page-1")) ? QStringLiteral("page-1") : (s_groups.isEmpty() ? QStringLiteral("imported") : s_groups.first().id));
		if (!hasGroup(m.group))
			addGroup(m.group, m.gridType, QString());
		m.id = newId(m.group, m.name.isEmpty() ? QStringLiteral("model") : m.name);
		if (!save(m, error))
			return 0;
		if (report)
			*report << QObject::tr("%1 -> %2").arg(m.name, group(m.group).name);
		return 1;
	}
	if (root["format"].toString() != QLatin1String("faircode-page-models"))
	{
		if (error)
			*error = QObject::tr("%1 is not a page models file").arg(path);
		return 0;
	}
	for (const QJsonValue& gv : root["groups"].toArray())
	{
		const QJsonObject go = gv.toObject();
		QString gid = go["id"].toString();
		if (gid.isEmpty())
			gid = slug(go["name"].toString());
		if (!hasGroup(gid))
		{
			QList<SuneerPageGroup> gs = s_groups;
			SuneerPageGroup g;
			g.id = gid;
			g.name = go["name"].toString(gid);
			g.note = go["note"].toString();
			g.grid = go["grid"].toString(QStringLiteral("normal"));
			gs << g;
			if (!saveGroups(gs, error))
				return count;
			if (report)
				*report << QObject::tr("new group %1").arg(g.name);
		}
		for (const QJsonValue& mv : go["models"].toArray())
		{
			QJsonObject mo = mv.toObject();
			const QString thumb = mo.take("thumbnail").toString();
			SuneerPageModel m = SuneerPageModel::fromJson(mo);
			m.group = gid;
			if (m.id.isEmpty() || QFileInfo::exists(filePath(gid, m.id)))
			{
				// the same id: keep the existing one when it is the same model, else a copy
				if (!m.id.isEmpty())
				{
					bool ok = false;
					const SuneerPageModel have = load(gid, m.id, &ok);
					if (ok && SuneerPageModel::similarity(have, m) >= 0.999 && have.name == m.name)
					{
						if (report)
							*report << QObject::tr("%1: already there (skipped)").arg(m.name);
						continue;
					}
				}
				m.id = newId(gid, m.name.isEmpty() ? QStringLiteral("model") : m.name);
			}
			if (!save(m, error))
				return count;
			if (!thumb.isEmpty())
			{
				QFile t(thumbPath(gid, m.id));
				if (t.open(QIODevice::WriteOnly))
					t.write(QByteArray::fromBase64(thumb.toLatin1()));
			}
			++count;
			if (report)
				*report << QObject::tr("%1 -> %2").arg(m.name, group(gid).name);
		}
	}
	return count;
}

QString suggestGroup(const QString& fileName, int pageIndex, int pageCount, int pageNumAttr, int obitCount, QString* why)
{
	loadGroups();
	const QString stem = QFileInfo(fileName).completeBaseName();
	const QString low = stem.toLower();
	auto has = [&](const QString& id) { return hasGroup(id); };
	// content first: an obituary page is an obituary page whatever it is called
	if (obitCount >= 6 && has(QStringLiteral("obituary")))
	{
		if (why)
			*why = QObject::tr("%n obituary module(s) on the page", "", obitCount);
		return QStringLiteral("obituary");
	}
	// "page1", "page-15", "Page_7", "p12", "21page4" (date, then page)
	static const QRegularExpression pageRe(QStringLiteral("(?:^|[^a-z])p(?:age|g)?[ _-]*(\\d{1,2})(?!\\d)"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch m = pageRe.match(low);
	if (m.hasMatch())
	{
		const int n = m.captured(1).toInt();
		if (n >= 1 && has(QStringLiteral("page-%1").arg(n)))
		{
			if (why)
				*why = QObject::tr("file name says page %1").arg(n);
			return QStringLiteral("page-%1").arg(n);
		}
	}
	struct Word { const char* word; const char* group; };
	static const Word words[] = {
		{ "obit", "obituary" }, { "charam", "obituary" }, { "ചരമ", "obituary" }, { "death", "obituary" },
		{ "edit", "editorial" }, { "mukhaprasangam", "editorial" }, { "മുഖപ്രസംഗം", "editorial" },
		{ "sport", "sports" }, { "spo", "sports" }, { "kayikam", "sports" }, { "കായിക", "sports" },
		{ "busi", "business" }, { "biz", "business" }, { "vyapar", "business" }, { "വ്യാപാര", "business" },
		{ "supp", "supplement" }, { "suppl", "supplement" } };
	for (const Word& w : words)
		if (low.contains(QString::fromUtf8(w.word)) && has(QString::fromUtf8(w.group)))
		{
			if (why)
				*why = QObject::tr("file name says \"%1\"").arg(QString::fromUtf8(w.word));
			return QString::fromUtf8(w.group);
		}
	// the user's own group names in the file name
	for (const SuneerPageGroup& g : s_groups)
	{
		if (g.id.startsWith(QLatin1String("page-")))
			continue;
		if (!g.name.isEmpty() && low.contains(g.name.toLower()))
		{
			if (why)
				*why = QObject::tr("file name contains the group name");
			return g.id;
		}
	}
	// a multi-page file: the page number inside
	if (pageCount > 1)
	{
		const int n = (pageNumAttr > 0 ? pageNumAttr : pageIndex) + 1;
		if (has(QStringLiteral("page-%1").arg(n)))
		{
			if (why)
				*why = QObject::tr("page %1 of %2 in the file").arg(n).arg(pageCount);
			return QStringLiteral("page-%1").arg(n);
		}
	}
	// a plain number at the start or the end ("15.sla", "deshabhimani-3.sla")
	static const QRegularExpression numRe(QStringLiteral("(?:^|[^0-9])(\\d{1,2})$"));
	const QRegularExpressionMatch nm = numRe.match(low);
	if (nm.hasMatch() && has(QStringLiteral("page-%1").arg(nm.captured(1).toInt())))
	{
		if (why)
			*why = QObject::tr("file name ends with %1").arg(nm.captured(1).toInt());
		return QStringLiteral("page-%1").arg(nm.captured(1).toInt());
	}
	if (why)
		*why = QObject::tr("no hint in the name, the page number or the content");
	return QString();
}

QString groupForDocPage(const QString& savedGroup, const QString& docFileName, int pageIndex, int pageCount)
{
	if (!savedGroup.isEmpty() && hasGroup(savedGroup))
		return savedGroup;
	const QString s = suggestGroup(docFileName, pageIndex, pageCount, -1, 0);
	if (!s.isEmpty())
		return s;
	loadGroups();
	return s_groups.isEmpty() ? QString() : s_groups.first().id;
}

QString suggestName(const QString& fileName, int pageIndex, int pageCount)
{
	const QString stem = QFileInfo(fileName).completeBaseName();
	if (pageCount > 1)
		return QStringLiteral("%1 p%2").arg(stem).arg(pageIndex + 1);
	return stem;
}

QString extractorScript()
{
	const QStringList candidates {
		ScPaths::instance().shareDir() + QStringLiteral("autolayout/page_models.py"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/autolayout/page_models.py"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/autolayout/page_models.py") };
	for (const QString& c : candidates)
		if (QFileInfo::exists(c))
			return QFileInfo(c).absoluteFilePath();
	return candidates.first();
}

} // namespace SuneerPageModels
