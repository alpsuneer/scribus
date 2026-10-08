#include "suneer_news_panel.h"

#include "commonstrings.h"
#include "hyphenator.h"
#include "iconmanager.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scupdatekeystore.h"
#include "selection.h"
#include "scribusview.h"
#include "pageitem_group.h"
#include "pagestructs.h"
#include "fpointarray.h"
#include "suneerimagelinks.h"
#include "ui/faircodehelpviewer.h"
#include "undomanager.h"
#include "undotransaction.h"
#include "undostate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QImageReader>
#include <QTextBoundaryFinder>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QBrush>
#include <QApplication>
#include <QNetworkCookie>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace {

const double  kMmToPt        = 2.8346;
const QString kSecretService = QStringLiteral("org.scribus.news");
const QString kKeyAccess     = QStringLiteral("accessToken");
const QString kKeyRefresh    = QStringLiteral("refreshToken");
const QString kKeyDevice     = QStringLiteral("deviceId");
const QString kKeyUserName   = QStringLiteral("userName");
// Short names seen on the server on 2026-10-07; used only when the public
// edition list cannot be loaded.
const QString kDefaultShortNames = QStringLiteral(
	"TVM,KOL,ALP,KTM,CHN,TCR,PKD,MLP,CLT,KNR,GULF,FTR,WKD,KLV,CNTRL,PTA,WYD,IDK,DELHI,KSD");

// An empty value (left over from an old conf) counts as "use the default".
QString settingsString(const QString& key, const QString& def)
{
	QSettings s("Faircode", "ScribusNews");
	const QString v = s.value(key, def).toString().trimmed();
	return v.isEmpty() ? def : v;
}

} // namespace

// ============================================================================
// Construction / UI
// ============================================================================

SuneerNewsPanel::SuneerNewsPanel(ScribusMainWindow* parent)
	: QDockWidget("News Browser", parent)
	, UndoObject(QStringLiteral("News Browser"))
	, m_mw(parent)
{
	m_nam = new QNetworkAccessManager(this);
	loadConfig();

	m_stack = new QStackedWidget(this);

	// Page 0: locked. Nothing but a message and the Login button.
	QWidget* lockW = new QWidget(m_stack);
	QVBoxLayout* lockVl = new QVBoxLayout(lockW);
	lockVl->setContentsMargins(8, 16, 8, 8);
	QLabel* lockTitle = new QLabel("<b>Faircode News Browser</b>", lockW);
	lockTitle->setStyleSheet("color:#1565C0;");
	m_lockLabel = new QLabel(tr("Log in to the workflow server to browse news."), lockW);
	m_lockLabel->setWordWrap(true);
	m_lockLabel->setStyleSheet("color:#666;");
	m_loginBtn = new QPushButton(tr("Login"), lockW);
	m_loginBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:6px;");
	connect(m_loginBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onLogin);
	lockVl->addWidget(lockTitle);
	lockVl->addWidget(m_lockLabel);
	lockVl->addWidget(m_loginBtn);
	lockVl->addStretch();
	m_stack->addWidget(lockW);

	// Page 1: the panel.
	QWidget* w = new QWidget(m_stack);
	QVBoxLayout* vl = new QVBoxLayout(w);
	vl->setSpacing(4);
	vl->setContentsMargins(4, 4, 4, 4);

	// Top bar: title, help, refresh, settings
	QHBoxLayout* topBar = new QHBoxLayout();
	IconManager& im = IconManager::instance();
	m_settingsBtn = new QPushButton(im.loadIcon("document-properties"), QString(), w);
	m_settingsBtn->setFixedWidth(30);
	m_settingsBtn->setToolTip(tr("Settings (server, layout, styles)"));
	connect(m_settingsBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onSettings);
	m_refreshBtn = new QPushButton(im.loadIcon("reload"), QString(), w);
	m_refreshBtn->setFixedWidth(30);
	m_refreshBtn->setToolTip(tr("Reload editions and pages"));
	connect(m_refreshBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onRefresh);
	QToolButton* helpBtn = FaircodeHelpViewer::makeHelpButton(w, tr("How to use the News Browser"));
	connect(helpBtn, &QToolButton::clicked, w, [w]() {
		FaircodeHelpViewer::showTopic(w, QStringLiteral("news-browser"));
	});
	QLabel* title = new QLabel("<b>Faircode News Browser</b>", w);
	title->setStyleSheet("color:#1565C0;");
	topBar->addWidget(title);
	topBar->addWidget(helpBtn);
	topBar->addStretch();
	topBar->addWidget(m_refreshBtn);
	topBar->addWidget(m_settingsBtn);
	vl->addLayout(topBar);

	// User row
	QHBoxLayout* userRow = new QHBoxLayout();
	m_userLabel = new QLabel("", w);
	m_userLabel->setStyleSheet("color:#2E7D32; font-size:11px;");
	m_userLabel->setWordWrap(true);
	m_logoutBtn = new QPushButton(tr("Logout"), w);
	connect(m_logoutBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onLogout);
	userRow->addWidget(m_userLabel, 1);
	userRow->addWidget(m_logoutBtn);
	vl->addLayout(userRow);

	// Edition
	vl->addWidget(new QLabel(tr("Edition:"), w));
	m_editionCombo = new QComboBox(w);
	connect(m_editionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &SuneerNewsPanel::onEditionChanged);
	vl->addWidget(m_editionCombo);

	// Page
	vl->addWidget(new QLabel(tr("Page:"), w));
	m_pageCombo = new QComboBox(w);
	vl->addWidget(m_pageCombo);

	// Date
	vl->addWidget(new QLabel(tr("Date:"), w));
	m_dateEdit = new QDateEdit(QDate::currentDate().addDays(1), w);
	m_dateEdit->setDisplayFormat("yyyy-MM-dd");
	m_dateEdit->setCalendarPopup(true);
	vl->addWidget(m_dateEdit);

	// Title style (the server no longer sends one)
	QHBoxLayout* styleRow = new QHBoxLayout();
	styleRow->addWidget(new QLabel(tr("Title style:"), w));
	m_titleStyleCmb = new QComboBox(w);
	m_titleStyleCmb->setEditable(true);
	m_titleStyleCmb->setToolTip(tr("Paragraph style for the headline of placed stories"));
	styleRow->addWidget(m_titleStyleCmb, 1);
	vl->addLayout(styleRow);
	fillTitleStyleCombo();

	// Photo position for the next placement (remembered); default from Settings.
	QHBoxLayout* photoRow = new QHBoxLayout();
	photoRow->addWidget(new QLabel(tr("Photo:"), w));
	m_photoPosCmb = new QComboBox(w);
	for (const QString& key : photoPositionKeys())
		m_photoPosCmb->addItem(photoPositionLabel(key), key);
	{
		const QString def = settingsString("layout/imagePosition", "right");
		const QString last = settingsString("layout/imagePositionPanel", def);
		int pi = m_photoPosCmb->findData(last);
		m_photoPosCmb->setCurrentIndex(pi >= 0 ? pi : 0);
	}
	connect(m_photoPosCmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		QSettings s("Faircode", "ScribusNews");
		s.setValue("layout/imagePositionPanel", m_photoPosCmb->currentData().toString());
	});
	photoRow->addWidget(m_photoPosCmb, 1);
	vl->addLayout(photoRow);

	// Fetch
	m_fetchBtn = new QPushButton(im.loadIcon("edit-find-replace"), tr("Fetch News"), w);
	m_fetchBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:4px;");
	connect(m_fetchBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onFetchNews);
	vl->addWidget(m_fetchBtn);

	// Status
	m_statusLabel = new QLabel("", w);
	m_statusLabel->setStyleSheet("color:#666; font-size:11px;");
	m_statusLabel->setWordWrap(true);
	vl->addWidget(m_statusLabel);

	// Select all
	m_newsList = new QListWidget();
	QHBoxLayout* selRow = new QHBoxLayout();
	m_selectAllChk = new QCheckBox(tr("Select All"), w);
	m_selectAllChk->setStyleSheet("font-size:11px; padding:2px;");
	connect(m_selectAllChk, &QCheckBox::toggled, this, &SuneerNewsPanel::onSelectAll);
	selRow->addWidget(m_selectAllChk, 1);
	m_selectUsedChk = new QCheckBox(tr("Select all used"), w);
	m_selectUsedChk->setStyleSheet("font-size:11px; padding:2px;");
	m_selectUsedChk->setToolTip(tr("Tick every used (grey) story - for Mark unused, never for placing"));
	connect(m_selectUsedChk, &QCheckBox::toggled, this, [this](bool on) {
		for (int i = 0; i < m_newsList->count(); i++)
			if (isPlacedRow(m_newsList->item(i)))
				m_newsList->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	});
	selRow->addWidget(m_selectUsedChk, 1);
	m_checkBtn = new QPushButton(im.loadIcon("reload"), tr("Check"), w);
	m_checkBtn->setToolTip(tr("Re-read the placed status of this page from the server (and retry unsent marks)"));
	connect(m_checkBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onCheck);
	selRow->addWidget(m_checkBtn);
	vl->addLayout(selRow);
	// Right-click on a row: Release
	m_newsList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_newsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) {
		QListWidgetItem* row = m_newsList->itemAt(p);
		if (!row)
			return;
		const int idx = row->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_newsData.size())
			return;
		const QJsonObject news = m_newsData[idx].toObject();
		const QString st = newsDtpStatus(news);
		QMenu menu(this);
		QAction* rel = st != "UNUSED" ? menu.addAction(tr("Mark as unused")) : nullptr;
		QAction* bal = st != "BALANCED" ? menu.addAction(tr("Mark as balance (keep for later)")) : nullptr;
		if (menu.isEmpty())
			return;
		QAction* chosen = menu.exec(m_newsList->viewport()->mapToGlobal(p));
		if (chosen && chosen == rel)
			markStories(QList<QJsonObject>() << news, unusedStatus());
		else if (chosen && chosen == bal)
		{
			m_markingBalance = true;
			markStories(QList<QJsonObject>() << news, "BALANCED");
			m_markingBalance = false;
		}
	});

	QSplitter* splitter = new QSplitter(Qt::Vertical, w);
	m_newsList->setWordWrap(false);
	m_newsList->setSpacing(2);
	m_newsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_newsList->setStyleSheet(
		"QListWidget { font-size:14px; }"
		"QListWidget::item { padding:6px; border-bottom:1px solid #ddd; }"
		"QListWidget::item:selected { background:#E3F2FD; color:#000; }"
		"QListWidget::item:hover { background:#F5F5F5; }"
		"QToolTip { font-size:14px; padding:6px; }");
	m_newsList->setIconSize(QSize(24, 24));
	m_newsList->setTextElideMode(Qt::ElideNone);   // we elide ourselves, by grapheme
	m_newsList->viewport()->installEventFilter(this);
	connect(m_newsList, &QListWidget::itemClicked, this, &SuneerNewsPanel::onNewsItemClicked);
	connect(m_newsList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { updateTickButtons(); });
	connect(m_newsList, &QListWidget::itemDoubleClicked, this, &SuneerNewsPanel::onPlaceNews);
	splitter->addWidget(m_newsList);
	vl->addWidget(splitter, 1);

	QHBoxLayout* btnRow = new QHBoxLayout();
	m_placeBtn = new QPushButton(im.loadIcon("tool-insert-text-frame"), tr("Place Selected"), w);
	m_placeBtn->setStyleSheet("background:#2E7D32; color:white; font-weight:bold; padding:4px;");
	m_placeBtn->setEnabled(false);
	connect(m_placeBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onPlaceSelected);
	btnRow->addWidget(m_placeBtn, 1);
	// Second row under Place: the status actions for the TICKED rows.
	QHBoxLayout* markRow = new QHBoxLayout();
	m_releaseBtn = new QPushButton(tr("Mark unused"), w);
	m_releaseBtn->setToolTip(tr("Mark every TICKED used (grey) story as unused on the server (available to everyone); optionally remove them from this page"));
	m_releaseBtn->setStyleSheet("padding:4px;");
	m_releaseBtn->setEnabled(false);
	connect(m_releaseBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onMarkUnusedTicked);
	markRow->addWidget(m_releaseBtn, 1);
	m_balanceBtn = new QPushButton(tr("Mark balance"), w);
	m_balanceBtn->setToolTip(tr("Mark every TICKED free or used story as BALANCE on the server (kept for a later day, listed under Balanced News); optionally remove them from this page"));
	m_balanceBtn->setStyleSheet("padding:4px;");
	m_balanceBtn->setEnabled(false);
	connect(m_balanceBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onMarkBalanceTicked);
	markRow->addWidget(m_balanceBtn, 1);
	vl->addLayout(btnRow);
	vl->addLayout(markRow);

	m_stack->addWidget(w);
	m_stack->setCurrentIndex(0);
	setWidget(m_stack);
	setMinimumWidth(240);

	// The keychain is touched after main-window construction so a keyring
	// prompt never lands in the middle of start-up.
	QTimer::singleShot(800, this, &SuneerNewsPanel::startupSession);

}

void SuneerNewsPanel::setDocument(ScribusDoc* doc)
{
	m_doc = doc;
	fillTitleStyleCombo();
}

void SuneerNewsPanel::fillTitleStyleCombo()
{
	if (!m_titleStyleCmb)
		return;
	const QString current = m_titleStyleCmb->currentText().isEmpty()
		? settingsString("style/titleStyle", "44 M")
		: m_titleStyleCmb->currentText();
	m_titleStyleCmb->blockSignals(true);
	m_titleStyleCmb->clear();
	QStringList names;
	if (m_doc)
	{
		// Headline styles in this house are named "<size> M" (44 M, 52 M ...).
		const StyleSet<ParagraphStyle>& styles = m_doc->paragraphStyles();
		static const QRegularExpression headRe(QStringLiteral("^\\d+\\s*M$"));
		for (int i = 0; i < styles.count(); ++i)
			if (headRe.match(styles[i].name()).hasMatch())
				names << styles[i].name();
	}
	if (names.isEmpty())
		names << "36 M" << "44 M" << "48 M" << "52 M" << "60 M";
	names.sort();
	m_titleStyleCmb->addItems(names);
	if (!current.isEmpty())
	{
		int idx = m_titleStyleCmb->findText(current);
		if (idx >= 0)
			m_titleStyleCmb->setCurrentIndex(idx);
		else
			m_titleStyleCmb->setEditText(current);
	}
	m_titleStyleCmb->blockSignals(false);
}

// ============================================================================
// Configuration
// ============================================================================

QString SuneerNewsPanel::defaultFilesBaseFor(const QString& apiHost)
{
	QUrl u(apiHost);
	if (!u.isValid() || u.host().isEmpty())
		return QStringLiteral("http://localhost/files");
	// nginx serves /files on the plain web port of the same machine
	return QStringLiteral("%1://%2/files").arg(u.scheme().isEmpty() ? "http" : u.scheme(), u.host());
}

void SuneerNewsPanel::loadConfig()
{
	QSettings s("Faircode", "ScribusNews");
	m_apiHost = s.value("apiHost").toString().trimmed();
	if (m_apiHost.isEmpty())
	{
		// Migrate the old key: "http://HOST:3011/api/v1/external" -> "http://HOST:3011"
		QString old = s.value("apiUrl").toString().trimmed();
		QUrl u(old);
		if (u.isValid() && !u.host().isEmpty())
		{
			m_apiHost = QStringLiteral("%1://%2").arg(u.scheme(), u.host());
			if (u.port() > 0)
				m_apiHost += QStringLiteral(":%1").arg(u.port());
		}
		if (m_apiHost.isEmpty() || m_apiHost.contains("localhost"))
			m_apiHost = QStringLiteral("http://10.199.100.133:3011");
		s.setValue("apiHost", m_apiHost);
		s.remove("apiUrl");
		s.remove("imageBase");   // was always the dead localhost:9000
	}
	while (m_apiHost.endsWith('/'))
		m_apiHost.chop(1);
	m_filesBase = s.value("filesBase").toString().trimmed();
	if (m_filesBase.isEmpty())
	{
		m_filesBase = defaultFilesBaseFor(m_apiHost);
		s.setValue("filesBase", m_filesBase);
	}
	while (m_filesBase.endsWith('/'))
		m_filesBase.chop(1);
	m_lastEdition = s.value("defaultEdition").toString();
	m_lastPage    = s.value("lastPage").toString();
	m_editionShortNames = s.value("editionShortNames", kDefaultShortNames).toString();
}

