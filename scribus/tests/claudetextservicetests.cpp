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

#include "claudetextservicetests.h"
#include "aitextmockserver.h"
#include "ai/aitextprompts.h"
#include "ai/claudetextservice.h"

using namespace AITextTestData;

namespace
{
	//! Distinctive on purpose, so the confidentiality tests can look for it in
	//! places it must never be.
	const char* const TestKey = "sk-ant-api03-TESTKEYDONOTLEAKME-0123456789";

	//! A success envelope of the documented shape.
	QByteArray answerBody(const QString& text, int inputTokens = 120, int outputTokens = 30)
	{
		QJsonObject block;
		block.insert("type", "text");
		block.insert("text", text);
		QJsonArray content;
		content.append(block);
		QJsonObject usage;
		usage.insert("input_tokens", inputTokens);
		usage.insert("output_tokens", outputTokens);
		QJsonObject root;
		root.insert("id", "msg_01");
		root.insert("type", "message");
		root.insert("role", "assistant");
		root.insert("model", "claude-sonnet-5");
		root.insert("stop_reason", "end_turn");
		root.insert("content", content);
		root.insert("usage", usage);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	//! Anthropic's documented error envelope.
	QByteArray errorBody(const QString& type, const QString& message)
	{
		QJsonObject error;
		error.insert("type", type);
		error.insert("message", message);
		QJsonObject root;
		root.insert("type", "error");
		root.insert("error", error);
		return QJsonDocument(root).toJson(QJsonDocument::Compact);
	}

	ClaudeTextService* serviceFor(AITextMockServer& server,
	                              const QString& model = QStringLiteral("claude-sonnet-5"),
	                              int timeout = 5,
	                              const QString& key = QString::fromLatin1(TestKey))
	{
		return new ClaudeTextService(key, model, timeout, nullptr, server.apiBase());
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

// --- the parts that need no server ------------------------------------------

void ClaudeTextServiceTests::testPromptTableCoversEveryTask()
{
	const QStringList tasks = AITextService::allTasks();
	QCOMPARE(tasks.size(), 6);
	for (const QString& task : tasks)
	{
		const QString tmpl = AITextPrompts::templateFor(task);
		QVERIFY2(!tmpl.isEmpty(), qPrintable(task));
	}
	// An unknown task must produce nothing rather than a plausible-looking
	// empty instruction that would be sent to a model anyway.
	QVERIFY(AITextPrompts::templateFor(QStringLiteral("nonsense")).isEmpty());
	QVERIFY(AITextPrompts::promptFor(QStringLiteral("nonsense"), QStringLiteral("x")).isEmpty());

	// Which tasks want a picture is a property of the task, not of a provider.
	QVERIFY(AITextService::taskNeedsImage(QLatin1String(AITextService::TaskCaption)));
	QVERIFY(AITextService::taskNeedsImage(QLatin1String(AITextService::TaskAltText)));
	QVERIFY(!AITextService::taskNeedsImage(QLatin1String(AITextService::TaskHeadline)));
	QVERIFY(!AITextService::taskNeedsImage(QLatin1String(AITextService::TaskTranslate)));
}

void ClaudeTextServiceTests::testPromptPlaceholdersAreSubstituted()
{
	const QString article = sampleArticle();

	QVariantMap translate;
	translate.insert(QStringLiteral("targetLang"), QStringLiteral("Malayalam"));
	const QString t = AITextPrompts::promptFor(QLatin1String(AITextService::TaskTranslate),
	                                           article, translate);
	QVERIFY2(t.contains(QStringLiteral("Malayalam")), qPrintable(t));
	QVERIFY2(t.contains(article), "the article itself must reach the model");
	// Nothing may be left for a model to puzzle over.
	QVERIFY2(!t.contains(QStringLiteral("{{")), qPrintable(t));

	QVariantMap headline;
	headline.insert(QStringLiteral("count"), 5);
	const QString h = AITextPrompts::promptFor(QLatin1String(AITextService::TaskHeadline),
	                                           article, headline);
	QVERIFY2(h.contains(QStringLiteral("5 headline")), qPrintable(h));
	QVERIFY(!h.contains(QStringLiteral("{{")));

	// A missing parameter gets a sensible default rather than leaving the
	// placeholder in the text that is sent.
	const QString bare = AITextPrompts::promptFor(QLatin1String(AITextService::TaskTranslate),
	                                              article);
	QVERIFY2(bare.contains(QStringLiteral("English")), qPrintable(bare));
	QVERIFY(!bare.contains(QStringLiteral("{{")));

	// Length words become sentence counts, so the same button gives the same
	// shape of answer twice running.
	QVariantMap summarize;
	summarize.insert(QStringLiteral("length"), QStringLiteral("short"));
	const QString s = AITextPrompts::promptFor(QLatin1String(AITextService::TaskSummarize),
	                                           article, summarize);
	QVERIFY2(s.contains(QStringLiteral("1-2 sentences")), qPrintable(s));

	/* Input is substituted last and never rescanned. An article that itself
	   contains a placeholder must arrive at the model verbatim - it is the
	   user's text, not an instruction. */
	const QString hostile = AITextPrompts::promptFor(
		QLatin1String(AITextService::TaskImprove),
		QStringLiteral("Text with {{targetLang}} inside it"));
	QVERIFY2(hostile.contains(QStringLiteral("{{targetLang}} inside it")), qPrintable(hostile));
}

void ClaudeTextServiceTests::testCuratedModelListIsWellFormed()
{
	const QList<AITextProtocol::ModelChoice>& models = ClaudeTextService::models();
	QVERIFY(!models.isEmpty());

	QSet<QString> ids;
	for (const AITextProtocol::ModelChoice& choice : models)
	{
		QVERIFY(!choice.id.isEmpty());
		QVERIFY(!choice.displayName.isEmpty());
		QVERIFY(!choice.hint.isEmpty());
		QVERIFY2(!ids.contains(choice.id), qPrintable(choice.id));
		ids.insert(choice.id);
		/* Model ids are complete as written and take no date suffix. Checked
		   against the current model table on 30 Aug 2026; the ids this feature
		   was specified against - claude-sonnet-4-5, claude-opus-4-5 - are
		   superseded and would 404. */
		static const QRegularExpression dated(QStringLiteral("-20[0-9]{6}$"));
		QVERIFY2(!dated.match(choice.id).hasMatch(), qPrintable(choice.id));
		// Priced, because the result dialog quotes a cost and a zero there
		// would silently become "free".
		QVERIFY2(choice.inputPerMTok > 0.0 && choice.outputPerMTok > 0.0, qPrintable(choice.id));
	}
	QVERIFY2(ids.contains(ClaudeTextService::defaultModel()),
	         qPrintable(ClaudeTextService::defaultModel()));
	QCOMPARE(ClaudeTextService::displayNameFor(QStringLiteral("claude-sonnet-5")),
	         QStringLiteral("Claude Sonnet 5"));
	// Something not in the list is shown as itself rather than as nothing.
	QCOMPARE(ClaudeTextService::displayNameFor(QStringLiteral("who-what")),
	         QStringLiteral("who-what"));
}

// --- what goes out on the wire ----------------------------------------------

void ClaudeTextServiceTests::testTextRequestWireFormat()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("A summary."));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	QVariantMap params;
	params.insert(QStringLiteral("length"), QStringLiteral("medium"));
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize),
	                             sampleArticle(), params));
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const AITextMockServer::Request& request = server.received.first();
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/v1/messages"));

	/* Byte-comparison against the request this input has to produce, built the
	   same way the service builds it. This pins the whole body: field names,
	   their order, and the exact prompt including the substituted article. */
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
	expected.insert("model", QStringLiteral("claude-sonnet-5"));
	expected.insert("max_tokens", 2048);
	expected.insert("messages", messages);
	QCOMPARE(request.body, QJsonDocument(expected).toJson(QJsonDocument::Compact));

	// The Malayalam survived the trip as Malayalam. A JSON encoder that
	// mangled it would still produce a valid request and a plausible answer,
	// so this is worth asserting rather than assuming.
	const QString sent = request.json().value("messages").toArray().at(0).toObject()
	                     .value("content").toArray().at(0).toObject()
	                     .value("text").toString();
	QVERIFY2(sent.contains(QStringLiteral("കേരളത്തിൽ")), qPrintable(sent.left(120)));

	// A text task must not invent a picture.
	QVERIFY2(!request.body.contains("image"), "a text task sent an image block");
}

