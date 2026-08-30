/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTMOCKSERVER_H
#define AITEXTMOCKSERVER_H

#include <QByteArray>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

/*!
 \brief Just enough HTTP to answer one request at a time, shared by the three
 text-provider test suites.

 The same shape as the mock in the image-service tests - Qt's QHttpServer
 module is not present in this build - and it keeps the whole request head as
 well, because half of what matters is which headers were sent.

 None of these suites may contact a real provider: they must pass on a machine
 with no account, no key and no network, and the user has billing for exactly
 one of the three. Every service takes its API root as a constructor argument
 for this reason and only the tests ever pass it; AITextServiceFactory does
 not, so a running Scribus has one possible host per provider.
 */
class AITextMockServer : public QTcpServer
{
public:
	struct Request
	{
		QByteArray method;
		QByteArray path;
		QByteArray head;     //!< every header line, verbatim
		QByteArray body;

		QByteArray header(const char* name) const
		{
			const QList<QByteArray> lines = head.split('\n');
			const QByteArray want = QByteArray(name).toLower() + ":";
			for (const QByteArray& line : lines)
			{
				const QByteArray trimmed = line.trimmed();
				if (trimmed.toLower().startsWith(want))
					return trimmed.mid(want.size()).trimmed();
			}
			return QByteArray();
		}

		QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
	};

	int status {200};
	QByteArray contentType {"application/json"};
	QByteArray body;
	bool hang {false};
	int delayMs {0};
	/*! \brief Answer with the head and the first few bytes of the body, then go
	    quiet with the connection still open.

	    The shape a fast rejection took on the live OpenRouter API, which used
	    to be reported as a sixty-second timeout because nothing read the reply
	    until finished() and that reply never finished. The text services were
	    written with that already fixed; these tests are what keeps it fixed. */
	bool stallAfterHead {false};

	QList<Request> received;
	int disconnectedEarly {0};

	bool startOnAnyPort() { return listen(QHostAddress::LocalHost, 0); }

	//! What the service is handed as its API root.
	QString apiBase() const
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

			int contentLength = 0;
			for (const QByteArray& line : lines)
			{
				const QByteArray trimmed = line.trimmed();
				if (trimmed.toLower().startsWith("content-length:"))
					contentLength = trimmed.mid(15).trimmed().toInt();
			}
			if (buffer->size() - (headerEnd + 4) < contentLength)
				return;

			Request request;
			const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
			if (requestLine.size() >= 2)
			{
				request.method = requestLine.at(0);
				request.path = requestLine.at(1);
			}
			request.head = head;
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
				// Truncating the body while promising the full Content-Length is
				// what leaves the reply unfinished. The connection stays open, so
				// there is no error either - just silence.
				out += stallAfterHead ? body.left(qMin(4, body.size())) : body;
				socket->write(out);
				socket->flush();
				if (!stallAfterHead)
					socket->disconnectFromHost();
			};
			if (delayMs > 0)
				QTimer::singleShot(delayMs, socket, reply);
			else
				reply();
		});
	}
};

namespace AITextTestData
{
	//! A small picture for the vision tasks. Distinctive colour so a test can
	//! tell it apart from an empty or default image.
	inline QImage sampleImage(int w = 40, int h = 30)
	{
		QImage img(w, h, QImage::Format_RGB32);
		img.fill(qRgb(20, 140, 200));
		return img;
	}

	//! A paragraph standing in for an article. Deliberately not Latin-only:
	//! every document this feature exists for is Malayalam, and a prompt
	//! pipeline that mangles the input would pass a test written in ASCII.
	inline QString sampleArticle()
	{
		return QStringLiteral("കേരളത്തിൽ ഇന്ന് ശക്തമായ മഴ പെയ്തു. "
		                      "Heavy rain was recorded across the district today.");
	}
}

#endif
