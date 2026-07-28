/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "autocorrectengine.h"

#include <cmath>

#include <QString>

#include "scimagehistogram.h"
#include "sctextstream.h"

namespace
{
	QImage ensureRGB(const QImage& in)
	{
		if (in.format() == QImage::Format_ARGB32 || in.format() == QImage::Format_RGB32)
			return in.copy();
		return in.convertToFormat(QImage::Format_ARGB32);
	}

	//! LUT that linearly stretches [lo, hi] to [0, 255].
	void buildStretchLut(unsigned char lut[256], int lo, int hi)
	{
		if (hi <= lo)
			hi = lo + 1;
		const double range = hi - lo;
		for (int i = 0; i < 256; ++i)
			lut[i] = static_cast<unsigned char>(qBound(0, qRound((i - lo) * 255.0 / range), 255));
	}

	//! Gamma LUT that maps input value \a avg onto \a target (endpoints fixed).
	void buildNeutralLut(unsigned char lut[256], int avg, int target)
	{
		avg = qBound(1, avg, 254);
		target = qBound(1, target, 254);
		double denom = std::log(avg / 255.0);
		if (std::fabs(denom) < 1e-6)
		{
			for (int i = 0; i < 256; ++i)
				lut[i] = static_cast<unsigned char>(i);
			return;
		}
		double e = std::log(target / 255.0) / denom;   // (avg/255)^e == target/255
		for (int i = 0; i < 256; ++i)
		{
			double v = 255.0 * std::pow(i / 255.0, e);
			lut[i] = static_cast<unsigned char>(qBound(0, qRound(v), 255));
		}
	}

	void applyLuts(QImage& img, const unsigned char* lutR, const unsigned char* lutG, const unsigned char* lutB)
	{
		int w = img.width();
		int h = img.height();
		for (int y = 0; y < h; ++y)
		{
			QRgb* s = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < w; ++x)
			{
				QRgb p = s[x];
				s[x] = qRgba(lutR[qRed(p)], lutG[qGreen(p)], lutB[qBlue(p)], qAlpha(p));
			}
		}
	}

	QImage perChannelStretch(const QImage& in, double sClip, double hClip)
	{
		QImage img = ensureRGB(in);
		HistogramData d = ImageHistogram::compute(img);
		QPoint rp = ImageHistogram::clipPoints(d.r, d.totalPixels, sClip, hClip);
		QPoint gp = ImageHistogram::clipPoints(d.g, d.totalPixels, sClip, hClip);
		QPoint bp = ImageHistogram::clipPoints(d.b, d.totalPixels, sClip, hClip);
		unsigned char lr[256], lg[256], lb[256];
		buildStretchLut(lr, rp.x(), rp.y());
		buildStretchLut(lg, gp.x(), gp.y());
		buildStretchLut(lb, bp.x(), bp.y());
		applyLuts(img, lr, lg, lb);
		return img;
	}

	QImage luminanceStretch(const QImage& in, double sClip, double hClip, double strength)
	{
		QImage img = ensureRGB(in);
		HistogramData d = ImageHistogram::compute(img);
		QPoint lp = ImageHistogram::clipPoints(d.luminance, d.totalPixels, sClip, hClip);
		unsigned char full[256], lut[256];
		buildStretchLut(full, lp.x(), lp.y());
		for (int i = 0; i < 256; ++i)   // blend toward identity by (1 - strength)
			lut[i] = static_cast<unsigned char>(qBound(0, qRound(i + (full[i] - i) * strength), 255));
		applyLuts(img, lut, lut, lut);
		return img;
	}

	QImage neutralizeMidtones(const QImage& in, int target)
	{
		QImage img = ensureRGB(in);
		QColor mid = ImageHistogram::averageMidtoneColor(img);
		unsigned char lr[256], lg[256], lb[256];
		buildNeutralLut(lr, mid.red(), target);
		buildNeutralLut(lg, mid.green(), target);
		buildNeutralLut(lb, mid.blue(), target);
		applyLuts(img, lr, lg, lb);
		return img;
	}

	//! Skin membership in [0,1] from the HSV heuristic (soft edges avoided; binary).
	double skinMask(int r, int g, int b)
	{
		int mx = qMax(r, qMax(g, b));
		int mn = qMin(r, qMin(g, b));
		double v = mx / 255.0;
		double s = (mx == 0) ? 0.0 : (mx - mn) / static_cast<double>(mx);
		double chroma = mx - mn;
		double h = 0.0;
		if (chroma > 0)
		{
			if (mx == r)
				h = 60.0 * std::fmod((g - b) / chroma, 6.0);
			else if (mx == g)
				h = 60.0 * ((b - r) / chroma + 2.0);
			else
				h = 60.0 * ((r - g) / chroma + 4.0);
			if (h < 0)
				h += 360.0;
		}
		const bool inHue = (h <= 50.0) || (h >= 340.0);
		if (inHue && s >= 0.15 && s <= 0.68 && v >= 0.35)
			return 1.0;
		return 0.0;
	}

	//! Blend \a corrected back toward \a original in skin regions.
	QImage protectSkin(const QImage& original, const QImage& corrected)
	{
		QImage orig = ensureRGB(original);
		QImage out = corrected;   // already ARGB32 from the pipeline
		int w = out.width();
		int h = out.height();
		for (int y = 0; y < h; ++y)
		{
			const QRgb* o = reinterpret_cast<const QRgb*>(orig.constScanLine(y));
			QRgb* c = reinterpret_cast<QRgb*>(out.scanLine(y));
			for (int x = 0; x < w; ++x)
			{
				double m = skinMask(qRed(o[x]), qGreen(o[x]), qBlue(o[x]));
				if (m <= 0.0)
					continue;
				double wcorr = 1.0 - m;   // less correction in skin
				int nr = qRound(qRed(o[x]) + (qRed(c[x]) - qRed(o[x])) * wcorr);
				int ng = qRound(qGreen(o[x]) + (qGreen(c[x]) - qGreen(o[x])) * wcorr);
				int nb = qRound(qBlue(o[x]) + (qBlue(c[x]) - qBlue(o[x])) * wcorr);
				c[x] = qRgba(qBound(0, nr, 255), qBound(0, ng, 255), qBound(0, nb, 255), qAlpha(c[x]));
			}
		}
		return out;
	}
}

