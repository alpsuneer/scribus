/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "canvasmode_imageeraser.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStatusBar>
#include <QTransform>

#include <cmath>

#include "canvas.h"
#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "prefsmanager.h"
#include "scimageerasermask.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "undomanager.h"
#include "undostate.h"

namespace
{
	//! Brush settings are shared by every eraser stroke in the session and are
	//! driven by the options bar, so they live with the mode rather than with
	//! any one item.
	int s_brushSize = 60;
	int s_brushHardness = 65;
}

int CanvasMode_ImageEraser::brushSize()
{
	return s_brushSize;
}

void CanvasMode_ImageEraser::setBrushSize(int size)
{
	s_brushSize = qBound(1, size, 500);
}

int CanvasMode_ImageEraser::brushHardness()
{
	return s_brushHardness;
}

void CanvasMode_ImageEraser::setBrushHardness(int hardness)
{
	s_brushHardness = qBound(0, hardness, 100);
}

CanvasMode_ImageEraser::CanvasMode_ImageEraser(ScribusView* view)
	: CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

PageItem* CanvasMode_ImageEraser::targetItem() const
{
	if (!m_doc || m_doc->m_Selection->isEmpty())
		return nullptr;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame())
		return nullptr;
	if (!item->imageIsAvailable || item->Pfile.isEmpty())
		return nullptr;
	if (item->OrigW <= 0 || item->OrigH <= 0)
		return nullptr;
	return item;
}

void CanvasMode_ImageEraser::activate(bool fromGesture)
{
	CanvasMode::activate(fromGesture);
	m_cursorValid = false;
	cancelStroke();

	if (!m_ScMW)
		return;
	m_ScMW->setImageEraserOptionsVisible(true);
	if (targetItem())
		m_ScMW->statusBar()->showMessage(tr("Image eraser: drag to erase, Alt+drag to restore, [ and ] resize the brush, Esc to finish"));
	else
		m_ScMW->statusBar()->showMessage(tr("Image eraser: select an image frame first"));
}

void CanvasMode_ImageEraser::deactivate(bool forGesture)
{
	CanvasMode::deactivate(forGesture);
	if (forGesture)
		return;

	// A stroke still in flight when the mode is torn down would otherwise
	// leave the item showing a preview mask that was never committed.
	if (m_painting)
	{
		PageItem* item = targetItem();
		if (item && item->itemName() == m_strokeItemName)
			commitStroke(item);
		else
			cancelStroke();
	}
	cancelStroke();

	m_cursorValid = false;
	if (m_ScMW)
	{
		m_ScMW->setImageEraserOptionsVisible(false);
		m_ScMW->statusBar()->clearMessage();
	}
	if (m_view)
		m_view->updateCanvas();
}

void CanvasMode_ImageEraser::enterEvent(QEvent*)
{
	// The brush ring drawn in drawControls() is the real cursor; a blank one
	// keeps the arrow from sitting in the middle of it.
	QApplication::setOverrideCursor(Qt::BlankCursor);
}

void CanvasMode_ImageEraser::leaveEvent(QEvent*)
{
	QApplication::restoreOverrideCursor();
	m_cursorValid = false;
	if (m_view)
		m_view->updateCanvas();
}

void CanvasMode_ImageEraser::exitEraserMode()
{
	cancelStroke();
	if (m_view)
		m_view->updateCanvas();
	if (m_ScMW)
		m_ScMW->setAppModeByToggle(false, modeImageEraser);
}

