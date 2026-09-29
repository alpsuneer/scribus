/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QDebug>
#include <QDomDocument>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDir>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>

#include "actionmanager.h"
#include "api/api_application.h"
#include "commonstrings.h"
#include "iconmanager.h"
#include "pluginmanager.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "prefsstructs.h"
#include "scpaths.h"
#include "scplugin.h"
#include "scraction.h"
#include "scribuscore.h"
#include "scribus.h"
#include "ui/ParagraphStylesPanel.h"
#include "ui/preferences/prefs_keyboardshortcuts.h"
#include "ui/scmessagebox.h"
#include "util.h"


Prefs_KeyboardShortcuts::Prefs_KeyboardShortcuts(QWidget* parent, ScribusDoc* /*doc*/)
	: Prefs_Pane(parent)
{
	setupUi(this);
	languageChange();

	m_caption = tr("Keyboard Shortcuts");
	m_icon = "pref-keyboard-shortcuts";

	defMenus = ActionManager::defaultMenus();
	defNonMenuActions = ActionManager::defaultNonMenuActions();

	auto itnmenua = defNonMenuActions->begin();
	PluginManager& pluginManager(PluginManager::instance());
	QStringList pluginNames(pluginManager.pluginNames(false));
	ScPlugin* plugin = nullptr;
	ScActionPlugin* ixplug = nullptr;
	QString pName;
	for (int i = 0; i < pluginNames.count(); ++i)
	{
		pName = pluginNames.at(i);
		plugin = pluginManager.getPlugin(pName, true);
		Q_ASSERT(plugin); // all the returned names should represent loaded plugins
		if (plugin->inherits("ScActionPlugin"))
		{
			ixplug = qobject_cast<ScActionPlugin*>(plugin);
			Q_ASSERT(ixplug);
			ScActionPlugin::ActionInfo ai(ixplug->actionInfo());
			itnmenua->second << ai.name;
		}
	}

	Q_CHECK_PTR(defMenus);
	lviToActionMap.clear();
	lviToMenuMap.clear();
	keyTable->clear();
	keyMap.clear();
	keyCode = 0;
	keyDisplay->setMinimumWidth(fontMetrics().horizontalAdvance("CTRL+ALT+SHIFT+W"));
	keyDisplay->setText("");
	selectedLVI = nullptr;

	clearSearchButton->setIcon(IconManager::instance().loadIcon("clear-right"));
	// signals and slots connections
	connect( keyTable, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(dispKey(QTreeWidgetItem*,QTreeWidgetItem*)));
	connect( noKey, SIGNAL(clicked()), this, SLOT(setNoKey()));
	connect( setKeyButton, SIGNAL(clicked()), this, SLOT(setKeyText()));
	connect( loadSetButton, SIGNAL(clicked()), this, SLOT(loadKeySetFile()));
	connect( importSetButton, SIGNAL(clicked()), this, SLOT(importKeySetFile()));
	connect( exportSetButton, SIGNAL(clicked()), this, SLOT(exportKeySetFile()));
	connect( resetSetButton, SIGNAL(clicked()), this, SLOT(resetKeySet()));
	connect( clearSearchButton, SIGNAL(clicked()), this, SLOT(clearSearchString()));
	connect( searchTextLineEdit, SIGNAL(textChanged(QString)), this, SLOT(applySearch(QString)));

	resetSetButton->setText(tr("&Reset to Scribus Defaults"));

	// Suneer: one list for every set. Built-in, stock and the user's own sets
	// all live in "Loadable Shortcut Sets"; the set buttons sit beside Load.
	loadableSets->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	loadableSets->setToolTip(tr("Choosing a set loads it into the list above"));
	auto* saveAsButton = new QPushButton(tr("Save As..."), this);
	saveSetButton = new QPushButton(tr("Save"), this);
	auto* defaultButton = new QPushButton(tr("Set as Default"), this);
	deleteSetButton = new QPushButton(tr("Delete"), this);
	saveAsButton->setToolTip(tr("Save the shortcuts above as a new named set"));
	saveSetButton->setToolTip(tr("Update the selected set with the shortcuts above"));
	defaultButton->setToolTip(tr("Make the selected set the one Scribus starts with"));
	deleteSetButton->setToolTip(tr("Delete the selected set"));
	int at = horizontalLayout_2->indexOf(loadSetButton) + 1;
	for (QPushButton* button : { saveAsButton, saveSetButton, defaultButton, deleteSetButton })
		horizontalLayout_2->insertWidget(at++, button);
	connect(loadableSets, &QComboBox::activated, this, &Prefs_KeyboardShortcuts::shortcutSetSelected);
	connect(saveAsButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::saveShortcutSetAs);
	connect(saveSetButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::saveShortcutSet);
	connect(defaultButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::makeShortcutSetDefault);
	connect(deleteSetButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::deleteShortcutSet);

}

