/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "scconfig.h"
#include "pdfexportdialog.h"

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpacerItem>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "colormgmt/sccolormgmtstructs.h"
#include "commonstrings.h"
#include "iconmanager.h"
#include "pdfoptions.h"
#include "pdfpresets.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "scpaths.h"
#include "scribusdoc.h"
#include "scribuscore.h"
#include "scribusview.h"
#include "ui/customfdialog.h"
#include "ui/scmessagebox.h"
#include "ui/scrspinbox.h"
#include "util.h"


PDFExportDialog::PDFExportDialog( QWidget* parent, const QString & docFileName,
								  const QMap<QString, int > & DocFonts,
								  ScribusView *currView, PDFOptions & pdfOptions,
								  const ScProfileInfoMap& PDFXProfiles, const SCFonts &AllFonts,
								  const ScProfileInfoMap& printerProfiles)
	: QDialog( parent ),
	m_doc(currView->m_doc),
	m_opts(pdfOptions),
	m_unitRatio(currView->m_doc->unitRatio()),
	m_printerProfiles(printerProfiles),
	m_pdfxProfiles(PDFXProfiles),
	m_allFonts(AllFonts),
	m_docFonts(DocFonts),
	m_docOwnOpts(pdfOptions)
{
	setModal(true);
	setWindowTitle( tr( "Save as PDF" ) );
	setWindowIcon(IconManager::instance().loadIcon("app-icon"));
	PDFExportLayout = new QVBoxLayout( this );
	PDFExportLayout->setSpacing(6);
	PDFExportLayout->setContentsMargins(9, 9, 9, 9);

	// Presets: named sets of every export setting except the file name and
	// the page range (see PdfPresets). The Default one is applied each time
	// the dialog opens, whatever the document itself has saved.
	QGroupBox* presetGroup = new QGroupBox( tr( "Preset" ), this );
	QVBoxLayout* presetGroupLayout = new QVBoxLayout( presetGroup );
	presetGroupLayout->setSpacing(6);
	presetGroupLayout->setContentsMargins(9, 9, 9, 9);
	QHBoxLayout* presetLayout = new QHBoxLayout;
	presetLayout->setSpacing(6);
	QLabel* presetLabel = new QLabel( tr( "Preset:" ), presetGroup );
	presetCombo = new QComboBox( presetGroup );
	presetCombo->setObjectName("pdfPresetCombo");
	presetCombo->setMinimumWidth(260);
	presetLabel->setBuddy(presetCombo);
	presetModifiedLabel = new QLabel( tr( "(modified)" ), presetGroup );
	presetModifiedLabel->setObjectName("pdfPresetModifiedLabel");
	// Keeps its place while hidden, so the buttons do not jump.
	QSizePolicy modifiedPolicy = presetModifiedLabel->sizePolicy();
	modifiedPolicy.setRetainSizeWhenHidden(true);
	presetModifiedLabel->setSizePolicy(modifiedPolicy);
	presetModifiedLabel->hide();
	presetSaveAsButton = new QPushButton( tr( "Save As..." ), presetGroup );
	presetSaveAsButton->setObjectName("pdfPresetSaveAs");
	presetSaveButton = new QPushButton( tr( "Save" ), presetGroup );
	presetSaveButton->setObjectName("pdfPresetSave");
	presetDeleteButton = new QPushButton( tr( "Delete" ), presetGroup );
	presetDeleteButton->setObjectName("pdfPresetDelete");
	presetDefaultButton = new QPushButton( tr( "Set as Default" ), presetGroup );
	presetDefaultButton->setObjectName("pdfPresetSetDefault");
	presetLayout->addWidget( presetLabel );
	presetLayout->addWidget( presetCombo, 1 );
	presetLayout->addWidget( presetModifiedLabel );
	presetLayout->addWidget( presetSaveAsButton );
	presetLayout->addWidget( presetSaveButton );
	presetLayout->addWidget( presetDeleteButton );
	presetLayout->addWidget( presetDefaultButton );
	presetGroupLayout->addLayout( presetLayout );
	QHBoxLayout* presetLayout2 = new QHBoxLayout;
	presetLayout2->setSpacing(6);
	presetCurrentAsDefaultButton = new QPushButton( tr( "Use current settings as Default" ), presetGroup );
	presetCurrentAsDefaultButton->setObjectName("pdfPresetCurrentAsDefault");
	presetExportButton = new QPushButton( tr( "Export..." ), presetGroup );
	presetExportButton->setObjectName("pdfPresetExport");
	presetImportButton = new QPushButton( tr( "Import..." ), presetGroup );
	presetImportButton->setObjectName("pdfPresetImport");
	presetOfficeCheck = new QCheckBox( tr( "Office preset" ), presetGroup );
	presetOfficeCheck->setObjectName("pdfPresetOffice");
	presetLayout2->addWidget( presetCurrentAsDefaultButton );
	presetLayout2->addWidget( presetExportButton );
	presetLayout2->addWidget( presetImportButton );
	presetLayout2->addWidget( presetOfficeCheck );
	presetLayout2->addStretch();
	presetGroupLayout->addLayout( presetLayout2 );
	useDocumentSettingsCheck = new QCheckBox( tr( "Use this document's own saved settings instead" ), presetGroup );
	useDocumentSettingsCheck->setObjectName("pdfUseDocumentSettings");
	useDocumentSettingsCheck->setChecked(false);
	presetGroupLayout->addWidget( useDocumentSettingsCheck );
	// None of these may become the dialog's default button: Enter exports.
	const QList<QPushButton*> presetButtons = presetGroup->findChildren<QPushButton*>();
	for (QPushButton* pb : presetButtons)
		pb->setAutoDefault(false);
	PDFExportLayout->addWidget( presetGroup );

	Name = new QGroupBox( this );
	Name->setTitle( tr( "O&utput to File:" ) );
	NameLayout = new QGridLayout( Name );
	NameLayout->setSpacing(6);
	NameLayout->setContentsMargins(9, 9, 9, 9);
	NameLayout->setAlignment( Qt::AlignTop );
	fileNameLineEdit = new QLineEdit( Name );
	fileNameLineEdit->setMinimumSize( QSize( 268, 22 ) );
	if (!m_opts.fileName.isEmpty())
		fileNameLineEdit->setText( QDir::toNativeSeparators(m_opts.fileName) );
	else
	{
		QFileInfo fi(docFileName);
		QString completeBaseName = fi.completeBaseName();
		if (completeBaseName.endsWith(".sla", Qt::CaseInsensitive))
			if (completeBaseName.length() > 4) completeBaseName.chop(4);
		if (completeBaseName.endsWith(".gz", Qt::CaseInsensitive))
			if (completeBaseName.length() > 3) completeBaseName.chop(3);
		if (fi.exists())
		{
			QString fileName(fi.path() + "/" + completeBaseName + ".pdf");
			fileNameLineEdit->setText( QDir::toNativeSeparators(fileName) );
		}
		else
		{
			PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
			QString pdfdir = dirs->get("pdf", fi.path());
			if (pdfdir.right(1) != "/")
				pdfdir += "/";
			QString fileName(pdfdir + completeBaseName + ".pdf");
			fileNameLineEdit->setText( QDir::toNativeSeparators(fileName) );
		}
	}
	NameLayout->addWidget( fileNameLineEdit, 0, 0 );
	changeButton = new QPushButton( Name );
	changeButton->setText( tr( "Cha&nge..." ) );
	changeButton->setMinimumSize( QSize( 88, 24 ) );
	NameLayout->addWidget( changeButton, 0, 1 );
	multiFile = new QCheckBox( tr( "Output one file for eac&h page" ), Name );
	multiFile->setChecked(m_opts.doMultiFile);
	NameLayout->addWidget( multiFile, 1, 0 );
	openAfterExportCheckBox = new QCheckBox( tr( "Open PDF after Export" ), Name );
	openAfterExportCheckBox->setChecked(m_opts.openAfterExport);
	NameLayout->addWidget( openAfterExportCheckBox, 2, 0 );
	PDFExportLayout->addWidget( Name );

	Options = new TabPDFOptions( this, pdfOptions, AllFonts, PDFXProfiles, DocFonts, currView->m_doc );
	PDFExportLayout->addWidget( Options );
	Layout7 = new QHBoxLayout;
	Layout7->setSpacing(6);
	Layout7->setContentsMargins(0, 0, 0, 0);
	QSpacerItem* spacer_2 = new QSpacerItem( 2, 2, QSizePolicy::Expanding, QSizePolicy::Minimum );
	Layout7->addItem( spacer_2 );
	okButton = new QPushButton( tr( "&Save" ), this );
	okButton->setAutoDefault( true );
	okButton->setDefault( true );
	Layout7->addWidget( okButton );
	cancelButton = new QPushButton( CommonStrings::tr_Cancel, this );
	Layout7->addWidget( cancelButton );
	PDFExportLayout->addLayout( Layout7 );
	if ((m_opts.Version == PDFVersion::PDF_X3) && (Options->InfoString->text().isEmpty()))
		okButton->setEnabled(false);
	resize(sizeHint());
//	setMaximumSize( sizeHint() );
//tooltips
	multiFile->setToolTip( "<qt>" + tr( "This enables exporting one individually named PDF file for each page in the document. Page numbers are added automatically. This is most useful for imposing PDF for commercial printing.") + "</qt>" );
	openAfterExportCheckBox->setToolTip( "<qt>" + tr( "Open the exported PDF with the PDF viewer as set in External Tools preferences, when not exporting to a multi-file export destination") + "</qt>" );
	okButton->setToolTip( "<qt>" + tr( "The save button will be disabled if you are trying to export PDF/X and the info string is missing from the PDF/X tab") + "</qt>" );
	// signals and slots connections
	connect( changeButton, SIGNAL( clicked() ), this, SLOT( ChangeFile() ) );
	connect( okButton, SIGNAL( clicked() ), this, SLOT( DoExport() ) );
	connect( cancelButton, SIGNAL( clicked() ), this, SLOT( reject() ) );
	connect( fileNameLineEdit, SIGNAL( editingFinished() ), this, SLOT( fileNameChanged() ) );
	connect( Options, SIGNAL(noInfo()), this, SLOT(disableSave()));
	connect( Options, SIGNAL(hasInfo()), this, SLOT(enableSave()));
	connect( presetCombo, SIGNAL(activated(int)), this, SLOT(handlePresetChange(int)));
	connect( presetSaveAsButton, SIGNAL(clicked()), this, SLOT(presetSaveAs()));
	connect( presetSaveButton, SIGNAL(clicked()), this, SLOT(presetSave()));
	connect( presetDeleteButton, SIGNAL(clicked()), this, SLOT(presetDelete()));
	connect( presetDefaultButton, SIGNAL(clicked()), this, SLOT(presetSetDefault()));
	connect( presetCurrentAsDefaultButton, SIGNAL(clicked()), this, SLOT(presetUseCurrentAsDefault()));
	connect( presetExportButton, SIGNAL(clicked()), this, SLOT(presetExport()));
	connect( presetImportButton, SIGNAL(clicked()), this, SLOT(presetImport()));
	connect( presetOfficeCheck, SIGNAL(clicked(bool)), this, SLOT(presetOfficeToggled(bool)));
	connect( useDocumentSettingsCheck, SIGNAL(toggled(bool)), this, SLOT(useDocumentSettingsToggled(bool)));

	presetSaveAsButton->setToolTip( "<qt>" + tr( "Save the settings of all tabs as a new named preset. The file name and the page range are not part of a preset.") + "</qt>" );
	presetSaveButton->setToolTip( "<qt>" + tr( "Update the selected preset with the settings shown now") + "</qt>" );
	presetDeleteButton->setToolTip( "<qt>" + tr( "Delete the selected preset") + "</qt>" );
	presetDefaultButton->setToolTip( "<qt>" + tr( "Start with the selected preset every time this dialog opens, for any document") + "</qt>" );
	presetCurrentAsDefaultButton->setToolTip( "<qt>" + tr( "Save the settings shown now and start with them every time this dialog opens. They are stored in the selected preset if it is one of your own, otherwise in a preset named \"My Default\".") + "</qt>" );
	presetExportButton->setToolTip( "<qt>" + tr( "Copy all your presets into a folder, to carry them to another PC") + "</qt>" );
	presetImportButton->setToolTip( "<qt>" + tr( "Add presets from files exported on another PC") + "</qt>" );
	presetOfficeCheck->setToolTip( "<qt>" + tr( "Mark this preset to be shipped to the office PCs with the next release package") + "</qt>" );
	useDocumentSettingsCheck->setToolTip( "<qt>" + tr( "Show and use the PDF settings saved inside this document instead of a preset. Exporting this way also updates the settings saved in the document.") + "</qt>" );

	// The Default preset wins over whatever the document carries.
	const QString defaultPreset = PdfPresets::defaultName();
	reloadPresetCombo(QString());
	if (!defaultPreset.isEmpty() && presetCombo->findData(defaultPreset) >= 0)
	{
		if (applyPreset(defaultPreset))
			reloadPresetCombo(defaultPreset);
	}
	updatePresetButtons();

	// Any widget on any tab can change a setting; rather than wire each one,
	// compare the whole state against what the preset left behind.
	m_modifiedTimer = new QTimer(this);
	m_modifiedTimer->setInterval(400);
	connect( m_modifiedTimer, SIGNAL(timeout()), this, SLOT(checkPresetModified()));
	m_modifiedTimer->start();
}

