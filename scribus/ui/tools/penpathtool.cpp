/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/penpathtool.h"

#include <cmath>

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QCursor>
#include <QPen>
#include <QVector>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

// ────────────────────────────────────────────────────────────────────────────
// Geometry helpers
// ────────────────────────────────────────────────────────────────────────────

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor PenPathTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-pen"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

namespace
{
	//! Extend \a path from \a from to \a to, straight when neither end carries a
	//! handle and cubic otherwise. A corner anchor's handles sit on its own
	//! position, so a mixed corner/smooth pair still produces the right curve.
	void appendSegment(QPainterPath& path, const PenAnchor& from, const PenAnchor& to)
	{
		if (!from.smooth && !to.smooth)
			path.lineTo(to.pos);
		else
			path.cubicTo(from.handleOut, to.handleIn, to.pos);
	}

	//! The path through \a anchors; \a closed adds the wrap-around segment.
	QPainterPath buildPath(const QVector<PenAnchor>& anchors, bool closed)
	{
		QPainterPath path;
		if (anchors.isEmpty())
			return path;
		// Winding, never odd-even: a hand-drawn outline crosses itself often and
		// the default rule punches wedges out of the selection (same trap the
		// freehand and polygon lassos hit).
		path.setFillRule(Qt::WindingFill);
		path.moveTo(anchors.first().pos);
		for (int i = 1; i < anchors.size(); ++i)
			appendSegment(path, anchors.at(i - 1), anchors.at(i));
		if (closed && anchors.size() >= 2)
		{
			appendSegment(path, anchors.last(), anchors.first());
			path.closeSubpath();
		}
		return path;
	}

	//! \a d snapped to the nearest 45° direction, keeping its length.
	QPointF constrainTo45(const QPointF& d)
	{
		const double len = std::hypot(d.x(), d.y());
		if (len <= 0.0)
			return d;
		const double step = M_PI / 4.0;
		const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
		return QPointF(len * std::cos(angle), len * std::sin(angle));
	}

	//! Two interleaved cosmetic dash passes — the marching-ants trick, so the
	//! outline stays readable over both light and dark parts of a photo.
	void strokeTwoPass(QPainter* painter, const QPainterPath& path, qreal opacity)
	{
		if (path.isEmpty())
			return;
		const QVector<qreal> dashes { 4.0, 4.0 };
		const qreal previous = painter->opacity();
		painter->setOpacity(opacity);
		painter->setBrush(Qt::NoBrush);

		QPen dark(Qt::black);
		dark.setCosmetic(true);
		dark.setWidth(1);
		dark.setDashPattern(dashes);
		painter->setPen(dark);
		painter->drawPath(path);

		QPen light(Qt::white);
		light.setCosmetic(true);
		light.setWidth(1);
		light.setDashPattern(dashes);
		light.setDashOffset(4.0);
		painter->setPen(light);
		painter->drawPath(path);

		painter->setOpacity(previous);
	}
}

// ────────────────────────────────────────────────────────────────────────────
// Overlay items
//
// Two scene items for the whole tool, however many anchors there are: one for
// the path, one for the anchors and handles. Both paint everything in a single
// paint() (the MarchingAntsItem pattern) instead of parking a QGraphicsItem per
// anchor, so adding a node does not churn the scene graph.
// ────────────────────────────────────────────────────────────────────────────

namespace
{
	//! Scene units per screen pixel, read live from the view so marker sizes stay
	//! constant on screen at any zoom.
	//!
	//! Safe to call from paint(), NEVER from boundingRect(): a bounding rect that
	//! changes with the zoom is a geometry change Qt was not told about, and it
	//! corrupts the scene's item index — the symptom is a SIGSEGV deep inside
	//! QGraphicsView::paintEvent() walking a stale entry, which is exactly what
	//! this tool did before the margins below were made zoom-independent.
	qreal sceneUnitsPerPixel(const QGraphicsItem* item)
	{
		if (item && item->scene() && !item->scene()->views().isEmpty())
		{
			const qreal scale = item->scene()->views().constFirst()->transform().m11();
			if (scale > 0.0)
				return 1.0 / scale;
		}
		return 1.0;
	}