bool CanvasMode_ImageEraser::canvasToMask(PageItem* item, const QPointF& canvasPos, QPointF& maskPos) const
{
	if (!item || !m_doc || !m_view)
		return false;

	const double scale = m_view->scale();
	if (scale <= 0.0)
		return false;

	// Widget pixels -> document points. localToCanvas() is not used here: it
	// snaps to ruler divisions, which would make freehand strokes stair-step.
	QPointF docPt(canvasPos.x() / scale + m_doc->minCanvasCoordinate.x(),
	              canvasPos.y() / scale + m_doc->minCanvasCoordinate.y());

	// Document points -> item-local points (undoes item position and rotation).
	QTransform itemXf = item->getTransform();
	bool ok = false;
	QTransform itemInv = itemXf.inverted(&ok);
	if (!ok)
		return false;
	QPointF localPt = itemInv.map(docPt);

	// Item-local points -> original image pixels. This mirrors the transform
	// chain in PageItem_ImageFrame::DrawObj_Item exactly, minus the low-res
	// proxy scale, so the result is in the full-resolution pixel space that
	// OrigW/OrigH describe.
	QTransform imgXf;
	if (item->imageFlippedH())
	{
		imgXf.translate(item->width(), 0);
		imgXf.scale(-1, 1);
	}
	if (item->imageFlippedV())
	{
		imgXf.translate(0, item->height());
		imgXf.scale(1, -1);
	}
	imgXf.translate(item->imageXOffset() * item->imageXScale(),
	                item->imageYOffset() * item->imageYScale());
	imgXf.rotate(item->imageRotation());
	if (item->imageXScale() == 0.0 || item->imageYScale() == 0.0)
		return false;
	imgXf.scale(item->imageXScale(), item->imageYScale());

	QTransform imgInv = imgXf.inverted(&ok);
	if (!ok)
		return false;
	QPointF imgPx = imgInv.map(localPt);

	// Original image pixels -> mask pixels (the mask is capped at MaxEdge).
	QImage mask = m_previewMask.isNull() ? ScEraserMask::maskOf(item->effectsInUse) : m_previewMask;
	int maskW = mask.isNull() ? 0 : mask.width();
	int maskH = mask.isNull() ? 0 : mask.height();
	if (maskW <= 0 || maskH <= 0)
		return false;

	maskPos = QPointF(imgPx.x() * double(maskW) / double(item->OrigW),
	                  imgPx.y() * double(maskH) / double(item->OrigH));
	return true;
}

double CanvasMode_ImageEraser::maskRadiusFor(PageItem* item) const
{
	if (!item || item->OrigW <= 0)
		return 0.0;

	int maskW = m_previewMask.isNull() ? 0 : m_previewMask.width();
	if (maskW <= 0)
	{
		QImage stored = ScEraserMask::maskOf(item->effectsInUse);
		maskW = stored.isNull() ? 0 : stored.width();
	}
	if (maskW <= 0)
		return 0.0;

	// The brush is specified in image pixels, so the same setting covers the
	// same part of the photo at any zoom.
	double maskPerImagePx = double(maskW) / double(item->OrigW);
	return qMax(0.5, (double(s_brushSize) * 0.5) * maskPerImagePx);
}

void CanvasMode_ImageEraser::refreshPreview(PageItem* item, const QRect& maskRegion)
{
	if (!item)
		return;

	// Recomputed from the stroke-start mask every time. The coverage buffer is
	// never cleared mid-stroke: it accumulates with max() and the difference is
	// applied once, so overlapping dab feathers cannot stack up and scallop the
	// edge at each mouse-move boundary.
	ScEraserMask::applyStroke(m_previewMask, m_baseMask, m_coverage, m_restoring, maskRegion);

	PageItem_ImageFrame* frame = item->asImageFrame();
	if (frame)
		frame->setLiveEraserMask(m_previewMask);

	item->update();
	m_doc->regionsChanged()->update(QRectF());
}

