/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OFFSET_SEPARATION_PRESETS_H
#define OFFSET_SEPARATION_PRESETS_H

#include "scribusapi.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

//! Halftone dot shape for offset separation output. Values are stable - they
//! are stored as the dot-shape combo's index in PrintOptions and in saved
//! custom presets.
enum class OffsetDotShape
{
	Round = 0,
	Elliptical = 1,
	Line = 2,
	Square = 3,
	Euclidean = 4,
	Diamond = 5
};

//! Display name for the dot-shape combo, in a fixed Round..Diamond order.
SCRIBUS_API QStringList offsetDotShapeNames();
SCRIBUS_API QString offsetDotShapeName(OffsetDotShape shape);
SCRIBUS_API OffsetDotShape offsetDotShapeFromName(const QString& name, bool* ok = nullptr);

//! What the Offset Separations tab's master switch actually produces. Values
//! are stable - held as the display name string (not this enum) in
//! PrintOptions and in saved custom presets, for the same reason
//! OffsetDotShape's name is: it keeps PrintOptions free of this include.
enum class OffsetOutputMode
{
	//! One grayscale plate page per enabled process colour, each screened
	//! per its own LPI/angle/dot shape - the tab's original behaviour.
	CmykSeparations = 0,
	//! A single grayscale sheet per tile, screened with one LPI/angle (reuses
	//! the Black plate's settings - every bundled preset already screens
	//! Black at 45 degrees, the conventional single-colour angle).
	Grayscale = 1,
	//! A single full-colour composite sheet per tile: no halftone override,
	//! no plate iteration - tiling and marks still apply.
	FullColor = 2
};

SCRIBUS_API QStringList offsetOutputModeNames();
SCRIBUS_API QString offsetOutputModeName(OffsetOutputMode mode);
SCRIBUS_API OffsetOutputMode offsetOutputModeFromName(const QString& name, bool* ok = nullptr);

/*! \brief PostScript SpotFunction body (braces included) for a HalftoneType 1
    dictionary, one per dot shape.

    Every formula here has been rendered with Ghostscript (a HalftoneType 1
    dict, sethalftone, a mid-gray rectfill, output to the pbmraw device so
    the real dot raster - not an antialiased contone approximation - is what
    gets inspected) at several gray levels, confirming each: (a) does not
    error out of sethalftone's [-1,1] range check, and (b) produces a dot
    pattern visibly distinct from the others. This replaced an earlier,
    unverified version of this function where "Square" was a duplicate of
    the Euclidean formula (no real square-dot formula had been sourced) and
    "Elliptical" produced out-of-range values that made sethalftone fail
    outright - both caught this way, not by inspection. See the per-case
    comments below for what was verified for each shape, and
    SUNEER_CHANGES.md for the fix. Round and Line are used as specified for
    this feature. Diamond is the classic PostScript Language Reference
    Manual diamond-dot example. */
SCRIBUS_API QString offsetDotShapeSpotFunction(OffsetDotShape shape);

//! One process-colour plate's screening settings.
struct SCRIBUS_API OffsetSepPlateSettings
{
	double lpi { 100.0 };
	double angle { 45.0 };
	bool printPlate { true };

	bool operator==(const OffsetSepPlateSettings& other) const
	{
		return (lpi == other.lpi) && (angle == other.angle) && (printPlate == other.printPlate);
	}
	bool operator!=(const OffsetSepPlateSettings& other) const { return !(*this == other); }
};

//! A full offset-separation configuration: one dot shape/resolution and four
//! per-plate LPI/angle/print settings. Used both for the six bundled
//! workflow presets and for user-saved custom presets.
struct SCRIBUS_API OffsetSepPreset
{
	QString name;
	bool builtIn { false };
	int resolution { 2400 };
	OffsetDotShape dotShape { OffsetDotShape::Round };
	OffsetSepPlateSettings cyan    { 100.0, 15.0, true };
	OffsetSepPlateSettings magenta { 100.0, 75.0, true };
	OffsetSepPlateSettings yellow  { 100.0,  0.0, true };
	OffsetSepPlateSettings black   { 100.0, 45.0, true };

	QJsonObject toJson() const;
	//! Parses a preset written by toJson(). On malformed input returns a
	//! default-constructed preset and sets *ok (if given) to false.
	static OffsetSepPreset fromJson(const QJsonObject& obj, bool* ok = nullptr);

	bool operator==(const OffsetSepPreset& other) const
	{
		return (name == other.name) && (resolution == other.resolution) && (dotShape == other.dotShape)
			&& (cyan == other.cyan) && (magenta == other.magenta) && (yellow == other.yellow) && (black == other.black);
	}
	bool operator!=(const OffsetSepPreset& other) const { return !(*this == other); }
};

namespace OffsetSepPresetLibrary
{
	//! The six bundled workflow presets, in display order. All have
	//! builtIn == true and cannot be deleted from the print dialog.
	SCRIBUS_API QVector<OffsetSepPreset> bundledPresets();

	//! Name of the preset selected by default on a fresh profile.
	SCRIBUS_API QString defaultPresetName();

	//! Parses the "OffsetSepCustomPresets" PrefsContext value (a JSON array of
	//! preset objects) into a preset list. An empty or malformed string yields
	//! an empty list rather than an error - a fresh profile has no custom
	//! presets yet.
	SCRIBUS_API QVector<OffsetSepPreset> parseCustomPresets(const QString& json);

	//! Inverse of parseCustomPresets(), for writing back to PrefsContext.
	SCRIBUS_API QString serializeCustomPresets(const QVector<OffsetSepPreset>& presets);
}

/*! \brief The combined HalftoneType 5 screening dictionary for all four
    process colorants plus /Default, as described for this feature:

    \code
    << /HalftoneType 5
       /Cyan    << /HalftoneType 1 /Frequency ... /Angle ... /SpotFunction {...} >>
       ...
    >> sethalftone
    \endcode

    Written once per document in PS_begin_doc()'s %%BeginSetup section so a
    RIP that resolves screens by colorant name (Separation/DeviceN colour
    spaces) picks up the right screen for each plate. It is not what actually
    screens the pre-separated, single-channel plate pages this pipeline
    already emits - see offsetSepPlateHalftone() and the note in
    PSLib::PS_plate(). */
SCRIBUS_API QString offsetSepHalftoneDict(const OffsetSepPreset& preset);

/*! \brief A single-colorant HalftoneType 1 dictionary for the plate PS_plate()
    is about to render, e.g.:

    \code
    << /HalftoneType 1 /Frequency 100 /Angle 15 /SpotFunction {...} >> sethalftone
    \endcode

    This is what actually screens each already-separated grayscale plate page:
    once PS_plate() has remapped setcmykcolor/setrgbcolor to grayscale for a
    single plate, the page's colour space is DeviceGray and a Type 5 dict
    keyed by colorant name is not guaranteed to be consulted for it. */
SCRIBUS_API QString offsetSepPlateHalftone(double lpi, double angle, OffsetDotShape shape);

#endif // OFFSET_SEPARATION_PRESETS_H
