/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include "offsettilingtests.h"
#include "offset_tiling.h"

void OffsetTilingTests::testPrintOrderNameRoundTrip()
{
	for (OffsetTilePrintOrder order : { OffsetTilePrintOrder::PlateFirst, OffsetTilePrintOrder::TileFirst, OffsetTilePrintOrder::SheetOptimized })
	{
		QString name = offsetTilePrintOrderName(order);
		QVERIFY(!name.isEmpty());
		bool ok = false;
		OffsetTilePrintOrder parsed = offsetTilePrintOrderFromName(name, &ok);
		QVERIFY(ok);
		QCOMPARE(parsed, order);
	}
}

void OffsetTilingTests::testPrintOrderFromUnknownNameFallsBackToPlateFirst()
{
	bool ok = true;
	OffsetTilePrintOrder order = offsetTilePrintOrderFromName("NotAnOrder", &ok);
	QVERIFY(!ok);
	QCOMPARE(order, OffsetTilePrintOrder::PlateFirst);
}

void OffsetTilingTests::testBroadsheetOnA3ProducesTwoByTwoGrid()
{
	// The task's own worked example: 350x538mm broadsheet, A3 (297x420mm)
	// paper, 5mm overlap, no printer margin -> 2x2 = 4 tiles per plate.
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.cols, 2);
	QCOMPARE(grid.rows, 2);
	QCOMPARE(grid.tileCount(), 4);
	QVERIFY(!grid.isSingleSheet());
}

void OffsetTilingTests::testDocumentFittingOnePaperNeedsNoTiling()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(200.0, 280.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.cols, 1);
	QCOMPARE(grid.rows, 1);
	QVERIFY(grid.isSingleSheet());
	QCOMPARE(grid.tiles.count(), 1);
	QCOMPARE(grid.tiles.at(0).width, 200.0);
	QCOMPARE(grid.tiles.at(0).height, 280.0);
	QCOMPARE(grid.tiles.at(0).left, 0.0);
	QCOMPARE(grid.tiles.at(0).bottom, 0.0);
}

void OffsetTilingTests::testExactFitNeedsNoTiling()
{
	// Document exactly the size of the usable paper area: must not tip over
	// into a second tile because of overlap subtraction.
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(297.0, 420.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QVERIFY(grid.isSingleSheet());
}

void OffsetTilingTests::testTinyOverflowAddsOneTile()
{
	// One point over the usable width must still require a second column,
	// however small the overflow.
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(297.1, 420.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.cols, 2);
	QCOMPARE(grid.rows, 1);
}

void OffsetTilingTests::testHugeDocumentProducesManyTiles()
{
	// A poster-sized document on A4 paper: should need a double-digit grid,
	// and must terminate (regression guard against an infinite/near-zero step).
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(3000.0, 2000.0), QSizeF(210.0, 297.0), 5.0, 10.0);
	QVERIFY(grid.cols >= 10);
	QVERIFY(grid.rows >= 5);
	QCOMPARE(grid.tileCount(), grid.cols * grid.rows);
}

void OffsetTilingTests::testZeroOverlapStillCoversFullDocument()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(500.0, 500.0), QSizeF(297.0, 420.0), 0.0, 0.0);
	QVERIFY(grid.cols >= 2);
	// With zero overlap the last column's right edge must reach exactly the
	// document's right edge.
	double maxRight = 0.0;
	for (const OffsetTileRect& tile : grid.tiles)
		maxRight = qMax(maxRight, tile.left + tile.width);
	QCOMPARE(maxRight, 500.0);
}

void OffsetTilingTests::testOverlapLargerThanUsableAreaIsClamped()
{
	// An overlap requested larger than the whole sheet must not hang or
	// produce a degenerate (zero-size/negative-step) grid.
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(1000.0, 1000.0), QSizeF(297.0, 420.0), 0.0, 10000.0);
	QVERIFY(grid.cols >= 1);
	QVERIFY(grid.rows >= 1);
	for (const OffsetTileRect& tile : grid.tiles)
	{
		QVERIFY(tile.width > 0.0);
		QVERIFY(tile.height > 0.0);
	}
}

void OffsetTilingTests::testTilesCoverFullDocumentWidthAndHeight()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);

	double maxRight = 0.0, maxTop = 0.0;
	double minLeft = 1e9, minBottom = 1e9;
	for (const OffsetTileRect& tile : grid.tiles)
	{
		minLeft = qMin(minLeft, tile.left);
		minBottom = qMin(minBottom, tile.bottom);
		maxRight = qMax(maxRight, tile.left + tile.width);
		maxTop = qMax(maxTop, tile.bottom + tile.height);
	}
	QCOMPARE(minLeft, 0.0);
	QCOMPARE(minBottom, 0.0);
	QCOMPARE(maxRight, 350.0);
	QCOMPARE(maxTop, 538.0);
}

