#ifndef CANVAS_MODE_SUNEERCONTOUR_H
#define CANVAS_MODE_SUNEERCONTOUR_H

#include <QPointer>
#include "canvasmode.h"
#include "fpointarray.h"

class PageItem;
class ScribusView;

class SuneerContourMode : public CanvasMode
{
    Q_OBJECT
public:
    explicit SuneerContourMode(ScribusView* view);
    ~SuneerContourMode() override = default;

    void activate(bool) override;
    void deactivate(bool) override;
    void mousePressEvent(QMouseEvent *m) override;
    void mouseMoveEvent(QMouseEvent *m) override;
    void mouseReleaseEvent(QMouseEvent *m) override;
    void drawControls(QPainter* p) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    FPointArray m_poly;
    bool m_drawing {false};
    double m_xp {-1.0};
    double m_yp {-1.0};
    QPointer<PageItem> m_targetItem;   // self-nulling: the item may be deleted while the mode is active
};

#endif
