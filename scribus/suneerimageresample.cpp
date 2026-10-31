/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerimageresample.h"
#include <algorithm>
#include "suneerphotostore.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "scimagefeather.h"
#include "scribusdoc.h"
#include "undomanager.h"
#include "util.h"
#include "commonstrings.h"
#include "scpage.h"
#include "scribus.h"
#include "scraction.h"
#include "selection.h"
#include "undogui.h"
#include "suneerphotoedit.h"
#include "suneerimagelinks.h"
#include "ui/resizeimagedialog.h"
#include <QRegularExpression>
#include <QVariant>

namespace
{
	QString humanSize(qint64 bytes)
	{
		return QLocale().formattedDataSize(bytes, 1);
	}

	const QString kOrg = QStringLiteral("Scribus");
	const QString kApp = QStringLiteral("SuneerImageTools");

	// Run one external command with a progress window that keeps the UI
	// alive, a Cancel button, and a hard timeout (a picture on a slow
	// network share must not hang Scribus). Returns the error text or "".
	QString runWithProgress(const QString& program, const QStringList& args, int timeoutMs, const QString& text, QWidget* parent)
	{
		QProcess proc;
		QEventLoop loop;
		bool done = false, timedOut = false, cancelled = false;
		QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, [&](int, QProcess::ExitStatus) { done = true; loop.quit(); });
		QObject::connect(&proc, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError) { if (!done) { done = true; loop.quit(); } });
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { timedOut = true; loop.quit(); });
		QProgressDialog progress(text, QObject::tr("Cancel"), 0, 0, parent);
		progress.setWindowTitle(QObject::tr("Image Size"));
		progress.setWindowModality(Qt::WindowModal);
		progress.setMinimumDuration(400);
		QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&]() { cancelled = true; loop.quit(); });
		proc.start(program, args);
		if (!proc.waitForStarted(5000))
			return QObject::tr("could not start %1").arg(program);
		timer.start(timeoutMs);
		loop.exec();
		progress.close();
		if (!done)
		{
			proc.kill();
			proc.waitForFinished(2000);
			if (cancelled)
				return QObject::tr("cancelled");
			if (timedOut)
				return QObject::tr("%1 did not finish within %2 s (slow network drive?)").arg(program).arg(timeoutMs / 1000);
		}
		if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
		{
			const QString err = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
			return err.isEmpty() ? QObject::tr("%1 failed (exit %2)").arg(program).arg(proc.exitCode()) : err.section('\n', -2);
		}
		return QString();
	}
}

int SuneerImageResample::settingInt(const QString& key, int def)
{
	return QSettings(kOrg, kApp).value(key, def).toInt();
}

QString SuneerImageResample::settingString(const QString& key, const QString& def)
{
	return QSettings(kOrg, kApp).value(key, def).toString();
}

void SuneerImageResample::setSetting(const QString& key, const QVariant& value)
{
	QSettings(kOrg, kApp).setValue(key, value);
}

SuneerImageResample::Info SuneerImageResample::probe(const PageItem* item)
{
	Info info;
	if (!item || item->Pfile.isEmpty())
		return info;
	const QFileInfo fi(item->Pfile);
	if (!fi.exists())
		return info;
	info.bytes = fi.size();
	QImageReader reader(item->Pfile);
	const QSize sz = reader.size();
	info.format = QString::fromLatin1(reader.format()).toUpper();
	if (info.format == QLatin1String("JPG")) info.format = QStringLiteral("JPEG");
	if (info.format == QLatin1String("TIF") || info.format == QLatin1String("TIFF")) info.format = QStringLiteral("TIFF");
	if (info.format.isEmpty())
		info.format = fi.suffix().toUpper();
	// Pixel size: the header when Qt can read it, else what the loader found.
	if (sz.isValid())
	{
		info.width = sz.width();
		info.height = sz.height();
	}
	else
	{
		info.width = item->OrigW;
		info.height = item->OrigH;
	}
	const ImageInfoRecord& ii = item->pixm.imgInfo;
	info.dpiX = ii.xres > 0 ? ii.xres : 72;
	info.dpiY = ii.yres > 0 ? ii.yres : 72;
	switch (ii.colorspace)
	{
		case ColorSpaceCMYK: info.colorMode = QStringLiteral("CMYK"); break;
		case ColorSpaceGray: info.colorMode = QStringLiteral("Gray"); break;
		case ColorSpaceDuotone: info.colorMode = QStringLiteral("Duotone"); break;
		default: info.colorMode = QStringLiteral("RGB"); break;
	}
	info.hasProfile = ii.isEmbedded;
	info.profileName = ii.profileName;
	info.exifOrientation = ii.exifInfo.orientation;
	// Alpha: a format that can carry it AND pixels that use it (TIFF/PNG are
	// decoded to ARGB whatever the file holds, so the channel alone says nothing).
	info.alpha = false;
	if (info.format != QLatin1String("JPEG") && info.format != QLatin1String("BMP"))
	{
		const QImage& px = item->pixm.qImage();
		if (!px.isNull() && px.hasAlphaChannel())
		{
			const QImage a = px.convertToFormat(QImage::Format_ARGB32);
			for (int y = 0; y < a.height() && !info.alpha; y += qMax(1, a.height() / 200))
			{
				const QRgb* line = reinterpret_cast<const QRgb*>(a.constScanLine(y));
				for (int x = 0; x < a.width(); x += qMax(1, a.width() / 200))
					if (qAlpha(line[x]) < 255) { info.alpha = true; break; }
			}
		}
	}
	if (info.format == QLatin1String("JPEG"))
		info.alpha = false;
	info.ok = info.width > 0 && info.height > 0;
	return info;
}