void SuneerNewsPanel::saveConfig()
{
	QSettings s("Faircode", "ScribusNews");
	s.setValue("apiHost", m_apiHost);
	s.setValue("filesBase", m_filesBase);
	s.setValue("defaultEdition", m_lastEdition);
	s.setValue("lastPage", m_lastPage);
	s.setValue("editionShortNames", m_editionShortNames);
}

QString SuneerNewsPanel::apiUrl(const QString& path) const
{
	return m_apiHost + QStringLiteral("/api/v1") + path;
}

// ============================================================================
// Requests
// ============================================================================

// The one place that builds API requests. When the server starts to require
// a login, the Authorization header goes here and nowhere else.
QNetworkRequest SuneerNewsPanel::apiRequest(const QUrl& url, bool bearer) const
{
	QNetworkRequest req(url);
	req.setRawHeader("Accept", "application/json");
	req.setRawHeader("User-Agent", "Scribus-NewsBrowser/1.0");
	req.setTransferTimeout(15000);
	if (bearer && !m_accessToken.isEmpty())
		req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
	return req;
}

int SuneerNewsPanel::httpStatus(QNetworkReply* reply)
{
	return reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
}

QString SuneerNewsPanel::serverMessage(const QByteArray& body)
{
	const QJsonObject o = QJsonDocument::fromJson(body).object();
	QString m = o["error"].toObject()["message"].toString();
	if (m.isEmpty())
		m = o["message"].toString();
	return m;
}

QString SuneerNewsPanel::describeError(QNetworkReply* reply) const
{
	if (!reply)
		return tr("No reply");
	const int status = httpStatus(reply);
	const QString host = QUrl(m_apiHost).host();
	switch (reply->error())
	{
		case QNetworkReply::NoError:
			return QString();
		case QNetworkReply::ConnectionRefusedError:
		case QNetworkReply::HostNotFoundError:
		case QNetworkReply::TimeoutError:
		case QNetworkReply::RemoteHostClosedError:
		case QNetworkReply::NetworkSessionFailedError:
		case QNetworkReply::UnknownNetworkError:
			return tr("Server not reachable (%1): %2").arg(host, reply->errorString());
		case QNetworkReply::OperationCanceledError:
			return tr("Request timed out (%1)").arg(host);
		default:
			break;
	}
	const QString msg = serverMessage(reply->peek(reply->bytesAvailable()));
	const QString path = reply->url().path();
	if (status == 404)
		return tr("HTTP 404 Route not found: %1").arg(path);
	if (status == 401 || status == 403)
		return tr("HTTP %1 Login required (%2)").arg(status).arg(msg.isEmpty() ? path : msg);
	if (status > 0)
		return tr("HTTP %1: %2").arg(status).arg(msg.isEmpty() ? reply->errorString() : msg);
	return reply->errorString();
}

// Blocking GET through the one request builder. The panel's flow is
// synchronous (placement needs the data), so a local event loop waits; the
// transfer timeout on the request really aborts the reply. The reply is
// deleted after \a done returns.
void SuneerNewsPanel::apiGet(const QUrl& url, std::function<void(QNetworkReply*)> done, bool retried)
{
	ensureSecretsLoaded();
	QNetworkReply* reply = m_nam->get(apiRequest(url, true));
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	reply->deleteLater();

	const int status = httpStatus(reply);
	if ((status != 401 && status != 403) || retried)
	{
		done(reply);
		return;
	}
	// Expired or invalid token: refresh once, then repeat the request once.
	bool ok = false;
	if (!m_refreshToken.isEmpty())
		ok = refreshAccessToken();
	if (!ok)
	{
		clearSession();
		setLocked(true);
		m_lockLabel->setText(tr("Session expired - please log in again."));
		if (!m_quietAuth)
			ok = loginDialog();
	}
	if (!ok)
	{
		done(reply);
		return;
	}
	apiGet(url, done, true);
}

// PATCH with Bearer; same expired-token handling as apiGet. Returns true
// when the server answered 2xx.
bool SuneerNewsPanel::apiPatch(const QUrl& url, const QJsonObject& body, std::function<void(QNetworkReply*)> done, bool retried)
{
	ensureSecretsLoaded();
	QNetworkRequest req = apiRequest(url, true);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	QNetworkReply* reply = m_nam->sendCustomRequest(req, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	reply->deleteLater();
	const int status = httpStatus(reply);
	if ((status == 401 || status == 403) && !retried)
	{
		bool ok = !m_refreshToken.isEmpty() && refreshAccessToken();
		if (!ok)
		{
			clearSession();
			setLocked(true);
			m_lockLabel->setText(tr("Session expired - please log in again."));
			if (!m_quietAuth)
				ok = loginDialog();
			if (ok)
				setLocked(false);
		}
		if (ok)
			return apiPatch(url, body, done, true);
	}
	done(reply);
	return reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
}

// PATCH /news/dtp-status {newsIds, dtpStatus, pageId}. No ids or tokens are logged.
bool SuneerNewsPanel::markOnServer(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error)
{
	if (newsIds.isEmpty() || pageId.isEmpty())
	{
		if (error) *error = tr("no story id / page id");
		return false;
	}
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
	{
		if (error) *error = tr("fixture mode, no server");
		return false;
	}
	QJsonObject body;
	body["newsIds"]   = QJsonArray::fromStringList(newsIds);
	body["dtpStatus"] = dtpStatus;
	body["pageId"]    = pageId;
	QString err;
	const bool ok = apiPatch(QUrl(apiUrl("/news/dtp-status")), body, [&](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError || httpStatus(reply) < 200 || httpStatus(reply) >= 300)
			err = describeError(reply);
	});
	qDebug() << "news dtp-status" << dtpStatus << newsIds.size() << "story(ies)" << (ok ? "ok" : "failed");
	if (error) *error = err;
	return ok;
}

// After a placement: mark USED on the server; keep the ids for a retry on
// Check when that fails. The local reply copy is updated so the marks show
// at once.
void SuneerNewsPanel::markPlaced(const QList<QJsonObject>& placedStories)
{
	QStringList ids;
	for (const QJsonObject& n : placedStories)
		if (!newsServerId(n).isEmpty())
			ids << newsServerId(n);
	if (ids.isEmpty())
		return;
	QString err;
	const bool ok = markOnServer(ids, m_fetchPageId, "USED", &err);
	if (ok)
	{
		for (const QString& id : ids)
			m_pendingMarks.remove(id);
		for (int i = 0; i < m_newsData.size(); ++i)
		{
			QJsonObject n = m_newsData[i].toObject();
			if (!ids.contains(newsServerId(n)))
				continue;
			QJsonObject pg = n["page"].toObject();
			pg["dtpStatus"] = "USED";
			n["page"] = pg;
			m_newsData[i] = n;
		}
		m_statusLabel->setText(tr("Placed %1 and marked USED on the server").arg(ids.size())
			+ (m_placeUsedSkipped > 0 ? tr(" - %n used story(ies) skipped", "", m_placeUsedSkipped) : QString()));
		m_placeUsedSkipped = 0;
	}
	else
	{
		for (const QString& id : ids)
			m_pendingMarks.insert(id);
		m_statusLabel->setText(tr("Warning: placed, but NOT marked on server (%1). Press Check to retry.").arg(err)
			+ (m_placeUsedSkipped > 0 ? tr(" - %n used story(ies) skipped", "", m_placeUsedSkipped) : QString()));
		m_placeUsedSkipped = 0;
	}
	refreshPlacedMarks();
}

// Update one story's status in the local reply copy and its row only.
void SuneerNewsPanel::setRowStatus(const QString& serverId, const QString& dtpStatus)
{
	for (int i = 0; i < m_newsData.size(); ++i)
	{
		QJsonObject n = m_newsData[i].toObject();
		if (newsServerId(n) != serverId)
			continue;
		QJsonObject pg = n["page"].toObject();
		pg["dtpStatus"] = dtpStatus;
		n["page"] = pg;
		QJsonArray pages = n["newsPages"].toArray();
		for (int k = 0; k < pages.size(); ++k)
		{
			QJsonObject p = pages[k].toObject();
			p["dtpStatus"] = dtpStatus;
			pages[k] = p;
		}
		if (!pages.isEmpty())
			n["newsPages"] = pages;
		m_newsData[i] = n;
	}
	refreshPlacedMarks();
}

// Check: retry pending marks, then re-read the page's status from the server.
void SuneerNewsPanel::onCheck()
{
	if (!m_pendingMarks.isEmpty() && !m_fetchPageId.isEmpty())
	{
		QString err;
		QStringList ids = m_pendingMarks.values();
		if (markOnServer(ids, m_fetchPageId, "USED", &err))
			m_pendingMarks.clear();
		else
			m_statusLabel->setText(tr("Warning: still not marked on server (%1)").arg(err));
	}
	if (!m_fetchPageId.isEmpty())
		onFetchNews();
}

// "Mark as unused" from the list: the selected placed rows -> BALANCED on
// the server. When the story is still in this document, offer to remove it
// from the page as well (through removeStory, one undo step).
void SuneerNewsPanel::onReleaseRows()
{
	QList<QJsonObject> rows;
	for (QListWidgetItem* it : m_newsList->selectedItems())
	{
		int idx = it->data(Qt::UserRole).toInt();
		if (idx >= 0 && idx < m_newsData.size() && newsIsUsed(m_newsData[idx].toObject()))
			rows << m_newsData[idx].toObject();
	}
	if (rows.isEmpty())
	{
		m_statusLabel->setText(tr("Select a used (grey) story first"));
		return;
	}
	for (const QJsonObject& n : rows)
	{
		const QString sid = newsServerId(n);
		const QString title = collapseSpaces(n["title"].toString()).left(40);
		PageItem* inDoc = findStoryItemInDoc(sid);
		QMessageBox box(this);
		box.setIcon(QMessageBox::Question);
		box.setWindowTitle(tr("Mark as unused"));
		box.setText(inDoc ? tr("'%1' is still in THIS document.\nMark it unused on the server?").arg(title)
		                  : tr("Mark '%1' as unused on the server (available to everyone)?").arg(title));
		QPushButton* markBtn = box.addButton(tr("Mark unused only"), QMessageBox::AcceptRole);
		QPushButton* removeBtn = inDoc ? box.addButton(tr("Remove from page too"), QMessageBox::DestructiveRole) : nullptr;
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(QMessageBox::Cancel);
		box.exec();
		if (box.clickedButton() == markBtn)
		{
			QString err;
			if (markOnServer(QStringList() << sid, m_fetchPageId, "BALANCED", &err))
			{
				setRowStatus(sid, "BALANCED");
				m_statusLabel->setText(tr("Marked unused on the server: %1").arg(title));
			}
			else
				m_statusLabel->setText(tr("Error: server refused (%1)").arg(err));
		}
		else if (removeBtn && box.clickedButton() == removeBtn)
			removeStory(inDoc);
	}
}

