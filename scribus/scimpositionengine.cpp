/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimpositionengine.h"

#include "scpaths.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

namespace {

double ptToMm(double pt) { return pt * 25.4 / 72.0; }

// Small epsilon: floating point mm/pt round-tripping should never flag an
// exact-fit page as overflowing.
const double OverflowEpsilonMm = 0.01;

//! Ghostscript colorant name -> the lowercase token used in the CTP file
//! naming convention. Order here is Cyan/Magenta/Yellow/Black -- also the
//! order ScImpositionEngine::sendToCtp() returns paths in.
struct InkMapping { const char* separationName; const char* shortName; };
const InkMapping CtpInks[] = {
	{ "Cyan", "cyan" }, { "Magenta", "magenta" }, { "Yellow", "yellow" }, { "Black", "black" }
};

} // namespace

double ScImpositionEngine::defaultSheetWidthMm()
{
	QSettings settings("Faircode", "CTPImposition");
	return settings.value("plateWidthMm", 700.0).toDouble();
}

double ScImpositionEngine::defaultSheetHeightMm()
{
	QSettings settings("Faircode", "CTPImposition");
	return settings.value("plateHeightMm", 576.0).toDouble();
}

int ScImpositionEngine::defaultResolutionXDpi()
{
	QSettings settings("Faircode", "CTPImposition");
	return settings.value("plateResolutionXDpi", 1200).toInt();
}

int ScImpositionEngine::defaultResolutionYDpi()
{
	QSettings settings("Faircode", "CTPImposition");
	return settings.value("plateResolutionYDpi", 1200).toInt();
}

// Placement is sequential: the right (or bottom) page is positioned directly
// against the left (or top) page's own rendered edge plus the gutter, so
// gutterMm=0 means the pages actually touch, regardless of how their native
// sizes compare to the sheet. (Never centered independently in a fixed
// half-sheet slot -- that was tried and left dead slot space that looked
// like an extra gutter even at gutterMm=0.)
ScImpositionEngine::PlacedPage ScImpositionEngine::computeLeftPlacement(double pageWmm, double pageHmm, const ImpositionSettings& s)
{
	PlacedPage p;
	if (pageWmm <= 0.0 || pageHmm <= 0.0)
		return p;
	p.pageW = pageWmm;
	p.pageH = pageHmm;
	p.pageX = s.marginLeftMm;
	p.pageY = s.marginTopMm;

	double availW = s.sheetWidthMm - s.marginLeftMm - s.marginRightMm;
	double availH = s.sheetHeightMm - s.marginTopMm - s.marginBottomMm;
	// "Slot" here is the whole available area: the left/top page has first
	// claim on it, before the sibling page or the gutter are even considered.
	p.slotX = p.pageX;
	p.slotY = p.pageY;
	p.slotW = availW;
	p.slotH = availH;
	p.overflows = (p.pageW > availW + OverflowEpsilonMm) || (p.pageH > availH + OverflowEpsilonMm);
	return p;
}

ScImpositionEngine::PlacedPage ScImpositionEngine::computeRightPlacement(double leftPageWmm, double leftPageHmm, double rightPageWmm, double rightPageHmm, const ImpositionSettings& s)
{
	PlacedPage leftP = computeLeftPlacement(leftPageWmm, leftPageHmm, s);

	PlacedPage p;
	if (rightPageWmm <= 0.0 || rightPageHmm <= 0.0)
		return p;
	p.pageW = rightPageWmm;
	p.pageH = rightPageHmm;

	double availW = s.sheetWidthMm - s.marginLeftMm - s.marginRightMm;
	double availH = s.sheetHeightMm - s.marginTopMm - s.marginBottomMm;

	if (s.landscape)
	{
		p.pageX = leftP.pageX + leftP.pageW + s.gutterMm;
		p.pageY = s.marginTopMm;
		p.slotX = p.pageX;
		p.slotY = p.pageY;
		// Remaining width after the left page and the gutter have claimed theirs.
		p.slotW = availW - (leftP.pageW + s.gutterMm);
		p.slotH = availH;
	}
	else
	{
		p.pageX = s.marginLeftMm;
		p.pageY = leftP.pageY + leftP.pageH + s.gutterMm;
		p.slotX = p.pageX;
		p.slotY = p.pageY;
		p.slotW = availW;
		p.slotH = availH - (leftP.pageH + s.gutterMm);
	}
	p.overflows = (p.pageW > p.slotW + OverflowEpsilonMm) || (p.pageH > p.slotH + OverflowEpsilonMm);
	return p;
}

