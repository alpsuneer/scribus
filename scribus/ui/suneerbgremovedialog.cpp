/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerbgremovedialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
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
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

// ---- view -----------------------------------------------------------------

SuneerBgView::SuneerBgView(QWidget* parent) : QGraphicsView(parent)
{
	setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	setTransformationAnchor(AnchorUnderMouse);
	setDragMode(NoDrag);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
}

void SuneerBgView::fitImage()
{
	if (scene())
		fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
}

void SuneerBgView::drawBackground(QPainter* painter, const QRectF& rect)
{
	// Checkerboard so transparency is visible.
	static QPixmap checks;
	if (checks.isNull())
	{
		checks = QPixmap(16, 16);
		checks.fill(QColor(200, 200, 200));
		QPainter p(&checks);
		p.fillRect(0, 0, 8, 8, QColor(240, 240, 240));
		p.fillRect(8, 8, 8, 8, QColor(240, 240, 240));
	}
	painter->save();
	painter->resetTransform();
	painter->fillRect(viewport()->rect(), QBrush(checks));
	painter->restore();
	Q_UNUSED(rect);
}

void SuneerBgView::wheelEvent(QWheelEvent* e)
{
	const double f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
	scale(f, f);
	e->accept();
}

void SuneerBgView::mousePressEvent(QMouseEvent* e)
{
	if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_space))
	{
		m_panning = true;
		m_lastPan = e->pos();
		setCursor(Qt::ClosedHandCursor);
		e->accept();
		return;
	}
	m_down = e->button() == Qt::LeftButton;
	emit toolPress(mapToScene(e->pos()), e->button());
	e->accept();
}

void SuneerBgView::mouseMoveEvent(QMouseEvent* e)
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

void SuneerBgView::mouseReleaseEvent(QMouseEvent* e)
{
	if (m_panning)
	{
		m_panning = false;
		setCursor(m_space ? Qt::OpenHandCursor : Qt::ArrowCursor);
		e->accept();
		return;
	}
	if (e->button() == Qt::LeftButton)
	{
		m_down = false;
		emit toolRelease(mapToScene(e->pos()));
	}
	e->accept();
}

void SuneerBgView::keyPressEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Space && !e->isAutoRepeat())
	{
		m_space = true;
		setCursor(Qt::OpenHandCursor);
		return;
	}
	QGraphicsView::keyPressEvent(e);
}

void SuneerBgView::keyReleaseEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Space && !e->isAutoRepeat())
	{
		m_space = false;
		if (!m_panning)
			setCursor(Qt::ArrowCursor);
		return;
	}
	QGraphicsView::keyReleaseEvent(e);
}

// ---- undo command ---------------------------------------------------------

class SuneerBgSnapshotCommand : public QUndoCommand
{
public:
	SuneerBgSnapshotCommand(SuneerBgRemoveDialog* dlg, const QPainterPath& beforeSel, const QImage& beforeBrush,
	                        const QPainterPath& afterSel, const QImage& afterBrush, const QString& label)
		: QUndoCommand(label), m_dlg(dlg), m_bSel(beforeSel), m_bBrush(beforeBrush), m_aSel(afterSel), m_aBrush(afterBrush) {}
	void undo() override { m_dlg->applySnapshot(m_bSel, m_bBrush); }
	void redo() override { if (m_first) { m_first = false; return; } m_dlg->applySnapshot(m_aSel, m_aBrush); }
private:
	SuneerBgRemoveDialog* m_dlg;
	QPainterPath m_bSel, m_aSel;
	QImage m_bBrush, m_aBrush;
	bool m_first { true };
};

// ---- dialog ---------------------------------------------------------------

