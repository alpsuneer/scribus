/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pdfseparationsviewer.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "fileloader.h"
#include "loadsaveplugin.h"
#include "plugins/formatidlist.h"
#include "scribus.h"
#include "scribusdoc.h"

namespace {
	const QStringList PlateOrder { "Cyan", "Magenta", "Yellow", "Black" };
}

PDFSeparationsViewer::PDFSeparationsViewer(ScribusMainWindow* mainWin, QWidget* parent) : QDialog(parent),
	m_mainWin(mainWin)
{
	setWindowTitle(tr("PDF Separations Viewer"));
	setModal(false);
	setAttribute(Qt::WA_DeleteOnClose);
	resize(880, 560);

	auto* mainLayout = new QVBoxLayout(this);

	// File row
	auto* fileLayout = new QHBoxLayout();
	fileLayout->addWidget(new QLabel(tr("File:")));
	m_fileEdit = new QLineEdit(this);
	m_fileEdit->setReadOnly(true);
	fileLayout->addWidget(m_fileEdit, 1);
	m_browseButton = new QPushButton(tr("Open PDF..."), this);
	fileLayout->addWidget(m_browseButton);
	mainLayout->addLayout(fileLayout);

	// Page row
	auto* pageLayout = new QHBoxLayout();
	pageLayout->addWidget(new QLabel(tr("Page:")));
	m_pageSpin = new QSpinBox(this);
	m_pageSpin->setMinimum(1);
	m_pageSpin->setMaximum(1);
	m_pageSpin->setEnabled(false);
	pageLayout->addWidget(m_pageSpin);
	m_pageOfLabel = new QLabel(tr("of 0"), this);
	pageLayout->addWidget(m_pageOfLabel);
	pageLayout->addStretch(1);
	mainLayout->addLayout(pageLayout);

	// Main split: preview | settings
	auto* splitLayout = new QHBoxLayout();

	auto* previewContainer = new QVBoxLayout();
	m_previewArea = new QScrollArea(this);
	m_previewArea->setMinimumWidth(420);
	m_previewLabel = new QLabel(this);
	m_previewLabel->setAlignment(Qt::AlignCenter);
	m_previewArea->setWidget(m_previewLabel);
	// Not resizable: zoom controls the label's size explicitly (see
	// applyZoom()). A resizable scroll area would fight that by snapping
	// the label back to viewport size on every layout pass, which is fine
	// for "fit" but breaks any zoom level that needs scrollbars.
	m_previewArea->setWidgetResizable(false);
	m_previewArea->setAlignment(Qt::AlignCenter);
	m_previewArea->viewport()->installEventFilter(this);
	previewContainer->addWidget(m_previewArea, 1);

	auto* zoomLayout = new QHBoxLayout();
	m_zoomOutButton = new QPushButton(QStringLiteral("−"), this);
	m_zoomOutButton->setFixedWidth(28);
	zoomLayout->addWidget(m_zoomOutButton);
	m_zoomCombo = new QComboBox(this);
	m_zoomCombo->setEditable(true);
	m_zoomCombo->addItems({ "25%", "50%", "75%", "100%", "150%", "200%", "400%", "800%" });
	m_zoomCombo->setCurrentText(tr("Fit"));
	m_zoomCombo->setMinimumWidth(90);
	zoomLayout->addWidget(m_zoomCombo);
	m_zoomInButton = new QPushButton(QStringLiteral("+"), this);
	m_zoomInButton->setFixedWidth(28);
	zoomLayout->addWidget(m_zoomInButton);
	m_zoomFitButton = new QPushButton(tr("Fit"), this);
	zoomLayout->addWidget(m_zoomFitButton);
	m_zoomActualButton = new QPushButton(tr("Actual"), this);
	zoomLayout->addWidget(m_zoomActualButton);
	zoomLayout->addStretch(1);
	m_zoomStatusLabel = new QLabel(this);
	zoomLayout->addWidget(m_zoomStatusLabel);
	previewContainer->addLayout(zoomLayout);

	splitLayout->addLayout(previewContainer, 1);

	auto* settingsLayout = new QVBoxLayout();

	auto* displayGroup = new QGroupBox(tr("Display Settings"), this);
	auto* displayLayout = new QVBoxLayout(displayGroup);
	m_compositeRadio = new QRadioButton(tr("Composite CMYK"), displayGroup);
	m_plateRadio = new QRadioButton(tr("Individual plate"), displayGroup);
	m_inkCoverageRadio = new QRadioButton(tr("Ink Coverage"), displayGroup);
	m_compositeRadio->setChecked(true);
	auto* modeGroup = new QButtonGroup(this);
	modeGroup->addButton(m_compositeRadio);
	modeGroup->addButton(m_plateRadio);
	modeGroup->addButton(m_inkCoverageRadio);
	displayLayout->addWidget(m_compositeRadio);
	displayLayout->addWidget(m_plateRadio);
	displayLayout->addWidget(m_inkCoverageRadio);
	auto* qualityLayout = new QHBoxLayout();
	qualityLayout->addWidget(new QLabel(tr("Preview Quality (പ്രിവ്യൂ ക്വാളിറ്റി):")));
	m_qualityCombo = new QComboBox(displayGroup);
	// "Fast" must stay index 0 / the first-added item: it's both the
	// pre-any-file-opened default (matches the dialog's initial
	// m_currentResolutionDPI) and setQualityComboSilently()'s fallback when a
	// requested DPI isn't one of these three.
	m_qualityCombo->addItem(tr("Fast (72 DPI) - വേഗം"), 72);
	m_qualityCombo->addItem(tr("Normal (150 DPI) - സാധാരണ"), 150);
	m_qualityCombo->addItem(tr("High (300 DPI) - ഉയർന്നത്"), 300);
	m_qualityCombo->setCurrentIndex(0);
	qualityLayout->addWidget(m_qualityCombo, 1);
	displayLayout->addLayout(qualityLayout);
	auto* thresholdLayout = new QHBoxLayout();
	thresholdLayout->addWidget(new QLabel(tr("Threshold:")));
	m_thresholdSpin = new QDoubleSpinBox(displayGroup);
	m_thresholdSpin->setSuffix(" %");
	m_thresholdSpin->setRange(100.0, 400.0);
	m_thresholdSpin->setValue(240.0);
	thresholdLayout->addWidget(m_thresholdSpin);
	thresholdLayout->addStretch(1);
	displayLayout->addLayout(thresholdLayout);
	settingsLayout->addWidget(displayGroup);

	auto* sepGroup = new QGroupBox(tr("Separations"), this);
	auto* sepLayout = new QVBoxLayout(sepGroup);
	m_plateTable = new QTableWidget(4, 3, sepGroup);
	m_plateTable->setHorizontalHeaderLabels({ QString(), tr("Plate"), tr("Coverage") });
	m_plateTable->verticalHeader()->hide();
	m_plateTable->horizontalHeader()->setStretchLastSection(true);
	m_plateTable->setSelectionMode(QAbstractItemView::SingleSelection);
	m_plateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_plateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_plateTable->setColumnWidth(0, 24);
	for (int row = 0; row < PlateOrder.count(); ++row)
	{
		auto* check = new QCheckBox(sepGroup);
		check->setChecked(true);
		connect(check, &QCheckBox::clicked, this, [this, row](bool) { plateCheckToggled(row); });
		m_plateTable->setCellWidget(row, 0, check);
		m_plateTable->setItem(row, 1, new QTableWidgetItem(PlateOrder.at(row)));
		auto* covItem = new QTableWidgetItem(QStringLiteral("--"));
		covItem->setFlags(covItem->flags() & ~Qt::ItemIsEditable);
		m_plateTable->setItem(row, 2, covItem);
	}
	connect(m_plateTable, &QTableWidget::cellClicked, this, &PDFSeparationsViewer::plateRowClicked);
	sepLayout->addWidget(m_plateTable);
	m_totalLabel = new QLabel(tr("Total: --"), sepGroup);
	sepLayout->addWidget(m_totalLabel);
	settingsLayout->addWidget(sepGroup);

	auto* warnGroup = new QGroupBox(tr("Warnings"), this);
	auto* warnLayout = new QVBoxLayout(warnGroup);
	m_warningsLabel = new QLabel(tr("None"), warnGroup);
	m_warningsLabel->setWordWrap(true);
	warnLayout->addWidget(m_warningsLabel);
	settingsLayout->addWidget(warnGroup);

	m_exportButton = new QPushButton(tr("Export Separations..."), this);
	m_exportButton->setEnabled(false);
	settingsLayout->addWidget(m_exportButton);

	auto* progressLayout = new QHBoxLayout();
	m_renderProgressBar = new QProgressBar(this);
	m_renderProgressBar->setRange(0, 0); // indeterminate: a single Ghostscript
	// pass has no real percent-complete to report (see reloadCurrentPage()).
	m_renderProgressBar->setTextVisible(false);
	m_renderProgressBar->setMaximumHeight(14);
	m_renderProgressBar->setVisible(false);
	progressLayout->addWidget(m_renderProgressBar, 1);
	m_cancelRenderButton = new QPushButton(tr("Cancel"), this);
	m_cancelRenderButton->setVisible(false);
	progressLayout->addWidget(m_cancelRenderButton);
	settingsLayout->addLayout(progressLayout);

	m_statusLabel = new QLabel(this);
	m_statusLabel->setWordWrap(true);
	settingsLayout->addWidget(m_statusLabel);

	settingsLayout->addStretch(1);
	splitLayout->addLayout(settingsLayout);
	mainLayout->addLayout(splitLayout, 1);

	// Bottom buttons
	auto* bottomLayout = new QHBoxLayout();
	m_closeButton = new QPushButton(tr("Close"), this);
	bottomLayout->addWidget(m_closeButton);
	bottomLayout->addStretch(1);
	m_placeButton = new QPushButton(tr("Place This PDF in Document..."), this);
	m_placeButton->setEnabled(false);
	bottomLayout->addWidget(m_placeButton);
	mainLayout->addLayout(bottomLayout);

	connect(m_browseButton, &QPushButton::clicked, this, &PDFSeparationsViewer::browseForFile);
	connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFSeparationsViewer::reloadCurrentPage);
	connect(m_compositeRadio, &QRadioButton::toggled, this, &PDFSeparationsViewer::updateDisplay);
	connect(m_plateRadio, &QRadioButton::toggled, this, &PDFSeparationsViewer::updateDisplay);
	connect(m_inkCoverageRadio, &QRadioButton::toggled, this, &PDFSeparationsViewer::updateDisplay);
	connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateDisplay(); rebuildWarnings(); });
	connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFSeparationsViewer::qualityComboChanged);
	connect(m_cancelRenderButton, &QPushButton::clicked, this, &PDFSeparationsViewer::cancelRender);
	connect(m_exportButton, &QPushButton::clicked, this, &PDFSeparationsViewer::exportSeparations);
	connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
	connect(m_placeButton, &QPushButton::clicked, this, &PDFSeparationsViewer::placeInDocument);

	connect(m_zoomOutButton, &QPushButton::clicked, this, &PDFSeparationsViewer::zoomOut);
	connect(m_zoomInButton, &QPushButton::clicked, this, &PDFSeparationsViewer::zoomIn);
	connect(m_zoomFitButton, &QPushButton::clicked, this, &PDFSeparationsViewer::zoomFit);
	connect(m_zoomActualButton, &QPushButton::clicked, this, &PDFSeparationsViewer::zoomActual);
	// Qt6 only kept the int overload of activated(); route it back to the text.
	connect(m_zoomCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { zoomComboActivated(m_zoomCombo->currentText()); });
	connect(m_zoomCombo->lineEdit(), &QLineEdit::returnPressed, this, [this]() { zoomComboActivated(m_zoomCombo->currentText()); });

	auto* zoomInShortcut1 = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
	connect(zoomInShortcut1, &QShortcut::activated, this, &PDFSeparationsViewer::zoomIn);
	auto* zoomInShortcut2 = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this);
	connect(zoomInShortcut2, &QShortcut::activated, this, &PDFSeparationsViewer::zoomIn);
	auto* zoomOutShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
	connect(zoomOutShortcut, &QShortcut::activated, this, &PDFSeparationsViewer::zoomOut);
	auto* zoomFitShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this);
	connect(zoomFitShortcut, &QShortcut::activated, this, &PDFSeparationsViewer::zoomFit);
	auto* zoomActualShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+1")), this);
	connect(zoomActualShortcut, &QShortcut::activated, this, &PDFSeparationsViewer::zoomActual);
}

