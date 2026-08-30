/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aitextservicefactory.h"

#include <QCoreApplication>

#include "ai/claudetextservice.h"
#include "ai/geminitextservice.h"
#include "ai/openaitextservice.h"
#include "prefsstructs.h"

namespace
{
	//! tr() for a namespace with no QObject to hang it on.
	QString translate(const char* text)
	{
		return QCoreApplication::translate("AITextServiceFactory", text);
	}
}

AITextProvider AITextServiceFactory::providerOf(const AIServicePrefs& prefs)
{
	switch (prefs.textProvider)
	{
	case static_cast<int>(AITextProvider::Claude):
		return AITextProvider::Claude;
	case static_cast<int>(AITextProvider::OpenAI):
		return AITextProvider::OpenAI;
	default:
		break;
	}
	// Anything unrecognised - a profile written by a later version, say - falls
	// back to the one that can actually be paid for here.
	return AITextProvider::Gemini;
}

QString AITextServiceFactory::effectiveKey(const AIServicePrefs& prefs)
{
	if (providerOf(prefs) == AITextProvider::Gemini && prefs.geminiTextUsesImageKey)
	{
		// One Google account, one key, entered once. The checkbox exists
		// because the image and text services are separate objects but the
		// billing behind them is not.
		return prefs.geminiApiKey.trimmed();
	}
	switch (providerOf(prefs))
	{
	case AITextProvider::Claude:
		return prefs.claudeApiKey.trimmed();
	case AITextProvider::OpenAI:
		return prefs.openAITextApiKey.trimmed();
	case AITextProvider::Gemini:
		break;
	}
	return prefs.geminiTextApiKey.trimmed();
}

QString AITextServiceFactory::blockedReason(const AIServicePrefs& prefs)
{
	if (!prefs.enabled)
		return translate("Enable AI features in Preferences > AI Services");

	if (effectiveKey(prefs).isEmpty())
	{
		switch (providerOf(prefs))
		{
		case AITextProvider::Claude:
			return translate("Enter a Claude API key in Preferences > AI Services");
		case AITextProvider::OpenAI:
			return translate("Enter an OpenAI API key in Preferences > AI Services");
		case AITextProvider::Gemini:
			break;
		}
		return translate("Enter a Gemini API key in Preferences > AI Services");
	}

	switch (providerOf(prefs))
	{
	case AITextProvider::Claude:
		if (prefs.claudeModel.trimmed().isEmpty())
			return translate("Pick a Claude model in Preferences > AI Services");
		return QString();
	case AITextProvider::OpenAI:
		if (prefs.openAITextModel.trimmed().isEmpty())
			return translate("Pick an OpenAI model in Preferences > AI Services");
		return QString();
	case AITextProvider::Gemini:
		break;
	}
	if (prefs.geminiTextModel.trimmed().isEmpty())
		return translate("Pick a Gemini model in Preferences > AI Services");
	return QString();
}

std::unique_ptr<AITextService> AITextServiceFactory::create(const AIServicePrefs& prefs, QString& reason)
{
	reason = blockedReason(prefs);
	if (!reason.isEmpty())
		return nullptr;

	// No apiBase argument anywhere below, on purpose. The host is the one
	// compiled into each service; only their own tests ever point them
	// elsewhere, so a running Scribus has exactly one destination per provider.
	const QString key = effectiveKey(prefs);
	switch (providerOf(prefs))
	{
	case AITextProvider::Claude:
		return std::make_unique<ClaudeTextService>(key, prefs.claudeModel.trimmed(),
		                                           prefs.claudeTimeoutSeconds);
	case AITextProvider::OpenAI:
		return std::make_unique<OpenAITextService>(key, prefs.openAITextModel.trimmed(),
		                                           prefs.openAITextTimeoutSeconds);
	case AITextProvider::Gemini:
		break;
	}
	return std::make_unique<GeminiTextService>(key, prefs.geminiTextModel.trimmed(),
	                                           prefs.geminiTextTimeoutSeconds);
}

QString AITextServiceFactory::providerLabel(const AIServicePrefs& prefs)
{
	switch (providerOf(prefs))
	{
	case AITextProvider::Claude:
		return translate("Claude");
	case AITextProvider::OpenAI:
		return translate("OpenAI");
	case AITextProvider::Gemini:
		break;
	}
	return translate("Gemini");
}

QString AITextServiceFactory::providerDescription(const AIServicePrefs& prefs)
{
	switch (providerOf(prefs))
	{
	case AITextProvider::Claude:
		return ClaudeTextService::displayNameFor(prefs.claudeModel.trimmed());
	case AITextProvider::OpenAI:
		return OpenAITextService::displayNameFor(prefs.openAITextModel.trimmed());
	case AITextProvider::Gemini:
		break;
	}
	return GeminiTextService::displayNameFor(prefs.geminiTextModel.trimmed());
}
