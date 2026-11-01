/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#include "suneerrollbackdialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include "scupdateclient.h"

SuneerRollbackDialog::SuneerRollbackDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Go back to a previous version"));
	resize(600, 360);
	auto* layout = new QVBoxLayout(this);
	auto* head = new QLabel(tr("Installed now: <b>%1</b>. Choose the version to go back to. Its program is installed again and the "
	                           "settings saved before that update come back; Scribus then restarts. Save your documents first.")
	                            .arg(ScUpdateClient::localVersion()), this);
	head->setWordWrap(true);
	layout->addWidget(head);

	m_table = new QTableWidget(0, 4, this);
	m_table->setHorizontalHeaderLabels({ tr("Version"), tr("Saved"), tr("Program"), tr("Settings") });
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->verticalHeader()->hide();
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(m_table, 1);

	m_note = new QLabel(this);
	m_note->setWordWrap(true);
	layout->addWidget(m_note);

	auto* buttons = new QDialogButtonBox(this);
	m_goButton = buttons->addButton(tr("Go back to this version"), QDialogButtonBox::AcceptRole);
	m_closeButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
	layout->addWidget(buttons);
	connect(m_goButton, &QPushButton::clicked, this, &SuneerRollbackDialog::goBackClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() { m_goButton->setEnabled(!m_table->selectedItems().isEmpty()); });
	connect(m_table, &QTableWidget::itemDoubleClicked, this, [this]() { goBackClicked(); });
	fill();

	const QString wanted = qEnvironmentVariable("SUNEER_ROLLBACK_SELFTEST");
	if (!wanted.isEmpty())
	{
		for (int r = 0; r < m_table->rowCount(); ++r)
			if (m_table->item(r, 0)->text() == wanted)
			{
				m_table->selectRow(r);
				m_autoConfirm = true;
				QTimer::singleShot(500, this, &SuneerRollbackDialog::goBackClicked);
			}
		if (!m_autoConfirm)
			fprintf(stderr, "ROLLBACK-SELFTEST: %s is not in the list\n", qPrintable(wanted));
	}
}

SuneerRollbackDialog::~SuneerRollbackDialog()
{
	if (m_fetcher)
		m_fetcher->cancel();
	if (m_installProcess)
	{
		// never interrupt dpkg (see UpdateAvailableDialog)
		m_installProcess->disconnect(this);
		m_installProcess->waitForFinished(-1);
		m_installProcess->deleteLater();
	}
}

void SuneerRollbackDialog::reject()
{
	if (m_installProcess)
		return;
	QDialog::reject();
}

void SuneerRollbackDialog::fill()
{
	m_points.clear();
	const QString current = ScUpdateClient::localVersion();
	for (const SuneerRestorePoint::Point& p : SuneerRestorePoint::points())
		if (p.version != current)
			m_points << p;
	m_table->setRowCount(m_points.size());
	for (int r = 0; r < m_points.size(); ++r)
	{
		const SuneerRestorePoint::Point& p = m_points[r];
		m_table->setItem(r, 0, new QTableWidgetItem(p.version));
		m_table->setItem(r, 1, new QTableWidgetItem(p.when.isValid() ? QLocale().toString(p.when, QLocale::ShortFormat) : QStringLiteral("-")));
		m_table->setItem(r, 2, new QTableWidgetItem(p.debPath.isEmpty() ? tr("from the update server") : tr("saved on this PC")));
		m_table->setItem(r, 3, new QTableWidgetItem(p.settingsDir.isEmpty() ? tr("none (settings stay as they are)") : tr("saved")));
	}
	if (m_points.isEmpty())
		m_note->setText(tr("No earlier version is saved on this PC yet. A restore point is made every time an update is installed from here."));
	else
	{
		m_table->selectRow(0);
		m_note->setText(tr("Package cache: %1. Settings backups: %2.").arg(SuneerRestorePoint::cacheDir(), SuneerRestorePoint::backupRoot()));
	}
	m_goButton->setEnabled(!m_points.isEmpty());
}

