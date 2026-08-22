/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/rectmarqueetool.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QCursor>
#include <QPen>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor RectMarqueeTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-select-rect"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

namespace
{
	QPen rubberPen()
	{
		QPen p(Qt::DashLine);
		p.setColor(Qt::white);
		p.setCosmetic(true);
		return p;
	}
}

void RectMarqueeTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor)
		return;
	m_start = imagePos;
	m_mode = modeFromModifiers(e->modifiers());
	m_active = true;
	createRubber(QRectF(m_start, m_start));
}

void RectMarqueeTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_active)
		return;
	setRubber(QRectF(m_start, imagePos).normalized());
}

void RectMarqueeTool::mouseRelease(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_active)
		return;
	m_active = false;
	QRectF r = QRectF(m_start, imagePos).normalized();
	removeRubber();
	if (!m_editor || !m_editor->selection())
		return;
	// Stray click — leave the selection unchanged. The threshold is how far the
	// hand moved, so it is a screen distance: a flat 2 image pixels is a tenth
	// of a pixel of cursor travel at 20x, and 10 pixels of travel on a big photo
	// zoomed out to fit, where an ordinary click-jitter would wipe the selection.
	const double slop = imageDistance(2.0);
	if (r.width() < slop || r.height() < slop)
		return;
	QRect ir = r.toRect().intersected(QRect(QPoint(0, 0), m_editor->selection()->size()));
	if (ir.width() < 1 || ir.height() < 1)
		return;
	commit(ir, m_mode);
}

void RectMarqueeTool::deactivate()
{
	m_active = false;
	removeRubber();
}

void RectMarqueeTool::createRubber(const QRectF& r)
{
	if (!m_editor || !m_editor->scene())
		return;
	removeRubber();
	auto* item = m_editor->scene()->addRect(r, rubberPen());
	item->setZValue(1000);
	m_rubber = item;
}

void RectMarqueeTool::setRubber(const QRectF& r)
{
	if (auto* item = qgraphicsitem_cast<QGraphicsRectItem*>(m_rubber))
		item->setRect(r);
}

void RectMarqueeTool::commit(const QRect& r, ScImageSelection::Mode mode)
{
	m_editor->setNextSelectionUndoLabel(tr("Rectangular Selection"));
	m_editor->selection()->setFromRect(r, mode);
}

void RectMarqueeTool::removeRubber()
{
	if (m_rubber && m_editor && m_editor->scene())
		m_editor->scene()->removeItem(m_rubber);
	delete m_rubber;
	m_rubber = nullptr;
}
