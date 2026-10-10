/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERBGREMOVE_H
#define SUNEERBGREMOVE_H

#include <QImage>
#include <QObject>
#include <QPainterPath>
#include <QProcess>
#include <QRect>
#include <QString>
#include <QTemporaryDir>

#include "scribusapi.h"

class PageItem;

/*!
 \brief Background removal (rembg) on the whole picture or on a selected part.

 The source file is never touched: the result is a new PNG next to the
 existing "_rembg" output and the frame is pointed at it. What was used to
 make it (source file, mode, selection outline, brush mask, the raw cut-out)
 is kept in the frame's item attributes, so the dialog can reopen the same
 state and the frame can be put back to the original.

 Modes:
  - Whole: the picture as the BG button always did it;
  - Inside: rembg runs on the selection's bounds plus a margin and its alpha
    is used inside the selection only, pixels outside stay as they were;
  - KeepSubject: everything outside the selection turns transparent and the
    background inside it is removed (one person out of a group).
 The brush mask refines the result afterwards: 0 = erased, 255 = restored
 from the source, 128 = untouched.
 */
namespace SuneerBgRemove
{
	enum class Mode { Whole, Inside, KeepSubject };

	// Item attribute names.
	extern const char* const AttrSource;     //!< picture before the first removal
	extern const char* const AttrMode;       //!< "whole" / "inside" / "keep"
	extern const char* const AttrSelection;  //!< outline, image pixels: "x,y x,y ...;x,y ..."
	extern const char* const AttrBrush;      //!< brush mask, ScEraserMask::encode() form
	extern const char* const AttrCut;        //!< raw rembg output PNG (region sized)
	extern const char* const AttrCutRect;    //!< "x,y,w,h" of that region in the source
	extern const char* const AttrModel;      //!< rembg model name
	extern const char* const AttrFeather;    //!< edge feather px given to rembg

	SCRIBUS_API QString modeName(Mode m);
	SCRIBUS_API Mode modeFromName(const QString& s);

	//! Selection outline <-> attribute string (polygons in image pixels).
	SCRIBUS_API QString encodePath(const QPainterPath& path);
	SCRIBUS_API QPainterPath decodePath(const QString& s);
	//! Alpha8 coverage mask of \a path over an image of \a size (null path -> null mask).
	SCRIBUS_API QImage maskFromPath(const QPainterPath& path, const QSize& size);

	//! Region rembg should see: the selection bounds plus a margin, or the
	//! whole image when \a selMask is null / the mode is Whole.
	SCRIBUS_API QRect regionFor(const QImage& selMask, Mode mode, const QSize& imageSize);

	/*! \brief Put the pieces together.
	    \param source   the full picture (ARGB32)
	    \param cut      rembg's RGBA output for \a region (same size as region)
	    \param selMask  Alpha8 selection (null = whole image)
	    \param brush    Grayscale8 brush mask (null = none) */
	SCRIBUS_API QImage compose(const QImage& source, const QImage& cut, const QRect& region,
	                           const QImage& selMask, Mode mode, const QImage& brush);

	//! Item attribute access.
	SCRIBUS_API QString attr(const PageItem* item, const char* name);
	SCRIBUS_API void setAttr(PageItem* item, const char* name, const QString& value);
	SCRIBUS_API void clearAttrs(PageItem* item);

	/*! \brief Outline of \a shapes (closed Scribus shapes on the canvas) in the
	    pixel space of \a frame's full-resolution image. Shapes that do not
	    overlap the frame give an empty path. */
	SCRIBUS_API QPainterPath shapesToImagePath(const PageItem* frame, const QList<PageItem*>& shapes);
}

/*!
 \brief Runs rembg on one image in a child process, without blocking the UI.
 */
class SCRIBUS_API SuneerBgRemover : public QObject
{
	Q_OBJECT
public:
	explicit SuneerBgRemover(QObject* parent = nullptr);
	~SuneerBgRemover() override;

	//! Start on \a region of \a source. Emits finished() or failed().
	void start(const QImage& source, const QRect& region, const QString& model, int featherPx);
	void cancel();
	bool isRunning() const;
	QRect region() const { return m_region; }

signals:
	void finished(const QImage& cut);
	void failed(const QString& error);

private:
	void onProcessDone(int code, QProcess::ExitStatus status);

	QProcess* m_proc { nullptr };
	QTemporaryDir m_dir;
	QRect m_region;
	QString m_inPath, m_outPath;
	bool m_cancelled { false };
};

#endif
