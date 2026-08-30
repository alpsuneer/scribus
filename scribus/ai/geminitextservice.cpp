/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/geminitextservice.h"

#include "ai/aitextservice.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace
{
	const QLatin1String InteractionsPath("/interactions");
	//! Listing models is a plain GET that runs nothing and so costs nothing.
	const QLatin1String ModelsPath("/models");
	const char* const KeyHeader = "x-goog-api-key";

	QString tr_(const char* text)
	{
		return QCoreApplication::translate("GeminiTextService", text);
	}

	/*! \brief The body as an object, unwrapping the array Google puts around it
	    on some endpoints.

	    Measured against the live API on 30 Aug 2026 while building the image
	    client: a bad key on <tt>GET /v1beta/models</tt> answers with a bare
	    object, and the same key on <tt>POST /v1beta/interactions</tt> answers
	    with the identical envelope inside a one-element array. Reading only one
	    shape throws the message away on whichever endpoint was not tested. */
	QJsonObject rootObjectOf(const QByteArray& body)
	{
		if (body.isEmpty())
			return QJsonObject();
		const QJsonDocument doc = QJsonDocument::fromJson(body);
		if (doc.isObject())
			return doc.object();
		if (doc.isArray())
		{
			const QJsonArray array = doc.array();
			for (const QJsonValue& value : array)
			{
				if (value.isObject())
					return value.toObject();
			}
		}
		return QJsonObject();
	}

	QJsonObject errorOf(const QByteArray& body)
	{
		return rootObjectOf(body).value(QLatin1String("error")).toObject();
	}

	QString messageOf(const QByteArray& body)
	{
		const QString message = errorOf(body).value(QLatin1String("message")).toString().trimmed();
		if (!message.isEmpty())
			return message;
		return QString::fromUtf8(body.left(300)).trimmed();
	}

	QString errorStatusOf(const QByteArray& body)
	{
		return errorOf(body).value(QLatin1String("status")).toString().trimmed();
	}

	QString errorReasonOf(const QByteArray& body)
	{
		const QJsonArray details = errorOf(body).value(QLatin1String("details")).toArray();
		for (const QJsonValue& value : details)
		{
			const QString reason = value.toObject().value(QLatin1String("reason")).toString().trimmed();
			if (!reason.isEmpty())
				return reason;
		}
		return QString();
	}

	/*! \brief Whether a failure is "this key is no good", however it was dressed.

	    Measured against the live API: an invalid key is HTTP 400 with status
	    INVALID_ARGUMENT and a detail reason of API_KEY_INVALID, not the 401
	    anyone would expect. Keying off the number would report the commonest
	    mistake a user can make as an unexplained bad request. */
	bool isBadKey(const QByteArray& body)
	{
		if (errorReasonOf(body) == QLatin1String("API_KEY_INVALID"))
			return true;
		const QString message = errorOf(body).value(QLatin1String("message")).toString();
		return message.contains(QLatin1String("API key not valid"), Qt::CaseInsensitive)
		    || message.contains(QLatin1String("API key is invalid"), Qt::CaseInsensitive);
	}

	class GeminiProtocol : public AITextProtocol
	{
	public:
		QString providerName() const override { return QStringLiteral("Gemini"); }
		QStringList supportedTasks() const override { return AITextService::allTasks(); }
		//! Every Gemini model in the curated list accepts an image.
		bool supportsVision() const override { return true; }

		void applyHeaders(QNetworkRequest& request, const QString& apiKey) const override
		{
			request.setRawHeader(KeyHeader, apiKey.toUtf8());
		}

		/*! \brief Build an Interactions request.

		    Schema verified 30 Aug 2026 against
		    https://ai.google.dev/gemini-api/docs/text-generation and
		    https://ai.google.dev/api/interactions-api. Note the flat input[] of
		    typed parts and snake_case mime_type: this is the Interactions API,
		    not the legacy contents/parts/inline_data shape. */
		Prepared buildExecute(const QString& apiBase, const QString& model,
		                      const QString& prompt, const QByteArray& jpegBase64,
		                      int /*maxTokens*/) const override
		{
			QJsonObject text;
			text.insert(QStringLiteral("type"), QStringLiteral("text"));
			text.insert(QStringLiteral("text"), prompt);

			QJsonArray input;
			input.append(text);
			if (!jpegBase64.isEmpty())
			{
				QJsonObject image;
				image.insert(QStringLiteral("type"), QStringLiteral("image"));
				image.insert(QStringLiteral("mime_type"), QStringLiteral("image/jpeg"));
				image.insert(QStringLiteral("data"), QString::fromLatin1(jpegBase64));
				input.append(image);
			}

			QJsonObject body;
			body.insert(QStringLiteral("model"), model);
			body.insert(QStringLiteral("input"), input);

			Prepared out;
			out.url = QUrl(apiBase + InteractionsPath);
			out.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
			out.post = true;
			return out;
		}

		Prepared buildTest(const QString& apiBase, const QString& /*model*/) const override
		{
			Prepared out;
			out.url = QUrl(apiBase + ModelsPath);
			out.post = false;
			return out;
		}

		bool parseAnswer(const QJsonObject& root, QString& text, QString& refusal) const override
		{
			const QString direct = root.value(QLatin1String("output_text")).toString().trimmed();
			if (!direct.isEmpty())
			{
				text = direct;
				return true;
			}

			QStringList parts;
			const QJsonArray steps = root.value(QLatin1String("steps")).toArray();
			for (const QJsonValue& step : steps)
			{
				const QJsonArray content = step.toObject().value(QLatin1String("content")).toArray();
				for (const QJsonValue& value : content)
				{
					const QString chunk = value.toObject().value(QLatin1String("text")).toString();
					if (!chunk.trimmed().isEmpty())
						parts << chunk;
				}
			}
			if (!parts.isEmpty())
			{
				text = parts.join(QLatin1String("\n"));
				return true;
			}

			// A 200 is not always a yes: a safety block arrives this way rather
			// than as an HTTP error, and the reason is the only useful thing.
			const QJsonObject feedback = root.value(QLatin1String("promptFeedback")).toObject();
			const QString blocked = feedback.value(QLatin1String("blockReason")).toString().trimmed();
			if (!blocked.isEmpty())
			{
				refusal = blocked;
				return true;
			}
			QStringList reasons;
			const QJsonArray errors = root.value(QLatin1String("errors")).toArray();
			for (const QJsonValue& value : errors)
			{
				const QString message = value.toObject().value(QLatin1String("message")).toString().trimmed();
				if (!message.isEmpty())
					reasons << message;
			}
			if (!reasons.isEmpty())
			{
				refusal = reasons.join(QLatin1String(", "));
				return true;
			}
			return false;
		}

		void parseUsage(const QJsonObject& root, int& inputTokens, int& outputTokens) const override
		{
			const QJsonObject usage = root.value(QLatin1String("usage")).toObject();
			inputTokens = usage.value(QLatin1String("total_input_tokens")).toInt();
			outputTokens = usage.value(QLatin1String("total_output_tokens")).toInt();
		}

		QString describeStatus(int status, const QByteArray& body) const override
		{
			const QString detail = messageOf(body);
			// What Google says went wrong, before what the number implies.
			if (isBadKey(body))
				return tr_("Invalid Gemini API key. Check it in Preferences > AI Services.");
			const QString googleStatus = errorStatusOf(body);
			if (googleStatus == QLatin1String("PERMISSION_DENIED"))
				return tr_("Gemini API access denied. Check that billing is enabled for your key "
				           "at aistudio.google.com.");
			if (googleStatus == QLatin1String("RESOURCE_EXHAUSTED"))
				return tr_("Gemini rate limit - try again in a moment.");

			switch (status)
			{
			case 400:
				if (!detail.isEmpty())
					return QCoreApplication::translate("GeminiTextService",
					       "Gemini rejected the request (400): %1").arg(detail);
				return tr_("Gemini rejected the request (400).");
			case 401:
				return tr_("Invalid Gemini API key. Check it in Preferences > AI Services.");
			case 403:
				return tr_("Gemini API access denied. Check that billing is enabled for your key "
				           "at aistudio.google.com.");
			case 429:
				return tr_("Gemini rate limit - try again in a moment.");
			default:
				break;
			}
			if (status >= 500)
				return QCoreApplication::translate("GeminiTextService",
				       "The Google API is temporarily unavailable (%1) - try again.").arg(status);
			if (!detail.isEmpty())
				return QCoreApplication::translate("GeminiTextService",
				       "Gemini answered %1: %2").arg(status).arg(detail);
			return QCoreApplication::translate("GeminiTextService", "Gemini answered %1.").arg(status);
		}

		QString describeTestSuccess(const QJsonObject& root) const override
		{
			const int count = root.value(QLatin1String("models")).toArray().size();
			if (count > 0)
				return QCoreApplication::translate("GeminiTextService",
				       "Key accepted. %1 models available.").arg(count);
			return tr_("Key accepted.");
		}

		const QList<ModelChoice>& models() const override { return GeminiTextService::models(); }
	};
}

