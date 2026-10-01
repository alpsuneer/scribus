/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "imagecaption.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

#include <zlib.h>

// Order of the sources is documented in imagecaption.h; keep the two in step.

namespace
{
	// ---------------------------------------------------------------- text --

	// UTF-8 when the bytes are valid UTF-8, otherwise Latin-1. Photos from
	// Windows tools and old cameras carry Latin-1 without saying so; anything
	// that writes Malayalam writes UTF-8, and valid UTF-8 is never mistaken
	// for Latin-1 here, so the Malayalam case always takes the first branch.
	QString utf8OrLatin1(const QByteArray& bytes)
	{
		QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
		QString s = dec.decode(bytes);
		if (!dec.hasError())
			return s;
		return QString::fromLatin1(bytes);
	}

	QString ucs2(const QByteArray& bytes, bool bigEndian)
	{
		const int n = bytes.size() / 2;
		QString s;
		s.reserve(n);
		for (int i = 0; i < n; ++i)
		{
			const uchar* p = reinterpret_cast<const uchar*>(bytes.constData()) + 2 * i;
			s.append(QChar(bigEndian ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p)));
		}
		return s;
	}

	// Drop NULs (fixed-size fields are NUL padded) and trim; also strip
	// trailing newlines, which GIMP's comment field keeps.
	QString clean(QString s)
	{
		s.remove(QChar(0));
		return s.trimmed();
	}

	QString xmlUnescape(QString s)
	{
		// Numeric entities first, then the named ones (so "&amp;lt;" stays "&lt;").
		static const QRegularExpression num("&#(x?)([0-9A-Fa-f]+);");
		QRegularExpressionMatchIterator it = num.globalMatch(s);
		QString out;
		int last = 0;
		while (it.hasNext())
		{
			QRegularExpressionMatch m = it.next();
			out += s.mid(last, m.capturedStart() - last);
			bool ok = false;
			uint cp = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
			if (ok && cp <= 0x10FFFF)
				out += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
			last = m.capturedEnd();
		}
		out += s.mid(last);
		out.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
		return out;
	}

	QByteArray inflateAll(const QByteArray& in)
	{
		QByteArray out;
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (inflateInit(&zs) != Z_OK)
			return out;
		zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
		zs.avail_in = in.size();
		char buf[16384];
		int rc = Z_OK;
		while (rc == Z_OK)
		{
			zs.next_out = reinterpret_cast<Bytef*>(buf);
			zs.avail_out = sizeof(buf);
			rc = inflate(&zs, Z_NO_FLUSH);
			if (rc != Z_OK && rc != Z_STREAM_END)
				break;
			out.append(buf, sizeof(buf) - zs.avail_out);
			if (out.size() > (16 << 20))   // a caption is not 16 MB
				break;
		}
		inflateEnd(&zs);
		return out;
	}

	// ----------------------------------------------------------- candidates --

	struct Blobs
	{
		QByteArray xmp;             // XMP packet (XML)
		QByteArray exif;            // TIFF structure (starts with II*\0 or MM\0*)
		QByteArray iptc;            // raw IPTC datasets (0x1C ...)
		QStringList jpegComments;   // COM markers, decoded
		QList<QPair<QString, QString>> pngText;   // keyword, text
	};

	// EXIF / TIFF IFD walk. Only the tags we need, only IFD0 and the Exif sub-IFD.
	struct TiffTag
	{
		quint16 type = 0;
		quint32 count = 0;
		QByteArray data;   // the value bytes, in file byte order
	};

	class TiffReader
	{
	public:
		explicit TiffReader(const QByteArray& b) : m_b(b)
		{
			if (b.size() < 8)
				return;
			if (b.startsWith("II"))
				m_le = true;
			else if (b.startsWith("MM"))
				m_le = false;
			else
				return;
			m_ok = true;
			quint32 ifd0 = u32(4);
			readIfd(ifd0, 0);
			if (m_tags.contains(0x8769))
			{
				const TiffTag& t = m_tags.value(0x8769);
				if (t.data.size() >= 4)
					readIfd(u32Of(t.data, 0), 1);
			}
		}
		bool ok() const { return m_ok; }
		bool littleEndian() const { return m_le; }
		bool has(quint16 tag) const { return m_tags.contains(tag); }
		TiffTag tag(quint16 t) const { return m_tags.value(t); }

	private:
		quint16 u16(int off) const
		{
			if (off < 0 || off + 2 > m_b.size()) return 0;
			const uchar* p = reinterpret_cast<const uchar*>(m_b.constData()) + off;
			return m_le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
		}
		quint32 u32(int off) const
		{
			if (off < 0 || off + 4 > m_b.size()) return 0;
			const uchar* p = reinterpret_cast<const uchar*>(m_b.constData()) + off;
			return m_le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
		}
		quint32 u32Of(const QByteArray& d, int off) const
		{
			if (off + 4 > d.size()) return 0;
			const uchar* p = reinterpret_cast<const uchar*>(d.constData()) + off;
			return m_le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
		}
		static int typeSize(quint16 type)
		{
			switch (type)
			{
				case 1: case 2: case 6: case 7: return 1;
				case 3: case 8: return 2;
				case 4: case 9: case 11: return 4;
				case 5: case 10: case 12: return 8;
				default: return 0;
			}
		}
		void readIfd(quint32 off, int depth)
		{
			if (depth > 1 || off < 8 || off + 2 > (quint32) m_b.size())
				return;
			const int n = u16(off);
			if (n > 1000)
				return;
			for (int i = 0; i < n; ++i)
			{
				const int e = off + 2 + i * 12;
				if (e + 12 > m_b.size())
					return;
				TiffTag t;
				const quint16 id = u16(e);
				t.type = u16(e + 2);
				t.count = u32(e + 4);
				const int sz = typeSize(t.type);
				if (sz == 0 || t.count > (quint32) m_b.size())
					continue;
				const qint64 bytes = (qint64) sz * t.count;
				if (bytes > m_b.size())
					continue;
				int valOff = (bytes <= 4) ? e + 8 : (int) u32(e + 8);
				if (valOff < 0 || valOff + bytes > m_b.size())
					continue;
				t.data = m_b.mid(valOff, bytes);
				if (!m_tags.contains(id))
					m_tags.insert(id, t);
			}
		}

		QByteArray m_b;
		bool m_le = true;
		bool m_ok = false;
		QHash<quint16, TiffTag> m_tags;
	};

	// -------------------------------------------------------------- readers --

	void readJpeg(const QByteArray& f, Blobs& out)
	{
		int pos = 2;
		QByteArray xmpParts;
		while (pos + 4 <= f.size())
		{
			if ((uchar) f[pos] != 0xFF)
				break;
			uchar marker = (uchar) f[pos + 1];
			if (marker == 0xFF) { ++pos; continue; }          // fill byte
			if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }
			if (marker == 0xDA || marker == 0xD9)              // SOS / EOI: image data follows
				break;
			const int len = (((uchar) f[pos + 2]) << 8) | (uchar) f[pos + 3];
			if (len < 2 || pos + 2 + len > f.size())
				break;
			const QByteArray seg = f.mid(pos + 4, len - 2);
			if (marker == 0xE1)
			{
				if (seg.startsWith("Exif\0\0") && out.exif.isEmpty())
					out.exif = seg.mid(6);
				else if (seg.startsWith("http://ns.adobe.com/xap/1.0/\0"))
					out.xmp += seg.mid(29);
				// Extended XMP (http://ns.adobe.com/xmp/extension/) is skipped:
				// a caption lives in the main packet.
			}
			else if (marker == 0xED && seg.startsWith("Photoshop 3.0\0"))
			{
				// 8BIM image resource blocks; 0x0404 is the IPTC-NAA record.
				int p = 14;
				while (p + 12 <= seg.size())
				{
					if (seg.mid(p, 4) != "8BIM")
						break;
					const quint16 id = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(seg.constData()) + p + 4);
					int nameLen = (uchar) seg[p + 6];
					int q = p + 7 + nameLen;
					if ((nameLen + 1) & 1) ++q;               // name is padded to even length
					if (q + 4 > seg.size()) break;
					const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(seg.constData()) + q);
					q += 4;
					if (q + (int) size > seg.size()) break;
					if (id == 0x0404 && out.iptc.isEmpty())
						out.iptc = seg.mid(q, size);
					q += size;
					if (size & 1) ++q;
					p = q;
				}
			}
			else if (marker == 0xFE)
			{
				const QString c = clean(utf8OrLatin1(seg));
				if (!c.isEmpty())
					out.jpegComments << c;
			}
			pos += 2 + len;
		}
	}

	void readPng(const QByteArray& f, Blobs& out)
	{
		int pos = 8;
		while (pos + 8 <= f.size())
		{
			const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(f.constData()) + pos);
			const QByteArray type = f.mid(pos + 4, 4);
			if (pos + 12 + (qint64) len > f.size())
				break;
			const QByteArray data = f.mid(pos + 8, len);
			if (type == "tEXt" || type == "zTXt" || type == "iTXt")
			{
				const int k = data.indexOf('\0');
				if (k > 0)
				{
					const QString keyword = QString::fromLatin1(data.left(k));
					QString text;
					if (type == "tEXt")
						text = QString::fromLatin1(data.mid(k + 1));
					else if (type == "zTXt")
						text = QString::fromLatin1(inflateAll(data.mid(k + 2)));
					else
					{
						// iTXt: keyword\0 compFlag compMethod lang\0 translated\0 text
						int p = k + 1;
						if (p + 2 <= data.size())
						{
							const bool compressed = data[p] != 0;
							p += 2;
							const int l1 = data.indexOf('\0', p);
							const int l2 = (l1 >= 0) ? data.indexOf('\0', l1 + 1) : -1;
							if (l2 >= 0)
							{
								const QByteArray body = data.mid(l2 + 1);
								text = QString::fromUtf8(compressed ? inflateAll(body) : body);
							}
						}
					}
					if (keyword == "XML:com.adobe.xmp")
						out.xmp += text.toUtf8();
					else
						out.pngText << qMakePair(keyword, text);
				}
			}
			else if (type == "eXIf" && out.exif.isEmpty())
				out.exif = data;
			else if (type == "IEND")
				break;
			pos += 12 + len;
		}
	}

	// ------------------------------------------------------------ extract --

	QString xmpDescription(const QByteArray& xmp)
	{
		if (xmp.isEmpty())
			return QString();
		const QString x = QString::fromUtf8(xmp);
		// Element form: <dc:description><rdf:Alt><rdf:li xml:lang="x-default">text</rdf:li>
		static const QRegularExpression elem("<dc:description[^>]*>.*?<rdf:li[^>]*>(.*?)</rdf:li>",
		                                     QRegularExpression::DotMatchesEverythingOption);
		QRegularExpressionMatch m = elem.match(x);
		if (m.hasMatch())
			return clean(xmlUnescape(m.captured(1)));
		// Attribute form: <rdf:Description ... dc:description="text"
		static const QRegularExpression attr("\\bdc:description=\"([^\"]*)\"");
		m = attr.match(x);
		if (m.hasMatch())
			return clean(xmlUnescape(m.captured(1)));
		return QString();
	}

	QString iptcCaption(const QByteArray& iptc)
	{
		if (iptc.isEmpty())
			return QString();
		// IPTC 1:90 CodedCharacterSet: ESC % G is UTF-8. Anything else (or
		// none) is decided by the bytes themselves.
		bool declaredUtf8 = false;
		QByteArray caption;
		int p = 0;
		while (p + 5 <= iptc.size())
		{
			if ((uchar) iptc[p] != 0x1C) { ++p; continue; }
			const uchar record = (uchar) iptc[p + 1];
			const uchar dataset = (uchar) iptc[p + 2];
			quint32 size = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(iptc.constData()) + p + 3);
			int q = p + 5;
			if (size & 0x8000)
			{
				// Extended: low 15 bits give the length of the size field.
				const int n = size & 0x7FFF;
				if (n > 4 || q + n > iptc.size()) break;
				size = 0;
				for (int i = 0; i < n; ++i)
					size = (size << 8) | (uchar) iptc[q + i];
				q += n;
			}
			if (q + (int) size > iptc.size())
				break;
			const QByteArray value = iptc.mid(q, size);
			if (record == 1 && dataset == 90 && value == QByteArray("\x1B%G", 3))
				declaredUtf8 = true;
			if (record == 2 && dataset == 120 && caption.isEmpty())
				caption = value;
			p = q + size;
		}
		if (caption.isEmpty())
			return QString();
		Q_UNUSED(declaredUtf8);   // UTF-8 is tried first either way; see utf8OrLatin1()
		return clean(utf8OrLatin1(caption));
	}

	QString exifUserComment(const TiffTag& t, bool littleEndian)
	{
		if (t.data.size() <= 8)
			return QString();
		const QByteArray head = t.data.left(8);
		const QByteArray body = t.data.mid(8);
		if (head.startsWith("ASCII"))
			return clean(utf8OrLatin1(body));          // "ASCII" fields often hold UTF-8 anyway
		if (head.startsWith("UNICODE"))
		{
			// UCS-2 in the EXIF byte order. A BOM, if present, wins.
			if (body.startsWith("\xFF\xFE")) return clean(ucs2(body.mid(2), false));
			if (body.startsWith("\xFE\xFF")) return clean(ucs2(body.mid(2), true));
			return clean(ucs2(body, !littleEndian));
		}
		if (head.startsWith("JIS"))
		{
			QStringDecoder dec("ISO-2022-JP");
			return dec.isValid() ? clean(dec.decode(body)) : QString();
		}
		// Undefined (all zero) header: gThumb and others put UTF-8 here.
		if (head == QByteArray(8, '\0'))
			return clean(utf8OrLatin1(body));
		// No header at all (some phone cameras write the bare text): the
		// first 8 bytes are part of the text, not a charset code.
		return clean(utf8OrLatin1(t.data));
	}

	void addCandidate(QList<ImageCaption::Result>& out, const QString& text, const QString& source)
	{
		const QString c = clean(text);
		if (c.isEmpty())
			return;
		out.append({ c, source });
	}

	void readSidecars(const QString& imagePath, QList<ImageCaption::Result>& out)
	{
		const QFileInfo fi(imagePath);
		const QString dir = fi.absolutePath();
		QStringList xmpFiles;
		xmpFiles << dir + "/" + fi.completeBaseName() + ".xmp"
		         << imagePath + ".xmp";
		for (const QString& xp : xmpFiles)
		{
			QFile f(xp);
			if (!f.exists() || !f.open(QIODevice::ReadOnly))
				continue;
			const QString d = xmpDescription(f.readAll());
			if (!d.isEmpty())
				addCandidate(out, d, "sidecar " + QFileInfo(xp).fileName());
		}
		// gThumb: <dir>/.comments/<filename>.xml
		//   <comment version="3.0"><caption>..</caption><note>..</note>...</comment>
		QFile g(dir + "/.comments/" + fi.fileName() + ".xml");
		if (g.exists() && g.open(QIODevice::ReadOnly))
		{
			QXmlStreamReader xr(&g);
			QString note, caption;
			while (!xr.atEnd())
			{
				xr.readNext();
				if (!xr.isStartElement())
					continue;
				if (xr.name() == QLatin1String("note"))
					note = xr.readElementText(QXmlStreamReader::IncludeChildElements);
				else if (xr.name() == QLatin1String("caption"))
					caption = xr.readElementText(QXmlStreamReader::IncludeChildElements);
			}
			const QString src = "sidecar .comments/" + fi.fileName() + ".xml";
			addCandidate(out, note, src + " <note>");
			addCandidate(out, caption, src + " <caption>");
		}
	}
}

