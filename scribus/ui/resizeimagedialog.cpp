/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "pageitem.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneergroupedit.h"
#include "ui/resizeimagedialog.h"
#include "undomanager.h"
#include "util.h"
#include "suneerimageresample.h"
#include <QComboBox>
#include <QDoubleSpinBox>

namespace
{
	QString humanSize(qint64 bytes)
	{
		return QLocale().formattedDataSize(bytes, 1);
	}
}

void ResizeImageDialog::openForSelection(ScribusDoc* doc, QWidget* parent)
{
	if (!doc || doc->m_Selection->isEmpty())
		return;
	PageItem* item = doc->m_Selection->itemAt(0);
	// One group holding exactly one image (image + caption): that image.
	if (const PageItem* group = SuneerGroupEdit::soleGroup(doc))
	{
		const QList<PageItem*> images = SuneerGroupEdit::imageChildren(group);
		item = (images.count() == 1) ? images.first() : nullptr;
	}
	if (!item || !item->isImageFrame() || item->Pfile.isEmpty() || !item->imageIsAvailable)
		return;
	ResizeImageDialog dlg(doc, item, QImage(), parent);
	dlg.exec();
}

// Units for the size fields: inches, mm, cm (remembered).
static double unitPerInch(int u) { return u == 1 ? 25.4 : u == 2 ? 2.54 : 1.0; }
static QString unitSuffix(int u) { return u == 1 ? QStringLiteral(" mm") : u == 2 ? QStringLiteral(" cm") : QStringLiteral(" in"); }

