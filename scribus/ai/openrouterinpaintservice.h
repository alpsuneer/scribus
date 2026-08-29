/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OPENROUTERINPAINTSERVICE_H
#define OPENROUTERINPAINTSERVICE_H

#include <QList>
#include <QString>
#include <QThread>

#include "ai/aiinpaintservice.h"
#include "scribusapi.h"

class OpenRouterInpaintWorker;

/*!
 \brief Inpainting through OpenRouter's unified image API.

 Unlike the LaMa service next door, this one is not a local server and not
 free: it is a paid account on the internet, it needs an API key, and the
 picture goes to OpenRouter and from there to whichever model vendor the user
 picked. Nothing here happens until the user has entered a key in Preferences,
 which is also where they choose the model.

 \section wire The wire format

 Verified against the live API and the current documentation on 29 Aug 2026:
 - https://openrouter.ai/docs/guides/overview/multimodal/image-generation
 - https://openrouter.ai/blog/announcements/image-api/
 - https://openrouter.ai/docs/api-reference/errors
 - and the machine-readable https://openrouter.ai/api/v1/images/models,
   whose \c supported_parameters is what settled the field names below.

 It is worth writing down because it is *not* what a reasonable person would
 guess, and an earlier draft of this work guessed all three of these wrong:

 - <tt>POST https://openrouter.ai/api/v1/images</tt>, JSON body
   <tt>{"model": ..., "prompt": ..., "input_references": [...]}</tt>.
   The reference images key is \c input_references, not \c reference_images,
   and each entry is an object
   <tt>{"type": "image_url", "image_url": {"url": "..."}}</tt> rather than a
   bare string. The URL may be an <tt>http(s)</tt> address or a
   <tt>data:</tt> URL, so the picture goes inline and is never uploaded
   anywhere first.
 - There is no \c modalities field. The endpoint returns images by
   definition; sending one is simply ignored.
 - Success is <tt>200</tt> with
   <tt>{"created": ..., "data": [{"b64_json": ..., "media_type": ...}],
   "usage": {..., "cost": 0.04}}</tt>. \c usage.cost is the real charge in
   USD for that one call, which is the only honest thing to quote a user.
 - Failure is a status code with
   <tt>{"error": {"code": ..., "message": ..., "metadata": {...}}}</tt>.
   For a moderation refusal the metadata carries \c reasons,
   \c provider_name and \c model_slug.

 \section masking How the area to remove is described

 The image models behind this API take a picture and an instruction. None of
 them takes a separate mask channel the way LaMa does, so the mask has to be
 said in the only language they all read: it is painted onto the picture as a
 bright red overlay, and the prompt tells the model that red means "remove
 this and rebuild what was behind it". That is why only one reference image is
 sent rather than an image and a mask.

 \section privacy Privacy

 This sends the user's picture to a third party, which is the whole point and
 also the thing to be careful about. The host is fixed at compile time; there
 is no discovery, no fallback provider and no telemetry. The API key travels
 in the Authorization header and appears nowhere else - not in the URL, not in
 an error message, and there is no logging in this file at all.
 */
class SCRIBUS_API OpenRouterInpaintService : public AIInpaintService
{
	Q_OBJECT

public:
	//! One entry of the curated model list offered in Preferences.
	struct ModelChoice
	{
		QString id;           //!< OpenRouter model id, e.g. openai/gpt-image-2
		QString displayName;  //!< What the dropdown shows
		QString hint;         //!< One line on when to reach for it
	};

	/*! \param apiKey OpenRouter API key. Never logged, never put in a URL.
	    \param model model id from models(), or anything OpenRouter accepts.
	    \param timeoutSeconds how long to wait for a reply.
	    \param apiBase root of the API. Defaults to the real OpenRouter and is
	           a parameter only so the unit tests can point it at a mock
	           server in their own process. Production code must not pass it:
	           AIInpaintServiceFactory never does, so a picture cannot leave
	           for anywhere but openrouter.ai in a running Scribus. */
	explicit OpenRouterInpaintService(const QString& apiKey,
	                                  const QString& model,
	                                  int timeoutSeconds,
	                                  QObject* parent = nullptr,
	                                  const QString& apiBase = defaultApiBase());
	~OpenRouterInpaintService() override;

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

	    Curated rather than fetched: the live list is 48 entries deep, most of
	    which cannot take an input image at all, and a dropdown of 48 is not a
	    choice, it is a puzzle. */
	static const QList<ModelChoice>& models();

	//! Display name for \a modelId, or the id itself if it is not one of ours.
	static QString displayNameFor(const QString& modelId);

	//! Last segment of a model id: "google/gemini-3-pro-image" -> "gemini-3-pro-image".
	static QString shortNameFor(const QString& modelId);

	//! shortNameFor() made safe for a filename: dashes become underscores.
	static QString fileTagFor(const QString& modelId);

	/*! \brief Marker prefix on an inpaintFailed() message that was the model
	    declining the job rather than anything breaking.

	    The caller shows a refusal differently from a failure - it is worth
	    suggesting another model, and worth not implying Scribus went wrong -
	    so the two have to be distinguishable without matching on prose. */
	static QString refusalMarker();
	//! True when \a error carries refusalMarker(). \a error keeps its text.
	static bool isRefusal(const QString& error);
	//! \a error with the marker taken off, ready to show someone.
	static QString strippedRefusal(const QString& error);

private:
	QThread m_thread;
	//! Lives on m_thread and owns the network access manager.
	OpenRouterInpaintWorker* m_worker {nullptr};
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
};

#endif
