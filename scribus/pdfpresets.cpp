/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfpresets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>

#include "prefsmanager.h"
#include "scpaths.h"
#include "scribusstructs.h"

namespace
{
	const char* const FormatName = "scribus-pdf-preset";
	const int FormatVersion = 1;

	QStringList namesIn(const QString& dir)
	{
		QStringList names;
		const QStringList files = QDir(dir).entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name | QDir::IgnoreCase);
		for (const QString& f : files)
		{
			QFile file(dir + f);
			if (!file.open(QIODevice::ReadOnly))
				continue;
			const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
			if (obj.value("format").toString() != QLatin1String(FormatName))
				continue;
			const QString name = obj.value("name").toString();
			if (!name.isEmpty() && !names.contains(name))
				names << name;
		}
		return names;
	}

	bool readFile(const QString& path, PdfPresets::Preset& out)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return false;
		return PdfPresets::fromJson(QJsonDocument::fromJson(file.readAll()).object(), out);
	}
}

QString PdfPresets::userDir()
{
	QString dir = PrefsManager::instance().preferencesLocation();
	if (!dir.endsWith('/'))
		dir += '/';
	return dir + "pdf-presets/";
}

QString PdfPresets::systemDir()
{
	return ScPaths::instance().shareDir() + "pdf-presets/";
}

QString PdfPresets::fileNameFor(const QString& name)
{
	// Any name is allowed in the dialog; the file name must survive "/" etc.
	return QString::fromLatin1(QUrl::toPercentEncoding(name, " ()+,-=@[]{}")) + ".json";
}

QStringList PdfPresets::userNames()
{
	return namesIn(userDir());
}

QStringList PdfPresets::systemNames()
{
	return namesIn(systemDir());
}

QStringList PdfPresets::allNames()
{
	QStringList names = userNames();
	const QStringList office = systemNames();
	for (const QString& n : office)
	{
		if (!names.contains(n))
			names << n;
	}
	return names;
}

bool PdfPresets::isUserPreset(const QString& name)
{
	return !name.isEmpty() && QFile::exists(userDir() + fileNameFor(name));
}

bool PdfPresets::exists(const QString& name)
{
	if (name.isEmpty())
		return false;
	return isUserPreset(name) || QFile::exists(systemDir() + fileNameFor(name));
}

bool PdfPresets::load(const QString& name, Preset& out)
{
	if (name.isEmpty())
		return false;
	if (readFile(userDir() + fileNameFor(name), out))
	{
		out.readOnly = false;
		return true;
	}
	if (readFile(systemDir() + fileNameFor(name), out))
	{
		out.readOnly = true;
		return true;
	}
	return false;
}

bool PdfPresets::save(const Preset& preset, QString* error)
{
	if (preset.name.trimmed().isEmpty())
	{
		if (error)
			*error = QObject::tr("The preset has no name.");
		return false;
	}
	if (!QDir().mkpath(userDir()))
	{
		if (error)
			*error = QObject::tr("Cannot create %1").arg(QDir::toNativeSeparators(userDir()));
		return false;
	}
	// QSaveFile: a half-written preset must never replace a good one.
	QSaveFile file(userDir() + fileNameFor(preset.name));
	if (!file.open(QIODevice::WriteOnly))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	file.write(QJsonDocument(toJson(preset, true)).toJson(QJsonDocument::Indented));
	if (!file.commit())
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}

bool PdfPresets::remove(const QString& name)
{
	if (!isUserPreset(name))
		return false;
	return QFile::remove(userDir() + fileNameFor(name));
}

QString PdfPresets::userDefaultName()
{
	QFile file(userDir() + "default.txt");
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(file.readAll()).trimmed();
}

QString PdfPresets::officeDefaultName()
{
	QFile file(systemDir() + "office-default.txt");
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(file.readAll()).trimmed();
}

QString PdfPresets::defaultName()
{
	return !userDefaultName().isEmpty() ? userDefaultName() : officeDefaultName();
}

