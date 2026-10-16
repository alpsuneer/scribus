/*
 See suneergappopup.h.
*/
#include "suneergappopup.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>

#include "linkbutton.h"

// ---- glyphs ----------------------------------------------------------------
// A bold rounded plus / minus (bar 25 % of the glyph box, corner radius 3 %,
// glyph 80 % of the icon), optionally after a direction arrow. The SVG is made
// for every rendered size in device pixels and coloured with the theme's
// button text colour at paint time (disabled: the palette's disabled colour,
// dimmed), so it stays crisp on HiDPI and follows a theme switch.
namespace
{
class SuneerGapGlyphEngine : public QIconEngine
{
public:
	SuneerGapGlyphEngine(char sign, char arrow) : m_sign(sign), m_arrow(arrow) {}

	QIconEngine* clone() const override { return new SuneerGapGlyphEngine(m_sign, m_arrow); }
	QString key() const override { return QStringLiteral("SuneerGapGlyph"); }

	void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
	{
		const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
		const QSize px(qRound(rect.width() * dpr), qRound(rect.height() * dpr));
		QSvgRenderer r(svg(px, mode).toUtf8());
		r.render(painter, QRectF(rect));
	}

	QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
	{
		return scaledPixmap(size, mode, state, 1.0);
	}

	QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override
	{
		const QSize px(qRound(size.width() * scale), qRound(size.height() * scale));
		QImage img(px, QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::transparent);
		{
			QPainter p(&img);
			p.setRenderHint(QPainter::Antialiasing);
			QSvgRenderer r(svg(px, mode).toUtf8());
			r.render(&p, QRectF(QPointF(0, 0), QSizeF(px)));
		}
		QPixmap pm = QPixmap::fromImage(img);
		pm.setDevicePixelRatio(scale);
		return pm;
	}

private:
	QString svg(const QSize& px, QIcon::Mode mode) const
	{
		const QPalette pal = QApplication::palette();
		QColor c = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText);
		if (mode == QIcon::Disabled)
			c.setAlphaF(0.45);   // dimmed even when a style's disabled text colour equals the normal one
		const QString fill = QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());

