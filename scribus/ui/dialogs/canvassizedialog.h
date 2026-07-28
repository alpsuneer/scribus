/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CANVASSIZEDIALOG_H
#define CANVASSIZEDIALOG_H

#include <QColor>
#include <QDialog>
#include <QImage>

#include "scribusapi.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

/*!
 \brief Photoshop-style "Canvas Size" dialog.

 Changes the canvas dimensions around the image (the image pixels are not
 resampled): the picture is placed on a larger/smaller canvas at an anchor
 position, with the new area filled by a chosen extension color. On OK it
 produces the recomposited image via resultImage().
 */
class SCRIBUS_API CanvasSizeDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CanvasSizeDialog(const QImage& source, QWidget* parent = nullptr);

	//! The recomposited image (equal to the source if nothing changed).
	QImage resultImage() const { return m_result; }
	//! true if OK produced a different canvas size than the source.
	bool canvasChanged() const { return m_changed; }

private slots:
	void onRelativeToggled(bool on);
	void onUnitChanged();
	void onExtensionColorChanged(int index);
	void accept() override;

private:
	//! New canvas size in pixels, honouring the Relative checkbox and unit.
	QSize targetPixelSize() const;
	double toPixels(double value) const;   //!< display value → pixels (current unit)
	double fromPixels(double px) const;     //!< pixels → display value (current unit)
	void updateSummary();

	QImage m_source;
	QImage m_result;
	bool m_changed { false };
	double m_res { 300.0 };            //!< px/inch, for physical-unit conversion
	QColor m_extensionColor { Qt::white };
	bool m_transparent { false };

	QLabel* m_currentSizeLabel { nullptr };
	QDoubleSpinBox* m_widthSpin { nullptr };
	QDoubleSpinBox* m_heightSpin { nullptr };
	QComboBox* m_unit { nullptr };
	QCheckBox* m_relative { nullptr };
	QButtonGroup* m_anchorGroup { nullptr };
	QComboBox* m_colorCombo { nullptr };
};

#endif // CANVASSIZEDIALOG_H
