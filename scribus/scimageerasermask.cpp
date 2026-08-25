/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimageerasermask.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QPainter>
#include <QtGlobal>

#include <cmath>

namespace
{
	//! Version tag leading the parameter string, so a later mask format can be
	//! told apart from this one instead of being decoded as garbage.
	const QLatin1String ParamVersion("1");

	QImage toMaskFormat(const QImage& in)
	{
		if (in.isNull())
			return QImage();
		if (in.format() == QImage::Format_Grayscale8)
			return in;
		return in.convertToFormat(QImage::Format_Grayscale8);
	}
}

QString ScEraserMask::encode(const QImage& mask)
{
	if (mask.isNull())
		return QString();

	QImage out = toMaskFormat(mask);
	QByteArray png;
	QBuffer buffer(&png);
	if (!buffer.open(QIODevice::WriteOnly))
		return QString();
	// Greyscale PNG of a mostly-flat mask compresses hard; this is what keeps
	// the .sla payload reasonable for a full-page photo.
	if (!out.save(&buffer, "PNG"))
		return QString();
	buffer.close();

	return ParamVersion + QLatin1Char(' ') + QString::fromLatin1(png.toBase64());
}

QImage ScEraserMask::decode(const QString& params)
{
	if (params.isEmpty())
		return QImage();

	int sep = params.indexOf(QLatin1Char(' '));
	if (sep <= 0)
		return QImage();
	if (params.left(sep) != ParamVersion)
		return QImage();

	QByteArray png = QByteArray::fromBase64(params.mid(sep + 1).toLatin1());
	if (png.isEmpty())
		return QImage();

	QImage mask;
	if (!mask.loadFromData(png, "PNG"))
		return QImage();
	return toMaskFormat(mask);
}

QString ScEraserMask::digest(const QString& params)
{
	if (params.isEmpty())
		return QString();
	QByteArray hash = QCryptographicHash::hash(params.toLatin1(), QCryptographicHash::Md5);
	return QString::fromLatin1(hash.toHex().left(16));
}

int ScEraserMask::indexIn(const ScImageEffectList& list)
{
	for (int i = 0; i < list.count(); ++i)
	{
		if (list.at(i).effectCode == ImageEffect::EF_ERASERMASK)
			return i;
	}
	return -1;
}

QString ScEraserMask::paramsOf(const ScImageEffectList& list)
{
	int idx = indexIn(list);
	if (idx < 0)
		return QString();
	return list.at(idx).effectParameters;
}

QImage ScEraserMask::maskOf(const ScImageEffectList& list)
{
	return decode(paramsOf(list));
}

bool ScEraserMask::isEmptyMask(const QImage& mask)
{
	if (mask.isNull())
		return true;
	QImage m = toMaskFormat(mask);
	for (int y = 0; y < m.height(); ++y)
	{
		const uchar* line = m.constScanLine(y);
		for (int x = 0; x < m.width(); ++x)
		{
			if (line[x] != Keep)
				return false;
		}
	}
	return true;
}

void ScEraserMask::removeFrom(ScImageEffectList& list)
{
	int idx = indexIn(list);
	if (idx >= 0)
		list.removeAt(idx);
}

void ScEraserMask::setMask(ScImageEffectList& list, const QImage& mask)
{
	// An erased-then-fully-restored frame must end up byte-identical to one
	// that was never erased, or undo would leave a stale payload in the .sla.
	if (isEmptyMask(mask))
	{
		removeFrom(list);
		return;
	}

	QString params = encode(mask);
	if (params.isEmpty())
		return;

	int idx = indexIn(list);
	if (idx >= 0)
	{
		list[idx].effectParameters = params;
		return;
	}

	ImageEffect effect;
	effect.effectCode = ImageEffect::EF_ERASERMASK;
	effect.effectParameters = params;
	list.append(effect);
}

