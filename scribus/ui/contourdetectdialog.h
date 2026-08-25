/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CONTOURDETECTDIALOG_H
#define CONTOURDETECTDIALOG_H

#include <QDialog>
#include <QImage>
#include <QPolygonF>

#include "util_contour.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class PageItem_ImageFrame;
class ContourPreviewWidget;

/*!
 \brief Options for tracing an image's visible region into its contour line.

 Carries its own preview rather than drawing on the canvas: the canvas only
 renders ContourLine once the item's text flow is actually set to use it, so a
 canvas preview would mean mutating the document to show something the user has
 not accepted yet.
 */
class ContourDetectDialog : public QDialog
{
	Q_OBJECT

public:
	ContourDetectDialog(QWidget* parent, PageItem_ImageFrame* item);

	int threshold() const;
	double tolerance() const;
	ScContour::Mode mode() const;
	//! Whether to switch the item's text flow to use the contour on apply.
	bool useForTextWrap() const;

private slots:
	void refreshPreview();

private:
	PageItem_ImageFrame* m_item {nullptr};
	//! The traced mask, fetched once: it does not change with the settings.
	QImage m_mask;

	QSlider* m_thresholdSlider {nullptr};
	QSpinBox* m_thresholdSpin {nullptr};
	QSlider* m_toleranceSlider {nullptr};
	QDoubleSpinBox* m_toleranceSpin {nullptr};
	QComboBox* m_modeCombo {nullptr};
	QCheckBox* m_wrapCheck {nullptr};
	QLabel* m_summary {nullptr};
	ContourPreviewWidget* m_preview {nullptr};
	QDialogButtonBox* m_buttons {nullptr};

	bool m_updating {false};
};

#endif
