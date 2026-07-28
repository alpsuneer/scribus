/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/dialogs/autoenhancedialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "autocorrectengine.h"

AutoEnhanceDialog::AutoEnhanceDialog(QWidget* parent)
	: FilterParamDialog(tr("Auto Enhance"), parent)
{
	// Algorithm choice.
	auto* algoBox = new QGroupBox(tr("Algorithm"), this);
	auto* algoLayout = new QVBoxLayout(algoBox);
	m_algorithm = new QButtonGroup(this);
	auto addAlgo = [&](const QString& text, int id, bool checked){
		auto* rb = new QRadioButton(text, algoBox);
		rb->setChecked(checked);
		algoLayout->addWidget(rb);
		m_algorithm->addButton(rb, id);
	};
	addAlgo(tr("Monochromatic Contrast"), AutoCorrectOptions::MonochromaticContrast, false);
	addAlgo(tr("Per-Channel Contrast"), AutoCorrectOptions::PerChannelContrast, false);
	addAlgo(tr("Find Dark && Light Colors"), AutoCorrectOptions::FindDarkAndLightColors, true);
	addAlgo(tr("Enhance Brightness && Contrast"), AutoCorrectOptions::EnhanceBrightnessContrast, false);
	contentLayout()->addWidget(algoBox);

	// Options.
	m_snapNeutral = new QCheckBox(tr("Snap Neutral Midtones"), this);
	m_snapNeutral->setChecked(true);
	contentLayout()->addWidget(m_snapNeutral);
	m_protectSkin = new QCheckBox(tr("Protect Skin Tones"), this);
	m_protectSkin->setChecked(true);
	contentLayout()->addWidget(m_protectSkin);

	// Clip / midtone sliders (shadow/highlight clip are value/100 percent).
	m_shadowClip    = addSliderRow(tr("Shadow Clip:"), 0, 500, 10);
	m_highlightClip = addSliderRow(tr("Highlight Clip:"), 0, 500, 10);
	m_midtoneTarget = addSliderRow(tr("Midtone Target:"), 0, 255, 128);

	connect(m_snapNeutral, &QCheckBox::toggled, this, &FilterParamDialog::paramsChanged);
	connect(m_protectSkin, &QCheckBox::toggled, this, &FilterParamDialog::paramsChanged);
	connect(m_algorithm, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int){ paramsChanged(); });

	finalizeLayout();
	loadSettings();
}

ScImageEffectList AutoEnhanceDialog::buildEffects() const
{
	AutoCorrectOptions opts;
	opts.algorithm = static_cast<AutoCorrectOptions::Algorithm>(qBound(0, m_algorithm->checkedId(), 3));
	opts.snapNeutralMidtones = m_snapNeutral->isChecked();
	opts.protectSkinTones = m_protectSkin->isChecked();
	opts.shadowClip = m_shadowClip->value() / 100.0;
	opts.highlightClip = m_highlightClip->value() / 100.0;
	opts.midtoneTarget = m_midtoneTarget->value();
	ScImageEffectList list;
	list.append(AutoCorrectEngine::makeAutoEnhance(opts));
	return list;
}

void AutoEnhanceDialog::loadFromEffect(const ImageEffect& effect)
{
	AutoCorrectOptions o = AutoCorrectEngine::parseEnhance(effect.effectParameters);
	if (auto* btn = m_algorithm->button(o.algorithm))
		btn->setChecked(true);
	m_snapNeutral->setChecked(o.snapNeutralMidtones);
	m_protectSkin->setChecked(o.protectSkinTones);
	m_shadowClip->setValue(qRound(o.shadowClip * 100.0));
	m_highlightClip->setValue(qRound(o.highlightClip * 100.0));
	m_midtoneTarget->setValue(o.midtoneTarget);
}

void AutoEnhanceDialog::resetToDefaults()
{
	if (auto* btn = m_algorithm->button(AutoCorrectOptions::FindDarkAndLightColors))
		btn->setChecked(true);
	m_snapNeutral->setChecked(true);
	m_protectSkin->setChecked(true);
	m_shadowClip->setValue(10);
	m_highlightClip->setValue(10);
	m_midtoneTarget->setValue(128);
}

void AutoEnhanceDialog::loadSettings()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("ScImageEditor/AutoEnhancePresets"));
	if (settings.contains(QStringLiteral("algorithm")))
	{
		int algo = settings.value(QStringLiteral("algorithm"), AutoCorrectOptions::FindDarkAndLightColors).toInt();
		if (auto* btn = m_algorithm->button(qBound(0, algo, 3)))
			btn->setChecked(true);
		m_snapNeutral->setChecked(settings.value(QStringLiteral("snapNeutral"), true).toBool());
		m_protectSkin->setChecked(settings.value(QStringLiteral("protectSkin"), true).toBool());
		m_shadowClip->setValue(settings.value(QStringLiteral("shadowClip"), 10).toInt());
		m_highlightClip->setValue(settings.value(QStringLiteral("highlightClip"), 10).toInt());
		m_midtoneTarget->setValue(settings.value(QStringLiteral("midtoneTarget"), 128).toInt());
	}
	settings.endGroup();
	// Persist the chosen options when the user accepts.
	connect(this, &QDialog::accepted, this, [this]{ saveSettings(); });
}

void AutoEnhanceDialog::saveSettings() const
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("ScImageEditor/AutoEnhancePresets"));
	settings.setValue(QStringLiteral("algorithm"), m_algorithm->checkedId());
	settings.setValue(QStringLiteral("snapNeutral"), m_snapNeutral->isChecked());
	settings.setValue(QStringLiteral("protectSkin"), m_protectSkin->isChecked());
	settings.setValue(QStringLiteral("shadowClip"), m_shadowClip->value());
	settings.setValue(QStringLiteral("highlightClip"), m_highlightClip->value());
	settings.setValue(QStringLiteral("midtoneTarget"), m_midtoneTarget->value());
	settings.endGroup();
}