bool ScImpositionEngine::imposeToPdf(const ImpositionSettings& s, const QString& outputPdfPath, QString* errorMessage)
{
	if (s.leftFilePath.isEmpty() || s.rightFilePath.isEmpty())
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Both a left and a right page must be selected.");
		return false;
	}
	if (s.leftPageIndex != 0 || s.rightPageIndex != 0)
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Only page 1 of a PDF can currently be imposed.");
		return false;
	}

	PdfPageInfo leftInfo = scanPdfInfo(s.leftFilePath);
	if (!leftInfo.valid)
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Could not read \"%1\".").arg(s.leftFilePath);
		return false;
	}
	PdfPageInfo rightInfo = scanPdfInfo(s.rightFilePath);
	if (!rightInfo.valid)
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Could not read \"%1\".").arg(s.rightFilePath);
		return false;
	}

	PlacedPage left = computeLeftPlacement(leftInfo.widthMm, leftInfo.heightMm, s);
	PlacedPage right = computeRightPlacement(leftInfo.widthMm, leftInfo.heightMm, rightInfo.widthMm, rightInfo.heightMm, s);
	if (left.overflows || right.overflows)
	{
		if (errorMessage)
		{
			*errorMessage = QObject::tr(
				"The %1 page (%2 x %3 mm) does not fit its %4 x %5 mm slot on the sheet. "
				"Reduce margins/gutter or check the page size -- pages are placed at their "
				"native size, never rescaled.")
				.arg(left.overflows ? QObject::tr("left") : QObject::tr("right"))
				.arg(left.overflows ? left.pageW : right.pageW, 0, 'f', 1)
				.arg(left.overflows ? left.pageH : right.pageH, 0, 'f', 1)
				.arg(left.overflows ? left.slotW : right.slotW, 0, 'f', 1)
				.arg(left.overflows ? left.slotH : right.slotH, 0, 'f', 1);
		}
		return false;
	}

	// Delegates the actual PDF assembly to a Python script (pypdf for page
	// placement, reportlab for plate marks) rather than Scribus's own PDF
	// import + pdflib_core pipeline: that native path (PdfPlug import into a
	// headless scratch ScribusDoc, exported via PDFlib) reliably produced a
	// blank, wrong-sized (A4) plate for reasons that resisted diagnosis --
	// every individual piece of that pipeline (ScribusDoc::setup/setPage/
	// addPage, ScPage's own constructor, PDFOptions, pdflib_core's MediaBox
	// emission) checked out correct on inspection, and pinpointing the actual
	// fault needed a debug-symbol rebuild this install didn't have. The
	// Python script is independently tested against real newspaper-page PDFs
	// (see impose_ctp.py's header) and produces correct, embedded-font
	// output where the native path did not.
	const QString scriptPath = ScPaths::instance().shareDir() + QStringLiteral("imposition/impose_ctp.py");
	if (!QFileInfo::exists(scriptPath))
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Imposition script not found: \"%1\".").arg(scriptPath);
		return false;
	}

	QStringList args;
	args << scriptPath
	     << QStringLiteral("--left") << s.leftFilePath
	     << QStringLiteral("--right") << s.rightFilePath
	     << QStringLiteral("--output") << outputPdfPath
	     << QStringLiteral("--plate-w") << QString::number(s.sheetWidthMm, 'f', 4)
	     << QStringLiteral("--plate-h") << QString::number(s.sheetHeightMm, 'f', 4)
	     << QStringLiteral("--left-margin") << QString::number(s.marginLeftMm, 'f', 4)
	     << QStringLiteral("--top-margin") << QString::number(s.marginTopMm, 'f', 4)
	     << QStringLiteral("--gutter") << QString::number(s.gutterMm, 'f', 4)
	     << QStringLiteral("--print-area") << (s.showPrintArea ? QStringLiteral("1") : QStringLiteral("0"))
	     << QStringLiteral("--regmarks") << (s.showRegmarks ? QStringLiteral("1") : QStringLiteral("0"))
	     << QStringLiteral("--auto-marks") << (s.showAutoMarks ? QStringLiteral("1") : QStringLiteral("0"))
	     << QStringLiteral("--furnitures") << (s.showFurnitures ? QStringLiteral("1") : QStringLiteral("0"))
	     << QStringLiteral("--colour-bar") << (s.showColourBar ? QStringLiteral("1") : QStringLiteral("0"))
	     << QStringLiteral("--pub") << s.pubCode
	     << QStringLiteral("--edition") << s.editionCode
	     << QStringLiteral("--left-page-num") << QString::number(s.leftPageNumber)
	     << QStringLiteral("--right-page-num") << QString::number(s.rightPageNumber);

	QProcess proc;
	proc.start(QStringLiteral("python3"), args);
	// Two source PDFs + reportlab drawing is fast, but give it real headroom
	// rather than reusing scanPdfInfo()'s 5s (a metadata-only `pdfinfo` call).
	if (!proc.waitForFinished(60000))
	{
		proc.kill();
		if (errorMessage)
			*errorMessage = QObject::tr("The imposition script timed out.");
		return false;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
	{
		if (errorMessage)
		{
			QString stderrText = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
			*errorMessage = stderrText.isEmpty()
				? QObject::tr("The imposition script failed (exit code %1).").arg(proc.exitCode())
				: stderrText;
		}
		return false;
	}
	if (!QFileInfo::exists(outputPdfPath))
	{
		if (errorMessage)
			*errorMessage = QObject::tr("The imposition script did not produce an output file.");
		return false;
	}

	return true;
}

