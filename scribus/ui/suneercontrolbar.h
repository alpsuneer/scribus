#ifndef SUNEERCONTROLBAR_H
#define SUNEERCONTROLBAR_H

#include <QToolBar>
#include <QWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPointer>
#include <QRadioButton>
#include <QCheckBox>

#include "alignselect.h"
#include "styleselect.h"
#include "undotransaction.h"
#include "ui/widgets/color_button.h"
#include "ui/propertiespalette_line.h"

class ScribusMainWindow;
class ScribusDoc;
class PageItem;
class ColorCombo;
class SMPShadeWidget;
class QPushButton;
class Selection;
struct SuneerGroupUndo;

class SuneerControlBar : public QToolBar
{
	Q_OBJECT
public:
	explicit SuneerControlBar(ScribusMainWindow* parent);
	void setDocument(ScribusDoc* doc);

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;

	void showEvent(QShowEvent* e) override {
		QToolBar::showEvent(e);
		updateGeometry();
		repaint();
	}
	void resizeEvent(QResizeEvent* e) override {
		QToolBar::resizeEvent(e);
		updateGeometry();
	}
	QSize sizeHint() const override {
		QSize s = QToolBar::sizeHint();
		s.setHeight(56);
		return s;
	}
	QSize minimumSizeHint() const override {
		return QSize(100, 56);
	}
public slots:
	void updateFromSelection();
	void unitChange();
	void updateCaptionFrame(PageItem* imgFrame);
	void languageChange();
	void iconSetChange();

private slots:
	// Row 1
	void onFontChanged(const QFont& font = QFont());
	//! Status-bar note when a text attribute was applied frame-wide (frame selected, not editing).
	void announceWholeFrameChange(const QString& what);
	void onFontSizeChanged(double val);
	void onAlignChanged(int align);
	// Row 2
	void onStyleChanged(int idx);
	void onLineSpacingChanged(double val);
	void onLineSpModeChanged(int mode);
	// Image
	void onImgRotChanged(double val);
	void onTextWrapShow();
	void onTextFlowNone();
	void onTextFlowShape();
	void onTextFlowBBox();
	void onTextFlowContour();
	void onTextFlowClip();
	void onImgRot90CCW();
	void onImgRot90CW();
	void onImgToFront();
	void onImgRaise();
	void onImgLower();
	void onImgToBack();
	void onImgFlipH();
	void onImgFlipV();
	void onImgFitFrame();
	void onImgFitImage();
	void onImgCropApply();
	void onImgCropResize();
	void onDocChangedForCaption();
	void onImgRemoveBackground();
	void onImgDrawContour();
	void onEmbedInSLA();
	void onEmbedLink();
	void onCollectToFolder();
	void onImgContourEditToggle(bool checked);
	void onStyleEffectChanged(int effect);
	void onColumnsChanged(int val);
	void onColumnGapChanged(double val);
	void onColumnGapModeChanged(int mode);
	void onFirstLineIndentChanged(double val);
	void onOutlineWidthChanged();
	void onOutlineStrokeColorChanged();
	void onOutlineStepUp();
	void onOutlineStepDown();
	void onOutlineOutwardToggled(bool checked);
	void onGapBeforeChanged(double val);
	void onGapAfterChanged(double val);
	void onTrackingChanged(double val);
	void onBaselineChanged(double val);
	void onScaleHChanged(double val);
	void onScaleVChanged(double val);
	// Text-frame gap buttons
	void onTextPadReset();
	void onTextPadAllChanged(double delta);
	void onTextPadSideChanged(int side, double delta);
	// Unused but declared
	void onAutoContour();
	void applyCornerRadius();
	void applyFeather();
	void onImgEdgeFeather();
	void onTextEdgeFeather();
	void onTextFrameBox();
	void onPadReset();
	void onPadAllChanged(double delta);
	void onPadSideChanged(int side, double delta);
	void onTextColorChanged();
	void onBgColorChanged();
	void onParaStyleChanged(int idx);
	// Paragraph Shading popup (local override, never edits the paragraph style)
	void onParagraphShadingShow();
	void onParagraphShadingChanged();
	void onParagraphShadingReset();
	// Line
	void onLineColorChanged();
	void onLineMaskChanged();
	void onFillColorChanged();
	void onImgLineColorChanged();
	void onFillOpacityChanged(double val);
	void onImgLineStyleChanged(int idx);
	void onImgWidthChanged(double val);
	void onImgHeightChanged(double val);
	void onImgLineStyleEdit();
	void onImgLineStyleAdd();
	void onLineNamedStyleChanged(int idx);
	void onLineStyleEditClicked();
	void onLineStyleNewClicked();
	void onLineWidthChanged(double val);
	void onLineStyleChanged(int idx);
	void onLineCapChanged(int cap);
	void onLineJoinChanged(int join);
	void onLineOpacityChanged(double val);
	void onLineStartArrowChanged(int idx);
	void onLineEndArrowChanged(int idx);
	void onLineDashOffsetChanged(double val);

private:
	QDoubleSpinBox* makeSpinBox(double min, double max, int dec, double step, const QString& suffix = "");
	QToolButton*    makeButton(const QString& text, const QString& tooltip, bool checkable = false);
	QLabel*         makeLabel(const QString& text, const QString& tooltip = "");
	void blockAllSignals(bool block);
	void applyLineWidthUnit();
	void showTextWidgets(bool show);
	void showImageWidgets(bool show);
	void showTextWrapWidgets(bool show);
	void updateTextWrapControls(PageItem* item);
	void showLineWidgets(bool show);
	/// Text frame the paragraph controls act on (unwraps an active table cell).
	PageItem* shadingTargetItem() const;
	/// Loads the paragraph's EFFECTIVE shading into the popup, whether that
	/// came from its paragraph style or from an earlier override.
	void loadParagraphShading();
	void endParagraphShadingTransaction();
	/// True when \a w is a control-bar focus-chain widget or a child of one.
	bool isInFocusChain(QWidget* w) const;

