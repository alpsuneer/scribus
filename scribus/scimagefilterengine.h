/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEFILTERENGINE_H
#define SCIMAGEFILTERENGINE_H

#include <QImage>
#include <QString>
#include <QVector>

#include "scribusapi.h"
#include "fpointarray.h"
#include "sccolor.h"
#include "scimagestructs.h"

class ColorList;

/*!
 \brief Parameters for a single tone curve (used by the multi-tone effects and
        the Curves adjustment). \a curve is a normalised control-point array;
        \a linear selects linear vs. spline interpolation.
 */
struct SCRIBUS_API CurvesParams
{
	FPointArray curve;
	bool linear { true };
};

struct SCRIBUS_API DuotoneParams
{
	ScColor     color1;
	int         shade1 { 100 };
	FPointArray curve1;
	bool        linear1 { true };
	ScColor     color2;
	int         shade2 { 100 };
	FPointArray curve2;
	bool        linear2 { true };
};

struct SCRIBUS_API TritoneParams
{
	ScColor     color1;
	int         shade1 { 100 };
	FPointArray curve1;
	bool        linear1 { true };
	ScColor     color2;
	int         shade2 { 100 };
	FPointArray curve2;
	bool        linear2 { true };
	ScColor     color3;
	int         shade3 { 100 };
	FPointArray curve3;
	bool        linear3 { true };
};

struct SCRIBUS_API QuadtoneParams
{
	ScColor     color1;
	int         shade1 { 100 };
	FPointArray curve1;
	bool        linear1 { true };
	ScColor     color2;
	int         shade2 { 100 };
	FPointArray curve2;
	bool        linear2 { true };
	ScColor     color3;
	int         shade3 { 100 };
	FPointArray curve3;
	bool        linear3 { true };
	ScColor     color4;
	int         shade4 { 100 };
	FPointArray curve4;
	bool        linear4 { true };
};

/*!
 \brief Stateless engine that applies Scribus image effects to a plain QImage.

 This is a thin, reusable wrapper around ScImage's effect pipeline. It lets the
 new ScImageEditor (and any other client) run the exact same, well-tested
 algorithms EffectsDialog uses, without duplicating the per-pixel code.

 Every method takes a QImage in and returns a new QImage out; the input is never
 modified. The color effects need a document ColorList for CMS / named-color
 resolution but do not mutate it (they operate on a private copy).
 */
class SCRIBUS_API ImageFilterEngine
{
public:
	// --- Generic dispatchers -------------------------------------------------
	//! Apply a single effect. \a colors provides doc/CMS context for color effects.
	static QImage applyEffect(const QImage& in, const ImageEffect& effect, ColorList& colors, bool cmyk = false);
	//! Apply a whole stack, in order (top of list applied first, matching Scribus).
	static QImage applyEffects(const QImage& in, const ScImageEffectList& effects, ColorList& colors, bool cmyk = false);

