/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CANVASMODE_IMAGEERASER_H
#define CANVASMODE_IMAGEERASER_H

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>

#include "canvasmode.h"
#include "scimagestructs.h"

class PageItem;
class ScribusView;
class ScribusMainWindow;

/*!
 \brief Photoshop-style eraser for placed images.

 Paints into a non-destructive greyscale mask carried by the item's
 ScImageEffectList (see scimageerasermask.h); the image file on disk is never
 touched. Hold Alt while painting to restore erased pixels instead.

 Brush geometry is kept in *image* pixels rather than canvas pixels so that a
 stroke covers the same part of the photo whatever the zoom, matching how a
 raster editor behaves.
 */
class CanvasMode_ImageEraser : public CanvasMode
{
	Q_OBJECT

public:
	explicit CanvasMode_ImageEraser(ScribusView* view);

	void activate(bool fromGesture) override;
	void deactivate(bool forGesture) override;
	void enterEvent(QEvent*) override;
	void leaveEvent(QEvent*) override;
	void mousePressEvent(QMouseEvent* m) override;
	void mouseMoveEvent(QMouseEvent* m) override;
	void mouseReleaseEvent(QMouseEvent* m) override;
	void keyPressEvent(QKeyEvent* e) override;
	void drawControls(QPainter* p) override;

	//! Brush diameter in image pixels, as shown in the options bar.
	static int brushSize();
	static void setBrushSize(int size);
	//! Brush hardness, 0 (fully feathered) to 100 (hard edged).
	static int brushHardness();
	static void setBrushHardness(int hardness);

private:
	//! The image frame the eraser acts on, or null when the selection is not
	//! a usable image frame.
	PageItem* targetItem() const;
	//! Leave eraser mode and hand the canvas back to normal editing.
	void exitEraserMode();

	//! Map a canvas position to a pixel in the mask of \a item. Returns false
	//! when the point falls outside the frame or the geometry is degenerate.
	bool canvasToMask(PageItem* item, const QPointF& canvasPos, QPointF& maskPos) const;
	//! Brush radius in mask pixels for \a item, derived from the image scale.
	double maskRadiusFor(PageItem* item) const;

	//! Push the in-progress mask onto the item so the canvas shows the stroke.
	void refreshPreview(PageItem* item, const QRect& maskRegion);
	//! Fold the finished stroke into the item's stored mask, as one undo step.
	void commitStroke(PageItem* item);
	//! Drop stroke state without touching the item.
	void cancelStroke();

	ScribusMainWindow* m_ScMW {nullptr};

	//! Item the current stroke belongs to; empty when no stroke is running.
	QString m_strokeItemName;
	//! Effect list as it was before the stroke, for the undo state.
	ScImageEffectList m_effectsBefore;
	//! Mask as it was before the stroke started.
	QImage m_baseMask;
	//! Coverage accumulated by the current stroke, max()-blended so overlapping
	//! dabs do not pile up into a dark core.
	QImage m_coverage;
	//! m_baseMask combined with m_coverage; what the canvas is showing.
	QImage m_previewMask;
	//! Bounding box of everything this stroke has touched, in mask pixels.
	QRect m_strokeRegion;

	bool m_painting {false};
	//! True when the stroke restores rather than erases (Alt held at press).
	bool m_restoring {false};
	//! Last mouse position in mask pixels, for interpolating between events.
	QPointF m_lastMaskPos;
	//! Distance travelled since the last brush dab, carried across mouse-move
	//! events so dab spacing does not depend on the event rate.
	double m_dabCarry {0.0};
	//! Last mouse position in canvas pixels, for the cursor ring.
	QPointF m_cursorCanvasPos;
	bool m_cursorValid {false};
};

#endif