PDFSeparationsViewer::~PDFSeparationsViewer() = default;

void PDFSeparationsViewer::setStatus(const QString& message, bool isError)
{
	m_statusLabel->setText(message);
	m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #a00;") : QString());
}

void PDFSeparationsViewer::browseForFile()
{
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open PDF"), QString(), tr("PDF Files (*.pdf)"));
	if (!fileName.isEmpty())
		openFile(fileName);
}

int PDFSeparationsViewer::recommendedResolutionDPI(const QSizeF& pageSizePts)
{
	// Only two tiers, matching what m_qualityCombo actually offers (72/150/
	// 300): up to A4 keeps full preview quality; A3 and anything bigger --
	// including the full-broadsheet ad pages this tool exists for -- goes to
	// the fastest tier. There's no separate 100 DPI tier for A3 specifically
	// because the dropdown has nothing at 100 to show for it.
	constexpr double kA4LongEdgePts = 842.0; // ISO A4 long edge, 29.7cm
	constexpr double kTolerancePts = 5.0;
	const double longEdge = qMax(pageSizePts.width(), pageSizePts.height());
	return (longEdge <= kA4LongEdgePts + kTolerancePts) ? 150 : 72;
}

bool PDFSeparationsViewer::isBroadsheetSize(const QSizeF& pageSizePts)
{
	// Deliberately stricter than recommendedResolutionDPI()'s A4 cutoff: this
	// gates the interactive "this will be slow" dialog, which should only
	// interrupt the user for genuinely large jobs, not every A3 page.
	constexpr double kA3LongEdgePts = 1191.0; // ISO A3 long edge, 42.0cm
	constexpr double kTolerancePts = 5.0;
	const double longEdge = qMax(pageSizePts.width(), pageSizePts.height());
	return longEdge > kA3LongEdgePts + kTolerancePts;
}

