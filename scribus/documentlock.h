/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef DOCUMENTLOCK_H
#define DOCUMENTLOCK_H

#include <QDateTime>
#include <QString>

#include "scribusapi.h"

/*!
 \brief InDesign-style lock file, so two PCs cannot edit one .sla at once.

 The office opens documents from a Windows server share over SMB; without this,
 two machines can open the same file and whoever saves last silently destroys
 the other's work.

 \par Why acquire() never checks first
 The lock is taken by ATTEMPTING an exclusive create, never by looking and then
 creating. On SMB, open(O_CREAT|O_EXCL) becomes an SMB2 CREATE with FILE_CREATE
 disposition, which the Windows server arbitrates: of two racing PCs exactly one
 succeeds and the other gets EEXIST. A check-then-create would instead be decided
 by the CIFS client's cached directory metadata (actimeo, ~1s), which can be
 stale — both machines could believe the file was free. The atomic create is the
 decision point; a stale cache can then only make the *message* slightly out of
 date, never admit a second writer.

 \par Staleness uses the file's mtime, not the timestamp inside it
 Every client compares against one clock — the file server's — so office PCs with
 drifting clocks cannot wrongly age a lock. The embedded timestamp is for display
 only.

 \par Known limits on this setup
 - PID only means anything on the machine that wrote the lock, so another
   machine's crash is detectable only by age. Hence takeover is a user decision.
 - A hung PC is indistinguishable from a crashed one.
 - SMB is case-insensitive while Linux path comparison is not, so the lock name
   is built from the lowercased document name: on a case-sensitive volume
   "A.sla" and "a.sla" in one folder would share a lock, which fails safe.
 */
class SCRIBUS_API DocumentLock
{
public:
	//! Who holds a lock, as read back from the file.
	struct Info
	{
		bool      valid { false };
		QString   user;
		QString   host;
		qint64    pid { 0 };
		QDateTime opened;     //!< as written by the holder — display only
		QDateTime fileTime;   //!< lock file mtime (server clock) — staleness math
		QString   raw;
	};

	//! Sibling hidden lock: /share/1.sla -> /share/.1.sla.lock
	static QString lockPathFor(const QString& documentPath);

	/*! Atomically create the lock. \returns true when this process now owns it.
	    On failure \a existing (if given) describes the current holder. */
	static bool acquire(const QString& documentPath, Info* existing = nullptr);

	//! Read a lock without taking it. Info::valid is false when absent/unreadable.
	static Info read(const QString& documentPath);
	//! Same, given the lock file itself.
	static Info readLockFile(const QString& lockPath);
	/*! Startup sweep: delete the locks in \a dirs (and \a subdirDepth levels of
	    subfolders) that THIS user on THIS host wrote and whose process is no
	    longer running. Locks of other users or hosts, and of live processes,
	    are left alone. \returns the lock files removed. */
	static QStringList removeOwnDeadLocksIn(const QStringList& dirs, int subdirDepth = 1);

	/*! Delete the lock, but only when this process actually owns it.
	    Ownership comes from the locks this process created (registerHeld()),
	    so a transiently unreadable lock on the share is still removed; the
	    delete is retried and checked, with a warning if it still fails. */
	static bool release(const QString& documentPath);

	/*! Delete a lock file, retrying briefly: over SMB a delete can fail while
	    another PC has the file open for a moment (e.g. reading who holds it).
	    
eturns true once the file is gone; logs a warning if it is not. */
	static bool removeLockFile(const QString& lockPath);

	//! This process's own lock, for a document it no longer has open.
	static bool isMineThisProcess(const Info& info);

	/*! Tie a held lock to an open document. The document's file name can change
	    (Save As, an imported file shown as "name(converted)"), so closing it
	    releases the lock it was opened with, not whatever its name is now. */
	static void bindDocument(const void* doc, const QString& documentPath);
	//! Release the lock bound to \a doc, if any. \returns false if none was bound.
	static bool releaseDocument(const void* doc);
	//! The document path \a doc's lock was taken for, or empty.
	static QString boundPath(const void* doc);

	//! Remove every lock this process still holds. For normal application exit.
	static void releaseAll();

	//! Remove someone else's lock and take it. Only ever on explicit user action.
	static bool takeOver(const QString& documentPath);

	//! Same user and same host as this process.
	static bool isMine(const Info& info);

	/*! Mine, and the recorded PID is no longer running — safe to reclaim
	    silently, which is the only automatic takeover. */
	static bool isMineAndDead(const Info& info);

	//! Lock file older than \a hours, by its mtime.
	static bool isStale(const Info& info, int hours);

	//! Human-readable "suneer on PC-DESK2 since 14:32".
	static QString describe(const Info& info);

	static QString currentUser();
	static QString currentHost();

	//! Default age past which a foreign lock may be offered for takeover.
	static const int DefaultStaleHours = 8;

	/*! Install SIGTERM/SIGINT/SIGHUP handlers that unlink any held locks, and
	    an atexit() hook that does the same on any normal exit.
	    Without this only File > Quit releases: a killed or crashed Scribus
	    leaves its lock behind, which was observed leaking five locks across one
	    regression battery run. Fatal signals (SIGSEGV and friends) are left
	    alone so the existing crash handler and core dumps are unaffected. */
	static void installSignalCleanup();
	//! True if this process created \a lockPath and has not released it.
	static bool isHeld(const QString& lockPath);

	//! Track/untrack a held lock so the signal handler can remove it.
	static void registerHeld(const QString& lockPath);
	static void unregisterHeld(const QString& lockPath);
};

#endif // DOCUMENTLOCK_H