void PdfPresets::setDefaultName(const QString& name)
{
	// A file next to the presets, written at once: the choice must survive a
	// crash, which a value held in the preferences until exit would not.
	if (name.isEmpty())
	{
		QFile::remove(userDir() + "default.txt");
		return;
	}
	if (!QDir().mkpath(userDir()))
		return;
	QSaveFile file(userDir() + "default.txt");
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(name.toUtf8());
	file.write("\n");
	file.commit();
}

void PdfPresets::applyTo(const PDFOptions& src, PDFOptions& dst)
{
	const QString fileName = dst.fileName;
	const int rangeSelection = dst.pageRangeSelection;
	const QString rangeString = dst.pageRangeString;
	dst = src;
	dst.fileName = fileName;
	dst.pageRangeSelection = rangeSelection;
	dst.pageRangeString = rangeString;
	dst.firstUse = false;
}

QJsonObject PdfPresets::toJson(const Preset& preset, bool withPasswords)
{
	const PDFOptions& o = preset.opts;
	QJsonObject j;
	j["format"] = QLatin1String(FormatName);
	j["formatVersion"] = FormatVersion;
	j["name"] = preset.name;
	j["office"] = preset.office;

	// General
	j["pdfVersion"] = int((PDFVersion::Version) o.Version);
	j["binding"] = o.Binding;
	j["thumbnails"] = o.Thumbnails;
	j["articles"] = o.Articles;
	j["useLayers"] = o.useLayers;
	j["bookmarks"] = o.Bookmarks;
	j["resolution"] = o.Resolution;
	j["embedPDF"] = o.embedPDF;
	j["mirrorH"] = o.MirrorH;
	j["mirrorV"] = o.MirrorV;
	j["rotateDegrees"] = o.RotateDeg;
	j["clipToPrinterMargins"] = o.doClip;
	j["outputOneFilePerPage"] = o.doMultiFile;
	j["openAfterExport"] = o.openAfterExport;
	// Images
	j["compress"] = o.Compress;
	j["compressMethod"] = int(o.CompressMethod);
	j["quality"] = o.Quality;
	j["downsample"] = o.RecalcPic;
	j["downsampleResolution"] = o.PicRes;
	// Fonts
	j["fontEmbedding"] = int(o.FontEmbedding);
	j["subsetAllFonts"] = preset.subsetAllFonts;
	// Extras
	j["presentationMode"] = o.PresentMode;
	// Viewer
	j["pageLayout"] = o.PageLayout;
	j["displayBookmarks"] = o.displayBookmarks;
	j["displayThumbs"] = o.displayThumbs;
	j["displayLayers"] = o.displayLayers;
	j["displayFullscreen"] = o.displayFullscreen;
	j["hideToolBar"] = o.hideToolBar;
	j["hideMenuBar"] = o.hideMenuBar;
	j["fitWindow"] = o.fitWindow;
	j["openAction"] = o.openAction;
	// Security
	j["encrypt"] = o.Encrypt;
	j["permissions"] = o.Permissions;
	j["passOwner"] = withPasswords ? o.PassOwner : QString();
	j["passUser"] = withPasswords ? o.PassUser : QString();
	// Colour
	j["useRGB"] = o.UseRGB;
	j["isGrayscale"] = o.isGrayscale;
	j["useSpotColors"] = o.UseSpotColors;
	j["preserveCMYUnderBlackText"] = o.preserveCMYUnderBlackText;
	j["useLPI"] = o.UseLPI;
	QJsonObject lpi;
	for (auto it = o.LPISettings.constBegin(); it != o.LPISettings.constEnd(); ++it)
	{
		QJsonObject one;
		one["frequency"] = it.value().Frequency;
		one["angle"] = it.value().Angle;
		one["spotFunction"] = it.value().SpotFunc;
		lpi[it.key()] = one;
	}
	j["lpiSettings"] = lpi;
	j["useSolidProfile"] = o.UseProfiles;
	j["solidProfile"] = o.SolidProf;
	j["solidIntent"] = o.Intent;
	j["solidCompression"] = o.SComp;
	j["useImageProfile"] = o.UseProfiles2;
	j["imageProfile"] = o.ImageProf;
	j["imageIntent"] = o.Intent2;
	j["ignoreEmbeddedImageProfiles"] = o.EmbeddedI;
	// PDF/X output intent
	j["outputProfile"] = o.PrintProf;
	j["infoString"] = o.Info;
	// Pre-press
	j["cropMarks"] = o.cropMarks;
	j["bleedMarks"] = o.bleedMarks;
	j["registrationMarks"] = o.registrationMarks;
	j["colorMarks"] = o.colorMarks;
	j["docInfoMarks"] = o.docInfoMarks;
	j["markLength"] = o.markLength;
	j["markOffset"] = o.markOffset;
	j["useDocBleeds"] = o.useDocBleeds;
	j["bleedTop"] = o.bleeds.top();
	j["bleedLeft"] = o.bleeds.left();
	j["bleedRight"] = o.bleeds.right();
	j["bleedBottom"] = o.bleeds.bottom();
	return j;
}

