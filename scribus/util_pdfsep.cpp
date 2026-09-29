/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "util_pdfsep.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>

#include "cmsettings.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "prefstable.h"
#include "scimage.h"
#include "scpaths.h"
#include "util.h"
#include "util_ghostscript.h"

bool pdfSepGetInfo(const QString& pdfPath, int pageNumber, PDFSepInfo& info)
{
	info = PDFSepInfo();

	QProcess proc;
	QStringList args { "-f", QString::number(pageNumber), "-l", QString::number(pageNumber), pdfPath };
	qDebug() << "[pdfsep] pdfinfo command:" << "pdfinfo" << args;
	proc.start(QStringLiteral("pdfinfo"), args);
	if (!proc.waitForStarted(10000))
	{
		info.errorMessage = QObject::tr("Could not run pdfinfo (poppler-utils not installed?)");
		qDebug() << "[pdfsep] pdfinfo failed to start:" << info.errorMessage;
		return false;
	}
	proc.waitForFinished(20000);
	qDebug() << "[pdfsep] pdfinfo exit code:" << proc.exitCode();
	if (proc.exitCode() != 0)
	{
		info.errorMessage = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
		qDebug() << "[pdfsep] pdfinfo stderr:" << info.errorMessage;
		if (info.errorMessage.isEmpty())
			info.errorMessage = QObject::tr("pdfinfo failed to read \"%1\"").arg(pdfPath);
		return false;
	}

	const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
	qDebug() << "[pdfsep] pdfinfo stdout:" << out;
	static const QRegularExpression pagesRe(QStringLiteral("^Pages:\\s*(\\d+)"), QRegularExpression::MultilineOption);
	static const QRegularExpression sizeRe(QStringLiteral("^Page size:\\s*([\\d.]+)\\s*x\\s*([\\d.]+)\\s*pts"), QRegularExpression::MultilineOption);

	auto pagesMatch = pagesRe.match(out);
	if (pagesMatch.hasMatch())
		info.pageCount = pagesMatch.captured(1).toInt();

	auto sizeMatch = sizeRe.match(out);
	if (sizeMatch.hasMatch())
		info.pageSizePts = QSizeF(sizeMatch.captured(1).toDouble(), sizeMatch.captured(2).toDouble());

	if (info.pageCount <= 0)
	{
		info.errorMessage = QObject::tr("Could not determine page count for \"%1\"").arg(pdfPath);
		return false;
	}

	info.valid = true;
	return true;
}

QImage pdfSepLoadPlateImage(const QString& tiffPath)
{
	CMSettings cms(nullptr, QString(), Intent_Relative_Colorimetric);
	cms.allowColorManagement(false);

	ScImage im;
	bool realCMYK = false;
	if (!im.loadPicture(tiffPath, 1, cms, ScImage::RGBData, 72, &realCMYK))
	{
		qDebug() << "[pdfsep] ScImage::loadPicture failed for" << tiffPath;
		return QImage();
	}

	return im.qImage().convertToFormat(QImage::Format_Grayscale8);
}

// Ghostscript's tiffsep device writes ordinary displayable grayscale
// separations: 255 = white paper (no ink for that plate), 0 = fully inked.
// Ink coverage is therefore the *inverse* of the raw sample value, scaled
// from 0..255 to 0..100. (Verified against a synthetic test PDF with known
// solid CMYK fills: a 90%-black fill measured at pixel value ~25, matching
// (1 - 0.90) * 255.)
static double averagePlateCoverage(const QString& tiffPath)
{
	QImage img = pdfSepLoadPlateImage(tiffPath);
	if (img.isNull())
		return 0.0;

	quint64 sum = 0;
	const int w = img.width();
	const int h = img.height();
	for (int y = 0; y < h; ++y)
	{
		const uchar* line = img.constScanLine(y);
		for (int x = 0; x < w; ++x)
			sum += (255 - line[x]);
	}
	const quint64 pixelCount = quint64(w) * quint64(h);
	if (pixelCount == 0)
		return 0.0;
	return (double(sum) / double(pixelCount)) * (100.0 / 255.0);
}