SuneerBgRemoveDialog::SuneerBgRemoveDialog(const QImage& source, const State& initial, bool hasOriginal, QWidget* parent)
	: QDialog(parent), m_source(source.convertToFormat(QImage::Format_ARGB32)), m_state(initial), m_hasOriginal(hasOriginal)
{
	setWindowTitle(tr("Remove Background"));
	setMinimumSize(900, 620);
	resize(1100, 720);

	m_undo = new QUndoStack(this);
	m_remover = new SuneerBgRemover(this);
	connect(m_remover, &SuneerBgRemover::finished, this, &SuneerBgRemoveDialog::onRemovalFinished);
	connect(m_remover, &SuneerBgRemover::failed, this, &SuneerBgRemoveDialog::onRemovalFailed);

	// canvas
	m_scene = new QGraphicsScene(this);
	m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(m_source.size())));
	m_pix = m_scene->addPixmap(QPixmap::fromImage(m_source));
	m_outline = m_scene->addPath(QPainterPath(), QPen(QColor(0, 120, 255), 0, Qt::DashLine));
	m_outline->setZValue(5);
	m_rubber = m_scene->addPath(QPainterPath(), QPen(QColor(255, 60, 0), 0, Qt::DashLine));
	m_rubber->setZValue(6);
	m_cursor = m_scene->addEllipse(QRectF(), QPen(QColor(0, 0, 0), 0), Qt::NoBrush);
	m_cursor->setZValue(7);
	m_cursor->setVisible(false);
	m_view = new SuneerBgView(this);
	m_view->setScene(m_scene);
	connect(m_view, &SuneerBgView::toolPress, this, &SuneerBgRemoveDialog::onToolPress);
	connect(m_view, &SuneerBgView::toolMove, this, &SuneerBgRemoveDialog::onToolMove);
	connect(m_view, &SuneerBgView::toolRelease, this, &SuneerBgRemoveDialog::onToolRelease);

	// right panel
	auto* panel = new QVBoxLayout();
	auto makeTool = [this](const QString& t, const QString& tip) {
		auto* b = new QToolButton(this);
		b->setText(t); b->setToolTip(tip); b->setCheckable(true); b->setMinimumWidth(64);
		return b;
	};
	auto* toolBox = new QGroupBox(tr("Select"), this);
	auto* toolLay = new QVBoxLayout(toolBox);
	auto* toolRow = new QHBoxLayout();
	m_rectBtn = makeTool(tr("Rect"), tr("Drag a rectangle"));
	m_lassoBtn = makeTool(tr("Lasso"), tr("Click points; click the first point (or double-click) to close"));
	m_panBtn = makeTool(tr("Pan"), tr("Drag to move the view (Space + drag or middle button work with any tool)"));
	toolRow->addWidget(m_rectBtn); toolRow->addWidget(m_lassoBtn); toolRow->addWidget(m_panBtn);
	toolLay->addLayout(toolRow);
	auto* addRow = new QHBoxLayout();
	m_addRadio = new QRadioButton(tr("Add"), this);
	m_subRadio = new QRadioButton(tr("Subtract"), this);
	m_addRadio->setChecked(true);
	addRow->addWidget(m_addRadio); addRow->addWidget(m_subRadio);
	toolLay->addLayout(addRow);
	auto* selBtns = new QHBoxLayout();
	auto* clearBtn = new QPushButton(tr("Clear"), this);
	auto* invBtn = new QPushButton(tr("Invert"), this);
	selBtns->addWidget(clearBtn); selBtns->addWidget(invBtn);
	toolLay->addLayout(selBtns);
	connect(clearBtn, &QPushButton::clicked, this, &SuneerBgRemoveDialog::clearSelection);
	connect(invBtn, &QPushButton::clicked, this, &SuneerBgRemoveDialog::invertSelection);
	panel->addWidget(toolBox);

	auto* modeBox = new QGroupBox(tr("Mode"), this);
	auto* modeLay = new QVBoxLayout(modeBox);
	m_wholeRadio = new QRadioButton(tr("Whole image"), this);
	m_insideRadio = new QRadioButton(tr("Only inside selection"), this);
	m_keepRadio = new QRadioButton(tr("Keep selected subject only"), this);
	modeLay->addWidget(m_wholeRadio); modeLay->addWidget(m_insideRadio); modeLay->addWidget(m_keepRadio);
	auto* modelRow = new QHBoxLayout();
	modelRow->addWidget(new QLabel(tr("Model:"), this));
	m_modelCombo = new QComboBox(this);
	m_modelCombo->addItem("u2net_human_seg - Portrait / Human", "u2net_human_seg");
	m_modelCombo->addItem("u2net - General objects", "u2net");
	m_modelCombo->addItem("isnet-general-use - High detail edges", "isnet-general-use");
	m_modelCombo->addItem("silueta - Silhouette / fast", "silueta");
	modelRow->addWidget(m_modelCombo, 1);
	modeLay->addLayout(modelRow);
	auto* featherRow = new QHBoxLayout();
	featherRow->addWidget(new QLabel(tr("Edge feather (px):"), this));
	m_featherSpin = new QSpinBox(this);
	m_featherSpin->setRange(0, 50);
	featherRow->addWidget(m_featherSpin);
	modeLay->addLayout(featherRow);
	m_runBtn = new QPushButton(tr("Run background removal"), this);
	m_cancelRunBtn = new QPushButton(tr("Cancel"), this);
	m_cancelRunBtn->setVisible(false);
	m_progress = new QProgressBar(this);
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	auto* runRow = new QHBoxLayout();
	runRow->addWidget(m_runBtn, 1); runRow->addWidget(m_cancelRunBtn);
	modeLay->addLayout(runRow);
	modeLay->addWidget(m_progress);
	connect(m_runBtn, &QPushButton::clicked, this, &SuneerBgRemoveDialog::runRemoval);
	connect(m_cancelRunBtn, &QPushButton::clicked, this, &SuneerBgRemoveDialog::cancelRemoval);
	panel->addWidget(modeBox);

	auto* brushBox = new QGroupBox(tr("Refine"), this);
	auto* brushLay = new QVBoxLayout(brushBox);
	auto* brushRow = new QHBoxLayout();
	m_eraseBtn = makeTool(tr("Erase"), tr("Paint to make pixels transparent"));
	m_restoreBtn = makeTool(tr("Restore"), tr("Paint to bring the original pixels back"));
	brushRow->addWidget(m_eraseBtn); brushRow->addWidget(m_restoreBtn);
	brushLay->addLayout(brushRow);
	auto* sizeRow = new QHBoxLayout();
	sizeRow->addWidget(new QLabel(tr("Size:"), this));
	m_sizeSlider = new QSlider(Qt::Horizontal, this);
	m_sizeSlider->setRange(2, 400); m_sizeSlider->setValue(40);
	sizeRow->addWidget(m_sizeSlider, 1);
	brushLay->addLayout(sizeRow);
	auto* softRow = new QHBoxLayout();
	softRow->addWidget(new QLabel(tr("Softness:"), this));
	m_softSlider = new QSlider(Qt::Horizontal, this);
	m_softSlider->setRange(0, 100); m_softSlider->setValue(50);
	softRow->addWidget(m_softSlider, 1);
	brushLay->addLayout(softRow);
	auto* undoRow = new QHBoxLayout();
	m_undoBtn = new QPushButton(tr("Undo"), this);
	m_redoBtn = new QPushButton(tr("Redo"), this);
	undoRow->addWidget(m_undoBtn); undoRow->addWidget(m_redoBtn);
	brushLay->addLayout(undoRow);
	connect(m_undoBtn, &QPushButton::clicked, m_undo, &QUndoStack::undo);
	connect(m_redoBtn, &QPushButton::clicked, m_undo, &QUndoStack::redo);
	connect(m_undo, &QUndoStack::canUndoChanged, m_undoBtn, &QPushButton::setEnabled);
	connect(m_undo, &QUndoStack::canRedoChanged, m_redoBtn, &QPushButton::setEnabled);
	m_undoBtn->setEnabled(false); m_redoBtn->setEnabled(false);
	panel->addWidget(brushBox);

	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	panel->addWidget(m_status);
	panel->addStretch(1);

	m_resetBtn = new QPushButton(tr("Reset to original"), this);
	m_resetBtn->setEnabled(hasOriginal);
	m_resetBtn->setToolTip(hasOriginal ? tr("Put the picture back to the file it was before background removal")
	                                   : tr("This frame has not had its background removed"));
	panel->addWidget(m_resetBtn);
	connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, tr("Reset to original"), tr("Put the original picture back and forget the selection and brush work?")) != QMessageBox::Yes)
			return;
		m_reset = true;
		accept();
	});
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	m_applyBtn = bb->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
	m_applyBtn->setDefault(true);
	connect(bb, &QDialogButtonBox::accepted, this, [this]() {
		if (m_state.cut.isNull() && m_state.mode != SuneerBgRemove::Mode::Whole && m_state.selection.isEmpty())
		{
			QMessageBox::information(this, tr("Remove Background"), tr("Run the background removal first."));
			return;
		}
		if (m_state.cut.isNull())
		{
			QMessageBox::information(this, tr("Remove Background"), tr("Press \"Run background removal\" first."));
			return;
		}
		accept();
	});
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	panel->addWidget(bb);

	auto* main = new QHBoxLayout(this);
	main->addWidget(m_view, 1);
	auto* side = new QWidget(this);
	side->setLayout(panel);
	side->setFixedWidth(300);
	main->addWidget(side);

	// tool groups
	auto* tools = new QButtonGroup(this);
	for (QToolButton* b : { m_rectBtn, m_lassoBtn, m_eraseBtn, m_restoreBtn, m_panBtn })
		tools->addButton(b);
	tools->setExclusive(true);
	m_rectBtn->setChecked(true);
	auto* modes = new QButtonGroup(this);
	modes->addButton(m_wholeRadio); modes->addButton(m_insideRadio); modes->addButton(m_keepRadio);
	connect(modes, &QButtonGroup::buttonClicked, this, &SuneerBgRemoveDialog::onModeChanged);

	// initial state
	switch (m_state.mode)
	{
		case SuneerBgRemove::Mode::Inside: m_insideRadio->setChecked(true); break;
		case SuneerBgRemove::Mode::KeepSubject: m_keepRadio->setChecked(true); break;
		default: m_wholeRadio->setChecked(m_state.selection.isEmpty()); if (!m_state.selection.isEmpty()) { m_insideRadio->setChecked(true); m_state.mode = SuneerBgRemove::Mode::Inside; } break;
	}
	const int mi = m_modelCombo->findData(m_state.model);
	m_modelCombo->setCurrentIndex(mi < 0 ? 0 : mi);
	m_featherSpin->setValue(m_state.feather);
	if (!m_state.brush.isNull() && m_state.brush.size() != m_source.size())
		m_state.brush = QImage();

	m_previewTimer = new QTimer(this);
	m_previewTimer->setSingleShot(true);
	m_previewTimer->setInterval(60);
	connect(m_previewTimer, &QTimer::timeout, this, &SuneerBgRemoveDialog::updatePreview);

	refreshOutline();
	updatePreview();
	if (!m_state.cut.isNull())
		m_status->setText(tr("Reopened with the earlier result. Change the selection and run again, or refine with the brushes."));
	else
		m_status->setText(tr("Select a part (or none for the whole image), pick a mode, then run."));
	QTimer::singleShot(0, m_view, &SuneerBgView::fitImage);
}

