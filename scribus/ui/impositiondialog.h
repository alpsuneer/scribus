/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMPOSITIONDIALOG_H
#define IMPOSITIONDIALOG_H

#include "scribusapi.h"
#include "scimpositionengine.h"

#include <QDialog>
#include <QString>

class ScribusDoc;
class QComboBox;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QTabWidget;
class ImpositionPreviewWidget;

/**
 * "Impose Pages" (Extras menu and the Control Bar's toolbar button): combine
 * a Left and a Right page -- each independently chosen from any .pdf file in
 * one folder, not necessarily related to the currently open document -- onto
 * one CTP plate-sized sheet, with a schematic (geometry-only) live preview.
 *
 * PDF input, not .sla: an earlier version rendered .sla content internally
 * (headless ScribusDoc + PageToPixmap), which had font-availability and
 * on-screen-preview bugs. PDFs carry embedded fonts and are imported via
 * Scribus's own PDF import plugin at Preview PDF / Send to CTP time --
 * see ScImpositionEngine::imposeToPdf(). The Setup tab's preview is
 * deliberately geometry-only (rectangles and labels, no rendered content),
 * so it has no font dependency of its own to get wrong.
 *
 * Two tabs: Setup (plate configuration -- Plate Properties, Elements,
 * Presets, Page Assignment) and Preview (the schematic plate layout,
 * full-size). Preview PDF / Send to CTP / Cancel stay outside the tabs,
 * always visible.
 *
 * "Send to CTP" generates the imposed PDF, converts it to 4 separated 1-bit
 * TIFF plates via Ghostscript's tiffsep1 device, renames them to the shop's
 * CTP convention, and copies them into the configured hot folder -- see
 * ScImpositionEngine::sendToCtp().
 *
 * Always side by side: ScImpositionEngine still supports a stacked
 * (Portrait) layout internally, but this dialog no longer exposes it -- the
 * only case this feature is built for is a newspaper spread, which is always
 * side by side.
 */
class SCRIBUS_API ImpositionDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ImpositionDialog(ScribusDoc* doc, QWidget* parent = nullptr);
	~ImpositionDialog() override;

protected:
	void showEvent(QShowEvent* event) override;

private slots:
	void browseFolderClicked();
	void browseHotFolderClicked();
	//! Folder path box changed (typed or via Browse...): rescans for .pdf
	//! files and repopulates both file combos.
	void folderEdited();
	void leftFileChanged();
	void rightFileChanged();
	void settingsChanged();
	void onLeftPageChanged();
	void onRightPageChanged();
	void onPlateSizeChanged();
	void onCenterOnPlateToggled();
	void savePresetClicked();
	void loadPresetClicked();
	void deletePresetClicked();
	void previewPdfClicked();
	void sendToCtpClicked();

