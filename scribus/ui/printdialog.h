/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PRINTDIALOG_H
#define PRINTDIALOG_H

#include "scribusapi.h"
#include "scribusstructs.h"
#include "offset_separation_presets.h"
#include "offset_tiling.h"

#include <QDialog>
#include <QStringList>
#include <QVector>
#include "ui_printdialogbase.h"

class PrefsContext;
class ScribusDoc;
class CupsOptions;
class TilePreviewWidget;

struct PrintOptions;

class SCRIBUS_API PrintDialog : public QDialog, Ui::PrintDialogBase
{
	Q_OBJECT

public:
	PrintDialog( QWidget* parent, ScribusDoc* doc, const PrintOptions& printOptions);
	~PrintDialog();

	QString printerName() const;
	QString outputFileName() const;
	bool outputToFile() const;
	int numCopies() const;
	bool isProofPrint() const;
	bool outputSeparations() const;
	QString separationName() const;
	QStringList allSeparations() const;
	bool color() const;
	bool mirrorHorizontal() const;
	bool mirrorVertical() const;
	bool doGCR() const;
	bool doClip() const;
	PrintLanguage printLanguage() const;
	bool doDev() const;
	bool doSpot() const;
	bool doPrintAll() const;
	bool doPrintCurrentPage() const;
	QString getPageString() const;

public slots:
	void setMinMax(int min, int max, int cur);

signals:
	void doPreview();

protected slots:
	void doDocBleeds();
	void createPageNumberRange();
	void selectOptions();
	void selectPrinter(const QString& prn);
	void selectPrintLanguage(const QString& prnLanguage);
	void selectRange(bool e);
	void selectSepMode(int e);
	void selectFile();
	void selectCommand();
	void okButtonClicked();
	void previewButtonClicked();

	// Offset Separations tab
	void offsetSepToggled(bool checked);
	void offsetSepFieldChanged();
	void offsetSepPresetChanged(int index);
	void offsetSepSavePreset();
	void offsetSepDeletePreset();
	void offsetSepResetDefaults();
	//! Any of offsetModeCmykRadio/GrayscaleRadio/FullColorRadio toggling.
	void offsetModeChanged();

	// Offset Separations tab: Tiling
	void offsetTileFieldChanged();
	void offsetTilePaperChanged(int index);

protected:
	ScribusDoc*    m_doc { nullptr };
	PrefsContext*  prefs { nullptr };
	CupsOptions*   m_cupsOptions { nullptr };
	int            m_unit { 0 };
	double         m_unitRatio { 1 };
	QStringList    m_spotColors;

	QByteArray m_devMode; // Buffer for storing storing printer options on Windows

	// Offset Separations tab: bundled + user-saved presets, and the name of
	// whichever preset is currently loaded (used to detect edits and to seed
	// the "Save Current as Preset" dialog).
	QVector<OffsetSepPreset> m_offsetBuiltInPresets;
	QVector<OffsetSepPreset> m_offsetCustomPresets;
	QString m_offsetActivePresetName;
	//! Guards preset-combo/field slots against firing while code, rather than
	//! the user, is setting widget values (loading a preset, rebuilding the
	//! combo) - without it, programmatic changes would flag the preset as
	//! user-modified.
	bool m_offsetUpdatingUI { false };

	QString getOptions();
	void storeValues();
	void setPrintLanguage(PrintLanguage engine);
	void setStoredValues(const QString& fileName);

	void offsetSepInit();
	void offsetSepPopulatePresetCombo();
	int offsetSepFindPresetIndex(const QString& name) const;
	OffsetSepPreset offsetSepCollectSettings() const;
	void offsetSepApplyPreset(const OffsetSepPreset& preset);
	void offsetSepLoadCustomPresets();
	void offsetSepPersistCustomPresets();
	//! Enables/disables offsetModeGroup, offsetSepOutputGroup,
	//! offsetSepPlatesGroup (CMYK mode only) and colorType based on the
	//! master switch and, for the plates table, the selected output mode.
	void offsetSepUpdateEnableState();
	//! Writes the current tab state into m_doc->Print_Options.offsetSep*/
	//! offsetOutputMode and the "print_options" PrefsContext, mirroring
	//! storeValues() for the rest of the dialog.
	void offsetSepStoreValues();
	//! Populates the tab from m_doc->Print_Options.offsetSep*/
	//! offsetOutputMode, mirroring setStoredValues() for the rest of the
	//! dialog.
	void offsetSepSetStoredValues();
	//! Which of the three offsetMode*Radio buttons is checked.
	OffsetOutputMode offsetSelectedOutputMode() const;

	// Offset Separations tab: Tiling
	void offsetTileInit();
	//! Fills offsetTilePaperCombo from the selected printer's supported
	//! paper sizes (PrinterUtil::getSupportedPaperSizes()), falling back to
	//! a fixed standard-size list when the printer can't be queried (output
	//! to file, or the queue does not answer) - plus a trailing "Custom"
	//! entry that enables offsetTileCustomWidth/Height.
	void offsetTilePopulatePaperCombo();
	//! Current tile paper size in points: either the selected PPD size via
	//! PrinterUtil::paperSizePoints(), or the Custom width/height fields.
	QSizeF offsetTileCurrentPaperSizePoints() const;
	//! The printer's minimum margin on the current tile paper size, or a
	//! 5mm fallback when it can't be queried - see
	//! PrinterUtil::getPrinterMarginValues().
	double offsetTileCurrentMarginPoints() const;
	//! Which of the three offsetTileOrient*Radio buttons is checked.
	OffsetTileOrientation offsetSelectedOrientation() const;
	//! Recomputes the tile grid for the document's current page size and
	//! refreshes offsetTileInfoLabel, offsetTileOrientRecommendedLabel and
	//! the tile preview widget. Safe to call whenever any tiling field, the
	//! output mode, the printer, or the separations master switch changes.
	void offsetTileUpdateInfo();
	void offsetTileStoreValues();
	void offsetTileSetStoredValues();

	//! Owned by offsetTilePreviewContainer's layout (see offsetTileInit());
	//! kept here only so offsetTileUpdateInfo() can push new grids into it.
	TilePreviewWidget* m_offsetTilePreview { nullptr };
};

#endif // PRINTDIALOG_H