QStringList PDFExportDialog::builtInPresets()
{
	return QStringList() << "News_Paper" << "Deshabhimani_Newspaper";
}

QString PDFExportDialog::fileName() const
{
	return QDir::fromNativeSeparators(fileNameLineEdit->text());
}

bool PDFExportDialog::keepsDocumentSettings() const
{
	return m_presetUsed && !useDocumentSettingsCheck->isChecked();
}

QString PDFExportDialog::selectedPreset() const
{
	return presetCombo->currentData().toString();
}

bool PDFExportDialog::cmsAvailableForExport() const
{
	return m_doc->HasCMS || (m_enableCmsOnExport && ScCore->haveCMS());
}

bool PDFExportDialog::currentSubsetAllFonts() const
{
	return Options->fontEmbeddingMode() == PDFOptions::EmbedFonts
		&& Options->fontsToEmbed().isEmpty() && !Options->fontsToSubset().isEmpty();
}

QByteArray PDFExportDialog::currentFingerprint()
{
	PDFOptions current(m_opts);
	collectOptions(current, false);
	return PdfPresets::fingerprint(current, currentSubsetAllFonts());
}

void PDFExportDialog::reloadPresetCombo(const QString& select)
{
	const QString defaultPreset = PdfPresets::defaultName();
	const QStringList own = PdfPresets::userNames();
	const QStringList all = PdfPresets::allNames();
	QSignalBlocker blocker(presetCombo);
	presetCombo->clear();
	presetCombo->addItem( tr( "Custom" ), QString() );
	const auto label = [&defaultPreset](const QString& name, const QString& kind)
	{
		QString text = name;
		if (!kind.isEmpty())
			text += " (" + kind + ")";
		if (name == defaultPreset)
			text += "  [" + tr("Default") + "]";
		return text;
	};
	for (const QString& name : all)
		presetCombo->addItem(label(name, own.contains(name) ? QString() : tr("office")), name);
	const QStringList builtIn = builtInPresets();
	for (const QString& name : builtIn)
	{
		if (!all.contains(name))
			presetCombo->addItem(label(name, tr("built-in")), name);
	}
	const int idx = select.isEmpty() ? 0 : presetCombo->findData(select);
	presetCombo->setCurrentIndex(qMax(0, idx));
}

