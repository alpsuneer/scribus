/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "commonstrings.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "ui/smcheckbox.h"
#include "ui/smcolorcombo.h"
#include "ui/smprulewidget.h"
#include "ui/smsccombobox.h"
#include "ui/smscrspinbox.h"
#include "ui/smspinbox.h"
#include "units.h"

namespace
{
	/// The 14 attributes of one rule, read side-agnostically.
	struct RuleValues
	{
		bool on { false };
		double weight { 1.0 };
		QString color;
		bool overprint { false };
		QString gapColor;
		bool gapOverprint { false };
		int type { 0 };
		int tint { 100 };
		int gapTint { 100 };
		int widthType { 0 };
		double offset { 0.0 };
		double leftIndent { 0.0 };
		double rightIndent { 0.0 };
		bool keepInFrame { true };
	};

	RuleValues valuesOf(const ParagraphStyle* s, bool above)
	{
		RuleValues v;
		if (!s)
			return v;
		v.on           = above ? s->ruleAboveOn() : s->ruleBelowOn();
		v.weight       = above ? s->ruleAboveWeight() : s->ruleBelowWeight();
		v.color        = above ? s->ruleAboveColor() : s->ruleBelowColor();
		v.overprint    = above ? s->ruleAboveOverprint() : s->ruleBelowOverprint();
		v.gapColor     = above ? s->ruleAboveGapColor() : s->ruleBelowGapColor();
		v.gapOverprint = above ? s->ruleAboveGapOverprint() : s->ruleBelowGapOverprint();
		v.type         = above ? static_cast<int>(s->ruleAboveType()) : static_cast<int>(s->ruleBelowType());
		v.tint         = above ? s->ruleAboveTint() : s->ruleBelowTint();
		v.gapTint      = above ? s->ruleAboveGapTint() : s->ruleBelowGapTint();
		v.widthType    = above ? static_cast<int>(s->ruleAboveWidthType()) : static_cast<int>(s->ruleBelowWidthType());
		v.offset       = above ? s->ruleAboveOffset() : s->ruleBelowOffset();
		v.leftIndent   = above ? s->ruleAboveLeftIndent() : s->ruleBelowLeftIndent();
		v.rightIndent  = above ? s->ruleAboveRightIndent() : s->ruleBelowRightIndent();
		v.keepInFrame  = above ? s->ruleAboveKeepInFrame() : s->ruleBelowKeepInFrame();
		return v;
	}

	/// Which of the 14 attributes are inherited from the parent style.
	RuleValues inheritedOf(const ParagraphStyle* s, bool above)
	{
		RuleValues v;
		if (!s)
			return v;
		v.on           = above ? s->isInhRuleAboveOn() : s->isInhRuleBelowOn();
		v.weight       = above ? s->isInhRuleAboveWeight() : s->isInhRuleBelowWeight();
		v.overprint    = above ? s->isInhRuleAboveOverprint() : s->isInhRuleBelowOverprint();
		v.gapOverprint = above ? s->isInhRuleAboveGapOverprint() : s->isInhRuleBelowGapOverprint();
		v.type         = above ? s->isInhRuleAboveType() : s->isInhRuleBelowType();
		v.tint         = above ? s->isInhRuleAboveTint() : s->isInhRuleBelowTint();
		v.gapTint      = above ? s->isInhRuleAboveGapTint() : s->isInhRuleBelowGapTint();
		v.widthType    = above ? s->isInhRuleAboveWidthType() : s->isInhRuleBelowWidthType();
		v.offset       = above ? s->isInhRuleAboveOffset() : s->isInhRuleBelowOffset();
		v.leftIndent   = above ? s->isInhRuleAboveLeftIndent() : s->isInhRuleBelowLeftIndent();
		v.rightIndent  = above ? s->isInhRuleAboveRightIndent() : s->isInhRuleBelowRightIndent();
		v.keepInFrame  = above ? s->isInhRuleAboveKeepInFrame() : s->isInhRuleBelowKeepInFrame();
		// The two colour fields use the QString members as carriers of their flag.
		v.color        = (above ? s->isInhRuleAboveColor() : s->isInhRuleBelowColor()) ? "1" : "0";
		v.gapColor     = (above ? s->isInhRuleAboveGapColor() : s->isInhRuleBelowGapColor()) ? "1" : "0";
		return v;
	}
}

