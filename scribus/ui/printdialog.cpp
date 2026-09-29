/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "printdialog.h"

#include <QApplication>
#include <QDir>
#include <QMap>
#include <QScreen>
#include <QStringList>
#include <QByteArray>
#include <QInputDialog>
#include <QLineEdit>

#include "scconfig.h"

#include "commonstrings.h"
#include "customfdialog.h"
#include "iconmanager.h"
#include "prefsmanager.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "cupsoptions.h"
#include "ui/scmessagebox.h"
#if defined(_WIN32)
	#include <Windows.h>
	#include <winspool.h>
#elif defined(HAVE_CUPS) // Haiku doesn't have it
	#include <cups/cups.h>
#endif
#include "sccolor.h"
#include "scpaths.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "scrspinbox.h"
#include "ui/createrange.h"
#include "ui/tile_preview_widget.h"
#include "units.h"
#include "offset_tiling.h"
#include "usertaskstructs.h"
#include "util.h"
#include "util_printer.h"

extern bool previewDinUse;

PrintDialog::PrintDialog( QWidget* parent, ScribusDoc* doc, const PrintOptions& printOptions)
		: QDialog( parent ),
		  m_doc(doc),
		  m_unit(doc->unitIndex()),
		  m_unitRatio(doc->unitRatio()),
		  m_devMode(printOptions.devMode)
{
	setupUi(this);
	setModal(true);

	m_unitRatio = unitGetRatioFromIndex(doc->unitIndex());
	prefs = PrefsManager::instance().prefsFile->getContext("print_options");

	setWindowIcon(IconManager::instance().loadIcon("app-icon"));
	pageNrButton->setIcon(IconManager::instance().loadIcon("ellipsis"));
	markLength->setNewUnit(m_unit);
	markLength->setMinimum(1 * m_unitRatio);
	markLength->setMaximum(3000 * m_unitRatio);
	markOffset->setNewUnit(m_unit);
	markOffset->setMinimum(0);
	markOffset->setMaximum(3000 * m_unitRatio);
	bleedBottom->setNewUnit(m_unit);
	bleedBottom->setMinimum(0);
	bleedBottom->setMaximum(3000 * m_unitRatio);
	bleedLeft->setNewUnit(m_unit);
	bleedLeft->setMinimum(0);
	bleedLeft->setMaximum(3000 * m_unitRatio);
	bleedRight->setNewUnit(m_unit);
	bleedRight->setMinimum(0);
	bleedRight->setMaximum(3000 * m_unitRatio);
	bleedTop->setNewUnit(m_unit);
	bleedTop->setMinimum(0);
	bleedTop->setMaximum(3000 * m_unitRatio);

	if (ScCore->haveGS() || ScCore->isWinGUI())
		previewButton->setEnabled(!previewDinUse);
	else
	{
		previewButton->setVisible(false);
		previewButton->setEnabled(false);
	}

	// Fill printer list
	QString printerName;
	QStringList printerNames = PrinterUtil::getPrinterNames();
	int numPrinters = printerNames.count();
	for (int i = 0; i < numPrinters; i++)
	{
		printerName = printerNames[i];
		PrintDest->addItem(printerName);
	}
	PrintDest->addItem( CommonStrings::trFile );

	int prnIndex = PrintDest->findText(printOptions.printer);
	if (prnIndex < 0)
		prnIndex = PrintDest->findText(PrinterUtil::getDefaultPrinterName());
	if (prnIndex >= 0)
	{
		PrintDest->setCurrentIndex(prnIndex);
		prefs->set("CurrentPrn", PrintDest->currentText());
	}

	// Fill Separation list
	ColorList usedSpots;
	doc->getUsedColors(usedSpots, true);
	m_spotColors = usedSpots.keys();

	separationsCombo->addItem( tr("All"), "All" );
	separationsCombo->addItem( tr("Cyan"), "Cyan" );
	separationsCombo->addItem( tr("Magenta"), "Magenta" );
	separationsCombo->addItem( tr("Yellow"), "Yellow" );
	separationsCombo->addItem( tr("Black"), "Black" );
	for (int i = 0; i < m_spotColors.count(); ++i)
	{
		const QString& spotName = m_spotColors.at(i);
		separationsCombo->addItem(spotName, spotName);
	}

	if (m_doc->pagePositioning() != 0)
	{
		bleedLeftText->setText( tr( "Inside:" ) );
		bleedRightText->setText( tr( "Outside:" ) );
	}

	QString prnDevice = printOptions.printer;
	if (prnDevice.isEmpty())
		prnDevice = PrintDest->currentText();
	if ((prnDevice == CommonStrings::trFile) || (PrintDest->count() == 1))
	{
		PrintDest->setCurrentIndex(PrintDest->count()-1);
		prefs->set("CurrentPrn", PrintDest->currentText());
		DateiT->setEnabled(true);
		fileNameEdit->setEnabled(true);
		if (!printOptions.filename.isEmpty())
			fileNameEdit->setText(QDir::toNativeSeparators(printOptions.filename));
		selectFileButton->setEnabled(true);
		altComCheckBox->setChecked(false);
		altComCheckBox->setEnabled(false);
	}

	offsetSepInit();
	offsetTileInit();

	// The Offset Separations tab (printdialogbase.ui) can be tall enough
	// that this dialog's natural sizeHint() exceeds a normal desktop's
	// available height, pushing the Print/Cancel/Preview button row below
	// the screen edge with no way to reach it by mouse - reported directly
	// against a real 768-900px-tall screen. Clamp the maximum height to the
	// screen instead of the dialog's own (possibly oversized) sizeHint();
	// the Offset Separations tab's own QScrollArea (printdialogbase.ui) is
	// what keeps that tab's content usable once this constrains the dialog
	// below the tab's full natural height. Width is left exactly as before.
	QSize hint = sizeHint();
	int maxHeight = hint.height();
	if (QScreen* screen = QApplication::primaryScreen())
	{
		int availableHeight = screen->availableGeometry().height() - 100;
		// A floor, not just the screen-derived ceiling: a very short or
		// misreported screen must not shrink the dialog to something the
		// Print Destination group and tab bar alone couldn't fit in.
		maxHeight = qMin(maxHeight, qMax(400, availableHeight));
	}
	setMaximumSize(hint.width(), maxHeight);
	PrintDest->setFocus();

	// signals and slots connections
	connect( okButton, SIGNAL( clicked() ), this, SLOT( okButtonClicked() ) );
	connect( cancelButton, SIGNAL( clicked() ), this, SLOT( reject() ) );
	connect( PrintDest, SIGNAL(textActivated(QString)), this, SLOT(selectPrinter(QString)));
	connect( printLanguages, SIGNAL(textActivated(QString)), this, SLOT(selectPrintLanguage(QString)));
	connect( printAllRadio, SIGNAL(toggled(bool)), this, SLOT(selectRange(bool)));
	connect( printCurrentRadio, SIGNAL(toggled(bool)), this, SLOT(selectRange(bool)));
	connect( pageNrButton, SIGNAL(clicked()), this, SLOT(createPageNumberRange()));
	connect( printSepCombo, SIGNAL(activated(int)), this, SLOT(selectSepMode(int)));
	connect( selectFileButton, SIGNAL(clicked()), this, SLOT(selectFile()));
	connect( altComCheckBox, SIGNAL(clicked()), this, SLOT(selectCommand()));
	connect( previewButton, SIGNAL(clicked()), this, SLOT(previewButtonClicked()));
	connect( docBleeds, SIGNAL(clicked()), this, SLOT(doDocBleeds()));
	connect( optionsButton, SIGNAL( clicked() ), this, SLOT( selectOptions() ) );

	setStoredValues(printOptions.filename);
#if defined(_WIN32)
	if (!outputToFile())
	{
		m_devMode = printOptions.devMode;
		PrinterUtil::initDeviceSettings(PrintDest->currentText(), m_devMode);
	}
#endif

	printLanguages->setupLanguages(PrintDest->currentText(), outputToFile());
	printLanguages->setEnabled(printLanguages->count() > 1);
}

PrintDialog::~PrintDialog()
{
#ifdef HAVE_CUPS
	delete m_cupsOptions;
#endif
	m_cupsOptions = nullptr;
}

