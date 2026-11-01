/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERTEXTCOLOUR_H
#define SUNEERTEXTCOLOUR_H

#include <functional>

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "scribusapi.h"
#include "undoobject.h"
#include "vgradient.h"

class CharStyle;
class ColorListBox;
class PageItem;
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class ScrSpinBox;
class ScribusDoc;
class Selection;
class UndoState;

/*! The text gradient presets shared by every document: the 12 built-in ones
    plus the user's own, in <prefs>/suneer-text-gradients.json (copy the file
    to another PC to share them). A built-in preset can be renamed or deleted
    too (the file then hides it). */
namespace SuneerGradientPresets
{
	struct Stop
	{
		double pos { 0.0 };
		QString color;            //!< document colour name (created as CMYK when missing)
		int c { 0 }, m { 0 }, y { 0 }, k { 0 };   //!< CMYK 0..255
		int shade { 100 };
		double opacity { 1.0 };
	};
	struct Preset
	{
		QString name;
		int type { 0 };           //!< 0 linear, 1 radial
		double angle { 90.0 };    //!< 0 = left to right, 90 = top to bottom
		QList<Stop> stops;
		bool builtIn { false };
	};

	SCRIBUS_API QString storagePath();
	SCRIBUS_API QList<Preset> builtIn();
	//! Built-in (minus the hidden ones, with saved overrides) then the saved ones.
	SCRIBUS_API QList<Preset> all();
	SCRIBUS_API bool find(const QString& name, Preset& out);
	//! Saves or replaces \a preset (a built-in name is overridden).
	SCRIBUS_API bool save(const Preset& preset);
	SCRIBUS_API bool remove(const QString& name);
	SCRIBUS_API bool rename(const QString& oldName, const QString& newName);
	//! \a doc's gradient \a name with its colours as a preset.
	SCRIBUS_API Preset fromDocument(ScribusDoc* doc, const QString& name, int type, double angle);
	//! Adds the preset \a name to \a doc (colours created as CMYK). True when the gradient exists afterwards.
	SCRIBUS_API bool ensureGradient(ScribusDoc* doc, const QString& name);
	//! The preset as a VGradient with display colours (no document needed).
	SCRIBUS_API VGradient toGradient(const Preset& p, ScribusDoc* doc);
	SCRIBUS_API QPixmap thumbnail(const Preset& p, const QSize& size, ScribusDoc* doc, bool selected);
}

/*! The colour bar: the gradient with round stops under it. Click a stop to
    select it, drag to move it, double-click the bar to add one. */
class SCRIBUS_API SuneerGradientBar : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerGradientBar(QWidget* parent = nullptr);

	void setGradient(const VGradient& g);
	const VGradient& gradient() const { return m_gradient; }
	int activeStop() const { return m_active; }
	void setActiveStop(int i);
	int stopCount() const { return m_gradient.stops(); }
	//! Position (0..1) of stop \a i.
	double stopPosition(int i) const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

signals:
	void stopSelected(int index);
	//! A stop moved (mouse released) or was added by a double-click.
	void stopsChanged();
	//! While a stop is being dragged (preview only).
	void stopMoving();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;

private:
	QRect barRect() const;
	int xOf(double pos) const;
	double posOf(int x) const;
	int stopAt(const QPoint& p) const;
	void moveStop(int index, double pos);

	VGradient m_gradient;
	int m_active { 0 };
	int m_dragging { -1 };
	bool m_moved { false };
};

/*! The preview strip: the selected text (or "മിസ്റ്റർ ട്രംപ്") in the
    text's font, filled with the gradient or the solid colour. */
class SCRIBUS_API SuneerTextPreview : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerTextPreview(QWidget* parent = nullptr);
	void setText(const QString& text);
	void setFontFamily(const QString& family, bool bold);
	void setSolid(const QColor& c);
	void setGradient(const VGradient& g, int type, double angle, bool perLine);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QString m_text;
	QString m_family;
	bool m_bold { true };
	bool m_isGradient { false };
	QColor m_solid { Qt::black };
	VGradient m_gradient;
	int m_type { 0 };
	double m_angle { 90.0 };
	bool m_perLine { true };
};

/*! Undo of a document gradient's stops (the colour bar edits). One state per
    change; the text's character-style change is in the same transaction. */
class SCRIBUS_API SuneerGradientUndo : public QObject, public UndoObject
{
	Q_OBJECT
public:
	explicit SuneerGradientUndo(QObject* parent = nullptr);
	void setDoc(ScribusDoc* doc) { m_doc = doc; }
	//! Records docGradients[name]: old → new (old empty = created) inside the current transaction.
	void record(const QString& name, const VGradient* oldGradient, const VGradient& newGradient);
	void restore(UndoState* state, bool isUndo) override;
	QString getUName() const override;

signals:
	void restored();

private:
	QPointer<ScribusDoc> m_doc;
};

/*! The "Text colour" panel (Adobe Express style): Solid | Gradient, preset
    thumbnails, live preview, colour bar with 2–4 stops, direction arrows,
    Apply to, More… (angle, shade, opacity, soft shadow). Every change applies
    at once to the text selection, the whole frame(s) or the selected table
    cells - one undo step each. Shown in the toolbar text-colour popup and in
    Properties > Text. The gradient engine (character style FillGradient*,
    document gradients) is unchanged. */
