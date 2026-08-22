/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/polygonlassotool.h"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QCursor>
#include <QPen>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor PolygonLassoTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-polygon"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

void PolygonLassoTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor)
		return;
	if (m_anchors.isEmpty())
		m_mode = modeFromModifiers(e->modifiers());
	m_anchors.append(imagePos);
	m_cursor = imagePos;
	updatePreview();
}

void PolygonLassoTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (m_anchors.isEmpty())
		return;
	m_cursor = imagePos;
	updatePreview();
}

void PolygonLassoTool::mouseDoubleClick(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	Q_UNUSED(imagePos)
	commit();
}

void PolygonLassoTool::keyPress(QKeyEvent* e)
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
				updatePreview();
			}
			e->accept();
			break;
		default:
			break;
	}
}

void PolygonLassoTool::deactivate()
{
	cancel();
}

void PolygonLassoTool::commit()
{
	if (m_editor && m_editor->selection() && m_anchors.size() >= 3)
	{
		QPainterPath path(m_anchors.first());
		// Same reason as the freehand lasso: a self-crossing outline would lose
		// whole wedges under the default odd-even fill rule.
		path.setFillRule(Qt::WindingFill);
		for (int i = 1; i < m_anchors.size(); ++i)
			path.lineTo(m_anchors.at(i));
		path.closeSubpath();
		m_editor->setNextSelectionUndoLabel(tr("Polygon Selection"));
		m_editor->selection()->setFromPath(path, m_mode);
	}
	cancel();
}

void PolygonLassoTool::cancel()
{
	m_anchors.clear();
	removePreview();
}

void PolygonLassoTool::updatePreview()
{
	if (!m_editor || !m_editor->scene() || m_anchors.isEmpty())
		return;
	QPainterPath path(m_anchors.first());
	for (int i = 1; i < m_anchors.size(); ++i)
		path.lineTo(m_anchors.at(i));
	path.lineTo(m_cursor);   // rubber-band segment to the cursor

	if (!m_preview)
	{
		QPen p(Qt::DashLine);
		p.setColor(Qt::white);
		p.setCosmetic(true);
		m_preview = m_editor->scene()->addPath(path, p);
		m_preview->setZValue(1000);
	}
	else
	{
		m_preview->setPath(path);
	}
}

void PolygonLassoTool::removePreview()
{
	if (m_preview && m_editor && m_editor->scene())
		m_editor->scene()->removeItem(m_preview);
	delete m_preview;
	m_preview = nullptr;
}
