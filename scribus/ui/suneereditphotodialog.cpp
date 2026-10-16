/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneereditphotodialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUndoStack>
#include <QUndoCommand>
#include <QShortcut>
#include <QDateTime>
#include <QPainterPath>
#include <QWheelEvent>

#include "pageitem.h"
#include "sccolorengine.h"
#include "scribusdoc.h"
#include "suneerbgremove.h"
#include "suneerphotostore.h"
#include "scribus.h"
#include "scribuscore.h"
#include "ui/suneer_news_panel.h"

using namespace SuneerPhotoEdit;

// ---------------------------------------------------------------------------
// SuneerPhotoView
// ---------------------------------------------------------------------------

SuneerPhotoView::SuneerPhotoView(QWidget* parent) : QGraphicsView(parent)
{
	setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	setDragMode(QGraphicsView::NoDrag);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
}

void SuneerPhotoView::fitImage()
{
	// the scene rect is the picture plus a margin (the shade item is far larger)
	if (scene())
		fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
}

void SuneerPhotoView::wheelEvent(QWheelEvent* e)
{
	if (!(e->modifiers() & Qt::ControlModifier))
	{
		bool used = false;
		emit wheelAt(mapToScene(e->position().toPoint()), e->angleDelta().y(), &used);
		if (used)
		{
			e->accept();
			return;
		}
	}
	const double f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
	scale(f, f);
	e->accept();
}

void SuneerPhotoView::mousePressEvent(QMouseEvent* e)
{
	if (e->button() == Qt::MiddleButton || (m_space && e->button() == Qt::LeftButton))
	{
		m_panning = true;
		m_lastPan = e->pos();
		setCursor(Qt::ClosedHandCursor);
		e->accept();
		return;
	}
	m_down = true;
	emit toolPress(mapToScene(e->pos()), e->button());
	e->accept();
}

void SuneerPhotoView::mouseMoveEvent(QMouseEvent* e)
{
	if (m_panning)
	{
		const QPoint d = e->pos() - m_lastPan;
		m_lastPan = e->pos();
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
		verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
		e->accept();
		return;
	}
	emit toolMove(mapToScene(e->pos()), m_down);
	e->accept();
}

void SuneerPhotoView::mouseReleaseEvent(QMouseEvent* e)
{
	if (m_panning)
	{
		m_panning = false;
		setCursor(m_space ? Qt::OpenHandCursor : Qt::ArrowCursor);
		e->accept();
		return;
	}
	m_down = false;
	emit toolRelease(mapToScene(e->pos()));
	e->accept();
}

void SuneerPhotoView::keyPressEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Space && !e->isAutoRepeat())
	{
		m_space = true;
		setCursor(Qt::OpenHandCursor);
		return;
	}
	QGraphicsView::keyPressEvent(e);
}

void SuneerPhotoView::keyReleaseEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Space && !e->isAutoRepeat())
	{
		m_space = false;
		setCursor(Qt::ArrowCursor);
		return;
	}
	QGraphicsView::keyReleaseEvent(e);
}

void SuneerPhotoView::drawBackground(QPainter* painter, const QRectF& rect)
{
	// checkerboard, so transparency is visible
	painter->save();
	painter->resetTransform();
	painter->fillRect(viewport()->rect(), QColor(60, 60, 60));
	painter->restore();
	Q_UNUSED(rect);
}

// ---------------------------------------------------------------------------
// SuneerEditPhotoDialog
// ---------------------------------------------------------------------------

namespace {
QToolButton* makeTool(const QString& text, const QString& tip, QWidget* parent, bool checkable = false)
{
	auto* b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setCheckable(checkable);
	b->setAutoRaise(false);
	b->setMinimumWidth(34);
	return b;
}
} // namespace

//! One mask edit (stroke, lasso action, reset) on the dialog's undo stack.
class SuneerMaskCommand : public QUndoCommand
{
public:
	SuneerMaskCommand(SuneerEditPhotoDialog* d, const QImage& before, const QImage& after, const QImage& pBefore, const QImage& pAfter, const QString& label)
		: QUndoCommand(label), m_d(d), m_before(before), m_after(after), m_pBefore(pBefore), m_pAfter(pAfter) {}
	void undo() override { m_d->m_protect = m_pBefore.isNull() ? QImage() : m_pBefore.copy(); m_d->setMask(m_before); }
	void redo() override { if (m_first) { m_first = false; return; } m_d->m_protect = m_pAfter.isNull() ? QImage() : m_pAfter.copy(); m_d->setMask(m_after); }
private:
	SuneerEditPhotoDialog* m_d;
	QImage m_before, m_after, m_pBefore, m_pAfter;
	bool m_first { true };
};