void SuneerRollbackDialog::goBackClicked()
{
	if (busy())
		return;
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_points.size())
		return;
	m_chosen = m_points[row];
	const QString what = m_chosen.settingsDir.isEmpty() ? tr("the program only (no settings backup exists for it)") : tr("the program and the settings saved with it");
	if (!m_autoConfirm)
	{
		const int ret = QMessageBox::question(this, tr("Go back"),
			tr("Go back from %1 to %2?\n\nThis installs %3 and restarts Scribus. A system window will ask for the administrator password.")
				.arg(ScUpdateClient::localVersion(), m_chosen.version, what),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (ret != QMessageBox::Yes)
			return;
	}
	m_goButton->setEnabled(false);
	m_closeButton->setEnabled(false);
	// the version being left gets its own restore point first (its package is cached by the install step)
	QString why;
	m_leftSettingsDir = SuneerRestorePoint::saveSettings(ScUpdateClient::localVersion(), &why);
	if (m_leftSettingsDir.isEmpty())
		qWarning().noquote() << "[Rollback] settings of" << ScUpdateClient::localVersion() << "not saved:" << why;

	if (!m_chosen.debPath.isEmpty())
	{
		install(m_chosen.debPath);
		return;
	}
	const ScUpdateSettings s = ScUpdateClient::effectiveSettings();
	m_fetcher = new SuneerDebFetcher(this);
	m_progress = new QProgressDialog(tr("Downloading version %1 from the update server…").arg(m_chosen.version), tr("Cancel"), 0, 0, this);
	m_progress->setWindowModality(Qt::WindowModal);
	m_progress->setMinimumDuration(0);
	connect(m_progress, &QProgressDialog::canceled, this, [this]() { if (m_fetcher) m_fetcher->cancel(); });
	connect(m_fetcher, &SuneerDebFetcher::progress, this, [this](qint64 r, qint64 t) { if (m_progress && t > 0) { m_progress->setRange(0, int(t)); m_progress->setValue(int(r)); } });
	connect(m_fetcher, &SuneerDebFetcher::failed, this, [this](const QString& m) {
		if (m_progress) { m_progress->hide(); m_progress->deleteLater(); m_progress = nullptr; }
		m_fetcher->deleteLater();
		m_fetcher = nullptr;
		QMessageBox::critical(this, tr("Go back"), tr("Version %1 could not be fetched: %2").arg(m_chosen.version, m));
		m_goButton->setEnabled(true);
		m_closeButton->setEnabled(true);
	});
	connect(m_fetcher, &SuneerDebFetcher::done, this, [this](const QString& path) {
		if (m_progress) { m_progress->hide(); m_progress->deleteLater(); m_progress = nullptr; }
		m_fetcher->deleteLater();
		m_fetcher = nullptr;
		install(path);
	});
	m_progress->show();
	m_fetcher->fetch(m_chosen.version, s.url, s.apiKey);
}

void SuneerRollbackDialog::install(const QString& debPath)
{
	m_installProcess = new QProcess(this);
	connect(m_installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &SuneerRollbackDialog::onInstallFinished);
	connect(m_installProcess, &QProcess::errorOccurred, this, &SuneerRollbackDialog::onInstallError);
	m_progress = new QProgressDialog(tr("Installing version %1…\nEnter the password in the system window if it asks. Do not turn off the computer.").arg(m_chosen.version), QString(), 0, 0, this);
	m_progress->setCancelButton(nullptr);
	m_progress->setWindowFlag(Qt::WindowCloseButtonHint, false);
	m_progress->setWindowModality(Qt::WindowModal);
	m_progress->setMinimumDuration(0);
	m_progress->show();
	m_installProcess->start(SuneerRestorePoint::program(), SuneerRestorePoint::installArguments(debPath, m_chosen.version));
}

