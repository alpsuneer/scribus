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
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "lamainpaintservicetests.h"
#include "ai/lamainpaintservice.h"

namespace
{
	/*!
	 \brief Just enough HTTP to answer one request at a time.

	 Qt's QHttpServer module is not present in this build, so this is a
	 QTcpServer that reads a request, remembers it, and writes back whatever
	 the test told it to. It deliberately does not try to be a web server: no
	 keep-alive, no chunked encoding, one reply then close.
	 */
	class MockIOPaint : public QTcpServer
	{
	public:
		struct Request
		{
			QByteArray method;
			QByteArray path;
			QByteArray contentType;
			QByteArray body;
		};

		//! What to answer with.
		int status {200};
		QByteArray contentType {"image/png"};
		QByteArray body;
		//! Read the request and then never answer, for the timeout case.
		bool hang {false};
		//! Wait this long before answering, so a test has time to cancel.
		int delayMs {0};

		QList<Request> received;
		//! Sockets the far end closed on us - which is what a real abort
		//! looks like from here.
		int disconnectedEarly {0};

		bool startOnAnyPort()
		{
			return listen(QHostAddress::LocalHost, 0);
		}

		QString url() const
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

				// Wait for the whole declared body before answering.
				int contentLength = 0;
				QByteArray contentTypeSeen;
				for (const QByteArray& line : lines)
				{
					const QByteArray trimmed = line.trimmed();
					if (trimmed.toLower().startsWith("content-length:"))
						contentLength = trimmed.mid(15).trimmed().toInt();
					else if (trimmed.toLower().startsWith("content-type:"))
						contentTypeSeen = trimmed.mid(13).trimmed();
				}
				const int haveBody = buffer->size() - (headerEnd + 4);
				if (haveBody < contentLength)
					return;

				Request request;
				const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
				if (requestLine.size() >= 2)
				{
					request.method = requestLine.at(0);
					request.path = requestLine.at(1);
				}
				request.contentType = contentTypeSeen;
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
					out += body;
					socket->write(out);
					socket->flush();
					socket->disconnectFromHost();
				};
				if (delayMs > 0)
					QTimer::singleShot(delayMs, socket, reply);
				else
					reply();
			});
		}
	};

	QImage sampleImage(int w = 100, int h = 100)
	{
		QImage img(w, h, QImage::Format_RGB32);
		for (int y = 0; y < h; ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < w; ++x)
				line[x] = qRgb((x * 2) % 256, (y * 3) % 256, 128);
		}
		return img;
	}

	QImage sampleMask(int w = 100, int h = 100)
	{
		QImage mask(w, h, QImage::Format_Grayscale8);
		mask.fill(0);
		for (int y = 30; y < 70; ++y)
			for (int x = 30; x < 70; ++x)
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
}

void LamaInpaintServiceTests::testUrlNormalisation()
{
	// A bare host:port is what people type. Without a scheme QUrl reads it as
	// a relative path and the request goes nowhere, with nothing worth showing
	// anyone by way of explanation.
	QCOMPARE(LamaInpaintService::normaliseBaseUrl("localhost:8080"), QString("http://localhost:8080"));
	QCOMPARE(LamaInpaintService::normaliseBaseUrl("  http://localhost:8080/  "), QString("http://localhost:8080"));
	QCOMPARE(LamaInpaintService::normaliseBaseUrl("http://host:1/////"), QString("http://host:1"));
	QCOMPARE(LamaInpaintService::normaliseBaseUrl("https://example:9/x"), QString("https://example:9/x"));
	QCOMPARE(LamaInpaintService::normaliseBaseUrl("   "), QString());
}

