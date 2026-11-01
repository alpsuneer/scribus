/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/

#include "suneertextcolour.h"

#include <cmath>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "appmodes.h"
#include "colorcombo.h"
#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_table.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "sccolor.h"
#include "sccolorengine.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "styles/charstyle.h"
#include "suneertable.h"
#include "ui/colorlistbox.h"
#include "ui/scrspinbox.h"
#include "undogui.h"
#include "undomanager.h"
#include "undostate.h"
#include "units.h"

// ───────────────────────────── presets ─────────────────────────────

namespace SuneerGradientPresets
{
	namespace
	{
		Stop st(double pos, const char* colour, int c, int m, int y, int k)
		{
			Stop s;
			s.pos = pos;
			s.color = QString::fromLatin1(colour);
			s.c = c; s.m = m; s.y = y; s.k = k;
			return s;
		}
		Preset pr(const char* name, double angle, int type, const QList<Stop>& stops)
		{
			Preset p;
			p.name = QString::fromLatin1(name);
			p.angle = angle;
			p.type = type;
			p.stops = stops;
			p.builtIn = true;
			return p;
		}

		QJsonObject stopToJson(const Stop& s)
		{
			return QJsonObject{ { "pos", s.pos }, { "color", s.color }, { "c", s.c }, { "m", s.m }, { "y", s.y }, { "k", s.k },
			                    { "shade", s.shade }, { "opacity", s.opacity } };
		}
		Stop stopFromJson(const QJsonObject& o)
		{
			Stop s;
			s.pos = o.value("pos").toDouble();
			s.color = o.value("color").toString();
			s.c = o.value("c").toInt(); s.m = o.value("m").toInt(); s.y = o.value("y").toInt(); s.k = o.value("k").toInt();
			s.shade = o.value("shade").toInt(100);
			s.opacity = o.value("opacity").toDouble(1.0);
			return s;
		}
		QJsonObject presetToJson(const Preset& p)
		{
			QJsonArray stops;
			for (const Stop& s : p.stops)
				stops << stopToJson(s);
			return QJsonObject{ { "name", p.name }, { "type", p.type }, { "angle", p.angle }, { "stops", stops } };
		}
		Preset presetFromJson(const QJsonObject& o)
		{
			Preset p;
			p.name = o.value("name").toString();
			p.type = o.value("type").toInt(0);
			p.angle = o.value("angle").toDouble(90.0);
			for (const QJsonValue& v : o.value("stops").toArray())
				p.stops << stopFromJson(v.toObject());
			return p;
		}

		struct Store
		{
			QList<Preset> saved;
			QStringList hidden;
		};

		Store load()
		{
			Store s;
			QFile f(storagePath());
			if (f.open(QIODevice::ReadOnly))
			{
				const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
				for (const QJsonValue& v : root.value("presets").toArray())
				{
					Preset p = presetFromJson(v.toObject());
					if (!p.name.isEmpty() && p.stops.count() >= 2)
						s.saved << p;
				}
				for (const QJsonValue& v : root.value("hidden").toArray())
					s.hidden << v.toString();
				return s;
			}
			// first run: the presets saved by the earlier panel (prefs context)
			PrefsFile* pf = PrefsManager::instance().prefsFile;
			PrefsContext* ctx = pf ? pf->getContext("suneer_textgradient", false) : nullptr;
			if (ctx)
			{
				const QJsonArray arr = QJsonDocument::fromJson(ctx->get("presets", "[]").toUtf8()).array();
				for (const QJsonValue& v : arr)
				{
					Preset p = presetFromJson(v.toObject());
					if (!p.name.isEmpty() && p.stops.count() >= 2)
						s.saved << p;
				}
			}
			return s;
		}

		bool write(const Store& s)
		{
			QJsonArray presets;
			for (const Preset& p : s.saved)
				presets << presetToJson(p);
			QJsonArray hidden;
			for (const QString& h : s.hidden)
				hidden << h;
			QFile f(storagePath());
			QDir().mkpath(QFileInfo(f).absolutePath());
			if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return false;
			f.write(QJsonDocument(QJsonObject{ { "presets", presets }, { "hidden", hidden } }).toJson(QJsonDocument::Indented));
			return true;
		}
	}

	QString storagePath()
	{
		return PrefsManager::instance().preferencesLocation() + QStringLiteral("/suneer-text-gradients.json");
	}

	QList<Preset> builtIn()
	{
		// CMYK 0..255. Dark at the top, the colour itself at the bottom (angle 90) for the headlines.
		return {
			pr("Gradient", 45, 0, { st(0.0, "Grad Red", 0, 217, 179, 0), st(0.5, "Grad Pink", 0, 179, 0, 0), st(1.0, "Grad Blue", 217, 153, 0, 0) }),
			pr("Red headline", 90, 0, { st(0.0, "Headline Dark Red", 0, 255, 255, 115), st(1.0, "Headline Red", 0, 255, 255, 0) }),
			pr("Grey headline", 90, 0, { st(0.0, "Headline Black", 0, 0, 0, 255), st(1.0, "Headline Grey", 0, 0, 0, 128) }),
			pr("Blue", 90, 0, { st(0.0, "Grad Dark Blue", 255, 204, 0, 77), st(1.0, "Grad Sky Blue", 204, 77, 0, 0) }),
			pr("Gold", 90, 0, { st(0.0, "Grad Dark Gold", 0, 77, 230, 77), st(1.0, "Grad Gold", 0, 38, 217, 0) }),
			pr("Purple orange", 0, 0, { st(0.0, "Grad Purple", 179, 230, 0, 0), st(1.0, "Grad Orange", 0, 153, 255, 0) }),
			pr("Green", 90, 0, { st(0.0, "Grad Dark Green", 230, 51, 255, 77), st(1.0, "Grad Green", 153, 0, 255, 0) }),
			pr("Teal", 90, 0, { st(0.0, "Grad Dark Teal", 255, 51, 102, 77), st(1.0, "Grad Teal", 179, 0, 77, 0) }),
			pr("Sunset", 90, 0, { st(0.0, "Grad Deep Orange", 0, 204, 230, 0), st(1.0, "Grad Yellow", 0, 26, 230, 0) }),
			pr("Fire", 270, 0, { st(0.0, "Grad Dark Red", 0, 255, 255, 77), st(0.55, "Grad Orange", 0, 153, 255, 0), st(1.0, "Grad Yellow", 0, 26, 230, 0) }),
			pr("Silver", 90, 0, { st(0.0, "Headline Grey", 0, 0, 0, 128), st(0.5, "Grad Light Grey", 0, 0, 0, 51), st(1.0, "Headline Dark Grey", 0, 0, 0, 217) }),
			pr("Ocean", 45, 1, { st(0.0, "Grad Ice", 51, 0, 0, 0), st(0.5, "Grad Sky", 153, 0, 26, 0), st(1.0, "Grad Ocean Blue", 255, 128, 0, 0) }),
		};
	}

	QList<Preset> all()
	{
		const Store s = load();
		QList<Preset> out;
		for (Preset p : builtIn())
		{
			if (s.hidden.contains(p.name))
				continue;
			for (const Preset& q : s.saved)
				if (q.name == p.name)
				{
					p.type = q.type;
					p.angle = q.angle;
					p.stops = q.stops;
				}
			out << p;
		}
		for (const Preset& q : s.saved)
		{
			bool isBuiltIn = false;
			for (const Preset& p : out)
				if (p.name == q.name)
					isBuiltIn = true;
			if (!isBuiltIn && !s.hidden.contains(q.name))
				out << q;
		}
		return out;
	}

	bool find(const QString& name, Preset& out)
	{
		for (const Preset& p : all())
			if (p.name == name)
			{
				out = p;
				return true;
			}
		return false;
	}

	bool save(const Preset& preset)
	{
		if (preset.name.isEmpty() || preset.stops.count() < 2)
			return false;
		Store s = load();
		s.hidden.removeAll(preset.name);
		for (int i = s.saved.count() - 1; i >= 0; --i)
			if (s.saved[i].name == preset.name)
				s.saved.removeAt(i);
		Preset p = preset;
		p.builtIn = false;
		s.saved << p;
		return write(s);
	}

	bool remove(const QString& name)
	{
		Store s = load();
		for (int i = s.saved.count() - 1; i >= 0; --i)
			if (s.saved[i].name == name)
				s.saved.removeAt(i);
		for (const Preset& p : builtIn())
			if (p.name == name && !s.hidden.contains(name))
				s.hidden << name;
		return write(s);
	}

	bool rename(const QString& oldName, const QString& newName)
	{
		if (newName.isEmpty() || oldName == newName)
			return false;
		Preset p;
		if (!find(oldName, p))
			return false;
		p.name = newName;
		return remove(oldName) && save(p);
	}

	Preset fromDocument(ScribusDoc* doc, const QString& name, int type, double angle)
	{
		Preset p;
		if (!doc || !doc->docGradients.contains(name))
			return p;
		p.name = name;
		p.type = type;
		p.angle = angle;
		const VGradient& g = doc->docGradients[name];
		for (const VColorStop* s : g.colorStops())
		{
			Stop o;
			o.pos = s->rampPoint;
			o.color = s->name;
			o.shade = s->shade;
			o.opacity = s->opacity;
			if (doc->PageColors.contains(s->name))
			{
				const ScColor& col = doc->PageColors[s->name];
				if (col.getColorModel() == colorModelCMYK)
					col.getCMYK(&o.c, &o.m, &o.y, &o.k);
				else
				{
					CMYKColor cmyk;
					ScColorEngine::getCMYKValues(col, doc, cmyk);
					cmyk.getValues(o.c, o.m, o.y, o.k);
				}
			}
			p.stops << o;
		}
		return p;
	}

	bool ensureGradient(ScribusDoc* doc, const QString& name)
	{
		if (!doc || name.isEmpty())
			return false;
		if (doc->docGradients.contains(name))
			return true;
		Preset p;
		if (!find(name, p))
			return false;
		VGradient g = toGradient(p, doc);
		doc->docGradients.insert(name, g);
		doc->changed();
		if (doc->scMW())
			doc->scMW()->updateColorLists();
		return true;
	}

	VGradient toGradient(const Preset& p, ScribusDoc* doc)
	{
		VGradient g(VGradient::linear);
		g.clearStops();
		for (const Stop& s : p.stops)
		{
			QColor q;
			if (doc)
			{
				if (!doc->PageColors.contains(s.color))
					doc->PageColors.insert(s.color, ScColor(s.c, s.m, s.y, s.k));
				q = ScColorEngine::getShadeColorProof(doc->PageColors[s.color], doc, s.shade);
			}
			else
			{
				ScColor c(s.c, s.m, s.y, s.k);
				q = c.getRawRGBColor();
				const double f = s.shade / 100.0;
				q = QColor(255 - (255 - q.red()) * f, 255 - (255 - q.green()) * f, 255 - (255 - q.blue()) * f);
			}
			q.setAlphaF(s.opacity);
			g.addStop(q, s.pos, 0.5, s.opacity, s.color, s.shade);
		}
		return g;
	}

	QPixmap thumbnail(const Preset& p, const QSize& size, ScribusDoc* doc, bool selected)
	{
		const qreal dpr = qApp->devicePixelRatio();
		QPixmap pm(size * dpr);
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);
		QPainter pt(&pm);
		pt.setRenderHint(QPainter::Antialiasing);
		const VGradient g = toGradient(p, doc);
		QRectF r(0.5, 0.5, size.width() - 1, size.height() - 1);
		QBrush brush;
		if (p.type == 1)
		{
			QRadialGradient rg(r.center(), qMax(r.width(), r.height()) / 2.0);
			for (const VColorStop* s : g.colorStops())
				rg.setColorAt(s->rampPoint, s->color);
			brush = rg;
		}
		else
		{
			const double a = p.angle * M_PI / 180.0;
			const QPointF d(cos(a), sin(a));
			const double half = fabs(r.width() / 2.0 * d.x()) + fabs(r.height() / 2.0 * d.y());
			QLinearGradient lg(r.center() - d * half, r.center() + d * half);
			for (const VColorStop* s : g.colorStops())
				lg.setColorAt(s->rampPoint, s->color);
			brush = lg;
		}
		pt.setPen(selected ? QPen(QColor(30, 110, 230), 2.0) : QPen(QColor(0, 0, 0, 60), 1.0));
		pt.setBrush(brush);
		pt.drawRoundedRect(r.adjusted(selected ? 0.5 : 0, selected ? 0.5 : 0, selected ? -0.5 : 0, selected ? -0.5 : 0), 5, 5);
		return pm;
	}
}

// ───────────────────────────── colour bar ─────────────────────────────

namespace
{
	const int kStopRadius = 7;
	const int kBarHeight = 18;
	const double kMinGap = 0.02;
}

SuneerGradientBar::SuneerGradientBar(QWidget* parent)
	: QWidget(parent)
{
	setMouseTracking(false);
	setCursor(Qt::PointingHandCursor);
}