SuneerEditPhotoDialog::SuneerEditPhotoDialog(ScribusDoc* doc, PageItem* item, QWidget* parent)
	: QDialog(parent), m_doc(doc), m_item(item)
{
	resize(1180, 780);
	m_isObit = isObitPhoto(item);
	m_frameAspect = (item && item->width() > 0) ? item->height() / item->width() : 1.0;
	// The source is always a LOCAL file: original/ in the document's photos
	// folder when the picture came from the Workflow, else the frame's own
	// file. Nothing here touches the network; only "Re-download original" does.
	m_sourcePath = sourcePathOf(item);
	const QString expectedOriginal = item ? (item->getObjectAttributes(QString::fromLatin1(SuneerPhotoStore::AttrOriginal)).isEmpty() ? QString() : item->getObjectAttributes(QString::fromLatin1(SuneerPhotoStore::AttrOriginal)).first().value) : QString();
	m_sourceMissing = !expectedOriginal.isEmpty() && !QFileInfo::exists(expectedOriginal);
	QString err;
	m_source = m_sourceMissing ? QImage() : loadSource(m_sourcePath, &err);
	if (m_sourceMissing)
		err = tr("The local original is missing: %1").arg(expectedOriginal);
	setWindowTitle(tr("Edit photo \u2014 %1").arg(m_sourceMissing ? QFileInfo(expectedOriginal).fileName() : QFileInfo(m_sourcePath).fileName()));
	const bool haveSettings = item && !item->getObjectAttributes(QString::fromLatin1(AttrSettings)).isEmpty();
	m_s = haveSettings ? settingsOf(item) : (m_isObit ? obitPreset(doc) : Settings());
	if (!haveSettings && item && !item->getObjectAttributes(QString::fromLatin1(AttrBgMode)).isEmpty())
	{
		const QString mode = item->getObjectAttributes(QString::fromLatin1(AttrBgMode)).first().value;
		m_s.removeBg = mode != QLatin1String("none");
		m_s.bgMode = mode;
	}
	if (item)
	{
		const QString cutPath = item->getObjectAttributes(QString::fromLatin1(AttrCut)).isEmpty() ? QString() : item->getObjectAttributes(QString::fromLatin1(AttrCut)).first().value;
		if (!cutPath.isEmpty() && QFileInfo::exists(cutPath))
		{
			m_cut = QImage(cutPath).convertToFormat(QImage::Format_ARGB32);
			if (m_cut.size() != m_source.size())
				m_cut = QImage();
			else
			{
				m_cutModel = m_s.cutModel.isEmpty() ? m_s.model : m_s.cutModel;
				m_cutRegion = m_s.cutRect.isNull() ? QRect(QPoint(0, 0), m_source.size()) : m_s.cutRect;
			}
		}
		const QString brushPath = item->getObjectAttributes(QString::fromLatin1(AttrBrush)).isEmpty() ? QString() : item->getObjectAttributes(QString::fromLatin1(AttrBrush)).first().value;
		if (!brushPath.isEmpty() && QFileInfo::exists(brushPath))
		{
			m_brush = QImage(brushPath).convertToFormat(QImage::Format_Grayscale8);
			if (m_brush.size() != m_source.size())
				m_brush = QImage();
		}
		const QString protectPath = item->getObjectAttributes(QString::fromLatin1(AttrProtect)).isEmpty() ? QString() : item->getObjectAttributes(QString::fromLatin1(AttrProtect)).first().value;
		if (!protectPath.isEmpty() && QFileInfo::exists(protectPath))
		{
			m_protect = QImage(protectPath).convertToFormat(QImage::Format_Grayscale8);
			if (m_protect.size() != m_source.size())
				m_protect = QImage();
		}
	}
	m_undo = new QUndoStack(this);
	m_undo->setUndoLimit(40);

	// ---- layout -----------------------------------------------------------
	auto* main = new QHBoxLayout(this);
	m_scene = new QGraphicsScene(this);
	m_view = new SuneerPhotoView(this);
	m_view->setScene(m_scene);
	main->addWidget(m_view, 1);
	m_pix = m_scene->addPixmap(QPixmap());
	m_shade = m_scene->addPath(QPainterPath(), Qt::NoPen, QColor(0, 0, 0, 110));
	m_cropItem = m_scene->addRect(QRectF(), QPen(QColor(255, 255, 255), 0), Qt::NoBrush);
	for (auto& h : m_handles)
		h = m_scene->addRect(QRectF(), QPen(Qt::black, 0), QBrush(Qt::white));
	m_guide = m_scene->addRect(QRectF(), QPen(QColor(255, 60, 60), 0, Qt::DashLine), Qt::NoBrush);
	m_cursor = m_scene->addEllipse(QRectF(), QPen(QColor(255, 255, 0), 0), Qt::NoBrush);
	m_cursor->setVisible(false);
	QPen ants(Qt::white, 0, Qt::CustomDashLine);
	ants.setDashPattern({4, 4});
	ants.setCosmetic(true);
	m_selItem = m_scene->addPath(QPainterPath(), ants, Qt::NoBrush);
	QPen rubberPen(QColor(0, 200, 255), 0, Qt::DashLine);
	rubberPen.setCosmetic(true);
	m_rubber = m_scene->addPath(QPainterPath(), rubberPen, Qt::NoBrush);
	m_shade->setZValue(5); m_cropItem->setZValue(6); m_guide->setZValue(7); m_selItem->setZValue(8); m_rubber->setZValue(8); m_cursor->setZValue(9);
	for (auto* h : m_handles)
		h->setZValue(8);
	m_antsTimer = new QTimer(this);
	m_antsTimer->setInterval(120);
	connect(m_antsTimer, &QTimer::timeout, this, &SuneerEditPhotoDialog::onAntsTick);

	auto* side = new QVBoxLayout();
	side->setSpacing(5);
	main->addLayout(side, 0);
	const QString where = m_sourceMissing ? tr("MISSING local original") : sourceIsLocalOriginal(item, m_sourcePath) ? tr("local original") : tr("local file");
	QLabel* srcLbl = new QLabel(tr("Source (%1): %2 (%3 x %4 px)").arg(where, QFileInfo(m_sourcePath).fileName()).arg(m_source.width()).arg(m_source.height()), this);
	srcLbl->setWordWrap(true);
	srcLbl->setMaximumWidth(320);
	srcLbl->setStyleSheet(m_sourceMissing ? "color:#c62828; font-size:11px;" : "color:#666; font-size:11px;");
	srcLbl->setToolTip(tr("Opened from: %1\nNever from the Workflow server - only \"Re-download original\" uses the network.").arg(m_sourceMissing ? expectedOriginal : m_sourcePath));
	setToolTip(srcLbl->toolTip());
	side->addWidget(srcLbl);

	// crop / rotate
	auto* geo = new QGroupBox(tr("Crop, rotate"), this);
	auto* gl = new QVBoxLayout(geo);
	auto* rotRow = new QHBoxLayout();
	m_rotL = makeTool(QStringLiteral("↶ 90"), tr("Rotate 90° left"), geo);
	m_rotR = makeTool(QStringLiteral("↷ 90"), tr("Rotate 90° right"), geo);
	m_flipHBtn = makeTool(QStringLiteral("↔"), tr("Flip horizontal"), geo);
	m_flipVBtn = makeTool(QStringLiteral("↕"), tr("Flip vertical"), geo);
	rotRow->addWidget(m_rotL); rotRow->addWidget(m_rotR); rotRow->addWidget(m_flipHBtn); rotRow->addWidget(m_flipVBtn);
	rotRow->addWidget(new QLabel(tr("Fine:"), geo));
	m_fineSlider = new QSlider(Qt::Horizontal, geo);
	m_fineSlider->setRange(-100, 100);
	m_fineSlider->setValue(qRound(m_s.fine * 10));
	m_fineSlider->setToolTip(tr("Fine rotation, -10 .. +10 degrees, for tilted photos"));
	m_fineLabel = new QLabel(QStringLiteral("%1°").arg(m_s.fine, 0, 'f', 1), geo);
	m_fineLabel->setMinimumWidth(42);
	rotRow->addWidget(m_fineSlider, 1); rotRow->addWidget(m_fineLabel);
	gl->addLayout(rotRow);
	m_freeChk = new QCheckBox(tr("Free crop (the frame takes the crop's shape)"), geo);
	m_freeChk->setChecked(m_s.freeAspect);
	m_freeChk->setToolTip(tr("Off: the crop keeps the frame's current shape. On: any shape; on Apply the frame's height changes to match, same width."));
	gl->addWidget(m_freeChk);
	auto* faceRow = new QHBoxLayout();
	m_autoFaceChk = new QCheckBox(tr("Auto face"), geo);
	m_autoFaceChk->setChecked(m_s.autoFace);
	m_autoFaceChk->setToolTip(tr("Obituary rule as a starting point: the face box is 60 %% of the crop width, centred, hair near the top (18 %% below). The crop stays movable."));
	m_guideChk = new QCheckBox(tr("Face guide"), geo);
	m_guideChk->setChecked(true);
	faceRow->addWidget(m_autoFaceChk); faceRow->addWidget(m_guideChk); faceRow->addStretch(1);
	gl->addLayout(faceRow);
	side->addWidget(geo);

	// remove background + mask tools
	auto* bgBox = new QGroupBox(tr("Remove background"), this);
	auto* bl = new QVBoxLayout(bgBox);
	auto* rmRow = new QHBoxLayout();
	m_removeBgChk = new QCheckBox(bgBox);
	m_removeBgChk->setVisible(false);
	m_removeBgChk->setChecked(m_s.removeBg);
	auto* modeLbl = new QLabel(tr("Background:"), bgBox);
	m_bgModeCombo = new QComboBox(bgBox);
	m_bgModeCombo->addItem(tr("Auto (AI)"), QStringLiteral("auto"));
	m_bgModeCombo->addItem(tr("Manual"), QStringLiteral("manual"));
	m_bgModeCombo->addItem(tr("None"), QStringLiteral("none"));
	m_bgModeCombo->setToolTip(tr("Auto: AI removes the background. Manual: no AI; build the mask with lasso, selections and brushes. None: background unchanged (crop and tone only)."));
	m_bgModeCombo->setCurrentIndex(qMax(0, m_bgModeCombo->findData(m_s.removeBg ? m_s.bgMode : QStringLiteral("none"))));
	rmRow->addWidget(modeLbl);
	rmRow->addWidget(m_bgModeCombo, 1);
	m_modelCombo = new QComboBox(bgBox);
	m_modelCombo->addItem(QStringLiteral("u2net_human_seg"), QStringLiteral("u2net_human_seg"));
	m_modelCombo->addItem(QStringLiteral("isnet-general-use"), QStringLiteral("isnet-general-use"));
	m_modelCombo->addItem(QStringLiteral("u2net"), QStringLiteral("u2net"));
	m_modelCombo->addItem(QStringLiteral("silueta"), QStringLiteral("silueta"));
	const int mi = m_modelCombo->findData(m_s.model);
	m_modelCombo->setCurrentIndex(mi < 0 ? 0 : mi);
	rmRow->addWidget(m_removeBgChk); rmRow->addWidget(m_modelCombo);
	m_modelCombo->setToolTip(tr("AI model (Auto mode)"));
	m_modelCombo->setEnabled(autoMode());
	bl->addLayout(rmRow);
	auto* refineRow = new QHBoxLayout();
	m_refineChk = new QCheckBox(tr("Refine edge (hair)"), bgBox);
	m_refineChk->setChecked(m_s.refine > 0);
	m_refineChk->setToolTip(tr("Smooths and cleans the cut-out's edge against the new background: matte smoothing, crisper edge, colour decontamination (no halo)."));
	m_refineSlider = new QSlider(Qt::Horizontal, bgBox);
	m_refineSlider->setRange(1, 100);
	m_refineSlider->setValue(m_s.refine > 0 ? m_s.refine : 40);
	refineRow->addWidget(m_refineChk); refineRow->addWidget(m_refineSlider, 1);
	bl->addLayout(refineRow);
	// tools: crop | selections | brushes (icons as symbols; tooltips Malayalam + English)
	auto* toolRow = new QHBoxLayout();
	m_cropToolBtn = makeTool(QStringLiteral("\u2316 ") + tr("Crop"), tr("Crop: move or resize the crop"), bgBox, true);
	m_lassoBtn = makeTool(QStringLiteral("\u27CB ") + tr("Lasso"), tr("Lasso: drag around an area. Shift adds, Alt subtracts; Alt while drawing switches to polygon."), bgBox, true);
	m_polyBtn = makeTool(QStringLiteral("\u2B21 ") + tr("Polygon"), tr("Polygon: click the corners; click the first point or double-click to close. Backspace undoes a point, Esc cancels."), bgBox, true);
	m_rectBtn = makeTool(QStringLiteral("\u25AD ") + tr("Rect"), tr("Rectangle selection: drag"), bgBox, true);
	m_ellipseBtn = makeTool(QStringLiteral("\u25EF ") + tr("Ellipse"), tr("Ellipse selection: drag"), bgBox, true);
	toolRow->addWidget(m_cropToolBtn); toolRow->addWidget(m_lassoBtn); toolRow->addWidget(m_polyBtn); toolRow->addWidget(m_rectBtn); toolRow->addWidget(m_ellipseBtn);
	bl->addLayout(toolRow);
	auto* brushToolRow = new QHBoxLayout();
	m_restoreBtn = makeTool(QStringLiteral("\u270E+ ") + tr("Brush restore"), tr("Brush restore: paint the original pixels back. [ ] size, Shift+[ ] hardness, Shift+click straight line"), bgBox, true);
	m_eraseBtn = makeTool(QStringLiteral("\u270E\u2212 ") + tr("Brush erase"), tr("Brush erase: paint areas transparent"), bgBox, true);
	m_protectBrushBtn = makeTool(QStringLiteral("\u25A3 ") + tr("Protect"), tr("Protect brush (green): always kept, also after a model change or Re-process"), bgBox, true);
	m_excludeBrushBtn = makeTool(QStringLiteral("\u25A2 ") + tr("Exclude"), tr("Exclude brush (red): always removed"), bgBox, true);
	m_cropToolBtn->setChecked(true);
	for (auto* b : { m_cropToolBtn, m_lassoBtn, m_polyBtn, m_rectBtn, m_ellipseBtn, m_restoreBtn, m_eraseBtn, m_protectBrushBtn, m_excludeBrushBtn })
		b->setAutoExclusive(true);
	m_protectBrushBtn->setStyleSheet("QToolButton:checked { background:#c8e6c9; }");
	m_excludeBrushBtn->setStyleSheet("QToolButton:checked { background:#ffcdd2; }");
	brushToolRow->addWidget(m_restoreBtn); brushToolRow->addWidget(m_eraseBtn); brushToolRow->addWidget(m_protectBrushBtn); brushToolRow->addWidget(m_excludeBrushBtn);
	bl->addLayout(brushToolRow);
	auto* brushRow = new QHBoxLayout();
	brushRow->addWidget(new QLabel(tr("Size:"), bgBox));
	m_brushSize = new QSlider(Qt::Horizontal, bgBox);
	m_brushSize->setRange(4, 300);
	m_brushSize->setValue(40);
	brushRow->addWidget(m_brushSize, 2);
	brushRow->addWidget(new QLabel(tr("Hard:"), bgBox));
	m_hardSlider = new QSlider(Qt::Horizontal, bgBox);
	m_hardSlider->setRange(0, 100);
	m_hardSlider->setValue(80);
	m_hardSlider->setToolTip(tr("Brush hardness: 100 % = crisp edge, 0 % = very soft"));
	brushRow->addWidget(m_hardSlider, 1);
	bl->addLayout(brushRow);
	auto* stabRow = new QHBoxLayout();
	stabRow->addWidget(new QLabel(tr("Stabiliser:"), bgBox));
	m_stabSlider = new QSlider(Qt::Horizontal, bgBox);
	m_stabSlider->setRange(0, 100);
	m_stabSlider->setValue(30);
	m_stabSlider->setToolTip(tr("Stroke smoothing against hand jitter (0 = off)"));
	stabRow->addWidget(m_stabSlider, 1);
	m_brushInfo = new QLabel(bgBox);
	m_brushInfo->setStyleSheet("color:#666; font-size:10px;");
	stabRow->addWidget(m_brushInfo);
	bl->addLayout(stabRow);
	// actions on the selection (with the feather below)
	auto* selRow = new QHBoxLayout();
	m_selRestoreBtn = new QPushButton(QStringLiteral("\u2795 ") + tr("Restore selection"), bgBox);
	m_selEraseBtn = new QPushButton(QStringLiteral("\u2796 ") + tr("Remove selection"), bgBox);
	m_selSmartBtn = new QPushButton(QStringLiteral("\u2728 ") + tr("Smart restore"), bgBox);
	m_selRestoreBtn->setToolTip(tr("Restore selection: the original pixels inside the selection come back (feathered edge). The rest stays as it is."));
	m_selEraseBtn->setToolTip(tr("Remove selection: the inside becomes transparent (background colour)."));
	m_selSmartBtn->setToolTip(tr("Smart restore: only pixels inside the selection whose colour matches the subject come back; background stays removed."));
	for (auto* b : { m_selRestoreBtn, m_selEraseBtn, m_selSmartBtn })
	{
		b->setStyleSheet("padding:2px 6px;");
		selRow->addWidget(b);
	}
	bl->addLayout(selRow);
	auto* selRow2 = new QHBoxLayout();
	m_selProtectBtn = new QPushButton(QStringLiteral("\u25A3 ") + tr("Protect sel."), bgBox);
	m_selExcludeBtn = new QPushButton(QStringLiteral("\u25A2 ") + tr("Exclude sel."), bgBox);
	m_selKeepBtn = new QPushButton(tr("Keep inside"), bgBox);
	m_selInvertBtn = new QPushButton(tr("Invert"), bgBox);
	m_selClearBtn = new QPushButton(tr("Clear"), bgBox);
	m_selProtectBtn->setToolTip(tr("Protect selection (green): always kept, also after a model change or Re-process."));
	m_selExcludeBtn->setToolTip(tr("Exclude selection (red): always removed."));
	m_selKeepBtn->setToolTip(tr("Keep only the inside, everything outside becomes transparent"));
	for (auto* b : { m_selProtectBtn, m_selExcludeBtn, m_selKeepBtn, m_selInvertBtn, m_selClearBtn })
	{
		b->setStyleSheet("padding:2px 6px;");
		selRow2->addWidget(b);
	}
	bl->addLayout(selRow2);
	auto* featherRow = new QHBoxLayout();
	featherRow->addWidget(new QLabel(tr("Feather:"), bgBox));
	m_featherSlider = new QSlider(Qt::Horizontal, bgBox);
	m_featherSlider->setRange(0, 40);     // half pixels
	m_featherSlider->setValue(3);         // 1.5 px
	m_featherSlider->setToolTip(tr("Soft edge of the selection, 0 - 20 px (image pixels), in 0.5 px steps"));
	featherRow->addWidget(m_featherSlider, 1);
	m_undoBtn = makeTool(QStringLiteral("↶"), tr("Undo the last mask edit (Ctrl+Z)"), bgBox);
	m_redoBtn = makeTool(QStringLiteral("↷"), tr("Redo (Ctrl+Shift+Z)"), bgBox);
	m_resetMaskBtn = makeTool(tr("Reset mask"), tr("Forget every brush, lasso, protect and exclude edit (the AI cut-out stays)"), bgBox);
	m_viewBtn = makeTool(QStringLiteral("\u25D0 ") + tr("View"), tr("View (key M): result on the background colour \u2192 checkerboard \u2192 red overlay on the removed parts. Green = protected, red hatch = excluded."), bgBox);
	featherRow->addWidget(m_undoBtn); featherRow->addWidget(m_redoBtn); featherRow->addWidget(m_resetMaskBtn); featherRow->addWidget(m_viewBtn);
	bl->addLayout(featherRow);
	auto* colRow = new QHBoxLayout();
	m_bgColorChk = new QCheckBox(tr("Background colour:"), bgBox);
	m_bgColorChk->setChecked(!m_s.bgColor.isEmpty());
	m_bgColorChk->setToolTip(tr("Off: transparent PNG (the frame shows what is behind). On: this colour behind the person; the frame fill gets the document colour of the same name when there is one."));
	m_bgColorBtn = new QPushButton(bgBox);
	m_bgColorBtn->setMinimumWidth(110);
	colRow->addWidget(m_bgColorChk); colRow->addWidget(m_bgColorBtn, 1);
	bl->addLayout(colRow);
	side->addWidget(bgBox);

	// tone
	auto* toneBox = new QGroupBox(tr("Tone match"), this);
	auto* tl = new QHBoxLayout(toneBox);
	m_toneChk = new QCheckBox(tr("Strength"), toneBox);
	m_toneChk->setChecked(m_s.tone > 0);
	m_toneChk->setToolTip(tr("White balance, auto levels (1 %% clip), gentle contrast, one face brightness, slight sharpening - the obituary processing."));
	m_toneSlider = new QSlider(Qt::Horizontal, toneBox);
	m_toneSlider->setRange(1, 100);
	m_toneSlider->setValue(m_s.tone > 0 ? m_s.tone : 60);
	tl->addWidget(m_toneChk); tl->addWidget(m_toneSlider, 1);
	side->addWidget(toneBox);

	side->addStretch(1);
	m_progress = new QProgressBar(this);
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	side->addWidget(m_progress);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setMaximumWidth(320);
	m_status->setStyleSheet("color:#666; font-size:11px;");
	side->addWidget(m_status);
	if (SuneerPhotoStore::isNewsPhoto(item))
	{
		QPushButton* redl = new QPushButton(tr("Re-download original from Workflow"), this);
		redl->setToolTip(tr("Fetch the picture from the Workflow again (e.g. replaced on the server, or the file is missing); the old file is kept as *_prev. The dialog then reopens on the new original."));
		side->addWidget(redl);
		connect(redl, &QPushButton::clicked, this, [this]() {
			ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
			if (!mw || !mw->m_suneerNewsPanel || !m_item)
				return;
			mw->m_suneerNewsPanel->setDocument(m_doc);
			QString report;
			const int n = mw->m_suneerNewsPanel->redownloadPhotos({m_item.data()}, true, &report);
			if (n <= 0)
			{
				QMessageBox::warning(this, tr("Edit photo"), report.isEmpty() ? tr("The picture could not be downloaded.") : report);
				return;
			}
			m_sourcePath = sourcePathOf(m_item);
			QString err;
			m_source = loadSource(m_sourcePath, &err);
			m_cut = QImage();
			m_cutRegion = QRect();
			m_brush = QImage();
			m_s.face = QRectF();
			m_sourceMissing = false;
			m_applyBtn->setEnabled(!m_source.isNull());
			rebuildTransform();
			resetCrop(m_s.autoFace);
			updatePreview();
			if (autoMode())
				runRemoval();
			m_status->setText(tr("Original re-downloaded: %1").arg(QFileInfo(m_sourcePath).fileName()));
		});
	}
	auto* resetRow = new QHBoxLayout();
	m_resetAutoBtn = new QPushButton(tr("Reset to automatic"), this);
	m_resetAutoBtn->setToolTip(m_isObit ? tr("The obituary settings again (auto face, remove background, Obit BG, tone) and the automatic crop; the photo is not manual any more after Apply.")
	                                    : tr("Everything off, the whole picture, automatic face crop if a face is found."));
	m_resetOrigBtn = new QPushButton(tr("Reset to original"), this);
	m_resetOrigBtn->setToolTip(tr("Put the original file back into the frame and forget every edit."));
	resetRow->addWidget(m_resetAutoBtn); resetRow->addWidget(m_resetOrigBtn);
	side->addLayout(resetRow);
	auto* btnRow = new QHBoxLayout();
	m_applyBtn = new QPushButton(tr("Apply"), this);
	m_applyBtn->setDefault(true);
	m_cancelBtn = new QPushButton(tr("Cancel"), this);
	btnRow->addStretch(1); btnRow->addWidget(m_applyBtn); btnRow->addWidget(m_cancelBtn);
	side->addLayout(btnRow);

	// ---- wiring -----------------------------------------------------------
	connect(m_view, &SuneerPhotoView::toolPress, this, &SuneerEditPhotoDialog::onToolPress);
	connect(m_view, &SuneerPhotoView::toolMove, this, &SuneerEditPhotoDialog::onToolMove);
	connect(m_view, &SuneerPhotoView::toolRelease, this, &SuneerEditPhotoDialog::onToolRelease);
	connect(m_view, &SuneerPhotoView::wheelAt, this, &SuneerEditPhotoDialog::onWheel);
	connect(m_rotL, &QToolButton::clicked, this, [this]() { onRotate(-90); });
	connect(m_rotR, &QToolButton::clicked, this, [this]() { onRotate(90); });
	connect(m_flipHBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onFlipH);
	connect(m_flipVBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onFlipV);
	connect(m_fineSlider, &QSlider::valueChanged, this, &SuneerEditPhotoDialog::onFineChanged);
	connect(m_freeChk, &QCheckBox::toggled, this, &SuneerEditPhotoDialog::onFreeToggled);
	connect(m_autoFaceChk, &QCheckBox::toggled, this, &SuneerEditPhotoDialog::onAutoFace);
	connect(m_guideChk, &QCheckBox::toggled, this, [this](bool) { refreshOverlay(); });
	connect(m_removeBgChk, &QCheckBox::toggled, this, &SuneerEditPhotoDialog::onRemoveBg);
	connect(m_bgModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SuneerEditPhotoDialog::onBgModeChanged);
	connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SuneerEditPhotoDialog::onModelChanged);
	connect(m_refineChk, &QCheckBox::toggled, this, [this](bool on) { m_s.refine = on ? m_refineSlider->value() : 0; schedulePreview(); });
	connect(m_refineSlider, &QSlider::valueChanged, this, &SuneerEditPhotoDialog::onRefineChanged);
	connect(m_bgColorChk, &QCheckBox::toggled, this, [this](bool on) { if (!on) onBgNone(); else if (m_s.bgColor.isEmpty()) onBgPick(); else schedulePreview(); updateBgButton(); });
	connect(m_bgColorBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onBgPick);
	connect(m_toneChk, &QCheckBox::toggled, this, &SuneerEditPhotoDialog::onToneToggled);
	connect(m_toneSlider, &QSlider::valueChanged, this, &SuneerEditPhotoDialog::onToneChanged);
	connect(m_hardSlider, &QSlider::valueChanged, this, [this](int v) { m_hardness = v / 100.0; updateMaskButtons(); });
	connect(m_stabSlider, &QSlider::valueChanged, this, [this](int v) { m_stabiliser = v; updateMaskButtons(); });
	connect(m_brushSize, &QSlider::valueChanged, this, [this](int) { updateMaskButtons(); });
	for (auto* b : { m_cropToolBtn, m_eraseBtn, m_restoreBtn, m_lassoBtn, m_polyBtn, m_rectBtn, m_ellipseBtn, m_protectBrushBtn, m_excludeBrushBtn })
		connect(b, &QToolButton::toggled, this, [this](bool) { cancelLasso(); m_cursor->setVisible(false); updateMaskButtons(); });
	connect(m_selEraseBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelErase);
	connect(m_selRestoreBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelRestore);
	connect(m_selSmartBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelSmart);
	connect(m_selProtectBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelProtect);
	connect(m_selExcludeBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelExclude);
	connect(m_viewBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onViewToggle);
	new QShortcut(QKeySequence(Qt::Key_M), this, this, &SuneerEditPhotoDialog::onViewToggle);
	connect(m_selKeepBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelKeep);
	connect(m_selInvertBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelInvert);
	connect(m_selClearBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onSelClear);
	connect(m_undoBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onUndoMask);
	connect(m_redoBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onRedoMask);
	connect(m_resetMaskBtn, &QToolButton::clicked, this, &SuneerEditPhotoDialog::onResetMask);
	connect(m_undo, &QUndoStack::indexChanged, this, [this](int) { updateMaskButtons(); });
	new QShortcut(QKeySequence::Undo, this, this, &SuneerEditPhotoDialog::onUndoMask);
	new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this, this, &SuneerEditPhotoDialog::onRedoMask);
	connect(m_applyBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onApply);
	connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_resetAutoBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onResetAutomatic);
	connect(m_resetOrigBtn, &QPushButton::clicked, this, &SuneerEditPhotoDialog::onResetOriginal);
	m_previewTimer = new QTimer(this);
	m_previewTimer->setSingleShot(true);
	m_previewTimer->setInterval(120);
	connect(m_previewTimer, &QTimer::timeout, this, &SuneerEditPhotoDialog::updatePreview);
	m_rerunTimer = new QTimer(this);
	m_rerunTimer->setSingleShot(true);
	m_rerunTimer->setInterval(600);
	connect(m_rerunTimer, &QTimer::timeout, this, [this]() { if (autoMode()) runRemoval(); });
	m_remover = new SuneerBgRemover(this);
	connect(m_remover, &SuneerBgRemover::finished, this, &SuneerEditPhotoDialog::onRemovalFinished);
	connect(m_remover, &SuneerBgRemover::failed, this, &SuneerEditPhotoDialog::onRemovalFailed);
	updateBgButton();
	updateMaskButtons();

	if (m_source.isNull())
	{
		m_status->setText(err + (SuneerPhotoStore::isNewsPhoto(item) ? tr("\nUse \"Re-download original from Workflow\" to fetch it again.") : QString()));
		m_applyBtn->setEnabled(false);
		return;
	}
	rebuildTransform();
	if (m_s.crop.isNull())
		resetCrop(m_s.autoFace);
	else if (m_s.autoFace && m_s.face.isNull())
		resetCrop(true);
	updatePreview();
	if (autoMode() && m_cut.isNull())
		runRemoval();
	m_status->setText(m_isObit && !haveSettings ? tr("Obituary photo: the obituary settings are preselected.") : QString());
}

void SuneerEditPhotoDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	QTimer::singleShot(0, this, [this]() { if (m_view) m_view->fitImage(); refreshOverlay(); });
}

SuneerEditPhotoDialog::~SuneerEditPhotoDialog()
{
	if (m_remover)
		m_remover->cancel();
}

SuneerEditPhotoDialog::Tool SuneerEditPhotoDialog::tool() const
{
	if (m_eraseBtn->isChecked()) return Erase;
	if (m_restoreBtn->isChecked()) return Restore;
	if (m_protectBrushBtn->isChecked()) return ProtectBrush;
	if (m_excludeBrushBtn->isChecked()) return ExcludeBrush;
	if (m_lassoBtn->isChecked()) return LassoFree;
	if (m_polyBtn->isChecked()) return LassoPoly;
	if (m_rectBtn->isChecked()) return RectSel;
	if (m_ellipseBtn->isChecked()) return EllipseSel;
	return CropTool;
}

double SuneerEditPhotoDialog::aspect() const
{
	return m_frameAspect > 0 ? m_frameAspect : 1.0;
}

void SuneerEditPhotoDialog::rebuildTransform()
{
	m_toTransformedFull = transformFor(m_s, m_source.size(), &m_fullSize);
}

QPointF SuneerEditPhotoDialog::sceneToSource(const QPointF& scenePos) const
{
	const QPointF full(scenePos.x() / m_f, scenePos.y() / m_f);
	return m_toTransformedFull.inverted().map(full);
}

