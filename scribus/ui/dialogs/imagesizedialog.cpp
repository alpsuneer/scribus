/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "imagesizedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace
{
	// Width/Height unit combo indices.
	enum Unit { UPixels = 0, UInches, UCm, UMm, UPoints, UPicas, UPercent };

	bool isPhysical(int unit)
	{
		return unit == UInches || unit == UCm || unit == UMm || unit == UPoints || unit == UPicas;
	}

	// How many of \a unit make up one inch (physical units only).
	double unitsPerInch(int unit)
	{
		switch (unit)
		{
			case UInches: return 1.0;
			case UCm:     return 2.54;
			case UMm:     return 25.4;
			case UPoints: return 72.0;
			case UPicas:  return 6.0;
			default:      return 1.0;
		}
	}

	// Convert a pixel count into the value shown for \a unit.
	double pxToDisplay(double px, double origPx, double res, int unit)
	{
		if (unit == UPixels)
			return px;
		if (unit == UPercent)
			return origPx > 0 ? px / origPx * 100.0 : 100.0;
		const double inches = res > 0 ? px / res : 0.0;
		return inches * unitsPerInch(unit);
	}

	// Convert a displayed value back into a pixel count.
	double displayToPx(double value, double origPx, double res, int unit)
	{
		if (unit == UPixels)
			return value;
		if (unit == UPercent)
			return value / 100.0 * origPx;
		const double inches = value / unitsPerInch(unit);
		return inches * res;
	}

	// A light 3×3 convolution used to emulate the "Bicubic Smoother/Sharper"
	// character on top of Qt's smooth scaler: a mild blur softens enlargements,
	// a mild unsharp crisps reductions. Alpha is preserved unchanged.
	QImage convolve3x3(const QImage& src, const float k[9])
	{
		QImage in = src.convertToFormat(QImage::Format_ARGB32);
		QImage out = in;
		const int w = in.width();
		const int h = in.height();
		for (int y = 0; y < h; ++y)
		{
			QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
			for (int x = 0; x < w; ++x)
			{
				float r = 0, g = 0, b = 0;
				int idx = 0;
				for (int dy = -1; dy <= 1; ++dy)
				{
					const int sy = qBound(0, y + dy, h - 1);
					const QRgb* srow = reinterpret_cast<const QRgb*>(in.scanLine(sy));
					for (int dx = -1; dx <= 1; ++dx, ++idx)
					{
						const int sx = qBound(0, x + dx, w - 1);
						const QRgb p = srow[sx];
						r += qRed(p)   * k[idx];
						g += qGreen(p) * k[idx];
						b += qBlue(p)  * k[idx];
					}
				}
				dst[x] = qRgba(qBound(0, int(r + 0.5f), 255),
				               qBound(0, int(g + 0.5f), 255),
				               qBound(0, int(b + 0.5f), 255),
				               qAlpha(dst[x]));
			}
		}
		return out;
	}

	QImage lightBlur(const QImage& src)
	{
		static const float k[9] = { 1/16.f, 2/16.f, 1/16.f,
		                            2/16.f, 4/16.f, 2/16.f,
		                            1/16.f, 2/16.f, 1/16.f };   // Gaussian
		return convolve3x3(src, k);
	}

	QImage lightSharpen(const QImage& src)
	{
		static const float k[9] = {  0.f,   -0.4f,  0.f,
		                            -0.4f,   2.6f, -0.4f,
		                             0.f,   -0.4f,  0.f };   // mild unsharp (sums to 1)
		return convolve3x3(src, k);
	}

	// A small painted "chain link" icon so the constrain-proportions toggle always
	// renders (no dependency on an emoji font being installed).
	QIcon makeLinkIcon()
	{
		QPixmap pm(18, 18);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing, true);
		QPen pen(QColor(70, 70, 70), 1.6);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(3, 5.5, 8, 7), 3, 3);
		p.drawRoundedRect(QRectF(7, 5.5, 8, 7), 3, 3);
		p.end();
		return QIcon(pm);
	}
}

