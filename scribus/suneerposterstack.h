#ifndef SUNEERPOSTERSTACK_H
#define SUNEERPOSTERSTACK_H

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

class PageItem;
class ScribusDoc;
class ScribusMainWindow;

/*! Item > Poster Stack...
 *
 * A typographic poster: the letters of each line are stretched to the full
 * width of the frame, the lines together fill its full height, and one image
 * shows through all the letters. Built on Fill Text with Image: the result is
 * the same letter-shaped image frame (the image stays movable and zoomable in
 * it, Text Effects work on it), and the original text frame is parked on the
 * hidden "Original text" layer with the settings stored on it.
 */
namespace SuneerPosterStack
{
	enum AutoSplit { Manual = 0, CharsPerLine = 1, Balanced = 2 };
	enum FillMode { Stretch = 0, KeepProportions = 1 };
	enum RowHeights { Equal = 0, ByCharacters = 1 };

	struct Settings
	{
		QString text;                   //!< lines separated by '\n'
		int     autoSplit { Manual };
		int     charsPerLine { 3 };     //!< grapheme clusters per line (CharsPerLine)
		int     lines { 2 };            //!< number of lines (Balanced)
		QString font;                   //!< Scribus font name; empty = the frame's font
		double  letterGap { 0.0 };      //!< points, in the result
		double  lineGap { 4.0 };        //!< points
		double  margin { 0.0 };         //!< points, all round inside the frame
		int     fillMode { Stretch };
		int     rowHeights { Equal };
		QString imageFile;
		QString backColor { "None" };   //!< colour of the frame area outside the letters

		QString toString() const;
		static Settings fromString(const QString& text);
	};

	//! Item attribute on the parked text frame that holds the settings.
	QString attributeName();

	//! Splits at grapheme cluster boundaries only; a conjunct, a chillu or a vowel sign never leaves its base.
	QStringList graphemeClusters(const QString& text);

	//! The lines the poster will have, after auto split; blank lines dropped.
	QStringList linesFor(const Settings& settings);

	//! The combined letter shape in the frame's own coordinates, for a frame of \a frameSize.
	QPainterPath buildPath(ScribusDoc* doc, const Settings& settings, const QSizeF& frameSize, QString* error);

	//! The text frame a Poster Stack dialog would edit for \a item (the item itself or its parked text), or nullptr.
	PageItem* textFrameFor(ScribusDoc* doc, PageItem* item);

	bool canRunOn(const PageItem* item);
	void runForSelection(ScribusMainWindow* mw);

	//! Replaces the poster made from \a textFrame. One undo step. \return the image frame, or nullptr with \a error.
	PageItem* apply(ScribusMainWindow* mw, PageItem* textFrame, const Settings& settings, QString* error);
}

#endif
