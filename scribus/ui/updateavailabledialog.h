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

class QPushButton;
class QProgressDialog;

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

private slots:
	void updateNowClicked();
	void onDownloadProgress(qint64 received, qint64 total);
	void onDownloadFinished(const QString& path);
	void onDownloadFailed(const QString& message);
	void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onInstallError(QProcess::ProcessError error);

private:
	void installUpdate(const QString& path);

	ScUpdateInfo m_info;
	QString m_apiKey;
	ScUpdateClient* m_client;
	QPushButton* m_updateButton { nullptr };
	QProgressDialog* m_progressDialog { nullptr };
	QProcess* m_installProcess { nullptr };
};

#endif
