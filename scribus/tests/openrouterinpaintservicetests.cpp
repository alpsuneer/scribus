/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "openrouterinpaintservicetests.h"
#include "ai/openrouterinpaintservice.h"

namespace
{
	//! The key used throughout. Distinctive on purpose, so that the
	//! confidentiality test can look for it in places it must never be.
	const char* const TestKey = "sk-or-v1-TESTKEYDONOTLEAKME0123456789";

	/*!
	 \brief Just enough HTTP to answer one request at a time.

	 Same shape as the mock in lamainpaintservicetests.cpp - Qt's QHttpServer
	 module is not present in this build - but it keeps the whole request head
	 as well, because half of what matters here is which headers were sent.
	 */
	class MockOpenRouter : public QTcpServer
	{
	public:
		struct Request
		{
			QByteArray method;
			QByteArray path;
			QByteArray head;     //!< every header line, verbatim
			QByteArray body;

			QByteArray header(const char* name) const
			{
				const QList<QByteArray> lines = head.split('\n');
				const QByteArray want = QByteArray(name).toLower() + ":";
				for (const QByteArray& line : lines)
				{
					const QByteArray trimmed = line.trimmed();
					if (trimmed.toLower().startsWith(want))
						return trimmed.mid(want.size()).trimmed();
				}
				return QByteArray();
			}

			QJsonObject json() const
			{
				return QJsonDocument::fromJson(body).object();
			}
		};

		int status {200};
		QByteArray contentType {"application/json"};
		QByteArray body;
		bool hang {false};
		int delayMs {0};
		/*! \brief Answer with the head and the first few bytes of the body,
		    then go quiet with the connection still open.

		    This is what a fast rejection from the live API looked like, and it
		    is the shape none of the other tests here produce: they all send a
		    complete response and close, so the reply reaches finished() and
		    every code path is the happy one. A response whose head has arrived
		    but whose body never completes used to sit until the timeout and be
		    reported as "did not answer", which is both wrong and unactionable -
		    the server had answered, in milliseconds, and said why. */
		bool stallAfterHead {false};

		QList<Request> received;
		int disconnectedEarly {0};

		bool startOnAnyPort() { return listen(QHostAddress::LocalHost, 0); }

		//! What the service is handed as its API root.
		QString apiBase() const
		{
			return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
		}

