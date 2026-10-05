#include "suneertexteffects.h"

#include <atomic>
#include <cmath>
#include <memory>

#include <QCheckBox>
#include <QColorSpace>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <tiffio.h>
#include <zlib.h>

#include "cmsettings.h"
#include "commonstrings.h"
#include "filewatcher.h"
#include "fpointarray.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "sccolor.h"
#include "sccolorengine.h"
#include "scimage.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerfilltextimage.h"
#include "suneerimagelinks.h"
#include "ui/colorcombo.h"
#include "ui/layers.h"
#include "undomanager.h"
#include "undostate.h"
#include "util.h"

using namespace SuneerTextEffects;

namespace
{
const QString kAttrFx    = QStringLiteral("SuneerEmboss");       // on the face: value = settings, parameter = id
const QString kAttrBase  = QStringLiteral("SuneerEmbossBase");   // on the face: what the face showed before the effect
const QString kAttrPiece = QStringLiteral("SuneerEmbossPiece");  // on a frame behind / over the face: value = role, parameter = id
const QString kFilePrefix = QStringLiteral("fx_");
const qint64  kMaxPixels = 40000000;   // three float planes of this size are held while shading
const int     kMaxSide   = 20000;

// Images written by this run of the program. Undo can bring a frame back that
// links one of them, so they are not deleted before the document closes.
QSet<QString> g_sessionFiles;

bool hasInner(int style) { return style != OuterBevel; }
bool hasOuter(int style) { return style == OuterBevel || style == Emboss || style == PillowEmboss; }

// ---------------------------------------------------------------- key=value;key=value

QString encodeKV(const QMap<QString, QString>& map)
{
	QStringList parts;
	for (auto it = map.constBegin(); it != map.constEnd(); ++it)
		parts << it.key() + "=" + QString::fromLatin1(QUrl::toPercentEncoding(it.value()));
	return parts.join(";");
}

QMap<QString, QString> decodeKV(const QString& text)
{
	QMap<QString, QString> map;
	const QStringList parts = text.split(';', Qt::SkipEmptyParts);
	for (const QString& part : parts)
	{
		const int eq = part.indexOf('=');
		if (eq > 0)
			map.insert(part.left(eq), QUrl::fromPercentEncoding(part.mid(eq + 1).toLatin1()));
	}
	return map;
}

// ---------------------------------------------------------------- item attributes

ObjectAttribute attributeOf(const PageItem* item, const QString& name)
{
	if (item)
	{
		const ObjAttrVector* all = const_cast<PageItem*>(item)->getObjectAttributes();
		for (const ObjectAttribute& a : *all)
			if (a.name == name)
				return a;
	}
	return ObjectAttribute();
}

void setAttribute(PageItem* item, const QString& name, const QString& value, const QString& parameter)
{
	ObjAttrVector* all = item->getObjectAttributes();
	for (ObjectAttribute& a : *all)
	{
		if (a.name == name)
		{
			a.value = value;
			a.parameter = parameter;
			return;
		}
	}
	ObjectAttribute a;
	a.name = name;
	a.type = QStringLiteral("string");
	a.value = value;
	a.parameter = parameter;
	a.relationship = QStringLiteral("none");
	a.autoaddto = QStringLiteral("none");
	all->append(a);
}

void clearAttribute(PageItem* item, const QString& name)
{
	ObjAttrVector* all = item->getObjectAttributes();
	for (int i = all->count() - 1; i >= 0; --i)
		if (all->at(i).name == name)
			all->removeAt(i);
}

// Undo puts the attributes back as they were, with everything else the command did.
void recordAttributes(PageItem* item, const ObjAttrVector& before)
{
	if (!UndoManager::undoEnabled())
		return;
	auto* is = new ScItemState<QPair<ObjAttrVector, ObjAttrVector> >(QObject::tr("Text Effects settings"));
	is->set("SUNEER_ITEM_ATTRIBUTES");
	is->setItem(qMakePair(before, *item->getObjectAttributes()));
	UndoManager::instance()->action(item, is);
}

// ---------------------------------------------------------------- colours

// The colours the presets use do not have to exist in the document.
bool builtinColor(const QString& name, ScColor& color)
{
	if (name == "White")                  { color = ScColor(0, 0, 0, 0); return true; }
	if (name == "Black")                  { color = ScColor(0, 0, 0, 255); return true; }
	if (name == "Emboss Gold Light")      { color = ScColor(0, 13, 128, 0); return true; }
	if (name == "Emboss Gold Dark")       { color = ScColor(64, 153, 255, 115); return true; }
	return false;
}

QStringList builtinColorNames()
{
	return { "White", "Black", "Emboss Gold Light", "Emboss Gold Dark" };
}

ScColor resolveColor(const ScribusDoc* doc, const QString& name)
{
	if (doc->PageColors.contains(name))
		return doc->PageColors[name];
	ScColor color(0, 0, 0, 255);
	builtinColor(name, color);
	return color;
}

// Channels as light, 0 = none, 1 = full: RGB as it is, CMYK as 1 - ink.
void lightOf(const ScribusDoc* doc, const ScColor& color, double shade, bool cmyk, float out[4])
{
	if (cmyk)
	{
		CMYKColorF c;
		ScColorEngine::getShadeColorCMYK(color, doc, c, shade);
		out[0] = 1.0f - float(c.c); out[1] = 1.0f - float(c.m); out[2] = 1.0f - float(c.y); out[3] = 1.0f - float(c.k);
	}
	else
	{
		const QColor q = ScColorEngine::getShadeColor(color, doc, shade);
		out[0] = q.redF(); out[1] = q.greenF(); out[2] = q.blueF(); out[3] = 1.0f;
	}
	for (int i = 0; i < 4; ++i)
		out[i] = qBound(0.0f, out[i], 1.0f);
}

// ---------------------------------------------------------------- the shading

struct Progress
{
	std::atomic<int>  percent { 0 };
	std::atomic<bool> cancel { false };
};

struct Maps
{
	int w { 0 };
	int h { 0 };
	QVector<uchar> hl;   // highlight amount
	QVector<uchar> sh;   // shadow amount
};

// Felzenszwalb & Huttenlocher, one dimension. f and d are squared distances.
void dt1d(const double* f, double* d, int* v, double* z, int n)
{
	const double inf = 1e30;
	int k = 0;
	v[0] = 0;
	z[0] = -inf;
	z[1] = inf;
	auto meet = [f](int q, int p) { return ((f[q] + double(q) * q) - (f[p] + double(p) * p)) / (2.0 * q - 2.0 * p); };
	for (int q = 1; q < n; ++q)
	{
		double s = meet(q, v[k]);
		while (s <= z[k])   // z[0] is -inf, so k stays at 0 or above
		{
			--k;
			s = meet(q, v[k]);
		}
		++k;
		v[k] = q;
		z[k] = s;
		z[k + 1] = inf;
	}
	k = 0;
	for (int q = 0; q < n; ++q)
	{
		while (z[k + 1] < q)
			++k;
		const int p = v[k];
		d[q] = double(q - p) * (q - p) + f[p];
	}
}

// in: 0 where a feature is, anything else elsewhere. out: distance in pixels to the nearest feature.
void distanceTransform(QVector<float>& grid, int w, int h)
{
	const double inf = 1e12;
	const int n = qMax(w, h);
	QVector<double> f(n), d(n), z(n + 1);
	QVector<int> v(n);
	for (int x = 0; x < w; ++x)
	{
		for (int y = 0; y < h; ++y)
			f[y] = (grid[y * w + x] == 0.0f) ? 0.0 : inf;
		dt1d(f.constData(), d.data(), v.data(), z.data(), h);
		for (int y = 0; y < h; ++y)
			grid[y * w + x] = float(qMin(d[y], inf));
	}
	for (int y = 0; y < h; ++y)
	{
		float* row = grid.data() + y * w;
		for (int x = 0; x < w; ++x)
			f[x] = row[x];
		dt1d(f.constData(), d.data(), v.data(), z.data(), w);
		for (int x = 0; x < w; ++x)
			row[x] = float(std::sqrt(qMin(d[x], inf)));
	}
}

void boxBlurLine(const float* src, float* dst, int n, int stride, int r)
{
	const float norm = 1.0f / float(2 * r + 1);
	float sum = 0.0f;
	for (int i = -r; i <= r; ++i)
		sum += src[qBound(0, i, n - 1) * stride];
	for (int i = 0; i < n; ++i)
	{
		dst[i * stride] = sum * norm;
		sum += src[qMin(n - 1, i + r + 1) * stride] - src[qMax(0, i - r) * stride];
	}
}

// Three box passes come close to a Gaussian of this sigma (in pixels).
void gaussianBlur(QVector<float>& a, QVector<float>& tmp, int w, int h, double sigma)
{
	if (sigma < 0.3)
		return;
	const int r = qMax(1, int(std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)));
	tmp.resize(a.size());
	for (int pass = 0; pass < 3; ++pass)
	{
		for (int y = 0; y < h; ++y)
			boxBlurLine(a.constData() + y * w, tmp.data() + y * w, w, 1, r);
		for (int x = 0; x < w; ++x)
			boxBlurLine(tmp.constData() + x, a.data() + x, h, w, r);
	}
}

// The signed distance to the edge of the letters, positive inside, in pixels of a w x h canvas whose
// (margin, margin) is the corner of the shape's bounding box. Measured on a mask several times finer
// than the canvas: at canvas resolution the edge is a staircase, and a bevel lit from the side shows
// every step of it as a streak.
bool signedDistance(const QPainterPath& shape, double pxPerPt, int margin, int w, int h, QVector<float>& sd, Progress* pg)
{
	const qint64 fineLimit = 24000000;
	const int ss = qBound(1, int(std::sqrt(double(fineLimit) / (double(w) * h))), 4);
	const int fw = w * ss;
	const int fh = h * ss;
	QImage mask(fw, fh, QImage::Format_Grayscale8);
	mask.fill(Qt::black);
	{
		QPainter p(&mask);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.scale(ss, ss);
		p.translate(margin, margin);
		p.scale(pxPerPt, pxPerPt);
		p.fillPath(shape, Qt::white);
	}
	const qsizetype fn = qsizetype(fw) * fh;
	QVector<float> in(fn), out(fn);
	for (int y = 0; y < fh; ++y)
	{
		const uchar* c = mask.constScanLine(y);
		float* a = in.data() + qsizetype(y) * fw;
		float* b = out.data() + qsizetype(y) * fw;
		for (int x = 0; x < fw; ++x)
		{
			const bool inside = c[x] >= 128;
			a[x] = inside ? 1.0f : 0.0f;   // features = outside pixels
			b[x] = inside ? 0.0f : 1.0f;   // features = inside pixels
		}
	}
	mask = QImage();
	distanceTransform(in, fw, fh);
	if (pg)
		pg->percent = 25;
	if (pg && pg->cancel)
		return false;
	distanceTransform(out, fw, fh);
	if (pg)
		pg->percent = 45;
	if (pg && pg->cancel)
		return false;
	sd.resize(qsizetype(w) * h);
	const float norm = 1.0f / float(ss * ss * ss);   // the mean of ss x ss values, in canvas pixels
	for (int y = 0; y < h; ++y)
	{
		float* d = sd.data() + qsizetype(y) * w;
		for (int x = 0; x < w; ++x)
		{
			float sum = 0.0f;
			for (int yy = 0; yy < ss; ++yy)
			{
				const qsizetype row = (qsizetype(y) * ss + yy) * fw + qsizetype(x) * ss;
				for (int xx = 0; xx < ss; ++xx)
					sum += (in[row + xx] > 0.0f) ? in[row + xx] - 0.5f : -(out[row + xx] - 0.5f);
			}
			d[x] = sum * norm;
		}
	}
	return true;
}

