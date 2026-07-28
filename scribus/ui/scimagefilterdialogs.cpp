/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimagefilterdialogs.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVector>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "autocorrectengine.h"
#include "colorcombo.h"
#include "commonstrings.h"
#include "curvewidget.h"
#include "ui/dialogs/autoenhancedialog.h"
#include "fpointarray.h"
#include "scimagefilterengine.h"
#include "scribusdoc.h"
#include "sctextstream.h"
#include "shadebutton.h"

// ── local helpers ────────────────────────────────────────────────────────────

namespace
{
	// A labelled slider + spinbox row, kept in sync. The spinbox is the value
	// source; connect its valueChanged(int) to the dialog's paramsChanged().
	QHBoxLayout* makeIntRow(const QString& label, int lo, int hi, int def,
	                        QSlider*& sliderOut, QSpinBox*& spinOut, QWidget* parent)
	{
		auto* row = new QHBoxLayout();
		auto* lbl = new QLabel(label, parent);
		lbl->setFixedWidth(80);
		lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		auto* slider = new QSlider(Qt::Horizontal, parent);
		slider->setRange(lo, hi);
		slider->setValue(def);
		slider->setMinimumWidth(200);
		slider->setTickPosition(QSlider::NoTicks);
		auto* spin = new QSpinBox(parent);
		spin->setRange(lo, hi);
		spin->setValue(def);
		spin->setFixedWidth(60);
		spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
		QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
		QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
		row->addWidget(lbl);
		row->addWidget(slider, 1);
		row->addWidget(spin);
		sliderOut = slider;
		spinOut = spin;
		return row;
	}

	QString curveToParams(CurveWidget* cw)
	{
		return ImageFilterEngine::formatCurve(cw->cDisplay->getCurve(), cw->cDisplay->isLinear());
	}

	// Read one curve ("numVals x y ... linFlag") from a stream into a widget.
	void loadCurveFromStream(ScTextStream& fp, CurveWidget* cw)
	{
		int numVals = 0;
		fp >> numVals;
		FPointArray curve;
		for (int i = 0; i < numVals; ++i)
		{
			double x = 0.0, y = 0.0;
			fp >> x;
			fp >> y;
			curve.addPoint(x, y);
		}
		int lin = 1;
		fp >> lin;
		if (curve.size() >= 2)
			cw->cDisplay->setCurve(curve);
		cw->setLinear(lin != 0);
	}

	// A titled group holding [ColorCombo] [ShadeButton] and a curve widget, as
	// used by the multi-tone dialogs.
	QGroupBox* makeToneGroup(const QString& title, ScribusDoc* doc,
	                         ColorCombo*& colorOut, ShadeButton*& shadeOut, CurveWidget*& curveOut,
	                         QWidget* parent)
	{
		auto* box = new QGroupBox(title, parent);
		auto* v = new QVBoxLayout(box);
		auto* top = new QHBoxLayout();
		auto* color = new ColorCombo(box);
		if (doc)
			color->setColors(doc->PageColors, false);
		auto* shade = new ShadeButton(box);
		shade->setValue(100);
		top->addWidget(color, 1);
		top->addWidget(shade);
		v->addLayout(top);
		auto* curve = new CurveWidget(box);
		v->addWidget(curve);
		colorOut = color;
		shadeOut = shade;
		curveOut = curve;
		return box;
	}
}

// ── FilterParamDialog ────────────────────────────────────────────────────────

FilterParamDialog::FilterParamDialog(const QString& title, QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(title);
	setModal(true);
	setMinimumWidth(460);   // give sliders room to feel responsive

	// Two-column: [ parameter rows | fixed-width button strip ].
	m_mainLayout = new QHBoxLayout(this);
	m_content = new QVBoxLayout();
	m_content->setSpacing(8);   // 8px between parameter rows
	m_mainLayout->addLayout(m_content, 1);

	// Debounce preview updates so dragging a slider stays smooth.
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(50);
	connect(m_debounce, &QTimer::timeout, this, &FilterParamDialog::firePreview);
}

void FilterParamDialog::finalizeLayout()
{
	auto* strip = new QVBoxLayout();
	strip->setSpacing(6);

	m_okBtn     = new QPushButton(tr("OK"), this);
	m_cancelBtn = new QPushButton(tr("Cancel"), this);
	m_autoBtn   = new QPushButton(tr("Auto"), this);
	m_resetBtn  = new QPushButton(tr("Reset"), this);
	m_okBtn->setDefault(true);
	m_autoBtn->setVisible(false);   // opt-in via enableAuto()

	strip->addWidget(m_okBtn);
	strip->addWidget(m_cancelBtn);
	strip->addWidget(m_autoBtn);
	strip->addWidget(m_resetBtn);
	strip->addStretch(1);

	m_previewCheck = new QCheckBox(tr("Preview"), this);
	m_previewCheck->setChecked(true);
	strip->addWidget(m_previewCheck);

	auto* stripWidget = new QWidget(this);
	stripWidget->setFixedWidth(90);
	stripWidget->setLayout(strip);
	m_mainLayout->addWidget(stripWidget);

	connect(m_okBtn,     &QPushButton::clicked, this, &QDialog::accept);
	connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_resetBtn,  &QPushButton::clicked, this, &FilterParamDialog::onResetClicked);
	connect(m_autoBtn,   &QPushButton::clicked, this, &FilterParamDialog::onAutoClicked);
	connect(m_previewCheck, &QCheckBox::toggled, this, &FilterParamDialog::onPreviewToggled);
}

void FilterParamDialog::enableAuto(bool on)
{
	if (m_autoBtn)
		m_autoBtn->setVisible(on);
}

QSlider* FilterParamDialog::addSliderRow(const QString& label, int min, int max, int defaultVal, const QString& suffix)
{
	auto* row = new QHBoxLayout();

	// 1. Right-aligned, fixed-width label.
	auto* lbl = new QLabel(label, this);
	lbl->setFixedWidth(80);
	lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	// 2. Horizontal slider — the primary control, expands to fill the row.
	auto* slider = new QSlider(Qt::Horizontal, this);
	slider->setMinimum(min);
	slider->setMaximum(max);
	slider->setValue(defaultVal);
	slider->setMinimumWidth(200);
	slider->setTickPosition(QSlider::NoTicks);

	// 3. Secondary numeric spinbox.
	auto* spin = new QSpinBox(this);
	spin->setMinimum(min);
	spin->setMaximum(max);
	spin->setValue(defaultVal);
	spin->setFixedWidth(60);
	spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);

	// Two-way sync + (debounced) live preview.
	connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
	connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
	connect(slider, &QSlider::valueChanged, this, &FilterParamDialog::paramsChanged);

	row->addWidget(lbl);
	row->addWidget(slider, 1);
	row->addWidget(spin);

	// 4. Optional fixed-width suffix label ("%", "°", "px").
	if (!suffix.isEmpty())
	{
		auto* suf = new QLabel(suffix, this);
		suf->setFixedWidth(24);
		row->addWidget(suf);
	}

	m_content->addLayout(row);
	return slider;
}

bool FilterParamDialog::previewEnabled() const
{
	return m_previewCheck && m_previewCheck->isChecked();
}

void FilterParamDialog::requestInitialPreview()
{
	if (previewEnabled() && onPreview)
		onPreview(buildEffects());
}

void FilterParamDialog::paramsChanged()
{
	if (m_debounce)
		m_debounce->start();   // coalesce rapid changes into one preview
	else
		firePreview();
}

void FilterParamDialog::firePreview()
{
	if (previewEnabled() && onPreview)
		onPreview(buildEffects());
}

void FilterParamDialog::onPreviewToggled(bool on)
{
	if (on)
		firePreview();
	else if (onPreviewOff)
		onPreviewOff();
}

void FilterParamDialog::onResetClicked()
{
	resetToDefaults();
	firePreview();
}

void FilterParamDialog::onAutoClicked()
{
	if (!m_sourceImage.isNull())
		computeAuto(m_sourceImage);
	firePreview();
}

QString FilterParamDialog::settingsKey() const
{
	QString key = windowTitle();
	key.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("_"));
	return QStringLiteral("ScImageEditor/FilterDialog/") + key;
}

void FilterParamDialog::showEvent(QShowEvent* event)
{
	if (!m_geometryRestored)
	{
		m_geometryRestored = true;
		QSettings settings;
		const QByteArray geom = settings.value(settingsKey() + QStringLiteral("/geometry")).toByteArray();
		if (!geom.isEmpty())
			restoreGeometry(geom);
	}
	QDialog::showEvent(event);
}

void FilterParamDialog::done(int result)
{
	QSettings settings;
	settings.setValue(settingsKey() + QStringLiteral("/geometry"), saveGeometry());
	QDialog::done(result);
}

