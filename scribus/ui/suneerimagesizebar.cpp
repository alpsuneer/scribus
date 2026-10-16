/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerimagesizebar.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QWidgetAction>
#include <QRegularExpression>
#include <QLocale>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>

#include "pageitem.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneergroupedit.h"
#include "suneerimageresample.h"
#include "undomanager.h"
#include "suneercursor.h"

using SuneerImageResample::Quality;

SuneerImageSizeBar::SuneerImageSizeBar(QWidget* parent) : QWidget(parent)
{
	auto* lay = new QHBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->setSpacing(2);

	m_unitCombo = new QComboBox(this);
	m_unitCombo->addItems({ QStringLiteral("cm"), QStringLiteral("mm"), QStringLiteral("in") });
	m_unitCombo->setToolTip(tr("Units of the width and height fields"));
	m_unitCombo->setFixedWidth(58);   // lives in the gear popup, room for "mm" + arrow
	m_unitCombo->setCurrentIndex(qBound(0, SuneerImageResample::settingInt(QStringLiteral("barUnit"), 0), 2));
	// (the unit chooser sits in the gear popup: the fields show their unit as a suffix)

	auto makeSpin = [this](const QString& tip) {
		auto* s = new QDoubleSpinBox(this);
		s->setRange(0.01, 999.99);   // "999.99 cm" sets the field width (see fitFieldWidths)
		s->setDecimals(2);
		s->setKeyboardTracking(false);
		s->setToolTip(tip);
		s->installEventFilter(this);
		return s;
	};
	// "W:" / "H:" labels in front of the fields; the dpi field says "dpi"
	// itself. The resample settings sit behind the gear button.
	m_wLbl = new QLabel(tr("W:"), this);
	lay->addWidget(m_wLbl);
	m_widthSpin = makeSpin(tr("Picture width on the page (Enter or the arrows apply)"));
	lay->addWidget(m_widthSpin);
	m_chainBtn = new QToolButton(this);
	m_chainBtn->setText(QStringLiteral("⚭"));
	m_chainBtn->setCheckable(true);
	m_chainBtn->setChecked(true);
	m_chainBtn->setFixedSize(22, 22);
	m_chainBtn->setToolTip(tr("Constrain proportions"));
	lay->addWidget(m_chainBtn);
	m_hLbl = new QLabel(tr("H:"), this);
	lay->addWidget(m_hLbl);
	m_heightSpin = makeSpin(tr("Picture height on the page (Enter or the arrows apply)"));
	lay->addWidget(m_heightSpin);

	m_dpiSpin = new QSpinBox(this);
	m_dpiSpin->setRange(10, 2400);
	m_dpiSpin->setSuffix(QStringLiteral(" dpi"));
	m_dpiSpin->setKeyboardTracking(false);
	m_dpiSpin->setToolTip(tr("Effective resolution on the page (Enter or the arrows apply)"));
	m_dpiSpin->installEventFilter(this);
	lay->addWidget(m_dpiSpin);

	m_resampleCheck = new QCheckBox(tr("Res."), this);
	m_resampleCheck->setChecked(SuneerImageResample::settingInt(QStringLiteral("barResample"), 1) != 0);
	m_resampleCheck->setToolTip(tr("On: a new size or dpi changes the picture's pixels (a new file is written from the original).\n"
	                               "Off: the pixels stay; a new dpi gives a new size on the page, a new size gives a new dpi."));
	lay->addWidget(m_resampleCheck);

	m_qualityCombo = new QComboBox(this);
	// Short names in the bar; the full ones are in the tooltip.
	m_qualityCombo->addItem(tr("Auto"), int(Quality::Auto));
	m_qualityCombo->addItem(tr("Sharper"), int(Quality::Sharper));
	m_qualityCombo->addItem(tr("Smoother"), int(Quality::Smoother));
	m_qualityCombo->addItem(tr("Bicubic"), int(Quality::Bicubic));
	m_qualityCombo->addItem(tr("Nearest"), int(Quality::Nearest));
	m_qualityCombo->setFixedWidth(100);
	m_qualityCombo->setToolTip(tr("Resample quality. Auto: Bicubic Sharper when reducing, Bicubic Smoother when enlarging"));
	m_qualityCombo->setCurrentIndex(qualityIndexFromSetting());
	m_sharpenSpin = new QSpinBox(this);
	m_sharpenSpin->setRange(0, 100);
	m_sharpenSpin->setSuffix(QStringLiteral("%"));
	m_sharpenSpin->setFixedWidth(46);
	m_sharpenSpin->setValue(SuneerImageResample::settingInt(QStringLiteral("sharpen"), 50));
	m_sharpenSpin->setToolTip(tr("Sharpen amount for Bicubic Sharper (luminance only, never the alpha channel)"));

	m_enlargeBtn = new QToolButton(this);
	m_enlargeBtn->setText(QStringLiteral("↑"));
	m_enlargeBtn->setCheckable(true);
	m_enlargeBtn->setChecked(SuneerImageResample::settingInt(QStringLiteral("allowEnlarge"), 0) != 0);
	m_enlargeBtn->setFixedSize(22, 22);
	m_enlargeBtn->setToolTip(tr("Allow enlarge: let a resample ask for more pixels than the original file has"));

	m_autoCheck = new QCheckBox(tr("Auto dpi"), this);
	m_autoCheck->setToolTip(tr("Auto dpi: after resizing the frame with the mouse, resample the picture to the dpi shown here\n(not the Auto fit box on the second row, which scales the picture with the frame)"));
	{
		QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
		m_autoCheck->setChecked(cfg.value(QStringLiteral("autoDpiEnabled"), false).toBool());
	}
	// Resample settings (quality, sharpen, allow enlarge, auto dpi) behind one
	// gear button: they are settings, not per-picture fields, and the toolbar
	// row has to fit a 1366 px screen.
	{
		auto* optBtn = new QToolButton(this);
		optBtn->setText(QStringLiteral("⚙"));
		optBtn->setFixedSize(26, 22);
		optBtn->setPopupMode(QToolButton::InstantPopup);
		optBtn->setToolTip(tr("Resample settings: quality, sharpen, allow enlarge, auto dpi after a mouse resize"));
		auto* menu = new QMenu(optBtn);
		auto* box = new QWidget(menu);
		auto* bl = new QHBoxLayout(box);
		bl->setContentsMargins(8, 6, 8, 6);
		bl->setSpacing(6);
		bl->addWidget(new QLabel(tr("Unit:"), box));
		bl->addWidget(m_unitCombo);
		bl->addWidget(new QLabel(tr("Quality:"), box));
		bl->addWidget(m_qualityCombo);
		bl->addWidget(new QLabel(tr("Sharpen:"), box));
		bl->addWidget(m_sharpenSpin);
		bl->addWidget(m_enlargeBtn);
		bl->addWidget(m_autoCheck);
		auto* wa = new QWidgetAction(menu);
		wa->setDefaultWidget(box);
		menu->addAction(wa);
		optBtn->setMenu(menu);
		lay->addWidget(optBtn);
	}

	m_infoLabel = new QLabel(this);
	m_infoLabel->setStyleSheet("color: #555; font-size: 11px;");
	lay->addWidget(m_infoLabel);
	// The live result line goes to the status bar (the toolbar row is full);
	// the label here only carries it as a tooltip.
	m_previewLabel = new QLabel(this);
	m_previewLabel->setVisible(false);

	connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int u) {
		SuneerImageResample::setSetting(QStringLiteral("barUnit"), u);
		refresh();
	});
	connect(m_resampleCheck, &QCheckBox::toggled, this, [this](bool on) {
		SuneerImageResample::setSetting(QStringLiteral("barResample"), on ? 1 : 0);
		m_qualityCombo->setVisible(on);
		m_sharpenSpin->setVisible(on);
		m_enlargeBtn->setVisible(on);
		updatePreview();
	});
	connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		SuneerImageResample::setSetting(QStringLiteral("resampleQuality"), m_qualityCombo->currentData().toInt());
		m_sharpenSpin->setEnabled(m_qualityCombo->currentData().toInt() != int(Quality::Nearest));
	});
	connect(m_sharpenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) { SuneerImageResample::setSetting(QStringLiteral("sharpen"), v); });
	connect(m_enlargeBtn, &QToolButton::toggled, this, [this](bool on) { SuneerImageResample::setSetting(QStringLiteral("allowEnlarge"), on ? 1 : 0); updatePreview(); });
	connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool on) {
		QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
		cfg.setValue(QStringLiteral("autoDpiEnabled"), on);
		cfg.setValue(QStringLiteral("autoDpiTarget"), m_dpiSpin->value());
		cfg.sync();
		status(on ? tr("Auto: after a mouse resize the picture is resampled to %1 dpi").arg(m_dpiSpin->value()) : tr("Auto resample off"));
	});
	// Enter / focus-out apply at once. The spin arrows and the wheel only
	// emit valueChanged (keyboard tracking is off, so typing does not), and
	// those are applied once, 300 ms after the last step.
	connect(m_widthSpin, &QDoubleSpinBox::editingFinished, this, [this]{ m_applyTimer.stop(); m_pending = 0; applySize(true); });
	connect(m_heightSpin, &QDoubleSpinBox::editingFinished, this, [this]{ m_applyTimer.stop(); m_pending = 0; applySize(false); });
	connect(m_dpiSpin, &QSpinBox::editingFinished, this, [this]{ m_applyTimer.stop(); m_pending = 0; applyDpi(); });
	connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { scheduleApply(1); });
	connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { scheduleApply(2); });
	connect(m_dpiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { scheduleApply(3); });
	m_applyTimer.setSingleShot(true);
	m_applyTimer.setInterval(300);
	connect(&m_applyTimer, &QTimer::timeout, this, &SuneerImageSizeBar::applyPending);
	// Live preview while typing (the spin boxes only commit on Enter).
	for (QAbstractSpinBox* sb : QList<QAbstractSpinBox*>{ m_widthSpin, m_heightSpin, m_dpiSpin })
		connect(sb->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, [this](const QString&) { updatePreview(); });
	m_qualityCombo->setVisible(m_resampleCheck->isChecked());
	m_sharpenSpin->setVisible(m_resampleCheck->isChecked());
	m_enlargeBtn->setVisible(m_resampleCheck->isChecked());
	m_sharpenSpin->setEnabled(m_qualityCombo->currentData().toInt() != int(Quality::Nearest));

	m_timer.setInterval(400);
	connect(&m_timer, &QTimer::timeout, this, [this]{ refresh(false); });
	m_timer.start();
	fitFieldWidths();
	refresh();
}

