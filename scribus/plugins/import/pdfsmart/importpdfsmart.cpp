/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "importpdfsmart.h"
#include "pdfsmartcolumndetector.h"
#include "pdfsmartfontextractor.h"
#include "pdfsmartfontmapper.h"
#include "pdfsmartgraphicsextractor.h"
#include "pdfsmartimageextractor.h"
#include "pdfsmartimportoptions.h"
#include "pdfsmartmalayalamfix.h"
#include "pdfsmarttextextractor.h"

#include <algorithm>
#include <vector>

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>

#include "commonstrings.h"
#include "pageitem.h"
#include "prefsmanager.h"
#include "sccolor.h"
#include "scfonts.h"
#include "scpage.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "styles/charstyle.h"
#include "styles/paragraphstyle.h"
#include "util.h"

namespace {

//! Best-effort PDF-font-name -> installed-Scribus-font match. Mirrors
//! SlaOutputDev::applyTextStyle() in plugins/import/pdf/slaoutput.cpp (the
//! existing PDF importer's own font-matching code) so this plugin's
//! best-effort behaves the same way the proven one does -- same three
//! name fields tried, same restriction to TTF faces. \retval an unusable
//! face (ScFace::usable() == false) if nothing matches; the caller should
//! then leave the frame's font at the document default.
ScFace findMatchingFont(SCFonts& allFonts, const QString& fontName)
{
	if (fontName.isEmpty())
		return ScFace::none();

	SCFontsIterator it(allFonts);
	for ( ; it.hasNext(); it.next())
	{
		const ScFace& face(it.current());
		if (!face.usable() || face.type() != ScFace::TTF)
			continue;
		if (face.psName() == fontName || face.family() == fontName || face.scName() == fontName)
			return face;
	}
	return ScFace::none();
}

//! PDF's standard convention for a subsetted font's /BaseFont name (ISO
//! 32000-1 9.6.4): exactly six uppercase Latin letters, then '+', then the
//! real name -- e.g. "ABCDEF+Calibri". Mirrors GfxFont::
//! getNameWithoutSubsetTag()'s own rule (which is what
//! PdfSmartFontExtractor keys its results by), reimplemented here since
//! poppler-cpp's text_box -- unlike the low-level GfxFont -- doesn't
//! expose a stripped name directly, and block.fontName (from
//! PdfSmartTextExtractor/PdfSmartColumnDetector, both poppler-cpp based)
//! still carries the tag if the PDF's font was subsetted.
QString stripPdfSubsetTag(const QString& fontName)
{
	if (fontName.size() <= 7 || fontName.at(6) != QLatin1Char('+'))
		return fontName;
	for (int i = 0; i < 6; ++i)
	{
		QChar c = fontName.at(i);
		if (c < QLatin1Char('A') || c > QLatin1Char('Z'))
			return fontName;
	}
	return fontName.mid(7);
}

//! Feature 2, coarse version: apply the page's dominant font name/size to
//! the whole frame's default style. There is one style for the whole block
//! (poppler-cpp's text_list() doesn't expose per-run boundaries lined up
//! with text()'s reconstructed string well enough to do per-run styling
//! reliably), and no color/bold/italic -- poppler-cpp's page-level API
//! doesn't expose those either. Real per-run styling would need the same
//! lower-level GfxState/OutputDev approach plugins/import/pdf uses, not
//! poppler-cpp; that is future-session work, not this one's.
//!
//! Feature 7: if \a registeredFonts (PDF font name, subset tag stripped,
//! -> Scribus font lookup key) has an entry for this block's font, that
//! extracted-from-the-PDF-itself font is used in preference to Feature 2's
//! name-matched substitute -- the exact font beats a lookalike whenever
//! PdfSmartFontExtractor managed to pull it out.
void applyBlockFormatting(PageItem* item, const PdfSmartTextBlock& block, ScribusDoc* doc, const QHash<QString, QString>& registeredFonts)
{
	CharStyle newStyle;
	if (block.fontSize > 0.0)
		newStyle.setFontSize(block.fontSize * 10); // CharStyle::FontSize is in decipoints

	ScFace face = ScFace::none();
	QString registeredKey = registeredFonts.value(stripPdfSubsetTag(block.fontName));
	if (!registeredKey.isEmpty())
		face = doc->AllFonts->value(registeredKey);
	if (!face.usable())
		face = findMatchingFont(*doc->AllFonts, block.fontName);
	if (face.usable())
		newStyle.setFont(face);

	ParagraphStyle dstyle(item->itemText.defaultStyle());
	dstyle.charStyle().applyCharStyle(newStyle);
	item->itemText.setDefaultStyle(dstyle);
	item->itemText.applyCharStyle(0, item->itemText.length(), newStyle);
	item->invalid = true;
}

//! Registers a PdfSmartColor with the document's color list (mirroring
//! SlaOutputDev::getColor()'s tryAddColor() pattern, with an isolated name
//! prefix so a color this plugin creates is never mistaken for one the
//! standard PDF importer made). \retval CommonStrings::None if \a color has
//! no model (e.g. a stroke() call recorded no fill, or vice versa).
QString registerColor(ScribusDoc* doc, const PdfSmartColor& color)
{
	if (color.model == PdfSmartColor::None)
		return CommonStrings::None;

	ScColor tmp;
	tmp.setSpotColor(false);
	tmp.setRegistrationColor(false);
	if (color.model == PdfSmartColor::Rgb)
		tmp.setRgbColorF(color.c1, color.c2, color.c3);
	else
		tmp.setCmykColorF(color.c1, color.c2, color.c3, color.c4);
	return doc->PageColors.tryAddColor(QStringLiteral("FromPdfSmart") + tmp.name(), tmp);
}

//! Feature 3: places one already-cropped-and-saved image file into its own
//! frame. Free scaling + keep-aspect-ratio is safe here because the crop
//! was sized in the same aspect ratio as the frame (both come from the
//! same PdfSmartImageRegion) -- see pdfsmartimageextractor.h for how the
//! file itself was produced (a re-render of the page region, not the
//! original embedded bytes).
void createImageItem(ScribusDoc* doc, ScPage* targetPage, const PdfSmartImageBlock& block)
{
	double x = targetPage->xOffset() + block.x;
	double y = targetPage->yOffset() + block.y;
	double w = std::max(block.width, 1.0);
	double h = std::max(block.height, 1.0);

	int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle, x, y, w, h, 0,
		CommonStrings::None, CommonStrings::None);
	if (z < 0)
		return;

