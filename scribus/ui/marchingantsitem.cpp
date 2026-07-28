/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/marchingantsitem.h"

#include <QPainter>
#include <QPen>

#include "scimageselection.h"

MarchingAntsItem::MarchingAntsItem(ScImageSelection* selection, QGraphicsItem* parent)
	: QGraphicsObject(parent),
	  m_selection(selection)
{
	setZValue(100);   // above the image pixmap
	setAcceptedMouseButtons(Qt::NoButton);   // purely decorative

	if (m_selection)
	{
		connect(m_selection, &ScImageSelection::changed, this, &MarchingAntsItem::onSelectionChanged);
		m_cachedPath = m_selection->outlinePath();
	}

	m_timer.setInterval(100);
	connect(&m_timer, &QTimer::timeout, this, &MarchingAntsItem::onTimer);
	if (!m_cachedPath.isEmpty())
		m_timer.start();
}

QRectF MarchingAntsItem::boundingRect() const
{
	if (m_cachedPath.isEmpty())
		return QRectF();
	// A little slack for the cosmetic pen width.
	return m_cachedPath.boundingRect().adjusted(-2, -2, 2, 2);
}

void MarchingAntsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(option)
	Q_UNUSED(widget)
	if (m_cachedPath.isEmpty())
		return;

	painter->setBrush(Qt::NoBrush);
	const QVector<qreal> dashes { 4.0, 4.0 };

	QPen black(Qt::black);
	black.setCosmetic(true);   // constant width regardless of zoom
	black.setWidth(1);
	black.setDashPattern(dashes);
	black.setDashOffset(m_phase);
	painter->setPen(black);
	painter->drawPath(m_cachedPath);

	QPen white(Qt::white);
	white.setCosmetic(true);
	white.setWidth(1);
	white.setDashPattern(dashes);
	white.setDashOffset(m_phase + 4);   // interleave with the black dashes
	painter->setPen(white);
	painter->drawPath(m_cachedPath);
}

void MarchingAntsItem::onSelectionChanged()
{
	prepareGeometryChange();
	m_cachedPath = m_selection ? m_selection->outlinePath() : QPainterPath();
	if (m_cachedPath.isEmpty())
		m_timer.stop();
	else if (!m_timer.isActive())
		m_timer.start();
	update();
}

void MarchingAntsItem::onTimer()
{
	m_phase = (m_phase + 1) % 8;
	update();
}
