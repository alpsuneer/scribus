/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimageeditor.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUndoView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "autocorrectengine.h"
#include <QInputDialog>

#include "iconmanager.h"
#include "pageitem_imageframe.h"
#include "sccolor.h"
#include "scimagefilterdialogs.h"
#include "scimagefilterengine.h"
#include "scimageselection.h"
#include "ui/dialogs/autoenhancedialog.h"
#include "ui/dialogs/imagesizedialog.h"
#include "ui/dialogs/canvassizedialog.h"
#include "ui/optionsbar.h"
#include "ui/tooloptions/moveoptionswidget.h"
#include "ui/tooloptions/cropoptionswidget.h"
#include "ui/marchingantsitem.h"
#include "ui/tools/imagetool.h"
#include "ui/tools/rectmarqueetool.h"
#include "ui/tools/ellipsemarqueetool.h"
#include "ui/tools/lassotool.h"
#include "ui/tools/polygonlassotool.h"
#include "ui/tools/samselecttool.h"
#include "ui/tools/refineedgesbrushtool.h"
#include "scribusdoc.h"
#include "util.h"
#include "selection.h"
#include "undomanager.h"

// ────────────────────────────────────────────────────────────────────────────
// Undo command
// ────────────────────────────────────────────────────────────────────────────

namespace
{
	// Coarse-grained command: captures the whole effect stack before/after a
	// mutation. Simple, always correct, and yields clean History labels.
	class StackChangeCommand : public QUndoCommand
	{
	public:
		StackChangeCommand(ScImageEditor* editor,
		                   const QList<ScImageEditor::StackEntry>& before,
		                   const QList<ScImageEditor::StackEntry>& after,
		                   const QString& text)
			: QUndoCommand(text), m_editor(editor), m_before(before), m_after(after)
		{}
		void undo() override { m_editor->applyStackState(m_before); }
		void redo() override { m_editor->applyStackState(m_after); }
	private:
		ScImageEditor* m_editor;
		QList<ScImageEditor::StackEntry> m_before;
		QList<ScImageEditor::StackEntry> m_after;
	};

	// Captures the working base image (+ cropped flag) before/after a crop, so
	// crops are undoable on the same stack as effect-stack changes. Base and
	// stack are independent concerns, so interleaved undo/redo composes correctly.
	class CropCommand : public QUndoCommand
	{
	public:
		CropCommand(ScImageEditor* editor,
		            const QImage& before, bool beforeCropped,
		            const QImage& after, bool afterCropped,
		            const QString& text)
			: QUndoCommand(text), m_editor(editor),
			  m_before(before), m_beforeCropped(beforeCropped),
			  m_after(after), m_afterCropped(afterCropped)
		{}
		void undo() override { m_editor->applyBaseState(m_before, m_beforeCropped); }
		void redo() override { m_editor->applyBaseState(m_after, m_afterCropped); }
	private:
		ScImageEditor* m_editor;
		QImage m_before;
		bool m_beforeCropped;
		QImage m_after;
		bool m_afterCropped;
	};

	// Captures the selection mask before/after an edit. The change has already
	// happened when the command is pushed, so the first redo() is a no-op.
	class SelectionChangeCommand : public QUndoCommand
	{
	public:
		SelectionChangeCommand(ScImageEditor* editor, const QImage& before, const QImage& after, const QString& text)
			: QUndoCommand(text), m_editor(editor), m_before(before), m_after(after)
		{}
		void undo() override { m_editor->applySelectionMask(m_before); }
		void redo() override
		{
			if (m_first) { m_first = false; return; }   // state already applied at push time
			m_editor->applySelectionMask(m_after);
		}
	private:
		ScImageEditor* m_editor;
		QImage m_before;
		QImage m_after;
		bool m_first { true };
	};
}

// ────────────────────────────────────────────────────────────────────────────
// ScImageEditorView
// ────────────────────────────────────────────────────────────────────────────

ScImageEditorView::ScImageEditorView(QGraphicsScene* scene, QWidget* parent)
	: QGraphicsView(scene, parent)
{
	setRenderHint(QPainter::SmoothPixmapTransform, true);
	setRenderHint(QPainter::Antialiasing, true);
	setDragMode(QGraphicsView::NoDrag);
	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	setResizeAnchor(QGraphicsView::AnchorViewCenter);
	setMouseTracking(true);
	setBackgroundBrush(QColor(64, 64, 64));
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setFocusPolicy(Qt::StrongFocus);   // needed for Enter (crop) / Esc / Space
}

void ScImageEditorView::zoomBy(double factor)
{
	double newZoom = qBound(0.05, m_zoom * factor, 40.0);
	if (qFuzzyCompare(newZoom, m_zoom))
		return;
	double s = newZoom / m_zoom;
	m_zoom = newZoom;
	scale(s, s);
	emit zoomChanged(m_zoom);
}

void ScImageEditorView::zoomActual()
{
	resetTransform();
	m_zoom = 1.0;
	emit zoomChanged(m_zoom);
}

void ScImageEditorView::zoomFit()
{
	if (!scene())
		return;
	fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
	m_zoom = transform().m11();
	emit zoomChanged(m_zoom);
}

void ScImageEditorView::setTool(Tool tool)
{
	if (m_tool == tool)
		return;
	m_tool = tool;
	updateCursor();
}

QRectF ScImageEditorView::selectionRect() const
{
	return m_selItem ? m_selItem->rect() : QRectF();
}

void ScImageEditorView::clearSelection()
{
	if (m_selItem)
	{
		scene()->removeItem(m_selItem);
		delete m_selItem;
		m_selItem = nullptr;
	}
	m_rubberActive = false;
	emit selectionChanged(QRectF());
}

void ScImageEditorView::updateCursor()
{
	switch (m_tool)
	{
		case Tool::Move:
		case Tool::Hand:       setCursor(Qt::OpenHandCursor); break;
		case Tool::Select:
		case Tool::Crop:       setCursor(Qt::CrossCursor); break;
		case Tool::Eyedropper: setCursor(Qt::PointingHandCursor); break;
		case Tool::Zoom:       setCursor(Qt::CrossCursor); break;
	}
}

QRectF ScImageEditorView::clampToScene(const QRectF& r) const
{
	return scene() ? r.intersected(scene()->sceneRect()) : r;
}

void ScImageEditorView::updateRubber(const QPointF& scenePos)
{
	if (!m_selItem)
		return;
	QRectF r = clampToScene(QRectF(m_rubberStart, scenePos).normalized());
	m_selItem->setRect(r);
}

void ScImageEditorView::wheelEvent(QWheelEvent* event)
{
	if (event->modifiers() & Qt::ControlModifier)
	{
		zoomBy(event->angleDelta().y() > 0 ? 1.25 : 0.8);
		event->accept();
		return;
	}
	QGraphicsView::wheelEvent(event);
}

void ScImageEditorView::mousePressEvent(QMouseEvent* event)
{
	// Space/middle-drag pan works regardless of the active tool. Left-drag pans
	// only for the legacy Move/Hand tools and only when no ImageTool is active.
	const bool wantPan = (event->button() == Qt::MiddleButton)
	                  || (m_spaceDown && event->button() == Qt::LeftButton)
	                  || (m_imageTool == nullptr && (m_tool == Tool::Move || m_tool == Tool::Hand) && event->button() == Qt::LeftButton);
	if (wantPan)
	{
		m_panning = true;
		m_lastPanPoint = event->pos();
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}

	// Route to the active ImageTool (marquee/lasso), if any.
	if (m_imageTool && event->button() == Qt::LeftButton)
	{
		m_imageTool->mousePress(event, mapToScene(event->pos()));
		event->accept();
		return;
	}

	if (event->button() == Qt::LeftButton)
	{
		const QPointF scenePos = mapToScene(event->pos());
		if (m_tool == Tool::Select || m_tool == Tool::Crop)
		{
			m_rubberActive = true;
			m_rubberStart = scenePos;
			if (!m_selItem)
			{
				QPen pen(Qt::DashLine);
				pen.setColor(Qt::white);
				pen.setCosmetic(true);
				m_selItem = scene()->addRect(QRectF(scenePos, scenePos), pen);
				m_selItem->setZValue(1000);
			}
			else
			{
				m_selItem->setRect(QRectF(scenePos, scenePos));
			}
			event->accept();
			return;
		}
		if (m_tool == Tool::Eyedropper)
		{
			emit colorPicked(scenePos.toPoint());
			event->accept();
			return;
		}
		if (m_tool == Tool::Zoom)
		{
			zoomBy((event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ? 0.8 : 1.25);
			event->accept();
			return;
		}
	}
	QGraphicsView::mousePressEvent(event);
}

void ScImageEditorView::mouseMoveEvent(QMouseEvent* event)
{
	emit cursorMoved(mapToScene(event->pos()));
	if (m_panning)
	{
		QPoint delta = event->pos() - m_lastPanPoint;
		m_lastPanPoint = event->pos();
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
		verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
		event->accept();
		return;
	}
	if (m_imageTool)
	{
		m_imageTool->mouseMove(event, mapToScene(event->pos()));
		event->accept();
		return;
	}
	if (m_rubberActive)
	{
		updateRubber(mapToScene(event->pos()));
		event->accept();
		return;
	}
	QGraphicsView::mouseMoveEvent(event);
}

void ScImageEditorView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton))
	{
		m_panning = false;
		updateCursor();
		event->accept();
		return;
	}
	if (m_imageTool && event->button() == Qt::LeftButton)
	{
		m_imageTool->mouseRelease(event, mapToScene(event->pos()));
		event->accept();
		return;
	}
	if (m_rubberActive && event->button() == Qt::LeftButton)
	{
		m_rubberActive = false;
		updateRubber(mapToScene(event->pos()));
		QRectF r = selectionRect();
		// Discard a stray click / too-small marquee.
		if (r.width() < 2.0 || r.height() < 2.0)
			clearSelection();
		else
			emit selectionChanged(r);
		event->accept();
		return;
	}
	QGraphicsView::mouseReleaseEvent(event);
}

