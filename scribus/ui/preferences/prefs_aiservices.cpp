/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QString>

#include "prefs_aiservices.h"

#include "ai/aiinpaintservicefactory.h"
#include "ai/lamainpaintservice.h"
#include "ai/openrouterinpaintservice.h"
#include "prefsstructs.h"
#include "scribusdoc.h"

Prefs_AIServices::Prefs_AIServices(QWidget* parent, ScribusDoc* /*doc*/)
	: Prefs_Pane(parent)
{
	setupUi(this);

	// The order of these two must match AIProvider, which is on the
	// preferences file format: the index is what gets stored.
	providerComboBox->addItem(tr("LaMa (local, free)"), static_cast<int>(AIProvider::LaMa));
	providerComboBox->addItem(tr("OpenRouter (cloud, paid)"), static_cast<int>(AIProvider::OpenRouter));

	const QList<OpenRouterInpaintService::ModelChoice>& models = OpenRouterInpaintService::models();
	for (const OpenRouterInpaintService::ModelChoice& choice : models)
		modelComboBox->addItem(choice.displayName, choice.id);

	languageChange();

	m_caption = tr("AI Services");
	m_icon = "applications-system";

	connect(enableAICheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::enabledToggled);
	connect(providerComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &Prefs_AIServices::providerChanged);
	connect(modelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &Prefs_AIServices::modelChanged);
	connect(showKeyCheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::showKeyToggled);
	connect(testConnectionButton, &QPushButton::clicked, this, &Prefs_AIServices::testConnectionClicked);
	connect(openRouterTestButton, &QPushButton::clicked, this, &Prefs_AIServices::testOpenRouterClicked);

	enabledToggled(enableAICheckBox->isChecked());
	providerChanged(providerComboBox->currentIndex());
}

Prefs_AIServices::~Prefs_AIServices() = default;

void Prefs_AIServices::languageChange()
{
	enableAICheckBox->setToolTip("<qt>" + tr("Turn on the optional AI-assisted features. "
		"They are off until you turn them on, and they only ever contact the service chosen below.") + "</qt>");
	providerComboBox->setToolTip("<qt>" + tr("Which service does the work for Apply (Best Quality). "
		"LaMa runs on this machine and is free. OpenRouter is a paid account on the internet, "
		"and your picture is sent to it.") + "</qt>");
	iopaintUrlLineEdit->setToolTip("<qt>" + tr("Address of the IOPaint server. "
		"Pictures are sent here and nowhere else.") + "</qt>");
	timeoutSpinBox->setToolTip("<qt>" + tr("How long to wait for the server to answer before giving up. "
		"Inpainting on a processor rather than a graphics card can take a minute for a large area.") + "</qt>");
	testConnectionButton->setToolTip("<qt>" + tr("Check that the server at the address above is running and has a model loaded.") + "</qt>");

	apiKeyLineEdit->setToolTip("<qt>" + tr("Your OpenRouter API key. It is stored in your preferences file "
		"only lightly obfuscated, so treat that file as you would the key itself.") + "</qt>");
	showKeyCheckBox->setToolTip("<qt>" + tr("Show the key, to check it was pasted correctly.") + "</qt>");
	modelComboBox->setToolTip("<qt>" + tr("Which image model does the work. You can change this between "
		"removals without re-entering the key - useful when one model refuses an edit, or to compare results.") + "</qt>");
	openRouterTimeoutSpinBox->setToolTip("<qt>" + tr("How long to wait for OpenRouter to answer before giving up.") + "</qt>");
	openRouterTestButton->setToolTip("<qt>" + tr("Check that the key above is valid and has credit. "
		"This costs nothing.") + "</qt>");

	infoLabel->setText("<qt><i>" + tr("AI inpainting runs on your own machine through IOPaint. "
		"Nothing is sent over the internet. Install and start it with:") + "</i><br/>"
		"<tt>pip install iopaint</tt><br/>"
		"<tt>iopaint start --model=lama --port=8080</tt></qt>");

	// Three plain facts, in the order someone deciding whether to use this
	// would want them: what it gets you, what it takes to start, what it costs
	// and what leaves the machine.
	openRouterInfoLabel->setText("<qt><i>"
		+ tr("One API key gives access to 30+ image models across providers.") + "<br/>"
		+ tr("Get a key: %1 - requires credits (about $10 minimum).")
		  .arg("<a href=\"https://openrouter.ai/keys\">https://openrouter.ai/keys</a>") + "<br/>"
		+ tr("Each operation costs roughly $0.03-0.15 (Rs 3-13). Your picture is sent to OpenRouter, "
		     "and on to the selected model provider's servers.")
		+ "</i></qt>");

	modelChanged(modelComboBox->currentIndex());
}

void Prefs_AIServices::enabledToggled(bool on)
{
	providerLabel->setEnabled(on);
	providerComboBox->setEnabled(on);
	iopaintGroupBox->setEnabled(on);
	openRouterGroupBox->setEnabled(on);
}

