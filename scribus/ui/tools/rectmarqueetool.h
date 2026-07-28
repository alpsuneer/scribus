/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef RECTMARQUEETOOL_H
#define RECTMARQUEETOOL_H

#include <QPointF>
#include <QRectF>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class QGraphicsItem;

/*!
 \brief Rectangular marquee selection tool. Drag a rubber-band rectangle;
        on release it commits a rectangular selection (Shift/Alt = add/subtract).
 */
class SCRIBUS_API RectMarqueeTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Rectangular Marquee"); }

	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override;
	void deactivate() override;

protected:
	// Shape hooks — the ellipse tool overrides these.
	virtual void createRubber(const QRectF& r);
	virtual void setRubber(const QRectF& r);
	virtual void commit(const QRect& r, ScImageSelection::Mode mode);
	void removeRubber();

	QGraphicsItem* m_rubber { nullptr };
	QPointF m_start;
	ScImageSelection::Mode m_mode { ScImageSelection::Replace };
	bool m_active { false };
};

#endif // RECTMARQUEETOOL_H
