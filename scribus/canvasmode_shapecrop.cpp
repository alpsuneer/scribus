/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "canvasmode_shapecrop.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStatusBar>
#include <QTransform>

#include "appmodes.h"
#include "canvas.h"
#include "pageitem.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "ui/modetoolbar.h"
#include "undomanager.h"

CanvasMode_ShapeCrop::CanvasMode_ShapeCrop(ScribusView* view)
	: CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

PageItem* CanvasMode_ShapeCrop::targetItem() const
{
	if (!m_doc || m_doc->m_Selection->isEmpty())
		return nullptr;
	return m_doc->m_Selection->itemAt(0);
}

QRectF CanvasMode_ShapeCrop::initialOverlayRect(const PageItem* item)
{
	const double w = item->width() * 0.6;
	const double h = item->height() * 0.6;
	const double x = item->xPos() + (item->width() - w) / 2.0;
	const double y = item->yPos() + (item->height() - h) / 2.0;
	return QRectF(x, y, w, h);
}

QPointF CanvasMode_ShapeCrop::mouseDocPos(const QPoint& viewPos) const
{
	return ShapeCrop::canvasToDoc(QPointF(viewPos), m_view->scale(), m_doc->minCanvasCoordinate.toQPointF());
}

double CanvasMode_ShapeCrop::handleToleranceDoc() const
{
	const double scale = m_view->scale();
	return (scale > 0.0) ? (8.0 / scale) : 8.0;
}

double CanvasMode_ShapeCrop::rotateHandleOffsetDoc() const
{
	const double scale = m_view->scale();
	return (scale > 0.0) ? (24.0 / scale) : 24.0;
}

void CanvasMode_ShapeCrop::applyCursorForHandle(ShapeCrop::Handle h)
{
	switch (h)
	{
		case ShapeCrop::HandleTopLeft:
		case ShapeCrop::HandleBottomRight:
			setResizeCursor(1);
			break;
		case ShapeCrop::HandleTopRight:
		case ShapeCrop::HandleBottomLeft:
			setResizeCursor(3);
			break;
		case ShapeCrop::HandleTop:
		case ShapeCrop::HandleBottom:
			setResizeCursor(5);
			break;
		case ShapeCrop::HandleLeft:
		case ShapeCrop::HandleRight:
			setResizeCursor(6);
			break;
		case ShapeCrop::HandleBody:
			m_view->setCursor(QCursor(Qt::SizeAllCursor));
			break;
		case ShapeCrop::HandleRotate:
			// Qt/Scribus have no built-in rotate cursor; cross is the closest stock shape.
			m_view->setCursor(QCursor(Qt::CrossCursor));
			break;
		default:
			m_view->setCursor(QCursor(Qt::ArrowCursor));
			break;
	}
}

void CanvasMode_ShapeCrop::activate(bool fromGesture)
{
	CanvasMode::activate(fromGesture);

	m_mouseDown = false;
	m_dragHandle = ShapeCrop::HandleNone;
	m_hasShape = false;
	m_rotation = 0.0;
	m_overlayRect = QRectF();
	m_cropItemName.clear();

	PageItem* item = targetItem();
	if (!item || !m_ScMW)
	{
		// Nothing sane to crop - selection changed under us, or somebody switched into this mode
		// without going through ScribusMainWindow::startFrameShapeCrop(). Bail to normal editing
		// rather than sitting inert with no overlay and no way out but Escape.
		if (m_ScMW)
			m_ScMW->setAppModeByToggle(false, modeShapeCrop);
		return;
	}

	const QString shapeId = m_ScMW->takePendingFrameShapeId();
	const FrameShapeDef* def = FrameShapeMenu::shapeById(shapeId);
	if (!def)
	{
		m_ScMW->setAppModeByToggle(false, modeShapeCrop);
		return;
	}

	m_shapeDef = *def;
	m_hasShape = true;
	m_cropItemName = item->itemName();
	m_overlayRect = initialOverlayRect(item);

	m_ScMW->statusBar()->showMessage(
		tr("Move and resize the shape. Press Enter to apply crop, Escape to cancel."));
}

