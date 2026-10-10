/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEEROPTIMIZEIMAGESDIALOG_H
#define SUNEEROPTIMIZEIMAGESDIALOG_H

#include <QDialog>
#include <QList>
#include <QPointer>

#include "scribusapi.h"

class PageItem;
class QLabel;
class QSpinBox;
class QTableWidget;
class ScribusDoc;

/*!
 \brief "Optimize Images...": every placed picture (selection or whole
 document) with its pixels, effective dpi and size; the ones above the target
 dpi are ticked; Apply fits each ticked one to its frame at the target dpi
 (SuneerImageResample::fitToFrame), one undo step for the lot.
 */
class SCRIBUS_API SuneerOptimizeImagesDialog : public QDialog
{
	Q_OBJECT
public:
	SuneerOptimizeImagesDialog(ScribusDoc* doc, const QList<PageItem*>& items, QWidget* parent = nullptr);
	//! Selected image frames, or all of the document's when none is selected.
	static void openFor(ScribusDoc* doc, QWidget* parent);

private:
	void fill();
	void retick();
	void apply();

	ScribusDoc* m_doc { nullptr };
	QList<QPointer<PageItem>> m_items;
	QTableWidget* m_table { nullptr };
	QSpinBox* m_targetSpin { nullptr };
	QLabel* m_totals { nullptr };
	QLabel* m_status { nullptr };
};

#endif
