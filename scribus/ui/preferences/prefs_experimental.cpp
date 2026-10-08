/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include "ui/preferences/prefs_experimental.h"
#include "prefsstructs.h"
#include "scribusdoc.h"
#include "ui/scshortcutregistry.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"

#include "ui/ParagraphStylesPanel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>

Prefs_Experimental::Prefs_Experimental(QWidget* parent, ScribusDoc* /*doc*/)
	: Prefs_Pane(parent)
{
	setupUi(this);
	languageChange();

	// Condense to Fit limits. Kept in the prefs file context "suneer_condense",
	// not in ApplicationPrefs: a new member there changes a struct the plugins share.
	QHBoxLayout* condenseRow = new QHBoxLayout();
	condenseRow->addWidget(new QLabel(tr("Condense to Fit - smallest horizontal scale:"), this));
	m_condenseMinScale = new QDoubleSpinBox(this);
	m_condenseMinScale->setRange(50.0, 100.0);
	m_condenseMinScale->setSingleStep(0.5);
	m_condenseMinScale->setDecimals(1);
	m_condenseMinScale->setSuffix(" %");
	m_condenseMinScale->setToolTip(tr("Condense to Fit never narrows text below this horizontal scale"));
	condenseRow->addWidget(m_condenseMinScale);
	condenseRow->addSpacing(16);
	condenseRow->addWidget(new QLabel(tr("lowest tracking:"), this));
	m_condenseMinTracking = new QDoubleSpinBox(this);
	m_condenseMinTracking->setRange(-10.0, 0.0);
	m_condenseMinTracking->setSingleStep(0.5);
	m_condenseMinTracking->setDecimals(1);
	m_condenseMinTracking->setSuffix(" %");
	m_condenseMinTracking->setToolTip(tr("Used only when the smallest scale is not enough. 0 = never change tracking"));
	condenseRow->addWidget(m_condenseMinTracking);
	condenseRow->addStretch();
	// Above the page's trailing spacer, right under the check boxes.
	verticalLayout->insertLayout(qMax(0, verticalLayout->count() - 1), condenseRow);
	// Design Style: clear the manual size/spacing (QSettings, read by the panel).
	m_clearManualOnDesign = new QCheckBox(tr("Clear manual size/spacing when applying a Design Style (Shift+click the icon to keep them)"), this);
	m_clearManualOnDesign->setToolTip(tr("Font size, scaling, tracking, baseline offset, line spacing, indents, alignment and drop caps set by hand go back to the style. Bold/italic, colours, underline and named character styles stay."));
	verticalLayout->insertWidget(qMax(0, verticalLayout->count() - 1), m_clearManualOnDesign);

	m_caption = tr("SR Menu");
	m_icon = "pref-experimental";
}

Prefs_Experimental::~Prefs_Experimental() = default;

void Prefs_Experimental::languageChange()
{
	// No need to do anything here, the UI language cannot change while prefs dialog is opened
}

void Prefs_Experimental::restoreDefaults(struct ApplicationPrefs *prefsData)
{
	enableNotesCheckBox->setChecked(prefsData->experimentalFeaturePrefs.notesEnabled);
	enableNewsBrowserCheckBox->setChecked(prefsData->experimentalFeaturePrefs.newsBrowserEnabled);
	// Kept with the other shortcut-check settings (QSettings), not in
	// ApplicationPrefs: a new member there changes a struct the plugins share.
	checkDuplicateShortcutsCheckBox->setChecked(ScShortcutRegistry::checkOnOpenEnabled());
	PrefsContext* condensePrefs = PrefsManager::instance().prefsFile->getContext("suneer_condense");
	m_condenseMinScale->setValue(condensePrefs ? condensePrefs->getDouble("min_scale", 90.0) : 90.0);
	m_condenseMinTracking->setValue(condensePrefs ? condensePrefs->getDouble("min_tracking", -2.0) : -2.0);
	m_clearManualOnDesign->setChecked(ParagraphStylesPanel::clearManualOnDesign());
}

void Prefs_Experimental::saveGuiToPrefs(struct ApplicationPrefs *prefsData) const
{
	prefsData->experimentalFeaturePrefs.notesEnabled = enableNotesCheckBox->isChecked();
	prefsData->experimentalFeaturePrefs.newsBrowserEnabled = enableNewsBrowserCheckBox->isChecked();
	ScShortcutRegistry::setCheckOnOpenEnabled(checkDuplicateShortcutsCheckBox->isChecked());
	if (PrefsContext* condensePrefs = PrefsManager::instance().prefsFile->getContext("suneer_condense"))
	{
		condensePrefs->set("min_scale", m_condenseMinScale->value());
		condensePrefs->set("min_tracking", m_condenseMinTracking->value());
	}
	ParagraphStylesPanel::setClearManualOnDesign(m_clearManualOnDesign->isChecked());
}