void ScImageEditorView::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (m_imageTool && event->button() == Qt::LeftButton)
	{
		m_imageTool->mouseDoubleClick(event, mapToScene(event->pos()));
		event->accept();
		return;
	}
	if (m_tool == Tool::Crop && event->button() == Qt::LeftButton)
	{
		QRectF r = selectionRect();
		if (r.width() >= 2.0 && r.height() >= 2.0)
		{
			emit cropConfirmed(r);
			event->accept();
			return;
		}
	}
	QGraphicsView::mouseDoubleClickEvent(event);
}

void ScImageEditorView::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
	{
		m_spaceDown = true;
		if (!m_panning)
			setCursor(Qt::OpenHandCursor);
		event->accept();
		return;
	}
	// Give the active ImageTool first crack (Enter/Esc/Backspace for lassos).
	if (m_imageTool)
	{
		event->ignore();
		m_imageTool->keyPress(event);
		if (event->isAccepted())
			return;
	}
	if (m_tool == Tool::Crop && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
	{
		QRectF r = selectionRect();
		if (r.width() >= 2.0 && r.height() >= 2.0)
		{
			emit cropConfirmed(r);
			event->accept();
			return;
		}
	}
	if (event->key() == Qt::Key_Escape)
	{
		clearSelection();
		event->accept();
		return;
	}
	QGraphicsView::keyPressEvent(event);
}

void ScImageEditorView::keyReleaseEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
	{
		m_spaceDown = false;
		if (!m_panning)
			updateCursor();
		event->accept();
		return;
	}
	QGraphicsView::keyReleaseEvent(event);
}

// ────────────────────────────────────────────────────────────────────────────
// ScImageEditor
// ────────────────────────────────────────────────────────────────────────────

ScImageEditor::ScImageEditor(const QImage& image, PageItem_ImageFrame* frame, QWidget* parent)
	: QMainWindow(parent),
	  m_frame(frame)
{
	setWindowTitle(tr("Scribus Image Editor"));
	resize(1200, 800);   // default on first open; overridden by saved geometry if present

	m_undoStack = new QUndoStack(this);

	m_scene = new QGraphicsScene(this);
	m_pixmapItem = m_scene->addPixmap(QPixmap());
	m_view = new ScImageEditorView(m_scene, this);
	setCentralWidget(m_view);

	createActions();
	createMenus();
	createToolBar();
	createDockWidgets();
	createStatusBar();

	connect(m_view, &ScImageEditorView::zoomChanged, this, &ScImageEditor::updateZoomLabel);
	connect(m_view, &ScImageEditorView::cursorMoved, this, &ScImageEditor::updateCoordLabel);
	connect(m_toolGroup, &QActionGroup::triggered, this, &ScImageEditor::onToolChanged);
	connect(m_view, &ScImageEditorView::selectionChanged, this, &ScImageEditor::onSelectionChanged);
	connect(m_view, &ScImageEditorView::cropConfirmed, this, &ScImageEditor::onCropConfirmed);
	connect(m_view, &ScImageEditorView::colorPicked, this, &ScImageEditor::onColorPicked);

	m_originalImage = image;     // pristine source for non-destructive effects
	// Seed the stack from any effects already on the frame so the editor is a
	// true round-trip: what you open is what is applied, and Save & Apply won't
	// silently drop previously-applied effects.
	if (m_frame)
	{
		for (const ImageEffect& e : m_frame->effectsInUse)
			m_stack.append(makeStackEntry(e, true));
	}
	setSceneImage(image);        // sets scene rect + size label
	ensureSelection();           // create the (empty) selection + marching-ants overlay
	refreshStackList();
	renderEffects();             // display original with the seeded effect stack
	m_view->setTool(ScImageEditorView::Tool::Move);   // matches the checked Move action
	loadWindowSettings();        // restore saved geometry/state
	// The initial fit-in-view happens in showEvent(), once the viewport has a real size.
}

void ScImageEditor::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	if (m_firstShow)
	{
		m_firstShow = false;
		if (m_scene)
			m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
		// keep the view's zoom bookkeeping and status bar in sync
		m_view->zoomFit();
	}
}

void ScImageEditor::closeEvent(QCloseEvent* event)
{
	saveWindowSettings();
	QMainWindow::closeEvent(event);
}

void ScImageEditor::loadWindowSettings()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("ScImageEditor"));
	const QByteArray geom = settings.value(QStringLiteral("geometry")).toByteArray();
	const QByteArray state = settings.value(QStringLiteral("windowState")).toByteArray();
	settings.endGroup();
	if (!geom.isEmpty())
		restoreGeometry(geom);
	if (!state.isEmpty())
		restoreState(state);
}

void ScImageEditor::saveWindowSettings()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("ScImageEditor"));
	settings.setValue(QStringLiteral("geometry"), saveGeometry());
	settings.setValue(QStringLiteral("windowState"), saveState());
	settings.endGroup();
}