void CanvasMode_ShapeCrop::deactivate(bool forGesture)
{
	CanvasMode::deactivate(forGesture);
	if (forGesture)
		return;
	m_mouseDown = false;
	m_dragHandle = ShapeCrop::HandleNone;
	m_hasShape = false;
	m_cropItemName.clear();
	if (m_ScMW)
		m_ScMW->statusBar()->clearMessage();
	m_view->unsetCursor();
}

void CanvasMode_ShapeCrop::enterEvent(QEvent*)
{
	m_view->setCursor(QCursor(Qt::ArrowCursor));
}

void CanvasMode_ShapeCrop::leaveEvent(QEvent*)
{
	m_view->unsetCursor();
}

void CanvasMode_ShapeCrop::cancelCrop()
{
	m_mouseDown = false;
	m_dragHandle = ShapeCrop::HandleNone;
	m_hasShape = false;
	if (m_view)
		m_view->updateCanvas();
	if (m_ScMW)
		m_ScMW->setAppModeByToggle(false, modeShapeCrop);
}

void CanvasMode_ShapeCrop::mousePressEvent(QMouseEvent* m)
{
	m->accept();

	PageItem* item = targetItem();
	const QString nowName = item ? item->itemName() : QString();
	if (!m_hasShape || nowName != m_cropItemName)
	{
		// The frame we started on is gone, or another one got selected from under us (e.g. via
		// the Outline palette) - drop back to normal editing rather than dragging a crop overlay
		// for a frame that is no longer the one on screen.
		cancelCrop();
		return;
	}

	const QPointF docPt = mouseDocPos(m->pos());
	const ShapeCrop::Handle hit = ShapeCrop::hitTest(docPt, m_overlayRect, m_rotation,
	                                                  handleToleranceDoc(), rotateHandleOffsetDoc());
	if (hit == ShapeCrop::HandleNone)
	{
		// Photoshop-style: a click outside the crop overlay cancels rather than being swallowed.
		cancelCrop();
		return;
	}

	m_mouseDown = true;
	m_dragHandle = hit;
	m_dragStartMouseDoc = docPt;
	m_dragStartRect = m_overlayRect;
	m_dragStartRotation = m_rotation;
	if (hit == ShapeCrop::HandleRotate)
	{
		const double angleToMouse = ShapeCrop::angleFromCenter(m_overlayRect.center(), docPt);
		m_dragStartAngleOffset = angleToMouse - m_rotation;
	}
}

void CanvasMode_ShapeCrop::mouseMoveEvent(QMouseEvent* m)
{
	if (!m_hasShape)
		return;

	if (!m_mouseDown)
	{
		const QPointF docPt = mouseDocPos(m->pos());
		const ShapeCrop::Handle hit = ShapeCrop::hitTest(docPt, m_overlayRect, m_rotation,
		                                                  handleToleranceDoc(), rotateHandleOffsetDoc());
		applyCursorForHandle(hit);
		return;
	}

	const QPointF docPt = mouseDocPos(m->pos());

	if (m_dragHandle == ShapeCrop::HandleBody)
	{
		// Translation commutes with rotation about a centre that moves along with it, so the raw
		// doc-space delta applies directly - no need to undo the rotation first.
		const QPointF delta = docPt - m_dragStartMouseDoc;
		m_overlayRect = m_dragStartRect.translated(delta);
	}
	else if (m_dragHandle == ShapeCrop::HandleRotate)
	{
		const double angleToMouse = ShapeCrop::angleFromCenter(m_dragStartRect.center(), docPt);
		double rotation = angleToMouse - m_dragStartAngleOffset;
		if (rotation < 0.0)
			rotation += 360.0;
		if (rotation >= 360.0)
			rotation -= 360.0;
		m_rotation = rotation;
	}
	else
	{
		// Resize: undo the overlay's rotation (fixed for the duration of this drag) so the drag
		// vector lands in the rect's own axis-aligned space, which is what resizeRect() expects.
		const QPointF center = m_dragStartRect.center();
		const QPointF localMouse = ShapeCrop::rotatePoint(docPt, center, -m_dragStartRotation);
		const QPointF localStart = ShapeCrop::rotatePoint(m_dragStartMouseDoc, center, -m_dragStartRotation);
		const QPointF localDelta = localMouse - localStart;
		const bool lockAspect = (m->modifiers() & Qt::ShiftModifier);
		m_overlayRect = ShapeCrop::resizeRect(m_dragStartRect, m_dragHandle, localDelta, lockAspect);
	}
	m_view->updateCanvas();
}