void CanvasMode_ImageEraser::commitStroke(PageItem* item)
{
	if (!m_painting || !item)
	{
		cancelStroke();
		return;
	}

	// Dropping the live preview also invalidates the draw composite, and the
	// composite key is derived from the stored parameters, so assigning
	// effectsInUse below is enough to get it rebuilt. Going through
	// setEraserMask() here would encode the mask to base64 a second time.
	PageItem_ImageFrame* frame = item->asImageFrame();
	if (frame)
		frame->clearLiveEraserMask();

	// Nothing actually changed (a click that missed the image, say): leave the
	// document alone rather than pushing an empty undo step.
	ScImageEffectList after = m_effectsBefore;
	ScEraserMask::setMask(after, m_previewMask);
	if (after == m_effectsBefore)
	{
		cancelStroke();
		if (m_view)
			m_view->updateCanvas();
		return;
	}

	item->effectsInUse = after;

	if (UndoManager::undoEnabled())
	{
		// Reuses the existing image-effects undo state: it swaps the whole
		// effect list, which is exactly what an eraser stroke changes, and
		// PageItem::restoreImageEffects already knows how to replay it.
		auto* state = new ScOldNewState<ScImageEffectList>(
			m_restoring ? Um::RestoreImageArea : Um::EraseImageArea, "", item->getUPixmap());
		state->set("APPLY_IMAGE_EFFECTS");
		state->setStates(m_effectsBefore, after);
		UndoManager::instance()->action(item, state);
	}

	m_doc->changed();
	item->update();
	m_doc->regionsChanged()->update(QRectF());
	cancelStroke();
}

void CanvasMode_ImageEraser::cancelStroke()
{
	m_painting = false;
	m_restoring = false;
	m_dabCarry = 0.0;
	m_strokeItemName.clear();
	m_effectsBefore.clear();
	m_baseMask = QImage();
	m_coverage = QImage();
	m_previewMask = QImage();
	m_strokeRegion = QRect();
}

void CanvasMode_ImageEraser::mousePressEvent(QMouseEvent* m)
{
	m->accept();

	if (m->button() != Qt::LeftButton)
		return;

	PageItem* item = targetItem();
	if (!item)
	{
		// No usable image frame selected: get out rather than silently
		// swallowing clicks on a canvas the user thinks is still live.
		exitEraserMode();
		return;
	}

	m_strokeItemName = item->itemName();
	m_effectsBefore = item->effectsInUse;
	m_restoring = (m->modifiers() & Qt::AltModifier) != 0;

	m_baseMask = ScEraserMask::maskOf(item->effectsInUse);
	if (m_baseMask.isNull())
		m_baseMask = ScEraserMask::createFor(item->OrigW, item->OrigH);
	if (m_baseMask.isNull())
		return;

	m_previewMask = m_baseMask;
	m_previewMask.detach();
	m_coverage = QImage(m_previewMask.size(), QImage::Format_Grayscale8);
	if (m_coverage.isNull())
		return;
	m_coverage.fill(0);
	m_strokeRegion = QRect();

	QPointF maskPos;
	if (!canvasToMask(item, m->position(), maskPos))
	{
		cancelStroke();
		return;
	}

	m_painting = true;
	m_lastMaskPos = maskPos;
	m_dabCarry = 0.0;

	QRect touched = ScEraserMask::stamp(m_coverage, maskPos, maskRadiusFor(item), s_brushHardness / 100.0);
	if (!touched.isNull())
	{
		m_strokeRegion = touched;
		refreshPreview(item, touched);
	}
}

void CanvasMode_ImageEraser::mouseMoveEvent(QMouseEvent* m)
{
	m->accept();
	m_cursorCanvasPos = m->position();
	m_cursorValid = true;

	if (!m_painting)
	{
		// Keep the brush ring following the pointer even when not painting.
		// drawControls() runs from Canvas::paintEvent, so the canvas is what
		// has to be repainted - updating the view widget would not reach it.
		if (m_canvas)
			m_canvas->update();
		return;
	}

	PageItem* item = targetItem();
	if (!item || item->itemName() != m_strokeItemName)
	{
		cancelStroke();
		return;
	}

	QPointF maskPos;
	if (!canvasToMask(item, m->position(), maskPos))
		return;

	QRect touched = ScEraserMask::stampLine(m_coverage, m_lastMaskPos, maskPos,
	                                        maskRadiusFor(item), s_brushHardness / 100.0,
	                                        m_dabCarry);
	m_lastMaskPos = maskPos;
	if (touched.isNull())
		return;

	m_strokeRegion = m_strokeRegion.isNull() ? touched : m_strokeRegion.united(touched);
	refreshPreview(item, touched);
}

