/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scupdatekeystore.h"

#include <memory>

#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QTimer>

#ifdef HAVE_QTKEYCHAIN
#include "qt6keychain/keychain.h"
#endif
#ifdef HAVE_KEYRING_PROBE
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#endif

namespace {

const QString kService = QStringLiteral("org.scribus.updater");
const QString kKeyName = QStringLiteral("apiKey");
const QString kObfuscatedKey = QStringLiteral("apiKeyObfuscated");
const int kJobTimeoutMs = 4000;

// Not encryption: a fixed mask just keeps the key from sitting in the prefs
// file as readable text. Only reached when no OS keychain backend exists.
QByteArray xorObfuscate(const QByteArray& data)
{
	static const char mask[] = "Scr1bus-Upd8ter-Faircode-2026";
	const int maskLen = sizeof(mask) - 1;
	QByteArray out(data);
	for (int i = 0; i < out.size(); ++i)
		out[i] = out[i] ^ mask[i % maskLen];
	return out;
}

void storeObfuscated(const QString& apiKey)
{
	QSettings settings("Faircode", "ScribusUpdater");
	settings.setValue(kObfuscatedKey, xorObfuscate(apiKey.toUtf8()).toBase64());
}

QString loadObfuscated()
{
	QSettings settings("Faircode", "ScribusUpdater");
	QByteArray obfuscated = QByteArray::fromBase64(settings.value(kObfuscatedKey).toByteArray());
	if (obfuscated.isEmpty())
		return QString();
	return QString::fromUtf8(xorObfuscate(obfuscated));
}

void clearObfuscated()
{
	QSettings settings("Faircode", "ScribusUpdater");
	settings.remove(kObfuscatedKey);
}

// Generic fallback entries live in their own file, one group per service.
QString obfuscatedGroupKey(const QString& service, const QString& key)
{
	return service + QLatin1Char('/') + key;
}

#ifdef HAVE_KEYRING_PROBE
// One D-Bus call that never starts a service (no activation: a missing
// daemon stays missing) and waits at most until \a deadline, with the event
// loop running. False on error or timeout; \a timedOut tells which.
bool busCall(QDBusMessage msg, const QDeadlineTimer& deadline, QDBusMessage* reply, bool* timedOut)
{
	*timedOut = false;
	const int ms = int(qMax<qint64>(1, deadline.remainingTime()));
	msg.setAutoStartService(false);
	QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(msg, ms));
	QEventLoop loop;
	QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
	QTimer::singleShot(ms + 100, &loop, &QEventLoop::quit);
	if (!watcher.isFinished())
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	if (!watcher.isFinished())
	{
		*timedOut = true;
		return false;
	}
	*reply = watcher.reply();
	if (reply->type() == QDBusMessage::ErrorMessage)
	{
		*timedOut = reply->errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply");
		return false;
	}
	return !reply->arguments().isEmpty();
}

ScUpdateKeyStore::KeyringState probeKeyring(int timeoutMs)
{
	using KS = ScUpdateKeyStore::KeyringState;
	if (!QDBusConnection::sessionBus().isConnected())
		return KS::NoSessionBus;
	const QDeadlineTimer deadline(timeoutMs);
	QDBusMessage reply;
	bool timedOut = false;

	QDBusMessage owner = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
	                                                    QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
	owner << QStringLiteral("org.freedesktop.secrets");
	if (!busCall(owner, deadline, &reply, &timedOut))
		return timedOut ? KS::NoAnswer : KS::NoDaemon;
	if (!reply.arguments().constFirst().toBool())
		return KS::NoDaemon;

	// The collection Qt Keychain (libsecret) writes to is the "default" alias.
	QDBusMessage alias = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.secrets"), QStringLiteral("/org/freedesktop/secrets"),
	                                                    QStringLiteral("org.freedesktop.Secret.Service"), QStringLiteral("ReadAlias"));
	alias << QStringLiteral("default");
	if (!busCall(alias, deadline, &reply, &timedOut))
		return timedOut ? KS::NoAnswer : KS::NoDefaultCollection;
	const QString path = reply.arguments().constFirst().value<QDBusObjectPath>().path();
	if (path.isEmpty() || path == QLatin1String("/"))
		return KS::NoDefaultCollection;

	QDBusMessage locked = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.secrets"), path,
	                                                     QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
	locked << QStringLiteral("org.freedesktop.Secret.Collection") << QStringLiteral("Locked");
	if (!busCall(locked, deadline, &reply, &timedOut))
		return timedOut ? KS::NoAnswer : KS::NoDefaultCollection;
	return reply.arguments().constFirst().value<QDBusVariant>().variant().toBool() ? KS::Locked : KS::Ready;
}
#endif

