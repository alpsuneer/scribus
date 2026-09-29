/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartfontextractor.h"

#include <QDir>
#include <QFile>

#include <poppler/ErrorCodes.h>
#include <poppler/GfxFont.h>
#include <poppler/GfxState.h>
#include <poppler/GlobalParams.h>
#include <poppler/Object.h>
#include <poppler/OutputDev.h>
#include <poppler/PDFDoc.h>
#include <poppler/goo/GooString.h>

#include "util_os.h"

namespace {

//! Constructed fresh per extractFromPage() call; \a extracted persists
//! across calls (owned by PdfSmartFontExtractor) so a font already found
//! on an earlier page is never re-extracted.
class FontCapturingOutputDev : public OutputDev
{
public:
	FontCapturingOutputDev(XRef* xref, QHash<QString, QString>* extracted, const QString& outputDir)
		: m_xref(xref), m_extracted(extracted), m_outputDir(outputDir)
	{
	}

	bool upsideDown() override { return true; }
	bool useDrawChar() override { return false; }
	bool interpretType3Chars() override { return false; }

	void updateFont(GfxState* state) override
	{
		const std::shared_ptr<GfxFont>& font = state->getFont();
		if (!font)
			return;

		if (!font->getName())
			return; // some fonts (notably many Type 3 fonts) have no name at all

		QString fontName = QString::fromStdString(font->getNameWithoutSubsetTag());
		if (fontName.isEmpty() || m_extracted->contains(fontName))
			return; // nothing to key it by, or already extracted on an earlier page

		Ref embeddedId;
		if (!font->getEmbeddedFontID(&embeddedId))
			return; // not embedded in this PDF -- Feature 2's substitution is the fallback

		QString extension;
		switch (font->getType())
		{
		case fontTrueType:
		case fontTrueTypeOT:
		case fontCIDType2:
		case fontCIDType2OT:
			extension = QStringLiteral("ttf");
			break;
		case fontType1C:
		case fontType1COT:
		case fontCIDType0C:
		case fontCIDType0COT:
			extension = QStringLiteral("otf");
			break;
		default:
			// Type 3 (no font file exists) or raw Type1 (PFA/PFB, not
			// handled this session) -- see the header's "Known
			// limitations".
			return;
		}

		std::optional<std::vector<unsigned char>> data = font->readEmbFontFile(m_xref);
		if (!data || data->empty())
			return;

		QDir dir(m_outputDir);
		if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
			return; // couldn't create the output directory -- skip this font, not the whole page

		QString filePath = dir.filePath(QStringLiteral("pdfsmart_font_%1.%2").arg(static_cast<int>(m_extracted->size())).arg(extension));
		QFile file(filePath);
		if (!file.open(QIODevice::WriteOnly))
			return;
		file.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size()));
		file.close();

		m_extracted->insert(fontName, filePath);
	}

private:
	XRef* m_xref { nullptr };
	QHash<QString, QString>* m_extracted { nullptr };
	QString m_outputDir;
};

} // namespace

PdfSmartFontExtractor::PdfSmartFontExtractor() = default;
PdfSmartFontExtractor::~PdfSmartFontExtractor() = default;

bool PdfSmartFontExtractor::open(const QString& pdfFileName)
{
	// Same pattern as PdfSmartGraphicsExtractor::open() -- see that class
	// for why globalParams gets reset here.
	globalParams.reset(new GlobalParams());
	globalParams->setErrQuiet(true);

	QByteArray encodedFileName = os_is_win() ? pdfFileName.toUtf8() : QFile::encodeName(pdfFileName);
	auto fname = std::make_unique<GooString>(encodedFileName.data());
	m_pdfDoc = std::make_unique<PDFDoc>(std::move(fname));

	if (!m_pdfDoc->isOk() || m_pdfDoc->getErrorCode() == errEncrypted)
	{
		m_pdfDoc.reset();
		return false;
	}
	return true;
}

QHash<QString, QString> PdfSmartFontExtractor::extractFromPage(int pageNumber, const QString& outputDir)
{
	if (!m_pdfDoc)
		return {};

	int pdfPageNumber = pageNumber + 1; // poppler's low-level API is 1-based
	if (pdfPageNumber < 1 || pdfPageNumber > m_pdfDoc->getNumPages())
		return {};

	QHash<QString, QString> before = m_extracted;

	FontCapturingOutputDev dev(m_pdfDoc->getXRef(), &m_extracted, outputDir);
	// Same hDPI/vDPI/useMediaBox/crop choices as PdfSmartGraphicsExtractor,
	// for the same reason: not that font extraction cares about coordinate
	// space, but PDFDoc::displayPage() needs some values, and matching
	// keeps every low-level pass in this plugin consistent.
	m_pdfDoc->displayPage(&dev, pdfPageNumber, 72.0, 72.0, 0, false, false, false);

	QHash<QString, QString> newlyExtracted;
	for (auto it = m_extracted.constBegin(); it != m_extracted.constEnd(); ++it)
	{
		if (!before.contains(it.key()))
			newlyExtracted.insert(it.key(), it.value());
	}
	return newlyExtracted;
}
