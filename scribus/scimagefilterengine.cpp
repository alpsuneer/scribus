/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimagefilterengine.h"

#include <QStringList>

#include "scimage.h"
#include "sccolor.h"
#include "commonstrings.h"

// ---------------------------------------------------------------------------
// Generic dispatchers
// ---------------------------------------------------------------------------

QImage ImageFilterEngine::runStack(const QImage& in, const ScImageEffectList& effects, ColorList& colors, bool cmyk)
{
	if (in.isNull())
		return in;
	ScImage im(in);
	if (effects.count() > 0)
		im.applyEffect(effects, colors, cmyk);
	// qImage() returns a reference into the ScImage; copy it out so the result
	// remains valid after `im` goes out of scope.
	return im.qImage().copy();
}

QImage ImageFilterEngine::applyEffect(const QImage& in, const ImageEffect& effect, ColorList& colors, bool cmyk)
{
	ScImageEffectList list;
	list.append(effect);
	return runStack(in, list, colors, cmyk);
}

QImage ImageFilterEngine::applyEffects(const QImage& in, const ScImageEffectList& effects, ColorList& colors, bool cmyk)
{
	return runStack(in, effects, colors, cmyk);
}

// ---------------------------------------------------------------------------
// Typed helpers — color-independent effects
// ---------------------------------------------------------------------------

QImage ImageFilterEngine::applyBlur(const QImage& in, int radius)
{
	ColorList colors;
	return applyEffect(in, makeBlur(radius), colors, false);
}

QImage ImageFilterEngine::applySharpen(const QImage& in, double radius, double sigma)
{
	ColorList colors;
	return applyEffect(in, makeSharpen(radius, sigma), colors, false);
}

QImage ImageFilterEngine::applyBrightness(const QImage& in, int value)
{
	ColorList colors;
	return applyEffect(in, makeBrightness(value), colors, false);
}

QImage ImageFilterEngine::applyContrast(const QImage& in, int value)
{
	ColorList colors;
	return applyEffect(in, makeContrast(value), colors, false);
}

QImage ImageFilterEngine::applyGrayscale(const QImage& in)
{
	ColorList colors;
	return applyEffect(in, makeGrayscale(), colors, false);
}

QImage ImageFilterEngine::applyInvert(const QImage& in)
{
	ColorList colors;
	return applyEffect(in, makeInvert(), colors, false);
}

QImage ImageFilterEngine::applyPosterize(const QImage& in, int levels)
{
	ColorList colors;
	return applyEffect(in, makePosterize(levels), colors, false);
}

QImage ImageFilterEngine::applySolarize(const QImage& in, double factor)
{
	ColorList colors;
	return applyEffect(in, makeSolarize(factor), colors, false);
}

QImage ImageFilterEngine::applyCurves(const QImage& in, const CurvesParams& params)
{
	ColorList colors;
	return applyEffect(in, makeGraduate(params), colors, false);
}

QImage ImageFilterEngine::applyLevels(const QImage& in, int inBlack, int inWhite, double gamma, int outBlack, int outWhite)
{
	ColorList colors;
	return applyEffect(in, makeLevels(inBlack, inWhite, gamma, outBlack, outWhite), colors, false);
}

QImage ImageFilterEngine::applyHueSaturation(const QImage& in, int hueShift, int satAdjust, int lightAdjust)
{
	ColorList colors;
	return applyEffect(in, makeHueSaturation(hueShift, satAdjust, lightAdjust), colors, false);
}

QImage ImageFilterEngine::applyColorBalance(const QImage& in, int sr, int sg, int sb, int mr, int mg, int mb, int hr, int hg, int hb, bool preserveLum)
{
	ColorList colors;
	return applyEffect(in, makeColorBalance(sr, sg, sb, mr, mg, mb, hr, hg, hb, preserveLum), colors, false);
}

QImage ImageFilterEngine::applyCmykAdjust(const QImage& in, int cAdj, int mAdj, int yAdj, int kAdj)
{
	ColorList colors;
	return applyEffect(in, makeCmykAdjust(cAdj, mAdj, yAdj, kAdj), colors, false);
}

