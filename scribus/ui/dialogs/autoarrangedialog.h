/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOARRANGEDIALOG_H
#define AUTOARRANGEDIALOG_H

#include <QDialog>

#include "scribusapi.h"
#include "autoarrangeengine.h"

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class ScribusDoc;

/*!
 \brief Dry-run reporter for "Auto Arrange Frames".

 The plan is always computed and shown BEFORE anything is written: the table
 lists every frame that would move with its from/to position, and the document
 is only touched if the operator presses Apply. Changing the gutter recomputes
 the plan in place, so the numbers can be checked before committing.

 This dialog replaced an options dialog whose defaults silently rewrote the
 page; showing the work first is the point of it.
 */
class SCRIBUS_API AutoArrangeDialog : public QDialog
{
	Q_OBJECT

public:
	AutoArrangeDialog(ScribusDoc* doc, QWidget* parent = nullptr);

	//! The plan as last shown — valid after exec() returns Accepted.
	const ArrangePlan& plan() const { return m_plan; }

private slots:
	void recompute();

private:
	void populate();

	ScribusDoc*     m_doc { nullptr };
	ArrangePlan     m_plan;

	QLabel*         m_summary { nullptr };
	QDoubleSpinBox* m_gutter { nullptr };
	QTableWidget*   m_table { nullptr };
	QPushButton*    m_apply { nullptr };
};

#endif // AUTOARRANGEDIALOG_H
