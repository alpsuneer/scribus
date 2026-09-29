/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OFFSET_TILING_H
#define OFFSET_TILING_H

#include "scribusapi.h"
#include "offset_separation_presets.h"

#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

//! Order tile/plate sheets are emitted in the PostScript job, selectable in
//! the Offset Separations tab's Tiling group. Values are stable - stored as
//! the combo's index and (by name, via the helpers below) in PrintOptions.
enum class OffsetTilePrintOrder
{
	//! All tiles of Cyan, then all tiles of Magenta, then Yellow, then Black.
	//! Groups physically-identical-colour film together, which is what makes
	//! plate assembly straightforward on a press floor - the default.
	PlateFirst = 0,
	//! Tile 1 of every plate, then tile 2 of every plate, and so on. Useful
	//! when tiles (not plates) are being proofed or checked one at a time.
	TileFirst = 1,
	//! Tile-first, but tiles are visited in a serpentine (boustrophedon)
	//! column order within each row - left-to-right on even rows, right-to-
	//! left on odd rows - so consecutive sheets are always adjacent tiles,
	//! minimising how far a manual imagesetter/platesetter has to reposition
	//! between sheets.
	SheetOptimized = 2
};

SCRIBUS_API QStringList offsetTilePrintOrderNames();
SCRIBUS_API QString offsetTilePrintOrderName(OffsetTilePrintOrder order);
SCRIBUS_API OffsetTilePrintOrder offsetTilePrintOrderFromName(const QString& name, bool* ok = nullptr);

//! Which way the tile paper is fed. Values are stable - held as the display
//! name string in PrintOptions, same reasoning as OffsetTilePrintOrder.
enum class OffsetTileOrientation
{
	Portrait = 0,
	Landscape = 1,
	//! Resolved to whichever of Portrait/Landscape needs fewer tiles for the
	//! document at hand - see offsetSuggestOrientation().
	Auto = 2
};

SCRIBUS_API QStringList offsetTileOrientationNames();
SCRIBUS_API QString offsetTileOrientationName(OffsetTileOrientation orientation);
SCRIBUS_API OffsetTileOrientation offsetTileOrientationFromName(const QString& name, bool* ok = nullptr);

//! One tile's placement within a document page.
struct SCRIBUS_API OffsetTileRect
{
	int col { 0 };
	int row { 0 };
	//! Distance in points from the page's left edge to this tile's left edge.
	double left { 0.0 };
	//! Distance in points from the page's BOTTOM edge to this tile's bottom
	//! edge - i.e. bottom-left origin, y increasing upward. This matches the
	//! page-local coordinate space PSLib::PS_begin_page() already clips
	//! margins in (see the Ma->bottom()/pg->height()-Ma->top() clip path
	//! there), not Qt's top-down QRectF convention - kept as named doubles
	//! rather than QRectF so a caller cannot mix the two up silently.
	double bottom { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
};

//! How a document page is covered by tiles at a given paper size.
struct SCRIBUS_API OffsetTileGrid
{
	int cols { 1 };
	int rows { 1 };
	QVector<OffsetTileRect> tiles;