void PDFExportDialog::updatePresetButtons()
{
	const QString name = selectedPreset();
	const bool own = PdfPresets::isUserPreset(name);
	presetSaveButton->setEnabled(own);
	presetDeleteButton->setEnabled(own);
	presetDefaultButton->setEnabled(!name.isEmpty() && name != PdfPresets::defaultName());
	presetOfficeCheck->setEnabled(own);
	bool office = false;
	if (own)
	{
		PdfPresets::Preset preset;
		office = PdfPresets::load(name, preset) && preset.office;
	}
	presetOfficeCheck->setChecked(office);
}

void PDFExportDialog::loadOptionsIntoWidgets()
{
	// TabPDFOptions reads its own reference to the options, which is m_opts.
	Options->restoreDefaults(m_opts, m_allFonts, m_pdfxProfiles, m_docFonts);
	multiFile->setChecked(m_opts.doMultiFile);
	openAfterExportCheckBox->setChecked(m_opts.openAfterExport);
	if ((m_opts.Version == PDFVersion::PDF_X3) && (Options->InfoString->text().isEmpty()))
		okButton->setEnabled(false);
	else
		okButton->setEnabled(true);
}

bool PDFExportDialog::applyPreset(const QString& name)
{
	if (name.isEmpty())
		return false;
	const QString typedFileName = fileNameLineEdit->text();
	if (builtInPresets().contains(name) && !PdfPresets::exists(name))
	{
		// Both built-in presets need document colour management; newspaper
		// documents commonly have it off. The widgets are switched over now,
		// the document itself only when the export starts.
		if (!m_doc->HasCMS && ScCore->haveCMS())
		{
			m_enableCmsOnExport = true;
			Options->enableCMS(true);
		}
		if (name == builtInPresets().at(0))
			Options->applyNewspaperPreset();
		else
			Options->applyDeshabhimaniPreset();
	}
	else
	{
		PdfPresets::Preset preset;
		if (!PdfPresets::load(name, preset))
			return false;
		const bool usesProfiles = !preset.opts.UseRGB && !preset.opts.isGrayscale && (preset.opts.UseProfiles || preset.opts.UseProfiles2);
		const bool isPDFX = (preset.opts.Version == PDFVersion::PDF_X1a) || (preset.opts.Version == PDFVersion::PDF_X3) || (preset.opts.Version == PDFVersion::PDF_X4);
		m_enableCmsOnExport = (usesProfiles || isPDFX) && !m_doc->HasCMS && ScCore->haveCMS();
		Options->enableCMS(m_doc->HasCMS || m_enableCmsOnExport);

		PdfPresets::applyTo(preset.opts, m_opts);
		// Font lists are per document: rebuilt from this document's fonts.
		const QStringList docFonts = m_docFonts.keys();
		m_opts.EmbedList.clear();
		m_opts.SubsetList.clear();
		m_opts.OutlineList.clear();
		if (m_opts.FontEmbedding == PDFOptions::EmbedFonts)
		{
			if (preset.subsetAllFonts)
				m_opts.SubsetList = docFonts;
			else
				m_opts.EmbedList = docFonts;   // those that cannot be embedded whole are subset by the tab
		}
		else if (m_opts.FontEmbedding == PDFOptions::OutlineFonts)
			m_opts.OutlineList = docFonts;
		loadOptionsIntoWidgets();
	}
	fileNameLineEdit->setText(typedFileName);
	m_presetUsed = true;
	m_appliedPreset = name;
	m_appliedFingerprint = currentFingerprint();
	presetModifiedLabel->hide();
	if (useDocumentSettingsCheck->isChecked())
	{
		QSignalBlocker blocker(useDocumentSettingsCheck);
		useDocumentSettingsCheck->setChecked(false);
	}
	return true;
}

