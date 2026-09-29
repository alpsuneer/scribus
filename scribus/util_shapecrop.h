/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UTIL_SHAPECROP_H
#define UTIL_SHAPECROP_H

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include "scribusapi.h"

/*! \brief Pure geometry for the interactive Frame Shape crop overlay (CanvasMode_ShapeCrop).

    Everything here is plain maths: no PageItem, no ScribusView, no Canvas. The
    caller supplies the view scale and the document's minCanvasCoordinate
    explicitly rather than this module reaching for them itself, which is also
    what keeps it link-testable in frameshapetests without pulling in the rest
    of the application - the same reason util_math.cpp's frame-shape
    generators take no ScribusDoc either.
 */
namespace ShapeCrop
{
	//! Which part of the overlay a point landed on. Order matches handlePositions().
	enum Handle
	{
		HandleNone = -1,
		HandleTopLeft = 0,
		HandleTop,
		HandleTopRight,
		HandleRight,
		HandleBottomRight,
		HandleBottom,
		HandleBottomLeft,
		HandleLeft,
		HandleRotate,
		HandleBody
	};

	//! View-canvas pixel coordinates -> document points.
	QPointF SCRIBUS_API canvasToDoc(const QPointF& canvasPt, double scale, const QPointF& minCanvasCoordinate);
	//! Document points -> view-canvas pixel coordinates.
	QPointF SCRIBUS_API docToCanvas(const QPointF& docPt, double scale, const QPointF& minCanvasCoordinate);
	//! Document points -> view-canvas pixel coordinates, corner to corner.
	QRectF SCRIBUS_API docToCanvas(const QRectF& docRect, double scale, const QPointF& minCanvasCoordinate);

	//! Rotate \a pt about \a center by \a angleDeg, clockwise on screen (Qt's QPainter::rotate() convention).
	QPointF SCRIBUS_API rotatePoint(const QPointF& pt, const QPointF& center, double angleDeg);

	/*! \brief The eight resize-handle positions plus the rotation handle, in the rect's own
	    unrotated space. Order matches the Handle enum: TopLeft, Top, TopRight, Right,
	    BottomRight, Bottom, BottomLeft, Left, Rotate.
	    \param rotateHandleOffset how far above the rect's top edge the rotation handle sits. */
	QList<QPointF> SCRIBUS_API handlePositions(const QRectF& rect, double rotateHandleOffset);

	/*! \brief What in \a rect, rotated by \a rotationDeg about its own centre, lies under \a docPt.
	    \param tolerance a handle's hit radius, in the same units as \a rect. */
	Handle SCRIBUS_API hitTest(const QPointF& docPt, const QRectF& rect, double rotationDeg,
	                           double tolerance, double rotateHandleOffset);

	/*! \brief Resize \a startRect by dragging \a handle.
	    \param localDelta the drag vector (current mouse minus mouse-down point) already expressed
	           in the rect's own unrotated space, i.e. with the overlay's fixed rotation undone.
	    \param lockAspect keep startRect's aspect ratio; only has an effect on corner handles.
	    \param minSize a result narrower or shorter than this on either axis is clamped to it,
	           anchored on the edge/corner opposite the one being dragged.
	    Body and rotate "handles" are not resizes; both return \a startRect unchanged. */
	QRectF SCRIBUS_API resizeRect(const QRectF& startRect, Handle handle, const QPointF& localDelta,
	                              bool lockAspect, double minSize = 4.0);

	//! The rotation handle's angle (degrees, 0 = straight up, clockwise positive) for \a docPt about \a center.
	double SCRIBUS_API angleFromCenter(const QPointF& center, const QPointF& docPt);

	/*! \brief Trace \a percentValues into a \a w x \a h path.
	    \param percentValues control points in FrameShapeDef::values()' format: percent of a
	           100x100 box, four doubles per point pair (outgoing then incoming control), a
	           leading -1 group marking a subpath break. The same format FrameShapeMenu::shapeIcon()
	           draws its menu icons from, here evaluated at an arbitrary box instead of a fixed one. */
	QPainterPath SCRIBUS_API pathFromPercentValues(const QList<double>& percentValues, double w, double h);
}

#endif
