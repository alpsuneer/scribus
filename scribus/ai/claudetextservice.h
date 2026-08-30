/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CLAUDETEXTSERVICE_H
#define CLAUDETEXTSERVICE_H

#include "ai/aitexthttpservice.h"
#include "scribusapi.h"

/*!
 \brief Text tasks through Anthropic's Messages API.

 \section wire The wire format

 Verified against the current documentation on 30 Aug 2026:
 - https://platform.claude.com/docs/en/api/messages/create
 - https://platform.claude.com/docs/en/build-with-claude/vision
 - https://platform.claude.com/docs/en/api/models-list

 - <tt>POST https://api.anthropic.com/v1/messages</tt>, with both
   <tt>x-api-key</tt> and <tt>anthropic-version: 2023-06-01</tt>. The version
   header is not optional; without it the request is rejected.
 - A picture is a content block
   <tt>{"type":"image","source":{"type":"base64","media_type":...,"data":...}}</tt>
   - note the nested \c source, which is what distinguishes this from the other
   two providers' flatter shapes.
 - The picture goes **before** the text. Anthropic's own guidance is that
   Claude works best with images first, and that is worth following even though
   either order is accepted.
 - Success is <tt>200</tt> with a \c content array; the answer is the \c text of
   the blocks whose type is \c text. Token counts are at
   \c usage.input_tokens / \c usage.output_tokens.
 - Failure is a status code with
   <tt>{"type":"error","error":{"type":...,"message":...}}</tt>.

 \section privacy Privacy

 The user's words and pictures go to Anthropic. The host is fixed at compile
 time; there is no discovery, no fallback provider and no telemetry. The key
 travels in the \c x-api-key header and appears nowhere else - not in the URL,
 not in an error message, and there is no logging in this file at all.
 */
class SCRIBUS_API ClaudeTextService : public AITextHttpService
{
	Q_OBJECT

public:
	explicit ClaudeTextService(const QString& apiKey, const QString& model,
	                           int timeoutSeconds, QObject* parent = nullptr,
	                           const QString& apiBase = defaultApiBase());

	//! The real API root. Not configurable by the user, on purpose; only the
	//! unit tests ever pass anything else, and the factory never does.
	static QString defaultApiBase();
	static const QList<AITextProtocol::ModelChoice>& models();
	static QString defaultModel();
	static QString displayNameFor(const QString& modelId);
};

#endif