void PDFExportDialog::checkPresetModified()
{
	if (m_appliedPreset.isEmpty())
	{
		presetModifiedLabel->hide();
		return;
	}
	presetModifiedLabel->setVisible(currentFingerprint() != m_appliedFingerprint);
}

bool PDFExportDialog::saveCurrentAs(const QString& name, bool office)
{
	PdfPresets::Preset preset;
	preset.name = name;
	preset.office = office;
	preset.opts = m_opts;
	collectOptions(preset.opts, false);
	preset.subsetAllFonts = currentSubsetAllFonts();
	QString error;
	if (!PdfPresets::save(preset, &error))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The preset could not be saved:\n%1").arg(error));
		return false;
	}
	m_presetUsed = true;
	m_appliedPreset = name;
	m_appliedFingerprint = currentFingerprint();
	presetModifiedLabel->hide();
	return true;
}

void PDFExportDialog::presetSaveAs()
{
	QString suggestion = selectedPreset();
	for (;;)
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Save Preset As"), tr("Preset name:"), QLineEdit::Normal, suggestion, &ok).trimmed();
		if (!ok || name.isEmpty())
			return;
		suggestion = name;
		if (builtInPresets().contains(name))
		{
			ScMessageBox::warning(this, CommonStrings::trWarning, tr("\"%1\" is the name of a built-in preset. Choose another name.").arg(name));
			continue;
		}
		bool office = false;
		if (PdfPresets::isUserPreset(name))
		{
			if (ScMessageBox::question(this, tr("Save Preset As"), tr("A preset named \"%1\" already exists. Replace it?").arg(name),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
				continue;
			PdfPresets::Preset old;
			office = PdfPresets::load(name, old) && old.office;
		}
		if (saveCurrentAs(name, office))
		{
			reloadPresetCombo(name);
			updatePresetButtons();
		}
		return;
	}
}

void PDFExportDialog::presetSave()
{
	const QString name = selectedPreset();
	if (!PdfPresets::isUserPreset(name))
		return;
	if (saveCurrentAs(name, presetOfficeCheck->isChecked()))
		updatePresetButtons();
}

void PDFExportDialog::presetDelete()
{
	const QString name = selectedPreset();
	if (!PdfPresets::isUserPreset(name))
		return;
	const bool wasDefault = (PdfPresets::userDefaultName() == name);
	QString question = tr("Delete the preset \"%1\"?").arg(name);
	if (wasDefault)
		question += "\n" + tr("It is your Default preset; there will be no Default of your own afterwards.");
	if (ScMessageBox::question(this, tr("Delete Preset"), question,
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
		return;
	if (!PdfPresets::remove(name))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The preset could not be deleted."));
		return;
	}
	if (wasDefault)
		PdfPresets::setDefaultName(QString());
	// The settings on screen stay as they are; they just no longer have a name
	// (unless an office preset of the same name now shows through).
	m_appliedPreset.clear();
	presetModifiedLabel->hide();
	reloadPresetCombo(QString());
	updatePresetButtons();
}

void PDFExportDialog::presetSetDefault()
{
	const QString name = selectedPreset();
	if (name.isEmpty())
		return;
	PdfPresets::setDefaultName(name);
	reloadPresetCombo(name);
	updatePresetButtons();
}

void PDFExportDialog::presetUseCurrentAsDefault()
{
	// One of the user's own presets is selected: that is the name they gave
	// these settings. Otherwise they have not named them.
	QString name = selectedPreset();
	bool office = false;
	if (PdfPresets::isUserPreset(name))
		office = presetOfficeCheck->isChecked();
	else
	{
		name = tr("My Default");
		PdfPresets::Preset old;
		office = PdfPresets::load(name, old) && !old.readOnly && old.office;
	}
	if (!saveCurrentAs(name, office))
		return;
	PdfPresets::setDefaultName(name);
	reloadPresetCombo(name);
	updatePresetButtons();
}

void PDFExportDialog::presetExport()
{
	if (PdfPresets::userNames().isEmpty())
	{
		ScMessageBox::information(this, tr("Export Presets"), tr("You have no presets of your own to export yet."));
		return;
	}
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	const QString start = dirs->get("pdfpresets", ScPaths::userDocumentDir());
	// Scribus's own file dialog, not the desktop's: it does not hang on a
	// network folder that has stopped answering.
	CustomFDialog folderDialog(this, start, tr("Export Presets to Folder"), QString(), fdDirectoriesOnly);
	// Its "compress / include fonts / include profiles" boxes belong to Collect for Output.
	const QList<QCheckBox*> collectOptions = folderDialog.findChildren<QCheckBox*>();
	for (QCheckBox* cb : collectOptions)
		cb->hide();
	if (folderDialog.exec() != QDialog::Accepted)
		return;
	const QString dir = folderDialog.selectedFile();
	if (dir.isEmpty())
		return;
	dirs->set("pdfpresets", dir);
	QString error;
	const int written = PdfPresets::exportAll(dir, &error);
	if (!error.isEmpty())
		ScMessageBox::warning(this, CommonStrings::trWarning, error);
	else
		ScMessageBox::information(this, tr("Export Presets"), tr("%1 preset(s) written to\n%2").arg(written).arg(QDir::toNativeSeparators(dir)));
}

void PDFExportDialog::presetImport()
{
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	const QString start = dirs->get("pdfpresets", ScPaths::userDocumentDir());
	CustomFDialog fileDialog(this, start, tr("Import Presets"), tr("PDF Presets (*.json);;All Files (*)"), fdExistingFilesI | fdHidePreviewCheckBox);
	if (fileDialog.exec() != QDialog::Accepted)
		return;
	const QStringList files = fileDialog.selectedFiles();
	if (files.isEmpty())
		return;
	dirs->set("pdfpresets", QFileInfo(files.first()).absolutePath());
	QStringList imported, failed;
	for (const QString& file : files)
	{
		QString name, error;
		bool existed = false;
		if (PdfPresets::importFile(file, &name, false, &existed, &error))
		{
			imported << name;
			continue;
		}
		if (existed)
		{
			if (ScMessageBox::question(this, tr("Import Presets"), tr("A preset named \"%1\" already exists. Replace it?").arg(name),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes
				&& PdfPresets::importFile(file, &name, true, nullptr, &error))
				imported << name;
			continue;
		}
		failed << (error.isEmpty() ? QDir::toNativeSeparators(file) : error);
	}
	// A preset replaced under the one in force no longer matches the screen.
	reloadPresetCombo(selectedPreset());
	updatePresetButtons();
	QString message = tr("%1 preset(s) imported.").arg(imported.count());
	if (!failed.isEmpty())
		message += "\n" + tr("Not imported:") + "\n" + failed.join("\n");
	ScMessageBox::information(this, tr("Import Presets"), message);
}

void PDFExportDialog::presetOfficeToggled(bool on)
{
	const QString name = selectedPreset();
	PdfPresets::Preset preset;
	if (!PdfPresets::isUserPreset(name) || !PdfPresets::load(name, preset))
		return;
	// Only the mark changes; the stored settings are not replaced by what is
	// on screen.
	preset.office = on;
	QString error;
	if (!PdfPresets::save(preset, &error))
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The preset could not be saved:\n%1").arg(error));
	updatePresetButtons();
}

void PDFExportDialog::useDocumentSettingsToggled(bool on)
{
	const QString typedFileName = fileNameLineEdit->text();
	if (on)
	{
		m_opts = m_docOwnOpts;
		m_enableCmsOnExport = false;
		Options->enableCMS(m_doc->HasCMS);
		loadOptionsIntoWidgets();
		fileNameLineEdit->setText(typedFileName);
		m_appliedPreset.clear();
		presetModifiedLabel->hide();
		reloadPresetCombo(QString());
	}
	else
	{
		const QString defaultPreset = PdfPresets::defaultName();
		if (!defaultPreset.isEmpty() && applyPreset(defaultPreset))
			reloadPresetCombo(defaultPreset);
	}
	updatePresetButtons();
}

bool PDFExportDialog::prepareDirectExport(const QString& presetName, const QString& fileName)
{
	if (presetCombo->findData(presetName) < 0 || !applyPreset(presetName))
		return false;
	reloadPresetCombo(presetName);
	fileNameLineEdit->setText(QDir::toNativeSeparators(fileName));
	Options->AllPages->setChecked(true);
	finalizeForExport();
	return true;
}

void PDFExportDialog::enableSave()
{
	okButton->setEnabled(true);
}

void PDFExportDialog::disableSave()
{
	okButton->setEnabled(false);
}

void PDFExportDialog::handlePresetChange(int /*index*/)
{
	const QString name = selectedPreset();
	if (name.isEmpty())
	{
		// "Custom": the settings on screen stay, they just have no name.
		m_appliedPreset.clear();
		presetModifiedLabel->hide();
	}
	else if (!applyPreset(name))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The preset \"%1\" could not be read.").arg(name));
		reloadPresetCombo(m_appliedPreset);
	}
	updatePresetButtons();
}