// The highlight and shadow amounts for a w x h canvas. pxPerPt turns the settings' points into pixels.
Maps shade(const QPainterPath& shape, int margin, int w, int h, const Settings& s, double pxPerPt, Progress* pg)
{
	Maps m;
	m.w = w;
	m.h = h;
	const qsizetype n = qsizetype(w) * h;
	auto step = [pg](int percent) { if (pg) pg->percent = percent; return pg && pg->cancel; };

	QVector<float> sd, other;
	if (!signedDistance(shape, pxPerPt, margin, w, h, sd, pg))
		return Maps();

	// the height of the surface, in pixels
	const double sizePx = qMax(0.5, s.size * pxPerPt);
	const double gain = (s.depth / 100.0) * (s.up ? 1.0 : -1.0);
	const bool smooth = (s.technique == Smooth);
	for (qsizetype i = 0; i < n; ++i)
	{
		const double d = sd[i];
		double t = 0.0;
		double ramp = sizePx;
		switch (s.style)
		{
		case OuterBevel:   t = 1.0 + d / sizePx; break;
		case Emboss:       t = 0.5 + d / sizePx; break;
		case PillowEmboss: ramp = sizePx / 2.0; t = std::fabs(d) / ramp; break;
		case Deboss:       t = 1.0 - d / sizePx; break;
		default:           t = d / sizePx; break;
		}
		t = qBound(0.0, t, 1.0);
		if (smooth)
			t = t * t * (3.0 - 2.0 * t);
		sd[i] = float(t * ramp * gain);
	}
	if (step(50))
		return Maps();
	const double softenPx = s.soften * pxPerPt * 0.5;
	const double baseSigma = smooth ? qMax(0.7, sizePx * 0.18) : 0.7;
	gaussianBlur(sd, other, w, h, std::sqrt(baseSigma * baseSigma + softenPx * softenPx));
	if (step(70))
		return Maps();

	// light it
	const double a = s.angle * M_PI / 180.0;
	const double alt = qBound(0.0, s.altitude, 90.0) * M_PI / 180.0;
	const double lx = std::cos(a) * std::cos(alt);
	const double ly = -std::sin(a) * std::cos(alt);   // image y runs down
	const double lz = std::sin(alt);
	const double hlNorm = 1.0 / qMax(1.0 - lz, 0.05);
	const double shNorm = 1.0 / qMax(lz, 0.25);
	m.hl.resize(n);
	m.sh.resize(n);
	for (int y = 0; y < h; ++y)
	{
		const float* row = sd.constData() + qsizetype(y) * w;
		const float* up = sd.constData() + qsizetype(qMax(0, y - 1)) * w;
		const float* down = sd.constData() + qsizetype(qMin(h - 1, y + 1)) * w;
		uchar* hl = m.hl.data() + qsizetype(y) * w;
		uchar* sh = m.sh.data() + qsizetype(y) * w;
		for (int x = 0; x < w; ++x)
		{
			const double gx = (row[qMin(w - 1, x + 1)] - row[qMax(0, x - 1)]) * 0.5;
			const double gy = (down[x] - up[x]) * 0.5;
			const double light = (-gx * lx - gy * ly + lz) / std::sqrt(gx * gx + gy * gy + 1.0);
			const double diff = light - lz;
			hl[x] = uchar(qBound(0.0, diff * hlNorm, 1.0) * 255.0 + 0.5);
			sh[x] = uchar(qBound(0.0, -diff * shNorm, 1.0) * 255.0 + 0.5);
		}
	}
	step(80);
	return m;
}

// ---------------------------------------------------------------- rasters

struct Raster
{
	int w { 0 };
	int h { 0 };
	int ch { 3 };          // 3 = RGB, 4 = CMYK ink
	QVector<uchar> px;

	void create(int width, int height, int channels, const uchar* fill)
	{
		w = width; h = height; ch = channels;
		px.resize(qsizetype(w) * h * ch);
		uchar* p = px.data();
		for (qsizetype i = 0, n = qsizetype(w) * h; i < n; ++i, p += ch)
			for (int c = 0; c < ch; ++c)
				p[c] = fill[c];
	}
};

// Screen with the highlight colour, then Multiply with the shadow colour, in light.
// (ox, oy) is where the raster sits in the maps.
void blend(Raster& r, const Maps& m, int ox, int oy, const float hlLight[4], const float shLight[4], double hlOpacity, double shOpacity)
{
	const bool ink = (r.ch == 4);
	for (int y = 0; y < r.h; ++y)
	{
		const int my = y + oy;
		if (my < 0 || my >= m.h)
			continue;
		uchar* p = r.px.data() + qsizetype(y) * r.w * r.ch;
		const uchar* hl = m.hl.constData() + qsizetype(my) * m.w;
		const uchar* sh = m.sh.constData() + qsizetype(my) * m.w;
		for (int x = 0; x < r.w; ++x, p += r.ch)
		{
			const int mx = x + ox;
			if (mx < 0 || mx >= m.w || (hl[mx] == 0 && sh[mx] == 0))
				continue;
			const float a1 = float(hl[mx] / 255.0 * hlOpacity);
			const float a2 = float(sh[mx] / 255.0 * shOpacity);
			for (int c = 0; c < r.ch; ++c)
			{
				float b = (ink ? 255 - p[c] : p[c]) / 255.0f;
				b = b + a1 * hlLight[c] * (1.0f - b);
				b = b * (1.0f - a2 * (1.0f - shLight[c]));
				const int v = int(b * 255.0f + 0.5f);
				p[c] = uchar(qBound(0, ink ? 255 - v : v, 255));
			}
		}
	}
}

// Draws src through imgToDst onto a filled image of the wanted size. A strong
// reduction is averaged first: the painter alone would only sample.
QImage resample(const QImage& src, const QTransform& imgToDst, const QSize& size, QImage::Format format, const QColor& fill)
{
	QImage out(size, format);
	out.fill(fill);
	const double sc = std::sqrt(std::fabs(imgToDst.determinant()));
	QImage pre = src;
	QTransform t = imgToDst;
	if (sc > 0.0 && sc < 0.7)
	{
		const QSize ps(qMax(1, int(std::lround(src.width() * sc))), qMax(1, int(std::lround(src.height() * sc))));
		pre = src.scaled(ps, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		t = QTransform::fromScale(double(src.width()) / ps.width(), double(src.height()) / ps.height()) * imgToDst;
	}
	QPainter p(&out);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.setTransform(t);
	p.drawImage(0, 0, pre);
	p.end();
	return out;
}

bool writePng(const Raster& r, const QString& file, int dpi, const QByteArray& icc)
{
	QImage img(r.w, r.h, QImage::Format_RGB888);
	for (int y = 0; y < r.h; ++y)
		memcpy(img.scanLine(y), r.px.constData() + qsizetype(y) * r.w * 3, size_t(r.w) * 3);
	img.setDotsPerMeterX(int(std::lround(dpi / 0.0254)));
	img.setDotsPerMeterY(int(std::lround(dpi / 0.0254)));
	if (!icc.isEmpty())
	{
		const QColorSpace cs = QColorSpace::fromIccProfile(icc);
		if (cs.isValid())
			img.setColorSpace(cs);
	}
	return img.save(file, "PNG", 40);
}

bool writeOverlayPng(const QImage& img, const QString& file, int dpi)
{
	QImage copy = img;
	copy.setDotsPerMeterX(int(std::lround(dpi / 0.0254)));
	copy.setDotsPerMeterY(int(std::lround(dpi / 0.0254)));
	return copy.save(file, "PNG", 40);
}

bool writeCmykTiff(const Raster& r, const QString& file, int dpi, const QByteArray& icc)
{
	TIFF* tif = TIFFOpen(QFile::encodeName(file).constData(), "w");
	if (!tif)
		return false;
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uint32_t(r.w));
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uint32_t(r.h));
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
	TIFFSetField(tif, TIFFTAG_INKSET, INKSET_CMYK);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
	TIFFSetField(tif, TIFFTAG_PREDICTOR, 2);
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
	TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
	TIFFSetField(tif, TIFFTAG_XRESOLUTION, double(dpi));
	TIFFSetField(tif, TIFFTAG_YRESOLUTION, double(dpi));
	if (!icc.isEmpty())
		TIFFSetField(tif, TIFFTAG_ICCPROFILE, uint32_t(icc.size()), icc.constData());
	bool ok = true;
	for (int y = 0; y < r.h && ok; ++y)
		ok = TIFFWriteScanline(tif, const_cast<uchar*>(r.px.constData() + qsizetype(y) * r.w * 4), uint32_t(y), 0) >= 0;
	TIFFClose(tif);
	return ok;
}

// ---------------------------------------------------------------- one rendering job

struct Job
{
	Settings s;
	double scale { 1.0 };        // pixels per point
	QPainterPath shape;          // the letters, in points, bounding box at (0, 0)
	int wf { 0 };                // face size in pixels
	int hf { 0 };
	int mp { 2 };                // margin around the face, in pixels
	bool cmyk { false };
	bool solid { true };
	uchar solidPx[4] { 0, 0, 0, 0 };
	QImage baseImg;              // cmyk: C,M,Y,K in r,g,b,a
	QTransform imgToFace;        // base image pixels -> face pixels
	QByteArray icc;
	uchar backPx[4] { 255, 255, 255, 0 };
	float hlLight[4] { 1, 1, 1, 1 };
	float shLight[4] { 0, 0, 0, 0 };
	QRgb hlRgb { 0xffffffff };
	QRgb shRgb { 0xff000000 };
	bool wantFace { false };     // base (+ the part of the effect inside the letters, in Bake)
	bool wantBack { false };     // Bake: opaque frame behind the letters
	bool wantOverlays { false }; // Live
	QString faceFile, backFile, hlFile, shFile;
};

struct Rendered
{
	QImage cov;
	Raster face, back;
	QImage hl, sh;
	QString error;
	bool cancelled { false };
};