SMPRulesWidget::SMPRulesWidget(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);

	auto* topLayout = new QHBoxLayout();
	m_sideCombo = new QComboBox(this);
	m_sideCombo->addItem(tr("Rule Above"));
	m_sideCombo->addItem(tr("Rule Below"));
	topLayout->addWidget(m_sideCombo);
	topLayout->addStretch(1);
	layout->addLayout(topLayout);

	m_stack = new QStackedWidget(this);
	auto* abovePage = new QWidget(m_stack);
	auto* belowPage = new QWidget(m_stack);
	buildPage(above, abovePage);
	buildPage(below, belowPage);
	m_stack->addWidget(abovePage);
	m_stack->addWidget(belowPage);
	layout->addWidget(m_stack);
	layout->addStretch(1);

	connect(m_sideCombo, SIGNAL(currentIndexChanged(int)), m_stack, SLOT(setCurrentIndex(int)));

	connectPage(above);
	connectPage(below);

	updateEnabledStates();
	languageChange();
}

void SMPRulesWidget::buildPage(SMRuleControls& rule, QWidget* page)
{
	auto* grid = new QGridLayout(page);
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(3, 1);

	int row = 0;

	rule.on = new SMCheckBox(page);
	grid->addWidget(rule.on, row, 0, 1, 4);
	++row;

	auto addLabel = [&rule, grid](QWidget* parent, int r, int c) -> QLabel* {
		auto* label = new QLabel(parent);
		label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		grid->addWidget(label, r, c);
		rule.labels.append(label);
		return label;
	};

	// Weight is a line weight and therefore always in points, like every other
	// line weight in Scribus. Offsets and indents follow the document unit.
	rule.weight = new SMScrSpinBox(0.25, 20.0, page, 0);
	rule.weight->setSuffix(unitGetSuffixFromIndex(0));
	addLabel(page, row, 0);
	grid->addWidget(rule.weight, row, 1);

	rule.type = new SMScComboBox(page);
	addLabel(page, row, 2);
	grid->addWidget(rule.type, row, 3);
	++row;

	rule.color = new SMColorCombo(page);
	addLabel(page, row, 0);
	grid->addWidget(rule.color, row, 1);
	rule.tint = new SMSpinBox(page);
	rule.tint->setRange(0, 100);
	rule.tint->setSuffix(" %");
	addLabel(page, row, 2);
	grid->addWidget(rule.tint, row, 3);
	++row;

	rule.colorIsTextColor = new SMCheckBox(page);
	grid->addWidget(rule.colorIsTextColor, row, 1);
	rule.overprint = new SMCheckBox(page);
	grid->addWidget(rule.overprint, row, 3);
	++row;

	rule.gapColor = new SMColorCombo(page);
	addLabel(page, row, 0);
	grid->addWidget(rule.gapColor, row, 1);
	rule.gapTint = new SMSpinBox(page);
	rule.gapTint->setRange(0, 100);
	rule.gapTint->setSuffix(" %");
	addLabel(page, row, 2);
	grid->addWidget(rule.gapTint, row, 3);
	++row;

	rule.gapColorIsTextColor = new SMCheckBox(page);
	grid->addWidget(rule.gapColorIsTextColor, row, 1);
	rule.gapOverprint = new SMCheckBox(page);
	grid->addWidget(rule.gapOverprint, row, 3);
	++row;

	auto* line = new QFrame(page);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	grid->addWidget(line, row, 0, 1, 4);
	++row;

	rule.widthType = new SMScComboBox(page);
	addLabel(page, row, 0);
	grid->addWidget(rule.widthType, row, 1);

	rule.offset = new SMScrSpinBox(-1000.0, 1000.0, page, 0);
	addLabel(page, row, 2);
	grid->addWidget(rule.offset, row, 3);
	++row;

	rule.leftIndent = new SMScrSpinBox(-1000.0, 1000.0, page, 0);
	addLabel(page, row, 0);
	grid->addWidget(rule.leftIndent, row, 1);

	rule.rightIndent = new SMScrSpinBox(-1000.0, 1000.0, page, 0);
	addLabel(page, row, 2);
	grid->addWidget(rule.rightIndent, row, 3);
	++row;

	rule.keepInFrame = new SMCheckBox(page);
	grid->addWidget(rule.keepInFrame, row, 0, 1, 4);
	++row;

	grid->setRowStretch(row, 1);
}