void PDFExportDialog::presetToCustom()
{
	// Kept for TabPDFOptions::presetOverridden(); a changed preset is now
	// shown as "(modified)" by checkPresetModified() instead.
}

void PDFExportDialog::DoExport()
{
	// Check the page ranges
	bool hasInvalidPageRange = false;
	QString pageString(this->getPagesString());
	std::vector<int> pageNumbers;

	parsePagesString(pageString, &pageNumbers, m_doc->DocPages.count());
	for (size_t i = 0; i < pageNumbers.size(); ++i)
	{
		int pageNumber = pageNumbers[i];
		if (pageNumber < 1 || pageNumber > m_doc->DocPages.count())
		{
			hasInvalidPageRange = true;
			break;
		}
	}

	if ((pageNumbers.empty()) || hasInvalidPageRange)
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The range of pages to export is invalid.\nPlease check it and try again."));
		return;
	}

	// Checking if the path exists
	bool createPath = false;
	QString fn = QDir::fromNativeSeparators(fileNameLineEdit->text());
	QFileInfo fi(fn);
	QString dirPath = QDir::toNativeSeparators(fi.absolutePath());
	if (!QFile::exists(fi.absolutePath()))
	{
		if (ScMessageBox::question(this, tr( "Save as PDF" ),
									tr("%1 does not exists and will be created, continue?").arg(dirPath),
									QMessageBox::Ok | QMessageBox::Cancel,
									QMessageBox::NoButton,	// GUI default
									QMessageBox::Ok)	// batch default
				  == QMessageBox::Cancel)
		{
			return;
		}
		createPath = true;
	}
	
	// NOTE: Qt4 contains QDir::mkpath()
	QDir d(fn);
	if (createPath)
	{
		if (!d.mkpath(fi.absolutePath()))
		{
			ScMessageBox::warning(this,
								 CommonStrings::trWarning,
								 tr("Cannot create directory: \n%1").arg(dirPath));
			return;
		}
	}

	bool doIt = false;
	if (multiFile->isChecked())
		doIt = true;
	else
		doIt = overwrite(this, fn);
	if (!doIt) return;

	finalizeForExport();
	accept();
}

