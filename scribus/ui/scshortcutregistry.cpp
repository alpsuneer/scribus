/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scshortcutregistry.h"

#include <QAction>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QShortcut>
#include <QTextEdit>
#include <QTextStream>
#include <QDebug>

#include "actionmanager.h"
#include "prefsmanager.h"
#include "scraction.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "styles/charstyle.h"
#include "styles/paragraphstyle.h"
#include "styles/styleset.h"

namespace
{
	QString portable(const QKeySequence& k) { return k.toString(QKeySequence::PortableText); }
	QString native(const QKeySequence& k) { return k.toString(QKeySequence::NativeText); }

	// GTK accelerator ("<Control><Alt>1", "<Primary>space", "<Super>l") -> Qt portable text.
	QString gtkAccelToQt(const QString& accel)
	{
		QString s = accel.trimmed();
		if (s.isEmpty())
			return QString();
		QString mods;
		static const QRegularExpression modRe("<([A-Za-z0-9_]+)>");
		QRegularExpressionMatchIterator it = modRe.globalMatch(s);
		while (it.hasNext())
		{
			const QString m = it.next().captured(1).toLower();
			if (m == "control" || m == "primary" || m == "ctrl") mods += "Ctrl+";
			else if (m == "shift") mods += "Shift+";
			else if (m == "alt" || m == "mod1") mods += "Alt+";
			else if (m == "super" || m == "mod4" || m == "meta") mods += "Meta+";
			else return QString();            // Hyper, Mod2..: not something Qt names
		}
		QString key = s;
		key.remove(modRe);
		if (key.isEmpty())
			return QString();
		static const QMap<QString, QString> names {
			{"space", "Space"}, {"equal", "="}, {"minus", "-"}, {"plus", "+"}, {"comma", ","},
			{"period", "."}, {"slash", "/"}, {"backslash", "\\"}, {"grave", "`"}, {"apostrophe", "'"},
			{"semicolon", ";"}, {"bracketleft", "["}, {"bracketright", "]"}, {"Return", "Return"},
			{"Escape", "Esc"}, {"BackSpace", "Backspace"}, {"Tab", "Tab"}, {"Delete", "Del"},
			{"Print", "Print"}, {"Up", "Up"}, {"Down", "Down"}, {"Left", "Left"}, {"Right", "Right"},
			{"Home", "Home"}, {"End", "End"}, {"Page_Up", "PgUp"}, {"Page_Down", "PgDown"},
			{"Insert", "Ins"}, {"Pause", "Pause"}, {"Menu", "Menu"}
		};
		if (names.contains(key))
			key = names.value(key);
		else if (key.length() == 1)
			key = key.toUpper();
		else if (key.startsWith("XF86") || key.endsWith("_L") || key.endsWith("_R"))
			return QString();                 // media keys / bare modifiers never collide with ours
		const QKeySequence ks(mods + key, QKeySequence::PortableText);
		return ks.isEmpty() ? QString() : portable(ks);
	}

	// fcitx5 key ("Control+space", "Shift+Alt_L", "Control+Shift+F") -> Qt portable text.
	QString fcitxKeyToQt(const QString& spec)
	{
		QStringList parts = spec.trimmed().split('+', Qt::SkipEmptyParts);
		if (parts.isEmpty())
			return QString();
		QString last = parts.takeLast();
		if (last.endsWith("_L") || last.endsWith("_R") || last.isEmpty())
			return QString();                 // modifier-only hotkey (e.g. Shift_L): no collision
		QString gtk;
		for (const QString& p : parts)
			gtk += "<" + p + ">";
		return gtkAccelToQt(gtk + last);
	}

	QString columnJoin(const QList<ScShortcutRegistry::Owner>& owners, const QString& sep)
	{
		QStringList l;
		for (const auto& o : owners)
			l << o.label();
		return l.join(sep);
	}
}

QString ScShortcutRegistry::Owner::label() const
{
	QString s = kind + ": " + name;
	if (!location.isEmpty())
		s += " (" + location + ")";
	return s;
}

ScShortcutRegistry& ScShortcutRegistry::instance()
{
	static ScShortcutRegistry reg;
	return reg;
}

ScShortcutRegistry::ScShortcutRegistry()
	: QObject(nullptr)
{
}

void ScShortcutRegistry::addProvider(const QString& providerId, Provider provider)
{
	m_providers[providerId] = std::move(provider);
}

