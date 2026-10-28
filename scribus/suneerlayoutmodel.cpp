/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerlayoutmodel.h"

#include <memory>

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>

#include "prefsmanager.h"

namespace {

const char* const kFormat = "faircode-layout-model";

QJsonArray strList(const QStringList& l)
{
	QJsonArray a;
	for (const QString& s : l)
		a << s;
	return a;
}

QStringList fromStrList(const QJsonValue& v)
{
	QStringList out;
	for (const QJsonValue& e : v.toArray())
		if (!e.toString().trimmed().isEmpty())
			out << e.toString().trimmed();
	return out;
}

double num(const QJsonObject& o, const char* key, double def)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isDouble() ? v.toDouble() : def;
}

} // namespace

// ---------------------------------------------------------------------------
// SuneerLayoutModel
// ---------------------------------------------------------------------------

SuneerSpacing::Values SuneerLayoutModel::spacing() const
{
	SuneerSpacing::Values v {};
	v.v[SuneerSpacing::ModuleGapV] = gapVMm;
	v.v[SuneerSpacing::ModuleGapH] = gutterMm;
	v.v[SuneerSpacing::PhotoText] = photoTextMm;
	v.v[SuneerSpacing::WrapPad] = wrapPadMm;
	v.v[SuneerSpacing::CaptionGap] = captionGapMm;
	v.v[SuneerSpacing::CaptionText] = captionTextMm;
	v.v[SuneerSpacing::HeadlineBody] = headlineBodyMm;
	v.v[SuneerSpacing::Inset] = insetMm;
	v.v[SuneerSpacing::ColumnGap] = columnGapMm;
	v.v[SuneerSpacing::RuleWidth] = ruleWidthPt;
	v.v[SuneerSpacing::KickerHeadline] = kickerHeadlineMm;
	v.ruleColor = ruleColor;
	return v;
}

void SuneerLayoutModel::setSpacing(const SuneerSpacing::Values& v)
{
	gapVMm = v.v[SuneerSpacing::ModuleGapV];
	gutterMm = v.v[SuneerSpacing::ModuleGapH];
	photoTextMm = v.v[SuneerSpacing::PhotoText];
	wrapPadMm = v.v[SuneerSpacing::WrapPad];
	captionGapMm = v.v[SuneerSpacing::CaptionGap];
	captionTextMm = v.v[SuneerSpacing::CaptionText];
	headlineBodyMm = v.v[SuneerSpacing::HeadlineBody];
	insetMm = v.v[SuneerSpacing::Inset];
	columnGapMm = v.v[SuneerSpacing::ColumnGap];
	ruleWidthPt = v.v[SuneerSpacing::RuleWidth];
	kickerHeadlineMm = v.v[SuneerSpacing::KickerHeadline];
	ruleColor = v.ruleColor;
}

int SuneerLayoutModel::columnsFor(int chars, bool lead) const
{
	int c = 0;
	if (lead && leadPosition != QLatin1String("none"))
		c = leadColumns;
	else if (fixedColumns > 0)
		c = fixedColumns;
	else
	{
		for (const WidthRule& r : widthRules)
			if (chars <= r.maxChars)
			{
				c = r.columns;
				break;
			}
		if (c <= 0)
			c = widthRules.isEmpty() ? 4 : widthRules.last().columns;
	}
	return qBound(1, c, qMax(1, columns));
}

int SuneerLayoutModel::photoColumnsFor(int storyColumns) const
{
	if (fixedPhotoColumns > 0)
		return qBound(1, fixedPhotoColumns, storyColumns);
	int best = 0;
	for (const PhotoRule& r : photoRules)
		if (r.storyColumns <= storyColumns && r.storyColumns >= 0)
			best = r.photoColumns;            // rules are sorted by story width: the last one that applies
	if (best <= 0)
		best = qMax(1, (storyColumns + 1) / 2);
	return qBound(1, best, storyColumns);
}

QString SuneerLayoutModel::headlineStyleFor(int cols, bool lead) const
{
	if (lead && !leadHeadlineStyle.isEmpty())
		return leadHeadlineStyle;
	QString s;
	for (const HeadRule& r : headlineRules)
		if (r.columns <= cols)
			s = r.style;                       // sorted by width: the widest rule that applies
	return s;
}