Prefs_KeyboardShortcuts::~Prefs_KeyboardShortcuts() = default;

void Prefs_KeyboardShortcuts::languageChange()
{
	// No need to do anything here, the UI language cannot change while prefs dialog is opened
}

void Prefs_KeyboardShortcuts::restoreDefaults(struct ApplicationPrefs *prefsData)
{
	keyMap = prefsData->keyShortcutPrefs.KeyActions;
	insertActions();
	dispKey(nullptr);
	refreshShortcutSets(defaultSetName());
}

void Prefs_KeyboardShortcuts::saveGuiToPrefs(struct ApplicationPrefs *prefsData) const
{
	prefsData->keyShortcutPrefs.KeyActions = keyMap;
	// Startup applies the Default set, so an edit made while one of the
	// user's own sets is the Default is written into that set, or it would
	// be gone at the next start. Built-in sets are never written.
	const QString defName = defaultSetName();
	if (userSetFiles.contains(defName))
		writeKeySet(keyMap, userSetFiles.value(defName), defName);
}

void Prefs_KeyboardShortcuts::setNoKey()
{
	if (noKey->isChecked())
	{
		if (selectedLVI != nullptr)
		{
			selectedLVI->setText(1, "");
			keyMap[lviToActionMap[selectedLVI]].keySequence = QKeySequence();
		}
		keyDisplay->setText("");
		noKey->setChecked(true);
	}
}


void Prefs_KeyboardShortcuts::importKeySetFile()
{
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString currentPath = dirs->get("keymapprefs_import", ScPaths::instance().shareDir() + "keysets/");
	QString s = QFileDialog::getOpenFileName(this, tr("Select a Key set file to read"), currentPath, tr("Key Set XML Files (*.xml)"));
	if (!s.isEmpty())
	{
		dirs->set("keymapprefs_import", QFileInfo(s).absolutePath());
		importKeySet(s);
	}
}
void Prefs_KeyboardShortcuts::exportKeySetFile()
{
	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString currentPath= dirs->get("keymapprefs_export", ".");
	QString s = QFileDialog::getSaveFileName(this, tr("Select a Key set file to save to"), currentPath, tr("Key Set XML Files (*.xml)") );
	if (!s.isEmpty())
	{
		dirs->set("keymapprefs_export", QFileInfo(s).absolutePath());
		exportKeySet(s);
	}
}

void Prefs_KeyboardShortcuts::importKeySet(const QString& filename)
{
	searchTextLineEdit->clear();

	QFileInfo fi(filename);
	if (!fi.exists())
		return;

	//import the file into qdomdoc
	QDomDocument doc( "keymapentries" );
	QFile file1( filename );
	if ( !file1.open( QIODevice::ReadOnly ) )
		return;

	QTextStream ts(&file1);
	ts.setEncoding(QStringConverter::Utf8);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	QDomDocument::ParseResult parseResult = doc.setContent(ts.readAll());
	if (!parseResult)
	{
		qDebug("%s", QString("Could not open key set file: %1\nError:%2 at line: %3, row: %4").arg(filename, parseResult.errorMessage).arg(parseResult.errorLine).arg(parseResult.errorColumn).toLatin1().constData());
		file1.close();
		return;
	}
#else
	QString errorMsg;
	int eline;
	int ecol;
	if ( !doc.setContent( ts.readAll(), &errorMsg, &eline, &ecol ))
	{
		qDebug("%s", QString("Could not open key set file: %1\nError:%2 at line: %3, row: %4").arg(filename, errorMsg).arg(eline).arg(ecol).toLatin1().constData());
		file1.close();
		return;
	}
#endif
	file1.close();

	//load the file now
	QDomElement docElem = doc.documentElement();
	if (docElem.tagName() == "shortcutset" && docElem.hasAttribute("name"))
	{
		QDomAttr keysetAttr = docElem.attributeNode( "name" );

		//clear current menu entries
		for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
			it.value().keySequence = QKeySequence();

		//load in new set
		for (QDomNode n = docElem.firstChild(); !n.isNull(); n = n.nextSibling())
		{
			QDomElement e = n.toElement();
			if (e.isNull())
				continue;
			if (e.hasAttribute("name") && e.hasAttribute("shortcut"))
			{
				QDomAttr nameAttr = e.attributeNode("name");
				QDomAttr shortcutAttr = e.attributeNode("shortcut");
				if (keyMap.contains(nameAttr.value()))
					keyMap[nameAttr.value()].keySequence = QKeySequence(shortcutAttr.value());
			}
		}
	}

	insertActions();
}

