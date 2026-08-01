#include "suneercontrolbar.h"
#include <QCompleter>
#include <QAbstractItemView>
#include <QTimer>
#include "undomanager.h"
#include "scribus.h"
#include "appmodes.h"
#include "ui/nodeeditpalette.h"
#include "scribusview.h"
#include "canvas.h"
#include "scribuscore.h"
#include "filewatcher.h"
#include "util/suneeralphawrap.h"
#include "canvasmode_suneercontour.h"
#include "scraction.h"
#include "iconmanager.h"
#include "scribusapp.h"
#include "stylemanager.h"
#include "alignselect.h"
#include "styleselect.h"
#include "ui/widgets/color_button.h"
#include "scpaths.h"
#include "scribusdoc.h"
#include "units.h"
#include "scfonts.h"
#include "prefsmanager.h"
#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_table.h"
#include "pageitem_textframe.h"
#include "selection.h"
#include "styles/paragraphstyle.h"
#include "styles/charstyle.h"
#include "propertywidget_distance.h"
#include "resizeimagedialog.h"
#include <QAction>
#include <QFont>
#include <QDebug>
#include <QFrame>
#include <QComboBox>
#include <QImage>
#include <QPolygon>
#include "fpointarray.h"
#include <QRadioButton>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QWidgetAction>
#include <QPushButton>
#include "colorcombo.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>

static const double MM2PT = 2.8346;
static const double PT2MM = 1.0 / MM2PT;

QDoubleSpinBox* SuneerControlBar::makeSpinBox(double min, double max, int dec, double step, const QString& suffix)
{
	QDoubleSpinBox* sb = new QDoubleSpinBox();
	sb->setRange(min, max);
	sb->setDecimals(dec);
	sb->setSingleStep(step);
	sb->setFixedWidth(72);
	if (!suffix.isEmpty()) sb->setSuffix(suffix);
	return sb;
}

QToolButton* SuneerControlBar::makeButton(const QString& text, const QString& tooltip, bool checkable)
{
	QToolButton* btn = new QToolButton();
	btn->setText(text);
	btn->setToolTip(tooltip);
	btn->setCheckable(checkable);
	btn->setFixedSize(26, 22);
	return btn;
}

QLabel* SuneerControlBar::makeLabel(const QString& text, const QString& tooltip)
{
	QLabel* lbl = new QLabel(text);
	lbl->setToolTip(tooltip.isEmpty() ? text : tooltip);
	return lbl;
}

static QFrame* makeSep()
{
	QFrame* f = new QFrame();
	f->setFrameShape(QFrame::VLine);
	f->setFixedWidth(8);
	f->setStyleSheet("color: #aaa;");
	return f;
}

