/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/lamainpaintservice.h"

#include <QBuffer>
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
	//! Paths on the IOPaint server. See the wire-format note in the header.
	const QLatin1String InpaintPath("/api/v1/inpaint");
	const QLatin1String ModelPath("/api/v1/model");

	//! PNG-encode for the wire. Anything with an alpha channel is flattened to
	//! RGB first: the server has no use for it and it would only be carried
	//! there and back.
	QByteArray encodePng(const QImage& image, bool asGrey)
	{
		QImage out = image;
		if (asGrey)
		{
			if (out.format() != QImage::Format_Grayscale8)
				out = out.convertToFormat(QImage::Format_Grayscale8);
		}
		else if (out.format() != QImage::Format_RGB888)
		{
			out = out.convertToFormat(QImage::Format_RGB888);
		}
		if (out.isNull())
			return QByteArray();

		QByteArray png;
		QBuffer buffer(&png);
		if (!buffer.open(QIODevice::WriteOnly))
			return QByteArray();
		if (!out.save(&buffer, "PNG"))
			return QByteArray();
		buffer.close();
		return png;
	}

	/*! \brief The real reason out of an IOPaint error body.

	    It answers failures as JSON with the useful text in "errors" and rather
	    less useful text in "detail" and "error", so they are tried in that
	    order before falling back to whatever the transport said. */
	QString describeError(const QByteArray& body, const QString& fallback)
	{
		if (!body.isEmpty())
		{
			QJsonParseError parseError {};
			const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
			if (parseError.error == QJsonParseError::NoError && doc.isObject())
			{
				const QJsonObject obj = doc.object();
				for (const QLatin1String key : { QLatin1String("errors"),
				                                 QLatin1String("detail"),
				                                 QLatin1String("error") })
				{
					const QString value = obj.value(key).toString().trimmed();
					if (!value.isEmpty())
						return value;
				}
			}
			// Not JSON, or JSON with nothing to say: show a little of it
			// rather than swallowing the only evidence there is.
			const QString text = QString::fromUtf8(body.left(300)).trimmed();
			if (!text.isEmpty())
				return text;
		}
		return fallback;
	}
}

/*!
 \brief The half of LamaInpaintService that lives on the private thread.

 It owns the network access manager, because a QNetworkAccessManager belongs to
 the thread that created it, and it does the encoding and decoding so that none
 of that lands on the GUI thread either.
 */
class LamaInpaintWorker : public QObject
{
	Q_OBJECT

public:
	LamaInpaintWorker() = default;

public slots:
	void configure(const QString& baseUrl, int timeoutSeconds)
	{
		m_baseUrl = baseUrl;
		m_timeoutSeconds = timeoutSeconds;
	}

	void testConnection()
	{
		if (!beginRequest(true))
			return;
		QNetworkReply* reply = m_nam->get(QNetworkRequest(QUrl(m_baseUrl + ModelPath)));
		trackReply(reply, /*isTest*/ true);
	}

	void inpaint(const QImage& image, const QImage& mask)
	{
		if (!beginRequest(false))
			return;

		const QByteArray imagePng = encodePng(image, false);
		const QByteArray maskPng = encodePng(mask, true);
		if (imagePng.isEmpty() || maskPng.isEmpty())
		{
			finishRequest();
			emit failed(tr("Could not encode the image for the inpainting server."));
			return;
		}

		QJsonObject body;
		body.insert(QStringLiteral("image"), QString::fromLatin1(imagePng.toBase64()));
		body.insert(QStringLiteral("mask"), QString::fromLatin1(maskPng.toBase64()));

		QNetworkRequest request { QUrl(m_baseUrl + InpaintPath) };
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		QNetworkReply* reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		trackReply(reply, /*isTest*/ false);
	}