SuneerImageResample::Info SuneerImageResample::probeFile(const QString& path)
{
	Info info;
	const QFileInfo fi(path);
	if (path.isEmpty() || !fi.exists())
		return info;
	info.bytes = fi.size();
	QImageReader reader(path);
	const QSize sz = reader.size();
	info.format = QString::fromLatin1(reader.format()).toUpper();
	if (info.format == QLatin1String("JPG")) info.format = QStringLiteral("JPEG");
	if (info.format == QLatin1String("TIF")) info.format = QStringLiteral("TIFF");
	if (sz.isValid())
	{
		info.width = sz.width();
		info.height = sz.height();
	}
	info.ok = info.width > 0 && info.height > 0;
	return info;
}

// The file a resample of the frame's CURRENT picture starts from: the current file itself, or the
// file the current one was resampled from (the same picture with more pixels, so repeated resizes do
// not pile up quality loss). Never an earlier picture of the frame: "SuneerImgOriginal" is the file
// Edit photo opens (the photo before the edit) - resampling from it brought the original back over
// an Edit photo > Apply / a processed obituary photo after the first resize (the background removal
// was lost). The original is used only when the user asks for it (Edit photo, Reset to original).
QString SuneerImageResample::originalPathOf(const PageItem* item)
{
	if (!item)
		return QString();
	auto attr = [item](const char* name) {
		const QList<ObjectAttribute> l = item->getObjectAttributes(QString::fromLatin1(name));
		return l.isEmpty() ? QString() : l.first().value;
	};
	auto same = [](const QString& a, const QString& b) {
		return !a.isEmpty() && !b.isEmpty() && QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
	};
	const QString cur = item->Pfile;
	// 1. the current file was written by resample() from this one (recorded since 2026-10-30)
	const QString from = attr("SuneerResampleOf");
	if (same(attr("SuneerResampleOut"), cur) && QFileInfo(from).isFile())
		return from;
	// 2. an older document: the current file is a resample copy "<base>_WxH_Ndpi[-N].<ext>" of one of
	//    the frame's own files - its edit / processed result first, the remembered original last
	static const QRegularExpression tag(QStringLiteral("_\\d+x\\d+_\\d+dpi(-\\d+)?$"));
	const QFileInfo ci(cur);
	QString base = ci.completeBaseName();
	if (base.contains(tag))
	{
		base.remove(tag);
		const QString wanted = base + QLatin1Char('.') + ci.suffix();
		for (const char* name : { "suneer.photoEditOutput", "obit.out", "SuneerImgOriginal", "news.original", "obit.src" })
		{
			const QString cand = attr(name);
			if (!cand.isEmpty() && QFileInfo(cand).fileName().compare(wanted, Qt::CaseInsensitive) == 0 && QFileInfo(cand).isFile())
				return cand;
		}
	}
	return cur;
}

double SuneerImageResample::effectiveDpi(const PageItem* item, double* dpiY)
{
	if (!item)
		return 0;
	const double sx = item->imageXScale() > 0 ? 72.0 / item->imageXScale() : 0;
	const double sy = item->imageYScale() > 0 ? 72.0 / item->imageYScale() : 0;
	if (dpiY)
		*dpiY = sy;
	return sx;
}

QColor SuneerImageResample::dpiColor(double dpi)
{
	if (dpi < 150) return QColor(200, 30, 30);
	if (dpi < 200) return QColor(230, 120, 0);
	if (dpi <= 400) return QColor(20, 140, 40);
	return QColor(30, 90, 220);
}

QString SuneerImageResample::dpiWord(double dpi)
{
	if (dpi < 150) return QObject::tr("too low");
	if (dpi < 200) return QObject::tr("low");
	if (dpi <= 400) return QObject::tr("good");
	return QObject::tr("too big");
}

SuneerImageResample::Quality SuneerImageResample::resolveQuality(Quality q, bool reducing)
{
	if (q == Quality::Auto)
		return reducing ? Quality::Sharper : Quality::Smoother;
	return q;
}