// "Remove story (mark unused)" from the document: every item of the story
// goes in one undo step, then the server mark. The undo state carries the
// mark, so undo brings the frames back and marks the story used again.
void SuneerNewsPanel::removeStory(PageItem* item)
{
	if (!m_doc || !isNewsStory(item))
		return;
	const QString storyId = attr(item, "news.story");
	if (storyItems(storyId).isEmpty())
		return;
	if (QMessageBox::question(this, tr("Remove story"),
	        tr("Remove '%1' from the page and mark it unused on the server?").arg(storyId),
	        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;

	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Remove story: %1").arg(storyId), QString(), Um::IDelete);
	QString err;
	const bool marked = removeStoryFrames(item, unusedStatus(), &err);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	if (marked)
		m_statusLabel->setText(tr("Removed from page and marked unused on the server: %1").arg(storyId));
	else
		m_statusLabel->setText(tr("Removed from page, but NOT marked unused on the server (%1) - use 'Mark unused' in News Browser later").arg(err));
}

// Shared by removeStory() and the bulk Mark unused: delete every item of
// the story (text, photos, captions - found by the story-id attribute),
// mark it unused on the server, record the mark for undo/redo. Returns the
// server result; the frames are gone either way.
bool SuneerNewsPanel::removeStoryFrames(PageItem* item, const QString& status, QString* error)
{
	const QString storyId = attr(item, "news.story");
	const QString sid = attr(item, "news.serverId");
	const QString pageId = attr(item, "news.pageId");
	const QList<PageItem*> items = storyItems(storyId);
	if (items.isEmpty())
	{
		if (error) *error = tr("no frames of this story in the document");
		return false;
	}
	// Delete through the document's own selection, exactly like the Delete
	// key does: the GUI observers are then told about the right items.
	m_doc->m_Selection->clear();
	for (PageItem* it : items)
		m_doc->m_Selection->addItem(it);
	m_removing = true;
	m_doc->itemSelection_DeleteItem();
	m_removing = false;

	QString err;
	const bool marked = !sid.isEmpty() && !pageId.isEmpty() && markOnServer(QStringList() << sid, pageId, status, &err);
	if (!marked && err.isEmpty())
		err = (sid.isEmpty() || pageId.isEmpty()) ? tr("the frames carry no story id / page id") : tr("server refused");
	if (UndoManager::undoEnabled() && !sid.isEmpty())
	{
		// Replayed by restore(): undo -> USED again, redo -> BALANCED again.
		auto* ss = new SimpleState(tr("Story status on server"), QString(), Um::IDelete);
		ss->set("SUNEER_NEWS_DTP");
		ss->set("SERVER_ID", sid);
		ss->set("PAGE_ID", pageId);
		ss->set("STORY", storyId);
		ss->set("WANT", status);
		UndoManager::instance()->action(this, ss);
	}
	if (marked)
		setRowStatus(sid, status);
	if (error) *error = err;
	return marked;
}

// "Mark unused (N)": every ticked used row. One confirmation that lists the
// headlines and, when some are still in this document, offers to remove
// them from the page too (one undo step for all). Each story is then marked
// on the server exactly as the single "Mark as unused" does; a story the
// server refuses stays used and its reason goes into the summary.
static QList<QJsonObject> suneerTickedRows(QListWidget* list, const QJsonArray& data, std::function<bool(const QJsonObject&)> keep)
{
	QList<QJsonObject> rows;
	for (int i = 0; i < list->count(); ++i)
	{
		const QListWidgetItem* row = list->item(i);
		if (row->checkState() != Qt::Checked)
			continue;
		const int idx = row->data(Qt::UserRole).toInt();
		if (idx >= 0 && idx < data.size() && keep(data[idx].toObject()))
			rows << data[idx].toObject();
	}
	return rows;
}

void SuneerNewsPanel::onMarkUnusedTicked()
{
	const QList<QJsonObject> rows = suneerTickedRows(m_newsList, m_newsData, [](const QJsonObject& n) { return newsDtpStatus(n) != "UNUSED"; });
	if (rows.isEmpty()) { m_statusLabel->setText(tr("Tick one or more used or balance stories first")); return; }
	markStories(rows, unusedStatus());
}

void SuneerNewsPanel::onMarkBalanceTicked()
{
	const QList<QJsonObject> rows = suneerTickedRows(m_newsList, m_newsData, [](const QJsonObject& n) { return newsDtpStatus(n) != "BALANCED"; });
	if (rows.isEmpty()) { m_statusLabel->setText(tr("Tick one or more free or used stories first")); return; }
	m_markingBalance = true;
	markStories(rows, "BALANCED");
	m_markingBalance = false;
}

// One confirmation that lists the headlines and, when some are still in
// this document, offers to remove them from the page too (one undo step for
// all). Each story is then marked on the server; a story the server refuses
// keeps its status and its reason goes into the summary. Only the changed
// rows are refreshed - no re-fetch, no document scan.
void SuneerNewsPanel::markStories(const QList<QJsonObject>& rows, const QString& status)
{
	if (rows.isEmpty())
		return;
	// Wording: what the user asked for, not the wire value (today "unused" is
	// still sent as BALANCED, see unusedStatus()).
	const bool askedBalance = m_markingBalance;
	const QString what = askedBalance ? tr("BALANCE (kept for a later day)") : tr("unused (available to everyone again)");
	QStringList titles;
	int inDocCount = 0;
	for (const QJsonObject& n : rows)
	{
		const QString t = collapseSpaces(n["title"].toString()).left(60);
		const bool inDoc = m_doc && findStoryItemInDoc(newsServerId(n));
		if (inDoc) ++inDocCount;
		titles << QStringLiteral("\u2022 ") + (t.isEmpty() ? tr("(untitled)") : t) + (inDoc ? tr("  [in this document]") : QString());
	}
	QMessageBox box(this);
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(askedBalance ? tr("Mark balance") : tr("Mark unused"));
	box.setText(tr("Mark these %n story(ies) as %1 on the server?", "", rows.size()).arg(what));
	box.setInformativeText(titles.join("\n"));
	QCheckBox* removeChk = nullptr;
	if (inDocCount > 0)
	{
		removeChk = new QCheckBox(tr("Also remove these %n story(ies) from the page (one undo step)", "", inDocCount), &box);
		removeChk->setChecked(true);
		box.setCheckBox(removeChk);
	}
	QPushButton* okBtn = box.addButton(askedBalance ? tr("Mark balance") : tr("Mark unused"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(okBtn);
	box.exec();
	if (box.clickedButton() != okBtn)
		return;
	const bool removeToo = removeChk && removeChk->isChecked() && m_doc;

	UndoTransaction tx;
	if (removeToo && UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Remove %n story(ies)", "", inDocCount), QString(), Um::IDelete);
	QStringList released, failed;
	for (const QJsonObject& n : rows)
	{
		const QString sid = newsServerId(n);
		const QString title = collapseSpaces(n["title"].toString()).left(40);
		QString err;
		bool ok = false;
		PageItem* inDoc = removeToo ? findStoryItemInDoc(sid) : nullptr;
		if (inDoc)
			ok = removeStoryFrames(inDoc, status, &err);   // frames gone + server mark + undo record
		else if (markOnServer(QStringList() << sid, m_fetchPageId, status, &err))
		{
			setRowStatus(sid, status);                  // this row only, no re-fetch
			ok = true;
		}
		if (ok)
			released << title;
		else
			failed << tr("%1: %2").arg(title, err.isEmpty() ? tr("server refused") : err);
	}
	if (tx)
		tx.commit();
	if (removeToo)
	{
		m_doc->m_Selection->clear();
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
		if (m_mw)
			m_mw->emitUpdateRequest(0);
	}
	if (m_selectUsedChk)
	{
		m_selectUsedChk->blockSignals(true);
		m_selectUsedChk->setChecked(false);
		m_selectUsedChk->blockSignals(false);
	}
	const QString done = askedBalance ? tr("Marked balance: %1") : tr("Marked unused: %1");
	m_statusLabel->setText(done.arg(released.size()) + (failed.isEmpty() ? QString() : tr(", refused: %1").arg(failed.size())));
	if (!failed.isEmpty())
	{
		QMessageBox::warning(this, box.windowTitle(),
			tr("%n story(ies) could not be marked and keep their status:", "", failed.size()) + "\n\n" + failed.join("\n")
			+ (released.isEmpty() ? QString() : "\n\n" + done.arg(released.join(", "))));
	}
	else if (rows.size() > 1)
		QMessageBox::information(this, box.windowTitle(), tr("All %n stories are marked %1 on the server.", "", rows.size()).arg(askedBalance ? tr("balance") : tr("unused"))
			+ (removeToo && inDocCount > 0 ? "\n" + tr("%n removed from the page (Undo brings them back and marks them used again).", "", inDocCount) : QString()));
}

void SuneerNewsPanel::restore(UndoState* state, bool isUndo)
{
	auto* ss = dynamic_cast<SimpleState*>(state);
	if (!ss || !ss->contains("SUNEER_NEWS_DTP"))
		return;
	const QString sid = ss->get("SERVER_ID");
	const QString pageId = ss->get("PAGE_ID");
	const QString story = ss->get("STORY");
	const QString want = isUndo ? QStringLiteral("USED") : (ss->contains("WANT") ? ss->get("WANT") : QStringLiteral("BALANCED"));
	QString err;
	if (markOnServer(QStringList() << sid, pageId, want, &err))
	{
		setRowStatus(sid, want);
		m_statusLabel->setText(isUndo ? tr("Story back on the page and marked used again: %1").arg(story)
		                              : tr("Story removed again and marked unused: %1").arg(story));
	}
	else
		m_statusLabel->setText(tr("Warning: frames %1, but the server mark failed (%2)")
		                       .arg(isUndo ? tr("restored") : tr("removed"), err));
}

// Plain Delete on story frames: a normal delete, no server call. Say so once.
void SuneerNewsPanel::deleteKeyHint()
{
	if (m_removing || m_deleteHintShown)
		return;
	m_deleteHintShown = true;
	if (m_mw)
		m_mw->setStatusBarInfoText(tr("Story frames deleted. To make the story available again, use right-click > Remove story (mark unused), or Mark as unused in the News Browser."));
	m_statusLabel->setText(tr("Hint: a plain delete does not change the server; use Remove story (mark unused)."));
}

// Release from the document (right-click on a placed story item).
void SuneerNewsPanel::releaseStory(PageItem* item)
{
	if (!item)
		return;
	const QString sid = attr(item, "news.serverId");
	const QString pageId = attr(item, "news.pageId");
	if (sid.isEmpty() || pageId.isEmpty())
	{
		m_statusLabel->setText(tr("This story carries no server id / page id"));
		return;
	}
	const QString name = attr(item, "news.story");
	if (QMessageBox::question(this, tr("Mark as unused"),
	        tr("Mark \"%1\" as unused on the server?\n(The frames stay in the document.)").arg(name),
	        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	QString err;
	if (markOnServer(QStringList() << sid, pageId, unusedStatus(), &err))
	{
		setRowStatus(sid, unusedStatus());
		m_statusLabel->setText(tr("Marked unused on the server: %1").arg(name));
	}
	else
		m_statusLabel->setText(tr("Error: server refused (%1)").arg(err));
}

void SuneerNewsPanel::balanceStory(PageItem* item)
{
	if (!item)
		return;
	const QString sid = attr(item, "news.serverId");
	const QString pageId = attr(item, "news.pageId");
	if (sid.isEmpty() || pageId.isEmpty())
	{
		m_statusLabel->setText(tr("This story carries no server id / page id"));
		return;
	}
	const QString name = attr(item, "news.story");
	if (QMessageBox::question(this, tr("Mark as balance"),
	        tr("Mark \"%1\" as BALANCE on the server (kept for a later day)?\n(The frames stay in the document.)").arg(name),
	        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	QString err;
	if (markOnServer(QStringList() << sid, pageId, "BALANCED", &err))
	{
		setRowStatus(sid, "BALANCED");
		m_statusLabel->setText(tr("Marked balance on the server: %1").arg(name));
	}
	else
		m_statusLabel->setText(tr("Error: server refused (%1)").arg(err));
}

QNetworkReply* SuneerNewsPanel::postBlocking(const QNetworkRequest& req, const QByteArray& body)
{
	QNetworkReply* reply = m_nam->post(req, body);
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	reply->deleteLater();
	return reply;
}

// ============================================================================
// Session: login, refresh, logout, keychain
// ============================================================================

void SuneerNewsPanel::ensureSecretsLoaded()
{
	if (m_secretsLoaded)
		return;
	m_secretsLoaded = true;
	m_accessToken  = ScUpdateKeyStore::loadSecret(kSecretService, kKeyAccess);
	m_refreshToken = ScUpdateKeyStore::loadSecret(kSecretService, kKeyRefresh);
	m_deviceId     = ScUpdateKeyStore::loadSecret(kSecretService, kKeyDevice);
	m_userName     = ScUpdateKeyStore::loadSecret(kSecretService, kKeyUserName);
	if (m_deviceId.isEmpty())
	{
		// One UUID per machine, generated once and kept with the tokens.
		m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		ScUpdateKeyStore::storeSecret(kSecretService, kKeyDevice, m_deviceId);
	}
}

void SuneerNewsPanel::saveSession()
{
	auto put = [](const QString& key, const QString& value) {
		if (value.isEmpty())
			ScUpdateKeyStore::clearSecret(kSecretService, key);
		else
			ScUpdateKeyStore::storeSecret(kSecretService, key, value);
	};
	put(kKeyAccess, m_accessToken);
	put(kKeyRefresh, m_refreshToken);
	put(kKeyUserName, m_userName);
}

void SuneerNewsPanel::clearSession()
{
	m_accessToken.clear();
	m_refreshToken.clear();
	m_userName.clear();
	m_allowedEditions.clear();
	m_allowedShortNames.clear();
	m_userInfoLoaded = false;
	saveSession();
}

// The refresh token only ever travels as an HttpOnly cookie.
void SuneerNewsPanel::captureRefreshCookie(QNetworkReply* reply)
{
	for (const QByteArray& name : reply->rawHeaderList())
	{
		if (name.compare("Set-Cookie", Qt::CaseInsensitive) != 0)
			continue;
		for (const QNetworkCookie& ck : QNetworkCookie::parseCookies(reply->rawHeader(name)))
			if (ck.name() == "refreshToken" && !ck.value().isEmpty())
				m_refreshToken = QString::fromUtf8(ck.value());
	}
}

void SuneerNewsPanel::setLocked(bool locked)
{
	m_stack->setCurrentIndex(locked ? 0 : 1);
	if (!locked)
	{
		m_userLabel->setText(m_userName.isEmpty() ? tr("Logged in") : tr("Logged in as %1").arg(m_userName));
		QString tip;
		if (!m_clientIp.isEmpty())
			tip = tr("The server sees your address as %1.").arg(m_clientIp);
		if (!m_ipAllowed)
			tip += "\n" + tr("That address is not approved in the server's IP list (code %1). "
			                 "The web app would block its screens; Scribus requests are not affected.").arg(m_ipCode);
		m_userLabel->setToolTip(tip);
	}
}

// The server tells which editions a user may use through user.editionGroups[]
// {edition:{id,name}, group:{…}} - the same list the web app's edition
// selector shows. There is no "all editions" flag on the server; an account
// with no editionGroups has no editions. Both the login reply and /users/me
// carry the list.
void SuneerNewsPanel::parseUser(const QJsonObject& user)
{
	QString name = (user["firstName"].toString() + " " + user["lastName"].toString()).trimmed();
	if (!name.isEmpty())
		m_userName = name;
	// The server's IP table (Admin > IP access): clientIp is what it saw,
	// code is the approval code, ipAllowed the table's verdict. The API does
	// not enforce it (its checkIP middleware is disabled); only the web app
	// blocks its own screens on it. Scribus requests are never affected.
	m_clientIp  = user["clientIp"].toString();
	m_ipCode    = user["code"].toInt();
	m_ipAllowed = !user.contains("ipAllowed") || user["ipAllowed"].toBool();
	m_allowedEditions.clear();
	m_allowedShortNames.clear();
	QSet<QString> seen;
	for (const QJsonValue& v : user["editionGroups"].toArray())
	{
		const QJsonObject eg = v.toObject();
		const QJsonObject ed = eg["edition"].toObject();
		QString id = ed["id"].toString();
		if (id.isEmpty())
			id = eg["editionId"].toString();
		if (id.isEmpty() || seen.contains(id))
			continue;
		seen.insert(id);
		m_allowedEditions << qMakePair(id, ed["name"].toString());
	}
	m_userInfoLoaded = true;
}

bool SuneerNewsPanel::fetchCurrentUser()
{
	bool ok = false;
	apiGet(QUrl(apiUrl("/users/me")), [this, &ok](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError)
		{
			m_statusLabel->setText(tr("Error: ") + tr("User profile: %1").arg(describeError(reply)));
			return;
		}
		parseUser(QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject());
		saveSession();
		ok = true;
	});
	if (ok && m_stack->currentIndex() == 1)
		m_userLabel->setText(tr("Logged in as %1").arg(m_userName));
	return ok;
}

bool SuneerNewsPanel::editionAllowed(const QString& shortName) const
{
	return m_allowedShortNames.contains(shortName);
}

void SuneerNewsPanel::setEditionsEnabled(bool on)
{
	m_editionCombo->setEnabled(on);
	m_pageCombo->setEnabled(on);
	m_dateEdit->setEnabled(on);
	m_fetchBtn->setEnabled(on);
	if (!on)
	{
		m_pageCombo->clear();
		m_newsList->clear();
		m_newsData = QJsonArray();
		m_placeBtn->setEnabled(false);
	}
}

// Start-up: a saved refresh token is tried silently; otherwise stay locked.
void SuneerNewsPanel::startupSession()
{
	// Test/offline aid: a captured news reply on disk instead of the server.
	// Nothing is sent anywhere; the panel just opens with that file.
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
	{
		m_userName = tr("fixture %1").arg(QFileInfo(qEnvironmentVariable("SCRIBUS_NEWS_FIXTURE")).fileName());
		m_allowedEditions << qMakePair(QStringLiteral("fixture"), QStringLiteral("Fixture"));
		m_userInfoLoaded = true;
		setLocked(false);
		setEditionsEnabled(true);
		m_editionCombo->addItem("FIX - Fixture", "FIX");
		m_allowedShortNames.insert("FIX");
		m_pageCombo->addItem("Fixture page", "fixture");
		m_statusLabel->setText(tr("Fixture mode: Fetch News reads the file"));
		return;
	}
	ensureSecretsLoaded();
	if (m_refreshToken.isEmpty())
	{
		m_accessToken.clear();
		setLocked(true);
		return;
	}
	m_quietAuth = true;
	const bool ok = refreshAccessToken();
	m_quietAuth = false;
	if (!ok)
	{
		clearSession();
		setLocked(true);
		m_lockLabel->setText(tr("Saved session is no longer valid - please log in."));
		return;
	}
	m_quietAuth = true;
	fetchCurrentUser();
	m_quietAuth = false;
	setLocked(false);
	onFetchStatic();
}

bool SuneerNewsPanel::refreshAccessToken()
{
	ensureSecretsLoaded();
	if (m_refreshToken.isEmpty() || m_deviceId.isEmpty())
		return false;
	QNetworkRequest req = apiRequest(QUrl(apiUrl("/users/auth/refresh")), false);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setRawHeader("x-device-id", m_deviceId.toUtf8());
	req.setRawHeader("Cookie", "refreshToken=" + m_refreshToken.toUtf8());
	QNetworkReply* reply = postBlocking(req, QByteArray("{}"));
	const int status = httpStatus(reply);
	if (reply->error() != QNetworkReply::NoError || status != 200)
	{
		if (status == 401 || status == 403)
			m_refreshToken.clear();   // refused: the cookie is dead
		return false;
	}
	captureRefreshCookie(reply);
	const QString token = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject()["accessToken"].toString();
	if (token.isEmpty())
		return false;
	m_accessToken = token;
	saveSession();
	return true;
}

bool SuneerNewsPanel::doLogin(const QString& email, const QString& password, QString& error)
{
	ensureSecretsLoaded();
	QJsonObject body;
	body["email"]    = email;
	body["password"] = password;
	body["deviceId"] = m_deviceId;
	QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
	body.remove("password");
	QNetworkRequest req = apiRequest(QUrl(apiUrl("/users/auth/login")), false);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	QNetworkReply* reply = postBlocking(req, bytes);
	bytes.fill('\0');   // the password is not kept around
	if (reply->error() != QNetworkReply::NoError || httpStatus(reply) != 200)
	{
		// The server says why: wrong password, disabled account, too many
		// attempts ... Show that; fall back to the transport error.
		const QString msg = serverMessage(reply->peek(reply->bytesAvailable()));
		error = msg.isEmpty() ? describeError(reply)
		                      : tr("%1 (HTTP %2)").arg(msg).arg(httpStatus(reply));
		return false;
	}
	captureRefreshCookie(reply);
	const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
	m_accessToken = data["accessToken"].toString();
	if (m_accessToken.isEmpty())
	{
		error = tr("Login reply carried no access token");
		return false;
	}
	m_userName.clear();
	parseUser(data["user"].toObject());
	if (m_userName.isEmpty())
		m_userName = email;
	saveSession();
	return true;
}

bool SuneerNewsPanel::loginDialog()
{
	if (m_loginDialogOpen)
		return false;
	m_loginDialogOpen = true;
	QDialog dlg(this);
	dlg.setWindowTitle(tr("News Browser - Login"));
	dlg.setMinimumWidth(380);
	QFormLayout* fl = new QFormLayout(&dlg);
	QLineEdit* emailEdit = new QLineEdit(settingsString("loginEmail", QString()), &dlg);
	emailEdit->setPlaceholderText("name@example.com");
	QLineEdit* passEdit = new QLineEdit(&dlg);
	passEdit->setEchoMode(QLineEdit::Password);
	QLabel* info = new QLabel(tr("Server: %1").arg(m_apiHost), &dlg);
	info->setStyleSheet("color:#666; font-size:11px;");
	QLabel* err = new QLabel(&dlg);
	err->setStyleSheet("color:#B00020;");
	err->setWordWrap(true);
	fl->addRow(info);
	fl->addRow(tr("Email:"), emailEdit);
	fl->addRow(tr("Password:"), passEdit);
	fl->addRow(err);
	QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	bb->button(QDialogButtonBox::Ok)->setText(tr("Login"));
	bb->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
	fl->addRow(bb);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	// Each click is one deliberate attempt; nothing is retried on its own
	// (repeated failures lock the account server-side).
	connect(bb, &QDialogButtonBox::accepted, &dlg, [&]() {
		const QString email = emailEdit->text().trimmed();
		if (email.isEmpty() || passEdit->text().isEmpty())
		{
			err->setText(tr("Enter email and password"));
			return;
		}
		QString e;
		bb->setEnabled(false);
		const bool ok = doLogin(email, passEdit->text(), e);
		passEdit->clear();
		bb->setEnabled(true);
		if (ok)
		{
			QSettings s("Faircode", "ScribusNews");
			s.setValue("loginEmail", email);
			dlg.accept();
		}
		else
			err->setText(e);
	});
	(emailEdit->text().isEmpty() ? emailEdit : passEdit)->setFocus();
	const bool ok = (dlg.exec() == QDialog::Accepted);
	m_loginDialogOpen = false;
	return ok;
}

void SuneerNewsPanel::onLogin()
{
	ensureSecretsLoaded();
	if (!loginDialog())
		return;
	setLocked(false);
	m_statusLabel->setText(tr("Logged in as %1").arg(m_userName));
	onFetchStatic();
}

void SuneerNewsPanel::onLogout()
{
	ensureSecretsLoaded();
	if (!m_accessToken.isEmpty())
	{
		// Best effort: the server revokes the refresh session it finds in the cookie.
		QNetworkRequest req = apiRequest(QUrl(apiUrl("/users/auth/logout")), true);
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		if (!m_refreshToken.isEmpty())
			req.setRawHeader("Cookie", "refreshToken=" + m_refreshToken.toUtf8());
		postBlocking(req, QByteArray("{}"));
	}
	clearSession();
	m_newsList->clear();
	m_newsData = QJsonArray();
	m_placeBtn->setEnabled(false);
	setLocked(true);
	m_lockLabel->setText(tr("Logged out."));
}

// ============================================================================
// Editions / pages / news (the /external routes, no login)
// ============================================================================

void SuneerNewsPanel::onRefresh()
{
	m_newsList->clear();
	m_newsData = QJsonArray();
	m_placeBtn->setEnabled(false);
	if (isLoggedIn())
		fetchCurrentUser();
	onFetchStatic();
}

// Edition list: /external/editions is gone, so the public web bootstrap is
// used; when that fails too, the short names from Settings.
void SuneerNewsPanel::onFetchStatic()
{
	m_statusLabel->setText(tr("Loading editions from %1 ...").arg(QUrl(m_apiHost).host()));
	m_editionCombo->blockSignals(true);
	m_editionCombo->clear();
	QString note;
	apiGet(QUrl(apiUrl("/common/web/initial-data/static")), [this, &note](QNetworkReply* reply) {
		m_editions = QJsonArray();
		if (reply->error() == QNetworkReply::NoError)
			m_editions = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject()["editions"].toArray();
		if (m_editions.isEmpty())
			note = describeError(reply);
	});
	m_allowedShortNames.clear();
	if (m_allowedEditions.isEmpty())
	{
		m_editionCombo->blockSignals(false);
		setEditionsEnabled(false);
		m_statusLabel->setText(m_userInfoLoaded
			? tr("No editions assigned to your account")
			: tr("Error: Could not load your profile - use Refresh"));
		return;
	}
	setEditionsEnabled(true);

	// Only the editions the server assigned to this user, matched by id with
	// the public list (which carries the short name the pages route needs).
	QList<QJsonObject> eds;
	QSet<QString> matched;
	for (const QJsonValue& v : m_editions)
	{
		const QJsonObject ed = v.toObject();
		if (ed["shortName"].toString().isEmpty())
			continue;
		for (const auto& allowed : m_allowedEditions)
			if (allowed.first == ed["id"].toString())
			{
				eds << ed;
				matched.insert(allowed.first);
				break;
			}
	}
	if (!eds.isEmpty())
	{
		std::sort(eds.begin(), eds.end(), [](const QJsonObject& a, const QJsonObject& b) {
			const bool pa = a["isPrintEdition"].toBool(), pb = b["isPrintEdition"].toBool();
			if (pa != pb) return pa;
			return a["name"].toString().localeAwareCompare(b["name"].toString()) < 0;
		});
		for (const QJsonObject& ed : eds)
		{
			const QString sn = ed["shortName"].toString();
			m_editionCombo->addItem(QStringLiteral("%1 - %2").arg(sn, ed["name"].toString()), sn);
			m_allowedShortNames.insert(sn);
		}
		const int unmatched = m_allowedEditions.size() - matched.size();
		m_statusLabel->setText(unmatched > 0
			? tr("%1 of your %2 editions available (%3 not in the public list)").arg(eds.size()).arg(m_allowedEditions.size()).arg(unmatched)
			: tr("%1 editions available to you").arg(eds.size()));
	}
	else
	{
		// Public list missing: the pages route needs a short name, which the
		// assignment does not carry. Try the configured short names by name.
		const QStringList configured = m_editionShortNames.split(',', Qt::SkipEmptyParts);
		for (const auto& allowed : m_allowedEditions)
		{
			QString sn;
			for (const QString& cand : configured)
				if (cand.trimmed().compare(allowed.second, Qt::CaseInsensitive) == 0
				    || allowed.second.startsWith(cand.trimmed(), Qt::CaseInsensitive))
					{ sn = cand.trimmed(); break; }
			if (sn.isEmpty())
				continue;
			m_editionCombo->addItem(QStringLiteral("%1 - %2").arg(sn, allowed.second), sn);
			m_allowedShortNames.insert(sn);
		}
		if (m_editionCombo->count() == 0)
		{
			m_editionCombo->blockSignals(false);
			setEditionsEnabled(false);
			m_statusLabel->setText(tr("Warning: Edition list unavailable (%1) - short names for your editions unknown").arg(
				note.isEmpty() ? tr("no shortName in reply") : note));
			return;
		}
		m_statusLabel->setText(tr("Warning: Edition list unavailable (%1) - using configured short names").arg(
			note.isEmpty() ? tr("no shortName in reply") : note));
	}
	int idx = m_lastEdition.isEmpty() ? -1 : m_editionCombo->findData(m_lastEdition);
	m_editionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	m_editionCombo->blockSignals(false);
	onEditionChanged(m_editionCombo->currentIndex());
}

void SuneerNewsPanel::onEditionChanged(int index)
{
	m_pageCombo->clear();
	m_pages = QJsonArray();
	if (index < 0)
		return;
	m_lastEdition = m_editionCombo->itemData(index).toString();   // the short name
	saveConfig();
	onFetchPages();
}

// Pages are always requested with the edition SHORT NAME.
void SuneerNewsPanel::onFetchPages()
{
	if (m_lastEdition.isEmpty())
		return;
	if (!editionAllowed(m_lastEdition))
	{
		m_statusLabel->setText(tr("Error: Edition %1 is not assigned to your account").arg(m_lastEdition));
		return;
	}
	m_statusLabel->setText(tr("Loading pages for %1 ...").arg(m_lastEdition));
	apiGet(QUrl(apiUrl("/external/edition-pages/" + m_lastEdition)), [this](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError)
		{
			m_statusLabel->setText(tr("Error: ") + tr("Pages for %1: %2").arg(m_lastEdition, describeError(reply)));
			return;
		}
		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		m_pages = o["data"].toArray();
		if (m_pages.isEmpty())
		{
			// The route answers HTTP 200 with success:false for an unknown edition.
			const QString msg = o["error"].toObject()["message"].toString();
			m_statusLabel->setText(tr("Warning: No pages for edition %1%2").arg(m_lastEdition, msg.isEmpty() ? QString() : ": " + msg));
			return;
		}
		QList<QJsonObject> pages;
		for (const QJsonValue& v : m_pages)
			pages << v.toObject();
		std::stable_sort(pages.begin(), pages.end(), [](const QJsonObject& a, const QJsonObject& b) {
			return a["displayOrder"].toInt() < b["displayOrder"].toInt();
		});
		m_pageCombo->blockSignals(true);
		for (const QJsonObject& pg : pages)
			m_pageCombo->addItem(pg["name"].toString(), pg["id"].toString());
		int idx = m_lastPage.isEmpty() ? -1 : m_pageCombo->findText(m_lastPage);
		if (idx >= 0)
			m_pageCombo->setCurrentIndex(idx);
		m_pageCombo->blockSignals(false);
		m_statusLabel->setText(tr("%1: %2 pages").arg(m_lastEdition).arg(pages.size()));
	});
}

void SuneerNewsPanel::onFetchNews()
{
	if (m_pageCombo->count() == 0 || m_pageCombo->currentIndex() < 0)
	{
		m_statusLabel->setText(tr("Choose an edition and a page first"));
		return;
	}
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
	{
		QFile f(qEnvironmentVariable("SCRIBUS_NEWS_FIXTURE"));
		if (f.open(QIODevice::ReadOnly))
		{
			const QJsonValue data = QJsonDocument::fromJson(f.readAll()).object()["data"];
			m_newsData = data.isArray() ? data.toArray() : data.toObject()["newses"].toArray();
			m_fetchDate = m_dateEdit->date().toString("yyyy-MM-dd");
			showNewsList();
		}
		else
			m_statusLabel->setText(tr("Error: Fixture file not readable"));
		return;
	}
	if (!editionAllowed(m_lastEdition))
	{
		m_statusLabel->setText(tr("Error: Edition %1 is not assigned to your account").arg(m_lastEdition));
		return;
	}
	m_fetchPageId = m_pageCombo->currentData().toString();
	m_fetchDate   = m_dateEdit->date().toString("yyyy-MM-dd");
	m_lastPage    = m_pageCombo->currentText();
	saveConfig();
	m_statusLabel->setText(tr("Fetching news..."));
	m_newsList->clear();
	m_newsData = QJsonArray();
	m_placeBtn->setEnabled(false);
	m_fetchBtn->setEnabled(false);
	QUrl url(apiUrl("/external/edition-pages/" + m_fetchPageId + "/news"));
	QUrlQuery q;
	q.addQueryItem("publishDate", m_fetchDate);
	url.setQuery(q);
	apiGet(url, [this](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError)
		{
			m_statusLabel->setText(tr("Error: ") + tr("News: %1").arg(describeError(reply)));
			return;
		}
		const QJsonValue data = QJsonDocument::fromJson(reply->readAll()).object()["data"];
		// The /external route answers data[]; a newer shape would be data.newses[].
		m_newsData = data.isArray() ? data.toArray() : data.toObject()["newses"].toArray();
		showNewsList();
	});
	m_fetchBtn->setEnabled(true);
}

QString SuneerNewsPanel::collapseSpaces(const QString& text)
{
	static const QRegularExpression ws(QStringLiteral("\\s+"));
	return QString(text).replace(ws, QStringLiteral(" ")).trimmed();
}

// Elide at the end on grapheme-cluster boundaries, so a Malayalam conjunct
// or a base letter with its vowel sign is never cut in half.
QString SuneerNewsPanel::elideGraphemes(const QString& text, const QFontMetrics& fm, int width)
{
	if (width <= 0 || fm.horizontalAdvance(text) <= width)
		return text;
	const QString ellipsis = QStringLiteral("\u2026");
	const int avail = width - fm.horizontalAdvance(ellipsis);
	QTextBoundaryFinder bf(QTextBoundaryFinder::Grapheme, text);
	int lastGood = 0;
	int pos = 0;
	while ((pos = bf.toNextBoundary()) != -1)
	{
		if (fm.horizontalAdvance(text.left(pos)) > avail)
			break;
		lastGood = pos;
	}
	return text.left(lastGood).trimmed() + ellipsis;
}

void SuneerNewsPanel::showNewsList()
{
	m_newsList->clear();
	for (int i = 0; i < m_newsData.size(); i++)
	{
		const QJsonObject news = m_newsData[i].toObject();
		const QString title  = collapseSpaces(news["title"].toString());
		const QString kicker = collapseSpaces(news["kicker"].toString());
		const QString byline = collapseSpaces(news["byline"].toString());
		const int imgCount   = news["newsMedia"].toArray().size();
		const bool hasImg    = imgCount > 0;
		QString label = title.isEmpty() ? kicker : title;
		if (label.isEmpty())
			label = tr("(untitled)");
		QListWidgetItem* item = new QListWidgetItem(label);
		item->setData(Qt::UserRole + 1, label);   // full text; relabelNewsList() elides
		QStringList tip;
		if (!kicker.isEmpty())
			tip << tr("Kicker: %1").arg(kicker);
		tip << tr("Title: %1").arg(title);
		if (!byline.isEmpty())
			tip << tr("Byline: %1").arg(byline);
		if (imgCount > 1)
			tip << tr("Has %1 photos").arg(imgCount);
		else if (hasImg)
			tip << tr("Has photo");
		else
			tip << tr("Text only");
		if (!news["refId"].toString().isEmpty())
			tip << news["refId"].toString();
		item->setToolTip(tip.join("\n"));
		item->setData(Qt::UserRole + 3, tip.join("\n"));
		item->setData(Qt::UserRole, i);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		if (hasImg)
			item->setIcon(photoIcon(imgCount));
		m_newsList->addItem(item);
	}
	relabelNewsList();
	refreshPlacedMarks();
	if (m_newsData.isEmpty())
		m_statusLabel->setText(tr("No news for %1 on %2").arg(m_pageCombo->currentText(), m_fetchDate));
	else
		m_statusLabel->setText(tr("%1 news items - Ctrl+Click to multi-select").arg(m_newsData.size()));
	m_placeBtn->setEnabled(!m_newsData.isEmpty());
}

// The theme's picture-frame icon; two or more photos get the count drawn
// into the lower right corner, so the list label itself stays title-only.
QIcon SuneerNewsPanel::photoIcon(int count)
{
	const int sz = 24;
	QPixmap pm(sz, sz);
	pm.fill(Qt::transparent);
	QPixmap base = IconManager::instance().loadIcon(count > 1 ? "image-collection" : "tool-insert-image", 20).pixmap(20, 20);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.drawPixmap(0, 2, base);
	if (count > 1)
	{
		const QString n = QString::number(count);
		QFont f = p.font();
		f.setPixelSize(10);
		f.setBold(true);
		p.setFont(f);
		const int bw = qMax(13, QFontMetrics(f).horizontalAdvance(n) + 6);
		QRect badge(sz - bw, sz - 13, bw, 13);
		p.setPen(QPen(Qt::white, 1));
		p.setBrush(QColor("#D32F2F"));
		p.drawRoundedRect(badge, 4, 4);
		p.setPen(Qt::white);
		p.drawText(badge, Qt::AlignCenter, n);
	}
	p.end();
	return QIcon(pm);
}

void SuneerNewsPanel::relabelNewsList()
{
	const QFontMetrics fm(m_newsList->font());
	// checkbox + icon + paddings; the rest is text
	const int reserved = 24 + 24 + 16;
	const int width = m_newsList->viewport()->width() - reserved;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		QListWidgetItem* item = m_newsList->item(i);
		const QString full = item->data(Qt::UserRole + 1).toString();
		item->setText(elideGraphemes(full, fm, width));
	}
}

bool SuneerNewsPanel::eventFilter(QObject* obj, QEvent* ev)
{
	if (m_newsList && obj == m_newsList->viewport() && ev->type() == QEvent::Resize)
		relabelNewsList();
	return QDockWidget::eventFilter(obj, ev);
}

void SuneerNewsPanel::onNewsItemClicked(QListWidgetItem* item)
{
	// Clicks on the checkbox only toggle.
	QPoint pos = m_newsList->viewport()->mapFromGlobal(QCursor::pos());
	QRect itemRect = m_newsList->visualItemRect(item);
	if (pos.x() - itemRect.left() < 24)
		return;
	// The preview is modal; wait one double-click interval so a double-click
	// (place / jump to the placed story) can still arrive.
	if (!m_previewTimer)
	{
		m_previewTimer = new QTimer(this);
		m_previewTimer->setSingleShot(true);
		connect(m_previewTimer, &QTimer::timeout, this, [this]() {
			QListWidgetItem* row = m_previewRow;
			m_previewRow = nullptr;
			if (!row)
				return;
			int idx = row->data(Qt::UserRole).toInt();
			if (idx >= 0 && idx < m_newsData.size())
				showPreview(m_newsData[idx].toObject());
		});
	}
	m_previewRow = item;
	m_previewTimer->start(QApplication::doubleClickInterval() + 50);
}

void SuneerNewsPanel::onSelectAll(bool checked)
{
	for (int i = 0; i < m_newsList->count(); i++)
		m_newsList->item(i)->setCheckState(checked && !isPlacedRow(m_newsList->item(i)) ? Qt::Checked : Qt::Unchecked);
}

QString SuneerNewsPanel::newsCaption(const QJsonObject& news)
{
	const QJsonArray media = news["newsMedia"].toArray();
	if (!media.isEmpty())
	{
		const QJsonObject m0 = media[0].toObject();
		QString c = m0["caption"].toString().trimmed();
		if (c.isEmpty())
			c = m0["media"].toObject()["caption"].toString().trimmed();
		if (!c.isEmpty())
			return c;
	}
	return news["caption"].toString().trimmed();
}

QString SuneerNewsPanel::newsHighlights(const QJsonObject& news)
{
	const QJsonValue v = news["highlights"];
	if (v.isArray())
	{
		QStringList parts;
		for (const QJsonValue& p : v.toArray())
			if (!p.toString().trimmed().isEmpty())
				parts << p.toString().trimmed();
		return parts.join("\n");
	}
	return v.toString().trimmed();
}

QStringList SuneerNewsPanel::contentToParagraphs(const QString& content)
{
	QString text = content;
	text.replace("\r\n", "\n");
	text.replace('\r', '\n');
	static const QRegularExpression tagRe(QStringLiteral("<\\s*/?\\s*[A-Za-z][^>]*>"));
	static const QRegularExpression entRe(QStringLiteral("&(#\\d+|#x[0-9A-Fa-f]+|[A-Za-z]+);"));
	const bool looksHtml = text.contains(tagRe) || text.contains(entRe);
	if (looksHtml)
	{
		// Block ends and <br> become paragraph breaks before the HTML parser
		// sees the text, so a run of inline-only markup still splits sensibly.
		static const QRegularExpression brRe(QStringLiteral("<\\s*br\\s*/?\\s*>"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression blockEndRe(QStringLiteral("<\\s*/\\s*(p|div|li|h[1-6]|tr|blockquote)\\s*>"), QRegularExpression::CaseInsensitiveOption);
		text.replace(brRe, "\n");
		text.replace(blockEndRe, "\n");
		// QTextDocumentFragment decodes entities and drops the remaining tags.
		// It does not touch the characters themselves.
		text.replace("\n", "<br>");
		text = QTextDocumentFragment::fromHtml(text).toPlainText();
		text.replace(QChar::ParagraphSeparator, '\n');
		text.replace(QChar::LineSeparator, '\n');
		text.replace(QChar::Nbsp, ' ');
	}
	QStringList out;
	for (const QString& line : text.split('\n'))
	{
		const QString t = line.trimmed();
		if (!t.isEmpty())
			out << t;
	}
	return out;
}

void SuneerNewsPanel::showPreview(const QJsonObject& news)
{
	const QString kicker   = news["kicker"].toString();
	const QString title    = news["title"].toString();
	const QString byline   = news["byline"].toString();
	const QString dateline = news["dateline"].toString();
	const QString highlights = newsHighlights(news);
	const QString caption  = newsCaption(news);
	const QStringList paras = contentToParagraphs(news["content"].toString());
	const bool hasImg = !news["newsMedia"].toArray().isEmpty();
	QString html;
	if (!kicker.isEmpty())
		html += QStringLiteral("<span style='color:#E65100; font-size:26px; font-weight:bold;'>%1</span><br>").arg(kicker.toHtmlEscaped());
	html += QStringLiteral("<b style='font-size:30px;'>%1</b>").arg(title.toHtmlEscaped());
	if (!highlights.isEmpty())
		html += QStringLiteral("<br><span style='font-size:24px; color:#333;'>%1</span>").arg(highlights.toHtmlEscaped().replace("\n", "<br>"));
	if (!byline.isEmpty())
		html += QStringLiteral("<br><span style='color:#666; font-size:24px;'>%1</span>").arg(byline.toHtmlEscaped());
	if (hasImg)
		html += QStringLiteral("<br><span style='color:#1565C0; font-size:24px;'>%1</span>")
		        .arg(caption.isEmpty() ? tr("Has image") : tr("Image: %1").arg(caption.toHtmlEscaped()));
	html += "<br><br>";
	for (int i = 0; i < paras.size(); ++i)
	{
		QString p = paras[i].toHtmlEscaped();
		if (i == 0 && !dateline.isEmpty())
			p = QStringLiteral("<b>%1:</b> ").arg(dateline.toHtmlEscaped()) + p;
		html += QStringLiteral("<p style='font-size:26px;'>%1</p>").arg(p);
	}
	QDialog dlg(this);
	dlg.setWindowTitle(title);
	dlg.setMinimumSize(900, 700);
	QVBoxLayout* vl = new QVBoxLayout(&dlg);
	QTextEdit* te = new QTextEdit(&dlg);
	te->setReadOnly(true);
	te->setHtml(html);
	vl->addWidget(te);
	QPushButton* closeBtn = new QPushButton(tr("Close"), &dlg);
	connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
	vl->addWidget(closeBtn);
	dlg.exec();
}

// ============================================================================
// Placement
// ============================================================================

void SuneerNewsPanel::onPlaceNews()
{
	if (m_previewTimer)
		m_previewTimer->stop();
	m_previewRow = nullptr;
	QListWidgetItem* sel = m_newsList->currentItem();
	if (!sel || !m_doc)
		return;
	int idx = sel->data(Qt::UserRole).toInt();
	if (idx < 0 || idx >= m_newsData.size())
		return;
	const QJsonObject news = m_newsData[idx].toObject();
	if (isPlacedRow(sel))
	{
		// Placed (server says USED): if it is in this document, jump to it.
		PageItem* it = findStoryItemInDoc(newsServerId(news));
		if (it)
		{
			selectWholeStory(it);
			const QRectF r = itemsRect(storyItems(attr(it, "news.story")));
			if (m_mw && m_mw->view && !r.isNull())
				m_mw->view->setCanvasCenterPos(r.center().x(), r.center().y());
			const int pg = m_doc->OnPage(it);
			m_statusLabel->setText(pg >= 0 ? tr("Placed - on page %1 of this document, selected").arg(pg + 1)
			                               : tr("Placed - on the pasteboard of this document, selected"));
		}
		else
			m_statusLabel->setText(tr("Placed on the server (maybe on another machine); not in this document"));
		return;
	}
	double x = m_doc->currentPage()->xOffset() + m_doc->margins()->left();
	double y = m_doc->currentPage()->yOffset() + m_doc->margins()->top() + 16.0 * kMmToPt;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Place news: %1").arg(news["title"].toString().left(40)),
		                                               QString(), Um::ICreate);
	QList<PageItem*> items = placeNews(news, x, y, false);
	Q_UNUSED(items);
	if (tx)
		tx.commit();
	markPlaced(QList<QJsonObject>() << news);
}

void SuneerNewsPanel::onPlaceSelected()
{
	if (!m_doc)
	{
		QMessageBox::warning(this, tr("Error"), tr("No document open!"));
		return;
	}
	QList<QJsonObject> stories, balance;
	int usedSkipped = 0;
	for (int i = 0; i < m_newsList->count(); i++)
	{
		if (m_newsList->item(i)->checkState() != Qt::Checked)
			continue;
		if (isPlacedRow(m_newsList->item(i)))
		{
			++usedSkipped;                 // ticked for Mark unused, never placed again
			continue;
		}
		int idx = m_newsList->item(i)->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_newsData.size())
			continue;
		if (isBalanceRow(m_newsList->item(i)))
			balance << m_newsData[idx].toObject();
		else
			stories << m_newsData[idx].toObject();
	}
	if (!balance.isEmpty())
	{
		// Asked once for the whole batch: a balance story is one kept for a later day.
		QStringList t;
		for (const QJsonObject& n : balance)
			t << QStringLiteral("\u2022 ") + collapseSpaces(n["title"].toString()).left(60);
		const int r = QMessageBox::question(this, tr("Balance story"),
			tr("%n of the ticked stories is/are BALANCE stories (kept for later):", "", balance.size()) + "\n" + t.join("\n")
			+ "\n\n" + tr("Place them now? They will be marked USED on the server."),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
		if (r == QMessageBox::Cancel)
			return;
		if (r == QMessageBox::Yes)
			stories += balance;
	}
	if (stories.isEmpty())
	{
		m_statusLabel->setText(usedSkipped > 0
			? tr("Nothing to place: the %n ticked story(ies) is/are already used (use 'Mark unused' to release)", "", usedSkipped)
			: tr("Tick the stories to place"));
		return;
	}
	m_placeUsedSkipped = usedSkipped;
	const QString target = settingsString("layout/placeTarget", "left");
	if (target == "page")
	{
		// Old behaviour: stacked on the current page, still one undo step.
		UndoTransaction tx;
		if (UndoManager::undoEnabled())
			tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
			                                               tr("Place %1 stories").arg(stories.size()), QString(), Um::ICreate);
		double baseX = m_doc->currentPage()->xOffset() + m_doc->margins()->left();
		double curY  = m_doc->currentPage()->yOffset() + m_doc->margins()->top() + 15.0 * kMmToPt;
		for (const QJsonObject& news : stories)
		{
			QList<PageItem*> items = placeNews(news, baseX, curY, false);
			const QRectF r = itemsRect(items);
			if (!r.isNull())
				curY = r.bottom() + 3.0 * kMmToPt;
		}
		if (tx)
			tx.commit();
		m_statusLabel->setText(tr("Placed %1 stories on the page").arg(stories.size()));
		markPlaced(stories);
		return;
	}
	placeOnPasteboard(stories, target == "right");
	markPlaced(stories);
}

int SuneerNewsPanel::storyColumns(const QJsonObject& news) const
{
	QSettings cfg("Faircode", "ScribusNews");
	int cols = news["noOfColumns"].toInt();
	if (cols <= 0)
		cols = cfg.value("layout/columns", 4).toInt();
	return cols <= 0 ? 4 : cols;
}

void SuneerNewsPanel::storyGeometry(int cols, double& totalW, double& colGapPt) const
{
	const double usableW = m_doc->pageWidth() - m_doc->margins()->left() - m_doc->margins()->right();
	QSettings cfgCol("Scribus", "SuneerColumnConfig");
	int numConfigs = cfgCol.beginReadArray("configs");
	double colGapMm = 4.0;
	bool useGuideGap = false;
	for (int ci = 0; ci < numConfigs; ++ci)
	{
		cfgCol.setArrayIndex(ci);
		if (cfgCol.value("columnNumber", 0).toInt() == cols)
		{
			colGapMm    = cfgCol.value("columnGutter", 4.0).toDouble();
			useGuideGap = cfgCol.value("useGuideGap", false).toBool();
			break;
		}
	}
	cfgCol.endArray();
	double guideGapPt = 0;
	int guideColCount = 0;
	if (ScPage* curPage = m_doc->currentPage())
	{
		guideColCount = curPage->guides.verticalAutoCount();
		guideGapPt    = curPage->guides.verticalAutoGap();
	}
	colGapPt = useGuideGap ? colGapMm * kMmToPt : (guideGapPt > 0 ? guideGapPt : colGapMm * kMmToPt);
	double singleColW;
	if (guideColCount > 0)
		singleColW = (usableW - colGapPt * guideColCount) / (guideColCount + 1);
	else
		singleColW = (usableW - colGapPt * (cols - 1)) / cols;
	totalW = singleColW * cols + colGapPt * (cols - 1);
}



// Story spike: the stories go on the pasteboard beside the current page,
// stacked top to bottom from the page top, a new stack further out when
// one would run below the page bottom, and never over items already lying
// there. The pasteboard is widened (undoably) when the stacks need it.
void SuneerNewsPanel::placeOnPasteboard(const QList<QJsonObject>& stories, bool rightSide)
{
	ScPage* page = m_doc->currentPage();
	if (!page)
		return;
	const double gap  = 10.0 * kMmToPt;
	const double vgap = 5.0 * kMmToPt;
	const double pageTop = page->yOffset();
	const double pageBottom = page->yOffset() + page->height();

	// The row of pages the current page sits in (facing pages).
	double rowLeft = page->xOffset(), rowRight = page->xOffset() + page->width();
	for (int i = 0; i < m_doc->Pages->count(); ++i)
	{
		ScPage* p = m_doc->Pages->at(i);
		if (qAbs(p->yOffset() - page->yOffset()) < 1.0)
		{
			rowLeft  = qMin(rowLeft, p->xOffset());
			rowRight = qMax(rowRight, p->xOffset() + p->width());
		}
	}

	// Items already lying on the pasteboard (anything not on a page).
	QList<QRectF> occupied;
	for (int i = 0; i < m_doc->Items->count(); ++i)
	{
		PageItem* it = m_doc->Items->at(i);
		if (m_doc->OnPage(it) == -1)
			occupied << it->getVisualBoundingRect();
	}

	double maxW = 0;
	for (const QJsonObject& news : stories)
	{
		double w, g;
		storyGeometry(storyColumns(news), w, g);
		maxW = qMax(maxW, w);
	}

	// A stack column is the area [edge - maxW, edge] (left side) or
	// [edge, edge + maxW] (right side) beside the page. Columns holding
	// existing pasteboard items are skipped outwards.
	auto freeEdge = [&](double edge) {
		for (int guard = 0; guard < 50; ++guard)
		{
			QRectF col = rightSide ? QRectF(edge, pageTop, maxW, page->height())
			                       : QRectF(edge - maxW, pageTop, maxW, page->height());
			bool hit = false;
			for (const QRectF& r : occupied)
			{
				if (!r.intersects(col))
					continue;
				edge = rightSide ? qMax(edge, r.right() + gap) : qMin(edge, r.left() - gap);
				hit = true;
			}
			if (!hit)
				break;
		}
		return edge;
	};

	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Place %1 stories on the pasteboard").arg(stories.size()),
		                                               QString(), Um::ICreate);

	// Lowest occupied point inside a stack column (page top when empty).
	auto columnBottom = [&](double edge) {
		QRectF col = rightSide ? QRectF(edge, pageTop, maxW, page->height())
		                       : QRectF(edge - maxW, pageTop, maxW, page->height());
		double bottom = pageTop;
		for (const QRectF& r : occupied)
			if (r.intersects(col))
				bottom = qMax(bottom, r.bottom() + vgap);
		return bottom;
	};
	// First column: the nearest one that still has room below what lies
	// there; otherwise the first free column further out.
	const double minRoom = 40.0 * kMmToPt;
	double colEdge = rightSide ? rowRight + gap : rowLeft - gap;
	double curY = pageTop;
	for (int guard = 0; guard < 50; ++guard)
	{
		const double bottom = columnBottom(colEdge);
		if (bottom <= pageTop + 0.5 || pageBottom - bottom >= minRoom)
		{
			curY = bottom;
			break;
		}
		colEdge = rightSide ? colEdge + maxW + gap : colEdge - maxW - gap;
	}

	QList<QList<PageItem*>> placedStories;
	for (const QJsonObject& news : stories)
	{
		double w, g;
		storyGeometry(storyColumns(news), w, g);
		double x = rightSide ? colEdge : colEdge - w;
		QList<PageItem*> items = placeNews(news, x, curY, false);
		if (items.isEmpty())
			continue;
		QRectF r = itemsRect(items);
		if (curY > pageTop + 1.0 && curY + r.height() > pageBottom)
		{
			// Would run below the page: start a new stack further out.
			colEdge = freeEdge(rightSide ? colEdge + maxW + gap : colEdge - maxW - gap);
			const double nx = rightSide ? colEdge : colEdge - w;
			moveStory(items, nx - r.left(), pageTop - r.top());
			r = itemsRect(items);
			curY = pageTop;
		}
		// New frames are born as items of the current page; this story lies
		// on the pasteboard, and must stay put when pages are re-laid out.
		for (PageItem* it : items)
			it->OwnPage = -1;
		placedStories << items;
		occupied << r;
		curY = r.bottom() + vgap;
	}

	// Widen the pasteboard when the stacks reach past it. With a wider left
	// scratch the pages move right; the new stacks are moved along by the
	// same amount so they stay beside the page.
	QRectF stack;
	for (const QList<PageItem*>& st : placedStories)
		stack = stack.isNull() ? itemsRect(st) : stack.united(itemsRect(st));
	const MarginStruct* sc = m_doc->scratch();
	if (!rightSide && stack.left() < 0)
	{
		const double delta = -stack.left() + gap;
		// Everything lying on the pasteboard (earlier stacks included) moves
		// with the pages, so the spike keeps its place beside the page.
		QList<PageItem*> pasteboardItems;
		for (int i = 0; i < m_doc->Items->count(); ++i)
			if (m_doc->Items->at(i)->OwnPage == -1)
				pasteboardItems << m_doc->Items->at(i);
		m_doc->setScratchUndoable(sc->left() + delta, sc->right());
		for (PageItem* it : pasteboardItems)
			m_doc->moveItem(delta, 0, it);
		stack.translate(delta, 0);
	}
	else if (rightSide && stack.right() > rowRight + sc->right())
		m_doc->setScratchUndoable(sc->left(), stack.right() - rowRight + gap);
	for (const QList<PageItem*>& st : placedStories)
		for (PageItem* it : st)
			it->OwnPage = -1;

	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	m_statusLabel->setText(tr("Placed %1 stories on the %2 pasteboard").arg(placedStories.size()).arg(rightSide ? tr("right") : tr("left")));
	showPlacedArea(stack);
}

bool SuneerNewsPanel::isNewsStory(const PageItem* item)
{
	return item && !attr(item, "news.story").isEmpty();
}

QList<PageItem*> SuneerNewsPanel::storyItems(const QString& storyId) const
{
	QList<PageItem*> out;
	if (!m_doc || storyId.isEmpty())
		return out;
	for (int i = 0; i < m_doc->Items->count(); ++i)
		if (attr(m_doc->Items->at(i), "news.story") == storyId)
			out << m_doc->Items->at(i);
	return out;
}

QRectF SuneerNewsPanel::itemsRect(const QList<PageItem*>& items)
{
	QRectF r;
	for (PageItem* it : items)
		if (it)
			r = r.isNull() ? QRectF(it->xPos(), it->yPos(), it->width(), it->height())
			               : r.united(QRectF(it->xPos(), it->yPos(), it->width(), it->height()));
	return r;
}

void SuneerNewsPanel::moveStory(const QList<PageItem*>& items, double dx, double dy)
{
	if (!m_doc || items.isEmpty() || (qAbs(dx) < 0.001 && qAbs(dy) < 0.001))
		return;
	Selection sel(this, false);
	for (PageItem* it : items)
		if (it)
			sel.addItem(it);
	m_doc->moveGroup(dx, dy, &sel);
}

QString SuneerNewsPanel::newsServerId(const QJsonObject& news)
{
	// The id the dtp-status route wants: news.id (cuid), not the refId.
	QString id = news["id"].toString().trimmed();
	if (id.isEmpty())
		id = news["refId"].toString().trimmed();
	return id;
}

QString SuneerNewsPanel::newsRefId(const QJsonObject& news)
{
	return news["refId"].toString().trimmed();
}

QString SuneerNewsPanel::newsDtpStatus(const QJsonObject& news)
{
	// /external/...: page.dtpStatus for the fetched page; layout routes: newsPages[].
	QString s;
	if (news["page"].isObject())
		s = news["page"].toObject()["dtpStatus"].toString().toUpper();
	if (s.isEmpty())
		for (const QJsonValue& v : news["newsPages"].toArray())
		{
			const QString ps = v.toObject()["dtpStatus"].toString().toUpper();
			if (ps == "USED") { s = ps; break; }
			if (ps == "BALANCED") s = ps;
		}
	return s.isEmpty() ? QStringLiteral("UNUSED") : s;
}

bool SuneerNewsPanel::newsIsUsed(const QJsonObject& news)
{
	return newsDtpStatus(news) == "USED";
}

QString SuneerNewsPanel::tagStoryItems(const QList<PageItem*>& items, const QString& title, const QString& pos, int imgCols, int cols, const QString& serverId)
{
	// Story id: headline (30 chars) made unique in this document.
	QString base = title.left(30).trimmed();
	if (base.isEmpty())
		base = tr("News story");
	QString id = base;
	for (int n = 2; !storyItems(id).isEmpty() && n < 1000; ++n)
		id = QStringLiteral("%1 (%2)").arg(base).arg(n);
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		setAttr(it, "news.story", id);
		setAttr(it, "news.photoPos", pos);
		setAttr(it, "news.photoCols", QString::number(imgCols));
		setAttr(it, "news.cols", QString::number(cols));
		if (!serverId.isEmpty())
			setAttr(it, "news.serverId", serverId);
		if (!m_fetchPageId.isEmpty())
			setAttr(it, "news.pageId", m_fetchPageId);
	}
	return id;
}

bool SuneerNewsPanel::isPlacedRow(const QListWidgetItem* item) const
{
	return item && item->data(Qt::UserRole + 2).toBool();
}

bool SuneerNewsPanel::isBalanceRow(const QListWidgetItem* item) const
{
	return item && item->data(Qt::UserRole + 4).toString() == "BALANCED";
}

PageItem* SuneerNewsPanel::findStoryItemInDoc(const QString& serverId) const
{
	if (!m_doc || serverId.isEmpty())
		return nullptr;
	for (int i = 0; i < m_doc->Items->count(); ++i)
		if (attr(m_doc->Items->at(i), "news.serverId") == serverId)
			return m_doc->Items->at(i);
	return nullptr;
}

// Marks come from the server reply (page.dtpStatus == USED), never from a
// scan of the local document: other DTP machines place from the same page
// into other files, and only the server knows.
void SuneerNewsPanel::refreshPlacedMarks()
{
	if (!m_newsList)
		return;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		QListWidgetItem* row = m_newsList->item(i);
		const int idx = row->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_newsData.size())
			continue;
		const QJsonObject news = m_newsData[idx].toObject();
		const QString sid = newsServerId(news);
		const QString status = newsDtpStatus(news);          // USED / BALANCED / UNUSED
		const bool placed = status == "USED";
		const bool pending = m_pendingMarks.contains(sid);
		const QString wasStatus = row->data(Qt::UserRole + 4).toString();
		row->setData(Qt::UserRole + 2, placed);
		row->setData(Qt::UserRole + 4, status);
		if (!wasStatus.isEmpty() && wasStatus != status)
			row->setCheckState(Qt::Unchecked);     // status flipped: a tick meant for the old state is dropped
		const QString tip = row->data(Qt::UserRole + 3).toString();
		const int imgCount = news["newsMedia"].toArray().size();
		const QJsonObject pg = news["page"].toObject();
		QString where = pg["name"].toString();
		if (where.isEmpty() && pg["displayOrder"].isDouble())
			where = tr("page %1").arg(pg["displayOrder"].toInt());
		if (!pg["updatedAt"].toString().isEmpty())
			where += (where.isEmpty() ? "" : ", ") + pg["updatedAt"].toString().left(16).replace('T', ' ');
		if (!pg["dtpUser"].toString().isEmpty())
			where += (where.isEmpty() ? "" : ", ") + pg["dtpUser"].toString();
		const QString whereTxt = where.isEmpty() ? QString() : " (" + where + ")";
		row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
		if (placed)
		{
			row->setIcon(IconManager::instance().loadIcon("ok"));
			row->setForeground(QBrush(QColor(140, 140, 140)));
			row->setToolTip(tr("USED - placed%1 (server status).\n"
			                   "Tick it for 'Mark unused' or 'Mark balance'; it cannot be placed again until then.").arg(whereTxt) + "\n" + tip);
		}
		else if (status == "BALANCED")
		{
			row->setIcon(IconManager::instance().loadIcon("panel-bookmarks"));
			row->setForeground(QBrush(QColor(150, 95, 0)));
			row->setToolTip(tr("BALANCE - kept for later%1 (server status).\n"
			                   "Tick it: 'Place Selected' asks once and then marks it used; 'Mark unused' releases it.").arg(whereTxt) + "\n" + tip);
		}
		else
		{
			row->setIcon(imgCount > 0 ? photoIcon(imgCount) : QIcon());
			row->setForeground(pending ? QBrush(QColor(180, 90, 0)) : QBrush());
			row->setToolTip((pending ? tr("Placed here but NOT marked on the server yet - press Check to retry.") : tr("FREE - not used yet (server status).")) + "\n" + tip);
		}
	}
	updateTickButtons();
}

void SuneerNewsPanel::updateTickButtons()
{
	if (!m_newsList || !m_releaseBtn || !m_placeBtn)
		return;
	int freeT = 0, usedT = 0, balT = 0;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		const QListWidgetItem* row = m_newsList->item(i);
		if (row->checkState() != Qt::Checked)
			continue;
		if (isPlacedRow(row)) ++usedT; else if (isBalanceRow(row)) ++balT; else ++freeT;
	}
	const int placeable = freeT + balT, releasable = usedT + balT, balanceable = freeT + usedT;
	m_placeBtn->setText(placeable > 0 ? tr("Place Selected (%1)").arg(placeable) : tr("Place Selected"));
	m_placeBtn->setEnabled(!m_newsData.isEmpty());
	m_releaseBtn->setText(releasable > 0 ? tr("Mark unused (%1)").arg(releasable) : tr("Mark unused"));
	m_releaseBtn->setEnabled(releasable > 0);
	if (m_balanceBtn)
	{
		m_balanceBtn->setText(balanceable > 0 ? tr("Mark balance (%1)").arg(balanceable) : tr("Mark balance"));
		m_balanceBtn->setEnabled(balanceable > 0);
	}
}

