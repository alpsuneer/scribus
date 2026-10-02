#include <QTimer>
/*
 For general Scribus (>=1.3.2) copyright and licensing information please refer
 to the COPYING file provided with the program. Following this notice may exist
 a copyright and/or license notice that predates the release of Scribus 1.3.2
 for which a new license (GPL+exception) is in place.
 */
/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/



#include "ui/suneercontrolbar.h"
#include <QApplication>
#include <QBuffer>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QToolTip>
#include <QDebug>

#include "canvas.h"
#include "canvasmode.h"
#include "canvasmode_imageimport.h"
#include "iconmanager.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerimagelinks.h"

CanvasMode_ImageImport::CanvasMode_ImageImport(ScribusView *view) : CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

void CanvasMode_ImageImport::setImageList(QStringList l)
{
	m_imageList = l;
	if (l.size()==1)
	{
		// Safe: check selection not empty before accessing itemAt(0)
		while (m_doc->m_Selection->isNotEmpty() && !m_doc->m_Selection->itemAt(0)->isImageFrame())
			m_doc->m_Selection->removeFirst();
		if (m_doc->m_Selection->isNotEmpty())
		{
			setImage(m_doc->m_Selection->itemAt(0));
			m_view->requestMode(submodePaintingDone);
		}
		else
			newToolTip(l.first());
	}
	else
		newToolTip(l.first());
}

void CanvasMode_ImageImport::newToolTip(const QString& name)
{
	QImage pm(80, 80, QImage::Format_ARGB32_Premultiplied);
	QBrush b(QColor(205,205,205), IconManager::instance().loadPixmap("testfill"));
	
	QPainter p;
	p.begin(&pm);
	p.fillRect(QRectF(0, 0, 80, 80), b);
	QImage thumb;
	if (thumb.load(name))
	{
		thumb = thumb.scaled(80,80,Qt::KeepAspectRatio);
		p.drawImage((80 - thumb.width()) / 2, (80 - thumb.height()) / 2, thumb);
		QBuffer buffer;
		buffer.open(QIODevice::WriteOnly);
		pm.save(&buffer, "PNG");
		QByteArray ba = buffer.buffer().toBase64();
		buffer.close();
		m_tipText = "<p align=\"center\"><img src=\"data:image/png;base64," + QString(ba) + "\"></p>";
	}
	else
		m_tipText = "<p align=\"center\">" + name.right(name.lastIndexOf("/")) + "</p>";
	QToolTip::showText(QPoint(m_Mx,m_My), m_tipText, qApp->activeWindow());
	p.end();
}

void CanvasMode_ImageImport::drawControls(QPainter* p)
{
	commonDrawControls(p, false);
}

void CanvasMode_ImageImport::enterEvent(QEvent *e)
{
	if (!m_canvas->m_viewMode.m_MouseButtonPressed)
		setModeCursor();
}

void CanvasMode_ImageImport::keyPressEvent(QKeyEvent *e)
{
	int kk = e->key();
	if (m_keyRepeat)
		return;
	m_keyRepeat = true;
	e->accept();
	switch (kk)
	{
		case Qt::Key_Tab:
			m_imageList.prepend(m_imageList.takeLast());
			break;
		case Qt::Key_Backtab:
			m_imageList.append(m_imageList.takeFirst());
			break;
		case Qt::Key_Backspace:
			m_imageList.removeFirst();
			break;
		case Qt::Key_Escape:
			m_imageList.clear();
			break;
	}
	updateList();
	m_keyRepeat = false;
}


void CanvasMode_ImageImport::leaveEvent(QEvent *e)
{
}

void CanvasMode_ImageImport::activate(bool fromGesture)
{
	CanvasMode::activate(fromGesture);

	m_canvas->m_viewMode.m_MouseButtonPressed = false;
	m_canvas->resetRenderMode();
	m_keyRepeat = false;
	m_doc->DragP = false;
	m_doc->leaveDrag = false;
	m_canvas->m_viewMode.operItemMoving = false;
	m_canvas->m_viewMode.operItemResizing = false;
	m_view->MidButt = false;
	setModeCursor();
	if (fromGesture)
		m_view->update();
}

