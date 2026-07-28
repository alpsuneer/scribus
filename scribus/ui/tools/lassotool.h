/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef LASSOTOOL_H
#define LASSOTOOL_H

#include <QPainterPath>
#include <QPointF>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class QGraphicsPathItem;

/*!
 \brief Freehand lasso: drag to trace a path; on release the closed path
        becomes the selection (Shift/Alt = add/subtract).
 */
class SCRIBUS_API LassoTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Lasso"); }

	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override;
	void deactivate() override;

private:
	void updatePreview();
	void removePreview();

	QPainterPath m_path;
	QPointF m_last;
	QGraphicsPathItem* m_preview { nullptr };
	ScImageSelection::Mode m_mode { ScImageSelection::Replace };
	bool m_active { false };
};

#endif // LASSOTOOL_H
