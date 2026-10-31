/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPHOTOHASH_H
#define SUNEERPHOTOHASH_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "scribusapi.h"

class PageItem;
class ScribusDoc;
class QImage;

/*!
 \brief Suneer: perceptual hashes of photos, to find the same photo twice.

 A pHash (DCT of a 32x32 grey picture, the 8x8 lowest frequencies against their median) of the
 whole picture and of its central 90 / 85 / 80 %. Measured on 28 real obituary headshots: the same
 photo resized or recompressed 0-2 bits, cropped up to 10 % per side 0-4 bits; two different people
 8 bits at the closest (headshots look alike), 24 typical. Hence the default "same" = 6 bits.
 A real page (2026-10-31) had two different women 6 bits apart, so a second test decides: a
 16x16 grey thumbnail per crop, brightness / contrast normalised, correlated - the same photo
 >= 0.978 (55 %, 4 % crop, JPEG 50 the worst), different people <= 0.933 over 379 pairs; same
 means also >= 0.95. Pure Qt GUI-free image code - safe
 on a worker thread. Results are cached per file (path + size + time) in memory and in
 hashes.json beside the document's photos (or the user cache for an unsaved one).
 */
namespace SuneerPhotoHash
{
	struct Hash
	{
		quint64 h[4] { 0, 0, 0, 0 };   //!< the whole picture, its central 90 %, 85 %, 80 %
		qint8 t[4][256] {};            //!< the same four as 16x16 normalised grey thumbnails
		bool ok { false };
		bool thumbs { false };         //!< t is filled (cache entries from before 2026-10-31: no)
	};

	//! The hash of one picture (no cache).
	SCRIBUS_API Hash ofImage(const QImage& img);
	//! The hash of a file, from the cache when the file is unchanged (thread-safe).
	SCRIBUS_API Hash ofFile(const QString& path);
	//! Smallest Hamming distance between the two pictures (whole / centre crops), 0..64.
	SCRIBUS_API int distance(const Hash& a, const Hash& b);
	//! The same photo: within \a maxBits and (when both have thumbnails) thumbnails correlating >= 0.95.
	SCRIBUS_API bool samePhoto(const Hash& a, const Hash& b, int maxBits, int* bits = nullptr);
	//! Settings > Layout "Photo match": distance at or below which two photos are the same (default 6).
	SCRIBUS_API int maxDistance();
	//! Similarity in percent for the user: 100 at distance 0, 0 at 32 (unrelated photos).
	SCRIBUS_API int percent(int distance);

	/*! The file whose hash stands for the frame's photo: the ORIGINAL when the frame was edited or
	    processed (a background-removed obituary photo maps to its original), else what it shows
	    (an embedded picture's temp copy, or the file it was embedded from). */
	SCRIBUS_API QString sourceFileOf(const PageItem* item);

	//! Where hashes.json lives for \a doc (the document's _photos folder, or the user cache).
	SCRIBUS_API QString cacheFileFor(const ScribusDoc* doc);
	//! Read / write the on-disk cache (merged into the memory cache).
	SCRIBUS_API void loadCache(const QString& jsonPath);
	SCRIBUS_API void saveCache(const QString& jsonPath);
}

#endif