QString SuneerImageResample::qualityName(Quality q)
{
	switch (q)
	{
		case Quality::Sharper:  return QObject::tr("Bicubic Sharper (reduce)");
		case Quality::Smoother: return QObject::tr("Bicubic Smoother (enlarge)");
		case Quality::Bicubic:  return QObject::tr("Bicubic");
		case Quality::Nearest:  return QObject::tr("Nearest");
		default:                return QObject::tr("Auto");
	}
}

bool SuneerImageResample::magickAvailable()
{
	static int cached = -1;
	if (cached < 0)
		cached = (!QStandardPaths::findExecutable("magick").isEmpty() || !QStandardPaths::findExecutable("convert").isEmpty()) ? 1 : 0;
	return cached == 1;
}

QString SuneerImageResample::outputPathFor(const ScribusDoc* doc, const QString& srcPath, int newW, int newH, int dpi)
{
	const QFileInfo src(srcPath);
	QString suffix = src.suffix().toLower();
	static const QStringList known { "jpg", "jpeg", "png", "tif", "tiff", "bmp" };
	if (!known.contains(suffix))
		suffix = QStringLiteral("jpg");
	QString dir = SuneerPhotoStore::editedDir(doc, true);   // "<doc>_photos/edited" for a saved document
	if (dir.isEmpty())
		dir = src.absolutePath();
	QDir().mkpath(dir);
	// Strip tags an earlier resample added, so names do not pile up.
	QString base = src.completeBaseName();
	base.remove(QRegularExpression(QStringLiteral("_\\d+x\\d+_\\d+dpi$")));
	const QString stem = QStringLiteral("%1/%2_%3x%4_%5dpi").arg(dir, base).arg(newW).arg(newH).arg(dpi);
	QString out = stem + "." + suffix;
	for (int n = 2; QFileInfo::exists(out) && n < 1000; ++n)
		out = stem + QStringLiteral("-%1.").arg(n) + suffix;
	return out;
}

void SuneerImageResample::fitPixels(const PageItem* item, int targetDpi, int& newW, int& newH)
{
	double effY = 0;
	const double effX = effectiveDpi(item, &effY);
	newW = qMax(1, qRound(item->OrigW * targetDpi / qMax(1.0, effX)));
	newH = qMax(1, qRound(item->OrigH * targetDpi / qMax(1.0, effY)));
}

QString SuneerImageResample::fitToFrame(ScribusDoc* doc, PageItem* item, int targetDpi, const Options& opt, QString* summary, QString* outPath)
{
	int w, h;
	fitPixels(item, targetDpi, w, h);
	return resample(doc, item, w, h, opt, summary, outPath);
}

