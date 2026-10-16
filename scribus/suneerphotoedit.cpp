/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerphotoedit.h"

#include <cmath>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QLineF>
#include <QPainterPath>
#include <vector>
#include <algorithm>
#include <QProcess>
#include <QStandardPaths>
#include <QtMath>

#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "sccolorengine.h"
#include "scribusdoc.h"
#include "suneerbgremove.h"
#include "suneerimagelinks.h"
#include "suneerphotostore.h"
#include <QFile>
#include "ui/suneer_obit_model.h"
#include "undomanager.h"
#include "undostate.h"

namespace SuneerPhotoEdit
{
const char* const AttrSettings = "suneer.photoEdit";
const char* const AttrManual   = "suneer.photoEditManual";
const char* const AttrCut      = "suneer.photoEditCut";
const char* const AttrBrush    = "suneer.photoEditBrush";
const char* const AttrOutput   = "suneer.photoEditOutput";
const char* const AttrProtect  = "suneer.photoEditProtect";
const char* const AttrBgMode   = "suneer.photoEditBgMode";

namespace {
QString attrOf(const PageItem* item, const QString& name)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> list = item->getObjectAttributes(name);
	return list.isEmpty() ? QString() : list.first().value;
}

void setAttrOf(PageItem* item, const QString& name, const QString& value)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = 0; i < attrs.size(); ++i)
		if (attrs[i].name == name)
		{
			if (value.isEmpty())
				attrs.remove(i);
			else
				attrs[i].value = value;
			item->setObjectAttributes(&attrs);
			return;
		}
	if (value.isEmpty())
		return;
	ObjectAttribute a;
	a.name = name;
	a.type = QStringLiteral("string");
	a.value = value;
	attrs.append(a);
	item->setObjectAttributes(&attrs);
}

QJsonArray rectJson(const QRectF& r)
{
	return QJsonArray{r.x(), r.y(), r.width(), r.height()};
}

QRectF rectFromJson(const QJsonValue& v)
{
	const QJsonArray a = v.toArray();
	if (a.size() != 4)
		return QRectF();
	return QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
}
} // namespace

QJsonObject Settings::toJson() const
{
	return QJsonObject{
		{"rot90", rot90}, {"fine", fine}, {"flipH", flipH}, {"flipV", flipV},
		{"crop", rectJson(crop)}, {"free", freeAspect}, {"autoFace", autoFace},
		{"removeBg", removeBg}, {"bgMode", bgMode}, {"model", model}, {"bgColor", bgColor}, {"bgName", bgName},
		{"tone", tone}, {"refine", refine}, {"face", rectJson(face)},
		{"cutRect", QJsonArray{cutRect.x(), cutRect.y(), cutRect.width(), cutRect.height()}}, {"cutModel", cutModel}};
}

Settings Settings::fromJson(const QJsonObject& o)
{
	Settings s;
	if (o.isEmpty())
		return s;
	s.rot90 = ((o["rot90"].toInt() % 360) + 360) % 360;
	s.fine = qBound(-10.0, o["fine"].toDouble(), 10.0);
	s.flipH = o["flipH"].toBool();
	s.flipV = o["flipV"].toBool();
	s.crop = rectFromJson(o["crop"]);
	s.freeAspect = o["free"].toBool();
	s.autoFace = o["autoFace"].toBool();
	s.removeBg = o["removeBg"].toBool();
	s.bgMode = o["bgMode"].toString().isEmpty() ? (s.removeBg ? QStringLiteral("auto") : QStringLiteral("none")) : o["bgMode"].toString();
	if (s.bgMode == QLatin1String("none"))
		s.removeBg = false;
	else
		s.removeBg = true;
	if (!o["model"].toString().isEmpty())
		s.model = o["model"].toString();
	s.bgColor = o["bgColor"].toString();
	s.bgName = o["bgName"].toString();
	s.tone = qBound(0, o["tone"].toInt(), 100);
	s.refine = qBound(0, o["refine"].toInt(), 100);
	s.face = rectFromJson(o["face"]);
	const QRectF cr = rectFromJson(o["cutRect"]);
	s.cutRect = cr.isNull() ? QRect() : cr.toRect();
	s.cutModel = o["cutModel"].toString();
	return s;
}

QString sourcePathOf(const PageItem* item)
{
	if (!item)
		return QString();
	QString p = attrOf(item, QString::fromLatin1(SuneerPhotoStore::AttrOriginal));   // original/ next to the document
	if (!p.isEmpty() && QFileInfo::exists(p))
		return p;
	p = attrOf(item, QStringLiteral("SuneerImgOriginal"));
	if (!p.isEmpty() && QFileInfo::exists(p))
		return p;
	p = attrOf(item, QString::fromLatin1(SuneerBgRemove::AttrSource));
	if (!p.isEmpty() && QFileInfo::exists(p))
		return p;
	p = attrOf(item, QStringLiteral("obit.src"));
	if (!p.isEmpty() && QFileInfo::exists(p))
		return p;
	return item->Pfile;
}

QImage loadSource(const QString& path, QString* error)
{
	QImageReader rd(path);
	rd.setAutoTransform(true);
	QImage img = rd.read();
	if (img.isNull())
	{
		if (error)
			*error = QObject::tr("The picture could not be read: %1 (%2)").arg(path, rd.errorString());
		return QImage();
	}
	return img.convertToFormat(QImage::Format_ARGB32);
}

QString sidecarPathFor(const QString& outputPath)
{
	return outputPath.isEmpty() ? QString() : outputPath + QStringLiteral(".json");
}