Raster baseRaster(const Job& j)
{
	Raster r;
	if (j.solid || j.baseImg.isNull())
	{
		r.create(j.wf, j.hf, j.cmyk ? 4 : 3, j.solidPx);
		return r;
	}
	const QSize size(j.wf, j.hf);
	if (!j.cmyk)
	{
		const QImage img = resample(j.baseImg, j.imgToFace, size, QImage::Format_RGB32, Qt::white);
		const uchar none[4] = { 255, 255, 255, 0 };
		r.create(j.wf, j.hf, 3, none);
		for (int y = 0; y < j.hf; ++y)
		{
			const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
			uchar* d = r.px.data() + qsizetype(y) * j.wf * 3;
			for (int x = 0; x < j.wf; ++x, d += 3)
			{
				d[0] = uchar(qRed(s[x])); d[1] = uchar(qGreen(s[x])); d[2] = uchar(qBlue(s[x]));
			}
		}
		return r;
	}
	// CMYK: the painter knows no four-ink image, so C,M,Y travel as an RGB image and K as a grey one.
	QImage cmy(j.baseImg.size(), QImage::Format_RGB32);
	QImage k(j.baseImg.size(), QImage::Format_Grayscale8);
	for (int y = 0; y < j.baseImg.height(); ++y)
	{
		const QRgb* s = reinterpret_cast<const QRgb*>(j.baseImg.constScanLine(y));
		QRgb* a = reinterpret_cast<QRgb*>(cmy.scanLine(y));
		uchar* b = k.scanLine(y);
		for (int x = 0; x < j.baseImg.width(); ++x)
		{
			a[x] = s[x] | 0xff000000;
			b[x] = uchar(qAlpha(s[x]));
		}
	}
	const QImage cmyOut = resample(cmy, j.imgToFace, size, QImage::Format_RGB32, Qt::black);
	const QImage kOut = resample(k, j.imgToFace, size, QImage::Format_Grayscale8, Qt::black);
	const uchar none[4] = { 0, 0, 0, 0 };
	r.create(j.wf, j.hf, 4, none);
	for (int y = 0; y < j.hf; ++y)
	{
		const QRgb* s = reinterpret_cast<const QRgb*>(cmyOut.constScanLine(y));
		const uchar* kk = kOut.constScanLine(y);
		uchar* d = r.px.data() + qsizetype(y) * j.wf * 4;
		for (int x = 0; x < j.wf; ++x, d += 4)
		{
			d[0] = uchar(qRed(s[x])); d[1] = uchar(qGreen(s[x])); d[2] = uchar(qBlue(s[x])); d[3] = kk[x];
		}
	}
	return r;
}

QImage overlay(const Maps& m, const QVector<uchar>& amount, QRgb color, double opacity, const QRect& part, const QImage* outsideOf)
{
	QImage img(part.size(), QImage::Format_ARGB32);
	const QRgb rgb = color & 0x00ffffff;
	for (int y = 0; y < part.height(); ++y)
	{
		QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
		const uchar* a = amount.constData() + qsizetype(y + part.y()) * m.w + part.x();
		const uchar* c = outsideOf ? outsideOf->constScanLine(y + part.y()) + part.x() : nullptr;
		for (int x = 0; x < part.width(); ++x)
		{
			double alpha = a[x] * opacity;
			if (c)
				alpha *= (255 - c[x]) / 255.0;
			d[x] = rgb | (QRgb(qBound(0, int(alpha + 0.5), 255)) << 24);
		}
	}
	return img;
}

void render(const Job& j, Rendered& out, Progress* pg)
{
	const int wc = j.wf + 2 * j.mp;
	const int hc = j.hf + 2 * j.mp;
	out.cov = QImage(wc, hc, QImage::Format_Grayscale8);
	out.cov.fill(Qt::black);
	{
		QPainter p(&out.cov);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.translate(j.mp, j.mp);
		p.scale(j.scale, j.scale);
		p.fillPath(j.shape, Qt::white);
	}
	if (pg)
		pg->percent = 5;
	const Maps maps = shade(j.shape, j.mp, wc, hc, j.s, j.scale, pg);
	if (maps.w == 0)
	{
		out.cancelled = true;
		return;
	}
	const double ho = j.s.highlightOpacity / 100.0;
	const double so = j.s.shadowOpacity / 100.0;
	if (j.wantFace)
	{
		out.face = baseRaster(j);
		if (j.s.mode == Bake && hasInner(j.s.style))
			blend(out.face, maps, j.mp, j.mp, j.hlLight, j.shLight, ho, so);
	}
	if (pg)
		pg->percent = 88;
	if (j.wantBack)
	{
		out.back.create(wc, hc, j.cmyk ? 4 : 3, j.backPx);
		blend(out.back, maps, 0, 0, j.hlLight, j.shLight, ho, so);
	}
	if (j.wantOverlays)
	{
		// Inside the letters only: the frames have the shape of the letters. Otherwise a rectangle,
		// and for Outer Bevel nothing may lie on the letters themselves.
		const QRect part = hasOuter(j.s.style) ? QRect(0, 0, wc, hc) : QRect(j.mp, j.mp, j.wf, j.hf);
		const QImage* outsideOf = (j.s.style == OuterBevel) ? &out.cov : nullptr;
		out.hl = overlay(maps, maps.hl, j.hlRgb, ho, part, outsideOf);
		out.sh = overlay(maps, maps.sh, j.shRgb, so, part, outsideOf);
	}
	if (pg)
		pg->percent = 92;
}

void renderAndSave(const Job& j, Rendered& out, Progress* pg)
{
	render(j, out, pg);
	if (out.cancelled)
		return;
	auto fail = [&out](const QString& file) { out.error = QObject::tr("Cannot write the image file:\n%1").arg(file); };
	if (j.wantFace && !j.faceFile.isEmpty())
	{
		if (!(j.cmyk ? writeCmykTiff(out.face, j.faceFile, j.s.dpi, j.icc) : writePng(out.face, j.faceFile, j.s.dpi, j.icc)))
			return fail(j.faceFile);
	}
	if (pg)
		pg->percent = 95;
	if (j.wantBack && !j.backFile.isEmpty())
	{
		if (!(j.cmyk ? writeCmykTiff(out.back, j.backFile, j.s.dpi, j.icc) : writePng(out.back, j.backFile, j.s.dpi, j.icc)))
			return fail(j.backFile);
	}
	if (j.wantOverlays)
	{
		if (!writeOverlayPng(out.hl, j.hlFile, j.s.dpi))
			return fail(j.hlFile);
		if (!writeOverlayPng(out.sh, j.shFile, j.s.dpi))
			return fail(j.shFile);
	}
	if (pg)
		pg->percent = 100;
}

// ---------------------------------------------------------------- what the selected item is

struct Base
{
	bool    solid { true };
	QString color { "Black" };
	double  shade { 100.0 };
	QString file;
	QString rel;                  // the same file, as a path from the document's folder: the folder may move
	double  sx { 1.0 }, sy { 1.0 }, ox { 0.0 }, oy { 0.0 }, rot { 0.0 };
	bool    flipH { false }, flipV { false };
	bool    coverFit { false };   // the letters changed size: fit the image to them again
	QString source;               // name of the parked original, if there is one
	bool    inlineOrig { false }; // the image was embedded in the document: "file" is a copy of it in the effects folder
	qint64  genSize { 0 };        // the image written into the face: file size and pixels. An embedded
	int     genW { 0 }, genH { 0 }; // copy of it has another name, but still these.

	QString toString() const
	{
		QMap<QString, QString> m;
		m["solid"] = solid ? "1" : "0";
		m["color"] = color;
		m["shade"] = QString::number(shade);
		m["file"] = file;
		m["rel"] = rel;
		m["sx"] = QString::number(sx, 'g', 12); m["sy"] = QString::number(sy, 'g', 12);
		m["ox"] = QString::number(ox, 'g', 12); m["oy"] = QString::number(oy, 'g', 12);
		m["rot"] = QString::number(rot, 'g', 12);
		m["fh"] = flipH ? "1" : "0"; m["fv"] = flipV ? "1" : "0";
		m["source"] = source;
		m["inl"] = inlineOrig ? "1" : "0";
		m["gs"] = QString::number(genSize); m["gw"] = QString::number(genW); m["gh"] = QString::number(genH);
		return encodeKV(m);
	}
	static Base fromString(const QString& text)
	{
		const QMap<QString, QString> m = decodeKV(text);
		Base b;
		b.solid = m.value("solid", "1") == "1";
		b.color = m.value("color", "Black");
		b.shade = m.value("shade", "100").toDouble();
		b.file = m.value("file");
		b.rel = m.value("rel");
		b.sx = m.value("sx", "1").toDouble(); b.sy = m.value("sy", "1").toDouble();
		b.ox = m.value("ox", "0").toDouble(); b.oy = m.value("oy", "0").toDouble();
		b.rot = m.value("rot", "0").toDouble();
		b.flipH = m.value("fh") == "1"; b.flipV = m.value("fv") == "1";
		b.source = m.value("source");
		b.inlineOrig = m.value("inl") == "1";
		b.genSize = m.value("gs").toLongLong(); b.genW = m.value("gw").toInt(); b.genH = m.value("gh").toInt();
		return b;
	}
};

struct Target
{
	QPointer<PageItem> face;      // the existing face, if any
	QPointer<PageItem> source;    // text frame / shape / group the letters are taken from, if any
	bool inPlace { false };       // the face keeps its shape and place; only its image changes
	QPainterPath shape;           // the letters, in the reference frame's own coordinates
	QTransform toPage;
	double rotation { 0.0 };
	Base base;
	bool baseIsCurrent { false }; // the base is what the face shows right now
	QString id;
	bool hasSettings { false };
	Settings settings;
	QString faceName;
	int layer { 0 };
	QString defaultBack { "White" };
};

// Where the recorded image is now: next to the document if it is there, else where it was.
QString recordedFile(const Base& b, const QString& documentFile)
{
	if (!b.rel.isEmpty() && !documentFile.isEmpty())
	{
		const QString moved = QDir::cleanPath(QFileInfo(documentFile).absoluteDir().absoluteFilePath(b.rel));
		if (QFileInfo::exists(moved))
			return moved;
	}
	return b.file;
}

bool isGeneratedFile(const QString& file)
{
	const QFileInfo fi(file);
	return fi.fileName().startsWith(kFilePrefix) && fi.absolutePath().endsWith(QStringLiteral("_effects"));
}

