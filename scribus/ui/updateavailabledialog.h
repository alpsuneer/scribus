/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef UPDATEAVAILABLEDIALOG_H
#define UPDATEAVAILABLEDIALOG_H

#include "scribusapi.h"
#include "scupdateclient.h"

#include <QDialog>
#include <QProcess>
#include <QString>

class QCloseEvent;
class QPushButton;
class QProgressDialog;
class SuneerDebFetcher;

/**
 * Shown once ScUpdateClient::checkForUpdate() reports a newer version.
 * "Update Now" downloads the installer with the same API key used to check,
 * then hands it to `pkexec dpkg -i` for an authenticated system install.
 *
 * The install's exit code and stderr are always read and shown on failure.
 * ScPrintEngine_PS's system("lpr ...") swallows its exit status and reports
 * success regardless (see CLAUDE.md); this path uses QProcess specifically
 * so that mistake isn't repeated here.
 */
class SCRIBUS_API UpdateAvailableDialog : public QDialog
{
	Q_OBJECT

public:
	UpdateAvailableDialog(const ScUpdateInfo& info, const QString& apiKey, QWidget* parent = nullptr);
	~UpdateAvailableDialog() override;

	//! Begin the download at once, as if "Update Now" had been clicked.
	void startUpdate();

public slots:
	void reject() override;

protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void updateNowClicked();
	void onDownloadProgress(qint64 received, qint64 total);
	void onDownloadFinished(const QString& path);
	void onDownloadFailed(const QString& message);
	void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onInstallError(QProcess::ProcessError error);

private:
	//! Restore point first (SuneerRestorePoint: settings backup; the installed version's package fetched
	//! from the server when it is not cached yet), then installUpdate().
	void prepareRestorePoint(const QString& path);
	void installUpdate(const QString& path);
	QString m_restoreNote;              //!< what the restore point holds, for the final message
	QStringList m_alsoCache;            //!< the installed version's .deb, cached by the install step
	SuneerDebFetcher* m_fetcher { nullptr };

	ScUpdateInfo m_info;
	QString m_apiKey;
	ScUpdateClient* m_client;
	QPushButton* m_updateButton { nullptr };
	QProgressDialog* m_progressDialog { nullptr };
	QProcess* m_installProcess { nullptr };
};

#endif
