/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SMPSHADEWIDGET_H
#define SMPSHADEWIDGET_H

#include <QList>
#include <QPointer>
#include <QWidget>

#include "styles/paragraphstyle.h"

class QLabel;
class ScribusDoc;
class SMCheckBox;
class SMColorCombo;
class SMScComboBox;
class SMScrSpinBox;
class SMSpinBox;

/**
 "Paragraph Shading" page of the paragraph style editor: a background band
 behind the paragraph with padding, corner radius and merge-adjacent control,
 the InDesign paragraph shading feature.
 */
class SMPShadeWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SMPShadeWidget(QWidget* parent = nullptr);
	~SMPShadeWidget() = default;

	void setDoc(ScribusDoc* doc);
	void languageChange();
	void unitChange(int unitIndex);

	/// Loads the shading attributes of pstyle. parent may be null when !hasParent.
	void showShade(const ParagraphStyle* pstyle, const ParagraphStyle* parent, bool hasParent, int unitIndex);
	void clearAll();

	SMCheckBox*   on { nullptr };
	SMColorCombo* color { nullptr };
	SMSpinBox*    tint { nullptr };
	SMScComboBox* widthType { nullptr };
	SMScrSpinBox* padTop { nullptr };
	SMScrSpinBox* padBottom { nullptr };
	SMScrSpinBox* padLeft { nullptr };
	SMScrSpinBox* padRight { nullptr };
	SMScrSpinBox* cornerRadius { nullptr };
	SMCheckBox*   mergeAdjacent { nullptr };

signals:
	/// Emitted whenever the user touches any shading control.
	void shadeChanged();

protected:
	void changeEvent(QEvent* e) override;

private:
	void fillColorCombo();

	// Self-nulling: the document may be closed while this widget lives on
	// (the control bar's shading popup keeps one). A raw pointer here was
	// the Paste crash of 2026-10-01 (freed PageColors read on UpdateRequest).
	QPointer<ScribusDoc> m_Doc;
	QList<QLabel*> m_labels;

private slots:
	void handleUpdateRequest(int updateFlags);
	void updateEnabledStates();
};

#endif // SMPSHADEWIDGET_H
