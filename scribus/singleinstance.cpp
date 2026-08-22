/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>

namespace
{
	// Long enough that a busy first instance still answers, short enough that a
	// dead socket does not stall a double-click.
	const int ConnectTimeoutMs = 800;
	const int WriteTimeoutMs   = 2000;
	const int ReadTimeoutMs    = 2000;
}

bool SingleInstance::s_disabled = false;

SingleInstance::SingleInstance(QObject* parent)
	: QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
	if (m_server)
	{
		m_server->close();
		QLocalServer::removeServer(m_server->serverName());
	}
}

bool SingleInstance::isDisabled()
{
	if (s_disabled)
		return true;
	return QProcessEnvironment::systemEnvironment().contains("SCRIBUS_NO_SINGLE_INSTANCE");
}

void SingleInstance::setDisabled(bool disabled)
{
	s_disabled = disabled;
}

QString SingleInstance::serverName()
{
	// Per-user AND per-display. Two desktops for the same user (a local X
	// session and a Kasm one) must not post documents into each other.
	const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	QString key = env.value("USER", QString::number(::geteuid()));
	key += '-';
	key += env.value("DISPLAY", QStringLiteral("nodisplay"));
	// The name becomes a filesystem path, so hash rather than sanitise by hand.
	const QByteArray digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
	return QStringLiteral("scribus-si-") + QString::fromLatin1(digest);
}

bool SingleInstance::sendToRunningInstance(const QStringList& files)
{
	if (isDisabled() || files.isEmpty())
		return false;

	QLocalSocket socket;
	socket.connectToServer(serverName());
	if (!socket.waitForConnected(ConnectTimeoutMs))
		return false;   // nobody listening, or the socket is stale

	QByteArray payload;
	{
		QDataStream out(&payload, QIODevice::WriteOnly);
		out.setVersion(QDataStream::Qt_5_0);
		out << files;
	}
	socket.write(payload);
	if (!socket.waitForBytesWritten(WriteTimeoutMs))
		return false;
	// Wait for the running instance to acknowledge. Without this a fast exit can
	// close the socket before it has read, and the file silently never opens.
	socket.waitForReadyRead(ReadTimeoutMs);
	socket.disconnectFromServer();
	return true;
}

bool SingleInstance::listen()
{
	if (isDisabled())
		return false;

	m_server = new QLocalServer(this);
	// Do not accept connections from other users.
	m_server->setSocketOptions(QLocalServer::UserAccessOption);

	if (!m_server->listen(serverName()))
	{
		// A crashed instance leaves its socket file behind and listen() fails
		// with AddressInUseError forever after. sendToRunningInstance() has
		// already failed to connect by this point, so nothing live owns it:
		// remove it and take over.
		if (m_server->serverError() == QAbstractSocket::AddressInUseError)
		{
			QLocalServer::removeServer(serverName());
			if (!m_server->listen(serverName()))
			{
				delete m_server;
				m_server = nullptr;
				return false;
			}
		}
		else
		{
			delete m_server;
			m_server = nullptr;
			return false;
		}
	}

	connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
	return true;
}

void SingleInstance::onNewConnection()
{
	while (QLocalSocket* socket = m_server->nextPendingConnection())
	{
		connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
		if (!socket->waitForReadyRead(ReadTimeoutMs))
		{
			socket->disconnectFromServer();
			continue;
		}
		QStringList files;
		{
			QDataStream in(socket);
			in.setVersion(QDataStream::Qt_5_0);
			in >> files;
		}
		// Acknowledge before doing anything slow, so the sender can exit.
		socket->write("ok");
		socket->waitForBytesWritten(WriteTimeoutMs);
		socket->disconnectFromServer();

		if (!files.isEmpty())
			emit filesReceived(files);
	}
}