void LamaInpaintServiceTests::testRequestWireFormat()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.contentType = "image/png";
	server.body = pngOf(sampleImage());

	const QImage image = sampleImage();
	const QImage mask = sampleMask();

	LamaInpaintService service(server.url(), 30);
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	service.inpaint(image, mask);
	QVERIFY(done.wait(20000));

	QCOMPARE(server.received.size(), 1);
	const MockIOPaint::Request& request = server.received.first();

	// The contract, exactly as a real IOPaint was observed to want it.
	QCOMPARE(request.method, QByteArray("POST"));
	QCOMPARE(request.path, QByteArray("/api/v1/inpaint"));
	QVERIFY2(request.contentType.startsWith("application/json"),
	         qPrintable(QStringLiteral("content type was '%1'").arg(QString::fromLatin1(request.contentType))));

	QJsonParseError parseError {};
	const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
	QCOMPARE(parseError.error, QJsonParseError::NoError);
	QVERIFY(doc.isObject());
	const QJsonObject obj = doc.object();

	// Exactly two keys, named exactly these: anything else and the server
	// either ignores it or rejects the lot.
	QCOMPARE(obj.keys().size(), 2);
	QVERIFY(obj.contains(QStringLiteral("image")));
	QVERIFY(obj.contains(QStringLiteral("mask")));

	// Both are plain base64 PNG - no data: prefix, which the server tolerates
	// but which would only make the body longer.
	const QByteArray imageBytes = QByteArray::fromBase64(obj.value("image").toString().toLatin1());
	const QByteArray maskBytes = QByteArray::fromBase64(obj.value("mask").toString().toLatin1());
	QVERIFY(!obj.value("image").toString().startsWith(QStringLiteral("data:")));
	QCOMPARE(imageBytes.left(8), QByteArray::fromHex("89504e470d0a1a0a"));   // PNG magic
	QCOMPARE(maskBytes.left(8), QByteArray::fromHex("89504e470d0a1a0a"));

	QImage sentImage;
	QImage sentMask;
	QVERIFY(sentImage.loadFromData(imageBytes, "PNG"));
	QVERIFY(sentMask.loadFromData(maskBytes, "PNG"));
	QCOMPARE(sentImage.size(), QSize(100, 100));
	QCOMPARE(sentMask.size(), QSize(100, 100));

	// The picture must arrive with its colours intact...
	QCOMPARE(qRed(sentImage.pixel(10, 10)), 20);
	QCOMPARE(qGreen(sentImage.pixel(10, 10)), 30);
	// ...and the mask the right way round: white is what gets regenerated.
	QCOMPARE(qGray(sentMask.pixel(50, 50)), 255);
	QCOMPARE(qGray(sentMask.pixel(5, 5)), 0);
}

void LamaInpaintServiceTests::testSuccessReturnsTheServersImage()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());

	QImage answer(100, 100, QImage::Format_RGB32);
	answer.fill(qRgb(7, 200, 42));
	server.contentType = "image/png";
	server.body = pngOf(answer);

	LamaInpaintService service(server.url(), 30);
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(done.wait(20000));
	QCOMPARE(failed.size(), 0);

	const QImage result = done.first().first().value<QImage>();
	QCOMPARE(result.size(), QSize(100, 100));
	// The server's pixels, not the ones that were sent.
	QCOMPARE(qRed(result.pixel(50, 50)), 7);
	QCOMPARE(qGreen(result.pixel(50, 50)), 200);
	QCOMPARE(qBlue(result.pixel(50, 50)), 42);
}

void LamaInpaintServiceTests::testConnectionSucceedsAndNamesTheModel()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.contentType = "application/json";
	server.body = R"({"name":"lama","path":"lama","model_type":"inpaint"})";

	LamaInpaintService service(server.url(), 30);
	QSignalSpy tested(&service, &AIInpaintService::connectionTested);
	service.testConnection();
	QVERIFY(tested.wait(20000));

	QCOMPARE(tested.first().at(0).toBool(), true);
	// Naming the model back is the difference between "something answered"
	// and "the thing that answered can inpaint".
	QVERIFY2(tested.first().at(1).toString().contains(QStringLiteral("lama")),
	         qPrintable(tested.first().at(1).toString()));

	QCOMPARE(server.received.size(), 1);
	QCOMPARE(server.received.first().method, QByteArray("GET"));
	QCOMPARE(server.received.first().path, QByteArray("/api/v1/model"));
}

void LamaInpaintServiceTests::testConnectionFailsWhenRefused()
{
	// Bind a port and drop it, so the port is almost certainly closed.
	quint16 deadPort = 0;
	{
		MockIOPaint probe;
		QVERIFY(probe.startOnAnyPort());
		deadPort = probe.serverPort();
	}

	LamaInpaintService service(QStringLiteral("http://127.0.0.1:%1").arg(deadPort), 30);
	QSignalSpy tested(&service, &AIInpaintService::connectionTested);
	service.testConnection();
	QVERIFY(tested.wait(20000));

	QCOMPARE(tested.first().at(0).toBool(), false);
	const QString detail = tested.first().at(1).toString();
	// The message has to say where it tried, or it is no help at all.
	QVERIFY2(detail.contains(QString::number(deadPort)), qPrintable(detail));
}

void LamaInpaintServiceTests::testConnectionFailsOnWrongPath()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.status = 404;
	server.contentType = "text/plain";
	server.body = "Not Found";

	LamaInpaintService service(server.url(), 30);
	QSignalSpy tested(&service, &AIInpaintService::connectionTested);
	service.testConnection();
	QVERIFY(tested.wait(20000));

	QCOMPARE(tested.first().at(0).toBool(), false);
	QVERIFY2(tested.first().at(1).toString().contains(QStringLiteral("404")),
	         qPrintable(tested.first().at(1).toString()));
}