void PDFSeparationsViewer::setQualityComboSilently(int dpi)
{
	int idx = m_qualityCombo->findData(dpi);
	if (idx < 0)
		idx = 0; // fall back to "Fast", the first-added item
	const QSignalBlocker blocker(m_qualityCombo);
	m_qualityCombo->setCurrentIndex(idx);
	m_currentResolutionDPI = m_qualityCombo->itemData(idx).toInt();
}

void PDFSeparationsViewer::openFile(const QString& fileName)
{
	PDFSepInfo info;
	if (!pdfSepGetInfo(fileName, 1, info))
	{
		QMessageBox::warning(this, tr("PDF Separations Viewer"), info.errorMessage);
		return;
	}

	// Judge against whatever quality was selected before this file was
	// opened (carried over from a previous file, or the dialog's initial
	// Fast default) -- not against the recommendation computed below.
	if (isBroadsheetSize(info.pageSizePts) && m_currentResolutionDPI > 72)
	{
		QMessageBox box(this);
		box.setIcon(QMessageBox::Warning);
		box.setWindowTitle(tr("Large PDF Detected - വലിയ PDF കണ്ടെത്തി"));
		box.setText(tr("This PDF is broadsheet size. -- ഈ PDF broadsheet size ആണ്.\n\n"
						"Preview may take a couple of minutes at the current resolution. -- "
						"നിലവിലുള്ള resolution-ൽ preview-ന് 2-3 minutes എടുക്കാം.\n\n"
						"Reduce quality to Fast (72 DPI) for a quicker preview? -- "
						"വേഗത്തിന് Fast (72 DPI) തിരഞ്ഞെടുക്കണോ?"));
		QPushButton* useFastButton = box.addButton(tr("Use Fast"), QMessageBox::AcceptRole);
		box.addButton(tr("Keep Current"), QMessageBox::RejectRole);
		QPushButton* cancelButton = box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(useFastButton);
		box.exec();
		if (box.clickedButton() == cancelButton)
			return; // leave any previously loaded file/state untouched
		if (box.clickedButton() == useFastButton)
			setQualityComboSilently(72);
		// "Keep Current": m_currentResolutionDPI / m_qualityCombo stay as they were.
	}
	else
	{
		setQualityComboSilently(recommendedResolutionDPI(info.pageSizePts));
	}

	m_currentFile = fileName;
	m_fileEdit->setText(fileName);
	m_pageOfLabel->setText(tr("of %1").arg(info.pageCount));
	m_pageSpin->setMaximum(qMax(1, info.pageCount));
	m_pageSpin->setEnabled(info.pageCount > 1);

	m_placeButton->setEnabled(m_mainWin && m_mainWin->HaveDoc);

	// Reset to page 1; if it's already 1, valueChanged() won't fire, so
	// trigger the render explicitly.
	if (m_pageSpin->value() == 1)
		reloadCurrentPage();
	else
		m_pageSpin->setValue(1);
}