void PDFExportDialog::finalizeForExport()
{
	if (m_modifiedTimer)
		m_modifiedTimer->stop();
	// The preset needs colour management and the document has it off: now,
	// with the export decided, is when the document is switched over.
	if (m_enableCmsOnExport && !m_doc->HasCMS && ScCore->haveCMS())
		m_doc->enableCMS(true);
	int pageIndex = (Options->Pages->currentRow() >= 0) ? Options->Pages->currentRow() : 0;
	m_presEffects = Options->EffVal;
	if (pageIndex < m_presEffects.count())
	{
		m_presEffects[pageIndex].pageViewDuration = Options->PageTime->value();
		m_presEffects[pageIndex].pageEffectDuration = Options->EffectTime->value();
		m_presEffects[pageIndex].effectType = Options->EffectType->currentIndex();
		m_presEffects[pageIndex].Dm = Options->EDirection->currentIndex();
		m_presEffects[pageIndex].M = Options->EDirection_2->currentIndex();
		m_presEffects[pageIndex].Di = Options->EDirection_2_2->currentIndex();
	}
	m_opts.LPISettings[Options->SelLPIcolor].Frequency = Options->LPIfreq->value();
	m_opts.LPISettings[Options->SelLPIcolor].Angle = Options->LPIangle->value();
	m_opts.LPISettings[Options->SelLPIcolor].SpotFunc = Options->LPIfunc->currentIndex();
}