	protected:
		void incomingConnection(qintptr descriptor) override
		{
			auto* socket = new QTcpSocket(this);
			if (!socket->setSocketDescriptor(descriptor))
			{
				delete socket;
				return;
			}
			auto* buffer = new QByteArray;
			auto* answered = new bool(false);

			connect(socket, &QTcpSocket::disconnected, this, [this, socket, buffer, answered]() {
				if (!*answered)
					++disconnectedEarly;
				delete buffer;
				delete answered;
				socket->deleteLater();
			});

			connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, answered]() {
				if (*answered)
					return;
				buffer->append(socket->readAll());

				const int headerEnd = buffer->indexOf("\r\n\r\n");
				if (headerEnd < 0)
					return;

				const QByteArray head = buffer->left(headerEnd);
				const QList<QByteArray> lines = head.split('\n');
				if (lines.isEmpty())
					return;

				int contentLength = 0;
				for (const QByteArray& line : lines)
				{
					const QByteArray trimmed = line.trimmed();
					if (trimmed.toLower().startsWith("content-length:"))
						contentLength = trimmed.mid(15).trimmed().toInt();
				}
				if (buffer->size() - (headerEnd + 4) < contentLength)
					return;

				Request request;
				const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
				if (requestLine.size() >= 2)
				{
					request.method = requestLine.at(0);
					request.path = requestLine.at(1);
				}
				request.head = head;
				request.body = buffer->mid(headerEnd + 4, contentLength);
				received.append(request);

				if (hang)
					return;

				auto reply = [this, socket, answered]() {
					if (!socket || socket->state() != QAbstractSocket::ConnectedState)
						return;
					*answered = true;
					QByteArray out;
					out += "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
					out += "Content-Type: " + contentType + "\r\n";
					out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
					out += "Connection: close\r\n\r\n";
					// Truncating the body while promising the full Content-Length
					// is what leaves the reply unfinished. The connection stays
					// open, so there is no error either - just silence.
					out += stallAfterHead ? body.left(qMin(4, body.size())) : body;
					socket->write(out);
					socket->flush();
					if (!stallAfterHead)
						socket->disconnectFromHost();
				};
				if (delayMs > 0)
					QTimer::singleShot(delayMs, socket, reply);
				else
					reply();
			});
		}
	};

	QImage sampleImage(int w = 60, int h = 60)
	{
		QImage img(w, h, QImage::Format_RGB32);
		for (int y = 0; y < h; ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < w; ++x)
				line[x] = qRgb(10, 200, 10);   // green, so red overlay shows up
		}
		return img;
	}

	QImage sampleMask(int w = 60, int h = 60)
	{
		QImage mask(w, h, QImage::Format_Grayscale8);
		mask.fill(0);
		for (int y = 20; y < 40; ++y)
			for (int x = 20; x < 40; ++x)
				mask.scanLine(y)[x] = 255;
		return mask;
	}

	QByteArray pngOf(const QImage& image)
	{
		QByteArray out;
		QBuffer buffer(&out);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");
		return out;
	}

	//! A success envelope of the documented shape.
	QByteArray successBody(const QImage& image)
	{
		QJsonObject entry;
		entry.insert("b64_json", QString::fromLatin1(pngOf(image).toBase64()));
		entry.insert("media_type", "image/png");
		QJsonArray data;
		data.append(entry);
		QJsonObject usage;
		usage.insert("cost", 0.04);
		QJsonObject root;
		root.insert("created", 1787764532);
		root.insert("data", data);
		root.insert("usage", usage);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! The documented error envelope.
	QByteArray errorBody(int code, const QString& message, const QJsonObject& metadata = QJsonObject())
	{
		QJsonObject error;
		error.insert("code", code);
		error.insert("message", message);
		if (!metadata.isEmpty())
			error.insert("metadata", metadata);
		QJsonObject root;
		root.insert("error", error);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! The one reference image out of a request body, decoded.
	QImage referenceImageOf(const QJsonObject& body)
	{
		const QJsonArray refs = body.value("input_references").toArray();
		if (refs.isEmpty())
			return QImage();
		const QString url = refs.at(0).toObject().value("image_url").toObject().value("url").toString();
		const int comma = url.indexOf(QLatin1Char(','));
		if (comma < 0)
			return QImage();
		QImage out;
		out.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()));
		return out;
	}

	//! Build a service pointed at \a server. Never at the real API.
	OpenRouterInpaintService* serviceFor(MockOpenRouter& server,
	                                     const QString& model = QStringLiteral("openai/gpt-image-2"),
	                                     int timeout = 5,
	                                     const QString& key = QString::fromLatin1(TestKey))
	{
		return new OpenRouterInpaintService(key, model, timeout, nullptr, server.apiBase());
	}
}

// --- the parts that need no server ------------------------------------------

void OpenRouterInpaintServiceTests::testCuratedModelListIsWellFormed()
{
	const QList<OpenRouterInpaintService::ModelChoice>& models = OpenRouterInpaintService::models();
	QVERIFY(!models.isEmpty());

	QSet<QString> ids;
	for (const OpenRouterInpaintService::ModelChoice& choice : models)
	{
		// A vendor prefix is not decoration: OpenRouter routes on it, and an
		// id without one is not a model that exists.
		QVERIFY2(choice.id.contains(QLatin1Char('/')), qPrintable(choice.id));
		QVERIFY(!choice.displayName.isEmpty());
		QVERIFY(!choice.hint.isEmpty());
		QVERIFY2(!ids.contains(choice.id), qPrintable(choice.id));
		ids.insert(choice.id);
	}

	// The default written by PrefsManager has to be one the dropdown offers,
	// or a fresh profile starts out selecting nothing.
	QVERIFY(ids.contains(QStringLiteral("google/gemini-3.1-flash-image-preview")));
	QCOMPARE(OpenRouterInpaintService::displayNameFor(QStringLiteral("openai/gpt-image-2")),
	         QStringLiteral("GPT Image 2"));
	// Something not in the list is shown as itself rather than as nothing.
	QCOMPARE(OpenRouterInpaintService::displayNameFor(QStringLiteral("who/what")),
	         QStringLiteral("who/what"));
}