bool SuneerLayoutModel::appliesTo(const QString& pageKey) const
{
	if (pageTypes.isEmpty())
		return true;
	const QString k = pageKey.trimmed().toLower();
	if (k.isEmpty())
		return true;
	for (const QString& t : pageTypes)
	{
		const QString tl = t.trimmed().toLower();
		if (!tl.isEmpty() && (k.contains(tl) || tl.contains(k)))
			return true;
	}
	return false;
}

QJsonObject SuneerLayoutModel::toJson() const
{
	QJsonArray widths;
	for (const WidthRule& r : widthRules)
		widths << QJsonObject{{"maxChars", r.maxChars}, {"columns", r.columns}};
	QJsonArray heads;
	for (const HeadRule& r : headlineRules)
		heads << QJsonObject{{"columns", r.columns}, {"style", r.style}};
	QJsonArray photos;
	for (const PhotoRule& r : photoRules)
		photos << QJsonObject{{"storyColumns", r.storyColumns}, {"photoColumns", r.photoColumns}};
	QJsonObject model{
		{"id", id}, {"name", name}, {"engine", engine}, {"origin", origin}, {"description", description},
		{"pageTypes", strList(pageTypes)},
		{"grid", QJsonObject{{"columns", columns}, {"fromGuides", gridFromGuides}, {"columnWidthMm", columnWidthMm}, {"gutterMm", gutterMm},
		                     {"useDocumentMargins", useDocMargins},
		                     {"margins", QJsonObject{{"top", marginTopMm}, {"bottom", marginBottomMm}, {"left", marginLeftMm}, {"right", marginRightMm}}}}},
		{"structure", QJsonObject{{"decks", decks}, {"leftRail", leftRail}, {"rightRail", rightRail},
		                          {"leadPosition", leadPosition}, {"leadColumns", leadColumns}}},
		{"stories", QJsonObject{{"widthByLength", widths}, {"fixedColumns", fixedColumns},
		                        {"headlineByWidth", heads}, {"leadHeadlineStyle", leadHeadlineStyle}}},
		{"photos", QJsonObject{{"widthByStory", photos}, {"fixedColumns", fixedPhotoColumns},
		                       {"position", photoPosition}, {"maxHeightMm", photoMaxHeightMm}}},
		{"spacing", QJsonObject{{"gapVerticalMm", gapVMm}, {"gapHorizontalMm", gutterMm}, {"headlineBodyMm", headlineBodyMm}, {"kickerHeadlineMm", kickerHeadlineMm},
		                        {"photoTextMm", photoTextMm}, {"wrapPaddingMm", wrapPadMm}, {"captionGapMm", captionGapMm},
		                        {"captionTextMm", captionTextMm}, {"textInsetMm", insetMm}, {"columnGapMm", columnGapMm},
		                        {"ruleWidthPt", ruleWidthPt}, {"ruleColor", ruleColor}}}};
	QJsonObject root{{"format", QString::fromLatin1(kFormat)}, {"version", 1},
	                 {"_comment", "Faircode Layout Model. Sizes in mm unless the key says Pt. Edit with Layout > Layout Models..."},
	                 {"model", model}};
	if (isObituary())
		root["obituary"] = obituary;
	return root;
}

