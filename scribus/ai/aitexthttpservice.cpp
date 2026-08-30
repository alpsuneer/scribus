/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aitexthttpservice.h"

#include "ai/aitextprompts.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace
{
	/*! \brief How long to keep collecting an error body once the response head
	    has already shown an error status.

	    The status alone is enough to report every case a user can act on, so
	    this is only a short window to pick up the message that usually comes
	    with it - and it is well under the two seconds a rejection is expected
	    to surface in. */
	const int ErrorBodyGraceMs = 1200;

	//! Long edge cap for a picture on its way to a vision model.
	const int MaxImageEdge = 1568;

	//! Quality 85 is plenty for a model that is being asked what is in the
	//! picture rather than to reproduce it, and it halves the upload.
	const int JpegQuality = 85;
}

AITextProtocol::~AITextProtocol() = default;

const AITextProtocol::ModelChoice* AITextProtocol::choiceFor(const QString& modelId) const
{
	const QList<ModelChoice>& list = models();
	for (const ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return &choice;
	}
	return nullptr;
}

QString AITextProtocol::displayNameFor(const QString& modelId) const
{
	const ModelChoice* choice = choiceFor(modelId);
	return choice ? choice->displayName : modelId;
}

/*!
 \brief The half of AITextHttpService that lives on the private thread.

 Owns the network access manager and every timer. Nothing here logs, and the
 key reaches exactly one place: the protocol's applyHeaders().
 */
class AITextHttpWorker : public QObject
{
	Q_OBJECT

public:
	explicit AITextHttpWorker(std::shared_ptr<const AITextProtocol> protocol)
		: m_protocol(std::move(protocol))
	{
	}

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
		send(m_protocol->buildTest(m_apiBase, m_model), /*isTest*/ true);
	}

	void execute(const QString& task, const QString& inputText,
	             const QImage& image, const QVariantMap& parameters)
	{
		if (!beginRequest(false))
			return;

		const QString prompt = AITextPrompts::promptFor(task, inputText, parameters);
		if (prompt.isEmpty())
		{
			finishRequest();
			emit failed(tr("Scribus does not know how to ask for \"%1\".").arg(task));
			return;
		}

		QByteArray jpeg;
		if (AITextService::taskNeedsImage(task))
		{
			jpeg = AITextHttpService::encodeImage(image);
			if (jpeg.isEmpty())
			{
				finishRequest();
				emit failed(tr("Could not encode the picture to send to %1.")
				            .arg(m_protocol->providerName()));
				return;
			}
		}

		emit progressUpdate(tr("Asking %1 ...").arg(m_protocol->displayNameFor(m_model)));
		send(m_protocol->buildExecute(m_apiBase, m_model, prompt, jpeg, MaxTokens), /*isTest*/ false);
	}

	void cancel()
	{
		if (!m_reply)
			return;
		m_cancelled = true;
		// abort() rather than merely disconnecting: this one is being paid for
		// by the token, so stopping the far end matters.
		m_reply->abort();
	}

signals:
	void completed(const AITextService::Response& result);
	void failed(const QString& error);
	void progressUpdate(const QString& status);
	void tested(bool ok, const QString& detail);

