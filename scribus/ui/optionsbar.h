/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OPTIONSBAR_H
#define OPTIONSBAR_H

#include <QMap>
#include <QToolBar>
#include <QWidget>

#include "scribusapi.h"

class QLabel;
class QStackedWidget;

/*!
 \brief Base class for a tool's contextual options panel (Photoshop-style).

 A ToolOptionsWidget is registered with an OptionsBar under a tool name and is
 shown whenever that tool becomes active. Subclasses lay out their controls in a
 horizontal row and report the tool they belong to via toolName().
 */
class SCRIBUS_API ToolOptionsWidget : public QWidget
{
	Q_OBJECT

public:
	explicit ToolOptionsWidget(QWidget* parent = nullptr);
	~ToolOptionsWidget() override = default;

	//! The name of the tool this widget provides options for.
	virtual QString toolName() const = 0;
};

/*!
 \brief A trivial placeholder options widget: a single dimmed label.

 Used for tools that do not yet expose real options (Select, Eyedropper, …).
 */
class SCRIBUS_API StubToolOptionsWidget : public ToolOptionsWidget
{
	Q_OBJECT

public:
	StubToolOptionsWidget(const QString& toolName, const QString& text, QWidget* parent = nullptr);
	QString toolName() const override { return m_toolName; }

private:
	QString m_toolName;
};

/*!
 \brief The Photoshop-style horizontal Options Bar shown below the menu bar.

 It hosts one ToolOptionsWidget per tool and swaps its visible content when the
 active tool changes. Tools with no registered widget show a dimmed empty label.
 */
class SCRIBUS_API OptionsBar : public QToolBar
{
	Q_OBJECT

public:
	explicit OptionsBar(QWidget* parent = nullptr);

	//! Register \a widget as the options panel for the tool named \a toolName.
	//! The bar takes ownership of the widget.
	void registerToolOptions(const QString& toolName, ToolOptionsWidget* widget);

public slots:
	//! Show the options panel for \a toolName (or the empty label if none).
	void setActiveTool(const QString& toolName);

private:
	QMap<QString, ToolOptionsWidget*> m_toolWidgets;
	QWidget* m_currentWidget { nullptr };   //!< the currently shown tool widget, or null
	QLabel* m_emptyLabel { nullptr };       //!< shown when a tool has no options
	QStackedWidget* m_stack { nullptr };    //!< holds every registered widget + the empty label
};

#endif // OPTIONSBAR_H
