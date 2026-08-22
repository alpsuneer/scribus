/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "impositiondialog.h"
#include "impositionpreviewwidget.h"

#include "scpage.h"
#include "scpaths.h"
#include "scribusdoc.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

ImpositionDialog::ImpositionDialog(ScribusDoc* doc, QWidget* parent)
	: QDialog(parent)
	, m_doc(doc)
	, m_sheetWidthMm(ScImpositionEngine::defaultSheetWidthMm())
	, m_sheetHeightMm(ScImpositionEngine::defaultSheetHeightMm())
{
	setWindowTitle(tr("Impose Pages"));

	QSettings settings("Faircode", "CTPImposition");

	auto* mainLayout = new QVBoxLayout(this);

	m_tabWidget = new QTabWidget(this);

	// ============================= Setup tab =============================
	auto* setupTab = new QWidget(m_tabWidget);
	auto* setupLayout = new QVBoxLayout(setupTab);

	// --- Plate Properties ---
	auto* plateGroup = new QGroupBox(tr("Plate Properties"), setupTab);
	auto* plateLayout = new QVBoxLayout(plateGroup);

	auto* nameRow = new QHBoxLayout();
	nameRow->addWidget(new QLabel(tr("Name:"), plateGroup));
	m_plateNameEdit = new QLineEdit(plateGroup);
	m_plateNameEdit->setText(settings.value("lastPlateName").toString());
	m_plateNameEdit->setToolTip(tr("Identifies this plate configuration; also used as the preset name for Save/Load/Delete Preset below."));
	nameRow->addWidget(m_plateNameEdit, 1);
	plateLayout->addLayout(nameRow);

	const QString plateSizeTip = tr(
		"Shop's confirmed CTP plate: 700.0 x 576.0mm. Changes are saved and reused next time.");
	auto* sizeRow = new QHBoxLayout();
	sizeRow->addWidget(new QLabel(tr("Width:"), plateGroup));
	m_sheetWidthSpin = new QDoubleSpinBox(plateGroup);
	m_sheetWidthSpin->setRange(50.0, 2000.0);
	m_sheetWidthSpin->setDecimals(1);
	m_sheetWidthSpin->setSuffix(tr(" mm"));
	m_sheetWidthSpin->setValue(m_sheetWidthMm);
	m_sheetWidthSpin->setToolTip(plateSizeTip);
	sizeRow->addWidget(m_sheetWidthSpin);
	sizeRow->addWidget(new QLabel(tr("Height:"), plateGroup));
	m_sheetHeightSpin = new QDoubleSpinBox(plateGroup);
	m_sheetHeightSpin->setRange(50.0, 2000.0);
	m_sheetHeightSpin->setDecimals(1);
	m_sheetHeightSpin->setSuffix(tr(" mm"));
	m_sheetHeightSpin->setValue(m_sheetHeightMm);
	m_sheetHeightSpin->setToolTip(plateSizeTip);
	sizeRow->addWidget(m_sheetHeightSpin);
	sizeRow->addStretch(1);
	plateLayout->addLayout(sizeRow);

	const QString resolutionTip = tr(
		"CTP imagesetter resolution: 1200dpi. Drives the pipeline's raster output --"
		" not just a label.");
	auto* resRow = new QHBoxLayout();
	resRow->addWidget(new QLabel(tr("Resolution:"), plateGroup));
	resRow->addWidget(new QLabel(tr("X"), plateGroup));
	m_resolutionXSpin = new QSpinBox(plateGroup);
	m_resolutionXSpin->setRange(72, 4800);
	m_resolutionXSpin->setSuffix(tr(" dpi"));
	m_resolutionXSpin->setValue(ScImpositionEngine::defaultResolutionXDpi());
	m_resolutionXSpin->setToolTip(resolutionTip);
	resRow->addWidget(m_resolutionXSpin);
	resRow->addWidget(new QLabel(tr("Y"), plateGroup));
	m_resolutionYSpin = new QSpinBox(plateGroup);
	m_resolutionYSpin->setRange(72, 4800);
	m_resolutionYSpin->setSuffix(tr(" dpi"));
	m_resolutionYSpin->setValue(ScImpositionEngine::defaultResolutionYDpi());
	m_resolutionYSpin->setToolTip(resolutionTip);
	resRow->addWidget(m_resolutionYSpin);
	resRow->addStretch(1);
	plateLayout->addLayout(resRow);

	auto* mediaRow = new QHBoxLayout();
	mediaRow->addWidget(new QLabel(tr("Media type:"), plateGroup));
	m_mediaTypeCombo = new QComboBox(plateGroup);
	m_mediaTypeCombo->setEditable(true);
	m_mediaTypeCombo->setInsertPolicy(QComboBox::NoInsert);
	m_mediaTypeCombo->setToolTip(tr("Free text -- type your own plate/media description; no fixed list."));
	const QString lastMedia = settings.value("lastMediaType").toString();
	if (!lastMedia.isEmpty())
		m_mediaTypeCombo->addItem(lastMedia);
	mediaRow->addWidget(m_mediaTypeCombo, 1);
	plateLayout->addLayout(mediaRow);

	auto* hotFolderRow = new QHBoxLayout();
	hotFolderRow->addWidget(new QLabel(tr("Hot folder:"), plateGroup));
	m_hotFolderEdit = new QLineEdit(plateGroup);
	m_hotFolderEdit->setText(settings.value("lastHotFolder").toString());
	m_hotFolderEdit->setToolTip(tr("Where Send to CTP copies the 4 renamed TIFF plates."));
	hotFolderRow->addWidget(m_hotFolderEdit, 1);
	m_hotFolderBrowseButton = new QPushButton(tr("Browse..."), plateGroup);
	hotFolderRow->addWidget(m_hotFolderBrowseButton);
	plateLayout->addLayout(hotFolderRow);

	auto* presetRow = new QHBoxLayout();
	presetRow->addWidget(new QLabel(tr("Presets:"), plateGroup));
	m_presetCombo = new QComboBox(plateGroup);
	m_presetCombo->setMinimumWidth(140);
	presetRow->addWidget(m_presetCombo, 1);
	m_savePresetButton = new QPushButton(tr("Save Preset"), plateGroup);
	presetRow->addWidget(m_savePresetButton);
	m_loadPresetButton = new QPushButton(tr("Load Preset"), plateGroup);
	presetRow->addWidget(m_loadPresetButton);
	m_deletePresetButton = new QPushButton(tr("Delete Preset"), plateGroup);
	presetRow->addWidget(m_deletePresetButton);
	plateLayout->addLayout(presetRow);

	setupLayout->addWidget(plateGroup);

	// --- Elements ---
	auto* elementsGroup = new QGroupBox(tr("Elements"), setupTab);
	auto* elementsLayout = new QGridLayout(elementsGroup);

	auto addElementRow = [elementsGroup, elementsLayout](int row, QCheckBox*& check, const QString& title,
	                                                       const QString& desc, const QString& tip, bool checked) {
		check = new QCheckBox(title, elementsGroup);
		check->setChecked(checked);
		check->setToolTip(tip);
		elementsLayout->addWidget(check, row, 0);
		auto* descLabel = new QLabel(QStringLiteral("— ") + desc, elementsGroup);
		descLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
		elementsLayout->addWidget(descLabel, row, 1);
	};

	addElementRow(0, m_printAreaCheck, tr("Print Area"), tr("the two page content"),
		tr("The two source pages themselves, cloned at native size. Turning this off produces a marks-only plate."), true);
	addElementRow(1, m_regmarksCheck, tr("Regmarks"), tr("registration marks"),
		tr("Circle + crosshair, 6mm diameter, 0.25pt line, at all 4 plate corners plus center top/bottom. "
		   "Prints on every separation (CMYK overprint). In the margin area, outside page content."), true);
	addElementRow(2, m_autoMarksCheck, tr("Auto Marks"), tr("automatic trim marks"),
		tr("0.25pt lines, 5mm long, 3mm offset from each page edge, at all 4 corners of each page."), false);
	addElementRow(3, m_furnituresCheck, tr("Furnitures"), tr("slug/label text"),
		tr("Slug line in the top margin: \"{PUB} | {DATE} | Page {L}-{R} | Edition {ED}\", Helvetica 8pt."), true);
	addElementRow(4, m_colourBarCheck, tr("Colour Bar"), tr("CMYK ink patches"),
		tr("Bottom margin: 9 patches, 6x6mm each -- C100/M100/Y100/K100/C50/M50/Y50/K50/Registration -- "
		   "each labelled below in 6pt."), true);
	addElementRow(5, m_barcodesCheck, tr("Barcodes"), tr("optional barcode"),
		tr("Reserved for a future job/plate barcode. No content or symbology has been specified yet, "
		   "so this currently draws and exports nothing."), false);
	addElementRow(6, m_guidelinesCheck, tr("Guidelines"), tr("non-printing guides"),
		tr("The margin rectangle shown in the Preview tab. Never exported to the PDF."), false);

	setupLayout->addWidget(elementsGroup);

	// --- Page Assignment ---
	auto* pageGroup = new QGroupBox(tr("Page Assignment"), setupTab);
	auto* pageGroupLayout = new QVBoxLayout(pageGroup);

	auto* folderRow = new QHBoxLayout();
	folderRow->addWidget(new QLabel(tr("Folder:"), pageGroup));
	m_folderEdit = new QLineEdit(pageGroup);
	m_folderEdit->setText(settings.value("lastFolder").toString());
	m_folderEdit->setToolTip(tr("Folder to list .pdf files from for both Left and Right."));
	folderRow->addWidget(m_folderEdit, 1);
	m_browseButton = new QPushButton(tr("Browse..."), pageGroup);
	folderRow->addWidget(m_browseButton);
	pageGroupLayout->addLayout(folderRow);

	auto* pageRow = new QHBoxLayout();
	auto* leftLabel = new QLabel(tr("Left page:"), pageGroup);
	leftLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
	pageRow->addWidget(leftLabel);
	m_leftFileCombo = new QComboBox(pageGroup);
	m_leftFileCombo->setMinimumWidth(130);
	pageRow->addWidget(m_leftFileCombo);
	m_leftPageCombo = new QComboBox(pageGroup);
	m_leftPageCombo->setMinimumWidth(90);
	pageRow->addWidget(m_leftPageCombo);
	pageRow->addSpacing(20);
	auto* rightLabel = new QLabel(tr("Right page:"), pageGroup);
	rightLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
	pageRow->addWidget(rightLabel);
	m_rightFileCombo = new QComboBox(pageGroup);
	m_rightFileCombo->setMinimumWidth(130);
	pageRow->addWidget(m_rightFileCombo);
	m_rightPageCombo = new QComboBox(pageGroup);
	m_rightPageCombo->setMinimumWidth(90);
	pageRow->addWidget(m_rightPageCombo);
	pageRow->addStretch(1);
	pageGroupLayout->addLayout(pageRow);

	m_leftFileCombo->setToolTip(tr("The .pdf file (from Folder above) placed on the left. Only its page 1 is imposed."));
	m_rightFileCombo->setToolTip(tr("The .pdf file (from Folder above) placed on the right. Only its page 1 is imposed."));

	auto* pageNumberRow = new QHBoxLayout();
	pageNumberRow->addWidget(new QLabel(tr("Left page #:"), pageGroup));
	m_leftPageNumberSpin = new QSpinBox(pageGroup);
	m_leftPageNumberSpin->setRange(1, 999);
	m_leftPageNumberSpin->setValue(settings.value("lastLeftPageNumber", 1).toInt());
	m_leftPageNumberSpin->setToolTip(tr("The newspaper's own page number for the left page -- used in the slug line and the CTP filename. Not the same as the page combo above, which only selects within the source PDF file."));
	pageNumberRow->addWidget(m_leftPageNumberSpin);
	pageNumberRow->addSpacing(20);
	pageNumberRow->addWidget(new QLabel(tr("Right page #:"), pageGroup));
	m_rightPageNumberSpin = new QSpinBox(pageGroup);
	m_rightPageNumberSpin->setRange(1, 999);
	m_rightPageNumberSpin->setValue(settings.value("lastRightPageNumber", 1).toInt());
	m_rightPageNumberSpin->setToolTip(tr("The newspaper's own page number for the right page -- used in the slug line and the CTP filename. Not the same as the page combo above, which only selects within the source PDF file."));
	pageNumberRow->addWidget(m_rightPageNumberSpin);
	pageNumberRow->addStretch(1);
	pageGroupLayout->addLayout(pageNumberRow);

	auto makeMmSpin = [this, pageGroup]() {
		auto* spin = new QDoubleSpinBox(pageGroup);
		spin->setRange(0.0, 200.0);
		spin->setDecimals(1);
		spin->setSuffix(tr(" mm"));
		spin->setValue(0.0);
		return spin;
	};

	auto* spacingRow = new QHBoxLayout();
	spacingRow->addWidget(new QLabel(tr("Gutter:"), pageGroup));
	m_gutterSpin = makeMmSpin();
	spacingRow->addWidget(m_gutterSpin);
	spacingRow->addStretch(1);
	pageGroupLayout->addLayout(spacingRow);

	auto* slugRow = new QHBoxLayout();
	slugRow->addWidget(new QLabel(tr("Publication:"), pageGroup));
	m_pubEdit = new QLineEdit(pageGroup);
	m_pubEdit->setMaximumWidth(90);
	m_pubEdit->setPlaceholderText(QStringLiteral("CAL"));
	m_pubEdit->setText(settings.value("lastPubCode").toString());
	m_pubEdit->setToolTip(tr("{PUB} in the slug line printed at the top of the plate."));
	slugRow->addWidget(m_pubEdit);
	slugRow->addSpacing(20);
	slugRow->addWidget(new QLabel(tr("Edition:"), pageGroup));
	m_editionEdit = new QLineEdit(pageGroup);
	m_editionEdit->setMaximumWidth(90);
	m_editionEdit->setPlaceholderText(QStringLiteral("8"));
	m_editionEdit->setText(settings.value("lastEditionCode").toString());
	m_editionEdit->setToolTip(tr("{ED} in the slug line printed at the top of the plate."));
	slugRow->addWidget(m_editionEdit);
	slugRow->addStretch(1);
	pageGroupLayout->addLayout(slugRow);

	auto* centerRow = new QHBoxLayout();
	m_centerOnPlateCheck = new QCheckBox(tr("Center on plate"), pageGroup);
	m_centerOnPlateCheck->setToolTip(tr(
		"Equal margins on all sides, computed from the plate size, gutter, and each "
		"page's native size. The margins below become read-only while this is checked."));
	centerRow->addWidget(m_centerOnPlateCheck);
	centerRow->addStretch(1);
	pageGroupLayout->addLayout(centerRow);

	auto* marginsRow = new QHBoxLayout();
	marginsRow->addWidget(new QLabel(tr("Margins:"), pageGroup));
	marginsRow->addWidget(new QLabel(tr("Top"), pageGroup));
	m_marginTopSpin = makeMmSpin();
	marginsRow->addWidget(m_marginTopSpin);
	marginsRow->addWidget(new QLabel(tr("Bottom"), pageGroup));
	m_marginBottomSpin = makeMmSpin();
	marginsRow->addWidget(m_marginBottomSpin);
	marginsRow->addWidget(new QLabel(tr("Left"), pageGroup));
	m_marginLeftSpin = makeMmSpin();
	marginsRow->addWidget(m_marginLeftSpin);
	marginsRow->addWidget(new QLabel(tr("Right"), pageGroup));
	m_marginRightSpin = makeMmSpin();
	marginsRow->addWidget(m_marginRightSpin);
	marginsRow->addStretch(1);
	pageGroupLayout->addLayout(marginsRow);

	m_centerWarningLabel = new QLabel(tr("Content wider than plate -- reduce gutter or page size"), pageGroup);
	m_centerWarningLabel->setStyleSheet(QStringLiteral("color: #c02020; font-weight: bold;"));
	m_centerWarningLabel->setVisible(false);
	pageGroupLayout->addWidget(m_centerWarningLabel);

	setupLayout->addWidget(pageGroup);
	setupLayout->addStretch(1);

	m_tabWidget->addTab(setupTab, tr("Setup"));

	// ============================ Preview tab =============================
	// Schematic only -- rectangles and labels, no rendered PDF content, so
	// this view has no font/rendering dependency of its own to get wrong.
	m_preview = new ImpositionPreviewWidget(m_tabWidget);
	m_tabWidget->addTab(m_preview, tr("Preview"));

	mainLayout->addWidget(m_tabWidget, 1);

	// --- Action buttons: always visible, outside the tabs ---
	auto* buttons = new QDialogButtonBox(this);
	m_previewPdfButton = buttons->addButton(tr("Preview PDF"), QDialogButtonBox::ActionRole);
	m_sendToCtpButton = buttons->addButton(tr("Send to CTP"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Cancel);
	mainLayout->addWidget(buttons, 0);

	connect(m_browseButton, &QPushButton::clicked, this, &ImpositionDialog::browseFolderClicked);
	connect(m_folderEdit, &QLineEdit::editingFinished, this, &ImpositionDialog::folderEdited);
	connect(m_leftFileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImpositionDialog::leftFileChanged);
	connect(m_rightFileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImpositionDialog::rightFileChanged);
	connect(m_leftPageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImpositionDialog::onLeftPageChanged);
	connect(m_rightPageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImpositionDialog::onRightPageChanged);
	connect(m_leftPageNumberSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_rightPageNumberSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_sheetWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::onPlateSizeChanged);
	connect(m_sheetHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::onPlateSizeChanged);
	connect(m_resolutionXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImpositionDialog::onPlateSizeChanged);
	connect(m_resolutionYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImpositionDialog::onPlateSizeChanged);
	connect(m_plateNameEdit, &QLineEdit::textChanged, this, &ImpositionDialog::settingsChanged);
	connect(m_mediaTypeCombo, &QComboBox::editTextChanged, this, &ImpositionDialog::settingsChanged);
	connect(m_hotFolderEdit, &QLineEdit::textChanged, this, &ImpositionDialog::settingsChanged);
	connect(m_hotFolderBrowseButton, &QPushButton::clicked, this, &ImpositionDialog::browseHotFolderClicked);
	connect(m_gutterSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_marginLeftSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_marginRightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_marginTopSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_marginBottomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImpositionDialog::settingsChanged);
	connect(m_centerOnPlateCheck, &QCheckBox::toggled, this, &ImpositionDialog::onCenterOnPlateToggled);
	connect(m_printAreaCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_regmarksCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_autoMarksCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_furnituresCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_colourBarCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_barcodesCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_guidelinesCheck, &QCheckBox::toggled, this, &ImpositionDialog::settingsChanged);
	connect(m_pubEdit, &QLineEdit::textChanged, this, &ImpositionDialog::settingsChanged);
	connect(m_editionEdit, &QLineEdit::textChanged, this, &ImpositionDialog::settingsChanged);
	connect(m_previewPdfButton, &QPushButton::clicked, this, &ImpositionDialog::previewPdfClicked);
	connect(m_sendToCtpButton, &QPushButton::clicked, this, &ImpositionDialog::sendToCtpClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_savePresetButton, &QPushButton::clicked, this, &ImpositionDialog::savePresetClicked);
	connect(m_loadPresetButton, &QPushButton::clicked, this, &ImpositionDialog::loadPresetClicked);
	connect(m_deletePresetButton, &QPushButton::clicked, this, &ImpositionDialog::deletePresetClicked);

	resize(820, 820);
	populateFileCombos();
	populatePresetCombo();
	settingsChanged();
}

ImpositionDialog::~ImpositionDialog()
{
}

void ImpositionDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	populateFileCombos();
	settingsChanged();
}

