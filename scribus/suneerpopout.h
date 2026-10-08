#ifndef SUNEERPOPOUT_H
#define SUNEERPOPOUT_H

#include <QPainterPath>
#include <QRectF>
#include <QString>

class PageItem;
class ScribusDoc;
class ScribusMainWindow;

/*! Item > Pop-out Subject...
 *
 * On a Fill Text with Image / Poster Stack result ("face"): part of the person
 * in the picture, usually the head, comes out in front of the letters while
 * the rest stays inside them. The person is found with the same rembg
 * background removal the image tools use; its mask becomes a vector clipping
 * path (no alpha, no soft mask in the PDF). A second image frame with the same
 * picture, scale and offset is clipped by (subject AND chosen region) and laid
 * on top. Moving or zooming the picture inside the letters moves the pop-out
 * with it (PageItem::checkChanges() calls itemChanged()).
 */
namespace SuneerPopOut
{
	enum Region { HeadOnly = 0, AboveLine = 1, Custom = 2 };
	enum Shape { Rectangle = 0, Ellipse = 1 };

	struct Settings
	{
		int     region { HeadOnly };
		double  lineY { -1.0 };          //!< AboveLine: image pixel row; < 0 = not set yet
		int     shape { Rectangle };
		QRectF  custom;                  //!< Custom: image pixel rectangle
		bool    shadow { false };
		bool    group { false };
		QString maskFile;                //!< cached rembg mask (alpha as grey PNG), or a brush-edited copy
		QString model { "u2net_human_seg" };
		//! From the dialog: the picture placement the preview showed (applied to the face too). Not stored.
		bool    setImage { false };
		double  imageScale { 1.0 };
		double  imageOffX { 0.0 };
		double  imageOffY { 0.0 };

		QString toString() const;
		static Settings fromString(const QString& text);
	};

	QString attributeName();          //!< on the face: the settings
	QString ofAttributeName();        //!< on the pop-out frame: the face's name
	QString pathAttributeName();      //!< on the pop-out frame: the clip polygon in image pixels

	//! \return true when the dialog can be opened for \a item (an image frame with a picture).
	bool canRunOn(const PageItem* item);
	void runForSelection(ScribusMainWindow* mw);
	//! Opens the dialog for \a face (an image frame).
	void runOn(ScribusMainWindow* mw, PageItem* face);

	//! Replaces the pop-out of \a face. One undo step.
	PageItem* apply(ScribusMainWindow* mw, PageItem* face, const Settings& settings, QString* error);

	//! Called by PageItem::checkChanges(): keeps a pop-out in step with its face.
	void itemChanged(PageItem* item);
}

#endif