SuneerBgRemoveDialog::~SuneerBgRemoveDialog()
{
	if (m_remover)
		m_remover->cancel();
}

SuneerBgRemoveDialog::Tool SuneerBgRemoveDialog::tool() const
{
	if (m_lassoBtn->isChecked()) return Lasso;
	if (m_eraseBtn->isChecked()) return Erase;
	if (m_restoreBtn->isChecked()) return Restore;
	if (m_panBtn->isChecked()) return Pan;
	return Rect;
}

bool SuneerBgRemoveDialog::subtractMode() const
{
	return m_subRadio->isChecked();
}

QImage SuneerBgRemoveDialog::result() const
{
	if (m_state.cut.isNull())
		return QImage();
	const QImage selMask = SuneerBgRemove::maskFromPath(m_state.selection, m_source.size());
	return SuneerBgRemove::compose(m_source, m_state.cut, m_state.cutRect, selMask, m_state.mode, m_state.brush);
}

void SuneerBgRemoveDialog::onModeChanged()
{
	m_state.mode = m_wholeRadio->isChecked() ? SuneerBgRemove::Mode::Whole
	             : m_insideRadio->isChecked() ? SuneerBgRemove::Mode::Inside : SuneerBgRemove::Mode::KeepSubject;
	// A result made for another region is not valid for this mode.
	if (!m_state.cut.isNull())
	{
		const QRect want = SuneerBgRemove::regionFor(SuneerBgRemove::maskFromPath(m_state.selection, m_source.size()), m_state.mode, m_source.size());
		if (want != m_state.cutRect)
			m_status->setText(tr("Mode changed: run the background removal again for this region."));
	}
	m_previewTimer->start();
}

