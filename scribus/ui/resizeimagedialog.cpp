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

	// Resample from the real file, not item->pixm (which can be a reduced
	// preview depending on the document's preview settings).
	QImage source(item->Pfile);
	if (source.isNull())
	{
		QMessageBox::warning(parent, tr("Resize Image"),
			tr("Could not read the image file:\n\"%1\"").arg(item->Pfile));
		return;
	}

	ResizeImageDialog dlg(doc, item, source, parent);
	dlg.exec();
}

ResizeImageDialog::ResizeImageDialog(ScribusDoc* doc, PageItem* item, const QImage& source, QWidget* parent)
	: QDialog(parent),
	  m_doc(doc),
	  m_item(item),
	  m_source(source)
{
	setWindowTitle(tr("Resize Image"));

	// Effective print resolution: the image scale is points per original
	// pixel, so 72/scale is the DPI this image actually prints at.
	const double scaleX = item->imageXScale();
	const double scaleY = item->imageYScale();
	m_effDpiX = (scaleX > 0.0) ? 72.0 / scaleX : 72.0;
	m_effDpiY = (scaleY > 0.0) ? 72.0 / scaleY : 72.0;

	auto* layout = new QVBoxLayout(this);

	const qint64 fileSize = QFileInfo(item->Pfile).size();
	QString dpiText = (qRound(m_effDpiX) == qRound(m_effDpiY))
		? tr("%1 dpi").arg(qRound(m_effDpiX))
		: tr("%1 x %2 dpi").arg(qRound(m_effDpiX)).arg(qRound(m_effDpiY));
	auto* info = new QLabel(tr("Current: %1 x %2 px — %3 — effective %4 at the current frame size")
		.arg(m_source.width()).arg(m_source.height())
		.arg(humanSize(fileSize), dpiText), this);
	info->setWordWrap(true);
	layout->addWidget(info);

	auto* sizeGroup = new QGroupBox(tr("Target Size"), this);
	auto* grid = new QGridLayout(sizeGroup);

	m_widthSpin = new QSpinBox(sizeGroup);
	m_widthSpin->setRange(1, 100000);
	m_widthSpin->setSuffix(tr(" px"));
	m_widthSpin->setValue(m_source.width());
	m_heightSpin = new QSpinBox(sizeGroup);
	m_heightSpin->setRange(1, 100000);
	m_heightSpin->setSuffix(tr(" px"));
	m_heightSpin->setValue(m_source.height());
	m_lockAspect = new QCheckBox(tr("Lock aspect ratio"), sizeGroup);
	m_lockAspect->setChecked(true);

	grid->addWidget(new QLabel(tr("Width:"), sizeGroup), 0, 0);
	grid->addWidget(m_widthSpin, 0, 1);
	grid->addWidget(new QLabel(tr("Height:"), sizeGroup), 0, 2);
	grid->addWidget(m_heightSpin, 0, 3);
	grid->addWidget(m_lockAspect, 1, 1, 1, 3);

	// The one-click path: make the file exactly what this frame needs at N dpi.
	auto* fitRow = new QHBoxLayout();
	fitRow->addWidget(new QLabel(tr("Fit to frame @"), sizeGroup));
	m_dpiSpin = new QSpinBox(sizeGroup);
	m_dpiSpin->setRange(72, 1200);
	m_dpiSpin->setValue(240);
	m_dpiSpin->setSuffix(tr(" dpi"));
	fitRow->addWidget(m_dpiSpin);
	auto* preset240 = new QPushButton(tr("240"), sizeGroup);
	auto* preset300 = new QPushButton(tr("300"), sizeGroup);
	preset240->setFixedWidth(48);
	preset300->setFixedWidth(48);
	fitRow->addWidget(preset240);
	fitRow->addWidget(preset300);
	auto* fitBtn = new QPushButton(tr("Fit"), sizeGroup);
	fitRow->addWidget(fitBtn);
	fitRow->addStretch(1);
	grid->addLayout(fitRow, 2, 0, 1, 4);

	layout->addWidget(sizeGroup);

	m_resultLine = new QLabel(this);
	m_resultLine->setWordWrap(true);
	layout->addWidget(m_resultLine);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Resize"));
	layout->addWidget(buttons);

	connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]{ syncLinked(true); });
	connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]{ syncLinked(false); });
	// Editing the DPI directly re-fits the pixel fields live, same as the
	// presets and the Fit button.
	connect(m_dpiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]{ fitToFrameDpi(); });
	connect(preset240, &QPushButton::clicked, this, [this]{ m_dpiSpin->setValue(240); fitToFrameDpi(); });
	connect(preset300, &QPushButton::clicked, this, [this]{ m_dpiSpin->setValue(300); fitToFrameDpi(); });
	connect(fitBtn, &QPushButton::clicked, this, [this]{ fitToFrameDpi(); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this]{ doResize(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// Default to the one-click answer so plain OK already does the right thing.
	fitToFrameDpi();
}

void ResizeImageDialog::fitToFrameDpi()
{
	const double dpi = m_dpiSpin->value();
	m_syncing = true;
	m_widthSpin->setValue(qMax(1, qRound(m_source.width() * dpi / m_effDpiX)));
	m_heightSpin->setValue(qMax(1, qRound(m_source.height() * dpi / m_effDpiY)));
	m_syncing = false;
	updateResultLine();
}

void ResizeImageDialog::syncLinked(bool fromWidth)
{
	if (m_syncing)
		return;
	if (m_lockAspect->isChecked())
	{
		m_syncing = true;
		if (fromWidth)
			m_heightSpin->setValue(qMax(1, qRound(double(m_widthSpin->value()) * m_source.height() / m_source.width())));
		else
			m_widthSpin->setValue(qMax(1, qRound(double(m_heightSpin->value()) * m_source.width() / m_source.height())));
		m_syncing = false;
	}
	updateResultLine();
}

void ResizeImageDialog::updateResultLine()
{
	const int newW = m_widthSpin->value();
	const int newH = m_heightSpin->value();
	const int resultDpi = qRound(m_effDpiX * newW / m_source.width());
	m_resultLine->setText(tr("%1 x %2 (%3) → %4 x %5 @ %6 dpi effective")
		.arg(m_source.width()).arg(m_source.height())
		.arg(humanSize(QFileInfo(m_item->Pfile).size()))
		.arg(newW).arg(newH).arg(resultDpi));
}

QString ResizeImageDialog::resampleImageFile(ScribusDoc* doc, PageItem* item, const QImage& source,
                                             int newW, int newH, double effDpiX, double effDpiY,
                                             QString* summary, QString* outPathOut)
{
	if (!doc || !item || source.isNull() || newW < 1 || newH < 1)
		return tr("nothing to resample");

	QImage out = source.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	// Bake the new effective DPI into the file so a redo (or a fresh placement
	// of this file) loads at the print size the resample was made for.
	const double rx = double(source.width()) / newW;
	const double ry = double(source.height()) / newH;
	out.setDotsPerMeterX(qRound((effDpiX / rx) / 0.0254));
	out.setDotsPerMeterY(qRound((effDpiY / ry) / 0.0254));

	const QFileInfo fi(item->Pfile);
	QString suffix = fi.suffix().toLower();
	static const QStringList knownSuffixes { "jpg", "jpeg", "png", "tif", "tiff", "bmp" };
	if (!knownSuffixes.contains(suffix))
		suffix = QStringLiteral("jpg");

	// An embedded ("inline") image lives in a session temp file that the .sla
	// saver base64-embeds on save. Resizing such a frame must stay embedded:
	// write the resized image to a fresh temp file and re-set the inline flags
	// after the relink, so the next save embeds the small data instead of
	// either keeping the big original or silently linking into /tmp.
	const bool wasInline = item->isInlineImage && item->isTempFile;
	QString outPath;
	if (wasInline)
	{
		QTemporaryFile tempFile(QDir::tempPath() + "/scribus_temp_XXXXXX." + suffix);
		if (!tempFile.open())
			return tr("could not create a temporary file for the embedded image");
		outPath = getLongPathName(tempFile.fileName());
		tempFile.setAutoRemove(false);
		tempFile.close();
	}
	else
	{
		// name_resized.ext beside the source; never overwrite anything, and
		// don't restack a tag the chain already carries.
		outPath = derivedImagePath(item->Pfile, QStringLiteral("_resized"), suffix);
	}

	const bool isJpeg = (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"));
	const QString writeError = writeImageToFile(out, outPath, isJpeg ? 92 : -1);
	if (!writeError.isEmpty())
		return writeError;

	// Relink the frame; loadImage() records a GET_IMAGE undo state. The scale
	// and offset setters record their own states on top of it, so wrap it all
	// in one transaction — a single Ctrl+Z points the frame back at the
	// original file with its old scale/offset. The image scale is points per
	// pixel, offsets are pixels, so scaling the pixels by 1/r means scale * r
	// and offset / r keeps the frame visually identical.
	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(Um::Selection, Um::IImageFrame, Um::GetImage, QString(), Um::IGetImage);
	const double oldScaleX = item->imageXScale();
	const double oldScaleY = item->imageYScale();
	const double oldOffX = item->imageXOffset();
	const double oldOffY = item->imageYOffset();
	// loadPict() deletes the frame's old temp file before relinking. Keep it:
	// the GET_IMAGE undo state points at it, so undo must still find the
	// original data (a stray session temp file is the price of a working undo).
	if (wasInline)
		item->isTempFile = false;
	doc->loadPict(outPath, item, false, true);
	if (wasInline)
	{
		// Stay embedded, now backed by the resized temp file. Undo/redo keep
		// these flags, so both states save as embedded data of the right file.
		item->isInlineImage = true;
		item->isTempFile = true;
	}
	item->setImageXYScale(oldScaleX * rx, oldScaleY * ry);
	item->setImageXYOffset(oldOffX / rx, oldOffY / ry);
	// updateClip() ends in updateGradientVectors(), which records its own
	// "Change gradient position" state; committing only after it keeps that
	// inside this transaction instead of burying it on top.
	item->updateClip();
	item->update();
	if (transaction)
		transaction.commit();
	doc->changed();
	doc->regionsChanged()->update(QRectF());

	if (summary)
		*summary = tr("%1 x %2 (%3) → %4 x %5 (%6) @ %7 dpi")
			.arg(source.width()).arg(source.height())
			.arg(humanSize(fi.size()))
			.arg(newW).arg(newH)
			.arg(humanSize(QFileInfo(outPath).size()))
			.arg(qRound(effDpiX / rx));
	if (outPathOut)
		*outPathOut = outPath;
	return QString();
}

void ResizeImageDialog::doResize()
{
	const int newW = m_widthSpin->value();
	const int newH = m_heightSpin->value();
	if (newW == m_source.width() && newH == m_source.height())
	{
		QMessageBox::information(this, windowTitle(), tr("The image is already this size — nothing to do."));
		return;
	}

	QString summary, outPath;
	const QString error = resampleImageFile(m_doc, m_item, m_source, newW, newH, m_effDpiX, m_effDpiY, &summary, &outPath);
	if (!error.isEmpty())
	{
		QMessageBox::warning(this, windowTitle(),
			tr("Could not write the resized image.\n\n%1").arg(error));
		return;
	}

	QMessageBox::information(this, windowTitle(),
		tr("%1\n\nSaved as \"%2\" and relinked. The original file is untouched.").arg(summary, outPath));
	accept();
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
	const int target = qBound(72, cfg.value(QStringLiteral("autoDpiTarget"), 300).toInt(), 1200);

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

	// Downsample only, and only when clearly above target: the 25% margin
	// keeps small nudge-resizes from degrading the image repeatedly.
	const double effDpiX = 72.0 / item->imageXScale();
	const double effDpiY = 72.0 / item->imageYScale();
	if (effDpiX <= target * 1.25)
	{
		reportSkip(tr("Auto-DPI: image is already at or below %1 dpi (currently %2 dpi)")
			.arg(target).arg(qRound(effDpiX)));
		return;
	}

	QImage source(item->Pfile);
	if (source.isNull())
	{
		reportSkip(tr("Auto-DPI: could not read \"%1\"").arg(item->Pfile));
		return;
	}
	const int newW = qMax(1, qRound(source.width() * target / effDpiX));
	const int newH = qMax(1, qRound(source.height() * target / effDpiY));
	if (newW >= source.width() || newH >= source.height())
	{
		reportSkip(tr("Auto-DPI: nothing to downsample"));
		return;
	}

	QString summary;
	const QString error = resampleImageFile(doc, item, source, newW, newH, effDpiX, effDpiY, &summary);
	if (!mainWin)
		return;
	if (!error.isEmpty())
		mainWin->statusBar()->showMessage(tr("Auto-resample failed: %1").arg(error), 8000);
	else
		mainWin->statusBar()->showMessage(tr("Auto-resampled: %1").arg(summary), 8000);
}
