/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#include "suneerxmlclean.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>

namespace SuneerXmlClean
{

static QHash<QString, QList<Hit>>& reports()
{
	static QHash<QString, QList<Hit>> r;
	return r;
}

bool isInvalid(uint code)
{
	if (code < 0x20)
		return code != 0x09 && code != 0x0A && code != 0x0D;
	return code == 0xFFFE || code == 0xFFFF;
}

QString stripInvalid(const QString& text)
{
	QString out;
	for (int i = 0; i < text.size(); ++i)
		if (isInvalid(text.at(i).unicode()))
		{
			if (out.isNull())
				out = text.left(i);
		}
		else if (!out.isNull())
			out += text.at(i);
	return out.isNull() ? text : out;
}

// the text of a CH= / Chars= attribute around byte i of the file: what stands before the character in
// its own run when that says anything, else the run before it (the headline above a dateline), within one story
static QString storyAt(const QByteArray& xml, int at)
{
	auto decode = [](QByteArray raw) {
		for (char c : QByteArrayLiteral("\t\r\n"))
			raw.replace(c, ' ');
		QString s;
		for (const QChar c : QString::fromUtf8(raw))
			if (c.unicode() != 0xAD && !isInvalid(c.unicode()))
				s += c;
		s.replace(QLatin1String("&amp;"), QLatin1String("&")).replace(QLatin1String("&quot;"), QLatin1String("\""))
		 .replace(QLatin1String("&lt;"), QLatin1String("<")).replace(QLatin1String("&gt;"), QLatin1String(">"));
		return s.trimmed();
	};
	const int storyStart = qMax(xml.lastIndexOf("<StoryText", at), xml.lastIndexOf("<PageObject", at));
	int ch = qMax(xml.lastIndexOf(" CH=\"", at), xml.lastIndexOf(" Chars=\"", at));
	QString text;
	if (ch > storyStart)
	{
		const int open = xml.indexOf('"', ch) + 1;
		text = decode(xml.mid(open, at - open));
		while (text.size() < 8 && ch > storyStart)
		{
			const int prev = qMax(xml.lastIndexOf(" CH=\"", ch - 1), xml.lastIndexOf(" Chars=\"", ch - 1));
			if (prev <= storyStart)
				break;
			ch = prev;
			const int from = xml.indexOf('"', ch) + 1;
			const QString run = decode(xml.mid(from, xml.indexOf('"', from) - from));
			if (!run.isEmpty())
				text = run;
		}
	}
	return text.left(40);
}

QList<Hit> clean(QByteArray& xml)
{
	struct Cut { int pos, length; uint code; };
	QList<Cut> cuts;
	const uchar* p = reinterpret_cast<const uchar*>(xml.constData());
	const int n = xml.size();
	for (int i = 0; i < n; ++i)
	{
		const uchar c = p[i];
		if (c < 0x20)
		{
			if (isInvalid(c))
				cuts.append({ i, 1, c });
		}
		else if (c == 0xEF && i + 2 < n && p[i + 1] == 0xBF && (p[i + 2] == 0xBE || p[i + 2] == 0xBF))
		{
			cuts.append({ i, 3, p[i + 2] == 0xBE ? 0xFFFEu : 0xFFFFu });
			i += 2;
		}
		else if (c == '&' && i + 3 < n && p[i + 1] == '#')
		{
			const int end = xml.indexOf(';', i);
			if (end < 0 || end - i > 12)
				continue;
			QByteArray num = xml.mid(i + 2, end - i - 2);
			const bool hex = num.startsWith('x') || num.startsWith('X');
			bool ok = false;
			const uint code = (hex ? num.mid(1) : num).toUInt(&ok, hex ? 16 : 10);
			if (ok && isInvalid(code))
			{
				cuts.append({ i, end - i + 1, code });
				i = end;
			}
		}
	}
	QList<Hit> hits;
	if (cuts.isEmpty())
		return hits;
	int line = 1, lineStart = 0, scanned = 0;
	for (const Cut& c : std::as_const(cuts))
	{
		for (; scanned < c.pos; ++scanned)
			if (p[scanned] == '\n')
			{
				++line;
				lineStart = scanned + 1;
			}
		Hit h;
		h.line = line;
		h.column = QString::fromUtf8(xml.mid(lineStart, c.pos - lineStart)).size() + 1;
		h.code = c.code;
		h.story = storyAt(xml, c.pos);
		hits.append(h);
	}
	QByteArray out;
	out.reserve(n);
	int from = 0;
	for (const Cut& c : std::as_const(cuts))
	{
		out.append(xml.constData() + from, c.pos - from);
		from = c.pos + c.length;
	}
	out.append(xml.constData() + from, n - from);
	xml = out;
	return hits;
}

QString message(const QList<Hit>& hits, bool askToSave)
{
	if (hits.isEmpty())
		return QString();
	const Hit& h = hits.first();
	QString where = h.story.isEmpty()
		? QCoreApplication::translate("SuneerXmlClean", "line %1").arg(h.line)
		: QCoreApplication::translate("SuneerXmlClean", "story '%1…', line %2").arg(h.story).arg(h.line);
	if (hits.size() > 1)
		where += QCoreApplication::translate("SuneerXmlClean", " and %1 more").arg(hits.size() - 1);
	QString text = (hits.size() == 1 ? QCoreApplication::translate("SuneerXmlClean", "Removed 1 invalid character (%1)").arg(where)
	                                 : QCoreApplication::translate("SuneerXmlClean", "Removed %1 invalid characters (%2)").arg(hits.size()).arg(where));
	if (askToSave)
		text += QCoreApplication::translate("SuneerXmlClean", " – please save the page");
	return text;
}

QIODevice* cleanReader(QIODevice* in, const QString& fileName)
{
	if (!in)
		return nullptr;
	QByteArray data = in->readAll();
	delete in;
	const QList<Hit> hits = clean(data);
	if (!hits.isEmpty())
	{
		reports().insert(QFileInfo(fileName).absoluteFilePath(), hits);
		qWarning().noquote() << "[SLA]" << fileName << message(hits, false);
	}
	auto* buffer = new QBuffer();
	buffer->setData(data);
	buffer->open(QIODevice::ReadOnly);
	return buffer;
}

QList<Hit> takeReport(const QString& fileName)
{
	return reports().take(QFileInfo(fileName).absoluteFilePath());
}

} // namespace SuneerXmlClean
