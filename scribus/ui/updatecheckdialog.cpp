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

#include <QDate>
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
	const char* kLastAutoCheckKey = "lastAutoCheck";
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
	m_updateButton = buttons->addButton(tr("Update"), QDialogButtonBox::AcceptRole);
	m_laterButton = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
	m_updateButton->setEnabled(false);
	layout->addWidget(buttons);

	connect(m_updateButton, &QPushButton::clicked, this, &UpdateCheckDialog::updateClicked);
	connect(m_settingsButton, &QPushButton::clicked, this, &UpdateCheckDialog::settingsClicked);
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
	if (canUpdate)
		m_updateButton->setDefault(true);
	else
		m_laterButton->setDefault(true);
}

void UpdateCheckDialog::startCheck()
{
	const ScUpdateSettings s = ScUpdateClient::effectiveSettings();
	m_apiKey = s.apiKey;
	if (s.url.isEmpty())
	{
		setResult(QStringLiteral("-"),
			tr("No update server is configured: %1 has no url line and nothing was entered under Settings...")
				.arg(ScUpdateClient::systemConfigPath()),
			QString(), false);
		return;
	}
	m_latestLabel->setText(QStringLiteral("-"));
	m_statusLabel->setText(s.source == QLatin1String("conf")
		? tr("Checking %1 (from %2)...").arg(s.url, ScUpdateClient::systemConfigPath())
		: tr("Checking %1...").arg(s.url));
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
	setResult(info.version,
		tr("<b>A newer version is available.</b>"),
		info.changelog.isEmpty() ? tr("No changelog was provided.") : info.changelog,
		true);
}

void UpdateCheckDialog::onUpToDate()
{
	const ScUpdateInfo& info = m_client->lastCheckedInfo();
	setResult(info.version.isEmpty() ? ScUpdateClient::localVersion() : info.version,
		tr("You have the latest version."),
		info.changelog, false);
}

void UpdateCheckDialog::onAuthError()
{
	setResult(QStringLiteral("-"), tr("The update server refused the API key. Click Settings... or contact your administrator."), QString(), false);
}

void UpdateCheckDialog::onNotFoundError()
{
	setResult(QStringLiteral("-"), tr("The update server has no latest.json at the saved URL. Click Settings... to check the address."), QString(), false);
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
	UpdateSettingsDialog dia(this);
	dia.exec();
	startCheck();
}

void UpdateCheckDialog::runStartupCheck(QWidget* parent)
{
	QSettings settings(kOrg, kApp);
	const QString today = QDate::currentDate().toString(Qt::ISODate);
	if (settings.value(kLastAutoCheckKey).toString() == today)
		return;                      // already tried today
	// The OS key store is read here too: a user who entered their own server
	// in Update Settings keeps using their key. For the system-wide server
	// (update.conf) the key comes from that file, empty for the LAN server.
	const ScUpdateSettings s = ScUpdateClient::effectiveSettings(true);
	if (s.url.isEmpty())
		return;                      // updater not configured here
	const QString apiKey = s.apiKey;
	const QString url = s.url;
	// The attempt counts, reachable server or not: at most one a day.
	settings.setValue(kLastAutoCheckKey, today);

	auto* client = new ScUpdateClient(parent);
	QObject::connect(client, &ScUpdateClient::updateAvailable, parent, [parent, client, apiKey](const ScUpdateInfo& info) {
		auto* dlg = new UpdateCheckDialog(parent, false);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->showUpdate(info, apiKey);
		dlg->show();
		client->deleteLater();
	});
	// Everything else is silent: no dialog for "up to date", a server that
	// is down, a wrong key or a manifest that fails verification.
	QObject::connect(client, &ScUpdateClient::upToDate, client, &QObject::deleteLater);
	QObject::connect(client, &ScUpdateClient::authError, client, &QObject::deleteLater);
	QObject::connect(client, &ScUpdateClient::notFoundError, client, &QObject::deleteLater);
	QObject::connect(client, &ScUpdateClient::networkError, client, [client](const QString& message) {
		qDebug().noquote() << "Startup update check:" << message;
		client->deleteLater();
	});
	// 5 s: a laptop that is switched off must cost nothing noticeable.
	client->checkForUpdate(url, apiKey, 5000);
}