	void cancel()
	{
		if (!m_reply)
			return;
		m_cancelled = true;
		// abort() rather than merely disconnecting: the point of Cancel is to
		// stop the far end working, not to stop listening to it.
		m_reply->abort();
	}

signals:
	void finished(const QImage& result);
	void failed(const QString& error);
	void tested(bool ok, const QString& detail);

private:
	//! Set up for a request, refusing if one is already running.
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
		m_cancelled = false;
		m_timedOut = false;
		return true;
	}

	void trackReply(QNetworkReply* reply, bool isTest)
	{
		m_reply = reply;

		// An explicit timer rather than setTransferTimeout(), because a
		// timeout and a user cancel both come back as OperationCanceledError
		// and the two have to be told apart to report either of them honestly.
		if (!m_timer)
		{
			m_timer = new QTimer(this);
			m_timer->setSingleShot(true);
			connect(m_timer, &QTimer::timeout, this, [this]() {
				if (!m_reply)
					return;
				m_timedOut = true;
				m_reply->abort();
			});
		}
		m_timer->start(qMax(1, m_timeoutSeconds) * 1000);

		connect(reply, &QNetworkReply::finished, this, [this, reply, isTest]() {
			handleReply(reply, isTest);
		});
	}

	void finishRequest()
	{
		if (m_timer)
			m_timer->stop();
		m_reply = nullptr;
	}

	void handleReply(QNetworkReply* reply, bool isTest)
	{
		const QNetworkReply::NetworkError netError = reply->error();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray body = reply->readAll();
		const QString transportError = reply->errorString();

		finishRequest();
		reply->deleteLater();

		const bool cancelled = m_cancelled;
		const bool timedOut = m_timedOut;
		m_cancelled = false;
		m_timedOut = false;

		if (timedOut)
		{
			const QString message = tr("The inpainting server did not answer within %1 seconds.")
			                        .arg(m_timeoutSeconds);
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
			// Never reached the server at all: no HTTP status was ever set.
			const QString message = tr("Could not reach %1: %2").arg(m_baseUrl, transportError);
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		if (status < 200 || status > 299)
		{
			const QString message = tr("The inpainting server answered %1: %2")
			                        .arg(status)
			                        .arg(describeError(body, transportError));
			if (isTest)
				emit tested(false, message);
			else
				emit failed(message);
			return;
		}

		if (isTest)
		{
			// The model endpoint answers with the loaded model's description.
			// Naming it back is what makes the test useful: it is the
			// difference between "something answered" and "lama is loaded".
			QString modelName;
			QJsonParseError parseError {};
			const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
			if (parseError.error == QJsonParseError::NoError && doc.isObject())
				modelName = doc.object().value(QStringLiteral("name")).toString();
			if (modelName.isEmpty())
				emit tested(true, tr("Connected."));
			else
				emit tested(true, tr("Connected. Model: %1").arg(modelName));
			return;
		}

		QImage result;
		if (!result.loadFromData(body))
		{
			emit failed(tr("The inpainting server answered with something that is not an image (%1 bytes).")
			            .arg(body.size()));
			return;
		}
		emit finished(result);
	}

	QNetworkAccessManager* m_nam {nullptr};
	QPointer<QNetworkReply> m_reply;
	QTimer* m_timer {nullptr};
	QString m_baseUrl;
	int m_timeoutSeconds {120};
	bool m_cancelled {false};
	bool m_timedOut {false};
};

QString LamaInpaintService::normaliseBaseUrl(const QString& url)
{
	QString trimmed = url.trimmed();
	while (trimmed.endsWith(QLatin1Char('/')))
		trimmed.chop(1);
	if (trimmed.isEmpty())
		return trimmed;
	// A bare host:port is what people actually type; without a scheme QUrl
	// reads it as a relative path and the request goes nowhere with no
	// explanation worth showing anyone.
	if (!trimmed.contains(QLatin1String("://")))
		trimmed.prepend(QLatin1String("http://"));
	return trimmed;
}

LamaInpaintService::LamaInpaintService(const QString& baseUrl, int timeoutSeconds, QObject* parent)
	: AIInpaintService(parent),
	  m_baseUrl(normaliseBaseUrl(baseUrl)),
	  m_timeoutSeconds(timeoutSeconds)
{
	m_worker = new LamaInpaintWorker;
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	connect(m_worker, &LamaInpaintWorker::finished, this, &AIInpaintService::inpaintFinished);
	connect(m_worker, &LamaInpaintWorker::failed, this, &AIInpaintService::inpaintFailed);
	connect(m_worker, &LamaInpaintWorker::tested, this, &AIInpaintService::connectionTested);

	m_thread.start();
	setEndpoint(m_baseUrl, timeoutSeconds);
}

LamaInpaintService::~LamaInpaintService()
{
	// Stop anything in flight before the thread goes, or the reply outlives
	// the manager that owns it.
	cancel();
	m_thread.quit();
	m_thread.wait();
}

QString LamaInpaintService::name() const
{
	return tr("IOPaint (LaMa)");
}

void LamaInpaintService::setEndpoint(const QString& baseUrl, int timeoutSeconds)
{
	m_baseUrl = normaliseBaseUrl(baseUrl);
	m_timeoutSeconds = timeoutSeconds;
	QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
	                          Q_ARG(QString, m_baseUrl), Q_ARG(int, m_timeoutSeconds));
}

void LamaInpaintService::testConnection()
{
	if (m_baseUrl.isEmpty())
	{
		emit connectionTested(false, tr("No server address is set."));
		return;
	}
	QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection);
}

void LamaInpaintService::inpaint(const QImage& image, const QImage& mask)
{
	if (m_baseUrl.isEmpty())
	{
		emit inpaintFailed(tr("No server address is set."));
		return;
	}
	QMetaObject::invokeMethod(m_worker, "inpaint", Qt::QueuedConnection,
	                          Q_ARG(QImage, image), Q_ARG(QImage, mask));
}

void LamaInpaintService::cancel()
{
	QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection);
}

#include "lamainpaintservice.moc"
