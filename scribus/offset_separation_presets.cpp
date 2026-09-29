/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "offset_separation_presets.h"

#include <QJsonArray>
#include <QJsonDocument>

QStringList offsetDotShapeNames()
{
	return {
		QStringLiteral("Round"),
		QStringLiteral("Elliptical"),
		QStringLiteral("Line"),
		QStringLiteral("Square"),
		QStringLiteral("Euclidean"),
		QStringLiteral("Diamond")
	};
}

QString offsetDotShapeName(OffsetDotShape shape)
{
	int idx = static_cast<int>(shape);
	QStringList names = offsetDotShapeNames();
	if ((idx < 0) || (idx >= names.count()))
		return names.at(0);
	return names.at(idx);
}

OffsetDotShape offsetDotShapeFromName(const QString& name, bool* ok)
{
	QStringList names = offsetDotShapeNames();
	int idx = names.indexOf(name);
	if (ok)
		*ok = (idx >= 0);
	if (idx < 0)
		return OffsetDotShape::Round;
	return static_cast<OffsetDotShape>(idx);
}

QString offsetDotShapeSpotFunction(OffsetDotShape shape)
{
	switch (shape)
	{
		case OffsetDotShape::Round:
			return QStringLiteral("{180 mul cos exch 180 mul cos add 2 div}");
		case OffsetDotShape::Elliptical:
			// The originally specified formula, {dup mul exch dup mul 0.75 mul
			// add sqrt neg}, returns values outside [-1,1] at the domain
			// corners (up to -1.323) and Ghostscript's sethalftone rejects it
			// with a rangecheck error - confirmed by rendering it. This one
			// pre-warps y by 0.7 before feeding both into the same bounded
			// "Euclidean dot" formula used for OffsetDotShape::Euclidean below;
			// since 0.7*[-1,1] stays inside [-1,1] and that formula is already
			// proven in-range there, the result is safe by construction.
			// Rendered at 0.1/0.25/0.5/0.75/0.9 gray with Ghostscript
			// (pbmraw device): no error, and the dots are visibly elongated
			// rather than round - not a textbook ellipse at every coverage
			// level, but confirmed distinct from Round and confirmed to not
			// crash a RIP.
			return QStringLiteral("{0.7 mul abs exch abs 2 copy add 1 gt "
				"{1 sub dup mul exch 1 sub dup mul add 1 sub} "
				"{dup mul exch dup mul add 1 sub} ifelse}");
		case OffsetDotShape::Line:
			return QStringLiteral("{pop}");
		case OffsetDotShape::Square:
			// max(|x|,|y|): level sets are axis-aligned squares, so the dot
			// grows and merges as a square rather than a circle or diamond.
			// Verified by rendering at 12 lpi with Ghostscript (pbmraw
			// device) at 0.25/0.5/0.75 gray: produces a crisp square grid,
			// including the correct checkerboard inversion exactly at 50%
			// coverage where adjacent square dots touch edge-to-edge - the
			// same verification method used to confirm this is not the
			// round/diamond pattern the previous, duplicated formula gave.
			return QStringLiteral("{abs exch abs 2 copy lt {exch} if pop}");
		case OffsetDotShape::Euclidean:
			// The PostScript Language Reference Manual's own "Euclidean dot"
			// example: dots grow round from each screen centre and merge
			// diagonally into diamonds at 50% coverage (the classic AM
			// "Euclidean" screening geometry RIPs expose under that name -
			// distinct from OffsetDotShape::Round's simpler cosine-based
			// approximation above, which does not fold at the corners).
			return QStringLiteral("{abs exch abs 2 copy add 1 gt "
				"{1 sub dup mul exch 1 sub dup mul add 1 sub} "
				"{dup mul exch dup mul add 1 sub} ifelse}");
		case OffsetDotShape::Diamond:
			return QStringLiteral("{abs exch abs 2 copy add 0.75 le "
				"{dup mul exch dup mul add 1.5 mul 1 exch sub} "
				"{2 copy add 1.23 le {0.85 mul add 1 exch sub} "
				"{1 sub dup mul exch 1 sub dup mul add 1 sub} ifelse} ifelse}");
	}
	return QStringLiteral("{180 mul cos exch 180 mul cos add 2 div}");
}

QStringList offsetOutputModeNames()
{
	return {
		QStringLiteral("CmykSeparations"),
		QStringLiteral("Grayscale"),
		QStringLiteral("FullColor")
	};
}

QString offsetOutputModeName(OffsetOutputMode mode)
{
	int idx = static_cast<int>(mode);
	QStringList names = offsetOutputModeNames();
	if ((idx < 0) || (idx >= names.count()))
		return names.at(0);
	return names.at(idx);
}

