#include "suneerarrangebar.h"

#include <QHBoxLayout>
#include <QToolButton>

#include "pageitem.h"
#include "propertiespalette_xyz.h"
#include "scraction.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scrspinbox.h"
#include "selection.h"
#include "units.h"

SuneerArrangeBar::SuneerArrangeBar(ScribusMainWindow* mw, QWidget* parent)
	: QWidget(parent), m_mw(mw)
{
	setObjectName("SuneerArrangeBar");
	QHBoxLayout* lay = new QHBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->setSpacing(1);

	m_rotation = new ScrSpinBox(this, SC_DEG);
	m_rotation->setWrapping(true);
	m_rotation->setDecimals(1);
	m_rotation->setFixedWidth(58);
	m_rotation->setKeyboardTracking(false);   // apply on Enter / focus out, one undo step
	lay->addWidget(m_rotation);

	auto mk = [&](QToolButton*& b) {
		b = new QToolButton(this);
		b->setFixedSize(24, 22);
		b->setAutoRaise(false);
		lay->addWidget(b);
	};
	mk(m_flipH); mk(m_flipV); mk(m_top); mk(m_up); mk(m_down); mk(m_bottom);

	connect(m_rotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SuneerArrangeBar::onRotationChanged);
	// Each click is the panel's click: same slot, same undo, same checks.
	connect(m_flipH,  &QToolButton::clicked, this, [this] { if (m_pal) m_pal->flipH->click(); });
	connect(m_flipV,  &QToolButton::clicked, this, [this] { if (m_pal) m_pal->flipV->click(); });
	connect(m_top,    &QToolButton::clicked, this, [this] { if (m_pal) m_pal->levelTop->click(); });
	connect(m_up,     &QToolButton::clicked, this, [this] { if (m_pal) m_pal->levelUp->click(); });
	connect(m_down,   &QToolButton::clicked, this, [this] { if (m_pal) m_pal->levelDown->click(); });
	connect(m_bottom, &QToolButton::clicked, this, [this] { if (m_pal) m_pal->levelBottom->click(); });
	setEnabled(false);
}

void SuneerArrangeBar::setDocument(ScribusDoc* doc)
{
	m_doc = doc;
	refresh();
}

void SuneerArrangeBar::setPalette(PropertiesPalette_XYZ* pal)
{
	if (m_pal == pal)
		return;
	if (m_pal)
		disconnect(m_pal, nullptr, this, nullptr);
	m_pal = pal;
	if (m_pal)
		connect(m_pal, &PropertiesPalette_XYZ::arrangeDisplayChanged, this, &SuneerArrangeBar::refresh);
	refresh();
}

bool SuneerArrangeBar::owns(const QWidget* w) const
{
	return w && (w == this || isAncestorOf(w));
}

void SuneerArrangeBar::revert()
{
	refresh();
}

void SuneerArrangeBar::mirrorButton(QToolButton* mine, QToolButton* theirs, const char* actionName, bool enabled)
{
	mine->setIcon(theirs->icon());
	QString tip = theirs->toolTip();
	// The menu action's shortcut, when it has one, as "Tooltip (Ctrl+…)".
	if (m_mw && actionName && m_mw->scrActions.contains(QString::fromLatin1(actionName)))
	{
		ScrAction* a = m_mw->scrActions[QString::fromLatin1(actionName)];
		if (a)
		{
			if (tip.isEmpty())
				tip = a->cleanMenuText();
			const QString sc = a->shortcut().toString(QKeySequence::NativeText);
			if (!sc.isEmpty() && !tip.contains(sc))
				tip += QStringLiteral(" (%1)").arg(sc);
		}
	}
	mine->setToolTip(tip);
	mine->setCheckable(theirs->isCheckable());
	mine->setChecked(theirs->isCheckable() && theirs->isChecked());
	mine->setEnabled(enabled);
}

void SuneerArrangeBar::levelRoom(bool& canUp, bool& canDown) const
{
	canUp = canDown = false;
	if (!m_doc || !m_doc->m_Selection || m_doc->m_Selection->isEmpty())
		return;
	// Same list the Raise/Lower commands walk: the group's children, or the
	// document items of the current page set.
	const PageItem* first = m_doc->m_Selection->itemAt(0);
	const QList<PageItem*>* list = first->isGroupChild() ? &first->parentGroup()->groupItemList : m_doc->Items;
	if (!list || list->count() <= 1)
		return;
	for (int i = 0; i < m_doc->m_Selection->count(); ++i)
	{
		int idx = list->indexOf(m_doc->m_Selection->itemAt(i));
		if (idx < 0)
			continue;
		canUp   = canUp   || idx < list->count() - 1;
		canDown = canDown || idx > 0;
	}
}

void SuneerArrangeBar::refresh()
{
	m_updating = true;
	bool palOn = m_doc && m_pal && m_pal->isEnabled();
	if (m_pal)
	{
		// Rotation: the panel's value, read-only state (locked item) and enabled state.
		bool rotOn = palOn && m_pal->rotationSpin->isEnabledTo(m_pal) && !m_pal->rotationSpin->isReadOnly();
		m_rotation->setValue(m_pal->rotationSpin->value());
		m_rotation->setToolTip(m_pal->rotationSpin->toolTip().isEmpty() ? tr("Rotation of the selected item") : m_pal->rotationSpin->toolTip());
		m_rotation->setEnabled(rotOn);

		bool flipOn = palOn && m_pal->flipH->isEnabledTo(m_pal);
		mirrorButton(m_flipH, m_pal->flipH, "itemFlipH", flipOn);
		mirrorButton(m_flipV, m_pal->flipV, "itemFlipV", flipOn);

		// Level buttons: enabled like the panel's group (same parent, not
		// locked), and greyed when there is no further up or down to go.
		bool levelOn = palOn && m_pal->labelLevel->isEnabledTo(m_pal);
		bool canUp = false, canDown = false;
		if (levelOn)
			levelRoom(canUp, canDown);
		mirrorButton(m_top,    m_pal->levelTop,    "itemRaiseToTop",   levelOn && canUp);
		mirrorButton(m_up,     m_pal->levelUp,     "itemRaise",        levelOn && canUp);
		mirrorButton(m_down,   m_pal->levelDown,   "itemLower",        levelOn && canDown);
		mirrorButton(m_bottom, m_pal->levelBottom, "itemLowerToBottom", levelOn && canDown);
	}
	else
		m_rotation->setValue(0);
	setEnabled(palOn);
	m_updating = false;
}

void SuneerArrangeBar::onRotationChanged(double v)
{
	if (m_updating || !m_pal || !m_doc)
		return;
	if (!m_pal->rotationSpin->isEnabledTo(m_pal) || m_pal->rotationSpin->isReadOnly())
		return;
	// Hand the value to the panel's field: its valueChanged runs the panel's
	// own handleRotation() (group transaction, multiple selection, undo).
	m_pal->rotationSpin->setValue(v);
}
