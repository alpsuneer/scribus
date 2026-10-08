/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UPDATECHECKDIALOG_H
#define UPDATECHECKDIALOG_H

#include "scribusapi.h"
#include "scupdateclient.h"

#include <QDialog>
#include <QPointer>
#include <QString>

class QLabel;
class QPushButton;
class QTextBrowser;

/**
 * Help > Check for Updates...
 *
 * Shows the installed version, asks the saved update server for latest.json
 * and shows the latest version and its changelog, with Update / Later.
 * "Update" hands over to UpdateAvailableDialog, which downloads, verifies and
 * installs (pkexec dpkg -i). "Settings..." opens UpdateSettingsDialog for the
 * server URL and API key; the check runs again when that closes.
 *
 * runStartupCheck() is the automatic check: at most once per calendar day,
 * fully asynchronous, silent unless a newer signed version exists. An
 * unreachable server, a wrong key or a bad manifest never shows anything and
 * never delays startup.
 */
class SCRIBUS_API UpdateCheckDialog : public QDialog
{
	Q_OBJECT

public:
	//! checkNow=false: the caller already has a result and will call showUpdate().
	explicit UpdateCheckDialog(QWidget* parent = nullptr, bool checkNow = true);

	//! Present an update that a check has already found and verified.
	void showUpdate(const ScUpdateInfo& info, const QString& apiKey);

	//! Once-a-day background check. Returns immediately.
	static void runStartupCheck(QWidget* parent);

	//! The update server URL saved by UpdateSettingsDialog, or empty.
	static QString savedServerUrl();

private slots:
	void startCheck();
	void onUpdateAvailable(const ScUpdateInfo& info);
	void onUpToDate();
	void onAuthError();
	void onNotFoundError();
	void onNetworkError(const QString& message);
	void updateClicked();
	void settingsClicked();
	void tryDefaultClicked();

private:
	void setResult(const QString& latest, const QString& status, const QString& changelog, bool canUpdate);

	ScUpdateClient* m_client { nullptr };
	ScUpdateInfo m_info;
	QString m_apiKey;

	QLabel* m_currentLabel { nullptr };
	QLabel* m_latestLabel { nullptr };
	QLabel* m_statusLabel { nullptr };
	QTextBrowser* m_changelog { nullptr };
	QPushButton* m_updateButton { nullptr };
	QPushButton* m_laterButton { nullptr };
	QPushButton* m_settingsButton { nullptr };
	QPushButton* m_tryDefaultButton { nullptr };
	//! The one Update Settings dialog, parented and modal to this window.
	QPointer<class UpdateSettingsDialog> m_settingsDialog;
	QString m_source;       //!< "settings" or "conf" for the check in progress
	QString m_checkedUrl;
};

#endif
