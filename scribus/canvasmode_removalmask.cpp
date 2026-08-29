/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "canvasmode_removalmask.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTransform>
#include <QtConcurrent>

#include <cmath>
#include <exception>
#include <new>

#include "canvas.h"
#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "scimageerasermask.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "ai/lamainpaintservice.h"
#include "prefsmanager.h"
#include "prefsstructs.h"
#include "ui/inpaintprogressdialog.h"
#include "undomanager.h"
#include "undotransaction.h"
#include "util_inpaint.h"

namespace
{
	//! Brush settings are shared by every removal stroke in the session and are
	//! driven by the options bar, so they live with the mode rather than with
	//! any one item. Kept apart from the eraser's so switching tools does not
	//! silently resize the other one's brush.
	int s_brushSize = 40;
	int s_brushHardness = 100;

	//! The mode instance on the canvas, for active(). Only ever written from
	//! the GUI thread, in activate() and deactivate().
	CanvasMode_RemovalMask* s_active = nullptr;

	//! Where the rebuilt files go, relative to the document or the picture.
	const QLatin1String EditsDirName(".scribus_edits");

	/*! \name Whether the AI service answered, remembered for the session

	    Testing costs a round trip and the answer almost never changes inside
	    one sitting, so it is settled once per address rather than every time
	    the options bar is redrawn - which happens on every brush stroke. The
	    address is remembered alongside the answer so that changing it in
	    Preferences asks again rather than reporting the old server's health. */
	//@{
	bool s_aiTested = false;
	bool s_aiOk = false;
	QString s_aiDetail;
	QString s_aiTestedUrl;
	//@}

	//! Longest side sent to the AI service. LaMa's quality stops improving
	//! above about this, and inference on a processor rather than a graphics
	//! card gets punishing well before it.
	constexpr int AiMaxSentEdge = 2048;

	/*! \brief Real picture kept around the mask when sending it off.

	    A model fills a hole from what surrounds it, so sending a tight crop
	    starves it of exactly what it needs. Generous compared with the
	    built-in kernel's few pixels, and bounded so that a small removal from
	    a large photograph is still a small upload. */
	int aiPadFor(const QRect& maskBounds)
	{
		const int longest = qMax(maskBounds.width(), maskBounds.height());
		return qBound(64, longest / 2, 512);
	}

	//! Overlay colour. Deliberately nothing like the eraser, which shows the
	//! erasure itself rather than a coloured wash: at a glance the two tools
	//! have to be tellable apart.
	constexpr int OverlayRed = 255;
	constexpr int OverlayGreen = 0;
	constexpr int OverlayBlue = 0;
	//! Alpha at full selection. Enough to read as a selection, sheer enough to
	//! judge what is still underneath it.
	constexpr int OverlayAlpha = 120;
}

CanvasMode_RemovalMask::CanvasMode_RemovalMask(ScribusView* view)
	: CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

CanvasMode_RemovalMask::~CanvasMode_RemovalMask()
{
	// A run still in flight when the mode is torn down: tell the worker to stop
	// and let it go. The flag is a shared_ptr held by the worker too, so it
	// stays alive to be read even though this object will not.
	if (m_cancel)
		m_cancel->store(true);
	if (m_aiService)
		m_aiService->cancel();
	// The dialog is parented to the main window, not to this mode, so it would
	// otherwise be left on screen reporting a job nobody is watching any more.
	if (m_progress)
	{
		m_progress->hide();
		m_progress->deleteLater();
		m_progress = nullptr;
	}
	if (s_active == this)
		s_active = nullptr;
}

CanvasMode_RemovalMask* CanvasMode_RemovalMask::active()
{
	return s_active;
}

int CanvasMode_RemovalMask::brushSize()
{
	return s_brushSize;
}

void CanvasMode_RemovalMask::setBrushSize(int size)
{
	s_brushSize = qBound(1, size, 500);
}

int CanvasMode_RemovalMask::brushHardness()
{
	return s_brushHardness;
}

void CanvasMode_RemovalMask::setBrushHardness(int hardness)
{
	s_brushHardness = qBound(0, hardness, 100);
}

PageItem* CanvasMode_RemovalMask::targetItem() const
{
	if (!m_doc || m_doc->m_Selection->isEmpty())
		return nullptr;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame())
		return nullptr;
	if (!item->imageIsAvailable || item->Pfile.isEmpty())
		return nullptr;
	// A PDF or EPS placement has no pixels to rebuild.
	if (!item->isRaster)
		return nullptr;
	if (item->OrigW <= 0 || item->OrigH <= 0)
		return nullptr;
	return item;
}

void CanvasMode_RemovalMask::activate(bool fromGesture)
{
	CanvasMode::activate(fromGesture);
	s_active = this;
	m_cursorValid = false;
	cancelStroke();

	if (!m_ScMW)
		return;
	m_ScMW->setRemovalToolOptionsVisible(true);
	// Settle whether the AI service is there, once per address per session, so
	// that the Best Quality button is right the first time it is looked at
	// rather than after the first disappointed press.
	refreshAiAvailability();
	notifyOptionsBar();
	if (targetItem())
		m_ScMW->statusBar()->showMessage(tr("Remove object: paint over what should go, then press Apply. Alt+drag unpaints, Shift+click continues in a straight line, [ and ] resize the brush, Esc to finish"));
	else
		m_ScMW->statusBar()->showMessage(tr("Remove object: select an image frame first"));
}

