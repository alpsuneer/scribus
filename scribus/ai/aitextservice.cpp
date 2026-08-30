/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aitextservice.h"

const char* const AITextService::TaskCaption   = "caption";
const char* const AITextService::TaskHeadline  = "headline";
const char* const AITextService::TaskSummarize = "summarize";
const char* const AITextService::TaskTranslate = "translate";
const char* const AITextService::TaskImprove   = "improve";
const char* const AITextService::TaskAltText   = "alttext";

AITextService::AITextService(QObject* parent)
	: QObject(parent)
{
	// Response crosses a thread boundary as a queued signal argument, so Qt has
	// to be told how to copy it. Registering here means no caller has to
	// remember to.
	static const int once = qRegisterMetaType<AITextService::Response>();
	Q_UNUSED(once)
}

// Out of line on purpose: it gives the class one translation unit to anchor its
// vtable in, rather than emitting a copy in everything that includes the header.
AITextService::~AITextService() = default;

QStringList AITextService::allTasks()
{
	return QStringList {
		QLatin1String(TaskCaption),
		QLatin1String(TaskHeadline),
		QLatin1String(TaskSummarize),
		QLatin1String(TaskTranslate),
		QLatin1String(TaskImprove),
		QLatin1String(TaskAltText)
	};
}

bool AITextService::taskNeedsImage(const QString& task)
{
	return task == QLatin1String(TaskCaption) || task == QLatin1String(TaskAltText);
}

QString AITextService::cancelledMarker()
{
	// Deliberately not translated and deliberately not printable: it is a token
	// compared against, never something for a user to read.
	return QStringLiteral("__scribus_aitext_cancelled__");
}

bool AITextService::isCancelled(const QString& error)
{
	return error == cancelledMarker();
}

QString AITextService::refusalMarker()
{
	return QStringLiteral("__scribus_aitext_refused__");
}

bool AITextService::isRefusal(const QString& error)
{
	return error.startsWith(refusalMarker());
}

QString AITextService::strippedRefusal(const QString& error)
{
	return isRefusal(error) ? error.mid(refusalMarker().size()) : error;
}