bool Prefs_KeyboardShortcuts::exportKeySet(const QString& filename)
{
	QString exportFileName;
	if (filename.endsWith(".xml"))
		exportFileName = filename;
	else
		exportFileName = filename+".xml";
	if (overwrite(this, exportFileName))
	{
		bool ok;
		QString setName = QInputDialog::getText(this, tr("Export Keyboard Shortcuts to File"), tr("Enter the name of the shortcut set:"), QLineEdit::Normal, QString(), &ok);
		if (!( ok && !setName.isEmpty()) )
			return false;
		if (!writeKeySet(keyMap, exportFileName, setName))
		{
			ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not write the shortcut set to %1").arg(exportFileName));
			return false;
		}
	}
	return true;
}

bool Prefs_KeyboardShortcuts::writeKeySet(const QMap<QString, Keys>& keys, const QString& fileName, const QString& setName)
{
	QDomDocument doc( "keymapentries" );
	QDomElement keySetElement = doc.createElement("shortcutset");
	keySetElement.setAttribute("name", setName);
	doc.appendChild(keySetElement);
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		if (it.key().isEmpty())
			continue;
		QDomElement function_shortcut = doc.createElement("function");
		function_shortcut.setAttribute("name", it.key());
		function_shortcut.setAttribute("shortcut", getKeyText(it.value().keySequence));
		keySetElement.appendChild(function_shortcut);
	}
	QFile f(fileName);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	QByteArray data("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	data += doc.toByteArray(4);
	bool ok = (f.write(data) == data.size());
	f.close();
	return ok;
}

QString Prefs_KeyboardShortcuts::scribusDefaultSetName()
{
	return QStringLiteral("Scribus Default");
}

QString Prefs_KeyboardShortcuts::newspaperDefaultSetName()
{
	return QStringLiteral("Newspaper Default");
}

QString Prefs_KeyboardShortcuts::userSetsDir()
{
	return PrefsManager::instance().preferencesLocation() + "shortcut-sets/";
}

static PrefsContext* shortcutSetPrefs()
{
	return PrefsManager::instance().prefsFile->getContext("keyboard_shortcuts");
}

QString Prefs_KeyboardShortcuts::defaultSetName()
{
	return shortcutSetPrefs()->get("default_set", newspaperDefaultSetName());
}

void Prefs_KeyboardShortcuts::setDefaultSetName(const QString& name)
{
	shortcutSetPrefs()->set("default_set", name);
	PrefsManager::instance().prefsFile->write();
}

void Prefs_KeyboardShortcuts::migrateOldMyDefault()
{
	const QString oldFile = PrefsManager::instance().preferencesLocation() + "my-default-shortcuts.xml";
	if (!QFile::exists(oldFile))
		return;
	const QString newFile = userSetsDir() + "My Default.xml";
	if (!QDir().mkpath(userSetsDir()) || QFile::exists(newFile) || !QFile::rename(oldFile, newFile))
		return;
	if (!shortcutSetPrefs()->contains("default_set"))
		setDefaultSetName(QStringLiteral("My Default"));
}

// name -> file for every keyset XML in dir, read from its <shortcutset name="">.
static QMap<QString, QString> scanSetDir(const QString& dirPath, const QStringList& skipFiles = QStringList())
{
	QMap<QString, QString> sets;
	QDir dir(dirPath, "*.xml", QDir::Name, QDir::Files);
	const QStringList files = dir.entryList();
	for (const QString& file : files)
	{
		if (skipFiles.contains(file))
			continue;
		QFile f(dir.filePath(file));
		if (!f.open(QIODevice::ReadOnly))
			continue;
		QDomDocument doc("keymapentries");
		if (!doc.setContent(&f))
			continue;
		QDomElement root = doc.documentElement();
		const QString name = root.attribute("name").trimmed();
		if (root.tagName() == "shortcutset" && !name.isEmpty() && !sets.contains(name))
			sets.insert(name, dir.filePath(file));
	}
	return sets;
}

