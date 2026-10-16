/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEEREDITPHOTODIALOG_H
#define SUNEEREDITPHOTODIALOG_H

#include <QDialog>
#include <QGraphicsView>
#include <QImage>
#include <QPainterPath>
#include <QPolygonF>
#include <QPointer>
#include <QElapsedTimer>
#include <QRect>

#include "scribusapi.h"
#include "suneerphotoedit.h"

class QCheckBox;
class QComboBox;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QUndoStack;
class PageItem;
class ScribusDoc;
class SuneerBgRemover;

//! Preview canvas: Ctrl+wheel / wheel outside the crop zooms, Space or the middle button pans.
class SuneerPhotoView : public QGraphicsView
{
	Q_OBJECT
public:
	explicit SuneerPhotoView(QWidget* parent = nullptr);
	void fitImage();
signals:
	void toolPress(const QPointF& scenePos, Qt::MouseButton button);
	void toolMove(const QPointF& scenePos, bool buttonDown);
	void toolRelease(const QPointF& scenePos);
	//! wheel without Ctrl: the dialog decides (crop size / zoom); returns true when used
	void wheelAt(const QPointF& scenePos, int delta, bool* used);
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
 \brief "Edit photo": the original picture with a crop locked to the frame's
 shape, rotate / flip / fine rotate, and the optional steps Auto face,
 Remove background (+ Erase / Restore brushes), Background colour and Tone
 match, all with a live preview. Apply writes a new file and relinks.
 */
class SCRIBUS_API SuneerEditPhotoDialog : public QDialog
{
	Q_OBJECT
public:
	SuneerEditPhotoDialog(ScribusDoc* doc, PageItem* item, QWidget* parent = nullptr);
	~SuneerEditPhotoDialog() override;

protected:
	void showEvent(QShowEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;

private slots:
	void onToolPress(const QPointF& p, Qt::MouseButton b);
	void onToolMove(const QPointF& p, bool down);
	void onToolRelease(const QPointF& p);
	void onWheel(const QPointF& p, int delta, bool* used);
	void schedulePreview();
	void updatePreview();
	void onRotate(int deg);
	void onFlipH();
	void onFlipV();
	void onFineChanged(int v);
	void onAutoFace(bool on);
	void onRemoveBg(bool on);
	void onModelChanged();
	void onBgNone();
	void onBgPick();
	void onToneToggled(bool on);
	void onToneChanged(int v);
	void onFreeToggled(bool on);
	void onRemovalFinished(const QImage& cut);
	void onRemovalFailed(const QString& err);
	void onApply();
	void onResetAutomatic();
	void onResetOriginal();
	void onSelErase();
	void onSelRestore();
	void onSelSmart();
	void onSelProtect();
	void onSelExclude();
	void onViewToggle();
	void onBgModeChanged(int idx);
	void onSelKeep();
	void onSelInvert();
	void onSelClear();
	void onResetMask();
	void onRefineChanged(int v);
	void onAntsTick();
	void onUndoMask();
	void onRedoMask();

private:
	enum Tool { CropTool, Erase, Restore, LassoFree, LassoPoly, RectSel, EllipseSel, ProtectBrush, ExcludeBrush };
	bool isBrushTool(Tool t) const { return t == Erase || t == Restore || t == ProtectBrush || t == ExcludeBrush; }
	bool isSelTool(Tool t) const { return t == LassoFree || t == LassoPoly || t == RectSel || t == EllipseSel; }
	Tool tool() const;
	//! the mask can be edited: Manual mode, or Auto with a finished cut-out
	bool maskReady() const { return m_s.removeBg && (m_s.bgMode == QLatin1String("manual") || !m_cut.isNull()); }
	bool autoMode() const { return m_s.removeBg && m_s.bgMode == QLatin1String("auto"); }
	double featherPx() const;
	void runRemoval();
	void ensureBrush();
	//! stamps from the last point to scenePos (spacing 10 % of the diameter), stabilised
	void brushTo(const QPointF& scenePos, bool first);
	double brushRadiusSource() const;
	void beginMaskEdit();            // snapshot for undo
	void endMaskEdit(const QString& label);
	void setMask(const QImage& m);   // from undo/redo
	void composePreview();           // base + mask -> pixmap (fast, no re-render)
	void refreshSelItem();
	void finishLasso(bool close);
	void cancelLasso();
	QPainterPath lassoPathScene() const;
	void applySelection(int target, bool outside, const QString& label);
	void updateMaskButtons();
	void resetCrop(bool useFace);
	void setCrop(const QRectF& cropFull);
	QRectF clampCrop(QRectF r) const;
	double aspect() const;   // height / width of the crop
	void refreshOverlay();
	void setBusy(bool on, const QString& text = QString());
	void updateBgButton();
	void rebuildTransform();
	QPointF sceneToSource(const QPointF& scenePos) const;

	ScribusDoc* m_doc { nullptr };
	QPointer<PageItem> m_item;
	QString m_sourcePath;
	QImage m_source;            // ARGB32, source orientation
	QImage m_cut;               // rembg RGBA, source size
	QImage m_brush;             // Grayscale8, source size
	QImage m_protect;           // Grayscale8, source size: 255 always kept, 0 always removed, 128 none
	QImage m_protectBefore;
	int m_viewMode { 0 };       // 0 result on colour/checker, 1 checkerboard, 2 red overlay on the removed parts
	bool m_resetAuto { false }; // "Reset to automatic" pressed: Apply does not mark the photo manual
	QString m_cutModel;         // model the cut was made with
	QRect m_cutRegion;          // SOURCE pixels the cut covers (crop + 10 % margin); null = whole
	QRect m_pendingRegion;      // region of the removal that is running
	double m_regionScale { 1.0 };   // model input px / source px of the pending run
	QTimer* m_rerunTimer { nullptr };   // crop moved outside the cached region: run again
	QElapsedTimer m_rembgTimer;
	bool m_sourceMissing { false };
	SuneerPhotoEdit::Settings m_s;
	double m_frameAspect { 1.0 };
	bool m_isObit { false };
	bool m_applied { false };
	bool m_fitted { false };

	// preview geometry
	QSize m_fullSize;           // transformed full-res size
	double m_f { 1.0 };         // preview px / full px
	QTransform m_toTransformedFull;   // source px -> transformed full px

	// canvas
	QGraphicsScene* m_scene { nullptr };
	SuneerPhotoView* m_view { nullptr };
	QGraphicsPixmapItem* m_pix { nullptr };
	QGraphicsPathItem* m_shade { nullptr };
	QGraphicsRectItem* m_cropItem { nullptr };
	QGraphicsRectItem* m_handles[4] { nullptr, nullptr, nullptr, nullptr };
	QGraphicsRectItem* m_guide { nullptr };
	QGraphicsEllipseItem* m_cursor { nullptr };
	QTimer* m_previewTimer { nullptr };

	// interaction
	int m_drag { -1 };          // -1 none, 0..3 handle, 4 move
	QPointF m_pressScene;
	QRectF m_pressCrop;
	QPointF m_lastStamp;        // last stamped point, scene px
	QPointF m_smooth;           // stabilised cursor, scene px
	bool m_stamping { false };
	bool m_haveLast { false };
	double m_hardness { 0.8 };
	int m_stabiliser { 30 };
	QImage m_maskBefore;        // snapshot at the start of an edit (undo)
	QUndoStack* m_undo { nullptr };
	friend class SuneerMaskCommand;
	// preview cache
	QImage m_previewBase;       // transformed preview, alpha from the cut (refined), tone applied, no mask
	QImage m_previewBaseAlpha;  // its alpha as Grayscale8
	// lasso
	QPainterPath m_sel;         // closed selection, transformed FULL-res px
	QPolygonF m_lassoPts;       // the shape being drawn, scene px
	bool m_lassoActive { false };
	bool m_lassoPoly { false };
	QGraphicsPathItem* m_selItem { nullptr };
	QGraphicsPathItem* m_rubber { nullptr };
	QTimer* m_antsTimer { nullptr };
	int m_antsOffset { 0 };
	qint64 m_lastClickMs { 0 };
	QPointF m_lastClickScene;

	// widgets
	QToolButton *m_rotL, *m_rotR, *m_flipHBtn, *m_flipVBtn;
	QSlider* m_fineSlider;
	QLabel* m_fineLabel;
	QCheckBox* m_freeChk;
	QCheckBox* m_autoFaceChk;
	QCheckBox* m_guideChk;
	QCheckBox* m_removeBgChk;   // hidden; the mode combo drives m_s.removeBg / m_s.bgMode
	QComboBox* m_bgModeCombo { nullptr };
	QComboBox* m_modelCombo;
	QToolButton *m_cropToolBtn, *m_eraseBtn, *m_restoreBtn, *m_lassoBtn, *m_polyBtn, *m_rectBtn, *m_ellipseBtn, *m_protectBrushBtn, *m_excludeBrushBtn;
	QPushButton *m_selSmartBtn, *m_selProtectBtn, *m_selExcludeBtn;
	QToolButton* m_viewBtn;
	QSlider* m_brushSize;
	QSlider* m_hardSlider;
	QSlider* m_stabSlider;
	QLabel* m_brushInfo;
	QSlider* m_featherSlider;
	QPushButton *m_selEraseBtn, *m_selRestoreBtn, *m_selKeepBtn, *m_selInvertBtn, *m_selClearBtn;
	QToolButton *m_undoBtn, *m_redoBtn, *m_resetMaskBtn;
	QCheckBox* m_refineChk;
	QSlider* m_refineSlider;
	QCheckBox* m_bgColorChk;
	QPushButton* m_bgColorBtn;
	QCheckBox* m_toneChk;
	QSlider* m_toneSlider;
	QPushButton *m_applyBtn, *m_cancelBtn, *m_resetAutoBtn, *m_resetOrigBtn;
	QProgressBar* m_progress;
	QLabel* m_status;
	SuneerBgRemover* m_remover { nullptr };
};

#endif
