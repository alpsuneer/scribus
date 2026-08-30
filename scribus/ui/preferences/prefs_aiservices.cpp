/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QString>

#include "prefs_aiservices.h"

#include "ai/aiinpaintservicefactory.h"
#include "ai/aitextservice.h"
#include "ai/aitextservicefactory.h"
#include "ai/claudetextservice.h"
#include "ai/geminitextservice.h"
#include "ai/openaitextservice.h"
#include "ai/geminiinpaintservice.h"
#include "ai/lamainpaintservice.h"
#include "ai/openrouterinpaintservice.h"
#include "prefsstructs.h"
#include "scribusdoc.h"

Prefs_AIServices::Prefs_AIServices(QWidget* parent, ScribusDoc* /*doc*/)
	: Prefs_Pane(parent)
{
	setupUi(this);

	// The id is carried as item data rather than inferred from the position:
	// AIProvider's numbers are on the preferences file format, and tying them
	// to the order of a dropdown would make reordering the dropdown a silent
	// change of what every existing profile means.
	providerComboBox->addItem(tr("LaMa (local, free)"), static_cast<int>(AIProvider::LaMa));
	providerComboBox->addItem(tr("OpenRouter (cloud, paid)"), static_cast<int>(AIProvider::OpenRouter));
	// "UPI" is in the label rather than only in the note below, because for a
	// user in India it is the whole reason this entry exists and it should be
	// visible before the dropdown is opened.
	providerComboBox->addItem(tr("Google Gemini (direct, UPI)"), static_cast<int>(AIProvider::Gemini));

	const QList<OpenRouterInpaintService::ModelChoice>& models = OpenRouterInpaintService::models();
	for (const OpenRouterInpaintService::ModelChoice& choice : models)
		modelComboBox->addItem(choice.displayName, choice.id);

	const QList<GeminiInpaintService::ModelChoice>& geminiModels = GeminiInpaintService::models();
	for (const GeminiInpaintService::ModelChoice& choice : geminiModels)
		geminiModelComboBox->addItem(choice.displayName, choice.id);

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
	connect(geminiModelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &Prefs_AIServices::geminiModelChanged);
	connect(geminiShowKeyCheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::geminiShowKeyToggled);
	connect(geminiTestButton, &QPushButton::clicked, this, &Prefs_AIServices::testGeminiClicked);

	// Gemini first, and labelled: it is the only one of the three that UPI can
	// pay for, which for this office is the difference between a feature that
	// can be switched on and one that cannot.
	textProviderComboBox->addItem(tr("Google Gemini text (UPI works)"),
	                              static_cast<int>(AITextProvider::Gemini));
	textProviderComboBox->addItem(tr("Anthropic Claude (international card needed)"),
	                              static_cast<int>(AITextProvider::Claude));
	textProviderComboBox->addItem(tr("OpenAI GPT (international card needed)"),
	                              static_cast<int>(AITextProvider::OpenAI));

	connect(textProviderComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &Prefs_AIServices::textProviderChanged);
	connect(textModelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &Prefs_AIServices::textModelChanged);
	connect(textShowKeyCheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::textShowKeyToggled);
	connect(textSameKeyCheckBox, &QCheckBox::toggled, this, &Prefs_AIServices::textSameKeyToggled);
	connect(textTestButton, &QPushButton::clicked, this, &Prefs_AIServices::testTextClicked);

	enabledToggled(enableAICheckBox->isChecked());
	providerChanged(providerComboBox->currentIndex());
}

Prefs_AIServices::~Prefs_AIServices() = default;