Settings settingsOf(const PageItem* item)
{
	const QString js = attrOf(item, QString::fromLatin1(AttrSettings));
	if (!js.isEmpty())
		return Settings::fromJson(QJsonDocument::fromJson(js.toUtf8()).object());
	// no attribute (file from another PC / older save): the sidecar next to the edited file
	for (const QString& p : { attrOf(item, QString::fromLatin1(AttrOutput)), item ? item->Pfile : QString() })
	{
		const QString sc = sidecarPathFor(p);
		if (!sc.isEmpty() && QFileInfo::exists(sc))
		{
			QFile f(sc);
			if (f.open(QIODevice::ReadOnly))
				return Settings::fromJson(QJsonDocument::fromJson(f.readAll()).object()["settings"].toObject());
		}
	}
	return Settings();
}

bool sourceIsLocalOriginal(const PageItem* item, const QString& sourcePath)
{
	if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath))
		return false;
	const QString orig = attrOf(item, QString::fromLatin1(SuneerPhotoStore::AttrOriginal));
	return !orig.isEmpty() && QFileInfo(orig).absoluteFilePath() == QFileInfo(sourcePath).absoluteFilePath();
}

QRect cropRegionInSource(const Settings& s, const QSize& sourceSize)
{
	const QRect all(QPoint(0, 0), sourceSize);
	if (s.crop.isNull())
		return all;
	const QTransform t = transformFor(s, sourceSize);
	QRectF r = t.inverted().mapRect(s.crop);
	const double mx = r.width() * 0.10, my = r.height() * 0.10;
	r.adjust(-mx, -my, mx, my);
	return r.toAlignedRect().intersected(all);
}

bool isManual(const PageItem* item)
{
	return attrOf(item, QString::fromLatin1(AttrManual)) == QLatin1String("1");
}

bool isObitPhoto(const PageItem* item)
{
	if (!item || !item->isImageFrame())
		return false;
	return attrOf(item, QStringLiteral("news.model")) == QLatin1String("obituary") && attrOf(item, QStringLiteral("news.role")) == QLatin1String("photo");
}

Settings obitPreset(const ScribusDoc* doc)
{
	Q_UNUSED(doc);
	const SuneerObitModel m = SuneerObitModel::load();
	Settings s;
	s.autoFace = true;
	s.removeBg = true;
	s.bgMode = m.bgPolicy == QLatin1String("manual") ? QStringLiteral("manual") : QStringLiteral("auto");
	s.model = m.rembgModel;
	s.bgColor = QColor(m.bgRGB[0], m.bgRGB[1], m.bgRGB[2]).name();
	s.bgName = QStringLiteral("Obit BG");
	s.tone = m.toneStrength;
	return s;
}

QTransform transformFor(const Settings& s, const QSize& sourceSize, QSize* outSize)
{
	// flips in source space, then the rotation about the centre; the result
	// is translated so its bounding box starts at (0,0).
	QTransform t;
	const double w = sourceSize.width(), h = sourceSize.height();
	if (s.flipH)
	{
		t.translate(w, 0);
		t.scale(-1, 1);
	}
	if (s.flipV)
	{
		QTransform f;
		f.translate(0, h);
		f.scale(1, -1);
		t = t * f;
	}
	QTransform r;
	r.translate(w / 2.0, h / 2.0);
	r.rotate(s.rot90 + s.fine);
	r.translate(-w / 2.0, -h / 2.0);
	t = t * r;
	const QRectF bb = t.mapRect(QRectF(0, 0, w, h));
	QTransform shift;
	shift.translate(-bb.x(), -bb.y());
	t = t * shift;
	if (outSize)
		*outSize = QSize(qMax(1, qRound(bb.width())), qMax(1, qRound(bb.height())));
	return t;
}

QRectF detectFace(const QString& path, QString* error)
{
	const QString script = SuneerObitPhotoWorker::scriptPath();
	if (!QFileInfo::exists(script))
	{
		if (error)
			*error = QObject::tr("photo script not installed: %1").arg(script);
		return QRectF();
	}
	QProcess proc;
	proc.start(QStringLiteral("python3"), {script, QStringLiteral("--face"), path});
	if (!proc.waitForFinished(20000))
	{
		proc.kill();
		if (error)
			*error = QObject::tr("face detection timed out");
		return QRectF();
	}
	const QJsonObject o = QJsonDocument::fromJson(proc.readAllStandardOutput().trimmed()).object();
	const QJsonArray f = o["face"].toArray();
	if (f.size() != 4)
	{
		if (error)
			*error = o["error"].toString().isEmpty() ? QObject::tr("no face found") : o["error"].toString();
		return QRectF();
	}
	return QRectF(f[0].toDouble(), f[1].toDouble(), f[2].toDouble(), f[3].toDouble());
}

QRectF faceCrop(const QRectF& face, double aspectHoverW, const QSize& imageSize)
{
	if (face.isNull() || face.width() <= 0)
		return QRectF();
	const SuneerObitModel m = SuneerObitModel::load();
	const double faceW = m.faceW > 0 ? m.faceW : 0.60;
	const double faceTop = m.faceTop;
	double w = face.width() / faceW;
	double h = w * (aspectHoverW > 0 ? aspectHoverW : 1.0);
	const double cx = face.x() + face.width() / 2.0;
	double x = cx - w / 2.0;
	double y = face.y() - faceTop * w;
	Q_UNUSED(imageSize);
	return QRectF(x, y, w, h);   // may reach outside the picture: padded with the background
}

