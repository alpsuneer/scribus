/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerspacing.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include "scpaths.h"
#include "suneerlayoutmodel.h"

namespace SuneerSpacing
{
namespace {
const char* const kNames[KeyCount] = { "moduleGapV", "moduleGapH", "photoText", "wrapPad", "captionGap", "captionText", "headlineBody", "inset", "columnGap", "ruleWidth", "kickerHeadline" };

// built-in defaults (the Normal model's), used when the house rules file has no number
const double kBuiltIn[KeyCount] = { 4.8, 8.0, 0.0, 2.0, 0.0, 2.0, 0.0, 0.0, 2.5, 0.0, 0.0 };

QJsonObject houseRules()
{
	static QJsonObject cached;
	static bool loaded = false;
	if (loaded)
		return cached;
	loaded = true;
	const QStringList candidates = { ScPaths::instance().shareDir() + QStringLiteral("autolayout/autolayout-rules.json"),
	                                 ScPaths::instance().shareDir() + QStringLiteral("../../resources/autolayout/autolayout-rules.json") };
	for (const QString& path : candidates)
	{
		QFile f(path);
		if (f.open(QIODevice::ReadOnly))
		{
			cached = QJsonDocument::fromJson(f.readAll()).object();
			if (!cached.isEmpty())
				break;
		}
	}
	return cached;
}

// a number at "a/b/c" in the house rules, or NaN
double houseNumber(const QString& path)
{
	QJsonValue v = houseRules();
	for (const QString& part : path.split(QLatin1Char('/')))
		v = v.toObject().value(part);
	return v.isDouble() ? v.toDouble() : qQNaN();
}
}

double houseDefault(Key k, QString* source)
{
	// where each gap was measured (autolayout-rules.json, tools/autolayout/analyze.py)
	QString path;
	switch (k)
	{
		case ModuleGapH: path = QStringLiteral("grid/gutter"); break;
		case ModuleGapV: path = QStringLiteral("gaps/vertical_between_modules/median"); break;
		case ColumnGap: path = QStringLiteral("story_frame/column_gap"); break;
		case WrapPad: path = QStringLiteral("engine_defaults_proposed/wrap_offset_mm"); break;
		default: break;
	}
	const double v = path.isEmpty() ? qQNaN() : houseNumber(path);
	if (!qIsNaN(v) && v >= 0.0)
	{
		if (source)
			*source = QObject::tr("house rules (%1)").arg(path);
		return v;
	}
	if (source)
		*source = QObject::tr("built-in default");
	return kBuiltIn[k];
}

Values houseDefaults()
{
	Values v {};
	for (int k = 0; k < KeyCount; ++k)
		v.v[k] = houseDefault(Key(k));
	return v;
}

QList<Key> gapKeys()
{
	return { HeadlineBody, KickerHeadline, ColumnGap, WrapPad, PhotoText, CaptionGap, CaptionText, ModuleGapH, ModuleGapV };
}

bool isStoryGap(Key k)
{
	return k != ModuleGapH && k != ModuleGapV && k != RuleWidth;
}

QString gapLabel(Key k)
{
	switch (k)
	{
		case HeadlineBody: return QObject::tr("Headline \u2192 body text");
		case KickerHeadline: return QObject::tr("Subheading / kicker \u2192 headline");
		case ColumnGap: return QObject::tr("Body column gutter");
		case WrapPad: return QObject::tr("Photo \u2192 text, beside (wrap)");
		case PhotoText: return QObject::tr("Photo \u2192 text, above / below");
		case CaptionGap: return QObject::tr("Photo \u2192 caption");
		case CaptionText: return QObject::tr("Caption \u2192 text");
		case ModuleGapH: return QObject::tr("Story to story, horizontal");
		case ModuleGapV: return QObject::tr("Story to story, vertical");
		default: return keyTitle(k);
	}
}

QString gapsToString(const Values& v)
{
	QJsonObject o;
	for (Key k : gapKeys())
		o.insert(keyName(k), qRound(v.v[k] * 100.0) / 100.0);
	return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool gapsFromString(const QString& json, Values& base)
{
	const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
	if (!d.isObject())
		return false;
	const QJsonObject o = d.object();
	for (int k = 0; k < KeyCount; ++k)
	{
		const QJsonValue jv = o.value(keyName(Key(k)));
		if (jv.isDouble() && jv.toDouble() >= 0.0 && jv.toDouble() <= 100.0)
			base.v[k] = jv.toDouble();
	}
	return true;
}

QString keyName(Key k) { return QString::fromLatin1(kNames[k]); }

QString keyTitle(Key k)
{
	switch (k)
	{
		case ModuleGapV: return QObject::tr("Gap between modules, vertical");
		case ModuleGapH: return QObject::tr("Gap between modules, horizontal (gutter)");
		case PhotoText: return QObject::tr("Photo-to-text gap");
		case WrapPad: return QObject::tr("Photo text-wrap padding");
		case CaptionGap: return QObject::tr("Photo-to-caption gap");
		case CaptionText: return QObject::tr("Caption-to-text gap");
		case HeadlineBody: return QObject::tr("Headline-to-body gap");
		case Inset: return QObject::tr("Text-frame inset");
		case ColumnGap: return QObject::tr("Text column gap (inside a story)");
		case RuleWidth: return QObject::tr("Separator rule between modules");
		case KickerHeadline: return QObject::tr("Kicker-to-headline gap");
		default: return QString();
	}
}

QString keyUnit(Key k) { return k == RuleWidth ? QStringLiteral("pt") : QStringLiteral("mm"); }

Values forModel(const QString& modelId)
{
	return SuneerLayoutModels::byId(modelId).spacing();
}
} // namespace SuneerSpacing