void CanvasMode_RemovalMask::deactivate(bool forGesture)
{
	CanvasMode::deactivate(forGesture);
	if (forGesture)
		return;

	if (m_painting)
		commitStroke();
	cancelStroke();

	if (m_runIsAI && m_aiService)
		m_aiService->cancel();

	// The mask is scratch state and does not outlive the mode. Leaving it
	// behind would mean a stray red wash reappearing over a frame the user has
	// since edited, with no way to tell what it was for.
	if (!m_running)
		clearMask();

	m_cursorValid = false;
	if (s_active == this)
		s_active = nullptr;
	if (m_ScMW)
	{
		m_ScMW->setRemovalToolOptionsVisible(false);
		m_ScMW->statusBar()->clearMessage();
	}
	if (m_view)
		m_view->updateCanvas();
}

void CanvasMode_RemovalMask::enterEvent(QEvent*)
{
	// The brush ring drawn in drawControls() is the real cursor; a blank one
	// keeps the arrow from sitting in the middle of it.
	QApplication::setOverrideCursor(Qt::BlankCursor);
}

void CanvasMode_RemovalMask::leaveEvent(QEvent*)
{
	QApplication::restoreOverrideCursor();
	m_cursorValid = false;
	if (m_view)
		m_view->updateCanvas();
}

void CanvasMode_RemovalMask::exitRemovalMode()
{
	cancelStroke();
	if (m_view)
		m_view->updateCanvas();
	if (m_ScMW)
		m_ScMW->setAppModeByToggle(false, modeRemoveObject);
}

bool CanvasMode_RemovalMask::hasSelection() const
{
	if (m_mask.isNull())
		return false;
	// Anything the threshold would keep counts, so the Apply button agrees
	// exactly with what applyRemoval() would actually act on.
	const int keepLimit = 255 - Inpaint::MaskThreshold;
	for (int y = 0; y < m_mask.height(); ++y)
	{
		const uchar* line = m_mask.constScanLine(y);
		for (int x = 0; x < m_mask.width(); ++x)
		{
			if (line[x] < keepLimit)
				return true;
		}
	}
	return false;
}

void CanvasMode_RemovalMask::clearMask()
{
	m_mask = QImage();
	m_overlay = QImage();
	m_maskItemName.clear();
	m_strokeEndValid = false;
	notifyOptionsBar();
	if (m_doc)
		m_doc->regionsChanged()->update(QRectF());
	if (m_canvas)
		m_canvas->update();
}

bool CanvasMode_RemovalMask::ensureMaskFor(PageItem* item)
{
	if (!item)
		return false;

	if (!m_mask.isNull() && m_maskItemName == item->itemName())
		return true;

	// Selection moved to another frame: the old mask means nothing there.
	m_mask = ScEraserMask::createFor(item->OrigW, item->OrigH);
	if (m_mask.isNull())
		return false;
	m_maskItemName = item->itemName();
	m_strokeEndValid = false;

	m_overlay = QImage(m_mask.size(), QImage::Format_ARGB32_Premultiplied);
	if (m_overlay.isNull())
	{
		m_mask = QImage();
		return false;
	}
	m_overlay.fill(Qt::transparent);
	return true;
}

bool CanvasMode_RemovalMask::canvasToMask(PageItem* item, const QPointF& canvasPos, QPointF& maskPos) const
{
	if (!item || !m_doc || !m_view || m_mask.isNull())
		return false;

	const double scale = m_view->scale();
	if (scale <= 0.0)
		return false;

	// Widget pixels -> document points. localToCanvas() is not used here: it
	// snaps to ruler divisions, which would make freehand strokes stair-step.
	QPointF docPt(canvasPos.x() / scale + m_doc->minCanvasCoordinate.x(),
	              canvasPos.y() / scale + m_doc->minCanvasCoordinate.y());

	QTransform itemXf = item->getTransform();
	bool ok = false;
	QTransform itemInv = itemXf.inverted(&ok);
	if (!ok)
		return false;
	QPointF localPt = itemInv.map(docPt);

	PageItem_ImageFrame* frame = item->asImageFrame();
	if (!frame)
		return false;
	if (item->imageXScale() == 0.0 || item->imageYScale() == 0.0)
		return false;

	QTransform maskInv = frame->maskPixelToLocal(m_mask.size()).inverted(&ok);
	if (!ok)
		return false;
	maskPos = maskInv.map(localPt);
	return true;
}

double CanvasMode_RemovalMask::maskRadiusFor(PageItem* item) const
{
	if (!item || item->OrigW <= 0 || m_mask.isNull())
		return 0.0;

	// The brush is specified in image pixels, so the same setting covers the
	// same part of the photo at any zoom.
	const double maskPerImagePx = double(m_mask.width()) / double(item->OrigW);
	return qMax(0.5, (double(s_brushSize) * 0.5) * maskPerImagePx);
}

