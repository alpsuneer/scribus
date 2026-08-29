/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aiinpaintservicefactory.h"

#include <QCoreApplication>

#include "ai/lamainpaintservice.h"
#include "ai/openrouterinpaintservice.h"
#include "prefsstructs.h"

namespace
{
	//! tr() for a namespace with no QObject to hang it on.
	QString translate(const char* text)
	{
		return QCoreApplication::translate("AIInpaintServiceFactory", text);
	}
}

AIProvider AIInpaintServiceFactory::providerOf(const AIServicePrefs& prefs)
{
	// Anything unrecognised - a profile written by a later version, say -
	// falls back to the local one rather than to the one that costs money.
	return prefs.provider == static_cast<int>(AIProvider::OpenRouter)
	     ? AIProvider::OpenRouter
	     : AIProvider::LaMa;
}

QString AIInpaintServiceFactory::blockedReason(const AIServicePrefs& prefs)
{
	if (!prefs.enabled)
		return translate("Enable AI features in Preferences > AI Services");

	switch (providerOf(prefs))
	{
	case AIProvider::OpenRouter:
		if (prefs.openRouterApiKey.trimmed().isEmpty())
			return translate("Enter an OpenRouter API key in Preferences > AI Services");
		if (prefs.openRouterModel.trimmed().isEmpty())
			return translate("Pick an OpenRouter model in Preferences > AI Services");
		return QString();
	case AIProvider::LaMa:
		break;
	}
	if (LamaInpaintService::normaliseBaseUrl(prefs.iopaintUrl).isEmpty())
		return translate("Set the IOPaint address in Preferences > AI Services");
	return QString();
}

std::unique_ptr<AIInpaintService> AIInpaintServiceFactory::create(const AIServicePrefs& prefs, QString& reason)
{
	reason = blockedReason(prefs);
	if (!reason.isEmpty())
		return nullptr;

	switch (providerOf(prefs))
	{
	case AIProvider::OpenRouter:
		// No apiBase argument on purpose. The host is the one compiled into
		// the service; only its own tests ever point it anywhere else.
		return std::make_unique<OpenRouterInpaintService>(prefs.openRouterApiKey.trimmed(),
		                                                  prefs.openRouterModel.trimmed(),
		                                                  prefs.openRouterTimeoutSeconds);
	case AIProvider::LaMa:
		break;
	}
	return std::make_unique<LamaInpaintService>(LamaInpaintService::normaliseBaseUrl(prefs.iopaintUrl),
	                                            prefs.requestTimeoutSeconds);
}

QString AIInpaintServiceFactory::providerTag(const AIServicePrefs& prefs)
{
	switch (providerOf(prefs))
	{
	case AIProvider::OpenRouter:
		// e.g. "openrouter_gemini_3.1_flash_image_preview", so that two
		// removals done with different models are told apart by their
		// filenames alone.
		return QStringLiteral("openrouter_")
		     + OpenRouterInpaintService::fileTagFor(prefs.openRouterModel.trimmed());
	case AIProvider::LaMa:
		break;
	}
	return QStringLiteral("lama");
}

QString AIInpaintServiceFactory::providerDescription(const AIServicePrefs& prefs)
{
	switch (providerOf(prefs))
	{
	case AIProvider::OpenRouter:
		return OpenRouterInpaintService::displayNameFor(prefs.openRouterModel.trimmed());
	case AIProvider::LaMa:
		break;
	}
	return translate("LaMa");
}
