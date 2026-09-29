#include "canvasmode_suneercrop.h"
#include "canvas.h"
#include "scribusview.h"
#include "scribus.h"
#include "ui/suneercontrolbar.h"
#include "scribusview.h"
#include "scribusdoc.h"
#include "selection.h"
#include "pageitem.h"
#include "util.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QCursor>
#include <QProcess>
#include <QScrollBar>
#include <QImage>
#include <QFileInfo>
#include <QApplication>
#include <QStatusBar>

CanvasMode_SuneerCrop::CanvasMode_SuneerCrop(ScribusView* view)
    : CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

CanvasMode_SuneerCrop::~CanvasMode_SuneerCrop()
{
    // Safety net: if this mode is torn down while still holding the cross
    // override cursor (leaveEvent never fired), pop it rather than leaving
    // it on QApplication's stack for good.
    if (m_cursorOverridden)
    {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}

PageItem* CanvasMode_SuneerCrop::croppedItem() const
{
    if (!m_doc || m_doc->m_Selection->isEmpty())
        return nullptr;
    return m_doc->m_Selection->itemAt(0);
}

void CanvasMode_SuneerCrop::activate(bool fromGesture)
{
    CanvasMode::activate(fromGesture);
    // Crop mode swallows the normal canvas gestures (frame resize included), so
    // it must announce itself and say how to get out — otherwise a stray click
    // on the toolbar's crop button leaves the canvas silently "dead".
    PageItem* item = croppedItem();
    m_cropItemName = item ? item->itemName() : QString();
    if (m_ScMW)
    {
        m_ScMW->statusBar()->showMessage(tr("Crop mode: drag a crop area — Enter to apply, Esc to cancel"));
        if (m_ScMW->suneerControlBar())
            m_ScMW->suneerControlBar()->setCropModeActive(true);
    }
}

void CanvasMode_SuneerCrop::deactivate(bool forGesture)
{
    CanvasMode::deactivate(forGesture);
    if (forGesture)
        return;
    m_drawing = false;
    m_hasCrop = false;
    m_cropRect = QRectF();
    m_cropItemName.clear();
    // Real deactivation (not a temporary gesture pause): the mouse may still
    // be sitting over the canvas, in which case leaveEvent() never fires and
    // the cross cursor enterEvent() pushed would otherwise never come off
    // QApplication's stack. See the flag's comment in the header. This also
    // covers the Enter-to-apply and Escape-to-cancel paths in
    // keyPressEvent(), which used to restore this by hand only on the Enter
    // branch.
    if (m_cursorOverridden)
    {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    if (m_ScMW)
    {
        m_ScMW->statusBar()->clearMessage();
        if (m_ScMW->suneerControlBar())
            m_ScMW->suneerControlBar()->setCropModeActive(false);
    }
}

void CanvasMode_SuneerCrop::exitCropMode()
{
    m_drawing = false;
    m_hasCrop = false;
    m_cropRect = QRectF();
    if (m_view)
        m_view->updateCanvas();
    if (m_ScMW)
        m_ScMW->setAppModeByToggle(false, modeSuneerImageCrop);
}

void CanvasMode_SuneerCrop::enterEvent(QEvent*)
{
    // Guarded so a second enterEvent before a matching leave never pushes
    // two overrides for one pop.
    if (!m_cursorOverridden)
    {
        QApplication::setOverrideCursor(Qt::CrossCursor);
        m_cursorOverridden = true;
    }
}

void CanvasMode_SuneerCrop::leaveEvent(QEvent*)
{
    if (m_cursorOverridden)
    {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}

void CanvasMode_SuneerCrop::mousePressEvent(QMouseEvent* m)
{
    m->accept();
    // The frame we were cropping is gone or another one was selected (e.g. via
    // the Outline palette): drop back to normal editing rather than silently
    // swallowing every drag on a frame the user is no longer cropping.
    PageItem* item = croppedItem();
    const QString nowName = item ? item->itemName() : QString();
    if (nowName != m_cropItemName)
    {
        exitCropMode();
        return;
    }
    m_startPoint = m->pos();
    m_endPoint   = m->pos();
    m_drawing    = true;
    m_hasCrop    = false;
    m_cropRect   = QRectF();
}

void CanvasMode_SuneerCrop::mouseMoveEvent(QMouseEvent* m)
{
    if (!m_drawing) return;
    m_endPoint = m->pos();
    m_view->updateCanvas();
}

void CanvasMode_SuneerCrop::mouseReleaseEvent(QMouseEvent* m)
{
    if (!m_drawing) return;
    m_drawing  = false;
    m_endPoint = m->pos();

    QRectF rect = QRectF(m_startPoint, m_endPoint).normalized();
    // Add scroll offset to get correct doc position
    double viewScale = m_view->scale();
    double scrollX = m_view->contentsX();
    double scrollY = m_view->contentsY();
    // Get selected item position
    double itemDocX = 0, itemDocY = 0;
    if (!m_doc->m_Selection->isEmpty()) {
        PageItem* selItem = m_doc->m_Selection->itemAt(0);
        if (selItem) {
            itemDocX = selItem->xPos();
            itemDocY = selItem->yPos();
        }
    }
    double minX = m_doc->minCanvasCoordinate.x();
    double minY = m_doc->minCanvasCoordinate.y();
    // viewport → doc: no scroll needed (m->pos() is canvas coords)
    double docX1 = rect.x() / viewScale + minX;
    double docY1 = rect.y() / viewScale + minY;
    double docX2 = rect.right() / viewScale + minX;
    double docY2 = rect.bottom() / viewScale + minY;
    m_cropRect = QRectF(QPointF(docX1, docY1), QPointF(docX2, docY2)).normalized();

    if (m_cropRect.width() < 5 || m_cropRect.height() < 5) {
        m_hasCrop = false;
        m_view->updateCanvas();
        return;
    }

    m_hasCrop = true;
    m_view->updateCanvas();
    // ഇവിടെ crop apply ആകില്ല — Enter key വരേക്കും കാത്തിരിക്കൂ
}

void CanvasMode_SuneerCrop::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        // Cancel — crop area clear ചെയ്യൂ
        exitCropMode();
        return;
    }

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!m_hasCrop) return;
        // Apply crop
        if (m_doc->m_Selection->isEmpty()) return;
        PageItem* item = m_doc->m_Selection->itemAt(0);
        if (!item || !item->isImageFrame()) return;
        // Check crop rect intersects with item bounds
        QRectF itemBounds(item->xPos(), item->yPos(), item->width(), item->height());
        if (!m_cropRect.intersects(itemBounds)) {
            m_hasCrop = false;
            m_cropRect = QRectF();
            m_view->updateCanvas();
            return;
        }
        // Clamp crop rect to item bounds
        m_cropRect = m_cropRect.intersected(itemBounds);
        // ✅ CORRECT: m->pos() = canvas pixels, doc = canvas/scale
        // item->xPos() = absolute doc coords (already includes page offset)
        // m_cropRect is already in doc coords (set in mouseReleaseEvent as pos/scale)
        double imgOffsetX = item->imageXOffset();
        double imgOffsetY = item->imageYOffset();

        double cropX = m_cropRect.x() - item->xPos() - imgOffsetX;
        double cropY = m_cropRect.y() - item->yPos() - imgOffsetY;
        double cropW = m_cropRect.width();
        double cropH = m_cropRect.height();


        if (cropW < 5 || cropH < 5) return;
        // Ensure crop is within image frame bounds
        if (m_cropRect.x() > item->xPos() + item->width() ||
            m_cropRect.y() > item->yPos() + item->height() ||
            m_cropRect.x() + m_cropRect.width() < item->xPos() ||
            m_cropRect.y() + m_cropRect.height() < item->yPos())
            return;

        double origScaleX = item->imageXScale();
        double origScaleY = item->imageYScale();
        double origOffX   = item->imageXOffset();
        double origOffY   = item->imageYOffset();

        double newOffX = origOffX - cropX / origScaleX;
        double newOffY = origOffY - cropY / origScaleY;
        item->setWidth(cropW);
        item->setHeight(cropH);
        item->setImageXYScale(origScaleX, origScaleY);
        item->setImageXYOffset(newOffX, newOffY);
        item->updateClip();
        item->update();
        m_doc->changed();
        m_doc->regionsChanged()->update(QRectF());

        // Crop the source file and relink. Done with Qt, not PIL: the old python
        // path hardcoded dpi=(72,72) on both save() calls, so a 300 dpi photo
        // always came back as 72, and its non-alpha branch used convert('RGB'),
        // which composites transparency onto black.
        SuneerControlBar* cb = m_ScMW->suneerControlBar();
        if (!item->Pfile.isEmpty()) {
            bool fixedSize = cb && cb->isCropResizeEnabled();
            double targetWmm = fixedSize ? cb->imgCropW() : (cropW / 2.8346);
            double targetHmm = fixedSize ? cb->imgCropH() : (cropH / 2.8346);
            QString inputPath = item->Pfile;
            QImage sourceImg(inputPath);
            if (targetWmm > 1 && targetHmm > 1 && !sourceImg.isNull()) {
                // Always PNG. Qt picks the writer from the file extension, so a
                // .jpg path writes JPEG no matter what format string save() is
                // handed — and JPEG carries neither alpha nor a dependable DPI.
                QString outputPath = derivedImagePath(inputPath, "_crop", "png");

                // cropX is frame-relative pts, origOffX is image offset in pts
                // cropX in points, origScaleX in pt/px
                int cx = qMax(0, qRound(cropX / origScaleX));
                int cy = qMax(0, qRound(cropY / origScaleY));
                int cw = qMax(1, qRound(cropW / origScaleX));
                int ch = qMax(1, qRound(cropH / origScaleY));
                // Clamp to image bounds
                // ✅ Actual image dimensions (imgInfo = real pixels)
                // pixm.width/height() = display-scaled preview (WRONG for clamping!)
                int imgW = item->OrigW;
                int imgH = item->OrigH;
                // Fallback if imgInfo not set
                if (imgW <= 0) imgW = item->pixm.width();
                if (imgH <= 0) imgH = item->pixm.height();
                cx = qMin(cx, imgW - 1);
                cy = qMin(cy, imgH - 1);
                cw = qMin(cw, imgW - cx);
                ch = qMin(ch, imgH - cy);
                // Resample at the source resolution, not at a fixed 72. The
                // target is a physical size in mm, so the pixel count has to be
                // derived from the dpi we mean to keep — otherwise "300 dpi"
                // would just be a metadata label on a 72 dpi raster.
                double srcDpiX = item->pixm.imgInfo.xres > 0 ? double(item->pixm.imgInfo.xres) : 72.0;
                double srcDpiY = item->pixm.imgInfo.yres > 0 ? double(item->pixm.imgInfo.yres) : 72.0;
                int rw = qMax(1, qRound(targetWmm / 25.4 * srcDpiX));
                int rh = qMax(1, qRound(targetHmm / 25.4 * srcDpiY));

                QImage cropped = sourceImg.copy(cx, cy, cw, ch);
                // Background Remove leaves an alpha channel; keep 8-bit alpha
                // through the resample instead of letting it fall to RGB32.
                if (sourceImg.hasAlphaChannel() && cropped.format() != QImage::Format_ARGB32)
                    cropped = cropped.convertToFormat(QImage::Format_ARGB32);

                QImage out = cropped.scaled(rw, rh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                // Stamp Scribus's resolution, not sourceImg's. Qt's own JPEG
                // reader honours JFIF density_unit=0 and falls back to 96 dpi,
                // so on exactly the files the loader fallback rescues it would
                // write 96 here. imgInfo is what Image Properties shows.
                out.setDotsPerMeterX(qRound(srcDpiX / 0.0254));
                out.setDotsPerMeterY(qRound(srcDpiY / 0.0254));

                if (out.save(outputPath, "PNG")) {
                    double mmToPt = 2.8346;
                    double newW = targetWmm * mmToPt;
                    double newH = targetHmm * mmToPt;

                    int actualW = qMax(1, out.width());
                    int actualH = qMax(1, out.height());

                    // Scale: actual pixels → frame pts (exact fit)
                    double scaleX = newW / (double)actualW;
                    double scaleY = newH / (double)actualH;

                    // Load image into frame (reload=true for cache clear)
                    item->Pfile = outputPath;
                    bool loadOk = m_doc->loadPict(outputPath, item, true, true);

                    if (loadOk) {
                        item->setWidth(newW);
                        item->setHeight(newH);
                        item->setImageXYScale(scaleX, scaleY);
                        item->setImageXYOffset(0.0, 0.0);
                        item->updateClip();
                        item->update();
                        m_doc->changed();
                        m_doc->regionsChanged()->update(QRectF());
                    } else {
                    }
                }
            }
        }

        m_hasCrop = false;
        m_cropRect = QRectF();
        // No manual restoreOverrideCursor() here: setAppModeByToggle() below
        // runs requestMode(modeNormal), which calls this mode's deactivate()
        // - that's what now pops the cross cursor, on every exit path
        // (Enter, Escape, a selection change), not just this one.
        m_view->updateCanvas();
        m_ScMW->setAppModeByToggle(false, modeSuneerImageCrop);
    }
}

