/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "autoarrangedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

#include "autoarrangeengine.h"
#include "scpage.h"
#include "scribusdoc.h"
#include "units.h"

AutoArrangeDialog::AutoArrangeDialog(ScribusDoc* doc, QWidget* parent)
	: QDialog(parent), m_doc(doc)
{
	setWindowTitle(tr("Auto Arrange Frames"));
	setModal(true);

	auto* main = new QVBoxLayout(this);

	// Detected summary for the current page.
	ArrangeOptions probe;   // defaults: both frame types, all protections on
	int columns = 0, frames = 0;
	double areaWmm = 0.0, areaHmm = 0.0;
	if (m_doc && m_doc->currentPage())
	{
		ScPage* page = m_doc->currentPage();
		const QRectF area = AutoArrangeEngine::detectContentArea(page);
		columns = AutoArrangeEngine::detectColumns(m_doc, page, area).count();
		frames  = AutoArrangeEngine::gatherFrames(m_doc, page, probe).count();
		areaWmm = value2value(area.width(),  SC_PT, SC_MM);
		areaHmm = value2value(area.height(), SC_PT, SC_MM);
	}

	auto* detGroup = new QGroupBox(tr("Detected on current page"), this);
	auto* detLay = new QVBoxLayout(detGroup);
	detLay->addWidget(new QLabel(tr("Columns: %1").arg(columns), this));
	detLay->addWidget(new QLabel(tr("Frames to arrange: %1").arg(frames), this));
	detLay->addWidget(new QLabel(tr("Content area: %1 × %2 mm")
		.arg(areaWmm, 0, 'f', 0).arg(areaHmm, 0, 'f', 0), this));
	main->addWidget(detGroup);

	if (columns == 0)
	{
		auto* warn = new QLabel(tr("⚠ No column guides found on this page.\n"
		                           "Add vertical guides (Page → Manage Guides) to define columns."), this);
		warn->setWordWrap(true);
		main->addWidget(warn);
	}

	// Behaviour
	auto* behGroup = new QGroupBox(tr("Behaviour"), this);
	auto* behLay = new QVBoxLayout(behGroup);
	m_preserveColumn = new QCheckBox(tr("Preserve original column"), this);
	m_preserveColumn->setChecked(true);
	m_preserveColumn->setToolTip(tr("Each frame stays in the column its centre falls into; frames never cross columns."));
	m_zeroGap = new QCheckBox(tr("Zero vertical gap"), this);
	m_zeroGap->setChecked(true);
	m_zeroGap->setToolTip(tr("Stack frames so the bottom of one frame touches the top of the next."));
	m_includeGroups = new QCheckBox(tr("Include grouped articles"), this);
	m_includeGroups->setChecked(true);
	m_includeGroups->setToolTip(tr("Arrange grouped articles as whole units (re-stacked vertically, layout preserved). "
	                               "Most newspaper articles are groups — leave this on."));
	m_dryRun = new QCheckBox(tr("Dry run (log to console, change nothing)"), this);
	m_dryRun->setToolTip(tr("Print the detected columns and planned moves to the console without modifying the document."));
	behLay->addWidget(m_preserveColumn);
	behLay->addWidget(m_zeroGap);
	behLay->addWidget(m_includeGroups);
	behLay->addWidget(m_dryRun);
	main->addWidget(behGroup);

	// Frame height behaviour
	auto* heightGroup = new QGroupBox(tr("Frame Height Behaviour"), this);
	auto* heightLay = new QVBoxLayout(heightGroup);
	m_heightFit  = new QRadioButton(tr("Fit to text (keeps frames auto-sizing) — recommended"), this);
	m_heightFill = new QRadioButton(tr("Fill column (sets a fixed height)"), this);
	m_heightKeep = new QRadioButton(tr("Keep original height (sets a fixed height)"), this);
	m_heightFit->setChecked(true);
	m_heightFit->setToolTip(tr("Each text frame is fitted to its own text (via Adjust Frame Height to Text), so it keeps auto-adjusting when you edit. Recommended for newspaper workflow."));
	m_heightFill->setToolTip(tr("Stretch frames to fill the column. This sets a FIXED height — frames no longer auto-size to text until you re-run Adjust Frame Height to Text."));
	m_heightKeep->setToolTip(tr("Keep each frame's current height. This sets a FIXED height — frames no longer auto-size to text until you re-run Adjust Frame Height to Text."));
	heightLay->addWidget(m_heightFit);
	heightLay->addWidget(m_heightFill);
	heightLay->addWidget(m_heightKeep);
	main->addWidget(heightGroup);

	// Protected (always on — informational)
	auto* protGroup = new QGroupBox(tr("Protected (never moved)"), this);
	auto* protLay = new QVBoxLayout(protGroup);
	protLay->addWidget(new QLabel(tr("● Master page items (border, guides, masthead, page number)"), this));
	protLay->addWidget(new QLabel(tr("● Locked frames"), this));
	protLay->addWidget(new QLabel(tr("● Frames on locked or hidden layers"), this));
	main->addWidget(protGroup);

	// Scope
	auto* scopeGroup = new QGroupBox(tr("Apply To"), this);
	auto* scopeLay = new QVBoxLayout(scopeGroup);
	m_scopeCurrent = new QRadioButton(tr("Current page only"), this);
	m_scopeAll     = new QRadioButton(tr("All pages"), this);
	m_scopeCurrent->setChecked(true);
	scopeLay->addWidget(m_scopeCurrent);
	scopeLay->addWidget(m_scopeAll);
	main->addWidget(scopeGroup);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	main->addWidget(buttons);
}

ArrangeOptions AutoArrangeDialog::options() const
{
	ArrangeOptions o;
	o.preserveColumn    = m_preserveColumn->isChecked();
	o.zeroGap           = m_zeroGap->isChecked();
	o.includeGroups     = m_includeGroups->isChecked();
	o.dryRun            = m_dryRun->isChecked();
	if (m_heightKeep->isChecked())
		o.heightMode = ArrangeOptions::KeepOriginal;
	else if (m_heightFit->isChecked())
		o.heightMode = ArrangeOptions::FitToText;
	else
		o.heightMode = ArrangeOptions::FillColumn;
	// Master / locked / locked-layer protection is always on; both frame types.
	o.protectMasterPageItems = true;
	o.protectLockedFrames    = true;
	o.protectLockedLayers    = true;
	o.includeTextFrames  = true;
	o.includeImageFrames = true;
	o.scope = m_scopeAll->isChecked() ? ArrangeOptions::AllPages : ArrangeOptions::CurrentPage;
	return o;
}
