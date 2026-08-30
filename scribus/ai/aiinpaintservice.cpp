/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aiinpaintservice.h"

AIInpaintService::AIInpaintService(QObject* parent)
	: QObject(parent)
{
}

// Out of line on purpose: it gives the class one translation unit to anchor its
// vtable in, rather than emitting a copy in everything that includes the header.
AIInpaintService::~AIInpaintService() = default;

QString AIInpaintService::cancelledMarker()
{
	// Deliberately not translated and deliberately not printable: it is a
	// token compared against, never something for a user to read.
	return QStringLiteral("__scribus_ai_cancelled__");
}

bool AIInpaintService::isCancelled(const QString& error)
{
	return error == cancelledMarker();
}

QString AIInpaintService::refusalMarker()
{
	// Not translated and not printable: a token compared against, never
	// something for a user to read. Stripped before the text is shown.
	return QStringLiteral("__scribus_ai_refused__");
}

bool AIInpaintService::isRefusal(const QString& error)
{
	return error.startsWith(refusalMarker());
}

QString AIInpaintService::strippedRefusal(const QString& error)
{
	return isRefusal(error) ? error.mid(refusalMarker().size()) : error;
}
