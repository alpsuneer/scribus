/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFSMARTIMPORTOPTIONS_H
#define PDFSMARTIMPORTOPTIONS_H

#include <QDialog>

#include "importpdfsmart.h"

namespace Ui {
class PdfSmartImportOptions;
}

//! \brief The Smart PDF Import dialog: file/page picker, the 8 feature
//! checkboxes, font/script handling combos, and the import target.
//! All checkboxes default to checked (see the .ui file) -- the operator
//! opts out of a feature rather than into it.
class PdfSmartImportOptions : public QDialog
{
	Q_OBJECT

public:
	explicit PdfSmartImportOptions(QWidget* parent = nullptr);
	~PdfSmartImportOptions() override;

	//! Read back everything the operator chose.
	PdfSmartImportSettings settings() const;

private slots:
	void onBrowseClicked();
	void onAllPagesToggled(bool checked);
	void onOkButtonClicked();

private:
	Ui::PdfSmartImportOptions* ui { nullptr };
};

#endif // PDFSMARTIMPORTOPTIONS_H
