/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfsmartgraphicsextractor.h"

#include <algorithm>

#include <QFile>
#include <QTransform>

#include <poppler/ErrorCodes.h>
#include <poppler/GfxState.h>
#include <poppler/GlobalParams.h>
#include <poppler/Object.h>
#include <poppler/OutputDev.h>
#include <poppler/PDFDoc.h>
#include <poppler/Stream.h>
#include <poppler/goo/GooString.h>

#include "util_os.h"

namespace {

//! Mirrors plugins/import/pdf/slaoutput.cpp::SlaOutputDev::convertPath()
//! (independently reimplemented, not shared code): walks a GfxPath into an
//! SVG path string FPointArray::parseSVG() can consume.
QString convertPathToSvg(const GfxPath* path, bool* closedOut)
{
	*closedOut = false;
	if (!path)
		return QString();

	QString output;
	for (int i = 0; i < path->getNumSubpaths(); ++i)
	{
		const GfxSubpath* subpath = path->getSubpath(i);
		if (subpath->getNumPoints() <= 0)
			continue;
		output += QString("M %1 %2").arg(subpath->getX(0)).arg(subpath->getY(0));
		int j = 1;
		while (j < subpath->getNumPoints())
		{
			if (subpath->getCurve(j))
			{
				output += QString("C %1 %2 %3 %4 %5 %6")
					.arg(subpath->getX(j)).arg(subpath->getY(j))
					.arg(subpath->getX(j + 1)).arg(subpath->getY(j + 1))
					.arg(subpath->getX(j + 2)).arg(subpath->getY(j + 2));
				j += 3;
			}
			else
			{
				output += QString("L %1 %2").arg(subpath->getX(j)).arg(subpath->getY(j));
				++j;
			}
		}
		if (subpath->isClosed())
		{
			output += QLatin1Char('Z');
			*closedOut = true;
		}
	}
	return output;
}

//! Common-case color space conversion -- see the "Known limitations" list
//! in pdfsmartgraphicsextractor.h.
PdfSmartColor colorFromGfx(GfxColorSpace* space, const GfxColor* color)
{
	PdfSmartColor result;
	if (!space || !color)
		return result;

	switch (space->getMode())
	{
	case csDeviceRGB:
	case csCalRGB:
	{
		GfxRGB rgb;
		space->getRGB(color, &rgb);
		result.model = PdfSmartColor::Rgb;
		result.c1 = colToDbl(rgb.r);
		result.c2 = colToDbl(rgb.g);
		result.c3 = colToDbl(rgb.b);
		break;
	}
	case csDeviceCMYK:
	{
		GfxCMYK cmyk;
		space->getCMYK(color, &cmyk);
		result.model = PdfSmartColor::Cmyk;
		result.c1 = colToDbl(cmyk.c);
		result.c2 = colToDbl(cmyk.m);
		result.c3 = colToDbl(cmyk.y);
		result.c4 = colToDbl(cmyk.k);
		break;
	}
	case csDeviceGray:
	case csCalGray:
	{
		GfxGray gray;
		space->getGray(color, &gray);
		result.model = PdfSmartColor::Cmyk;
		result.c4 = 1.0 - colToDbl(gray);
		break;
	}
	default:
		// Separation/Indexed/Pattern/ICCBased/Lab/etc: not handled this
		// session. Solid black so the shape is at least visible, rather
		// than silently dropping its color and leaving it invisible.
		result.model = PdfSmartColor::Cmyk;
		result.c4 = 1.0;
		break;
	}
	return result;
}

//! Constructed fresh per page inside extractFromPage() -- no state to reset
//! between pages. Only overrides what this session needs; every other
//! OutputDev callback (text, patterns, shadings, soft masks, forms, ...)
//! uses the base class's do-nothing default.
class CapturingOutputDev : public OutputDev
{
public:
	CapturingOutputDev(QList<PdfSmartImageRegion>* images, QList<PdfSmartVectorShape>* vectors)
		: m_images(images), m_vectors(vectors)
	{
	}

	bool upsideDown() override { return true; }
	bool useDrawChar() override { return false; }
	bool interpretType3Chars() override { return false; }
	bool useTilingPatternFill() override { return false; }
	bool useShadedFills(int /* type */) override { return false; }
	bool useFillColorStop() override { return false; }
	bool useDrawForm() override { return false; }

	void stroke(GfxState* state) override
	{
		if (!m_vectors)
			return;
		PdfSmartVectorShape shape;
		if (!buildShape(state, &shape))
			return;
		shape.strokeColor = colorFromGfx(state->getStrokeColorSpace(), state->getStrokeColor());
		shape.lineWidth = state->getTransformedLineWidth();
		m_vectors->append(shape);
	}

	void fill(GfxState* state) override { recordFill(state); }
	void eoFill(GfxState* state) override { recordFill(state); }

