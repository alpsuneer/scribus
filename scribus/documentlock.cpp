/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "documentlock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QThread>
#include <QtDebug>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace
{
	// Held locks, as plain C strings. A signal handler may only call
	// async-signal-safe functions, so no QString, no allocation, no locking —
	// just unlink(), which is on the safe list.
	const int    MaxHeldLocks = 32;
	const int    MaxLockPath  = 4096;
	char         g_heldLocks[MaxHeldLocks][MaxLockPath];
	volatile sig_atomic_t g_heldCount = 0;
	bool         g_handlersInstalled = false;

	void releaseHeldLocksAsync()
	{
		for (int i = 0; i < g_heldCount && i < MaxHeldLocks; ++i)
		{
			if (g_heldLocks[i][0] != '\0')
				::unlink(g_heldLocks[i]);
		}
	}

	// Document -> the path its lock was taken for. Main thread only.
	QHash<const void*, QString>& docLocks()
	{
		static QHash<const void*, QString> map;
		return map;
	}

	void releaseHeldLocksAtExit()
	{
		releaseHeldLocksAsync();
	}

	void lockSignalHandler(int sig)
	{
		releaseHeldLocksAsync();
		// Re-raise with the default action so the process still terminates the
		// way it would have, and any core dump is unchanged.
		::signal(sig, SIG_DFL);
		::raise(sig);
	}
}

QString DocumentLock::currentUser()
{
	const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	QString u = env.value("USER");
	if (u.isEmpty())
		u = env.value("LOGNAME");
	if (u.isEmpty())
		u = QString::number(::geteuid());
	return u;
}

QString DocumentLock::currentHost()
{
	QString h = QHostInfo::localHostName();
	if (h.isEmpty())
		h = QStringLiteral("unknown-host");
	return h;
}

QString DocumentLock::lockPathFor(const QString& documentPath)
{
	const QFileInfo fi(documentPath);
	// Lowercased: SMB/Windows is case-insensitive, so "1.SLA" and "1.sla" are one
	// document and must share one lock. On a case-sensitive volume this makes two
	// files differing only in case share a lock, which fails safe.
	return fi.absolutePath() + QStringLiteral("/.") + fi.fileName().toLower() + QStringLiteral(".lock");
}

bool DocumentLock::acquire(const QString& documentPath, Info* existing)
{
	const QString lockPath = lockPathFor(documentPath);

	// Exclusive create IS the decision. Never look before leaping: see the
	// header for why check-then-create is unsafe over SMB.
	const QByteArray native = QFile::encodeName(lockPath);
	int fd = ::open(native.constData(), O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0)
	{
		if (errno == EEXIST && existing)
			*existing = read(documentPath);
		return false;
	}

	QString payload;
	QTextStream ts(&payload);
	ts << "scribus-lock 1\n"
	   << "user=" << currentUser() << '\n'
	   << "host=" << currentHost() << '\n'
	   << "pid=" << QCoreApplication::applicationPid() << '\n'
	   << "opened=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n'
	   << "document=" << QFileInfo(documentPath).fileName() << '\n';
	const QByteArray bytes = payload.toUtf8();
	const ssize_t written = ::write(fd, bytes.constData(), bytes.size());
	::close(fd);

	if (written != bytes.size())
	{
		// A half-written lock would name nobody; better to hold none at all.
		QFile::remove(lockPath);
		return false;
	}
	registerHeld(lockPath);
	return true;
}

DocumentLock::Info DocumentLock::read(const QString& documentPath)
{
	return readLockFile(lockPathFor(documentPath));
}

QStringList DocumentLock::removeOwnDeadLocksIn(const QStringList& dirs, int subdirDepth)
{
	QStringList removed;
	QStringList todo = dirs;
	QStringList seen;
	for (int level = 0; level <= subdirDepth && !todo.isEmpty(); ++level)
	{
		QStringList next;
		for (const QString& d : std::as_const(todo))
		{
			const QString abs = QDir(d).absolutePath();
			if (abs.isEmpty() || seen.contains(abs) || !QDir(abs).exists())
				continue;
			seen << abs;
			QDir dir(abs);
			const QStringList locks = dir.entryList({ QStringLiteral(".*.lock") }, QDir::Files | QDir::Hidden);
			for (const QString& name : locks)
			{
				const QString lockPath = dir.filePath(name);
				if (isHeld(lockPath))
					continue;                   // ours, this very process
				const Info info = readLockFile(lockPath);
				if (!isMineAndDead(info))
					continue;                   // someone else's, or a live process of ours
				if (QFile::remove(lockPath))
					removed << lockPath;
			}
			const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
			for (const QString& sdir : subs)
				next << dir.filePath(sdir);
		}
		todo = next;
	}
	return removed;
}

