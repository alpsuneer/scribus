/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "commonstrings.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "ui/smcheckbox.h"
#include "ui/smcolorcombo.h"
#include "ui/smpshadewidget.h"
#include "ui/smsccombobox.h"
#include "ui/smscrspinbox.h"
#include "ui/smspinbox.h"
#include "units.h"

SMPShadeWidget::SMPShadeWidget(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	auto* grid = new QGridLayout();
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(3, 1);
	layout->addLayout(grid);
	layout->addStretch(1);

	int row = 0;

	on = new SMCheckBox(this);
	grid->addWidget(on, row, 0, 1, 4);
	++row;

	auto addLabel = [this, grid](int r, int c) -> QLabel* {
		auto* label = new QLabel(this);
		label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		grid->addWidget(label, r, c);
		m_labels.append(label);
		return label;
	};

	color = new SMColorCombo(this);
	addLabel(row, 0);
	grid->addWidget(color, row, 1);
	tint = new SMSpinBox(this);
	tint->setRange(0, 100);
	tint->setSuffix(" %");
	addLabel(row, 2);
	grid->addWidget(tint, row, 3);
	++row;

	widthType = new SMScComboBox(this);
	addLabel(row, 0);
	grid->addWidget(widthType, row, 1);
	cornerRadius = new SMScrSpinBox(0.0, 200.0, this, 0);
	addLabel(row, 2);
	grid->addWidget(cornerRadius, row, 3);
	++row;

	auto* line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	grid->addWidget(line, row, 0, 1, 4);
	++row;

	// Padding grows the band beyond the text lines; negative values shrink it.
	padTop = new SMScrSpinBox(-1000.0, 1000.0, this, 0);
	addLabel(row, 0);
	grid->addWidget(padTop, row, 1);
	padBottom = new SMScrSpinBox(-1000.0, 1000.0, this, 0);
	addLabel(row, 2);
	grid->addWidget(padBottom, row, 3);
	++row;

	padLeft = new SMScrSpinBox(-1000.0, 1000.0, this, 0);
	addLabel(row, 0);
	grid->addWidget(padLeft, row, 1);
	padRight = new SMScrSpinBox(-1000.0, 1000.0, this, 0);
	addLabel(row, 2);
	grid->addWidget(padRight, row, 3);
	++row;

	mergeAdjacent = new SMCheckBox(this);
	grid->addWidget(mergeAdjacent, row, 0, 1, 4);
	++row;

	grid->setRowStretch(row, 1);

	connect(on, SIGNAL(toggled(bool)), this, SIGNAL(shadeChanged()));
	connect(on, SIGNAL(toggled(bool)), this, SLOT(updateEnabledStates()));
	connect(color, SIGNAL(currentIndexChanged(int)), this, SIGNAL(shadeChanged()));
	connect(tint, SIGNAL(valueChanged(int)), this, SIGNAL(shadeChanged()));
	connect(widthType, SIGNAL(currentIndexChanged(int)), this, SIGNAL(shadeChanged()));
	connect(padTop, SIGNAL(valueChanged(double)), this, SIGNAL(shadeChanged()));
	connect(padBottom, SIGNAL(valueChanged(double)), this, SIGNAL(shadeChanged()));
	connect(padLeft, SIGNAL(valueChanged(double)), this, SIGNAL(shadeChanged()));
	connect(padRight, SIGNAL(valueChanged(double)), this, SIGNAL(shadeChanged()));
	connect(cornerRadius, SIGNAL(valueChanged(double)), this, SIGNAL(shadeChanged()));
	connect(mergeAdjacent, SIGNAL(toggled(bool)), this, SIGNAL(shadeChanged()));

	updateEnabledStates();
	languageChange();
}

void SMPShadeWidget::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::LanguageChange)
		languageChange();
	else
		QWidget::changeEvent(e);
}

void SMPShadeWidget::languageChange()
{
	on->setText(tr("Shading On"));
	mergeAdjacent->setText(tr("Merge Adjacent Paragraphs"));

	const QStringList labelTexts { tr("Color:"), tr("Tint:"), tr("Width:"), tr("Corner Radius:"),
	                               tr("Top Padding:"), tr("Bottom Padding:"),
	                               tr("Left Padding:"), tr("Right Padding:") };
	for (int i = 0; i < m_labels.count() && i < labelTexts.count(); ++i)
		m_labels[i]->setText(labelTexts[i]);

	int oldWidth = widthType->currentIndex();
	bool widthBlocked = widthType->blockSignals(true);
	widthType->clear();
	widthType->addItem(tr("Column"));
	widthType->addItem(tr("Text"));
	widthType->setCurrentIndex(qMax(0, oldWidth));
	widthType->blockSignals(widthBlocked);

	on->setToolTip(tr("Draw a background color band behind the paragraph"));
	widthType->setToolTip(tr("Span the whole column, or only the width of the text"));
	mergeAdjacent->setToolTip(tr("Render consecutive paragraphs with identical shading as one continuous band"));
}

