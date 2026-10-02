/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerimagelinks.h"

#include <QFileInfo>
#include <QObject>
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

namespace
{
	// -1 = not read from the prefs file yet
	int s_badges = -1;
	int s_alwaysEmbed = -1;

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
		return out;
	}

	QString pageOf(ScribusDoc* doc, const PageItem* item)
	{
		if (!item->OnMasterPage.isEmpty())
			return QObject::tr("Master: %1").arg(item->OnMasterPage);
		if (item->OwnPage >= 0 && item->OwnPage < doc->DocPages.count())
			return QString::number(item->OwnPage + 1);
		return QString("-");
	}
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

bool SuneerImageLinks::embedItem(ScribusDoc* doc, PageItem* item)
{
	if (!doc || statusOf(item) != Linked)
		return false;
	const QString oldPath = item->Pfile;
	if (!QFileInfo::exists(oldPath))
		return false;
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
	item->update();
	return true;
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
	if (embedItem(doc, item))
		doc->changed();
}

bool SuneerImageLinks::badgesShown()            { return readPref(s_badges, "show_link_badges"); }
void SuneerImageLinks::setBadgesShown(bool on)  { writePref(s_badges, "show_link_badges", on); }
bool SuneerImageLinks::alwaysEmbed()            { return readPref(s_alwaysEmbed, "always_embed"); }
void SuneerImageLinks::setAlwaysEmbed(bool on)  { writePref(s_alwaysEmbed, "always_embed", on); }
