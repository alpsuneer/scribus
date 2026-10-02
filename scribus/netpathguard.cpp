/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "netpathguard.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QtDebug>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <climits>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	using Clock = std::chrono::steady_clock;

	// One helper thread's view of one path. Shared between that thread and the
	// callers waiting on it; the thread may outlive every caller (it is stuck in
	// the kernel for as long as the server stays away), hence shared_ptr.
	struct Probe
	{
		std::mutex              mutex;
		std::condition_variable cv;
		std::vector<QByteArray> prefixes;      // "/home", "/home/s1", ... native encoding
		QStringList             prefixNames;   // same, for messages and matching
		size_t                  at { 0 };      // prefix being stat()ed right now
		bool                    done { false };
		bool                    dead { false };   // leads into a folder already skipped
		bool                    gaveUp { false }; // a caller timed out on this probe
		Clock::time_point       started;
	};

	struct State
	{
		std::mutex                               mutex;
		QHash<QString, std::shared_ptr<Probe> >  running;   // by clean absolute path
		QStringList                              skipped;   // unreachable folders
		QSet<QString>                            reported;  // already handed to the status bar
		QStringList                              notes;     // skipped folders not yet shown
		QHash<QString, QString>                  linkTarget; // skipped symlink -> where it points
	};

	State& state()
	{
		// Leaked on purpose: a helper thread stuck on a dead mount can still be
		// alive during static destruction.
		static State* s = new State;
		return *s;
	}

	QString cleanAbsolute(const QString& path)
	{
		if (path.isEmpty())
			return QString();
		QString p = path;
		if (p.startsWith(QLatin1String("file://")))
			p = p.mid(7);
		if (!QDir::isAbsolutePath(p))
			p = QDir::currentPath() + QLatin1Char('/') + p;
		p = QDir::cleanPath(p);
		while (p.length() > 1 && p.endsWith(QLatin1Char('/')))
			p.chop(1);
		return p;
	}

	bool isUnder(const QString& path, const QString& folder)
	{
		if (path == folder)
			return true;
		return path.startsWith(folder) && path.length() > folder.length() && path.at(folder.length()) == QLatin1Char('/');
	}

	// state().mutex must be held.
	bool skippedLocked(const QString& abs)
	{
		for (const QString& folder : std::as_const(state().skipped))
		{
			if (isUnder(abs, folder))
				return true;
		}
		return false;
	}

	void probeThread(std::shared_ptr<Probe> probe, QString abs)
	{
		bool deadLink = false;
		for (size_t i = 0; i < probe->prefixes.size(); ++i)
		{
			{
				std::lock_guard<std::mutex> lock(probe->mutex);
				probe->at = i;
			}
			struct stat st;
			// On a dead hard mount this (or the stat below) is where the thread stays.
			if (::lstat(probe->prefixes[i].constData(), &st) != 0)
				break;   // an answer, even "no such file", is an answer
			if (S_ISLNK(st.st_mode))
			{
				// ~/Desktop/F -> /mnt/F: a link into a folder that is already
				// being skipped needs no second wait.
				char buf[PATH_MAX];
				const ssize_t n = ::readlink(probe->prefixes[i].constData(), buf, sizeof(buf) - 1);
				if (n > 0)
				{
					QString target = QFile::decodeName(QByteArray(buf, int(n)));
					const QString link = probe->prefixNames.at(int(i));
					if (!QDir::isAbsolutePath(target))
						target = link.left(link.lastIndexOf(QLatin1Char('/')) + 1) + target;
					target = QDir::cleanPath(target);
					State& s = state();
					std::lock_guard<std::mutex> lock(s.mutex);
					if (skippedLocked(target))
					{
						if (!s.skipped.contains(link))
							s.skipped << link;
						s.linkTarget.insert(link, target);
						deadLink = true;
					}
				}
				if (deadLink)
					break;
			}
			if (::stat(probe->prefixes[i].constData(), &st) != 0)
				break;
		}

		QString recovered;
		{
			std::lock_guard<std::mutex> lock(probe->mutex);
			probe->done = true;
			probe->dead = deadLink;
			if (probe->gaveUp)
				recovered = probe->prefixNames.value(int(probe->at));
		}
		probe->cv.notify_all();

		State& s = state();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.running.value(abs) == probe)
			s.running.remove(abs);
		// It answered after all (server back, or a soft mount finally gave an
		// error): stop skipping, the next use decides afresh.
		if (!recovered.isEmpty())
		{
			s.skipped.removeAll(recovered);
			s.reported.remove(recovered);
			const QStringList links = s.linkTarget.keys();
			for (const QString& link : links)
			{
				if (isUnder(s.linkTarget.value(link), recovered))
				{
					s.skipped.removeAll(link);
					s.linkTarget.remove(link);
				}
			}
		}
	}

	// state().mutex must be held. Returns the probe for abs, starting one if needed.
	std::shared_ptr<Probe> probeFor(const QString& abs)
	{
		State& s = state();
		auto it = s.running.constFind(abs);
		if (it != s.running.constEnd())
			return it.value();

		auto probe = std::make_shared<Probe>();
		const QStringList parts = abs.split(QLatin1Char('/'), Qt::SkipEmptyParts);
		QString prefix;
		for (const QString& part : parts)
		{
			prefix += QLatin1Char('/') + part;
			probe->prefixNames << prefix;
			probe->prefixes.push_back(QFile::encodeName(prefix));
		}
		if (probe->prefixes.empty())
		{
			probe->prefixNames << QStringLiteral("/");
			probe->prefixes.push_back(QByteArray("/"));
		}
		probe->started = Clock::now();
		s.running.insert(abs, probe);
		std::thread(probeThread, probe, abs).detach();
		return probe;
	}

	// Wait for a probe; on timeout record the folder it is stuck on.
	bool waitFor(const std::shared_ptr<Probe>& probe, int timeoutMs)
	{
		QString stuckOn;
		{
			std::unique_lock<std::mutex> lock(probe->mutex);
			const auto deadline = probe->started + std::chrono::milliseconds(timeoutMs);
			if (probe->cv.wait_until(lock, deadline, [&probe] { return probe->done; }))
				return !probe->dead;
			probe->gaveUp = true;
			stuckOn = probe->prefixNames.value(int(probe->at));
		}

		State& s = state();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (!stuckOn.isEmpty() && !s.skipped.contains(stuckOn))
		{
			s.skipped << stuckOn;
			if (!s.reported.contains(stuckOn))
			{
				s.reported.insert(stuckOn);
				s.notes << stuckOn;
				qWarning().noquote() << "[NetPathGuard] no answer within" << timeoutMs << "ms, skipping" << stuckOn;
			}
		}
		return false;
	}
}

