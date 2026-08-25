/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UTIL_CONTOUR_H
#define UTIL_CONTOUR_H

#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QVector>

#include "scribusapi.h"

/*!
 \brief Boundary tracing for binary masks.

 Used to turn an image eraser mask (see scimageerasermask.h) into a polygon
 that can become a PageItem's ContourLine, so text wraps around what is
 actually still visible in a photo rather than around its frame.
 */
namespace ScContour
{
	//! Which of the traced rings to keep.
	enum class Mode
	{
		LargestOnly,   //!< the single biggest solid region, holes discarded
		AllRegions,    //!< every solid region, holes discarded
		IncludeHoles   //!< every solid region plus the holes inside them
	};

	/*! \brief One traced boundary ring.

	    Points are grid-corner coordinates: for a w x h mask they run 0..w and
	    0..h, because the boundary follows pixel edges rather than pixel
	    centres. The ring is closed implicitly - the last point joins the
	    first, and they are never equal.  */
	struct SCRIBUS_API Ring
	{
		QPolygonF points;
		//! Shoelace area. Positive for a solid region's outer boundary,
		//! negative for a hole, in the y-down raster coordinate system.
		double signedArea {0.0};
		bool isHole() const { return signedArea < 0.0; }
	};

	/*! \brief Trace every boundary of the true cells in a \a w x \a h grid.

	    Edge-following marching squares: each cell edge between a true cell and
	    a false one (or the outside of the grid) becomes a directed unit edge,
	    oriented so the solid side is on the right. Chaining those gives closed
	    rings whose winding already distinguishes outer boundaries from holes,
	    with no separate containment test.

	    Where two solid cells meet only at a corner, the walk takes the
	    sharpest available clockwise turn. That is the tie-break that keeps
	    diagonally touching regions apart (4-connected foreground) instead of
	    letting the trace jump between them. */
	SCRIBUS_API QList<Ring> traceBoundaries(const QVector<bool>& grid, int w, int h);

	//! Binarise \a mask at \a threshold: a sample >= threshold is solid.
	//! Works on the greyscale value, so an 8-bit mask and an alpha channel
	//! both read the same way.
	SCRIBUS_API QVector<bool> binarize(const QImage& mask, int threshold);

	/*! \brief Ramer-Douglas-Peucker simplification of a closed ring.

	    A closed ring has no natural endpoints to anchor the recursion, so it
	    is split at its two mutually most distant points and each half is
	    simplified as an open polyline. Rings that would collapse below three
	    points are returned unchanged. */
	SCRIBUS_API QPolygonF simplifyClosed(const QPolygonF& ring, double tolerance);

	//! Drop points lying on the straight line between their neighbours. Cheap,
	//! and it removes the one-vertex-per-pixel-step noise before the RDP pass.
	SCRIBUS_API QPolygonF removeCollinear(const QPolygonF& ring);

	//! Shoelace signed area of a closed ring.
	SCRIBUS_API double signedArea(const QPolygonF& ring);

	/*! \brief Full pipeline: binarise, trace, select by \a mode, simplify.
	    Rings come back in mask grid-corner coordinates. */
	SCRIBUS_API QList<Ring> detect(const QImage& mask, int threshold, double tolerance, Mode mode);
}

#endif