	//! Worst-case scene units for \a screenPixels, used to pad bounding rects.
	//! ScImageEditorView clamps zoom to a floor of 0.05, so this is an upper
	//! bound at any zoom the user can reach, and — unlike the live conversion —
	//! it is a constant, which is what the geometry contract requires. Below that
	//! floor (only reachable by fitting an enormous image to the window) a marker
	//! could clip at the edge; cosmetic, and never a crash.
	constexpr qreal kMinViewScale = 0.05;
	constexpr qreal boundsMargin(qreal screenPixels) { return screenPixels / kMinViewScale; }
}

class PenPathPreviewItem : public QGraphicsItem
{
public:
	void setState(const QVector<PenAnchor>& anchors, const QPointF& cursor, bool hasCursor)
	{
		prepareGeometryChange();
		m_anchors = anchors;
		m_cursor = cursor;
		m_hasCursor = hasCursor;
		update();
	}

	QRectF boundingRect() const override
	{
		QPainterPath path = buildPath(m_anchors, false);
		QRectF box = path.controlPointRect();
		if (m_hasCursor)
			box = box.united(QRectF(m_cursor, m_cursor));
		if (box.isNull())
			return QRectF();
		const qreal margin = boundsMargin(4.0);
		return box.adjusted(-margin, -margin, margin, margin);
	}

	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
	{
		if (m_anchors.isEmpty())
			return;
		strokeTwoPass(painter, buildPath(m_anchors, false), 1.0);
		if (m_hasCursor)
		{
			// The segment still being placed is drawn at half strength so it
			// reads as provisional next to the anchored part of the path.
			PenAnchor live;
			live.pos = live.handleIn = live.handleOut = m_cursor;
			QPainterPath seg;
			seg.moveTo(m_anchors.last().pos);
			appendSegment(seg, m_anchors.last(), live);
			strokeTwoPass(painter, seg, 0.5);
		}
	}

private:
	QVector<PenAnchor> m_anchors;
	QPointF m_cursor;
	bool m_hasCursor { false };
};

class PenHandlesItem : public QGraphicsItem
{
public:
	void setState(const QVector<PenAnchor>& anchors, bool closeHint)
	{
		prepareGeometryChange();
		m_anchors = anchors;
		m_closeHint = closeHint;
		update();
	}

	QRectF boundingRect() const override
	{
		if (m_anchors.isEmpty())
			return QRectF();
		QRectF box(m_anchors.first().pos, m_anchors.first().pos);
		for (const PenAnchor& a : m_anchors)
		{
			box = box.united(QRectF(a.pos, a.pos));
			if (a.smooth)
			{
				box = box.united(QRectF(a.handleIn, a.handleIn));
				box = box.united(QRectF(a.handleOut, a.handleOut));
			}
		}
		// Generous by construction: the markers are sized in screen pixels, so at
		// low zoom they cover a lot of scene units and the pad has to assume the
		// worst case rather than track the current one.
		const qreal margin = boundsMargin(12.0);
		return box.adjusted(-margin, -margin, margin, margin);
	}

	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
	{
		if (m_anchors.isEmpty())
			return;
		const qreal u = sceneUnitsPerPixel(this);   // scene units per screen pixel
		painter->setRenderHint(QPainter::Antialiasing, true);

		QPen outline(Qt::black);
		outline.setCosmetic(true);
		outline.setWidth(1);

		QPen leader(QColor(140, 140, 140));
		leader.setCosmetic(true);
		leader.setWidth(1);

		// Handle leaders and knobs first, so the anchor sits on top of them.
		for (const PenAnchor& a : m_anchors)
		{
			if (!a.smooth)
				continue;
			painter->setPen(leader);
			painter->setBrush(Qt::NoBrush);
			painter->drawLine(a.handleIn, a.pos);
			painter->drawLine(a.pos, a.handleOut);

			painter->setPen(outline);
			painter->setBrush(Qt::white);
			const qreal r = 3.0 * u;
			painter->drawEllipse(a.handleIn, r, r);
			painter->drawEllipse(a.handleOut, r, r);
		}

		painter->setPen(outline);
		painter->setBrush(Qt::white);
		const qreal r = 4.0 * u;
		for (const PenAnchor& a : m_anchors)
		{
			if (a.smooth)
				painter->drawEllipse(a.pos, r, r);
			else
				painter->drawRect(QRectF(a.pos.x() - r, a.pos.y() - r, 2 * r, 2 * r));
		}

		if (m_closeHint)
		{
			// Ring around the first anchor: "you are over the start".
			painter->setBrush(Qt::NoBrush);
			painter->setPen(outline);
			const qreal hr = 6.0 * u;
			painter->drawEllipse(m_anchors.first().pos, hr, hr);
		}
	}

private:
	QVector<PenAnchor> m_anchors;
	bool m_closeHint { false };
};

