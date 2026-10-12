/*
suneer: frame Width / Height fields for the top control bar.

A thin mirror of Properties → X, Y, Z → W / H. The Properties panel stays the
single authority: values, unit, decimals, enabled state and the ratio lock are
read from it on its sizeDisplayChanged() signal, and a value typed here is
handed back to it (applyWidthFromToolbar / applyHeightFromToolbar), so the
resize follows exactly the same path as the panel — same base point, groups,
lines, text re-layout, image scaling, welds and undo.
*/
#ifndef SUNEERFRAMESIZEBAR_H
#define SUNEERFRAMESIZEBAR_H

#include <QWidget>
#include <QPointer>

class QLabel;
class LinkButton;
class ScrSpinBox;
class ScribusDoc;
class PropertiesPalette_XYZ;

class SuneerFrameSizeBar : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerFrameSizeBar(QWidget* parent = nullptr);

	void setDocument(ScribusDoc* doc);
	void setPalette(PropertiesPalette_XYZ* pal);
	//! Esc: put the panel's value back without applying.
	void revert();
	//! Rubber-band size while drawing a new frame: shown read-only, in the document unit.
	void showDrawingSize(double wPt, double hPt);
	void endDrawingSize();
	bool owns(const QWidget* w) const;
	ScrSpinBox* widthSpin() const { return m_w; }
	ScrSpinBox* heightSpin() const { return m_h; }

public slots:
	void refresh();
	void iconSetChange();
	void languageChange();

private slots:
	void onWidthChanged(double v);
	void onHeightChanged(double v);
	void onLockToggled(bool on);

private:
	QLabel*     m_icon  {nullptr};
	QLabel*     m_wLbl  {nullptr};
	QLabel*     m_hLbl  {nullptr};
	ScrSpinBox* m_w     {nullptr};
	ScrSpinBox* m_h     {nullptr};
	LinkButton* m_lock  {nullptr};
	QPointer<ScribusDoc> m_doc;
	QPointer<PropertiesPalette_XYZ> m_pal;
	bool m_updating {false};
	int  m_unitIndex {-1};
	bool m_drawing {false};
};

#endif
