/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMPOSITIONENGINE_H
#define SCIMPOSITIONENGINE_H

#include "scribusapi.h"

#include <QString>
#include <QStringList>

/**
 * Settings for a two-page side-by-side (or stacked) imposition onto one
 * plate sheet.
 *
 * Plate size/resolution default to the shop's CTP setup (see
 * ScImpositionEngine::defaultSheetWidthMm/HeightMm/ResolutionXDpi/YDpi),
 * sourced from QSettings under Faircode/CTPImposition pending a real CTP
 * Preferences panel, but are user-editable in the dialog (Plate Properties)
 * and persisted back to the same QSettings keys.
 *
 * leftFilePath/rightFilePath point at PDF files, not .sla -- see NOTES.md /
 * the imposition redesign: rendering .sla content internally (headless
 * ScribusDoc + PageToPixmap) had font-availability and preview bugs. A
 * follow-up attempt imported each PDF via Scribus's own PdfPlug/pdflib_core
 * pipeline (the code "Import > Get Vector File" uses) but exported a blank,
 * wrong-sized (A4) plate for reasons that resisted diagnosis without a
 * debug-symbol rebuild (this install's Release binary carries no DWARF
 * info). ScImpositionEngine::imposeToPdf() now shells out to a small,
 * independently-tested Python script (scribus/imposition/impose_ctp.py,
 * using pypdf for page placement and reportlab for plate marks) instead --
 * see that function's doc comment.
 */
struct SCRIBUS_API ImpositionSettings
{
	QString leftFilePath;    //!< absolute path to the .pdf the left page comes from
	int leftPageIndex = 0;   //!< 0-based page index within leftFilePath -- only 0 (page 1) is currently supported, see imposeToPdf()
	QString rightFilePath;   //!< absolute path to the .pdf the right page comes from
	int rightPageIndex = 0;  //!< 0-based page index within rightFilePath -- only 0 (page 1) is currently supported, see imposeToPdf()
	//! The newspaper's own page numbers -- e.g. 1 and 12 for a page-1/page-12
	//! spread -- entered by the operator (Page Assignment). Deliberately NOT
	//! derived from leftPageIndex/rightPageIndex: those only index within a
	//! single-page source PDF (always 0) and have no relationship to which
	//! real page that PDF happens to contain. Used for the slug line and the
	//! CTP filename's {LEFT}/{RIGHT} tokens.
	int leftPageNumber = 1;
	int rightPageNumber = 1;
	bool landscape = true;   //!< true: pages sit side by side; false: stacked top/bottom
	double gutterMm = 0.0;
	double marginLeftMm = 0.0;
	double marginRightMm = 0.0;
	double marginTopMm = 0.0;
	double marginBottomMm = 0.0;

	// Elements panel -- each independently toggles one thing drawn/exported on
	// the plate. Defaults match the operator's confirmed layout.
	bool showPrintArea = true;   //!< the two source pages themselves
	bool showRegmarks = true;    //!< registration targets: 4 corners + center top/bottom
	bool showAutoMarks = false;  //!< per-page trim/crop marks at each page's 4 corners
	bool showFurnitures = true;  //!< slug line (publication/date/pages/edition) in the top margin
	bool showColourBar = true;   //!< CMYK ink-patch bar in the bottom margin
	bool showBarcodes = false;   //!< reserved -- no content/symbology specified yet, currently a no-op
	bool showGuidelines = false; //!< non-printing margin guide rectangle; preview only, never exported

	double sheetWidthMm = 700.0;
	double sheetHeightMm = 576.0;
	int resolutionXDpi = 1200;
	int resolutionYDpi = 1200;
	QString plateName;   //!< also doubles as the plate-preset name
	QString mediaType;   //!< free text -- operator's own plate/media vocabulary, no fixed list
	QString hotFolderPath; //!< where Send to CTP copies the 4 renamed TIFF plates; see ScImpositionEngine::sendToCtp()

	//! {PUB} and {ED} tokens in the slug line ("CAL | 01-12-2025 | Page 1-8 | Edition 8").
	QString pubCode;
	QString editionCode;

	//! Each source PDF's own native page size, in mm -- populated by the
	//! dialog via ScImpositionEngine::scanPdfInfo() whenever the folder/file/
	//! page selection changes. The schematic preview widget draws from these
	//! (and everything else in this struct) alone; it never touches a PDF or
	//! ScribusDoc itself.
	double leftPageWidthMm = 0.0;
	double leftPageHeightMm = 0.0;
	bool leftPageValid = false;
	double rightPageWidthMm = 0.0;
	double rightPageHeightMm = 0.0;
	bool rightPageValid = false;
};

namespace ScImpositionEngine
{
	//! Last-used plate width/height/resolution (Faircode/CTPImposition QSettings,
	//! keys plateWidthMm/plateHeightMm/plateResolutionXDpi/plateResolutionYDpi).
	//! Defaults are the shop's confirmed CTP setup: 700 x 576mm @ 1200dpi.
	SCRIBUS_API double defaultSheetWidthMm();
	SCRIBUS_API double defaultSheetHeightMm();
	SCRIBUS_API int defaultResolutionXDpi();
	SCRIBUS_API int defaultResolutionYDpi();

