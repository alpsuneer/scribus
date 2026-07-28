/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef MOVEOPTIONSWIDGET_H
#define MOVEOPTIONSWIDGET_H

#include "scribusapi.h"
#include "ui/optionsbar.h"

class QCheckBox;
class QComboBox;
class QToolButton;

/*!
 \brief Options panel for the Move tool: Auto-Select, transform controls,
        and (stub) Align / Distribute buttons — a Photoshop-style row.
 */
class SCRIBUS_API MoveOptionsWidget : public ToolOptionsWidget
{
	Q_OBJECT

public:
	explicit MoveOptionsWidget(QWidget* parent = nullptr);
	QString toolName() const override { return QStringLiteral("Move"); }

private slots:
	void onAutoSelectToggled(bool on);

private:
	QToolButton* makeIconButton(const QString& iconName, int fallback, const QString& tip);

	QCheckBox* m_autoSelect { nullptr };
	QComboBox* m_autoSelectScope { nullptr };   //!< Layer | Group (enabled with Auto-Select)
	QCheckBox* m_showTransform { nullptr };
};

#endif // MOVEOPTIONSWIDGET_H