ScImpositionEngine::PdfPageInfo ScImpositionEngine::scanPdfInfo(const QString& filePath)
{
	PdfPageInfo info;

	QProcess proc;
	proc.start(QStringLiteral("pdfinfo"), QStringList() << filePath);
	if (!proc.waitForFinished(5000))
	{
		proc.kill();
		return info;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
		return info;

	const QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
	const QStringList lines = output.split(QLatin1Char('\n'));
	for (const QString& line : lines)
	{
		if (line.startsWith(QLatin1String("Pages:")))
		{
			bool ok = false;
			int n = line.mid(6).trimmed().toInt(&ok);
			if (ok)
				info.pageCount = n;
		}
		else if (line.startsWith(QLatin1String("Page size:")))
		{
			// "Page size:      992.13 x 1525.04 pts" -- points, page 1 (or the
			// -f/-l-selected range, unused here since only page 1 is imposable).
			const QStringList parts = line.mid(10).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
			if (parts.count() >= 3)
			{
				bool okW = false, okH = false;
				double wPt = parts.at(0).toDouble(&okW);
				double hPt = parts.at(2).toDouble(&okH);
				if (okW && okH)
				{
					info.widthMm = ptToMm(wPt);
					info.heightMm = ptToMm(hPt);
				}
			}
		}
	}
	info.valid = (info.pageCount > 0) && (info.widthMm > 0.0) && (info.heightMm > 0.0);
	return info;
}

QStringList ScImpositionEngine::sendToCtp(const QString& imposedPdfPath, const ImpositionSettings& s, QString* errorMessage)
{
	if (s.hotFolderPath.isEmpty())
	{
		if (errorMessage)
			*errorMessage = QObject::tr("No hot folder is configured. Set one in Plate Properties.");
		return QStringList();
	}
	QDir hotDir(s.hotFolderPath);
	if (!hotDir.exists())
	{
		if (errorMessage)
			*errorMessage = QObject::tr("The hot folder does not exist: \"%1\".").arg(s.hotFolderPath);
		return QStringList();
	}
	if (!QFileInfo::exists(imposedPdfPath))
	{
		if (errorMessage)
			*errorMessage = QObject::tr("The imposed PDF does not exist: \"%1\".").arg(imposedPdfPath);
		return QStringList();
	}

	// Ghostscript's actual output filenames are its own convention (see
	// below), not something -sOutputFile lets us pin down directly, so this
	// writes into a private scratch directory and renames/copies afterward
	// rather than generating straight into the hot folder.
	QTemporaryDir scratchDir;
	if (!scratchDir.isValid())
	{
		if (errorMessage)
			*errorMessage = QObject::tr("Could not create a temporary directory for the CTP plates.");
		return QStringList();
	}

	// imposeToPdf() always produces a single-page PDF, so Ghostscript's %d
	// always substitutes to "1" -- no need to discover it after the fact.
	const QString gsBaseName = QStringLiteral("plate_1");
	const QString gsOutputTemplate = scratchDir.filePath(QStringLiteral("plate_%d.tif"));

	QStringList args;
	args << QStringLiteral("-dBATCH") << QStringLiteral("-dNOPAUSE") << QStringLiteral("-dSAFER")
	     << QStringLiteral("-sDEVICE=tiffsep1")
	     << QStringLiteral("-r%1x%2").arg(s.resolutionXDpi).arg(s.resolutionYDpi)
	     << QStringLiteral("-sCompression=g4")
	     << QStringLiteral("-sOutputFile=%1").arg(gsOutputTemplate)
	     << imposedPdfPath;

	QProcess gs;
	gs.start(QStringLiteral("gs"), args);
	if (!gs.waitForFinished(180000))
	{
		gs.kill();
		if (errorMessage)
			*errorMessage = QObject::tr("Ghostscript timed out generating the CTP plates.");
		return QStringList();
	}
	if (gs.exitStatus() != QProcess::NormalExit || gs.exitCode() != 0)
	{
		if (errorMessage)
		{
			QString stderrText = QString::fromLocal8Bit(gs.readAllStandardError()).trimmed();
			*errorMessage = stderrText.isEmpty()
				? QObject::tr("Ghostscript failed generating the CTP plates (exit code %1).").arg(gs.exitCode())
				: stderrText;
		}
		return QStringList();
	}

	const QString dateStamp = QDate::currentDate().toString(QStringLiteral("ddMMyy"));
	const QString pub = s.pubCode.isEmpty() ? QStringLiteral("PUB") : s.pubCode;
	const QString edition = s.editionCode.isEmpty() ? QStringLiteral("ED") : s.editionCode;
	const int leftPageNum = s.leftPageNumber;
	const int rightPageNum = s.rightPageNumber;

	QStringList result;
	for (const InkMapping& ink : CtpInks)
	{
		// tiffsep1 names each separation
		// "<template-with-%d-substituted>(<SeparationName>).tif" -- confirmed
		// against a real run; NOT "<template><SeparationName>.tif".
		const QString sourcePath = scratchDir.filePath(QStringLiteral("%1(%2).tif")
			.arg(gsBaseName, QString::fromLatin1(ink.separationName)));
		if (!QFileInfo::exists(sourcePath))
		{
			if (errorMessage)
				*errorMessage = QObject::tr("Ghostscript did not produce the %1 plate.").arg(QString::fromLatin1(ink.separationName));
			return QStringList();
		}

		const QString targetName = QStringLiteral("%1-%2-%3-%4-%5-%6.TIF")
			.arg(dateStamp, pub)
			.arg(leftPageNum).arg(rightPageNum)
			.arg(edition, QString::fromLatin1(ink.shortName));
		const QString targetPath = hotDir.filePath(targetName);

		QFile::remove(targetPath); // QFile::copy() fails outright if the destination already exists
		if (!QFile::copy(sourcePath, targetPath))
		{
			if (errorMessage)
				*errorMessage = QObject::tr("Could not copy \"%1\" to the hot folder.").arg(targetName);
			return QStringList();
		}
		result << targetPath;
	}

	return result;
}
