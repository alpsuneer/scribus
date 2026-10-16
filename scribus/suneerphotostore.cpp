/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerphotostore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include "pageitem.h"
#include "scribusdoc.h"
#include "util_file.h"

namespace SuneerPhotoStore
{
const char* const AttrFileUrl    = "news.fileUrl";
const char* const AttrOriginal   = "news.original";
const char* const AttrMediaIndex = "news.index";

namespace {
QString attrOf(const PageItem* item, const QString& name)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> l = item->getObjectAttributes(name);
	return l.isEmpty() ? QString() : l.first().value;
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

QList<PageItem*> allItems(const ScribusDoc* doc)
{
	QList<PageItem*> out;
	if (!doc)
		return out;
	for (int pass = 0; pass < 2; ++pass)
	{
		const QList<PageItem*>& list = pass == 0 ? doc->DocItems : doc->MasterItems;
		for (PageItem* it : list)
		{
			if (!it)
				continue;
			if (it->isGroup())
				out << it->getAllChildren();
			out << it;
		}
	}
	return out;
}

bool copyAtomic(const QString& from, const QString& to)
{
	QDir().mkpath(QFileInfo(to).absolutePath());
	const QString tmp = to + QStringLiteral(".part");
	QFile::remove(tmp);
	if (!QFile::copy(from, tmp))
		return false;
	QFile::remove(to);
	if (!QFile::rename(tmp, to))
	{
		QFile::remove(tmp);
		return false;
	}
	return true;
}

QString uniquePath(const QString& dir, const QString& stem, const QString& ext)
{
	for (int n = 1; n < 10000; ++n)
	{
		const QString cand = dir + QStringLiteral("/") + stem + (n == 1 ? QString() : QString::number(n)) + QStringLiteral(".") + ext;
		if (!QFileInfo::exists(cand))
			return cand;
	}
	return dir + QStringLiteral("/") + stem + QStringLiteral(".") + ext;
}
} // namespace

QString folderForSla(const QString& slaPath)
{
	if (slaPath.isEmpty())
		return QString();
	const QFileInfo fi(slaPath);
	QString base = fi.completeBaseName();
	if (base.endsWith(QLatin1String(".sla"), Qt::CaseInsensitive))
		base.chop(4);
	return fi.absolutePath() + QStringLiteral("/") + base + QStringLiteral("_photos");
}

QString folder(const ScribusDoc* doc)
{
	if (!doc || !doc->hasName || doc->documentFileName().isEmpty())
		return QString();
	return folderForSla(doc->documentFileName());
}

QString originalDir(const ScribusDoc* doc, bool create)
{
	const QString f = folder(doc);
	if (f.isEmpty())
		return QString();
	const QString d = f + QStringLiteral("/original");
	if (create)
		QDir().mkpath(d);
	return d;
}

QString editedDir(const ScribusDoc* doc, bool create)
{
	const QString f = folder(doc);
	if (f.isEmpty())
		return QString();
	const QString d = f + QStringLiteral("/edited");
	if (create)
		QDir().mkpath(d);
	return d;
}

QString cacheDir()
{
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news");
}

bool isNewsPhoto(const PageItem* item)
{
	return item && item->isImageFrame() && !attrOf(item, QStringLiteral("news.story")).isEmpty();
}

QString storyKey(const PageItem* item)
{
	QString k = attrOf(item, QStringLiteral("news.refId"));
	if (k.isEmpty())
		k = attrOf(item, QStringLiteral("news.serverId"));
	if (k.isEmpty())
		k = attrOf(item, QStringLiteral("news.story"));
	return k;
}

QString safeName(const QString& key, int mediaIndex, const QString& ext)
{
	static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]"));
	QString k = key;
	k.replace(unsafe, QStringLiteral("_"));
	if (k.isEmpty() || k == QLatin1String(".") || k == QLatin1String(".."))
		k = QStringLiteral("photo");
	QString e = ext.toLower();
	e.replace(unsafe, QString());
	if (e.isEmpty())
		e = QStringLiteral("jpg");
	return QStringLiteral("%1_%2.%3").arg(k.left(80)).arg(qMax(0, mediaIndex)).arg(e);
}

