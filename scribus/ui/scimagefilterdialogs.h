/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SCIMAGEFILTERDIALOGS_H
#define SCIMAGEFILTERDIALOGS_H

#include <functional>

#include <QColor>
#include <QDialog>

#include "scribusapi.h"
#include "scimagestructs.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QSlider;
class QTimer;
class QVBoxLayout;

class CurveWidget;
class ColorCombo;
class ShadeButton;
class ScribusDoc;

/*!
 \brief Reusable base class for the Photoshop-style filter dialogs used by
        ScImageEditor.

 Layout is two-column: parameter widgets on the left, a fixed-width button strip
 on the right ([OK] [Cancel] [Auto] [Reset] … ☑ Preview at the bottom). A
 subclass typically just adds a few addSliderRow() rows and overrides
 buildEffects() (and optionally resetToDefaults()/computeAuto()); everything
 else — the live debounced preview, the button strip, and window-position
 persistence — is inherited.
 */
class SCRIBUS_API FilterParamDialog : public QDialog
{
	Q_OBJECT

public:
	explicit FilterParamDialog(const QString& title, QWidget* parent = nullptr);

	//! The effect(s) described by the dialog's current widget state.
	virtual ScImageEffectList buildEffects() const = 0;

	//! Seed the widgets from an existing effect (used by Edit). Default: no-op.
	virtual void loadFromEffect(const ImageEffect& effect) { Q_UNUSED(effect) }

	//! Restore the dialog's parameters to their defaults (Reset button).
	virtual void resetToDefaults() {}

	//! Compute automatic parameters from \a source (Auto button). Optional.
	virtual void computeAuto(const QImage& source) { Q_UNUSED(source) }

	bool previewEnabled() const;

	//! Fire the first preview once the caller has wired the callbacks.
	void requestInitialPreview();

	//! The base image the filter is applied to — context for Auto and preview.
	void setSourceImage(const QImage& image) { m_sourceImage = image; }
	const QImage& sourceImage() const { return m_sourceImage; }

	// Wired by ScImageEditor: live preview / restore-on-preview-off.
	std::function<void(const ScImageEffectList&)> onPreview;
	std::function<void()> onPreviewOff;

public slots:
	//! Any parameter widget change routes here; schedules a debounced preview.
	void paramsChanged();

protected:
	//! Left-column layout where subclasses add parameter rows.
	QVBoxLayout* contentLayout() const { return m_content; }
	//! Build the right-hand button strip + Preview; call at end of subclass ctor.
	void finalizeLayout();
	//! Show the optional Auto button (call when the subclass overrides computeAuto).
	void enableAuto(bool on);

	/*! Add a "[Label:] [slider] [spinbox] [suffix]" row to the left column, with
	    two-way slider/spinbox sync already wired and change routed to the live
	    preview. Returns the slider — read its value() in buildEffects(). */
	QSlider* addSliderRow(const QString& label, int min, int max, int defaultVal, const QString& suffix = QString());

	void showEvent(QShowEvent* event) override;
	void done(int result) override;

private slots:
	void onPreviewToggled(bool on);
	void onResetClicked();
	void onAutoClicked();
	void firePreview();

private:
	QString settingsKey() const;

	QHBoxLayout* m_mainLayout { nullptr };
	QVBoxLayout* m_content { nullptr };
	QCheckBox*   m_previewCheck { nullptr };
	QPushButton* m_okBtn { nullptr };
	QPushButton* m_cancelBtn { nullptr };
	QPushButton* m_autoBtn { nullptr };
	QPushButton* m_resetBtn { nullptr };
	QTimer*      m_debounce { nullptr };
	QImage       m_sourceImage;
	bool         m_geometryRestored { false };
};

// ── Blur ────────────────────────────────────────────────────────────────────
class SCRIBUS_API GaussianBlurDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit GaussianBlurDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QSpinBox* m_radius { nullptr };
	QSlider*  m_radiusSlider { nullptr };
};

// ── Motion Blur ───────────────────────────────────────────────────────────
class SCRIBUS_API MotionBlurDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit MotionBlurDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QSlider* m_angle { nullptr };
	QSlider* m_distance { nullptr };
};

// ── Radial Blur ───────────────────────────────────────────────────────────
class SCRIBUS_API RadialBlurDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit RadialBlurDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QSlider*   m_amount { nullptr };
	QComboBox* m_mode { nullptr };   // 0 = Spin, 1 = Zoom
};

// ── Box Blur ──────────────────────────────────────────────────────────────
class SCRIBUS_API BoxBlurDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit BoxBlurDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QSlider* m_radius { nullptr };
};