void CanvasMode_SuneerCrop::drawControls(QPainter* p)
{
    if (!m_drawing && !m_hasCrop) return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    QRectF cropRect = QRectF(m_startPoint, m_endPoint).normalized();

    // Fixed size — aspect ratio lock
    SuneerControlBar* cb = m_ScMW->suneerControlBar();
    if (cb && cb->isCropResizeEnabled()) {
        double targetW = cb->imgCropW();
        double targetH = cb->imgCropH();
        if (targetW > 0 && targetH > 0) {
            double ratio = targetW / targetH;
            int w = cropRect.width();
            int h = qRound(w / ratio);
            cropRect.setHeight(h);
        }
    }

    // Dark overlay
    QPainterPath outside;
    outside.addRect(m_view->rect());
    QPainterPath inside;
    inside.addRect(cropRect);
    p->fillPath(outside.subtracted(inside), QColor(0, 0, 0, 120));

    // Crop border
    p->setPen(QPen(Qt::white, 1, Qt::SolidLine));
    p->setBrush(Qt::NoBrush);
    p->drawRect(cropRect);

    // Rule of thirds
    p->setPen(QPen(QColor(255, 255, 255, 80), 1));
    int gx1 = cropRect.left() + cropRect.width() / 3;
    int gx2 = cropRect.left() + cropRect.width() * 2 / 3;
    int gy1 = cropRect.top() + cropRect.height() / 3;
    int gy2 = cropRect.top() + cropRect.height() * 2 / 3;
    p->drawLine(gx1, cropRect.top(), gx1, cropRect.bottom());
    p->drawLine(gx2, cropRect.top(), gx2, cropRect.bottom());
    p->drawLine(cropRect.left(), gy1, cropRect.right(), gy1);
    p->drawLine(cropRect.left(), gy2, cropRect.right(), gy2);

    // Handles
    int hs = 8;
    p->setBrush(Qt::white);
    p->setPen(QPen(Qt::black, 1));
    QList<QPointF> handles = {
        cropRect.topLeft(), cropRect.topRight(),
        cropRect.bottomLeft(), cropRect.bottomRight(),
        QPointF(cropRect.center().x(), cropRect.top()),
        QPointF(cropRect.center().x(), cropRect.bottom()),
        QPointF(cropRect.left(), cropRect.center().y()),
        QPointF(cropRect.right(), cropRect.center().y())
    };
    for (auto& pt : handles)
        p->drawRect(QRectF(pt.x()-hs/2, pt.y()-hs/2, hs, hs));

    // Hint text
    if (m_hasCrop) {
        p->setPen(Qt::white);
        p->drawText(cropRect.bottomLeft() + QPoint(5, 15), "Enter: Crop | Esc: Cancel");
    }

    p->restore();
}
