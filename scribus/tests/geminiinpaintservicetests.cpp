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

#include "geminiinpaintservicetests.h"
#include "ai/aiinpaintcomposite.h"
#include "ai/aiinpaintprompts.h"
#include "ai/geminiinpaintservice.h"

namespace
{
	//! The key used throughout. Distinctive on purpose, so that the
	//! confidentiality tests can look for it in places it must never be.
	const char* const TestKey = "AIzaSy-TESTKEYDONOTLEAKME-0123456789abcd";

	//! Just enough HTTP to answer one request at a time. Same shape as the
	//! mock in openrouterinpaintservicetests.cpp - Qt's QHttpServer module is
	//! not present in this build - and it keeps the whole request head as well,
	//! because half of what matters here is which headers were sent.
	class MockGemini : public QTcpServer
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

		    The shape a fast rejection took on the live OpenRouter API, which
		    used to be reported as a sixty-second timeout. This client was
		    written with that already fixed, and these tests are what keeps it
		    that way. */
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
					// Truncating the body while promising the full
					// Content-Length is what leaves the reply unfinished. The
					// connection stays open, so there is no error either.
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
				line[x] = qRgb(10, 200, 10);   // green, so a red overlay shows up
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

	//! A success envelope of the documented shape: the picture at
	//! output_image.data, and the same content mirrored under steps[].
	QByteArray successBody(const QImage& image, const QString& alsoSaid = QString())
	{
		const QString b64 = QString::fromLatin1(pngOf(image).toBase64());

		QJsonObject outputImage;
		outputImage.insert("type", "image");
		outputImage.insert("mime_type", "image/png");
		outputImage.insert("data", b64);

		QJsonArray content;
		if (!alsoSaid.isEmpty())
		{
			QJsonObject textPart;
			textPart.insert("type", "text");
			textPart.insert("text", alsoSaid);
			content.append(textPart);
		}
		content.append(outputImage);

		QJsonObject step;
		step.insert("type", "model_output");
		step.insert("content", content);
		QJsonArray steps;
		steps.append(step);

		QJsonObject root;
		root.insert("id", "interaction-1");
		root.insert("object", "interaction");
		root.insert("status", "completed");
		root.insert("model", "gemini-3.1-flash-image");
		root.insert("steps", steps);
		root.insert("output_image", outputImage);
		if (!alsoSaid.isEmpty())
			root.insert("output_text", alsoSaid);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! An answer carrying only words: what a refusal looks like.
	QByteArray textOnlyBody(const QString& said)
	{
		QJsonObject textPart;
		textPart.insert("type", "text");
		textPart.insert("text", said);
		QJsonArray content;
		content.append(textPart);
		QJsonObject step;
		step.insert("type", "model_output");
		step.insert("content", content);
		QJsonArray steps;
		steps.append(step);

		QJsonObject root;
		root.insert("object", "interaction");
		root.insert("status", "completed");
		root.insert("steps", steps);
		root.insert("output_text", said);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! Google's documented error envelope.
	QByteArray errorBody(int code, const QString& message, const QString& statusText = QString())
	{
		QJsonObject error;
		error.insert("code", code);
		error.insert("message", message);
		if (!statusText.isEmpty())
			error.insert("status", statusText);
		QJsonObject root;
		root.insert("error", error);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! The one picture out of a request body, decoded.
	QImage sentImageOf(const QJsonObject& body)
	{
		const QJsonArray input = body.value("input").toArray();
		for (const QJsonValue& value : input)
		{
			const QJsonObject part = value.toObject();
			if (part.value("type").toString() != QLatin1String("image"))
				continue;
			QImage out;
			out.loadFromData(QByteArray::fromBase64(part.value("data").toString().toLatin1()));
			return out;
		}
		return QImage();
	}

	//! Build a service pointed at \a server. Never at the real API.
	GeminiInpaintService* serviceFor(MockGemini& server,
	                                 const QString& model = QStringLiteral("gemini-3.1-flash-image"),
	                                 int timeout = 5,
	                                 const QString& key = QString::fromLatin1(TestKey))
	{
		return new GeminiInpaintService(key, model, timeout, nullptr, server.apiBase());
	}
}

// --- the parts that need no server ------------------------------------------

void GeminiInpaintServiceTests::testCuratedModelListIsWellFormed()
{
	const QList<GeminiInpaintService::ModelChoice>& models = GeminiInpaintService::models();
	QVERIFY(!models.isEmpty());

	QSet<QString> ids;
	for (const GeminiInpaintService::ModelChoice& choice : models)
	{
		QVERIFY(!choice.id.isEmpty());
		QVERIFY(!choice.displayName.isEmpty());
		QVERIFY(!choice.hint.isEmpty());
		QVERIFY2(!ids.contains(choice.id), qPrintable(choice.id));
		ids.insert(choice.id);
		/* These are Google's own ids and carry no vendor prefix. OpenRouter's
		   ids for some of the same models do ("google/gemini-3.1-flash-image"),
		   and pasting one of those here would 404: the two catalogues are not
		   interchangeable, which is worth a test because the two lists sit
		   side by side in Preferences. */
		QVERIFY2(!choice.id.contains(QLatin1Char('/')), qPrintable(choice.id));
		// Checked against https://ai.google.dev/gemini-api/docs/models on
		// 30 Aug 2026: all three are stable, and the "-preview" ids these
		// carried earlier in the year now 404.
		QVERIFY2(!choice.id.endsWith(QLatin1String("-preview")), qPrintable(choice.id));
	}

	// The default written by PrefsManager has to be one the dropdown offers,
	// or a fresh profile starts out selecting nothing.
	QVERIFY2(ids.contains(GeminiInpaintService::defaultModel()),
	         qPrintable(GeminiInpaintService::defaultModel()));
	QCOMPARE(GeminiInpaintService::displayNameFor(QStringLiteral("gemini-3.1-flash-image")),
	         QStringLiteral("Nano Banana 2"));
	// Something not in the list is shown as itself rather than as nothing.
	QCOMPARE(GeminiInpaintService::displayNameFor(QStringLiteral("who-what")),
	         QStringLiteral("who-what"));
}

void GeminiInpaintServiceTests::testShortAndFileNamesForModels()
{
	QCOMPARE(GeminiInpaintService::shortNameFor(QStringLiteral("gemini-3.1-flash-image")),
	         QStringLiteral("gemini-3.1-flash-image"));
	// A "models/" prefix is how the REST API names them in its own responses,
	// and a user pasting one should not end up with it in a filename.
	QCOMPARE(GeminiInpaintService::shortNameFor(QStringLiteral("models/gemini-3-pro-image")),
	         QStringLiteral("gemini-3-pro-image"));
	// A preview and its stable release are the same model to a filename.
	QCOMPARE(GeminiInpaintService::shortNameFor(QStringLiteral("gemini-3-pro-image-preview")),
	         QStringLiteral("gemini-3-pro-image"));

	QCOMPARE(GeminiInpaintService::fileTagFor(QStringLiteral("gemini-3.1-flash-image")),
	         QStringLiteral("gemini_3.1_flash_image"));
	// Nothing that could turn a filename into a path or hide an extension.
	const QString hostile = GeminiInpaintService::fileTagFor(QStringLiteral("a/../b c:d"));
	QVERIFY2(!hostile.contains(QLatin1Char('/')), qPrintable(hostile));
	QVERIFY2(!hostile.contains(QLatin1Char(' ')), qPrintable(hostile));
	QVERIFY2(!hostile.contains(QLatin1Char(':')), qPrintable(hostile));
}

// --- what goes out on the wire ----------------------------------------------

void GeminiInpaintServiceTests::testRequestWireFormat()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	const QImage image = sampleImage(100, 100);
	const QImage mask = sampleMask(100, 100);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(image, mask);
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const MockGemini::Request& request = server.received.first();

	// Interactions API: no model in the path, no ":generateContent" suffix.
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/interactions"));
	QVERIFY2(!request.path.contains("generateContent"), request.path.constData());
	QVERIFY2(!request.path.contains("models/"), request.path.constData());

	/* Byte-comparison against the request this input has to produce. Built the
	   same way the service builds it, from the same shared compositor, so this
	   pins the whole body: field names, their order, the prompt and the exact
	   JPEG bytes of the composited picture. */
	QJsonObject expectedText;
	expectedText.insert("type", "text");
	expectedText.insert("text", QString::fromLatin1(AIInpaintPrompts::REMOVE_OBJECT_COMPOSITE));
	QJsonObject expectedImage;
	expectedImage.insert("type", "image");
	expectedImage.insert("mime_type", "image/jpeg");
	expectedImage.insert("data", QString::fromLatin1(
		AIInpaintComposite::toJpeg(AIInpaintComposite::maskOverlay(image, mask)).toBase64()));
	QJsonArray expectedInput;
	expectedInput.append(expectedText);
	expectedInput.append(expectedImage);
	QJsonObject expected;
	expected.insert("model", QStringLiteral("gemini-3.1-flash-image"));
	expected.insert("input", expectedInput);

	QCOMPARE(request.body, QJsonDocument(expected).toJson(QJsonDocument::Compact));

	// And the individual field names, so a failure above says which one moved.
	const QJsonObject body = request.json();
	const QJsonArray input = body.value("input").toArray();
	QCOMPARE(input.size(), 2);
	QCOMPARE(input.at(1).toObject().value("mime_type").toString(), QStringLiteral("image/jpeg"));
	// The legacy generateContent spellings must not creep back in.
	QVERIFY(!body.contains(QStringLiteral("contents")));
	QVERIFY(!body.contains(QStringLiteral("generationConfig")));
	QVERIFY2(!request.body.contains("inline_data"), "inline_data is the legacy shape");
	QVERIFY2(!request.body.contains("inlineData"), "inlineData is the legacy shape");
}

void GeminiInpaintServiceTests::testApiKeyTravelsOnlyInItsOwnHeader()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const MockGemini::Request& request = server.received.first();
	const QByteArray key = QByteArray(TestKey);

	QCOMPARE(request.header("x-goog-api-key"), key);

	// Google's own documentation shows "?key=" for some endpoints. A URL ends
	// up in error strings and proxy logs, so it is not used that way here.
	QVERIFY2(!request.path.contains(key), request.path.constData());
	QVERIFY2(!request.path.contains("key="), request.path.constData());
	QVERIFY2(!request.body.contains(key), "the key must not be in the request body");

	// And in no other header either - not Authorization, not a stray copy.
	int occurrences = 0;
	const QList<QByteArray> lines = request.head.split('\n');
	for (const QByteArray& line : lines)
	{
		if (line.contains(key))
			++occurrences;
	}
	QCOMPARE(occurrences, 1);
	QVERIFY(request.header("Authorization").isEmpty());
}

void GeminiInpaintServiceTests::testModelRoutingChangesOnlyTheModelField()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<GeminiInpaintService> first(serviceFor(server, QStringLiteral("gemini-3.1-flash-image")));
	QSignalSpy doneFirst(first.data(), &AIInpaintService::inpaintFinished);
	first->inpaint(sampleImage(), sampleMask());
	QVERIFY(doneFirst.wait(5000));