SuneerLayoutModel SuneerLayoutModel::fromJson(const QJsonObject& root)
{
	SuneerLayoutModel m;
	const QJsonObject o = root["model"].toObject();
	m.id = o["id"].toString();
	m.name = o["name"].toString(m.id);
	m.engine = o["engine"].toString(QStringLiteral("news"));
	m.origin = o["origin"].toString(QStringLiteral("user"));
	m.description = o["description"].toString();
	m.pageTypes = fromStrList(o["pageTypes"]);
	const QJsonObject g = o["grid"].toObject();
	m.columns = qBound(1, g["columns"].toInt(m.columns), 24);
	m.gridFromGuides = g.contains(QLatin1String("fromGuides")) ? g["fromGuides"].toBool() : (m.id == QLatin1String("normal"));
	m.columnWidthMm = qMax(0.0, num(g, "columnWidthMm", m.columnWidthMm));
	m.gutterMm = qMax(0.0, num(g, "gutterMm", m.gutterMm));
	m.useDocMargins = g["useDocumentMargins"].toBool(m.useDocMargins);
	const QJsonObject mg = g["margins"].toObject();
	m.marginTopMm = num(mg, "top", m.marginTopMm);
	m.marginBottomMm = num(mg, "bottom", m.marginBottomMm);
	m.marginLeftMm = num(mg, "left", m.marginLeftMm);
	m.marginRightMm = num(mg, "right", m.marginRightMm);
	const QJsonObject st = o["structure"].toObject();
	m.decks = qBound(0, st["decks"].toInt(m.decks), 8);
	m.leftRail = st["leftRail"].toBool(m.leftRail);
	m.rightRail = st["rightRail"].toBool(m.rightRail);
	m.leadPosition = st["leadPosition"].toString(m.leadPosition);
	m.leadColumns = qBound(1, st["leadColumns"].toInt(m.leadColumns), 24);
	const QJsonObject so = o["stories"].toObject();
	for (const QJsonValue& v : so["widthByLength"].toArray())
		m.widthRules << WidthRule { v.toObject()["maxChars"].toInt(), qMax(1, v.toObject()["columns"].toInt(1)) };
	std::sort(m.widthRules.begin(), m.widthRules.end(), [](const WidthRule& a, const WidthRule& b) { return a.maxChars < b.maxChars; });
	m.fixedColumns = qMax(0, so["fixedColumns"].toInt(0));
	for (const QJsonValue& v : so["headlineByWidth"].toArray())
		m.headlineRules << HeadRule { qMax(1, v.toObject()["columns"].toInt(1)), v.toObject()["style"].toString() };
	std::sort(m.headlineRules.begin(), m.headlineRules.end(), [](const HeadRule& a, const HeadRule& b) { return a.columns < b.columns; });
	m.leadHeadlineStyle = so["leadHeadlineStyle"].toString();
	const QJsonObject po = o["photos"].toObject();
	for (const QJsonValue& v : po["widthByStory"].toArray())
		m.photoRules << PhotoRule { qMax(1, v.toObject()["storyColumns"].toInt(1)), qMax(1, v.toObject()["photoColumns"].toInt(1)) };
	std::sort(m.photoRules.begin(), m.photoRules.end(), [](const PhotoRule& a, const PhotoRule& b) { return a.storyColumns < b.storyColumns; });
	m.fixedPhotoColumns = qMax(0, po["fixedColumns"].toInt(0));
	m.photoPosition = po["position"].toString(m.photoPosition);
	m.photoMaxHeightMm = qMax(10.0, num(po, "maxHeightMm", m.photoMaxHeightMm));
	const QJsonObject sp = o["spacing"].toObject();
	m.gapVMm = qMax(0.0, num(sp, "gapVerticalMm", m.gapVMm));
	m.gutterMm = qMax(0.0, num(sp, "gapHorizontalMm", m.gutterMm));
	m.headlineBodyMm = qMax(0.0, num(sp, "headlineBodyMm", m.headlineBodyMm));
	m.kickerHeadlineMm = qMax(0.0, num(sp, "kickerHeadlineMm", m.kickerHeadlineMm));
	m.photoTextMm = qMax(0.0, num(sp, "photoTextMm", m.photoTextMm));
	m.wrapPadMm = qMax(0.0, num(sp, "wrapPaddingMm", m.wrapPadMm));
	m.captionGapMm = qMax(0.0, num(sp, "captionGapMm", m.captionGapMm));
	m.captionTextMm = qMax(0.0, num(sp, "captionTextMm", m.captionTextMm));
	m.insetMm = qMax(0.0, num(sp, "textInsetMm", m.insetMm));
	m.columnGapMm = qMax(0.0, num(sp, "columnGapMm", m.columnGapMm));
	m.ruleWidthPt = qMax(0.0, num(sp, "ruleWidthPt", m.ruleWidthPt));
	m.ruleColor = sp["ruleColor"].toString(m.ruleColor);
	m.obituary = root["obituary"].toObject();
	return m;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

namespace SuneerLayoutModels
{
namespace {
QList<SuneerLayoutModel> s_models;
bool s_loaded = false;
bool s_migrating = false;

QString prefsDir()
{
	QString base = PrefsManager::instance().preferencesLocation();
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.config/scribus");
	while (base.endsWith('/'))
		base.chop(1);
	return base;
}

bool writeJson(const QString& path, const QJsonObject& o, QString* error)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error)
			*error = QObject::tr("cannot write %1").arg(path);
		return false;
	}
	f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
	return true;
}