	ScribusMainWindow* m_scmw {nullptr};
	QPointer<ScribusDoc> m_doc;
	bool m_updating {false};
	bool m_iconsSet {false};

	// ── ROW 1 ─────────────────────────────────
	QComboBox*      m_fontCombo           {nullptr};
	bool            m_fontComboPopupShown  {false};

	// ── Live font preview (arrow-key / hover navigation in the font dropdown) ──
	// Preview applies fonts with undo suppressed, so the original per-run fonts are kept
	// here and restored when the popup closes. A selection can span several fonts, hence
	// runs rather than one style: restoring a single style would flatten the others.
	struct FontPreviewRun   { int start; int length; QString fontName; };
	struct FontPreviewFrame { QPointer<PageItem> item; QList<FontPreviewRun> runs; };
	QList<FontPreviewFrame> m_fontPreviewFrames;
	bool     m_fontPreviewActive  {false};
	QTimer*  m_fontPreviewTimer   {nullptr};
	QString  m_fontPreviewPending;
	QString  m_fontPreviewApplied;
	void snapshotFontPreview();
	void applyFontPreview(const QString& fontName);
	void restoreFontPreview();
	void endFontPreview();
public:
	void focusFontCombo();
	/// Hands the caret back to the text frame (Enter applies, Esc cancels).
	void returnFocusToCanvas();
	QDoubleSpinBox* m_fontSizeSpin        {nullptr};
	AlignSelect*    m_alignSelect         {nullptr};
	StyleSelect*    m_styleSelect         {nullptr};
	ColorCombo*     m_outlineStrokeColorCombo {nullptr};
	QToolButton*    m_outlineIncBtn       {nullptr};
	QToolButton*    m_outlineDecBtn       {nullptr};
	QCheckBox*      m_outlineOutwardChk   {nullptr};
	QToolButton*    m_padResetBtn         {nullptr};
	QToolButton*    m_padAllPlusBtn       {nullptr};
	QToolButton*    m_padAllMinusBtn      {nullptr};
	QToolButton*    m_padTopPlusBtn       {nullptr};
	QToolButton*    m_padTopMinusBtn      {nullptr};
	QToolButton*    m_padBottomPlusBtn    {nullptr};
	QToolButton*    m_padBottomMinusBtn   {nullptr};
	QToolButton*    m_padLeftPlusBtn      {nullptr};
	QToolButton*    m_padLeftMinusBtn     {nullptr};
	QToolButton*    m_padRightPlusBtn     {nullptr};
	QToolButton*    m_padRightMinusBtn    {nullptr};
	ColorButton*    m_textColorBtn        {nullptr};
	ColorButton*    m_bgColorBtn          {nullptr};
	ColorButton*    m_txtLineColorBtn     {nullptr};
	ColorButton*    m_txtLineMaskBtn      {nullptr};
	QLabel*         m_columnsIconLbl      {nullptr};
	QSpinBox*       m_columnsSpin         {nullptr};
	QComboBox*      m_columnGapCombo      {nullptr};
	QDoubleSpinBox* m_columnGapSpin       {nullptr};
	QLabel*         m_firstLineIndentIconLbl {nullptr};
	QDoubleSpinBox* m_firstLineIndentSpin {nullptr};
	QLabel*         m_gapBeforeIconLbl    {nullptr};
	QDoubleSpinBox* m_gapBeforeSpin       {nullptr};
	QLabel*         m_gapAfterIconLbl     {nullptr};
	QDoubleSpinBox* m_gapAfterSpin        {nullptr};