	QScopedPointer<GeminiInpaintService> second(serviceFor(server, QStringLiteral("gemini-3-pro-image")));
	QSignalSpy doneSecond(second.data(), &AIInpaintService::inpaintFinished);
	second->inpaint(sampleImage(), sampleMask());
	QVERIFY(doneSecond.wait(5000));

	QCOMPARE(server.received.size(), 2);
	QJsonObject a = server.received.at(0).json();
	QJsonObject b = server.received.at(1).json();
	QCOMPARE(a.value("model").toString(), QStringLiteral("gemini-3.1-flash-image"));
	QCOMPARE(b.value("model").toString(), QStringLiteral("gemini-3-pro-image"));
	// Picking a different model must not quietly change the instruction or the
	// picture: take the model out and the two requests are the same request.
	a.remove("model");
	b.remove("model");
	QCOMPARE(QJsonDocument(a).toJson(QJsonDocument::Compact),
	         QJsonDocument(b).toJson(QJsonDocument::Compact));
	// Both went to the same place.
	QCOMPARE(server.received.at(0).path, server.received.at(1).path);
}

void GeminiInpaintServiceTests::testMaskIsPaintedIntoTheImageThatIsSent()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.body = successBody(sampleImage());

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const QImage sent = sentImageOf(server.received.first().json());
	QVERIFY(!sent.isNull());
	QCOMPARE(sent.size(), QSize(60, 60));

	// Inside the mask is red, outside it is the original green. JPEG is lossy,
	// so this asks which channel dominates rather than for exact values.
	const QRgb inside = sent.pixel(30, 30);
	QVERIFY2(qRed(inside) > 150 && qGreen(inside) < 110,
	         qPrintable(QStringLiteral("inside mask: %1,%2,%3")
	                    .arg(qRed(inside)).arg(qGreen(inside)).arg(qBlue(inside))));
	const QRgb outside = sent.pixel(5, 5);
	QVERIFY2(qGreen(outside) > 150 && qRed(outside) < 110,
	         qPrintable(QStringLiteral("outside mask: %1,%2,%3")
	                    .arg(qRed(outside)).arg(qGreen(outside)).arg(qBlue(outside))));
}

