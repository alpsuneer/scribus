/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEERTEXTGRADIENT_H
#define SUNEERTEXTGRADIENT_H

#include <functional>

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "scribusapi.h"

class CharStyle;
class ColorCombo;
class PageItem;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class ScribusDoc;
class ScrSpinBox;
class Selection;

/*! Gradient fill for text that stays text (character style attributes
    FillGradient / FillGradientType / FillGradientAngle / FillGradientMapping):
    Fill Solid / Gradient, a document gradient (Edit > Colours and Fills) or a
    preset, linear with an angle or radial, one gradient per line or across the
    frame's text. With \a ApplyToSelection it applies straight to the text
    selection, the whole frame or the selected table cells, like the other text
    options, one undo step, and also offers the frame's soft shadow. With
    \a StyleEditor it only edits values for the Style Manager (changed() is
    emitted, writeTo() fills the style) and offers "Inherited". */
class SCRIBUS_API SuneerTextGradientWidget : public QWidget
{
	Q_OBJECT
public:
	enum Mode { ApplyToSelection, StyleEditor };

	explicit SuneerTextGradientWidget(Mode mode, QWidget* parent = nullptr);

	void setDoc(ScribusDoc* doc);
	//! Where ApplyToSelection writes; the document selection when unset.
	void setSelectionProvider(const std::function<Selection*()>& provider) { m_selection = provider; }

	//! Shows \a style's values. In StyleEditor mode \a inherited selects "Inherited".
	void showStyle(const CharStyle& style, bool inherited = false);
	//! Shows the soft shadow of \a item (ApplyToSelection only).
	void showItem(PageItem* item);
	//! StyleEditor: the four attributes into \a style (reset when Inherited).
	void writeTo(CharStyle& style) const;

	/*! The built-in and saved presets ("Red headline", "Grey headline", ...): the names. */
	static QStringList presetNames();
	/*! Makes sure \a name is a document gradient: a preset is added with its colours
	    (CMYK, created when missing). True when the gradient exists afterwards. */
	static bool ensureGradient(ScribusDoc* doc, const QString& name);
	/*! Saves the document gradient \a name, with its colours' values, as a preset for
	    every document (prefs context "suneer_textgradient"). */
	static bool savePreset(ScribusDoc* doc, const QString& name);

	/*! SUNEER_TEXTGRAD_SELFTEST=<prefix>: on the opened document, applies gradients
	    through the normal path (itemSelection_ApplyCharStyle) to the frames named
	    GradLine (per line), GradFrame (per frame), GradRadial (radial) and the first
	    word of GradRange (a character range, edit mode); checks undo / redo; saves
	    <prefix>.sla (SUNEER_TEXTGRAD_SAVEAS), canvas pictures <prefix>-*.png and
	    <prefix>-report.txt; then quits unless SUNEER_TEXTGRAD_SELFTEST_HOLD. */
	static void runSelfTestIfAsked(class ScribusView* view, ScribusDoc* doc);

signals:
	void changed();

private:
	void fillGradientCombo(const QString& current);
	void updateEnabled();
	void onFillChanged();
	void apply();
	void applyShadow();
	void savePresetClicked();
	CharStyle currentValues() const;

	Mode m_mode;
	QPointer<ScribusDoc> m_doc;
	std::function<Selection*()> m_selection;
	PageItem* m_item { nullptr };
	bool m_updating { false };

	QComboBox* m_fill { nullptr };
	QComboBox* m_gradient { nullptr };
	QComboBox* m_type { nullptr };
	QSpinBox* m_angle { nullptr };
	QComboBox* m_mapping { nullptr };
	QPushButton* m_savePreset { nullptr };
	QPushButton* m_editGradients { nullptr };
	QLabel* m_gradientLabel { nullptr };
	QLabel* m_typeLabel { nullptr };
	QLabel* m_angleLabel { nullptr };
	QLabel* m_mappingLabel { nullptr };

	QGroupBox* m_shadow { nullptr };
	ScrSpinBox* m_shadowX { nullptr };
	ScrSpinBox* m_shadowY { nullptr };
	ScrSpinBox* m_shadowBlur { nullptr };
	ColorCombo* m_shadowColor { nullptr };
	QSpinBox* m_shadowOpacity { nullptr };
};

#endif