	// ── ROW 2 advanced spinboxes ─────────────
	QLabel*         m_trackingIconLbl     {nullptr};
	QDoubleSpinBox* m_trackingSpin        {nullptr};
	QLabel*         m_baselineIconLbl     {nullptr};
	QDoubleSpinBox* m_baselineSpin        {nullptr};
	QLabel*         m_scaleHIconLbl       {nullptr};
	QDoubleSpinBox* m_scaleHSpin          {nullptr};
	QLabel*         m_scaleVIconLbl       {nullptr};
	QDoubleSpinBox* m_scaleVSpin          {nullptr};

	// ── ROW 2 ─────────────────────────────────
	QComboBox*      m_styleCombo          {nullptr};
	QDoubleSpinBox* m_lineSpSpin          {nullptr};
	QComboBox*      m_lineSpModeCombo     {nullptr};

	// ── Image ──────────────────────────────────
	QLabel*         m_imgRotIconLbl       {nullptr};
	QDoubleSpinBox* m_imgRotSpin          {nullptr};
	QToolButton*    m_imgRot90CCWBtn      {nullptr};
	QToolButton*    m_imgRot90CWBtn       {nullptr};
	QToolButton*    m_imgToFrontBtn       {nullptr};
	QToolButton*    m_imgRaiseBtn         {nullptr};
	QToolButton*    m_imgLowerBtn         {nullptr};
	QToolButton*    m_imgToBackBtn        {nullptr};
	QToolButton*    m_imgFlipHBtn         {nullptr};
	QToolButton*    m_imgFlipVBtn         {nullptr};
	QToolButton*    m_imgFitFrameBtn      {nullptr};
	// Shape
	QToolButton*    m_textFlowNoneBtn     {nullptr};
	QToolButton*    m_textFlowShapeBtn    {nullptr};
	QToolButton*    m_textFlowBBoxBtn     {nullptr};
	QToolButton*    m_textFlowContourBtn  {nullptr};
	QToolButton*    m_textFlowClipBtn     {nullptr};
	QToolButton*    m_textWrapBtn         {nullptr};
	QWidget*        m_textWrapPopup       {nullptr};
	QRadioButton*   m_wrapNoneRadio       {nullptr};
	QRadioButton*   m_wrapShapeRadio      {nullptr};
	QRadioButton*   m_wrapBBoxRadio       {nullptr};
	QRadioButton*   m_wrapContourRadio    {nullptr};
	QRadioButton*   m_wrapClipRadio       {nullptr};
	QToolButton*    m_imgEmbedBtn          {nullptr};
	QToolButton*    m_imgDrawContourBtn    {nullptr};
	QToolButton*    m_imgContourEditBtn    {nullptr};
	QToolButton*    m_imgRemoveBgBtn       {nullptr};
	QToolButton*    m_imgFitImageBtn      {nullptr};
	// Caption auto-update timer
	QTimer*  m_captionTimer    {nullptr};
	double   m_lastImgW        {0.0};
	double   m_lastImgH        {0.0};
	double   m_lastImgX        {0.0};
	double   m_lastImgY        {0.0};
	QString  m_trackedItemName;

