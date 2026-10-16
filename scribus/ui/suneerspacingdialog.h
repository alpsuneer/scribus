/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERSPACINGDIALOG_H
#define SUNEERSPACINGDIALOG_H

#include <functional>
#include <QDialog>
#include <QList>

#include "scribusapi.h"
#include "suneerspacing.h"
#include "ui/preferences/prefs_pane.h"

class QCheckBox;
class QKeyEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;
class ScribusDoc;

//! The table: global value + per model "use global" / override.
class SCRIBUS_API SuneerSpacingEditor : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerSpacingEditor(QWidget* parent = nullptr);
	void load(const SuneerSpacing::Store& s);
	SuneerSpacing::Store current() const;
signals:
	void changed();
private:
	struct Cell { QCheckBox* useGlobal { nullptr }; QDoubleSpinBox* spin { nullptr }; };
	QTableWidget* m_table { nullptr };
	QDoubleSpinBox* m_global[SuneerSpacing::KeyCount] {};
	QList<QList<Cell>> m_cells;     // [model][key]
	QLineEdit* m_ruleColor { nullptr };
	bool m_loading { false };
};

//! Preferences > Faircode Layout: Spacing
class SCRIBUS_API Prefs_SuneerSpacing : public Prefs_Pane
{
	Q_OBJECT
public:
	Prefs_SuneerSpacing(QWidget* parent, ScribusDoc* doc = nullptr);
	void restoreDefaults(struct ApplicationPrefs* prefsData) override;
	void saveGuiToPrefs(struct ApplicationPrefs* prefsData) const override;
private:
	SuneerSpacingEditor* m_editor { nullptr };
};

/*!
 \brief The same editor as a dialog (News Browser "Spacing..."): live preview
 on the current page (debounced), "Apply to this page", Cancel restores.
 */
class SCRIBUS_API SuneerSpacingDialog : public QDialog
{
	Q_OBJECT
public:
	struct PageHooks
	{
		std::function<QString()> pageModel;                                     //!< the model the page uses
		std::function<void()> snapshot;                                         //!< remember the page
		std::function<void()> restore;                                          //!< put the page back
		std::function<int(const SuneerSpacing::Values&, bool final)> apply;     //!< re-space; final = one undo step + rules
	};
	SuneerSpacingDialog(const PageHooks& hooks, QWidget* parent = nullptr);

protected:
	void reject() override;
	void accept() override;
	void keyPressEvent(QKeyEvent* e) override;

private:
	void schedulePreview();
	void preview();
	void applyToPage();

	SuneerSpacingEditor* m_editor { nullptr };
	PageHooks m_hooks;
	QCheckBox* m_liveChk { nullptr };
	QLabel* m_info { nullptr };
	QTimer* m_timer { nullptr };
	bool m_previewing { false };
	SuneerSpacing::Store m_original;
};

#endif
