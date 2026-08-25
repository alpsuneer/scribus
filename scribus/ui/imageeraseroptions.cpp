/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/imageeraseroptions.h"

#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

#include "canvasmode_imageeraser.h"
#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "scimageerasermask.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "canvas.h"
#include "scribusview.h"
#include "selection.h"
#include "undomanager.h"
#include "undostate.h"

ImageEraserOptions::ImageEraserOptions(ScribusMainWindow* mw)
	: QToolBar(tr("Image Eraser"), mw), m_ScMW(mw)
{
	setObjectName(QStringLiteral("ImageEraserOptions"));

	m_sizeLabel = new QLabel(this);
	addWidget(m_sizeLabel);

	m_sizeSlider = new QSlider(Qt::Horizontal, this);
	m_sizeSlider->setRange(1, 500);
	m_sizeSlider->setValue(CanvasMode_ImageEraser::brushSize());
	m_sizeSlider->setFixedWidth(140);
	addWidget(m_sizeSlider);

	m_sizeSpin = new QSpinBox(this);
	m_sizeSpin->setRange(1, 500);
	m_sizeSpin->setValue(CanvasMode_ImageEraser::brushSize());
	m_sizeSpin->setSuffix(QStringLiteral(" px"));
	addWidget(m_sizeSpin);

	addSeparator();

	m_hardnessLabel = new QLabel(this);
	addWidget(m_hardnessLabel);

	m_hardnessSlider = new QSlider(Qt::Horizontal, this);
	m_hardnessSlider->setRange(0, 100);
	m_hardnessSlider->setValue(CanvasMode_ImageEraser::brushHardness());
	m_hardnessSlider->setFixedWidth(100);
	addWidget(m_hardnessSlider);

	m_hardnessSpin = new QSpinBox(this);
	m_hardnessSpin->setRange(0, 100);
	m_hardnessSpin->setValue(CanvasMode_ImageEraser::brushHardness());
	m_hardnessSpin->setSuffix(QStringLiteral(" %"));
	addWidget(m_hardnessSpin);

	addSeparator();

	m_resetButton = new QToolButton(this);
	m_resetButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	addWidget(m_resetButton);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setContentsMargins(8, 0, 4, 0);
	addWidget(m_hintLabel);

	connect(m_sizeSlider, &QSlider::valueChanged, this, &ImageEraserOptions::sizeChanged);
	connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEraserOptions::sizeChanged);
	connect(m_hardnessSlider, &QSlider::valueChanged, this, &ImageEraserOptions::hardnessChanged);
	connect(m_hardnessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEraserOptions::hardnessChanged);
	connect(m_resetButton, &QToolButton::clicked, this, &ImageEraserOptions::resetErasure);

	languageChange();
}

void ImageEraserOptions::languageChange()
{
	setWindowTitle(tr("Image Eraser"));
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
	if (m_resetButton)
	{
		m_resetButton->setText(tr("Reset Erasure"));
		m_resetButton->setToolTip(tr("Restore every erased pixel on the selected image frame"));
	}
	if (m_hintLabel)
		m_hintLabel->setText(tr("Alt: restore"));
}

void ImageEraserOptions::refreshFromMode()
{
	if (m_updating)
		return;
	m_updating = true;
	if (m_sizeSlider)
		m_sizeSlider->setValue(CanvasMode_ImageEraser::brushSize());
	if (m_sizeSpin)
		m_sizeSpin->setValue(CanvasMode_ImageEraser::brushSize());
	if (m_hardnessSlider)
		m_hardnessSlider->setValue(CanvasMode_ImageEraser::brushHardness());
	if (m_hardnessSpin)
		m_hardnessSpin->setValue(CanvasMode_ImageEraser::brushHardness());
	m_updating = false;
}

void ImageEraserOptions::sizeChanged(int value)
{
	if (m_updating)
		return;
	m_updating = true;
	CanvasMode_ImageEraser::setBrushSize(value);
	int applied = CanvasMode_ImageEraser::brushSize();
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

void ImageEraserOptions::hardnessChanged(int value)
{
	if (m_updating)
		return;
	m_updating = true;
	CanvasMode_ImageEraser::setBrushHardness(value);
	int applied = CanvasMode_ImageEraser::brushHardness();
	if (m_hardnessSlider)
		m_hardnessSlider->setValue(applied);
	if (m_hardnessSpin)
		m_hardnessSpin->setValue(applied);
	m_updating = false;

	if (m_ScMW && m_ScMW->doc && m_ScMW->view && m_ScMW->view->m_canvas)
		m_ScMW->view->m_canvas->update();
}

void ImageEraserOptions::resetErasure()
{
	if (!m_ScMW || !m_ScMW->doc)
		return;
	ScribusDoc* doc = m_ScMW->doc;
	if (doc->m_Selection->isEmpty())
		return;

	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame())
		return;

	ScImageEffectList before = item->effectsInUse;
	if (ScEraserMask::indexIn(before) < 0)
		return;

	ScImageEffectList after = before;
	ScEraserMask::removeFrom(after);
	item->effectsInUse = after;

	PageItem_ImageFrame* frame = item->asImageFrame();
	if (frame)
	{
		frame->clearLiveEraserMask();
		frame->setEraserMask(QImage());
	}

	if (UndoManager::undoEnabled())
	{
		auto* state = new ScOldNewState<ScImageEffectList>(Um::RestoreImageArea, "", item->getUPixmap());
		state->set("APPLY_IMAGE_EFFECTS");
		state->setStates(before, after);
		UndoManager::instance()->action(item, state);
	}

	doc->changed();
	item->update();
	doc->regionsChanged()->update(QRectF());
}