void OffsetTilingTests::testNeighbouringTilesOverlapByRequestedAmount()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.cols, 2);

	const OffsetTileRect* left = nullptr;
	const OffsetTileRect* right = nullptr;
	for (const OffsetTileRect& tile : grid.tiles)
	{
		if ((tile.row == 0) && (tile.col == 0))
			left = &tile;
		if ((tile.row == 0) && (tile.col == 1))
			right = &tile;
	}
	QVERIFY(left != nullptr);
	QVERIFY(right != nullptr);
	double overlapAmount = (left->left + left->width) - right->left;
	QCOMPARE(overlapAmount, 5.0);
}

void OffsetTilingTests::testNoTileExceedsUsablePaperArea()
{
	double margin = 5.0;
	QSizeF paper(297.0, 420.0);
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), paper, margin, 5.0);
	double usableW = paper.width() - 2.0 * margin;
	double usableH = paper.height() - 2.0 * margin;
	for (const OffsetTileRect& tile : grid.tiles)
	{
		QVERIFY(tile.width <= usableW + 0.001);
		QVERIFY(tile.height <= usableH + 0.001);
	}
}

void OffsetTilingTests::testSingleTileLabelHasNoPositionSuffix()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(200.0, 280.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QString label = offsetTileLabel(grid.tiles.at(0), grid);
	QCOMPARE(label, QString("Tile 1 of 1"));
}

void OffsetTilingTests::testCornerTileLabelsInTwoByTwoGrid()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.tileCount(), 4);

	for (const OffsetTileRect& tile : grid.tiles)
	{
		QString label = offsetTileLabel(tile, grid);
		if ((tile.row == 0) && (tile.col == 0))
			QVERIFY(label.endsWith("Top-Left"));
		if ((tile.row == 0) && (tile.col == 1))
			QVERIFY(label.endsWith("Top-Right"));
		if ((tile.row == 1) && (tile.col == 0))
			QVERIFY(label.endsWith("Bottom-Left"));
		if ((tile.row == 1) && (tile.col == 1))
			QVERIFY(label.endsWith("Bottom-Right"));
	}
}

void OffsetTilingTests::testSingleColumnGridOmitsHorizontalPosition()
{
	// Narrow, tall document: forces rows > 1 but cols == 1.
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(200.0, 1000.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.cols, 1);
	QVERIFY(grid.rows > 1);
	for (const OffsetTileRect& tile : grid.tiles)
	{
		QString label = offsetTileLabel(tile, grid);
		QVERIFY(!label.contains("Left"));
		QVERIFY(!label.contains("Right"));
	}
}

void OffsetTilingTests::testSingleRowGridOmitsVerticalPosition()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(1000.0, 200.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(grid.rows, 1);
	QVERIFY(grid.cols > 1);
	for (const OffsetTileRect& tile : grid.tiles)
	{
		QString label = offsetTileLabel(tile, grid);
		QVERIFY(!label.contains("Top"));
		QVERIFY(!label.contains("Bottom"));
	}
}

void OffsetTilingTests::testRegistrationMarksAreBalancedGraphicsState()
{
	QString ps = offsetTileRegistrationMarks(297.0, 420.0);
	int gsCount = ps.count(QStringLiteral("gs\n"));
	int grCount = ps.count(QStringLiteral("gr\n"));
	QCOMPARE(gsCount, grCount);
	QCOMPARE(gsCount, 1);
}

void OffsetTilingTests::testCutMarksAreBalancedGraphicsState()
{
	QString ps = offsetTileCutMarks(297.0, 420.0);
	int gsCount = ps.count(QStringLiteral("gs\n"));
	int grCount = ps.count(QStringLiteral("gr\n"));
	QCOMPARE(gsCount, grCount);
	QCOMPARE(gsCount, 1);
}

void OffsetTilingTests::testCutMarksDistinctFromRegistrationMarks()
{
	// Regression guard: these must stay two independently-toggleable mark
	// types (separate checkboxes in the dialog), not the same PostScript
	// text under two names.
	QVERIFY(offsetTileCutMarks(297.0, 420.0) != offsetTileRegistrationMarks(297.0, 420.0));
}

void OffsetTilingTests::testTileLabelIncludesPlateNameWhenGiven()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QString ps = offsetTileLabelPS(grid.tiles.at(0), grid, QStringLiteral("Cyan"), 297.0, 420.0);
	QVERIFY(ps.contains("Cyan / Tile 1 of 4"));
}

void OffsetTilingTests::testTileLabelOmitsPlateNameWhenComposite()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(200.0, 280.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QString ps = offsetTileLabelPS(grid.tiles.at(0), grid, QString(), 297.0, 420.0);
	QVERIFY(ps.contains("(Tile 1 of 1)"));
	QVERIFY(!ps.contains(" / Tile"));
}