void PDFSeparationsViewer::reloadCurrentPage()
{
	if (m_currentFile.isEmpty())
		return;

	if (!m_tempDirObj.isValid())
	{
		setStatus(tr("Could not create a temporary directory for rendering."), true);
		return;
	}

	setStatus(tr("Analyzing PDF... -- PDF വിശകലനം ചെയ്യുന്നു..."), false);
	qApp->processEvents();

	const int pageNumber = m_pageSpin->value();

	if (!pdfSepGetInfo(m_currentFile, pageNumber, m_pageInfo))
	{
		setStatus(m_pageInfo.errorMessage, true);
		return;
	}

	// m_currentResolutionDPI comes from m_qualityCombo: user-selected, or set
	// silently by the smart default / broadsheet-warning logic in openFile().
	const int resolutionDPI = m_currentResolutionDPI;

	QString renderStatus = tr("Rendering separations with Ghostscript...");
	if (resolutionDPI >= 150 && isBroadsheetSize(m_pageInfo.pageSizePts))
		renderStatus += QLatin1Char(' ') + tr("This may take a minute or more at this resolution.");
	setStatus(renderStatus, false);

	m_renderCancelRequested = false;
	m_cancelRenderButton->setVisible(true);
	m_cancelRenderButton->setEnabled(true);
	m_renderProgressBar->setVisible(true);
	qApp->processEvents();

	// tiffsep computes all four plates plus the composite in one Ghostscript
	// pass -- there is no real "now doing Cyan, now doing Magenta" boundary
	// to report, so progress here is a single indeterminate stage rather
	// than four fabricated ones. Genuine per-plate progress (below) starts
	// once these files actually exist and are loaded one at a time.
	const bool genOk = pdfSepGenerate(m_currentFile, pageNumber, resolutionDPI, m_tempDirObj.path(), m_sepResult, &m_renderCancelRequested);

	m_cancelRenderButton->setVisible(false);
	m_renderProgressBar->setVisible(false);

	if (!genOk)
	{
		if (m_renderCancelRequested)
			setStatus(tr("Rendering cancelled."), false);
		else
			setStatus(m_sepResult.errorMessage, true);
		m_exportButton->setEnabled(false);
		return;
	}

	static const QStringList plateLoadStatus {
		tr("Cyan render ചെയ്യുന്നു..."),
		tr("Magenta render ചെയ്യുന്നു..."),
		tr("Yellow render ചെയ്യുന്നു..."),
		tr("Black render ചെയ്യുന്നു...")
	};

	m_plateImages.clear();
	for (int i = 0; i < m_sepResult.plates.count(); ++i)
	{
		const PDFSepPlate& plate = m_sepResult.plates.at(i);
		setStatus(i < plateLoadStatus.count() ? plateLoadStatus.at(i) : tr("Loading %1 separation...").arg(plate.name), false);
		qApp->processEvents();

		QImage plateImg = pdfSepLoadPlateImage(plate.tiffPath);
		if (plateImg.isNull())
		{
			setStatus(tr("Could not read the %1 separation Ghostscript wrote to \"%2\".").arg(plate.name, plate.tiffPath), true);
			m_exportButton->setEnabled(false);
			m_plateImages.clear();
			return;
		}
		m_plateImages.append(plateImg);
	}

	for (int row = 0; row < m_sepResult.plates.count() && row < m_plateTable->rowCount(); ++row)
	{
		const PDFSepPlate& plate = m_sepResult.plates.at(row);
		m_plateTable->item(row, 2)->setText(QStringLiteral("%1%").arg(plate.coveragePercent, 0, 'f', 0));
	}
	m_totalLabel->setText(tr("Total: %1%").arg(m_sepResult.totalCoveragePercent, 0, 'f', 0));

	setStatus(tr("Building composite preview... -- Composite തയ്യാറാക്കുന്നു..."), false);
	qApp->processEvents();

	// The page-average total above will stay well under any sane threshold
	// even when a small region is badly over-inked (e.g. a rich-black
	// logo), so warnings are judged against the worst single pixel instead
	// -- the same value the Ink Coverage view highlights in red.
	m_peakCoveragePercent = 0.0;
	m_richBlackPercent = 0.0;
	if (m_plateImages.count() == PlateOrder.count()) // expects Cyan, Magenta, Yellow, Black in that order
	{
		const int w = m_plateImages.constFirst().width();
		const int h = m_plateImages.constFirst().height();
		int peakSample = 0;
		quint64 darkPixels = 0;
		quint64 richBlackPixels = 0;
		for (int y = 0; y < h; ++y)
		{
			const uchar* cLine = m_plateImages.at(0).constScanLine(y);
			const uchar* mLine = m_plateImages.at(1).constScanLine(y);
			const uchar* yLine = m_plateImages.at(2).constScanLine(y);
			const uchar* kLine = m_plateImages.at(3).constScanLine(y);
			for (int x = 0; x < w; ++x)
			{
				const int inkC = 255 - cLine[x];
				const int inkM = 255 - mLine[x];
				const int inkY = 255 - yLine[x];
				const int inkK = 255 - kLine[x];
				const int sum = inkC + inkM + inkY + inkK;
				peakSample = qMax(peakSample, sum);

				// A visually dark pixel (heavy combined ink) whose darkness
				// comes almost entirely from C/M/Y rather than K is the
				// signature of black that was rebuilt from RGB rather than
				// carried through as pure K -- e.g. a PDF placed as a
				// picture, rasterized through an RGB-only device, then
				// reconverted to CMYK on export. Deliberate rich black
				// (a real prepress choice for large solids) still runs K
				// high alongside the CMY; this doesn't.
				if (sum > 380) // "dark": combined ink over ~150% of one channel
				{
					++darkPixels;
					const double cmyAvg = (inkC + inkM + inkY) / 3.0;
					if (inkK < 77 && cmyAvg > 128) // K < 30%, C/M/Y averaging > 50%
						++richBlackPixels;
				}
			}
		}
		m_peakCoveragePercent = (double(peakSample) / 255.0) * 100.0;
		if (darkPixels > 0)
			m_richBlackPercent = (double(richBlackPixels) / double(darkPixels)) * 100.0;
	}

	m_exportButton->setEnabled(true);
	setStatus(QString(), false);

	rebuildWarnings();
	updateDisplay();
}