// --- what comes back --------------------------------------------------------

void GeminiInpaintServiceTests::testSuccessReturnsTheModelsImage()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	QImage returned(24, 18, QImage::Format_RGB32);
	returned.fill(qRgb(7, 9, 11));
	server.body = successBody(returned);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	QCOMPARE(failed.count(), 0);
	const QImage result = done.first().at(0).value<QImage>();
	QCOMPARE(result.size(), QSize(24, 18));
	QCOMPARE(result.pixel(3, 3), qRgb(7, 9, 11));
}

void GeminiInpaintServiceTests::testImageIsFoundInStepsWhenThereIsNoOutputImage()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	QImage returned(16, 16, QImage::Format_RGB32);
	returned.fill(qRgb(200, 0, 0));

	// The same answer with the convenience accessor left out: the picture is
	// only under steps[].content[]. Both are documented, so both must work.
	QJsonObject root = QJsonDocument::fromJson(successBody(returned)).object();
	root.remove("output_image");
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));
	QCOMPARE(done.first().at(0).value<QImage>().size(), QSize(16, 16));
}

void GeminiInpaintServiceTests::testTextAlongsideTheImageIsIgnored()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	QImage returned(12, 12, QImage::Format_RGB32);
	returned.fill(qRgb(1, 2, 3));
	// Models like to narrate. A picture plus commentary is a success.
	server.body = successBody(returned, QStringLiteral("Sure! I removed the object for you."));

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(5000));

	QCOMPARE(failed.count(), 0);
	QCOMPARE(done.first().at(0).value<QImage>().size(), QSize(12, 12));
}

