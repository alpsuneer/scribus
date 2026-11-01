/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERRESTOREPOINT_H
#define SUNEERRESTOREPOINT_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include "scribusapi.h"

class ScUpdateClient;

/*! suneer 2026-11-01: "Go back to the previous Scribus version" (program + settings).
    Before the updater installs a new package it saves a restore point:
      - the package files stay in /var/cache/scribus-updates/ (root-owned; the privileged install
        step copies every .deb it installs there and keeps the last 3);
      - the user's settings go to ~/.config/Scribus/backup/<date>_<version>/ (the Scribus/*.conf
        files, the prefs folder's prefs*.xml / scribus*.rc / shortcut-sets/ / text gradient presets /
        office-defaults receipt; last 3 kept).
    Help > Check for Updates... > "Go back to previous version..." lists the versions that have a
    cached package and/or a settings backup. Going back installs that .deb through the same
    pkexec step, copies the settings back once this Scribus has exited (a small shell waiter, so
    the exiting program cannot overwrite them again) and starts Scribus again. A package that is
    not cached is downloaded from the update server (scribus_<version>_amd64.deb + .sig, Ed25519
    over the file bytes, checked against the compiled-in release key).
    <prefs>/update-rollback.json remembers the version that was left; Check for Updates shows that
    note and installs it again only when Update is pressed (there is no automatic check anyway). */
namespace SuneerRestorePoint
{
	struct Point
	{
		QString version;
		QDateTime when;          //!< when the package was cached / the settings were saved
		QString debPath;         //!< cached .deb, empty when it must come from the server
		QString settingsDir;     //!< settings backup folder, empty when none
	};

	SCRIBUS_API QString cacheDir();
	SCRIBUS_API QString backupRoot();
	SCRIBUS_API QString debFileName(const QString& version);
	//! True for <upstream>-YYYYMMDD-N[.localM]: the only version strings a package is named after.
	SCRIBUS_API bool isPackageVersion(const QString& version);
	//! The cached package of a version, or empty.
	SCRIBUS_API QString cachedDeb(const QString& version);
	//! Copies the user's settings to backupRoot()/<date>_<version>/; returns that folder (empty + error on failure).
	SCRIBUS_API QString saveSettings(const QString& version, QString* error = nullptr);
	//! Every version with a cached package or a settings backup, newest first.
	SCRIBUS_API QList<Point> points();

	//! The privileged step: `pkexec /bin/sh -c <script> sh <deb> <version> <cache> [<more .deb to cache>...]`.
	//! Checks the .deb is package "scribus" in exactly <version>, copies it (and the extra ones) into the
	//! cache, runs dpkg -i, prunes the cache to the last 3. Exit 3 = not the expected package.
	SCRIBUS_API QString program();
	SCRIBUS_API QStringList installArguments(const QString& debPath, const QString& version, const QStringList& alsoCache = QStringList());

	//! Starts a detached waiter that copies <settingsDir> back once process <pid> has exited, then
	//! (restart) starts Scribus again. Returns false with error when the list could not be written.
	SCRIBUS_API bool restoreAfterExit(const QString& settingsDir, qint64 pid, bool restart, QString* error = nullptr);

	//! <prefs>/update-rollback.json: the version the user left and the one they went back to.
	SCRIBUS_API void writeRollbackMarker(const QString& fromVersion, const QString& toVersion);
	//! The version left by the last "Go back" while this is still the version gone back to; else empty.
	SCRIBUS_API QString rollbackLeftVersion(QDateTime* when = nullptr);
	SCRIBUS_API void clearRollbackMarker();

	//! Checks a base64 Ed25519 signature over the bytes of a file against the compiled-in release key.
	SCRIBUS_API bool verifyFileSignature(const QString& filePath, const QString& signatureBase64, QString* error);
}

/*! Downloads scribus_<version>_amd64.deb and its .sig from the update server into the user's
    download folder and verifies the signature. One shot: done(path) or failed(message). */
class SCRIBUS_API SuneerDebFetcher : public QObject
{
	Q_OBJECT
public:
	explicit SuneerDebFetcher(QObject* parent = nullptr);
	//! serverUrl = the update server base (ScUpdateClient::effectiveSettings().url).
	void fetch(const QString& version, const QString& serverUrl, const QString& apiKey);
	void cancel();
signals:
	void progress(qint64 received, qint64 total);
	void done(const QString& debPath);
	void failed(const QString& message);
private:
	ScUpdateClient* m_client;
	QString m_version, m_debPath, m_sigPath;
	QUrl m_server;
	QString m_apiKey;
	bool m_sigStage { false };
};

#endif
