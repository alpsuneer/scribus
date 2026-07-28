/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimageselection.h"

#include <cmath>

#include <QPainter>
#include <QVector>

ScImageSelection::ScImageSelection(const QSize& imageSize, QObject* parent)
	: QObject(parent)
{
	QSize sz = imageSize.isValid() ? imageSize : QSize(1, 1);
	m_mask = QImage(sz, QImage::Format_Alpha8);
	m_mask.fill(0);
}

// ── Query ────────────────────────────────────────────────────────────────────

QSize ScImageSelection::size() const
{
	return m_mask.size();
}

QRect ScImageSelection::bounds() const
{
	if (!m_boundsDirty)
		return m_cachedBounds;
	int w = m_mask.width();
	int h = m_mask.height();
	int minX = w, minY = h, maxX = -1, maxY = -1;
	for (int y = 0; y < h; ++y)
	{
		const uchar* s = m_mask.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			if (s[x] != 0)
			{
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
		}
	}
	m_cachedBounds = (maxX < 0) ? QRect() : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
	m_boundsDirty = false;
	return m_cachedBounds;
}

bool ScImageSelection::isEmpty() const
{
	return bounds().isEmpty();
}

bool ScImageSelection::contains(const QPoint& p) const
{
	if (!m_mask.rect().contains(p))
		return false;
	return m_mask.constScanLine(p.y())[p.x()] >= 128;
}

// ── Modification ─────────────────────────────────────────────────────────────

void ScImageSelection::clear()
{
	m_mask.fill(0);
	invalidateCache();
	emit changed();
}

void ScImageSelection::selectAll()
{
	m_mask.fill(255);
	invalidateCache();
	emit changed();
}

void ScImageSelection::invert()
{
	int w = m_mask.width();
	int h = m_mask.height();
	for (int y = 0; y < h; ++y)
	{
		uchar* s = m_mask.scanLine(y);
		for (int x = 0; x < w; ++x)
			s[x] = 255 - s[x];
	}
	invalidateCache();
	emit changed();
}

QImage ScImageSelection::rasterizeStamp(const std::function<void(QPainter&)>& draw, bool antialias) const
{
	// Paint the shape as opaque white on a transparent ARGB32 buffer; the alpha
	// channel then holds the coverage (anti-aliased at the edges).
	QImage buf(m_mask.size(), QImage::Format_ARGB32_Premultiplied);
	buf.fill(Qt::transparent);
	QPainter p(&buf);
	p.setRenderHint(QPainter::Antialiasing, antialias);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	draw(p);
	p.end();

	QImage stamp(m_mask.size(), QImage::Format_Alpha8);
	int w = stamp.width();
	int h = stamp.height();
	for (int y = 0; y < h; ++y)
	{
		const QRgb* src = reinterpret_cast<const QRgb*>(buf.constScanLine(y));
		uchar* dst = stamp.scanLine(y);
		for (int x = 0; x < w; ++x)
			dst[x] = qAlpha(src[x]);
	}
	return stamp;
}

void ScImageSelection::blendStamp(const QImage& stamp, Mode mode)
{
	int w = m_mask.width();
	int h = m_mask.height();
	for (int y = 0; y < h; ++y)
	{
		uchar* d = m_mask.scanLine(y);
		const uchar* s = stamp.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			int cur = d[x];
			int add = s[x];
			int out;
			switch (mode)
			{
				case Add:       out = qMax(cur, add); break;
				case Subtract:  out = cur * (255 - add) / 255; break;
				case Intersect: out = cur * add / 255; break;
				case Replace:
				default:        out = add; break;
			}
			d[x] = static_cast<uchar>(qBound(0, out, 255));
		}
	}
	invalidateCache();
	emit changed();
}

void ScImageSelection::setFromRect(const QRect& r, Mode mode, bool antialias)
{
	QImage stamp = rasterizeStamp([&](QPainter& p){ p.drawRect(r); }, antialias);
	blendStamp(stamp, mode);
}

