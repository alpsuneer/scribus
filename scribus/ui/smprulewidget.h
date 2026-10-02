/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SMPRULEWIDGET_H
#define SMPRULEWIDGET_H

#include <QList>
#include <QPointer>
#include <QWidget>

#include "styles/paragraphstyle.h"

class QComboBox;
class QLabel;
class QStackedWidget;
class ScribusDoc;
class SMCheckBox;
class SMColorCombo;
class SMScComboBox;
class SMScrSpinBox;
class SMSpinBox;

/**
 The controls of a single rule (rule above or rule below). Both pages hold the
 same set, so everything below can be driven side-agnostically.
 */
struct SMRuleControls
{
	SMCheckBox*   on { nullptr };
	SMScrSpinBox* weight { nullptr };
	SMScComboBox* type { nullptr };
	SMColorCombo* color { nullptr };
	SMCheckBox*   colorIsTextColor { nullptr };
	SMSpinBox*    tint { nullptr };
	SMCheckBox*   overprint { nullptr };
	SMColorCombo* gapColor { nullptr };
	SMCheckBox*   gapColorIsTextColor { nullptr };
	SMSpinBox*    gapTint { nullptr };
	SMCheckBox*   gapOverprint { nullptr };
	SMScComboBox* widthType { nullptr };
	SMScrSpinBox* offset { nullptr };
	SMScrSpinBox* leftIndent { nullptr };
	SMScrSpinBox* rightIndent { nullptr };
	SMCheckBox*   keepInFrame { nullptr };

	QList<QLabel*> labels;
};

/**
 "Paragraph Rules" page of the paragraph style editor: the InDesign style
 rule above / rule below settings, one page each, selected by a combo box.
 */
class SMPRulesWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SMPRulesWidget(QWidget* parent = nullptr);
	~SMPRulesWidget() = default;

	void setDoc(ScribusDoc* doc);
	void languageChange();
	void unitChange(int unitIndex);

	/// Loads the rule attributes of pstyle. parent may be null when !hasParent.
	void showRules(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, int unitIndex);
	void clearAll();

	SMRuleControls above;
	SMRuleControls below;

signals:
	/// Emitted whenever the user touches any rule control.
	void ruleChanged();

protected:
	void changeEvent(QEvent* e) override;

private:
	void buildPage(SMRuleControls& rule, QWidget* page);
	void connectPage(const SMRuleControls& rule);
	void showSide(SMRuleControls& rule, const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, double unitRatio, bool isAbove);
	void fillColorCombos();

	// Self-nulling: the document may be closed while this widget lives on
	// (the control bar's shading popup keeps one). A raw pointer here was
	// the Paste crash of 2026-10-01 (freed PageColors read on UpdateRequest).
	QPointer<ScribusDoc> m_Doc;
	QComboBox* m_sideCombo { nullptr };
	QStackedWidget* m_stack { nullptr };

private slots:
	void handleUpdateRequest(int updateFlags);
	void updateEnabledStates();
};

#endif // SMPRULEWIDGET_H
