/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ai/claudetextservice.h"

#include "ai/aitextservice.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace
{
	const QLatin1String MessagesPath("/v1/messages");
	//! Listing models is a plain GET that runs nothing and so costs nothing.
	const QLatin1String ModelsPath("/v1/models");

	//! Required on every request. Not a nicety: without it the API refuses.
	const char* const VersionHeader = "anthropic-version";
	const char* const VersionValue = "2023-06-01";
	const char* const KeyHeader = "x-api-key";

	QString tr_(const char* text)
	{
		return QCoreApplication::translate("ClaudeTextService", text);
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

	/*! \brief Whether a failure is really "this account cannot pay".

	    Anthropic reports an exhausted balance as a 400 invalid_request_error
	    whose message names the credit balance, not as its own status code, so
	    the number alone would send the user to the wrong place. */
	bool isBilling(const QByteArray& body)
	{
		const QString message = errorOf(body).value(QLatin1String("message")).toString();
		return message.contains(QLatin1String("credit balance"), Qt::CaseInsensitive)
		    || message.contains(QLatin1String("billing"), Qt::CaseInsensitive)
		    || message.contains(QLatin1String("insufficient"), Qt::CaseInsensitive);
	}

	class ClaudeProtocol : public AITextProtocol
	{
	public:
		QString providerName() const override { return QStringLiteral("Claude"); }
		QStringList supportedTasks() const override { return AITextService::allTasks(); }
		//! Every model in the curated list below can be shown a picture.
		bool supportsVision() const override { return true; }

		void applyHeaders(QNetworkRequest& request, const QString& apiKey) const override
		{
			request.setRawHeader(KeyHeader, apiKey.toUtf8());
			request.setRawHeader(VersionHeader, VersionValue);
		}

		/*! \brief Build a Messages request.

		    Schema verified 30 Aug 2026 against
		    https://platform.claude.com/docs/en/api/messages/create and
		    https://platform.claude.com/docs/en/build-with-claude/vision. */
		Prepared buildExecute(const QString& apiBase, const QString& model,
		                      const QString& prompt, const QByteArray& jpegBase64,
		                      int maxTokens) const override
		{
			QJsonArray content;
			if (!jpegBase64.isEmpty())
			{
				// Image first: Anthropic's guidance is that Claude works best
				// that way round.
				QJsonObject source;
				source.insert(QStringLiteral("type"), QStringLiteral("base64"));
				source.insert(QStringLiteral("media_type"), QStringLiteral("image/jpeg"));
				source.insert(QStringLiteral("data"), QString::fromLatin1(jpegBase64));
				QJsonObject image;
				image.insert(QStringLiteral("type"), QStringLiteral("image"));
				image.insert(QStringLiteral("source"), source);
				content.append(image);
			}
			QJsonObject text;
			text.insert(QStringLiteral("type"), QStringLiteral("text"));
			text.insert(QStringLiteral("text"), prompt);
			content.append(text);

			QJsonObject message;
			message.insert(QStringLiteral("role"), QStringLiteral("user"));
			message.insert(QStringLiteral("content"), content);
			QJsonArray messages;
			messages.append(message);

			QJsonObject body;
			body.insert(QStringLiteral("model"), model);
			body.insert(QStringLiteral("max_tokens"), maxTokens);
			body.insert(QStringLiteral("messages"), messages);

			Prepared out;
			out.url = QUrl(apiBase + MessagesPath);
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
			// A safety decline arrives as a 200 with stop_reason "refusal", so
			// a successful HTTP call is not the same as an answer.
			const QString stop = root.value(QLatin1String("stop_reason")).toString();
			if (stop == QLatin1String("refusal"))
			{
				const QJsonObject details = root.value(QLatin1String("stop_details")).toObject();
				const QString explanation = details.value(QLatin1String("explanation")).toString().trimmed();
				const QString category = details.value(QLatin1String("category")).toString().trimmed();
				refusal = !explanation.isEmpty() ? explanation
				        : (!category.isEmpty() ? category : tr_("the request was declined"));
				return true;
			}

			QStringList parts;
			const QJsonArray content = root.value(QLatin1String("content")).toArray();
			for (const QJsonValue& value : content)
			{
				const QJsonObject block = value.toObject();
				if (block.value(QLatin1String("type")).toString() != QLatin1String("text"))
					continue;
				const QString chunk = block.value(QLatin1String("text")).toString();
				if (!chunk.trimmed().isEmpty())
					parts << chunk;
			}
			if (parts.isEmpty())
				return false;
			text = parts.join(QLatin1String("\n"));
			return true;
		}

		void parseUsage(const QJsonObject& root, int& inputTokens, int& outputTokens) const override
		{
			const QJsonObject usage = root.value(QLatin1String("usage")).toObject();
			inputTokens = usage.value(QLatin1String("input_tokens")).toInt();
			outputTokens = usage.value(QLatin1String("output_tokens")).toInt();
		}

		QString describeStatus(int status, const QByteArray& body) const override
		{
			const QString detail = messageOf(body);
			// Billing before the status number: an exhausted balance is a 400
			// here, and "bad request" would send the user hunting in the wrong
			// place for something they only need to top up.
			if (isBilling(body))
				return tr_("Your Anthropic account cannot pay for this request. "
				           "Check billing and credit at console.anthropic.com.");
			switch (status)
			{
			case 401:
				return tr_("Invalid Claude API key. Check it in Preferences > AI Services.");
			case 403:
				return tr_("Claude refused this key's permissions. Check the key's scope at "
				           "console.anthropic.com.");
			case 429:
				return tr_("Claude is rate limiting this key - try again in a moment.");
			default:
				break;
			}
			if (status == 529)
				return tr_("Claude is overloaded right now - try again in a moment.");
			if (status >= 500)
				return QCoreApplication::translate("ClaudeTextService",
				       "Claude is temporarily unavailable (%1) - try again.").arg(status);
			if (!detail.isEmpty())
				return QCoreApplication::translate("ClaudeTextService",
				       "Claude answered %1: %2").arg(status).arg(detail);
			return QCoreApplication::translate("ClaudeTextService", "Claude answered %1.").arg(status);
		}

		QString describeTestSuccess(const QJsonObject& root) const override
		{
			const int count = root.value(QLatin1String("data")).toArray().size();
			if (count > 0)
				return QCoreApplication::translate("ClaudeTextService",
				       "Key accepted. %1 models available.").arg(count);
			return tr_("Key accepted.");
		}

		const QList<ModelChoice>& models() const override { return ClaudeTextService::models(); }
	};
}

QString ClaudeTextService::defaultApiBase()
{
	return QStringLiteral("https://api.anthropic.com");
}

const QList<AITextProtocol::ModelChoice>& ClaudeTextService::models()
{
	/* Curated list of Aug 2026. Model ids and per-million-token prices checked
	   on 30 Aug 2026 against the current model table; see
	   https://platform.claude.com/docs/en/about-claude/models/overview and
	   https://claude.com/pricing.

	   Note for anyone updating: the ids are complete as written and take no
	   date suffix. Earlier drafts of this feature specified
	   "claude-sonnet-4-5" and "claude-opus-4-5"; those are superseded by the 5
	   family below. All three accept images. */
	static const QList<AITextProtocol::ModelChoice> list = {
		{ QStringLiteral("claude-sonnet-5"),
		  QObject::tr("Claude Sonnet 5"),
		  QObject::tr("Recommended. Balanced quality and cost."),
		  2.0, 10.0 },
		{ QStringLiteral("claude-opus-5"),
		  QObject::tr("Claude Opus 5"),
		  QObject::tr("Highest quality, and the most expensive."),
		  5.0, 25.0 },
		{ QStringLiteral("claude-haiku-4-5"),
		  QObject::tr("Claude Haiku 4.5"),
		  QObject::tr("Cheapest and fastest. Good for bulk work."),
		  1.0, 5.0 },
	};
	return list;
}

QString ClaudeTextService::defaultModel()
{
	return QStringLiteral("claude-sonnet-5");
}

QString ClaudeTextService::displayNameFor(const QString& modelId)
{
	const QList<AITextProtocol::ModelChoice>& list = models();
	for (const AITextProtocol::ModelChoice& choice : list)
	{
		if (choice.id == modelId)
			return choice.displayName;
	}
	return modelId;
}

ClaudeTextService::ClaudeTextService(const QString& apiKey, const QString& model,
                                     int timeoutSeconds, QObject* parent,
                                     const QString& apiBase)
	: AITextHttpService(std::make_shared<ClaudeProtocol>(), apiKey, model,
	                    timeoutSeconds, parent, apiBase)
{
}