		const int w = qMax(1, px.width());
		const int h = qMax(1, px.height());
		QString body;
		if (m_sign == 's')
			body = sidesBody(w, h, fill);
		else
			body = signBody(w, h);
		return QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%2' viewBox='0 0 %1 %2'>"
		                      "<g fill='%3' fill-opacity='%4'>%5</g></svg>")
		    .arg(w).arg(h).arg(fill).arg(c.alphaF()).arg(body);
	}

	QString signBody(int w, int h) const
	{
		// glyph box: the whole icon, or its right part beside the arrow
		const int box = m_arrow ? qMin(h, w * 11 / 20) : qMin(w, h);
		int g = qRound(box * 0.80);                 // glyph 80 % of the box
		const int t = qMax(2, qRound(box * 0.25));  // bar 25 % of the box
		if ((g - t) % 2)                            // even margins on both sides of the bar
			++g;
		const double rx = qMax(0.5, box * 0.03);
		const int bx = m_arrow ? w - box : (w - box) / 2;
		const int by = (h - box) / 2;
		const int gx = bx + (box - g) / 2;
		const int gy = by + (box - g) / 2;
		const int mid = (g - t) / 2;

		QString body;
		auto rectEl = [&](int x, int y, int rw, int rh) {
			body += QStringLiteral("<rect x='%1' y='%2' width='%3' height='%4' rx='%5'/>")
			            .arg(x).arg(y).arg(rw).arg(rh).arg(rx);
		};
		rectEl(gx, gy + mid, g, t);                 // horizontal bar (plus and minus)
		if (m_sign == '+')
			rectEl(gx + mid, gy, t, g);             // vertical bar

		if (m_arrow)
		{
			// arrow in the left part, pointing right before rotation
			const double ab = qMin(double(h), double(w - box)) * 0.9;
			const double cx = (w - box) / 2.0;
			const double cy = h / 2.0;
			const double s = qMax(1.5, ab * 0.18);  // shaft thickness
			const double hl = ab * 0.45;            // head length
			const double x0 = cx - ab / 2, x1 = cx + ab / 2;
			const QString path = QStringLiteral("M%1,%2 L%3,%2 L%3,%4 L%5,%6 L%3,%7 L%3,%8 L%1,%8 Z")
			    .arg(x0).arg(cy - s / 2).arg(x1 - hl).arg(cy - ab / 2).arg(x1).arg(cy).arg(cy + ab / 2).arg(cy + s / 2);
			const int deg = m_arrow == 'u' ? -90 : m_arrow == 'd' ? 90 : m_arrow == 'l' ? 180 : 0;
			body += QStringLiteral("<path d='%1' transform='rotate(%2 %3 %4)'/>")
			            .arg(path).arg(deg).arg(cx).arg(cy);
		}
		return body;
	}

	// "Gap sides": a frame outline with a bold gap bar outside each side, and a
	// small down arrow at the right (the button opens a panel).
	QString sidesBody(int w, int h, const QString& fill) const
	{
		const int box = qMin(h, w - h / 3);         // square part, the arrow gets the rest
		const double u = box / 20.0;                // 20-unit grid
		const double t = qMax(1.0, qRound(box * 0.15) * 1.0);   // gap bar thickness
		const double rx = qMax(0.5, box * 0.03);
		const double fw = qMax(1.0, qRound(u * 1.5) * 1.0);    // frame stroke
		QString body;
		// frame (stroked, no fill)
		body += QStringLiteral("<rect x='%1' y='%1' width='%2' height='%2' fill='none' stroke='%3' stroke-width='%4'/>")
		            .arg(u * 6 + fw / 2).arg(u * 8 - fw).arg(fill).arg(fw);
		// bars: top, bottom, left, right
		const double a = u * 5, len = u * 10;
		body += QStringLiteral("<rect x='%1' y='%2' width='%3' height='%4' rx='%5'/>").arg(a).arg(u * 1).arg(len).arg(t).arg(rx);
		body += QStringLiteral("<rect x='%1' y='%2' width='%3' height='%4' rx='%5'/>").arg(a).arg(box - u * 1 - t).arg(len).arg(t).arg(rx);
		body += QStringLiteral("<rect x='%1' y='%2' width='%3' height='%4' rx='%5'/>").arg(u * 1).arg(a).arg(t).arg(len).arg(rx);
		body += QStringLiteral("<rect x='%1' y='%2' width='%3' height='%4' rx='%5'/>").arg(box - u * 1 - t).arg(a).arg(t).arg(len).arg(rx);
		// small down arrow
		const double ax0 = box + (w - box) * 0.1, ax1 = w - (w - box) * 0.1;
		const double ay = h * 0.62, ah = (ax1 - ax0) / 2;
		body += QStringLiteral("<path d='M%1,%2 L%3,%2 L%4,%5 Z'/>").arg(ax0).arg(ay - ah / 2).arg(ax1).arg((ax0 + ax1) / 2).arg(ay + ah / 2);
		return body;
	}

	char m_sign;
	char m_arrow;
};

// The frame diagram in the middle of the popup: the frame, and the gap around
// it drawn as a dashed outline. Painted with the palette, so it follows the theme.
class GapDiagram : public QWidget
{
public:
	explicit GapDiagram(QWidget* parent) : QWidget(parent) { setFixedSize(72, 56); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QPalette pal = palette();
		const QColor ink = pal.color(QPalette::ButtonText);
		QRectF outer = QRectF(rect()).adjusted(3.5, 3.5, -3.5, -3.5);
		QRectF frame = outer.adjusted(12, 10, -12, -10);
		QPen dash(pal.color(QPalette::Highlight), 1.5, Qt::DashLine);
		p.setPen(dash);
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(outer, 2, 2);
		QColor fillC = ink;
		fillC.setAlphaF(0.12);
		p.setBrush(fillC);
		p.setPen(QPen(ink, 2));
		p.drawRect(frame);
		// three "text lines" inside the frame
		p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
		for (int i = 0; i < 3; ++i)
		{
			const double y = frame.top() + frame.height() * (i + 1) / 4.0;
			p.drawLine(QPointF(frame.left() + 5, y), QPointF(frame.right() - (i == 2 ? 14 : 5), y));
		}
	}
};
} // namespace

QIcon suneerGapGlyphIcon(char sign, char arrow)
{
	return QIcon(new SuneerGapGlyphEngine(sign, arrow));
}