	PageItem* item = doc->Items->at(z);
	doc->loadPict(block.extractedFilePath, item);
	item->setImageScalingMode(true, true);
}

//! Feature 4: places one vector shape as its own Polygon (closed) or
//! PolyLine (open) item. Mirrors SlaOutputDev::stroke()'s pattern of
//! adding the item at the page's own origin with a placeholder size, then
//! letting ScribusDoc::adjustItemSize() reconcile the item's real position
//! and size from the PoLine geometry that's assigned afterward -- see the
//! "Known limitations" list in pdfsmartgraphicsextractor.h for what this
//! does NOT attempt (merging a filled+stroked shape into one item, dash
//! patterns, line caps/joins, opacity).
void createVectorItem(ScribusDoc* doc, ScPage* targetPage, const PdfSmartVectorShape& shape)
{
	if (shape.points.size() <= 3)
		return;

	QString fillColorName = registerColor(doc, shape.fillColor);
	QString strokeColorName = registerColor(doc, shape.strokeColor);

	double xCoor = targetPage->xOffset();
	double yCoor = targetPage->yOffset();

	PageItem::ItemType itemType = shape.closed ? PageItem::Polygon : PageItem::PolyLine;
	int z = doc->itemAdd(itemType, PageItem::Unspecified, xCoor, yCoor, 10, 10, shape.lineWidth,
		fillColorName, strokeColorName);
	if (z < 0)
		return;

	PageItem* item = doc->Items->at(z);
	item->PoLine = shape.points.copy();
	item->ClipEdited = true;
	item->FrameType = 3;
	FPoint wh = shape.points.widthHeight();
	item->setWidthHeight(wh.x(), wh.y());
	doc->adjustItemSize(item);
	item->setTextFlowMode(PageItem::TextFlowDisabled);
}

} // namespace