QImage ImageFilterEngine::applySelectiveColor(const QImage& in, const QVector<int>& adjustments, bool relative)
{
	ColorList colors;
	return applyEffect(in, makeSelectiveColor(adjustments, relative), colors, false);
}

QImage ImageFilterEngine::applyChannelMixer(const QImage& in, const QVector<int>& mix, bool monochrome)
{
	ColorList colors;
	return applyEffect(in, makeChannelMixer(mix, monochrome), colors, false);
}

QImage ImageFilterEngine::applyPhotoFilter(const QImage& in, int fr, int fg, int fb, int density, bool preserveLum)
{
	ColorList colors;
	return applyEffect(in, makePhotoFilter(fr, fg, fb, density, preserveLum), colors, false);
}

QImage ImageFilterEngine::applyThreshold(const QImage& in, int level)
{
	ColorList colors;
	return applyEffect(in, makeThreshold(level), colors, false);
}

QImage ImageFilterEngine::applyBlackWhite(const QImage& in, const QVector<int>& weights, bool tint, int tr, int tg, int tb)
{
	ColorList colors;
	return applyEffect(in, makeBlackWhite(weights, tint, tr, tg, tb), colors, false);
}

QImage ImageFilterEngine::applyMotionBlur(const QImage& in, int angle, int distance)
{
	ColorList colors;
	return applyEffect(in, makeMotionBlur(angle, distance), colors, false);
}

QImage ImageFilterEngine::applyRadialBlur(const QImage& in, int amount, int mode)
{
	ColorList colors;
	return applyEffect(in, makeRadialBlur(amount, mode), colors, false);
}

QImage ImageFilterEngine::applyBoxBlur(const QImage& in, int radius)
{
	ColorList colors;
	return applyEffect(in, makeBoxBlur(radius), colors, false);
}

QImage ImageFilterEngine::applyShadowsHighlights(const QImage& in, int shadowAmount, int shadowTone,
	int highlightAmount, int highlightTone, int radius, int color, int midtone)
{
	ColorList colors;
	return applyEffect(in, makeShadowsHighlights(shadowAmount, shadowTone, highlightAmount, highlightTone,
		radius, color, midtone), colors, false);
}

// ---------------------------------------------------------------------------
// Typed helpers — color effects
// ---------------------------------------------------------------------------

QImage ImageFilterEngine::applyColorize(const QImage& in, ColorList& colors, const ScColor& color, int shade)
{
	// Work on a private copy of the palette so we never mutate the caller's list.
	ColorList work;
	work = colors;
	QString name = work.tryAddColor(QString("FilterEngineColor"), color);

	ImageEffect effect;
	effect.effectCode = ImageEffect::EF_COLORIZE;
	effect.effectParameters = QString("%1\n%2").arg(name).arg(shade);
	return applyEffect(in, effect, work, false);
}

QImage ImageFilterEngine::applyDuotone(const QImage& in, ColorList& colors, const DuotoneParams& p)
{
	ColorList work;
	work = colors;
	QString n1 = work.tryAddColor(QString("FilterEngineColor1"), p.color1);
	QString n2 = work.tryAddColor(QString("FilterEngineColor2"), p.color2);

	ImageEffect effect;
	effect.effectCode = ImageEffect::EF_DUOTONE;
	effect.effectParameters = QString("%1\n%2\n%3 %4 %5 %6")
			.arg(n1, n2)
			.arg(p.shade1).arg(p.shade2)
			.arg(formatCurve(p.curve1, p.linear1), formatCurve(p.curve2, p.linear2));
	return applyEffect(in, effect, work, false);
}

QImage ImageFilterEngine::applyTritone(const QImage& in, ColorList& colors, const TritoneParams& p)
{
	ColorList work;
	work = colors;
	QString n1 = work.tryAddColor(QString("FilterEngineColor1"), p.color1);
	QString n2 = work.tryAddColor(QString("FilterEngineColor2"), p.color2);
	QString n3 = work.tryAddColor(QString("FilterEngineColor3"), p.color3);

	ImageEffect effect;
	effect.effectCode = ImageEffect::EF_TRITONE;
	effect.effectParameters = QString("%1\n%2\n%3\n%4 %5 %6 %7 %8 %9")
			.arg(n1, n2, n3)
			.arg(p.shade1).arg(p.shade2).arg(p.shade3)
			.arg(formatCurve(p.curve1, p.linear1), formatCurve(p.curve2, p.linear2), formatCurve(p.curve3, p.linear3));
	return applyEffect(in, effect, work, false);
}