void ClaudeTextServiceTests::testVisionRequestPutsTheImageFirst()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("Malayalam: ചിത്രം\nEnglish: A picture"));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	AITextService::Request req;
	req.task = QLatin1String(AITextService::TaskCaption);
	req.inputImage = sampleImage();
	service->execute(req);
	QVERIFY(done.wait(5000));

	QCOMPARE(server.received.size(), 1);
	const QJsonArray content = server.received.first().json()
	                           .value("messages").toArray().at(0).toObject()
	                           .value("content").toArray();
	QCOMPARE(content.size(), 2);

	// Image first: Anthropic's own guidance, and the reason the builder puts it
	// there rather than appending it.
	const QJsonObject image = content.at(0).toObject();
	QCOMPARE(image.value("type").toString(), QStringLiteral("image"));
	const QJsonObject source = image.value("source").toObject();
	QCOMPARE(source.value("type").toString(), QStringLiteral("base64"));
	QCOMPARE(source.value("media_type").toString(), QStringLiteral("image/jpeg"));
	// The nested source is what distinguishes this from the other two
	// providers' flatter shapes; a flat block would be silently ignored.
	QVERIFY(!image.contains(QStringLiteral("data")));
	QVERIFY(!image.contains(QStringLiteral("image_url")));

	const QByteArray jpeg = QByteArray::fromBase64(source.value("data").toString().toLatin1());
	QVERIFY(!jpeg.isEmpty());
	QImage decoded;
	QVERIFY2(decoded.loadFromData(jpeg), "the picture must arrive as a readable JPEG");
	QCOMPARE(decoded.size(), QSize(40, 30));

	QCOMPARE(content.at(1).toObject().value("type").toString(), QStringLiteral("text"));
}