void LamaInpaintServiceTests::testHttpErrorReportsServerDetail()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.status = 500;
	server.contentType = "application/json";
	// The shape a real IOPaint answers a bad request with.
	server.body = R"({"error":"Error","detail":"","body":"","errors":"Invalid base64-encoded string"})";

	LamaInpaintService service(server.url(), 30);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(20000));

	const QString message = failed.first().first().toString();
	// The user gets the server's own words, not "an error occurred".
	QVERIFY2(message.contains(QStringLiteral("Invalid base64-encoded string")), qPrintable(message));
	QVERIFY2(message.contains(QStringLiteral("500")), qPrintable(message));
	QVERIFY(!AIInpaintService::isCancelled(message));
}

void LamaInpaintServiceTests::testHttpErrorWithoutJsonStillSaysSomething()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.status = 400;
	server.contentType = "text/plain";
	server.body = "Bad Request: mask and image must be the same size";

	LamaInpaintService service(server.url(), 30);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(20000));

	const QString message = failed.first().first().toString();
	QVERIFY2(message.contains(QStringLiteral("400")), qPrintable(message));
	QVERIFY2(message.contains(QStringLiteral("same size")), qPrintable(message));
}

void LamaInpaintServiceTests::testConnectionRefusedIsReported()
{
	quint16 deadPort = 0;
	{
		MockIOPaint probe;
		QVERIFY(probe.startOnAnyPort());
		deadPort = probe.serverPort();
	}

	LamaInpaintService service(QStringLiteral("http://127.0.0.1:%1").arg(deadPort), 30);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(20000));
	QCOMPARE(done.size(), 0);

	const QString message = failed.first().first().toString();
	QVERIFY2(message.contains(QString::number(deadPort)), qPrintable(message));
	QVERIFY(!AIInpaintService::isCancelled(message));
}

void LamaInpaintServiceTests::testTimeoutIsReported()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.hang = true;   // reads the request, never answers

	// One second, so the test does not sit here.
	LamaInpaintService service(server.url(), 1);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(20000));

	const QString message = failed.first().first().toString();
	// A timeout and a cancel both surface as an aborted request, and they must
	// not be reported as each other.
	QVERIFY2(message.contains(QStringLiteral("did not answer")), qPrintable(message));
	QVERIFY(!AIInpaintService::isCancelled(message));
}

void LamaInpaintServiceTests::testCancelAbortsTheRequest()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.contentType = "image/png";
	server.body = pngOf(sampleImage());
	server.delayMs = 5000;   // long enough that cancel certainly lands first

	LamaInpaintService service(server.url(), 60);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	service.inpaint(sampleImage(), sampleMask());

	// Let the request actually reach the server before pulling it.
	QTRY_COMPARE_WITH_TIMEOUT(server.received.size(), 1, 20000);
	service.cancel();

	QVERIFY(failed.wait(20000));
	QCOMPARE(done.size(), 0);
	// Cancelling is answered, so no caller waits for ever, but it is marked
	// rather than described so that nobody shows it to a user as an error.
	QVERIFY(AIInpaintService::isCancelled(failed.first().first().toString()));

	// And it really was aborted: the far end saw the connection go before it
	// had answered, rather than the answer being received and thrown away.
	QTRY_VERIFY_WITH_TIMEOUT(server.disconnectedEarly >= 1, 20000);
}

void LamaInpaintServiceTests::testNonImageResponseIsRejected()
{
	MockIOPaint server;
	QVERIFY(server.startOnAnyPort());
	server.status = 200;
	server.contentType = "text/html";
	server.body = "<html>this is not a picture</html>";

	LamaInpaintService service(server.url(), 30);
	QSignalSpy failed(&service, &AIInpaintService::inpaintFailed);
	QSignalSpy done(&service, &AIInpaintService::inpaintFinished);
	service.inpaint(sampleImage(), sampleMask());
	QVERIFY(failed.wait(20000));
	QCOMPARE(done.size(), 0);
	QVERIFY2(failed.first().first().toString().contains(QStringLiteral("not an image")),
	         qPrintable(failed.first().first().toString()));
}

// Guiless: these tests need an event loop for the sockets, and nothing else.
// QTEST_MAIN would drag in QApplication and the whole widget stack with it.
QTEST_GUILESS_MAIN(LamaInpaintServiceTests)
