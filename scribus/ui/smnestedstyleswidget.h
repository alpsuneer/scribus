/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SMNESTEDSTYLESWIDGET_H
#define SMNESTEDSTYLESWIDGET_H

#include <QList>
#include <QPointer>
#include <QWidget>

#include "styles/paragraphstyle.h"

class QPushButton;
class QTableWidget;
class QToolButton;
class ScribusDoc;

/**
 "Nested Styles" page of the paragraph style editor: the InDesign style ordered
 list of character-style rules that is resolved at layout time.

 Each row is one rule: a character style, whether the run stops before or after
 the delimiter, how many delimiters to count, and the delimiter itself. The
 delimiter cell is an editable combo, so it takes both the named presets and any
 single character the user types, including non-Latin ones.
 */
class SMNestedStylesWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SMNestedStylesWidget(QWidget* parent = nullptr);
	~SMNestedStylesWidget() = default;

	void setDoc(ScribusDoc* doc);
	void languageChange();

	/// Loads the nested style rules of pstyle. parent may be null when !hasParent.
	void showNestedStyles(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent);
	void clearAll();

	/// The rules currently shown, in order.
	QList<ParagraphStyle::NestedStyleRule> rules() const;
	/// True while the style is showing its parent's rules untouched.
	bool useParentValue() const { return m_useParentValue; }

signals:
	/// Emitted whenever the user touches any control.
	void nestedStylesChanged();

protected:
	void changeEvent(QEvent* e) override;

private:
	void setRules(const QList<ParagraphStyle::NestedStyleRule>& rules);
	void insertRow(int row, const ParagraphStyle::NestedStyleRule& rule);
	void fillCharStyleCombo(class QComboBox* combo, const QString& current);
	void fillDelimiterCombo(class QComboBox* combo, const ParagraphStyle::NestedStyleRule& rule);
	ParagraphStyle::NestedStyleRule ruleAt(int row) const;
	/// Marks the style as carrying its own rules and tells the style manager.
	void touched();

	QPointer<ScribusDoc> m_Doc;   // self-nulling, see smpshadewidget.h
	QTableWidget* m_table { nullptr };
	QPushButton* m_addButton { nullptr };
	QPushButton* m_deleteButton { nullptr };
	QToolButton* m_upButton { nullptr };
	QToolButton* m_downButton { nullptr };
	QToolButton* m_parentButton { nullptr };

	QList<ParagraphStyle::NestedStyleRule> m_parentRules;
	bool m_hasParent { false };
	bool m_useParentValue { false };
	/// Suppresses change signals while the table is being repopulated.
	bool m_loading { false };

private slots:
	void updateButtonStates();
	void slotAdd();
	void slotDelete();
	void slotUp();
	void slotDown();
	void slotCellChanged();
	void slotParentClicked();
};

#endif // SMNESTEDSTYLESWIDGET_H
