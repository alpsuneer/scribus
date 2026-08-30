/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITRANSLATEDIALOG_H
#define AITRANSLATEDIALOG_H

#include <QDialog>
#include <QString>

#include "scribusapi.h"

class QComboBox;

/*!
 \brief Which language to translate into.

 The source language is not asked for. Every one of these models detects it
 from the text far more reliably than a user picking from a list of a hundred,
 and getting it wrong is the one way to make the translation silently useless.

 The list is ordered for this newsroom rather than alphabetically: Malayalam
 and English first because that is nearly every job, then the neighbouring
 Indian languages, then the handful of world languages that come up.
 */
class SCRIBUS_API AITranslateDialog : public QDialog
{
	Q_OBJECT

public:
	explicit AITranslateDialog(QWidget* parent, const QString& initialTarget = QString());

	//! The language name to put in the prompt, e.g. "Malayalam".
	QString targetLanguage() const;

	//! The offered languages, in the order they are shown.
	static QStringList languages();

private:
	QComboBox* m_target {nullptr};
};

#endif
