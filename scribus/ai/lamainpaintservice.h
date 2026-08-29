/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef LAMAINPAINTSERVICE_H
#define LAMAINPAINTSERVICE_H

#include <QString>
#include <QThread>

#include "ai/aiinpaintservice.h"
#include "scribusapi.h"

class LamaInpaintWorker;

/*!
 \brief Inpainting through a local IOPaint server running the LaMa model.

 IOPaint is a separate program the user installs and runs:

 \verbatim
 pip install iopaint
 iopaint start --model=lama --port=8080
 \endverbatim

 Nothing here installs, downloads or starts it. If it is not running, that is
 reported and the feature stays switched off; Scribus has its own inpainting
 for the ordinary case and does not need this one to exist.

 \section wire The wire format

 Established by asking a running instance rather than by reading anything, and
 worth writing down because it is not what the documentation of neighbouring
 projects would lead you to expect - in particular it is **not** multipart:

 - <tt>POST {url}/api/v1/inpaint</tt>, <tt>Content-Type: application/json</tt>,
   body <tt>{"image": "<base64 PNG>", "mask": "<base64 PNG>"}</tt>. A
   <tt>data:image/png;base64,</tt> prefix is tolerated but not needed.
 - White in the mask means "regenerate this", which is the same sense the
   built-in kernel uses, so the mask needs no inversion.
 - Success is <tt>200</tt> with <tt>Content-Type: image/png</tt> and the whole
   repaired picture as raw PNG bytes, the same size as the one sent. Pixels
   outside the mask come back untouched.
 - Failure is <tt>500</tt> with a JSON body carrying the real reason in
   <tt>errors</tt> (with <tt>detail</tt> and <tt>error</tt> alongside it).

 \section threading Threading

 The socket, the PNG encoding and the base64 both ways all happen on a private
 thread. Encoding a 2048-pixel picture is a few hundred milliseconds on its
 own, which is plainly not something to do between two paint events. The public
 methods below are safe to call from the GUI thread and return immediately; the
 signals arrive back on the caller's thread.
 */
class SCRIBUS_API LamaInpaintService : public AIInpaintService
{
	Q_OBJECT

public:
	/*! \param baseUrl root of the IOPaint server, e.g. http://localhost:8080
	    \param timeoutSeconds how long to wait before giving up on a reply */
	explicit LamaInpaintService(const QString& baseUrl, int timeoutSeconds, QObject* parent = nullptr);
	~LamaInpaintService() override;

	QString name() const override;
	void testConnection() override;
	void inpaint(const QImage& image, const QImage& mask) override;
	void cancel() override;

	//! Change where and how long, without tearing the service down.
	void setEndpoint(const QString& baseUrl, int timeoutSeconds);

	QString baseUrl() const { return m_baseUrl; }

	//! Trim a user-typed URL into the form the requests are built from:
	//! whitespace off, trailing slashes off, and a scheme if none was given.
	static QString normaliseBaseUrl(const QString& url);

private:
	QThread m_thread;
	//! Lives on m_thread and owns the network access manager; never touched
	//! directly from here, only through queued invocations.
	LamaInpaintWorker* m_worker {nullptr};
	QString m_baseUrl;
	int m_timeoutSeconds {120};
};

#endif