private:
	//! Enough for a long summary or six headlines, and small enough that a
	//! runaway answer cannot quietly cost a fortune.
	static const int MaxTokens = 2048;

	bool beginRequest(bool isTest)
	{
		if (!m_nam)
			m_nam = new QNetworkAccessManager(this);
		if (m_reply)
		{
			const QString busy = tr("A request is already running.");
			isTest ? emit tested(false, busy) : emit failed(busy);
			return false;
		}
		if (m_apiKey.isEmpty())
		{
			const QString noKey = tr("No %1 API key is set. Enter one in Preferences > AI Services.")
			                      .arg(m_protocol->providerName());
			isTest ? emit tested(false, noKey) : emit failed(noKey);
			return false;
		}
		if (m_model.isEmpty())
		{
			const QString noModel = tr("No %1 model is selected. Pick one in Preferences > AI Services.")
			                        .arg(m_protocol->providerName());
			isTest ? emit tested(false, noModel) : emit failed(noModel);
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

	void send(const AITextProtocol::Prepared& prepared, bool isTest)
	{
		QNetworkRequest request { prepared.url };
		if (prepared.post)
			request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		m_protocol->applyHeaders(request, m_apiKey);
		trackReply(prepared.post ? m_nam->post(request, prepared.body) : m_nam->get(request), isTest);
	}

	void trackReply(QNetworkReply* reply, bool isTest)
	{
		m_reply = reply;
		m_isTest = isTest;

		// An explicit timer rather than setTransferTimeout(), because a timeout
		// and a user cancel both arrive as OperationCanceledError and the two
		// have to be told apart to report either of them honestly.
		if (!m_timer)
		{
			m_timer = new QTimer(this);
			m_timer->setSingleShot(true);
			connect(m_timer, &QTimer::timeout, this, [this]() {
				if (!m_reply)
					return;
				// If the head already carried an error status then the service
				// did answer, and reporting a timeout would be wrong as well as
				// useless.
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

		// Read the body as it arrives, and take the status from the head the
		// moment it lands. Waiting for finished() is what once turned a fast
		// rejection into a sixty-second timeout on the image side.
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

		const QString message = m_protocol->describeStatus(status, body);
		isTest ? emit tested(false, message) : emit failed(message);
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
			const QString message = tr("%1 did not answer within %2 seconds.")
			                        .arg(m_protocol->providerName()).arg(m_timeoutSeconds);
			isTest ? emit tested(false, message) : emit failed(message);
			return;
		}
		if (cancelled)
		{
			isTest ? emit tested(false, tr("Cancelled."))
			       : emit failed(AITextService::cancelledMarker());
			return;
		}
		if (netError != QNetworkReply::NoError && status == 0)
		{
			// Never reached the service at all: no HTTP status was ever set.
			// transportError describes the socket and cannot contain the key.
			const QString message = tr("Could not reach %1: %2")
			                        .arg(m_protocol->providerName(), transportError);
			isTest ? emit tested(false, message) : emit failed(message);
			return;
		}
		if (status < 200 || status > 299)
		{
			const QString message = m_protocol->describeStatus(status, body);
			isTest ? emit tested(false, message) : emit failed(message);
			return;
		}

		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		{
			const QString message = tr("%1 answered with something that is not JSON (%2 bytes).")
			                        .arg(m_protocol->providerName()).arg(body.size());
			isTest ? emit tested(false, message) : emit failed(message);
			return;
		}
		const QJsonObject root = doc.object();

		if (isTest)
		{
			emit tested(true, m_protocol->describeTestSuccess(root));
			return;
		}

		QString text;
		QString refusal;
		if (!m_protocol->parseAnswer(root, text, refusal))
		{
			emit failed(tr("%1 returned no answer.").arg(m_protocol->providerName()));
			return;
		}
		if (!refusal.isEmpty())
		{
			// A refusal is not a fault and is never retried. The model's own
			// words are the most useful thing there is to show.
			emit failed(AITextService::refusalMarker()
			            + tr("%1 declined: %2")
			              .arg(m_protocol->displayNameFor(m_model), refusal));
			return;
		}

		AITextService::Response response;
		response.modelDisplayName = m_protocol->displayNameFor(m_model);
		response.results = splitResults(text);
		if (response.results.isEmpty())
		{
			emit failed(tr("%1 returned an empty answer.").arg(m_protocol->providerName()));
			return;
		}

		int inputTokens = 0, outputTokens = 0;
		m_protocol->parseUsage(root, inputTokens, outputTokens);
		response.tokensUsed = inputTokens + outputTokens;
		if (const AITextProtocol::ModelChoice* choice = m_protocol->choiceFor(m_model))
		{
			response.estimatedCost = (inputTokens  * choice->inputPerMTok
			                        + outputTokens * choice->outputPerMTok) / 1000000.0;
		}
		emit completed(response);
	}

	/*! \brief One answer, or several when the model was asked for options.

	    Only the multi-option tasks split. Everything else is returned whole,
	    because a summary that happens to contain a blank line is one summary
	    and chopping it into two would be a silent corruption of the answer. */
	QStringList splitResults(const QString& text) const
	{
		const QString trimmed = text.trimmed();
		if (trimmed.isEmpty())
			return QStringList();
		if (!m_multiResult)
			return QStringList { trimmed };

		QStringList out;
		const QStringList lines = trimmed.split(QLatin1Char('\n'));
		for (const QString& line : lines)
		{
			QString candidate = line.trimmed();
			if (candidate.isEmpty())
				continue;
			// Models number things however often they are asked not to.
			static const QRegularExpression leadIn(QStringLiteral("^\\s*(?:[0-9]+\\s*[.)\\]]|[-*•])\\s*"));
			candidate.remove(leadIn);
			candidate = candidate.trimmed();
			if (!candidate.isEmpty())
				out << candidate;
		}
		return out.isEmpty() ? QStringList { trimmed } : out;
	}

public:
	//! Set per request: only the headline task wants its answer split up.
	bool m_multiResult {false};

private:
	std::shared_ptr<const AITextProtocol> m_protocol;
	QNetworkAccessManager* m_nam {nullptr};
	QPointer<QNetworkReply> m_reply;
	QTimer* m_timer {nullptr};
	QTimer* m_graceTimer {nullptr};
	QByteArray m_body;
	int m_httpStatus {0};
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
	bool m_cancelled {false};
	bool m_timedOut {false};
	bool m_isTest {false};
	bool m_reported {false};
};

int AITextHttpService::maxImageEdge()
{
	return MaxImageEdge;
}

QByteArray AITextHttpService::encodeImage(const QImage& image)
{
	if (image.isNull())
		return QByteArray();

	QImage sent = image;
	const int longest = qMax(sent.width(), sent.height());
	if (longest > MaxImageEdge)
	{
		// Every one of these providers downscales past its own limit anyway and
		// bills for the pixels it was sent, so shrinking here costs nothing and
		// saves both the upload and the tokens.
		sent = sent.scaled(QSize(MaxImageEdge, MaxImageEdge), Qt::KeepAspectRatio,
		                   Qt::SmoothTransformation);
	}
	if (sent.isNull())
		return QByteArray();
	if (sent.format() != QImage::Format_RGB32 && sent.format() != QImage::Format_ARGB32)
		sent = sent.convertToFormat(QImage::Format_RGB32);

	QByteArray jpeg;
	QBuffer buffer(&jpeg);
	if (!buffer.open(QIODevice::WriteOnly))
		return QByteArray();
	if (!sent.save(&buffer, "JPEG", JpegQuality))
		return QByteArray();
	buffer.close();
	return jpeg.isEmpty() ? QByteArray() : jpeg.toBase64();
}

AITextHttpService::AITextHttpService(std::shared_ptr<const AITextProtocol> protocol,
                                     const QString& apiKey, const QString& model,
                                     int timeoutSeconds, QObject* parent,
                                     const QString& apiBase)
	: AITextService(parent),
	  m_protocol(std::move(protocol)),
	  m_apiKey(apiKey),
	  m_model(model),
	  m_apiBase(apiBase),
	  m_timeoutSeconds(timeoutSeconds)
{
	m_worker = new AITextHttpWorker(m_protocol);
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	connect(m_worker, &AITextHttpWorker::completed, this, &AITextService::completed);
	connect(m_worker, &AITextHttpWorker::failed, this, &AITextService::failed);
	connect(m_worker, &AITextHttpWorker::progressUpdate, this, &AITextService::progressUpdate);
	connect(m_worker, &AITextHttpWorker::tested, this, &AITextService::connectionTested);

	m_thread.start();
	setCredentials(m_apiKey, m_model, m_timeoutSeconds);
}

AITextHttpService::~AITextHttpService()
{
	// Stop anything in flight before the thread goes, or the reply outlives the
	// manager that owns it.
	cancel();
	m_thread.quit();
	m_thread.wait();
}

QString AITextHttpService::name() const
{
	return tr("%1 (%2)").arg(m_protocol->providerName(), m_protocol->displayNameFor(m_model));
}

QStringList AITextHttpService::supportedTasks() const
{
	return m_protocol->supportedTasks();
}

bool AITextHttpService::supportsVision() const
{
	return m_protocol->supportsVision();
}

void AITextHttpService::setCredentials(const QString& apiKey, const QString& model, int timeoutSeconds)
{
	m_apiKey = apiKey;
	m_model = model;
	m_timeoutSeconds = timeoutSeconds;
	QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
	                          Q_ARG(QString, m_apiKey), Q_ARG(QString, m_model),
	                          Q_ARG(int, m_timeoutSeconds), Q_ARG(QString, m_apiBase));
}

/*! \brief Report a failure the same way a remote one arrives.

    The checks below happen here rather than after a round trip, because there
    is no sense spending a call to be told the picture was missing. But a caller
    that connects a slot and then calls execute() must not have that slot run
    inside its own call - re-entering the caller mid-statement is how a service
    gets deleted from under itself. So a local refusal is posted, exactly like a
    reply from the far end, and every path out of execute() is asynchronous. */
void AITextHttpService::failLater(const QString& message)
{
	QMetaObject::invokeMethod(this, [this, message]() {
		emit failed(message);
	}, Qt::QueuedConnection);
}

void AITextHttpService::execute(const Request& req)
{
	if (!supportedTasks().contains(req.task))
	{
		failLater(tr("%1 cannot do that here.").arg(m_protocol->providerName()));
		return;
	}
	if (AITextService::taskNeedsImage(req.task))
	{
		if (!supportsVision())
		{
			failLater(tr("%1 cannot look at pictures. Pick a model that can in "
			             "Preferences > AI Services.").arg(m_protocol->displayNameFor(m_model)));
			return;
		}
		if (req.inputImage.isNull())
		{
			failLater(tr("That needs a picture, and the selected frame has none."));
			return;
		}
	}
	else if (req.inputText.trimmed().isEmpty())
	{
		failLater(tr("That needs some text, and the selected frame is empty."));
		return;
	}

	// Only the headline task wants its answer broken into options.
	m_worker->m_multiResult = (req.task == QLatin1String(AITextService::TaskHeadline));
	QMetaObject::invokeMethod(m_worker, "execute", Qt::QueuedConnection,
	                          Q_ARG(QString, req.task), Q_ARG(QString, req.inputText),
	                          Q_ARG(QImage, req.inputImage), Q_ARG(QVariantMap, req.parameters));
}

void AITextHttpService::cancel()
{
	QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection);
}

void AITextHttpService::testConnection()
{
	QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection);
}

#include "aitexthttpservice.moc"