private:
	ImpositionSettings collectSettings() const;
	bool generateImposedPdf(const QString& outputPath, QString* errorMessage);
	//! Scans the folder in m_folderEdit for *.pdf files and repopulates both
	//! file combos, preserving each side's current filename selection where
	//! it's still present in the new listing.
	void populateFileCombos();
	//! Repopulates pageCombo with "Page 1".."Page N" for whichever file is
	//! currently selected in fileCombo (via ScImpositionEngine::scanPdfInfo()),
	//! defaulting to Page 1. Only page 1 actually imposes (see
	//! ScImpositionEngine::imposeToPdf()'s doc comment) -- Preview PDF / Send
	//! to CTP report a clear error if a later page is selected, rather than
	//! silently imposing the wrong one.
	void populatePageCombo(QComboBox* fileCombo, QComboBox* pageCombo);
	//! Re-scans m_leftPageInfo/m_rightPageInfo to match the file combos'
	//! current selections (ScImpositionEngine::scanPdfInfo() -- a lightweight
	//! `pdfinfo` query, no ScribusDoc involved), then refreshes the preview.
	void updatePdfInfo();
	QString folderPath() const;
	QString selectedFileName(QComboBox* fileCombo) const;
	//! Repopulates m_presetCombo from the Presets/ subgroups in the
	//! Faircode/CTPImposition QSettings, selecting selectName if given.
	void populatePresetCombo(const QString& selectName = QString());
	//! Loads Presets/<name> and applies it to the Plate Properties widgets.
	void applyPreset(const QString& name);
	//! When m_centerOnPlateCheck is on, recomputes all four margins from the
	//! plate size, gutter, and each side's native PDF page size
	//! (m_leftPageInfo/m_rightPageInfo), and writes them into the (disabled)
	//! margin spins. No-op when the checkbox is off, or before both sides
	//! have valid page info. Shows/hides m_centerWarningLabel depending on
	//! whether the content overflows the plate. Called from settingsChanged(),
	//! so it re-runs on every plate size, gutter, file/page, and checkbox
	//! change.
	void updateCenteringMargins();

	//! The document Impose Pages was opened from. Used only to build plate
	//! marks (registration marks / color bar / slug line / auto marks) --
	//! never as a source of page content, since page content now comes from
	//! whichever PDF files are selected in the folder below.
	ScribusDoc* m_doc;
	double m_sheetWidthMm;
	double m_sheetHeightMm;

	QTabWidget* m_tabWidget { nullptr };

	// --- Plate Properties ---
	QLineEdit* m_plateNameEdit { nullptr };
	QDoubleSpinBox* m_sheetWidthSpin { nullptr };
	QDoubleSpinBox* m_sheetHeightSpin { nullptr };
	QSpinBox* m_resolutionXSpin { nullptr };
	QSpinBox* m_resolutionYSpin { nullptr };
	QComboBox* m_mediaTypeCombo { nullptr };
	QLineEdit* m_hotFolderEdit { nullptr };
	QPushButton* m_hotFolderBrowseButton { nullptr };

	// --- Plate presets, keyed by m_plateNameEdit's text ---
	QComboBox* m_presetCombo { nullptr };
	QPushButton* m_savePresetButton { nullptr };
	QPushButton* m_loadPresetButton { nullptr };
	QPushButton* m_deletePresetButton { nullptr };

	// --- Elements ---
	QCheckBox* m_printAreaCheck { nullptr };
	QCheckBox* m_regmarksCheck { nullptr };
	QCheckBox* m_autoMarksCheck { nullptr };
	QCheckBox* m_furnituresCheck { nullptr };
	QCheckBox* m_colourBarCheck { nullptr };
	QCheckBox* m_barcodesCheck { nullptr };
	QCheckBox* m_guidelinesCheck { nullptr };

	// --- Page assignment ---
	QLineEdit* m_folderEdit { nullptr };
	QPushButton* m_browseButton { nullptr };
	QComboBox* m_leftFileCombo { nullptr };
	QComboBox* m_leftPageCombo { nullptr };
	QComboBox* m_rightFileCombo { nullptr };
	QComboBox* m_rightPageCombo { nullptr };
	//! The newspaper's own page numbers (distinct from the page combos above,
	//! which only select within a single-page source PDF) -- see
	//! ImpositionSettings::leftPageNumber/rightPageNumber.
	QSpinBox* m_leftPageNumberSpin { nullptr };
	QSpinBox* m_rightPageNumberSpin { nullptr };
	QDoubleSpinBox* m_gutterSpin { nullptr };
	QCheckBox* m_centerOnPlateCheck { nullptr };
	QDoubleSpinBox* m_marginLeftSpin { nullptr };
	QDoubleSpinBox* m_marginRightSpin { nullptr };
	QDoubleSpinBox* m_marginTopSpin { nullptr };
	QDoubleSpinBox* m_marginBottomSpin { nullptr };
	QLabel* m_centerWarningLabel { nullptr };
	QLineEdit* m_pubEdit { nullptr };
	QLineEdit* m_editionEdit { nullptr };

	ImpositionPreviewWidget* m_preview { nullptr };
	QPushButton* m_previewPdfButton { nullptr };
	QPushButton* m_sendToCtpButton { nullptr };

	//! Native page size (mm) + page count for the current Left/Right file
	//! selections, from ScImpositionEngine::scanPdfInfo() -- a lightweight
	//! `pdfinfo` query, refreshed by updatePdfInfo(). Feeds both
	//! updateCenteringMargins()'s math and the schematic preview (via
	//! ImpositionSettings::leftPageWidthMm etc., set in collectSettings()).
	ScImpositionEngine::PdfPageInfo m_leftPageInfo;
	ScImpositionEngine::PdfPageInfo m_rightPageInfo;
};

#endif