#ifdef HAVE_QTKEYCHAIN
struct JobResult
{
	bool done { false };
	QKeychain::Error error { QKeychain::NoError };
	QString errorString;
	QString text;
};

// Runs a keychain job for at most \a timeoutMs; the result is copied when
// the job finishes. A job still running at the timeout is left to finish on
// its own and then deletes itself (its result is ignored).
JobResult runJob(QKeychain::Job* job, int timeoutMs)
{
	auto res = std::make_shared<JobResult>();
	QEventLoop loop;
	QObject::connect(job, &QKeychain::Job::finished, &loop, [&loop, res, job]() {
		res->done = true;
		res->error = job->error();
		res->errorString = job->errorString();
		if (auto* read = qobject_cast<QKeychain::ReadPasswordJob*>(job))
			res->text = read->textData();
		loop.quit();
	});
	QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
	job->setAutoDelete(false);
	job->start();
	if (!res->done)
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	if (res->done)
		job->deleteLater();
	else
	{
		job->setAutoDelete(true);
		res->errorString = QStringLiteral("keyring did not answer within %1 ms").arg(timeoutMs);
	}
	return *res;
}
#endif

} // namespace

ScUpdateKeyStore::KeyringState ScUpdateKeyStore::keyringState(int timeoutMs, int* elapsedMs)
{
	static bool known = false;
	static bool probing = false;
	static KeyringState state = KeyringState::NotBuilt;
	if (elapsedMs)
		*elapsedMs = 0;
	if (known)
		return state;
	// Asked again while the probe's event loop runs (a login during a slow
	// check): not ready for that caller, and no second probe.
	if (probing)
		return KeyringState::NoAnswer;
	probing = true;
	QElapsedTimer t;
	t.start();
#if !defined(HAVE_QTKEYCHAIN)
	Q_UNUSED(timeoutMs);
	state = KeyringState::NotBuilt;
#elif defined(HAVE_KEYRING_PROBE)
	// The bounded probe first: QKeychain::isAvailable() may itself ask D-Bus
	// (KWallet) without a timeout.
	state = probeKeyring(timeoutMs);
	if (state == KeyringState::Ready && !QKeychain::isAvailable())
		state = KeyringState::NoDaemon;
#else
	// Windows / macOS keychains never need a keyring to be created first.
	Q_UNUSED(timeoutMs);
	state = QKeychain::isAvailable() ? KeyringState::Ready : KeyringState::NoDaemon;
#endif
	known = true;
	probing = false;
	if (elapsedMs)
		*elapsedMs = int(t.elapsed());
	return state;
}

QString ScUpdateKeyStore::keyringStateText(KeyringState state)
{
	switch (state)
	{
		case KeyringState::Ready:               return QStringLiteral("ready (default keyring unlocked)");
		case KeyringState::NotBuilt:            return QStringLiteral("not built with Qt Keychain");
		case KeyringState::NoSessionBus:        return QStringLiteral("no D-Bus session bus");
		case KeyringState::NoDaemon:            return QStringLiteral("no keyring service running");
		case KeyringState::NoDefaultCollection: return QStringLiteral("no default (login) keyring");
		case KeyringState::Locked:              return QStringLiteral("default keyring is locked");
		case KeyringState::NoAnswer:            return QStringLiteral("keyring service did not answer");
	}
	return QString();
}

bool ScUpdateKeyStore::isSecureBackendAvailable()
{
	return keyringState() == KeyringState::Ready;
}

bool ScUpdateKeyStore::keyringStore(const QString& service, const QString& key, const QString& value, int timeoutMs, QString* error)
{
#ifdef HAVE_QTKEYCHAIN
	const KeyringState st = keyringState();
	if (st != KeyringState::Ready)
	{
		if (error)
			*error = keyringStateText(st);
		return false;
	}
	auto* job = new QKeychain::WritePasswordJob(service);
	job->setKey(key);
	job->setTextData(value);
	const JobResult r = runJob(job, timeoutMs);
	if (!r.done || r.error != QKeychain::NoError)
	{
		if (error)
			*error = r.errorString;
		return false;
	}
	return true;
#else
	Q_UNUSED(service); Q_UNUSED(key); Q_UNUSED(value); Q_UNUSED(timeoutMs);
	if (error)
		*error = keyringStateText(KeyringState::NotBuilt);
	return false;
#endif
}

