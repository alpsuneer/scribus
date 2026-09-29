/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
						main.cpp  -  description
							-------------------
	begin                : Fre Apr  6 21:47:55 CEST 2001
	copyright            : (C) 2001 by Franz Schmid
	email                : Franz.Schmid@altmuehlnet.de
	copyright            : (C) 2004 by Alessandro Rimoldi
	email                : http://ideale.ch/contact
	copyright            : (C) 2005 by Craig Bradney
	email                : cbradney@scribus.info
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include <iostream>
#include <csignal>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <unistd.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QImageReader>
#include <QMessageBox>
#include <QStandardPaths>

#include "scribusapp.h"
#include "scribuscore.h"
#include "scribus.h"
#include "scimagecachemanager.h"
#include "util.h"

#include "scconfig.h"

int mainApp(int argc, char **argv);
void initCrashHandler();
static void defaultCrashHandler(int sig, siginfo_t* info, void* context);

ScribusCore SCRIBUS_API *ScCore;
ScribusQApp SCRIBUS_API *ScQApp;
bool emergencyActivated;

int main(int argc, char *argv[])
{
	return mainApp(argc, argv);
}

/*!
\author Franz Schmid
\author Alessandro Rimoldi
\author Craig Bradney
\date Mon Feb  9 14:07:46 CET 2004
\brief Launches the Gui
\param argc Number of arguments passed to Scribus
\param argv *argv list of the arguments passed to Scribus
\retval int Error code from the execution of Scribus
*/
int mainApp(int argc, char **argv)
{
	emergencyActivated = false;

#if !defined(Q_OS_MACOS)
	qputenv("QT_QPA_PLATFORM", "xcb");
#endif

	QImageReader::setAllocationLimit(1024);

	ScribusQApp app(argc, argv);
	initCrashHandler();
	// Before this session spawns any gs of its own: a prior session that was
	// force-killed (or OOM-killed, or crashed outside the handler above)
	// leaves its Ghostscript preview/separations child orphaned and still
	// burning a full core. Left alone these accumulate across sessions and
	// starve every later render. See util.cpp for the matching criteria.
	reapOrphanedGhostscriptChildren();
	app.parseCommandLine();
	
	if (QApplication::platformName() == "wayland")
	{
		QString errHdr = QObject::tr("Fatal Error");
		QString errMsg = QObject::tr("Scribus does not support the Wayland platform. Use XWayland to run Scribus on Wayland. Scribus will close now.");
		if (app.useGUI)
		{
			QMessageBox::critical(nullptr, errHdr, errMsg);
		}
		else
		{
			std::cout << errHdr.toStdString() << std::endl;
			std::cout << "-------------" << std::endl;
			std::cout << errMsg.toStdString() << std::endl;
		}
		return EXIT_FAILURE;
	}
	
	int appRetVal = app.init();
	if (appRetVal == EXIT_FAILURE)
		return(EXIT_FAILURE);
	// The files went to an already-running instance, so there is no window here
	// and nothing to pump: entering exec() would hang as an invisible process.
	if (app.handedOff())
		return EXIT_SUCCESS;
	if (app.useGUI)
		return app.exec();
	return EXIT_SUCCESS;
}

/* --------------------------------------------------------------------------
   Crash capture.

   Everything from here down runs in signal context, where almost nothing is
   legal: no malloc, no printf, no Qt. The rule that shapes this code is that
   the evidence is written FIRST, with raw write(2), before any call that could
   fault or block; only then do we try the risky-but-valuable emergency save;
   and finally we re-raise so the kernel still writes a core.

   The previous version did the opposite: a modal dialog (which blocks forever
   waiting for a click nobody can give), then emergencySave(), then exit(255),
   which suppresses the core. It wrote no log at all, so a crash that the
   handler processed successfully left no trace anywhere on the system.
   -------------------------------------------------------------------------- */

#if !defined(Q_OS_OPENBSD) && !defined(Q_OS_FREEBSD)
#define SCRIBUS_HAVE_EXECINFO 1
#include <execinfo.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char **environ;

static char    s_crashLogPath[PATH_MAX];	//!< rendered at init; the handler must not allocate
static char    s_notifyPath[PATH_MAX];		//!< absolute path to notify-send, empty if unavailable
static void*   s_btBuf[64];					//!< pre-allocated backtrace frames
static stack_t s_altStack;

