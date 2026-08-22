/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "util_printer.h"
#include "scconfig.h"
#include <QProcess>
#include <QPrinterInfo>
#include <QPrinter>
#include <QPageLayout>

#if defined(_WIN32)
 #include <Windows.h>
 #include <winspool.h>
#endif

#include <QStringList>
#include <QDataStream>
#include <QByteArray>

#include "commonstrings.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "scribuscore.h"
#include "util_os.h"

void PrinterUtil::getDefaultPrintOptions(PrintOptions& options, const MarginStruct& docBleeds)
{
	PrefsManager& prefsManager = PrefsManager::instance();
	PrefsContext *prnPrefs = prefsManager.prefsFile->getContext("print_options");

	options.firstUse = true;
	options.printer  = prnPrefs->get("CurrentPrn", QString());
	options.useAltPrintCommand = prnPrefs->getBool("OtherCom", false);
	options.printerCommand = prnPrefs->get("Command", QString());
	options.outputSeparations = prnPrefs->getInt("Separations", 0);
	options.useColor = (prnPrefs->getInt("PrintColor", 0) == 0);
	QStringList spots { "All" , "Cyan", "Magenta", "Yellow", "Black" };
	int selectedSep  = prnPrefs->getInt("SepArt", 0);
	if ((selectedSep < 0) || (selectedSep > 4))
		selectedSep = 0;
	options.separationName = spots.at(selectedSep);
	if (prnPrefs->contains("PrintLanguage"))
		options.prnLanguage = (PrintLanguage) prnPrefs->getInt("PrintLanguage", (int) PrinterUtil::getDefaultPrintLanguage(options.printer, false));
	else
		options.prnLanguage = (PrintLanguage) prnPrefs->getInt("PSLevel", (int) PrintLanguage::PostScript3);
	options.mirrorH = prnPrefs->getBool("MirrorH", false);
	options.mirrorV = prnPrefs->getBool("MirrorV", false);
	options.setDevParam = prnPrefs->getBool("doDev", false);
	options.doGCR   = prnPrefs->getBool("DoGCR", prefsManager.appPrefs.printerPrefs.GCRMode);
	options.doClip  = prnPrefs->getBool("Clip", false);
	options.useSpotColors = prnPrefs->getBool("doSpot", true);
	options.useDocBleeds  = true;
	options.bleeds = docBleeds;
	options.markLength = prnPrefs->getDouble("markLength", 20.0);
	options.markOffset = prnPrefs->getDouble("markOffset", 0.0);
	options.cropMarks  = prnPrefs->getBool("cropMarks", false);
	options.bleedMarks = prnPrefs->getBool("bleedMarks", false);
	options.registrationMarks = prnPrefs->getBool("registrationMarks", false);
	options.colorMarks = prnPrefs->getBool("colorMarks", false);
	options.includePDFMarks = prnPrefs->getBool("includePDFMarks", true);
}

QString PrinterUtil::getDefaultPrinterName()
{
	return QPrinterInfo::defaultPrinterName();
}

QStringList PrinterUtil::getPrinterNames()
{
	return QPrinterInfo::availablePrinterNames();
}

bool PrinterUtil::getDefaultPaperSize(const QString& printerName, QString& mediaName, QSizeF* sizePoints)
{
	if (printerName.isEmpty())
		return false;
	QPrinterInfo pInfo = QPrinterInfo::printerInfo(printerName);
	if (pInfo.isNull())
		return false;

	QPageSize pageSize = pInfo.defaultPageSize();
	if (!pageSize.isValid())
		return false;

	// A named standard size gives a media keyword lpr understands directly.
	// Anything custom or unrecognised is reported as unknown so the caller can
	// fall back and tell the user, rather than sending a size the printer will
	// silently reinterpret.
	QString name;
	switch (pageSize.id())
	{
		case QPageSize::A3:     name = "A3"; break;
		case QPageSize::A4:     name = "A4"; break;
		case QPageSize::A5:     name = "A5"; break;
		case QPageSize::B4:     name = "B4"; break;
		case QPageSize::B5:     name = "B5"; break;
		case QPageSize::Letter: name = "Letter"; break;
		case QPageSize::Legal:  name = "Legal"; break;
		case QPageSize::Tabloid: name = "Tabloid"; break;
		default: break;
	}
	if (name.isEmpty())
		return false;

	mediaName = name;
	if (sizePoints)
		*sizePoints = pageSize.size(QPageSize::Point);
	return true;
}