// Field widths from the font: the widest value the field can show
// ("999.99 cm", "2400 dpi") plus the spin arrows and the frame, so
// "120.50 cm" is never clipped on any font or screen.
void SuneerImageSizeBar::fitFieldWidths()
{
	for (QDoubleSpinBox* sb : { m_widthSpin, m_heightSpin })
	{
		sb->setFixedWidth(sb->sizeHint().width());
		sb->updateGeometry();
	}
	m_dpiSpin->setFixedWidth(m_dpiSpin->sizeHint().width());
	updateGeometry();
}

void SuneerImageSizeBar::scheduleApply(int what)
{
	if (m_busy)
		return;
	m_pending = what;
	m_applyTimer.start();
	updatePreview();
}

void SuneerImageSizeBar::applyPending()
{
	const int what = m_pending;
	m_pending = 0;
	if (what == 1)
		applySize(true);
	else if (what == 2)
		applySize(false);
	else if (what == 3)
		applyDpi();
}

int SuneerImageSizeBar::qualityIndexFromSetting() const
{
	const int q = SuneerImageResample::settingInt(QStringLiteral("resampleQuality"), int(Quality::Auto));
	const int idx = m_qualityCombo->findData(q);
	return idx < 0 ? 0 : idx;
}

bool SuneerImageSizeBar::eventFilter(QObject* obj, QEvent* ev)
{
	if (ev->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(ev);
		if (ke->key() == Qt::Key_Escape)
		{
			m_applyTimer.stop();
			m_pending = 0;
			if (auto* w = qobject_cast<QWidget*>(obj))
				w->clearFocus();
			refresh(true);
			return true;
		}
	}
	return QWidget::eventFilter(obj, ev);
}