// Photo and caption wrap the body text at the wrap gap on all four sides
// (Scribus wrap offsets with bounding-box flow). Live: moving or resizing
// the frame reflows the text at once.
void SuneerNewsPanel::setWrap(PageItem* item, bool on)
{
	if (!item)
		return;
	if (!on)
	{
		item->setTextFlowMode(PageItem::TextFlowDisabled);
		return;
	}
	QSettings cfg("Faircode", "ScribusNews");
	const double gap = qMax(0.0, cfg.value("layout/wrapGapMm", 2.0).toDouble()) * kMmToPt;
	item->setWrapOffsets(gap, gap, gap, gap);
	item->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
}

// The one layout routine (placement and the right-click re-arrange). The
// story frame stays where it is; photos go to the chosen side below the
// kicker+headline block (its laid-out height is f.headH), captions under
// each photo, and the story frame is fitted to its text afterwards.
void SuneerNewsPanel::layoutStory(StoryFrames& f, double x, double y, int cols, int imgCols, const QString& pos)
{
	if (!f.text)
		return;
	cols = qMax(1, cols);
	double totalW = 0, colGap = 0;
	storyGeometry(cols, totalW, colGap);
	const double colW = (totalW - (cols - 1) * colGap) / cols;
	QSettings cfg("Faircode", "ScribusNews");
	const double defaultImgH = cfg.value("image/height", 60.0).toDouble() * kMmToPt;
	const double gap = qMax(0.0, cfg.value("layout/wrapGapMm", 2.0).toDouble()) * kMmToPt;

	const bool fullWidth = (pos == "top" || pos == "topAbove");
	const int n = fullWidth ? cols : qBound(1, imgCols, cols);
	int startCol = 0;
	if (pos == "right")
		startCol = cols - n;
	else if (pos == "middle" || pos == "bottom")
		startCol = (cols - n) / 2;
	const double pw = colW * n + (n - 1) * colGap;
	const double px = x + startCol * (colW + colGap);
	const bool wrap = (pos != "none");

	// Welds carry the offset from weld time: unweld, lay out, weld again.
	for (int i = 0; i < f.photos.size(); ++i)
	{
		if (f.photos[i] && f.photos[i]->isWelded())
			f.photos[i]->unWeld();
		if (i < f.captions.size() && f.captions[i] && f.captions[i]->isWelded())
			f.captions[i]->unWeld();
	}

	auto stackPhotos = [&](double photoY) {
		double cy = photoY;
		for (int i = 0; i < f.photos.size(); ++i)
		{
			PageItem* ph = f.photos[i];
			if (!ph)
				continue;
			double hgt = ph->height();
			if (ph->pixm.width() > 0 && ph->pixm.height() > 0)
				hgt = pw * (double) ph->pixm.height() / ph->pixm.width();
			else if (hgt <= 1.0)
				hgt = defaultImgH;
			ph->setXYPos(px, cy);
			ph->setWidth(pw);
			ph->setHeight(hgt);
			ph->updateClip();
			if (ph->pixm.width() > 0)
			{
				// Like the reference: free scaling with the ratio kept, the
				// scale chosen so the picture fills the frame width.
				ph->setImageScalingMode(false, true);
				ph->adjustPictScale();
				ph->setImageScalingMode(true, true);
				ph->setImageXYOffset(0, 0);
			}
			setWrap(ph, wrap);
			cy += hgt;
			PageItem* cap = (i < f.captions.size()) ? f.captions[i] : nullptr;
			if (cap)
			{
				cap->setXYPos(px, cy);
				cap->setWidth(pw);
				cap->updateClip();
				if (cap->itemText.length() > 0)
					fitFrame(cap);
				else
					cap->setHeight(10.0 * kMmToPt);
				setWrap(cap, wrap);
				cy = cap->yPos() + cap->height();
			}
			cy += gap;
		}
		return cy;
	};

	// Story frame geometry (position is the caller's; width/columns ours).
	f.text->setXYPos(x, y);
	f.text->setWidth(totalW);
	f.text->setColumns(cols);
	f.text->setColumnGap(colGap);
	f.text->setTextToFrameDist(0.0, 0.0, 0.0, 0.0);
	f.text->setTextFlowMode(PageItem::TextFlowDisabled);
	f.text->updateClip();

	if (pos == "none" || f.photos.isEmpty())
	{
		for (int i = 0; i < f.photos.size(); ++i)
		{
			if (f.photos[i]) setWrap(f.photos[i], false);
			if (i < f.captions.size() && f.captions[i]) setWrap(f.captions[i], false);
		}
		fitFrame(f.text);
		return;
	}

	if (pos == "topAbove")
	{
		// Photos first, the story frame below them.
		const double bottom = stackPhotos(y);
		f.text->setXYPos(x, bottom);
		fitFrame(f.text);
	}
	else if (pos == "bottom")
	{
		// Fit the text first, then the photos below it (no wrap needed).
		fitFrame(f.text);
		stackPhotos(f.text->yPos() + f.text->height() + gap);
		for (int i = 0; i < f.photos.size(); ++i)
		{
			if (f.photos[i]) setWrap(f.photos[i], false);
			if (i < f.captions.size() && f.captions[i]) setWrap(f.captions[i], false);
		}
	}
	else
	{
		// top / right / left / middle: the wrap region (photo grown by the
		// gap) starts exactly at the bottom of the kicker+headline block, so
		// the photo top is that height plus the gap and the headline is
		// never wrapped.
		f.text->setHeight(qMax(f.text->height(), 200.0 * kMmToPt));
		stackPhotos(y + f.headH + gap);
		fitFrame(f.text);
	}
	for (int i = 0; i < f.photos.size(); ++i)
		if (f.photos[i] && i < f.captions.size() && f.captions[i])
			f.photos[i]->weldTo(f.captions[i]);
}

