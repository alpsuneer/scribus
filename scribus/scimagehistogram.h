/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEHISTOGRAM_H
#define SCIMAGEHISTOGRAM_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QVector>

#include "scribusapi.h"

//! Per-channel + luminance histograms of an image (256 bins each).
struct SCRIBUS_API HistogramData
{
	QVector<int> r;
	QVector<int> g;
	QVector<int> b;
	QVector<int> luminance;
	int totalPixels { 0 };
};

/*!
 \brief Pure image-analysis helpers used by the auto-correction engine.
        No ML, no external dependencies — just counting pixels.
 */
class SCRIBUS_API ImageHistogram
{
public:
	static HistogramData compute(const QImage& image);

	//! Value (bin index) below which \a percent of the pixels lie.
	static int percentile(const QVector<int>& histogram, int totalPixels, double percent);

	//! Black/white clip points for a channel: x = shadow point, y = highlight point.
	static QPoint clipPoints(const QVector<int>& channel, int totalPixels,
	                         double shadowClip = 0.1, double highlightClip = 0.1);

	//! Average colour of the midtone region (luminance 25–75 percentile).
	static QColor averageMidtoneColor(const QImage& image);
};

#endif // SCIMAGEHISTOGRAM_H