PageItem* SuneerImageSizeBar::currentImageItem(ScribusDoc** docOut) const
{
	ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
	if (!mw || !mw->HaveDoc || !mw->doc)
		return nullptr;
	ScribusDoc* doc = mw->doc;
	if (docOut)
		*docOut = doc;
	if (doc->m_Selection->count() != 1)
	{
		if (const PageItem* group = SuneerGroupEdit::soleGroup(doc))
		{
			const QList<PageItem*> images = SuneerGroupEdit::imageChildren(group);
			if (images.count() == 1 && images.first()->imageIsAvailable)
				return images.first();
		}
		return nullptr;
	}
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || !item->imageIsAvailable || item->Pfile.isEmpty())
		return nullptr;
	return item;
}

double SuneerImageSizeBar::unitPerInch() const
{
	switch (m_unitCombo->currentIndex())
	{
		case 1: return 25.4;
		case 2: return 1.0;
		default: return 2.54;
	}
}

void SuneerImageSizeBar::refresh(bool force)
{
	if (m_busy)
		return;
	if (!force && m_applyTimer.isActive())
		return;   // an arrow step is waiting to be applied
	PageItem* item = currentImageItem();
	const bool on = item != nullptr;
	for (QWidget* w : QList<QWidget*>{ m_wLbl, m_hLbl, m_widthSpin, m_heightSpin, m_chainBtn, m_dpiSpin, m_unitCombo, m_resampleCheck, m_qualityCombo, m_sharpenSpin })
		w->setEnabled(on);
	if (!on)
	{
		m_infoLabel->setText(QString());
		m_previewLabel->setText(QString());
		m_shownDpi = 0;
		return;
	}
	if (!force && (m_widthSpin->hasFocus() || m_heightSpin->hasFocus() || m_dpiSpin->hasFocus()))
		return;   // never clobber what is being typed
	const double upi = unitPerInch();
	const QString su = QStringLiteral(" ") + m_unitCombo->currentText();
	if (m_widthSpin->suffix() != su)
	{
		m_widthSpin->setSuffix(su);
		m_heightSpin->setSuffix(su);
		fitFieldWidths();
	}
	const double wIn = item->OrigW * item->imageXScale() / 72.0;
	const double hIn = item->OrigH * item->imageYScale() / 72.0;
	double effY = 0;
	const double effX = SuneerImageResample::effectiveDpi(item, &effY);
	const int dpi = qRound(qMin(effX, effY));
	QSignalBlocker b1(m_widthSpin), b2(m_heightSpin), b3(m_dpiSpin);
	m_widthSpin->setSuffix(su);
	m_heightSpin->setSuffix(su);
	m_widthSpin->setValue(wIn * upi);
	m_heightSpin->setValue(hIn * upi);
	m_dpiSpin->setValue(qBound(m_dpiSpin->minimum(), dpi, m_dpiSpin->maximum()));
	m_shownW = wIn; m_shownH = hIn; m_shownDpi = dpi;
	m_curW = item->OrigW; m_curH = item->OrigH;
	m_dpiSpin->setStyleSheet(QStringLiteral("color: %1;").arg(SuneerImageResample::dpiColor(dpi).name()));
	const SuneerImageResample::Info orig = SuneerImageResample::probeFile(SuneerImageResample::originalPathOf(item));
	m_origW = orig.ok ? orig.width : item->OrigW;
	m_origH = orig.ok ? orig.height : item->OrigH;
	m_origBytes = orig.ok ? orig.bytes : QFileInfo(item->Pfile).size();
	const qint64 bytes = QFileInfo(item->Pfile).size();
	m_infoLabel->setText(tr("%1×%2px %3").arg(item->OrigW).arg(item->OrigH).arg(QLocale().formattedDataSize(bytes, 1).remove(' ')));
	m_infoLabel->setToolTip(tr("%1\nOriginal: %2 x %3 px").arg(item->Pfile).arg(m_origW).arg(m_origH));
	m_previewLabel->setText(QString());
	m_previewLabel->setToolTip(QString());
}