void SMPShadeWidget::unitChange(int unitIndex)
{
	padTop->setNewUnit(unitIndex);
	padBottom->setNewUnit(unitIndex);
	padLeft->setNewUnit(unitIndex);
	padRight->setNewUnit(unitIndex);
	cornerRadius->setNewUnit(unitIndex);
}

void SMPShadeWidget::setDoc(ScribusDoc* doc)
{
	if (m_Doc)
		disconnect(m_Doc->scMW(), SIGNAL(UpdateRequest(int)), this, SLOT(handleUpdateRequest(int)));

	m_Doc = doc;

	if (m_Doc)
	{
		fillColorCombo();
		connect(m_Doc->scMW(), SIGNAL(UpdateRequest(int)), this, SLOT(handleUpdateRequest(int)));
	}
}

void SMPShadeWidget::handleUpdateRequest(int updateFlags)
{
	if (m_Doc && (updateFlags & reqColorsUpdate))
		fillColorCombo();
}

void SMPShadeWidget::fillColorCombo()
{
	if (!m_Doc)
		return;

	QString current = color->currentColor();
	bool blocked = color->blockSignals(true);
	color->setColors(m_Doc->PageColors, true);
	if (!current.isEmpty())
		color->setCurrentColor(current);
	color->blockSignals(blocked);
}

void SMPShadeWidget::updateEnabledStates()
{
	const bool enabled = on->isChecked();
	color->setEnabled(enabled);
	tint->setEnabled(enabled);
	widthType->setEnabled(enabled);
	padTop->setEnabled(enabled);
	padBottom->setEnabled(enabled);
	padLeft->setEnabled(enabled);
	padRight->setEnabled(enabled);
	cornerRadius->setEnabled(enabled);
	mergeAdjacent->setEnabled(enabled);
}

void SMPShadeWidget::showShade(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, int unitIndex)
{
	if (!pstyle)
		return;

	const double unitRatio = unitGetRatioFromIndex(unitIndex);

	if (hasParent && parent)
	{
		on->setChecked(pstyle->shadeOn(), pstyle->isInhShadeOn());
		on->setParentValue(parent->shadeOn());
		tint->setValue(pstyle->shadeTint(), pstyle->isInhShadeTint());
		tint->setParentValue(parent->shadeTint());
		widthType->setCurrentItem(static_cast<int>(pstyle->shadeWidthType()), pstyle->isInhShadeWidthType());
		widthType->setParentItem(static_cast<int>(parent->shadeWidthType()));
		padTop->setValue(pstyle->shadeTopPadding() * unitRatio, pstyle->isInhShadeTopPadding());
		padTop->setParentValue(parent->shadeTopPadding() * unitRatio);
		padBottom->setValue(pstyle->shadeBottomPadding() * unitRatio, pstyle->isInhShadeBottomPadding());
		padBottom->setParentValue(parent->shadeBottomPadding() * unitRatio);
		padLeft->setValue(pstyle->shadeLeftPadding() * unitRatio, pstyle->isInhShadeLeftPadding());
		padLeft->setParentValue(parent->shadeLeftPadding() * unitRatio);
		padRight->setValue(pstyle->shadeRightPadding() * unitRatio, pstyle->isInhShadeRightPadding());
		padRight->setParentValue(parent->shadeRightPadding() * unitRatio);
		cornerRadius->setValue(pstyle->shadeCornerRadius() * unitRatio, pstyle->isInhShadeCornerRadius());
		cornerRadius->setParentValue(parent->shadeCornerRadius() * unitRatio);
		mergeAdjacent->setChecked(pstyle->shadeMergeAdjacent(), pstyle->isInhShadeMergeAdjacent());
		mergeAdjacent->setParentValue(parent->shadeMergeAdjacent());
		color->setCurrentText(pstyle->shadeColor(), pstyle->isInhShadeColor());
		color->setParentText(parent->shadeColor());
	}
	else
	{
		on->setChecked(pstyle->shadeOn());
		tint->setValue(pstyle->shadeTint());
		widthType->setCurrentItem(static_cast<int>(pstyle->shadeWidthType()));
		padTop->setValue(pstyle->shadeTopPadding() * unitRatio);
		padBottom->setValue(pstyle->shadeBottomPadding() * unitRatio);
		padLeft->setValue(pstyle->shadeLeftPadding() * unitRatio);
		padRight->setValue(pstyle->shadeRightPadding() * unitRatio);
		cornerRadius->setValue(pstyle->shadeCornerRadius() * unitRatio);
		mergeAdjacent->setChecked(pstyle->shadeMergeAdjacent());
		color->setCurrentText(pstyle->shadeColor());
	}

	updateEnabledStates();
}

void SMPShadeWidget::clearAll()
{
	on->setChecked(false);
	tint->clear();
	padTop->clear();
	padBottom->clear();
	padLeft->clear();
	padRight->clear();
	cornerRadius->clear();
	mergeAdjacent->setChecked(true);
	updateEnabledStates();
}