QSize SuneerGradientBar::sizeHint() const
{
	return QSize(240, kBarHeight + kStopRadius * 2 + 6);
}

void SuneerGradientBar::setGradient(const VGradient& g)
{
	m_gradient = g;
	m_active = qBound(0, m_active, qMax(0, m_gradient.stops() - 1));
	update();
}

void SuneerGradientBar::setActiveStop(int i)
{
	m_active = qBound(0, i, qMax(0, m_gradient.stops() - 1));
	update();
}

double SuneerGradientBar::stopPosition(int i) const
{
	if (i < 0 || i >= m_gradient.stops())
		return 0.0;
	return m_gradient.colorStops().at(i)->rampPoint;
}

QRect SuneerGradientBar::barRect() const
{
	return QRect(kStopRadius + 1, 2, width() - 2 * kStopRadius - 2, kBarHeight);
}

int SuneerGradientBar::xOf(double pos) const
{
	const QRect b = barRect();
	return b.left() + qRound(pos * b.width());
}

double SuneerGradientBar::posOf(int x) const
{
	const QRect b = barRect();
	return qBound(0.0, double(x - b.left()) / double(qMax(1, b.width())), 1.0);
}

int SuneerGradientBar::stopAt(const QPoint& p) const
{
	const int cy = barRect().bottom() + kStopRadius + 1;
	int best = -1;
	int bestD = kStopRadius * kStopRadius + 16;
	for (int i = 0; i < m_gradient.stops(); ++i)
	{
		const int dx = p.x() - xOf(stopPosition(i));
		const int dy = p.y() - cy;
		const int d = dx * dx + dy * dy;
		if (d < bestD)
		{
			bestD = d;
			best = i;
		}
	}
	return best;
}

void SuneerGradientBar::moveStop(int index, double pos)
{
	if (index < 0 || index >= m_gradient.stops())
		return;
	const QList<VColorStop*>& stops = m_gradient.colorStops();
	double lo = 0.0, hi = 1.0;
	if (index > 0)
		lo = stops.at(index - 1)->rampPoint + kMinGap;
	if (index < stops.count() - 1)
		hi = stops.at(index + 1)->rampPoint - kMinGap;
	if (lo > hi)
		return;
	stops.at(index)->rampPoint = qBound(lo, pos, hi);
}

void SuneerGradientBar::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRect b = barRect();
	// checkerboard for transparent stops
	p.save();
	p.setClipRect(b);
	for (int y = b.top(); y < b.bottom(); y += 6)
		for (int x = b.left(); x < b.right(); x += 6)
			p.fillRect(QRect(x, y, 6, 6), ((x / 6 + y / 6) % 2) ? QColor(200, 200, 200) : QColor(240, 240, 240));
	p.restore();
	QLinearGradient lg(b.topLeft(), b.topRight());
	for (const VColorStop* s : m_gradient.colorStops())
	{
		QColor c = s->color;
		c.setAlphaF(s->opacity);
		lg.setColorAt(s->rampPoint, c);
	}
	p.setPen(QPen(QColor(0, 0, 0, 90), 1));
	p.setBrush(lg);
	p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
	// stops
	const int cy = b.bottom() + kStopRadius + 1;
	for (int i = 0; i < m_gradient.stops(); ++i)
	{
		const VColorStop* s = m_gradient.colorStops().at(i);
		const int cx = xOf(s->rampPoint);
		const bool active = (i == m_active);
		p.setPen(QPen(active ? QColor(30, 110, 230) : QColor(255, 255, 255), active ? 2.5 : 2.0));
		QColor fill = s->color;
		fill.setAlphaF(1.0);
		p.setBrush(fill);
		p.drawEllipse(QPointF(cx, cy), kStopRadius, kStopRadius);
		if (active)
		{
			p.setPen(QPen(QColor(0, 0, 0, 120), 1));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QPointF(cx, cy), kStopRadius + 2.0, kStopRadius + 2.0);
		}
	}
}

void SuneerGradientBar::mousePressEvent(QMouseEvent* e)
{
	if (e->button() != Qt::LeftButton)
		return;
	const int i = stopAt(e->pos());
	if (i >= 0)
	{
		m_active = i;
		m_dragging = i;
		m_moved = false;
		update();
		emit stopSelected(i);
	}
}

void SuneerGradientBar::mouseMoveEvent(QMouseEvent* e)
{
	if (m_dragging < 0)
		return;
	moveStop(m_dragging, posOf(e->pos().x()));
	m_moved = true;
	update();
	emit stopMoving();
}

void SuneerGradientBar::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() != Qt::LeftButton || m_dragging < 0)
		return;
	const bool moved = m_moved;
	m_dragging = -1;
	m_moved = false;
	if (moved)
		emit stopsChanged();
}

void SuneerGradientBar::mouseDoubleClickEvent(QMouseEvent* e)
{
	if (e->button() != Qt::LeftButton || !barRect().contains(e->pos()) || m_gradient.stops() >= 4)
		return;
	const double pos = posOf(e->pos().x());
	// colour of the ramp at that point: the neighbour before it
	const QList<VColorStop*>& stops = m_gradient.colorStops();
	const VColorStop* ref = stops.first();
	for (const VColorStop* s : stops)
		if (s->rampPoint <= pos)
			ref = s;
	m_gradient.addStop(ref->color, pos, 0.5, ref->opacity, ref->name, ref->shade);
	for (int i = 0; i < m_gradient.stops(); ++i)
		if (qFuzzyCompare(m_gradient.colorStops().at(i)->rampPoint, qBound(0.0, pos, 1.0)))
			m_active = i;
	update();
	emit stopSelected(m_active);
	emit stopsChanged();
}

// ───────────────────────────── preview strip ─────────────────────────────

SuneerTextPreview::SuneerTextPreview(QWidget* parent)
	: QWidget(parent)
{
	m_text = QString::fromUtf8("മിസ്റ്റർ ട്രംപ്");
}

QSize SuneerTextPreview::sizeHint() const
{
	return QSize(240, 58);
}

void SuneerTextPreview::setText(const QString& text)
{
	const QString t = text.simplified();
	m_text = t.isEmpty() ? QString::fromUtf8("മിസ്റ്റർ ട്രംപ്") : t.left(24);
	update();
}

void SuneerTextPreview::setFontFamily(const QString& family, bool bold)
{
	m_family = family;
	m_bold = bold;
	update();
}

void SuneerTextPreview::setSolid(const QColor& c)
{
	m_isGradient = false;
	m_solid = c;
	update();
}

void SuneerTextPreview::setGradient(const VGradient& g, int type, double angle, bool perLine)
{
	m_isGradient = true;
	m_gradient = g;
	m_type = type;
	m_angle = angle;
	m_perLine = perLine;
	update();
}

void SuneerTextPreview::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(QColor(0, 0, 0, 50), 1));
	p.setBrush(Qt::white);
	p.drawRoundedRect(r, 5, 5);

	QFont f;
	if (!m_family.isEmpty())
		f.setFamily(m_family);
	f.setBold(m_bold);
	f.setPixelSize(qMax(12, int(height() * 0.5)));
	QPainterPath path;
	path.addText(0, 0, f, m_text);
	QRectF br = path.boundingRect();
	if (br.isEmpty())
		return;
	// fit: shrink when the text is wider than the strip
	const double avail = r.width() - 16;
	double scale = 1.0;
	if (br.width() > avail)
		scale = avail / br.width();
	QTransform tr;
	tr.translate(r.center().x(), r.center().y());
	tr.scale(scale, scale);
	tr.translate(-br.center().x(), -br.center().y());
	path = tr.map(path);
	br = path.boundingRect();

	QBrush brush(m_solid);
	if (m_isGradient && !m_gradient.isEmpty())
	{
		if (m_type == 1)
		{
			QRadialGradient rg(br.center(), QLineF(br.center(), br.bottomRight()).length());
			for (const VColorStop* s : m_gradient.colorStops())
			{
				QColor c = s->color;
				c.setAlphaF(s->opacity);
				rg.setColorAt(s->rampPoint, c);
			}
			brush = rg;
		}
		else
		{
			const double a = m_angle * M_PI / 180.0;
			const QPointF d(cos(a), sin(a));
			const double half = fabs(br.width() / 2.0 * d.x()) + fabs(br.height() / 2.0 * d.y());
			QLinearGradient lg(br.center() - d * half, br.center() + d * half);
			for (const VColorStop* s : m_gradient.colorStops())
			{
				QColor c = s->color;
				c.setAlphaF(s->opacity);
				lg.setColorAt(s->rampPoint, c);
			}
			brush = lg;
		}
	}
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawPath(path);
}

// ───────────────────────────── gradient undo ─────────────────────────────

namespace
{
	class GradientState : public SimpleState
	{
	public:
		GradientState(const QString& name, const VGradient* oldG, const VGradient& newG)
			: SimpleState(QObject::tr("Text gradient"), name), gradientName(name), existed(oldG != nullptr), newGradient(newG)
		{
			if (oldG)
				oldGradient = *oldG;
		}
		QString gradientName;
		bool existed;
		VGradient oldGradient;
		VGradient newGradient;
	};
}

SuneerGradientUndo::SuneerGradientUndo(QObject* parent)
	: QObject(parent), UndoObject(QObject::tr("Text gradient"))
{
}

QString SuneerGradientUndo::getUName() const
{
	return QObject::tr("Text gradient");
}

void SuneerGradientUndo::record(const QString& name, const VGradient* oldGradient, const VGradient& newGradient)
{
	if (!UndoManager::undoEnabled())
		return;
	UndoManager::instance()->action(this, new GradientState(name, oldGradient, newGradient));
}

void SuneerGradientUndo::restore(UndoState* state, bool isUndo)
{
	GradientState* gs = dynamic_cast<GradientState*>(state);
	if (!gs || !m_doc)
		return;
	if (isUndo)
	{
		if (gs->existed)
			m_doc->docGradients.insert(gs->gradientName, gs->oldGradient);
		else
			m_doc->docGradients.remove(gs->gradientName);
	}
	else
		m_doc->docGradients.insert(gs->gradientName, gs->newGradient);
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_doc->scMW())
		m_doc->scMW()->updateColorLists();
	emit restored();
}

// ───────────────────────────── the panel ─────────────────────────────

namespace
{
	struct Dir { const char* glyph; int angle; const char* tip; };
	const Dir kDirs[] = {
		{ "→", 0, "Left to right" }, { "↘", 45, "Top left to bottom right" }, { "↓", 90, "Top to bottom" },
		{ "↙", 135, "Top right to bottom left" }, { "←", 180, "Right to left" }, { "↖", 225, "Bottom right to top left" },
		{ "↑", 270, "Bottom to top" }, { "↗", 315, "Bottom left to top right" },
	};
	const QSize kThumb(40, 28);

	QToolButton* flatButton(const QString& text, QWidget* parent, bool checkable)
	{
		auto* b = new QToolButton(parent);
		b->setText(text);
		b->setCheckable(checkable);
		b->setAutoRaise(false);
		b->setToolButtonStyle(Qt::ToolButtonTextOnly);
		return b;
	}
}

SuneerTextColourWidget::SuneerTextColourWidget(QWidget* parent)
	: QWidget(parent)
{
	m_undo = new SuneerGradientUndo(this);
	connect(m_undo, &SuneerGradientUndo::restored, this, [this]() {
		if (m_doc && m_doc->docGradients.contains(m_gradName))
		{
			m_bar->setGradient(m_doc->docGradients[m_gradName]);
			updateStopEditor();
			updatePreview();
		}
	});
	buildUi();
}

