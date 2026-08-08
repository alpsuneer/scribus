/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "canvassizedialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
	enum Unit { UPixels = 0, UPercent, UInches, UCm, UMm };
	enum ExtColor { EBackground = 0, EForeground, EWhite, EBlack, ETransparent, EOther };

	double unitsPerInch(int unit)
	{
		switch (unit)
		{
			case UInches: return 1.0;
			case UCm:     return 2.54;
			case UMm:     return 25.4;
			default:      return 1.0;
		}
	}

	// Arrow glyphs for the 3×3 anchor selector (index = row*3 + col).
	const char* anchorGlyph(int i)
	{
		static const char* glyphs[9] = { "↖", "↑", "↗", "←", "•", "→", "↙", "↓", "↘" };
		return (i >= 0 && i < 9) ? glyphs[i] : "•";
	}
}

CanvasSizeDialog::CanvasSizeDialog(const QImage& source, QWidget* parent)
	: QDialog(parent), m_source(source), m_result(source)
{
	setWindowTitle(tr("Canvas Size"));
	setModal(true);

	if (source.dotsPerMeterX() > 0)
		m_res = source.dotsPerMeterX() * 0.0254;
	if (m_res < 1.0)
		m_res = 300.0;

	auto* main = new QVBoxLayout(this);

	// Current size
	m_currentSizeLabel = new QLabel(this);
	main->addWidget(m_currentSizeLabel);

	// New size group
	auto* newGroup = new QGroupBox(tr("New Size"), this);
	auto* grid = new QGridLayout(newGroup);

	grid->addWidget(new QLabel(tr("Width:"), this), 0, 0);
	m_widthSpin = new QDoubleSpinBox(this);
	m_widthSpin->setDecimals(2);
	m_widthSpin->setRange(-1.0e6, 1.0e6);
	m_widthSpin->setKeyboardTracking(false);
	grid->addWidget(m_widthSpin, 0, 1);

	grid->addWidget(new QLabel(tr("Height:"), this), 1, 0);
	m_heightSpin = new QDoubleSpinBox(this);
	m_heightSpin->setDecimals(2);
	m_heightSpin->setRange(-1.0e6, 1.0e6);
	m_heightSpin->setKeyboardTracking(false);
	grid->addWidget(m_heightSpin, 1, 1);

	m_unit = new QComboBox(this);
	m_unit->addItems({ tr("pixels"), tr("percent"), tr("inches"), tr("cm"), tr("mm") });
	grid->addWidget(m_unit, 0, 2);

	m_relative = new QCheckBox(tr("Relative"), this);
	grid->addWidget(m_relative, 2, 0, 1, 3);

	main->addWidget(newGroup);

	// Anchor selector (3×3)
	auto* anchorGroup = new QGroupBox(tr("Anchor"), this);
	auto* anchorGrid = new QGridLayout(anchorGroup);
	anchorGrid->setSpacing(2);
	m_anchorGroup = new QButtonGroup(this);
	m_anchorGroup->setExclusive(true);
	for (int i = 0; i < 9; ++i)
	{
		auto* btn = new QToolButton(this);
		btn->setCheckable(true);
		btn->setFixedSize(28, 28);
		btn->setText(QString::fromUtf8(anchorGlyph(i)));
		if (i == 4)
			btn->setChecked(true);   // default anchor = center
		m_anchorGroup->addButton(btn, i);
		anchorGrid->addWidget(btn, i / 3, i % 3);
	}
	main->addWidget(anchorGroup);

	// Extension color
	auto* colorRow = new QHBoxLayout;
	colorRow->addWidget(new QLabel(tr("Canvas extension color:"), this));
	m_colorCombo = new QComboBox(this);
	m_colorCombo->addItem(tr("Background"), EBackground);
	m_colorCombo->addItem(tr("Foreground"), EForeground);
	m_colorCombo->addItem(tr("White"), EWhite);
	m_colorCombo->addItem(tr("Black"), EBlack);
	m_colorCombo->addItem(tr("Transparent"), ETransparent);
	m_colorCombo->addItem(tr("Other..."), EOther);
	colorRow->addWidget(m_colorCombo, 1);
	main->addLayout(colorRow);

	// Buttons
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &CanvasSizeDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CanvasSizeDialog::reject);
	main->addWidget(buttons);

	connect(m_relative, &QCheckBox::toggled, this, &CanvasSizeDialog::onRelativeToggled);
	connect(m_unit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CanvasSizeDialog::onUnitChanged);
	connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CanvasSizeDialog::onExtensionColorChanged);

	// Seed the new-size fields with the current dimensions.
	m_widthSpin->setValue(source.width());
	m_heightSpin->setValue(source.height());
	updateSummary();
}

