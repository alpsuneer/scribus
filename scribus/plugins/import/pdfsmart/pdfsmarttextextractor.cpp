/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmarttextextractor.h"

#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-global.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-rectangle.h>

#include <algorithm>

#include <QHash>

namespace {

QString ustringToQString(const poppler::ustring& s)
{
	poppler::byte_array utf8 = s.to_utf8();
	return QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

} // namespace

PdfSmartTextExtractor::PdfSmartTextExtractor() = default;
PdfSmartTextExtractor::~PdfSmartTextExtractor() = default;

bool PdfSmartTextExtractor::open(const QString& pdfFileName)
{
	m_document.reset(poppler::document::load_from_file(pdfFileName.toStdString()));
	// A locked (password-protected) document loads but exposes no content
	// until unlocked -- there's no UI for a password in this session's
	// dialog, so treat it the same as a failed open.
	if (m_document && m_document->is_locked())
		m_document.reset();
	return m_document != nullptr;
}

int PdfSmartTextExtractor::pageCount() const
{
	return m_document ? m_document->pages() : 0;
}

QSizeF PdfSmartTextExtractor::pageSizePoints(int pageNumber) const
{
	if (!m_document)
		return QSizeF();
	std::unique_ptr<poppler::page> page(m_document->create_page(pageNumber));
	if (!page)
		return QSizeF();
	// page_rect() is the raw PDF MediaBox, unscaled and un-rotated -- exactly
	// the page size Scribus should create, since Scribus's own internal unit
	// is points too (see importpdfsmart.cpp).
	poppler::rectf r = page->page_rect();
	return QSizeF(r.width(), r.height());
}

QList<PdfSmartTextBlock> PdfSmartTextExtractor::extractFromPage(int pageNumber) const
{
	QList<PdfSmartTextBlock> blocks;
	if (!m_document)
		return blocks;

	std::unique_ptr<poppler::page> page(m_document->create_page(pageNumber));
	if (!page)
		return blocks;

	// physical_layout is poppler's own, well-tested attempt at reproducing
	// visual line and paragraph structure (blank lines between paragraphs,
	// reasonable column handling) -- far more reliable than a from-scratch
	// word-clustering pass would be for a first implementation. Splitting
	// this one page-sized block into per-paragraph/per-column blocks is
	// left to a later session (Features 5/6).
	QString text = ustringToQString(page->text(poppler::rectf(), poppler::page::physical_layout)).trimmed();
	if (text.isEmpty())
		return blocks; // no extractable text -- e.g. a scanned/image-only page

	// text_list() is a second, separate extraction pass (poppler re-renders
	// the page internally for it) used only to find where the text actually
	// sits on the page and which font dominates it -- text() above already
	// gave us the content. Not the most efficient way to do this, but simple
	// and correct, which matters more given this can't be build-tested here.
	//
	// IMPORTANT: text_box::bbox() comes from poppler's TextOutputDev, which
	// always renders into a top-left-origin, Y-down "device" space
	// (confirmed via TextOutputDev::upsideDown() == true in poppler's own
	// source, poppler-25.03.0/poppler/TextOutputDev.h) -- NOT the
	// bottom-left-origin space that page_rect()/raw PDF geometry use. So
	// these coordinates need no Y-flip, unlike the naive "PDF is
	// bottom-left" assumption. text_list() also always renders at rotation
	// 0 regardless of the page's own /Rotate entry, so a rotated PDF page's
	// text will not line up correctly here -- a known limitation, not
	// handled this session.
	std::vector<poppler::text_box> words = page->text_list(poppler::page::text_list_include_font);

	double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
	bool haveBox = false;
	QHash<QString, int> fontVotes;     // "name|size" -> word count
	QHash<QString, QString> fontNameOf; // "name|size" -> name
	QHash<QString, double> fontSizeOf;  // "name|size" -> size

	for (const poppler::text_box& tb : words)
	{
		poppler::rectf bbox = tb.bbox();
		if (bbox.width() <= 0.0 && bbox.height() <= 0.0)
			continue;

		if (!haveBox)
		{
			minX = bbox.left();
			minY = bbox.top();
			maxX = bbox.right();
			maxY = bbox.bottom();
			haveBox = true;
		}
		else
		{
			minX = std::min(minX, bbox.left());
			minY = std::min(minY, bbox.top());
			maxX = std::max(maxX, bbox.right());
			maxY = std::max(maxY, bbox.bottom());
		}

		if (tb.has_font_info())
		{
			QString name = QString::fromUtf8(tb.get_font_name().c_str());
			double size = tb.get_font_size();
			QString key = name + QLatin1Char('|') + QString::number(size, 'f', 2);
			fontVotes[key]++;
			fontNameOf[key] = name;
			fontSizeOf[key] = size;
		}
	}

	PdfSmartTextBlock block;
	block.text = text;

	if (haveBox)
	{
		block.x = minX;
		block.y = minY;
		block.width = maxX - minX;
		block.height = maxY - minY;
	}
	else
	{
		// text_list() found nothing usable (shouldn't normally happen when
		// text() above returned content) -- fall back to the whole page so
		// the text is at least placed somewhere rather than dropped.
		poppler::rectf pageRect = page->page_rect();
		block.x = 0.0;
		block.y = 0.0;
		block.width = pageRect.width();
		block.height = pageRect.height();
	}

	QString bestKey;
	int bestVotes = 0;
	for (auto it = fontVotes.constBegin(); it != fontVotes.constEnd(); ++it)
	{
		if (it.value() > bestVotes)
		{
			bestVotes = it.value();
			bestKey = it.key();
		}
	}
	if (!bestKey.isEmpty())
	{
		block.fontName = fontNameOf.value(bestKey);
		block.fontSize = fontSizeOf.value(bestKey);
	}

	blocks.append(block);
	return blocks;
}

QList<PdfSmartWordBox> PdfSmartTextExtractor::extractWordsFromPage(int pageNumber) const
{
	QList<PdfSmartWordBox> result;
	if (!m_document)
		return result;

	std::unique_ptr<poppler::page> page(m_document->create_page(pageNumber));
	if (!page)
		return result;

	std::vector<poppler::text_box> words = page->text_list(poppler::page::text_list_include_font);
	result.reserve(static_cast<int>(words.size()));

	for (const poppler::text_box& tb : words)
	{
		poppler::rectf bbox = tb.bbox();
		if (bbox.width() <= 0.0 && bbox.height() <= 0.0)
			continue;

		QString text = ustringToQString(tb.text());
		if (text.isEmpty())
			continue;

		PdfSmartWordBox word;
		word.text = text;
		word.x = bbox.left();
		word.y = bbox.top();
		word.width = bbox.width();
		word.height = bbox.height();
		word.hasSpaceAfter = tb.has_space_after();
		if (tb.has_font_info())
		{
			word.fontName = QString::fromUtf8(tb.get_font_name().c_str());
			word.fontSize = tb.get_font_size();
		}
		result.append(word);
	}

	return result;
}
