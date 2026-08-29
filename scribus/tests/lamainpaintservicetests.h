/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef LAMAINPAINTSERVICETESTS_H
#define LAMAINPAINTSERVICETESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for LamaInpaintService, the client for a local IOPaint server.
 *
 * Everything here runs against a mock server in this process, never against a
 * real IOPaint: a test that needs a model loaded and a GPU warm is not a test,
 * it is a coin toss. The mock is a few lines of QTcpServer because Qt's
 * QHttpServer module is not present in this build.
 *
 * The wire format the mock asserts on was established by asking a real running
 * IOPaint, not by reading documentation - see the note in
 * lamainpaintservice.h. If IOPaint ever changes it, testRequestWireFormat()
 * is the test that will say so.
 */
class LamaInpaintServiceTests : public QObject
{
	Q_OBJECT
public:
	LamaInpaintServiceTests() {}

private slots:
	void testUrlNormalisation();

	void testRequestWireFormat();
	void testSuccessReturnsTheServersImage();

	void testConnectionSucceedsAndNamesTheModel();
	void testConnectionFailsWhenRefused();
	void testConnectionFailsOnWrongPath();

	void testHttpErrorReportsServerDetail();
	void testHttpErrorWithoutJsonStillSaysSomething();
	void testConnectionRefusedIsReported();
	void testTimeoutIsReported();
	void testCancelAbortsTheRequest();
	void testNonImageResponseIsRejected();
};

#endif