class SCRIBUS_API SuneerTextColourWidget : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerTextColourWidget(QWidget* parent = nullptr);

	void setDoc(ScribusDoc* doc);
	//! Where the changes go; the document selection when unset.
	void setSelectionProvider(const std::function<Selection*()>& provider) { m_selection = provider; }
	//! Shows \a style (the text at the cursor / the frame's first character).
	void showStyle(const CharStyle& style);
	//! The item whose text and soft shadow are shown.
	void showItem(PageItem* item);

	bool isGradient() const;
	QString gradientName() const { return m_gradName; }

	/*! SUNEER_TEXTCOLOUR_SELFTEST=<prefix>: with a document open, applies three
	    presets to the frames GradLine / GradFrame / GradRadial through this widget
	    (preset click, direction button, Apply to), checks undo, grabs the popup
	    <prefix>-popup.png and the canvas <prefix>-canvas.png, writes <prefix>-report.txt. */
	static void runSelfTestIfAsked(class ScribusView* view, ScribusDoc* doc, SuneerTextColourWidget* widget);

signals:
	void changed();
	//! Apply pressed: a popup closes.
	void closeRequested();

private:
	enum Scope { WholeText = 0, EachLine = 1, SelectedWords = 2 };

	void buildUi();
	void rebuildPresets();
	void presetClicked(const QString& name);
	void presetMenu(const QString& name, const QPoint& globalPos);
	void setModeGradient(bool gradient, bool applyNow);
	void directionClicked(int angle, bool radial);
	void scopeClicked(int scope);
	void stopSelected(int index);
	void stopColourPicked(const QString& colour);
	void stopShadeChanged();
	void addCmykColour();
	void addStop();
	void swapStops();
	void removeStop();
	void stopsEdited(bool apply);
	void solidColourPicked();
	void savePresetAsked();
	void nameRowDone();
	void removeGradient();
	void applyShadow();
	void toggleMore();

	void updatePreview();
	void updateStopEditor();
	void updateDirectionButtons();
	void updateScopeButtons();
	void syncButtonsFromState();
	bool canApply() const;
	//! The char style this panel describes, applied to the target: one undo step.
	void apply(const QString& undoName = QString());
	//! Makes the current gradient one of this text's own (a copy when a preset / another use shares it).
	void ownGradient();
	bool gradientUsedElsewhere(const QString& name, const QList<PageItem*>& except) const;
	QString uniqueGradientName() const;
	QList<PageItem*> targetItems() const;
	bool selectionScopePossible() const;
	QColor stopDisplayColour(const QString& name, int shade, double opacity) const;
	VGradient currentGradient() const;
	void writeGradient(const VGradient& g, const QString& undoName);

	QPointer<ScribusDoc> m_doc;
	std::function<Selection*()> m_selection;
	PageItem* m_item { nullptr };
	SuneerGradientUndo* m_undo { nullptr };
	bool m_updating { false };

	// state shown
	bool m_gradient { false };
	QString m_gradName;
	int m_type { 0 };
	double m_angle { 90.0 };
	int m_scope { EachLine };
	QString m_solidColour;
	double m_solidShade { 100.0 };

	// widgets
	QToolButton* m_solidBtn { nullptr };
	QToolButton* m_gradientBtn { nullptr };
	SuneerTextPreview* m_preview { nullptr };
	QStackedWidget* m_pages { nullptr };
	// solid page
	ColorListBox* m_solidList { nullptr };
	QSpinBox* m_solidShadeSpin { nullptr };
	// gradient page
	QWidget* m_presetGrid { nullptr };
	QGridLayout* m_presetLayout { nullptr };
	QList<QToolButton*> m_presetButtons;
	QToolButton* m_presetAdd { nullptr };
	SuneerGradientBar* m_bar { nullptr };
	QPushButton* m_addStop { nullptr };
	QPushButton* m_swapStops { nullptr };
	QPushButton* m_removeStop { nullptr };
	QWidget* m_stopEditor { nullptr };
	QLabel* m_stopLabel { nullptr };
	ColorListBox* m_stopList { nullptr };
	QSpinBox* m_cmyk[4] { nullptr, nullptr, nullptr, nullptr };
	QPushButton* m_cmykAdd { nullptr };
	QList<QToolButton*> m_dirButtons;   //!< 8 arrows then radial
	QList<QToolButton*> m_scopeButtons;
	QPushButton* m_moreBtn { nullptr };
	QWidget* m_more { nullptr };
	QSpinBox* m_angleSpin { nullptr };
	QSpinBox* m_stopShade { nullptr };
	QSpinBox* m_stopOpacity { nullptr };
	QGroupBox* m_shadow { nullptr };
	ScrSpinBox* m_shadowX { nullptr };
	ScrSpinBox* m_shadowY { nullptr };
	ScrSpinBox* m_shadowBlur { nullptr };
	class ColorCombo* m_shadowColor { nullptr };
	QSpinBox* m_shadowOpacity { nullptr };
	// bottom row + inline name row
	QPushButton* m_savePreset { nullptr };
	QPushButton* m_removeGradient { nullptr };
	QPushButton* m_apply { nullptr };
	QWidget* m_nameRow { nullptr };
	QLabel* m_nameLabel { nullptr };
	QLineEdit* m_nameEdit { nullptr };
	QString m_renameFrom;   //!< name row renames this preset; empty = save new
};

/*! A frameless popup holding the panel, under a toolbar button; closes on a
    click outside or on Apply. */
class SCRIBUS_API SuneerTextColourPopup : public QFrame
{
	Q_OBJECT
public:
	explicit SuneerTextColourPopup(SuneerTextColourWidget* widget, QWidget* parent = nullptr);
	SuneerTextColourWidget* widget() const { return m_widget; }
	void showBelow(QWidget* anchor);

private:
	SuneerTextColourWidget* m_widget;
};

#endif
