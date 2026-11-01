/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERROLLBACKDIALOG_H
#define SUNEERROLLBACKDIALOG_H

#include <QDialog>
#include <QList>
#include <QProcess>
#include <QString>
#include "scribusapi.h"
#include "suneerrestorepoint.h"

class QLabel;
class QProgressDialog;
class QPushButton;
class QTableWidget;

/*! Help > Check for Updates... > "Go back to previous version...": the saved versions (version, date,
    package cached or from the server, settings backup yes/no); choose one, confirm, and the package is
    installed through the same pkexec step the updater uses, the settings backup is copied back once
    this Scribus has exited, and Scribus restarts. Before that the version being left gets its own
    restore point, so "Go back" can also go forward again.
    Env for tests: SUNEER_ROLLBACK_SELFTEST=<version>: that version is chosen and confirmed without a
    click (the pkexec step still runs; as root it needs no password). */
class SCRIBUS_API SuneerRollbackDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SuneerRollbackDialog(QWidget* parent = nullptr);
	~SuneerRollbackDialog() override;

	//! The main window to close when the restart is accepted (nullptr: only tell the user).
	void setMainWindow(QWidget* mainWindow) { m_mainWindow = mainWindow; }

public slots:
	void reject() override;

private slots:
	void goBackClicked();
	void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void onInstallError(QProcess::ProcessError error);

private:
	void fill();
	void install(const QString& debPath);
	void finishWithRestart();
	bool busy() const { return m_installProcess != nullptr || m_fetcher != nullptr; }

	QList<SuneerRestorePoint::Point> m_points;
	SuneerRestorePoint::Point m_chosen;
	QString m_leftSettingsDir;                 //!< restore point of the version being left
	QTableWidget* m_table { nullptr };
	QLabel* m_note { nullptr };
	QPushButton* m_goButton { nullptr };
	QPushButton* m_closeButton { nullptr };
	QProgressDialog* m_progress { nullptr };
	QProcess* m_installProcess { nullptr };
	SuneerDebFetcher* m_fetcher { nullptr };
	QWidget* m_mainWindow { nullptr };
	bool m_autoConfirm { false };
};

#endif
