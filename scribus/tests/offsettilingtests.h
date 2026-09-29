/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OFFSETTILINGTESTS_H
#define OFFSETTILINGTESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for the Offset Separations tab's auto-tiling math and PostScript
 * mark/label text generation (offset_tiling.h/.cpp).
 *
 * Out of scope here, and NOT exercised by this suite: the Tiling group in the
 * PrintDialog "Offset Separations" tab (a QDialog, needs a QApplication),
 * live printer paper-size detection (needs a real or CUPS-registered
 * printer), and pslib.cpp's actual per-tile PostScript output (needs a
 * ScribusDoc and, ultimately, a real print/RIP to confirm registration).
 * All reviewed by reading, not run - see the delivery report for what
 * remains unverified.
 */
class OffsetTilingTests : public QObject
{
	Q_OBJECT

private slots:
	// Print order name <-> enum mapping
	void testPrintOrderNameRoundTrip();
	void testPrintOrderFromUnknownNameFallsBackToPlateFirst();

	// Tile grid calculation - the deliverable's own worked example
	void testBroadsheetOnA3ProducesTwoByTwoGrid();
	void testDocumentFittingOnePaperNeedsNoTiling();
	void testExactFitNeedsNoTiling();
	void testTinyOverflowAddsOneTile();
	void testHugeDocumentProducesManyTiles();
	void testZeroOverlapStillCoversFullDocument();
	void testOverlapLargerThanUsableAreaIsClamped();

	// Grid geometry invariants
	void testTilesCoverFullDocumentWidthAndHeight();
	void testNeighbouringTilesOverlapByRequestedAmount();
	void testNoTileExceedsUsablePaperArea();

	// Tile labelling
	void testSingleTileLabelHasNoPositionSuffix();
	void testCornerTileLabelsInTwoByTwoGrid();
	void testSingleColumnGridOmitsHorizontalPosition();
	void testSingleRowGridOmitsVerticalPosition();

	// PostScript mark/label generation - offsetTileRegistrationMarks(),
	// offsetTileCutMarks(), offsetTileLabelPS()
	void testRegistrationMarksAreBalancedGraphicsState();
	void testCutMarksAreBalancedGraphicsState();
	void testCutMarksDistinctFromRegistrationMarks();
	void testTileLabelIncludesPlateNameWhenGiven();
	void testTileLabelOmitsPlateNameWhenComposite();
	void testTileLabelEscapesParenthesesInLabel();

	// Orientation name <-> enum mapping
	void testOrientationNameRoundTrip();
	void testOrientationFromUnknownNameFallsBackToAuto();

	// Orientation auto-suggest - the deliverable's own worked example
	void testBroadsheetOnA3RecommendsLandscape();
	void testDocumentThatFitsEitherWayPrefersPortraitOnTie();
	void testOrientedPaperSizeSwapsForLandscape();
	void testOrientedPaperSizeUnchangedForPortrait();
	void testOrientedPaperSizeResolvesAutoToTheSuggestion();

	// Total sheets formula - the deliverable's own worked examples
	void testCmykPortraitSixteenSheets();
	void testCmykLandscapeEightSheets();
	void testGrayscaleLandscapeTwoSheets();
	void testFullColorUsesOnePlateRegardlessOfEnabledPlateCount();
	void testCmykWithDisabledPlatesReducesSheetCount();
};

#endif
