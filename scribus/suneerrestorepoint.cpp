/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#include "suneerrestorepoint.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QtGlobal>
#include <cstring>

#include "prefsmanager.h"
#include "scpaths.h"
#include "scupdateclient.h"

namespace SuneerRestorePoint
{

static const char* kInfo = "restore-point.json";
static const char* kMarker = "update-rollback.json";
static const int kKeep = 3;

QString cacheDir()
{
	const QString env = qEnvironmentVariable("SUNEER_UPDATE_CACHE_DIR");
	return env.isEmpty() ? QStringLiteral("/var/cache/scribus-updates") : env;
}

QString backupRoot()
{
	// next to the Scribus/*.conf files (QSettings "Scribus"), as asked: ~/.config/Scribus/backup/
	return QFileInfo(QSettings(QStringLiteral("Scribus"), QStringLiteral("SuneerDesignStyle")).fileName()).absolutePath() + QStringLiteral("/backup");
}

QString debFileName(const QString& version)
{
	return QStringLiteral("scribus_%1_amd64.deb").arg(version);
}

bool isPackageVersion(const QString& version)
{
	static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+\\.\\d+-\\d{8}-\\d+(\\.local\\d+)?$"));
	return re.match(version).hasMatch();
}

QString cachedDeb(const QString& version)
{
	const QString p = cacheDir() + QLatin1Char('/') + debFileName(version);
	return QFileInfo(p).isFile() ? p : QString();
}

static QJsonObject readJson(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return QJsonObject();
	return QJsonDocument::fromJson(f.readAll()).object();
}

static bool writeJson(const QString& path, const QJsonObject& o)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
	return true;
}

// the files a restore point holds: {backup-relative name, absolute original}
static QList<QPair<QString, QString>> settingsFiles()
{
	QList<QPair<QString, QString>> out;
	const QString confDir = QFileInfo(QSettings(QStringLiteral("Scribus"), QStringLiteral("SuneerDesignStyle")).fileName()).absolutePath();
	for (const QString& f : QDir(confDir).entryList({ QStringLiteral("*.conf") }, QDir::Files))
		out << qMakePair(QStringLiteral("Scribus/") + f, confDir + QLatin1Char('/') + f);
	const QString prefs = QDir::cleanPath(PrefsManager::instance().preferencesLocation());
	for (const QString& f : QDir(prefs).entryList({ QStringLiteral("prefs*.xml"), QStringLiteral("scribus*.rc"), QStringLiteral("suneer-text-gradients.json"),
	                                               QStringLiteral("office-defaults-received.json"), QStringLiteral("scribusshapes.xml") }, QDir::Files))
		out << qMakePair(QStringLiteral("prefs/") + f, prefs + QLatin1Char('/') + f);
	const QString sets = prefs + QStringLiteral("/shortcut-sets");
	for (const QString& f : QDir(sets).entryList({ QStringLiteral("*.xml") }, QDir::Files))
		out << qMakePair(QStringLiteral("prefs/shortcut-sets/") + f, sets + QLatin1Char('/') + f);
	return out;
}

static void pruneBackups()
{
	QDir root(backupRoot());
	QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);   // <yyyyMMdd-HHmmss>_<version>: name order = time order
	dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&root](const QString& d) { return !QFile::exists(root.filePath(d) + QLatin1Char('/') + QLatin1String(kInfo)); }), dirs.end());
	while (dirs.size() > kKeep)
		QDir(root.filePath(dirs.takeFirst())).removeRecursively();
}

