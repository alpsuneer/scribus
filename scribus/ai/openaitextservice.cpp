/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/openaitextservice.h"

#include "ai/aitextservice.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace
{
	const QLatin1String CompletionsPath("/v1/chat/completions");
	//! Listing models is a plain GET that runs nothing and so costs nothing.
	const QLatin1String ModelsPath("/v1/models");

	QString tr_(const char* text)
	{
		return QCoreApplication::translate("OpenAITextService", text);
	}

	QJsonObject errorOf(const QByteArray& body)
	{
		const QJsonDocument doc = QJsonDocument::fromJson(body);
		return doc.isObject() ? doc.object().value(QLatin1String("error")).toObject()
		                      : QJsonObject();
	}

	QString messageOf(const QByteArray& body)
	{
		const QString message = errorOf(body).value(QLatin1String("message")).toString().trimmed();
		if (!message.isEmpty())
			return message;
		return QString::fromUtf8(body.left(300)).trimmed();
	}

	//! OpenAI's machine-readable code, e.g. "insufficient_quota".
	QString codeOf(const QByteArray& body)
	{
		return errorOf(body).value(QLatin1String("code")).toString().trimmed();
	}

	/*! \brief Whether a failure is really "this account cannot pay".

	    OpenAI reports an exhausted balance as a 429 with code
	    insufficient_quota - the same status as an ordinary rate limit, which
	    means "try again in a moment" would be advice that never comes true. */
	bool isBilling(const QByteArray& body)
	{
		if (codeOf(body) == QLatin1String("insufficient_quota"))
			return true;
		const QString message = errorOf(body).value(QLatin1String("message")).toString();
		return message.contains(QLatin1String("quota"), Qt::CaseInsensitive)
		    || message.contains(QLatin1String("billing"), Qt::CaseInsensitive);
	}

	class OpenAIProtocol : public AITextProtocol
	{
	public:
		QString providerName() const override { return QStringLiteral("OpenAI"); }
		QStringList supportedTasks() const override { return AITextService::allTasks(); }
		//! Every model in the curated list below accepts an image.
		bool supportsVision() const override { return true; }

		void applyHeaders(QNetworkRequest& request, const QString& apiKey) const override
		{
			request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
		}

		/*! \brief Build a Chat Completions request.

		    Schema verified 30 Aug 2026 against
		    https://developers.openai.com/api/docs/guides/images-vision. Note
		    max_completion_tokens rather than max_tokens, and the picture as a
		    complete data: URL inside image_url. */
		Prepared buildExecute(const QString& apiBase, const QString& model,
		                      const QString& prompt, const QByteArray& jpegBase64,
		                      int maxTokens) const override
		{
			QJsonArray content;
			QJsonObject text;
			text.insert(QStringLiteral("type"), QStringLiteral("text"));
			text.insert(QStringLiteral("text"), prompt);
			content.append(text);
			if (!jpegBase64.isEmpty())
			{
				QJsonObject url;
				url.insert(QStringLiteral("url"),
				           QLatin1String("data:image/jpeg;base64,") + QString::fromLatin1(jpegBase64));
				QJsonObject image;
				image.insert(QStringLiteral("type"), QStringLiteral("image_url"));
				image.insert(QStringLiteral("image_url"), url);
				content.append(image);
			}

			QJsonObject message;
			message.insert(QStringLiteral("role"), QStringLiteral("user"));
			message.insert(QStringLiteral("content"), content);
			QJsonArray messages;
			messages.append(message);

			QJsonObject body;
			body.insert(QStringLiteral("model"), model);
			body.insert(QStringLiteral("messages"), messages);
			body.insert(QStringLiteral("max_completion_tokens"), maxTokens);

			Prepared out;
			out.url = QUrl(apiBase + CompletionsPath);
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
			const QJsonArray choices = root.value(QLatin1String("choices")).toArray();
			if (choices.isEmpty())
				return false;
			const QJsonObject first = choices.at(0).toObject();
			const QJsonObject message = first.value(QLatin1String("message")).toObject();

			// A safety decline arrives as a 200 with the refusal in its own
			// field and content null, so a successful call is not an answer.
			const QString declined = message.value(QLatin1String("refusal")).toString().trimmed();
			if (!declined.isEmpty())
			{
				refusal = declined;
				return true;
			}
			const QString content = message.value(QLatin1String("content")).toString();
			if (content.trimmed().isEmpty())
				return false;
			text = content;
			return true;
		}

		void parseUsage(const QJsonObject& root, int& inputTokens, int& outputTokens) const override
		{
			const QJsonObject usage = root.value(QLatin1String("usage")).toObject();
			inputTokens = usage.value(QLatin1String("prompt_tokens")).toInt();
			outputTokens = usage.value(QLatin1String("completion_tokens")).toInt();
		}

		QString describeStatus(int status, const QByteArray& body) const override
		{
			const QString detail = messageOf(body);
			// Billing before the number: an exhausted quota is also a 429, and
			// telling the user to wait a moment would be advice that never
			// comes true.
			if (isBilling(body))
				return tr_("Your OpenAI account is out of quota. Check billing at "
				           "platform.openai.com.");
			switch (status)
			{
			case 401:
				return tr_("Invalid OpenAI API key. Check it in Preferences > AI Services.");
			case 403:
				return tr_("OpenAI refused this key's permissions. Check the key at "
				           "platform.openai.com.");
			case 429:
				return tr_("OpenAI is rate limiting this key - try again in a moment.");
			default:
				break;
			}
			if (status >= 500)
				return QCoreApplication::translate("OpenAITextService",
				       "OpenAI is temporarily unavailable (%1) - try again.").arg(status);
			if (!detail.isEmpty())
				return QCoreApplication::translate("OpenAITextService",
				       "OpenAI answered %1: %2").arg(status).arg(detail);
			return QCoreApplication::translate("OpenAITextService", "OpenAI answered %1.").arg(status);
		}

		QString describeTestSuccess(const QJsonObject& root) const override
		{
			const int count = root.value(QLatin1String("data")).toArray().size();
			if (count > 0)
				return QCoreApplication::translate("OpenAITextService",
				       "Key accepted. %1 models available.").arg(count);
			return tr_("Key accepted.");
		}

		const QList<ModelChoice>& models() const override { return OpenAITextService::models(); }
	};
}