void SuneerTextColourWidget::buildUi()
{
	setObjectName("suneerTextColour");
	auto* top = new QVBoxLayout(this);
	top->setContentsMargins(6, 6, 6, 6);
	top->setSpacing(5);

	// 1. Solid | Gradient
	auto* modeRow = new QHBoxLayout();
	modeRow->setSpacing(0);
	m_solidBtn = flatButton(tr("Solid"), this, true);
	m_gradientBtn = flatButton(tr("Gradient"), this, true);
	m_solidBtn->setObjectName("suneerTextSolid");
	m_gradientBtn->setObjectName("suneerTextGradient");
	for (QToolButton* b : { m_solidBtn, m_gradientBtn })
	{
		b->setMinimumHeight(26);
		b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}
	auto* modeGroup = new QButtonGroup(this);
	modeGroup->setExclusive(true);
	modeGroup->addButton(m_solidBtn, 0);
	modeGroup->addButton(m_gradientBtn, 1);
	m_solidBtn->setChecked(true);
	modeRow->addWidget(m_solidBtn);
	modeRow->addWidget(m_gradientBtn);
	top->addLayout(modeRow);
	connect(m_solidBtn, &QToolButton::clicked, this, [this]() { setModeGradient(false, true); });
	connect(m_gradientBtn, &QToolButton::clicked, this, [this]() { setModeGradient(true, true); });

	// 3. preview
	m_preview = new SuneerTextPreview(this);
	top->addWidget(m_preview);

	// Apply to: which text changes, on the Solid and the Gradient page alike
	{
		auto* targetRow = new QHBoxLayout();
		targetRow->setSpacing(0);
		targetRow->addWidget(new QLabel(tr("Apply to:"), this));
		auto* targetGroup = new QButtonGroup(this);
		targetGroup->setExclusive(true);
		const QStringList names { tr("Selected text"), tr("This paragraph"), tr("Whole text") };
		const QStringList tips { tr("Only the selected characters"), tr("The paragraph the cursor is in"), tr("All the text of the frame") };
		for (int i = 0; i < 3; ++i)
		{
			QToolButton* b = flatButton(names[i], this, true);
			b->setToolTip(tips[i]);
			b->setMinimumHeight(24);
			b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			targetGroup->addButton(b, i);
			targetRow->addWidget(b);
			m_targetButtons << b;
			connect(b, &QToolButton::clicked, this, [this, i]() { targetClicked(i); });
		}
		top->addLayout(targetRow);
		m_targetNote = new QLabel(this);
		m_targetNote->setWordWrap(true);
		top->addWidget(m_targetNote);
	}

	m_pages = new QStackedWidget(this);
	top->addWidget(m_pages);

	// solid page
	{
		auto* page = new QWidget(m_pages);
		auto* l = new QVBoxLayout(page);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(4);
		m_solidList = new ColorListBox(ColorListBox::fancyPixmap, page);
		m_solidList->setObjectName("suneerTextSolidList");
		m_solidList->setMinimumHeight(120);
		m_solidList->setMaximumHeight(170);
		l->addWidget(m_solidList);
		auto* row = new QHBoxLayout();
		row->addWidget(new QLabel(tr("Shade:"), page));
		m_solidShadeSpin = new QSpinBox(page);
		m_solidShadeSpin->setRange(0, 100);
		m_solidShadeSpin->setValue(100);
		m_solidShadeSpin->setSuffix(QStringLiteral(" %"));
		row->addWidget(m_solidShadeSpin);
		row->addStretch(1);
		l->addLayout(row);
		m_pages->addWidget(page);
		connect(m_solidList, &ColorListBox::itemClicked, this, [this](int) { solidColourPicked(); });
		connect(m_solidShadeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { solidColourPicked(); });
	}

	// gradient page
	{
		auto* page = new QWidget(m_pages);
		auto* l = new QVBoxLayout(page);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(5);

		// 2. presets
		m_presetGrid = new QWidget(page);
		m_presetLayout = new QGridLayout(m_presetGrid);
		m_presetLayout->setContentsMargins(0, 0, 0, 0);
		m_presetLayout->setSpacing(3);
		l->addWidget(m_presetGrid);

		// 4. colour bar + stop buttons
		m_bar = new SuneerGradientBar(page);
		m_bar->setObjectName("suneerGradientBar");
		l->addWidget(m_bar);
		auto* stopRow = new QHBoxLayout();
		stopRow->setSpacing(3);
		m_addStop = new QPushButton(tr("+ Add colour"), page);
		m_swapStops = new QPushButton(QString::fromUtf8("⇄ Swap"), page);
		m_removeStop = new QPushButton(QString::fromUtf8("− Remove"), page);
		m_addStop->setToolTip(tr("Add a colour stop after the selected one (up to 4)"));
		m_swapStops->setToolTip(tr("Reverse the colours"));
		m_removeStop->setToolTip(tr("Remove the selected colour stop (at least 2 stay)"));
		for (QPushButton* b : { m_addStop, m_swapStops, m_removeStop })
		{
			b->setFlat(false);
			stopRow->addWidget(b);
		}
		l->addLayout(stopRow);
		connect(m_bar, &SuneerGradientBar::stopSelected, this, [this](int i) { stopSelected(i); });
		connect(m_bar, &SuneerGradientBar::stopsChanged, this, [this]() { stopsEdited(true); });
		connect(m_bar, &SuneerGradientBar::stopMoving, this, [this]() { updatePreview(); });
		connect(m_addStop, &QPushButton::clicked, this, [this]() { addStop(); });
		connect(m_swapStops, &QPushButton::clicked, this, [this]() { swapStops(); });
		connect(m_removeStop, &QPushButton::clicked, this, [this]() { removeStop(); });

		// the selected stop's colour: document colours + a new CMYK colour
		m_stopEditor = new QWidget(page);
		auto* se = new QVBoxLayout(m_stopEditor);
		se->setContentsMargins(0, 0, 0, 0);
		se->setSpacing(3);
		m_stopLabel = new QLabel(tr("Colour of stop 1:"), m_stopEditor);
		se->addWidget(m_stopLabel);
		m_stopList = new ColorListBox(ColorListBox::fancyPixmap, m_stopEditor);
		m_stopList->setObjectName("suneerStopColourList");
		m_stopList->setMinimumHeight(96);
		m_stopList->setMaximumHeight(120);
		se->addWidget(m_stopList);
		auto* cmykRow = new QHBoxLayout();
		cmykRow->setSpacing(2);
		cmykRow->addWidget(new QLabel(tr("New CMYK:"), m_stopEditor));
		const char* labels[4] = { "C", "M", "Y", "K" };
		for (int i = 0; i < 4; ++i)
		{
			m_cmyk[i] = new QSpinBox(m_stopEditor);
			m_cmyk[i]->setRange(0, 100);
			m_cmyk[i]->setPrefix(QString::fromLatin1(labels[i]) + QLatin1Char(' '));
			m_cmyk[i]->setToolTip(tr("%1 in percent").arg(QString::fromLatin1(labels[i])));
			cmykRow->addWidget(m_cmyk[i]);
		}
		m_cmykAdd = new QPushButton(tr("Add"), m_stopEditor);
		m_cmykAdd->setToolTip(tr("Add this CMYK colour to the document and use it for the stop"));
		cmykRow->addWidget(m_cmykAdd);
		se->addLayout(cmykRow);
		l->addWidget(m_stopEditor);
		connect(m_stopList, &ColorListBox::itemClicked, this, [this](int) { stopColourPicked(m_stopList->currentColor()); });
		connect(m_cmykAdd, &QPushButton::clicked, this, [this]() { addCmykColour(); });

		// 5. direction
		auto* dirRow = new QHBoxLayout();
		dirRow->setSpacing(2);
		dirRow->addWidget(new QLabel(tr("Direction:"), page));
		auto* dirGroup = new QButtonGroup(this);
		dirGroup->setExclusive(true);
		for (const Dir& d : kDirs)
		{
			QToolButton* b = flatButton(QString::fromUtf8(d.glyph), page, true);
			b->setFixedSize(26, 24);
			b->setToolTip(tr(d.tip));
			b->setProperty("angle", d.angle);
			dirGroup->addButton(b);
			dirRow->addWidget(b);
			m_dirButtons << b;
			connect(b, &QToolButton::clicked, this, [this, d]() { directionClicked(d.angle, false); });
		}
		QToolButton* radial = flatButton(QString::fromUtf8("◎"), page, true);
		radial->setFixedSize(26, 24);
		radial->setToolTip(tr("Radial: from the middle outwards"));
		dirGroup->addButton(radial);
		dirRow->addWidget(radial);
		m_dirButtons << radial;
		connect(radial, &QToolButton::clicked, this, [this]() { directionClicked(m_angle, true); });
		dirRow->addStretch(1);
		l->addLayout(dirRow);

		// 6. how the gradient spreads over that text
		auto* scopeRow = new QHBoxLayout();
		scopeRow->setSpacing(0);
		scopeRow->addWidget(new QLabel(tr("Gradient:"), page));
		auto* scopeGroup = new QButtonGroup(this);
		scopeGroup->setExclusive(true);
		const QStringList scopeNames { tr("Whole"), tr("Each line") };
		const QStringList scopeTips { tr("One gradient across all the lines it is applied to"), tr("Every line gets the whole gradient (the headline look)") };
		for (int i = 0; i < 2; ++i)
		{
			QToolButton* b = flatButton(scopeNames[i], page, true);
			b->setToolTip(scopeTips[i]);
			b->setMinimumHeight(24);
			b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			scopeGroup->addButton(b, i);
			scopeRow->addWidget(b);
			m_scopeButtons << b;
			connect(b, &QToolButton::clicked, this, [this, i]() { scopeClicked(i); });
		}
		l->addLayout(scopeRow);
		m_pages->addWidget(page);
	}

	// 7. More…
	m_moreBtn = new QPushButton(tr("More…"), this);
	m_moreBtn->setFlat(true);
	m_moreBtn->setCheckable(true);
	m_moreBtn->setStyleSheet("QPushButton { text-align: left; color: palette(link); }");
	top->addWidget(m_moreBtn);
	m_more = new QWidget(this);
	auto* mg = new QGridLayout(m_more);
	mg->setContentsMargins(0, 0, 0, 0);
	mg->setHorizontalSpacing(4);
	mg->setVerticalSpacing(3);
	mg->addWidget(new QLabel(tr("Angle:"), m_more), 0, 0);
	m_angleSpin = new QSpinBox(m_more);
	m_angleSpin->setRange(0, 359);
	m_angleSpin->setWrapping(true);
	m_angleSpin->setSuffix(QStringLiteral(" °"));
	m_angleSpin->setToolTip(tr("Exact direction of a linear gradient: 0 = left to right, 90 = top to bottom"));
	mg->addWidget(m_angleSpin, 0, 1);
	mg->addWidget(new QLabel(tr("Stop shade:"), m_more), 0, 2);
	m_stopShade = new QSpinBox(m_more);
	m_stopShade->setRange(0, 100);
	m_stopShade->setSuffix(QStringLiteral(" %"));
	mg->addWidget(m_stopShade, 0, 3);
	mg->addWidget(new QLabel(tr("Stop opacity:"), m_more), 1, 2);
	m_stopOpacity = new QSpinBox(m_more);
	m_stopOpacity->setRange(0, 100);
	m_stopOpacity->setSuffix(QStringLiteral(" %"));
	m_stopOpacity->setToolTip(tr("Transparency of the selected stop (PDF: soft mask; not in PostScript printing)"));
	mg->addWidget(m_stopOpacity, 1, 3);
	connect(m_angleSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (m_updating)
			return;
		m_angle = v;
		m_type = 0;
		updateDirectionButtons();
		updatePreview();
		apply(tr("Gradient direction"));
	});
	connect(m_stopShade, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { stopShadeChanged(); });
	connect(m_stopOpacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { stopShadeChanged(); });

	m_shadow = new QGroupBox(tr("Soft shadow (frame)"), m_more);
	m_shadow->setCheckable(true);
	m_shadow->setChecked(false);
	m_shadow->setToolTip(tr("The frame's soft shadow: with no frame fill it shadows the text. Blurred, so it is an image in the PDF; PostScript printing does not draw it."));
	auto* sg = new QGridLayout(m_shadow);
	sg->setContentsMargins(4, 4, 4, 4);
	sg->setVerticalSpacing(3);
	m_shadowX = new ScrSpinBox(-200.0, 200.0, m_shadow, 0);
	m_shadowY = new ScrSpinBox(-200.0, 200.0, m_shadow, 0);
	m_shadowBlur = new ScrSpinBox(0.0, 100.0, m_shadow, 0);
	m_shadowColor = new ColorCombo(m_shadow);
	m_shadowOpacity = new QSpinBox(m_shadow);
	m_shadowOpacity->setRange(0, 100);
	m_shadowOpacity->setSuffix(QStringLiteral(" %"));
	sg->addWidget(new QLabel(tr("X:"), m_shadow), 0, 0);
	sg->addWidget(m_shadowX, 0, 1);
	sg->addWidget(new QLabel(tr("Y:"), m_shadow), 0, 2);
	sg->addWidget(m_shadowY, 0, 3);
	sg->addWidget(new QLabel(tr("Blur:"), m_shadow), 1, 0);
	sg->addWidget(m_shadowBlur, 1, 1);
	sg->addWidget(new QLabel(tr("Opacity:"), m_shadow), 1, 2);
	sg->addWidget(m_shadowOpacity, 1, 3);
	sg->addWidget(new QLabel(tr("Colour:"), m_shadow), 2, 0);
	sg->addWidget(m_shadowColor, 2, 1, 1, 3);
	mg->addWidget(m_shadow, 2, 0, 1, 4);
	connect(m_shadow, &QGroupBox::toggled, this, [this]() { applyShadow(); });
	for (ScrSpinBox* sb : { m_shadowX, m_shadowY, m_shadowBlur })
		connect(sb, QOverload<double>::of(&ScrSpinBox::valueChanged), this, [this]() { applyShadow(); });
	connect(m_shadowOpacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { applyShadow(); });
	connect(m_shadowColor, QOverload<int>::of(&QComboBox::activated), this, [this]() { applyShadow(); });
	m_more->setVisible(false);
	top->addWidget(m_more);
	connect(m_moreBtn, &QPushButton::toggled, this, [this]() { toggleMore(); });

	// inline name row (save as preset / rename)
	m_nameRow = new QWidget(this);
	auto* nr = new QHBoxLayout(m_nameRow);
	nr->setContentsMargins(0, 0, 0, 0);
	nr->setSpacing(3);
	m_nameLabel = new QLabel(tr("Preset name:"), m_nameRow);
	m_nameEdit = new QLineEdit(m_nameRow);
	auto* nameOk = new QPushButton(tr("Save"), m_nameRow);
	auto* nameCancel = new QPushButton(tr("Cancel"), m_nameRow);
	nr->addWidget(m_nameLabel);
	nr->addWidget(m_nameEdit, 1);
	nr->addWidget(nameOk);
	nr->addWidget(nameCancel);
	m_nameRow->setVisible(false);
	top->addWidget(m_nameRow);
	connect(nameOk, &QPushButton::clicked, this, [this]() { nameRowDone(); });
	connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() { nameRowDone(); });
	connect(nameCancel, &QPushButton::clicked, this, [this]() { m_nameRow->setVisible(false); m_renameFrom.clear(); });

	// 8. bottom row
	auto* bottom = new QHBoxLayout();
	bottom->setSpacing(3);
	m_savePreset = new QPushButton(QString::fromUtf8("★ ") + tr("Save as preset"), this);
	m_savePreset->setToolTip(tr("Keep this gradient, with its colours and direction, as a preset for every document"));
	m_removeGradient = new QPushButton(tr("Remove gradient"), this);
	m_removeGradient->setToolTip(tr("Back to the solid text colour"));
	m_apply = new QPushButton(tr("Apply"), this);
	m_apply->setDefault(true);
	m_apply->setToolTip(tr("Apply once more and close"));
	bottom->addWidget(m_savePreset);
	bottom->addWidget(m_removeGradient);
	bottom->addStretch(1);
	bottom->addWidget(m_apply);
	top->addLayout(bottom);
	connect(m_savePreset, &QPushButton::clicked, this, [this]() { savePresetAsked(); });
	connect(m_removeGradient, &QPushButton::clicked, this, [this]() { removeGradient(); });
	connect(m_apply, &QPushButton::clicked, this, [this]() {
		if (canApply())
			apply();
		emit closeRequested();
	});

	rebuildPresets();
	syncButtonsFromState();
	setMinimumWidth(300);
}