// ── Sharpen ───────────────────────────────────────────────────────────────
class SCRIBUS_API SharpenDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit SharpenDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QDoubleSpinBox* m_radius { nullptr };
	QDoubleSpinBox* m_sigma { nullptr };
};

// ── Brightness / Contrast ────────────────────────────────────────────────
class SCRIBUS_API BrightnessContrastDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	//! onlyCode: -1 = both sliders; EF_BRIGHTNESS or EF_CONTRAST = single slider.
	explicit BrightnessContrastDialog(int onlyCode = -1, QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
	void computeAuto(const QImage& source) override;
private:
	int      m_onlyCode { -1 };
	QSlider* m_brightness { nullptr };
	QSlider* m_contrast { nullptr };
};

// ── Shadows / Highlights ──────────────────────────────────────────────────
class SCRIBUS_API ShadowsHighlightsDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit ShadowsHighlightsDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	QSlider* m_shadowAmount { nullptr };
	QSlider* m_shadowTone { nullptr };
	QSlider* m_highlightAmount { nullptr };
	QSlider* m_highlightTone { nullptr };
	QSlider* m_radius { nullptr };
	QSlider* m_color { nullptr };
	QSlider* m_midtone { nullptr };
};

// ── Levels ────────────────────────────────────────────────────────────────
class SCRIBUS_API LevelsDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit LevelsDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
	void computeAuto(const QImage& source) override;
private:
	QSlider* m_inBlack { nullptr };
	QSlider* m_inWhite { nullptr };
	QSlider* m_gamma { nullptr };    //!< value/100 = gamma (10..999 → 0.10..9.99)
	QSlider* m_outBlack { nullptr };
	QSlider* m_outWhite { nullptr };
};

// ── Hue / Saturation ──────────────────────────────────────────────────────
class SCRIBUS_API HueSaturationDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit HueSaturationDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	QSlider* m_hue { nullptr };
	QSlider* m_saturation { nullptr };
	QSlider* m_lightness { nullptr };
};

// ── Color Balance ─────────────────────────────────────────────────────────
class SCRIBUS_API ColorBalanceDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit ColorBalanceDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	// Shadows / Midtones / Highlights × Cyan-Red / Magenta-Green / Yellow-Blue.
	QSlider*   m_sR { nullptr };
	QSlider*   m_sG { nullptr };
	QSlider*   m_sB { nullptr };
	QSlider*   m_mR { nullptr };
	QSlider*   m_mG { nullptr };
	QSlider*   m_mB { nullptr };
	QSlider*   m_hR { nullptr };
	QSlider*   m_hG { nullptr };
	QSlider*   m_hB { nullptr };
	QCheckBox* m_preserveLum { nullptr };
};

// ── CMYK Adjustments ──────────────────────────────────────────────────────
class SCRIBUS_API CmykAdjustDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit CmykAdjustDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	QSlider* m_cyan { nullptr };
	QSlider* m_magenta { nullptr };
	QSlider* m_yellow { nullptr };
	QSlider* m_black { nullptr };
};

// ── Selective Color ───────────────────────────────────────────────────────
class SCRIBUS_API SelectiveColorDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit SelectiveColorDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	void selectRange(int index);   //!< save current sliders, load the new range's

	QComboBox*    m_range { nullptr };
	QSlider*      m_cyan { nullptr };
	QSlider*      m_magenta { nullptr };
	QSlider*      m_yellow { nullptr };
	QSlider*      m_black { nullptr };
	QRadioButton* m_relative { nullptr };
	QRadioButton* m_absolute { nullptr };
	int m_values[9][4] { };        //!< [range][C,M,Y,K]
	int m_currentRange { 0 };
};

// ── Channel Mixer ─────────────────────────────────────────────────────────
class SCRIBUS_API ChannelMixerDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit ChannelMixerDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	void selectSlot(int slot);     //!< save current sliders, load slot's (0=R,1=G,2=B,3=mono)
	void syncSlidersToState();

	QComboBox* m_output { nullptr };
	QSlider*   m_srcR { nullptr };
	QSlider*   m_srcG { nullptr };
	QSlider*   m_srcB { nullptr };
	QSlider*   m_const { nullptr };
	QCheckBox* m_monochrome { nullptr };
	int m_values[4][4] { };        //!< [slot 0=R,1=G,2=B,3=mono][srcR,srcG,srcB,const]
	int m_currentSlot { 0 };
};

