/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPHOTOEDIT_H
#define SUNEERPHOTOEDIT_H

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QTransform>

#include "scribusapi.h"

class PageItem;
class ScribusDoc;
class QWidget;

/*!
 \brief "Edit photo" for any image frame: crop (frame aspect), rotate, flip,
 optional face-centred start, background removal (rembg) with a refine mask,
 background colour or transparency, tone match. Non-destructive: the ORIGINAL
 file is kept (attribute SuneerImgOriginal), the result is a new PNG next to
 the document, the settings live on the frame (attribute suneer.photoEdit) so
 the dialog reopens with the same state, and the frame is marked manually
 edited so automatic obituary re-processing leaves it alone.
 */
namespace SuneerPhotoEdit
{
	extern const char* const AttrSettings;   //!< JSON of Settings
	extern const char* const AttrManual;     //!< "1" after an Apply
	extern const char* const AttrCut;        //!< rembg cut-out PNG (RGBA, source size, source orientation)
	extern const char* const AttrBrush;      //!< refine mask PNG (Grayscale8, source size; 0 erase, 255 restore, 128 untouched)
	extern const char* const AttrOutput;     //!< the processed file written by the last Apply
	extern const char* const AttrProtect;    //!< protect / exclude mask PNG (Grayscale8, source size; 255 always kept, 0 always removed, 128 none)
	extern const char* const AttrBgMode;     //!< "auto" (AI), "manual" (lasso / brushes only), "none"

	struct Settings
	{
		int rot90 { 0 };            // 0, 90, 180, 270 (clockwise)
		double fine { 0.0 };        // -10 .. 10 degrees
		bool flipH { false };
		bool flipV { false };
		QRectF crop;                // in the transformed image's pixels (null = whole)
		bool freeAspect { false };  // crop may change the frame's shape
		bool autoFace { false };
		bool removeBg { false };
		QString bgMode { QStringLiteral("auto") };   // "auto" = rembg, "manual" = no AI, mask by hand, "none" = no background change
		QString model { QStringLiteral("u2net_human_seg") };
		QString bgColor;            // "#rrggbb" or empty = transparent
		QString bgName;             // document colour name the frame fill takes ("Obit BG")
		int tone { 0 };             // 0 = off, 1..100 strength
		int refine { 0 };           // 0 = off, 1..100: refine the cut-out's edge (hair) against the background
		QRectF face;                // detected face box in SOURCE pixels (unrotated), null = none
		QRect cutRect;              // SOURCE pixels the stored cut-out covers (crop + margin); null = whole picture
		QString cutModel;           // the model the stored cut-out was made with

		QJsonObject toJson() const;
		static Settings fromJson(const QJsonObject& o);
	};

	//! The picture every edit starts from: SuneerImgOriginal, else the BG tool's source, else Pfile.
	SCRIBUS_API QString sourcePathOf(const PageItem* item);
	SCRIBUS_API QImage loadSource(const QString& path, QString* error = nullptr);
	//! The settings: the item attribute, else the sidecar "<output>.json" next to the edited file.
	SCRIBUS_API Settings settingsOf(const PageItem* item);
	//! The sidecar next to \a outputPath ("<output>.json"): settings + the file paths, for other PCs.
	SCRIBUS_API QString sidecarPathFor(const QString& outputPath);
	//! True when the source is a local file in the document's photos folder (original/), i.e. never the network.
	SCRIBUS_API bool sourceIsLocalOriginal(const PageItem* item, const QString& sourcePath);
	/*! The crop (transformed pixels) mapped back to SOURCE pixels with 10 % margin on
	    each side, clamped to the picture: the only part rembg sees. */
	SCRIBUS_API QRect cropRegionInSource(const Settings& s, const QSize& sourceSize);
	SCRIBUS_API bool isManual(const PageItem* item);
	//! True when the frame is an obituary-model photo (news.model obituary, role photo).
	SCRIBUS_API bool isObitPhoto(const PageItem* item);
	//! The obituary presets (auto face, remove bg, Obit BG, tone) for a fresh obit photo.
	SCRIBUS_API Settings obitPreset(const ScribusDoc* doc);

	//! source -> transformed image (rot90, fine, flips). Also used for the crop coordinates.
	SCRIBUS_API QTransform transformFor(const Settings& s, const QSize& sourceSize, QSize* outSize = nullptr);
	//! Largest frontal face in \a path via the obituary script (OpenCV Haar); null when none / no script.
	SCRIBUS_API QRectF detectFace(const QString& path, QString* error = nullptr);
	//! The obituary crop rule in transformed pixels: face box 60 % of the width, centred, top at 18 %.
	SCRIBUS_API QRectF faceCrop(const QRectF& faceInTransformed, double aspectHoverW, const QSize& imageSize);
	//! Same-tone portrait (C++ port of the obituary script's tone_match). Alpha is kept.
	SCRIBUS_API QImage toneMatch(const QImage& img, const QRectF& face, double strength);