void OpenRouterInpaintServiceTests::testShortAndFileNamesForModels()
{
	QCOMPARE(OpenRouterInpaintService::shortNameFor(QStringLiteral("google/gemini-3-pro-image")),
	         QStringLiteral("gemini-3-pro-image"));
	QCOMPARE(OpenRouterInpaintService::shortNameFor(QStringLiteral("novendor")),
	         QStringLiteral("novendor"));

	QCOMPARE(OpenRouterInpaintService::fileTagFor(QStringLiteral("google/gemini-3-pro-image")),
	         QStringLiteral("gemini_3_pro_image"));
	// A dot is a legal filename character and carries the version, so it stays.
	QCOMPARE(OpenRouterInpaintService::fileTagFor(QStringLiteral("black-forest-labs/flux.2-flex")),
	         QStringLiteral("flux.2_flex"));
	// Anything that could make a path mean something else does not survive.
	QCOMPARE(OpenRouterInpaintService::fileTagFor(QStringLiteral("x/a b:c")),
	         QStringLiteral("a_b_c"));
}

void OpenRouterInpaintServiceTests::testRefusalMarkerRoundTrips()
{
	const QString text = QStringLiteral("I can't edit images of real people.");
	const QString marked = OpenRouterInpaintService::refusalMarker() + text;

	QVERIFY(OpenRouterInpaintService::isRefusal(marked));
	QCOMPARE(OpenRouterInpaintService::strippedRefusal(marked), text);

	// An ordinary failure is not a refusal, and stripping it changes nothing.
	QVERIFY(!OpenRouterInpaintService::isRefusal(text));
	QCOMPARE(OpenRouterInpaintService::strippedRefusal(text), text);

	// A refusal is not a cancel. The caller tells these apart to decide
	// whether to say anything at all.
	QVERIFY(!AIInpaintService::isCancelled(marked));
}

// --- the request ------------------------------------------------------------

void OpenRouterInpaintServiceTests::testRequestWireFormat()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	OpenRouterInpaintService service(QString::fromLatin1(TestKey),
	                                 QStringLiteral("openai/gpt-image-2"), 5, nullptr, server.apiBase());
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const MockOpenRouter::Request& request = server.received.first();
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/images"));
	QVERIFY(request.header("content-type").startsWith("application/json"));

	const QJsonObject body = request.json();
	QCOMPARE(body.value("model").toString(), QStringLiteral("openai/gpt-image-2"));
	QVERIFY(body.value("prompt").toString().contains(QStringLiteral("red")));

	// The field is input_references, not reference_images, and each entry is
	// an object rather than a bare string. Both of those were guessed wrong
	// before the live API was consulted - this is the test that pins them.
	QVERIFY(body.contains("input_references"));
	QVERIFY(!body.contains("reference_images"));
	const QJsonArray refs = body.value("input_references").toArray();
	QCOMPARE(refs.size(), 1);
	const QJsonObject ref = refs.at(0).toObject();
	QCOMPARE(ref.value("type").toString(), QStringLiteral("image_url"));
	QVERIFY(ref.value("image_url").toObject().value("url").toString()
	        .startsWith(QStringLiteral("data:image/jpeg;base64,")));

	// There is no modalities field on this endpoint.
	QVERIFY(!body.contains("modalities"));
}

void OpenRouterInpaintServiceTests::testAuthAndAttributionHeaders()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	const MockOpenRouter::Request& request = server.received.first();
	QCOMPARE(request.header("authorization"), QByteArray("Bearer ") + TestKey);
	// OpenRouter attributes traffic on its public rankings with these. They
	// name Scribus and carry nothing about the user or the document.
	QCOMPARE(request.header("http-referer"), QByteArray("https://scribus.net"));
	QCOMPARE(request.header("x-title"), QByteArray("Scribus"));
}