DocumentLock::Info DocumentLock::readLockFile(const QString& lockPath)
{
	Info info;
	QFileInfo lfi(lockPath);
	if (!lfi.exists())
		return info;

	// mtime comes from the file server, so every client ages a lock against the
	// same clock rather than its own.
	info.fileTime = lfi.lastModified();

	QFile f(lockPath);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return info;
	const QString text = QString::fromUtf8(f.readAll());
	f.close();
	info.raw = text;

	const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
	for (const QString& line : lines)
	{
		const int eq = line.indexOf('=');
		if (eq <= 0)
			continue;
		const QString key = line.left(eq).trimmed();
		const QString val = line.mid(eq + 1).trimmed();
		if (key == QLatin1String("user"))        info.user = val;
		else if (key == QLatin1String("host"))   info.host = val;
		else if (key == QLatin1String("pid"))    info.pid = val.toLongLong();
		else if (key == QLatin1String("opened")) info.opened = QDateTime::fromString(val, Qt::ISODate);
	}
	// A lock naming nobody is worse than none: treat it as present but unknown,
	// so the user still gets a warning rather than silent shared editing.
	info.valid = true;
	return info;
}

bool DocumentLock::isMine(const Info& info)
{
	if (!info.valid)
		return false;
	return info.user == currentUser() && info.host == currentHost();
}

bool DocumentLock::isMineAndDead(const Info& info)
{
	if (!isMine(info) || info.pid <= 0)
		return false;
	if (info.pid == QCoreApplication::applicationPid())
		return false;                       // that is us, right now
	// Only meaningful because the host matches: a PID from another machine says
	// nothing about anything running here.
	return ::kill(static_cast<pid_t>(info.pid), 0) != 0 && errno == ESRCH;
}

bool DocumentLock::isStale(const Info& info, int hours)
{
	if (!info.valid || !info.fileTime.isValid() || hours <= 0)
		return false;
	return info.fileTime.secsTo(QDateTime::currentDateTime()) > qint64(hours) * 3600;
}

QString DocumentLock::describe(const Info& info)
{
	if (!info.valid)
		return QString();
	const QString who  = info.user.isEmpty() ? QStringLiteral("someone") : info.user;
	const QString host = info.host.isEmpty() ? QStringLiteral("another machine") : info.host;
	const QDateTime when = info.opened.isValid() ? info.opened : info.fileTime;
	if (!when.isValid())
		return QStringLiteral("%1 on %2").arg(who, host);
	// Same day: just the time, as in "since 14:32". Otherwise include the date,
	// so a lock left on Friday does not read as though it were taken minutes ago.
	const QDateTime now = QDateTime::currentDateTime();
	const QString stamp = (when.date() == now.date())
	                    ? QLocale().toString(when.time(), QLocale::ShortFormat)
	                    : QLocale().toString(when, QLocale::ShortFormat);
	return QStringLiteral("%1 on %2 since %3").arg(who, host, stamp);
}

bool DocumentLock::release(const QString& documentPath)
{
	const QString lockPath = lockPathFor(documentPath);
	const Info info = read(documentPath);
	const bool ours = info.valid && isMine(info) && info.pid == QCoreApplication::applicationPid();
	if (isHeld(lockPath))
	{
		// We created this lock. Remove it unless the file now names someone
		// else (they took it over from us); a lock that cannot be read right
		// now over the share is still ours and must not be left behind.
		unregisterHeld(lockPath);
		if (info.valid && !info.raw.isEmpty() && !ours)
			return false;
		return removeLockFile(lockPath);
	}
	// Only ever remove our own lock. Deleting another machine's on close would
	// undo the whole point.
	if (!ours)
		return false;
	return removeLockFile(lockPath);
}

