/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "moveoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>

#include "iconmanager.h"

namespace
{
	// A thin vertical separator, matching the toolbar look.
	QFrame* vSeparator(QWidget* parent)
	{
		auto* line = new QFrame(parent);
		line->setFrameShape(QFrame::VLine);
		line->setFrameShadow(QFrame::Sunken);
		return line;
	}
}

MoveOptionsWidget::MoveOptionsWidget(QWidget* parent)
	: ToolOptionsWidget(parent)
{
	auto* lay = new QHBoxLayout(this);
	lay->setContentsMargins(6, 2, 6, 2);
	lay->setSpacing(6);

	// Auto-Select + scope
	m_autoSelect = new QCheckBox(tr("Auto-Select"), this);
	lay->addWidget(m_autoSelect);
	m_autoSelectScope = new QComboBox(this);
	m_autoSelectScope->addItems({ tr("Layer"), tr("Group") });
	m_autoSelectScope->setEnabled(false);   // disabled until Auto-Select is on
	lay->addWidget(m_autoSelectScope);

	lay->addWidget(vSeparator(this));

	// Show Transform Controls
	m_showTransform = new QCheckBox(tr("Show Transform Controls"), this);
	lay->addWidget(m_showTransform);

	lay->addWidget(vSeparator(this));

	// Align (stub buttons)
	lay->addWidget(new QLabel(tr("Align:"), this));
	lay->addWidget(makeIconButton("align-horizontal-left",    QStyle::SP_ArrowLeft,  tr("Align Left")));
	lay->addWidget(makeIconButton("align-horizontal-center",  QStyle::SP_ArrowRight, tr("Align Center (Horizontal)")));
	lay->addWidget(makeIconButton("align-horizontal-right",   QStyle::SP_ArrowRight, tr("Align Right")));
	lay->addWidget(makeIconButton("align-vertical-top",       QStyle::SP_ArrowUp,    tr("Align Top")));
	lay->addWidget(makeIconButton("align-vertical-center",    QStyle::SP_ArrowDown,  tr("Align Center (Vertical)")));
	lay->addWidget(makeIconButton("align-vertical-bottom",    QStyle::SP_ArrowDown,  tr("Align Bottom")));

	lay->addWidget(vSeparator(this));

	// Distribute (stub buttons)
	lay->addWidget(new QLabel(tr("Distribute:"), this));
	lay->addWidget(makeIconButton("align-horizontal-center", QStyle::SP_ArrowRight, tr("Distribute Horizontally")));
	lay->addWidget(makeIconButton("align-vertical-center",   QStyle::SP_ArrowDown,  tr("Distribute Vertically")));

	lay->addStretch(1);

	connect(m_autoSelect, &QCheckBox::toggled, this, &MoveOptionsWidget::onAutoSelectToggled);
}

QToolButton* MoveOptionsWidget::makeIconButton(const QString& iconName, int fallback, const QString& tip)
{
	auto* btn = new QToolButton(this);
	QIcon ic = IconManager::instance().loadIcon(iconName, 16);
	if (ic.isNull())
		ic = style()->standardIcon(static_cast<QStyle::StandardPixmap>(fallback));
	btn->setIcon(ic);
	btn->setToolTip(tip);
	btn->setAutoRaise(true);
	// Stub for now — real align/distribute logic arrives in a later phase.
	return btn;
}

void MoveOptionsWidget::onAutoSelectToggled(bool on)
{
	m_autoSelectScope->setEnabled(on);
}
