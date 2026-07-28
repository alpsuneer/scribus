/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "optionsbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>

ToolOptionsWidget::ToolOptionsWidget(QWidget* parent)
	: QWidget(parent)
{
}

StubToolOptionsWidget::StubToolOptionsWidget(const QString& toolName, const QString& text, QWidget* parent)
	: ToolOptionsWidget(parent), m_toolName(toolName)
{
	auto* lay = new QHBoxLayout(this);
	lay->setContentsMargins(8, 0, 8, 0);
	auto* label = new QLabel(text, this);
	label->setEnabled(false);   // dimmed placeholder text
	lay->addWidget(label);
	lay->addStretch(1);
}

OptionsBar::OptionsBar(QWidget* parent)
	: QToolBar(parent)
{
	setObjectName(QStringLiteral("ScImageEditorOptionsBar"));
	setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	setMovable(false);
	setFloatable(false);
	setFixedHeight(34);   // Photoshop-style thin bar (~32px)

	// A single stacked widget holds every registered panel plus the empty label,
	// so switching tools is a cheap page change (no add/remove churn).
	m_stack = new QStackedWidget(this);
	m_emptyLabel = new QLabel(tr("No options for this tool"), this);
	m_emptyLabel->setEnabled(false);
	m_emptyLabel->setContentsMargins(8, 0, 8, 0);
	m_stack->addWidget(m_emptyLabel);
	m_stack->setCurrentWidget(m_emptyLabel);
	addWidget(m_stack);
}

void OptionsBar::registerToolOptions(const QString& toolName, ToolOptionsWidget* widget)
{
	if (!widget || toolName.isEmpty())
		return;
	m_toolWidgets.insert(toolName, widget);
	m_stack->addWidget(widget);
}

void OptionsBar::setActiveTool(const QString& toolName)
{
	ToolOptionsWidget* w = m_toolWidgets.value(toolName, nullptr);
	if (w)
	{
		m_stack->setCurrentWidget(w);
		m_currentWidget = w;
	}
	else
	{
		m_stack->setCurrentWidget(m_emptyLabel);
		m_currentWidget = nullptr;
	}
}