double CanvasSizeDialog::toPixels(double value) const
{
	const int unit = m_unit->currentIndex();
	if (unit == UPixels)
		return value;
	if (unit == UPercent)
		return value;   // handled specially in targetPixelSize()
	return value / unitsPerInch(unit) * m_res;
}

double CanvasSizeDialog::fromPixels(double px) const
{
	const int unit = m_unit->currentIndex();
	if (unit == UPixels || unit == UPercent)
		return px;
	return px / m_res * unitsPerInch(unit);
}

QSize CanvasSizeDialog::targetPixelSize() const
{
	const int unit = m_unit->currentIndex();
	const bool relative = m_relative->isChecked();
	double wPx = 0.0, hPx = 0.0;
	if (unit == UPercent)
	{
		// Percent is relative to the current size (delta% when Relative).
		const double wp = m_widthSpin->value() / 100.0;
		const double hp = m_heightSpin->value() / 100.0;
		wPx = relative ? m_source.width() + m_source.width() * wp : m_source.width() * wp;
		hPx = relative ? m_source.height() + m_source.height() * hp : m_source.height() * hp;
	}
	else
	{
		wPx = toPixels(m_widthSpin->value());
		hPx = toPixels(m_heightSpin->value());
		if (relative)
		{
			wPx = m_source.width() + wPx;
			hPx = m_source.height() + hPx;
		}
	}
	return QSize(qMax(1, qRound(wPx)), qMax(1, qRound(hPx)));
}

void CanvasSizeDialog::updateSummary()
{
	m_currentSizeLabel->setText(tr("Current Size: %1 × %2 pixels")
		.arg(m_source.width()).arg(m_source.height()));
}

void CanvasSizeDialog::onRelativeToggled(bool on)
{
	// Relative fields express deltas; absolute fields the target size.
	if (on)
	{
		m_widthSpin->setValue(0);
		m_heightSpin->setValue(0);
	}
	else
	{
		m_widthSpin->setValue(fromPixels(m_source.width()));
		m_heightSpin->setValue(fromPixels(m_source.height()));
	}
}

void CanvasSizeDialog::onUnitChanged()
{
	if (m_relative->isChecked())
		return;   // deltas: leave the numbers as-is
	// Re-express the current absolute target in the new unit.
	if (m_unit->currentIndex() == UPercent)
	{
		m_widthSpin->setValue(100);
		m_heightSpin->setValue(100);
	}
	else
	{
		m_widthSpin->setValue(fromPixels(m_source.width()));
		m_heightSpin->setValue(fromPixels(m_source.height()));
	}
}

void CanvasSizeDialog::onExtensionColorChanged(int index)
{
	const int id = m_colorCombo->itemData(index).toInt();
	m_transparent = false;
	switch (id)
	{
		case EBackground:  m_extensionColor = Qt::white; break;   // no doc bg/fg here
		case EForeground:  m_extensionColor = Qt::black; break;
		case EWhite:       m_extensionColor = Qt::white; break;
		case EBlack:       m_extensionColor = Qt::black; break;
		case ETransparent: m_transparent = true; break;
		case EOther:
		{
			const QColor c = QColorDialog::getColor(m_extensionColor, this, tr("Canvas Extension Color"));
			if (c.isValid())
				m_extensionColor = c;
			break;
		}
		default: break;
	}
}

void CanvasSizeDialog::accept()
{
	const QSize target = targetPixelSize();
	if (target == m_source.size())
	{
		m_result = m_source;
		m_changed = false;
		QDialog::accept();
		return;
	}

	QImage canvas(target, QImage::Format_ARGB32);
	if (m_transparent)
		canvas.fill(Qt::transparent);
	else
		canvas.fill(m_extensionColor);

	// Position the original within the new canvas per the chosen anchor.
	const int anchor = m_anchorGroup->checkedId() < 0 ? 4 : m_anchorGroup->checkedId();
	const int col = anchor % 3;   // 0 left, 1 center, 2 right
	const int row = anchor / 3;   // 0 top,  1 middle, 2 bottom
	const int dx = target.width() - m_source.width();
	const int dy = target.height() - m_source.height();
	const int xoff = (col == 0) ? 0 : (col == 1 ? dx / 2 : dx);
	const int yoff = (row == 0) ? 0 : (row == 1 ? dy / 2 : dy);

	QPainter p(&canvas);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.drawImage(xoff, yoff, m_source);
	p.end();

	// Preserve DPI metadata from the original image
	m_result = canvas;
	m_result.setDotsPerMeterX(m_source.dotsPerMeterX());
	m_result.setDotsPerMeterY(m_source.dotsPerMeterY());
	m_changed = true;
	QDialog::accept();
}