QString ImpositionDialog::folderPath() const
{
	return m_folderEdit->text().trimmed();
}

QString ImpositionDialog::selectedFileName(QComboBox* fileCombo) const
{
	return fileCombo->currentText();
}

void ImpositionDialog::browseFolderClicked()
{
	QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"), folderPath());
	if (dir.isEmpty())
		return;
	m_folderEdit->setText(dir);
	folderEdited();
}

void ImpositionDialog::browseHotFolderClicked()
{
	QString dir = QFileDialog::getExistingDirectory(this, tr("Select Hot Folder"), m_hotFolderEdit->text().trimmed());
	if (dir.isEmpty())
		return;
	m_hotFolderEdit->setText(dir);
}

void ImpositionDialog::folderEdited()
{
	QSettings settings("Faircode", "CTPImposition");
	settings.setValue("lastFolder", folderPath());
	populateFileCombos();
	settingsChanged();
}

void ImpositionDialog::populateFileCombos()
{
	QString folder = folderPath();
	QString prevLeft = selectedFileName(m_leftFileCombo);
	QString prevRight = selectedFileName(m_rightFileCombo);

	QStringList files;
	if (!folder.isEmpty())
	{
		QDir dir(folder);
		files = dir.entryList(QStringList() << QStringLiteral("*.pdf"), QDir::Files, QDir::Name);
	}

	m_leftFileCombo->blockSignals(true);
	m_rightFileCombo->blockSignals(true);
	m_leftFileCombo->clear();
	m_rightFileCombo->clear();
	m_leftFileCombo->addItems(files);
	m_rightFileCombo->addItems(files);
	if (!files.isEmpty())
	{
		int leftIdx = files.indexOf(prevLeft);
		int rightIdx = files.indexOf(prevRight);
		m_leftFileCombo->setCurrentIndex(leftIdx >= 0 ? leftIdx : 0);
		m_rightFileCombo->setCurrentIndex(rightIdx >= 0 ? rightIdx : (files.count() > 1 ? 1 : 0));
	}
	m_leftFileCombo->blockSignals(false);
	m_rightFileCombo->blockSignals(false);

	populatePageCombo(m_leftFileCombo, m_leftPageCombo);
	populatePageCombo(m_rightFileCombo, m_rightPageCombo);
	updatePdfInfo();
}