QRectF SuneerEditPhotoDialog::clampCrop(QRectF r) const
{
	const QRectF img(0, 0, m_fullSize.width(), m_fullSize.height());
	if (r.width() < 8) r.setWidth(8);
	if (r.height() < 8) r.setHeight(8);
	if (r.right() < img.left() + img.width() * 0.1) r.moveRight(img.left() + img.width() * 0.1);
	if (r.left() > img.right() - img.width() * 0.1) r.moveLeft(img.right() - img.width() * 0.1);
	if (r.bottom() < img.top() + img.height() * 0.1) r.moveBottom(img.top() + img.height() * 0.1);
	if (r.top() > img.bottom() - img.height() * 0.1) r.moveTop(img.bottom() - img.height() * 0.1);
	return r;
}

void SuneerEditPhotoDialog::setCrop(const QRectF& cropFull)
{
	m_s.crop = clampCrop(cropFull);
	refreshOverlay();
	// the cut-out covers the crop + margin only: a crop moved beyond it runs rembg again (background, debounced)
	if (autoMode() && !m_cut.isNull() && !m_cutRegion.isNull() && !m_remover->isRunning())
	{
		const QRect need = cropRegionInSource(m_s, m_source.size());
		if (!m_cutRegion.contains(need))
			m_rerunTimer->start();
	}
}

void SuneerEditPhotoDialog::resetCrop(bool useFace)
{
	if (useFace)
	{
		if (m_s.face.isNull())
		{
			QString err;
			setBusy(true, tr("Looking for the face..."));
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			m_s.face = detectFace(m_sourcePath, &err);
			setBusy(false);
			if (m_s.face.isNull())
				m_status->setText(tr("No face found (%1) - centred crop").arg(err));
		}
		if (!m_s.face.isNull())
		{
			const QRectF faceT = m_toTransformedFull.mapRect(m_s.face);
			setCrop(faceCrop(faceT, aspect(), m_fullSize));
			return;
		}
	}
	const double W = m_fullSize.width(), H = m_fullSize.height();
	double w = W, h = w * aspect();
	if (h > H)
	{
		h = H;
		w = h / aspect();
	}
	setCrop(QRectF((W - w) / 2.0, (H - h) / 2.0, w, h));
}

void SuneerEditPhotoDialog::refreshOverlay()
{
	const QRectF c(m_s.crop.x() * m_f, m_s.crop.y() * m_f, m_s.crop.width() * m_f, m_s.crop.height() * m_f);
	m_cropItem->setRect(c);
	QPainterPath outer;
	outer.addRect(m_pix->boundingRect().united(c).adjusted(-2000, -2000, 2000, 2000));
	QPainterPath inner;
	inner.addRect(c);
	m_shade->setPath(outer.subtracted(inner));
	const double hs = 6.0 / qMax(0.05, m_view->transform().m11());
	const QPointF corners[4] { c.topLeft(), c.topRight(), c.bottomRight(), c.bottomLeft() };
	for (int i = 0; i < 4; ++i)
		m_handles[i]->setRect(QRectF(corners[i].x() - hs / 2, corners[i].y() - hs / 2, hs, hs));
	if (m_guideChk->isChecked())
	{
		const double bw = c.width() * 0.60;
		m_guide->setRect(QRectF(c.x() + (c.width() - bw) / 2.0, c.y() + 0.18 * c.width(), bw, bw));
		m_guide->setVisible(true);
	}
	else
		m_guide->setVisible(false);
	refreshSelItem();
}

void SuneerEditPhotoDialog::schedulePreview()
{
	m_previewTimer->start();
}

// Full preview: the transformed picture with the cut (refined) and the tone;
// the mask is composited on top by composePreview() so strokes never wait
// for a re-render.
void SuneerEditPhotoDialog::updatePreview()
{
	if (m_source.isNull())
		return;
	Settings s = m_s;
	if (s.removeBg && s.bgMode != QLatin1String("manual") && m_cut.isNull())
		s.removeBg = false;   // AI not finished yet: the picture as it is
	QSize fullSize;
	m_previewBase = renderWhole(m_source, m_cut, QImage(), s, true, &fullSize);
	m_fullSize = fullSize;
	m_f = fullSize.width() > 0 ? double(m_previewBase.width()) / fullSize.width() : 1.0;
	m_previewBaseAlpha = QImage(m_previewBase.size(), QImage::Format_Grayscale8);
	for (int y = 0; y < m_previewBase.height(); ++y)
	{
		const QRgb* l = reinterpret_cast<const QRgb*>(m_previewBase.constScanLine(y));
		uchar* a = m_previewBaseAlpha.scanLine(y);
		for (int x = 0; x < m_previewBase.width(); ++x)
			a[x] = uchar(qAlpha(l[x]));
	}
	composePreview();
	m_scene->setSceneRect(m_pix->boundingRect().adjusted(-60, -60, 60, 60));
	refreshOverlay();
	if (!m_fitted && isVisible())
	{
		m_fitted = true;
		m_view->fitImage();
	}
}