QString editedPathFor(const ScribusDoc* doc, const QString& sourcePath, const QString& tag, const QString& ext)
{
	const QFileInfo src(sourcePath);
	QString stem = src.completeBaseName();
	while (stem.startsWith(QLatin1Char('.')))
		stem.remove(0, 1);
	if (stem.isEmpty())
		stem = QStringLiteral("image");
	const QString dir = editedDir(doc, true);
	if (dir.isEmpty())
		return uniquePath(src.absolutePath(), stem + tag, ext);
	return uniquePath(dir, stem + tag, ext);
}

QString redirectDerived(const ScribusDoc* doc, const QString& candidate)
{
	const QString dir = editedDir(doc, false);
	if (dir.isEmpty() || candidate.isEmpty())
		return candidate;
	const QFileInfo fi(candidate);
	if (fi.absolutePath() == QFileInfo(dir).absoluteFilePath())
		return candidate;
	QDir().mkpath(dir);
	return uniquePath(dir, fi.completeBaseName(), fi.suffix().isEmpty() ? QStringLiteral("png") : fi.suffix());
}

bool isInStore(const ScribusDoc* doc, const QString& path)
{
	const QString f = folder(doc);
	if (f.isEmpty() || path.isEmpty())
		return false;
	const QString abs = QFileInfo(path).absoluteFilePath();
	return abs.startsWith(QFileInfo(f).absoluteFilePath() + QStringLiteral("/"));
}

QString importOriginal(const ScribusDoc* doc, const QString& cachePath, const QString& key, int mediaIndex, QString* why)
{
	const QString dir = originalDir(doc, true);
	if (dir.isEmpty())
		return cachePath;
	if (cachePath.isEmpty() || !QFileInfo::exists(cachePath))
	{
		if (why)
			*why = QStringLiteral("source missing: ") + cachePath;
		return cachePath;
	}
	const QString target = dir + QStringLiteral("/") + safeName(key, mediaIndex, QFileInfo(cachePath).suffix());
	if (QFileInfo(target).absoluteFilePath() == QFileInfo(cachePath).absoluteFilePath())
		return target;
	if (QFileInfo::exists(target) && QFileInfo(target).size() == QFileInfo(cachePath).size())
		return target;   // same download already there
	if (!copyAtomic(cachePath, target))
	{
		if (why)
			*why = QStringLiteral("could not write ") + target;
		return cachePath;
	}
	return target;
}

QStringList pathAttributes()
{
	return { QStringLiteral("news.original"), QStringLiteral("obit.src"), QStringLiteral("obit.out"),
	         QStringLiteral("SuneerImgOriginal"), QStringLiteral("suneer.photoEditCut"), QStringLiteral("suneer.photoEditBrush"),
	         QStringLiteral("suneer.photoEditOutput"), QStringLiteral("SuneerBgSource"), QStringLiteral("SuneerBgCut") };
}

void rewritePaths(PageItem* item, const QString& from, const QString& to)
{
	if (!item || from.isEmpty())
		return;
	const QString fromAbs = QFileInfo(from).absoluteFilePath();
	auto map = [&](const QString& p) -> QString {
		if (p.isEmpty())
			return p;
		const QString abs = QFileInfo(p).absoluteFilePath();
		if (abs == fromAbs)
			return to;
		if (abs.startsWith(fromAbs + QStringLiteral("/")))
			return to + abs.mid(fromAbs.length());
		return p;
	};
	if (!item->Pfile.isEmpty())
		item->Pfile = map(item->Pfile);
	for (const QString& a : pathAttributes())
	{
		const QString v = attrOf(item, a);
		if (!v.isEmpty())
		{
			const QString nv = map(v);
			if (nv != v)
				setAttrOf(item, a, nv);
		}
	}
}