void SuneerBgRemoveDialog::pushUndo(const QString& label)
{
	m_undo->push(new SuneerBgSnapshotCommand(this, m_undoSel, m_undoBrush, m_state.selection, m_state.brush, label));
}

void SuneerBgRemoveDialog::applySnapshot(const QPainterPath& sel, const QImage& brush)
{
	m_state.selection = sel;
	m_state.brush = brush;
	refreshOutline();
	m_previewTimer->start();
}

void SuneerBgRemoveDialog::commitShape(const QPainterPath& shapeIn)
{
	QPainterPath shape = shapeIn;
	shape.closeSubpath();
	m_undoSel = m_state.selection;
	m_undoBrush = m_state.brush;
	if (subtractMode())
		m_state.selection = m_state.selection.subtracted(shape);
	else
		m_state.selection = m_state.selection.isEmpty() ? shape : m_state.selection.united(shape);
	m_state.selection.setFillRule(Qt::WindingFill);
	if (m_wholeRadio->isChecked() && !m_state.selection.isEmpty())
	{
		m_insideRadio->setChecked(true);
		m_state.mode = SuneerBgRemove::Mode::Inside;
	}
	pushUndo(subtractMode() ? tr("Subtract from selection") : tr("Add to selection"));
	refreshOutline();
	m_previewTimer->start();
}