SuneerControlBar::SuneerControlBar(ScribusMainWindow* parent)
	: QToolBar("Control Bar", parent), m_scmw(parent)
{
	setObjectName("SuneerControlBar");
	setMovable(true);
	setFloatable(true);

	QWidget* container = new QWidget(this);
	QVBoxLayout* vlay = new QVBoxLayout(container);
	vlay->setContentsMargins(2, 1, 2, 1);
	vlay->setSpacing(1);

	// ══════════════════════════════════════════════════════════════
	// ROW 1: Font Selection | Font Size | Character Formatting
	//         | Paragraph Alignment | Indents & Spacing
	// ══════════════════════════════════════════════════════════════
	QHBoxLayout* row1 = new QHBoxLayout();
	row1->setSpacing(3);
	row1->setContentsMargins(0,0,0,0);
	auto addR1 = [&](QWidget* w) { row1->addWidget(w); m_textWidgets << w; };

	// Font Selection
	m_fontCombo = new QComboBox();
	m_fontCombo->setFixedWidth(160);
	m_fontCombo->setEditable(true);
	m_fontCombo->setInsertPolicy(QComboBox::NoInsert);
	m_fontCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
	m_fontCombo->completer()->setFilterMode(Qt::MatchContains);
	m_fontCombo->setToolTip("Font Family");
	addR1(m_fontCombo);

	// Font Size
	m_fontSizeSpin = makeSpinBox(0.1, 3000, 2, 0.3, " pt");
	m_fontSizeSpin->setFixedWidth(80);
	m_fontSizeSpin->setToolTip("Font Size (pt)");
	addR1(m_fontSizeSpin);



	addR1(makeSep());

	// Typography — use existing StyleSelect widget (has popups for outline/shadow)
	m_styleSelect = new StyleSelect(this);
	m_styleSelect->hideCapsButtons();     // remove All Caps / Fake Small Caps from the layout
	m_styleSelect->removeGroupSpacers();  // drop the fixed 12px group gaps → even spacing
	m_styleSelect->setButtonSpacing(3);   // uniform gap matching the toolbar row spacing
	// Detach the Shadow button so it can be placed after the +/- outline-width controls.
	QToolButton* shadowBtn = m_styleSelect->detachShadowButton();
	addR1(m_styleSelect);
	m_textWidgets << m_styleSelect;

	// Add a stroke/shadow colour chooser to the Outline hold-down popup
	if (m_styleSelect->outlinePopup())
	{
		QWidget* ocw = new QWidget(this);
		QHBoxLayout* ocl = new QHBoxLayout(ocw);
		ocl->setContentsMargins(8, 4, 8, 6);
		ocl->setSpacing(6);
		QLabel* ocLbl = new QLabel(tr("Colour:"), ocw);
		ocLbl->setToolTip(tr("Color of text stroke and/or drop shadow, depending which is chosen. If both are chosen, then they share the same color."));
		ocl->addWidget(ocLbl);
		m_outlineStrokeColorCombo = new ColorCombo(false, ocw);
		m_outlineStrokeColorCombo->setMinimumWidth(140);
		m_outlineStrokeColorCombo->setToolTip(tr("Color of text stroke and/or drop shadow, depending which is chosen. If both are chosen, then they share the same color."));
		m_outlineStrokeColorCombo->addItem(CommonStrings::tr_NoneColor);   // default "None" before a doc is loaded
		ocl->addWidget(m_outlineStrokeColorCombo);
		QWidgetAction* oca = new QWidgetAction(this);
		oca->setDefaultWidget(ocw);
		m_styleSelect->outlinePopup()->addAction(oca);
	}

	// [outward checkbox] + vertical [+/-] step buttons — placed in row1 right after the
	// effects group (StyleSelect stays untouched so its Outline "A" button is never clipped).
	{
		QWidget* leadW = new QWidget(this);
		QHBoxLayout* leadL = new QHBoxLayout(leadW);
		leadL->setContentsMargins(4, 0, 4, 0);
		leadL->setSpacing(5);                     // space between checkbox and the +/- stack
		leadL->setAlignment(Qt::AlignVCenter);

		m_outlineOutwardChk = new QCheckBox(leadW);
		m_outlineOutwardChk->setToolTip(tr("Grow the outline stroke outward from the text (default: inward)"));
		leadL->addWidget(m_outlineOutwardChk, 0, Qt::AlignVCenter);

		// Vertical +/- stack — buttons tall enough for the glyph to render clearly
		QWidget* stepW = new QWidget(leadW);
		QVBoxLayout* stepL = new QVBoxLayout(stepW);
		stepL->setContentsMargins(0, 0, 0, 0);
		stepL->setSpacing(1);
		m_outlineIncBtn = new QToolButton(stepW);
		m_outlineIncBtn->setText("+");
		m_outlineIncBtn->setFixedSize(22, 13);
		m_outlineIncBtn->setStyleSheet("QToolButton { padding:0; margin:0; font-size:12px; font-weight:bold; }");
		m_outlineIncBtn->setToolTip(tr("Increase outline stroke width"));
		m_outlineDecBtn = new QToolButton(stepW);
		m_outlineDecBtn->setText("−");   // minus sign
		m_outlineDecBtn->setFixedSize(22, 13);
		m_outlineDecBtn->setStyleSheet("QToolButton { padding:0; margin:0; font-size:12px; font-weight:bold; }");
		m_outlineDecBtn->setToolTip(tr("Decrease outline stroke width"));
		stepL->addWidget(m_outlineIncBtn);
		stepL->addWidget(m_outlineDecBtn);
		leadL->addWidget(stepW, 0, Qt::AlignVCenter);

		addR1(leadW);   // add to row1 after StyleSelect; shows/hides with the text widgets
	}

	// Shadow button, moved to appear immediately after the +/- outline-width controls
	if (shadowBtn)
		addR1(shadowBtn);

	addR1(makeSep());

	// Paragraph Alignment — use existing AlignSelect widget
	m_alignSelect = new AlignSelect(this);
	m_alignSelect->setLabelVisibility(false);
	addR1(m_alignSelect);
	m_textWidgets << m_alignSelect;

	addR1(makeSep());

	// Columns & Gap
	m_columnsIconLbl = new QLabel(this);
	m_columnsIconLbl->setFixedSize(20, 20);
	addR1(m_columnsIconLbl);
	m_columnsSpin = new QSpinBox();
	m_columnsSpin->setRange(1, 100);
	m_columnsSpin->setValue(1);
	m_columnsSpin->setFixedWidth(50);
	m_columnsSpin->setToolTip("Number of Columns");
	addR1(m_columnsSpin);

	m_columnGapCombo = new QComboBox();
	m_columnGapCombo->addItems({"Gap:", "Width:"});
	m_columnGapCombo->setFixedWidth(70);
	addR1(m_columnGapCombo);

	m_columnGapSpin = makeSpinBox(0, 1000, 2, 0.5, " mm");
	m_columnGapSpin->setFixedWidth(85);
	m_columnGapSpin->setToolTip("Column Gap (mm)");
	addR1(m_columnGapSpin);

	addR1(makeSep());

	// Text Color button
	m_textColorBtn = new ColorButton(this);
	m_textColorBtn->setFixedSize(26, 22);
	m_textColorBtn->setToolTip("Text Color");
	m_textColorBtn->setContext(Context::Text);
	m_textColorBtn->setMenuContextType(ColorButton::Floating);
	m_textColorBtn->setColor(CommonStrings::tr_NoneColor);
	addR1(m_textColorBtn);
	m_textWidgets << m_textColorBtn;

	// Background Color button
	m_bgColorBtn = new ColorButton(this);
	m_bgColorBtn->setFixedSize(26, 22);
	m_bgColorBtn->setToolTip("Background Color");
	m_bgColorBtn->setContext(Context::TextBackground);
	m_bgColorBtn->setMenuContextType(ColorButton::Floating);
	m_bgColorBtn->setColor(CommonStrings::tr_NoneColor);
	addR1(m_bgColorBtn);
	m_textWidgets << m_bgColorBtn;

	// Image widgets in row1
	// Image rotation
	m_imgRotIconLbl = new QLabel(this);
	m_imgRotIconLbl->setFixedSize(20, 20);
	row1->addWidget(m_imgRotIconLbl);
	m_imageWidgets << m_imgRotIconLbl;
	m_imgRotSpin = makeSpinBox(0, 359.99, 1, 1, "°");
	m_imgRotSpin->setFixedWidth(65);
	m_imgRotSpin->setToolTip("Image Rotation (°)");
	row1->addWidget(m_imgRotSpin);
	m_imageWidgets << m_imgRotSpin;

	// Flip buttons
	m_imgFlipHBtn = makeButton("", "Flip Horizontal");
	row1->addWidget(m_imgFlipHBtn);
	m_imageWidgets << m_imgFlipHBtn;

	m_imgFlipVBtn = makeButton("", "Flip Vertical");
	row1->addWidget(m_imgFlipVBtn);
	m_imageWidgets << m_imgFlipVBtn;

	// Rotate 90
	m_imgRot90CCWBtn = makeButton("", "Rotate 90° Counter-Clockwise");
	row1->addWidget(m_imgRot90CCWBtn);
	m_imageWidgets << m_imgRot90CCWBtn;

	m_imgRot90CWBtn = makeButton("", "Rotate 90° Clockwise");
	row1->addWidget(m_imgRot90CWBtn);
	m_imageWidgets << m_imgRot90CWBtn;

	// Level buttons — created here, added to row1 later (in m_textWrapWidgets section)
	m_imgToFrontBtn   = makeButton("", "Move to Front");
	m_imgRaiseBtn     = makeButton("", "Move One Level Up");
	m_imgLowerBtn     = makeButton("", "Move One Level Down");
	m_imgToBackBtn    = makeButton("", "Move to Back");

	// Frame fit buttons
	m_imgFitFrameBtn  = makeButton("Fi", "Fit Frame to Image");
	row1->addWidget(m_imgFitFrameBtn);
	m_imageWidgets << m_imgFitFrameBtn;

	m_imgFitImageBtn  = makeButton("IF", "Fit Image to Frame");
	row1->addWidget(m_imgFitImageBtn);
	m_imageWidgets << m_imgFitImageBtn;

	// Crop + Resize section
	// Enable/Disable checkbox
	// old Crop Enable button removed
	// Width spinbox
	m_imgCropW = new QDoubleSpinBox();
	m_imgCropW->setRange(1, 9999);
	m_imgCropW->setSuffix(" mm");
	m_imgCropW->setValue(80);
	m_imgCropW->setFixedWidth(75);
	m_imgCropW->setToolTip("Image Width (mm)");
	row1->addWidget(m_imgCropW);
	m_imageWidgets << m_imgCropW;
	// X label
	QLabel* xLbl = new QLabel("×");
	row1->addWidget(xLbl);
	m_imageWidgets << xLbl;
	// Height spinbox
	m_imgCropH = new QDoubleSpinBox();
	m_imgCropH->setRange(1, 9999);
	m_imgCropH->setSuffix(" mm");
	m_imgCropH->setValue(60);
	m_imgCropH->setFixedWidth(75);
	m_imgCropH->setToolTip("Image Height (mm)");
	row1->addWidget(m_imgCropH);
	m_imageWidgets << m_imgCropH;
	// Apply crop+resize button
	m_imgCropApplyBtn = makeButton("CP", "Crop + Resize to fixed size");
	m_imgCropApplyBtn->setIcon(QIcon("/usr/local/share/scribus/icons/1_7_0/16/crop.png"));
	m_imgCropApplyBtn->setIconSize(QSize(20, 20));
	row1->addWidget(m_imgCropApplyBtn);
	m_imageWidgets << m_imgCropApplyBtn;
	// Resize Image button — resample the source file to what the frame needs
	// in print (Fit @ 240dpi one-click path); writes name_resized.* + relinks.
	QToolButton* imgResizeBtn = makeButton("RS", "Resize Image (reduce file resolution to frame)");
	connect(imgResizeBtn, &QToolButton::clicked, this, [this]{ ResizeImageDialog::openForSelection(m_doc, this); });
	row1->addWidget(imgResizeBtn);
	m_imageWidgets << imgResizeBtn;
	// Background Remove button (rembg)
	m_imgRemoveBgBtn = makeButton("BG", "Remove Background (AI)");
	m_imgRemoveBgBtn->setFixedSize(26, 22);

	row1->addWidget(m_imgRemoveBgBtn);
	m_imageWidgets << m_imgRemoveBgBtn;

	// Draw Contour button
	m_imgDrawContourBtn = makeButton("DC", "Draw Contour (Freehand)");
	m_imgDrawContourBtn->setFixedSize(26, 22);

	row1->addWidget(m_imgDrawContourBtn);
	m_imageWidgets << m_imgDrawContourBtn;

	// Active Contour Line Editing Mode button
	m_imgContourEditBtn = makeButton("CL", "Active Contour Line Editing Mode");
	m_imgContourEditBtn->setFixedSize(26, 22);
	m_imgContourEditBtn->setCheckable(true);
	
	row1->addWidget(m_imgContourEditBtn);
	m_imageWidgets << m_imgContourEditBtn;

	// Embed dropdown button
	// ✅ AC button — CL-ന് ശേഷം, Embed-ന് മുൻപ്
	m_autoContourBtn = makeButton("AC", "Auto-detect Image Contour");
	m_autoContourBtn->setFixedSize(26, 22);
	
	m_autoContourBtn->setToolTip("Auto-detect image shape and set contour line for text wrap");
	row1->addWidget(m_autoContourBtn);
	m_imageWidgets << m_autoContourBtn;

	m_imgEmbedBtn = new QToolButton(this);
	m_imgEmbedBtn->setText("📦");
	m_imgEmbedBtn->setFixedSize(26, 22);
	m_imgEmbedBtn->setToolTip("Image Embed Options");
	m_imgEmbedBtn->setPopupMode(QToolButton::InstantPopup);
	QMenu* embedMenu = new QMenu(m_imgEmbedBtn);
	embedMenu->addAction("Embed in SLA (Base64)", this, [this]{ onEmbedInSLA(); });
	embedMenu->addAction("Embed Link",            this, [this]{ onEmbedLink(); });
	embedMenu->addAction("Collect to Folder",     this, [this]{ onCollectToFolder(); });
	m_imgEmbedBtn->setMenu(embedMenu);
	row1->addWidget(m_imgEmbedBtn);
	m_imageWidgets << m_imgEmbedBtn;

	// ✅ Image Size display
	QWidget* sizeSep = makeSep();
	row1->addWidget(sizeSep); m_imageWidgets << sizeSep;

	// Width: external label + framed spinbox (label outside keeps the box uncluttered)
	{ auto* wl = makeLabel("W:", "Image Width"); row1->addWidget(wl); m_imageWidgets << wl; }
	m_imgWidthSpin = new QDoubleSpinBox();
	m_imgWidthSpin->setRange(0, 10000);
	m_imgWidthSpin->setDecimals(2);
	m_imgWidthSpin->setSuffix(" mm");
	m_imgWidthSpin->setFixedWidth(95);
	m_imgWidthSpin->setToolTip("Image Width");
	row1->addWidget(m_imgWidthSpin);
	m_imageWidgets << m_imgWidthSpin;

	// wide gap between W and H so they read as clearly separate fields
	QWidget* hwSpacer = new QWidget(this);
	hwSpacer->setFixedWidth(16);
	row1->addWidget(hwSpacer);
	m_imageWidgets << hwSpacer;

	// Height: external label + framed spinbox
	{ auto* hl = makeLabel("H:", "Image Height"); row1->addWidget(hl); m_imageWidgets << hl; }
	m_imgHeightSpin = new QDoubleSpinBox();
	m_imgHeightSpin->setRange(0, 10000);
	m_imgHeightSpin->setDecimals(2);
	m_imgHeightSpin->setSuffix(" mm");
	m_imgHeightSpin->setFixedWidth(95);
	m_imgHeightSpin->setToolTip("Image Height");
	row1->addWidget(m_imgHeightSpin);
	m_imageWidgets << m_imgHeightSpin;

	// Effective print DPI of the selected image; typing a value + Enter
	// resamples the file to it at the current frame size (self-contained
	// widget — see ImageDpiField).
	ImageDpiField* imgDpiField = new ImageDpiField(this);
	row1->addWidget(imgDpiField);
	m_imageWidgets << imgDpiField;

	// wide gap before the Fill section
	QWidget* hwEndSpacer = new QWidget(this);
	hwEndSpacer->setFixedWidth(16);
	row1->addWidget(hwEndSpacer);
	m_imageWidgets << hwEndSpacer;

	// (Image Fill/Line/Line Width/Line Style moved to image row 2 — see below)

	m_txtLineColorBtn = new ColorButton(this);
	m_txtLineColorBtn->setFixedSize(26,22);
	m_txtLineColorBtn->setToolTip("Line Color");
	m_txtLineColorBtn->setContext(Context::Line);
	m_txtLineColorBtn->setMenuContextType(ColorButton::Floating);
	m_txtLineColorBtn->setColor(CommonStrings::tr_NoneColor);
	row1->addWidget(m_txtLineColorBtn); m_textWidgets << m_txtLineColorBtn;
	m_fillColorBtn = new ColorButton(this);
	m_fillColorBtn->setFixedSize(26,22);
	m_fillColorBtn->setToolTip("Text Frame Fill Color");
	m_fillColorBtn->setContext(Context::Fill);
	m_fillColorBtn->setMenuContextType(ColorButton::Floating);
	m_fillColorBtn->setColor(CommonStrings::tr_NoneColor);
	row1->addWidget(m_fillColorBtn); m_textWidgets << m_fillColorBtn;
	{ auto* sep = makeSep(); row1->addWidget(sep); m_textWidgets << sep; }

	// Line Width thickness field (label removed to save space)
	m_textLineWidthSpin = makeSpinBox(0, 300, 2, 0.1, " pt");
	m_textLineWidthSpin->setFixedWidth(75);
	m_textLineWidthSpin->setToolTip(tr("Thickness of line"));
	row1->addWidget(m_textLineWidthSpin);
	m_textWidgets << m_textLineWidthSpin;

	{ auto* sep2 = makeSep(); row1->addWidget(sep2); m_textWidgets << sep2; }

	// Text Frame Border Style
	m_textLineStyleCombo = new QComboBox();
	m_textLineStyleCombo->setFixedWidth(110);
	m_textLineStyleCombo->addItem("───────", (int)Qt::SolidLine);
	m_textLineStyleCombo->addItem("- - - - -", (int)Qt::DashLine);
	m_textLineStyleCombo->addItem("· · · · ·", (int)Qt::DotLine);
	m_textLineStyleCombo->addItem("-·-·-", (int)Qt::DashDotLine);
	m_textLineStyleCombo->addItem("-··-··-", (int)Qt::DashDotDotLine);
	row1->addWidget(m_textLineStyleCombo);
	m_textWidgets << m_textLineStyleCombo;

	{ auto* sep3 = makeSep(); row1->addWidget(sep3); m_textWidgets << sep3; }

	// (Text Wrap + Layer order + L/R/T/B distances moved to image row 2 — see below)
#if 0
	// ── Text Wrap buttons — row1, right side (visible for Text Frames AND Image Frames) ──
	{
		auto* wrapSep = makeSep();
		row1->addWidget(wrapSep);
		m_textWrapWidgets << wrapSep;
	}
	m_textFlowNoneBtn    = makeButton("", "No Text Flow",           true);
	m_textFlowShapeBtn   = makeButton("", "Text Flow Around Shape", true);
	m_textFlowBBoxBtn    = makeButton("", "Text Flow Around Box",   true);
	m_textFlowContourBtn = makeButton("", "Text Flow Contour Line", true);
	m_textFlowClipBtn    = makeButton("", "Text Flow Image Clip",   true);
	m_textWrapBtn = new QToolButton(this);
	m_textWrapBtn->setText("Wrap");
	m_textWrapBtn->setToolTip("Text Wrap Options");
	m_textWrapBtn->setFixedSize(46, 22);
	row1->addWidget(m_textFlowNoneBtn);    m_textWrapWidgets << m_textFlowNoneBtn;
	row1->addWidget(m_textFlowShapeBtn);   m_textWrapWidgets << m_textFlowShapeBtn;
	row1->addWidget(m_textFlowBBoxBtn);    m_textWrapWidgets << m_textFlowBBoxBtn;
	row1->addWidget(m_textFlowContourBtn); m_textWrapWidgets << m_textFlowContourBtn;
	row1->addWidget(m_textFlowClipBtn);    m_textWrapWidgets << m_textFlowClipBtn;
	row1->addWidget(m_textWrapBtn);        m_textWrapWidgets << m_textWrapBtn;

	// Layer order buttons — shown for both Text Frames and Image Frames
	{
		auto* layerSep = makeSep();
		row1->addWidget(layerSep);
		m_textWrapWidgets << layerSep;
	}
	row1->addWidget(m_imgToFrontBtn);    m_textWrapWidgets << m_imgToFrontBtn;
	row1->addWidget(m_imgRaiseBtn);      m_textWrapWidgets << m_imgRaiseBtn;
	row1->addWidget(m_imgLowerBtn);      m_textWrapWidgets << m_imgLowerBtn;
	row1->addWidget(m_imgToBackBtn);     m_textWrapWidgets << m_imgToBackBtn;

#endif

	// ── Text Wrap flow + Layer order buttons at the END of row 1 (shared text/image) ──
	{
		auto* wrapSep = makeSep();
		row1->addWidget(wrapSep); m_textWrapWidgets << wrapSep;
	}
	m_textFlowNoneBtn    = makeButton("", "No Text Flow",           true);
	m_textFlowShapeBtn   = makeButton("", "Text Flow Around Shape", true);
	m_textFlowBBoxBtn    = makeButton("", "Text Flow Around Box",   true);
	m_textFlowContourBtn = makeButton("", "Text Flow Contour Line", true);
	m_textFlowClipBtn    = makeButton("", "Text Flow Image Clip",   true);
	m_textWrapBtn = new QToolButton(this);
	m_textWrapBtn->setText("Wrap");
	m_textWrapBtn->setToolTip("Text Wrap Options");
	m_textWrapBtn->setFixedSize(46, 22);
	row1->addWidget(m_textFlowNoneBtn);    m_textWrapWidgets << m_textFlowNoneBtn;
	row1->addWidget(m_textFlowShapeBtn);   m_textWrapWidgets << m_textFlowShapeBtn;
	row1->addWidget(m_textFlowBBoxBtn);    m_textWrapWidgets << m_textFlowBBoxBtn;
	row1->addWidget(m_textFlowContourBtn); m_textWrapWidgets << m_textFlowContourBtn;
	row1->addWidget(m_textFlowClipBtn);    m_textWrapWidgets << m_textFlowClipBtn;
	row1->addWidget(m_textWrapBtn);        m_textWrapWidgets << m_textWrapBtn;
	{
		auto* layerSep = makeSep();
		row1->addWidget(layerSep); m_textWrapWidgets << layerSep;
	}
	row1->addWidget(m_imgToFrontBtn);    m_textWrapWidgets << m_imgToFrontBtn;
	row1->addWidget(m_imgRaiseBtn);      m_textWrapWidgets << m_imgRaiseBtn;
	row1->addWidget(m_imgLowerBtn);      m_textWrapWidgets << m_imgLowerBtn;
	row1->addWidget(m_imgToBackBtn);     m_textWrapWidgets << m_imgToBackBtn;

	row1->addStretch();
	vlay->addLayout(row1);



	// ══════════════════════════════════════════════════════════════
	// ROW 2: Font Style | Line Spacing | Kerning | Tracking
	//         | Hyphenation | Paragraph Settings
	// ══════════════════════════════════════════════════════════════
	QHBoxLayout* row2 = new QHBoxLayout();
	row2->setSpacing(3);
	row2->setContentsMargins(0,0,0,0);
	auto addR2 = [&](QWidget* w) { row2->addWidget(w); m_textWidgets << w; };

	// Font Style
	m_styleCombo = new QComboBox();
	m_styleCombo->setFixedWidth(160);
	m_styleCombo->setToolTip("Font Style");
	m_styleCombo->addItems({"Regular", "Bold", "Italic", "Bold Italic"});
	addR2(m_styleCombo);


	// Line Spacing

	m_lineSpSpin = makeSpinBox(0.1, 3000, 2, 0.3, " pt");
	m_lineSpSpin->setFixedWidth(80);
	m_lineSpSpin->setToolTip("Line Spacing (pt)");
	addR2(m_lineSpSpin);

	m_lineSpModeCombo = new QComboBox();
	m_lineSpModeCombo->setFixedWidth(80);
	m_lineSpModeCombo->setToolTip("Line Spacing Mode");
	m_lineSpModeCombo->addItems({"Fixed", "Auto", "Baseline"});
	addR2(m_lineSpModeCombo);
	addR2(makeSep());

	// Tracking
	m_trackingIconLbl = new QLabel(this);
	m_trackingIconLbl->setFixedSize(22, 22);
	addR2(m_trackingIconLbl);
	m_trackingSpin = makeSpinBox(-300, 300, 1, 1, "");
	m_trackingSpin->setFixedWidth(65);
	m_trackingSpin->setToolTip("Tracking / Character Spacing");
	addR2(m_trackingSpin);

	addR2(makeSep());

	// Baseline
	m_baselineIconLbl = new QLabel(this);
	m_baselineIconLbl->setFixedSize(22, 22);
	addR2(m_baselineIconLbl);
	m_baselineSpin = makeSpinBox(-300, 300, 1, 1, "");
	m_baselineSpin->setFixedWidth(65);
	m_baselineSpin->setToolTip("Baseline Offset");
	addR2(m_baselineSpin);

	addR2(makeSep());

	// Scale H
	m_scaleHIconLbl = new QLabel(this);
	m_scaleHIconLbl->setFixedSize(22, 22);
	addR2(m_scaleHIconLbl);
	m_scaleHSpin = makeSpinBox(10, 400, 1, 1, " %");
	m_scaleHSpin->setFixedWidth(70);
	m_scaleHSpin->setToolTip("Horizontal Scale (%)");
	addR2(m_scaleHSpin);

	// Scale V
	m_scaleVIconLbl = new QLabel(this);
	m_scaleVIconLbl->setFixedSize(22, 22);
	addR2(m_scaleVIconLbl);
	m_scaleVSpin = makeSpinBox(10, 400, 1, 1, " %");
	m_scaleVSpin->setFixedWidth(70);
	m_scaleVSpin->setToolTip("Vertical Scale (%)");
	addR2(m_scaleVSpin);













	addR2(makeSep());

	// First Line Indent with icon (Left/Right Indent removed to save toolbar space)
	m_firstLineIndentIconLbl = new QLabel(this);
	m_firstLineIndentIconLbl->setFixedSize(20, 20);
	addR2(m_firstLineIndentIconLbl);
	m_firstLineIndentSpin = makeSpinBox(-1000, 1000, 2, 0.5, " mm");
	m_firstLineIndentSpin->setFixedWidth(80);
	m_firstLineIndentSpin->setToolTip("First Line Indent (mm)");
	addR2(m_firstLineIndentSpin);

	// Space Above/Below Paragraph (ParagraphStyle gapBefore/gapAfter) — pt, matches Properties Palette Distances
	m_gapBeforeIconLbl = new QLabel(this);
	m_gapBeforeIconLbl->setFixedSize(20, 20);
	addR2(m_gapBeforeIconLbl);
	m_gapBeforeSpin = makeSpinBox(0, 300, 2, 0.5, " pt");
	m_gapBeforeSpin->setFixedWidth(75);
	m_gapBeforeSpin->setToolTip(tr("Space Above Paragraph"));
	addR2(m_gapBeforeSpin);

	m_gapAfterIconLbl = new QLabel(this);
	m_gapAfterIconLbl->setFixedSize(20, 20);
	addR2(m_gapAfterIconLbl);
	m_gapAfterSpin = makeSpinBox(0, 300, 2, 0.5, " pt");
	m_gapAfterSpin->setFixedWidth(75);
	m_gapAfterSpin->setToolTip(tr("Space Below Paragraph"));
	addR2(m_gapAfterSpin);

	// Text Edge Feather button
	// Fe button in text row2
	// Text Fe button — separate from image Fe
	m_textFeatherBtn = makeButton("Fe", "Edge Feather");
	m_textFeatherBtn->setFixedSize(28, 22);
	m_textFeatherBtn->setToolTip("Apply edge feather to text frame");
	row2->addWidget(m_textFeatherBtn);
	m_textWidgets << m_textFeatherBtn;
	// Text Frame Box button — opens a popup to choose border sides + text inset
	m_textBoxBtn = makeButton("☐", "Frame Border");
	m_textBoxBtn->setFixedSize(34, 22);
	m_textBoxBtn->setToolTip("Apply border to chosen sides + text inset");
	{
		QMenu* borderMenu = new QMenu(this);
		QWidget* bw = new QWidget(this);
		QVBoxLayout* bl = new QVBoxLayout(bw);
		bl->setContentsMargins(10, 8, 10, 8);
		bl->setSpacing(5);
		bl->addWidget(new QLabel(tr("<b>Frame Border sides</b>"), bw));
		m_borderTopChk    = new QCheckBox(tr("Top"), bw);    m_borderTopChk->setChecked(true);
		m_borderBottomChk = new QCheckBox(tr("Bottom"), bw); m_borderBottomChk->setChecked(true);
		m_borderLeftChk   = new QCheckBox(tr("Left"), bw);   m_borderLeftChk->setChecked(true);
		m_borderRightChk  = new QCheckBox(tr("Right"), bw);  m_borderRightChk->setChecked(true);
		bl->addWidget(m_borderTopChk);
		bl->addWidget(m_borderBottomChk);
		bl->addWidget(m_borderLeftChk);
		bl->addWidget(m_borderRightChk);
		{
			QHBoxLayout* il = new QHBoxLayout();
			il->addWidget(new QLabel(tr("Text inset:"), bw));
			m_borderInsetSpin = makeSpinBox(0, 50, 2, 0.5, " mm");
			m_borderInsetSpin->setValue(2.0);
			il->addWidget(m_borderInsetSpin);
			bl->addLayout(il);
		}
		QPushButton* applyBtn = new QPushButton(tr("Apply"), bw);
		connect(applyBtn, &QPushButton::clicked, this, [this, borderMenu]{ onTextFrameBox(); borderMenu->close(); });
		bl->addWidget(applyBtn);
		QWidgetAction* wa = new QWidgetAction(this);
		wa->setDefaultWidget(bw);
		borderMenu->addAction(wa);
		m_textBoxBtn->setMenu(borderMenu);
		m_textBoxBtn->setPopupMode(QToolButton::InstantPopup);
	}
	row2->addWidget(m_textBoxBtn);
	m_textWidgets << m_textBoxBtn;

	// ── Text-frame Gap controls (mirrors image frame pad buttons) ──
	{ auto* s = makeSep(); row2->addWidget(s); m_textWidgets << s; }
	{ auto* lbl = makeLabel("Gap:", "Text Frame Inner Distance"); row2->addWidget(lbl); m_textWidgets << lbl; }

	// Session-wide mode toggle for the pad buttons below (default: external wrap).
	m_internalPadChk = new QCheckBox(tr("Internal"), this);
	m_internalPadChk->setToolTip(tr("When checked, padding buttons adjust internal "
	                                "text distance instead of external wrap boundary"));
	m_internalPadChk->setChecked(false);
	row2->addWidget(m_internalPadChk); m_textWidgets << m_internalPadChk;

	m_textPadAllPlusBtn = makeButton("+", "Increase Gap (All Sides)");
	m_textPadAllPlusBtn->setFixedSize(32, 26);
	row2->addWidget(m_textPadAllPlusBtn); m_textWidgets << m_textPadAllPlusBtn;

	m_textPadAllMinusBtn = makeButton("-", "Decrease Gap (All Sides)");
	m_textPadAllMinusBtn->setFixedSize(32, 26);
	row2->addWidget(m_textPadAllMinusBtn); m_textWidgets << m_textPadAllMinusBtn;

	m_textPadResetBtn = makeButton("↺", "Reset Gap to Zero");
	m_textPadResetBtn->setFixedSize(28, 26);
	m_textPadResetBtn->setStyleSheet("QToolButton { color: #666; font-size: 13px; }");
	row2->addWidget(m_textPadResetBtn); m_textWidgets << m_textPadResetBtn;

	m_textPadTopPlusBtn = makeButton("↑+", "Increase Top Gap");
	m_textPadTopPlusBtn->setFixedSize(36, 28);
	m_textPadTopPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	row2->addWidget(m_textPadTopPlusBtn); m_textWidgets << m_textPadTopPlusBtn;

	m_textPadTopMinusBtn = makeButton("↑-", "Decrease Top Gap");
	m_textPadTopMinusBtn->setFixedSize(36, 28);
	m_textPadTopMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	row2->addWidget(m_textPadTopMinusBtn); m_textWidgets << m_textPadTopMinusBtn;

	m_textPadBottomPlusBtn = makeButton("↓+", "Increase Bottom Gap");
	m_textPadBottomPlusBtn->setFixedSize(36, 28);
	m_textPadBottomPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	row2->addWidget(m_textPadBottomPlusBtn); m_textWidgets << m_textPadBottomPlusBtn;

	m_textPadBottomMinusBtn = makeButton("↓-", "Decrease Bottom Gap");
	m_textPadBottomMinusBtn->setFixedSize(36, 28);
	m_textPadBottomMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	row2->addWidget(m_textPadBottomMinusBtn); m_textWidgets << m_textPadBottomMinusBtn;

	m_textPadLeftPlusBtn = makeButton("←+", "Increase Left Gap");
	m_textPadLeftPlusBtn->setFixedSize(36, 28);
	m_textPadLeftPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	row2->addWidget(m_textPadLeftPlusBtn); m_textWidgets << m_textPadLeftPlusBtn;

	m_textPadLeftMinusBtn = makeButton("←-", "Decrease Left Gap");
	m_textPadLeftMinusBtn->setFixedSize(36, 28);
	m_textPadLeftMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	row2->addWidget(m_textPadLeftMinusBtn); m_textWidgets << m_textPadLeftMinusBtn;

	m_textPadRightPlusBtn = makeButton("→+", "Increase Right Gap");
	m_textPadRightPlusBtn->setFixedSize(36, 28);
	m_textPadRightPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	row2->addWidget(m_textPadRightPlusBtn); m_textWidgets << m_textPadRightPlusBtn;

	m_textPadRightMinusBtn = makeButton("→-", "Decrease Right Gap");
	m_textPadRightMinusBtn->setFixedSize(36, 28);
	m_textPadRightMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	row2->addWidget(m_textPadRightMinusBtn); m_textWidgets << m_textPadRightMinusBtn;

	vlay->addLayout(row2);

	// ══ SECOND ROW (shared): image shape/flow + wrap/layer/distance ══
	// Everything below is appended to the SAME row2 as the text controls, so
	// every frame type shows exactly two rows. Per-widget visibility groups
	// (m_imageWidgets / m_textWrapWidgets) decide what is shown in each mode.
	QHBoxLayout* imgRow2b = row2;
	auto addIR2 = [&](QWidget* w) { imgRow2b->addWidget(w); m_imageWidgets << w; };

	// ── Moved from row1 (decongestion): image Fill/Line/Line Width/Line Style ──
	{
		QWidget* clrSep = makeSep();
		imgRow2b->addWidget(clrSep); m_imageWidgets << clrSep;

		QLabel* fillLbl = new QLabel("Fill", this);
		fillLbl->setToolTip("Fill Color");
		imgRow2b->addWidget(fillLbl); m_imageWidgets << fillLbl;

		m_imgFillColorBtn = new ColorButton(this);
		m_imgFillColorBtn->setFixedSize(26,22);
		m_imgFillColorBtn->setToolTip("Fill Color");
		m_imgFillColorBtn->setContext(Context::Fill);
		m_imgFillColorBtn->setMenuContextType(ColorButton::Floating);
		m_imgFillColorBtn->setColor(CommonStrings::tr_NoneColor);
		imgRow2b->addWidget(m_imgFillColorBtn); m_imageWidgets << m_imgFillColorBtn;

		m_fillOpacitySpin = makeSpinBox(0,100,1,1," %");
		m_fillOpacitySpin->setFixedWidth(60); m_fillOpacitySpin->setValue(100);
		m_fillOpacitySpin->setToolTip("Fill Opacity (%)");
		imgRow2b->addWidget(m_fillOpacitySpin); m_imageWidgets << m_fillOpacitySpin;

		QWidget* clrSep2 = makeSep();
		imgRow2b->addWidget(clrSep2); m_imageWidgets << clrSep2;

		QLabel* lineLbl = new QLabel("Line", this);
		lineLbl->setToolTip("Line Color");
		imgRow2b->addWidget(lineLbl); m_imageWidgets << lineLbl;

		m_imgLineColorBtn = new ColorButton(this);
		m_imgLineColorBtn->setFixedSize(26,22);
		m_imgLineColorBtn->setToolTip("Line Color");
		m_imgLineColorBtn->setContext(Context::Line);
		m_imgLineColorBtn->setMenuContextType(ColorButton::Floating);
		m_imgLineColorBtn->setColor(CommonStrings::tr_NoneColor);
		imgRow2b->addWidget(m_imgLineColorBtn); m_imageWidgets << m_imgLineColorBtn;

		m_imgLineOpacitySpin = makeSpinBox(0,100,1,1," %");
		m_imgLineOpacitySpin->setFixedWidth(60); m_imgLineOpacitySpin->setValue(100);
		m_imgLineOpacitySpin->setToolTip("Line Opacity (%)");
		imgRow2b->addWidget(m_imgLineOpacitySpin); m_imageWidgets << m_imgLineOpacitySpin;

		{ auto* lbl = makeLabel(tr("Line Width"), tr("Thickness of line"));
		  imgRow2b->addWidget(lbl); m_imageWidgets << lbl; }
		m_imgLineWidthSpin = makeSpinBox(0, 300, 2, 0.1, " pt");
		m_imgLineWidthSpin->setFixedWidth(75);
		m_imgLineWidthSpin->setToolTip(tr("Thickness of line"));
		imgRow2b->addWidget(m_imgLineWidthSpin); m_imageWidgets << m_imgLineWidthSpin;

		QWidget* sep2 = makeSep();
		imgRow2b->addWidget(sep2); m_imageWidgets << sep2;

		m_imgLineStyleCombo = new QComboBox(this);
		m_imgLineStyleCombo->setFixedWidth(120);
		m_imgLineStyleCombo->setToolTip("Line Style of current object");
		m_imgLineStyleCombo->addItem("No Style");
		m_imgLineStyleCombo->setVisible(false);
		imgRow2b->addWidget(m_imgLineStyleCombo); m_imageWidgets << m_imgLineStyleCombo;

		m_imgLineStyleEditBtn = makeButton("✎", "Edit current selected style");
		m_imgLineStyleEditBtn->setFixedSize(26, 26);
		imgRow2b->addWidget(m_imgLineStyleEditBtn); m_imageWidgets << m_imgLineStyleEditBtn;

		m_imgLineStyleAddBtn = makeButton("+", "Add new line style");
		m_imgLineStyleAddBtn->setFixedSize(26, 26);
		imgRow2b->addWidget(m_imgLineStyleAddBtn); m_imageWidgets << m_imgLineStyleAddBtn;
	}

	// (Text Wrap + Layer order buttons + L/R/T/B distances all moved to the END of row 1
	//  — see row 1 construction)

	// Text Wrap popup panel  (buttons are created in row1 above)
	m_textWrapPopup = new QWidget(nullptr, Qt::Popup);
	m_textWrapPopup->setWindowTitle("Text Wrap");
	QVBoxLayout* wrapLay = new QVBoxLayout(m_textWrapPopup);
	wrapLay->setContentsMargins(8, 8, 8, 8);
	wrapLay->setSpacing(6);
	wrapLay->addWidget(new QLabel("<b>Text Wrap</b>"));
	m_wrapNoneRadio     = new QRadioButton("None",              m_textWrapPopup);
	m_wrapShapeRadio    = new QRadioButton("Wrap Around Shape", m_textWrapPopup);
	m_wrapBBoxRadio     = new QRadioButton("Bounding Box",      m_textWrapPopup);
	m_wrapContourRadio  = new QRadioButton("Contour Line",      m_textWrapPopup);
	m_wrapClipRadio     = new QRadioButton("Image Clip Path",   m_textWrapPopup);
	wrapLay->addWidget(m_wrapNoneRadio);
	wrapLay->addWidget(m_wrapShapeRadio);
	wrapLay->addWidget(m_wrapBBoxRadio);
	wrapLay->addWidget(m_wrapContourRadio);
	wrapLay->addWidget(m_wrapClipRadio);
	m_wrapNoneRadio->setChecked(true);
	m_textWrapPopup->adjustSize();


	// Image-Text distance +/- buttons
	auto* padLbl = makeLabel("Gap:", "Image to Text Distance");
	addIR2(padLbl);

	m_padAllPlusBtn = makeButton("+", "Increase Gap (All Sides)");
	m_padAllPlusBtn->setFixedSize(32, 26);
	addIR2(m_padAllPlusBtn);

	m_padAllMinusBtn = makeButton("-", "Decrease Gap (All Sides)");
	m_padAllMinusBtn->setFixedSize(32, 26);
	addIR2(m_padAllMinusBtn);

	m_padResetBtn = makeButton("↺", "Reset Gap to Zero");
	m_padResetBtn->setFixedSize(28, 26);
	m_padResetBtn->setStyleSheet("QToolButton { color: #666; font-size: 13px; }");
	addIR2(m_padResetBtn);


	m_padTopPlusBtn    = makeButton("↑+", "Increase Top Gap");
	m_padTopPlusBtn->setFixedSize(36, 28);
	m_padTopPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	addIR2(m_padTopPlusBtn);
	m_padTopMinusBtn   = makeButton("↑-", "Decrease Top Gap");
	m_padTopMinusBtn->setFixedSize(36, 28);
	m_padTopMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	addIR2(m_padTopMinusBtn);

	m_padBottomPlusBtn = makeButton("↓+", "Increase Bottom Gap");
	m_padBottomPlusBtn->setFixedSize(36, 28);
	m_padBottomPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	addIR2(m_padBottomPlusBtn);
	m_padBottomMinusBtn = makeButton("↓-", "Decrease Bottom Gap");
	m_padBottomMinusBtn->setFixedSize(36, 28);
	m_padBottomMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	addIR2(m_padBottomMinusBtn);

	m_padLeftPlusBtn   = makeButton("←+", "Increase Left Gap");
	m_padLeftPlusBtn->setFixedSize(36, 28);
	m_padLeftPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	addIR2(m_padLeftPlusBtn);
	m_padLeftMinusBtn  = makeButton("←-", "Decrease Left Gap");
	m_padLeftMinusBtn->setFixedSize(36, 28);
	m_padLeftMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	addIR2(m_padLeftMinusBtn);

	m_padRightPlusBtn  = makeButton("→+", "Increase Right Gap");
	m_padRightPlusBtn->setFixedSize(36, 28);
	m_padRightPlusBtn->setStyleSheet("QToolButton { color: #1565C0; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #0D47A1; }");
	addIR2(m_padRightPlusBtn);
	m_padRightMinusBtn = makeButton("→-", "Decrease Right Gap");
	m_padRightMinusBtn->setFixedSize(36, 28);
	m_padRightMinusBtn->setStyleSheet("QToolButton { color: #E65100; font-size: 13px; font-weight: bold; } QToolButton:hover { color: #BF360C; }");
	addIR2(m_padRightMinusBtn);
	m_autoFitChk = new QCheckBox("Auto-Fit", this);
	m_autoFitChk->setToolTip("Auto Fit Frame Height to content");
	m_autoFitChk->setFixedHeight(22);
	addIR2(m_autoFitChk);
	addIR2(makeSep());
	QLabel* cornerLbl = new QLabel("⌐:", this);
	cornerLbl->setToolTip("Corner Radius");
	cornerLbl->setFixedSize(18, 22);
	addIR2(cornerLbl);
	m_cornerRadiusSpin = new QDoubleSpinBox(this);
	m_cornerRadiusSpin->setRange(0, 500);
	m_cornerRadiusSpin->setDecimals(2);
	m_cornerRadiusSpin->setSuffix(" mm");
	m_cornerRadiusSpin->setFixedWidth(80);
	m_cornerRadiusSpin->setToolTip("Corner Radius");
	addIR2(m_cornerRadiusSpin);
	// Corner Options popup button
	m_cornerOptionsBtn = makeButton("⌐▾", "Corner Options per corner");
	m_cornerOptionsBtn->setFixedSize(32, 22);
	m_cornerOptionsBtn->setToolTip("Individual corner radius options");
	addIR2(m_cornerOptionsBtn);
	// Corner Options popup
	m_cornerPopup = new QWidget(this, Qt::Popup);
	m_cornerPopup->setWindowTitle("Corner Options");
	QVBoxLayout* cornerVlay = new QVBoxLayout(m_cornerPopup);
	cornerVlay->setContentsMargins(10, 10, 10, 10);
	cornerVlay->setSpacing(6);
	cornerVlay->addWidget(new QLabel("<b>Corner Size:</b>"));
	QGridLayout* cornerGrid = new QGridLayout();
	cornerGrid->setSpacing(4);
	auto makeCornerSpin = [&]() {
		QDoubleSpinBox* s = new QDoubleSpinBox(m_cornerPopup);
		s->setRange(0, 500); s->setDecimals(2);
		s->setSuffix(" mm"); s->setFixedWidth(90);
		return s;
	};
	m_cornerTLSpin = makeCornerSpin();
	m_cornerTRSpin = makeCornerSpin();
	m_cornerBLSpin = makeCornerSpin();
	m_cornerBRSpin = makeCornerSpin();
	m_cornerTLSpin->setToolTip("Top Left");
	m_cornerTRSpin->setToolTip("Top Right");
	m_cornerBLSpin->setToolTip("Bottom Left");
	m_cornerBRSpin->setToolTip("Bottom Right");
	cornerGrid->addWidget(new QLabel("TL:"), 0, 0);
	cornerGrid->addWidget(m_cornerTLSpin,    0, 1);
	cornerGrid->addWidget(new QLabel("TR:"), 0, 2);
	cornerGrid->addWidget(m_cornerTRSpin,    0, 3);
	cornerGrid->addWidget(new QLabel("BL:"), 1, 0);
	cornerGrid->addWidget(m_cornerBLSpin,    1, 1);
	cornerGrid->addWidget(new QLabel("BR:"), 1, 2);
	cornerGrid->addWidget(m_cornerBRSpin,    1, 3);
	m_cornerLinkChk = new QCheckBox("Link all corners", m_cornerPopup);
	m_cornerLinkChk->setChecked(true);
	cornerVlay->addLayout(cornerGrid);
	cornerVlay->addWidget(m_cornerLinkChk);


	// Auto contour from image shape



	addIR2(makeSep());
	// Feather button + popup
	m_featherBtn = makeButton("🌫", "Edge Feather");
	m_featherBtn->setToolTip("Apply edge feather/blur to selected item");
	addIR2(m_featherBtn);
	// Feather popup
	m_featherPopup = new QWidget(this, Qt::Popup);
	m_featherPopup->setWindowTitle("Edge Feather");
	QVBoxLayout* featherVlay = new QVBoxLayout(m_featherPopup);
	featherVlay->setContentsMargins(10,10,10,10);
	featherVlay->setSpacing(6);
	featherVlay->addWidget(new QLabel("<b>Edge Feather</b>"));
	// Direction buttons
	QHBoxLayout* featherDirLay = new QHBoxLayout();
	auto makeFBtn = [&](const QString& t, const QString& tip) {
		QToolButton* b = new QToolButton(m_featherPopup);
		b->setText(t); b->setToolTip(tip);
		b->setCheckable(true); b->setFixedSize(36,26);
		return b;
	};
	m_featherAllBtn    = makeFBtn("All",  "All sides");
	m_featherTopBtn    = makeFBtn("Top",  "Top edge only");
	m_featherBottomBtn = makeFBtn("Bot",  "Bottom edge only");
	m_featherLeftBtn   = makeFBtn("Left", "Left edge only");
	m_featherRightBtn  = makeFBtn("Rgt",  "Right edge only");
	m_featherAllBtn->setChecked(true);
	featherDirLay->addWidget(m_featherAllBtn);
	featherDirLay->addWidget(m_featherTopBtn);
	featherDirLay->addWidget(m_featherBottomBtn);
	featherDirLay->addWidget(m_featherLeftBtn);
	featherDirLay->addWidget(m_featherRightBtn);
	featherVlay->addLayout(featherDirLay);
	// Feather amount
	featherVlay->addWidget(new QLabel("Amount (mm):"));
	m_featherSpin = new QDoubleSpinBox(m_featherPopup);
	m_featherSpin->setRange(0, 50);
	m_featherSpin->setDecimals(2);
	m_featherSpin->setSuffix(" mm");
	m_featherSpin->setFixedWidth(100);
	featherVlay->addWidget(m_featherSpin);
	// Apply button
	QPushButton* featherApplyBtn = new QPushButton("Apply", m_featherPopup);
	featherVlay->addWidget(featherApplyBtn);
	connect(featherApplyBtn, &QPushButton::clicked, this, &SuneerControlBar::applyFeather);
	row2->addStretch();


	// ═══════════════════════════════════════════════════════════
	// LINE ROW 1: Color | Width | Style | Cap | Join | Opacity
	// ═══════════════════════════════════════════════════════════
	QHBoxLayout* lineRow1 = new QHBoxLayout();
	lineRow1->setSpacing(3);
	lineRow1->setContentsMargins(0,0,0,0);
	auto addLR1 = [&](QWidget* w) { lineRow1->addWidget(w); m_lineWidgets << w; };

	m_lineColorBtn = new ColorButton(this);
	m_lineColorBtn->setFixedSize(26, 22);
	m_lineColorBtn->setToolTip("Line Color");
	m_lineColorBtn->setContext(Context::Line);
	m_lineColorBtn->setMenuContextType(ColorButton::Floating);
	m_lineColorBtn->setColor(CommonStrings::tr_NoneColor);
		addLR1(m_lineColorBtn);

	// Mask / Opacity
	m_lineMaskBtn = new ColorButton(this);
	m_lineMaskBtn->setFixedSize(26, 22);
	m_lineMaskBtn->setToolTip("Mask of Line / Opacity");
	m_lineMaskBtn->setContext(Context::LineMask);
	m_lineMaskBtn->setMenuContextType(ColorButton::Floating);
	m_lineMaskBtn->setColor(CommonStrings::tr_NoneColor);
		addLR1(m_lineMaskBtn);
	addLR1(makeSep());

	// Named Line Style combo
	m_lineNamedStyleCombo = new QComboBox();
	m_lineNamedStyleCombo->setFixedWidth(120);
	m_lineNamedStyleCombo->setToolTip("Named Line Style");
	addLR1(m_lineNamedStyleCombo);

	// Edit style button
	m_lineStyleEditBtn = new QToolButton();
	m_lineStyleEditBtn->setIcon(QIcon());
	m_lineStyleEditBtn->setText("✎");
	m_lineStyleEditBtn->setFixedSize(26, 22);
	m_lineStyleEditBtn->setToolTip("Edit Line Style");
	addLR1(m_lineStyleEditBtn);

	// New style button
	m_lineStyleNewBtn = new QToolButton();
	m_lineStyleNewBtn->setText("+");
	m_lineStyleNewBtn->setFixedSize(26, 22);
	m_lineStyleNewBtn->setToolTip("New Line Style");
	addLR1(m_lineStyleNewBtn);
	addLR1(makeSep());

	m_lineWidthIconLbl = new QLabel(this);
	m_lineWidthIconLbl->setFixedSize(20, 20);
	m_lineWidthIconLbl->setToolTip("Line Width");
	addLR1(m_lineWidthIconLbl);
	m_lineWidthSpin = makeSpinBox(0, 300, 2, 0.1, " pt");
	m_lineWidthSpin->setFixedWidth(75);
	m_lineWidthSpin->setToolTip("Line Width (pt)");
	addLR1(m_lineWidthSpin);
	addLR1(makeSep());

	m_lineStyleCombo = new QComboBox();
	m_lineStyleCombo->setFixedWidth(110);
	m_lineStyleCombo->setToolTip("Line Style");
	m_lineStyleCombo->addItem("───────", (int)Qt::SolidLine);
	m_lineStyleCombo->addItem("- - - - -",  (int)Qt::DashLine);
	m_lineStyleCombo->addItem("· · · · ·",  (int)Qt::DotLine);
	m_lineStyleCombo->addItem("-·-·-",    (int)Qt::DashDotLine);
	m_lineStyleCombo->addItem("-··-··-",  (int)Qt::DashDotDotLine);
	addLR1(m_lineStyleCombo);
	addLR1(makeSep());

	addLR1(makeLabel("Cap:"));
	m_lineCapFlatBtn   = makeButton("⊣", "Flat Cap",   true);
	m_lineCapRoundBtn  = makeButton("○", "Round Cap",  true);
	m_lineCapSquareBtn = makeButton("□", "Square Cap", true);
	addLR1(m_lineCapFlatBtn);
	addLR1(m_lineCapRoundBtn);
	addLR1(m_lineCapSquareBtn);
	addLR1(makeSep());

	addLR1(makeLabel("Join:"));
	m_lineJoinMiterBtn = makeButton("⌐", "Miter Join", true);
	m_lineJoinRoundBtn = makeButton("╮", "Round Join", true);
	m_lineJoinBevelBtn = makeButton("╲", "Bevel Join", true);
	addLR1(m_lineJoinMiterBtn);
	addLR1(m_lineJoinRoundBtn);
	addLR1(m_lineJoinBevelBtn);
	addLR1(makeSep());

	addLR1(makeLabel("Opacity:"));
	m_lineOpacitySpin = makeSpinBox(0, 100, 1, 1, " %");
	m_lineOpacitySpin->setFixedWidth(65);
	m_lineOpacitySpin->setValue(100);
	m_lineOpacitySpin->setToolTip("Line Opacity (%)");
	addLR1(m_lineOpacitySpin);

	lineRow1->addStretch();
	vlay->addLayout(lineRow1);

	// ═══════════════════════════════════════════════════════════
	// LINE ROW 2: Start Arrow | End Arrow | Dash Offset
	// ═══════════════════════════════════════════════════════════
	QHBoxLayout* lineRow2 = new QHBoxLayout();
	lineRow2->setSpacing(3);
	lineRow2->setContentsMargins(0,0,0,0);
	auto addLR2 = [&](QWidget* w) { lineRow2->addWidget(w); m_lineWidgets << w; };

	addLR2(makeLabel("→ Start:"));
	m_lineStartArrowCombo = new QComboBox();
	m_lineStartArrowCombo->setFixedWidth(120);
	m_lineStartArrowCombo->setToolTip("Start Arrow");
	m_lineStartArrowCombo->addItem("None");
	addLR2(m_lineStartArrowCombo);
	addLR2(makeSep());

	addLR2(makeLabel("→ End:"));
	m_lineEndArrowCombo = new QComboBox();
	m_lineEndArrowCombo->setFixedWidth(120);
	m_lineEndArrowCombo->setToolTip("End Arrow");
	m_lineEndArrowCombo->addItem("None");
	addLR2(m_lineEndArrowCombo);
	addLR2(makeSep());

	addLR2(makeLabel("Dash Offset:"));
	m_lineDashOffsetSpin = makeSpinBox(0, 100, 2, 0.5, " pt");
	m_lineDashOffsetSpin->setFixedWidth(75);
	m_lineDashOffsetSpin->setToolTip("Dash Offset (pt)");
	addLR2(m_lineDashOffsetSpin);

	lineRow2->addStretch();
	vlay->addLayout(lineRow2);

	addWidget(container);
	showTextWidgets(false);
	showImageWidgets(false);
	showLineWidgets(false);
	showTextWrapWidgets(false);

	// Load icons immediately
	iconSetChange();

	// ── Connections ───────────────────────────────────────────────
	connect(m_fontCombo->lineEdit(), &QLineEdit::returnPressed, this, [this]() { onFontChanged(QFont()); });
	connect(m_fontCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
		// Order-independent with the popup's Hide: if the preview is somehow still live,
		// roll it back first so the committed undo step starts from the original font.
		if (m_fontPreviewActive) { restoreFontPreview(); endFontPreview(); }
		if (m_fontComboPopupShown) { m_fontComboPopupShown = false; onFontChanged(QFont()); }
	});
	connect(m_fontCombo, &QComboBox::customContextMenuRequested, this, [this](const QPoint&) { m_fontComboPopupShown = true; });
	QObject::connect(m_fontCombo->view(), &QAbstractItemView::pressed, this, [this](const QModelIndex&) { m_fontComboPopupShown = true; });

	// ── Live font preview: navigating the dropdown applies the highlighted font to the
	// selected text immediately. highlighted() is arrow-key/hover navigation, activated()
	// is the commit — Qt fires them separately, which is exactly the split we need.
	m_fontPreviewTimer = new QTimer(this);
	m_fontPreviewTimer->setSingleShot(true);
	m_fontPreviewTimer->setInterval(40);
	connect(m_fontPreviewTimer, &QTimer::timeout, this, [this]() {
		if (m_fontPreviewPending.isEmpty() || m_fontPreviewPending == m_fontPreviewApplied)
			return;
		if (!m_fontPreviewActive)
		{
			snapshotFontPreview();
			m_fontPreviewActive = true;
		}
		applyFontPreview(m_fontPreviewPending);
	});
	connect(m_fontCombo, QOverload<int>::of(&QComboBox::highlighted), this, [this](int idx) {
		if (!m_doc || idx < 0 || m_updating)
			return;
		// Enter on a highlighted row must reach the activated() commit path above.
		m_fontComboPopupShown = true;
		m_fontPreviewPending = m_fontCombo->itemText(idx);
		// Debounce: each apply is a full relayout + canvas repaint, so a held-down arrow
		// key must not queue one per font.
		m_fontPreviewTimer->start();
	});
	m_fontCombo->view()->installEventFilter(this);
	connect(m_styleCombo,       QOverload<int>::of(&QComboBox::currentIndexChanged),   this, &SuneerControlBar::onStyleChanged);
	connect(m_fontSizeSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),  this, &SuneerControlBar::onFontSizeChanged);
	connect(m_trackingSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onTrackingChanged);
	connect(m_baselineSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onBaselineChanged);
	connect(m_scaleHSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onScaleHChanged);
	connect(m_scaleVSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onScaleVChanged);
	connect(m_lineSpSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),  this, &SuneerControlBar::onLineSpacingChanged);
	connect(m_lineSpModeCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),   this, &SuneerControlBar::onLineSpModeChanged);

	connect(m_padResetBtn,      &QToolButton::clicked, this, &SuneerControlBar::onPadReset);
	connect(m_padAllPlusBtn,    &QToolButton::clicked, this, [this]{ onPadAllChanged(+0.5); });
	connect(m_padAllMinusBtn,   &QToolButton::clicked, this, [this]{ onPadAllChanged(-0.5); });
	connect(m_padTopPlusBtn,    &QToolButton::clicked, this, [this]{ onPadSideChanged(0, +0.5); });
	connect(m_padTopMinusBtn,   &QToolButton::clicked, this, [this]{ onPadSideChanged(0, -0.5); });
	connect(m_padBottomPlusBtn, &QToolButton::clicked, this, [this]{ onPadSideChanged(1, +0.5); });
	connect(m_padBottomMinusBtn,&QToolButton::clicked, this, [this]{ onPadSideChanged(1, -0.5); });
	connect(m_padLeftPlusBtn,   &QToolButton::clicked, this, [this]{ onPadSideChanged(2, +0.5); });
	connect(m_padLeftMinusBtn,  &QToolButton::clicked, this, [this]{ onPadSideChanged(2, -0.5); });
	connect(m_padRightPlusBtn,  &QToolButton::clicked, this, [this]{ onPadSideChanged(3, +0.5); });
	connect(m_padRightMinusBtn, &QToolButton::clicked, this, [this]{ onPadSideChanged(3, -0.5); });

	// Text-frame gap buttons
	connect(m_textPadResetBtn,      &QToolButton::clicked, this, &SuneerControlBar::onTextPadReset);
	connect(m_textPadAllPlusBtn,    &QToolButton::clicked, this, [this]{ onTextPadAllChanged(+0.5); });
	connect(m_textPadAllMinusBtn,   &QToolButton::clicked, this, [this]{ onTextPadAllChanged(-0.5); });
	connect(m_textPadTopPlusBtn,    &QToolButton::clicked, this, [this]{ onTextPadSideChanged(0, +0.5); });
	connect(m_textPadTopMinusBtn,   &QToolButton::clicked, this, [this]{ onTextPadSideChanged(0, -0.5); });
	connect(m_textPadBottomPlusBtn, &QToolButton::clicked, this, [this]{ onTextPadSideChanged(1, +0.5); });
	connect(m_textPadBottomMinusBtn,&QToolButton::clicked, this, [this]{ onTextPadSideChanged(1, -0.5); });
	connect(m_textPadLeftPlusBtn,   &QToolButton::clicked, this, [this]{ onTextPadSideChanged(2, +0.5); });
	connect(m_textPadLeftMinusBtn,  &QToolButton::clicked, this, [this]{ onTextPadSideChanged(2, -0.5); });
	connect(m_textPadRightPlusBtn,  &QToolButton::clicked, this, [this]{ onTextPadSideChanged(3, +0.5); });
	connect(m_textPadRightMinusBtn, &QToolButton::clicked, this, [this]{ onTextPadSideChanged(3, -0.5); });

	connect(m_imgRotSpin,        QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onImgRotChanged);
	connect(m_autoContourBtn,   &QToolButton::clicked, this, &SuneerControlBar::onAutoContour);
	// Corner options popup open
	connect(m_cornerOptionsBtn, &QToolButton::clicked, this, [this](){
		QPoint pos = m_cornerOptionsBtn->mapToGlobal(QPoint(0, m_cornerOptionsBtn->height()));
		m_cornerPopup->move(pos);
		m_cornerPopup->show();
	});
	// Link all corners — TL change → others follow
	connect(m_cornerTLSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
		if (m_cornerLinkChk->isChecked()) {
			m_cornerTRSpin->blockSignals(true);
			m_cornerBLSpin->blockSignals(true);
			m_cornerBRSpin->blockSignals(true);
			m_cornerTRSpin->setValue(val);
			m_cornerBLSpin->setValue(val);
			m_cornerBRSpin->setValue(val);
			m_cornerTRSpin->blockSignals(false);
			m_cornerBLSpin->blockSignals(false);
			m_cornerBRSpin->blockSignals(false);
		}
		applyCornerRadius();
	});
	auto applyCorner = [this](double){ applyCornerRadius(); };
	connect(m_cornerTRSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCorner);
	connect(m_cornerBLSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCorner);
	connect(m_cornerBRSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCorner);
	connect(m_textFeatherBtn, &QToolButton::clicked, this, &SuneerControlBar::onTextEdgeFeather);
	connect(m_featherBtn, &QToolButton::clicked, this, [this](){
		QPoint pos = m_featherBtn->mapToGlobal(QPoint(0, m_featherBtn->height()));
		m_featherPopup->move(pos);
		m_featherPopup->show();
	});
	connect(m_cornerRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
		ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
		if (!doc) return;
		double unitRatio = doc->unitRatio();
		for (int i = 0; i < doc->m_Selection->count(); ++i)
		{
			PageItem* item = doc->m_Selection->itemAt(i);
			if (item)
				item->setCornerRadius(val / unitRatio);
		}
		doc->setFrameRounded();
		doc->changed();
		doc->changedPagePreview();
		doc->regionsChanged()->update(QRect());
		if (doc->m_Selection->count() > 0)
			doc->m_Selection->itemAt(0)->update();
	});
	connect(m_autoFitChk, &QCheckBox::toggled, this, [this](bool checked){
		ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
		if (!doc) return;
		for (int i = 0; i < doc->m_Selection->count(); ++i)
		{
			PageItem* item = doc->m_Selection->itemAt(i);
			if (item && item->isImageFrame())
			{
				// checked=true → AutoFit ON (scale=false)
				// checked=false → Manual scale ON (scale=true)
				item->setImageScalingMode(!checked, true);
				item->update();
			}
		}
		doc->regionsChanged()->update(QRectF());
		doc->changed();
	});
	connect(m_imgWidthSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onImgWidthChanged);
	connect(m_imgHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onImgHeightChanged);
	connect(m_imgLineStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onImgLineStyleChanged);
	connect(m_imgLineStyleEditBtn, &QToolButton::clicked, this, &SuneerControlBar::onImgLineStyleEdit);
	connect(m_imgLineStyleAddBtn,  &QToolButton::clicked, this, &SuneerControlBar::onImgLineStyleAdd);
	connect(m_imgFillColorBtn,  &ColorButton::changed,  this, &SuneerControlBar::onFillColorChanged);
	connect(m_imgLineColorBtn,  &ColorButton::changed,  this, &SuneerControlBar::onImgLineColorChanged);
	connect(m_fillOpacitySpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onFillOpacityChanged);
	connect(m_imgLineOpacitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onLineOpacityChanged);
	connect(m_textWrapBtn,        &QToolButton::clicked, this, &SuneerControlBar::onTextWrapShow);
	connect(m_wrapNoneRadio,      &QRadioButton::clicked, this, &SuneerControlBar::onTextFlowNone);
	connect(m_wrapShapeRadio,     &QRadioButton::clicked, this, &SuneerControlBar::onTextFlowShape);
	connect(m_wrapBBoxRadio,      &QRadioButton::clicked, this, &SuneerControlBar::onTextFlowBBox);
	connect(m_wrapContourRadio,   &QRadioButton::clicked, this, &SuneerControlBar::onTextFlowContour);
	connect(m_wrapClipRadio,      &QRadioButton::clicked, this, &SuneerControlBar::onTextFlowClip);
	connect(m_textFlowNoneBtn,    &QToolButton::clicked, this, &SuneerControlBar::onTextFlowNone);
	connect(m_textFlowShapeBtn,   &QToolButton::clicked, this, &SuneerControlBar::onTextFlowShape);
	connect(m_textFlowBBoxBtn,    &QToolButton::clicked, this, &SuneerControlBar::onTextFlowBBox);
	connect(m_textFlowContourBtn, &QToolButton::clicked, this, &SuneerControlBar::onTextFlowContour);
	connect(m_textFlowClipBtn,    &QToolButton::clicked, this, &SuneerControlBar::onTextFlowClip);
	connect(m_imgRot90CCWBtn,   &QToolButton::clicked, this, &SuneerControlBar::onImgRot90CCW);
	connect(m_imgRot90CWBtn,    &QToolButton::clicked, this, &SuneerControlBar::onImgRot90CW);
	connect(m_imgToFrontBtn,    &QToolButton::clicked, this, &SuneerControlBar::onImgToFront);
	connect(m_imgRaiseBtn,      &QToolButton::clicked, this, &SuneerControlBar::onImgRaise);
	connect(m_imgLowerBtn,      &QToolButton::clicked, this, &SuneerControlBar::onImgLower);
	connect(m_imgToBackBtn,     &QToolButton::clicked, this, &SuneerControlBar::onImgToBack);
	connect(m_imgFlipHBtn,      &QToolButton::clicked, this, &SuneerControlBar::onImgFlipH);
	connect(m_imgFlipVBtn,      &QToolButton::clicked, this, &SuneerControlBar::onImgFlipV);
	connect(m_imgFitFrameBtn,   &QToolButton::clicked, this, &SuneerControlBar::onImgFitFrame);
	connect(m_imgFitImageBtn,   &QToolButton::clicked, this, &SuneerControlBar::onImgFitImage);
	connect(m_imgCropApplyBtn,  &QToolButton::clicked, this, &SuneerControlBar::onImgCropApply);
	connect(m_imgRemoveBgBtn,    &QToolButton::clicked, this, &SuneerControlBar::onImgRemoveBackground);
	connect(m_textFeatherBtn, &QToolButton::clicked, this, &SuneerControlBar::onTextEdgeFeather);
	connect(m_featherBtn, &QToolButton::clicked, this, [this](){
		if (!m_doc || m_doc->m_Selection->isEmpty()) return;
		PageItem* item = m_doc->m_Selection->itemAt(0);
		if (item->isTextFrame())
			onTextEdgeFeather();
		else if (item->isImageFrame())
			onImgEdgeFeather();
	});
	connect(m_imgDrawContourBtn, &QToolButton::clicked, this, &SuneerControlBar::onImgDrawContour);
	connect(m_imgContourEditBtn, &QToolButton::toggled, this, &SuneerControlBar::onImgContourEditToggle);

	// ── Line connections ─────────────────────────────────────────
	connect(m_lineWidthSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onLineWidthChanged);
	connect(m_lineStyleCombo,      QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onLineStyleChanged);

	connect(m_textLineWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onLineWidthChanged);
	connect(m_imgLineWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onLineWidthChanged);

	connect(m_textLineStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onLineStyleChanged);
	connect(m_lineOpacitySpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onLineOpacityChanged);
	connect(m_lineDashOffsetSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &SuneerControlBar::onLineDashOffsetChanged);
	connect(m_lineStartArrowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onLineStartArrowChanged);
	connect(m_lineEndArrowCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onLineEndArrowChanged);
	connect(m_lineCapFlatBtn,   &QToolButton::clicked, this, [this]{ onLineCapChanged(Qt::FlatCap);   });
	connect(m_lineCapRoundBtn,  &QToolButton::clicked, this, [this]{ onLineCapChanged(Qt::RoundCap);  });
	connect(m_lineCapSquareBtn, &QToolButton::clicked, this, [this]{ onLineCapChanged(Qt::SquareCap); });
	connect(m_lineJoinMiterBtn, &QToolButton::clicked, this, [this]{ onLineJoinChanged(Qt::MiterJoin); });
	connect(m_lineJoinRoundBtn, &QToolButton::clicked, this, [this]{ onLineJoinChanged(Qt::RoundJoin); });
	connect(m_lineJoinBevelBtn, &QToolButton::clicked, this, [this]{ onLineJoinChanged(Qt::BevelJoin); });
	connect(m_styleSelect, &StyleSelect::State, this, &SuneerControlBar::onStyleEffectChanged);
	if (m_styleSelect->OutlineVal && m_styleSelect->OutlineVal->LWidth)
		connect(m_styleSelect->OutlineVal->LWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, &SuneerControlBar::onOutlineWidthChanged);
	if (m_outlineStrokeColorCombo)
		connect(m_outlineStrokeColorCombo, QOverload<int>::of(&QComboBox::activated),
			this, &SuneerControlBar::onOutlineStrokeColorChanged);
	if (m_outlineIncBtn)
		connect(m_outlineIncBtn, &QToolButton::clicked, this, &SuneerControlBar::onOutlineStepUp);
	if (m_outlineDecBtn)
		connect(m_outlineDecBtn, &QToolButton::clicked, this, &SuneerControlBar::onOutlineStepDown);
	if (m_outlineOutwardChk)
		connect(m_outlineOutwardChk, &QCheckBox::toggled, this, &SuneerControlBar::onOutlineOutwardToggled);
	connect(m_textColorBtn,     &ColorButton::changed,  this, &SuneerControlBar::onTextColorChanged);
	connect(m_bgColorBtn,       &ColorButton::changed,  this, &SuneerControlBar::onBgColorChanged);
	connect(m_lineColorBtn,     &ColorButton::changed,  this, &SuneerControlBar::onLineColorChanged);
	connect(m_lineMaskBtn,        &ColorButton::changed,    this, &SuneerControlBar::onLineMaskChanged);
	connect(m_txtLineColorBtn, &ColorButton::changed, this, &SuneerControlBar::onLineColorChanged);
	connect(m_fillColorBtn, &ColorButton::changed, this, &SuneerControlBar::onFillColorChanged);
	connect(m_lineNamedStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SuneerControlBar::onLineNamedStyleChanged);
	connect(m_lineStyleEditBtn,   &QToolButton::clicked,    this, &SuneerControlBar::onLineStyleEditClicked);
	connect(m_lineStyleNewBtn,    &QToolButton::clicked,    this, &SuneerControlBar::onLineStyleNewClicked);
	connect(m_padResetBtn,      &QToolButton::clicked, this, &SuneerControlBar::onPadReset);
	connect(m_padAllPlusBtn,    &QToolButton::clicked, this, [this]{ onPadAllChanged(+0.5); });
	connect(m_padAllMinusBtn,   &QToolButton::clicked, this, [this]{ onPadAllChanged(-0.5); });
	connect(m_columnsSpin,      QOverload<int>::of(&QSpinBox::valueChanged),           this, &SuneerControlBar::onColumnsChanged);
	connect(m_columnGapSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),  this, &SuneerControlBar::onColumnGapChanged);
	connect(m_columnGapCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),   this, &SuneerControlBar::onColumnGapModeChanged);
	connect(m_firstLineIndentSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onFirstLineIndentChanged);
	connect(m_gapBeforeSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onGapBeforeChanged);
	connect(m_gapAfterSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerControlBar::onGapAfterChanged);
	connect(m_alignSelect,      &AlignSelect::State,                                   this, &SuneerControlBar::onAlignChanged);
	connect(ScQApp, SIGNAL(iconSetChanged()), this, SLOT(iconSetChange()));
}