// ── GaussianBlurDialog ───────────────────────────────────────────────────────

GaussianBlurDialog::GaussianBlurDialog(QWidget* parent)
	: FilterParamDialog(tr("Gaussian Blur"), parent)
{
	QSlider* slider = nullptr;
	contentLayout()->addLayout(makeIntRow(tr("Radius:"), 0, 30, 2, slider, m_radius, this));
	connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged), this, &FilterParamDialog::paramsChanged);
	finalizeLayout();
}

ScImageEffectList GaussianBlurDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeBlur(m_radius->value()));
	return list;
}

void GaussianBlurDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	double radius = 0.0;
	fp >> radius;
	m_radius->setValue(qRound(radius));
}

// ── MotionBlurDialog ─────────────────────────────────────────────────────────

MotionBlurDialog::MotionBlurDialog(QWidget* parent)
	: FilterParamDialog(tr("Motion Blur"), parent)
{
	m_angle    = addSliderRow(tr("Angle:"), 0, 360, 0, QStringLiteral("°"));
	m_distance = addSliderRow(tr("Distance:"), 1, 100, 10, QStringLiteral("px"));
	finalizeLayout();
}

ScImageEffectList MotionBlurDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeMotionBlur(m_angle->value(), m_distance->value()));
	return list;
}

void MotionBlurDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int angle = 0, distance = 10;
	fp >> angle; fp >> distance;
	m_angle->setValue(angle);
	m_distance->setValue(distance);
}

// ── RadialBlurDialog ─────────────────────────────────────────────────────────

RadialBlurDialog::RadialBlurDialog(QWidget* parent)
	: FilterParamDialog(tr("Radial Blur"), parent)
{
	m_amount = addSliderRow(tr("Amount:"), 1, 100, 10);

	auto* modeRow = new QHBoxLayout();
	auto* modeLbl = new QLabel(tr("Method:"), this);
	modeLbl->setFixedWidth(80);
	modeLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_mode = new QComboBox(this);
	m_mode->addItems({ tr("Spin"), tr("Zoom") });
	modeRow->addWidget(modeLbl);
	modeRow->addWidget(m_mode, 1);
	contentLayout()->addLayout(modeRow);

	connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FilterParamDialog::paramsChanged);
	finalizeLayout();
}

ScImageEffectList RadialBlurDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeRadialBlur(m_amount->value(), m_mode->currentIndex()));
	return list;
}

void RadialBlurDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int amount = 10, mode = 0;
	fp >> amount; fp >> mode;
	m_amount->setValue(amount);
	m_mode->setCurrentIndex(qBound(0, mode, 1));
}

// ── BoxBlurDialog ────────────────────────────────────────────────────────────

BoxBlurDialog::BoxBlurDialog(QWidget* parent)
	: FilterParamDialog(tr("Box Blur"), parent)
{
	m_radius = addSliderRow(tr("Radius:"), 1, 50, 3, QStringLiteral("px"));
	finalizeLayout();
}

ScImageEffectList BoxBlurDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeBoxBlur(m_radius->value()));
	return list;
}

void BoxBlurDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int radius = 3;
	fp >> radius;
	m_radius->setValue(radius);
}

// ── ShadowsHighlightsDialog ──────────────────────────────────────────────────

ShadowsHighlightsDialog::ShadowsHighlightsDialog(QWidget* parent)
	: FilterParamDialog(tr("Shadows/Highlights"), parent)
{
	auto addGroupLabel = [this](const QString& text)
	{
		auto* lbl = new QLabel(text, this);
		QFont f = lbl->font();
		f.setBold(true);
		lbl->setFont(f);
		contentLayout()->addWidget(lbl);
	};

	addGroupLabel(tr("Shadows"));
	m_shadowAmount = addSliderRow(tr("Amount:"), 0, 100, 35, QStringLiteral("%"));
	m_shadowTone   = addSliderRow(tr("Tone:"), 0, 100, 50, QStringLiteral("%"));

	addGroupLabel(tr("Highlights"));
	m_highlightAmount = addSliderRow(tr("Amount:"), 0, 100, 0, QStringLiteral("%"));
	m_highlightTone   = addSliderRow(tr("Tone:"), 0, 100, 50, QStringLiteral("%"));

	addGroupLabel(tr("Adjustments"));
	m_radius  = addSliderRow(tr("Radius:"), 0, 200, 30, QStringLiteral("px"));
	m_color   = addSliderRow(tr("Color:"), -100, 100, 20);
	m_midtone = addSliderRow(tr("Midtone:"), -100, 100, 0);

	finalizeLayout();
}

ScImageEffectList ShadowsHighlightsDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeShadowsHighlights(m_shadowAmount->value(), m_shadowTone->value(),
		m_highlightAmount->value(), m_highlightTone->value(), m_radius->value(),
		m_color->value(), m_midtone->value()));
	return list;
}

void ShadowsHighlightsDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int sAmt = 35, sTone = 50, hAmt = 0, hTone = 50, radius = 30, color = 20, midtone = 0;
	fp >> sAmt; fp >> sTone; fp >> hAmt; fp >> hTone; fp >> radius; fp >> color; fp >> midtone;
	m_shadowAmount->setValue(sAmt);
	m_shadowTone->setValue(sTone);
	m_highlightAmount->setValue(hAmt);
	m_highlightTone->setValue(hTone);
	m_radius->setValue(radius);
	m_color->setValue(color);
	m_midtone->setValue(midtone);
}

void ShadowsHighlightsDialog::resetToDefaults()
{
	m_shadowAmount->setValue(35);
	m_shadowTone->setValue(50);
	m_highlightAmount->setValue(0);
	m_highlightTone->setValue(50);
	m_radius->setValue(30);
	m_color->setValue(20);
	m_midtone->setValue(0);
}

// ── SharpenDialog ────────────────────────────────────────────────────────────

SharpenDialog::SharpenDialog(QWidget* parent)
	: FilterParamDialog(tr("Sharpen"), parent)
{
	auto* row1 = new QHBoxLayout();
	auto* l1 = new QLabel(tr("Radius:"), this);
	l1->setMinimumWidth(90);
	m_radius = new QDoubleSpinBox(this);
	m_radius->setRange(0.0, 10.0);
	m_radius->setSingleStep(0.1);
	m_radius->setValue(0.0);
	row1->addWidget(l1);
	row1->addWidget(m_radius, 1);
	contentLayout()->addLayout(row1);

	auto* row2 = new QHBoxLayout();
	auto* l2 = new QLabel(tr("Sigma:"), this);
	l2->setMinimumWidth(90);
	m_sigma = new QDoubleSpinBox(this);
	m_sigma->setRange(0.1, 10.0);
	m_sigma->setSingleStep(0.1);
	m_sigma->setValue(1.0);
	row2->addWidget(l2);
	row2->addWidget(m_sigma, 1);
	contentLayout()->addLayout(row2);

	connect(m_radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &FilterParamDialog::paramsChanged);
	connect(m_sigma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &FilterParamDialog::paramsChanged);
	finalizeLayout();
}

ScImageEffectList SharpenDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeSharpen(m_radius->value(), m_sigma->value()));
	return list;
}

void SharpenDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	double radius = 0.0, sigma = 1.0;
	fp >> radius;
	fp >> sigma;
	m_radius->setValue(radius);
	m_sigma->setValue(sigma);
}

// ── BrightnessContrastDialog ─────────────────────────────────────────────────

BrightnessContrastDialog::BrightnessContrastDialog(int onlyCode, QWidget* parent)
	: FilterParamDialog(onlyCode == ImageEffect::EF_CONTRAST ? tr("Contrast")
	                    : onlyCode == ImageEffect::EF_BRIGHTNESS ? tr("Brightness")
	                    : tr("Brightness/Contrast"), parent),
	  m_onlyCode(onlyCode)
{
	if (onlyCode != ImageEffect::EF_CONTRAST)
		m_brightness = addSliderRow(tr("Brightness:"), -150, 150, 0);
	if (onlyCode != ImageEffect::EF_BRIGHTNESS)
		m_contrast = addSliderRow(tr("Contrast:"), -100, 100, 0);
	finalizeLayout();
	if (m_brightness && m_contrast)   // Auto is only meaningful for the combined dialog
		enableAuto(true);
}

ScImageEffectList BrightnessContrastDialog::buildEffects() const
{
	ScImageEffectList list;
	if (m_brightness && m_brightness->value() != 0)
		list.append(ImageFilterEngine::makeBrightness(m_brightness->value()));
	if (m_contrast && m_contrast->value() != 0)
		list.append(ImageFilterEngine::makeContrast(m_contrast->value()));
	return list;
}

void BrightnessContrastDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int v = 0;
	fp >> v;
	if (effect.effectCode == ImageEffect::EF_CONTRAST && m_contrast)
		m_contrast->setValue(v);
	else if (m_brightness)
		m_brightness->setValue(v);
	else if (m_contrast)
		m_contrast->setValue(v);
}

void BrightnessContrastDialog::resetToDefaults()
{
	if (m_brightness)
		m_brightness->setValue(0);
	if (m_contrast)
		m_contrast->setValue(0);
}

void BrightnessContrastDialog::computeAuto(const QImage& source)
{
	if (source.isNull())
		return;
	// Mean luminance on a downsampled copy; shift brightness so the mean → 128.
	QImage img = source;
	const int maxDim = 256;
	if (img.width() > maxDim || img.height() > maxDim)
		img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::FastTransformation);
	qulonglong sum = 0, n = 0;
	for (int y = 0; y < img.height(); ++y)
	{
		for (int x = 0; x < img.width(); ++x)
		{
			QColor c = img.pixelColor(x, y);
			sum += static_cast<qulonglong>(qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()));
			++n;
		}
	}
	if (n == 0)
		return;
	const int avg = static_cast<int>(sum / n);
	if (m_brightness)
		m_brightness->setValue(qBound(-150, 128 - avg, 150));
	if (m_contrast)
		m_contrast->setValue(0);
}

// ── LevelsDialog ─────────────────────────────────────────────────────────────

LevelsDialog::LevelsDialog(QWidget* parent)
	: FilterParamDialog(tr("Levels"), parent)
{
	m_inBlack  = addSliderRow(tr("Input Black:"), 0, 254, 0);
	m_inWhite  = addSliderRow(tr("Input White:"), 1, 255, 255);
	m_gamma    = addSliderRow(tr("Gamma:"), 10, 999, 100);   // /100 → 0.10..9.99
	m_outBlack = addSliderRow(tr("Output Black:"), 0, 255, 0);
	m_outWhite = addSliderRow(tr("Output White:"), 0, 255, 255);
	finalizeLayout();
	enableAuto(true);
}

ScImageEffectList LevelsDialog::buildEffects() const
{
	int inB = m_inBlack->value();
	int inW = m_inWhite->value();
	if (inW <= inB)
		inW = inB + 1;
	const double gamma = m_gamma->value() / 100.0;
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeLevels(inB, inW, gamma, m_outBlack->value(), m_outWhite->value()));
	return list;
}

void LevelsDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int inB = 0, inW = 255, outB = 0, outW = 255;
	double gamma = 1.0;
	fp >> inB;
	fp >> inW;
	fp >> gamma;
	fp >> outB;
	fp >> outW;
	m_inBlack->setValue(inB);
	m_inWhite->setValue(inW);
	m_gamma->setValue(qBound(10, qRound(gamma * 100.0), 999));
	m_outBlack->setValue(outB);
	m_outWhite->setValue(outW);
}

void LevelsDialog::resetToDefaults()
{
	m_inBlack->setValue(0);
	m_inWhite->setValue(255);
	m_gamma->setValue(100);
	m_outBlack->setValue(0);
	m_outWhite->setValue(255);
}

void LevelsDialog::computeAuto(const QImage& source)
{
	if (source.isNull())
		return;
	// Auto-levels: stretch the luminance range to full [0,255].
	QImage img = source;
	const int maxDim = 256;
	if (img.width() > maxDim || img.height() > maxDim)
		img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::FastTransformation);
	int lo = 255, hi = 0;
	for (int y = 0; y < img.height(); ++y)
	{
		for (int x = 0; x < img.width(); ++x)
		{
			QColor c = img.pixelColor(x, y);
			const int lum = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
			lo = qMin(lo, lum);
			hi = qMax(hi, lum);
		}
	}
	if (hi <= lo)
		return;
	m_inBlack->setValue(qBound(0, lo, 254));
	m_inWhite->setValue(qBound(1, hi, 255));
	m_gamma->setValue(100);
	m_outBlack->setValue(0);
	m_outWhite->setValue(255);
}

// ── HueSaturationDialog ──────────────────────────────────────────────────────

HueSaturationDialog::HueSaturationDialog(QWidget* parent)
	: FilterParamDialog(tr("Hue/Saturation"), parent)
{
	m_hue        = addSliderRow(tr("Hue:"), -180, 180, 0, QStringLiteral("°"));
	m_saturation = addSliderRow(tr("Saturation:"), -100, 100, 0);
	m_lightness  = addSliderRow(tr("Lightness:"), -100, 100, 0);
	finalizeLayout();
}

ScImageEffectList HueSaturationDialog::buildEffects() const
{
	ScImageEffectList list;
	if (m_hue->value() != 0 || m_saturation->value() != 0 || m_lightness->value() != 0)
		list.append(ImageFilterEngine::makeHueSaturation(m_hue->value(), m_saturation->value(), m_lightness->value()));
	return list;
}

void HueSaturationDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int hue = 0, sat = 0, light = 0;
	fp >> hue;
	fp >> sat;
	fp >> light;
	m_hue->setValue(hue);
	m_saturation->setValue(sat);
	m_lightness->setValue(light);
}

void HueSaturationDialog::resetToDefaults()
{
	m_hue->setValue(0);
	m_saturation->setValue(0);
	m_lightness->setValue(0);
}

// ── ColorBalanceDialog ───────────────────────────────────────────────────────

ColorBalanceDialog::ColorBalanceDialog(QWidget* parent)
	: FilterParamDialog(tr("Color Balance"), parent)
{
	auto addGroupLabel = [this](const QString& text)
	{
		auto* lbl = new QLabel(text, this);
		QFont f = lbl->font();
		f.setBold(true);
		lbl->setFont(f);
		contentLayout()->addWidget(lbl);
	};

	addGroupLabel(tr("Shadows"));
	m_sR = addSliderRow(tr("Cyan / Red:"), -100, 100, 0);
	m_sG = addSliderRow(tr("Magenta / Green:"), -100, 100, 0);
	m_sB = addSliderRow(tr("Yellow / Blue:"), -100, 100, 0);

	addGroupLabel(tr("Midtones"));
	m_mR = addSliderRow(tr("Cyan / Red:"), -100, 100, 0);
	m_mG = addSliderRow(tr("Magenta / Green:"), -100, 100, 0);
	m_mB = addSliderRow(tr("Yellow / Blue:"), -100, 100, 0);

	addGroupLabel(tr("Highlights"));
	m_hR = addSliderRow(tr("Cyan / Red:"), -100, 100, 0);
	m_hG = addSliderRow(tr("Magenta / Green:"), -100, 100, 0);
	m_hB = addSliderRow(tr("Yellow / Blue:"), -100, 100, 0);

	m_preserveLum = new QCheckBox(tr("Preserve Luminosity"), this);
	m_preserveLum->setChecked(true);
	contentLayout()->addWidget(m_preserveLum);
	connect(m_preserveLum, &QCheckBox::toggled, this, &FilterParamDialog::paramsChanged);

	finalizeLayout();
}

ScImageEffectList ColorBalanceDialog::buildEffects() const
{
	const int vals[9] = {
		m_sR->value(), m_sG->value(), m_sB->value(),
		m_mR->value(), m_mG->value(), m_mB->value(),
		m_hR->value(), m_hG->value(), m_hB->value()
	};
	bool any = false;
	for (int v : vals)
		if (v != 0)
			any = true;
	ScImageEffectList list;
	if (any)
		list.append(ImageFilterEngine::makeColorBalance(vals[0], vals[1], vals[2], vals[3], vals[4],
			vals[5], vals[6], vals[7], vals[8], m_preserveLum->isChecked()));
	return list;
}

void ColorBalanceDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int sr = 0, sg = 0, sb = 0, mr = 0, mg = 0, mb = 0, hr = 0, hg = 0, hb = 0, preserve = 1;
	fp >> sr; fp >> sg; fp >> sb;
	fp >> mr; fp >> mg; fp >> mb;
	fp >> hr; fp >> hg; fp >> hb;
	fp >> preserve;
	m_sR->setValue(sr); m_sG->setValue(sg); m_sB->setValue(sb);
	m_mR->setValue(mr); m_mG->setValue(mg); m_mB->setValue(mb);
	m_hR->setValue(hr); m_hG->setValue(hg); m_hB->setValue(hb);
	m_preserveLum->setChecked(preserve != 0);
}

void ColorBalanceDialog::resetToDefaults()
{
	for (QSlider* sl : { m_sR, m_sG, m_sB, m_mR, m_mG, m_mB, m_hR, m_hG, m_hB })
		sl->setValue(0);
	m_preserveLum->setChecked(true);
}

// ── CmykAdjustDialog ─────────────────────────────────────────────────────────