QImage toneMatch(const QImage& in, const QRectF& face, double strength)
{
	if (strength <= 0.0 || in.isNull())
		return in;
	QImage img = in.convertToFormat(QImage::Format_ARGB32);
	const int W = img.width(), H = img.height();
	std::vector<float> rgb(size_t(W) * H * 3);
	std::vector<quint8> alpha(size_t(W) * H);
	for (int y = 0; y < H; ++y)
	{
		const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < W; ++x)
		{
			const size_t i = size_t(y) * W + x;
			rgb[i * 3] = qRed(line[x]); rgb[i * 3 + 1] = qGreen(line[x]); rgb[i * 3 + 2] = qBlue(line[x]);
			alpha[i] = qAlpha(line[x]);
		}
	}
	std::vector<float> src(rgb);
	// 1. grey-world white balance on the mid-tones of opaque pixels
	double sum[3] { 0, 0, 0 };
	long n = 0;
	for (size_t i = 0; i < alpha.size(); ++i)
	{
		if (alpha[i] < 128)
			continue;
		const float mean = (rgb[i * 3] + rgb[i * 3 + 1] + rgb[i * 3 + 2]) / 3.0f;
		if (mean > 40 && mean < 220)
		{
			sum[0] += rgb[i * 3]; sum[1] += rgb[i * 3 + 1]; sum[2] += rgb[i * 3 + 2];
			++n;
		}
	}
	if (n > 100)
	{
		const double m0 = sum[0] / n, m1 = sum[1] / n, m2 = sum[2] / n, mm = (m0 + m1 + m2) / 3.0;
		const double gain[3] { qBound(0.8, mm / qMax(m0, 1.0), 1.25), qBound(0.8, mm / qMax(m1, 1.0), 1.25), qBound(0.8, mm / qMax(m2, 1.0), 1.25) };
		for (size_t i = 0; i < alpha.size(); ++i)
			for (int c = 0; c < 3; ++c)
				rgb[i * 3 + c] = float(rgb[i * 3 + c] * gain[c]);
	}
	// 2. auto levels, 1 % clip on the luminance of opaque pixels
	std::vector<float> lum;
	lum.reserve(alpha.size());
	for (size_t i = 0; i < alpha.size(); ++i)
		if (alpha[i] >= 128)
			lum.push_back(0.299f * rgb[i * 3] + 0.587f * rgb[i * 3 + 1] + 0.114f * rgb[i * 3 + 2]);
	if (lum.size() > 100)
	{
		std::vector<float> sorted(lum);
		std::sort(sorted.begin(), sorted.end());
		const float lo = sorted[size_t(sorted.size() * 0.01)], hi = sorted[size_t(sorted.size() * 0.99)];
		if (hi - lo > 20)
			for (size_t i = 0; i < alpha.size(); ++i)
				for (int c = 0; c < 3; ++c)
					rgb[i * 3 + c] = (rgb[i * 3 + c] - lo) * (255.0f / (hi - lo));
	}
	// 3. gentle S-curve
	const double k = 1.15, tk = std::tanh(k);
	for (size_t i = 0; i < rgb.size(); ++i)
	{
		const double v = qBound(0.0, double(rgb[i]), 255.0) / 255.0;
		rgb[i] = float(255.0 * (0.5 + 0.5 * std::tanh(k * (v - 0.5) * 2.0) / tk));
	}
	// 4. one target face luminance via gamma
	if (!face.isNull() && face.width() > 2 && face.height() > 2)
	{
		double fsum = 0;
		long fn = 0;
		const int x0 = qBound(0, int(face.left()), W), x1 = qBound(0, int(face.right()), W);
		const int y0 = qBound(0, int(face.top()), H), y1 = qBound(0, int(face.bottom()), H);
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
			{
				const size_t i = size_t(y) * W + x;
				fsum += 0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2];
				++fn;
			}
		if (fn > 0)
		{
			const double flum = fsum / fn;
			if (flum > 5)
			{
				const double gamma = qBound(0.7, std::log(150.0 / 255.0) / std::log(qMax(flum, 1.0) / 255.0), 1.4);
				for (size_t i = 0; i < rgb.size(); ++i)
					rgb[i] = float(255.0 * std::pow(qBound(0.0, double(rgb[i]) / 255.0, 1.0), gamma));
			}
		}
	}
	// 5. slight unsharp mask (3x3 blur approximation, amount 0.5)
	{
		std::vector<float> blur(rgb);
		for (int y = 1; y < H - 1; ++y)
			for (int x = 1; x < W - 1; ++x)
				for (int c = 0; c < 3; ++c)
				{
					float s = 0;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
							s += rgb[(size_t(y + dy) * W + (x + dx)) * 3 + c];
					blur[(size_t(y) * W + x) * 3 + c] = s / 9.0f;
				}
		for (size_t i = 0; i < rgb.size(); ++i)
			rgb[i] = qBound(0.0f, rgb[i] + 0.5f * (rgb[i] - blur[i]), 255.0f);
	}
	// blend
	const float st = float(qBound(0.0, strength, 1.0));
	for (int y = 0; y < H; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < W; ++x)
		{
			const size_t i = size_t(y) * W + x;
			const int r = qBound(0, int(src[i * 3] * (1 - st) + rgb[i * 3] * st + 0.5f), 255);
			const int g = qBound(0, int(src[i * 3 + 1] * (1 - st) + rgb[i * 3 + 1] * st + 0.5f), 255);
			const int b = qBound(0, int(src[i * 3 + 2] * (1 - st) + rgb[i * 3 + 2] * st + 0.5f), 255);
			line[x] = qRgba(r, g, b, alpha[i]);
		}
	}
	return img;
}

