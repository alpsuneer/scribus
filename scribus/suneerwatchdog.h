/* suneer: freeze watchdog. A thread watches a heartbeat the main thread refreshes every 50 ms; when it
   stops for over a second the main thread's backtrace is captured (SIGUSR2) and the stall is written to
   ~/scribus-crashlogs/stalls.log with its length when it ends. SUNEER_NO_WATCHDOG=1 disables it. */
#ifndef SUNEERWATCHDOG_H
#define SUNEERWATCHDOG_H
#include <QObject>
namespace SuneerWatchdog
{
	void start(QObject* mainThreadOwner);   //!< from the main thread, once the event loop runs
	QString logPath();
}
#endif
