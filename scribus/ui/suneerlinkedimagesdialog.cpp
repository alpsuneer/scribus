/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/suneerlinkedimagesdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SuneerLinkedImagesDialog::SuneerLinkedImagesDialog(QWidget* parent, const QString& action, const QList<SuneerImageLinks::Entry>& entries)
	: QDialog(parent)
{
	setObjectName("SuneerLinkedImagesDialog");
	setWindowTitle(tr("Linked Images"));
	setModal(true);

	int linked = 0, missing = 0;
	for (const auto& e : entries)
	{
		if (e.status == SuneerImageLinks::Missing)
			++missing;
		else
			++linked;
	}

	auto* layout = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("%1: this document has %2 linked image(s) and %3 missing image(s).\n"
	                            "A linked image is only a path to a file; it is lost when that file is moved "
	                            "or the document is opened on another machine.")
	                         .arg(action).arg(linked).arg(missing), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto* tree = new QTreeWidget(this);
	tree->setObjectName("linkedImagesList");
	tree->setRootIsDecorated(false);
	tree->setSelectionMode(QAbstractItemView::NoSelection);
	tree->setHeaderLabels({ tr("Page"), tr("Frame"), tr("File"), tr("Status") });
	for (const auto& e : entries)
	{
		auto* row = new QTreeWidgetItem(tree);
		row->setText(0, e.page);
		row->setText(1, e.frameName);
		row->setText(2, e.path);
		row->setToolTip(2, e.path);
		row->setText(3, SuneerImageLinks::statusText(e.status));
		if (e.status == SuneerImageLinks::Missing)
			row->setForeground(3, QColor(214, 30, 30));
	}
	tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	tree->header()->setStretchLastSection(true);
	tree->setMinimumSize(620, 180);
	layout->addWidget(tree, 1);

	if (missing > 0)
	{
		auto* note = new QLabel(tr("Missing images cannot be embedded; they stay as they are."), this);
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	m_dontAsk = new QCheckBox(tr("Don't ask again for this document"), this);
	m_dontAsk->setObjectName("dontAskAgain");
	m_dontAsk->setToolTip(tr("Remembered inside this document only. Turn it back on with Extras > Warn About Linked Images in This Document."));
	layout->addWidget(m_dontAsk);

	auto* buttons = new QDialogButtonBox(this);
	QPushButton* embedBtn = buttons->addButton(tr("Embed all and continue"), QDialogButtonBox::AcceptRole);
	embedBtn->setObjectName("embedAndContinue");
	QPushButton* contBtn = buttons->addButton(tr("Continue anyway"), QDialogButtonBox::ActionRole);
	contBtn->setObjectName("continueAnyway");
	QPushButton* cancelBtn = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
	cancelBtn->setObjectName("cancel");
	embedBtn->setEnabled(linked > 0);
	if (linked > 0)
		embedBtn->setDefault(true);
	else
		contBtn->setDefault(true);
	layout->addWidget(buttons);

	connect(embedBtn, &QPushButton::clicked, this, [this]{ m_choice = EmbedAndContinue; accept(); });
	connect(contBtn, &QPushButton::clicked, this, [this]{ m_choice = ContinueAnyway; accept(); });
	connect(cancelBtn, &QPushButton::clicked, this, [this]{ m_choice = Cancel; reject(); });
}

bool SuneerLinkedImagesDialog::dontAskAgain() const
{
	return m_dontAsk && m_dontAsk->isChecked();
}