bool ScUpdateKeyStore::keyringLoad(const QString& service, const QString& key, QString* value, int timeoutMs, QString* error)
{
	if (value)
		value->clear();
#ifdef HAVE_QTKEYCHAIN
	const KeyringState st = keyringState();
	if (st != KeyringState::Ready)
	{
		if (error)
			*error = keyringStateText(st);
		return false;
	}
	auto* job = new QKeychain::ReadPasswordJob(service);
	job->setKey(key);
	const JobResult r = runJob(job, timeoutMs);
	if (r.done && r.error == QKeychain::NoError)
	{
		if (value)
			*value = r.text;
		return true;
	}
	if (r.done && r.error == QKeychain::EntryNotFound)
		return true;
	if (error)
		*error = r.errorString;
	return false;
#else
	Q_UNUSED(service); Q_UNUSED(key); Q_UNUSED(timeoutMs);
	if (error)
		*error = keyringStateText(KeyringState::NotBuilt);
	return false;
#endif
}

bool ScUpdateKeyStore::keyringClear(const QString& service, const QString& key, int timeoutMs, QString* error)
{
#ifdef HAVE_QTKEYCHAIN
	const KeyringState st = keyringState();
	if (st != KeyringState::Ready)
	{
		if (error)
			*error = keyringStateText(st);
		return false;
	}
	auto* job = new QKeychain::DeletePasswordJob(service);
	job->setKey(key);
	const JobResult r = runJob(job, timeoutMs);
	if (!r.done || (r.error != QKeychain::NoError && r.error != QKeychain::EntryNotFound))
	{
		if (error)
			*error = r.errorString;
		return false;
	}
	return true;
#else
	Q_UNUSED(service); Q_UNUSED(key); Q_UNUSED(timeoutMs);
	if (error)
		*error = keyringStateText(KeyringState::NotBuilt);
	return false;
#endif
}

QString ScUpdateKeyStore::loadLegacySecret(const QString& service, const QString& key)
{
	QSettings settings("Faircode", "ScribusSecrets");
	QByteArray obfuscated = QByteArray::fromBase64(settings.value(obfuscatedGroupKey(service, key)).toByteArray());
	if (obfuscated.isEmpty())
		return QString();
	return QString::fromUtf8(xorObfuscate(obfuscated));
}

void ScUpdateKeyStore::clearLegacySecret(const QString& service, const QString& key)
{
	QSettings settings("Faircode", "ScribusSecrets");
	if (settings.contains(obfuscatedGroupKey(service, key)))
		settings.remove(obfuscatedGroupKey(service, key));
}

bool ScUpdateKeyStore::store(const QString& apiKey)
{
	if (keyringStore(kService, kKeyName, apiKey, kJobTimeoutMs))
	{
		// Successful keychain write supersedes any stale obfuscated copy.
		clearObfuscated();
		return true;
	}
	storeObfuscated(apiKey);
	return true;
}

QString ScUpdateKeyStore::load()
{
	if (keyringState() == KeyringState::Ready)
	{
		QString value;
		if (!keyringLoad(kService, kKeyName, &value, kJobTimeoutMs))
			return QString();
		if (!value.isEmpty())
			return value;
	}
	return loadObfuscated();
}

void ScUpdateKeyStore::clear()
{
	keyringClear(kService, kKeyName, kJobTimeoutMs);
	clearObfuscated();
}

bool ScUpdateKeyStore::storeSecret(const QString& service, const QString& key, const QString& value)
{
	if (keyringStore(service, key, value, kJobTimeoutMs))
	{
		clearLegacySecret(service, key);
		return true;
	}
	QSettings settings("Faircode", "ScribusSecrets");
	settings.setValue(obfuscatedGroupKey(service, key), xorObfuscate(value.toUtf8()).toBase64());
	return true;
}

QString ScUpdateKeyStore::loadSecret(const QString& service, const QString& key)
{
	if (keyringState() == KeyringState::Ready)
	{
		QString value;
		if (!keyringLoad(service, key, &value, kJobTimeoutMs))
			return QString();
		if (!value.isEmpty())
			return value;
	}
	return loadLegacySecret(service, key);
}

void ScUpdateKeyStore::clearSecret(const QString& service, const QString& key)
{
	keyringClear(service, key, kJobTimeoutMs);
	clearLegacySecret(service, key);
}