CmykAdjustDialog::CmykAdjustDialog(QWidget* parent)
	: FilterParamDialog(tr("CMYK Adjustments"), parent)
{
	m_cyan    = addSliderRow(tr("Cyan:"), -100, 100, 0, QStringLiteral("%"));
	m_magenta = addSliderRow(tr("Magenta:"), -100, 100, 0, QStringLiteral("%"));
	m_yellow  = addSliderRow(tr("Yellow:"), -100, 100, 0, QStringLiteral("%"));
	m_black   = addSliderRow(tr("Black:"), -100, 100, 0, QStringLiteral("%"));
	finalizeLayout();
}

ScImageEffectList CmykAdjustDialog::buildEffects() const
{
	ScImageEffectList list;
	if (m_cyan->value() != 0 || m_magenta->value() != 0 || m_yellow->value() != 0 || m_black->value() != 0)
		list.append(ImageFilterEngine::makeCmykAdjust(m_cyan->value(), m_magenta->value(), m_yellow->value(), m_black->value()));
	return list;
}

void CmykAdjustDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int c = 0, m = 0, y = 0, k = 0;
	fp >> c; fp >> m; fp >> y; fp >> k;
	m_cyan->setValue(c);
	m_magenta->setValue(m);
	m_yellow->setValue(y);
	m_black->setValue(k);
}

void CmykAdjustDialog::resetToDefaults()
{
	m_cyan->setValue(0);
	m_magenta->setValue(0);
	m_yellow->setValue(0);
	m_black->setValue(0);
}

// ── SelectiveColorDialog ─────────────────────────────────────────────────────

SelectiveColorDialog::SelectiveColorDialog(QWidget* parent)
	: FilterParamDialog(tr("Selective Color"), parent)
{
	// Colour-range selector (order must match ScImage::selectiveColor).
	auto* rangeRow = new QHBoxLayout();
	auto* rangeLbl = new QLabel(tr("Colors:"), this);
	rangeLbl->setFixedWidth(80);
	rangeLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_range = new QComboBox(this);
	m_range->addItems({ tr("Reds"), tr("Yellows"), tr("Greens"), tr("Cyans"), tr("Blues"),
	                    tr("Magentas"), tr("Whites"), tr("Neutrals"), tr("Blacks") });
	rangeRow->addWidget(rangeLbl);
	rangeRow->addWidget(m_range, 1);
	contentLayout()->addLayout(rangeRow);

	m_cyan    = addSliderRow(tr("Cyan:"), -100, 100, 0, QStringLiteral("%"));
	m_magenta = addSliderRow(tr("Magenta:"), -100, 100, 0, QStringLiteral("%"));
	m_yellow  = addSliderRow(tr("Yellow:"), -100, 100, 0, QStringLiteral("%"));
	m_black   = addSliderRow(tr("Black:"), -100, 100, 0, QStringLiteral("%"));

	// Relative / Absolute mode.
	auto* modeRow = new QHBoxLayout();
	modeRow->addWidget(new QLabel(tr("Method:"), this));
	m_relative = new QRadioButton(tr("Relative"), this);
	m_absolute = new QRadioButton(tr("Absolute"), this);
	m_relative->setChecked(true);
	modeRow->addWidget(m_relative);
	modeRow->addWidget(m_absolute);
	modeRow->addStretch(1);
	contentLayout()->addLayout(modeRow);

	connect(m_range, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){ selectRange(idx); });
	connect(m_relative, &QRadioButton::toggled, this, &FilterParamDialog::paramsChanged);

	finalizeLayout();
}

void SelectiveColorDialog::selectRange(int index)
{
	if (index < 0 || index >= 9)
		return;
	// Save the sliders of the range we are leaving.
	m_values[m_currentRange][0] = m_cyan->value();
	m_values[m_currentRange][1] = m_magenta->value();
	m_values[m_currentRange][2] = m_yellow->value();
	m_values[m_currentRange][3] = m_black->value();
	// Load the newly-selected range.
	m_currentRange = index;
	m_cyan->setValue(m_values[index][0]);
	m_magenta->setValue(m_values[index][1]);
	m_yellow->setValue(m_values[index][2]);
	m_black->setValue(m_values[index][3]);
}

ScImageEffectList SelectiveColorDialog::buildEffects() const
{
	// Merge live slider values (current range) with the stored per-range values.
	QVector<int> vals(36, 0);
	bool any = false;
	for (int rng = 0; rng < 9; ++rng)
	{
		int c, m, y, k;
		if (rng == m_currentRange)
		{
			c = m_cyan->value(); m = m_magenta->value(); y = m_yellow->value(); k = m_black->value();
		}
		else
		{
			c = m_values[rng][0]; m = m_values[rng][1]; y = m_values[rng][2]; k = m_values[rng][3];
		}
		vals[rng * 4 + 0] = c;
		vals[rng * 4 + 1] = m;
		vals[rng * 4 + 2] = y;
		vals[rng * 4 + 3] = k;
		if (c || m || y || k)
			any = true;
	}
	ScImageEffectList list;
	if (any)
		list.append(ImageFilterEngine::makeSelectiveColor(vals, m_relative->isChecked()));
	return list;
}

void SelectiveColorDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	for (int rng = 0; rng < 9; ++rng)
		for (int ch = 0; ch < 4; ++ch)
		{
			int v = 0;
			fp >> v;
			m_values[rng][ch] = v;
		}
	int relative = 1;
	fp >> relative;
	m_relative->setChecked(relative != 0);
	m_absolute->setChecked(relative == 0);
	m_currentRange = m_range->currentIndex();
	if (m_currentRange < 0)
		m_currentRange = 0;
	m_cyan->setValue(m_values[m_currentRange][0]);
	m_magenta->setValue(m_values[m_currentRange][1]);
	m_yellow->setValue(m_values[m_currentRange][2]);
	m_black->setValue(m_values[m_currentRange][3]);
}

void SelectiveColorDialog::resetToDefaults()
{
	for (int rng = 0; rng < 9; ++rng)
		for (int ch = 0; ch < 4; ++ch)
			m_values[rng][ch] = 0;
	m_cyan->setValue(0);
	m_magenta->setValue(0);
	m_yellow->setValue(0);
	m_black->setValue(0);
	m_relative->setChecked(true);
}

// ── ChannelMixerDialog ───────────────────────────────────────────────────────

ChannelMixerDialog::ChannelMixerDialog(QWidget* parent)
	: FilterParamDialog(tr("Channel Mixer"), parent)
{
	// Defaults: identity RGB outputs; Monochrome starts luminance-ish 40/40/20.
	const int def[4][4] = { {100, 0, 0, 0}, {0, 100, 0, 0}, {0, 0, 100, 0}, {40, 40, 20, 0} };
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m_values[i][j] = def[i][j];

	auto* outRow = new QHBoxLayout();
	auto* outLbl = new QLabel(tr("Output:"), this);
	outLbl->setFixedWidth(80);
	outLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_output = new QComboBox(this);
	m_output->addItems({ tr("Red"), tr("Green"), tr("Blue") });
	outRow->addWidget(outLbl);
	outRow->addWidget(m_output, 1);
	contentLayout()->addLayout(outRow);

	m_srcR  = addSliderRow(tr("Red:"), -200, 200, 100, QStringLiteral("%"));
	m_srcG  = addSliderRow(tr("Green:"), -200, 200, 0, QStringLiteral("%"));
	m_srcB  = addSliderRow(tr("Blue:"), -200, 200, 0, QStringLiteral("%"));
	m_const = addSliderRow(tr("Constant:"), -100, 100, 0, QStringLiteral("%"));

	m_monochrome = new QCheckBox(tr("Monochrome"), this);
	contentLayout()->addWidget(m_monochrome);

	connect(m_output, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
		if (!m_monochrome->isChecked())
			selectSlot(idx);
	});
	connect(m_monochrome, &QCheckBox::toggled, this, [this](bool on){
		m_output->setEnabled(!on);
		selectSlot(on ? 3 : m_output->currentIndex());
		paramsChanged();   // the monochrome flag itself affects the result
	});

	finalizeLayout();
	syncSlidersToState();   // load slot 0 (Red output) into the sliders
}

void ChannelMixerDialog::selectSlot(int slot)
{
	if (slot < 0 || slot > 3)
		return;
	m_values[m_currentSlot][0] = m_srcR->value();
	m_values[m_currentSlot][1] = m_srcG->value();
	m_values[m_currentSlot][2] = m_srcB->value();
	m_values[m_currentSlot][3] = m_const->value();
	m_currentSlot = slot;
	syncSlidersToState();
}

void ChannelMixerDialog::syncSlidersToState()
{
	m_srcR->setValue(m_values[m_currentSlot][0]);
	m_srcG->setValue(m_values[m_currentSlot][1]);
	m_srcB->setValue(m_values[m_currentSlot][2]);
	m_const->setValue(m_values[m_currentSlot][3]);
}

