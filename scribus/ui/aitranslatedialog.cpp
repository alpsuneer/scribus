/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/aitranslatedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

AITranslateDialog::AITranslateDialog(QWidget* parent, const QString& initialTarget)
	: QDialog(parent)
{
	setWindowTitle(tr("Translate Text"));
	setModal(true);

	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout;

	m_target = new QComboBox(this);
	m_target->addItems(languages());
	m_target->setEditable(true);
	// Editable because the list cannot cover every language a wire story might
	// arrive in, and a model will translate into one this dropdown never heard
	// of perfectly well.
	const int index = m_target->findText(initialTarget);
	m_target->setCurrentIndex(index >= 0 ? index : 0);

	form->addRow(tr("Translate &into:"), m_target);
	layout->addLayout(form);

	auto* note = new QLabel(tr("The source language is detected from the text."), this);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

QStringList AITranslateDialog::languages()
{
	// Ordered for this newsroom, not alphabetically: the first two are nearly
	// every job, then the neighbours, then what turns up on the wire.
	return QStringList {
		QStringLiteral("Malayalam"),
		QStringLiteral("English"),
		QStringLiteral("Hindi"),
		QStringLiteral("Tamil"),
		QStringLiteral("Kannada"),
		QStringLiteral("Telugu"),
		QStringLiteral("Arabic"),
		QStringLiteral("French"),
		QStringLiteral("German"),
		QStringLiteral("Spanish"),
		QStringLiteral("Chinese (Simplified)")
	};
}

QString AITranslateDialog::targetLanguage() const
{
	return m_target->currentText().trimmed();
}
