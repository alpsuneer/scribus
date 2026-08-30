/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef GEMINITEXTSERVICETESTS_H
#define GEMINITEXTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for GeminiTextService - the client that asks Gemini for words,
 * not the Nano Banana client that asks it for pictures.
 *
 * Everything runs against the shared in-process mock. Nothing here contacts
 * generativelanguage.googleapis.com: this is the one provider the user of this
 * fork can actually pay for, which makes it more important rather than less
 * that the suite never spends anything.
 */
class GeminiTextServiceTests : public QObject
{
	Q_OBJECT
public:
	GeminiTextServiceTests() {}

private slots:
	void testCuratedModelListIsWellFormed();
	void testTextRequestWireFormat();
	void testVisionRequestUsesTheFlatInlineShape();
	void testAuthHeaderIsTheGoogleOne();

	void testSuccessReturnsTheAnswer();
	void testAnswerIsFoundInStepsWhenThereIsNoOutputText();
	void testHeadlineTaskSplitsIntoOptions();
	void testBlockReasonIsReportedAsARefusal();

	void testArrayWrappedErrorEnvelopeIsUnderstood();
	void testRejectedKeyIsReportedAsAKeyProblem_data();
	void testRejectedKeyIsReportedAsAKeyProblem();
	void testErrorStatusesAreReportedPromptly_data();
	void testErrorStatusesAreReportedPromptly();

	void testTimeoutIsReported();
	void testCancelAbortsAndStaysQuiet();
	void testConnectionAcceptsAKeyThatCanListModels();
	void testApiKeyNeverAppearsInAnyLogOutput();
};

#endif
