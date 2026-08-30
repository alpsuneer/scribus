/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AIINPAINTSERVICEFACTORY_H
#define AIINPAINTSERVICEFACTORY_H

#include <memory>

#include <QString>

#include "scribusapi.h"

class AIInpaintService;
struct AIServicePrefs;

//! Which implementation "Apply (Best Quality)" goes to. Stored in the
//! preferences as an int, so the numbers here are on the file format and must
//! not be reordered.
enum class AIProvider
{
	LaMa = 0,        //!< A local IOPaint server. Free, private, no account.
	OpenRouter = 1,  //!< A paid cloud API. Costs money, sends the picture out.
	Gemini = 2       //!< Google directly. Costs money, and can be paid by UPI.
};

/*!
 \brief Builds the inpainting service the preferences ask for.

 One place that knows how a preference turns into a service, so that adding a
 third provider is a change here and nowhere else. The callers hold an
 AIInpaintService and never name an implementation.

 The OpenRouter service is always built against its own default host. The
 address is not user-configurable and the factory never overrides it, which is
 what makes "a picture only ever goes to openrouter.ai" a property of the
 program rather than of a setting.
 */
namespace AIInpaintServiceFactory
{
	//! The provider \a prefs selects, clamped to one that exists.
	SCRIBUS_API AIProvider providerOf(const AIServicePrefs& prefs);

	/*! \brief A service for the configured provider, or nullptr if it is not
	    configured well enough to build one (no address, no key).

	    \param reason set to a sentence saying what is missing when the result
	           is nullptr. */
	SCRIBUS_API std::unique_ptr<AIInpaintService> create(const AIServicePrefs& prefs, QString& reason);

	//! Why the configured provider cannot run yet, or empty if it can. Same
	//! checks as create(), without building anything.
	SCRIBUS_API QString blockedReason(const AIServicePrefs& prefs);

	//! Short name of the configured provider, for undo entries and filenames.
	SCRIBUS_API QString providerTag(const AIServicePrefs& prefs);

	//! What the user should be told is doing the work, e.g. "Nano Banana 2".
	SCRIBUS_API QString providerDescription(const AIServicePrefs& prefs);

	/*! \brief What to call the service itself, e.g. "OpenRouter" or "Gemini".

	    Separate from providerDescription(), which names the model. Both appear
	    together - "Remove Object (Gemini: Nano Banana 2)" - because the same
	    model can be reached through either service and the undo history is the
	    only place that record survives. Empty for the local provider, which
	    has nothing worth naming twice. */
	SCRIBUS_API QString providerLabel(const AIServicePrefs& prefs);

	/*! \brief Whether the configured provider sends the picture off this
	    machine, which is what changes both the wait and the bill. */
	SCRIBUS_API bool providerIsCloud(const AIServicePrefs& prefs);
}

#endif
