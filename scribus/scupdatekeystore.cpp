/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scupdatekeystore.h"

#include <QByteArray>
#include <QEventLoop>
#include <QSettings>

#ifdef HAVE_QTKEYCHAIN
#include "qt6keychain/keychain.h"
#endif

namespace {

const QString kService = QStringLiteral("org.scribus.updater");
const QString kKeyName = QStringLiteral("apiKey");
const QString kObfuscatedKey = QStringLiteral("apiKeyObfuscated");

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

} // namespace

bool ScUpdateKeyStore::isSecureBackendAvailable()
{
#ifdef HAVE_QTKEYCHAIN
	return QKeychain::isAvailable();
#else
	return false;
#endif
}

bool ScUpdateKeyStore::store(const QString& apiKey)
{
#ifdef HAVE_QTKEYCHAIN
	if (QKeychain::isAvailable())
	{
		QKeychain::WritePasswordJob job(kService);
		job.setAutoDelete(false);
		job.setKey(kKeyName);
		job.setTextData(apiKey);
		QEventLoop loop;
		QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
		job.start();
		loop.exec();
		if (job.error() == QKeychain::NoError)
		{
			// Successful keychain write supersedes any stale obfuscated copy.
			clearObfuscated();
			return true;
		}
	}
#endif
	storeObfuscated(apiKey);
	return true;
}

QString ScUpdateKeyStore::load()
{
#ifdef HAVE_QTKEYCHAIN
	if (QKeychain::isAvailable())
	{
		QKeychain::ReadPasswordJob job(kService);
		job.setAutoDelete(false);
		job.setKey(kKeyName);
		QEventLoop loop;
		QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
		job.start();
		loop.exec();
		if (job.error() == QKeychain::NoError)
			return job.textData();
		if (job.error() != QKeychain::EntryNotFound)
			return QString();
	}
#endif
	return loadObfuscated();
}

void ScUpdateKeyStore::clear()
{
#ifdef HAVE_QTKEYCHAIN
	if (QKeychain::isAvailable())
	{
		QKeychain::DeletePasswordJob job(kService);
		job.setAutoDelete(false);
		job.setKey(kKeyName);
		QEventLoop loop;
		QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
		job.start();
		loop.exec();
	}
#endif
	clearObfuscated();
}
