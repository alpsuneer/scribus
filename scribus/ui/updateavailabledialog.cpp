/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "updateavailabledialog.h"

#include "api/api_application.h"
#include "scpaths.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateAvailableDialog::UpdateAvailableDialog(const ScUpdateInfo& info, const QString& apiKey, QWidget* parent)
	: QDialog(parent)
	, m_info(info)
	, m_apiKey(apiKey)
	, m_client(new ScUpdateClient(this))
{
	setWindowTitle(tr("Update Available"));
	resize(480, 360);

	auto* layout = new QVBoxLayout(this);

	auto* headline = new QLabel(tr("<b>Version %1 is available</b> (you have %2)")
		.arg(m_info.version, ScUpdateClient::localVersion()), this);
	layout->addWidget(headline);

	auto* changelog = new QTextBrowser(this);
	changelog->setOpenExternalLinks(false);
	changelog->setPlainText(m_info.changelog.isEmpty() ? tr("No changelog was provided.") : m_info.changelog);
	layout->addWidget(changelog, 1);

	auto* buttons = new QDialogButtonBox(this);
	m_updateButton = buttons->addButton(tr("Update Now"), QDialogButtonBox::AcceptRole);
	buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
	connect(m_updateButton, &QPushButton::clicked, this, &UpdateAvailableDialog::updateNowClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	connect(m_client, &ScUpdateClient::downloadProgress, this, &UpdateAvailableDialog::onDownloadProgress);
	connect(m_client, &ScUpdateClient::downloadFinished, this, &UpdateAvailableDialog::onDownloadFinished);
	connect(m_client, &ScUpdateClient::downloadFailed, this, &UpdateAvailableDialog::onDownloadFailed);
}

UpdateAvailableDialog::~UpdateAvailableDialog()
{
	if (m_client)
		m_client->cancelDownload();
	if (m_installProcess)
	{
		// Never kill it: interrupting dpkg -i as root can leave the package
		// half-configured. reject() already refuses to close while it runs,
		// so this only happens if the parent is torn down; let it finish.
		m_installProcess->disconnect(this);
		m_installProcess->waitForFinished(-1);
		m_installProcess->deleteLater();
	}
}

void UpdateAvailableDialog::reject()
{
	if (m_installProcess)
		return; // dpkg is running; the dialog closes itself when it finishes
	QDialog::reject();
}

void UpdateAvailableDialog::closeEvent(QCloseEvent* event)
{
	if (m_installProcess)
	{
		event->ignore();
		return;
	}
	QDialog::closeEvent(event);
}

void UpdateAvailableDialog::startUpdate()
{
	updateNowClicked();
}

void UpdateAvailableDialog::updateNowClicked()
{
	m_updateButton->setEnabled(false);

	QString fileName = QFileInfo(m_info.downloadUrl.path()).fileName();
	if (fileName.isEmpty())
		fileName = QStringLiteral("scribus-update.deb");
	const QString destPath = ScPaths::downloadDir() + fileName;

	m_progressDialog = new QProgressDialog(tr("Downloading update…"), tr("Cancel"), 0, 0, this);
	m_progressDialog->setWindowModality(Qt::WindowModal);
	m_progressDialog->setMinimumDuration(0);
	connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
		m_client->cancelDownload();
		m_updateButton->setEnabled(true);
	});
	m_progressDialog->show();

	m_client->downloadUpdate(m_info, m_apiKey, destPath);
}

void UpdateAvailableDialog::onDownloadProgress(qint64 received, qint64 total)
{
	if (!m_progressDialog)
		return;
	if (total > 0)
	{
		m_progressDialog->setRange(0, static_cast<int>(total));
		m_progressDialog->setValue(static_cast<int>(received));
	}
}

