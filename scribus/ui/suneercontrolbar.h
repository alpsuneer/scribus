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
#include "ui/widgets/color_button.h"
#include "ui/propertiespalette_line.h"

class ScribusMainWindow;
class ScribusDoc;
class PageItem;

class SuneerControlBar : public QToolBar
{
	Q_OBJECT
public:
	explicit SuneerControlBar(ScribusMainWindow* parent);
	void setDocument(ScribusDoc* doc);

protected:
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
	void updateCaptionFrame(PageItem* imgFrame);
	void languageChange();
	void iconSetChange();

private slots:
	// Row 1
	void onFontChanged(const QFont& font = QFont());
	void onFontSizeChanged(double val);
	void onTextDistChanged();
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
	void onLeftIndentChanged(double val);
	void onRightIndentChanged(double val);
	void onFirstLineIndentChanged(double val);
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
	void showTextWidgets(bool show);
	void showImageWidgets(bool show);
	void showTextWrapWidgets(bool show);
	void updateTextWrapControls(PageItem* item);
	void showLineWidgets(bool show);
	void syncGapSpinsFromTextFrame(PageItem* item);

	ScribusMainWindow* m_scmw {nullptr};
	QPointer<ScribusDoc> m_doc;
	bool m_updating {false};
	bool m_iconsSet {false};

	// ── ROW 1 ─────────────────────────────────
	QComboBox*      m_fontCombo           {nullptr};
	bool            m_fontComboPopupShown  {false};
public:
	void focusFontCombo() { m_fontCombo->setFocus(); m_fontCombo->lineEdit()->selectAll(); }
	QDoubleSpinBox* m_fontSizeSpin        {nullptr};
	AlignSelect*    m_alignSelect         {nullptr};
	StyleSelect*    m_styleSelect         {nullptr};
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
	QLabel*         m_leftIndentIconLbl   {nullptr};
	QDoubleSpinBox* m_leftIndentSpin      {nullptr};
	QLabel*         m_rightIndentIconLbl  {nullptr};
	QDoubleSpinBox* m_rightIndentSpin     {nullptr};
	QLabel*         m_firstLineIndentIconLbl {nullptr};
	QDoubleSpinBox* m_firstLineIndentSpin {nullptr};

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
	QDoubleSpinBox* m_textDistLeftSpin    {nullptr};
	QDoubleSpinBox* m_textDistRightSpin   {nullptr};
	QDoubleSpinBox* m_textDistTopSpin     {nullptr};
	QDoubleSpinBox* m_textDistBottomSpin  {nullptr};
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
};

#endif
