/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/openrouterinpaintservice.h"

#include "ai/aiinpaintcomposite.h"
#include "ai/aiinpaintprompts.h"

#include <QBuffer>
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
	//! Paths under the API root. See the wire-format note in the header.
	const QLatin1String ImagesPath("/images");
	//! Key check. It is /key - *not* /auth/key, which does not exist.
	const QLatin1String KeyPath("/key");

	//! Sent as HTTP-Referer and X-Title. OpenRouter uses these to attribute
	//! traffic on its public model rankings; they identify Scribus, not the
	//! user, and carry nothing from the document.
	const QLatin1String RefererHeader("https://scribus.net");
	const QLatin1String TitleHeader("Scribus");

	/*! \brief How long to keep collecting an error body once the response head
	    has already shown an error status.

	    The status alone is enough to report every case a user can act on, so
	    this is only a short window to pick up the message that usually comes
	    with it. It is deliberately well under the two seconds a rejection is
	    expected to surface in: an error body that has not arrived by then is
	    not going to arrive. */
	const int ErrorBodyGraceMs = 1200;

	//! JPEG-encode the composited picture as a complete data: URL, ready to go
	//! straight into the request. Empty on failure.
	QString toDataUrl(const QImage& image)
	{
		const QByteArray jpeg = AIInpaintComposite::toJpeg(image);
		if (jpeg.isEmpty())
			return QString();
		return QLatin1String("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
	}

	//! The "error" object out of a response body, or an empty object.
	QJsonObject errorObjectOf(const QByteArray& body)
	{
		if (body.isEmpty())
			return QJsonObject();
		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
			return QJsonObject();
		return doc.object().value(QLatin1String("error")).toObject();
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

	//! Which vendor was behind a failure, when the metadata names one.
	QString providerOf(const QByteArray& body)
	{
		const QJsonObject metadata = errorObjectOf(body).value(QLatin1String("metadata")).toObject();
		return metadata.value(QLatin1String("provider_name")).toString().trimmed();
	}

	/*! \brief Why a moderation system rejected the job, if that is what
	    happened.

	    An error whose metadata carries "reasons" is content moderation rather
	    than a fault, and is worth telling apart: the answer to it is another
	    model, not another attempt. */
	QString moderationReasonOf(const QByteArray& body)
	{
		const QJsonObject metadata = errorObjectOf(body).value(QLatin1String("metadata")).toObject();
		const QJsonValue reasons = metadata.value(QLatin1String("reasons"));
		if (!reasons.isArray())
			return QString();
		QStringList out;
		const QJsonArray array = reasons.toArray();
		for (const QJsonValue& value : array)
		{
			const QString text = value.toString().trimmed();
			if (!text.isEmpty())
				out << text;
		}
		return out.join(QLatin1String(", "));
	}

	/*! \brief Any text the model produced instead of a picture.

	    A model that declines usually still says something, and that sentence
	    is the most useful thing there is to show - far better than reporting
	    that zero images came back. */
	QString textInAnswer(const QJsonObject& root)
	{
		const QJsonArray data = root.value(QLatin1String("data")).toArray();
		for (const QJsonValue& value : data)
		{
			const QJsonObject entry = value.toObject();
			for (const QLatin1String key : { QLatin1String("text"),
			                                 QLatin1String("revised_prompt"),
			                                 QLatin1String("finish_reason") })
			{
				const QString text = entry.value(key).toString().trimmed();
				if (!text.isEmpty())
					return text;
			}
		}
		for (const QLatin1String key : { QLatin1String("text"), QLatin1String("message") })
		{
			const QString text = root.value(key).toString().trimmed();
			if (!text.isEmpty())
				return text;
		}
		return root.value(QLatin1String("error")).toObject()
		           .value(QLatin1String("message")).toString().trimmed();
	}
}

/*!
 \brief The half of OpenRouterInpaintService that lives on the private thread.

 Compositing the overlay, the JPEG encode and the base64 both ways are all
 proportional to the size of the picture and none of them belong between two
 paint events.
 */
class OpenRouterInpaintWorker : public QObject
{
	Q_OBJECT

public:
	OpenRouterInpaintWorker() = default;

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
		QNetworkRequest request { QUrl(m_apiBase + KeyPath) };
		applyHeaders(request);
		trackReply(m_nam->get(request), /*isTest*/ true);
	}

	void inpaint(const QImage& image, const QImage& mask)
	{
		if (!beginRequest(false))
			return;

		const QImage marked = AIInpaintComposite::maskOverlay(image, mask);
		const QString dataUrl = toDataUrl(marked);
		if (dataUrl.isEmpty())
		{
			finishRequest();
			emit failed(tr("Could not encode the image to send to OpenRouter."));
			return;
		}

		// One reference image: the picture with the area to remove painted
		// red. See the note on masking in the header for why there is no
		// separate mask.
		QJsonObject imageUrl;
		imageUrl.insert(QStringLiteral("url"), dataUrl);
		QJsonObject reference;
		reference.insert(QStringLiteral("type"), QStringLiteral("image_url"));
		reference.insert(QStringLiteral("image_url"), imageUrl);
		QJsonArray references;
		references.append(reference);

		QJsonObject body;
		body.insert(QStringLiteral("model"), m_model);
		body.insert(QStringLiteral("prompt"), QString::fromLatin1(AIInpaintPrompts::REMOVE_OBJECT_COMPOSITE));
		body.insert(QStringLiteral("input_references"), references);

		QNetworkRequest request { QUrl(m_apiBase + ImagesPath) };
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
		// by the second, so stopping the far end matters more here than it
		// does against a local server.
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
		request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
		request.setRawHeader("HTTP-Referer", QByteArray(RefererHeader.data(), RefererHeader.size()));
		request.setRawHeader("X-Title", QByteArray(TitleHeader.data(), TitleHeader.size()));
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
			const QString noKey = tr("No OpenRouter API key is set. Enter one in Preferences > AI Services.");
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

		// Read the body as it arrives rather than once at the end. readAll()
		// in the finished() handler only ever sees anything for a reply that
		// reaches a clean finish, and the replies that matter here are exactly
		// the ones that do not.
		connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
			if (!m_reported)
				m_body.append(reply->readAll());
		});

		// The status code is known as soon as the head lands, which is the
		// moment a failure is already decided. Waiting for the rest of a body
		// that may never complete is what turned a fast rejection into a
		// sixty-second timeout.
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

	/*! \brief Report the failure the response head already described, without
	    waiting for a reply that may never finish.

	    Reached from the error-body grace timer and from the overall timeout.
	    Whatever body arrived is used; for every status a user can act on the
	    status alone is enough. */
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
		// The verdict is in, and this one is being paid for by the second:
		// nothing is gained by leaving the transfer running. The finished()
		// that follows is ignored, m_reported having been set above.
		reply->abort();
		reply->deleteLater();

		m_cancelled = false;
		m_timedOut = false;

		emitStatusFailure(status, body, isTest);
	}

	//! The verdict for a response that carried an HTTP error status, wherever
	//! we came to learn of it: a clean finish, the grace timer, or the timeout.
	void emitStatusFailure(int status, const QByteArray& body, bool isTest)
	{
		// Moderation is not a fault, and the answer to it is a different model
		// rather than another go, so it is marked as a refusal.
		const QString moderation = moderationReasonOf(body);
		if (!isTest && !moderation.isEmpty())
		{
			emit failed(OpenRouterInpaintService::refusalMarker()
			            + tr("%1 declined this edit: %2")
			              .arg(OpenRouterInpaintService::displayNameFor(m_model), moderation));
			return;
		}
		const QString message = describeStatus(status, body);
		if (isTest)
			emit tested(false, message);
		else
			emit failed(message);
	}

	//! The user-facing sentence for an HTTP status. These are the four that
	//! mean something a user can act on; everything else falls through to
	//! whatever OpenRouter said.
	QString describeStatus(int status, const QByteArray& body) const
	{
		const QString detail = messageOf(body);
		switch (status)
		{
		case 401:
			return tr("OpenRouter rejected the API key. Check your OpenRouter API key in Preferences > AI Services.");
		case 402:
			return tr("Your OpenRouter account is out of credits. Add credits to your OpenRouter account and try again.");
		case 429:
			return tr("OpenRouter is rate limiting this key - try again in a moment.");
		default:
			break;
		}
		if (status >= 500)
		{
			const QString provider = providerOf(body);
			if (!provider.isEmpty())
				return tr("%1 is not answering through OpenRouter right now (%2). Try again, or pick another model in Preferences.")
				       .arg(provider).arg(status);
			return tr("OpenRouter returned a server error (%1). Try again in a moment.").arg(status);
		}
		if (!detail.isEmpty())
			return tr("OpenRouter answered %1: %2").arg(status).arg(detail);
		return tr("OpenRouter answered %1.").arg(status);
	}

	void handleReply(QNetworkReply* reply, bool isTest)
	{
		if (m_reported)
		{
			// Already answered from the response head. This is the finished()
			// that follows the abort() in concludeFromResponseSoFar(), and the
			// reply is on its way out.
			return;
		}

		const QNetworkReply::NetworkError netError = reply->error();
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		// A connection that broke after the head arrived loses the attribute,
		// but not what the head said. That happens whenever the far end rejects
		// a large upload without draining it, which is the common shape of a
		// refusal here: the picture is megabytes and the verdict is instant.
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
			const QString message = tr("OpenRouter did not answer within %1 seconds.").arg(m_timeoutSeconds);
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
			// Never reached OpenRouter at all: no HTTP status was ever set.
			// transportError describes the socket, and cannot contain the key.
			const QString message = tr("Could not reach OpenRouter: %1").arg(transportError);
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		if (status < 200 || status > 299)
		{
			emitStatusFailure(status, body, isTest);
			return;
		}

		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		{
			const QString message = tr("OpenRouter answered with something that is not JSON (%1 bytes).")
			                        .arg(body.size());
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}
		const QJsonObject root = doc.object();

		if (isTest)
		{
			// The key endpoint answers with the key's limits. Saying what is
			// left is what makes the test worth pressing: it is the difference
			// between "the key is valid" and "the key is valid and can pay".
			const QJsonObject data = root.value(QStringLiteral("data")).isObject()
			                       ? root.value(QStringLiteral("data")).toObject()
			                       : root;
			const QJsonValue remaining = data.value(QStringLiteral("limit_remaining"));
			if (remaining.isDouble())
			{
				emit tested(true, tr("Key accepted. About $%1 of credit left.")
				                  .arg(remaining.toDouble(), 0, 'f', 2));
				return;
			}
			if (data.value(QStringLiteral("is_free_tier")).toBool(false))
			{
				emit tested(true, tr("Key accepted, but the account has no credits yet. "
				                     "Image models need paid credit."));
				return;
			}
			emit tested(true, tr("Key accepted."));
			return;
		}

		// --- the picture, if there is one ---
		const QJsonArray data = root.value(QStringLiteral("data")).toArray();
		QByteArray imageBytes;
		QString unusableUrl;
		for (const QJsonValue& value : data)
		{
			const QJsonObject entry = value.toObject();
			const QString b64 = entry.value(QStringLiteral("b64_json")).toString();
			if (!b64.isEmpty())
			{
				imageBytes = QByteArray::fromBase64(b64.toLatin1());
				if (!imageBytes.isEmpty())
					break;
			}
			const QString url = entry.value(QStringLiteral("url")).toString();
			if (url.startsWith(QLatin1String("data:")))
			{
				const int comma = url.indexOf(QLatin1Char(','));
				if (comma > 0)
				{
					imageBytes = QByteArray::fromBase64(url.mid(comma + 1).toLatin1());
					if (!imageBytes.isEmpty())
						break;
				}
			}
			else if (!url.isEmpty())
			{
				unusableUrl = url;
			}
		}

		if (imageBytes.isEmpty())
		{
			if (!unusableUrl.isEmpty())
			{
				// Deliberately not fetched. This service contacts openrouter.ai
				// and nothing else, and a link handed back in a response is not
				// a good enough reason to go and talk to another host.
				emit failed(tr("This model returned a link to the result rather than the image itself, "
				               "which Scribus does not follow. Pick another model in Preferences."));
				return;
			}
			// No picture came back. Whatever the model said instead is the
			// most useful thing to show, and it is nearly always a refusal.
			const QString said = textInAnswer(root);
			if (!said.isEmpty())
			{
				emit failed(OpenRouterInpaintService::refusalMarker()
				            + tr("%1 returned no image. It said: %2")
				              .arg(OpenRouterInpaintService::displayNameFor(m_model), said));
				return;
			}
			emit failed(tr("OpenRouter returned no image."));
			return;
		}

		QImage result;
		if (!result.loadFromData(imageBytes))
		{
			emit failed(tr("OpenRouter returned %1 bytes that are not a readable image.")
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
	//! Everything the response has said so far, accumulated as it arrives. A
	//! reply that never finishes cleanly still has to be able to report what
	//! the server already told us.
	QByteArray m_body;
	//! Status from the response head, known long before the body is complete.
	//! 0 until the head lands.
	int m_httpStatus {0};
	//! Whether the request in flight is a key check rather than an edit, so
	//! that the paths which report without a finished() reply know which
	//! signal to raise.
	bool m_isTest {false};
	//! Set once a verdict has been emitted, so that the finished() following
	//! our own abort() does not emit a second one.
	bool m_reported {false};
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
	bool m_cancelled {false};
	bool m_timedOut {false};
};

QString OpenRouterInpaintService::defaultApiBase()
{
	return QStringLiteral("https://openrouter.ai/api/v1");
}

const QList<OpenRouterInpaintService::ModelChoice>& OpenRouterInpaintService::models()
{
	// Curated list of Aug 2026, checked against the live
	// https://openrouter.ai/api/v1/images/models on 29 Aug 2026: every id here
	// exists and every one of them lists "image" among its input modalities
	// and "input_references" among its supported parameters, which is what an
	// edit needs. Verify against
	// https://openrouter.ai/models?output_modalities=image and update as
	// needed.
	//
	// Note for anyone updating: the chat-completions list at /api/v1/models is
	// *not* the same set and does not contain most of these. The images API
	// has its own catalogue at /api/v1/images/models, and that is the one to
	// check against.
	static const QList<ModelChoice> list = {
		{ QStringLiteral("google/gemini-3.1-flash-image-preview"),
		  QObject::tr("Nano Banana 2"),
		  QObject::tr("Recommended. Balanced quality and cost.") },
		{ QStringLiteral("google/gemini-3.1-flash-lite-image"),
		  QObject::tr("Nano Banana 2 Lite"),
		  QObject::tr("Cheapest. For working through a lot of removals.") },
		{ QStringLiteral("google/gemini-3-pro-image-preview"),
		  QObject::tr("Nano Banana Pro"),
		  QObject::tr("Premium quality, around twice the cost.") },
		{ QStringLiteral("openai/gpt-image-2"),
		  QObject::tr("GPT Image 2"),
		  QObject::tr("Best at following the mask, and at leaving text alone.") },
		{ QStringLiteral("black-forest-labs/flux.2-flex"),
		  QObject::tr("FLUX 2 Flex"),
		  QObject::tr("Different look, and usually the least restrictive.") },
		{ QStringLiteral("bytedance-seed/seedream-4.5"),
		  QObject::tr("Seedream 4.5"),
		  QObject::tr("Good on portraits and on keeping faces consistent.") },
	};
	return list;
}

QString OpenRouterInpaintService::displayNameFor(const QString& modelId)
{
	const QList<ModelChoice>& list = models();
	for (const ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return choice.displayName;
	}
	return modelId;
}

QString OpenRouterInpaintService::shortNameFor(const QString& modelId)
{
	const int slash = modelId.lastIndexOf(QLatin1Char('/'));
	return slash >= 0 ? modelId.mid(slash + 1) : modelId;
}

QString OpenRouterInpaintService::fileTagFor(const QString& modelId)
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

// The three below now live on AIInpaintService, a refusal being the same kind
// of answer whichever model gave it. They stay here as forwards so that every
// existing caller keeps compiling.
QString OpenRouterInpaintService::refusalMarker()
{
	return AIInpaintService::refusalMarker();
}

bool OpenRouterInpaintService::isRefusal(const QString& error)
{
	return AIInpaintService::isRefusal(error);
}

QString OpenRouterInpaintService::strippedRefusal(const QString& error)
{
	return AIInpaintService::strippedRefusal(error);
}

OpenRouterInpaintService::OpenRouterInpaintService(const QString& apiKey,
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
	m_worker = new OpenRouterInpaintWorker;
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	connect(m_worker, &OpenRouterInpaintWorker::finished, this, &AIInpaintService::inpaintFinished);
	connect(m_worker, &OpenRouterInpaintWorker::failed, this, &AIInpaintService::inpaintFailed);
	connect(m_worker, &OpenRouterInpaintWorker::tested, this, &AIInpaintService::connectionTested);

	m_thread.start();
	setCredentials(m_apiKey, m_model, m_timeoutSeconds);
}

OpenRouterInpaintService::~OpenRouterInpaintService()
{
	// Stop anything in flight before the thread goes, or the reply outlives
	// the manager that owns it.
	cancel();
	m_thread.quit();
	m_thread.wait();
}

QString OpenRouterInpaintService::name() const
{
	return tr("OpenRouter (%1)").arg(displayNameFor(m_model));
}

void OpenRouterInpaintService::setCredentials(const QString& apiKey, const QString& model, int timeoutSeconds)
{
	m_apiKey = apiKey;
	m_model = model;
	m_timeoutSeconds = timeoutSeconds;
	QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
	                          Q_ARG(QString, m_apiKey), Q_ARG(QString, m_model),
	                          Q_ARG(int, m_timeoutSeconds), Q_ARG(QString, m_apiBase));
}

void OpenRouterInpaintService::testConnection()
{
	QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection);
}

void OpenRouterInpaintService::inpaint(const QImage& image, const QImage& mask)
{
	if (m_model.isEmpty())
	{
		emit inpaintFailed(tr("No OpenRouter model is selected. Pick one in Preferences > AI Services."));
		return;
	}
	QMetaObject::invokeMethod(m_worker, "inpaint", Qt::QueuedConnection,
	                          Q_ARG(QImage, image), Q_ARG(QImage, mask));
}

void OpenRouterInpaintService::cancel()
{
	QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection);
}

#include "openrouterinpaintservice.moc"