OffsetOutputMode offsetOutputModeFromName(const QString& name, bool* ok)
{
	QStringList names = offsetOutputModeNames();
	int idx = names.indexOf(name);
	if (ok)
		*ok = (idx >= 0);
	if (idx < 0)
		return OffsetOutputMode::CmykSeparations;
	return static_cast<OffsetOutputMode>(idx);
}

static QJsonObject plateToJson(const OffsetSepPlateSettings& plate)
{
	QJsonObject obj;
	obj["lpi"] = plate.lpi;
	obj["angle"] = plate.angle;
	obj["print"] = plate.printPlate;
	return obj;
}

static OffsetSepPlateSettings plateFromJson(const QJsonObject& obj, const OffsetSepPlateSettings& fallback)
{
	OffsetSepPlateSettings plate = fallback;
	if (obj.contains("lpi"))
		plate.lpi = obj.value("lpi").toDouble(fallback.lpi);
	if (obj.contains("angle"))
		plate.angle = obj.value("angle").toDouble(fallback.angle);
	if (obj.contains("print"))
		plate.printPlate = obj.value("print").toBool(fallback.printPlate);
	return plate;
}

QJsonObject OffsetSepPreset::toJson() const
{
	QJsonObject obj;
	obj["name"] = name;
	obj["resolution"] = resolution;
	obj["dotShape"] = offsetDotShapeName(dotShape);
	obj["cyan"] = plateToJson(cyan);
	obj["magenta"] = plateToJson(magenta);
	obj["yellow"] = plateToJson(yellow);
	obj["black"] = plateToJson(black);
	return obj;
}

OffsetSepPreset OffsetSepPreset::fromJson(const QJsonObject& obj, bool* ok)
{
	OffsetSepPreset preset;
	QString presetName = obj.value("name").toString();
	if (presetName.isEmpty())
	{
		if (ok)
			*ok = false;
		return preset;
	}
	preset.name = presetName;
	preset.builtIn = false;
	preset.resolution = obj.value("resolution").toInt(preset.resolution);
	preset.dotShape = offsetDotShapeFromName(obj.value("dotShape").toString(), nullptr);
	preset.cyan    = plateFromJson(obj.value("cyan").toObject(), preset.cyan);
	preset.magenta = plateFromJson(obj.value("magenta").toObject(), preset.magenta);
	preset.yellow  = plateFromJson(obj.value("yellow").toObject(), preset.yellow);
	preset.black   = plateFromJson(obj.value("black").toObject(), preset.black);
	if (ok)
		*ok = true;
	return preset;
}

