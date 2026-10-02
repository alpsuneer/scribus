/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDF_OPTS_H
#define PDF_OPTS_H

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

#include "colormgmt/sccolormgmtstructs.h"
#include "pdfoptions.h"
#include "scribusapi.h"
#include "scribusstructs.h"
#include "ui/tabpdfoptions.h"

class ScribusView;

/**
 * @file pdfopts.h
 * @brief PDF export GUI code
 */

class PDFOptions;
class ScribusDoc;

/**
 * @brief PDF export dialog
 *
 * Most of the guts of the dialog actually come from TabPDFOptions, which
 * is also used by the preferences dialog.
 */
class SCRIBUS_API PDFExportDialog : public QDialog
{
	Q_OBJECT

public:
	PDFExportDialog( QWidget* parent, const QString & docFileName,
					 const QMap<QString, int > & DocFonts,
					 ScribusView * currView, PDFOptions & pdfOptions,
					 const ScProfileInfoMap& PDFXProfiles, const SCFonts & AllFonts,
					 const ScProfileInfoMap& printerProfiles);
	~PDFExportDialog() {};

	void updateDocOptions();
	QString getPagesString();

	/*! True when the export ran from a preset, so the document's own PDF
	    settings must be put back afterwards. False when no preset was ever
	    applied, or "Use this document's own saved settings instead" is on:
	    then the dialog's values become the document's, as in stock Scribus. */
	bool keepsDocumentSettings() const;
	//! The document's PDF settings as they were when the dialog was created.
	const PDFOptions& documentOwnOptions() const { return m_docOwnOpts; }
	//! Name of the preset in force ("" for none).
	QString appliedPreset() const { return m_appliedPreset; }
	QString fileName() const;
	/*! Export without showing the dialog: apply \a presetName, all pages,
	    write to \a fileName. The caller then proceeds exactly as after exec(). */
	bool prepareDirectExport(const QString& presetName, const QString& fileName);
	//! Names of the two presets that are built into the program.
	static QStringList builtInPresets();

protected slots:
	void DoExport();
	void ChangeFile();
	void fileNameChanged();
	void enableSave();
	void disableSave();
	void handlePresetChange(int index);
	void presetToCustom();
	void presetSaveAs();
	void presetSave();
	void presetDelete();
	void presetSetDefault();
	void presetUseCurrentAsDefault();
	void presetExport();
	void presetImport();
	void presetOfficeToggled(bool on);
	void useDocumentSettingsToggled(bool on);
	void checkPresetModified();

protected:
	// Widgets
	QVBoxLayout* PDFExportLayout;
	QGridLayout* NameLayout;
	QHBoxLayout* Layout7;
	QGroupBox* Name;
	QCheckBox* multiFile;
	QCheckBox* openAfterExportCheckBox;
	QPushButton* changeButton;
	QPushButton* okButton;
	QPushButton* cancelButton;
	QLineEdit* fileNameLineEdit;
	QComboBox* presetCombo;
	QLabel* presetModifiedLabel { nullptr };
	QPushButton* presetSaveAsButton { nullptr };
	QPushButton* presetSaveButton { nullptr };
	QPushButton* presetDeleteButton { nullptr };
	QPushButton* presetDefaultButton { nullptr };
	QPushButton* presetCurrentAsDefaultButton { nullptr };
	QPushButton* presetExportButton { nullptr };
	QPushButton* presetImportButton { nullptr };
	QCheckBox* presetOfficeCheck { nullptr };
	QCheckBox* useDocumentSettingsCheck { nullptr };
	TabPDFOptions* Options;

	// Presets
	//! Widgets -> \a opts. \a forExport also writes the per-page presentation effects into the document.
	void collectOptions(PDFOptions& opts, bool forExport);
	//! The last steps of DoExport() that are not checks or questions.
	void finalizeForExport();
	bool applyPreset(const QString& name);
	void loadOptionsIntoWidgets();
	void reloadPresetCombo(const QString& select);
	void updatePresetButtons();
	QString selectedPreset() const;
	bool currentSubsetAllFonts() const;
	QByteArray currentFingerprint();
	bool saveCurrentAs(const QString& name, bool office);
	bool cmsAvailableForExport() const;

	// Other members
	ScribusDoc* m_doc;
	QList<PDFPresentationData> m_presEffects;
	PDFOptions & m_opts;
	double m_unitRatio;
	const ScProfileInfoMap& m_printerProfiles;
	const ScProfileInfoMap& m_pdfxProfiles;
	const SCFonts& m_allFonts;
	QMap<QString, int> m_docFonts;
	PDFOptions m_docOwnOpts;
	QString m_appliedPreset;
	QByteArray m_appliedFingerprint;
	bool m_presetUsed { false };
	/*! The preset in force needs colour management and the document has it
	    off. It is switched on for the document only when the export really
	    starts, never by just opening or cancelling the dialog. */
	bool m_enableCmsOnExport { false };
	class QTimer* m_modifiedTimer { nullptr };
};

#endif // PDF_OPTS_H