	QDoubleSpinBox* m_imgCropW            {nullptr};
	double imgCropW() const { return m_imgCropW ? m_imgCropW->value() : 80.0; }
	bool isCropResizeEnabled() const { return m_imgCropEnableChk ? m_imgCropEnableChk->isChecked() : false; }
	//! Show the crop button as pressed while the canvas is in crop mode, so the
	//! mode is visible instead of silently swallowing canvas gestures.
	void setCropModeActive(bool active);
	double imgCropH() const { return m_imgCropH ? m_imgCropH->value() : 60.0; }
	QDoubleSpinBox* m_imgCropH            {nullptr};
	QToolButton*    m_imgCropApplyBtn     {nullptr};
	QToolButton*      m_imgCropEnableChk   {nullptr};
	QToolButton*    m_autoContourBtn      {nullptr};
	QCheckBox*      m_autoFitChk          {nullptr};
	QDoubleSpinBox* m_cornerRadiusSpin    {nullptr};
	QToolButton*    m_cornerOptionsBtn    {nullptr};
	QWidget*        m_cornerPopup         {nullptr};
	QDoubleSpinBox* m_cornerTLSpin        {nullptr};
	QDoubleSpinBox* m_cornerTRSpin        {nullptr};
	QDoubleSpinBox* m_cornerBLSpin        {nullptr};
	QDoubleSpinBox* m_cornerBRSpin        {nullptr};
	QCheckBox*      m_cornerLinkChk       {nullptr};
	QToolButton*    m_featherBtn           {nullptr};
	QToolButton*    m_textFeatherBtn       {nullptr};
	QToolButton*    m_textBoxBtn           {nullptr};
	// Paragraph Shading — same QWidget+Qt::Popup pattern as m_cornerPopup,
	// hosting the Style Manager's own shading page so the two cannot drift.
	QToolButton*    m_shadingBtn           {nullptr};
	QWidget*        m_shadingPopup         {nullptr};
	SMPShadeWidget* m_shadeWidget          {nullptr};
	QPushButton*    m_shadingResetBtn      {nullptr};
	// The controls Tab walks, in order. Esc/Enter are intercepted for exactly
	// these, however focus arrived — keying off the shortcut instead would mean
	// Esc worked after Ctrl+Shift+F but silently did nothing after a mouse
	// click into the same field.
	QList<QWidget*> m_focusChain;
	// Guards re-entry while the popup is being loaded from the selection.
	bool            m_shadingLoading       {false};
	// Which controls the user actually moved during this popup session. Only
	// these are written back, so every selected paragraph keeps inheriting the
	// rest from ITS OWN style — which is what makes a selection spanning
	// several different paragraph styles behave sanely.
	//
	// Tracked here rather than read from SMPShadeWidget::useParentValue(),
	// which is a one-shot consuming read (it clears its own flag): fine for the
	// Style Manager's single write-back, useless for live preview.
	enum ShadeAttr
	{
		SA_On = 0, SA_Color, SA_Tint, SA_WidthType,
		SA_PadTop, SA_PadBottom, SA_PadLeft, SA_PadRight,
		SA_Radius, SA_Merge, SA_COUNT
	};
	bool            m_shadeTouched[SA_COUNT] {};
	// One undo step per popup session: opened on show, committed on close, so a
	// dozen spinbox ticks collapse into a single "Paragraph Shading" entry.
	UndoTransaction m_shadingTrans;
	bool            m_shadingTouched       {false};
	QCheckBox*      m_borderTopChk         {nullptr};
	QCheckBox*      m_borderBottomChk      {nullptr};
	QCheckBox*      m_borderLeftChk        {nullptr};
	QCheckBox*      m_borderRightChk       {nullptr};
	QDoubleSpinBox* m_borderInsetSpin      {nullptr};
	QWidget*        m_featherPopup         {nullptr};
	QDoubleSpinBox* m_featherSpin          {nullptr};
	QToolButton*    m_featherAllBtn        {nullptr};
	QToolButton*    m_featherTopBtn        {nullptr};
	QToolButton*    m_featherBottomBtn     {nullptr};
	QToolButton*    m_featherLeftBtn       {nullptr};
	QToolButton*    m_featherRightBtn      {nullptr};