QImage ScEraserMask::createFor(int imageW, int imageH)
{
	if (imageW <= 0 || imageH <= 0)
		return QImage();

	int w = imageW;
	int h = imageH;
	int longest = qMax(w, h);
	if (longest > MaxEdge)
	{
		double factor = double(MaxEdge) / double(longest);
		w = qMax(1, qRound(w * factor));
		h = qMax(1, qRound(h * factor));
	}

	QImage mask(w, h, QImage::Format_Grayscale8);
	if (mask.isNull())
		return QImage();
	mask.fill(Keep);
	return mask;
}

QRect ScEraserMask::stamp(QImage& coverage, const QPointF& centre, double radius, double hardness)
{
	if (coverage.isNull() || radius <= 0.0)
		return QRect();

	hardness = qBound(0.0, hardness, 1.0);

	int x0 = qMax(0, int(std::floor(centre.x() - radius)));
	int y0 = qMax(0, int(std::floor(centre.y() - radius)));
	int x1 = qMin(coverage.width() - 1, int(std::ceil(centre.x() + radius)));
	int y1 = qMin(coverage.height() - 1, int(std::ceil(centre.y() + radius)));
	if (x1 < x0 || y1 < y0)
		return QRect();

	// Below this the feather band is narrower than a pixel, so treat the dab as
	// hard and let plain anti-aliasing of the rim do the smoothing.
	const double feather = 1.0 - hardness;
	const double invRadius = 1.0 / radius;

	for (int y = y0; y <= y1; ++y)
	{
		uchar* line = coverage.scanLine(y);
		double dy = double(y) + 0.5 - centre.y();
		for (int x = x0; x <= x1; ++x)
		{
			double dx = double(x) + 0.5 - centre.x();
			double d = std::sqrt(dx * dx + dy * dy) * invRadius;
			if (d >= 1.0)
				continue;

			double f;
			if (d <= hardness || feather <= 0.0001)
			{
				f = 1.0;
			}
			else
			{
				f = 1.0 - (d - hardness) / feather;
				// Smoothstep: a linear ramp leaves a visible edge where the
				// falloff meets the solid core.
				f = f * f * (3.0 - 2.0 * f);
			}

			int v = int(f * 255.0 + 0.5);
			if (v > line[x])
				line[x] = uchar(v);
		}
	}

	return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

QRect ScEraserMask::stampLine(QImage& coverage, const QPointF& a, const QPointF& b, double radius, double hardness, double& carry)
{
	if (coverage.isNull() || radius <= 0.0)
		return QRect();

	double dx = b.x() - a.x();
	double dy = b.y() - a.y();
	double len = std::sqrt(dx * dx + dy * dy);

	// Quarter-radius spacing: wide enough to keep the dab count down on a fast
	// drag, tight enough that a soft brush does not come out scalloped.
	const double spacing = qMax(0.5, radius * 0.25);

	if (len <= 0.0)
	{
		carry += 0.0;
		return QRect();
	}

	QRect touched;
	// Dabs are placed at fixed arc-length intervals along the whole stroke, not
	// per segment, so splitting a gesture into more mouse-move events cannot
	// change where they land.
	double d = spacing - carry;
	if (d < 0.0)
		d = 0.0;
	double last = -1.0;
	while (d <= len)
	{
		double t = d / len;
		QRect r = stamp(coverage, QPointF(a.x() + dx * t, a.y() + dy * t), radius, hardness);
		if (!r.isNull())
			touched = touched.isNull() ? r : touched.united(r);
		last = d;
		d += spacing;
	}

	carry = (last < 0.0) ? (carry + len) : (len - last);
	return touched;
}

void ScEraserMask::applyStroke(QImage& out, const QImage& base, const QImage& coverage, bool restore, const QRect& region)
{
	if (out.isNull() || base.isNull() || coverage.isNull())
		return;
	if (out.size() != base.size() || out.size() != coverage.size())
		return;

	QRect area = region.isValid() ? region.intersected(out.rect()) : out.rect();
	if (area.isEmpty())
		return;

	for (int y = area.top(); y <= area.bottom(); ++y)
	{
		uchar* o = out.scanLine(y);
		const uchar* b = base.constScanLine(y);
		const uchar* c = coverage.constScanLine(y);
		for (int x = area.left(); x <= area.right(); ++x)
		{
			int cov = c[x];
			if (cov == 0)
			{
				o[x] = b[x];
				continue;
			}
			int v = restore ? (int(b[x]) + cov) : (int(b[x]) - cov);
			o[x] = uchar(qBound(0, v, 255));
		}
	}
}

void ScEraserMask::applyToAlpha(QImage& image, const QImage& mask)
{
	if (image.isNull() || mask.isNull())
		return;

	if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied)
		image = image.convertToFormat(QImage::Format_ARGB32);
	else if (image.format() == QImage::Format_ARGB32_Premultiplied)
		image = image.convertToFormat(QImage::Format_ARGB32);

	QImage m = toMaskFormat(mask);
	if (m.size() != image.size())
		m = m.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	const int w = image.width();
	const int h = image.height();
	for (int y = 0; y < h; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		const uchar* mline = m.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			int keep = mline[x];
			if (keep == Keep)
				continue;
			QRgb px = line[x];
			int a = (qAlpha(px) * keep) / 255;
			line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), a);
		}
	}
}

