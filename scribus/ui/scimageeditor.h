/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEEDITOR_H
#define SCIMAGEEDITOR_H

#include <QMainWindow>
#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QPointer>

#include "scribusapi.h"
#include "scimagestructs.h"

class QAction;
class QActionGroup;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QToolBar;

class QUndoStack;

class QGraphicsRectItem;

class ScImageSelection;
class MarchingAntsItem;
class ImageTool;

class ColorList;
class FilterParamDialog;
class PageItem_ImageFrame;
class OptionsBar;
class MoveOptionsWidget;
class CropOptionsWidget;

/*!
 \brief The image canvas view: Ctrl+wheel zoom, space/middle-drag pan, plus the
        Phase 3 tools (Move/Pan, Select marquee, Crop, Eyedropper, Zoom, Hand).
 */
class SCRIBUS_API ScImageEditorView : public QGraphicsView
{
	Q_OBJECT

public:
	enum class Tool { Move, Select, Crop, Eyedropper, Zoom, Hand };

	explicit ScImageEditorView(QGraphicsScene* scene, QWidget* parent = nullptr);

	double zoomFactor() const { return m_zoom; }

	void setTool(Tool tool);
	Tool tool() const { return m_tool; }

	//! When set, left-button mouse/key events route to this tool (see ImageTool).
	//! Null restores the legacy built-in tool handling.
	void setActiveImageTool(ImageTool* tool) { m_imageTool = tool; }

	//! Current marquee in scene (== image pixel) coordinates; null if none.
	QRectF selectionRect() const;
	void clearSelection();

public slots:
	void zoomBy(double factor);
	void zoomActual();
	void zoomFit();

signals:
	void cursorMoved(const QPointF& scenePos);
	void zoomChanged(double factor);
	void selectionChanged(const QRectF& sceneRect);
	void cropConfirmed(const QRectF& sceneRect);
	void colorPicked(const QPoint& imagePos);

protected:
	void wheelEvent(QWheelEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;

private:
	void updateCursor();
	void updateRubber(const QPointF& scenePos);
	QRectF clampToScene(const QRectF& r) const;

	double m_zoom { 1.0 };
	bool m_spaceDown { false };
	bool m_panning { false };
	QPoint m_lastPanPoint;

	Tool m_tool { Tool::Move };
	bool m_rubberActive { false };
	QPointF m_rubberStart;
	QGraphicsRectItem* m_selItem { nullptr };
	ImageTool* m_imageTool { nullptr };   //!< active ImageTool, or null for legacy handling
};

/*!
 \brief Built-in "Scribus Image Editor" window (Photoshop-lite).

 Phase 1 is scaffolding only: it displays the image, offers non-functional
 placeholder tools and dock panels, and can save the (currently unmodified)
 image back to the frame's file and reload it via "Save & Apply".
 */
class SCRIBUS_API ScImageEditor : public QMainWindow
{
	Q_OBJECT

public:
	ScImageEditor(const QImage& image, PageItem_ImageFrame* frame, QWidget* parent = nullptr);
	~ScImageEditor() = default;

public:
	//! One applied effect plus its editor-side metadata (visibility, labels).
	struct StackEntry
	{
		ImageEffect effect;
		bool visible { true };
		QString name;
		QString summary;
		QImage mask;   //!< Alpha8 selection mask; null = apply to whole image
	};

	//! The pixel selection for this editor (never null once the image is set).
	ScImageSelection* selection() const { return m_selection; }
	//! The currently displayed image (base + committed effects) — SAM input.
	const QImage& currentImage() const { return m_image; }
	//! The canvas scene (tools add live-preview items here).
	QGraphicsScene* scene() const { return m_scene; }
	//! The canvas view.
	ScImageEditorView* view() const { return m_view; }
	//! Activate an ImageTool (null → back to legacy built-in tool handling).
	void setActiveTool(ImageTool* tool);

	//! Swap in a whole stack state and re-render (used by undo/redo commands).
	void applyStackState(const QList<StackEntry>& state);
	//! Swap in a base image + cropped flag and re-render (used by crop undo/redo).
	void applyBaseState(const QImage& base, bool cropped);
	//! Replace the selection mask (used by selection undo/redo commands).
	void applySelectionMask(const QImage& mask);
	//! Label the next selection edit for the undo history (call before mutating).
	void setNextSelectionUndoLabel(const QString& label) { m_pendingSelectionLabel = label; }
	//! Group many selection edits (a brush stroke) into a single undo entry.
	void beginSelectionStroke();
	void endSelectionStroke(const QString& label);