void PDFSeparationsViewer::qualityComboChanged(int index)
{
	const int dpi = m_qualityCombo->itemData(index).toInt();
	if (dpi <= 0 || dpi == m_currentResolutionDPI)
		return;
	m_currentResolutionDPI = dpi;
	reloadCurrentPage();
}

void PDFSeparationsViewer::cancelRender()
{
	m_renderCancelRequested = true;
	m_cancelRenderButton->setEnabled(false);
	setStatus(tr("Cancelling..."), false);
}

bool PDFSeparationsViewer::isPlateChecked(int index) const
{
	auto* check = qobject_cast<QCheckBox*>(m_plateTable->cellWidget(index, 0));
	return check && check->isChecked();
}

QImage PDFSeparationsViewer::compositeFromCheckedPlates() const
{
	// Requires Cyan, Magenta, Yellow, Black in that order (see PlateOrder /
	// util_pdfsep.cpp's plateNames) so the per-row pointers below line up
	// with C/M/Y/K positionally instead of doing a string compare per pixel.
	if (m_plateImages.count() != PlateOrder.count())
		return {};

	const bool cChecked = isPlateChecked(0);
	const bool mChecked = isPlateChecked(1);
	const bool yChecked = isPlateChecked(2);
	const bool kChecked = isPlateChecked(3);

	const QImage& first = m_plateImages.constFirst();
	QImage out(first.size(), QImage::Format_ARGB32);

	const int w = out.width();
	const int h = out.height();
	for (int y = 0; y < h; ++y)
	{
		const uchar* cLine = m_plateImages.at(0).constScanLine(y);
		const uchar* mLine = m_plateImages.at(1).constScanLine(y);
		const uchar* yLine = m_plateImages.at(2).constScanLine(y);
		const uchar* kLine = m_plateImages.at(3).constScanLine(y);
		QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < w; ++x)
		{
			// Plate TIFFs are ordinary grayscale: 255 = no ink, 0 = full ink,
			// so invert each sample to get an ink amount (0..255) first.
			const int cyan    = cChecked ? (255 - cLine[x]) : 0;
			const int magenta = mChecked ? (255 - mLine[x]) : 0;
			const int yellow  = yChecked ? (255 - yLine[x]) : 0;
			const int black   = kChecked ? (255 - kLine[x]) : 0;

			// Proper subtractive CMYK -> RGB: each channel's ink and the
			// black plate each independently filter out reflected light, so
			// they combine multiplicatively, not by adding ink amounts and
			// clamping. The old additive-clamp version matched this exactly
			// whenever K was unchecked (K=0 reduces both formulas to the
			// same 255-C), but overdarkened everywhere ink channels
			// overlapped with K left on.
			const int r = ((255 - cyan) * (255 - black)) / 255;
			const int g = ((255 - magenta) * (255 - black)) / 255;
			const int b = ((255 - yellow) * (255 - black)) / 255;
			dst[x] = qRgb(r, g, b);
		}
	}
	return out;
}

