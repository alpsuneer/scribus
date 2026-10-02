/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef NETPATHGUARD_H
#define NETPATHGUARD_H

#include <QString>
#include <QStringList>

#include "scribusapi.h"

/*!
 \brief Keeps the GUI thread from blocking on a dead network folder.

 The office documents live on an NFS/SMB share. When the server is unreachable
 a "hard" mount does not fail — every stat() on it simply never returns, and a
 stat() cannot be given a timeout. So the stat() is done in a helper thread and
 the caller waits for at most \a timeoutMs. If no answer comes, the folder is
 remembered as unreachable ("skipped") and everything below it is refused at
 once, without waiting again.

 \par What "reachable" means
 The path ANSWERED in time — it may still not exist. Callers keep their own
 exists() test; this only promises that the test will not hang.

 \par Finding the folder to skip
 The helper thread walks the path from the root down (/home, /home/s1, ...), so
 the component it is stuck on is the top of the dead mount (or the symlink that
 leads into it), not just the one file that was asked for.

 \par Recovery
 The stuck helper thread is left alone; when the server comes back its stat()
 returns and the folder stops being skipped. Nothing polls.
 */
class SCRIBUS_API NetPathGuard
{
public:
	static const int DefaultTimeoutMs = 2000;

	/*! Start checking \a paths without waiting. Used at startup so the wait
	    runs alongside the rest of initialisation instead of after it. */
	static void precheck(const QStringList& paths);

	/*! True when \a path answered within \a timeoutMs, counted from when its
	    check started (a precheck() already under way is not restarted).
	    Never blocks longer than \a timeoutMs. An empty path is reachable. */
	static bool reachable(const QString& path, int timeoutMs = DefaultTimeoutMs);

	//! No waiting: is \a path inside a folder already found unreachable?
	static bool isSkipped(const QString& path);

	//! \a paths without the unreachable ones; one shared wait for the whole list.
	static QStringList reachableOnly(const QStringList& paths, int timeoutMs = DefaultTimeoutMs);

	//! Folders currently skipped.
	static QStringList skippedFolders();

	/*! Folders found unreachable since the last call, for the status-bar note
	    ("Network folder not reachable: <path> skipped"). Each folder is
	    handed out once, until it has answered again. */
	static QStringList takeNewlySkipped();
};

#endif // NETPATHGUARD_H