void ImpositionDialog::populatePageCombo(QComboBox* fileCombo, QComboBox* pageCombo)
{
	QString fileName = selectedFileName(fileCombo);
	int pageCount = 0;
	if (!fileName.isEmpty())
	{
		QString path = QDir(folderPath()).filePath(fileName);
		pageCount = ScImpositionEngine::scanPdfInfo(path).pageCount;
	}

	int prev = pageCombo->currentData().isValid() ? pageCombo->currentData().toInt() : 0;

	pageCombo->blockSignals(true);
	pageCombo->clear();
	for (int i = 0; i < pageCount; ++i)
		pageCombo->addItem(tr("Page %1").arg(i + 1), i);
	if (pageCount > 0)
		pageCombo->setCurrentIndex(qBound(0, prev, pageCount - 1));
	pageCombo->blockSignals(false);
}

void ImpositionDialog::updatePdfInfo()
{
	QDir folder(folderPath());
	QString leftFile = selectedFileName(m_leftFileCombo);
	QString rightFile = selectedFileName(m_rightFileCombo);
	QString leftPath = leftFile.isEmpty() ? QString() : folder.filePath(leftFile);
	QString rightPath = rightFile.isEmpty() ? QString() : folder.filePath(rightFile);

	m_leftPageInfo = leftPath.isEmpty() ? ScImpositionEngine::PdfPageInfo() : ScImpositionEngine::scanPdfInfo(leftPath);
	m_rightPageInfo = rightPath.isEmpty() ? ScImpositionEngine::PdfPageInfo() : ScImpositionEngine::scanPdfInfo(rightPath);

	if (!leftPath.isEmpty() && !m_leftPageInfo.valid)
		QMessageBox::warning(this, tr("Imposition"), tr("Could not read the left page's file:\n%1").arg(leftPath));
	if (!rightPath.isEmpty() && !m_rightPageInfo.valid)
		QMessageBox::warning(this, tr("Imposition"), tr("Could not read the right page's file:\n%1").arg(rightPath));
}

