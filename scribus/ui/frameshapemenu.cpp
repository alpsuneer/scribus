/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QAction>
#include <QEvent>
#include <QHelpEvent>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QToolTip>

#include "ui/frameshapemenu.h"

#include "fpointarray.h"
#include "prefsmanager.h"
#include "prefsstructs.h"
#include "scribusapp.h"
#include "ui/autoformbuttongroup.h"
#include "util_math.h"

namespace
{
	//! How many entries the Recent section keeps.
	constexpr int kRecentCount = 3;
	//! What the button applies before the user has ever chosen anything.
	const char* kDefaultShapeId = "ellipse";

	//! The Autoform table indices this menu reuses rather than regenerating.
	constexpr int kAutoformTriangle = 14;
	constexpr int kAutoformHeart    = 18;

	QList<double> autoformValues(int shapeNumber)
	{
		int count = 0;
		qreal* data = AutoformButtonGroup::getShapeData(shapeNumber, &count);
		QList<double> vals;
		if (!data)
			return vals;
		vals.reserve(count);
		for (int i = 0; i < count; ++i)
			vals << data[i];
		return vals;
	}
}

/* ------------------------------------------------------------------ */
/* FrameShapeDef                                                       */
/* ------------------------------------------------------------------ */

QString FrameShapeDef::name() const
{
	/* Kept as one lookup rather than a string in the table, so the names are
	   retranslated when the UI language changes instead of being frozen at the
	   moment the static catalogue was first built. */
	if (id == "rectangle")    return FrameShapeMenu::tr("Rectangle");
	if (id == "roundedrect")  return FrameShapeMenu::tr("Rounded Rectangle");
	if (id == "ellipse")      return FrameShapeMenu::tr("Ellipse");
	if (id == "triangle")     return FrameShapeMenu::tr("Triangle");
	if (id == "pentagon")     return FrameShapeMenu::tr("Pentagon");
	if (id == "hexagon")      return FrameShapeMenu::tr("Hexagon");
	if (id == "octagon")      return FrameShapeMenu::tr("Octagon");
	if (id == "star5")        return FrameShapeMenu::tr("Star (5-point)");
	if (id == "star6")        return FrameShapeMenu::tr("Star (6-point)");
	if (id == "heart")        return FrameShapeMenu::tr("Heart");
	if (id == "cross")        return FrameShapeMenu::tr("Cross");
	return id;
}

QList<double> FrameShapeDef::values() const
{
	// Both rectangle entries are a rectangle: the rounded one differs only by
	// the corner radius ScribusMainWindow::applyFrameShape() sets afterwards.
	if (id == "rectangle")    return autoformValues(0);
	if (id == "roundedrect")  return autoformValues(0);
	if (id == "ellipse")      return autoformValues(1);
	if (id == "triangle")     return autoformValues(kAutoformTriangle);
	if (id == "pentagon")     return polygonFrameShape(5);
	if (id == "hexagon")      return polygonFrameShape(6);
	if (id == "octagon")      return polygonFrameShape(8);
	if (id == "star5")        return starFrameShape(5, 0.4);
	if (id == "star6")        return starFrameShape(6, 0.4);
	if (id == "heart")        return autoformValues(kAutoformHeart);
	if (id == "cross")        return crossFrameShape(0.30);
	return QList<double>();
}

QList<double> FrameShapeDef::iconValues() const
{
	if (id != "roundedrect")
		return values();
	QPainterPath path;
	const double r = cornerRadiusFraction * 100.0;
	path.addRoundedRect(0.0, 0.0, 100.0, 100.0, r, r);
	return frameShapeValuesFromPath(path);
}

/* ------------------------------------------------------------------ */
/* The catalogue                                                       */
/* ------------------------------------------------------------------ */

