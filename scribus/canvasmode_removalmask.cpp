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

QString CanvasMode_RemovalMask::outputPathFor(PageItem* item, QString& error) const
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
				? QStringLiteral("%1_inpaint_%2.png").arg(stem, stamp)
				: QStringLiteral("%1_inpaint_%2_%3.png").arg(stem, stamp).arg(n);
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
	const QString outputPath = outputPathFor(item, pathError);
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

void CanvasMode_RemovalMask::inpaintCancelled()
{
	if (m_cancel)
		m_cancel->store(true);
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
	const QString outputPath = m_runOutputPath;
	const QString sourcePath = m_runSourcePath;
	const QString itemName = m_runItemName;
	QPointer<ScribusDoc> runDoc = m_runDoc;

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

	QImage toSave = outcome.image;
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
			item->getUName(), item->getUPixmap(), Um::RemoveObject,
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
