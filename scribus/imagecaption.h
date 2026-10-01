/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMAGECAPTION_H
#define IMAGECAPTION_H

#include <QString>
#include <QStringList>

/*!
 \brief Read the caption embedded in (or beside) an image file.

 One place for "what is this picture's caption", used by Ctrl+I place-image
 and available to anything else. Reads the file itself - no exiftool or
 other external program - so it behaves the same on every machine.

 Sources, in the order tried; the first non-empty text that is not a junk
 default (see isJunk()) wins:

   a. XMP dc:description            (embedded XMP packet, APP1 in JPEG, iTXt
                                     XML:com.adobe.xmp in PNG, tag 700 in TIFF)
   b. IPTC 2:120 Caption-Abstract   (APP13 Photoshop IRB in JPEG, tag 33723
                                     in TIFF; charset from IPTC 1:90, else
                                     UTF-8 when valid, else Latin-1)
   c. EXIF ImageDescription         (tag 0x010E)
   d. EXIF UserComment              (tag 0x9286, 8-byte charset header:
                                     ASCII, UNICODE (UCS-2, EXIF byte order),
                                     JIS, or undefined/all-zero = UTF-8 as
                                     gThumb writes it)
   e. Windows XPTitle / XPComment   (tags 0x9C9B / 0x9C9C, UCS-2 LE)
   f. JPEG COM marker               (GIMP: Image Properties > Comment)
   g. PNG tEXt/iTXt/zTXt            (keywords Description, Comment, Title;
                                     iTXt is UTF-8, tEXt/zTXt Latin-1)
   h. Sidecar files                 (<name>.xmp, <name>.<ext>.xmp next to the
                                     image, and gThumb's .comments/<name>.xml
                                     - <note> then <caption>)

 Text is trimmed of surrounding whitespace and trailing newlines.
 */
namespace ImageCaption
{
	struct Result
	{
		QString text;    //!< the caption, trimmed; empty when nothing usable was found
		QString source;  //!< where it came from, e.g. "XMP dc:description", "IPTC 2:120", "sidecar .comments/x.xml"
		bool found() const { return !text.isEmpty(); }
	};

	//! Read the caption of \a imagePath. Never throws; a missing or unreadable
	//! file gives an empty Result.
	Result read(const QString& imagePath);

	//! Every candidate found, in source order, junk included - for diagnostics.
	QList<Result> readAll(const QString& imagePath);

	//! True for text that is empty after trimming or matches a known camera /
	//! editor default such as "Created with GIMP" (case-insensitive).
	bool isJunk(const QString& text);

	//! The junk defaults (whole-text match) and junk prefixes (starts-with),
	//! one place to extend.
	QStringList junkDefaults();
	QStringList junkPrefixes();
}

#endif // IMAGECAPTION_H
