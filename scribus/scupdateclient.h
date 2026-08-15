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
 */
class SCRIBUS_API ScUpdateClient : public QObject
{
	Q_OBJECT

public:
	explicit ScUpdateClient(QObject* parent = nullptr);
	~ScUpdateClient() override;

	//! Compares "major.minor.patch[.build]" version strings; <0, 0, >0 like QString::compare().
	static int compareVersions(const QString& a, const QString& b);

	void checkForUpdate(const QString& baseUrl, const QString& apiKey);
	void downloadUpdate(const QUrl& url, const QString& apiKey, const QString& destinationPath);
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
	bool m_downloadCancelled {false};
};

#endif