ScImageEffectList ChannelMixerDialog::buildEffects() const
{
	QVector<int> mix(16, 0);
	for (int slot = 0; slot < 4; ++slot)
	{
		int a, b, c, d;
		if (slot == m_currentSlot)
		{
			a = m_srcR->value(); b = m_srcG->value(); c = m_srcB->value(); d = m_const->value();
		}
		else
		{
			a = m_values[slot][0]; b = m_values[slot][1]; c = m_values[slot][2]; d = m_values[slot][3];
		}
		mix[slot * 4 + 0] = a;
		mix[slot * 4 + 1] = b;
		mix[slot * 4 + 2] = c;
		mix[slot * 4 + 3] = d;
	}
	const bool mono = m_monochrome->isChecked();
	// A pure identity RGB matrix (not monochrome) is a no-op — skip it.
	const bool identity = !mono
		&& mix[0] == 100 && mix[1] == 0   && mix[2] == 0    && mix[3] == 0
		&& mix[4] == 0   && mix[5] == 100 && mix[6] == 0    && mix[7] == 0
		&& mix[8] == 0   && mix[9] == 0   && mix[10] == 100 && mix[11] == 0;
	ScImageEffectList list;
	if (!identity)
		list.append(ImageFilterEngine::makeChannelMixer(mix, mono));
	return list;
}

void ChannelMixerDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	for (int slot = 0; slot < 4; ++slot)
		for (int ch = 0; ch < 4; ++ch)
		{
			int v = 0;
			fp >> v;
			m_values[slot][ch] = v;
		}
	int mono = 0;
	fp >> mono;
	m_monochrome->setChecked(mono != 0);
	m_output->setEnabled(mono == 0);
	m_currentSlot = (mono != 0) ? 3 : m_output->currentIndex();
	if (m_currentSlot < 0)
		m_currentSlot = 0;
	syncSlidersToState();
}

void ChannelMixerDialog::resetToDefaults()
{
	const int def[4][4] = { {100, 0, 0, 0}, {0, 100, 0, 0}, {0, 0, 100, 0}, {40, 40, 20, 0} };
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			m_values[i][j] = def[i][j];
	m_monochrome->setChecked(false);
	m_output->setEnabled(true);
	m_output->setCurrentIndex(0);
	m_currentSlot = 0;
	syncSlidersToState();
}

// ── PhotoFilterDialog ────────────────────────────────────────────────────────

namespace
{
	struct PhotoPreset { const char* name; int r, g, b; };
	const PhotoPreset kPhotoPresets[] = {
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Warming Filter (85)"),  236, 138, 0   },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Warming Filter (LBA)"), 250, 150, 92  },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Cooling Filter (80)"),  0,   109, 255 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Cooling Filter (LBB)"), 73,  148, 255 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Red"),                  234, 26,  26  },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Orange"),              243, 101, 42  },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Yellow"),              247, 213, 25  },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Green"),               25,  201, 25  },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Cyan"),                25,  201, 201 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Blue"),                29,  66,  244 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Violet"),              155, 25,  201 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Magenta"),             227, 24,  138 },
		{ QT_TRANSLATE_NOOP("PhotoFilterDialog", "Sepia"),               172, 122, 51  },
	};
	const int kPhotoPresetCount = static_cast<int>(sizeof(kPhotoPresets) / sizeof(kPhotoPresets[0]));
}

PhotoFilterDialog::PhotoFilterDialog(QWidget* parent)
	: FilterParamDialog(tr("Photo Filter"), parent)
{
	// Filter row: preset selector, colour swatch, and a custom-colour button.
	auto* filterRow = new QHBoxLayout();
	auto* filterLbl = new QLabel(tr("Filter:"), this);
	filterLbl->setFixedWidth(80);
	filterLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_preset = new QComboBox(this);
	for (const auto& preset : kPhotoPresets)
		m_preset->addItem(tr(preset.name));
	m_preset->addItem(tr("Custom..."));
	m_swatch = new QLabel(this);
	m_swatch->setFixedSize(24, 24);
	m_swatch->setFrameShape(QFrame::Box);
	m_swatch->setAutoFillBackground(true);
	auto* customBtn = new QPushButton(tr("Color..."), this);
	filterRow->addWidget(filterLbl);
	filterRow->addWidget(m_preset, 1);
	filterRow->addWidget(m_swatch);
	filterRow->addWidget(customBtn);
	contentLayout()->addLayout(filterRow);

	m_density = addSliderRow(tr("Density:"), 0, 100, 25, QStringLiteral("%"));

	m_preserveLum = new QCheckBox(tr("Preserve Luminosity"), this);
	m_preserveLum->setChecked(true);
	contentLayout()->addWidget(m_preserveLum);

	connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
		if (idx >= 0 && idx < kPhotoPresetCount)
			setFilterColor(QColor(kPhotoPresets[idx].r, kPhotoPresets[idx].g, kPhotoPresets[idx].b), false);
	});
	connect(customBtn, &QPushButton::clicked, this, [this]{
		QColor c = QColorDialog::getColor(m_filterColor, this, tr("Filter Color"));
		if (c.isValid())
			setFilterColor(c, true);
	});
	connect(m_preserveLum, &QCheckBox::toggled, this, &FilterParamDialog::paramsChanged);

	finalizeLayout();
	updateSwatch();
}

void PhotoFilterDialog::setFilterColor(const QColor& color, bool selectCustomItem)
{
	m_filterColor = color;
	if (selectCustomItem)
	{
		QSignalBlocker blocker(m_preset);
		m_preset->setCurrentIndex(kPhotoPresetCount);   // the "Custom..." item
	}
	updateSwatch();
	paramsChanged();
}

void PhotoFilterDialog::updateSwatch()
{
	if (!m_swatch)
		return;
	QPalette pal = m_swatch->palette();
	pal.setColor(QPalette::Window, m_filterColor);
	m_swatch->setPalette(pal);
}

ScImageEffectList PhotoFilterDialog::buildEffects() const
{
	ScImageEffectList list;
	if (m_density->value() > 0)
		list.append(ImageFilterEngine::makePhotoFilter(m_filterColor.red(), m_filterColor.green(),
			m_filterColor.blue(), m_density->value(), m_preserveLum->isChecked()));
	return list;
}

void PhotoFilterDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int fr = 236, fg = 138, fb = 0, density = 25, preserve = 1;
	fp >> fr; fp >> fg; fp >> fb; fp >> density; fp >> preserve;
	m_filterColor = QColor(fr, fg, fb);
	m_density->setValue(density);
	m_preserveLum->setChecked(preserve != 0);
	// Reflect the colour in the preset selector (match a preset, else Custom).
	int match = kPhotoPresetCount;
	for (int i = 0; i < kPhotoPresetCount; ++i)
		if (kPhotoPresets[i].r == fr && kPhotoPresets[i].g == fg && kPhotoPresets[i].b == fb)
		{
			match = i;
			break;
		}
	QSignalBlocker blocker(m_preset);
	m_preset->setCurrentIndex(match);
	updateSwatch();
}

void PhotoFilterDialog::resetToDefaults()
{
	QSignalBlocker blocker(m_preset);
	m_preset->setCurrentIndex(0);
	m_filterColor = QColor(kPhotoPresets[0].r, kPhotoPresets[0].g, kPhotoPresets[0].b);
	m_density->setValue(25);
	m_preserveLum->setChecked(true);
	updateSwatch();
}

// ── ThresholdDialog ──────────────────────────────────────────────────────────

ThresholdDialog::ThresholdDialog(QWidget* parent)
	: FilterParamDialog(tr("Threshold"), parent)
{
	m_level = addSliderRow(tr("Level:"), 1, 255, 128);
	finalizeLayout();
	enableAuto(true);   // Auto = mean luminance
}

ScImageEffectList ThresholdDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeThreshold(m_level->value()));
	return list;
}

void ThresholdDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int level = 128;
	fp >> level;
	m_level->setValue(level);
}

void ThresholdDialog::resetToDefaults()
{
	m_level->setValue(128);
}

void ThresholdDialog::computeAuto(const QImage& source)
{
	if (source.isNull())
		return;
	QImage img = source;
	const int maxDim = 256;
	if (img.width() > maxDim || img.height() > maxDim)
		img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::FastTransformation);
	qulonglong sum = 0, n = 0;
	for (int y = 0; y < img.height(); ++y)
	{
		for (int x = 0; x < img.width(); ++x)
		{
			QColor c = img.pixelColor(x, y);
			sum += static_cast<qulonglong>(qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()));
			++n;
		}
	}
	if (n == 0)
		return;
	m_level->setValue(qBound(1, static_cast<int>(sum / n), 255));
}

// ── BlackWhiteDialog ─────────────────────────────────────────────────────────

