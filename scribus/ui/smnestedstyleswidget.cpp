/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/smnestedstyleswidget.h"

#include "scribusdoc.h"

namespace
{
	enum Column
	{
		ColCharStyle = 0,
		ColMode      = 1,
		ColCount     = 2,
		ColDelimiter = 3,
		ColCount_    = 4
	};

	/// Sentinel used in the character style combo for "no character style".
	const int NoCharStyleIndex = 0;
}

SMNestedStylesWidget::SMNestedStylesWidget(QWidget* parent)
	: QWidget(parent)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	m_table = new QTableWidget(0, ColCount_, this);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->verticalHeader()->hide();
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	layout->addWidget(m_table);

	QHBoxLayout* buttons = new QHBoxLayout();
	m_addButton = new QPushButton(this);
	m_deleteButton = new QPushButton(this);
	m_upButton = new QToolButton(this);
	m_upButton->setArrowType(Qt::UpArrow);
	m_downButton = new QToolButton(this);
	m_downButton->setArrowType(Qt::DownArrow);
	m_parentButton = new QToolButton(this);
	m_parentButton->hide();

	buttons->addWidget(m_addButton);
	buttons->addWidget(m_deleteButton);
	buttons->addWidget(m_upButton);
	buttons->addWidget(m_downButton);
	buttons->addStretch(1);
	buttons->addWidget(m_parentButton);
	layout->addLayout(buttons);

	connect(m_addButton, SIGNAL(clicked()), this, SLOT(slotAdd()));
	connect(m_deleteButton, SIGNAL(clicked()), this, SLOT(slotDelete()));
	connect(m_upButton, SIGNAL(clicked()), this, SLOT(slotUp()));
	connect(m_downButton, SIGNAL(clicked()), this, SLOT(slotDown()));
	connect(m_parentButton, SIGNAL(clicked()), this, SLOT(slotParentClicked()));
	connect(m_table, SIGNAL(itemSelectionChanged()), this, SLOT(updateButtonStates()));

	languageChange();
	updateButtonStates();
}

void SMNestedStylesWidget::languageChange()
{
	m_table->setHorizontalHeaderLabels(QStringList()
		<< tr("Character Style")
		<< tr("Mode")
		<< tr("Count")
		<< tr("Delimiter"));

	m_addButton->setText(tr("&Add"));
	m_deleteButton->setText(tr("&Delete"));
	m_upButton->setToolTip(tr("Move the selected rule up"));
	m_downButton->setToolTip(tr("Move the selected rule down"));
	m_parentButton->setText(tr(" Parent Nested Styles "));
	m_parentButton->setToolTip(tr("Revert to the nested styles of the parent style"));
	m_addButton->setToolTip(tr("Rules apply in order: each one starts where the previous one ended"));
}

void SMNestedStylesWidget::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::LanguageChange)
		languageChange();
	QWidget::changeEvent(e);
}

void SMNestedStylesWidget::setDoc(ScribusDoc* doc)
{
	m_Doc = doc;
}

void SMNestedStylesWidget::clearAll()
{
	QSignalBlocker blocker(m_table);
	m_table->setRowCount(0);
	m_parentRules.clear();
	m_hasParent = false;
	m_useParentValue = false;
	m_parentButton->hide();
	updateButtonStates();
}

void SMNestedStylesWidget::fillCharStyleCombo(QComboBox* combo, const QString& current)
{
	combo->clear();
	combo->addItem(tr("No Style"));

	bool found = current.isEmpty();
	if (m_Doc)
	{
		const StyleSet<CharStyle>& cstyles = m_Doc->charStyles();
		for (int i = 0; i < cstyles.count(); ++i)
		{
			combo->addItem(cstyles[i].name());
			if (cstyles[i].name() == current)
				found = true;
		}
	}

	if (!found)
	{
		// The rule points at a character style that is not in this document.
		// Keep the name so it is not silently lost, and flag it: the layout
		// skips such a rule rather than restyling anything.
		combo->addItem(current);
		combo->setItemData(combo->count() - 1, QColor(Qt::red), Qt::ForegroundRole);
		combo->setItemData(combo->count() - 1, tr("This character style does not exist. The rule is ignored."), Qt::ToolTipRole);
	}

	const int index = current.isEmpty() ? NoCharStyleIndex : combo->findText(current);
	combo->setCurrentIndex(index < 0 ? NoCharStyleIndex : index);
}

