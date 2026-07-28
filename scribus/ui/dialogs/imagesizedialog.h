/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMAGESIZEDIALOG_H
#define IMAGESIZEDIALOG_H

#include <QDialog>
#include <QImage>

#include "scribusapi.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;

/*!
 \brief Photoshop-style "Image Size" dialog.

 Lets the user change the pixel dimensions and/or resolution of an image, with
 an optional link (constrain proportions), a choice of unit per dimension, a
 resample toggle + method, and a live preview / file-size readout. On OK it
 produces the resampled image via resultImage().
 */
class SCRIBUS_API ImageSizeDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ImageSizeDialog(const QImage& source, QWidget* parent = nullptr);

	//! The resized image (equal to the source if nothing changed).
	QImage resultImage() const { return m_result; }
	//! true if OK produced a different pixel size than the source.
	bool imageResized() const { return m_resized; }

private slots:
	void onFitToChanged(int index);
	void onWidthChanged();
	void onHeightChanged();
	void onResolutionChanged();
	void onUnitChanged();
	void onResampleToggled(bool on);
	void onLinkToggled(bool on);
	void onResampleMethodChanged();
	void accept() override;

private:
	//! Rewrite every field from the current pixel/resolution state (blocks signals).
	void syncFields();
	void updateSummary();
	void updatePreview();
	//! The Qt transformation mode implied by the current resample method.
	Qt::TransformationMode transformationMode() const;

	QImage m_source;
	QImage m_result;
	bool m_resized { false };

	// Master state (kept independent of the display units).
	double m_pxW { 0.0 };
	double m_pxH { 0.0 };
	double m_origPxW { 0.0 };
	double m_origPxH { 0.0 };
	double m_res { 300.0 };   //!< resolution in pixels per inch
	bool m_link { true };
	bool m_guard { false };   //!< suppress recursive field updates

	// Widgets
	QComboBox* m_fitToCombo { nullptr };
	QLabel* m_dimensionsLabel { nullptr };
	QLabel* m_fileSizeLabel { nullptr };
	QDoubleSpinBox* m_widthSpin { nullptr };
	QComboBox* m_widthUnit { nullptr };
	QDoubleSpinBox* m_heightSpin { nullptr };
	QComboBox* m_heightUnit { nullptr };
	QToolButton* m_linkBtn { nullptr };
	QDoubleSpinBox* m_resSpin { nullptr };
	QComboBox* m_resUnit { nullptr };
	QCheckBox* m_resampleCheck { nullptr };
	QComboBox* m_resampleMethod { nullptr };
	QLabel* m_previewLabel { nullptr };
};

#endif // IMAGESIZEDIALOG_H