void ScShortcutRegistry::registerShortcut(QShortcut* shortcut, const QString& panelName, const QString& what, const QString& ownerId)
{
	if (!shortcut)
		return;
	// Dead entries go when a new one comes in; QShortcuts are rebuilt often.
	for (int i = m_registered.size() - 1; i >= 0; --i)
		if (m_registered[i].shortcut.isNull())
			m_registered.removeAt(i);
	m_registered.append({ shortcut, panelName, what, ownerId });
}

// Menu actions: the main window's ScrActions, labelled through the Keyboard
// Shortcuts tables (menu name + clean text) when they know the action.
QList<ScShortcutRegistry::Owner> ScShortcutRegistry::actionOwners() const
{
	QList<Owner> out;
	ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
	if (!mw)
		return out;
	const QMap<QString, Keys>& keyActions = PrefsManager::instance().appPrefs.keyShortcutPrefs.KeyActions;
	// Top-level menu of each action, as the Keyboard Shortcuts page groups them.
	static QMap<QString, QString> menuOf;
	if (menuOf.isEmpty())
	{
		const QVector<QPair<QString, QStringList>>* menus = ActionManager::defaultMenus();
		for (int i = 0; menus && i < menus->count(); ++i)
			for (const QString& a : menus->at(i).second)
				menuOf.insert(a, menus->at(i).first);
	}
	for (auto it = mw->scrActions.constBegin(); it != mw->scrActions.constEnd(); ++it)
	{
		ScrAction* a = it.value();
		if (!a || a->shortcut().isEmpty())
			continue;
		const QString actionName = it.key();
		Owner o;
		o.kind = tr("Menu action");
		o.id = "action:" + actionName;
		o.key = a->shortcut();
		o.location = tr("Keyboard Shortcuts");
		QString text = a->text();
		text.remove('&');
		if (keyActions.contains(actionName))
		{
			const Keys& k = keyActions.value(actionName);
			if (!k.cleanMenuText.isEmpty())
				text = k.cleanMenuText;
		}
		const QString menu = keyActions.contains(actionName) && !keyActions.value(actionName).menuName.isEmpty()
			? keyActions.value(actionName).menuName : menuOf.value(actionName);
		if (!menu.isEmpty())
			text = menu + QStringLiteral(" → ") + text;
		o.name = text.isEmpty() ? actionName : text;
		o.clear = [actionName]() {
			QMap<QString, Keys>& ka = PrefsManager::instance().appPrefs.keyShortcutPrefs.KeyActions;
			if (ka.contains(actionName))
				ka[actionName].keySequence = QKeySequence();
			ScribusMainWindow* w = ScCore ? ScCore->primaryMainWindow() : nullptr;
			if (w && w->scrActions.contains(actionName) && w->scrActions[actionName])
				w->scrActions[actionName]->setShortcut(QKeySequence());
		};
		out << o;
	}
	// Plain QActions that live in this window and carry a key: Qt's own
	// QMdiSubWindow system menu ("&Close", Ctrl+W), toolbars of docks, ...
	QSet<const QObject*> seen;
	for (auto it = mw->scrActions.constBegin(); it != mw->scrActions.constEnd(); ++it)
		seen.insert(it.value());
	const QList<QAction*> others = mw->findChildren<QAction*>();
	for (QAction* a : others)
	{
		if (seen.contains(a) || a->shortcut().isEmpty() || qobject_cast<ScrAction*>(a))
			continue;
		bool live = false;
		const QList<QObject*> objs = a->associatedObjects();
		for (const QObject* ob : objs)
		{
			const QWidget* w = qobject_cast<const QWidget*>(ob);
			for (; w; w = w->parentWidget())
			{
				if (w == mw) { live = true; break; }
				if (w->isWindow() && !qobject_cast<const QMenu*>(w)) break;
			}
			if (live) break;
		}
		if (!live)
			continue;
		Owner o;
		o.kind = tr("Qt action");
		QString text = a->text(); text.remove('&');
		o.name = text.isEmpty() ? (a->objectName().isEmpty() ? tr("unnamed") : a->objectName()) : text;
		o.location = a->parent() ? a->parent()->metaObject()->className() : QStringLiteral("?");
		o.id = QString("qaction:%1/%2").arg(o.location, o.name);
		o.key = a->shortcut();
		QPointer<QAction> p = a;
		o.clear = [p]() { if (p) p->setShortcut(QKeySequence()); };
		out << o;
	}
	return out;
}

