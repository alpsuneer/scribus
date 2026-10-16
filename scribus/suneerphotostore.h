/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPHOTOSTORE_H
#define SUNEERPHOTOSTORE_H

#include <QList>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

class PageItem;
class ScribusDoc;

/*!
 \brief Where the News Browser's photos live: next to the document.

   <sla folder>/<sla name>_photos/original/   files exactly as downloaded (never modified)
   <sla folder>/<sla name>_photos/edited/     every processed / derived file

 File names in original/: <refId or story id>_<media index>.<ext>, safe
 characters only. Frames link to these files (relative paths in the .sla,
 never embedded). An unsaved document keeps using the per-user cache; the
 first Save / Save As adopts everything into the folder. The older
 "<name>_images/" output is migrated into edited/ on save.
 */
namespace SuneerPhotoStore
{
	extern const char* const AttrFileUrl;     //!< news.fileUrl: the Workflow URL the picture came from
	extern const char* const AttrOriginal;    //!< news.original: the file in original/ (absolute while loaded)
	extern const char* const AttrMediaIndex;  //!< news.index (already set by the placement)

	//! "<dir>/<base>_photos" for a saved document (or for \a slaPath), empty when unsaved.
	SCRIBUS_API QString folder(const ScribusDoc* doc);
	SCRIBUS_API QString folderForSla(const QString& slaPath);
	SCRIBUS_API QString originalDir(const ScribusDoc* doc, bool create = true);
	SCRIBUS_API QString editedDir(const ScribusDoc* doc, bool create = true);
	SCRIBUS_API QString cacheDir();
	//! True for a frame the News Browser placed (news.story set) that holds a picture.
	SCRIBUS_API bool isNewsPhoto(const PageItem* item);
	//! refId, else serverId, else story id - the key of the file name.
	SCRIBUS_API QString storyKey(const PageItem* item);
	//! "<key>_<idx>.<ext>" with only [A-Za-z0-9._-].
	SCRIBUS_API QString safeName(const QString& key, int mediaIndex, const QString& ext);
	//! Where a derived file of \a sourcePath goes: edited/<base><tag>[N].<ext> (saved doc), else next to the source.
	SCRIBUS_API QString editedPathFor(const ScribusDoc* doc, const QString& sourcePath, const QString& tag, const QString& ext = QStringLiteral("png"));
	//! A path some tool chose next to its source: moved into edited/ when the document has a folder.
	SCRIBUS_API QString redirectDerived(const ScribusDoc* doc, const QString& candidate);
	//! True when \a path is inside the document's photos folder.
	SCRIBUS_API bool isInStore(const ScribusDoc* doc, const QString& path);

	/*! Copy a downloaded file into original/ as <key>_<idx>.<ext>, written
	    to a temporary name and renamed when complete (the folder may be a
	    network share). Returns the new path, or \a cachePath when the
	    document is unsaved / the copy failed (\a why says so). An existing
	    identical file is kept. */
	SCRIBUS_API QString importOriginal(const ScribusDoc* doc, const QString& cachePath, const QString& key, int mediaIndex, QString* why = nullptr);

	/*! Before a save to \a slaPath: Save As to another name/folder copies the
	    whole folder and relinks; "<base>_images/" is merged into edited/;
	    News photos still in the cache (unsaved document until now) are copied
	    into original/ and the frames relinked. Returns a short report. */
	SCRIBUS_API QString prepareForSave(ScribusDoc* doc, const QString& slaPath);

	//! Every file path the document refers to inside the folder (Pfile and the tool attributes).
	SCRIBUS_API QStringList referencedFiles(const ScribusDoc* doc);
	//! Files in the folder no frame refers to (candidates for "Remove unused photos").
	SCRIBUS_API QStringList unusedFiles(const ScribusDoc* doc);
	//! News photo frames whose linked file is missing (for "Re-download missing photos").
	SCRIBUS_API QList<PageItem*> missingNewsPhotos(const ScribusDoc* doc);
	/*! After a load: the whole folder may have been moved (or opened from
	    another PC). Pfile is relative in the .sla, but the tool attributes
	    carry absolute paths: every one that points into some "..._photos/"
	    folder is re-based onto this document's folder. Returns the count changed. */
	SCRIBUS_API int relocateAttributes(ScribusDoc* doc);
	//! Rewrite every path attribute (and Pfile) of \a item that starts with \a from.
	SCRIBUS_API void rewritePaths(PageItem* item, const QString& from, const QString& to);
	//! The attribute names that carry file paths.
	SCRIBUS_API QStringList pathAttributes();
}

#endif