void CanvasMode_ImageImport::deactivate(bool forGesture)
{
	m_view->setRedrawMarkerShown(false);
	CanvasMode::deactivate(forGesture);
}

void CanvasMode_ImageImport::mouseDoubleClickEvent(QMouseEvent *m)
{
	m->accept();
	m_canvas->m_viewMode.m_MouseButtonPressed = false;
	m_canvas->resetRenderMode();
}

void CanvasMode_ImageImport::mouseMoveEvent(QMouseEvent *m)
{
	m_Mx = m->globalPosition().x();
	m_My = m->globalPosition().y();
	QToolTip::showText(m->globalPosition().toPoint(), m_tipText, qApp->activeWindow());
	QToolTip::showText(m->globalPosition().toPoint(), m_tipText + "<b></b>", qApp->activeWindow());
	m->accept();
	PageItem *item;
	if ((item = m_canvas->itemUnderCursor(m->globalPosition())) != nullptr)
	{
		PageItem_ImageFrame *currItem;
		if ((currItem = item->asImageFrame()) != nullptr)
			m_view->setCursor(IconManager::instance().loadCursor("cursor-image"));
		else
			m_view->setCursor(QCursor(Qt::ArrowCursor));
	}
	else
		m_view->setCursor(IconManager::instance().loadCursor("cursor-image"));
	if (commonMouseMove(m))
		return;
}

void CanvasMode_ImageImport::mousePressEvent(QMouseEvent *m)
{
	m_canvas->PaintSizeRect(QRect());
	m_canvas->m_viewMode.m_MouseButtonPressed = true;
	m_canvas->m_viewMode.operItemMoving = false;
	m_view->HaveSelRect = false;
	m_doc->DragP = false;
	m_doc->leaveDrag = false;
	m->accept();
	m_view->registerMousePress(m->globalPosition());
	PageItem *item;
	if (!m_imageList.empty())
	{
		if ((item = m_canvas->itemUnderCursor(m->globalPosition())) != nullptr)
		{
			PageItem_ImageFrame *currItem;
			if ((currItem = item->asImageFrame()) != nullptr)
				setImage(currItem);
			else
				// Non-image frame — create new frame at this position
				view()->ImageAfterDraw = true;
		}
		else
			view()->ImageAfterDraw = true;
	}
	if (m->button() == Qt::MiddleButton)
	{
		m_view->MidButt = true;
		if (m->modifiers() & Qt::ControlModifier)
			m_view->DrawNew();
		return;
	}
}

void CanvasMode_ImageImport::mouseReleaseEvent(QMouseEvent *m)
{
	m_canvas->m_viewMode.m_MouseButtonPressed = false;
	m_canvas->resetRenderMode();
	m->accept();
	m_view->updateContents();
	m_canvas->setRenderModeUseBuffer(false);
	m_doc->DragP = false;
	m_doc->leaveDrag = false;
	m_canvas->m_viewMode.operItemMoving = false;
	m_canvas->m_viewMode.operItemResizing = false;
	m_view->MidButt = false;
	updateList();
}

