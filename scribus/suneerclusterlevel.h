#ifndef SUNEERCLUSTERLEVEL_H
#define SUNEERCLUSTERLEVEL_H

#include <QVariant>

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

#endif