void suneerSetGapGlyph(QToolButton* btn, char sign, char arrow)
{
	btn->setText(QString());
	btn->setIcon(suneerGapGlyphIcon(sign, arrow));
	btn->setIconSize(sign == 's' ? QSize(26, 18) : arrow ? QSize(20, 18) : QSize(18, 18));
	btn->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));
}

// ---- popup -----------------------------------------------------------------

static const char* s_sideName[4] = { "top", "bottom", "left", "right" };

SuneerGapPopup::SuneerGapPopup(QWidget* parent)
	: QWidget(parent, Qt::Popup)
{
	setAutoFillBackground(true);
	setWindowTitle(tr("Gap Sides"));
	auto* vlay = new QVBoxLayout(this);
	vlay->setContentsMargins(8, 8, 8, 8);
	vlay->setSpacing(6);

	m_title = new QLabel(this);
	QFont tf = m_title->font();
	tf.setBold(true);
	m_title->setFont(tf);
	vlay->addWidget(m_title);

	// One cluster per side: [-] [value] [+], placed next to that side of the diagram.
	auto* grid = new QGridLayout();
	grid->setHorizontalSpacing(4);
	grid->setVerticalSpacing(4);
	for (int s = 0; s < 4; ++s)
	{
		const QString name = QString::fromLatin1(s_sideName[s]);
		auto* cluster = new QWidget(this);
		auto* h = new QHBoxLayout(cluster);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(2);
		m_minus[s] = new QToolButton(cluster);
		m_minus[s]->setFixedSize(24, 22);
		suneerSetGapGlyph(m_minus[s], '-');
		m_minus[s]->setToolTip(tr("Decrease %1 gap").arg(name));
		m_value[s] = new QDoubleSpinBox(cluster);
		m_value[s]->setRange(0.0, 500.0);
		m_value[s]->setDecimals(2);
		m_value[s]->setSingleStep(0.5);
		m_value[s]->setSuffix(QStringLiteral(" mm"));
		m_value[s]->setButtonSymbols(QAbstractSpinBox::NoButtons);
		m_value[s]->setKeyboardTracking(false);
		m_value[s]->setAlignment(Qt::AlignRight);
		m_value[s]->setFixedWidth(68);
		m_value[s]->setToolTip(tr("%1 gap (mm). Type a value and press Enter.").arg(name.left(1).toUpper() + name.mid(1)));
		m_plus[s] = new QToolButton(cluster);
		m_plus[s]->setFixedSize(24, 22);
		suneerSetGapGlyph(m_plus[s], '+');
		m_plus[s]->setToolTip(tr("Increase %1 gap").arg(name));
		h->addWidget(m_minus[s]);
		h->addWidget(m_value[s]);
		h->addWidget(m_plus[s]);
		connect(m_minus[s], &QToolButton::clicked, this, [this, s] { onStep(s, -1.0); });
		connect(m_plus[s], &QToolButton::clicked, this, [this, s] { onStep(s, +1.0); });
		// Only Enter applies a typed value: focus loss (closing the panel, Esc,
		// a click elsewhere) must not, or a linked panel would copy one side's
		// shown value onto all four.
		m_value[s]->installEventFilter(this);
		static const int row[4] = { 0, 2, 1, 1 };
		static const int col[4] = { 1, 1, 0, 2 };
		grid->addWidget(cluster, row[s], col[s], s < 2 ? Qt::AlignHCenter : (s == Left ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter));
	}
	grid->addWidget(new GapDiagram(this), 1, 1, Qt::AlignCenter);
	vlay->addLayout(grid);

	auto* bottom = new QHBoxLayout();
	bottom->setSpacing(6);
	m_link = new LinkButton(this);
	m_link->setCheckable(true);
	m_link->setToolTip(tr("Link all sides: a click or a typed value changes all four sides"));
	bottom->addWidget(m_link);
	auto* linkLbl = new QLabel(tr("Link all sides"), this);
	bottom->addWidget(linkLbl);
	bottom->addSpacing(10);
	bottom->addWidget(new QLabel(tr("Step:"), this));
	m_step = new QDoubleSpinBox(this);
	m_step->setRange(0.05, 20.0);
	m_step->setDecimals(2);
	m_step->setSingleStep(0.1);
	m_step->setSuffix(QStringLiteral(" mm"));
	m_step->setFixedWidth(80);
	m_step->setToolTip(tr("How much one - / + click in this panel changes the gap"));
	bottom->addWidget(m_step);
	bottom->addStretch(1);
	m_reset = new QToolButton(this);
	m_reset->setText(tr("Reset"));
	m_reset->setToolTip(tr("Reset Gap to Zero (all sides)"));
	bottom->addWidget(m_reset);
	vlay->addLayout(bottom);

	connect(m_step, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (m_refreshing || m_stepKey.isEmpty())
			return;
		QSettings st(QStringLiteral("Scribus"), QStringLiteral("SuneerGap"));
		st.setValue(QStringLiteral("step_") + m_stepKey, v);
	});
	connect(m_reset, &QToolButton::clicked, this, [this] {
		if (reset)
			reset();
		refresh();
	});
	connect(m_link, &QToolButton::toggled, this, [this](bool on) {
		QSettings st(QStringLiteral("Scribus"), QStringLiteral("SuneerGap"));
		st.setValue(QStringLiteral("linked"), on);
	});
	{
		QSettings st(QStringLiteral("Scribus"), QStringLiteral("SuneerGap"));
		m_link->setChecked(st.value(QStringLiteral("linked"), false).toBool());
	}
}

