#ifndef SUNEERFILLTEXTIMAGE_H
#define SUNEERFILLTEXTIMAGE_H

#include <QPainterPath>
#include <QString>

class PageItem;
class ScribusMainWindow;

/*! Item > Fill Text with Image...
 *
 * Turns a text frame into ONE image frame whose shape is the outline of the
 * text, as it is laid out and shaped (so Malayalam conjuncts and vowel signs
 * are the glyphs the page shows, not the characters typed), and loads an image
 * into it, scaled to cover the letters and centred. The text frame itself is
 * kept, on a hidden, non-printing layer, so the headline can be rebuilt after
 * a text change. Everything is one undo step.
 */
namespace SuneerFillTextImage
{
	struct Options
	{
		QString imageFile;
		bool    outline { false };          //!< thin stroke around the letters
		double  outlineWidth { 0.5 };       //!< points
		QString outlineColor { "Black" };
		bool    shadow { false };           //!< soft drop shadow behind the letters
	};

	//! Name of the layer the original text frames are parked on.
	QString originalTextLayerName();

	//! The outline of the text as the frame lays it out and shapes it, in the frame's own
	//! coordinates, all glyphs united into one winding path. Empty when nothing has an outline.
	QPainterPath lettersPath(PageItem* textFrame);

	//! Asks for the image and the options, then applies them to the selected text frame.
	void runForSelection(ScribusMainWindow* mw);

	//! \return the new image frame, or nullptr with \a error set.
	PageItem* apply(ScribusMainWindow* mw, PageItem* textFrame, const Options& options, QString* error);
}

#endif