ImageSizeDialog::ImageSizeDialog(const QImage& source, QWidget* parent)
	: QDialog(parent), m_source(source), m_result(source)
{
	setWindowTitle(tr("Image Size"));
	setModal(true);

	m_origPxW = m_pxW = source.width();
	m_origPxH = m_pxH = source.height();
	// Seed resolution from the image's DPI metadata if present (else 300).
	if (source.dotsPerMeterX() > 0)
		m_res = source.dotsPerMeterX() * 0.0254;
	if (m_res < 1.0)
		m_res = 300.0;

	auto* main = new QVBoxLayout(this);

	// ── Fit To presets ──────────────────────────────────────────────────────
	auto* fitRow = new QHBoxLayout;
	fitRow->addWidget(new QLabel(tr("Fit To:"), this));
	m_fitToCombo = new QComboBox(this);
	m_fitToCombo->addItem(tr("Original Size"),   QSize(source.width(), source.height()));
	m_fitToCombo->addItem(tr("1280 × 720 (720p)"),   QSize(1280, 720));
	m_fitToCombo->addItem(tr("1920 × 1080 (1080p)"), QSize(1920, 1080));
	m_fitToCombo->addItem(tr("2560 × 1440 (1440p)"), QSize(2560, 1440));
	m_fitToCombo->addItem(tr("3840 × 2160 (4K UHD)"), QSize(3840, 2160));
	m_fitToCombo->addItem(tr("A4 @ 300 dpi (2480 × 3508)"), QSize(2480, 3508));
	m_fitToCombo->addItem(tr("A4 @ 150 dpi (1240 × 1754)"), QSize(1240, 1754));
	m_fitToCombo->addItem(tr("800 × 600"),           QSize(800, 600));
	m_fitToCombo->addItem(tr("Custom"),              QSize());
	fitRow->addWidget(m_fitToCombo, 1);
	auto* savePreset = new QPushButton(tr("Save Preset"), this);
	savePreset->setEnabled(false);   // stub
	fitRow->addWidget(savePreset);
	main->addLayout(fitRow);

	// ── Dimensions summary + file size ──────────────────────────────────────
	auto* summaryRow = new QHBoxLayout;
	m_dimensionsLabel = new QLabel(this);
	m_fileSizeLabel = new QLabel(this);
	m_fileSizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	summaryRow->addWidget(m_dimensionsLabel, 1);
	summaryRow->addWidget(m_fileSizeLabel);
	main->addLayout(summaryRow);

	// ── Preview + Image Size group side by side ─────────────────────────────
	auto* midRow = new QHBoxLayout;

	m_previewLabel = new QLabel(this);
	m_previewLabel->setFixedSize(120, 120);
	m_previewLabel->setFrameShape(QFrame::Box);
	m_previewLabel->setAlignment(Qt::AlignCenter);
	midRow->addWidget(m_previewLabel);

	auto* sizeGroup = new QGroupBox(tr("Image Size"), this);
	auto* grid = new QGridLayout(sizeGroup);

	auto makeDimSpin = [this]() {
		auto* s = new QDoubleSpinBox(this);
		s->setDecimals(2);
		s->setRange(0.01, 1.0e7);
		s->setKeyboardTracking(false);
		return s;
	};
	auto makeUnitCombo = [this]() {
		auto* c = new QComboBox(this);
		c->addItems({ tr("pixels"), tr("inches"), tr("cm"), tr("mm"), tr("points"), tr("picas"), tr("percent") });
		return c;
	};

	grid->addWidget(new QLabel(tr("Width:"), this), 0, 0);
	m_widthSpin = makeDimSpin();
	grid->addWidget(m_widthSpin, 0, 1);
	m_widthUnit = makeUnitCombo();
	grid->addWidget(m_widthUnit, 0, 2);

	grid->addWidget(new QLabel(tr("Height:"), this), 1, 0);
	m_heightSpin = makeDimSpin();
	grid->addWidget(m_heightSpin, 1, 1);
	m_heightUnit = makeUnitCombo();
	grid->addWidget(m_heightUnit, 1, 2);

	// Link (constrain proportions) toggle spanning the two rows.
	m_linkBtn = new QToolButton(this);
	m_linkBtn->setCheckable(true);
	m_linkBtn->setChecked(true);
	m_linkBtn->setIcon(makeLinkIcon());
	m_linkBtn->setToolTip(tr("Constrain proportions"));
	grid->addWidget(m_linkBtn, 0, 3, 2, 1);

	grid->addWidget(new QLabel(tr("Resolution:"), this), 2, 0);
	m_resSpin = new QDoubleSpinBox(this);
	m_resSpin->setDecimals(2);
	m_resSpin->setRange(1.0, 1.0e5);
	m_resSpin->setKeyboardTracking(false);
	grid->addWidget(m_resSpin, 2, 1);
	m_resUnit = new QComboBox(this);
	m_resUnit->addItems({ tr("pixels/inch"), tr("pixels/cm") });
	grid->addWidget(m_resUnit, 2, 2);

	midRow->addWidget(sizeGroup, 1);
	main->addLayout(midRow);

	// ── Resample ────────────────────────────────────────────────────────────
	auto* resampleRow = new QHBoxLayout;
	m_resampleCheck = new QCheckBox(tr("Resample:"), this);
	m_resampleCheck->setChecked(true);
	resampleRow->addWidget(m_resampleCheck);
	m_resampleMethod = new QComboBox(this);
	m_resampleMethod->addItems({
		tr("Automatic"),
		tr("Bicubic Smoother (enlarge)"),
		tr("Bicubic Sharper (reduce)"),
		tr("Bicubic"),
		tr("Bilinear"),
		tr("Nearest Neighbor")
	});
	resampleRow->addWidget(m_resampleMethod, 1);
	main->addLayout(resampleRow);

	// ── Buttons ─────────────────────────────────────────────────────────────
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &ImageSizeDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &ImageSizeDialog::reject);
	main->addWidget(buttons);

	// Wiring
	connect(m_fitToCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageSizeDialog::onFitToChanged);
	connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImageSizeDialog::onWidthChanged);
	connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImageSizeDialog::onHeightChanged);
	connect(m_resSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ImageSizeDialog::onResolutionChanged);
	connect(m_widthUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageSizeDialog::onUnitChanged);
	connect(m_heightUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageSizeDialog::onUnitChanged);
	connect(m_resUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageSizeDialog::onUnitChanged);
	connect(m_resampleCheck, &QCheckBox::toggled, this, &ImageSizeDialog::onResampleToggled);
	connect(m_linkBtn, &QToolButton::toggled, this, &ImageSizeDialog::onLinkToggled);
	connect(m_resampleMethod, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImageSizeDialog::onResampleMethodChanged);

	syncFields();
	updatePreview();
}