void ClaudeTextServiceTests::testAuthHeadersAreBothPresent()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("ok"));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove),
	                             QStringLiteral("some text")));
	QVERIFY(done.wait(5000));

	const AITextMockServer::Request& request = server.received.first();
	const QByteArray key = QByteArray(TestKey);
	QCOMPARE(request.header("x-api-key"), key);
	// Not optional: without the version header the API refuses the request.
	QCOMPARE(request.header("anthropic-version"), QByteArray("2023-06-01"));
	// Bearer is OpenAI's shape, not this one.
	QVERIFY(request.header("Authorization").isEmpty());

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

// --- what comes back --------------------------------------------------------

void ClaudeTextServiceTests::testSuccessReturnsTheAnswer()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("മഴ ശക്തമായി"));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize), sampleArticle()));
	QVERIFY(done.wait(5000));

	QCOMPARE(failed.count(), 0);
	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 1);
	QCOMPARE(response.results.first(), QStringLiteral("മഴ ശക്തമായി"));
	QCOMPARE(response.modelDisplayName, QStringLiteral("Claude Sonnet 5"));
}

void ClaudeTextServiceTests::testHeadlineTaskSplitsIntoOptions()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// Models number things however often they are asked not to.
	server.body = answerBody(QStringLiteral("1. Heavy rain hits Kerala\n"
	                                        "2) Downpour floods the district\n"
	                                        "- Rain batters the coast"));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskHeadline), sampleArticle()));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 3);
	// The numbering is stripped: it is the model's formatting, not part of the
	// headline, and it would end up in the frame.
	QCOMPARE(response.results.at(0), QStringLiteral("Heavy rain hits Kerala"));
	QCOMPARE(response.results.at(1), QStringLiteral("Downpour floods the district"));
	QCOMPARE(response.results.at(2), QStringLiteral("Rain batters the coast"));
}