void ImpositionDialog::leftFileChanged()
{
	populatePageCombo(m_leftFileCombo, m_leftPageCombo);
	updatePdfInfo();
	settingsChanged();
}

void ImpositionDialog::rightFileChanged()
{
	populatePageCombo(m_rightFileCombo, m_rightPageCombo);
	updatePdfInfo();
	settingsChanged();
}

void ImpositionDialog::onLeftPageChanged()
{
	settingsChanged();
}

void ImpositionDialog::onRightPageChanged()
{
	settingsChanged();
}

void ImpositionDialog::onPlateSizeChanged()
{
	m_sheetWidthMm = m_sheetWidthSpin->value();
	m_sheetHeightMm = m_sheetHeightSpin->value();
	QSettings settings("Faircode", "CTPImposition");
	settings.setValue("plateWidthMm", m_sheetWidthMm);
	settings.setValue("plateHeightMm", m_sheetHeightMm);
	settings.setValue("plateResolutionXDpi", m_resolutionXSpin->value());
	settings.setValue("plateResolutionYDpi", m_resolutionYSpin->value());
	settingsChanged();
}

void ImpositionDialog::onCenterOnPlateToggled()
{
	bool centered = m_centerOnPlateCheck->isChecked();
	m_marginTopSpin->setEnabled(!centered);
	m_marginBottomSpin->setEnabled(!centered);
	m_marginLeftSpin->setEnabled(!centered);
	m_marginRightSpin->setEnabled(!centered);

	// When turning centering off, the margin spins simply keep whatever
	// values updateCenteringMargins() last wrote -- that's the "starting
	// point for manual adjustment" the operator asked for. When turning it
	// on, settingsChanged() (below) recomputes them immediately.
	settingsChanged();
}

