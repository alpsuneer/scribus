/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef INPAINTPROGRESSDIALOG_H
#define INPAINTPROGRESSDIALOG_H

#include <QDialog>
#include <QElapsedTimer>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

/*!
 \brief Progress while an object removal is being computed.

 Modeless on purpose. Inpainting a large mask can take a while and the run
 happens on a worker thread, so there is no reason to freeze the application
 around it; the user can keep reading the page while it works.

 The dialog reports rather than decides: pressing Cancel emits cancelled() and
 nothing else. Whoever started the run owns the cancel flag and the tidying up,
 because only it knows what state the document is in.
 */
class InpaintProgressDialog : public QDialog
{
	Q_OBJECT

public:
	explicit InpaintProgressDialog(QWidget* parent = nullptr);

	void languageChange();

public slots:
	//! Move the bar. Values outside 0..100 are ignored.
	void setPercent(int percent);
	//! Replace the line above the bar.
	void setMessage(const QString& message);
	/*! \brief Switch the bar to a moving barber's pole with no position.

	    For work that cannot say how far along it is. A bar sitting at zero for
	    a minute looks like something that has hung; one that is visibly moving
	    does not, and that is the whole of the difference. */
	void setIndeterminate(bool indeterminate);
	/*! \brief Show \a hint once the run has been going \a seconds.

	    So that a long wait explains itself instead of looking like a fault,
	    without putting a warning in front of someone whose run takes two
	    seconds. */
	void setHint(int afterSeconds, const QString& hint);
	//! Cancel pressed: the button goes dead and says so, but the dialog stays
	//! up until the run really stops, so it is clear something is still
	//! happening rather than looking like nothing was done.
	void markCancelling();

signals:
	void cancelled();

protected:
	//! Closing the window is the same request as pressing Cancel; there must
	//! be no way to dismiss the dialog and leave the run going unattended.
	void closeEvent(QCloseEvent* e) override;
	//! Escape reaches QDialog::reject() without going through closeEvent(),
	//! so it needs the same treatment or the job would carry on with nothing
	//! on screen to stop it.
	void reject() override;

private slots:
	void tick();
	void cancelPressed();

private:
	QLabel* m_message {nullptr};
	QProgressBar* m_bar {nullptr};
	QLabel* m_elapsed {nullptr};
	QLabel* m_hint {nullptr};
	QPushButton* m_cancelButton {nullptr};
	QTimer* m_timer {nullptr};
	QElapsedTimer m_clock;
	QString m_hintText;
	int m_hintAfterSeconds {0};
	bool m_cancelling {false};
};

#endif
