/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scupdateclient.h"

#include "api/api_application.h"
#include "scupdatekeystore.h"
#include "scupdate_release_version.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

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
	// major.minor.patch[.build][-date[-serial]]. Releases are
	// "1.7.3-YYYYMMDD-N" (tools/release.sh); the date and serial decide
	// between two builds of the same upstream version. Older packages used
	// "-DDMMYY-N", which as a number is always below any YYYYMMDD.
	static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)(?:\\.(\\d+))?(?:-(\\d+))?(?:-(\\d+))?"));

	auto parse = [](const QString& v) {
		std::array<qlonglong, 6> parts{ 0, 0, 0, 0, 0, 0 };
		QRegularExpressionMatch m = re.match(v);
		if (!m.hasMatch())
			return parts;
		for (int i = 0; i < 6; ++i)
		{
			const QString cap = m.captured(i + 1);
			parts[i] = cap.isEmpty() ? 0 : cap.toLongLong();
		}
		return parts;
	};

	const std::array<qlonglong, 6> pa = parse(a);
	const std::array<qlonglong, 6> pb = parse(b);
	for (int i = 0; i < 6; ++i)
	{
		if (pa[i] != pb[i])
			return pa[i] < pb[i] ? -1 : 1;
	}
	return 0;
}

QString ScUpdateClient::localVersion()
{
	// SCRIBUS_RELEASE_VERSION comes from the generated header and is "" for a
	// developer build. fromLatin1, not QStringLiteral: the latter stores
	// UTF-16, and the release script (and anyone with `strings`) must be able
	// to find the version in the binary as plain bytes.
	static const char releaseVersion[] = SCRIBUS_RELEASE_VERSION;
	if (releaseVersion[0] != '\0')
		return QString::fromLatin1(releaseVersion);
	return ScribusAPI::getVersion();
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


// Plain words for the errors an office PC meets: a server that is off or
// unreachable, or that did not answer in time. Qt's own texts ("Operation
// canceled" for a timeout) say nothing a DTP operator can act on.
static QString suneerPlainNetworkError(QNetworkReply* reply, const QUrl& url, int timeoutMs)
{
	const QString host = url.host() + (url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString());
	switch (reply->error())
	{
		case QNetworkReply::OperationCanceledError:
		case QNetworkReply::TimeoutError:
			return QCoreApplication::translate("ScUpdateClient", "Update server %1 did not answer within %2 seconds. Is the server on and the network connected?")
			       .arg(host).arg(qMax(1, timeoutMs / 1000));
		case QNetworkReply::ConnectionRefusedError:
			return QCoreApplication::translate("ScUpdateClient", "Update server %1 refused the connection (nothing is listening there).").arg(host);
		case QNetworkReply::HostNotFoundError:
			return QCoreApplication::translate("ScUpdateClient", "Update server %1 was not found (name or address wrong, or no network).").arg(host);
		case QNetworkReply::RemoteHostClosedError:
		case QNetworkReply::NetworkSessionFailedError:
		case QNetworkReply::UnknownNetworkError:
			return QCoreApplication::translate("ScUpdateClient", "Update server %1 is not reachable: %2").arg(host, reply->errorString());
		default:
			return reply->errorString();
	}
}

QString ScUpdateClient::systemConfigPath()
{
	return QStringLiteral("/etc/scribus/update.conf");
}

QString ScUpdateClient::validateServerUrl(const QString& text, QString* why)
{
	auto fail = [why](const QString& reason) { if (why) *why = reason; return QString(); };
	QString t = text.trimmed();
	if (t.isEmpty())
		return fail(QCoreApplication::translate("ScUpdateClient", "no address"));
	// A pasted shell command ("curl -sS http://...") or several words.
	static const QRegularExpression spaces(QStringLiteral("\\s"));
	if (t.contains(spaces))
		return fail(QCoreApplication::translate("ScUpdateClient", "it contains spaces or extra words; enter only the address, like http://server:8095/scribus-updates"));
	const QUrl url(t, QUrl::StrictMode);
	if (!url.isValid() || url.host().isEmpty())
		return fail(QCoreApplication::translate("ScUpdateClient", "it is not a valid address"));
	const QString scheme = url.scheme().toLower();
	if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
		return fail(QCoreApplication::translate("ScUpdateClient", "it must start with http:// or https://"));
	if (!url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
		return fail(QCoreApplication::translate("ScUpdateClient", "it must be a plain address without login, ? or #"));
	// Retired servers. Expressed by host name and port, never by address, so
	// this stays true for whatever the LAN numbering is:
	//  - the mDNS name of the first laptop server, on any port;
	//  - port 8081, which only the laptop and the first LAN server ever used;
	//  - /scribus-updates on the default port 80, the short-lived path through
	//    the workflow server's own web server (now port 8095).
	const QString host = url.host().toLower();
	const int port = url.port(scheme == QLatin1String("https") ? 443 : 80);
	const QString path = url.path();
	if (host == QLatin1String("scribus-updates.local") || host.endsWith(QLatin1String(".scribus-updates.local")))
		return fail(QCoreApplication::translate("ScUpdateClient", "that server (scribus-updates.local) no longer exists"));
	if (port == 8081)
		return fail(QCoreApplication::translate("ScUpdateClient", "port 8081 belonged to the old update server, which no longer exists"));
	if (port == 80 && scheme == QLatin1String("http") && path.startsWith(QLatin1String("/scribus-updates")))
		return fail(QCoreApplication::translate("ScUpdateClient", "the update server moved to port 8095"));
	while (t.endsWith('/'))
		t.chop(1);
	if (t.endsWith(QLatin1String("/latest.json")))
		t.chop(12);
	if (why)
		why->clear();
	return t;
}

void ScUpdateClient::clearSavedServerUrl()
{
	QSettings settings(QStringLiteral("Faircode"), QStringLiteral("ScribusUpdater"));
	settings.remove(QStringLiteral("serverUrl"));
}

ScUpdateSettings ScUpdateClient::effectiveSettings(bool readKeyStore)
{
	ScUpdateSettings result;
	// 1. What this user entered in Update Settings - if it is usable. A broken
	// or retired saved URL used to override update.conf forever ("Protocol ""
	// is unknown" for a pasted "//host/latest.json"); now it is dropped and
	// the system-wide file wins.
	QSettings settings(QStringLiteral("Faircode"), QStringLiteral("ScribusUpdater"));
	const QString rawUserUrl = settings.value(QStringLiteral("serverUrl")).toString();
	if (!rawUserUrl.trimmed().isEmpty())
	{
		QString why;
		const QString userUrl = validateServerUrl(rawUserUrl, &why);
		if (!userUrl.isEmpty())
		{
			result.url = userUrl;
			result.apiKey = readKeyStore ? ScUpdateKeyStore::load() : QString();
			result.source = QStringLiteral("settings");
			return result;
		}
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			qWarning().noquote() << "Updater: ignoring the saved update server URL (" << why << "); using" << systemConfigPath();
		}
		settings.remove(QStringLiteral("serverUrl"));
		result.ignoredUserUrl = QCoreApplication::translate("ScUpdateClient",
			"The update server address saved in Settings was ignored because %1. The system-wide address from %2 is used instead.").arg(why, systemConfigPath());
	}
	// 2. The system-wide file (installed by the .deb / cmake --install).
	QFile conf(systemConfigPath());
	if (conf.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QTextStream in(&conf);
		while (!in.atEnd())
		{
			const QString line = in.readLine().trimmed();
			if (line.isEmpty() || line.startsWith('#'))
				continue;
			const int eq = line.indexOf('=');
			if (eq <= 0)
				continue;
			const QString key = line.left(eq).trimmed();
			const QString value = line.mid(eq + 1).trimmed();
			if (key == QLatin1String("url"))
				result.url = value;
			else if (key == QLatin1String("api_key"))
				result.apiKey = value;
		}
	}
	while (result.url.endsWith('/'))
		result.url.chop(1);
	if (!result.url.isEmpty())
		result.source = QStringLiteral("conf");
	else
		result.apiKey.clear();
	return result;
}

void ScUpdateClient::checkForUpdate(const QString& baseUrl, const QString& apiKey, int timeoutMs)
{
	QString trimmedUrl = baseUrl;
	while (trimmedUrl.endsWith('/'))
		trimmedUrl.chop(1);

	m_checkUrl = QUrl(trimmedUrl + QStringLiteral("/latest.json"));
	QNetworkRequest request(m_checkUrl);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	// An unreachable, off or hung server must end as a network error within
	// timeoutMs (connection time included), never as a reply that hangs:
	// the startup check relies on this.
	request.setTransferTimeout(timeoutMs);
	m_checkTimeoutMs = timeoutMs;
	if (!apiKey.isEmpty())
	{
		request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
		request.setRawHeader("X-API-Key", apiKey.toUtf8());
	}

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
		emit networkError(suneerPlainNetworkError(reply, m_checkUrl, m_checkTimeoutMs));
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

	m_lastInfo = info;
	if (compareVersions(remoteVersion, localVersion()) > 0)
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
	if (!apiKey.isEmpty())
	{
		request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
		request.setRawHeader("X-API-Key", apiKey.toUtf8());
	}

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
			QString message = suneerPlainNetworkError(reply, reply->url(), 0);
			if (httpStatus == 401 || httpStatus == 403)
				message = tr("Invalid API key while downloading the update.");
			else if (httpStatus == 404)
				message = tr("The update file is missing on the server (%1).").arg(reply->url().fileName());
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