void OpenRouterInpaintServiceTests::testModelRoutingChangesOnlyTheModelField()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<OpenRouterInpaintService> service(
		serviceFor(server, QStringLiteral("google/gemini-3.1-flash-image-preview")));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);

	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	// The model can change between operations without re-entering the key.
	service->setCredentials(QString::fromLatin1(TestKey), QStringLiteral("bytedance-seed/seedream-4.5"), 5);
	service->inpaint(sampleImage(), sampleMask());
	QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);

	QCOMPARE(server.received.size(), 2);
	QJsonObject first = server.received.at(0).json();
	QJsonObject second = server.received.at(1).json();

	QCOMPARE(first.value("model").toString(), QStringLiteral("google/gemini-3.1-flash-image-preview"));
	QCOMPARE(second.value("model").toString(), QStringLiteral("bytedance-seed/seedream-4.5"));
	QCOMPARE(server.received.at(0).header("authorization"),
	         server.received.at(1).header("authorization"));

	// Only the model moved. Take it out of both and the requests are identical.
	first.remove("model");
	second.remove("model");
	QCOMPARE(QJsonDocument(first).toJson(QJsonDocument::Compact),
	         QJsonDocument(second).toJson(QJsonDocument::Compact));
}

void OpenRouterInpaintServiceTests::testMaskIsPaintedIntoTheReferenceImage()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	// These models take no mask channel, so the mask has to be visible in the
	// picture itself or nothing tells the model where to work.
	const QImage sent = referenceImageOf(server.received.first().json());
	QVERIFY(!sent.isNull());
	QCOMPARE(sent.size(), QSize(60, 60));

	const QColor inside = sent.pixelColor(30, 30);    // masked
	const QColor outside = sent.pixelColor(5, 5);     // untouched

	// Red inside. JPEG is lossy, so this asks for the colour it plainly is
	// rather than for an exact value.
	QVERIFY2(inside.red() > 180 && inside.green() < 90 && inside.blue() < 90,
	         qPrintable(inside.name()));
	// Still the original green outside: the overlay must not bleed over the
	// rest of the picture.
	QVERIFY2(outside.green() > 150 && outside.red() < 90, qPrintable(outside.name()));
}

// --- the answer -------------------------------------------------------------

void OpenRouterInpaintServiceTests::testSuccessReturnsTheModelsImage()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	QImage answer(60, 60, QImage::Format_RGB32);
	answer.fill(qRgb(1, 2, 3));
	server.body = successBody(answer);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);

	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));
	QCOMPARE(failed.size(), 0);

	const QImage got = done.first().at(0).value<QImage>();
	QCOMPARE(got.size(), QSize(60, 60));
	QCOMPARE(got.convertToFormat(QImage::Format_RGB32).pixel(10, 10), qRgb(1, 2, 3));
}

void OpenRouterInpaintServiceTests::testDataUrlResultIsAlsoAccepted()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	QImage answer(20, 20, QImage::Format_RGB32);
	answer.fill(qRgb(9, 9, 9));

	// Some models answer with a data: URL in "url" rather than "b64_json".
	QJsonObject entry;
	entry.insert("url", QStringLiteral("data:image/png;base64,")
	                    + QString::fromLatin1(pngOf(answer).toBase64()));
	QJsonArray data;
	data.append(entry);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));
	QCOMPARE(done.first().at(0).value<QImage>().size(), QSize(20, 20));
}

void OpenRouterInpaintServiceTests::testRemoteUrlResultIsRefusedRatherThanFetched()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	QJsonObject entry;
	entry.insert("url", QStringLiteral("https://cdn.example.com/result.png"));
	QJsonArray data;
	data.append(entry);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	// The rule is that this service talks to one host. A link in a response is
	// not a good enough reason to go and talk to another one, so it says so
	// rather than quietly fetching it.
	QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("link")));
	QCOMPARE(server.received.size(), 1);   // no second request went anywhere
}