void ScImageSelection::setFromEllipse(const QRect& r, Mode mode)
{
	QImage stamp = rasterizeStamp([&](QPainter& p){ p.drawEllipse(r); }, true);
	blendStamp(stamp, mode);
}

void ScImageSelection::setFromPath(const QPainterPath& path, Mode mode)
{
	QImage stamp = rasterizeStamp([&](QPainter& p){ p.drawPath(path); }, true);
	blendStamp(stamp, mode);
}

void ScImageSelection::setFromMask(const QImage& mask, Mode mode)
{
	QImage stamp;
	if (mask.size() == m_mask.size() && mask.format() == QImage::Format_Alpha8)
		stamp = mask;
	else
	{
		// Normalise: scale to fit and take the alpha (or luminance) as coverage.
		QImage src = mask;
		if (src.size() != m_mask.size())
			src = src.scaled(m_mask.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		stamp = QImage(m_mask.size(), QImage::Format_Alpha8);
		bool hasAlpha = src.hasAlphaChannel();
		QImage rgb = src.convertToFormat(QImage::Format_ARGB32);
		int w = stamp.width(), h = stamp.height();
		for (int y = 0; y < h; ++y)
		{
			const QRgb* s = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
			uchar* d = stamp.scanLine(y);
			for (int x = 0; x < w; ++x)
				d[x] = hasAlpha ? qAlpha(s[x]) : qRound(0.299 * qRed(s[x]) + 0.587 * qGreen(s[x]) + 0.114 * qBlue(s[x]));
		}
	}
	blendStamp(stamp, mode);
}

void ScImageSelection::paintBrush(const QPointF& center, double radius, double hardness, bool add)
{
	if (radius < 0.5)
		return;
	const int w = m_mask.width();
	const int h = m_mask.height();
	const double inner = radius * qBound(0.0, hardness, 1.0);   // fully-opaque radius
	const double falloff = qMax(0.001, radius - inner);
	const int x0 = qMax(0, static_cast<int>(std::floor(center.x() - radius)));
	const int x1 = qMin(w - 1, static_cast<int>(std::ceil(center.x() + radius)));
	const int y0 = qMax(0, static_cast<int>(std::floor(center.y() - radius)));
	const int y1 = qMin(h - 1, static_cast<int>(std::ceil(center.y() + radius)));
	for (int y = y0; y <= y1; ++y)
	{
		uchar* s = m_mask.scanLine(y);
		double dy = y - center.y();
		for (int x = x0; x <= x1; ++x)
		{
			double dx = x - center.x();
			double dist = std::sqrt(dx * dx + dy * dy);
			if (dist > radius)
				continue;
			double cov = (dist <= inner) ? 1.0 : 1.0 - (dist - inner) / falloff;
			int c = qBound(0, static_cast<int>(std::lround(cov * 255.0)), 255);
			if (add)
				s[x] = static_cast<uchar>(qMax<int>(s[x], c));
			else
				s[x] = static_cast<uchar>(s[x] * (255 - c) / 255);
		}
	}
	invalidateCache();
	emit changed();
}

// ── Refinement ───────────────────────────────────────────────────────────────

namespace
{
	// Separable box blur of an Alpha8 mask (sliding window, O(n) per pass).
	void boxBlurAlpha8(QImage& img, int radius)
	{
		if (radius < 1)
			return;
		int w = img.width();
		int h = img.height();
		int div = 2 * radius + 1;
		QVector<int> tmp(w * h);
		// horizontal
		for (int y = 0; y < h; ++y)
		{
			const uchar* s = img.constScanLine(y);
			int sum = 0;
			for (int k = -radius; k <= radius; ++k)
				sum += s[qBound(0, k, w - 1)];
			for (int x = 0; x < w; ++x)
			{
				tmp[y * w + x] = sum / div;
				sum -= s[qBound(0, x - radius, w - 1)];
				sum += s[qBound(0, x + radius + 1, w - 1)];
			}
		}
		// vertical
		for (int x = 0; x < w; ++x)
		{
			int sum = 0;
			for (int k = -radius; k <= radius; ++k)
				sum += tmp[qBound(0, k, h - 1) * w + x];
			for (int y = 0; y < h; ++y)
			{
				img.scanLine(y)[x] = static_cast<uchar>(qBound(0, sum / div, 255));
				sum -= tmp[qBound(0, y - radius, h - 1) * w + x];
				sum += tmp[qBound(0, y + radius + 1, h - 1) * w + x];
			}
		}
	}

	// Separable morphology (dilate = max, erode = min) with a square element.
	void morphAlpha8(QImage& img, int radius, bool dilate)
	{
		if (radius < 1)
			return;
		int w = img.width();
		int h = img.height();
		QImage tmp(img.size(), QImage::Format_Alpha8);
		// horizontal
		for (int y = 0; y < h; ++y)
		{
			const uchar* s = img.constScanLine(y);
			uchar* d = tmp.scanLine(y);
			for (int x = 0; x < w; ++x)
			{
				int v = dilate ? 0 : 255;
				for (int k = -radius; k <= radius; ++k)
				{
					int xx = qBound(0, x + k, w - 1);
					v = dilate ? qMax(v, static_cast<int>(s[xx])) : qMin(v, static_cast<int>(s[xx]));
				}
				d[x] = static_cast<uchar>(v);
			}
		}
		// vertical
		for (int x = 0; x < w; ++x)
		{
			for (int y = 0; y < h; ++y)
			{
				int v = dilate ? 0 : 255;
				for (int k = -radius; k <= radius; ++k)
				{
					int yy = qBound(0, y + k, h - 1);
					v = dilate ? qMax(v, static_cast<int>(tmp.constScanLine(yy)[x]))
					           : qMin(v, static_cast<int>(tmp.constScanLine(yy)[x]));
				}
				img.scanLine(y)[x] = static_cast<uchar>(v);
			}
		}
	}
}

void ScImageSelection::feather(double radius)
{
	if (radius < 0.5)
		return;
	boxBlurAlpha8(m_mask, qRound(radius));
	invalidateCache();
	emit changed();
}

void ScImageSelection::expand(int pixels)
{
	if (pixels < 1)
		return;
	morphAlpha8(m_mask, pixels, true);
	invalidateCache();
	emit changed();
}

void ScImageSelection::contract(int pixels)
{
	if (pixels < 1)
		return;
	morphAlpha8(m_mask, pixels, false);
	invalidateCache();
	emit changed();
}

void ScImageSelection::smooth(int radius)
{
	if (radius < 1)
		return;
	// Blur then re-threshold to round off jagged edges.
	boxBlurAlpha8(m_mask, radius);
	int w = m_mask.width();
	int h = m_mask.height();
	for (int y = 0; y < h; ++y)
	{
		uchar* s = m_mask.scanLine(y);
		for (int x = 0; x < w; ++x)
			s[x] = (s[x] >= 128) ? 255 : 0;
	}
	invalidateCache();
	emit changed();
}

// ── Rendering aids ───────────────────────────────────────────────────────────

QRegion ScImageSelection::asRegion() const
{
	QRegion region;
	int w = m_mask.width();
	int h = m_mask.height();
	for (int y = 0; y < h; ++y)
	{
		const uchar* s = m_mask.constScanLine(y);
		int x = 0;
		while (x < w)
		{
			if (s[x] >= 128)
			{
				int start = x;
				while (x < w && s[x] >= 128)
					++x;
				region += QRect(start, y, x - start, 1);
			}
			else
				++x;
		}
	}
	return region;
}

QPainterPath ScImageSelection::outlinePath() const
{
	if (!m_outlineDirty)
		return m_cachedOutline;
	QPainterPath path;
	path.addRegion(asRegion());
	m_cachedOutline = path.simplified();   // merge the run rects into clean outline polygons
	m_outlineDirty = false;
	return m_cachedOutline;
}

void ScImageSelection::invalidateCache()
{
	m_boundsDirty = true;
	m_outlineDirty = true;
}