void SuneerEditPhotoDialog::composePreview()
{
	if (m_previewBase.isNull())
		return;
	QImage whole = m_previewBase.copy();
	const bool bgOn = maskReady();
	auto transformMask = [&](const QImage& src) {
		QImage maskT(whole.size(), QImage::Format_Grayscale8);
		maskT.fill(128);
		QPainter mp(&maskT);
		mp.setRenderHint(QPainter::SmoothPixmapTransform, true);
		QTransform t = m_toTransformedFull;
		t *= QTransform::fromScale(m_f, m_f);
		mp.setTransform(t);
		mp.drawImage(0, 0, src);
		mp.end();
		return maskT;
	};
	QImage protectT;
	if (bgOn && !m_brush.isNull())
		applyMaskToAlpha(whole, transformMask(m_brush), m_previewBaseAlpha);
	if (bgOn && !m_protect.isNull())
	{
		protectT = transformMask(m_protect);
		applyProtectToAlpha(whole, protectT);
	}
	QImage shown(whole.size(), QImage::Format_ARGB32);
	const int mode = bgOn ? m_viewMode : 0;
	if (mode == 2)
	{
		// the original picture, the removed parts tinted red
		shown = m_previewBase.copy();
		for (int y = 0; y < shown.height(); ++y)
		{
			QRgb* sl = reinterpret_cast<QRgb*>(shown.scanLine(y));
			const QRgb* wl = reinterpret_cast<const QRgb*>(whole.constScanLine(y));
			for (int x = 0; x < shown.width(); ++x)
			{
				const int a = qAlpha(wl[x]);
				const int r = qRed(sl[x]), g = qGreen(sl[x]), b = qBlue(sl[x]);
				const double k = (255 - a) / 255.0 * 0.6;
				sl[x] = qRgba(int(r * (1 - k) + 230 * k), int(g * (1 - k) + 30 * k), int(b * (1 - k) + 30 * k), 255);
			}
		}
	}
	else
	{
		if (bgOn && mode == 0 && !m_s.bgColor.isEmpty())
			shown.fill(QColor(m_s.bgColor));
		else
		{
			QPainter cp(&shown);
			const int sq = 12;
			for (int y = 0; y < shown.height(); y += sq)
				for (int x = 0; x < shown.width(); x += sq)
					cp.fillRect(x, y, sq, sq, ((x / sq + y / sq) % 2) ? QColor(170, 170, 170) : QColor(215, 215, 215));
		}
		QPainter p(&shown);
		p.drawImage(0, 0, whole);
		p.end();
	}
	// protect (green) / exclude (red) overlay, always visible while editing
	if (!protectT.isNull())
	{
		for (int y = 0; y < shown.height(); ++y)
		{
			QRgb* sl = reinterpret_cast<QRgb*>(shown.scanLine(y));
			const uchar* pl = protectT.constScanLine(y);
			for (int x = 0; x < shown.width(); ++x)
			{
				if (pl[x] == 128)
					continue;
				const bool keep = pl[x] > 128;
				const int r = qRed(sl[x]), g = qGreen(sl[x]), b = qBlue(sl[x]);
				sl[x] = keep ? qRgba(int(r * 0.6), int(g * 0.6 + 100), int(b * 0.6), qAlpha(sl[x])) : qRgba(int(r * 0.6 + 100), int(g * 0.6), int(b * 0.6), qAlpha(sl[x]));
			}
		}
	}
	m_pix->setPixmap(QPixmap::fromImage(shown));
}

// ---- interaction ----------------------------------------------------------

double SuneerEditPhotoDialog::brushRadiusSource() const
{
	const double scaleT = std::sqrt(qAbs(m_toTransformedFull.determinant()));
	return (m_brushSize->value() / 2.0) / m_f / qMax(0.01, scaleT);
}

void SuneerEditPhotoDialog::onToolPress(const QPointF& p, Qt::MouseButton b)
{
	if (b != Qt::LeftButton)
		return;
	const Tool t = tool();
	const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
	if (isBrushTool(t))
	{
		if (!maskReady())
		{
			m_status->setText(tr("Choose Background: Manual (or Auto and wait for the AI) first."));
			return;
		}
		ensureBrush();
		beginMaskEdit();
		m_stamping = true;
		if ((mods & Qt::ShiftModifier) && m_haveLast)
		{
			// straight line from the last point
			QImage& target = (t == ProtectBrush || t == ExcludeBrush) ? m_protect : m_brush;
			stampSegment(target, sceneToSource(m_lastStamp), sceneToSource(p), brushRadiusSource(), m_hardness, (t == Erase || t == ExcludeBrush) ? 0 : 255, brushRadiusSource() * 0.2);
			m_lastStamp = p;
			m_smooth = p;
			composePreview();
		}
		else
		{
			m_smooth = p;
			brushTo(p, true);
		}
		return;
	}
	if (t == RectSel || t == EllipseSel)
	{
		if (!maskReady())
		{
			m_status->setText(tr("Choose Background: Manual (or Auto and wait for the AI) first."));
			return;
		}
		m_lassoActive = true;
		m_lassoPoly = false;
		m_lassoPts.clear();
		m_lassoPts << p << p;
		m_antsTimer->start();
		return;
	}
	if (t == LassoFree || t == LassoPoly)
	{
		if (!maskReady())
		{
			m_status->setText(tr("Choose Background: Manual (or Auto and wait for the AI) first."));
			return;
		}
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const bool dbl = m_lassoActive && (now - m_lastClickMs) < 350 && QLineF(p, m_lastClickScene).length() < 6.0 / qMax(0.05, m_view->transform().m11());
		m_lastClickMs = now;
		m_lastClickScene = p;
		if (!m_lassoActive)
		{
			m_lassoActive = true;
			m_lassoPoly = (t == LassoPoly);
			m_lassoPts.clear();
			m_lassoPts << p;
			m_antsTimer->start();
			return;
		}
		if (m_lassoPoly)
		{
			const double closeDist = 8.0 / qMax(0.05, m_view->transform().m11());
			if (dbl || (m_lassoPts.size() > 2 && QLineF(p, m_lassoPts.first()).length() < closeDist))
			{
				finishLasso(true);
				return;
			}
			m_lassoPts << p;
			refreshSelItem();
		}
		return;
	}
	// crop
	const QRectF c = m_cropItem->rect();
	const double hs = 10.0 / qMax(0.05, m_view->transform().m11());
	const QPointF corners[4] { c.topLeft(), c.topRight(), c.bottomRight(), c.bottomLeft() };
	m_drag = -1;
	for (int i = 0; i < 4; ++i)
		if (QLineF(p, corners[i]).length() <= hs)
			m_drag = i;
	if (m_drag < 0 && c.contains(p))
		m_drag = 4;
	m_pressScene = p;
	m_pressCrop = m_s.crop;
}

void SuneerEditPhotoDialog::onToolMove(const QPointF& p, bool down)
{
	const Tool t = tool();
	const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
	if (isBrushTool(t))
	{
		const double r = m_brushSize->value() / 2.0;
		m_cursor->setRect(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r));
		m_cursor->setPen(QPen(t == ProtectBrush ? QColor(0, 200, 0) : t == ExcludeBrush ? QColor(230, 30, 30) : QColor(255, 255, 0), 0));
		m_cursor->setVisible(true);
		if (down && m_stamping)
			brushTo(p, false);
		return;
	}
	m_cursor->setVisible(false);
	if ((t == RectSel || t == EllipseSel) && m_lassoActive && down)
	{
		// rubber shape from the press point to here
		const QPointF a = m_lassoPts.first();
		QPainterPath rb;
		if (t == RectSel)
			rb.addRect(QRectF(a, p).normalized());
		else
			rb.addEllipse(QRectF(a, p).normalized());
		m_rubber->setPath(rb);
		m_lassoPts[1] = p;
		return;
	}
	if (t == LassoFree || t == LassoPoly)
	{
		if (!m_lassoActive)
			return;
		// Alt while drawing: freehand <-> polygon
		const bool altPoly = (mods & Qt::AltModifier) ? !(t == LassoPoly) : (t == LassoPoly);
		if (altPoly != m_lassoPoly)
			m_lassoPoly = altPoly;
		if (!m_lassoPoly && down)
		{
			if (QLineF(p, m_lassoPts.last()).length() > 1.5 / qMax(0.05, m_view->transform().m11()))
				m_lassoPts << p;
			refreshSelItem();
		}
		else if (m_lassoPoly)
		{
			QPainterPath rb;
			rb.moveTo(m_lassoPts.last());
			rb.lineTo(p);
			m_rubber->setPath(rb);
		}
		return;
	}
	if (!down || m_drag < 0)
		return;
	const QPointF d((p.x() - m_pressScene.x()) / m_f, (p.y() - m_pressScene.y()) / m_f);
	QRectF r = m_pressCrop;
	if (m_drag == 4)
		r.translate(d);
	else
	{
		const QPointF anchor = m_drag == 0 ? m_pressCrop.bottomRight() : m_drag == 1 ? m_pressCrop.bottomLeft() : m_drag == 2 ? m_pressCrop.topLeft() : m_pressCrop.topRight();
		QPointF corner = (m_drag == 0 ? m_pressCrop.topLeft() : m_drag == 1 ? m_pressCrop.topRight() : m_drag == 2 ? m_pressCrop.bottomRight() : m_pressCrop.bottomLeft()) + d;
		double w = qAbs(corner.x() - anchor.x()), h = qAbs(corner.y() - anchor.y());
		if (!m_s.freeAspect)
		{
			if (w * aspect() > h) h = w * aspect(); else w = h / aspect();
		}
		w = qMax(8.0, w); h = qMax(8.0, h);
		const double x = corner.x() < anchor.x() ? anchor.x() - w : anchor.x();
		const double y = corner.y() < anchor.y() ? anchor.y() - h : anchor.y();
		r = QRectF(x, y, w, h);
	}
	setCrop(r);
}