// QShortcuts our panels registered, plus any QShortcut in the main window
// nobody registered (best effort: named after its parent widget).
QList<ScShortcutRegistry::Owner> ScShortcutRegistry::customShortcutOwners(const QSet<QString>& knownIds) const
{
	QList<Owner> out;
	QSet<const QShortcut*> seen;
	for (const Registered& r : m_registered)
	{
		if (r.shortcut.isNull() || r.shortcut->key().isEmpty() || !r.shortcut->isEnabled())
			continue;
		seen.insert(r.shortcut.data());
		if (!r.ownerId.isEmpty() && knownIds.contains(r.ownerId))
			continue;                         // already listed through its provider
		Owner o;
		o.kind = tr("Custom shortcut");
		o.name = r.what;
		o.location = r.panel;
		o.id = r.ownerId.isEmpty() ? QString("qshortcut:%1/%2").arg(r.panel, r.what) : r.ownerId;
		o.key = r.shortcut->key();
		QPointer<QShortcut> sc = r.shortcut;
		o.clear = [sc]() { if (sc) { sc->setEnabled(false); sc->setKey(QKeySequence()); } };
		out << o;
	}
	ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
	if (!mw)
		return out;
	const QList<QShortcut*> all = mw->findChildren<QShortcut*>();
	for (QShortcut* sc : all)
	{
		if (seen.contains(sc) || sc->key().isEmpty() || !sc->isEnabled())
			continue;
		const QWidget* pw = qobject_cast<const QWidget*>(sc->parent());
		const bool live = sc->context() == Qt::ApplicationShortcut || (pw && pw->window() == mw);
		if (!live)
			continue;
		Owner o;
		o.kind = tr("Custom shortcut");
		o.name = sc->objectName().isEmpty() ? tr("unnamed QShortcut") : sc->objectName();
		o.location = pw ? pw->metaObject()->className() : QStringLiteral("?");
		o.id = QString("qshortcut:%1/%2").arg(o.location, o.name);
		if (knownIds.contains(o.id) || knownIds.contains(sc->objectName()))
			continue;
		o.key = sc->key();
		QPointer<QShortcut> p = sc;
		o.clear = [p]() { if (p) { p->setEnabled(false); p->setKey(QKeySequence()); } };
		out << o;
	}
	return out;
}

QList<ScShortcutRegistry::Owner> ScShortcutRegistry::allOwners() const
{
	QList<Owner> out = actionOwners();
	QSet<QString> ids;
	for (const Owner& o : out)
		ids.insert(o.id);
	for (auto it = m_providers.constBegin(); it != m_providers.constEnd(); ++it)
	{
		const QList<Owner> part = it.value()();
		for (const Owner& o : part)
		{
			if (o.key.isEmpty() || ids.contains(o.id))
				continue;
			ids.insert(o.id);
			out << o;
		}
	}
	const QList<Owner> custom = customShortcutOwners(ids);
	for (const Owner& o : custom)
	{
		if (ids.contains(o.id))
			continue;
		ids.insert(o.id);
		out << o;
	}
	out << desktopGrabs();
	return out;
}

QList<ScShortcutRegistry::Owner> ScShortcutRegistry::ownersOf(const QKeySequence& key, const QString& excludeId) const
{
	QList<Owner> out;
	if (key.isEmpty())
		return out;
	const QList<Owner> all = allOwners();
	for (const Owner& o : all)
	{
		if (!excludeId.isEmpty() && o.id == excludeId)
			continue;
		if (key.matches(o.key) == QKeySequence::ExactMatch)
			out << o;
	}
	return out;
}

QMap<QString, QList<ScShortcutRegistry::Owner>> ScShortcutRegistry::duplicates() const
{
	QMap<QString, QList<Owner>> byKey;
	const QList<Owner> all = allOwners();
	for (const Owner& o : all)
		byKey[portable(o.key)] << o;
	QMap<QString, QList<Owner>> out;
	for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it)
	{
		if (it.value().count() < 2)
			continue;
		// Two desktop grabs of one key are the desktop's business, not ours.
		int ours = 0;
		for (const Owner& o : it.value())
			if (!o.desktop)
				++ours;
		if (ours == 0)
			continue;
		out.insert(it.key(), it.value());
	}
	return out;
}

