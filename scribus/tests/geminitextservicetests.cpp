/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "geminitextservicetests.h"
#include "aitextmockserver.h"
#include "ai/aitextprompts.h"
#include "ai/geminitextservice.h"

using namespace AITextTestData;

namespace
{
	const char* const TestKey = "AIzaSy-TEXTTESTKEYDONOTLEAKME-0123456789";

	//! A success envelope of the documented shape: the words at output_text,
	//! and the same content mirrored under steps[].
	QByteArray answerBody(const QString& text, int inputTokens = 100, int outputTokens = 25)
	{
		QJsonObject part;
		part.insert("type", "text");
		part.insert("text", text);
		QJsonArray content;
		content.append(part);
		QJsonObject step;
		step.insert("type", "model_output");
		step.insert("content", content);
		QJsonArray steps;
		steps.append(step);
		QJsonObject usage;
		usage.insert("total_input_tokens", inputTokens);
		usage.insert("total_output_tokens", outputTokens);
		QJsonObject root;
		root.insert("object", "interaction");
		root.insert("status", "completed");
		root.insert("steps", steps);
		root.insert("output_text", text);
		root.insert("usage", usage);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! Google's documented error envelope, optionally wrapped in the array the
	//! interactions endpoint actually sends.
	QByteArray errorBody(int code, const QString& message, const QString& statusText = QString(),
	                     const QString& reason = QString(), bool wrapInArray = false)
	{
		QJsonObject error;
		error.insert("code", code);
		error.insert("message", message);
		if (!statusText.isEmpty())
			error.insert("status", statusText);
		if (!reason.isEmpty())
		{
			QJsonObject info;
			info.insert("@type", "type.googleapis.com/google.rpc.ErrorInfo");
			info.insert("reason", reason);
			QJsonArray details;
			details.append(info);
			error.insert("details", details);
		}
		QJsonObject root;
		root.insert("error", error);
		if (wrapInArray)
		{
			QJsonArray wrapper;
			wrapper.append(root);
			return QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
		}
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	GeminiTextService* serviceFor(AITextMockServer& server,
	                              const QString& model = QStringLiteral("gemini-3.7-flash"),
	                              int timeout = 5,
	                              const QString& key = QString::fromLatin1(TestKey))
	{
		return new GeminiTextService(key, model, timeout, nullptr, server.apiBase());
	}

	AITextService::Request textRequest(const QString& task, const QString& input,
	                                   const QVariantMap& parameters = QVariantMap())
	{
		AITextService::Request req;
		req.task = task;
		req.inputText = input;
		req.parameters = parameters;
		return req;
	}
}

void GeminiTextServiceTests::testCuratedModelListIsWellFormed()
{
	const QList<AITextProtocol::ModelChoice>& models = GeminiTextService::models();
	QVERIFY(!models.isEmpty());

	QSet<QString> ids;
	for (const AITextProtocol::ModelChoice& choice : models)
	{
		QVERIFY(!choice.id.isEmpty());
		QVERIFY(!choice.displayName.isEmpty());
		QVERIFY(!choice.hint.isEmpty());
		QVERIFY2(!ids.contains(choice.id), qPrintable(choice.id));
		ids.insert(choice.id);
		// Google's own ids carry no vendor prefix. OpenRouter's id for a
		// similar model looks like "google/gemini-..." and is not
		// interchangeable with this one.
		QVERIFY2(!choice.id.contains(QLatin1Char('/')), qPrintable(choice.id));
	}
	QVERIFY2(ids.contains(GeminiTextService::defaultModel()),
	         qPrintable(GeminiTextService::defaultModel()));

	/* These must be *text* models. The image client next door offers the Nano
	   Banana list, and the two are not interchangeable: sending a text prompt
	   to an image model gets a picture back, and the parser would report no
	   answer. A shared "-image" id in both lists is the mistake this catches. */
	for (const AITextProtocol::ModelChoice& choice : models)
		QVERIFY2(!choice.id.contains(QLatin1String("-image")), qPrintable(choice.id));
}

void GeminiTextServiceTests::testTextRequestWireFormat()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("A summary."));

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	QVariantMap params;
	params.insert(QStringLiteral("length"), QStringLiteral("medium"));
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize),
	                             sampleArticle(), params));
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const AITextMockServer::Request& request = server.received.first();

	// Interactions API: no model in the path, no ":generateContent" suffix.
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/interactions"));
	QVERIFY2(!request.path.contains("generateContent"), request.path.constData());
	QVERIFY2(!request.path.contains("models/"), request.path.constData());

	// Byte-comparison against the request this input has to produce.
	QJsonObject text;
	text.insert("type", "text");
	text.insert("text", AITextPrompts::promptFor(QLatin1String(AITextService::TaskSummarize),
	                                             sampleArticle(), params));
	QJsonArray input;
	input.append(text);
	QJsonObject expected;
	expected.insert("model", QStringLiteral("gemini-3.7-flash"));
	expected.insert("input", input);
	QCOMPARE(request.body, QJsonDocument(expected).toJson(QJsonDocument::Compact));

	// The legacy generateContent spellings must not creep back in.
	const QJsonObject body = request.json();
	QVERIFY(!body.contains(QStringLiteral("contents")));
	QVERIFY(!body.contains(QStringLiteral("generationConfig")));
	QVERIFY2(!request.body.contains("inline_data"), "inline_data is the legacy shape");

	// The Malayalam survived as Malayalam.
	const QString sent = body.value("input").toArray().at(0).toObject().value("text").toString();
	QVERIFY2(sent.contains(QStringLiteral("കേരളത്തിൽ")), qPrintable(sent.left(120)));
}