void Prefs_AIServices::languageChange()
{
	enableAICheckBox->setToolTip("<qt>" + tr("Turn on the optional AI-assisted features. "
		"They are off until you turn them on, and they only ever contact the service chosen below.") + "</qt>");
	providerComboBox->setToolTip("<qt>" + tr("Which service does the work for Apply (Best Quality). "
		"LaMa runs on this machine and is free. OpenRouter and Google Gemini are paid accounts "
		"on the internet, and your picture is sent to them. Gemini can be paid for by UPI.") + "</qt>");
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

	geminiApiKeyLineEdit->setToolTip("<qt>" + tr("Your Google AI Studio API key. It is stored in your preferences file "
		"only lightly obfuscated, so treat that file as you would the key itself.") + "</qt>");
	geminiShowKeyCheckBox->setToolTip("<qt>" + tr("Show the key, to check it was pasted correctly.") + "</qt>");
	geminiModelComboBox->setToolTip("<qt>" + tr("Which image model does the work. You can change this between "
		"removals without re-entering the key - useful when one model refuses an edit, or to compare results.") + "</qt>");
	geminiTimeoutSpinBox->setToolTip("<qt>" + tr("How long to wait for Gemini to answer before giving up.") + "</qt>");
	geminiTestButton->setToolTip("<qt>" + tr("Check that the key above is valid. This lists the models the key "
		"can reach, which runs no model and so costs nothing.") + "</qt>");

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

	// The same four facts as OpenRouter's note, in the same order, plus the one
	// that is the actual reason to choose this provider: it can be paid for
	// from India without a card that works internationally.
	geminiInfoLabel->setText("<qt><i>"
		+ tr("Requires a Google AI Studio API key with billing enabled.") + "<br/>"
		+ tr("Get a key: %1")
		  .arg("<a href=\"https://aistudio.google.com\">https://aistudio.google.com</a>") + "<br/>"
		+ tr("India: pay via UPI at Google AI Studio billing (INR billing, GST included).") + "<br/>"
		+ tr("Each operation costs approximately $0.05-0.15 (Rs 4-13). "
		     "Your picture is sent to Google's servers.")
		+ "</i></qt>");

	textProviderComboBox->setToolTip("<qt>" + tr("Which service answers the AI Text Tools menu. "
		"This is a separate choice from the image provider above - you can use a local image model "
		"and a cloud text one at the same time.") + "</qt>");
	textApiKeyLineEdit->setToolTip("<qt>" + tr("Your API key for the selected text provider. "
		"It is stored in your preferences file only lightly obfuscated, so treat that file as you "
		"would the key itself.") + "</qt>");
	textSameKeyCheckBox->setToolTip("<qt>" + tr("One Google account bills both the image and the "
		"text service, so the same key works for both.") + "</qt>");
	textTestButton->setToolTip("<qt>" + tr("Check that the key above is valid. This lists the "
		"models the key can reach, which runs no model and so costs nothing.") + "</qt>");

	textInfoLabel->setText("<qt><i>"
		+ tr("Text AI enables caption generation, headline suggestions, translation, "
		     "summaries and alt text from the Item &gt; AI Text Tools menu.") + "<br/>"
		+ tr("Payment: Gemini accepts UPI in India. Claude and OpenAI require an "
		     "international card at this time.") + "<br/>"
		+ tr("Your text - and, for captions and alt text, your picture - is sent to the "
		     "provider you choose here.")
		+ "</i></qt>");

	modelChanged(modelComboBox->currentIndex());
	geminiModelChanged(geminiModelComboBox->currentIndex());
	textModelChanged(textModelComboBox->currentIndex());
}

void Prefs_AIServices::enabledToggled(bool on)
{
	providerLabel->setEnabled(on);
	providerComboBox->setEnabled(on);
	iopaintGroupBox->setEnabled(on);
	openRouterGroupBox->setEnabled(on);
	geminiGroupBox->setEnabled(on);
	textAiGroupBox->setEnabled(on);
}

