/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CANVASMODE_REMOVALMASK_H
#define CANVASMODE_REMOVALMASK_H

#include <atomic>
#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QPointF>
#include <QRect>
#include <QString>

#include "canvasmode.h"

class InpaintProgressDialog;
class QTimer;
class PageItem;
class ScribusDoc;
class ScribusView;
class ScribusMainWindow;

/*!
 \brief Paint over an unwanted object and have the pixels underneath rebuilt.

 The user brushes a red mask over whatever should go, then presses Apply: the
 masked pixels are regenerated from the pixels around them by Telea fast
 marching inpainting (see util_inpaint.h), the result is written to a new file
 beside the document, and the frame is pointed at it. The file the user
 imported is never written to.

 This is not the eraser, and the difference is worth keeping straight. The
 eraser hides pixels behind a mask that lives in the document and can be taken
 back pixel by pixel at any time; nothing about the picture changes. This tool
 invents new pixels and hands the frame a different file. Its mask is scratch
 working state: it is never saved, and Apply, Cancel or leaving the mode all
 throw it away.

 The brush itself is the eraser's, not a copy of it - the stroke code in
 ScEraserMask is already standalone, so both tools call the same stamp(),
 stampLine() and applyStroke(). That is also why the mask is held in the
 eraser's sense (255 untouched, 0 fully selected) rather than the more obvious
 one: it lets applyStroke() be used verbatim, including the dab-spacing and
 overlap handling that two real scalloping bugs were fixed in.
 */
class CanvasMode_RemovalMask : public CanvasMode
{
	Q_OBJECT

public:
	explicit CanvasMode_RemovalMask(ScribusView* view);
	~CanvasMode_RemovalMask() override;

	void activate(bool fromGesture) override;
	void deactivate(bool forGesture) override;
	void enterEvent(QEvent*) override;
	void leaveEvent(QEvent*) override;
	void mousePressEvent(QMouseEvent* m) override;
	void mouseMoveEvent(QMouseEvent* m) override;
	void mouseReleaseEvent(QMouseEvent* m) override;
	void keyPressEvent(QKeyEvent* e) override;
	void drawControls(QPainter* p) override;

	//! The mode instance currently on the canvas, or null. The options bar
	//! needs to reach the live mask to know whether Apply can be pressed, and
	//! this is cheaper and clearer than casting the view's current mode.
	static CanvasMode_RemovalMask* active();

	//! Brush diameter in image pixels, as shown in the options bar.
	static int brushSize();
	static void setBrushSize(int size);
	//! Brush hardness, 0 (fully feathered) to 100 (hard edged).
	static int brushHardness();
	static void setBrushHardness(int hardness);

	//! True when at least one pixel is selected for removal.
	bool hasSelection() const;
	//! True while an inpainting run is in flight; the mask is frozen then.
	bool isRunning() const { return m_running; }

	//! Throw the mask away and repaint. Does not touch the document.
	void clearMask();

	/*! \brief Start the removal: threshold the mask, hand it to the inpainter
	    on a worker thread, and put a progress dialog up. Returns immediately;
	    the document is only touched when the run finishes. */
	void applyRemoval();

private slots:
	//! The worker finished, was cancelled, or threw. Runs on the GUI thread.
	void inpaintFinished();
	//! Cancel pressed on the progress dialog.
	void inpaintCancelled();

private:
	//! What the worker hands back, so the GUI thread can report properly
	//! instead of guessing why it got a null image.
	struct Outcome
	{
		QImage image;
		QString error;
	};

	//! The image frame this tool acts on, or null when the selection is not a
	//! usable raster image frame.
	PageItem* targetItem() const;
	//! Leave the mode and hand the canvas back to normal editing.
	void exitRemovalMode();

	//! Make sure m_mask exists and belongs to \a item, discarding one left
	//! over from a different frame.
	bool ensureMaskFor(PageItem* item);

