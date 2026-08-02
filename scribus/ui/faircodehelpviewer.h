/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef FAIRCODEHELPVIEWER_H
#define FAIRCODEHELPVIEWER_H

#include <QDialog>
#include <QPointer>

#include "scribusapi.h"

class QTextBrowser;
class QToolButton;

/**
 * \brief In-app help for the Faircode-specific panels.
 *
 * One non-modal window shared by every help button. Content is read from
 * share/help/faircode/faircode-help.html at open time rather than compiled in,
 * so the text can be corrected on a live installation without a rebuild.
 * Each help button passes the anchor of the section it documents.
 */
class SCRIBUS_API FaircodeHelpViewer : public QDialog
{
	Q_OBJECT

public:
	/**
	 * \brief Show \a anchor, raising the existing window if one is already open.
	 * Repeated clicks reuse a single window instead of stacking dialogs.
	 */
	static void showTopic(QWidget* parent, const QString& anchor);

	//! \brief Small flat "?" button for a tab bar corner or a panel header.
	static QToolButton* makeHelpButton(QWidget* parent, const QString& tooltip);

protected:
	explicit FaircodeHelpViewer(QWidget* parent);
	void goToAnchor(const QString& anchor);

private slots:
	void onHomeClicked();

private:
	static QString helpFilePath();
	bool loadContent();

	QTextBrowser* m_browser {nullptr};
	bool m_loaded {false};

	static QPointer<FaircodeHelpViewer> s_instance;
};

#endif // FAIRCODEHELPVIEWER_H