void SuneerBgRemoveDialog::clearSelection()
{
	if (m_state.selection.isEmpty())
		return;
	m_undoSel = m_state.selection; m_undoBrush = m_state.brush;
	m_state.selection = QPainterPath();
	pushUndo(tr("Clear selection"));
	refreshOutline();
	m_previewTimer->start();
}

void SuneerBgRemoveDialog::invertSelection()
{
	m_undoSel = m_state.selection; m_undoBrush = m_state.brush;
	QPainterPath all;
	all.addRect(QRectF(QPointF(0, 0), QSizeF(m_source.size())));
	m_state.selection = m_state.selection.isEmpty() ? all : all.subtracted(m_state.selection);
	pushUndo(tr("Invert selection"));
	refreshOutline();
	m_previewTimer->start();
}

void SuneerBgRemoveDialog::refreshOutline()
{
	m_outline->setPath(m_state.selection);
}

void SuneerBgRemoveDialog::ensureBrush()
{
	if (m_state.brush.isNull())
	{
		m_state.brush = QImage(m_source.size(), QImage::Format_Grayscale8);
		m_state.brush.fill(128);
	}
}

void SuneerBgRemoveDialog::stampBrush(const QPointF& p)
{
	ensureBrush();
	const double r = m_sizeSlider->value() / 2.0;
	const double soft = m_softSlider->value() / 100.0;
	const bool erase = tool() == Erase;
	const int x0 = qMax(0, int(std::floor(p.x() - r))), x1 = qMin(m_source.width() - 1, int(std::ceil(p.x() + r)));
	const int y0 = qMax(0, int(std::floor(p.y() - r))), y1 = qMin(m_source.height() - 1, int(std::ceil(p.y() + r)));
	for (int y = y0; y <= y1; ++y)
	{
		uchar* line = m_state.brush.scanLine(y);
		for (int x = x0; x <= x1; ++x)
		{
			const double d = std::hypot(x + 0.5 - p.x(), y + 0.5 - p.y());
			if (d > r)
				continue;
			double k = 1.0;
			const double hard = r * (1.0 - soft);
			if (d > hard && r > hard)
				k = 1.0 - (d - hard) / (r - hard);
			const int cur = line[x];
			const int target = erase ? 0 : 255;
			const int v = int(std::lround(cur + (target - cur) * k));
			line[x] = uchar(qBound(0, v, 255));
		}
	}
}

void SuneerBgRemoveDialog::onToolPress(const QPointF& p, Qt::MouseButton b)
{
	const Tool t = tool();
	if (t == Pan)
		return;
	if (b == Qt::RightButton)
	{
		if (t == Lasso && m_lassoActive)
		{
			// Right click: close the lasso where it is.
			m_lassoActive = false;
			m_rubber->setPath(QPainterPath());
			if (m_lasso.size() >= 3)
			{
				QPainterPath path; path.addPolygon(m_lasso); commitShape(path);
			}
			m_lasso.clear();
		}
		return;
	}
	if (b != Qt::LeftButton)
		return;
	if (t == Rect)
	{
		m_pressPos = p;
		m_dragging = true;
	}
	else if (t == Lasso)
	{
		if (!m_lassoActive)
		{
			m_lassoActive = true;
			m_lasso.clear();
			m_lasso << p;
		}
		else
		{
			const double closeDist = 8.0 / qMax(0.01, m_view->transform().m11());
			if (m_lasso.size() >= 3 && QLineF(p, m_lasso.first()).length() <= closeDist)
			{
				m_lassoActive = false;
				m_rubber->setPath(QPainterPath());
				QPainterPath path; path.addPolygon(m_lasso); commitShape(path);
				m_lasso.clear();
				return;
			}
			m_lasso << p;
		}
		QPainterPath rp; rp.addPolygon(m_lasso); m_rubber->setPath(rp);
	}
	else if (t == Erase || t == Restore)
	{
		m_undoSel = m_state.selection;
		m_undoBrush = m_state.brush.isNull() ? QImage() : m_state.brush.copy();
		m_dragging = true;
		m_lastStamp = p;
		stampBrush(p);
		m_previewTimer->start();
	}
}