QString effectsDir(const QString& documentFile)
{
	const QFileInfo fi(documentFile);
	QString base = fi.fileName();
	for (const char* ext : { ".sla.gz", ".sla", ".scd.gz", ".scd" })
	{
		if (base.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
		{
			base.chop(int(strlen(ext)));
			break;
		}
	}
	return fi.absolutePath() + "/" + base + "_effects";
}

QString newId()
{
	return QString("%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
}

QList<PageItem*> piecesOf(ScribusDoc* doc, const QString& id)
{
	QList<PageItem*> pieces;
	if (id.isEmpty())
		return pieces;
	for (PageItem* item : std::as_const(*doc->Items))
	{
		const ObjectAttribute a = attributeOf(item, kAttrPiece);
		if (!a.name.isEmpty() && a.parameter == id)
			pieces << item;
	}
	return pieces;
}

PageItem* faceById(ScribusDoc* doc, const QString& id, const PageItem* notThis = nullptr)
{
	for (PageItem* item : std::as_const(*doc->Items))
	{
		if (item == notThis)
			continue;
		const ObjectAttribute a = attributeOf(item, kAttrFx);
		if (!a.name.isEmpty() && a.parameter == id)
			return item;
	}
	return nullptr;
}

bool leafColor(const PageItem* item, QString& color, double& shade)
{
	if (item->isGroup())
	{
		for (const PageItem* child : item->groupItemList)
			if (leafColor(child, color, shade))
				return true;
		return false;
	}
	if (item->isTextFrame())
	{
		if (item->itemText.length() == 0)
			return false;
		const CharStyle& cs = item->itemText.charStyle(0);
		color = cs.fillColor();
		shade = cs.fillShade();
	}
	else
	{
		color = item->fillColor();
		shade = item->fillShade();
	}
	return color != CommonStrings::None && !color.isEmpty();
}

// The outline of what the item shows, in its own coordinates.
QPainterPath itemPath(PageItem* item, bool topLevel = true)
{
	if (item->isTextFrame())
		return SuneerFillTextImage::lettersPath(item);
	if (item->isGroup())
	{
		QPainterPath all;
		const QTransform inverse = item->getTransform().inverted();
		for (PageItem* child : std::as_const(item->groupItemList))
		{
			const QPainterPath part = itemPath(child, false);
			if (!part.isEmpty())
				all.addPath((child->getTransform() * inverse).map(part));
		}
		all.setFillRule(Qt::WindingFill);
		return topLevel ? all.simplified() : all;
	}
	if (item->PoLine.size() < 4)
		return QPainterPath();
	if (!topLevel && (item->fillColor() == CommonStrings::None) && !item->isImageFrame())
		return QPainterPath();   // a rule or an unfilled outline inside the group is no letter
	QPainterPath p = item->PoLine.toQPainterPath(true);
	p.setFillRule(item->fillEvenOdd() ? Qt::OddEvenFill : Qt::WindingFill);
	return p;
}

bool resolve(ScribusDoc* doc, PageItem* sel, Target& t, QString* error)
{
	auto fail = [error](const QString& why) {
		if (error)
			*error = why;
		return false;
	};
	if (!doc || !sel)
		return fail(QObject::tr("Select one headline first: a text frame, a Fill Text with Image result, or a shape."));
	if (sel->isGroupChild())
		return fail(QObject::tr("The item is inside a group. Ungroup it, or select the whole group, first."));
	const int parkingLayer = doc->layerIDFromName(SuneerFillTextImage::originalTextLayerName());

	PageItem* face = nullptr;
	PageItem* source = nullptr;
	const ObjectAttribute pieceAttr = attributeOf(sel, kAttrPiece);
	if (!pieceAttr.name.isEmpty())
	{
		face = faceById(doc, pieceAttr.parameter);
		if (!face)
			return fail(QObject::tr("This frame belongs to a Bevel & Emboss whose letters are gone. Delete it."));
	}
	else if (sel->isImageFrame())
		face = sel;
	else if (sel->isTextFrame() || sel->isPolygon() || sel->isPolyLine() || sel->isGroup())
		source = sel;
	else
		return fail(QObject::tr("Text Effects works on a text frame, a Fill Text with Image result, a shape, or a group of shapes."));

	if (source)
	{
		if (source->isTextFrame())
		{
			if (source->prevInChain() || source->nextInChain())
				return fail(QObject::tr("The text frame is linked to other frames. Use a frame of its own for the headline."));
			if (source->itemText.length() == 0)
				return fail(QObject::tr("The text frame is empty."));
		}
		t.shape = itemPath(source);
		if (!source->isTextFrame() && !source->isGroup())
			t.shape = t.shape.simplified();
		t.shape.setFillRule(Qt::WindingFill);
		t.toPage = source->getTransform();
		t.rotation = source->rotation();
		const QString name = source->itemName() + QStringLiteral(" image");
		for (PageItem* candidate : std::as_const(*doc->Items))
			if (candidate->itemName() == name && candidate->isImageFrame())
				face = candidate;
		t.inPlace = false;
		t.faceName = name;
		if (source->isTextFrame() && source->fillColor() != CommonStrings::None && !source->fillColor().isEmpty())
			t.defaultBack = source->fillColor();
	}
	else
	{
		if (face->PoLine.size() < 4)
			return fail(QObject::tr("The frame has no shape."));
		t.shape = face->PoLine.toQPainterPath(true);
		t.shape.setFillRule(face->fillEvenOdd() ? Qt::OddEvenFill : Qt::WindingFill);
		t.toPage = face->getTransform();
		t.rotation = face->rotation();
		t.inPlace = true;
		t.faceName = face->itemName();
	}
	const QRectF bounds = t.shape.boundingRect();
	if (t.shape.isEmpty() || bounds.width() < 1.0 || bounds.height() < 1.0)
		return fail(QObject::tr("There are no letter outlines to work on (only spaces, or the font has no glyphs for the text)."));

	if (face)
	{
		const ObjectAttribute fx = attributeOf(face, kAttrFx);
		if (!fx.name.isEmpty())
		{
			t.settings = Settings::fromString(fx.value);
			t.hasSettings = true;
			t.id = fx.parameter;
		}
		const ObjectAttribute baseAttr = attributeOf(face, kAttrBase);
		const Base recorded = Base::fromString(baseAttr.value);
		// Does the face show an image we wrote? By its place, or, once it was embedded, by its size.
		const bool showsOurs = !baseAttr.name.isEmpty()
		                       && (isGeneratedFile(face->Pfile)
		                           || (recorded.genSize > 0 && face->OrigW == recorded.genW && face->OrigH == recorded.genH
		                               && QFileInfo(face->Pfile).size() == recorded.genSize));
		if (showsOurs)
		{
			t.base = recorded;   // what the face showed before is on record
			t.base.file = recordedFile(recorded, doc->documentFileName());
		}
		else if (!face->Pfile.isEmpty() && face->imageIsAvailable)
		{
			t.baseIsCurrent = true;
			t.base.inlineOrig = face->isInlineImage || face->isTempFile;
			t.base.solid = false;
			t.base.file = face->Pfile;
			t.base.sx = face->imageXScale(); t.base.sy = face->imageYScale();
			t.base.ox = face->imageXOffset(); t.base.oy = face->imageYOffset();
			t.base.rot = face->imageRotation();
			t.base.flipH = face->imageFlippedH(); t.base.flipV = face->imageFlippedV();
		}
		else if (!face->Pfile.isEmpty())
			return fail(QObject::tr("The image of the frame is missing:\n%1").arg(face->Pfile));
		else if (face->fillColor() != CommonStrings::None && !face->fillColor().isEmpty())
		{
			t.base.solid = true;
			t.base.color = face->fillColor();
			t.base.shade = face->fillShade();
		}
		else
			return fail(QObject::tr("The image frame is empty. Use Fill Text with Image on a text frame first, or select the text frame itself."));
		if (source && !t.base.solid)
			t.base.coverFit = qAbs(face->width() - bounds.width()) > 0.5 || qAbs(face->height() - bounds.height()) > 0.5;
		if (source && t.base.solid)
			leafColor(source, t.base.color, t.base.shade);   // the colour of the text may have changed too
		t.layer = face->m_layerID;
	}
	else
	{
		if (!leafColor(source, t.base.color, t.base.shade))
			return fail(QObject::tr("The item has no fill colour to use for the face of the letters."));
		t.base.solid = true;
		t.layer = source->m_layerID;
		if (t.layer == parkingLayer)
		{
			// a parked original without its face: the lowest layer that is not the parking layer
			for (const ScLayer& layer : std::as_const(doc->Layers))
				if (layer.ID != parkingLayer && (t.layer == parkingLayer || layer.Level < doc->layerLevelFromID(t.layer)))
					t.layer = layer.ID;
		}
	}
	if (source)
		t.base.source = source->itemName();
	// A copy of a face carries the id of the original: it must not take the original's frames.
	if (t.id.isEmpty() || (face && faceById(doc, t.id, face)))
		t.id = newId();
	t.face = face;
	t.source = source;
	return true;
}

QString lockProblem(ScribusDoc* doc, const Target& t)
{
	QList<PageItem*> items = piecesOf(doc, t.id);
	if (t.face)
		items << t.face;
	if (t.source)
		items << t.source;
	for (const PageItem* item : std::as_const(items))
	{
		if (item->locked())
			return QObject::tr("\"%1\" is locked. Unlock it first.").arg(item->itemName());
		if (doc->layerLocked(item->m_layerID))
			return QObject::tr("The layer of \"%1\" is locked. Unlock it first.").arg(item->itemName());
	}
	if (doc->layerLocked(t.layer))
		return QObject::tr("The layer the headline is on is locked. Unlock it first.");
	return QString();
}

// ---------------------------------------------------------------- generated files

bool readDocumentText(const QString& file, QByteArray& text)
{
	gzFile gz = gzopen(QFile::encodeName(file).constData(), "rb");   // reads a plain .sla as it is
	if (!gz)
		return false;
	char buffer[65536];
	int n;
	while ((n = gzread(gz, buffer, sizeof(buffer))) > 0)
		text.append(buffer, n);
	gzclose(gz);
	return n == 0 && !text.isEmpty();
}

// A generated image may go when nothing can show it any more: not the open
// document, not the document as it is saved on disk (it may be closed without
// saving), and, while the document is open, not an undo step of this session.
void sweep(ScribusDoc* doc, const QString& documentFile)
{
	if (documentFile.isEmpty())
		return;
	QDir dir(effectsDir(documentFile));
	if (!dir.exists())
		return;
	QByteArray saved;
	if (!readDocumentText(documentFile, saved))
		return;
	QSet<QString> used;
	if (doc)
	{
		QList<PageItem*> all = doc->getAllItems(doc->DocItems) + doc->getAllItems(doc->MasterItems) + doc->getAllItems(doc->FrameItems.values());
		for (const PageItem* item : std::as_const(all))
		{
			if (!item->Pfile.isEmpty())
				used.insert(QFileInfo(item->Pfile).absoluteFilePath());
			const ObjectAttribute baseAttr = attributeOf(item, kAttrBase);
			if (!baseAttr.name.isEmpty())
				used.insert(QFileInfo(recordedFile(Base::fromString(baseAttr.value), documentFile)).absoluteFilePath());
		}
	}
	const QStringList files = dir.entryList({ kFilePrefix + "*" }, QDir::Files);
	for (const QString& name : files)
	{
		const QString path = dir.absoluteFilePath(name);
		if (used.contains(path) || saved.contains(name.toUtf8()))
			continue;
		if (doc && g_sessionFiles.contains(path))
			continue;
		if (ScCore->fileWatcher->isWatching(path))
			ScCore->fileWatcher->removeFile(path);
		QFile::remove(path);
		g_sessionFiles.remove(path);
	}
	if (dir.isEmpty())
		dir.rmdir(dir.absolutePath());
}

// ---------------------------------------------------------------- building a job from a target

struct Plan
{
	Job job;
	QRectF bounds;        // of the letters, in the reference frame's coordinates
	double margin { 0 };  // points, around the bounds, for the frames that reach outside the letters
	bool faceIsGenerated { false };
	QSize baseSize;       // pixels of the base image
	Base base;            // with the cover fit worked out
};

bool loadBaseImage(ScribusDoc* doc, const PageItem* like, const QString& file, QImage& img, bool& cmyk, QByteArray& icc, QString* error)
{
	if (!QFileInfo(file).isReadable())
	{
		if (error)
			*error = QObject::tr("Cannot read the image the letters are filled with:\n%1").arg(file);
		return false;
	}
	const QString ext = QFileInfo(file).suffix().toLower();
	const int page = like ? like->pixm.imgInfo.actualPageNumber : 0;
	CMSettings cms(doc, QString(), Intent_Perceptual);
	cms.allowColorManagement(false);
	cms.setUseEmbeddedProfile(true);
	bool realCMYK = false;
	cmyk = false;
	bool ok = false;
	{
		ScImage raw;
		if ((ext == "tif" || ext == "tiff" || ext == "psd")
		    && raw.loadPicture(file, page, cms, ScImage::RawData, 72, &realCMYK)
		    && raw.imgInfo.colorspace == ColorSpaceCMYK && !raw.qImage().isNull())
		{
			img = raw.qImage().convertToFormat(QImage::Format_ARGB32);
			cmyk = true;
			ok = true;
		}
	}
	if (!ok)
	{
		ScImage rgb;
		if (!rgb.loadPicture(file, page, cms, ScImage::RGBData, 72, &realCMYK) || rgb.qImage().isNull())
		{
			if (error)
				*error = QObject::tr("The image could not be loaded:\n%1").arg(file);
			return false;
		}
		img = rgb.qImage().copy();
	}
	int components = 0;
	QByteArray profile;
	ScImage probe;
	probe.getEmbeddedProfile(file, &profile, &components);
	if (!profile.isEmpty() && components == (cmyk ? 4 : 3))
		icc = profile;
	return true;
}

QTransform baseTransform(const Base& b, double faceW, double faceH, double scale)
{
	QTransform t;
	t.scale(scale, scale);
	if (b.flipH)
	{
		t.translate(faceW, 0);
		t.scale(-1, 1);
	}
	if (b.flipV)
	{
		t.translate(0, faceH);
		t.scale(1, -1);
	}
	t.translate(b.ox * b.sx, b.oy * b.sy);
	t.rotate(b.rot);
	t.scale(b.sx, b.sy);
	return t;
}

void fillColors(const ScribusDoc* doc, Job& job, const Base& base)
{
	const Settings& s = job.s;
	lightOf(doc, resolveColor(doc, s.highlightColor), 100.0, job.cmyk, job.hlLight);
	lightOf(doc, resolveColor(doc, s.shadowColor), 100.0, job.cmyk, job.shLight);
	job.hlRgb = ScColorEngine::getRGBColor(resolveColor(doc, s.highlightColor), doc).rgb();
	job.shRgb = ScColorEngine::getRGBColor(resolveColor(doc, s.shadowColor), doc).rgb();
	float light[4];
	lightOf(doc, resolveColor(doc, s.backColor), 100.0, job.cmyk, light);
	for (int c = 0; c < 4; ++c)
		job.backPx[c] = uchar(std::lround((job.cmyk ? 1.0f - light[c] : light[c]) * 255.0f));
	if (base.solid)
	{
		lightOf(doc, resolveColor(doc, base.color), base.shade, job.cmyk, light);
		for (int c = 0; c < 4; ++c)
			job.solidPx[c] = uchar(std::lround((job.cmyk ? 1.0f - light[c] : light[c]) * 255.0f));
	}
}

int marginPixels(const Settings& s, double scale)
{
	if (!hasOuter(s.style))
		return 2;
	const double sizePx = s.size * scale;
	return int(std::ceil(sizePx * 1.6 + s.soften * scale * 1.5 + 4.0));
}

// ---------------------------------------------------------------- frames

PageItem* addFrame(ScribusDoc* doc, const QPainterPath* letters, const QPointF& origin, const QSizeF& size, double rotation, const QString& name, int layer)
{
	const int z = doc->itemAdd(PageItem::ImageFrame, letters ? PageItem::Unspecified : PageItem::Rectangle, origin.x(), origin.y(),
	                           size.width(), size.height(), 0.0, CommonStrings::None, CommonStrings::None);
	PageItem* item = doc->Items->at(z);
	if (letters)
	{
		QPainterPath path = *letters;
		item->PoLine.resize(0);
		item->PoLine.fromQPainterPath(path, true);
		item->setFillEvenOdd(letters->fillRule() == Qt::OddEvenFill);
		item->ClipEdited = true;
		item->FrameType = 3;
		item->OldB2 = item->width();
		item->OldH2 = item->height();
		item->Clip = flattenPath(item->PoLine, item->Segments);
		item->ContourLine = item->PoLine.copy();
	}
	item->setRotation(rotation);
	item->setItemName(name);
	item->m_layerID = layer;
	doc->setRedrawBounding(item);
	return item;
}

bool showImage(ScribusDoc* doc, PageItem* item, const QString& file, double sx, double sy, double ox, double oy, double rot, bool flipH, bool flipV)
{
	if (item->Pfile != file || !item->imageIsAvailable)
	{
		doc->loadPict(file, item, false, false);
		if (!item->imageIsAvailable)
			return false;
	}
	item->setImageScalingMode(true, true);
	if (item->imageFlippedH() != flipH)
		item->setImageFlippedH(flipH);
	if (item->imageFlippedV() != flipV)
		item->setImageFlippedV(flipV);
	if (item->imageRotation() != rot)
		item->setImageRotation(rot);
	item->setImageXYScale(sx, sy);
	item->setImageXYOffset(ox, oy);
	return true;
}

void deleteItem(ScribusDoc* doc, PageItem* item)
{
	Selection gone(doc, false);
	gone.addItem(item);
	doc->itemSelection_DeleteItem(&gone);
}

int ensureParkingLayer(ScribusDoc* doc)
{
	// Made outside the undo step, flags set every time: see Fill Text with Image.
	int layerID = doc->layerIDFromName(SuneerFillTextImage::originalTextLayerName());
	const bool undoWasOn = UndoManager::undoEnabled();
	UndoManager::instance()->setUndoEnabled(false);
	if (layerID < 0)
	{
		const int activeLayer = doc->activeLayer();
		layerID = doc->addLayer(SuneerFillTextImage::originalTextLayerName(), false);
		doc->setActiveLayer(activeLayer);
	}
	doc->setLayerPrintable(layerID, false);
	doc->setLayerVisible(layerID, false);
	UndoManager::instance()->setUndoEnabled(undoWasOn);
	return layerID;
}

void refresh(ScribusMainWindow* mw)
{
	ScribusDoc* doc = mw->doc;
	mw->layerPalette->setDoc(doc);
	mw->rebuildLayersList();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	if (mw->view)
		mw->view->DrawNew();
}

// Sizes the job; lowers the resolution when the image would be too large.
void sizeJob(Job& job, const QRectF& bounds, QStringList* warnings)
{
	int dpi = job.s.dpi;
	while (true)
	{
		job.scale = dpi / 72.0;
		job.wf = qMax(1, int(std::ceil(bounds.width() * job.scale)));
		job.hf = qMax(1, int(std::ceil(bounds.height() * job.scale)));
		job.mp = marginPixels(job.s, job.scale);
		const qint64 wc = job.wf + 2 * job.mp;
		const qint64 hc = job.hf + 2 * job.mp;
		if ((wc * hc <= kMaxPixels && wc <= kMaxSide && hc <= kMaxSide) || dpi <= 36)
			break;
		const double f = qMin(std::sqrt(double(kMaxPixels) / double(wc * hc)), double(kMaxSide) / double(qMax(wc, hc)));
		dpi = qMax(36, qMin(dpi - 1, int(dpi * f)));
	}
	if (dpi != job.s.dpi)
	{
		if (warnings)
			*warnings << QObject::tr("The headline is very large: the effect image was made at %1 dpi instead of %2 dpi to keep it below %3 megapixels.")
			             .arg(dpi).arg(job.s.dpi).arg(kMaxPixels / 1000000);
		job.s.dpi = dpi;
	}
}

// ---------------------------------------------------------------- presets

struct Preset
{
	const char* name;
	int style, technique;
	double depth;
	bool up;
	double size, soften, angle, altitude;
	const char* hl;
	double hlOpacity;
	const char* sh;
	double shOpacity;
};

const Preset kPresets[] = {
	{ "Classic Emboss", Emboss,       Smooth,     100.0, true, 3.0, 0.0, 120.0, 30.0, "White",             75.0, "Black",            75.0 },
	{ "Gold Bevel",     InnerBevel,   ChiselHard, 250.0, true, 4.0, 0.5, 120.0, 35.0, "Emboss Gold Light", 90.0, "Emboss Gold Dark", 80.0 },
	{ "Stamped",        Deboss,       Smooth,     150.0, true, 2.5, 0.5, 120.0, 30.0, "White",             50.0, "Black",            70.0 },
	{ "Pillow",         PillowEmboss, Smooth,     120.0, true, 5.0, 1.0, 120.0, 30.0, "White",             75.0, "Black",            75.0 },
};

} // namespace

// ================================================================ public

QString Settings::toString() const
{
	QMap<QString, QString> m;
	m["style"] = QString::number(style);
	m["technique"] = QString::number(technique);
	m["depth"] = QString::number(depth);
	m["up"] = up ? "1" : "0";
	m["size"] = QString::number(size);
	m["soften"] = QString::number(soften);
	m["angle"] = QString::number(angle);
	m["altitude"] = QString::number(altitude);
	m["hl"] = highlightColor;
	m["hlo"] = QString::number(highlightOpacity);
	m["sh"] = shadowColor;
	m["sho"] = QString::number(shadowOpacity);
	m["dpi"] = QString::number(dpi);
	m["mode"] = QString::number(mode);
	m["back"] = backColor;
	return encodeKV(m);
}

Settings Settings::fromString(const QString& text)
{
	const QMap<QString, QString> m = decodeKV(text);
	Settings s;
	auto num = [&m](const char* key, double fallback, double low, double high) {
		bool ok = false;
		const double v = m.value(key).toDouble(&ok);
		return ok ? qBound(low, v, high) : fallback;
	};
	s.style = int(num("style", s.style, InnerBevel, Deboss));
	s.technique = int(num("technique", s.technique, Smooth, ChiselHard));
	s.depth = num("depth", s.depth, 1.0, 1000.0);
	s.up = m.value("up", "1") == "1";
	s.size = num("size", s.size, 0.1, 100.0);
	s.soften = num("soften", s.soften, 0.0, 50.0);
	s.angle = num("angle", s.angle, -360.0, 360.0);
	s.altitude = num("altitude", s.altitude, 0.0, 90.0);
	s.highlightColor = m.value("hl", s.highlightColor);
	s.highlightOpacity = num("hlo", s.highlightOpacity, 0.0, 100.0);
	s.shadowColor = m.value("sh", s.shadowColor);
	s.shadowOpacity = num("sho", s.shadowOpacity, 0.0, 100.0);
	s.dpi = (int(num("dpi", s.dpi, 72, 1200)) >= 600) ? 600 : 300;
	s.mode = int(num("mode", s.mode, Bake, Live));
	s.backColor = m.value("back", s.backColor);
	return s;
}

QStringList SuneerTextEffects::presetNames()
{
	QStringList names;
	for (const Preset& p : kPresets)
		names << QString::fromLatin1(p.name);
	return names;
}

bool SuneerTextEffects::applyPreset(const QString& name, Settings& s)
{
	for (const Preset& p : kPresets)
	{
		if (name != QLatin1String(p.name))
			continue;
		s.style = p.style; s.technique = p.technique; s.depth = p.depth; s.up = p.up;
		s.size = p.size; s.soften = p.soften; s.angle = p.angle; s.altitude = p.altitude;
		s.highlightColor = QString::fromLatin1(p.hl); s.highlightOpacity = p.hlOpacity;
		s.shadowColor = QString::fromLatin1(p.sh); s.shadowOpacity = p.shOpacity;
		return true;
	}
	return false;
}

bool SuneerTextEffects::canRunOn(const PageItem* item)
{
	if (!item || item->isGroupChild())
		return false;
	if (item->isTextFrame())
		return item->itemText.length() > 0;
	return item->isImageFrame() || item->isPolygon() || item->isPolyLine() || item->isGroup();
}

void SuneerTextEffects::deletePieces(ScribusDoc* doc, PageItem* face)
{
	if (!doc || !face)
		return;
	const ObjectAttribute fx = attributeOf(face, kAttrFx);
	if (fx.name.isEmpty() || faceById(doc, fx.parameter, face))
		return;
	const QList<PageItem*> pieces = piecesOf(doc, fx.parameter);
	for (PageItem* piece : pieces)
		deleteItem(doc, piece);
}

void SuneerTextEffects::sweepUnusedFiles(const QString& documentFile)
{
	sweep(nullptr, documentFile);
}

bool SuneerTextEffects::apply(ScribusMainWindow* mw, PageItem* item, const Settings& settings, QString* error, QStringList* warnings)
{
	auto fail = [error](const QString& why) {
		if (error)
			*error = why;
		return false;
	};
	if (!mw || !mw->HaveDoc || !mw->doc)
		return fail(QObject::tr("There is no document."));
	ScribusDoc* doc = mw->doc;
	if (!doc->hasName || doc->documentFileName().isEmpty())
		return fail(QObject::tr("Save the document first: the effect images are kept in a folder next to it."));
	Target t;
	if (!resolve(doc, item, t, error))
		return false;
	const QString locked = lockProblem(doc, t);
	if (!locked.isEmpty())
		return fail(locked);

	// ---- what has to be made
	Plan plan;
	Job& job = plan.job;
	job.s = settings;
	plan.bounds = t.shape.boundingRect();
	plan.base = t.base;
	job.shape = t.shape.translated(-plan.bounds.topLeft());
	job.shape.setFillRule(t.shape.fillRule());
	job.solid = plan.base.solid;
	if (!plan.base.solid)
	{
		if (!loadBaseImage(doc, t.face, plan.base.file, job.baseImg, job.cmyk, job.icc, error))
			return false;
		if (plan.base.coverFit && job.baseImg.width() > 0 && job.baseImg.height() > 0)
		{
			const double fit = qMax(plan.bounds.width() / job.baseImg.width(), plan.bounds.height() / job.baseImg.height());
			plan.base.sx = plan.base.sy = fit;
			plan.base.ox = ((plan.bounds.width() - job.baseImg.width() * fit) / 2.0) / fit;
			plan.base.oy = ((plan.bounds.height() - job.baseImg.height() * fit) / 2.0) / fit;
			plan.base.rot = 0.0;
		}
	}
	else
		job.cmyk = resolveColor(doc, plan.base.color).getColorModel() != colorModelRGB;
	sizeJob(job, plan.bounds, warnings);
	if (!plan.base.solid)
		job.imgToFace = baseTransform(plan.base, plan.bounds.width(), plan.bounds.height(), job.scale);
	fillColors(doc, job, plan.base);
	const bool bake = (job.s.mode == Bake);
	// The face needs an image of its own when the effect is written into it, or when it has no image at all.
	job.wantFace = plan.base.solid || (bake && hasInner(job.s.style));
	job.wantBack = bake && hasOuter(job.s.style);
	job.wantOverlays = !bake;

	const QString dirPath = effectsDir(doc->documentFileName());
	if (!QDir().mkpath(dirPath))
		return fail(QObject::tr("Cannot create the folder for the effect images:\n%1").arg(dirPath));
	const QString stamp = QString("%1_%2").arg(t.id, QString::number(QDateTime::currentMSecsSinceEpoch(), 36));
	const QString ext = job.cmyk ? QStringLiteral(".tif") : QStringLiteral(".png");
	QStringList written;
	if (!plan.base.solid && t.baseIsCurrent && plan.base.inlineOrig)
	{
		// The image is embedded in the document and its file is a temporary one: keep a copy
		// that is still there next week, for the next Apply and for Remove Effect.
		const QString copy = dirPath + "/" + kFilePrefix + stamp + "_base." + QFileInfo(plan.base.file).suffix();
		if (!QFile::copy(plan.base.file, copy))
			return fail(QObject::tr("Cannot write the image file:\n%1").arg(copy));
		plan.base.file = copy;
		written << copy;
	}
	job.faceFile = dirPath + "/" + kFilePrefix + stamp + "_face" + ext;
	job.backFile = dirPath + "/" + kFilePrefix + stamp + "_back" + ext;
	job.hlFile = dirPath + "/" + kFilePrefix + stamp + "_highlight.png";
	job.shFile = dirPath + "/" + kFilePrefix + stamp + "_shadow.png";

	// ---- make the images, off the GUI thread
	auto progress = std::make_shared<Progress>();
	auto rendered = std::make_shared<Rendered>();
	auto jobCopy = std::make_shared<Job>(job);
	{
		QProgressDialog dlg(QObject::tr("Making the Bevel & Emboss images..."), QObject::tr("Cancel"), 0, 100, mw);
		dlg.setObjectName("suneerTextEffectsProgress");
		dlg.setWindowTitle(QObject::tr("Text Effects"));
		dlg.setWindowModality(Qt::WindowModal);
		dlg.setMinimumDuration(400);
		dlg.setAutoClose(false);
		dlg.setAutoReset(false);
		QFutureWatcher<void> watcher;
		QEventLoop loop;
		QTimer timer;
		QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
		QObject::connect(&timer, &QTimer::timeout, &dlg, [&] {
			dlg.setValue(qMin(99, progress->percent.load()));
			if (dlg.wasCanceled())
				progress->cancel = true;
		});
		timer.start(100);
		watcher.setFuture(QtConcurrent::run([jobCopy, rendered, progress] { renderAndSave(*jobCopy, *rendered, progress.get()); }));
		if (!watcher.isFinished())
			loop.exec();
		watcher.waitForFinished();
		timer.stop();
		dlg.reset();
	}
	if (job.wantFace) written << job.faceFile;
	if (job.wantBack) written << job.backFile;
	if (job.wantOverlays) written << job.hlFile << job.shFile;
	auto discard = [&written] {
		for (const QString& f : std::as_const(written))
			QFile::remove(f);
	};
	if (rendered->cancelled || progress->cancel)
	{
		discard();
		return fail(QObject::tr("Cancelled. Nothing was changed."));
	}
	if (!rendered->error.isEmpty())
	{
		discard();
		return fail(rendered->error);
	}
	// The items may have gone while the event loop ran.
	if ((t.inPlace && !t.face) || (!t.inPlace && !t.source))
	{
		discard();
		return fail(QObject::tr("The headline was removed while the images were made."));
	}
	for (const QString& f : std::as_const(written))
		g_sessionFiles.insert(f);

	// ---- change the document: one undo step
	const double m = job.mp / job.scale;
	const double imageScale = 72.0 / job.s.dpi;
	const QList<PageItem*> oldPieces = piecesOf(doc, t.id);
	PageItem* source = t.source;
	PageItem* face = t.face;
	const int parkingLayer = t.inPlace ? -1 : ensureParkingLayer(doc);

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                        QObject::tr("Bevel & Emboss"), QString(), nullptr);
	doc->m_Selection->delaySignalsOn();
	doc->m_Selection->clear();
	for (PageItem* piece : oldPieces)
		deleteItem(doc, piece);

	const QPointF faceOrigin = t.toPage.map(plan.bounds.topLeft());
	const QPointF outerOrigin = t.toPage.map(plan.bounds.topLeft() - QPointF(m, m));
	const QSizeF outerSize((job.wf + 2 * job.mp) / job.scale, (job.hf + 2 * job.mp) / job.scale);
	bool ok = true;

	PageItem* back = nullptr;
	if (job.wantBack)
	{
		back = addFrame(doc, nullptr, outerOrigin, outerSize, t.rotation, t.faceName + QStringLiteral(" emboss back"), t.layer);
		ok = ok && showImage(doc, back, job.backFile, imageScale, imageScale, 0, 0, 0, false, false);
		setAttribute(back, kAttrPiece, QStringLiteral("back"), t.id);
	}

	if (!t.inPlace)
	{
		// the letters come from the text frame / shape: a new face, the old one goes
		struct Look { bool has { false }; double lineWidth { 0 }; QString lineColor; int flow { 0 };
		              bool shadow { false }; QString shColor; int shShade { 100 }; double shBlur { 0 }, shX { 0 }, shY { 0 }, shOpacity { 0 }; int shBlend { 0 };
		              QString profile; bool useEmbedded { true }; eRenderIntent intent { Intent_Perceptual }; } look;
		if (face)
		{
			look.has = true;
			look.lineWidth = face->lineWidth(); look.lineColor = face->lineColor(); look.flow = face->textFlowMode();
			look.shadow = face->hasSoftShadow(); look.shColor = face->softShadowColor(); look.shShade = face->softShadowShade();
			look.shBlur = face->softShadowBlurRadius(); look.shX = face->softShadowXOffset(); look.shY = face->softShadowYOffset();
			look.shOpacity = face->softShadowOpacity(); look.shBlend = face->softShadowBlendMode();
			look.profile = face->ImageProfile; look.useEmbedded = face->UseEmbedded; look.intent = face->ImageIntent;
			deleteItem(doc, face);
		}
		else
			look.flow = source->textFlowMode();
		face = addFrame(doc, &job.shape, faceOrigin, plan.bounds.size(), t.rotation, t.faceName, t.layer);
		face->setTextFlowMode(static_cast<PageItem::TextFlowMode>(look.flow));
		if (look.has)
		{
			// how the image of the old face was colour managed holds for the copy made from it
			face->ImageProfile = look.profile;
			face->UseEmbedded = look.useEmbedded;
			face->ImageIntent = look.intent;
			if (look.lineColor != CommonStrings::None && look.lineWidth > 0.0)
			{
				face->setLineColor(look.lineColor);
				face->setLineWidth(look.lineWidth);
			}
			if (look.shadow)
			{
				face->setHasSoftShadow(true);
				face->setSoftShadowColor(look.shColor); face->setSoftShadowShade(look.shShade);
				face->setSoftShadowBlurRadius(look.shBlur); face->setSoftShadowXOffset(look.shX); face->setSoftShadowYOffset(look.shY);
				face->setSoftShadowOpacity(look.shOpacity); face->setSoftShadowBlendMode(look.shBlend);
			}
		}
		if (source->m_layerID != parkingLayer)
		{
			doc->m_Selection->clear();
			doc->m_Selection->addItem(source);
			doc->itemSelection_SendToLayer(parkingLayer);
			doc->m_Selection->clear();
		}
	}
	else if (back)
	{
		// the new frame is on top of everything: the letters go above it
		doc->m_Selection->clear();
		doc->m_Selection->addItem(face);
		doc->bringItemSelectionToFront();
		doc->m_Selection->clear();
	}

	plan.base.genSize = 0;
	plan.base.genW = plan.base.genH = 0;
	if (job.wantFace)
	{
		ok = ok && showImage(doc, face, job.faceFile, imageScale, imageScale, 0, 0, 0, false, false);
		plan.base.genSize = QFileInfo(job.faceFile).size();
		plan.base.genW = job.wf;
		plan.base.genH = job.hf;
	}
	else if (!(t.inPlace && t.baseIsCurrent))   // else the face shows it already
	{
		ok = ok && showImage(doc, face, plan.base.file, plan.base.sx, plan.base.sy, plan.base.ox, plan.base.oy, plan.base.rot, plan.base.flipH, plan.base.flipV);
		if (ok && plan.base.inlineOrig)
			SuneerImageLinks::embedItem(doc, face);   // it was embedded before, so it is again
	}
	if (!plan.base.solid)
		plan.base.rel = QFileInfo(doc->documentFileName()).absoluteDir().relativeFilePath(plan.base.file);
	const ObjAttrVector attributesBefore = *face->getObjectAttributes();
	setAttribute(face, kAttrFx, job.s.toString(), t.id);
	setAttribute(face, kAttrBase, plan.base.toString(), t.id);
	recordAttributes(face, attributesBefore);

	if (job.wantOverlays)
	{
		const bool outer = hasOuter(job.s.style);
		const struct { const QString* file; const char* role; int blend; } layers[] = {
			{ &job.hlFile, "highlight", 4 /* Screen */ }, { &job.shFile, "shadow", 3 /* Multiply */ } };
		for (const auto& l : layers)
		{
			PageItem* over = outer ? addFrame(doc, nullptr, outerOrigin, outerSize, t.rotation, t.faceName + " emboss " + l.role, t.layer)
			                       : addFrame(doc, &job.shape, faceOrigin, plan.bounds.size(), t.rotation, t.faceName + " emboss " + l.role, t.layer);
			ok = ok && showImage(doc, over, *l.file, imageScale, imageScale, 0, 0, 0, false, false);
			over->setFillBlendmode(l.blend);
			setAttribute(over, kAttrPiece, QString::fromLatin1(l.role), t.id);
		}
	}

	doc->m_Selection->clear();
	doc->m_Selection->addItem(face);
	doc->m_Selection->delaySignalsOff();
	if (transaction)
		transaction.commit();
	if (!ok)
	{
		// a generated image did not load: take the whole step back
		if (UndoManager::undoEnabled())
			UndoManager::instance()->undo(1);
		refresh(mw);
		return fail(QObject::tr("An effect image could not be loaded into its frame. Nothing was changed."));
	}
	sweep(doc, doc->documentFileName());
	refresh(mw);
	return true;
}

