/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "offset_tiling.h"

#include <cmath>

#include <QtGlobal>

QStringList offsetTilePrintOrderNames()
{
	return {
		QStringLiteral("Plate-first"),
		QStringLiteral("Tile-first"),
		QStringLiteral("Sheet-optimized")
	};
}

QString offsetTilePrintOrderName(OffsetTilePrintOrder order)
{
	int idx = static_cast<int>(order);
	QStringList names = offsetTilePrintOrderNames();
	if ((idx < 0) || (idx >= names.count()))
		return names.at(0);
	return names.at(idx);
}

OffsetTilePrintOrder offsetTilePrintOrderFromName(const QString& name, bool* ok)
{
	QStringList names = offsetTilePrintOrderNames();
	int idx = names.indexOf(name);
	if (ok)
		*ok = (idx >= 0);
	if (idx < 0)
		return OffsetTilePrintOrder::PlateFirst;
	return static_cast<OffsetTilePrintOrder>(idx);
}

QStringList offsetTileOrientationNames()
{
	return {
		QStringLiteral("Portrait"),
		QStringLiteral("Landscape"),
		QStringLiteral("Auto")
	};
}

QString offsetTileOrientationName(OffsetTileOrientation orientation)
{
	int idx = static_cast<int>(orientation);
	QStringList names = offsetTileOrientationNames();
	if ((idx < 0) || (idx >= names.count()))
		return names.at(0);
	return names.at(idx);
}

OffsetTileOrientation offsetTileOrientationFromName(const QString& name, bool* ok)
{
	QStringList names = offsetTileOrientationNames();
	int idx = names.indexOf(name);
	if (ok)
		*ok = (idx >= 0);
	if (idx < 0)
		return OffsetTileOrientation::Auto;
	return static_cast<OffsetTileOrientation>(idx);
}

OffsetTileGrid offsetCalculateTileGrid(QSizeF docSize, QSizeF paperSize, double margin, double overlap)
{
	OffsetTileGrid grid;

	double docW = qMax(0.0, docSize.width());
	double docH = qMax(0.0, docSize.height());
	margin = qMax(0.0, margin);

	double usableW = qMax(1.0, paperSize.width()  - 2.0 * margin);
	double usableH = qMax(1.0, paperSize.height() - 2.0 * margin);

	// An overlap as large as (or larger than) the usable area would make the
	// tile step zero or negative and loop forever below - clamp it well
	// short of that.
	overlap = qMax(0.0, overlap);
	overlap = qMin(overlap, qMin(usableW, usableH) * 0.5);

	double stepW = qMax(1.0, usableW - overlap);
	double stepH = qMax(1.0, usableH - overlap);

	int cols = (docW <= usableW) ? 1 : static_cast<int>(std::ceil((docW - overlap) / stepW));
	int rows = (docH <= usableH) ? 1 : static_cast<int>(std::ceil((docH - overlap) / stepH));
	cols = qMax(1, cols);
	rows = qMax(1, rows);

	grid.cols = cols;
	grid.rows = rows;
	grid.tiles.reserve(cols * rows);

	// Row 0 is the topmost row: it starts at the page's top edge and its
	// bottom-origin y (see OffsetTileRect::bottom) is therefore the largest.
	for (int row = 0; row < rows; ++row)
	{
		double topOffset = row * stepH;
		double height = qMax(0.0, qMin(usableH, docH - topOffset));
		double bottom = docH - topOffset - height;
		for (int col = 0; col < cols; ++col)
		{
			double left = col * stepW;
			double width = qMax(0.0, qMin(usableW, docW - left));

			OffsetTileRect tile;
			tile.col = col;
			tile.row = row;
			tile.left = left;
			tile.bottom = bottom;
			tile.width = width;
			tile.height = height;
			grid.tiles.append(tile);
		}
	}

	return grid;
}

OffsetTileOrientation offsetSuggestOrientation(QSizeF docSize, QSizeF paperSize, double margin, double overlap)
{
	int portraitTiles = offsetCalculateTileGrid(docSize, paperSize, margin, overlap).tileCount();
	QSizeF landscapePaper(paperSize.height(), paperSize.width());
	int landscapeTiles = offsetCalculateTileGrid(docSize, landscapePaper, margin, overlap).tileCount();
	return (landscapeTiles < portraitTiles) ? OffsetTileOrientation::Landscape : OffsetTileOrientation::Portrait;
}

QSizeF offsetOrientedPaperSize(QSizeF docSize, QSizeF paperSize, OffsetTileOrientation orientation, double margin, double overlap)
{
	if (orientation == OffsetTileOrientation::Auto)
		orientation = offsetSuggestOrientation(docSize, paperSize, margin, overlap);
	if (orientation == OffsetTileOrientation::Landscape)
		return QSizeF(paperSize.height(), paperSize.width());
	return paperSize;
}

int offsetTotalSheets(int basePages, int tilesPerPage, OffsetOutputMode mode, int enabledPlateCount)
{
	int plates = (mode == OffsetOutputMode::CmykSeparations) ? qMax(1, enabledPlateCount) : 1;
	return qMax(0, basePages) * qMax(1, tilesPerPage) * plates;
}