QString SuneerImageResample::resample(ScribusDoc* doc, PageItem* item, int newW, int newH, const Options& opt, QString* summary, QString* outPath)
{
	if (!doc || !item || !item->isImageFrame() || item->Pfile.isEmpty() || newW < 1 || newH < 1)
		return QObject::tr("nothing to resample");
	const Info info = probe(item);
	if (!info.ok)
		return QObject::tr("the picture file could not be read:\n%1").arg(item->Pfile);
	if (newW == info.width && newH == info.height)
		return QObject::tr("the picture already has this size");
	// Every resample reads the ORIGINAL file, never an earlier resampled copy,
	// so repeated changes do not pile up quality loss. The original is
	// remembered on the frame the first time.
	const QString srcPath = originalPathOf(item);
	const Info src = srcPath == item->Pfile ? info : probeFile(srcPath);
	if (!src.ok)
		return QObject::tr("the original picture file could not be read:\n%1").arg(srcPath);
	if (!opt.allowEnlarge && (newW > src.width || newH > src.height))
		return QObject::tr("that would enlarge the picture (%1 x %2 -> %3 x %4); tick \"Allow enlarge\" if you really want that")
		       .arg(src.width).arg(src.height).arg(newW).arg(newH);

	const double rx = double(info.width) / newW;
	const double ry = double(info.height) / newH;
	double effY = 0;
	const double effX = effectiveDpi(item, &effY);
	const double newDpiX = opt.targetDpi > 0 ? opt.targetDpi : effX / rx;
	const double newDpiY = opt.targetDpi > 0 ? opt.targetDpi : effY / ry;

	// An embedded picture lives in a session temp file and is base64-embedded
	// on save: the small result must stay embedded, so it goes to a fresh temp file.
	const bool wasInline = item->isInlineImage && item->isTempFile;
	QString dst;
	QString suffix = QFileInfo(item->Pfile).suffix().toLower();
	static const QStringList known { "jpg", "jpeg", "png", "tif", "tiff", "bmp" };
	if (!known.contains(suffix))
		suffix = QStringLiteral("jpg");
	if (wasInline)
	{
		QTemporaryFile tempFile(QDir::tempPath() + "/scribus_temp_XXXXXX." + suffix);
		if (!tempFile.open())
			return QObject::tr("could not create a temporary file for the embedded picture");
		dst = getLongPathName(tempFile.fileName());
		tempFile.setAutoRemove(false);
		tempFile.close();
		QFile::remove(dst);
	}
	else
		dst = outputPathFor(doc, srcPath, newW, newH, qRound(newDpiX));

	// 1. the pixels
	QString err;
	const bool isJpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");
	bool written = false;
	if (magickAvailable())
	{
		QString program = QStandardPaths::findExecutable("magick");
		QStringList args;
		if (program.isEmpty())
			program = QStandardPaths::findExecutable("convert");
		const Quality q = resolveQuality(opt.quality, newW < src.width || newH < src.height);
		const char* filter = q == Quality::Nearest ? "Point" : q == Quality::Smoother ? "Mitchell" : q == Quality::Bicubic ? "Catrom" : "Lanczos";
		args << srcPath + "[0]"
		     << "-filter" << filter
		     << "-resize" << QStringLiteral("%1x%2!").arg(newW).arg(newH);
		if (q == Quality::Sharper && opt.sharpen > 0)
		{
			// Light unsharp mask on the luminance only: Lab's L for colour
			// pictures (alpha and chroma untouched, so a cut-out keeps clean
			// edges), the single channel for Gray, the inks for CMYK. The
			// amount grows with the reduction, as Photoshop's Sharper does.
			const double ratio = qMax(1.0, double(src.width) / newW);
			const double amount = (opt.sharpen / 100.0) * qMin(1.5, 0.5 + 0.25 * ratio);
			const QString unsharp = QStringLiteral("0x0.5+%1+0").arg(amount, 0, 'f', 2);
			if (info.colorMode == QLatin1String("RGB"))
				args << "-colorspace" << "Lab" << "-channel" << "R" << "-unsharp" << unsharp << "+channel" << "-colorspace" << "sRGB";
			else if (info.colorMode == QLatin1String("Gray"))
				args << "-channel" << "R" << "-unsharp" << unsharp << "+channel";
			else
				args << "-channel" << "CMYK" << "-unsharp" << unsharp << "+channel";
		}
		args << "-units" << "PixelsPerInch"
		     << "-density" << QStringLiteral("%1x%2").arg(newDpiX, 0, 'f', 2).arg(newDpiY, 0, 'f', 2);
		if (isJpeg)
			args << "-quality" << QString::number(qBound(50, opt.jpegQuality, 100));
		args << dst;
		err = runWithProgress(program, args, opt.timeoutMs,
		                      QObject::tr("Resampling %1\n%2 x %3 -> %4 x %5 ...").arg(QFileInfo(item->Pfile).fileName()).arg(info.width).arg(info.height).arg(newW).arg(newH),
		                      opt.parent);
		written = err.isEmpty() && QFileInfo::exists(dst);
		if (!written && err == QObject::tr("cancelled"))
			return err;
	}
	if (!written)
	{
		// Qt fallback: RGB only - it cannot keep CMYK, the profile or EXIF.
		if (info.colorMode != QLatin1String("RGB") && info.colorMode != QLatin1String("Gray"))
			return QObject::tr("ImageMagick is needed to resample a %1 picture without converting it to RGB%2")
			       .arg(info.colorMode, err.isEmpty() ? QString() : "\n(" + err + ")");
		QImage source(srcPath);
		if (source.isNull())
			return QObject::tr("could not read the picture:\n%1").arg(srcPath);
		QImage out = source.scaled(newW, newH, Qt::IgnoreAspectRatio,
		                           resolveQuality(opt.quality, newW < src.width) == Quality::Nearest ? Qt::FastTransformation : Qt::SmoothTransformation);
		out.setDotsPerMeterX(qRound(newDpiX / 0.0254));
		out.setDotsPerMeterY(qRound(newDpiY / 0.0254));
		const QString werr = writeImageToFile(out, dst, isJpeg ? opt.jpegQuality : -1);
		if (!werr.isEmpty())
			return werr + (err.isEmpty() ? QString() : "\n(" + err + ")");
	}

	// 2. relink, same look, one undo step
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IImageFrame, Um::GetImage, QString(), Um::IGetImage);
	const double oldScaleX = item->imageXScale(), oldScaleY = item->imageYScale();
	const double oldOffX = item->imageXOffset(), oldOffY = item->imageYOffset();
	if (wasInline)
		item->isTempFile = false;   // keep the old temp file: undo points back at it
	// The file Edit photo opens: remembered for a plain photo only - an edited / processed one has its
	// original recorded by its tool already (and its current file is not an original)
	const bool editedFrame = !item->getObjectAttributes(QStringLiteral("suneer.photoEditOutput")).isEmpty()
	                         || !item->getObjectAttributes(QStringLiteral("obit.out")).isEmpty()
	                         || !item->getObjectAttributes(QStringLiteral("SuneerBgSource")).isEmpty();
	if (item->getObjectAttributes(QStringLiteral("SuneerImgOriginal")).isEmpty() && !wasInline && !editedFrame)
	{
		ObjAttrVector attrs = *item->getObjectAttributes();
		ObjectAttribute a;
		a.name = QStringLiteral("SuneerImgOriginal");
		a.type = QStringLiteral("string");
		a.value = item->Pfile;
		attrs.append(a);
		item->setObjectAttributes(&attrs);
	}
	if (!doc->loadPict(dst, item, false, true))
	{
		if (tx)
			tx.cancel();
		return QObject::tr("the resampled file could not be loaded:\n%1").arg(dst);
	}
	if (wasInline)
	{
		item->isInlineImage = true;
		item->isTempFile = true;
	}
	{
		// this chain: the next resample of this picture starts again from srcPath (see originalPathOf)
		ObjAttrVector attrs = *item->getObjectAttributes();
		auto put = [&attrs](const QString& name, const QString& value) {
			for (ObjectAttribute& a : attrs)
				if (a.name == name)
				{
					a.value = value;
					return;
				}
			ObjectAttribute a;
			a.name = name;
			a.type = QStringLiteral("string");
			a.value = value;
			attrs.append(a);
		};
		put(QStringLiteral("SuneerResampleOf"), QFileInfo(srcPath).absoluteFilePath());
		put(QStringLiteral("SuneerResampleOut"), QFileInfo(item->Pfile).absoluteFilePath());
		item->setObjectAttributes(&attrs);
	}
	item->setImageXYScale(oldScaleX * rx, oldScaleY * ry);
	item->setImageXYOffset(oldOffX / rx, oldOffY / ry);
	// The feather amount is in picture pixels: fewer pixels, smaller amount.
	double featherPx = 0;
	QString featherDir;
	if (ScFeatherMask::paramsOf(item->effectsInUse, featherPx, featherDir))
		ScFeatherMask::set(item->effectsInUse, featherPx / rx, featherDir);
	// The wrap contour is in frame points and did not move: tell the frame
	// so the new pixel grid is its new baseline rather than a change.
	if (auto* frame = item->asImageFrame())
		frame->suneerAcceptContour();
	item->updateClip();
	item->update();
	if (tx)
		tx.commit();
	doc->changed();
	doc->regionsChanged()->update(QRectF());

	if (summary)
		*summary = QObject::tr("%1 x %2 (%3) -> %4 x %5 (%6) @ %7 dpi")
			.arg(info.width).arg(info.height).arg(humanSize(info.bytes))
			.arg(newW).arg(newH).arg(humanSize(QFileInfo(dst).size())).arg(qRound(newDpiX));
	if (outPath)
		*outPath = dst;
	return QString();
}

