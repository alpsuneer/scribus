/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/refineedgesbrushtool.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPen>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor RefineEdgesBrushTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-select-brush"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

QWidget* RefineEdgesBrushTool::optionsBar()
{
	// A fresh widget each activation (the editor's options toolbar owns/deletes it);
	// the tool keeps the state, so the sliders re-init from the current values.
	auto* w = new QWidget;
	auto* lay = new QHBoxLayout(w);
	lay->setContentsMargins(6, 2, 6, 2);

	lay->addWidget(new QLabel(tr("Size:")));
	auto* sizeSlider = new QSlider(Qt::Horizontal, w);
	sizeSlider->setRange(2, 300);
	sizeSlider->setValue(qRound(m_radius));
	sizeSlider->setFixedWidth(150);
	auto* sizeSpin = new QSpinBox(w);
	sizeSpin->setRange(2, 300);
	sizeSpin->setValue(qRound(m_radius));
	sizeSpin->setSuffix(tr(" px"));
	lay->addWidget(sizeSlider);
	lay->addWidget(sizeSpin);

	lay->addSpacing(14);
	lay->addWidget(new QLabel(tr("Hardness:")));
	auto* hardSlider = new QSlider(Qt::Horizontal, w);
	hardSlider->setRange(0, 100);
	hardSlider->setValue(qRound(m_hardness * 100));
	hardSlider->setFixedWidth(120);
	auto* hardSpin = new QSpinBox(w);
	hardSpin->setRange(0, 100);
	hardSpin->setValue(qRound(m_hardness * 100));
	hardSpin->setSuffix(QStringLiteral("%"));
	lay->addWidget(hardSlider);
	lay->addWidget(hardSpin);

	lay->addSpacing(14);
	lay->addWidget(new QLabel(tr("(Alt = erase)")));
	lay->addStretch(1);

	connect(sizeSlider, &QSlider::valueChanged, sizeSpin, &QSpinBox::setValue);
	connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), sizeSlider, &QSlider::setValue);
	connect(sizeSlider, &QSlider::valueChanged, this, [this](int v){ m_radius = v; updateRing(m_lastPos); });
	connect(hardSlider, &QSlider::valueChanged, hardSpin, &QSpinBox::setValue);
	connect(hardSpin, QOverload<int>::of(&QSpinBox::valueChanged), hardSlider, &QSlider::setValue);
	connect(hardSlider, &QSlider::valueChanged, this, [this](int v){ m_hardness = v / 100.0; });

	m_sizeSlider = sizeSlider;   // QPointer — auto-nulls when the widget is deleted
	m_hardSlider = hardSlider;
	return w;
}

void RefineEdgesBrushTool::activate(ScImageEditor* editor)
{
	ImageTool::activate(editor);
	updateRing(QPointF(0, 0));
}

void RefineEdgesBrushTool::deactivate()
{
	m_painting = false;
	removeRing();
}

void RefineEdgesBrushTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor || !m_editor->selection())
		return;
	m_painting = true;
	m_subtract = e->modifiers().testFlag(Qt::AltModifier);
	m_lastPos = imagePos;
	m_editor->beginSelectionStroke();
	m_editor->selection()->paintBrush(imagePos, m_radius, m_hardness, !m_subtract);
	updateRing(imagePos);
}

void RefineEdgesBrushTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	updateRing(imagePos);
	if (!m_painting || !m_editor || !m_editor->selection())
		return;
	stampLine(m_lastPos, imagePos);
	m_lastPos = imagePos;
}

void RefineEdgesBrushTool::mouseRelease(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	Q_UNUSED(imagePos)
	if (!m_painting)
		return;
	m_painting = false;
	if (m_editor)
		m_editor->endSelectionStroke(m_subtract ? tr("Erase Selection") : tr("Refine Edges"));
}

void RefineEdgesBrushTool::keyPress(QKeyEvent* e)
{
	const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
	switch (e->key())
	{
		case Qt::Key_BracketLeft:
			if (shift)
				m_hardness = qMax(0.0, m_hardness - 0.1);
			else
				m_radius = qMax(2.0, m_radius - qMax(2.0, m_radius * 0.2));
			syncOptionsBar();
			updateRing(m_lastPos);
			e->accept();
			break;
		case Qt::Key_BracketRight:
			if (shift)
				m_hardness = qMin(1.0, m_hardness + 0.1);
			else
				m_radius = qMin(1000.0, m_radius + qMax(2.0, m_radius * 0.2));
			syncOptionsBar();
			updateRing(m_lastPos);
			e->accept();
			break;
		default:
			break;
	}
}

void RefineEdgesBrushTool::stampLine(const QPointF& from, const QPointF& to)
{
	// Dab along the segment so a fast drag stays continuous.
	const double len = QLineF(from, to).length();
	const double step = qMax(1.0, m_radius * 0.25);
	const int n = static_cast<int>(len / step);
	for (int i = 1; i <= n; ++i)
	{
		double t = static_cast<double>(i) / (n + 1);
		QPointF p = from + (to - from) * t;
		m_editor->selection()->paintBrush(p, m_radius, m_hardness, !m_subtract);
	}
	m_editor->selection()->paintBrush(to, m_radius, m_hardness, !m_subtract);
}

void RefineEdgesBrushTool::syncOptionsBar()
{
	if (m_sizeSlider)
		m_sizeSlider->setValue(qRound(m_radius));
	if (m_hardSlider)
		m_hardSlider->setValue(qRound(m_hardness * 100));
}

void RefineEdgesBrushTool::updateRing(const QPointF& pos)
{
	if (!m_editor || !m_editor->scene())
		return;
	QRectF r(pos.x() - m_radius, pos.y() - m_radius, 2 * m_radius, 2 * m_radius);
	if (!m_ring)
	{
		QPen pen(Qt::DashLine);
		pen.setColor(Qt::white);
		pen.setCosmetic(true);
		m_ring = m_editor->scene()->addEllipse(r, pen);
		m_ring->setZValue(1001);
	}
	else
	{
		m_ring->setRect(r);
	}
}

void RefineEdgesBrushTool::removeRing()
{
	if (m_ring && m_editor && m_editor->scene())
		m_editor->scene()->removeItem(m_ring);
	delete m_ring;
	m_ring = nullptr;
}