QImage ImageFilterEngine::applyQuadtone(const QImage& in, ColorList& colors, const QuadtoneParams& p)
{
	ColorList work;
	work = colors;
	QString n1 = work.tryAddColor(QString("FilterEngineColor1"), p.color1);
	QString n2 = work.tryAddColor(QString("FilterEngineColor2"), p.color2);
	QString n3 = work.tryAddColor(QString("FilterEngineColor3"), p.color3);
	QString n4 = work.tryAddColor(QString("FilterEngineColor4"), p.color4);

	ImageEffect effect;
	effect.effectCode = ImageEffect::EF_QUADTONE;
	QString colStr = QString("%1\n%2\n%3\n%4\n").arg(n1, n2, n3, n4);
	QString shadeStr = QString("%1 %2 %3 %4 ").arg(p.shade1).arg(p.shade2).arg(p.shade3).arg(p.shade4);
	QString curveStr = QString("%1 %2 %3 %4")
			.arg(formatCurve(p.curve1, p.linear1), formatCurve(p.curve2, p.linear2),
				 formatCurve(p.curve3, p.linear3), formatCurve(p.curve4, p.linear4));
	effect.effectParameters = colStr + shadeStr + curveStr;
	return applyEffect(in, effect, work, false);
}

// ---------------------------------------------------------------------------
// Effect builders
// ---------------------------------------------------------------------------

ImageEffect ImageFilterEngine::makeBlur(int radius)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_BLUR;
	e.effectParameters = QString("%1 1.0").arg(radius);
	return e;
}

ImageEffect ImageFilterEngine::makeSharpen(double radius, double sigma)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_SHARPEN;
	e.effectParameters = QString("%1 %2").arg(radius).arg(sigma);
	return e;
}

ImageEffect ImageFilterEngine::makeBrightness(int value)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_BRIGHTNESS;
	e.effectParameters = QString::number(value);
	return e;
}

ImageEffect ImageFilterEngine::makeContrast(int value)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_CONTRAST;
	e.effectParameters = QString::number(value);
	return e;
}

ImageEffect ImageFilterEngine::makeGrayscale()
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_GRAYSCALE;
	e.effectParameters = QString();
	return e;
}

ImageEffect ImageFilterEngine::makeInvert()
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_INVERT;
	e.effectParameters = QString();
	return e;
}

ImageEffect ImageFilterEngine::makePosterize(int levels)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_POSTERIZE;
	e.effectParameters = QString::number(levels);
	return e;
}

ImageEffect ImageFilterEngine::makeSolarize(double factor)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_SOLARIZE;
	e.effectParameters = QString::number(factor);
	return e;
}

ImageEffect ImageFilterEngine::makeGraduate(const CurvesParams& params)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_GRADUATE;
	e.effectParameters = formatCurve(params.curve, params.linear);
	return e;
}

ImageEffect ImageFilterEngine::makeLevels(int inBlack, int inWhite, double gamma, int outBlack, int outWhite)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_LEVELS;
	e.effectParameters = QString("%1 %2 %3 %4 %5")
			.arg(inBlack).arg(inWhite).arg(gamma).arg(outBlack).arg(outWhite);
	return e;
}

ImageEffect ImageFilterEngine::makeHueSaturation(int hueShift, int satAdjust, int lightAdjust)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_HUESAT;
	e.effectParameters = QString("%1 %2 %3").arg(hueShift).arg(satAdjust).arg(lightAdjust);
	return e;
}

ImageEffect ImageFilterEngine::makeColorBalance(int sr, int sg, int sb, int mr, int mg, int mb, int hr, int hg, int hb, bool preserveLum)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_COLORBALANCE;
	e.effectParameters = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
			.arg(sr).arg(sg).arg(sb).arg(mr).arg(mg).arg(mb).arg(hr).arg(hg).arg(hb)
			.arg(preserveLum ? 1 : 0);
	return e;
}