void SuneerControlBar::setDocument(ScribusDoc* doc)
{
	m_doc = doc;
	// Load Scribus fonts into combo
	m_fontCombo->blockSignals(true);
	m_fontCombo->clear();
	QStringList fonts = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts.keys();
	fonts.sort();
	for (const QString& f : fonts)
		m_fontCombo->addItem(f);
	m_fontCombo->blockSignals(false);
	if (doc) {
		m_textColorBtn->setDoc(doc);
		m_bgColorBtn->setDoc(doc);
		if (m_fillColorBtn)
			m_fillColorBtn->setDoc(doc);
		m_lineColorBtn->setDoc(doc);
		m_lineMaskBtn->setDoc(doc);
		if (m_txtLineColorBtn) m_txtLineColorBtn->setDoc(doc);
		if (m_txtLineMaskBtn) m_txtLineMaskBtn->setDoc(doc);
		if (m_outlineStrokeColorCombo)
		{
			m_outlineStrokeColorCombo->blockSignals(true);
			m_outlineStrokeColorCombo->setColors(doc->PageColors, true);
			m_outlineStrokeColorCombo->blockSignals(false);
		}
		// Set default colors on document load
		if (m_txtLineColorBtn) { m_txtLineColorBtn->setColor(doc->itemToolPrefs().lineColor, doc->itemToolPrefs().lineColorShade); m_txtLineColorBtn->update(); }
		if (m_txtLineMaskBtn) { m_txtLineMaskBtn->setColor(doc->itemToolPrefs().lineColor, doc->itemToolPrefs().lineColorShade); m_txtLineMaskBtn->update(); }
		// Populate named line styles
		m_lineNamedStyleCombo->blockSignals(true);
		m_lineNamedStyleCombo->clear();
		m_lineNamedStyleCombo->addItem("No Style");
		if (doc) {
			for (const auto& s : doc->lineStyles().keys())
				m_lineNamedStyleCombo->addItem(s);
		}
		m_lineNamedStyleCombo->blockSignals(false);

		connect(doc->m_Selection, SIGNAL(selectionChanged()),
				this, SLOT(updateFromSelection()),
				Qt::UniqueConnection);

		// Line Width spinboxes follow the document unit
		applyLineWidthUnit();

		// ✅ Caption auto-update: 200ms polling timer
		if (!m_captionTimer) {
			m_captionTimer = new QTimer(this);
			m_captionTimer->setInterval(50);
			connect(m_captionTimer, &QTimer::timeout,
				this, &SuneerControlBar::onDocChangedForCaption);
		}
		m_captionTimer->start();
		// ✅ docChanged → caption auto update
		connect(doc, &ScribusDoc::docChanged,
			this, &SuneerControlBar::onDocChangedForCaption);
	}
}