QImage PDFSeparationsViewer::inkCoverageImage() const
{
	if (m_plateImages.isEmpty())
		return {};

	const QImage& first = m_plateImages.constFirst();
	QImage out(first.size(), QImage::Format_ARGB32);

	const double thresholdSample = (m_thresholdSpin->value() / 100.0) * 255.0;
	const int w = out.width();
	const int h = out.height();
	for (int y = 0; y < h; ++y)
	{
		QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < w; ++x)
		{
			int sum = 0;
			for (const QImage& plate : m_plateImages)
				sum += (255 - plate.constScanLine(y)[x]);
			if (sum > thresholdSample)
				dst[x] = qRgb(255, 0, 0);
			else
			{
				const int shade = 255 - qMin(255, sum);
				dst[x] = qRgb(shade, shade, shade);
			}
		}
	}
	return out;
}

void PDFSeparationsViewer::updateDisplay()
{
	rebuildPreviewImage();
	applyZoom();
}

void PDFSeparationsViewer::rebuildPreviewImage()
{
	if (m_plateImages.isEmpty())
	{
		m_currentDisplayImage = QImage();
		return;
	}

	if (m_inkCoverageRadio->isChecked())
		m_currentDisplayImage = inkCoverageImage();
	else if (m_plateRadio->isChecked())
		m_currentDisplayImage = (m_selectedPlate >= 0 && m_selectedPlate < m_plateImages.count()) ? m_plateImages.at(m_selectedPlate) : QImage();
	else
		m_currentDisplayImage = compositeFromCheckedPlates();
}

double PDFSeparationsViewer::fitZoomFactor() const
{
	if (m_currentDisplayImage.isNull())
		return 1.0;
	const int viewW = m_previewArea->viewport()->width() - 6;
	const int viewH = m_previewArea->viewport()->height() - 6;
	if (viewW <= 0 || viewH <= 0)
		return 1.0;
	const double sx = double(viewW) / double(m_currentDisplayImage.width());
	const double sy = double(viewH) / double(m_currentDisplayImage.height());
	// Fit means "show the whole page", so shrink to whichever axis is
	// tighter; never upscale past 100% just because the viewport is huge
	// and the page is small -- that's what "Actual" is for.
	return qMin(1.0, qMin(sx, sy));
}

