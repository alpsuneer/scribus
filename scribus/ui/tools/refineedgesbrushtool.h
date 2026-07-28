/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef REFINEEDGESBRUSHTOOL_H
#define REFINEEDGESBRUSHTOOL_H

#include <QPointF>
#include <QPointer>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class QGraphicsEllipseItem;
class QSlider;
class QWidget;

/*!
 \brief Soft brush that paints the selection to clean up / refine its edges.

 Drag to add coverage; hold Alt to erase. [ and ] change the brush size; Shift+[
 / Shift+] change hardness. A ring follows the cursor to show the brush. Each
 stroke is a single undo step.
 */
class SCRIBUS_API RefineEdgesBrushTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Refine Edges Brush"); }
	QCursor cursor() const override;
	QWidget* optionsBar() override;

	void activate(ScImageEditor* editor) override;
	void deactivate() override;
	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override;
	void keyPress(QKeyEvent* e) override;

private:
	void updateRing(const QPointF& pos);
	void removeRing();
	void stampLine(const QPointF& from, const QPointF& to);
	void syncOptionsBar();   //!< reflect m_radius/m_hardness in the options sliders

	double m_radius { 30.0 };
	double m_hardness { 0.6 };
	bool m_painting { false };
	bool m_subtract { false };
	QPointF m_lastPos;
	QGraphicsEllipseItem* m_ring { nullptr };
	QPointer<QSlider> m_sizeSlider;   //!< options-bar sliders (auto-null on delete)
	QPointer<QSlider> m_hardSlider;
};

#endif // REFINEEDGESBRUSHTOOL_H