void SuneerEditPhotoDialog::onToolRelease(const QPointF& p)
{
	const Tool t = tool();
	if (m_stamping)
	{
		m_stamping = false;
		endMaskEdit(t == Erase ? tr("Erase stroke") : t == Restore ? tr("Restore stroke") : t == ProtectBrush ? tr("Protect stroke") : tr("Exclude stroke"));
		composePreview();
	}
	if ((t == RectSel || t == EllipseSel) && m_lassoActive)
	{
		const QRectF r = QRectF(m_lassoPts.first(), p).normalized();
		m_lassoActive = false;
		m_lassoPts.clear();
		m_rubber->setPath(QPainterPath());
		if (r.width() > 2 && r.height() > 2)
		{
			QPainterPath scenePath;
			if (t == RectSel)
				scenePath.addRect(r);
			else
				scenePath.addEllipse(r);
			QPainterPath full = QTransform::fromScale(1.0 / m_f, 1.0 / m_f).map(scenePath);
			const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
			if ((mods & Qt::ShiftModifier) && !m_sel.isEmpty())
				m_sel = m_sel.united(full);
			else if ((mods & Qt::AltModifier) && !m_sel.isEmpty())
				m_sel = m_sel.subtracted(full);
			else
				m_sel = full;
			m_sel.setFillRule(Qt::WindingFill);
			m_status->setText(tr("Selection ready: Restore / Remove / Smart restore / Protect / Exclude."));
		}
		refreshSelItem();
		updateMaskButtons();
		m_drag = -1;
		return;
	}
	if ((t == LassoFree || t == LassoPoly) && m_lassoActive && !m_lassoPoly)
	{
		// freehand: the release closes the shape (a tiny shape is ignored)
		if (m_lassoPts.size() > 3)
			finishLasso(true);
		else
			cancelLasso();
	}
	Q_UNUSED(p);
	m_drag = -1;
}

void SuneerEditPhotoDialog::onWheel(const QPointF& p, int delta, bool* used)
{
	const Tool t = tool();
	if (isBrushTool(t))
	{
		// wheel over the picture with a brush: size
		m_brushSize->setValue(qBound(m_brushSize->minimum(), int(m_brushSize->value() * (delta > 0 ? 1.1 : 1 / 1.1)), m_brushSize->maximum()));
		const double r = m_brushSize->value() / 2.0;
		m_cursor->setRect(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r));
		*used = true;
		return;
	}
	if (t != CropTool || !m_cropItem->rect().contains(p))
		return;
	const double k = delta > 0 ? 1.05 : 1.0 / 1.05;
	QRectF r = m_s.crop;
	const QPointF c = r.center();
	r.setWidth(r.width() * k);
	r.setHeight(r.height() * k);
	r.moveCenter(c);
	setCrop(r);
	*used = true;
}

void SuneerEditPhotoDialog::keyPressEvent(QKeyEvent* e)
{
	const bool shift = e->modifiers() & Qt::ShiftModifier;
	if (e->key() == Qt::Key_BracketLeft || e->key() == Qt::Key_BracketRight || e->text() == QStringLiteral("{") || e->text() == QStringLiteral("}"))
	{
		const bool up = e->key() == Qt::Key_BracketRight || e->text() == QStringLiteral("}");
		if (shift || e->text() == QStringLiteral("{") || e->text() == QStringLiteral("}"))
			m_hardSlider->setValue(qBound(0, m_hardSlider->value() + (up ? 10 : -10), 100));
		else
			m_brushSize->setValue(qBound(m_brushSize->minimum(), int(m_brushSize->value() * (up ? 1.15 : 1 / 1.15)), m_brushSize->maximum()));
		e->accept();
		return;
	}
	if (e->key() == Qt::Key_Backspace && m_lassoActive && m_lassoPoly)
	{
		if (m_lassoPts.size() > 1)
			m_lassoPts.removeLast();
		else
			cancelLasso();
		refreshSelItem();
		e->accept();
		return;
	}
	if (e->key() == Qt::Key_Escape && m_lassoActive)
	{
		cancelLasso();
		e->accept();
		return;
	}
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
	{
		if (m_lassoActive && m_lassoPoly)
		{
			finishLasso(true);
			e->accept();
			return;
		}
	}
	QDialog::keyPressEvent(e);
}

// ---- brushes --------------------------------------------------------------

void SuneerEditPhotoDialog::ensureBrush()
{
	if (m_brush.isNull() || m_brush.size() != m_source.size())
	{
		m_brush = QImage(m_source.size(), QImage::Format_Grayscale8);
		m_brush.fill(128);
	}
	if (m_protect.isNull() || m_protect.size() != m_source.size())
	{
		m_protect = QImage(m_source.size(), QImage::Format_Grayscale8);
		m_protect.fill(128);
	}
}

void SuneerEditPhotoDialog::brushTo(const QPointF& scenePos, bool first)
{
	const Tool t = tool();
	const int target = (t == Erase || t == ExcludeBrush) ? 0 : 255;
	QImage& mask = (t == ProtectBrush || t == ExcludeBrush) ? m_protect : m_brush;
	// stabiliser: the stamped point follows the cursor with a lag
	const double lag = m_stabiliser / 100.0 * 0.85;
	QPointF p = first ? scenePos : m_smooth + (scenePos - m_smooth) * (1.0 - lag);
	m_smooth = p;
	const double r = brushRadiusSource();
	if (first || !m_haveLast)
		stampSoft(mask, sceneToSource(p), r, m_hardness, target);
	else
		stampSegment(mask, sceneToSource(m_lastStamp), sceneToSource(p), r, m_hardness, target, qMax(0.5, 2.0 * r * 0.10));
	m_lastStamp = p;
	m_haveLast = true;
	composePreview();
}

void SuneerEditPhotoDialog::beginMaskEdit()
{
	m_resetAuto = false;
	ensureBrush();
	m_maskBefore = m_brush.copy();
	m_protectBefore = m_protect.copy();
}

void SuneerEditPhotoDialog::endMaskEdit(const QString& label)
{
	if (m_maskBefore.isNull())
		return;
	if (m_maskBefore == m_brush && m_protectBefore == m_protect)
		return;
	m_undo->push(new SuneerMaskCommand(this, m_maskBefore, m_brush.copy(), m_protectBefore, m_protect.copy(), label));
	m_maskBefore = QImage();
	m_protectBefore = QImage();
}

void SuneerEditPhotoDialog::setMask(const QImage& m)
{
	m_brush = m.isNull() ? QImage() : m.copy();
	composePreview();
	updateMaskButtons();
}

void SuneerEditPhotoDialog::onUndoMask()
{
	if (m_undo->canUndo())
		m_undo->undo();
}

void SuneerEditPhotoDialog::onRedoMask()
{
	if (m_undo->canRedo())
		m_undo->redo();
}

void SuneerEditPhotoDialog::onResetMask()
{
	ensureBrush();
	beginMaskEdit();
	m_brush.fill(128);
	m_protect.fill(128);
	endMaskEdit(tr("Reset mask"));
	composePreview();
}

void SuneerEditPhotoDialog::updateMaskButtons()
{
	m_undoBtn->setEnabled(m_undo->canUndo());
	m_redoBtn->setEnabled(m_undo->canRedo());
	const bool hasSel = !m_sel.isEmpty();
	for (auto* b : { m_selEraseBtn, m_selRestoreBtn, m_selSmartBtn, m_selProtectBtn, m_selExcludeBtn, m_selKeepBtn, m_selInvertBtn, m_selClearBtn })
		b->setEnabled(hasSel);
	m_brushInfo->setText(tr("%1 px, hard %2 %, stab %3").arg(m_brushSize->value()).arg(m_hardSlider->value()).arg(m_stabiliser));
}

// ---- lasso ----------------------------------------------------------------

QPainterPath SuneerEditPhotoDialog::lassoPathScene() const
{
	QPainterPath path;
	if (m_lassoPts.size() < 2)
		return path;
	path.addPolygon(m_lassoPts);
	path.closeSubpath();
	return path;
}

void SuneerEditPhotoDialog::refreshSelItem()
{
	QPainterPath shown;
	if (!m_sel.isEmpty())
	{
		QTransform t = QTransform::fromScale(m_f, m_f);
		shown = t.map(m_sel);
	}
	if (m_lassoActive && m_lassoPts.size() >= 2)
	{
		QPainterPath cur;
		cur.addPolygon(m_lassoPts);
		shown.addPath(cur);
	}
	m_selItem->setPath(shown);
	if (!m_lassoActive)
		m_rubber->setPath(QPainterPath());
	if (shown.isEmpty())
		m_antsTimer->stop();
	else if (!m_antsTimer->isActive())
		m_antsTimer->start();
}

void SuneerEditPhotoDialog::onAntsTick()
{
	m_antsOffset = (m_antsOffset + 1) % 8;
	QPen pen = m_selItem->pen();
	pen.setDashOffset(m_antsOffset);
	m_selItem->setPen(pen);
}

void SuneerEditPhotoDialog::finishLasso(bool close)
{
	if (!m_lassoActive)
		return;
	m_lassoActive = false;
	m_rubber->setPath(QPainterPath());
	if (close && m_lassoPts.size() > 2)
	{
		QPainterPath scenePath = lassoPathScene();
		scenePath.setFillRule(Qt::WindingFill);
		QPainterPath full = QTransform::fromScale(1.0 / m_f, 1.0 / m_f).map(scenePath);
		const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
		if ((mods & Qt::ShiftModifier) && !m_sel.isEmpty())
			m_sel = m_sel.united(full);
		else if ((mods & Qt::AltModifier) && !m_sel.isEmpty())
			m_sel = m_sel.subtracted(full);
		else
			m_sel = full;
		m_sel.setFillRule(Qt::WindingFill);
		m_status->setText(tr("Selection ready: Erase, Restore or Keep inside (Shift+lasso adds, Alt+lasso subtracts)."));
	}
	m_lassoPts.clear();
	refreshSelItem();
	updateMaskButtons();
}

