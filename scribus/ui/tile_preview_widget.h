/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef TILE_PREVIEW_WIDGET_H
#define TILE_PREVIEW_WIDGET_H

#include "scribusapi.h"
#include "offset_tiling.h"

#include <QRectF>
#include <QSizeF>
#include <QWidget>

/*! \brief Small schematic preview of the Offset Separations tab's tile grid:
    the document page outline, scaled to fit, with each tile drawn as a
    numbered, alternately-coloured rectangle. Pure QPainter, no external
    rendering dependency - a single-digit or low-double-digit tile count is
    the realistic range, so paintEvent() stays well under a frame's worth of
    work and never blocks the dialog.

    Geometry-only preview, like ImpositionPreviewWidget: it never renders the
    document's actual content, only the tile grid's rectangles and numbers.
*/
class SCRIBUS_API TilePreviewWidget : public QWidget
{
	Q_OBJECT

public:
	explicit TilePreviewWidget(QWidget* parent = nullptr);

	//! Recomputes and repaints from the given grid and document page size
	//! (millimetres - purely for the widget's own aspect ratio, any
	//! consistent unit would do since only ratios are drawn).
	void setGrid(const OffsetTileGrid& grid, QSizeF docSizeMm);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QRectF docRectInWidget() const;

	OffsetTileGrid m_grid;
	QSizeF m_docSizeMm;
};

#endif
