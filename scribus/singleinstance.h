/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>
#include <QStringList>

#include "scribusapi.h"

class QLocalServer;

/*!
 \brief Makes a second launch hand its files to the running Scribus.

 Double-clicking a .sla started a fresh process every time, so the same document
 could be opened twice in two processes — which is how documents get corrupted.
 ScribusMainWindow::loadDoc() already refuses to open a document twice
 (scribus.cpp, "PV - 5780"), but that guard only sees the MDI windows of ONE
 process. Funnelling every launch into a single process is what makes it
 effective.

 Standard Qt approach: the first instance listens on a QLocalServer; later ones
 connect, send their absolute paths and exit immediately.

 The socket name is per-user AND per-display, so a Kasm desktop and a local X
 session do not hand each other documents.

 Opting out — needed so scribus-debug can run an isolated instance under gdb, and
 so the headless regression batteries never post files into the operator's live
 GUI:
   * SCRIBUS_NO_SINGLE_INSTANCE=1 in the environment
   * --no-single-instance / -nsi on the command line
   * automatically in --no-gui and --python-script runs
 */
class SCRIBUS_API SingleInstance : public QObject
{
	Q_OBJECT

public:
	explicit SingleInstance(QObject* parent = nullptr);
	~SingleInstance() override;

	/*! Hand \a files to an already-running instance.
	    \returns true when they were delivered and this process should exit. */
	static bool sendToRunningInstance(const QStringList& files);

	/*! Become the instance that receives files. Safe to call when another
	    process holds a STALE socket: that is detected and taken over.
	    \returns true if now listening. */
	bool listen();

	//! Per-user, per-display socket name.
	static QString serverName();

	//! True when any opt-out applies (env var, flag, or headless/script mode).
	static bool isDisabled();
	static void setDisabled(bool disabled);

signals:
	/*! Absolute paths arrived from another launch. Connected to the main window,
	    which QUEUES them — it must not open a document from inside this signal,
	    because a modal dialog may be up. */
	void filesReceived(const QStringList& files);

private slots:
	void onNewConnection();

private:
	QLocalServer* m_server { nullptr };
	static bool s_disabled;
};

#endif // SINGLEINSTANCE_H
