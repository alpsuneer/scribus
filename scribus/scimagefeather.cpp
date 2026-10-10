/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimagefeather.h"

#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	// Separable minimum filter of radius r (square window): shrinks the
	// opaque region so the blur that follows fades inside the outline.
	void erode(QVector<uchar>& a, int w, int h, int r)
	{
		if (r <= 0)
			return;
		QVector<uchar> tmp(a.size());
		for (int y = 0; y < h; ++y)
		{
			const uchar* src = a.constData() + qsizetype(y) * w;
			uchar* dst = tmp.data() + qsizetype(y) * w;
			for (int x = 0; x < w; ++x)
			{
				int m = 255;
				const int x0 = qMax(0, x - r), x1 = qMin(w - 1, x + r);
				for (int k = x0; k <= x1; ++k)
					m = qMin(m, int(src[k]));
				dst[x] = uchar(m);
			}
		}
		for (int x = 0; x < w; ++x)
		{
			for (int y = 0; y < h; ++y)
			{
				int m = 255;
				const int y0 = qMax(0, y - r), y1 = qMin(h - 1, y + r);
				for (int k = y0; k <= y1; ++k)
					m = qMin(m, int(tmp[qsizetype(k) * w + x]));
				a[qsizetype(y) * w + x] = uchar(m);
			}
		}
	}

	// Three passes of a separable box blur of radius r: close to a Gaussian
	// of sigma ~ r, and O(w*h) per pass whatever the radius.
	void boxBlurPass(QVector<uchar>& a, int w, int h, int r)
	{
		if (r <= 0)
			return;
		QVector<uchar> tmp(a.size());
		const int win = 2 * r + 1;
		for (int y = 0; y < h; ++y)
		{
			const uchar* src = a.constData() + qsizetype(y) * w;
			uchar* dst = tmp.data() + qsizetype(y) * w;
			int sum = 0;
			for (int k = -r; k <= r; ++k)
				sum += src[qBound(0, k, w - 1)];
			for (int x = 0; x < w; ++x)
			{
				dst[x] = uchar(sum / win);
				sum += src[qBound(0, x + r + 1, w - 1)] - src[qBound(0, x - r, w - 1)];
			}
		}
		for (int x = 0; x < w; ++x)
		{
			int sum = 0;
			for (int k = -r; k <= r; ++k)
				sum += tmp[qsizetype(qBound(0, k, h - 1)) * w + x];
			for (int y = 0; y < h; ++y)
			{
				a[qsizetype(y) * w + x] = uchar(sum / win);
				sum += tmp[qsizetype(qBound(0, y + r + 1, h - 1)) * w + x] - tmp[qsizetype(qBound(0, y - r, h - 1)) * w + x];
			}
		}
	}

	QString normDirection(const QString& d)
	{
		const QString s = d.trimmed().toLower();
		if (s == "top" || s == "bottom" || s == "left" || s == "right")
			return s;
		return QStringLiteral("all");
	}
}

int ScFeatherMask::indexIn(const ScImageEffectList& list)
{
	for (int i = 0; i < list.count(); ++i)
		if (list.at(i).effectCode == ImageEffect::EF_FEATHER)
			return i;
	return -1;
}

QString ScFeatherMask::encode(double px, const QString& direction)
{
	return QString::number(px, 'f', 2) + " " + normDirection(direction);
}

bool ScFeatherMask::paramsOf(const ScImageEffectList& list, double& px, QString& direction)
{
	px = 0;
	direction = QStringLiteral("all");
	const int i = indexIn(list);
	if (i < 0)
		return false;
	const QStringList parts = list.at(i).effectParameters.simplified().split(' ', Qt::SkipEmptyParts);
	if (parts.isEmpty())
		return false;
	bool ok = false;
	px = parts.at(0).toDouble(&ok);
	if (!ok || px <= 0)
		return false;
	if (parts.size() > 1)
		direction = normDirection(parts.at(1));
	return true;
}

void ScFeatherMask::removeFrom(ScImageEffectList& list)
{
	int i;
	while ((i = indexIn(list)) >= 0)
		list.removeAt(i);
}

void ScFeatherMask::set(ScImageEffectList& list, double px, const QString& direction)
{
	if (px <= 0)
	{
		removeFrom(list);
		return;
	}
	ImageEffect ef;
	ef.effectCode = ImageEffect::EF_FEATHER;
	ef.effectParameters = encode(px, direction);
	const int i = indexIn(list);
	if (i >= 0)
		list[i] = ef;
	else
		list.append(ef);
}

bool ScFeatherMask::hasTransparency(const QByteArray& alpha, int w, int h)
{
	const qsizetype n = qsizetype(w) * qsizetype(h);
	if (alpha.size() < n)
		return false;
	const uchar* a = reinterpret_cast<const uchar*>(alpha.constData());
	for (qsizetype i = 0; i < n; ++i)
		if (a[i] < 250)
			return true;
	return false;
}