	/*! Render the result at full resolution.
	    \param source  ARGB32, source orientation
	    \param cut     rembg RGBA cut-out, source size (null when removeBg is off / not run yet)
	    \param brush   Grayscale8 refine mask, source size (null = none)
	    \param preview when true the image is reduced to ~1400 px first (fast); the crop is scaled with it
	    \param cropOut the crop actually used (transformed pixels, after clamping) */
	SCRIBUS_API QImage render(const QImage& source, const QImage& cut, const QImage& brush, const Settings& s, bool preview, QRectF* cropOut = nullptr, const QImage& protect = QImage());
	//! The whole transformed image with alpha/tone/background applied, no crop (the dialog's canvas).
	SCRIBUS_API QImage renderWhole(const QImage& source, const QImage& cut, const QImage& brush, const Settings& s, bool preview, QSize* fullSize = nullptr, const QImage& protect = QImage());
	/*! "Smart restore": inside \a coverage (Alpha/Gray8 0..255 of the selection),
	    the weight 0..255 with which a pixel belongs to the subject rather than
	    the background, from the colours the cut-out already keeps / removes
	    inside the selection (colour distance to the two colour sets). */
	SCRIBUS_API QImage smartRestoreWeights(const QImage& source, const QImage& cut, const QImage& coverage);
	/*! The alpha map in SOURCE pixels from the cut-out (or fully opaque for the
	    manual mode), the brush mask and the protect mask. The RGB pixels are
	    never touched: the result is composed as original RGB x alpha. */
	SCRIBUS_API QImage alphaMap(const QSize& size, const QImage& cut, const QImage& brush, const QImage& protect, bool manualOpaque);
	//! Apply protect/exclude (255 keep, 0 remove) onto the alpha of \a img (same size) in place.
	SCRIBUS_API void applyProtectToAlpha(QImage& img, const QImage& protect);

	// ---- mask editing (the refine mask is Grayscale8, source size: 0 erase, 255 restore, 128 untouched)
	//! One soft round stamp: hardness 0..1 (1 = hard edge), target 0 or 255, opacity 0..1.
	SCRIBUS_API void stampSoft(QImage& mask, const QPointF& centre, double radius, double hardness, int target, double opacity = 1.0);
	//! Stamps along the segment a-b with the given spacing (fast strokes stay continuous).
	SCRIBUS_API void stampSegment(QImage& mask, const QPointF& a, const QPointF& b, double radius, double hardness, int target, double spacing);
	/*! Set the mask inside (or outside) a closed path (source pixels) to target,
	    with a feathered, anti-aliased edge of featherPx. */
	SCRIBUS_API void applyPathToMask(QImage& mask, const QPainterPath& path, double featherPx, int target, bool outside);
	//! Alpha8 coverage of a path (anti-aliased), optionally feathered.
	SCRIBUS_API QImage pathCoverage(const QPainterPath& path, const QSize& size, double featherPx);
	//! The rembg cut-out with its edge refined (strength 1..100): matte smoothing + colour decontamination, no halo.
	SCRIBUS_API QImage refineEdge(const QImage& cut, int strength);
	//! Apply mask (Grayscale8) to the alpha of img in place (same size): 255 -> opaque, 0 -> transparent, in between blended.
	SCRIBUS_API void applyMaskToAlpha(QImage& img, const QImage& mask, const QImage& cutAlphaRef);

	//! Where the next processed file goes: <doc dir>/<doc>_images/<source base>_edit<N>.png
	SCRIBUS_API QString outputPathFor(const ScribusDoc* doc, const QString& sourcePath, const QString& tag = QStringLiteral("_edit"));

	/*! Apply: write \a result (and the cut/brush files), relink the frame, fit the
	    picture to the frame (free aspect: the frame takes the crop's shape, same
	    width), keep the wrap contour, clear the orange flag, store the settings,
	    mark manual. One undo step. */
	SCRIBUS_API bool apply(ScribusDoc* doc, PageItem* item, const QImage& result, const QImage& cut, const QImage& brush,
	                       const Settings& s, const QString& sourcePath, QString* error = nullptr, const QImage& protect = QImage(), bool manual = true);
	//! Back to the original file, settings cleared, not manual any more. One undo step.
	SCRIBUS_API bool resetToOriginal(ScribusDoc* doc, PageItem* item, QString* error = nullptr);

	//! Open the dialog for \a item (the selected frame, or the sole image in a selected group).
	SCRIBUS_API void openDialog(ScribusDoc* doc, PageItem* item, QWidget* parent);
	//! The "Photo background colour" dialog for the selection (or every obit photo of the page).
	SCRIBUS_API void openBackgroundDialog(ScribusDoc* doc, QWidget* parent);
}

#endif