void ClaudeTextServiceTests::testProseTasksAreNotSplitUp()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// A summary that happens to contain a line break is one summary.
	server.body = answerBody(QStringLiteral("First sentence.\nSecond sentence."));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize), sampleArticle()));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.results.size(), 1);
	QVERIFY(response.results.first().contains(QLatin1Char('\n')));
}

void ClaudeTextServiceTests::testTokensAndCostAreReported()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.body = answerBody(QStringLiteral("ok"), 1000000, 1000000);

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy done(service.data(), &AITextService::completed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(done.wait(5000));

	const auto response = done.first().at(0).value<AITextService::Response>();
	QCOMPARE(response.tokensUsed, 2000000);
	// Sonnet 5 is $2 per million in and $10 per million out, so exactly one
	// million of each is $12. The dialog quotes this, so it has to be arithmetic
	// rather than a guess.
	QVERIFY2(qAbs(response.estimatedCost - 12.0) < 0.0001,
	         qPrintable(QString::number(response.estimatedCost)));
}

void ClaudeTextServiceTests::testStopReasonRefusalIsReportedAsARefusal()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// A safety decline is a 200: HTTP success is not the same as an answer.
	QJsonObject details;
	details.insert("type", "refusal");
	details.insert("category", "cyber");
	details.insert("explanation", "This request was declined by safety policy.");
	QJsonObject root;
	root.insert("type", "message");
	root.insert("stop_reason", "refusal");
	root.insert("stop_details", details);
	root.insert("content", QJsonArray());
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	// A refusal is not a fault, and the caller shows it differently.
	QVERIFY2(AITextService::isRefusal(error), qPrintable(error));
	const QString shown = AITextService::strippedRefusal(error);
	QVERIFY2(shown.contains(QStringLiteral("safety policy")), qPrintable(shown));
	QVERIFY2(shown.contains(QStringLiteral("Claude Sonnet 5")), qPrintable(shown));
}

void ClaudeTextServiceTests::testBillingIsToldApartFromAPlainBadRequest()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	// Anthropic reports an exhausted balance as a 400, not its own status, so
	// keying off the number alone would send the user hunting in the wrong
	// place for something they only need to top up.
	server.status = 400;
	server.body = errorBody(QStringLiteral("invalid_request_error"),
	                        QStringLiteral("Your credit balance is too low to access the API."));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	const QString error = failed.first().at(0).toString();
	QVERIFY2(error.contains(QStringLiteral("billing"), Qt::CaseInsensitive), qPrintable(error));
	QVERIFY2(error.contains(QStringLiteral("console.anthropic.com")), qPrintable(error));
}

void ClaudeTextServiceTests::testErrorStatusesAreReportedPromptly_data()
{
	QTest::addColumn<int>("status");
	QTest::addColumn<QString>("errorType");
	QTest::addColumn<QString>("serverMessage");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<bool>("stall");

	/* Every status a user can meet, answered head-first with a body that never
	   completes - the shape that once turned a fast rejection into a
	   sixty-second timeout. Each must arrive inside two seconds and still say
	   what the user should go and do.

	   401 is not stalled, and that is a Qt limitation rather than a choice: 401
	   and 407 are the HTTP authentication statuses and Qt withholds the whole
	   response for them - no metaDataChanged, no readyRead, not even
	   authenticationRequired - until the body is complete, in case it has to
	   resend with credentials. Measured on Qt 6.8.2 while fixing the image
	   client: every other status fired metaDataChanged within 3 ms, 401 and 407
	   fired nothing at all. Real error bodies arrive whole, which is the row
	   below. */
	QTest::newRow("403 forbidden")    << 403 << QStringLiteral("permission_error")
	                                  << QStringLiteral("Not allowed") << QStringLiteral("permission") << true;
	QTest::newRow("429 rate limited") << 429 << QStringLiteral("rate_limit_error")
	                                  << QStringLiteral("Slow down") << QStringLiteral("rate limiting") << true;
	QTest::newRow("500 server error") << 500 << QStringLiteral("api_error")
	                                  << QStringLiteral("Boom") << QStringLiteral("unavailable") << true;
	QTest::newRow("529 overloaded")   << 529 << QStringLiteral("overloaded_error")
	                                  << QStringLiteral("Busy") << QStringLiteral("overloaded") << true;
	QTest::newRow("401 bad key")      << 401 << QStringLiteral("authentication_error")
	                                  << QStringLiteral("invalid x-api-key") << QStringLiteral("Claude API key") << false;
}

