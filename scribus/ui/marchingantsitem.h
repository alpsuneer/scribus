/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef MARCHINGANTSITEM_H
#define MARCHINGANTSITEM_H

#include <QGraphicsObject>
#include <QPainterPath>
#include <QTimer>

#include "scribusapi.h"

class ScImageSelection;

/*!
 \brief Animated "marching ants" overlay for a ScImageSelection.

 Draws the selection outline as an animated black/white dashed line on top of
 the image pixmap. It tracks the selection's changed() signal (recaching the
 outline only when it actually changes) and animates a dash-offset on a timer.
 */
class SCRIBUS_API MarchingAntsItem : public QGraphicsObject
{
	Q_OBJECT

public:
	explicit MarchingAntsItem(ScImageSelection* selection, QGraphicsItem* parent = nullptr);

	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private slots:
	void onSelectionChanged();
	void onTimer();

private:
	ScImageSelection* m_selection { nullptr };
	QTimer m_timer;
	int m_phase { 0 };
	QPainterPath m_cachedPath;
};

#endif // MARCHINGANTSITEM_H
