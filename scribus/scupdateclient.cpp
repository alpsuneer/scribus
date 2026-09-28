/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scupdateclient.h"

#include "api/api_application.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <array>

#ifdef HAVE_UPDATE_SIGNING
#include <openssl/evp.h>
#endif

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

QByteArray ScUpdateClient::manifestSigningMessage(const QString& version, const QString& url, const QString& sha256)
{
	// A fixed line format rather than the JSON text itself: JSON has no
	// canonical byte form, so signing it would break on any reformatting.
	return QByteArrayLiteral("scribus-update-v1\n")
		+ version.toUtf8() + '\n'
		+ url.toUtf8() + '\n'
		+ sha256.toUtf8() + '\n';
}

bool ScUpdateClient::verifySignature(const QByteArray& message, const QString& signatureBase64, const QByteArray& publicKeyBase64, QString* error)
{
	auto fail = [error](const QString& why) {
		if (error)
			*error = why;
		return false;
	};
#ifdef HAVE_UPDATE_SIGNING
	const QByteArray key = QByteArray::fromBase64(publicKeyBase64, QByteArray::AbortOnBase64DecodingErrors);
	if (key.size() != 32)
		return fail(tr("This build has no valid update signing key, so updates cannot be verified."));
	const QByteArray sig = QByteArray::fromBase64(signatureBase64.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	if (sig.size() != 64)
		return fail(tr("The update manifest has no valid signature."));

	EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
		reinterpret_cast<const unsigned char*>(key.constData()), key.size());
	if (!pkey)
		return fail(tr("The update signing key could not be loaded."));
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	bool ok = ctx
		&& EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1
		&& EVP_DigestVerify(ctx,
			reinterpret_cast<const unsigned char*>(sig.constData()), sig.size(),
			reinterpret_cast<const unsigned char*>(message.constData()), message.size()) == 1;
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	if (!ok)
		return fail(tr("The update manifest signature does not match. The update was refused."));
	return true;
#else
	Q_UNUSED(message);
	Q_UNUSED(signatureBase64);
	Q_UNUSED(publicKeyBase64);
	return fail(tr("This build of Scribus was made without OpenSSL, so updates cannot be verified."));
#endif
}

bool ScUpdateClient::sameOrigin(const QUrl& a, const QUrl& b)
{
	if (!a.isValid() || !b.isValid())
		return false;
	const QString scheme = a.scheme().toLower();
	if (scheme != b.scheme().toLower())
		return false;
	if (a.host().compare(b.host(), Qt::CaseInsensitive) != 0)
		return false;
	const int defaultPort = (scheme == QLatin1String("https")) ? 443 : 80;
	return a.port(defaultPort) == b.port(defaultPort);
}

QString ScUpdateClient::fileSha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return QString();
	return QString::fromLatin1(hash.result().toHex());
}

QByteArray ScUpdateClient::releasePublicKey()
{
#ifdef SCRIBUS_UPDATE_PUBKEY
	return QByteArrayLiteral(SCRIBUS_UPDATE_PUBKEY);
#else
	return QByteArray();
#endif
}

void ScUpdateClient::checkForUpdate(const QString& baseUrl, const QString& apiKey)
{
	QString trimmedUrl = baseUrl;
	while (trimmedUrl.endsWith('/'))
		trimmedUrl.chop(1);

	m_checkUrl = QUrl(trimmedUrl + QStringLiteral("/latest.json"));
	QNetworkRequest request(m_checkUrl);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
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
	info.sha256 = obj.value(QStringLiteral("sha256")).toString().toLower();
	info.serverUrl = m_checkUrl;

	// Refuse before offering anything: the installer runs as root.
	static const QRegularExpression hexRe(QStringLiteral("^[0-9a-f]{64}$"));
	if (!sameOrigin(info.downloadUrl, m_checkUrl))
	{
		emit networkError(tr("The update server pointed to a download on a different server (%1). The update was refused.")
			.arg(info.downloadUrl.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery)));
		return;
	}
	if (!hexRe.match(info.sha256).hasMatch())
	{
		emit networkError(tr("The update manifest has no valid sha256. The update was refused."));
		return;
	}
	QString verifyError;
	if (!verifySignature(manifestSigningMessage(remoteVersion, urlStr, info.sha256),
			obj.value(QStringLiteral("signature")).toString(), releasePublicKey(), &verifyError))
	{
		emit networkError(verifyError);
		return;
	}

	const QString localVersion = ScribusAPI::getVersion();
	if (compareVersions(remoteVersion, localVersion) > 0)
		emit updateAvailable(info);
	else
		emit upToDate();
}

void ScUpdateClient::downloadUpdate(const ScUpdateInfo& info, const QString& apiKey, const QString& destinationPath)
{
	cancelDownload();

	// The API key only ever goes to the server the manifest came from.
	if (!sameOrigin(info.downloadUrl, info.serverUrl) || info.sha256.isEmpty())
	{
		emit downloadFailed(tr("The update download is not on the update server. The update was refused."));
		return;
	}
	m_expectedSha256 = info.sha256;
	const QUrl url = info.downloadUrl;

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
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
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

	const QString actual = fileSha256(m_downloadPath);
	if (actual.isEmpty() || actual != m_expectedSha256)
	{
		QFile::remove(m_downloadPath);
		emit downloadFailed(tr("The downloaded update does not match its signed checksum and was deleted."));
		return;
	}
	emit downloadFinished(m_downloadPath);
}

void ScUpdateClient::cancelDownload()
{
	if (!m_downloadReply)
		return;
	m_downloadCancelled = true;
	m_downloadReply->abort();
}
