/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "updatecheckdialog.h"

#include "scupdatekeystore.h"
#include "updateavailabledialog.h"
#include "updatesettingsdialog.h"
#include "suneerrollbackdialog.h"
#include "suneerrestorepoint.h"
#include "scribus.h"
#include "scraction.h"

#include <QApplication>
#include <QFile>

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QDebug>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
	// Same store as UpdateSettingsDialog.
	const char* kOrg = "Faircode";
	const char* kApp = "ScribusUpdater";
	const char* kUrlKey = "serverUrl";
}

QString UpdateCheckDialog::savedServerUrl()
{
	QSettings settings(kOrg, kApp);
	return settings.value(kUrlKey).toString().trimmed();
}

UpdateCheckDialog::UpdateCheckDialog(QWidget* parent, bool checkNow)
	: QDialog(parent)
	, m_client(new ScUpdateClient(this))
{
	setWindowTitle(tr("Check for Updates"));
	resize(520, 400);

	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout();
	m_currentLabel = new QLabel(ScUpdateClient::localVersion(), this);
	m_currentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	form->addRow(tr("Installed version:"), m_currentLabel);
	m_latestLabel = new QLabel(QStringLiteral("-"), this);
	m_latestLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	form->addRow(tr("Latest version:"), m_latestLabel);
	layout->addLayout(form);

	m_statusLabel = new QLabel(this);
	m_statusLabel->setWordWrap(true);
	layout->addWidget(m_statusLabel);

	layout->addWidget(new QLabel(tr("What is new:"), this));
	m_changelog = new QTextBrowser(this);
	m_changelog->setOpenExternalLinks(false);
	layout->addWidget(m_changelog, 1);

	auto* buttons = new QDialogButtonBox(this);
	m_settingsButton = buttons->addButton(tr("Settings..."), QDialogButtonBox::ResetRole);
	m_tryDefaultButton = buttons->addButton(tr("Try the default server"), QDialogButtonBox::ActionRole);
	m_tryDefaultButton->setToolTip(tr("Forget the address saved under Settings... and check the server named in %1").arg(ScUpdateClient::systemConfigPath()));
	m_tryDefaultButton->hide();
	m_goBackButton = buttons->addButton(tr("Go back to previous version..."), QDialogButtonBox::ActionRole);
	m_goBackButton->setToolTip(tr("Install an earlier version again, with the settings saved before its update"));
	m_updateButton = buttons->addButton(tr("Update"), QDialogButtonBox::AcceptRole);
	m_laterButton = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
	m_updateButton->setEnabled(false);
	layout->addWidget(buttons);

	connect(m_updateButton, &QPushButton::clicked, this, &UpdateCheckDialog::updateClicked);
	connect(m_settingsButton, &QPushButton::clicked, this, &UpdateCheckDialog::settingsClicked);
	connect(m_tryDefaultButton, &QPushButton::clicked, this, &UpdateCheckDialog::tryDefaultClicked);
	connect(m_goBackButton, &QPushButton::clicked, this, &UpdateCheckDialog::goBackClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_client, &ScUpdateClient::updateAvailable, this, &UpdateCheckDialog::onUpdateAvailable);
	connect(m_client, &ScUpdateClient::upToDate, this, &UpdateCheckDialog::onUpToDate);
	connect(m_client, &ScUpdateClient::authError, this, &UpdateCheckDialog::onAuthError);
	connect(m_client, &ScUpdateClient::notFoundError, this, &UpdateCheckDialog::onNotFoundError);
	connect(m_client, &ScUpdateClient::networkError, this, &UpdateCheckDialog::onNetworkError);

	if (checkNow)
		QTimer::singleShot(0, this, &UpdateCheckDialog::startCheck);
}

void UpdateCheckDialog::setResult(const QString& latest, const QString& status, const QString& changelog, bool canUpdate)
{
	m_latestLabel->setText(latest);
	m_statusLabel->setText(status);
	m_changelog->setPlainText(changelog);
	m_updateButton->setEnabled(canUpdate);
	m_settingsButton->setEnabled(true);
	// A saved address that fails: offer the system-wide one in one click.
	const bool failed = latest == QLatin1String("-");
	m_tryDefaultButton->setVisible(failed && m_source == QLatin1String("settings")
		&& !ScUpdateClient::effectiveSettings(false).url.isEmpty());
	if (canUpdate)
		m_updateButton->setDefault(true);
	else
		m_laterButton->setDefault(true);
}