	//! Where each source page lands on the sheet, in sheet-relative mm.
	struct SCRIBUS_API PlacedPage
	{
		double slotX = 0, slotY = 0, slotW = 0, slotH = 0; //!< allotted half of the sheet
		double pageX = 0, pageY = 0, pageW = 0, pageH = 0; //!< page's own native-size rect, flush at the slot origin
		bool overflows = false;                            //!< page's native size exceeds its slot
	};

	//! pageWmm/pageHmm are the left page's own native size (from
	//! ImpositionSettings::leftPageWidthMm/HeightMm, or scanPdfInfo() directly).
	SCRIBUS_API PlacedPage computeLeftPlacement(double pageWmm, double pageHmm, const ImpositionSettings& settings);
	//! leftPageWmm/leftPageHmm are needed too, to know where the left page's
	//! own edge (and therefore the gutter) actually falls.
	SCRIBUS_API PlacedPage computeRightPlacement(double leftPageWmm, double leftPageHmm, double rightPageWmm, double rightPageHmm, const ImpositionSettings& settings);

	/**
	 * Builds one imposed PDF containing both source PDF pages at their native
	 * size, side by side, on a single plate-sized page, with optional plate
	 * marks (registration marks / colour bar / slug line / auto-marks, each
	 * independently gated by its own settings.show* flag).
	 *
	 * Implementation: runs `python3 <shareDir>/imposition/impose_ctp.py`
	 * (installed alongside the app, located via ScPaths::shareDir()) via a
	 * blocking QProcess, passing settings as command-line arguments. The
	 * script uses pypdf to place each source PDF's page 1 at its correct
	 * position (true vector content, embedded fonts carried through as-is --
	 * no rescaling, no rasterization) and reportlab to draw the plate marks,
	 * merging both onto one blank plate-sized page. Independently verified
	 * against real newspaper-page PDFs before this integration -- see
	 * impose_ctp.py's own header comment.
	 *
	 * Only page 1 of each source PDF is supported -- settings.leftPageIndex/
	 * rightPageIndex must be 0, or the call fails rather than silently
	 * imposing the wrong page.
	 *
	 * Fails (returns false, *errorMessage set from the script's stderr) if
	 * python3/pypdf/reportlab aren't available, a source page's native size
	 * doesn't fit its allotted slot (checked before the script ever runs --
	 * pages are placed at native size, never rescaled), or the script itself
	 * fails for any other reason.
	 */
	SCRIBUS_API bool imposeToPdf(const ImpositionSettings& settings,
	                              const QString& outputPdfPath, QString* errorMessage = nullptr);

	//! Page count and native page-1 size (mm) for a PDF file, via `pdfinfo`
	//! (poppler-utils) -- no ScribusDoc, no Scribus PDF import machinery, just
	//! a lightweight metadata query for populating the page combo and the
	//! schematic preview. PdfPageInfo::valid is false (and pageCount 0) if the
	//! file can't be read or `pdfinfo` isn't installed.
	struct SCRIBUS_API PdfPageInfo
	{
		bool valid = false;
		int pageCount = 0;
		double widthMm = 0.0;
		double heightMm = 0.0;
	};
	SCRIBUS_API PdfPageInfo scanPdfInfo(const QString& filePath);

	/**
	 * Converts an already-generated imposed PDF (imposeToPdf()'s output) into
	 * 4 separated 1-bit TIFF plates via Ghostscript's tiffsep1 device at
	 * settings.resolutionXDpi/YDpi, renames them to the shop's CTP convention
	 * -- DDMMYY-{PUB}-{LEFT}-{RIGHT}-{EDITION}-{ink}.TIF, e.g.
	 * "170826-CAL-1-12-8-cyan.TIF" for a plate generated 17 Aug 2026,
	 * publication CAL, left page 1, right page 12, edition 8 -- and copies
	 * them into settings.hotFolderPath.
	 *
	 * Ghostscript's own tiffsep1 output naming is
	 * "<template-with-%d-substituted>(<SeparationName>).tif" (confirmed
	 * against a real run -- not "<template>Cyan.tif" as might be assumed);
	 * this function accounts for that when locating Ghostscript's output
	 * before renaming it, so nothing outside this function needs to know it.
	 *
	 * Returns the 4 final file paths (in the hot folder) in Cyan/Magenta/
	 * Yellow/Black order on success, or an empty list with *errorMessage set
	 * on failure (no hot folder configured, hot folder doesn't exist,
	 * Ghostscript missing/failed, or a plate failed to copy).
	 */
	SCRIBUS_API QStringList sendToCtp(const QString& imposedPdfPath, const ImpositionSettings& settings,
	                                   QString* errorMessage = nullptr);
}

#endif