void Prefs_AIServices::providerChanged(int index)
{
	// Read the id out of the item rather than trusting the position, so that
	// the order of the dropdown and the order of the stored enum are allowed
	// to be different things.
	const int provider = providerComboBox->itemData(index).toInt();
	const bool lama = (provider == static_cast<int>(AIProvider::LaMa));
	// Hidden rather than greyed: the settings for the providers that are not
	// selected are not "unavailable", they are irrelevant, and a page with all
	// three on it invites filling in more than one.
	iopaintGroupBox->setVisible(lama);
	infoLabel->setVisible(lama);
	openRouterGroupBox->setVisible(provider == static_cast<int>(AIProvider::OpenRouter));
	geminiGroupBox->setVisible(provider == static_cast<int>(AIProvider::Gemini));
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

void Prefs_AIServices::geminiModelChanged(int index)
{
	const QList<GeminiInpaintService::ModelChoice>& models = GeminiInpaintService::models();
	if (index < 0 || index >= models.size())
	{
		geminiModelHintLabel->clear();
		return;
	}
	geminiModelHintLabel->setText(QStringLiteral("<qt><i>%1</i></qt>")
	                              .arg(models.at(index).hint.toHtmlEscaped()));
}

void Prefs_AIServices::geminiShowKeyToggled(bool on)
{
	geminiApiKeyLineEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
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

	geminiApiKeyLineEdit->setText(ai.geminiApiKey);
	geminiShowKeyCheckBox->setChecked(false);
	geminiShowKeyToggled(false);
	geminiTimeoutSpinBox->setValue(ai.geminiTimeoutSeconds);

	// Same treatment as the OpenRouter model above, and for the same reason.
	int geminiIndex = geminiModelComboBox->findData(ai.geminiModel);
	if (geminiIndex < 0 && !ai.geminiModel.isEmpty())
	{
		geminiModelComboBox->addItem(ai.geminiModel, ai.geminiModel);
		geminiIndex = geminiModelComboBox->count() - 1;
	}
	geminiModelComboBox->setCurrentIndex(qMax(0, geminiIndex));

	m_textKeys[static_cast<int>(AITextProvider::Gemini)] = ai.geminiTextApiKey;
	m_textKeys[static_cast<int>(AITextProvider::Claude)] = ai.claudeApiKey;
	m_textKeys[static_cast<int>(AITextProvider::OpenAI)] = ai.openAITextApiKey;
	m_textModels[static_cast<int>(AITextProvider::Gemini)] = ai.geminiTextModel;
	m_textModels[static_cast<int>(AITextProvider::Claude)] = ai.claudeModel;
	m_textModels[static_cast<int>(AITextProvider::OpenAI)] = ai.openAITextModel;
	m_textTimeouts[static_cast<int>(AITextProvider::Gemini)] = ai.geminiTextTimeoutSeconds;
	m_textTimeouts[static_cast<int>(AITextProvider::Claude)] = ai.claudeTimeoutSeconds;
	m_textTimeouts[static_cast<int>(AITextProvider::OpenAI)] = ai.openAITextTimeoutSeconds;
	m_geminiTextUsesImageKey = ai.geminiTextUsesImageKey;

	m_currentTextProvider = static_cast<int>(AITextServiceFactory::providerOf(ai));
	{
		const QSignalBlocker blockProvider(textProviderComboBox);
		const int textIndex = textProviderComboBox->findData(m_currentTextProvider);
		textProviderComboBox->setCurrentIndex(qMax(0, textIndex));
	}
	loadTextFields(m_currentTextProvider);

	enabledToggled(ai.enabled);
	providerChanged(providerComboBox->currentIndex());
	testResultLabel->clear();
	openRouterTestResultLabel->clear();
	geminiTestResultLabel->clear();
	textTestResultLabel->clear();
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
	ai.geminiApiKey = geminiApiKeyLineEdit->text().trimmed();
	ai.geminiModel = geminiModelComboBox->currentData().toString();
	ai.geminiTimeoutSeconds = geminiTimeoutSpinBox->value();

	/* The text section edits one provider at a time through one set of
	   widgets, so what is on screen is banked first and then all three are
	   written. Writing only the visible one would wipe the other two every
	   time the page was opened. saveGuiToPrefs() is const, so the stash is
	   done on local copies rather than on the members. */
	QString keys[3] = { m_textKeys[0], m_textKeys[1], m_textKeys[2] };
	QString textModels[3] = { m_textModels[0], m_textModels[1], m_textModels[2] };
	int timeouts[3] = { m_textTimeouts[0], m_textTimeouts[1], m_textTimeouts[2] };
	bool sameKey = m_geminiTextUsesImageKey;
	if (m_currentTextProvider >= 0 && m_currentTextProvider <= 2)
	{
		keys[m_currentTextProvider] = textApiKeyLineEdit->text().trimmed();
		textModels[m_currentTextProvider] = textModelComboBox->currentData().toString();
		timeouts[m_currentTextProvider] = textTimeoutSpinBox->value();
		if (m_currentTextProvider == static_cast<int>(AITextProvider::Gemini))
			sameKey = textSameKeyCheckBox->isChecked();
	}

	ai.textProvider = textProviderComboBox->currentData().toInt();
	ai.geminiTextApiKey = keys[static_cast<int>(AITextProvider::Gemini)];
	ai.claudeApiKey = keys[static_cast<int>(AITextProvider::Claude)];
	ai.openAITextApiKey = keys[static_cast<int>(AITextProvider::OpenAI)];
	ai.geminiTextModel = textModels[static_cast<int>(AITextProvider::Gemini)];
	ai.claudeModel = textModels[static_cast<int>(AITextProvider::Claude)];
	ai.openAITextModel = textModels[static_cast<int>(AITextProvider::OpenAI)];
	ai.geminiTextTimeoutSeconds = timeouts[static_cast<int>(AITextProvider::Gemini)];
	ai.claudeTimeoutSeconds = timeouts[static_cast<int>(AITextProvider::Claude)];
	ai.openAITextTimeoutSeconds = timeouts[static_cast<int>(AITextProvider::OpenAI)];
	ai.geminiTextUsesImageKey = sameKey;
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

void Prefs_AIServices::showGeminiResult(const QString& text, const QString& colour)
{
	geminiTestResultLabel->setText(QStringLiteral("<qt><b style=\"color:%1;\">%2</b></qt>")
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

void Prefs_AIServices::testGeminiClicked()
{
	// What is in the box, not what was saved: the reason to press this is to
	// find out whether the key that was just pasted works.
	const QString key = geminiApiKeyLineEdit->text().trimmed();
	if (key.isEmpty())
	{
		showGeminiResult(tr("\u2717 Enter an API key first."), QStringLiteral("#b00020"));
		return;
	}

	const QString model = geminiModelComboBox->currentData().toString();
	const int timeout = geminiTimeoutSpinBox->value();

	if (!m_geminiService)
	{
		m_geminiService = new GeminiInpaintService(key, model, timeout, this);
		connect(m_geminiService, &AIInpaintService::connectionTested,
		        this, &Prefs_AIServices::geminiTested);
	}
	else
	{
		m_geminiService->setCredentials(key, model, timeout);
	}

	geminiTestButton->setEnabled(false);
	// Not "Testing <key> ...". The key does not go on the screen.
	showGeminiResult(tr("Checking the key with Google ..."), QStringLiteral("#666666"));
	m_geminiService->testConnection();
}

void Prefs_AIServices::geminiTested(bool ok, const QString& detail)
{
	geminiTestButton->setEnabled(true);
	if (ok)
		showGeminiResult(QStringLiteral("\u2713 ") + detail, QStringLiteral("#1b7f2a"));
	else
		showGeminiResult(QStringLiteral("\u2717 ") + detail, QStringLiteral("#b00020"));
}

// --- the text section -------------------------------------------------------

void Prefs_AIServices::stashTextFields(int provider)
{
	if (provider < 0 || provider > 2)
		return;
	m_textKeys[provider] = textApiKeyLineEdit->text().trimmed();
	m_textModels[provider] = textModelComboBox->currentData().toString();
	m_textTimeouts[provider] = textTimeoutSpinBox->value();
	if (provider == static_cast<int>(AITextProvider::Gemini))
		m_geminiTextUsesImageKey = textSameKeyCheckBox->isChecked();
}

void Prefs_AIServices::loadTextFields(int provider)
{
	if (provider < 0 || provider > 2)
		return;

	// The model list belongs to the provider, so it is rebuilt rather than
	// filtered: an id from one provider is meaningless to another.
	const QList<AITextProtocol::ModelChoice>* models = nullptr;
	switch (static_cast<AITextProvider>(provider))
	{
	case AITextProvider::Claude:
		models = &ClaudeTextService::models();
		break;
	case AITextProvider::OpenAI:
		models = &OpenAITextService::models();
		break;
	case AITextProvider::Gemini:
		models = &GeminiTextService::models();
		break;
	}

	const QSignalBlocker blockModel(textModelComboBox);
	textModelComboBox->clear();
	for (const AITextProtocol::ModelChoice& choice : *models)
		textModelComboBox->addItem(choice.displayName, choice.id);

	// A model the list no longer offers - an old profile, or one edited by
	// hand - is added rather than silently swapped, so that saving the page
	// does not quietly change which model gets used.
	int index = textModelComboBox->findData(m_textModels[provider]);
	if (index < 0 && !m_textModels[provider].isEmpty())
	{
		textModelComboBox->addItem(m_textModels[provider], m_textModels[provider]);
		index = textModelComboBox->count() - 1;
	}
	textModelComboBox->setCurrentIndex(qMax(0, index));

	textApiKeyLineEdit->setText(m_textKeys[provider]);
	textTimeoutSpinBox->setValue(m_textTimeouts[provider]);

	// The shared-key checkbox only means anything for Gemini, which is the one
	// provider that also exists on the image side.
	const bool isGemini = (provider == static_cast<int>(AITextProvider::Gemini));
	textSameKeyCheckBox->setVisible(isGemini);
	{
		const QSignalBlocker blockSame(textSameKeyCheckBox);
		textSameKeyCheckBox->setChecked(isGemini && m_geminiTextUsesImageKey);
	}
	textShowKeyCheckBox->setChecked(false);
	textShowKeyToggled(false);
	textSameKeyToggled(textSameKeyCheckBox->isChecked() && isGemini);
	textModelChanged(textModelComboBox->currentIndex());
	textTestResultLabel->clear();
}

void Prefs_AIServices::textProviderChanged(int index)
{
	const int provider = textProviderComboBox->itemData(index).toInt();
	if (provider == m_currentTextProvider)
		return;
	// Bank what is on screen before replacing it, or switching the dropdown to
	// read a hint would silently discard a freshly pasted key.
	stashTextFields(m_currentTextProvider);
	m_currentTextProvider = provider;
	loadTextFields(provider);
}

void Prefs_AIServices::textModelChanged(int index)
{
	const QList<AITextProtocol::ModelChoice>* models = nullptr;
	switch (static_cast<AITextProvider>(m_currentTextProvider))
	{
	case AITextProvider::Claude:
		models = &ClaudeTextService::models();
		break;
	case AITextProvider::OpenAI:
		models = &OpenAITextService::models();
		break;
	case AITextProvider::Gemini:
		models = &GeminiTextService::models();
		break;
	}
	if (!models || index < 0 || index >= models->size())
	{
		textModelHintLabel->clear();
		return;
	}
	textModelHintLabel->setText(QStringLiteral("<qt><i>%1</i></qt>")
	                            .arg(models->at(index).hint.toHtmlEscaped()));
}

void Prefs_AIServices::textShowKeyToggled(bool on)
{
	textApiKeyLineEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
}

void Prefs_AIServices::textSameKeyToggled(bool on)
{
	// Greyed rather than hidden: the user should be able to see that a key is
	// in use and where it is coming from, without being able to edit a copy of
	// it that would then be ignored.
	textApiKeyLineEdit->setEnabled(!on);
	textShowKeyCheckBox->setEnabled(!on);
	if (on)
		textApiKeyLineEdit->setText(apiKeyLineEdit->text().isEmpty()
		                            ? QString() : geminiApiKeyLineEdit->text());
}

void Prefs_AIServices::testTextClicked()
{
	// What is in the boxes, not what was saved: the reason to press this is to
	// find out whether the key that was just pasted works.
	const bool sameKey = textSameKeyCheckBox->isVisible() && textSameKeyCheckBox->isChecked();
	const QString key = sameKey ? geminiApiKeyLineEdit->text().trimmed()
	                            : textApiKeyLineEdit->text().trimmed();
	if (key.isEmpty())
	{
		textTestResultLabel->setText(QStringLiteral("<qt><b style=\"color:#b00020;\">%1</b></qt>")
		                             .arg(tr("\u2717 Enter an API key first.")));
		return;
	}

	const QString model = textModelComboBox->currentData().toString();
	const int timeout = textTimeoutSpinBox->value();

	// Rebuilt rather than reconfigured: switching provider changes the class,
	// not just the credentials.
	delete m_textService;
	switch (static_cast<AITextProvider>(m_currentTextProvider))
	{
	case AITextProvider::Claude:
		m_textService = new ClaudeTextService(key, model, timeout, this);
		break;
	case AITextProvider::OpenAI:
		m_textService = new OpenAITextService(key, model, timeout, this);
		break;
	case AITextProvider::Gemini:
		m_textService = new GeminiTextService(key, model, timeout, this);
		break;
	}
	connect(m_textService, &AITextService::connectionTested, this, &Prefs_AIServices::textTested);

	textTestButton->setEnabled(false);
	// Not "Testing <key> ...". The key does not go on the screen.
	textTestResultLabel->setText(QStringLiteral("<qt><b style=\"color:#666666;\">%1</b></qt>")
	                             .arg(tr("Checking the key ...")));
	m_textService->testConnection();
}

void Prefs_AIServices::textTested(bool ok, const QString& detail)
{
	textTestButton->setEnabled(true);
	textTestResultLabel->setText(QStringLiteral("<qt><b style=\"color:%1;\">%2 %3</b></qt>")
	                             .arg(ok ? QStringLiteral("#1b7f2a") : QStringLiteral("#b00020"),
	                                  ok ? QStringLiteral("\u2713") : QStringLiteral("\u2717"),
	                                  detail.toHtmlEscaped()));
}