void CanvasMode_ImageImport::setImage(PageItem *currItem)
{
	QString fileName = m_imageList.takeFirst();
	currItem->pixm.imgInfo.isRequest = false;
	currItem->UseEmbedded = true;
	currItem->EmbeddedProfile.clear();
	currItem->ImageProfile = m_doc->cmsSettings().DefaultImageRGBProfile;
	currItem->ImageIntent = m_doc->cmsSettings().DefaultIntentImages;
	qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
	m_doc->loadPict(fileName, currItem, false, true);
	// Create caption frame only when loading via file dialog (fileName not empty)
	QString captName = QString("caption_%1").arg(currItem->itemName());
	bool captExists = false;
	for (PageItem* pi : m_doc->DocItems) {
		if (pi->itemName() == captName) { captExists = true; break; }
	}
	if (!captExists && !fileName.isEmpty() && m_createCaption) {
		const double MM2PT = 2.8346;
		double captH = 10.0 * MM2PT;
		PageItem* captFrame = m_doc->createPageItem(
			PageItem::TextFrame, PageItem::Rectangle,
			currItem->xPos(), currItem->yPos() + currItem->height(),
			currItem->width(), captH,
			0, CommonStrings::None, CommonStrings::None);
		if (captFrame) {
			captFrame->setItemName(captName);
			captFrame->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);

			// ✅ Text distance: 1.5mm top + bottom
			const double textDist = 1.5 * MM2PT;
			captFrame->setTextToFrameDist(0.0, 0.0, textDist, textDist);

			// ✅ Caption frame border
			captFrame->setLineWidth(0.5);
			captFrame->setLineColor(CommonStrings::None);
			captFrame->setFillColor(CommonStrings::None);

			m_doc->DocItems.append(captFrame);
			// Weld caption to image frame
			currItem->weldTo(captFrame);
		}
	}
	// Update caption frame with delay (after image fit)
	{
		// QPointers: the frame or the document can be gone 200 ms from now.
		QPointer<PageItem> captItemRef(currItem);
		QPointer<ScribusDoc> docRef(m_doc);
		ScribusMainWindow* scmwRef = m_ScMW;
		QTimer::singleShot(200, [captItemRef, docRef, scmwRef]() {
			if (!captItemRef || !docRef)
				return;
			if (scmwRef && scmwRef->suneerControlBar())
				scmwRef->suneerControlBar()->updateCaptionFrame(captItemRef);
			// "Always embed placed images" (Ctrl+I, Get Image, double-click load).
			// Only now: the caption reader above looks for sidecar files next to
			// the ORIGINAL picture, and embedding points the frame at a temp copy.
			SuneerImageLinks::embedPlaced(docRef, captItemRef);
		});
	}
	// Call to showScaleAndOffset() is now very likely unnecessary
	// due to mechanisms used to update properties in PP in 1.5.x+
	// m_ScMW->contentPalette->update(currItem->isImageFrame());
	// ✅ Image frame: no border (original)
	currItem->setLineWidth(0.5);
	currItem->setLineColor(CommonStrings::None);
	currItem->setFillColor(CommonStrings::None);

	// Text wrap: Ctrl+I (m_autoWrap) places a NEW frame and sets bounding-box
	// wrap with a 3 mm gap. Every other way into this mode - double-click on
	// an empty frame, File > Import > Get Image, replacing an image - loads
	// into a frame that already exists, so the wrap mode, wrap distances and
	// contour the frame already has (from a Design Style, or set by hand)
	// are left exactly as they are. This used to force TextFlowDisabled here,
	// which is how a Design Style image frame lost its wrap on double-click.
	if (m_autoWrap) {
		const double MM2PT = 2.8346;
		const double gap = 3.0 * MM2PT;
		currItem->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
		currItem->setTextToFrameDistLeft(gap);
		currItem->setTextToFrameDistRight(gap);
		currItem->setTextToFrameDistTop(gap);
		currItem->setTextToFrameDistBottom(gap);
	}
	// The loaded image can change the frame's clip (image-clipping wrap) and
	// the frame may have been resized to it: re-lay-out the text frames that
	// wrap around this one now, not on the next click elsewhere.
	if (currItem->textFlowMode() != PageItem::TextFlowDisabled)
		currItem->checkTextFlowInteractions(true);
	m_ScMW->repaint();
	qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
	m_view->DrawNew();
	currItem->emitAllToGUI();
}

void CanvasMode_ImageImport::updateList()
{
	if (m_imageList.isEmpty())
	{
		m_view->requestMode(submodePaintingDone);
		QToolTip::hideText();
	}
	else
		newToolTip(m_imageList.constFirst());
}
