/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerphotohash.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

#include "pageitem.h"
#include "scribusdoc.h"
#include "suneerphotostore.h"

namespace
{
	QMutex& cacheMutex()
	{
		static QMutex m;
		return m;
	}
	// "path|size|mtime" -> hash
	QHash<QString, SuneerPhotoHash::Hash>& memCache()
	{
		static QHash<QString, SuneerPhotoHash::Hash> c;
		return c;
	}

	QString keyOf(const QFileInfo& fi)
	{
		return QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
	}

	// 32x32 grey -> 2D DCT-II -> the 8x8 lowest frequencies (DC left out of the median) -> 64 bits
	quint64 phash(const QImage& src)
	{
		const QImage g = src.convertToFormat(QImage::Format_Grayscale8).scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		double px[32][32];
		for (int y = 0; y < 32; ++y)
		{
			const uchar* row = g.constScanLine(y);
			for (int x = 0; x < 32; ++x)
				px[y][x] = row[x];
		}
		static double cosTab[8][32];
		static bool init = false;
		if (!init)
		{
			for (int u = 0; u < 8; ++u)
				for (int x = 0; x < 32; ++x)
					cosTab[u][x] = std::cos((2 * x + 1) * u * M_PI / 64.0);
			init = true;   // benign race: every thread writes the same values
		}
		double tmp[8][32];
		for (int u = 0; u < 8; ++u)
			for (int y = 0; y < 32; ++y)
			{
				double s = 0;
				for (int x = 0; x < 32; ++x)
					s += px[y][x] * cosTab[u][x];
				tmp[u][y] = s;
			}
		double dct[64];
		for (int v = 0; v < 8; ++v)
			for (int u = 0; u < 8; ++u)
			{
				double s = 0;
				for (int y = 0; y < 32; ++y)
					s += tmp[u][y] * cosTab[v][y];
				dct[v * 8 + u] = s;
			}
		double sorted[63];
		std::copy(dct + 1, dct + 64, sorted);
		std::nth_element(sorted, sorted + 31, sorted + 63);
		const double median = sorted[31];
		quint64 h = 0;
		for (int i = 0; i < 64; ++i)
			if (dct[i] > median)
				h |= (quint64(1) << i);
		return h;
	}
}