void GeminiInpaintServiceTests::testTextOnlyAnswerIsReportedAsARefusal()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.body = textOnlyBody(QStringLiteral("I can't edit photographs of real people."));

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	// A refusal is not a fault, and the caller shows it differently.
	QVERIFY2(AIInpaintService::isRefusal(error), qPrintable(error));
	const QString shown = AIInpaintService::strippedRefusal(error);
	// What the model actually said is the useful part, not "no image".
	QVERIFY2(shown.contains(QStringLiteral("real people")), qPrintable(shown));
	QVERIFY2(shown.contains(QStringLiteral("Nano Banana 2")), qPrintable(shown));
}

void GeminiInpaintServiceTests::testBlockReasonIsReportedAsARefusal()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	// 200 OK, no picture, and a safety verdict instead.
	QJsonObject feedback;
	feedback.insert("blockReason", QStringLiteral("SAFETY"));
	QJsonObject root;
	root.insert("object", "interaction");
	root.insert("status", "completed");
	root.insert("promptFeedback", feedback);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(AIInpaintService::isRefusal(error), qPrintable(error));
	QVERIFY2(AIInpaintService::strippedRefusal(error).contains(QStringLiteral("SAFETY")),
	         qPrintable(error));
}

void GeminiInpaintServiceTests::testInteractionErrorsAreReportedAsARefusal()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	// A 200 whose interaction failed: the reason is in errors[], not in HTTP.
	QJsonObject entry;
	entry.insert("code", QStringLiteral("IMAGE_SAFETY"));
	entry.insert("message", QStringLiteral("The generated image was blocked by safety filters."));
	QJsonArray errors;
	errors.append(entry);
	QJsonObject root;
	root.insert("object", "interaction");
	root.insert("status", "failed");
	root.insert("errors", errors);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString shown = AIInpaintService::strippedRefusal(failed.first().at(0).toString());
	QVERIFY2(shown.contains(QStringLiteral("safety filters")), qPrintable(shown));
}

// --- failures ---------------------------------------------------------------

