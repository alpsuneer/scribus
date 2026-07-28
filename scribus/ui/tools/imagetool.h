/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMAGETOOL_H
#define IMAGETOOL_H

#include <QCursor>
#include <QIcon>
#include <QObject>
#include <QPointF>

#include "scribusapi.h"
#include "scimageselection.h"

class QKeyEvent;
class QMouseEvent;
class QWidget;
class ScImageEditor;

/*!
 \brief Abstract base for interactive image-editor tools.

 An active tool receives mouse/key events (in image-pixel coordinates) routed
 from the canvas view. Existing built-in tools (Move/Zoom/Hand/…) keep their
 legacy in-view handling; new tools — the selection marquees and lassos — are
 implemented as ImageTool subclasses. A future SAM "Smart Select" tool slots in
 the same way, writing its result to editor->selection().
 */
class SCRIBUS_API ImageTool : public QObject
{
	Q_OBJECT

public:
	explicit ImageTool(QObject* parent = nullptr) : QObject(parent) {}
	~ImageTool() override = default;

	virtual QString name() const = 0;
	virtual QIcon icon() const { return QIcon(); }
	virtual QCursor cursor() const { return Qt::CrossCursor; }
	virtual QWidget* optionsBar() { return nullptr; }

	virtual void activate(ScImageEditor* editor) { m_editor = editor; }
	virtual void deactivate() {}

	virtual void mousePress(QMouseEvent* e, const QPointF& imagePos) = 0;
	virtual void mouseMove(QMouseEvent* e, const QPointF& imagePos) = 0;
	virtual void mouseRelease(QMouseEvent* e, const QPointF& imagePos) = 0;
	virtual void mouseDoubleClick(QMouseEvent* e, const QPointF& imagePos) { mousePress(e, imagePos); }
	virtual void keyPress(QKeyEvent* e) { Q_UNUSED(e) }

protected:
	ScImageEditor* m_editor { nullptr };

	//! Selection combination mode from Shift/Alt modifiers (Photoshop convention).
	ScImageSelection::Mode modeFromModifiers(Qt::KeyboardModifiers mods) const;
};

#endif // IMAGETOOL_H
