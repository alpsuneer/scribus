/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scupdateclient.h"

#include "api/api_application.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <array>

ScUpdateClient::ScUpdateClient(QObject* parent)
	: QObject(parent)
	, m_networkManager(new QNetworkAccessManager(this))
{
}

ScUpdateClient::~ScUpdateClient()
{
	if (m_checkReply)
	{
		m_checkReply->disconnect(this);
		m_checkReply->abort();
		m_checkReply->deleteLater();
	}
	if (m_downloadReply)
	{
		m_downloadReply->disconnect(this);
		m_downloadReply->abort();
		m_downloadReply->deleteLater();
	}
	if (m_downloadFile)
	{
		m_downloadFile->close();
		delete m_downloadFile;
	}
}

int ScUpdateClient::compareVersions(const QString& a, const QString& b)
{
	static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));

	auto parse = [](const QString& v) {
		std::array<int, 4> parts{ 0, 0, 0, 0 };
		QRegularExpressionMatch m = re.match(v);
		if (!m.hasMatch())
			return parts;
		for (int i = 0; i < 4; ++i)
		{
			const QString cap = m.captured(i + 1);
			parts[i] = cap.isEmpty() ? 0 : cap.toInt();
		}
		return parts;
	};

	const std::array<int, 4> pa = parse(a);
	const std::array<int, 4> pb = parse(b);
	for (int i = 0; i < 4; ++i)
	{
		if (pa[i] != pb[i])
			return pa[i] < pb[i] ? -1 : 1;
	}
	return 0;
}

void ScUpdateClient::checkForUpdate(const QString& baseUrl, const QString& apiKey)
{
	QString trimmedUrl = baseUrl;
	while (trimmedUrl.endsWith('/'))
		trimmedUrl.chop(1);

	QNetworkRequest request(QUrl(trimmedUrl + QStringLiteral("/latest.json")));
	request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
	request.setRawHeader("X-API-Key", apiKey.toUtf8());

	if (m_checkReply)
	{
		m_checkReply->disconnect(this);
		m_checkReply->abort();
		m_checkReply->deleteLater();
	}
	m_checkReply = m_networkManager->get(request);
	connect(m_checkReply, &QNetworkReply::finished, this, &ScUpdateClient::onCheckFinished);
}

void ScUpdateClient::onCheckFinished()
{
	QNetworkReply* reply = m_checkReply;
	m_checkReply = nullptr;
	reply->deleteLater();

	const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if (httpStatus == 401 || httpStatus == 403)
	{
		emit authError();
		return;
	}
	if (httpStatus == 404)
	{
		emit notFoundError();
		return;
	}
	if (reply->error() != QNetworkReply::NoError)
	{
		emit networkError(reply->errorString());
		return;
	}

	const QByteArray body = reply->readAll();
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
	{
		emit networkError(tr("Update server returned an unreadable response."));
		return;
	}

	const QJsonObject obj = doc.object();
	const QString remoteVersion = obj.value(QStringLiteral("version")).toString();
	if (remoteVersion.isEmpty())
	{
		emit networkError(tr("Update server response did not include a version."));
		return;
	}

	ScUpdateInfo info;
	info.version = remoteVersion;
	info.changelog = obj.value(QStringLiteral("changelog")).toString(obj.value(QStringLiteral("notes")).toString());

	QString urlStr = obj.value(QStringLiteral("url")).toString();
	if (urlStr.isEmpty())
		urlStr = obj.value(QStringLiteral("download_url")).toString();
	if (urlStr.isEmpty())
		urlStr = obj.value(QStringLiteral("deb_url")).toString();
	info.downloadUrl = QUrl(urlStr);

	const QString localVersion = ScribusAPI::getVersion();
	if (compareVersions(remoteVersion, localVersion) > 0)
		emit updateAvailable(info);
	else
		emit upToDate();
}

void ScUpdateClient::downloadUpdate(const QUrl& url, const QString& apiKey, const QString& destinationPath)
{
	cancelDownload();

	m_downloadFile = new QFile(destinationPath, this);
	if (!m_downloadFile->open(QIODevice::WriteOnly))
	{
		emit downloadFailed(tr("Could not open %1 for writing.").arg(destinationPath));
		delete m_downloadFile;
		m_downloadFile = nullptr;
		return;
	}
	m_downloadPath = destinationPath;

	QNetworkRequest request(url);
	request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
	request.setRawHeader("X-API-Key", apiKey.toUtf8());

	m_downloadReply = m_networkManager->get(request);
	connect(m_downloadReply, &QNetworkReply::readyRead, this, &ScUpdateClient::onDownloadReadyRead);
	connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &ScUpdateClient::downloadProgress);
	connect(m_downloadReply, &QNetworkReply::finished, this, &ScUpdateClient::onDownloadFinished);
}

void ScUpdateClient::onDownloadReadyRead()
{
	if (m_downloadFile && m_downloadReply)
		m_downloadFile->write(m_downloadReply->readAll());
}

void ScUpdateClient::onDownloadFinished()
{
	QNetworkReply* reply = m_downloadReply;
	m_downloadReply = nullptr;
	reply->deleteLater();

	if (m_downloadFile)
		m_downloadFile->close();

	const bool cancelled = m_downloadCancelled;
	m_downloadCancelled = false;

	const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const bool failed = !cancelled && ((reply->error() != QNetworkReply::NoError) || (httpStatus != 0 && httpStatus >= 400));

	if (cancelled || failed)
	{
		if (m_downloadFile)
		{
			m_downloadFile->remove();
			delete m_downloadFile;
			m_downloadFile = nullptr;
		}
		if (failed)
		{
			QString message = reply->errorString();
			if (httpStatus == 401 || httpStatus == 403)
				message = tr("Invalid API key while downloading the update.");
			emit downloadFailed(message);
		}
		return;
	}

	delete m_downloadFile;
	m_downloadFile = nullptr;
	emit downloadFinished(m_downloadPath);
}

void ScUpdateClient::cancelDownload()
{
	if (!m_downloadReply)
		return;
	m_downloadCancelled = true;
	m_downloadReply->abort();
}