QTransform CanvasMode_RemovalMask::maskToCanvas(PageItem* item) const
{
	QTransform t;
	PageItem_ImageFrame* frame = item ? item->asImageFrame() : nullptr;
	if (!frame || !m_view || !m_doc || m_overlay.isNull())
		return t;

	t = frame->maskPixelToLocal(m_overlay.size());
	t *= item->getTransform();

	QTransform toCanvas;
	toCanvas.scale(m_view->scale(), m_view->scale());
	toCanvas.translate(-m_doc->minCanvasCoordinate.x(), -m_doc->minCanvasCoordinate.y());
	t *= toCanvas;
	return t;
}

void CanvasMode_RemovalMask::refreshOverlay(const QRect& region)
{
	if (m_mask.isNull() || m_overlay.isNull())
		return;

	QRect area = region.isValid() ? region.intersected(m_overlay.rect()) : m_overlay.rect();
	if (area.isEmpty())
		return;

	// Premultiplied, so the overlay can be blitted straight onto the canvas
	// without Qt having to convert a whole photo-sized image every repaint.
	for (int y = area.top(); y <= area.bottom(); ++y)
	{
		const uchar* src = m_mask.constScanLine(y);
		QRgb* dst = reinterpret_cast<QRgb*>(m_overlay.scanLine(y));
		for (int x = area.left(); x <= area.right(); ++x)
		{
			const int selected = 255 - int(src[x]);
			if (selected <= 0)
			{
				dst[x] = 0;
				continue;
			}
			const int a = (OverlayAlpha * selected) / 255;
			dst[x] = qRgba((OverlayRed * a) / 255, (OverlayGreen * a) / 255, (OverlayBlue * a) / 255, a);
		}
	}
}

void CanvasMode_RemovalMask::commitStroke()
{
	if (!m_painting)
		return;
	m_painting = false;
	m_subtracting = false;
	m_dabCarry = 0.0;
	m_strokeBase = QImage();
	m_coverage = QImage();
	notifyOptionsBar();
}

void CanvasMode_RemovalMask::cancelStroke()
{
	m_painting = false;
	m_subtracting = false;
	m_dabCarry = 0.0;
	m_strokeBase = QImage();
	m_coverage = QImage();
}

void CanvasMode_RemovalMask::notifyOptionsBar()
{
	if (m_ScMW)
		m_ScMW->updateRemovalToolOptions();
}

void CanvasMode_RemovalMask::mousePressEvent(QMouseEvent* m)
{
	m->accept();

	if (m->button() != Qt::LeftButton)
		return;
	if (m_running)
		return;   // the mask is the worker's input; freeze it until it is done

	PageItem* item = targetItem();
	if (!item)
	{
		// No usable image frame selected: get out rather than silently
		// swallowing clicks on a canvas the user thinks is still live.
		exitRemovalMode();
		return;
	}

	if (!ensureMaskFor(item))
		return;

	QPointF maskPos;
	if (!canvasToMask(item, m->position(), maskPos))
		return;

	m_strokeBase = m_mask;
	m_strokeBase.detach();
	m_coverage = QImage(m_mask.size(), QImage::Format_Grayscale8);
	if (m_coverage.isNull())
	{
		cancelStroke();
		return;
	}
	m_coverage.fill(0);

	m_subtracting = (m->modifiers() & Qt::AltModifier) != 0;
	m_painting = true;
	m_dabCarry = 0.0;

	const double radius = maskRadiusFor(item);
	const double hardness = s_brushHardness / 100.0;

	QRect touched;
	if ((m->modifiers() & Qt::ShiftModifier) && m_strokeEndValid)
	{
		// Shift joins this click to where the last stroke stopped, which is how
		// every raster editor draws a straight run of brush.
		touched = ScEraserMask::stampLine(m_coverage, m_strokeEnd, maskPos, radius, hardness, m_dabCarry);
		QRect ends = ScEraserMask::stamp(m_coverage, m_strokeEnd, radius, hardness);
		touched = touched.isNull() ? ends : touched.united(ends);
	}
	else
	{
		touched = ScEraserMask::stamp(m_coverage, maskPos, radius, hardness);
	}

	m_lastMaskPos = maskPos;
	m_strokeEnd = maskPos;
	m_strokeEndValid = true;

	if (touched.isNull())
		return;

	ScEraserMask::applyStroke(m_mask, m_strokeBase, m_coverage, m_subtracting, touched);
	refreshOverlay(touched);
	if (m_canvas)
		m_canvas->update();
}

void CanvasMode_RemovalMask::mouseMoveEvent(QMouseEvent* m)
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
	if (!item || item->itemName() != m_maskItemName)
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
	m_strokeEnd = maskPos;
	if (touched.isNull())
	{
		if (m_canvas)
			m_canvas->update();
		return;
	}

	ScEraserMask::applyStroke(m_mask, m_strokeBase, m_coverage, m_subtracting, touched);
	refreshOverlay(touched);
	if (m_canvas)
		m_canvas->update();
}

