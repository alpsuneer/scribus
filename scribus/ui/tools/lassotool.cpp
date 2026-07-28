/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/lassotool.h"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPen>
#include <QtMath>

#include "scimageselection.h"
#include "ui/scimageeditor.h"

void LassoTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor)
		return;
	m_mode = modeFromModifiers(e->modifiers());
	m_path = QPainterPath();
	m_path.moveTo(imagePos);
	m_last = imagePos;
	m_active = true;
	updatePreview();
}

void LassoTool::mouseMove(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_active)
		return;
	// Subsample: only add a vertex once the cursor has moved a couple of pixels.
	if (QLineF(m_last, imagePos).length() < 2.0)
		return;
	m_path.lineTo(imagePos);
	m_last = imagePos;
	updatePreview();
}

void LassoTool::mouseRelease(QMouseEvent* e, const QPointF& imagePos)
{
	Q_UNUSED(e)
	if (!m_active)
		return;
	m_active = false;
	m_path.lineTo(imagePos);
	m_path.closeSubpath();
	removePreview();
	if (m_editor && m_editor->selection() && m_path.elementCount() > 2)
	{
		m_editor->setNextSelectionUndoLabel(tr("Lasso Selection"));
		m_editor->selection()->setFromPath(m_path, m_mode);
	}
	m_path = QPainterPath();
}

void LassoTool::deactivate()
{
	m_active = false;
	m_path = QPainterPath();
	removePreview();
}

void LassoTool::updatePreview()
{
	if (!m_editor || !m_editor->scene())
		return;
	if (!m_preview)
	{
		QPen p(Qt::DashLine);
		p.setColor(Qt::white);
		p.setCosmetic(true);
		m_preview = m_editor->scene()->addPath(m_path, p);
		m_preview->setZValue(1000);
	}
	else
	{
		m_preview->setPath(m_path);
	}
}

void LassoTool::removePreview()
{
	if (m_preview && m_editor && m_editor->scene())
		m_editor->scene()->removeItem(m_preview);
	delete m_preview;
	m_preview = nullptr;
}
