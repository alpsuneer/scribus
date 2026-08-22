/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PRINTERUTIL_H
#define PRINTERUTIL_H


#include <QString>
#include <QMap>
#include <QMarginsF>
#include <QSizeF>
#include <QStringList>

#include "scribusapi.h"
#include "scribusstructs.h"

class SCRIBUS_API PrinterUtil
{
	public:
		PrinterUtil() = default;
		~PrinterUtil() = default;

		static void getDefaultPrintOptions(PrintOptions& options, const MarginStruct& docBleeds);

		QString static getDefaultPrinterName();
		QStringList static getPrinterNames();

		/**
		 * @brief The paper a printer is set up to use.
		 *
		 * Reads the printer's default page size (CUPS DefaultPageSize by way of
		 * QPrinterInfo). Proof printing sends this as the media instead of the
		 * document's own size, so a broadsheet lands on whatever sheet the proof
		 * printer actually holds.
		 *
		 * @param printerName printer to query
		 * @param mediaName receives a CUPS media keyword, e.g. "A4" or "A3"
		 * @return false if the printer is unknown or reports no usable size, in
		 * which case mediaName is left untouched and the caller should fall back.
		 */
		bool static getDefaultPaperSize(const QString& printerName, QString& mediaName, QSizeF* sizePoints = nullptr);

		/**
		 * @brief Paper sizes a queue actually offers, with the queue's default.
		 *
		 * Queried from cupsd rather than the PPD file: /etc/cups/ppd is
		 * root:lp 0640 and the desktop user is not in group lp, so parsing the
		 * PPD would fail for exactly the users who need this. The names are
		 * PPD keywords ("A4", "A3"), which is what -o media= expects.
		 *
		 * @param names receives the offered sizes, default first if identifiable
		 * @param defaultName receives the queue's default size
		 * @return false if the queue reports no page sizes at all
		 */
		bool static getSupportedPaperSizes(const QString& printerName, QStringList& names, QString& defaultName);

		/**
		 * @brief Paper trays a queue offers, with the queue's default.
		 *
		 * Same source and the same reason as getSupportedPaperSizes(). Names
		 * are PPD keywords ("Auto", "Tray1"), which is what -o InputSlot=
		 * expects.
		 *
		 * @return false if the queue reports no input slots (single-source
		 * printer): callers should then hide the control entirely.
		 */
		bool static getInputSlots(const QString& printerName, QStringList& traySlots, QString& defaultSlot);

		/**
		 * @brief Size in points of a PPD paper keyword, for scaling.
		 * @return an invalid (empty) QSizeF if the name is not one we can size.
		 */
		QSizeF static paperSizePoints(const QString& mediaName);

#if defined(_WIN32)
		/**
		 * @brief Get the defaults settings for a specified printer (Windows only)
		 *
		 * This function retrieve the default settings for a specified
		 * printer and return true on success
		 * This function is available only on Windows systems
		 *
		 * @param printerName the printer name
		 * @param devModeA an array which will store the DEVMODE structure with printer settings
		 * @return true if default settings were successfully retrieved.
		 */
		static bool getDefaultSettings(QString printerName, QByteArray& devModeA);
		/**
		 * @brief Initialize print options dialog box settings (Windows only) 
		 *
		 * This function initialize the print options dialog box for a specified
		 * printer and return true on success
		 * This function is available only on Windows systems
		 *
		 * @param printerName the printer name
		 * @param devModeA an array storing the DEVMODE structure for the specified printer
		 * @return true if default settings were successfully retrieved.
		 */
		static bool initDeviceSettings(QString printerName, QByteArray& devModeA);
#endif
		/**
		 * @brief Get the 4 minimum page margins for a certain paper size on the given printer
		 *
		 * @param printerName the printer name 
		 * @param pageSize the page size to get the margins for
		 * @param ptsTopMargin the page's top margin in points
		 * @param m_ptsBottomMargin the page's bottom margin in points
		 * @param m_ptsLeftMargin the page's left margin in points
		 * @param m_ptsRightMargin the page's right margin in points
		\retval bool true on success
		 */
		static bool getPrinterMarginValues(const QString& printerName, const QSizeF& pageSize, QMarginsF& margins);
		/**
		 * @brief Get default print engine for a specific printer
		 * @param printerName the printer name
		 * @param toFile if file printing is planned
		 */
		static PrintLanguage getDefaultPrintLanguage(const QString& printerName, bool toFile);
		/**
		 * @brief Get print engines supported by a specific printer
		 * @param printerName the printer name
		 * @param toFile if file printing is planned
		 */
		static PrintLanguageMap getPrintLanguageSupport(const QString& printerName, bool toFile);
		/**
		 * @brief Check if a print engine is supported by a specific printer
		 * @param printerName the printer name
		 * @param engine the print engine for which support is to be checked
		 * @param toFile if file printing is planned
		 */
		static bool checkPrintLanguageSupport(const QString& printerName, PrintLanguage engine, bool toFile);
		/**
		 * @brief Check if a specified printer supports postscript input
		 *
		 * On Windows, the function test postscript support for a specified printer
		 * and return true if ps is supported
		 * On non Windows systems, the function always return true
		 *
		 * @param printerName the printer name
		 * @return true is printer support postscript, false otherwise.
		 *
		 */
		static bool isPostscriptPrinter(const QString& printerName);
		/**
		 * @brief Check if a specified printer supports PDF input
		 *
		 * On Windows, the function always return false
		 *
		 * @param printerName the printer name
		 * @return true is printer support PDF, false otherwise.
		 *
		 */
		static bool supportsPDF(const QString& printerName);
};

#endif // DRUCK_H