void SuneerControlBar::showTextWidgets(bool show)
{
	if (show)
		showLineWidgets(false);

	for (QWidget* w : m_textWidgets)
	{
		if (w)
			w->setVisible(show);
	}
}

void SuneerControlBar::showLineWidgets(bool show)
{
	for (QWidget* w : m_lineWidgets)
		w->setVisible(show);
}

void SuneerControlBar::showImageWidgets(bool show)
{
	if (show) showLineWidgets(false);
	for (QWidget* w : m_imageWidgets) w->setVisible(show);
}

void SuneerControlBar::showTextWrapWidgets(bool show)
{
	for (QWidget* w : m_textWrapWidgets)
		w->setVisible(show);
}

void SuneerControlBar::updateTextWrapControls(PageItem* item)
{
	if (!item) return;
	const PageItem::TextFlowMode mode = item->textFlowMode();
	m_textFlowNoneBtn->setChecked(    mode == PageItem::TextFlowDisabled);
	m_textFlowShapeBtn->setChecked(   mode == PageItem::TextFlowUsesFrameShape);
	m_textFlowBBoxBtn->setChecked(    mode == PageItem::TextFlowUsesBoundingBox);
	m_textFlowContourBtn->setChecked( mode == PageItem::TextFlowUsesContourLine);
	m_textFlowClipBtn->setChecked(    mode == PageItem::TextFlowUsesImageClipping);
	// Sync the popup radios too
	m_wrapNoneRadio->setChecked(    mode == PageItem::TextFlowDisabled);
	m_wrapShapeRadio->setChecked(   mode == PageItem::TextFlowUsesFrameShape);
	m_wrapBBoxRadio->setChecked(    mode == PageItem::TextFlowUsesBoundingBox);
	m_wrapContourRadio->setChecked( mode == PageItem::TextFlowUsesContourLine);
	m_wrapClipRadio->setChecked(    mode == PageItem::TextFlowUsesImageClipping);
	// "Image Clip Path" only applies to image frames
	m_textFlowClipBtn->setEnabled(item->isImageFrame());
	m_wrapClipRadio->setEnabled(item->isImageFrame());
}

void SuneerControlBar::blockAllSignals(bool block)
{
	const QList<QObject*> all = {
		m_fontCombo, m_styleCombo, m_fontSizeSpin,
		m_styleSelect,
		m_columnsSpin, m_columnGapSpin, m_columnGapCombo,
		m_firstLineIndentSpin,
		m_gapBeforeSpin, m_gapAfterSpin,
		m_trackingSpin, m_baselineSpin, m_scaleHSpin, m_scaleVSpin,
		m_lineSpSpin, m_lineSpModeCombo,
		 m_imgRotSpin
	};
	for (QObject* o : all) o->blockSignals(block);
}

