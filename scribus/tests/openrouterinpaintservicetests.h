/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef OPENROUTERINPAINTSERVICETESTS_H
#define OPENROUTERINPAINTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for OpenRouterInpaintService, the client for OpenRouter's
 * unified image API.
 *
 * Everything here runs against a mock server in this process. Nothing in this
 * file may contact openrouter.ai: these tests must be runnable on a machine
 * with no account, no key and no network, and a test suite that spends money
 * per run is not one anybody will keep running.
 *
 * The service takes its API root as a constructor argument for exactly this
 * reason, and only these tests ever pass it. AIInpaintServiceFactory does not,
 * so the running program has one possible host.
 *
 * The wire format asserted on here was verified on 29 Aug 2026 against the
 * live https://openrouter.ai/api/v1/images/models and the documentation
 * linked from openrouterinpaintservice.h. If OpenRouter changes it,
 * testRequestWireFormat() is the test that will say so.
 */
class OpenRouterInpaintServiceTests : public QObject
{
	Q_OBJECT
public:
	OpenRouterInpaintServiceTests() {}

private slots:
	void testCuratedModelListIsWellFormed();
	void testShortAndFileNamesForModels();
	void testRefusalMarkerRoundTrips();

	void testRequestWireFormat();
	void testAuthAndAttributionHeaders();
	void testModelRoutingChangesOnlyTheModelField();
	void testMaskIsPaintedIntoTheReferenceImage();

	void testSuccessReturnsTheModelsImage();
	void testDataUrlResultIsAlsoAccepted();
	void testRemoteUrlResultIsRefusedRatherThanFetched();

	void testTextOnlyAnswerIsReportedAsARefusal();
	void testModerationErrorIsReportedAsARefusal();

	void testUnauthorisedMentionsTheApiKey();
	void testPaymentRequiredMentionsCredits();
	void testRateLimitIsReported();
	void testServerErrorNamesTheProvider();

	void testMissingKeyIsRefusedBeforeAnyRequest();
	void testFastRejectionDoesNotBecomeATimeout();
	void testErrorStatusesAreReportedPromptly_data();
	void testErrorStatusesAreReportedPromptly();
	void testTimeoutIsReported();
	void testCancelAbortsAndStaysQuiet();

	void testConnectionReportsRemainingCredit();
	void testConnectionFailsOnBadKey();

	void testApiKeyNeverLeavesTheAuthorizationHeader();
};

#endif
