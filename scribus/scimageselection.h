/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGESELECTION_H
#define SCIMAGESELECTION_H

#include <functional>

#include <QImage>
#include <QObject>
#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>

#include "scribusapi.h"

/*!
 \brief Pixel selection for the built-in image editor.

 The selection is stored as an 8-bit coverage mask (Format_Alpha8) the same
 size as the image: 0 = unselected, 255 = fully selected, in-between = partial
 (anti-aliased edges / feathering). This model is deliberately mask-based so a
 future AI "Smart Select" (SAM) can drop its result straight in via
 setFromMask().
 */
class SCRIBUS_API ScImageSelection : public QObject
{
	Q_OBJECT

public:
	enum Mode { Replace, Add, Subtract, Intersect };

	explicit ScImageSelection(const QSize& imageSize, QObject* parent = nullptr);

	// --- Query ---------------------------------------------------------------
	QSize size() const;
	bool isEmpty() const;                 //!< true if nothing is selected
	QRect bounds() const;                 //!< tight bounding box of the selection
	bool contains(const QPoint& p) const; //!< true if coverage >= 128 at p
	const QImage& mask() const { return m_mask; }

	// --- Modification --------------------------------------------------------
	void clear();
	void selectAll();
	void invert();
	void setFromRect(const QRect& r, Mode mode = Replace, bool antialias = true);
	void setFromEllipse(const QRect& r, Mode mode = Replace);
	void setFromPath(const QPainterPath& path, Mode mode = Replace);
	void setFromMask(const QImage& mask, Mode mode = Replace);

	//! Stamp a soft circular brush at \a center (image coords). \a hardness 0..1
	//! sets the fully-opaque inner fraction; \a add paints coverage, else erases.
	//! Only the brush's bounding box is touched (cheap enough for live strokes).
	void paintBrush(const QPointF& center, double radius, double hardness, bool add);

	// --- Refinement ----------------------------------------------------------
	void feather(double radius);
	void expand(int pixels);
	void contract(int pixels);
	void smooth(int radius);

	// --- Rendering aids ------------------------------------------------------
	QPainterPath outlinePath() const;     //!< boundary at threshold 128 (marching ants)
	QRegion asRegion() const;

signals:
	void changed();

private:
	//! Rasterise a shape (drawn by \a draw) into an Alpha8 coverage stamp.
	QImage rasterizeStamp(const std::function<void(QPainter&)>& draw, bool antialias) const;
	//! Combine an Alpha8 \a stamp into m_mask according to \a mode.
	void blendStamp(const QImage& stamp, Mode mode);
	void invalidateCache();

	QImage m_mask;                        //!< Format_Alpha8
	mutable QRect m_cachedBounds;
	mutable bool m_boundsDirty { true };
	mutable QPainterPath m_cachedOutline;
	mutable bool m_outlineDirty { true };
};

#endif // SCIMAGESELECTION_H
