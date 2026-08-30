/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/geminiinpaintservice.h"

#include "ai/aiinpaintcomposite.h"
#include "ai/aiinpaintprompts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace
{
	//! Where an edit is asked for. Note there is no model in the path: the
	//! Interactions API takes it as a field. See the wire note in the header.
	const QLatin1String InteractionsPath("/interactions");
	/*! \brief Key check.

	    Listing models is the cheapest call that proves a key: it is a plain
	    GET, it runs no model, it generates no tokens and so it costs nothing,
	    and it fails with 401 for a bad key and 403 for a key whose project has
	    no billing - which are the two things the button is being pressed to
	    find out. Asking a model to produce a one-pixel image would also work
	    and would be billed every time somebody pressed the button. */
	const QLatin1String ModelsPath("/models");

	//! The key goes in this header and nowhere else.
	const char* const ApiKeyHeader = "x-goog-api-key";

	/*! \brief How long to keep collecting an error body once the response head
	    has already shown an error status.

	    Same reasoning as the OpenRouter client: the status alone is enough to
	    report every case a user can act on, so this is only a short window to
	    pick up the message that usually comes with it, and it is well under
	    the two seconds a rejection is expected to surface in. */
	const int ErrorBodyGraceMs = 1200;

	/*! \brief The body as an object, unwrapping the array Google sometimes
	    puts around it.

	    Observed against the live API on 30 Aug 2026: a bad key on
	    <tt>GET /v1beta/models</tt> answers with a bare object, and the same bad
	    key on <tt>POST /v1beta/interactions</tt> answers with the identical
	    envelope wrapped in a one-element array. Reading only one of the two
	    shapes throws away the message on whichever endpoint is not the one that
	    was tested, and the inpaint path is the one that matters. */
	QJsonObject rootObjectOf(const QByteArray& body)
	{
		if (body.isEmpty())
			return QJsonObject();
		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
		if (parseError.error != QJsonParseError::NoError)
			return QJsonObject();
		if (doc.isObject())
			return doc.object();
		if (doc.isArray())
		{
			const QJsonArray array = doc.array();
			for (const QJsonValue& value : array)
			{
				if (value.isObject())
					return value.toObject();
			}
		}
		return QJsonObject();
	}

	//! The "error" object out of a response body, or an empty object.
	QJsonObject errorObjectOf(const QByteArray& body)
	{
		return rootObjectOf(body).value(QLatin1String("error")).toObject();
	}

	/*! \brief Google's own classification of a failure.

	    The HTTP code is not the reliable part. An invalid API key comes back as
	    400 INVALID_ARGUMENT with a detail reason of API_KEY_INVALID, not as the
	    401 anyone would expect - measured against the live API on 30 Aug 2026,
	    on both endpoints this client uses. Reading the status and the reason
	    rather than the number is what lets the most common mistake a user can
	    make be reported as the thing they should go and fix. */
	QString errorStatusOf(const QByteArray& body)
	{
		return errorObjectOf(body).value(QLatin1String("status")).toString().trimmed();
	}

	//! First machine-readable reason in the error details, e.g. API_KEY_INVALID.
	QString errorReasonOf(const QByteArray& body)
	{
		const QJsonArray details = errorObjectOf(body).value(QLatin1String("details")).toArray();
		for (const QJsonValue& value : details)
		{
			const QString reason = value.toObject().value(QLatin1String("reason")).toString().trimmed();
			if (!reason.isEmpty())
				return reason;
		}
		return QString();
	}

	//! Whether a failure is "this key is no good", however it was dressed up.
	bool isBadKey(const QByteArray& body)
	{
		if (errorReasonOf(body) == QLatin1String("API_KEY_INVALID"))
			return true;
		// The reason array is the dependable signal; the sentence is a fallback
		// for the day it is not sent.
		const QString message = errorObjectOf(body).value(QLatin1String("message")).toString();
		return message.contains(QLatin1String("API key not valid"), Qt::CaseInsensitive)
		    || message.contains(QLatin1String("API key is invalid"), Qt::CaseInsensitive);
	}

	//! Whatever the far end actually said, or an empty string if it said
	//! nothing usable. Never contains anything the caller sent.
	QString messageOf(const QByteArray& body)
	{
		const QJsonObject error = errorObjectOf(body);
		const QString message = error.value(QLatin1String("message")).toString().trimmed();
		if (!message.isEmpty())
			return message;
		// Not the documented envelope: show a little of it rather than
		// swallowing the only evidence there is.
		return QString::fromUtf8(body.left(300)).trimmed();
	}

	/*! \brief The picture out of an answer, base64-decoded.

	    Two places to look, because the documentation describes both and they
	    carry the same bytes: output_image is the convenience accessor, and
	    steps[].content[] is the full record the model actually produced. */
	QByteArray imageInAnswer(const QJsonObject& root)
	{
		const QJsonObject outputImage = root.value(QLatin1String("output_image")).toObject();
		const QString direct = outputImage.value(QLatin1String("data")).toString();
		if (!direct.isEmpty())
		{
			const QByteArray bytes = QByteArray::fromBase64(direct.toLatin1());
			if (!bytes.isEmpty())
				return bytes;
		}

		const QJsonArray steps = root.value(QLatin1String("steps")).toArray();
		for (const QJsonValue& step : steps)
		{
			const QJsonArray content = step.toObject().value(QLatin1String("content")).toArray();
			for (const QJsonValue& value : content)
			{
				const QJsonObject part = value.toObject();
				// Either the part says it is an image, or its mime type does.
				// Google's own docs have written this key both ways, so both
				// spellings are accepted rather than guessed between.
				const QString type = part.value(QLatin1String("type")).toString();
				QString mime = part.value(QLatin1String("mime_type")).toString();
				if (mime.isEmpty())
					mime = part.value(QLatin1String("mimeType")).toString();
				if (type != QLatin1String("image") && !mime.startsWith(QLatin1String("image/")))
					continue;
				const QString data = part.value(QLatin1String("data")).toString();
				if (data.isEmpty())
					continue;
				const QByteArray bytes = QByteArray::fromBase64(data.toLatin1());
				if (!bytes.isEmpty())
					return bytes;
			}
		}
		return QByteArray();
	}

	/*! \brief Any words the model produced instead of a picture.

	    A model that declines usually still says why, and that sentence is the
	    most useful thing there is to show - far better than reporting that no
	    image came back. */
	QString textInAnswer(const QJsonObject& root)
	{
		const QString outputText = root.value(QLatin1String("output_text")).toString().trimmed();
		if (!outputText.isEmpty())
			return outputText;

		QStringList said;
		const QJsonArray steps = root.value(QLatin1String("steps")).toArray();
		for (const QJsonValue& step : steps)
		{
			const QJsonArray content = step.toObject().value(QLatin1String("content")).toArray();
			for (const QJsonValue& value : content)
			{
				const QString text = value.toObject().value(QLatin1String("text")).toString().trimmed();
				if (!text.isEmpty())
					said << text;
			}
		}
		return said.join(QLatin1String(" "));
	}

	/*! \brief Why the request was stopped, when something other than the model
	    stopped it.

	    A 200 is not always a yes. The interaction carries its own status and
	    its own errors array, and a safety block arrives that way rather than
	    as an HTTP error. promptFeedback.blockReason is the older spelling of
	    the same idea and is still checked, because a refusal reported as "no
	    image came back" tells the user nothing they can act on. */
	QString blockReasonOf(const QJsonObject& root)
	{
		const QJsonObject feedback = root.value(QLatin1String("promptFeedback")).toObject();
		const QString legacy = feedback.value(QLatin1String("blockReason")).toString().trimmed();
		if (!legacy.isEmpty())
			return legacy;

		QStringList reasons;
		const QJsonArray errors = root.value(QLatin1String("errors")).toArray();
		for (const QJsonValue& value : errors)
		{
			const QJsonObject entry = value.toObject();
			const QString message = entry.value(QLatin1String("message")).toString().trimmed();
			if (!message.isEmpty())
				reasons << message;
			else
			{
				const QString code = entry.value(QLatin1String("code")).toString().trimmed();
				if (!code.isEmpty())
					reasons << code;
			}
		}
		return reasons.join(QLatin1String(", "));
	}
}