QString saveSettings(const QString& version, QString* error)
{
	auto fail = [error](const QString& why) { if (error) *error = why; return QString(); };
	const QString dir = backupRoot() + QLatin1Char('/') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('_') + version;
	if (!QDir().mkpath(dir))
		return fail(QStringLiteral("cannot create %1").arg(dir));
	QJsonArray files;
	for (const auto& f : settingsFiles())
	{
		const QString to = dir + QLatin1Char('/') + f.first;
		QDir().mkpath(QFileInfo(to).absolutePath());
		if (!QFile::copy(f.second, to))
			return fail(QStringLiteral("cannot copy %1").arg(f.second));
		files << QJsonObject { { QStringLiteral("backup"), f.first }, { QStringLiteral("path"), f.second } };
	}
	QJsonObject info { { QStringLiteral("version"), version }, { QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate) },
	                   { QStringLiteral("files"), files } };
	if (!writeJson(dir + QLatin1Char('/') + QLatin1String(kInfo), info))
		return fail(QStringLiteral("cannot write %1").arg(dir));
	pruneBackups();
	return dir;
}

QList<Point> points()
{
	QMap<QString, Point> byVersion;
	static const QRegularExpression debRe(QStringLiteral("^scribus_(.+)_amd64\\.deb$"));
	for (const QFileInfo& fi : QDir(cacheDir()).entryInfoList({ QStringLiteral("scribus_*_amd64.deb") }, QDir::Files))
	{
		const QRegularExpressionMatch m = debRe.match(fi.fileName());
		if (!m.hasMatch() || !isPackageVersion(m.captured(1)))
			continue;
		Point& p = byVersion[m.captured(1)];
		p.version = m.captured(1);
		p.debPath = fi.absoluteFilePath();
		p.when = fi.lastModified();
	}
	QDir root(backupRoot());
	for (const QString& d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
	{
		const QJsonObject info = readJson(root.filePath(d) + QLatin1Char('/') + QLatin1String(kInfo));
		const QString v = info.value(QStringLiteral("version")).toString();
		if (v.isEmpty())
			continue;
		Point& p = byVersion[v];
		p.version = v;
		p.settingsDir = root.filePath(d);           // name order: the newest backup of a version wins
		const QDateTime when = QDateTime::fromString(info.value(QStringLiteral("date")).toString(), Qt::ISODate);
		if (when.isValid() && (!p.when.isValid() || when > p.when))
			p.when = when;
	}
	QList<Point> out = byVersion.values();
	std::sort(out.begin(), out.end(), [](const Point& a, const Point& b) { return ScUpdateClient::compareVersions(a.version, b.version) > 0; });
	return out;
}

QString program()
{
	const QString env = qEnvironmentVariable("SUNEER_UPDATE_PKEXEC");
	return env.isEmpty() ? QStringLiteral("pkexec") : env;
}

QStringList installArguments(const QString& debPath, const QString& version, const QStringList& alsoCache)
{
	// One fixed script, paths only as positional arguments. Everything a user-writable path could
	// hand it is checked by dpkg-deb before it is copied anywhere or installed.
	static const char* script =
		"set -e\n"
		"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin; export PATH\n"
		"deb=\"$1\"; ver=\"$2\"; cache=\"$3\"; shift 3\n"
		"[ \"$(dpkg-deb -f \"$deb\" Package)\" = scribus ] || { echo \"$deb is not the scribus package\" >&2; exit 3; }\n"
		"[ \"$(dpkg-deb -f \"$deb\" Version)\" = \"$ver\" ] || { echo \"$deb is not version $ver\" >&2; exit 3; }\n"
		"mkdir -p \"$cache\"; chmod 755 \"$cache\"\n"
		"for f in \"$deb\" \"$@\"; do\n"
		"  [ -f \"$f\" ] || continue\n"
		"  [ \"$(dpkg-deb -f \"$f\" Package)\" = scribus ] || continue\n"
		"  v=$(dpkg-deb -f \"$f\" Version) || continue\n"
		"  t=\"$cache/scribus_${v}_amd64.deb\"\n"
		"  cp -f \"$f\" \"$t.tmp\" && chmod 644 \"$t.tmp\" && mv -f \"$t.tmp\" \"$t\"\n"
		"done\n"
		"dpkg -i \"$deb\"\n"
		"ls -t \"$cache\"/scribus_*_amd64.deb 2>/dev/null | tail -n +4 | while read -r old; do rm -f \"$old\"; done\n"
		"exit 0\n";
	QStringList args { QStringLiteral("/bin/sh"), QStringLiteral("-c"), QString::fromLatin1(script), QStringLiteral("sh"), debPath, version, cacheDir() };
	args << alsoCache;
	return args;
}

bool restoreAfterExit(const QString& settingsDir, qint64 pid, bool restart, QString* error)
{
	auto fail = [error](const QString& why) { if (error) *error = why; return false; };
	const QJsonObject info = readJson(settingsDir + QLatin1Char('/') + QLatin1String(kInfo));
	const QJsonArray files = info.value(QStringLiteral("files")).toArray();
	if (files.isEmpty())
		return fail(QStringLiteral("%1 holds no settings").arg(settingsDir));
	// src<TAB>dst per line; paths may hold spaces (shortcut-sets/My Default.xml) but never a tab
	QFile list(settingsDir + QStringLiteral("/restore.list"));
	if (!list.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return fail(QStringLiteral("cannot write %1").arg(list.fileName()));
	for (const QJsonValue& v : files)
	{
		const QJsonObject o = v.toObject();
		const QString src = settingsDir + QLatin1Char('/') + o.value(QStringLiteral("backup")).toString();
		const QString dst = o.value(QStringLiteral("path")).toString();
		if (!QFile::exists(src) || dst.isEmpty() || src.contains(QLatin1Char('\t')) || dst.contains(QLatin1Char('\t')))
			continue;
		list.write((src + QLatin1Char('\t') + dst + QLatin1Char('\n')).toUtf8());
	}
	list.close();
	QString scribus = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath() + QStringLiteral("/scribus");
	if (!QFileInfo(scribus).isExecutable())
		scribus = QCoreApplication::applicationFilePath();
	const QString script = QStringLiteral(
		"pid=\"$1\"; list=\"$2\"; restart=\"$3\"; app=\"$4\"; log=\"$5\"\n"
		"while kill -0 \"$pid\" 2>/dev/null; do sleep 1; done; sleep 1\n"
		"tab=\"$(printf '\\t')\"\n"
		"while IFS=\"$tab\" read -r src dst; do mkdir -p \"$(dirname \"$dst\")\"; cp -f \"$src\" \"$dst\" && echo \"restored $dst\" >> \"$log\"; done < \"$list\"\n"
		"echo \"done $(date)\" >> \"$log\"\n"
		"[ \"$restart\" = 1 ] && exec \"$app\"\n"
		"exit 0\n");
	const QString log = settingsDir + QStringLiteral("/restore.log");
	QFile::remove(log);
	// the restarted Scribus must not inherit a self-test variable of this one (SUNEER_UPDATE_SELFTEST,
	// SUNEER_ROLLBACK_SELFTEST, SUNEER_OFFICEDEF_SELFTEST: a test would run itself again)
	QProcess waiter;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	for (const QString& k : env.keys())
		if (k.startsWith(QLatin1String("SUNEER_UPDATE")) || k.startsWith(QLatin1String("SUNEER_ROLLBACK")) || k.startsWith(QLatin1String("SUNEER_OFFICEDEF")))
			env.remove(k);
	waiter.setProcessEnvironment(env);
	waiter.setProgram(QStringLiteral("/bin/sh"));
	waiter.setArguments({ QStringLiteral("-c"), script, QStringLiteral("sh"), QString::number(pid), list.fileName(),
	                      restart ? QStringLiteral("1") : QStringLiteral("0"), scribus, log });
	waiter.setStandardOutputFile(QProcess::nullDevice());
	waiter.setStandardErrorFile(log, QIODevice::Append);
	if (!waiter.startDetached())
		return fail(QStringLiteral("could not start the restore step"));
	return true;
}

static QString markerPath()
{
	return QDir::cleanPath(PrefsManager::instance().preferencesLocation()) + QLatin1Char('/') + QLatin1String(kMarker);
}

void writeRollbackMarker(const QString& fromVersion, const QString& toVersion)
{
	writeJson(markerPath(), QJsonObject { { QStringLiteral("from"), fromVersion }, { QStringLiteral("to"), toVersion },
	                                      { QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate) } });
}

QString rollbackLeftVersion(QDateTime* when)
{
	const QJsonObject o = readJson(markerPath());
	const QString from = o.value(QStringLiteral("from")).toString();
	if (from.isEmpty())
		return QString();
	// the user went on to another version by themselves: the note is over
	if (ScUpdateClient::compareVersions(ScUpdateClient::localVersion(), o.value(QStringLiteral("to")).toString()) != 0)
	{
		clearRollbackMarker();
		return QString();
	}
	if (when)
		*when = QDateTime::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
	return from;
}

void clearRollbackMarker()
{
	QFile::remove(markerPath());
}

bool verifyFileSignature(const QString& filePath, const QString& signatureBase64, QString* error)
{
	QFile f(filePath);
	if (!f.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = QStringLiteral("cannot read %1").arg(filePath);
		return false;
	}
	return ScUpdateClient::verifySignature(f.readAll(), signatureBase64.trimmed(), ScUpdateClient::releasePublicKey(), error);
}

} // namespace SuneerRestorePoint