// What Enter would do, from the text in the fields right now.
void SuneerImageSizeBar::updatePreview()
{
	if (m_shownDpi <= 0 || m_curW <= 0)
		return;
	auto valueOf = [](QAbstractSpinBox* sb, double fallback) {
		bool ok = false;
		QString t = sb->findChild<QLineEdit*>()->text();
		t.remove(QRegularExpression(QStringLiteral("[^0-9.,]"))).replace(',', '.');
		const double v = t.toDouble(&ok);
		return ok && v > 0 ? v : fallback;
	};
	const double upi = unitPerInch();
	double wIn = valueOf(m_widthSpin, m_shownW * upi) / upi;
	double hIn = valueOf(m_heightSpin, m_shownH * upi) / upi;
	int dpi = qRound(valueOf(m_dpiSpin, m_shownDpi));
	const bool wEdited = m_widthSpin->hasFocus(), hEdited = m_heightSpin->hasFocus(), dEdited = m_dpiSpin->hasFocus();
	const double aspect = double(m_origH) / qMax(1, m_origW);
	if (m_chainBtn->isChecked())
	{
		if (wEdited) hIn = wIn * aspect;
		else if (hEdited) wIn = hIn / aspect;
	}
	QString text, color = QStringLiteral("#555");
	if (!m_resampleCheck->isChecked())
	{
		// pixels fixed
		if (dEdited)
		{
			wIn = m_curW / double(dpi);
			hIn = m_curH / double(dpi);
		}
		else
			dpi = qMax(1, qRound(m_curW / qMax(0.01, wIn)));
		text = tr("→ %1×%2px, %3 dpi, %4×%5%6, no new file").arg(m_curW).arg(m_curH).arg(dpi)
		       .arg(wIn * upi, 0, 'f', 2).arg(hIn * upi, 0, 'f', 2).arg(m_unitCombo->currentText());
	}
	else
	{
		const int newW = qMax(1, qRound(wIn * dpi)), newH = qMax(1, qRound(hIn * dpi));
		const double ratio = double(newW) * newH / (double(m_origW) * qMax(1, m_origH));
		const qint64 est = qint64(m_origBytes * ratio);
		text = tr("→ %1×%2px, %3 dpi, ~%4").arg(newW).arg(newH).arg(dpi).arg(QLocale().formattedDataSize(est, 1).remove(' '));
		if (newW > m_origW || newH > m_origH)
		{
			color = QStringLiteral("#e07800");
			text += m_enlargeBtn->isChecked() ? tr(" (enlarges beyond the original %1×%2)").arg(m_origW).arg(m_origH)
			                                  : tr(" (more than the original %1×%2 - not allowed, see ↑)").arg(m_origW).arg(m_origH);
		}
	}
	m_previewLabel->setText(text);
	if (ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr)
		mw->statusBar()->showMessage((color == QStringLiteral("#e07800") ? QStringLiteral("⚠ ") : QString()) + text, 15000);
	for (QWidget* w : QList<QWidget*>{ m_widthSpin, m_heightSpin, m_dpiSpin })
		w->setToolTip(text);
}

