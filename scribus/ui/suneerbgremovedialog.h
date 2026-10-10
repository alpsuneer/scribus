/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERBGREMOVEDIALOG_H
#define SUNEERBGREMOVEDIALOG_H

#include <QDialog>
#include <QGraphicsView>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRect>

#include "scribusapi.h"
#include "suneerbgremove.h"

class QComboBox;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QUndoStack;

//! Preview canvas: wheel zooms, Space/middle button pans, the rest goes to the dialog's tool.
class SuneerBgView : public QGraphicsView
{
	Q_OBJECT
public:
	explicit SuneerBgView(QWidget* parent = nullptr);
	void fitImage();
signals:
	void toolPress(const QPointF& scenePos, Qt::MouseButton button);
	void toolMove(const QPointF& scenePos, bool buttonDown);
	void toolRelease(const QPointF& scenePos);
protected:
	void wheelEvent(QWheelEvent* e) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;
	void keyReleaseEvent(QKeyEvent* e) override;
	void drawBackground(QPainter* painter, const QRectF& rect) override;
private:
	bool m_space { false };
	bool m_panning { false };
	bool m_down { false };
	QPoint m_lastPan;
};

/*!
 \brief "Remove Background" dialog: select a part of the picture, run rembg
 on it in the background, refine with Erase/Restore brushes, apply.
 */
class SCRIBUS_API SuneerBgRemoveDialog : public QDialog
{
	Q_OBJECT
public:
	struct State
	{
		SuneerBgRemove::Mode mode { SuneerBgRemove::Mode::Whole };
		QString model { QStringLiteral("u2net_human_seg") };
		int feather { 0 };
		QPainterPath selection;   //!< image pixels
		QImage brush;             //!< Grayscale8, 128 = untouched
		QImage cut;               //!< raw rembg output for cutRect
		QRect cutRect;
	};

	SuneerBgRemoveDialog(const QImage& source, const State& initial, bool hasOriginal, QWidget* parent = nullptr);
	~SuneerBgRemoveDialog() override;

	const State& state() const { return m_state; }
	//! The composed picture, or a null image when nothing was run.
	QImage result() const;
	//! "Reset to original" was pressed: the caller puts the source picture back.
	bool resetRequested() const { return m_reset; }

private slots:
	void onToolPress(const QPointF& p, Qt::MouseButton b);
	void onToolMove(const QPointF& p, bool down);
	void onToolRelease(const QPointF& p);
	void runRemoval();
	void cancelRemoval();
	void onRemovalFinished(const QImage& cut);
	void onRemovalFailed(const QString& err);
	void clearSelection();
	void invertSelection();
	void updatePreview();
	void refreshOutline();
	void onModeChanged();
	void applySnapshot(const QPainterPath& sel, const QImage& brush);

private:
	enum Tool { Rect, Lasso, Erase, Restore, Pan };
	Tool tool() const;
	bool subtractMode() const;
	void pushUndo(const QString& label);
	void commitShape(const QPainterPath& shape);
	void stampBrush(const QPointF& p);
	void ensureBrush();
	void setBusy(bool on, const QString& text = QString());

	QImage m_source;
	State m_state;
	bool m_reset { false };
	bool m_hasOriginal { false };

	// undo
	QUndoStack* m_undo { nullptr };
	QPainterPath m_undoSel;
	QImage m_undoBrush;
	friend class SuneerBgSnapshotCommand;

	// canvas
	QGraphicsScene* m_scene { nullptr };
	SuneerBgView* m_view { nullptr };
	QGraphicsPixmapItem* m_pix { nullptr };
	QGraphicsPathItem* m_outline { nullptr };
	QGraphicsPathItem* m_rubber { nullptr };
	QGraphicsEllipseItem* m_cursor { nullptr };
	QTimer* m_previewTimer { nullptr };

	// tool state
	QPointF m_pressPos;
	QPolygonF m_lasso;
	bool m_lassoActive { false };
	bool m_dragging { false };
	QPointF m_lastStamp;

	// widgets
	QToolButton *m_rectBtn, *m_lassoBtn, *m_eraseBtn, *m_restoreBtn, *m_panBtn;
	QRadioButton *m_addRadio, *m_subRadio;
	QRadioButton *m_wholeRadio, *m_insideRadio, *m_keepRadio;
	QComboBox* m_modelCombo;
	QSpinBox* m_featherSpin;
	QSlider *m_sizeSlider, *m_softSlider;
	QPushButton *m_runBtn, *m_cancelRunBtn, *m_applyBtn, *m_resetBtn, *m_undoBtn, *m_redoBtn;
	QProgressBar* m_progress;
	QLabel* m_status;

	SuneerBgRemover* m_remover { nullptr };
};

#endif