// Ask cupsd (not the PPD file, which the desktop user cannot read) for one of
// the option lists lpoptions reports, e.g. "PageSize" or "InputSlot". Returns
// the offered values; the queue's default is the one marked with '*'.
static bool scQueryPrinterOptionList(const QString& printerName, const QString& optionKey,
                                     QStringList& values, QString& defaultValue)
{
	values.clear();
	defaultValue.clear();
	if (printerName.isEmpty())
		return false;

	QProcess proc;
	proc.start("lpoptions", QStringList() << "-p" << printerName << "-l");
	if (!proc.waitForFinished(4000))
	{
		proc.kill();
		proc.waitForFinished(1000);
		return false;
	}
	const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());

	// Each line looks like:  PageSize/Media Size: A4 *A3 A5 ...
	const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	for (const QString& line : lines)
	{
		int slash = line.indexOf(QLatin1Char('/'));
		int colon = line.indexOf(QLatin1Char(':'));
		if ((slash < 0) || (colon < slash))
			continue;
		if (line.left(slash).trimmed() != optionKey)
			continue;
		const QStringList tokens = line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (const QString& token : tokens)
		{
			QString value = token.trimmed();
			bool isDefault = value.startsWith(QLatin1Char('*'));
			if (isDefault)
				value.remove(0, 1);
			if (value.isEmpty())
				continue;
			values.append(value);
			if (isDefault)
				defaultValue = value;
		}
		break;
	}
	return !values.isEmpty();
}

bool PrinterUtil::getSupportedPaperSizes(const QString& printerName, QStringList& names, QString& defaultName)
{
	return scQueryPrinterOptionList(printerName, QStringLiteral("PageSize"), names, defaultName);
}

bool PrinterUtil::getInputSlots(const QString& printerName, QStringList& traySlots, QString& defaultSlot)
{
	return scQueryPrinterOptionList(printerName, QStringLiteral("InputSlot"), traySlots, defaultSlot);
}

QSizeF PrinterUtil::paperSizePoints(const QString& mediaName)
{
	// Only the sizes a proof is ever scaled onto need to be resolvable here;
	// anything else is reported as unknown so the caller can fall back rather
	// than invent a geometry.
	static const QHash<QString, QPageSize::PageSizeId> sizeIds {
		{ QStringLiteral("A3"),      QPageSize::A3 },
		{ QStringLiteral("A4"),      QPageSize::A4 },
		{ QStringLiteral("A5"),      QPageSize::A5 },
		{ QStringLiteral("A6"),      QPageSize::A6 },
		{ QStringLiteral("B4"),      QPageSize::B4 },
		{ QStringLiteral("B5"),      QPageSize::B5 },
		{ QStringLiteral("Letter"),  QPageSize::Letter },
		{ QStringLiteral("Legal"),   QPageSize::Legal },
		{ QStringLiteral("Tabloid"), QPageSize::Tabloid },
		{ QStringLiteral("Executive"), QPageSize::Executive },
	};
	auto it = sizeIds.constFind(mediaName);
	if (it == sizeIds.constEnd())
		return QSizeF();
	return QPageSize(it.value()).size(QPageSize::Point);
}

#if defined(_WIN32)
bool PrinterUtil::getDefaultSettings(QString printerName, QByteArray& devModeA)
{
	bool done;
	uint size;
	LONG result = IDOK+1;
	Qt::HANDLE handle = nullptr;
	// Get the printer handle
	done = OpenPrinterW((LPWSTR) printerName.utf16(), &handle, nullptr);
	if (!done)
		return false;
	// Get size of DEVMODE structure (public + private data)
	size = DocumentPropertiesW((HWND) ScCore->primaryMainWindow()->winId(), handle, (LPWSTR) printerName.utf16(), nullptr, nullptr, 0);
	// Allocate the memory needed by the DEVMODE structure
	devModeA.resize(size);
	// Retrieve printer default settings
	result = DocumentPropertiesW((HWND) ScCore->primaryMainWindow()->winId(), handle, (LPWSTR) printerName.utf16(), (DEVMODEW*) devModeA.data(), nullptr, DM_OUT_BUFFER);
	// Free the printer handle
	ClosePrinter(handle);
	return (result == IDOK);
}
#endif

#if defined(_WIN32)
bool PrinterUtil::initDeviceSettings(QString printerName, QByteArray& devModeA)
{
	bool done;
	uint size;
	LONG result = IDOK+1;
	Qt::HANDLE handle = nullptr;
	// Get the printer handle
	done = OpenPrinterW((LPWSTR) printerName.utf16(), &handle, nullptr);
	if (!done)
		return false;
	// Get size of DEVMODE structure (public + private data)
	size = DocumentPropertiesW((HWND) ScCore->primaryMainWindow()->winId(), handle, (LPWSTR) printerName.utf16(), nullptr, nullptr, 0);
	// Compare size with DevMode structure size
	if (devModeA.size() == size)
	{
		// Merge printer settings
		result = DocumentPropertiesW((HWND) ScCore->primaryMainWindow()->winId(), handle, (LPWSTR) printerName.utf16(), (DEVMODEW*) devModeA.data(), (DEVMODEW*) devModeA.data(), DM_IN_BUFFER | DM_OUT_BUFFER);
	}
	else
	{
		// Retrieve default settings
		devModeA.resize(size);
		result = DocumentPropertiesW((HWND) ScCore->primaryMainWindow()->winId(), handle, (LPWSTR) printerName.utf16(), (DEVMODEW*) devModeA.data(), nullptr, DM_OUT_BUFFER);
	}
	done = (result == IDOK);
	// Free the printer handle
	ClosePrinter(handle);
	return done;
}
#endif