void CanvasMode_RemovalMask::mouseReleaseEvent(QMouseEvent* m)
{
	m->accept();
	commitStroke();
}

void CanvasMode_RemovalMask::keyPressEvent(QKeyEvent* e)
{
	switch (e->key())
	{
	case Qt::Key_Escape:
		e->accept();
		exitRemovalMode();
		return;
	case Qt::Key_BracketLeft:
		e->accept();
		// Photoshop's brush-resize keys. Proportional steps so the control
		// stays usable across the whole 1-500 range.
		setBrushSize(s_brushSize - qMax(1, s_brushSize / 10));
		notifyOptionsBar();
		if (m_canvas)
			m_canvas->update();
		return;
	case Qt::Key_BracketRight:
		e->accept();
		setBrushSize(s_brushSize + qMax(1, s_brushSize / 10));
		notifyOptionsBar();
		if (m_canvas)
			m_canvas->update();
		return;
	default:
		break;
	}
	CanvasMode::keyPressEvent(e);
}

void CanvasMode_RemovalMask::drawControls(QPainter* p)
{
	PageItem* item = targetItem();

	if (item && !m_overlay.isNull() && item->itemName() == m_maskItemName)
	{
		p->save();
		p->setTransform(maskToCanvas(item), true);
		p->setRenderHint(QPainter::SmoothPixmapTransform, true);
		p->drawImage(0, 0, m_overlay);
		p->restore();
	}

	if (!m_cursorValid || !m_view)
		return;

	if (!item)
	{
		// enterEvent() blanks the real cursor because the brush ring stands in
		// for it. With no usable image frame there is no ring to draw, so mark
		// the pointer explicitly rather than leaving it invisible.
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
	double radius = (double(s_brushSize) * 0.5) * qAbs(item->imageXScale()) * m_view->scale();
	if (radius < 1.0)
		radius = 1.0;

	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setBrush(Qt::NoBrush);

	// Red ring to match the wash it lays down, with a dark backing so it stays
	// visible over a light photo.
	p->setPen(QPen(QColor(0, 0, 0, 200), 3));
	p->drawEllipse(m_cursorCanvasPos, radius, radius);
	const bool subtracting = m_painting ? m_subtracting
	                                    : ((QApplication::keyboardModifiers() & Qt::AltModifier) != 0);
	p->setPen(QPen(subtracting ? QColor(255, 255, 255, 235) : QColor(255, 70, 70, 245), 1));
	p->drawEllipse(m_cursorCanvasPos, radius, radius);

	if (subtracting)
	{
		// Alt unpaints; mark it so the two directions are not confused
		// mid-stroke.
		const double t = qMin(radius * 0.5, 6.0);
		p->drawLine(QPointF(m_cursorCanvasPos.x() - t, m_cursorCanvasPos.y()),
		            QPointF(m_cursorCanvasPos.x() + t, m_cursorCanvasPos.y()));
	}

	if (s_brushHardness < 100)
	{
		const double inner = radius * (double(s_brushHardness) / 100.0);
		if (inner >= 2.0)
		{
			p->setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
			p->drawEllipse(m_cursorCanvasPos, inner, inner);
		}
	}

	p->restore();
}

QImage CanvasMode_RemovalMask::thresholdedMask() const
{
	if (m_mask.isNull())
		return QImage();

	QImage binary(m_mask.size(), QImage::Format_Grayscale8);
	if (binary.isNull())
		return QImage();

	const int keepLimit = 255 - Inpaint::MaskThreshold;
	for (int y = 0; y < m_mask.height(); ++y)
	{
		const uchar* src = m_mask.constScanLine(y);
		uchar* dst = binary.scanLine(y);
		for (int x = 0; x < m_mask.width(); ++x)
			dst[x] = (src[x] < keepLimit) ? 255 : 0;
	}
	return binary;
}

QString CanvasMode_RemovalMask::outputPathFor(PageItem* item, const QString& tag, QString& error) const
{
	error.clear();
	if (!item || item->Pfile.isEmpty())
	{
		error = tr("The frame has no image file.");
		return QString();
	}

	const QFileInfo source(item->Pfile);

	// Beside the document when it has one, so the rebuilt picture travels with
	// the job; beside the picture otherwise.
	QString baseDir;
	if (m_doc && m_doc->hasName && !m_doc->documentFileName().isEmpty())
		baseDir = QFileInfo(m_doc->documentFileName()).absolutePath();
	if (baseDir.isEmpty())
		baseDir = source.absolutePath();

	QStringList candidates;
	if (!baseDir.isEmpty())
		candidates << baseDir;
	if (baseDir != source.absolutePath())
		candidates << source.absolutePath();
	candidates << QStandardPaths::writableLocation(QStandardPaths::TempLocation);

	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
	const QString stem = source.completeBaseName();

	for (const QString& dir : candidates)
	{
		if (dir.isEmpty())
			continue;
		QDir editsDir(dir + QLatin1Char('/') + EditsDirName);
		if (!editsDir.exists() && !QDir(dir).mkpath(EditsDirName))
			continue;
		if (!QFileInfo(editsDir.absolutePath()).isWritable())
			continue;

		// Two removals inside the same second must not land on one name, so a
		// counter is appended rather than the second one overwriting the first.
		for (int n = 0; n < 1000; ++n)
		{
			QString name = (n == 0)
				? QStringLiteral("%1_%2_%3.png").arg(stem, tag, stamp)
				: QStringLiteral("%1_%2_%3_%4.png").arg(stem, tag, stamp).arg(n);
			const QString full = editsDir.absoluteFilePath(name);
			if (QFileInfo::exists(full))
				continue;
			if (QFileInfo(full) == source)
				continue;   // cannot happen with this naming, but never assume it
			return full;
		}
	}

	error = tr("Could not find a writable place to save the result. Save the document somewhere writable and try again.");
	return QString();
}

double CanvasMode_RemovalMask::maskCoverage() const
{
	if (m_mask.isNull())
		return 0.0;
	const int keepLimit = 255 - Inpaint::MaskThreshold;
	qint64 selected = 0;
	for (int y = 0; y < m_mask.height(); ++y)
	{
		const uchar* line = m_mask.constScanLine(y);
		for (int x = 0; x < m_mask.width(); ++x)
		{
			if (line[x] < keepLimit)
				++selected;
		}
	}
	const qint64 total = qint64(m_mask.width()) * qint64(m_mask.height());
	return total > 0 ? double(selected) / double(total) : 0.0;
}

void CanvasMode_RemovalMask::ensureAiService()
{
	const AIServicePrefs& prefs = PrefsManager::instance().appPrefs.aiServicePrefs;
	const QString url = LamaInpaintService::normaliseBaseUrl(prefs.iopaintUrl);
	if (!m_aiService)
	{
		m_aiService = new LamaInpaintService(url, prefs.requestTimeoutSeconds, this);
		connect(m_aiService, &AIInpaintService::connectionTested,
		        this, &CanvasMode_RemovalMask::aiConnectionTested);
		connect(m_aiService, &AIInpaintService::inpaintFinished,
		        this, &CanvasMode_RemovalMask::aiInpaintFinished);
		connect(m_aiService, &AIInpaintService::inpaintFailed,
		        this, &CanvasMode_RemovalMask::aiInpaintFailed);
	}
	else
	{
		m_aiService->setEndpoint(url, prefs.requestTimeoutSeconds);
	}
}

void CanvasMode_RemovalMask::refreshAiAvailability(bool force)
{
	const AIServicePrefs& prefs = PrefsManager::instance().appPrefs.aiServicePrefs;
	if (!prefs.enabled)
		return;

	const QString url = LamaInpaintService::normaliseBaseUrl(prefs.iopaintUrl);
	if (!force && s_aiTested && s_aiTestedUrl == url)
		return;

	s_aiTestedUrl = url;
	ensureAiService();
	m_aiService->testConnection();
}

void CanvasMode_RemovalMask::aiConnectionTested(bool ok, const QString& detail)
{
	s_aiTested = true;
	s_aiOk = ok;
	s_aiDetail = detail;
	notifyOptionsBar();
}

bool CanvasMode_RemovalMask::aiReady() const
{
	return aiBlockedReason().isEmpty();
}

QString CanvasMode_RemovalMask::aiBlockedReason() const
{
	const AIServicePrefs& prefs = PrefsManager::instance().appPrefs.aiServicePrefs;
	if (!prefs.enabled)
		return tr("Enable AI features in Preferences > AI Services");

	const QString url = LamaInpaintService::normaliseBaseUrl(prefs.iopaintUrl);
	if (url.isEmpty())
		return tr("Set the IOPaint address in Preferences > AI Services");
	if (m_running)
		return tr("A removal is already running");
	if (!hasSelection())
		return tr("Paint a mask area first");
	if (!s_aiTested || s_aiTestedUrl != url)
		return tr("Checking whether IOPaint is running at %1 ...").arg(url);
	if (!s_aiOk)
		return tr("IOPaint not reachable at %1. Is it running?\n%2").arg(url, s_aiDetail);
	return QString();
}

void CanvasMode_RemovalMask::applyRemoval()
{
	if (m_running)
		return;

	PageItem* item = targetItem();
	if (!item || item->itemName() != m_maskItemName)
	{
		if (m_ScMW)
			m_ScMW->statusBar()->showMessage(tr("Select the frame the mask was painted on."), 4000);
		return;
	}
	if (!hasSelection())
		return;

	if (m_painting)
		commitStroke();

	// The picture is re-read from disk at full resolution rather than taken
	// from the frame's preview: the preview may be a low-resolution proxy, and
	// the result is going to replace the real file.
	const QString sourcePath = item->Pfile;
	QImage source(sourcePath);
	if (source.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Could not read the image file:\n%1\n\n"
		                         "Only pictures Scribus can load as pixels can be repaired this way.").arg(sourcePath));
		return;
	}

	QImage binary = thresholdedMask();
	if (binary.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("Ran out of memory preparing the mask."));
		return;
	}

	QString pathError;
	const QString outputPath = outputPathFor(item, QStringLiteral("inpaint"), pathError);
	if (outputPath.isEmpty())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), pathError);
		return;
	}

	// The user's original is never a legal destination. Checked here and again
	// after the run, because the two are separated by an unbounded wait during
	// which a lot can change.
	Q_ASSERT(outputPath != sourcePath);
	if (outputPath == sourcePath || QFileInfo(outputPath) == QFileInfo(sourcePath))
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Refusing to overwrite the original image file."));
		return;
	}

	m_cancel = std::make_shared<std::atomic<bool>>(false);
	m_percent = std::make_shared<std::atomic<int>>(0);
	m_runDoc = m_doc;
	m_runItemName = item->itemName();
	m_runSourcePath = sourcePath;
	m_runOutputPath = outputPath;
	m_running = true;
	notifyOptionsBar();

	m_progress = new InpaintProgressDialog(m_ScMW);
	m_progress->setAttribute(Qt::WA_DeleteOnClose, false);
	connect(m_progress, &InpaintProgressDialog::cancelled, this, &CanvasMode_RemovalMask::inpaintCancelled);
	m_progress->show();

	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(100);
	connect(m_pollTimer, &QTimer::timeout, this, [this]() {
		if (m_progress && m_percent)
			m_progress->setPercent(m_percent->load());
	});
	m_pollTimer->start();

	auto cancel = m_cancel;
	auto percent = m_percent;
	const int radius = Inpaint::DefaultRadius;

	m_watcher = new QFutureWatcher<Outcome>(this);
	connect(m_watcher, &QFutureWatcherBase::finished, this, &CanvasMode_RemovalMask::inpaintFinished);
	m_watcher->setFuture(QtConcurrent::run([source, binary, radius, cancel, percent]() -> Outcome {
		Outcome out;
		try
		{
			Inpaint::Options opts;
			opts.radius = radius;
			opts.cancel = cancel.get();
			opts.progress = [percent](int p) { percent->store(p); };
			out.image = Inpaint::inpaint(source, binary, opts);
			if (out.image.isNull() && !cancel->load())
				out.error = tr("The inpainter could not produce a result for this mask.");
		}
		catch (const std::bad_alloc&)
		{
			out.error = tr("Ran out of memory. Try removing a smaller area at a time.");
		}
		catch (const std::exception& e)
		{
			out.error = QString::fromLocal8Bit(e.what());
		}
		catch (...)
		{
			out.error = tr("The removal failed for an unknown reason.");
		}
		return out;
	}));
}