void OpenRouterInpaintServiceTests::testTextOnlyAnswerIsReportedAsARefusal()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	// 200 OK, no image, just the model explaining itself. This is what a
	// safety refusal usually looks like.
	QJsonObject entry;
	entry.insert("text", QStringLiteral("I cannot edit photographs of identifiable people."));
	QJsonArray data;
	data.append(entry);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));
	QCOMPARE(done.size(), 0);

	const QString error = failed.first().at(0).toString();
	QVERIFY(OpenRouterInpaintService::isRefusal(error));
	// The model's own words, not a summary of them.
	QVERIFY(OpenRouterInpaintService::strippedRefusal(error)
	        .contains(QStringLiteral("identifiable people")));
	// And which model said it, since the answer is to try another one.
	QVERIFY(OpenRouterInpaintService::strippedRefusal(error)
	        .contains(QStringLiteral("GPT Image 2")));

	// Nothing was retried. A refusal is a decision, not a hiccup.
	QCOMPARE(server.received.size(), 1);
}

void OpenRouterInpaintServiceTests::testModerationErrorIsReportedAsARefusal()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 403;

	QJsonObject metadata;
	metadata.insert("reasons", QJsonArray { QStringLiteral("violence") });
	metadata.insert("provider_name", QStringLiteral("OpenAI"));
	metadata.insert("model_slug", QStringLiteral("openai/gpt-image-2"));
	server.body = errorBody(403, QStringLiteral("Flagged by moderation"), metadata);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY(OpenRouterInpaintService::isRefusal(error));
	QVERIFY(OpenRouterInpaintService::strippedRefusal(error).contains(QStringLiteral("violence")));
	QCOMPARE(server.received.size(), 1);
}

// --- the HTTP statuses that mean something a user can act on ----------------

void OpenRouterInpaintServiceTests::testUnauthorisedMentionsTheApiKey()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 401;
	server.body = errorBody(401, QStringLiteral("No auth credentials found"));

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("API key")), qPrintable(error));
	QVERIFY2(error.contains(QStringLiteral("Preferences")), qPrintable(error));
	// Saying which key was rejected would put it on the screen.
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

void OpenRouterInpaintServiceTests::testPaymentRequiredMentionsCredits()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 402;
	server.body = errorBody(402, QStringLiteral("Insufficient credits"));

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("credits")), qPrintable(error));
}

void OpenRouterInpaintServiceTests::testRateLimitIsReported()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 429;
	server.body = errorBody(429, QStringLiteral("Rate limit exceeded"));

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive), qPrintable(error));
}

void OpenRouterInpaintServiceTests::testServerErrorNamesTheProvider()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 502;

	QJsonObject metadata;
	metadata.insert("provider_name", QStringLiteral("Black Forest Labs"));
	metadata.insert("error_type", QStringLiteral("provider_error"));
	server.body = errorBody(502, QStringLiteral("Provider returned an invalid response"), metadata);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	// Which vendor is down is the useful half of a 502: it tells the user
	// whether another model would get them past it.
	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("Black Forest Labs")), qPrintable(error));
	QVERIFY2(error.contains(QStringLiteral("502")), qPrintable(error));
}

// --- giving up, and being told to stop --------------------------------------

void OpenRouterInpaintServiceTests::testMissingKeyIsRefusedBeforeAnyRequest()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	QScopedPointer<OpenRouterInpaintService> service(
		serviceFor(server, QStringLiteral("openai/gpt-image-2"), 5, QString()));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("API key")));
	// Nothing was sent. There is no point spending a round trip to be told
	// what we already know.
	QCOMPARE(server.received.size(), 0);
}

/*!
 \brief A rejection that arrives at once must be reported at once.

 The regression this pins: OpenRouter answered 402 in well under a second, and
 Scribus said "OpenRouter did not answer within 60 seconds". The response head
 had arrived immediately and carried both the status and the reason; nothing
 read it, because the only thing that read the response at all ran on
 finished(), and this reply never finished.

 The service is given the production sixty-second timeout on purpose. If the
 timeout is what ends the request, this test takes a minute and fails; passing
 inside two seconds is the whole assertion.
 */
