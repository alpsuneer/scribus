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

#include "openaitextservicetests.h"
#include "aitextmockserver.h"
#include "ai/aitextprompts.h"
#include "ai/openaitextservice.h"

using namespace AITextTestData;

namespace
{
	const char* const TestKey = "sk-proj-TESTKEYDONOTLEAKME-0123456789abcdef";

	//! A success envelope of the documented shape.
	QByteArray answerBody(const QString& text, int promptTokens = 90, int completionTokens = 20)
	{
		QJsonObject message;
		message.insert("role", "assistant");
		message.insert("content", text);
		QJsonObject choice;
		choice.insert("index", 0);
		choice.insert("finish_reason", "stop");
		choice.insert("message", message);
		QJsonArray choices;
		choices.append(choice);
		QJsonObject usage;
		usage.insert("prompt_tokens", promptTokens);
		usage.insert("completion_tokens", completionTokens);
		QJsonObject root;
		root.insert("id", "chatcmpl-1");
		root.insert("object", "chat.completion");
		root.insert("model", "gpt-5.6-terra");
		root.insert("choices", choices);
		root.insert("usage", usage);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! OpenAI's documented error envelope.
	QByteArray errorBody(const QString& message, const QString& type = QString(),
	                     const QString& code = QString())
	{
		QJsonObject error;
		error.insert("message", message);
		if (!type.isEmpty())
			error.insert("type", type);
		if (!code.isEmpty())
			error.insert("code", code);
		QJsonObject root;
		root.insert("error", error);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	OpenAITextService* serviceFor(AITextMockServer& server,
	                              const QString& model = QStringLiteral("gpt-5.6-terra"),
	                              int timeout = 5,
	                              const QString& key = QString::fromLatin1(TestKey))
	{
		return new OpenAITextService(key, model, timeout, nullptr, server.apiBase());
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

void OpenAITextServiceTests::testCuratedModelListIsWellFormed()
{
	const QList<AITextProtocol::ModelChoice>& models = OpenAITextService::models();
	QVERIFY(!models.isEmpty());

	QSet<QString> ids;
	for (const AITextProtocol::ModelChoice& choice : models)
	{
		QVERIFY(!choice.id.isEmpty());
		QVERIFY(!choice.displayName.isEmpty());
		QVERIFY(!choice.hint.isEmpty());
		QVERIFY2(!ids.contains(choice.id), qPrintable(choice.id));
		ids.insert(choice.id);
	}
	QVERIFY2(ids.contains(OpenAITextService::defaultModel()),
	         qPrintable(OpenAITextService::defaultModel()));
	QCOMPARE(OpenAITextService::displayNameFor(QStringLiteral("who-what")),
	         QStringLiteral("who-what"));
}

void OpenAITextServiceTests::testTextRequestWireFormat()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("A summary."));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	QVariantMap params;
	params.insert(QStringLiteral("length"), QStringLiteral("medium"));
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize),
	                             sampleArticle(), params));
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const AITextMockServer::Request& request = server.received.first();
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/v1/chat/completions"));

	// Byte-comparison against the request this input has to produce.
	QJsonObject text;
	text.insert("type", "text");
	text.insert("text", AITextPrompts::promptFor(QLatin1String(AITextService::TaskSummarize),
	                                             sampleArticle(), params));
	QJsonArray content;
	content.append(text);
	QJsonObject message;
	message.insert("role", "user");
	message.insert("content", content);
	QJsonArray messages;
	messages.append(message);
	QJsonObject expected;
	expected.insert("model", QStringLiteral("gpt-5.6-terra"));
	expected.insert("messages", messages);
	expected.insert("max_completion_tokens", 2048);
	QCOMPARE(request.body, QJsonDocument(expected).toJson(QJsonDocument::Compact));

	/* max_tokens is the older spelling and does not account for reasoning
	   tokens; new code is meant to send max_completion_tokens. Sending the old
	   one is not an error the API reports - it is simply ignored on current
	   models - so nothing but this test would notice it coming back. */
	QVERIFY2(!request.json().contains(QStringLiteral("max_tokens")),
	         "max_tokens is the superseded spelling");

	const QString sent = request.json().value("messages").toArray().at(0).toObject()
	                     .value("content").toArray().at(0).toObject().value("text").toString();
	QVERIFY2(sent.contains(QStringLiteral("കേരളത്തിൽ")), qPrintable(sent.left(120)));
}

