/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTSERVICE_H
#define AITEXTSERVICE_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "scribusapi.h"

/*!
 \brief A service that answers questions about words, and about pictures in
 words.

 Deliberately *not* the same interface as AIInpaintService next door, and the
 two must not be merged. An inpainting service takes a picture and a mask and
 gives back a picture: one shape of question, one shape of answer, and the
 answer replaces pixels. This takes a task name, some text and sometimes a
 picture, and gives back one or several pieces of writing that a person then
 reads, edits, accepts or throws away. The failure modes differ too - a model
 that writes a bad headline has still succeeded as far as HTTP is concerned,
 and the user is the only judge.

 Everything here is asynchronous and reports through signals, because the
 implementations talk to something over a socket and none of that may happen on
 the GUI thread. Exactly one of completed() or failed() follows each execute()
 call - including when the caller cancels, which arrives as failed() carrying
 cancelledMarker() so that no caller can be left waiting for a signal that
 never comes.

 Nothing here runs on its own. Every call is started by the user from a menu,
 and the result is shown for approval before it can touch the document.

 Implementations must contact the address configured for them and no other. No
 fallback provider, no discovery, no telemetry: the user's words leave this
 machine only if the user asked for that, to the one service they picked.
 */
class SCRIBUS_API AITextService : public QObject
{
	Q_OBJECT

public:
	//! Task names. Constants rather than an enum because they cross the
	//! interface as strings, are stored in QAction data, and appear in the
	//! prompt table in aitextprompts.h.
	static const char* const TaskCaption;    //!< Picture in, caption out
	static const char* const TaskHeadline;   //!< Article in, headline options out
	static const char* const TaskSummarize;  //!< Article in, short summary out
	static const char* const TaskTranslate;  //!< Text in, translation out
	static const char* const TaskImprove;    //!< Text in, tidied text out
	static const char* const TaskAltText;    //!< Picture in, alt text out

	//! Every task this file knows about, in menu order.
	static QStringList allTasks();
	//! True for the two tasks that need a picture rather than words.
	static bool taskNeedsImage(const QString& task);

	struct Request
	{
		QString task;             //!< One of the Task* constants above
		QString inputText;        //!< The words, for a text task
		QImage inputImage;        //!< The picture, for a vision task
		/*! \brief Task-specific extras.

		    Known keys: "targetLang" (translate), "count" (headline),
		    "length" (summarize), "language" (alt text). Anything a service
		    does not recognise is ignored rather than refused. */
		QVariantMap parameters;
	};

	struct Response
	{
		//! One entry for a single answer, several for a task that was asked
		//! for options. Never empty on success.
		QStringList results;
		QString explanation;      //!< Anything the model said around the answer
		int tokensUsed {0};       //!< Input + output, as the service reported it
		double estimatedCost {0.0};  //!< USD, from the service's own token counts
		QString modelDisplayName; //!< What to tell the user did the work
	};

	explicit AITextService(QObject* parent = nullptr);
	~AITextService() override;

	//! Short human-readable name of the service, for messages.
	virtual QString name() const = 0;
	//! Which of the Task* constants this service will accept.
	virtual QStringList supportedTasks() const = 0;
	//! Whether the configured model can be shown a picture.
	virtual bool supportsVision() const = 0;

	/*! \brief Ask the question. Answers with completed() or failed().

	    A request whose task needs a picture and has none, or whose model
	    cannot see, fails rather than quietly sending the prompt on its own. */
	virtual void execute(const Request& req) = 0;

	/*! \brief Abandon whatever is in flight.

	    Must actually abort the request rather than ignore its answer: the
	    point of Cancel is to stop the meter running. */
	virtual void cancel() = 0;

	//! Ask whether the key works. Answers with connectionTested().
	virtual void testConnection() = 0;

	/*! \brief What failed() carries when the failure was the caller's own
	    cancel() rather than anything going wrong. */
	static QString cancelledMarker();
	static bool isCancelled(const QString& error);

	/*! \brief Marker prefix on a failed() message that was the model declining
	    rather than anything breaking.

	    Shown differently: a refusal is not a fault, it is not retried, and it
	    is worth suggesting another model rather than implying Scribus went
	    wrong. */
	static QString refusalMarker();
	static bool isRefusal(const QString& error);
	static QString strippedRefusal(const QString& error);

signals:
	void completed(const AITextService::Response& result);
	void failed(const QString& error);
	//! A line for the progress toast, e.g. what is being waited on.
	void progressUpdate(const QString& status);
	void connectionTested(bool ok, const QString& detail);
};

Q_DECLARE_METATYPE(AITextService::Response)

#endif