void SuneerControlBar::updateFromSelection()
{
	// Set icons once

	if (!m_doc) return;
	if (m_doc->m_Selection->isEmpty())
	{
		showTextWidgets(false);
		showImageWidgets(false);
		showTextWrapWidgets(false);
		return;
	}

	m_updating = true;
	blockAllSignals(true);

	PageItem* item = m_doc->m_Selection->itemAt(0);

	// Table cell active → treat as text frame
	PageItem* textItem = item;
	if (item->isTable()) {
		PageItem_Table* tbl = item->asTable();
		if (tbl && tbl->activeCell().textFrame())
			textItem = tbl->activeCell().textFrame();
	}

	if (textItem->isTextFrame())
	{
		showTextWidgets(true);
		showImageWidgets(false);

		m_textLineWidthSpin->setValue(item->lineWidth() * m_doc->unitRatio());

		int textStyleIdx =
			m_textLineStyleCombo->findData((int)item->lineStyle());

		if (textStyleIdx >= 0)
			m_textLineStyleCombo->setCurrentIndex(textStyleIdx);

		const CharStyle& cs = textItem->currentCharStyle();
		const ParagraphStyle& ps = textItem->currentStyle();

		// Font family — from effective char style (same as Text Properties)
		QString fontName = cs.font().scName();
		if (fontName.isEmpty())
			fontName = ps.charStyle().font().scName();
		m_fontCombo->setCurrentText(fontName);

		// Font size
		m_fontSizeSpin->setValue(cs.fontSize() / 10.0);

		// Font style
		QString style = "Regular";
		if (fontName.contains("Bold Italic", Qt::CaseInsensitive)) style = "Bold Italic";
		else if (fontName.contains("Bold", Qt::CaseInsensitive)) style = "Bold";
		else if (fontName.contains("Italic", Qt::CaseInsensitive)) style = "Italic";
		m_styleCombo->setCurrentText(style);

		// Character formatting via StyleSelect
		m_styleSelect->setStyle(cs.effects());
		// Outline stroke width popup + stroke/shadow colour
		if (m_styleSelect->OutlineVal && m_styleSelect->OutlineVal->LWidth)
		{
			m_styleSelect->OutlineVal->LWidth->blockSignals(true);
			m_styleSelect->OutlineVal->LWidth->setValue(cs.outlineWidth() / 10.0);
			m_styleSelect->OutlineVal->LWidth->blockSignals(false);
		}
		if (m_outlineStrokeColorCombo)
		{
			m_outlineStrokeColorCombo->blockSignals(true);
			m_outlineStrokeColorCombo->setColors(m_doc->PageColors, true);
			m_outlineStrokeColorCombo->setCurrentColor(cs.strokeColor());
			m_outlineStrokeColorCombo->blockSignals(false);
		}
		if (m_outlineOutwardChk)
		{
			m_outlineOutwardChk->blockSignals(true);
			m_outlineOutwardChk->setChecked(cs.outlineOutward() != 0);
			m_outlineOutwardChk->blockSignals(false);
		}

		// Alignment
		m_alignSelect->setStyle(ps.alignment(), ps.direction());



		// Colors
		m_textColorBtn->setDoc(m_doc);
		m_textColorBtn->setColor(cs.fillColor(), cs.fillShade());
		m_textColorBtn->updateFloatingContext();
		m_textColorBtn->updatePreview();
		m_textColorBtn->update();

		m_bgColorBtn->setDoc(m_doc);
		m_bgColorBtn->setColor(cs.backColor(), cs.backShade());
		m_bgColorBtn->updateFloatingContext();
		m_bgColorBtn->updatePreview();
		m_bgColorBtn->update();

		// Text Frame Fill Color
		if (m_fillColorBtn)
		{
			m_fillColorBtn->setDoc(m_doc);
			m_fillColorBtn->setType(item->gradientType());
			m_fillColorBtn->setColor(item->fillColor(), item->fillShade());
			m_fillColorBtn->updateFloatingContext();
			m_fillColorBtn->updatePreview();
			m_fillColorBtn->update();
		}

		if (m_fillColorBtn)
		{
			m_fillColorBtn->setDoc(m_doc);
			m_fillColorBtn->setType(item->gradientType());
			m_fillColorBtn->setColor(item->fillColor(), item->fillShade());
			m_fillColorBtn->update();
		}

		if (m_txtLineColorBtn)
		{
			m_txtLineColorBtn->setDoc(m_doc);
			m_txtLineColorBtn->setColor(item->lineColor(), item->lineShade());
			m_txtLineColorBtn->updateFloatingContext();
			m_txtLineColorBtn->updatePreview();
			m_txtLineColorBtn->update();
		}

		// Columns
		if (item->isTextFrame())
		{
			PageItem_TextFrame* tf = item->asTextFrame();
			if (tf) {
				m_columnsSpin->setValue(tf->columns());
				m_columnGapSpin->setValue(tf->columnGap() * PT2MM);
			}
		}

		// Indents
		m_firstLineIndentSpin->setValue(ps.firstIndent() * PT2MM);
		// Paragraph spacing (pt, matches Properties Palette Distances)
		m_gapBeforeSpin->setValue(ps.gapBefore());
		m_gapAfterSpin->setValue(ps.gapAfter());

		// Advanced typography
		m_trackingSpin->setValue(cs.tracking() / 10.0);
		m_baselineSpin->setValue(cs.baselineOffset() / 10.0);
		m_scaleHSpin->setValue(cs.scaleH() / 10.0);
		m_scaleVSpin->setValue(cs.scaleV() / 10.0);

		// Line spacing
		m_lineSpSpin->setValue(ps.lineSpacing());
		m_lineSpModeCombo->setCurrentIndex(ps.lineSpacingMode());

		// Text Wrap — visible and functional for text frames too
		showTextWrapWidgets(true);
		updateTextWrapControls(item);








	}
	else if (item->isImageFrame())
	{
		showTextWidgets(false);
		showImageWidgets(true);
		showTextWrapWidgets(true);
		updateTextWrapControls(item);
		m_imgRotSpin->setValue(fabs(item->imageRotation()));
		// ✅ Show frame size in mm
		const double PT2MM = 1.0 / 2.8346;
		if (m_imgWidthSpin) {
			m_imgWidthSpin->blockSignals(true);
			m_imgWidthSpin->setValue(item->width() * PT2MM);
			m_imgWidthSpin->blockSignals(false);
		}
		if (m_imgHeightSpin) {
			m_imgHeightSpin->blockSignals(true);
			m_imgHeightSpin->setValue(item->height() * PT2MM);
			m_imgHeightSpin->blockSignals(false);
		}
		if (m_imgLineWidthSpin) m_imgLineWidthSpin->setValue(item->lineWidth() * m_doc->unitRatio());
	}
	else if (item->isLine() || item->isPolyLine() || item->isArc() || item->isSpiral())
	{
		showTextWidgets(false);
		showImageWidgets(true);
		showLineWidgets(false);
		showTextWrapWidgets(true);
		updateTextWrapControls(item);
		{
			const double PT2MM_w = 1.0 / 2.8346;
			// Show current frame size in W/H fields
			if (m_imgWidthSpin)  { m_imgWidthSpin->blockSignals(true);  m_imgWidthSpin->setValue(item->width()  * PT2MM_w); m_imgWidthSpin->blockSignals(false); }
			if (m_imgHeightSpin) { m_imgHeightSpin->blockSignals(true); m_imgHeightSpin->setValue(item->height() * PT2MM_w); m_imgHeightSpin->blockSignals(false); }
		}
		if (m_imgFillColorBtn) { m_imgFillColorBtn->setDoc(m_doc); m_imgFillColorBtn->setColor(item->fillColor(), item->fillShade()); m_imgFillColorBtn->update(); }
		if (m_imgLineColorBtn) { m_imgLineColorBtn->setDoc(m_doc); m_imgLineColorBtn->setColor(item->lineColor(), item->lineShade()); m_imgLineColorBtn->update(); }
		if (m_fillOpacitySpin) m_fillOpacitySpin->setValue(qRound((1.0-item->fillTransparency())*100.0));
		if (m_imgLineOpacitySpin) m_imgLineOpacitySpin->setValue(qRound((1.0-item->lineTransparency())*100.0));
		if (m_imgLineStyleCombo) {
			m_imgLineStyleCombo->blockSignals(true);
			// Reload styles list
			m_imgLineStyleCombo->clear();
			m_imgLineStyleCombo->addItem("No Style");
			for (const QString& s : m_doc->lineStyles().keys())
				m_imgLineStyleCombo->addItem(s);
			// Set current
			if (item->NamedLStyle.isEmpty())
				m_imgLineStyleCombo->setCurrentIndex(0);
			else
				m_imgLineStyleCombo->setCurrentText(item->NamedLStyle);
			m_imgLineStyleCombo->blockSignals(false);
		}
		m_updating = true;
		m_lineWidthSpin->setValue(item->lineWidth() * m_doc->unitRatio());
		if (m_imgLineWidthSpin) m_imgLineWidthSpin->setValue(item->lineWidth() * m_doc->unitRatio());
		m_lineOpacitySpin->setValue(qRound((1.0 - item->lineTransparency()) * 100.0));
		if (m_lineColorBtn) {
			m_lineColorBtn->setDoc(m_doc);
			m_lineColorBtn->setColor(item->lineColor(), item->lineShade());
			if (m_txtLineColorBtn) { m_txtLineColorBtn->setDoc(m_doc); m_txtLineColorBtn->update(); }
			if (m_txtLineMaskBtn) { m_txtLineMaskBtn->setDoc(m_doc); m_txtLineMaskBtn->update(); }
			if (m_txtLineColorBtn) { m_txtLineColorBtn->setColor(item->lineColor(), item->lineShade()); m_txtLineColorBtn->update(); }

			if (m_fillColorBtn)
			{
				m_fillColorBtn->setDoc(m_doc);
				m_fillColorBtn->setColor(item->fillColor(), item->fillShade());
				m_fillColorBtn->updatePreview();
				m_fillColorBtn->updateFloatingContext();
				m_fillColorBtn->update();
			}
			m_lineColorBtn->update();
		}
		if (m_lineMaskBtn) {
			m_lineMaskBtn->setDoc(m_doc);
			m_lineMaskBtn->setType(Gradient_None);
			m_lineMaskBtn->setColor(
				"",
				100,
				item->lineTransparency());
			m_lineMaskBtn->updatePreview();
			m_lineMaskBtn->updateFloatingContext();
		}
		m_lineDashOffsetSpin->setValue(item->dashOffset());
		int styleIdx = m_lineStyleCombo->findData((int)item->lineStyle());
		if (styleIdx >= 0) m_lineStyleCombo->setCurrentIndex(styleIdx);
		m_lineCapFlatBtn->setChecked(item->lineEnd()    == Qt::FlatCap);
		m_lineCapRoundBtn->setChecked(item->lineEnd()   == Qt::RoundCap);
		m_lineCapSquareBtn->setChecked(item->lineEnd()  == Qt::SquareCap);
		m_lineJoinMiterBtn->setChecked(item->lineJoin() == Qt::MiterJoin);
		m_lineJoinRoundBtn->setChecked(item->lineJoin() == Qt::RoundJoin);
		m_lineJoinBevelBtn->setChecked(item->lineJoin() == Qt::BevelJoin);
		m_lineStartArrowCombo->setCurrentIndex(item->startArrowIndex());
		m_lineEndArrowCombo->setCurrentIndex(item->endArrowIndex());
		// Named style
		m_lineNamedStyleCombo->blockSignals(true);
		if (item->NamedLStyle.isEmpty())
			m_lineNamedStyleCombo->setCurrentIndex(0);
		else
			m_lineNamedStyleCombo->setCurrentText(item->NamedLStyle);
		m_lineStyleEditBtn->setEnabled(m_lineNamedStyleCombo->currentIndex() != 0);
		m_lineNamedStyleCombo->blockSignals(false);
		m_updating = false;
	}
	else if (item->isPolygon() || item->isRegularPolygon())
	{
		showTextWidgets(false);
		showImageWidgets(true);
		showLineWidgets(false);
		showTextWrapWidgets(true);
		updateTextWrapControls(item);
		{
			const double PT2MM_w = 1.0 / 2.8346;
			// Show current frame size in W/H fields
			if (m_imgWidthSpin)  { m_imgWidthSpin->blockSignals(true);  m_imgWidthSpin->setValue(item->width()  * PT2MM_w); m_imgWidthSpin->blockSignals(false); }
			if (m_imgHeightSpin) { m_imgHeightSpin->blockSignals(true); m_imgHeightSpin->setValue(item->height() * PT2MM_w); m_imgHeightSpin->blockSignals(false); }
		}
		if (m_imgFillColorBtn) { m_imgFillColorBtn->setDoc(m_doc); m_imgFillColorBtn->setColor(item->fillColor(), item->fillShade()); m_imgFillColorBtn->update(); }
		if (m_imgLineColorBtn) { m_imgLineColorBtn->setDoc(m_doc); m_imgLineColorBtn->setColor(item->lineColor(), item->lineShade()); m_imgLineColorBtn->update(); }
		if (m_fillOpacitySpin) m_fillOpacitySpin->setValue(qRound((1.0-item->fillTransparency())*100.0));
		if (m_imgLineOpacitySpin) m_imgLineOpacitySpin->setValue(qRound((1.0-item->lineTransparency())*100.0));
		if (m_imgLineWidthSpin) m_imgLineWidthSpin->setValue(item->lineWidth() * m_doc->unitRatio());
		m_updating = false;
	}
	else
	{
		showTextWidgets(false);
		showImageWidgets(false);
		showLineWidgets(false);
		showTextWrapWidgets(false);
	}

	blockAllSignals(false);
	m_updating = false;
}

// ── Slots ─────────────────────────────────────────────────────

// ── Live font preview ───────────────────────────────────────────────────────────
// Frames the font would land on, mirroring the target list
// ScribusDoc::itemSelection_ApplyCharStyle() builds so tables preview the same way
// they apply.
static QList<PageItem*> suneerPreviewTargetFrames(ScribusDoc* doc)
{
	QList<PageItem*> frames;
	if (!doc)
		return frames;
	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!item)
			continue;
		if (item->isTable() && doc->appMode == modeEditTable)
		{
			PageItem_Table* table = item->asTable();
			if (table->hasSelection())
			{
				const QSet<TableCell> cells = table->selectedCells();
				for (const TableCell& cell : cells)
				{
					if (cell.textFrame())
						frames.append(cell.textFrame());
				}
			}
			else if (table->activeCell().textFrame())
				frames.append(table->activeCell().textFrame());
		}
		else
			frames.append(item);
	}
	return frames;
}

void SuneerControlBar::snapshotFontPreview()
{
	m_fontPreviewFrames.clear();
	if (!m_doc)
		return;
	const QList<PageItem*> frames = suneerPreviewTargetFrames(m_doc);
	for (PageItem* item : frames)
	{
		if (!item || item->itemText.length() <= 0)
			continue;
		// In edit mode only the highlighted range changes; at frame level the whole story does.
		int start = 0;
		int end = item->itemText.length();
		if ((m_doc->appMode == modeEdit || m_doc->appMode == modeEditTable) && item->itemText.hasSelection())
		{
			start = item->itemText.startOfSelection();
			end   = item->itemText.endOfSelection();
		}
		if (end <= start)
			continue;
		FontPreviewFrame snap;
		snap.item = item;
		int runStart = start;
		QString runFont = item->itemText.charStyle(start).font().scName();
		for (int pos = start + 1; pos <= end; ++pos)
		{
			const QString f = (pos < end) ? item->itemText.charStyle(pos).font().scName() : QString();
			if (pos == end || f != runFont)
			{
				snap.runs.append({ runStart, pos - runStart, runFont });
				runStart = pos;
				runFont = f;
			}
		}
		if (!snap.runs.isEmpty())
			m_fontPreviewFrames.append(snap);
	}
}

void SuneerControlBar::applyFontPreview(const QString& fontName)
{
	if (!m_doc || fontName.isEmpty())
		return;
	if (!PrefsManager::instance().appPrefs.fontPrefs.AvailFonts.contains(fontName))
		return;
	// Reuse the normal apply path, only with history switched off: setUndoEnabled() is
	// counter-based, and itemSelection_ApplyCharStyle() only opens a transaction when
	// undo is enabled, so the preview leaves no undo steps behind.
	UndoManager::instance()->setUndoEnabled(false);
	m_doc->itemSelection_SetFont(fontName);
	UndoManager::instance()->setUndoEnabled(true);
	m_fontPreviewApplied = fontName;
}

void SuneerControlBar::restoreFontPreview()
{
	if (!m_doc || m_fontPreviewFrames.isEmpty())
		return;
	SCFonts& availFonts = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts;
	UndoManager::instance()->setUndoEnabled(false);
	for (const FontPreviewFrame& snap : m_fontPreviewFrames)
	{
		PageItem* item = snap.item;
		if (!item)
			continue;
		for (const FontPreviewRun& run : snap.runs)
		{
			if (run.fontName.isEmpty() || !availFonts.contains(run.fontName))
				continue;
			// The story can have been re-laid out under us; clamp rather than trust offsets.
			const int len = qMin(run.length, item->itemText.length() - run.start);
			if (run.start < 0 || len <= 0)
				continue;
			CharStyle cs;
			cs.setFont(availFonts[run.fontName]);
			item->itemText.applyCharStyle(run.start, len, cs);
		}
		item->invalid = true;
		item->invalidateLayout();
	}
	UndoManager::instance()->setUndoEnabled(true);
	m_doc->regionsChanged()->update(QRectF());
	m_fontPreviewApplied.clear();
}

void SuneerControlBar::endFontPreview()
{
	if (m_fontPreviewTimer)
		m_fontPreviewTimer->stop();
	m_fontPreviewFrames.clear();
	m_fontPreviewActive = false;
	m_fontPreviewPending.clear();
	m_fontPreviewApplied.clear();
}

bool SuneerControlBar::eventFilter(QObject* obj, QEvent* ev)
{
	// QComboBox has no "popup cancelled" signal, so the popup hiding is our cue to put the
	// previewed text back — that covers Escape, clicking away, and committing a row alike.
	// On a commit the activated() handler re-applies the font properly (with undo) right
	// afterwards, so the undo step records original -> chosen rather than preview -> chosen.
	if (m_fontCombo && obj == m_fontCombo->view() && ev->type() == QEvent::Hide && m_fontPreviewActive)
	{
		restoreFontPreview();
		endFontPreview();
	}
	return QToolBar::eventFilter(obj, ev);
}

void SuneerControlBar::onFontChanged(const QFont& font)
{
	if (m_updating || !m_doc) return;
	QString fontName = m_fontCombo->currentText();
	if (!m_doc->m_Selection->isEmpty()) {
		PageItem* item = m_doc->m_Selection->itemAt(0);
		// suneer: table-wide Font — only handle the "whole table selected as an object"
		// case here. In modeEditTable we fall through to itemSelection_SetFont, which
		// routes through itemSelection_ApplyCharStyle and formats every selected cell
		// (table->selectedCells) with proper single-Ctrl+Z undo — same path the working
		// color handlers use.
		if (item->isTable() && m_doc->appMode != modeEditTable) {
			PageItem_Table* tbl = item->asTable();
			ScFace face = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts[fontName];
			for (int r = 0; r < tbl->rows(); r++)
				for (int c = 0; c < tbl->columns(); c++) {
					PageItem_TextFrame* tf = tbl->cellAt(r, c).textFrame();
					if (!tf) continue;
					CharStyle cs; cs.setFont(face);
					tf->itemText.applyCharStyle(0, tf->itemText.length(), cs);
					tf->layout(); tf->update();
				}
			m_doc->changed();
			m_doc->regionsChanged()->update(QRectF());
			if (ScCore->primaryMainWindow() && ScCore->primaryMainWindow()->view)
				ScCore->primaryMainWindow()->view->DrawNew();
			return;
		}
	}
	m_doc->itemSelection_SetFont(fontName);
	m_doc->changed();
	// suneer: make the chosen font "sticky". itemSelection_SetFont above already
	// applied the font as a character override to the currently selected text, but
	// that is lost once the text is deleted. Also update each selected text frame's
	// default paragraph style so newly typed text keeps this font instead of
	// reverting to the paragraph style's original default. Runs for BOTH the
	// has-selection and no-selection cases (the old code only did the latter).
	if (!m_doc->m_Selection->isEmpty()) {
		ScFace face = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts[fontName];
		for (int i = 0; i < m_doc->m_Selection->count(); ++i)
		{
			PageItem* item = m_doc->m_Selection->itemAt(i);
			if (!item || !item->isTextFrame()) continue;
			ParagraphStyle ps = item->itemText.defaultStyle();
			ps.charStyle().setFont(face);
			item->itemText.setDefaultStyle(ps);
			item->invalid = true;
		}
		m_doc->regionsChanged()->update(QRectF());
	}

	if (ScCore->primaryMainWindow() &&
		ScCore->primaryMainWindow()->view &&
		ScCore->primaryMainWindow()->view->m_canvas)
	{
		ScCore->primaryMainWindow()->view->m_canvas->setFocus();
	}
}

void SuneerControlBar::onStyleChanged(int)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	QString base = item->currentCharStyle().font().scName();
	base = base.replace(" Bold Italic","").replace(" Bold","").replace(" Italic","").trimmed();
	QString style = m_styleCombo->currentText();
	if (style != "Regular") base += " " + style;
	m_doc->itemSelection_SetFont(base.trimmed());
	m_doc->changed();
}

void SuneerControlBar::onFontSizeChanged(double val)
{
	if (m_updating || !m_doc) return;
	if (!m_doc->m_Selection->isEmpty()) {
		PageItem* item = m_doc->m_Selection->itemAt(0);
		// suneer: table-wide FontSize — only handle the "whole table selected as an
		// object" case here (no built-in doc support for that). In modeEditTable we fall
		// through to itemSelection_SetFontSize, which routes through
		// itemSelection_ApplyCharStyle and formats every selected cell (table->selectedCells)
		// with proper single-Ctrl+Z undo — same path the working color handlers use.
		if (item->isTable() && m_doc->appMode != modeEditTable) {
			PageItem_Table* tbl = item->asTable();
			for (int r = 0; r < tbl->rows(); r++)
				for (int c = 0; c < tbl->columns(); c++) {
					PageItem_TextFrame* tf = tbl->cellAt(r, c).textFrame();
					if (!tf) continue;
					CharStyle cs;
					cs.setFontSize(qRound(val * 10));
					tf->itemText.applyCharStyle(0, tf->itemText.length(), cs);
					tf->layout(); tf->update();
				}
			m_doc->changed();
			m_doc->regionsChanged()->update(QRectF());
			if (ScCore->primaryMainWindow() && ScCore->primaryMainWindow()->view)
				ScCore->primaryMainWindow()->view->DrawNew();
			return;
		}
	}
	m_doc->itemSelection_SetFontSize(qRound(val * 10));
	m_doc->changed();
	// suneer: make the chosen size "sticky" — same rationale as onFontChanged.
	// Font size is stored in 1/10 pt (25pt => 250). Update each selected text
	// frame's default paragraph style so newly typed text keeps this size.
	if (!m_doc->m_Selection->isEmpty()) {
		int sizeTenths = qRound(val * 10);
		for (int i = 0; i < m_doc->m_Selection->count(); ++i)
		{
			PageItem* item = m_doc->m_Selection->itemAt(i);
			if (!item || !item->isTextFrame()) continue;
			ParagraphStyle ps = item->itemText.defaultStyle();
			ps.charStyle().setFontSize(sizeTenths);
			item->itemText.setDefaultStyle(ps);
			item->invalid = true;
		}
		m_doc->regionsChanged()->update(QRectF());
	}
}

void SuneerControlBar::onLineSpacingChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetLineSpacing(val);
	m_doc->changed();
}

void SuneerControlBar::onLineSpModeChanged(int mode)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetLineSpacingMode(mode);
	m_doc->changed();
}

void SuneerControlBar::onAlignChanged(int align)
{
	if (m_updating || !m_doc) return;
	if (!m_doc->m_Selection->isEmpty()) {
		PageItem* item = m_doc->m_Selection->itemAt(0);
		if (item->isTable()) {
			PageItem_Table* tbl = item->asTable();
			for (int r = 0; r < tbl->rows(); r++) {
				for (int c = 0; c < tbl->columns(); c++) {
					PageItem_TextFrame* tf = tbl->cellAt(r, c).textFrame();
					if (!tf || !tf->HasSel) continue;
					m_doc->m_Selection->clear();
					m_doc->m_Selection->addItem(tf);
					m_doc->itemSelection_SetAlignment(align);
				}
			}
			m_doc->m_Selection->clear();
			m_doc->m_Selection->addItem(tbl);
			m_doc->changed();
			return;
		}
	}
	m_doc->itemSelection_SetAlignment(align);
	m_doc->changed();
}

void SuneerControlBar::onStyleEffectChanged(int effect)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetEffects(effect);
	m_doc->changed();
}

