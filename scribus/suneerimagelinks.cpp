/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerimagelinks.h"
#include "suneerphotostore.h"

#include <QPen>
#include <QPainter>
#include <QImage>
#include <QHash>
#include <QFontMetrics>
#include <QFont>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include "filewatcher.h"
#include "pageitem.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "undomanager.h"
#include "undostate.h"
#include "undotransaction.h"
#include "util_file.h"

namespace
{
	// -1 = not read from the prefs file yet
	int s_badges = -1;
	int s_alwaysEmbed = -1;
	int s_badgeSize = -1;
	// Drawn once per (status, size, label): the canvas asks on every repaint.
	QHash<int, QImage> s_badgeCache;

	PrefsContext* prefsContext()
	{
		PrefsFile* pf = PrefsManager::instance().prefsFile;
		return pf ? pf->getContext("suneer_images") : nullptr;
	}

	bool readPref(int& cache, const QString& key)
	{
		if (cache < 0)
		{
			PrefsContext* ctx = prefsContext();
			if (!ctx)
				return true; // prefs not up yet: the default, and do not cache it
			cache = ctx->getBool(key, true) ? 1 : 0;
		}
		return cache == 1;
	}

	void writePref(int& cache, const QString& key, bool on)
	{
		cache = on ? 1 : 0;
		if (PrefsContext* ctx = prefsContext())
			ctx->set(key, on);
	}

	void addWithChildren(PageItem* item, QList<PageItem*>& out, QSet<PageItem*>& seen)
	{
		if (!item || seen.contains(item))
			return;
		seen.insert(item);
		out.append(item);
		if (item->isGroup())
		{
			const QList<PageItem*> children = item->getAllChildren();
			for (PageItem* child : children)
				addWithChildren(child, out, seen);
		}
	}

	QList<PageItem*> allItems(ScribusDoc* doc)
	{
		QList<PageItem*> out;
		QSet<PageItem*> seen;
		for (PageItem* item : std::as_const(doc->DocItems))
			addWithChildren(item, out, seen);
		for (PageItem* item : std::as_const(doc->MasterItems))
			addWithChildren(item, out, seen);
		for (auto it = doc->FrameItems.constBegin(); it != doc->FrameItems.constEnd(); ++it)
			addWithChildren(it.value(), out, seen);
		for (auto it = doc->docPatterns.constBegin(); it != doc->docPatterns.constEnd(); ++it)
			for (PageItem* item : it.value().items)
				addWithChildren(item, out, seen);
		return out;
	}

	QString attrOf(const PageItem* item, const QString& name)
	{
		if (!item)
			return QString();
		const QList<ObjectAttribute> l = item->getObjectAttributes(name);
		return l.isEmpty() ? QString() : l.first().value;
	}

	void setAttrOf(PageItem* item, const QString& name, const QString& value)
	{
		ObjAttrVector attrs = *item->getObjectAttributes();
		for (int i = 0; i < attrs.size(); ++i)
			if (attrs[i].name == name)
			{
				attrs[i].value = value;
				item->setObjectAttributes(&attrs);
				return;
			}
		ObjectAttribute a;
		a.name = name;
		a.type = QStringLiteral("string");
		a.value = value;
		attrs.append(a);
		item->setObjectAttributes(&attrs);
	}

	PageItem* topLevelOf(const PageItem* item)
	{
		const PageItem* top = item;
		while (top && top->Parent)
			top = top->Parent;
		return const_cast<PageItem*>(top);
	}

