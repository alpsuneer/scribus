/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMPORTPDFSMART_H
#define IMPORTPDFSMART_H

#include <QObject>
#include <QString>

class ScribusDoc;

//! \brief Everything the operator chose in the Smart PDF Import dialog for
//! one import run.
struct PdfSmartImportSettings
{
	QString fileName;
	QString pageRange { QStringLiteral("*") };

	bool importTextAsFrames { true };
	bool preserveTextFormatting { true };
	bool recreateTextFlow { true };
	bool detectColumnsAutomatically { true };
	bool importImages { true };
	bool preserveVectorObjects { true };
	bool convertEmbeddedFonts { true };
	bool malayalamIndicCorrection { true };

	QString fontSubstitution { QStringLiteral("Auto") };
	QString scriptDetection { QStringLiteral("Auto") };
	bool importIntoNewDocument { true };
};

//! \brief Smart PDF importer: rebuilds PDF content as editable Scribus
//! objects (text frames, image frames, vector shapes) instead of the
//! standard importer's flattened output.
//!
//! This is a separate, explicitly user-chosen import path -- see
//! importpdfsmartplugin.h for why it does not touch plugins/import/pdf.
//! Session 2 implements Feature 1 (text as editable text frames, one frame
//! per page) and a coarse Feature 2 (page-dominant font name/size only, no
//! per-run styling, no color). Images, vectors, column detection, text
//! flow, real font conversion and Malayalam correction are later sessions
//! (see NOTES.md) -- their checkboxes are accepted by the dialog but have
//! no effect yet.
class ImportPdfSmart : public QObject
{
	Q_OBJECT

public:
	explicit ImportPdfSmart(QObject* parent = nullptr);
	~ImportPdfSmart() override = default;

	//! Show the options dialog and, if accepted, perform the import.
	//! \retval bool true if an import was performed, false if the operator
	//! cancelled, gave no file, or the import could not be completed.
	bool run();

private:
	bool importWithSettings(const PdfSmartImportSettings& settings, ScribusDoc* doc);
};

#endif // IMPORTPDFSMART_H
