/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OPENAITEXTSERVICETESTS_H
#define OPENAITEXTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for OpenAITextService, the client for OpenAI's Chat Completions
 * API.
 *
 * Everything runs against the shared in-process mock. Nothing here contacts
 * api.openai.com: these must pass with no account, no key and no network.
 */
class OpenAITextServiceTests : public QObject
{
	Q_OBJECT
public:
	OpenAITextServiceTests() {}

private slots:
	void testCuratedModelListIsWellFormed();
	void testTextRequestWireFormat();
	void testVisionRequestUsesADataUrl();
	void testAuthHeaderIsABearerToken();

	void testSuccessReturnsTheAnswer();
	void testHeadlineTaskSplitsIntoOptions();
	void testRefusalFieldIsReportedAsARefusal();
	void testExhaustedQuotaIsNotReportedAsARateLimit();

	void testErrorStatusesAreReportedPromptly_data();
	void testErrorStatusesAreReportedPromptly();

	void testTimeoutIsReported();
	void testCancelAbortsAndStaysQuiet();
	void testConnectionAcceptsAKeyThatCanListModels();
	void testApiKeyNeverAppearsInAnyLogOutput();
};

#endif