// ------------------------------------------------------------------ fetcher ---

SuneerDebFetcher::SuneerDebFetcher(QObject* parent)
	: QObject(parent)
	, m_client(new ScUpdateClient(this))
{
	connect(m_client, &ScUpdateClient::downloadProgress, this, &SuneerDebFetcher::progress);
	connect(m_client, &ScUpdateClient::downloadFailed, this, [this](const QString& m) {
		QFile::remove(m_debPath);
		QFile::remove(m_sigPath);
		emit failed(m_sigStage ? tr("The signature file for %1 could not be downloaded: %2").arg(m_version, m) : m);
	});
	connect(m_client, &ScUpdateClient::downloadFinished, this, [this](const QString& path) {
		if (!m_sigStage)
		{
			m_sigStage = true;
			m_client->downloadFile(QUrl(m_server.toString() + QLatin1Char('/') + SuneerRestorePoint::debFileName(m_version) + QStringLiteral(".sig")), m_server, m_apiKey, m_sigPath);
			return;
		}
		QFile sig(path);
		QString why;
		if (!sig.open(QIODevice::ReadOnly) || !SuneerRestorePoint::verifyFileSignature(m_debPath, QString::fromLatin1(sig.readAll()), &why))
		{
			QFile::remove(m_debPath);
			QFile::remove(m_sigPath);
			emit failed(tr("The package %1 from the server does not carry a valid release signature: %2").arg(m_version, why));
			return;
		}
		QFile::remove(m_sigPath);
		emit done(m_debPath);
	});
}

void SuneerDebFetcher::fetch(const QString& version, const QString& serverUrl, const QString& apiKey)
{
	m_version = version;
	QString base = serverUrl.trimmed();
	if (base.endsWith(QLatin1String("/latest.json")))        // ScUpdateInfo::serverUrl is the manifest URL
		base.chop(int(strlen("/latest.json")));
	while (base.endsWith(QLatin1Char('/')))
		base.chop(1);
	m_server = QUrl(base);
	m_apiKey = apiKey;
	m_sigStage = false;
	m_debPath = ScPaths::downloadDir() + SuneerRestorePoint::debFileName(version);
	m_sigPath = m_debPath + QStringLiteral(".sig");
	if (!SuneerRestorePoint::isPackageVersion(version) || serverUrl.isEmpty())
	{
		emit failed(serverUrl.isEmpty() ? tr("No update server is set up, so the package %1 cannot be downloaded.").arg(version)
		                                : tr("%1 is not a release version.").arg(version));
		return;
	}
	m_client->downloadFile(QUrl(m_server.toString() + QLatin1Char('/') + SuneerRestorePoint::debFileName(version)), m_server, apiKey, m_debPath);
}

void SuneerDebFetcher::cancel()
{
	m_client->cancelDownload();
}