// Scribus's own keysets folder. malayalam-dtp.xml is left out because
// "Newspaper Default" already is that set, laid over this build's defaults.
static QMap<QString, QString> stockSets()
{
	return scanSetDir(ScPaths::instance().shareDir() + "keysets/", { QStringLiteral("malayalam-dtp.xml") });
}

static QMap<QString, QString> userSets()
{
	QMap<QString, QString> sets = scanSetDir(Prefs_KeyboardShortcuts::userSetsDir());
	sets.remove(Prefs_KeyboardShortcuts::scribusDefaultSetName());
	sets.remove(Prefs_KeyboardShortcuts::newspaperDefaultSetName());
	const QMap<QString, QString> stock = stockSets();
	for (auto it = stock.cbegin(); it != stock.cend(); ++it)
		sets.remove(it.key());
	return sets;
}

// Lays a keyset file over keys; clearFirst gives the file the whole map, as
// importKeySet() does for a complete set.
static void readKeySetInto(const QString& fileName, QMap<QString, Keys>& keys, bool clearFirst)
{
	QFile f(fileName);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QDomDocument doc("keymapentries");
	if (!doc.setContent(&f))
		return;
	if (clearFirst)
	{
		for (auto it = keys.begin(); it != keys.end(); ++it)
			it.value().keySequence = QKeySequence();
	}
	for (QDomNode n = doc.documentElement().firstChild(); !n.isNull(); n = n.nextSibling())
	{
		QDomElement e = n.toElement();
		if (e.hasAttribute("name") && e.hasAttribute("shortcut") && keys.contains(e.attribute("name")))
			keys[e.attribute("name")].keySequence = QKeySequence(e.attribute("shortcut"));
	}
}

bool Prefs_KeyboardShortcuts::buildSetKeys(const QString& name, QMap<QString, Keys>& keys)
{
	if (name == scribusDefaultSetName() || name == newspaperDefaultSetName())
	{
		// Built from the shortcuts compiled into this build, so the custom
		// actions keep theirs; Newspaper adds its keyset on top.
		const QMap<QString, QKeySequence>* defaults = ActionManager::defaultShortcuts();
		for (auto it = keys.begin(); it != keys.end(); ++it)
			it.value().keySequence = defaults->value(it.key());
		if (name == newspaperDefaultSetName())
			readKeySetInto(ScPaths::instance().shareDir() + "keysets/malayalam-dtp.xml", keys, false);
		return true;
	}
	QString file = userSets().value(name);
	if (file.isEmpty())
		file = stockSets().value(name);
	if (file.isEmpty())
		return false;
	readKeySetInto(file, keys, true);
	return true;
}

bool Prefs_KeyboardShortcuts::applyDefaultSet(QMap<QString, Keys>& keys)
{
	// Only a Default someone actually chose: a profile that never picked one
	// keeps the shortcuts it had when Scribus was closed.
	PrefsContext* ctx = shortcutSetPrefs();
	if (!ctx->contains("default_set"))
		return false;
	return buildSetKeys(ctx->get("default_set"), keys);
}

bool Prefs_KeyboardShortcuts::isBuiltinSet(const QString& name) const
{
	return !userSetFiles.contains(name);
}

QString Prefs_KeyboardShortcuts::selectedSetName() const
{
	return loadableSets->currentData().toString();
}

void Prefs_KeyboardShortcuts::refreshShortcutSets(const QString& selectName)
{
	userSetFiles = userSets();
	const QString defName = defaultSetName();
	QStringList builtins { scribusDefaultSetName(), newspaperDefaultSetName() };
	builtins += stockSets().keys();

	QSignalBlocker blocker(loadableSets);
	loadableSets->clear();
	for (const QString& name : std::as_const(builtins))
		loadableSets->addItem((name == defName) ? tr("%1 (Default)").arg(name) : name, name);
	if (!userSetFiles.isEmpty())
		loadableSets->insertSeparator(loadableSets->count());
	for (auto it = userSetFiles.cbegin(); it != userSetFiles.cend(); ++it)
	{
		const QString& name = it.key();
		loadableSets->addItem((name == defName) ? tr("%1 [User] (Default)").arg(name) : tr("%1 [User]").arg(name), name);
	}
	int index = loadableSets->findData(selectName);
	loadableSets->setCurrentIndex(index >= 0 ? index : 0);
	updateSetButtons();
}