void PDFSeparationsViewer::applyZoom()
{
	if (m_currentDisplayImage.isNull())
	{
		m_previewLabel->setPixmap(QPixmap());
		m_zoomStatusLabel->clear();
		return;
	}

	const double factor = m_fitToWindow ? fitZoomFactor() : m_zoomFactor;
	const QSize targetSize = (m_currentDisplayImage.size() * factor).expandedTo(QSize(1, 1));

	QPixmap pixmap = QPixmap::fromImage(m_currentDisplayImage);
	if (targetSize != pixmap.size())
		pixmap = pixmap.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	m_previewLabel->setPixmap(pixmap);
	m_previewLabel->resize(pixmap.size());

	const int percent = qRound(factor * 100.0);
	m_zoomStatusLabel->setText(tr("Zoom: %1%").arg(percent));
	if (!m_zoomCombo->lineEdit()->hasFocus())
		m_zoomCombo->setCurrentText(m_fitToWindow ? tr("Fit (%1%)").arg(percent) : QStringLiteral("%1%").arg(percent));
}

void PDFSeparationsViewer::setZoomFactor(double factor)
{
	m_fitToWindow = false;
	m_zoomFactor = qBound(0.1, factor, 8.0);
	applyZoom();
}

void PDFSeparationsViewer::zoomIn()
{
	const double current = m_fitToWindow ? fitZoomFactor() : m_zoomFactor;
	setZoomFactor(current + 0.25);
}

void PDFSeparationsViewer::zoomOut()
{
	const double current = m_fitToWindow ? fitZoomFactor() : m_zoomFactor;
	setZoomFactor(current - 0.25);
}

void PDFSeparationsViewer::zoomFit()
{
	m_fitToWindow = true;
	applyZoom();
}

void PDFSeparationsViewer::zoomActual()
{
	setZoomFactor(1.0);
}

void PDFSeparationsViewer::zoomComboActivated(const QString& text)
{
	QString t = text.trimmed();
	if (t.startsWith(tr("Fit"), Qt::CaseInsensitive))
	{
		zoomFit();
		return;
	}
	t.remove(QLatin1Char('%'));
	bool ok = false;
	const double value = t.toDouble(&ok);
	if (ok && value > 0)
		setZoomFactor(value / 100.0);
	else
		applyZoom(); // bad input: just redisplay the current zoom, don't crash
}

bool PDFSeparationsViewer::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_previewArea->viewport())
	{
		if (event->type() == QEvent::Wheel)
		{
			auto* wheelEvent = static_cast<QWheelEvent*>(event);
			if (wheelEvent->modifiers() & Qt::ControlModifier)
			{
				if (!m_currentDisplayImage.isNull())
				{
					const double oldFactor = m_fitToWindow ? fitZoomFactor() : m_zoomFactor;
					const QPointF cursorPos = wheelEvent->position();
					const QPointF contentPos(
						m_previewArea->horizontalScrollBar()->value() + cursorPos.x(),
						m_previewArea->verticalScrollBar()->value() + cursorPos.y());
					const QPointF imagePos = contentPos / oldFactor;

					const double steps = wheelEvent->angleDelta().y() / 120.0;
					setZoomFactor(oldFactor + steps * 0.1);

					const double newFactor = m_fitToWindow ? fitZoomFactor() : m_zoomFactor;
					m_previewArea->horizontalScrollBar()->setValue(qRound(imagePos.x() * newFactor - cursorPos.x()));
					m_previewArea->verticalScrollBar()->setValue(qRound(imagePos.y() * newFactor - cursorPos.y()));
				}
				return true;
			}
			if (wheelEvent->modifiers() & Qt::ShiftModifier)
			{
				m_previewArea->horizontalScrollBar()->setValue(m_previewArea->horizontalScrollBar()->value() - wheelEvent->angleDelta().y());
				return true;
			}
			return false; // plain wheel: let the scroll area's default vertical scroll happen
		}
		else if (event->type() == QEvent::MouseButtonPress)
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(event);
			if (mouseEvent->button() == Qt::MiddleButton)
			{
				m_panning = true;
				m_panLastPos = mouseEvent->position().toPoint();
				m_previewArea->viewport()->setCursor(Qt::ClosedHandCursor);
				return true;
			}
		}
		else if (event->type() == QEvent::MouseMove)
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(event);
			if (m_panning)
			{
				const QPoint delta = mouseEvent->position().toPoint() - m_panLastPos;
				m_panLastPos = mouseEvent->position().toPoint();
				m_previewArea->horizontalScrollBar()->setValue(m_previewArea->horizontalScrollBar()->value() - delta.x());
				m_previewArea->verticalScrollBar()->setValue(m_previewArea->verticalScrollBar()->value() - delta.y());
				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonRelease)
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(event);
			if (mouseEvent->button() == Qt::MiddleButton && m_panning)
			{
				m_panning = false;
				m_previewArea->viewport()->unsetCursor();
				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonDblClick)
		{
			if (m_fitToWindow)
				zoomActual();
			else
				zoomFit();
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void PDFSeparationsViewer::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	if (m_fitToWindow)
		applyZoom();
}

void PDFSeparationsViewer::plateRowClicked(int row, int)
{
	m_selectedPlate = row;
	if (!m_plateRadio->isChecked())
		m_plateRadio->setChecked(true);
	else
		updateDisplay();
}