void CanvasMode_RemovalMask::applyRemovalAI()
{
	if (m_running)
		return;

	const QString blocked = aiBlockedReason();
	if (!blocked.isEmpty())
	{
		if (m_ScMW)
			m_ScMW->statusBar()->showMessage(blocked, 6000);
		return;
	}

	PageItem* item = targetItem();
	if (!item || item->itemName() != m_maskItemName)
	{
		if (m_ScMW)
			m_ScMW->statusBar()->showMessage(tr("Select the frame the mask was painted on."), 4000);
		return;
	}

	const QString sourcePath = item->Pfile;
	QImage source(sourcePath);
	if (source.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Could not read the image file:\n%1").arg(sourcePath));
		return;
	}
	if (source.format() != QImage::Format_RGB32 && source.format() != QImage::Format_ARGB32)
		source = source.convertToFormat(source.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);

	// The mask is painted at its own resolution; the region has to be worked
	// out in the picture's, or the rectangle sent would not line up with it.
	QImage binary = thresholdedMask();
	if (binary.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("Ran out of memory preparing the mask."));
		return;
	}
	if (binary.size() != source.size())
		binary = binary.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

	// Bounding box of what is to go, grown so the model has something to work
	// from, then clipped to the picture.
	int bx0 = source.width();
	int by0 = source.height();
	int bx1 = -1;
	int by1 = -1;
	for (int y = 0; y < binary.height(); ++y)
	{
		const uchar* line = binary.constScanLine(y);
		for (int x = 0; x < binary.width(); ++x)
		{
			if (!line[x])
				continue;
			if (x < bx0) bx0 = x;
			if (x > bx1) bx1 = x;
			if (y < by0) by0 = y;
			if (y > by1) by1 = y;
		}
	}
	if (bx1 < bx0 || by1 < by0)
		return;   // nothing selected after thresholding

	const QRect bounds(QPoint(bx0, by0), QPoint(bx1, by1));
	const int pad = aiPadFor(bounds);
	QRect roi = bounds.adjusted(-pad, -pad, pad, pad)
	                  .intersected(QRect(QPoint(0, 0), source.size()));
	if (roi.isEmpty())
		return;

	QImage roiImage = source.copy(roi);
	QImage roiMask = binary.copy(roi);

	// Sending more than this buys nothing and costs a great deal on a
	// processor. The mask is re-thresholded after scaling, because a smooth
	// resample of a yes/no selection produces neither.
	const int longest = qMax(roiImage.width(), roiImage.height());
	if (longest > AiMaxSentEdge)
	{
		const QSize sent = roiImage.size().scaled(AiMaxSentEdge, AiMaxSentEdge, Qt::KeepAspectRatio);
		roiImage = roiImage.scaled(sent, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		roiMask = roiMask.scaled(sent, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	}
	if (roiImage.isNull() || roiMask.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("Ran out of memory preparing the region."));
		return;
	}

	QString pathError;
	const QString outputPath = outputPathFor(item, QStringLiteral("lama"), pathError);
	if (outputPath.isEmpty())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), pathError);
		return;
	}

	// Same hard rule as the built-in path, checked before the work starts and
	// again before anything is written.
	Q_ASSERT(outputPath != sourcePath);
	if (outputPath == sourcePath || QFileInfo(outputPath) == QFileInfo(sourcePath))
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Refusing to overwrite the original image file."));
		return;
	}

	if (m_painting)
		commitStroke();

	m_runDoc = m_doc;
	m_runItemName = item->itemName();
	m_runSourcePath = sourcePath;
	m_runOutputPath = outputPath;
	m_runIsAI = true;
	m_runRoi = roi;
	m_runFullImage = source;
	m_runFullMask = binary;
	m_running = true;
	notifyOptionsBar();

	m_progress = new InpaintProgressDialog(m_ScMW);
	m_progress->setMessage(tr("Inpainting (Best Quality)..."));
	// The server reports no progress, so an honest bar is one that says only
	// that something is happening.
	m_progress->setIndeterminate(true);
	m_progress->setHint(10, tr("CPU inpainting can take 30-60 seconds."));
	connect(m_progress, &InpaintProgressDialog::cancelled, this, &CanvasMode_RemovalMask::inpaintCancelled);
	m_progress->show();

	ensureAiService();
	m_aiService->inpaint(roiImage, roiMask);
}