void PrintDialog::selectOptions()
{
#ifdef HAVE_CUPS
	if (!m_cupsOptions)
		m_cupsOptions = new CupsOptions(this, PrintDest->currentText());
	if (!m_cupsOptions->exec())
	{
		delete m_cupsOptions; // if options was canceled delete dia 
		m_cupsOptions = nullptr;    // so that getoptions() in the okButtonClicked() will get
		             // the default values from the last successful run
	}

#elif defined(_WIN32)
	bool done;
	Qt::HANDLE handle = nullptr;
	DEVMODEW* devMode = (DEVMODEW*) m_devMode.data();
	// Retrieve the selected printer
	QString printerS = PrintDest->currentText(); 
	// Get a printer handle
	done = OpenPrinterW((LPWSTR) printerS.utf16(), &handle, nullptr);
	if (!done)
		return;
	// Merge stored settings, prompt user and return user settings
	DocumentPropertiesW((HWND) winId(), handle, (LPWSTR) printerS.utf16(), (DEVMODEW*) m_devMode.data(), (DEVMODEW*) m_devMode.data(), 
						DM_IN_BUFFER | DM_IN_PROMPT | DM_OUT_BUFFER);
	// Free the printer handle
	ClosePrinter(handle);

	// With some drivers, one can set the number of copies in print option dialog
	// Set it back to Copies widget in this case
	devMode->dmCopies = qMax(1, qMin((int) devMode->dmCopies, Copies->maximum()));
	if (devMode->dmCopies != numCopies())
	{
		bool sigBlocked = Copies->blockSignals(true);
		Copies->setValue(devMode->dmCopies);
		Copies->blockSignals(sigBlocked);
	}
#endif
}

QString PrintDialog::getOptions()
{
#ifdef HAVE_CUPS
	QString printerOptions;
	if (!m_cupsOptions)
		m_cupsOptions = new CupsOptions(this, PrintDest->currentText());

	auto printOptions = m_cupsOptions->options();
	for (auto it = printOptions.begin(); it != printOptions.end(); ++it)
	{
		const QString& optionKey = it.key();
		const CupsOptions::OptionData& printOption = it.value(); 
		if (m_cupsOptions->useDefaultValue(optionKey))
			continue;

		if (printOption.keyword == "mirror")
			printerOptions += " -o mirror";
		else if (printOption.keyword == "page-set")
		{
			int pageSetIndex = m_cupsOptions->optionIndex(optionKey);
			if (pageSetIndex > 0)
			{
				printerOptions += " -o " + printOption.keyword + "=";
				printerOptions += (pageSetIndex == 1) ? "even" : "odd";
			}
		}
		else if (printOption.keyword == "number-up")
		{
			printerOptions += " -o " + printOption.keyword + "=";
			switch (m_cupsOptions->optionIndex(optionKey))
			{
				case 0:
					printerOptions += "1";
					break;
				case 1:
					printerOptions += "2";
					break;
				case 2:
					printerOptions += "4";
					break;
				case 3:
					printerOptions += "6";
					break;
				case 4:
					printerOptions += "9";
					break;
				case 5:
					printerOptions += "16";
					break;
			}
		}
		else if (printOption.keyword == "orientation")
			printerOptions += " -o landscape";
		else
		{
			printerOptions += " -o " + printOption.keyword + "=" + m_cupsOptions->optionText(optionKey);
		}
	}
	return printerOptions;
#else
	return QString();
#endif
}

void PrintDialog::selectCommand()
{
	bool test = altComCheckBox->isChecked();
	OthText->setEnabled(test);
	altCommand->setEnabled(test);
	PrintDest->setEnabled(!test);
	if (altComCheckBox->isChecked())
	{
		DateiT->setEnabled(false);
		fileNameEdit->setEnabled(false);
		selectFileButton->setEnabled(false);
		optionsButton->setEnabled(false);
	}
	else
	{
		selectPrinter(PrintDest->currentText());
		if (PrintDest->currentText() != CommonStrings::trFile)
			optionsButton->setEnabled(true);
	}
}

void PrintDialog::selectPrinter(const QString& prn)
{
	bool toFile = prn == CommonStrings::trFile;
	DateiT->setEnabled(toFile);
	fileNameEdit->setEnabled(toFile);
	selectFileButton->setEnabled(toFile);
	optionsButton->setEnabled(!toFile);
	altComCheckBox->setEnabled(!toFile);
	if (toFile)
		altComCheckBox->setChecked(false);
#if defined(_WIN32)
	if (!toFile)
	{
		if (!PrinterUtil::getDefaultSettings(PrintDest->currentText(), m_devMode))
			qWarning( tr("Failed to retrieve printer settings").toLatin1().data() );
	}
#endif
	if (toFile && fileNameEdit->text().isEmpty())
	{
		QFileInfo fi(m_doc->documentFileName());
		QString fileExt = (printLanguage() == PrintLanguage::PDF) ? ".pdf" : ".ps";
		if (fi.isRelative()) // if (m_doc->DocName.startsWith( tr("Document")))
			fileNameEdit->setText( QDir::toNativeSeparators(QDir::currentPath() + "/" + m_doc->documentFileName() + fileExt) );
		else
		{
			QString completeBaseName = fi.completeBaseName();
			if (completeBaseName.endsWith(".sla", Qt::CaseInsensitive))
				if (completeBaseName.length() > 4) completeBaseName.chop(4);
			if (completeBaseName.endsWith(".gz", Qt::CaseInsensitive))
				if (completeBaseName.length() > 3) completeBaseName.chop(3);
			fileNameEdit->setText( QDir::toNativeSeparators(fi.path() + "/" + completeBaseName + fileExt) );
		}
	}

	// Get page description language supported by the selected printer
	printLanguages->setupLanguages(prn, toFile);
	printLanguages->setEnabled(printLanguages->count() > 1);

	prefs->set("CurrentPrn", prn);
	prefs->set("CurrentPrnEngine", (int) printLanguages->currentLanguage());

	PrintLanguage prnLanguage = printLanguage();

	bool psSupported = false;
	psSupported |= (prnLanguage == PrintLanguage::PostScript1);
	psSupported |= (prnLanguage == PrintLanguage::PostScript2);
	psSupported |= (prnLanguage == PrintLanguage::PostScript3);

	printSepCombo->setEnabled(psSupported);
	separationsCombo->setEnabled(psSupported && (printSepCombo->currentIndex() == 1));
	if (!psSupported)
	{
		setCurrentComboItem(printSepCombo, tr("Print Normal"));
		setCurrentComboItem(separationsCombo, tr("All"));
	}
	// Offset separation screening is PostScript-only output (see
	// PSLib::PS_plate()/createPS()); a PDF or GDI target has no plate pages
	// to screen, so the tab's master switch follows the same PS-support test
	// as the existing Print Separations combo above.
	offsetSepEnabledCheck->setEnabled(psSupported);
	if (!psSupported)
		offsetSepEnabledCheck->setChecked(false);
	offsetTilePopulatePaperCombo();

	bool pdfMarksSupported = (prnLanguage == PrintLanguage::PDF);
	pdfMarksSupported |= (prnLanguage == PrintLanguage::PostScript1);
	pdfMarksSupported |= (prnLanguage == PrintLanguage::PostScript2);
	pdfMarksSupported |= (prnLanguage == PrintLanguage::PostScript3);
	usePDFMarks->setEnabled(pdfMarksSupported);

	if (outputToFile())
	{
		QString newExt = "prn";
		if (prnLanguage == PrintLanguage::PDF)
			newExt = "pdf";
		if (psSupported)
			newExt = "ps";
		QString outputFileName = fileNameEdit->text();
		if (!outputFileName.isEmpty())
		{
			QFileInfo fileInfo(outputFileName);
			QString currentExt = fileInfo.suffix();
			if (!currentExt.isEmpty())
			{
				int lastIndex = outputFileName.lastIndexOf(currentExt);
				outputFileName.replace(lastIndex, currentExt.length(), newExt);
				fileNameEdit->setText(outputFileName);
			}
		}
	}
}

