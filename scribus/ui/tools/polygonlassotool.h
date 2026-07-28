/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef POLYGONLASSOTOOL_H
#define POLYGONLASSOTOOL_H

#include <QPointF>
#include <QVector>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class QGraphicsPathItem;

/*!
 \brief Polygonal lasso: click to place anchors, a rubber-band line follows the
        cursor; double-click or Enter closes and commits, Escape cancels,
        Backspace removes the last anchor.
 */
class SCRIBUS_API PolygonLassoTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Polygonal Lasso"); }

	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override { Q_UNUSED(e) Q_UNUSED(imagePos) }  // polygon lasso commits on click/Enter, not release
	void mouseDoubleClick(QMouseEvent* e, const QPointF& imagePos) override;
	void keyPress(QKeyEvent* e) override;
	void deactivate() override;

private:
	void updatePreview();
	void removePreview();
	void commit();
	void cancel();

	QVector<QPointF> m_anchors;
	QPointF m_cursor;
	QGraphicsPathItem* m_preview { nullptr };
	ScImageSelection::Mode m_mode { ScImageSelection::Replace };
};

#endif // POLYGONLASSOTOOL_H