	void drawImageMask(GfxState* state, Object* ref, Stream* str, int width, int height, bool invert, bool interpolate, bool inlineImg) override
	{
		recordImage(state);
		// The base implementation is what actually consumes an inline
		// image's bytes out of the content stream when inlineImg is true;
		// skipping this call in that case would leave the PDF parser
		// desynchronized on whatever comes next. For a non-inline image it
		// does nothing, so calling it unconditionally is always safe --
		// verified against poppler 25.03.0's OutputDev::drawImageMask().
		OutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
	}

	void drawImage(GfxState* state, Object* ref, Stream* str, int width, int height, GfxImageColorMap* colorMap, bool interpolate, const int* maskColors, bool inlineImg) override
	{
		recordImage(state);
		OutputDev::drawImage(state, ref, str, width, height, colorMap, interpolate, maskColors, inlineImg);
	}

private:
	void recordFill(GfxState* state)
	{
		if (!m_vectors)
			return;
		PdfSmartVectorShape shape;
		if (!buildShape(state, &shape))
			return;
		shape.fillColor = colorFromGfx(state->getFillColorSpace(), state->getFillColor());
		m_vectors->append(shape);
	}

	//! Path extraction shared by fill() and stroke() -- the geometry
	//! doesn't depend on which one is being recorded, only the color does.
	bool buildShape(GfxState* state, PdfSmartVectorShape* shape) const
	{
		bool closed = false;
		QString svg = convertPathToSvg(state->getPath(), &closed);
		if (svg.isEmpty())
			return false;

		FPointArray points;
		if (!points.parseSVG(svg))
			return false;

		const double* ctm = state->getCTM();
		QTransform t(ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]);
		points.map(t);

		FPoint wh = points.widthHeight();
		if (points.size() <= 3 || (wh.x() <= 0.0 && wh.y() <= 0.0))
			return false;

		shape->points = points;
		shape->closed = closed;
		return true;
	}

	void recordImage(GfxState* state)
	{
		if (!m_images)
			return;

		// PDF draws an image into the unit square [0,1]x[0,1] under the
		// CTM in effect at the Do operator -- the standard technique for
		// finding where an image XObject actually lands on the page (also
		// used, with extra rotation handling this session skips, by
		// slaoutput.cpp's own drawImage()).
		const double* ctm = state->getCTM();
		QTransform t(ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]);
		QPointF corners[4] = {
			t.map(QPointF(0, 0)), t.map(QPointF(1, 0)),
			t.map(QPointF(0, 1)), t.map(QPointF(1, 1))
		};
		double minX = corners[0].x(), maxX = corners[0].x();
		double minY = corners[0].y(), maxY = corners[0].y();
		for (int i = 1; i < 4; ++i)
		{
			minX = std::min(minX, corners[i].x());
			maxX = std::max(maxX, corners[i].x());
			minY = std::min(minY, corners[i].y());
			maxY = std::max(maxY, corners[i].y());
		}
		if ((maxX - minX) <= 0.0 || (maxY - minY) <= 0.0)
			return;

		PdfSmartImageRegion region;
		region.x = minX;
		region.y = minY;
		region.width = maxX - minX;
		region.height = maxY - minY;
		m_images->append(region);
	}

	QList<PdfSmartImageRegion>* m_images { nullptr };
	QList<PdfSmartVectorShape>* m_vectors { nullptr };
};

} // namespace

PdfSmartGraphicsExtractor::PdfSmartGraphicsExtractor() = default;
PdfSmartGraphicsExtractor::~PdfSmartGraphicsExtractor() = default;

bool PdfSmartGraphicsExtractor::open(const QString& pdfFileName)
{
	// Same pattern plugins/import/pdf/importpdf.cpp uses to open a low-level
	// PDFDoc: reset globalParams (poppler's process-wide config singleton --
	// safe to redo; the existing PDF plugin already does this on every
	// import) and build a GooString file name with the same platform-aware
	// encoding it uses.
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

int PdfSmartGraphicsExtractor::pageCount() const
{
	return m_pdfDoc ? m_pdfDoc->getNumPages() : 0;
}

void PdfSmartGraphicsExtractor::extractFromPage(int pageNumber, QList<PdfSmartImageRegion>* images, QList<PdfSmartVectorShape>* vectors) const
{
	if (images)
		images->clear();
	if (vectors)
		vectors->clear();

	if (!m_pdfDoc || (!images && !vectors))
		return;

	int pdfPageNumber = pageNumber + 1; // poppler's low-level API is 1-based
	if (pdfPageNumber < 1 || pdfPageNumber > m_pdfDoc->getNumPages())
		return;

	CapturingOutputDev dev(images, vectors);
	// hDPI=vDPI=72 so this OutputDev's device-space units are plain points.
	// useMediaBox=false, crop=false to match exactly what poppler-cpp's own
	// text_list() (used by PdfSmartTextExtractor) passes to the same
	// low-level displayPageSlice() call underneath -- both false means
	// poppler measures from the page's CropBox, not its MediaBox. If these
	// didn't match, image/vector positions would be offset from text
	// positions on any PDF where CropBox != MediaBox (print PDFs with
	// bleed, most commonly).
	m_pdfDoc->displayPage(&dev, pdfPageNumber, 72.0, 72.0, 0, false, false, false);
}
