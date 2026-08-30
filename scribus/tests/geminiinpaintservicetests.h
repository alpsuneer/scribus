/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef GEMINIINPAINTSERVICETESTS_H
#define GEMINIINPAINTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for GeminiInpaintService, the client that talks to Google's
 * Gemini API directly.
 *
 * Everything here runs against a mock server in this process. Nothing in this
 * file may contact generativelanguage.googleapis.com: these tests must be
 * runnable on a machine with no account, no key and no network, and a test
 * suite that spends money per run is not one anybody will keep running.
 *
 * The service takes its API root as a constructor argument for exactly this
 * reason, and only these tests ever pass it. AIInpaintServiceFactory does not,
 * so the running program has one possible host.
 *
 * The wire format asserted on here was verified on 30 Aug 2026 against
 * https://ai.google.dev/gemini-api/docs/image-generation and
 * https://ai.google.dev/api/interactions-api. If Google changes it,
 * testRequestWireFormat() is the test that will say so.
 */
class GeminiInpaintServiceTests : public QObject
{
	Q_OBJECT
public:
	GeminiInpaintServiceTests() {}

private slots:
	void testCuratedModelListIsWellFormed();
	void testShortAndFileNamesForModels();

	void testRequestWireFormat();
	void testApiKeyTravelsOnlyInItsOwnHeader();
	void testModelRoutingChangesOnlyTheModelField();
	void testMaskIsPaintedIntoTheImageThatIsSent();

	void testSuccessReturnsTheModelsImage();
	void testImageIsFoundInStepsWhenThereIsNoOutputImage();
	void testTextAlongsideTheImageIsIgnored();

	void testTextOnlyAnswerIsReportedAsARefusal();
	void testBlockReasonIsReportedAsARefusal();
	void testInteractionErrorsAreReportedAsARefusal();

	void testErrorStatusesAreReportedPromptly_data();
	void testErrorStatusesAreReportedPromptly();
	void testArrayWrappedErrorEnvelopeIsUnderstood();
	void testRejectedKeyIsReportedAsAKeyProblem_data();
	void testRejectedKeyIsReportedAsAKeyProblem();

	void testMissingKeyIsRefusedBeforeAnyRequest();
	void testTimeoutIsReported();
	void testCancelAbortsAndStaysQuiet();

	void testConnectionAcceptsAKeyThatCanListModels();
	void testConnectionFailsOnBadKey();

	void testApiKeyNeverAppearsInAnyLogOutput();
};

#endif
