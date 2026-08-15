/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "updatesettingsdialog.h"
#include "updateavailabledialog.h"

#include "scupdatekeystore.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
	const char* kOrg = "Faircode";
	const char* kApp = "ScribusUpdater";
	const char* kUrlKey = "serverUrl";
}

UpdateSettingsDialog::UpdateSettingsDialog(QWidget* parent)
	: QDialog(parent)
	, m_client(new ScUpdateClient(this))
{
	setWindowTitle(tr("Update Settings"));

	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout();

	m_urlEdit = new QLineEdit(this);
	m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/scribus"));
	form->addRow(tr("Update Server URL:"), m_urlEdit);

	auto* keyRow = new QHBoxLayout();
	m_keyEdit = new QLineEdit(this);
	m_keyEdit->setEchoMode(QLineEdit::Password);
	keyRow->addWidget(m_keyEdit);
	m_showKeyButton = new QToolButton(this);
	m_showKeyButton->setText(tr("Show"));
	m_showKeyButton->setCheckable(true);
	keyRow->addWidget(m_showKeyButton);
	form->addRow(tr("API Key:"), keyRow);

	layout->addLayout(form);

	m_saveCheck = new QCheckBox(tr("Save Settings (remember URL and key)"), this);
	layout->addWidget(m_saveCheck);

	m_statusLabel = new QLabel(this);
	m_statusLabel->setWordWrap(true);
	layout->addWidget(m_statusLabel);

	auto* buttons = new QDialogButtonBox(this);
	m_checkButton = buttons->addButton(tr("Check for Updates"), QDialogButtonBox::ActionRole);
	m_clearKeyButton = buttons->addButton(tr("Clear Saved Key"), QDialogButtonBox::ResetRole);
	buttons->addButton(QDialogButtonBox::Close);
	layout->addWidget(buttons);

	connect(m_checkButton, &QPushButton::clicked, this, &UpdateSettingsDialog::checkClicked);
	connect(m_clearKeyButton, &QPushButton::clicked, this, &UpdateSettingsDialog::clearKeyClicked);
	connect(m_showKeyButton, &QToolButton::toggled, this, &UpdateSettingsDialog::toggleKeyVisibility);
	connect(m_saveCheck, &QCheckBox::toggled, this, &UpdateSettingsDialog::saveToggled);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_client, &ScUpdateClient::updateAvailable, this, &UpdateSettingsDialog::onUpdateAvailable);
	connect(m_client, &ScUpdateClient::upToDate, this, &UpdateSettingsDialog::onUpToDate);
	connect(m_client, &ScUpdateClient::authError, this, &UpdateSettingsDialog::onAuthError);
	connect(m_client, &ScUpdateClient::notFoundError, this, &UpdateSettingsDialog::onNotFoundError);
	connect(m_client, &ScUpdateClient::networkError, this, &UpdateSettingsDialog::onNetworkError);

	QSettings settings(kOrg, kApp);
	const QString savedUrl = settings.value(kUrlKey).toString();
	const QString savedKey = ScUpdateKeyStore::load();
	m_urlEdit->setText(savedUrl);
	m_keyEdit->setText(savedKey);
	m_saveCheck->setChecked(!savedUrl.isEmpty() || !savedKey.isEmpty());
}

QString UpdateSettingsDialog::currentApiKey() const
{
	return m_keyEdit->text();
}

void UpdateSettingsDialog::setBusy(bool busy)
{
	m_checkButton->setEnabled(!busy);
	m_urlEdit->setEnabled(!busy);
	m_keyEdit->setEnabled(!busy);
}

void UpdateSettingsDialog::persistSettings()
{
	QSettings settings(kOrg, kApp);
	settings.setValue(kUrlKey, m_urlEdit->text().trimmed());
	ScUpdateKeyStore::store(currentApiKey());
}

void UpdateSettingsDialog::checkClicked()
{
	const QString url = m_urlEdit->text().trimmed();
	const QString key = currentApiKey();
	if (url.isEmpty() || key.isEmpty())
	{
		m_statusLabel->setText(tr("Enter both the server URL and the API key."));
		return;
	}

	setBusy(true);
	m_statusLabel->setText(tr("Checking for updates…"));
	m_client->checkForUpdate(url, key);
}

void UpdateSettingsDialog::clearKeyClicked()
{
	ScUpdateKeyStore::clear();
	m_keyEdit->clear();
	m_statusLabel->setText(tr("Saved API key cleared."));
}

void UpdateSettingsDialog::toggleKeyVisibility()
{
	const bool showing = m_showKeyButton->isChecked();
	m_keyEdit->setEchoMode(showing ? QLineEdit::Normal : QLineEdit::Password);
	m_showKeyButton->setText(showing ? tr("Hide") : tr("Show"));
}

void UpdateSettingsDialog::saveToggled(bool checked)
{
	if (checked)
		persistSettings();
}

void UpdateSettingsDialog::onUpdateAvailable(const ScUpdateInfo& info)
{
	setBusy(true);
	m_statusLabel->clear();

	if (m_saveCheck->isChecked())
	{
		persistSettings();
	}
	else
	{
		const int ret = QMessageBox::question(this, tr("Save Settings"),
			tr("Save the server URL and API key for next time?"),
			QMessageBox::Yes | QMessageBox::No);
		if (ret == QMessageBox::Yes)
		{
			m_saveCheck->setChecked(true); // triggers persistSettings() via saveToggled
		}
	}

	const QString apiKey = currentApiKey();
	accept();

	auto* dlg = new UpdateAvailableDialog(info, apiKey, parentWidget());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}

void UpdateSettingsDialog::onUpToDate()
{
	setBusy(false);
	m_statusLabel->setText(tr("You're on the latest version."));
	if (m_saveCheck->isChecked())
		persistSettings();
}

void UpdateSettingsDialog::onAuthError()
{
	setBusy(false);
	m_statusLabel->setText(tr("Invalid API key — contact your administrator."));
}

void UpdateSettingsDialog::onNotFoundError()
{
	setBusy(false);
	m_statusLabel->setText(tr("Update server not found."));
}

void UpdateSettingsDialog::onNetworkError(const QString& message)
{
	setBusy(false);
	m_statusLabel->setText(message);
}