QString GeminiTextService::defaultApiBase()
{
	return QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
}

const QList<AITextProtocol::ModelChoice>& GeminiTextService::models()
{
	/* Curated list of Aug 2026, checked against
	   https://ai.google.dev/gemini-api/docs/models on 30 Aug 2026.

	   Note for anyone updating: the ids this feature was specified against
	   ("gemini-2.0-flash", "gemini-2.0-flash-lite") are no longer in Google's
	   list; the 3.x Flash family replaced them. These are also *not* the image
	   ids used by GeminiInpaintService - that list is the Nano Banana models,
	   and the two are not interchangeable.

	   Prices are left at zero deliberately: Google's per-token text rates were
	   not verified while writing this, and a made-up number on a cost line is
	   worse than no number. The dialog omits the estimate when it is zero. */
	static const QList<AITextProtocol::ModelChoice> list = {
		{ QStringLiteral("gemini-3.7-flash"),
		  QObject::tr("Gemini 3.7 Flash"),
		  QObject::tr("Recommended. Fast, capable and cheap."),
		  0.0, 0.0 },
		{ QStringLiteral("gemini-2.5-pro"),
		  QObject::tr("Gemini 2.5 Pro"),
		  QObject::tr("Deeper reasoning for difficult text."),
		  0.0, 0.0 },
		{ QStringLiteral("gemini-3.1-flash-lite"),
		  QObject::tr("Gemini 3.1 Flash Lite"),
		  QObject::tr("Cheapest. For working through a lot of text."),
		  0.0, 0.0 },
	};
	return list;
}

QString GeminiTextService::defaultModel()
{
	return QStringLiteral("gemini-3.7-flash");
}

QString GeminiTextService::displayNameFor(const QString& modelId)
{
	const QList<AITextProtocol::ModelChoice>& list = models();
	for (const AITextProtocol::ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return choice.displayName;
	}
	return modelId;
}

GeminiTextService::GeminiTextService(const QString& apiKey, const QString& model,
                                     int timeoutSeconds, QObject* parent,
                                     const QString& apiBase)
	: AITextHttpService(std::make_shared<GeminiProtocol>(), apiKey, model,
	                    timeoutSeconds, parent, apiBase)
{
}
