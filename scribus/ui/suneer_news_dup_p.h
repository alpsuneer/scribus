/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/*
 * Suneer News Browser: what suneer_news_dup.cpp (duplicate news / photos) and
 * suneer_news_repeat.cpp (Check repetition on page) share. Private to those two.
 */
#ifndef SUNEER_NEWS_DUP_P_H
#define SUNEER_NEWS_DUP_P_H

#include <QList>
#include <QString>

#include "suneerduplicatenews.h"

// one story (or a lone photo) to compare: from the News Browser list or from the document
struct SuneerDupItem
{
	QString sid;        // workflow story id (empty for a frame that is not a placed story)
	QString title;
	QString body;
	QString photo;      // file whose hash stands for its photo (the original for an edited one)
	QString module;     // the story's name in the document / the list headline
	bool onPage { false };
	bool usedHere { false };  // list story the workflow has as USED on this page: an id-less frame with its text is its own placement
	int listIndex { -1 };   // index in m_newsData for list stories
};

struct SuneerDupPair
{
	int a { -1 };
	int b { -1 };
	QString kind;       // "story" (one story placed again), "news" (another story, text), "photo" (another story)
	int score { 0 };
};

namespace SuneerDupScan
{
	//! Every duplicate pair of \a items (the same id, near-same text, the same photo). Thread-safe.
	QList<SuneerDupPair> scanPairs(const QList<SuneerDupItem>& items, int threshold, int photoBits);
	//! Settings > Layout "Duplicate news at" (%), default 85.
	int threshold();
}

#endif
