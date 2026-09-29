/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSEPARATIONSVIEWER_H
#define PDFSEPARATIONSVIEWER_H

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QTemporaryDir>
#include <QVector>

#include "scribusapi.h"
#include "util_pdfsep.h"

class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QResizeEvent;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class ScribusMainWindow;

/**
 * @brief Standalone viewer to check the CMYK separations and ink coverage of
 * any external PDF, independent of the currently open Scribus document.
 * Reachable from File > PDF Tools > View PDF Separations...
 */
class SCRIBUS_API PDFSeparationsViewer : public QDialog
{
	Q_OBJECT

public:
	/**
	 * @param mainWin Owning main window, used only to enable "Place This PDF
	 * in Document..."; may be nullptr, in which case that button is hidden.
	 */
	explicit PDFSeparationsViewer(ScribusMainWindow* mainWin, QWidget* parent = nullptr);
	~PDFSeparationsViewer() override;

	/** @brief Open and analyse fileName immediately after construction. */
	void openFile(const QString& fileName);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private slots:
	void browseForFile();
	void reloadCurrentPage();
	void updateDisplay();
	void plateRowClicked(int row, int column);
	void plateCheckToggled(int row);
	void exportSeparations();
	void placeInDocument();
	void zoomIn();
	void zoomOut();
	void zoomFit();
	void zoomActual();
	void zoomComboActivated(const QString& text);
	void qualityComboChanged(int index);
	void cancelRender();

private:
	void setStatus(const QString& message, bool isError);
	void rebuildWarnings();
	QImage compositeFromCheckedPlates() const;
	QImage inkCoverageImage() const;
	bool isPlateChecked(int index) const;

	/** @brief DPI tier ("Fast" 72 or "Normal" 150 -- the only two the smart
	 * default ever picks; "High" 300 is manual-only) recommended for a page
	 * of this size. Small pages (up to A4) get full preview quality; A3 and
	 * anything larger renders at the fastest tier. */
	static int recommendedResolutionDPI(const QSizeF& pageSizePts);
	/** @brief True for anything bigger than A3 -- the threshold the slow-preview
	 * warning dialog uses, deliberately stricter than recommendedResolutionDPI()'s
	 * A4 cutoff so the interactive warning only fires for genuinely large jobs. */
	static bool isBroadsheetSize(const QSizeF& pageSizePts);
	/** @brief Select dpi in m_qualityCombo without emitting a change signal
	 * (and thus without triggering a redundant re-render). */
	void setQualityComboSilently(int dpi);

	void rebuildPreviewImage();
	void applyZoom();
	void setZoomFactor(double factor);
	double fitZoomFactor() const;

	ScribusMainWindow* m_mainWin { nullptr };
	QTemporaryDir m_tempDirObj;
	QString m_currentFile;

	PDFSepInfo   m_pageInfo;
	PDFSepResult m_sepResult;
	QVector<QImage> m_plateImages; // grayscale, same order as m_sepResult.plates
	int m_selectedPlate { 0 };
	double m_peakCoveragePercent { 0.0 }; // worst single pixel, not the page average
	double m_richBlackPercent { 0.0 }; // % of dark pixels whose darkness comes from CMY, not K

	int  m_currentResolutionDPI { 72 }; // matches m_qualityCombo's default "Fast" entry
	bool m_renderCancelRequested { false }; // polled by pdfSepGenerate() via System()

	QLineEdit*    m_fileEdit { nullptr };
	QPushButton*  m_browseButton { nullptr };
	QSpinBox*     m_pageSpin { nullptr };
	QLabel*       m_pageOfLabel { nullptr };

	QLabel*       m_previewLabel { nullptr };
	QScrollArea*  m_previewArea { nullptr };
	QImage        m_currentDisplayImage; // full-res, unscaled; zoom is applied on top of this

	bool          m_fitToWindow { true };
	double        m_zoomFactor { 1.0 }; // 1.0 == 100%, only meaningful when !m_fitToWindow
	bool          m_panning { false };
	QPoint        m_panLastPos;

	QPushButton*  m_zoomOutButton { nullptr };
	QComboBox*    m_zoomCombo { nullptr };
	QPushButton*  m_zoomInButton { nullptr };
	QPushButton*  m_zoomFitButton { nullptr };
	QPushButton*  m_zoomActualButton { nullptr };
	QLabel*       m_zoomStatusLabel { nullptr };

	QRadioButton* m_compositeRadio { nullptr };
	QRadioButton* m_plateRadio { nullptr };
	QRadioButton* m_inkCoverageRadio { nullptr };
	QComboBox*    m_qualityCombo { nullptr };

	QTableWidget*   m_plateTable { nullptr };
	QLabel*         m_totalLabel { nullptr };
	QDoubleSpinBox* m_thresholdSpin { nullptr };

	QLabel*       m_warningsLabel { nullptr };
	QLabel*       m_statusLabel { nullptr };
	QProgressBar* m_renderProgressBar { nullptr };
	QPushButton*  m_cancelRenderButton { nullptr };
	QPushButton*  m_exportButton { nullptr };

	QPushButton*  m_closeButton { nullptr };
	QPushButton*  m_placeButton { nullptr };
};

#endif