bool pdfSepGenerate(const QString& pdfPath, int pageNumber, int resolutionDPI, const QString& tempDir, PDFSepResult& result, const bool* cancel)
{
	result = PDFSepResult();

	QDir dir(tempDir);
	if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
	{
		result.errorMessage = QObject::tr("Could not create temporary directory \"%1\"").arg(tempDir);
		return false;
	}

	const QString baseName = QStringLiteral("pdfsep_page%1").arg(pageNumber);
	const QString outputFile = dir.filePath(baseName + QStringLiteral(".tif"));

	// Ghostscript names each plate file after the base output name. Older
	// releases append ".Name.tif"; 9.06+ uses "(Name).tif" instead (see the
	// identical version gate in PrintPreviewCreator_PS::renderPreviewSep()).
	int gsVersion = 0;
	getNumericGSVersion(gsVersion);
	const QStringList plateNames { QStringLiteral("Cyan"), QStringLiteral("Magenta"), QStringLiteral("Yellow"), QStringLiteral("Black") };
	auto plateFilePath = [&](const QString& name) {
		if (gsVersion > 0 && gsVersion <= 905)
			return dir.filePath(baseName + "." + name + ".tif");
		return dir.filePath(baseName + "(" + name + ").tif");
	};

	// Clean up any leftovers from a previous run against this same page
	// number before Ghostscript writes new ones.
	QFile::remove(outputFile);
	for (const QString& name : plateNames)
		QFile::remove(plateFilePath(name));

	// Built by hand rather than through callGS(): that helper always appends
	// a trailing "-c showpage", which after a single already-complete PDF
	// page counts as a *second* page. tiffsep refuses outright to write a
	// second page to this fixed (non-%d) output filename -- "Unrecoverable
	// error, exit code 1" -- so the extra showpage must not happen here.
	PrefsManager& prefsManager = PrefsManager::instance();
	QStringList args;
	args.append(QStringLiteral("-q"));
	args.append(QStringLiteral("-dNOPAUSE"));
	args.append(QStringLiteral("-dQUIET"));
	args.append(QStringLiteral("-dPARANOIDSAFER"));
	args.append(QStringLiteral("-dBATCH"));
	args.append(QStringLiteral("-sDEVICE=tiffsep"));
	if (prefsManager.appPrefs.extToolPrefs.gs_AntiAliasText)
		args.append(QStringLiteral("-dTextAlphaBits=4"));
	if (prefsManager.appPrefs.extToolPrefs.gs_AntiAliasGraphics)
		args.append(QStringLiteral("-dGraphicsAlphaBits=4"));

	PrefsContext* pc = prefsManager.prefsFile->getContext("Fonts");
	PrefsTable* extraFonts = pc->getTable("ExtraFontDirs");
	const char sep = ScPaths::envPathSeparator;
	QString fontPathArg;
	if (extraFonts->getRowCount() >= 1)
		fontPathArg = QString("-sFONTPATH=%1").arg(QDir::toNativeSeparators(extraFonts->get(0, 0)));
	for (int i = 1; i < extraFonts->getRowCount(); ++i)
		fontPathArg += QString("%1%2").arg(sep).arg(QDir::toNativeSeparators(extraFonts->get(i, 0)));
	if (!fontPathArg.isEmpty())
		args.append(fontPathArg);

	args.append(QString("-r%1").arg(resolutionDPI));
	args.append(QString("-dFirstPage=%1").arg(pageNumber));
	args.append(QString("-dLastPage=%1").arg(pageNumber));
	args.append(QString("-sOutputFile=%1").arg(QDir::toNativeSeparators(outputFile)));
	args.append(QDir::toNativeSeparators(pdfPath));

	const QString gsExe = getShortPathName(prefsManager.ghostscriptExecutable());
	const QString gsLog = dir.filePath(baseName + QStringLiteral(".gs.log"));
	qDebug() << "[pdfsep] GS command:" << gsExe << args;
	int ret = System(gsExe, args, gsLog, QString(), cancel);
	qDebug() << "[pdfsep] GS exit code:" << ret;
	if (ret == -1 && cancel && *cancel)
	{
		// Cancelled by the caller: System() already killed the process. No
		// errorMessage -- this isn't a failure, and the caller distinguishes
		// "cancelled" from "failed" via *cancel, not by message content.
		qDebug() << "[pdfsep] GS render cancelled by caller";
		return false;
	}
	{
		QFile logFile(gsLog);
		if (logFile.open(QIODevice::ReadOnly))
		{
			const QByteArray log = logFile.readAll();
			qDebug() << "[pdfsep] GS stderr:" << log;
			if (ret != 0)
				result.errorMessage = QString::fromLocal8Bit(log).trimmed();
		}
	}
	if (ret != 0)
	{
		if (result.errorMessage.isEmpty())
			result.errorMessage = QObject::tr("Ghostscript failed to render separations for \"%1\"").arg(pdfPath);
		return false;
	}

	if (!QFileInfo::exists(outputFile))
	{
		result.errorMessage = QObject::tr("Ghostscript did not produce a composite preview for \"%1\"").arg(pdfPath);
		qDebug() << "[pdfsep] expected composite missing:" << outputFile;
		return false;
	}
	result.compositeTiffPath = outputFile;
	qDebug() << "[pdfsep] composite written:" << outputFile << QFileInfo(outputFile).size() << "bytes";

	double totalCoverage = 0.0;
	for (const QString& name : plateNames)
	{
		const QString platePath = plateFilePath(name);
		if (!QFileInfo::exists(platePath))
		{
			result.errorMessage = QObject::tr("Ghostscript did not produce the %1 separation").arg(name);
			return false;
		}
		PDFSepPlate plate;
		plate.name = name;
		plate.tiffPath = platePath;
		plate.coveragePercent = averagePlateCoverage(platePath);
		qDebug() << "[pdfsep] plate" << name << platePath << QFileInfo(platePath).size() << "bytes, coverage" << plate.coveragePercent << "%";
		totalCoverage += plate.coveragePercent;
		result.plates.append(plate);
	}
	result.totalCoveragePercent = totalCoverage;

	result.ok = true;
	return true;
}
