/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERIMAGELINKS_H
#define SUNEERIMAGELINKS_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

class PageItem;
class ScribusDoc;

/*!
 * \brief One place that answers "is this picture inside the .sla, or only a
 * path to a file somewhere?" and that embeds pictures.
 *
 * Embedded = PageItem::isImageInline(): the saver writes the picture as Base64
 * ImageData. Linked = the .sla only carries the path (PFILE). Missing = linked
 * and the file could not be loaded.
 *
 * Used by the canvas badges, Extras > Embed All Images, the check before
 * Save / PDF / Print / Proof Print, the Preflight Verifier and the "Always
 * embed placed images" preference, so all five agree on what "linked" means.
 */
namespace SuneerImageLinks
{
	enum Status { NotAnImage, Empty, Embedded, Linked, Missing };

	struct Entry
	{
		QPointer<PageItem> item;
		QString page;       //!< "3", "Master: Normal", or "-" when not on a page
		QString frameName;
		QString path;
		Status status { NotAnImage };
	};

	//! Cheap: flags only, no file system access (it is called while painting).
	SCRIBUS_API Status statusOf(const PageItem* item);
	SCRIBUS_API QString statusText(Status s);

	//! Every image frame of the document (pages, master pages, inside groups,
	//! inline frames). With \a onlyProblems only Linked and Missing ones.
	//! Unlike statusOf() this also looks at the disk, so a file deleted after
	//! it was loaded is reported as Missing.
	SCRIBUS_API QList<Entry> collect(ScribusDoc* doc, bool onlyProblems = true);

	//! Embed one linked image. Undoable. False when it is not a linked image
	//! or the file cannot be copied.
	SCRIBUS_API bool embedItem(ScribusDoc* doc, PageItem* item);

	//! Embed every linked image as ONE undo step. Returns the number embedded;
	//! \a failed gets "page / frame: path" for each one that could not be.
	SCRIBUS_API int embedAll(ScribusDoc* doc, QStringList* failed = nullptr);

	//! Called right after an image was placed by the user: embeds it when the
	//! "Always embed placed images" preference is on.
	SCRIBUS_API void embedPlaced(ScribusDoc* doc, PageItem* item);

	// Application preferences (prefs context "suneer_images"), both on by default.
	SCRIBUS_API bool badgesShown();
	SCRIBUS_API void setBadgesShown(bool on);
	SCRIBUS_API bool alwaysEmbed();
	SCRIBUS_API void setAlwaysEmbed(bool on);
}

#endif