// ==================================================================== API ==

QStringList ImageCaption::junkDefaults()
{
	// Compared case-insensitively against the trimmed text. Add more here.
	static const QStringList junk {
		QStringLiteral("Created with GIMP"),
		QStringLiteral("Created with The GIMP"),
		QStringLiteral("OLYMPUS DIGITAL CAMERA"),
		QStringLiteral("SONY DSC"),
		QStringLiteral("Screenshot"),
		QStringLiteral("DCIM"),
		QStringLiteral("Exif_JPEG_PICTURE"),
		QStringLiteral("KONICA MINOLTA DIGITAL CAMERA"),
		QStringLiteral("MINOLTA DIGITAL CAMERA"),
		QStringLiteral("SAMSUNG DIGITAL CAMERA"),
		QStringLiteral("DIGITAL CAMERA"),
		QStringLiteral("Processed with VSCO"),
		QStringLiteral("binary comment"),
	};
	return junk;
}

QStringList ImageCaption::junkPrefixes()
{
	// Text that *starts* with one of these is junk: encoder banners and
	// camera debug dumps whose tail varies. Case-insensitive. Add more here.
	static const QStringList junk {
		QStringLiteral("CREATOR: gd-jpeg"),                 // libgd (PHP thumbnails, web downloads)
		QStringLiteral("File written by Adobe Photoshop"),
		QStringLiteral("Intel(R) JPEG Library"),
		QStringLiteral("LEAD Technologies"),
		QStringLiteral("JPEG Encoder Copyright"),
		QStringLiteral("Optimized by JPEGmini"),
		QStringLiteral("AppleMark"),
		QStringLiteral("Lavc"),                             // ffmpeg/libavcodec
		QStringLiteral("filter: 0; fileterIntensity"),      // Xiaomi/Redmi camera debug string (sic)
		QStringLiteral("filter: "),
	};
	return junk;
}