void PrintDialog::selectPrintLanguage(const QString& prnLanguage)
{
	prefs->set("CurrentPrnEngine", (int) printLanguages->currentLanguage());

	bool psSupported = false;
	psSupported |= (prnLanguage == CommonStrings::trPostScript1);
	psSupported |= (prnLanguage == CommonStrings::trPostScript2);
	psSupported |= (prnLanguage == CommonStrings::trPostScript3);

	printSepCombo->setEnabled(psSupported);
	separationsCombo->setEnabled(psSupported && (printSepCombo->currentIndex() == 1));
	if (!psSupported)
	{
		setCurrentComboItem(printSepCombo, tr("Print Normal"));
		setCurrentComboItem(separationsCombo, tr("All"));
	}
	offsetSepEnabledCheck->setEnabled(psSupported);
	if (!psSupported)
		offsetSepEnabledCheck->setChecked(false);

	bool pdfMarksSupported = (prnLanguage == CommonStrings::trPDF);
	pdfMarksSupported |= (prnLanguage == CommonStrings::trPostScript1);
	pdfMarksSupported |= (prnLanguage == CommonStrings::trPostScript2);
	pdfMarksSupported |= (prnLanguage == CommonStrings::trPostScript3);
	usePDFMarks->setEnabled(pdfMarksSupported);

	if (outputToFile())
	{
		QString newExt = "prn";
		if (prnLanguage == CommonStrings::trPDF)
			newExt = "pdf";
		if (psSupported)
			newExt = "ps";
		QString outputFileName = fileNameEdit->text();
		if (!outputFileName.isEmpty())
		{
			QFileInfo fileInfo(outputFileName);
			QString currentExt = fileInfo.suffix();
			if (!currentExt.isEmpty())
			{
				int lastIndex = outputFileName.lastIndexOf(currentExt);
				outputFileName.replace(lastIndex, currentExt.length(), newExt);
				fileNameEdit->setText(outputFileName);
			}
		}
	}
}

void PrintDialog::selectRange(bool e)
{
	pageNr->setEnabled(!e);
	pageNrButton->setEnabled(!e);
}

void PrintDialog::selectSepMode(int e)
{
	separationsCombo->setEnabled(e != 0);
}

void PrintDialog::selectFile()
{
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString wdir = dirs->get("printdir", ScPaths::userDocumentDir());

	PrintLanguage prnLanguage = printLanguage();
	QString fileFilter = (prnLanguage == PrintLanguage::PDF) ? tr("PDF Files (*.pdf);;All Files (*)") : tr("PostScript Files (*.ps);;All Files (*)");
	CustomFDialog dia(this, wdir, tr("Save As"), fileFilter, fdNone | fdHidePreviewCheckBox);
	if (!fileNameEdit->text().isEmpty())
		dia.setSelection(fileNameEdit->text());
	if (dia.exec() == QDialog::Accepted)
	{
		QString selectedFile = dia.selectedFile();
		dirs->set("printdir", selectedFile.left(selectedFile.lastIndexOf("/")));
		fileNameEdit->setText( QDir::toNativeSeparators(selectedFile) );
	}
}

void PrintDialog::setMinMax(int min, int max, int cur)
{
	QString tmp, tmp2;
	printCurrentRadio->setText( tr( "Print Current Pa&ge" ) + " (" + tmp.setNum(cur) + ")");
	pageNr->setText(tmp.setNum(min) + "-" + tmp2.setNum(max));
}

void PrintDialog::storeValues()
{
	getOptions(); // options were not set get last options with this hack

	m_doc->Print_Options.printer = PrintDest->currentText();
	m_doc->Print_Options.filename = QDir::fromNativeSeparators(fileNameEdit->text());
	m_doc->Print_Options.toFile = outputToFile();
	m_doc->Print_Options.copies = numCopies();
	m_doc->Print_Options.outputSeparations = outputSeparations();
	m_doc->Print_Options.separationName = separationName();
	m_doc->Print_Options.allSeparations = allSeparations();
	if (m_doc->Print_Options.outputSeparations)
		m_doc->Print_Options.useSpotColors = true;
	else
		m_doc->Print_Options.useSpotColors = doSpot();
	m_doc->Print_Options.useColor = color();
	m_doc->Print_Options.mirrorH  = mirrorHorizontal();
	m_doc->Print_Options.mirrorV  = mirrorVertical();
	m_doc->Print_Options.doClip   = doClip();
	m_doc->Print_Options.doGCR    = doGCR();
	m_doc->Print_Options.prnLanguage = printLanguage();
	m_doc->Print_Options.setDevParam = doDev();
	m_doc->Print_Options.useDocBleeds  = docBleeds->isChecked();
	m_doc->Print_Options.bleeds.setTop(bleedTop->value() / m_doc->unitRatio());
	m_doc->Print_Options.bleeds.setLeft(bleedLeft->value() / m_doc->unitRatio());
	m_doc->Print_Options.bleeds.setRight(bleedRight->value() / m_doc->unitRatio());
	m_doc->Print_Options.bleeds.setBottom(bleedBottom->value() / m_doc->unitRatio());
	m_doc->Print_Options.markLength = markLength->value() / m_doc->unitRatio();
	m_doc->Print_Options.markOffset = markOffset->value() / m_doc->unitRatio();
	m_doc->Print_Options.cropMarks  = cropMarks->isChecked();
	m_doc->Print_Options.bleedMarks = bleedMarks->isChecked();
	m_doc->Print_Options.registrationMarks = registrationMarks->isChecked();
	m_doc->Print_Options.colorMarks = colorMarks->isChecked();
	m_doc->Print_Options.includePDFMarks = usePDFMarks->isChecked();
	if (altComCheckBox->isChecked())
	{
		m_doc->Print_Options.printerCommand = altCommand->text();
		m_doc->Print_Options.useAltPrintCommand = true;
	}
	else
		m_doc->Print_Options.useAltPrintCommand = false;
	m_doc->Print_Options.printerOptions = getOptions();
	m_doc->Print_Options.devMode = m_devMode;
	offsetSepStoreValues();
	offsetTileStoreValues();
}

void PrintDialog::okButtonClicked()
{
	storeValues();
	accept();
}

void PrintDialog::previewButtonClicked()
{
	storeValues();
	emit doPreview();
}

void PrintDialog::setStoredValues(const QString& fileName)
{
	if (m_doc->Print_Options.firstUse)
		PrinterUtil::getDefaultPrintOptions(m_doc->Print_Options, m_doc->bleedsVal());
	
	int selectedDest = PrintDest->findText(m_doc->Print_Options.printer);
	if ((selectedDest > -1) && (selectedDest < PrintDest->count()))
	{
		PrintDest->setCurrentIndex(selectedDest);
		prefs->set("CurrentPrn", PrintDest->currentText());
		bool printToFile = PrintDest->currentText() == CommonStrings::trFile;
		if (printToFile)
			fileNameEdit->setText(QDir::toNativeSeparators(fileName));
		printLanguages->setupLanguages(PrintDest->currentText(), printToFile);
		printLanguages->setEnabled(printLanguages->count() > 1);
		setPrintLanguage(m_doc->Print_Options.prnLanguage);
		selectPrinter(PrintDest->currentText());
	}
	if (PrintDest->currentText() != CommonStrings::trFile)
	{
		altComCheckBox->setChecked(m_doc->Print_Options.useAltPrintCommand);
		altComCheckBox->setEnabled(true);
	}
	else
	{
		altComCheckBox->setChecked(false);
		altComCheckBox->setEnabled(false);
	}
	if (altComCheckBox->isChecked())
	{
		selectCommand();
		altCommand->setText(m_doc->Print_Options.printerCommand);
	}
	printAllRadio->setChecked(prefs->getBool("PrintAll", true));
	printCurrentRadio->setChecked(prefs->getBool("CurrentPage", false));
	bool printRangeChecked = prefs->getBool("PrintRange", false);
	printRangeRadio->setChecked(printRangeChecked);
	pageNr->setEnabled(printRangeChecked);
	pageNr->setText(prefs->get("PageNr", "1-1"));
	Copies->setValue(1);
	printSepCombo->setCurrentIndex(m_doc->Print_Options.outputSeparations);
	colorType->setCurrentIndex(m_doc->Print_Options.useColor ? 0 : 1);
	int selectedSep = separationsCombo->findData(m_doc->Print_Options.separationName);
	separationsCombo->setCurrentIndex((selectedSep >= 0) ? selectedSep : 0);
	if (printSepCombo->currentIndex() == 1)
		separationsCombo->setEnabled(true);
	setPrintLanguage(m_doc->Print_Options.prnLanguage);
	mirrorHor->setChecked(m_doc->Print_Options.mirrorH);
	mirrorVert->setChecked(m_doc->Print_Options.mirrorV);
	setMediaSize->setChecked(m_doc->Print_Options.setDevParam);
	applyGCR->setChecked(m_doc->Print_Options.doGCR);
	clipMargins->setChecked(m_doc->Print_Options.doClip);
	convertSpots->setChecked(!m_doc->Print_Options.useSpotColors);
	docBleeds->setChecked(m_doc->Print_Options.useDocBleeds);
	if (docBleeds->isChecked())
	{
		bleedTop->setValue(m_doc->bleeds()->top() * m_unitRatio);
		bleedBottom->setValue(m_doc->bleeds()->bottom() * m_unitRatio);
		bleedRight->setValue(m_doc->bleeds()->right() * m_unitRatio);
		bleedLeft->setValue(m_doc->bleeds()->left() * m_unitRatio);
	}
	else
	{
		bleedTop->setValue(m_doc->Print_Options.bleeds.top() * m_unitRatio);
		bleedBottom->setValue(m_doc->Print_Options.bleeds.bottom() * m_unitRatio);
		bleedRight->setValue(m_doc->Print_Options.bleeds.right() * m_unitRatio);
		bleedLeft->setValue(m_doc->Print_Options.bleeds.left() * m_unitRatio);
	}
	bleedTop->setEnabled(!docBleeds->isChecked());
	bleedBottom->setEnabled(!docBleeds->isChecked());
	bleedRight->setEnabled(!docBleeds->isChecked());
	bleedLeft->setEnabled(!docBleeds->isChecked());
	markLength->setValue(m_doc->Print_Options.markLength * m_unitRatio);
	markOffset->setValue(m_doc->Print_Options.markOffset * m_unitRatio);
	cropMarks->setChecked(m_doc->Print_Options.cropMarks);
	bleedMarks->setChecked(m_doc->Print_Options.bleedMarks);
	registrationMarks->setChecked(m_doc->Print_Options.registrationMarks);
	colorMarks->setChecked(m_doc->Print_Options.colorMarks);
	usePDFMarks->setChecked(m_doc->Print_Options.includePDFMarks);
	offsetSepSetStoredValues();
	offsetTileSetStoredValues();
}

