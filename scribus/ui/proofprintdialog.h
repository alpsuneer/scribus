/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PROOFPRINTDIALOG_H
#define PROOFPRINTDIALOG_H

#include <QDialog>
#include <QSizeF>
#include <QString>

#include "scribusapi.h"

class QComboBox;
class QPushButton;
class QLabel;
class QSpinBox;

/**
 * @brief The dialog behind Proof Print (F9).
 *
 * Small on purpose: the routine gesture is F9 then Enter. Paper and Source are
 * driven by the selected queue's own capabilities rather than a fixed list, so
 * an A4-only printer offers no choice to make and a printer with one tray does
 * not show a tray control at all.
 *
 * Choices are remembered per printer - proofing to an A3 machine on A3 must not
 * change what an A4-only machine does - in a preferences context of its own, so
 * nothing here can disturb the production print settings used by Ctrl+P.
 */
class SCRIBUS_API ProofPrintDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ProofPrintDialog(QWidget* parent = nullptr);

	//! Queue to print to.
	QString printerName() const;
	//! PPD paper keyword to send as -o media=, e.g. "A4".
	QString paperName() const;
	//! Chosen paper in points, for scaling. Empty if it could not be sized.
	QSizeF paperSizePoints() const;
	//! PPD tray keyword to send as -o InputSlot=, empty when the printer has none.
	QString inputSlot() const;
	int copies() const;

	//! True when the printer reported no usable paper list and A4 was assumed.
	bool paperAssumed() const { return m_paperAssumed; }

	//! Remember the choices for next time. Called on accept.
	void saveChoices();

protected:
	//! Qt reassigns focus when a dialog is shown, so a setFocus() in the
	//! constructor is overridden: claim it here instead.
	void showEvent(QShowEvent* event) override;

private slots:
	void printerChanged();

private:
	void loadPrinters();
	void reloadCapabilities();

	QComboBox* m_printerCombo { nullptr };
	QComboBox* m_paperCombo { nullptr };
	QLabel*    m_paperFixedLabel { nullptr };
	QLabel*    m_sourceLabel { nullptr };
	QComboBox* m_sourceCombo { nullptr };
	QSpinBox*  m_copiesSpin { nullptr };
	QPushButton* m_printButton { nullptr };
	bool       m_paperAssumed { false };
	//! Explicit state rather than widget visibility: once exec() returns the
	//! dialog is hidden, so isVisible() would report false for everything and
	//! the chosen tray would be silently dropped from the job.
	bool       m_sourceOffered { false };
	bool       m_singlePaper { false };
};

#endif // PROOFPRINTDIALOG_H