/*!
 \brief The half of GeminiInpaintService that lives on the private thread.

 Compositing the overlay, the JPEG encode and the base64 both ways are all
 proportional to the size of the picture and none of them belong between two
 paint events.
 */
class GeminiInpaintWorker : public QObject
{
	Q_OBJECT

public:
	GeminiInpaintWorker() = default;

public slots:
	void configure(const QString& apiKey, const QString& model, int timeoutSeconds, const QString& apiBase)
	{
		m_apiKey = apiKey;
		m_model = model;
		m_timeoutSeconds = timeoutSeconds;
		m_apiBase = apiBase;
	}

	void testConnection()
	{
		if (!beginRequest(true))
			return;
		QNetworkRequest request { QUrl(m_apiBase + ModelsPath) };
		applyHeaders(request);
		trackReply(m_nam->get(request), /*isTest*/ true);
	}

	/*! \brief Ask Gemini to remove what the mask covers.

	    Request shape verified on 30 Aug 2026 against
	    https://ai.google.dev/gemini-api/docs/image-generation and
	    https://ai.google.dev/api/interactions-api. Note the flat \c input
	    array of typed parts, \c mime_type in snake_case and the base64 sitting
	    directly in \c data: this is the Interactions API, not the older
	    contents/parts/inline_data shape of the legacy generateContent
	    endpoint, and not what most examples on the internet still show. */
	void inpaint(const QImage& image, const QImage& mask)
	{
		if (!beginRequest(false))
			return;

		const QImage marked = AIInpaintComposite::maskOverlay(image, mask);
		const QByteArray jpeg = AIInpaintComposite::toJpeg(marked);
		if (jpeg.isEmpty())
		{
			finishRequest();
			emit failed(tr("Could not encode the image to send to Gemini."));
			return;
		}

		// One picture: the region with the area to remove painted red. See the
		// note on masking in aiinpaintprompts.h for why there is no separate
		// mask channel.
		QJsonObject textPart;
		textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
		textPart.insert(QStringLiteral("text"),
		                QString::fromLatin1(AIInpaintPrompts::REMOVE_OBJECT_COMPOSITE));

		QJsonObject imagePart;
		imagePart.insert(QStringLiteral("type"), QStringLiteral("image"));
		imagePart.insert(QStringLiteral("mime_type"), QStringLiteral("image/jpeg"));
		imagePart.insert(QStringLiteral("data"), QString::fromLatin1(jpeg.toBase64()));

		QJsonArray input;
		input.append(textPart);
		input.append(imagePart);

		QJsonObject body;
		body.insert(QStringLiteral("model"), m_model);
		body.insert(QStringLiteral("input"), input);

		QNetworkRequest request { QUrl(m_apiBase + InteractionsPath) };
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		applyHeaders(request);
		trackReply(m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)),
		           /*isTest*/ false);
	}

	void cancel()
	{
		if (!m_reply)
			return;
		m_cancelled = true;
		// abort() rather than merely disconnecting: this one is being paid for
		// by the call, so stopping the far end matters more here than it does
		// against a local server.
		m_reply->abort();
	}

