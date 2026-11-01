/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#include "suneerofficedefaults.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

#include "prefsmanager.h"
#include "scpaths.h"
#include "ui/preferences/prefs_keyboardshortcuts.h"

namespace SuneerOfficeDefaults
{

static const char* kManifest = "office-defaults.json";
static const char* kReceipt = "office-defaults-received.json";

static QString sha256(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}

QString shippedDir()
{
	const QString env = qEnvironmentVariable("SUNEER_OFFICE_DEFAULTS_DIR");
	if (!env.isEmpty())
		return env;
	const QString share = QDir::cleanPath(ScPaths::instance().shareDir() + "/office-defaults");
	if (QDir(share).exists())
		return share;
	return QStringLiteral("/usr/local/share/scribus/office-defaults");
}

QString userPathFor(const QString& fileName)
{
	if (fileName == QLatin1String("SuneerDesignStyle.conf"))
		return QSettings("Scribus", "SuneerDesignStyle").fileName();
	if (fileName == QLatin1String("ParagraphStyleShortcuts.conf"))
		return QSettings("Scribus", "ParagraphStyleShortcuts").fileName();
	if (fileName == QLatin1String("suneer-text-gradients.json"))
		return PrefsManager::instance().preferencesLocation() + QStringLiteral("/suneer-text-gradients.json");
	return QString();
}

static QString receiptPath()
{
	return PrefsManager::instance().preferencesLocation() + QLatin1Char('/') + QLatin1String(kReceipt);
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
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
	return true;
}

static bool backupAndCopy(const QString& from, const QString& to)
{
	QDir().mkpath(QFileInfo(to).absolutePath());
	if (QFile::exists(to))
	{
		const QString bak = to + QStringLiteral(".bak-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
		QFile::remove(bak);
		QFile::rename(to, bak);
	}
	return QFile::copy(from, to);
}

QStringList syncAtStartup(QWidget* parent)
{
	Q_UNUSED(parent);
	QStringList out;
	const QString dir = shippedDir();
	const QJsonObject manifest = readJson(dir + QLatin1Char('/') + QLatin1String(kManifest));
	const QString version = manifest.value("version").toString();
	const QJsonObject files = manifest.value("files").toObject();
	if (files.isEmpty())
	{
		out << QStringLiteral("office defaults: none shipped (%1)").arg(dir);
		return out;
	}
	QJsonObject receipt = readJson(receiptPath());
	const bool newVersion = receipt.value(QStringLiteral("version")).toString() != version;
	bool changed = false;
	for (auto it = files.constBegin(); it != files.constEnd(); ++it)
	{
		const QString name = it.key();
		const QJsonObject info = it.value().toObject();
		const QString shippedHash = info.value("sha256").toString();
		const QString shipped = dir + QLatin1Char('/') + name;
		const QString user = userPathFor(name);
		if (user.isEmpty() || !QFile::exists(shipped) || shippedHash.isEmpty())
		{
			out << QStringLiteral("%1: not handled (%2)").arg(name, user.isEmpty() ? QStringLiteral("unknown file") : QStringLiteral("not in the package"));
			continue;
		}
		if (!newVersion && receipt.value(name).toString() == shippedHash)
		{
			out << QStringLiteral("%1: this version received already").arg(name);
			continue;
		}
		// 2026-11-01 (user decision): no question. A new office version always replaces the user's
		// file; the old one stays next to it as .bak-<date>, and the settings backup made before an
		// update (SuneerRestorePoint) holds the whole set for "Go back to previous version".
		const QString userHash = QFile::exists(user) ? sha256(user) : QString();
		if (userHash == shippedHash)
			out << QStringLiteral("%1: the user's file is the office version already").arg(name);
		else if (backupAndCopy(shipped, user))
			out << QStringLiteral("%1: %2 -> office version %3 (old file kept as .bak)").arg(name,
				userHash.isEmpty() ? QStringLiteral("missing") : QStringLiteral("replaced"), version);
		else
		{
			out << QStringLiteral("%1: could not write %2").arg(name, user);
			continue;
		}
		receipt.insert(name, shippedHash);
		changed = true;
	}
	// the dbi shortcut set: the Default with every new office version (a profile's own copy of the
	// shipped set, when it differs, is set aside as .bak so the shipped keys apply); the receipt
	// remembers the version so that a user's later choice of another set is left alone until the
	// next version
	const QString keysetSeen = receipt.value(QStringLiteral("keyset-version")).toString();
	if (!version.isEmpty() && keysetSeen != version)
	{
		out << QStringLiteral("keyset: \"%1\" made the Default shortcut set for %2%3")
		       .arg(Prefs_KeyboardShortcuts::newspaperDefaultSetName(), version,
		            Prefs_KeyboardShortcuts::adoptShippedSet() ? QStringLiteral(" (own copy of the set kept as .bak)") : QString());
		receipt.insert(QStringLiteral("keyset-version"), version);
		changed = true;
	}
	else
		out << QStringLiteral("keyset: Default set \"%1\" left as chosen").arg(Prefs_KeyboardShortcuts::defaultSetName());
	if (changed)
	{
		receipt.insert(QStringLiteral("version"), version);
		QDir().mkpath(QFileInfo(receiptPath()).absolutePath());
		writeJson(receiptPath(), receipt);
	}
	return out;
}

} // namespace SuneerOfficeDefaults