void SuneerControlBar::onOutlineWidthChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	if (!m_styleSelect || !m_styleSelect->OutlineVal || !m_styleSelect->OutlineVal->LWidth) return;
	// CharStyle outline width is stored in tenths of a percent
	int x = qRound(m_styleSelect->OutlineVal->LWidth->value() * 10.0);
	m_doc->itemSelection_SetOutlineWidth(x);
	m_doc->changed();
}

void SuneerControlBar::onOutlineStrokeColorChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	if (!m_outlineStrokeColorCombo) return;
	m_doc->itemSelection_SetStrokeColor(m_outlineStrokeColorCombo->currentColor());
	m_doc->changed();
}

void SuneerControlBar::onOutlineStepUp()
{
	if (!m_styleSelect || !m_styleSelect->OutlineVal || !m_styleSelect->OutlineVal->LWidth) return;
	// stepUp() bumps the spinbox by one step and emits valueChanged → onOutlineWidthChanged applies it
	m_styleSelect->OutlineVal->LWidth->stepUp();
}

void SuneerControlBar::onOutlineStepDown()
{
	if (!m_styleSelect || !m_styleSelect->OutlineVal || !m_styleSelect->OutlineVal->LWidth) return;
	m_styleSelect->OutlineVal->LWidth->stepDown();
}

void SuneerControlBar::onOutlineOutwardToggled(bool checked)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	m_doc->itemSelection_SetOutlineOutward(checked ? 1 : 0);
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onTrackingChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetTracking(qRound(val * 10));
	m_doc->changed();
}

void SuneerControlBar::onBaselineChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetBaselineOffset(qRound(val * 10));
	m_doc->changed();
}

void SuneerControlBar::onScaleHChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetScaleH(qRound(val * 10));
	m_doc->changed();
}

void SuneerControlBar::onScaleVChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetScaleV(qRound(val * 10));
	m_doc->changed();
}

void SuneerControlBar::onColumnsChanged(int val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (item->isTextFrame()) {
		item->setColumns(val);
		item->update(); m_doc->changed();
	}
}

void SuneerControlBar::onColumnGapChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (item->isTextFrame()) {
		item->setColumnGap(val * MM2PT);
		item->update(); m_doc->changed();
	}
}

void SuneerControlBar::onColumnGapModeChanged(int) {}

void SuneerControlBar::onFirstLineIndentChanged(double val)
{
	if (m_updating || !m_doc) return;
	ParagraphStyle ps; ps.setFirstIndent(val * MM2PT);
	m_doc->itemSelection_ApplyParagraphStyle(ps);   // merge-only, matches Properties Palette Distances
	m_doc->changed();
}

void SuneerControlBar::onGapBeforeChanged(double val)
{
	if (m_updating || !m_doc) return;
	ParagraphStyle ps; ps.setGapBefore(val);
	m_doc->itemSelection_ApplyParagraphStyle(ps);   // merge-only, matches Properties Palette Distances
	m_doc->changed();
}

void SuneerControlBar::onGapAfterChanged(double val)
{
	if (m_updating || !m_doc) return;
	ParagraphStyle ps; ps.setGapAfter(val);
	m_doc->itemSelection_ApplyParagraphStyle(ps);   // merge-only, matches Properties Palette Distances
	m_doc->changed();
}

void SuneerControlBar::onTextColorChanged()
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetFillColor(m_textColorBtn->colorName());
	m_doc->itemSelection_SetFillShade(m_textColorBtn->colorData().Shade);
	m_doc->changed();
}

void SuneerControlBar::onBgColorChanged()
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetBackgroundColor(m_bgColorBtn->colorName());
	m_doc->itemSelection_SetBackgroundShade(m_bgColorBtn->colorData().Shade);
	m_doc->changed();
}

void SuneerControlBar::onImgRotChanged(double val)
{
	if (m_updating || !m_doc) return;
	m_doc->itemSelection_SetImageRotation(360 - val);
	m_doc->changed();
}

void SuneerControlBar::onTextWrapShow()
{
	QPoint pos = m_textWrapBtn->mapToGlobal(QPoint(0, m_textWrapBtn->height()));
	m_textWrapPopup->move(pos);
	m_textWrapPopup->show();
	m_textWrapPopup->raise();
}

void SuneerControlBar::onTextFlowNone()
{
	if (!m_doc) return;
	for (int i=0;i<m_doc->m_Selection->count();i++) m_doc->m_Selection->itemAt(i)->setTextFlowMode(PageItem::TextFlowDisabled);
	m_doc->changed();
}

void SuneerControlBar::onTextFlowShape()
{
	if (!m_doc) return;
	for (int i=0;i<m_doc->m_Selection->count();i++) m_doc->m_Selection->itemAt(i)->setTextFlowMode(PageItem::TextFlowUsesFrameShape);
	m_doc->changed();
}

void SuneerControlBar::onTextFlowBBox()
{
	if (!m_doc) return;
	for (int i=0;i<m_doc->m_Selection->count();i++) m_doc->m_Selection->itemAt(i)->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
	m_doc->changed();
}

void SuneerControlBar::onTextFlowContour()
{
	if (!m_doc) return;
	for (int i=0;i<m_doc->m_Selection->count();i++) m_doc->m_Selection->itemAt(i)->setTextFlowMode(PageItem::TextFlowUsesContourLine);
	m_doc->changed();
}

void SuneerControlBar::onTextFlowClip()
{
	if (!m_doc) return;
	for (int i=0;i<m_doc->m_Selection->count();i++) m_doc->m_Selection->itemAt(i)->setTextFlowMode(PageItem::TextFlowUsesImageClipping);
	m_doc->changed();
}

void SuneerControlBar::onImgRot90CCW()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	for (int i = 0; i < m_doc->m_Selection->count(); i++) {
		PageItem* item = m_doc->m_Selection->itemAt(i);
		m_doc->rotateItem(-90.0, item);
	}
	m_doc->changed();
}

void SuneerControlBar::onImgRot90CW()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	for (int i = 0; i < m_doc->m_Selection->count(); i++) {
		PageItem* item = m_doc->m_Selection->itemAt(i);
		m_doc->rotateItem(90.0, item);
	}
	m_doc->changed();
}

void SuneerControlBar::onImgToFront()
{
	if (!m_doc) return;
	m_doc->bringItemSelectionToFront();
	m_doc->changed();
}

void SuneerControlBar::onImgRaise()
{
	if (!m_doc) return;
	m_doc->itemSelection_RaiseItem();
	m_doc->changed();
}

void SuneerControlBar::onImgLower()
{
	if (!m_doc) return;
	m_doc->itemSelection_LowerItem();
	m_doc->changed();
}

void SuneerControlBar::onImgToBack()
{
	if (!m_doc) return;
	m_doc->sendItemSelectionToBack();
	m_doc->changed();
}

void SuneerControlBar::onImgFlipH()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	m_doc->itemSelection_FlipH();
	m_doc->changed();
}

void SuneerControlBar::onImgFlipV()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	m_doc->itemSelection_FlipV();
	m_doc->changed();
}

void SuneerControlBar::onImgFitFrame()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	m_doc->itemSelection_AdjustFrametoImageSize();
	m_doc->changed();
}

void SuneerControlBar::onImgFitImage()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	m_doc->itemSelection_AdjustImagetoFrameSize();
	m_doc->changed();
}




void SuneerControlBar::onImgDrawContour()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item->isImageFrame()) return;
	ScribusMainWindow* mw = ScCore->primaryMainWindow();
	if (!mw) return;
	// Switch to freehand contour draw mode
	mw->setAppModeByToggle(true, modeSuneerContourDraw);
}

void SuneerControlBar::onImgContourEditToggle(bool checked)
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	ScribusMainWindow* mw = ScCore->primaryMainWindow();
	if (!mw) return;
	if (checked) {
		mw->setAppModeByToggle(true, modeEditClip);
		m_doc->nodeEdit.setIsContourLine(true);
		if (mw->nodePalette) {
			mw->nodePalette->show();
			if (!mw->nodePalette->EditCont->isChecked()) {
				mw->nodePalette->EditCont->setChecked(true);
				mw->nodePalette->ToggleContourMode();
			}
		}
	} else {
		mw->setAppModeByToggle(false, modeEditClip);
		m_doc->nodeEdit.setIsContourLine(false);
		if (mw->nodePalette) {
			if (mw->nodePalette->EditCont->isChecked()) {
				mw->nodePalette->EditCont->setChecked(false);
				mw->nodePalette->ToggleContourMode();
			}
		}
	}
	m_doc->changed();
	m_doc->invalidateAll();
	m_doc->regionsChanged()->update(QRectF());
	if (mw->view) { mw->view->DrawNew(); mw->view->update(); }
}

void SuneerControlBar::onImgRemoveBackground()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (item->Pfile.isEmpty()) {
		QMessageBox::warning(this, "Remove Background", "No image loaded in this frame.");
		return;
	}

	// Model selection dialog
	QDialog dlg(this);
	dlg.setWindowTitle("Remove Background");
	dlg.setFixedWidth(300);
	QVBoxLayout* vl = new QVBoxLayout(&dlg);

	QLabel* lbl = new QLabel("Select AI Model:", &dlg);
	QFont f = lbl->font(); f.setBold(true); lbl->setFont(f);
	vl->addWidget(lbl);

	QComboBox* modelCombo = new QComboBox(&dlg);
	modelCombo->addItem("u2net_human_seg  — Portrait / Human (Best)", "u2net_human_seg");
	modelCombo->addItem("u2net            — General Objects",          "u2net");
	modelCombo->addItem("isnet-general-use — High Detail Edges",       "isnet-general-use");
	modelCombo->addItem("silueta          — Silhouette / Fast",        "silueta");
	modelCombo->setCurrentIndex(0);
	vl->addWidget(modelCombo);

	// Feather option
	vl->addWidget(new QLabel("Edge Feather (px):"));
	QSpinBox* featherSpin = new QSpinBox(&dlg);
	featherSpin->setRange(0, 50);
	featherSpin->setValue(0);
	featherSpin->setSuffix(" px");
	featherSpin->setToolTip("Blur edges after background removal (0 = no feather)");
	vl->addWidget(featherSpin);
	vl->addSpacing(8);
	QDialogButtonBox* btns = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	vl->addWidget(btns);

	if (dlg.exec() != QDialog::Accepted) return;

	QString modelName  = modelCombo->currentData().toString();
	int featherPx = featherSpin->value();
	QString inputPath  = item->Pfile;
	QFileInfo fi(inputPath);
	QString outputPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_rembg.png";

	QString script = QString(
		"from rembg import remove, new_session\n"
		"from PIL import Image\n"
		"import numpy as np\n"
		"session = new_session('%3')\n"
		"img = Image.open(r'%1').convert('RGBA')\n"
		"result = remove(img, session=session, alpha_matting=True, alpha_matting_foreground_threshold=240, alpha_matting_background_threshold=10, alpha_matting_erode_size=10)\n"
		"import PIL.ImageFilter as _IF\n"
		"if %4 > 0:\n"
		"    r,g,b,a = result.split()\n"
		"    a = a.filter(_IF.GaussianBlur(radius=%4))\n"
		"    result = __import__('PIL.Image', fromlist=['Image']).merge('RGBA',(r,g,b,a))\n"
		"result.save(r'%2')\n"
	).arg(inputPath).arg(outputPath).arg(modelName).arg(featherPx);

	QApplication::setOverrideCursor(Qt::WaitCursor);

	QProcess proc;
	proc.start("python3", QStringList() << "-c" << script);
	proc.waitForFinished(120000); // 2 min timeout

	QApplication::restoreOverrideCursor();

	QString stdOut = QString::fromLocal8Bit(proc.readAllStandardOutput());
	QString stdErr = QString::fromLocal8Bit(proc.readAllStandardError());

	if (proc.exitCode() == 0) {
		item->Pfile = outputPath;
		m_doc->loadPict(outputPath, item, false, true);
		updateCaptionFrame(item);
		// Auto fit image to frame
		if (item->OrigW > 0 && item->OrigH > 0) {
			double scaleX = item->width()  / (double)item->OrigW;
			double scaleY = item->height() / (double)item->OrigH;
			double scale  = qMin(scaleX, scaleY);
			item->setImageXYScale(scale, scale);
			item->setImageXYOffset(
				(item->width()  - item->OrigW * scale) / 2.0,
				(item->height() - item->OrigH * scale) / 2.0);
		}
		item->update();
		m_doc->changed();
		// Auto contour + text flow + enable contour editing mode
		QTimer::singleShot(400, this, [this]() {
			onAutoContour();
			onTextFlowContour();
			m_doc->regionsChanged()->update(QRectF());
			// Enable Contour Line Editing Mode
			ScribusMainWindow* mw = ScCore->primaryMainWindow();
			if (mw) {
				// Switch to node edit mode
				mw->setAppModeByToggle(true, modeEditClip);
				// Set contour line mode directly
				m_doc->nodeEdit.setIsContourLine(true);
					// Show node palette
				if (mw->nodePalette)
					mw->nodePalette->show();
				// Force canvas redraw
				m_doc->regionsChanged()->update(QRectF());
				m_doc->changed();
				if (mw->view) {
					mw->view->DrawNew();
					mw->view->update();
				}
			}
		});
		QMessageBox::information(this, "Done", "Background removed + Contour detected!\nSaved: " + outputPath);
	} else {
		QString msg = stdErr.isEmpty() ? stdOut : stdErr;
		QMessageBox::warning(this, "rembg Error", msg.isEmpty() ? "Unknown error\nExit: " + QString::number(proc.exitCode()) : msg);
	}
}

void SuneerControlBar::onPadReset()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	for (int i = 0; i < m_doc->m_Selection->count(); i++)
		m_doc->m_Selection->itemAt(i)->setWrapOffsets(0, 0, 0, 0);
	m_doc->changed();
}

// ── Text-frame gap handlers ─────────────────────────────────────────────────

void SuneerControlBar::onTextPadReset()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	const bool internal = m_internalPadChk && m_internalPadChk->isChecked();
	// One undo step per button press rather than one per selected frame.
	UndoTransaction padTransaction;
	if (UndoManager::undoEnabled())
		padTransaction = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                                          Um::TextFrameDist, QString(), Um::IBorder);
	for (int i = 0; i < m_doc->m_Selection->count(); i++) {
		PageItem* item = m_doc->m_Selection->itemAt(i);
		if (item->isTextFrame()) {
			if (internal)
				item->setTextToFrameDist(0, 0, 0, 0);  // internal text distance
			else
				item->setWrapOffsets(0, 0, 0, 0);       // external wrap boundary
		}
		item->update();
	}
	if (padTransaction)
		padTransaction.commit();
	m_doc->regionsChanged()->update(QRectF());
	m_doc->changed();
}

void SuneerControlBar::onTextPadAllChanged(double delta)
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	const double step = delta * 2.8346;
	const bool internal = m_internalPadChk && m_internalPadChk->isChecked();
	// One undo step per button press rather than one per selected frame.
	// setTextToFrameDist() records its own state; setWrapOffsets() records
	// none, and an empty transaction is discarded by commit(), so wrapping
	// both paths is safe.
	UndoTransaction padTransaction;
	if (UndoManager::undoEnabled())
		padTransaction = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                                          Um::TextFrameDist, QString(), Um::IBorder);
	for (int i = 0; i < m_doc->m_Selection->count(); i++) {
		PageItem* item = m_doc->m_Selection->itemAt(i);
		if (!item->isTextFrame()) continue;
		if (internal)
			item->setTextToFrameDist(   // internal text distance (left, right, top, bottom)
				qMax(0.0, item->textToFrameDistLeft()   + step),
				qMax(0.0, item->textToFrameDistRight()  + step),
				qMax(0.0, item->textToFrameDistTop()    + step),
				qMax(0.0, item->textToFrameDistBottom() + step));
		else
			item->setWrapOffsets(       // external wrap boundary (top, bottom, left, right)
				qMax(0.0, item->wrapOffsetTop()    + step),
				qMax(0.0, item->wrapOffsetBottom() + step),
				qMax(0.0, item->wrapOffsetLeft()   + step),
				qMax(0.0, item->wrapOffsetRight()  + step));
		item->update();
	}
	if (padTransaction)
		padTransaction.commit();
	m_doc->regionsChanged()->update(QRectF());
	m_doc->changed();
}

// side: 0=top 1=bottom 2=left 3=right
void SuneerControlBar::onTextPadSideChanged(int side, double delta)
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	const double step = delta * 2.8346;
	const bool internal = m_internalPadChk && m_internalPadChk->isChecked();
	// One undo step per button press rather than one per selected frame.
	UndoTransaction padTransaction;
	if (UndoManager::undoEnabled())
		padTransaction = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                                          Um::TextFrameDist, QString(), Um::IBorder);
	for (int i = 0; i < m_doc->m_Selection->count(); i++) {
		PageItem* item = m_doc->m_Selection->itemAt(i);
		if (!item->isTextFrame()) continue;
		if (internal) {
			double l = item->textToFrameDistLeft(),  r = item->textToFrameDistRight();
			double t = item->textToFrameDistTop(),   b = item->textToFrameDistBottom();
			if      (side == 0) t = qMax(0.0, t + step);
			else if (side == 1) b = qMax(0.0, b + step);
			else if (side == 2) l = qMax(0.0, l + step);
			else if (side == 3) r = qMax(0.0, r + step);
			item->setTextToFrameDist(l, r, t, b);   // internal text distance
		} else {
			double t = item->wrapOffsetTop(),  b = item->wrapOffsetBottom();
			double l = item->wrapOffsetLeft(), r = item->wrapOffsetRight();
			if      (side == 0) t = qMax(0.0, t + step);
			else if (side == 1) b = qMax(0.0, b + step);
			else if (side == 2) l = qMax(0.0, l + step);
			else if (side == 3) r = qMax(0.0, r + step);
			item->setWrapOffsets(t, b, l, r);       // external wrap boundary
		}
		item->update();
	}
	if (padTransaction)
		padTransaction.commit();
	m_doc->regionsChanged()->update(QRectF());
	m_doc->changed();
}

void SuneerControlBar::onPadAllChanged(double delta)
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	double step = delta * MM2PT;
	for (int i = 0; i < m_doc->m_Selection->count(); i++)
	{
		PageItem* item = m_doc->m_Selection->itemAt(i);
		// Contour mode ആണോ check
		if (item->textFlowMode() == PageItem::TextFlowUsesContourLine
			&& !item->ContourLine.empty()
			&& m_doc->nodeEdit.isContourLine())
		{
			// Contour points scale ചെയ്യൂ
			FPointArray contour = item->ContourLine;
			int n = contour.size();
			double cx = 0, cy = 0;
			for (int j = 0; j < n; j++) { FPoint p = contour.point(j); cx += p.x(); cy += p.y(); }
			cx /= n; cy /= n;
			for (int j = 0; j < n; j++) {
				FPoint p = contour.point(j);
				double dx = p.x()-cx, dy = p.y()-cy;
				double dist = qSqrt(dx*dx+dy*dy);
				if (dist > 0.001)
					contour.setPoint(j, FPoint(cx+dx/dist*(dist+step), cy+dy/dist*(dist+step)));
			}
			item->setContour(contour);
		} else {
			// Bounding box mode — wrapOffsets
			item->setWrapOffsets(
				qMax(0.0, item->wrapOffsetTop()    + step),
				qMax(0.0, item->wrapOffsetBottom() + step),
				qMax(0.0, item->wrapOffsetLeft()   + step),
				qMax(0.0, item->wrapOffsetRight()  + step));

			// ✅ Caption frame-ലും same gap apply ചെയ്യൂ
			if (item->isImageFrame()) {
				QString captName = QString("caption_%1").arg(item->itemName());
				for (PageItem* pi : m_doc->DocItems) {
					if (pi->itemName() == captName && pi->isTextFrame()) {
						pi->setWrapOffsets(
							qMax(0.0, pi->wrapOffsetTop()    + step),
							qMax(0.0, pi->wrapOffsetBottom() + step),
							qMax(0.0, pi->wrapOffsetLeft()   + step),
							qMax(0.0, pi->wrapOffsetRight()  + step));
						pi->update();
						break;
					}
				}
			}
		}
		item->update();
	}
	m_doc->changed();
	m_doc->invalidateAll();
	m_doc->regionsChanged()->update(QRectF());
	if (ScCore->primaryMainWindow() && ScCore->primaryMainWindow()->view) {
		ScCore->primaryMainWindow()->view->DrawNew();
		ScCore->primaryMainWindow()->view->update();
	}
}