bool PdfPresets::fromJson(const QJsonObject& j, Preset& out)
{
	if (j.value("format").toString() != QLatin1String(FormatName))
		return false;
	out.name = j.value("name").toString();
	if (out.name.isEmpty())
		return false;
	out.office = j.value("office").toBool(false);

	// A key that is missing (an older file, a hand-edited one) keeps the
	// stock default rather than becoming 0 / false.
	PDFOptions o;
	const auto B = [&j](const char* key, bool def) { return j.contains(key) ? j.value(key).toBool(def) : def; };
	const auto I = [&j](const char* key, int def) { return j.contains(key) ? j.value(key).toInt(def) : def; };
	const auto D = [&j](const char* key, double def) { return j.contains(key) ? j.value(key).toDouble(def) : def; };
	const auto S = [&j](const char* key, const QString& def) { return j.contains(key) ? j.value(key).toString(def) : def; };

	const int version = I("pdfVersion", int(PDFVersion::PDF_14));
	if (version >= PDFVersion::PDFVersion_Min && version <= PDFVersion::PDFVersion_Max)
		o.Version = (PDFVersion::Version) version;
	o.Binding = I("binding", o.Binding);
	o.Thumbnails = B("thumbnails", o.Thumbnails);
	o.Articles = B("articles", o.Articles);
	o.useLayers = B("useLayers", o.useLayers);
	o.Bookmarks = B("bookmarks", o.Bookmarks);
	o.Resolution = I("resolution", o.Resolution);
	o.embedPDF = B("embedPDF", o.embedPDF);
	o.MirrorH = B("mirrorH", o.MirrorH);
	o.MirrorV = B("mirrorV", o.MirrorV);
	o.RotateDeg = I("rotateDegrees", o.RotateDeg);
	o.doClip = B("clipToPrinterMargins", o.doClip);
	o.doMultiFile = B("outputOneFilePerPage", o.doMultiFile);
	o.openAfterExport = B("openAfterExport", o.openAfterExport);
	o.Compress = B("compress", o.Compress);
	o.CompressMethod = (PDFOptions::PDFCompression) qBound(0, I("compressMethod", int(o.CompressMethod)), 3);
	o.Quality = I("quality", o.Quality);
	o.RecalcPic = B("downsample", o.RecalcPic);
	o.PicRes = I("downsampleResolution", o.PicRes);
	o.FontEmbedding = (PDFOptions::PDFFontEmbedding) qBound(0, I("fontEmbedding", int(o.FontEmbedding)), 2);
	out.subsetAllFonts = B("subsetAllFonts", false);
	o.PresentMode = B("presentationMode", o.PresentMode);
	o.PageLayout = I("pageLayout", o.PageLayout);
	o.displayBookmarks = B("displayBookmarks", o.displayBookmarks);
	o.displayThumbs = B("displayThumbs", o.displayThumbs);
	o.displayLayers = B("displayLayers", o.displayLayers);
	o.displayFullscreen = B("displayFullscreen", o.displayFullscreen);
	o.hideToolBar = B("hideToolBar", o.hideToolBar);
	o.hideMenuBar = B("hideMenuBar", o.hideMenuBar);
	o.fitWindow = B("fitWindow", o.fitWindow);
	o.openAction = S("openAction", o.openAction);
	o.Encrypt = B("encrypt", o.Encrypt);
	o.Permissions = I("permissions", o.Permissions);
	o.PassOwner = S("passOwner", QString());
	o.PassUser = S("passUser", QString());
	o.UseRGB = B("useRGB", o.UseRGB);
	o.isGrayscale = B("isGrayscale", o.isGrayscale);
	o.UseSpotColors = B("useSpotColors", o.UseSpotColors);
	o.preserveCMYUnderBlackText = B("preserveCMYUnderBlackText", o.preserveCMYUnderBlackText);
	o.UseLPI = B("useLPI", o.UseLPI);
	const QJsonObject lpi = j.value("lpiSettings").toObject();
	for (auto it = lpi.constBegin(); it != lpi.constEnd(); ++it)
	{
		const QJsonObject one = it.value().toObject();
		LPIData data;
		data.Frequency = one.value("frequency").toInt();
		data.Angle = one.value("angle").toInt();
		data.SpotFunc = one.value("spotFunction").toInt();
		o.LPISettings.insert(it.key(), data);
	}
	o.UseProfiles = B("useSolidProfile", o.UseProfiles);
	o.SolidProf = S("solidProfile", o.SolidProf);
	o.Intent = I("solidIntent", o.Intent);
	o.SComp = I("solidCompression", o.SComp);
	o.UseProfiles2 = B("useImageProfile", o.UseProfiles2);
	o.ImageProf = S("imageProfile", o.ImageProf);
	o.Intent2 = I("imageIntent", o.Intent2);
	o.EmbeddedI = B("ignoreEmbeddedImageProfiles", o.EmbeddedI);
	o.PrintProf = S("outputProfile", o.PrintProf);
	o.Info = S("infoString", o.Info);
	o.cropMarks = B("cropMarks", o.cropMarks);
	o.bleedMarks = B("bleedMarks", o.bleedMarks);
	o.registrationMarks = B("registrationMarks", o.registrationMarks);
	o.colorMarks = B("colorMarks", o.colorMarks);
	o.docInfoMarks = B("docInfoMarks", o.docInfoMarks);
	o.markLength = D("markLength", o.markLength);
	o.markOffset = D("markOffset", o.markOffset);
	o.useDocBleeds = B("useDocBleeds", o.useDocBleeds);
	o.bleeds.set(D("bleedTop", 0.0), D("bleedLeft", 0.0), D("bleedBottom", 0.0), D("bleedRight", 0.0));
	o.firstUse = false;
	out.opts = o;
	return true;
}