void SuneerEditPhotoDialog::cancelLasso()
{
	if (!m_lassoActive)
		return;
	m_lassoActive = false;
	m_lassoPts.clear();
	m_rubber->setPath(QPainterPath());
	refreshSelItem();
}

void SuneerEditPhotoDialog::applySelection(int target, bool outside, const QString& label)
{
	if (m_sel.isEmpty() || !maskReady())
		return;
	ensureBrush();
	beginMaskEdit();
	// selection in transformed full px -> source px; feather in image px
	const QPainterPath src = m_toTransformedFull.inverted().map(m_sel);
	applyPathToMask(m_brush, src, featherPx(), target, outside);
	endMaskEdit(label);
	composePreview();
}

void SuneerEditPhotoDialog::onSelErase() { applySelection(0, false, tr("Remove selection")); }
void SuneerEditPhotoDialog::onSelRestore() { applySelection(255, false, tr("Restore selection")); }
void SuneerEditPhotoDialog::onSelKeep() { applySelection(0, true, tr("Keep inside")); }

void SuneerEditPhotoDialog::onSelProtect()
{
	if (m_sel.isEmpty() || !maskReady())
		return;
	ensureBrush();
	beginMaskEdit();
	applyPathToMask(m_protect, m_toTransformedFull.inverted().map(m_sel), featherPx(), 255, false);
	endMaskEdit(tr("Protect selection"));
	composePreview();
}

void SuneerEditPhotoDialog::onSelExclude()
{
	if (m_sel.isEmpty() || !maskReady())
		return;
	ensureBrush();
	beginMaskEdit();
	applyPathToMask(m_protect, m_toTransformedFull.inverted().map(m_sel), featherPx(), 0, false);
	endMaskEdit(tr("Exclude selection"));
	composePreview();
}

// Smart restore: only pixels that look like the subject (the pixels the
// cut-out already keeps inside the selection) come back.
void SuneerEditPhotoDialog::onSelSmart()
{
	if (m_sel.isEmpty() || !maskReady())
		return;
	ensureBrush();
	beginMaskEdit();
	const QPainterPath src = m_toTransformedFull.inverted().map(m_sel);
	const QImage cov = pathCoverage(src, m_source.size(), featherPx());
	const QImage w = smartRestoreWeights(m_source, m_cut, cov);
	bool any = false;
	for (int y = 0; y < m_brush.height(); ++y)
	{
		uchar* ml = m_brush.scanLine(y);
		const uchar* wl = w.constScanLine(y);
		for (int x = 0; x < m_brush.width(); ++x)
		{
			if (wl[x] == 0)
				continue;
			any = true;
			const double a = wl[x] / 255.0;
			ml[x] = uchar(qBound(0.0, ml[x] * (1.0 - a) + 255.0 * a + 0.5, 255.0));
		}
	}
	if (!any)
	{
		m_status->setText(tr("Smart restore: nothing kept inside the selection to learn the subject's colour from - use Restore selection."));
		m_maskBefore = QImage();
		return;
	}
	endMaskEdit(tr("Smart restore"));
	composePreview();
}

double SuneerEditPhotoDialog::featherPx() const
{
	return m_featherSlider->value() / 2.0;
}

void SuneerEditPhotoDialog::onBgModeChanged(int idx)
{
	const QString mode = m_bgModeCombo->itemData(idx).toString();
	const QString old = m_s.removeBg ? m_s.bgMode : QStringLiteral("none");
	if (mode == old)
		return;
	if (mode == QLatin1String("manual"))
	{
		// Auto -> Manual: keep the AI mask as the starting point, or start from the whole original
		bool fromAi = false;
		if (old == QLatin1String("auto") && !m_cut.isNull())
		{
			QMessageBox box(QMessageBox::Question, tr("Manual background"), tr("Start the manual mask from the current AI mask, or from the full original picture?"), QMessageBox::NoButton, this);
			QPushButton* ai = box.addButton(tr("Start from AI mask"), QMessageBox::AcceptRole);
			box.addButton(tr("Full original"), QMessageBox::RejectRole);
			box.exec();
			fromAi = box.clickedButton() == ai;
		}
		ensureBrush();
		beginMaskEdit();
		if (fromAi)
		{
			// the current alpha (AI + brush) becomes the manual mask: a = 255 * m / 128 below 128
			const QImage a = alphaMap(m_source.size(), m_cut, m_brush, QImage(), false);
			for (int y = 0; y < m_brush.height(); ++y)
			{
				uchar* bl = m_brush.scanLine(y);
				const uchar* al = a.constScanLine(y);
				for (int x = 0; x < m_brush.width(); ++x)
					bl[x] = al[x] >= 255 ? 128 : uchar(al[x] * 128 / 255);
			}
		}
		else
			m_brush.fill(128);
		m_s.removeBg = true;
		m_s.bgMode = QStringLiteral("manual");
		endMaskEdit(tr("Manual background"));
		m_remover->cancel();
		setBusy(false);
		m_status->setText(tr("Manual background: build the mask with Lasso / Polygon / Rect / Ellipse and the brushes."));
	}
	else if (mode == QLatin1String("auto"))
	{
		m_s.removeBg = true;
		m_s.bgMode = QStringLiteral("auto");
		if (m_cut.isNull() || m_cutModel != m_s.model)
			runRemoval();
	}
	else
	{
		m_s.removeBg = false;
		m_s.bgMode = QStringLiteral("none");
		m_remover->cancel();
		setBusy(false);
	}
	{
		QSignalBlocker b(m_removeBgChk);
		m_removeBgChk->setChecked(m_s.removeBg);
	}
	m_modelCombo->setEnabled(autoMode());
	updatePreview();
}

void SuneerEditPhotoDialog::onViewToggle()
{
	m_viewMode = (m_viewMode + 1) % 3;
	m_status->setText(m_viewMode == 0 ? tr("View: result on the background colour") : m_viewMode == 1 ? tr("View: checkerboard") : tr("View: red overlay on the removed parts"));
	composePreview();
}

void SuneerEditPhotoDialog::onSelInvert()
{
	if (m_sel.isEmpty())
		return;
	QPainterPath all;
	all.addRect(QRectF(0, 0, m_fullSize.width(), m_fullSize.height()));
	m_sel = all.subtracted(m_sel);
	m_sel.setFillRule(Qt::WindingFill);
	refreshSelItem();
}

void SuneerEditPhotoDialog::onSelClear()
{
	m_sel = QPainterPath();
	cancelLasso();
	refreshSelItem();
	updateMaskButtons();
}

void SuneerEditPhotoDialog::onRefineChanged(int v)
{
	if (m_refineChk->isChecked())
	{
		m_s.refine = v;
		schedulePreview();
	}
}

// ---- settings -------------------------------------------------------------

void SuneerEditPhotoDialog::onRotate(int deg)
{
	m_s.rot90 = ((m_s.rot90 + deg) % 360 + 360) % 360;
	rebuildTransform();
	resetCrop(m_s.autoFace);
	onSelClear();
	schedulePreview();
}

void SuneerEditPhotoDialog::onFlipH()
{
	m_s.flipH = !m_s.flipH;
	rebuildTransform();
	resetCrop(m_s.autoFace);
	onSelClear();
	schedulePreview();
}

void SuneerEditPhotoDialog::onFlipV()
{
	m_s.flipV = !m_s.flipV;
	rebuildTransform();
	resetCrop(m_s.autoFace);
	onSelClear();
	schedulePreview();
}

void SuneerEditPhotoDialog::onFineChanged(int v)
{
	m_s.fine = v / 10.0;
	m_fineLabel->setText(QStringLiteral("%1°").arg(m_s.fine, 0, 'f', 1));
	const QPointF centreRel = m_fullSize.width() > 0 ? QPointF(m_s.crop.center().x() / m_fullSize.width(), m_s.crop.center().y() / m_fullSize.height()) : QPointF(0.5, 0.5);
	rebuildTransform();
	QRectF r = m_s.crop;
	r.moveCenter(QPointF(centreRel.x() * m_fullSize.width(), centreRel.y() * m_fullSize.height()));
	setCrop(r);
	onSelClear();
	schedulePreview();
}

void SuneerEditPhotoDialog::onFreeToggled(bool on)
{
	m_s.freeAspect = on;
	if (!on)
	{
		QRectF r = m_s.crop;
		r.setHeight(r.width() * aspect());
		setCrop(r);
	}
}

void SuneerEditPhotoDialog::onAutoFace(bool on)
{
	m_s.autoFace = on;
	if (on)
		resetCrop(true);
}

void SuneerEditPhotoDialog::onRemoveBg(bool on)
{
	m_s.removeBg = on;
	if (autoMode() && (m_cut.isNull() || m_cutModel != m_s.model))
		runRemoval();
	else
		schedulePreview();
}

void SuneerEditPhotoDialog::onModelChanged()
{
	m_s.model = m_modelCombo->currentData().toString();
	if (autoMode() && m_cutModel != m_s.model)
		runRemoval();
}

