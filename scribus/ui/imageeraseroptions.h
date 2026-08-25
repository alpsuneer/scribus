/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMAGEERASEROPTIONS_H
#define IMAGEERASEROPTIONS_H

#include <QToolBar>

class QLabel;
class QSlider;
class QSpinBox;
class QToolButton;
class ScribusMainWindow;

/*!
 \brief Brush controls for the image eraser canvas mode.

 A plain QToolBar rather than an ScToolBar on purpose: ScToolBar carries an
 IsVisible preference and restores itself from it, which would fight the
 show-with-the-mode behaviour wanted here. This bar is owned by the main
 window, hidden by default, and shown only while the eraser is active.
 */
class ImageEraserOptions : public QToolBar
{
	Q_OBJECT

public:
	explicit ImageEraserOptions(ScribusMainWindow* mw);

	//! Pull the widgets back into step with the mode's current brush settings,
	//! after the [ and ] keys have changed them on the canvas.
	void refreshFromMode();

	void languageChange();

private slots:
	void sizeChanged(int value);
	void hardnessChanged(int value);
	void resetErasure();

private:
	ScribusMainWindow* m_ScMW {nullptr};

	QLabel* m_sizeLabel {nullptr};
	QSlider* m_sizeSlider {nullptr};
	QSpinBox* m_sizeSpin {nullptr};
	QLabel* m_hardnessLabel {nullptr};
	QSlider* m_hardnessSlider {nullptr};
	QSpinBox* m_hardnessSpin {nullptr};
	QToolButton* m_resetButton {nullptr};
	QLabel* m_hintLabel {nullptr};

	//! Guards the two-way binding between each slider and its spin box.
	bool m_updating {false};
};

#endif
