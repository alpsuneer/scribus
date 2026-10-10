#ifndef SUNEERCLUSTERLEVEL_H
#define SUNEERCLUSTERLEVEL_H

#include <QVariant>

#include "pageitem.h"
#include "scribusdoc.h"

// How HarfBuzz groups characters into glyph clusters, per document.
//
// Stock Scribus (1.5.x, 1.6.x and upstream 1.7.x) shapes with one cluster per
// character where the font allows it. This build shapes with one cluster per
// grapheme, so a Malayalam syllable is a single unit. Layout only breaks a
// line at a cluster end and never right after the first cluster of a word, so
// the same story breaks in different places: a page fitted in an older Scribus
// loses the break after a word's first syllable, runs longer and overflows.
//
// A document that came from an older Scribus therefore keeps character
// clusters, and says so in its file once it is saved here
// (Document SuneerClusterLevel="chars"). The same documents also get the stock
// quarter-point line-end search in PageItem_TextFrame's LineControl instead of
// this build's coarse step. Pages made in this build are left exactly as they
// were laid out. Kept as a dynamic property: a new ScribusDoc member would
// change a class the plugins share.

inline bool suneerDocUsesCharClusters(const ScribusDoc* doc)
{
	return doc && doc->property("suneerCharClusters").toBool();
}

inline void suneerSetDocUsesCharClusters(ScribusDoc* doc, bool on)
{
	if (doc)
		doc->setProperty("suneerCharClusters", on ? QVariant(true) : QVariant());
}

// The same behaviour carried by ONE frame: a text frame copied out of a
// document that breaks lines the old way is marked with the item attribute
// SuneerClusterLevel=chars (written by saveElements, so the clipboard, a
// second window and the Scrapbook all carry it), and keeps its line breaks
// wherever it is pasted, with no dependence on the target document's flag.
inline bool suneerItemMarkedCharClusters(const PageItem* item)
{
	if (!item)
		return false;
	const QList<ObjectAttribute> attrs = item->getObjectAttributes(QStringLiteral("SuneerClusterLevel"));
	for (const ObjectAttribute& attr : attrs)
		if (attr.value == QLatin1String("chars"))
			return true;
	return false;
}

inline bool suneerItemUsesCharClusters(const PageItem* item)
{
	if (!item)
		return false;
	return suneerDocUsesCharClusters(item->doc()) || suneerItemMarkedCharClusters(item);
}

inline void suneerMarkItemCharClusters(PageItem* item, bool on)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = attrs.count() - 1; i >= 0; --i)
		if (attrs.at(i).name == QLatin1String("SuneerClusterLevel"))
			attrs.removeAt(i);
	if (on)
	{
		ObjectAttribute attr;
		attr.name = QStringLiteral("SuneerClusterLevel");
		attr.type = QStringLiteral("string");
		attr.value = QStringLiteral("chars");
		attrs.append(attr);
	}
	item->setObjectAttributes(&attrs);
}

#endif