void SuneerImageSizeBar::status(const QString& text, bool warn)
{
	ScribusMainWindow* mw = ScCore ? ScCore->primaryMainWindow() : nullptr;
	if (!mw)
		return;
	const QString line = (warn ? QStringLiteral("\u26A0 ") : QString()) + text;
	mw->statusBar()->showMessage(line, warn ? 12000 : 8000);
	// The frame resize that follows posts its own "Image Frame selected"
	// line on top; say it again once that has gone by.
	QTimer::singleShot(900, mw, [mw, line, warn]() { mw->statusBar()->showMessage(line, warn ? 12000 : 8000); });
}

void SuneerImageSizeBar::applySize(bool fromWidth)
{
	ScribusDoc* doc = nullptr;
	PageItem* item = currentImageItem(&doc);
	if (!item || m_busy)
		return;
	const double upi = unitPerInch();
	double wIn = m_widthSpin->value() / upi;
	double hIn = m_heightSpin->value() / upi;
	if (qAbs(wIn - m_shownW) < 1e-4 && qAbs(hIn - m_shownH) < 1e-4)
		return;   // Enter without a change
	if (m_chainBtn->isChecked() && m_origW > 0 && m_origH > 0)
	{
		const double aspect = double(m_origH) / m_origW;
		if (fromWidth) hIn = wIn * aspect; else wIn = hIn / aspect;
	}
	if (m_resampleCheck->isChecked())
		runResample(item, doc, wIn, hIn, m_dpiSpin->value(), true);
	else
	{
		// pixels fixed: the size decides the dpi
		const int dpi = qMax(1, qRound(item->OrigW / qMax(0.01, wIn)));
		runNoResample(item, doc, wIn, item->OrigH / double(dpi), dpi);
	}
}