// ---------------------------------------------------------------------------
// Public algorithms
// ---------------------------------------------------------------------------

QImage AutoCorrectEngine::autoTone(const QImage& in)
{
	return perChannelStretch(in, 0.1, 0.1);
}

QImage AutoCorrectEngine::autoContrast(const QImage& in)
{
	return luminanceStretch(in, 0.1, 0.1, 1.0);
}

QImage AutoCorrectEngine::autoColor(const QImage& in)
{
	QImage img = perChannelStretch(in, 0.1, 0.1);
	return neutralizeMidtones(img, 128);
}

QImage AutoCorrectEngine::autoEnhanceCombined(const QImage& in, const AutoCorrectOptions& opts)
{
	QImage result;
	switch (opts.algorithm)
	{
		case AutoCorrectOptions::MonochromaticContrast:
			result = luminanceStretch(in, opts.shadowClip, opts.highlightClip, 1.0);
			break;
		case AutoCorrectOptions::PerChannelContrast:
			result = perChannelStretch(in, opts.shadowClip, opts.highlightClip);
			break;
		case AutoCorrectOptions::EnhanceBrightnessContrast:
			result = luminanceStretch(in, opts.shadowClip, opts.highlightClip, 0.5);
			break;
		case AutoCorrectOptions::FindDarkAndLightColors:
		default:
			result = perChannelStretch(in, opts.shadowClip, opts.highlightClip);
			break;
	}
	if (opts.snapNeutralMidtones)
		result = neutralizeMidtones(result, opts.midtoneTarget);
	if (opts.protectSkinTones)
		result = protectSkin(in, result);
	return result;
}