void SMNestedStylesWidget::fillDelimiterCombo(QComboBox* combo, const ParagraphStyle::NestedStyleRule& rule)
{
	combo->clear();
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);

	// The data role carries the delimiter type; presets have no single sensible
	// literal spelling, so they are listed by name.
	combo->addItem(tr("End of Paragraph"), static_cast<int>(ParagraphStyle::NestedDelimEndOfPara));
	combo->addItem(tr("Space"), static_cast<int>(ParagraphStyle::NestedDelimSpace));
	combo->addItem(tr("Tab"), static_cast<int>(ParagraphStyle::NestedDelimTab));
	combo->addItem(tr("En Space"), static_cast<int>(ParagraphStyle::NestedDelimEnSpace));
	combo->addItem(tr("Em Space"), static_cast<int>(ParagraphStyle::NestedDelimEmSpace));

	if (rule.delimiterType == ParagraphStyle::NestedDelimChar)
	{
		combo->setEditText(QString::fromUcs4(&rule.delimiter, 1));
		combo->lineEdit()->setToolTip(tr("Type any single character, or pick one of the named delimiters"));
	}
	else
	{
		const int index = combo->findData(static_cast<int>(rule.delimiterType));
		combo->setCurrentIndex(index < 0 ? 0 : index);
	}
}

void SMNestedStylesWidget::insertRow(int row, const ParagraphStyle::NestedStyleRule& rule)
{
	m_table->insertRow(row);

	QComboBox* styleCombo = new QComboBox(m_table);
	fillCharStyleCombo(styleCombo, rule.charStyleName);
	m_table->setCellWidget(row, ColCharStyle, styleCombo);
	connect(styleCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCellChanged()));

	QComboBox* modeCombo = new QComboBox(m_table);
	modeCombo->addItem(tr("Up To"), static_cast<int>(ParagraphStyle::NestedUpTo));
	modeCombo->addItem(tr("Through"), static_cast<int>(ParagraphStyle::NestedThrough));
	modeCombo->setCurrentIndex(rule.mode == ParagraphStyle::NestedUpTo ? 0 : 1);
	modeCombo->setToolTip(tr("\"Through\" styles the delimiter too, \"Up To\" stops before it"));
	m_table->setCellWidget(row, ColMode, modeCombo);
	connect(modeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCellChanged()));

	QSpinBox* countSpin = new QSpinBox(m_table);
	countSpin->setRange(1, 9);
	countSpin->setValue(qBound(1, rule.count, 9));
	countSpin->setToolTip(tr("Which occurrence of the delimiter the rule reaches"));
	m_table->setCellWidget(row, ColCount, countSpin);
	connect(countSpin, SIGNAL(valueChanged(int)), this, SLOT(slotCellChanged()));

	QComboBox* delimCombo = new QComboBox(m_table);
	fillDelimiterCombo(delimCombo, rule);
	m_table->setCellWidget(row, ColDelimiter, delimCombo);
	connect(delimCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCellChanged()));
	connect(delimCombo->lineEdit(), SIGNAL(textEdited(QString)), this, SLOT(slotCellChanged()));
}

ParagraphStyle::NestedStyleRule SMNestedStylesWidget::ruleAt(int row) const
{
	ParagraphStyle::NestedStyleRule rule;

	const QComboBox* styleCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColCharStyle));
	if (styleCombo && styleCombo->currentIndex() != NoCharStyleIndex)
		rule.charStyleName = styleCombo->currentText();

	const QComboBox* modeCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColMode));
	if (modeCombo)
		rule.mode = static_cast<ParagraphStyle::NestedStyleMode>(modeCombo->currentData().toInt());

	const QSpinBox* countSpin = qobject_cast<QSpinBox*>(m_table->cellWidget(row, ColCount));
	if (countSpin)
		rule.count = countSpin->value();

	const QComboBox* delimCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColDelimiter));
	if (delimCombo)
	{
		const QString text = delimCombo->currentText();
		const int presetIndex = delimCombo->findText(text);
		if (presetIndex >= 0)
		{
			rule.delimiterType = static_cast<ParagraphStyle::NestedDelimiterType>(delimCombo->itemData(presetIndex).toInt());
		}
		else
		{
			// Anything the user typed is a literal delimiter. Take the first
			// character by codepoint so astral characters survive, and so
			// matching never depends on the script.
			const QList<uint> ucs4 = text.toUcs4();
			rule.delimiterType = ParagraphStyle::NestedDelimChar;
			rule.delimiter = ucs4.isEmpty() ? 0 : static_cast<char32_t>(ucs4.first());
		}
	}

	return rule;
}

