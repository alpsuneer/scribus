/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/proofprintdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>

#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "util_printer.h"

namespace
{
	//! Preferences of their own: proof choices must never write into the
	//! production print settings.
	const char* PROOF_PREFS_CONTEXT = "proof_print";

	PrefsContext* proofPrefs()
	{
		return PrefsManager::instance().prefsFile->getContext(PROOF_PREFS_CONTEXT);
	}

	//! Per-printer keys, so an A3 machine remembering A3 cannot change what an
	//! A4-only machine does.
	QString paperKey(const QString& printer)  { return QStringLiteral("Paper_") + printer; }
	QString sourceKey(const QString& printer) { return QStringLiteral("Source_") + printer; }
}

ProofPrintDialog::ProofPrintDialog(QWidget* parent) : QDialog(parent)
{
	setWindowTitle(tr("Proof Print"));
	setModal(true);

	auto* form = new QFormLayout();
	m_printerCombo = new QComboBox(this);
	form->addRow(tr("&Printer:"), m_printerCombo);

	m_paperCombo = new QComboBox(this);
	m_paperFixedLabel = new QLabel(this);
	auto* paperRow = new QVBoxLayout();
	paperRow->setContentsMargins(0, 0, 0, 0);
	paperRow->addWidget(m_paperCombo);
	paperRow->addWidget(m_paperFixedLabel);
	// Build the label by hand: the addRow(QString, QLayout*) overload cannot set
	// a buddy, so Qt has nothing to attach the accelerator to and the label
	// renders the literal "P&aper:" while Alt+A does nothing.
	auto* paperLabel = new QLabel(tr("P&aper:"), this);
	paperLabel->setBuddy(m_paperCombo);
	form->addRow(paperLabel, paperRow);

	m_sourceCombo = new QComboBox(this);
	m_sourceLabel = new QLabel(tr("&Source:"), this);
	m_sourceLabel->setBuddy(m_sourceCombo);
	form->addRow(m_sourceLabel, m_sourceCombo);

	m_copiesSpin = new QSpinBox(this);
	m_copiesSpin->setRange(1, 999);
	m_copiesSpin->setValue(1);
	form->addRow(tr("&Copies:"), m_copiesSpin);

	// Use the standard Ok slot relabelled, rather than a hand-added AcceptRole
	// button: QDialog recomputes the default button when it is shown, and only
	// the standard one reliably keeps default status, which is what makes Enter
	// print. (Verified: with an added button, Escape cancelled but Enter did
	// nothing.)
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	QPushButton* printButton = buttons->button(QDialogButtonBox::Ok);
	m_printButton = printButton;
	printButton->setText(tr("&Print"));
	printButton->setDefault(true);
	printButton->setAutoDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);

	// "Enter prints" has to hold wherever the focus happens to be: a combo or
	// the spin box otherwise swallows Return before the default button sees it.
	for (int key : { Qt::Key_Return, Qt::Key_Enter })
	{
		auto* shortcut = new QShortcut(QKeySequence(key), this);
		connect(shortcut, &QShortcut::activated, this, &QDialog::accept);
	}

	loadPrinters();
	reloadCapabilities();
	connect(m_printerCombo, &QComboBox::currentTextChanged, this, &ProofPrintDialog::printerChanged);

}

void ProofPrintDialog::loadPrinters()
{
	const QStringList printers = PrinterUtil::getPrinterNames();
	m_printerCombo->addItems(printers);

	QString last = proofPrefs()->get("LastPrinter", QString());
	if (last.isEmpty() || !printers.contains(last))
		last = PrinterUtil::getDefaultPrinterName();
	int idx = m_printerCombo->findText(last);
	if (idx >= 0)
		m_printerCombo->setCurrentIndex(idx);
}

void ProofPrintDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// Routine use is F9 then Enter, so Print takes the focus. Left to Qt the
	// Printer combo gets it and Return opens that dropdown instead of printing.
	if (m_printButton)
		m_printButton->setFocus();
}

void ProofPrintDialog::printerChanged()
{
	reloadCapabilities();
}