void SuneerControlBar::onPadSideChanged(int side, double delta)
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	double step = delta * MM2PT;
	for (int i = 0; i < m_doc->m_Selection->count(); i++)
	{
		PageItem* item = m_doc->m_Selection->itemAt(i);
		if (item->textFlowMode() == PageItem::TextFlowUsesContourLine
			&& !item->ContourLine.empty()
			&& m_doc->nodeEdit.isContourLine())
		{
			// Contour side scale
			FPointArray contour = item->ContourLine;
			int n = contour.size();
			double cx = 0, cy = 0;
			for (int j = 0; j < n; j++) { FPoint p = contour.point(j); cx += p.x(); cy += p.y(); }
			cx /= n; cy /= n;
			for (int j = 0; j < n; j++) {
				FPoint p = contour.point(j);
				double dx = p.x()-cx, dy = p.y()-cy;
				double dist = qSqrt(dx*dx+dy*dy);
				if (dist > 0.001) {
					bool apply = (side==0 && dy<0)||(side==1 && dy>0)||(side==2 && dx<0)||(side==3 && dx>0);
					if (apply)
						contour.setPoint(j, FPoint(p.x()+dx/dist*step, p.y()+dy/dist*step));
				}
			}
			item->setContour(contour);
		} else {
			// Bounding box mode
			double t=item->wrapOffsetTop(), b=item->wrapOffsetBottom();
			double l=item->wrapOffsetLeft(), r=item->wrapOffsetRight();
			if (side==0) t=qMax(0.0,t+step);
			if (side==1) b=qMax(0.0,b+step);
			if (side==2) l=qMax(0.0,l+step);
			if (side==3) r=qMax(0.0,r+step);
			item->setWrapOffsets(t,b,l,r);
		}
		item->update();
	}
	m_doc->changed();
	m_doc->invalidateAll();
	m_doc->regionsChanged()->update(QRectF());
	if (ScCore->primaryMainWindow() && ScCore->primaryMainWindow()->view) {
		ScCore->primaryMainWindow()->view->DrawNew();
		ScCore->primaryMainWindow()->view->update();
	}
}

void SuneerControlBar::onAutoContour()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item->isImageFrame() || !item->imageIsAvailable) return;
	if (item->Pfile.isEmpty()) return;

	// Auto fit image to frame first
	if (item->OrigW > 0 && item->OrigH > 0) {
		double imgAspect   = (double)item->OrigW / item->OrigH;
		double frameAspect = item->width() / item->height();
		double scale;
		if (imgAspect > frameAspect)
			scale = item->width() / (double)item->OrigW;
		else
			scale = item->height() / (double)item->OrigH;
		item->setImageXYScale(scale, scale);
		item->setImageXYOffset(
			(item->width()  - item->OrigW * scale) / 2.0,
			(item->height() - item->OrigH * scale) / 2.0);
	}

	FPointArray contour = SuneerAlphaWrap::createContour(
		item->Pfile,
		item->width(),
		item->height(),
		item->imageXScale(),
		item->imageYScale(),
		item->imageXOffset(),
		item->imageYOffset()
	);

	if (contour.size() < 3) return;

	item->ContourLine = contour;
	item->setTextFlowMode(PageItem::TextFlowUsesContourLine);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onParaStyleChanged(int) {}