void SuneerBgRemoveDialog::onToolMove(const QPointF& p, bool down)
{
	const Tool t = tool();
	if (t == Erase || t == Restore)
	{
		const double r = m_sizeSlider->value() / 2.0;
		m_cursor->setRect(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r));
		m_cursor->setVisible(true);
		if (down && m_dragging)
		{
			// Stamp along the stroke so fast moves leave no gaps.
			const double step = qMax(1.0, r / 3.0);
			const QLineF seg(m_lastStamp, p);
			const int n = qMax(1, int(seg.length() / step));
			for (int i = 1; i <= n; ++i)
				stampBrush(seg.pointAt(double(i) / n));
			m_lastStamp = p;
			m_previewTimer->start();
		}
		return;
	}
	m_cursor->setVisible(false);
	if (t == Rect && m_dragging && down)
	{
		QPainterPath rp; rp.addRect(QRectF(m_pressPos, p).normalized()); m_rubber->setPath(rp);
	}
	else if (t == Lasso && m_lassoActive)
	{
		QPolygonF poly = m_lasso; poly << p;
		QPainterPath rp; rp.addPolygon(poly); m_rubber->setPath(rp);
	}
}

void SuneerBgRemoveDialog::onToolRelease(const QPointF& p)
{
	const Tool t = tool();
	if (t == Rect && m_dragging)
	{
		m_dragging = false;
		m_rubber->setPath(QPainterPath());
		const QRectF r = QRectF(m_pressPos, p).normalized().intersected(QRectF(QPointF(0, 0), QSizeF(m_source.size())));
		if (r.width() >= 2 && r.height() >= 2)
		{
			QPainterPath path; path.addRect(r); commitShape(path);
		}
	}
	else if ((t == Erase || t == Restore) && m_dragging)
	{
		m_dragging = false;
		pushUndo(t == Erase ? tr("Erase") : tr("Restore"));
		m_previewTimer->start();
	}
}

void SuneerBgRemoveDialog::setBusy(bool on, const QString& text)
{
	m_progress->setVisible(on);
	m_cancelRunBtn->setVisible(on);
	m_runBtn->setEnabled(!on);
	m_applyBtn->setEnabled(!on);
	if (!text.isEmpty())
		m_status->setText(text);
}

void SuneerBgRemoveDialog::runRemoval()
{
	m_state.model = m_modelCombo->currentData().toString();
	m_state.feather = m_featherSpin->value();
	const QImage selMask = SuneerBgRemove::maskFromPath(m_state.selection, m_source.size());
	if (m_state.mode != SuneerBgRemove::Mode::Whole && selMask.isNull())
	{
		QMessageBox::information(this, tr("Remove Background"), tr("Draw a selection first, or choose \"Whole image\"."));
		return;
	}
	const QRect region = SuneerBgRemove::regionFor(selMask, m_state.mode, m_source.size());
	setBusy(true, tr("Running rembg on %1 x %2 pixels... the window stays usable; press Cancel to stop.").arg(region.width()).arg(region.height()));
	m_remover->start(m_source, region, m_state.model, m_state.feather);
}

void SuneerBgRemoveDialog::cancelRemoval()
{
	m_remover->cancel();
	setBusy(false, tr("Cancelled."));
}

void SuneerBgRemoveDialog::onRemovalFinished(const QImage& cut)
{
	m_state.cut = cut;
	m_state.cutRect = m_remover->region();
	setBusy(false, tr("Done. Refine with Erase / Restore if needed, then Apply."));
	updatePreview();
}

void SuneerBgRemoveDialog::onRemovalFailed(const QString& err)
{
	setBusy(false, tr("Background removal failed: %1").arg(err));
	QMessageBox::warning(this, tr("Remove Background"), err);
}

void SuneerBgRemoveDialog::updatePreview()
{
	const QImage shown = m_state.cut.isNull() ? m_source : result();
	m_pix->setPixmap(QPixmap::fromImage(shown));
}