void OpenRouterInpaintServiceTests::testFastRejectionDoesNotBecomeATimeout()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 402;
	server.body = errorBody(402, QStringLiteral("Insufficient credits"));
	server.stallAfterHead = true;

	QScopedPointer<OpenRouterInpaintService> service(
		serviceFor(server, QStringLiteral("openai/gpt-image-2"), 60));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);

	QElapsedTimer elapsed;
	elapsed.start();
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY2(failed.wait(2000), "a 402 that arrived in milliseconds was still not reported after two seconds");
	QVERIFY2(elapsed.elapsed() < 2000, qPrintable(QString::number(elapsed.elapsed())));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("credits")), qPrintable(error));
	// The two ways of saying the wrong thing. Either would mean the request
	// fell through to the timeout again.
	QVERIFY2(!error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("timeout"), Qt::CaseInsensitive), qPrintable(error));
	// A failure is not a cancel either.
	QVERIFY(!AIInpaintService::isCancelled(error));
}

void OpenRouterInpaintServiceTests::testErrorStatusesAreReportedPromptly_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QString>("serverMessage");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<bool>("stall");

	// Every status a user can meet, answered the way the live API answered the
	// 402: head first, body never completed. None of them may wait for the
	// timeout, and each has to keep saying the thing that tells the user what
	// to do about it.
	QTest::newRow("400 bad request")  << 400 << QStringLiteral("Invalid model")             << QStringLiteral("400")         << true;
	QTest::newRow("402 no credits")   << 402 << QStringLiteral("Insufficient credits")      << QStringLiteral("credits")     << true;
	QTest::newRow("403 forbidden")    << 403 << QStringLiteral("Forbidden")                 << QStringLiteral("403")         << true;
	QTest::newRow("429 rate limited") << 429 << QStringLiteral("Rate limit exceeded")       << QStringLiteral("rate limit")  << true;
	QTest::newRow("500 server error") << 500 << QStringLiteral("Internal server error")     << QStringLiteral("server error")<< true;
	QTest::newRow("503 unavailable")  << 503 << QStringLiteral("Service unavailable")       << QStringLiteral("server error")<< true;

	/* 401 is not stalled, and that is a limitation rather than an oversight.
	   401 and 407 are the two HTTP authentication statuses, and Qt withholds
	   the whole response for them - no metaDataChanged, no readyRead, no
	   errorOccurred, not even authenticationRequired - until the response body
	   is complete, because it may have to resend the request with credentials.
	   A 401 whose body never completes therefore offers the client nothing to
	   act on and can only end at the timeout; measured on Qt 6.8.2, where every
	   other status above fired metaDataChanged within 3 ms and 401 and 407
	   fired nothing at all.

	   This is not the case that bit us: OpenRouter's error bodies are small and
	   arrive whole, which is the row below, and it has to be prompt too. */
	QTest::newRow("401 bad key")      << 401 << QStringLiteral("No auth credentials found") << QStringLiteral("API key")     << false;
}

void OpenRouterInpaintServiceTests::testErrorStatusesAreReportedPromptly()
{
	QFETCH(int, status);
	QFETCH(QString, serverMessage);
	QFETCH(QString, expected);
	QFETCH(bool, stall);

	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = errorBody(status, serverMessage);
	server.stallAfterHead = stall;

	QScopedPointer<OpenRouterInpaintService> service(
		serviceFor(server, QStringLiteral("openai/gpt-image-2"), 60));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);

	QElapsedTimer elapsed;
	elapsed.start();
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY2(failed.wait(2000), qPrintable(QStringLiteral("status %1 was not reported within two seconds").arg(status)));
	QVERIFY2(elapsed.elapsed() < 2000, qPrintable(QString::number(elapsed.elapsed())));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(expected, Qt::CaseInsensitive), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("timeout"), Qt::CaseInsensitive), qPrintable(error));
	// Nothing the user sent may come back out in the message.
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