QString OpenAITextService::defaultApiBase()
{
	return QStringLiteral("https://api.openai.com");
}

const QList<AITextProtocol::ModelChoice>& OpenAITextService::models()
{
	/* Curated list of Aug 2026, checked against
	   https://developers.openai.com/api/docs/models on 30 Aug 2026. All three
	   accept images.

	   Note for anyone updating: the ids this feature was specified against
	   ("gpt-5", "gpt-5-mini", "gpt-4o") are not in OpenAI's current list; the
	   5.6 family replaced them.

	   Prices are left at zero deliberately: OpenAI's per-token rates were not
	   verified while writing this, and a made-up number on a cost line is worse
	   than no number. The dialog omits the estimate when it is zero. */
	static const QList<AITextProtocol::ModelChoice> list = {
		{ QStringLiteral("gpt-5.6-terra"),
		  QObject::tr("GPT-5.6 Terra"),
		  QObject::tr("Recommended. Balanced quality and cost."),
		  0.0, 0.0 },
		{ QStringLiteral("gpt-5.6-sol"),
		  QObject::tr("GPT-5.6 Sol"),
		  QObject::tr("Flagship, for complex professional work."),
		  0.0, 0.0 },
		{ QStringLiteral("gpt-5.6-luna"),
		  QObject::tr("GPT-5.6 Luna"),
		  QObject::tr("Cheapest. For high-volume work."),
		  0.0, 0.0 },
	};
	return list;
}

QString OpenAITextService::defaultModel()
{
	return QStringLiteral("gpt-5.6-terra");
}

QString OpenAITextService::displayNameFor(const QString& modelId)
{
	const QList<AITextProtocol::ModelChoice>& list = models();
	for (const AITextProtocol::ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return choice.displayName;
	}
	return modelId;
}

OpenAITextService::OpenAITextService(const QString& apiKey, const QString& model,
                                     int timeoutSeconds, QObject* parent,
                                     const QString& apiBase)
	: AITextHttpService(std::make_shared<OpenAIProtocol>(), apiKey, model,
	                    timeoutSeconds, parent, apiBase)
{
}