const QList<FrameShapeDef>& FrameShapeMenu::catalogue()
{
	/* Ordered the way they are offered, which is roughly how often a page
	   needs them: the three that do most of the work, then the polygons, then
	   the two decorative ones. */
	static const QList<FrameShapeDef> shapes = {
		{ "rectangle",   0,                                0.0  },
		{ "roundedrect", 0,                                0.10 },
		{ "ellipse",     1,                                0.0  },
		{ "triangle",    kAutoformTriangle,                0.0  },
		{ "pentagon",    FrameShapeDef::CustomFrameType,   0.0  },
		{ "hexagon",     FrameShapeDef::CustomFrameType,   0.0  },
		{ "octagon",     FrameShapeDef::CustomFrameType,   0.0  },
		{ "star5",       FrameShapeDef::CustomFrameType,   0.0  },
		{ "star6",       FrameShapeDef::CustomFrameType,   0.0  },
		{ "heart",       kAutoformHeart,                   0.0  },
		{ "cross",       FrameShapeDef::CustomFrameType,   0.0  }
	};
	return shapes;
}

const FrameShapeDef* FrameShapeMenu::shapeById(const QString& id)
{
	const QList<FrameShapeDef>& all = catalogue();
	for (const FrameShapeDef& def : all)
	{
		if (def.id == id)
			return &def;
	}
	return nullptr;
}

