/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/removaltoolwidget.h"

#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

#include "canvas.h"
#include "canvasmode_removalmask.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"

RemovalToolWidget::RemovalToolWidget(ScribusMainWindow* mw)
	: QToolBar(tr("Remove Object"), mw), m_ScMW(mw)
{
	setObjectName(QStringLiteral("RemovalToolOptions"));

	m_sizeLabel = new QLabel(this);
	addWidget(m_sizeLabel);

	m_sizeSlider = new QSlider(Qt::Horizontal, this);
	m_sizeSlider->setRange(1, 500);
	m_sizeSlider->setValue(CanvasMode_RemovalMask::brushSize());
	m_sizeSlider->setFixedWidth(140);
	addWidget(m_sizeSlider);

	m_sizeSpin = new QSpinBox(this);
	m_sizeSpin->setRange(1, 500);
	m_sizeSpin->setValue(CanvasMode_RemovalMask::brushSize());
	m_sizeSpin->setSuffix(QStringLiteral(" px"));
	addWidget(m_sizeSpin);

	addSeparator();

	m_hardnessLabel = new QLabel(this);
	addWidget(m_hardnessLabel);

	m_hardnessSlider = new QSlider(Qt::Horizontal, this);
	m_hardnessSlider->setRange(0, 100);
	m_hardnessSlider->setValue(CanvasMode_RemovalMask::brushHardness());
	m_hardnessSlider->setFixedWidth(100);
	addWidget(m_hardnessSlider);

	m_hardnessSpin = new QSpinBox(this);
	m_hardnessSpin->setRange(0, 100);
	m_hardnessSpin->setValue(CanvasMode_RemovalMask::brushHardness());
	m_hardnessSpin->setSuffix(QStringLiteral(" %"));
	addWidget(m_hardnessSpin);

	addSeparator();

	m_clearButton = new QToolButton(this);
	m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	addWidget(m_clearButton);

	m_applyButton = new QToolButton(this);
	m_applyButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	addWidget(m_applyButton);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setContentsMargins(8, 0, 4, 0);
	addWidget(m_hintLabel);

	connect(m_sizeSlider, &QSlider::valueChanged, this, &RemovalToolWidget::sizeChanged);
	connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &RemovalToolWidget::sizeChanged);
	connect(m_hardnessSlider, &QSlider::valueChanged, this, &RemovalToolWidget::hardnessChanged);
	connect(m_hardnessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &RemovalToolWidget::hardnessChanged);
	connect(m_clearButton, &QToolButton::clicked, this, &RemovalToolWidget::clearClicked);
	connect(m_applyButton, &QToolButton::clicked, this, &RemovalToolWidget::applyClicked);

	languageChange();
	refreshFromMode();
}

void RemovalToolWidget::languageChange()
{
	setWindowTitle(tr("Remove Object"));
	if (m_sizeLabel)
		m_sizeLabel->setText(tr("Brush:"));
	if (m_hardnessLabel)
		m_hardnessLabel->setText(tr("Hardness:"));
	if (m_sizeSlider)
		m_sizeSlider->setToolTip(tr("Brush diameter in image pixels. [ and ] resize it from the canvas."));
	if (m_sizeSpin)
		m_sizeSpin->setToolTip(m_sizeSlider ? m_sizeSlider->toolTip() : QString());
	if (m_hardnessSlider)
		m_hardnessSlider->setToolTip(tr("0% is a fully feathered edge, 100% a hard one."));
	if (m_hardnessSpin)
		m_hardnessSpin->setToolTip(m_hardnessSlider ? m_hardnessSlider->toolTip() : QString());
	if (m_clearButton)
	{
		m_clearButton->setText(tr("Clear Mask"));
		m_clearButton->setToolTip(tr("Throw away everything painted so far"));
	}
	if (m_applyButton)
	{
		m_applyButton->setText(tr("Apply"));
		m_applyButton->setToolTip(tr("Rebuild the painted pixels from the ones around them. "
		                             "The result is saved as a new file; your original image is not changed."));
	}
	if (m_hintLabel)
		m_hintLabel->setText(tr("Alt: unpaint   Shift: straight line"));
}

void RemovalToolWidget::refreshFromMode()
{
	CanvasMode_RemovalMask* mode = CanvasMode_RemovalMask::active();
	const bool running = mode && mode->isRunning();
	const bool ready = mode && !running && mode->hasSelection();

	if (m_applyButton)
		m_applyButton->setEnabled(ready);
	if (m_clearButton)
		m_clearButton->setEnabled(mode && !running && mode->hasSelection());

	if (m_updating)
		return;
	m_updating = true;
	if (m_sizeSlider)
		m_sizeSlider->setValue(CanvasMode_RemovalMask::brushSize());
	if (m_sizeSpin)
		m_sizeSpin->setValue(CanvasMode_RemovalMask::brushSize());
	if (m_hardnessSlider)
		m_hardnessSlider->setValue(CanvasMode_RemovalMask::brushHardness());
	if (m_hardnessSpin)
		m_hardnessSpin->setValue(CanvasMode_RemovalMask::brushHardness());
	m_updating = false;
}

void RemovalToolWidget::sizeChanged(int value)
{
	if (m_updating)
		return;
	m_updating = true;
	CanvasMode_RemovalMask::setBrushSize(value);
	const int applied = CanvasMode_RemovalMask::brushSize();
	if (m_sizeSlider)
		m_sizeSlider->setValue(applied);
	if (m_sizeSpin)
		m_sizeSpin->setValue(applied);
	m_updating = false;

	// Redraw so the brush ring on the canvas follows the new size immediately.
	// The ring is painted by the canvas mode's drawControls(), which runs from
	// Canvas::paintEvent, so the canvas is what needs repainting.
	if (m_ScMW && m_ScMW->doc && m_ScMW->view && m_ScMW->view->m_canvas)
		m_ScMW->view->m_canvas->update();
}

void RemovalToolWidget::hardnessChanged(int value)
{
	if (m_updating)
		return;
	m_updating = true;
	CanvasMode_RemovalMask::setBrushHardness(value);
	const int applied = CanvasMode_RemovalMask::brushHardness();
	if (m_hardnessSlider)
		m_hardnessSlider->setValue(applied);
	if (m_hardnessSpin)
		m_hardnessSpin->setValue(applied);
	m_updating = false;

	if (m_ScMW && m_ScMW->doc && m_ScMW->view && m_ScMW->view->m_canvas)
		m_ScMW->view->m_canvas->update();
}

void RemovalToolWidget::applyClicked()
{
	CanvasMode_RemovalMask* mode = CanvasMode_RemovalMask::active();
	if (mode)
		mode->applyRemoval();
}

void RemovalToolWidget::clearClicked()
{
	CanvasMode_RemovalMask* mode = CanvasMode_RemovalMask::active();
	if (mode)
		mode->clearMask();
}