namespace {
// The RGB picture (opaque) transformed; outside the picture: white (the
// alpha map says it is transparent there), never black.
QImage transformed(const QImage& img, const QTransform& t, const QSize& outSize)
{
	QImage out(outSize, QImage::Format_ARGB32);
	out.fill(QColor(255, 255, 255, 255));
	QPainter p(&out);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setTransform(t);
	p.drawImage(0, 0, img);
	p.end();
	return out;
}

// A Grayscale8 map transformed the same way (0 outside the picture).
QImage transformedGray(const QImage& gray, const QTransform& t, const QSize& outSize)
{
	QImage out(outSize, QImage::Format_Grayscale8);
	out.fill(0);
	QPainter p(&out);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setTransform(t);
	p.drawImage(0, 0, gray);
	p.end();
	return out;
}

void setAlphaFrom(QImage& img, const QImage& gray)
{
	for (int y = 0; y < img.height(); ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		const uchar* a = gray.constScanLine(y);
		for (int x = 0; x < img.width(); ++x)
			line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a[x]);
	}
}

QImage scaledForPreview(const QImage& img, double* factor)
{
	const int maxSide = 1400;
	const int side = qMax(img.width(), img.height());
	if (side <= maxSide)
	{
		*factor = 1.0;
		return img;
	}
	*factor = double(maxSide) / side;
	return img.scaled(qRound(img.width() * *factor), qRound(img.height() * *factor), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
} // namespace

QImage renderWhole(const QImage& source0, const QImage& cut0, const QImage& brush0, const Settings& s, bool preview, QSize* fullSize, const QImage& protect0)
{
	if (source0.isNull())
		return QImage();
	double f = 1.0;
	QImage source = preview ? scaledForPreview(source0, &f) : source0;
	QImage cut = cut0, brush = brush0, protect = protect0;
	if (preview && f != 1.0)
	{
		if (!cut.isNull())
			cut = cut.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (!brush.isNull())
			brush = brush.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
		if (!protect.isNull())
			protect = protect.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
	}
	if (s.removeBg && s.refine > 0 && !cut.isNull() && s.bgMode != QLatin1String("manual"))
		cut = refineEdge(cut, s.refine);
	// RGB and alpha travel separately: the original pixels are never zeroed
	// in transparent areas, so a later restore shows the real picture.
	QSize outSize;
	const QTransform t = transformFor(s, source.size(), &outSize);
	QImage img = transformed(source.convertToFormat(QImage::Format_ARGB32), t, outSize);
	QImage alpha;
	if (s.removeBg)
		alpha = transformedGray(alphaMap(source.size(), cut, brush, protect, s.bgMode == QLatin1String("manual")), t, outSize);
	else
	{
		QImage opaque(source.size(), QImage::Format_Grayscale8);
		opaque.fill(255);
		alpha = transformedGray(opaque, t, outSize);
	}
	setAlphaFrom(img, alpha);   // before the tone, so the statistics ignore the removed background
	if (s.tone > 0)
	{
		QRectF faceT;
		if (!s.face.isNull())
			faceT = t.mapRect(QRectF(s.face.x() * f, s.face.y() * f, s.face.width() * f, s.face.height() * f));
		img = toneMatch(img, faceT, s.tone / 100.0);
	}
	if (fullSize)
	{
		QSize fs;
		transformFor(s, source0.size(), &fs);
		*fullSize = fs;
	}
	return img;
}

QImage render(const QImage& source0, const QImage& cut0, const QImage& brush0, const Settings& s, bool preview, QRectF* cropOut, const QImage& protect)
{
	QSize fullSize;
	QImage whole = renderWhole(source0, cut0, brush0, s, preview, &fullSize, protect);
	if (whole.isNull())
		return QImage();
	const double f = fullSize.width() > 0 ? double(whole.width()) / fullSize.width() : 1.0;
	QRectF crop = s.crop.isNull() ? QRectF(0, 0, fullSize.width(), fullSize.height()) : s.crop;
	if (cropOut)
		*cropOut = crop;
	const QRectF c(crop.x() * f, crop.y() * f, crop.width() * f, crop.height() * f);
	const QSize outSize(qMax(1, qRound(c.width())), qMax(1, qRound(c.height())));
	QImage out(outSize, QImage::Format_ARGB32);
	const QColor bg = (s.removeBg && !s.bgColor.isEmpty()) ? QColor(s.bgColor) : QColor();
	if (s.removeBg && !bg.isValid())
		out.fill(Qt::transparent);
	else if (bg.isValid())
		out.fill(bg);
	else
		out.fill(Qt::white);   // an opaque crop reaching outside the picture: white padding
	QPainter p(&out);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.drawImage(QPointF(-c.x(), -c.y()), whole);
	p.end();
	return out;
}

QString outputPathFor(const ScribusDoc* doc, const QString& sourcePath, const QString& tag)
{
	// a saved document: edited/ in its photos folder
	if (!SuneerPhotoStore::editedDir(doc, false).isEmpty() || !SuneerPhotoStore::folder(doc).isEmpty())
		return SuneerPhotoStore::editedPathFor(doc, sourcePath, tag);
	QString dir;
	const QString docName = doc ? doc->documentFileName() : QString();
	if (!docName.isEmpty() && QFileInfo(docName).dir().exists())
		dir = QFileInfo(docName).dir().absolutePath() + QStringLiteral("/") + QFileInfo(docName).completeBaseName() + QStringLiteral("_images");
	else
		dir = QFileInfo(sourcePath).absolutePath();
	QDir().mkpath(dir);
	const QString base = QFileInfo(sourcePath).completeBaseName();
	for (int n = 1; n < 10000; ++n)
	{
		const QString cand = dir + QStringLiteral("/") + base + tag + (n == 1 ? QString() : QString::number(n)) + QStringLiteral(".png");
		if (!QFileInfo::exists(cand))
			return cand;
	}
	return dir + QStringLiteral("/") + base + tag + QStringLiteral(".png");
}

bool apply(ScribusDoc* doc, PageItem* item, const QImage& result, const QImage& cut, const QImage& brush,
           const Settings& s, const QString& sourcePath, QString* error, const QImage& protect, bool manual)
{
	if (!doc || !item || result.isNull())
		return false;
	const QString outPath = outputPathFor(doc, sourcePath);
	QImage toSave = result.convertToFormat(QImage::Format_ARGB32);
	// dpi from the frame: the crop fills the frame width
	const double frameWin = qMax(1.0, item->width()) / 72.0;
	const double dpi = qBound(36.0, result.width() / frameWin, 2400.0);
	toSave.setDotsPerMeterX(qRound(dpi / 0.0254));
	toSave.setDotsPerMeterY(qRound(dpi / 0.0254));
	if (!toSave.save(outPath, "PNG"))
	{
		if (error)
			*error = QObject::tr("Could not write %1").arg(outPath);
		return false;
	}
	QString cutPath = attrOf(item, QString::fromLatin1(AttrCut));
	if (!cut.isNull())
	{
		if (cutPath.isEmpty() || !QFileInfo::exists(cutPath))
			cutPath = outputPathFor(doc, sourcePath, QStringLiteral("_cut"));
		cut.save(cutPath, "PNG");
	}
	QString brushPath = attrOf(item, QString::fromLatin1(AttrBrush));
	if (!brush.isNull())
	{
		if (brushPath.isEmpty() || !QFileInfo::exists(brushPath))
			brushPath = outputPathFor(doc, sourcePath, QStringLiteral("_mask"));
		brush.save(brushPath, "PNG");
	}
	QString protectPath = attrOf(item, QString::fromLatin1(AttrProtect));
	if (!protect.isNull())
	{
		if (protectPath.isEmpty() || !QFileInfo::exists(protectPath))
			protectPath = outputPathFor(doc, sourcePath, QStringLiteral("_protect"));
		protect.save(protectPath, "PNG");
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), QObject::tr("Edit photo"), QString(), Um::IGetImage);
	const QString inputPath = item->Pfile;
	const bool wasEmbedded = item->isImageInline();
	if (wasEmbedded)
		item->isTempFile = false;
	if (!doc->loadPict(outPath, item, false, true))
	{
		if (tx)
			tx.cancel();
		UndoManager::instance()->setUndoEnabled(false);
		doc->loadPict(inputPath, item, false, true);
		UndoManager::instance()->setUndoEnabled(true);
		if (error)
			*error = QObject::tr("Could not load the new picture: %1").arg(outPath);
		return false;
	}
	if (s.freeAspect && result.width() > 0)
	{
		// the frame takes the crop's shape, same width
		const double newH = item->width() * result.height() / double(result.width());
		doc->sizeItem(item->width(), newH, item, false, true, false);
	}
	item->setImageScalingMode(false, true);   // scale to frame, proportional: the crop IS the frame
	item->adjustPictScale();
	item->setImageXYOffset(0, 0);
	if (wasEmbedded)
	{
		item->isInlineImage = true;
		item->isTempFile = true;
	}
	else
		SuneerImageLinks::embedPlaced(doc, item);
	if (auto* frame = item->asImageFrame())
		frame->suneerSyncAutoContour(true);
	if (attrOf(item, QStringLiteral("SuneerImgOriginal")).isEmpty())
		setAttrOf(item, QStringLiteral("SuneerImgOriginal"), sourcePath);
	setAttrOf(item, QString::fromLatin1(AttrSettings), QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact)));
	setAttrOf(item, QString::fromLatin1(AttrManual), manual ? QStringLiteral("1") : QString());
	setAttrOf(item, QString::fromLatin1(AttrCut), cut.isNull() ? QString() : cutPath);
	setAttrOf(item, QString::fromLatin1(AttrBrush), brush.isNull() ? QString() : brushPath);
	setAttrOf(item, QString::fromLatin1(AttrProtect), protect.isNull() ? QString() : protectPath);
	setAttrOf(item, QString::fromLatin1(AttrBgMode), s.removeBg ? s.bgMode : QStringLiteral("none"));
	setAttrOf(item, QString::fromLatin1(AttrOutput), outPath);
	{
		// sidecar: the same state next to the edited file (survives a lost attribute, other PCs)
		QJsonObject side{{"version", 1}, {"settings", s.toJson()}, {"source", QFileInfo(sourcePath).fileName()},
		                 {"cut", cut.isNull() ? QString() : QFileInfo(cutPath).fileName()}, {"mask", brush.isNull() ? QString() : QFileInfo(brushPath).fileName()},
		                 {"protect", protect.isNull() ? QString() : QFileInfo(protectPath).fileName()}, {"manual", manual},
		                 {"output", QFileInfo(outPath).fileName()}};
		QFile sf(sidecarPathFor(outPath));
		if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
			sf.write(QJsonDocument(side).toJson(QJsonDocument::Indented));
	}
	if (!attrOf(item, QStringLiteral("obit.photoStatus")).isEmpty())
		setAttrOf(item, QStringLiteral("obit.photoStatus"), QStringLiteral("ok"));
	setAttrOf(item, QStringLiteral("obit.checkInfo"), QString());
	setAttrOf(item, QStringLiteral("obit.out"), attrOf(item, QStringLiteral("obit.out")).isEmpty() ? QString() : outPath);
	if (!s.bgName.isEmpty() && doc->PageColors.contains(s.bgName))
		item->setFillColor(s.bgName);
	else if (s.removeBg && s.bgColor.isEmpty())
		item->setFillColor(CommonStrings::None);
	item->updateClip();
	item->update();
	if (tx)
		tx.commit();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	return true;
}