void ImpositionDialog::updateCenteringMargins()
{
	if (m_centerWarningLabel)
		m_centerWarningLabel->setVisible(false);

	if (!m_centerOnPlateCheck->isChecked())
		return;
	if (!m_leftPageInfo.valid || !m_rightPageInfo.valid)
		return; // nothing scanned yet -- nothing to center against

	ImpositionSettings s = collectSettings();
	// pageW/pageH come straight from each PDF's own native page size, never
	// from the margins in `s` -- safe to use even though those margins are
	// about to be overwritten below.
	ScImpositionEngine::PlacedPage left = ScImpositionEngine::computeLeftPlacement(m_leftPageInfo.widthMm, m_leftPageInfo.heightMm, s);
	ScImpositionEngine::PlacedPage right = ScImpositionEngine::computeRightPlacement(m_leftPageInfo.widthMm, m_leftPageInfo.heightMm, m_rightPageInfo.widthMm, m_rightPageInfo.heightMm, s);
	if (left.pageW <= 0.0 || right.pageW <= 0.0)
		return; // page info invalid

	double contentW = left.pageW + s.gutterMm + right.pageW;
	double contentH = qMax(left.pageH, right.pageH);

	bool overflow = (contentW > s.sheetWidthMm) || (contentH > s.sheetHeightMm);
	if (m_centerWarningLabel)
		m_centerWarningLabel->setVisible(overflow);

	double horizontalMargin = qMax(0.0, (s.sheetWidthMm - contentW) / 2.0);
	double verticalMargin = qMax(0.0, (s.sheetHeightMm - contentH) / 2.0);

	// Blocked so writing the computed values doesn't re-enter settingsChanged()
	// (which is what called us in the first place) via each spin's own
	// valueChanged connection.
	m_marginTopSpin->blockSignals(true);
	m_marginBottomSpin->blockSignals(true);
	m_marginLeftSpin->blockSignals(true);
	m_marginRightSpin->blockSignals(true);
	m_marginTopSpin->setValue(verticalMargin);
	m_marginBottomSpin->setValue(verticalMargin);
	m_marginLeftSpin->setValue(horizontalMargin);
	m_marginRightSpin->setValue(horizontalMargin);
	m_marginTopSpin->blockSignals(false);
	m_marginBottomSpin->blockSignals(false);
	m_marginLeftSpin->blockSignals(false);
	m_marginRightSpin->blockSignals(false);
}