/*! \brief write(2) a NUL-terminated string. Async-signal-safe. */
static void asWrite(int fd, const char* s)
{
	size_t n = 0;
	while (s[n])
		++n;
	// Nothing useful can be done about a short write from a crash handler.
	ssize_t written = ::write(fd, s, n);
	(void) written;
}

/*! \brief Write an unsigned value in the given base. Async-signal-safe. */
static void asWriteUnsigned(int fd, unsigned long v, unsigned long base)
{
	char buf[32];
	int i = (int) sizeof(buf);
	buf[--i] = '\0';
	if (v == 0)
		buf[--i] = '0';
	while (v != 0 && i > 0)
	{
		unsigned long d = v % base;
		buf[--i] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
		v /= base;
	}
	asWrite(fd, buf + i);
}

/*! \brief Write a signed decimal value. Async-signal-safe. */
static void asWriteSigned(int fd, long v)
{
	if (v < 0)
	{
		asWrite(fd, "-");
		asWriteUnsigned(fd, (unsigned long) (-v), 10);
		return;
	}
	asWriteUnsigned(fd, (unsigned long) v, 10);
}

/*! \brief Write the crash record to an open fd. Async-signal-safe throughout. */
static void writeCrashRecord(int fd, int sig, siginfo_t* info)
{
	void* faultAddr = info ? info->si_addr : nullptr;

	asWrite(fd, "=== Scribus crash ===\nsignal:     ");
	asWriteSigned(fd, sig);
	asWrite(fd, "\nsi_code:    ");
	asWriteSigned(fd, info ? info->si_code : 0);
	asWrite(fd, "\nfault addr: 0x");
	asWriteUnsigned(fd, (unsigned long) faultAddr, 16);
	asWrite(fd, "\nunix time:  ");
	asWriteSigned(fd, (long) ::time(nullptr));
	asWrite(fd, "\npid:        ");
	asWriteSigned(fd, (long) ::getpid());
	asWrite(fd, "\n--- backtrace ---\n");
	// Demangling here would mean __cxa_demangle(), which allocates, so the
	// names are left mangled for c++filt to resolve after the fact.
	asWrite(fd, "(frames 0-1 are this handler and the signal trampoline;\n"
	            " demangle with: c++filt < this-file)\n");
#ifdef SCRIBUS_HAVE_EXECINFO
	// backtrace_symbols_fd() is the async-safe variant: unlike
	// backtrace_symbols() it does not allocate. The binary is linked with
	// CMAKE_ENABLE_EXPORTS, so these frames carry function names.
	int frames = backtrace(s_btBuf, (int) (sizeof(s_btBuf) / sizeof(s_btBuf[0])));
	backtrace_symbols_fd(s_btBuf, frames, fd);
#else
	asWrite(fd, "(no execinfo on this platform)\n");
#endif
	asWrite(fd, "=== end ===\n");
}

/*! \brief Tell the operator, without blocking.

fork() and execve() are both async-signal-safe. This replaces the modal
ScMessageBox the old handler raised from signal context, which ran a nested Qt
event loop and hung the process indefinitely when nobody was there to click it. */
static void asNotify()
{
	if (s_notifyPath[0] == '\0')
		return;
	pid_t pid = ::fork();
	if (pid != 0)
		return;						// parent, or fork failed: carry on regardless
	char* const argv[] = { s_notifyPath,
	                       (char*) "-u", (char*) "critical",
	                       (char*) "Scribus crashed", s_crashLogPath, nullptr };
	::execve(s_notifyPath, argv, environ);
	::_exit(127);					// exec failed; the parent must not notice
}