	//! Map a canvas position to a pixel in the mask of \a item. False when the
	//! point falls outside the frame or the geometry is degenerate.
	bool canvasToMask(PageItem* item, const QPointF& canvasPos, QPointF& maskPos) const;
	//! Brush radius in mask pixels for \a item, derived from the image scale.
	double maskRadiusFor(PageItem* item) const;
	//! Mask pixels to canvas widget pixels, for drawing the overlay.
	QTransform maskToCanvas(PageItem* item) const;

	//! Rebuild the red overlay from the mask over \a region.
	void refreshOverlay(const QRect& region);
	//! Fold the finished stroke into the mask and refresh the options bar.
	void commitStroke();
	//! Drop stroke state without touching the mask.
	void cancelStroke();
	//! Tell the options bar whether Apply is now possible.
	void notifyOptionsBar();

	//! Selection as a plain yes/no mask, thresholded at MaskThreshold.
	QImage thresholdedMask() const;

	/*! \brief Where the rebuilt picture should be written.

	    Beside the document if it has been saved, otherwise beside the source
	    picture, otherwise the system temporary directory; in a .scribus_edits
	    subdirectory, stamped with the time, and given a counter if a file of
	    that name somehow already exists. Never the source file itself: that is
	    checked here and asserted again at the point of writing.
	    \param error set to a user-facing explanation when this returns empty.*/
	QString outputPathFor(PageItem* item, QString& error) const;

	//! Tear down the dialog, watcher and poll timer after a run ends.
	void finishRun();

	ScribusMainWindow* m_ScMW {nullptr};

	//! Frame the mask belongs to; empty when there is no mask.
	QString m_maskItemName;
	/*! \brief The mask, in ScEraserMask's sense: 255 means "leave this pixel
	    alone", 0 means "remove it". Inverted on the way out; see the class
	    comment for why it is stored this way round. */
	QImage m_mask;
	//! Semi-transparent red picture of m_mask, drawn over the frame.
	QImage m_overlay;

	//! Mask as it was when the current stroke started.
	QImage m_strokeBase;
	//! Coverage accumulated by the current stroke, max()-blended.
	QImage m_coverage;

	bool m_painting {false};
	//! True when the stroke takes pixels out of the mask (Alt held at press).
	bool m_subtracting {false};
	//! Last mouse position in mask pixels, for interpolating between events.
	QPointF m_lastMaskPos;
	//! Where the previous stroke ended, so Shift can draw a line on from it.
	QPointF m_strokeEnd;
	bool m_strokeEndValid {false};
	//! Distance travelled since the last dab, carried across mouse-move events
	//! so dab spacing does not depend on the event rate.
	double m_dabCarry {0.0};

	//! Last mouse position in canvas pixels, for the cursor ring.
	QPointF m_cursorCanvasPos;
	bool m_cursorValid {false};

	//! \name In-flight run
	//@{
	bool m_running {false};
	/*! \brief Cancel flag and progress counter, shared with the worker.

	    Shared pointers rather than members because the worker outlives a
	    cancel: if the user closes the document while a removal is running, the
	    mode goes away and the worker keeps reading these until it notices the
	    flag. Poll and store rather than a cross-thread signal for the same
	    reason - there is no receiver whose lifetime the worker can rely on. */
	std::shared_ptr<std::atomic<bool>> m_cancel;
	std::shared_ptr<std::atomic<int>> m_percent;
	//! Drives the progress bar from m_percent on the GUI thread.
	QTimer* m_pollTimer {nullptr};
	QFutureWatcher<Outcome>* m_watcher {nullptr};
	InpaintProgressDialog* m_progress {nullptr};
	//! Everything the finish handler needs, captured when the run started, so
	//! it never has to trust a pointer that the user could have invalidated
	//! while the dialog was up.
	QPointer<ScribusDoc> m_runDoc;
	QString m_runItemName;
	QString m_runSourcePath;
	QString m_runOutputPath;
	//@}
};

#endif