void CanvasMode_ShapeCrop::mouseReleaseEvent(QMouseEvent*)
{
	if (!m_mouseDown)
		return;
	m_mouseDown = false;
	m_dragHandle = ShapeCrop::HandleNone;
	m_view->updateCanvas();
}

void CanvasMode_ShapeCrop::keyPressEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Escape)
	{
		cancelCrop();
		return;
	}
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
	{
		commitCrop();
		return;
	}
}

void CanvasMode_ShapeCrop::commitCrop()
{
	if (!m_hasShape)
		return;

	PageItem* item = targetItem();
	if (!item || item->itemName() != m_cropItemName)
	{
		cancelCrop();
		return;
	}
	if (m_overlayRect.width() < 2.0 || m_overlayRect.height() < 2.0)
		return; // Degenerate overlay - stay in crop mode rather than apply nonsense.

	/* Moving/resizing the frame alone would drag the image along with it, since imageXOffset and
	   imageYOffset are measured from the frame's own top-left - the same reason
	   CanvasMode_SuneerCrop's plain rectangular crop recomputes them. Compute the shift while
	   item->xPos()/yPos() still hold the *old* origin, in the frame's own local space (this
	   assumes the frame was not already rotated going in, same as that tool assumes). */
	const bool isImage = item->isImageFrame() && !item->Pfile.isEmpty();
	double newImageOffX = item->imageXOffset();
	double newImageOffY = item->imageYOffset();
	if (isImage)
	{
		const double cropX = m_overlayRect.x() - item->xPos();
		const double cropY = m_overlayRect.y() - item->yPos();
		const double scaleX = item->imageXScale();
		const double scaleY = item->imageYScale();
		if (scaleX != 0.0)
			newImageOffX = item->imageXOffset() - cropX / scaleX;
		if (scaleY != 0.0)
			newImageOffY = item->imageYOffset() - cropY / scaleY;
	}

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
	{
		transaction = undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
			tr("Crop with %1").arg(m_shapeDef.name()), QString(), Um::IBorder);
	}

	item->setXYPos(m_overlayRect.x(), m_overlayRect.y());
	item->setWidthHeight(m_overlayRect.width(), m_overlayRect.height());
	item->setRotation(m_rotation);
	if (isImage)
		item->setImageXYOffset(newImageOffX, newImageOffY);
	/* setXYPos()/setWidthHeight()/setRotation()/setImageXYOffset() only record undo history
	   through checkChanges(), and PageItem::shouldCheck() skips it while the mouse is down -
	   which by the time Enter is pressed it never is here, but forcing it is what makes the
	   move+resize+rotate+image-offset undo record unconditional, in the same transaction as the
	   shape change below. */
	item->checkChanges(true);

	const QList<double> points = m_shapeDef.values();
	m_doc->item_setFrameShape(item, m_shapeDef.frameType, points.count(), points.constData());
	if (m_shapeDef.frameType == 0)
	{
		/* Rounded corners are a radius on a rectangle, not a shape of their own - the same branch
		   ScribusMainWindow::applyFrameShape() takes, reproduced here rather than shared because
		   that function opens and commits its own, differently-scoped transaction. */
		const double radius = m_shapeDef.cornerRadiusFraction * qMin(item->width(), item->height());
		item->setCornerRadii(0.0, 0.0, 0.0, 0.0);
		item->setCornerRadius(radius);
		if (radius > 0.0)
			item->SetFrameRound();
		else
			item->SetRectFrame();
		m_doc->setRedrawBounding(item);
		item->update();
	}

	if (transaction)
		transaction.commit();

	FrameShapeMenu::noteShapeUsed(m_shapeDef.id);

	m_doc->changed();
	m_doc->changedPagePreview();
	m_doc->regionsChanged()->update(QRectF());

	if (m_ScMW)
	{
		if (m_ScMW->modeToolBar)
			m_ScMW->modeToolBar->updateFrameShapeButton();
		m_ScMW->statusBar()->showMessage(tr("Crop with %1").arg(m_shapeDef.name()), 4000);
	}

	m_hasShape = false;
	m_mouseDown = false;
	m_dragHandle = ShapeCrop::HandleNone;
	if (m_ScMW)
		m_ScMW->setAppModeByToggle(false, modeShapeCrop);
}

