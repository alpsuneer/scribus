/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef RESIZEIMAGEDIALOG_H
#define RESIZEIMAGEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include "scribusapi.h"

class QCheckBox;
class QLabel;
class QSpinBox;
class PageItem;
class ScribusDoc;

/*!
 \brief "Resize Image" — resample an image frame's source file down to what the
 frame actually needs in print.

 Writes a new file beside the source (name_resized.ext, original untouched),
 relinks the frame to it and adjusts the image scale/offset so the frame looks
 pixel-identical. The relink rides PageItem::loadImage()'s GET_IMAGE undo
 state, so Ctrl+Z points the frame back at the original file.

 The one-click path is "Fit to Frame @ DPI": target pixels are computed from
 the image's current printed size so its effective resolution becomes exactly
 the chosen DPI (default 240 — the News_Paper standard).
 */
class SCRIBUS_API ResizeImageDialog : public QDialog
{
	Q_OBJECT

public:
	//! Guards + opens the dialog for the current selection. Silent no-op when
	//! the selection is not an image frame with a loaded image.
	static void openForSelection(ScribusDoc* doc, QWidget* parent);

	/*! The shared resample pipeline: scale \a source to \a newW x \a newH,
	 write it as name_resized.ext beside the item's file (never overwriting),
	 relink the frame keeping its look identical, one undo step. Returns an
	 empty string on success or the failure reason. On success \a summary gets
	 "6000 x 4000 (8.2 MB) → 1890 x 1260 (410 KB) @ 240 dpi" and \a outPathOut
	 the written file path. */
	static QString resampleImageFile(ScribusDoc* doc, PageItem* item, const QImage& source,
	                                 int newW, int newH, double effDpiX, double effDpiY,
	                                 QString* summary = nullptr, QString* outPathOut = nullptr);

	/*! Auto-DPI evaluation, shared by the canvas resize gesture (after a
	 shrink) and the toolbar toggle (immediately on enable / target change).
	 Resamples the single selected image frame down to the configured target
	 DPI when the Auto-DPI toggle is on and the effective DPI exceeds the
	 target by more than 25%. Downsample only; no-op otherwise — silent from
	 the gesture, with a brief "already at/below" status line when
	 \a reportWhenSkipped is set (the toggle path). The gesture runs this
	 inside its own transaction (resize + resample = one undo step); stand-
	 alone calls still undo as one step via resampleImageFile()'s transaction. */
	static void maybeAutoResample(ScribusDoc* doc, bool reportWhenSkipped = false);

private:
	ResizeImageDialog(ScribusDoc* doc, PageItem* item, const QImage& source, QWidget* parent);

	void fitToFrameDpi();
	void syncLinked(bool fromWidth);
	void updateResultLine();
	void doResize();

	ScribusDoc* m_doc { nullptr };
	PageItem* m_item { nullptr };
	QImage m_source;
	double m_effDpiX { 72.0 };
	double m_effDpiY { 72.0 };

	QSpinBox* m_widthSpin { nullptr };
	QSpinBox* m_heightSpin { nullptr };
	QCheckBox* m_lockAspect { nullptr };
	QSpinBox* m_dpiSpin { nullptr };
	QLabel* m_resultLine { nullptr };
	bool m_syncing { false };
};

/*!
 \brief Compact toolbar field showing the selected image's effective print DPI.

 Self-contained: an internal timer polls the current document/selection (via
 ScCore), so hosts only need to construct and place it. Shows the effective
 DPI of a single selected image frame, live; typing a value and pressing
 Enter resamples the image file to that DPI at the current frame size via
 ResizeImageDialog::resampleImageFile() — the quick path of the dialog.
 Disabled for non-image or multiple selections.
 */
class SCRIBUS_API ImageDpiField : public QWidget
{
	Q_OBJECT

public:
	explicit ImageDpiField(QWidget* parent = nullptr);

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
	void refresh();

private:
	PageItem* currentImageItem(ScribusDoc** docOut = nullptr) const;
	void applyDpi();

	QSpinBox* m_spin { nullptr };
	QCheckBox* m_autoCheck { nullptr };
	QSpinBox* m_autoTarget { nullptr };
	QTimer m_timer;
};

#endif // RESIZEIMAGEDIALOG_H
