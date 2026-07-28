#include "canvasmode_suneercrop.h"
#include "canvas.h"
#include "scribusview.h"
#include "scribus.h"
#include "ui/suneercontrolbar.h"
#include "scribusview.h"
#include "scribusdoc.h"
#include "selection.h"
#include "pageitem.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QCursor>
#include <QProcess>
#include <QScrollBar>
#include <QImage>
#include <QFileInfo>
#include <QApplication>

CanvasMode_SuneerCrop::CanvasMode_SuneerCrop(ScribusView* view)
    : CanvasMode(view), m_ScMW(view->m_ScMW)
{
}

void CanvasMode_SuneerCrop::enterEvent(QEvent*)
{
    QApplication::setOverrideCursor(Qt::CrossCursor);
}

void CanvasMode_SuneerCrop::leaveEvent(QEvent*)
{
    QApplication::restoreOverrideCursor();
}

void CanvasMode_SuneerCrop::mousePressEvent(QMouseEvent* m)
{
    m->accept();
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
        m_drawing = false;
        m_hasCrop = false;
        m_cropRect = QRectF();
        m_view->updateCanvas();
        m_ScMW->setAppModeByToggle(false, modeSuneerImageCrop);
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

        // Python crop — always save file
        SuneerControlBar* cb = m_ScMW->suneerControlBar();
        if (!item->Pfile.isEmpty()) {
            bool fixedSize = cb && cb->isCropResizeEnabled();
            double targetWmm = fixedSize ? cb->imgCropW() : (cropW / 2.8346);
            double targetHmm = fixedSize ? cb->imgCropH() : (cropH / 2.8346);
            if (targetWmm > 1 && targetHmm > 1) {
                QString inputPath  = item->Pfile;
                QFileInfo fi(inputPath);
                QString outputPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_crop.jpg";

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
                int rw = qMax(1, qRound(targetWmm / 25.4 * 72));
                int rh = qMax(1, qRound(targetHmm / 25.4 * 72));

                QString script = QString(
                    "from PIL import Image\n"
                    "img = Image.open(r'%1')\n"
                    "iw, ih = img.size\n"
                    "x1 = max(0, min(%2, iw-1))\n"
                    "y1 = max(0, min(%3, ih-1))\n"
                    "x2 = max(x1+1, min(%2+%4, iw))\n"
                    "y2 = max(y1+1, min(%3+%5, ih))\n"
                    "print('crop box:', x1, y1, x2, y2, 'img:', iw, ih)\n"
                    "crop = img.crop((x1, y1, x2, y2))\n"
                    "out = crop.resize((%6, %7), Image.LANCZOS)\n"
                    "out = out.convert('RGB')\n"
                    "out.save(r'%8', quality=95, dpi=(72,72))\n"
                    "print('done')\n"
                ).arg(inputPath).arg(cx).arg(cy).arg(cw).arg(ch).arg(rw).arg(rh).arg(outputPath);

                QProcess proc;
                proc.start("python3", QStringList() << "-c" << script);
                proc.waitForFinished(30000);

                if (proc.exitCode() == 0) {
                    double mmToPt = 2.8346;
                    double newW = targetWmm * mmToPt;
                    double newH = targetHmm * mmToPt;

                    // ✅ QImage ഉപയോഗിച്ച് actual pixel size read ചെയ്യൂ
                    // (item->pixm.width() display-scaled ആയതിനാൽ reliable അല്ല)
                    QImage checkImg(outputPath);
                    int actualW = checkImg.width();
                    int actualH = checkImg.height();


                    if (actualW <= 0 || actualH <= 0) {
                        actualW = qMax(1, rw);
                        actualH = qMax(1, rh);
                    }

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
        QApplication::restoreOverrideCursor();
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
