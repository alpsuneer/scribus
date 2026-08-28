/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef REMOVALTOOLWIDGET_H
#define REMOVALTOOLWIDGET_H

#include <QToolBar>

class QLabel;
class QSlider;
class QSpinBox;
class QToolButton;
class ScribusMainWindow;

/*!
 \brief Brush and Apply controls for the object removal canvas mode.

 A plain QToolBar rather than an ScToolBar, for the same reason as
 ImageEraserOptions: ScToolBar carries an IsVisible preference and restores
 itself from it, which would fight the show-with-the-mode behaviour wanted
 here. Owned by the main window, hidden by default, shown only while the
 removal tool is active.
 */
class RemovalToolWidget : public QToolBar
{
	Q_OBJECT

public:
	explicit RemovalToolWidget(ScribusMainWindow* mw);

	/*! \brief Pull the widgets back into step with the mode.

	    Covers the brush settings after the [ and ] keys have changed them on
	    the canvas, and the enabled state of Apply, which depends on whether
	    anything is painted and on whether a run is already going. */
	void refreshFromMode();

	void languageChange();

private slots:
	void sizeChanged(int value);
	void hardnessChanged(int value);
	void applyClicked();
	void clearClicked();

private:
	ScribusMainWindow* m_ScMW {nullptr};

	QLabel* m_sizeLabel {nullptr};
	QSlider* m_sizeSlider {nullptr};
	QSpinBox* m_sizeSpin {nullptr};
	QLabel* m_hardnessLabel {nullptr};
	QSlider* m_hardnessSlider {nullptr};
	QSpinBox* m_hardnessSpin {nullptr};
	QToolButton* m_clearButton {nullptr};
	QToolButton* m_applyButton {nullptr};
	QLabel* m_hintLabel {nullptr};

	//! Guards the two-way binding between each slider and its spin box.
	bool m_updating {false};
};

#endif
