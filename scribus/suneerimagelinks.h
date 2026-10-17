/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERIMAGELINKS_H
#define SUNEERIMAGELINKS_H

#include <functional>

#include <QImage>
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

	/*! Item attribute written when a picture is embedded: the file it was
	    linked to (absolute while loaded, re-based like the other photo-folder
	    paths). "Unembed / Extract" takes its file name from it. */
	extern const char* const AttrEmbeddedFrom;

	//! Embed one linked image. Undoable. False when it is not a linked image
	//! or the file cannot be copied. \a bytes gets the size of the file.
	SCRIBUS_API bool embedItem(ScribusDoc* doc, PageItem* item, qint64* bytes = nullptr);

	/*! Embed the linked images of collect(doc, true) - the ones the "linked
	    images" check before Save / PDF / Print lists - as ONE undo step.
	    Photos in "<doc>_photos/" are not among them. Returns the number
	    embedded; \a failed gets "page / frame: path" for each one that could not be. */
	SCRIBUS_API int embedAll(ScribusDoc* doc, QStringList* failed = nullptr);

	//! Every image frame of the document: pages, pasteboard, master pages,
	//! inside groups (nested), inline frames and pattern items.
	SCRIBUS_API QList<PageItem*> imageFrames(ScribusDoc* doc);
	//! The image frames among \a items, group members included.
	SCRIBUS_API QList<PageItem*> imageFramesIn(const QList<PageItem*>& items);
	//! "3", "Master: Normal", "pasteboard", "in text" or "pattern".
	SCRIBUS_API QString placeOf(ScribusDoc* doc, const PageItem* item);
	//! What kind of picture it is: "News, edited, BG removed, obit, group 'x'" ...
	SCRIBUS_API QString kindOf(ScribusDoc* doc, const PageItem* item);

	struct EmbedReport
	{
		int total { 0 };        //!< image frames looked at (empty frames not counted)
		int embedded { 0 };     //!< embedded now
		int already { 0 };      //!< were embedded before
		int repaired { 0 };     //!< embedded, but the temp copy was gone: copied again
		qint64 bytes { 0 };     //!< size of the files embedded now
		QStringList skipped;    //!< "frame (page N): reason"
	};
	/*! The explicit "Embed All" command: embed EVERY image of \a items, linked
	    files in the document's photos folder included (News Browser originals,
	    Edit photo / BG removal results, obituary photos). The file embedded is
	    the one the frame shows. Missing files are skipped and listed. One undo
	    step. \a progress is called with (done, total) after each frame. */
	SCRIBUS_API EmbedReport embedItems(ScribusDoc* doc, const QList<PageItem*>& items,
	                                   const std::function<void(int, int)>& progress = {});

	struct ExtractReport
	{
		int extracted { 0 };
		QString folder;
		QStringList skipped;    //!< "frame (page N): reason"
	};
	/*! "Unembed / Extract": write each embedded picture of \a items to
	    "<doc>_photos/edited/" (file name from AttrEmbeddedFrom, else the frame
	    name) and link the frame to it. Needs a saved document. One undo step. */
	SCRIBUS_API ExtractReport extractItems(ScribusDoc* doc, const QList<PageItem*>& items,
	                                       const std::function<void(int, int)>& progress = {});

	//! One line per image frame: place, name, status, kind, file. For the self-test and reports.
	SCRIBUS_API QStringList describeAll(ScribusDoc* doc);

	//! Called right after an image was placed by the user: embeds it when the
	//! "Always embed placed images" preference is on.
	SCRIBUS_API void embedPlaced(ScribusDoc* doc, PageItem* item);

	// Application preferences (prefs context "suneer_images"), both on by default.
	SCRIBUS_API bool badgesShown();
	SCRIBUS_API void setBadgesShown(bool on);
	SCRIBUS_API bool alwaysEmbed();
	SCRIBUS_API void setAlwaysEmbed(bool on);

	//! Canvas badge size, Preferences > Item Tools. Default Large.
	enum BadgeSize { BadgeSmall = 0, BadgeMedium = 1, BadgeLarge = 2 };
	SCRIBUS_API int badgeSize();
	SCRIBUS_API void setBadgeSize(int size);
	//! Badge height in screen pixels for the current size (20 / 24 / 28).
	SCRIBUS_API int badgePixels();
	/*! The badge as a picture, one image pixel per screen pixel: an orange
	    "LINK" or red "MISSING" tab with a white outline. Without \a withLabel
	    it is a square of the same height (red gets a white cross), for frames
	    too narrow to hold the word. Null for any other status. */
	SCRIBUS_API QImage badgeImage(Status status, bool withLabel);
}

#endif