QByteArray PdfPresets::fingerprint(const PDFOptions& opts, bool subsetAllFonts)
{
	Preset p;
	p.name = QStringLiteral("-");
	p.opts = opts;
	p.subsetAllFonts = subsetAllFonts;
	QJsonObject j = toJson(p, true);
	j.remove("name");
	j.remove("office");
	// Bleed values only count when the document's own bleeds are not used.
	if (opts.useDocBleeds)
	{
		j.remove("bleedTop");
		j.remove("bleedLeft");
		j.remove("bleedRight");
		j.remove("bleedBottom");
	}
	return QJsonDocument(j).toJson(QJsonDocument::Compact);
}

int PdfPresets::exportAll(const QString& dir, QString* error)
{
	QString target = dir;
	if (!target.endsWith('/'))
		target += '/';
	if (!QDir().mkpath(target))
	{
		if (error)
			*error = QObject::tr("Cannot create %1").arg(QDir::toNativeSeparators(target));
		return 0;
	}
	int written = 0;
	const QStringList names = userNames();
	for (const QString& name : names)
	{
		const QString src = userDir() + fileNameFor(name);
		const QString dst = target + fileNameFor(name);
		QFile::remove(dst);
		if (QFile::copy(src, dst))
			++written;
		else if (error)
			*error = QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(dst));
	}
	return written;
}

bool PdfPresets::importFile(const QString& file, QString* name, bool overwrite, bool* existed, QString* error)
{
	Preset preset;
	if (!readFile(file, preset))
	{
		if (error)
			*error = QObject::tr("%1 is not a PDF preset file.").arg(QDir::toNativeSeparators(file));
		return false;
	}
	if (name)
		*name = preset.name;
	const bool already = isUserPreset(preset.name);
	if (existed)
		*existed = already;
	if (already && !overwrite)
		return false;
	return save(preset, error);
}