Qt::TransformationMode ImageSizeDialog::transformationMode() const
{
	// Nearest Neighbor is the only hard/fast mode; every other method is smooth.
	if (m_resampleMethod->currentText().contains(tr("Nearest")))
		return Qt::FastTransformation;
	return Qt::SmoothTransformation;
}

void ImageSizeDialog::syncFields()
{
	QSignalBlocker b1(m_widthSpin), b2(m_heightSpin), b3(m_resSpin);
	const int wu = m_widthUnit->currentIndex();
	const int hu = m_heightUnit->currentIndex();
	m_widthSpin->setValue(pxToDisplay(m_pxW, m_origPxW, m_res, wu));
	m_heightSpin->setValue(pxToDisplay(m_pxH, m_origPxH, m_res, hu));
	const double resDisplay = (m_resUnit->currentIndex() == 1) ? m_res / 2.54 : m_res;   // px/cm
	m_resSpin->setValue(resDisplay);
	updateSummary();
}

void ImageSizeDialog::updateSummary()
{
	const int w = qMax(1, qRound(m_pxW));
	const int h = qMax(1, qRound(m_pxH));
	m_dimensionsLabel->setText(tr("Dimensions: %1 × %2 pixels").arg(w).arg(h));

	const int channels = m_source.hasAlphaChannel() ? 4 : 3;
	const double bytes = static_cast<double>(w) * h * channels;
	QString sizeText;
	if (bytes >= 1024.0 * 1024.0)
		sizeText = tr("~%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
	else
		sizeText = tr("~%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
	m_fileSizeLabel->setText(sizeText);
}

void ImageSizeDialog::updatePreview()
{
	if (m_source.isNull())
		return;
	const QImage scaled = m_source.scaled(m_previewLabel->size(), Qt::KeepAspectRatio, transformationMode());
	m_previewLabel->setPixmap(QPixmap::fromImage(scaled));
}

void ImageSizeDialog::onFitToChanged(int index)
{
	if (m_guard)
		return;
	const QSize sz = m_fitToCombo->itemData(index).toSize();
	if (!sz.isValid() || sz.isEmpty())
		return;   // "Custom" — leave the current values
	m_guard = true;
	if (!m_resampleCheck->isChecked())
		m_resampleCheck->setChecked(true);   // presets change pixel dimensions
	m_pxW = sz.width();
	m_pxH = sz.height();
	m_guard = false;
	syncFields();
	updatePreview();
}

void ImageSizeDialog::onWidthChanged()
{
	if (m_guard)
		return;
	m_guard = true;
	const int unit = m_widthUnit->currentIndex();
	const double v = m_widthSpin->value();
	if (!m_resampleCheck->isChecked())
	{
		// Pixels are locked; only a physical edit is meaningful (it changes DPI).
		if (isPhysical(unit))
		{
			const double inches = v / unitsPerInch(unit);
			if (inches > 0.0)
				m_res = m_pxW / inches;
		}
	}
	else
	{
		const double newPx = displayToPx(v, m_origPxW, m_res, unit);
		if (newPx > 0.0)
		{
			if (m_link && m_pxW > 0.0)
				m_pxH *= newPx / m_pxW;
			m_pxW = newPx;
		}
	}
	m_guard = false;
	m_fitToCombo->setCurrentIndex(m_fitToCombo->count() - 1);   // Custom
	syncFields();
	updatePreview();
}

void ImageSizeDialog::onHeightChanged()
{
	if (m_guard)
		return;
	m_guard = true;
	const int unit = m_heightUnit->currentIndex();
	const double v = m_heightSpin->value();
	if (!m_resampleCheck->isChecked())
	{
		if (isPhysical(unit))
		{
			const double inches = v / unitsPerInch(unit);
			if (inches > 0.0)
				m_res = m_pxH / inches;
		}
	}
	else
	{
		const double newPx = displayToPx(v, m_origPxH, m_res, unit);
		if (newPx > 0.0)
		{
			if (m_link && m_pxH > 0.0)
				m_pxW *= newPx / m_pxH;
			m_pxH = newPx;
		}
	}
	m_guard = false;
	m_fitToCombo->setCurrentIndex(m_fitToCombo->count() - 1);   // Custom
	syncFields();
	updatePreview();
}

void ImageSizeDialog::onResolutionChanged()
{
	if (m_guard)
		return;
	m_guard = true;
	double r = m_resSpin->value();
	if (m_resUnit->currentIndex() == 1)   // px/cm → px/inch
		r *= 2.54;
	if (r > 0.0)
	{
		if (m_resampleCheck->isChecked())
		{
			// Keep the physical (document) size; pixel dimensions follow the DPI.
			const double inchesW = m_pxW / m_res;
			const double inchesH = m_pxH / m_res;
			m_res = r;
			m_pxW = inchesW * m_res;
			m_pxH = inchesH * m_res;
		}
		else
		{
			// Pixels fixed; only the physical size (px / DPI) changes.
			m_res = r;
		}
	}
	m_guard = false;
	syncFields();
	updatePreview();
}

void ImageSizeDialog::onUnitChanged()
{
	// Keep the W/H unit combos in lock-step, then re-display in the new unit.
	if (!m_guard && sender() == m_widthUnit && m_heightUnit->currentIndex() != m_widthUnit->currentIndex())
	{
		QSignalBlocker b(m_heightUnit);
		m_heightUnit->setCurrentIndex(m_widthUnit->currentIndex());
	}
	else if (!m_guard && sender() == m_heightUnit && m_widthUnit->currentIndex() != m_heightUnit->currentIndex())
	{
		QSignalBlocker b(m_widthUnit);
		m_widthUnit->setCurrentIndex(m_heightUnit->currentIndex());
	}
	syncFields();
}

void ImageSizeDialog::onResampleToggled(bool on)
{
	// When resampling is off, proportions are inherently locked (pixels fixed).
	m_linkBtn->setEnabled(on);
	updateSummary();
}

void ImageSizeDialog::onLinkToggled(bool on)
{
	m_link = on;
}

void ImageSizeDialog::onResampleMethodChanged()
{
	updatePreview();
}

void ImageSizeDialog::accept()
{
	const int newW = qMax(1, qRound(m_pxW));
	const int newH = qMax(1, qRound(m_pxH));

	if (m_resampleCheck->isChecked() && (newW != m_source.width() || newH != m_source.height()))
	{
		m_result = m_source.scaled(newW, newH, Qt::IgnoreAspectRatio, transformationMode());
		// "Bicubic Smoother/Sharper" get a light post-filter over Qt's scaler:
		// a mild blur softens enlargements, a mild unsharp crisps reductions.
		const QString method = m_resampleMethod->currentText();
		if (method.contains(tr("Smoother")))
			m_result = lightBlur(m_result);
		else if (method.contains(tr("Sharper")))
			m_result = lightSharpen(m_result);
		m_resized = true;
	}
	else
	{
		m_result = m_source;
		m_resized = false;
	}

	// Stamp the chosen resolution into the result's DPI metadata.
	const int dpm = qRound(m_res / 0.0254);
	if (dpm > 0)
	{
		m_result.setDotsPerMeterX(dpm);
		m_result.setDotsPerMeterY(dpm);
	}

	QDialog::accept();
}