QJsonObject readJson(const QString& path, QString* error = nullptr)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = QObject::tr("cannot read %1").arg(path);
		return QJsonObject();
	}
	QJsonParseError pe;
	const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
	if (pe.error != QJsonParseError::NoError || !d.isObject())
	{
		if (error)
			*error = QObject::tr("%1: %2").arg(path, pe.errorString());
		return QJsonObject();
	}
	return d.object();
}

// The spacing a model had in the old one-file store (global + per model override).
void oldSpacing(const QString& key, SuneerLayoutModel& m)
{
	// shipped defaults of the old store
	double v[SuneerSpacing::KeyCount] = { 4.8, 8.0, 0.0, 2.0, 0.0, 2.0, 0.0, 0.0, 2.5, 0.0, 0.0 };
	QString ruleColor = QStringLiteral("Black");
	const QJsonObject root = readJson(prefsDir() + QStringLiteral("/layout-spacing.json"));
	const char* const names[SuneerSpacing::KeyCount] = { "moduleGapV", "moduleGapH", "photoText", "wrapPad", "captionGap", "captionText", "headlineBody", "inset", "columnGap", "ruleWidth", "kickerHeadline" };
	if (!root.isEmpty())
	{
		const QJsonObject g = root["global"].toObject();
		for (int k = 0; k < SuneerSpacing::KeyCount; ++k)
			if (g.contains(QLatin1String(names[k])))
				v[k] = qMax(0.0, g[QLatin1String(names[k])].toDouble());
		if (!g["ruleColor"].toString().isEmpty())
			ruleColor = g["ruleColor"].toString();
		const QJsonObject ov = root["models"].toObject()[key].toObject();
		for (int k = 0; k < SuneerSpacing::KeyCount; ++k)
			if (ov.contains(QLatin1String(names[k])))
				v[k] = qMax(0.0, ov[QLatin1String(names[k])].toDouble());
	}
	else
	{
		// never had the spacing file: the even older News Browser settings
		QSettings cfg("Faircode", "ScribusNews");
		v[SuneerSpacing::ModuleGapH] = cfg.value("layout/gridGutterMm", v[SuneerSpacing::ModuleGapH]).toDouble();
		v[SuneerSpacing::WrapPad] = cfg.value("layout/wrapGapMm", v[SuneerSpacing::WrapPad]).toDouble();
		v[SuneerSpacing::ColumnGap] = cfg.value("layout/colGapMm", v[SuneerSpacing::ColumnGap]).toDouble();
		if (key == QLatin1String("obituary"))
		{
			v[SuneerSpacing::ModuleGapV] = 2.3;
			v[SuneerSpacing::ModuleGapH] = 3.0;
		}
	}
	SuneerSpacing::Values sv {};
	for (int k = 0; k < SuneerSpacing::KeyCount; ++k)
		sv.v[k] = v[k];
	sv.ruleColor = ruleColor;
	m.setSpacing(sv);
}

SuneerLayoutModel makeNormal()
{
	SuneerLayoutModel m;
	m.id = QStringLiteral("normal");
	m.name = QStringLiteral("Normal (4+3)");
	m.origin = QStringLiteral("built-in");
	m.description = QStringLiteral("The News Browser's house placement: every story 4 grid columns wide, its photo 3 columns.");
	QSettings cfg("Faircode", "ScribusNews");
	m.columns = qBound(1, cfg.value("layout/gridColumns", 8).toInt(), 24);
	m.gridFromGuides = true;            // as before: the page's column guides when it has them
	m.fixedColumns = qBound(1, cfg.value("layout/storyColumns", 4).toInt(), 8);
	m.fixedPhotoColumns = qBound(1, cfg.value("layout/photoColumns", 3).toInt(), 8);
	m.photoPosition = cfg.value("layout/photoPosition", QStringLiteral("right")).toString();
	m.photoMaxHeightMm = cfg.value("layout/photoMaxHeightMm", 100.0).toDouble();
	m.widthRules = { {999999, m.fixedColumns} };
	return m;
}