void SuneerTextColourWidget::rebuildPresets()
{
	for (QToolButton* b : m_presetButtons)
		b->deleteLater();
	m_presetButtons.clear();
	if (m_presetAdd)
	{
		m_presetAdd->deleteLater();
		m_presetAdd = nullptr;
	}
	const QList<SuneerGradientPresets::Preset> presets = SuneerGradientPresets::all();
	const int cols = 6;
	int i = 0;
	for (const SuneerGradientPresets::Preset& p : presets)
	{
		auto* b = new QToolButton(m_presetGrid);
		b->setObjectName("suneerPreset_" + p.name);
		b->setAutoRaise(true);
		b->setToolTip(p.name);
		b->setIconSize(kThumb);
		b->setFixedSize(kThumb.width() + 6, kThumb.height() + 6);
		b->setIcon(SuneerGradientPresets::thumbnail(p, kThumb, m_doc, m_gradient && p.name == m_gradName));
		b->setContextMenuPolicy(Qt::CustomContextMenu);
		b->setProperty("presetName", p.name);
		m_presetLayout->addWidget(b, i / cols, i % cols);
		m_presetButtons << b;
		const QString name = p.name;
		connect(b, &QToolButton::clicked, this, [this, name]() { presetClicked(name); });
		connect(b, &QToolButton::customContextMenuRequested, this, [this, name, b](const QPoint& pos) { presetMenu(name, b->mapToGlobal(pos)); });
		++i;
	}
	m_presetAdd = new QToolButton(m_presetGrid);
	m_presetAdd->setObjectName("suneerPresetAdd");
	m_presetAdd->setText(QStringLiteral("+"));
	m_presetAdd->setToolTip(tr("Save the current gradient as a preset"));
	m_presetAdd->setFixedSize(kThumb.width() + 6, kThumb.height() + 6);
	m_presetAdd->setStyleSheet("QToolButton { font-size: 18px; font-weight: bold; border: 1px dashed palette(mid); border-radius: 5px; }");
	m_presetLayout->addWidget(m_presetAdd, i / cols, i % cols);
	connect(m_presetAdd, &QToolButton::clicked, this, [this]() { savePresetAsked(); });
}

void SuneerTextColourWidget::setDoc(ScribusDoc* doc)
{
	m_doc = doc;
	m_item = nullptr;
	m_undo->setDoc(doc);
	m_shadowX->setNewUnit(doc ? doc->unitIndex() : 0);
	m_shadowY->setNewUnit(doc ? doc->unitIndex() : 0);
	m_shadowBlur->setNewUnit(doc ? doc->unitIndex() : 0);
	if (doc)
	{
		m_shadowColor->setColors(doc->PageColors, false);
		m_solidList->setColors(doc->PageColors, true);
		m_stopList->setColors(doc->PageColors, false);
	}
	rebuildPresets();
}

void SuneerTextColourWidget::showStyle(const CharStyle& style)
{
	m_updating = true;
	m_gradient = !style.fillGradient().isEmpty();
	m_gradName = style.fillGradient();
	m_type = qBound(0, style.fillGradientType(), 1);
	m_angle = fmod(style.fillGradientAngle() + 360.0, 360.0);
	m_solidColour = style.fillColor();
	m_solidShade = style.fillShade();
	m_scope = (style.fillGradientMapping() == 1) ? WholeText : EachLine;
	if (m_gradient && m_doc && !m_doc->docGradients.contains(m_gradName))
		SuneerGradientPresets::ensureGradient(m_doc, m_gradName);
	if (m_gradient && m_doc && m_doc->docGradients.contains(m_gradName))
		m_bar->setGradient(m_doc->docGradients[m_gradName]);
	else if (!m_gradient)
	{
		// the bar keeps showing a gradient to start from
		if (m_bar->stopCount() < 2)
		{
			SuneerGradientPresets::Preset p;
			if (SuneerGradientPresets::find(QStringLiteral("Red headline"), p) || !SuneerGradientPresets::all().isEmpty())
			{
				if (p.name.isEmpty())
					p = SuneerGradientPresets::all().first();
				m_bar->setGradient(SuneerGradientPresets::toGradient(p, nullptr));
			}
		}
	}
	const ScFace& face = style.font();
	m_preview->setFontFamily(face.family(), face.style().contains(QLatin1String("Bold"), Qt::CaseInsensitive) || face.scName().contains(QLatin1String("Bold"), Qt::CaseInsensitive));
	m_solidList->setCurrentColor(m_solidColour);
	m_solidShadeSpin->setValue(qRound(m_solidShade));
	m_angleSpin->setValue(qRound(m_angle) % 360);
	m_pages->setCurrentIndex(m_gradient ? 1 : 0);
	m_solidBtn->setChecked(!m_gradient);
	m_gradientBtn->setChecked(m_gradient);
	syncButtonsFromState();
	updateStopEditor();
	updatePreview();
	refreshTarget(false);
	m_updating = false;
}

void SuneerTextColourWidget::showItem(PageItem* item)
{
	m_item = item;
	m_updating = true;
	QString text;
	if (item)
	{
		refreshTarget(false);
		int start = 0, length = 0;
		PageItem* edited = editedTextItem();
		if (edited && m_target != TargetWhole && targetRange(start, length) && length > 0)
			text = edited->itemText.text(start, length);
		else
			text = item->itemText.plainText().section(QChar(0x2029), 0, 0).section(QChar('\n'), 0, 0);
		text.replace(QChar(0x2028), QChar(' ')).replace(QChar(0x2029), QChar(' '));
	}
	m_preview->setText(text);
	refreshTarget(false);
	updateScopeButtons();
	m_shadow->setEnabled(item != nullptr);
	if (item)
	{
		const QList<QWidget*> ws { m_shadow, m_shadowX, m_shadowY, m_shadowBlur, m_shadowColor, m_shadowOpacity };
		QList<bool> blocked;
		for (QWidget* w : ws)
			blocked << w->blockSignals(true);
		m_shadow->setChecked(item->hasSoftShadow());
		m_shadowX->setValue(item->softShadowXOffset() * m_shadowX->unitRatio());
		m_shadowY->setValue(item->softShadowYOffset() * m_shadowY->unitRatio());
		m_shadowBlur->setValue(item->softShadowBlurRadius() * m_shadowBlur->unitRatio());
		m_shadowColor->setCurrentColor(item->softShadowColor());
		m_shadowOpacity->setValue(qRound((1.0 - item->softShadowOpacity()) * 100.0));
		for (int i = 0; i < ws.count(); ++i)
			ws[i]->blockSignals(blocked[i]);
	}
	m_updating = false;
}

bool SuneerTextColourWidget::isGradient() const
{
	return m_gradient && !m_gradName.isEmpty();
}

Selection* SuneerTextColourWidget::targetSelection() const
{
	if (!m_doc)
		return nullptr;
	Selection* sel = m_selection ? m_selection() : nullptr;
	return sel ? sel : m_doc->m_Selection;
}

bool SuneerTextColourWidget::canApply() const
{
	Selection* target = targetSelection();
	return target && !target->isEmpty();
}

PageItem* SuneerTextColourWidget::editedTextItem() const
{
	Selection* target = targetSelection();
	if (!target || target->count() != 1 || !target->itemAt(0))
		return nullptr;
	PageItem* it = target->itemAt(0);
	if (m_doc->appMode == modeEdit)
		return it->isTextFrame() ? it : nullptr;
	if (m_doc->appMode == modeEditTable && it->isTable() && !it->asTable()->hasSelection())
	{
		const QList<PageItem_TextFrame*> cells = SuneerTable::cellFrames(it->asTable(), true);
		return cells.size() == 1 ? cells.first() : nullptr;
	}
	return nullptr;
}

SuneerTextColourWidget::Target SuneerTextColourWidget::defaultTarget() const
{
	PageItem* edited = editedTextItem();
	if (!edited)
		return TargetWhole;
	return edited->itemText.hasSelection() ? TargetSelection : TargetParagraph;
}

QString SuneerTextColourWidget::targetSignature() const
{
	Selection* target = targetSelection();
	if (!target || !m_doc)
		return QString();
	QString sig = QString::number(m_doc->appMode);
	if (PageItem* edited = editedTextItem())
	{
		const StoryText& t = edited->itemText;
		sig += QStringLiteral("|%1").arg(quintptr(edited));
		sig += t.hasSelection() ? QStringLiteral("|s%1-%2").arg(t.startOfSelection()).arg(t.endOfSelection())
		                        : QStringLiteral("|p%1").arg(t.nrOfParagraph(qBound(0, t.cursorPosition(), t.length())));
		return sig;
	}
	for (int i = 0; i < target->count(); ++i)
		sig += QStringLiteral("|%1").arg(quintptr(target->itemAt(i)));
	return sig;
}

void SuneerTextColourWidget::refreshTarget(bool force)
{
	const QString sig = targetSignature();
	if (force || sig != m_targetSignature)
	{
		m_targetSignature = sig;
		m_target = defaultTarget();
	}
	updateTargetButtons();
}

void SuneerTextColourWidget::resetTarget()
{
	Selection* target = targetSelection();
	refreshTarget(true);
	showItem(target && !target->isEmpty() ? target->itemAt(0) : nullptr);
}

bool SuneerTextColourWidget::targetRange(int& start, int& length) const
{
	PageItem* edited = editedTextItem();
	if (!edited)
		return false;
	const StoryText& t = edited->itemText;
	const bool selection = t.hasSelection();
	if (m_target == TargetWhole)
	{
		start = 0;
		length = t.length();
	}
	else if (m_target == TargetSelection)
	{
		if (!selection)
			return false;
		start = t.startOfSelection();
		length = t.endOfSelection() - start;
	}
	else
	{
		const int from = selection ? t.startOfSelection() : qBound(0, t.cursorPosition(), t.length());
		const int to = selection ? t.endOfSelection() - 1 : from;
		start = t.startOfParagraph(t.nrOfParagraph(from));
		length = t.endOfParagraph(t.nrOfParagraph(to)) - start;
	}
	return true;
}

void SuneerTextColourWidget::updateTargetButtons()
{
	if (m_targetButtons.size() != 3)
		return;
	PageItem* edited = editedTextItem();
	const bool hasText = edited && edited->itemText.hasSelection();
	for (int i = 0; i < 3; ++i)
	{
		const bool blocked = m_targetButtons[i]->blockSignals(true);
		m_targetButtons[i]->setChecked(i == m_target);
		m_targetButtons[i]->blockSignals(blocked);
	}
	m_targetButtons[TargetSelection]->setEnabled(hasText);
	m_targetButtons[TargetParagraph]->setEnabled(edited != nullptr);
	Selection* target = targetSelection();
	const int frames = target ? target->count() : 0;
	QString note;
	if (m_target == TargetSelection)
		note = tr("Only the selected text changes.");
	else if (m_target == TargetParagraph)
		note = hasText ? tr("The whole paragraph(s) of the selection change.") : tr("Nothing is selected: only the paragraph with the cursor changes.");
	else if (edited)
		note = tr("All the text of this frame changes.");
	else if (m_doc && m_doc->appMode == modeEditTable)
		note = tr("All the text of the selected cells changes.");
	else
		note = tr("All the text of the %n selected frame(s) changes.", "", frames);
	m_targetNote->setText(note);
}