void SuneerImageSizeBar::applyDpi()
{
	ScribusDoc* doc = nullptr;
	PageItem* item = currentImageItem(&doc);
	if (!item || m_busy)
		return;
	const int dpi = m_dpiSpin->value();
	{
		QSettings cfg(QStringLiteral("Scribus"), QStringLiteral("SuneerImageTools"));
		cfg.setValue(QStringLiteral("autoDpiTarget"), dpi);
	}
	if (dpi == m_shownDpi)
		return;
	if (m_resampleCheck->isChecked())
		runResample(item, doc, m_shownW, m_shownH, dpi, false);
	else
		runNoResample(item, doc, item->OrigW / double(dpi), item->OrigH / double(dpi), dpi);
}

// Resample OFF: same pixels, same file; scale = 72/dpi and the frame takes
// the picture's size with its top-left where it was. One undo step.
void SuneerImageSizeBar::runNoResample(PageItem* item, ScribusDoc* doc, double widthIn, double heightIn, int dpi)
{
	m_busy = true;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), tr("Image Size"), QString(), Um::IResize);
	item->setImageXYScale(72.0 / dpi, 72.0 / dpi);
	item->setImageXYOffset(0, 0);
	doc->sizeItem(widthIn * 72.0, heightIn * 72.0, item, false, true, true);
	item->updateClip();
	item->update();
	if (tx)
		tx.commit();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	suneerCursorSettle(doc->view(), "image size (no resample) end");
	const double upi = unitPerInch();
	status(tr("%1 x %2 px kept; now %3 x %4 %5 at %6 dpi (no new file)").arg(item->OrigW).arg(item->OrigH)
	       .arg(widthIn * upi, 0, 'f', 2).arg(heightIn * upi, 0, 'f', 2).arg(m_unitCombo->currentText()).arg(dpi));
	m_busy = false;
	refresh(true);
}

void SuneerImageSizeBar::runResample(PageItem* item, ScribusDoc* doc, double widthIn, double heightIn, int dpi, bool setFrame)
{
	const int newW = qMax(1, qRound(widthIn * dpi));
	const int newH = qMax(1, qRound(heightIn * dpi));
	const QString originalPath = SuneerImageResample::originalPathOf(item);
	const SuneerImageResample::Info orig = SuneerImageResample::probeFile(originalPath);
	if (!orig.ok)
	{
		status(tr("The original picture file could not be read: %1").arg(originalPath), true);
		refresh(true);   // the field shows the picture's real size again
		return;
	}
	if (!m_enlargeBtn->isChecked() && (newW > orig.width || newH > orig.height))
	{
		status(tr("Not done: %1 x %2 px would be more than the original %3 x %4 px. Lower the dpi, or allow enlarging (↑ button).")
		       .arg(newW).arg(newH).arg(orig.width).arg(orig.height), true);
		refresh(true);   // the field shows the picture's real size again
		return;
	}
	m_busy = true;
	SuneerCursorGuard busy(Qt::BusyCursor);
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(item->getUName(), item->getUPixmap(), tr("Image Size"), QString(), Um::IGetImage);
	SuneerImageResample::Options opt;
	opt.jpegQuality = SuneerImageResample::settingInt(QStringLiteral("jpegQuality"), 92);
	opt.allowEnlarge = true;   // checked above against the original
	opt.targetDpi = dpi;
	opt.quality = Quality(m_qualityCombo->currentData().toInt());
	opt.sharpen = m_sharpenSpin->value();
	opt.parent = ScCore->primaryMainWindow();
	QString summary;
	const QString err = SuneerImageResample::resample(doc, item, newW, newH, opt, &summary);
	if (err.isEmpty())
	{
		item->setImageXYScale(72.0 / dpi, 72.0 / dpi);
		if (setFrame)
		{
			doc->sizeItem(widthIn * 72.0, heightIn * 72.0, item, false, true, true);
			item->setImageXYOffset(0, 0);
		}
		item->updateClip();
		item->update();
		if (tx)
			tx.commit();
		doc->changed();
		doc->regionsChanged()->update(QRectF());
		status(tr("%1 (%2)").arg(summary, SuneerImageResample::qualityName(SuneerImageResample::resolveQuality(opt.quality, newW < orig.width))));
	}
	else
	{
		if (tx)
			tx.cancel();
		if (err != QObject::tr("cancelled"))
			status(tr("Not done: %1").arg(err), true);
	}
	busy.release();
	m_busy = false;
	refresh(true);
	suneerCursorSettle(doc->view(), "resample end");
}