QList<ParagraphStyle::NestedStyleRule> SMNestedStylesWidget::rules() const
{
	QList<ParagraphStyle::NestedStyleRule> result;
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		const ParagraphStyle::NestedStyleRule rule = ruleAt(row);
		// A rule with no character style styles nothing; a literal delimiter
		// rule with no character typed cannot be matched. Both are dropped
		// rather than written out in an unusable state.
		if (rule.charStyleName.isEmpty())
			continue;
		if (rule.delimiterType == ParagraphStyle::NestedDelimChar && rule.delimiter == 0)
			continue;
		result.append(rule);
	}
	return result;
}

void SMNestedStylesWidget::setRules(const QList<ParagraphStyle::NestedStyleRule>& rules)
{
	m_loading = true;
	m_table->setRowCount(0);
	for (const ParagraphStyle::NestedStyleRule& rule : rules)
		insertRow(m_table->rowCount(), rule);
	m_loading = false;
	updateButtonStates();
}

void SMNestedStylesWidget::showNestedStyles(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent)
{
	if (!pstyle)
	{
		clearAll();
		return;
	}

	m_hasParent = hasParent;
	m_parentRules = (hasParent && parent) ? parent->nestedStyleRules() : QList<ParagraphStyle::NestedStyleRule>();
	m_useParentValue = hasParent && pstyle->isInhNestedStyles();

	setRules(pstyle->nestedStyleRules());

	// Same affordance as the tab ruler: the button only appears once the style
	// has its own rules, and clicking it goes back to inheriting.
	if (m_hasParent && !m_useParentValue)
		m_parentButton->show();
	else
		m_parentButton->hide();
}

void SMNestedStylesWidget::updateButtonStates()
{
	const int row = m_table->currentRow();
	const int rowCount = m_table->rowCount();

	m_addButton->setEnabled(rowCount < ParagraphStyle::MaxNestedStyleRules);
	m_deleteButton->setEnabled(row >= 0);
	m_upButton->setEnabled(row > 0);
	m_downButton->setEnabled(row >= 0 && row < rowCount - 1);
}

void SMNestedStylesWidget::touched()
{
	if (m_loading)
		return;

	m_useParentValue = false;
	if (m_hasParent)
		m_parentButton->show();

	updateButtonStates();
	emit nestedStylesChanged();
}

void SMNestedStylesWidget::slotAdd()
{
	if (m_table->rowCount() >= ParagraphStyle::MaxNestedStyleRules)
		return;

	ParagraphStyle::NestedStyleRule rule;
	rule.mode = ParagraphStyle::NestedThrough;
	rule.count = 1;
	rule.delimiterType = ParagraphStyle::NestedDelimEndOfPara;

	m_loading = true;
	insertRow(m_table->rowCount(), rule);
	m_loading = false;

	m_table->setCurrentCell(m_table->rowCount() - 1, ColCharStyle);
	touched();
}

void SMNestedStylesWidget::slotDelete()
{
	const int row = m_table->currentRow();
	if (row < 0)
		return;

	m_loading = true;
	m_table->removeRow(row);
	m_loading = false;
	touched();
}

void SMNestedStylesWidget::slotUp()
{
	const int row = m_table->currentRow();
	if (row <= 0)
		return;

	QList<ParagraphStyle::NestedStyleRule> current;
	for (int i = 0; i < m_table->rowCount(); ++i)
		current.append(ruleAt(i));
	current.swapItemsAt(row, row - 1);

	setRules(current);
	m_table->setCurrentCell(row - 1, ColCharStyle);
	touched();
}

void SMNestedStylesWidget::slotDown()
{
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_table->rowCount() - 1)
		return;

	QList<ParagraphStyle::NestedStyleRule> current;
	for (int i = 0; i < m_table->rowCount(); ++i)
		current.append(ruleAt(i));
	current.swapItemsAt(row, row + 1);

	setRules(current);
	m_table->setCurrentCell(row + 1, ColCharStyle);
	touched();
}

void SMNestedStylesWidget::slotCellChanged()
{
	touched();
}

void SMNestedStylesWidget::slotParentClicked()
{
	setRules(m_parentRules);
	m_useParentValue = true;
	m_parentButton->hide();
	emit nestedStylesChanged();
}
