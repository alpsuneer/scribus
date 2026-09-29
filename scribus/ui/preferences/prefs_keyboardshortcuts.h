/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#ifndef PREFS_KEYBOARDSHORTCUTS_H
#define PREFS_KEYBOARDSHORTCUTS_H

#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QKeyEvent>
#include <QEvent>

#include "ui_prefs_keyboardshortcutsbase.h"
#include "prefs_pane.h"
#include "scribusapi.h"
#include "scribusstructs.h"


class SCRIBUS_API Prefs_KeyboardShortcuts : public Prefs_Pane, Ui::Prefs_KeyboardShortcuts
{
	Q_OBJECT

	public:
		Prefs_KeyboardShortcuts(QWidget* parent, ScribusDoc* doc = nullptr);
		~Prefs_KeyboardShortcuts();

		void restoreDefaults(struct ApplicationPrefs *prefsData) override;
		void saveGuiToPrefs(struct ApplicationPrefs *prefsData) const override;

		bool event( QEvent* ev ) override;
		void keyPressEvent(QKeyEvent *k) override;
		void keyReleaseEvent(QKeyEvent *k) override;

		static QString getKeyText(const QKeySequence& KeyC);
		static QString getTrKeyText(const QKeySequence& KeyC);

		// Suneer: named shortcut sets. Built-in sets are computed; user sets are
		// keyset XML files in userSetsDir(). The default set's name is kept in
		// prefs172.xml ("keyboard_shortcuts" / "default_set").
		static QString scribusDefaultSetName();
		static QString newspaperDefaultSetName();
		static QString userSetsDir();
		static QString defaultSetName();
		static void setDefaultSetName(const QString& name);
		// Moves a pre-shortcut-sets my-default-shortcuts.xml into userSetsDir() once.
		static void migrateOldMyDefault();
		// Writes keys in the keyset XML format, without any dialog.
		static bool writeKeySet(const QMap<QString, Keys>& keys, const QString& fileName, const QString& setName);

	public slots:
		void languageChange();

protected:
	QMap<QString,Keys> keyMap;
	QMap<QString,Keys>::Iterator currentKeyMapRow;
	QMap<QString, QString> keySetList;
	QMap<QTreeWidgetItem*, QString> lviToActionMap;
	QList<QTreeWidgetItem*> lviToMenuMap;
	QVector< QPair<QString, QStringList> >* defMenus;
	QVector< QPair<QString, QStringList> >* defNonMenuActions;
	QTreeWidgetItem * selectedLVI { nullptr };
	QComboBox* shortcutSetCombo { nullptr };
	QPushButton* saveSetButton { nullptr };
	QPushButton* deleteSetButton { nullptr };
	QMap<QString, QString> userSetFiles; // set name -> file

	void refreshShortcutSets(const QString& selectName);
	QString selectedSetName() const;
	bool isBuiltinSet(const QString& name) const;
	void loadNamedSet(const QString& name);
	void overlayKeySetFile(const QString& fileName);
	void updateSetButtons();
	int keyCode { 0 };

	void insertActions();
	void importKeySet(const QString&);
	bool exportKeySet(const QString&);
	QStringList scanForSets();
	// excludeAction lets the action currently being edited be skipped, so re-pressing an
	// action's own shortcut is not reported as a conflict with itself.
	bool checkKey(int code, const QString& excludeAction = QString());
	QString getAction(int code, const QString& excludeAction = QString());
	// Owner label if the sequence belongs to a non-ScrAction shortcut (paragraph-styles
	// panel), otherwise an empty string. Those cannot be reassigned from this page.
	QString dynamicShortcutOwner(int code) const;
	// Clear keySeq from every action except excludeAction, updating their list rows too.
	void clearConflictingShortcuts(const QKeySequence& keySeq, const QString& excludeAction);

protected slots:
	void setKeyText();
	void dispKey(QTreeWidgetItem* current, QTreeWidgetItem* previous=0);
	void setNoKey();
	void loadKeySetFile();
	void importKeySetFile();
	void exportKeySetFile();
	void resetKeySet();
	void shortcutSetSelected(int index);
	void saveShortcutSetAs();
	void saveShortcutSet();
	void makeShortcutSetDefault();
	void deleteShortcutSet();
	void clearSearchString();
	void applySearch( const QString & newss );
};

#endif // PREFS_KEYBOARDSHORTCUTS_H
