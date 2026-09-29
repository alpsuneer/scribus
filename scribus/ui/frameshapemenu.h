/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef FRAMESHAPEMENU_H
#define FRAMESHAPEMENU_H

#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QToolButton>

#include "scribusapi.h"

class QAction;
class QEvent;
class QShowEvent;

/*!
 \brief One entry of the Frame Shape menu.

 \a frameType is what ScribusDoc::item_setFrameShape() is handed, and it means
 what it means everywhere else in Scribus: 0 is a rectangle, 1 an ellipse, and
 anything else selects a custom outline and is stored on the item as
 frameType + 2. That stored number is also read back by the Properties Palette
 to pick the picture on its shape button, so where a shape has a true
 equivalent in the Autoform table it carries that table's index - Heart is 18,
 Triangle is 14 - and the palette shows the right picture. The shapes with no
 equivalent there carry FrameShapeDef::CustomFrameType, which is outside the
 table on purpose: the palette then falls back to its default picture instead
 of naming a shape this is not.
 */
struct SCRIBUS_API FrameShapeDef
{
	//! Outside the Autoform table (0..44), for shapes it has no picture of.
	static constexpr int CustomFrameType = 90;

	QString id;                     //!< Stable, stored in prefs, never translated.
	int     frameType { 0 };
	//! Corner radius as a fraction of the frame's shorter side. Rounded rectangle only.
	double  cornerRadiusFraction { 0.0 };

	//! Translated, for the menu.
	QString name() const;
	/*! \brief The control points handed to ScribusDoc::item_setFrameShape().

	    For frameType 0 and 1 item_setFrameShape() rebuilds the outline itself,
	    from PageItem::SetRectFrame() and SetOvalFrame(), and does not read these
	    to draw with - but it does record them as the shape a *redo* restores.
	    So these have to be the geometry those two produce, not something that
	    merely looks like it, or redo would put back an outline the frame never
	    had. */
	QList<double> values() const;
	/*! \brief The control points the menu draws its icon from.

	    The same as values() except for the rounded rectangle, whose corners are
	    a radius on a rectangle rather than a shape - there is nothing in
	    values() to draw, and two identical rectangles in the menu would be no
	    help in telling the two entries apart. */
	QList<double> iconValues() const;
};

/*!
 \brief The dropdown half of the Frame Shape split button.

 Recent shapes come first, then the eleven common ones, then a way through to
 the Properties Palette for everything this menu deliberately leaves out.
 */
class SCRIBUS_API FrameShapeMenu : public QMenu
{
	Q_OBJECT

public:
	explicit FrameShapeMenu(QWidget* parent = nullptr);

	static const QList<FrameShapeDef>& catalogue();
	static const FrameShapeDef* shapeById(const QString& id);
	//! Drawn from the shape's own geometry, so an icon cannot disagree with what it applies.
	static QIcon shapeIcon(const FrameShapeDef& def, int pixmapSize = 16);

	static QStringList recentShapeIds();
	static void noteShapeUsed(const QString& id);
	//! What the main half of the button applies: the last shape used, or Ellipse.
	static QString lastUsedShapeId();

	//! Rebuild the entries, picking up any change to the recent list.
	void rebuild();

signals:
	void shapeChosen(const QString& id);
	void optionsRequested();

protected:
	void changeEvent(QEvent* e) override;

private slots:
	void iconSetChange();

private:
	QAction* m_optionsAction { nullptr };
};

/*!
 \brief The toolbar button: click the face to apply, click the arrow to choose.

 QToolButton has one tooltip and this button has two jobs, so the tooltip is
 answered per half - the face says which shape it will apply, the arrow says it
 opens the picker. Without this a user hovering the arrow is told about the
 shape it is not going to apply.
 */
class SCRIBUS_API FrameShapeToolButton : public QToolButton
{
	Q_OBJECT

public:
	explicit FrameShapeToolButton(QWidget* parent = nullptr);
	void setArrowToolTip(const QString& tip) { m_arrowToolTip = tip; }

protected:
	bool event(QEvent* e) override;
	bool eventFilter(QObject* watched, QEvent* e) override;
	void showEvent(QShowEvent* e) override;

private:
	QString m_arrowToolTip;
	QPointer<QWidget> m_tooltipHost;
};

#endif
