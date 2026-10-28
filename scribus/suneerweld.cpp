#include "suneerweld.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cstdio>

#include "pageitem.h"
#include "pageitem_textframe.h"
#include "pagestructs.h"
#include "scribusdoc.h"
#include "suneerspacing.h"
#include "undomanager.h"
#include "undotransaction.h"

namespace
{
QString attrOf(const PageItem* item, const QString& name)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> found = item->getObjectAttributes(name);
	return found.isEmpty() ? QString() : found.first().value;
}

// a uid made by ensurePhotoUid (a UUID without braces), not an item name
bool isUid(const QString& s)
{
	return s.size() == 36 && !QUuid::fromString(s).isNull();
}

QString newUid()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void removeAttr(PageItem* item, const QString& name)
{
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = attrs.size() - 1; i >= 0; --i)
		if (attrs[i].name == name)
			attrs.removeAt(i);
	item->setObjectAttributes(&attrs);
}
}

namespace SuneerWeld
{

QString storyId(const PageItem* item)
{
	return attrOf(item, QStringLiteral("news.story"));
}

QString attr(const PageItem* item, const QString& name)
{
	return attrOf(item, name);
}

void setAttr(PageItem* item, const QString& name, const QString& value)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (ObjectAttribute& a : attrs)
		if (a.name == name)
		{
			a.value = value;
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

bool captionAttached(const PageItem* photo)
{
	return attrOf(photo, QStringLiteral("news.captionAttached")) != QLatin1String("0");
}

void setCaptionAttached(PageItem* photo, bool on)
{
	setAttr(photo, QStringLiteral("news.captionAttached"), on ? QStringLiteral("1") : QStringLiteral("0"));
}

// every item of the document, group members included (an obituary module is a group)
static void collectItems(const QList<PageItem*>& from, QList<PageItem*>& out)
{
	for (PageItem* it : from)
	{
		if (!it)
			continue;
		out << it;
		if (it->isGroup() && !it->groupItemList.isEmpty())
			collectItems(it->groupItemList, out);
	}
}

QString photoUid(const PageItem* photo)
{
	return attrOf(photo, QStringLiteral("news.photoUid"));
}

QString ensurePhotoUid(PageItem* photo)
{
	QString uid = photoUid(photo);
	if (uid.isEmpty() && photo)
	{
		uid = newUid();
		setAttr(photo, QStringLiteral("news.photoUid"), uid);
	}
	return uid;
}

void linkCaption(PageItem* photo, PageItem* caption)
{
	if (photo && caption)
		setAttr(caption, QStringLiteral("news.captionOf"), ensurePhotoUid(photo));
}

QList<PageItem*> captionsOf(ScribusDoc* doc, PageItem* photo, bool evenWhenDetached)
{
	QList<PageItem*> out;
	if (!doc || !photo || !photo->isImageFrame())
		return out;
	if (!evenWhenDetached && !captionAttached(photo))
		return out;
	QList<PageItem*> all;
	collectItems(doc->DocItems, all);
	// the stable link: the captions that carry the photo's uid - and only those, once the photo has one
	const QString uid = photoUid(photo);
	if (!uid.isEmpty())
	{
		for (PageItem* it : std::as_const(all))
			if (it && it != photo && it->isTextFrame() && attrOf(it, QStringLiteral("news.captionOf")) == uid)
				out << it;
		return out;
	}
	// a photo without uid (relinkDocument() gives one on load): the old rules, never a caption that carries a
	// uid - that caption belongs to the photo with that uid
	const QString name = photo->itemName();
	const QString byName = QStringLiteral("caption_") + name;
	const QString story = storyId(photo);
	const QString index = attrOf(photo, QStringLiteral("news.index"));
	const bool isNewsPhoto = attrOf(photo, QStringLiteral("news.role")) == QLatin1String("photo");
	QList<PageItem*> byAttr, byItemName, byStory;
	for (PageItem* it : std::as_const(all))
	{
		if (!it || it == photo || !it->isTextFrame())
			continue;
		const QString link = attrOf(it, QStringLiteral("news.captionOf"));
		if (isUid(link))
			continue;
		if (link == name)
			byAttr << it;
		else if (it->itemName() == byName)
			byItemName << it;
		else if (isNewsPhoto && !story.isEmpty() && attrOf(it, QStringLiteral("news.role")) == QLatin1String("caption")
		         && storyId(it) == story && attrOf(it, QStringLiteral("news.index")) == index)
			byStory << it;
	}
	if (!byAttr.isEmpty())
		return byAttr;
	if (!byItemName.isEmpty())
		return byItemName;
	return byStory;
}

// --- relinking (load, page import) and copies (paste, duplicate) ---

// a frame the relink looks at as a caption: a News Browser caption, one linked by news.captionOf, one named caption_<photo>
static bool isCaptionFrame(const PageItem* it)
{
	return it->isTextFrame() && (attrOf(it, QStringLiteral("news.role")) == QLatin1String("caption") || !attrOf(it, QStringLiteral("news.captionOf")).isEmpty()
	                             || it->itemName().startsWith(QLatin1String("caption_")));
}

// the old name link: news.captionOf = <photo name>, or the caption named caption_<photo>
static bool nameLinked(const PageItem* photo, const PageItem* cap)
{
	const QString name = photo->itemName();
	return attrOf(cap, QStringLiteral("news.captionOf")) == name || cap->itemName() == QStringLiteral("caption_") + name;
}

// how far the caption is from its place under the photo (pt); far when they are not in the same group
static double underDistance(const PageItem* photo, const PageItem* cap)
{
	if (photo->Parent != cap->Parent)
		return 1.0e5;
	return qAbs(cap->xPos() - photo->xPos()) + qAbs(cap->width() - photo->width()) + qAbs(cap->yPos() - (photo->yPos() + photo->height()));
}

// the two frames are welded to each other (not only through a third frame)
static bool directlyWelded(const PageItem* a, const PageItem* b)
{
	for (const PageItem::WeldingInfo& w : a->weldList)
		if (w.weldItem == b)
			return true;
	for (const PageItem::WeldingInfo& w : b->weldList)
		if (w.weldItem == a)
			return true;
	return false;
}

// the caption sits right under the photo: the same left edge and width (1.5 mm either way), its top from 1.5 mm
// inside the photo to 6 mm below it - where the image import, the control bar and the attach rule put a caption
static bool sitsUnder(const PageItem* photo, const PageItem* cap)
{
	const double mm = 72.0 / 25.4;
	if (photo->Parent != cap->Parent)
		return false;
	const double gap = cap->yPos() - (photo->yPos() + photo->height());
	return qAbs(cap->xPos() - photo->xPos()) <= 1.5 * mm && qAbs(cap->width() - photo->width()) <= 1.5 * mm && gap >= -1.5 * mm && gap <= 6.0 * mm;
}

struct LinkCand
{
	PageItem* photo;
	PageItem* cap;
	double cost;
};

// the frames of one story share a number: a weld cluster (stories are welded stars)
static QHash<const PageItem*, int> weldClusters(const QList<PageItem*>& items)
{
	QHash<const PageItem*, int> id;
	int next = 0;
	for (PageItem* it : items)
	{
		if (id.contains(it))
			continue;
		for (PageItem* c : cluster(it))
			id.insert(c, next);
		++next;
	}
	return id;
}

// the same story (weld cluster, or one obituary group) before everything, then the caption nearest its place
// under the photo, then the old name link
static double linkCost(const PageItem* photo, const PageItem* cap, const QHash<const PageItem*, int>& clusterOf)
{
	const bool sameStory = (photo->Parent && photo->Parent == cap->Parent) || clusterOf.value(photo, -1) == clusterOf.value(cap, -2);
	return (sameStory ? 0.0 : 1.0e6) + underDistance(photo, cap) - (nameLinked(photo, cap) ? 0.5 : 0.0);
}

// one photo per caption and one caption per photo, the cheapest candidates first
static QList<QPair<PageItem*, PageItem*>> matchOneToOne(QList<LinkCand> cands)
{
	std::stable_sort(cands.begin(), cands.end(), [](const LinkCand& a, const LinkCand& b) { return a.cost < b.cost; });
	QSet<PageItem*> usedPhoto, usedCap;
	QList<QPair<PageItem*, PageItem*>> out;
	for (const LinkCand& c : std::as_const(cands))
	{
		if (usedPhoto.contains(c.photo) || usedCap.contains(c.cap))
			continue;
		usedPhoto.insert(c.photo);
		usedCap.insert(c.cap);
		out << qMakePair(c.photo, c.cap);
	}
	return out;
}

int relinkDocument(ScribusDoc* doc, QString* report)
{
	if (!doc)
		return 0;
	QList<PageItem*> all;
	collectItems(doc->DocItems, all);
	QList<PageItem*> photos, caps;
	for (PageItem* it : std::as_const(all))
	{
		if (it->isImageFrame())
			photos << it;
		else if (isCaptionFrame(it))
			caps << it;
	}
	if (report)
		*report = QStringLiteral("no caption frames");
	if (caps.isEmpty())
		return 0;
	const QHash<const PageItem*, int> clusterOf = weldClusters(all);
	int byUid = 0, renewed = 0, detached = 0, upgraded = 0;
	QSet<PageItem*> pairedPhoto, pairedCap;

	// 1. the uid links. Photos that share a uid (copied by an older version): the first keeps it, every other one
	//    gets a new uid; each caption goes with the photo it belongs to; a second caption of one photo is detached
	QHash<QString, QList<PageItem*>> photosOf, capsOf;
	QStringList uidOrder;
	for (PageItem* p : std::as_const(photos))
	{
		const QString u = photoUid(p);
		if (u.isEmpty())
			continue;
		if (!photosOf.contains(u))
			uidOrder << u;
		photosOf[u] << p;
	}
	for (PageItem* c : std::as_const(caps))
	{
		const QString l = attrOf(c, QStringLiteral("news.captionOf"));
		if (!l.isEmpty())
			capsOf[l] << c;
	}
	for (const QString& u : std::as_const(uidOrder))
	{
		const QList<PageItem*> ps = photosOf.value(u);
		const QList<PageItem*> cs = capsOf.value(u);
		if (ps.size() == 1 && cs.size() <= 1)
		{
			if (cs.size() == 1)
			{
				pairedPhoto.insert(ps.first());
				pairedCap.insert(cs.first());
				++byUid;
			}
			continue;
		}
		QList<LinkCand> cands;
		for (PageItem* p : ps)
			for (PageItem* c : cs)
				cands << LinkCand { p, c, linkCost(p, c, clusterOf) };
		const QList<QPair<PageItem*, PageItem*>> pairs = matchOneToOne(cands);
		for (int i = 1; i < ps.size(); ++i)
		{
			setAttr(ps[i], QStringLiteral("news.photoUid"), newUid());
			++renewed;
		}
		for (const auto& pr : pairs)
		{
			setAttr(pr.second, QStringLiteral("news.captionOf"), photoUid(pr.first));
			pairedPhoto.insert(pr.first);
			pairedCap.insert(pr.second);
			++byUid;
		}
		for (PageItem* c : cs)
			if (!pairedCap.contains(c))
			{
				removeAttr(c, QStringLiteral("news.captionOf"));
				++detached;
			}
	}

	// 2. links the old way (a file of an older version): a News Browser caption goes with the photo of its story
	//    and index (news.story + news.index - not its name, which can belong to another photo since the last load);
	//    another caption with the photo it names (news.captionOf = <name>, caption_<name>) when it is welded to it,
	//    in its group or under it
	QList<LinkCand> cands;
	for (PageItem* c : std::as_const(caps))
	{
		if (pairedCap.contains(c))
			continue;
		const QString story = storyId(c);
		const QString index = attrOf(c, QStringLiteral("news.index"));
		const bool newsCaption = attrOf(c, QStringLiteral("news.role")) == QLatin1String("caption");
		for (PageItem* p : std::as_const(photos))
		{
			if (pairedPhoto.contains(p))
				continue;
			if (!story.isEmpty())
			{
				if (newsCaption && attrOf(p, QStringLiteral("news.role")) == QLatin1String("photo") && storyId(p) == story
				    && attrOf(p, QStringLiteral("news.index")) == index)
					cands << LinkCand { p, c, linkCost(p, c, clusterOf) };
			}
			else
			{
				// a caption made by the image import / the control bar (caption_<photo>): by the name it carries when
				// welded to the photo, in its group or under it; else by its weld to the photo when it sits under it;
				// else by where it sits alone (the photo's name is not in the file: it is made again on every load)
				const bool under = sitsUnder(p, c);
				const bool welded = directlyWelded(p, c);
				double tier = -1.0;
				if (nameLinked(p, c) && (linkCost(p, c, clusterOf) < 1.0e6 || underDistance(p, c) < 20.0 * 72.0 / 25.4))
					tier = 0.0;
				else if (welded && under)
					tier = 1.0;
				else if (under)
					tier = 2.0;
				if (tier >= 0.0)
					cands << LinkCand { p, c, tier * 1.0e7 + linkCost(p, c, clusterOf) };
			}
		}
	}
	for (const auto& pr : matchOneToOne(cands))
	{
		linkCaption(pr.first, pr.second);
		// the gap it already has under its photo is its caption gap from now on (an older placement 0.2 mm, an image-
		// import caption 1 mm): the next attach keeps it where it is instead of moving it to the model / house gap
		if (attrOf(pr.second, QStringLiteral("news.captionGap")).isEmpty() && sitsUnder(pr.first, pr.second))
			setAttr(pr.second, QStringLiteral("news.captionGap"),
			        QString::number(pr.second->yPos() - (pr.first->yPos() + pr.first->height()), 'f', 3));
		pairedCap.insert(pr.second);
		++upgraded;
	}

	// 3. weld points outside their frame (an older version moved a caption without its weld point: one 5336 pt right
	//    of a caption 847 pt wide): a photo and its caption get the edge they share, any other weld the midpoint of
	//    the two frames' centres (as Scribus welds frames), kept inside the frame
	QHash<QString, PageItem*> photoOfUid;
	for (PageItem* p : std::as_const(photos))
		if (!photoUid(p).isEmpty())
			photoOfUid.insert(photoUid(p), p);
	auto linkedPair = [&](PageItem* a, PageItem* b, PageItem*& photo, PageItem*& cap) {
		for (int k = 0; k < 2; ++k)
		{
			PageItem* c = k == 0 ? a : b;
			PageItem* p = k == 0 ? b : a;
			if (p->isImageFrame() && c->isTextFrame() && photoOfUid.value(attrOf(c, QStringLiteral("news.captionOf"))) == p)
			{
				photo = p;
				cap = c;
				return true;
			}
		}
		return false;
	};
	int weldsRepaired = 0;
	for (PageItem* it : std::as_const(all))
	{
		for (int i = 0; i < it->weldList.count(); ++i)
		{
			PageItem* other = it->weldList[i].weldItem;
			const FPoint wp = it->weldList[i].weldPoint;
			if (!other || (wp.x() >= -1.0 && wp.x() <= it->width() + 1.0 && wp.y() >= -1.0 && wp.y() <= it->height() + 1.0))
				continue;
			PageItem* photo = nullptr;
			PageItem* cap = nullptr;
			if (linkedPair(it, other, photo, cap))
				fixCaptionWeldPoints(photo, cap);
			else
			{
				const double mx = it->width() / 2.0 + ((other->xPos() + other->width() / 2.0) - (it->xPos() + it->width() / 2.0)) / 2.0;
				const double my = it->height() / 2.0 + ((other->yPos() + other->height() / 2.0) - (it->yPos() + it->height() / 2.0)) / 2.0;
				it->weldList[i].weldPoint = FPoint(qBound(0.0, mx, it->width()), qBound(0.0, my, it->height()));
			}
			++weldsRepaired;
		}
	}
	if (report)
		*report = QStringLiteral("%1 caption(s) linked by uid, %2 linked now from the old name / story / weld / place, %3 copied photo uid(s) renewed, "
		                         "%4 second caption(s) detached, %5 caption frame(s) without a photo, %6 weld point(s) outside their frame repaired")
		              .arg(byUid).arg(upgraded).arg(renewed).arg(detached).arg(caps.size() - pairedCap.size()).arg(weldsRepaired);
	return upgraded + renewed + detached + weldsRepaired;
}

void fixCaptionWeldPoints(PageItem* photo, PageItem* caption)
{
	if (!photo || !caption)
		return;
	photo->setWeldPoint(0.0, photo->height(), caption);   // no-op when the two are not welded
	caption->setWeldPoint(0.0, 0.0, photo);
}

QList<PageItem*> orphanCaptions(ScribusDoc* doc)
{
	QList<PageItem*> out;
	if (!doc)
		return out;
	QList<PageItem*> all;
	collectItems(doc->DocItems, all);
	QSet<PageItem*> linked;
	for (PageItem* it : std::as_const(all))
		if (it->isImageFrame())
			for (PageItem* c : captionsOf(doc, it, true))
				linked.insert(c);
	for (PageItem* it : std::as_const(doc->DocItems))
		if (it && isCaptionFrame(it) && !linked.contains(it))
			out << it;
	return out;
}

QList<PageItem*> captionsLeftBehind(ScribusDoc* doc, const QList<PageItem*>& items)
{
	QList<PageItem*> out;
	if (!doc)
		return out;
	QList<PageItem*> all;
	collectItems(items, all);   // the members of a selected group go with it
	const QSet<PageItem*> going(all.begin(), all.end());
	for (PageItem* it : std::as_const(all))
		if (it->isImageFrame())
			for (PageItem* c : captionsOf(doc, it, true))
				if (!going.contains(c) && !out.contains(c))
					out << c;
	return out;
}

int renewCopiedLinks(ScribusDoc* doc, const QList<PageItem*>& addedTop)
{
	if (!doc || addedTop.isEmpty())
		return 0;
	QList<PageItem*> added;
	collectItems(addedTop, added);
	const QSet<PageItem*> isAdded(added.begin(), added.end());
	// what the document had before: its photos' uids, and how many captions carry each
	QList<PageItem*> before;
	collectItems(doc->DocItems, before);
	collectItems(doc->MasterItems, before);
	QSet<QString> uids;
	QHash<QString, int> captionsWith;
	for (PageItem* it : std::as_const(before))
	{
		if (isAdded.contains(it))
			continue;
		if (it->isImageFrame())
		{
			const QString u = photoUid(it);
			if (!u.isEmpty())
				uids.insert(u);
		}
		else if (it->isTextFrame())
		{
			const QString l = attrOf(it, QStringLiteral("news.captionOf"));
			if (!l.isEmpty())
				captionsWith[l] += 1;
		}
	}
	int n = 0;
	QHash<QString, QString> renewed;   // the copied uid -> the copy's own
	for (PageItem* it : std::as_const(added))
	{
		if (!it->isImageFrame())
			continue;
		const QString u = photoUid(it);
		if (u.isEmpty() || !uids.contains(u))
			continue;
		const QString nu = newUid();
		setAttr(it, QStringLiteral("news.photoUid"), nu);
		renewed.insert(u, nu);
		++n;
	}
	for (PageItem* it : std::as_const(added))
	{
		if (!it->isTextFrame())
			continue;
		const QString l = attrOf(it, QStringLiteral("news.captionOf"));
		if (l.isEmpty())
			continue;
		if (renewed.contains(l))
		{
			setAttr(it, QStringLiteral("news.captionOf"), renewed.value(l));
			++n;
		}
		else if (uids.contains(l) && captionsWith.value(l) > 0)
		{
			// a copy of the caption alone: its photo keeps its own caption, the copy is a free text frame
			removeAttr(it, QStringLiteral("news.captionOf"));
			++n;
		}
	}
	if (n > 0 && qEnvironmentVariableIsSet("SUNEER_CAPTION_DEBUG"))
		fprintf(stderr, "CAPTION copy  : %d link(s) renewed for %d copied photo(s) in %d new item(s)\n", n, int(renewed.size()), int(added.size()));
	return n;
}

static bool s_attaching = false;

// undo / redo replay (ScribusDoc::isUndoRedoOngoing): the photos whose captions are attached once the step
// is complete, and the frames whose position / size the step restored from their own undo records
static QList<QPointer<PageItem>> s_undoPhotos;
static QSet<const PageItem*> s_restoredPos, s_restoredSize;

// on the page or the pasteboard (a group member there too) - not only kept alive by an undo record
static bool inDocument(ScribusDoc* doc, const PageItem* item)
{
	const PageItem* top = item;
	while (top->Parent)
		top = top->Parent;
	PageItem* t = const_cast<PageItem*>(top);
	return doc->DocItems.contains(t) || doc->MasterItems.contains(t);
}

// afterUndo (undoStepDone): a caption keeps the position / size its own undo record restored in this step,
// is left alone when it already sits there, and is fitted only when it takes a new width (its text is the same)
static int attachNow(ScribusDoc* doc, PageItem* photo, bool fitHeight, bool afterUndo)
{
	const QList<PageItem*> caps = captionsOf(doc, photo);
	if (caps.isEmpty())
		return 0;
	s_attaching = true;
	int n = 0;
	const bool dbg = qEnvironmentVariableIsSet("SUNEER_CAPTION_DEBUG");
	// nothing in here is an undo step of its own: the photo's own undo re-attaches the caption
	UndoManager::instance()->setUndoEnabled(false);   // counter: paired with the true at the end
	for (PageItem* cap : caps)
	{
		if (dbg)
			fprintf(stderr, "CAPTION before: photo %s x=%.2f y=%.2f w=%.2f h=%.2f | caption %s x=%.2f y=%.2f w=%.2f h=%.2f (mm)\n", qPrintable(photo->itemName()),
			        photo->xPos() / 2.8346, photo->yPos() / 2.8346, photo->width() / 2.8346, photo->height() / 2.8346, qPrintable(cap->itemName()),
			        cap->xPos() / 2.8346, cap->yPos() / 2.8346, cap->width() / 2.8346, cap->height() / 2.8346);
		// the gap written at placement (pt), else the layout model the story was placed with
		// (news.model: "normal", "obituary", ...), else the house caption gap
		bool okGap = false;
		double gap = attrOf(cap, QStringLiteral("news.captionGap")).toDouble(&okGap);
		if (!okGap)
		{
			QString model = attrOf(photo, QStringLiteral("news.model"));
			if (model.isEmpty())
				model = attrOf(cap, QStringLiteral("news.model"));
			if (model.startsWith(QLatin1String("obituary")))
				model = QStringLiteral("obituary");
			gap = model.isEmpty() ? SuneerSpacing::houseDefault(SuneerSpacing::CaptionGap) * 72.0 / 25.4
			                      : SuneerSpacing::forModel(model).pt(SuneerSpacing::CaptionGap);
		}
		const bool keepPos = afterUndo && s_restoredPos.contains(cap);
		const bool keepSize = afterUndo && s_restoredSize.contains(cap);
		const double x = keepPos ? cap->xPos() : photo->xPos();
		const double y = keepPos ? cap->yPos() : photo->yPos() + photo->height() + gap;
		const double w = keepSize ? cap->width() : photo->width();
		const bool widthChanged = qAbs(cap->width() - w) >= 0.01;
		const bool geomSame = qAbs(cap->xPos() - x) < 0.01 && qAbs(cap->yPos() - y) < 0.01 && !widthChanged;
		// Fitted to its text where it sits now - after an undo / redo too when this attach moves or widens it: the
		// replayed step fitted it at another place and the fitted height depends on the place (another frame's wrap
		// can push its lines down: undo(2) of [photo taller, story moved] left it 3 mm too tall). Not when its own
		// record restored its size in this step (a layout model's Apply, a re-layout: that size is exact), and not
		// when it already sits in place untouched (a step that only moved it along with its photo, or never touched
		// it - an older file's caption keeps the height it has).
		const bool fit = fitHeight && !keepSize && (!afterUndo || !geomSame) && cap->isTextFrame() && cap->itemText.length() > 0;
		if (afterUndo && geomSame && !fit)
		{
			if (dbg)
				fprintf(stderr, "CAPTION undo  : caption %s kept (restored pos %d size %d)\n", qPrintable(cap->itemName()), keepPos ? 1 : 0, keepSize ? 1 : 0);
			continue;
		}
		const double h0 = cap->height();
		if (!afterUndo || !geomSame)
		{
			cap->setXYPos(x, y);
			cap->setWidth(w);
			cap->ContourLine = cap->PoLine.copy();
			cap->imageClip = cap->PoLine.copy();
			cap->updateClip(false);
		}
		cap->invalidateLayout();
		if (fit)
		{
			// Fitted to its text where it sits: the wraps of the frames above it take part. When the caption lies
			// over another frame whose wrap pushes its lines far down, the fit runs away - then it gets the height
			// its text needs on its own and shows the usual overflow mark (no other story is moved). Decided from
			// the text, the width and the place alone, never from the height it had before (the old rule, "3x the
			// old height", kept a caption made much narrower at its old height - and an undo then fitted it from
			// another old height: a different result for the same place and width).
			PageItem_TextFrame* tf = cap->asTextFrame();
			tf->suneerFitCaptionHeight(false);
			const double fitted = cap->height();
			if (fitted > 20.0)   // at most 20 pt it cannot be more than 3x its own text + 20 pt
			{
				tf->suneerFitCaptionHeight(false, true);
				const double ownText = cap->height();
				const bool runaway = fitted > ownText * 3.0 + 20.0;
				cap->setHeight(runaway ? ownText : fitted);
				cap->updateClip(false);
				cap->invalidateLayout();
				if (dbg)
					fprintf(stderr, "CAPTION fit   : caption %s fitted %.2f mm, its text alone %.2f mm%s\n", qPrintable(cap->itemName()), fitted / 2.8346, ownText / 2.8346,
					        runaway ? " - a wrap pushes it down: own height, overflow mark" : "");
			}
		}
		const bool heightChanged = qAbs(cap->height() - h0) >= 0.01;
		if (afterUndo && geomSame && !heightChanged)
		{
			if (dbg)
				fprintf(stderr, "CAPTION undo  : caption %s kept (restored pos %d size %d), fitted again: same height\n", qPrintable(cap->itemName()), keepPos ? 1 : 0,
				        keepSize ? 1 : 0);
			continue;
		}
		// the weld points follow the new place (never left where the caption was before: an older version left one
		// 5336 pt outside its caption)
		fixCaptionWeldPoints(photo, cap);
		// the caption's change bookkeeping: its move / resize must not be recorded as an undo step of its
		// own later (PageItem::checkChanges from the canvas timer) - the photo's undo re-attaches it
		cap->oldXpos = cap->xPos();
		cap->oldYpos = cap->yPos();
		cap->oldWidth = cap->width();
		cap->oldHeight = cap->height();
		cap->update();
		if (dbg)
			fprintf(stderr, "CAPTION after : photo %s y=%.2f h=%.2f bottom=%.2f gap=%.2f | caption x=%.2f y=%.2f w=%.2f h=%.2f fit=%d%s\n", qPrintable(photo->itemName()),
			        photo->yPos() / 2.8346, photo->height() / 2.8346, (photo->yPos() + photo->height()) / 2.8346, gap / 2.8346,
			        cap->xPos() / 2.8346, cap->yPos() / 2.8346, cap->width() / 2.8346, cap->height() / 2.8346, fit ? 1 : 0,
			        afterUndo ? (heightChanged ? " (undo/redo: fitted again, height changed)" : " (undo/redo)") : "");
		if (!geomSame || heightChanged)
			++n;
	}
	UndoManager::instance()->setUndoEnabled(true);
	s_attaching = false;
	return n;
}

int attachCaptions(ScribusDoc* doc, PageItem* photo, bool fitHeight)
{
	if (s_attaching || !doc || !photo || doc->isLoading())
		return 0;
	if (doc->isUndoRedoOngoing())
	{
		// the step is still being replayed (restoreResize sizes the photo before it moves it back):
		// noted, attached by undoStepDone()
		if (photo->isImageFrame() && !s_undoPhotos.contains(QPointer<PageItem>(photo)))
			s_undoPhotos << photo;
		return 0;
	}
	return attachNow(doc, photo, fitHeight, false);
}

void noteRestored(const PageItem* item, bool position, bool size)
{
	const ScribusDoc* doc = item ? item->doc() : nullptr;
	if (!doc || !doc->isUndoRedoOngoing())
		return;
	if (position)
		s_restoredPos.insert(item);
	if (size)
		s_restoredSize.insert(item);
}

int undoStepDone()
{
	if (s_attaching)
		return 0;
	int n = 0;
	const QList<QPointer<PageItem>> photos = s_undoPhotos;
	s_undoPhotos.clear();
	for (const QPointer<PageItem>& p : photos)
	{
		PageItem* photo = p.data();
		ScribusDoc* doc = photo ? photo->doc() : nullptr;
		if (!doc || doc->isLoading() || !inDocument(doc, photo))
			continue;   // the step removed it (the undo of a placement)
		n += attachNow(doc, photo, true, true);
	}
	s_restoredPos.clear();
	s_restoredSize.clear();
	if (n > 0 && qEnvironmentVariableIsSet("SUNEER_CAPTION_DEBUG"))
		fprintf(stderr, "CAPTION undo  : step done, %d caption(s) attached for %d photo(s)\n", n, int(photos.size()));
	return n;
}

bool sameStory(const PageItem* a, const PageItem* b)
{
	const QString s = storyId(a);
	return !s.isEmpty() && s == storyId(b);
}

bool isCaptionPair(const PageItem* a, const PageItem* b)
{
	if (!a || !b)
		return false;
	auto captionOf = [](const PageItem* photo, const PageItem* cap) {
		return photo->isImageFrame() && cap->isTextFrame() && attrOf(cap, QStringLiteral("news.role")) == QLatin1String("caption");
	};
	return captionOf(a, b) || captionOf(b, a);
}

QList<PageItem*> cluster(PageItem* item)
{
	QList<PageItem*> out;
	if (!item)
		return out;
	QSet<PageItem*> seen;
	QList<PageItem*> todo { item };
	while (!todo.isEmpty())
	{
		PageItem* it = todo.takeFirst();
		if (!it || seen.contains(it))
			continue;
		seen.insert(it);
		out << it;
		for (const PageItem::WeldingInfo& w : it->weldList)
			if (w.weldItem && !seen.contains(w.weldItem))
				todo << w.weldItem;
	}
	return out;
}

int weldStory(ScribusDoc* doc, const QList<PageItem*>& itemsIn, PageItem* anchor)
{
	QList<PageItem*> items;
	for (PageItem* it : itemsIn)
		if (it && !it->isGroup() && !items.contains(it))
			items << it;
	if (items.size() < 2)
		return 0;
	if (!anchor || !items.contains(anchor))
	{
		anchor = nullptr;
		for (PageItem* it : items)
		{
			const QString role = attrOf(it, QStringLiteral("news.role"));
			if (it->isTextFrame() && (role == QLatin1String("text") || role == QLatin1String("body")))
			{
				anchor = it;
				break;
			}
		}
		if (!anchor)
			for (PageItem* it : items)
				if (it->isTextFrame()) { anchor = it; break; }
		if (!anchor)
			anchor = items.first();
	}
	UndoTransaction tx;
	if (doc && UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, Um::WeldItems, QString(), Um::IGroup);
	int made = 0;
	for (PageItem* it : items)
	{
		if (it == anchor)
			continue;
		// already connected (directly or through another frame): a second path would be a cycle
		if (cluster(anchor).contains(it))
			continue;
		it->weldTo(anchor);
		++made;
	}
	if (tx)
		tx.commit();
	return made;
}

int unweldStory(const QList<PageItem*>& items)
{
	int n = 0;
	for (PageItem* it : items)
		if (it && it->isWelded())
		{
			it->unWeld();
			++n;
		}
	return n;
}

} // namespace SuneerWeld