// A working copy of the picture no larger than ~300 ppi at the placed width
// (the print standard), made once from the cached original. Big camera files
// (6000 px and more) otherwise cost decode time and memory at every reload
// and make canvas painting slow when the preview is set to full resolution.
// The original stays in the cache. Returns the path to load.
static QString suneerWorkingCopy(const QString& original, double placedWidthPt)
{
	if (original.isEmpty() || placedWidthPt <= 0)
		return original;
	QImageReader reader(original);
	const QSize sz = reader.size();
	if (!sz.isValid())
		return original;
	const int needed = qRound(placedWidthPt / 72.0 * 300.0);
	if (sz.width() <= needed * 1.15)
		return original;
	const QString copy = original + QStringLiteral(".w%1.jpg").arg(needed);
	if (QFile::exists(copy) && QFileInfo(copy).lastModified() >= QFileInfo(original).lastModified())
		return copy;
	reader.setAutoTransform(true);
	reader.setScaledSize(QSize(needed, qRound((double) sz.height() * needed / sz.width())));
	const QImage img = reader.read();
	if (img.isNull() || !img.save(copy, "JPEG", 92))
		return original;
	return copy;
}

QList<PageItem*> SuneerNewsPanel::placeNews(const QJsonObject& news, double x, double y, bool ownTransaction)
{
	QList<PageItem*> created;
	if (!m_doc)
		return created;

	const QString title    = collapseSpaces(news["title"].toString());
	const QString kicker   = collapseSpaces(news["kicker"].toString());
	const QString byline   = collapseSpaces(news["byline"].toString());
	QString dateline       = collapseSpaces(news["dateline"].toString());
	if (dateline.isEmpty())
		dateline = collapseSpaces(news["place"].toString());
	const QString highlights = newsHighlights(news);
	QStringList bodyParas  = contentToParagraphs(news["content"].toString());
	// No dateline field: a short first line of the content ("കൊച്ചി") is the place.
	if (dateline.isEmpty() && !bodyParas.isEmpty())
	{
		const QString first = bodyParas.first();
		static const QRegularExpression sentenceEnd(QStringLiteral("[.!?।]"));
		if (first.length() <= 25 && first.split(' ', Qt::SkipEmptyParts).size() <= 3 && !first.contains(sentenceEnd))
		{
			dateline = first;
			bodyParas.removeFirst();
		}
	}

	// Styles from the Style tab; the headline style from the panel combo.
	QStringList missing;
	const QString titleStyle      = styleOrEmpty(m_titleStyleCmb->currentText().trimmed(), tr("title style"), missing);
	const QString bodyStyle       = styleOrEmpty(settingsString("style/bodyStyle", "02 BodyText"), tr("body style"), missing);
	const QString firstBodyStyle  = styleOrEmpty(settingsString("style/firstBodyStyle", "03 BodyNoInd"), tr("first body style"), missing);
	const QString kickerStyle     = styleOrEmpty(settingsString("style/kickerStyle", "12 Kicker"), tr("kicker style"), missing);
	const QString bylineStyle     = styleOrEmpty(settingsString("style/bylineStyle", "07 Byline"), tr("byline style"), missing);
	const QString datelineStyle   = styleOrEmpty(settingsString("style/datelineStyle", "01 Dateline"), tr("dateline style"), missing);
	const QString highlightsStyle = settingsString("style/highlightsStyle", "05 Highlights");
	const QString captionStyle    = settingsString("style/captionStyle", "09 Caption");
	const bool haveHighlightStyle = m_doc->paragraphStyles().contains(highlightsStyle);

	const int cols = storyColumns(news);
	const int imgCols = storyImageCols(news);
	const QString pos = currentPhotoPos();
	double totalW = 0, colGapPt = 0;
	storyGeometry(cols, totalW, colGapPt);

	UndoTransaction placeTx;
	if (ownTransaction && UndoManager::undoEnabled())
		placeTx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                                    tr("Place news: %1").arg(title.left(40)),
		                                                    QString(), Um::ICreate);
	m_doc->m_Selection->clear();

	auto makeTextFrame = [&](double fx, double fy, double fw, double fh) -> PageItem* {
		int idx = m_doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, fx, fy, fw, fh, 0,
		                         CommonStrings::None, CommonStrings::None);
		PageItem* it = (idx >= 0) ? m_doc->Items->at(idx) : nullptr;
		if (it)
		{
			ParagraphStyle defStyle(it->itemText.defaultStyle());
			defStyle.setHyphenationMode(ParagraphStyle::HyphenationMode::AutomaticHyphenation);
			defStyle.charStyle().setLanguage(QStringLiteral("ml"));
			it->itemText.setDefaultStyle(defStyle);
			it->setTextToFrameDist(0.0, 0.0, 0.0, 0.0);
			created << it;
		}
		return it;
	};
	auto addPara = [&](PageItem* frame, const QString& text, const QString& styleName) {
		if (!frame || text.trimmed().isEmpty())
			return;
		StoryText& story = frame->itemText;
		int startPos = story.length();
		story.insertChars(text);
		story.insertChars(QChar(13));
		if (!styleName.isEmpty())
		{
			ParagraphStyle pStyle;
			pStyle.setParent(styleName);
			story.applyStyle(startPos, pStyle, story.length() - startPos);
		}
	};

	StoryFrames f;
	f.text = makeTextFrame(x, y, totalW, 200.0 * kMmToPt);
	if (!f.text)
	{
		if (placeTx)
			placeTx.cancel();
		m_statusLabel->setText(tr("Error: could not create the story frame"));
		return created;
	}
	f.text->setItemName(title.left(30).trimmed().isEmpty() ? tr("News story") : title.left(30).trimmed());
	f.text->setColumns(cols);
	f.text->setColumnGap(colGapPt);
	setAttr(f.text, "news.role", "text");

	// Kicker + headline block first, fitted, so its height is known.
	if (!kicker.isEmpty())
		addPara(f.text, kicker, kickerStyle);
	addPara(f.text, title, titleStyle);
	fitFrame(f.text);
	f.headH = f.text->height();
	setAttr(f.text, "news.headH", QString::number(f.headH, 'f', 3));

	if (!highlights.isEmpty())
		for (const QString& hl : highlights.split('\n'))
			addPara(f.text, hl, haveHighlightStyle ? highlightsStyle : firstBodyStyle);
	if (!byline.isEmpty())
		addPara(f.text, byline, bylineStyle);
	if (!dateline.isEmpty())
		addPara(f.text, dateline, datelineStyle);
	bool firstBody = true;
	for (const QString& para : bodyParas)
	{
		addPara(f.text, para, firstBody && !firstBodyStyle.isEmpty() ? firstBodyStyle : bodyStyle);
		firstBody = false;
	}
	m_doc->docHyphenator->slotHyphenate(f.text);

	// Photos: separate frames, each with a caption frame (caption_<image>).
	const QJsonArray media = news["newsMedia"].toArray();
	if (pos != "none" && !media.isEmpty())
	{
		QStringList failed;
		for (int mi = 0; mi < media.size(); ++mi)
		{
			const QJsonObject m = media[mi].toObject();
			const QJsonObject md = m["media"].toObject();
			const QString fileUrl = md["fileUrl"].toString();
			QString capText = collapseSpaces(m["caption"].toString());
			if (capText.isEmpty())
				capText = collapseSpaces(md["caption"].toString());
			if (capText.isEmpty() && mi == 0)
				capText = collapseSpaces(news["caption"].toString());

			m_statusLabel->setText(tr("Downloading picture %1 of %2...").arg(mi + 1).arg(media.size()));
			QString why;
			QString localImg = downloadImage(fileUrl, &why);
			if (localImg.isEmpty())
				failed << QStringLiteral("%1: %2").arg(QFileInfo(fileUrl).fileName(), why);
			else
			{
				const int n = (pos == "top" || pos == "topAbove") ? cols : qBound(1, imgCols, cols);
				const double colW = (totalW - (cols - 1) * colGapPt) / cols;
				localImg = suneerWorkingCopy(localImg, colW * n + (n - 1) * colGapPt);
			}

			int ifIdx = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle,
			                           x, y, 50.0 * kMmToPt, 50.0 * kMmToPt, 0, CommonStrings::None, CommonStrings::None);
			PageItem* imgFrame = (ifIdx >= 0) ? m_doc->Items->at(ifIdx) : nullptr;
			if (!imgFrame)
				continue;
			created << imgFrame;
			setAttr(imgFrame, "news.role", "photo");
			setAttr(imgFrame, "news.index", QString::number(mi));
			imgFrame->setFillColor(CommonStrings::None);
			if (!localImg.isEmpty())
			{
				m_doc->loadPict(localImg, imgFrame, false, true);
				// Screen preview at normal (72 dpi) resolution for this frame,
				// whatever the document default; export still uses the full data.
				if (imgFrame->pixm.imgInfo.lowResType == 0)
					imgFrame->setResolution(1);
				SuneerImageLinks::embedPlaced(m_doc, imgFrame);   // "always embed" pref
				imgFrame->setLineColor(CommonStrings::None);
			}
			else
			{
				imgFrame->setLineColor("Black");
				imgFrame->setLineShade(40);
				imgFrame->setLineWidth(0.5);
			}
			f.photos << imgFrame;

			PageItem* captFrame = makeTextFrame(x, y, 50.0 * kMmToPt, 10.0 * kMmToPt);
			if (captFrame)
			{
				captFrame->setItemName(QStringLiteral("caption_%1").arg(imgFrame->itemName()));
				setAttr(captFrame, "news.role", "caption");
				setAttr(captFrame, "news.index", QString::number(mi));
				captFrame->setLineColor(CommonStrings::None);
				captFrame->setFillColor(CommonStrings::None);
				if (!capText.isEmpty())
					captFrame->itemText.insertChars(capText);
				if (m_doc->paragraphStyles().contains(captionStyle))
				{
					ParagraphStyle cs;
					cs.setParent(captionStyle);
					captFrame->itemText.applyStyle(0, cs, qMax(1, captFrame->itemText.length()));
				}
				else if (!missing.contains(QStringLiteral("%1 \"%2\"").arg(tr("caption style"), captionStyle)))
					missing << QStringLiteral("%1 \"%2\"").arg(tr("caption style"), captionStyle);
			}
			f.captions << captFrame;
		}
		if (!failed.isEmpty())
			missing << tr("photo not downloaded (%1)").arg(failed.join("; "));
	}

	layoutStory(f, x, y, cols, imgCols, pos);
	tagStoryItems(created, title, pos, imgCols, cols, newsServerId(news));
	for (PageItem* it : created)
		if (it && !newsRefId(news).isEmpty())
			setAttr(it, "news.refId", newsRefId(news));

	if (placeTx)
		placeTx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);

	if (!missing.isEmpty())
		m_statusLabel->setText(tr("Warning: placed, but: %1").arg(missing.join("; ")));
	else
		m_statusLabel->setText(tr("Placed: %1").arg(title.left(60)));
	return created;
}