// ────────────────────────────────────────────────────────────────────────────
// PenPathTool
// ────────────────────────────────────────────────────────────────────────────

void PenPathTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor)
		return;

	// Alt on an existing anchor converts it rather than starting a new one. This
	// is the only place the tool hit-tests; everything else is append-only.
	if (e->modifiers().testFlag(Qt::AltModifier) && !m_anchors.isEmpty())
	{
		const int index = anchorAt(imagePos);
		if (index >= 0)
		{
			toggleAnchorKind(index);
			updatePreview();
			return;
		}
	}

	if (m_anchors.isEmpty())
		m_mode = modeFromModifiers(e->modifiers());   // latched for the whole path

	PenAnchor anchor;
	anchor.pos = anchor.handleIn = anchor.handleOut = imagePos;
	anchor.smooth = false;
	m_anchors.append(anchor);

	m_dragStart = imagePos;
	m_dragging = true;
	m_cursor = imagePos;
	updatePreview();
}

void PenPathTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor || m_anchors.isEmpty())
		return;
	m_cursor = imagePos;

	if (m_dragging)
	{
		// Click versus click-and-drag is a question about how far the hand moved,
		// so the gate is a screen distance rather than a flat image-pixel count.
		if (QLineF(m_dragStart, imagePos).length() >= imageDistance(3.0))
		{
			PenAnchor& anchor = m_anchors.last();
			QPointF delta = imagePos - anchor.pos;
			if (e->modifiers().testFlag(Qt::ShiftModifier))
				delta = constrainTo45(delta);
			anchor.smooth = true;
			anchor.handleOut = anchor.pos + delta;
			anchor.handleIn = anchor.pos - delta;   // symmetric pair
		}
	}
	updatePreview();
}

void PenPathTool::mouseRelease(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	Q_UNUSED(imagePos)
	m_dragging = false;
	updatePreview();
}

void PenPathTool::mouseDoubleClick(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	Q_UNUSED(imagePos)
	commit();
}

void PenPathTool::keyPress(QKeyEvent* e)
{
	switch (e->key())
	{
		case Qt::Key_Return:
		case Qt::Key_Enter:
			commit();
			e->accept();
			break;
		case Qt::Key_Escape:
			cancel();
			e->accept();
			break;
		case Qt::Key_Backspace:
			if (!m_anchors.isEmpty())
			{
				m_anchors.removeLast();
				m_dragging = false;
				if (m_anchors.isEmpty())
					removePreview();
				else
					updatePreview();
			}
			e->accept();
			break;
		default:
			break;
	}
}

void PenPathTool::deactivate()
{
	cancel();
}

int PenPathTool::anchorAt(const QPointF& p) const
{
	const double reach = imageDistance(6.0);
	// Back to front: the most recently placed anchor wins an overlap.
	for (int i = m_anchors.size() - 1; i >= 0; --i)
	{
		if (QLineF(p, m_anchors.at(i).pos).length() <= reach)
			return i;
	}
	return -1;
}