void ScImageEditor::createActions()
{
	IconManager& im = IconManager::instance();

	// Left toolbar — exclusive placeholder tools (no functionality yet)
	m_toolGroup = new QActionGroup(this);
	m_toolGroup->setExclusive(true);

	// Load a Scribus icon by id; if it isn't in the active icon set, fall back to a
	// Qt standard icon so a tool button is never blank.
	auto makeTool = [&](const QString& iconName, QStyle::StandardPixmap fallback, const QString& text) -> QAction* {
		QIcon ic = im.loadIcon(iconName, 24);
		if (ic.isNull())
			ic = style()->standardIcon(fallback);
		QAction* a = new QAction(ic, text, this);
		a->setCheckable(true);
		a->setToolTip(text);
		m_toolGroup->addAction(a);
		return a;
	};
	// User-provided Photoshop-style PNG tool icons, shipped in the iconset root
	// as <base>-24.png plus a <base>-48.png HiDPI variant. Loaded via addFile()
	// with explicit sizes because IconManager returns PNGs at native size and
	// never consults @2x siblings; QIcon then picks the right file per screen
	// device pixel ratio.
	auto makePngTool = [&](const QString& pngBase, QStyle::StandardPixmap fallback, const QString& text) -> QAction* {
		QIcon ic;
		QString p24 = im.pathForIcon(pngBase + "-24.png");
		QString p48 = im.pathForIcon(pngBase + "-48.png");
		if (!p24.isEmpty())
			ic.addFile(p24, QSize(24, 24));
		if (!p48.isEmpty())
			ic.addFile(p48, QSize(48, 48));
		if (ic.isNull())
			ic = style()->standardIcon(fallback);
		QAction* a = new QAction(ic, text, this);
		a->setCheckable(true);
		a->setToolTip(text);
		m_toolGroup->addAction(a);
		return a;
	};
	// tool-move, not align-mode-move: the latter is the Align & Distribute palette's glyph
	// and reads as "alignment", not the Photoshop 4-way move cursor.
	m_toolMove       = makeTool("tool-move",                 QStyle::SP_ArrowRight,             tr("Move"));
	m_toolSelect     = makeTool("tool-select",               QStyle::SP_FileDialogListView,     tr("Select"));
	m_toolCrop       = makeTool("transform-crop-and-resize", QStyle::SP_FileDialogDetailedView, tr("Crop"));
	m_toolEyedropper = makeTool("tool-color-picker",         QStyle::SP_DialogResetButton,      tr("Eyedropper"));
	m_toolZoom       = makeTool("tool-zoom",                 QStyle::SP_FileDialogContentsView, tr("Zoom"));
	m_toolHand       = makePngTool("tool-hand",              QStyle::SP_DesktopIcon,            tr("Hand"));
	// Selection tools (mask-based, via the ImageTool framework).
	m_toolRectMarquee    = makePngTool("select-rectangular", QStyle::SP_FileDialogListView,  tr("Rectangular Marquee"));
	m_toolEllipseMarquee = makePngTool("select-ellipse",     QStyle::SP_FileDialogListView,  tr("Elliptical Marquee"));
	m_toolLasso          = makePngTool("select-lasso",       QStyle::SP_FileDialogDetailedView, tr("Lasso"));
	m_toolPolyLasso      = makePngTool("select-polygon",     QStyle::SP_FileDialogDetailedView, tr("Polygonal Lasso"));
	m_toolSmartSelect    = makePngTool("select-smart",       QStyle::SP_DialogYesButton,     tr("Smart Select (SAM)"));
	m_toolRefineBrush    = makeTool("select-brush",       QStyle::SP_DialogResetButton,      tr("Refine Edges Brush"));
	m_rectMarqueeTool    = new RectMarqueeTool(this);
	m_ellipseMarqueeTool = new EllipseMarqueeTool(this);
	m_lassoTool          = new LassoTool(this);
	m_polyLassoTool      = new PolygonLassoTool(this);
	m_samTool            = new SamSelectTool(this);
	m_refineBrushTool    = new RefineEdgesBrushTool(this);

	// Photoshop-style single-key tool shortcuts (active while the editor is focused).
	m_toolMove->setShortcut(QKeySequence(Qt::Key_V));
	m_toolCrop->setShortcut(QKeySequence(Qt::Key_C));
	m_toolEyedropper->setShortcut(QKeySequence(Qt::Key_I));
	m_toolZoom->setShortcut(QKeySequence(Qt::Key_Z));
	m_toolHand->setShortcut(QKeySequence(Qt::Key_H));
	m_toolRectMarquee->setShortcut(QKeySequence(Qt::Key_M));
	m_toolEllipseMarquee->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
	m_toolLasso->setShortcut(QKeySequence(Qt::Key_L));
	m_toolPolyLasso->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
	m_toolSmartSelect->setShortcut(QKeySequence(Qt::Key_W));
	m_toolRefineBrush->setShortcut(QKeySequence(Qt::Key_R));
	m_toolMove->setChecked(true);

	// The sidebar is icon-only, so the tooltip is the only place the tool name appears —
	// fold the shortcut in too. Done after the shortcuts above are assigned.
	for (QAction* a : m_toolGroup->actions())
	{
		const QKeySequence sc = a->shortcut();
		a->setToolTip(sc.isEmpty() ? a->text()
		                           : QString("%1 (%2)").arg(a->text(), sc.toString(QKeySequence::NativeText)));
	}

	// File
	m_actClose = new QAction(tr("&Close"), this);
	m_actClose->setShortcut(QKeySequence::Close);
	connect(m_actClose, &QAction::triggered, this, &ScImageEditor::close);

	m_actSaveApply = new QAction(tr("&Save && Apply"), this);
	m_actSaveApply->setShortcut(QKeySequence::Save);
	connect(m_actSaveApply, &QAction::triggered, this, &ScImageEditor::saveAndApply);

	m_actExportFlattened = new QAction(tr("&Export Flattened Image..."), this);
	connect(m_actExportFlattened, &QAction::triggered, this, &ScImageEditor::exportFlattened);

	m_actRevert = new QAction(tr("&Revert"), this);
	connect(m_actRevert, &QAction::triggered, this, &ScImageEditor::revertImage);

	// Edit — undo/redo drive the effect-stack QUndoStack.
	m_actUndo = new QAction(tr("&Undo"), this);
	m_actUndo->setShortcut(QKeySequence::Undo);
	m_actUndo->setEnabled(m_undoStack->canUndo());
	connect(m_actUndo, &QAction::triggered, m_undoStack, &QUndoStack::undo);
	connect(m_undoStack, &QUndoStack::canUndoChanged, m_actUndo, &QAction::setEnabled);

	m_actRedo = new QAction(tr("&Redo"), this);
	m_actRedo->setShortcut(QKeySequence::Redo);
	m_actRedo->setEnabled(m_undoStack->canRedo());
	connect(m_actRedo, &QAction::triggered, m_undoStack, &QUndoStack::redo);
	connect(m_undoStack, &QUndoStack::canRedoChanged, m_actRedo, &QAction::setEnabled);

	// Image
	m_actImageSize = new QAction(tr("&Image Size..."), this);
	m_actImageSize->setShortcut(QKeySequence(Qt::ALT | Qt::CTRL | Qt::Key_I));
	connect(m_actImageSize, &QAction::triggered, this, &ScImageEditor::openImageSizeDialog);
	m_actCanvasSize = new QAction(tr("&Canvas Size..."), this);
	m_actCanvasSize->setShortcut(QKeySequence(Qt::ALT | Qt::CTRL | Qt::Key_C));
	connect(m_actCanvasSize, &QAction::triggered, this, &ScImageEditor::openCanvasSizeDialog);

	// View
	m_actZoomIn = new QAction(im.loadIcon("zoom-in"), tr("Zoom &In"), this);
	m_actZoomIn->setShortcut(QKeySequence::ZoomIn);
	connect(m_actZoomIn, &QAction::triggered, this, [this]{ m_view->zoomBy(1.25); });

	m_actZoomOut = new QAction(im.loadIcon("zoom-out"), tr("Zoom &Out"), this);
	m_actZoomOut->setShortcut(QKeySequence::ZoomOut);
	connect(m_actZoomOut, &QAction::triggered, this, [this]{ m_view->zoomBy(0.8); });

	m_actZoomFit = new QAction(tr("&Fit in Window"), this);
	m_actZoomFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
	connect(m_actZoomFit, &QAction::triggered, this, [this]{ m_view->zoomFit(); });

	m_actZoomActual = new QAction(im.loadIcon("zoom-original"), tr("Actual Size (&100%)"), this);
	m_actZoomActual->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
	connect(m_actZoomActual, &QAction::triggered, this, [this]{ m_view->zoomActual(); });
}

void ScImageEditor::createMenus()
{
	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	fileMenu->addAction(m_actSaveApply);
	fileMenu->addAction(m_actExportFlattened);
	fileMenu->addAction(m_actRevert);
	fileMenu->addSeparator();
	fileMenu->addAction(m_actClose);

	QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
	editMenu->addAction(m_actUndo);
	editMenu->addAction(m_actRedo);

	QMenu* imageMenu = menuBar()->addMenu(tr("&Image"));
	imageMenu->addAction(m_actImageSize);
	imageMenu->addAction(m_actCanvasSize);

	createSelectMenu();
	createFilterMenu();

	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
	viewMenu->addAction(m_actZoomIn);
	viewMenu->addAction(m_actZoomOut);
	viewMenu->addAction(m_actZoomFit);
	viewMenu->addAction(m_actZoomActual);
}

void ScImageEditor::createSelectMenu()
{
	QMenu* selMenu = menuBar()->addMenu(tr("&Select"));

	QAction* aAll = selMenu->addAction(tr("&All"), this, &ScImageEditor::selectAll);
	aAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
	QAction* aDes = selMenu->addAction(tr("&Deselect"), this, &ScImageEditor::selectDeselect);
	aDes->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
	QAction* aRes = selMenu->addAction(tr("&Reselect"), this, &ScImageEditor::selectReselect);
	aRes->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
	QAction* aInv = selMenu->addAction(tr("&Inverse"), this, &ScImageEditor::selectInverse);
	aInv->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));

	selMenu->addSeparator();
	QAction* aFeather = selMenu->addAction(tr("&Feather..."), this, &ScImageEditor::selectFeather);
	aFeather->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
	selMenu->addAction(tr("&Expand..."), this, &ScImageEditor::selectExpand);
	selMenu->addAction(tr("&Contract..."), this, &ScImageEditor::selectContract);
	selMenu->addAction(tr("&Smooth..."), this, &ScImageEditor::selectSmooth);

	selMenu->addSeparator();
	QAction* aLoad = selMenu->addAction(tr("&Load Selection"));
	aLoad->setEnabled(false);   // stub — Phase 4C
	QAction* aSave = selMenu->addAction(tr("Sa&ve Selection"));
	aSave->setEnabled(false);   // stub — Phase 4C
}

void ScImageEditor::selectAll()
{
	if (m_selection)
	{
		m_pendingSelectionLabel = tr("Select All");
		m_selection->selectAll();
	}
}

void ScImageEditor::selectDeselect()
{
	if (m_selection && !m_selection->isEmpty())
	{
		m_lastSelectionMask = m_selection->mask();   // remember for Reselect
		m_pendingSelectionLabel = tr("Deselect");
		m_selection->clear();
	}
}

void ScImageEditor::selectReselect()
{
	if (m_selection && !m_lastSelectionMask.isNull())
	{
		m_pendingSelectionLabel = tr("Reselect");
		m_selection->setFromMask(m_lastSelectionMask, ScImageSelection::Replace);
	}
}

void ScImageEditor::selectInverse()
{
	if (m_selection)
	{
		m_pendingSelectionLabel = tr("Inverse Selection");
		m_selection->invert();
	}
}

void ScImageEditor::selectFeather()
{
	if (!m_selection || m_selection->isEmpty())
		return;
	bool ok = false;
	double r = QInputDialog::getDouble(this, tr("Feather Selection"), tr("Radius (pixels):"),
		5.0, 0.1, 250.0, 1, &ok);
	if (ok)
	{
		m_pendingSelectionLabel = tr("Feather");
		m_selection->feather(r);
	}
}

void ScImageEditor::selectExpand()
{
	if (!m_selection || m_selection->isEmpty())
		return;
	bool ok = false;
	int px = QInputDialog::getInt(this, tr("Expand Selection"), tr("Expand by (pixels):"), 1, 1, 200, 1, &ok);
	if (ok)
	{
		m_pendingSelectionLabel = tr("Expand");
		m_selection->expand(px);
	}
}

void ScImageEditor::selectContract()
{
	if (!m_selection || m_selection->isEmpty())
		return;
	bool ok = false;
	int px = QInputDialog::getInt(this, tr("Contract Selection"), tr("Contract by (pixels):"), 1, 1, 200, 1, &ok);
	if (ok)
	{
		m_pendingSelectionLabel = tr("Contract");
		m_selection->contract(px);
	}
}