// ---------------------------------------------------------------------------
// mask editing
// ---------------------------------------------------------------------------

void stampSoft(QImage& mask, const QPointF& centre, double radius, double hardness, int target, double opacity)
{
	if (mask.isNull() || radius <= 0)
		return;
	const int x0 = qMax(0, int(std::floor(centre.x() - radius - 1))), x1 = qMin(mask.width() - 1, int(std::ceil(centre.x() + radius + 1)));
	const int y0 = qMax(0, int(std::floor(centre.y() - radius - 1))), y1 = qMin(mask.height() - 1, int(std::ceil(centre.y() + radius + 1)));
	const double hard = qBound(0.0, hardness, 1.0);
	const double inner = radius * hard;
	for (int y = y0; y <= y1; ++y)
	{
		uchar* line = mask.scanLine(y);
		for (int x = x0; x <= x1; ++x)
		{
			const double d = std::hypot(x + 0.5 - centre.x(), y + 0.5 - centre.y());
			double a;
			if (d <= inner)
				a = 1.0;
			else if (d >= radius + 0.5)
				continue;
			else
			{
				// smooth falloff from the hard core to the rim (+0.5 px anti-alias)
				const double t = (d - inner) / qMax(0.001, radius + 0.5 - inner);
				a = 1.0 - t * t * (3 - 2 * t);
			}
			a *= opacity;
			const double v = line[x] * (1.0 - a) + target * a;
			line[x] = uchar(qBound(0.0, v + 0.5, 255.0));
		}
	}
}