ImageEffect ImageFilterEngine::makeCmykAdjust(int cAdj, int mAdj, int yAdj, int kAdj)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_CMYKADJ;
	e.effectParameters = QString("%1 %2 %3 %4").arg(cAdj).arg(mAdj).arg(yAdj).arg(kAdj);
	return e;
}

ImageEffect ImageFilterEngine::makeSelectiveColor(const QVector<int>& adjustments, bool relative)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_SELECTIVECOLOR;
	QStringList parts;
	parts.reserve(37);
	for (int i = 0; i < 36; ++i)
		parts << QString::number(i < adjustments.size() ? adjustments.at(i) : 0);
	parts << QString::number(relative ? 1 : 0);
	e.effectParameters = parts.join(QLatin1Char(' '));
	return e;
}

ImageEffect ImageFilterEngine::makeChannelMixer(const QVector<int>& mix, bool monochrome)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_CHANNELMIXER;
	QStringList parts;
	parts.reserve(17);
	for (int i = 0; i < 16; ++i)
		parts << QString::number(i < mix.size() ? mix.at(i) : 0);
	parts << QString::number(monochrome ? 1 : 0);
	e.effectParameters = parts.join(QLatin1Char(' '));
	return e;
}

ImageEffect ImageFilterEngine::makePhotoFilter(int fr, int fg, int fb, int density, bool preserveLum)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_PHOTOFILTER;
	e.effectParameters = QString("%1 %2 %3 %4 %5")
			.arg(fr).arg(fg).arg(fb).arg(density).arg(preserveLum ? 1 : 0);
	return e;
}

ImageEffect ImageFilterEngine::makeThreshold(int level)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_THRESHOLD;
	e.effectParameters = QString::number(level);
	return e;
}

ImageEffect ImageFilterEngine::makeBlackWhite(const QVector<int>& weights, bool tint, int tr, int tg, int tb)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_BLACKWHITE;
	QStringList parts;
	parts.reserve(10);
	for (int i = 0; i < 6; ++i)
		parts << QString::number(i < weights.size() ? weights.at(i) : 0);
	parts << QString::number(tint ? 1 : 0);
	parts << QString::number(tr) << QString::number(tg) << QString::number(tb);
	e.effectParameters = parts.join(QLatin1Char(' '));
	return e;
}

ImageEffect ImageFilterEngine::makeMotionBlur(int angle, int distance)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_MOTIONBLUR;
	e.effectParameters = QString("%1 %2").arg(angle).arg(distance);
	return e;
}

ImageEffect ImageFilterEngine::makeRadialBlur(int amount, int mode)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_RADIALBLUR;
	e.effectParameters = QString("%1 %2").arg(amount).arg(mode);
	return e;
}

ImageEffect ImageFilterEngine::makeBoxBlur(int radius)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_BOXBLUR;
	e.effectParameters = QString::number(radius);
	return e;
}

ImageEffect ImageFilterEngine::makeShadowsHighlights(int shadowAmount, int shadowTone,
	int highlightAmount, int highlightTone, int radius, int color, int midtone)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_SHADOWHIGHLIGHT;
	e.effectParameters = QString("%1 %2 %3 %4 %5 %6 %7")
			.arg(shadowAmount).arg(shadowTone).arg(highlightAmount).arg(highlightTone)
			.arg(radius).arg(color).arg(midtone);
	return e;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString ImageFilterEngine::formatCurve(const FPointArray& curve, bool linear)
{
	// ScImage expects: numVals x0 y0 x1 y1 ... linearFlag
	// Fall back to an identity ramp for a degenerate (empty) curve.
	if (curve.size() < 2)
		return QString("2 0.0 0.0 1.0 1.0 %1").arg(linear ? 1 : 0);

	QString result = QString::number(curve.size());
	for (int i = 0; i < curve.size(); ++i)
	{
		const FPoint& pt = curve.point(i);
		result += QString(" %1 %2").arg(pt.x()).arg(pt.y());
	}
	result += QString(" %1").arg(linear ? 1 : 0);
	return result;
}
