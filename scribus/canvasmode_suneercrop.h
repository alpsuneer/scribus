#ifndef CANVASMODE_SUNEERCROP_H
#define CANVASMODE_SUNEERCROP_H
#include "canvasmode.h"
#include <QRubberBand>
#include <QPointF>

class ScribusView;
class ScribusMainWindow;

class CanvasMode_SuneerCrop : public CanvasMode
{
    Q_OBJECT
public:
    explicit CanvasMode_SuneerCrop(ScribusView* view);
    void enterEvent(QEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent* m) override;
    void mouseMoveEvent(QMouseEvent* m) override;
    void mouseReleaseEvent(QMouseEvent* m) override;
    void keyPressEvent(QKeyEvent* e) override;
    void drawControls(QPainter* p) override;
private:
    ScribusMainWindow* m_ScMW {nullptr};
    QPointF  m_startPoint;
    QPointF  m_endPoint;
    bool    m_drawing {false};
    bool    m_hasCrop {false};
    QRectF  m_cropRect;
};
#endif