void ScImageEditor::selectSmooth()
{
	if (!m_selection || m_selection->isEmpty())
		return;
	bool ok = false;
	int r = QInputDialog::getInt(this, tr("Smooth Selection"), tr("Radius (pixels):"), 2, 1, 100, 1, &ok);
	if (ok)
	{
		m_pendingSelectionLabel = tr("Smooth");
		m_selection->smooth(r);
	}
}

void ScImageEditor::populateFilterMenu(QMenu* menu, bool primary)
{
	// Add a leaf filter action to `sub`, connect it, and track it so it can be
	// grayed out when there is no image to work on. Shortcuts are assigned only
	// on the primary (menu-bar) copy — the "Add Filter" button reuses this same
	// builder, and duplicating a shortcut would make it ambiguous.
	auto addFilter = [&](QMenu* sub, const QString& text, std::function<void()> fn,
	                     const QKeySequence& sc = QKeySequence()) -> QAction* {
		QAction* a = sub->addAction(text);
		connect(a, &QAction::triggered, this, fn);
		if (primary && !sc.isEmpty())
			a->setShortcut(sc);
		m_filterActions.append(a);
		return a;
	};

	// Auto colour correction (above the manual Adjustments group).
	QMenu* autoMenu = menu->addMenu(tr("Auto"));
	addFilter(autoMenu, tr("Auto Tone"), [this]{
		ScImageEffectList l; l.append(AutoCorrectEngine::makeAutoTone()); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
	addFilter(autoMenu, tr("Auto Contrast"), [this]{
		ScImageEffectList l; l.append(AutoCorrectEngine::makeAutoContrast()); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::ALT | Qt::Key_L));
	addFilter(autoMenu, tr("Auto Color"), [this]{
		ScImageEffectList l; l.append(AutoCorrectEngine::makeAutoColor()); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
	autoMenu->addSeparator();
	addFilter(autoMenu, tr("Auto Enhance"), [this]{
		ScImageEffectList l; l.append(AutoCorrectEngine::makeAutoEnhance(AutoCorrectOptions())); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E));
	autoMenu->addSeparator();
	addFilter(autoMenu, tr("Auto CMYK Optimize"), [this]{
		ScImageEffectList l; l.append(AutoCorrectEngine::makeAutoCmyk(CmykOptimizeOptions())); commitEffects(l); });
	autoMenu->addSeparator();
	addFilter(autoMenu, tr("Auto Enhance Options..."), [this]{ runFilterDialog(new AutoEnhanceDialog(this)); });

	QMenu* blurMenu = menu->addMenu(tr("Blur"));
	addFilter(blurMenu, tr("Gaussian Blur..."), [this]{ runFilterDialog(new GaussianBlurDialog(this)); });
	addFilter(blurMenu, tr("Motion Blur..."), [this]{ runFilterDialog(new MotionBlurDialog(this)); });
	addFilter(blurMenu, tr("Radial Blur..."), [this]{ runFilterDialog(new RadialBlurDialog(this)); });
	addFilter(blurMenu, tr("Box Blur..."), [this]{ runFilterDialog(new BoxBlurDialog(this)); });

	QMenu* sharpenMenu = menu->addMenu(tr("Sharpen"));
	addFilter(sharpenMenu, tr("Sharpen..."), [this]{ runFilterDialog(new SharpenDialog(this)); });

	QMenu* adjMenu = menu->addMenu(tr("Adjustments"));
	addFilter(adjMenu, tr("Brightness/Contrast..."), [this]{ runFilterDialog(new BrightnessContrastDialog(-1, this)); },
		QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
	addFilter(adjMenu, tr("Levels..."), [this]{ runFilterDialog(new LevelsDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::Key_L));
	addFilter(adjMenu, tr("Curves..."), [this]{ runFilterDialog(new CurvesDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::Key_M));
	addFilter(adjMenu, tr("Shadows/Highlights..."), [this]{ runFilterDialog(new ShadowsHighlightsDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
	addFilter(adjMenu, tr("Hue/Saturation..."), [this]{ runFilterDialog(new HueSaturationDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::Key_U));
	addFilter(adjMenu, tr("Color Balance..."), [this]{ runFilterDialog(new ColorBalanceDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::Key_B));
	addFilter(adjMenu, tr("CMYK Adjustments..."), [this]{ runFilterDialog(new CmykAdjustDialog(this)); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
	addFilter(adjMenu, tr("Selective Color..."), [this]{ runFilterDialog(new SelectiveColorDialog(this)); });
	addFilter(adjMenu, tr("Channel Mixer..."), [this]{ runFilterDialog(new ChannelMixerDialog(this)); });
	addFilter(adjMenu, tr("Photo Filter..."), [this]{ runFilterDialog(new PhotoFilterDialog(this)); });
	addFilter(adjMenu, tr("Posterize..."), [this]{ runFilterDialog(new PosterizeDialog(this)); });
	addFilter(adjMenu, tr("Threshold..."), [this]{ runFilterDialog(new ThresholdDialog(this)); });
	addFilter(adjMenu, tr("Black && White..."), [this]{ runFilterDialog(new BlackWhiteDialog(this)); });
	adjMenu->addSeparator();
	addFilter(adjMenu, tr("Invert"), [this]{
		ScImageEffectList l; l.append(ImageFilterEngine::makeInvert()); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::Key_I));
	addFilter(adjMenu, tr("Grayscale"), [this]{
		ScImageEffectList l; l.append(ImageFilterEngine::makeGrayscale()); commitEffects(l); },
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));

	QMenu* colorMenu = menu->addMenu(tr("Color"));
	addFilter(colorMenu, tr("Colorize..."), [this]{
		runFilterDialog(new ColorizeDialog(m_frame ? m_frame->doc() : nullptr, this)); });
	addFilter(colorMenu, tr("Duotone..."), [this]{
		runFilterDialog(new DuotoneDialog(m_frame ? m_frame->doc() : nullptr, this)); });
	addFilter(colorMenu, tr("Tritone..."), [this]{
		runFilterDialog(new TritoneDialog(m_frame ? m_frame->doc() : nullptr, this)); });
	addFilter(colorMenu, tr("Quadtone..."), [this]{
		runFilterDialog(new QuadtoneDialog(m_frame ? m_frame->doc() : nullptr, this)); });
}

void ScImageEditor::createFilterMenu()
{
	m_filterMenu = menuBar()->addMenu(tr("F&ilter"));
	populateFilterMenu(m_filterMenu, true);   // primary copy owns the shortcuts
	m_filterMenu->addSeparator();
	m_actRemoveAllFilters = m_filterMenu->addAction(tr("Remove All Filters"));
	connect(m_actRemoveAllFilters, &QAction::triggered, this, &ScImageEditor::removeAllFilters);
}

// ── Effect stack ────────────────────────────────────────────────────────────

ColorList& ScImageEditor::docColors()
{
	static ColorList s_emptyColors;
	if (m_frame && m_frame->doc())
		return m_frame->doc()->PageColors;
	return s_emptyColors;
}

ScImageEditor::StackEntry ScImageEditor::makeStackEntry(const ImageEffect& effect, bool visible, const QImage& mask) const
{
	StackEntry entry;
	entry.effect = effect;
	entry.visible = visible;
	entry.name = filterEffectName(effect.effectCode);
	entry.summary = filterEffectSummary(effect);
	entry.mask = mask;
	if (!mask.isNull())
		entry.summary += (entry.summary.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("selection");
	return entry;
}

ScImageEffectList ScImageEditor::effectiveEffects() const
{
	ScImageEffectList list;
	for (const StackEntry& e : m_stack)
	{
		if (e.visible)
			list.append(e.effect);
	}
	return list;
}

QImage ScImageEditor::currentSelectionMask() const
{
	if (m_selection && !m_selection->isEmpty())
		return m_selection->mask();
	return QImage();
}

bool ScImageEditor::hasMaskedEffects() const
{
	for (const StackEntry& e : m_stack)
	{
		if (e.visible && !e.mask.isNull())
			return true;
	}
	return false;
}

namespace
{
	// result = base*(1 - a) + effected*a, where a = mask/255 (Alpha8, same size).
	QImage blendMasked(const QImage& base, const QImage& effected, const QImage& mask)
	{
		QImage b = base.convertToFormat(QImage::Format_ARGB32);
		QImage f = effected.convertToFormat(QImage::Format_ARGB32);
		int w = qMin(b.width(), f.width());
		int h = qMin(b.height(), f.height());
		if (mask.width() < w || mask.height() < h)
		{
			f.setDotsPerMeterX(base.dotsPerMeterX());
			f.setDotsPerMeterY(base.dotsPerMeterY());
			return f;   // mask too small — fall back to full application
		}
		for (int y = 0; y < h; ++y)
		{
			QRgb* bp = reinterpret_cast<QRgb*>(b.scanLine(y));
			const QRgb* fp = reinterpret_cast<const QRgb*>(f.constScanLine(y));
			const uchar* mp = mask.constScanLine(y);
			for (int x = 0; x < w; ++x)
			{
				int a = mp[x];
				if (a == 0)
					continue;                 // keep base
				if (a >= 255)
				{
					bp[x] = fp[x];            // fully effected
					continue;
				}
				int ia = 255 - a;
				int nr = (qRed(bp[x])   * ia + qRed(fp[x])   * a) / 255;
				int ng = (qGreen(bp[x]) * ia + qGreen(fp[x]) * a) / 255;
				int nb = (qBlue(bp[x])  * ia + qBlue(fp[x])  * a) / 255;
				bp[x] = qRgba(nr, ng, nb, qAlpha(bp[x]));
			}
		}
		// Preserve DPI from the original base image
		b.setDotsPerMeterX(base.dotsPerMeterX());
		b.setDotsPerMeterY(base.dotsPerMeterY());
		return b;
	}
}

QImage ScImageEditor::applyStackToImage(const QList<StackEntry>& entries)
{
	if (m_originalImage.isNull())
		return m_originalImage;
	return applyEntriesTo(m_originalImage, entries);
}

QImage ScImageEditor::applyEntriesTo(QImage base, const QList<StackEntry>& entries)
{
	QImage cur = base;
	ColorList& colors = docColors();
	for (const StackEntry& e : entries)
	{
		if (!e.visible)
			continue;
		ScImageEffectList one;
		one.append(e.effect);
		QImage effected = ImageFilterEngine::applyEffects(cur, one, colors, false);
		// Preserve DPI through effect application and masking
		effected.setDotsPerMeterX(cur.dotsPerMeterX());
		effected.setDotsPerMeterY(cur.dotsPerMeterY());
		cur = e.mask.isNull() ? effected : blendMasked(cur, effected, e.mask);
	}
	return cur;
}

QImage ScImageEditor::stackPrefixImage(int count)
{
	count = qBound(0, count, static_cast<int>(m_stack.size()));
	if (m_stackPrefixCount == count && !m_stackPrefixCache.isNull())
		return m_stackPrefixCache;
	m_stackPrefixCache = applyStackToImage(m_stack.mid(0, count));
	m_stackPrefixCount = count;
	return m_stackPrefixCache;
}

void ScImageEditor::invalidateStackPrefixCache()
{
	m_stackPrefixCache = QImage();
	m_stackPrefixCount = -1;
}

void ScImageEditor::setActiveTool(ImageTool* tool)
{
	if (m_activeTool == tool)
		return;
	if (m_activeTool)
		m_activeTool->deactivate();
	m_activeTool = tool;
	if (m_view)
		m_view->setActiveImageTool(tool);
	if (tool)
	{
		tool->activate(this);
		if (m_view)
			m_view->viewport()->setCursor(tool->cursor());
	}
	updateToolOptionsBar();
}

void ScImageEditor::ensureSelection()
{
	if (m_originalImage.isNull())
		return;
	const QSize sz = m_originalImage.size();
	if (m_selection && m_selection->size() == sz)
		return;   // already the right size
	// (Re)create the selection + marching-ants overlay for the new base size.
	if (m_marchingAnts)
	{
		if (m_scene)
			m_scene->removeItem(m_marchingAnts);
		delete m_marchingAnts;
		m_marchingAnts = nullptr;
	}
	delete m_selection;
	m_selection = new ScImageSelection(sz, this);
	m_marchingAnts = new MarchingAntsItem(m_selection);
	if (m_scene)
		m_scene->addItem(m_marchingAnts);
	// Track selection edits for the undo history (baseline = the new empty mask).
	connect(m_selection, &ScImageSelection::changed, this, &ScImageEditor::onSelectionEdited);
	m_selectionUndoBaseline = m_selection->mask();
}

void ScImageEditor::onSelectionEdited()
{
	if (!m_selection)
		return;
	QImage after = m_selection->mask();
	if (m_suppressSelectionUndo)
	{
		m_selectionUndoBaseline = after;   // change came from an undo/redo; just track it
		return;
	}
	if (after == m_selectionUndoBaseline)
		return;   // no actual change
	const QString label = m_pendingSelectionLabel.isEmpty() ? tr("Selection") : m_pendingSelectionLabel;
	if (m_undoStack)
		m_undoStack->push(new SelectionChangeCommand(this, m_selectionUndoBaseline, after, label));
	m_selectionUndoBaseline = after;
	m_pendingSelectionLabel.clear();
}

void ScImageEditor::applySelectionMask(const QImage& mask)
{
	if (!m_selection)
		return;
	m_suppressSelectionUndo = true;
	m_selection->setFromMask(mask, ScImageSelection::Replace);
	m_selectionUndoBaseline = m_selection->mask();
	m_suppressSelectionUndo = false;
}

void ScImageEditor::beginSelectionStroke()
{
	if (!m_selection)
		return;
	m_strokeStartMask = m_selection->mask();
	m_suppressSelectionUndo = true;   // don't push per-dab; one command at stroke end
}

void ScImageEditor::endSelectionStroke(const QString& label)
{
	if (!m_selection)
		return;
	m_suppressSelectionUndo = false;
	QImage after = m_selection->mask();
	if (after != m_strokeStartMask && m_undoStack)
		m_undoStack->push(new SelectionChangeCommand(this, m_strokeStartMask, after, label));
	m_selectionUndoBaseline = after;
	m_strokeStartMask = QImage();
}

void ScImageEditor::renderEffects()
{
	if (m_originalImage.isNull())
		return;
	setSceneImage(applyStackToImage(m_stack));   // per-entry masking applied here
	updateFilterActions();
}

void ScImageEditor::setPreviewImage(const QImage& preview)
{
	// Temporary display only: update the pixmap + size readout, but leave
	// m_image (the committed result) untouched so Cancel can restore cleanly.
	if (m_pixmapItem)
		m_pixmapItem->setPixmap(QPixmap::fromImage(preview));
	if (m_sizeLabel)
		m_sizeLabel->setText(tr("%1 x %2 px").arg(preview.width()).arg(preview.height()));
}

void ScImageEditor::clearPreview()
{
	renderEffects();   // redisplay the committed stack (restores m_image + pixmap)
}

void ScImageEditor::commitPreviewAsFilter(const ScImageEffectList& effects)
{
	commitEffects(effects);
}

void ScImageEditor::previewEffects(const ScImageEffectList& extra)
{
	if (m_originalImage.isNull())
		return;
	// The pending (Add) filter uses the CURRENT selection as its mask. Only
	// the new effect is computed per preview tick; the committed stack comes
	// from the prefix cache.
	const QImage mask = currentSelectionMask();
	QList<StackEntry> pending;
	for (const ImageEffect& e : extra)
		pending.append(makeStackEntry(e, true, mask));
	setPreviewImage(applyEntriesTo(stackPrefixImage(m_stack.size()), pending));
}

void ScImageEditor::previewReplace(int row, const ScImageEffectList& repl)
{
	if (m_originalImage.isNull())
		return;
	if (row < 0 || row >= m_stack.size())
	{
		setPreviewImage(applyStackToImage(m_stack));
		return;
	}
	// Editing an existing entry keeps that entry's own mask. Entries before
	// the edited row come from the prefix cache; the replacement plus the
	// entries after it are recomputed per preview tick (they depend on it).
	const QImage mask = m_stack.at(row).mask;
	QList<StackEntry> tail;
	if (!repl.isEmpty())
	{
		for (int k = 0; k < repl.size(); ++k)
			tail.append(makeStackEntry(repl.at(k), true, mask));   // shown even if the entry was hidden
	}
	for (int k = row + 1; k < m_stack.size(); ++k)
		tail.append(m_stack.at(k));
	setPreviewImage(applyEntriesTo(stackPrefixImage(row), tail));
}

void ScImageEditor::applyStackState(const QList<StackEntry>& state)
{
	m_stack = state;
	invalidateStackPrefixCache();
	refreshStackList();
	renderEffects();
}

void ScImageEditor::commitStackChange(const QString& text, const QList<StackEntry>& newState)
{
	if (m_undoStack)
		m_undoStack->push(new StackChangeCommand(this, m_stack, newState, text));   // push() calls redo() → applyStackState(newState)
	else
		applyStackState(newState);
}

void ScImageEditor::commitEffects(const ScImageEffectList& extra)
{
	if (extra.isEmpty())
	{
		renderEffects();
		return;
	}
	QList<StackEntry> newState = m_stack;
	QStringList names;
	const QImage mask = currentSelectionMask();   // bake the active selection into these entries
	for (const ImageEffect& e : extra)
	{
		newState.append(makeStackEntry(e, true, mask));
		names << filterEffectName(e.effectCode);
	}
	commitStackChange(tr("Add %1").arg(names.join(QStringLiteral(", "))), newState);
}

void ScImageEditor::replaceStackEntry(int row, const ScImageEffectList& repl)
{
	if (row < 0 || row >= m_stack.size())
		return;
	QList<StackEntry> newState = m_stack;
	const bool vis = newState[row].visible;
	const QString name = newState[row].name;
	const QImage mask = newState[row].mask;   // editing keeps the entry's mask
	if (repl.isEmpty())
	{
		newState.removeAt(row);
	}
	else
	{
		newState[row] = makeStackEntry(repl.at(0), vis, mask);
		for (int k = 1; k < repl.size(); ++k)
			newState.insert(row + k, makeStackEntry(repl.at(k), vis, mask));
	}
	commitStackChange(tr("Edit %1").arg(name), newState);
}

void ScImageEditor::runFilterDialog(FilterParamDialog* dlg)
{
	dlg->setSourceImage(m_originalImage);   // context for Auto + preview
	dlg->onPreview    = [this](const ScImageEffectList& list){ previewEffects(list); };
	dlg->onPreviewOff = [this]{ clearPreview(); };
	dlg->requestInitialPreview();
	if (dlg->exec() == QDialog::Accepted)
		commitPreviewAsFilter(dlg->buildEffects());
	else
		clearPreview();   // discard preview, restore the committed stack
	delete dlg;
}

void ScImageEditor::removeAllFilters()
{
	if (m_stack.isEmpty())
		return;
	commitStackChange(tr("Remove All Filters"), QList<StackEntry>());
}

void ScImageEditor::updateFilterActions()
{
	const bool hasImage = !m_originalImage.isNull();
	for (QAction* a : m_filterActions)
		a->setEnabled(hasImage);
	if (m_actRemoveAllFilters)
		m_actRemoveAllFilters->setEnabled(hasImage && !m_stack.isEmpty());
}

// ── Adjustments dock — stack UI ──────────────────────────────────────────────

int ScImageEditor::currentStackRow() const
{
	return m_stackList ? m_stackList->currentRow() : -1;
}

void ScImageEditor::refreshStackList()
{
	if (!m_stackList)
		return;
	const int keepRow = m_stackList->currentRow();
	QSignalBlocker blocker(m_stackList);
	m_stackList->clear();
	for (const StackEntry& e : m_stack)
	{
		QString text = e.name;
		if (!e.summary.isEmpty())
			text += QStringLiteral(" — ") + e.summary;
		auto* item = new QListWidgetItem(text, m_stackList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(e.visible ? Qt::Checked : Qt::Unchecked);
		item->setToolTip(tr("Uncheck to hide this filter"));
	}
	if (keepRow >= 0 && keepRow < m_stackList->count())
		m_stackList->setCurrentRow(keepRow);
	updateStackButtons();
	updateFilterActions();
}

void ScImageEditor::onStackItemChanged(QListWidgetItem* item)
{
	if (!item || !m_stackList)
		return;
	const int row = m_stackList->row(item);
	if (row < 0 || row >= m_stack.size())
		return;
	const bool visible = (item->checkState() == Qt::Checked);
	if (m_stack[row].visible == visible)
		return;
	QList<StackEntry> newState = m_stack;
	newState[row].visible = visible;
	commitStackChange(visible ? tr("Show %1").arg(m_stack[row].name)
	                          : tr("Hide %1").arg(m_stack[row].name), newState);
}

void ScImageEditor::updateStackButtons()
{
	const int row = currentStackRow();
	const bool has = (row >= 0 && row < m_stack.size());
	if (m_removeBtn) m_removeBtn->setEnabled(has);
	if (m_upBtn)     m_upBtn->setEnabled(has && row > 0);
	if (m_downBtn)   m_downBtn->setEnabled(has && row < m_stack.size() - 1);
	// Some effects have no editable parameters (parameterless or recomputing).
	bool editable = false;
	if (has)
	{
		const int code = m_stack[row].effect.effectCode;
		editable = (code != ImageEffect::EF_INVERT && code != ImageEffect::EF_GRAYSCALE
			&& code != ImageEffect::EF_AUTOTONE && code != ImageEffect::EF_AUTOCONTRAST
			&& code != ImageEffect::EF_AUTOCOLOR && code != ImageEffect::EF_AUTOCMYK);
	}
	if (m_editBtn) m_editBtn->setEnabled(editable);
}

void ScImageEditor::editSelectedEffect()
{
	const int row = currentStackRow();
	if (row < 0 || row >= m_stack.size())
		return;
	const ImageEffect orig = m_stack[row].effect;
	FilterParamDialog* dlg = makeFilterDialogForEffect(orig, m_frame ? m_frame->doc() : nullptr, this);
	if (!dlg)
		return;   // parameterless effect
	dlg->setSourceImage(m_originalImage);
	dlg->onPreview    = [this, row](const ScImageEffectList& list){ previewReplace(row, list); };
	dlg->onPreviewOff = [this]{ clearPreview(); };
	dlg->requestInitialPreview();
	if (dlg->exec() == QDialog::Accepted)
		replaceStackEntry(row, dlg->buildEffects());
	else
		clearPreview();
	delete dlg;
}

void ScImageEditor::removeSelectedEffect()
{
	const int row = currentStackRow();
	if (row < 0 || row >= m_stack.size())
		return;
	const QString name = m_stack[row].name;
	QList<StackEntry> newState = m_stack;
	newState.removeAt(row);
	commitStackChange(tr("Remove %1").arg(name), newState);
}

void ScImageEditor::moveSelectedEffectUp()
{
	const int row = currentStackRow();
	if (row <= 0 || row >= m_stack.size())
		return;
	QList<StackEntry> newState = m_stack;
	newState.move(row, row - 1);
	commitStackChange(tr("Move %1 up").arg(m_stack[row].name), newState);
	if (m_stackList)
		m_stackList->setCurrentRow(row - 1);
}

void ScImageEditor::moveSelectedEffectDown()
{
	const int row = currentStackRow();
	if (row < 0 || row >= m_stack.size() - 1)
		return;
	QList<StackEntry> newState = m_stack;
	newState.move(row, row + 1);
	commitStackChange(tr("Move %1 down").arg(m_stack[row].name), newState);
	if (m_stackList)
		m_stackList->setCurrentRow(row + 1);
}

void ScImageEditor::createToolBar()
{
	QToolBar* toolBar = new QToolBar(tr("Tools"), this);
	toolBar->setObjectName(QStringLiteral("ScImageEditorTools"));
	toolBar->setOrientation(Qt::Vertical);
	toolBar->setMovable(false);
	// Photoshop-style tool column: icons only, the name and shortcut live in the tooltip.
	toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	toolBar->setIconSize(QSize(24, 24));
	toolBar->setFixedWidth(48);
	// Checked = active tool, hover = feedback; both taken from the palette so the strip
	// follows whatever Scribus theme (light or dark) is in use.
	toolBar->setStyleSheet(
		"QToolBar { spacing: 2px; padding: 3px; }"
		"QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 3px; }"
		"QToolButton:hover { background: palette(midlight); }"
		"QToolButton:checked { background: palette(mid); border-color: palette(shadow); }");
	// Grouped the way Photoshop groups them: navigation, selection, retouch.
	const QList<QAction*> navTools = { m_toolMove, m_toolSelect, m_toolCrop, m_toolEyedropper, m_toolZoom, m_toolHand };
	for (QAction* a : navTools)
		toolBar->addAction(a);
	toolBar->addSeparator();
	const QList<QAction*> selectTools = { m_toolRectMarquee, m_toolEllipseMarquee, m_toolLasso, m_toolPolyLasso, m_toolSmartSelect };
	for (QAction* a : selectTools)
		toolBar->addAction(a);
	toolBar->addSeparator();
	toolBar->addAction(m_toolRefineBrush);
	addToolBar(Qt::LeftToolBarArea, toolBar);

	// Photoshop-style Options Bar (below the menu bar) for the legacy tools.
	// It swaps its content when the active tool changes; ImageTool-based tools
	// use the separate m_toolOptionsBar below instead.
	m_optionsBar = new OptionsBar(this);
	m_moveOptions = new MoveOptionsWidget(this);
	m_cropOptions = new CropOptionsWidget(this);
	m_optionsBar->registerToolOptions(QStringLiteral("Move"), m_moveOptions);
	m_optionsBar->registerToolOptions(QStringLiteral("Crop"), m_cropOptions);
	m_optionsBar->registerToolOptions(QStringLiteral("Select"),
		new StubToolOptionsWidget(QStringLiteral("Select"), tr("Drag to select a rectangular region."), this));
	m_optionsBar->registerToolOptions(QStringLiteral("Eyedropper"),
		new StubToolOptionsWidget(QStringLiteral("Eyedropper"), tr("Click the image to sample a color."), this));
	m_optionsBar->registerToolOptions(QStringLiteral("Zoom"),
		new StubToolOptionsWidget(QStringLiteral("Zoom"), tr("Click to zoom; Ctrl+wheel to zoom at the cursor."), this));
	m_optionsBar->registerToolOptions(QStringLiteral("Hand"),
		new StubToolOptionsWidget(QStringLiteral("Hand"), tr("Drag to pan the canvas."), this));
	addToolBar(Qt::TopToolBarArea, m_optionsBar);
	connect(m_cropOptions, &CropOptionsWidget::commitRequested, this, &ScImageEditor::onCropCommit);
	connect(m_cropOptions, &CropOptionsWidget::cancelRequested, this, &ScImageEditor::onCropCancel);

	// Top options bar — populated with the active ImageTool's controls on demand.
	addToolBarBreak(Qt::TopToolBarArea);
	m_toolOptionsBar = new QToolBar(tr("Tool Options"), this);
	m_toolOptionsBar->setObjectName(QStringLiteral("ScImageEditorToolOptions"));
	m_toolOptionsBar->setMovable(false);
	addToolBar(Qt::TopToolBarArea, m_toolOptionsBar);
	m_toolOptionsBar->hide();

	// Match the initial Move tool.
	m_optionsBar->setActiveTool(QStringLiteral("Move"));
}

void ScImageEditor::updateToolOptionsBar()
{
	if (!m_toolOptionsBar)
		return;
	m_toolOptionsBar->clear();   // deletes the previous tool's option widget
	QWidget* opts = m_activeTool ? m_activeTool->optionsBar() : nullptr;
	if (opts)
	{
		m_toolOptionsBar->addWidget(opts);
		m_toolOptionsBar->show();
	}
	else
	{
		m_toolOptionsBar->hide();
	}
}

QWidget* ScImageEditor::buildAdjustmentsPanel()
{
	auto* panel = new QWidget(this);
	panel->setMinimumWidth(240);
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(4, 4, 4, 4);

	// "Add Filter ▾" split-button whose menu mirrors the Filter menu.
	auto* addBtn = new QToolButton(panel);
	addBtn->setText(tr("Add Filter"));
	addBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	addBtn->setPopupMode(QToolButton::InstantPopup);
	auto* addMenu = new QMenu(addBtn);
	populateFilterMenu(addMenu);
	addBtn->setMenu(addMenu);
	layout->addWidget(addBtn);

	m_stackList = new QListWidget(panel);
	m_stackList->setSelectionMode(QAbstractItemView::SingleSelection);
	layout->addWidget(m_stackList, 1);

	// Edit / Remove / Up / Down row.
	auto* btnRow = new QHBoxLayout();
	m_editBtn   = new QPushButton(tr("Edit"), panel);
	m_removeBtn = new QPushButton(tr("Remove"), panel);
	m_upBtn     = new QPushButton(tr("Up"), panel);
	m_downBtn   = new QPushButton(tr("Down"), panel);
	btnRow->addWidget(m_editBtn);
	btnRow->addWidget(m_removeBtn);
	btnRow->addWidget(m_upBtn);
	btnRow->addWidget(m_downBtn);
	layout->addLayout(btnRow);

	connect(m_stackList, &QListWidget::itemChanged, this, &ScImageEditor::onStackItemChanged);
	connect(m_stackList, &QListWidget::currentRowChanged, this, [this]{ updateStackButtons(); });
	connect(m_stackList, &QListWidget::itemDoubleClicked, this, &ScImageEditor::editSelectedEffect);
	connect(m_editBtn,   &QPushButton::clicked, this, &ScImageEditor::editSelectedEffect);
	connect(m_removeBtn, &QPushButton::clicked, this, &ScImageEditor::removeSelectedEffect);
	connect(m_upBtn,     &QPushButton::clicked, this, &ScImageEditor::moveSelectedEffectUp);
	connect(m_downBtn,   &QPushButton::clicked, this, &ScImageEditor::moveSelectedEffectDown);

	updateStackButtons();
	return panel;
}

void ScImageEditor::createDockWidgets()
{
	auto makeDock = [&](const QString& title, const QString& objName, QWidget* content) -> QDockWidget* {
		QDockWidget* dock = new QDockWidget(title, this);
		dock->setObjectName(objName);
		dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
		if (!content)
		{
			content = new QWidget(dock);
			content->setMinimumWidth(220);
		}
		dock->setWidget(content);
		addDockWidget(Qt::RightDockWidgetArea, dock);
		return dock;
	};
	// Layers — a single, non-editable "Background" entry for now.
	auto* layersList = new QListWidget(this);
	layersList->setMinimumWidth(220);
	auto* bg = new QListWidgetItem(tr("Background"), layersList);
	bg->setFlags(bg->flags() & ~Qt::ItemIsEditable);
	layersList->setCurrentItem(bg);

	// History — a live view of the effect-stack undo history.
	auto* historyView = new QUndoView(m_undoStack, this);
	historyView->setMinimumWidth(220);
	historyView->setEmptyLabel(tr("Original Image"));

	QDockWidget* layersDock      = makeDock(tr("Layers"),      QStringLiteral("ScImageEditorLayers"), layersList);
	QDockWidget* historyDock     = makeDock(tr("History"),     QStringLiteral("ScImageEditorHistory"), historyView);
	QDockWidget* adjustmentsDock = makeDock(tr("Adjustments"), QStringLiteral("ScImageEditorAdjustments"), buildAdjustmentsPanel());
	// One tabbed panel on the right holding all three; Adjustments is the default tab.
	tabifyDockWidget(layersDock, historyDock);
	tabifyDockWidget(historyDock, adjustmentsDock);
	adjustmentsDock->raise();
}

void ScImageEditor::createStatusBar()
{
	m_zoomLabel   = new QLabel(this);
	m_coordsLabel = new QLabel(this);
	m_sizeLabel   = new QLabel(this);
	m_swatchLabel = new QLabel(this);
	m_swatchLabel->setFixedSize(16, 16);
	m_swatchLabel->setFrameShape(QFrame::Box);
	m_swatchLabel->setAutoFillBackground(true);
	m_swatchLabel->setToolTip(tr("Eyedropper sample"));
	m_colorLabel  = new QLabel(this);
	statusBar()->addPermanentWidget(m_swatchLabel);
	statusBar()->addPermanentWidget(m_colorLabel);
	statusBar()->addPermanentWidget(m_zoomLabel);
	statusBar()->addPermanentWidget(m_coordsLabel);
	statusBar()->addPermanentWidget(m_sizeLabel);
	updateZoomLabel(m_view->zoomFactor());
	updateCoordLabel(QPointF(0, 0));
}

void ScImageEditor::setSceneImage(const QImage& image)
{
	m_image = image;
	if (m_pixmapItem)
		m_pixmapItem->setPixmap(QPixmap::fromImage(m_image));
	// Size the scene to the pixmap so fitInView() works correctly.
	if (m_scene && m_pixmapItem)
		m_scene->setSceneRect(m_pixmapItem->boundingRect());
	if (m_sizeLabel)
		m_sizeLabel->setText(tr("%1 x %2 px").arg(m_image.width()).arg(m_image.height()));
	// Keep the Crop tool's "Original Ratio" preset in sync with the image.
	if (m_cropOptions && m_image.height() > 0)
		m_cropOptions->setImageAspect(static_cast<double>(m_image.width()) / m_image.height());
}

void ScImageEditor::updateZoomLabel(double factor)
{
	if (m_zoomLabel)
		m_zoomLabel->setText(tr("Zoom: %1%").arg(qRound(factor * 100.0)));
}

void ScImageEditor::updateCoordLabel(const QPointF& scenePos)
{
	if (m_coordsLabel)
		m_coordsLabel->setText(tr("X: %1  Y: %2").arg(qRound(scenePos.x())).arg(qRound(scenePos.y())));
}

// ── Tools (Phase 3) ──────────────────────────────────────────────────────────

namespace
{
	QColor averageColor(const QImage& img, const QRect& rect)
	{
		QRect r = rect.intersected(img.rect());
		if (r.isEmpty())
			return QColor();
		qulonglong rs = 0, gs = 0, bs = 0, n = 0;
		for (int y = r.top(); y <= r.bottom(); ++y)
		{
			for (int x = r.left(); x <= r.right(); ++x)
			{
				QColor c = img.pixelColor(x, y);
				rs += c.red();
				gs += c.green();
				bs += c.blue();
				++n;
			}
		}
		if (n == 0)
			return QColor();
		return QColor(static_cast<int>(rs / n), static_cast<int>(gs / n), static_cast<int>(bs / n));
	}
}

void ScImageEditor::onToolChanged(QAction* action)
{
	// New mask-based selection tools route through the ImageTool framework.
	ImageTool* imageTool = nullptr;
	if (action == m_toolRectMarquee)         imageTool = m_rectMarqueeTool;
	else if (action == m_toolEllipseMarquee) imageTool = m_ellipseMarqueeTool;
	else if (action == m_toolLasso)          imageTool = m_lassoTool;
	else if (action == m_toolPolyLasso)      imageTool = m_polyLassoTool;
	else if (action == m_toolSmartSelect)    imageTool = m_samTool;
	else if (action == m_toolRefineBrush)    imageTool = m_refineBrushTool;
	if (imageTool)
	{
		// The ImageTool framework uses its own options bar; hide the legacy one.
		if (m_optionsBar)
			m_optionsBar->hide();
		setActiveTool(imageTool);
		return;
	}

	// Legacy built-in tools keep their in-view handling.
	setActiveTool(nullptr);
	ScImageEditorView::Tool t = ScImageEditorView::Tool::Move;
	QString name = QStringLiteral("Move");
	if (action == m_toolSelect)          { t = ScImageEditorView::Tool::Select;     name = QStringLiteral("Select"); }
	else if (action == m_toolCrop)       { t = ScImageEditorView::Tool::Crop;       name = QStringLiteral("Crop"); }
	else if (action == m_toolEyedropper) { t = ScImageEditorView::Tool::Eyedropper; name = QStringLiteral("Eyedropper"); }
	else if (action == m_toolZoom)       { t = ScImageEditorView::Tool::Zoom;       name = QStringLiteral("Zoom"); }
	else if (action == m_toolHand)       { t = ScImageEditorView::Tool::Hand;       name = QStringLiteral("Hand"); }
	if (m_view)
		m_view->setTool(t);
	if (m_optionsBar)
	{
		m_optionsBar->setActiveTool(name);
		m_optionsBar->show();
	}
}

void ScImageEditor::onSelectionChanged(const QRectF& sceneRect)
{
	m_selectionRect = sceneRect;
}

void ScImageEditor::onCropConfirmed(const QRectF& sceneRect)
{
	performCrop(sceneRect);
}

void ScImageEditor::applyBaseState(const QImage& base, bool cropped)
{
	m_originalImage = base;
	invalidateStackPrefixCache();
	m_baseIsCropped = cropped;
	m_selectionRect = QRectF();
	if (m_view)
		m_view->clearSelection();
	ensureSelection();      // base size may have changed (crop) — resize the selection
	renderEffects();        // re-run the stack on this base; updates the size label
	if (m_view)
		m_view->zoomFit();
}

void ScImageEditor::performCrop(const QRectF& sceneRect)
{
	if (m_originalImage.isNull())
		return;
	QRect r = sceneRect.toRect().intersected(m_originalImage.rect());
	if (r.width() < 1 || r.height() < 1)
		return;
	// Crop the working base; the effect stack is re-run on top of it. A crop
	// changes dimensions, so it cannot be represented as a non-destructive
	// effectsInUse entry — Save & Apply writes the cropped base to the file.
	const QImage before = m_originalImage;
	const bool beforeCropped = m_baseIsCropped;
	QImage after = m_originalImage.copy(r);
	// Preserve DPI metadata from the original image
	after.setDotsPerMeterX(m_originalImage.dotsPerMeterX());
	after.setDotsPerMeterY(m_originalImage.dotsPerMeterY());
	if (m_undoStack)
		m_undoStack->push(new CropCommand(this, before, beforeCropped, after, true, tr("Crop"))); // push() → redo() → applyBaseState(after, true)
	else
		applyBaseState(after, true);
}

void ScImageEditor::applyBaseResize(const QImage& newBase, const QString& label)
{
	if (newBase.isNull() || m_originalImage.isNull())
		return;
	// A resize / canvas change is a destructive baseline change (dimensions
	// differ), so — like crop — it lives on the base image and is written to the
	// source file on Save & Apply. Reuse the crop undo command for this.
	const QImage before = m_originalImage;
	const bool beforeCropped = m_baseIsCropped;
	if (m_undoStack)
		m_undoStack->push(new CropCommand(this, before, beforeCropped, newBase, true, label));
	else
		applyBaseState(newBase, true);
}

void ScImageEditor::openImageSizeDialog()
{
	if (m_originalImage.isNull())
		return;
	ImageSizeDialog dlg(m_originalImage, this);
	if (dlg.exec() != QDialog::Accepted || !dlg.imageResized())
		return;
	const QImage result = dlg.resultImage();
	applyBaseResize(result, tr("Resize to %1×%2").arg(result.width()).arg(result.height()));
	statusBar()->showMessage(tr("Resized image to %1 × %2 px").arg(result.width()).arg(result.height()), 3000);
}

void ScImageEditor::openCanvasSizeDialog()
{
	if (m_originalImage.isNull())
		return;
	CanvasSizeDialog dlg(m_originalImage, this);
	if (dlg.exec() != QDialog::Accepted || !dlg.canvasChanged())
		return;
	applyBaseResize(dlg.resultImage(), tr("Canvas Size"));
}

void ScImageEditor::onCropCommit()
{
	if (!m_view)
		return;
	const QRectF r = m_view->selectionRect();
	if (r.isValid() && r.width() >= 1.0 && r.height() >= 1.0)
	{
		performCrop(r);
		m_view->clearSelection();
	}
	else
	{
		statusBar()->showMessage(tr("Draw a crop rectangle on the image first."), 2500);
	}
}

void ScImageEditor::onCropCancel()
{
	if (m_view)
		m_view->clearSelection();
}

void ScImageEditor::onColorPicked(const QPoint& imagePos)
{
	if (m_image.isNull())
		return;
	QColor c;
	if (m_selectionRect.isValid() && m_selectionRect.width() >= 1.0 && m_selectionRect.height() >= 1.0)
		c = averageColor(m_image, m_selectionRect.toRect());
	else if (m_image.rect().contains(imagePos))
		c = m_image.pixelColor(imagePos);
	if (!c.isValid())
		return;

	const QString hex = c.name(QColor::HexRgb).toUpper();
	if (m_colorLabel)
		m_colorLabel->setText(QString("%1 (%2, %3, %4)").arg(hex).arg(c.red()).arg(c.green()).arg(c.blue()));
	if (m_swatchLabel)
	{
		QPalette pal = m_swatchLabel->palette();
		pal.setColor(QPalette::Window, c);
		m_swatchLabel->setPalette(pal);
	}
	QApplication::clipboard()->setText(hex);
	statusBar()->showMessage(tr("Sampled %1 — copied to clipboard").arg(hex), 2500);
}

void ScImageEditor::saveAndApply()
{
	if (!m_frame || m_frame->Pfile.isEmpty())
		return;
	ScribusDoc* doc = m_frame->doc();
	if (!doc)
		return;

	// Region-masked (selection) effects cannot be represented in the frame's
	// non-destructive effectsInUse list (Scribus applies those whole-image), so
	// they must be flattened into the file.
	if (hasMaskedEffects())
	{
		const auto answer = QMessageBox::question(this, tr("Save && Apply"),
			tr("This image uses selection-limited adjustments, which must be flattened into the source file:\n\"%1\"\n\n"
			   "Any other frames using this file will be affected. Continue?").arg(m_frame->Pfile),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes)
			return;
		const QString writeError = m_image.isNull() ? tr("there is no image to write") : writeImageToFile(m_image, m_frame->Pfile);
		if (!writeError.isEmpty())
		{
			QMessageBox::warning(this, tr("Save && Apply"),
				tr("Could not write the flattened image to \"%1\".\n\n%2").arg(m_frame->Pfile, writeError));
			return;
		}
		m_frame->effectsInUse.clear();   // everything is baked into the file now
		m_baseIsCropped = false;
		m_frame->loadImage(m_frame->Pfile, true);
		m_frame->update();
		doc->changed();
		doc->regionsChanged()->update(QRectF());
		return;
	}

	// A crop changes pixel dimensions and cannot be a non-destructive effect, so
	// write the cropped base (without effects) back to the source file; the
	// effect stack is then re-applied on top via effectsInUse as usual.
	if (m_baseIsCropped)
	{
		const auto answer = QMessageBox::question(this, tr("Save && Apply"),
			tr("The image was cropped. Applying it will overwrite the source file:\n\"%1\"\n\n"
			   "Any other frames using this file will be affected. Continue?").arg(m_frame->Pfile),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes)
			return;
		const QString writeError = writeImageToFile(m_originalImage, m_frame->Pfile);
		if (!writeError.isEmpty())
		{
			QMessageBox::warning(this, tr("Save && Apply"),
				tr("Could not write the cropped image to \"%1\".\n\n%2").arg(m_frame->Pfile, writeError));
			return;
		}
		m_baseIsCropped = false;   // the file now matches the cropped base
	}

	// Non-destructive: store the (visible) stack on the frame as effectsInUse.
	// Scribus re-applies these to the original file every time the image loads,
	// so we never overwrite the source image on disk.
	const ScImageEffectList newEffects = effectiveEffects();

	// Route through the doc so the change is registered for the document's undo
	// history (mirrors the right-click Image Effects path). updatePic() only
	// touches the doc's real selection, so we reload the frame explicitly below.
	Selection tmpSelection(this, false);
	tmpSelection.addItem(m_frame);
	doc->itemSelection_ApplyImageEffects(newEffects, &tmpSelection);

	// Guarantee the frame's pixmap reflects the new effects even if it is not
	// the document's current selection.
	m_frame->effectsInUse = newEffects;
	m_frame->loadImage(m_frame->Pfile, true);
	m_frame->update();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
}

void ScImageEditor::exportFlattened()
{
	if (m_image.isNull())
	{
		QMessageBox::warning(this, tr("Export Flattened Image"), tr("There is no image to export."));
		return;
	}
	const QString filter = tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;TIFF (*.tif *.tiff)");
	QString selectedFilter;
	QString path = QFileDialog::getSaveFileName(this, tr("Export Flattened Image"), QString(), filter, &selectedFilter);
	if (path.isEmpty())
		return;

	// Append a default extension matching the chosen filter if the user omitted one.
	QFileInfo fi(path);
	if (fi.suffix().isEmpty())
	{
		if (selectedFilter.startsWith(QLatin1String("JPEG")))
			path += QStringLiteral(".jpg");
		else if (selectedFilter.startsWith(QLatin1String("TIFF")))
			path += QStringLiteral(".tif");
		else
			path += QStringLiteral(".png");
	}

	// m_image already holds the original with the visible effect stack baked in.
	const QString writeError = writeImageToFile(m_image, path);
	if (!writeError.isEmpty())
	{
		QMessageBox::warning(this, tr("Export Flattened Image"),
			tr("Could not save the image to \"%1\".\n\n%2").arg(path, writeError));
	}
}

void ScImageEditor::revertImage()
{
	if (!m_frame || m_frame->Pfile.isEmpty())
		return;
	QImage img(m_frame->Pfile);
	if (img.isNull())
		return;
	m_originalImage = img;
	m_stack.clear();
	invalidateStackPrefixCache();
	if (m_undoStack)
		m_undoStack->clear();   // prior states referred to the old base image
	ensureSelection();
	if (m_selection)
	{
		m_suppressSelectionUndo = true;   // revert already cleared the undo stack
		m_selection->clear();
		m_selectionUndoBaseline = m_selection->mask();
		m_suppressSelectionUndo = false;
	}
	refreshStackList();
	renderEffects();
	m_view->zoomFit();
}
