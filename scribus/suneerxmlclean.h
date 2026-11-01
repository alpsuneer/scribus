/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERXMLCLEAN_H
#define SUNEERXMLCLEAN_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "scribusapi.h"

class QIODevice;

/*! suneer 2026-11-01: characters XML 1.0 does not allow (U+0000-U+0008, U+000B, U+000C, U+000E-U+001F,
    U+FFFE, U+FFFF), raw or as a numeric reference, stop the whole document from loading.
    Trap: inside a live StoryText several of these are Scribus' own markers (SpecialChars::OBJECT = U+0019,
    COLBREAK, FRAMEBREAK, PAGENUMBER, PAGECOUNT). The save code writes those as elements, so the XML never
    holds them raw: clean a file or text from outside, never a story of an open document. */
namespace SuneerXmlClean
{
	struct Hit
	{
		int line { 0 };
		int column { 0 };
		uint code { 0 };
		QString story;      //!< the text just before the character
	};

	SCRIBUS_API bool isInvalid(uint code);
	//! Removes the characters from outside text (news, clipboard); tab, LF and CR stay.
	SCRIBUS_API QString stripInvalid(const QString& text);
	//! Removes them from an SLA in memory; line / column / story are those of the file as it was.
	SCRIBUS_API QList<Hit> clean(QByteArray& xml);
	//! "Removed 1 invalid character (story '...', line 488) - please save the page"
	SCRIBUS_API QString message(const QList<Hit>& hits, bool askToSave = true);
	//! The loaders' slaReader(): takes \a in, returns a device over the cleaned bytes, remembers what was removed.
	SCRIBUS_API QIODevice* cleanReader(QIODevice* in, const QString& fileName);
	//! What cleanReader() removed from \a fileName; forgets it.
	SCRIBUS_API QList<Hit> takeReport(const QString& fileName);
}

#endif