signals:
	void finished(const QImage& result);
	void failed(const QString& error);
	void tested(bool ok, const QString& detail);

private:
	//! The key goes here and nowhere else. Not in the URL, not in the body,
	//! not in a log - there is no logging in this file.
	void applyHeaders(QNetworkRequest& request) const
	{
		request.setRawHeader(ApiKeyHeader, m_apiKey.toUtf8());
	}

	bool beginRequest(bool isTest)
	{
		if (!m_nam)
			m_nam = new QNetworkAccessManager(this);
		if (m_reply)
		{
			const QString busy = tr("A request is already running.");
			if (isTest)
				emit tested(false, busy);
			else
				emit failed(busy);
			return false;
		}
		if (m_apiKey.isEmpty())
		{
			const QString noKey = tr("No Gemini API key is set. Enter one in Preferences > AI Services.");
			if (isTest)
				emit tested(false, noKey);
			else
				emit failed(noKey);
			return false;
		}
		m_cancelled = false;
		m_timedOut = false;
		m_reported = false;
		m_httpStatus = 0;
		m_body.clear();
		if (m_graceTimer)
			m_graceTimer->stop();
		return true;
	}

	void trackReply(QNetworkReply* reply, bool isTest)
	{
		m_reply = reply;
		m_isTest = isTest;

		// An explicit timer rather than setTransferTimeout(), because a
		// timeout and a user cancel both arrive as OperationCanceledError and
		// the two have to be told apart to report either of them honestly.
		if (!m_timer)
		{
			m_timer = new QTimer(this);
			m_timer->setSingleShot(true);
			connect(m_timer, &QTimer::timeout, this, [this]() {
				if (!m_reply)
					return;
				// If the head already carried an error status then the server
				// did answer, and reporting a timeout would be both wrong and
				// useless. Say what it said.
				if (m_httpStatus >= 400)
				{
					concludeFromResponseSoFar();
					return;
				}
				m_timedOut = true;
				m_reply->abort();
			});
		}
		m_timer->start(qMax(1, m_timeoutSeconds) * 1000);

		if (!m_graceTimer)
		{
			m_graceTimer = new QTimer(this);
			m_graceTimer->setSingleShot(true);
			connect(m_graceTimer, &QTimer::timeout, this, [this]() {
				concludeFromResponseSoFar();
			});
		}

		// Read the body as it arrives rather than once at the end, and take the
		// status from the head as soon as it lands. A reply whose body never
		// completes still has to be reportable: waiting for finished() is what
		// turned a fast rejection into a timeout on the OpenRouter client.
		connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
			if (!m_reported)
				m_body.append(reply->readAll());
		});
		connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() {
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status <= 0)
				return;
			m_httpStatus = status;
			// 3xx is not a verdict: QNetworkAccessManager follows redirects on
			// its own and the real status arrives in a later head.
			if (status >= 400 && m_graceTimer && !m_graceTimer->isActive())
				m_graceTimer->start(ErrorBodyGraceMs);
		});
		connect(reply, &QNetworkReply::finished, this, [this, reply, isTest]() {
			handleReply(reply, isTest);
		});
	}

	void finishRequest()
	{
		if (m_timer)
			m_timer->stop();
		if (m_graceTimer)
			m_graceTimer->stop();
		m_reply = nullptr;
	}

	//! Report the failure the response head already described, without waiting
	//! for a reply that may never finish.
	void concludeFromResponseSoFar()
	{
		if (m_reported || !m_reply)
			return;
		if (m_reply->bytesAvailable() > 0)
			m_body.append(m_reply->readAll());

		const int status = m_httpStatus;
		const QByteArray body = m_body;
		const bool isTest = m_isTest;

		m_reported = true;
		QNetworkReply* reply = m_reply;
		finishRequest();
		reply->abort();
		reply->deleteLater();

		m_cancelled = false;
		m_timedOut = false;

		const QString message = describeStatus(status, body);
		if (isTest)
			emit tested(false, message);
		else
			emit failed(message);
	}

	/*! \brief The user-facing sentence for an HTTP status.

	    Each of these says what the user has to go and do, because a status
	    number on its own is not something anyone can act on. Anything not
	    listed falls through to whatever Google said. */
	QString describeStatus(int status, const QByteArray& body) const
	{
		const QString detail = messageOf(body);

		// What Google says went wrong, before what the HTTP code implies. A
		// rejected key arrives as 400, so keying off the number alone would
		// report the commonest mistake of all as an unexplained bad request.
		if (isBadKey(body))
			return tr("Invalid Gemini API key. Check your Gemini API key in Preferences > AI Services.");
		const QString googleStatus = errorStatusOf(body);
		if (googleStatus == QLatin1String("PERMISSION_DENIED"))
			return tr("Gemini API access denied. Check that billing is enabled for your key at aistudio.google.com.");
		if (googleStatus == QLatin1String("RESOURCE_EXHAUSTED"))
			return tr("Gemini rate limit - try again in a moment.");

		switch (status)
		{
		case 400:
			// Not a user error as such, so the server's own words are the
			// useful part - this is the one that gets pasted into a bug report.
			// The number goes in regardless, because the words are the part
			// that can be missing: an error body that never finished arriving
			// leaves a fragment, and a fragment on its own says nothing.
			if (!detail.isEmpty())
				return tr("Gemini rejected the request (400): %1").arg(detail);
			return tr("Gemini rejected the request (400).");
		case 401:
			return tr("Invalid Gemini API key. Check your Gemini API key in Preferences > AI Services.");
		case 403:
			return tr("Gemini API access denied. Check that billing is enabled for your key at aistudio.google.com.");
		case 429:
			return tr("Gemini rate limit - try again in a moment.");
		default:
			break;
		}
		if (status >= 500)
			return tr("The Google API is temporarily unavailable (%1) - try again.").arg(status);
		if (!detail.isEmpty())
			return tr("Gemini answered %1: %2").arg(status).arg(detail);
		return tr("Gemini answered %1.").arg(status);
	}

	void handleReply(QNetworkReply* reply, bool isTest)
	{
		if (m_reported)
		{
			// Already answered from the response head. This is the finished()
			// that follows our own abort().
			return;
		}

		const QNetworkReply::NetworkError netError = reply->error();
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		// A connection that broke after the head arrived loses the attribute,
		// but not what the head said.
		if (status <= 0)
			status = m_httpStatus;
		// An aborted reply has nothing to read and complains if asked.
		const bool aborted = m_cancelled || m_timedOut;
		if (!aborted && reply->bytesAvailable() > 0)
			m_body.append(reply->readAll());
		const QByteArray body = aborted ? QByteArray() : m_body;
		const QString transportError = reply->errorString();

		finishRequest();
		reply->deleteLater();

		const bool cancelled = m_cancelled;
		const bool timedOut = m_timedOut;
		m_cancelled = false;
		m_timedOut = false;

		if (timedOut)
		{
			const QString message = tr("Gemini did not answer within %1 seconds.").arg(m_timeoutSeconds);
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}
		if (cancelled)
		{
			if (isTest)
				emit tested(false, tr("Cancelled."));
			else
				emit failed(AIInpaintService::cancelledMarker());
			return;
		}

		if (netError != QNetworkReply::NoError && status == 0)
		{
			// Never reached Google at all: no HTTP status was ever set.
			// transportError describes the socket, and cannot contain the key.
			const QString message = tr("Could not reach the Google API: %1").arg(transportError);
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		if (status < 200 || status > 299)
		{
			const QString message = describeStatus(status, body);
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		// Unwrapped the same way as an error body: this endpoint has been seen
		// to wrap its envelope in a one-element array, and there is no reason
		// to assume it does that only when it is unhappy.
		const QJsonObject root = rootObjectOf(body);
		if (root.isEmpty())
		{
			const QString message = tr("Gemini answered with something that is not JSON (%1 bytes).")
			                        .arg(body.size());
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		if (isTest)
		{
			// A key that can list models is a key Google accepts. It does not
			// prove the account can pay - that shows up as 403 on the first
			// real call - so the wording promises only what was checked.
			const int count = root.value(QStringLiteral("models")).toArray().size();
			if (count > 0)
				emit tested(true, tr("Key accepted. %n model(s) available.", "", count));
			else
				emit tested(true, tr("Key accepted."));
			return;
		}

		// --- the picture, if there is one ---
		const QByteArray imageBytes = imageInAnswer(root);
		if (imageBytes.isEmpty())
		{
			// A 200 is not always a yes: a safety block arrives this way rather
			// than as an HTTP error, and the reason is the only useful thing
			// there is to show.
			const QString blocked = blockReasonOf(root);
			if (!blocked.isEmpty())
			{
				emit failed(AIInpaintService::refusalMarker()
				            + tr("%1 declined this edit: %2")
				              .arg(GeminiInpaintService::displayNameFor(m_model), blocked));
				return;
			}
			const QString said = textInAnswer(root);
			if (!said.isEmpty())
			{
				emit failed(AIInpaintService::refusalMarker()
				            + tr("%1 returned no image. It said: %2")
				              .arg(GeminiInpaintService::displayNameFor(m_model), said));
				return;
			}
			emit failed(tr("Gemini returned no image."));
			return;
		}

		QImage result;
		if (!result.loadFromData(imageBytes))
		{
			emit failed(tr("Gemini returned %1 bytes that are not a readable image.")
			            .arg(imageBytes.size()));
			return;
		}
		emit finished(result);
	}

	QNetworkAccessManager* m_nam {nullptr};
	QPointer<QNetworkReply> m_reply;
	QTimer* m_timer {nullptr};
	//! Bounds the wait for the rest of an error body once the head has already
	//! shown an error status.
	QTimer* m_graceTimer {nullptr};
	//! Everything the response has said so far, accumulated as it arrives.
	QByteArray m_body;
	//! Status from the response head. 0 until the head lands.
	int m_httpStatus {0};
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
	bool m_cancelled {false};
	bool m_timedOut {false};
	//! Whether the request in flight is a key check rather than an edit.
	bool m_isTest {false};
	//! Set once a verdict has been emitted, so the finished() following our own
	//! abort() does not emit a second one.
	bool m_reported {false};
};

QString GeminiInpaintService::defaultApiBase()
{
	return QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
}

const QList<GeminiInpaintService::ModelChoice>& GeminiInpaintService::models()
{
	// Curated list of Aug 2026, checked against
	// https://ai.google.dev/gemini-api/docs/models on 30 Aug 2026. All three
	// are the stable releases: the "-preview" suffixes these ids carried
	// earlier in the year are gone, and an id with one on it now 404s.
	//
	// Note for anyone updating: OpenRouter's catalogue still lists some of the
	// same models under "-preview" names. Those are OpenRouter's ids, not
	// Google's, and the two lists are not interchangeable.
	static const QList<ModelChoice> list = {
		{ QStringLiteral("gemini-3.1-flash-image"),
		  QObject::tr("Nano Banana 2"),
		  QObject::tr("Recommended. Balanced quality and cost.") },
		{ QStringLiteral("gemini-3.1-flash-lite-image"),
		  QObject::tr("Nano Banana 2 Lite"),
		  QObject::tr("Cheapest. For working through a lot of removals.") },
		{ QStringLiteral("gemini-3-pro-image"),
		  QObject::tr("Nano Banana Pro"),
		  QObject::tr("Premium quality, around twice the cost.") },
	};
	return list;
}

QString GeminiInpaintService::defaultModel()
{
	return QStringLiteral("gemini-3.1-flash-image");
}

QString GeminiInpaintService::displayNameFor(const QString& modelId)
{
	const QList<ModelChoice>& list = models();
	for (const ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return choice.displayName;
	}
	return modelId;
}

QString GeminiInpaintService::shortNameFor(const QString& modelId)
{
	const int slash = modelId.lastIndexOf(QLatin1Char('/'));
	QString name = slash >= 0 ? modelId.mid(slash + 1) : modelId;
	// A preview and its stable release are the same model as far as a filename
	// is concerned, and the suffix is only noise in one.
	if (name.endsWith(QLatin1String("-preview")))
		name.chop(8);
	return name;
}

QString GeminiInpaintService::fileTagFor(const QString& modelId)
{
	QString tag = shortNameFor(modelId);
	tag.replace(QLatin1Char('-'), QLatin1Char('_'));
	// Anything else that is not a plain filename character goes too, so that a
	// future model id cannot produce a path that is not what it looks like.
	for (int i = 0; i < tag.size(); ++i)
	{
		const QChar c = tag.at(i);
		if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('.'))
			tag[i] = QLatin1Char('_');
	}
	return tag;
}

GeminiInpaintService::GeminiInpaintService(const QString& apiKey,
                                           const QString& model,
                                           int timeoutSeconds,
                                           QObject* parent,
                                           const QString& apiBase)
	: AIInpaintService(parent),
	  m_apiKey(apiKey),
	  m_model(model),
	  m_apiBase(apiBase),
	  m_timeoutSeconds(timeoutSeconds)
{
	m_worker = new GeminiInpaintWorker;
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	connect(m_worker, &GeminiInpaintWorker::finished, this, &AIInpaintService::inpaintFinished);
	connect(m_worker, &GeminiInpaintWorker::failed, this, &AIInpaintService::inpaintFailed);
	connect(m_worker, &GeminiInpaintWorker::tested, this, &AIInpaintService::connectionTested);

	m_thread.start();
	setCredentials(m_apiKey, m_model, m_timeoutSeconds);
}

GeminiInpaintService::~GeminiInpaintService()
{
	// Stop anything in flight before the thread goes, or the reply outlives
	// the manager that owns it.
	cancel();
	m_thread.quit();
	m_thread.wait();
}

QString GeminiInpaintService::name() const
{
	return tr("Gemini (%1)").arg(displayNameFor(m_model));
}

void GeminiInpaintService::setCredentials(const QString& apiKey, const QString& model, int timeoutSeconds)
{
	m_apiKey = apiKey;
	m_model = model;
	m_timeoutSeconds = timeoutSeconds;
	QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
	                          Q_ARG(QString, m_apiKey), Q_ARG(QString, m_model),
	                          Q_ARG(int, m_timeoutSeconds), Q_ARG(QString, m_apiBase));
}

void GeminiInpaintService::testConnection()
{
	QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection);
}

void GeminiInpaintService::inpaint(const QImage& image, const QImage& mask)
{
	if (m_model.isEmpty())
	{
		emit inpaintFailed(tr("No Gemini model is selected. Pick one in Preferences > AI Services."));
		return;
	}
	QMetaObject::invokeMethod(m_worker, "inpaint", Qt::QueuedConnection,
	                          Q_ARG(QImage, image), Q_ARG(QImage, mask));
}

void GeminiInpaintService::cancel()
{
	QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection);
}

#include "geminiinpaintservice.moc"
