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
 * HAVE_QTKEYCHAIN and the keyring is ready, see keyringState()).
 * Falls back to QSettings with a fixed-key XOR mask, which is obfuscation,
 * not encryption — it only keeps the key from sitting in the prefs file as
 * plain readable text. The key is never written in plaintext.
 *
 * Scribus never makes the keyring ask anything: the keychain is used only
 * when the Secret Service already runs and its default collection exists
 * and is unlocked. Otherwise a write would pop a hidden "create new keyring"
 * or "unlock" prompt and wait for it (chn-dtp26: the News Browser login hung).
 * Every keychain call is bounded by a timeout.
 */
namespace ScUpdateKeyStore
{
	enum class KeyringState
	{
		Ready,                //!< default collection exists and is unlocked
		NotBuilt,             //!< built without Qt Keychain
		NoSessionBus,         //!< no D-Bus session bus
		NoDaemon,             //!< nothing owns org.freedesktop.secrets (not started)
		NoDefaultCollection,  //!< no "login" / default keyring: a write would prompt to create one
		Locked,               //!< the default keyring is locked: a read or write would prompt to unlock
		NoAnswer              //!< the Secret Service did not answer in time
	};

	/*! Probes the Secret Service over D-Bus without starting, creating or
	    unlocking anything; at most \a timeoutMs. The result is kept for the
	    rest of the session (a keyring set up later needs a Scribus restart).
	    \a elapsedMs: how long this call took (0 when cached). */
	SCRIBUS_API KeyringState keyringState(int timeoutMs = 3000, int* elapsedMs = nullptr);
	SCRIBUS_API QString keyringStateText(KeyringState state);
	SCRIBUS_API bool isSecureBackendAvailable();   //!< keyringState() == Ready

	SCRIBUS_API bool store(const QString& apiKey);
	SCRIBUS_API QString load();
	SCRIBUS_API void clear();

	// Generic named secrets for other features. Same backend rules as above:
	// OS keychain when ready, otherwise an obfuscated (not encrypted) prefs
	// entry. \a service groups the keys, e.g. "org.scribus.news".
	SCRIBUS_API bool storeSecret(const QString& service, const QString& key, const QString& value);
	SCRIBUS_API QString loadSecret(const QString& service, const QString& key);
	SCRIBUS_API void clearSecret(const QString& service, const QString& key);

	// Keyring only, never the prefs file, at most \a timeoutMs each; false
	// when the keyring is not ready, failed or did not answer (\a error says
	// why). keyringLoad() gives true and an empty value for a missing entry.
	SCRIBUS_API bool keyringStore(const QString& service, const QString& key, const QString& value, int timeoutMs = 4000, QString* error = nullptr);
	SCRIBUS_API bool keyringLoad(const QString& service, const QString& key, QString* value, int timeoutMs = 4000, QString* error = nullptr);
	SCRIBUS_API bool keyringClear(const QString& service, const QString& key, int timeoutMs = 4000, QString* error = nullptr);

	// The obfuscated prefs copy of a named secret (written by older builds
	// when no keyring existed): read and remove only.
	SCRIBUS_API QString loadLegacySecret(const QString& service, const QString& key);
	SCRIBUS_API void clearLegacySecret(const QString& service, const QString& key);
}

#endif
