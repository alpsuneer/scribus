/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <memory>

#include "scpaths.h"
#include "scprintengine_ps.h"
#include "scribusstructs.h"
#include "scribusdoc.h"
#include "util_printer.h"
#include "scribuscore.h"
#include "pslib.h"
#include "util_file.h"
#include "util_ghostscript.h"

ScPrintEngine_PS::ScPrintEngine_PS(ScribusDoc& doc)
	: ScPrintEngine(doc)
{

}

bool ScPrintEngine_PS::print(PrintOptions& options)
{
	QString filename(options.filename);

	std::unique_ptr<PSLib> psLib(new PSLib(&m_doc, options, PSLib::OutputPS));
	if (!psLib)
		return false;

	if (!options.toFile)
		filename = ScPaths::tempFileDir() + "/tmp.ps";

	// Write the PS to a file
	filename = QDir::toNativeSeparators(filename);

	int psCreationRetVal = psLib->createPS(filename);
	if (psCreationRetVal != 0)
	{
		QFile::remove(filename);
		if (psCreationRetVal == 2)
			return true;
		m_errorMessage = psLib->errorMessage();
		return false;
	}
	if (options.prnLanguage != PrintLanguage::PostScript3 && ScCore->haveGS())
	{
		// use gs to convert our PS to a lower version
		QString tmp;
		QStringList opts;
		opts.append( QString("-dDEVICEWIDTHPOINTS=%1").arg(tmp.setNum(m_doc.pageWidth())) );
		opts.append( QString("-dDEVICEHEIGHTPOINTS=%1").arg(tmp.setNum(m_doc.pageHeight())) );
		convertPS2PS(filename, filename + ".tmp", opts, (int) options.prnLanguage);
		moveFile(filename + ".tmp", filename);
	}
	if (options.toFile)
		return true;

	// Print and delete the PS file
	QByteArray cmd;
	if (options.useAltPrintCommand)
	{
		cmd += options.printerCommand.toLocal8Bit();
		cmd += " ";
		cmd += "\"" + filename.toLocal8Bit() + "\"";
		system(cmd.data());
	}
	else
	{
		QStringList cupsOptions;
		// Page size. Production keeps asking for the document's own size so the
		// output is 1:1. A proof goes onto whatever sheet the proof printer
		// actually holds: ask for that media and let fit-to-page scale the
		// broadsheet down proportionally and centred (it never crops or
		// stretches). If the printer reports a size we do not recognise we fall
		// back to A4, which is what the proof dialog reports too.
		QString proofMedia = options.proofMedia;
		if (options.isProofPrint && proofMedia.isEmpty())
			PrinterUtil::getDefaultPaperSize(options.printer, proofMedia);
		if (options.isProofPrint)
		{
			cupsOptions << "media=" + (proofMedia.isEmpty() ? QStringLiteral("A4") : proofMedia);
			// Pull from the tray the user picked, so the sheet that comes out is
			// the one they chose the paper size for.
			if (!options.inputSlot.isEmpty())
				cupsOptions << "InputSlot=" + options.inputSlot;
		}
		else
		{
			double pw = m_doc.pageWidth()  / 2.8346456693;
			double ph = m_doc.pageHeight() / 2.8346456693;
			cupsOptions << QStringLiteral("media=Custom.%1x%2mm").arg((int) pw).arg((int) ph);
		}
		cupsOptions << QStringLiteral("fit-to-page=true");
		if (options.isProofPrint)
		{
			cupsOptions << QStringLiteral("print-quality=draft");
			cupsOptions << QStringLiteral("Resolution=150dpi");
		}
		// No shell: lpr (or lp when lpr is not installed) gets an argument
		// list, and a failure is reported with the queue's real state. The old
		// system("lpr ...") call discarded its status, so a queue that was
		// missing, disabled or rejecting jobs -- or a PC without lpr at all --
		// reported success and produced nothing.
		QString sendError;
		if (!PrinterUtil::sendToQueue(options.printer, options.copies, cupsOptions,
		                              options.printerOptions, filename, sendError))
		{
			m_errorMessage = sendError;
			return false;
		}
	}
	// Disabled that for now, as kprinter won't work otherwise
	// leaving that file around doesn't harm, as it will be overwritten the next time.
	// unlink(filename);

	return true;
}