QImage ScFeatherMask::maskFor(const QByteArray& alpha, int w, int h, double px, const QString& direction)
{
	if (w <= 0 || h <= 0 || px <= 0)
		return QImage();
	const int r = qMax(1, int(std::lround(px)));
	QImage mask(w, h, QImage::Format_Grayscale8);
	const QString dir = normDirection(direction);
	if (hasTransparency(alpha, w, h))
	{
		if (dir == "all")
		{
			// Cut-out, all sides: fade inside the subject's own outline.
			QVector<uchar> a(qsizetype(w) * qsizetype(h));
			memcpy(a.data(), alpha.constData(), size_t(a.size()));
			erode(a, w, h, qMax(1, r / 2));
			const int br = qMax(1, int(std::lround(r / 3.0)));   // 3 box passes ~ Gaussian of radius r
			boxBlurPass(a, w, h, br);
			boxBlurPass(a, w, h, br);
			boxBlurPass(a, w, h, br);
			for (int y = 0; y < h; ++y)
				memcpy(mask.scanLine(y), a.constData() + qsizetype(y) * w, size_t(w));
			return mask;
		}
		// Cut-out, one side: a linear ramp measured from the subject's real
		// visible edge (bounding box of the non-transparent pixels), not from
		// the frame edge, running inward over the amount. The other sides of
		// the outline stay as they are.
		const uchar* al = reinterpret_cast<const uchar*>(alpha.constData());
		int minX = w, maxX = -1, minY = h, maxY = -1;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (al[qsizetype(y) * w + x] > 8)
				{
					minX = qMin(minX, x); maxX = qMax(maxX, x);
					minY = qMin(minY, y); maxY = qMax(maxY, y);
				}
		mask.fill(255);
		if (maxX < 0)
			return mask;
		for (int y = 0; y < h; ++y)
		{
			uchar* line = mask.scanLine(y);
			double fy = 1.0;
			if (dir == "top" && y - minY < r)        fy = qMax(0, y - minY) / double(r);
			if (dir == "bottom" && maxY - y < r)     fy = qMax(0, maxY - y) / double(r);
			for (int x = 0; x < w; ++x)
			{
				double f = fy;
				if (dir == "left" && x - minX < r)   f *= qMax(0, x - minX) / double(r);
				if (dir == "right" && maxX - x < r)  f *= qMax(0, maxX - x) / double(r);
				line[x] = uchar(qBound(0, int(std::lround(f * 255.0)), 255));
			}
		}
		return mask;
	}
	// Opaque picture: linear ramp from the chosen edge(s) in.
	const bool top = dir == "all" || dir == "top";
	const bool bottom = dir == "all" || dir == "bottom";
	const bool left = dir == "all" || dir == "left";
	const bool right = dir == "all" || dir == "right";
	for (int y = 0; y < h; ++y)
	{
		uchar* line = mask.scanLine(y);
		double fy = 1.0;
		if (top && y < r) fy *= double(y) / r;
		if (bottom && (h - 1 - y) < r) fy *= double(h - 1 - y) / r;
		for (int x = 0; x < w; ++x)
		{
			double f = fy;
			if (left && x < r) f *= double(x) / r;
			if (right && (w - 1 - x) < r) f *= double(w - 1 - x) / r;
			line[x] = uchar(qBound(0, int(std::lround(f * 255.0)), 255));
		}
	}
	return mask;
}

void ScFeatherMask::applyToImage(QImage& image, double px, const QString& direction)
{
	if (image.isNull() || px <= 0)
		return;
	if (image.format() != QImage::Format_ARGB32)
		image = image.convertToFormat(QImage::Format_ARGB32);
	const int w = image.width(), h = image.height();
	QByteArray alpha(qsizetype(w) * qsizetype(h), char(uchar(255)));
	uchar* a = reinterpret_cast<uchar*>(alpha.data());
	for (int y = 0; y < h; ++y)
	{
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < w; ++x)
			a[qsizetype(y) * w + x] = uchar(qAlpha(line[x]));
	}
	const QImage mask = maskFor(alpha, w, h, px, direction);
	if (mask.isNull())
		return;
	for (int y = 0; y < h; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		const uchar* m = mask.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			if (m[x] == 255)
				continue;
			const QRgb p = line[x];
			line[x] = qRgba(qRed(p), qGreen(p), qBlue(p), (qAlpha(p) * m[x]) / 255);
		}
	}
}

void ScFeatherMask::mergeIntoAlphaBytes(QByteArray& alpha, int w, int h, double px, const QString& direction)
{
	if (w <= 0 || h <= 0 || px <= 0)
		return;
	const qsizetype n = qsizetype(w) * qsizetype(h);
	if (alpha.size() < n)
	{
		QByteArray grown(n, char(uchar(255)));
		memcpy(grown.data(), alpha.constData(), size_t(alpha.size()));
		alpha = grown;
	}
	const QImage mask = maskFor(alpha, w, h, px, direction);
	if (mask.isNull())
		return;
	uchar* a = reinterpret_cast<uchar*>(alpha.data());
	for (int y = 0; y < h; ++y)
	{
		const uchar* m = mask.constScanLine(y);
		for (int x = 0; x < w; ++x)
			a[qsizetype(y) * w + x] = uchar((int(a[qsizetype(y) * w + x]) * int(m[x])) / 255);
	}
}