ResizeImageDialog::ResizeImageDialog(ScribusDoc* doc, PageItem* item, const QImage& source, QWidget* parent)
	: QDialog(parent),
	  m_doc(doc),
	  m_item(item),
	  m_source(source)
{
	Q_UNUSED(source);
	setWindowTitle(tr("Image Size"));
	setMinimumWidth(520);
	m_info = SuneerImageResample::probe(item);
	m_effDpiX = SuneerImageResample::effectiveDpi(item, &m_effDpiY);
	m_unit = SuneerImageResample::settingInt(QStringLiteral("sizeUnit"), 0);

	auto* layout = new QVBoxLayout(this);

	// ---- what the file is
	auto* infoGroup = new QGroupBox(tr("Picture file"), this);
	auto* ig = new QGridLayout(infoGroup);
	auto addRow = [&](int row, const QString& label, QLabel*& value) {
		ig->addWidget(new QLabel(label, infoGroup), row, 0);
		value = new QLabel(infoGroup);
		value->setTextInteractionFlags(Qt::TextSelectableByMouse);
		ig->addWidget(value, row, 1);
	};
	addRow(0, tr("Pixels:"), m_pixelsLabel);
	addRow(1, tr("Stored resolution:"), m_storedDpiLabel);
	addRow(2, tr("Size at that resolution:"), m_storedSizeLabel);
	addRow(3, tr("On the page:"), m_placedLabel);
	addRow(4, tr("Effective resolution:"), m_effDpiLabel);
	addRow(5, tr("File:"), m_fileLabel);
	auto* unitRow = new QHBoxLayout();
	unitRow->addWidget(new QLabel(tr("Units:"), infoGroup));
	m_unitCombo = new QComboBox(infoGroup);
	m_unitCombo->addItems({ tr("inches"), tr("mm"), tr("cm") });
	m_unitCombo->setCurrentIndex(qBound(0, m_unit, 2));
	unitRow->addWidget(m_unitCombo);
	unitRow->addStretch(1);
	ig->addLayout(unitRow, 6, 0, 1, 2);
	layout->addWidget(infoGroup);

	// ---- resample
	auto* sizeGroup = new QGroupBox(tr("Resample to"), this);
	auto* grid = new QGridLayout(sizeGroup);
	m_widthSpin = new QDoubleSpinBox(sizeGroup);
	m_widthSpin->setRange(0.01, 10000);
	m_widthSpin->setDecimals(3);
	m_heightSpin = new QDoubleSpinBox(sizeGroup);
	m_heightSpin->setRange(0.01, 10000);
	m_heightSpin->setDecimals(3);
	m_resSpin = new QSpinBox(sizeGroup);
	m_resSpin->setRange(10, 2400);
	m_resSpin->setSuffix(tr(" dpi"));
	m_lockAspect = new QCheckBox(tr("Constrain proportions"), sizeGroup);
	m_lockAspect->setChecked(true);
	m_allowEnlarge = new QCheckBox(tr("Allow enlarge"), sizeGroup);
	m_allowEnlarge->setToolTip(tr("Off: the picture is only ever made smaller"));
	grid->addWidget(new QLabel(tr("Width:"), sizeGroup), 0, 0);
	grid->addWidget(m_widthSpin, 0, 1);
	grid->addWidget(new QLabel(tr("Height:"), sizeGroup), 0, 2);
	grid->addWidget(m_heightSpin, 0, 3);
	grid->addWidget(new QLabel(tr("Resolution:"), sizeGroup), 1, 0);
	grid->addWidget(m_resSpin, 1, 1);
	grid->addWidget(m_lockAspect, 1, 2, 1, 2);
	grid->addWidget(m_allowEnlarge, 2, 2, 1, 2);
	m_resultLine = new QLabel(sizeGroup);
	m_resultLine->setWordWrap(true);
	grid->addWidget(m_resultLine, 2, 0, 1, 2);

	// The one-click path: exactly what this frame needs at N dpi.
	auto* fitRow = new QHBoxLayout();
	fitRow->addWidget(new QLabel(tr("Fit to frame @"), sizeGroup));
	m_dpiSpin = new QSpinBox(sizeGroup);
	m_dpiSpin->setRange(72, 1200);
	m_dpiSpin->setValue(SuneerImageResample::settingInt(QStringLiteral("fitDpi"), 300));
	m_dpiSpin->setSuffix(tr(" dpi"));
	fitRow->addWidget(m_dpiSpin);
	auto* fitBtn = new QPushButton(tr("Fit to frame"), sizeGroup);
	fitRow->addWidget(fitBtn);
	fitRow->addStretch(1);
	fitRow->addWidget(new QLabel(tr("JPEG quality:"), sizeGroup));
	m_qualitySpin = new QSpinBox(sizeGroup);
	m_qualitySpin->setRange(50, 100);
	m_qualitySpin->setValue(SuneerImageResample::settingInt(QStringLiteral("jpegQuality"), 92));
	fitRow->addWidget(m_qualitySpin);
	grid->addLayout(fitRow, 3, 0, 1, 4);
	layout->addWidget(sizeGroup);

	m_offerOnImport = new QCheckBox(tr("On image import, offer to downsample when the picture is above 400 dpi on the page"), this);
	m_offerOnImport->setChecked(QSettings(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools")).value(QStringLiteral("offerOnImport"), false).toBool());
	layout->addWidget(m_offerOnImport);

	auto* toolLine = new QLabel(SuneerImageResample::magickAvailable()
		? tr("Resampling with ImageMagick (keeps colour mode, ICC profile, EXIF). Output: %1").arg(outputFolderText())
		: tr("ImageMagick not found: RGB pictures only, via Qt. Output: %1").arg(outputFolderText()), this);
	toolLine->setWordWrap(true);
	toolLine->setStyleSheet("color: gray; font-size: 11px;");
	layout->addWidget(toolLine);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
	layout->addWidget(buttons);

	connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int u) {
		m_unit = u;
		SuneerImageResample::setSetting(QStringLiteral("sizeUnit"), u);
		refreshInfo();
		setFromPixels(m_targetW, m_targetH, m_targetDpi);
	});
	connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]{ syncLinked(true); });
	connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]{ syncLinked(false); });
	connect(m_resSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]{ syncLinked(true); });
	connect(m_dpiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { SuneerImageResample::setSetting(QStringLiteral("fitDpi"), v); });
	connect(m_qualitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { SuneerImageResample::setSetting(QStringLiteral("jpegQuality"), v); });
	connect(m_offerOnImport, &QCheckBox::toggled, this, [](bool on) { SuneerImageResample::setSetting(QStringLiteral("offerOnImport"), on); });
	connect(fitBtn, &QPushButton::clicked, this, [this]{ fitToFrameDpi(); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this]{ doResize(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	refreshInfo();
	fitToFrameDpi();   // the usual answer is already in the fields
}

QString ResizeImageDialog::outputFolderText() const
{
	if (m_doc && m_doc->hasName)
	{
		const QFileInfo df(m_doc->documentFileName());
		return df.completeBaseName() + QStringLiteral("_images/");
	}
	return tr("beside the picture (document not saved yet)");
}

void ResizeImageDialog::refreshInfo()
{
	const double upi = unitPerInch(m_unit);
	const QString su = unitSuffix(m_unit);
	m_pixelsLabel->setText(tr("%1 x %2 px").arg(m_info.width).arg(m_info.height));
	m_storedDpiLabel->setText(tr("%1 x %2 dpi (from the file)").arg(qRound(m_info.dpiX)).arg(qRound(m_info.dpiY)));
	m_storedSizeLabel->setText(QStringLiteral("%1 x %2%3").arg(m_info.width / m_info.dpiX * upi, 0, 'f', 2).arg(m_info.height / m_info.dpiY * upi, 0, 'f', 2).arg(su));
	// Placed size of the whole picture on the page (points -> inches).
	const double placedWin = m_item->OrigW * m_item->imageXScale() / 72.0;
	const double placedHin = m_item->OrigH * m_item->imageYScale() / 72.0;
	m_placedLabel->setText(tr("%1 x %2%3 (frame %4 x %5%3)").arg(placedWin * upi, 0, 'f', 2).arg(placedHin * upi, 0, 'f', 2).arg(su)
	                       .arg(m_item->width() / 72.0 * upi, 0, 'f', 2).arg(m_item->height() / 72.0 * upi, 0, 'f', 2));
	const double eff = qMin(m_effDpiX, m_effDpiY);
	m_effDpiLabel->setText(tr("<b>%1 dpi</b> - %2 (%3 x %4)").arg(qRound(eff)).arg(SuneerImageResample::dpiWord(eff)).arg(qRound(m_effDpiX)).arg(qRound(m_effDpiY)));
	m_effDpiLabel->setStyleSheet(QStringLiteral("color: %1;").arg(SuneerImageResample::dpiColor(eff).name()));
	m_fileLabel->setText(tr("%1, %2, %3, alpha: %4%5\n%6")
		.arg(QLocale().formattedDataSize(m_info.bytes, 1), m_info.format, m_info.colorMode, m_info.alpha ? tr("yes") : tr("no"),
		     m_info.hasProfile ? tr(", ICC: %1").arg(m_info.profileName.isEmpty() ? tr("embedded") : m_info.profileName) : QString(),
		     QFileInfo(m_item->Pfile).fileName()));
	m_fileLabel->setToolTip(m_item->Pfile);
}

// The three fields describe one picture: size (units) x resolution = pixels.
void ResizeImageDialog::setFromPixels(int w, int h, double dpi)
{
	m_syncing = true;
	m_targetW = qMax(1, w);
	m_targetH = qMax(1, h);
	m_targetDpi = qMax(10.0, dpi);
	const double upi = unitPerInch(m_unit);
	m_widthSpin->setSuffix(unitSuffix(m_unit));
	m_heightSpin->setSuffix(unitSuffix(m_unit));
	m_widthSpin->setValue(m_targetW / m_targetDpi * upi);
	m_heightSpin->setValue(m_targetH / m_targetDpi * upi);
	m_resSpin->setValue(qRound(m_targetDpi));
	m_syncing = false;
	updateResultLine();
}

void ResizeImageDialog::fitToFrameDpi()
{
	int w, h;
	SuneerImageResample::fitPixels(m_item, m_dpiSpin->value(), w, h);
	setFromPixels(w, h, m_dpiSpin->value());
}

void ResizeImageDialog::syncLinked(bool fromWidth)
{
	if (m_syncing)
		return;
	const double upi = unitPerInch(m_unit);
	const double dpi = m_resSpin->value();
	double win = m_widthSpin->value() / upi;
	double hin = m_heightSpin->value() / upi;
	if (m_lockAspect->isChecked() && m_info.width > 0 && m_info.height > 0)
	{
		const double aspect = double(m_info.height) / m_info.width;
		if (fromWidth)
			hin = win * aspect;
		else
			win = hin / aspect;
	}
	setFromPixels(qRound(win * dpi), qRound(hin * dpi), dpi);
}

void ResizeImageDialog::updateResultLine()
{
	const double pixRatio = double(m_targetW) * m_targetH / qMax(1.0, double(m_info.width) * m_info.height);
	const qint64 est = qint64(m_info.bytes * pixRatio);
	const bool enlarge = m_targetW > m_info.width || m_targetH > m_info.height;
	m_resultLine->setText(tr("Result: %1 x %2 px, about %3%4")
		.arg(m_targetW).arg(m_targetH).arg(QLocale().formattedDataSize(est, 1))
		.arg(enlarge ? tr(" - ENLARGES the picture") : QString()));
	m_resultLine->setStyleSheet(enlarge ? QStringLiteral("color: #c00;") : QString());
}

void ResizeImageDialog::doResize()
{
	SuneerImageResample::Options opt;
	opt.jpegQuality = m_qualitySpin->value();
	opt.allowEnlarge = m_allowEnlarge->isChecked();
	opt.parent = this;
	QString summary, outPath;
	const QString error = SuneerImageResample::resample(m_doc, m_item, m_targetW, m_targetH, opt, &summary, &outPath);
	if (!error.isEmpty())
	{
		if (error != tr("cancelled"))
			QMessageBox::warning(this, windowTitle(), tr("Could not resample the picture.\n\n%1").arg(error));
		return;
	}
	if (ScCore->primaryMainWindow())
		ScCore->primaryMainWindow()->statusBar()->showMessage(tr("%1 - saved as %2, original untouched").arg(summary, QFileInfo(outPath).fileName()), 10000);
	accept();
}

QString ResizeImageDialog::resampleImageFile(ScribusDoc* doc, PageItem* item, const QImage& source,
                                             int newW, int newH, double effDpiX, double effDpiY,
                                             QString* summary, QString* outPathOut)
{
	// Kept for the toolbar DPI field and the canvas Auto-DPI path: same pipeline.
	Q_UNUSED(source); Q_UNUSED(effDpiX); Q_UNUSED(effDpiY);
	SuneerImageResample::Options opt;
	opt.jpegQuality = SuneerImageResample::settingInt(QStringLiteral("jpegQuality"), 92);
	opt.parent = ScCore->primaryMainWindow();
	return SuneerImageResample::resample(doc, item, newW, newH, opt, summary, outPathOut);
}

// ── ImageDpiField ────────────────────────────────────────────────────────────

ImageDpiField::ImageDpiField(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	auto* label = new QLabel(tr("DPI:"), this);
	layout->addWidget(label);
	m_spin = new QSpinBox(this);
	m_spin->setRange(72, 1200);
	m_spin->setFixedWidth(64);
	m_spin->setKeyboardTracking(false);
	m_spin->setToolTip(tr("Effective image resolution at the current frame size.\nType a value and press Enter to resample the image file to it\n(writes name_resized beside the original and relinks; undoable)."));
	m_spin->installEventFilter(this);
	layout->addWidget(m_spin);

	// Auto-DPI: after a manual frame shrink on the canvas, resample the image
	// down to the target automatically (see maybeAutoResample). Off by default;
	// persisted in settings. The 25% threshold lives in maybeAutoResample.
	QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
	m_autoCheck = new QCheckBox(tr("Auto"), this);
	m_autoCheck->setToolTip(tr("Auto-resample on frame resize: after shrinking an image frame on the canvas,\nautomatically resample the image file down to the target DPI\n(only when the effective DPI exceeds the target by more than 25%)."));
	m_autoCheck->setChecked(cfg.value(QStringLiteral("autoDpiEnabled"), false).toBool());
	layout->addWidget(m_autoCheck);
	m_autoTarget = new QSpinBox(this);
	m_autoTarget->setRange(72, 1200);
	m_autoTarget->setFixedWidth(56);
	m_autoTarget->setKeyboardTracking(false);
	m_autoTarget->setValue(cfg.value(QStringLiteral("autoDpiTarget"), 300).toInt());
	m_autoTarget->setToolTip(tr("Auto-resample target DPI"));
	m_autoTarget->setEnabled(m_autoCheck->isChecked());
	layout->addWidget(m_autoTarget);
	// Enabling the toggle (or lowering the target while it is on) evaluates the
	// current selection right away, so the user does not have to touch the
	// frame again to see it take effect. Same helper as the drag-release path,
	// so the two can never diverge.
	connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool on) {
		m_autoTarget->setEnabled(on);
		QSettings s(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
		s.setValue(QStringLiteral("autoDpiEnabled"), on);
		s.sync();   // maybeAutoResample() re-reads the settings
		if (!on)
			return;
		ScribusDoc* doc = nullptr;
		currentImageItem(&doc);
		ResizeImageDialog::maybeAutoResample(doc, true);
		refresh();
	});
	connect(m_autoTarget, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		QSettings s(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
		s.setValue(QStringLiteral("autoDpiTarget"), v);
		s.sync();
		if (!m_autoCheck->isChecked())
			return;
		ScribusDoc* doc = nullptr;
		currentImageItem(&doc);
		ResizeImageDialog::maybeAutoResample(doc, true);
		refresh();
	});

	m_spin->setEnabled(false);   // enabled by refresh() once an image frame is selected

	// Self-contained refresh: poll the current document/selection so hosting
	// toolbars don't need any wiring beyond placing this widget.
	m_timer.setInterval(500);
	connect(&m_timer, &QTimer::timeout, this, &ImageDpiField::refresh);
	m_timer.start();
}

PageItem* ImageDpiField::currentImageItem(ScribusDoc** docOut) const
{
	if (docOut)
		*docOut = nullptr;
	if (!ScCore || !ScCore->primaryMainWindow() || !ScCore->primaryMainWindow()->HaveDoc)
		return nullptr;
	ScribusDoc* doc = ScCore->primaryMainWindow()->doc;
	if (!doc || doc->m_Selection->count() != 1)
		return nullptr;
	PageItem* item = doc->m_Selection->itemAt(0);
	// One group holding exactly one image (image + caption): that image.
	if (const PageItem* group = SuneerGroupEdit::soleGroup(doc))
	{
		const QList<PageItem*> images = SuneerGroupEdit::imageChildren(group);
		item = (images.count() == 1) ? images.first() : nullptr;
	}
	if (!item || !item->isImageFrame() || item->Pfile.isEmpty() || !item->imageIsAvailable)
		return nullptr;
	if (item->imageXScale() <= 0.0 || item->imageYScale() <= 0.0)
		return nullptr;
	if (docOut)
		*docOut = doc;
	return item;
}

void ImageDpiField::refresh()
{
	PageItem* item = currentImageItem();
	// Only the DPI readout is selection-bound. The Auto toggle and its target
	// are a global preference, so they stay clickable with any selection —
	// disabling them made ticking the box silently do nothing at all.
	m_spin->setEnabled(item != nullptr);
	if (!item || m_spin->hasFocus())
		return;   // don't clobber a value the user is typing
	const int dpi = qBound(m_spin->minimum(), qRound(72.0 / item->imageXScale()), m_spin->maximum());
	if (m_spin->value() != dpi)
	{
		QSignalBlocker blocker(m_spin);
		m_spin->setValue(dpi);
	}
}

bool ImageDpiField::eventFilter(QObject* obj, QEvent* ev)
{
	if (obj == m_spin && ev->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(ev);
		if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
		{
			m_spin->interpretText();
			applyDpi();
			return true;
		}
		if (ke->key() == Qt::Key_Escape)
		{
			m_spin->clearFocus();
			refresh();
			return true;
		}
	}
	return QWidget::eventFilter(obj, ev);
}

void ImageDpiField::applyDpi()
{
	ScribusDoc* doc = nullptr;
	PageItem* item = currentImageItem(&doc);
	if (!item)
		return;

	const double effDpiX = 72.0 / item->imageXScale();
	const double effDpiY = 72.0 / item->imageYScale();
	const int targetDpi = m_spin->value();
	if (targetDpi == qRound(effDpiX))
	{
		if (ScCore->primaryMainWindow())
			ScCore->primaryMainWindow()->statusBar()->showMessage(tr("Image is already at %1 dpi").arg(targetDpi), 5000);
		return;
	}

	// Resample from the real file, not the possibly preview-scaled pixm.
	QImage source(item->Pfile);
	if (source.isNull())
	{
		QMessageBox::warning(this, tr("Resize Image"),
			tr("Could not read the image file:\n\"%1\"").arg(item->Pfile));
		return;
	}

	const int newW = qMax(1, qRound(source.width() * targetDpi / effDpiX));
	const int newH = qMax(1, qRound(source.height() * targetDpi / effDpiY));

	QString summary;
	const QString error = ResizeImageDialog::resampleImageFile(doc, item, source, newW, newH, effDpiX, effDpiY, &summary);
	if (!error.isEmpty())
	{
		QMessageBox::warning(this, tr("Resize Image"),
			tr("Could not write the resized image.\n\n%1").arg(error));
		return;
	}

	if (ScCore->primaryMainWindow())
		ScCore->primaryMainWindow()->statusBar()->showMessage(summary, 8000);
	m_spin->clearFocus();
	refresh();
}

void ResizeImageDialog::maybeAutoResample(ScribusDoc* doc, bool reportWhenSkipped)
{
	ScribusMainWindow* mainWin = (ScCore ? ScCore->primaryMainWindow() : nullptr);
	// Only the toggle path reports skips; the gesture stays silent so ordinary
	// frame nudges do not spam the status bar.
	auto reportSkip = [mainWin, reportWhenSkipped](const QString& msg) {
		if (reportWhenSkipped && mainWin)
			mainWin->statusBar()->showMessage(msg, 5000);
	};

	QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
	if (!cfg.value(QStringLiteral("autoDpiEnabled"), false).toBool())
		return;
	const int target = qBound(72, cfg.value(QStringLiteral("autoDpiTarget"), 300).toInt(), 2400);

	// These are silent from the gesture (a drag on a text frame must not natter),
	// but the toggle path says why nothing happened — otherwise ticking the box
	// with the wrong thing selected looks like a broken feature.
	if (!doc || doc->m_Selection->count() != 1)
	{
		reportSkip(tr("Auto-DPI: select a single image frame for it to apply"));
		return;
	}
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || item->Pfile.isEmpty() || !item->imageIsAvailable)
	{
		reportSkip(tr("Auto-DPI: select an image frame with a loaded image"));
		return;
	}
	if (item->imageXScale() <= 0.0 || item->imageYScale() <= 0.0)
	{
		reportSkip(tr("Auto-DPI: image scale is not usable on this frame"));
		return;
	}

	// Resample to the target whenever the frame's picture is not at it
	// (5% margin against nudge-resizes). Always from the original file, so
	// enlarging the frame again gets pixels back - up to the original's
	// count unless enlarging is allowed.
	const double effDpiX = 72.0 / item->imageXScale();
	if (qAbs(effDpiX - target) <= target * 0.05)
	{
		reportSkip(tr("Auto-DPI: image is already at %1 dpi").arg(target));
		return;
	}
	int newW = 0, newH = 0;
	SuneerImageResample::fitPixels(item, target, newW, newH);
	const SuneerImageResample::Info orig = SuneerImageResample::probeFile(SuneerImageResample::originalPathOf(item));
	const bool allowEnlarge = SuneerImageResample::settingInt(QStringLiteral("allowEnlarge"), 0) != 0;
	if (orig.ok && !allowEnlarge && (newW > orig.width || newH > orig.height))
	{
		newW = orig.width;
		newH = orig.height;   // the most the original has
	}
	SuneerImageResample::Options opt;
	opt.jpegQuality = SuneerImageResample::settingInt(QStringLiteral("jpegQuality"), 92);
	opt.allowEnlarge = allowEnlarge;
	opt.parent = mainWin;
	QString summary;
	const QString error = SuneerImageResample::resample(doc, item, newW, newH, opt, &summary);
	if (!mainWin)
		return;
	if (!error.isEmpty())
		mainWin->statusBar()->showMessage(tr("Auto-resample failed: %1").arg(error), 8000);
	else
		mainWin->statusBar()->showMessage(tr("Auto-resampled: %1").arg(summary), 8000);
}