void stampSegment(QImage& mask, const QPointF& a, const QPointF& b, double radius, double hardness, int target, double spacing)
{
	const double len = QLineF(a, b).length();
	const double step = qMax(0.5, spacing);
	if (len < step)
	{
		stampSoft(mask, b, radius, hardness, target);
		return;
	}
	const int n = int(len / step);
	for (int i = 1; i <= n; ++i)
	{
		const double t = double(i) / n;
		stampSoft(mask, a + (b - a) * t, radius, hardness, target);
	}
}

namespace {
void boxBlur8(QImage& img, int r)
{
	if (r <= 0 || img.isNull())
		return;
	const int W = img.width(), H = img.height();
	std::vector<int> tmp(size_t(W) * H);
	// horizontal
	for (int y = 0; y < H; ++y)
	{
		const uchar* line = img.constScanLine(y);
		int sum = 0;
		for (int x = -r; x <= r; ++x)
			sum += line[qBound(0, x, W - 1)];
		for (int x = 0; x < W; ++x)
		{
			tmp[size_t(y) * W + x] = sum;
			sum += line[qBound(0, x + r + 1, W - 1)] - line[qBound(0, x - r, W - 1)];
		}
	}
	const int n = 2 * r + 1;
	// vertical
	std::vector<int> col(H);
	for (int x = 0; x < W; ++x)
	{
		int sum = 0;
		for (int y = -r; y <= r; ++y)
			sum += tmp[size_t(qBound(0, y, H - 1)) * W + x];
		for (int y = 0; y < H; ++y)
		{
			col[y] = sum;
			sum += tmp[size_t(qBound(0, y + r + 1, H - 1)) * W + x] - tmp[size_t(qBound(0, y - r, H - 1)) * W + x];
		}
		for (int y = 0; y < H; ++y)
			img.scanLine(y)[x] = uchar(qBound(0, (col[y] + n * n / 2) / (n * n), 255));
	}
}
} // namespace

QImage pathCoverage(const QPainterPath& path, const QSize& size, double featherPx)
{
	QImage cov(size, QImage::Format_Grayscale8);
	cov.fill(0);
	QPainter p(&cov);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawPath(path);
	p.end();
	const int r = int(featherPx + 0.5);
	if (r > 0)
	{
		boxBlur8(cov, qMax(1, r / 2));
		boxBlur8(cov, qMax(1, r / 2));
	}
	return cov;
}

void applyPathToMask(QImage& mask, const QPainterPath& path, double featherPx, int target, bool outside)
{
	if (mask.isNull() || path.isEmpty())
		return;
	const QImage cov = pathCoverage(path, mask.size(), featherPx);
	for (int y = 0; y < mask.height(); ++y)
	{
		uchar* line = mask.scanLine(y);
		const uchar* cl = cov.constScanLine(y);
		for (int x = 0; x < mask.width(); ++x)
		{
			const double a = (outside ? 255 - cl[x] : cl[x]) / 255.0;
			if (a <= 0.0)
				continue;
			line[x] = uchar(qBound(0.0, line[x] * (1.0 - a) + target * a + 0.5, 255.0));
		}
	}
}