void SuneerNewsPanel::selectWholeStory(PageItem* item)
{
	if (!m_doc || !isNewsStory(item))
		return;
	const QList<PageItem*> items = storyItems(attr(item, "news.story"));
	m_doc->m_Selection->clear();
	for (PageItem* it : items)
		m_doc->m_Selection->addItem(it);
	if (m_mw)
		m_mw->HaveNewSel();
	m_doc->regionsChanged()->update(QRectF());
}

// Right-click on a placed story: re-arrange its photos in place with the
// one layout routine. One undo step.
void SuneerNewsPanel::rearrangeStory(PageItem* item, const QString& posIn, int imgColsIn)
{
	if (!m_doc || !isNewsStory(item))
		return;
	const QString id = attr(item, "news.story");
	const QList<PageItem*> items = storyItems(id);
	QString pos = posIn.isEmpty() ? attr(item, "news.photoPos") : posIn;
	if (pos.isEmpty())
		pos = "right";
	int imgCols = imgColsIn > 0 ? imgColsIn : attr(item, "news.photoCols").toInt();
	if (imgCols <= 0)
		imgCols = 2;
	int cols = attr(item, "news.cols").toInt();
	if (cols <= 0)
		cols = 4;

	StoryFrames f;
	QMap<int, PageItem*> photos, captions;
	for (PageItem* it : items)
	{
		const QString role = attr(it, "news.role");
		if (role == "text") f.text = it;
		else if (role == "photo") photos.insert(attr(it, "news.index").toInt(), it);
		else if (role == "caption") captions.insert(attr(it, "news.index").toInt(), it);
	}
	for (auto it = photos.begin(); it != photos.end(); ++it)
	{
		f.photos << it.value();
		f.captions << captions.value(it.key(), nullptr);
	}
	if (!f.text)
	{
		m_statusLabel->setText(tr("Error: this story has no story frame to re-arrange"));
		return;
	}
	f.headH = attr(f.text, "news.headH").toDouble();
	const bool pasteboard = (f.text->OwnPage == -1);
	const QRectF before = itemsRect(items);

	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Photo position: %1").arg(id), QString(), Um::IGroup);
	layoutStory(f, f.text->xPos(), f.text->yPos(), cols, imgCols, pos);
	for (PageItem* it : items)
	{
		setAttr(it, "news.photoPos", pos);
		setAttr(it, "news.photoCols", QString::number(imgCols));
		if (pasteboard)
			it->OwnPage = -1;
	}
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(before.united(itemsRect(items)));
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	m_statusLabel->setText(tr("Photo %1, %2 columns: %3").arg(photoPositionLabel(pos)).arg(imgCols).arg(id));
}

