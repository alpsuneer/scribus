/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef GEMINITEXTSERVICE_H
#define GEMINITEXTSERVICE_H

#include "ai/aitexthttpservice.h"
#include "scribusapi.h"

/*!
 \brief Text tasks through Google's Gemini API.

 Not the Nano Banana client. GeminiInpaintService next door asks Gemini for a
 *picture*; this asks it for *words*. They share a host, an auth header and an
 account - and nothing else worth merging, which is why they are two files.
 Sharing the account is the point: it is the only one of the three text
 providers a user in India can pay for with UPI, so it is the only one that
 works here without an international card.

 \section wire The wire format

 Verified against the current documentation on 30 Aug 2026:
 - https://ai.google.dev/gemini-api/docs/text-generation
 - https://ai.google.dev/api/interactions-api
 - https://ai.google.dev/gemini-api/docs/models

 This is **not** the shape this feature was specified against, and not the one
 most Gemini examples still show. Google has moved text generation to the same
 Interactions API the image models use, and now labels
 <tt>models/{id}:generateContent</tt> the *Generate Content API (Legacy)*:

 - <tt>POST https://generativelanguage.googleapis.com/v1beta/interactions</tt>
   with <tt>{"model": ..., "input": [ ... ]}</tt>. The model is a field, not a
   path segment, and there is no <tt>:generateContent</tt> suffix.
 - \c input is a flat array of typed parts, not the nested
   <tt>contents[].parts[]</tt> of the legacy API. A picture is
   <tt>{"type":"image","mime_type":"image/jpeg","data":"<base64>"}</tt> - the
   base64 sits directly in \c data with no \c inline_data wrapper.
 - There is no \c generationConfig.responseModalities. A text model returns
   text.
 - The answer is at \c output_text, and also under \c steps[].content[].
 - Failure is a status code with
   <tt>{"error":{"code":...,"message":...,"status":...}}</tt> - which
   <tt>/interactions</tt> wraps in a one-element array, unlike
   <tt>/models</tt>. Both shapes are accepted here.

 \section privacy Privacy

 The user's words and pictures go to Google. The host is fixed at compile time.
 The key travels in the \c x-goog-api-key header and appears nowhere else -
 Google's own docs show <tt>?key=</tt> for some endpoints, which is not used
 here because a URL ends up in error strings and proxy logs.
 */
class SCRIBUS_API GeminiTextService : public AITextHttpService
{
	Q_OBJECT

public:
	explicit GeminiTextService(const QString& apiKey, const QString& model,
	                           int timeoutSeconds, QObject* parent = nullptr,
	                           const QString& apiBase = defaultApiBase());

	static QString defaultApiBase();
	static const QList<AITextProtocol::ModelChoice>& models();
	static QString defaultModel();
	static QString displayNameFor(const QString& modelId);
};

#endif
