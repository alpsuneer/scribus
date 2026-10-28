/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERMODULEPANEL_H
#define SUNEERMODULEPANEL_H

#include <QDialog>
#include <QPointer>

#include "scribusapi.h"
#include "suneerlayoutengine.h"

class Canvas;
class QLabel;
class QListWidget;
class QPainter;
class ScribusDoc;
class ScribusMainWindow;
class SuneerNewsPanel;

/*!
 \brief Layout > Page Modules...: the modules of the current page as coloured outlines
 on the canvas (News Browser stories, detected stories, obits, blocks, fixed frames),
 with merge / split / keep fixed on the frames selected on the page. The result is
 written on the frames (layout.module, layout.role, layout.fixed), so the next
 re-layout reads it instead of guessing.
 */
class SCRIBUS_API SuneerModulePanel : public QDialog
{
	Q_OBJECT
public:
	static SuneerModulePanel* openFor(ScribusMainWindow* mw, ScribusDoc* doc, SuneerNewsPanel* news);
	//! The outlines (canvas paint, widget coordinates); nothing when the panel is closed.
	static void paintOverlay(QPainter* painter, const Canvas* canvas, ScribusDoc* doc);
	static bool showing(ScribusDoc* doc);

protected:
	void closeEvent(QCloseEvent* e) override;

private:
	SuneerModulePanel(ScribusMainWindow* mw, ScribusDoc* doc, SuneerNewsPanel* news);
	void detect(bool fromScratch);
	void refreshList();
	void repaintCanvas();
	QList<int> selectedModules() const;
	void mergeSelected();
	void splitSelected();
	void setFixed(bool fixed);
	void save();
	void apply();
	void selectModule(int row);

	ScribusMainWindow* m_mw { nullptr };
	QPointer<ScribusDoc> m_doc;
	QPointer<SuneerNewsPanel> m_news;
	int m_page { -1 };
	QList<SuneerLayout::Module> m_modules;
	QListWidget* m_list { nullptr };
	QLabel* m_count { nullptr };
	static QPointer<SuneerModulePanel> s_panel;
};

#endif