void SuneerTextColourWidget::targetClicked(int target)
{
	if (m_updating)
		return;
	// choosing only: the next colour goes there (the colour shown now is not forced on all that text)
	m_target = static_cast<Target>(target);
	updateTargetButtons();
}

QList<PageItem*> SuneerTextColourWidget::targetItems() const
{
	QList<PageItem*> out;
	if (!m_doc)
		return out;
	Selection* sel = m_selection ? m_selection() : nullptr;
	Selection* target = sel ? sel : m_doc->m_Selection;
	if (!target)
		return out;
	for (int i = 0; i < target->count(); ++i)
	{
		PageItem* it = target->itemAt(i);
		if (!it)
			continue;
		out << it;
		if (it->isTable())
		{
			PageItem_Table* tbl = it->asTable();
			for (int r = 0; tbl && r < tbl->rows(); ++r)
				for (int c = 0; c < tbl->columns(); ++c)
					if (PageItem* cell = tbl->cellAt(r, c).textFrame())
						out << cell;
		}
	}
	return out;
}

void SuneerTextColourWidget::syncButtonsFromState()
{
	updateDirectionButtons();
	updateScopeButtons();
	m_removeGradient->setEnabled(m_gradient);
	m_savePreset->setEnabled(m_gradient);
	m_presetAdd->setEnabled(m_gradient);
}

void SuneerTextColourWidget::updateDirectionButtons()
{
	for (int i = 0; i < m_dirButtons.count(); ++i)
	{
		QToolButton* b = m_dirButtons[i];
		const bool on = (i == 8) ? (m_type == 1) : (m_type == 0 && qRound(m_angle) % 360 == b->property("angle").toInt());
		const bool blocked = b->blockSignals(true);
		b->setChecked(on);
		b->blockSignals(blocked);
	}
	m_angleSpin->setEnabled(m_type == 0);
}

void SuneerTextColourWidget::updateScopeButtons()
{
	for (int i = 0; i < m_scopeButtons.count(); ++i)
	{
		const bool blocked = m_scopeButtons[i]->blockSignals(true);
		m_scopeButtons[i]->setChecked(i == m_scope);
		m_scopeButtons[i]->blockSignals(blocked);
	}
}

QColor SuneerTextColourWidget::stopDisplayColour(const QString& name, int shade, double opacity) const
{
	QColor q(Qt::black);
	if (m_doc && m_doc->PageColors.contains(name))
		q = ScColorEngine::getShadeColorProof(m_doc->PageColors[name], m_doc, shade);
	q.setAlphaF(opacity);
	return q;
}

VGradient SuneerTextColourWidget::currentGradient() const
{
	return m_bar->gradient();
}

void SuneerTextColourWidget::updatePreview()
{
	if (m_gradient)
		m_preview->setGradient(m_bar->gradient(), m_type, m_angle, m_scope != WholeText);
	else
		m_preview->setSolid(stopDisplayColour(m_solidColour, qRound(m_solidShade), 1.0));
	// preset highlight
	for (QToolButton* b : m_presetButtons)
	{
		const QString name = b->property("presetName").toString();
		SuneerGradientPresets::Preset p;
		if (SuneerGradientPresets::find(name, p))
			b->setIcon(SuneerGradientPresets::thumbnail(p, kThumb, m_doc, m_gradient && name == m_gradName));
	}
}

void SuneerTextColourWidget::updateStopEditor()
{
	const int i = m_bar->activeStop();
	const VGradient& g = m_bar->gradient();
	const bool have = i >= 0 && i < g.stops();
	m_stopEditor->setEnabled(have);
	m_removeStop->setEnabled(g.stops() > 2);
	m_addStop->setEnabled(g.stops() < 4);
	if (!have)
		return;
	const VColorStop* s = g.colorStops().at(i);
	m_stopLabel->setText(tr("Colour of stop %1 (%2 %):").arg(i + 1).arg(qRound(s->rampPoint * 100)));
	const bool b1 = m_stopList->blockSignals(true), b2 = m_stopShade->blockSignals(true), b3 = m_stopOpacity->blockSignals(true);
	m_stopList->setCurrentColor(s->name);
	m_stopShade->setValue(s->shade);
	m_stopOpacity->setValue(qRound(s->opacity * 100.0));
	m_stopList->blockSignals(b1); m_stopShade->blockSignals(b2); m_stopOpacity->blockSignals(b3);
}

// ── actions ──

void SuneerTextColourWidget::setModeGradient(bool gradient, bool applyNow)
{
	if (m_updating)
		return;
	m_gradient = gradient;
	m_pages->setCurrentIndex(gradient ? 1 : 0);
	if (gradient)
	{
		if (m_gradName.isEmpty() || (m_doc && !m_doc->docGradients.contains(m_gradName)))
		{
			// the first time: the red headline preset
			QString name = QStringLiteral("Red headline");
			SuneerGradientPresets::Preset p;
			if (!SuneerGradientPresets::find(name, p) && !SuneerGradientPresets::all().isEmpty())
			{
				p = SuneerGradientPresets::all().first();
				name = p.name;
			}
			if (!p.name.isEmpty())
			{
				m_gradName = name;
				m_type = p.type;
				m_angle = p.angle;
				SuneerGradientPresets::ensureGradient(m_doc, name);
			}
		}
		if (m_doc && m_doc->docGradients.contains(m_gradName))
			m_bar->setGradient(m_doc->docGradients[m_gradName]);
	}
	m_solidBtn->setChecked(!gradient);
	m_gradientBtn->setChecked(gradient);
	syncButtonsFromState();
	updateStopEditor();
	updatePreview();
	if (applyNow)
		apply(gradient ? tr("Text gradient") : tr("Solid text colour"));
}

void SuneerTextColourWidget::presetClicked(const QString& name)
{
	SuneerGradientPresets::Preset p;
	if (!SuneerGradientPresets::find(name, p))
		return;
	if (m_doc)
	{
		// the document copy of a preset always follows the preset (its own edits are copies)
		m_doc->docGradients.insert(name, SuneerGradientPresets::toGradient(p, m_doc));
		if (m_doc->scMW())
			m_doc->scMW()->updateColorLists();
	}
	m_gradient = true;
	m_gradName = name;
	m_type = p.type;
	m_angle = p.angle;
	m_pages->setCurrentIndex(1);
	m_solidBtn->setChecked(false);
	m_gradientBtn->setChecked(true);
	m_updating = true;
	m_angleSpin->setValue(qRound(m_angle) % 360);
	m_updating = false;
	if (m_doc)
		m_bar->setGradient(m_doc->docGradients[name]);
	else
		m_bar->setGradient(SuneerGradientPresets::toGradient(p, nullptr));
	m_bar->setActiveStop(0);
	syncButtonsFromState();
	updateStopEditor();
	updatePreview();
	apply(tr("Preset %1").arg(name));
}

void SuneerTextColourWidget::presetMenu(const QString& name, const QPoint& globalPos)
{
	QMenu menu(this);
	QAction* rename = menu.addAction(tr("Rename…"));
	QAction* del = menu.addAction(tr("Delete"));
	QAction* chosen = menu.exec(globalPos);
	if (chosen == rename)
	{
		m_renameFrom = name;
		m_nameLabel->setText(tr("New name:"));
		m_nameEdit->setText(name);
		m_nameRow->setVisible(true);
		m_nameEdit->setFocus();
		m_nameEdit->selectAll();
	}
	else if (chosen == del)
	{
		SuneerGradientPresets::remove(name);
		rebuildPresets();
		updatePreview();
	}
}

void SuneerTextColourWidget::directionClicked(int angle, bool radial)
{
	if (m_updating)
		return;
	m_type = radial ? 1 : 0;
	if (!radial)
		m_angle = angle;
	m_updating = true;
	m_angleSpin->setValue(qRound(m_angle) % 360);
	m_updating = false;
	updateDirectionButtons();
	updatePreview();
	apply(radial ? tr("Radial gradient") : tr("Gradient direction"));
}

void SuneerTextColourWidget::scopeClicked(int scope)
{
	if (m_updating)
		return;
	m_scope = scope;
	updateScopeButtons();
	updatePreview();
	apply(tr("Gradient apply to"));
}

void SuneerTextColourWidget::stopSelected(int)
{
	updateStopEditor();
}

void SuneerTextColourWidget::stopColourPicked(const QString& colour)
{
	if (m_updating || colour.isEmpty() || !m_doc || !m_doc->PageColors.contains(colour))
		return;
	VGradient g = m_bar->gradient();
	const int i = m_bar->activeStop();
	if (i < 0 || i >= g.stops())
		return;
	VColorStop* s = g.colorStops().at(i);
	s->name = colour;
	s->color = stopDisplayColour(colour, s->shade, s->opacity);
	m_bar->setGradient(g);
	stopsEdited(true);
}

void SuneerTextColourWidget::stopShadeChanged()
{
	if (m_updating)
		return;
	VGradient g = m_bar->gradient();
	const int i = m_bar->activeStop();
	if (i < 0 || i >= g.stops())
		return;
	VColorStop* s = g.colorStops().at(i);
	s->shade = m_stopShade->value();
	s->opacity = m_stopOpacity->value() / 100.0;
	s->color = stopDisplayColour(s->name, s->shade, s->opacity);
	m_bar->setGradient(g);
	stopsEdited(true);
}

void SuneerTextColourWidget::addCmykColour()
{
	if (!m_doc)
		return;
	const int c = m_cmyk[0]->value(), m = m_cmyk[1]->value(), y = m_cmyk[2]->value(), k = m_cmyk[3]->value();
	const QString name = QStringLiteral("CMYK %1 %2 %3 %4").arg(c).arg(m).arg(y).arg(k);
	if (!m_doc->PageColors.contains(name))
	{
		m_doc->PageColors.insert(name, ScColor(qRound(c * 2.55), qRound(m * 2.55), qRound(y * 2.55), qRound(k * 2.55)));
		m_doc->changed();
		if (m_doc->scMW())
			m_doc->scMW()->updateColorLists();
		m_stopList->setColors(m_doc->PageColors, false);
		m_solidList->setColors(m_doc->PageColors, true);
		m_shadowColor->setColors(m_doc->PageColors, false);
	}
	if (m_gradient)
	{
		m_stopList->setCurrentColor(name);
		stopColourPicked(name);
	}
	else
	{
		m_solidList->setCurrentColor(name);
		solidColourPicked();
	}
}

void SuneerTextColourWidget::addStop()
{
	VGradient g = m_bar->gradient();
	if (g.stops() >= 4)
		return;
	const int i = qBound(0, m_bar->activeStop(), g.stops() - 1);
	const QList<VColorStop*>& stops = g.colorStops();
	const VColorStop* a = stops.at(i);
	const VColorStop* b = stops.at(qMin(i + 1, stops.count() - 1));
	double pos = (a == b) ? (stops.at(i - 1)->rampPoint + a->rampPoint) / 2.0 : (a->rampPoint + b->rampPoint) / 2.0;
	g.addStop(a->color, pos, 0.5, a->opacity, a->name, a->shade);
	m_bar->setGradient(g);
	for (int j = 0; j < g.stops(); ++j)
		if (qFuzzyCompare(g.colorStops().at(j)->rampPoint, pos))
			m_bar->setActiveStop(j);
	stopsEdited(true);
}

void SuneerTextColourWidget::swapStops()
{
	VGradient g = m_bar->gradient();
	VGradient r(g.type());
	r.clearStops();
	for (const VColorStop* s : g.colorStops())
		r.addStop(s->color, 1.0 - s->rampPoint, 0.5, s->opacity, s->name, s->shade);
	const int active = m_bar->activeStop();
	m_bar->setGradient(r);
	m_bar->setActiveStop(r.stops() - 1 - active);
	stopsEdited(true);
}

void SuneerTextColourWidget::removeStop()
{
	VGradient g = m_bar->gradient();
	if (g.stops() <= 2)
		return;
	const int i = m_bar->activeStop();
	VGradient r(g.type());
	r.clearStops();
	for (int j = 0; j < g.stops(); ++j)
		if (j != i)
		{
			const VColorStop* s = g.colorStops().at(j);
			r.addStop(s->color, s->rampPoint, 0.5, s->opacity, s->name, s->shade);
		}
	m_bar->setGradient(r);
	m_bar->setActiveStop(qMax(0, i - 1));
	stopsEdited(true);
}