void PDFExportDialog::ChangeFile()
{
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString wdir  = dirs->get("pdf", ScPaths::userDocumentDir());

	QString wfile = QDir::fromNativeSeparators(fileNameLineEdit->text()); 
	if (!wfile.isEmpty())
	{
		QFileInfo fInfo(wfile);
		QString absPath = fInfo.absolutePath(); // Yes, we mean the file directory here
		if (QDir(absPath).exists())
			wdir = wfile;
	}

	QString d = QFileDialog::getSaveFileName(this, tr("Save As"), wdir, tr("PDF Files (*.pdf);;All Files (*)"), nullptr, QFileDialog::DontConfirmOverwrite);
	if (d.length() > 0)
	{
		QString fn(QDir::fromNativeSeparators(d));
		dirs->set("pdf", fn.left(fn.lastIndexOf("/")));
		fileNameLineEdit->setText(QDir::toNativeSeparators(d));
	}	
}

void PDFExportDialog::fileNameChanged()
{
	QString fileName = checkFileExtension(fileNameLineEdit->text(),"pdf");
	fileNameLineEdit->setText( QDir::toNativeSeparators(fileName) );
}

void PDFExportDialog::updateDocOptions()
{
	collectOptions(m_opts, true);
}

void PDFExportDialog::collectOptions(PDFOptions& opts, bool forExport)
{
	if (&opts != &m_opts)
	{
		// A snapshot (preset, "modified" check): include the LPI row being
		// edited, which DoExport() only writes back when exporting.
		if (!Options->SelLPIcolor.isEmpty() && opts.LPISettings.contains(Options->SelLPIcolor))
		{
			opts.LPISettings[Options->SelLPIcolor].Frequency = Options->LPIfreq->value();
			opts.LPISettings[Options->SelLPIcolor].Angle = Options->LPIangle->value();
			opts.LPISettings[Options->SelLPIcolor].SpotFunc = Options->LPIfunc->currentIndex();
		}
	}
	opts.fileName = QDir::fromNativeSeparators(fileNameLineEdit->text());
	opts.doMultiFile = multiFile->isChecked();
	opts.openAfterExport = openAfterExportCheckBox->isChecked();
	opts.Thumbnails = Options->CheckBox1->isChecked();
	opts.Compress = Options->Compression->isChecked();
	opts.CompressMethod = (PDFOptions::PDFCompression) Options->CMethod->currentIndex();
	opts.Quality = Options->CQuality->currentIndex();
	opts.Resolution = Options->Resolution->value();
	opts.FontEmbedding = Options->fontEmbeddingMode();
	opts.EmbedList = Options->fontsToEmbed();
	opts.SubsetList = Options->fontsToSubset();
	opts.OutlineList = Options->fontsToOutline();
	opts.RecalcPic = Options->DSColor->isChecked();
	opts.PicRes = Options->ValC->value();
	opts.embedPDF = Options->EmbedPDF->isChecked();
	opts.Bookmarks = Options->CheckBM->isChecked();
	opts.Binding = Options->ComboBind->currentIndex();
	opts.MirrorH = Options->MirrorH->isChecked();
	opts.MirrorV = Options->MirrorV->isChecked();
	opts.doClip = Options->ClipMarg->isChecked();
	opts.RotateDeg = Options->RotateDeg->currentIndex() * 90;
	opts.pageRangeSelection = Options->AllPages->isChecked() ? 0 : 1;
	opts.pageRangeString = Options->PageNr->text();
	opts.PresentMode = Options->CheckBox10->isChecked();
	if (opts.PresentMode && forExport)
	{
		for (int pg = 0; pg < m_doc->Pages->count(); ++pg)
		{
			m_doc->Pages->at(pg)->PresentVals = m_presEffects[pg];
		}
	}
	opts.Articles = Options->Article->isChecked();
	opts.Encrypt = Options->Encry->isChecked();
	opts.UseLPI = Options->UseLPI->isChecked();
	opts.useLayers = Options->useLayers->isChecked();
	opts.UseSpotColors = !Options->useSpot->isChecked();
	opts.displayBookmarks = Options->useBookmarks->isChecked();
	opts.displayFullscreen = Options->useFullScreen->isChecked();
	opts.displayLayers = Options->useLayers2->isChecked();
	opts.displayThumbs = Options->useThumbnails->isChecked();
	opts.hideMenuBar = Options->hideMenuBar->isChecked();
	opts.hideToolBar = Options->hideToolBar->isChecked();
	opts.fitWindow = Options->fitWindow->isChecked();
	opts.useDocBleeds = Options->docBleeds->isChecked();
	if (!Options->docBleeds->isChecked())
	{
		opts.bleeds.setTop(Options->bleedTopSpinBox->value() / m_unitRatio);
		opts.bleeds.setLeft(Options->bleedLeftSpinBox->value() / m_unitRatio);
		opts.bleeds.setRight(Options->bleedRightSpinBox->value() / m_unitRatio);
		opts.bleeds.setBottom(Options->bleedBottomSpinBox->value()/ m_unitRatio);
	}
	opts.markLength = Options->markLength->value() / m_unitRatio;
	opts.markOffset = Options->markOffset->value() / m_unitRatio;
	opts.cropMarks = Options->cropMarks->isChecked();
	opts.bleedMarks = Options->bleedMarks->isChecked();
	opts.registrationMarks = Options->registrationMarks->isChecked();
	opts.colorMarks = Options->colorMarks->isChecked();
	opts.docInfoMarks = Options->docInfoMarks->isChecked();
	int pgl = PDFOptions::SinglePage;
	if (Options->singlePage->isChecked())
		pgl = PDFOptions::SinglePage;
	else if (Options->continuousPages->isChecked())
		pgl = PDFOptions::OneColumn;
	else if (Options->facingPagesLeft->isChecked())
		pgl = PDFOptions::TwoColumnLeft;
	else if (Options->facingPagesRight->isChecked())
		pgl = PDFOptions::TwoColumnRight;
	opts.PageLayout = pgl;
	if (Options->actionCombo->currentIndex() != 0)
		opts.openAction = Options->actionCombo->currentText();
	else
		opts.openAction = "";
	if (Options->Encry->isChecked())
	{
		int Perm = -64;
		if (Options->PDFVersionCombo->version() == PDFVersion::PDF_14)
			Perm &= ~0x00240000;
		if (Options->PrintSec->isChecked())
			Perm += 4;
		if (Options->ModifySec->isChecked())
			Perm += 8;
		if (Options->CopySec->isChecked())
			Perm += 16;
		if (Options->AddSec->isChecked())
			Perm += 32;
		opts.Permissions = Perm;
		opts.PassOwner = Options->PassOwner->text();
		opts.PassUser = Options->PassUser->text();
	}
	opts.Version = Options->PDFVersionCombo->version();
	if (Options->OutCombo->currentIndex() == 0)
	{
		opts.UseRGB = true;
		opts.isGrayscale = false;
		opts.UseProfiles = false;
		opts.UseProfiles2 = false;
	}
	else
	{
		if (Options->OutCombo->currentIndex() == 2)
		{
			opts.isGrayscale = true;
			opts.UseRGB = false;
			opts.UseProfiles = false;
			opts.UseProfiles2 = false;
		}
		else
		{
			opts.isGrayscale = false;
			opts.UseRGB = false;
			if (cmsAvailableForExport())
			{
				opts.UseProfiles = Options->EmbedProfs->isChecked();
				opts.UseProfiles2 = Options->EmbedProfs2->isChecked();
				if (opts.Version != PDFVersion::PDF_X1a)
				{
					opts.Intent = Options->IntendS->currentIndex();
					opts.Intent2 = Options->IntendI->currentIndex();
					opts.EmbeddedI = Options->NoEmbedded->isChecked();
					opts.SolidProf = Options->SolidPr->currentText();
					opts.ImageProf = Options->ImageP->currentText();
				}
				opts.PrintProf = Options->PrintProfC->currentText();
				if ((opts.Version == PDFVersion::PDF_X3) || (opts.Version == PDFVersion::PDF_X1a) || (opts.Version == PDFVersion::PDF_X4))
				{
					opts.Info = Options->InfoString->text();
					opts.Encrypt = false;
					opts.MirrorH = false;
					opts.MirrorV = false;
					//#8306 : PDF/X-3 export ignores rotation setting
					//opts.RotateDeg = 0;
					opts.PresentMode = false;
				}
			}
			else
			{
				opts.UseProfiles = false;
				opts.UseProfiles2 = false;
			}
		}
	}
}

QString PDFExportDialog::getPagesString()
{
	if (Options->AllPages->isChecked())
		return "*";
	return Options->PageNr->text();
}