QString prepareForSave(ScribusDoc* doc, const QString& slaPath)
{
	if (!doc || slaPath.isEmpty())
		return QString();
	QStringList report;
	const QString newFolder = folderForSla(slaPath);
	const QString oldFolder = (doc->hasName && !doc->documentFileName().isEmpty()) ? folderForSla(doc->documentFileName()) : QString();
	const QList<PageItem*> items = allItems(doc);

	// 1. Save As elsewhere: the folder goes along, every path follows.
	if (!oldFolder.isEmpty() && QFileInfo(oldFolder).absoluteFilePath() != QFileInfo(newFolder).absoluteFilePath() && QDir(oldFolder).exists())
	{
		int copied = 0;
		QDirIterator it(oldFolder, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString src = it.next();
			const QString rel = QDir(oldFolder).relativeFilePath(src);
			if (copyAtomic(src, newFolder + QStringLiteral("/") + rel))
				++copied;
		}
		for (PageItem* item : items)
			rewritePaths(item, oldFolder, newFolder);
		report << QStringLiteral("photos folder copied to %1 (%2 files)").arg(newFolder).arg(copied);
	}

	// 2. the older "<base>_images/" output merges into edited/
	const QFileInfo sfi(slaPath);
	QString base = sfi.completeBaseName();
	if (base.endsWith(QLatin1String(".sla"), Qt::CaseInsensitive))
		base.chop(4);
	const QStringList oldDirs { sfi.absolutePath() + QStringLiteral("/") + base + QStringLiteral("_images"),
	                            oldFolder.isEmpty() ? QString() : QFileInfo(doc->documentFileName()).absolutePath() + QStringLiteral("/") + QFileInfo(doc->documentFileName()).completeBaseName() + QStringLiteral("_images") };
	for (const QString& oldImages : oldDirs)
	{
		if (oldImages.isEmpty() || !QDir(oldImages).exists())
			continue;
		const QString edited = newFolder + QStringLiteral("/edited");
		QDir().mkpath(edited);
		int moved = 0;
		QDirIterator it(oldImages, QDir::Files, QDirIterator::Subdirectories);
		QList<QPair<QString, QString>> renames;
		while (it.hasNext())
		{
			const QString src = it.next();
			const QString rel = QDir(oldImages).relativeFilePath(src);
			const QString dst = edited + QStringLiteral("/") + rel;
			QDir().mkpath(QFileInfo(dst).absolutePath());
			if (QFile::exists(dst))
				QFile::remove(dst);
			if (QFile::rename(src, dst) || copyAtomic(src, dst))
			{
				renames << qMakePair(src, dst);
				++moved;
			}
		}
		for (PageItem* item : items)
			for (const auto& rn : renames)
				rewritePaths(item, rn.first, rn.second);
		// empty folders go away
		QDir d(oldImages);
		d.removeRecursively();
		if (moved)
			report << QStringLiteral("%1 files moved from %2 into edited/").arg(moved).arg(oldImages);
	}

	// 3. News photos still in the cache: into original/ (and their working copies into edited/)
	const QString cache = QFileInfo(cacheDir()).absoluteFilePath();
	const QString original = newFolder + QStringLiteral("/original");
	const QString edited = newFolder + QStringLiteral("/edited");
	int adopted = 0;
	QHash<QString, QString> done;   // cache path -> store path
	for (PageItem* item : items)
	{
		if (!isNewsPhoto(item))
			continue;
		const int idx = attrOf(item, QString::fromLatin1(AttrMediaIndex)).toInt();
		const QString key = storyKey(item);
		QStringList paths { item->Pfile };
		for (const QString& a : pathAttributes())
			if (!attrOf(item, a).isEmpty())
				paths << attrOf(item, a);
		for (const QString& p : paths)
		{
			if (p.isEmpty())
				continue;
			const QString abs = QFileInfo(p).absoluteFilePath();
			if (!abs.startsWith(cache + QStringLiteral("/")) || !QFileInfo::exists(abs))
				continue;
			if (done.contains(abs))
				continue;
			QString target;
			const bool isWorkingCopy = QFileInfo(abs).fileName().contains(QLatin1String(".w")) && abs.endsWith(QLatin1String(".jpg")) && QFileInfo(abs).completeBaseName().contains(QLatin1String(".w"));
			const bool derived = isWorkingCopy || abs.contains(QLatin1String("/obit/"));
			if (derived)
			{
				QDir().mkpath(edited);
				target = edited + QStringLiteral("/") + QFileInfo(abs).fileName();
			}
			else
			{
				QDir().mkpath(original);
				target = original + QStringLiteral("/") + safeName(key, idx, QFileInfo(abs).suffix());
				if (QFileInfo::exists(target) && QFileInfo(target).size() != QFileInfo(abs).size())
					target = uniquePath(original, QFileInfo(safeName(key, idx, QFileInfo(abs).suffix())).completeBaseName(), QFileInfo(abs).suffix());
			}
			if (QFileInfo::exists(target) || copyAtomic(abs, target))
			{
				done[abs] = target;
				++adopted;
			}
		}
		for (auto it = done.constBegin(); it != done.constEnd(); ++it)
			rewritePaths(item, it.key(), it.value());
		if (attrOf(item, QString::fromLatin1(AttrOriginal)).isEmpty())
		{
			// the file in original/ this frame came from, when we know it
			for (auto it = done.constBegin(); it != done.constEnd(); ++it)
				if (it.value().startsWith(original + QStringLiteral("/")) && QFileInfo(it.value()).fileName().startsWith(QFileInfo(safeName(key, idx, QStringLiteral("x"))).completeBaseName()))
					setAttrOf(item, QString::fromLatin1(AttrOriginal), it.value());
		}
	}
	if (adopted)
		report << QStringLiteral("%1 photo files adopted from the cache into %2").arg(adopted).arg(newFolder);
	return report.join(QStringLiteral("; "));
}

