/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/removaltoolwidget.h"

#include <QAction>
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

	m_applyBestButton = new QToolButton(this);
	m_applyBestButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	addWidget(m_applyBestButton);

	m_recommendLabel = new QLabel(this);
	m_recommendLabel->setContentsMargins(6, 0, 2, 0);
	m_recommendAction = addWidget(m_recommendLabel);
	m_recommendAction->setVisible(false);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setContentsMargins(8, 0, 4, 0);
	addWidget(m_hintLabel);

	connect(m_sizeSlider, &QSlider::valueChanged, this, &RemovalToolWidget::sizeChanged);
	connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &RemovalToolWidget::sizeChanged);
	connect(m_hardnessSlider, &QSlider::valueChanged, this, &RemovalToolWidget::hardnessChanged);
	connect(m_hardnessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &RemovalToolWidget::hardnessChanged);
	connect(m_clearButton, &QToolButton::clicked, this, &RemovalToolWidget::clearClicked);
	connect(m_applyButton, &QToolButton::clicked, this, &RemovalToolWidget::applyClicked);
	connect(m_applyBestButton, &QToolButton::clicked, this, &RemovalToolWidget::applyBestQualityClicked);

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
		m_applyButton->setText(tr("Apply (Fast)"));
		m_applyButton->setToolTip(tr("Rebuild the painted pixels from the ones around them, here on this machine. "
		                             "Instant, and the right choice for most removals. "
		                             "The result is saved as a new file; your original image is not changed."));
	}
	if (m_applyBestButton)
		m_applyBestButton->setText(tr("Apply (Best Quality)"));
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

	if (m_applyBestButton)
	{
		// The tooltip is the whole feature when the button is off: it is the
		// only place the user finds out whether to go to Preferences, start a
		// server, or simply paint something first.
		const QString blocked = mode ? mode->aiBlockedReason()
		                             : tr("Enable AI features in Preferences > AI Services");
		m_applyBestButton->setEnabled(blocked.isEmpty());
		if (blocked.isEmpty())
			m_applyBestButton->setToolTip(tr("Send the area around the mask to your local IOPaint server and "
			                                 "let the model fill it. Slower, and much better on people, "
			                                 "crowds and textured backgrounds. "
			                                 "The result is saved as a new file; your original image is not changed."));
		else
			m_applyBestButton->setToolTip(blocked);
	}

	if (m_recommendLabel)
	{
		// Advice for the case the built-in fill is known to be weak at, put
		// where the decision is made and nowhere near a dialog box.
		const bool large = mode && !running && mode->hasSelection() && mode->maskCoverage() > 0.05;
		if (large)
			m_recommendLabel->setText(QStringLiteral("<i>%1</i>")
			                          .arg(tr("Large area - Best Quality recommended")));
		if (m_recommendAction)
			m_recommendAction->setVisible(large);
	}

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

void RemovalToolWidget::applyBestQualityClicked()
{
	CanvasMode_RemovalMask* mode = CanvasMode_RemovalMask::active();
	if (mode)
		mode->applyRemovalAI();
}

void RemovalToolWidget::clearClicked()
{
	CanvasMode_RemovalMask* mode = CanvasMode_RemovalMask::active();
	if (mode)
		mode->clearMask();
}