QImage refineEdge(const QImage& cutIn, int strength)
{
	if (cutIn.isNull() || strength <= 0)
		return cutIn;
	QImage cut = cutIn.convertToFormat(QImage::Format_ARGB32);
	const int W = cut.width(), H = cut.height();
	// 1. the matte: smoothed by a radius that grows with the strength, then
	//    its contrast raised around the middle so the edge stays crisp and
	//    the faint fringe (halo) goes
	QImage alpha(QSize(W, H), QImage::Format_Grayscale8);
	for (int y = 0; y < H; ++y)
	{
		const QRgb* l = reinterpret_cast<const QRgb*>(cut.constScanLine(y));
		uchar* a = alpha.scanLine(y);
		for (int x = 0; x < W; ++x)
			a[x] = uchar(qAlpha(l[x]));
	}
	const int r = 1 + strength / 25;
	QImage smooth = alpha;
	boxBlur8(smooth, r);
	const double k = 1.0 + strength / 40.0;
	// 2. colour decontamination: the colour of a half-transparent pixel is
	//    replaced by the alpha-weighted average of its neighbourhood (the
	//    opaque inside wins), so the old background's tint does not bleed
	//    into the new one
	const int rc = 2 + strength / 20;
	QImage out(cut.size(), QImage::Format_ARGB32);
	for (int y = 0; y < H; ++y)
	{
		QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
		const uchar* sa = smooth.constScanLine(y);
		for (int x = 0; x < W; ++x)
		{
			double a = (sa[x] / 255.0 - 0.5) * k + 0.5;
			a = qBound(0.0, a, 1.0);
			const QRgb src = reinterpret_cast<const QRgb*>(cut.constScanLine(y))[x];
			int rr = qRed(src), gg = qGreen(src), bb = qBlue(src);
			if (a > 0.02 && a < 0.98)
			{
				double sr = 0, sg = 0, sb = 0, sw = 0;
				for (int dy = -rc; dy <= rc; ++dy)
				{
					const int yy = y + dy;
					if (yy < 0 || yy >= H)
						continue;
					const QRgb* cl = reinterpret_cast<const QRgb*>(cut.constScanLine(yy));
					for (int dx = -rc; dx <= rc; ++dx)
					{
						const int xx = x + dx;
						if (xx < 0 || xx >= W)
							continue;
						const double w = qAlpha(cl[xx]) / 255.0;
						if (w < 0.6)
							continue;
						sr += qRed(cl[xx]) * w; sg += qGreen(cl[xx]) * w; sb += qBlue(cl[xx]) * w; sw += w;
					}
				}
				if (sw > 0)
				{
					rr = int(sr / sw + 0.5); gg = int(sg / sw + 0.5); bb = int(sb / sw + 0.5);
				}
			}
			o[x] = qRgba(rr, gg, bb, int(a * 255 + 0.5));
		}
	}
	return out;
}

QImage alphaMap(const QSize& size, const QImage& cut, const QImage& brush, const QImage& protect, bool manualOpaque)
{
	QImage out(size, QImage::Format_Grayscale8);
	const bool haveCut = !manualOpaque && !cut.isNull() && cut.size() == size;
	const bool haveBrush = !brush.isNull() && brush.size() == size;
	const bool haveProtect = !protect.isNull() && protect.size() == size;
	for (int y = 0; y < size.height(); ++y)
	{
		uchar* ol = out.scanLine(y);
		const QRgb* cl = haveCut ? reinterpret_cast<const QRgb*>(cut.constScanLine(y)) : nullptr;
		const uchar* bl = haveBrush ? brush.constScanLine(y) : nullptr;
		const uchar* pl = haveProtect ? protect.constScanLine(y) : nullptr;
		for (int x = 0; x < size.width(); ++x)
		{
			int a = cl ? qAlpha(cl[x]) : 255;
			if (bl)
			{
				const int m = bl[x];
				if (m > 128)
					a = a + (255 - a) * (m - 128) / 127;
				else if (m < 128)
					a = a * m / 128;
			}
			if (pl)
			{
				const int p = pl[x];
				if (p > 128)
					a = a + (255 - a) * (p - 128) / 127;
				else if (p < 128)
					a = a * p / 128;
			}
			ol[x] = uchar(qBound(0, a, 255));
		}
	}
	return out;
}

void applyProtectToAlpha(QImage& img, const QImage& protect)
{
	if (img.isNull() || protect.isNull() || protect.size() != img.size())
		return;
	for (int y = 0; y < img.height(); ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		const uchar* pl = protect.constScanLine(y);
		for (int x = 0; x < img.width(); ++x)
		{
			const int p = pl[x];
			if (p == 128)
				continue;
			int a = qAlpha(line[x]);
			if (p > 128)
				a = a + (255 - a) * (p - 128) / 127;
			else
				a = a * p / 128;
			line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qBound(0, a, 255));
		}
	}
}