int relocateAttributes(ScribusDoc* doc)
{
	const QString f = folder(doc);
	if (f.isEmpty())
		return 0;
	const QString here = QFileInfo(f).absoluteFilePath();
	const QString marker = QStringLiteral("_photos/");
	int changed = 0;
	for (PageItem* item : allItems(doc))
	{
		for (const QString& a : pathAttributes())
		{
			const QString v = attrOf(item, a);
			if (v.isEmpty())
				continue;
			const int pos = v.indexOf(marker);
			if (pos < 0)
				continue;
			const QString rel = v.mid(pos + marker.length());
			const QString nv = here + QStringLiteral("/") + rel;
			if (nv != v && !QFileInfo::exists(v) && QFileInfo::exists(nv))
			{
				setAttrOf(item, a, nv);
				++changed;
			}
		}
	}
	return changed;
}

QStringList referencedFiles(const ScribusDoc* doc)
{
	QSet<QString> set;
	for (PageItem* item : allItems(doc))
	{
		if (!item->Pfile.isEmpty())
			set << QFileInfo(item->Pfile).absoluteFilePath();
		for (const QString& a : pathAttributes())
		{
			const QString v = attrOf(item, a);
			if (!v.isEmpty())
			{
				set << QFileInfo(v).absoluteFilePath();
				// sidecars of the obituary script and of the photo editor
				if (v.endsWith(QLatin1String(".png")))
				{
					set << QFileInfo(v).absoluteFilePath() + QStringLiteral(".json");
					set << QFileInfo(v.left(v.length() - 4) + QStringLiteral(".cut.png")).absoluteFilePath();
				}
			}
		}
	}
	return QStringList(set.begin(), set.end());
}

QStringList unusedFiles(const ScribusDoc* doc)
{
	QStringList out;
	const QString f = folder(doc);
	if (f.isEmpty() || !QDir(f).exists())
		return out;
	const QStringList refs = referencedFiles(doc);
	const QSet<QString> used(refs.begin(), refs.end());
	QDirIterator it(f, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		const QString p = it.next();
		if (p.endsWith(QLatin1String(".part")))
		{
			out << p;
			continue;
		}
		if (!used.contains(QFileInfo(p).absoluteFilePath()))
			out << p;
	}
	out.sort();
	return out;
}

QList<PageItem*> missingNewsPhotos(const ScribusDoc* doc)
{
	QList<PageItem*> out;
	for (PageItem* item : allItems(doc))
		if (isNewsPhoto(item) && !item->isImageInline() && !item->Pfile.isEmpty() && !QFileInfo::exists(item->Pfile))
			out << item;
	return out;
}
} // namespace SuneerPhotoStore