void SuneerRollbackDialog::onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (m_progress) { m_progress->hide(); m_progress->deleteLater(); m_progress = nullptr; }
	const QString err = QString::fromLocal8Bit(m_installProcess->readAllStandardError());
	const QString out = QString::fromLocal8Bit(m_installProcess->readAllStandardOutput());
	m_installProcess->deleteLater();
	m_installProcess = nullptr;
	if (exitStatus == QProcess::NormalExit && exitCode == 0)
	{
		finishWithRestart();
		return;
	}
	const QString details = err.isEmpty() ? out : err;
	QMessageBox::critical(this, tr("Go back"), tr("Version %1 could not be installed (exit code %2).\n\n%3")
		.arg(m_chosen.version).arg(exitCode).arg(details.isEmpty() ? tr("No further detail was reported.") : details));
	m_goButton->setEnabled(true);
	m_closeButton->setEnabled(true);
}

void SuneerRollbackDialog::onInstallError(QProcess::ProcessError error)
{
	if (error != QProcess::FailedToStart)
		return;
	if (m_progress) { m_progress->hide(); m_progress->deleteLater(); m_progress = nullptr; }
	QMessageBox::critical(this, tr("Go back"), tr("Could not start %1. Is polkit installed?").arg(SuneerRestorePoint::program()));
	m_installProcess->deleteLater();
	m_installProcess = nullptr;
	m_goButton->setEnabled(true);
	m_closeButton->setEnabled(true);
}

void SuneerRollbackDialog::finishWithRestart()
{
	SuneerRestorePoint::writeRollbackMarker(ScUpdateClient::localVersion(), m_chosen.version);
	QString why;
	bool restoring = false;
	if (!m_chosen.settingsDir.isEmpty())
	{
		restoring = SuneerRestorePoint::restoreAfterExit(m_chosen.settingsDir, QCoreApplication::applicationPid(), true, &why);
		if (!restoring)
			qWarning().noquote() << "[Rollback] settings restore not started:" << why;
	}
	else
		restoring = QProcess::startDetached(QStringLiteral("/bin/sh"), { QStringLiteral("-c"),
			QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; sleep 1; exec \"$2\""), QStringLiteral("sh"),
			QString::number(QCoreApplication::applicationPid()), QCoreApplication::applicationDirPath() + QStringLiteral("/scribus") });
	fprintf(stderr, "ROLLBACK: installed %s, settings %s, restart %s\n", qPrintable(m_chosen.version),
	        m_chosen.settingsDir.isEmpty() ? "none" : qPrintable(m_chosen.settingsDir), restoring ? "yes" : "no");
	if (!m_autoConfirm)
		QMessageBox::information(this, tr("Go back"),
			restoring ? tr("Version %1 is installed. Scribus now closes; the saved settings are put back and Scribus starts again with %1.").arg(m_chosen.version)
			          : tr("Version %1 is installed, but the restart step could not be started (%2). Close Scribus and start it again yourself.").arg(m_chosen.version, why));
	accept();
	if (m_mainWindow && restoring)
	{
		// The main window refuses to close while the New Document / Preferences dialog exists, and a closed
		// dialog is deleted only after its own exec() returns: close it, then try again. Without such a
		// dialog there is exactly one close(), so an unsaved-document question is asked once.
		auto* closer = new QTimer(m_mainWindow);
		closer->setInterval(300);
		connect(closer, &QTimer::timeout, m_mainWindow, [mw = m_mainWindow, closer]() {
			bool blocked = false;
			for (QWidget* w : mw->findChildren<QDialog*>())
				if (w->objectName() == QLatin1String("NewDocumentWindow") || w->objectName() == QLatin1String("PreferencesDialog"))
				{
					blocked = true;
					w->close();
				}
			if (blocked)
				return;
			closer->stop();
			closer->deleteLater();
			mw->close();
		});
		closer->start();
	}
}