void ClaudeTextServiceTests::testErrorStatusesAreReportedPromptly()
{
	QFETCH(int, status);
	QFETCH(QString, errorType);
	QFETCH(QString, serverMessage);
	QFETCH(QString, expected);
	QFETCH(bool, stall);

	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = status;
	server.body = errorBody(errorType, serverMessage);
	server.stallAfterHead = stall;

	// The production sixty-second timeout on purpose: if the timeout is what
	// ends the request, this takes a minute and fails.
	QScopedPointer<ClaudeTextService> service(
		serviceFor(server, QStringLiteral("claude-sonnet-5"), 60));
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
	QVERIFY(!AITextService::isCancelled(error));
	QVERIFY(!error.contains(QString::fromLatin1(TestKey)));
}

// --- refusals before anything leaves ----------------------------------------

void ClaudeTextServiceTests::testMissingKeyIsRefusedBeforeAnyRequest()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());

	QScopedPointer<ClaudeTextService> service(
		serviceFor(server, QStringLiteral("claude-sonnet-5"), 5, QString()));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(5000));

	QVERIFY2(failed.first().at(0).toString().contains(QStringLiteral("Preferences")),
	         qPrintable(failed.first().at(0).toString()));
	// Nothing may go out without a key: not even the user's words.
	QCOMPARE(server.received.size(), 0);
}

void ClaudeTextServiceTests::testVisionTaskWithoutAPictureIsRefused()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy failed(service.data(), &AITextService::failed);
	// Caption with no image: the check happens here rather than at the far end,
	// so nothing is spent finding out.
	service->execute(textRequest(QLatin1String(AITextService::TaskCaption), QString()));
	QVERIFY(failed.wait(5000));
	QVERIFY2(failed.first().at(0).toString().contains(QStringLiteral("picture")),
	         qPrintable(failed.first().at(0).toString()));
	QCOMPARE(server.received.size(), 0);

	// And an empty text task likewise.
	QSignalSpy failedAgain(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskSummarize), QStringLiteral("   ")));
	QVERIFY(failedAgain.wait(5000));
	QCOMPARE(server.received.size(), 0);
}

void ClaudeTextServiceTests::testTimeoutIsReported()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;

	QScopedPointer<ClaudeTextService> service(
		serviceFor(server, QStringLiteral("claude-sonnet-5"), 1));
	QSignalSpy failed(service.data(), &AITextService::failed);
	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QVERIFY(failed.wait(10000));

	const QString error = failed.first().at(0).toString();
	// A timeout is not a cancel, and must not be reported as one.
	QVERIFY(!AITextService::isCancelled(error));
	QVERIFY2(error.contains(QStringLiteral("did not answer")), qPrintable(error));
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

void ClaudeTextServiceTests::testCancelAbortsAndStaysQuiet()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.delayMs = 4000;
	server.body = answerBody(QStringLiteral("too late"));

	QScopedPointer<ClaudeTextService> service(
		serviceFor(server, QStringLiteral("claude-sonnet-5"), 30));
	QSignalSpy failed(service.data(), &AITextService::failed);
	QSignalSpy done(service.data(), &AITextService::completed);

	service->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
	QTRY_VERIFY_WITH_TIMEOUT(server.received.size() == 1, 5000);
	service->cancel();

	// Cancelling still has to produce exactly one signal, or a caller that
	// cancels waits for one for ever - but it must not be an error anybody is
	// shown.
	QVERIFY(failed.wait(5000));
	QCOMPARE(done.count(), 0);
	QVERIFY(AITextService::isCancelled(failed.first().at(0).toString()));
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 5000);
}