	bool sameBytes(const QString& a, const QString& b)
	{
		QFile fa(a), fb(b);
		if (fa.size() != fb.size() || !fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
			return false;
		while (!fa.atEnd())
			if (fa.read(1 << 20) != fb.read(1 << 20))
				return false;
		return true;
	}

	QString pageOf(ScribusDoc* doc, const PageItem* item)
	{
		return SuneerImageLinks::placeOf(doc, item);
	}

	QString frameLabel(ScribusDoc* doc, const PageItem* item)
	{
		const QString place = SuneerImageLinks::placeOf(doc, item);
		bool isNumber = false;
		place.toInt(&isNumber);
		return QObject::tr("%1 (%2)").arg(item->itemName(), isNumber ? QObject::tr("page %1").arg(place) : place);
	}
}

const char* const SuneerImageLinks::AttrEmbeddedFrom = "suneer.embeddedFrom";

QString SuneerImageLinks::placeOf(ScribusDoc* doc, const PageItem* item)
{
	if (!doc || !item)
		return QString("-");
	const PageItem* top = topLevelOf(item);
	if (top->isEmbedded)
		return QObject::tr("in text");
	if (!top->OnMasterPage.isEmpty())
		return QObject::tr("Master: %1").arg(top->OnMasterPage);
	if (doc->DocItems.contains(const_cast<PageItem*>(top)))
	{
		if (top->OwnPage >= 0 && top->OwnPage < doc->DocPages.count())
			return QString::number(top->OwnPage + 1);
		return QObject::tr("pasteboard");
	}
	for (auto it = doc->docPatterns.constBegin(); it != doc->docPatterns.constEnd(); ++it)
		if (it.value().items.contains(const_cast<PageItem*>(top)))
			return QObject::tr("pattern %1").arg(it.key());
	return QString("-");
}

QString SuneerImageLinks::kindOf(ScribusDoc* doc, const PageItem* item)
{
	QStringList k;
	if (!item)
		return QString();
	if (SuneerPhotoStore::isNewsPhoto(item))
		k << QStringLiteral("News");
	if (!attrOf(item, QStringLiteral("suneer.photoEditOutput")).isEmpty() || !attrOf(item, QStringLiteral("SuneerImgOriginal")).isEmpty())
		k << QStringLiteral("edited");
	if (!attrOf(item, QStringLiteral("suneer.photoEditCut")).isEmpty() || !attrOf(item, QStringLiteral("SuneerBgCut")).isEmpty()
	    || !attrOf(item, QStringLiteral("SuneerBgSource")).isEmpty()
	    || (!attrOf(item, QStringLiteral("obit.out")).isEmpty() && item->Pfile.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)))
		k << QStringLiteral("BG removed");
	if (attrOf(item, QStringLiteral("news.model")) == QLatin1String("obituary") || !attrOf(item, QStringLiteral("obit.src")).isEmpty())
		k << QStringLiteral("obit");
	if (!item->isImageInline() && doc && SuneerPhotoStore::isInStore(doc, item->Pfile))
		k << QStringLiteral("_photos");
	if (item->Parent)
		k << QStringLiteral("group '%1'").arg(item->Parent->itemName());
	if (item->effectsInUse.count() > 0)
		k << QStringLiteral("effects %1").arg(item->effectsInUse.count());
	return k.join(QStringLiteral(", "));
}

QList<PageItem*> SuneerImageLinks::imageFrames(ScribusDoc* doc)
{
	QList<PageItem*> out;
	if (!doc)
		return out;
	const QList<PageItem*> items = allItems(doc);
	for (PageItem* item : items)
		if (statusOf(item) != NotAnImage)
			out.append(item);
	return out;
}

QList<PageItem*> SuneerImageLinks::imageFramesIn(const QList<PageItem*>& items)
{
	QList<PageItem*> all;
	QSet<PageItem*> seen;
	for (PageItem* item : items)
		addWithChildren(item, all, seen);
	QList<PageItem*> out;
	for (PageItem* item : std::as_const(all))
		if (statusOf(item) != NotAnImage)
			out.append(item);
	return out;
}

