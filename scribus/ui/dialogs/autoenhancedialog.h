/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOENHANCEDIALOG_H
#define AUTOENHANCEDIALOG_H

#include "scribusapi.h"
#include "ui/scimagefilterdialogs.h"

class QButtonGroup;
class QCheckBox;
class QSlider;

/*!
 \brief Options dialog for the combined "Auto Enhance" correction. Built on the
        shared FilterParamDialog framework (two-column layout, live preview,
        OK/Cancel/Reset). Choice of algorithm plus neutral-midtone snap, skin
        protection, and shadow/highlight clip + midtone target sliders.
 */
class SCRIBUS_API AutoEnhanceDialog : public FilterParamDialog
{
	Q_OBJECT

public:
	explicit AutoEnhanceDialog(QWidget* parent = nullptr);

	ScImageEffectList buildEffects() const override;
	void loadFromEffect(const ImageEffect& effect) override;
	void resetToDefaults() override;

private:
	void loadSettings();
	void saveSettings() const;

	QButtonGroup* m_algorithm { nullptr };
	QCheckBox*    m_snapNeutral { nullptr };
	QCheckBox*    m_protectSkin { nullptr };
	QSlider*      m_shadowClip { nullptr };    //!< value / 100 = percent
	QSlider*      m_highlightClip { nullptr }; //!< value / 100 = percent
	QSlider*      m_midtoneTarget { nullptr };
};

#endif // AUTOENHANCEDIALOG_H