void initCrashHandler()
{
	// Leave fatal signals at SIG_DFL so the OS writes a core dump and
	// debuggers see the real fault, instead of the handler below.
	if (qEnvironmentVariableIsSet("SCRIBUS_NO_CRASH_HANDLER"))
		return;

	// Everything the handler needs is rendered now, while allocating is still
	// legal. The file name carries the session start time; the crash time
	// itself goes into the record. The crash-*.log name matches what
	// scribus-debug produces, so housekeeping.sh already keeps these out of
	// the 7-day sweep.
	const QString logDir = QDir::homePath() + "/scribus-crashlogs";
	QDir().mkpath(logDir);
	const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
	const QString logPath = QString("%1/crash-%2-%3.log").arg(logDir, stamp).arg((long) ::getpid());
	qstrncpy(s_crashLogPath, logPath.toUtf8().constData(), sizeof(s_crashLogPath));

	const QString notifySend = QStandardPaths::findExecutable("notify-send");
	if (!notifySend.isEmpty())
		qstrncpy(s_notifyPath, notifySend.toUtf8().constData(), sizeof(s_notifyPath));

#ifdef SCRIBUS_HAVE_EXECINFO
	// Prime glibc: the first backtrace() may dlopen and malloc, and that must
	// not be happening for the first time inside the handler.
	backtrace(s_btBuf, (int) (sizeof(s_btBuf) / sizeof(s_btBuf[0])));
#endif

	// An alternate stack is what lets a stack-overflow SIGSEGV still be caught:
	// without it the kernel cannot push a signal frame and kills us outright.
	const size_t altSize = (size_t) SIGSTKSZ * 4;
	s_altStack.ss_sp = ::malloc(altSize);
	if (s_altStack.ss_sp)
	{
		s_altStack.ss_size  = altSize;
		s_altStack.ss_flags = 0;
		::sigaltstack(&s_altStack, nullptr);
	}

	struct sigaction sa;
	::memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = defaultCrashHandler;
	// SA_SIGINFO   gives us si_code and the fault address.
	// SA_ONSTACK   lets the handler run after a stack overflow.
	// SA_RESETHAND means a fault *inside* the handler hits SIG_DFL and dumps a
	//              core at once, instead of going through the kernel's force path.
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;

	// SIGBUS was missing from the old list, so bus errors were never handled.
	const int fatalSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	sigset_t mask;
	sigemptyset(&mask);
	for (int sig : fatalSignals)
	{
		::sigaction(sig, &sa, nullptr);
		sigaddset(&mask, sig);
	}
	::sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

static void defaultCrashHandler(int sig, siginfo_t* info, void* /*context*/)
{
	static volatile sig_atomic_t handlerEntered = 0;
	static volatile sig_atomic_t saveAttempted  = 0;

	// Bound the whole handler. SIGALRM stays at SIG_DFL, so if anything below
	// hangs, the process dies rather than freezing the operator's desktop. The
	// old code armed its alarm *after* the blocking dialog, where it could
	// never fire in time to bound the hang it was meant to bound.
	::signal(SIGALRM, SIG_DFL);
	::alarm(30);

	if (handlerEntered)
	{
		// A second fatal signal while handling the first. The record is already
		// on disk, so stop here and let the kernel have the corpse.
		::raise(sig);
		::_exit(255);
	}
	handlerEntered = 1;
	emergencyActivated = true;

	// 1. Evidence first, before any call that can fault or block.
	asWrite(STDERR_FILENO, "Scribus crashed - signal ");
	asWriteSigned(STDERR_FILENO, sig);
	asWrite(STDERR_FILENO, "\nwriting ");
	asWrite(STDERR_FILENO, s_crashLogPath);
	asWrite(STDERR_FILENO, "\n");

	int fd = ::open(s_crashLogPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd >= 0)
	{
		writeCrashRecord(fd, sig, info);
		::fsync(fd);
		::close(fd);
	}

	// 2. Non-blocking notification, in place of the modal dialog.
	asNotify();

	// 3. The operator's unsaved page is worth more than a clean shutdown, but
	//    these are Qt calls in signal context: they can fault or hang. They run
	//    only after the record above is safely on disk, and only once.
	if (!saveAttempted)
	{
		saveAttempted = 1;
		ScImageCacheManager::instance().removeMasterLock();
		if (ScribusQApp::useGUI && ScCore)
		{
			ScribusMainWindow* mainWin = ScCore->primaryMainWindow();
			if (mainWin)
				mainWin->emergencySave();
		}
	}

	// 4. Re-raise with the default action so the kernel still writes a core and
	//    systemd-coredump captures it. SA_RESETHAND already restored SIG_DFL;
	//    the signal is blocked while the handler runs, so unblock it first.
	sigset_t self;
	sigemptyset(&self);
	sigaddset(&self, sig);
	::sigprocmask(SIG_UNBLOCK, &self, nullptr);
	::raise(sig);
	::_exit(255);					// only reached if raise() somehow returns
}
