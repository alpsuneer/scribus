/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/contourdetectdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "pageitem_imageframe.h"

/*!
 \brief Shows the mask with the traced outline drawn over it.

 Draws the mask rather than the photo: what is being traced is the alpha, and
 seeing the alpha is what makes a wrong threshold obvious.
 */
class ContourPreviewWidget : public QWidget
{
public:
	explicit ContourPreviewWidget(QWidget* parent) : QWidget(parent)
	{
		setMinimumSize(260, 200);
	}

	void setMask(const QImage& mask) { m_mask = mask; update(); }
	void setRings(const QList<ScContour::Ring>& rings) { m_rings = rings; update(); }
	void setThreshold(int t) { m_threshold = t; update(); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), palette().base());
		if (m_mask.isNull())
			return;

		// Letterbox the mask into the widget and reuse the same mapping for
		// the outline, so the two cannot drift apart.
		QSize shown = m_mask.size();
		shown.scale(size() - QSize(2, 2), Qt::KeepAspectRatio);
		QRect target(QPoint(0, 0), shown);
		target.moveCenter(rect().center());

		// Show what the threshold actually selects, not the raw grey ramp.
		QImage binary(m_mask.size(), QImage::Format_Grayscale8);
		for (int y = 0; y < m_mask.height(); ++y)
		{
			const uchar* src = m_mask.constScanLine(y);
			uchar* dst = binary.scanLine(y);
			for (int x = 0; x < m_mask.width(); ++x)
				dst[x] = (int(src[x]) >= m_threshold) ? uchar(210) : uchar(60);
		}
		p.drawImage(target, binary);

		if (m_rings.isEmpty())
			return;

		double sx = double(target.width()) / double(m_mask.width());
		double sy = double(target.height()) / double(m_mask.height());
		p.setRenderHint(QPainter::Antialiasing);
		p.translate(target.topLeft());
		p.scale(sx, sy);

		// Hairline in device space: the scale above would otherwise make the
		// outline thickness depend on the zoom the letterbox happened to pick.
		QPen pen(QColor(220, 30, 30));
		pen.setCosmetic(true);
		pen.setWidth(2);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		for (const ScContour::Ring& r : m_rings)
		{
			// Holes in a lighter shade so the two kinds are told apart.
			pen.setColor(r.isHole() ? QColor(250, 150, 40) : QColor(220, 30, 30));
			p.setPen(pen);
			p.drawPolygon(r.points);
		}
	}

private:
	QImage m_mask;
	QList<ScContour::Ring> m_rings;
	int m_threshold {128};
};

