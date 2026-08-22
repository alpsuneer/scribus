/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PENPATHTOOL_H
#define PENPATHTOOL_H

#include <QPointF>
#include <QVector>

#include "scribusapi.h"
#include "ui/tools/imagetool.h"

class PenPathPreviewItem;
class PenHandlesItem;

/*!
 \brief One node of a pen path.

 \a handleIn / \a handleOut are absolute positions, not offsets, so they drop
 straight into QPainterPath::cubicTo(). A corner anchor keeps both handles on
 top of \a pos, which makes a cubic segment through it degenerate into the
 straight line we want — so the segment builder never needs a special case
 beyond picking lineTo() when neither end is smooth.
 */
struct PenAnchor
{
	QPointF pos;
	QPointF handleIn;
	QPointF handleOut;
	bool smooth { false };
};

/*!
 \brief Pen path selection: precision curved selections, Photoshop-style.

 Click places a corner anchor; click-and-drag places a smooth one and pulls a
 symmetric pair of handles. Enter or double-click closes the path and commits it
 as a selection, Escape discards, Backspace drops the last anchor, Alt+click on
 an existing anchor converts it between corner and smooth, and Shift while
 dragging a handle constrains it to 45° increments.

 V1 is deliberately append-only: there is no hit-testing for dragging an anchor
 or a handle after the fact (Alt+click convert is the one exception, and needs
 only a proximity test). Refinement after committing is done with the Add /
 Subtract selection modes.
 */
class SCRIBUS_API PenPathTool : public ImageTool
{
	Q_OBJECT

public:
	using ImageTool::ImageTool;

	QString name() const override { return tr("Pen Path"); }

	QCursor cursor() const override;

	void mousePress(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseMove(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseRelease(QMouseEvent* e, const QPointF& imagePos) override;
	void mouseDoubleClick(QMouseEvent* e, const QPointF& imagePos) override;
	void keyPress(QKeyEvent* e) override;
	void deactivate() override;

private:
	void updatePreview();
	void removePreview();
	void commit();
	void cancel();

	//! Index of the anchor under \a p (within a few screen pixels), or -1.
	int anchorAt(const QPointF& p) const;
	//! Flip anchor \a index between corner and smooth.
	void toggleAnchorKind(int index);
	//! True while the cursor sits close enough to the first anchor to hint "close here".
	bool closeHintActive() const;

	QVector<PenAnchor> m_anchors;
	QPointF m_cursor;
	QPointF m_dragStart;
	bool m_dragging { false };
	PenPathPreviewItem* m_pathItem { nullptr };
	PenHandlesItem* m_handlesItem { nullptr };
	//! Latched from the modifiers on the FIRST click of the path and then left
	//! alone: once drawing starts Shift and Alt mean pen things (constrain,
	//! convert), so the combine mode cannot be changed mid-path.
	ScImageSelection::Mode m_mode { ScImageSelection::Replace };
};

#endif // PENPATHTOOL_H
