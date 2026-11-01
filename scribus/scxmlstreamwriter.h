/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCXMLSTREAMWRITER_H
#define SCXMLSTREAMWRITER_H

#include <type_traits>

#include <QByteArray>
#include <QString>
#include <QXmlStreamWriter>

#include "pdfversion.h"
#include "scribusapi.h"

class SCRIBUS_API ScXmlStreamWriter : public QXmlStreamWriter
{
public:
	ScXmlStreamWriter(void) : QXmlStreamWriter() {}
	explicit ScXmlStreamWriter(QByteArray* array) : QXmlStreamWriter(array) {}
	explicit ScXmlStreamWriter(QIODevice* device) : QXmlStreamWriter(device) {}
	explicit ScXmlStreamWriter(QString*   string) : QXmlStreamWriter(string) {}

	// suneer: nothing XML 1.0 forbids ever reaches a file (one such character made a page unreadable)
	static bool xmlInvalid(ushort c) { return (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) || c >= 0xFFFE; }
	static QString xmlSafe(const QString& text)
	{
		for (int i = 0; i < text.size(); ++i)
			if (xmlInvalid(text.at(i).unicode()))
			{
				QString out = text.left(i);
				for (int k = i + 1; k < text.size(); ++k)
					if (!xmlInvalid(text.at(k).unicode()))
						out += text.at(k);
				return out;
			}
		return text;
	}
	void writeAttribute(const QString& name, const QString& value) { QXmlStreamWriter::writeAttribute(name, xmlSafe(value)); }
	void writeCharacters(const QString& text) { QXmlStreamWriter::writeCharacters(xmlSafe(text)); }
	void writeAttribute(const QString& name, const PDFVersion& value) { QXmlStreamWriter::writeAttribute(name, QString::number(value)); }

	template<typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true>
	void writeAttribute(const QString & name, T value)    { QXmlStreamWriter::writeAttribute(name, QString::number(value)); }

	template<typename T, std::enable_if_t< std::is_floating_point_v<T>, bool> = true>
	void writeAttribute(const QString & name, T value) { QXmlStreamWriter::writeAttribute(name, QString::number(value, 'g', 15)); }
};

#endif