// ── Photo Filter ──────────────────────────────────────────────────────────
class SCRIBUS_API PhotoFilterDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit PhotoFilterDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	void setFilterColor(const QColor& color, bool selectCustomItem);
	void updateSwatch();

	QComboBox* m_preset { nullptr };
	QLabel*    m_swatch { nullptr };
	QSlider*   m_density { nullptr };
	QCheckBox* m_preserveLum { nullptr };
	QColor     m_filterColor { 236, 138, 0 };   //!< default: Warming Filter (85)
};

// ── Threshold ─────────────────────────────────────────────────────────────
class SCRIBUS_API ThresholdDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit ThresholdDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
	void computeAuto(const QImage& source) override;
private:
	QSlider* m_level { nullptr };
};

// ── Black & White ─────────────────────────────────────────────────────────
class SCRIBUS_API BlackWhiteDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit BlackWhiteDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;
private:
	void updateTintUi();

	QSlider*   m_reds { nullptr };
	QSlider*   m_yellows { nullptr };
	QSlider*   m_greens { nullptr };
	QSlider*   m_cyans { nullptr };
	QSlider*   m_blues { nullptr };
	QSlider*   m_magentas { nullptr };
	QCheckBox* m_tint { nullptr };
	QLabel*    m_tintSwatch { nullptr };
	QPushButton* m_tintBtn { nullptr };
	QColor     m_tintColor { 225, 211, 179 };
};

// ── Posterize ─────────────────────────────────────────────────────────────
class SCRIBUS_API PosterizeDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit PosterizeDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	QSpinBox* m_levels { nullptr };
	QSlider*  m_levelsSlider { nullptr };
};

// ── Curves ────────────────────────────────────────────────────────────────
class SCRIBUS_API CurvesDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit CurvesDialog(QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	CurveWidget* m_curve { nullptr };
};

// ── Colorize ──────────────────────────────────────────────────────────────
class SCRIBUS_API ColorizeDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit ColorizeDialog(ScribusDoc* doc, QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	ColorCombo*  m_color { nullptr };
	ShadeButton* m_shade { nullptr };
};

// ── Duotone ───────────────────────────────────────────────────────────────
class SCRIBUS_API DuotoneDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit DuotoneDialog(ScribusDoc* doc, QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	ColorCombo*  m_color1 { nullptr };
	ShadeButton* m_shade1 { nullptr };
	CurveWidget* m_curve1 { nullptr };
	ColorCombo*  m_color2 { nullptr };
	ShadeButton* m_shade2 { nullptr };
	CurveWidget* m_curve2 { nullptr };
};

// ── Tritone ───────────────────────────────────────────────────────────────
class SCRIBUS_API TritoneDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit TritoneDialog(ScribusDoc* doc, QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	ColorCombo*  m_color1 { nullptr };
	ShadeButton* m_shade1 { nullptr };
	CurveWidget* m_curve1 { nullptr };
	ColorCombo*  m_color2 { nullptr };
	ShadeButton* m_shade2 { nullptr };
	CurveWidget* m_curve2 { nullptr };
	ColorCombo*  m_color3 { nullptr };
	ShadeButton* m_shade3 { nullptr };
	CurveWidget* m_curve3 { nullptr };
};

// ── Quadtone ──────────────────────────────────────────────────────────────
class SCRIBUS_API QuadtoneDialog : public FilterParamDialog
{
	Q_OBJECT
public:
	explicit QuadtoneDialog(ScribusDoc* doc, QWidget* parent = nullptr);
	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
private:
	ColorCombo*  m_color1 { nullptr };
	ShadeButton* m_shade1 { nullptr };
	CurveWidget* m_curve1 { nullptr };
	ColorCombo*  m_color2 { nullptr };
	ShadeButton* m_shade2 { nullptr };
	CurveWidget* m_curve2 { nullptr };
	ColorCombo*  m_color3 { nullptr };
	ShadeButton* m_shade3 { nullptr };
	CurveWidget* m_curve3 { nullptr };
	ColorCombo*  m_color4 { nullptr };
	ShadeButton* m_shade4 { nullptr };
	CurveWidget* m_curve4 { nullptr };
};

// ── Free helpers ──────────────────────────────────────────────────────────
//! Create the right dialog for editing \a effect, pre-seeded from it.
//! Returns nullptr for parameterless effects (Invert / Grayscale).
SCRIBUS_API FilterParamDialog* makeFilterDialogForEffect(const ImageEffect& effect, ScribusDoc* doc, QWidget* parent);
//! Human-readable name for an effect code (e.g. "Gaussian Blur").
SCRIBUS_API QString filterEffectName(int effectCode);
//! One-line parameter summary for an effect (e.g. "Radius 3").
SCRIBUS_API QString filterEffectSummary(const ImageEffect& effect);

#endif // SCIMAGEFILTERDIALOGS_H
