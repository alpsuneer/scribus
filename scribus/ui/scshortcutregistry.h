/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCSHORTCUTREGISTRY_H
#define SCSHORTCUTREGISTRY_H

#include <functional>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

class QShortcut;
class QWidget;

/*!
 * \brief One place that knows every key assignment Scribus has, so every
 * "assign a shortcut" field can ask the same question and get the same answer.
 *
 * Sources: the menu actions of the main window (Keyboard Shortcuts), the
 * paragraph and character styles of the open document, the Next Style Chain
 * keys, the Column Style configs, the Design Styles, every QShortcut our own
 * panels create (they register here), and - read-only - keys the desktop
 * (Cinnamon / GNOME / Xfce) or fcitx5 grab before Scribus ever sees them.
 *
 * Code that owns keys outside this file registers a provider; a provider
 * returns its current owners each time it is asked, so nothing here goes stale.
 */
class ScShortcutRegistry : public QObject
{
	Q_OBJECT
public:
	struct Owner
	{
		QString kind;      //!< "Menu action", "Paragraph style", "Next Style Chain", ...
		QString name;      //!< "Item → Adjust Frame to Image", "02 BodyText", ...
		QString location;  //!< "Keyboard Shortcuts", "this document", "Paragraph Styles panel", ...
		QString id;        //!< unique: "action:itemAdjustFrameToImage", "pstyle:02 BodyText"
		QKeySequence key;
		bool desktop = false;              //!< grabbed outside Scribus: cannot be cleared from here
		std::function<void()> clear;       //!< removes the assignment at its source; may be empty
		//! "Menu action: Item → Adjust Frame to Image (Keyboard Shortcuts)"
		QString label() const;
	};
	using Provider = std::function<QList<Owner>()>;

	enum Resolution { Replace, ChooseAnother, Cancel };

	static ScShortcutRegistry& instance();

	//! Add or replace the provider registered under \a providerId.
	void addProvider(const QString& providerId, Provider provider);
	//! Our own QShortcuts register here so the check always knows about them.
	//! \a ownerId: the id of the Owner this shortcut realises (so it is not
	//! listed twice); empty for a shortcut that has no other representation.
	void registerShortcut(QShortcut* shortcut, const QString& panelName, const QString& what, const QString& ownerId = QString());

	//! Everything that currently uses \a key, except the owner with \a excludeId.
	QList<Owner> ownersOf(const QKeySequence& key, const QString& excludeId = QString()) const;
	QList<Owner> allOwners() const;
	//! key (portable text) -> its owners, for every key claimed more than once.
	QMap<QString, QList<Owner>> duplicates() const;

	//! "Ctrl+Alt+I is already used by: Menu action: Item → Adjust Frame to Image (Keyboard Shortcuts)"
	static QString conflictText(const QKeySequence& key, const QList<Owner>& owners, bool multiLine = false);

	//! The one question every assignment field asks. On Replace the owners'
	//! clear() functions have run and changed() has been emitted before this
	//! returns; the caller then stores the new assignment.
	Resolution askOnConflict(QWidget* parent, const QKeySequence& key, const QList<Owner>& owners);

	//! Tell every panel that an assignment changed somewhere, so the old place
	//! shows it as empty and QShortcuts are rebuilt.
	void notifyChanged();

	//! Startup / document-open check: list duplicates once in a small dialog,
	//! with "Don't show again until something changes". Does nothing at all,
	//! not even the scan, unless checkOnOpenEnabled().
	void maybeShowDuplicatesDialog(QWidget* parent, const QString& when);
	//! Extras > Check Duplicate Shortcuts...: the same list, once, on demand.
	//! Runs whatever the preference says, ignores "Don't show again", and
	//! says so when there is nothing to list.
	void showDuplicatesDialogNow(QWidget* parent);

	//! Preferences > SR Menu > "Check for duplicate shortcuts when opening
	//! documents and at startup". Off unless the user ticked it.
	static bool checkOnOpenEnabled();
	static void setCheckOnOpenEnabled(bool enabled);

	//! Keys grabbed by the desktop or fcitx5 (read once, on first use).
	QList<Owner> desktopGrabs() const;
	void reloadDesktopGrabs();

	//! Insert \a w directly below \a anchor in whatever layout holds it.
	static void insertBelow(QWidget* anchor, QWidget* w);

signals:
	void changed();

private:
	ScShortcutRegistry();
	QList<Owner> actionOwners() const;
	QList<Owner> customShortcutOwners(const QSet<QString>& knownIds) const;
	void loadDesktopGrabs() const;

	QMap<QString, Provider> m_providers;
	struct Registered { QPointer<QShortcut> shortcut; QString panel; QString what; QString ownerId; };
	QList<Registered> m_registered;
	mutable bool m_desktopLoaded = false;
	mutable QList<Owner> m_desktopGrabs;
	QPointer<QWidget> m_duplicatesDialog;
	void showDuplicatesDialog(QWidget* parent, const QString& when, bool onDemand);
};

/*!
 * \brief Red one-line label under a shortcut field: "Ctrl+Alt+I is already
 * used by: ...". Empty (and hidden) while the key is free.
 */
class ScShortcutConflictLabel : public QLabel
{
	Q_OBJECT
public:
	explicit ScShortcutConflictLabel(QWidget* parent = nullptr);
	//! Re-check \a key; \a excludeId is the owner being edited.
	void setKey(const QKeySequence& key, const QString& excludeId = QString());
	bool hasConflict() const { return !m_owners.isEmpty(); }
	const QList<ScShortcutRegistry::Owner>& owners() const { return m_owners; }
	QKeySequence key() const { return m_key; }
private:
	QKeySequence m_key;
	QList<ScShortcutRegistry::Owner> m_owners;
};

#endif
