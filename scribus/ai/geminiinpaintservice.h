/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef GEMINIINPAINTSERVICE_H
#define GEMINIINPAINTSERVICE_H

#include <QList>
#include <QString>
#include <QThread>

#include "ai/aiinpaintservice.h"
#include "scribusapi.h"

class GeminiInpaintWorker;

/*!
 \brief Inpainting through Google's Gemini API, talking to Google directly.

 The third provider, and the reason it exists is money rather than pictures:
 OpenRouter takes cards, Google AI Studio takes UPI and bills in rupees with
 GST, and a newspaper in Kerala can pay the second one. The models behind it
 are largely the same ones - the Nano Banana family reached OpenRouter through
 Google in the first place - so what this buys is a way to pay, not a different
 result.

 Everything else is deliberately identical to the OpenRouter client next door:
 the same red mask overlay, the same shared prompt, the same JPEG quality, the
 same ROI already cropped by the caller. A user who switches provider because
 of a payment method should not find their removals coming out differently.

 \section wire The wire format

 Verified against the current documentation on 30 Aug 2026:
 - https://ai.google.dev/gemini-api/docs/image-generation
 - https://ai.google.dev/api/interactions-api
 - https://ai.google.dev/gemini-api/docs/models

 The shape below is **not** the one most Gemini examples on the internet show,
 and it is not the one this task was specified against. Google has moved image
 generation to the Interactions API and the older
 <tt>models/{id}:generateContent</tt> endpoint is now labelled *Generate
 Content API (Legacy)* in their own documentation. What is implemented here is
 what the image-generation guide currently documents:

 - <tt>POST https://generativelanguage.googleapis.com/v1beta/interactions</tt>
   with <tt>{"model": ..., "input": [ ... ]}</tt>. There is no model id in the
   path and no <tt>:generateContent</tt> suffix; the model is a field.
 - \c input is a flat array of typed parts, not the nested
   <tt>contents[].parts[]</tt> of the legacy API. A picture is
   <tt>{"type": "image", "mime_type": "image/jpeg", "data": "<base64>"}</tt>
   and an instruction is <tt>{"type": "text", "text": "..."}</tt>. The field is
   \c mime_type in snake_case, and the base64 sits directly in \c data - there
   is no \c inline_data wrapper.
 - There is no \c generationConfig.responseModalities. An image model returns
   an image by default; \c response_format exists but is only needed to ask for
   a particular aspect ratio or size, which this does not.
 - Success is <tt>200</tt> with the picture at <tt>output_image.data</tt>,
   base64, and its type at \c output_image.mime_type. The same content is also
   reachable through <tt>steps[].content[]</tt>, which is where a model that
   answered with words instead puts them, and \c output_text carries any
   commentary.
 - Failure is a status code with
   <tt>{"error": {"code": ..., "message": ..., "status": ...}}</tt>.

 \section privacy Privacy

 This sends the user's picture to Google. The host is fixed at compile time;
 there is no discovery, no fallback provider and no telemetry. The API key
 travels in the \c x-goog-api-key header and appears nowhere else - not in the
 URL, not in an error message, and there is no logging in this file at all.
 Google's own documentation shows the key as a <tt>?key=</tt> query parameter
 for some endpoints; it is not used that way here, because a URL ends up in
 error strings and proxy logs and a header does not.
 */
class SCRIBUS_API GeminiInpaintService : public AIInpaintService
{
	Q_OBJECT

public:
	//! One entry of the curated model list offered in Preferences.
	struct ModelChoice
	{
		QString id;           //!< Gemini model id, e.g. gemini-3.1-flash-image
		QString displayName;  //!< What the dropdown shows
		QString hint;         //!< One line on when to reach for it
	};

	/*! \param apiKey Google AI Studio API key. Never logged, never in a URL.
	    \param model model id from models(), or anything Gemini accepts.
	    \param timeoutSeconds how long to wait for a reply.
	    \param apiBase root of the API. Defaults to the real Google endpoint
	           and is a parameter only so the unit tests can point it at a mock
	           server in their own process. Production code must not pass it:
	           AIInpaintServiceFactory never does, so a picture cannot leave
	           for anywhere but generativelanguage.googleapis.com in a running
	           Scribus. */
	explicit GeminiInpaintService(const QString& apiKey,
	                              const QString& model,
	                              int timeoutSeconds,
	                              QObject* parent = nullptr,
	                              const QString& apiBase = defaultApiBase());
	~GeminiInpaintService() override;

	QString name() const override;
	void testConnection() override;
	void inpaint(const QImage& image, const QImage& mask) override;
	void cancel() override;

	//! Change key, model and timeout without tearing the service down.
	void setCredentials(const QString& apiKey, const QString& model, int timeoutSeconds);

	QString model() const { return m_model; }

	//! The real API root. Not configurable by the user, on purpose.
	static QString defaultApiBase();

	/*! \brief The models offered in Preferences.

	    Curated rather than fetched: /v1beta/models answers with everything the
	    key can reach, which is dozens of text and embedding models that cannot
	    return a picture at all. */
	static const QList<ModelChoice>& models();

	//! The one a fresh profile starts on.
	static QString defaultModel();

	//! Display name for \a modelId, or the id itself if it is not one of ours.
	static QString displayNameFor(const QString& modelId);

	//! Last path segment of a model id, with any "-preview" suffix removed.
	static QString shortNameFor(const QString& modelId);

	//! shortNameFor() made safe for a filename: dashes become underscores.
	static QString fileTagFor(const QString& modelId);

private:
	QThread m_thread;
	//! Lives on m_thread and owns the network access manager.
	GeminiInpaintWorker* m_worker {nullptr};
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
};

#endif