void GeminiInpaintServiceTests::testErrorStatusesAreReportedPromptly_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QString>("serverMessage");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<bool>("stall");

	/* Every status a user can meet, answered head-first with a body that never
	   completes - the shape that used to be reported as a sixty-second timeout
	   on the OpenRouter client. Each has to arrive inside two seconds and say
	   what the user should go and do about it.

	   401 is not stalled, and that is a Qt limitation rather than a choice:
	   401 and 407 are the HTTP authentication statuses and Qt withholds the
	   whole response for them - no metaDataChanged, no readyRead, not even
	   authenticationRequired - until the body is complete, in case it has to
	   resend the request with credentials. Measured on Qt 6.8.2: every other
	   status below fired metaDataChanged within 3ms, 401 and 407 fired nothing
	   at all. Google's error bodies arrive whole, which is what is tested. */
	// Twice: stalled, where only the status is knowable and the message must
	// still name it; and whole, where the server's own words are what a bug
	// report needs and have to survive into the message.
	QTest::newRow("400 stalled")      << 400 << QStringLiteral("Invalid JSON payload")        << QStringLiteral("400")                  << true;
	QTest::newRow("400 whole")        << 400 << QStringLiteral("Invalid JSON payload")        << QStringLiteral("Invalid JSON payload") << false;
	QTest::newRow("403 no billing")   << 403 << QStringLiteral("Permission denied")           << QStringLiteral("billing")              << true;
	QTest::newRow("429 rate limited") << 429 << QStringLiteral("Resource exhausted")          << QStringLiteral("rate limit")           << true;
	QTest::newRow("500 server error") << 500 << QStringLiteral("Internal error")              << QStringLiteral("unavailable")          << true;
	QTest::newRow("503 unavailable")  << 503 << QStringLiteral("The service is overloaded")   << QStringLiteral("unavailable")          << true;
	QTest::newRow("401 bad key")      << 401 << QStringLiteral("API key not valid")           << QStringLiteral("Gemini API key")       << false;
}

void GeminiInpaintServiceTests::testErrorStatusesAreReportedPromptly()
{
	QFETCH(int, status);
	QFETCH(QString, serverMessage);
	QFETCH(QString, expected);
	QFETCH(bool, stall);

	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = errorBody(status, serverMessage);
	server.stallAfterHead = stall;

	// The production sixty-second timeout on purpose: if the timeout is what
	// ends the request, this takes a minute and fails.
	QScopedPointer<GeminiInpaintService> service(
		serviceFor(server, QStringLiteral("gemini-3.1-flash-image"), 60));
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
	QVERIFY(!AIInpaintService::isCancelled(error));
	// Nothing the user sent may come back out in the message.
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

void GeminiInpaintServiceTests::testMissingKeyIsRefusedBeforeAnyRequest()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());

	QScopedPointer<GeminiInpaintService> service(
		serviceFor(server, QStringLiteral("gemini-3.1-flash-image"), 5, QString()));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	service->inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("Preferences")), qPrintable(error));
	// Nothing may go out without a key: not even the picture.
	QCOMPARE(server.received.size(), 0);
}

void GeminiInpaintServiceTests::testTimeoutIsReported()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;

	QScopedPointer<GeminiInpaintService> service(
		serviceFor(server, QStringLiteral("gemini-3.1-flash-image"), 1));
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

void GeminiInpaintServiceTests::testCancelAbortsAndStaysQuiet()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.delayMs = 4000;
	server.body = successBody(sampleImage());

	QScopedPointer<GeminiInpaintService> service(
		serviceFor(server, QStringLiteral("gemini-3.1-flash-image"), 30));
	QSignalSpy failed(service.data(), &AIInpaintService::inpaintFailed);
	QSignalSpy done(service.data(), &AIInpaintService::inpaintFinished);

	service->inpaint(sampleImage(), sampleMask());
	QTRY_VERIFY_WITH_TIMEOUT(server.received.size() == 1, 5000);
	service->cancel();

	// Cancelling still has to produce exactly one signal, or a caller that
	// cancels waits for one for ever - but it must not be an error anybody is
	// shown.
	QVERIFY(failed.wait(5000));
	QCOMPARE(done.count(), 0);
	QVERIFY(AIInpaintService::isCancelled(failed.first().at(0).toString()));

	// And the far end really was let go of rather than merely ignored.
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

