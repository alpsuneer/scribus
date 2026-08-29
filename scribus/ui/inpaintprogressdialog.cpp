/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/inpaintprogressdialog.h"

#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

InpaintProgressDialog::InpaintProgressDialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("InpaintProgressDialog"));
	setModal(false);
	// No context-help button, and no system menu: the only two ways out are
	// Cancel and the run finishing.
	setWindowFlags((windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowContextHelpButtonHint);

	auto* layout = new QVBoxLayout(this);
	layout->setSpacing(8);

	m_message = new QLabel(this);
	layout->addWidget(m_message);

	m_bar = new QProgressBar(this);
	m_bar->setRange(0, 100);
	m_bar->setValue(0);
	m_bar->setMinimumWidth(300);
	layout->addWidget(m_bar);

	m_elapsed = new QLabel(this);
	layout->addWidget(m_elapsed);

	m_hint = new QLabel(this);
	m_hint->setWordWrap(true);
	m_hint->hide();
	layout->addWidget(m_hint);

	m_cancelButton = new QPushButton(this);
	auto* buttons = new QHBoxLayout();
	buttons->addStretch(1);
	buttons->addWidget(m_cancelButton);
	layout->addLayout(buttons);

	connect(m_cancelButton, &QPushButton::clicked, this, &InpaintProgressDialog::cancelPressed);

	m_clock.start();
	m_timer = new QTimer(this);
	m_timer->setInterval(250);
	connect(m_timer, &QTimer::timeout, this, &InpaintProgressDialog::tick);
	m_timer->start();

	languageChange();
	tick();
}

void InpaintProgressDialog::languageChange()
{
	setWindowTitle(tr("Remove Object"));
	if (m_message)
		m_message->setText(tr("Removing object..."));
	if (m_cancelButton)
		m_cancelButton->setText(m_cancelling ? tr("Cancelling...") : tr("Cancel"));
	tick();
}

void InpaintProgressDialog::setPercent(int percent)
{
	if (percent < 0 || percent > 100)
		return;
	if (m_bar)
		m_bar->setValue(percent);
}

void InpaintProgressDialog::tick()
{
	if (!m_elapsed)
		return;
	const double seconds = double(m_clock.elapsed()) / 1000.0;
	m_elapsed->setText(tr("Elapsed: %1 s").arg(seconds, 0, 'f', 1));

	if (m_hint && !m_hintText.isEmpty() && !m_hint->isVisible() &&
	    seconds >= double(m_hintAfterSeconds))
	{
		m_hint->setText(QStringLiteral("<qt><i>%1</i></qt>").arg(m_hintText.toHtmlEscaped()));
		m_hint->show();
		adjustSize();
	}
}

void InpaintProgressDialog::setMessage(const QString& message)
{
	if (m_message)
		m_message->setText(message);
}

void InpaintProgressDialog::setIndeterminate(bool indeterminate)
{
	if (!m_bar)
		return;
	if (indeterminate)
		m_bar->setRange(0, 0);
	else
		m_bar->setRange(0, 100);
}

void InpaintProgressDialog::setHint(int afterSeconds, const QString& hint)
{
	m_hintAfterSeconds = afterSeconds;
	m_hintText = hint;
}

void InpaintProgressDialog::cancelPressed()
{
	if (m_cancelling)
		return;
	markCancelling();
	emit cancelled();
}

void InpaintProgressDialog::markCancelling()
{
	m_cancelling = true;
	if (m_cancelButton)
	{
		m_cancelButton->setEnabled(false);
		m_cancelButton->setText(tr("Cancelling..."));
	}
	if (m_message)
		m_message->setText(tr("Stopping..."));
}

void InpaintProgressDialog::reject()
{
	cancelPressed();
	// Deliberately not calling QDialog::reject(): the dialog stays up until
	// the run it is reporting on has actually stopped.
}

void InpaintProgressDialog::closeEvent(QCloseEvent* e)
{
	if (!m_cancelling)
	{
		// Treat the window close as Cancel and keep the dialog up until the
		// worker actually stops, rather than orphaning a running job.
		cancelPressed();
		e->ignore();
		return;
	}
	e->ignore();
}