void SuneerControlBar::iconSetChange()
{
	IconManager& im = IconManager::instance();
	if (!im.loadIcon("fether").isNull())
		m_featherBtn->setIcon(im.loadIcon("fether"));
	else
		m_featherBtn->setIcon(QIcon("/usr/local/share/scribus/icons/1_7_0/fether.png"));
	m_featherBtn->setText("");
	m_featherBtn->setIconSize(QSize(20, 20));
	if (!im.loadIcon("corner-radius").isNull())
		m_cornerOptionsBtn->setIcon(im.loadIcon("corner-radius"));
	else
		m_cornerOptionsBtn->setIcon(QIcon("/usr/local/share/scribus/icons/1_7_0/corner-radius.png"));
	m_cornerOptionsBtn->setText("");
	m_cornerOptionsBtn->setIconSize(QSize(20, 20));
	m_imgRotIconLbl->setPixmap(im.loadPixmap("object-rotation").scaled(16,16,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_imgRemoveBgBtn->setIcon(im.loadIcon("rembg"));
	// DC button knife icon
	if (!im.loadIcon("suneer-knife").isNull())
		m_imgDrawContourBtn->setIcon(im.loadIcon("suneer-knife"));
	else
		m_imgDrawContourBtn->setText("DC");
	m_imgFlipHBtn->setIcon(im.loadIcon("flip-object-horizontal"));
	m_imgFlipVBtn->setIcon(im.loadIcon("flip-object-vertical"));
	if (m_scmw && m_scmw->scrActions.contains("itemRaiseToTop"))
		m_imgToFrontBtn->setIcon(m_scmw->scrActions["itemRaiseToTop"]->icon());
	if (m_scmw && m_scmw->scrActions.contains("itemRaise"))
		m_imgRaiseBtn->setIcon(m_scmw->scrActions["itemRaise"]->icon());
	if (m_scmw && m_scmw->scrActions.contains("itemLower"))
		m_imgLowerBtn->setIcon(m_scmw->scrActions["itemLower"]->icon());
	if (m_scmw && m_scmw->scrActions.contains("itemLowerToBottom"))
		m_imgToBackBtn->setIcon(m_scmw->scrActions["itemLowerToBottom"]->icon());
	// Text flow icons
	im.loadIcon("text-wrap-none").isNull() ? void() : m_textFlowNoneBtn->setIcon(im.loadIcon("text-wrap-none"));
	m_textFlowShapeBtn->setIcon(im.loadIcon("text-wrap-shape"));
	m_textFlowBBoxBtn->setIcon(im.loadIcon("text-wrap-boundingbox"));
	m_textFlowContourBtn->setIcon(im.loadIcon("text-wrap-contour"));
	m_textFlowClipBtn->setIcon(im.loadIcon("text-wrap-image-clip"));
	m_imgRot90CCWBtn->setText("↺");
	m_imgRot90CWBtn->setText("↻");
	m_imgFitFrameBtn->setText("Fi");
	m_imgFitFrameBtn->setToolTip("Fit Frame to Image");
	m_imgFitImageBtn->setText("IF");
	m_imgFitImageBtn->setToolTip("Fit Image to Frame");
	m_columnsIconLbl->setPixmap(im.loadPixmap("paragraph-columns").scaled(16,16,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_firstLineIndentIconLbl->setPixmap(im.loadPixmap("paragraph-indent-firstline").scaled(16,16,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_gapBeforeIconLbl->setPixmap(im.loadPixmap("paragraph-space-above").scaled(16,16,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_gapAfterIconLbl->setPixmap(im.loadPixmap("paragraph-space-below").scaled(16,16,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_trackingIconLbl->setPixmap(im.loadPixmap("character-letter-tracking").scaled(18,18,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_baselineIconLbl->setPixmap(im.loadPixmap("character-offset-baseline").scaled(18,18,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_scaleHIconLbl->setPixmap(im.loadPixmap("character-scale-width").scaled(18,18,Qt::KeepAspectRatio,Qt::SmoothTransformation));
	m_scaleVIconLbl->setPixmap(im.loadPixmap("character-scale-height").scaled(18,18,Qt::KeepAspectRatio,Qt::SmoothTransformation));
}

void SuneerControlBar::languageChange()
{
	setWindowTitle(tr("Control Bar"));
}

void SuneerControlBar::onEmbedInSLA()
{
	if (!m_doc) return;
	// Every image frame in the document: page items, master page items, and the
	// contents of groups on either. makeImageInline() copies the picture to a temp
	// file and sets isInlineImage, which is what makes the .sla saver write the
	// picture as Base64 ImageData instead of a PFILE path.
	QList<PageItem*> frames;
	for (int i = 0; i < m_doc->DocItems.count(); ++i)
		frames.append(m_doc->DocItems.at(i));
	for (int i = 0; i < m_doc->MasterItems.count(); ++i)
		frames.append(m_doc->MasterItems.at(i));
	const int topLevelCount = frames.count();
	for (int i = 0; i < topLevelCount; ++i)
	{
		if (frames.at(i)->isGroup())
			frames.append(frames.at(i)->getAllChildren());
	}
	int embedded = 0, skipped = 0, already = 0;
	QStringList failed;
	for (PageItem* item : std::as_const(frames))
	{
		if (!item->isImageFrame())
			continue;
		if (item->isImageInline())
		{
			++already;
			continue;
		}
		if (!item->imageIsAvailable || item->Pfile.isEmpty())
		{
			++skipped;
			continue;
		}
		const QString oldPath = item->Pfile;
		if (ScCore->fileWatcher->isWatching(oldPath))
			ScCore->fileWatcher->removeFile(oldPath);
		item->makeImageInline();
		if (item->isImageInline())
		{
			ScCore->fileWatcher->addFile(item->Pfile);
			++embedded;
		}
		else
		{
			// makeImageInline() fails silently when the temp copy cannot be made
			ScCore->fileWatcher->addFile(oldPath);
			failed.append(QFileInfo(oldPath).fileName());
		}
	}
	QString msg = tr("Embedded %1 images (%2 skipped — no image loaded).").arg(embedded).arg(skipped);
	if (already > 0)
		msg += tr("\n%1 already embedded.").arg(already);
	if (!failed.isEmpty())
		msg += tr("\nCould not embed: %1").arg(failed.join(", "));
	if (embedded > 0)
		msg += tr("\nSave the document to write them into the file.");
	QMessageBox::information(this, tr("Embed in SLA"), msg);
	if (embedded > 0)
		m_doc->changed();
}

void SuneerControlBar::onEmbedLink()
{
	if (!m_doc) return;
	// Copy images to document folder and update paths
	QString docPath = QFileInfo(m_doc->documentFileName()).absolutePath();
	if (docPath.isEmpty()) {
		QMessageBox::warning(this, "Embed Link", "Please save the document first.");
		return;
	}
	int count = 0;
	for (int i = 0; i < m_doc->DocItems.count(); i++) {
		PageItem* item = m_doc->DocItems.at(i);
		if (!item->isImageFrame() || !item->imageIsAvailable) continue;
		if (item->Pfile.isEmpty()) continue;
		QFileInfo fi(item->Pfile);
		QString newPath = docPath + "/" + fi.fileName();
		if (item->Pfile != newPath) {
			QFile::copy(item->Pfile, newPath);
			item->Pfile = newPath;
			m_doc->loadPict(newPath, item, true, false);
			count++;
		}
	}
	QMessageBox::information(this, "Embed Link",
		QString("Linked %1 images to document folder.").arg(count));
	m_doc->changed();
}

void SuneerControlBar::onCollectToFolder()
{
	if (!m_doc) return;
	ScribusMainWindow* mw = ScCore->primaryMainWindow();
	if (mw) mw->fileCollect(false, true, true); // withFonts=true, withProfiles=true
}

void SuneerControlBar::onImgCropApply()
{
    if (!m_doc || m_doc->m_Selection->isEmpty()) return;
    PageItem* item = m_doc->m_Selection->itemAt(0);
    if (!item || !item->isImageFrame()) return;
    // Activate crop overlay mode
    item->imageCropMode = true;
    item->imageCropRect = QRectF(
        item->xPos(), item->yPos(),
        item->width(), item->height());
    item->activeCropHandle = -1;

    // Switch to crop canvas mode
    ScribusMainWindow* mw = m_doc->scMW();
    if (mw) {
        ScribusView* view = mw->view;
        if (view) {
            // ✅ Auto zoom: image frame fully visible ആക്കൂ
            // item->xPos() = absolute canvas coords (page offset already included)
            double frameX = item->xPos();
            double frameY = item->yPos();
            double frameW = item->width();
            double frameH = item->height();

            // Viewport size
            double vpW = view->visibleWidth();
            double vpH = view->visibleHeight();

            // 10% padding
            double padX = frameW * 0.10;
            double padY = frameH * 0.10;

            // Scale to fit
            double scaleX = vpW / (frameW + 2.0 * padX);
            double scaleY = vpH / (frameH + 2.0 * padY);
            double newScale = qMin(scaleX, scaleY);
            newScale = qMax(0.1, qMin(2.0, newScale));

            // Auto zoom / center disabled
        }
        mw->setAppModeByToggle(true, modeSuneerImageCrop);
    }
    m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::setCropModeActive(bool active)
{
    if (!m_imgCropApplyBtn)
        return;
    m_imgCropApplyBtn->setCheckable(true);
    QSignalBlocker blocker(m_imgCropApplyBtn);
    m_imgCropApplyBtn->setChecked(active);
    // The default checked look is too subtle at this button size, and crop mode
    // swallows canvas gestures — make it unmistakable regardless of theme.
    m_imgCropApplyBtn->setStyleSheet(active
        ? QStringLiteral("QToolButton { background: #e8a33d; border: 2px solid #b3701a;"
                         " border-radius: 3px; font-weight: bold; }")
        : QString());
    m_imgCropApplyBtn->setToolTip(active
        ? tr("Crop mode active — Enter to apply, Esc to cancel")
        : tr("Crop + Resize to fixed size"));
}

void SuneerControlBar::onImgCropResize()
{
    if (!m_doc || m_doc->m_Selection->isEmpty()) return;
    PageItem* item = m_doc->m_Selection->itemAt(0);
    if (!item || !item->isImageFrame()) return;
    if (item->Pfile.isEmpty()) {
        QMessageBox::warning(this, "Crop+Resize", "No image loaded!");
        return;
    }

    double mmToPt = 2.8346;
    double targetW = m_imgCropW->value();  // mm
    double targetH = m_imgCropH->value();  // mm

    QString inputPath  = item->Pfile;
    QFileInfo fi(inputPath);
    QString outputPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_crop.jpg";

    // Current frame crop area in image pixels
    double scaleX = item->imageXScale();
    double scaleY = item->imageYScale();
    double offX   = -item->imageXOffset();
    double offY   = -item->imageYOffset();
    double frameW = item->width();
    double frameH = item->height();

    // Crop box in image pixels
    int cropX = qRound(offX / scaleX);
    int cropY = qRound(offY / scaleY);
    int cropW = qRound(frameW / scaleX);
    int cropH = qRound(frameH / scaleY);
    int resW  = qRound(targetW / 25.4 * 300); // 300 DPI
    int resH  = qRound(targetH / 25.4 * 300);

    QString script = QString(
        "from PIL import Image\n"
        "img = Image.open(r'%1')\n"
        "crop = img.crop((%2, %3, %2+%4, %3+%5))\n"
        "out = crop.resize((%6, %7), Image.LANCZOS)\n"
        "out.save(r'%8', quality=95)\n"
        "print('done')\n"
    ).arg(inputPath)
     .arg(cropX).arg(cropY)
     .arg(cropW).arg(cropH)
     .arg(resW).arg(resH)
     .arg(outputPath);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess proc;
    proc.start("python3", QStringList() << "-c" << script);
    proc.waitForFinished(30000);
    QApplication::restoreOverrideCursor();

    if (proc.exitCode() == 0) {
        m_doc->loadPict(outputPath, item, false, true);
        // Frame size = target
        item->setWidth(targetW * mmToPt);
        item->setHeight(targetH * mmToPt);
        item->setImageXYScale(
            item->width()  / item->pixm.width(),
            item->height() / item->pixm.height());
        item->setImageXYOffset(0, 0);
        item->updateClip();
        item->update();
        m_doc->changed();
        m_doc->regionsChanged()->update(QRectF());
        QMessageBox::information(this, "Crop+Resize", "Done! " + outputPath);
    } else {
        QString err = QString::fromLocal8Bit(proc.readAllStandardError());
        QMessageBox::warning(this, "Crop+Resize Error", err);
    }
}

void SuneerControlBar::onLineMaskChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	if (!m_lineMaskBtn) return;
	// Opacity: 1.0 = fully opaque → transparency = 1.0 - opacity
	double opacity = m_lineMaskBtn->colorData().Opacity;
	m_doc->itemSelection_SetItemLineTransparency(opacity);
}

void SuneerControlBar::onLineNamedStyleChanged(int idx)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (idx == 0)
		item->NamedLStyle.clear();
	else
		item->NamedLStyle = m_lineNamedStyleCombo->currentText();
	m_lineStyleEditBtn->setEnabled(idx != 0);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineStyleEditClicked()
{
	if (!m_scmw) return;
	m_scmw->styleMgr()->show();
}

void SuneerControlBar::onLineStyleNewClicked()
{
	if (!m_scmw) return;
	m_scmw->styleMgr()->show();
}

void SuneerControlBar::onLineColorChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	ColorButton* srcBtn = m_txtLineColorBtn && sender() == m_txtLineColorBtn ? m_txtLineColorBtn : m_lineColorBtn;
	item->setLineColor(srcBtn->colorName());
	item->setLineShade(100.0);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineWidthChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	// Spinbox value is in the document display unit; convert back to points.
	const double ratio = m_doc->unitRatio();
	// Same path as Properties Palette → Line → Thickness of Line:
	// proper Um::LineWidth undo, multi-select, and redraw.
	m_doc->itemSelection_SetLineWidth(ratio != 0.0 ? val / ratio : val);
}

// Make the Line Width spinboxes follow the document unit (suffix, decimals,
// range), matching the Properties Palette → Line → Thickness of Line.
void SuneerControlBar::applyLineWidthUnit()
{
	if (!m_doc) return;
	const int    idx    = m_doc->unitIndex();
	const double ratio  = m_doc->unitRatio();
	const QString suffix = unitGetSuffixFromIndex(idx);
	const int    dec    = unitGetPrecisionFromIndex(idx);
	for (QDoubleSpinBox* sb : { m_textLineWidthSpin, m_imgLineWidthSpin, m_lineWidthSpin })
	{
		if (!sb) continue;
		sb->blockSignals(true);
		sb->setSuffix(suffix);
		sb->setDecimals(dec);
		sb->setRange(0.0, 300.0 * ratio);   // keep the internal 300 pt cap
		sb->blockSignals(false);
	}
}

void SuneerControlBar::unitChange()
{
	applyLineWidthUnit();
	updateFromSelection();
}

void SuneerControlBar::onLineStyleChanged(int /*idx*/)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;

	PageItem* item = m_doc->m_Selection->itemAt(0);

	QComboBox* combo =
		(item->isTextFrame() && m_textLineStyleCombo)
			? m_textLineStyleCombo
			: m_lineStyleCombo;

	Qt::PenStyle style =
		(Qt::PenStyle) combo->currentData().toInt();

	item->setLineStyle(style);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineCapChanged(int cap)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setLineEnd((Qt::PenCapStyle)cap);
	m_lineCapFlatBtn->setChecked(cap   == Qt::FlatCap);
	m_lineCapRoundBtn->setChecked(cap  == Qt::RoundCap);
	m_lineCapSquareBtn->setChecked(cap == Qt::SquareCap);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineJoinChanged(int join)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setLineJoin((Qt::PenJoinStyle)join);
	m_lineJoinMiterBtn->setChecked(join == Qt::MiterJoin);
	m_lineJoinRoundBtn->setChecked(join == Qt::RoundJoin);
	m_lineJoinBevelBtn->setChecked(join == Qt::BevelJoin);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineOpacityChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setLineTransparency(1.0 - val / 100.0);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineStartArrowChanged(int idx)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setStartArrowIndex(idx);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineEndArrowChanged(int idx)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setEndArrowIndex(idx);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onLineDashOffsetChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setDashOffset(val);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onDocChangedForCaption()
{
    // 200ms timer polling — image resize detect & caption sync
    if (!m_doc || m_doc->m_Selection->isEmpty()) return;
    PageItem* item = m_doc->m_Selection->itemAt(0);
    if (!item || !item->isImageFrame()) return;

    double curW = item->width();
    double curH = item->height();
    double curX = item->xPos();
    double curY = item->yPos();
    QString curName = item->itemName();

    // Size/position change detect
    bool sizeChanged = (curName != m_trackedItemName ||
        qAbs(curW - m_lastImgW) > 0.05 ||
        qAbs(curH - m_lastImgH) > 0.05 ||
        qAbs(curX - m_lastImgX) > 0.05 ||
        qAbs(curY - m_lastImgY) > 0.05);

    // Update tracking values
    m_lastImgW = curW;
    m_lastImgH = curH;
    m_lastImgX = curX;
    m_lastImgY = curY;
    m_trackedItemName = curName;

    if (!sizeChanged) return;

    // Caption frame find
    QString captName = QString("caption_%1").arg(item->itemName());
    PageItem* captFrame = nullptr;
    for (PageItem* pi : m_doc->DocItems) {
        if (pi->itemName() == captName && pi->isTextFrame()) {
            captFrame = pi;
            break;
        }
    }
    if (!captFrame) return;

    // ✅ Width sync only — weld handles position automatically
    // Position set ചെയ്യരുത് — weld-ഉമായി conflict ആകും!
    bool widthChanged = qAbs(captFrame->width() - curW) > 0.05;
    if (widthChanged) {
        captFrame->setWidth(curW);
        captFrame->setTextToFrameDist(0.0, 0.0, 4.2519, 4.2519); // 1.5mm
        captFrame->updateClip();
        captFrame->invalidateLayout();
        captFrame->update();
        m_doc->regionsChanged()->update(QRectF());
    }
}

void SuneerControlBar::updateCaptionFrame(PageItem* imgFrame)
{
    if (!imgFrame || !m_doc) return;
    // Find caption frame by name convention
    QString captName = QString("caption_%1").arg(imgFrame->itemName());
    PageItem* captFrame = nullptr;
    for (PageItem* item : m_doc->DocItems) {
        if (item->itemName() == captName && item->isTextFrame()) {
            captFrame = item;
            break;
        }
    }
    if (!captFrame) return;
    // Get image description using exiftool
    QString desc;
    if (!imgFrame->Pfile.isEmpty()) {
        QProcess exifProc;
        exifProc.start("exiftool", QStringList() << "-UserComment" << "-b" << imgFrame->Pfile);
        exifProc.waitForFinished(5000);
        desc = QString::fromUtf8(exifProc.readAllStandardOutput()).trimmed();
        if (desc.isEmpty()) {
            // Try ImageDescription
            exifProc.start("exiftool", QStringList() << "-ImageDescription" << "-b" << imgFrame->Pfile);
            exifProc.waitForFinished(5000);
            desc = QString::fromUtf8(exifProc.readAllStandardOutput()).trimmed();
        }
    }
    if (desc.isEmpty()) desc = imgFrame->pixm.imgInfo.exifInfo.comment;
    if (desc.isEmpty()) desc = imgFrame->pixm.imgInfo.exifInfo.userComment;
    if (desc.isEmpty()) return;
    // Fill caption frame
    captFrame->itemText.clear();
    // Insert each character properly
    for (int ci = 0; ci < desc.length(); ++ci)
        captFrame->itemText.insertChars(ci, QString(desc[ci]));
    if (m_doc->paragraphStyles().contains("09 Caption")) {
        const ParagraphStyle& capStyle = m_doc->paragraphStyles().get("09 Caption");
        for (int i = 0; i < captFrame->itemText.length(); ++i)
            captFrame->itemText.applyStyle(i, capStyle);
    }
    // Sync caption frame position + width with image frame
    captFrame->setWidth(imgFrame->width());
    captFrame->setTextToFrameDist(0.0, 0.0, 4.2519, 4.2519); // 1.5mm
    captFrame->setXPos(imgFrame->xPos());
    captFrame->setYPos(imgFrame->yPos() + imgFrame->height());
    captFrame->updateClip();
    captFrame->invalidateLayout();
    captFrame->update();
    m_doc->regionsChanged()->update(QRectF());
    m_doc->changed();
}

void SuneerControlBar::onFillColorChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	ColorButton* btn = qobject_cast<ColorButton*>(sender());
	if (!btn) btn = m_fillColorBtn;
	if (!btn) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setFillColor(btn->colorName());
	item->setFillShade(btn->colorData().Shade);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onFillOpacityChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setFillTransparency(1.0 - val / 100.0);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onImgLineStyleChanged(int idx)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	QString styleName = (idx == 0) ? "" : m_imgLineStyleCombo->currentText();
	item->setCustomLineStyle(styleName);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onImgLineStyleEdit()
{
	if (!m_scmw) return;
	// Show style manager
	if (!m_scmw->scrActions["editStyles"]->isChecked())
		m_scmw->scrActions["editStyles"]->trigger();
	else
		m_scmw->scrActions["editStyles"]->setChecked(true);
}

void SuneerControlBar::onImgLineStyleAdd()
{
	if (!m_scmw) return;
	if (!m_scmw->scrActions["editStyles"]->isChecked())
		m_scmw->scrActions["editStyles"]->trigger();
	else
		m_scmw->scrActions["editStyles"]->setChecked(true);
}

void SuneerControlBar::onImgLineColorChanged()
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	ColorButton* btn = qobject_cast<ColorButton*>(sender());
	if (!btn) btn = m_imgLineColorBtn;
	if (!btn) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	item->setLineColor(btn->colorName());
	item->setLineShade(btn->colorData().Shade);
	item->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onImgWidthChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	const double MM2PT = 2.8346;
	if (!item->isImageFrame() || item->OrigW <= 0)
	{
		// Generic resize for shapes, polygons, lines etc. — scale the item's path
		double gw = val * MM2PT;
		if (gw <= 0) return;
		bool oldS = item->Sizing;
		item->Sizing = false;
		item->OldB2 = item->width();
		item->OldH2 = item->height();
		m_doc->sizeItem(gw, item->height(), item, true, true, false);
		item->Sizing = oldS;
		item->update();
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
		return;
	}
	// New frame width in points
	double newW = val * MM2PT;
	// Keep aspect ratio
	double aspect = (item->OrigH > 0) ? (double)item->OrigH / item->OrigW : 1.0;
	double newH = newW * aspect;
	// Resize through the document like the corner-drag path does: sizeItem()
	// invalidates the old bounding rect and updates the clip path, so no stale
	// outline/handles remain (raw setWidth/setHeight left the old frame ghost
	// on the canvas). One transaction so frame size, image scale and caption
	// undo as a single step.
	UndoTransaction resizeTransaction;
	if (UndoManager::undoEnabled())
		resizeTransaction = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), Um::Resize, QString(), Um::IResize);
	bool oldSizing = item->Sizing;
	item->Sizing = false;
	item->OldB2 = item->width();
	item->OldH2 = item->height();
	m_doc->sizeItem(newW, newH, item, true, true, false);
	item->Sizing = oldSizing;
	// Scale image to fill frame exactly
	double scaleX = newW / item->OrigW;
	double scaleY = newH / item->OrigH;
	item->setImageXYScale(scaleX, scaleY);
	item->setImageXYOffset(0, 0);
	item->update();
	// Update caption frame size + position (same invalidation rules)
	QString captName = QString("caption_%1").arg(item->itemName());
	for (PageItem* pi : m_doc->DocItems) {
		if (pi->itemName() == captName) {
			bool oldPiSizing = pi->Sizing;
			pi->Sizing = false;
			pi->OldB2 = pi->width();
			pi->OldH2 = pi->height();
			m_doc->sizeItem(newW, pi->height(), pi, true, true, false);
			pi->Sizing = oldPiSizing;
			m_doc->moveItem(item->xPos() - pi->xPos(), item->yPos() + newH - pi->yPos(), pi);
			pi->update();
			break;
		}
	}
	if (resizeTransaction)
		resizeTransaction.commit();
	// Update H: spin — show frame height
	if (m_imgHeightSpin) {
		m_imgHeightSpin->blockSignals(true);
		m_imgHeightSpin->setValue(newH / 2.8346);
		m_imgHeightSpin->blockSignals(false);
	}
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::onImgHeightChanged(double val)
{
	if (m_updating || !m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	const double MM2PT = 2.8346;
	if (!item->isImageFrame() || item->OrigH <= 0)
	{
		// Generic resize for shapes, polygons, lines etc. — scale the item's path
		double gh = val * MM2PT;
		if (gh <= 0) return;
		bool oldS = item->Sizing;
		item->Sizing = false;
		item->OldB2 = item->width();
		item->OldH2 = item->height();
		m_doc->sizeItem(item->width(), gh, item, true, true, false);
		item->Sizing = oldS;
		item->update();
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
		return;
	}
	// New frame height in points
	double newH = val * MM2PT;
	// Keep aspect ratio
	double aspect = (item->OrigW > 0) ? (double)item->OrigW / item->OrigH : 1.0;
	double newW = newH * aspect;
	// See onImgWidthChanged: sizeItem() instead of raw setWidth/setHeight so
	// the old bounds are invalidated and the resize is one undo step.
	UndoTransaction resizeTransaction;
	if (UndoManager::undoEnabled())
		resizeTransaction = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), Um::Resize, QString(), Um::IResize);
	bool oldSizing = item->Sizing;
	item->Sizing = false;
	item->OldB2 = item->width();
	item->OldH2 = item->height();
	m_doc->sizeItem(newW, newH, item, true, true, false);
	item->Sizing = oldSizing;
	// Scale image to fill frame exactly
	double scaleX = newW / item->OrigW;
	double scaleY = newH / item->OrigH;
	item->setImageXYScale(scaleX, scaleY);
	item->setImageXYOffset(0, 0);
	item->update();
	// Update caption frame (same invalidation rules)
	QString captName2 = QString("caption_%1").arg(item->itemName());
	for (PageItem* pi : m_doc->DocItems) {
		if (pi->itemName() == captName2) {
			bool oldPiSizing = pi->Sizing;
			pi->Sizing = false;
			pi->OldB2 = pi->width();
			pi->OldH2 = pi->height();
			m_doc->sizeItem(newW, pi->height(), pi, true, true, false);
			pi->Sizing = oldPiSizing;
			m_doc->moveItem(item->xPos() - pi->xPos(), item->yPos() + newH - pi->yPos(), pi);
			pi->update();
			break;
		}
	}
	if (resizeTransaction)
		resizeTransaction.commit();
	// Update W: spin — show frame width
	if (m_imgWidthSpin) {
		m_imgWidthSpin->blockSignals(true);
		m_imgWidthSpin->setValue(newW / 2.8346);
		m_imgWidthSpin->blockSignals(false);
	}
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerControlBar::applyCornerRadius()
{
	ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
	if (!doc) return;
	double unitRatio = doc->unitRatio();
	double tl = m_cornerTLSpin->value() / unitRatio;
	double tr = m_cornerTRSpin->value() / unitRatio;
	double bl = m_cornerBLSpin->value() / unitRatio;
	double br = m_cornerBRSpin->value() / unitRatio;

	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!item) continue;
		double w = item->width();
		double h = item->height();

		// Clamp radius to half of min dimension
		double maxR = qMin(w, h) / 2.0;
		double rTL = qMin(tl, maxR);
		double rTR = qMin(tr, maxR);
		double rBL = qMin(bl, maxR);
		double rBR = qMin(br, maxR);

		QPainterPath path;
		path.moveTo(rTL, 0);
		path.lineTo(w - rTR, 0);
		if (rTR > 0) path.arcTo(w - 2*rTR, 0, 2*rTR, 2*rTR, 90, -90);
		path.lineTo(w, h - rBR);
		if (rBR > 0) path.arcTo(w - 2*rBR, h - 2*rBR, 2*rBR, 2*rBR, 0, -90);
		path.lineTo(rBL, h);
		if (rBL > 0) path.arcTo(0, h - 2*rBL, 2*rBL, 2*rBL, 270, -90);
		path.lineTo(0, rTL);
		if (rTL > 0) path.arcTo(0, 0, 2*rTL, 2*rTL, 180, -90);
		path.closeSubpath();

		item->PoLine.resize(0);
		item->PoLine.fromQPainterPath(path);
		item->Clip = flattenPath(item->PoLine, item->Segments);
		item->ClipEdited = true;
		item->FrameType = 2;
		item->update();
		doc->setRedrawBounding(item);
	}
	doc->regionsChanged()->update(QRect());
	doc->changed();
	doc->changedPagePreview();
}

void SuneerControlBar::applyFeather()
{
	ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
	if (!doc) return;
	double unitRatio = doc->unitRatio();
	double blurPts = m_featherSpin->value() / unitRatio;

	// Direction → offset
	double ox = 0.0, oy = 0.0;
	if (m_featherTopBtn->isChecked())         { ox = 0;       oy = blurPts; }
	else if (m_featherBottomBtn->isChecked()) { ox = 0;       oy = -blurPts; }
	else if (m_featherLeftBtn->isChecked())   { ox = blurPts; oy = 0; }
	else if (m_featherRightBtn->isChecked())  { ox = -blurPts;oy = 0; }
	// All → ox=0, oy=0

	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!item) continue;
		item->setHasSoftShadow(blurPts > 0);
		item->setSoftShadowBlurRadius(blurPts);
		item->setSoftShadowXOffset(ox);
		item->setSoftShadowYOffset(oy);
		item->setSoftShadowColor("Black");
		item->setSoftShadowShade(100);
		item->setSoftShadowOpacity(0.0);  // 0.0 = fully visible
		item->setSoftShadowBlendMode(0);
		item->setSoftShadowErasedByObject(false);
		item->setSoftShadowHasObjectTransparency(false);
		item->update();
		doc->setRedrawBounding(item);
	}
	doc->regionsChanged()->update(QRect());
	doc->changed();
	m_featherPopup->hide();
}

void SuneerControlBar::onImgEdgeFeather()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (item->Pfile.isEmpty()) {
		QMessageBox::warning(this, "Edge Feather", "No image loaded in this frame.");
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle("Edge Feather");
	dlg.setFixedWidth(280);
	QVBoxLayout* vl = new QVBoxLayout(&dlg);
	vl->addWidget(new QLabel("<b>Edge Feather</b>"));
	vl->addSpacing(6);
	// Direction
	vl->addWidget(new QLabel("Direction:"));
	QComboBox* dirCombo = new QComboBox(&dlg);
	dirCombo->addItem("All Sides",  "all");
	dirCombo->addItem("Top Only",   "top");
	dirCombo->addItem("Bottom Only","bottom");
	dirCombo->addItem("Left Only",  "left");
	dirCombo->addItem("Right Only", "right");
	vl->addWidget(dirCombo);
	vl->addSpacing(6);
	// Amount
	vl->addWidget(new QLabel("Feather Amount (px):"));
	QSpinBox* featherSpin = new QSpinBox(&dlg);
	featherSpin->setRange(1, 200);
	featherSpin->setValue(20);
	featherSpin->setSuffix(" px");
	vl->addWidget(featherSpin);
	vl->addSpacing(8);
	QDialogButtonBox* btns = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	vl->addWidget(btns);
	if (dlg.exec() != QDialog::Accepted) return;

	QString direction = dirCombo->currentData().toString();
	int featherPx = featherSpin->value();
	QString inputPath = item->Pfile;
	QFileInfo fi(inputPath);
	QString outputPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_feather.png";

	QString script = QString(
		"from PIL import Image, ImageFilter\n"
		"import numpy as np\n"
		"img = Image.open(r'%1').convert('RGBA')\n"
		"w, h = img.size\n"
		"arr = np.array(img).astype(float)\n"
		"alpha = arr[:,:,3].copy()\n"
		"feather = %2\n"
		"direction = '%3'\n"
		"mask = np.ones((h, w), dtype=float)\n"
		"if direction in ('all', 'top'):\n"
		"    for y in range(min(feather, h)):\n"
		"        mask[y, :] *= y / feather\n"
		"if direction in ('all', 'bottom'):\n"
		"    for y in range(min(feather, h)):\n"
		"        mask[h-1-y, :] *= y / feather\n"
		"if direction in ('all', 'left'):\n"
		"    for x in range(min(feather, w)):\n"
		"        mask[:, x] *= x / feather\n"
		"if direction in ('all', 'right'):\n"
		"    for x in range(min(feather, w)):\n"
		"        mask[:, w-1-x] *= x / feather\n"
		"arr[:,:,3] = alpha * mask\n"
		"result = Image.fromarray(arr.astype(np.uint8))\n"
		"result.save(r'%4')\n"
	).arg(inputPath).arg(featherPx).arg(direction).arg(outputPath);

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QProcess proc;
	proc.start("python3", QStringList() << "-c" << script);
	proc.waitForFinished(60000);
	QApplication::restoreOverrideCursor();

	if (proc.exitCode() == 0) {
		item->Pfile = outputPath;
		m_doc->loadPict(outputPath, item, false, true);
		item->update();
		m_doc->changed();
		QMessageBox::information(this, "Done", "Edge feather applied!\nSaved: " + outputPath);
	} else {
		QString err = QString::fromLocal8Bit(proc.readAllStandardError());
		QMessageBox::warning(this, "Error", err.isEmpty() ? "Unknown error" : err);
	}
}

void SuneerControlBar::onTextEdgeFeather()
{
	if (!m_doc || m_doc->m_Selection->isEmpty()) return;
	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item->isTextFrame()) {
		QMessageBox::warning(this, "Edge Feather", "Please select a text frame.");
		return;
	}

	QDialog dlg(this);
	dlg.setWindowTitle("Text Edge Feather");
	dlg.setFixedWidth(280);
	QVBoxLayout* vl = new QVBoxLayout(&dlg);
	vl->addWidget(new QLabel("<b>Text Edge Feather</b>"));
	vl->addSpacing(6);
	vl->addWidget(new QLabel("Direction:"));
	QComboBox* dirCombo = new QComboBox(&dlg);
	dirCombo->addItem("All Sides",   "all");
	dirCombo->addItem("Top Only",    "top");
	dirCombo->addItem("Bottom Only", "bottom");
	dirCombo->addItem("Left Only",   "left");
	dirCombo->addItem("Right Only",  "right");
	vl->addWidget(dirCombo);
	vl->addSpacing(6);
	vl->addWidget(new QLabel("Feather Amount (px):"));
	QSpinBox* featherSpin = new QSpinBox(&dlg);
	featherSpin->setRange(1, 200);
	featherSpin->setValue(20);
	featherSpin->setSuffix(" px");
	vl->addWidget(featherSpin);
	vl->addSpacing(6);
	vl->addWidget(new QLabel("DPI:"));
	QSpinBox* dpiSpin = new QSpinBox(&dlg);
	dpiSpin->setRange(72, 600);
	dpiSpin->setValue(150);
	dpiSpin->setSuffix(" dpi");
	vl->addWidget(dpiSpin);
	vl->addSpacing(8);
	QDialogButtonBox* btns = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	vl->addWidget(btns);
	if (dlg.exec() != QDialog::Accepted) return;

	QString direction = dirCombo->currentData().toString();
	int featherPx = featherSpin->value();
	int dpi = dpiSpin->value();

	// Render text frame to QImage using DrawObj_toImage
	double scale = dpi / 72.0;
	QImage pm = item->DrawObj_toImage(qMax(item->width(), item->height()) * scale);
	if (pm.isNull()) {
		QMessageBox::warning(this, "Error", "Could not render text frame.");
		return;
	}

	// Save to temp PNG
	QString docPath = m_doc->documentFileName();
	QFileInfo fi(docPath.isEmpty() ? QDir::homePath() + "/untitled" : docPath);
	QString outputPath = fi.absolutePath() + "/text_feather_" +
	                     QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
	pm.save(outputPath, "PNG");

	// Apply feather via Python PIL
	QString script = QString(
		"from PIL import Image\n"
		"import numpy as np\n"
		"img = Image.open(r'%1').convert('RGBA')\n"
		"w, h = img.size\n"
		"arr = np.array(img).astype(float)\n"
		"alpha = arr[:,:,3].copy()\n"
		"feather = %2\n"
		"direction = '%3'\n"
		"mask = np.ones((h, w), dtype=float)\n"
		"if direction in ('all', 'top'):\n"
		"    for y in range(min(feather, h)):\n"
		"        mask[y, :] *= y / feather\n"
		"if direction in ('all', 'bottom'):\n"
		"    for y in range(min(feather, h)):\n"
		"        mask[h-1-y, :] *= y / feather\n"
		"if direction in ('all', 'left'):\n"
		"    for x in range(min(feather, w)):\n"
		"        mask[:, x] *= x / feather\n"
		"if direction in ('all', 'right'):\n"
		"    for x in range(min(feather, w)):\n"
		"        mask[:, w-1-x] *= x / feather\n"
		"arr[:,:,3] = alpha * mask\n"
		"result = Image.fromarray(arr.astype(np.uint8))\n"
		"result.save(r'%1')\n"
	).arg(outputPath).arg(featherPx).arg(direction);

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QProcess proc;
	proc.start("python3", QStringList() << "-c" << script);
	proc.waitForFinished(60000);
	QApplication::restoreOverrideCursor();

	if (proc.exitCode() != 0) {
		QString err = QString::fromLocal8Bit(proc.readAllStandardError());
		QMessageBox::warning(this, "Error", err.isEmpty() ? "Unknown error" : err);
		return;
	}

	// New image frame — same position/size as text frame
	double ix = item->xPos();
	double iy = item->yPos();
	double iw = item->width();
	double ih = item->height();
	int pg = item->OwnPage;

	m_doc->m_Selection->clear();
	int z = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified,
	                       ix, iy, iw, ih, 0,
	                       m_doc->itemToolPrefs().imageFillColor,
	                       m_doc->itemToolPrefs().imageStrokeColor);
	if (z < 0) {
		QMessageBox::warning(this, "Error", "Could not create image frame.");
		return;
	}
	PageItem* imgItem = m_doc->Items->at(z);
	imgItem->OwnPage = pg;
	imgItem->Pfile = outputPath;
	m_doc->loadPict(outputPath, imgItem, false, true);
	if (imgItem->OrigW > 0 && imgItem->OrigH > 0) {
		double scaleX = iw / (double)imgItem->OrigW;
		double scaleY = ih / (double)imgItem->OrigH;
		imgItem->setImageXYScale(scaleX, scaleY);
		imgItem->setImageXYOffset(0, 0);
	}
	imgItem->update();
	m_doc->changed();
	QMessageBox::information(this, "Done", "Text edge feather applied!\nNew image frame created.");
}

void SuneerControlBar::onTextFrameBox()
{
	ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
	if (!doc || doc->m_Selection->isEmpty()) return;

	const double MM2PT = 2.8346;
	const double inset = (m_borderInsetSpin ? m_borderInsetSpin->value() : 2.0) * MM2PT;
	const bool top    = !m_borderTopChk    || m_borderTopChk->isChecked();
	const bool bottom = !m_borderBottomChk || m_borderBottomChk->isChecked();
	const bool left   = !m_borderLeftChk   || m_borderLeftChk->isChecked();
	const bool right  = !m_borderRightChk  || m_borderRightChk->isChecked();

	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!item || !item->isTextFrame()) continue;

		// Per-side border flags (rendered by PageItem::DrawObj_Post etc.)
		item->TopLine    = top;
		item->BottomLine = bottom;
		item->LeftLine   = left;
		item->RightLine  = right;

		// Border stroke used by the selected sides; if none selected, no border.
		if (top || bottom || left || right)
		{
			item->setLineWidth(0.5);
			item->setLineColor("Black");
		}
		else
			item->setLineColor(CommonStrings::None);

		// Text inset (editable value, applied to all four sides)
		item->setTextToFrameDistLeft(inset);
		item->setTextToFrameDistRight(inset);
		item->setTextToFrameDistTop(inset);
		item->setTextToFrameDistBottom(inset);

		item->update();
	}
	doc->regionsChanged()->update(QRect());
	doc->changed();
}