void ImpositionDialog::populatePresetCombo(const QString& selectName)
{
	QSettings settings("Faircode", "CTPImposition");
	settings.beginGroup(QStringLiteral("Presets"));
	QStringList names = settings.childGroups();
	settings.endGroup();
	names.sort(Qt::CaseInsensitive);

	m_presetCombo->blockSignals(true);
	m_presetCombo->clear();
	m_presetCombo->addItems(names);
	if (!selectName.isEmpty())
	{
		int idx = names.indexOf(selectName);
		if (idx >= 0)
			m_presetCombo->setCurrentIndex(idx);
	}
	m_presetCombo->blockSignals(false);
}

void ImpositionDialog::applyPreset(const QString& name)
{
	QSettings settings("Faircode", "CTPImposition");
	settings.beginGroup(QStringLiteral("Presets/") + name);
	double w = settings.value("widthMm", m_sheetWidthMm).toDouble();
	double h = settings.value("heightMm", m_sheetHeightMm).toDouble();
	int resX = settings.value("resolutionXDpi", m_resolutionXSpin->value()).toInt();
	int resY = settings.value("resolutionYDpi", m_resolutionYSpin->value()).toInt();
	QString media = settings.value("mediaType").toString();
	QString hotFolder = settings.value("hotFolder").toString();
	settings.endGroup();

	m_plateNameEdit->setText(name);
	m_sheetWidthSpin->setValue(w);
	m_sheetHeightSpin->setValue(h);
	m_resolutionXSpin->setValue(resX);
	m_resolutionYSpin->setValue(resY);

	int mediaIdx = m_mediaTypeCombo->findText(media);
	if (mediaIdx < 0 && !media.isEmpty())
	{
		m_mediaTypeCombo->addItem(media);
		mediaIdx = m_mediaTypeCombo->count() - 1;
	}
	if (mediaIdx >= 0)
		m_mediaTypeCombo->setCurrentIndex(mediaIdx);
	else
		m_mediaTypeCombo->setCurrentText(media);

	if (!hotFolder.isEmpty())
		m_hotFolderEdit->setText(hotFolder);

	onPlateSizeChanged(); // persists as "last used" and refreshes the preview
}