	// Preview hooks used by the FilterParamDialog framework.
	//! Temporarily display \a preview on the canvas (does not alter the stack).
	void setPreviewImage(const QImage& preview);
	//! Discard any live preview and redisplay the committed stack.
	void clearPreview();
	//! Commit the previewed effect(s) onto the stack (undoable).
	void commitPreviewAsFilter(const ScImageEffectList& effects);

protected slots:
	void saveAndApply();
	void exportFlattened();
	void revertImage();
	void updateZoomLabel(double factor);
	void updateCoordLabel(const QPointF& scenePos);
	void removeAllFilters();
	void editSelectedEffect();
	void removeSelectedEffect();
	void moveSelectedEffectUp();
	void moveSelectedEffectDown();
	void onStackItemChanged(QListWidgetItem* item);
	void updateStackButtons();
	void onToolChanged(QAction* action);
	void onSelectionChanged(const QRectF& sceneRect);
	void onCropConfirmed(const QRectF& sceneRect);
	void onColorPicked(const QPoint& imagePos);
	//! The selection emitted changed() — push an undo command for the transition.
	void onSelectionEdited();
	// Image menu
	void openImageSizeDialog();
	void openCanvasSizeDialog();
	// Crop tool options bar
	void onCropCommit();
	void onCropCancel();
	// Select menu
	void selectAll();
	void selectDeselect();
	void selectReselect();
	void selectInverse();
	void selectFeather();
	void selectExpand();
	void selectContract();
	void selectSmooth();

protected:
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

	void createActions();
	void createMenus();
	void createFilterMenu();
	void populateFilterMenu(QMenu* menu, bool primary = false);
	void createSelectMenu();
	void createToolBar();
	//! Show the active tool's options widget (if any) in the top options bar.
	void updateToolOptionsBar();
	void createDockWidgets();
	QWidget* buildAdjustmentsPanel();
	void createStatusBar();
	void setSceneImage(const QImage& image);
	void loadWindowSettings();
	void saveWindowSettings();

	// --- Effect stack (Phase 2) ---------------------------------------------
	//! The doc palette used to resolve named colors for color effects.
	ColorList& docColors();
	//! The visible subset of the stack, in apply order.
	ScImageEffectList effectiveEffects() const;
	//! Wrap a raw effect into a StackEntry with name/summary/visibility and an
	//! optional selection mask (null = whole image).
	StackEntry makeStackEntry(const ImageEffect& effect, bool visible, const QImage& mask = QImage()) const;
	//! Render the visible entries onto the base image, one effect at a time,
	//! blending each masked entry through its selection mask.
	QImage applyStackToImage(const QList<StackEntry>& entries);
	//! true if any visible entry is region-masked (Save & Apply must bake it).
	bool hasMaskedEffects() const;
	//! The current selection mask if non-empty, else a null image.
	QImage currentSelectionMask() const;
	//! Re-apply the visible effect stack to the original image and display it.
	//! Create/resize the selection + marching-ants overlay for the current base.
	void ensureSelection();
	void renderEffects();
	//! Temporarily display the visible stack plus \a extra (live preview for Add).
	void previewEffects(const ScImageEffectList& extra);
	//! Temporarily display the stack with entry \a row replaced by \a repl (Edit preview).
	void previewReplace(int row, const ScImageEffectList& repl);
	//! Append \a extra to the stack (each as a visible entry) and re-render.
	void commitEffects(const ScImageEffectList& extra);
	//! Replace entry \a row with \a repl (empty removes it) and re-render.
	void replaceStackEntry(int row, const ScImageEffectList& repl);
	//! Push an undoable transition from the current stack to \a newState.
	void commitStackChange(const QString& text, const QList<StackEntry>& newState);
	//! Run a modal Add-filter dialog, wiring live preview / commit / cancel.
	void runFilterDialog(FilterParamDialog* dlg);
	//! Rebuild the Adjustments list widget from m_stack.
	void refreshStackList();
	int  currentStackRow() const;
	//! Enable/disable filter actions based on image / stack state.
	void updateFilterActions();

	bool m_firstShow { true };

	//! Crop the working base to \a sceneRect (in image pixel coordinates).
	void performCrop(const QRectF& sceneRect);

