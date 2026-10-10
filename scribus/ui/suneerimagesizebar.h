/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERIMAGESIZEBAR_H
#define SUNEERIMAGESIZEBAR_H

#include <QTimer>
#include <QWidget>

#include "scribusapi.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;
class PageItem;
class ScribusDoc;

/*!
 \brief Toolbar "Image Size" fields for the selected image frame, like
 Photoshop's Image Size dialog.

 W / H (cm, mm or in), a chain lock, DPI, a Resample switch, the resample
 quality (Sharper / Smoother / Bicubic / Nearest) with a sharpen amount, an
 "allow enlarge" toggle, Auto, and a live line with the result.

 Resample OFF: the pixels never change and no file is written. A new dpi
 gives a new size (pixels / dpi) and the frame and picture take it on the
 page; a new size gives a new dpi. Resample ON: a new size keeps the dpi and
 resamples the picture to the pixels that size needs; a new dpi keeps the
 size and resamples to the new pixel count. Always from the original file,
 never overwriting it, applied on Enter or focus-out, one undo step.
 */
class SCRIBUS_API SuneerImageSizeBar : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerImageSizeBar(QWidget* parent = nullptr);
	void refresh();

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;

private:
	PageItem* currentImageItem(ScribusDoc** docOut = nullptr) const;
	double unitPerInch() const;
	void applySize(bool fromWidth);
	void applyDpi();
	void runResample(PageItem* item, ScribusDoc* doc, double widthIn, double heightIn, int dpi, bool setFrame);
	void runNoResample(PageItem* item, ScribusDoc* doc, double widthIn, double heightIn, int dpi);
	void updatePreview();
	void status(const QString& text, bool warn = false);
	int qualityIndexFromSetting() const;

	QComboBox* m_unitCombo { nullptr };
	QDoubleSpinBox* m_widthSpin { nullptr };
	QDoubleSpinBox* m_heightSpin { nullptr };
	QToolButton* m_chainBtn { nullptr };
	QSpinBox* m_dpiSpin { nullptr };
	QCheckBox* m_resampleCheck { nullptr };
	QComboBox* m_qualityCombo { nullptr };
	QSpinBox* m_sharpenSpin { nullptr };
	QToolButton* m_enlargeBtn { nullptr };
	QCheckBox* m_autoCheck { nullptr };
	QLabel* m_infoLabel { nullptr };
	QLabel* m_previewLabel { nullptr };
	QTimer m_timer;
	bool m_busy { false };
	double m_shownW { 0 }, m_shownH { 0 };
	int m_shownDpi { 0 };
	int m_origW { 0 }, m_origH { 0 };
	qint64 m_origBytes { 0 };
	int m_curW { 0 }, m_curH { 0 };
};

#endif