void OpenAITextServiceTests::testVisionRequestUsesADataUrl()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("Alt text."));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	AITextService::Request req;
	req.task = QLatin1String(AITextService::TaskAltText);
	req.inputImage = sampleImage();
	service->execute(req);
	QVERIFY(done.wait(5000));

	const QJsonArray content = server.received.first().json()
	                           .value("messages").toArray().at(0).toObject()
	                           .value("content").toArray();
	QCOMPARE(content.size(), 2);
	QCOMPARE(content.at(0).toObject().value("type").toString(), QStringLiteral("text"));

	// A complete data: URL nested in image_url - not bare base64, and not
	// Claude's nested "source". Getting this wrong produces a valid request
	// that silently carries no picture.
	const QJsonObject image = content.at(1).toObject();
	QCOMPARE(image.value("type").toString(), QStringLiteral("image_url"));
	const QString url = image.value("image_url").toObject().value("url").toString();
	QVERIFY2(url.startsWith(QStringLiteral("data:image/jpeg;base64,")), qPrintable(url.left(60)));
	QVERIFY(!image.contains(QStringLiteral("source")));
	QVERIFY(!image.contains(QStringLiteral("data")));

	const int comma = url.indexOf(QLatin1Char(','));
	QImage decoded;
	QVERIFY(decoded.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1())));
	QCOMPARE(decoded.size(), QSize(40, 30));
}

void OpenAITextServiceTests::testAuthHeaderIsABearerToken()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("ok"));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(done.wait(5000));

	const AITextMockServer::Request& request = server.received.first();
	const QByteArray key = QByteArray(TestKey);
	QCOMPARE(request.header("Authorization"), QByteArray("Bearer ") + key);
	// x-api-key is Anthropic's shape, x-goog-api-key is Google's.
	QVERIFY(request.header("x-api-key").isEmpty());
	QVERIFY(request.header("x-goog-api-key").isEmpty());

	QVERIFY2(!request.path.contains(key), request.path.constData());
	QVERIFY2(!request.body.contains(key), "the key must not be in the request body");
	int occurrences = 0;
	const QList<QByteArray> lines = request.head.split('\n');
	for (const QByteArray& line : lines)
	{
		if (line.contains(key))
			++occurrences;
	}
	QCOMPARE(occurrences, 1);
}

void OpenAITextServiceTests::testSuccessReturnsTheAnswer()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("മഴ ശക്തമായി"));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize), sampleArticle()));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 1);
	QCOMPARE(response.results.first(), QStringLiteral("മഴ ശക്തമായി"));
	QCOMPARE(response.tokensUsed, 110);
	QCOMPARE(response.modelDisplayName, QStringLiteral("GPT-5.6 Terra"));
	// No verified price list, so no estimate rather than a made-up one.
	QCOMPARE(response.estimatedCost, 0.0);
}

void OpenAITextServiceTests::testHeadlineTaskSplitsIntoOptions()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("First line\nSecond line\nThird line"));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskHeadline), sampleArticle()));
	QVERIFY(done.wait(5000));

	QCOMPARE(done.first().at(0).value<AITextService::Response>().results.size(), 3);
}

void OpenAITextServiceTests::testRefusalFieldIsReportedAsARefusal()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// A safety decline is a 200 with content null and the reason in its own
	// field: HTTP success is not the same as an answer.
	QJsonObject message;
	message.insert("role", "assistant");
	message.insert("content", QJsonValue());
	message.insert("refusal", QStringLiteral("I can't help with that request."));
	QJsonObject choice;
	choice.insert("message", message);
	choice.insert("finish_reason", "stop");
	QJsonArray choices;
	choices.append(choice);
	QJsonObject root;
	root.insert("choices", choices);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(AITextService::isRefusal(error), qPrintable(error));
	const QString shown = AITextService::strippedRefusal(error);
	QVERIFY2(shown.contains(QStringLiteral("can't help")), qPrintable(shown));
	QVERIFY2(shown.contains(QStringLiteral("GPT-5.6 Terra")), qPrintable(shown));
}

/*!
 \brief An exhausted account and a rate limit are both 429, and mean opposite
 things.

 "Try again in a moment" is advice that comes true for one of them and never
 for the other, so the machine-readable code has to be read rather than the
 status number.
 */
