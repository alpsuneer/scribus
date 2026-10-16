/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerspacing.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include "prefsmanager.h"
#include "ui/suneer_obit_model.h"

namespace SuneerSpacing
{
namespace {
Store s_store;
bool s_loaded = false;

const char* const kNames[KeyCount] = { "moduleGapV", "moduleGapH", "photoText", "wrapPad", "captionGap", "captionText", "headlineBody", "inset", "columnGap", "ruleWidth" };
const double kDefaults[KeyCount] = { 4.8, 8.0, 0.0, 2.0, 0.0, 2.0, 0.0, 0.0, 2.5, 0.0 };

QJsonObject valuesJson(const double* v, const QString& ruleColor)
{
	QJsonObject o;
	for (int k = 0; k < KeyCount; ++k)
		o[QString::fromLatin1(kNames[k])] = v[k];
	o["ruleColor"] = ruleColor;
	return o;
}

Store fromJson(const QJsonObject& root)
{
	Store s = defaults();
	const QJsonObject g = root["global"].toObject();
	for (int k = 0; k < KeyCount; ++k)
		if (g.contains(QString::fromLatin1(kNames[k])))
			s.global.v[k] = qMax(0.0, g[QString::fromLatin1(kNames[k])].toDouble());
	if (!g["ruleColor"].toString().isEmpty())
		s.global.ruleColor = g["ruleColor"].toString();
	const QJsonObject ov = root["models"].toObject();
	s.overrides.clear();
	for (auto it = ov.constBegin(); it != ov.constEnd(); ++it)
	{
		const QJsonObject m = it.value().toObject();
		QMap<int, double> map;
		for (int k = 0; k < KeyCount; ++k)
			if (m.contains(QString::fromLatin1(kNames[k])))
				map[k] = qMax(0.0, m[QString::fromLatin1(kNames[k])].toDouble());
		s.overrides[it.key()] = map;
	}
	return s;
}

QJsonObject toJson(const Store& s)
{
	QJsonObject models;
	for (auto it = s.overrides.constBegin(); it != s.overrides.constEnd(); ++it)
	{
		QJsonObject m;
		for (auto kt = it.value().constBegin(); kt != it.value().constEnd(); ++kt)
			m[QString::fromLatin1(kNames[kt.key()])] = kt.value();
		models[it.key()] = m;
	}
	return QJsonObject{{"version", 1},
	                   {"_comment", "Faircode Layout spacing for every News Browser layout model. mm, except ruleWidth (pt). A key under models.<name> overrides the global value."},
	                   {"global", valuesJson(s.global.v, s.global.ruleColor)}, {"models", models}};
}
} // namespace

QStringList models() { return { QStringLiteral("normal"), QStringLiteral("obituary"), QStringLiteral("auto") }; }

QString modelTitle(const QString& model)
{
	if (model == QLatin1String("normal")) return QObject::tr("Normal (4+3)");
	if (model == QLatin1String("obituary")) return QObject::tr("Obituary");
	if (model == QLatin1String("auto")) return QObject::tr("Auto Layout");
	return model;
}

QString keyName(Key k) { return QString::fromLatin1(kNames[k]); }

QString keyTitle(Key k)
{
	switch (k)
	{
		case ModuleGapV: return QObject::tr("Module gap vertical");
		case ModuleGapH: return QObject::tr("Module gap horizontal (gutter)");
		case PhotoText: return QObject::tr("Photo-to-text gap");
		case WrapPad: return QObject::tr("Photo text-wrap padding");
		case CaptionGap: return QObject::tr("Caption gap (photo → caption)");
		case CaptionText: return QObject::tr("Caption-to-text gap");
		case HeadlineBody: return QObject::tr("Headline-to-body extra space");
		case Inset: return QObject::tr("Text-frame inset");
		case ColumnGap: return QObject::tr("Text column gap (inside a story)");
		case RuleWidth: return QObject::tr("Separator rule between modules");
		default: return QString();
	}
}

QString keyUnit(Key k) { return k == RuleWidth ? QStringLiteral("pt") : QStringLiteral("mm"); }

Values Store::forModel(const QString& model) const
{
	Values v = global;
	const auto it = overrides.constFind(model);
	if (it != overrides.constEnd())
		for (auto kt = it.value().constBegin(); kt != it.value().constEnd(); ++kt)
			v.v[kt.key()] = kt.value();
	return v;
}

QString filePath()
{
	QString base = PrefsManager::instance().preferencesLocation();
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.config/scribus");
	while (base.endsWith('/'))
		base.chop(1);
	return base + QStringLiteral("/layout-spacing.json");
}

Store defaults()
{
	Store s;
	for (int k = 0; k < KeyCount; ++k)
		s.global.v[k] = kDefaults[k];
	s.overrides[QStringLiteral("obituary")] = QMap<int, double>{ {ModuleGapV, 2.3}, {ModuleGapH, 3.0} };
	s.overrides[QStringLiteral("normal")] = QMap<int, double>();
	s.overrides[QStringLiteral("auto")] = QMap<int, double>();
	return s;
}

const Store& store()
{
	if (s_loaded)
		return s_store;
	s_loaded = true;
	QFile f(filePath());
	if (f.open(QIODevice::ReadOnly))
	{
		s_store = fromJson(QJsonDocument::fromJson(f.readAll()).object());
		return s_store;
	}
	// First run: the defaults, carrying over what the old settings had.
	s_store = defaults();
	QSettings cfg("Faircode", "ScribusNews");
	s_store.global.v[ModuleGapH] = cfg.value("layout/gridGutterMm", kDefaults[ModuleGapH]).toDouble();
	s_store.global.v[WrapPad] = cfg.value("layout/wrapGapMm", kDefaults[WrapPad]).toDouble();
	s_store.global.v[ColumnGap] = cfg.value("layout/colGapMm", kDefaults[ColumnGap]).toDouble();
	const SuneerObitModel om = SuneerObitModel::load();
	s_store.overrides[QStringLiteral("obituary")][ModuleGapV] = om.moduleGapMm;
	s_store.overrides[QStringLiteral("obituary")][ModuleGapH] = om.gridGapMm;
	if (qAbs(om.newsColGapMm - s_store.global.v[ColumnGap]) > 0.01)
		s_store.overrides[QStringLiteral("obituary")][ColumnGap] = om.newsColGapMm;
	save(s_store);
	return s_store;
}

bool save(const Store& s, QString* error)
{
	const QString path = filePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error)
			*error = QObject::tr("cannot write %1").arg(path);
		return false;
	}
	f.write(QJsonDocument(toJson(s)).toJson(QJsonDocument::Indented));
	s_store = s;
	s_loaded = true;
	return true;
}

bool exportTo(const QString& path, QString* error)
{
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error)
			*error = QObject::tr("cannot write %1").arg(path);
		return false;
	}
	f.write(QJsonDocument(toJson(store())).toJson(QJsonDocument::Indented));
	return true;
}

bool importFrom(const QString& path, QString* error)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = QObject::tr("cannot read %1").arg(path);
		return false;
	}
	QJsonParseError pe;
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
	if (pe.error != QJsonParseError::NoError || !doc.object().contains("global"))
	{
		if (error)
			*error = QObject::tr("%1 is not a spacing file").arg(path);
		return false;
	}
	return save(fromJson(doc.object()), error);
}

Values forModel(const QString& model)
{
	return store().forModel(model);
}
} // namespace SuneerSpacing
