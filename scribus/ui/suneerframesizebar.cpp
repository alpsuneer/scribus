#include "suneerframesizebar.h"

#include <QHBoxLayout>
#include <QLabel>

#include "iconmanager.h"
#include "linkbutton.h"
#include "propertiespalette_xyz.h"
#include "scribusapp.h"
#include "scribusdoc.h"
#include "scrspinbox.h"
#include "units.h"

SuneerFrameSizeBar::SuneerFrameSizeBar(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("SuneerFrameSizeBar");
	QHBoxLayout* lay = new QHBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->setSpacing(2);

	m_icon = new QLabel(this);
	m_icon->setFixedSize(16, 16);
	m_icon->setScaledContents(true);
	m_icon->setVisible(false);   // compact bar: the two fields and the lock only

	// Compact: no "W:"/"H:" labels on the bar (tooltips say which is which),
	// so the whole group is ~215 px instead of ~300.
	m_wLbl = new QLabel(this);
	m_wLbl->setVisible(false);
	m_w = new ScrSpinBox(this, SC_PT);
	m_w->setFixedWidth(86);
	lay->addWidget(m_w);

	m_hLbl = new QLabel(this);
	m_hLbl->setVisible(false);
	m_h = new ScrSpinBox(this, SC_PT);
	m_h->setFixedWidth(86);
	lay->addWidget(m_h);

	m_lock = new LinkButton(this);
	m_lock->setAutoRaise(true);
	lay->addWidget(m_lock);

	// Tab walks W -> H -> lock, in reading order.
	QWidget::setTabOrder(m_w, m_h);
	QWidget::setTabOrder(m_h, m_lock);

	connect(m_w, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerFrameSizeBar::onWidthChanged);
	connect(m_h, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerFrameSizeBar::onHeightChanged);
	connect(m_lock, &QToolButton::toggled, this, &SuneerFrameSizeBar::onLockToggled);
	connect(ScQApp, SIGNAL(iconSetChanged()), this, SLOT(iconSetChange()));

	iconSetChange();
	languageChange();
	setEnabled(false);
}

void SuneerFrameSizeBar::iconSetChange()
{
	m_icon->setPixmap(IconManager::instance().loadPixmap("frame-resize"));
}

void SuneerFrameSizeBar::languageChange()
{
	m_wLbl->setText(tr("W:"));
	m_hLbl->setText(tr("H:"));
	m_icon->setToolTip(tr("Frame width / height (same as Properties → X, Y, Z)"));
	m_w->setToolTip(tr("W: frame width"));
	m_h->setToolTip(tr("H: frame height"));
	m_lock->setToolTip(tr("Keep the frame's width and height ratio (shared with Properties)"));
}

void SuneerFrameSizeBar::setDocument(ScribusDoc* doc)
{
	m_doc = doc;
	m_unitIndex = -1;
	refresh();
}

void SuneerFrameSizeBar::setPalette(PropertiesPalette_XYZ* pal)
{
	if (m_pal == pal)
		return;
	if (m_pal)
		disconnect(m_pal, nullptr, this, nullptr);
	m_pal = pal;
	if (!m_pal)
		return;
	connect(m_pal, &PropertiesPalette_XYZ::sizeDisplayChanged, this, &SuneerFrameSizeBar::refresh);
	// One lock state, not two: the panel's button is the state, ours mirrors it.
	connect(m_pal->keepFrameWHRatioButton, &QToolButton::toggled, this, [this](bool on) {
		if (m_lock->isChecked() != on)
		{
			m_updating = true;
			m_lock->setChecked(on);
			m_updating = false;
		}
	});
	refresh();
}

bool SuneerFrameSizeBar::owns(const QWidget* w) const
{
	return w && (w == this || isAncestorOf(w));
}

void SuneerFrameSizeBar::refresh()
{
	if (m_drawing)
		return;   // the rubber band owns the display until the mouse is released
	m_updating = true;
	m_w->setReadOnly(false);
	m_h->setReadOnly(false);
	bool haveDoc = m_doc && m_pal;
	if (haveDoc)
	{
		int unitIndex = m_doc->unitIndex();
		if (unitIndex != m_unitIndex)
		{
			m_unitIndex = unitIndex;
			m_w->setNewUnit(unitIndex);
			m_h->setNewUnit(unitIndex);
			m_w->setDecimals(2);   // 2 decimals fit the compact field ("100.00 mm")
			m_h->setDecimals(2);
		}
		// Same constants (page sizes etc.) and limits as the panel's own fields,
		// so "10cm", "65+5" and the document constants behave identically.
		m_w->setConstants(&m_doc->constants());
		m_h->setConstants(&m_doc->constants());
		double mn, mx, val;
		int deci;
		// 2 decimals on the bar (the compact field shows "100.00 mm"); the
		// palette keeps its own precision.
		m_pal->widthSpin->getValues(&mn, &mx, &deci, &val);
		m_w->setValues(mn, mx, qMin(deci, 2), val);
		m_pal->heightSpin->getValues(&mn, &mx, &deci, &val);
		m_h->setValues(mn, mx, qMin(deci, 2), val);

		// The panel is disabled as a whole with no selection; a single field is
		// disabled for a locked item or a line's hidden height.
		bool palOn = m_pal->isEnabled();
		m_w->setEnabled(palOn && m_pal->widthSpin->isEnabledTo(m_pal));
		m_h->setEnabled(palOn && m_pal->heightSpin->isEnabledTo(m_pal) && !m_pal->heightSpin->isHidden());
		m_lock->setEnabled(palOn && !m_pal->keepFrameWHRatioButton->isHidden());
		m_lock->setChecked(m_pal->keepFrameWHRatioButton->isChecked());
		setEnabled(palOn);
	}
	else
	{
		m_w->setConstants(nullptr);
		m_h->setConstants(nullptr);
		m_w->showValue(0);
		m_h->showValue(0);
		setEnabled(false);
	}
	m_updating = false;
}

void SuneerFrameSizeBar::revert()
{
	refresh();
}

void SuneerFrameSizeBar::showDrawingSize(double wPt, double hPt)
{
	if (!m_doc)
		return;
	m_updating = true;
	int unitIndex = m_doc->unitIndex();
	if (unitIndex != m_unitIndex)
	{
		m_unitIndex = unitIndex;
		m_w->setNewUnit(unitIndex);
		m_h->setNewUnit(unitIndex);
		m_w->setDecimals(2);
		m_h->setDecimals(2);
	}
	double ratio = m_doc->unitRatio();
	m_drawing = true;
	setEnabled(true);
	m_w->setEnabled(true);
	m_h->setEnabled(true);
	m_w->setReadOnly(true);
	m_h->setReadOnly(true);
	m_lock->setEnabled(false);
	m_w->showValue(qAbs(wPt) * ratio);
	m_h->showValue(qAbs(hPt) * ratio);
	m_updating = false;
}

void SuneerFrameSizeBar::endDrawingSize()
{
	m_drawing = false;
	refresh();
}

void SuneerFrameSizeBar::onWidthChanged(double v)
{
	if (m_updating || !m_pal || !m_doc)
		return;
	m_pal->applyWidthFromToolbar(v);
}

void SuneerFrameSizeBar::onHeightChanged(double v)
{
	if (m_updating || !m_pal || !m_doc)
		return;
	m_pal->applyHeightFromToolbar(v);
}

void SuneerFrameSizeBar::onLockToggled(bool on)
{
	if (m_updating || !m_pal)
		return;
	if (m_pal->keepFrameWHRatioButton->isChecked() != on)
		m_pal->keepFrameWHRatioButton->setChecked(on);
}
