/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTSERVICEFACTORY_H
#define AITEXTSERVICEFACTORY_H

#include <memory>

#include <QString>

#include "scribusapi.h"

class AITextService;
struct AIServicePrefs;

/*! \brief Which service answers the AI Text Tools menu.

    Stored in the preferences as an int, so the numbers here are on the file
    format and must not be reordered.

    Gemini is 0, and therefore what a profile with no such setting gets. That is
    not alphabetical: it is the only one of the three that can be paid for from
    India with UPI, so it is the only one a user here can actually turn on
    without first obtaining an international card. */
enum class AITextProvider
{
	Gemini = 0,   //!< Google, and the only one UPI can pay for
	Claude = 1,   //!< Anthropic
	OpenAI = 2    //!< OpenAI
};

/*!
 \brief Builds the text service the preferences ask for.

 One place that knows how a preference turns into a service, so the callers
 hold an AITextService and never name an implementation.

 Deliberately separate from AIInpaintServiceFactory: the two answer different
 questions, are configured independently, and a user may well want a local
 image model and a cloud text one at the same time. Each service is always
 built against its own compiled-in host; the factory never overrides it, which
 is what makes "the user's words only ever go to the one service they picked" a
 property of the program rather than of a setting.
 */
namespace AITextServiceFactory
{
	//! The provider \a prefs selects, clamped to one that exists.
	SCRIBUS_API AITextProvider providerOf(const AIServicePrefs& prefs);

	/*! \brief A service for the configured provider, or nullptr if it is not
	    configured well enough to build one.

	    \param reason set to a sentence saying what is missing when the result
	           is nullptr. */
	SCRIBUS_API std::unique_ptr<AITextService> create(const AIServicePrefs& prefs, QString& reason);

	//! Why the configured provider cannot run yet, or empty if it can.
	SCRIBUS_API QString blockedReason(const AIServicePrefs& prefs);

	//! What to call the service itself, e.g. "Claude".
	SCRIBUS_API QString providerLabel(const AIServicePrefs& prefs);

	//! What the user should be told is doing the work, e.g. "Claude Sonnet 5".
	SCRIBUS_API QString providerDescription(const AIServicePrefs& prefs);

	//! The key the configured provider should use, honouring the
	//! "same key as the image service" checkbox for Gemini.
	SCRIBUS_API QString effectiveKey(const AIServicePrefs& prefs);
}

#endif