bool SuneerTextEffects::remove(ScribusMainWindow* mw, PageItem* item, QString* error)
{
	auto fail = [error](const QString& why) {
		if (error)
			*error = why;
		return false;
	};
	if (!mw || !mw->HaveDoc || !mw->doc)
		return fail(QObject::tr("There is no document."));
	ScribusDoc* doc = mw->doc;
	Target t;
	if (!resolve(doc, item, t, error))
		return false;
	if (!t.face || !t.hasSettings)
		return fail(QObject::tr("This item has no Bevel & Emboss."));
	const QString locked = lockProblem(doc, t);
	if (!locked.isEmpty())
		return fail(locked);
	PageItem* face = t.face;
	const int parkingLayer = doc->layerIDFromName(SuneerFillTextImage::originalTextLayerName());
	// a plain-colour face was made by us: the original it came from returns
	PageItem* original = nullptr;
	if (t.base.solid && !t.base.source.isEmpty() && parkingLayer >= 0)
	{
		for (PageItem* candidate : std::as_const(*doc->Items))
			if (candidate->itemName() == t.base.source && candidate->m_layerID == parkingLayer)
				original = candidate;
	}
	if (t.base.solid && !original)
		return fail(QObject::tr("The original \"%1\" is not on the layer \"%2\" any more, so the letters cannot be put back.")
		            .arg(t.base.source, SuneerFillTextImage::originalTextLayerName()));
	if (original && (original->locked() || doc->layerLocked(parkingLayer)))
		return fail(QObject::tr("\"%1\" or its layer is locked. Unlock it first.").arg(original->itemName()));

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                        QObject::tr("Remove Bevel & Emboss"), QString(), nullptr);
	doc->m_Selection->delaySignalsOn();
	doc->m_Selection->clear();
	const QList<PageItem*> pieces = piecesOf(doc, t.id);
	for (PageItem* piece : pieces)
		deleteItem(doc, piece);
	PageItem* select = face;
	if (original)
	{
		const int layer = face->m_layerID;
		deleteItem(doc, face);
		doc->m_Selection->addItem(original);
		doc->itemSelection_SendToLayer(layer);
		select = original;
	}
	else
	{
		if (!t.baseIsCurrent)
		{
			showImage(doc, face, t.base.file, t.base.sx, t.base.sy, t.base.ox, t.base.oy, t.base.rot, t.base.flipH, t.base.flipV);
			if (t.base.inlineOrig)
				SuneerImageLinks::embedItem(doc, face);
		}
		const ObjAttrVector attributesBefore = *face->getObjectAttributes();
		clearAttribute(face, kAttrFx);
		clearAttribute(face, kAttrBase);
		recordAttributes(face, attributesBefore);
	}
	doc->m_Selection->clear();
	doc->m_Selection->addItem(select);
	doc->m_Selection->delaySignalsOff();
	if (transaction)
		transaction.commit();
	sweep(doc, doc->documentFileName());
	refresh(mw);
	return true;
}