void SuneerImageResample::offerDownsampleAfterImport(ScribusDoc* doc, PageItem* item, QWidget* parent)
{
	if (!doc || !item || !item->isImageFrame() || !item->imageIsAvailable)
		return;
	if (!QSettings(kOrg, kApp).value(QStringLiteral("offerOnImport"), false).toBool())
		return;
	double effY = 0;
	const double eff = qMin(effectiveDpi(item, &effY), effY);
	if (eff <= 400)
		return;
	const int target = settingInt(QStringLiteral("fitDpi"), 300);
	int w, h;
	fitPixels(item, target, w, h);
	const Info info = probe(item);
	const int r = QMessageBox::question(parent, QObject::tr("Image Size"),
		QObject::tr("This picture is %1 x %2 px, which is %3 dpi at the placed size (%4).\n\nResample it to %5 x %6 px for %7 dpi? The original file is kept.")
			.arg(info.width).arg(info.height).arg(qRound(eff)).arg(humanSize(info.bytes)).arg(w).arg(h).arg(target),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
	if (r != QMessageBox::Yes)
		return;
	Options opt;
	opt.jpegQuality = settingInt(QStringLiteral("jpegQuality"), 92);
	opt.parent = parent;
	QString summary;
	const QString err = fitToFrame(doc, item, target, opt, &summary);
	if (!err.isEmpty())
		QMessageBox::warning(parent, QObject::tr("Image Size"), err);
}

// ---------------------------------------------------------------------------
// Self-test (tests/suneer/image_keep_edit.sh): an edited photo stays edited
// ---------------------------------------------------------------------------
// SUNEER_IMGKEEP_SELFTEST=<report prefix>, SUNEER_IMGKEEP_DIR=<fixtures>, SUNEER_IMGKEEP_PHASE=1|2.
// Fixtures: photo.jpg and obit_orig.jpg (green background, red subject), obit_out.png (the obituary
// pipeline's result: grey Obit BG). NEWS = Edit photo > Apply (background blue); OBIT = resampled once
// as it was (green), then processed (grey). Every step looks at the PIXELS the frame has loaded
// (a background corner): blue / grey = the edit, green = the original came back (the bug).
// Phase 1: resize + auto-resample (the handle path), W/H without resample, the image size toolbar's
// resample, enlarge again, Adjust Frame to Image, embedded, save. Phase 2: the saved file reopened.
namespace {
QString cornerKind(const PageItem* f)
{
	if (!f || !f->imageIsAvailable)
		return QStringLiteral("none");
	const QImage& im = f->pixm.qImage();
	if (im.isNull())
		return QStringLiteral("none");
	const QColor c = im.pixelColor(qMin(4, im.width() - 1), qMin(4, im.height() - 1));
	if (c.green() > 150 && c.red() < 110 && c.blue() < 110)
		return QStringLiteral("GREEN(original)");
	if (c.blue() > 150 && c.red() < 110 && c.green() < 110)
		return QStringLiteral("blue(edited)");
	if (qAbs(c.red() - 190) < 25 && qAbs(c.green() - 196) < 25 && qAbs(c.blue() - 200) < 25)
		return QStringLiteral("grey(processed)");
	return QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

QString attrOfItem(const PageItem* f, const QString& name)
{
	const QList<ObjectAttribute> l = f->getObjectAttributes(name);
	return l.isEmpty() ? QString() : l.first().value;
}

void setAttrOfItem(PageItem* f, const QString& name, const QString& value)
{
	ObjAttrVector attrs = *f->getObjectAttributes();
	for (ObjectAttribute& a : attrs)
		if (a.name == name)
		{
			a.value = value;
			f->setObjectAttributes(&attrs);
			return;
		}
	ObjectAttribute a;
	a.name = name;
	a.type = QStringLiteral("string");
	a.value = value;
	attrs.append(a);
	f->setObjectAttributes(&attrs);
}
} // namespace

void SuneerImageResample::runKeepEditSelfTestIfAsked(ScribusMainWindow* mw, ScribusDoc* doc)
{
	const QString prefix = qEnvironmentVariable("SUNEER_IMGKEEP_SELFTEST");
	if (prefix.isEmpty() || !doc || !mw)
		return;
	const QString fix = qEnvironmentVariable("SUNEER_IMGKEEP_DIR");
	const int phase = qEnvironmentVariableIntValue("SUNEER_IMGKEEP_PHASE");
	QStringList lines;
	int fails = 0;
	auto check = [&](const QString& what, bool ok) {
		lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		fails += ok ? 0 : 1;
	};
	auto note = [&](const QString& l) { lines << QStringLiteral("  ") + l; };
	QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
	auto autoDpi = [&cfg](bool on) {
		cfg.setValue(QStringLiteral("autoDpiEnabled"), on);
		cfg.setValue(QStringLiteral("autoDpiTarget"), 300);
		cfg.sync();
	};
	auto select = [doc](PageItem* f) {
		doc->m_Selection->clear();
		doc->m_Selection->addItem(f);
	};
	auto frame = [doc](const QString& name) -> PageItem* {
		for (int i = 0; i < doc->Items->count(); ++i)
			if (doc->Items->at(i)->itemName() == name)
				return doc->Items->at(i);
		return nullptr;
	};
	for (UndoGui* g : mw->findChildren<UndoGui*>())
		UndoManager::instance()->removeGui(g);
	const double px = doc->Pages->at(0)->xOffset(), py = doc->Pages->at(0)->yOffset();
	const QString want[2] = { QStringLiteral("blue(edited)"), QStringLiteral("grey(processed)") };
	const QString names[2] = { QStringLiteral("NEWS"), QStringLiteral("OBIT") };

	if (phase != 2)
	{
		// NEWS: a photo, Edit photo > Apply with the background removed (here: green -> blue)
		const int ia = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, px + 40, py + 60, 200, 150, 1, CommonStrings::None, CommonStrings::None);
		PageItem* a = doc->Items->at(ia);
		a->setItemName(names[0]);
		doc->loadPict(fix + QStringLiteral("/photo.jpg"), a, false, true);
		a->setImageScalingMode(false, true);
		a->adjustPictScale();
		QImage src(fix + QStringLiteral("/photo.jpg"));
		QImage edited = src.convertToFormat(QImage::Format_ARGB32);
		for (int y = 0; y < edited.height(); ++y)
		{
			QRgb* row = reinterpret_cast<QRgb*>(edited.scanLine(y));
			for (int x = 0; x < edited.width(); ++x)
				if (qGreen(row[x]) > 150 && qRed(row[x]) < 110)
					row[x] = qRgb(20, 40, 230);
		}
		SuneerPhotoEdit::Settings s;
		s.removeBg = true;
		s.bgMode = QStringLiteral("auto");
		QString err;
		const bool applied = SuneerPhotoEdit::apply(doc, a, edited, QImage(), QImage(), s, fix + QStringLiteral("/photo.jpg"), &err, QImage(), true);
		note(QStringLiteral("NEWS after Edit photo > Apply: %1, SuneerImgOriginal=%2").arg(QFileInfo(a->Pfile).fileName(), QFileInfo(attrOfItem(a, "SuneerImgOriginal")).fileName()));
		check(QStringLiteral("NEWS: Edit photo applied (%1) and shows the edit (%2)").arg(err.isEmpty() ? QStringLiteral("ok") : err, cornerKind(a)),
		      applied && cornerKind(a) == want[0]);

		// OBIT: placed and resampled once as it was (green), then processed by the obituary pipeline (grey)
		const int ib = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, px + 300, py + 60, 120, 140, 1, CommonStrings::None, CommonStrings::None);
		PageItem* b = doc->Items->at(ib);
		b->setItemName(names[1]);
		doc->loadPict(fix + QStringLiteral("/obit_orig.jpg"), b, false, true);
		b->setImageScalingMode(false, true);
		b->adjustPictScale();
		select(b);
		Options o0;
		o0.allowEnlarge = false;
		o0.targetDpi = 300;
		QString sum0;
		const QString e0 = resample(doc, b, 500, 625, o0, &sum0);
		note(QStringLiteral("OBIT resampled before processing: %1 %2 -> SuneerImgOriginal=%3").arg(e0, sum0, QFileInfo(attrOfItem(b, "SuneerImgOriginal")).fileName()));
		doc->loadPict(fix + QStringLiteral("/obit_out.png"), b, false, true);
		b->setImageScalingMode(false, true);
		b->adjustPictScale();
		setAttrOfItem(b, QStringLiteral("obit.src"), fix + QStringLiteral("/obit_orig.jpg"));
		setAttrOfItem(b, QStringLiteral("obit.out"), fix + QStringLiteral("/obit_out.png"));
		setAttrOfItem(b, QStringLiteral("news.original"), fix + QStringLiteral("/obit_orig.jpg"));
		check(QStringLiteral("OBIT shows the processed picture (%1)").arg(cornerKind(b)), cornerKind(b) == want[1]);

		for (int k = 0; k < 2; ++k)
		{
			PageItem* f = frame(names[k]);
			const QString n = names[k];
			const double w0 = f->width(), h0 = f->height();
			// 1. the handle path: frame smaller, then what the resize gesture does on release (auto-resample on)
			autoDpi(true);
			select(f);
			doc->sizeItem(w0 * 0.5, h0 * 0.5, f, false, true, false);
			ResizeImageDialog::maybeAutoResample(doc);
			check(QStringLiteral("%1 resized by handle (auto-resample ON): %2 - %3").arg(n, cornerKind(f), QFileInfo(f->Pfile).fileName()), cornerKind(f) == want[k]);
			// 2. W/H in the properties (no resample)
			autoDpi(false);
			doc->sizeItem(w0 * 1.3, h0 * 1.3, f, false, true, false);
			f->adjustPictScale();
			check(QStringLiteral("%1 W/H changed (auto-resample OFF): %2").arg(n, cornerKind(f)), cornerKind(f) == want[k]);
			// 3. the image size toolbar (W/H / dpi -> a new file): its resample
			Options o;
			o.allowEnlarge = true;
			o.targetDpi = 150;
			QString sum;
			const Info cur = probe(f);
			const QString e = resample(doc, f, qMax(1, cur.width / 2), qMax(1, cur.height / 2), o, &sum);
			check(QStringLiteral("%1 image size toolbar resample (%2): %3").arg(n, e.isEmpty() ? sum : e, cornerKind(f)), cornerKind(f) == want[k]);
			// 4. bigger again with auto-resample on: more pixels from the edit's own file, never the original
			autoDpi(true);
			select(f);
			doc->sizeItem(w0, h0, f, false, true, false);
			ResizeImageDialog::maybeAutoResample(doc);
			check(QStringLiteral("%1 enlarged again (auto-resample ON): %2 - %3").arg(n, cornerKind(f), QFileInfo(f->Pfile).fileName()), cornerKind(f) == want[k]);
			autoDpi(false);
			// 5. Item > Adjust Frame to Image
			select(f);
			if (mw->scrActions.contains("itemAdjustFrameToImage"))
				mw->scrActions["itemAdjustFrameToImage"]->trigger();
			check(QStringLiteral("%1 Adjust Frame to Image: %2").arg(n, cornerKind(f)), cornerKind(f) == want[k]);
		}
		// Edit photo still opens on the ORIGINAL (with its mask / settings)
		PageItem* a2 = frame(names[0]);
		check(QStringLiteral("NEWS: Edit photo still opens the original (%1), its settings kept (%2)")
		      .arg(QFileInfo(SuneerPhotoEdit::sourcePathOf(a2)).fileName()).arg(!attrOfItem(a2, QString::fromLatin1(SuneerPhotoEdit::AttrSettings)).isEmpty()),
		      QFileInfo(SuneerPhotoEdit::sourcePathOf(a2)).fileName() == QLatin1String("photo.jpg") && !attrOfItem(a2, QString::fromLatin1(SuneerPhotoEdit::AttrSettings)).isEmpty());
		// 6. embedded: the edited picture embedded, then resized + auto-resampled
		const bool emb = SuneerImageLinks::embedItem(doc, a2);
		autoDpi(true);
		select(a2);
		doc->sizeItem(a2->width() * 0.6, a2->height() * 0.6, a2, false, true, false);
		ResizeImageDialog::maybeAutoResample(doc);
		autoDpi(false);
		check(QStringLiteral("NEWS embedded (%1), resized + auto-resampled: %2, still embedded %3").arg(emb).arg(cornerKind(a2)).arg(a2->isInlineImage),
		      emb && cornerKind(a2) == want[0] && a2->isInlineImage);
		// a PLAIN photo (no edit): resampled twice, both times from its own original (no quality pile-up);
		// Edit photo can still open that original
		{
			const int ip = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, px + 40, py + 400, 200, 150, 1, CommonStrings::None, CommonStrings::None);
			PageItem* p = doc->Items->at(ip);
			p->setItemName(QStringLiteral("PLAIN"));
			doc->loadPict(fix + QStringLiteral("/photo.jpg"), p, false, true);
			p->setImageScalingMode(false, true);
			p->adjustPictScale();
			Options o;
			o.targetDpi = 300;
			QString s1, s2;
			resample(doc, p, 800, 600, o, &s1);
			const QString first = p->Pfile;
			resample(doc, p, 400, 300, o, &s2);
			check(QStringLiteral("PLAIN photo resampled twice: both from photo.jpg (2nd from %1), Edit photo opens %2")
			      .arg(QFileInfo(attrOfItem(p, "SuneerResampleOf")).fileName(), QFileInfo(SuneerPhotoEdit::sourcePathOf(p)).fileName()),
			      QFileInfo(attrOfItem(p, "SuneerResampleOf")).fileName() == QLatin1String("photo.jpg") && first != p->Pfile
			      && QFileInfo(SuneerPhotoEdit::sourcePathOf(p)).fileName() == QLatin1String("photo.jpg") && cornerKind(p) == QLatin1String("GREEN(original)"));
		}
		// an OLDER document (no chain record): the processed file is found by the resample copy's name,
		// never the remembered original (OBIT: SuneerImgOriginal = obit_orig.jpg)
		{
			PageItem* b = frame(names[1]);
			ObjAttrVector attrs = *b->getObjectAttributes();
			attrs.erase(std::remove_if(attrs.begin(), attrs.end(), [](const ObjectAttribute& a) { return a.name.startsWith(QLatin1String("SuneerResample")); }), attrs.end());
			b->setObjectAttributes(&attrs);
			check(QStringLiteral("older document: OBIT %1 resamples from %2 (its original %3 is not used)")
			      .arg(QFileInfo(b->Pfile).fileName(), QFileInfo(originalPathOf(b)).fileName(), QFileInfo(attrOfItem(b, "SuneerImgOriginal")).fileName()),
			      QFileInfo(originalPathOf(b)).fileName() == QLatin1String("obit_out.png"));
		}
		mw->grab().save(prefix + QStringLiteral("-phase1.png"));
		doc->setSuneerLinkedImagesNoAsk(true);
		const bool saved = mw->slotFileSave();
		check(QStringLiteral("saved (%1)").arg(doc->documentFileName()), saved);
	}
	else
	{
		// the saved file reopened: what it shows, and a resize again
		for (int k = 0; k < 2; ++k)
		{
			PageItem* f = frame(names[k]);
			check(QStringLiteral("reopened: %1 shows %2 (%3, embedded %4)").arg(names[k], cornerKind(f), f ? QFileInfo(f->Pfile).fileName() : QString()).arg(f && f->isInlineImage),
			      f && cornerKind(f) == want[k]);
			if (!f)
				continue;
			autoDpi(true);
			select(f);
			doc->sizeItem(f->width() * 0.7, f->height() * 0.7, f, false, true, false);
			ResizeImageDialog::maybeAutoResample(doc);
			autoDpi(false);
			check(QStringLiteral("reopened: %1 resized + auto-resampled: %2").arg(names[k], cornerKind(f)), cornerKind(f) == want[k]);
		}
		PageItem* a = frame(names[0]);
		check(QStringLiteral("reopened: NEWS Edit photo source is still the original (%1)").arg(a ? QFileInfo(SuneerPhotoEdit::sourcePathOf(a)).fileName() : QString()),
		      a && QFileInfo(SuneerPhotoEdit::sourcePathOf(a)).fileName() == QLatin1String("photo.jpg"));
		mw->grab().save(prefix + QStringLiteral("-phase2.png"));
		doc->setModified(false);
	}
	lines << (fails ? QStringLiteral("FAILED: %1").arg(fails) : QStringLiteral("ALL PASS"));
	QFile out(prefix + QStringLiteral("-report.txt"));
	if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		out.write(lines.join('\n').toUtf8() + "\n");
	out.close();
	fprintf(stderr, "IMGKEEP-SELFTEST done\n");
	QTimer::singleShot(300, mw, [mw]() { mw->close(); });
}
