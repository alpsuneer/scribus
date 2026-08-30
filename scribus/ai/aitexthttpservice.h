/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTHTTPSERVICE_H
#define AITEXTHTTPSERVICE_H

#include <memory>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QThread>
#include <QUrl>

#include "ai/aitextservice.h"
#include "scribusapi.h"

class QNetworkRequest;
class AITextHttpWorker;

/*!
 \brief Everything three text providers do identically, written once.

 Claude, OpenAI and Gemini differ in about forty lines each: the URL, the auth
 header, the shape of the request body and where the answer sits in the reply.
 Everything around that is the same, and it is the part that is easy to get
 wrong - a private thread so the GUI never blocks, a timeout that can be told
 apart from a cancel, reading the response head as it arrives so a fast
 rejection is reported in a second rather than at the timeout, and never
 letting the key reach a URL or a message.

 That last set was learned the hard way on the image side, where a 402 that
 arrived in under a second was reported as "did not answer within 60 seconds"
 because nothing read the reply until finished() and that reply never finished.
 Three copies of that logic would be three chances to reintroduce it, so there
 is one, and each provider supplies only what actually differs.

 \section threading Threading

 The protocol is shared between the service and its worker thread rather than
 owned by one of them, which is safe because every one of its methods is const
 and it holds no state at all: it is a table of provider facts plus pure
 functions over its arguments. Give it a member that changes and that stops
 being true.
 */
class SCRIBUS_API AITextProtocol
{
public:
	//! One entry of a provider's curated model list.
	struct ModelChoice
	{
		QString id;           //!< Model id as the provider spells it
		QString displayName;  //!< What the dropdown shows
		QString hint;         //!< One line on when to reach for it
		//! USD per million input / output tokens, for the cost estimate shown
		//! to the user. Approximate by nature and marked as such in the UI.
		double inputPerMTok {0.0};
		double outputPerMTok {0.0};
	};

	//! A request ready to be sent, as the protocol wants it built.
	struct Prepared
	{
		QUrl url;
		QByteArray body;      //!< Empty means send a GET
		bool post {true};
	};

	virtual ~AITextProtocol();

	virtual QString providerName() const = 0;
	virtual QStringList supportedTasks() const = 0;
	virtual bool supportsVision() const = 0;

	//! The key goes in whatever header this provider uses, and nowhere else.
	virtual void applyHeaders(QNetworkRequest& request, const QString& apiKey) const = 0;

	/*! \param prompt already filled in from aitextprompts.h.
	    \param jpegBase64 empty for a text-only task. */
	virtual Prepared buildExecute(const QString& apiBase, const QString& model,
	                              const QString& prompt, const QByteArray& jpegBase64,
	                              int maxTokens) const = 0;

	//! The cheapest call that proves a key. Must not run a model if avoidable.
	virtual Prepared buildTest(const QString& apiBase, const QString& model) const = 0;

	/*! \brief The model's answer, or why there is not one.

	    \param root the parsed response body.
	    \param text set to what the model wrote.
	    \param refusal set when the model declined; reported as a refusal
	           rather than as a fault, and never retried.
	    \return false when neither could be found. */
	virtual bool parseAnswer(const QJsonObject& root, QString& text, QString& refusal) const = 0;

	//! Token counts out of the reply, if it reported them. 0 when it did not.
	virtual void parseUsage(const QJsonObject& root, int& inputTokens, int& outputTokens) const = 0;

	//! The user-facing sentence for a non-2xx status.
	virtual QString describeStatus(int status, const QByteArray& body) const = 0;

	//! What to say when the key check succeeded.
	virtual QString describeTestSuccess(const QJsonObject& root) const = 0;

	virtual const QList<ModelChoice>& models() const = 0;
	QString displayNameFor(const QString& modelId) const;
	const ModelChoice* choiceFor(const QString& modelId) const;
};

/*!
 \brief A text service that speaks HTTP, driven by an AITextProtocol.

 Concrete services derive from this and hand it a protocol; they add only their
 own statics (their model list, their defaults) on top.
 */
class SCRIBUS_API AITextHttpService : public AITextService
{
	Q_OBJECT

public:
	AITextHttpService(std::shared_ptr<const AITextProtocol> protocol,
	                  const QString& apiKey, const QString& model,
	                  int timeoutSeconds, QObject* parent,
	                  const QString& apiBase);
	~AITextHttpService() override;

	QString name() const override;
	QStringList supportedTasks() const override;
	bool supportsVision() const override;
	void execute(const Request& req) override;
	void cancel() override;
	void testConnection() override;

	//! Change key, model and timeout without tearing the service down.
	void setCredentials(const QString& apiKey, const QString& model, int timeoutSeconds);

	QString model() const { return m_model; }

	/*! \brief Longest edge, in pixels, of a picture sent to a vision model.

	    Above this a model downscales anyway and charges for the privilege, so
	    the shrinking happens here where it also cuts the upload. */
	static int maxImageEdge();

	//! JPEG-encode and base64 a picture for a vision request, downscaling it
	//! to maxImageEdge() first. Empty on failure.
	static QByteArray encodeImage(const QImage& image);

private:
	//! Post a refusal so that every exit from execute() is asynchronous, the
	//! same as a reply from the far end. See the note on the definition.
	void failLater(const QString& message);

	QThread m_thread;
	AITextHttpWorker* m_worker {nullptr};
	//! Const and stateless, so both threads may read it. See the note above.
	std::shared_ptr<const AITextProtocol> m_protocol;
	QString m_apiKey;
	QString m_model;
	QString m_apiBase;
	int m_timeoutSeconds {60};
};

#endif