// ================================================================ the dialog

namespace
{
// A small picture of the result. RGB and approximate: it is there to choose the look.
QImage previewImage(ScribusDoc* doc, const Target& t, const Settings& settings, const QImage& baseThumb, const QSize& room)
{
	const QRectF bounds = t.shape.boundingRect();
	Job job;
	job.s = settings;
	job.s.mode = Bake;
	const double outerPt = hasOuter(settings.style) ? settings.size * 1.6 + settings.soften * 1.5 : 0.0;
	job.scale = qMin((room.width() - 8) / (bounds.width() + 2 * outerPt), (room.height() - 8) / (bounds.height() + 2 * outerPt));
	job.scale = qBound(0.05, job.scale, settings.dpi / 72.0);
	job.shape = t.shape.translated(-bounds.topLeft());
	job.shape.setFillRule(t.shape.fillRule());
	job.wf = qMax(1, int(std::ceil(bounds.width() * job.scale)));
	job.hf = qMax(1, int(std::ceil(bounds.height() * job.scale)));
	job.mp = marginPixels(job.s, job.scale);
	job.cmyk = false;
	job.solid = t.base.solid || baseThumb.isNull();
	Base base = t.base;
	if (job.solid)
	{
		if (!t.base.solid)
		{
			base.solid = true;   // the image could not be read for the preview: a grey stands in
			base.color = "Black";
			base.shade = 40;
		}
	}
	else
	{
		job.baseImg = baseThumb;
		job.imgToFace = QTransform::fromScale(job.wf / double(baseThumb.width()), job.hf / double(baseThumb.height()));
	}
	fillColors(doc, job, base);
	job.wantFace = true;
	job.wantBack = true;
	if (!hasOuter(settings.style) || settings.mode == Live)
		job.backPx[0] = job.backPx[1] = job.backPx[2] = 255;   // nothing of ours is behind the letters: paper
	Rendered r;
	render(job, r, nullptr);
	if (r.cancelled)
		return QImage();
	const int wc = r.back.w;
	const int hc = r.back.h;
	QImage out(wc, hc, QImage::Format_RGB32);
	const bool outer = hasOuter(settings.style);
	for (int y = 0; y < hc; ++y)
	{
		QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
		const uchar* b = r.back.px.constData() + qsizetype(y) * wc * 3;
		const uchar* c = r.cov.constScanLine(y);
		const int fy = y - job.mp;
		for (int x = 0; x < wc; ++x, b += 3)
		{
			int rr = outer ? b[0] : 255, gg = outer ? b[1] : 255, bb = outer ? b[2] : 255;
			const int fx = x - job.mp;
			if (c[x] && fx >= 0 && fy >= 0 && fx < r.face.w && fy < r.face.h)
			{
				const uchar* f = r.face.px.constData() + (qsizetype(fy) * r.face.w + fx) * 3;
				const int a = c[x];
				rr = (f[0] * a + rr * (255 - a)) / 255;
				gg = (f[1] * a + gg * (255 - a)) / 255;
				bb = (f[2] * a + bb * (255 - a)) / 255;
			}
			d[x] = qRgb(rr, gg, bb);
		}
	}
	return out;
}

// What the face shows without the effect, as the face frame shows it, small.
QImage baseThumbnail(const Target& t, const QSize& size)
{
	if (t.base.solid)
		return QImage();
	QImageReader reader(t.base.file);
	reader.setAutoTransform(false);
	const QSize full = reader.size();
	if (!full.isValid() || full.isEmpty())
		return QImage();
	// read it reduced, then cut the part the letters cover
	const QRectF bounds = t.shape.boundingRect();
	Base b = t.base;
	if (b.coverFit)
	{
		const double fit = qMax(bounds.width() / full.width(), bounds.height() / full.height());
		b.sx = b.sy = fit;
		b.ox = ((bounds.width() - full.width() * fit) / 2.0) / fit;
		b.oy = ((bounds.height() - full.height() * fit) / 2.0) / fit;
		b.rot = 0.0;
	}
	const double thumbScale = qMin(1.0, 1600.0 / qMax(full.width(), full.height()));
	const QSize small(qMax(1, int(full.width() * thumbScale)), qMax(1, int(full.height() * thumbScale)));
	reader.setScaledSize(small);
	QImage img = reader.read();
	if (img.isNull())
		return QImage();
	img = img.convertToFormat(QImage::Format_RGB32);
	const double scale = qMin(size.width() / bounds.width(), size.height() / bounds.height());
	const QSize outSize(qMax(1, int(bounds.width() * scale)), qMax(1, int(bounds.height() * scale)));
	const QTransform toFace = QTransform::fromScale(double(full.width()) / small.width(), double(full.height()) / small.height())
	                          * baseTransform(b, bounds.width(), bounds.height(), scale);
	return resample(img, toFace, outSize, QImage::Format_RGB32, Qt::white);
}
}