void UpdateCheckDialog::startCheck()
{
	const ScUpdateSettings s = ScUpdateClient::effectiveSettings();
	m_apiKey = s.apiKey;
	m_source = s.source;
	m_checkedUrl = s.url;
	m_tryDefaultButton->hide();
	if (s.url.isEmpty())
	{
		setResult(QStringLiteral("-"),
			tr("No update server is set up: %1 has no url line and nothing valid was entered under Settings...")
				.arg(ScUpdateClient::systemConfigPath()),
			QString(), false);
		return;
	}
	m_latestLabel->setText(QStringLiteral("-"));
	QString line = tr("Checking %1 ...").arg(s.url);
	if (!s.ignoredUserUrl.isEmpty())
		line = s.ignoredUserUrl + QLatin1Char('\n') + line;
	m_statusLabel->setText(line);
	m_changelog->clear();
	m_updateButton->setEnabled(false);
	m_settingsButton->setEnabled(false);
	m_client->checkForUpdate(s.url, m_apiKey, 15000);
}

void UpdateCheckDialog::showUpdate(const ScUpdateInfo& info, const QString& apiKey)
{
	m_apiKey = apiKey;
	onUpdateAvailable(info);
}

void UpdateCheckDialog::onUpdateAvailable(const ScUpdateInfo& info)
{
	m_info = info;
	// a version the user went back from is offered, never pushed: it goes in only when Update is pressed
	QDateTime leftWhen;
	const QString left = SuneerRestorePoint::rollbackLeftVersion(&leftWhen);
	QString status = tr("<b>A newer version is available.</b>");
	if (!left.isEmpty() && ScUpdateClient::compareVersions(info.version, left) <= 0)
		status = tr("<b>A newer version is available.</b> You went back from %1 on %2; it is installed again only if you press Update.")
		         .arg(left, QLocale().toString(leftWhen, QLocale::ShortFormat));
	setResult(info.version, status,
		info.changelog.isEmpty() ? tr("No changelog was provided.") : info.changelog,
		true);
}

void UpdateCheckDialog::onUpToDate()
{
	const ScUpdateInfo& info = m_client->lastCheckedInfo();
	const QString v = info.version.isEmpty() ? ScUpdateClient::localVersion() : info.version;
	setResult(v, tr("You have the latest version (%1).").arg(v), info.changelog, false);
}

void UpdateCheckDialog::onAuthError()
{
	setResult(QStringLiteral("-"), tr("The update server %1 refused the API key. Click Settings... or ask the administrator.").arg(m_checkedUrl), QString(), false);
}

void UpdateCheckDialog::onNotFoundError()
{
	setResult(QStringLiteral("-"), tr("Nothing was found at %1/latest.json: the server is reachable but the update address is wrong. Click Settings... to check it.").arg(m_checkedUrl), QString(), false);
}

void UpdateCheckDialog::onNetworkError(const QString& message)
{
	setResult(QStringLiteral("-"), tr("Could not check for updates: %1").arg(message), QString(), false);
}

void UpdateCheckDialog::updateClicked()
{
	if (m_info.version.isEmpty())
		return;
	// UpdateAvailableDialog owns the download, the checksum checks and the
	// pkexec install, including the rule that dpkg is never interrupted.
	auto* dlg = new UpdateAvailableDialog(m_info, m_apiKey, parentWidget());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
	dlg->startUpdate();
	accept();
}

void UpdateCheckDialog::settingsClicked()
{
	// One instance, owned by and modal to this window, always brought to the
	// front: on the office PCs it used to open behind this dialog, or a second
	// copy opened while the first was still up, and the button "did nothing".
	if (!m_settingsDialog)
	{
		m_settingsDialog = new UpdateSettingsDialog(this);
		m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
		m_settingsDialog->setWindowModality(Qt::WindowModal);
		connect(m_settingsDialog, &QDialog::finished, this, [this]() { startCheck(); });
	}
	m_settingsDialog->show();
	const QRect me = frameGeometry();
	m_settingsDialog->adjustSize();
	m_settingsDialog->move(me.center() - m_settingsDialog->rect().center());
	m_settingsDialog->raise();
	m_settingsDialog->activateWindow();
}

