/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CANVASMODE_SHAPECROP_H
#define CANVASMODE_SHAPECROP_H

#include <QPointF>
#include <QRectF>
#include <QString>

#include "canvasmode.h"
#include "ui/frameshapemenu.h"
#include "util_shapecrop.h"

class PageItem;
class ScribusMainWindow;

/*! \brief Photoshop-style interactive crop: pick a shape from the Frame Shape menu, drag an
    overlay into place over the selected frame, then commit it with Enter (or cancel with Escape,
    or by clicking outside the overlay).

    Modelled on CanvasMode_SuneerCrop, which does the same thing for a plain rectangular crop -
    an overlay the user manipulates before anything is applied to the frame, rather than an
    instant apply-on-click. The overlay's shape preview is drawn from the same percent-of-frame
    control points FrameShapeMenu::shapeIcon() draws its menu icons from
    (ShapeCrop::pathFromPercentValues(), evaluated at the overlay's current size instead of a
    fixed 16px icon).

    On commit, the selected frame is moved and resized to the overlay's bounds (and rotation, if
    the overlay was rotated) and then reshaped with ScribusDoc::item_setFrameShape() using the
    catalogue's own control points - the same call ScribusMainWindow::applyFrameShape() makes -
    so the result is pixel-for-pixel what the existing Frame Shape menu would have produced had
    the frame already had the overlay's bounds. For an image frame, imageXOffset/imageYOffset are
    shifted by the same amount so the image content stays visually anchored under the overlay
    instead of jumping with the frame, the same way CanvasMode_SuneerCrop's plain rectangular crop
    does it.

    Both crop tools assume the frame being cropped is not itself already rotated - the overlay is
    drawn and hit-tested against the frame's unrotated xPos()/yPos()/width()/height(), so a source
    frame with a pre-existing rotation is not accounted for specially. */
class CanvasMode_ShapeCrop : public CanvasMode
{
	Q_OBJECT
public:
	explicit CanvasMode_ShapeCrop(ScribusView* view);
	void activate(bool fromGesture) override;
	void deactivate(bool forGesture) override;
	void enterEvent(QEvent*) override;
	void leaveEvent(QEvent*) override;
	void mousePressEvent(QMouseEvent* m) override;
	void mouseMoveEvent(QMouseEvent* m) override;
	void mouseReleaseEvent(QMouseEvent* m) override;
	void keyPressEvent(QKeyEvent* e) override;
	void drawControls(QPainter* p) override;

private:
	//! Leave shape-crop mode and restore normal editing, without touching the frame.
	void cancelCrop();
	//! Resize/reposition the frame to the overlay's bounds and apply the chosen shape to it.
	void commitCrop();
	//! The frame shape-crop mode was entered on; used to notice a selection change mid-drag.
	PageItem* targetItem() const;
	//! 60% of the item's bounds, centred - the overlay's starting size and position, in doc points.
	static QRectF initialOverlayRect(const PageItem* item);
	//! m->pos() (view-canvas pixels) translated to document points.
	QPointF mouseDocPos(const QPoint& viewPos) const;
	//! A handle's hit radius, in document points at the view's current zoom.
	double handleToleranceDoc() const;
	//! How far above the overlay the rotation handle floats, in document points at the current zoom.
	double rotateHandleOffsetDoc() const;
	void applyCursorForHandle(ShapeCrop::Handle h);

	ScribusMainWindow* m_ScMW { nullptr };

	FrameShapeDef m_shapeDef;
	bool          m_hasShape { false };
	QString       m_cropItemName;   //!< item crop mode started on (empty = none)

	QRectF  m_overlayRect;          //!< doc points, unrotated (rotation is applied about its centre)
	double  m_rotation { 0.0 };     //!< degrees, clockwise, 0 = axis-aligned

	ShapeCrop::Handle m_dragHandle { ShapeCrop::HandleNone };
	bool    m_mouseDown { false };
	QPointF m_dragStartMouseDoc;
	QRectF  m_dragStartRect;
	double  m_dragStartRotation { 0.0 };
	double  m_dragStartAngleOffset { 0.0 };  //!< angle-to-mouse minus m_rotation at rotate-drag start, kept constant so rotation doesn't jump to the handle
};

#endif
