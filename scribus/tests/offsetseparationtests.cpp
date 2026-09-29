/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include "offsetseparationtests.h"
#include "offset_separation_presets.h"

namespace
{
	const OffsetSepPreset* findPreset(const QVector<OffsetSepPreset>& presets, const QString& name)
	{
		for (const OffsetSepPreset& preset : presets)
		{
			if (preset.name == name)
				return &preset;
		}
		return nullptr;
	}

	int braceBalance(const QString& psFragment)
	{
		int balance = 0;
		for (QChar c : psFragment)
		{
			if (c == '{')
				++balance;
			else if (c == '}')
				--balance;
		}
		return balance;
	}
}

void OffsetSeparationTests::testDotShapeNameRoundTrip()
{
	for (OffsetDotShape shape : { OffsetDotShape::Round, OffsetDotShape::Elliptical, OffsetDotShape::Line,
	                               OffsetDotShape::Square, OffsetDotShape::Euclidean, OffsetDotShape::Diamond })
	{
		QString name = offsetDotShapeName(shape);
		QVERIFY(!name.isEmpty());
		bool ok = false;
		OffsetDotShape parsed = offsetDotShapeFromName(name, &ok);
		QVERIFY(ok);
		QCOMPARE(parsed, shape);
	}
}

void OffsetSeparationTests::testDotShapeFromUnknownNameFallsBackToRound()
{
	bool ok = true;
	OffsetDotShape shape = offsetDotShapeFromName("NotAShape", &ok);
	QVERIFY(!ok);
	QCOMPARE(shape, OffsetDotShape::Round);
}

void OffsetSeparationTests::testOutputModeNameRoundTrip()
{
	for (OffsetOutputMode mode : { OffsetOutputMode::CmykSeparations, OffsetOutputMode::Grayscale, OffsetOutputMode::FullColor })
	{
		QString name = offsetOutputModeName(mode);
		QVERIFY(!name.isEmpty());
		bool ok = false;
		OffsetOutputMode parsed = offsetOutputModeFromName(name, &ok);
		QVERIFY(ok);
		QCOMPARE(parsed, mode);
	}
}

void OffsetSeparationTests::testOutputModeFromUnknownNameFallsBackToCmykSeparations()
{
	bool ok = true;
	OffsetOutputMode mode = offsetOutputModeFromName("NotAMode", &ok);
	QVERIFY(!ok);
	QCOMPARE(mode, OffsetOutputMode::CmykSeparations);
}

void OffsetSeparationTests::testBundledPresetCount()
{
	QCOMPARE(OffsetSepPresetLibrary::bundledPresets().count(), 6);
}

void OffsetSeparationTests::testMalayalamNewspaperDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "Malayalam Newspaper");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 2400);
	QCOMPARE(preset->dotShape, OffsetDotShape::Round);
	QCOMPARE(preset->cyan.lpi, 100.0);
	QCOMPARE(preset->cyan.angle, 15.0);
	QCOMPARE(preset->magenta.lpi, 100.0);
	QCOMPARE(preset->magenta.angle, 75.0);
	QCOMPARE(preset->yellow.lpi, 100.0);
	QCOMPARE(preset->yellow.angle, 0.0);
	QCOMPARE(preset->black.lpi, 100.0);
	QCOMPARE(preset->black.angle, 45.0);
	QVERIFY(preset->cyan.printPlate);
	QVERIFY(preset->magenta.printPlate);
	QVERIFY(preset->yellow.printPlate);
	QVERIFY(preset->black.printPlate);
}

void OffsetSeparationTests::testEnglishNewspaperDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "English Newspaper");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 2400);
	QCOMPARE(preset->dotShape, OffsetDotShape::Round);
	QCOMPARE(preset->cyan.lpi, 85.0);
	QCOMPARE(preset->black.angle, 45.0);
}

void OffsetSeparationTests::testWeeklyMagazineDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "Weekly Magazine");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 2400);
	QCOMPARE(preset->dotShape, OffsetDotShape::Elliptical);
	QCOMPARE(preset->magenta.lpi, 133.0);
}

void OffsetSeparationTests::testMonthlyMagazineDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "Monthly Magazine");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 2400);
	QCOMPARE(preset->dotShape, OffsetDotShape::Elliptical);
	QCOMPARE(preset->yellow.lpi, 150.0);
}

void OffsetSeparationTests::testPosterDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "Poster/Large Format");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 1200);
	QCOMPARE(preset->dotShape, OffsetDotShape::Round);
	QCOMPARE(preset->black.lpi, 65.0);
}

void OffsetSeparationTests::testBookInteriorDefaults()
{
	const auto presets = OffsetSepPresetLibrary::bundledPresets();
	const OffsetSepPreset* preset = findPreset(presets, "Book Interior");
	QVERIFY(preset != nullptr);
	QCOMPARE(preset->resolution, 2400);
	QCOMPARE(preset->dotShape, OffsetDotShape::Round);
	QCOMPARE(preset->cyan.lpi, 133.0);
}