void Prefs_KeyboardShortcuts::updateSetButtons()
{
	const bool userSet = !isBuiltinSet(selectedSetName());
	saveSetButton->setEnabled(userSet);
	deleteSetButton->setEnabled(userSet);
}

void Prefs_KeyboardShortcuts::loadNamedSet(const QString& name)
{
	if (!buildSetKeys(name, keyMap))
		return;
	m_loadedSet = name;
	insertActions();
	dispKey(nullptr);
}

void Prefs_KeyboardShortcuts::loadKeySetFile()
{
	loadNamedSet(selectedSetName());
}

void Prefs_KeyboardShortcuts::shortcutSetSelected(int index)
{
	Q_UNUSED(index);
	updateSetButtons();
	loadNamedSet(selectedSetName());
}

void Prefs_KeyboardShortcuts::saveShortcutSetAs()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Save Shortcut Set As"), tr("Name of the new shortcut set:"),
		QLineEdit::Normal, QString(), &ok).trimmed();
	if (!ok || name.isEmpty())
		return;
	if (loadableSets->findData(name) >= 0 && isBuiltinSet(name))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("\"%1\" is a built-in set and cannot be replaced. Please choose another name.").arg(name));
		return;
	}
	QString fileName = userSetFiles.value(name);
	if (!fileName.isEmpty())
	{
		if (ScMessageBox::question(this, tr("Save Shortcut Set As"),
				tr("A shortcut set named \"%1\" already exists. Replace it?").arg(name),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;
	}
	else
	{
		QString base = name;
		base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
		fileName = userSetsDir() + base + ".xml";
		for (int i = 2; QFile::exists(fileName); ++i)
			fileName = userSetsDir() + QString("%1-%2.xml").arg(base).arg(i);
	}
	if (!QDir().mkpath(userSetsDir()) || !writeKeySet(keyMap, fileName, name))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not save the shortcut set to %1").arg(fileName));
		return;
	}
	m_loadedSet = name;
	refreshShortcutSets(name);
}

void Prefs_KeyboardShortcuts::saveShortcutSet()
{
	const QString name = selectedSetName();
	if (isBuiltinSet(name))
		return;
	if (!writeKeySet(keyMap, userSetFiles.value(name), name))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not save the shortcut set to %1").arg(userSetFiles.value(name)));
		return;
	}
	m_loadedSet = name;
	ScMessageBox::information(this, tr("Save"), tr("The shortcut set \"%1\" was updated.").arg(name));
}

void Prefs_KeyboardShortcuts::makeShortcutSetDefault()
{
	const QString name = selectedSetName();
	if (m_loadedSet != name)
		loadNamedSet(name);
	setDefaultSetName(name);
	// Applied right away, not only marked: the live menus take the set's keys
	// now, whether the dialog then closes with OK or Cancel.
	PrefsManager::instance().appPrefs.keyShortcutPrefs.KeyActions = keyMap;
	ScCore->primaryMainWindow()->applyShortcutsFromPrefs();
	refreshShortcutSets(name);
}

