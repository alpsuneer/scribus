/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/samselecttool.h"

#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPen>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QWidget>

#include "scimagesam.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

QCursor SamSelectTool::cursor() const
{
	return Qt::CrossCursor;
}

QWidget* SamSelectTool::optionsBar()
{
	// Fresh widget each activation (the editor's options toolbar owns/deletes it);
	// the tool keeps the state, so the toggle re-inits from m_addMode.
	auto* w = new QWidget;
	auto* lay = new QHBoxLayout(w);
	lay->setContentsMargins(6, 2, 6, 2);

	lay->addWidget(new QLabel(tr("Points:")));
	auto* addBtn = new QToolButton(w);
	addBtn->setText(tr("Add (+)"));
	addBtn->setCheckable(true);
	addBtn->setToolTip(tr("Click to add regions of the object to the selection"));
	auto* subBtn = new QToolButton(w);
	subBtn->setText(tr("Subtract (−)"));
	subBtn->setCheckable(true);
	subBtn->setToolTip(tr("Click to exclude regions from the selection (Alt-click does this too)"));
	auto* group = new QButtonGroup(w);
	group->setExclusive(true);
	group->addButton(addBtn);
	group->addButton(subBtn);
	addBtn->setChecked(m_addMode);
	subBtn->setChecked(!m_addMode);
	lay->addWidget(addBtn);
	lay->addWidget(subBtn);

	lay->addSpacing(14);
	lay->addWidget(new QLabel(tr("Combine:")));
	auto* combine = new QComboBox(w);
	combine->addItem(tr("New Selection"));        // ScImageSelection::Replace
	combine->addItem(tr("Add to Selection"));     // Add
	combine->addItem(tr("Subtract from Selection")); // Subtract
	combine->addItem(tr("Intersect with Selection")); // Intersect
	combine->setCurrentIndex(static_cast<int>(m_combineMode));
	combine->setToolTip(tr("How the segmented object merges with the existing selection"));
	lay->addWidget(combine);

	lay->addSpacing(14);
	auto* aa = new QCheckBox(tr("Anti-alias"), w);
	aa->setChecked(m_antialias);
	aa->setToolTip(tr("Smooth the mask edge from the model's soft output instead of a hard, jagged cut"));
	lay->addWidget(aa);

	lay->addSpacing(14);
	lay->addWidget(new QLabel(tr("Feather:")));
	auto* feather = new QSpinBox(w);
	feather->setRange(0, 250);
	feather->setValue(m_feather);
	feather->setSuffix(tr(" px"));
	feather->setToolTip(tr("Soften the edge of the segmented object"));
	lay->addWidget(feather);

	lay->addSpacing(14);
	auto* newBtn = new QPushButton(tr("New Object"), w);
	newBtn->setToolTip(tr("Start a new object: clear the prompt points but keep the current selection (so the next object can add to it)"));
	lay->addWidget(newBtn);

	lay->addStretch(1);

	connect(addBtn, &QToolButton::toggled, this, [this](bool on){ if (on) m_addMode = true; });
	connect(subBtn, &QToolButton::toggled, this, [this](bool on){ if (on) m_addMode = false; });
	connect(combine, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
		m_combineMode = static_cast<ScImageSelection::Mode>(idx);
		// Re-combine the current object live when the mode changes (no re-inference).
		if (m_ready && !m_lastSamMask.isNull())
			applyMask(m_lastSamMask);
	});
	connect(feather, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
		m_feather = v;
		// Re-feather + re-combine the current object live (no re-inference).
		if (m_ready && !m_lastSamMask.isNull())
			applyMask(m_lastSamMask);
	});
	connect(aa, &QCheckBox::toggled, this, [this](bool on){
		m_antialias = on;
		SamSegmenter::instance().setAntialias(on);
		// AA is baked into the decoder output, so re-run the last prompt (cheap:
		// the encoder embedding is cached, only the decoder runs again).
		resegment();
	});
	connect(newBtn, &QPushButton::clicked, this, [this]{
		// Reset the prompt for a new object; the current selection is kept so the
		// next object can add to / subtract from it via the combine mode.
		clearSession();
		showStatus(tr("Smart Select: click a new object (current selection kept)"));
	});
	return w;
}

void SamSelectTool::activate(ScImageEditor* editor)
{
	ImageTool::activate(editor);
	m_ready = false;
	clearSession();
	SamSegmenter& sam = SamSegmenter::instance();
	if (!sam.ensureLoaded())
	{
		showStatus(sam.statusMessage());
		return;
	}
	sam.setAntialias(m_antialias);
	// Encode the current image once (this is the expensive step).
	QApplication::setOverrideCursor(Qt::WaitCursor);
	showStatus(tr("Smart Select: analyzing image..."));
	m_ready = sam.setImage(m_editor->currentImage());
	QApplication::restoreOverrideCursor();
	showStatus(m_ready ? tr("Smart Select: click an object, or drag a box") : sam.statusMessage());
}

void SamSelectTool::deactivate()
{
	m_dragging = false;
	removeRubber();
	clearSession();
}

void SamSelectTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_editor)
		return;
	m_start = imagePos;
	m_dragging = true;
	removeRubber();
	QPen pen(Qt::DashLine);
	pen.setColor(Qt::white);
	pen.setCosmetic(true);
	m_rubber = m_editor->scene()->addRect(QRectF(m_start, m_start), pen);
	m_rubber->setZValue(1000);
}

void SamSelectTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_dragging)
		return;
	if (auto* item = qgraphicsitem_cast<QGraphicsRectItem*>(m_rubber))
		item->setRect(QRectF(m_start, imagePos).normalized());
}

void SamSelectTool::mouseRelease(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_dragging)
		return;
	m_dragging = false;
	removeRubber();
	if (!m_ready || !m_editor || !m_editor->selection())
		return;

	if (QLineF(m_start, imagePos).length() < 5.0)
	{
		// Point click: add to the running prompt and re-segment the same object.
		// Alt-click always subtracts, regardless of the options-bar toggle.
		const bool fg = m_addMode && !e->modifiers().testFlag(Qt::AltModifier);
		beginSessionIfNeeded();
		m_points.append(m_start.toPoint());
		m_labels.append(fg ? 1 : 0);
		addPointMarker(m_start, fg);
		refineFromPoints();
	}
	else
	{
		// Box: start a fresh object from the box prompt.
		clearSession();
		beginSessionIfNeeded();
		m_lastBox = QRect(m_start.toPoint(), imagePos.toPoint()).normalized();
		m_haveLastBox = true;
		applyMask(SamSegmenter::instance().segmentInBox(m_lastBox));
	}
}

void SamSelectTool::resegment()
{
	if (!m_ready || !m_editor || !m_editor->selection())
		return;
	if (!m_points.isEmpty())
		refineFromPoints();
	else if (m_haveLastBox)
		applyMask(SamSegmenter::instance().segmentInBox(m_lastBox));
}

void SamSelectTool::beginSessionIfNeeded()
{
	// Snapshot the selection that exists BEFORE this object, so a non-Replace
	// combine mode always merges the fresh SAM result into the same baseline
	// (re-running on each refine click / mode change would otherwise stack up).
	if (m_haveBaseline || !m_editor || !m_editor->selection())
		return;
	m_baseline = m_editor->selection()->mask().copy();
	m_haveBaseline = true;
}

void SamSelectTool::refineFromPoints()
{
	SamSegmenter& sam = SamSegmenter::instance();
	QImage mask = sam.segmentAtPoints(m_points, m_labels);
	if (mask.isNull())
	{
		showStatus(sam.statusMessage());
		return;
	}
	applyMask(mask);
	showStatus(tr("Smart Select: %n point(s) — Add/Subtract to refine, or New for a fresh object", "", m_points.size()));
}

void SamSelectTool::applyMask(const QImage& mask)
{
	if (mask.isNull())
	{
		showStatus(SamSegmenter::instance().statusMessage());
		return;
	}
	m_lastSamMask = mask;                       // store RAW result so feather/mode changes re-derive
	const QImage obj = featherMask(mask);       // soften the object edge per the Feather setting
	ScImageSelection* sel = m_editor->selection();
	// One undo entry: begin snapshots the current selection, then we restore the
	// baseline and merge the SAM object into it per the combine mode.
	m_editor->beginSelectionStroke();
	if (m_combineMode == ScImageSelection::Replace || !m_haveBaseline)
	{
		sel->setFromMask(obj, ScImageSelection::Replace);
	}
	else
	{
		sel->setFromMask(m_baseline, ScImageSelection::Replace);
		sel->setFromMask(obj, m_combineMode);
	}
	m_editor->endSelectionStroke(tr("Smart Select"));
}

QImage SamSelectTool::featherMask(const QImage& mask) const
{
	if (m_feather < 1 || mask.isNull())
		return mask;
	// Reuse the selection's tested box-blur feather via a throwaway selection.
	ScImageSelection tmp(mask.size());
	tmp.setFromMask(mask, ScImageSelection::Replace);
	tmp.feather(m_feather);
	return tmp.mask();
}

void SamSelectTool::addPointMarker(const QPointF& p, bool foreground)
{
	if (!m_editor || !m_editor->scene())
		return;
	const double r = 4.0;
	QPen pen(Qt::black);
	pen.setCosmetic(true);
	QBrush brush(foreground ? QColor(0, 220, 0) : QColor(230, 0, 0));
	QGraphicsItem* dot = m_editor->scene()->addEllipse(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r), pen, brush);
	dot->setZValue(1002);
	m_markers.append(dot);
}

void SamSelectTool::clearSession()
{
	m_points.clear();
	m_labels.clear();
	m_baseline = QImage();
	m_lastSamMask = QImage();
	m_haveBaseline = false;
	m_haveLastBox = false;
	m_lastBox = QRect();
	for (QGraphicsItem* m : m_markers)
	{
		if (m && m_editor && m_editor->scene())
			m_editor->scene()->removeItem(m);
		delete m;
	}
	m_markers.clear();
}

void SamSelectTool::removeRubber()
{
	if (m_rubber && m_editor && m_editor->scene())
		m_editor->scene()->removeItem(m_rubber);
	delete m_rubber;
	m_rubber = nullptr;
}

void SamSelectTool::showStatus(const QString& msg)
{
	if (m_editor)
		m_editor->statusBar()->showMessage(msg, 6000);
}