void UpdateAvailableDialog::onDownloadFinished(const QString& path)
{
	if (m_progressDialog)
	{
		m_progressDialog->hide();
		m_progressDialog->deleteLater();
		m_progressDialog = nullptr;
	}

	// Say whose password the system prompt will want. pkexec asks for the
	// password of an administrator account: this user's own if it is in the
	// sudo group, otherwise the root (administrator) password of this PC.
	auto isAdmin = []() {
		const QString me = qEnvironmentVariable("USER");
		QFile group(QStringLiteral("/etc/group"));
		if (me.isEmpty() || !group.open(QIODevice::ReadOnly | QIODevice::Text))
			return false;
		QTextStream in(&group);
		while (!in.atEnd())
		{
			const QString line = in.readLine();
			if (line.startsWith(QLatin1String("sudo:")) || line.startsWith(QLatin1String("wheel:")) || line.startsWith(QLatin1String("admin:")))
			{
				const QStringList members = line.section(':', 3).split(',', Qt::SkipEmptyParts);
				if (members.contains(me))
					return true;
			}
		}
		return false;
	};
	const QString who = isAdmin()
		? tr("A system window will ask for YOUR password (your account is an administrator).")
		: tr("A system window will ask for the ADMINISTRATOR (root) password of this PC, not your own login password. "
		     "If you do not know it, click No and ask the person who set up this computer.");
	const int ret = QMessageBox::question(this, tr("Install Update"),
		tr("The update was downloaded and its signature and checksum were verified. Install it now?\n\n%1").arg(who),
		QMessageBox::Yes | QMessageBox::No);
	if (ret != QMessageBox::Yes)
	{
		m_updateButton->setEnabled(true);
		return;
	}
	installUpdate(path);
}

void UpdateAvailableDialog::onDownloadFailed(const QString& message)
{
	if (m_progressDialog)
	{
		m_progressDialog->hide();
		m_progressDialog->deleteLater();
		m_progressDialog = nullptr;
	}
	QMessageBox::critical(this, tr("Download Failed"), message);
	m_updateButton->setEnabled(true);
}

void UpdateAvailableDialog::installUpdate(const QString& path)
{
	// Checked again here, not only after the download: the file sits in a
	// user-writable folder while the "Install now?" question is open.
	if (ScUpdateClient::fileSha256(path) != m_info.sha256)
	{
		QFile::remove(path);
		QMessageBox::critical(this, tr("Install Failed"),
			tr("The downloaded update changed after it was verified, so it was deleted and not installed."));
		m_updateButton->setEnabled(true);
		return;
	}

	m_installProcess = new QProcess(this);
	connect(m_installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this, &UpdateAvailableDialog::onInstallFinished);
	connect(m_installProcess, &QProcess::errorOccurred, this, &UpdateAvailableDialog::onInstallError);

	m_progressDialog = new QProgressDialog(
		tr("Installing update…\nEnter the password in the system window if it asks. Do not turn off the computer."), QString(), 0, 0, this);
	// No Cancel: once pkexec hands over, dpkg must be allowed to finish.
	// Dismissing the password prompt is the way to back out before that.
	m_progressDialog->setCancelButton(nullptr);
	m_progressDialog->setWindowFlag(Qt::WindowCloseButtonHint, false);
	m_progressDialog->setWindowModality(Qt::WindowModal);
	m_progressDialog->setMinimumDuration(0);
	m_progressDialog->show();

	m_installProcess->start(QStringLiteral("pkexec"), { QStringLiteral("dpkg"), QStringLiteral("-i"), path });
}

void UpdateAvailableDialog::onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (m_progressDialog)
	{
		m_progressDialog->hide();
		m_progressDialog->deleteLater();
		m_progressDialog = nullptr;
	}

	const QString stderrOutput = QString::fromLocal8Bit(m_installProcess->readAllStandardError());
	const QString stdoutOutput = QString::fromLocal8Bit(m_installProcess->readAllStandardOutput());

	if (exitStatus == QProcess::NormalExit && exitCode == 0)
	{
		QMessageBox::information(this, tr("Update Installed"),
			tr("The update was installed successfully. Restart Scribus to use the new version."));
		accept();
	}
	else
	{
		const QString details = stderrOutput.isEmpty() ? stdoutOutput : stderrOutput;
		QMessageBox::critical(this, tr("Install Failed"),
			tr("The update could not be installed (exit code %1).\n\n%2")
				.arg(exitCode)
				.arg(details.isEmpty() ? tr("No further detail was reported.") : details));
		m_updateButton->setEnabled(true);
	}

	m_installProcess->deleteLater();
	m_installProcess = nullptr;
}

void UpdateAvailableDialog::onInstallError(QProcess::ProcessError error)
{
	if (error != QProcess::FailedToStart)
		return; // other errors are followed by finished()

	if (m_progressDialog)
	{
		m_progressDialog->hide();
		m_progressDialog->deleteLater();
		m_progressDialog = nullptr;
	}
	QMessageBox::critical(this, tr("Install Failed"),
		tr("Could not start pkexec. Is polkit installed?"));
	m_updateButton->setEnabled(true);
	m_installProcess->deleteLater();
	m_installProcess = nullptr;
}