void Prefs_KeyboardShortcuts::deleteShortcutSet()
{
	const QString name = selectedSetName();
	if (isBuiltinSet(name))
		return;
	if (ScMessageBox::question(this, tr("Delete Shortcut Set"),
			tr("Delete the shortcut set \"%1\"? This cannot be undone.").arg(name),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	if (!QFile::remove(userSetFiles.value(name)))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not delete %1").arg(userSetFiles.value(name)));
		return;
	}
	if (m_loadedSet == name)
		m_loadedSet.clear();
	if (name == defaultSetName())
	{
		// The default is gone: fall back to Newspaper Default and show it.
		setDefaultSetName(newspaperDefaultSetName());
		refreshShortcutSets(newspaperDefaultSetName());
		loadNamedSet(newspaperDefaultSetName());
		return;
	}
	refreshShortcutSets(defaultSetName());
}

void Prefs_KeyboardShortcuts::resetKeySet()
{
	QString location = ScPaths::instance().shareDir();
	QString defaultKeySetFileName = QDir::toNativeSeparators(location + "keysets/scribus15.xml");
	importKeySet(defaultKeySetFileName);
}

QString Prefs_KeyboardShortcuts::getKeyText(const QKeySequence& KeyC)
{
	return KeyC.toString();
}

QString Prefs_KeyboardShortcuts::getTrKeyText(const QKeySequence& KeyC)
{
	return KeyC.toString(QKeySequence::NativeText);
}

void Prefs_KeyboardShortcuts::setKeyText()
{
	if (keyTable->currentItem() == nullptr)
	{
		setKeyButton->setChecked(false);
		return;
	}
	if (setKeyButton->isChecked())
	{
		keyCode = 0;
		grabKeyboard();
	}
	else
		releaseKeyboard();
}

void Prefs_KeyboardShortcuts::insertActions()
{
	bool first = true;
	bool firstMenu = true;
	QTreeWidgetItem* currLVI = nullptr;
	QTreeWidgetItem* currMenuLVI = nullptr;
	QTreeWidgetItem* prevLVI = nullptr;
	QTreeWidgetItem* prevMenuLVI = nullptr;

	lviToActionMap.clear();
	lviToMenuMap.clear();
	keyTable->clear();

	for (int i = 0; i < defMenus->count(); ++i)
	{
		const QPair<QString, QStringList> &actionStrings = defMenus->at(i);
		if (firstMenu)
		{
			currMenuLVI = new QTreeWidgetItem(keyTable);
			firstMenu = false;
		}
		else
			currMenuLVI = new QTreeWidgetItem(keyTable, prevMenuLVI);
		Q_CHECK_PTR(currMenuLVI);
		lviToMenuMap.append(currMenuLVI);
		currMenuLVI->setText(0, actionStrings.first);
		currMenuLVI->setExpanded(true);
		currMenuLVI->setFlags(Qt::ItemIsEnabled);
		prevMenuLVI = currMenuLVI;
		first = true;
		currLVI = nullptr;
		prevLVI = nullptr;
		for (int j = 0; j < actionStrings.second.count(); ++j)
		{
			QString actionName = actionStrings.second.at(j);
			if (!keyMap.contains(actionName))
			{
				qDebug() << "The action " << actionName << " is not defined in shortcut map";
				continue;
			}
			const Keys &actionKeys = keyMap[actionName];
			if (actionKeys.cleanMenuText.isEmpty())
				continue;
			if (first)
			{
				currLVI = new QTreeWidgetItem(currMenuLVI);
				first = false;
			}
			else
				currLVI = new QTreeWidgetItem(currMenuLVI, prevLVI);
			Q_CHECK_PTR(currLVI);
			lviToActionMap.insert(currLVI, actionName);
			currLVI->setText(0, actionKeys.cleanMenuText);
			currLVI->setText(1, actionKeys.keySequence.toString(QKeySequence::NativeText));
			prevLVI = currLVI;
		}
	}
	//Non menu actions
	for (int i = 0; i < defNonMenuActions->count(); ++i)
	{
		const QPair<QString, QStringList> &actionStrings = defNonMenuActions->at(i);
		if (firstMenu)
		{
			currMenuLVI = new QTreeWidgetItem(keyTable);
			firstMenu = false;
		}
		else
			currMenuLVI = new QTreeWidgetItem(keyTable, prevMenuLVI);
		Q_CHECK_PTR(currMenuLVI);
		lviToMenuMap.append(currMenuLVI);
		currMenuLVI->setText(0, actionStrings.first);
		currMenuLVI->setExpanded(true);
		currMenuLVI->setFlags(Qt::ItemIsEnabled);
		prevMenuLVI = currMenuLVI;
		first = true;
		currLVI = nullptr;
		prevLVI = nullptr;
		for (int j = 0; j < actionStrings.second.count(); ++j)
		{
			QString actionName = actionStrings.second.at(j);
			if (!keyMap.contains(actionName))
			{
				qDebug() << "The action " << actionName << " is not defined in shortcut map";
				continue;
			}
			const Keys &actionKeys = keyMap[actionName];
			if (actionKeys.cleanMenuText.isEmpty())
				continue;
			if (first)
			{
				currLVI = new QTreeWidgetItem(currMenuLVI);
				first = false;
			}
			else
				currLVI = new QTreeWidgetItem(currMenuLVI, prevLVI);
			Q_CHECK_PTR(currLVI);
			lviToActionMap.insert(currLVI, actionName);
			currLVI->setText(0, actionKeys.cleanMenuText);
			currLVI->setText(1, actionKeys.keySequence.toString(QKeySequence::NativeText));
			prevLVI = currLVI;
		}
	}
	keyTable->resizeColumnToContents(0);
}

void Prefs_KeyboardShortcuts::applySearch( const QString & newss )
{
	//Must run this as if newss is not empty and we go to the next for loop, the set visible doesn't work
	for (auto it = lviToMenuMap.begin(); it != lviToMenuMap.end(); ++it)
		(*it)->setHidden(false);
	if (newss.isEmpty())
	{
		for (auto it = lviToActionMap.begin(); it != lviToActionMap.end(); ++it)
			it.key()->setHidden(false);
		return;
	}
	//Seem to need to do this.. isOpen doesn't seem to do what it says
	for (auto it = lviToActionMap.begin(); it != lviToActionMap.end(); ++it)
	{
		if (it.key()->text(0).contains(newss, Qt::CaseInsensitive))
			it.key()->setHidden(false);
		else
			it.key()->setHidden(true);
	}
}

void Prefs_KeyboardShortcuts::dispKey(QTreeWidgetItem* qlvi, QTreeWidgetItem*)
{
	if (setKeyButton->isChecked())
	{
		releaseKeyboard();
		setKeyButton->setChecked(false);
	}
	if (qlvi != nullptr && lviToActionMap.contains(qlvi))
	{
		selectedLVI = qlvi;
		QString actionName = lviToActionMap[qlvi];
		if (actionName.isEmpty())
			return;
		keyDisplay->setText(keyMap[actionName].keySequence.toString(QKeySequence::NativeText));
		if (keyMap[actionName].keySequence.isEmpty())
			noKey->setChecked(true);
		else
			userDef->setChecked(true);
	}
	else
	{
		noKey->setChecked(true);
		keyDisplay->setText("");
		selectedLVI = nullptr;
	}
	noKey->setEnabled(selectedLVI != nullptr);
	userDef->setEnabled(selectedLVI != nullptr);
	setKeyButton->setEnabled(selectedLVI != nullptr);
	keyDisplay->setEnabled(selectedLVI != nullptr);
}

bool Prefs_KeyboardShortcuts::event( QEvent* ev )
{
	bool ret = QWidget::event( ev );
	if ( ev->type() == QEvent::KeyPress )
		keyPressEvent((QKeyEvent*)ev);
	if ( ev->type() == QEvent::KeyRelease )
		keyReleaseEvent((QKeyEvent*)ev);
	return ret;
}

void Prefs_KeyboardShortcuts::keyPressEvent(QKeyEvent *k)
{
	if (!setKeyButton->isChecked())
		return;

	switch (k->key())
	{
		case Qt::Key_Meta:
			keyCode |= Qt::META;
			break;
		case Qt::Key_Shift:
			keyCode |= Qt::SHIFT;
			break;
		case Qt::Key_Alt:
			keyCode |= Qt::ALT;
			break;
		case Qt::Key_Control:
			keyCode |= Qt::CTRL;
			break;
		default:
			keyCode |= k->key();
			keyDisplay->setText(getTrKeyText(keyCode));
			releaseKeyboard();
			if (selectedLVI)
			{
				QString actionName = lviToActionMap[selectedLVI];
				const QKeySequence newKeySequence(keyCode);
				const QString oldText = keyMap[actionName].keySequence.toString(QKeySequence::NativeText);
				// Shortcuts owned by the paragraph-styles panel are not in keyMap, so they
				// cannot be reassigned from here — refuse rather than create a duplicate.
				const QString dynOwner = dynamicShortcutOwner(keyCode);
				if (!dynOwner.isEmpty())
				{
					ScMessageBox::information(this, CommonStrings::trWarning,
						tr("The %1 key sequence is already in use by %2.\n"
						   "Change or remove it in the Paragraph Styles panel first.")
						   .arg(getTrKeyText(keyCode), dynOwner));
					selectedLVI->setText(1, oldText);
					keyDisplay->setText(oldText);
				}
				else if (checkKey(keyCode, actionName))
				{
					// Offer to move the shortcut rather than refusing outright. Leaving both
					// bindings in place would make Qt fire neither ("Ambiguous shortcut
					// overload"), so a duplicate is never an acceptable outcome here.
					QMessageBox::StandardButton reply = ScMessageBox::question(this, tr("Shortcut Conflict"),
						tr("The %1 key sequence is already used by \"%2\".\nReassign it to \"%3\"?")
						   .arg(getTrKeyText(keyCode), getAction(keyCode, actionName), keyMap[actionName].cleanMenuText),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
					if (reply == QMessageBox::Yes)
					{
						clearConflictingShortcuts(newKeySequence, actionName);
						selectedLVI->setText(1, newKeySequence.toString(QKeySequence::NativeText));
						keyMap[actionName].keySequence = newKeySequence;
						userDef->setChecked(true);
					}
					else
					{
						selectedLVI->setText(1, oldText);
						keyDisplay->setText(oldText);
					}
				}
				else
				{
					selectedLVI->setText(1, newKeySequence.toString(QKeySequence::NativeText));
					keyMap[actionName].keySequence = newKeySequence;
					userDef->setChecked(true);
				}
			}
			setKeyButton->setChecked(false);
	}

	if (setKeyButton->isChecked())
		keyDisplay->setText(getTrKeyText(keyCode));
}

void Prefs_KeyboardShortcuts::keyReleaseEvent(QKeyEvent *k)
{
	if (!setKeyButton->isChecked())
		return;

	if (k->key() == Qt::Key_Meta)
		keyCode &= ~Qt::META;
	if (k->key() == Qt::Key_Shift)
		keyCode &= ~Qt::SHIFT;
	if (k->key() == Qt::Key_Alt)
		keyCode &= ~Qt::ALT;
	if (k->key() == Qt::Key_Control)
		keyCode &= ~Qt::CTRL;
	keyDisplay->setText(getTrKeyText(keyCode));
}

QString Prefs_KeyboardShortcuts::getAction(int code, const QString& excludeAction)
{
	QKeySequence key(code);
	if (key.isEmpty())
		return QString();
	for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
	{
		if (it.key() == excludeAction || it.value().keySequence.isEmpty())
			continue;
		// ExactMatch only: PartialMatch would flag unrelated prefixes as conflicts.
		if (key.matches(it.value().keySequence) == QKeySequence::ExactMatch)
			return it->cleanMenuText;
	}
	return QString();
}

bool Prefs_KeyboardShortcuts::checkKey(int code, const QString& excludeAction)
{
	QKeySequence key(code);
	if (key.isEmpty())
		return false;
	for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
	{
		if (it.key() == excludeAction || it.value().keySequence.isEmpty())
			continue;
		if (key.matches(it.value().keySequence) == QKeySequence::ExactMatch)
			return true;
	}
	return false;
}

QString Prefs_KeyboardShortcuts::dynamicShortcutOwner(int code) const
{
	QKeySequence key(code);
	if (key.isEmpty())
		return QString();
	const QMap<QString, QKeySequence> dynamic = ParagraphStylesPanel::dynamicShortcuts();
	for (auto it = dynamic.constBegin(); it != dynamic.constEnd(); ++it)
	{
		if (key.matches(it.value()) == QKeySequence::ExactMatch)
			return it.key();
	}
	return QString();
}

void Prefs_KeyboardShortcuts::clearConflictingShortcuts(const QKeySequence& keySeq, const QString& excludeAction)
{
	if (keySeq.isEmpty())
		return;
	for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
	{
		if (it.key() == excludeAction || it.value().keySequence.isEmpty())
			continue;
		if (keySeq.matches(it.value().keySequence) != QKeySequence::ExactMatch)
			continue;
		it.value().keySequence = QKeySequence();
		// Keep the list view in step with the map, otherwise the old owner keeps
		// displaying a shortcut it no longer holds.
		for (auto lvi = lviToActionMap.constBegin(); lvi != lviToActionMap.constEnd(); ++lvi)
		{
			if (lvi.value() == it.key() && lvi.key())
				lvi.key()->setText(1, QString());
		}
	}
}

void Prefs_KeyboardShortcuts::clearSearchString( )
{
	searchTextLineEdit->clear();
}