QStringList SuneerImageLinks::describeAll(ScribusDoc* doc)
{
	QStringList lines;
	const QList<PageItem*> items = imageFrames(doc);
	for (PageItem* item : items)
	{
		Status st = statusOf(item);
		QString state = statusText(st);
		if (st == Linked && !QFileInfo::exists(item->Pfile))
			state = statusText(Missing);
		if (st == Embedded && !QFileInfo::exists(item->Pfile))
			state = QStringLiteral("Embedded, copy LOST");
		const QString file = st == Embedded ? attrOf(item, QString::fromLatin1(AttrEmbeddedFrom)) : item->Pfile;
		lines << QStringLiteral("%1 | %2 | %3 | %4 | %5x%6 | %7")
		         .arg(placeOf(doc, item), item->itemName(), state, kindOf(doc, item))
		         .arg(item->pixm.width()).arg(item->pixm.height())
		         .arg(file.isEmpty() ? QStringLiteral("-") : QFileInfo(file).fileName());
	}
	return lines;
}

SuneerImageLinks::Status SuneerImageLinks::statusOf(const PageItem* item)
{
	if (!item || !item->isImageFrame() || item->isOSGFrame() || item->isLatexFrame())
		return NotAnImage;
	if (item->Pfile.isEmpty())
		return Empty;
	if (item->isImageInline())
		return Embedded;
	return item->imageIsAvailable ? Linked : Missing;
}

QString SuneerImageLinks::statusText(Status s)
{
	switch (s)
	{
		case Embedded: return QObject::tr("Embedded");
		case Linked:   return QObject::tr("Linked (not embedded)");
		case Missing:  return QObject::tr("Missing file");
		case Empty:    return QObject::tr("Empty frame");
		default:       return QString();
	}
}

QList<SuneerImageLinks::Entry> SuneerImageLinks::collect(ScribusDoc* doc, bool onlyProblems)
{
	QList<Entry> result;
	if (!doc)
		return result;
	const QList<PageItem*> items = allItems(doc);
	for (PageItem* item : items)
	{
		Status st = statusOf(item);
		if (st == NotAnImage || st == Empty)
			continue;
		// The flag says "was loaded"; the file may have gone since.
		if (st == Linked && !QFileInfo::exists(item->Pfile))
			st = Missing;
		// a linked file inside "<doc>_photos/" is intended (relative path, moves with the document)
		if (onlyProblems && st == Linked && SuneerPhotoStore::isInStore(doc, item->Pfile))
			continue;
		if (onlyProblems && st == Embedded)
			continue;
		Entry e;
		e.item = item;
		e.page = pageOf(doc, item);
		e.frameName = item->itemName();
		e.path = item->Pfile;
		e.status = st;
		result.append(e);
	}
	return result;
}

bool SuneerImageLinks::embedItem(ScribusDoc* doc, PageItem* item, qint64* bytes)
{
	if (!doc || statusOf(item) != Linked)
		return false;
	const QString oldPath = item->Pfile;
	if (!QFileInfo::exists(oldPath))
		return false;
	if (bytes)
		*bytes = QFileInfo(oldPath).size();
	FileWatcher* fw = ScCore->fileWatcher;
	const bool watched = fw && fw->isWatching(oldPath);
	if (watched)
		fw->removeFile(oldPath);
	// Copies the picture to a temp file and sets isInlineImage; the saver then
	// writes it as Base64 ImageData. Fails silently when the copy fails.
	item->makeImageInline();
	if (!item->isImageInline())
	{
		if (watched)
			fw->addFile(oldPath);
		return false;
	}
	if (fw)
		fw->addFile(item->Pfile);
	if (UndoManager::undoEnabled())
	{
		auto* ss = new SimpleState(QObject::tr("Embed image"), QFileInfo(oldPath).fileName(), Um::IGetImage);
		ss->set("SUNEER_IMAGE_INLINE");
		ss->set("OLD_PATH", oldPath);
		ss->set("NEW_PATH", item->Pfile);
		UndoManager::instance()->action(item, ss);
	}
	// where it came from: "Unembed / Extract" names the file after it, and
	// the photo folder's relocation / Save As keep it pointing at the folder
	setAttrOf(item, QString::fromLatin1(AttrEmbeddedFrom), QFileInfo(oldPath).absoluteFilePath());
	item->update();
	return true;
}