bool DocumentLock::removeLockFile(const QString& lockPath)
{
	// A delete over SMB can fail while another PC briefly has the file open
	// (reading who holds it); the server refuses it with a sharing violation.
	// Retry for a little under a second, and always check it is really gone.
	const int delaysMs[] = { 0, 50, 100, 200, 400 };
	QString lastError;
	for (int delay : delaysMs)
	{
		if (delay > 0)
			QThread::msleep(delay);
		if (!QFile::exists(lockPath))
			return true;
		QFile f(lockPath);
		if (f.remove() && !QFile::exists(lockPath))
			return true;
		lastError = f.errorString();
	}
	if (!QFile::exists(lockPath))
		return true;
	qWarning("DocumentLock: could not remove lock file %s after 5 attempts (%s); other PCs will see this document as in use",
	         qPrintable(QDir::toNativeSeparators(lockPath)), qPrintable(lastError));
	return false;
}

bool DocumentLock::isMineThisProcess(const Info& info)
{
	return isMine(info) && info.pid == QCoreApplication::applicationPid();
}

void DocumentLock::bindDocument(const void* doc, const QString& documentPath)
{
	if (doc)
		docLocks().insert(doc, documentPath);
}

bool DocumentLock::releaseDocument(const void* doc)
{
	if (!docLocks().contains(doc))
		return false;
	const QString path = docLocks().take(doc);
	release(path);
	return true;
}

QString DocumentLock::boundPath(const void* doc)
{
	return docLocks().value(doc);
}

void DocumentLock::releaseAll()
{
	const QList<QString> paths = docLocks().values();
	docLocks().clear();
	for (const QString& path : paths)
		release(path);
	releaseHeldLocksAsync();   // anything else still registered
}

bool DocumentLock::takeOver(const QString& documentPath)
{
	const QString lockPath = lockPathFor(documentPath);
	if (!removeLockFile(lockPath))
		return false;
	// Re-acquire exclusively: if another machine got in between the remove and
	// here, it keeps the lock and we fail safe rather than both writing.
	return acquire(documentPath);
}

void DocumentLock::registerHeld(const QString& lockPath)
{
	const QByteArray native = QFile::encodeName(lockPath);
	if (native.size() >= MaxLockPath)
		return;
	for (int i = 0; i < g_heldCount && i < MaxHeldLocks; ++i)
		if (::strcmp(g_heldLocks[i], native.constData()) == 0)
			return;                      // already tracked
	if (g_heldCount >= MaxHeldLocks)
		return;                          // more than 32 open documents: skip
	::strncpy(g_heldLocks[g_heldCount], native.constData(), MaxLockPath - 1);
	g_heldLocks[g_heldCount][MaxLockPath - 1] = '\0';
	++g_heldCount;
}

bool DocumentLock::isHeld(const QString& lockPath)
{
	const QByteArray native = QFile::encodeName(lockPath);
	for (int i = 0; i < g_heldCount && i < MaxHeldLocks; ++i)
		if (::strcmp(g_heldLocks[i], native.constData()) == 0)
			return true;
	return false;
}

void DocumentLock::unregisterHeld(const QString& lockPath)
{
	const QByteArray native = QFile::encodeName(lockPath);
	for (int i = 0; i < g_heldCount && i < MaxHeldLocks; ++i)
	{
		if (::strcmp(g_heldLocks[i], native.constData()) != 0)
			continue;
		// Compact: move the last entry into this slot.
		if (i != g_heldCount - 1)
			::strncpy(g_heldLocks[i], g_heldLocks[g_heldCount - 1], MaxLockPath - 1);
		g_heldLocks[g_heldCount - 1][0] = '\0';
		--g_heldCount;
		return;
	}
}

void DocumentLock::installSignalCleanup()
{
	if (g_handlersInstalled)
		return;
	g_handlersInstalled = true;
	// Only termination signals. SIGSEGV/SIGABRT are deliberately untouched:
	// scribus-debug relies on them reaching gdb, and SCRIBUS_NO_CRASH_HANDLER
	// exists precisely so they are not intercepted.
	::signal(SIGTERM, lockSignalHandler);
	::signal(SIGINT,  lockSignalHandler);
	::signal(SIGHUP,  lockSignalHandler);
	// Any normal exit, including one that skips closing documents one by one.
	std::atexit(releaseHeldLocksAtExit);
}