void GeminiTextServiceTests::testVisionRequestUsesTheFlatInlineShape()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("Alt text."));

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	AITextService::Request req;
	req.task = QLatin1String(AITextService::TaskAltText);
	req.inputImage = sampleImage();
	service->execute(req);
	QVERIFY(done.wait(5000));

	const QJsonArray input = server.received.first().json().value("input").toArray();
	QCOMPARE(input.size(), 2);
	QCOMPARE(input.at(0).toObject().value("type").toString(), QStringLiteral("text"));

	// Flat and snake_case: the base64 sits directly in "data" with no
	// inline_data wrapper and no nested "source" - that last is Claude's shape.
	const QJsonObject image = input.at(1).toObject();
	QCOMPARE(image.value("type").toString(), QStringLiteral("image"));
	QCOMPARE(image.value("mime_type").toString(), QStringLiteral("image/jpeg"));
	QVERIFY(!image.contains(QStringLiteral("source")));
	QVERIFY(!image.contains(QStringLiteral("inline_data")));

	QImage decoded;
	QVERIFY(decoded.loadFromData(QByteArray::fromBase64(image.value("data").toString().toLatin1())));
	QCOMPARE(decoded.size(), QSize(40, 30));
}

void GeminiTextServiceTests::testAuthHeaderIsTheGoogleOne()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("ok"));

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(done.wait(5000));

	const AITextMockServer::Request& request = server.received.first();
	const QByteArray key = QByteArray(TestKey);
	QCOMPARE(request.header("x-goog-api-key"), key);
	// Google's docs show "?key=" for some endpoints. A URL ends up in error
	// strings and proxy logs, so it is not used that way here.
	QVERIFY2(!request.path.contains("key="), request.path.constData());
	QVERIFY2(!request.path.contains(key), request.path.constData());
	QVERIFY2(!request.body.contains(key), "the key must not be in the request body");
	QVERIFY(request.header("Authorization").isEmpty());
}

void GeminiTextServiceTests::testSuccessReturnsTheAnswer()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("മഴ ശക്തമായി"));

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize), sampleArticle()));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 1);
	QCOMPARE(response.results.first(), QStringLiteral("മഴ ശക്തമായി"));
	QCOMPARE(response.tokensUsed, 125);
	QCOMPARE(response.modelDisplayName, QStringLiteral("Gemini 3.7 Flash"));
	// No verified price list for Gemini text, so no estimate is offered rather
	// than a made-up one. The dialog omits the line when this is zero.
	QCOMPARE(response.estimatedCost, 0.0);
}

void GeminiTextServiceTests::testAnswerIsFoundInStepsWhenThereIsNoOutputText()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// The same answer with the convenience accessor left out: the words are
	// only under steps[].content[]. Both are documented, so both must work.
	QJsonObject root = QJsonDocument::fromJson(answerBody(QStringLiteral("Only in steps"))).object();
	root.remove("output_text");
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(done.wait(5000));
	QCOMPARE(done.first().at(0).value<AITextService::Response>().results.first(),
	         QStringLiteral("Only in steps"));
}