void OpenAITextServiceTests::testExhaustedQuotaIsNotReportedAsARateLimit()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = 429;
	server.body = errorBody(QStringLiteral("You exceeded your current quota."),
	                        QStringLiteral("insufficient_quota"),
	                        QStringLiteral("insufficient_quota"));

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("quota"), Qt::CaseInsensitive), qPrintable(error));
	QVERIFY2(error.contains(QStringLiteral("billing")), qPrintable(error));
	// The thing it must not say.
	QVERIFY2(!error.contains(QStringLiteral("try again in a moment"), Qt::CaseInsensitive),
	         qPrintable(error));
}

void OpenAITextServiceTests::testErrorStatusesAreReportedPromptly_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QString>("serverMessage");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<bool>("stall");

	// 401 is not stalled: Qt withholds the whole response for the HTTP
	// authentication statuses until the body completes. See the note in the
	// Claude suite for the measurement.
	QTest::newRow("403 forbidden")    << 403 << QStringLiteral("Forbidden")
	                                  << QStringLiteral("permission") << true;
	QTest::newRow("429 rate limited") << 429 << QStringLiteral("Rate limit reached")
	                                  << QStringLiteral("rate limiting") << true;
	QTest::newRow("500 server error") << 500 << QStringLiteral("Server error")
	                                  << QStringLiteral("unavailable") << true;
	QTest::newRow("503 unavailable")  << 503 << QStringLiteral("Overloaded")
	                                  << QStringLiteral("unavailable") << true;
	QTest::newRow("401 bad key")      << 401 << QStringLiteral("Incorrect API key provided")
	                                  << QStringLiteral("OpenAI API key") << false;
}

void OpenAITextServiceTests::testErrorStatusesAreReportedPromptly()
{
	QFETCH(int, status);
	QFETCH(QString, serverMessage);
	QFETCH(QString, expected);
	QFETCH(bool, stall);

	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = errorBody(serverMessage);
	server.stallAfterHead = stall;

	// The production sixty-second timeout on purpose.
	QScopedPointer<OpenAITextService> service(
		serviceFor(server, QStringLiteral("gpt-5.6-terra"), 60));
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

void OpenAITextServiceTests::testTimeoutIsReported()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;

	QScopedPointer<OpenAITextService> service(
		serviceFor(server, QStringLiteral("gpt-5.6-terra"), 1));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(10000));

	const QString error = failed.first().at(0).toString();
	QVERIFY(!AITextService::isCancelled(error));
	QVERIFY2(error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

void OpenAITextServiceTests::testCancelAbortsAndStaysQuiet()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.delayMs = 4000;
	server.body = answerBody(QStringLiteral("too late"));

	QScopedPointer<OpenAITextService> service(
		serviceFor(server, QStringLiteral("gpt-5.6-terra"), 30));
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

void OpenAITextServiceTests::testConnectionAcceptsAKeyThatCanListModels()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	QJsonObject one;
	one.insert("id", "gpt-5.6-terra");
	QJsonArray data;
	data.append(one);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<OpenAITextService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AITextService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));
	QVERIFY2(tested.first().at(0).toBool(), qPrintable(tested.first().at(1).toString()));

	// A plain GET that runs no model: pressing the button must never be billable.
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	QCOMPARE(server.received.first().path, QByteArray("/v1/models"));
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

void OpenAITextServiceTests::testApiKeyNeverAppearsInAnyLogOutput()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());

	g_captured.clear();
	g_previous = qInstallMessageHandler(capture);

	QStringList shown;
	{
		server.body = answerBody(QStringLiteral("fine"));
		QScopedPointer<OpenAITextService> ok(serviceFor(server));
		QSignalSpy done(ok.data(), &AITextService::completed);
		ok->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
		QVERIFY(done.wait(5000));

		server.status = 401;
		server.body = errorBody(QStringLiteral("Incorrect API key provided"));
		QScopedPointer<OpenAITextService> bad(serviceFor(server));
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
		QVERIFY2(!line.contains(QStringLiteral("TESTKEYDONOTLEAKME")), qPrintable(line));
	}
	for (const QString& message : std::as_const(shown))
		QVERIFY2(!message.contains(key), qPrintable(message));
}

QTEST_GUILESS_MAIN(OpenAITextServiceTests)