QString offsetTileLabel(const OffsetTileRect& tile, const OffsetTileGrid& grid)
{
	int index = tile.row * grid.cols + tile.col + 1;
	QString label = QStringLiteral("Tile %1 of %2").arg(index).arg(grid.tileCount());

	QString vPos = (grid.rows <= 1) ? QString()
		: (tile.row == 0) ? QStringLiteral("Top")
		: (tile.row == grid.rows - 1) ? QStringLiteral("Bottom")
		: QStringLiteral("Mid");
	QString hPos = (grid.cols <= 1) ? QString()
		: (tile.col == 0) ? QStringLiteral("Left")
		: (tile.col == grid.cols - 1) ? QStringLiteral("Right")
		: QStringLiteral("Mid");

	if (!vPos.isEmpty() || !hPos.isEmpty())
	{
		if (vPos.isEmpty())
			vPos = QStringLiteral("Mid");
		if (hPos.isEmpty())
			hPos = QStringLiteral("Mid");
		label += QStringLiteral(" / %1-%2").arg(vPos, hPos);
	}

	return label;
}

static QString psEscapeString(QString text)
{
	text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	text.replace(QLatin1Char('('), QStringLiteral("\\("));
	text.replace(QLatin1Char(')'), QStringLiteral("\\)"));
	return text;
}

QString offsetTileRegistrationMarks(double sheetWidth, double sheetHeight, double markLength)
{
	markLength = qMax(2.0, markLength);
	double halfMark = markLength / 2.0;
	// Marks sit a fixed distance in from each sheet edge rather than at the
	// tile's own (post-clip) bounds, so they land at the same spot on every
	// tile sheet regardless of that tile's size - consistent corners are
	// what makes them useful for stacking/aligning trimmed film.
	double inset = markLength;
	double left = inset;
	double right = qMax(inset, sheetWidth - inset);
	double bottom = inset;
	double top = qMax(inset, sheetHeight - inset);

	QString out;
	out += QStringLiteral("gs\n0 setgray 0.3 setlinewidth\n");

	auto crosshair = [&](double cx, double cy)
	{
		out += QString::number(cx - halfMark) + QStringLiteral(" ") + QString::number(cy)
			+ QStringLiteral(" moveto ") + QString::number(cx + halfMark) + QStringLiteral(" ") + QString::number(cy) + QStringLiteral(" lineto stroke\n");
		out += QString::number(cx) + QStringLiteral(" ") + QString::number(cy - halfMark)
			+ QStringLiteral(" moveto ") + QString::number(cx) + QStringLiteral(" ") + QString::number(cy + halfMark) + QStringLiteral(" lineto stroke\n");
	};
	crosshair(left, bottom);
	crosshair(right, bottom);
	crosshair(right, top);
	crosshair(left, top);

	out += QStringLiteral("gr\n");
	return out;
}

QString offsetTileCutMarks(double sheetWidth, double sheetHeight, double markLength)
{
	markLength = qMax(2.0, markLength);
	double halfMark = markLength / 2.0;
	double midX = sheetWidth / 2.0;
	double midY = sheetHeight / 2.0;
	double inset = markLength; // stays clear of the registration crosshairs in the corners

	QString out;
	out += QStringLiteral("gs\n0 setgray 0.5 setlinewidth\n");

	auto tick = [&](double x1, double y1, double x2, double y2)
	{
		out += QString::number(x1) + QStringLiteral(" ") + QString::number(y1)
			+ QStringLiteral(" moveto ") + QString::number(x2) + QStringLiteral(" ") + QString::number(y2) + QStringLiteral(" lineto stroke\n");
	};
	// Bottom and top edge midpoints: short vertical ticks straddling the trim line.
	tick(midX, inset - halfMark, midX, inset + halfMark);
	tick(midX, sheetHeight - inset - halfMark, midX, sheetHeight - inset + halfMark);
	// Left and right edge midpoints: short horizontal ticks.
	tick(inset - halfMark, midY, inset + halfMark, midY);
	tick(sheetWidth - inset - halfMark, midY, sheetWidth - inset + halfMark, midY);

	out += QStringLiteral("gr\n");
	return out;
}

QString offsetTileLabelPS(const OffsetTileRect& tile, const OffsetTileGrid& grid,
	const QString& plateName, double sheetWidth, double sheetHeight)
{
	Q_UNUSED(sheetWidth)
	const double markLength = 20.0; // matches offsetTileRegistrationMarks()'s default inset, so the label sits clear of a corner crosshair when both are on
	double left = markLength;
	double top = qMax(markLength, sheetHeight - markLength);

	QString label = offsetTileLabel(tile, grid);
	if (!plateName.isEmpty())
		label = plateName + QStringLiteral(" / ") + label;
	label = psEscapeString(label);

	QString out;
	out += QStringLiteral("gs\n0 setgray\n");
	out += QStringLiteral("/Helvetica findfont 8 scalefont setfont\n");
	out += QString::number(left) + QStringLiteral(" ") + QString::number(top - markLength * 1.5)
		+ QStringLiteral(" moveto (") + label + QStringLiteral(") show\n");
	out += QStringLiteral("gr\n");
	return out;
}