// rembg sees only the crop + 10 % margin, downscaled to a long side of
// 1024 px; the alpha comes back upscaled to the region's full resolution.
// A cut-out that already covers the region (same model) is reused.
void SuneerEditPhotoDialog::runRemoval()
{
	if (m_source.isNull())
		return;
	const QRect region = cropRegionInSource(m_s, m_source.size());
	if (!m_cut.isNull() && m_cutModel == m_s.model && !m_cutRegion.isNull() && m_cutRegion.contains(region))
	{
		m_status->setText(tr("Cut-out from the cache (%1 x %2 px region, %3).").arg(m_cutRegion.width()).arg(m_cutRegion.height()).arg(m_cutModel));
		schedulePreview();
		return;
	}
	m_remover->cancel();
	QImage part = m_source.copy(region);
	const int longSide = qMax(part.width(), part.height());
	m_regionScale = longSide > 1024 ? 1024.0 / longSide : 1.0;
	if (m_regionScale < 1.0)
		part = part.scaled(qMax(1, qRound(part.width() * m_regionScale)), qMax(1, qRound(part.height() * m_regionScale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	m_pendingRegion = region;
	m_rembgTimer.start();
	setBusy(true, tr("Removing the background (%1, %2 x %3 px region -> %4 x %5 for the model)...").arg(m_s.model).arg(region.width()).arg(region.height()).arg(part.width()).arg(part.height()));
	m_remover->start(part, part.rect(), m_s.model, 0);
}

void SuneerEditPhotoDialog::onRemovalFinished(const QImage& cutSmall)
{
	setBusy(false);
	const qint64 ms = m_rembgTimer.elapsed();
	const QRect region = m_pendingRegion.isNull() ? QRect(QPoint(0, 0), m_source.size()) : m_pendingRegion;
	// alpha of the small result -> the region's full resolution
	QImage small = cutSmall.convertToFormat(QImage::Format_ARGB32);
	QImage alphaSmall(small.size(), QImage::Format_Grayscale8);
	for (int y = 0; y < small.height(); ++y)
	{
		const QRgb* l = reinterpret_cast<const QRgb*>(small.constScanLine(y));
		uchar* a = alphaSmall.scanLine(y);
		for (int x = 0; x < small.width(); ++x)
			a[x] = uchar(qAlpha(l[x]));
	}
	const QImage alpha = alphaSmall.size() == region.size() ? alphaSmall : alphaSmall.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QImage cut(m_source.size(), QImage::Format_ARGB32);
	cut.fill(Qt::transparent);
	for (int y = 0; y < region.height(); ++y)
	{
		const int sy = region.y() + y;
		if (sy < 0 || sy >= m_source.height())
			continue;
		const QRgb* sl = reinterpret_cast<const QRgb*>(m_source.constScanLine(sy));
		QRgb* cl = reinterpret_cast<QRgb*>(cut.scanLine(sy));
		const uchar* al = alpha.constScanLine(y);
		for (int x = 0; x < region.width(); ++x)
		{
			const int sx = region.x() + x;
			if (sx < 0 || sx >= m_source.width())
				continue;
			cl[sx] = qRgba(qRed(sl[sx]), qGreen(sl[sx]), qBlue(sl[sx]), al[x]);
		}
	}
	m_cut = cut;
	m_cutRegion = region;
	m_cutModel = m_s.model;
	m_s.cutRect = region;
	m_s.cutModel = m_cutModel;
	m_status->setText(tr("Background removed in %1 s (%2 x %3 px region, model input %4 px). Erase / Restore / Lasso refine the result.")
	                  .arg(ms / 1000.0, 0, 'f', 1).arg(region.width()).arg(region.height()).arg(qRound(qMax(region.width(), region.height()) * m_regionScale)));
	if (qEnvironmentVariableIsSet("SUNEER_DRAG_DEBUG"))
		fprintf(stderr, "EDITDBG rembg region %dx%d scaled %.3f -> %d ms (source %dx%d)\n", region.width(), region.height(), m_regionScale, (int)ms, m_source.width(), m_source.height());
	schedulePreview();
}

void SuneerEditPhotoDialog::onRemovalFailed(const QString& err)
{
	setBusy(false);
	m_status->setText(tr("Background removal failed: %1").arg(err));
}

void SuneerEditPhotoDialog::onBgNone()
{
	m_s.bgColor.clear();
	updateBgButton();
	schedulePreview();
}

void SuneerEditPhotoDialog::onBgPick()
{
	QColorDialog dlg(m_s.bgColor.isEmpty() ? QColor(182, 184, 185) : QColor(m_s.bgColor), this);
	dlg.setOption(QColorDialog::DontUseNativeDialog, true);
	dlg.setWindowTitle(tr("Background colour (the picker button takes a colour from anywhere on the screen)"));
	int k = 0;
	for (auto it = m_doc->PageColors.constBegin(); it != m_doc->PageColors.constEnd() && k < 16; ++it, ++k)
		QColorDialog::setCustomColor(k, ScColorEngine::getRGBColor(it.value(), m_doc));
	if (dlg.exec() != QDialog::Accepted)
	{
		if (m_s.bgColor.isEmpty())
			m_bgColorChk->setChecked(false);
		return;
	}
	m_s.bgColor = dlg.selectedColor().name();
	m_bgColorChk->setChecked(true);
	m_s.bgName.clear();
	for (auto it = m_doc->PageColors.constBegin(); it != m_doc->PageColors.constEnd(); ++it)
		if (ScColorEngine::getRGBColor(it.value(), m_doc).name() == m_s.bgColor)
		{
			m_s.bgName = it.key();
			break;
		}
	updateBgButton();
	schedulePreview();
}

void SuneerEditPhotoDialog::updateBgButton()
{
	if (m_s.bgColor.isEmpty())
	{
		m_bgColorBtn->setText(tr("none (transparent)"));
		m_bgColorBtn->setStyleSheet(QString());
	}
	else
	{
		const QColor c(m_s.bgColor);
		m_bgColorBtn->setText(m_s.bgName.isEmpty() ? m_s.bgColor.toUpper() : m_s.bgName);
		m_bgColorBtn->setStyleSheet(QStringLiteral("background:%1; color:%2;").arg(m_s.bgColor, c.lightness() > 140 ? QStringLiteral("black") : QStringLiteral("white")));
	}
}

void SuneerEditPhotoDialog::onToneToggled(bool on)
{
	m_s.tone = on ? m_toneSlider->value() : 0;
	schedulePreview();
}

void SuneerEditPhotoDialog::onToneChanged(int v)
{
	if (m_toneChk->isChecked())
	{
		m_s.tone = v;
		schedulePreview();
	}
}

void SuneerEditPhotoDialog::setBusy(bool on, const QString& text)
{
	m_progress->setVisible(on);
	if (!text.isEmpty())
		m_status->setText(text);
	m_applyBtn->setEnabled(!on && !m_source.isNull());
}

// ---- buttons --------------------------------------------------------------

void SuneerEditPhotoDialog::onApply()
{
	if (!m_item || !m_doc || m_source.isNull())
		return;
	if (autoMode() && m_cut.isNull())
	{
		m_status->setText(tr("The background removal has not finished yet."));
		return;
	}
	setBusy(true, tr("Rendering at full resolution..."));
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	if (m_s.removeBg && !m_s.bgColor.isEmpty() && m_s.bgName.isEmpty() && m_isObit)
		m_s.bgName = QStringLiteral("Obit BG");
	if (!m_s.bgName.isEmpty() && !m_doc->PageColors.contains(m_s.bgName) && !m_s.bgColor.isEmpty())
	{
		ScColor col;
		col.setRgbColor(QColor(m_s.bgColor).red(), QColor(m_s.bgColor).green(), QColor(m_s.bgColor).blue());
		m_doc->PageColors.insert(m_s.bgName, col);
	}
	m_s.cutRect = m_cutRegion;
	m_s.cutModel = m_cutModel;
	const QImage useCut = autoMode() ? m_cut : QImage();
	const QImage result = SuneerPhotoEdit::render(m_source, useCut, m_s.removeBg ? m_brush : QImage(), m_s, false, nullptr, m_s.removeBg ? m_protect : QImage());
	QString err;
	if (!apply(m_doc, m_item, result, useCut, m_s.removeBg ? m_brush : QImage(), m_s, m_sourcePath, &err, m_protect, !m_resetAuto || m_s.bgMode == QLatin1String("manual")))
	{
		setBusy(false);
		QMessageBox::warning(this, tr("Edit photo"), err);
		return;
	}
	m_applied = true;
	accept();
}

void SuneerEditPhotoDialog::onResetAutomatic()
{
	m_resetAuto = true;   // Apply after this leaves the photo automatic (protect / exclude areas are kept)
	m_s = m_isObit ? obitPreset(m_doc) : Settings();
	if (!m_isObit)
		m_s.autoFace = true;
	QSignalBlocker b1(m_autoFaceChk), b2(m_removeBgChk), b3(m_bgColorChk), b4(m_toneChk), b5(m_toneSlider), b6(m_fineSlider), b7(m_freeChk), b8(m_modelCombo), b9(m_refineChk);
	m_autoFaceChk->setChecked(m_s.autoFace);
	m_removeBgChk->setChecked(m_s.removeBg);
	m_bgColorChk->setChecked(!m_s.bgColor.isEmpty());
	m_toneChk->setChecked(m_s.tone > 0);
	m_toneSlider->setValue(m_s.tone > 0 ? m_s.tone : 60);
	m_refineChk->setChecked(false);
	m_fineSlider->setValue(0);
	m_fineLabel->setText(QStringLiteral("0.0°"));
	m_freeChk->setChecked(false);
	m_modelCombo->setCurrentIndex(qMax(0, m_modelCombo->findData(m_s.model)));
	if (!m_brush.isNull())
	{
		beginMaskEdit();
		m_brush.fill(128);
		endMaskEdit(tr("Reset to automatic"));
	}
	onSelClear();
	rebuildTransform();
	resetCrop(m_s.autoFace);
	updateBgButton();
	{
		QSignalBlocker bm(m_bgModeCombo);
		m_bgModeCombo->setCurrentIndex(qMax(0, m_bgModeCombo->findData(m_s.removeBg ? m_s.bgMode : QStringLiteral("none"))));
		m_modelCombo->setEnabled(autoMode());
	}
	if (autoMode() && (m_cut.isNull() || m_cutModel != m_s.model))
		runRemoval();
	else
		schedulePreview();
}

void SuneerEditPhotoDialog::onResetOriginal()
{
	if (!m_item || !m_doc)
		return;
	QString err;
	if (!resetToOriginal(m_doc, m_item, &err))
	{
		QMessageBox::warning(this, tr("Edit photo"), err);
		return;
	}
	m_applied = true;
	accept();
}

// ---------------------------------------------------------------------------
// entry point
// ---------------------------------------------------------------------------

void SuneerPhotoEdit::openDialog(ScribusDoc* doc, PageItem* item, QWidget* parent)
{
	if (!doc || !item || !item->isImageFrame())
		return;
	if (item->Pfile.isEmpty() || !item->imageIsAvailable)
	{
		QMessageBox::information(parent, QObject::tr("Edit photo"), QObject::tr("The frame has no picture."));
		return;
	}
	SuneerEditPhotoDialog dlg(doc, item, parent);
	dlg.exec();
}