void SuneerGapPopup::setStepKey(const QString& key, double defaultMm)
{
	m_stepKey = key;
	QSettings st(QStringLiteral("Scribus"), QStringLiteral("SuneerGap"));
	m_refreshing = true;
	m_step->setValue(st.value(QStringLiteral("step_") + key, defaultMm).toDouble());
	m_refreshing = false;
}

bool SuneerGapPopup::linked() const
{
	return m_link && m_link->isChecked();
}

void SuneerGapPopup::refresh()
{
	Values v;
	if (readValues)
		v = readValues();
	m_refreshing = true;
	m_title->setText(v.title);
	for (int s = 0; s < 4; ++s)
	{
		m_value[s]->setEnabled(v.valid && v.editable);
		m_value[s]->setValue(v.valid ? v.mm[s] : 0.0);
		m_minus[s]->setEnabled(v.valid);
		m_plus[s]->setEnabled(v.valid);
	}
	m_reset->setEnabled(v.valid);
	m_refreshing = false;
}

void SuneerGapPopup::onStep(int side, double sign)
{
	if (!stepSide)
		return;
	stepSide(linked() ? All : side, sign * m_step->value());
	refresh();
}

void SuneerGapPopup::onTyped(int side)
{
	if (m_refreshing || !setSide)
		return;
	// Enter on an unchanged value is no step
	Values v;
	if (readValues)
		v = readValues();
	if (!v.valid || !v.editable)
		return;
	const double mm = m_value[side]->value();
	if (linked())
	{
		bool same = true;
		for (int s = 0; s < 4; ++s)
			same = same && qAbs(v.mm[s] - mm) < 0.005;
		if (!same)
			setSide(All, mm);
	}
	else if (qAbs(v.mm[side] - mm) >= 0.005)
		setSide(side, mm);
	refresh();
}

bool SuneerGapPopup::eventFilter(QObject* obj, QEvent* e)
{
	if (e->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent*>(e)->key();
		if (key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			for (int s = 0; s < 4; ++s)
			{
				if (obj == m_value[s])
				{
					m_value[s]->interpretText();
					onTyped(s);
					m_value[s]->selectAll();
					return true;
				}
			}
		}
	}
	return QWidget::eventFilter(obj, e);
}

void SuneerGapPopup::keyPressEvent(QKeyEvent* e)
{
	// Enter is taken by the value fields (eventFilter); it never closes the panel.
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
	{
		e->accept();
		return;
	}
	QWidget::keyPressEvent(e);
}

void SuneerGapPopup::paintEvent(QPaintEvent* e)
{
	QWidget::paintEvent(e);
	// a Qt::Popup has no window frame: a 1 px border keeps it apart from the canvas
	QPainter p(this);
	p.setPen(palette().color(QPalette::Mid));
	p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void SuneerGapPopup::popupBelow(QWidget* anchor)
{
	refresh();
	adjustSize();
	QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height()));
	if (QScreen* scr = anchor->screen())
	{
		const QRect avail = scr->availableGeometry();
		if (pos.x() + width() > avail.right())
			pos.setX(qMax(avail.left(), avail.right() - width()));
		if (pos.y() + height() > avail.bottom())
			pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height());
	}
	move(pos);
	show();
	m_value[Top]->setFocus();
	m_value[Top]->selectAll();
}
