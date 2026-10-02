/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERLINKEDIMAGESDIALOG_H
#define SUNEERLINKEDIMAGESDIALOG_H

#include <QDialog>
#include <QList>

#include "scribusapi.h"
#include "suneerimagelinks.h"

class QCheckBox;

/*!
 * \brief Shown before Save, PDF export, Print and Proof Print when the document
 * has linked or missing images: lists them and asks what to do.
 */
class SCRIBUS_API SuneerLinkedImagesDialog : public QDialog
{
	Q_OBJECT
public:
	enum Choice { Cancel, EmbedAndContinue, ContinueAnyway };

	//! \a action: "Save", "PDF export", "Print", "Proof Print" (already translated)
	SuneerLinkedImagesDialog(QWidget* parent, const QString& action, const QList<SuneerImageLinks::Entry>& entries);

	Choice choice() const { return m_choice; }
	bool dontAskAgain() const;

private:
	Choice m_choice { Cancel };
	QCheckBox* m_dontAsk { nullptr };
};

#endif
