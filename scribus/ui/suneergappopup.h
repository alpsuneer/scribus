/*
 Frame gap ("pad") controls of the Suneer control bar.

 The toolbar keeps four gap buttons: + / - (all sides), Reset and "Gap sides".
 "Gap sides" opens SuneerGapPopup: a small frame diagram with - / value / +
 on each side, a "link all sides" toggle, the step and Reset. The popup holds
 no document logic; the control bar fills in the callbacks with its existing
 slots, so every click or typed value is the same undo step the toolbar
 buttons make.

 The +/- glyphs (and the "Gap sides" icon) are SVG generated per rendered size
 and coloured with the theme's button text colour at paint time.
*/
#ifndef SUNEERGAPPOPUP_H
#define SUNEERGAPPOPUP_H

#include <functional>

#include <QIcon>
#include <QString>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QToolButton;

//! sign: '+', '-', or 's' (the "Gap sides" icon: a frame with gap bars and a small down arrow);
//! arrow: 0, or 'u' 'd' 'l' 'r' for a direction arrow before the sign.
QIcon suneerGapGlyphIcon(char sign, char arrow = 0);
//! Gives a button the bold glyph (button size unchanged, text removed).
void suneerSetGapGlyph(QToolButton* btn, char sign, char arrow = 0);

class SuneerGapPopup : public QWidget
{
	Q_OBJECT
public:
	enum Side { Top = 0, Bottom = 1, Left = 2, Right = 3, All = -1 };   // the control bar's side numbers

	struct Values
	{
		bool valid {false};      // something selected that has this gap
		bool editable {true};    // false: the gap is a contour line, only +/- work
		double mm[4] {0, 0, 0, 0};
		QString title;
	};

	explicit SuneerGapPopup(QWidget* parent = nullptr);

	std::function<Values()> readValues;
	std::function<void(int side, double deltaMm)> stepSide;   // side All = every side
	std::function<void(int side, double mm)> setSide;         // side All = every side
	std::function<void()> reset;

	//! Remembered step per gap kind (QSettings Scribus/SuneerGap/step_<key>).
	void setStepKey(const QString& key, double defaultMm);
	void popupBelow(QWidget* anchor);
	void refresh();

protected:
	bool eventFilter(QObject* obj, QEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;
	void paintEvent(QPaintEvent* e) override;

private:
	void onStep(int side, double sign);
	void onTyped(int side);
	bool linked() const;

	QLabel* m_title {nullptr};
	QDoubleSpinBox* m_value[4] {nullptr, nullptr, nullptr, nullptr};
	QToolButton* m_minus[4] {nullptr, nullptr, nullptr, nullptr};
	QToolButton* m_plus[4] {nullptr, nullptr, nullptr, nullptr};
	QToolButton* m_link {nullptr};
	QDoubleSpinBox* m_step {nullptr};
	QToolButton* m_reset {nullptr};
	QString m_stepKey;
	bool m_refreshing {false};
};

#endif