QString PrintDialog::printerName() const
{
	return PrintDest->currentText();
}

QString PrintDialog::outputFileName() const
{
	return QDir::fromNativeSeparators(fileNameEdit->text());
}

bool PrintDialog::outputToFile() const
{
	return (PrintDest->currentText() == CommonStrings::trFile);
}

bool PrintDialog::isProofPrint() const
{
	return proofPrint->isChecked();
}

int PrintDialog::numCopies() const
{
	return Copies->value();
}

bool PrintDialog::outputSeparations() const
{
	return separationsCombo->isEnabled();
}

QString PrintDialog::separationName() const
{
	if (separationsCombo->currentIndex() == 0)
		return QString("All");
	if (separationsCombo->currentIndex() == 1)
		return QString("Cyan");
	if (separationsCombo->currentIndex() == 2)
		return QString("Magenta");
	if (separationsCombo->currentIndex() == 3)
		return QString("Yellow");
	if (separationsCombo->currentIndex() == 4)
		return QString("Black");
	return separationsCombo->currentText();
}

QStringList PrintDialog::allSeparations() const
{
	QStringList ret;
	for (int i = 1; i < separationsCombo->count(); ++i)
		ret.append(separationsCombo->itemText(i));
	return ret;
}

bool PrintDialog::color() const
{
	return colorType->currentIndex() == 0;
}

bool PrintDialog::mirrorHorizontal() const
{
	return mirrorHor->isChecked();
}

bool PrintDialog::mirrorVertical() const
{
	return mirrorVert->isChecked();
}

bool PrintDialog::doGCR() const
{
	return applyGCR->isChecked();
}

bool PrintDialog::doClip() const
{
	return clipMargins->isChecked();
}

PrintLanguage PrintDialog::printLanguage() const
{
	return printLanguages->currentLanguage();
}

bool PrintDialog::doDev() const
{
	return setMediaSize->isChecked();
}

bool PrintDialog::doSpot() const
{
	return !convertSpots->isChecked();
}

bool PrintDialog::doPrintAll() const
{
	return printAllRadio->isChecked();
}

bool PrintDialog::doPrintCurrentPage() const
{
	return printCurrentRadio->isChecked();
}

QString PrintDialog::getPageString() const
{
	return pageNr->text();
}

void PrintDialog::doDocBleeds()
{
	if (docBleeds->isChecked())
	{
		prefs->set("BleedTop", bleedTop->value() / m_unitRatio);
		prefs->set("BleedBottom", bleedBottom->value() / m_unitRatio);
		prefs->set("BleedRight", bleedRight->value() / m_unitRatio);
		prefs->set("BleedLeft", bleedLeft->value() / m_unitRatio);
		bleedTop->setValue(m_doc->bleeds()->top() * m_unitRatio);
		bleedBottom->setValue(m_doc->bleeds()->bottom() * m_unitRatio);
		bleedRight->setValue(m_doc->bleeds()->right() * m_unitRatio);
		bleedLeft->setValue(m_doc->bleeds()->left() * m_unitRatio);
	}
	else
	{
		bleedTop->setValue(prefs->getDouble("BleedTop",0.0) * m_unitRatio);
		bleedBottom->setValue(prefs->getDouble("BleedBottom",0.0) * m_unitRatio);
		bleedRight->setValue(prefs->getDouble("BleedRight",0.0) * m_unitRatio);
		bleedLeft->setValue(prefs->getDouble("BleedLeft",0.0) * m_unitRatio);
	}
	bool isChecked = docBleeds->isChecked();
	prefs->set("UseDocBleeds", isChecked);
	bleedTop->setEnabled(!isChecked);
	bleedBottom->setEnabled(!isChecked);
	bleedRight->setEnabled(!isChecked);
	bleedLeft->setEnabled(!isChecked);
}

void PrintDialog::createPageNumberRange( )
{
	if (m_doc != nullptr)
	{
		CreateRange cr(pageNr->text(), m_doc->DocPages.count(), this);
		if (cr.exec())
		{
			CreateRangeData crData;
			cr.getCreateRangeData(crData);
			pageNr->setText(crData.pageRange);
			return;
		}
	}
	pageNr->setText(QString());
}

void PrintDialog::setPrintLanguage(PrintLanguage prnLanguage)
{
	int itemIndex = printLanguages->findLanguage(prnLanguage);
	if (itemIndex >= 0)
		printLanguages->setCurrentIndex(itemIndex);
	else if (printLanguages->count() > 0)
	{
		itemIndex = printLanguages->findLanguage(PrintLanguage::PostScript3);
		if (itemIndex >= 0)
			printLanguages->setCurrentIndex(itemIndex);
		else
			printLanguages->setCurrentIndex(printLanguages->count() - 1);
	}
}

// -------------------------------------------------------------------------
// Offset Separations tab
// -------------------------------------------------------------------------