// Scroll and zoom so the stack and the current page are both in view.
void SuneerNewsPanel::showPlacedArea(const QRectF& stackRect)
{
	if (!m_mw || !m_mw->view || !m_doc || stackRect.isNull())
		return;
	ScPage* page = m_doc->currentPage();
	QRectF area = stackRect.united(QRectF(page->xOffset(), page->yOffset(), page->width(), page->height()));
	area.adjust(-20, -20, 20, 20);
	ScribusView* view = m_mw->view;
	const QSize vp = view->viewport()->size();
	double scale = qMin(vp.width() / area.width(), vp.height() / area.height());
	scale = qBound(0.05, scale, view->scale());   // never zoom in, only out as needed
	view->zoom(area.center().x(), area.center().y(), scale, false);
	view->setCanvasCenterPos(area.center().x(), area.center().y());
}

QString SuneerNewsPanel::styleOrEmpty(const QString& name, const QString& what, QStringList& missing) const
{
	if (name.trimmed().isEmpty())
		return QString();
	if (m_doc && m_doc->paragraphStyles().contains(name))
		return name;
	missing << QStringLiteral("%1 \"%2\"").arg(what, name);
	return QString();
}

QStringList SuneerNewsPanel::photoPositionKeys()
{
	return { "top", "topAbove", "right", "left", "middle", "bottom", "none" };
}