void SuneerTextEffects::runForSelection(ScribusMainWindow* mw)
{
	if (!mw || !mw->HaveDoc || !mw->doc)
		return;
	ScribusDoc* doc = mw->doc;
	const QString title = QObject::tr("Text Effects");
	QPointer<PageItem> item = (doc->m_Selection->count() == 1) ? doc->m_Selection->itemAt(0) : nullptr;
	if (!item)
	{
		QMessageBox::information(mw, title, QObject::tr("Select one headline first: a text frame, a Fill Text with Image result, or a shape."));
		return;
	}
	Target target;
	QString error;
	if (!resolve(doc, item, target, &error))
	{
		QMessageBox::information(mw, title, error);
		return;
	}
	error = lockProblem(doc, target);
	if (!error.isEmpty())
	{
		QMessageBox::information(mw, title, error);
		return;
	}
	if (!doc->hasName || doc->documentFileName().isEmpty())
	{
		// the effect images are kept next to the .sla, so there has to be one
		if (QMessageBox::question(mw, title, QObject::tr("The document has not been saved yet.\n\nText Effects keeps its images in a folder next to the document, "
		                                                 "so the document has to be saved first. Save it now?"),
		                          QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save) != QMessageBox::Save)
			return;
		mw->slotFileSaveAs();
		if (!mw->HaveDoc || mw->doc != doc || !doc->hasName || !item)
			return;
	}

	QSettings prefs("Faircode", "ScribusTextEffects");
	Settings s = target.hasSettings ? target.settings : Settings::fromString(prefs.value("last").toString());
	if (!target.hasSettings)
		s.backColor = target.defaultBack;

	QDialog dlg(mw);
	dlg.setObjectName("suneerTextEffectsDialog");
	dlg.setWindowTitle(title);
	auto* top = new QVBoxLayout(&dlg);
	auto* presetRow = new QHBoxLayout;
	presetRow->addWidget(new QLabel(QObject::tr("Preset:"), &dlg));
	auto* presetCombo = new QComboBox(&dlg);
	presetCombo->setObjectName("preset");
	presetCombo->addItem(QObject::tr("Custom"));
	presetCombo->addItems(presetNames());
	presetRow->addWidget(presetCombo, 1);
	top->addLayout(presetRow);

	auto* columns = new QHBoxLayout;
	top->addLayout(columns);
	auto* box = new QGroupBox(QObject::tr("Bevel && Emboss"), &dlg);
	box->setObjectName("bevelEmboss");
	columns->addWidget(box);
	auto* form = new QFormLayout(box);

	auto* styleCombo = new QComboBox(box);
	styleCombo->setObjectName("style");
	styleCombo->addItems({ QObject::tr("Inner Bevel"), QObject::tr("Outer Bevel"), QObject::tr("Emboss"), QObject::tr("Pillow Emboss"), QObject::tr("Deboss (Stamped)") });
	form->addRow(QObject::tr("Style:"), styleCombo);
	auto* techniqueCombo = new QComboBox(box);
	techniqueCombo->setObjectName("technique");
	techniqueCombo->addItems({ QObject::tr("Smooth"), QObject::tr("Chisel Hard") });
	form->addRow(QObject::tr("Technique:"), techniqueCombo);
	auto spin = [box](const char* name, double low, double high, double stepSize, int decimals, const QString& suffix) {
		auto* w = new QDoubleSpinBox(box);
		w->setObjectName(name);
		w->setRange(low, high);
		w->setSingleStep(stepSize);
		w->setDecimals(decimals);
		w->setSuffix(suffix);
		return w;
	};
	auto* depthSpin = spin("depth", 1, 1000, 10, 0, " %");
	form->addRow(QObject::tr("Depth:"), depthSpin);
	auto* directionCombo = new QComboBox(box);
	directionCombo->setObjectName("direction");
	directionCombo->addItems({ QObject::tr("Up"), QObject::tr("Down") });
	form->addRow(QObject::tr("Direction:"), directionCombo);
	auto* sizeSpin = spin("size", 0.1, 100, 0.5, 1, " pt");
	form->addRow(QObject::tr("Size:"), sizeSpin);
	auto* softenSpin = spin("soften", 0, 50, 0.5, 1, " pt");
	form->addRow(QObject::tr("Soften:"), softenSpin);
	auto* angleSpin = spin("angle", -180, 360, 5, 0, QString::fromUtf8(" °"));
	form->addRow(QObject::tr("Light Angle:"), angleSpin);
	auto* altitudeSpin = spin("altitude", 0, 90, 5, 0, QString::fromUtf8(" °"));
	form->addRow(QObject::tr("Altitude:"), altitudeSpin);

	ColorList colors = doc->PageColors;
	const QStringList extra = builtinColorNames();
	for (const QString& name : extra)
	{
		ScColor c;
		if (!colors.contains(name) && builtinColor(name, c))
			colors.insert(name, c);
	}
	auto colorRow = [&](const char* comboName, const char* spinName, ColorCombo*& combo, QDoubleSpinBox*& opacity) {
		auto* row = new QHBoxLayout;
		combo = new ColorCombo(false, box);
		combo->setObjectName(comboName);
		combo->setColors(colors, false);
		row->addWidget(combo, 1);
		opacity = spin(spinName, 0, 100, 5, 0, " %");
		row->addWidget(opacity);
		return row;
	};
	ColorCombo* hlCombo = nullptr; QDoubleSpinBox* hlSpin = nullptr;
	ColorCombo* shCombo = nullptr; QDoubleSpinBox* shSpin = nullptr;
	form->addRow(QObject::tr("Highlight:"), colorRow("highlightColor", "highlightOpacity", hlCombo, hlSpin));
	form->addRow(QObject::tr("Shadow:"), colorRow("shadowColor", "shadowOpacity", shCombo, shSpin));
	auto* dpiCombo = new QComboBox(box);
	dpiCombo->setObjectName("resolution");
	dpiCombo->addItem(QObject::tr("300 dpi"), 300);
	dpiCombo->addItem(QObject::tr("600 dpi"), 600);
	form->addRow(QObject::tr("Resolution:"), dpiCombo);
	auto* modeCombo = new QComboBox(box);
	modeCombo->setObjectName("outputMode");
	modeCombo->addItem(QObject::tr("Bake (print-safe)"), Bake);
	modeCombo->addItem(QObject::tr("Live (transparency)"), Live);
	form->addRow(QObject::tr("Output:"), modeCombo);
	auto* backCombo = new ColorCombo(false, box);
	backCombo->setObjectName("backColor");
	backCombo->setColors(colors, false);
	auto* backLabel = new QLabel(QObject::tr("Behind the letters:"), box);
	form->addRow(backLabel, backCombo);

	auto* right = new QVBoxLayout;
	columns->addLayout(right, 1);
	auto* preview = new QLabel(&dlg);
	preview->setObjectName("preview");
	preview->setFixedSize(460, 300);
	preview->setAlignment(Qt::AlignCenter);
	preview->setFrameShape(QFrame::StyledPanel);
	preview->setStyleSheet("background: #ffffff;");
	right->addWidget(preview);
	auto* previewNote = new QLabel(QObject::tr("Preview at low resolution, in screen colours."), &dlg);
	previewNote->setWordWrap(true);
	right->addWidget(previewNote);
	right->addStretch(1);

	auto* note = new QLabel(&dlg);
	note->setObjectName("note");
	note->setWordWrap(true);
	note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	note->setMinimumHeight(note->fontMetrics().lineSpacing() * 3 + 4);
	top->addWidget(note);

	auto* buttons = new QDialogButtonBox(&dlg);
	auto* applyBtn = buttons->addButton(QObject::tr("Apply"), QDialogButtonBox::AcceptRole);
	applyBtn->setObjectName("apply");
	auto* removeBtn = buttons->addButton(QObject::tr("Remove Effect"), QDialogButtonBox::DestructiveRole);
	removeBtn->setObjectName("removeEffect");
	removeBtn->setEnabled(target.hasSettings);
	buttons->addButton(QDialogButtonBox::Cancel);
	top->addWidget(buttons);

	bool loading = false;
	auto show = [&](const Settings& v) {
		loading = true;
		styleCombo->setCurrentIndex(v.style);
		techniqueCombo->setCurrentIndex(v.technique);
		depthSpin->setValue(v.depth);
		directionCombo->setCurrentIndex(v.up ? 0 : 1);
		sizeSpin->setValue(v.size);
		softenSpin->setValue(v.soften);
		angleSpin->setValue(v.angle);
		altitudeSpin->setValue(v.altitude);
		hlCombo->setCurrentColor(colors.contains(v.highlightColor) ? v.highlightColor : QStringLiteral("White"));
		hlSpin->setValue(v.highlightOpacity);
		shCombo->setCurrentColor(colors.contains(v.shadowColor) ? v.shadowColor : QStringLiteral("Black"));
		shSpin->setValue(v.shadowOpacity);
		dpiCombo->setCurrentIndex(v.dpi >= 600 ? 1 : 0);
		modeCombo->setCurrentIndex(v.mode == Live ? 1 : 0);
		backCombo->setCurrentColor(colors.contains(v.backColor) ? v.backColor : QStringLiteral("White"));
		loading = false;
	};
	auto read = [&]() {
		Settings v;
		v.style = styleCombo->currentIndex();
		v.technique = techniqueCombo->currentIndex();
		v.depth = depthSpin->value();
		v.up = directionCombo->currentIndex() == 0;
		v.size = sizeSpin->value();
		v.soften = softenSpin->value();
		v.angle = angleSpin->value();
		v.altitude = altitudeSpin->value();
		v.highlightColor = hlCombo->currentColor();
		v.highlightOpacity = hlSpin->value();
		v.shadowColor = shCombo->currentColor();
		v.shadowOpacity = shSpin->value();
		v.dpi = dpiCombo->currentData().toInt();
		v.mode = modeCombo->currentData().toInt();
		v.backColor = backCombo->currentColor();
		return v;
	};

	const QImage thumb = baseThumbnail(target, preview->size());
	QTimer previewTimer;
	previewTimer.setSingleShot(true);
	previewTimer.setInterval(120);
	auto refreshPreview = [&]() {
		const Settings v = read();
		const bool outer = hasOuter(v.style);
		const bool bake = (v.mode == Bake);
		backLabel->setEnabled(outer && bake);
		backCombo->setEnabled(outer && bake);
		QString text;
		if (!bake)
			text = QObject::tr("<b>Warning:</b> Live puts the highlight and the shadow in two frames with Screen and Multiply blending over the letters. "
			                   "That is transparency: PDF/X-1a and PDF 1.3 output and PostScript printing cannot carry it and drop or spoil the effect. Use Bake for the press and for the printer.");
		else if (outer)
			text = QObject::tr("This style reaches outside the letters. Bake puts that part in an opaque frame behind them, filled with the colour "
			                   "chosen for \"Behind the letters\": pick the colour the headline stands on.");
		else
			text = QObject::tr("The highlight and the shadow are written into a copy of the image in the letters. Nothing transparent goes to the PDF.");
		note->setText(text);
		const QImage img = previewImage(doc, target, v, thumb, preview->size());
		if (!img.isNull())
			preview->setPixmap(QPixmap::fromImage(img));
	};
	QObject::connect(&previewTimer, &QTimer::timeout, &dlg, refreshPreview);
	auto changed = [&]() {
		if (loading)
			return;
		presetCombo->blockSignals(true);
		presetCombo->setCurrentIndex(0);
		presetCombo->blockSignals(false);
		previewTimer.start();
	};
	const QList<QComboBox*> combos = { styleCombo, techniqueCombo, directionCombo, dpiCombo, modeCombo, hlCombo, shCombo, backCombo };
	for (QComboBox* c : combos)
		QObject::connect(c, &QComboBox::currentIndexChanged, &dlg, changed);
	const QList<QDoubleSpinBox*> spins = { depthSpin, sizeSpin, softenSpin, angleSpin, altitudeSpin, hlSpin, shSpin };
	for (QDoubleSpinBox* w : spins)
		QObject::connect(w, &QDoubleSpinBox::valueChanged, &dlg, changed);
	QObject::connect(presetCombo, &QComboBox::currentIndexChanged, &dlg, [&](int index) {
		if (index <= 0)
			return;
		Settings v = read();
		if (applyPreset(presetCombo->itemText(index), v))
		{
			show(v);
			previewTimer.start();
		}
	});
	bool removeAsked = false;
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(removeBtn, &QPushButton::clicked, &dlg, [&] { removeAsked = true; dlg.accept(); });

	show(s);
	refreshPreview();
	if (dlg.exec() != QDialog::Accepted || !item || !mw->HaveDoc || mw->doc != doc)
		return;
	if (removeAsked)
	{
		if (!remove(mw, item, &error))
			QMessageBox::warning(mw, title, error);
		else
			mw->setStatusBarInfoText(QObject::tr("Bevel & Emboss removed."));
		return;
	}
	s = read();
	prefs.setValue("last", s.toString());
	QStringList warnings;
	if (!apply(mw, item, s, &error, &warnings))
	{
		QMessageBox::warning(mw, title, error);
		return;
	}
	if (!warnings.isEmpty())
		QMessageBox::information(mw, title, warnings.join("\n\n"));
	mw->setStatusBarInfoText(s.mode == Bake ? QObject::tr("Bevel & Emboss baked into the image: no transparency.")
	                                        : QObject::tr("Bevel & Emboss applied as live transparency frames."));
}
