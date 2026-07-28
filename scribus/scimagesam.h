/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGESAM_H
#define SCIMAGESAM_H

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include "scribusapi.h"

/*!
 \brief Segment-Anything (MobileSAM) inference wrapper for the image editor's
        Smart Select tool.

 Runs an ONNX encoder once per image (cached embedding), then a lightweight
 decoder per click/box prompt, returning an Alpha8 selection mask sized to the
 image. ONNX Runtime is optional at build time (HAVE_SAM); without it every
 call reports "not available" so the tool degrades gracefully.

 Expected models (MobileSAM / samexporter export) in modelDir():
   encoder.onnx   input [1,3,1024,1024] → image_embeddings [1,256,64,64]
   decoder.onnx   standard SAM decoder I/O (image_embeddings, point_coords,
                  point_labels, mask_input, has_mask_input, orig_im_size)
 */
class SCRIBUS_API SamSegmenter
{
public:
	static SamSegmenter& instance();

	//! Directory the models are loaded from: ~/.config/scribus/sam/
	static QString modelDir();

	//! Load the models if present (idempotent). Returns isAvailable().
	bool ensureLoaded();
	//! true if ONNX Runtime is compiled in AND both models loaded.
	bool isAvailable() const;
	//! Human-readable state for the UI (why it is / isn't ready).
	QString statusMessage() const;

	//! Run the encoder on \a image and cache its embedding. Returns success.
	bool setImage(const QImage& image);
	//! When true, masks are anti-aliased from the raw decoder logits (soft edge
	//! across the zero-crossing) instead of hard-thresholded. Off by default.
	void setAntialias(bool on);
	bool antialias() const;
	//! Segment from a click in image coordinates (foreground/background).
	QImage segmentAtPoint(const QPoint& pt, bool foreground = true);
	//! Segment from several prompt points (labels: 1 = foreground, 0 = background).
	//! Used for iterative refinement (add/subtract points).
	QImage segmentAtPoints(const QVector<QPoint>& points, const QVector<int>& labels);
	//! Segment from a box in image coordinates.
	QImage segmentInBox(const QRect& box);

private:
	SamSegmenter();
	~SamSegmenter();
	SamSegmenter(const SamSegmenter&) = delete;
	SamSegmenter& operator=(const SamSegmenter&) = delete;

	struct Impl;
	Impl* d;
};

#endif // SCIMAGESAM_H