void UpdateCheckDialog::goBackClicked()
{
	SuneerRollbackDialog dlg(this);
	dlg.setMainWindow(parentWidget());
	dlg.exec();
	if (dlg.result() == QDialog::Accepted)
		accept();
}

void UpdateCheckDialog::tryDefaultClicked()
{
	ScUpdateClient::clearSavedServerUrl();
	startCheck();
}

void UpdateCheckDialog::runSelfTestIfAsked(QWidget* mainWindow)
{
	const QString prefix = qEnvironmentVariable("SUNEER_UPDATE_SELFTEST");
	if (prefix.isEmpty())
		return;
	auto updateDialogs = []() {
		QStringList names;
		for (QWidget* w : QApplication::topLevelWidgets())
			if (w->isVisible() && (qobject_cast<UpdateCheckDialog*>(w) || qobject_cast<UpdateAvailableDialog*>(w)))
				names << w->metaObject()->className();
		return names;
	};
	auto write = [](const QString& path, const QString& text) {
		QFile f(path);
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			f.write(text.toUtf8());
	};
	// 1. 15 s after startup (the old automatic check fired at 10 s): no update dialog may exist
	QTimer::singleShot(15000, mainWindow, [mainWindow, prefix, updateDialogs, write]() {
		const QStringList dialogs = updateDialogs();
		write(prefix + QStringLiteral("-startup.txt"), QStringLiteral("update dialogs after 15 s: %1\n").arg(dialogs.isEmpty() ? QStringLiteral("none") : dialogs.join(',')));
		fprintf(stderr, "UPDATE-SELFTEST startup: %s\n", dialogs.isEmpty() ? "no dialog" : qPrintable(dialogs.join(',')));
		// 2. the manual path: the Help menu action; the dialog is modal, so the result is read from a timer
		QTimer::singleShot(8000, mainWindow, [mainWindow, prefix, updateDialogs, write]() {
			UpdateCheckDialog* dlg = nullptr;
			for (QWidget* w : QApplication::topLevelWidgets())
				if (auto* d = qobject_cast<UpdateCheckDialog*>(w))
					dlg = d;
			QString text;
			if (!dlg)
				text = QStringLiteral("manual: no Check for Updates dialog\n");
			else
			{
				text = QStringLiteral("manual: installed %1 | latest %2 | status %3 | Update button enabled %4 | dialogs %5\n")
				       .arg(dlg->m_currentLabel->text(), dlg->m_latestLabel->text(), dlg->m_statusLabel->text())
				       .arg(dlg->m_updateButton->isEnabled()).arg(updateDialogs().join(','));
				dlg->grab().save(prefix + QStringLiteral("-manual.png"));
				// SUNEER_UPDATE_SELFTEST_ACTION: later (default, nothing installed) | update (press Update;
				// SUNEER_UPDATE_AUTOYES=1 answers the install question) | goback (Go back..., with
				// SUNEER_ROLLBACK_SELFTEST=<version> choosing the row). The pkexec step really runs.
				const QString action = qEnvironmentVariable("SUNEER_UPDATE_SELFTEST_ACTION", QStringLiteral("later"));
				if (action == QLatin1String("update") && dlg->m_updateButton->isEnabled())
					dlg->m_updateButton->click();
				else if (action == QLatin1String("goback"))
					dlg->m_goBackButton->click();
				else
					dlg->reject();                       // Later: nothing is installed
			}
			write(prefix + QStringLiteral("-manual.txt"), text);
			fprintf(stderr, "UPDATE-SELFTEST %s", qPrintable(text));
			if (qEnvironmentVariable("SUNEER_UPDATE_SELFTEST_ACTION", QStringLiteral("later")) == QLatin1String("later"))
				QTimer::singleShot(500, mainWindow, [mainWindow]() { mainWindow->close(); });
		});
		if (auto* mw = qobject_cast<ScribusMainWindow*>(mainWindow))
			if (mw->scrActions.contains("helpCheckUpdates"))
				mw->scrActions["helpCheckUpdates"]->trigger();
	});
}
