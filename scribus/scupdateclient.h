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

	//! Compares "major.minor.patch[.build]" version strings; <0, 0, >0 like QString::compare().
	static int compareVersions(const QString& a, const QString& b);

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


	void checkForUpdate(const QString& baseUrl, const QString& apiKey);
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
	bool m_downloadCancelled {false};
};

#endif