// --- the Test Connection button ---------------------------------------------

void ClaudeTextServiceTests::testConnectionAcceptsAKeyThatCanListModels()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	QJsonObject one;
	one.insert("id", "claude-sonnet-5");
	QJsonArray data;
	data.append(one);
	QJsonObject root;
	root.insert("data", data);
	server.body = QJsonDocument(root).toJson(QJsonDocument::Compact);

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AITextService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));
	QVERIFY2(tested.first().at(0).toBool(), qPrintable(tested.first().at(1).toString()));

	QCOMPARE(server.received.size(), 1);
	// A plain GET that runs no model: pressing the button must never be
	// billable, and must never send the user's words anywhere.
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	QCOMPARE(server.received.first().path, QByteArray("/v1/models"));
	QVERIFY(server.received.first().body.isEmpty());
	QCOMPARE(server.received.first().header("x-api-key"), QByteArray(TestKey));
}

void ClaudeTextServiceTests::testConnectionFailsOnBadKey()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());
	server.status = 401;
	server.body = errorBody(QStringLiteral("authentication_error"),
	                        QStringLiteral("invalid x-api-key"));

	QScopedPointer<ClaudeTextService> service(serviceFor(server));
	QSignalSpy tested(service.data(), &AITextService::connectionTested);
	service->testConnection();
	QVERIFY(tested.wait(5000));

	QVERIFY(!tested.first().at(0).toBool());
	const QString detail = tested.first().at(1).toString();
	QVERIFY2(detail.contains(QStringLiteral("Claude API key")), qPrintable(detail));
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
 an error string ends up in a bug report or a screen share, and the user has no
 way of knowing it happened.
 */
void ClaudeTextServiceTests::testApiKeyNeverAppearsInAnyLogOutput()
{
	AITextMockServer server;
	QVERIFY(server.startOnAnyPort());

	g_captured.clear();
	g_previous = qInstallMessageHandler(capture);

	QStringList shown;
	{
		server.body = answerBody(QStringLiteral("fine"));
		QScopedPointer<ClaudeTextService> ok(serviceFor(server));
		QSignalSpy done(ok.data(), &AITextService::completed);
		ok->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
		QVERIFY(done.wait(5000));

		server.status = 401;
		server.body = errorBody(QStringLiteral("authentication_error"),
		                        QStringLiteral("invalid x-api-key"));
		QScopedPointer<ClaudeTextService> bad(serviceFor(server));
		QSignalSpy failed(bad.data(), &AITextService::failed);
		bad->execute(textRequest(QLatin1String(AITextService::TaskImprove), QStringLiteral("x")));
		QVERIFY(failed.wait(5000));
		shown << failed.first().at(0).toString();

		QSignalSpy tested(bad.data(), &AITextService::connectionTested);
		bad->testConnection();
		QVERIFY(tested.wait(5000));
		shown << tested.first().at(1).toString();
	}

	qInstallMessageHandler(g_previous);

	const QString key = QString::fromLatin1(TestKey);
	for (const QString& line : std::as_const(g_captured))
	{
		QVERIFY2(!line.contains(key), qPrintable(line));
		// A fragment would be just as bad as the whole thing.
		QVERIFY2(!line.contains(QStringLiteral("TESTKEYDONOTLEAKME")), qPrintable(line));
	}
	// The messages shown to the user are logs waiting to happen.
	for (const QString& message : std::as_const(shown))
		QVERIFY2(!message.contains(key), qPrintable(message));
}

// Guiless: these tests need an event loop for the sockets and QImage for the
// vision path, and nothing else. QTEST_MAIN would drag in the widget stack.
QTEST_GUILESS_MAIN(ClaudeTextServiceTests)