void NetPathGuard::precheck(const QStringList& paths)
{
	State& s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	for (const QString& path : paths)
	{
		const QString abs = cleanAbsolute(path);
		if (abs.isEmpty() || skippedLocked(abs))
			continue;
		probeFor(abs);
	}
}

bool NetPathGuard::reachable(const QString& path, int timeoutMs)
{
	const QString abs = cleanAbsolute(path);
	if (abs.isEmpty())
		return true;

	std::shared_ptr<Probe> probe;
	{
		State& s = state();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (skippedLocked(abs))
			return false;
		probe = probeFor(abs);
	}
	return waitFor(probe, timeoutMs);
}

bool NetPathGuard::isSkipped(const QString& path)
{
	const QString abs = cleanAbsolute(path);
	if (abs.isEmpty())
		return false;
	State& s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return skippedLocked(abs);
}

QStringList NetPathGuard::reachableOnly(const QStringList& paths, int timeoutMs)
{
	// Start them all first: the waits then overlap, so ten dead paths cost one
	// timeout, not ten.
	precheck(paths);
	QStringList out;
	for (const QString& path : paths)
	{
		if (reachable(path, timeoutMs))
			out << path;
	}
	return out;
}

QStringList NetPathGuard::skippedFolders()
{
	State& s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.skipped;
}

QStringList NetPathGuard::takeNewlySkipped()
{
	State& s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	const QStringList notes = s.notes;
	s.notes.clear();
	return notes;
}
