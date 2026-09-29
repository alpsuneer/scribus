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
    ~CanvasMode_SuneerCrop() override;
    void activate(bool fromGesture) override;
    void deactivate(bool forGesture) override;
    void enterEvent(QEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent* m) override;
    void mouseMoveEvent(QMouseEvent* m) override;
    void mouseReleaseEvent(QMouseEvent* m) override;
    void keyPressEvent(QKeyEvent* e) override;
    void drawControls(QPainter* p) override;

private:
    //! Leave crop mode and restore normal editing (shared by Esc, selection
    //! change and apply).
    void exitCropMode();
    //! The frame crop mode was entered on; used to notice a selection change.
    PageItem* croppedItem() const;

    ScribusMainWindow* m_ScMW {nullptr};
    QPointF  m_startPoint;
    QPointF  m_endPoint;
    bool    m_drawing {false};
    bool    m_hasCrop {false};
    QRectF  m_cropRect;
    QString m_cropItemName;   //!< item crop mode started on (empty = none)
    /*! \brief Whether this mode currently holds a QApplication override
        cursor (the cross cursor enterEvent() pushes while dragging a crop
        rectangle).

        enterEvent()/leaveEvent() are not guaranteed to pair up: the mode can
        be deactivated (Escape, a tool switch, Enter to apply) while the
        mouse never left the canvas, which would leave the override on
        QApplication's stack forever with no widget left that owns popping
        it. This flag lets deactivate() and the destructor pop it exactly
        once, only if enterEvent actually pushed it and leaveEvent has not
        already popped it - restoreOverrideCursor() on an empty stack pops
        whatever unrelated override the rest of the app happens to be
        showing. */
    bool    m_cursorOverridden {false};
};
#endif
