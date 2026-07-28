/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimagehistogram.h"

#include <QtGlobal>

namespace
{
	QImage ensureRGB(const QImage& in)
	{
		if (in.format() == QImage::Format_ARGB32 || in.format() == QImage::Format_RGB32)
			return in;
		return in.convertToFormat(QImage::Format_ARGB32);
	}
}

HistogramData ImageHistogram::compute(const QImage& image)
{
	HistogramData data;
	data.r = QVector<int>(256, 0);
	data.g = QVector<int>(256, 0);
	data.b = QVector<int>(256, 0);
	data.luminance = QVector<int>(256, 0);

	QImage img = ensureRGB(image);
	int w = img.width();
	int h = img.height();
	for (int y = 0; y < h; ++y)
	{
		const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; ++x)
		{
			QRgb p = s[x];
			int r = qRed(p), g = qGreen(p), b = qBlue(p);
			data.r[r]++;
			data.g[g]++;
			data.b[b]++;
			int lum = qBound(0, qRound(0.299 * r + 0.587 * g + 0.114 * b), 255);
			data.luminance[lum]++;
		}
	}
	data.totalPixels = w * h;
	return data;
}

int ImageHistogram::percentile(const QVector<int>& histogram, int totalPixels, double percent)
{
	if (totalPixels <= 0 || histogram.isEmpty())
		return 0;
	const double target = totalPixels * percent / 100.0;
	long long cumulative = 0;
	for (int i = 0; i < histogram.size(); ++i)
	{
		cumulative += histogram[i];
		if (cumulative >= target)
			return i;
	}
	return histogram.size() - 1;
}

QPoint ImageHistogram::clipPoints(const QVector<int>& channel, int totalPixels,
                                  double shadowClip, double highlightClip)
{
	int black = percentile(channel, totalPixels, shadowClip);
	int white = percentile(channel, totalPixels, 100.0 - highlightClip);
	if (white <= black)
		white = qMin(255, black + 1);
	return QPoint(black, white);
}

QColor ImageHistogram::averageMidtoneColor(const QImage& image)
{
	QImage img = ensureRGB(image);
	HistogramData data = compute(img);
	int loLum = percentile(data.luminance, data.totalPixels, 25.0);
	int hiLum = percentile(data.luminance, data.totalPixels, 75.0);
	if (hiLum < loLum)
		qSwap(loLum, hiLum);

	long long sr = 0, sg = 0, sb = 0, count = 0;
	int w = img.width();
	int h = img.height();
	for (int y = 0; y < h; ++y)
	{
		const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; ++x)
		{
			QRgb p = s[x];
			int r = qRed(p), g = qGreen(p), b = qBlue(p);
			int lum = qBound(0, qRound(0.299 * r + 0.587 * g + 0.114 * b), 255);
			if (lum >= loLum && lum <= hiLum)
			{
				sr += r;
				sg += g;
				sb += b;
				++count;
			}
		}
	}
	if (count == 0)
		return QColor(128, 128, 128);
	return QColor(static_cast<int>(sr / count), static_cast<int>(sg / count), static_cast<int>(sb / count));
}