	//! cols*rows rather than tiles.count(): offsetCalculateTileGrid() always
	//! fills every row/col slot, so the two agree there, but this also lets
	//! a caller that only has one tile's grid position (e.g. PSLib::PS_end_page(),
	//! which does not carry the full tile list) build a one-element grid with
	//! just cols/rows set and still get a correct count for labelling.
	int tileCount() const { return cols * rows; }
	//! True when the page fits on one sheet - the grid is 1x1 and covers the
	//! full page, so callers should print it as a single ordinary sheet
	//! rather than going through the tiled code path at all.
	bool isSingleSheet() const { return (cols <= 1) && (rows <= 1); }
};

/*! \brief Splits a document page into a grid of tiles that each fit within
    paperSize (minus margin on every edge), overlapping neighbours by overlap.

    All arguments and all returned rectangle fields are in the same linear
    unit - points when called from the print pipeline (ScribusDoc::pageWidth()/
    height(), PrinterUtil::paperSizePoints()), millimetres when called from
    the print dialog's live preview label. Mixing units between arguments
    produces a meaningless grid; the function does not know or care which
    unit it was given.

    docSize.width()/height() fitting within one sheet's usable area on both
    axes yields a 1x1 grid equal to the full page - see
    OffsetTileGrid::isSingleSheet() - even though this is called
    unconditionally by anything that needs to know if tiling is required.

    @param docSize document page size
    @param paperSize physical sheet size a tile must fit on
    @param margin printer's unprintable margin, subtracted from paperSize on
    every edge to get the usable area a tile may occupy
    @param overlap how much neighbouring tiles overlap on their shared edge,
    for trim and registration once the film/plate output is cut and stacked
*/
SCRIBUS_API OffsetTileGrid offsetCalculateTileGrid(QSizeF docSize, QSizeF paperSize, double margin, double overlap);

/*! \brief Portrait or Landscape - whichever needs fewer tiles for docSize.

    paperSize must be given in its own "portrait" convention (width <=
    height, as PrinterUtil::paperSizePoints() returns it) regardless of which
    orientation wins; this swaps it internally to test Landscape. Ties (equal
    tile counts) favour Portrait, matching the print dialog's radio button
    order. Never returns Auto - Auto is what calls this to resolve itself.
*/
SCRIBUS_API OffsetTileOrientation offsetSuggestOrientation(QSizeF docSize, QSizeF paperSize, double margin, double overlap);

/*! \brief Resolves paperSize (portrait convention) to the actual sheet size
    tiling should use for the given orientation choice.

    Portrait returns paperSize unchanged; Landscape returns it with width and
    height swapped; Auto resolves via offsetSuggestOrientation() first and
    then does the same.
*/
SCRIBUS_API QSizeF offsetOrientedPaperSize(QSizeF docSize, QSizeF paperSize, OffsetTileOrientation orientation, double margin, double overlap);

/*! \brief Total physical sheets for one export run.

    basePages x tilesPerPage x plate count, where the plate count is
    enabledPlateCount (usually 4, less any process colour the operator
    unchecked in the Per-Plate table) for CmykSeparations and always 1 for
    Grayscale/FullColor - see OffsetOutputMode. This is the one place that
    formula is written down; PrintDialog's live info label and PSLib::createPS()'s
    %%Pages: count both call it rather than each re-deriving it.
*/
SCRIBUS_API int offsetTotalSheets(int basePages, int tilesPerPage, OffsetOutputMode mode, int enabledPlateCount);

//! Human-readable tile position, e.g. "Tile 1 of 4 / Top-Left". Column/row
//! extremes are named (Top/Bottom, Left/Right); a tile in neither extreme on
//! an axis is called "Mid" on that axis. A 1xN or Nx1 grid omits the axis
//! that never varies.
SCRIBUS_API QString offsetTileLabel(const OffsetTileRect& tile, const OffsetTileGrid& grid);

/*! \name Per-tile PostScript marks
    All three are emitted in the tile's own sheet-local coordinate space
    (0,0 at the tile's own bottom-left corner, matching what
    PSLib::PS_begin_page() establishes for a tiled page - see PSPageTile in
    pslib.h), independent of where the tile sits within the full document
    page. Each is a self-contained gs/gr (save/restore) pair: safe to emit
    in any combination, in any order, at any point after a page's content
    and before PS_end_page(), and none touches colour or line width outside
    its own block. Kept as three independent functions rather than one,
    matching the print dialog's three independent checkboxes (registration
    marks / cut marks / tile label) exactly - a caller enables whichever it
    needs rather than getting an all-or-nothing bundle.
*/
//!@{

//! Corner registration crosshairs, a fixed distance in from each sheet edge
//! regardless of the tile's own size - so they land at the same spot on
//! every tile sheet, which is what makes them useful for aligning trimmed
//! film/plate output across plates.
SCRIBUS_API QString offsetTileRegistrationMarks(double sheetWidth, double sheetHeight, double markLength = 20.0);

//! Short perpendicular cut/trim tick marks at the midpoint of each sheet
//! edge, marking where neighbouring tiles should be trimmed and butted
//! together - distinct from the corner registration crosshairs above.
SCRIBUS_API QString offsetTileCutMarks(double sheetWidth, double sheetHeight, double markLength = 10.0);

//! Just the "Plate / Tile N of M / position" text label (see
//! offsetTileLabel()), with no marks of its own.
//! @param plateName colorant this sheet is for ("Cyan" etc), or empty for a
//! composite (non-separated) tiled sheet - prefixed onto the label text.
SCRIBUS_API QString offsetTileLabelPS(const OffsetTileRect& tile, const OffsetTileGrid& grid,
	const QString& plateName, double sheetWidth, double sheetHeight);

//!@}

#endif // OFFSET_TILING_H