QImage AutoCorrectEngine::autoCmykOptimize(const QImage& in, const CmykOptimizeOptions& opts)
{
	QImage img = ensureRGB(in);
	const int limitBytes = qBound(0, qRound(opts.totalInkLimit / 100.0 * 255.0), 1020);
	const double ucr = qBound(0.0, opts.ucrAmount, 1.0);
	int w = img.width();
	int h = img.height();
	for (int y = 0; y < h; ++y)
	{
		QRgb* s = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < w; ++x)
		{
			QRgb p = s[x];
			int r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);
			int k = 255 - qMax(r, qMax(g, b));
			int c, m, yv;
			if (k >= 255)
			{
				c = 0; m = 0; yv = 0;
			}
			else
			{
				c = (255 - r - k) * 255 / (255 - k);
				m = (255 - g - k) * 255 / (255 - k);
				yv = (255 - b - k) * 255 / (255 - k);
			}

			if (opts.applyUCR)
			{
				// Move the grey (achromatic) component of C,M,Y into K.
				int grey = qMin(c, qMin(m, yv));
				int move = qRound(grey * ucr);
				c -= move; m -= move; yv -= move;
				k = qBound(0, k + move, 255);
			}
			// If still over the ink limit, scale C,M,Y (not K) down to fit.
			int total = c + m + yv + k;
			if (total > limitBytes && (c + m + yv) > 0)
			{
				double scale = static_cast<double>(limitBytes - k) / (c + m + yv);
				scale = qBound(0.0, scale, 1.0);
				c = qRound(c * scale);
				m = qRound(m * scale);
				yv = qRound(yv * scale);
			}
			c = qBound(0, c, 255); m = qBound(0, m, 255); yv = qBound(0, yv, 255);

			int nr = (255 - c) * (255 - k) / 255;
			int ng = (255 - m) * (255 - k) / 255;
			int nb = (255 - yv) * (255 - k) / 255;
			s[x] = qRgba(nr, ng, nb, a);
		}
	}
	return img;
}

// ---------------------------------------------------------------------------
// Effect builders / parsers
// ---------------------------------------------------------------------------

ImageEffect AutoCorrectEngine::makeAutoTone()
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_AUTOTONE;
	e.effectParameters = QString();
	return e;
}

ImageEffect AutoCorrectEngine::makeAutoContrast()
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_AUTOCONTRAST;
	e.effectParameters = QString();
	return e;
}

ImageEffect AutoCorrectEngine::makeAutoColor()
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_AUTOCOLOR;
	e.effectParameters = QString();
	return e;
}

ImageEffect AutoCorrectEngine::makeAutoEnhance(const AutoCorrectOptions& opts)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_AUTOENHANCE;
	e.effectParameters = QString("%1 %2 %3 %4 %5 %6")
			.arg(static_cast<int>(opts.algorithm))
			.arg(opts.snapNeutralMidtones ? 1 : 0)
			.arg(opts.protectSkinTones ? 1 : 0)
			.arg(opts.shadowClip)
			.arg(opts.highlightClip)
			.arg(opts.midtoneTarget);
	return e;
}

ImageEffect AutoCorrectEngine::makeAutoCmyk(const CmykOptimizeOptions& opts)
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_AUTOCMYK;
	e.effectParameters = QString("%1 %2 %3")
			.arg(opts.totalInkLimit)
			.arg(opts.applyUCR ? 1 : 0)
			.arg(opts.ucrAmount);
	return e;
}

AutoCorrectOptions AutoCorrectEngine::parseEnhance(const QString& params)
{
	AutoCorrectOptions o;
	QString s = params;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int algo = static_cast<int>(o.algorithm), snap = 1, protect = 1, target = 128;
	double sClip = o.shadowClip, hClip = o.highlightClip;
	fp >> algo;
	fp >> snap;
	fp >> protect;
	fp >> sClip;
	fp >> hClip;
	fp >> target;
	o.algorithm = static_cast<AutoCorrectOptions::Algorithm>(qBound(0, algo, 3));
	o.snapNeutralMidtones = (snap != 0);
	o.protectSkinTones = (protect != 0);
	o.shadowClip = sClip;
	o.highlightClip = hClip;
	o.midtoneTarget = target;
	return o;
}

CmykOptimizeOptions AutoCorrectEngine::parseCmyk(const QString& params)
{
	CmykOptimizeOptions o;
	QString s = params;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int limit = o.totalInkLimit, ucrFlag = 1;
	double amt = o.ucrAmount;
	fp >> limit;
	fp >> ucrFlag;
	fp >> amt;
	o.totalInkLimit = limit;
	o.applyUCR = (ucrFlag != 0);
	o.ucrAmount = amt;
	return o;
}