SuneerLayoutModel makeObituary()
{
	SuneerLayoutModel m;
	m.id = QStringLiteral("obituary");
	m.name = QStringLiteral("Obituary \u2013 news right");
	m.engine = QStringLiteral("obituary");
	m.origin = QStringLiteral("built-in");
	m.description = QStringLiteral("Photo + text obit modules packed in columns 1-8, the 2-column obituary news stacked in columns 9-10 (Page-15_ObitKRN).");
	m.pageTypes = { QStringLiteral("Obituary"), QStringLiteral("obit"), QStringLiteral("ചരമ") };
	m.columns = 10;
	m.gutterMm = 3.0;
	m.gapVMm = 2.3;
	m.captionGapMm = 1.0;     // photo -> caption
	m.captionTextMm = 1.5;    // caption (or name frame) -> the text under it
	m.photoPosition = QStringLiteral("top");
	return m;
}

// Numbers measured from our own pages (resources/autolayout/autolayout-rules.json).
SuneerLayoutModel makeLearned()
{
	SuneerLayoutModel m;
	m.id = QStringLiteral("pradeshikam-learned");
	m.name = QStringLiteral("Pradeshikam (learned)");
	m.origin = QStringLiteral("built-in");
	m.description = QStringLiteral("Measured from 7 general pages / 105 modules: 8 columns, brief rail at the left, 5-column lead at the top, width by body length.");
	m.pageTypes = { QStringLiteral("Pradeshikam"), QStringLiteral("General"), QStringLiteral("News") };
	m.columns = 8;
	m.gutterMm = 8.0;
	m.leftRail = true;
	m.leadPosition = QStringLiteral("top-right");
	m.leadColumns = 5;
	m.widthRules = { {450, 1}, {900, 2}, {1300, 3}, {999999, 4} };
	m.headlineRules = { {1, QStringLiteral("18 M")}, {2, QStringLiteral("24 M")}, {3, QStringLiteral("32 M")} };
	m.photoRules = { {1, 1}, {3, 1}, {4, 2}, {5, 2} };
	m.photoPosition = QStringLiteral("middle");
	m.photoMaxHeightMm = 90.0;
	m.gapVMm = 4.8;
	m.wrapPadMm = 2.0;
	m.captionTextMm = 2.0;
	m.columnGapMm = 2.5;
	return m;
}

void load()
{
	s_models.clear();
	const QDir d(dir());
	for (const QFileInfo& fi : d.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name))
	{
		const QJsonObject root = readJson(fi.absoluteFilePath());
		if (root["format"].toString() != QLatin1String(kFormat))
			continue;
		SuneerLayoutModel m = SuneerLayoutModel::fromJson(root);
		m.id = fi.completeBaseName();          // the file name is the id
		if (m.name.isEmpty())
			m.name = m.id;
		s_models << m;
	}
	std::stable_sort(s_models.begin(), s_models.end(), [](const SuneerLayoutModel& a, const SuneerLayoutModel& b) {
		if (a.builtIn() != b.builtIn())
			return a.builtIn();
		return a.name.toLower() < b.name.toLower();
	});
	s_loaded = true;
}
} // namespace

QString dir() { return prefsDir() + QStringLiteral("/layout-models"); }

QString filePath(const QString& id) { return dir() + QStringLiteral("/") + id + QStringLiteral(".json"); }