QString SuneerNewsPanel::photoPositionLabel(const QString& key)
{
	if (key == "top")      return tr("Top (full width, below headline)");
	if (key == "topAbove") return tr("Top (above headline)");
	if (key == "right")    return tr("Right");
	if (key == "left")     return tr("Left");
	if (key == "middle")   return tr("Middle");
	if (key == "bottom")   return tr("Bottom");
	if (key == "none")     return tr("None (text only)");
	return key;
}

QString SuneerNewsPanel::currentPhotoPos() const
{
	if (m_photoPosCmb && m_photoPosCmb->currentIndex() >= 0)
		return m_photoPosCmb->currentData().toString();
	return settingsString("layout/imagePosition", "right");
}

int SuneerNewsPanel::storyImageCols(const QJsonObject& news) const
{
	QSettings cfg("Faircode", "ScribusNews");
	int imgCols = news["imageColumns"].toInt();
	if (imgCols <= 0)
		imgCols = cfg.value("layout/imageColumns", 2).toInt();
	return qMax(1, imgCols);
}

void SuneerNewsPanel::setAttr(PageItem* item, const QString& name, const QString& value)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (ObjectAttribute& a : attrs)
		if (a.name == name)
		{
			a.value = value;
			item->setObjectAttributes(&attrs);
			return;
		}
	ObjectAttribute a;
	a.name = name;
	a.type = "string";
	a.value = value;
	attrs.append(a);
	item->setObjectAttributes(&attrs);
}

QString SuneerNewsPanel::attr(const PageItem* item, const QString& name)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> found = item->getObjectAttributes(name);
	return found.isEmpty() ? QString() : found.first().value;
}



void SuneerNewsPanel::fitFrame(PageItem* frame)
{
	if (!frame || !m_mw)
		return;
	frame->invalidateLayout();
	frame->layout();
	m_doc->m_Selection->clear();
	m_doc->m_Selection->addItem(frame);
	m_mw->suneerAutoFitHeight();
	m_doc->m_Selection->clear();
}

// Text wrap with a gap: the contour line is the frame rectangle grown by
// the gap, so the body text keeps its distance from photo and caption.

// The one layout routine, used by placement and by "Photo position" /
// "Photo columns" on a placed story. Headline frame first (full width),
// then photos and body according to the position; the headline is never
// inside a wrap area.


// Right-click on a placed story: ungroup, re-arrange with the one layout
// routine, group again. One undo step.

// Cache in TempLocation/scribus_news/<hash>-<basename>. A server-side change
// is detected with ETag / Last-Modified (304 keeps the cached copy). Empty
// files are never kept; an empty return means "no picture".
QString SuneerNewsPanel::downloadImage(const QString& fileUrl, QString* why)
{
	auto fail = [&](const QString& reason) {
		if (why)
			*why = reason;
		qDebug() << "news image:" << reason;
		return QString();
	};
	if (fileUrl.trimmed().isEmpty())
		return fail(tr("no file URL"));
	// Per-user cache (~/.cache/scribus/news). A shared /tmp folder created by
	// another user was not writable, which lost every photo silently.
	const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/news/";
	if (!QDir().mkpath(cacheDir) || !QFileInfo(cacheDir).isWritable())
		return fail(tr("cache folder not writable: %1").arg(cacheDir));

	QString base = QFileInfo(QUrl(fileUrl).path()).fileName();
	static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]"));
	base.replace(unsafe, "_");
	if (base.isEmpty() || base == "." || base == "..")
		base = QStringLiteral("image.jpg");
	const QString hash = QString::fromLatin1(QCryptographicHash::hash(fileUrl.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
	const QString localPath = cacheDir + hash + "-" + base;
	const QString metaPath  = localPath + ".meta";

	QString fullUrl = fileUrl;
	if (!fileUrl.startsWith("http://") && !fileUrl.startsWith("https://"))
		fullUrl = m_filesBase + (fileUrl.startsWith('/') ? fileUrl : "/" + fileUrl);

	// Through the one request builder; the Bearer header goes along (the
	// file server accepts it today and may require it later).
	QNetworkRequest req = apiRequest(QUrl(fullUrl), true);
	req.setRawHeader("Accept", "image/*");
	req.setTransferTimeout(20000);
	const bool haveCached = QFile::exists(localPath) && QFileInfo(localPath).size() > 0;
	if (haveCached)
	{
		QSettings meta(metaPath, QSettings::IniFormat);
		const QString etag = meta.value("etag").toString();
		const QString lastMod = meta.value("lastModified").toString();
		if (!etag.isEmpty())
			req.setRawHeader("If-None-Match", etag.toUtf8());
		if (!lastMod.isEmpty())
			req.setRawHeader("If-Modified-Since", lastMod.toUtf8());
	}

	QNetworkReply* reply = m_nam->get(req);
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QTimer::singleShot(25000, reply, &QNetworkReply::abort);   // really aborts, ends the loop
	loop.exec();
	reply->deleteLater();

	const int status = httpStatus(reply);
	qDebug() << "news image:" << QUrl(fullUrl).path() << "HTTP" << status
	         << (reply->error() == QNetworkReply::NoError ? "" : reply->errorString());
	if (status == 304 && haveCached)
		return localPath;
	if (reply->error() != QNetworkReply::NoError || status != 200)
	{
		if (haveCached)
			return localPath;
		return fail(status > 0 ? tr("HTTP %1 %2").arg(status).arg(serverMessage(reply->peek(reply->bytesAvailable())))
		                       : describeError(reply));
	}
	const QByteArray bytes = reply->readAll();
	if (bytes.isEmpty())
		return haveCached ? localPath : fail(tr("empty reply"));
	QFile f(localPath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return fail(tr("cannot write %1: %2").arg(localPath, f.errorString()));
	f.write(bytes);
	f.close();
	QSettings meta(metaPath, QSettings::IniFormat);
	meta.setValue("etag", QString::fromUtf8(reply->rawHeader("ETag")));
	meta.setValue("lastModified", QString::fromUtf8(reply->rawHeader("Last-Modified")));
	meta.setValue("url", fullUrl);
	return localPath;
}

// ============================================================================
// Settings dialog
// ============================================================================

void SuneerNewsPanel::onSettings()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Faircode News - Configuration"));
	dlg.setMinimumWidth(520);
	dlg.setMinimumHeight(400);
	QVBoxLayout* mainVl = new QVBoxLayout(&dlg);
	QSettings s("Faircode", "ScribusNews");
	QTabWidget* tabs = new QTabWidget(&dlg);
	mainVl->addWidget(tabs);

	// Connection
	QWidget* connTab = new QWidget();
	QFormLayout* fl = new QFormLayout(connTab);
	fl->setSpacing(10);
	QLineEdit* hostEdit = new QLineEdit(m_apiHost, connTab);
	hostEdit->setPlaceholderText("http://IP:3011");
	fl->addRow(tr("API server:"), hostEdit);
	QLineEdit* filesEdit = new QLineEdit(m_filesBase, connTab);
	filesEdit->setPlaceholderText("http://IP/files");
	fl->addRow(tr("Files base:"), filesEdit);
	QPushButton* deriveBtn = new QPushButton(tr("Derive files base from server"), connTab);
	connect(deriveBtn, &QPushButton::clicked, connTab, [hostEdit, filesEdit]() {
		filesEdit->setText(defaultFilesBaseFor(hostEdit->text().trimmed()));
	});
	fl->addRow("", deriveBtn);
	QLineEdit* shortNamesEdit = new QLineEdit(m_editionShortNames, connTab);
	shortNamesEdit->setToolTip(tr("Used only when the edition list cannot be loaded from the server"));
	fl->addRow(tr("Edition short names (fallback):"), shortNamesEdit);
	QLabel* note = new QLabel(tr("Login is per user (Logout in the panel). The password is never stored; "
	                             "the session tokens are kept in the system keychain."), connTab);
	note->setWordWrap(true);
	note->setStyleSheet("color:#666; font-size:11px;");
	fl->addRow(note);
	tabs->addTab(connTab, tr("🔗 Connection"));

	// Layout
	QWidget* layoutTab = new QWidget();
	QFormLayout* fl2 = new QFormLayout(layoutTab);
	fl2->setSpacing(10);
	QSpinBox* colsSpin = new QSpinBox(layoutTab);
	colsSpin->setRange(1, 8);
	colsSpin->setValue(s.value("layout/columns", 4).toInt());
	fl2->addRow(tr("Columns when the story has none:"), colsSpin);
	QSpinBox* imgColsSpin = new QSpinBox(layoutTab);
	imgColsSpin->setRange(1, 4);
	imgColsSpin->setValue(s.value("layout/imageColumns", 2).toInt());
	fl2->addRow(tr("Image columns when the story has none:"), imgColsSpin);
	QComboBox* posCmb = new QComboBox(layoutTab);
	for (const QString& key : photoPositionKeys())
		posCmb->addItem(photoPositionLabel(key), key);
	{
		int pi = posCmb->findData(settingsString("layout/imagePosition", "right"));
		posCmb->setCurrentIndex(pi >= 0 ? pi : 0);
	}
	fl2->addRow(tr("Default photo position:"), posCmb);
	QDoubleSpinBox* wrapGapSpin = new QDoubleSpinBox(layoutTab);
	wrapGapSpin->setRange(0.0, 10.0);
	wrapGapSpin->setSuffix(" mm");
	wrapGapSpin->setValue(s.value("layout/wrapGapMm", 2.0).toDouble());
	fl2->addRow(tr("Text wrap gap around photos:"), wrapGapSpin);
	QComboBox* targetCmb = new QComboBox(layoutTab);
	targetCmb->addItem(tr("Pasteboard left of the page (story spike)"), "left");
	targetCmb->addItem(tr("Pasteboard right of the page"), "right");
	targetCmb->addItem(tr("Current page (old behaviour)"), "page");
	{
		int ti = targetCmb->findData(settingsString("layout/placeTarget", "left"));
		targetCmb->setCurrentIndex(ti >= 0 ? ti : 0);
	}
	fl2->addRow(tr("Place stories on:"), targetCmb);
	tabs->addTab(layoutTab, tr("📐 Layout"));

	// Image
	QWidget* imgTab = new QWidget();
	QFormLayout* fl3 = new QFormLayout(imgTab);
	fl3->setSpacing(10);
	QDoubleSpinBox* imgHspin = new QDoubleSpinBox(imgTab);
	imgHspin->setRange(10, 500);
	imgHspin->setValue(s.value("image/height", 60.0).toDouble());
	imgHspin->setSuffix(" mm");
	fl3->addRow(tr("Initial image height:"), imgHspin);
	tabs->addTab(imgTab, tr("🖼 Image"));

	// Styles
	QWidget* styleTab = new QWidget();
	QFormLayout* fl4 = new QFormLayout(styleTab);
	fl4->setSpacing(10);
	auto styleEdit = [&](const QString& key, const QString& def) {
		return new QLineEdit(settingsString(key, def), styleTab);
	};
	QLineEdit* titleStyleEdit     = styleEdit("style/titleStyle", "44 M");
	QLineEdit* kickerStyleEdit    = styleEdit("style/kickerStyle", "12 Kicker");
	QLineEdit* highlightStyleEdit = styleEdit("style/highlightsStyle", "05 Highlights");
	QLineEdit* bylineStyleEdit    = styleEdit("style/bylineStyle", "07 Byline");
	QLineEdit* firstBodyStyleEdit = styleEdit("style/firstBodyStyle", "03 BodyNoInd");
	QLineEdit* bodyStyleEdit      = styleEdit("style/bodyStyle", "02 BodyText");
	QLineEdit* captionStyleEdit   = styleEdit("style/captionStyle", "09 Caption");
	QLineEdit* datelineStyleEdit  = styleEdit("style/datelineStyle", "01 Dateline");
	fl4->addRow(tr("Default title style:"), titleStyleEdit);
	fl4->addRow(tr("Kicker style:"), kickerStyleEdit);
	fl4->addRow(tr("Highlights style:"), highlightStyleEdit);
	fl4->addRow(tr("Byline style:"), bylineStyleEdit);
	fl4->addRow(tr("First body paragraph style:"), firstBodyStyleEdit);
	fl4->addRow(tr("Body style:"), bodyStyleEdit);
	fl4->addRow(tr("Caption style:"), captionStyleEdit);
	fl4->addRow(tr("Dateline style:"), datelineStyleEdit);
	tabs->addTab(styleTab, tr("🎨 Style"));

	QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	mainVl->addWidget(bb);

	if (dlg.exec() != QDialog::Accepted)
		return;

	QString host = hostEdit->text().trimmed();
	while (host.endsWith('/'))
		host.chop(1);
	if (!host.startsWith("http://") && !host.startsWith("https://"))
		host.prepend("http://");
	QString files = filesEdit->text().trimmed();
	while (files.endsWith('/'))
		files.chop(1);
	if (files.isEmpty())
		files = defaultFilesBaseFor(host);
	const bool hostChanged = (host != m_apiHost);
	m_apiHost   = host;
	m_filesBase = files;
	m_editionShortNames = shortNamesEdit->text().trimmed();
	if (m_editionShortNames.isEmpty())
		m_editionShortNames = kDefaultShortNames;
	saveConfig();
	s.setValue("layout/columns", colsSpin->value());
	s.setValue("layout/imageColumns", imgColsSpin->value());
	s.setValue("layout/placeTarget", targetCmb->currentData().toString());
	s.setValue("layout/imagePosition", posCmb->currentData().toString());
	s.setValue("layout/wrapGapMm", wrapGapSpin->value());
	s.setValue("image/height", imgHspin->value());
	s.setValue("style/titleStyle", titleStyleEdit->text().trimmed());
	s.setValue("style/kickerStyle", kickerStyleEdit->text().trimmed());
	s.setValue("style/highlightsStyle", highlightStyleEdit->text().trimmed());
	s.setValue("style/bylineStyle", bylineStyleEdit->text().trimmed());
	s.setValue("style/firstBodyStyle", firstBodyStyleEdit->text().trimmed());
	s.setValue("style/bodyStyle", bodyStyleEdit->text().trimmed());
	s.setValue("style/captionStyle", captionStyleEdit->text().trimmed());
	s.setValue("style/datelineStyle", datelineStyleEdit->text().trimmed());
	m_statusLabel->setText(tr("Settings saved"));
	if (hostChanged)
	{
		// A session from one server means nothing to another.
		if (isLoggedIn())
			onLogout();
		return;
	}
	onRefresh();
}
