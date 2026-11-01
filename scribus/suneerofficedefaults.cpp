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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
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

// ---- merge: what the office has and the user lacks is added, the user's own entries stay ----

static int mergeDesignStyles(const QString& shipped, const QString& user)
{
	QSettings s(shipped, QSettings::IniFormat), u(user, QSettings::IniFormat);
	// a style the user has already: the same tooltip, or the same content (styles list, columns, photo) under
	// another name - a renamed style is not added a second time
	auto signature = [](QSettings& q) {
		return q.value("styles").toStringList().join(QLatin1Char('|')).simplified().toLower() + QLatin1Char('#') + q.value("columns").toString()
		       + QLatin1Char('#') + q.value("imagePosition").toString().toLower();
	};
	QStringList have;
	QSet<QString> usedKeys;
	const int n = u.beginReadArray("styles");
	for (int i = 0; i < n; ++i)
	{
		u.setArrayIndex(i);
		have << u.value("tooltip").toString().trimmed().toLower() << signature(u);
		const QString k = u.value("shortcut").toString().trimmed();
		if (!k.isEmpty())
			usedKeys.insert(k);
	}
	u.endArray();
	QList<QVariantMap> add;
	const int m = s.beginReadArray("styles");
	for (int i = 0; i < m; ++i)
	{
		s.setArrayIndex(i);
		const QString tip = s.value("tooltip").toString().trimmed().toLower();
		if (tip.isEmpty() || have.contains(tip) || have.contains(signature(s)))
			continue;
		QVariantMap e;
		for (const QString& k : s.childKeys())
			e.insert(k, s.value(k));
		if (usedKeys.contains(e.value("shortcut").toString().trimmed()))
			e.insert("shortcut", QString());       // the user's key stays with the user's style
		add << e;
	}
	s.endArray();
	if (add.isEmpty())
		return 0;
	u.beginWriteArray("styles");
	for (int i = 0; i < add.size(); ++i)
	{
		u.setArrayIndex(n + i);
		for (auto it = add[i].constBegin(); it != add[i].constEnd(); ++it)
			u.setValue(it.key(), it.value());
	}
	u.endArray();
	if (!u.contains("General/clearManualFormatting") && s.contains("General/clearManualFormatting"))
		u.setValue("General/clearManualFormatting", s.value("General/clearManualFormatting"));
	u.sync();
	return add.size();
}

static int mergeShortcuts(const QString& shipped, const QString& user)
{
	QSettings s(shipped, QSettings::IniFormat), u(user, QSettings::IniFormat);
	QSet<QString> usedKeys;
	for (const QString& k : u.allKeys())
		usedKeys.insert(u.value(k).toString().trimmed());
	int added = 0;
	for (const QString& k : s.allKeys())
	{
		if (u.contains(k) || usedKeys.contains(s.value(k).toString().trimmed()))
			continue;
		u.setValue(k, s.value(k));
		++added;
	}
	u.sync();
	return added;
}

static int mergeGradients(const QString& shipped, const QString& user)
{
	QJsonObject s = readJson(shipped), u = readJson(user);
	QSet<QString> have;
	QJsonArray presets = u.value("presets").toArray();
	for (const QJsonValue& v : presets)
		have.insert(v.toObject().value("name").toString().trimmed().toLower());
	int added = 0;
	for (const QJsonValue& v : s.value("presets").toArray())
	{
		const QString name = v.toObject().value("name").toString().trimmed().toLower();
		if (name.isEmpty() || have.contains(name))
			continue;
		presets << v;
		++added;
	}
	if (added == 0)
		return 0;
	u.insert("presets", presets);
	writeJson(user, u);
	return added;
}

static int mergeFile(const QString& name, const QString& shipped, const QString& user)
{
	if (name == QLatin1String("SuneerDesignStyle.conf"))
		return mergeDesignStyles(shipped, user);
	if (name == QLatin1String("ParagraphStyleShortcuts.conf"))
		return mergeShortcuts(shipped, user);
	if (name == QLatin1String("suneer-text-gradients.json"))
		return mergeGradients(shipped, user);
	return 0;
}