void CanvasMode_ImageEraser::mouseReleaseEvent(QMouseEvent* m)
{
	m->accept();
	if (!m_painting)
		return;

	PageItem* item = targetItem();
	if (item && item->itemName() == m_strokeItemName)
		commitStroke(item);
	else
		cancelStroke();
}

void CanvasMode_ImageEraser::keyPressEvent(QKeyEvent* e)
{
	switch (e->key())
	{
	case Qt::Key_Escape:
		e->accept();
		exitEraserMode();
		return;
	case Qt::Key_BracketLeft:
		e->accept();
		// Photoshop's brush-resize keys. Proportional steps so the control
		// stays usable across the whole 1-500 range.
		setBrushSize(s_brushSize - qMax(1, s_brushSize / 10));
		if (m_ScMW)
			m_ScMW->updateImageEraserOptions();
		if (m_canvas)
			m_canvas->update();
		return;
	case Qt::Key_BracketRight:
		e->accept();
		setBrushSize(s_brushSize + qMax(1, s_brushSize / 10));
		if (m_ScMW)
			m_ScMW->updateImageEraserOptions();
		if (m_canvas)
			m_canvas->update();
		return;
	default:
		break;
	}
	CanvasMode::keyPressEvent(e);
}

void CanvasMode_ImageEraser::drawControls(QPainter* p)
{
	if (!m_cursorValid || !m_view)
		return;

	PageItem* item = targetItem();
	if (!item)
	{
		// enterEvent() blanks the real cursor because the brush ring stands in
		// for it. With no usable image frame there is no ring to draw, so mark
		// the pointer explicitly rather than leaving it invisible - and say why
		// the tool is inert.
		p->save();
		p->setRenderHint(QPainter::Antialiasing);
		p->setBrush(Qt::NoBrush);
		p->setPen(QPen(QColor(0, 0, 0, 200), 3));
		p->drawEllipse(m_cursorCanvasPos, 7.0, 7.0);
		p->setPen(QPen(QColor(255, 255, 255, 230), 1));
		p->drawEllipse(m_cursorCanvasPos, 7.0, 7.0);
		p->drawLine(m_cursorCanvasPos + QPointF(-5, -5), m_cursorCanvasPos + QPointF(5, 5));
		p->restore();
		return;
	}

	// Ring radius in canvas pixels: brush size is in image pixels, so it has to
	// go through the image scale and the view zoom to be drawn.
	double radiusPts = (double(s_brushSize) * 0.5) * qAbs(item->imageXScale());
	double radius = radiusPts * m_view->scale();
	if (radius < 1.0)
		radius = 1.0;

	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setBrush(Qt::NoBrush);

	// Two-tone ring so it stays visible over both light and dark photos.
	p->setPen(QPen(QColor(0, 0, 0, 200), 3));
	p->drawEllipse(m_cursorCanvasPos, radius, radius);
	p->setPen(QPen(QColor(255, 255, 255, 230), 1));
	p->drawEllipse(m_cursorCanvasPos, radius, radius);

	// Alt turns the eraser into a restore brush; mark it so the two are not
	// confused mid-stroke.
	bool restoring = m_painting ? m_restoring
	                            : ((QApplication::keyboardModifiers() & Qt::AltModifier) != 0);
	if (restoring)
	{
		p->setPen(QPen(QColor(255, 255, 255, 230), 1));
		double t = qMin(radius * 0.5, 6.0);
		p->drawLine(QPointF(m_cursorCanvasPos.x() - t, m_cursorCanvasPos.y()),
		            QPointF(m_cursorCanvasPos.x() + t, m_cursorCanvasPos.y()));
		p->drawLine(QPointF(m_cursorCanvasPos.x(), m_cursorCanvasPos.y() - t),
		            QPointF(m_cursorCanvasPos.x(), m_cursorCanvasPos.y() + t));
	}

	// A soft brush has no visible edge at the ring, so show where the solid
	// core ends too.
	if (s_brushHardness < 100)
	{
		double inner = radius * (double(s_brushHardness) / 100.0);
		if (inner >= 2.0)
		{
			p->setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
			p->drawEllipse(m_cursorCanvasPos, inner, inner);
		}
	}

	p->restore();
}