ImportPdfSmart::ImportPdfSmart(QObject* parent) : QObject(parent)
{
}

bool ImportPdfSmart::run()
{
	PdfSmartImportOptions dlg(ScCore->primaryMainWindow());
	if (dlg.exec() != QDialog::Accepted)
		return false;

	PdfSmartImportSettings settings = dlg.settings();
	ScribusDoc* doc = ScCore->primaryMainWindow()->doc;

	return importWithSettings(settings, doc);
}

bool ImportPdfSmart::importWithSettings(const PdfSmartImportSettings& settings, ScribusDoc* /* doc */)
{
	ScribusMainWindow* mainWin = ScCore->primaryMainWindow();

	if (settings.fileName.trimmed().isEmpty())
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("No PDF file was chosen."));
		return false;
	}
	if (!QFileInfo::exists(settings.fileName))
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("The file \"%1\" does not exist.").arg(settings.fileName));
		return false;
	}
	if (!settings.importTextAsFrames && !settings.importImages && !settings.preserveVectorObjects)
	{
		// This build implements Features 1, 3 and 4 (text, images, vector
		// objects). Every other checkbox is accepted by the dialog but has
		// no effect yet (see importpdfsmart.h); with all three of the
		// things this build can actually do turned off, there would be
		// nothing to import.
		QMessageBox::information(mainWin, tr("Smart PDF Import"),
			tr("This build can import text, images and vector objects. Please leave at least one of "
			   "\"Import Text as Editable Text Frames\", \"Import Images\" or \"Preserve Vector Objects\" checked."));
		return false;
	}
	if (!settings.importIntoNewDocument)
	{
		QMessageBox::information(mainWin, tr("Smart PDF Import"),
			tr("Importing into the current document is not implemented yet this session. Please choose \"New Document\" as the target."));
		return false;
	}

	PdfSmartTextExtractor extractor;
	if (!extractor.open(settings.fileName))
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"),
			tr("\"%1\" could not be opened as a PDF (it may be corrupt, or password-protected).").arg(settings.fileName));
		return false;
	}

	int pdfPageCount = extractor.pageCount();
	if (pdfPageCount <= 0)
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("This PDF has no pages."));
		return false;
	}

	// parsePagesString() (scribus/util.cpp) is the same page-range parser
	// used throughout Scribus (print dialog, page import, etc.) -- 1-based
	// page numbers, "*" for all pages.
	std::vector<int> pageNumbers;
	parsePagesString(settings.pageRange, &pageNumbers, pdfPageCount);
	if (pageNumbers.empty())
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("The page range \"%1\" is invalid.").arg(settings.pageRange));
		return false;
	}

	QSizeF firstPageSize = extractor.pageSizePoints(pageNumbers.front() - 1);
	if (firstPageSize.isEmpty())
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("Could not read the size of page %1.").arg(pageNumbers.front()));
		return false;
	}

	// Images and vectors both come from PdfSmartGraphicsExtractor's single
	// low-level render pass per page (see that class's header for why this
	// needs poppler's low-level API rather than the poppler-cpp extractor
	// above). Opened once, up front, like the text extractor -- a failure
	// here is a warning, not fatal: text import can still proceed.
	bool wantImages = settings.importImages;
	bool wantVectors = settings.preserveVectorObjects;
	PdfSmartGraphicsExtractor graphicsExtractor;
	if ((wantImages || wantVectors) && !graphicsExtractor.open(settings.fileName))
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"),
			tr("Images and vector objects could not be processed for this file -- continuing without them."));
		wantImages = false;
		wantVectors = false;
	}

	// Feature 5: stateless, just reused per page below.
	PdfSmartColumnDetector columnDetector;

	// Feature 3 needs a second, independent poppler-cpp handle (see
	// pdfsmartimageextractor.h) to render page pixels for cropping.
	PdfSmartImageExtractor imageExtractor;
	if (wantImages && !imageExtractor.open(settings.fileName))
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"),
			tr("Images could not be processed for this file -- continuing without them."));
		wantImages = false;
	}
	QString imagesOutputDir;
	if (wantImages)
	{
		QFileInfo pdfInfo(settings.fileName);
		imagesOutputDir = pdfInfo.absolutePath() + QStringLiteral("/") + pdfInfo.completeBaseName() + QStringLiteral("_pdfsmart_images");
	}

	// Feature 7: a third independent low-level pass (see
	// pdfsmartfontextractor.h for why it's kept separate from
	// PdfSmartGraphicsExtractor rather than added to that already-working
	// class). registeredFonts accumulates PDF font name (subset tag
	// stripped) -> Scribus lookup key across every page, so a font found
	// on page 1 is available when formatting a block on page 5 too.
	bool wantFonts = settings.convertEmbeddedFonts;
	PdfSmartFontExtractor fontExtractor;
	if (wantFonts && !fontExtractor.open(settings.fileName))
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"),
			tr("Embedded fonts could not be processed for this file -- falling back to substituted fonts."));
		wantFonts = false;
	}
	QString fontsOutputDir;
	if (wantFonts)
	{
		QFileInfo pdfInfo(settings.fileName);
		fontsOutputDir = pdfInfo.absolutePath() + QStringLiteral("/") + pdfInfo.completeBaseName() + QStringLiteral("_pdfsmart_fonts");
	}
	QHash<QString, QString> registeredFonts;
	int fontsConverted = 0;

	QProgressDialog progress(tr("Analyzing PDF..."), tr("Cancel"), 0, static_cast<int>(pageNumbers.size()), mainWin);
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	progress.setValue(0);

	// firstPageSize (and every PdfSmartTextBlock coordinate below) is in
	// points with no unit conversion applied before reaching doFileNew()/
	// itemAdd() -- verified against scribus/units.cpp's
	// unitGetRatioFromIndex(), where the PT entry is 1.0 and every other
	// unit is defined relative to it, and cross-checked against
	// plugins/import/pdf/slaoutput.cpp, which passes PDF geometry straight
	// through to itemAdd() the same way, only ever adding the target page's
	// own xOffset()/yOffset() -- never scaling. Scribus's internal unit is
	// points; docUnitIndex below only controls the ruler/dialog display
	// unit for the new document, not how these numbers are interpreted.
	//
	// One new document sized to the FIRST imported page. Session 2 does not
	// handle a PDF whose pages differ in size -- every page in the new
	// document gets this same size, a known limitation for mixed-size PDFs.
	ScribusDoc* newDoc = mainWin->doFileNew(
		firstPageSize.width(), firstPageSize.height(),
		0.0, 0.0, 0.0, 0.0,
		0.0, 0,
		false,
		0,
		PrefsManager::instance().appPrefs.docSetupPrefs.docUnitIndex,
		0, 0, 1,
		QStringLiteral("Custom"), true,
		static_cast<int>(pageNumbers.size()), true, 0);

	if (!newDoc)
	{
		QMessageBox::warning(mainWin, tr("Smart PDF Import"), tr("Could not create a new document."));
		return false;
	}

	int textFramesCreated = 0;
	int imagesCreated = 0;
	int vectorsCreated = 0;
	int flowContinuationsDetected = 0;
	int malayalamBlocksCorrected = 0;
	int pagesWithNoContent = 0;
	size_t pagesProcessed = 0;
	bool wasCanceled = false;

	for (pagesProcessed = 0; pagesProcessed < pageNumbers.size(); ++pagesProcessed)
	{
		if (progress.wasCanceled())
		{
			wasCanceled = true;
			break;
		}

		int pdfPageNumber = pageNumbers[pagesProcessed]; // 1-based
		progress.setLabelText(tr("Page %1 of %2...").arg(static_cast<int>(pagesProcessed) + 1).arg(static_cast<int>(pageNumbers.size())));
		progress.setValue(static_cast<int>(pagesProcessed));
		QApplication::processEvents();

		if (static_cast<int>(pagesProcessed) >= newDoc->DocPages.count())
			break; // shouldn't happen -- doFileNew created one page per imported PDF page

		ScPage* targetPage = newDoc->DocPages.at(static_cast<int>(pagesProcessed));
		newDoc->setCurrentPage(targetPage); // so each new item's OwnPage lands on this page

		bool pageHasContent = false;

		// Only useful together with text import (applyBlockFormatting() is
		// the only consumer of registeredFonts), so skip the work entirely
		// when text import is off.
		if (wantFonts && settings.importTextAsFrames)
		{
			QHash<QString, QString> newlyExtracted = fontExtractor.extractFromPage(pdfPageNumber - 1, fontsOutputDir);
			for (auto it = newlyExtracted.constBegin(); it != newlyExtracted.constEnd(); ++it)
			{
				QString registeredKey = PdfSmartFontMapper::registerEmbeddedFont(newDoc, it.value());
				if (!registeredKey.isEmpty())
				{
					registeredFonts.insert(it.key(), registeredKey);
					fontsConverted++;
				}
			}
		}

		if (settings.importTextAsFrames)
		{
			// Column detection needs word-level data and rebuilds its own
			// blocks from it (see pdfsmartcolumndetector.h for why
			// extractFromPage()'s single whole-page block, built from
			// poppler's already-interleaved physical_layout text, can't be
			// split into columns after the fact). Unchecked, this is
			// exactly Session 2's original single-block-per-page behavior,
			// untouched.
			QList<PdfSmartTextBlock> textBlocks;
			if (settings.detectColumnsAutomatically)
			{
				QList<PdfSmartWordBox> words = extractor.extractWordsFromPage(pdfPageNumber - 1);
				textBlocks = columnDetector.detectColumnBlocks(words);
			}
			else
			{
				textBlocks = extractor.extractFromPage(pdfPageNumber - 1);
			}

			// Feature 6: detection only, not linked -- see
			// pdfsmartcolumndetector.h for why actually calling
			// PageItem::link() on these would risk re-flowing text away
			// from the positions just extracted above.
			if (settings.recreateTextFlow)
				flowContinuationsDetected += static_cast<int>(columnDetector.detectTextFlow(textBlocks).size());

			for (const PdfSmartTextBlock& block : textBlocks)
			{
				double x = targetPage->xOffset() + block.x;
				double y = targetPage->yOffset() + block.y;
				double w = std::max(block.width, 1.0);
				double h = std::max(block.height, 1.0);

				int z = newDoc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, x, y, w, h, 0,
					CommonStrings::None, CommonStrings::None);
				if (z >= 0)
				{
					PageItem* item = newDoc->Items->at(z);

					// Feature 8: NFC normalization + legacy chillu-sequence
					// collapsing only -- see pdfsmartmalayalamfix.h for what
					// this deliberately does not attempt (reordering,
					// missing-ZWJ conjunct repair) and why. Safe to skip
					// blocks with no Malayalam at all.
					bool isMalayalam = settings.malayalamIndicCorrection && PdfSmartMalayalamFix::containsMalayalam(block.text);
					QString text = isMalayalam ? PdfSmartMalayalamFix::correct(block.text) : block.text;
					item->itemText.insertChars(text);
					if (isMalayalam)
						malayalamBlocksCorrected++;

					// Feature 7's registered-font substitution lives inside
					// applyBlockFormatting() (see that function's comment),
					// so it only takes effect together with Feature 2 here --
					// a documented coupling, not attempting to apply a
					// converted font without also applying formatting.
					if (settings.preserveTextFormatting)
						applyBlockFormatting(item, block, newDoc, registeredFonts);
					textFramesCreated++;
					pageHasContent = true;
				}
			}
		}

		if (wantImages || wantVectors)
		{
			QList<PdfSmartImageRegion> imageRegions;
			QList<PdfSmartVectorShape> vectorShapes;
			graphicsExtractor.extractFromPage(pdfPageNumber - 1,
				wantImages ? &imageRegions : nullptr,
				wantVectors ? &vectorShapes : nullptr);

			if (wantImages && !imageRegions.isEmpty())
			{
				QList<PdfSmartImageBlock> imageBlocks = imageExtractor.extractFromPage(pdfPageNumber - 1, imageRegions, imagesOutputDir);
				for (const PdfSmartImageBlock& imageBlock : imageBlocks)
				{
					createImageItem(newDoc, targetPage, imageBlock);
					imagesCreated++;
					pageHasContent = true;
				}
			}

			if (wantVectors)
			{
				for (const PdfSmartVectorShape& shape : vectorShapes)
				{
					createVectorItem(newDoc, targetPage, shape);
					vectorsCreated++;
					pageHasContent = true;
				}
			}
		}

		if (!pageHasContent)
			pagesWithNoContent++;
	}

	progress.setValue(static_cast<int>(pageNumbers.size()));
	newDoc->setCurrentPage(newDoc->DocPages.at(0));
	newDoc->setModified(true);

	int totalCreated = textFramesCreated + imagesCreated + vectorsCreated;
	QString summary = wasCanceled
		? tr("Import canceled after %1 of %2 page(s).\n").arg(static_cast<int>(pagesProcessed)).arg(static_cast<int>(pageNumbers.size()))
		: tr("Imported %1 page(s).\n").arg(static_cast<int>(pageNumbers.size()));
	if (settings.importTextAsFrames)
		summary += QStringLiteral("\n") + tr("%1 text frame(s) created.").arg(textFramesCreated);
	if (settings.importImages)
		summary += QStringLiteral("\n") + tr("%1 image(s) created.").arg(imagesCreated);
	if (settings.preserveVectorObjects)
		summary += QStringLiteral("\n") + tr("%1 vector object(s) created.").arg(vectorsCreated);
	if (settings.convertEmbeddedFonts)
	{
		if (fontsConverted > 0)
			summary += QStringLiteral("\n") + tr("%1 embedded font(s) extracted from the PDF and used directly "
				"(TrueType/OpenType and CFF only; Type 3 and raw Type 1 fonts fall back to a substituted font "
				"instead -- see the plugin notes). These fonts remain available for the rest of this Scribus "
				"session, in any document, until Scribus is restarted.").arg(fontsConverted);
		else
			summary += QStringLiteral("\n") + tr("No embedded fonts could be extracted; substituted fonts were used instead.");
	}
	if (settings.malayalamIndicCorrection)
		summary += QStringLiteral("\n") + tr("%1 text frame(s) had Malayalam/Indic correction applied "
			"(Unicode normalization and legacy chillu-letter fixes only -- not character reordering; "
			"see the plugin notes).").arg(malayalamBlocksCorrected);
	if (settings.recreateTextFlow)
	{
		if (flowContinuationsDetected > 0)
			summary += QStringLiteral("\n") + tr("%1 likely text-flow continuation(s) detected between frames, "
				"but not linked automatically -- linking would risk shifting text away from the positions "
				"just placed. If you want two frames to flow as one story, select the first, then use "
				"Item > Text Frame Links > Link Text Frames and click the second.")
				.arg(flowContinuationsDetected);
		else
			summary += QStringLiteral("\n") + tr("No likely text-flow continuations detected between frames.");
	}
	if (pagesWithNoContent > 0)
		summary += QStringLiteral("\n") + tr("%1 page(s) had nothing this build could extract.").arg(pagesWithNoContent);
	QMessageBox::information(mainWin, tr("Smart PDF Import"), summary);

	return totalCreated > 0;
}
