/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UPDATESETTINGSDIALOG_H
#define UPDATESETTINGSDIALOG_H

#include "scribusapi.h"
#include "scupdateclient.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

/**
 * Entry point for Help > Check for Updates. The update server URL and API
 * key always come from here (or from what was previously saved through
 * here) — never hardcoded.
 *
 * The key is round-tripped through ScUpdateKeyStore, which prefers the OS
 * keychain and falls back to obfuscated prefs storage; this dialog never
 * writes it to QSettings itself.
 */
class SCRIBUS_API UpdateSettingsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit UpdateSettingsDialog(QWidget* parent = nullptr);

private slots:
	void checkClicked();
	void useDefaultClicked();
	void clearKeyClicked();
	void toggleKeyVisibility();
	void saveToggled(bool checked);

	void onUpdateAvailable(const ScUpdateInfo& info);
	void onUpToDate();
	void onAuthError();
	void onNotFoundError();
	void onNetworkError(const QString& message);

private:
	void setBusy(bool busy);
	bool persistSettings();
	//! The URL from the edit, validated; empty (with the inline error shown) when unusable.
	QString validatedUrl(bool allowEmpty);
	QString m_defaultUrl;
	QString currentApiKey() const;

	ScUpdateClient* m_client;
	QLineEdit* m_urlEdit { nullptr };
	QLineEdit* m_keyEdit { nullptr };
	QToolButton* m_showKeyButton { nullptr };
	QCheckBox* m_saveCheck { nullptr };
	QPushButton* m_checkButton { nullptr };
	QPushButton* m_clearKeyButton { nullptr };
	QPushButton* m_useDefaultButton { nullptr };
	QLabel* m_urlErrorLabel { nullptr };
	QLabel* m_statusLabel { nullptr };
};

#endif
