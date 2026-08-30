/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/aitextprompts.h"

#include "ai/aitextservice.h"

QString AITextPrompts::templateFor(const QString& task)
{
	if (task == QLatin1String(AITextService::TaskCaption))
		return QString::fromLatin1(TASK_CAPTION);
	if (task == QLatin1String(AITextService::TaskHeadline))
		return QString::fromLatin1(TASK_HEADLINE);
	if (task == QLatin1String(AITextService::TaskSummarize))
		return QString::fromLatin1(TASK_SUMMARIZE);
	if (task == QLatin1String(AITextService::TaskTranslate))
		return QString::fromLatin1(TASK_TRANSLATE);
	if (task == QLatin1String(AITextService::TaskImprove))
		return QString::fromLatin1(TASK_IMPROVE);
	if (task == QLatin1String(AITextService::TaskAltText))
		return QString::fromLatin1(TASK_ALTTEXT);
	return QString();
}

QString AITextPrompts::summaryLengthPhrase(const QString& length)
{
	// Sentence counts rather than adjectives: "short" means whatever the model
	// decides it means today, and a summary that changes length between two
	// runs of the same button looks like a bug.
	if (length == QLatin1String("short"))
		return QStringLiteral("1-2 sentences");
	if (length == QLatin1String("long"))
		return QStringLiteral("5-6 sentences");
	return QStringLiteral("2-3 sentences");
}

QString AITextPrompts::fillTemplate(const QString& tmpl, const QString& input,
                                    const QVariantMap& parameters)
{
	if (tmpl.isEmpty())
		return QString();

	// Defaults matter: a placeholder left unfilled would be sent to the model
	// verbatim, and "Translate to {{targetLang}}" is a question no model can
	// answer usefully.
	const QString targetLang = parameters.value(QStringLiteral("targetLang"))
	                           .toString().trimmed();
	const QString language = parameters.value(QStringLiteral("language"))
	                         .toString().trimmed();
	const QString lengthKey = parameters.value(QStringLiteral("length"))
	                          .toString().trimmed();
	bool countOk = false;
	const int count = parameters.value(QStringLiteral("count")).toInt(&countOk);

	QString out = tmpl;
	out.replace(QLatin1String("{{targetLang}}"),
	            targetLang.isEmpty() ? QStringLiteral("English") : targetLang);
	out.replace(QLatin1String("{{language}}"),
	            language.isEmpty() ? QStringLiteral("English") : language);
	out.replace(QLatin1String("{{length}}"), summaryLengthPhrase(lengthKey));
	out.replace(QLatin1String("{{count}}"),
	            QString::number((countOk && count >= 1 && count <= 5) ? count : 3));

	// {{input}} goes last and is the only one carrying user text. Because each
	// replace() runs over the result of the previous one, filling it earlier
	// would let an article containing "{{targetLang}}" have that rewritten -
	// harmless here, but the kind of thing that turns into a real injection the
	// day a placeholder means something more interesting.
	out.replace(QLatin1String("{{input}}"), input);
	return out;
}

QString AITextPrompts::promptFor(const QString& task, const QString& input,
                                 const QVariantMap& parameters)
{
	return fillTemplate(templateFor(task), input, parameters);
}
