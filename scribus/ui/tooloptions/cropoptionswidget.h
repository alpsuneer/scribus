/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CROPOPTIONSWIDGET_H
#define CROPOPTIONSWIDGET_H

#include "scribusapi.h"
#include "ui/optionsbar.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

/*!
 \brief Options panel for the Crop tool: aspect-ratio presets, W/H fields with a
        unit combo, resolution, "Delete Cropped Pixels", and Commit/Cancel.

 The ratio presets keep the W and H fields proportional; Commit/Cancel are
 surfaced as signals for the editor to act on (crop the current marquee / clear).
 */
class SCRIBUS_API CropOptionsWidget : public ToolOptionsWidget
{
	Q_OBJECT

public:
	explicit CropOptionsWidget(QWidget* parent = nullptr);
	QString toolName() const override { return QStringLiteral("Crop"); }

	//! Set the reference image aspect (w/h) used by the "Original Ratio" preset.
	void setImageAspect(double aspect) { m_imageAspect = aspect; }

signals:
	void commitRequested();   //!< user clicked the green ✓ Commit button
	void cancelRequested();   //!< user clicked the red ✗ Cancel button
	void straightenRequested();

private slots:
	void onRatioChanged(int index);
	void onWidthEdited();
	void onHeightEdited();

private:
	//! The aspect ratio (w/h) implied by the current preset, or <= 0 if free.
	double currentRatio() const;

	QComboBox* m_ratio { nullptr };
	QLineEdit* m_width { nullptr };
	QLineEdit* m_height { nullptr };
	QComboBox* m_unit { nullptr };
	QLineEdit* m_resolution { nullptr };
	QComboBox* m_resUnit { nullptr };
	QCheckBox* m_deletePixels { nullptr };

	double m_imageAspect { 1.0 };   //!< current image w/h, for "Original Ratio"
	bool m_updating { false };      //!< guard against W<->H edit feedback loops
};

#endif // CROPOPTIONSWIDGET_H