// --- the Test Connection button ---------------------------------------------

void GeminiInpaintServiceTests::testConnectionAcceptsAKeyThatCanListModels()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	QJsonObject one;
	one.insert("name", QStringLiteral("models/gemini-3.1-flash-image"));
	QJsonArray models;
	models.append(one);
	QJsonObject root;
	root.insert("models", models);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AIInpaintService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));

	QVERIFY2(tested.first().at(0).toBool(), qPrintable(tested.first().at(1).toString()));

	QCOMPARE(server.received.size(), 1);
	// A plain GET that runs no model: pressing the button must never be
	// billable, and must never send a picture anywhere.
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	QCOMPARE(server.received.first().path, QByteArray("/models"));
	QVERIFY(server.received.first().body.isEmpty());
	QCOMPARE(server.received.first().header("x-goog-api-key"), QByteArray(TestKey));
}

void GeminiInpaintServiceTests::testConnectionFailsOnBadKey()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());
	server.status = 401;
	server.body = errorBody(401, QStringLiteral("API key not valid"), QStringLiteral("UNAUTHENTICATED"));

	QScopedPointer<GeminiInpaintService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AIInpaintService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));

	QVERIFY(!tested.first().at(0).toBool());
	const QString detail = tested.first().at(1).toString();
	QVERIFY2(detail.contains(QStringLiteral("API key")), qPrintable(detail));
	// Saying which key was rejected would put it on the screen.
	QVERIFY(!detail.contains(QString::fromLatin1(TestKey)));
}

// --- confidentiality --------------------------------------------------------

namespace
{
	QStringList g_captured;
	QtMessageHandler g_previous = nullptr;

	void capture(QtMsgType type, const QMessageLogContext& context, const QString& message)
	{
		g_captured << message;
		if (context.file)
			g_captured << QString::fromLatin1(context.file);
		Q_UNUSED(type)
	}
}

/*!
 \brief The key must not reach any log, by any route.

 Not a re-run of the header test: that one reads the request, this one watches
 everything the process says while making it. A key that leaks into a qDebug or
 into an error string ends up in a bug report or a screen share, and the user
 has no way of knowing it happened.
 */
void GeminiInpaintServiceTests::testApiKeyNeverAppearsInAnyLogOutput()
{
	MockGemini server;
	QVERIFY(server.startOnAnyPort());

	g_captured.clear();
	g_previous = qInstallMessageHandler(capture);

	QStringList errors;
	{
		// A run that succeeds, a run that is rejected, and a key check that
		// fails: the three routes a key could take into a message.
		server.body = successBody(sampleImage());
		QScopedPointer<GeminiInpaintService> ok(serviceFor(server));
		QSignalSpy done(ok.data(), &AIInpaintService::inpaintFinished);
		ok->inpaint(sampleImage(), sampleMask());
		QVERIFY(done.wait(5000));

		server.status = 401;
		server.body = errorBody(401, QStringLiteral("API key not valid"));
		QScopedPointer<GeminiInpaintService> bad(serviceFor(server));
		QSignalSpy failed(bad.data(), &AIInpaintService::inpaintFailed);
		bad->inpaint(sampleImage(), sampleMask());
		QVERIFY(failed.wait(5000));
		errors << failed.first().at(0).toString();

		QSignalSpy tested(bad.data(), &AIInpaintService::connectionTested);
		bad->testConnection();
		QVERIFY(tested.wait(5000));
		errors << tested.first().at(1).toString();
	}

	qInstallMessageHandler(g_previous);

	const QString key = QString::fromLatin1(TestKey);
	for (const QString& line : std::as_const(g_captured))
		QVERIFY2(!line.contains(key), qPrintable(line));
	// The messages shown to the user are logs waiting to happen.
	for (const QString& error : std::as_const(errors))
		QVERIFY2(!error.contains(key), qPrintable(error));
	// A fragment of it would be just as bad as the whole thing.
	for (const QString& line : std::as_const(g_captured))
		QVERIFY2(!line.contains(QStringLiteral("TESTKEYDONOTLEAKME")), qPrintable(line));
}

// Guiless: these tests need an event loop for the sockets, and nothing else.
// QTEST_MAIN would drag in QApplication and the whole widget stack with it.
QTEST_GUILESS_MAIN(GeminiInpaintServiceTests)
