/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
                          pageitem.h  -  description
                             -------------------
    copyright            : Scribus Team
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PAGEITEM_IMAGEFRAME_H
#define PAGEITEM_IMAGEFRAME_H

#include <QImage>
#include <QString>
#include <QRectF>
#include <QKeyEvent>
#include <QTransform>

#include "scribusapi.h"
#include "pageitem.h"
#include "util_contour.h"
class ScPainter;
class ScribusDoc;

class SCRIBUS_API PageItem_ImageFrame : public PageItem
{
	Q_OBJECT

public:
	PageItem_ImageFrame(ScribusDoc *pa, double x, double y, double w, double h, double w2, const QString& fill, const QString& outline);
	PageItem_ImageFrame(const PageItem & p) : PageItem(p) {}
	~PageItem_ImageFrame();

	PageItem_ImageFrame * asImageFrame() override { return this; }
	const PageItem_ImageFrame * asImageFrame() const override { return this; }
	bool isImageFrame() const override { return true; }

	void handleModeEditKey(QKeyEvent *k, bool& keyRepeat) override;
	void clearContents() override;
	
	bool createInfoGroup(QFrame *, QGridLayout *) override;
	void applicableActions(QStringList& actionList) override;
	QString infoDescription() const override;

	//! \name Non-destructive eraser mask
	//! The stored mask lives in effectsInUse as an ImageEffect::EF_ERASERMASK
	//! entry, which is what gives it .sla persistence, copy/paste and undo for
	//! free. These accessors deal in the decoded greyscale image; see
	//! scimageerasermask.h for the storage format.
	//@{
	//! Mask currently in force: the live stroke preview when one is running,
	//! otherwise the one decoded from effectsInUse. Null when nothing is erased.
	QImage eraserMask() const;
	//! Replace the stored mask. An empty mask removes the effect entry.
	void setEraserMask(const QImage& mask);
	//! True when this frame has anything erased.
	bool hasEraserMask() const;

	/*! \brief Show \a mask on canvas without storing it.
	    Used while a stroke is in progress: re-encoding a base64 PNG on every
	    mouse move would be far too slow, so the preview bypasses effectsInUse
	    and only the finished stroke is committed. */
	void setLiveEraserMask(const QImage& mask);
	void clearLiveEraserMask();
	//@}

	//! \name Image coordinate mapping
	//! One definition of where the image sits inside the frame, shared by
	//! everything that has to go between pixels and page geometry. It mirrors
	//! the transform chain in DrawObj_Item, which is the authority; keep them
	//! together if that chain ever changes.
	//@{
	/*! \brief Full-resolution image pixels (the OrigW x OrigH space) to
	    item-local points. Excludes the low-res proxy scale, so it is in terms
	    of the real image, not whatever preview pixmap is loaded. */
	QTransform imagePixelToLocal() const;
	//! As imagePixelToLocal(), starting from the space of a \a maskSize mask.
	QTransform maskPixelToLocal(const QSize& maskSize) const;
	//@}

	//! \name Contour detection
	//@{
	/*! \brief The mask a contour should be traced from: the eraser mask if the
	    frame has one, otherwise the image's own alpha channel. Null when
	    neither exists, which means "nothing is transparent". */
	QImage contourSourceMask() const;
	//! True when there is something to trace, i.e. contourSourceMask() is set.
	bool canDetectContour() const;

	/*! \brief Trace the visible region of the image into a contour.

	    \param threshold alpha at or above which a pixel counts as visible.
	    \param tolerance simplification tolerance, in image pixels.
	    \param mode      which traced rings to keep.
	    \param message   set to a user-facing explanation when the result is
	                     empty or needed clamping; may be null.
	    \returns the contour in item-local points, ready for ContourLine.
	             Empty when nothing is visible. */
	FPointArray detectContourFromMask(int threshold, double tolerance,
	                                  ScContour::Mode mode, QString* message = nullptr) const;
	//@}

protected:
	void DrawObj_Item(ScPainter *p, const QRectF& e) override;

private:
	/*! \brief The image to draw: the eraser composite when a mask is in force,
	    otherwise pixm itself. Rebuilds the composite only when the mask or the
	    underlying pixmap actually changed. */
	QImage* imageForDraw();

	//! Transient stroke preview; never saved. See setLiveEraserMask().
	QImage m_liveEraserMask;
	bool m_liveEraserMaskSet {false};

	//! pixm with the eraser mask multiplied into its alpha channel.
	QImage m_eraserComposite;
	//! Identity of what m_eraserComposite was built from, so a stale composite
	//! is never drawn after the image is reloaded or the mask edited.
	QString m_eraserCompositeKey;
};

#endif