QIcon FrameShapeMenu::shapeIcon(const FrameShapeDef& def, int pixmapSize)
{
	/* Drawn from the shape's own control points, the same way
	   AutoformButtonGroup::getIconPixmap() draws the Autoform previews. A
	   hand-drawn SVG per shape would be one more thing to keep in step with the
	   geometry, and would not follow the palette into a dark theme. */
	const QList<double> vals = def.iconValues();
	FPointArray path;
	for (int i = 0; i + 3 < vals.size(); i += 4)
	{
		if (vals[i] < 0)
		{
			path.setMarker();
			continue;
		}
		path.addPoint(28.0 * vals[i]     / 100.0, 28.0 * vals[i + 1] / 100.0);
		path.addPoint(28.0 * vals[i + 2] / 100.0, 28.0 * vals[i + 3] / 100.0);
	}

	QImage ico(32, 32, QImage::Format_ARGB32_Premultiplied);
	ico.fill(0);
	QPainter painter(&ico);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setBrush(ScQApp->palette().color(QPalette::WindowText));
	painter.setPen(QPen(ScQApp->palette().color(QPalette::Midlight), 1.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
	painter.translate(2.0, 2.0);
	painter.drawPath(path.toQPainterPath(true));
	painter.end();

	return QIcon(QPixmap::fromImage(ico.scaled(pixmapSize, pixmapSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
}

/* ------------------------------------------------------------------ */
/* Recent shapes                                                       */
/* ------------------------------------------------------------------ */

QStringList FrameShapeMenu::recentShapeIds()
{
	QStringList ids;
	const QStringList stored = PrefsManager::instance().appPrefs.uiPrefs.recentFrameShapes;
	for (const QString& id : stored)
	{
		// A profile can name a shape this build no longer has; skip it rather
		// than putting an entry in the menu that does nothing when clicked.
		if (shapeById(id) && !ids.contains(id))
			ids << id;
		if (ids.size() >= kRecentCount)
			break;
	}
	return ids;
}

void FrameShapeMenu::noteShapeUsed(const QString& id)
{
	if (!shapeById(id))
		return;
	QStringList ids = recentShapeIds();
	ids.removeAll(id);
	ids.prepend(id);
	while (ids.size() > kRecentCount)
		ids.removeLast();
	PrefsManager::instance().appPrefs.uiPrefs.recentFrameShapes = ids;
}

QString FrameShapeMenu::lastUsedShapeId()
{
	const QStringList ids = recentShapeIds();
	if (!ids.isEmpty())
		return ids.first();
	/* Ellipse rather than Rectangle: a frame is already rectangular, so
	   defaulting to one would make the button look broken the first time it is
	   pressed. */
	return QString::fromLatin1(kDefaultShapeId);
}

/* ------------------------------------------------------------------ */
/* The menu                                                            */
/* ------------------------------------------------------------------ */

FrameShapeMenu::FrameShapeMenu(QWidget* parent) : QMenu(parent)
{
	rebuild();
	connect(ScQApp, SIGNAL(iconSetChanged()), this, SLOT(iconSetChange()));
}

void FrameShapeMenu::rebuild()
{
	clear();
	m_optionsAction = nullptr;

	const QStringList recent = recentShapeIds();
	if (!recent.isEmpty())
	{
		addSection(tr("Recent"));
		for (const QString& id : recent)
		{
			const FrameShapeDef* def = shapeById(id);
			if (!def)
				continue;
			QAction* a = addAction(shapeIcon(*def), def->name());
			a->setIconVisibleInMenu(true);
			a->setData(def->id);
			connect(a, &QAction::triggered, this, [this, id]() { emit shapeChosen(id); });
		}
	}

	addSection(tr("Common Shapes"));
	const QList<FrameShapeDef>& all = catalogue();
	for (const FrameShapeDef& def : all)
	{
		QAction* a = addAction(shapeIcon(def), def.name());
		a->setIconVisibleInMenu(true);
		a->setData(def.id);
		const QString id = def.id;
		connect(a, &QAction::triggered, this, [this, id]() { emit shapeChosen(id); });
	}

	addSeparator();
	m_optionsAction = addAction(tr("More Shapes..."));
	m_optionsAction->setStatusTip(tr("Open the Properties Palette, where every built-in shape and the node editor live"));
	connect(m_optionsAction, &QAction::triggered, this, [this]() { emit optionsRequested(); });
}

void FrameShapeMenu::iconSetChange()
{
	rebuild();
}

void FrameShapeMenu::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::LanguageChange)
		rebuild();
	else
		QMenu::changeEvent(e);
}

/* ------------------------------------------------------------------ */
/* The split button                                                    */
/* ------------------------------------------------------------------ */

FrameShapeToolButton::FrameShapeToolButton(QWidget* parent) : QToolButton(parent)
{
	setPopupMode(QToolButton::MenuButtonPopup);
}

void FrameShapeToolButton::showEvent(QShowEvent* e)
{
	// QToolBar::addWidget() reparents, so the host is only known once shown.
	QWidget* host = parentWidget();
	if (host && (host != m_tooltipHost))
	{
		if (m_tooltipHost)
			m_tooltipHost->removeEventFilter(this);
		host->installEventFilter(this);
		m_tooltipHost = host;
	}
	QToolButton::showEvent(e);
}

bool FrameShapeToolButton::eventFilter(QObject* watched, QEvent* e)
{
	/* A disabled widget receives no mouse events, so Qt hands the tooltip
	   request to the toolbar behind it - and the moment this button most needs
	   to explain itself is the moment it is greyed out. Catch it on the host
	   and answer for the button when the pointer is over it. */
	if ((e->type() == QEvent::ToolTip) && !isEnabled() && (watched == m_tooltipHost))
	{
		auto* he = static_cast<QHelpEvent*>(e);
		const QPoint local = mapFromGlobal(he->globalPos());
		if (rect().contains(local) && !toolTip().isEmpty())
		{
			QToolTip::showText(he->globalPos(), toolTip(), this);
			return true;
		}
	}
	return QToolButton::eventFilter(watched, e);
}

bool FrameShapeToolButton::event(QEvent* e)
{
	if ((e->type() == QEvent::ToolTip) && !m_arrowToolTip.isEmpty())
	{
		auto* he = static_cast<QHelpEvent*>(e);
		QStyleOptionToolButton opt;
		initStyleOption(&opt);
		const QRect arrowRect = style()->subControlRect(QStyle::CC_ToolButton, &opt,
		                                               QStyle::SC_ToolButtonMenu, this);
		QToolTip::showText(he->globalPos(),
		                   arrowRect.contains(he->pos()) ? m_arrowToolTip : toolTip(),
		                   this);
		return true;
	}
	return QToolButton::event(e);
}