void PenPathTool::toggleAnchorKind(int index)
{
	if (index < 0 || index >= m_anchors.size())
		return;
	PenAnchor& anchor = m_anchors[index];
	if (anchor.smooth)
	{
		anchor.smooth = false;
		anchor.handleIn = anchor.handleOut = anchor.pos;
		return;
	}

	// Corner → smooth needs a direction to pull the handles along. Take it from
	// the neighbours, the way a curve through those points would run; with only
	// one neighbour, aim at it. An isolated anchor has nothing to infer from, so
	// it stays a corner.
	const bool hasPrev = index > 0;
	const bool hasNext = index + 1 < m_anchors.size();
	QPointF direction;
	if (hasPrev && hasNext)
		direction = m_anchors.at(index + 1).pos - m_anchors.at(index - 1).pos;
	else if (hasNext)
		direction = m_anchors.at(index + 1).pos - anchor.pos;
	else if (hasPrev)
		direction = anchor.pos - m_anchors.at(index - 1).pos;
	else
		return;

	const double length = std::hypot(direction.x(), direction.y());
	if (length <= 0.0)
		return;
	// A third of the gap to the nearest neighbour is the usual rule of thumb for
	// a handle that curves without overshooting.
	double reach = length / 3.0;
	if (hasPrev)
		reach = qMin(reach, QLineF(anchor.pos, m_anchors.at(index - 1).pos).length() / 3.0);
	if (hasNext)
		reach = qMin(reach, QLineF(anchor.pos, m_anchors.at(index + 1).pos).length() / 3.0);
	if (reach <= 0.0)
		return;

	const QPointF unit = direction / length;
	anchor.smooth = true;
	anchor.handleOut = anchor.pos + unit * reach;
	anchor.handleIn = anchor.pos - unit * reach;
}

bool PenPathTool::closeHintActive() const
{
	if (m_anchors.size() < 2)
		return false;
	return QLineF(m_cursor, m_anchors.first().pos).length() <= imageDistance(8.0);
}

void PenPathTool::commit()
{
	if (m_editor && m_editor->selection() && m_anchors.size() >= 2)
	{
		const QPainterPath path = buildPath(m_anchors, true);
		const QRectF traced = path.boundingRect();
		// A path with no area at all (two coincident clicks, say) would wipe the
		// selection in Replace mode, so hold it to the same screen-distance bar
		// the marquees use for a stray click.
		const double slop = imageDistance(2.0);
		if (traced.width() >= slop || traced.height() >= slop)
		{
			m_editor->setNextSelectionUndoLabel(tr("Pen Path Selection"));
			m_editor->selection()->setFromPath(path, m_mode);
		}
	}
	cancel();
}

void PenPathTool::cancel()
{
	m_anchors.clear();
	m_dragging = false;
	removePreview();
}

void PenPathTool::updatePreview()
{
	if (!m_editor || !m_editor->scene())
		return;
	if (m_anchors.isEmpty())
	{
		removePreview();
		return;
	}
	QGraphicsScene* scene = m_editor->scene();
	if (!m_pathItem)
	{
		m_pathItem = new PenPathPreviewItem();
		m_pathItem->setZValue(1000);
		scene->addItem(m_pathItem);
	}
	if (!m_handlesItem)
	{
		m_handlesItem = new PenHandlesItem();
		m_handlesItem->setZValue(1002);
		scene->addItem(m_handlesItem);
	}
	m_pathItem->setState(m_anchors, m_cursor, true);
	m_handlesItem->setState(m_anchors, closeHintActive());
}

void PenPathTool::removePreview()
{
	QGraphicsScene* scene = m_editor ? m_editor->scene() : nullptr;
	if (scene && m_pathItem)
		scene->removeItem(m_pathItem);
	delete m_pathItem;
	m_pathItem = nullptr;
	if (scene && m_handlesItem)
		scene->removeItem(m_handlesItem);
	delete m_handlesItem;
	m_handlesItem = nullptr;
}