void PDFSeparationsViewer::plateCheckToggled(int)
{
	updateDisplay();
}

void PDFSeparationsViewer::rebuildWarnings()
{
	QStringList warnings;
	if (m_peakCoveragePercent > m_thresholdSpin->value())
		warnings << tr("Peak ink coverage %1% exceeds the %2% limit (see the red areas in Ink Coverage view).").arg(m_peakCoveragePercent, 0, 'f', 0).arg(m_thresholdSpin->value(), 0, 'f', 0);
	if (m_richBlackPercent > 25.0)
		warnings << tr("Rich Black detected (%1% of dark pixels are built from C/M/Y rather than K). "
					   "May cause registration issues in newspaper print -- pure K text/lines are usually meant to be K only.")
					   .arg(m_richBlackPercent, 0, 'f', 0);

	m_warningsLabel->setText(warnings.isEmpty() ? tr("None") : warnings.join(QStringLiteral("\n")));
}

void PDFSeparationsViewer::exportSeparations()
{
	if (!m_sepResult.ok)
		return;

	QString targetDir = QFileDialog::getExistingDirectory(this, tr("Export Separations To"));
	if (targetDir.isEmpty())
		return;

	QFileInfo srcInfo(m_currentFile);
	const QString stem = srcInfo.completeBaseName();
	const int pageNumber = m_pageSpin->value();

	bool allOk = true;
	auto copyOne = [&](const QString& srcPath, const QString& suffix) {
		const QString destPath = QDir(targetDir).filePath(QStringLiteral("%1_page%2_%3.tif").arg(stem).arg(pageNumber).arg(suffix));
		QFile::remove(destPath);
		if (!QFile::copy(srcPath, destPath))
			allOk = false;
	};

	copyOne(m_sepResult.compositeTiffPath, QStringLiteral("Composite"));
	for (const PDFSepPlate& plate : std::as_const(m_sepResult.plates))
		copyOne(plate.tiffPath, plate.name);

	if (allOk)
		QMessageBox::information(this, tr("Export Separations"), tr("Separation TIFFs saved to \"%1\".").arg(targetDir));
	else
		QMessageBox::warning(this, tr("Export Separations"), tr("Some files could not be copied to \"%1\".").arg(targetDir));
}

void PDFSeparationsViewer::placeInDocument()
{
	if (!m_mainWin || !m_mainWin->HaveDoc || m_currentFile.isEmpty())
		return;

	// NOT the image-frame/loadPict path used until now (and not what
	// dragging a PDF onto the canvas does either): ScImgDataLoader_PDF
	// rasterizes the page through Ghostscript's png16m/pngalpha device --
	// RGB only, see scimgdataloader_pdf.cpp -- and hardcodes
	// m_imageInfoRecord.colorspace = ColorSpaceRGB. A pure-K newspaper ad
	// placed that way has its black irreversibly blended into RGB, then
	// reconstituted as rich black on the next CMYK export. That is what
	// produced the Deshabhimani bug report, not a missing LittleCMS
	// black-preservation flag -- color management never even runs here;
	// the data is gone before it could apply.
	//
	// The PDF import plugin's vector path (SlaOutputDev, used below via the
	// generic loader) reads PDF operators directly and preserves exact
	// DeviceCMYK / Separation-on-CMYK values (confirmed by reading
	// AnoOutputDev::getColor() in slaoutput.cpp) -- that's what this uses
	// instead.
	//
	// MUST include lfInteractive. Without it, m_interactive is false inside
	// ImportPdfPlugin, and importpdf.cpp:161 branches on "!m_interactive"
	// (independent of lfUseCurrentPage) to force-insert a new blank page via
	// ScribusDoc::addPage(0) before the real import even starts. In one
	// production run that addPage() call threw, and since Qt does not
	// propagate exceptions out of a slot, the whole application aborted
	// (SIGABRT via Qt's terminate handler -- confirmed from the crash log's
	// backtrace, not guessed). Yes, this reopens the redundant page-range
	// dialog on top of this one; that's a real cost, but it's the
	// established, working code path -- the same one File > Import > PDF
	// already uses -- and it does not crash.
	FileLoader fileLoader(m_currentFile);
	int testResult = fileLoader.testFile();
	if (testResult == -1 || testResult < FORMATID_FIRSTUSER)
	{
		QMessageBox::warning(this, tr("PDF Separations Viewer"), tr("This file could not be recognised as an importable format."));
		return;
	}

	const FileFormat* fmt = LoadSavePlugin::getFormatById(testResult);
	if (!fmt)
		return;

	m_mainWin->doc->dontResize = true;
	fmt->loadFile(m_currentFile, LoadSavePlugin::lfUseCurrentPage | LoadSavePlugin::lfInteractive);
	m_mainWin->doc->dontResize = false;

	close();
}
