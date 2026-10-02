/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PDFPRESETS_H
#define PDFPRESETS_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "pdfoptions.h"
#include "scribusapi.h"

/*!
 \brief Named PDF export presets, and which one is the Default.

 A preset is every export setting of the Save as PDF dialog except the two that
 belong to one export rather than to a way of exporting: the file name and the
 page range.

 \par Where they live
 - the user's own: <preferences folder>/pdf-presets/<name>.json — on a normal
   install ~/.config/scribus/pdf-presets/. Never touched by an update.
 - the office's: <share>/pdf-presets/ — shipped read-only in the package
   (tools/update-office-pdf-presets.py copies the ones marked "office" from the
   release laptop). A user preset with the same name hides the office one.

 \par Which one is the Default
 The user's own choice (<preferences folder>/pdf-presets/default.txt) wins.
 Without one, the office Default named in <share>/pdf-presets/office-default.txt
 is used. Without either there is no Default and the dialog behaves as stock
 Scribus does.

 \par Fonts
 Per-font embed/subset lists are a property of one document's fonts and mean
 nothing in another document, so a preset keeps the embedding mode and whether
 fonts are subset, not the lists.

 \par Passwords
 Kept in the user's own preset files (the Security tab is part of a preset),
 but never written into the office copy that goes into the package.
 */
class SCRIBUS_API PdfPresets
{
public:
	struct Preset
	{
		QString    name;
		PDFOptions opts;
		bool       subsetAllFonts { false };
		bool       office { false };     //!< marked to be shipped to the office PCs
		bool       readOnly { false };   //!< came from the package
	};

	static QString userDir();
	static QString systemDir();

	static QStringList userNames();
	static QStringList systemNames();
	//! User presets first, then office presets that no user preset hides.
	static QStringList allNames();

	static bool exists(const QString& name);
	static bool isUserPreset(const QString& name);
	static bool load(const QString& name, Preset& out);
	static bool save(const Preset& preset, QString* error = nullptr);
	static bool remove(const QString& name);

	//! Empty when there is none, or when the named preset no longer exists.
	static QString defaultName();
	static QString userDefaultName();
	static QString officeDefaultName();
	static void setDefaultName(const QString& name);

	static QJsonObject toJson(const Preset& preset, bool withPasswords);
	static bool fromJson(const QJsonObject& obj, Preset& out);
	/*! The settings a preset covers, in a canonical form, for "has anything
	    changed since the preset was applied". */
	static QByteArray fingerprint(const PDFOptions& opts, bool subsetAllFonts);

	//! Copies \a src into \a dst except the file name and the page range.
	static void applyTo(const PDFOptions& src, PDFOptions& dst);

	//! Writes every user preset into \a dir. Returns the number written.
	static int exportAll(const QString& dir, QString* error = nullptr);
	/*! Reads one exported preset file. \a name gets its preset name. Fails
	    without writing when it exists and \a overwrite is false (\a existed set). */
	static bool importFile(const QString& file, QString* name, bool overwrite, bool* existed = nullptr, QString* error = nullptr);

	static QString fileNameFor(const QString& name);
};

#endif // PDFPRESETS_H