void SuneerTextColourWidget::stopsEdited(bool applyNow)
{
	updateStopEditor();
	updatePreview();
	if (!applyNow || m_updating)
		return;
	if (!m_gradient)
	{
		m_gradient = true;
		m_pages->setCurrentIndex(1);
		m_solidBtn->setChecked(false);
		m_gradientBtn->setChecked(true);
	}
	writeGradient(m_bar->gradient(), tr("Gradient colours"));
}

void SuneerTextColourWidget::solidColourPicked()
{
	if (m_updating || !m_doc)
		return;
	m_solidColour = m_solidList->currentColor();
	m_solidShade = m_solidShadeSpin->value();
	if (m_solidColour.isEmpty())
		return;
	m_gradient = false;
	updatePreview();
	apply(tr("Text colour"));
}

void SuneerTextColourWidget::savePresetAsked()
{
	if (!m_gradient)
		return;
	m_renameFrom.clear();
	m_nameLabel->setText(tr("Preset name:"));
	QString suggested = m_gradName;
	if (suggested.startsWith(QLatin1String("Text gradient")))
		suggested = tr("My gradient");
	m_nameEdit->setText(suggested);
	m_nameRow->setVisible(true);
	m_nameEdit->setFocus();
	m_nameEdit->selectAll();
}

void SuneerTextColourWidget::nameRowDone()
{
	const QString name = m_nameEdit->text().trimmed();
	m_nameRow->setVisible(false);
	if (name.isEmpty())
		return;
	if (!m_renameFrom.isEmpty())
	{
		SuneerGradientPresets::rename(m_renameFrom, name);
		if (m_gradName == m_renameFrom && m_doc && m_doc->docGradients.contains(m_renameFrom))
		{
			m_doc->docGradients.insert(name, m_doc->docGradients.value(m_renameFrom));
			m_gradName = name;
			apply(tr("Preset renamed"));
		}
		m_renameFrom.clear();
		rebuildPresets();
		updatePreview();
		return;
	}
	if (!m_doc || !m_doc->docGradients.contains(m_gradName))
		return;
	// the document gets a gradient with the preset's name; the preset keeps the CMYK values
	if (name != m_gradName)
	{
		m_doc->docGradients.insert(name, m_doc->docGradients.value(m_gradName));
		m_gradName = name;
		if (m_doc->scMW())
			m_doc->scMW()->updateColorLists();
	}
	SuneerGradientPresets::save(SuneerGradientPresets::fromDocument(m_doc, name, m_type, m_angle));
	rebuildPresets();
	updatePreview();
	apply(tr("Preset %1").arg(name));
}

void SuneerTextColourWidget::removeGradient()
{
	if (!m_gradient)
		return;
	m_gradient = false;
	m_pages->setCurrentIndex(0);
	m_solidBtn->setChecked(true);
	m_gradientBtn->setChecked(false);
	if (m_solidColour.isEmpty() || m_solidColour == CommonStrings::None)
	{
		m_solidColour = QStringLiteral("Black");
		m_solidShade = 100.0;
		m_updating = true;
		m_solidList->setCurrentColor(m_solidColour);
		m_solidShadeSpin->setValue(100);
		m_updating = false;
	}
	syncButtonsFromState();
	updatePreview();
	apply(tr("Remove text gradient"));
}

void SuneerTextColourWidget::toggleMore()
{
	m_more->setVisible(m_moreBtn->isChecked());
	m_moreBtn->setText(m_moreBtn->isChecked() ? tr("Less…") : tr("More…"));
	// a popup grows / shrinks with it
	for (QWidget* w = this; w; w = w->parentWidget())
		if (qobject_cast<SuneerTextColourPopup*>(w))
		{
			w->adjustSize();
			break;
		}
}

// ── applying ──

bool SuneerTextColourWidget::gradientUsedElsewhere(const QString& name, const QList<PageItem*>& except) const
{
	if (!m_doc || name.isEmpty())
		return false;
	auto scan = [&](const QList<PageItem*>& items) {
		for (PageItem* it : items)
		{
			if (!it)
				continue;
			if (it->gradient() == name && it->GrType != 0)
				return true;
			if (except.contains(it))
				continue;
			if (it->isTable())
			{
				PageItem_Table* tbl = it->asTable();
				for (int r = 0; tbl && r < tbl->rows(); ++r)
					for (int c = 0; c < tbl->columns(); ++c)
					{
						PageItem* cell = tbl->cellAt(r, c).textFrame();
						if (!cell || except.contains(cell))
							continue;
						for (int k = 0; k < cell->itemText.length(); ++k)
							if (cell->itemText.charStyle(k).fillGradient() == name)
								return true;
					}
				continue;
			}
			if (!it->isTextFrame() && !it->isPathText())
				continue;
			// the story once per chain
			if (it->prevInChain())
				continue;
			for (int k = 0; k < it->itemText.length(); ++k)
				if (it->itemText.charStyle(k).fillGradient() == name)
					return true;
		}
		return false;
	};
	return scan(m_doc->DocItems) || scan(m_doc->MasterItems);
}

QString SuneerTextColourWidget::uniqueGradientName() const
{
	for (int n = 1; n < 10000; ++n)
	{
		const QString name = QStringLiteral("Text gradient %1").arg(n);
		if (!m_doc || !m_doc->docGradients.contains(name))
			return name;
	}
	return QStringLiteral("Text gradient");
}

void SuneerTextColourWidget::ownGradient()
{
	if (!m_doc)
		return;
	bool shared = false;
	SuneerGradientPresets::Preset p;
	if (m_gradName.isEmpty() || !m_doc->docGradients.contains(m_gradName))
		shared = true;
	else if (SuneerGradientPresets::find(m_gradName, p))
		shared = true;   // a preset's document copy stays as the preset
	else
	{
		PageItem* edited = editedTextItem();
		int from = 0, length = 0;
		const bool part = edited && targetRange(from, length) && length < edited->itemText.length();
		QList<PageItem*> except = targetItems();
		if (part)
			except.clear();   // the rest of this frame counts as "elsewhere"
		shared = gradientUsedElsewhere(m_gradName, except);
		if (part)
		{
			// other characters of this very frame?
			for (int k = 0; k < edited->itemText.length() && !shared; ++k)
				if ((k < from || k >= from + length) && edited->itemText.charStyle(k).fillGradient() == m_gradName)
					shared = true;
		}
	}
	if (shared)
		m_gradName = uniqueGradientName();
}

void SuneerTextColourWidget::writeGradient(const VGradient& g, const QString& undoName)
{
	if (!m_doc)
		return;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::SelectionGroup, Um::IGroup, undoName, m_gradName, Um::IFill);
	ownGradient();
	const bool existed = m_doc->docGradients.contains(m_gradName);
	const VGradient old = existed ? m_doc->docGradients.value(m_gradName) : VGradient();
	m_doc->docGradients.insert(m_gradName, g);
	if (qEnvironmentVariableIsSet("SUNEER_TEXTCOLOUR_DEBUG"))
		fprintf(stderr, "TEXTCOLOUR writeGradient '%s' existed=%d stops=%d\n", qPrintable(m_gradName), existed ? 1 : 0, g.stops());
	m_undo->record(m_gradName, existed ? &old : nullptr, g);
	apply(QString());
	if (tx)
		tx.commit();
	// after the apply: refreshing the colour palettes re-enters showStyle() with the text's style
	if (m_doc->scMW())
		m_doc->scMW()->updateColorLists();
	updatePreview();
}

void SuneerTextColourWidget::apply(const QString& undoName)
{
	if (qEnvironmentVariableIsSet("SUNEER_TEXTCOLOUR_DEBUG"))
		fprintf(stderr, "TEXTCOLOUR apply '%s' updating=%d doc=%p canApply=%d gradient=%d name='%s' type=%d angle=%g scope=%d\n",
		        qPrintable(undoName), m_updating ? 1 : 0, (void*) m_doc.data(), canApply() ? 1 : 0, m_gradient ? 1 : 0, qPrintable(m_gradName), m_type, m_angle, m_scope);
	if (m_updating || !m_doc || !canApply())
		return;
	// the selection decides: a changed one puts Apply to back on what it means, never on something wider
	refreshTarget(false);
	PageItem* edited = editedTextItem();
	int from = 0, length = 0;
	if (edited && (!targetRange(from, length) || length <= 0))
		return;
	CharStyle cs;
	if (m_gradient && !m_gradName.isEmpty())
	{
		SuneerGradientPresets::ensureGradient(m_doc, m_gradName);
		cs.setFillGradient(m_gradName);
		cs.setFillGradientType(m_type);
		cs.setFillGradientAngle(m_angle);
		cs.setFillGradientMapping(m_scope == WholeText ? 1 : 0);
	}
	else
	{
		cs.setFillGradient(QString());
		if (!m_solidColour.isEmpty())
		{
			cs.setFillColor(m_solidColour);
			cs.setFillShade(m_solidShade);
		}
	}
	Selection* sel = m_selection ? m_selection() : nullptr;
	UndoTransaction tx;
	if (!undoName.isEmpty() && UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::SelectionGroup, Um::IGroup, undoName, QString(), Um::IFont);
	if (edited)
	{
		// the core applies to the text selection: lend it the target range, then give the user's back
		StoryText& story = edited->itemText;
		const bool hadSelection = story.hasSelection();
		const int oldStart = hadSelection ? story.startOfSelection() : 0;
		const int oldLength = hadSelection ? story.endOfSelection() - oldStart : 0;
		const int oldCursor = story.cursorPosition();
		story.deselectAll();
		story.select(from, length);
		m_doc->itemSelection_ApplyCharStyle(cs, sel, QString());
		story.deselectAll();
		if (hadSelection)
			story.select(oldStart, oldLength);
		story.setCursorPosition(oldCursor);
	}
	else
	{
		// frames or cells: all their text. Several frames in edit mode take the frame path.
		const int oldMode = m_doc->appMode;
		if (oldMode == modeEdit)
			m_doc->appMode = modeNormal;
		m_doc->itemSelection_ApplyCharStyle(cs, sel, QString());
		m_doc->appMode = oldMode;
	}
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	for (PageItem* it : targetItems())
	{
		it->invalidateLayout();
		it->update();
	}
	emit changed();
}

void SuneerTextColourWidget::applyShadow()
{
	if (m_updating || !m_doc || !canApply())
		return;
	Selection* sel = m_selection ? m_selection() : nullptr;
	Selection* target = sel ? sel : m_doc->m_Selection;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::SelectionGroup, Um::IGroup, tr("Soft shadow"), QString(), Um::IGroup);
	for (int i = 0; i < target->count(); ++i)
	{
		PageItem* it = target->itemAt(i);
		if (!it)
			continue;
		it->setSoftShadowXOffset(m_shadowX->value() / m_shadowX->unitRatio());
		it->setSoftShadowYOffset(m_shadowY->value() / m_shadowY->unitRatio());
		it->setSoftShadowBlurRadius(m_shadowBlur->value() / m_shadowBlur->unitRatio());
		QString col = m_shadowColor->currentColor();
		if (col.isEmpty() || col == CommonStrings::tr_NoneColor)
			col = QStringLiteral("Black");
		it->setSoftShadowColor(col);
		it->setSoftShadowOpacity(1.0 - m_shadowOpacity->value() / 100.0);
		it->setHasSoftShadow(m_shadow->isChecked());
	}
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

// ───────────────────────────── popup ─────────────────────────────

SuneerTextColourPopup::SuneerTextColourPopup(SuneerTextColourWidget* widget, QWidget* parent)
	: QFrame(parent, Qt::Popup | Qt::FramelessWindowHint), m_widget(widget)
{
	setObjectName("suneerTextColourPopup");
	setFrameShape(QFrame::StyledPanel);
	setFrameShadow(QFrame::Raised);
	auto* l = new QVBoxLayout(this);
	l->setContentsMargins(2, 2, 2, 2);
	l->addWidget(widget);
	connect(widget, &SuneerTextColourWidget::closeRequested, this, [this]() { hide(); });
}

void SuneerTextColourPopup::showBelow(QWidget* anchor)
{
	m_widget->resetTarget();
	adjustSize();
	QPoint pos = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 2)) : QCursor::pos();
	if (QScreen* scr = anchor ? anchor->screen() : qApp->primaryScreen())
	{
		const QRect avail = scr->availableGeometry();
		if (pos.x() + width() > avail.right())
			pos.setX(avail.right() - width());
		if (pos.y() + height() > avail.bottom())
			pos.setY(qMax(avail.top(), (anchor ? anchor->mapToGlobal(QPoint(0, 0)).y() : pos.y()) - height() - 2));
	}
	move(pos);
	show();
	raise();
}

// ───────────────────────────── self-test ─────────────────────────────

