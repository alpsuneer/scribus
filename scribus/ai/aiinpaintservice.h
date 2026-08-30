/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AIINPAINTSERVICE_H
#define AIINPAINTSERVICE_H

#include <QImage>
#include <QObject>
#include <QString>

#include "scribusapi.h"

/*!
 \brief An inpainting service that lives somewhere other than in this process.

 The Remove Object tool has two ways of filling a hole. The built-in one
 (util_inpaint.h) is a pair of algorithms that run here, need nothing installed
 and are the right answer for most removals. The other is a model, which lives
 behind this interface because it is not part of Scribus: the user installs it,
 runs it, and points a preference at it.

 Everything here is asynchronous and reports through signals, because the
 implementations talk to something over a socket and none of that may happen on
 the GUI thread. Exactly one of inpaintFinished() or inpaintFailed() follows
 each inpaint() call - including when the caller cancels, which arrives as
 inpaintFailed() so that no caller can be left waiting for a signal that never
 comes. A caller that asked to cancel is expected to know it did and to say
 nothing to the user about it.

 Implementations must contact the address configured for them and no other. No
 fallback host, no discovery, no telemetry: a picture leaves this machine only
 if the user typed somewhere for it to go.
 */
class SCRIBUS_API AIInpaintService : public QObject
{
	Q_OBJECT

public:
	explicit AIInpaintService(QObject* parent = nullptr);
	~AIInpaintService() override;

	//! Short human-readable name of the service, for messages and logs.
	virtual QString name() const = 0;

	//! Ask whether the service is there and usable. Answers with
	//! connectionTested().
	virtual void testConnection() = 0;

	/*! \brief Fill the white part of \a mask in \a image.

	    \param image the picture to repair.
	    \param mask 8-bit, non-zero where the pixels are to be regenerated -
	           the same sense the built-in kernel uses. */
	virtual void inpaint(const QImage& image, const QImage& mask) = 0;

	/*! \brief Abandon whatever is in flight.

	    Must actually abort the request rather than ignore its answer: the
	    point of Cancel is to stop the machine working, not to stop looking. */
	virtual void cancel() = 0;

	/*! \brief What inpaintFailed() carries when the failure was the caller's
	    own cancel() rather than anything going wrong.

	    Cancelling still has to produce a signal, or a caller that cancels
	    would wait for one for ever. It must not produce an *error* the user is
	    shown, though, so it is marked rather than described. */
	static QString cancelledMarker();
	//! True when \a error is the cancel marker rather than a real failure.
	static bool isCancelled(const QString& error);

	/*! \brief Marker prefix on an inpaintFailed() message that was the model
	    declining the job rather than anything breaking.

	    The caller shows a refusal differently from a failure - it is worth
	    suggesting another model, and worth not implying Scribus went wrong -
	    so the two have to be distinguishable without matching on prose.

	    Common to every service that asks a model rather than an algorithm: a
	    refusal from Gemini is the same kind of answer as a refusal through
	    OpenRouter, and a caller should not have to ask which one it was
	    talking to before it can tell. */
	static QString refusalMarker();
	//! True when \a error carries refusalMarker(). \a error keeps its text.
	static bool isRefusal(const QString& error);
	//! \a error with the marker taken off, ready to show someone.
	static QString strippedRefusal(const QString& error);

signals:
	void inpaintFinished(const QImage& result);
	void inpaintFailed(const QString& error);
	void connectionTested(bool ok, const QString& detail);
};

#endif
