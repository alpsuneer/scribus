/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartimportoptions.h"
#include "ui_pdfsmartimportoptions.h"

#include "commonstrings.h"
#include "ui/scmessagebox.h"

#include <QFileDialog>
#include <QPushButton>

PdfSmartImportOptions::PdfSmartImportOptions(QWidget* parent) : QDialog(parent), ui(new Ui::PdfSmartImportOptions)
{
	ui->setupUi(this);

	if (QPushButton* okButton = ui->buttonBox->button(QDialogButtonBox::Ok))
		okButton->setText(tr("Import"));

	connect(ui->browseButton, SIGNAL(clicked()), this, SLOT(onBrowseClicked()));
	connect(ui->allPagesCheck, SIGNAL(toggled(bool)), this, SLOT(onAllPagesToggled(bool)));
	connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onOkButtonClicked()));
	connect(ui->buttonBox, SIGNAL(rejected()), this, SLOT(reject()));

	onAllPagesToggled(ui->allPagesCheck->isChecked());
}

PdfSmartImportOptions::~PdfSmartImportOptions()
{
	delete ui;
}

void PdfSmartImportOptions::onBrowseClicked()
{
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open PDF File"), QString(), tr("PDF Files (*.pdf)"));
	if (!fileName.isEmpty())
		ui->fileEdit->setText(fileName);
}

void PdfSmartImportOptions::onAllPagesToggled(bool checked)
{
	ui->pageRangeEdit->setEnabled(!checked);
}

void PdfSmartImportOptions::onOkButtonClicked()
{
	if (ui->fileEdit->text().trimmed().isEmpty())
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Please choose a PDF file to import."));
		return;
	}

	accept();
}

PdfSmartImportSettings PdfSmartImportOptions::settings() const
{
	PdfSmartImportSettings s;
	s.fileName  = ui->fileEdit->text();
	s.pageRange = ui->allPagesCheck->isChecked() ? QStringLiteral("*") : ui->pageRangeEdit->text();

	s.importTextAsFrames         = ui->textFramesCheck->isChecked();
	s.preserveTextFormatting     = ui->textFormattingCheck->isChecked();
	s.recreateTextFlow           = ui->textFlowCheck->isChecked();
	s.detectColumnsAutomatically = ui->columnDetectCheck->isChecked();
	s.importImages               = ui->imagesCheck->isChecked();
	s.preserveVectorObjects      = ui->vectorCheck->isChecked();
	s.convertEmbeddedFonts       = ui->embeddedFontsCheck->isChecked();
	s.malayalamIndicCorrection   = ui->malayalamCheck->isChecked();

	s.fontSubstitution = ui->fontSubstitutionCombo->currentText();
	s.scriptDetection  = ui->scriptDetectionCombo->currentText();
	s.importIntoNewDocument = (ui->targetDocCombo->currentIndex() == 0);

	return s;
}