void GeminiTextServiceTests::testHeadlineTaskSplitsIntoOptions()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("1. First line\n2. Second line\n3. Third line"));

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskHeadline), sampleArticle()));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 3);
	QCOMPARE(response.results.at(0), QStringLiteral("First line"));
}

void GeminiTextServiceTests::testBlockReasonIsReportedAsARefusal()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// 200 OK, no words, and a safety verdict instead.
	QJsonObject feedback;
	feedback.insert("blockReason", QStringLiteral("SAFETY"));
	QJsonObject root;
	root.insert("object", "interaction");
	root.insert("promptFeedback", feedback);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(AITextService::isRefusal(error), qPrintable(error));
	QVERIFY2(AITextService::strippedRefusal(error).contains(QStringLiteral("SAFETY")),
	         qPrintable(error));
}

/*!
 \brief The interactions endpoint wraps its error envelope in an array.

 Measured against the live API on 30 Aug 2026 while building the image client:
 GET /v1beta/models answers with a bare object, POST /v1beta/interactions
 answers with the identical envelope inside a one-element array. A parser that
 only understands the object shape drops the message on exactly the endpoint
 that does the work.
 */
void GeminiTextServiceTests::testArrayWrappedErrorEnvelopeIsUnderstood()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = 429;
	server.body = errorBody(429, QStringLiteral("Quota exceeded"),
	                        QStringLiteral("RESOURCE_EXHAUSTED"), QString(), /*wrapInArray*/ true);

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("not JSON")), qPrintable(error));
}

void GeminiTextServiceTests::testRejectedKeyIsReportedAsAKeyProblem_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QByteArray>("body");

	/* An invalid key does not come back as 401. Measured against the live API
	   on 30 Aug 2026: HTTP 400, status INVALID_ARGUMENT, detail reason
	   API_KEY_INVALID. Keying off the number alone would report the single most
	   common mistake a user can make as an unexplained bad request. */
	QTest::newRow("live shape: 400 + reason")
		<< 400 << errorBody(400, QStringLiteral("API key not valid. Please pass a valid API key."),
		                    QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("API_KEY_INVALID"), true);
	QTest::newRow("400, message only")
		<< 400 << errorBody(400, QStringLiteral("API key not valid. Please pass a valid API key."),
		                    QStringLiteral("INVALID_ARGUMENT"));
	QTest::newRow("401, as one would expect")
		<< 401 << errorBody(401, QStringLiteral("Unauthenticated"), QStringLiteral("UNAUTHENTICATED"));
}

void GeminiTextServiceTests::testRejectedKeyIsReportedAsAKeyProblem()
{
	QFETCH(int, status);
	QFETCH(QByteArray, body);

	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = body;

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("Gemini API key")), qPrintable(error));
	QVERIFY2(error.contains(QStringLiteral("Preferences")), qPrintable(error));
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

void GeminiTextServiceTests::testErrorStatusesAreReportedPromptly_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QString>("serverMessage");
	QTest::addColumn<QString>("googleStatus");
	QTest::addColumn<QString>("expected");

	// Answered head-first with a body that never completes: the shape that once
	// turned a fast rejection into a sixty-second timeout. 401 is excluded
	// because Qt withholds the whole response for it - see the note in the
	// Claude suite - and it is covered whole above.
	QTest::newRow("400 bad request")  << 400 << QStringLiteral("Bad input")
	                                  << QString() << QStringLiteral("400");
	QTest::newRow("403 no billing")   << 403 << QStringLiteral("Permission denied")
	                                  << QStringLiteral("PERMISSION_DENIED") << QStringLiteral("billing");
	QTest::newRow("429 rate limited") << 429 << QStringLiteral("Too many")
	                                  << QStringLiteral("RESOURCE_EXHAUSTED") << QStringLiteral("rate limit");
	QTest::newRow("500 server error") << 500 << QStringLiteral("Boom")
	                                  << QString() << QStringLiteral("unavailable");
	QTest::newRow("503 unavailable")  << 503 << QStringLiteral("Overloaded")
	                                  << QString() << QStringLiteral("unavailable");
}