QImage smartRestoreWeights(const QImage& source0, const QImage& cut, const QImage& coverage)
{
	QImage w(source0.size(), QImage::Format_Grayscale8);
	w.fill(0);
	if (source0.isNull() || cut.isNull() || coverage.isNull() || cut.size() != source0.size() || coverage.size() != source0.size())
		return w;
	const QImage source = source0.convertToFormat(QImage::Format_ARGB32);
	// colour samples inside the selection: kept (alpha > 200) = subject, removed (alpha < 40) = background
	struct C { double r, g, b; };
	std::vector<C> fg, bg;
	for (int y = 0; y < source.height(); y += 2)
	{
		const QRgb* sl = reinterpret_cast<const QRgb*>(source.constScanLine(y));
		const QRgb* cl = reinterpret_cast<const QRgb*>(cut.constScanLine(y));
		const uchar* cv = coverage.constScanLine(y);
		for (int x = 0; x < source.width(); x += 2)
		{
			if (cv[x] < 128)
				continue;
			const int a = qAlpha(cl[x]);
			if (a > 200)
				fg.push_back({double(qRed(sl[x])), double(qGreen(sl[x])), double(qBlue(sl[x]))});
			else if (a < 40)
				bg.push_back({double(qRed(sl[x])), double(qGreen(sl[x])), double(qBlue(sl[x]))});
		}
	}
	// a few colour centres per set (k-means, k = 4, 6 rounds)
	auto centres = [](std::vector<C>& pts) {
		std::vector<C> cs;
		if (pts.empty())
			return cs;
		const int k = qMin(4, int(pts.size()));
		for (int i = 0; i < k; ++i)
			cs.push_back(pts[size_t(i) * pts.size() / k]);
		std::vector<int> assign(pts.size(), 0);
		for (int round = 0; round < 6; ++round)
		{
			for (size_t i = 0; i < pts.size(); ++i)
			{
				double best = 1e18;
				for (int j = 0; j < k; ++j)
				{
					const double d = (pts[i].r - cs[j].r) * (pts[i].r - cs[j].r) + (pts[i].g - cs[j].g) * (pts[i].g - cs[j].g) + (pts[i].b - cs[j].b) * (pts[i].b - cs[j].b);
					if (d < best) { best = d; assign[i] = j; }
				}
			}
			std::vector<C> sum(k, {0, 0, 0});
			std::vector<int> n(k, 0);
			for (size_t i = 0; i < pts.size(); ++i)
			{
				sum[assign[i]].r += pts[i].r; sum[assign[i]].g += pts[i].g; sum[assign[i]].b += pts[i].b; ++n[assign[i]];
			}
			for (int j = 0; j < k; ++j)
				if (n[j] > 0)
					cs[j] = {sum[j].r / n[j], sum[j].g / n[j], sum[j].b / n[j]};
		}
		return cs;
	};
	const std::vector<C> fgc = centres(fg), bgc = centres(bg);
	if (fgc.empty())
		return w;   // nothing kept inside the selection: no colour model, plain restore instead
	auto dist = [](const C& a, const QRgb p) { const double dr = a.r - qRed(p), dg = a.g - qGreen(p), db = a.b - qBlue(p); return dr * dr + dg * dg + db * db; };
	for (int y = 0; y < source.height(); ++y)
	{
		const QRgb* sl = reinterpret_cast<const QRgb*>(source.constScanLine(y));
		const uchar* cv = coverage.constScanLine(y);
		uchar* wl = w.scanLine(y);
		for (int x = 0; x < source.width(); ++x)
		{
			if (cv[x] == 0)
				continue;
			double dFg = 1e18, dBg = 1e18;
			for (const C& c : fgc) dFg = qMin(dFg, dist(c, sl[x]));
			for (const C& c : bgc) dBg = qMin(dBg, dist(c, sl[x]));
			double wt;
			if (bgc.empty())
				wt = dFg < 40.0 * 40.0 ? 1.0 : qMax(0.0, 1.0 - (std::sqrt(dFg) - 40.0) / 60.0);
			else
				wt = dBg / qMax(1e-6, dFg + dBg);   // 1 = like the subject, 0 = like the background
			wt = qBound(0.0, (wt - 0.5) * 2.5 + 0.5, 1.0);   // decisive around the middle
			wl[x] = uchar(qRound(wt * cv[x]));
		}
	}
	return w;
}

void applyMaskToAlpha(QImage& img, const QImage& mask, const QImage& cutAlphaRef)
{
	if (img.isNull() || mask.isNull() || mask.size() != img.size())
		return;
	const bool haveRef = !cutAlphaRef.isNull() && cutAlphaRef.size() == img.size();
	for (int y = 0; y < img.height(); ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		const uchar* ml = mask.constScanLine(y);
		const uchar* rl = haveRef ? cutAlphaRef.constScanLine(y) : nullptr;
		for (int x = 0; x < img.width(); ++x)
		{
			int a = rl ? rl[x] : qAlpha(line[x]);
			const int m = ml[x];
			if (m > 128)
				a = a + (255 - a) * (m - 128) / 127;
			else if (m < 128)
				a = a * m / 128;
			line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qBound(0, a, 255));
		}
	}
}

bool resetToOriginal(ScribusDoc* doc, PageItem* item, QString* error)
{
	if (!doc || !item)
		return false;
	const QString src = sourcePathOf(item);
	if (src.isEmpty() || !QFileInfo::exists(src))
	{
		if (error)
			*error = QObject::tr("The original picture is not available");
		return false;
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), QObject::tr("Reset photo to original"), QString(), Um::IGetImage);
	if (!doc->loadPict(src, item, false, true))
	{
		if (tx)
			tx.cancel();
		if (error)
			*error = QObject::tr("Could not load %1").arg(src);
		return false;
	}
	item->setImageScalingMode(false, true);
	item->adjustPictScale();
	item->setImageXYOffset(0, 0);
	SuneerImageLinks::embedPlaced(doc, item);
	if (auto* frame = item->asImageFrame())
		frame->suneerSyncAutoContour(true);
	setAttrOf(item, QString::fromLatin1(AttrSettings), QString());
	setAttrOf(item, QString::fromLatin1(AttrManual), QString());
	setAttrOf(item, QString::fromLatin1(AttrOutput), QString());
	item->update();
	if (tx)
		tx.commit();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	return true;
}
} // namespace SuneerPhotoEdit
