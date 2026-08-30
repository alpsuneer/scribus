/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/aitextresultdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

AITextResultDialog::AITextResultDialog(QWidget* parent, const QString& task,
                                       const AITextService::Response& response)
	: QDialog(parent),
	  m_task(task)
{
	setWindowTitle(titleForTask(task));
	setModal(true);
	resize(560, 420);

	auto* layout = new QVBoxLayout(this);

	// Built once and hidden when there is only one answer, rather than
	// rebuilt per response: Regenerate can change how many results come back,
	// and a dialog that re-lays itself out under the cursor is unpleasant.
	m_options = new QListWidget(this);
	m_options->setAlternatingRowColors(true);
	m_options->setMaximumHeight(140);
	layout->addWidget(m_options);

	m_editor = new QTextEdit(this);
	m_editor->setAcceptRichText(false);
	// Editable on purpose: a headline that is nearly right is the common case,
	// and sending the user to another window to fix one word would be worse
	// than not offering it at all.
	m_editor->setToolTip(tr("You can edit this before inserting it."));
	layout->addWidget(m_editor, 1);

	m_error = new QLabel(this);
	m_error->setWordWrap(true);
	m_error->setVisible(false);
	layout->addWidget(m_error);

	m_footer = new QLabel(this);
	m_footer->setWordWrap(true);
	m_footer->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_footer);

	auto* buttons = new QHBoxLayout;
	m_regenerateButton = new QPushButton(tr("&Regenerate"), this);
	m_regenerateButton->setToolTip(tr("Ask again. This sends the same request and costs "
	                                  "the same again."));
	m_copyButton = new QPushButton(tr("&Copy"), this);
	m_replaceButton = new QPushButton(tr("Replace &Frame Text"), this);
	m_replaceButton->setToolTip(tr("Replace everything in the selected frame with this text."));
	m_newFrameButton = new QPushButton(tr("Insert as &New Frame"), this);
	auto* closeButton = new QPushButton(tr("Cl&ose"), this);

	buttons->addWidget(m_regenerateButton);
	buttons->addStretch(1);
	buttons->addWidget(m_copyButton);
	buttons->addWidget(m_replaceButton);
	buttons->addWidget(m_newFrameButton);
	buttons->addWidget(closeButton);
	layout->addLayout(buttons);

	connect(m_options, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row >= 0 && row < m_results.size())
			m_editor->setPlainText(m_results.at(row));
	});
	connect(m_regenerateButton, &QPushButton::clicked, this, [this]() {
		m_error->setVisible(false);
		emit regenerateRequested();
	});
	connect(m_copyButton, &QPushButton::clicked, this, &AITextResultDialog::copyToClipboard);
	connect(m_replaceButton, &QPushButton::clicked, this, &AITextResultDialog::replaceInFrame);
	connect(m_newFrameButton, &QPushButton::clicked, this, &AITextResultDialog::insertAsNewFrame);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

	rebuild(response);
}

AITextResultDialog::~AITextResultDialog() = default;

QString AITextResultDialog::titleForTask(const QString& task)
{
	if (task == QLatin1String(AITextService::TaskCaption))
		return tr("Suggested Caption");
	if (task == QLatin1String(AITextService::TaskHeadline))
		return tr("Suggested Headlines");
	if (task == QLatin1String(AITextService::TaskSummarize))
		return tr("Summary");
	if (task == QLatin1String(AITextService::TaskTranslate))
		return tr("Translation");
	if (task == QLatin1String(AITextService::TaskImprove))
		return tr("Improved Text");
	if (task == QLatin1String(AITextService::TaskAltText))
		return tr("Alt Text");
	return tr("AI Result");
}

void AITextResultDialog::rebuild(const AITextService::Response& response)
{
	m_results = response.results;

	m_options->clear();
	// A list of one is not a choice, so it is not shown as one.
	const bool several = m_results.size() > 1;
	m_options->setVisible(several);
	if (several)
	{
		for (const QString& result : std::as_const(m_results))
			m_options->addItem(result);
		m_options->setCurrentRow(0);
	}
	m_editor->setPlainText(m_results.isEmpty() ? QString() : m_results.first());

	QStringList footer;
	if (!response.modelDisplayName.isEmpty())
		footer << tr("Model: %1").arg(response.modelDisplayName);
	if (response.tokensUsed > 0)
		footer << tr("Tokens: %1").arg(response.tokensUsed);
	// Only quote a cost when there is a verified price to quote. A made-up
	// number on a line the user will believe is worse than no number, so a
	// provider whose rates are not in the table simply omits this.
	if (response.estimatedCost > 0.0)
		footer << tr("about $%1").arg(response.estimatedCost, 0, 'f', 4);
	m_footer->setText(QStringLiteral("<qt><i>%1</i></qt>")
	                  .arg(footer.join(QStringLiteral(" &middot; ")).toHtmlEscaped()));
}

void AITextResultDialog::setResponse(const AITextService::Response& response)
{
	setBusy(false);
	m_error->setVisible(false);
	rebuild(response);
}

void AITextResultDialog::setBusy(bool busy)
{
	m_regenerateButton->setEnabled(!busy);
	m_copyButton->setEnabled(!busy);
	m_replaceButton->setEnabled(!busy);
	m_newFrameButton->setEnabled(!busy);
	m_editor->setReadOnly(busy);
	if (busy)
		m_regenerateButton->setText(tr("Working ..."));
	else
		m_regenerateButton->setText(tr("&Regenerate"));
}

void AITextResultDialog::showError(const QString& error)
{
	setBusy(false);
	// Shown in place rather than as a message box, and the previous answer is
	// left on screen: a regeneration that fails must not throw away the one
	// the user already has.
	m_error->setText(QStringLiteral("<qt><b style=\"color:#b00020;\">%1</b></qt>")
	                 .arg(error.toHtmlEscaped()));
	m_error->setVisible(true);
}

QString AITextResultDialog::currentText() const
{
	return m_editor->toPlainText().trimmed();
}

QString AITextResultDialog::chosenText() const
{
	return currentText();
}

void AITextResultDialog::copyToClipboard()
{
	QApplication::clipboard()->setText(currentText());
	m_action = CopyOnly;
	// Deliberately does not close: copying one headline and then wanting
	// another is the obvious next thing to do.
	m_copyButton->setText(tr("Copied"));
}

void AITextResultDialog::replaceInFrame()
{
	if (currentText().isEmpty())
		return;
	m_action = Replace;
	accept();
}

void AITextResultDialog::insertAsNewFrame()
{
	if (currentText().isEmpty())
		return;
	m_action = InsertNewFrame;
	accept();
}