void GeminiTextServiceTests::testErrorStatusesAreReportedPromptly()
{
	QFETCH(int, status);
	QFETCH(QString, serverMessage);
	QFETCH(QString, googleStatus);
	QFETCH(QString, expected);

	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = errorBody(status, serverMessage, googleStatus, QString(), true);
	server.stallAfterHead = true;

	// The production sixty-second timeout on purpose.
	QScopedPointer<GeminiTextService> service(
		serviceFor(server, QStringLiteral("gemini-3.7-flash"), 60));
	QSignalSpy failed(service.data(), &AITextService::failed);

	QElapsedTimer elapsed;
	elapsed.start();
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY2(failed.wait(2000),
	         qPrintable(QStringLiteral("status %1 was not reported within two seconds").arg(status)));
	QVERIFY2(elapsed.elapsed() < 2000, qPrintable(QString::number(elapsed.elapsed())));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(expected, Qt::CaseInsensitive), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

void GeminiTextServiceTests::testTimeoutIsReported()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;

	QScopedPointer<GeminiTextService> service(
		serviceFor(server, QStringLiteral("gemini-3.7-flash"), 1));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(10000));

	const QString error = failed.first().at(0).toString();
	QVERIFY(!AITextService::isCancelled(error));
	QVERIFY2(error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

void GeminiTextServiceTests::testCancelAbortsAndStaysQuiet()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.delayMs = 4000;
	server.body = answerBody(QStringLiteral("too late"));

	QScopedPointer<GeminiTextService> service(
		serviceFor(server, QStringLiteral("gemini-3.7-flash"), 30));
	QSignalSpy failed(service.data(), &AITextService::failed);
	QSignalSpy done(service.data(), &AITextService::completed);

	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QTRY_VERIFY_WITH_TIMEOUT(server.received.size() == 1, 5000);
	service->cancel();

	QVERIFY(failed.wait(5000));
	QCOMPARE(done.count(), 0);
	QVERIFY(AITextService::isCancelled(failed.first().at(0).toString()));
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

void GeminiTextServiceTests::testConnectionAcceptsAKeyThatCanListModels()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	QJsonObject one;
	one.insert("name", QStringLiteral("models/gemini-3.7-flash"));
	QJsonArray models;
	models.append(one);
	QJsonObject root;
	root.insert("models", models);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<GeminiTextService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AITextService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));
	QVERIFY2(tested.first().at(0).toBool(), qPrintable(tested.first().at(1).toString()));

	// A plain GET that runs no model: pressing the button must never be
	// billable - which matters most here, on the account that actually works.
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	QCOMPARE(server.received.first().path, QByteArray("/models"));
	QVERIFY(server.received.first().body.isEmpty());
}

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

void GeminiTextServiceTests::testApiKeyNeverAppearsInAnyLogOutput()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());

	g_captured.clear();
	g_previous = qInstallMessageHandler(capture);

	QStringList shown;
	{
		server.body = answerBody(QStringLiteral("fine"));
		QScopedPointer<GeminiTextService> ok(serviceFor(server));
		QSignalSpy done(ok.data(), &AITextService::completed);
		ok->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
		QVERIFY(done.wait(5000));

		server.status = 400;
		server.body = errorBody(400, QStringLiteral("API key not valid."),
		                        QStringLiteral("INVALID_ARGUMENT"),
		                        QStringLiteral("API_KEY_INVALID"), true);
		QScopedPointer<GeminiTextService> bad(serviceFor(server));
		QSignalSpy failed(bad.data(), &AITextService::failed);
		bad->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
		QVERIFY(failed.wait(5000));
		shown << failed.first().at(0).toString();
	}

	qInstallMessageHandler(g_previous);

	const QString key = QString::fromLatin1(TestKey);
	for (const QString& line : std::as_const(g_captured))
	{
		QVERIFY2(!line.contains(key), qPrintable(line));
		QVERIFY2(!line.contains(QStringLiteral("TEXTTESTKEYDONOTLEAKME")), qPrintable(line));
	}
	for (const QString& message : std::as_const(shown))
		QVERIFY2(!message.contains(key), qPrintable(message));
}

QTEST_GUILESS_MAIN(GeminiTextServiceTests)