QByteArray ScEraserMask::toAlphaBytes(const QImage& mask, int w, int h)
{
	QByteArray out;
	if (mask.isNull() || w <= 0 || h <= 0)
		return out;

	QImage m = toMaskFormat(mask);
	if (m.width() != w || m.height() != h)
		m = m.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	out.resize(qsizetype(w) * qsizetype(h));
	char* dst = out.data();
	for (int y = 0; y < h; ++y)
	{
		memcpy(dst + qsizetype(y) * w, m.constScanLine(y), w);
	}
	return out;
}

void ScEraserMask::mergeIntoAlphaBytes(QByteArray& alpha, const QImage& mask, int w, int h)
{
	if (mask.isNull() || w <= 0 || h <= 0)
		return;

	QByteArray erase = toAlphaBytes(mask, w, h);
	if (erase.isEmpty())
		return;

	qsizetype count = qsizetype(w) * qsizetype(h);
	if (alpha.size() < count)
	{
		// The renderer had no alpha of its own (the common case for a JPEG):
		// start from fully opaque so the erasure is the only thing masking.
		QByteArray grown(count, char(uchar(255)));
		memcpy(grown.data(), alpha.constData(), size_t(alpha.size()));
		alpha = grown;
	}

	uchar* a = reinterpret_cast<uchar*>(alpha.data());
	const uchar* e = reinterpret_cast<const uchar*>(erase.constData());
	for (qsizetype i = 0; i < count; ++i)
		a[i] = uchar((int(a[i]) * int(e[i])) / 255);
}

void ScEraserMask::mergeIntoPdfImageMask(QByteArray& stencil, const QImage& mask, int w, int h)
{
	if (mask.isNull() || w <= 0 || h <= 0)
		return;

	QImage m = toMaskFormat(mask);
	if (m.width() != w || m.height() != h)
		m = m.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	int rowBytes = w / 8;
	if ((w % 8) != 0)
		rowBytes++;

	qsizetype needed = qsizetype(rowBytes) * qsizetype(h);
	if (stencil.size() < needed)
	{
		// No stencil from the file: start all-zero, which in this inverted
		// sense means "paint every pixel", then punch the erasure into it.
		QByteArray grown(needed, char(0));
		memcpy(grown.data(), stencil.constData(), size_t(stencil.size()));
		stencil = grown;
	}

	uchar* out = reinterpret_cast<uchar*>(stencil.data());
	for (int y = 0; y < h; ++y)
	{
		const uchar* mline = m.constScanLine(y);
		uchar* orow = out + qsizetype(y) * rowBytes;
		for (int x = 0; x < w; ++x)
		{
			// Half-way point: with only one bit available there is nowhere for
			// a partially erased pixel to go.
			if (mline[x] >= 128)
				continue;
			orow[x >> 3] |= uchar(0x80 >> (x & 7));
		}
	}
}
