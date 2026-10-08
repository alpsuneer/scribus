/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCUPDATECLIENT_H
#define SCUPDATECLIENT_H

#include "scribusapi.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

//! Where the updater gets its server URL and API key from, in this order:
//! the user's own Update Settings (QSettings + key store), then
//! /etc/scribus/update.conf. The key always comes from the same place as the
//! URL, so a system-wide server never gets a user's key for another server.
struct SCRIBUS_API ScUpdateSettings
{
	QString url;
	QString apiKey;
	//! "settings", "conf" or "" (nothing configured)
	QString source;
	//! Set when a saved user URL was ignored this time (why, in plain words).
	QString ignoredUserUrl;
};

struct SCRIBUS_API ScUpdateInfo
{
	QString version;
	QString changelog;
	QUrl downloadUrl;
	//! Lower-case hex SHA-256 of the installer, covered by the manifest signature.
	QString sha256;
	//! The update server this manifest was fetched from; downloads must stay on it.
	QUrl serverUrl;
};

/**
 * Talks to a private, user-supplied update server. Never hardcodes a URL or
 * key: both are passed in per call by the caller (the Update Settings
 * dialog), sourced from user input or from ScUpdateKeyStore.
 *
 * checkForUpdate() does GET <baseUrl>/latest.json with the API key sent on
 * both the Authorization and X-API-Key headers, since either is a reasonable
 * server-side convention and sending both costs nothing.
 *
 * downloadUpdate() authenticates the same way to fetch the installer the
 * server pointed to.
 *
 * The installer is run as root (pkexec dpkg -i), so nothing is offered or
 * installed unless the manifest carries an Ed25519 signature made with the
 * release key whose public half is compiled in (SCRIBUS_UPDATE_PUBKEY), over
 * manifestSigningMessage(version, url, sha256). The download must be on the
 * same scheme/host/port as the server, and must hash to the signed sha256.
 * See tools/sign-update-manifest.sh for the server-side half.
 */
class SCRIBUS_API ScUpdateClient : public QObject
{
	Q_OBJECT

public:
	explicit ScUpdateClient(QObject* parent = nullptr);
	~ScUpdateClient() override;

	//! Compares "major.minor.patch[.build][-YYYYMMDD[-N]]" version strings; <0, 0, >0 like QString::compare().
	//! A missing part counts as 0, so "1.7.3" is older than "1.7.3-20260930-1".
	static int compareVersions(const QString& a, const QString& b);
	//! The version this build reports to the updater: the release version
	//! tools/release.sh compiled in (SCRIBUS_RELEASE_VERSION), or the plain
	//! upstream version for a developer build.
	static QString localVersion();

	//! The exact bytes the release key signs for one manifest.
	static QByteArray manifestSigningMessage(const QString& version, const QString& url, const QString& sha256);
	//! Ed25519 check of a base64 signature against a base64 raw 32-byte public key.
	static bool verifySignature(const QByteArray& message, const QString& signatureBase64, const QByteArray& publicKeyBase64, QString* error);
	//! True when both URLs share scheme, host and port.
	static bool sameOrigin(const QUrl& a, const QUrl& b);
	//! Lower-case hex SHA-256 of a file, or an empty string if it cannot be read.
	static QString fileSha256(const QString& path);
	//! The compiled-in release public key (base64), empty when none was configured.
	static QByteArray releasePublicKey();
	//! Path of the system-wide updater configuration file.
	static QString systemConfigPath();
	//! The effective server URL and key (see ScUpdateSettings). readKeyStore=false
	//! skips the OS key store (it can block); the conf file key is still read.
	static ScUpdateSettings effectiveSettings(bool readKeyStore = true);
	//! One http(s) URL for the update server, or empty with \a why set: not a
	//! URL, more than one word (a pasted command), no scheme, or a retired
	//! server. The returned string is normalised (trimmed, no trailing slash).
	static QString validateServerUrl(const QString& text, QString* why = nullptr);
	//! Forget the URL a user saved in Update Settings (back to update.conf).
	static void clearSavedServerUrl();


	//! An empty apiKey sends no authentication headers. timeoutMs bounds the
	//! whole request, connection included; a server that is off ends in
	//! networkError() within that time.
	void checkForUpdate(const QString& baseUrl, const QString& apiKey, int timeoutMs = 15000);
	//! The verified manifest of the last successful check, newer or not.
	const ScUpdateInfo& lastCheckedInfo() const { return m_lastInfo; }
	void downloadUpdate(const ScUpdateInfo& info, const QString& apiKey, const QString& destinationPath);
	void cancelDownload();

signals:
	void updateAvailable(const ScUpdateInfo& info);
	void upToDate();
	void authError();
	void notFoundError();
	void networkError(const QString& message);

	void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
	void downloadFinished(const QString& filePath);
	void downloadFailed(const QString& message);

private slots:
	void onCheckFinished();
	void onDownloadReadyRead();
	void onDownloadFinished();

private:
	QNetworkAccessManager* m_networkManager;
	QNetworkReply* m_checkReply {nullptr};
	QNetworkReply* m_downloadReply {nullptr};
	QFile* m_downloadFile {nullptr};
	QString m_downloadPath;
	QString m_expectedSha256;
	QUrl m_checkUrl;
	int m_checkTimeoutMs { 15000 };
	ScUpdateInfo m_lastInfo;
	bool m_downloadCancelled {false};
};

#endif
