/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartimageextractor.h"

#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page-renderer.h>
#include <poppler/cpp/poppler-page.h>

#include <QDir>
#include <QImage>
#include <QTemporaryFile>

#include "scpaths.h"

PdfSmartImageExtractor::PdfSmartImageExtractor() = default;
PdfSmartImageExtractor::~PdfSmartImageExtractor() = default;

bool PdfSmartImageExtractor::open(const QString& pdfFileName)
{
	m_document.reset(poppler::document::load_from_file(pdfFileName.toStdString()));
	if (m_document && m_document->is_locked())
		m_document.reset();
	return m_document != nullptr;
}

QList<PdfSmartImageBlock> PdfSmartImageExtractor::extractFromPage(int pageNumber, const QList<PdfSmartImageRegion>& regions, const QString& outputDir, double dpi) const
{
	QList<PdfSmartImageBlock> blocks;
	if (!m_document || regions.isEmpty())
		return blocks;

	std::unique_ptr<poppler::page> page(m_document->create_page(pageNumber));
	if (!page)
		return blocks;

	QDir dir(outputDir);
	if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
		return blocks; // couldn't create the output directory -- skip this page's images, not the whole import

	// Render the whole page once via poppler-cpp's own renderer (same API
	// family PdfSmartTextExtractor uses), then crop it in memory below --
	// see the class comment in the header for why this doesn't decode the
	// original embedded image bytes.
	poppler::page_renderer renderer;
	poppler::image rendered = renderer.render_page(page.get(), dpi, dpi);
	if (!rendered.is_valid())
		return blocks;

	// Round-trip through a real PNG file rather than reading poppler::image's
	// raw pixel buffer directly: that would need mapping poppler's pixel
	// format enum to Qt's QImage::Format with the right byte order, which
	// this session has no way to verify without a compiler or a visual
	// check. Going through poppler's own PNG encoder and Qt's own PNG
	// decoder sidesteps that risk entirely, at the cost of one extra file
	// round-trip per page.
	QTemporaryFile tempFile(QDir(ScPaths::tempFileDir()).filePath(QStringLiteral("pdfsmart_page_XXXXXX.png")));
	tempFile.setAutoRemove(true);
	if (!tempFile.open())
		return blocks;
	QString wholePagePngPath = tempFile.fileName();
	tempFile.close(); // release the handle so poppler can write to the path

	if (!rendered.save(wholePagePngPath.toStdString(), "png", static_cast<int>(dpi)))
		return blocks;

	QImage wholePage(wholePagePngPath);
	if (wholePage.isNull())
		return blocks;

	const double scale = dpi / 72.0; // regions are in points; wholePage is in pixels at dpi

	for (int i = 0; i < regions.size(); ++i)
	{
		const PdfSmartImageRegion& region = regions.at(i);
		QRect pixelRect(
			static_cast<int>(region.x * scale),
			static_cast<int>(region.y * scale),
			static_cast<int>(region.width * scale),
			static_cast<int>(region.height * scale));
		pixelRect = pixelRect.intersected(wholePage.rect());
		if (pixelRect.isEmpty())
			continue;

		QImage cropped = wholePage.copy(pixelRect);
		if (cropped.isNull())
			continue;

		QString filePath = dir.filePath(QStringLiteral("pdfsmart_p%1_img%2.png").arg(pageNumber + 1).arg(i + 1));
		if (!cropped.save(filePath, "PNG"))
			continue;

		PdfSmartImageBlock block;
		block.extractedFilePath = filePath;
		block.x = region.x;
		block.y = region.y;
		block.width = region.width;
		block.height = region.height;
		blocks.append(block);
	}

	return blocks;
}