	// --- Typed helpers: color-independent effects ---------------------------
	static QImage applyBlur(const QImage& in, int radius);
	static QImage applySharpen(const QImage& in, double radius, double sigma);
	static QImage applyBrightness(const QImage& in, int value);
	static QImage applyContrast(const QImage& in, int value);
	static QImage applyGrayscale(const QImage& in);
	static QImage applyInvert(const QImage& in);
	static QImage applyPosterize(const QImage& in, int levels);
	static QImage applySolarize(const QImage& in, double factor);
	static QImage applyCurves(const QImage& in, const CurvesParams& params);
	static QImage applyLevels(const QImage& in, int inBlack, int inWhite, double gamma, int outBlack, int outWhite);
	static QImage applyHueSaturation(const QImage& in, int hueShift, int satAdjust, int lightAdjust);
	static QImage applyColorBalance(const QImage& in, int sr, int sg, int sb, int mr, int mg, int mb, int hr, int hg, int hb, bool preserveLum);
	static QImage applyCmykAdjust(const QImage& in, int cAdj, int mAdj, int yAdj, int kAdj);
	//! adjustments: 36 ints, range-major (9 ranges × C,M,Y,K).
	static QImage applySelectiveColor(const QImage& in, const QVector<int>& adjustments, bool relative);
	//! mix: 16 ints — outR/outG/outB/mono each {r,g,b,const}.
	static QImage applyChannelMixer(const QImage& in, const QVector<int>& mix, bool monochrome);
	static QImage applyPhotoFilter(const QImage& in, int fr, int fg, int fb, int density, bool preserveLum);
	static QImage applyThreshold(const QImage& in, int level);
	//! weights: 6 ints (Reds, Yellows, Greens, Cyans, Blues, Magentas).
	static QImage applyBlackWhite(const QImage& in, const QVector<int>& weights, bool tint, int tr, int tg, int tb);
	static QImage applyMotionBlur(const QImage& in, int angle, int distance);
	static QImage applyRadialBlur(const QImage& in, int amount, int mode);
	static QImage applyBoxBlur(const QImage& in, int radius);
	static QImage applyShadowsHighlights(const QImage& in, int shadowAmount, int shadowTone,
		int highlightAmount, int highlightTone, int radius, int color, int midtone);

	// --- Typed helpers: color effects (need a doc ColorList for context) -----
	static QImage applyColorize(const QImage& in, ColorList& colors, const ScColor& color, int shade);
	static QImage applyDuotone(const QImage& in, ColorList& colors, const DuotoneParams& params);
	static QImage applyTritone(const QImage& in, ColorList& colors, const TritoneParams& params);
	static QImage applyQuadtone(const QImage& in, ColorList& colors, const QuadtoneParams& params);

	// --- Effect builders -----------------------------------------------------
	// Construct the ImageEffect (effectCode + effectParameters string) that
	// ScImage::applyEffect understands. Handy for building an effect stack that
	// is stored non-destructively in PageItem::effectsInUse.
	static ImageEffect makeBlur(int radius);
	static ImageEffect makeSharpen(double radius, double sigma);
	static ImageEffect makeBrightness(int value);
	static ImageEffect makeContrast(int value);
	static ImageEffect makeGrayscale();
	static ImageEffect makeInvert();
	static ImageEffect makePosterize(int levels);
	static ImageEffect makeSolarize(double factor);
	static ImageEffect makeGraduate(const CurvesParams& params);
	static ImageEffect makeLevels(int inBlack, int inWhite, double gamma, int outBlack, int outWhite);
	static ImageEffect makeHueSaturation(int hueShift, int satAdjust, int lightAdjust);
	static ImageEffect makeColorBalance(int sr, int sg, int sb, int mr, int mg, int mb, int hr, int hg, int hb, bool preserveLum);
	static ImageEffect makeCmykAdjust(int cAdj, int mAdj, int yAdj, int kAdj);
	static ImageEffect makeSelectiveColor(const QVector<int>& adjustments, bool relative);
	static ImageEffect makeChannelMixer(const QVector<int>& mix, bool monochrome);
	static ImageEffect makePhotoFilter(int fr, int fg, int fb, int density, bool preserveLum);
	static ImageEffect makeThreshold(int level);
	static ImageEffect makeBlackWhite(const QVector<int>& weights, bool tint, int tr, int tg, int tb);
	static ImageEffect makeMotionBlur(int angle, int distance);
	static ImageEffect makeRadialBlur(int amount, int mode);
	static ImageEffect makeBoxBlur(int radius);
	static ImageEffect makeShadowsHighlights(int shadowAmount, int shadowTone,
		int highlightAmount, int highlightTone, int radius, int color, int midtone);

	//! Serialise a curve as "numVals x0 y0 x1 y1 ... linearFlag" (ScImage format).
	static QString formatCurve(const FPointArray& curve, bool linear);

private:
	//! Run a prepared effect stack through ScImage and return the RGB result.
	static QImage runStack(const QImage& in, const ScImageEffectList& effects, ColorList& colors, bool cmyk);
};

#endif // SCIMAGEFILTERENGINE_H
