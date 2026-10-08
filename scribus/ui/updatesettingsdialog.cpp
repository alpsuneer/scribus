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
	// The system-wide address, read straight from the file (a saved user URL
	// must not hide it here).
	{
		QSettings probe(kOrg, kApp);
		const QString saved = probe.value(kUrlKey).toString();
		probe.remove(kUrlKey);
		m_defaultUrl = ScUpdateClient::effectiveSettings(false).url;
		if (!saved.isEmpty())
			probe.setValue(kUrlKey, saved);
	}
	// Empty here means "use /etc/scribus/update.conf"; show what that is.
	m_urlEdit->setPlaceholderText(!m_defaultUrl.isEmpty()
		? tr("%1 (default, from %2)").arg(m_defaultUrl, ScUpdateClient::systemConfigPath())
		: QStringLiteral("http://server:8095/scribus-updates"));
	m_urlEdit->setToolTip(tr("One address only, starting with http:// or https://. Leave empty to use the default from %1.").arg(ScUpdateClient::systemConfigPath()));
	auto* urlRow = new QHBoxLayout();
	urlRow->addWidget(m_urlEdit, 1);
	m_useDefaultButton = new QPushButton(tr("Use default"), this);
	m_useDefaultButton->setToolTip(tr("Forget the saved address and use %1").arg(m_defaultUrl.isEmpty() ? ScUpdateClient::systemConfigPath() : m_defaultUrl));
	m_useDefaultButton->setEnabled(!m_defaultUrl.isEmpty());
	m_useDefaultButton->setAutoDefault(false);   // Enter in the URL field means "check", not "forget"
	urlRow->addWidget(m_useDefaultButton);
	form->addRow(tr("Update Server URL:"), urlRow);
	m_urlErrorLabel = new QLabel(this);
	m_urlErrorLabel->setWordWrap(true);
	m_urlErrorLabel->setStyleSheet(QStringLiteral("color: #b00020;"));
	m_urlErrorLabel->setMinimumWidth(360);
	m_urlErrorLabel->hide();
	form->addRow(QString(), m_urlErrorLabel);

	auto* keyRow = new QHBoxLayout();
	m_keyEdit = new QLineEdit(this);
	m_keyEdit->setEchoMode(QLineEdit::Password);
	m_keyEdit->setPlaceholderText(tr("optional"));
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
	m_checkButton->setDefault(true);
	layout->addWidget(buttons);

	connect(m_checkButton, &QPushButton::clicked, this, &UpdateSettingsDialog::checkClicked);
	connect(m_clearKeyButton, &QPushButton::clicked, this, &UpdateSettingsDialog::clearKeyClicked);
	connect(m_useDefaultButton, &QPushButton::clicked, this, &UpdateSettingsDialog::useDefaultClicked);
	connect(m_urlEdit, &QLineEdit::textEdited, this, [this]() { m_urlErrorLabel->hide(); });
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
	if (!savedUrl.trimmed().isEmpty())
	{
		QString why;
		if (ScUpdateClient::validateServerUrl(savedUrl, &why).isEmpty())
		{
			m_urlErrorLabel->setText(tr("The saved address is not used because %1. Correct it or click Use default.").arg(why));
			m_urlErrorLabel->show();
			adjustSize();
		}
	}
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

QString UpdateSettingsDialog::validatedUrl(bool allowEmpty)
{
	const QString text = m_urlEdit->text();
	if (text.trimmed().isEmpty())
	{
		m_urlErrorLabel->hide();
		return allowEmpty ? QString() : QString();
	}
	QString why;
	const QString url = ScUpdateClient::validateServerUrl(text, &why);
	if (url.isEmpty())
	{
		m_urlErrorLabel->setText(tr("This address cannot be used because %1.").arg(why));
		m_urlErrorLabel->show();
		adjustSize();                       // the dialog is already visible: make room for the row
		return QString();
	}
	m_urlErrorLabel->hide();
	if (url != text)
		m_urlEdit->setText(url);
	return url;
}

bool UpdateSettingsDialog::persistSettings()
{
	const QString url = validatedUrl(true);
	if (url.isEmpty() && !m_urlEdit->text().trimmed().isEmpty())
		return false;                       // invalid: shown inline, nothing saved
	QSettings settings(kOrg, kApp);
	if (url.isEmpty())
		settings.remove(kUrlKey);           // empty = use update.conf
	else
		settings.setValue(kUrlKey, url);
	ScUpdateKeyStore::store(currentApiKey());
	return true;
}

void UpdateSettingsDialog::useDefaultClicked()
{
	ScUpdateClient::clearSavedServerUrl();
	m_urlEdit->clear();
	m_urlErrorLabel->hide();
	m_statusLabel->setText(m_defaultUrl.isEmpty()
		? tr("Saved address forgotten. %1 names no server.").arg(ScUpdateClient::systemConfigPath())
		: tr("Saved address forgotten. Scribus now uses %1 (from %2).").arg(m_defaultUrl, ScUpdateClient::systemConfigPath()));
}

void UpdateSettingsDialog::checkClicked()
{
	QString url = validatedUrl(true);
	QString key = currentApiKey();
	if (url.isEmpty() && !m_urlEdit->text().trimmed().isEmpty())
		return;                             // invalid: the inline error says why
	if (url.isEmpty())
	{
		// Fall back to the system-wide file, key included.
		const ScUpdateSettings s = ScUpdateClient::effectiveSettings(false);
		url = s.url;
		if (key.isEmpty())
			key = s.apiKey;
	}
	if (url.isEmpty())
	{
		m_statusLabel->setText(tr("Enter the server address (there is none in %1).").arg(ScUpdateClient::systemConfigPath()));
		return;
	}
	if (m_saveCheck->isChecked() && !persistSettings())
		return;

	setBusy(true);
	m_statusLabel->setText(tr("Checking %1 ...").arg(url));
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
	if (checked && !persistSettings())
		m_saveCheck->setChecked(false);
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
	const QString v = m_client->lastCheckedInfo().version;
	m_statusLabel->setText(v.isEmpty() ? tr("You have the latest version.") : tr("You have the latest version (%1).").arg(v));
	if (m_saveCheck->isChecked())
		persistSettings();
}

void UpdateSettingsDialog::onAuthError()
{
	setBusy(false);
	m_statusLabel->setText(tr("The update server refused the API key. Ask the administrator."));
}

void UpdateSettingsDialog::onNotFoundError()
{
	setBusy(false);
	m_statusLabel->setText(tr("Nothing was found at that address (no latest.json): the server answers, but the address is wrong."));
}

void UpdateSettingsDialog::onNetworkError(const QString& message)
{
	setBusy(false);
	m_statusLabel->setText(message);
}
