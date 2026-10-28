#include "suneerwatchdog.h"
#include <QAtomicInteger>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>

namespace
{
std::atomic<qint64> s_heartbeat { 0 };
pthread_t s_mainThread;
void* s_frames[64];
std::atomic<int> s_frameCount { 0 };
std::atomic<bool> s_captured { false };

void captureHandler(int)
{
	// runs on the main thread, wherever it is stuck: just record the stack
	s_frameCount = backtrace(s_frames, 64);
	s_captured = true;
}

class WatchThread : public QThread
{
public:
	void run() override
	{
		const qint64 limitMs = 1000;
		bool inStall = false;
		qint64 stallStart = 0;
		while (!isInterruptionRequested())
		{
			msleep(200);
			const qint64 now = QDateTime::currentMSecsSinceEpoch();
			const qint64 hb = s_heartbeat.load();
			if (hb == 0)
				continue;
			if (!inStall && now - hb > limitMs)
			{
				inStall = true;
				stallStart = hb;
				s_captured = false;
				pthread_kill(s_mainThread, SIGUSR2);
				for (int i = 0; i < 20 && !s_captured; ++i)
					msleep(10);
				QFile f(SuneerWatchdog::logPath());
				if (f.open(QIODevice::Append | QIODevice::Text))
				{
					const QString head = QStringLiteral("%1 STALL: main thread silent for %2 ms so far (pid %3)\n")
					    .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(now - hb).arg(getpid());
					f.write(head.toUtf8());
					if (s_captured)
					{
						f.write("  main thread backtrace (c++filt to demangle):\n");
						f.flush();
						backtrace_symbols_fd(s_frames, s_frameCount.load(), f.handle());
					}
					else
						f.write("  (backtrace not captured)\n");
					f.close();
				}
			}
			else if (inStall && now - hb <= limitMs)
			{
				inStall = false;
				QFile f(SuneerWatchdog::logPath());
				if (f.open(QIODevice::Append | QIODevice::Text))
					f.write(QStringLiteral("%1 STALL ended: %2 ms\n\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(hb - stallStart).toUtf8());
			}
		}
	}
};
WatchThread* s_thread = nullptr;
}

namespace SuneerWatchdog
{
QString logPath()
{
	const QString dir = QDir::homePath() + QStringLiteral("/scribus-crashlogs");
	QDir().mkpath(dir);
	return dir + QStringLiteral("/stalls.log");
}

void start(QObject* owner)
{
	if (s_thread || qEnvironmentVariableIsSet("SUNEER_NO_WATCHDOG"))
		return;
	s_mainThread = pthread_self();
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = captureHandler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR2, &sa, nullptr);
	s_heartbeat = QDateTime::currentMSecsSinceEpoch();
	QTimer* beat = new QTimer(owner);
	beat->setInterval(50);
	QObject::connect(beat, &QTimer::timeout, owner, []() { s_heartbeat = QDateTime::currentMSecsSinceEpoch(); });
	beat->start();
	s_thread = new WatchThread();
	s_thread->setObjectName(QStringLiteral("suneer-watchdog"));
	s_thread->start(QThread::LowPriority);
	// SUNEER_STALL_TEST=<ms>: block the main thread that long, 5 s from now, to prove the log works
	const int testMs = qEnvironmentVariableIntValue("SUNEER_STALL_TEST");
	if (testMs > 0)
		QTimer::singleShot(5000, owner, [testMs]() { QThread::msleep(testMs); });
}
}