void CanvasMode_RemovalMask::aiInpaintFinished(const QImage& result)
{
	if (!m_running || !m_runIsAI)
		return;

	m_runIsAI = false;
	finishRun();

	if (result.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("The inpainting server returned no image."));
		return;
	}

	QImage roiResult = result;
	if (roiResult.size() != m_runRoi.size())
	{
		// It was scaled down on the way out; put it back where it came from.
		roiResult = roiResult.scaled(m_runRoi.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (roiResult.isNull())
		{
			QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("Ran out of memory scaling the result."));
			return;
		}
	}
	if (roiResult.format() != QImage::Format_RGB32 && roiResult.format() != QImage::Format_ARGB32)
		roiResult = roiResult.convertToFormat(QImage::Format_RGB32);

	// Only the masked pixels are taken. Everything else in the picture stays
	// exactly as it was, rather than coming back softened by the trip out to
	// 2048 pixels and home again.
	QImage composed = m_runFullImage;
	const bool wantsAlpha = composed.hasAlphaChannel();
	for (int y = 0; y < m_runRoi.height(); ++y)
	{
		const int fy = m_runRoi.top() + y;
		QRgb* dst = reinterpret_cast<QRgb*>(composed.scanLine(fy));
		const QRgb* src = reinterpret_cast<const QRgb*>(roiResult.constScanLine(y));
		const uchar* mask = m_runFullMask.constScanLine(fy);
		for (int x = 0; x < m_runRoi.width(); ++x)
		{
			const int fx = m_runRoi.left() + x;
			if (!mask[fx])
				continue;
			const QRgb px = src[x];
			dst[fx] = wantsAlpha ? qRgba(qRed(px), qGreen(px), qBlue(px), qAlpha(dst[fx]))
			                     : qRgb(qRed(px), qGreen(px), qBlue(px));
		}
	}

	m_runFullImage = QImage();
	m_runFullMask = QImage();
	deliverResult(composed, Um::RemoveObjectAI);
}

