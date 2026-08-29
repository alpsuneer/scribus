/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QString>

#include "prefs_aiservices.h"

#include "ai/lamainpaintservice.h"
#include "prefsstructs.h"
#include "scribusdoc.h"

Prefs_AIServices::Prefs_AIServices(QWidget* parent, ScribusDoc* /*doc*/)
	: Prefs_Pane(parent)
{
	setupUi(this);
	languageChange();

	m_caption = tr("AI Services");
	m_icon = "applications-system";

	connect(enableAICheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::enabledToggled);
	connect(testConnectionButton, &QPushButton::clicked, this, &Prefs_AIServices::testConnectionClicked);

	enabledToggled(enableAICheckBox->isChecked());
}

Prefs_AIServices::~Prefs_AIServices() = default;

void Prefs_AIServices::languageChange()
{
	enableAICheckBox->setToolTip("<qt>" + tr("Turn on the optional AI-assisted features. "
		"They are off until you turn them on, and they only ever contact the address below.") + "</qt>");
	iopaintUrlLineEdit->setToolTip("<qt>" + tr("Address of the IOPaint server. "
		"Pictures are sent here and nowhere else.") + "</qt>");
	timeoutSpinBox->setToolTip("<qt>" + tr("How long to wait for the server to answer before giving up. "
		"Inpainting on a processor rather than a graphics card can take a minute for a large area.") + "</qt>");
	testConnectionButton->setToolTip("<qt>" + tr("Check that the server at the address above is running and has a model loaded.") + "</qt>");

	infoLabel->setText("<qt><i>" + tr("AI inpainting runs on your own machine through IOPaint. "
		"Nothing is sent over the internet. Install and start it with:") + "</i><br/>"
		"<tt>pip install iopaint</tt><br/>"
		"<tt>iopaint start --model=lama --port=8080</tt></qt>");
}

void Prefs_AIServices::enabledToggled(bool on)
{
	iopaintGroupBox->setEnabled(on);
}

void Prefs_AIServices::restoreDefaults(struct ApplicationPrefs *prefsData)
{
	enableAICheckBox->setChecked(prefsData->aiServicePrefs.enabled);
	iopaintUrlLineEdit->setText(prefsData->aiServicePrefs.iopaintUrl);
	timeoutSpinBox->setValue(prefsData->aiServicePrefs.requestTimeoutSeconds);
	enabledToggled(prefsData->aiServicePrefs.enabled);
	testResultLabel->clear();
}

void Prefs_AIServices::saveGuiToPrefs(struct ApplicationPrefs *prefsData) const
{
	prefsData->aiServicePrefs.enabled = enableAICheckBox->isChecked();
	prefsData->aiServicePrefs.iopaintUrl = LamaInpaintService::normaliseBaseUrl(iopaintUrlLineEdit->text());
	prefsData->aiServicePrefs.requestTimeoutSeconds = timeoutSpinBox->value();
}

void Prefs_AIServices::showTestResult(const QString& text, const QString& colour)
{
	testResultLabel->setText(QStringLiteral("<qt><b style=\"color:%1;\">%2</b></qt>")
	                         .arg(colour, text.toHtmlEscaped()));
}

void Prefs_AIServices::testConnectionClicked()
{
	const QString url = LamaInpaintService::normaliseBaseUrl(iopaintUrlLineEdit->text());
	if (url.isEmpty())
	{
		showTestResult(tr("✗ Enter a server address first."), QStringLiteral("#b00020"));
		return;
	}

	if (!m_service)
	{
		m_service = new LamaInpaintService(url, timeoutSpinBox->value(), this);
		connect(m_service, &AIInpaintService::connectionTested, this, &Prefs_AIServices::connectionTested);
	}
	else
	{
		m_service->setEndpoint(url, timeoutSpinBox->value());
	}

	testConnectionButton->setEnabled(false);
	showTestResult(tr("Testing %1 ...").arg(url), QStringLiteral("#666666"));
	m_service->testConnection();
}

void Prefs_AIServices::connectionTested(bool ok, const QString& detail)
{
	testConnectionButton->setEnabled(true);
	if (ok)
		showTestResult(QStringLiteral("✓ ") + detail, QStringLiteral("#1b7f2a"));
	else
		showTestResult(QStringLiteral("✗ ") + detail, QStringLiteral("#b00020"));
}