void ensureMigrated()
{
	if (s_migrating || s_loaded)
		return;
	s_migrating = true;
	struct Guard { ~Guard() { s_migrating = false; } } guard;
	QDir().mkpath(dir());
	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
	bool wrote = false;

	// Obituary: the old obituary.json (the obit engine's own file) becomes the
	// "obituary" part of the model file; the spacing comes from the old store.
	const QString obitPath = filePath(QStringLiteral("obituary"));
	const QJsonObject obitRoot = readJson(obitPath);
	if (obitRoot["format"].toString() != QLatin1String(kFormat))
	{
		SuneerLayoutModel m = makeObituary();
		oldSpacing(QStringLiteral("obituary"), m);
		if (!obitRoot.isEmpty())
		{
			QFile::copy(obitPath, obitPath + QStringLiteral(".pre-models-") + stamp + QStringLiteral(".bak"));
			m.obituary = obitRoot;
			const QJsonObject grid = obitRoot["grid"].toObject();
			m.columns = qBound(1, grid["columns"].toInt(m.columns), 24);
			m.marginTopMm = num(grid, "contentTopMm", m.marginTopMm);
		}
		// an empty "obituary" part: SuneerObitModel fills in its defaults on the first load
		writeJson(obitPath, m.toJson(), nullptr);
		wrote = true;
	}
	// "Obituary - news left": the same obituary model with the 2-column news in columns 1-2 and the
	// obit modules in 3-10. Made once from the obituary file (its obit part is copied: photo
	// processing, styles, classification stay the same); the old "Obituary" is named "news right".
	{
		const QString leftPath = filePath(QStringLiteral("obituary-left"));
		QJsonObject right = readJson(obitPath);
		if (right["format"].toString() == QLatin1String(kFormat))
		{
			SuneerLayoutModel rm = SuneerLayoutModel::fromJson(right);
			if (rm.name == QLatin1String("Obituary"))
			{
				rm.name = makeObituary().name;
				rm.description = makeObituary().description;
				writeJson(obitPath, rm.toJson(), nullptr);
				wrote = true;
			}
			if (!QFile::exists(leftPath))
			{
				SuneerLayoutModel lm = rm;
				lm.id = QStringLiteral("obituary-left");
				lm.name = QStringLiteral("Obituary \u2013 news left");
				lm.origin = QStringLiteral("built-in");
				lm.description = QStringLiteral("The 2-column obituary news stacked in columns 1-2, the obit modules packed in columns 3-10.");
				QJsonObject ob = lm.obituary;
				QJsonObject grid = ob["grid"].toObject();
				const int cols = qBound(1, grid["columns"].toInt(10), 30);
				grid["obitColumns"] = QJsonArray { qMin(3, cols), cols };
				grid["newsColumns"] = QJsonArray { 1, qMin(2, cols) };
				ob["grid"] = grid;
				ob["name"] = lm.name;
				lm.obituary = ob;
				writeJson(leftPath, lm.toJson(), nullptr);
				wrote = true;
			}
		}
	}
	// the 2-column news gaps of the obituary models (2026-10-24): files still at the old zeros get the defaults once
	for (const QString& id : { QStringLiteral("obituary"), QStringLiteral("obituary-left") })
	{
		const QJsonObject root = readJson(filePath(id));
		if (root["format"].toString() != QLatin1String(kFormat))
			continue;
		SuneerLayoutModel m = SuneerLayoutModel::fromJson(root);
		if (m.captionGapMm == 0.0 && (m.captionTextMm == 0.0 || m.captionTextMm == 2.0))   // the old values
		{
			const SuneerLayoutModel d = makeObituary();
			m.captionGapMm = d.captionGapMm;
			m.captionTextMm = d.captionTextMm;
			m.photoTextMm = 0.0;
			writeJson(filePath(id), m.toJson(), nullptr);
			wrote = true;
		}
	}
	if (!QFile::exists(filePath(QStringLiteral("normal"))))
	{
		SuneerLayoutModel m = makeNormal();
		oldSpacing(QStringLiteral("normal"), m);
		writeJson(filePath(m.id), m.toJson(), nullptr);
		wrote = true;
	}
	if (!QFile::exists(filePath(QStringLiteral("pradeshikam-learned"))))
	{
		SuneerLayoutModel m = makeLearned();
		writeJson(filePath(m.id), m.toJson(), nullptr);
		wrote = true;
	}
	// The old one-file spacing store is not read any more; keep it as a backup.
	const QString oldSpacingFile = prefsDir() + QStringLiteral("/layout-spacing.json");
	if (QFile::exists(oldSpacingFile))
		QFile::rename(oldSpacingFile, oldSpacingFile + QStringLiteral(".migrated-") + stamp);
	if (wrote || !s_loaded)
		load();
}

QList<SuneerLayoutModel> all()
{
	if (!s_loaded)
		ensureMigrated();
	if (!s_loaded)
		load();
	return s_models;
}

bool exists(const QString& id)
{
	for (const SuneerLayoutModel& m : all())
		if (m.id == id)
			return true;
	return false;
}

namespace {
std::unique_ptr<SuneerLayoutModel> s_editing;
}

void setEditing(const SuneerLayoutModel* model)
{
	if (model)
		s_editing = std::make_unique<SuneerLayoutModel>(*model);
	else
		s_editing.reset();
}

const SuneerLayoutModel* editing()
{
	return s_editing.get();
}

SuneerLayoutModel byId(const QString& id)
{
	if (s_editing && s_editing->id == id)
		return *s_editing;
	return savedById(id);
}

SuneerLayoutModel savedById(const QString& id)
{
	const QList<SuneerLayoutModel> list = all();
	for (const SuneerLayoutModel& m : list)
		if (m.id == id)
			return m;
	for (const SuneerLayoutModel& m : list)
		if (m.id == QLatin1String("normal"))
			return m;
	SuneerLayoutModel m = makeNormal();
	oldSpacing(QStringLiteral("normal"), m);
	return m;
}