bool ImageCaption::isJunk(const QString& text)
{
	const QString t = text.trimmed();
	if (t.isEmpty())
		return true;
	for (const QString& j : junkDefaults())
		if (t.compare(j, Qt::CaseInsensitive) == 0)
			return true;
	for (const QString& j : junkPrefixes())
		if (t.startsWith(j, Qt::CaseInsensitive))
			return true;
	// Camera debug dumps: several "key: value;" pairs and nothing a person
	// would write as a caption.
	static const QRegularExpression kv("^(?:[A-Za-z_\\-]+\\s*:\\s*[^;]*;\\s*){3,}");
	if (kv.match(t).hasMatch())
		return true;
	return false;
}

QList<ImageCaption::Result> ImageCaption::readAll(const QString& imagePath)
{
	QList<Result> out;
	QFile file(imagePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		readSidecars(imagePath, out);
		return out;
	}
	const QByteArray f = file.readAll();
	file.close();

	Blobs b;
	if (f.startsWith("\xFF\xD8"))
		readJpeg(f, b);
	else if (f.startsWith("\x89PNG\r\n\x1A\n"))
		readPng(f, b);
	else if (f.startsWith("II*\0") || f.startsWith("MM\0*"))
	{
		b.exif = f;
		TiffReader t(f);
		if (t.has(700))   b.xmp = t.tag(700).data;      // XMP
		if (t.has(33723)) b.iptc = t.tag(33723).data;   // IPTC-NAA
	}

	// a. XMP
	addCandidate(out, xmpDescription(b.xmp), "XMP dc:description");
	// b. IPTC
	addCandidate(out, iptcCaption(b.iptc), "IPTC 2:120 Caption-Abstract");
	// c/d/e. EXIF
	if (!b.exif.isEmpty())
	{
		TiffReader t(b.exif);
		if (t.ok())
		{
			if (t.has(0x010E))
				addCandidate(out, utf8OrLatin1(t.tag(0x010E).data), "EXIF ImageDescription");
			if (t.has(0x9286))
				addCandidate(out, exifUserComment(t.tag(0x9286), t.littleEndian()), "EXIF UserComment");
			if (t.has(0x9C9B))
				addCandidate(out, ucs2(t.tag(0x9C9B).data, false), "Windows XPTitle");
			if (t.has(0x9C9C))
				addCandidate(out, ucs2(t.tag(0x9C9C).data, false), "Windows XPComment");
		}
	}
	// f. JPEG COM
	for (const QString& c : b.jpegComments)
		addCandidate(out, c, "JPEG COM");
	// g. PNG text chunks, preferred keywords first
	for (const char* key : { "Description", "Comment", "Title" })
		for (const auto& kv : b.pngText)
			if (kv.first.compare(QLatin1String(key), Qt::CaseInsensitive) == 0)
				addCandidate(out, kv.second, QString("PNG %1").arg(kv.first));
	// h. sidecars
	readSidecars(imagePath, out);
	return out;
}

ImageCaption::Result ImageCaption::read(const QString& imagePath)
{
	const QList<Result> all = readAll(imagePath);
	for (const Result& r : all)
		if (!isJunk(r.text))
			return r;
	return Result();
}