void SMPRulesWidget::connectPage(const SMRuleControls& rule)
{
	connect(rule.on, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
	connect(rule.on, SIGNAL(toggled(bool)), this, SLOT(updateEnabledStates()));
	connect(rule.weight, SIGNAL(valueChanged(double)), this, SIGNAL(ruleChanged()));
	connect(rule.type, SIGNAL(currentIndexChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.type, SIGNAL(currentIndexChanged(int)), this, SLOT(updateEnabledStates()));
	connect(rule.color, SIGNAL(currentIndexChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.colorIsTextColor, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
	connect(rule.colorIsTextColor, SIGNAL(toggled(bool)), this, SLOT(updateEnabledStates()));
	connect(rule.tint, SIGNAL(valueChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.overprint, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
	connect(rule.gapColor, SIGNAL(currentIndexChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.gapColorIsTextColor, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
	connect(rule.gapColorIsTextColor, SIGNAL(toggled(bool)), this, SLOT(updateEnabledStates()));
	connect(rule.gapTint, SIGNAL(valueChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.gapOverprint, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
	connect(rule.widthType, SIGNAL(currentIndexChanged(int)), this, SIGNAL(ruleChanged()));
	connect(rule.offset, SIGNAL(valueChanged(double)), this, SIGNAL(ruleChanged()));
	connect(rule.leftIndent, SIGNAL(valueChanged(double)), this, SIGNAL(ruleChanged()));
	connect(rule.rightIndent, SIGNAL(valueChanged(double)), this, SIGNAL(ruleChanged()));
	connect(rule.keepInFrame, SIGNAL(toggled(bool)), this, SIGNAL(ruleChanged()));
}

void SMPRulesWidget::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::LanguageChange)
		languageChange();
	else
		QWidget::changeEvent(e);
}

void SMPRulesWidget::languageChange()
{
	int oldSide = m_sideCombo->currentIndex();
	bool sideBlocked = m_sideCombo->blockSignals(true);
	m_sideCombo->clear();
	m_sideCombo->addItem(tr("Rule Above"));
	m_sideCombo->addItem(tr("Rule Below"));
	m_sideCombo->setCurrentIndex(qMax(0, oldSide));
	m_sideCombo->blockSignals(sideBlocked);

	SMRuleControls* sides[2] = { &above, &below };
	for (SMRuleControls* rule : sides)
	{
		rule->on->setText(tr("Rule On"));
		rule->colorIsTextColor->setText(tr("Use Text Color"));
		rule->gapColorIsTextColor->setText(tr("Use Text Color"));
		rule->overprint->setText(tr("Overprint Stroke"));
		rule->gapOverprint->setText(tr("Overprint Gap"));
		rule->keepInFrame->setText(tr("Keep In Frame"));

		rule->weight->setSuffix(unitGetSuffixFromIndex(0));

		const QStringList labelTexts { tr("Weight:"), tr("Type:"), tr("Color:"), tr("Tint:"),
									   tr("Gap Color:"), tr("Gap Tint:"), tr("Width:"), tr("Offset:"),
									   tr("Left Indent:"), tr("Right Indent:") };
		for (int i = 0; i < rule->labels.count() && i < labelTexts.count(); ++i)
			rule->labels[i]->setText(labelTexts[i]);

		int oldType = rule->type->currentIndex();
		bool typeBlocked = rule->type->blockSignals(true);
		rule->type->clear();
		rule->type->addItem(tr("Solid"));
		rule->type->addItem(tr("Dashed"));
		rule->type->addItem(tr("Dotted"));
		rule->type->addItem(tr("Double"));
		rule->type->addItem(tr("Thick - Thin"));
		rule->type->addItem(tr("Thin - Thick"));
		rule->type->setCurrentIndex(qMax(0, oldType));
		rule->type->blockSignals(typeBlocked);

		int oldWidth = rule->widthType->currentIndex();
		bool widthBlocked = rule->widthType->blockSignals(true);
		rule->widthType->clear();
		rule->widthType->addItem(tr("Column"));
		rule->widthType->addItem(tr("Text"));
		rule->widthType->setCurrentIndex(qMax(0, oldWidth));
		rule->widthType->blockSignals(widthBlocked);

		rule->on->setToolTip(tr("Draw a horizontal rule with the paragraph"));
		rule->keepInFrame->setToolTip(tr("Hold the rule inside the text frame instead of letting it draw outside"));
		rule->widthType->setToolTip(tr("Span the whole column, or only the width of the text"));
	}
}

void SMPRulesWidget::unitChange(int unitIndex)
{
	SMRuleControls* sides[2] = { &above, &below };
	for (SMRuleControls* rule : sides)
	{
		rule->offset->setNewUnit(unitIndex);
		rule->leftIndent->setNewUnit(unitIndex);
		rule->rightIndent->setNewUnit(unitIndex);
	}
}

void SMPRulesWidget::setDoc(ScribusDoc* doc)
{
	// Connect through the main window, never through m_Doc->scMW(): the old
	// document may already be gone when the new one (or nullptr) arrives.
	ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
	if (mw)
		disconnect(mw, SIGNAL(UpdateRequest(int)), this, SLOT(handleUpdateRequest(int)));

	m_Doc = doc;

	if (m_Doc)
	{
		fillColorCombos();
		if (mw)
			connect(mw, SIGNAL(UpdateRequest(int)), this, SLOT(handleUpdateRequest(int)));
	}
}

void SMPRulesWidget::handleUpdateRequest(int updateFlags)
{
	if (m_Doc && (updateFlags & reqColorsUpdate))
		fillColorCombos();
}

void SMPRulesWidget::fillColorCombos()
{
	if (!m_Doc)
		return;

	SMRuleControls* sides[2] = { &above, &below };
	for (SMRuleControls* rule : sides)
	{
		SMColorCombo* combos[2] = { rule->color, rule->gapColor };
		for (SMColorCombo* combo : combos)
		{
			QString current = combo->currentColor();
			bool blocked = combo->blockSignals(true);
			combo->setColors(m_Doc->PageColors, true);
			if (!current.isEmpty())
				combo->setCurrentColor(current);
			combo->blockSignals(blocked);
		}
	}
}

void SMPRulesWidget::updateEnabledStates()
{
	SMRuleControls* sides[2] = { &above, &below };
	for (SMRuleControls* rule : sides)
	{
		const bool on = rule->on->isChecked();
		const int type = rule->type->currentIndex();
		// Only the multi-band rule types have a gap to colour.
		const bool hasGap = (type != ParagraphStyle::RuleSolid);

		rule->weight->setEnabled(on);
		rule->type->setEnabled(on);
		rule->color->setEnabled(on && !rule->colorIsTextColor->isChecked());
		rule->colorIsTextColor->setEnabled(on);
		rule->tint->setEnabled(on);
		rule->overprint->setEnabled(on);
		rule->gapColor->setEnabled(on && hasGap && !rule->gapColorIsTextColor->isChecked());
		rule->gapColorIsTextColor->setEnabled(on && hasGap);
		rule->gapTint->setEnabled(on && hasGap);
		rule->gapOverprint->setEnabled(on && hasGap);
		rule->widthType->setEnabled(on);
		rule->offset->setEnabled(on);
		rule->leftIndent->setEnabled(on);
		rule->rightIndent->setEnabled(on);
		rule->keepInFrame->setEnabled(on);
	}
}

void SMPRulesWidget::showRules(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, int unitIndex)
{
	if (!pstyle)
		return;

	const double unitRatio = unitGetRatioFromIndex(unitIndex);
	showSide(above, pstyle, parent, hasParent, unitRatio, true);
	showSide(below, pstyle, parent, hasParent, unitRatio, false);
	updateEnabledStates();
}

void SMPRulesWidget::showSide(SMRuleControls& rule, const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, double unitRatio, bool isAbove)
{
	const RuleValues v = valuesOf(pstyle, isAbove);
	const RuleValues inh = inheritedOf(pstyle, isAbove);
	const bool colorIsText = (v.color == ParagraphStyle::RuleTextColor);
	const bool gapColorIsText = (v.gapColor == ParagraphStyle::RuleTextColor);

	if (hasParent && parent)
	{
		const RuleValues p = valuesOf(parent, isAbove);

		rule.on->setChecked(v.on, inh.on);
		rule.on->setParentValue(p.on);
		rule.weight->setValue(v.weight, inh.weight);
		rule.weight->setParentValue(p.weight);
		rule.type->setCurrentItem(v.type, inh.type);
		rule.type->setParentItem(p.type);
		rule.tint->setValue(v.tint, inh.tint);
		rule.tint->setParentValue(p.tint);
		rule.gapTint->setValue(v.gapTint, inh.gapTint);
		rule.gapTint->setParentValue(p.gapTint);
		rule.overprint->setChecked(v.overprint, inh.overprint);
		rule.overprint->setParentValue(p.overprint);
		rule.gapOverprint->setChecked(v.gapOverprint, inh.gapOverprint);
		rule.gapOverprint->setParentValue(p.gapOverprint);
		rule.widthType->setCurrentItem(v.widthType, inh.widthType);
		rule.widthType->setParentItem(p.widthType);
		rule.offset->setValue(v.offset * unitRatio, inh.offset);
		rule.offset->setParentValue(p.offset * unitRatio);
		rule.leftIndent->setValue(v.leftIndent * unitRatio, inh.leftIndent);
		rule.leftIndent->setParentValue(p.leftIndent * unitRatio);
		rule.rightIndent->setValue(v.rightIndent * unitRatio, inh.rightIndent);
		rule.rightIndent->setParentValue(p.rightIndent * unitRatio);
		rule.keepInFrame->setChecked(v.keepInFrame, inh.keepInFrame);
		rule.keepInFrame->setParentValue(p.keepInFrame);
		// With the sentinel active the combo has no matching entry, so leave it
		// showing whatever palette colour it held as the fallback.
		if (!colorIsText)
			rule.color->setCurrentText(v.color, inh.color == "1");
		rule.color->setParentText(p.color);
		if (!gapColorIsText)
			rule.gapColor->setCurrentText(v.gapColor, inh.gapColor == "1");
		rule.gapColor->setParentText(p.gapColor);
	}
	else
	{
		rule.on->setChecked(v.on);
		rule.weight->setValue(v.weight);
		rule.type->setCurrentItem(v.type);
		rule.tint->setValue(v.tint);
		rule.gapTint->setValue(v.gapTint);
		rule.overprint->setChecked(v.overprint);
		rule.gapOverprint->setChecked(v.gapOverprint);
		rule.widthType->setCurrentItem(v.widthType);
		rule.offset->setValue(v.offset * unitRatio);
		rule.leftIndent->setValue(v.leftIndent * unitRatio);
		rule.rightIndent->setValue(v.rightIndent * unitRatio);
		rule.keepInFrame->setChecked(v.keepInFrame);
		if (!colorIsText)
			rule.color->setCurrentText(v.color);
		if (!gapColorIsText)
			rule.gapColor->setCurrentText(v.gapColor);
	}

	// The "(Text Color)" sentinel has no entry in the colour list, so it is
	// carried by a checkbox and the combo keeps showing the fallback colour.
	rule.colorIsTextColor->setChecked(colorIsText);
	rule.gapColorIsTextColor->setChecked(gapColorIsText);
}

void SMPRulesWidget::clearAll()
{
	SMRuleControls* sides[2] = { &above, &below };
	for (SMRuleControls* rule : sides)
	{
		rule->on->setChecked(false);
		rule->weight->clear();
		rule->tint->clear();
		rule->gapTint->clear();
		rule->offset->clear();
		rule->leftIndent->clear();
		rule->rightIndent->clear();
		rule->colorIsTextColor->setChecked(false);
		rule->gapColorIsTextColor->setChecked(false);
		rule->overprint->setChecked(false);
		rule->gapOverprint->setChecked(false);
		rule->keepInFrame->setChecked(true);
	}
	updateEnabledStates();
}