SuneerImageLinks::EmbedReport SuneerImageLinks::embedItems(ScribusDoc* doc, const QList<PageItem*>& items,
                                                           const std::function<void(int, int)>& progress)
{
	EmbedReport rep;
	if (!doc)
		return rep;
	QList<PageItem*> todo;
	QSet<PageItem*> seen;
	for (PageItem* item : items)
	{
		const Status st = statusOf(item);
		if (st == NotAnImage || st == Empty || seen.contains(item))
			continue;
		seen.insert(item);
		todo.append(item);
	}
	rep.total = todo.count();
	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(QObject::tr("Document"), Um::IDocument, QObject::tr("Embed all images"), QString(), Um::IGetImage);
	FileWatcher* fw = ScCore->fileWatcher;
	for (int i = 0; i < todo.count(); ++i)
	{
		PageItem* item = todo.at(i);
		if (progress)
			progress(i, todo.count());
		if (item->isImageInline())
		{
			if (QFileInfo::exists(item->Pfile))
			{
				++rep.already;
				continue;
			}
			// The temp copy behind an embedded picture is gone (a /tmp clean-up
			// while Scribus ran): Save would write the frame without ImageData.
			const QString from = attrOf(item, QString::fromLatin1(AttrEmbeddedFrom));
			if (!from.isEmpty() && QFileInfo::exists(from))
			{
				item->Pfile = from;
				item->isInlineImage = false;
				item->isTempFile = false;
				item->makeImageInline();
				if (item->isImageInline())
				{
					if (fw)
						fw->addFile(item->Pfile);
					++rep.repaired;
					continue;
				}
			}
			rep.skipped << QObject::tr("%1: embedded copy lost and no file to take it from again").arg(frameLabel(doc, item));
			continue;
		}
		if (!QFileInfo::exists(item->Pfile))
		{
			rep.skipped << QObject::tr("%1: missing file %2").arg(frameLabel(doc, item), QFileInfo(item->Pfile).fileName());
			continue;
		}
		if (!item->imageIsAvailable)
		{
			// the file is there now (folder mounted / restored after loading): load it first
			const bool fho = item->imageFlippedH();
			const bool fvo = item->imageFlippedV();
			doc->loadPict(item->Pfile, item, true, false);
			item->setImageFlippedH(fho);
			item->setImageFlippedV(fvo);
			if (!item->imageIsAvailable)
			{
				rep.skipped << QObject::tr("%1: the file cannot be read: %2").arg(frameLabel(doc, item), QFileInfo(item->Pfile).fileName());
				continue;
			}
		}
		qint64 b = 0;
		if (embedItem(doc, item, &b))
		{
			++rep.embedded;
			rep.bytes += b;
		}
		else
			rep.skipped << QObject::tr("%1: could not copy %2").arg(frameLabel(doc, item), QFileInfo(item->Pfile).fileName());
	}
	if (progress)
		progress(todo.count(), todo.count());
	if (transaction.isStarted())
	{
		if (rep.embedded > 0)
			transaction.commit();
		else
			transaction.cancel();
	}
	if (rep.embedded > 0 || rep.repaired > 0)
	{
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
	return rep;
}

SuneerImageLinks::ExtractReport SuneerImageLinks::extractItems(ScribusDoc* doc, const QList<PageItem*>& items,
                                                               const std::function<void(int, int)>& progress)
{
	ExtractReport rep;
	if (!doc)
		return rep;
	rep.folder = SuneerPhotoStore::editedDir(doc, true);
	QList<PageItem*> todo;
	for (PageItem* item : items)
		if (statusOf(item) == Embedded && !todo.contains(item))
			todo.append(item);
	if (rep.folder.isEmpty())
	{
		for (PageItem* item : std::as_const(todo))
			rep.skipped << QObject::tr("%1: the document has no photos folder yet - save it first").arg(frameLabel(doc, item));
		return rep;
	}
	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(QObject::tr("Document"), Um::IDocument, QObject::tr("Extract embedded images"), QString(), Um::IGetImage);
	FileWatcher* fw = ScCore->fileWatcher;
	const QString marker = QStringLiteral("_photos/edited/");
	for (int i = 0; i < todo.count(); ++i)
	{
		PageItem* item = todo.at(i);
		if (progress)
			progress(i, todo.count());
		const QString temp = item->Pfile;
		if (!QFileInfo::exists(temp))
		{
			rep.skipped << QObject::tr("%1: the embedded copy is gone").arg(frameLabel(doc, item));
			continue;
		}
		// the name it had when it was embedded (sub-folder of edited/ kept), else the frame name
		const QString from = attrOf(item, QString::fromLatin1(AttrEmbeddedFrom));
		QString rel;
		if (!from.isEmpty())
		{
			const int pos = from.indexOf(marker);
			rel = pos >= 0 ? from.mid(pos + marker.length()) : QFileInfo(from).fileName();
		}
		if (rel.isEmpty())
		{
			QString stem = item->itemName();
			stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
			rel = stem + QStringLiteral(".") + QFileInfo(temp).suffix();
		}
		QString target = rep.folder + QStringLiteral("/") + rel;
		if (QFileInfo::exists(target) && !sameBytes(temp, target))
		{
			// a different picture already has this name: <stem>2.<ext>, <stem>3.<ext> ...
			const QFileInfo fi(target);
			for (int n = 2; n < 10000 && QFileInfo::exists(target) && !sameBytes(temp, target); ++n)
				target = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + QString::number(n) + QStringLiteral(".") + fi.suffix();
		}
		if (!QFileInfo::exists(target))
		{
			QDir().mkpath(QFileInfo(target).absolutePath());
			if (!copyFile(temp, target))
			{
				rep.skipped << QObject::tr("%1: could not write %2").arg(frameLabel(doc, item), target);
				continue;
			}
		}
		if (fw && fw->isWatching(temp))
			fw->removeFile(temp);
		// The temp copy stays on disk: undo makes the frame embedded again from it.
		item->Pfile = QDir::fromNativeSeparators(target);
		item->isInlineImage = false;
		item->isTempFile = false;
		if (fw)
			fw->addFile(item->Pfile);
		if (UndoManager::undoEnabled())
		{
			auto* ss = new SimpleState(QObject::tr("Extract image"), QFileInfo(target).fileName(), Um::IGetImage);
			ss->set("SUNEER_IMAGE_EXTRACT");
			ss->set("OLD_PATH", item->Pfile);
			ss->set("NEW_PATH", temp);
			UndoManager::instance()->action(item, ss);
		}
		item->update();
		++rep.extracted;
	}
	if (progress)
		progress(todo.count(), todo.count());
	if (transaction.isStarted())
	{
		if (rep.extracted > 0)
			transaction.commit();
		else
			transaction.cancel();
	}
	if (rep.extracted > 0)
	{
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
	return rep;
}

int SuneerImageLinks::embedAll(ScribusDoc* doc, QStringList* failed)
{
	if (!doc)
		return 0;
	const QList<Entry> entries = collect(doc, true);
	if (entries.isEmpty())
		return 0;
	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(QObject::tr("Document"), Um::IDocument, QObject::tr("Embed all images"), QString(), Um::IGetImage);
	int embedded = 0;
	for (const Entry& e : entries)
	{
		if (e.item && e.status == Linked && embedItem(doc, e.item))
		{
			++embedded;
			continue;
		}
		if (failed)
			failed->append(QString("%1 / %2: %3").arg(e.page, e.frameName, e.path));
	}
	if (transaction.isStarted())
	{
		if (embedded > 0)
			transaction.commit();
		else
			transaction.cancel();
	}
	if (embedded > 0)
	{
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
	return embedded;
}

void SuneerImageLinks::embedPlaced(ScribusDoc* doc, PageItem* item)
{
	if (!doc || !item || doc->isLoading() || !alwaysEmbed())
		return;
	// News Browser photos live next to the document and stay linked.
	if (SuneerPhotoStore::isNewsPhoto(item) || SuneerPhotoStore::isInStore(doc, item->Pfile))
		return;
	if (embedItem(doc, item))
		doc->changed();
}

bool SuneerImageLinks::badgesShown()            { return readPref(s_badges, "show_link_badges"); }
void SuneerImageLinks::setBadgesShown(bool on)  { writePref(s_badges, "show_link_badges", on); }
bool SuneerImageLinks::alwaysEmbed()            { return readPref(s_alwaysEmbed, "always_embed"); }
void SuneerImageLinks::setAlwaysEmbed(bool on)  { writePref(s_alwaysEmbed, "always_embed", on); }

int SuneerImageLinks::badgeSize()
{
	if (s_badgeSize < 0)
	{
		PrefsContext* ctx = prefsContext();
		if (!ctx)
			return BadgeLarge; // prefs not up yet: the default, and do not cache it
		s_badgeSize = qBound(int(BadgeSmall), ctx->getInt("badge_size", BadgeLarge), int(BadgeLarge));
	}
	return s_badgeSize;
}

void SuneerImageLinks::setBadgeSize(int size)
{
	s_badgeSize = qBound(int(BadgeSmall), size, int(BadgeLarge));
	if (PrefsContext* ctx = prefsContext())
		ctx->set("badge_size", s_badgeSize);
}

int SuneerImageLinks::badgePixels()
{
	switch (badgeSize())
	{
		case BadgeSmall:  return 20;
		case BadgeMedium: return 24;
		default:          return 28;
	}
}

QImage SuneerImageLinks::badgeImage(Status status, bool withLabel)
{
	if (status != Linked && status != Missing)
		return QImage();
	const bool missing = (status == Missing);
	const int h = badgePixels();
	const int key = h * 4 + (missing ? 2 : 0) + (withLabel ? 1 : 0);
	const auto cached = s_badgeCache.constFind(key);
	if (cached != s_badgeCache.constEnd())
		return cached.value();

	// Not translated: the operator reads these two words as symbols.
	const QString label = missing ? QStringLiteral("MISSING") : QStringLiteral("LINK");
	QFont font(QStringLiteral("Sans"));
	font.setBold(true);
	font.setPixelSize(qRound(h * 0.52));
	font.setLetterSpacing(QFont::AbsoluteSpacing, h / 28.0);
	const QFontMetrics fm(font);
	const int w = withLabel ? fm.horizontalAdvance(label) + qRound(h * 0.75) : h;

	// ARGB32, not premultiplied: ScPainter::drawImage() uses the colour
	// channels as they are and the alpha channel as a mask.
	QImage img(w, h, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	// White outline, so the badge stands off dark and light photos alike.
	const qreal outline = qMax(1.5, h / 18.0);
	const QRectF body(outline / 2.0, outline / 2.0, w - outline, h - outline);
	painter.setPen(QPen(Qt::white, outline));
	painter.setBrush(missing ? QColor(214, 30, 30) : QColor(255, 140, 0));
	painter.drawRoundedRect(body, h * 0.18, h * 0.18);
	if (withLabel)
	{
		painter.setFont(font);
		painter.setPen(Qt::white);
		painter.drawText(QRectF(0, 0, w, h), Qt::AlignCenter, label);
	}
	else if (missing)
	{
		// a white cross, so red and orange differ by shape too
		painter.setPen(QPen(Qt::white, h * 0.12, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(w * 0.30, h * 0.30), QPointF(w * 0.70, h * 0.70));
		painter.drawLine(QPointF(w * 0.70, h * 0.30), QPointF(w * 0.30, h * 0.70));
	}
	painter.end();
	s_badgeCache.insert(key, img);
	return img;
}