void Prefs_AIServices::providerChanged(int index)
{
	const bool openRouter = (index == static_cast<int>(AIProvider::OpenRouter));
	// Hidden rather than greyed: the settings for the provider that is not
	// selected are not "unavailable", they are irrelevant, and a page with
	// both on it invites filling both in.
	iopaintGroupBox->setVisible(!openRouter);
	infoLabel->setVisible(!openRouter);
	openRouterGroupBox->setVisible(openRouter);
}

void Prefs_AIServices::modelChanged(int index)
{
	const QList<OpenRouterInpaintService::ModelChoice>& models = OpenRouterInpaintService::models();
	if (index < 0 || index >= models.size())
	{
		modelHintLabel->clear();
		return;
	}
	modelHintLabel->setText(QStringLiteral("<qt><i>%1</i></qt>").arg(models.at(index).hint.toHtmlEscaped()));
}

void Prefs_AIServices::showKeyToggled(bool on)
{
	apiKeyLineEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
}

void Prefs_AIServices::restoreDefaults(struct ApplicationPrefs *prefsData)
{
	const AIServicePrefs& ai = prefsData->aiServicePrefs;

	enableAICheckBox->setChecked(ai.enabled);
	iopaintUrlLineEdit->setText(ai.iopaintUrl);
	timeoutSpinBox->setValue(ai.requestTimeoutSeconds);

	const int providerIndex = providerComboBox->findData(static_cast<int>(AIInpaintServiceFactory::providerOf(ai)));
	providerComboBox->setCurrentIndex(qMax(0, providerIndex));

	apiKeyLineEdit->setText(ai.openRouterApiKey);
	showKeyCheckBox->setChecked(false);
	showKeyToggled(false);
	openRouterTimeoutSpinBox->setValue(ai.openRouterTimeoutSeconds);

	// A model the list no longer offers - an old profile, or one edited by
	// hand - is added rather than silently swapped for something else, so that
	// saving the page does not quietly change which model gets used.
	int modelIndex = modelComboBox->findData(ai.openRouterModel);
	if (modelIndex < 0 && !ai.openRouterModel.isEmpty())
	{
		modelComboBox->addItem(ai.openRouterModel, ai.openRouterModel);
		modelIndex = modelComboBox->count() - 1;
	}
	modelComboBox->setCurrentIndex(qMax(0, modelIndex));

	enabledToggled(ai.enabled);
	providerChanged(providerComboBox->currentIndex());
	testResultLabel->clear();
	openRouterTestResultLabel->clear();
}

void Prefs_AIServices::saveGuiToPrefs(struct ApplicationPrefs *prefsData) const
{
	AIServicePrefs& ai = prefsData->aiServicePrefs;

	ai.enabled = enableAICheckBox->isChecked();
	ai.provider = providerComboBox->currentData().toInt();
	ai.iopaintUrl = LamaInpaintService::normaliseBaseUrl(iopaintUrlLineEdit->text());
	ai.requestTimeoutSeconds = timeoutSpinBox->value();
	ai.openRouterApiKey = apiKeyLineEdit->text().trimmed();
	ai.openRouterModel = modelComboBox->currentData().toString();
	ai.openRouterTimeoutSeconds = openRouterTimeoutSpinBox->value();
}

void Prefs_AIServices::showTestResult(const QString& text, const QString& colour)
{
	testResultLabel->setText(QStringLiteral("<qt><b style=\"color:%1;\">%2</b></qt>")
	                         .arg(colour, text.toHtmlEscaped()));
}

void Prefs_AIServices::showOpenRouterResult(const QString& text, const QString& colour)
{
	openRouterTestResultLabel->setText(QStringLiteral("<qt><b style=\"color:%1;\">%2</b></qt>")
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

void Prefs_AIServices::testOpenRouterClicked()
{
	// What is in the box, not what was saved: the reason to press this is to
	// find out whether the key that was just pasted works.
	const QString key = apiKeyLineEdit->text().trimmed();
	if (key.isEmpty())
	{
		showOpenRouterResult(tr("✗ Enter an API key first."), QStringLiteral("#b00020"));
		return;
	}

	const QString model = modelComboBox->currentData().toString();
	const int timeout = openRouterTimeoutSpinBox->value();

	if (!m_openRouterService)
	{
		m_openRouterService = new OpenRouterInpaintService(key, model, timeout, this);
		connect(m_openRouterService, &AIInpaintService::connectionTested,
		        this, &Prefs_AIServices::openRouterTested);
	}
	else
	{
		m_openRouterService->setCredentials(key, model, timeout);
	}

	openRouterTestButton->setEnabled(false);
	// Not "Testing <key> ...". The key does not go on the screen.
	showOpenRouterResult(tr("Checking the key with OpenRouter ..."), QStringLiteral("#666666"));
	m_openRouterService->testConnection();
}

void Prefs_AIServices::openRouterTested(bool ok, const QString& detail)
{
	openRouterTestButton->setEnabled(true);
	if (ok)
		showOpenRouterResult(QStringLiteral("✓ ") + detail, QStringLiteral("#1b7f2a"));
	else
		showOpenRouterResult(QStringLiteral("✗ ") + detail, QStringLiteral("#b00020"));
}