void PrintDialog::offsetSepInit()
{
	m_offsetUpdatingUI = true;

	for (const QString& shapeName : offsetDotShapeNames())
		offsetSepDotShapeCombo->addItem(shapeName);

	offsetCyanAngle->setDecimals(1);
	offsetMagentaAngle->setDecimals(1);
	offsetYellowAngle->setDecimals(1);
	offsetBlackAngle->setDecimals(1);

	m_offsetBuiltInPresets = OffsetSepPresetLibrary::bundledPresets();
	offsetSepLoadCustomPresets();
	offsetSepPopulatePresetCombo();

	m_offsetUpdatingUI = false;

	connect(offsetSepEnabledCheck, SIGNAL(toggled(bool)), this, SLOT(offsetSepToggled(bool)));
	connect(offsetModeCmykRadio, SIGNAL(toggled(bool)), this, SLOT(offsetModeChanged()));
	connect(offsetModeGrayscaleRadio, SIGNAL(toggled(bool)), this, SLOT(offsetModeChanged()));
	connect(offsetModeFullColorRadio, SIGNAL(toggled(bool)), this, SLOT(offsetModeChanged()));
	connect(offsetSepPresetCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(offsetSepPresetChanged(int)));
	connect(offsetSepSaveButton, SIGNAL(clicked()), this, SLOT(offsetSepSavePreset()));
	connect(offsetSepDeleteButton, SIGNAL(clicked()), this, SLOT(offsetSepDeletePreset()));
	connect(offsetSepResetButton, SIGNAL(clicked()), this, SLOT(offsetSepResetDefaults()));

	connect(offsetSepResolution, SIGNAL(valueChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetSepDotShapeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetCyanLPI, SIGNAL(valueChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetCyanAngle, SIGNAL(valueChanged(double)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetCyanPrint, SIGNAL(toggled(bool)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetMagentaLPI, SIGNAL(valueChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetMagentaAngle, SIGNAL(valueChanged(double)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetMagentaPrint, SIGNAL(toggled(bool)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetYellowLPI, SIGNAL(valueChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetYellowAngle, SIGNAL(valueChanged(double)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetYellowPrint, SIGNAL(toggled(bool)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetBlackLPI, SIGNAL(valueChanged(int)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetBlackAngle, SIGNAL(valueChanged(double)), this, SLOT(offsetSepFieldChanged()));
	connect(offsetBlackPrint, SIGNAL(toggled(bool)), this, SLOT(offsetSepFieldChanged()));

	offsetSepUpdateEnableState();
}

void PrintDialog::offsetSepUpdateEnableState()
{
	bool enabled = offsetSepEnabledCheck->isChecked();
	offsetModeGroup->setEnabled(enabled);
	offsetSepOutputGroup->setEnabled(enabled);
	// Per-plate LPI/angle/print only means anything in CMYK Separations mode -
	// Grayscale screens with a single angle (see offsetSelectedOutputMode())
	// and Full Color applies no screening at all.
	offsetSepPlatesGroup->setEnabled(enabled && (offsetSelectedOutputMode() == OffsetOutputMode::CmykSeparations));
	// Composite colour/grayscale choice does not apply once separations are
	// being screened plate by plate - see the deliverable's mutual-exclusion
	// requirement for this checkbox.
	colorType->setEnabled(!enabled);
}

void PrintDialog::offsetSepToggled(bool checked)
{
	Q_UNUSED(checked)
	offsetSepUpdateEnableState();
	offsetTileUpdateInfo();
}

OffsetOutputMode PrintDialog::offsetSelectedOutputMode() const
{
	if (offsetModeGrayscaleRadio->isChecked())
		return OffsetOutputMode::Grayscale;
	if (offsetModeFullColorRadio->isChecked())
		return OffsetOutputMode::FullColor;
	return OffsetOutputMode::CmykSeparations;
}

void PrintDialog::offsetModeChanged()
{
	if (m_offsetUpdatingUI)
		return;
	offsetSepUpdateEnableState();
	offsetTileUpdateInfo();
}

void PrintDialog::offsetSepPopulatePresetCombo()
{
	bool wasUpdating = m_offsetUpdatingUI;
	m_offsetUpdatingUI = true;
	QString previousData = offsetSepPresetCombo->currentData().toString();
	offsetSepPresetCombo->clear();
	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
		offsetSepPresetCombo->addItem(preset.name, preset.name);
	for (const OffsetSepPreset& preset : std::as_const(m_offsetCustomPresets))
		offsetSepPresetCombo->addItem(preset.name, preset.name);
	if (!previousData.isEmpty())
	{
		int idx = offsetSepFindPresetIndex(previousData);
		if (idx >= 0)
			offsetSepPresetCombo->setCurrentIndex(idx);
	}
	m_offsetUpdatingUI = wasUpdating;
}

int PrintDialog::offsetSepFindPresetIndex(const QString& name) const
{
	for (int i = 0; i < offsetSepPresetCombo->count(); ++i)
	{
		if (offsetSepPresetCombo->itemData(i).toString() == name)
			return i;
	}
	return -1;
}

OffsetSepPreset PrintDialog::offsetSepCollectSettings() const
{
	OffsetSepPreset preset;
	preset.name = m_offsetActivePresetName;
	preset.resolution = offsetSepResolution->value();
	preset.dotShape = static_cast<OffsetDotShape>(offsetSepDotShapeCombo->currentIndex());
	preset.cyan    = { static_cast<double>(offsetCyanLPI->value()), offsetCyanAngle->value(), offsetCyanPrint->isChecked() };
	preset.magenta = { static_cast<double>(offsetMagentaLPI->value()), offsetMagentaAngle->value(), offsetMagentaPrint->isChecked() };
	preset.yellow  = { static_cast<double>(offsetYellowLPI->value()), offsetYellowAngle->value(), offsetYellowPrint->isChecked() };
	preset.black   = { static_cast<double>(offsetBlackLPI->value()), offsetBlackAngle->value(), offsetBlackPrint->isChecked() };
	return preset;
}

void PrintDialog::offsetSepApplyPreset(const OffsetSepPreset& preset)
{
	bool wasUpdating = m_offsetUpdatingUI;
	m_offsetUpdatingUI = true;

	offsetSepResolution->setValue(preset.resolution);
	offsetSepDotShapeCombo->setCurrentIndex(static_cast<int>(preset.dotShape));
	offsetCyanLPI->setValue(qRound(preset.cyan.lpi));
	offsetCyanAngle->setValue(preset.cyan.angle);
	offsetCyanPrint->setChecked(preset.cyan.printPlate);
	offsetMagentaLPI->setValue(qRound(preset.magenta.lpi));
	offsetMagentaAngle->setValue(preset.magenta.angle);
	offsetMagentaPrint->setChecked(preset.magenta.printPlate);
	offsetYellowLPI->setValue(qRound(preset.yellow.lpi));
	offsetYellowAngle->setValue(preset.yellow.angle);
	offsetYellowPrint->setChecked(preset.yellow.printPlate);
	offsetBlackLPI->setValue(qRound(preset.black.lpi));
	offsetBlackAngle->setValue(preset.black.angle);
	offsetBlackPrint->setChecked(preset.black.printPlate);

	m_offsetActivePresetName = preset.name;

	int idx = offsetSepFindPresetIndex(preset.name);
	if (idx >= 0)
	{
		offsetSepPresetCombo->setItemText(idx, preset.name);
		offsetSepPresetCombo->setCurrentIndex(idx);
	}
	offsetSepDeleteButton->setEnabled(!preset.builtIn && (idx >= 0));

	m_offsetUpdatingUI = wasUpdating;
}

void PrintDialog::offsetSepFieldChanged()
{
	if (m_offsetUpdatingUI)
		return;
	int idx = offsetSepPresetCombo->currentIndex();
	if (idx < 0)
		return;
	QString baseName = offsetSepPresetCombo->itemData(idx).toString();
	QString modifiedText = baseName + QStringLiteral(" *");
	if (offsetSepPresetCombo->itemText(idx) != modifiedText)
		offsetSepPresetCombo->setItemText(idx, modifiedText);
}

void PrintDialog::offsetSepPresetChanged(int index)
{
	if (m_offsetUpdatingUI)
		return;
	if ((index < 0) || (index >= offsetSepPresetCombo->count()))
		return;
	QString name = offsetSepPresetCombo->itemData(index).toString();
	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name == name)
		{
			offsetSepApplyPreset(preset);
			return;
		}
	}
	for (const OffsetSepPreset& preset : std::as_const(m_offsetCustomPresets))
	{
		if (preset.name == name)
		{
			offsetSepApplyPreset(preset);
			return;
		}
	}
}

void PrintDialog::offsetSepSavePreset()
{
	bool activeIsBuiltIn = false;
	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name == m_offsetActivePresetName)
		{
			activeIsBuiltIn = true;
			break;
		}
	}

	bool ok = false;
	QString suggested = activeIsBuiltIn ? QString() : m_offsetActivePresetName;
	QString name = QInputDialog::getText(this, tr("Save Offset Separation Preset"),
		tr("Preset name:"), QLineEdit::Normal, suggested, &ok);
	if (!ok)
		return;
	name = name.trimmed();
	if (name.isEmpty())
		return;

	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name.compare(name, Qt::CaseInsensitive) == 0)
		{
			ScMessageBox::warning(this, CommonStrings::trWarning,
				tr("\"%1\" is a bundled preset and cannot be overwritten. Choose a different name.").arg(name));
			return;
		}
	}

	OffsetSepPreset preset = offsetSepCollectSettings();
	preset.name = name;
	preset.builtIn = false;

	bool replaced = false;
	for (OffsetSepPreset& existing : m_offsetCustomPresets)
	{
		if (existing.name.compare(name, Qt::CaseInsensitive) == 0)
		{
			existing = preset;
			replaced = true;
			break;
		}
	}
	if (!replaced)
		m_offsetCustomPresets.append(preset);

	offsetSepPersistCustomPresets();
	offsetSepPopulatePresetCombo();
	offsetSepApplyPreset(preset);
}

void PrintDialog::offsetSepDeletePreset()
{
	int idx = offsetSepPresetCombo->currentIndex();
	if (idx < 0)
		return;
	QString name = offsetSepPresetCombo->itemData(idx).toString();

	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name == name)
			return; // bundled presets cannot be deleted; button should already be disabled
	}

	int removeIdx = -1;
	for (int i = 0; i < m_offsetCustomPresets.count(); ++i)
	{
		if (m_offsetCustomPresets.at(i).name == name)
		{
			removeIdx = i;
			break;
		}
	}
	if (removeIdx < 0)
		return;

	m_offsetCustomPresets.remove(removeIdx);
	offsetSepPersistCustomPresets();
	offsetSepPopulatePresetCombo();

	QString defaultName = OffsetSepPresetLibrary::defaultPresetName();
	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name == defaultName)
		{
			offsetSepApplyPreset(preset);
			break;
		}
	}
}

void PrintDialog::offsetSepResetDefaults()
{
	QString defaultName = OffsetSepPresetLibrary::defaultPresetName();
	for (const OffsetSepPreset& preset : std::as_const(m_offsetBuiltInPresets))
	{
		if (preset.name == defaultName)
		{
			offsetSepApplyPreset(preset);
			break;
		}
	}
	offsetSepEnabledCheck->setChecked(false);
}

void PrintDialog::offsetSepLoadCustomPresets()
{
	QString json = prefs->get("OffsetSepCustomPresets", QString());
	m_offsetCustomPresets = OffsetSepPresetLibrary::parseCustomPresets(json);
}

void PrintDialog::offsetSepPersistCustomPresets()
{
	prefs->set("OffsetSepCustomPresets", OffsetSepPresetLibrary::serializeCustomPresets(m_offsetCustomPresets));
	// Custom presets are user-authored data the operator explicitly asked to
	// save/delete, unlike the rest of this dialog's per-field prefs (which
	// only reach disk at application quit) - flush immediately so a save
	// survives a crash between now and the next quit.
	PrefsManager::instance().savePrefsXML();
}

void PrintDialog::offsetSepStoreValues()
{
	OffsetSepPreset current = offsetSepCollectSettings();

	m_doc->Print_Options.offsetSepEnabled = offsetSepEnabledCheck->isChecked();
	m_doc->Print_Options.offsetSepResolution = current.resolution;
	m_doc->Print_Options.offsetSepDotShape = offsetDotShapeName(current.dotShape);
	m_doc->Print_Options.offsetSepCyanLPI = current.cyan.lpi;
	m_doc->Print_Options.offsetSepCyanAngle = current.cyan.angle;
	m_doc->Print_Options.offsetSepCyanPrint = current.cyan.printPlate;
	m_doc->Print_Options.offsetSepMagentaLPI = current.magenta.lpi;
	m_doc->Print_Options.offsetSepMagentaAngle = current.magenta.angle;
	m_doc->Print_Options.offsetSepMagentaPrint = current.magenta.printPlate;
	m_doc->Print_Options.offsetSepYellowLPI = current.yellow.lpi;
	m_doc->Print_Options.offsetSepYellowAngle = current.yellow.angle;
	m_doc->Print_Options.offsetSepYellowPrint = current.yellow.printPlate;
	m_doc->Print_Options.offsetSepBlackLPI = current.black.lpi;
	m_doc->Print_Options.offsetSepBlackAngle = current.black.angle;
	m_doc->Print_Options.offsetSepBlackPrint = current.black.printPlate;
	m_doc->Print_Options.offsetOutputMode = offsetOutputModeName(offsetSelectedOutputMode());

	prefs->set("OffsetSepEnabled", m_doc->Print_Options.offsetSepEnabled);
	prefs->set("OffsetOutputMode", m_doc->Print_Options.offsetOutputMode);
	prefs->set("OffsetSepResolution", m_doc->Print_Options.offsetSepResolution);
	prefs->set("OffsetSepDotShape", m_doc->Print_Options.offsetSepDotShape);
	prefs->set("OffsetSepCyanLPI", m_doc->Print_Options.offsetSepCyanLPI);
	prefs->set("OffsetSepCyanAngle", m_doc->Print_Options.offsetSepCyanAngle);
	prefs->set("OffsetSepCyanPrint", m_doc->Print_Options.offsetSepCyanPrint);
	prefs->set("OffsetSepMagentaLPI", m_doc->Print_Options.offsetSepMagentaLPI);
	prefs->set("OffsetSepMagentaAngle", m_doc->Print_Options.offsetSepMagentaAngle);
	prefs->set("OffsetSepMagentaPrint", m_doc->Print_Options.offsetSepMagentaPrint);
	prefs->set("OffsetSepYellowLPI", m_doc->Print_Options.offsetSepYellowLPI);
	prefs->set("OffsetSepYellowAngle", m_doc->Print_Options.offsetSepYellowAngle);
	prefs->set("OffsetSepYellowPrint", m_doc->Print_Options.offsetSepYellowPrint);
	prefs->set("OffsetSepBlackLPI", m_doc->Print_Options.offsetSepBlackLPI);
	prefs->set("OffsetSepBlackAngle", m_doc->Print_Options.offsetSepBlackAngle);
	prefs->set("OffsetSepBlackPrint", m_doc->Print_Options.offsetSepBlackPrint);
}

void PrintDialog::offsetSepSetStoredValues()
{
	const PrintOptions& opts = m_doc->Print_Options;

	OffsetSepPreset preset;
	preset.name = m_offsetActivePresetName.isEmpty() ? OffsetSepPresetLibrary::defaultPresetName() : m_offsetActivePresetName;
	preset.resolution = opts.offsetSepResolution;
	preset.dotShape = offsetDotShapeFromName(opts.offsetSepDotShape, nullptr);
	preset.cyan    = { opts.offsetSepCyanLPI, opts.offsetSepCyanAngle, opts.offsetSepCyanPrint };
	preset.magenta = { opts.offsetSepMagentaLPI, opts.offsetSepMagentaAngle, opts.offsetSepMagentaPrint };
	preset.yellow  = { opts.offsetSepYellowLPI, opts.offsetSepYellowAngle, opts.offsetSepYellowPrint };
	preset.black   = { opts.offsetSepBlackLPI, opts.offsetSepBlackAngle, opts.offsetSepBlackPrint };

	// If these values still match a known preset exactly (true on first use,
	// where they come straight from PrinterUtil::getDefaultPrintOptions()'s
	// Malayalam-Newspaper-shaped defaults) show that preset selected and
	// unmodified; otherwise just load the values with no preset highlighted.
	bool matched = false;
	for (const OffsetSepPreset& candidate : std::as_const(m_offsetBuiltInPresets))
	{
		if ((candidate.resolution == preset.resolution) && (candidate.dotShape == preset.dotShape)
			&& (candidate.cyan == preset.cyan) && (candidate.magenta == preset.magenta)
			&& (candidate.yellow == preset.yellow) && (candidate.black == preset.black))
		{
			offsetSepApplyPreset(candidate);
			matched = true;
			break;
		}
	}
	if (!matched)
	{
		for (const OffsetSepPreset& candidate : std::as_const(m_offsetCustomPresets))
		{
			if ((candidate.resolution == preset.resolution) && (candidate.dotShape == preset.dotShape)
				&& (candidate.cyan == preset.cyan) && (candidate.magenta == preset.magenta)
				&& (candidate.yellow == preset.yellow) && (candidate.black == preset.black))
			{
				offsetSepApplyPreset(candidate);
				matched = true;
				break;
			}
		}
	}
	if (!matched)
	{
		bool wasUpdating = m_offsetUpdatingUI;
		m_offsetUpdatingUI = true;
		offsetSepResolution->setValue(preset.resolution);
		offsetSepDotShapeCombo->setCurrentIndex(static_cast<int>(preset.dotShape));
		offsetCyanLPI->setValue(qRound(preset.cyan.lpi));
		offsetCyanAngle->setValue(preset.cyan.angle);
		offsetCyanPrint->setChecked(preset.cyan.printPlate);
		offsetMagentaLPI->setValue(qRound(preset.magenta.lpi));
		offsetMagentaAngle->setValue(preset.magenta.angle);
		offsetMagentaPrint->setChecked(preset.magenta.printPlate);
		offsetYellowLPI->setValue(qRound(preset.yellow.lpi));
		offsetYellowAngle->setValue(preset.yellow.angle);
		offsetYellowPrint->setChecked(preset.yellow.printPlate);
		offsetBlackLPI->setValue(qRound(preset.black.lpi));
		offsetBlackAngle->setValue(preset.black.angle);
		offsetBlackPrint->setChecked(preset.black.printPlate);
		m_offsetActivePresetName.clear();
		offsetSepPresetCombo->setCurrentIndex(-1);
		offsetSepDeleteButton->setEnabled(false);
		m_offsetUpdatingUI = wasUpdating;
	}

	bool okMode = false;
	OffsetOutputMode mode = offsetOutputModeFromName(opts.offsetOutputMode, &okMode);
	if (!okMode)
		mode = OffsetOutputMode::CmykSeparations;
	if (mode == OffsetOutputMode::Grayscale)
		offsetModeGrayscaleRadio->setChecked(true);
	else if (mode == OffsetOutputMode::FullColor)
		offsetModeFullColorRadio->setChecked(true);
	else
		offsetModeCmykRadio->setChecked(true);

	offsetSepEnabledCheck->setChecked(opts.offsetSepEnabled);
	offsetSepUpdateEnableState();
}

// -------------------------------------------------------------------------
// Offset Separations tab: Tiling
// -------------------------------------------------------------------------

void PrintDialog::offsetTileInit()
{
	for (const QString& name : offsetTilePrintOrderNames())
		offsetTilePrintOrderCombo->addItem(name);

	offsetTileCustomWidth->setDecimals(1);
	offsetTileCustomHeight->setDecimals(1);
	offsetTileOverlap->setDecimals(1);

	connect(offsetTileEnabledCheck, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTilePaperCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(offsetTilePaperChanged(int)));
	connect(offsetTileCustomWidth, SIGNAL(valueChanged(double)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileCustomHeight, SIGNAL(valueChanged(double)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileOrientPortraitRadio, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileOrientLandscapeRadio, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileOrientAutoRadio, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileOverlap, SIGNAL(valueChanged(double)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileRegMarksCheck, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileCutMarksCheck, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTileLabelCheck, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));
	connect(offsetTilePrintOrderCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(offsetTileFieldChanged()));
	// The separations master switch and output mode change the total-sheets
	// figure the live info label shows below - keep it in sync too.
	connect(offsetSepEnabledCheck, SIGNAL(toggled(bool)), this, SLOT(offsetTileFieldChanged()));

	// Small schematic tile-arrangement preview, built here rather than in
	// Designer XML (see offsetTilePreviewContainer in printdialogbase.ui) so
	// this .ui stays free of a <customwidgets> promotion entry to keep in
	// sync by hand.
	m_offsetTilePreview = new TilePreviewWidget(offsetTilePreviewContainer);
	offsetTilePreviewContainer->layout()->addWidget(m_offsetTilePreview);

	offsetTilePopulatePaperCombo(); // also applies enable-state and the first info update
}

void PrintDialog::offsetTilePopulatePaperCombo()
{
	QString previous = (offsetTilePaperCombo->count() > 0)
		? offsetTilePaperCombo->currentData().toString()
		: m_doc->Print_Options.offsetTilePaperSize;

	bool wasBlocked = offsetTilePaperCombo->blockSignals(true);
	offsetTilePaperCombo->clear();

	QStringList names;
	QString defaultName;
	// Print-to-file has no queue to ask, and lpoptions can simply fail to
	// answer for a queue that's offline - either way fall back to a fixed
	// list of sizes this dialog already knows how to size (see the A0-A2
	// entries PrinterUtil::paperSizePoints() gained for this feature).
	bool detected = !outputToFile() && PrinterUtil::getSupportedPaperSizes(PrintDest->currentText(), names, defaultName);
	if (!detected || names.isEmpty())
	{
		names = { QStringLiteral("A3"), QStringLiteral("A4"), QStringLiteral("A2"),
			QStringLiteral("A1"), QStringLiteral("A0"), QStringLiteral("Letter"),
			QStringLiteral("Legal"), QStringLiteral("Tabloid") };
	}
	for (const QString& name : std::as_const(names))
	{
		if (!PrinterUtil::paperSizePoints(name).isValid())
			continue; // a PPD keyword this dialog has no geometry for - offering it would let the user pick a size the live preview and pslib.cpp cannot compute tiles against
		offsetTilePaperCombo->addItem(name, name);
	}
	offsetTilePaperCombo->addItem(tr("Custom"), QStringLiteral("Custom"));

	int idx = offsetTilePaperCombo->findData(previous);
	offsetTilePaperCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	offsetTilePaperCombo->blockSignals(wasBlocked);

	offsetTileFieldChanged();
}

QSizeF PrintDialog::offsetTileCurrentPaperSizePoints() const
{
	QString name = offsetTilePaperCombo->currentData().toString();
	if (name == QStringLiteral("Custom"))
		return QSizeF(mm2pts(offsetTileCustomWidth->value()), mm2pts(offsetTileCustomHeight->value()));
	QSizeF size = PrinterUtil::paperSizePoints(name);
	if (!size.isValid())
		size = PrinterUtil::paperSizePoints(QStringLiteral("A3")); // last-resort fallback so the live preview never shows a blank/degenerate grid
	return size;
}

double PrintDialog::offsetTileCurrentMarginPoints() const
{
	const double fallbackMM = 5.0;
	if (outputToFile())
		return mm2pts(fallbackMM);
	QMarginsF margins;
	if (!PrinterUtil::getPrinterMarginValues(PrintDest->currentText(), offsetTileCurrentPaperSizePoints(), margins))
		return mm2pts(fallbackMM);
	// A single scalar margin for offsetCalculateTileGrid(): the largest of
	// the four, so no edge is ever asked to hold content the printer can't
	// actually image, even on a printer with asymmetric margins.
	return qMax(qMax(margins.left(), margins.right()), qMax(margins.top(), margins.bottom()));
}

OffsetTileOrientation PrintDialog::offsetSelectedOrientation() const
{
	if (offsetTileOrientPortraitRadio->isChecked())
		return OffsetTileOrientation::Portrait;
	if (offsetTileOrientLandscapeRadio->isChecked())
		return OffsetTileOrientation::Landscape;
	return OffsetTileOrientation::Auto;
}

void PrintDialog::offsetTileUpdateInfo()
{
	QSizeF docSize(m_doc->pageWidth(), m_doc->pageHeight());
	QSizeF basePaperSize = offsetTileCurrentPaperSizePoints(); // portrait convention, before orientation
	double margin = offsetTileCurrentMarginPoints();
	double overlap = mm2pts(offsetTileOverlap->value());

	OffsetTileOrientation chosenOrientation = offsetSelectedOrientation();
	OffsetTileOrientation resolvedOrientation = (chosenOrientation == OffsetTileOrientation::Auto)
		? offsetSuggestOrientation(docSize, basePaperSize, margin, overlap)
		: chosenOrientation;
	QSizeF paperSize = offsetOrientedPaperSize(docSize, basePaperSize, chosenOrientation, margin, overlap);

	// Recommended-orientation hint: shown regardless of which radio is
	// selected, so switching to Auto later still tells the operator what it
	// will pick for this document.
	int portraitTiles = offsetCalculateTileGrid(docSize, basePaperSize, margin, overlap).tileCount();
	QSizeF landscapeBase(basePaperSize.height(), basePaperSize.width());
	int landscapeTiles = offsetCalculateTileGrid(docSize, landscapeBase, margin, overlap).tileCount();
	if (offsetTileEnabledCheck->isChecked() && (portraitTiles != landscapeTiles))
	{
		bool landscapeWins = (landscapeTiles < portraitTiles);
		offsetTileOrientRecommendedLabel->setText(tr("Recommended: %1 (%2 tiles vs %3)")
			.arg(landscapeWins ? tr("Landscape") : tr("Portrait"))
			.arg(qMin(portraitTiles, landscapeTiles))
			.arg(qMax(portraitTiles, landscapeTiles)));
	}
	else
		offsetTileOrientRecommendedLabel->setText(QString());

	OffsetTileGrid grid = offsetCalculateTileGrid(docSize, paperSize, margin, overlap);
	bool tilingActive = offsetTileEnabledCheck->isChecked() && !grid.isSingleSheet();
	int tileCount = tilingActive ? grid.tileCount() : 1;

	OffsetOutputMode mode = offsetSelectedOutputMode();
	int enabledPlates = 0;
	if (offsetCyanPrint->isChecked())    enabledPlates++;
	if (offsetMagentaPrint->isChecked()) enabledPlates++;
	if (offsetYellowPrint->isChecked())  enabledPlates++;
	if (offsetBlackPrint->isChecked())   enabledPlates++;
	int totalSheets = offsetTotalSheets(1, tileCount, mode, enabledPlates);

	QString modeText = (mode == OffsetOutputMode::Grayscale) ? tr("Grayscale")
		: (mode == OffsetOutputMode::FullColor) ? tr("Full Color")
		: tr("CMYK Separations");

	QStringList lines;
	lines << tr("Document: %1 × %2 mm").arg(pts2mm(docSize.width()), 0, 'f', 1).arg(pts2mm(docSize.height()), 0, 'f', 1);
	lines << tr("Paper:    %1 × %2 mm (%3)").arg(pts2mm(paperSize.width()), 0, 'f', 1).arg(pts2mm(paperSize.height()), 0, 'f', 1)
		.arg((resolvedOrientation == OffsetTileOrientation::Landscape) ? tr("Landscape") : tr("Portrait"));
	if (tilingActive)
		lines << tr("Tiles:    %1 × %2 = %3 per plate").arg(grid.cols).arg(grid.rows).arg(tileCount);
	else if (offsetTileEnabledCheck->isChecked())
		lines << tr("Tiles:    not needed - document fits the selected paper");
	else
		lines << tr("Tiles:    auto-tiling is off");
	lines << tr("Mode:     %1").arg(modeText);
	lines << QString();
	lines << tr("Total sheets to print: %1").arg(totalSheets);

	offsetTileInfoLabel->setText(lines.join(QStringLiteral("\n")));

	if (m_offsetTilePreview)
	{
		m_offsetTilePreview->setGrid(tilingActive ? grid : OffsetTileGrid(),
			QSizeF(pts2mm(docSize.width()), pts2mm(docSize.height())));
	}
}

void PrintDialog::offsetTileFieldChanged()
{
	bool enabled = offsetTileEnabledCheck->isChecked();
	bool isCustom = offsetTilePaperCombo->currentData().toString() == QStringLiteral("Custom");
	offsetTilePaperCombo->setEnabled(enabled);
	offsetTileCustomWidth->setEnabled(enabled && isCustom);
	offsetTileCustomHeight->setEnabled(enabled && isCustom);
	offsetTileOrientationGroup->setEnabled(enabled);
	offsetTileOverlap->setEnabled(enabled);
	offsetTileRegMarksCheck->setEnabled(enabled);
	offsetTileCutMarksCheck->setEnabled(enabled);
	offsetTileLabelCheck->setEnabled(enabled);
	offsetTilePrintOrderCombo->setEnabled(enabled);
	offsetTileUpdateInfo();
}

void PrintDialog::offsetTilePaperChanged(int index)
{
	Q_UNUSED(index)
	offsetTileFieldChanged();
}

void PrintDialog::offsetTileStoreValues()
{
	m_doc->Print_Options.offsetTileEnabled = offsetTileEnabledCheck->isChecked();
	m_doc->Print_Options.offsetTilePaperSize = offsetTilePaperCombo->currentData().toString();
	m_doc->Print_Options.offsetTileCustomWidthPts = mm2pts(offsetTileCustomWidth->value());
	m_doc->Print_Options.offsetTileCustomHeightPts = mm2pts(offsetTileCustomHeight->value());
	m_doc->Print_Options.offsetTileOverlapPts = mm2pts(offsetTileOverlap->value());
	m_doc->Print_Options.offsetTileOrientation = offsetTileOrientationName(offsetSelectedOrientation());
	m_doc->Print_Options.offsetTileRegMarks = offsetTileRegMarksCheck->isChecked();
	m_doc->Print_Options.offsetTileCutMarks = offsetTileCutMarksCheck->isChecked();
	m_doc->Print_Options.offsetTileShowLabel = offsetTileLabelCheck->isChecked();
	m_doc->Print_Options.offsetTilePrintOrder = offsetTilePrintOrderCombo->currentText();

	prefs->set("OffsetTilingEnabled", m_doc->Print_Options.offsetTileEnabled);
	prefs->set("OffsetTilePaperSize", m_doc->Print_Options.offsetTilePaperSize);
	prefs->set("OffsetTileCustomWidthPts", m_doc->Print_Options.offsetTileCustomWidthPts);
	prefs->set("OffsetTileCustomHeightPts", m_doc->Print_Options.offsetTileCustomHeightPts);
	// Stored in mm, like the LPI/angle keys above are in their own human
	// units - see PrinterUtil::getDefaultPrintOptions()'s matching mm2pts().
	prefs->set("OffsetTileOverlapMM", offsetTileOverlap->value());
	prefs->set("OffsetTileOrientation", m_doc->Print_Options.offsetTileOrientation);
	prefs->set("OffsetTileRegMarks", m_doc->Print_Options.offsetTileRegMarks);
	prefs->set("OffsetTileCutMarks", m_doc->Print_Options.offsetTileCutMarks);
	prefs->set("OffsetTileShowLabel", m_doc->Print_Options.offsetTileShowLabel);
	prefs->set("OffsetTilePrintOrder", m_doc->Print_Options.offsetTilePrintOrder);
}

void PrintDialog::offsetTileSetStoredValues()
{
	const PrintOptions& opts = m_doc->Print_Options;

	offsetTileEnabledCheck->setChecked(opts.offsetTileEnabled);

	int paperIdx = offsetTilePaperCombo->findData(opts.offsetTilePaperSize);
	offsetTilePaperCombo->setCurrentIndex(paperIdx >= 0 ? paperIdx : 0);

	if (opts.offsetTileCustomWidthPts > 0.0)
		offsetTileCustomWidth->setValue(pts2mm(opts.offsetTileCustomWidthPts));
	if (opts.offsetTileCustomHeightPts > 0.0)
		offsetTileCustomHeight->setValue(pts2mm(opts.offsetTileCustomHeightPts));

	offsetTileOverlap->setValue(pts2mm(opts.offsetTileOverlapPts));

	bool okOrient = false;
	OffsetTileOrientation orientation = offsetTileOrientationFromName(opts.offsetTileOrientation, &okOrient);
	if (!okOrient)
		orientation = OffsetTileOrientation::Auto;
	if (orientation == OffsetTileOrientation::Portrait)
		offsetTileOrientPortraitRadio->setChecked(true);
	else if (orientation == OffsetTileOrientation::Landscape)
		offsetTileOrientLandscapeRadio->setChecked(true);
	else
		offsetTileOrientAutoRadio->setChecked(true);

	offsetTileRegMarksCheck->setChecked(opts.offsetTileRegMarks);
	offsetTileCutMarksCheck->setChecked(opts.offsetTileCutMarks);
	offsetTileLabelCheck->setChecked(opts.offsetTileShowLabel);

	bool ok = false;
	OffsetTilePrintOrder order = offsetTilePrintOrderFromName(opts.offsetTilePrintOrder, &ok);
	int orderIdx = offsetTilePrintOrderCombo->findText(offsetTilePrintOrderName(ok ? order : OffsetTilePrintOrder::PlateFirst));
	if (orderIdx >= 0)
		offsetTilePrintOrderCombo->setCurrentIndex(orderIdx);

	offsetTileFieldChanged();
}
