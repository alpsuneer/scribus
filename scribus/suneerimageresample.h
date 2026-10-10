/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERIMAGERESAMPLE_H
#define SUNEERIMAGERESAMPLE_H

#include <QColor>
#include <QString>
#include <QWidget>

#include "scribusapi.h"

class PageItem;
class ScribusDoc;

/*!
 \brief "Image Size" for placed pictures: what the file really is, and
 resampling it to what the page needs - without ever touching the original.

 The resampled file goes to "<docname>_images/<name>_<W>x<H>_<dpi>.<ext>"
 beside the document (beside the picture when the document is unsaved).
 ImageMagick does the resampling when it is installed, so JPEG quality, the
 colour mode (CMYK / Gray), the embedded ICC profile and the EXIF data all
 survive; Qt is the fallback for RGB pictures only. The frame is relinked so
 the page looks exactly as before (scale and offset recalculated), the
 feather effect is rescaled, the wrap contour is kept, and it is one undo step.
 */
namespace SuneerImageResample
{
	struct SCRIBUS_API Info
	{
		bool ok { false };
		int width { 0 };
		int height { 0 };
		double dpiX { 72.0 };
		double dpiY { 72.0 };
		qint64 bytes { 0 };
		QString format;      //!< "JPEG", "PNG", "TIFF", ...
		QString colorMode;   //!< "RGB", "CMYK", "Gray", "Duotone"
		bool alpha { false };
		bool hasProfile { false };
		QString profileName;
		int exifOrientation { 0 };
	};

	//! Resample quality, Photoshop-style.
	enum class Quality { Auto, Sharper, Smoother, Bicubic, Nearest };

	struct SCRIBUS_API Options
	{
		Quality quality { Quality::Auto };   //!< Auto: Sharper when reducing, Smoother when enlarging
		int sharpen { 50 };                  //!< unsharp amount %, Sharper only, luminance channel
		int jpegQuality { 92 };
		int timeoutMs { 120000 };
		bool allowEnlarge { false };
		//! The dpi the picture will be shown at after the caller sets its
		//! scale (file name and density); 0 = keep the current placed size.
		double targetDpi { 0 };
		QWidget* parent { nullptr };
	};

	//! What the frame's picture file is (reads the header only, plus what
	//! the frame's loader already knows: colour space, profile, EXIF).
	SCRIBUS_API Info probe(const PageItem* item);
	//! Header-only probe of a file (pixels, format, size).
	SCRIBUS_API Info probeFile(const QString& path);
	//! The file every resample reads from: the item attribute SuneerImgOriginal
	//! when set (and present), else the frame's current file.
	SCRIBUS_API QString originalPathOf(const PageItem* item);
	//! Effective print resolution of the frame's picture per axis (72 / scale).
	SCRIBUS_API double effectiveDpi(const PageItem* item, double* dpiY = nullptr);
	//! red < 150, orange < 200, green 200-400, blue above 400.
	SCRIBUS_API QColor dpiColor(double dpi);
	SCRIBUS_API QString dpiWord(double dpi);

	SCRIBUS_API bool magickAvailable();
	//! Where the resampled file goes (never an existing path).
	SCRIBUS_API QString outputPathFor(const ScribusDoc* doc, const QString& srcPath, int newW, int newH, int dpi);

	/*! \brief Resample the frame's file to \a newW x \a newH and relink.
	    Returns an empty string on success, else the reason. \a summary gets
	    "6000 x 4000 (8.2 MB) -> 1890 x 1260 (410 KB) @ 300 dpi". */
	SCRIBUS_API QString resample(ScribusDoc* doc, PageItem* item, int newW, int newH, const Options& opt,
	                             QString* summary = nullptr, QString* outPath = nullptr);
	//! The one-click action: exactly the pixels the frame needs at \a targetDpi.
	SCRIBUS_API QString fitToFrame(ScribusDoc* doc, PageItem* item, int targetDpi, const Options& opt,
	                               QString* summary = nullptr, QString* outPath = nullptr);
	//! The quality that Auto resolves to for this change.
	SCRIBUS_API Quality resolveQuality(Quality q, bool reducing);
	SCRIBUS_API QString qualityName(Quality q);
	//! Pixels fitToFrame() would produce.
	SCRIBUS_API void fitPixels(const PageItem* item, int targetDpi, int& newW, int& newH);

	//! Remembered settings (QSettings "Scribus"/"SuneerImageTools").
	SCRIBUS_API int settingInt(const QString& key, int def);
	SCRIBUS_API void setSetting(const QString& key, const QVariant& value);
	SCRIBUS_API QString settingString(const QString& key, const QString& def);

	//! After an image import: when the preference is on and the picture is
	//! far above 400 dpi on the page, offer to fit it to the frame.
	SCRIBUS_API void offerDownsampleAfterImport(ScribusDoc* doc, PageItem* item, QWidget* parent);
}

#endif