void OffsetSeparationTests::testAllBundledPresetsAreMarkedBuiltIn()
{
	for (const OffsetSepPreset& preset : OffsetSepPresetLibrary::bundledPresets())
		QVERIFY(preset.builtIn);
}

void OffsetSeparationTests::testDefaultPresetNameMatchesABundledPreset()
{
	QString defaultName = OffsetSepPresetLibrary::defaultPresetName();
	QVERIFY(findPreset(OffsetSepPresetLibrary::bundledPresets(), defaultName) != nullptr);
}

void OffsetSeparationTests::testCustomPresetRoundTrip()
{
	OffsetSepPreset preset;
	preset.name = "My Custom Preset";
	preset.builtIn = false;
	preset.resolution = 1800;
	preset.dotShape = OffsetDotShape::Diamond;
	preset.cyan    = { 120.0, 10.0, true };
	preset.magenta = { 120.0, 70.0, false };
	preset.yellow  = { 120.0,  5.0, true };
	preset.black   = { 120.0, 40.0, true };

	QVector<OffsetSepPreset> presets { preset };
	QString json = OffsetSepPresetLibrary::serializeCustomPresets(presets);
	QVERIFY(!json.isEmpty());

	QVector<OffsetSepPreset> parsed = OffsetSepPresetLibrary::parseCustomPresets(json);
	QCOMPARE(parsed.count(), 1);
	QCOMPARE(parsed.at(0).name, preset.name);
	QCOMPARE(parsed.at(0).resolution, preset.resolution);
	QCOMPARE(parsed.at(0).dotShape, preset.dotShape);
	QCOMPARE(parsed.at(0).cyan, preset.cyan);
	QCOMPARE(parsed.at(0).magenta, preset.magenta);
	QCOMPARE(parsed.at(0).yellow, preset.yellow);
	QCOMPARE(parsed.at(0).black, preset.black);
}

void OffsetSeparationTests::testCustomPresetRoundTripMultiple()
{
	OffsetSepPreset a;
	a.name = "Preset A";
	a.resolution = 2000;
	OffsetSepPreset b;
	b.name = "Preset B";
	b.resolution = 2200;

	QVector<OffsetSepPreset> presets { a, b };
	QString json = OffsetSepPresetLibrary::serializeCustomPresets(presets);
	QVector<OffsetSepPreset> parsed = OffsetSepPresetLibrary::parseCustomPresets(json);

	QCOMPARE(parsed.count(), 2);
	QVERIFY(findPreset(parsed, "Preset A") != nullptr);
	QVERIFY(findPreset(parsed, "Preset B") != nullptr);
}

void OffsetSeparationTests::testEmptyJsonYieldsNoPresets()
{
	QCOMPARE(OffsetSepPresetLibrary::parseCustomPresets(QString()).count(), 0);
	QCOMPARE(OffsetSepPresetLibrary::parseCustomPresets(QString("   ")).count(), 0);
}

void OffsetSeparationTests::testMalformedJsonYieldsNoPresets()
{
	QCOMPARE(OffsetSepPresetLibrary::parseCustomPresets(QString("{not valid json")).count(), 0);
	QCOMPARE(OffsetSepPresetLibrary::parseCustomPresets(QString("{\"not\":\"an array\"}")).count(), 0);
}

void OffsetSeparationTests::testPresetMissingNameIsRejected()
{
	bool ok = true;
	QJsonObject obj;
	obj["resolution"] = 2400;
	OffsetSepPreset preset = OffsetSepPreset::fromJson(obj, &ok);
	QVERIFY(!ok);
	Q_UNUSED(preset)
}

void OffsetSeparationTests::testSpotFunctionRoundHasBalancedBraces()
{
	QString fn = offsetDotShapeSpotFunction(OffsetDotShape::Round);
	QCOMPARE(braceBalance(fn), 0);
	QVERIFY(fn.startsWith('{'));
	QVERIFY(fn.endsWith('}'));
}

void OffsetSeparationTests::testSpotFunctionLineIsPop()
{
	QCOMPARE(offsetDotShapeSpotFunction(OffsetDotShape::Line), QString("{pop}"));
}

void OffsetSeparationTests::testAllSixDotShapesHaveDistinctSpotFunctions()
{
	// Regression guard for the bug this replaced: Square used to be a
	// verbatim duplicate of Euclidean's formula. Every shape's PostScript
	// text must now be unique.
	const QVector<OffsetDotShape> shapes {
		OffsetDotShape::Round, OffsetDotShape::Elliptical, OffsetDotShape::Line,
		OffsetDotShape::Square, OffsetDotShape::Euclidean, OffsetDotShape::Diamond
	};
	for (int i = 0; i < shapes.count(); ++i)
	{
		for (int j = i + 1; j < shapes.count(); ++j)
			QVERIFY(offsetDotShapeSpotFunction(shapes.at(i)) != offsetDotShapeSpotFunction(shapes.at(j)));
	}
}

