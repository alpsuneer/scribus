/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTPROMPTS_H
#define AITEXTPROMPTS_H

#include <QString>
#include <QVariantMap>

/*!
 \brief The instructions sent to a text model, and the one place they live.

 These are part of the contract with the model in the same way a request body
 is: change the wording and the same article produces a different headline.
 They are shared across Claude, OpenAI and Gemini on purpose - a user who
 switches provider because of what a card will pay for should get the same kind
 of answer, not a different house style. Scattering them through three services
 would guarantee they drifted.

 Not translated, deliberately. They are addressed to a model, not to a person,
 and these models follow English instructions most reliably; the *output*
 language is controlled by the prompt text and by the parameters, not by which
 build of Scribus is running. A Malayalam UI must not quietly ask a different
 question and get a different answer back.

 \section placeholders Placeholders

 A prompt may contain {{input}}, {{targetLang}}, {{count}}, {{length}} and
 {{language}}. AIInpaintPrompts has no equivalent because an image prompt has
 no user text in it; here the article itself is substituted in, so
 fillTemplate() is the only supported way to build the final string and it is
 what the services call.
 */
namespace AITextPrompts
{
	/*! \brief Picture in, caption out.

	    Two languages because a Malayalam paper lays out a Malayalam caption but
	    files and searches in English, and asking twice costs a second call. */
	constexpr const char* TASK_CAPTION =
		"Look at this image and write a concise, engaging caption suitable for a "
		"newspaper article. Return two versions: one in Malayalam and one in English. "
		"Format: 'Malayalam: <text>\nEnglish: <text>' - no extra commentary.";

	/*! \brief Article in, several headlines out.

	    Deliberately asks for varied tone: a sub-editor wants something to choose
	    between, and three near-identical lines are one option, not three. */
	constexpr const char* TASK_HEADLINE =
		"Read the following article text and suggest {{count}} headline options suitable for "
		"a newspaper. Format each on its own line. Vary the tone: one factual, one "
		"engaging, one dramatic. No numbering.\n\nArticle:\n{{input}}";

	//! Article in, short summary out, in the language it arrived in.
	constexpr const char* TASK_SUMMARIZE =
		"Summarize the following article in {{length}}. Preserve key facts, names, "
		"dates. Language: same as input.\n\nArticle:\n{{input}}";

	//! Text in, translation out. Proper nouns are the thing most often lost.
	constexpr const char* TASK_TRANSLATE =
		"Translate the following text to {{targetLang}}. Preserve tone, formatting, "
		"proper nouns. Return only the translation.\n\nText:\n{{input}}";

	//! Text in, tidied text out, in the same language and meaning.
	constexpr const char* TASK_IMPROVE =
		"Improve the following text for grammar, clarity, and style. Preserve meaning "
		"and language. Return only the improved text.\n\nText:\n{{input}}";

	/*! \brief Picture in, alt text out.

	    "what is visible, not interpretation" is the whole job: alt text that
	    editorialises is worse than none. */
	constexpr const char* TASK_ALTTEXT =
		"Write a concise alt text description of this image for accessibility. "
		"Language: {{language}} (default English). One or two sentences. Focus on "
		"what is visible, not interpretation.";

	//! The prompt for \a task, or an empty string if it is not one of ours.
	QString templateFor(const QString& task);

	/*! \brief Substitute the parameters into a prompt.

	    \param tmpl one of the constants above.
	    \param input the user's text, put in for {{input}}.
	    \param parameters the request's extras; each known key fills its own
	           placeholder, and a missing one gets a sensible default rather
	           than leaving "{{targetLang}}" in the text sent to a model.

	    Substitution is one pass and only into the template, so text that
	    happens to contain "{{input}}" is inserted, never re-scanned. */
	QString fillTemplate(const QString& tmpl, const QString& input,
	                     const QVariantMap& parameters = QVariantMap());

	//! fillTemplate(templateFor(task), ...). Empty for an unknown task.
	QString promptFor(const QString& task, const QString& input,
	                  const QVariantMap& parameters = QVariantMap());

	/*! \brief How a summarize length parameter reads in the prompt.

	    "short"/"medium"/"long" from the dialog become sentence counts, because
	    a model given a word like "short" picks its own meaning each time. */
	QString summaryLengthPhrase(const QString& length);
}

#endif
