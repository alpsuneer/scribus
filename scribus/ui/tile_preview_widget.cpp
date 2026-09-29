/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "tile_preview_widget.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace {
	const int PreviewPadding = 10;
	//! Small fixed palette of alternating tile fills, cycling by (row*cols+col)
	//! mod 4 - enough to make adjacent tiles visually distinct without needing
	//! a colour per tile count.
	const QColor TileFills[4] = {
		QColor(200, 225, 250), QColor(250, 220, 190),
		QColor(210, 245, 210), QColor(245, 210, 245)
	};
}

TilePreviewWidget::TilePreviewWidget(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(160, 160);
}

QSize TilePreviewWidget::sizeHint() const
{
	return QSize(220, 220);
}

void TilePreviewWidget::setGrid(const OffsetTileGrid& grid, QSizeF docSizeMm)
{
	m_grid = grid;
	m_docSizeMm = docSizeMm;
	update();
}

QRectF TilePreviewWidget::docRectInWidget() const
{
	QRectF avail(PreviewPadding, PreviewPadding, width() - 2.0 * PreviewPadding, height() - 2.0 * PreviewPadding);
	if ((avail.width() <= 0.0) || (avail.height() <= 0.0) || (m_docSizeMm.width() <= 0.0) || (m_docSizeMm.height() <= 0.0))
		return QRectF();
	double scale = std::min(avail.width() / m_docSizeMm.width(), avail.height() / m_docSizeMm.height());
	double w = m_docSizeMm.width() * scale;
	double h = m_docSizeMm.height() * scale;
	return QRectF(avail.x() + (avail.width() - w) / 2.0, avail.y() + (avail.height() - h) / 2.0, w, h);
}

void TilePreviewWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette().window());

	QRectF docRect = docRectInWidget();
	if (docRect.isEmpty())
	{
		painter.setPen(palette().text().color());
		painter.drawText(rect(), Qt::AlignCenter, tr("No document"));
		return;
	}

	// Document page outline.
	painter.setPen(QPen(palette().text().color(), 1));
	painter.setBrush(QColor(250, 250, 250));
	painter.drawRect(docRect);

	if (m_grid.tiles.isEmpty() || m_grid.isSingleSheet())
	{
		painter.setPen(palette().text().color());
		painter.drawText(docRect, Qt::AlignCenter, tr("1 sheet\n(no tiling needed)"));
		return;
	}

	double scaleX = docRect.width() / m_docSizeMm.width();
	double scaleY = docRect.height() / m_docSizeMm.height();

	QFont numberFont = painter.font();
	numberFont.setBold(true);

	for (const OffsetTileRect& tile : m_grid.tiles)
	{
		// tile.bottom measures up from the document's BOTTOM edge (y-up -
		// see OffsetTileRect's coordinate note in offset_tiling.h); the
		// widget paints top-down, so the y axis is flipped here.
		double x = tile.left * scaleX;
		double yTop = (m_docSizeMm.height() - tile.bottom - tile.height) * scaleY;
		double w = tile.width * scaleX;
		double h = tile.height * scaleY;
		QRectF tileRect(docRect.x() + x, docRect.y() + yTop, w, h);

		int colorIndex = (tile.row * m_grid.cols + tile.col) % 4;
		painter.setPen(QPen(Qt::black, 1));
		painter.setBrush(TileFills[colorIndex]);
		painter.drawRect(tileRect);

		int index = tile.row * m_grid.cols + tile.col + 1;
		painter.setFont(numberFont);
		painter.setPen(Qt::black);
		painter.drawText(tileRect, Qt::AlignCenter, QString::number(index));
	}
}