BlackWhiteDialog::BlackWhiteDialog(QWidget* parent)
	: FilterParamDialog(tr("Black && White"), parent)
{
	// Photoshop default weights.
	m_reds     = addSliderRow(tr("Reds:"), -200, 300, 40, QStringLiteral("%"));
	m_yellows  = addSliderRow(tr("Yellows:"), -200, 300, 60, QStringLiteral("%"));
	m_greens   = addSliderRow(tr("Greens:"), -200, 300, 40, QStringLiteral("%"));
	m_cyans    = addSliderRow(tr("Cyans:"), -200, 300, 60, QStringLiteral("%"));
	m_blues    = addSliderRow(tr("Blues:"), -200, 300, 20, QStringLiteral("%"));
	m_magentas = addSliderRow(tr("Magentas:"), -200, 300, 80, QStringLiteral("%"));

	// Tint row.
	auto* tintRow = new QHBoxLayout();
	m_tint = new QCheckBox(tr("Tint"), this);
	m_tintSwatch = new QLabel(this);
	m_tintSwatch->setFixedSize(24, 24);
	m_tintSwatch->setFrameShape(QFrame::Box);
	m_tintSwatch->setAutoFillBackground(true);
	m_tintBtn = new QPushButton(tr("Color..."), this);
	tintRow->addWidget(m_tint);
	tintRow->addWidget(m_tintSwatch);
	tintRow->addWidget(m_tintBtn);
	tintRow->addStretch(1);
	contentLayout()->addLayout(tintRow);

	connect(m_tint, &QCheckBox::toggled, this, [this]{ updateTintUi(); paramsChanged(); });
	connect(m_tintBtn, &QPushButton::clicked, this, [this]{
		QColor c = QColorDialog::getColor(m_tintColor, this, tr("Tint Color"));
		if (c.isValid())
		{
			m_tintColor = c;
			updateTintUi();
			paramsChanged();
		}
	});

	finalizeLayout();
	updateTintUi();
}

void BlackWhiteDialog::updateTintUi()
{
	const bool on = m_tint && m_tint->isChecked();
	if (m_tintSwatch)
	{
		QPalette pal = m_tintSwatch->palette();
		pal.setColor(QPalette::Window, on ? m_tintColor : QColor(128, 128, 128));
		m_tintSwatch->setPalette(pal);
	}
	if (m_tintBtn)
		m_tintBtn->setEnabled(on);
}

ScImageEffectList BlackWhiteDialog::buildEffects() const
{
	QVector<int> weights{ m_reds->value(), m_yellows->value(), m_greens->value(),
	                      m_cyans->value(), m_blues->value(), m_magentas->value() };
	ScImageEffectList list;
	list.append(ImageFilterEngine::makeBlackWhite(weights, m_tint->isChecked(),
		m_tintColor.red(), m_tintColor.green(), m_tintColor.blue()));
	return list;
}

void BlackWhiteDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int wgt[6] = { 40, 60, 40, 60, 20, 80 };
	for (int i = 0; i < 6; ++i)
		fp >> wgt[i];
	int tint = 0, tr = 225, tg = 211, tb = 179;
	fp >> tint;
	fp >> tr; fp >> tg; fp >> tb;
	m_reds->setValue(wgt[0]);
	m_yellows->setValue(wgt[1]);
	m_greens->setValue(wgt[2]);
	m_cyans->setValue(wgt[3]);
	m_blues->setValue(wgt[4]);
	m_magentas->setValue(wgt[5]);
	m_tintColor = QColor(tr, tg, tb);
	m_tint->setChecked(tint != 0);
	updateTintUi();
}

void BlackWhiteDialog::resetToDefaults()
{
	m_reds->setValue(40);
	m_yellows->setValue(60);
	m_greens->setValue(40);
	m_cyans->setValue(60);
	m_blues->setValue(20);
	m_magentas->setValue(80);
	m_tint->setChecked(false);
	m_tintColor = QColor(225, 211, 179);
	updateTintUi();
}

// ── PosterizeDialog ──────────────────────────────────────────────────────────

PosterizeDialog::PosterizeDialog(QWidget* parent)
	: FilterParamDialog(tr("Posterize"), parent)
{
	contentLayout()->addLayout(makeIntRow(tr("Levels:"), 2, 255, 6, m_levelsSlider, m_levels, this));
	connect(m_levels, QOverload<int>::of(&QSpinBox::valueChanged), this, &FilterParamDialog::paramsChanged);
	finalizeLayout();
}

ScImageEffectList PosterizeDialog::buildEffects() const
{
	ScImageEffectList list;
	list.append(ImageFilterEngine::makePosterize(m_levels->value()));
	return list;
}

void PosterizeDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	int levels = 6;
	fp >> levels;
	m_levels->setValue(levels);
}

// ── CurvesDialog ─────────────────────────────────────────────────────────────

CurvesDialog::CurvesDialog(QWidget* parent)
	: FilterParamDialog(tr("Curves"), parent)
{
	m_curve = new CurveWidget(this);
	contentLayout()->addWidget(m_curve);
	connect(m_curve->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	finalizeLayout();
}

ScImageEffectList CurvesDialog::buildEffects() const
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_GRADUATE;
	e.effectParameters = curveToParams(m_curve);
	ScImageEffectList list;
	list.append(e);
	return list;
}

void CurvesDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	loadCurveFromStream(fp, m_curve);
}

// ── ColorizeDialog ───────────────────────────────────────────────────────────

ColorizeDialog::ColorizeDialog(ScribusDoc* doc, QWidget* parent)
	: FilterParamDialog(tr("Colorize"), parent)
{
	auto* row = new QHBoxLayout();
	row->addWidget(new QLabel(tr("Color:"), this));
	m_color = new ColorCombo(this);
	if (doc)
		m_color->setColors(doc->PageColors, false);
	m_shade = new ShadeButton(this);
	m_shade->setValue(100);
	row->addWidget(m_color, 1);
	row->addWidget(m_shade);
	contentLayout()->addLayout(row);

	connect(m_color, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_shade, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	finalizeLayout();
}

ScImageEffectList ColorizeDialog::buildEffects() const
{
	ImageEffect e;
	e.effectCode = ImageEffect::EF_COLORIZE;
	e.effectParameters = QString("%1\n%2").arg(m_color->currentText()).arg(m_shade->getValue());
	ScImageEffectList list;
	list.append(e);
	return list;
}

void ColorizeDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	QString col = fp.readLine();
	int shade = 100;
	fp >> shade;
	m_color->setCurrentColor(col);
	m_shade->setValue(shade);
}

// ── DuotoneDialog ────────────────────────────────────────────────────────────