QString ScShortcutRegistry::conflictText(const QKeySequence& key, const QList<Owner>& owners, bool multiLine)
{
	if (owners.isEmpty())
		return QString();
	if (multiLine)
		return tr("%1 is already used by:\n  %2").arg(native(key), columnJoin(owners, "\n  "));
	return tr("%1 is already used by: %2").arg(native(key), columnJoin(owners, "; "));
}

ScShortcutRegistry::Resolution ScShortcutRegistry::askOnConflict(QWidget* parent, const QKeySequence& key, const QList<Owner>& owners)
{
	if (owners.isEmpty())
		return Replace;
	bool anyClearable = false, anyDesktop = false, anyAction = false;
	for (const Owner& o : owners)
	{
		if (o.desktop) anyDesktop = true;
		else if (o.clear) anyClearable = true;
		if (o.id.startsWith("action:")) anyAction = true;
	}
	QString text = conflictText(key, owners, true);
	if (anyDesktop)
		text += "\n\n" + tr("A key the desktop or fcitx5 grabs never reaches Scribus; Scribus cannot remove it from there.");
	if (anyAction)
		text += "\n\n" + tr("A menu action's key is cleared for this session and in the saved preferences. "
		                    "If a Default shortcut set is applied again at the next start, change the set in "
		                    "Preferences > Keyboard Shortcuts as well.");
	QMessageBox box(QMessageBox::Warning, tr("Shortcut already in use"), text, QMessageBox::NoButton, parent);
	QPushButton* replaceBtn = nullptr;
	if (anyClearable)
		replaceBtn = box.addButton(tr("Replace (remove it from there)"), QMessageBox::AcceptRole);
	else if (anyDesktop)
		replaceBtn = box.addButton(tr("Keep it anyway"), QMessageBox::AcceptRole);
	QPushButton* otherBtn = box.addButton(tr("Choose another key"), QMessageBox::ActionRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(otherBtn);
	box.exec();
	if (box.clickedButton() == replaceBtn && replaceBtn)
	{
		for (const Owner& o : owners)
			if (!o.desktop && o.clear)
				o.clear();
		notifyChanged();
		return Replace;
	}
	if (box.clickedButton() == otherBtn)
		return ChooseAnother;
	return Cancel;
}

void ScShortcutRegistry::notifyChanged()
{
	emit changed();
}

// --- desktop / fcitx5 -------------------------------------------------------

QList<ScShortcutRegistry::Owner> ScShortcutRegistry::desktopGrabs() const
{
	if (!m_desktopLoaded)
		loadDesktopGrabs();
	return m_desktopGrabs;
}

void ScShortcutRegistry::reloadDesktopGrabs()
{
	m_desktopLoaded = false;
	m_desktopGrabs.clear();
}

void ScShortcutRegistry::loadDesktopGrabs() const
{
	m_desktopLoaded = true;
	m_desktopGrabs.clear();
	auto add = [this](const QString& qtKey, const QString& kind, const QString& name, const QString& where) {
		if (qtKey.isEmpty())
			return;
		Owner o;
		o.kind = kind; o.name = name; o.location = where;
		o.id = QString("desktop:%1/%2").arg(where, name);
		o.key = QKeySequence(qtKey, QKeySequence::PortableText);
		o.desktop = true;
		m_desktopGrabs << o;
	};

	// fcitx5: ~/.config/fcitx5/config, [Hotkey] section. Only keys with a
	// non-modifier key can collide with a Scribus shortcut.
	{
		QFile f(QDir::homePath() + "/.config/fcitx5/config");
		if (f.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			QTextStream in(&f);
			QString section;
			while (!in.atEnd())
			{
				const QString line = in.readLine().trimmed();
				if (line.startsWith('['))
				{
					section = line;
					continue;
				}
				if (section != "[Hotkey]" && !section.startsWith("[Hotkey/"))
					continue;
				const int eq = line.indexOf('=');
				if (eq <= 0 || line.startsWith('#'))
					continue;
				const QString name = line.left(eq).trimmed();
				const QString value = line.mid(eq + 1).trimmed();
				if (value.isEmpty() || name == "ModifierOnlyKeyTimeout")
					continue;
				// fcitx5 writes list entries as "0=Control+space" under [Hotkey/TriggerKeys]
				const QString hotkeyName = section.startsWith("[Hotkey/") ? section.mid(8).chopped(1) : name;
				add(fcitxKeyToQt(value), tr("fcitx5 hotkey"), hotkeyName, tr("fcitx5 config"));
			}
		}
	}

	// Cinnamon / GNOME: gsettings. Only when a display and the tool exist, and
	// never longer than two seconds in total.
	const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
	if (!qEnvironmentVariableIsEmpty("DISPLAY") && !QStandardPaths::findExecutable("gsettings").isEmpty())
	{
		QStringList schemas;
		if (desktop.contains("cinnamon"))
			schemas << "org.cinnamon.desktop.keybindings.wm" << "org.cinnamon.desktop.keybindings.media-keys" << "org.cinnamon.desktop.keybindings";
		else if (desktop.contains("gnome") || desktop.contains("unity") || desktop.contains("budgie"))
			schemas << "org.gnome.desktop.wm.keybindings" << "org.gnome.settings-daemon.plugins.media-keys" << "org.gnome.shell.keybindings";
		else
			schemas << "org.cinnamon.desktop.keybindings.wm" << "org.gnome.desktop.wm.keybindings";
		static const QRegularExpression lineRe("^(\\S+)\\s+(\\S+)\\s+(.*)$");
		static const QRegularExpression accelRe("'([^']+)'");
		for (const QString& schema : schemas)
		{
			QProcess p;
			p.start("gsettings", { "list-recursively", schema });
			if (!p.waitForFinished(700))
			{
				p.kill();
				continue;
			}
			const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
			for (const QString& line : lines)
			{
				const QRegularExpressionMatch m = lineRe.match(line);
				if (!m.hasMatch())
					continue;
				const QString setting = m.captured(2);
				QRegularExpressionMatchIterator it = accelRe.globalMatch(m.captured(3));
				while (it.hasNext())
				{
					const QString accel = it.next().captured(1);
					if (accel.isEmpty() || accel == "disabled")
						continue;
					add(gtkAccelToQt(accel), tr("Desktop shortcut"), setting, schema.section('.', -1));
				}
			}
		}
	}

	// Xfce: the keyboard-shortcuts channel file, if this user has one.
	{
		QFile f(QDir::homePath() + "/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-keyboard-shortcuts.xml");
		if (f.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			static const QRegularExpression propRe("<property name=\"([^\"]+)\" type=\"string\" value=\"([^\"]*)\"");
			const QString all = QString::fromUtf8(f.readAll());
			QRegularExpressionMatchIterator it = propRe.globalMatch(all);
			while (it.hasNext())
			{
				const QRegularExpressionMatch m = it.next();
				QString accel = m.captured(1);
				accel.replace("&lt;", "<").replace("&gt;", ">");
				add(gtkAccelToQt(accel), tr("Desktop shortcut"), m.captured(2), "xfce4-keyboard-shortcuts");
			}
		}
	}
}

// --- startup dialog ---------------------------------------------------------

// Stored next to "ignoredDuplicatesHash", per user. No key means off, so a
// fresh install - the office PCs after an update - starts with the check off.
bool ScShortcutRegistry::checkOnOpenEnabled()
{
	return QSettings("Faircode", "ScribusShortcuts").value("checkOnOpen", false).toBool();
}

void ScShortcutRegistry::setCheckOnOpenEnabled(bool enabled)
{
	QSettings cfg("Faircode", "ScribusShortcuts");
	if (enabled)
		cfg.setValue("checkOnOpen", true);
	else
		cfg.remove("checkOnOpen");
}

void ScShortcutRegistry::maybeShowDuplicatesDialog(QWidget* parent, const QString& when)
{
	// Before duplicates(): with the preference off no time goes into scanning.
	if (!checkOnOpenEnabled())
		return;
	showDuplicatesDialog(parent, when, false);
}

void ScShortcutRegistry::showDuplicatesDialogNow(QWidget* parent)
{
	showDuplicatesDialog(parent, tr("checked now"), true);
}

void ScShortcutRegistry::showDuplicatesDialog(QWidget* parent, const QString& when, bool onDemand)
{
	if (m_duplicatesDialog)
	{
		if (!onDemand)
			return;
		m_duplicatesDialog->close();   // asked for a fresh list
	}
	const QMap<QString, QList<Owner>> dups = duplicates();
	if (dups.isEmpty())
	{
		if (onDemand)
			QMessageBox::information(parent, tr("Duplicate shortcuts"), tr("No shortcut is assigned more than once."));
		return;
	}
	QStringList lines;
	for (auto it = dups.constBegin(); it != dups.constEnd(); ++it)
		lines << QString("%1\n    %2").arg(native(QKeySequence(it.key(), QKeySequence::PortableText)), columnJoin(it.value(), "\n    "));
	const QString report = lines.join("\n\n");
	const QString hash = QString::fromLatin1(QCryptographicHash::hash(report.toUtf8(), QCryptographicHash::Sha1).toHex());
	QSettings cfg("Faircode", "ScribusShortcuts");
	if (!onDemand && cfg.value("ignoredDuplicatesHash").toString() == hash)
		return;

	auto* dlg = new QDialog(parent);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle(tr("Duplicate shortcuts"));
	dlg->setMinimumSize(560, 360);
	auto* lay = new QVBoxLayout(dlg);
	auto* intro = new QLabel(tr("These keys are assigned more than once (%1). Qt fires none of the claimants of an ambiguous key, "
	                            "and a key the desktop or fcitx5 grabs never reaches Scribus. Change them where they are assigned:").arg(when), dlg);
	intro->setWordWrap(true);
	lay->addWidget(intro);
	auto* text = new QTextEdit(dlg);
	text->setReadOnly(true);
	text->setPlainText(report);
	lay->addWidget(text, 1);
	auto* dontShow = new QCheckBox(tr("Don't show again until something changes"), dlg);
	dontShow->setChecked(cfg.value("ignoredDuplicatesHash").toString() == hash);
	// Only the automatic check can be silenced; on demand it has no meaning.
	dontShow->setVisible(!onDemand);
	lay->addWidget(dontShow);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
	connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
	connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
	lay->addWidget(bb);
	connect(dlg, &QDialog::finished, dlg, [dontShow, hash, onDemand]() {
		if (onDemand)
			return;   // leave the "don't show again" choice as it was
		QSettings c("Faircode", "ScribusShortcuts");
		if (dontShow->isChecked())
			c.setValue("ignoredDuplicatesHash", hash);
		else
			c.remove("ignoredDuplicatesHash");
	});
	m_duplicatesDialog = dlg;
	dlg->show();
}

// --- helpers ----------------------------------------------------------------

void ScShortcutRegistry::insertBelow(QWidget* anchor, QWidget* w)
{
	if (!anchor || !w)
		return;
	QWidget* pw = anchor->parentWidget();
	if (!pw || !pw->layout())
		return;
	// Find the layout (possibly nested) that holds the anchor.
	std::function<bool(QLayout*)> place = [&](QLayout* l) -> bool {
		for (int i = 0; i < l->count(); ++i)
		{
			QLayoutItem* item = l->itemAt(i);
			if (item->widget() == anchor)
			{
				if (auto* box = qobject_cast<QBoxLayout*>(l))
				{
					box->insertWidget(i + 1, w);
					return true;
				}
				if (auto* grid = qobject_cast<QGridLayout*>(l))
				{
					int row, col, rs, cs;
					grid->getItemPosition(i, &row, &col, &rs, &cs);
					// QGridLayout cannot shift rows down; a new bottom row spanning the grid is the nearest "below".
					Q_UNUSED(row); Q_UNUSED(col); Q_UNUSED(rs); Q_UNUSED(cs);
					grid->addWidget(w, grid->rowCount(), 0, 1, grid->columnCount());
					return true;
				}
				l->addWidget(w);
				return true;
			}
			if (item->layout() && place(item->layout()))
				return true;
		}
		return false;
	};
	if (!place(pw->layout()))
		pw->layout()->addWidget(w);
}

ScShortcutConflictLabel::ScShortcutConflictLabel(QWidget* parent)
	: QLabel(parent)
{
	setStyleSheet("color: #c00000; font-size: 9pt;");
	setWordWrap(true);
	hide();
}

void ScShortcutConflictLabel::setKey(const QKeySequence& key, const QString& excludeId)
{
	m_key = key;
	m_owners = key.isEmpty() ? QList<ScShortcutRegistry::Owner>() : ScShortcutRegistry::instance().ownersOf(key, excludeId);
	if (m_owners.isEmpty())
	{
		clear();
		hide();
		return;
	}
	setText(ScShortcutRegistry::conflictText(key, m_owners));
	show();
}