namespace SuneerPhotoHash
{

Hash ofImage(const QImage& img)
{
	Hash h;
	if (img.isNull() || img.width() < 8 || img.height() < 8)
		return h;
	// a transparent cut-out (background removed) is judged on white, like the page shows it
	QImage flat = img;
	if (img.hasAlphaChannel())
	{
		flat = QImage(img.size(), QImage::Format_RGB32);
		flat.fill(Qt::white);
		QImage a = img.convertToFormat(QImage::Format_ARGB32);
		for (int y = 0; y < a.height(); ++y)
		{
			const QRgb* s = reinterpret_cast<const QRgb*>(a.constScanLine(y));
			QRgb* d = reinterpret_cast<QRgb*>(flat.scanLine(y));
			for (int x = 0; x < a.width(); ++x)
			{
				const int al = qAlpha(s[x]);
				d[x] = qRgb((qRed(s[x]) * al + 255 * (255 - al)) / 255, (qGreen(s[x]) * al + 255 * (255 - al)) / 255, (qBlue(s[x]) * al + 255 * (255 - al)) / 255);
			}
		}
	}
	h.h[0] = phash(flat);
	const double frac[3] = { 0.90, 0.85, 0.80 };
	for (int k = 0; k < 3; ++k)
	{
		const int cw = int(flat.width() * frac[k]), ch = int(flat.height() * frac[k]);
		h.h[k + 1] = phash(flat.copy((flat.width() - cw) / 2, (flat.height() - ch) / 2, cw, ch));
	}
	h.ok = true;
	return h;
}

Hash ofFile(const QString& path)
{
	const QFileInfo fi(path);
	if (path.isEmpty() || !fi.isFile())
		return Hash();
	const QString key = keyOf(fi);
	{
		QMutexLocker lock(&cacheMutex());
		const auto it = memCache().constFind(key);
		if (it != memCache().constEnd())
			return *it;
	}
	QImageReader reader(path);
	reader.setAutoTransform(true);
	// no need for every pixel: 32x32 is what is hashed
	const QSize sz = reader.size();
	if (sz.isValid() && sz.width() > 512)
		reader.setScaledSize(sz.scaled(512, 512, Qt::KeepAspectRatio));
	const Hash h = ofImage(reader.read());
	if (h.ok)
	{
		QMutexLocker lock(&cacheMutex());
		memCache().insert(key, h);
	}
	return h;
}

int distance(const Hash& a, const Hash& b)
{
	if (!a.ok || !b.ok)
		return 64;
	// one of the two may be a crop of the other: its whole picture against the other's centres
	int best = 64;
	for (int k = 0; k < 4; ++k)
		best = qMin(best, qMin(int(qPopulationCount(a.h[0] ^ b.h[k])), int(qPopulationCount(a.h[k] ^ b.h[0]))));
	return best;
}

int maxDistance()
{
	QSettings s(QStringLiteral("Faircode"), QStringLiteral("ScribusNews"));
	return qBound(0, s.value(QStringLiteral("dup/photoBits"), 6).toInt(), 30);
}

int percent(int d)
{
	return qBound(0, 100 - d * 100 / 32, 100);
}

QString sourceFileOf(const PageItem* item)
{
	if (!item || !item->isImageFrame())
		return QString();
	auto attr = [item](const char* name) {
		const QList<ObjectAttribute> l = item->getObjectAttributes(QString::fromLatin1(name));
		return l.isEmpty() ? QString() : l.first().value;
	};
	// edited / processed: the original (a background-removed version is the same photo)
	for (const char* name : { "obit.src", "news.original", "SuneerImgOriginal", "SuneerBgSource" })
	{
		const QString p = attr(name);
		if (!p.isEmpty() && QFileInfo(p).isFile())
			return p;
	}
	if (QFileInfo(item->Pfile).isFile())
		return item->Pfile;
	const QString from = attr("suneer.embeddedFrom");
	return QFileInfo(from).isFile() ? from : QString();
}

QString cacheFileFor(const ScribusDoc* doc)
{
	const QString folder = SuneerPhotoStore::folder(doc);
	if (!folder.isEmpty() && QFileInfo(folder).isDir())
		return folder + QStringLiteral("/hashes.json");
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news/hashes.json");
}

void loadCache(const QString& jsonPath)
{
	QFile f(jsonPath);
	if (!f.open(QIODevice::ReadOnly))
		return;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	QMutexLocker lock(&cacheMutex());
	for (auto it = o.constBegin(); it != o.constEnd(); ++it)
	{
		const QJsonObject e = it.value().toObject();
		const QStringList parts = e[QStringLiteral("h")].toString().split(QLatin1Char(' '));
		if (parts.size() != 4)
			continue;
		Hash h;
		for (int k = 0; k < 4; ++k)
			h.h[k] = parts[k].toULongLong(nullptr, 16);
		h.ok = true;
		memCache().insert(it.key(), h);
	}
}

void saveCache(const QString& jsonPath)
{
	if (jsonPath.isEmpty())
		return;
	QJsonObject o;
	const QString dir = QFileInfo(jsonPath).absolutePath();
	{
		QMutexLocker lock(&cacheMutex());
		for (auto it = memCache().constBegin(); it != memCache().constEnd(); ++it)
		{
			// only this folder's files go into its hashes.json (the user cache takes the rest)
			if (!jsonPath.contains(QLatin1String("/news/hashes.json")) && !it.key().startsWith(QFileInfo(dir).absolutePath()))
				continue;
			QJsonObject e;
			e[QStringLiteral("h")] = QStringLiteral("%1 %2 %3 %4").arg(it->h[0], 16, 16, QLatin1Char('0')).arg(it->h[1], 16, 16, QLatin1Char('0'))
			                             .arg(it->h[2], 16, 16, QLatin1Char('0')).arg(it->h[3], 16, 16, QLatin1Char('0'));
			o.insert(it.key(), e);
		}
	}
	QDir().mkpath(dir);
	QSaveFile f(jsonPath);
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

}