void CanvasMode_RemovalMask::aiInpaintFailed(const QString& error)
{
	if (!m_running || !m_runIsAI)
		return;

	m_runIsAI = false;
	m_runFullImage = QImage();
	m_runFullMask = QImage();
	const bool cancelled = AIInpaintService::isCancelled(error);
	finishRun();

	if (cancelled)
	{
		// The mask is kept on purpose: cancelling usually means "not like
		// that", and having to repaint the selection to try again, or to try
		// the fast path instead, would be gratuitous.
		if (m_ScMW)
			m_ScMW->statusBar()->showMessage(tr("Object removal cancelled. The mask is still there if you want to adjust it."), 6000);
		return;
	}

	// Whatever the server or the network actually said, not a summary of it.
	// The mask survives so the user can retry, or fall back to Apply (Fast).
	QMessageBox::critical(m_ScMW, tr("Remove Object (Best Quality)"), error);
	if (m_ScMW)
		m_ScMW->statusBar()->showMessage(tr("The mask is still there. You can try again, or use Apply (Fast)."), 8000);
}

void CanvasMode_RemovalMask::inpaintCancelled()
{
	if (m_cancel)
		m_cancel->store(true);
	// The AI path has no shared flag to raise: the request itself has to be
	// aborted, or the server carries on working for a minute after the user
	// has stopped waiting.
	if (m_runIsAI && m_aiService)
		m_aiService->cancel();
}