	QPointer<PageItem_ImageFrame> m_frame;
	QImage m_image;                 //!< currently displayed image (original + committed effects)
	QImage m_originalImage;         //!< working base (pristine, unless cropped)
	QList<StackEntry> m_stack;      //!< applied effect stack (top = first applied)
	QUndoStack* m_undoStack { nullptr };
	QRectF m_selectionRect;         //!< active marquee (image coords), or null
	bool m_baseIsCropped { false }; //!< true once the base image has been cropped

	ScImageSelection* m_selection { nullptr };
	MarchingAntsItem* m_marchingAnts { nullptr };
	ImageTool* m_activeTool { nullptr };   //!< current ImageTool (marquee/lasso), or null
	QImage m_selectionUndoBaseline;        //!< mask as of the last pushed selection command
	bool m_suppressSelectionUndo { false };//!< true while applying a selection undo/redo
	QString m_pendingSelectionLabel;       //!< history label for the next selection edit
	QImage m_strokeStartMask;              //!< selection at the start of a brush stroke

	QGraphicsScene* m_scene { nullptr };
	QGraphicsPixmapItem* m_pixmapItem { nullptr };
	ScImageEditorView* m_view { nullptr };

	// Left toolbar — exclusive placeholder tools
	QActionGroup* m_toolGroup { nullptr };
	QAction* m_toolMove { nullptr };
	QAction* m_toolSelect { nullptr };
	QAction* m_toolCrop { nullptr };
	QAction* m_toolEyedropper { nullptr };
	QAction* m_toolZoom { nullptr };
	QAction* m_toolHand { nullptr };
	// Selection tools (ImageTool-based)
	QAction* m_toolRectMarquee { nullptr };
	QAction* m_toolEllipseMarquee { nullptr };
	QAction* m_toolLasso { nullptr };
	QAction* m_toolPolyLasso { nullptr };
	QAction* m_toolSmartSelect { nullptr };
	QAction* m_toolRefineBrush { nullptr };
	ImageTool* m_rectMarqueeTool { nullptr };
	ImageTool* m_ellipseMarqueeTool { nullptr };
	ImageTool* m_lassoTool { nullptr };
	ImageTool* m_polyLassoTool { nullptr };
	ImageTool* m_samTool { nullptr };
	ImageTool* m_refineBrushTool { nullptr };
	QImage m_lastSelectionMask;   //!< remembers the last selection for Reselect

	// Menu actions
	QAction* m_actClose { nullptr };
	QAction* m_actSaveApply { nullptr };
	QAction* m_actExportFlattened { nullptr };
	QAction* m_actRevert { nullptr };
	QAction* m_actUndo { nullptr };
	QAction* m_actRedo { nullptr };
	QAction* m_actImageSize { nullptr };
	QAction* m_actCanvasSize { nullptr };
	QAction* m_actZoomIn { nullptr };
	QAction* m_actZoomOut { nullptr };
	QAction* m_actZoomFit { nullptr };
	QAction* m_actZoomActual { nullptr };

	// Filter menu
	QMenu* m_filterMenu { nullptr };
	QAction* m_actRemoveAllFilters { nullptr };
	QList<QAction*> m_filterActions;   //!< every filter item, for enable/disable

	// Top options bar — shows the active tool's contextual controls
	QToolBar* m_toolOptionsBar { nullptr };

	// Photoshop-style Options Bar (below the menu bar) for the legacy tools.
	OptionsBar* m_optionsBar { nullptr };
	MoveOptionsWidget* m_moveOptions { nullptr };
	CropOptionsWidget* m_cropOptions { nullptr };

	//! Resize/canvas-size the working base (undoable), mirroring the crop path.
	void applyBaseResize(const QImage& newBase, const QString& label);

	// Adjustments dock — effects stack UI
	QListWidget* m_stackList { nullptr };
	QPushButton* m_editBtn { nullptr };
	QPushButton* m_removeBtn { nullptr };
	QPushButton* m_upBtn { nullptr };
	QPushButton* m_downBtn { nullptr };

	// Status bar labels
	QLabel* m_zoomLabel { nullptr };
	QLabel* m_coordsLabel { nullptr };
	QLabel* m_sizeLabel { nullptr };
	QLabel* m_swatchLabel { nullptr };   //!< eyedropper color swatch
	QLabel* m_colorLabel { nullptr };    //!< eyedropper hex + rgb readout
};

#endif // SCIMAGEEDITOR_H
