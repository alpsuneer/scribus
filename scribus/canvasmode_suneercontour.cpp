#include "canvasmode_suneercontour.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "pageitem.h"
#include "selection.h"
#include "fpointarray.h"
#include "canvas.h"
#include "suneercursor.h"
#include "pageitem_imageframe.h"
#include "undomanager.h"
#include "undostate.h"
#include <QMouseEvent>
#include <QPainter>
#include <QCursor>

SuneerContourMode::SuneerContourMode(ScribusView* view)
    : CanvasMode(view)
{
}

void SuneerContourMode::activate(bool fromGesture)
{
    // Pen/knife cursor - on the VIEW, like every other canvas mode. A cursor
    // set on the Canvas widget itself would outlive this mode and hide the
    // resize/move cursors the normal mode puts on the view.
    m_view->setCursor(QCursor(Qt::PointingHandCursor));
    m_poly.resize(0);
    m_drawing = false;
    // Get target image item
    m_targetItem = nullptr;
    if (m_doc->m_Selection->count() > 0)
        m_targetItem = m_doc->m_Selection->itemAt(0);
}

void SuneerContourMode::deactivate(bool forGesture)
{
    m_view->setCursor(QCursor(Qt::ArrowCursor));
    if (!forGesture)
        suneerCursorSettle(m_view, "contour draw end");
}

void SuneerContourMode::keyPressEvent(QKeyEvent *e)
{
    // ESC to exit
    if (e->key() == Qt::Key_Escape) {
        m_view->requestMode(modeNormal);
        e->accept();
    }
}

void SuneerContourMode::mousePressEvent(QMouseEvent *m)
{
    if (m->button() != Qt::LeftButton) return;
    m_drawing = true;
    m_poly.resize(0);
    FPoint p = m_canvas->globalToCanvas(m->globalPosition().toPoint());
    // Convert to item coordinates
    if (m_targetItem) {
        double x = p.x() - m_targetItem->xPos();
        double y = p.y() - m_targetItem->yPos();
        m_poly.addQuadPoint(x, y, x, y, x, y, x, y);
    }
    m_xp = p.x();
    m_yp = p.y();
    m->accept();
}

void SuneerContourMode::mouseMoveEvent(QMouseEvent *m)
{
    if (!m_drawing) return;
    FPoint p = m_canvas->globalToCanvas(m->globalPosition().toPoint());
    if (m_targetItem) {
        double x = p.x() - m_targetItem->xPos();
        double y = p.y() - m_targetItem->yPos();
        // Add point if moved enough
        double dx = p.x() - m_xp;
        double dy = p.y() - m_yp;
        if (dx*dx + dy*dy > 4) {
            m_poly.addQuadPoint(x, y, x, y, x, y, x, y);
            m_xp = p.x();
            m_yp = p.y();
            m_canvas->update();
        }
    }
    m->accept();
}

void SuneerContourMode::mouseReleaseEvent(QMouseEvent *m)
{
    if (!m_drawing || !m_targetItem) return;
    m_drawing = false;

    if (m_poly.size() >= 3) {
        // Close path
        FPoint first = m_poly.point(0);
        m_poly.addQuadPoint(first.x(), first.y(), first.x(), first.y(),
                            first.x(), first.y(), first.x(), first.y());
        // Set as ContourLine: user data, one undo step, and from now on
        // the contour follows the picture (hand-made, never regenerated).
        const FPointArray before = m_targetItem->ContourLine.copy();
        const int oldFlow = m_targetItem->textFlowMode();
        UndoTransaction tx;
        if (UndoManager::undoEnabled())
            tx = UndoManager::instance()->beginTransaction(m_targetItem->getUName(), m_targetItem->getUPixmap(), tr("Draw Contour"), QString(), Um::IBorder);
        m_targetItem->ContourLine = m_poly.copy();
        m_targetItem->ClipEdited = true;
        if (auto* frame = m_targetItem->asImageFrame())
        {
            frame->suneerSetAutoContour(false);
            frame->suneerAcceptContour();
        }
        m_targetItem->setTextFlowMode(PageItem::TextFlowUsesContourLine);
        if (UndoManager::undoEnabled())
        {
            auto* state = new ScOldNewState<FPointArray>(Um::EditContourLine, QString(), Um::IBorder);
            state->set("SUNEER_CONTOUR");
            state->set("DETECT_CONTOUR_FLOW", true);
            state->set("DETECT_CONTOUR_FLOW_OLD", oldFlow);
            state->set("DETECT_CONTOUR_FLOW_NEW", int(PageItem::TextFlowUsesContourLine));
            state->setStates(before, m_targetItem->ContourLine);
            UndoManager::instance()->action(m_targetItem, state);
        }
        if (tx)
            tx.commit();
        m_targetItem->update();
        m_doc->changed();
        m_doc->regionsChanged()->update(QRectF());
    }
    // Force canvas refresh
    m_doc->invalidateAll();
    m_doc->regionsChanged()->update(QRectF());
    m_canvas->update();
    // Return to normal mode
    m_view->requestMode(modeNormal);
    m->accept();
}

void SuneerContourMode::drawControls(QPainter* p)
{
    if (!m_drawing || m_poly.size() < 2) return;
    if (!m_targetItem) return;
    p->save();
    p->setPen(QPen(QColor(255, 30, 30), 2, Qt::SolidLine));
    p->setBrush(Qt::NoBrush);
    QPolygonF poly;
    double sx = m_canvas->scale();
    double dx = m_doc->minCanvasCoordinate.x() * sx;
    double dy = m_doc->minCanvasCoordinate.y() * sx;
    for (int i = 0; i < m_poly.size(); i++) {
        FPoint pt = m_poly.point(i);
        double screenX = (m_targetItem->xPos() + pt.x()) * sx - dx;
        double screenY = (m_targetItem->yPos() + pt.y()) * sx - dy;
        poly << QPointF(screenX, screenY);
    }
    p->drawPolyline(poly);
    p->restore();
}
