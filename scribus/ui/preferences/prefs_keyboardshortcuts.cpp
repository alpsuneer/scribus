/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QDebug>
#include <QDomDocument>
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

	// Suneer: the user's own default set, next to the Scribus-defaults Reset.
	resetSetButton->setText(tr("&Reset to Scribus Defaults"));
	auto* saveMyDefaultButton = new QPushButton(tr("Save as Default"), this);
	saveMyDefaultButton->setToolTip(tr("Save the shortcuts shown here as your own default set"));
	resetMyDefaultButton = new QPushButton(tr("Reset to My Default"), this);
	resetMyDefaultButton->setToolTip(tr("Restore the set saved with \"Save as Default\""));
	resetMyDefaultButton->setEnabled(QFile::exists(myDefaultShortcutsPath()));
	const int resetIndex = horizontalLayout_2->indexOf(resetSetButton);
	horizontalLayout_2->insertWidget(resetIndex, resetMyDefaultButton);
	horizontalLayout_2->insertWidget(resetIndex, saveMyDefaultButton);
	connect(saveMyDefaultButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::saveAsMyDefault);
	connect(resetMyDefaultButton, &QPushButton::clicked, this, &Prefs_KeyboardShortcuts::resetToMyDefault);

}

Prefs_KeyboardShortcuts::~Prefs_KeyboardShortcuts() = default;

void Prefs_KeyboardShortcuts::languageChange()
{
	// No need to do anything here, the UI language cannot change while prefs dialog is opened
}

void Prefs_KeyboardShortcuts::restoreDefaults(struct ApplicationPrefs *prefsData)
{
	keyMap = prefsData->keyShortcutPrefs.KeyActions;
	loadableSets->clear();
	loadableSets->addItems(scanForSets());
	insertActions();
	dispKey(nullptr);
}

void Prefs_KeyboardShortcuts::saveGuiToPrefs(struct ApplicationPrefs *prefsData) const
{
	prefsData->keyShortcutPrefs.KeyActions = keyMap;
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

void Prefs_KeyboardShortcuts::loadKeySetFile()
{
	if (keySetList.contains(loadableSets->currentText()))
		importKeySet(keySetList[loadableSets->currentText()]);
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

QString Prefs_KeyboardShortcuts::myDefaultShortcutsPath()
{
	return PrefsManager::instance().preferencesLocation() + "my-default-shortcuts.xml";
}

void Prefs_KeyboardShortcuts::saveAsMyDefault()
{
	const QString path = myDefaultShortcutsPath();
	if (!writeKeySet(keyMap, path, tr("My Default")))
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not save your default shortcuts to %1").arg(path));
		return;
	}
	resetMyDefaultButton->setEnabled(true);
	ScMessageBox::information(this, tr("Save as Default"), tr("Your default shortcuts were saved to %1").arg(path));
}

void Prefs_KeyboardShortcuts::resetToMyDefault()
{
	const QString path = myDefaultShortcutsPath();
	if (!QFile::exists(path))
	{
		resetMyDefaultButton->setEnabled(false);
		return;
	}
	importKeySet(path);
}

void Prefs_KeyboardShortcuts::resetKeySet()
{
	QString location = ScPaths::instance().shareDir();
	QString defaultKeySetFileName = QDir::toNativeSeparators(location + "keysets/scribus15.xml");
	importKeySet(defaultKeySetFileName);
}

QStringList Prefs_KeyboardShortcuts::scanForSets()
{
	keySetList.clear();
	QString location(ScPaths::instance().shareDir() + "keysets/");
	QDir keySetsDir(QDir::toNativeSeparators(location), "*.xml", QDir::Name, QDir::Files | QDir::NoSymLinks);
	if ((!keySetsDir.exists()) || (keySetsDir.count() <= 0))
		return QStringList();

	QStringList appNames;
	for (uint fileCounter = 0; fileCounter < keySetsDir.count(); ++fileCounter)
	{
		QString filename(QDir::toNativeSeparators(location + keySetsDir[fileCounter]));
		QDomDocument doc("keymapentries");
		QFile file(filename);
		if (!file.open( QIODevice::ReadOnly))
			continue;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		QDomDocument::ParseResult parseResult = doc.setContent(&file);
		if (!parseResult)
		{
			qDebug("%s", QString("Could not open key set file: %1\nError:%2 at line: %3, row: %4").arg(keySetsDir[fileCounter], parseResult.errorMessage).arg(parseResult.errorLine).arg(parseResult.errorColumn).toLatin1().constData());
			file.close();
			continue;
		}
#else
		QString errorMsg;
		int eline;
		int ecol;
		if (!doc.setContent( &file, &errorMsg, &eline, &ecol ))
		{
			qDebug("%s", QString("Could not open key set file: %1\nError:%2 at line: %3, row: %4").arg(keySetsDir[fileCounter], errorMsg).arg(eline).arg(ecol).toLatin1().constData());
			file.close();
			continue;
		}
#endif
		file.close();

		QDomElement docElem = doc.documentElement();
		if (docElem.tagName() == "shortcutset" && docElem.hasAttribute("name"))
		{
			QDomAttr nameAttr = docElem.attributeNode("name");
			if(nameAttr.value().contains(ScribusAPI::getVersionScribus().remove(".svn")))
				appNames.prepend(nameAttr.value());
			else
				appNames.append(nameAttr.value());
			keySetList.insert(nameAttr.value(), filename);
		}
	}
	return QStringList(appNames);
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

