/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef CLAUDETEXTSERVICETESTS_H
#define CLAUDETEXTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for ClaudeTextService, the client for Anthropic's Messages API.
 *
 * Everything here runs against a mock server in this process. Nothing in this
 * file may contact api.anthropic.com: these tests must be runnable on a machine
 * with no account, no key and no network, and the user of this fork has billing
 * for Gemini only.
 *
 * The wire format asserted on here was verified on 30 Aug 2026 against the
 * documentation linked from claudetextservice.h. If Anthropic changes it,
 * testTextRequestWireFormat() is the test that will say so.
 */
class ClaudeTextServiceTests : public QObject
{
	Q_OBJECT
public:
	ClaudeTextServiceTests() {}

private slots:
	void testPromptTableCoversEveryTask();
	void testPromptPlaceholdersAreSubstituted();
	void testCuratedModelListIsWellFormed();

	void testTextRequestWireFormat();
	void testVisionRequestPutsTheImageFirst();
	void testAuthHeadersAreBothPresent();

	void testSuccessReturnsTheAnswer();
	void testHeadlineTaskSplitsIntoOptions();
	void testProseTasksAreNotSplitUp();
	void testTokensAndCostAreReported();

	void testStopReasonRefusalIsReportedAsARefusal();
	void testBillingIsToldApartFromAPlainBadRequest();

	void testErrorStatusesAreReportedPromptly_data();
	void testErrorStatusesAreReportedPromptly();

	void testMissingKeyIsRefusedBeforeAnyRequest();
	void testVisionTaskWithoutAPictureIsRefused();
	void testTimeoutIsReported();
	void testCancelAbortsAndStaysQuiet();

	void testConnectionAcceptsAKeyThatCanListModels();
	void testConnectionFailsOnBadKey();
	void testApiKeyNeverAppearsInAnyLogOutput();
};

#endif