void OffsetTilingTests::testTileLabelEscapesParenthesesInLabel()
{
	OffsetTileGrid grid = offsetCalculateTileGrid(QSizeF(200.0, 280.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QString ps = offsetTileLabelPS(grid.tiles.at(0), grid, QStringLiteral("Spot (Pantone)"), 297.0, 420.0);
	QVERIFY(ps.contains("Spot \\(Pantone\\)"));
}

void OffsetTilingTests::testOrientationNameRoundTrip()
{
	for (OffsetTileOrientation orientation : { OffsetTileOrientation::Portrait, OffsetTileOrientation::Landscape, OffsetTileOrientation::Auto })
	{
		QString name = offsetTileOrientationName(orientation);
		QVERIFY(!name.isEmpty());
		bool ok = false;
		OffsetTileOrientation parsed = offsetTileOrientationFromName(name, &ok);
		QVERIFY(ok);
		QCOMPARE(parsed, orientation);
	}
}

void OffsetTilingTests::testOrientationFromUnknownNameFallsBackToAuto()
{
	bool ok = true;
	OffsetTileOrientation orientation = offsetTileOrientationFromName("Sideways", &ok);
	QVERIFY(!ok);
	QCOMPARE(orientation, OffsetTileOrientation::Auto);
}

void OffsetTilingTests::testBroadsheetOnA3RecommendsLandscape()
{
	// The task's own worked example: 350x538mm broadsheet on A3 (297x420mm,
	// portrait convention) needs 4 tiles portrait but only 2 landscape.
	OffsetTileOrientation suggestion = offsetSuggestOrientation(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(suggestion, OffsetTileOrientation::Landscape);

	OffsetTileGrid portraitGrid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	OffsetTileGrid landscapeGrid = offsetCalculateTileGrid(QSizeF(350.0, 538.0), QSizeF(420.0, 297.0), 0.0, 5.0);
	QCOMPARE(portraitGrid.tileCount(), 4);
	QCOMPARE(landscapeGrid.tileCount(), 2);
}

void OffsetTilingTests::testDocumentThatFitsEitherWayPrefersPortraitOnTie()
{
	// A square document on a square-ish paper: both orientations need the
	// same tile count, so the tie must resolve to Portrait (the dialog's
	// first radio option), not flip unpredictably.
	OffsetTileOrientation suggestion = offsetSuggestOrientation(QSizeF(100.0, 100.0), QSizeF(297.0, 420.0), 0.0, 5.0);
	QCOMPARE(suggestion, OffsetTileOrientation::Portrait);
}

void OffsetTilingTests::testOrientedPaperSizeSwapsForLandscape()
{
	QSizeF size = offsetOrientedPaperSize(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), OffsetTileOrientation::Landscape, 0.0, 5.0);
	QCOMPARE(size, QSizeF(420.0, 297.0));
}

void OffsetTilingTests::testOrientedPaperSizeUnchangedForPortrait()
{
	QSizeF size = offsetOrientedPaperSize(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), OffsetTileOrientation::Portrait, 0.0, 5.0);
	QCOMPARE(size, QSizeF(297.0, 420.0));
}

void OffsetTilingTests::testOrientedPaperSizeResolvesAutoToTheSuggestion()
{
	QSizeF size = offsetOrientedPaperSize(QSizeF(350.0, 538.0), QSizeF(297.0, 420.0), OffsetTileOrientation::Auto, 0.0, 5.0);
	QCOMPARE(size, QSizeF(420.0, 297.0)); // Auto resolves to Landscape here, same as testBroadsheetOnA3RecommendsLandscape
}

void OffsetTilingTests::testCmykPortraitSixteenSheets()
{
	// 1 page x 4 tiles (portrait) x 4 plates = 16 sheets.
	QCOMPARE(offsetTotalSheets(1, 4, OffsetOutputMode::CmykSeparations, 4), 16);
}

void OffsetTilingTests::testCmykLandscapeEightSheets()
{
	// 1 page x 2 tiles (landscape) x 4 plates = 8 sheets.
	QCOMPARE(offsetTotalSheets(1, 2, OffsetOutputMode::CmykSeparations, 4), 8);
}

void OffsetTilingTests::testGrayscaleLandscapeTwoSheets()
{
	// 1 page x 2 tiles (landscape) x 1 plate (grayscale ignores plate count) = 2 sheets.
	QCOMPARE(offsetTotalSheets(1, 2, OffsetOutputMode::Grayscale, 4), 2);
}

void OffsetTilingTests::testFullColorUsesOnePlateRegardlessOfEnabledPlateCount()
{
	QCOMPARE(offsetTotalSheets(1, 4, OffsetOutputMode::FullColor, 4), 4);
	QCOMPARE(offsetTotalSheets(1, 4, OffsetOutputMode::FullColor, 1), 4);
}

void OffsetTilingTests::testCmykWithDisabledPlatesReducesSheetCount()
{
	// Two process colours unchecked in the Per-Plate table: 2 enabled plates
	// instead of 4.
	QCOMPARE(offsetTotalSheets(1, 4, OffsetOutputMode::CmykSeparations, 2), 8);
}

QTEST_APPLESS_MAIN(OffsetTilingTests)