void SuneerTextColourWidget::runScopeTestIfAsked(ScribusView* view, ScribusDoc* doc, SuneerTextColourWidget* widget)
{
	static bool started = false;
	const QString prefix = qEnvironmentVariable("SUNEER_TEXTCOLOUR_SCOPETEST");
	if (started || prefix.isEmpty() || !view || !doc || !widget)
		return;
	started = true;
	QTimer::singleShot(2500, widget, [view, doc, widget, prefix]() {
		QFile rf(prefix + QStringLiteral("-report.txt"));
		if (!rf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			return;
		QTextStream ts(&rf);
		int failed = 0;
		auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); };
		auto check = [&](const QString& what, bool ok) { line(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what)); if (!ok) ++failed; };
		if (ScribusMainWindow* mw = doc->scMW())
			for (UndoGui* g : mw->findChildren<UndoGui*>())
				UndoManager::instance()->removeGui(g);
		auto frame = [&](const QString& name) -> PageItem* {
			for (PageItem* it : doc->DocItems)
				if (it && it->itemName() == name)
					return it;
			return nullptr;
		};
		PageItem* story = frame(QStringLiteral("Story"));
		PageItem* m1 = frame(QStringLiteral("Multi1"));
		PageItem* m2 = frame(QStringLiteral("Multi2"));
		PageItem* tableItem = frame(QStringLiteral("Cells"));
		check(QStringLiteral("test frames found"), story && m1 && m2 && tableItem && tableItem->isTable());
		if (!(story && m1 && m2 && tableItem && tableItem->isTable()))
		{
			rf.close();
			QTimer::singleShot(200, qApp, []() { QApplication::exit(2); });
			return;
		}
		const QString solid = QStringLiteral("Red");
		if (!doc->PageColors.contains(solid))
			doc->PageColors.insert(solid, ScColor(0, 255, 255, 0));
		const QString black = story->itemText.charStyle(0).fillColor();
		widget->setDoc(doc);
		auto pump = [&]() { for (int i = 0; i < 8; ++i) QApplication::processEvents(); };
		const int headEnd = story->itemText.endOfParagraph(0);
		const int bodyStart = story->itemText.startOfParagraph(1);
		const int len = story->itemText.length();
		int ml = 0;
		for (const QChar c : story->itemText.text(0, headEnd))
			ml += (c.unicode() >= 0x0D00 && c.unicode() <= 0x0D7F) ? 1 : 0;
		line(QStringLiteral("  Story: headline %1 chars (%2 Malayalam codepoints), body %3 chars").arg(headEnd).arg(ml).arg(len - bodyStart));
		auto allChars = [&](PageItem* it, int from, int to, const std::function<bool(const CharStyle&)>& f) {
			for (int i = from; i < to; ++i)
				if (it->itemText.text(i) != SpecialChars::PARSEP && !f(it->itemText.charStyle(i)))
					return false;
			return true;
		};
		auto isSolid = [&](const CharStyle& c) { return c.fillColor() == solid && c.fillGradient().isEmpty(); };
		auto isPlain = [&](const CharStyle& c) { return c.fillColor() == black && c.fillGradient().isEmpty(); };
		auto isGrad = [&](const CharStyle& c) { return !c.fillGradient().isEmpty(); };
		auto head = [&](const std::function<bool(const CharStyle&)>& f) { return allChars(story, 0, headEnd, f); };
		auto body = [&](const std::function<bool(const CharStyle&)>& f) { return allChars(story, bodyStart, len, f); };
		auto setScope = [&]() { widget->resetTarget(); pump(); };
		auto enter = [&](PageItem* it) {
			view->requestMode(modeNormal);
			doc->m_Selection->clear();
			doc->m_Selection->addItem(it);
			view->requestMode(modeEdit);
			pump();
			it->itemText.deselectAll();
		};
		auto selectText = [&](int from, int count) {
			story->itemText.deselectAll();
			story->itemText.select(from, count);
			story->itemText.setCursorPosition(from + count);
			widget->showStyle(story->itemText.charStyle(from));
			setScope();
		};
		auto cursorAt = [&](int pos) {
			story->itemText.deselectAll();
			story->itemText.setCursorPosition(pos);
			widget->showStyle(story->itemText.charStyle(pos));
			setScope();
		};
		auto pickSolid = [&]() {
			widget->m_solidBtn->click();
			pump();
			widget->m_solidList->setCurrentColor(solid);
			widget->solidColourPicked();
			pump();
		};
		auto preset = [&](const QString& name) {
			for (QToolButton* b : widget->m_presetButtons)
				if (b->property("presetName").toString() == name)
					b->click();
			pump();
		};
		auto undo = [&](int n = 1) { UndoManager::instance()->undo(n); pump(); };
		auto shot = [&](const QString& name) {
			story->itemText.deselectAll();
			view->requestMode(modeNormal);
			doc->m_Selection->clear();
			doc->changed();
			doc->regionsChanged()->update(QRectF());
			view->zoom(qRound(story->xPos() + 250.0), qRound(story->yPos() + 200.0), 1.0, false);
			view->DrawNew();
			pump();
			const QString png = prefix + QStringLiteral("-") + name + QStringLiteral(".png");
			line(QStringLiteral("  picture %1: %2").arg(png, view->viewport()->grab().save(png) ? "saved" : "FAILED"));
		};
		auto panel = [&](const QString& name) {
			const QString png = prefix + QStringLiteral("-popup-") + name + QStringLiteral(".png");
			widget->grab().save(png);
			line(QStringLiteral("  popup %1 | %2").arg(png, widget->m_targetNote->text()));
		};
		const auto target = [&]() { return int(widget->m_target); };

		// 1. a selection, solid: the headline only; one undo step gives everything back
		enter(story);
		selectText(0, headEnd);
		check(QStringLiteral("headline selected: Apply to = Selected text"), target() == TargetSelection && widget->m_targetButtons[TargetSelection]->isEnabled());
		panel(QStringLiteral("selected"));
		pickSolid();
		check(QStringLiteral("solid: headline coloured, body untouched"), head(isSolid) && body(isPlain));
		check(QStringLiteral("selection kept after the change"), story->itemText.hasSelection() && story->itemText.startOfSelection() == 0 && story->itemText.endOfSelection() == headEnd);
		shot(QStringLiteral("1-head-solid"));
		enter(story);
		undo();
		check(QStringLiteral("one undo step: headline back"), head(isPlain) && body(isPlain));

		// 2. gradient on the headline: each line, then whole; each change one undo step
		selectText(0, headEnd);
		widget->m_scope = EachLine;
		preset(QStringLiteral("Red headline"));
		widget->m_scopeButtons[EachLine]->click();
		pump();
		check(QStringLiteral("gradient each line: headline only (mapping %1)").arg(story->itemText.charStyle(0).fillGradientMapping()),
		      head(isGrad) && body(isPlain) && story->itemText.charStyle(0).fillGradientMapping() == 0);
		shot(QStringLiteral("2-head-gradient-line"));
		enter(story);
		selectText(0, headEnd);
		widget->m_scopeButtons[WholeText]->click();
		pump();
		check(QStringLiteral("gradient whole: headline only (mapping %1)").arg(story->itemText.charStyle(0).fillGradientMapping()),
		      head(isGrad) && body(isPlain) && story->itemText.charStyle(0).fillGradientMapping() == 1);
		shot(QStringLiteral("3-head-gradient-whole"));
		enter(story);

		// 3. then the body only: the headline keeps its gradient
		selectText(bodyStart, len - bodyStart);
		pickSolid();
		check(QStringLiteral("body solid: headline keeps its gradient"), head(isGrad) && body(isSolid));
		shot(QStringLiteral("4-body-solid"));
		enter(story);
		selectText(bodyStart, len - bodyStart);
		preset(QStringLiteral("Fire"));
		check(QStringLiteral("body gradient: its own, headline's unchanged (%1 / %2)").arg(story->itemText.charStyle(0).fillGradient(), story->itemText.charStyle(bodyStart).fillGradient()),
		      story->itemText.charStyle(0).fillGradient() == QLatin1String("Red headline") && story->itemText.charStyle(bodyStart).fillGradient() == QLatin1String("Fire"));
		shot(QStringLiteral("5-body-gradient"));
		enter(story);
		undo(4 + 1);   // Fire, body solid, whole, gradient each line + its re-click
		line(QStringLiteral("  undone to: head gradient '%1' body gradient '%2'").arg(story->itemText.charStyle(0).fillGradient(), story->itemText.charStyle(bodyStart).fillGradient()));
		while (!head(isPlain) && UndoManager::instance()->hasUndoActions())
			undo();
		check(QStringLiteral("undo: back to plain"), head(isPlain) && body(isPlain));

		// 4. a cursor, nothing selected: this paragraph only, cursor and "no selection" kept
		const int cursor = bodyStart + 5;
		cursorAt(cursor);
		check(QStringLiteral("cursor in the body: Apply to = This paragraph, Selected text off"),
		      target() == TargetParagraph && !widget->m_targetButtons[TargetSelection]->isEnabled());
		panel(QStringLiteral("cursor"));
		pickSolid();
		check(QStringLiteral("cursor: only that paragraph (headline untouched)"), head(isPlain) && body(isSolid));
		check(QStringLiteral("cursor stays, nothing selected"), !story->itemText.hasSelection() && story->itemText.cursorPosition() == cursor);
		undo();
		cursorAt(2);
		preset(QStringLiteral("Fire"));
		check(QStringLiteral("cursor, gradient: only the headline"), head(isGrad) && body(isPlain));
		undo();
		check(QStringLiteral("gradient: exactly one undo step (cursor)"), head(isPlain) && body(isPlain));
		selectText(0, headEnd);
		preset(QStringLiteral("Red headline"));
		undo();
		check(QStringLiteral("gradient: exactly one undo step (selection)"), head(isPlain) && body(isPlain));

		// 5. Whole text only when chosen, and for that selection only
		selectText(0, headEnd);
		widget->m_targetButtons[TargetWhole]->click();
		pump();
		check(QStringLiteral("choosing Whole text changes nothing yet"), head(isPlain) && body(isPlain));
		pickSolid();
		check(QStringLiteral("Whole text chosen: headline and body"), head(isSolid) && body(isSolid));
		check(QStringLiteral("the user's selection still there"), story->itemText.hasSelection() && story->itemText.endOfSelection() == headEnd);
		undo();
		check(QStringLiteral("undo of Whole text: one step"), head(isPlain) && body(isPlain));
		setScope();
		check(QStringLiteral("popup opened again: back on Selected text"), target() == TargetSelection);
		// 6. the selection goes while the panel says Selected text: narrower or nothing, never the whole text
		story->itemText.deselectAll();
		story->itemText.setCursorPosition(2);
		pickSolid();
		check(QStringLiteral("selection gone: Apply to follows (%1), body untouched").arg(target()), target() == TargetParagraph && body(isPlain));
		undo();

		// 7. frames (not editing): all their text
		view->requestMode(modeNormal);
		pump();
		doc->m_Selection->clear();
		doc->m_Selection->addItem(m1);
		doc->m_Selection->addItem(m2);
		widget->showStyle(m1->itemText.charStyle(0));
		setScope();
		check(QStringLiteral("two frames: Apply to = Whole text, the others off"), target() == TargetWhole && !widget->m_targetButtons[TargetSelection]->isEnabled() && !widget->m_targetButtons[TargetParagraph]->isEnabled());
		panel(QStringLiteral("frames"));
		pickSolid();
		check(QStringLiteral("two frames solid: all the text of both, Story untouched"), allChars(m1, 0, m1->itemText.length(), isSolid) && allChars(m2, 0, m2->itemText.length(), isSolid) && head(isPlain));
		undo();
		check(QStringLiteral("two frames: one undo step"), allChars(m1, 0, m1->itemText.length(), isPlain) && allChars(m2, 0, m2->itemText.length(), isPlain));
		preset(QStringLiteral("Fire"));
		check(QStringLiteral("two frames gradient: both"), allChars(m1, 0, m1->itemText.length(), isGrad) && allChars(m2, 0, m2->itemText.length(), isGrad));
		undo();
		check(QStringLiteral("two frames gradient: exactly one undo step"), allChars(m1, 0, m1->itemText.length(), isPlain) && allChars(m2, 0, m2->itemText.length(), isPlain));
		preset(QStringLiteral("Fire"));
		doc->m_Selection->clear();
		shot(QStringLiteral("6-frames-gradient"));

		// 8. table cells
		PageItem_Table* table = tableItem->asTable();
		auto cell = [&](int r, int c) { return static_cast<PageItem*>(table->cellAt(r, c).textFrame()); };
		auto cellIs = [&](int r, int c, const std::function<bool(const CharStyle&)>& f) { PageItem* x = cell(r, c); return x->itemText.length() > 0 && allChars(x, 0, x->itemText.length(), f); };
		doc->m_Selection->clear();
		doc->m_Selection->addItem(tableItem);
		view->requestMode(modeEditTable);
		pump();
		table->selectCells(0, 0, 0, 1);
		widget->showStyle(cell(0, 0)->itemText.charStyle(0));
		setScope();
		check(QStringLiteral("cells selected: Apply to = Whole text (%1)").arg(table->hasSelection()), table->hasSelection() && target() == TargetWhole);
		panel(QStringLiteral("cells"));
		pickSolid();
		check(QStringLiteral("cells solid: the two selected cells, not the row below"), cellIs(0, 0, isSolid) && cellIs(0, 1, isSolid) && cellIs(1, 0, isPlain) && cellIs(1, 1, isPlain));
		undo();
		check(QStringLiteral("cells: one undo step"), cellIs(0, 0, isPlain) && cellIs(0, 1, isPlain));
		preset(QStringLiteral("Fire"));
		check(QStringLiteral("cells gradient: the two selected cells only"), cellIs(0, 0, isGrad) && cellIs(0, 1, isGrad) && cellIs(1, 0, isPlain));
		undo();
		check(QStringLiteral("cells gradient: exactly one undo step"), cellIs(0, 0, isPlain) && cellIs(0, 1, isPlain));
		preset(QStringLiteral("Fire"));
		table->clearSelection();
		view->requestMode(modeNormal);
		pump();
		doc->m_Selection->clear();
		shot(QStringLiteral("7-cells-gradient"));
		line(failed == 0 ? QStringLiteral("ALL PASS") : QStringLiteral("FAILED: %1").arg(failed));
		rf.close();
		QTimer::singleShot(200, qApp, [failed]() { QApplication::exit(failed ? 1 : 0); });
	});
}