void CanvasMode_RemovalMask::finishRun()
{
	if (m_pollTimer)
	{
		m_pollTimer->stop();
		m_pollTimer->deleteLater();
		m_pollTimer = nullptr;
	}
	if (m_progress)
	{
		m_progress->hide();
		m_progress->deleteLater();
		m_progress = nullptr;
	}
	if (m_watcher)
	{
		m_watcher->deleteLater();
		m_watcher = nullptr;
	}
	m_running = false;
	m_percent.reset();
	notifyOptionsBar();
}

void CanvasMode_RemovalMask::inpaintFinished()
{
	if (!m_watcher)
		return;

	const Outcome outcome = m_watcher->result();
	const bool wasCancelled = m_cancel && m_cancel->load();

	m_cancel.reset();
	finishRun();

	if (wasCancelled)
	{
		// The mask is deliberately kept: cancelling usually means "not like
		// that", and repainting the whole selection to try again would be
		// gratuitous.
		if (m_ScMW)
			m_ScMW->statusBar()->showMessage(tr("Object removal cancelled. The mask is still there if you want to adjust it."), 6000);
		return;
	}

	if (!outcome.error.isEmpty())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), outcome.error);
		return;
	}
	if (outcome.image.isNull())
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"), tr("The removal produced no image."));
		return;
	}

	deliverResult(outcome.image, Um::RemoveObject);
}

/*! The careful half of both paths. Everything between here and the end of the
    function is the same whether the pixels came from the built-in kernel or
    from a model on the other end of a socket. */
void CanvasMode_RemovalMask::deliverResult(const QImage& image, const QString& undoName)
{
	const QString outputPath = m_runOutputPath;
	const QString sourcePath = m_runSourcePath;
	const QString itemName = m_runItemName;
	QPointer<ScribusDoc> runDoc = m_runDoc;

	// The dialog was modeless, so nothing stopped the user from closing the
	// document or replacing the picture while it ran. Put the result somewhere
	// only if it is still the thing that was asked for.
	if (runDoc.isNull() || runDoc != m_doc)
	{
		QMessageBox::warning(m_ScMW, tr("Remove Object"),
		                     tr("The document changed while the object was being removed, so the result was discarded."));
		return;
	}
	PageItem* item = runDoc->getItemFromName(itemName);
	if (!item || !item->isImageFrame())
	{
		QMessageBox::warning(m_ScMW, tr("Remove Object"),
		                     tr("The image frame is gone, so the result was discarded."));
		return;
	}
	if (item->Pfile != sourcePath)
	{
		QMessageBox::warning(m_ScMW, tr("Remove Object"),
		                     tr("The frame's image changed while the object was being removed, so the result was discarded."));
		return;
	}

	Q_ASSERT(outputPath != item->Pfile);
	if (outputPath == item->Pfile || QFileInfo(outputPath) == QFileInfo(item->Pfile))
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Refusing to overwrite the original image file."));
		return;
	}

	QImage toSave = image;
	// Carry the source's resolution across. Scribus derives the image scale
	// from the file's DPI, so a PNG saved at Qt's default would re-enter the
	// frame at a different size and the picture would jump.
	if (item->pixm.imgInfo.xres > 0 && item->pixm.imgInfo.yres > 0)
	{
		toSave.setDotsPerMeterX(int(double(item->pixm.imgInfo.xres) / 0.0254 + 0.5));
		toSave.setDotsPerMeterY(int(double(item->pixm.imgInfo.yres) / 0.0254 + 0.5));
	}

	QDir().mkpath(QFileInfo(outputPath).absolutePath());
	if (!toSave.save(outputPath, "PNG"))
	{
		QMessageBox::critical(m_ScMW, tr("Remove Object"),
		                      tr("Could not write the result to:\n%1").arg(outputPath));
		return;
	}

	// What the frame looked like before, so the picture stays where the user
	// put it. loadImage() resets scale and offset whenever the file name
	// changes, which for a hand-placed picture would shift it in the frame.
	const double xScale = item->imageXScale();
	const double yScale = item->imageYScale();
	const double xOffset = item->imageXOffset();
	const double yOffset = item->imageYOffset();
	const bool freeScaled = !item->ScaleType;

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
	{
		transaction = UndoManager::instance()->beginTransaction(
			item->getUName(), item->getUPixmap(), undoName,
			QFileInfo(outputPath).fileName(), Um::IImageFrame);
	}

	// Goes through the ordinary image-load path, which records its own undo
	// state carrying the old file name: undo puts the original picture back.
	item->loadImage(outputPath, false);

	if (freeScaled)
	{
		// These setters record their own undo steps inside the transaction, so
		// redo puts the placement back too rather than only undo.
		item->setImageXScale(xScale);
		item->setImageYScale(yScale);
		item->setImageXOffset(xOffset);
		item->setImageYOffset(yOffset);
	}

	if (transaction)
		transaction.commit();

	runDoc->changed();
	item->update();
	runDoc->regionsChanged()->update(QRectF());

	clearMask();
	if (m_ScMW)
		m_ScMW->statusBar()->showMessage(tr("Object removed. The result was saved as %1; your original image is untouched.")
		                                 .arg(QFileInfo(outputPath).fileName()), 8000);
	exitRemovalMode();
}
