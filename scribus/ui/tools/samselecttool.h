/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SAMSELECTTOOL_H
#define SAMSELECTTOOL_H

#include <QFutureWatcher>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class QGraphicsItem;
class QWidget;

/*!
 \brief AI "Smart Select" tool (Segment Anything / MobileSAM).

 On activation it runs the SAM encoder on the current image once (cached). A
 click segments the object under the cursor; further clicks refine the SAME
 object by adding foreground (include) or background (exclude) prompt points —
 the options-bar toggle (or Alt-click for subtract) chooses which. A drag
 segments inside a box and starts a fresh object; "New Selection" clears the
 prompt points. Degrades gracefully to a status message when the model is
 unavailable.
 */
class SCRIBUS_API SamSelectTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Smart Select (SAM)"); }
	QCursor cursor() const override;
	QWidget* optionsBar() override;

	void activate(ScImageEditor* editor) override;
	void deactivate() override;
	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override;

private:
	void removeRubber();
	void showStatus(const QString& msg);
	void clearSession();                                //!< drop prompt points + markers + baseline
	void beginSessionIfNeeded();                        //!< snapshot the pre-SAM selection once per object
	void addPointMarker(const QPointF& p, bool foreground);
	void applyMask(const QImage& mask);                 //!< combine \a mask with the baseline per m_combineMode
	QImage featherMask(const QImage& mask) const;       //!< soften the object edge by m_feather px
	void refineFromPoints();                            //!< re-run SAM on all points
	void resegment();                                   //!< re-run the last prompt (e.g. after AA toggle)
	void onEncodeFinished();                            //!< worker-thread encode completed

	QGraphicsItem* m_rubber { nullptr };
	QPointF m_start;
	bool m_dragging { false };
	bool m_ready { false };
	//! true while the encoder runs on a worker thread; canvas clicks are
	//! ignored (with a status hint) until it clears. Cleared by deactivate()
	//! so a stale encode result is discarded rather than applied (ONNX runs
	//! cannot be aborted mid-flight; "cancel" = ignore the result).
	bool m_encoding { false };
	QFutureWatcher<bool> m_encodeWatcher;
	bool m_addMode { true };                            //!< true = add (fg) point, false = subtract (bg)
	bool m_antialias { true };                          //!< anti-alias mask edges from the decoder logits
	int m_feather { 0 };                                //!< soften the object edge by N px (0 = crisp)
	ScImageSelection::Mode m_combineMode { ScImageSelection::Replace }; //!< how the SAM object merges with the existing selection
	QVector<QPoint> m_points;                           //!< accumulated prompt points (image coords)
	QVector<int> m_labels;                              //!< 1 = foreground, 0 = background (parallel to m_points)
	QVector<QGraphicsItem*> m_markers;                  //!< on-canvas dots for the prompt points
	QImage m_baseline;                                  //!< selection mask before this object (for non-Replace combine)
	QImage m_lastSamMask;                               //!< last SAM result (re-combined live when feather/mode change)
	QRect m_lastBox;                                    //!< last box prompt (for re-inference on AA toggle)
	bool m_haveBaseline { false };
	bool m_haveLastBox { false };
};

#endif // SAMSELECTTOOL_H