bool PrinterUtil::getPrinterMarginValues(const QString& printerName, const QSizeF& pageSize, QMarginsF& margins)
{
	QPrinterInfo pInfo = QPrinterInfo::printerInfo(printerName);
	if (pInfo.isNull())
		return false;

	QPrinter printer(pInfo, QPrinter::HighResolution);
	margins = printer.pageLayout().margins();

	// Unfortunately margin values are not updated when calling QPrinter or QPageLayout's setOrientation()
	// so we have to adapt margin values according to orientation ourselves
	if (pageSize.width() > pageSize.height())
	{
		double l = margins.left();
		double r = margins.right();
		double b = margins.bottom();
		double t = margins.top();
		margins = QMarginsF(b, l, t, r);
	}
	return true;
}

PrintLanguage PrinterUtil::getDefaultPrintLanguage(const QString&  /*printerName*/, bool toFile)
{
	if (!toFile)
	{
#if defined(_WIN32)
		return PrintLanguage::WindowsGDI;
#else
		return PrintLanguage::PostScript3;
#endif
	}
	return PrintLanguage::PostScript3;
}

PrintLanguageMap PrinterUtil::getPrintLanguageSupport(const QString& printerName, bool toFile)
{
	PrintLanguageMap prnMap;
	if (toFile || PrinterUtil::isPostscriptPrinter(printerName))
	{
		if (ScCore->haveGS())
		{
			prnMap.insert(CommonStrings::trPostScript1, PrintLanguage::PostScript1);
			prnMap.insert(CommonStrings::trPostScript2, PrintLanguage::PostScript2);
		}
		prnMap.insert(CommonStrings::trPostScript3, PrintLanguage::PostScript3);
	}
	if (toFile || PrinterUtil::supportsPDF(printerName))
		prnMap.insert(CommonStrings::trPDF, PrintLanguage::PDF);
#if defined(_WIN32)
	if (!toFile)
		prnMap.insert(CommonStrings::trWindowsGDI, PrintLanguage::WindowsGDI);
#endif
	return prnMap;
}

bool PrinterUtil::checkPrintLanguageSupport(const QString& printerName, PrintLanguage engine, bool toFile)
{
	if (engine >= PrintLanguage::PostScript1 && engine <= PrintLanguage::PostScript3)
		return (toFile || PrinterUtil::isPostscriptPrinter(printerName));

	if (engine == PrintLanguage::WindowsGDI)
		return os_is_win();

	if (engine == PrintLanguage::PDF)
		return toFile || os_is_unix();

	return false;
}

bool PrinterUtil::supportsPDF(const QString& /*printerName*/)
{
#ifdef _WIN32
	return false;
#else
	return true;
#endif
}

//Parameter needed on win32..
bool PrinterUtil::isPostscriptPrinter(const QString& printerName)
{
#ifdef _WIN32
	HDC dc;
	int	escapeCode;
	char technology[MAX_PATH] = {0};
	
	// Create the default device context
	dc = CreateDCW(nullptr, (LPCWSTR) printerName.utf16(), nullptr, nullptr);
	if (!dc)
	{
		qWarning("isPostscriptPrinter() failed to create device context for %s", printerName.toLatin1().data());
		return false;
	}
	// test if printer support the POSTSCRIPT_PASSTHROUGH escape code
	escapeCode = POSTSCRIPT_PASSTHROUGH;
	if (ExtEscape(dc, QUERYESCSUPPORT, sizeof(int), (LPCSTR) &escapeCode, 0, nullptr) > 0)
	{
		DeleteDC(dc);
		return true;
	}
	// test if printer support the POSTSCRIPT_DATA escape code
	escapeCode = POSTSCRIPT_DATA;
	if (ExtEscape(dc, QUERYESCSUPPORT, sizeof(int), (LPCSTR) &escapeCode, 0, nullptr) > 0)
	{
		DeleteDC(dc);
		return true;
	}
	// try to get postscript support by testing the printer technology
	escapeCode = GETTECHNOLOGY;
	if (ExtEscape(dc, QUERYESCSUPPORT, sizeof(int), (LPCSTR) &escapeCode, 0, nullptr) > 0)
	{
		// if GETTECHNOLOGY is supported, then ... get technology
		if (ExtEscape(dc, GETTECHNOLOGY, 0, nullptr, MAX_PATH, (LPSTR) technology) > 0)
		{
			// check technology string for postscript word
			strupr(technology);
			if (strstr(technology, "POSTSCRIPT"))
			{
				DeleteDC(dc);
				return true;
			}
		}
	}
	DeleteDC(dc);
	return false;
#else
	return true;
#endif
}