bool save(const SuneerLayoutModel& in, QString* error)
{
	SuneerLayoutModel m = in;
	if (m.id.isEmpty())
		m.id = newId(m.name);
	if (m.isObituary() && m.obituary.isEmpty())
	{
		// keep the obit engine's part that is on disk
		const QJsonObject old = readJson(filePath(m.id));
		m.obituary = old["obituary"].toObject();
	}
	if (!writeJson(filePath(m.id), m.toJson(), error))
		return false;
	load();
	return true;
}

bool remove(const QString& id, QString* error)
{
	const SuneerLayoutModel m = byId(id);
	if (m.id != id)
		return true;
	if (m.builtIn())
	{
		if (error)
			*error = QObject::tr("%1 is a built-in model; it can be changed but not deleted").arg(m.name);
		return false;
	}
	if (!QFile::remove(filePath(id)))
	{
		if (error)
			*error = QObject::tr("cannot delete %1").arg(filePath(id));
		return false;
	}
	load();
	return true;
}

QString newId(const QString& name)
{
	QString base = name.toLower();
	base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
	while (base.startsWith('-'))
		base.remove(0, 1);
	while (base.endsWith('-'))
		base.chop(1);
	if (base.isEmpty())
		base = QStringLiteral("model");
	QString id = base;
	for (int n = 2; QFile::exists(filePath(id)); ++n)
		id = base + QStringLiteral("-%1").arg(n);
	return id;
}

bool exportTo(const QString& id, const QString& path, QString* error)
{
	const QString src = filePath(id);
	const QJsonObject root = readJson(src, error);
	if (root.isEmpty())
		return false;
	return writeJson(path, root, error);
}

QString importFrom(const QString& path, QString* error)
{
	const QJsonObject root = readJson(path, error);
	if (root.isEmpty())
		return QString();
	if (root["format"].toString() != QLatin1String(kFormat))
	{
		if (error)
			*error = QObject::tr("%1 is not a Layout Model file").arg(path);
		return QString();
	}
	SuneerLayoutModel m = SuneerLayoutModel::fromJson(root);
	if (m.name.isEmpty())
		m.name = QFileInfo(path).completeBaseName();
	QString id = m.id.isEmpty() ? newId(m.name) : m.id;
	if (QFile::exists(filePath(id)))
	{
		id = newId(m.name + QStringLiteral(" imported"));
		m.name += QStringLiteral(" (imported)");
	}
	m.id = id;
	m.origin = QStringLiteral("imported");
	if (!save(m, error))
		return QString();
	return id;
}

SuneerLayoutModel builtInDefault(const QString& id)
{
	SuneerLayoutModel m;
	if (id == QLatin1String("obituary") || id == QLatin1String("obituary-left"))
	{
		m = makeObituary();
		m.obituary = byId(id).obituary;       // the obit engine values are kept (edited on their own tab)
		if (id == QLatin1String("obituary-left"))
		{
			m.id = id;
			m.name = QStringLiteral("Obituary \u2013 news left");
			m.description = QStringLiteral("The 2-column obituary news stacked in columns 1-2, the obit modules packed in columns 3-10.");
		}
	}
	else if (id == QLatin1String("pradeshikam-learned"))
		m = makeLearned();
	else
	{
		m = makeNormal();
		m.gapVMm = 4.8; m.gutterMm = 8.0; m.wrapPadMm = 2.0; m.captionTextMm = 2.0; m.columnGapMm = 2.5;
	}
	return m;
}

void reload() { load(); }

QString lastModelFor(const QString& pageKey)
{
	QString k = pageKey.trimmed();
	k.replace('/', '_');
	QSettings s("Faircode", "ScribusNews");
	return s.value(QStringLiteral("layoutModel/") + (k.isEmpty() ? QStringLiteral("_default") : k)).toString();
}

void setLastModelFor(const QString& pageKey, const QString& id)
{
	QString k = pageKey.trimmed();
	k.replace('/', '_');
	QSettings s("Faircode", "ScribusNews");
	s.setValue(QStringLiteral("layoutModel/") + (k.isEmpty() ? QStringLiteral("_default") : k), id);
}
} // namespace SuneerLayoutModels