QStringList syncAtStartup(QWidget* parent)
{
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
	bool changed = false;

	struct Pending { QString name, shipped, user, shippedHash; };
	QList<Pending> ask;
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
		const QString received = receipt.value(name).toString();
		if (received == shippedHash)
		{
			out << QStringLiteral("%1: this version received already").arg(name);
			continue;
		}
		const QString userHash = QFile::exists(user) ? sha256(user) : QString();
		QStringList known { received };
		for (const QJsonValue& v : info.value("previous").toArray())
			known << v.toString();
		const bool untouched = userHash.isEmpty() || userHash == shippedHash || known.contains(userHash);
		if (untouched)
		{
			if (userHash == shippedHash)
				out << QStringLiteral("%1: the user's file is the office version already").arg(name);
			else if (backupAndCopy(shipped, user))
				out << QStringLiteral("%1: %2 -> office version %3 (old file kept as .bak)").arg(name, userHash.isEmpty() ? QStringLiteral("missing") : QStringLiteral("unchanged since the last office version"), version);
			else
			{
				out << QStringLiteral("%1: could not write %2").arg(name, user);
				continue;
			}
			receipt.insert(name, shippedHash);
			changed = true;
			continue;
		}
		ask << Pending { name, shipped, user, shippedHash };
	}
	if (!ask.isEmpty())
	{
		QStringList names;
		for (const Pending& p : ask)
			names << p.name;
		QString answer = qEnvironmentVariable("SUNEER_OFFICEDEF_ANSWER").toLower();
		if (answer.isEmpty())
		{
			QMessageBox box(QMessageBox::Question, QObject::tr("Office settings updated"),
			                QObject::tr("Scribus %1 brings new office settings for:\n    %2\n\nYou have changed these on this computer. "
			                            "Use the office version (your file is kept as a .bak copy), keep yours, or merge "
			                            "(the office's new styles / shortcuts / presets are added, yours stay)?")
			                    .arg(version, names.join(QStringLiteral("\n    "))),
			                QMessageBox::NoButton, parent);
			QPushButton* office = box.addButton(QObject::tr("Use office version"), QMessageBox::AcceptRole);
			QPushButton* keep = box.addButton(QObject::tr("Keep mine"), QMessageBox::RejectRole);
			QPushButton* merge = box.addButton(QObject::tr("Merge"), QMessageBox::ActionRole);
			box.setDefaultButton(merge);
			box.exec();
			answer = box.clickedButton() == office ? QStringLiteral("office") : box.clickedButton() == merge ? QStringLiteral("merge") : QStringLiteral("keep");
			Q_UNUSED(keep);
		}
		for (const Pending& p : ask)
		{
			if (answer == QLatin1String("office"))
			{
				if (!backupAndCopy(p.shipped, p.user))
				{
					out << QStringLiteral("%1: could not write %2").arg(p.name, p.user);
					continue;
				}
				out << QStringLiteral("%1: local changes -> office version %2 (old file kept as .bak)").arg(p.name, version);
			}
			else if (answer == QLatin1String("merge"))
			{
				const QString bak = p.user + QStringLiteral(".bak-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
				QFile::remove(bak);
				QFile::copy(p.user, bak);
				const int n = mergeFile(p.name, p.shipped, p.user);
				out << QStringLiteral("%1: local changes -> merged, %2 new entr%3 added (old file kept as .bak)").arg(p.name).arg(n).arg(n == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
			}
			else
				out << QStringLiteral("%1: local changes -> kept (not asked again for %2)").arg(p.name, version);
			receipt.insert(p.name, p.shippedHash);
			changed = true;
		}
	}
	if (changed)
	{
		receipt.insert(QStringLiteral("version"), version);
		QDir().mkpath(QFileInfo(receiptPath()).absolutePath());
		writeJson(receiptPath(), receipt);
	}
	// the dbi shortcut set: the Default when this profile never chose one
	if (Prefs_KeyboardShortcuts::adoptShippedSetIfUnchosen())
		out << QStringLiteral("keyset: \"%1\" made the Default shortcut set (none was chosen before)").arg(Prefs_KeyboardShortcuts::newspaperDefaultSetName());
	else
		out << QStringLiteral("keyset: Default set \"%1\" left as chosen").arg(Prefs_KeyboardShortcuts::defaultSetName());
	return out;
}

} // namespace SuneerOfficeDefaults