void ImpositionDialog::savePresetClicked()
{
	QString name = m_plateNameEdit->text().trimmed();
	if (name.isEmpty())
	{
		QMessageBox::information(this, tr("Save Preset"), tr("Enter a plate Name first -- it's used as the preset name."));
		return;
	}

	QSettings settings("Faircode", "CTPImposition");
	settings.beginGroup(QStringLiteral("Presets/") + name);
	settings.setValue("widthMm", m_sheetWidthSpin->value());
	settings.setValue("heightMm", m_sheetHeightSpin->value());
	settings.setValue("resolutionXDpi", m_resolutionXSpin->value());
	settings.setValue("resolutionYDpi", m_resolutionYSpin->value());
	settings.setValue("mediaType", m_mediaTypeCombo->currentText().trimmed());
	settings.setValue("hotFolder", m_hotFolderEdit->text().trimmed());
	settings.endGroup();

	populatePresetCombo(name);
}

void ImpositionDialog::loadPresetClicked()
{
	QString name = m_presetCombo->currentText();
	if (name.isEmpty())
		return;
	applyPreset(name);
}

void ImpositionDialog::deletePresetClicked()
{
	QString name = m_presetCombo->currentText();
	if (name.isEmpty())
		return;

	QSettings settings("Faircode", "CTPImposition");
	settings.beginGroup(QStringLiteral("Presets"));
	settings.remove(name);
	settings.endGroup();

	populatePresetCombo();
}