	// ── Text-frame gap buttons (row2) ──────────────────────────────
	QToolButton*    m_textPadAllPlusBtn    {nullptr};
	QToolButton*    m_textPadAllMinusBtn   {nullptr};
	QToolButton*    m_textPadResetBtn      {nullptr};
	QToolButton*    m_textPadTopPlusBtn    {nullptr};
	QToolButton*    m_textPadTopMinusBtn   {nullptr};
	QToolButton*    m_textPadBottomPlusBtn {nullptr};
	QToolButton*    m_textPadBottomMinusBtn{nullptr};
	QToolButton*    m_textPadLeftPlusBtn   {nullptr};
	QToolButton*    m_textPadLeftMinusBtn  {nullptr};
	QToolButton*    m_textPadRightPlusBtn  {nullptr};
	QToolButton*    m_textPadRightMinusBtn {nullptr};
	// Session-wide toggle: checked → pad buttons adjust internal text distance
	// (textToFrameDist*); unchecked (default) → external wrap boundary (wrapOffset*)
	QCheckBox*      m_internalPadChk       {nullptr};

	// Unused — kept for compat
	QComboBox*      m_paraStyleCombo      {nullptr};

	// ── Line ─────────────────────────────────────────────────────
	ColorButton*    m_fillColorBtn         {nullptr};
	ColorButton*    m_imgFillColorBtn      {nullptr};
	ColorButton*    m_imgLineColorBtn      {nullptr};
	QDoubleSpinBox* m_fillOpacitySpin      {nullptr};
	QDoubleSpinBox* m_imgLineOpacitySpin   {nullptr};
	QDoubleSpinBox* m_imgWidthSpin         {nullptr};
	QDoubleSpinBox* m_imgHeightSpin        {nullptr};
	QComboBox*      m_imgLineStyleCombo    {nullptr};
	QToolButton*    m_imgLineStyleEditBtn  {nullptr};
	QToolButton*    m_imgLineStyleAddBtn   {nullptr};
	ColorButton*    m_lineColorBtn         {nullptr};
	ColorButton*    m_lineMaskBtn          {nullptr};
	QComboBox*      m_lineNamedStyleCombo  {nullptr};
	QToolButton*    m_lineStyleEditBtn     {nullptr};
	QToolButton*    m_lineStyleNewBtn      {nullptr};
	QLabel*         m_lineWidthIconLbl     {nullptr};
	QDoubleSpinBox* m_lineWidthSpin        {nullptr};
	QComboBox*      m_lineStyleCombo       {nullptr};

