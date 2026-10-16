/*
suneer: rotation / flip / arrange group for the top control bar.
A thin mirror of Properties → X, Y, Z: rotation field, Flip H, Flip V,
Raise to top, Raise, Lower, Lower to bottom. The panel stays the single
authority: every click is forwarded to the panel's own button (same slot,
same undo, same icon and tooltip), a rotation typed here is handed to the
panel's rotation field, and the displayed state is read back from the panel
on its arrangeDisplayChanged() signal.
*/
#ifndef SUNEERARRANGEBAR_H
#define SUNEERARRANGEBAR_H

#include <QPointer>
#include <QWidget>

class QToolButton;
class ScrSpinBox;
class ScribusDoc;
class ScribusMainWindow;
class PropertiesPalette_XYZ;

class SuneerArrangeBar : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerArrangeBar(ScribusMainWindow* mw, QWidget* parent = nullptr);
	void setDocument(ScribusDoc* doc);
	void setPalette(PropertiesPalette_XYZ* pal);
	//! Esc in the rotation field: put the panel's value back without applying.
	void revert();
	bool owns(const QWidget* w) const;

public slots:
	void refresh();

private slots:
	void onRotationChanged(double v);

private:
	void mirrorButton(QToolButton* mine, QToolButton* theirs, const char* actionName, bool enabled);
	//! Where the first selected item sits among its siblings: can it go up / down?
	void levelRoom(bool& canUp, bool& canDown) const;

	ScribusMainWindow* m_mw {nullptr};
	ScrSpinBox*  m_rotation {nullptr};
	QToolButton* m_flipH {nullptr};
	QToolButton* m_flipV {nullptr};
	QToolButton* m_top {nullptr};
	QToolButton* m_up {nullptr};
	QToolButton* m_down {nullptr};
	QToolButton* m_bottom {nullptr};
	QPointer<ScribusDoc> m_doc;
	QPointer<PropertiesPalette_XYZ> m_pal;
	bool m_updating {false};
};

#endif
