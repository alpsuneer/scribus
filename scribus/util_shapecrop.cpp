/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "util_shapecrop.h"

#include <cmath>

#include "fpointarray.h"

namespace ShapeCrop
{

QPointF canvasToDoc(const QPointF& canvasPt, double scale, const QPointF& minCanvasCoordinate)
{
	if (scale == 0.0)
		return minCanvasCoordinate;
	return QPointF(canvasPt.x() / scale + minCanvasCoordinate.x(),
	               canvasPt.y() / scale + minCanvasCoordinate.y());
}

QPointF docToCanvas(const QPointF& docPt, double scale, const QPointF& minCanvasCoordinate)
{
	return QPointF((docPt.x() - minCanvasCoordinate.x()) * scale,
	               (docPt.y() - minCanvasCoordinate.y()) * scale);
}

QRectF docToCanvas(const QRectF& docRect, double scale, const QPointF& minCanvasCoordinate)
{
	return QRectF(docToCanvas(docRect.topLeft(), scale, minCanvasCoordinate),
	              docToCanvas(docRect.bottomRight(), scale, minCanvasCoordinate));
}

QPointF rotatePoint(const QPointF& pt, const QPointF& center, double angleDeg)
{
	const double rad = angleDeg * M_PI / 180.0;
	const double c = std::cos(rad);
	const double s = std::sin(rad);
	const double dx = pt.x() - center.x();
	const double dy = pt.y() - center.y();
	return QPointF(center.x() + dx * c - dy * s, center.y() + dx * s + dy * c);
}

QList<QPointF> handlePositions(const QRectF& rect, double rotateHandleOffset)
{
	const QPointF c = rect.center();
	QList<QPointF> pts;
	pts.reserve(9);
	pts << rect.topLeft();                                  // HandleTopLeft
	pts << QPointF(c.x(), rect.top());                       // HandleTop
	pts << rect.topRight();                                  // HandleTopRight
	pts << QPointF(rect.right(), c.y());                     // HandleRight
	pts << rect.bottomRight();                                // HandleBottomRight
	pts << QPointF(c.x(), rect.bottom());                    // HandleBottom
	pts << rect.bottomLeft();                                 // HandleBottomLeft
	pts << QPointF(rect.left(), c.y());                      // HandleLeft
	pts << QPointF(c.x(), rect.top() - rotateHandleOffset);  // HandleRotate
	return pts;
}

Handle hitTest(const QPointF& docPt, const QRectF& rect, double rotationDeg,
               double tolerance, double rotateHandleOffset)
{
	const QPointF center = rect.center();
	// Undo the overlay's rotation so the test happens in the rect's own axis-aligned space.
	const QPointF local = rotatePoint(docPt, center, -rotationDeg);

	const QList<QPointF> handles = handlePositions(rect, rotateHandleOffset);
	for (int i = 0; i < handles.size(); ++i)
	{
		const double dx = local.x() - handles[i].x();
		const double dy = local.y() - handles[i].y();
		if ((dx * dx + dy * dy) <= tolerance * tolerance)
			return static_cast<Handle>(i);
	}
	if (rect.contains(local))
		return HandleBody;
	return HandleNone;
}

QRectF resizeRect(const QRectF& startRect, Handle handle, const QPointF& localDelta,
                   bool lockAspect, double minSize)
{
	if (handle == HandleBody || handle == HandleRotate || handle == HandleNone)
		return startRect;

	const bool movesLeft   = (handle == HandleTopLeft || handle == HandleLeft || handle == HandleBottomLeft);
	const bool movesRight  = (handle == HandleTopRight || handle == HandleRight || handle == HandleBottomRight);
	const bool movesTop    = (handle == HandleTopLeft || handle == HandleTop || handle == HandleTopRight);
	const bool movesBottom = (handle == HandleBottomLeft || handle == HandleBottom || handle == HandleBottomRight);
	const bool isCorner    = (movesLeft || movesRight) && (movesTop || movesBottom);

	double left   = startRect.left();
	double right  = startRect.right();
	double top    = startRect.top();
	double bottom = startRect.bottom();

	if (movesLeft)
		left = qMin(startRect.left() + localDelta.x(), right - minSize);
	if (movesRight)
		right = qMax(startRect.right() + localDelta.x(), left + minSize);
	if (movesTop)
		top = qMin(startRect.top() + localDelta.y(), bottom - minSize);
	if (movesBottom)
		bottom = qMax(startRect.bottom() + localDelta.y(), top + minSize);

	if (lockAspect && isCorner && startRect.width() > 0.0 && startRect.height() > 0.0)
	{
		// Drive the resize off whichever axis moved more, relative to its own original size, then
		// derive the other axis from startRect's aspect ratio rather than the independent drag.
		const double wRatio = (right - left) / startRect.width();
		const double hRatio = (bottom - top) / startRect.height();
		double scale = (qAbs(wRatio - 1.0) >= qAbs(hRatio - 1.0)) ? wRatio : hRatio;
		scale = qMax(scale, minSize / qMin(startRect.width(), startRect.height()));

		const double newWidth  = startRect.width() * scale;
		const double newHeight = startRect.height() * scale;
		if (movesLeft)
			left = right - newWidth;
		else
			right = left + newWidth;
		if (movesTop)
			top = bottom - newHeight;
		else
			bottom = top + newHeight;
	}

	return QRectF(QPointF(left, top), QPointF(right, bottom));
}

double angleFromCenter(const QPointF& center, const QPointF& docPt)
{
	const double dx = docPt.x() - center.x();
	const double dy = docPt.y() - center.y();
	double deg = std::atan2(dx, -dy) * 180.0 / M_PI;
	if (deg < 0.0)
		deg += 360.0;
	return deg;
}

QPainterPath pathFromPercentValues(const QList<double>& percentValues, double w, double h)
{
	FPointArray path;
	for (int i = 0; i + 3 < percentValues.size(); i += 4)
	{
		if (percentValues[i] < 0)
		{
			path.setMarker();
			continue;
		}
		path.addPoint(w * percentValues[i]     / 100.0, h * percentValues[i + 1] / 100.0);
		path.addPoint(w * percentValues[i + 2] / 100.0, h * percentValues[i + 3] / 100.0);
	}
	return path.toQPainterPath(true);
}

} // namespace ShapeCrop
