/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCUPDATEKEYSTORE_H
#define SCUPDATEKEYSTORE_H

#include "scribusapi.h"

#include <QString>

/**
 * Storage for the updater's API key.
 *
 * Prefers the OS keychain (via Qt Keychain, when Scribus was built with
 * HAVE_QTKEYCHAIN and a secure backend is actually present at runtime).
 * Falls back to QSettings with a fixed-key XOR mask, which is obfuscation,
 * not encryption — it only keeps the key from sitting in the prefs file as
 * plain readable text. The key is never written in plaintext.
 */
namespace ScUpdateKeyStore
{
	SCRIBUS_API bool isSecureBackendAvailable();

	SCRIBUS_API bool store(const QString& apiKey);
	SCRIBUS_API QString load();
	SCRIBUS_API void clear();

	// Generic named secrets for other features (e.g. the News Browser's
	// access/refresh tokens). Same backend rules as above: OS keychain when
	// available, otherwise an obfuscated (not encrypted) prefs entry.
	// \a service groups the keys, e.g. "org.scribus.news".
	SCRIBUS_API bool storeSecret(const QString& service, const QString& key, const QString& value);
	SCRIBUS_API QString loadSecret(const QString& service, const QString& key);
	SCRIBUS_API void clearSecret(const QString& service, const QString& key);
}

#endif