void OffsetSeparationTests::testAllSpotFunctionsHaveBalancedBraces()
{
	for (OffsetDotShape shape : { OffsetDotShape::Round, OffsetDotShape::Elliptical, OffsetDotShape::Line,
	                               OffsetDotShape::Square, OffsetDotShape::Euclidean, OffsetDotShape::Diamond })
	{
		QString fn = offsetDotShapeSpotFunction(shape);
		QCOMPARE(braceBalance(fn), 0);
	}
}

void OffsetSeparationTests::testSquareSpotFunctionIsChebyshevNorm()
{
	// Pinned to the exact text verified by rendering with Ghostscript (see
	// the header comment on offsetDotShapeSpotFunction()): max(|x|,|y|), so
	// its level sets are axis-aligned squares. A regression back to the old
	// (duplicate-of-Euclidean) formula, or any other change, fails this.
	QCOMPARE(offsetDotShapeSpotFunction(OffsetDotShape::Square),
		QString("{abs exch abs 2 copy lt {exch} if pop}"));
}

void OffsetSeparationTests::testEllipticalSpotFunctionStaysInRange()
{
	// Pinned to the exact text verified by rendering with Ghostscript.
	QCOMPARE(offsetDotShapeSpotFunction(OffsetDotShape::Elliptical),
		QString("{0.7 mul abs exch abs 2 copy add 1 gt "
			"{1 sub dup mul exch 1 sub dup mul add 1 sub} "
			"{dup mul exch dup mul add 1 sub} ifelse}"));

	// The bug this replaced: the original formula returned values outside
	// [-1,1] at the domain corners, which made a real PostScript interpreter's
	// sethalftone fail with a rangecheck error (confirmed by rendering it).
	// This is a direct C++ reimplementation of the PostScript above - not the
	// old formula - so it stands as a standing numeric proof that a spot
	// function built this way (warp one axis by a factor in [0,1], then feed
	// both into this piecewise body) cannot leave [-1,1], for any x,y in the
	// domain a RIP will actually pass in.
	auto euclideanDotBody = [](double ax, double ay) -> double
	{
		if (ax + ay > 1.0)
			return (ax - 1.0) * (ax - 1.0) + (ay - 1.0) * (ay - 1.0) - 1.0;
		return ax * ax + ay * ay - 1.0;
	};

	const int steps = 41; // -1.0 to 1.0 in 0.05 increments, corners included
	for (int i = 0; i < steps; ++i)
	{
		double x = -1.0 + (2.0 * i) / (steps - 1);
		for (int j = 0; j < steps; ++j)
		{
			double y = -1.0 + (2.0 * j) / (steps - 1);
			double value = euclideanDotBody(qAbs(x), 0.7 * qAbs(y));
			QVERIFY(value >= -1.0);
			QVERIFY(value <= 1.0);
		}
	}
}

void OffsetSeparationTests::testPlateHalftoneContainsFrequencyAndAngle()
{
	QString ps = offsetSepPlateHalftone(100.0, 15.0, OffsetDotShape::Round);
	QVERIFY(ps.contains("/HalftoneType 1"));
	QVERIFY(ps.contains("/Frequency 100"));
	QVERIFY(ps.contains("/Angle 15"));
	QVERIFY(ps.contains("sethalftone"));
}

void OffsetSeparationTests::testPlateHalftoneUsesRequestedDotShape()
{
	QString ps = offsetSepPlateHalftone(100.0, 45.0, OffsetDotShape::Line);
	QVERIFY(ps.contains(offsetDotShapeSpotFunction(OffsetDotShape::Line)));
}

void OffsetSeparationTests::testCombinedHalftoneDictHasFourColorantsAndDefault()
{
	OffsetSepPreset preset = *findPreset(OffsetSepPresetLibrary::bundledPresets(), "Malayalam Newspaper");
	QString ps = offsetSepHalftoneDict(preset);

	QVERIFY(ps.contains("/HalftoneType 5"));
	QVERIFY(ps.contains("/Cyan"));
	QVERIFY(ps.contains("/Magenta"));
	QVERIFY(ps.contains("/Yellow"));
	QVERIFY(ps.contains("/Black"));
	QVERIFY(ps.contains("/Default"));
	QVERIFY(ps.contains("sethalftone"));
	QCOMPARE(braceBalance(ps), 0);
}

void OffsetSeparationTests::testCombinedHalftoneDictUsesPerPlateAngles()
{
	OffsetSepPreset preset = *findPreset(OffsetSepPresetLibrary::bundledPresets(), "Malayalam Newspaper");
	QString ps = offsetSepHalftoneDict(preset);

	QVERIFY(ps.contains("/Angle 15")); // Cyan
	QVERIFY(ps.contains("/Angle 75")); // Magenta
	QVERIFY(ps.contains("/Angle 0"));  // Yellow
	QVERIFY(ps.contains("/Angle 45")); // Black
}

QTEST_APPLESS_MAIN(OffsetSeparationTests)