void OpenRouterInpaintServiceTests::testTimeoutIsReported()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;

	QScopedPointer<OpenRouterInpaintService> service(
		serviceFor(server, QStringLiteral("openai/gpt-image-2"), 1));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(10000));

	const QString error = failed.first().at(0).toString();
	// A timeout is not a cancel, and must not be reported as one.
	QVERIFY(!AIInpaintService::isCancelled(error));
	QVERIFY2(error.contains(QStringLiteral("did not answer")), qPrintable(error));

	// The request really was dropped rather than left running.
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

void OpenRouterInpaintServiceTests::testCancelAbortsAndStaysQuiet()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.delayMs = 4000;
	server.body = successBody(sampleImage());

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server, QStringLiteral("openai/gpt-image-2"), 30));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);

	service->inpaint(sampleImage(), sampleMask());
	QTRY_VERIFY_WITH_TIMEOUT(server.received.size() == 1, 5000);
	service->cancel();

	QVERIFY(failed.wait(5000));
	QCOMPARE(done.size(), 0);

	// Cancelling still signals - a caller that got nothing would wait for ever
	// - but it signals the marker, not an error anybody is shown.
	const QString error = failed.first().at(0).toString();
	QVERIFY(AIInpaintService::isCancelled(error));
	QVERIFY(!OpenRouterInpaintService::isRefusal(error));

	// And the far end was actually told, rather than merely ignored.
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

// --- Test Connection --------------------------------------------------------

void OpenRouterInpaintServiceTests::testConnectionReportsRemainingCredit()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());

	QJsonObject data;
	data.insert("limit_remaining", 8.25);
	data.insert("usage", 1.75);
	data.insert("is_free_tier", false);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AIInpaintService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));

	QCOMPARE(tested.first().at(0).toBool(), true);
	// The credit check endpoint is /key. It is not /auth/key, which is what
	// this was first written against and which does not exist.
	QCOMPARE(server.received.first().path, QByteArray("/key"));
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	// Naming the balance is what makes the button worth pressing.
	QVERIFY2(tested.first().at(1).toString().contains(QStringLiteral("8.25")),
	         qPrintable(tested.first().at(1).toString()));
}

void OpenRouterInpaintServiceTests::testConnectionFailsOnBadKey()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 401;
	server.body = errorBody(401, QStringLiteral("Invalid API key"));

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AIInpaintService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));

	QCOMPARE(tested.first().at(0).toBool(), false);
	QVERIFY(tested.first().at(1).toString().contains(QStringLiteral("API key")));
}

// --- the key ----------------------------------------------------------------

void OpenRouterInpaintServiceTests::testApiKeyNeverLeavesTheAuthorizationHeader()
{
	MockOpenRouter server;
	QVERIFY(server.startOnAnyPort());
	server.status = 401;
	server.body = errorBody(401, QStringLiteral("No auth credentials found"));

	QScopedPointer<OpenRouterInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const MockOpenRouter::Request& request = server.received.first();
	const QByteArray key(TestKey);

	// In the one place it belongs.
	QVERIFY(request.header("authorization").contains(key));
	// And nowhere else: not in the path, not in a query string, not in the
	// body, and not in the message the user is about to be shown.
	QVERIFY(!request.path.contains(key));
	QVERIFY(!request.body.contains(key));
	QVERIFY(!failed.first().at(0).toString().toUtf8().contains(key));
	QVERIFY(!service->name().toUtf8().contains(key));

	// Every header line except Authorization is clean, so a proxy log or a
	// crash dump of the request head cannot pick it up from somewhere else.
	const QList<QByteArray> lines = request.head.split('\n');
	for (const QByteArray& line : lines)
	{
		if (line.trimmed().toLower().startsWith("authorization:"))
			continue;
		QVERIFY2(!line.contains(key), qPrintable(QString::fromUtf8(line)));
	}
}

// Guiless: these tests need an event loop for the sockets, and nothing else.
// QTEST_MAIN would drag in QApplication and the whole widget stack with it.
QTEST_GUILESS_MAIN(OpenRouterInpaintServiceTests)
