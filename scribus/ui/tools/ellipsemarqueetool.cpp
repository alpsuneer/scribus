/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/ellipsemarqueetool.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QCursor>
#include <QPen>

#include "iconmanager.h"
#include "scimageselection.h"
#include "ui/scimageeditor.h"

//! The crosshair every selection tool used to share told the user nothing about
//! which one was active. Each now carries the shared crosshair plus its own
//! badge; the hotspot stays on the crosshair centre so precision is unchanged.
QCursor EllipseMarqueeTool::cursor() const
{
	const QCursor c = IconManager::instance().loadCursor(QStringLiteral("cursor-select-ellipse"), 15, 15);
	// A missing icon id yields a null pixmap, and QCursor turns that into a plain
	// arrow — worse than the crosshair it replaced — so fall back explicitly.
	return c.pixmap().isNull() ? QCursor(Qt::CrossCursor) : c;
}

void EllipseMarqueeTool::createRubber(const QRectF& r)
{
	if (!m_editor || !m_editor->scene())
		return;
	removeRubber();
	QPen p(Qt::DashLine);
	p.setColor(Qt::white);
	p.setCosmetic(true);
	auto* item = m_editor->scene()->addEllipse(r, p);
	item->setZValue(1000);
	m_rubber = item;
}

void EllipseMarqueeTool::setRubber(const QRectF& r)
{
	if (auto* item = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_rubber))
		item->setRect(r);
}

void EllipseMarqueeTool::commit(const QRect& r, ScImageSelection::Mode mode)
{
	m_editor->setNextSelectionUndoLabel(tr("Elliptical Selection"));
	m_editor->selection()->setFromEllipse(r, mode);
}