	// Text Frame border controls
	QDoubleSpinBox* m_textLineWidthSpin    {nullptr};
	QDoubleSpinBox* m_imgLineWidthSpin     {nullptr};
	QComboBox*      m_textLineStyleCombo   {nullptr};
	QToolButton*    m_lineCapFlatBtn       {nullptr};
	QToolButton*    m_lineCapRoundBtn      {nullptr};
	QToolButton*    m_lineCapSquareBtn     {nullptr};
	QToolButton*    m_lineJoinMiterBtn     {nullptr};
	QToolButton*    m_lineJoinRoundBtn     {nullptr};
	QToolButton*    m_lineJoinBevelBtn     {nullptr};
	QDoubleSpinBox* m_lineOpacitySpin      {nullptr};
	QComboBox*      m_lineStartArrowCombo  {nullptr};
	QComboBox*      m_lineEndArrowCombo    {nullptr};
	QDoubleSpinBox* m_lineDashOffsetSpin   {nullptr};
	PropertiesPalette_Line* m_linePalette {nullptr};
	QList<QWidget*> m_lineWidgets;

	QList<QWidget*> m_textWidgets;
	QList<QWidget*> m_imageWidgets;
	QList<QWidget*> m_textWrapWidgets;

	// Align and Distribute toggle, pinned to the right end of the bar for
	// every selection type. A toolbar action, not a row widget, so the
	// show/hide passes never touch it and QToolBar moves it into the ">>"
	// menu when the window is too narrow.
	QAction* m_alignDistributeAction {nullptr};
	bool     m_alignPaletteConnected {false};
	//! The palette is open and in front (not closed, not behind another tab).
	bool alignPaletteShowing() const;
	//! Visible only with a selection; checked while the palette is showing.
	void updateAlignDistributeButton();
	void onAlignDistributeClicked();
	//! Put the floating palette under the button instead of over the page.
	void placeAlignPaletteByButton();

	// ── Group editing ────────────────────────────────────────────
	// One group selected: the bar shows the toolbar of what is INSIDE it, and
	// every slot acts on m_groupSel (the children of the chosen kind, nested
	// groups included) instead of the document selection. The document
	// selection itself is never touched, so the group stays selected, grouped
	// and in place.
	enum GroupKind { GK_None = -1, GK_Image = 0, GK_Text, GK_Line, GK_Shape, GK_COUNT };
	friend struct SuneerGroupUndo;
	QWidget*   m_groupEditWidget {nullptr};   // "Edit:" + combo, first in row 1
	QComboBox* m_groupEditCombo  {nullptr};
	Selection* m_groupSel        {nullptr};   // non-GUI; empty unless m_groupActive
	bool       m_groupActive     {false};
	int        m_groupChoice     {GK_None};   // last kind picked, kept for the session
	QPointer<PageItem> m_groupItem;
	QList<QAbstractSpinBox*> m_mixedSpins;
	static int groupKindOf(const PageItem* item);
	//! Sets up group mode for \a item; returns the item the bar should display.
	PageItem* resolveGroupTarget(PageItem* item);
	//! Blanks the fields whose value differs between the group's children.
	void showGroupMixedValues();
	void setSpinMixed(QAbstractSpinBox* sb);
	//! What the slots act on: the group's children in group mode, else the document selection.
	Selection* sel() const;
	//! Same, as a customSelection argument (nullptr = document selection).
	Selection* tsel() const { return m_groupActive ? m_groupSel : nullptr; }
	//! Group mode: every child of the chosen kind. Otherwise the first selected item only.
	QList<PageItem*> targetItems() const;
	//! What flow mode, wrap distance, flip and rotation act on: always the document selection (the group itself).
	Selection* wrapSel() const;
	//! Group with ONE frame of the chosen kind: selects that frame, for tools that need a canvas mode.
	void enterSoleGroupChild();
	//! Re-reads the bar after a group change without disturbing a field being typed in.
	void refreshAfterGroupChange();
	QToolButton* m_imgResizeBtn {nullptr};
};

#endif