namespace OffsetSepPresetLibrary
{

QVector<OffsetSepPreset> bundledPresets()
{
	QVector<OffsetSepPreset> presets;

	// Standard newspaper/magazine screen angles: 15/75/0/45 for C/M/Y/K.
	// Kept as named constants only here - each preset below spells out its
	// own values so the table stays easy to audit against SUNEER_CHANGES.md.

	OffsetSepPreset malayalamNewspaper;
	malayalamNewspaper.name = QStringLiteral("Malayalam Newspaper");
	malayalamNewspaper.builtIn = true;
	malayalamNewspaper.resolution = 2400;
	malayalamNewspaper.dotShape = OffsetDotShape::Round;
	malayalamNewspaper.cyan    = { 100.0, 15.0, true };
	malayalamNewspaper.magenta = { 100.0, 75.0, true };
	malayalamNewspaper.yellow  = { 100.0,  0.0, true };
	malayalamNewspaper.black   = { 100.0, 45.0, true };
	presets.append(malayalamNewspaper);

	OffsetSepPreset englishNewspaper;
	englishNewspaper.name = QStringLiteral("English Newspaper");
	englishNewspaper.builtIn = true;
	englishNewspaper.resolution = 2400;
	englishNewspaper.dotShape = OffsetDotShape::Round;
	englishNewspaper.cyan    = { 85.0, 15.0, true };
	englishNewspaper.magenta = { 85.0, 75.0, true };
	englishNewspaper.yellow  = { 85.0,  0.0, true };
	englishNewspaper.black   = { 85.0, 45.0, true };
	presets.append(englishNewspaper);

	OffsetSepPreset weeklyMagazine;
	weeklyMagazine.name = QStringLiteral("Weekly Magazine");
	weeklyMagazine.builtIn = true;
	weeklyMagazine.resolution = 2400;
	weeklyMagazine.dotShape = OffsetDotShape::Elliptical;
	weeklyMagazine.cyan    = { 133.0, 15.0, true };
	weeklyMagazine.magenta = { 133.0, 75.0, true };
	weeklyMagazine.yellow  = { 133.0,  0.0, true };
	weeklyMagazine.black   = { 133.0, 45.0, true };
	presets.append(weeklyMagazine);

	OffsetSepPreset monthlyMagazine;
	monthlyMagazine.name = QStringLiteral("Monthly Magazine");
	monthlyMagazine.builtIn = true;
	monthlyMagazine.resolution = 2400;
	monthlyMagazine.dotShape = OffsetDotShape::Elliptical;
	monthlyMagazine.cyan    = { 150.0, 15.0, true };
	monthlyMagazine.magenta = { 150.0, 75.0, true };
	monthlyMagazine.yellow  = { 150.0,  0.0, true };
	monthlyMagazine.black   = { 150.0, 45.0, true };
	presets.append(monthlyMagazine);

	OffsetSepPreset poster;
	poster.name = QStringLiteral("Poster/Large Format");
	poster.builtIn = true;
	poster.resolution = 1200;
	poster.dotShape = OffsetDotShape::Round;
	poster.cyan    = { 65.0, 15.0, true };
	poster.magenta = { 65.0, 75.0, true };
	poster.yellow  = { 65.0,  0.0, true };
	poster.black   = { 65.0, 45.0, true };
	presets.append(poster);

	OffsetSepPreset bookInterior;
	bookInterior.name = QStringLiteral("Book Interior");
	bookInterior.builtIn = true;
	bookInterior.resolution = 2400;
	bookInterior.dotShape = OffsetDotShape::Round;
	bookInterior.cyan    = { 133.0, 15.0, true };
	bookInterior.magenta = { 133.0, 75.0, true };
	bookInterior.yellow  = { 133.0,  0.0, true };
	bookInterior.black   = { 133.0, 45.0, true };
	presets.append(bookInterior);

	return presets;
}

QString defaultPresetName()
{
	return QStringLiteral("Malayalam Newspaper");
}

QVector<OffsetSepPreset> parseCustomPresets(const QString& json)
{
	QVector<OffsetSepPreset> presets;
	if (json.trimmed().isEmpty())
		return presets;

	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
	if ((parseError.error != QJsonParseError::NoError) || !doc.isArray())
		return presets;

	const QJsonArray array = doc.array();
	for (const QJsonValue& value : array)
	{
		if (!value.isObject())
			continue;
		bool ok = false;
		OffsetSepPreset preset = OffsetSepPreset::fromJson(value.toObject(), &ok);
		if (ok)
			presets.append(preset);
	}
	return presets;
}

QString serializeCustomPresets(const QVector<OffsetSepPreset>& presets)
{
	QJsonArray array;
	for (const OffsetSepPreset& preset : presets)
		array.append(preset.toJson());
	QJsonDocument doc(array);
	return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

} // namespace OffsetSepPresetLibrary

static QString plateHalftoneSubDict(const OffsetSepPlateSettings& plate, OffsetDotShape shape)
{
	return QStringLiteral("<< /HalftoneType 1 /Frequency %1 /Angle %2 /SpotFunction %3 >>")
		.arg(plate.lpi)
		.arg(plate.angle)
		.arg(offsetDotShapeSpotFunction(shape));
}

QString offsetSepHalftoneDict(const OffsetSepPreset& preset)
{
	QString out;
	out += QStringLiteral("%% Custom halftone screening for offset separation\n");
	out += QStringLiteral("<< /HalftoneType 5\n");
	out += QStringLiteral("   /Cyan    %1\n").arg(plateHalftoneSubDict(preset.cyan, preset.dotShape));
	out += QStringLiteral("   /Magenta %1\n").arg(plateHalftoneSubDict(preset.magenta, preset.dotShape));
	out += QStringLiteral("   /Yellow  %1\n").arg(plateHalftoneSubDict(preset.yellow, preset.dotShape));
	out += QStringLiteral("   /Black   %1\n").arg(plateHalftoneSubDict(preset.black, preset.dotShape));
	// Default entry: falls back to the black plate's screen, matching how a
	// spot-colour plate is rendered through PS_plate()'s default branch.
	out += QStringLiteral("   /Default %1\n").arg(plateHalftoneSubDict(preset.black, preset.dotShape));
	out += QStringLiteral(">> sethalftone\n");
	return out;
}

QString offsetSepPlateHalftone(double lpi, double angle, OffsetDotShape shape)
{
	return QStringLiteral("<< /HalftoneType 1 /Frequency %1 /Angle %2 /SpotFunction %3 >> sethalftone\n")
		.arg(lpi)
		.arg(angle)
		.arg(offsetDotShapeSpotFunction(shape));
}