void ProofPrintDialog::reloadCapabilities()
{
	const QString printer = m_printerCombo->currentText();

	// --- Paper -----------------------------------------------------------
	QStringList papers;
	QString defaultPaper;
	m_paperAssumed = false;
	PrinterUtil::getSupportedPaperSizes(printer, papers, defaultPaper);

	// A printer can advertise thirty sizes; a proof only ever wants the two
	// broadsheet-sensible ones plus whatever the queue itself is set to.
	QStringList offered;
	for (const QString& candidate : { QStringLiteral("A4"), QStringLiteral("A3") })
	{
		if (papers.contains(candidate))
			offered.append(candidate);
	}
	if (!defaultPaper.isEmpty() && !offered.contains(defaultPaper))
		offered.prepend(defaultPaper);
	if (offered.isEmpty())
	{
		// Nothing usable reported: say so rather than imply a detected size.
		offered.append(QStringLiteral("A4"));
		defaultPaper = QStringLiteral("A4");
		m_paperAssumed = true;
	}

	m_paperCombo->blockSignals(true);
	m_paperCombo->clear();
	m_paperCombo->addItems(offered);
	QString rememberedPaper = proofPrefs()->get(paperKey(printer), QString());
	QString wantPaper = offered.contains(rememberedPaper) ? rememberedPaper : defaultPaper;
	int paperIdx = m_paperCombo->findText(wantPaper);
	m_paperCombo->setCurrentIndex(paperIdx >= 0 ? paperIdx : 0);
	m_paperCombo->blockSignals(false);

	// One size means there is no choice to make: show it, do not offer it.
	m_singlePaper = (offered.count() <= 1);
	const bool singlePaper = m_singlePaper;
	m_paperCombo->setVisible(!singlePaper);
	m_paperFixedLabel->setVisible(singlePaper);
	if (singlePaper)
	{
		m_paperFixedLabel->setText(m_paperAssumed
			? tr("%1 (paper unknown - assumed)").arg(offered.first())
			: offered.first());
	}

	// --- Source ----------------------------------------------------------
	QStringList traySlots;
	QString defaultSlot;
	PrinterUtil::getInputSlots(printer, traySlots, defaultSlot);

	m_sourceCombo->blockSignals(true);
	m_sourceCombo->clear();
	m_sourceCombo->addItems(traySlots);
	QString rememberedSlot = proofPrefs()->get(sourceKey(printer), QString());
	QString wantSlot = traySlots.contains(rememberedSlot) ? rememberedSlot : defaultSlot;
	int slotIdx = m_sourceCombo->findText(wantSlot);
	m_sourceCombo->setCurrentIndex(slotIdx >= 0 ? slotIdx : 0);
	m_sourceCombo->blockSignals(false);

	// A single-source printer gets no tray control at all.
	m_sourceOffered = (traySlots.count() > 1);
	const bool showSource = m_sourceOffered;
	m_sourceCombo->setVisible(showSource);
	m_sourceLabel->setVisible(showSource);

	adjustSize();
}

QString ProofPrintDialog::printerName() const
{
	return m_printerCombo->currentText();
}

QString ProofPrintDialog::paperName() const
{
	if (m_paperCombo->count() > 0)
		return m_paperCombo->currentText();
	return QStringLiteral("A4");
}

QSizeF ProofPrintDialog::paperSizePoints() const
{
	QSizeF size = PrinterUtil::paperSizePoints(paperName());
	if (!size.isEmpty())
		return size;
	// Unrecognised keyword: ask the queue what its default measures, and only
	// then give up to the caller's fallback.
	QString media;
	QSizeF detected;
	if (PrinterUtil::getDefaultPaperSize(printerName(), media, &detected) && !detected.isEmpty())
		return detected;
	return QSizeF();
}

QString ProofPrintDialog::inputSlot() const
{
	if (!m_sourceOffered)
		return QString();
	return m_sourceCombo->currentText();
}

int ProofPrintDialog::copies() const
{
	return m_copiesSpin->value();
}

void ProofPrintDialog::saveChoices()
{
	const QString printer = printerName();
	if (printer.isEmpty())
		return;
	PrefsContext* prefs = proofPrefs();
	prefs->set("LastPrinter", printer);
	prefs->set(paperKey(printer), paperName());
	const QString slot = inputSlot();
	if (!slot.isEmpty())
		prefs->set(sourceKey(printer), slot);
}
