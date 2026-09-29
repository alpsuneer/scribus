/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OFFSETSEPARATIONTESTS_H
#define OFFSETSEPARATIONTESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for the Offset Separations print-dialog feature's pure data and
 * PostScript-text-generation logic (offset_separation_presets.h/.cpp).
 *
 * Out of scope here, and NOT exercised by this suite: the PrintDialog "Offset
 * Separations" tab itself (a QDialog, needs a QApplication and widget
 * plumbing) and pslib.cpp's actual PostScript generation (needs a
 * ScribusDoc). Both were reviewed by reading, not run - see the delivery
 * report for what remains unverified.
 */
class OffsetSeparationTests : public QObject
{
	Q_OBJECT

private slots:
	// Dot shape name <-> enum mapping
	void testDotShapeNameRoundTrip();
	void testDotShapeFromUnknownNameFallsBackToRound();

	// Output mode name <-> enum mapping
	void testOutputModeNameRoundTrip();
	void testOutputModeFromUnknownNameFallsBackToCmykSeparations();

	// Bundled presets
	void testBundledPresetCount();
	void testMalayalamNewspaperDefaults();
	void testEnglishNewspaperDefaults();
	void testWeeklyMagazineDefaults();
	void testMonthlyMagazineDefaults();
	void testPosterDefaults();
	void testBookInteriorDefaults();
	void testAllBundledPresetsAreMarkedBuiltIn();
	void testDefaultPresetNameMatchesABundledPreset();

	// Custom preset JSON round-trip
	void testCustomPresetRoundTrip();
	void testCustomPresetRoundTripMultiple();
	void testEmptyJsonYieldsNoPresets();
	void testMalformedJsonYieldsNoPresets();
	void testPresetMissingNameIsRejected();

	// PostScript spot-function text
	void testSpotFunctionRoundHasBalancedBraces();
	void testSpotFunctionLineIsPop();
	void testAllSixDotShapesHaveDistinctSpotFunctions();
	void testAllSpotFunctionsHaveBalancedBraces();
	void testSquareSpotFunctionIsChebyshevNorm();
	void testEllipticalSpotFunctionStaysInRange();

	// PostScript halftone dictionary generation
	void testPlateHalftoneContainsFrequencyAndAngle();
	void testPlateHalftoneUsesRequestedDotShape();
	void testCombinedHalftoneDictHasFourColorantsAndDefault();
	void testCombinedHalftoneDictUsesPerPlateAngles();
};

#endif
