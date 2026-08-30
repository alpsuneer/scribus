/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AITEXTRESULTDIALOG_H
#define AITEXTRESULTDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

#include "ai/aitextservice.h"
#include "scribusapi.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTextEdit;

/*!
 \brief What the model wrote, and what the user may do with it.

 The gate between a model and the document. Nothing an AI produces reaches a
 frame without passing through here first and being chosen by a person - that
 is the whole reason this is modal and has no "apply automatically".

 Several results are shown as a list to pick from, one is shown as an editable
 block. Editable on purpose: a headline that is nearly right is the common
 case, and making the user copy it out to a text editor to fix one word would
 be worse than not offering it.

 The footer says which model answered and what it cost, because on these
 providers every press of Regenerate spends money and the user should be able
 to see that accumulating rather than discover it on a bill.
 */
class SCRIBUS_API AITextResultDialog : public QDialog
{
	Q_OBJECT

public:
	//! What the user chose to do. The caller performs it; this dialog only
	//! ever reports, so that undo and document access stay in one place.
	enum Action
	{
		Nothing,         //!< Closed or cancelled
		Replace,         //!< Put it in the selected frame, replacing what is there
		InsertNewFrame,  //!< Put it in a new frame below the selected one
		CopyOnly         //!< Already copied to the clipboard; nothing to do
	};

	/*! \param task one of the AITextService::Task* constants, used only to
	           title the window in the user's terms. */
	AITextResultDialog(QWidget* parent, const QString& task,
	                   const AITextService::Response& response);
	~AITextResultDialog() override;

	//! What the user picked, after exec() returns.
	Action chosenAction() const { return m_action; }
	//! The text they picked, as they left it - edits included.
	QString chosenText() const;

	//! Show a fresh answer in place of the current one, after Regenerate.
	void setResponse(const AITextService::Response& response);
	//! Grey the buttons while a regeneration is in flight.
	void setBusy(bool busy);
	//! Show a failure without closing, so the earlier answer is not lost.
	void showError(const QString& error);

	//! Human-readable name for a task, e.g. "Suggested headlines".
	static QString titleForTask(const QString& task);

signals:
	//! The user pressed Regenerate. The caller runs the request again and
	//! calls setResponse() or showError().
	void regenerateRequested();

private slots:
	void copyToClipboard();
	void replaceInFrame();
	void insertAsNewFrame();

private:
	void rebuild(const AITextService::Response& response);
	//! The currently selected result, taken from the editor so that whatever
	//! the user typed is what they get.
	QString currentText() const;

	QListWidget* m_options {nullptr};   //!< Only built when there is a choice
	QTextEdit* m_editor {nullptr};
	QLabel* m_footer {nullptr};
	QLabel* m_error {nullptr};
	QPushButton* m_copyButton {nullptr};
	QPushButton* m_replaceButton {nullptr};
	QPushButton* m_newFrameButton {nullptr};
	QPushButton* m_regenerateButton {nullptr};

	QStringList m_results;
	Action m_action {Nothing};
	QString m_task;
};

#endif