void SuneerTextColourWidget::runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc, SuneerTextColourWidget* widget)
{
	static bool started = false;
	const QString prefix = qEnvironmentVariable("SUNEER_TEXTCOLOUR_SELFTEST");
	if (started || prefix.isEmpty() || !view || !doc || !widget)
		return;
	started = true;
	QTimer::singleShot(2500, widget, [view, doc, widget, prefix]() {
		QFile rf(prefix + QStringLiteral("-report.txt"));
		if (!rf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			return;
		QTextStream ts(&rf);
		int failed = 0;
		auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "TEXTCOLOUR-SELFTEST %s\n", qPrintable(l)); };
		auto check = [&](const QString& what, bool ok) { line(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what)); if (!ok) ++failed; };
		if (ScribusMainWindow* mw = doc->scMW())
			for (UndoGui* g : mw->findChildren<UndoGui*>())
				UndoManager::instance()->removeGui(g);
		auto frame = [&](const QString& name) -> PageItem* {
			for (PageItem* it : doc->DocItems)
				if (it && it->itemName() == name)
					return it;
			return nullptr;
		};
		auto select = [&](PageItem* it) {
			doc->m_Selection->clear();
			doc->m_Selection->addItem(it);
			widget->setDoc(doc);
			widget->showStyle(it->itemText.charStyle(0));
			widget->showItem(it);
		};
		auto pump = [&]() { for (int i = 0; i < 8; ++i) QApplication::processEvents(); };
		auto presetButton = [&](const QString& name) -> QToolButton* {
			for (QToolButton* b : widget->m_presetButtons)
				if (b->property("presetName").toString() == name)
					return b;
			return nullptr;
		};
		PageItem* gLine = frame(QStringLiteral("GradLine"));
		PageItem* gFrame = frame(QStringLiteral("GradFrame"));
		PageItem* gRadial = frame(QStringLiteral("GradRadial"));
		check(QStringLiteral("test frames found"), gLine && gFrame && gRadial);
		if (!(gLine && gFrame && gRadial))
		{
			rf.close();
			QTimer::singleShot(200, qApp, []() { QApplication::exit(2); });
			return;
		}
		const QStringList presets = { QStringLiteral("Gradient"), QStringLiteral("Red headline"), QStringLiteral("Fire") };
		check(QStringLiteral("12 built-in presets"), SuneerGradientPresets::builtIn().count() == 12);
		check(QStringLiteral("preset buttons shown: %1").arg(widget->m_presetButtons.count()), widget->m_presetButtons.count() >= 12);

		// GradLine: preset "Gradient" (diagonal) via its thumbnail, each line
		select(gLine);
		QToolButton* b1 = presetButton(presets[0]);
		check(QStringLiteral("preset button '%1'").arg(presets[0]), b1 != nullptr);
		if (b1) b1->click();
		pump();
		const CharStyle& c1 = gLine->itemText.charStyle(0);
		check(QStringLiteral("GradLine: gradient '%1' angle 45 per line (got '%2' a%3 m%4)").arg(presets[0], c1.fillGradient()).arg(c1.fillGradientAngle()).arg(c1.fillGradientMapping()),
		      c1.fillGradient() == presets[0] && qRound(c1.fillGradientAngle()) == 45 && c1.fillGradientMapping() == 0 && gLine->itemText.charStyle(gLine->itemText.length() - 1).fillGradient() == presets[0]);
		check(QStringLiteral("document has gradient + CMYK colours"), doc->docGradients.contains(presets[0]) && doc->PageColors.contains(QStringLiteral("Grad Pink")) && doc->PageColors[QStringLiteral("Grad Pink")].getColorModel() == colorModelCMYK);
		const int undoBefore = UndoManager::instance()->hasUndoActions() ? 1 : 0;
		check(QStringLiteral("undo step recorded"), undoBefore == 1);

		// GradFrame: "Red headline", direction ↓ then Apply to: Whole text
		select(gFrame);
		if (QToolButton* b2 = presetButton(presets[1])) b2->click();
		pump();
		widget->m_dirButtons[2]->click();   // ↓ 90
		pump();
		widget->m_scopeButtons[0]->click(); // whole text
		pump();
		const CharStyle& c2 = gFrame->itemText.charStyle(0);
		check(QStringLiteral("GradFrame: '%1' 90° whole text (got '%2' a%3 m%4)").arg(presets[1], c2.fillGradient()).arg(c2.fillGradientAngle()).arg(c2.fillGradientMapping()),
		      c2.fillGradient() == presets[1] && qRound(c2.fillGradientAngle()) == 90 && c2.fillGradientMapping() == 1);

		// GradRadial: "Fire" then radial button, then a stop colour edit → own copy
		select(gRadial);
		if (QToolButton* b3 = presetButton(presets[2])) b3->click();
		pump();
		widget->m_dirButtons[8]->click();   // radial
		pump();
		const CharStyle& c3 = gRadial->itemText.charStyle(0);
		check(QStringLiteral("GradRadial: '%1' radial (got '%2' t%3)").arg(presets[2], c3.fillGradient()).arg(c3.fillGradientType()), c3.fillGradient() == presets[2] && c3.fillGradientType() == 1);
		widget->m_bar->setActiveStop(0);
		widget->stopSelected(0);
		widget->m_stopList->setCurrentColor(QStringLiteral("Headline Black"));
		SuneerGradientPresets::ensureGradient(doc, QStringLiteral("Grey headline"));
		widget->m_stopList->setColors(doc->PageColors, false);
		widget->stopColourPicked(QStringLiteral("Headline Black"));
		pump();
		const CharStyle& c4 = gRadial->itemText.charStyle(0);
		const bool ownCopy = c4.fillGradient().startsWith(QLatin1String("Text gradient")) && doc->docGradients.contains(c4.fillGradient())
		                     && doc->docGradients[c4.fillGradient()].colorStops().first()->name == QLatin1String("Headline Black")
		                     && doc->docGradients[presets[2]].colorStops().first()->name == QLatin1String("Grad Dark Red");
		check(QStringLiteral("stop colour edit makes the text's own copy '%1', preset untouched").arg(c4.fillGradient()), ownCopy);
		const QString ownName = c4.fillGradient();
		UndoManager::instance()->undo(1);
		pump();
		check(QStringLiteral("undo: back to '%1', copy removed").arg(presets[2]), gRadial->itemText.charStyle(0).fillGradient() == presets[2] && !doc->docGradients.contains(ownName));
		UndoManager::instance()->redo(1);
		pump();
		check(QStringLiteral("redo: copy back"), gRadial->itemText.charStyle(0).fillGradient() == ownName && doc->docGradients.contains(ownName));

		// Remove gradient on GradRadial → solid, then undo
		widget->removeGradient();
		pump();
		check(QStringLiteral("Remove gradient → solid"), gRadial->itemText.charStyle(0).fillGradient().isEmpty());
		UndoManager::instance()->undo(1);
		pump();
		check(QStringLiteral("undo brings the gradient back"), gRadial->itemText.charStyle(0).fillGradient() == ownName);

		// preset save / rename / delete in the store
		{
			const QString tmpName = QStringLiteral("Selftest preset");
			SuneerGradientPresets::Preset p = SuneerGradientPresets::fromDocument(doc, ownName, 1, 45);
			p.name = tmpName;
			check(QStringLiteral("save preset"), SuneerGradientPresets::save(p) && SuneerGradientPresets::find(tmpName, p) && p.stops.count() == 3);
			check(QStringLiteral("rename preset"), SuneerGradientPresets::rename(tmpName, tmpName + QStringLiteral(" 2")) && SuneerGradientPresets::find(tmpName + QStringLiteral(" 2"), p) && !SuneerGradientPresets::find(tmpName, p));
			check(QStringLiteral("delete preset"), SuneerGradientPresets::remove(tmpName + QStringLiteral(" 2")) && !SuneerGradientPresets::find(tmpName + QStringLiteral(" 2"), p));
			check(QStringLiteral("delete + restore built-in"), SuneerGradientPresets::remove(QStringLiteral("Teal")) && !SuneerGradientPresets::find(QStringLiteral("Teal"), p)
			      && SuneerGradientPresets::save(SuneerGradientPresets::builtIn().at(7)) && SuneerGradientPresets::find(QStringLiteral("Teal"), p));
			line(QStringLiteral("preset file: %1").arg(SuneerGradientPresets::storagePath()));
		}

		// pictures: the popup and the canvas
		select(gLine);
		widget->m_moreBtn->setChecked(false);
		QWidget* top = widget->window();
		if (top && top != widget && qobject_cast<SuneerTextColourPopup*>(top))
			top->show();
		else
			widget->show();
		pump();
		const QString popupPng = prefix + QStringLiteral("-popup.png");
		line(QStringLiteral("picture %1: %2").arg(popupPng, widget->grab().save(popupPng) ? "saved" : "FAILED"));
		widget->m_moreBtn->setChecked(true);
		pump();
		const QString morePng = prefix + QStringLiteral("-popup-more.png");
		line(QStringLiteral("picture %1: %2").arg(morePng, widget->grab().save(morePng) ? "saved" : "FAILED"));
		widget->m_moreBtn->setChecked(false);
		widget->m_solidBtn->click();
		pump();
		const QString solidPng = prefix + QStringLiteral("-popup-solid.png");
		line(QStringLiteral("picture %1: %2").arg(solidPng, widget->grab().save(solidPng) ? "saved" : "FAILED"));
		// the Properties > Text copy of the panel
		if (ScribusMainWindow* mw = doc->scMW())
			for (SuneerTextColourWidget* other : mw->findChildren<SuneerTextColourWidget*>())
				if (other != widget)
				{
					other->setDoc(doc);
					other->showStyle(gLine->itemText.charStyle(0));
					other->showItem(gLine);
					const QString png = prefix + QStringLiteral("-properties.png");
					line(QStringLiteral("picture %1: %2").arg(png, other->grab().save(png) ? "saved" : "FAILED"));
					break;
				}
		UndoManager::instance()->undo(1);   // the solid click
		pump();
		doc->m_Selection->clear();
		doc->changed();
		view->DrawNew();
		pump();
		const double ds = PrefsManager::instance().appPrefs.displayPrefs.displayScale;
		auto shot = [&](const QString& name, PageItem* centre, double pct) {
			view->zoom(qRound(centre->xPos() + centre->width() / 2.0), qRound(centre->yPos() + centre->height() / 2.0), pct / 100.0 * ds, false);
			view->DrawNew();
			pump();
			const QString png = prefix + QStringLiteral("-") + name + QStringLiteral(".png");
			line(QStringLiteral("picture %1: %2").arg(png, view->viewport()->grab().save(png) ? "saved" : "FAILED"));
		};
		shot(QStringLiteral("canvas-line"), gLine, 45);
		shot(QStringLiteral("canvas-frame"), gFrame, 45);
		shot(QStringLiteral("canvas-radial"), gRadial, 45);
		shot(QStringLiteral("canvas-page"), gFrame, 20);
		const QString saveAs = qEnvironmentVariable("SUNEER_TEXTCOLOUR_SAVEAS");
		if (!saveAs.isEmpty())
			check(QStringLiteral("saved %1").arg(saveAs), doc->save(saveAs));
		line(failed == 0 ? QStringLiteral("ALL PASS") : QStringLiteral("FAILED: %1").arg(failed));
		rf.close();
		if (!qEnvironmentVariableIsSet("SUNEER_TEXTCOLOUR_SELFTEST_HOLD"))
			QTimer::singleShot(200, qApp, [failed]() { QApplication::exit(failed ? 1 : 0); });
	});
}
