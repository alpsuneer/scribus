/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerimageresample.h"
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

QString SuneerImageResample::originalPathOf(const PageItem* item)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> list = item->getObjectAttributes(QStringLiteral("SuneerImgOriginal"));
	if (!list.isEmpty() && QFileInfo::exists(list.first().value))
		return list.first().value;
	return item->Pfile;
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
	if (item->getObjectAttributes(QStringLiteral("SuneerImgOriginal")).isEmpty() && !wasInline)
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