ContourDetectDialog::ContourDetectDialog(QWidget* parent, PageItem_ImageFrame* item)
	: QDialog(parent), m_item(item)
{
	setWindowTitle(tr("Detect Contour from Image"));
	setModal(true);

	if (m_item)
		m_mask = m_item->contourSourceMask();

	auto* form = new QFormLayout;

	auto* thresholdRow = new QHBoxLayout;
	m_thresholdSlider = new QSlider(Qt::Horizontal, this);
	m_thresholdSlider->setRange(1, 254);
	m_thresholdSlider->setValue(128);
	m_thresholdSpin = new QSpinBox(this);
	m_thresholdSpin->setRange(1, 254);
	m_thresholdSpin->setValue(128);
	thresholdRow->addWidget(m_thresholdSlider);
	thresholdRow->addWidget(m_thresholdSpin);
	// No ampersand mnemonics on these two: addRow() with a QLayout has no
	// widget to make the label's buddy, and an unbuddied QLabel renders the
	// '&' literally instead of turning it into an accelerator.
	form->addRow(tr("Alpha threshold:"), thresholdRow);

	auto* toleranceRow = new QHBoxLayout;
	m_toleranceSlider = new QSlider(Qt::Horizontal, this);
	// Slider works in tenths so it can carry the fractional range.
	m_toleranceSlider->setRange(5, 200);
	m_toleranceSlider->setValue(20);
	m_toleranceSpin = new QDoubleSpinBox(this);
	m_toleranceSpin->setRange(0.5, 20.0);
	m_toleranceSpin->setSingleStep(0.5);
	m_toleranceSpin->setDecimals(1);
	m_toleranceSpin->setValue(2.0);
	m_toleranceSpin->setSuffix(tr(" px"));
	toleranceRow->addWidget(m_toleranceSlider);
	toleranceRow->addWidget(m_toleranceSpin);
	form->addRow(tr("Simplify tolerance:"), toleranceRow);

	m_modeCombo = new QComboBox(this);
	m_modeCombo->addItem(tr("Largest region only"), int(ScContour::Mode::LargestOnly));
	m_modeCombo->addItem(tr("All separate regions"), int(ScContour::Mode::AllRegions));
	m_modeCombo->addItem(tr("Include holes"), int(ScContour::Mode::IncludeHoles));
	form->addRow(tr("&Contour:"), m_modeCombo);

	m_wrapCheck = new QCheckBox(tr("Use the contour for text wrap"), this);
	m_wrapCheck->setChecked(true);
	m_wrapCheck->setToolTip(tr("Without this the contour is stored but text keeps "
	                           "wrapping around whatever the frame is set to."));
	form->addRow(QString(), m_wrapCheck);

	m_preview = new ContourPreviewWidget(this);
	m_preview->setMask(m_mask);

	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_preview, 1);
	layout->addWidget(m_summary);
	layout->addLayout(form);
	layout->addWidget(m_buttons);

	connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int v) {
		if (m_updating) return;
		m_updating = true; m_thresholdSpin->setValue(v); m_updating = false;
		refreshPreview();
	});
	connect(m_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (m_updating) return;
		m_updating = true; m_thresholdSlider->setValue(v); m_updating = false;
		refreshPreview();
	});
	connect(m_toleranceSlider, &QSlider::valueChanged, this, [this](int v) {
		if (m_updating) return;
		m_updating = true; m_toleranceSpin->setValue(v / 10.0); m_updating = false;
		refreshPreview();
	});
	connect(m_toleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
		if (m_updating) return;
		m_updating = true; m_toleranceSlider->setValue(qRound(v * 10.0)); m_updating = false;
		refreshPreview();
	});
	connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ContourDetectDialog::refreshPreview);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	refreshPreview();
}

int ContourDetectDialog::threshold() const
{
	return m_thresholdSpin->value();
}

double ContourDetectDialog::tolerance() const
{
	return m_toleranceSpin->value();
}

ScContour::Mode ContourDetectDialog::mode() const
{
	return static_cast<ScContour::Mode>(m_modeCombo->currentData().toInt());
}

bool ContourDetectDialog::useForTextWrap() const
{
	return m_wrapCheck->isChecked();
}

void ContourDetectDialog::refreshPreview()
{
	if (m_mask.isNull())
	{
		m_summary->setText(tr("This image has nothing transparent to trace."));
		m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		return;
	}

	QList<ScContour::Ring> rings = ScContour::detect(m_mask, threshold(), tolerance(), mode());
	m_preview->setThreshold(threshold());
	m_preview->setRings(rings);

	if (rings.isEmpty())
	{
		m_summary->setText(tr("No visible region at this threshold."));
		m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		return;
	}

	int nodes = 0;
	int holes = 0;
	for (const ScContour::Ring& r : rings)
	{
		nodes += r.points.size();
		if (r.isHole())
			++holes;
	}
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);

	QString text = tr("%n region(s)", "", rings.size() - holes);
	if (holes > 0)
		text += tr(", %n hole(s)", "", holes);
	text += tr(", %n node(s)", "", nodes);
	if (nodes > 2000)
	{
		text += QLatin1String("  ");
		text += tr("That many nodes will slow down layout - raise the tolerance.");
	}
	m_summary->setText(text);
}