DuotoneDialog::DuotoneDialog(ScribusDoc* doc, QWidget* parent)
	: FilterParamDialog(tr("Duotone"), parent)
{
	contentLayout()->addWidget(makeToneGroup(tr("Color 1"), doc, m_color1, m_shade1, m_curve1, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 2"), doc, m_color2, m_shade2, m_curve2, this));

	connect(m_color1, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color2, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_shade1, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade2, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_curve1->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve2->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	finalizeLayout();
}

ScImageEffectList DuotoneDialog::buildEffects() const
{
	QString p = m_color1->currentText() + "\n";
	p += m_color2->currentText() + "\n";
	p += QString("%1 %2 ").arg(m_shade1->getValue()).arg(m_shade2->getValue());
	p += curveToParams(m_curve1) + " ";
	p += curveToParams(m_curve2);
	ImageEffect e;
	e.effectCode = ImageEffect::EF_DUOTONE;
	e.effectParameters = p;
	ScImageEffectList list;
	list.append(e);
	return list;
}

void DuotoneDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	QString c1 = fp.readLine();
	QString c2 = fp.readLine();
	int s1 = 100, s2 = 100;
	fp >> s1;
	fp >> s2;
	m_color1->setCurrentColor(c1);
	m_shade1->setValue(s1);
	m_color2->setCurrentColor(c2);
	m_shade2->setValue(s2);
	loadCurveFromStream(fp, m_curve1);
	loadCurveFromStream(fp, m_curve2);
}

// ── TritoneDialog ────────────────────────────────────────────────────────────

TritoneDialog::TritoneDialog(ScribusDoc* doc, QWidget* parent)
	: FilterParamDialog(tr("Tritone"), parent)
{
	contentLayout()->addWidget(makeToneGroup(tr("Color 1"), doc, m_color1, m_shade1, m_curve1, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 2"), doc, m_color2, m_shade2, m_curve2, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 3"), doc, m_color3, m_shade3, m_curve3, this));

	connect(m_color1, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color2, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color3, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_shade1, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade2, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade3, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_curve1->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve2->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve3->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	finalizeLayout();
}

ScImageEffectList TritoneDialog::buildEffects() const
{
	QString p = m_color1->currentText() + "\n";
	p += m_color2->currentText() + "\n";
	p += m_color3->currentText() + "\n";
	p += QString("%1 %2 %3 ").arg(m_shade1->getValue()).arg(m_shade2->getValue()).arg(m_shade3->getValue());
	p += curveToParams(m_curve1) + " ";
	p += curveToParams(m_curve2) + " ";
	p += curveToParams(m_curve3);
	ImageEffect e;
	e.effectCode = ImageEffect::EF_TRITONE;
	e.effectParameters = p;
	ScImageEffectList list;
	list.append(e);
	return list;
}

void TritoneDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	QString c1 = fp.readLine();
	QString c2 = fp.readLine();
	QString c3 = fp.readLine();
	int s1 = 100, s2 = 100, s3 = 100;
	fp >> s1;
	fp >> s2;
	fp >> s3;
	m_color1->setCurrentColor(c1);
	m_shade1->setValue(s1);
	m_color2->setCurrentColor(c2);
	m_shade2->setValue(s2);
	m_color3->setCurrentColor(c3);
	m_shade3->setValue(s3);
	loadCurveFromStream(fp, m_curve1);
	loadCurveFromStream(fp, m_curve2);
	loadCurveFromStream(fp, m_curve3);
}

// ── QuadtoneDialog ───────────────────────────────────────────────────────────

QuadtoneDialog::QuadtoneDialog(ScribusDoc* doc, QWidget* parent)
	: FilterParamDialog(tr("Quadtone"), parent)
{
	contentLayout()->addWidget(makeToneGroup(tr("Color 1"), doc, m_color1, m_shade1, m_curve1, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 2"), doc, m_color2, m_shade2, m_curve2, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 3"), doc, m_color3, m_shade3, m_curve3, this));
	contentLayout()->addWidget(makeToneGroup(tr("Color 4"), doc, m_color4, m_shade4, m_curve4, this));

	connect(m_color1, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color2, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color3, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_color4, SIGNAL(activated(int)), this, SLOT(paramsChanged()));
	connect(m_shade1, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade2, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade3, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_shade4, SIGNAL(clicked()), this, SLOT(paramsChanged()));
	connect(m_curve1->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve2->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve3->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	connect(m_curve4->cDisplay, SIGNAL(modified()), this, SLOT(paramsChanged()));
	finalizeLayout();
}

ScImageEffectList QuadtoneDialog::buildEffects() const
{
	QString p = m_color1->currentText() + "\n";
	p += m_color2->currentText() + "\n";
	p += m_color3->currentText() + "\n";
	p += m_color4->currentText() + "\n";
	p += QString("%1 %2 %3 %4 ").arg(m_shade1->getValue()).arg(m_shade2->getValue())
			.arg(m_shade3->getValue()).arg(m_shade4->getValue());
	p += curveToParams(m_curve1) + " ";
	p += curveToParams(m_curve2) + " ";
	p += curveToParams(m_curve3) + " ";
	p += curveToParams(m_curve4);
	ImageEffect e;
	e.effectCode = ImageEffect::EF_QUADTONE;
	e.effectParameters = p;
	ScImageEffectList list;
	list.append(e);
	return list;
}

void QuadtoneDialog::loadFromEffect(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	QString c1 = fp.readLine();
	QString c2 = fp.readLine();
	QString c3 = fp.readLine();
	QString c4 = fp.readLine();
	int s1 = 100, s2 = 100, s3 = 100, s4 = 100;
	fp >> s1;
	fp >> s2;
	fp >> s3;
	fp >> s4;
	m_color1->setCurrentColor(c1);
	m_shade1->setValue(s1);
	m_color2->setCurrentColor(c2);
	m_shade2->setValue(s2);
	m_color3->setCurrentColor(c3);
	m_shade3->setValue(s3);
	m_color4->setCurrentColor(c4);
	m_shade4->setValue(s4);
	loadCurveFromStream(fp, m_curve1);
	loadCurveFromStream(fp, m_curve2);
	loadCurveFromStream(fp, m_curve3);
	loadCurveFromStream(fp, m_curve4);
}

// ── Free helpers ─────────────────────────────────────────────────────────────

FilterParamDialog* makeFilterDialogForEffect(const ImageEffect& effect, ScribusDoc* doc, QWidget* parent)
{
	FilterParamDialog* dlg = nullptr;
	switch (effect.effectCode)
	{
		case ImageEffect::EF_BLUR:       dlg = new GaussianBlurDialog(parent); break;
		case ImageEffect::EF_SHARPEN:    dlg = new SharpenDialog(parent); break;
		case ImageEffect::EF_BRIGHTNESS:
		case ImageEffect::EF_CONTRAST:   dlg = new BrightnessContrastDialog(effect.effectCode, parent); break;
		case ImageEffect::EF_POSTERIZE:  dlg = new PosterizeDialog(parent); break;
		case ImageEffect::EF_LEVELS:     dlg = new LevelsDialog(parent); break;
		case ImageEffect::EF_SHADOWHIGHLIGHT: dlg = new ShadowsHighlightsDialog(parent); break;
		case ImageEffect::EF_HUESAT:     dlg = new HueSaturationDialog(parent); break;
		case ImageEffect::EF_COLORBALANCE: dlg = new ColorBalanceDialog(parent); break;
		case ImageEffect::EF_CMYKADJ:    dlg = new CmykAdjustDialog(parent); break;
		case ImageEffect::EF_SELECTIVECOLOR: dlg = new SelectiveColorDialog(parent); break;
		case ImageEffect::EF_CHANNELMIXER: dlg = new ChannelMixerDialog(parent); break;
		case ImageEffect::EF_PHOTOFILTER: dlg = new PhotoFilterDialog(parent); break;
		case ImageEffect::EF_THRESHOLD:  dlg = new ThresholdDialog(parent); break;
		case ImageEffect::EF_BLACKWHITE: dlg = new BlackWhiteDialog(parent); break;
		case ImageEffect::EF_MOTIONBLUR: dlg = new MotionBlurDialog(parent); break;
		case ImageEffect::EF_RADIALBLUR: dlg = new RadialBlurDialog(parent); break;
		case ImageEffect::EF_BOXBLUR:    dlg = new BoxBlurDialog(parent); break;
		case ImageEffect::EF_AUTOENHANCE: dlg = new AutoEnhanceDialog(parent); break;
		case ImageEffect::EF_GRADUATE:   dlg = new CurvesDialog(parent); break;
		case ImageEffect::EF_COLORIZE:   dlg = new ColorizeDialog(doc, parent); break;
		case ImageEffect::EF_DUOTONE:    dlg = new DuotoneDialog(doc, parent); break;
		case ImageEffect::EF_TRITONE:    dlg = new TritoneDialog(doc, parent); break;
		case ImageEffect::EF_QUADTONE:   dlg = new QuadtoneDialog(doc, parent); break;
		default: return nullptr;   // Invert / Grayscale / Solarize — no editable params here
	}
	dlg->loadFromEffect(effect);
	return dlg;
}

QString filterEffectName(int effectCode)
{
	switch (effectCode)
	{
		case ImageEffect::EF_INVERT:     return QObject::tr("Invert");
		case ImageEffect::EF_GRAYSCALE:  return QObject::tr("Grayscale");
		case ImageEffect::EF_COLORIZE:   return QObject::tr("Colorize");
		case ImageEffect::EF_BRIGHTNESS: return QObject::tr("Brightness");
		case ImageEffect::EF_CONTRAST:   return QObject::tr("Contrast");
		case ImageEffect::EF_SHARPEN:    return QObject::tr("Sharpen");
		case ImageEffect::EF_BLUR:       return QObject::tr("Gaussian Blur");
		case ImageEffect::EF_SOLARIZE:   return QObject::tr("Solarize");
		case ImageEffect::EF_DUOTONE:    return QObject::tr("Duotone");
		case ImageEffect::EF_TRITONE:    return QObject::tr("Tritone");
		case ImageEffect::EF_QUADTONE:   return QObject::tr("Quadtone");
		case ImageEffect::EF_GRADUATE:   return QObject::tr("Curves");
		case ImageEffect::EF_POSTERIZE:  return QObject::tr("Posterize");
		case ImageEffect::EF_LEVELS:     return QObject::tr("Levels");
		case ImageEffect::EF_HUESAT:     return QObject::tr("Hue/Saturation");
		case ImageEffect::EF_COLORBALANCE: return QObject::tr("Color Balance");
		case ImageEffect::EF_CMYKADJ:    return QObject::tr("CMYK Adjustments");
		case ImageEffect::EF_SELECTIVECOLOR: return QObject::tr("Selective Color");
		case ImageEffect::EF_CHANNELMIXER: return QObject::tr("Channel Mixer");
		case ImageEffect::EF_PHOTOFILTER: return QObject::tr("Photo Filter");
		case ImageEffect::EF_THRESHOLD:  return QObject::tr("Threshold");
		case ImageEffect::EF_BLACKWHITE: return QObject::tr("Black & White");
		case ImageEffect::EF_MOTIONBLUR: return QObject::tr("Motion Blur");
		case ImageEffect::EF_RADIALBLUR: return QObject::tr("Radial Blur");
		case ImageEffect::EF_BOXBLUR:    return QObject::tr("Box Blur");
		case ImageEffect::EF_AUTOTONE:   return QObject::tr("Auto Tone");
		case ImageEffect::EF_AUTOCONTRAST: return QObject::tr("Auto Contrast");
		case ImageEffect::EF_AUTOCOLOR:  return QObject::tr("Auto Color");
		case ImageEffect::EF_AUTOENHANCE: return QObject::tr("Auto Enhance");
		case ImageEffect::EF_AUTOCMYK:   return QObject::tr("Auto CMYK Optimize");
		case ImageEffect::EF_SHADOWHIGHLIGHT: return QObject::tr("Shadows/Highlights");
	}
	return QObject::tr("Effect");
}

QString filterEffectSummary(const ImageEffect& effect)
{
	QString s = effect.effectParameters;
	ScTextStream fp(&s, QIODevice::ReadOnly);
	switch (effect.effectCode)
	{
		case ImageEffect::EF_BLUR:
		{
			double r = 0.0;
			fp >> r;
			return QObject::tr("Radius %1").arg(qRound(r));
		}
		case ImageEffect::EF_SHARPEN:
		{
			double r = 0.0, sg = 0.0;
			fp >> r;
			fp >> sg;
			return QObject::tr("Radius %1, Sigma %2").arg(r).arg(sg);
		}
		case ImageEffect::EF_BRIGHTNESS:
		case ImageEffect::EF_CONTRAST:
		{
			int v = 0;
			fp >> v;
			return (v > 0) ? QString("+%1").arg(v) : QString::number(v);
		}
		case ImageEffect::EF_POSTERIZE:
		{
			int v = 0;
			fp >> v;
			return QObject::tr("%1 levels").arg(v);
		}
		case ImageEffect::EF_LEVELS:
		{
			int inB = 0, inW = 255, outB = 0, outW = 255;
			double gamma = 1.0;
			fp >> inB;
			fp >> inW;
			fp >> gamma;
			fp >> outB;
			fp >> outW;
			return QObject::tr("In %1–%2, γ%3, Out %4–%5").arg(inB).arg(inW).arg(gamma, 0, 'g', 3).arg(outB).arg(outW);
		}
		case ImageEffect::EF_HUESAT:
		{
			int hue = 0, sat = 0, light = 0;
			fp >> hue;
			fp >> sat;
			fp >> light;
			return QObject::tr("H %1°, S %2, L %3").arg(hue).arg(sat).arg(light);
		}
		case ImageEffect::EF_COLORBALANCE:
		{
			int sr = 0, sg = 0, sb = 0, mr = 0, mg = 0, mb = 0, hr = 0, hg = 0, hb = 0;
			fp >> sr; fp >> sg; fp >> sb;
			fp >> mr; fp >> mg; fp >> mb;
			fp >> hr; fp >> hg; fp >> hb;
			return QObject::tr("S %1/%2/%3 · M %4/%5/%6 · H %7/%8/%9")
					.arg(sr).arg(sg).arg(sb).arg(mr).arg(mg).arg(mb).arg(hr).arg(hg).arg(hb);
		}
		case ImageEffect::EF_CMYKADJ:
		{
			int c = 0, m = 0, y = 0, k = 0;
			fp >> c; fp >> m; fp >> y; fp >> k;
			return QObject::tr("C%1 M%2 Y%3 K%4").arg(c).arg(m).arg(y).arg(k);
		}
		case ImageEffect::EF_SELECTIVECOLOR:
		{
			int activeRanges = 0;
			for (int rng = 0; rng < 9; ++rng)
			{
				bool any = false;
				for (int ch = 0; ch < 4; ++ch)
				{
					int v = 0;
					fp >> v;
					if (v != 0)
						any = true;
				}
				if (any)
					++activeRanges;
			}
			int relative = 1;
			fp >> relative;
			return QObject::tr("%1 range(s), %2").arg(activeRanges)
					.arg(relative != 0 ? QObject::tr("Relative") : QObject::tr("Absolute"));
		}
		case ImageEffect::EF_CHANNELMIXER:
		{
			int mix[16];
			for (int i = 0; i < 16; ++i)
			{
				mix[i] = 0;
				fp >> mix[i];
			}
			int mono = 0;
			fp >> mono;
			if (mono != 0)
				return QObject::tr("Monochrome %1/%2/%3").arg(mix[12]).arg(mix[13]).arg(mix[14]);
			return QObject::tr("RGB matrix");
		}
		case ImageEffect::EF_PHOTOFILTER:
		{
			int fr = 0, fg = 0, fb = 0, density = 0;
			fp >> fr; fp >> fg; fp >> fb; fp >> density;
			QString hex = QColor(fr, fg, fb).name(QColor::HexRgb).toUpper();
			return QObject::tr("%1, %2%").arg(hex).arg(density);
		}
		case ImageEffect::EF_THRESHOLD:
		{
			int level = 128;
			fp >> level;
			return QObject::tr("Level %1").arg(level);
		}
		case ImageEffect::EF_BLACKWHITE:
		{
			int wgt[6] = { 0, 0, 0, 0, 0, 0 };
			for (int i = 0; i < 6; ++i)
				fp >> wgt[i];
			int tint = 0;
			fp >> tint;
			return tint != 0 ? QObject::tr("Tinted") : QObject::tr("Grayscale mix");
		}
		case ImageEffect::EF_MOTIONBLUR:
		{
			int angle = 0, distance = 0;
			fp >> angle; fp >> distance;
			return QObject::tr("%1°, %2 px").arg(angle).arg(distance);
		}
		case ImageEffect::EF_RADIALBLUR:
		{
			int amount = 0, mode = 0;
			fp >> amount; fp >> mode;
			return QObject::tr("%1, amount %2").arg(mode == 0 ? QObject::tr("Spin") : QObject::tr("Zoom")).arg(amount);
		}
		case ImageEffect::EF_BOXBLUR:
		{
			int radius = 0;
			fp >> radius;
			return QObject::tr("Radius %1").arg(radius);
		}
		case ImageEffect::EF_AUTOENHANCE:
		{
			AutoCorrectOptions o = AutoCorrectEngine::parseEnhance(effect.effectParameters);
			QString algo;
			switch (o.algorithm)
			{
				case AutoCorrectOptions::MonochromaticContrast: algo = QObject::tr("Mono Contrast"); break;
				case AutoCorrectOptions::PerChannelContrast:    algo = QObject::tr("Per-Channel"); break;
				case AutoCorrectOptions::EnhanceBrightnessContrast: algo = QObject::tr("Brightness/Contrast"); break;
				default: algo = QObject::tr("Find Dark & Light"); break;
			}
			return algo;
		}
		case ImageEffect::EF_AUTOCMYK:
		{
			CmykOptimizeOptions o = AutoCorrectEngine::parseCmyk(effect.effectParameters);
			return QObject::tr("Ink limit %1%").arg(o.totalInkLimit);
		}
		case ImageEffect::EF_SHADOWHIGHLIGHT:
		{
			int sAmt = 0, sTone = 0, hAmt = 0;
			fp >> sAmt; fp >> sTone; fp >> hAmt;
			return QObject::tr("Shadows %1%, Highlights %2%").arg(sAmt).arg(hAmt);
		}
		case ImageEffect::EF_SOLARIZE:
		{
			double f = 0.0;
			fp >> f;
			return QString::number(f);
		}
		case ImageEffect::EF_COLORIZE:
		{
			QString c = fp.readLine();
			int sh = 100;
			fp >> sh;
			return QString("%1 %2%").arg(c).arg(sh);
		}
		case ImageEffect::EF_DUOTONE:
		{
			QString c1 = fp.readLine(), c2 = fp.readLine();
			return c1 + ", " + c2;
		}
		case ImageEffect::EF_TRITONE:
		{
			QString c1 = fp.readLine(), c2 = fp.readLine(), c3 = fp.readLine();
			return c1 + ", " + c2 + ", " + c3;
		}
		case ImageEffect::EF_QUADTONE:
		{
			QString c1 = fp.readLine(), c2 = fp.readLine(), c3 = fp.readLine(), c4 = fp.readLine();
			return c1 + ", " + c2 + ", " + c3 + ", " + c4;
		}
	}
	return QString();
}
