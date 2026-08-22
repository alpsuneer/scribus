/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/lassotool.h"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QMouseEvent>
#include <QCursor>
#include <QPen>
#include <QVector>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor LassoTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-select-lasso"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

void LassoTool::mousePress(QMouseEvent* e, const QPointF& imagePos)
{
	if (!m_editor)
		return;
	m_mode = modeFromModifiers(e->modifiers());
	m_path = QPainterPath();
	// A hand-traced outline crosses itself constantly (you loop back over your
	// own line, or the closing segment cuts across the trace). Under the default
	// odd-even rule every such crossing punches a hole in the selection, so the
	// lasso must fill by winding.
	m_path.setFillRule(Qt::WindingFill);
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
	// Subsample: only add a vertex once the cursor has moved a couple of pixels
	// on screen — at 8x zoom a flat 2 image pixels is 16 pixels of cursor travel
	// and the outline visibly lags and comes out faceted.
	if (QLineF(m_last, imagePos).length() < imageDistance(2.0))
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
	// A stray click (or a click with a couple of pixels of hand jitter) traces
	// no area; committing it would silently wipe the current selection. What
	// counts as "no area" is how far the hand moved, so the threshold is a
	// screen distance: three image pixels is a twitch at high zoom but a
	// deliberate drag once a big photo is zoomed out to fit.
	const QRectF traced = m_path.boundingRect();
	const double slop = imageDistance(3.0);
	const bool tooSmall = traced.width() < slop && traced.height() < slop;
	if (m_editor && m_editor->selection() && !tooSmall)
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
	if (!m_previewDark)
	{
		const QVector<qreal> dashes { 4.0, 4.0 };
		QPen dark(Qt::black);
		dark.setCosmetic(true);   // constant width regardless of zoom
		dark.setWidth(1);
		dark.setDashPattern(dashes);
		QPen light(Qt::white);
		light.setCosmetic(true);
		light.setWidth(1);
		light.setDashPattern(dashes);
		light.setDashOffset(4.0);   // interleave with the black dashes

		m_previewDark = m_editor->scene()->addPath(m_path, dark);
		m_previewDark->setZValue(1000);
		m_previewLight = m_editor->scene()->addPath(m_path, light);
		m_previewLight->setZValue(1001);
	}
	else
	{
		m_previewDark->setPath(m_path);
		m_previewLight->setPath(m_path);
	}
}

void LassoTool::removePreview()
{
	QGraphicsScene* scene = m_editor ? m_editor->scene() : nullptr;
	if (scene && m_previewDark)
		scene->removeItem(m_previewDark);
	if (scene && m_previewLight)
		scene->removeItem(m_previewLight);
	delete m_previewDark;
	m_previewDark = nullptr;
	delete m_previewLight;
	m_previewLight = nullptr;
}