void CanvasMode_ShapeCrop::drawControls(QPainter* p)
{
	if (!m_hasShape)
		return;
	PageItem* item = targetItem();
	if (!item)
		return;

	const double scale = m_view->scale();
	const QPointF minCanvas = m_doc->minCanvasCoordinate.toQPointF();
	const QRectF frameDocRect(item->xPos(), item->yPos(), item->width(), item->height());
	const QRectF frameCanvasRect = ShapeCrop::docToCanvas(frameDocRect, scale, minCanvas);
	const QPointF canvasCenter = ShapeCrop::docToCanvas(m_overlayRect.center(), scale, minCanvas);
	const double canvasW = m_overlayRect.width() * scale;
	const double canvasH = m_overlayRect.height() * scale;
	const QRectF localRect(-canvasW / 2.0, -canvasH / 2.0, canvasW, canvasH);

	p->save();
	p->setRenderHint(QPainter::Antialiasing);

	// Dim the frame outside the (rotated) overlay - the same subtracted-path technique
	// CanvasMode_SuneerCrop uses for the plain rectangular crop tool.
	QTransform overlayTransform;
	overlayTransform.translate(canvasCenter.x(), canvasCenter.y());
	overlayTransform.rotate(m_rotation);
	QPainterPath outside;
	outside.addRect(frameCanvasRect);
	QPainterPath hole;
	hole.addPolygon(overlayTransform.map(QPolygonF(localRect)));
	hole.closeSubpath();
	p->fillPath(outside.subtracted(hole), QColor(0, 0, 0, 120));

	p->save();
	p->translate(canvasCenter);
	p->rotate(m_rotation);

	// The shape itself: the same percent-of-frame control points FrameShapeMenu::shapeIcon()
	// draws its menu icons from, evaluated at the overlay's current size instead of 16px.
	QPainterPath shapePath = ShapeCrop::pathFromPercentValues(m_shapeDef.iconValues(), canvasW, canvasH);
	shapePath.translate(localRect.topLeft());
	p->setPen(QPen(QColor(0, 120, 215), 2));
	p->setBrush(QColor(0, 120, 215, 60));
	p->drawPath(shapePath);

	// Bounding-box border and handles, drawn separately so a thin shape (e.g. the Cross) still
	// gets a fully grabbable frame.
	p->setBrush(Qt::NoBrush);
	p->setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
	p->drawRect(localRect);

	const double handleOffsetCanvas = 24.0;
	const QList<QPointF> handles = ShapeCrop::handlePositions(localRect, handleOffsetCanvas);
	p->setPen(QPen(Qt::black, 1));
	p->setBrush(Qt::white);
	const double hs = 7.0;
	for (int i = 0; i + 1 < handles.size(); ++i) // all but the rotation handle: square resize handles
		p->drawRect(QRectF(handles[i].x() - hs / 2.0, handles[i].y() - hs / 2.0, hs, hs));

	const QPointF rotateHandle = handles.last();
	p->setPen(QPen(QColor(0, 120, 215), 1));
	p->drawLine(QPointF(0.0, localRect.top()), rotateHandle);
	p->setPen(QPen(Qt::black, 1));
	p->setBrush(Qt::white);
	p->drawEllipse(rotateHandle, hs / 2.0, hs / 2.0);

	p->restore(); // undo translate/rotate

	p->setPen(Qt::white);
	const QPointF hintPos = ShapeCrop::docToCanvas(QPointF(m_overlayRect.left(), m_overlayRect.bottom()),
	                                               scale, minCanvas) + QPointF(4, 16);
	p->drawText(hintPos, tr("Enter: apply crop  |  Esc: cancel"));

	p->restore();
}
