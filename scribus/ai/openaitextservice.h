/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OPENAITEXTSERVICE_H
#define OPENAITEXTSERVICE_H

#include "ai/aitexthttpservice.h"
#include "scribusapi.h"

/*!
 \brief Text tasks through OpenAI's Chat Completions API.

 \section wire The wire format

 Verified against the current documentation on 30 Aug 2026:
 - https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create
 - https://developers.openai.com/api/docs/guides/images-vision
 - https://developers.openai.com/api/docs/models

 - <tt>POST https://api.openai.com/v1/chat/completions</tt> with
   <tt>Authorization: Bearer &lt;key&gt;</tt>.
 - A picture is a content part
   <tt>{"type":"image_url","image_url":{"url":"data:image/jpeg;base64,..."}}</tt>
   - a complete data: URL rather than bare base64, which is what makes this
   shape different from the other two providers'.
 - The output cap is \c max_completion_tokens, **not** \c max_tokens.
   \c max_tokens is the older spelling and does not account for reasoning
   tokens; new code is meant to send the former.
 - Success is <tt>200</tt> with the answer at
   \c choices[0].message.content, and token counts at
   \c usage.prompt_tokens / \c usage.completion_tokens.
 - Failure is a status code with
   <tt>{"error":{"message":...,"type":...,"code":...}}</tt>.

 \section privacy Privacy

 The user's words and pictures go to OpenAI. The host is fixed at compile time.
 The key travels in the Authorization header and appears nowhere else.
 */
class SCRIBUS_API OpenAITextService : public AITextHttpService
{
	Q_OBJECT

public:
	explicit OpenAITextService(const QString& apiKey, const QString& model,
	                           int timeoutSeconds, QObject* parent = nullptr,
	                           const QString& apiBase = defaultApiBase());

	static QString defaultApiBase();
	static const QList<AITextProtocol::ModelChoice>& models();
	static QString defaultModel();
	static QString displayNameFor(const QString& modelId);
};

#endif
