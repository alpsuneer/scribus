/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/faircodehelpviewer.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "scpaths.h"

QPointer<FaircodeHelpViewer> FaircodeHelpViewer::s_instance;

FaircodeHelpViewer::FaircodeHelpViewer(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Faircode Scribus Help"));
	setModal(false);
	// Wide enough for the tables in the rules and shading sections without
	// forcing horizontal scrolling.
	resize(720, 640);

	auto* layout = new QVBoxLayout(this);

	m_browser = new QTextBrowser(this);
	m_browser->setOpenExternalLinks(false);
	// Anchors inside the one help file are followed internally; nothing else
	// is navigable, so there is no history to manage.
	m_browser->setOpenLinks(true);
	layout->addWidget(m_browser);

	auto* buttons = new QDialogButtonBox(this);
	auto* homeButton = buttons->addButton(tr("Contents"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	connect(homeButton, &QPushButton::clicked, this, &FaircodeHelpViewer::onHomeClicked);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	layout->addWidget(buttons);
}

QString FaircodeHelpViewer::helpFilePath()
{
	return ScPaths::instance().shareDir() + "help/faircode/faircode-help.html";
}

bool FaircodeHelpViewer::loadContent()
{
	if (m_loaded)
		return true;

	const QString path = helpFilePath();
	if (!QFileInfo::exists(path))
	{
		// Say where the file was expected rather than showing an empty window:
		// on a part-installed tree that is the whole diagnosis.
		m_browser->setHtml(tr("<h3>Help file not found</h3>"
		                      "<p>Expected it at:</p><p><tt>%1</tt></p>"
		                      "<p>Reinstall Scribus, or check that the help resources "
		                      "were installed.</p>").arg(path));
		return false;
	}

	// setSource() resolves relative links (images, css) against the file's own
	// directory, which keeps the content file free of absolute paths.
	m_browser->setSource(QUrl::fromLocalFile(path));
	m_loaded = true;
	return true;
}

void FaircodeHelpViewer::goToAnchor(const QString& anchor)
{
	if (!loadContent())
		return;
	if (anchor.isEmpty())
		m_browser->scrollToAnchor(QStringLiteral("top"));
	else
		m_browser->scrollToAnchor(anchor);
}

void FaircodeHelpViewer::onHomeClicked()
{
	goToAnchor(QString());
}

void FaircodeHelpViewer::showTopic(QWidget* parent, const QString& anchor)
{
	if (!s_instance)
		s_instance = new FaircodeHelpViewer(parent);

	s_instance->show();
	s_instance->raise();
	s_instance->activateWindow();
	// After show(): scrollToAnchor() needs the viewport laid out to know where
	// the anchor is, otherwise it silently scrolls nowhere.
	s_instance->goToAnchor(anchor);
}

QToolButton* FaircodeHelpViewer::makeHelpButton(QWidget* parent, const QString& tooltip)
{
	auto* button = new QToolButton(parent);
	button->setText(QStringLiteral("?"));
	button->setToolTip(tooltip);
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::NoFocus);
	button->setCursor(Qt::PointingHandCursor);
	button->setFixedSize(20, 20);
	button->setStyleSheet(QStringLiteral(
		"QToolButton { border: none; color: #1565C0; font-weight: bold; font-size: 12px; }"
		"QToolButton:hover { color: #0D47A1; text-decoration: underline; }"));
	return button;
}