ImpositionSettings ImpositionDialog::collectSettings() const
{
	ImpositionSettings s;
	QDir folder(folderPath());
	QString leftFile = selectedFileName(m_leftFileCombo);
	QString rightFile = selectedFileName(m_rightFileCombo);
	s.leftFilePath = leftFile.isEmpty() ? QString() : folder.filePath(leftFile);
	s.rightFilePath = rightFile.isEmpty() ? QString() : folder.filePath(rightFile);
	s.leftPageIndex = m_leftPageCombo->currentData().isValid() ? m_leftPageCombo->currentData().toInt() : 0;
	s.rightPageIndex = m_rightPageCombo->currentData().isValid() ? m_rightPageCombo->currentData().toInt() : 0;
	s.leftPageNumber = m_leftPageNumberSpin->value();
	s.rightPageNumber = m_rightPageNumberSpin->value();
	s.landscape = true; // this dialog only offers a side-by-side spread
	s.gutterMm = m_gutterSpin->value();
	s.marginLeftMm = m_marginLeftSpin->value();
	s.marginRightMm = m_marginRightSpin->value();
	s.marginTopMm = m_marginTopSpin->value();
	s.marginBottomMm = m_marginBottomSpin->value();
	s.showPrintArea = m_printAreaCheck->isChecked();
	s.showRegmarks = m_regmarksCheck->isChecked();
	s.showAutoMarks = m_autoMarksCheck->isChecked();
	s.showFurnitures = m_furnituresCheck->isChecked();
	s.showColourBar = m_colourBarCheck->isChecked();
	s.showBarcodes = m_barcodesCheck->isChecked();
	s.showGuidelines = m_guidelinesCheck->isChecked();
	s.sheetWidthMm = m_sheetWidthMm;
	s.sheetHeightMm = m_sheetHeightMm;
	s.resolutionXDpi = m_resolutionXSpin->value();
	s.resolutionYDpi = m_resolutionYSpin->value();
	s.plateName = m_plateNameEdit->text().trimmed();
	s.mediaType = m_mediaTypeCombo->currentText().trimmed();
	s.hotFolderPath = m_hotFolderEdit->text().trimmed();
	s.pubCode = m_pubEdit->text().trimmed();
	s.editionCode = m_editionEdit->text().trimmed();
	s.leftPageWidthMm = m_leftPageInfo.widthMm;
	s.leftPageHeightMm = m_leftPageInfo.heightMm;
	s.leftPageValid = m_leftPageInfo.valid;
	s.rightPageWidthMm = m_rightPageInfo.widthMm;
	s.rightPageHeightMm = m_rightPageInfo.heightMm;
	s.rightPageValid = m_rightPageInfo.valid;
	return s;
}

void ImpositionDialog::settingsChanged()
{
	QSettings settings("Faircode", "CTPImposition");
	settings.setValue("lastPubCode", m_pubEdit->text().trimmed());
	settings.setValue("lastEditionCode", m_editionEdit->text().trimmed());
	settings.setValue("lastPlateName", m_plateNameEdit->text().trimmed());
	settings.setValue("lastMediaType", m_mediaTypeCombo->currentText().trimmed());
	settings.setValue("lastHotFolder", m_hotFolderEdit->text().trimmed());
	settings.setValue("lastLeftPageNumber", m_leftPageNumberSpin->value());
	settings.setValue("lastRightPageNumber", m_rightPageNumberSpin->value());

	updateCenteringMargins();

	if (!m_leftPageCombo->count() || !m_rightPageCombo->count())
		return;
	m_preview->setSettings(collectSettings());
}

bool ImpositionDialog::generateImposedPdf(const QString& outputPath, QString* errorMessage)
{
	return ScImpositionEngine::imposeToPdf(collectSettings(), outputPath, errorMessage);
}

void ImpositionDialog::previewPdfClicked()
{
	QString path = ScPaths::tempFileDir() + QStringLiteral("scribus-imposition-preview.pdf");
	QString error;
	setCursor(Qt::WaitCursor);
	bool ok = generateImposedPdf(path, &error);
	unsetCursor();
	if (!ok)
	{
		QMessageBox::critical(this, tr("Imposition Failed"), error);
		return;
	}
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ImpositionDialog::sendToCtpClicked()
{
	ImpositionSettings s = collectSettings();
	if (s.hotFolderPath.isEmpty())
	{
		QMessageBox::warning(this, tr("Send to CTP"), tr("Set a Hot folder in Plate Properties first."));
		return;
	}

	QString outDir = ScPaths::applicationDataDir(true) + QStringLiteral("imposition/");
	QDir().mkpath(outDir);
	QString fileName = QStringLiteral("imposed-p%1-p%2-%3.pdf")
		.arg(s.leftPageNumber)
		.arg(s.rightPageNumber)
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
	QString path = outDir + fileName;

	QString error;
	setCursor(Qt::WaitCursor);
	bool ok = generateImposedPdf(path, &error);
	QStringList plates;
	if (ok)
		plates = ScImpositionEngine::sendToCtp(path, s, &error);
	unsetCursor();

	if (!ok)
	{
		QMessageBox::critical(this, tr("Imposition Failed"), error);
		return;
	}
	if (plates.isEmpty())
	{
		QMessageBox::critical(this, tr("Send to CTP Failed"),
			tr("The imposed PDF was generated:\n%1\n\n"
			   "But converting it to CTP plates failed:\n%2")
				.arg(path, error));
		return;
	}

	QMessageBox::information(this, tr("Sent to CTP"),
		tr("4 plates copied to the hot folder:\n\n%1").arg(plates.join(QStringLiteral("\n"))));
}
