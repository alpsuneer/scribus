/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "cropoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace
{
	QFrame* vSeparator(QWidget* parent)
	{
		auto* line = new QFrame(parent);
		line->setFrameShape(QFrame::VLine);
		line->setFrameShadow(QFrame::Sunken);
		return line;
	}

	enum RatioIndex
	{
		RatioFree = 0,
		RatioOriginal,
		RatioSquare,   // 1:1
		Ratio4x3,
		Ratio3x2,
		Ratio16x9,
		RatioA4Portrait,
		RatioA4Landscape,
		RatioCustom
	};
}

CropOptionsWidget::CropOptionsWidget(QWidget* parent)
	: ToolOptionsWidget(parent)
{
	auto* lay = new QHBoxLayout(this);
	lay->setContentsMargins(6, 2, 6, 2);
	lay->setSpacing(6);

	// Ratio preset
	lay->addWidget(new QLabel(tr("Ratio:"), this));
	m_ratio = new QComboBox(this);
	m_ratio->addItem(tr("Free"),           RatioFree);
	m_ratio->addItem(tr("Original Ratio"), RatioOriginal);
	m_ratio->addItem(tr("1:1 (Square)"),   RatioSquare);
	m_ratio->addItem(tr("4:3"),            Ratio4x3);
	m_ratio->addItem(tr("3:2"),            Ratio3x2);
	m_ratio->addItem(tr("16:9"),           Ratio16x9);
	m_ratio->addItem(tr("A4 Portrait"),    RatioA4Portrait);
	m_ratio->addItem(tr("A4 Landscape"),   RatioA4Landscape);
	m_ratio->addItem(tr("Custom..."),      RatioCustom);
	lay->addWidget(m_ratio);

	// W x H numeric
	auto* validator = new QDoubleValidator(0.0, 1e6, 3, this);
	m_width = new QLineEdit(this);
	m_width->setValidator(validator);
	m_width->setFixedWidth(64);
	m_width->setPlaceholderText(tr("W"));
	m_height = new QLineEdit(this);
	m_height->setValidator(validator);
	m_height->setFixedWidth(64);
	m_height->setPlaceholderText(tr("H"));
	lay->addWidget(m_width);
	lay->addWidget(new QLabel(QStringLiteral("×"), this));   // ×
	lay->addWidget(m_height);

	m_unit = new QComboBox(this);
	m_unit->addItems({ tr("px"), tr("in"), tr("mm"), tr("cm") });
	lay->addWidget(m_unit);

	lay->addWidget(vSeparator(this));

	// Resolution
	lay->addWidget(new QLabel(tr("Resolution:"), this));
	m_resolution = new QLineEdit(this);
	m_resolution->setValidator(new QDoubleValidator(1.0, 1e5, 2, this));
	m_resolution->setFixedWidth(54);
	m_resolution->setText(QStringLiteral("300"));
	lay->addWidget(m_resolution);
	m_resUnit = new QComboBox(this);
	m_resUnit->addItems({ tr("px/inch"), tr("px/cm") });
	lay->addWidget(m_resUnit);

	lay->addWidget(vSeparator(this));

	// Delete cropped pixels
	m_deletePixels = new QCheckBox(tr("Delete Cropped Pixels"), this);
	m_deletePixels->setChecked(true);
	lay->addWidget(m_deletePixels);

	lay->addWidget(vSeparator(this));

	// Straighten (stub)
	auto* straighten = new QPushButton(tr("Straighten"), this);
	lay->addWidget(straighten);

	lay->addStretch(1);

	// Commit / Cancel
	auto* commit = new QPushButton(QStringLiteral("✓ ") + tr("Commit"), this);
	commit->setStyleSheet(QStringLiteral("QPushButton { color: white; background: #2e7d32; padding: 2px 10px; }"));
	auto* cancel = new QPushButton(QStringLiteral("✗ ") + tr("Cancel"), this);
	cancel->setStyleSheet(QStringLiteral("QPushButton { color: white; background: #c62828; padding: 2px 10px; }"));
	lay->addWidget(commit);
	lay->addWidget(cancel);

	connect(m_ratio, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CropOptionsWidget::onRatioChanged);
	connect(m_width, &QLineEdit::editingFinished, this, &CropOptionsWidget::onWidthEdited);
	connect(m_height, &QLineEdit::editingFinished, this, &CropOptionsWidget::onHeightEdited);
	connect(commit, &QPushButton::clicked, this, &CropOptionsWidget::commitRequested);
	connect(cancel, &QPushButton::clicked, this, &CropOptionsWidget::cancelRequested);
	connect(straighten, &QPushButton::clicked, this, &CropOptionsWidget::straightenRequested);
}

double CropOptionsWidget::currentRatio() const
{
	switch (m_ratio->currentData().toInt())
	{
		case RatioOriginal:     return m_imageAspect > 0 ? m_imageAspect : 1.0;
		case RatioSquare:       return 1.0;
		case Ratio4x3:          return 4.0 / 3.0;
		case Ratio3x2:          return 3.0 / 2.0;
		case Ratio16x9:         return 16.0 / 9.0;
		case RatioA4Portrait:   return 210.0 / 297.0;
		case RatioA4Landscape:  return 297.0 / 210.0;
		default:                return 0.0;   // Free / Custom — unconstrained
	}
}

void CropOptionsWidget::onRatioChanged(int)
{
	const double ratio = currentRatio();
	const bool free = ratio <= 0.0;
	// Free/Custom leave the fields independent; a fixed ratio drives H from W.
	if (!free)
		onWidthEdited();
}

void CropOptionsWidget::onWidthEdited()
{
	if (m_updating)
		return;
	const double ratio = currentRatio();
	if (ratio <= 0.0)
		return;
	const double w = m_width->text().toDouble();
	if (w <= 0.0)
		return;
	m_updating = true;
	m_height->setText(QString::number(w / ratio, 'g', 6));
	m_updating = false;
}

void CropOptionsWidget::onHeightEdited()
{
	if (m_updating)
		return;
	const double ratio = currentRatio();
	if (ratio <= 0.0)
		return;
	const double h = m_height->text().toDouble();
	if (h <= 0.0)
		return;
	m_updating = true;
	m_width->setText(QString::number(h * ratio, 'g', 6));
	m_updating = false;
}
