#include <algorithm>
#include "suneer_news_panel.h"
#include "suneer_obit_model.h"

#include "commonstrings.h"
#include "hyphenator.h"
#include "iconmanager.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribuscore.h"
#include "sccolor.h"
#include "scupdatekeystore.h"
#include "suneerboxfit.h"
#include "selection.h"
#include "scribusview.h"
#include "pageitem_group.h"
#include "pagestructs.h"
#include "fpointarray.h"
#include "suneerimagelinks.h"
#include "suneerphotobadges.h"
#include "ui/suneertextgradient.h"
#include "ui/faircodehelpviewer.h"
#include "undomanager.h"
#include "undogui.h"
#include "undotransaction.h"
#include "undostate.h"

#include <QDateTime>
#include <QCheckBox>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QToolTip>
#include <QHelpEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <memory>

#include <QDir>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QImageReader>
#include <QBuffer>
#include <QProcess>
#include <QImageWriter>
#include <QFontMetricsF>
#include <QTemporaryFile>
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
#include "suneerphotoedit.h"
#include "suneerphotostore.h"
#include "suneerspacing.h"
#include "suneertextclean.h"
#include "suneerweld.h"
#include "suneerlayoutmodel.h"
#include "suneerlayoutengine.h"
#include "netpathguard.h"
#include "scribuswin.h"
#include "third_party/zip/scribus_zip.h"
#include <QFileDialog>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTime>
#include <QSysInfo>
#include <QDesktopServices>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QBrush>
#include <QApplication>
#include <QNetworkCookie>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSlider>
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

// One row of the news list: [checkbox][icon][title ......][ TAG ]. The tag
// (USD / UNUSD / BAL from the status role) sits in a fixed strip at the right
// end and is always complete; only the title is elided, by grapheme clusters.
// The item's text stays the plain title, so search, sorting and tooltips are
// untouched. Nothing is painted outside the row rectangle.
// suneer: a push button / check box that never forces the panel wider than the
// Styles tab: its minimum width is the icon plus a few characters, the label is
// elided with "…" when the dock is narrow, and the tooltip always starts with
// the full label. Every function and shortcut stays as it was.
class SuneerElideButton : public QPushButton
{
public:
	// Preferred = may shrink: a layout only honours minimumSizeHint() for a policy with the shrink flag
	SuneerElideButton(const QString& text, QWidget* parent = nullptr) : QPushButton(text, parent) { setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
	SuneerElideButton(const QIcon& icon, const QString& text, QWidget* parent = nullptr) : QPushButton(icon, text, parent) { setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
	QSize minimumSizeHint() const override
	{
		const QSize s = QPushButton::sizeHint();   // (the base minimumSizeHint() calls the virtual sizeHint())
		const int chars = fontMetrics().horizontalAdvance(QStringLiteral("MMM…"));
		return QSize(qMin(s.width(), chars + (icon().isNull() ? 0 : iconSize().width() + 4) + 16), s.height());
	}
protected:
	void paintEvent(QPaintEvent*) override
	{
		QStylePainter p(this);
		QStyleOptionButton opt;
		initStyleOption(&opt);
		const int iconW = opt.icon.isNull() ? 0 : opt.iconSize.width() + 4;
		const int avail = opt.rect.width() - iconW - 16;
		if (fontMetrics().horizontalAdvance(opt.text) > avail)
			opt.text = SuneerNewsPanel::elideGraphemes(opt.text, fontMetrics(), qMax(0, avail));
		p.drawControl(QStyle::CE_PushButton, opt);
	}
	bool event(QEvent* e) override
	{
		if (e->type() == QEvent::ToolTip)
		{
			const QString tip = toolTip();
			QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(),
			                   QStringLiteral("<b>%1</b>%2").arg(text().toHtmlEscaped(), tip.isEmpty() ? QString() : QStringLiteral("<br>") + tip.toHtmlEscaped()), this);
			return true;
		}
		return QPushButton::event(e);
	}
};

class SuneerElideCheck : public QCheckBox
{
public:
	SuneerElideCheck(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) { setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
	QSize minimumSizeHint() const override
	{
		const QSize s = QCheckBox::sizeHint();   // (the base minimumSizeHint() calls the virtual sizeHint())
		return QSize(qMin(s.width(), fontMetrics().horizontalAdvance(QStringLiteral("MMM…")) + 24), s.height());
	}
protected:
	void paintEvent(QPaintEvent*) override
	{
		QStylePainter p(this);
		QStyleOptionButton opt;
		initStyleOption(&opt);
		const int avail = opt.rect.width() - 24;
		if (fontMetrics().horizontalAdvance(opt.text) > avail)
			opt.text = SuneerNewsPanel::elideGraphemes(opt.text, fontMetrics(), qMax(0, avail));
		p.drawControl(QStyle::CE_CheckBox, opt);
	}
	bool event(QEvent* e) override
	{
		if (e->type() == QEvent::ToolTip)
		{
			const QString tip = toolTip();
			QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(),
			                   QStringLiteral("<b>%1</b>%2").arg(text().toHtmlEscaped(), tip.isEmpty() ? QString() : QStringLiteral("<br>") + tip.toHtmlEscaped()), this);
			return true;
		}
		return QCheckBox::event(e);
	}
};

// suneer: a combo box keeps the dock narrow: its minimum is a few characters,
// never its widest entry (the drop-down list still shows the full names).
static void narrowCombo(QComboBox* cb, int chars = 6)
{
	cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	cb->setMinimumContentsLength(chars);
	cb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

class SuneerNewsRowDelegate : public QStyledItemDelegate
{
public:
	explicit SuneerNewsRowDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

	QFont metaFont(const QFont& base) const
	{
		QFont f(base);
		f.setPointSizeF(qMax(6.0, base.pointSizeF() * 0.78));
		return f;
	}

	static QString tagFor(const QString& status, QColor& color, bool dark)
	{
		if (status == "USED")     { color = dark ? QColor("#EF5350") : QColor("#D32F2F"); return QStringLiteral("USD"); }
		if (status == "BALANCED") { color = dark ? QColor("#FFA726") : QColor("#E65100"); return QStringLiteral("BAL"); }
		color = dark ? QColor("#66BB6A") : QColor("#2E7D32");
		return QStringLiteral("UNUSD");
	}

	QFont tagFont(const QFont& base) const
	{
		QFont f(base);
		f.setBold(true);
		f.setPointSizeF(qMax(6.0, base.pointSizeF() * 0.78));
		return f;
	}

	int tagStripWidth(const QFont& base) const
	{
		// Widest tag + padding, fixed for every row so the tags line up.
		return QFontMetrics(tagFont(base)).horizontalAdvance(QStringLiteral("UNUSD")) + 14;
	}

	// The row never asks for more width than the view has: the title is
	// elided to the space left, so there is no horizontal scrolling and the
	// tag strip always lies inside the viewport.
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QSize s = QStyledItemDelegate::sizeHint(option, index);
		s.setWidth(1);
		// second line: date, words, photos, reporter (UserRole + 5)
		if (!index.data(Qt::UserRole + 5).toString().isEmpty())
			s.setHeight(s.height() + QFontMetrics(metaFont(option.font)).height() + 2);
		return s;
	}

	void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt(option);
		initStyleOption(&opt, index);
		int strip = tagStripWidth(opt.font);
		// the desk's column count ("3 col") as a small badge left of the status tag
		const QString colBadge = index.data(Qt::UserRole + 6).toString();
		const int badgeW = colBadge.isEmpty() ? 0 : QFontMetrics(tagFont(opt.font)).horizontalAdvance(colBadge) + 10;
		strip += badgeW ? badgeW + 4 : 0;
		// Title area: the row minus the tag strip. Elide the title to it.
		QStyleOptionViewItem textOpt(opt);
		textOpt.rect.setRight(opt.rect.right() - strip);
		textOpt.textElideMode = Qt::ElideNone;
		const QString meta = index.data(Qt::UserRole + 5).toString();
		const int metaH = meta.isEmpty() ? 0 : QFontMetrics(metaFont(opt.font)).height() + 2;
		textOpt.rect.setBottom(opt.rect.bottom() - metaH);   // the headline on the first line only
		const QString full = index.data(Qt::UserRole + 1).toString().isEmpty() ? opt.text : index.data(Qt::UserRole + 1).toString();
		QStyle* st = opt.widget ? opt.widget->style() : QApplication::style();
		const QRect textRect = st->subElementRect(QStyle::SE_ItemViewItemText, &textOpt, opt.widget);
		textOpt.text = SuneerNewsPanel::elideGraphemes(full, QFontMetrics(opt.font), qMax(0, textRect.width() - 4));
		p->save();
		p->setClipRect(opt.rect);
		// Selection/hover background across the whole row, then the row content.
		QStyleOptionViewItem bgOpt(opt);
		bgOpt.text.clear();
		bgOpt.icon = QIcon();
		bgOpt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
		st->drawPrimitive(QStyle::PE_PanelItemViewItem, &bgOpt, p, opt.widget);
		textOpt.state &= ~QStyle::State_Selected;   // background already drawn
		textOpt.backgroundBrush = Qt::NoBrush;
		st->drawControl(QStyle::CE_ItemViewItem, &textOpt, p, opt.widget);
		// The tag, right-aligned in its strip.
		const QString status = index.data(Qt::UserRole + 4).toString();
		const bool dark = opt.palette.color(QPalette::Base).lightness() < 128;
		QColor col;
		const QString tag = tagFor(status, col, dark);
		p->setFont(tagFont(opt.font));
		p->setPen(col);
		QRect tagRect(opt.rect.right() - strip, opt.rect.top(), strip - 6, opt.rect.height() - metaH);
		p->drawText(tagRect, Qt::AlignRight | Qt::AlignVCenter, tag);
		if (badgeW > 0)
		{
			const int tagW = QFontMetrics(tagFont(opt.font)).horizontalAdvance(tag);
			const int bh = QFontMetrics(tagFont(opt.font)).height() + 2;
			QRect badge(opt.rect.right() - 6 - tagW - 4 - badgeW, opt.rect.top() + (opt.rect.height() - metaH - bh) / 2, badgeW, bh);
			p->setPen(Qt::NoPen);
			p->setBrush(dark ? QColor("#1E88E5") : QColor("#1565C0"));
			p->drawRoundedRect(badge, 3, 3);
			p->setPen(Qt::white);
			p->drawText(badge, Qt::AlignCenter, colBadge);
		}
		if (metaH > 0)
		{
			// second line, under the headline, aligned with its text; grey, elided
			const QFontMetrics mfm(metaFont(opt.font));
			p->setFont(metaFont(opt.font));
			p->setPen(opt.state & QStyle::State_Selected ? opt.palette.color(QPalette::Text) : (dark ? QColor("#9E9E9E") : QColor("#757575")));
			QRect metaRect(textRect.left(), opt.rect.bottom() - metaH + 1, opt.rect.right() - 6 - textRect.left(), metaH - 1);
			p->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter, SuneerNewsPanel::elideGraphemes(meta, mfm, metaRect.width()));
		}
		p->restore();
	}
};


// Page lock (see the "Page lock" section below).
namespace {
const int kLockTimeoutMs = 5000;
const int kPageNameRole = Qt::UserRole + 1;

// QComboBox that says when its list is about to open (lock status refresh).
class SuneerPopupCombo : public QComboBox
{
public:
	using QComboBox::QComboBox;
	std::function<void()> onPopup;
	void showPopup() override
	{
		if (onPopup)
			onPopup();
		QComboBox::showPopup();
	}
};
}

namespace {

const double  kMmToPt        = 2.8346;
const QString kSecretService = QStringLiteral("org.scribus.news");
const QString kKeyAccess     = QStringLiteral("accessToken");
const QString kKeyRefresh    = QStringLiteral("refreshToken");
const QString kKeyDevice     = QStringLiteral("deviceId");
const QString kKeyUserName   = QStringLiteral("userName");
// Short names seen on the server on 2026-10-07; used only when the public
// edition list cannot be loaded.
const QString kDefaultApiHost = QStringLiteral("http://10.199.100.133:3011");
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

	// Outer widget: a header that is ALWAYS shown (title, server, help,
	// reload, settings) above the locked/unlocked stack. The gear used to live
	// on the logged-in page only, so a wrong server address — which makes the
	// login fail — left no way to open the settings and fix it.
	QWidget* outer = new QWidget(this);
	QVBoxLayout* outerVl = new QVBoxLayout(outer);
	outerVl->setSpacing(2);
	outerVl->setContentsMargins(4, 4, 4, 0);
	IconManager& im = IconManager::instance();
	{
		QHBoxLayout* topBar = new QHBoxLayout();
		topBar->setSpacing(4);
		QLabel* title = new QLabel("<b>Faircode News Browser</b>", outer);
		title->setStyleSheet("color:#1565C0;");
		// The title may be clipped when the docker is narrow; the buttons are
		// fixed-size and never are.
		title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		title->setMinimumWidth(40);
		QToolButton* helpBtn = FaircodeHelpViewer::makeHelpButton(outer, tr("How to use the News Browser"));
		connect(helpBtn, &QToolButton::clicked, outer, [outer]() {
			FaircodeHelpViewer::showTopic(outer, QStringLiteral("news-browser"));
		});
		m_refreshBtn = new QPushButton(im.loadIcon("reload"), QString(), outer);
		m_refreshBtn->setFixedWidth(30);
		m_refreshBtn->setToolTip(tr("Reload editions and pages"));
		connect(m_refreshBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onRefresh);
		m_settingsBtn = new QPushButton(im.loadIcon("document-properties"), QString(), outer);
		m_settingsBtn->setFixedWidth(30);
		m_settingsBtn->setToolTip(tr("News Browser settings"));
		connect(m_settingsBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onSettings);
		topBar->addWidget(title, 1);
		topBar->addWidget(helpBtn, 0);
		topBar->addWidget(m_refreshBtn, 0);
		topBar->addWidget(m_settingsBtn, 0);
		outerVl->addLayout(topBar);
		m_hostLabel = new QLabel(outer);
		m_hostLabel->setStyleSheet("color:#888; font-size:10px;");
		m_hostLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		outerVl->addWidget(m_hostLabel);
		updateHostLabel();
	}

	m_stack = new QStackedWidget(outer);
	outerVl->addWidget(m_stack, 1);

	// Page 0: locked. Nothing but a message and the Login button.
	QWidget* lockW = new QWidget(m_stack);
	QVBoxLayout* lockVl = new QVBoxLayout(lockW);
	lockVl->setContentsMargins(8, 16, 8, 8);
	m_lockLabel = new QLabel(tr("Log in to the workflow server to browse news."), lockW);
	m_lockLabel->setWordWrap(true);
	m_lockLabel->setStyleSheet("color:#666;");
	m_loginBtn = new QPushButton(tr("Login"), lockW);
	m_loginBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:6px;");
	connect(m_loginBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onLogin);
	lockVl->addWidget(m_lockLabel);
	lockVl->addWidget(m_loginBtn);
	lockVl->addStretch();
	m_stack->addWidget(lockW);

	// Page 1: the panel.
	QWidget* w = new QWidget(m_stack);
	QVBoxLayout* vl = new QVBoxLayout(w);
	vl->setSpacing(4);
	vl->setContentsMargins(4, 4, 4, 4);

	// (title / help / reload / settings live in the always-visible header above)

	// User row
	QHBoxLayout* userRow = new QHBoxLayout();
	m_userLabel = new QLabel("", w);
	m_userLabel->setStyleSheet("color:#2E7D32; font-size:11px;");
	m_userLabel->setWordWrap(true);
	m_logoutBtn = new SuneerElideButton(tr("Logout"), w);
	connect(m_logoutBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onLogout);
	userRow->addWidget(m_userLabel, 1);
	userRow->addWidget(m_logoutBtn);
	vl->addLayout(userRow);

	// Edition
	vl->addWidget(new QLabel(tr("Edition:"), w));
	m_editionCombo = new QComboBox(w);
	narrowCombo(m_editionCombo);
	connect(m_editionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &SuneerNewsPanel::onEditionChanged);
	vl->addWidget(m_editionCombo);

	// Page
	vl->addWidget(new QLabel(tr("Page:"), w));
	auto* pageCombo = new SuneerPopupCombo(w);
	m_pageCombo = pageCombo;
	narrowCombo(m_pageCombo);
	// The list shows who has which page open: asked again (async) whenever it opens.
	pageCombo->onPopup = [this]() { refreshPageLocks(); };
	connect(m_pageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) { preselectLayoutModel(); onPageSelected(i); });
	vl->addWidget(m_pageCombo);
	m_pageLockLabel = new QLabel(w);
	m_pageLockLabel->setWordWrap(true);
	m_pageLockLabel->hide();
	vl->addWidget(m_pageLockLabel);
	// Another user's page: take its lock (the workflow's Locked Pages right, or a lock that went stale)
	m_takeOverBtn = new QPushButton(tr("Take over page"), w);
	m_takeOverBtn->setObjectName("takeOverPageBtn");
	m_takeOverBtn->hide();
	connect(m_takeOverBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onTakeOverPage);
	vl->addWidget(m_takeOverBtn);
	m_lockKeepAlive = new QTimer(this);
	m_lockKeepAlive->setInterval(qEnvironmentVariableIsSet("SUNEER_PAGELOCK_KEEPALIVE_MS") ? qEnvironmentVariableIntValue("SUNEER_PAGELOCK_KEEPALIVE_MS") : 60000);
	connect(m_lockKeepAlive, &QTimer::timeout, this, &SuneerNewsPanel::keepPageLockAlive);
	connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { releasePageLockOnExit(); });

	// Date
	vl->addWidget(new QLabel(tr("Date:"), w));
	m_dateEdit = new QDateEdit(QDate::currentDate().addDays(1), w);
	m_dateEdit->setDisplayFormat("yyyy-MM-dd");
	m_dateEdit->setCalendarPopup(true);
	vl->addWidget(m_dateEdit);
	m_lockDateDebounce = new QTimer(this);
	m_lockDateDebounce->setSingleShot(true);
	m_lockDateDebounce->setInterval(600);   // typing a date: one check when it settles
	connect(m_lockDateDebounce, &QTimer::timeout, this, &SuneerNewsPanel::onLockDateChanged);
	connect(m_dateEdit, &QDateEdit::dateChanged, this, [this]() { m_lockDateDebounce->start(); });

	// (Title style and Photo position are no longer chosen here: the headline size comes from the
	// placed width - Workflow columns -> width -> the model's / house headline rule - and the photo from
	// the Layout Model. Per story: right-click > Place with...)
	// Fetch
	m_fetchBtn = new SuneerElideButton(im.loadIcon("edit-find-replace"), tr("Fetch News"), w);
	m_fetchBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:4px;");
	connect(m_fetchBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onFetchNews);
	// Next to it: the page's own .sla from the workflow (template + placed news), opened with the lock.
	m_openWfBtn = new SuneerElideButton(im.loadIcon("16/document-open.png"), tr("Open page from workflow"), w);
	m_openWfBtn->setToolTip(tr("Download this page's .sla (Edition / Page / Date) from the workflow, save it in the pages folder "
	                           "(Settings) as <edition>/<date>/<page>.sla, open it and lock the page for you"));
	m_openWfBtn->setStyleSheet("padding:4px;");
	connect(m_openWfBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onOpenPageFromWorkflow);
	{
		auto* fetchRow = new QHBoxLayout();
		fetchRow->setSpacing(4);
		fetchRow->addWidget(m_fetchBtn, 1);
		fetchRow->addWidget(m_openWfBtn, 1);
		vl->addLayout(fetchRow);
	}

	// Status: a progress bar while something runs, then ONE short line;
	// everything long goes to the Details dialog (copyable).
	m_statusBar = new QProgressBar(w);
	m_statusBar->setFixedHeight(16);
	m_statusBar->setTextVisible(true);
	m_statusBar->setAlignment(Qt::AlignCenter);
	m_statusBar->setStyleSheet("QProgressBar { font-size:10px; } ");
	m_statusBar->setVisible(false);
	vl->addWidget(m_statusBar);
	// Cancel of a long background job (Process obituary photos); shown only while it runs
	m_obitCancelBtn = new QPushButton(tr("Cancel"), w);
	m_obitCancelBtn->setObjectName("obitWfCancelBtn");
	m_obitCancelBtn->setToolTip(tr("Stop processing the obituary photos: the ones finished so far are used, the rest keep their original picture"));
	m_obitCancelBtn->hide();
	connect(m_obitCancelBtn, &QPushButton::clicked, this, [this]() {
		if (!m_obitWfRunning)
			return;
		if (m_obitWorker)
			m_obitWorker->cancel();
		finishObitWfBatch(true, false, tr("cancelled"));
	});
	vl->addWidget(m_obitCancelBtn);
	QHBoxLayout* statusRow = new QHBoxLayout();
	statusRow->setSpacing(4);
	m_statusLabel = new QLabel("", w);
	m_statusLabel->setStyleSheet("color:#666; font-size:11px;");
	m_statusLabel->setWordWrap(false);
	m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	statusRow->addWidget(m_statusLabel, 1);
	m_statusDetailsBtn = new QToolButton(w);
	m_statusDetailsBtn->setText(tr("Details\u2026"));
	m_statusDetailsBtn->setAutoRaise(true);
	m_statusDetailsBtn->setStyleSheet("font-size:10px;");
	m_statusDetailsBtn->setToolTip(tr("The full log of the last actions: stories without a picture, low res / no face / bg? photos, check, re-pack, server errors. Copyable."));
	connect(m_statusDetailsBtn, &QToolButton::clicked, this, &SuneerNewsPanel::onStatusDetails);
	statusRow->addWidget(m_statusDetailsBtn, 0);
	vl->addLayout(statusRow);

	// Select all
	m_newsList = new SuneerNewsListWidget();
	QHBoxLayout* selRow = new QHBoxLayout();
	m_selectAllChk = new SuneerElideCheck(tr("Select All"), w);
	m_selectAllChk->setStyleSheet("font-size:11px; padding:2px;");
	connect(m_selectAllChk, &QCheckBox::toggled, this, &SuneerNewsPanel::onSelectAll);
	selRow->addWidget(m_selectAllChk, 1);
	m_filterCombo = new QComboBox(w);
	narrowCombo(m_filterCombo, 5);
	m_filterCombo->addItem(tr("Show: all"), "ALL");
	m_filterCombo->addItem(tr("Show: USD"), "USED");
	m_filterCombo->addItem(tr("Show: UNUSD"), "UNUSED");
	m_filterCombo->addItem(tr("Show: BAL"), "BALANCED");
	m_filterCombo->setToolTip(tr("Which stories the list shows (local filter, no new fetch). Select All, Place and Mark act on the visible rows only."));
	{
		QSettings cfg("Faircode", "ScribusNews");
		const int idx = m_filterCombo->findData(cfg.value("list/filter", "ALL").toString());
		m_filterCombo->setCurrentIndex(idx < 0 ? 0 : idx);
	}
	connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		QSettings cfg("Faircode", "ScribusNews");
		cfg.setValue("list/filter", m_filterCombo->currentData().toString());
		applyListFilter(true);
	});
	m_selectUsedChk = new SuneerElideCheck(tr("Select all used"), w);
	m_selectUsedChk->setStyleSheet("font-size:11px; padding:2px;");
	m_selectUsedChk->setToolTip(tr("Tick every used (grey) story - for Mark unused, never for placing"));
	connect(m_selectUsedChk, &QCheckBox::toggled, this, [this](bool on) {
		for (int i = 0; i < m_newsList->count(); i++)
			if (isPlacedRow(m_newsList->item(i)) && !m_newsList->item(i)->isHidden())
				m_newsList->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	});
	selRow->addWidget(m_selectUsedChk, 1);
	m_checkBtn = new SuneerElideButton(im.loadIcon("reload"), tr("Check"), w);
	m_checkBtn->setToolTip(tr("Re-read the placed status of this page from the server (and retry unsent marks)"));
	connect(m_checkBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onCheck);
	selRow->addWidget(m_checkBtn);
	// Own row: the filter plus the whole-page counts, so the selection row
	// above keeps its width (a wider row pushes the list's right edge, and the
	// tags with it, out of the dock).
	QHBoxLayout* filterRow = new QHBoxLayout();
	filterRow->addWidget(m_filterCombo);
	m_countLabel = new QLabel(w);
	m_countLabel->setStyleSheet("font-size:11px;");
	m_countLabel->setWordWrap(true);
	filterRow->addWidget(m_countLabel, 1);
	vl->addLayout(selRow);
	vl->addLayout(filterRow);
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
		const QString sidRow = newsServerId(news);
		// the desk's column count: place this story now as 1..8 columns, or set the width for Place Selected
		QAction* placeAs[9] = {};
		QAction* useCols[9] = {};
		QAction* useDefault = nullptr;
		QList<QPair<QAction*, QString>> headActs, posActs;   // "Place with…": headline style / photo position (empty value = automatic)
		if (!isPlacedRow(row))
		{
			const int wf = news.value(QStringLiteral("noOfColumns")).toInt();
			QMenu* pm = menu.addMenu(tr("Place as"));
			QMenu* pw = menu.addMenu(tr("Place with\u2026"));
			QMenu* hm = pw->addMenu(tr("Headline style"));
			{
				const QString cur = requestedHeadStyle(news);
				QAction* autoA = hm->addAction(tr("Automatic (by width)"));
				autoA->setCheckable(true); autoA->setChecked(cur.isEmpty()); headActs << qMakePair(autoA, QString());
				hm->addSeparator();
				// the headline paragraph styles of the document: "<size> M" / "<size> B" (the house naming), by size
				QList<QPair<int, QString>> heads;
				static const QRegularExpression headName(QStringLiteral("^(\\d+)\\s+[MB]$"));
				if (m_doc)
					for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
					{
						const QString nm = m_doc->paragraphStyles()[i].name();
						const QRegularExpressionMatch mm = headName.match(nm);
						if (mm.hasMatch())
							heads << qMakePair(mm.captured(1).toInt(), nm);
					}
				std::sort(heads.begin(), heads.end());
				for (const auto& h : heads)
				{
					QAction* a = hm->addAction(h.second);
					a->setCheckable(true); a->setChecked(cur == h.second); headActs << qMakePair(a, h.second);
				}
			}
			QMenu* phm = pw->addMenu(tr("Photo"));
			{
				const QString cur = requestedPhotoPos(news);
				QAction* autoA = phm->addAction(tr("Automatic (Layout Model)"));
				autoA->setCheckable(true); autoA->setChecked(cur.isEmpty()); posActs << qMakePair(autoA, QString());
				phm->addSeparator();
				for (const QString& k : QStringList { "left", "right", "top", "none" })
				{
					QAction* a = phm->addAction(k == QLatin1String("none") ? tr("None (no photo)") : photoPositionLabel(k));
					a->setCheckable(true); a->setChecked(cur == k); posActs << qMakePair(a, k);
				}
			}
			QMenu* um = pw->addMenu(tr("Columns"));
			for (int c = 1; c <= 8; ++c)
			{
				const QString label = (c == 1 ? tr("1 column") : tr("%1 columns").arg(c)) + (c == wf ? tr(" (Workflow)") : QString());
				placeAs[c] = pm->addAction(label);
				useCols[c] = um->addAction(label);
				useCols[c]->setCheckable(true);
				useCols[c]->setChecked(requestedColumns(news) == c);
			}
			um->addSeparator();
			useDefault = um->addAction(wf > 0 ? tr("Workflow's value (%1)").arg(wf == 1 ? tr("1 column") : tr("%1 columns").arg(wf)) : tr("Layout model default (no Workflow value)"));
			menu.addSeparator();
		}
		QAction* retry = m_pendingMarks.contains(sidRow) ? menu.addAction(tr("Retry mark used (placed here, not marked on server)")) : nullptr;
		QAction* rel = allowedTransition(st, unusedStatus()) ? menu.addAction(tr("Mark as unused")) : nullptr;
		QAction* bal = allowedTransition(st, "BALANCED") ? menu.addAction(tr("Mark as balance (keep for later)")) : nullptr;
		if (menu.isEmpty())
			return;
		QAction* chosen = menu.exec(m_newsList->viewport()->mapToGlobal(p));
		if (!chosen)
			return;
		for (int c = 1; c <= 8; ++c)
		{
			if (chosen != placeAs[c] && chosen != useCols[c])
				continue;
			QJsonObject n2 = news;
			n2[QStringLiteral("_colsOverride")] = c;
			m_newsData[idx] = n2;
			relabelOverrideBadge(row, n2);
			if (chosen == placeAs[c])
			{
				m_newsList->setCurrentItem(row);
				onPlaceNews();
			}
			return;
		}
		for (const auto& ha : headActs)
			if (chosen == ha.first)
			{
				QJsonObject n2 = news;
				if (ha.second.isEmpty()) n2.remove(QStringLiteral("_headStyle")); else n2[QStringLiteral("_headStyle")] = ha.second;
				m_newsData[idx] = n2;
				relabelOverrideBadge(row, n2);
				return;
			}
		for (const auto& pa : posActs)
			if (chosen == pa.first)
			{
				QJsonObject n2 = news;
				if (pa.second.isEmpty()) n2.remove(QStringLiteral("_photoPos")); else n2[QStringLiteral("_photoPos")] = pa.second;
				m_newsData[idx] = n2;
				relabelOverrideBadge(row, n2);
				return;
			}
		if (useDefault && chosen == useDefault)
		{
			QJsonObject n2 = news;
			n2.remove(QStringLiteral("_colsOverride"));
			m_newsData[idx] = n2;
			relabelOverrideBadge(row, n2);
			return;
		}
		if (chosen && retry && chosen == retry)
			retryPendingMarks(QStringList() << sidRow);
		else if (chosen && chosen == rel)
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
	m_newsList->setIconSize(QSize(18, 18));   // suneer: smaller thumbnails on a narrow dock
	m_newsList->setItemDelegate(new SuneerNewsRowDelegate(m_newsList));
	m_newsList->setTextElideMode(Qt::ElideNone);
	m_newsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_newsList->setResizeMode(QListView::Adjust);
	m_newsList->setTextElideMode(Qt::ElideNone);   // we elide ourselves, by grapheme
	m_newsList->viewport()->installEventFilter(this);
	connect(m_newsList, &QListWidget::itemClicked, this, &SuneerNewsPanel::onNewsItemClicked);
	connect(m_newsList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { updateTickButtons(); });
	connect(m_newsList, &QListWidget::itemDoubleClicked, this, &SuneerNewsPanel::onPlaceNews);
	splitter->addWidget(m_newsList);
	vl->addWidget(splitter, 1);

	QHBoxLayout* btnRow = new QHBoxLayout();
	// Layout model: how Place Selected arranges the ticked stories.
	m_layoutModelCmb = new QComboBox(w);
	narrowCombo(m_layoutModelCmb);
	m_layoutModelCmb->setToolTip(tr("Layout model for Place Selected: Normal (story + photo frames) or Obituary (photo + text modules in a masonry over the obit columns, short news in the last columns). Remembered per page type; an OBIT page starts with Obituary."));
	fillLayoutModelCombo();
	// compact, so Place Selected keeps its room on a narrow panel
	QToolButton* spacingBtn = new QToolButton(w);
	spacingBtn->setText(QStringLiteral("\u21D5"));
	spacingBtn->setFixedWidth(26);
	spacingBtn->setToolTip(tr("Spacing\u2026 (Faircode Layout): module gaps, gutter, wrap padding, caption and photo gaps for every layout model; live preview and Apply to this page"));
	connect(spacingBtn, &QToolButton::clicked, this, &SuneerNewsPanel::onSpacing);
	spacingBtn->setToolTip(tr("Layout Models\u2026: grid, structure, story and photo rules and every spacing value of each model; live sample page; Apply spacing to this page"));
	QToolButton* galleryBtn = new QToolButton(w);
	galleryBtn->setText(QStringLiteral("\u25A6"));
	galleryBtn->setFixedWidth(26);
	galleryBtn->setToolTip(tr("Layout Gallery\u2026: this page arranged by every layout model (thumbnails, how many stories fit)"));
	connect(galleryBtn, &QToolButton::clicked, this, &SuneerNewsPanel::openLayoutGallery);
	connect(m_layoutModelCmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		QSettings cfg("Faircode", "ScribusNews");
		QString k = layoutModelKey().trimmed();
		k.replace('/', '_');
		cfg.setValue(QStringLiteral("layoutModel/") + (k.isEmpty() ? QStringLiteral("_default") : k), m_layoutModelCmb->currentData().toString());
		updateLayoutModelInfo();
	});
	btnRow->addWidget(m_layoutModelCmb, 1);
	btnRow->addWidget(spacingBtn);
	btnRow->addWidget(galleryBtn);
	m_layoutModelInfo = new QLabel(w);
	m_layoutModelInfo->setStyleSheet("font-size:10px; color:#555;");
	m_layoutModelInfo->setWordWrap(true);
	vl->addWidget(m_layoutModelInfo);
	m_placeBtn = new SuneerElideButton(im.loadIcon("tool-insert-text-frame"), tr("Place Selected"), w);
	m_placeBtn->setStyleSheet("background:#2E7D32; color:white; font-weight:bold; padding:4px;");
	m_placeBtn->setEnabled(false);
	connect(m_placeBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onPlaceSelected);
	btnRow->addWidget(m_placeBtn, 1);
	// Second row under Place: the status actions for the TICKED rows.
	QHBoxLayout* markRow = new QHBoxLayout();
	m_releaseBtn = new SuneerElideButton(tr("Mark unused"), w);
	m_releaseBtn->setToolTip(tr("Mark every TICKED used (grey) story as unused on the server (available to everyone); optionally remove them from this page"));
	m_releaseBtn->setStyleSheet("padding:4px;");
	m_releaseBtn->setEnabled(false);
	connect(m_releaseBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onMarkUnusedTicked);
	markRow->addWidget(m_releaseBtn, 1);
	m_balanceBtn = new SuneerElideButton(tr("Mark balance"), w);
	m_balanceBtn->setToolTip(tr("Mark every TICKED free or used story as BALANCE on the server (kept for a later day, listed under Balanced News); optionally remove them from this page"));
	m_balanceBtn->setStyleSheet("padding:4px;");
	m_balanceBtn->setEnabled(false);
	connect(m_balanceBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onMarkBalanceTicked);
	markRow->addWidget(m_balanceBtn, 1);
	// The Obituary-model buttons on a row of their own (the mark row is full).
	QHBoxLayout* obitRow = new QHBoxLayout();
	m_obitRepackBtn = new SuneerElideButton(tr("Re-pack page"), w);
	m_obitRepackBtn->setToolTip(tr("Layout > Re-pack Page (Ctrl+Alt+R): every module's text frame fitted to its last line, then the obit modules moved up in their own columns (order kept, no column change), 2-column news only in the news columns; what no longer fits goes to the pasteboard. Locked items, ads, masthead/folio and modules pinned with \"Pin position\" are never moved. One undo step."));
	m_obitRepackBtn->setStyleSheet("padding:4px;");
	connect(m_obitRepackBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onObitRepack);
	obitRow->addWidget(m_obitRepackBtn, 1);
	m_obitPinBtn = new SuneerElideButton(tr("Pin position"), w);
	m_obitPinBtn->setToolTip(tr("Mark the SELECTED placed modules as moved by hand (attribute news.manual = 1): Re-pack leaves them where they are. Click again on a pinned module to unpin it."));
	m_obitPinBtn->setStyleSheet("padding:4px;");
	connect(m_obitPinBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onObitPinSelected);
	obitRow->addWidget(m_obitPinBtn, 1);
	m_obitReprocessBtn = new SuneerElideButton(tr("Re-process photos"), w);
	m_obitReprocessBtn->setToolTip(tr("Obituary model: apply the current photo settings (background, tone, face fill, frame shape) again to the SELECTED placed obit photos, or to all of them when nothing is selected. The text is not touched."));
	m_obitReprocessBtn->setStyleSheet("padding:4px;");
	connect(m_obitReprocessBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onObitReprocess);
	obitRow->addWidget(m_obitReprocessBtn, 1);
	QHBoxLayout* photoRow2 = new QHBoxLayout();
	QPushButton* editPhotoBtn = new SuneerElideButton(tr("Edit photo\u2026"), w);
	editPhotoBtn->setToolTip(tr("Crop, rotate, background and tone of the SELECTED photo on its original picture (also inside a module). Edited photos are never re-processed automatically."));
	editPhotoBtn->setStyleSheet("padding:4px;");
	connect(editPhotoBtn, &QPushButton::clicked, this, [this]() {
		if (!m_doc) return;
		PageItem* item = m_doc->m_Selection->count() == 1 ? m_doc->m_Selection->itemAt(0) : nullptr;
		if (item && item->isGroup())
		{
			PageItem* found = nullptr;
			for (PageItem* c : item->groupItemList)
				if (c && c->isImageFrame() && !found) found = c;
			item = found;
		}
		if (!item || !item->isImageFrame()) { setStatus(tr("Edit photo: select one photo (or its module)")); return; }
		SuneerPhotoEdit::openDialog(m_doc, item, this);
	});
	photoRow2->addWidget(editPhotoBtn, 1);
	QPushButton* bgColorBtn = new SuneerElideButton(tr("Background colour\u2026"), w);
	bgColorBtn->setToolTip(tr("One colour behind the selected cut-out photos, or every obituary photo of this page when nothing is selected; live preview; becomes the Obituary default."));
	bgColorBtn->setStyleSheet("padding:4px;");
	connect(bgColorBtn, &QPushButton::clicked, this, [this]() { if (m_doc) SuneerPhotoEdit::openBackgroundDialog(m_doc, this); });
	photoRow2->addWidget(bgColorBtn, 1);
	vl->addLayout(photoRow2);
	m_obitWfBtn = new SuneerElideButton(tr("Process obituary photos"), w);
	m_obitWfBtn->setObjectName("obitWfProcessBtn");
	m_obitWfBtn->setToolTip(tr("Obituary photos of a page opened from the workflow (or any obituary photo not processed yet): background removal, face crop, Obit BG colour and tone - the SELECTED photos, or the whole page when nothing is selected. Runs in the background; photos edited by hand or already processed are skipped. One undo step."));
	m_obitWfBtn->setStyleSheet("padding:4px;");
	connect(m_obitWfBtn, &QPushButton::clicked, this, [this]() {
		processObitPhotos(m_doc && m_doc->m_Selection->count() > 0);
	});
	vl->addWidget(m_obitWfBtn);
	vl->addLayout(btnRow);
	// Re-layout: the stories already on the current page, arranged by another layout model (one undo step)
	QHBoxLayout* layoutRow = new QHBoxLayout();
	m_relayoutBtn = new QToolButton(w);
	m_relayoutBtn->setText(tr("Re-layout\u2026"));
	m_relayoutBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_relayoutBtn->setPopupMode(QToolButton::InstantPopup);
	m_relayoutBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_relayoutBtn->setMinimumWidth(40);
	m_relayoutBtn->setToolTip(tr("Arrange the stories already placed on the current page with a layout model. Their text, photos and captions keep their content and edits; frames placed by hand and pinned modules stay; stories that do not fit go to the pasteboard and are listed. One undo step."));
	QMenu* relayoutMenu = new QMenu(m_relayoutBtn);
	m_relayoutBtn->setMenu(relayoutMenu);
	connect(relayoutMenu, &QMenu::aboutToShow, this, [this, relayoutMenu]() {
		relayoutMenu->clear();
		relayoutMenu->addAction(tr("Layout Gallery (compare models)..."), this, [this]() { openLayoutGallery(); });
		relayoutMenu->addAction(tr("Page Modules (check the modules)..."), this, [this]() { openPageModules(); });
		relayoutMenu->addSeparator();
		const QString active = activeModelId();
		for (const auto& choice : pageModelChoices())
		{
			QAction* a = relayoutMenu->addAction(choice.second);
			a->setCheckable(true);
			a->setChecked(choice.first == active);
			const QString id = choice.first;
			connect(a, &QAction::triggered, this, [this, id]() { relayoutPage(id); });
		}
	});
	layoutRow->addWidget(m_relayoutBtn, 1);
	vl->addLayout(layoutRow);
	vl->addLayout(markRow);
	vl->addLayout(obitRow);

	m_stack->addWidget(w);
	m_stack->setCurrentIndex(0);
	setWidget(outer);
	setMinimumWidth(240);

	// The keychain is touched after main-window construction so a keyring
	// prompt never lands in the middle of start-up.
	QTimer::singleShot(800, this, &SuneerNewsPanel::startupSession);
	if (qEnvironmentVariableIsSet("SUNEER_PAGELOCK_SELFTEST"))
		QTimer::singleShot(4000, this, [this]() { runPageLockSelfTest(qEnvironmentVariable("SUNEER_PAGELOCK_SELFTEST")); });
	if (qEnvironmentVariableIsSet("SUNEER_WFOPEN_SELFTEST"))
		QTimer::singleShot(4000, this, [this]() { runWorkflowOpenSelfTest(qEnvironmentVariable("SUNEER_WFOPEN_SELFTEST")); });
	if (qEnvironmentVariableIsSet("SUNEER_OBITWF_SELFTEST"))
		QTimer::singleShot(4000, this, [this]() { runObitWfSelfTest(qEnvironmentVariable("SUNEER_OBITWF_SELFTEST")); });
	if (qEnvironmentVariableIsSet("SUNEER_TAKEOVER_SELFTEST"))
		QTimer::singleShot(4000, this, [this]() { runTakeOverSelfTest(qEnvironmentVariable("SUNEER_TAKEOVER_SELFTEST")); });
	if (qEnvironmentVariableIsSet("SUNEER_WFSYNC_SELFTEST"))
		QTimer::singleShot(4000, this, [this]() { runSyncSelfTest(qEnvironmentVariable("SUNEER_WFSYNC_SELFTEST")); });
	loadPendingMarks();                        // marks a closed Scribus could not send
	{
		// "Keep" answers of the page sync, across restarts
		QSettings ks("Faircode", "ScribusNews");
		const QStringList kept = ks.value("sync/kept").toStringList();
		m_syncKept = QSet<QString>(kept.begin(), kept.end());
	}
	if (qEnvironmentVariableIsSet("SUNEER_LOGIN_SELFTEST"))
	{
		// After the startup session check; _EARLY: while it still runs (a
		// Login clicked during a slow keyring check).
		if (qEnvironmentVariableIsSet("SUNEER_LOGIN_SELFTEST_EARLY"))
			QTimer::singleShot(1500, this, [this]() { runLoginSelfTest(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST")); });
		else
		{
			auto* wait = new QTimer(this);
			wait->setInterval(200);
			connect(wait, &QTimer::timeout, this, [this, wait]() {
				if (!m_startupDone)
					return;
				wait->stop();
				wait->deleteLater();
				QTimer::singleShot(500, this, [this]() { runLoginSelfTest(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST")); });
			});
			wait->start();
		}
	}

}

void SuneerNewsPanel::setDocument(ScribusDoc* doc)
{
	{
		static bool undoHooked = false;
		if (!undoHooked)
		{
			undoHooked = true;
			// after an undo / redo the frames of a deleted story may be back (or gone again)
			connect(UndoManager::instance(), &UndoManager::undoSignal, this, [this](int) { QTimer::singleShot(0, this, [this]() { reevaluateAfterUndo(); }); });
			connect(UndoManager::instance(), &UndoManager::redoSignal, this, [this](int) { QTimer::singleShot(0, this, [this]() { reevaluateAfterUndo(); }); });
		}
	}
	if (!doc && m_doc && m_heldDocBound && m_heldDoc == m_doc)
		releasePageLock(false, QStringLiteral("document closed"));
	if (doc != m_doc)
	{
		// another document: what was noted for the old one is void (the same document is set again
		// on every page change / undo - the pending deletes must survive that)
		m_deleted.clear();
		m_cutStories.clear();
		m_autoUnused.clear();
		// m_autoRetry stays: it is per story and page, not per document (kept on disk as well)
		m_storyIndex.clear();
		m_storyIndexDoc = nullptr;
	}
	m_doc = doc;
	// tests/suneer/layout_models_apply.sh: the layout model regression test, once, when the document is there
	static bool selfTestStarted = false;
	const QString selfTest = qEnvironmentVariable("SUNEER_LAYOUT_SELFTEST");
	if (doc && !selfTest.isEmpty() && !selfTestStarted)
	{
		selfTestStarted = true;
		QTimer::singleShot(4000, this, [this, selfTest]() { runLayoutSelfTest(selfTest); });
	}
	// SUNEER_UNUSED_SELFTEST: delete-from-page scenarios (tests/suneer/mark_unused_on_delete.sh)
	static bool unusedTestStarted = false;
	const QString unusedTest = qEnvironmentVariable("SUNEER_UNUSED_SELFTEST");
	if (doc && !unusedTest.isEmpty() && !unusedTestStarted)
	{
		unusedTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait, unusedTest]() {
			if (!m_doc || m_doc->isLoading() || !UndoManager::undoEnabled() || !m_mw || !m_mw->HaveDoc)
				return;
			wait->stop();
			wait->deleteLater();
			QTimer::singleShot(2000, this, [this, unusedTest]() { runUnusedSelfTest(unusedTest); });
		});
		wait->start();
	}
	// SUNEER_REPACK_SELFTEST: Re-pack Page / Re-pack Automatically on the opened page (tests/suneer/repack_page.sh)
	static bool repackTestStarted = false;
	const QString repackTest = qEnvironmentVariable("SUNEER_REPACK_SELFTEST");
	if (doc && !repackTest.isEmpty() && !repackTestStarted)
	{
		repackTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait, repackTest]() {
			if (!m_doc || m_doc->isLoading() || !UndoManager::undoEnabled() || !m_mw || !m_mw->HaveDoc)
				return;
			wait->stop();
			wait->deleteLater();
			QTimer::singleShot(2500, this, [this, repackTest]() { runRepackSelfTest(repackTest); });
		});
		wait->start();
	}
	// SUNEER_BOXFIT_SELFTEST: Frame Border box fitted to the text (tests/suneer/box_fit.sh)
	static bool boxFitTestStarted = false;
	if (doc && qEnvironmentVariableIsSet("SUNEER_BOXFIT_SELFTEST") && !boxFitTestStarted)
	{
		boxFitTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait]() {
			if (!m_doc || m_doc->isLoading() || !m_mw || !m_mw->HaveDoc || !m_mw->view)
				return;
			wait->stop();
			wait->deleteLater();
			QTimer::singleShot(1500, this, [this]() { if (m_doc && m_mw) SuneerBoxFit::runSelfTestIfAsked(m_mw->view, m_doc); });
		});
		wait->start();
	}
	// SUNEER_TEXTGRAD_SELFTEST: gradient text fill apply / undo / save (tests/suneer/text_gradient.sh)
	static bool textGradTestStarted = false;
	if (doc && qEnvironmentVariableIsSet("SUNEER_TEXTGRAD_SELFTEST") && !textGradTestStarted)
	{
		textGradTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait]() {
			if (!m_doc || m_doc->isLoading() || !m_mw || !m_mw->HaveDoc || !m_mw->view)
				return;
			wait->stop();
			wait->deleteLater();
			SuneerTextGradientWidget::runSelfTestIfAsked(m_mw->view, m_doc);
		});
		wait->start();
	}
	// SUNEER_BADGE_SELFTEST: photo badges at 25 / 50 / 100 / 200 % (tests/suneer/photo_badges.sh)
	static bool badgeTestStarted = false;
	if (doc && qEnvironmentVariableIsSet("SUNEER_BADGE_SELFTEST") && !badgeTestStarted)
	{
		badgeTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait]() {
			if (!m_doc || m_doc->isLoading() || !m_mw || !m_mw->HaveDoc || !m_mw->view)
				return;
			wait->stop();
			wait->deleteLater();
			SuneerPhotoBadges::runSelfTestIfAsked(m_mw->view, m_doc);
		});
		wait->start();
	}
	// SUNEER_PAGEMODEL_IMPORT: the import dialog run unattended (tests/suneer/page_models_import.sh)
	static bool pageModelImportStarted = false;
	const QString pmImport = qEnvironmentVariable("SUNEER_PAGEMODEL_IMPORT_REPORT");
	if (doc && !pmImport.isEmpty() && !pageModelImportStarted)
	{
		pageModelImportStarted = true;
		QTimer::singleShot(3000, this, [this, pmImport]() { runPageModelImportTest(pmImport); });
	}
	// SUNEER_PAGEMODEL_SELFTEST: apply the page model SUNEER_PAGEMODEL=<group>/<id> to the opened page (tests/suneer/page_models_apply.sh)
	static bool pageModelTestStarted = false;
	const QString pmTest = qEnvironmentVariable("SUNEER_PAGEMODEL_SELFTEST");
	if (doc && !pmTest.isEmpty() && !pageModelTestStarted)
	{
		pageModelTestStarted = true;
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait, pmTest]() {
			if (!m_doc || m_doc->isLoading() || !UndoManager::undoEnabled() || !m_mw || !m_mw->HaveDoc)
				return;
			wait->stop();
			wait->deleteLater();
			QTimer::singleShot(2000, this, [this, pmTest]() { runPageModelSelfTest(pmTest); });
		});
		wait->start();
	}
	// SUNEER_GAPS_SELFTEST: "Adjust gaps" on the stories of the opened page (overlap / undo / re-layout checks)
	static bool gapTestStarted = false;
	const QString gapTest = qEnvironmentVariable("SUNEER_GAPS_SELFTEST");
	if (doc && !gapTest.isEmpty() && !gapTestStarted)
	{
		gapTestStarted = true;
		// only when the document has finished loading (undo is blocked until then; a page with many photos loads for seconds)
		auto* wait = new QTimer(this);
		wait->setInterval(500);
		connect(wait, &QTimer::timeout, this, [this, wait, gapTest]() {
			if (!m_doc || m_doc->isLoading() || !UndoManager::undoEnabled() || !m_mw || !m_mw->HaveDoc)
				return;
			wait->stop();
			wait->deleteLater();
			QTimer::singleShot(2000, this, [this, gapTest]() { runGapSelfTest(gapTest); });
		});
		wait->start();
	}
}

QString SuneerNewsPanel::panelTitleStyle() const
{
	return settingsString("style/titleStyle", "44 M");
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
	{
		// the panel's own Photo selector is gone: its remembered choice goes too (other settings untouched)
		QSettings old("Faircode", "ScribusNews");
		old.remove("layout/photoPositionPanel");
	}
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
			m_apiHost = kDefaultApiHost;
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
	m_pagesFolder = s.value("pagesFolder").toString().trimmed();
}

void SuneerNewsPanel::saveConfig()
{
	QSettings s("Faircode", "ScribusNews");
	s.setValue("apiHost", m_apiHost);
	s.setValue("filesBase", m_filesBase);
	s.setValue("defaultEdition", m_lastEdition);
	s.setValue("lastPage", m_lastPage);
	s.setValue("editionShortNames", m_editionShortNames);
	if (m_pagesFolder.isEmpty())
		s.remove("pagesFolder");
	else
		s.setValue("pagesFolder", m_pagesFolder);
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

// Transitions. Confirmed from the web app bundle (2026-10-08): UNUSED->USED,
// UNUSED->BALANCED (Layout page), USED->UNUSED (Used page), BALANCED->UNUSED
// (Balanced page). USED<->BALANCED are not used by the web app; the server
// decides - they are offered and its answer is shown. Same->same is pointless.
bool SuneerNewsPanel::allowedTransition(const QString& from, const QString& to)
{
	if (from.isEmpty() || to.isEmpty() || from == to)
		return false;
	return true;
}

QString SuneerNewsPanel::transitionWhy(const QString& from, const QString& to)
{
	if (from == to)
		return to == "USED" ? tr("already used") : to == "BALANCED" ? tr("already a balance story") : tr("already free");
	return QString();
}

void SuneerNewsPanel::logTextJunk(const QJsonObject& news) const
{
	static QSet<QString> logged;
	const QString sid = newsServerId(news);
	if (sid.isEmpty() || logged.contains(sid))
		return;
	logged.insert(sid);
	QStringList found;
	auto check = [&](const QString& field, const QJsonValue& v) {
		const QString d = SuneerTextClean::describeJunk(v.toString());
		if (!d.isEmpty())
			found << QStringLiteral("%1 (%2)").arg(field, d);
	};
	for (const char* k : { "title", "kicker", "byline", "dateline", "place", "caption", "content" })
		check(QLatin1String(k), news[QLatin1String(k)]);
	const QJsonValue hl = news["highlights"];
	if (hl.isArray())
	{
		const QJsonArray a = hl.toArray();
		for (int i = 0; i < a.size(); ++i)
			check(QStringLiteral("highlights[%1]").arg(i), a[i]);
	}
	else
		check(QStringLiteral("highlights"), hl);
	const QJsonArray media = news["newsMedia"].toArray();
	for (int i = 0; i < media.size(); ++i)
	{
		const QJsonObject m = media[i].toObject();
		check(QStringLiteral("newsMedia[%1].caption").arg(i), m["caption"]);
		check(QStringLiteral("newsMedia[%1].media.caption").arg(i), m["media"].toObject()["caption"]);
	}
	if (!found.isEmpty())
		apiLog(QStringLiteral("text clean ..%1 \"%2\": %3").arg(sid.right(6), SuneerTextClean::oneLine(news["title"].toString()).left(30), found.join(QStringLiteral(" | "))));
}

void SuneerNewsPanel::apiLog(const QString& line) const
{
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/news/";
	QDir().mkpath(dir);
	QFile f(dir + "api.log");
	if (f.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream out(&f);
		out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "  " << line << "\n";
	}
	qDebug().noquote() << "news api:" << line;
}

// Fresh read of one story's status on one page: GET /news/:id -> newsPages[];
// when that route is not usable (older/other backend, no right), the page's
// own news list (/external/edition-pages/:page/news) is read instead.
QString SuneerNewsPanel::serverStatus(const QString& newsId, const QString& pageId, QString* who, QString* error)
{
	if (who) who->clear();
	if (error) error->clear();
	if (newsId.isEmpty())
		return QString();
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
	{
		// Fixture: the FILE is "the server" - re-read it, so a status changed
		// there after the list was fetched is seen like another PC's change.
		QFile f(qEnvironmentVariable("SCRIBUS_NEWS_FIXTURE"));
		if (f.open(QIODevice::ReadOnly))
		{
			const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
			for (const QJsonValue& v : root["data"].toArray())
				if (newsServerId(v.toObject()) == newsId)
				{
					if (who) *who = v.toObject()["page"].toObject()["dtpUser"].toString();
					return newsDtpStatus(v.toObject());
				}
		}
		return QString();
	}
	QString status, err;
	int code = 0;
	apiGet(QUrl(apiUrl("/news/" + newsId)), [&](QNetworkReply* reply) {
		code = httpStatus(reply);
		if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
		{
			err = describeError(reply);
			return;
		}
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		QJsonObject news = root["data"].isObject() ? root["data"].toObject() : root;
		if (news["news"].isObject())
			news = news["news"].toObject();
		QJsonObject hit;
		for (const QJsonValue& v : news["newsPages"].toArray())
		{
			const QJsonObject p = v.toObject();
			const QString pid = p["pageId"].toString().isEmpty() ? p["page"].toObject()["id"].toString() : p["pageId"].toString();
			if (pid == pageId || pageId.isEmpty())
			{
				hit = p;
				break;
			}
		}
		if (hit.isEmpty() && news["newsPages"].toArray().size() == 1)
			hit = news["newsPages"].toArray().first().toObject();
		status = hit["dtpStatus"].toString().toUpper();
		if (who)
		{
			// Whatever the server records about the last change, if anything.
			for (const QString& k : {"dtpUpdatedBy", "dtpUser", "updatedBy", "lastUpdatedBy", "user"})
			{
				const QJsonValue v = hit[k];
				QString name = v.isObject() ? (v.toObject()["name"].toString().isEmpty() ? v.toObject()["email"].toString() : v.toObject()["name"].toString()) : v.toString();
				if (!name.isEmpty()) { *who = name; break; }
			}
		}
	});
	apiLog(QStringLiteral("GET /news/%1 -> HTTP %2, status on page %3 = %4%5").arg(newsId.right(6)).arg(code).arg(pageId.right(6), status.isEmpty() ? "?" : status, err.isEmpty() ? QString() : " (" + err + ")"));
	if (status.isEmpty() && !pageId.isEmpty())
	{
		// Fallback: the same list the panel fetches, which needs no special right.
		QUrl url(apiUrl("/external/edition-pages/" + pageId + "/news"));
		QUrlQuery q;
		q.addQueryItem("publishDate", m_fetchDate);
		url.setQuery(q);
		QString err2;
		int code2 = 0;
		apiGet(url, [&](QNetworkReply* reply) {
			code2 = httpStatus(reply);
			if (reply->error() != QNetworkReply::NoError || code2 < 200 || code2 >= 300)
			{
				err2 = describeError(reply);
				return;
			}
			const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
			QJsonArray arr = root["data"].isArray() ? root["data"].toArray() : root["data"].toObject()["news"].toArray();
			if (arr.isEmpty() && root["news"].isArray())
				arr = root["news"].toArray();
			for (const QJsonValue& v : arr)
				if (newsServerId(v.toObject()) == newsId)
				{
					status = newsDtpStatus(v.toObject());
					if (who) *who = v.toObject()["page"].toObject()["dtpUser"].toString();
					break;
				}
		});
		apiLog(QStringLiteral("GET /external/edition-pages/%1/news (fallback) -> HTTP %2, status = %3%4").arg(pageId.right(6)).arg(code2).arg(status.isEmpty() ? "?" : status, err2.isEmpty() ? QString() : " (" + err2 + ")"));
		if (status.isEmpty() && err.isEmpty())
			err = err2;
	}
	if (error) *error = err;
	return status;
}

// PATCH /news/dtp-status {newsIds, dtpStatus, fromStatus, pageId}, one
// story at a time so each carries its own fromStatus. No ids or tokens are
// logged.
// Like markOnServer(), but ONE PATCH for every id of the page that is in the same state: the
// per-id read and checks stay (they are what keeps us honest), the write is batched.
bool SuneerNewsPanel::markOnServerBatch(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error, const QString& expected)
{
	const bool fixture = !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE");
	m_lastFrom.clear();
	m_lastMarkError.clear();
	if (newsIds.isEmpty() || (pageId.isEmpty() && !fixture))
	{
		if (error) *error = tr("no story id / page id");
		for (const QString& id : newsIds) m_lastMarkError[id] = tr("no story id / page id");
		return false;
	}
	if (!fixture)
	{
		QString why;
		if (markGate(pageId, &why) == MarkGate::Blocked)
		{
			// only the holder of the page lock changes used / unused of that page
			const QString e = tr("not changed: %1").arg(why);
			if (error) *error = e;
			for (const QString& id : newsIds) m_lastMarkError[id] = e;
			syncLog(QStringLiteral("mark %1 of %2 stor(ies) on page ..%3 NOT sent - %4").arg(dtpStatus).arg(newsIds.size()).arg(pageId.right(6), why));
			return false;
		}
	}
	QStringList errors;
	QHash<QString, QStringList> byFrom;        // current status -> ids to move
	for (const QString& id : newsIds)
	{
		QString who, rerr;
		const QString current = serverStatus(id, pageId, &who, &rerr);
		m_lastFrom[id] = current;
		if (current.isEmpty())
		{
			errors << id.right(6) + QStringLiteral(": ") + tr("could not read the current status (%1)").arg(rerr.isEmpty() ? tr("no page entry") : rerr);
			m_lastMarkError[id] = errors.last();
			continue;
		}
		if (current == dtpStatus)
		{
			setRowStatus(id, current);
			continue;
		}
		if (!expected.isEmpty() && current != expected)
		{
			setRowStatus(id, current);
			errors << id.right(6) + QStringLiteral(": ") + (who.isEmpty() ? tr("changed meanwhile - now %1").arg(current) : tr("changed by %1 - now %2").arg(who, current));
			m_lastMarkError[id] = errors.last();
			continue;
		}
		if (!allowedTransition(current, dtpStatus))
		{
			setRowStatus(id, current);
			errors << id.right(6) + QStringLiteral(": ") + tr("%1 (server status %2)").arg(transitionWhy(current, dtpStatus), current);
			m_lastMarkError[id] = errors.last();
			continue;
		}
		byFrom[current] << id;
	}
	for (auto it = byFrom.constBegin(); it != byFrom.constEnd(); ++it)
	{
		if (fixture)
		{
			apiLog(QStringLiteral("FIXTURE batch PATCH /news/dtp-status {newsIds:[%1], dtpStatus:%2, fromStatus:%3, pageId:..%4}").arg(it.value().join(','), dtpStatus, it.key(), pageId.right(6)));
			for (const QString& id : it.value())
				m_lastMarkError[id] = tr("fixture mode, no server");
			errors << tr("fixture mode, no server");
			continue;
		}
		QJsonObject body;
		body["newsIds"]    = QJsonArray::fromStringList(it.value());
		body["dtpStatus"]  = dtpStatus;
		if (dtpStatus == unusedStatus())          // the workflow refuses fromStatus with USED / BALANCED
			body["fromStatus"] = it.key();
		body["pageId"]     = pageId;
		QString err;
		int status = 0;
		bool ok = apiPatch(QUrl(apiUrl("/news/dtp-status")), body, [&](QNetworkReply* reply) {
			status = httpStatus(reply);
			if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
				err = describeError(reply);
		});
		if (!ok && status == 400 && err.contains("fromStatus", Qt::CaseInsensitive) && err.contains("not allowed", Qt::CaseInsensitive))
		{
			body.remove("fromStatus");
			err.clear();
			ok = apiPatch(QUrl(apiUrl("/news/dtp-status")), body, [&](QNetworkReply* reply) {
				status = httpStatus(reply);
				if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
					err = describeError(reply);
			});
		}
		apiLog(QStringLiteral("PATCH /news/dtp-status {newsIds:[%1] (%2), dtpStatus:%3, fromStatus:%4, pageId:..%5} -> HTTP %6 %7")
		       .arg(it.value().join(',')).arg(it.value().size()).arg(dtpStatus, body.contains("fromStatus") ? it.key() : QStringLiteral("(omitted)"), pageId.right(6)).arg(status).arg(ok ? QStringLiteral("ok") : err));
		if (!ok)
		{
			errors << (err.isEmpty() ? tr("server refused (HTTP %1)").arg(status) : err);
			for (const QString& id : it.value())
				m_lastMarkError[id] = errors.last();
		}
	}
	if (error) *error = errors.join("; ");
	return errors.isEmpty();
}

bool SuneerNewsPanel::markOnServer(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error, const QString& expected)
{
	const bool fixture = !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE");
	m_lastFrom.clear();
	m_lastMarkError.clear();
	if (newsIds.isEmpty() || (pageId.isEmpty() && !fixture))
	{
		if (error) *error = tr("no story id / page id");
		for (const QString& id : newsIds) m_lastMarkError[id] = tr("no story id / page id");
		return false;
	}
	if (!fixture)
	{
		QString why;
		if (markGate(pageId, &why) == MarkGate::Blocked)
		{
			// only the holder of the page lock changes used / unused of that page
			const QString e = tr("not changed: %1").arg(why);
			if (error) *error = e;
			for (const QString& id : newsIds) m_lastMarkError[id] = e;
			syncLog(QStringLiteral("mark %1 of %2 stor(ies) on page ..%3 NOT sent - %4").arg(dtpStatus).arg(newsIds.size()).arg(pageId.right(6), why));
			return false;
		}
	}
	QStringList errors;
	auto fail = [&](const QString& id, const QString& why) { errors << why; m_lastMarkError[id] = why; };
	for (const QString& id : newsIds)
	{
		QString who, rerr;
		const QString current = serverStatus(id, pageId, &who, &rerr);
		m_lastFrom[id] = current;
		if (current.isEmpty())
		{
			fail(id, tr("could not read the current status (%1)").arg(rerr.isEmpty() ? tr("no page entry") : rerr));
			continue;
		}
		if (current == dtpStatus)
		{
			// Already what we want (e.g. undo after a failed mark): nothing to send.
			apiLog(QStringLiteral("story ..%1 already %2 on page ..%3 - no request").arg(id.right(6), dtpStatus, pageId.right(6)));
			setRowStatus(id, current);
			continue;
		}
		if (!expected.isEmpty() && current != expected)
		{
			// Someone else changed it since the list was read: show, refresh, stop.
			setRowStatus(id, current);
			fail(id, who.isEmpty() ? tr("this story was changed meanwhile - now %1").arg(current)
			                       : tr("this story was changed by %1 - now %2").arg(who, current));
			continue;
		}
		if (!allowedTransition(current, dtpStatus))
		{
			setRowStatus(id, current);
			fail(id, tr("%1 (server status %2)").arg(transitionWhy(current, dtpStatus), current));
			continue;
		}
		if (fixture)
		{
			fail(id, tr("fixture mode, no server"));   // the read and the checks above still ran
			continue;
		}
		QJsonObject body;
		body["newsIds"]    = QJsonArray::fromStringList(QStringList() << id);
		body["dtpStatus"]  = dtpStatus;
		if (dtpStatus == unusedStatus())          // the workflow refuses fromStatus with USED / BALANCED
			body["fromStatus"] = current;
		body["pageId"]     = pageId;
		QString err;
		int status = 0;
		bool ok = apiPatch(QUrl(apiUrl("/news/dtp-status")), body, [&](QNetworkReply* reply) {
			status = httpStatus(reply);
			if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
				err = describeError(reply);
		});
		if (!ok && status == 400 && err.contains("fromStatus", Qt::CaseInsensitive) && err.contains("not allowed", Qt::CaseInsensitive))
		{
			// An older backend that knows no fromStatus: same change, same
			// story, field left out. Not a status retry.
			body.remove("fromStatus");
			err.clear();
			ok = apiPatch(QUrl(apiUrl("/news/dtp-status")), body, [&](QNetworkReply* reply) {
				status = httpStatus(reply);
				if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
					err = describeError(reply);
			});
		}
		apiLog(QStringLiteral("PATCH /news/dtp-status {newsIds:[..%1], dtpStatus:%2, fromStatus:%3, pageId:..%4} -> HTTP %5 %6")
		       .arg(id.right(6), dtpStatus, body.contains("fromStatus") ? current : "(omitted)", pageId.right(6)).arg(status).arg(ok ? "ok" : err));
		if (!ok)
		{
			// Refused: read once more so the row shows the truth; no resend.
			const QString now = serverStatus(id, pageId, &who);
			if (!now.isEmpty() && now != current)
			{
				setRowStatus(id, now);
				fail(id, who.isEmpty() ? tr("this story was changed meanwhile - now %1").arg(now)
				                       : tr("this story was changed by %1 - now %2").arg(who, now));
			}
			else
				fail(id, err.isEmpty() ? tr("server refused (HTTP %1)").arg(status) : err);
		}
	}
	if (error) *error = errors.join("; ");
	return errors.isEmpty();
}

// After a placement: mark USED on the server; keep the ids for a retry on
// Check when that fails. The local reply copy is updated so the marks show
// at once.
void SuneerNewsPanel::markPlaced(const QList<QJsonObject>& placedStories)
{
	QStringList okTitles, failed;
	int placed = 0;
	for (const QJsonObject& n : placedStories)
	{
		const QString sid = newsServerId(n);
		const QString title = collapseSpaces(n["title"].toString()).left(40);
		if (sid.isEmpty())
			continue;
		++placed;
		QString err;
		// Expected = what the list showed (free, or balance placed knowingly):
		// a story someone else used meanwhile is reported, not overwritten.
		const bool ok = markOnServer(QStringList() << sid, m_fetchPageId, "USED", &err, newsDtpStatus(n));
		const QString prev = m_lastFrom.value(sid);
		for (SimpleState* ss : m_placeStates)
			if (ss && ss->get("SERVER_ID") == sid)
			{
				if (!prev.isEmpty())
					ss->set("PREV", prev);
				ss->set("MARKED", ok ? "1" : "0");
			}
		if (ok)
		{
			m_pendingMarks.remove(sid);
			setRowStatus(sid, "USED");             // this row only
			okTitles << title;
		}
		else
		{
			m_pendingMarks.insert(sid);
			failed << tr("%1 - %2").arg(title, err);
		}
	}
	m_placeStates.clear();
	QString line = tr("Placed %n story(ies)", "", placed);
	if (!failed.isEmpty())
		line += tr("; %n could not be marked used on the server", "", failed.size());
	else if (placed > 0)
		line += tr(", marked USED on the server");
	if (m_placeUsedSkipped > 0)
		line += tr(" - %n used story(ies) skipped", "", m_placeUsedSkipped);
	m_placeUsedSkipped = 0;
	setStatus(line, !failed.isEmpty());
	refreshPlacedMarks();
	if (!failed.isEmpty())
		setStatusDetails(tr("Not marked used on the server (they stay placed, shown as 'Placed here - not marked on server'; right-click the row > Retry mark used, or press Check):")
		                 + "\n\u2022 " + failed.join("\n\u2022 "));
}

// ---------------------------------------------------------------------------
// Status line: one short line under Fetch News, red when something failed,
// the full text in the tooltip, everything in the Details log.
// ---------------------------------------------------------------------------

void SuneerNewsPanel::setStatus(const QString& text, bool failed)
{
	static const QRegularExpression failWords(QStringLiteral("could not|failed|Not done|not marked|refused|Error|NOT |not found|not readable|parked"), QRegularExpression::CaseInsensitiveOption);
	m_statusFull = text;
	if (m_statusBar)
		m_statusBar->setVisible(false);
	const bool red = failed || failWords.match(text).hasMatch();
	QString first = text.section('\n', 0, 0).simplified();
	if (m_statusLabel)
	{
		m_statusLabel->setStyleSheet(red ? QStringLiteral("color:#c62828; font-size:11px;") : QStringLiteral("color:#666; font-size:11px;"));
		const int w = qMax(120, m_statusLabel->width() - 4);
		m_statusLabel->setText(m_statusLabel->fontMetrics().elidedText(first, Qt::ElideRight, w));
		m_statusLabel->setToolTip(text);
	}
	if (!text.trimmed().isEmpty())
		m_statusLog << QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text;
	while (m_statusLog.size() > 400)
		m_statusLog.removeFirst();
}

void SuneerNewsPanel::setStatusDetails(const QString& text)
{
	if (text.trimmed().isEmpty())
		return;
	m_statusLog << QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text;
	if (m_statusLabel)
		m_statusLabel->setToolTip(m_statusFull + QStringLiteral("\n\n") + text);
}

void SuneerNewsPanel::statusProgress(const QString& step, int done, int total)
{
	if (!m_statusBar)
		return;
	m_statusBar->setVisible(true);
	if (total > 0)
	{
		m_statusBar->setRange(0, total);
		m_statusBar->setValue(qBound(0, done, total));
		m_statusBar->setFormat(QStringLiteral("%1 %2/%3").arg(step).arg(done).arg(total));
	}
	else
	{
		m_statusBar->setRange(0, 0);
		m_statusBar->setFormat(step + QStringLiteral("\u2026"));
	}
	if (m_statusLabel)
		m_statusLabel->setText(QString());
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SuneerNewsPanel::statusProgressDone()
{
	if (m_statusBar)
		m_statusBar->setVisible(false);
}

void SuneerNewsPanel::onStatusDetails()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("News Browser - details"));
	dlg.resize(760, 520);
	QVBoxLayout* lay = new QVBoxLayout(&dlg);
	QPlainTextEdit* txt = new QPlainTextEdit(&dlg);
	txt->setReadOnly(true);
	txt->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	QFont f = txt->font();
	f.setPointSizeF(f.pointSizeF() - 1);
	txt->setFont(f);
	QString lockNow;
	{
		const QString id = m_pageCombo ? m_pageCombo->currentData().toString() : QString();
		const QString date = m_dateEdit ? m_dateEdit->date().toString("yyyy-MM-dd") : QString();
		const auto it = m_pageLocks.constFind(id);
		if (!id.isEmpty() && m_pageLocksKnown && m_pageLocksDate == date && it != m_pageLocks.constEnd() && it->userId != m_userId)
			lockNow = pageLockDetails(id, date) + QStringLiteral("\n\n----- log -----\n");
	}
	txt->setPlainText(lockNow + m_statusLog.join(QStringLiteral("\n")));
	txt->moveCursor(lockNow.isEmpty() ? QTextCursor::End : QTextCursor::Start);   // another user's page: its lock first
	lay->addWidget(txt);
	QHBoxLayout* row = new QHBoxLayout();
	QPushButton* copyBtn = new QPushButton(tr("Copy all"), &dlg);
	connect(copyBtn, &QPushButton::clicked, &dlg, [txt]() { QApplication::clipboard()->setText(txt->toPlainText()); });
	QPushButton* logBtn = new QPushButton(tr("Open api.log folder"), &dlg);
	connect(logBtn, &QPushButton::clicked, &dlg, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news"))); });
	QPushButton* closeBtn = new QPushButton(tr("Close"), &dlg);
	connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
	row->addWidget(copyBtn);
	row->addWidget(logBtn);
	row->addStretch(1);
	row->addWidget(closeBtn);
	lay->addLayout(row);
	dlg.exec();
}

// Inside the placement transaction: the server side of the undo record.
void SuneerNewsPanel::recordPlaceUndo(const QList<QJsonObject>& stories)
{
	m_placeStates.clear();
	if (!UndoManager::undoEnabled())
		return;
	for (const QJsonObject& n : stories)
	{
		const QString sid = newsServerId(n);
		if (sid.isEmpty())
			continue;
		auto* ss = new SimpleState(tr("Story status on server"), QString(), Um::ICreate);
		ss->set("SUNEER_NEWS_PLACE");
		ss->set("SERVER_ID", sid);
		ss->set("PAGE_ID", m_fetchPageId);
		ss->set("STORY", collapseSpaces(n["title"].toString()).left(40));
		ss->set("PREV", newsDtpStatus(n));       // refined by markPlaced() with the fresh read
		ss->set("MARKED", "0");
		UndoManager::instance()->action(this, ss);
		m_placeStates << ss;
	}
}

// Placed but not marked USED: try again (Check, or right-click > Retry mark used).
void SuneerNewsPanel::retryPendingMarks(const QStringList& which)
{
	QStringList ids = which.isEmpty() ? m_pendingMarks.values() : which;
	if (ids.isEmpty())
		return;
	QStringList done, failed;
	for (const QString& sid : ids)
	{
		QString err, title = sid.right(6);
		for (const QJsonValue& v : m_newsData)
			if (newsServerId(v.toObject()) == sid)
				title = collapseSpaces(v.toObject()["title"].toString()).left(40);
		if (markOnServer(QStringList() << sid, m_fetchPageId, "USED", &err))
		{
			m_pendingMarks.remove(sid);
			setRowStatus(sid, "USED");
			done << title;
		}
		else
			failed << tr("%1 - %2").arg(title, err);
	}
	refreshPlacedMarks();
	setStatus(tr("Marked used: %1, still not marked: %2").arg(done.size()).arg(failed.size()));
	if (!failed.isEmpty())
		QMessageBox::warning(this, tr("Retry mark used"), tr("Still not marked used on the server:") + "\n\n" + failed.join("\n"));
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
	if (!m_pendingMarks.isEmpty())
		retryPendingMarks();
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
		setStatus(tr("Select a used (grey) story first"));
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
				setStatus(tr("Marked unused on the server: %1").arg(title));
			}
			else
				setStatus(tr("Error: server refused (%1)").arg(err));
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
		setStatus(tr("Removed from page and marked unused on the server: %1").arg(storyId));
	else
		setStatus(tr("Removed from page, but NOT marked unused on the server (%1) - use 'Mark unused' in News Browser later").arg(err));
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
	const bool marked = !sid.isEmpty() && !pageId.isEmpty() && markOnServer(QStringList() << sid, pageId, status, &err, QStringLiteral("USED"));
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
		if (row->checkState() != Qt::Checked || row->isHidden())
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
	if (rows.isEmpty()) { setStatus(tr("Tick one or more used or balance stories first")); return; }
	markStories(rows, unusedStatus());
}

void SuneerNewsPanel::onMarkBalanceTicked()
{
	const QList<QJsonObject> rows = suneerTickedRows(m_newsList, m_newsData, [](const QJsonObject& n) { return newsDtpStatus(n) != "BALANCED"; });
	if (rows.isEmpty()) { setStatus(tr("Tick one or more free or used stories first")); return; }
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
	if (!confirmWorkflowPage(tr("Mark")))
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
		else if (markOnServer(QStringList() << sid, m_fetchPageId, status, &err, newsDtpStatus(n)))
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
	setStatus(done.arg(released.size()) + (failed.isEmpty() ? QString() : tr(", refused: %1").arg(failed.size())));
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
	if (restoreObitWfPhotos(state, isUndo))
		return;
	auto* ss = dynamic_cast<SimpleState*>(state);
	if (ss)
		apiLog(QStringLiteral("undo/redo state %1 %2 story ..%3").arg(isUndo ? "undo" : "redo", ss->contains("SUNEER_NEWS_PLACE") ? "PLACE" : ss->contains("SUNEER_NEWS_DTP") ? "DTP" : "other", ss->get("SERVER_ID").right(6)));
	if (ss && ss->contains("SUNEER_NEWS_PLACE"))
	{
		// Place Selected undone: frames are gone (the transaction), the story
		// goes back to the status it had; redone: USED again.
		const QString sid = ss->get("SERVER_ID");
		const QString pageId = ss->get("PAGE_ID");
		const QString story = ss->get("STORY");
		const QString prev = ss->get("PREV").isEmpty() ? QStringLiteral("UNUSED") : ss->get("PREV");
		const QString want = isUndo ? prev : QStringLiteral("USED");
		const QString expect = isUndo ? QStringLiteral("USED") : prev;
		QString err;
		if (isUndo && ss->get("MARKED") != "1")
		{
			// It never became USED on the server: nothing to put back.
			m_pendingMarks.remove(sid);
			setRowStatus(sid, prev);
			setStatus(tr("Placement undone: %1 (it was not marked used on the server)").arg(story));
			return;
		}
		if (markOnServer(QStringList() << sid, pageId, want, &err, expect))
		{
			m_pendingMarks.remove(sid);
			setRowStatus(sid, want);
			setStatus(isUndo ? tr("Placement undone and the story is %1 again on the server: %2").arg(want, story)
			                              : tr("Placed again and marked USED on the server: %1").arg(story));
		}
		else
		{
			setStatus(tr("Warning: placement %1, but the server status could not be set to %2 (%3)")
			                       .arg(isUndo ? tr("undone") : tr("redone"), want, err));
			// one warning for the whole undo / redo step (a Place of 30 stories is 30 server states)
			if (m_restoreFailures.isEmpty())
				QTimer::singleShot(0, this, [this, isUndo, want, err]() {
					const QStringList names = m_restoreFailures;
					m_restoreFailures.clear();
					QMessageBox::warning(this, tr("Undo placement"),
						tr("The frames were %1, but %n story(ies) could not be set to %2 on the server:\n%3\n\n%4\n\nUse Check / Mark unused in the News Browser.", "", names.size())
						.arg(isUndo ? tr("removed") : tr("placed again"), want, err, names.join(QStringLiteral(", "))));
				});
			m_restoreFailures << story;
		}
		return;
	}
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
		setStatus(isUndo ? tr("Story back on the page and marked used again: %1").arg(story)
		                              : tr("Story removed again and marked unused: %1").arg(story));
	}
	else
		setStatus(tr("Warning: frames %1, but the server mark failed (%2)")
		                       .arg(isUndo ? tr("restored") : tr("removed"), err));
}

// Plain Delete on story frames: a normal delete, no server call. Say so once.
void SuneerNewsPanel::deleteKeyHint()
{
	if (m_removing || m_deleteHintShown || markUnusedOnDeletePref())
		return;
	m_deleteHintShown = true;
	// Main-window status bar only: the same document path also runs when a
	// placement is undone, and the panel's own line must keep that result.
	if (m_mw)
		m_mw->setStatusBarInfoText(tr("Story frames deleted. To make the story available again, use right-click > Remove story (mark unused), or Mark as unused in the News Browser."));
}

// ---------------------------------------------------------------------------
// Delete from the page = mark unused (automatic)
// ---------------------------------------------------------------------------

bool SuneerNewsPanel::markUnusedOnDeletePref()
{
	QSettings s("Faircode", "ScribusNews");
	return s.value("news/markUnusedOnDelete", true).toBool();
}

// ---- the index: story id -> live frames of that story (top-level items and group children) ----
// Built once per document from one scan, then kept by the places (tagStoryItems), the deletes
// (noteDeleting), the pastes (notePasted: the pasted selection) and undo / redo (the tracked
// stories only). "Last frame of the story gone?" is then one hash lookup.
void SuneerNewsPanel::indexBuild()
{
	m_storyIndex.clear();
	m_storyIndexDoc = m_doc;
	if (!m_doc)
		return;
	std::function<void(PageItem*)> add = [&](PageItem* it) {
		const QString st = attr(it, "news.story");
		if (!st.isEmpty())
			m_storyIndex[st].insert(it);
		if (it->isGroup())
			for (PageItem* c : it->groupItemList)
				add(c);
	};
	for (int i = 0; i < m_doc->Items->count(); ++i)
		add(m_doc->Items->at(i));
}

void SuneerNewsPanel::indexAdd(const QList<PageItem*>& items)
{
	if (m_storyIndexDoc != m_doc)
		indexBuild();
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
		{
			const QString st = attr(p, "news.story");
			if (!st.isEmpty())
				m_storyIndex[st].insert(p);
		}
	}
}

void SuneerNewsPanel::indexRemove(const QList<PageItem*>& items)
{
	if (m_storyIndexDoc != m_doc)
		indexBuild();
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
		{
			const QString st = attr(p, "news.story");
			if (st.isEmpty())
				continue;
			auto f = m_storyIndex.find(st);
			if (f == m_storyIndex.end())
				continue;
			f->remove(p);
			if (f->isEmpty())
				m_storyIndex.erase(f);
		}
	}
}

//! Live frames of a story: the index (O(1)); a story the index does not know has none.
int SuneerNewsPanel::indexCount(const QString& storyId)
{
	if (m_storyIndexDoc != m_doc)
		indexBuild();
	auto f = m_storyIndex.constFind(storyId);
	return f == m_storyIndex.constEnd() ? 0 : f->size();
}

//! After undo / redo the frames of a tracked story may be back: its index entry is rebuilt from the document (only that id).
void SuneerNewsPanel::indexResync(const QString& storyId)
{
	if (m_storyIndexDoc != m_doc)
	{
		indexBuild();
		return;
	}
	m_storyIndex.remove(storyId);
	QList<PageItem*> found = storyItems(storyId);   // one scan, for this id only, on undo / redo
	for (PageItem* it : found)
	{
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
			if (attr(p, "news.story") == storyId)
				m_storyIndex[storyId].insert(p);
	}
}

// The document, before it deletes the frames: the index loses them; the stories among them are
// noted for the debounced check (one single-shot ~2 s after the last delete - the only timer here).
void SuneerNewsPanel::noteDeleting(const QList<PageItem*>& items)
{
	if (!m_doc || m_removing || m_preview)
		return;
	// A deleted frame takes its welds with it; the rest of the story is welded again right after
	// the deletion (own undo step, so Undo unwinds in order: re-weld, then the deletion).
	{
		QSet<QString> touched;
		for (PageItem* it : items)
			if (it && !attr(it, "news.story").isEmpty())
				touched.insert(attr(it, "news.story"));
		if (!touched.isEmpty())
		{
			QPointer<ScribusDoc> doc = m_doc;
			QTimer::singleShot(0, this, [this, doc, touched]() {
				if (!doc || doc != m_doc) return;
				for (const QString& st : touched)
					weldStoryItems(storyItems(st));
			});
		}
	}
	QElapsedTimer clock;
	clock.start();
	const bool pref = markUnusedOnDeletePref();
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
		{
			const QString story = attr(p, "news.story");
			if (story.isEmpty())
				continue;
			if (pref && !m_deleted.contains(story))
			{
				const QString sid = attr(p, "news.serverId");
				if (!sid.isEmpty())
				{
					DeletedStory d;
					d.storyId = story;
					d.serverId = sid;
					d.pageId = attr(p, "news.pageId");
					d.title = p->itemName();
					m_deleted.insert(story, d);
				}
			}
		}
	}
	indexRemove(items);
	m_autoHookNs += clock.nsecsElapsed();
	if (m_deleted.isEmpty())
		return;
	if (!m_deleteCheckTimer)
	{
		m_deleteCheckTimer = new QTimer(this);
		m_deleteCheckTimer->setSingleShot(true);
		connect(m_deleteCheckTimer, &QTimer::timeout, this, &SuneerNewsPanel::checkDeletedStories);
	}
	m_deleteCheckTimer->start(2000);          // debounce: many deletes in a row -> one check, one request per page
}

// Cut: the frames go to the clipboard; the stories stay USED until they are pasted (nothing to do)
// or the clipboard holds something else (then they are gone for good).
void SuneerNewsPanel::noteCut(const QList<PageItem*>& items)
{
	if (!markUnusedOnDeletePref())
		return;
	for (PageItem* it : items)
	{
		if (!it)
			continue;
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
			if (!attr(p, "news.story").isEmpty() && !attr(p, "news.serverId").isEmpty())
				m_cutStories.insert(attr(p, "news.story"));
	}
	if (m_cutStories.isEmpty())
		return;
	static bool clipHooked = false;
	if (!clipHooked)
	{
		clipHooked = true;
		connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
			// something else on the clipboard: the cut frames can never be pasted now
			if (m_cutStories.isEmpty())
				return;
			m_cutStories.clear();
			if (m_deleteCheckTimer)
				m_deleteCheckTimer->start(200);
		});
	}
}

// After a paste: the pasted items are the selection - into the index; cut stories are back.
void SuneerNewsPanel::notePasted()
{
	if (!m_doc)
		return;
	QList<PageItem*> pasted;
	for (int i = 0; i < m_doc->m_Selection->count(); ++i)
		pasted << m_doc->m_Selection->itemAt(i);
	indexAdd(pasted);
	if (m_cutStories.isEmpty() && m_deleted.isEmpty())
		return;
	m_cutStories.clear();
	if (m_deleteCheckTimer)
		m_deleteCheckTimer->start(200);
}

// The debounced check: every noted story with no frame left in the index (pages and pasteboard -
// the index covers the whole document) and not waiting on the clipboard is marked UNUSED.
void SuneerNewsPanel::checkDeletedStories()
{
	QElapsedTimer clock;
	clock.start();
	if (!m_doc)
	{
		m_deleted.clear();
		return;
	}
	QList<DeletedStory> gone;
	for (auto it = m_deleted.begin(); it != m_deleted.end();)
	{
		if (m_cutStories.contains(it.key()))
		{
			++it;                                   // on the clipboard: decided when it is pasted or replaced
			continue;
		}
		if (indexCount(it.key()) == 0)
		{
			// the index says gone: confirmed with one scan for THIS id only, so a frame made by a path
			// without a hook (Item > Duplicate, a script) never leads to a wrong mark
			indexResync(it.key());
			if (indexCount(it.key()) == 0)
				gone << it.value();
		}
		it = m_deleted.erase(it);
	}
	m_autoHookNs += clock.nsecsElapsed();
	if (!gone.isEmpty())
		autoMarkStories(gone, unusedStatus(), QStringLiteral("USED"), tr("deleted from the page"));
	// a delete is also the moment to retry what failed earlier (no loop, no timer)
	retryAutoMarks();
}

// One asynchronous PATCH per page (the server takes a list of ids); the UI never waits. The
// reply updates the rows, the status line and the main window's status bar; a failure goes
// to the retry queue, which is tried again on the next delete, Fetch News, Save and close (and
// is kept on disk for the next start). Only the holder of the page lock changes a page: another
// user's page is left alone, a page this Scribus has not locked waits in the queue.
void SuneerNewsPanel::autoMarkStories(const QList<DeletedStory>& stories, const QString& status, const QString& expected, const QString& why)
{
	if (stories.isEmpty())
		return;
	QHash<QString, QList<DeletedStory>> byPage;
	for (const DeletedStory& d : stories)
		byPage[d.pageId] << d;
	const bool fixture = !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE");
	for (auto it = byPage.constBegin(); it != byPage.constEnd(); ++it)
	{
		QStringList ids, storyIds;
		for (const DeletedStory& d : it.value())
		{
			ids << d.serverId;
			storyIds << d.storyId;
			if (status == unusedStatus())
			{
				DeletedStory u = d;
				u.marked = false;
				m_autoUnused.insert(d.storyId, u);   // the intent, whatever the server says
			}
			else
				m_autoUnused.remove(d.storyId);
			m_autoRetry.remove(retryKey(d));
		}
		m_autoMarkLog << QStringLiteral("%1 %2 [%3]").arg(status, why, storyIds.join(','));
		const QString pageId = it.key();
		const QList<DeletedStory> group = it.value();
		auto queue = [this, &group, &status]() {
			for (const DeletedStory& d : group) { DeletedStory r = d; r.title = status; m_autoRetry.insert(retryKey(r), r); }
		};
		if (fixture || pageId.isEmpty())
		{
			syncLog(QStringLiteral("auto mark %1 (%2): %3 - %4").arg(status, why, ids.join(','), fixture ? QStringLiteral("fixture, no request") : QStringLiteral("no page id, queued")));
			if (!fixture)
				queue();
			continue;
		}
		QString gateWhy;
		const MarkGate gate = markGate(pageId, &gateWhy);
		if (gate == MarkGate::Blocked)
		{
			syncLog(QStringLiteral("auto mark %1 (%2): %3 on page ..%4 NOT sent - %5").arg(status, why, ids.join(','), pageId.right(6), gateWhy));
			const QString msg = tr("Used / unused not changed: %1").arg(gateWhy);
			setStatus(msg, true);
			if (m_mw)
				m_mw->setStatusBarInfoText(msg);
			continue;
		}
		if (gate == MarkGate::Deferred)
		{
			syncLog(QStringLiteral("auto mark %1 (%2): %3 on page ..%4 queued - %5").arg(status, why, ids.join(','), pageId.right(6), gateWhy));
			queue();
			continue;
		}
		QJsonObject body;
		body["newsIds"]    = QJsonArray::fromStringList(ids);
		body["dtpStatus"]  = status;
		// The workflow's validator wants fromStatus with UNUSED only and refuses it with USED /
		// BALANCED ("fromStatus" is not allowed): a re-mark USED after an undo used to fail there.
		if (status == unusedStatus())
			body["fromStatus"] = expected;
		body["pageId"]     = pageId;
		ensureSecretsLoaded();
		QNetworkRequest req = apiRequest(QUrl(apiUrl("/news/dtp-status")), true);
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setTransferTimeout(8000);               // short: a dead server never holds anything up
		QNetworkReply* reply = m_nam->sendCustomRequest(req, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
		const bool unused = status == unusedStatus();
		++m_marksInFlight;
		for (const DeletedStory& d : group)
			m_inFlightMarks.insert(retryKey(d), status);
		connect(reply, &QNetworkReply::finished, this, [this, reply, group, ids, status, why, unused, pageId]() {
			reply->deleteLater();
			--m_marksInFlight;
			for (const DeletedStory& d : group)
				if (m_inFlightMarks.value(retryKey(d)) == status)
					m_inFlightMarks.remove(retryKey(d));
			const int http = httpStatus(reply);
			const bool ok = reply->error() == QNetworkReply::NoError && http >= 200 && http < 300;
			const QJsonValue count = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject()["updatedCount"];
			syncLog(QStringLiteral("PATCH /news/dtp-status {newsIds:[%1] (%2), dtpStatus:%3, pageId:..%4} -> HTTP %5 %6%7 (async, %8)")
			        .arg(ids.join(',')).arg(ids.size()).arg(status, pageId.right(6)).arg(http).arg(ok ? QStringLiteral("ok") : describeError(reply),
			             count.isDouble() ? QStringLiteral(", updatedCount %1").arg(count.toInt()) : QString(), why));
			QStringList names;
			for (const DeletedStory& d : group)
			{
				names << d.storyId;
				if (ok)
				{
					setRowStatus(d.serverId, status);
					if (unused && m_autoUnused.contains(d.storyId))
						m_autoUnused[d.storyId].marked = true;
				}
				else
				{
					DeletedStory r = d;
					r.title = status;                 // the retry remembers which status it wants
					m_autoRetry.insert(retryKey(r), r);
				}
			}
			savePendingMarks();
			QString msg;
			if (ok)
				msg = unused ? tr("%n story(ies) marked unused on the server (%1) - Undo (Ctrl+Z) brings them back and marks them used again", "", names.size()).arg(why)
				             : tr("%n story(ies) marked used on the server (%1)", "", names.size()).arg(why);
			else
				msg = tr("Warning: %n story(ies) could not be marked %1 on the server (%2) - kept for a retry on the next delete, Fetch News, Save or close: %3", "", names.size())
				          .arg(unused ? tr("unused") : tr("used"), describeError(reply), names.join(QStringLiteral(", ")));
			setStatus(msg, !ok);
			if (m_mw)
				m_mw->setStatusBarInfoText(msg);
			refreshUsedBadges();
		});
	}
	savePendingMarks();
}

void SuneerNewsPanel::refreshUsedBadges()
{
	if (m_newsList)
	{
		m_newsList->viewport()->update();
		updateTickButtons();
	}
}

// Retry of failed / waiting marks: the next delete check, Fetch News, Save and close - never a timer.
void SuneerNewsPanel::retryAutoMarks()
{
	if (m_autoRetry.isEmpty() || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	QHash<QString, QList<DeletedStory>> byStatus;
	for (const DeletedStory& d : m_autoRetry)
		byStatus[d.title] << d;
	m_autoRetry.clear();                          // a failure (or a page not locked) puts them back
	for (auto it = byStatus.constBegin(); it != byStatus.constEnd(); ++it)
		autoMarkStories(it.value(), it.key(), it.key() == unusedStatus() ? QStringLiteral("USED") : unusedStatus(), tr("retry"));
}

//! Save: what is on the page goes USED, what failed earlier is sent again.
void SuneerNewsPanel::noteSaved()
{
	pushOnPage(tr("Save"));
	retryAutoMarks();
}

// The retry queue on disk (no token, no story text): serverId, pageId, wanted status, story name.
static QString pendingMarksPath()
{
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news/pending-marks.json");
}

void SuneerNewsPanel::loadPendingMarks()
{
	QFile f(pendingMarksPath());
	if (!f.open(QIODevice::ReadOnly))
		return;
	int n = 0;
	for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array())
	{
		const QJsonObject o = v.toObject();
		DeletedStory d;
		d.serverId = o["serverId"].toString();
		d.pageId = o["pageId"].toString();
		d.title = o["status"].toString();
		d.storyId = o["story"].toString();
		if (d.serverId.isEmpty() || d.pageId.isEmpty() || d.title.isEmpty() || m_autoRetry.contains(retryKey(d)))
			continue;
		m_autoRetry.insert(retryKey(d), d);
		++n;
	}
	if (n > 0)
		syncLog(QStringLiteral("pending marks from the last session: %1 (sent on the next Fetch News / Save / delete)").arg(n));
}

void SuneerNewsPanel::savePendingMarks() const
{
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	QJsonArray a;
	for (const DeletedStory& d : m_autoRetry)
	{
		QJsonObject o;
		o["serverId"] = d.serverId;
		o["pageId"] = d.pageId;
		o["status"] = d.title;
		o["story"] = d.storyId;
		a.append(o);
	}
	const QString path = pendingMarksPath();
	if (a.isEmpty() && !QFileInfo::exists(path))
		return;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(QJsonDocument(a).toJson(QJsonDocument::Indented));
		f.commit();
	}
}

// ---------------------------------------------------------------------------
// Regression test: the delete-from-page scenarios (tests/suneer/mark_unused_on_delete.sh).
// Runs in fixture mode on a page placed from the same fixture; every scenario waits for the
// 2 s check and then looks at what autoMarkStories() decided (m_autoMarkLog), never at the server.
// ---------------------------------------------------------------------------
void SuneerNewsPanel::runUnusedSelfTest(const QString& outPath)
{
	auto* out = new QFile(outPath, this);
	if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	auto* ts = new QTextStream(out);
	auto line = [ts](const QString& l) { *ts << l << '\n'; ts->flush(); fprintf(stderr, "UNUSED-SELFTEST %s\n", qPrintable(l)); };
	if (!m_doc)
	{
		line(QStringLiteral("FAIL no document"));
		QTimer::singleShot(0, qApp, []() { qApp->exit(2); });
		return;
	}
	// the stories on the page, by story id, with their frames
	QStringList stories;
	for (int i = 0; i < m_doc->Items->count(); ++i)
	{
		const QString st = attr(m_doc->Items->at(i), "news.story");
		if (!st.isEmpty() && !attr(m_doc->Items->at(i), "news.serverId").isEmpty() && !stories.contains(st))
			stories << st;
	}
	line(QStringLiteral("document %1: %2 placed stories, pref markUnusedOnDelete=%3, fixture=%4").arg(m_doc->documentFileName()).arg(stories.size()).arg(markUnusedOnDeletePref() ? 1 : 0).arg(qEnvironmentVariable("SCRIBUS_NEWS_FIXTURE")));
	if (stories.size() < 8)
	{
		line(QStringLiteral("FAIL need at least 8 placed stories"));
		QTimer::singleShot(0, qApp, []() { qApp->exit(2); });
		return;
	}
	for (int i = 0; i < m_doc->Items->count(); ++i)
		if (!attr(m_doc->Items->at(i), "news.story").isEmpty() && attr(m_doc->Items->at(i), "news.pageId").isEmpty())
			setAttr(m_doc->Items->at(i), "news.pageId", QStringLiteral("selftest-page"));
	// the undo GUIs (toolbar widget, Action History) keep their own item lists; an undo requested
	// from code trips them (their list may be shorter than the stack): unregistered for the test
	for (UndoGui* g : m_mw->findChildren<UndoGui*>())
		UndoManager::instance()->removeGui(g);
	auto itemsOf = [this](const QString& st) { return storyItems(st); };
	QElapsedTimer hookClock;
	auto hookTime = [this]() { const double ms = m_autoHookNs / 1e6; m_autoHookNs = 0; return ms; };
	auto deleteItems = [this](const QList<PageItem*>& items) {
		Selection sel(this, false);
		for (PageItem* it : items)
			sel.addItem(it);
		m_doc->itemSelection_DeleteItem(&sel);
		qApp->processEvents();
	};
	auto photoOf = [](const QList<PageItem*>& items) -> PageItem* {
		for (PageItem* it : items)
			if (it->isImageFrame())
				return it;
		return nullptr;
	};
	struct Step { QString name; std::function<void()> act; std::function<bool(QString&)> check; int waitMs; };
	auto* steps = new QList<Step>();
	auto* failures = new int(0);
	auto* idx = new int(0);
	auto logSince = [this](int from) { QStringList l = m_autoMarkLog.mid(from); return l.join(QStringLiteral(" | ")); };
	auto* mark = new int(0);
	auto expectMarks = [this, mark, logSince](const QStringList& want, QString& detail) {
		const QStringList got = m_autoMarkLog.mid(*mark);
		detail = QStringLiteral("marks: %1").arg(got.isEmpty() ? QStringLiteral("none") : got.join(QStringLiteral(" | ")));
		if (got.size() != want.size())
			return false;
		for (int i = 0; i < want.size(); ++i)
			if (!got[i].startsWith(want[i]))
				return false;
		return true;
	};
	const QString A = stories[0], B = stories[1], C = stories[2], D = stories[3], E = stories[4], F = stories[5], G = stories[6], H = stories[7];
	// 1. whole story deleted -> UNUSED
	steps->append({ QStringLiteral("1 delete whole story"), [=]() { deleteItems(itemsOf(A)); },
	                [=](QString& d) { const bool ok = expectMarks({ unusedStatus() + QStringLiteral(" deleted from the page [") + A + QStringLiteral("]") }, d); d += QStringLiteral("; UI time in the hook + check: %1 ms").arg(hookTime(), 0, 'f', 3); return ok; }, 3500 });
	// 4. undo of 1 -> USED, redo -> UNUSED  (3 undo steps back: paste, cut, photo delete... so use the explicit count)
	steps->append({ QStringLiteral("2a undo the delete of story 1"), [=]() {
		                UndoManager::instance()->undo(1); qApp->processEvents(); },
	                [=](QString& d) { return expectMarks({ QStringLiteral("USED undo [") + A + QStringLiteral("]") }, d) && !itemsOf(A).isEmpty(); }, 1500 });
	steps->append({ QStringLiteral("2b redo the delete"), [=]() { UndoManager::instance()->redo(1); qApp->processEvents(); },
	                [=](QString& d) { return expectMarks({ unusedStatus() + QStringLiteral(" redo [") + A + QStringLiteral("]") }, d) && itemsOf(A).isEmpty(); }, 1500 });
	// 2. only the photo -> nothing
	steps->append({ QStringLiteral("2 delete only the photo"), [=]() { PageItem* p = photoOf(itemsOf(B)); if (p) deleteItems({ p }); },
	                [=](QString& d) { return expectMarks({}, d) && !itemsOf(B).isEmpty(); }, 3500 });
	// 3. cut + paste -> nothing (neither after the wait nor after the paste)
	steps->append({ QStringLiteral("3a cut (wait 3 s)"), [=]() {
		                m_doc->m_Selection->clear();
		                for (PageItem* it : itemsOf(C)) m_doc->m_Selection->addItem(it);
		                m_mw->slotEditCut(); qApp->processEvents(); },
	                [=](QString& d) { return expectMarks({}, d) && itemsOf(C).isEmpty(); }, 3500 });
	steps->append({ QStringLiteral("3b paste back"), [=]() { m_mw->slotEditPaste(); qApp->processEvents(); },
	                [=](QString& d) { return expectMarks({}, d) && !itemsOf(C).isEmpty(); }, 3500 });
	// 5. two stories at once -> one batch with both
	steps->append({ QStringLiteral("5 delete two stories at once"), [=]() { deleteItems(itemsOf(D) + itemsOf(E)); },
	                [=](QString& d) { QStringList got = m_autoMarkLog.mid(*mark); d = QStringLiteral("marks: ") + got.join(" | ") + QStringLiteral("; UI time in the hook + check: %1 ms").arg(hookTime(), 0, 'f', 3); return got.size() == 1 && got[0].contains(D) && got[0].contains(E) && got[0].startsWith(unusedStatus()); }, 3500 });
	// 5c. twenty stories at once (timing): every remaining story but the ones the later steps need
	steps->append({ QStringLiteral("5c delete 20 stories at once (timing)"), [=]() {
		                QList<PageItem*> all;
		                int n = 0;
		                for (const QString& st : stories)
		                {
		                	if (st == F || st == G || st == H || st == B || st == C || st == A || st == D || st == E) continue;
		                	if (itemsOf(st).isEmpty()) continue;
		                	all += itemsOf(st);
		                	if (++n >= 20) break;
		                }
		                deleteItems(all); },
	                [=](QString& d) { QStringList got = m_autoMarkLog.mid(*mark); d = QStringLiteral("marks: %1 batch(es), %2 ids; UI time in the hook + check: %3 ms").arg(got.size()).arg(got.isEmpty() ? 0 : got[0].count(',') + 1).arg(hookTime(), 0, 'f', 3); return got.size() == 1 && got[0].count(',') + 1 == 20; }, 3500 });
	// 5b. delete page: story F moved to a new page 2, the page deleted
	steps->append({ QStringLiteral("5b delete page with a story on it"), [=]() {
		                ScPage* p2 = m_doc->addPage(m_doc->Pages->count());
		                if (!p2) return;
		                const double dx = p2->xOffset() - m_doc->Pages->at(0)->xOffset(), dy = p2->yOffset() - m_doc->Pages->at(0)->yOffset();
		                for (PageItem* it : itemsOf(F)) { it->setXYPos(it->xPos() + dx, it->yPos() + dy); it->OwnPage = p2->pageNr(); }
		                m_mw->deletePage(p2->pageNr() + 1, p2->pageNr() + 1); qApp->processEvents(); },
	                [=](QString& d) { const bool ok = expectMarks({ unusedStatus() + QStringLiteral(" deleted from the page [") + F + QStringLiteral("]") }, d); d += QStringLiteral("; frames of the story left %1, pages %2").arg(itemsOf(F).size()).arg(m_doc->Pages->count()); return ok && itemsOf(F).isEmpty(); }, 3500 });
	// 6. placed twice: a second frame with the same ids; delete the original -> nothing; delete the copy -> UNUSED
	steps->append({ QStringLiteral("6a story placed twice: delete the first placement"), [=]() {
		                const int ni = m_doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, 100, 100, 80, 40, 0, CommonStrings::None, CommonStrings::None);
		                PageItem* copy = m_doc->Items->at(ni);
		                copy->setItemName(QStringLiteral("twice_copy"));
		                PageItem* orig = itemsOf(G).first();
		                setAttr(copy, "news.story", G); setAttr(copy, "news.serverId", attr(orig, "news.serverId")); setAttr(copy, "news.pageId", attr(orig, "news.pageId")); setAttr(copy, "news.role", "text");
		                QList<PageItem*> originals; for (PageItem* it : itemsOf(G)) if (it != copy) originals << it;
		                deleteItems(originals); },
	                [=](QString& d) { return expectMarks({}, d) && itemsOf(G).size() == 1; }, 3500 });
	steps->append({ QStringLiteral("6b delete the second placement too"), [=]() { deleteItems(itemsOf(G)); },
	                [=](QString& d) { return expectMarks({ unusedStatus() + QStringLiteral(" deleted from the page [") + G + QStringLiteral("]") }, d); }, 3500 });
	// 7. pref off -> nothing
	steps->append({ QStringLiteral("7 pref off: delete whole story"), [=]() { QSettings st("Faircode", "ScribusNews"); st.setValue("news/markUnusedOnDelete", false); st.sync(); deleteItems(itemsOf(H)); },
	                [=](QString& d) { QSettings st("Faircode", "ScribusNews"); st.setValue("news/markUnusedOnDelete", true); st.sync(); return expectMarks({}, d) && itemsOf(H).isEmpty(); }, 3500 });

	auto* timer = new QTimer(this);
	timer->setSingleShot(true);
	std::function<void()> next;
	next = [=]() mutable {
		if (*idx > 0)
		{
			const Step& done = (*steps)[*idx - 1];
			QString detail;
			const bool ok = done.check(detail);
			if (!ok) ++*failures;
			line(QStringLiteral("%1 %2: %3").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), done.name, detail));
		}
		if (*idx >= steps->size())
		{
			line(*failures == 0 ? QStringLiteral("RESULT PASS") : QStringLiteral("RESULT FAIL %1").arg(*failures));
			ts->flush();
			out->close();
			m_doc->setModified(false);
			const int f = *failures;
			QTimer::singleShot(500, qApp, [f]() { qApp->exit(f == 0 ? 0 : 1); });
			return;
		}
		*mark = m_autoMarkLog.size();
		const Step& s = (*steps)[*idx];
		++*idx;
		s.act();
		timer->start(s.waitMs);
	};
	connect(timer, &QTimer::timeout, this, [next]() mutable { next(); });
	next();
}

void SuneerNewsPanel::reevaluateAfterUndo()
{
	if (!m_doc || !markUnusedOnDeletePref())
		return;
	// only the tracked stories are looked at (a few ids), each re-synced in the index from the document
	QList<DeletedStory> back, goneAgain;
	for (auto it = m_autoUnused.begin(); it != m_autoUnused.end();)
	{
		indexResync(it.key());
		if (indexCount(it.key()) > 0)
		{
			back << it.value();
			it = m_autoUnused.erase(it);
		}
		else
			++it;
	}
	for (const DeletedStory& d : back)
	{
		m_deleted.insert(d.storyId, d);       // watched again: a redo of the delete takes it away once more
		m_autoRetry.remove(retryKey(d));      // a pending "unused" for it is void now
	}
	if (!back.isEmpty())
		autoMarkStories(back, QStringLiteral("USED"), unusedStatus(), tr("undo"));
	for (auto it = m_deleted.begin(); it != m_deleted.end();)
	{
		indexResync(it.key());
		if (!m_cutStories.contains(it.key()) && indexCount(it.key()) == 0)
		{
			goneAgain << it.value();
			it = m_deleted.erase(it);
		}
		else
			++it;
	}
	if (!goneAgain.isEmpty())
		autoMarkStories(goneAgain, unusedStatus(), QStringLiteral("USED"), tr("redo"));
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
		setStatus(tr("This story carries no server id / page id"));
		return;
	}
	const QString name = attr(item, "news.story");
	if (QMessageBox::question(this, tr("Mark as unused"),
	        tr("Mark \"%1\" as unused on the server?\n(The frames stay in the document.)").arg(name),
	        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	QString err;
	if (markOnServer(QStringList() << sid, pageId, unusedStatus(), &err, QStringLiteral("USED")))
	{
		setRowStatus(sid, unusedStatus());
		setStatus(tr("Marked unused on the server: %1").arg(name));
	}
	else
		setStatus(tr("Error: server refused (%1)").arg(err));
}

void SuneerNewsPanel::balanceStory(PageItem* item)
{
	if (!item)
		return;
	const QString sid = attr(item, "news.serverId");
	const QString pageId = attr(item, "news.pageId");
	if (sid.isEmpty() || pageId.isEmpty())
	{
		setStatus(tr("This story carries no server id / page id"));
		return;
	}
	const QString name = attr(item, "news.story");
	if (QMessageBox::question(this, tr("Mark as balance"),
	        tr("Mark \"%1\" as BALANCE on the server (kept for a later day)?\n(The frames stay in the document.)").arg(name),
	        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	QString err;
	if (markOnServer(QStringList() << sid, pageId, "BALANCED", &err, QStringLiteral("USED")))
	{
		setRowStatus(sid, "BALANCED");
		setStatus(tr("Marked balance on the server: %1").arg(name));
	}
	else
		setStatus(tr("Error: server refused (%1)").arg(err));
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

void SuneerNewsPanel::loginLog(const QString& line)
{
	const QString dir = QDir::homePath() + QStringLiteral("/scribus-crashlogs");
	QDir().mkpath(dir);
	const QString path = dir + QStringLiteral("/login.log");
	if (QFileInfo(path).size() > 1024 * 1024)
	{
		QFile::remove(path + QStringLiteral(".1"));
		QFile::rename(path, path + QStringLiteral(".1"));
	}
	QFile f(path);
	if (!f.open(QIODevice::Append | QIODevice::Text))
		return;
	QTextStream out(&f);
	out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
	    << "  [" << QCoreApplication::applicationPid() << "]  " << line << "\n";
}

// "suneer@example.com" -> "s***@example.com": enough to tell accounts apart in the log.
QString SuneerNewsPanel::maskEmail(const QString& email)
{
	const int at = email.indexOf(QLatin1Char('@'));
	if (at <= 0)
		return email.isEmpty() ? QStringLiteral("(empty)") : email.left(1) + QStringLiteral("***");
	return email.left(1) + QStringLiteral("***") + email.mid(at);
}

// The keyring is used only when it is ready (ScUpdateKeyStore::keyringState):
// a missing default keyring would make the first write pop a hidden "create
// new keyring" prompt and wait for it (chn-dtp26), a locked one an unlock
// prompt. Without it the tokens live in memory for this session only.
void SuneerNewsPanel::ensureSecretsLoaded()
{
	// The check runs a local event loop (at most ~3 s when the keyring hangs):
	// a Login clicked meanwhile goes ahead without it, and its session wins.
	if (m_secretsLoaded || m_secretsLoading)
		return;
	m_secretsLoading = true;
	const int gen = m_sessionGen;
	QString access, refresh, device, user, why;
	int probeMs = 0;
	const ScUpdateKeyStore::KeyringState st = ScUpdateKeyStore::keyringState(3000, &probeMs);
	loginLog(QStringLiteral("keyring check: %1 (%2 ms)").arg(ScUpdateKeyStore::keyringStateText(st)).arg(probeMs));
	bool fromKeyring = false;
	if (st == ScUpdateKeyStore::KeyringState::Ready)
	{
		QElapsedTimer t;
		t.start();
		QString err;
		const bool ok = ScUpdateKeyStore::keyringLoad(kSecretService, kKeyAccess, &access, 3000, &err)
		             && ScUpdateKeyStore::keyringLoad(kSecretService, kKeyRefresh, &refresh, 3000, &err)
		             && ScUpdateKeyStore::keyringLoad(kSecretService, kKeyDevice, &device, 3000, &err)
		             && ScUpdateKeyStore::keyringLoad(kSecretService, kKeyUserName, &user, 3000, &err);
		loginLog(ok ? QStringLiteral("keyring read: ok, saved session %1 (%2 ms)").arg(refresh.isEmpty() ? "no" : "yes").arg(t.elapsed())
		            : QStringLiteral("keyring read failed: %1 (%2 ms)").arg(err).arg(t.elapsed()));
		if (ok)
			fromKeyring = true;
		else
			why = err;
	}
	else
		why = ScUpdateKeyStore::keyringStateText(st);
	if (!fromKeyring)
	{
		// What a build without a keyring left in the prefs file (read only;
		// it is never written again), the device id from the plain settings.
		access  = ScUpdateKeyStore::loadLegacySecret(kSecretService, kKeyAccess);
		refresh = ScUpdateKeyStore::loadLegacySecret(kSecretService, kKeyRefresh);
		user    = ScUpdateKeyStore::loadLegacySecret(kSecretService, kKeyUserName);
		device  = ScUpdateKeyStore::loadLegacySecret(kSecretService, kKeyDevice);
		if (device.isEmpty())
			device = settingsString(QStringLiteral("deviceId"), QString());
		loginLog(QStringLiteral("keyring not used (%1): login kept for this session only; older saved session %2")
		             .arg(why, refresh.isEmpty() ? "none" : "found in the prefs file"));
	}
	m_secretsLoading = false;
	m_secretsLoaded = true;
	if (!fromKeyring)
	{
		m_sessionOnly = true;
		if (m_keyringWhy.isEmpty())
			m_keyringWhy = why;
	}
	if (gen != m_sessionGen)
		loginLog(QStringLiteral("keyring check finished after a login / logout in the meantime: that session is kept"));
	else
	{
		m_accessToken = access;
		m_refreshToken = refresh;
		m_userName = user;
	}
	if (m_deviceId.isEmpty())
		m_deviceId = device;
	if (m_deviceId.isEmpty())
	{
		// One UUID per machine, generated once and kept with the tokens. It is
		// not a secret: without a keyring it goes to ScribusNews.conf.
		m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (m_sessionOnly || !ScUpdateKeyStore::keyringStore(kSecretService, kKeyDevice, m_deviceId, 3000))
			QSettings("Faircode", "ScribusNews").setValue("deviceId", m_deviceId);
	}
}

void SuneerNewsPanel::saveSession()
{
	if (!m_sessionOnly)
	{
		QElapsedTimer t;
		t.start();
		QString err;
		auto put = [&err](const QString& key, const QString& value) {
			return value.isEmpty() ? ScUpdateKeyStore::keyringClear(kSecretService, key, 4000, &err)
			                       : ScUpdateKeyStore::keyringStore(kSecretService, key, value, 4000, &err);
		};
		const bool ok = put(kKeyAccess, m_accessToken) && put(kKeyRefresh, m_refreshToken) && put(kKeyUserName, m_userName);
		loginLog(ok ? QStringLiteral("keyring write: ok (%1 ms)").arg(t.elapsed())
		            : QStringLiteral("keyring write failed: %1 (%2 ms) - this session only from now on").arg(err).arg(t.elapsed()));
		if (ok)
			return;
		m_sessionOnly = true;
		m_keyringWhy = err;
	}
	// Session only: nothing goes to disk, and an older prefs-file copy is
	// stale from now on (the server rotates the refresh token).
	ScUpdateKeyStore::clearLegacySecret(kSecretService, kKeyAccess);
	ScUpdateKeyStore::clearLegacySecret(kSecretService, kKeyRefresh);
	ScUpdateKeyStore::clearLegacySecret(kSecretService, kKeyUserName);
	if (!m_accessToken.isEmpty() || !m_refreshToken.isEmpty())
		showSessionOnlyNotice();
}

void SuneerNewsPanel::showSessionOnlyNotice()
{
	if (m_sessionOnlyNoticeShown)
		return;
	m_sessionOnlyNoticeShown = true;
	m_sessionOnlyNotice = tr("Login saved for this session only - keyring not available (%1).\n"
	                         "You will be asked to log in again after Scribus restarts.").arg(m_keyringWhy);
	loginLog(QStringLiteral("notice shown: login saved for this session only"));
	setStatus(tr("Login saved for this session only - keyring not available"));
	auto* box = new QMessageBox(QMessageBox::Information, tr("News Browser"), m_sessionOnlyNotice, QMessageBox::Ok, this);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setModal(false);
	box->show();
	if (m_loginSelfTest)
	{
		if (qEnvironmentVariableIsSet("SUNEER_LOGIN_SELFTEST_SHOT"))
			QTimer::singleShot(200, box, [box]() { box->grab().save(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST_SHOT") + "-notice.png"); });
		QTimer::singleShot(500, box, &QMessageBox::close);
	}
}

void SuneerNewsPanel::clearSession()
{
	++m_sessionGen;
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
	if (m_refreshBtn)
		m_refreshBtn->setEnabled(!locked);
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
	if (!user["id"].toString().isEmpty())
		m_userId = user["id"].toString();
	// user.permissions {name: granted} (login reply and /users/me)
	if (user["permissions"].isObject())
	{
		m_permissions.clear();
		const QJsonObject perms = user["permissions"].toObject();
		for (auto it = perms.constBegin(); it != perms.constEnd(); ++it)
			if (it.value().toBool())
				m_permissions.insert(it.key());
	}
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
		// The user's edition GROUP id is what /news/layout/pages wants.
		const QString gid = eg["id"].toString();
		if (!gid.isEmpty())
		{
			m_editionGroupIds[id] = gid;
			if (!ed["shortName"].toString().isEmpty())
				m_editionGroupIds[ed["shortName"].toString()] = gid;
			if (!ed["name"].toString().isEmpty())
				m_editionGroupIds[ed["name"].toString()] = gid;
		}
	}
	m_userInfoLoaded = true;
}

bool SuneerNewsPanel::fetchCurrentUser()
{
	bool ok = false;
	apiGet(QUrl(apiUrl("/users/me")), [this, &ok](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError)
		{
			setStatus(tr("Error: ") + tr("User profile: %1").arg(describeError(reply)));
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
		setStatus(tr("Fixture mode: Fetch News reads the file"));
		return;
	}
	const int gen = m_sessionGen;
	ensureSecretsLoaded();
	if (gen != m_sessionGen)
	{
		// The user logged in while the keyring was checked: nothing to restore.
		m_startupDone = true;
		return;
	}
	if (m_refreshToken.isEmpty())
	{
		m_startupDone = true;
		loginLog(QStringLiteral("startup: no saved session, panel locked"));
		m_accessToken.clear();
		setLocked(true);
		return;
	}
	m_quietAuth = true;
	const bool ok = refreshAccessToken();
	m_quietAuth = false;
	if (gen != m_sessionGen)
	{
		m_startupDone = true;   // a login while the refresh ran: keep it
		return;
	}
	if (!ok)
	{
		m_startupDone = true;
		loginLog(QStringLiteral("startup: saved session refused or server not reachable, panel locked"));
		clearSession();
		setLocked(true);
		m_lockLabel->setText(tr("Saved session is no longer valid - please log in."));
		return;
	}
	m_quietAuth = true;
	fetchCurrentUser();
	m_quietAuth = false;
	m_startupDone = true;
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
	QElapsedTimer t;
	t.start();
	const int gen = m_sessionGen;
	QNetworkReply* reply = postBlocking(req, QByteArray("{}"));
	const int status = httpStatus(reply);
	loginLog(QStringLiteral("token refresh: %1 after %2 ms")
	             .arg(status > 0 ? QStringLiteral("HTTP %1").arg(status) : QStringLiteral("network error (%1)").arg(reply->errorString()))
	             .arg(t.elapsed()));
	if (gen != m_sessionGen)
	{
		loginLog(QStringLiteral("token refresh ignored: a login / logout happened while it ran"));
		return isLoggedIn();
	}
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

namespace {
// The whole login request; Qt's own transfer timeout is a little longer so
// the dialog's guard always decides and gives its own message.
const int kLoginTimeoutMs = 10000;
}

QNetworkReply* SuneerNewsPanel::startLoginRequest(const QString& email, const QString& password)
{
	ensureSecretsLoaded();
	if (m_deviceId.isEmpty())
	{
		// Login while the startup keyring check still runs: the device id
		// from the plain settings (it is not a secret).
		m_deviceId = settingsString(QStringLiteral("deviceId"), QString());
		if (m_deviceId.isEmpty())
		{
			m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			QSettings("Faircode", "ScribusNews").setValue("deviceId", m_deviceId);
		}
	}
	QJsonObject body;
	body["email"]    = email;
	body["password"] = password;
	body["deviceId"] = m_deviceId;
	QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
	body.remove("password");
	QNetworkRequest req = apiRequest(QUrl(apiUrl("/users/auth/login")), false);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setTransferTimeout(kLoginTimeoutMs + 2000);
	loginLog(QStringLiteral("login request: POST %1/users/auth/login as %2").arg(QUrl(m_apiHost).authority(), maskEmail(email)));
	QNetworkReply* reply = m_nam->post(req, bytes);
	bytes.fill('\0');   // the password is not kept around
	return reply;
}

QString SuneerNewsPanel::loginErrorText(QNetworkReply* reply, bool timedOut) const
{
	const QString host = QUrl(m_apiHost).authority();
	if (timedOut)
		return tr("No answer from the server %1 within %2 seconds.\nCheck the network and the server address (Settings), then try again.")
		    .arg(host).arg(kLoginTimeoutMs / 1000);
	const int status = httpStatus(reply);
	const QString msg = serverMessage(reply->peek(reply->bytesAvailable()));
	switch (reply->error())
	{
		case QNetworkReply::NoError:
			break;
		case QNetworkReply::ConnectionRefusedError:
			return tr("The server %1 refused the connection: the News server is not running at that address.\nCheck the server address (Settings).").arg(host);
		case QNetworkReply::HostNotFoundError:
			return tr("Server name %1 not found.\nCheck the server address (Settings).").arg(host);
		case QNetworkReply::TimeoutError:
		case QNetworkReply::OperationCanceledError:
			return tr("No answer from the server %1 within %2 seconds.\nCheck the network and the server address (Settings), then try again.")
			    .arg(host).arg(kLoginTimeoutMs / 1000);
		case QNetworkReply::RemoteHostClosedError:
		case QNetworkReply::NetworkSessionFailedError:
		case QNetworkReply::UnknownNetworkError:
		case QNetworkReply::TemporaryNetworkFailureError:
			return tr("Cannot reach the server %1: %2").arg(host, reply->errorString());
		default:
			if (status == 0)
				return tr("Cannot reach the server %1: %2").arg(host, reply->errorString());
			break;
	}
	if (status == 400 || status == 401 || status == 403)
		return tr("%1 (HTTP %2)").arg(msg.isEmpty() ? tr("Wrong email or password") : msg).arg(status);
	if (status == 429)
		return tr("Too many login attempts - wait a few minutes and try again (HTTP 429)%1").arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
	if (status == 404)
		return tr("The login address was not found on %1 (HTTP 404).\nCheck the server address (Settings).").arg(host);
	if (status >= 500)
		return tr("Server error (HTTP %1)%2").arg(status).arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
	if (status != 200)
		return tr("Unexpected answer from the server (HTTP %1)%2").arg(status).arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
	return QString();
}

bool SuneerNewsPanel::finishLogin(QNetworkReply* reply, const QString& email, bool timedOut, qint64 ms, QString& error)
{
	const int status = httpStatus(reply);
	loginLog(QStringLiteral("login reply: %1 after %2 ms")
	             .arg(timedOut ? QStringLiteral("no answer (timed out)")
	                           : status > 0 ? QStringLiteral("HTTP %1").arg(status)
	                                        : QStringLiteral("network error %1 (%2)").arg(int(reply->error())).arg(reply->errorString()))
	             .arg(ms));
	if (timedOut || reply->error() != QNetworkReply::NoError || status != 200)
	{
		// The server says why: wrong password, disabled account, too many
		// attempts ... Show that; otherwise the transport problem.
		error = loginErrorText(reply, timedOut);
		loginLog(QStringLiteral("login failed: %1").arg(QString(error).replace('\n', ' ')));
		return false;
	}
	captureRefreshCookie(reply);
	const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
	m_accessToken = data["accessToken"].toString();
	if (m_accessToken.isEmpty())
	{
		error = tr("Login reply carried no access token");
		loginLog(QStringLiteral("login failed: reply without an access token"));
		return false;
	}
	m_userName.clear();
	parseUser(data["user"].toObject());
	if (m_userName.isEmpty())
		m_userName = email;
	++m_sessionGen;
	loginLog(QStringLiteral("login ok: refresh cookie %1").arg(m_refreshToken.isEmpty() ? "missing" : "received"));
	saveSession();
	return true;
}

// The request runs asynchronously while the dialog stays usable: Cancel is
// always enabled and aborts it, nothing waits longer than kLoginTimeoutMs,
// and a keyring problem can no longer hold the dialog (saveSession() only
// uses a ready keyring, with a timeout).
bool SuneerNewsPanel::loginDialog()
{
	if (m_loginDialogOpen)
		return false;
	m_loginDialogOpen = true;
	loginLog(QStringLiteral("login dialog opened, server %1").arg(QUrl(m_apiHost).authority()));
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
	err->setWordWrap(true);
	fl->addRow(info);
	fl->addRow(tr("Email:"), emailEdit);
	fl->addRow(tr("Password:"), passEdit);
	fl->addRow(err);
	QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	QPushButton* loginBtn = bb->button(QDialogButtonBox::Ok);
	QPushButton* cancelBtn = bb->button(QDialogButtonBox::Cancel);
	loginBtn->setText(tr("Login"));
	cancelBtn->setText(tr("Cancel"));
	fl->addRow(bb);

	QPointer<QNetworkReply> pending;
	bool timedOut = false;
	bool cancelled = false;
	QString email;
	QElapsedTimer clock;
	QTimer guard;
	guard.setTimerType(Qt::PreciseTimer);
	guard.setSingleShot(true);
	guard.setInterval(kLoginTimeoutMs);
	auto setBusy = [&](bool busy) {
		emailEdit->setEnabled(!busy);
		passEdit->setEnabled(!busy);
		loginBtn->setEnabled(!busy);
		cancelBtn->setEnabled(true);   // always: it aborts a running request
	};
	auto showError = [&](const QString& text) {
		err->setStyleSheet("color:#B00020;");
		err->setText(text);
		m_loginSelfTestError = text;
		dlg.adjustSize();   // a two- or three-line message must not be cut off
	};
	connect(&guard, &QTimer::timeout, &dlg, [&]() {
		if (!pending)
			return;
		timedOut = true;
		pending->abort();
	});
	// Each click is one deliberate attempt; nothing is retried on its own
	// (repeated failures lock the account server-side).
	connect(bb, &QDialogButtonBox::accepted, &dlg, [&]() {
		if (pending)
			return;
		email = emailEdit->text().trimmed();
		if (email.isEmpty() || passEdit->text().isEmpty())
		{
			showError(tr("Enter email and password"));
			return;
		}
		timedOut = false;
		err->setStyleSheet("color:#666;");
		err->setText(tr("Connecting to %1 ...").arg(QUrl(m_apiHost).authority()));
		setBusy(true);
		clock.start();
		QNetworkReply* reply = startLoginRequest(email, passEdit->text());
		passEdit->clear();
		pending = reply;
		guard.start();
		connect(reply, &QNetworkReply::finished, &dlg, [&, reply]() {
			guard.stop();
			pending = nullptr;
			const qint64 ms = clock.elapsed();
			m_loginSelfTestRequestMs = ms;
			if (cancelled)
			{
				reply->deleteLater();
				return;
			}
			QString e;
			const bool ok = finishLogin(reply, email, timedOut, ms, e);
			reply->deleteLater();
			if (ok)
			{
				QSettings s("Faircode", "ScribusNews");
				s.setValue("loginEmail", email);
				dlg.accept();
				return;
			}
			setBusy(false);
			showError(e);
			passEdit->setFocus();
			if (m_loginSelfTest && qEnvironmentVariableIsSet("SUNEER_LOGIN_SELFTEST_SHOT"))
				QTimer::singleShot(100, &dlg, [&]() { dlg.grab().save(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST_SHOT") + "-error.png"); });
		});
	});
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	// Cancel, Esc and the window's close button all end here.
	connect(&dlg, &QDialog::rejected, &dlg, [&]() {
		if (pending)
		{
			cancelled = true;
			loginLog(QStringLiteral("login cancelled by the user after %1 ms; request aborted").arg(clock.elapsed()));
			pending->abort();
		}
	});
	if (m_loginSelfTest)
	{
		// The self-test fills the dialog, clicks Login (and Cancel when asked)
		// and watches the buttons while the request runs.
		QTimer::singleShot(300, &dlg, [&]() {
			emailEdit->setText(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST_EMAIL", "test@example.com"));
			passEdit->setText(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST_PASS", "right"));
			loginBtn->click();
			const int cancelMs = qEnvironmentVariableIntValue("SUNEER_LOGIN_SELFTEST_CANCEL_MS");
			if (cancelMs > 0)
				QTimer::singleShot(cancelMs, &dlg, [&]() {
					QElapsedTimer c;
					c.start();
					cancelBtn->click();
					m_loginSelfTestCancelCloseMs = dlg.isVisible() ? -1 : c.elapsed();
				});
		});
		auto* tick = new QTimer(&dlg);
		tick->setInterval(100);
		connect(tick, &QTimer::timeout, &dlg, [&]() {
			if (!pending)
				return;
			++m_loginSelfTestTicks;
			if (m_loginSelfTestTicks == 15 && qEnvironmentVariableIsSet("SUNEER_LOGIN_SELFTEST_SHOT"))
				dlg.grab().save(qEnvironmentVariable("SUNEER_LOGIN_SELFTEST_SHOT") + "-busy.png");
			if (loginBtn->isEnabled())
				m_loginSelfTestLoginEnabledBusy = true;
			if (!cancelBtn->isEnabled())
				m_loginSelfTestCancelDisabledBusy = true;
		});
		tick->start();
		QTimer::singleShot(30000, &dlg, &QDialog::reject);   // never hang the test
	}
	(emailEdit->text().isEmpty() ? emailEdit : passEdit)->setFocus();
	const bool ok = (dlg.exec() == QDialog::Accepted);
	if (pending)
	{
		cancelled = true;
		pending->abort();
	}
	m_loginDialogOpen = false;
	loginLog(ok ? QStringLiteral("login done, logged in as %1").arg(maskEmail(email))
	            : QStringLiteral("login dialog closed, not logged in"));
	return ok;
}

void SuneerNewsPanel::onLogin()
{
	ensureSecretsLoaded();
	if (!loginDialog())
		return;
	setLocked(false);
	setStatus(tr("Logged in as %1").arg(m_userName));
	onFetchStatic();
}

void SuneerNewsPanel::onLogout()
{
	ensureSecretsLoaded();
	loginLog(QStringLiteral("logout"));
	releasePageLock(true, QStringLiteral("logout"));
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

// SUNEER_LOGIN_SELFTEST=<report>: one login through the real dialog with
// SUNEER_LOGIN_SELFTEST_EMAIL / _PASS (and _CANCEL_MS: click Cancel after
// that many ms), then the report and quit. tests/suneer/news_login.sh runs it
// against a mock server in each keyring state; secrets are never written to
// the report, only whether they are present.
void SuneerNewsPanel::runLoginSelfTest(const QString& report)
{
	m_loginSelfTest = true;
	ensureSecretsLoaded();
	const ScUpdateKeyStore::KeyringState st = ScUpdateKeyStore::keyringState();
	QElapsedTimer t;
	t.start();
	onLogin();
	const qint64 dialogMs = t.elapsed();
	// A login during the startup check: the check finishes afterwards (its
	// event loop is below this call, so wait from a timer) and must not lock
	// the panel again.
	auto* wait = new QTimer(this);
	wait->setInterval(200);
	auto waited = std::make_shared<QElapsedTimer>();
	waited->start();
	connect(wait, &QTimer::timeout, this, [this, wait, waited, report, st, dialogMs]() {
		if (!m_startupDone && waited->elapsed() < 15000)
			return;
		wait->stop();
		wait->deleteLater();
		writeLoginSelfTestReport(report, int(st), dialogMs);
	});
	wait->start();
}

void SuneerNewsPanel::writeLoginSelfTestReport(const QString& report, int keyringState, qint64 dialogMs)
{
	const auto st = ScUpdateKeyStore::KeyringState(keyringState);
	QStringList lines;
	lines << QStringLiteral("keyring: %1").arg(ScUpdateKeyStore::keyringStateText(st))
	      << QStringLiteral("sessionOnly: %1").arg(m_sessionOnly ? "yes" : "no")
	      << QStringLiteral("loggedIn: %1").arg(isLoggedIn() ? "yes" : "no")
	      << QStringLiteral("user: %1").arg(m_userName)
	      << QStringLiteral("error: %1").arg(QString(m_loginSelfTestError).replace('\n', ' '))
	      << QStringLiteral("requestMs: %1").arg(m_loginSelfTestRequestMs)
	      << QStringLiteral("ticksWhileBusy: %1").arg(m_loginSelfTestTicks)
	      << QStringLiteral("loginEnabledWhileBusy: %1").arg(m_loginSelfTestLoginEnabledBusy ? "yes" : "no")
	      << QStringLiteral("cancelDisabledWhileBusy: %1").arg(m_loginSelfTestCancelDisabledBusy ? "yes" : "no")
	      << QStringLiteral("cancelCloseMs: %1").arg(m_loginSelfTestCancelCloseMs)
	      << QStringLiteral("dialogMs: %1").arg(dialogMs)
	      << QStringLiteral("notice: %1").arg(QString(m_sessionOnlyNotice).replace('\n', ' '))
	      << QStringLiteral("startupDone: %1").arg(m_startupDone ? "yes" : "no")
	      << QStringLiteral("panelUnlocked: %1").arg(m_stack->currentIndex() == 1 ? "yes" : "no");
	if (st == ScUpdateKeyStore::KeyringState::Ready)
	{
		QString v;
		const bool read = ScUpdateKeyStore::keyringLoad(kSecretService, kKeyRefresh, &v, 3000);
		lines << QStringLiteral("keyringHasRefreshToken: %1").arg(!read ? "read failed" : v.isEmpty() ? "no" : "yes");
	}
	lines << QStringLiteral("prefsFileHasRefreshToken: %1").arg(ScUpdateKeyStore::loadLegacySecret(kSecretService, kKeyRefresh).isEmpty() ? "no" : "yes");
	QFile f(report);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		f.write(lines.join('\n').toUtf8() + "\n");
	f.close();
	fprintf(stderr, "LOGIN-SELFTEST done: %s\n", qPrintable(report));
	QTimer::singleShot(200, qApp, []() { QApplication::exit(0); });
}


// ============================================================================
// Page lock: "Page 2 is opened by …" like the workflow's Layout page
// ============================================================================
//
// The workflow keeps one LayoutPageStatus row per page and publish date;
// lockedById / lockedAt say who has the page open in the Layout page. The web
// app reads it with GET /news/layout/pages?publishDate&editionGroup (status
// .lockedBy.username) and sets it with PATCH /news/layout/pages/lock
// {pageIds, publishDate}: that list is the user's WHOLE set of locked pages
// for the date - pages missing from it are unlocked, and a page another user
// holds is refused (HTTP 400). So the set is always rebuilt here from the
// user's current locks over all their editions before one page is added or
// removed. There is no expiry and no heartbeat on the server: a lock lasts
// until it is released (or an admin unlocks it in Locked Pages).


QString SuneerNewsPanel::pageNameAt(int index) const
{
	if (index < 0 || index >= m_pageCombo->count())
		return QString();
	const QString name = m_pageCombo->itemData(index, kPageNameRole).toString();
	return name.isEmpty() ? m_pageCombo->itemText(index) : name;
}

QString SuneerNewsPanel::currentPageKey() const
{
	const QString id = m_pageCombo->currentData().toString();
	return id.isEmpty() ? QString() : id + QLatin1Char('|') + m_dateEdit->date().toString("yyyy-MM-dd");
}

QString SuneerNewsPanel::formatLockSince(const QDateTime& since)
{
	if (!since.isValid())
		return QString();
	const QDateTime local = since.toLocalTime();
	return local.date() == QDate::currentDate() ? local.toString("HH:mm") : local.toString("d MMM HH:mm");
}

// One asynchronous request with the login token and a short timeout. A 401
// gets one token refresh and one retry; nothing here ever waits on its own.
void SuneerNewsPanel::lockCall(const QByteArray& verb, const QUrl& url, const QByteArray& body,
                               std::function<void(QNetworkReply*)> done, bool retried)
{
	QNetworkRequest req = apiRequest(url, true);
	req.setTransferTimeout(kLockTimeoutMs);
	if (!body.isEmpty())
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	QNetworkReply* reply = m_nam->sendCustomRequest(req, verb, body);
	auto t = std::make_shared<QElapsedTimer>();
	t->start();
	connect(reply, &QNetworkReply::finished, this, [this, reply, verb, url, body, done, retried, t]() {
		reply->deleteLater();
		const int status = httpStatus(reply);
		apiLog(QStringLiteral("page lock: %1 %2 -> %3 (%4 ms)").arg(QString::fromLatin1(verb), url.path(),
		       status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString()).arg(t->elapsed()));
		if ((status == 401 || status == 403) && !retried && !m_refreshToken.isEmpty())
		{
			m_quietAuth = true;
			const bool ok = refreshAccessToken();
			m_quietAuth = false;
			if (ok)
			{
				lockCall(verb, url, body, done, true);
				return;
			}
		}
		if (done)
			done(reply);
	});
}

void SuneerNewsPanel::refreshPageLocks(std::function<void(bool, const QString&)> done)
{
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	const QString gid = editionGroupIdFor(m_lastEdition);
	if (m_accessToken.isEmpty() || gid.isEmpty() || m_pageCombo->count() == 0)
	{
		if (done)
			done(false, m_accessToken.isEmpty() ? tr("not logged in") : tr("no edition group for %1").arg(m_lastEdition));
		return;
	}
	QUrl u(apiUrl("/news/layout/pages"));
	QUrlQuery q;
	q.addQueryItem("publishDate", date);
	q.addQueryItem("editionGroup", gid);
	u.setQuery(q);
	const QString edition = m_lastEdition;
	lockCall("GET", u, QByteArray(), [this, date, edition, done](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError || httpStatus(reply) != 200)
		{
			const QString err = describeError(reply);
			if (done)
				done(false, err);
			return;
		}
		// The edition or date changed while the answer travelled: it is not about the Page box any more.
		if (edition != m_lastEdition || date != m_dateEdit->date().toString("yyyy-MM-dd"))
		{
			if (done)
				done(false, tr("selection changed"));
			return;
		}
		const QJsonValue d = QJsonDocument::fromJson(reply->readAll()).object()["data"];
		const QJsonArray pages = d.isArray() ? d.toArray() : d.toObject()["pages"].toArray();
		m_pageLocks.clear();
		for (const QJsonValue& v : pages)
		{
			const QJsonObject pg = v.toObject();
			const QJsonObject st = pg["status"].toObject();
			if (st.isEmpty() || st["lockedAt"].isNull() || st["lockedAt"].toString().isEmpty() || st["lockedById"].toString().isEmpty())
				continue;
			PageLockInfo info;
			info.userId = st["lockedById"].toString();
			info.userName = st["lockedBy"].toObject()["username"].toString();
			if (info.userName.isEmpty())
				info.userName = tr("another user");
			info.since = QDateTime::fromString(st["lockedAt"].toString(), Qt::ISODateWithMs);
			info.completed = st["isCompleted"].toBool();
			// a workflow with the lock heartbeat (spec 2026-10-30) also says when the holder was last seen, and where
			if (!st["lastSeenAt"].toString().isEmpty())
				info.lastSeen = QDateTime::fromString(st["lastSeenAt"].toString(), Qt::ISODateWithMs);
			info.client = st["lockClient"].toString();
			m_pageLocks.insert(pg["id"].toString(), info);
		}
		m_pageLocksDate = date;
		m_pageLocksKnown = true;
		applyPageLockLabels();
		showPageLockLine();
		if (done)
			done(true, QString());
	});
}

// "Page 2 – Suneer" in grey for pages other users hold; the plain name otherwise.
void SuneerNewsPanel::applyPageLockLabels()
{
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	const bool known = m_pageLocksKnown && m_pageLocksDate == date;
	for (int i = 0; i < m_pageCombo->count(); ++i)
	{
		const QString id = m_pageCombo->itemData(i).toString();
		const QString name = pageNameAt(i);
		const auto it = known ? m_pageLocks.constFind(id) : m_pageLocks.constEnd();
		const bool other = known && it != m_pageLocks.constEnd() && it->userId != m_userId;
		m_pageCombo->setItemText(i, other ? QStringLiteral("%1 – %2").arg(name, it->userName) : name);
		m_pageCombo->setItemData(i, other ? QVariant(QColor("#8A8A8A")) : QVariant(), Qt::ForegroundRole);
		m_pageCombo->setItemData(i, other ? QVariant(tr("Opened by %1 since %2").arg(it->userName, formatLockSince(it->since))) : QVariant(), Qt::ToolTipRole);
	}
}

// The line under the Page box for the selected page + date.
void SuneerNewsPanel::showPageLockLine()
{
	if (!m_pageLockLabel)
		return;
	const int idx = m_pageCombo->currentIndex();
	const QString id = m_pageCombo->currentData().toString();
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	if (idx < 0 || id.isEmpty())
	{
		m_pageLockLabel->hide();
		return;
	}
	const QString name = pageNameAt(idx);
	QString text, style;
	if (m_takeOverBtn)
		m_takeOverBtn->hide();
	if (m_heldPageId == id && m_heldDate == date)
	{
		text = tr("%1 is opened by you (the workflow shows it locked for others)").arg(name);
		style = "color:#2E7D32;";
	}
	else if (m_pageLocksKnown && m_pageLocksDate == date && m_pageLocks.contains(id))
	{
		const PageLockInfo info = m_pageLocks.value(id);
		if (info.userId == m_userId)
		{
			text = tr("%1 is opened by you since %2").arg(name, formatLockSince(info.since));
			style = "color:#2E7D32;";
		}
		else
		{
			text = tr("\U0001F512 %1 is opened by %2 (since %3)").arg(name, info.userName, formatLockSince(info.since));
			if (!info.client.isEmpty())
				text += tr(" on %1").arg(info.client);
			if (lockIsStale(info))
				text += tr(" - no sign of life for %n min", "", int(info.lastSeen.secsTo(QDateTime::currentDateTimeUtc()) / 60));
			if (m_lockOverride.contains(id + QLatin1Char('|') + date))
				text += tr(" - you continued anyway: used / unused not synced");
			style = "color:#B00020; font-weight:bold;";
			QString why;
			const bool can = canTakeOver(info, &why);
			if (m_takeOverBtn)
			{
				m_takeOverBtn->setText(lockIsStale(info) ? tr("Take over page (stale lock)") : tr("Take over page"));
				m_takeOverBtn->setToolTip(can ? tr("%1 loses the lock of %2; it becomes yours, and used / unused of the page is synced").arg(info.userName, name) : why);
				m_takeOverBtn->setVisible(can);
			}
		}
	}
	else
	{
		m_pageLockLabel->hide();
		return;
	}
	m_pageLockLabel->setText(text);
	m_pageLockLabel->setStyleSheet(style + " font-size:11px;");
	m_pageLockLabel->show();
}

void SuneerNewsPanel::syncMyPageLocks(const QString& date, const QString& add, const QString& remove,
                                      std::function<void(bool, const QSet<QString>&, const QString&)> done)
{
	m_lockQueue << [this, date, add, remove, done]() {
		syncMyPageLocksNow(date, add, remove, [this, done](bool ok, const QSet<QString>& mine, const QString& err) {
			done(ok, mine, err);
			if (!m_lockQueue.isEmpty())
				m_lockQueue.takeFirst()();   // still busy: the next one runs
			else
				m_lockQueueBusy = false;
		});
	};
	if (!m_lockQueueBusy)
	{
		m_lockQueueBusy = true;
		m_lockQueue.takeFirst()();
	}
}

void SuneerNewsPanel::syncMyPageLocksNow(const QString& date, const QString& add, const QString& remove,
                                         std::function<void(bool, const QSet<QString>&, const QString&)> done)
{
	// Every edition group of the user: the PATCH list must hold the user's
	// locks in all editions for this date, or the server unlocks the others.
	QSet<QString> groups;
	for (auto it = m_editionGroupIds.constBegin(); it != m_editionGroupIds.constEnd(); ++it)
		if (!it.value().isEmpty())
			groups.insert(it.value());
	if (groups.isEmpty() || m_userId.isEmpty())
	{
		done(false, {}, m_userId.isEmpty() ? tr("the login did not say who you are") : tr("no edition groups"));
		return;
	}
	struct State { int pending; QSet<QString> mine; QString error; };
	auto st = std::make_shared<State>();
	st->pending = groups.size();
	for (const QString& gid : groups)
	{
		QUrl u(apiUrl("/news/layout/pages"));
		QUrlQuery q;
		q.addQueryItem("publishDate", date);
		q.addQueryItem("editionGroup", gid);
		u.setQuery(q);
		lockCall("GET", u, QByteArray(), [this, st, date, add, remove, done](QNetworkReply* reply) {
			if (reply->error() != QNetworkReply::NoError || httpStatus(reply) != 200)
				st->error = describeError(reply);
			else
			{
				const QJsonValue d = QJsonDocument::fromJson(reply->readAll()).object()["data"];
				for (const QJsonValue& v : d.isArray() ? d.toArray() : d.toObject()["pages"].toArray())
				{
					const QJsonObject s = v.toObject()["status"].toObject();
					if (s["lockedById"].toString() == m_userId && !s["lockedAt"].isNull() && !s["lockedAt"].toString().isEmpty())
						st->mine.insert(v.toObject()["id"].toString());
				}
			}
			if (--st->pending > 0)
				return;
			if (!st->error.isEmpty())
			{
				// Without the full list a PATCH could unlock the user's other pages: send nothing.
				done(false, {}, st->error);
				return;
			}
			QSet<QString> want = st->mine;
			if (!add.isEmpty())
				want.insert(add);
			if (!remove.isEmpty())
				want.remove(remove);
			if (want == st->mine)
			{
				done(true, st->mine, QString());
				return;
			}
			QJsonObject body;
			QJsonArray ids;
			for (const QString& id : want)
				ids << id;
			body["pageIds"] = ids;
			body["publishDate"] = date;
			lockCall("PATCH", QUrl(apiUrl("/news/layout/pages/lock")), QJsonDocument(body).toJson(QJsonDocument::Compact),
			         [this, done](QNetworkReply* r) {
				if (r->error() != QNetworkReply::NoError || httpStatus(r) != 200)
				{
					const QString msg = serverMessage(r->peek(r->bytesAvailable()));
					done(false, {}, msg.isEmpty() ? describeError(r) : tr("%1 (HTTP %2)").arg(msg).arg(httpStatus(r)));
					return;
				}
				QSet<QString> now;
				for (const QJsonValue& v : QJsonDocument::fromJson(r->readAll()).object()["data"].toObject()["lockedPages"].toArray())
				{
					const QJsonObject o = v.toObject();
					now.insert(o["pageId"].toString().isEmpty() ? o["page"].toObject()["id"].toString() : o["pageId"].toString());
				}
				done(true, now, QString());
			});
		});
	}
}

void SuneerNewsPanel::onPageSelected(int index)
{
	if (index < 0 || m_pageLockAsking)
		return;
	checkAndTakePageLock();
}

// The lock is per page AND date: a new date is a new page to take (the old
// date's lock goes once the new choice stands).
void SuneerNewsPanel::onLockDateChanged()
{
	m_pageLocksKnown = false;
	applyPageLockLabels();
	showPageLockLine();
	if (m_pageCombo->currentIndex() < 0)
		return;
	if (!m_heldPageId.isEmpty() || m_pageCombo->currentIndex() == m_prevPageIndex)
		checkAndTakePageLock();
	else
		refreshPageLocks();
}

// The one flow behind choosing a page and Fetch News: ask the server (async,
// 5 s); another user's page -> the line + the question; a free page (or one
// already this user's) -> take it. A network problem never stops the work.
void SuneerNewsPanel::checkAndTakePageLock(std::function<void()> then)
{
	const int idx = m_pageCombo->currentIndex();
	const QString id = m_pageCombo->currentData().toString();
	const QString name = pageNameAt(idx);
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	if (idx < 0 || id.isEmpty() || m_accessToken.isEmpty() || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
	{
		m_lockGateKey = currentPageKey();
		if (then)
			then();
		return;
	}
	const int gen = ++m_lockCheckGen;
	// The page held so far is let go only once the new choice stands
	// ("Choose another page" goes back to it, still locked).
	auto releaseOld = [this, id, date]() {
		if (!m_heldPageId.isEmpty() && (m_heldPageId != id || m_heldDate != date))
			releasePageLock(false, QStringLiteral("another page or date chosen"));
	};
	refreshPageLocks([this, gen, idx, id, name, date, then, releaseOld](bool ok, const QString& err) {
		if (gen != m_lockCheckGen)
			return;   // a newer choice is being checked
		const QString key = id + QLatin1Char('|') + date;
		if (!ok)
		{
			m_pageLockLabel->setText(tr("Lock status of %1 unknown (%2) - not locked").arg(name, err));
			m_pageLockLabel->setStyleSheet("color:#8A6D00; font-size:11px;");
			m_pageLockLabel->show();
			apiLog(QStringLiteral("page lock: check of %1 %2 failed (%3) - going on without a lock").arg(name, date, err));
			releaseOld();
			m_prevPageIndex = idx;
			m_lockGateKey = key;
			if (then)
				then();
			return;
		}
		const auto it = m_pageLocks.constFind(id);
		if (it != m_pageLocks.constEnd() && it->userId != m_userId && !m_lockOverride.contains(key))
		{
			const PageLockInfo info = *it;
			if (!askLockedPage(name, info))
			{
				// Choose another page: back to the previous one, list open.
				apiLog(QStringLiteral("page lock: %1 %2 is opened by %3 - the user chose another page").arg(name, date, info.userName));
				m_lockGateKey.clear();
				m_pageCombo->blockSignals(true);
				m_pageCombo->setCurrentIndex(m_prevPageIndex >= 0 && m_prevPageIndex != idx ? m_prevPageIndex : -1);
				m_pageCombo->blockSignals(false);
				// The held page's date may differ from the date box only when the
				// date was changed: then the old page is not the user's choice any more.
				if (!m_heldPageId.isEmpty() && m_heldDate != date)
					releasePageLock(false, QStringLiteral("date changed"));
				showPageLockLine();
				if (qEnvironmentVariableIsEmpty("SUNEER_PAGELOCK_SELFTEST"))
					QTimer::singleShot(0, m_pageCombo, [this]() { if (m_pageCombo->isVisible()) m_pageCombo->showPopup(); });
				return;
			}
			apiLog(QStringLiteral("page lock: %1 %2 is opened by %3 - the user continued anyway").arg(name, date, info.userName));
			releaseOld();
			m_lockOverride.insert(key);
			showPageLockLine();
		}
		else if (it == m_pageLocks.constEnd() || it->userId == m_userId)
		{
			releaseOld();
			takePageLock(id, name, date);
		}
		m_prevPageIndex = idx;
		m_lockGateKey = key;
		if (then)
			then();
	});
}

bool SuneerNewsPanel::askLockedPage(const QString& pageName, const PageLockInfo& info)
{
	m_pageLockAsking = true;
	QMessageBox box(QMessageBox::Warning, tr("Page already opened"),
	                tr("%1 for %2 is opened by %3 (since %4).\n\nIf you both lay out this page, one of you overwrites the other's work.")
	                    .arg(pageName, m_dateEdit->date().toString("yyyy-MM-dd"), info.userName, formatLockSince(info.since)),
	                QMessageBox::NoButton, this);
	QPushButton* cont = box.addButton(tr("Continue anyway"), QMessageBox::AcceptRole);
	QPushButton* other = box.addButton(tr("Choose another page"), QMessageBox::RejectRole);
	box.setDefaultButton(other);
	const QString answer = m_pageLockTestAnswer;
	if (!answer.isEmpty())
		QTimer::singleShot(300, &box, [&box, cont, other, answer]() {
			box.grab().save(qEnvironmentVariable("SUNEER_PAGELOCK_SELFTEST") + "-question.png");
			(answer == QLatin1String("continue") ? cont : other)->click();
		});
	box.exec();
	m_pageLockAsking = false;
	return box.clickedButton() == cont;
}

void SuneerNewsPanel::takePageLock(const QString& pageId, const QString& pageName, const QString& date)
{
	if (m_heldPageId == pageId && m_heldDate == date)
		return;
	const bool wasMine = m_pageLocks.contains(pageId) && m_pageLocks.value(pageId).userId == m_userId;
	syncMyPageLocks(date, pageId, QString(), [this, pageId, pageName, date, wasMine](bool ok, const QSet<QString>& mine, const QString& err) {
		const bool stillCurrent = m_pageCombo->currentData().toString() == pageId && m_dateEdit->date().toString("yyyy-MM-dd") == date;
		if (!ok || !mine.contains(pageId))
		{
			apiLog(QStringLiteral("page lock: could not lock %1 %2: %3").arg(pageName, date, err.isEmpty() ? QStringLiteral("not in the reply") : err));
			if (stillCurrent)
			{
				m_pageLockLabel->setText(tr("%1 could not be locked for you (%2)").arg(pageName, err.isEmpty() ? tr("taken meanwhile?") : err));
				m_pageLockLabel->setStyleSheet("color:#8A6D00; font-size:11px;");
				m_pageLockLabel->show();
				refreshPageLocks();
			}
			return;
		}
		if (!stillCurrent)
		{
			// Another page was chosen while this one was being locked: let it go again.
			if (!wasMine)
				syncMyPageLocks(date, QString(), pageId, [](bool, const QSet<QString>&, const QString&) {});
			return;
		}
		m_heldPageId = pageId;
		m_heldPageName = pageName;
		m_heldDate = date;
		m_heldWasMineBefore = wasMine;
		m_heldDoc = m_doc;
		m_heldDocBound = (m_doc != nullptr);
		apiLog(QStringLiteral("page lock: %1 %2 locked for you%3").arg(pageName, date, wasMine ? QStringLiteral(" (it already was - e.g. in the browser; not released by Scribus)") : QString()));
		m_lockKeepAlive->start();
		sendLockHeartbeat();
		showPageLockLine();
	});
}

void SuneerNewsPanel::releasePageLock(bool blocking, const QString& why)
{
	if (m_heldPageId.isEmpty())
		return;
	const QString id = m_heldPageId, name = m_heldPageName, date = m_heldDate;
	const bool keep = m_heldWasMineBefore;
	m_heldPageId.clear();
	m_heldPageName.clear();
	m_heldDate.clear();
	m_heldWasMineBefore = false;
	m_heldDocBound = false;
	m_heldDoc = nullptr;
	m_lockKeepAlive->stop();
	showPageLockLine();
	if (keep)
	{
		apiLog(QStringLiteral("page lock: %1 %2 left locked (%3): it was yours before Scribus took it").arg(name, date, why));
		return;
	}
	auto finished = std::make_shared<bool>(false);
	++m_lockReleasesInFlight;
	syncMyPageLocks(date, QString(), id, [this, name, date, why, finished](bool ok, const QSet<QString>&, const QString& err) {
		*finished = true;
		--m_lockReleasesInFlight;
		apiLog(ok ? QStringLiteral("page lock: %1 %2 released (%3)").arg(name, date, why)
		          : QStringLiteral("page lock: release of %1 %2 failed (%3): %4").arg(name, date, why, err));
	});
	if (blocking && !*finished)
	{
		// Exit / logout: wait a little for the answer, never more than 3 s.
		QEventLoop loop;
		QTimer poll;
		poll.setInterval(20);
		connect(&poll, &QTimer::timeout, &loop, [&loop, finished]() { if (*finished) loop.quit(); });
		QTimer::singleShot(3000, &loop, &QEventLoop::quit);
		poll.start();
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
}

// Every minute while a page is held: still ours? The server has no heartbeat,
// but an admin can unlock a page (Locked Pages) or another user can take it
// once it is free. A free page is taken again; another user's is shown.
void SuneerNewsPanel::keepPageLockAlive()
{
	if (m_heldPageId.isEmpty())
	{
		m_lockKeepAlive->stop();
		return;
	}
	if (m_heldDocBound && !m_heldDoc)
	{
		releasePageLock(false, QStringLiteral("document closed"));
		return;
	}
	sendLockHeartbeat();
	const QString id = m_heldPageId, name = m_heldPageName, date = m_heldDate;
	if (m_pageCombo->currentData().toString() != id || m_dateEdit->date().toString("yyyy-MM-dd") != date)
		return;
	refreshPageLocks([this, id, name, date](bool ok, const QString& err) {
		if (!ok || m_heldPageId != id)
		{
			if (!ok)
				apiLog(QStringLiteral("page lock: keep-alive check of %1 failed (%2) - kept as it was").arg(name, err));
			return;
		}
		const auto it = m_pageLocks.constFind(id);
		if (it != m_pageLocks.constEnd() && it->userId == m_userId)
			return;
		m_heldPageId.clear();
		m_heldDate.clear();
		if (it == m_pageLocks.constEnd())
		{
			apiLog(QStringLiteral("page lock: %1 %2 was unlocked on the server - locked again").arg(name, date));
			takePageLock(id, name, date);
		}
		else
		{
			apiLog(QStringLiteral("page lock: %1 %2 is now opened by %3").arg(name, date, it->userName));
			setStatus(tr("%1 is now opened by %2").arg(name, it->userName), true);
			showPageLockLine();
		}
	});
}

void SuneerNewsPanel::releasePageLockOnExit()
{
	QElapsedTimer t;
	t.start();
	releasePageLock(true, QStringLiteral("Scribus closed"));
	if (m_lockReleasesInFlight > 0)
	{
		// A release started a moment ago (the document was closed on the way out).
		QEventLoop loop;
		QTimer poll;
		poll.setInterval(20);
		connect(&poll, &QTimer::timeout, &loop, [this, &loop]() { if (m_lockReleasesInFlight <= 0) loop.quit(); });
		QTimer::singleShot(qMax<qint64>(1, 3000 - t.elapsed()), &loop, &QEventLoop::quit);
		poll.start();
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	if (t.elapsed() > 5)
		apiLog(QStringLiteral("page lock: exit waited %1 ms for the release").arg(t.elapsed()));
}

// SUNEER_PAGELOCK_SELFTEST=<report prefix>: the page lock end to end against
// tests/suneer/fixtures/news_page_lock_mock.py (two users, the workflow's own
// lock rules). This Scribus is user A; user B acts through the same API with
// its own token. Each step waits for the server, then checks the Page box and
// the server's state. Ends by closing Scribus with a page held (the test
// script checks afterwards that the exit released it).
void SuneerNewsPanel::runPageLockSelfTest(const QString& report)
{
	struct Ctx
	{
		QStringList lines;
		int fails = 0;
		QList<std::function<void(std::function<void()>)>> steps;
		int step = 0;
		QJsonArray serverPages;   // last GET as A
		QString date, date2;
		QPointer<QWidget> page;   // the News Browser page in its tab stack
	};
	auto c = std::make_shared<Ctx>();
	c->date = qEnvironmentVariable("SUNEER_PAGELOCK_DATE", "2026-11-02");
	c->date2 = QDate::fromString(c->date, "yyyy-MM-dd").addDays(1).toString("yyyy-MM-dd");
	auto check = [c](const QString& what, bool ok) {
		c->lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		c->fails += ok ? 0 : 1;
	};
	auto note = [c](const QString& l) { c->lines << QStringLiteral("  ") + l; };
	auto indexOf = [this](const QString& pageId) { return m_pageCombo->findData(pageId); };
	// a request with any token (user B, or A for the server's view)
	auto call = [this](const QByteArray& token, const QByteArray& verb, const QString& path, const QJsonObject& body,
	                   std::function<void(int, QJsonObject)> done) {
		QNetworkRequest req(QUrl(apiUrl(path)));
		req.setRawHeader("Authorization", "Bearer " + token);
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setTransferTimeout(5000);
		QNetworkReply* r = m_nam->sendCustomRequest(req, verb, body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(r, &QNetworkReply::finished, this, [r, done]() {
			r->deleteLater();
			done(httpStatus(r), QJsonDocument::fromJson(r->readAll()).object());
		});
	};
	// who holds page \a id on \a date in the server's eyes ("" = free)
	auto holder = [call](const QString& group, const QString& date, std::function<void(QHash<QString, QString>)> done) {
		call("AT-A", "GET", QStringLiteral("/news/layout/pages?publishDate=%1&editionGroup=%2").arg(date, group), {},
		     [done](int, QJsonObject o) {
			QHash<QString, QString> h;
			for (const QJsonValue& v : o["data"].toArray())
			{
				const QJsonObject st = v.toObject()["status"].toObject();
				h[v.toObject()["id"].toString()] = st["lockedAt"].isNull() ? QString() : st["lockedBy"].toObject()["username"].toString();
			}
			done(h);
		});
	};
	auto waitFor = [this](std::function<bool()> cond, int ms, std::function<void(bool)> done) {
		auto* t = new QTimer(this);
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		t->setInterval(50);
		connect(t, &QTimer::timeout, this, [t, el, cond, ms, done]() {
			const bool ok = cond();
			if (!ok && el->elapsed() < ms)
				return;
			t->stop();
			t->deleteLater();
			done(ok);
		});
		t->start();
	};
	auto select = [this](int index) { m_pageCombo->setCurrentIndex(index); };
	auto run = std::make_shared<std::function<void()>>();
	*run = [this, c, run, report]() {
		if (c->step >= c->steps.size())
		{
			c->lines << (c->fails ? QStringLiteral("FAILED: %1").arg(c->fails) : QStringLiteral("ALL PASS"));
			QFile f(report + "-report.txt");
			if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
				f.write(c->lines.join('\n').toUtf8() + "\n");
			f.close();
			fprintf(stderr, "PAGELOCK-SELFTEST done\n");
			// Exit with Page 5 held: closeEvent -> releasePageLockOnExit (checked by the script).
			QTimer::singleShot(300, this, [this]() { if (m_mw) m_mw->close(); else QApplication::exit(0); });
			return;
		}
		auto st = c->steps[c->step++];
		st([run]() { QTimer::singleShot(100, *run); });
	};

	// 0. log in as A, edition KNR, the test date; A already has a TVM page locked (the browser)
	c->steps << [this, c, check, call, waitFor](std::function<void()> next) {
		// The News Browser page to the front of its tab stack (screenshots of the panel).
		// (widget() is null: the page lives in the Styles dock's tabs.)
		for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
			if (auto* sw = qobject_cast<QStackedWidget*>(w->parentWidget()))
			{
				sw->setCurrentWidget(w);
				c->page = w;
			}
		m_dateEdit->blockSignals(true);
		m_dateEdit->setDate(QDate::fromString(c->date, "yyyy-MM-dd"));
		m_dateEdit->blockSignals(false);
		QNetworkReply* r = startLoginRequest(QStringLiteral("a@test.local"), QStringLiteral("S3cret-right"));
		connect(r, &QNetworkReply::finished, this, [this, r, c, check, call, waitFor, next]() {
			QString e;
			const bool ok = finishLogin(r, QStringLiteral("a@test.local"), false, 0, e);
			r->deleteLater();
			check(QStringLiteral("login as A (user id %1)").arg(m_userId), ok && !m_userId.isEmpty());
			setLocked(false);
			onFetchStatic();
			const int k = m_editionCombo->findData("KNR");
			if (k >= 0)
				m_editionCombo->setCurrentIndex(k);
			waitFor([this]() { return m_pageCombo->count() >= 8; }, 8000, [this, c, check, call, next](bool ok) {
				check(QStringLiteral("KNR pages listed (%1)").arg(m_pageCombo->count()), ok);
				call("AT-A", "PATCH", "/news/layout/pages/lock", QJsonObject{{"pageIds", QJsonArray{"p-tvm-2"}}, {"publishDate", c->date}},
				     [check, next](int s, QJsonObject) { check(QStringLiteral("A's TVM Page 2 locked in the 'browser' (HTTP %1)").arg(s), s == 200); next(); });
			});
		});
	};
	// 1. B opens KNR Page 2 -> the Page box shows it
	c->steps << [this, c, check, call, note](std::function<void()> next) {
		call("AT-B", "PATCH", "/news/layout/pages/lock", QJsonObject{{"pageIds", QJsonArray{"p-knr-2"}}, {"publishDate", c->date}},
		     [this, check, note, next](int s, QJsonObject) {
			check(QStringLiteral("B locks KNR Page 2 (HTTP %1)").arg(s), s == 200);
			refreshPageLocks([this, check, note, next](bool ok, const QString& err) {
				const int i = m_pageCombo->findData("p-knr-2");
				const QString text = m_pageCombo->itemText(i);
				const QColor fg = m_pageCombo->itemData(i, Qt::ForegroundRole).value<QColor>();
				check(QStringLiteral("dropdown shows \"%1\" in grey (%2)").arg(text, fg.name()), ok && text == QStringLiteral("Page 2 – otheruser") && fg.isValid());
				check(QStringLiteral("other pages unmarked (\"%1\")").arg(m_pageCombo->itemText(m_pageCombo->findData("p-knr-1"))), m_pageCombo->itemText(m_pageCombo->findData("p-knr-1")) == QStringLiteral("Page 1"));
				m_pageCombo->showPopup();
				QTimer::singleShot(400, this, [this, err, next]() {
					m_pageCombo->view()->window()->grab().save(qEnvironmentVariable("SUNEER_PAGELOCK_SELFTEST") + "-dropdown.png");
					m_pageCombo->hidePopup();
					next();
				});
			});
		});
	};
	// 2. A takes Page 1 (free): locked for A, A's TVM page kept (the PATCH list is the whole set)
	c->steps << [this, c, check, holder, waitFor, select, indexOf](std::function<void()> next) {
		// Page 1 is the page the list opened on (restored, not chosen): choosing it again = the check.
		if (m_pageCombo->currentIndex() == indexOf("p-knr-1"))
			checkAndTakePageLock();
		else
			select(indexOf("p-knr-1"));
		waitFor([this]() { return m_heldPageId == "p-knr-1"; }, 6000, [this, c, check, holder, next](bool ok) {
			check(QStringLiteral("selecting Page 1 takes its lock (held: %1)").arg(m_heldPageId), ok);
			check(QStringLiteral("line under the Page box: \"%1\"").arg(m_pageLockLabel->text()), !m_pageLockLabel->isHidden() && m_pageLockLabel->text().contains("opened by you"));
			holder("eg-A-knr", c->date, [c, check, holder, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: KNR Page 1 locked by %1").arg(h["p-knr-1"]), h["p-knr-1"] == "suneer");
				holder("eg-A-tvm", c->date, [check, next](QHash<QString, QString> t) {
					check(QStringLiteral("server: A's TVM Page 2 still locked by %1 (not dropped by the PATCH)").arg(t["p-tvm-2"]), t["p-tvm-2"] == "suneer");
					next();
				});
			});
		});
	};
	// 3. A chooses Page 2 (B's) -> question -> Choose another page: back to Page 1, still held
	c->steps << [this, c, check, holder, waitFor, select, indexOf](std::function<void()> next) {
		m_pageLockTestAnswer = "choose";
		const int gen = m_lockCheckGen;
		select(indexOf("p-knr-2"));
		waitFor([this, gen]() { return m_lockCheckGen > gen && !m_pageLockAsking && m_pageCombo->currentData().toString() == "p-knr-1"; }, 8000,
		        [this, c, check, holder, next](bool ok) {
			check(QStringLiteral("B's Page 2 asked; 'Choose another page' went back to %1").arg(pageNameAt(m_pageCombo->currentIndex())), ok);
			check(QStringLiteral("Page 1 still held (%1)").arg(m_heldPageId), m_heldPageId == "p-knr-1");
			holder("eg-A-knr", c->date, [check, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: Page 1 %1, Page 2 %2").arg(h["p-knr-1"], h["p-knr-2"]), h["p-knr-1"] == "suneer" && h["p-knr-2"] == "otheruser");
				next();
			});
		});
	};
	// 4. Page 2 again -> Continue anyway: the line says so, Page 1 released, Page 2 stays B's
	c->steps << [this, c, check, holder, waitFor, select, indexOf](std::function<void()> next) {
		m_pageLockTestAnswer = "continue";
		select(indexOf("p-knr-2"));
		waitFor([this, c]() { return m_lockOverride.contains("p-knr-2|" + c->date) && m_lockReleasesInFlight == 0; }, 8000,
		        [this, c, check, holder, next](bool ok) {
			check(QStringLiteral("Continue anyway: \"%1\"").arg(m_pageLockLabel->text()), ok && m_pageLockLabel->text().contains("is opened by otheruser") && m_pageLockLabel->text().contains("continued anyway"));
			(c->page ? c->page.data() : static_cast<QWidget*>(m_stack))->grab().save(qEnvironmentVariable("SUNEER_PAGELOCK_SELFTEST") + "-continued.png");
			holder("eg-A-knr", c->date, [check, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: Page 1 released (\"%1\"), Page 2 still %2").arg(h["p-knr-1"], h["p-knr-2"]), h["p-knr-1"].isEmpty() && h["p-knr-2"] == "otheruser");
				next();
			});
		});
	};
	// 5. Page 3 (free) -> held
	c->steps << [this, c, check, holder, waitFor, select, indexOf](std::function<void()> next) {
		select(indexOf("p-knr-3"));
		waitFor([this]() { return m_heldPageId == "p-knr-3"; }, 6000, [this, c, check, holder, next](bool ok) {
			check(QStringLiteral("Page 3 held (%1)").arg(m_heldPageId), ok);
			holder("eg-A-knr", c->date, [check, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: Page 3 %1").arg(h["p-knr-3"]), h["p-knr-3"] == "suneer");
				next();
			});
		});
	};
	// 6. date +1 -> Page 3 of the next day taken, today's released; and back
	c->steps << [this, c, check, holder, waitFor](std::function<void()> next) {
		m_dateEdit->setDate(QDate::fromString(c->date2, "yyyy-MM-dd"));
		waitFor([this, c]() { return m_heldPageId == "p-knr-3" && m_heldDate == c->date2 && m_lockReleasesInFlight == 0; }, 8000,
		        [this, c, check, holder, next](bool ok) {
			check(QStringLiteral("date changed: Page 3 held for %1").arg(m_heldDate), ok);
			holder("eg-A-knr", c->date, [this, c, check, holder, next](QHash<QString, QString> h) {
				holder("eg-A-knr", c->date2, [this, c, check, h, next](QHash<QString, QString> h2) {
					check(QStringLiteral("server: Page 3 %1 \"%2\", %3 \"%4\"").arg(c->date, h["p-knr-3"], c->date2, h2["p-knr-3"]), h["p-knr-3"].isEmpty() && h2["p-knr-3"] == "suneer");
					m_dateEdit->setDate(QDate::fromString(c->date, "yyyy-MM-dd"));
					next();
				});
			});
		});
	};
	c->steps << [this, c, check, waitFor](std::function<void()> next) {
		waitFor([this, c]() { return m_heldPageId == "p-knr-3" && m_heldDate == c->date && m_lockReleasesInFlight == 0; }, 8000,
		        [this, c, check, next](bool ok) { check(QStringLiteral("date back: Page 3 held for %1").arg(m_heldDate), ok); next(); });
	};
	// 7. an admin unlocks Page 3 (Locked Pages) -> keep-alive takes it again
	c->steps << [this, c, check, call, holder, waitFor](std::function<void()> next) {
		call("AT-B", "PATCH", "/news/layout/pages/p-knr-3/toggle-lock", QJsonObject{{"isLocked", false}, {"publishDate", c->date}},
		     [this, c, check, holder, waitFor, next](int s, QJsonObject) {
			check(QStringLiteral("admin unlock of Page 3 (HTTP %1)").arg(s), s == 200);
			keepPageLockAlive();
			waitFor([this]() { return false; }, 1500, [this, c, check, holder, next](bool) {
				holder("eg-A-knr", c->date, [this, check, next](QHash<QString, QString> h) {
					check(QStringLiteral("keep-alive: Page 3 locked again (%1, held %2)").arg(h["p-knr-3"], m_heldPageId), h["p-knr-3"] == "suneer" && m_heldPageId == "p-knr-3");
					next();
				});
			});
		});
	};
	// 8. admin unlock + B takes Page 3 -> keep-alive shows it, no longer held
	c->steps << [this, c, check, call, waitFor](std::function<void()> next) {
		call("AT-B", "PATCH", "/news/layout/pages/p-knr-3/toggle-lock", QJsonObject{{"isLocked", false}, {"publishDate", c->date}},
		     [this, c, check, call, waitFor, next](int, QJsonObject) {
			call("AT-B", "PATCH", "/news/layout/pages/lock", QJsonObject{{"pageIds", QJsonArray{"p-knr-2", "p-knr-3"}}, {"publishDate", c->date}},
			     [this, check, waitFor, next](int s, QJsonObject) {
				keepPageLockAlive();
				waitFor([this]() { return m_heldPageId.isEmpty(); }, 6000, [this, check, s, next](bool ok) {
					check(QStringLiteral("B took Page 3 (HTTP %1): keep-alive lets go, line \"%2\"").arg(s).arg(m_pageLockLabel->text()),
					      ok && m_pageLockLabel->text().contains("opened by otheruser"));
					next();
				});
			});
		});
	};
	// 9. Page 6 already A's (the browser) -> held but never released by Scribus
	c->steps << [this, c, check, call, holder, waitFor, select, indexOf](std::function<void()> next) {
		call("AT-A", "PATCH", "/news/layout/pages/lock", QJsonObject{{"pageIds", QJsonArray{"p-tvm-2", "p-knr-6"}}, {"publishDate", c->date}},
		     [this, c, check, holder, waitFor, select, indexOf, next](int, QJsonObject) {
			select(indexOf("p-knr-6"));
			waitFor([this]() { return m_heldPageId == "p-knr-6"; }, 6000, [this, c, check, holder, waitFor, select, indexOf, next](bool ok) {
				check(QStringLiteral("Page 6 (already A's in the browser) held, marked as A's before (%1)").arg(m_heldWasMineBefore ? "yes" : "no"), ok && m_heldWasMineBefore);
				select(indexOf("p-knr-7"));
				waitFor([this]() { return m_heldPageId == "p-knr-7" && m_lockReleasesInFlight == 0; }, 6000, [c, check, holder, next](bool ok2) {
					holder("eg-A-knr", c->date, [check, ok2, next](QHash<QString, QString> h) {
						check(QStringLiteral("moved to Page 7 (%1): Page 6 left as it was in the browser (%2)").arg(h["p-knr-7"], h["p-knr-6"]), ok2 && h["p-knr-7"] == "suneer" && h["p-knr-6"] == "suneer");
						next();
					});
				});
			});
		});
	};
	// 10. Fetch News on a page chosen without the check -> the check runs first, then the fetch
	c->steps << [this, c, check, waitFor, indexOf](std::function<void()> next) {
		m_pageCombo->blockSignals(true);
		m_pageCombo->setCurrentIndex(indexOf("p-knr-8"));
		m_pageCombo->blockSignals(false);
		onFetchNews();
		waitFor([this]() { return m_heldPageId == "p-knr-8" && m_fetchPageId == "p-knr-8"; }, 8000, [this, check, next](bool ok) {
			check(QStringLiteral("Fetch News: Page 8 checked + locked first, then fetched (held %1, fetched %2)").arg(m_heldPageId, m_fetchPageId), ok);
			next();
		});
	};
	// 11. the document closes -> its page lock is released
	c->steps << [this, c, check, holder, waitFor](std::function<void()> next) {
		const bool hadDoc = m_heldDocBound;
		if (m_mw && m_mw->HaveDoc)
			m_mw->slotFileClose();
		waitFor([this]() { return m_heldPageId.isEmpty() && m_lockReleasesInFlight == 0; }, 6000, [c, check, holder, hadDoc, next](bool ok) {
			holder("eg-A-knr", c->date, [check, ok, hadDoc, next](QHash<QString, QString> h) {
				check(QStringLiteral("document closed: Page 8 released (bound to the document: %1, server \"%2\")").arg(hadDoc ? "yes" : "no", h["p-knr-8"]), hadDoc && ok && h["p-knr-8"].isEmpty());
				next();
			});
		});
	};
	// 12. the server stops answering -> after ~5 s "unknown", Scribus never stops
	c->steps << [this, c, check, call, waitFor, select, indexOf](std::function<void()> next) {
		call("AT-A", "POST", "/mock/hang", QJsonObject{{"on", true}}, [this, c, check, call, waitFor, select, indexOf, next](int, QJsonObject) {
			auto ticks = std::make_shared<int>(0);
			auto* tick = new QTimer(this);
			tick->setInterval(100);
			connect(tick, &QTimer::timeout, this, [ticks]() { ++*ticks; });
			tick->start();
			auto el = std::make_shared<QElapsedTimer>();
			el->start();
			select(indexOf("p-knr-4"));
			waitFor([this]() { return m_pageLockLabel->text().contains("unknown"); }, 9000, [this, c, check, call, tick, ticks, el, next](bool ok) {
				tick->stop();
				tick->deleteLater();
				check(QStringLiteral("server hangs: \"%1\" after %2 ms, event loop ran %3 ticks of 100 ms").arg(m_pageLockLabel->text()).arg(el->elapsed()).arg(*ticks),
				      ok && el->elapsed() < 7000 && *ticks >= el->elapsed() / 100 - 5);
				call("AT-A", "POST", "/mock/hang", QJsonObject{{"on", false}}, [next](int, QJsonObject) { next(); });
			});
		});
	};
	// 13. Page 5 held, then Scribus is closed (the script checks the release)
	c->steps << [this, c, check, waitFor, select, indexOf](std::function<void()> next) {
		select(indexOf("p-knr-5"));
		waitFor([this]() { return m_heldPageId == "p-knr-5"; }, 6000, [this, check, next](bool ok) {
			check(QStringLiteral("Page 5 held before closing Scribus (%1)").arg(m_heldPageId), ok);
			next();
		});
	};
	(*run)();
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
	setStatus(tr("Loading editions from %1 ...").arg(QUrl(m_apiHost).host()));
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
		setStatus(m_userInfoLoaded
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
			// The login reply names editions by id only; the short name is
			// what the combo carries, so key the user's edition group by it too.
			if (m_editionGroupIds.contains(ed["id"].toString()))
				m_editionGroupIds[sn] = m_editionGroupIds[ed["id"].toString()];
		}
		const int unmatched = m_allowedEditions.size() - matched.size();
		setStatus(unmatched > 0
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
			if (m_editionGroupIds.contains(allowed.first))
				m_editionGroupIds[sn] = m_editionGroupIds[allowed.first];
		}
		if (m_editionCombo->count() == 0)
		{
			m_editionCombo->blockSignals(false);
			setEditionsEnabled(false);
			setStatus(tr("Warning: Edition list unavailable (%1) - short names for your editions unknown").arg(
				note.isEmpty() ? tr("no shortName in reply") : note));
			return;
		}
		setStatus(tr("Warning: Edition list unavailable (%1) - using configured short names").arg(
			note.isEmpty() ? tr("no shortName in reply") : note));
	}
	int idx = m_lastEdition.isEmpty() ? -1 : m_editionCombo->findData(m_lastEdition);
	m_editionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	m_editionCombo->blockSignals(false);
	onEditionChanged(m_editionCombo->currentIndex());
}

void SuneerNewsPanel::onEditionChanged(int index)
{
	releasePageLock(false, QStringLiteral("edition changed"));
	m_pageLocks.clear();
	m_pageLocksKnown = false;
	m_prevPageIndex = -1;
	m_pageCombo->clear();
	if (m_pageLockLabel)
		m_pageLockLabel->hide();
	if (m_takeOverBtn)
		m_takeOverBtn->hide();
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
		setStatus(tr("Error: Edition %1 is not assigned to your account").arg(m_lastEdition));
		return;
	}
	setStatus(tr("Loading pages for %1 ...").arg(m_lastEdition));
	apiGet(QUrl(apiUrl("/external/edition-pages/" + m_lastEdition)), [this](QNetworkReply* reply) {
		QString extErr;
		if (reply->error() != QNetworkReply::NoError)
			extErr = describeError(reply);
		const QJsonObject o = extErr.isEmpty() ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject();
		m_pages = o["data"].toArray();
		if (m_pages.isEmpty())
		{
			// The public pages route is gone or empty (the deployed server
			// answers 404 for it since 2026-10-09): the Layout page's own
			// authenticated list of this edition's pages for the chosen date.
			const QString gid = editionGroupIdFor(m_lastEdition);
			if (!m_accessToken.isEmpty() && !gid.isEmpty())
			{
				QUrl pu(apiUrl("/news/layout/pages"));
				QUrlQuery pq;
				pq.addQueryItem("publishDate", m_dateEdit->date().toString("yyyy-MM-dd"));
				pq.addQueryItem("editionGroup", gid);
				pu.setQuery(pq);
				QString perr;
				apiGet(pu, [&](QNetworkReply* r2) {
					if (r2->error() != QNetworkReply::NoError || httpStatus(r2) < 200 || httpStatus(r2) >= 300)
					{
						perr = describeError(r2);
						return;
					}
					const QJsonValue d = QJsonDocument::fromJson(r2->readAll()).object()["data"];
					m_pages = d.isArray() ? d.toArray() : d.toObject()["pages"].toArray();
				});
				apiLog(QStringLiteral("GET /news/layout/pages (fallback) -> %1 pages%2").arg(m_pages.size()).arg(perr.isEmpty() ? QString() : " (" + perr + ")"));
			}
		}
		if (m_pages.isEmpty())
		{
			const QString msg = o["error"].toObject()["message"].toString();
			setStatus(tr("Warning: No pages for edition %1%2").arg(m_lastEdition, (extErr.isEmpty() ? msg : extErr).isEmpty() ? QString() : ": " + (extErr.isEmpty() ? msg : extErr)));
			return;
		}
		QList<QJsonObject> pages;
		for (const QJsonValue& v : m_pages)
			pages << v.toObject();
		std::stable_sort(pages.begin(), pages.end(), [](const QJsonObject& a, const QJsonObject& b) {
			return a["displayOrder"].toInt() < b["displayOrder"].toInt();
		});
		m_pageCombo->blockSignals(true);
		m_pageTypes.clear();
		for (const QJsonObject& pg : pages)
		{
			m_pageCombo->addItem(pg["name"].toString(), pg["id"].toString());
			m_pageCombo->setItemData(m_pageCombo->count() - 1, pg["name"].toString(), kPageNameRole);
			QString type = pg["pageType"].toString();
			if (type.isEmpty())
				type = pg["type"].toString();
			if (!type.isEmpty())
				m_pageTypes[pg["id"].toString()] = type;
		}
		int idx = -1;
		for (int i = 0; i < m_pageCombo->count() && !m_lastPage.isEmpty(); ++i)
			if (pageNameAt(i) == m_lastPage)
				idx = i;
		if (idx >= 0)
			m_pageCombo->setCurrentIndex(idx);
		m_pageCombo->blockSignals(false);
		m_prevPageIndex = m_pageCombo->currentIndex();
		preselectLayoutModel();
		// Who has which page open (no lock is taken until a page is chosen or fetched).
		m_pageLocksKnown = false;
		refreshPageLocks();
		setStatus(tr("%1: %2 pages").arg(m_lastEdition).arg(pages.size()));
	});
}

void SuneerNewsPanel::onFetchNews()
{
	if (m_pageCombo->count() == 0 || m_pageCombo->currentIndex() < 0)
	{
		setStatus(tr("Choose an edition and a page first"));
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
			setStatus(tr("Error: Fixture file not readable"));
		return;
	}
	if (!editionAllowed(m_lastEdition))
	{
		setStatus(tr("Error: Edition %1 is not assigned to your account").arg(m_lastEdition));
		return;
	}
	// The page lock first (async, at most 5 s): another user's page asks;
	// "Choose another page" stops here. A network problem never stops it.
	if (m_lockGateKey != currentPageKey())
	{
		m_fetchBtn->setEnabled(false);
		const QString key = currentPageKey();
		checkAndTakePageLock([this, key]() {
			m_fetchBtn->setEnabled(true);
			if (m_lockGateKey == key && currentPageKey() == key)
				onFetchNews();
		});
		QTimer::singleShot(kLockTimeoutMs + 1000, m_fetchBtn, [this]() { m_fetchBtn->setEnabled(true); });
		return;
	}
	m_fetchPageId = m_pageCombo->currentData().toString();
	m_fetchDate   = m_dateEdit->date().toString("yyyy-MM-dd");
	m_lastPage    = pageNameAt(m_pageCombo->currentIndex());
	saveConfig();
	statusProgress(tr("Fetching"), 0, 0);
	m_newsList->clear();
	m_newsData = QJsonArray();
	m_placeBtn->setEnabled(false);
	m_fetchBtn->setEnabled(false);
	// Two sources, merged by story id:
	//  1. /external/edition-pages/:page/news (no login) - the deployed server
	//     appears to hand out only stories not yet placed, and its media
	//     entries carry captions;
	//  2. /news/layout?publishDate&pageId (login) - the Layout page's own list:
	//     EVERY status, with page.dtpStatus and the page name. Authoritative
	//     for the status; paged, so all pages are read.
	QJsonArray external, layout;
	QString extErr, layErr;
	QUrl url(apiUrl("/external/edition-pages/" + m_fetchPageId + "/news"));
	QUrlQuery q;
	q.addQueryItem("publishDate", m_fetchDate);
	url.setQuery(q);
	apiGet(url, [&](QNetworkReply* reply) {
		if (reply->error() != QNetworkReply::NoError)
		{
			extErr = describeError(reply);
			return;
		}
		const QJsonValue data = QJsonDocument::fromJson(reply->readAll()).object()["data"];
		external = data.isArray() ? data.toArray() : data.toObject()["newses"].toArray();
	});
	int layoutTotal = -1, layCode = 0;
	if (!m_accessToken.isEmpty())
	{
		for (int pageNo = 1; pageNo <= 50; ++pageNo)
		{
			QUrl lu(apiUrl("/news/layout"));
			QUrlQuery lq;
			lq.addQueryItem("publishDate", m_fetchDate);
			lq.addQueryItem("pageId", m_fetchPageId);
			lq.addQueryItem("page", QString::number(pageNo));
			lq.addQueryItem("pageSize", "100");
			lu.setQuery(lq);
			int got = 0;
			bool ok = false;
			apiGet(lu, [&](QNetworkReply* reply) {
				layCode = httpStatus(reply);
				if (reply->error() != QNetworkReply::NoError || layCode < 200 || layCode >= 300)
				{
					layErr = describeError(reply);
					return;
				}
				ok = true;
				const QJsonObject d = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
				const QJsonArray arr = d["newses"].toArray();
				layoutTotal = d["totalNewses"].toInt(-1);
				for (const QJsonValue& v : arr)
					layout.append(v);
				got = arr.size();
			});
			if (!ok || got == 0 || (layoutTotal >= 0 && layout.size() >= layoutTotal))
				break;
		}
		apiLog(QStringLiteral("GET /news/layout?publishDate=%2&pageId=..%1&page=..&pageSize=100 -> HTTP %5, %3 stories (total %6)%4").arg(m_fetchPageId.right(6), m_fetchDate).arg(layout.size()).arg(layErr.isEmpty() ? QString() : " (" + layErr + ")").arg(layCode).arg(layoutTotal));
	}
	//  3. /news/used?publishDate&editionPage&editionGroup (login) - the web
	//     app's "Used News" page: stories whose entry for THIS page is USED;
	//  4. /news/balanced?publishDate&editionGroup (login) - its "Balanced
	//     News" page: stories balanced on every page (edition-wide, no page).
	//     Both want the user's edition GROUP id (from /users/me) and give only
	//     id/refId/title/reporter/createdAt(/newsPages) - no body text.
	QJsonArray usedList, balList;
	QString usedErr, balErr;
	int usedCode = 0, balCode = 0;
	const QString groupId = editionGroupIdFor(m_lastEdition);
	m_fetchNote.clear();
	if (m_accessToken.isEmpty())
	{
		m_fetchNote = tr("Used: log in to see used stories");
		apiLog(QStringLiteral("GET /news/used, /news/balanced skipped: not logged in"));
	}
	else if (groupId.isEmpty())
	{
		usedErr = balErr = tr("no edition group id for %1 in the login reply").arg(m_lastEdition);
		m_fetchNote = tr("Used: %1").arg(usedErr);
		apiLog(QStringLiteral("GET /news/used, /news/balanced skipped: %1 (known: %2)").arg(usedErr, QStringList(m_editionGroupIds.keys()).join(',')));
	}
	else
	{
		auto pagedList = [&](const QString& path, const QString& listKey, const QString& totalKey, bool withPage, QJsonArray& out, QString& err, int& lastCode) {
			int total = -1;
			QStringList keys, top;
			for (int pageNo = 1; pageNo <= 50; ++pageNo)
			{
				QUrl u(apiUrl(path));
				QUrlQuery uq;
				uq.addQueryItem("page", QString::number(pageNo));
				uq.addQueryItem("pageSize", "100");
				uq.addQueryItem("search", "");
				uq.addQueryItem("publishDate", m_fetchDate);
				if (withPage)
					uq.addQueryItem("editionPage", m_fetchPageId);
				uq.addQueryItem("editionGroup", groupId);
				u.setQuery(uq);
				int got = 0;
				bool ok = false;
				apiGet(u, [&](QNetworkReply* reply) {
					lastCode = httpStatus(reply);
					if (reply->error() != QNetworkReply::NoError || lastCode < 200 || lastCode >= 300)
					{
						err = describeError(reply);
						return;
					}
					ok = true;
					const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
					if (top.isEmpty())
						top = root.keys() + (root["data"].isObject() ? QStringList{"data:" + root["data"].toObject().keys().join('/')} : QStringList());
					const QJsonObject d = root["data"].toObject();
					const QJsonArray arr = d[listKey].toArray();
					total = d[totalKey].toInt(-1);
					for (const QJsonValue& v : arr)
						out.append(v);
					got = arr.size();
					if (keys.isEmpty() && !arr.isEmpty())
						keys = arr.first().toObject().keys();
				});
				if (!ok || got == 0 || (total >= 0 && out.size() >= total))
					break;
			}
			apiLog(QStringLiteral("GET %1?page=..&pageSize=100&search=&publishDate=%2&editionPage=%3&editionGroup=..%4 -> HTTP %5, %6 stories (total %7)%8%9%10")
			       .arg(path, m_fetchDate, withPage ? ".." + m_fetchPageId.right(6) : QString(), groupId.right(6)).arg(lastCode).arg(out.size()).arg(total)
			       .arg(top.isEmpty() ? QString() : " keys: " + top.join(','), keys.isEmpty() ? QString() : " item fields: " + keys.join(','), err.isEmpty() ? QString() : " (" + err + ")"));
		};
		pagedList("/news/used", "usedNews", "totalUsedNews", true, usedList, usedErr, usedCode);
		pagedList("/news/balanced", "balancedNews", "totalBalancedNews", false, balList, balErr, balCode);
		auto note = [&](const QString& what, int code, const QString& e, const QString& path) {
			if (e.isEmpty())
				return QString();
			if (code == 401 || code == 403)
				return tr("%1: HTTP %2 - log in").arg(what).arg(code);
			if (code == 404)
				return tr("%1: HTTP 404 %2").arg(what, path);
			if (code == 0)
				return tr("%1: network error %2").arg(what, QUrl(m_apiHost).host());
			return tr("%1: HTTP %2 %3").arg(what).arg(code).arg(e);
		};
		QStringList notes;
		const QString un = note(tr("Used"), usedCode, usedErr, "/news/used");
		const QString bn = note(tr("Balance"), balCode, balErr, "/news/balanced");
		if (!un.isEmpty()) notes << un;
		if (!bn.isEmpty()) notes << bn;
		m_fetchNote = notes.join(QStringLiteral(" · "));
	}
	apiLog(QStringLiteral("GET /external/edition-pages/..%1/news date %2 -> %3 stories%4").arg(m_fetchPageId.right(6), m_fetchDate).arg(external.size()).arg(extErr.isEmpty() ? QString() : " (" + extErr + ")"));
	if (!extErr.isEmpty() && layout.isEmpty() && usedList.isEmpty() && balList.isEmpty())
	{
		setStatus(tr("Error: ") + tr("News: %1").arg(extErr));
		m_fetchBtn->setEnabled(true);
		return;
	}
	// Merge: external entries first (richer media), then the layout entries
	// not seen yet; the layout entry's status/page wins when both exist.
	QJsonArray merged;
	QHash<QString, int> index;
	for (const QJsonValue& v : external)
	{
		const QString id = newsServerId(v.toObject());
		if (id.isEmpty() || index.contains(id))
			continue;
		index[id] = merged.size();
		merged.append(v);
	}
	for (const QJsonValue& v : layout)
	{
		const QJsonObject ln = v.toObject();
		const QString id = newsServerId(ln);
		if (id.isEmpty())
			continue;
		if (index.contains(id))
		{
			QJsonObject n = merged[index[id]].toObject();
			if (ln["page"].isObject())
				n["page"] = ln["page"];
			if (!ln["dateline"].toString().isEmpty() && n["dateline"].toString().isEmpty())
				n["dateline"] = ln["dateline"];
			// the desk's "Columns" / "Image columns": only /news/layout (login) sends them, /external does not
			for (const char* k : { "noOfColumns", "imageColumns" })
				if (!ln.value(QLatin1String(k)).isNull() && ln.value(QLatin1String(k)).toInt() > 0)
					n[QLatin1String(k)] = ln.value(QLatin1String(k));
			if (mergeMedia(n, ln))
				apiLog(QStringLiteral("story ..%1: pictures taken from /news/layout (%2); the /external entry had none").arg(id.right(6)).arg(mediaList(n).size()));
			merged[index[id]] = n;
		}
		else
		{
			index[id] = merged.size();
			merged.append(ln);
		}
	}
	// The web app's own Used / Balanced lists: their verdict wins. A story
	// only they know gets a minimal entry (title, reporter, page) tagged
	// with where it came from; it carries no body, so it previews as such.
	auto fold = [&](const QJsonArray& list, const QString& status, const QString& source) {
		for (const QJsonValue& v : list)
		{
			const QJsonObject e = v.toObject();
			const QString id = newsServerId(e);
			if (id.isEmpty())
				continue;
			QJsonObject n = index.contains(id) ? merged[index[id]].toObject() : e;
			if (index.contains(id) && mergeMedia(n, e))
				apiLog(QStringLiteral("story ..%1: pictures taken from /news/%2 (%3)").arg(id.right(6), source).arg(mediaList(n).size()));
			QJsonObject pg = n["page"].toObject();
			if (status == "USED" || pg["dtpStatus"].toString().toUpper() != "USED")
			{
				pg["dtpStatus"] = status;
				if (status == "USED")
				{
					if (pg["id"].toString().isEmpty())
						pg["id"] = m_fetchPageId;
					if (pg["name"].toString().isEmpty())
						pg["name"] = m_lastPage;
				}
			}
			n["page"] = pg;
			n["_source"] = source;
			if (!index.contains(id))
			{
				index[id] = merged.size();
				merged.append(n);
			}
			else
				merged[index[id]] = n;
		}
	};
	fold(usedList, "USED", "used");
	fold(balList, "BALANCED", "balanced");
	{
		int withPic = 0, rawOnly = 0;
		for (const QJsonValue& v : merged)
		{
			const QJsonObject n = v.toObject();
			if (!mediaList(n).isEmpty())
				++withPic;
			else if (!n["newsMedia"].toArray().isEmpty())
				++rawOnly;   // newsMedia present but without any usable URL
		}
		apiLog(QStringLiteral("list: %1 stories, %2 with pictures, %3 with newsMedia entries lacking a fileUrl").arg(merged.size()).arg(withPic).arg(rawOnly));
		for (const QJsonValue& v : merged)
		{
			const QJsonObject n = v.toObject();
			const QJsonArray ml = mediaList(n);
			if (!ml.isEmpty())
				apiLog(QStringLiteral("  ..%1 \"%2\": %3 picture(s), first %4").arg(newsServerId(n).right(6), collapseSpaces(n["title"].toString()).left(30)).arg(ml.size()).arg(QUrl(ml.first().toObject()["media"].toObject()["fileUrl"].toString()).path()));
			else if (!n["newsMedia"].toArray().isEmpty())
				apiLog(QStringLiteral("  ..%1 \"%2\": newsMedia[0] keys %3 - no URL found").arg(newsServerId(n).right(6), collapseSpaces(n["title"].toString()).left(30), n["newsMedia"].toArray().first().toObject().keys().join(',')));
		}
	}
	m_newsData = merged;
	m_fetchSources = QStringLiteral("%1 ext%2 + %3 layout%4 + %5 used%6 + %7 bal%8")
	                 .arg(external.size()).arg(extErr.isEmpty() ? QString() : "(err)")
	                 .arg(layout.size()).arg(layErr.isEmpty() ? QString() : "(err)")
	                 .arg(usedList.size()).arg(usedErr.isEmpty() ? QString() : "(err)")
	                 .arg(balList.size()).arg(balErr.isEmpty() ? QString() : "(err)");
	showNewsList();          // status line: counts + m_fetchNote (applyListFilter)
	if (m_newsData.isEmpty() && !m_fetchNote.isEmpty())
		setStatus(statusText() + QStringLiteral(" · ") + m_fetchNote);
	m_fetchBtn->setEnabled(true);
	// the open document is this page: what is on it -> used / unused in the workflow
	QTimer::singleShot(0, this, [this]() { reconcilePage(tr("Fetch News")); });
}

// The user's edition GROUP id (what /news/used, /news/balanced and
// /news/layout/pages want) for the edition the combo names: by short name,
// by the edition id the public list gives that short name, or by name.
QString SuneerNewsPanel::editionGroupIdFor(const QString& edition) const
{
	if (edition.isEmpty())
		return QString();
	if (m_editionGroupIds.contains(edition))
		return m_editionGroupIds.value(edition);
	for (const QJsonValue& v : m_editions)
	{
		const QJsonObject ed = v.toObject();
		if (ed["shortName"].toString().compare(edition, Qt::CaseInsensitive) == 0)
		{
			if (m_editionGroupIds.contains(ed["id"].toString()))
				return m_editionGroupIds.value(ed["id"].toString());
			if (m_editionGroupIds.contains(ed["name"].toString()))
				return m_editionGroupIds.value(ed["name"].toString());
		}
	}
	for (auto it = m_editionGroupIds.constBegin(); it != m_editionGroupIds.constEnd(); ++it)
		if (it.key().compare(edition, Qt::CaseInsensitive) == 0 || it.key().startsWith(edition, Qt::CaseInsensitive))
			return it.value();
	return QString();
}

QString SuneerNewsPanel::collapseSpaces(const QString& text)
{
	// one line, blank ends (zero-width characters, NBSP, HTML leftovers) removed
	return SuneerTextClean::oneLine(text);
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

int SuneerNewsPanel::requestedColumns(const QJsonObject& news)
{
	const int ov = news.value(QStringLiteral("_colsOverride")).toInt(0);
	if (ov > 0)
		return qBound(1, ov, 8);
	const QJsonValue v = news.value(QStringLiteral("noOfColumns"));
	const int n = v.isDouble() ? v.toInt() : v.toString().toInt();
	return n > 0 ? qBound(1, n, 8) : 0;
}

QString SuneerNewsPanel::overrideBadge(const QJsonObject& news)
{
	QStringList parts;
	if (const int rc = requestedColumns(news); rc > 0)
		parts << tr("%1 col").arg(rc) + (news.value(QStringLiteral("_colsOverride")).toInt() > 0 ? QStringLiteral("*") : QString());
	if (!requestedHeadStyle(news).isEmpty())
		parts << requestedHeadStyle(news);
	const QString pp = requestedPhotoPos(news);
	if (!pp.isEmpty())
		parts << (pp == QLatin1String("none") ? tr("no photo") : photoPositionLabel(pp));
	return parts.join(QStringLiteral(" \u00B7 "));
}

void SuneerNewsPanel::relabelOverrideBadge(QListWidgetItem* row, const QJsonObject& news)
{
	if (!row)
		return;
	row->setData(Qt::UserRole + 6, overrideBadge(news));
	m_newsList->viewport()->update();
}

QString SuneerNewsPanel::headlineStyleForWidth(int cols) const
{
	QString s = activeModel().headlineStyleFor(cols, false);
	if (s.isEmpty())
	{
		// house table: the headline size most often seen per width on the measured 2024-09-21 pages
		// (autolayout-rules.json headline.size_by_cols_observed); the model's own rules win when it has them
		static const char* const kHouse[] = { "", "16 M", "24 M", "36 M", "40 M", "48 M", "56 M", "64 M", "68 B" };
		s = QString::fromLatin1(kHouse[qBound(1, cols, 8)]);
	}
	if (m_doc && !s.isEmpty() && !m_doc->paragraphStyles().contains(s))
		return QString();
	return s;
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
		const int imgCount   = mediaList(news).size();
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
		if (!news["_source"].toString().isEmpty())
		{
			// Came from the web app's Used/Balanced list: name the reporter
			// and the filing time; the list carries no body text.
			const QJsonObject by = news["createdBy"].toObject();
			const QString who = (by["firstName"].toString() + " " + by["lastName"].toString()).trimmed();
			if (!who.isEmpty())
				tip << tr("Reporter: %1").arg(who);
			if (!news["createdAt"].toString().isEmpty())
				tip << tr("Filed: %1").arg(news["createdAt"].toString().left(16).replace('T', ' '));
			tip << (news["_source"].toString() == "used" ? tr("From the server's Used list") : tr("From the server's Balanced list"));
		}
		item->setToolTip(tip.join("\n"));
		item->setData(Qt::UserRole + 3, tip.join("\n"));
		// suneer: second row line - date, words, photos, reporter (the headline keeps line one)
		{
			QStringList meta;
			const QString when = news["createdAt"].toString().left(10);
			if (!when.isEmpty())
				meta << when;
			const QString body = contentToParagraphs(news["content"].toString()).join(QLatin1Char(' '));
			const int words = body.isEmpty() ? 0 : body.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
			if (words > 0)
				meta << (words == 1 ? tr("1 word") : tr("%1 words").arg(words));
			if (imgCount > 0)
				meta << (imgCount == 1 ? tr("1 photo") : tr("%1 photos").arg(imgCount));
			const QJsonObject by = news["createdBy"].toObject();
			const QString who = (by["firstName"].toString() + " " + by["lastName"].toString()).trimmed();
			if (!who.isEmpty())
				meta << who;
			else if (!byline.isEmpty())
				meta << byline;
			item->setData(Qt::UserRole + 5, meta.join(QStringLiteral(" · ")));
		}
		// the desk's column count / "Place with" overrides as a badge ("3 col*", "36 M", photo position)
		item->setData(Qt::UserRole + 6, overrideBadge(news));
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
		setStatus(tr("No news for %1 on %2").arg(pageNameAt(m_pageCombo->currentIndex()), m_fetchDate));
	else
		applyListFilter(true);     // "N stories - X USD, Y UNUSD, Z BAL (showing: f)" or "No f stories (N total) - change Show:"
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
	// The row delegate elides at paint time; the item text stays the full
	// title (search, sorting, tooltips). A resize only needs a repaint.
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		QListWidgetItem* item = m_newsList->item(i);
		const QString full = item->data(Qt::UserRole + 1).toString();
		if (item->text() != full)
			item->setText(full);
	}
	m_newsList->viewport()->update();
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
	{
		QListWidgetItem* row = m_newsList->item(i);
		if (row->isHidden())
			continue;                       // filter: only what is shown
		row->setCheckState(checked && !isPlacedRow(row) && !isBalanceRow(row) ? Qt::Checked : Qt::Unchecked);
	}
}

QString SuneerNewsPanel::newsCaption(const QJsonObject& news)
{
	const QJsonArray media = mediaList(news);
	if (!media.isEmpty())
	{
		const QJsonObject m0 = media[0].toObject();
		QString c = SuneerTextClean::paragraphs(m0["caption"].toString()).join(QChar('\n'));
		if (c.isEmpty())
			c = SuneerTextClean::paragraphs(m0["media"].toObject()["caption"].toString()).join(QChar('\n'));
		if (!c.isEmpty())
			return c;
	}
	return SuneerTextClean::paragraphs(news["caption"].toString()).join(QChar('\n'));
}

QString SuneerNewsPanel::newsHighlights(const QJsonObject& news)
{
	const QJsonValue v = news["highlights"];
	if (v.isArray())
	{
		QStringList parts;
		for (const QJsonValue& p : v.toArray())
			parts << SuneerTextClean::paragraphs(p.toString());
		return parts.join("\n");
	}
	return SuneerTextClean::paragraphs(v.toString()).join(QChar('\n'));
}

QStringList SuneerNewsPanel::contentToParagraphs(const QString& content)
{
	// HTML decoded, every paragraph trimmed (zero-width characters too: the API ends many
	// stories with "\n\u200B\n\u200B"), empty paragraphs dropped
	return SuneerTextClean::paragraphs(content);
}

void SuneerNewsPanel::showPreview(const QJsonObject& news)
{
	const QString kicker   = news["kicker"].toString();
	const QString title    = news["title"].toString();
	const QString byline   = news["byline"].toString();
	const QString dateline = news["dateline"].toString();
	const QString highlights = newsHighlights(news);
	const QStringList paras = contentToParagraphs(news["content"].toString());
	QString html;
	if (!kicker.isEmpty())
		html += QStringLiteral("<span style='color:#E65100; font-size:26px; font-weight:bold;'>%1</span><br>").arg(kicker.toHtmlEscaped());
	html += QStringLiteral("<b style='font-size:30px;'>%1</b>").arg(title.toHtmlEscaped());
	if (!highlights.isEmpty())
		html += QStringLiteral("<br><span style='font-size:24px; color:#333;'>%1</span>").arg(highlights.toHtmlEscaped().replace("\n", "<br>"));
	if (!byline.isEmpty())
		html += QStringLiteral("<br><span style='color:#666; font-size:24px;'>%1</span>").arg(byline.toHtmlEscaped());
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

	// ---- photos: every media entry, thumbnails first, loaded one after the
	// other through downloadImage() (the same cache, token and format
	// sniffing as Place) once the dialog is on screen.
	const QJsonArray media = mediaList(news);
	struct Cell
	{
		QToolButton* btn {nullptr};
		QLabel* info {nullptr};
		QString fileUrl, thumbUrl, caption, localFull;
	};
	QList<Cell*> cells;
	if (media.isEmpty())
	{
		QLabel* none = new QLabel(tr("No photo"), &dlg);
		none->setStyleSheet("color:#888; font-size:18px; padding:4px;");
		vl->addWidget(none);
	}
	else
	{
		QScrollArea* sa = new QScrollArea(&dlg);
		sa->setWidgetResizable(true);
		sa->setFixedHeight(250);
		sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		QWidget* bar = new QWidget(sa);
		QHBoxLayout* hb = new QHBoxLayout(bar);
		hb->setAlignment(Qt::AlignLeft);
		for (int i = 0; i < media.size(); ++i)
		{
			const QJsonObject m = media[i].toObject();
			const QJsonObject md = m["media"].toObject();
			Cell* c = new Cell;
			c->fileUrl = md["fileUrl"].toString().trimmed();
			c->thumbUrl = md["thumbUrl"].toString().trimmed();
			c->caption = collapseSpaces(m["caption"].toString());
			if (c->caption.isEmpty())
				c->caption = collapseSpaces(md["caption"].toString());
			QWidget* cellW = new QWidget(bar);
			QVBoxLayout* cv = new QVBoxLayout(cellW);
			cv->setContentsMargins(2, 2, 2, 2);
			c->btn = new QToolButton(cellW);
			c->btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
			c->btn->setIconSize(QSize(160, 160));
			c->btn->setFixedSize(176, 190);
			c->btn->setText(tr("loading..."));
			c->btn->setToolTip(c->fileUrl);
			c->info = new QLabel(cellW);
			c->info->setWordWrap(true);
			c->info->setFixedWidth(176);
			c->info->setStyleSheet("font-size:12px; color:#333;");
			c->info->setText(c->caption);
			cv->addWidget(c->btn);
			cv->addWidget(c->info);
			hb->addWidget(cellW);
			cells << c;
		}
		sa->setWidget(bar);
		vl->addWidget(sa);
	}

	QTextEdit* te = new QTextEdit(&dlg);
	te->setReadOnly(true);
	te->setHtml(html);
	vl->addWidget(te);
	QPushButton* closeBtn = new QPushButton(tr("Close"), &dlg);
	connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
	vl->addWidget(closeBtn);

	bool closing = false;
	auto showFull = [&dlg, &closing](const QString& path, const QString& caption) {
		if (closing)
			return;
		QImageReader rd(path);
		rd.setAutoTransform(true);
		const QImage img = rd.read();
		if (img.isNull())
		{
			QMessageBox::warning(&dlg, tr("Image"), tr("Not an image: %1\n%2").arg(path, rd.errorString()));
			return;
		}
		QDialog big(&dlg);
		big.setWindowTitle(caption.isEmpty() ? QFileInfo(path).fileName() : caption);
		QVBoxLayout* bl = new QVBoxLayout(&big);
		QScrollArea* bsa = new QScrollArea(&big);
		QLabel* pic = new QLabel(bsa);
		const QSize avail = dlg.screen() ? dlg.screen()->availableSize() * 0.85 : QSize(1200, 800);
		pic->setPixmap(QPixmap::fromImage(img.size().width() > avail.width() || img.size().height() > avail.height()
			? img.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img));
		bsa->setWidget(pic);
		bsa->setWidgetResizable(true);
		bl->addWidget(bsa);
		QLabel* dim = new QLabel(tr("%1 x %2 px - %3").arg(img.width()).arg(img.height()).arg(path), &big);
		dim->setWordWrap(true);
		bl->addWidget(dim);
		QPushButton* ok = new QPushButton(tr("Close"), &big);
		connect(ok, &QPushButton::clicked, &big, &QDialog::accept);
		bl->addWidget(ok);
		big.resize(avail.width() * 0.9, avail.height());
		big.exec();
	};
	auto setFailed = [](Cell* c, const QString& why) {
		c->btn->setIcon(QIcon());
		c->btn->setText(why);
		c->btn->setStyleSheet("QToolButton { background:#D32F2F; color:white; border:2px solid #B71C1C; padding:4px; }");
		c->btn->setToolTip(c->fileUrl + "\n" + why);
	};
	for (Cell* c : cells)
	{
		connect(c->btn, &QToolButton::clicked, &dlg, [this, c, &showFull, &setFailed, &closing]() {
			if (closing)
				return;
			if (c->localFull.isEmpty())
			{
				QString why;
				c->localFull = downloadImage(c->fileUrl, &why);
				if (c->localFull.isEmpty())
				{
					setFailed(c, why);
					return;
				}
			}
			showFull(c->localFull, c->caption);
		});
	}
	// Sequential loader: thumbnail (thumbUrl, else the file) for the button,
	// then the full file for its pixel size - which also warms the cache
	// that Place uses, so both see the same picture.
	int next = 0;
	std::function<void()> loadNext;
	loadNext = [&]() {
		if (closing || next >= cells.size())
			return;
		Cell* c = cells[next++];
		const QString thumbSrc = c->thumbUrl.isEmpty() ? c->fileUrl : c->thumbUrl;
		QString why;
		QString local = downloadImage(thumbSrc, &why);
		if (closing)
			return;
		if (local.isEmpty() && !c->thumbUrl.isEmpty())
			local = downloadImage(c->fileUrl, &why);   // no thumbnail: the picture itself
		if (closing)
			return;
		if (local.isEmpty())
			setFailed(c, why);
		else
		{
			QImageReader rd(local);
			rd.setAutoTransform(true);
			const QImage img = rd.read();
			if (img.isNull())
				setFailed(c, tr("not an image: %1").arg(rd.errorString().isEmpty() ? sniffImageFormat(QByteArray()) : rd.errorString()));
			else
			{
				c->btn->setIcon(QIcon(QPixmap::fromImage(img.scaled(QSize(160, 160), Qt::KeepAspectRatio, Qt::SmoothTransformation))));
				c->btn->setText(QString());
				c->btn->setStyleSheet(QString());
			}
		}
		// The full picture: pixel size under the thumbnail.
		QString why2;
		const QString full = downloadImage(c->fileUrl, &why2);
		if (closing)
			return;
		if (full.isEmpty())
			c->info->setText(QStringLiteral("%1<br><span style='color:#D32F2F;'>%2</span>").arg(c->caption.toHtmlEscaped(), tr("image: %1").arg(why2).toHtmlEscaped()));
		else
		{
			c->localFull = full;
			QImageReader rd(full);
			rd.setAutoTransform(true);
			const QSize sz = rd.size();
			if (sz.isValid())
				c->info->setText(QStringLiteral("%1<br><span style='color:#555;'>%2 x %3 px</span>").arg(c->caption.toHtmlEscaped()).arg(sz.width()).arg(sz.height()));
			else
				c->info->setText(QStringLiteral("%1<br><span style='color:#D32F2F;'>%2</span>").arg(c->caption.toHtmlEscaped(), tr("not an image: %1").arg(rd.errorString()).toHtmlEscaped()));
		}
		QTimer::singleShot(0, &dlg, loadNext);
	};
	if (!cells.isEmpty())
		QTimer::singleShot(0, &dlg, loadNext);
	dlg.exec();
	closing = true;
	qDeleteAll(cells);
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
	if (!isPlacedRow(sel) && !confirmWorkflowPage(tr("Place")))
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
			setStatus(pg >= 0 ? tr("Placed - on page %1 of this document, selected").arg(pg + 1)
			                               : tr("Placed - on the pasteboard of this document, selected"));
		}
		else
			setStatus(tr("Placed on the server (maybe on another machine); not in this document"));
		return;
	}
	// the top-left of the active model's content area
	const ScPage* cp = m_doc->currentPage();
	const SuneerLayout::Grid grid = SuneerLayout::gridFor(activeModel(), cp->width(), cp->height(), cp->Margins.top(), cp->Margins.bottom(),
	                                                      cp->Margins.left(), cp->Margins.right(),
	                                                      cp->guides.verticalAutoCount() > 0 ? cp->guides.verticalAutoCount() + 1 : 0);
	double x = cp->xOffset() + grid.content.left();
	double y = cp->yOffset() + grid.content.top();
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
		                                               tr("Place news: %1").arg(news["title"].toString().left(40)),
		                                               QString(), Um::ICreate);
	QList<PageItem*> items = placeNews(news, x, y, false);
	Q_UNUSED(items);
	recordPlaceUndo(QList<QJsonObject>() << news);
	if (tx)
		tx.commit();
	markPlaced(QList<QJsonObject>() << news);
}


// A story known only from the server's Used/Balanced list has no body: the full record
// (GET /news/:id, with media) is fetched first. False (and a warning) when it cannot be read.
bool SuneerNewsPanel::ensureFullStory(QJsonObject& n)
{
	if (n["_source"].toString().isEmpty() || n.contains("content") || n.contains("newsMedia"))
	{
		fetchMediaIfMissing(n);
		return true;
	}
	const QString id = newsServerId(n);
	QJsonObject full;
	QString err;
	int code = 0;
	apiGet(QUrl(apiUrl("/news/" + id)), [&](QNetworkReply* reply) {
		code = httpStatus(reply);
		if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
		{
			err = describeError(reply);
			return;
		}
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		full = root["data"].isObject() ? root["data"].toObject() : root;
		if (full["news"].isObject())
			full = full["news"].toObject();
	});
	apiLog(QStringLiteral("GET /news/%1 (full story before placing) -> HTTP %2%3").arg(id.right(6)).arg(code).arg(err.isEmpty() ? QString() : " (" + err + ")"));
	if (full.isEmpty())
	{
		QMessageBox::warning(this, tr("Story not loaded"),
			tr("'%1' could not be read from the server, so it was not placed:\n%2").arg(collapseSpaces(n["title"].toString()).left(60), err));
		return false;
	}
	for (auto it = full.begin(); it != full.end(); ++it)
		if (it.key() != "page")
			n[it.key()] = it.value();
	fetchMediaIfMissing(n);
	for (int i = 0; i < m_newsData.size(); ++i)
		if (newsServerId(m_newsData[i].toObject()) == id)
			m_newsData[i] = n;
	return true;
}

void SuneerNewsPanel::onPlaceSelected()
{
	if (!m_doc)
	{
		QMessageBox::warning(this, tr("Error"), tr("No document open!"));
		return;
	}
	if (!confirmWorkflowPage(tr("Place")))
		return;
	QList<QJsonObject> stories, balance;
	int usedSkipped = 0;
	for (int i = 0; i < m_newsList->count(); i++)
	{
		if (m_newsList->item(i)->checkState() != Qt::Checked || m_newsList->item(i)->isHidden())
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
		setStatus(usedSkipped > 0
			? tr("Nothing to place: the %n ticked story(ies) is/are already used (use 'Mark unused' to release)", "", usedSkipped)
			: tr("Tick the stories to place"));
		return;
	}
	// A story known only from the server's Used/Balanced list has no body:
	// fetch the full record (GET /news/:id, with media) before placing it.
	for (QJsonObject& n : stories)
		if (!ensureFullStory(n))
			n = QJsonObject();
	stories.erase(std::remove_if(stories.begin(), stories.end(), [](const QJsonObject& n) { return n.isEmpty(); }), stories.end());
	if (stories.isEmpty())
		return;
	// a page model placeholder selected on the page: the one ticked story goes into it
	if (placeIntoSelectedSlot(stories))
		return;
	m_placeUsedSkipped = usedSkipped;
	if (activeModel().isObituary())
	{
		placeObituaries(stories);
		return;
	}
	if (activeModelId() != QLatin1String("normal"))
	{
		// any other news model arranges the stories on the page, around everything already there
		bool changed = false;
		const QString report = arrangeCore(activeModel(), (int) SuneerLayout::Keep::EveryFrame, stories, &changed);
		QMessageBox::information(this, tr("Place Selected"), report);
		return;
	}
	const QString target = settingsString("layout/placeTarget", "left");
	if (target == "page")
	{
		// Old behaviour: stacked on the current page, still one undo step.
		UndoTransaction tx;
		if (UndoManager::undoEnabled())
			tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup,
			                                               tr("Place %1 stories").arg(stories.size()), QString(), Um::ICreate);
		const ScPage* cp = m_doc->currentPage();
		const SuneerLayout::Grid grid = SuneerLayout::gridFor(activeModel(), cp->width(), cp->height(), cp->Margins.top(), cp->Margins.bottom(),
		                                                      cp->Margins.left(), cp->Margins.right(), 0);
		double baseX = cp->xOffset() + grid.content.left();
		double curY  = cp->yOffset() + grid.content.top();
		for (const QJsonObject& news : stories)
		{
			QList<PageItem*> items = placeNews(news, baseX, curY, false);
			const QRectF r = itemsRect(items);
			if (!r.isNull())
				curY = r.bottom() + spacingValues().pt(SuneerSpacing::ModuleGapV);
		}
		recordPlaceUndo(stories);
		if (tx)
			tx.commit();
		markPlaced(stories);
		return;
	}
	placeOnPasteboard(stories, target == "right");
	markPlaced(stories);
}

// House default until Auto Layout: 4 grid columns for the story, 3 for the
// photo, photo at the right. The server's noOfColumns / imageColumns are
// ignored on purpose (Auto Layout will decide later).
int SuneerNewsPanel::storyColumns(const QJsonObject& news) const
{
	if (m_forceCols > 0)
		return m_forceCols;
	// the desk's "Columns" (Workflow / right-click override) beats the model's default width
	if (const int req = requestedColumns(news); req > 0)
		return qBound(1, req, qMax(1, activeModel().columns));
	// the active model: a fixed width (Normal: Settings "Story columns") or the width by body length
	int chars = 0;
	for (const QString& p : contentToParagraphs(news["content"].toString()))
		chars += p.length();
	return qBound(1, activeModel().columnsFor(chars, false), 24);
}
// Width of a story or photo that spans \a cols columns of the page grid.
// The grid is the page's automatic column guides when it has them; else
// Settings (house grid: 8 columns of 34.25 mm with an 8 mm gutter on the
// 330 mm type area). \a colGapPt is the gutter between grid columns.
void SuneerNewsPanel::storyGeometry(int cols, double& totalW, double& colGapPt) const
{
	// the active model's grid on the current page (column guides when the model says so)
	const ScPage* p = m_doc->currentPage();
	SuneerLayout::Grid g;
	// "Adjust gaps" with the story-to-story gaps: the grid with that horizontal gap
	SuneerLayoutModel gm = activeModel();
	if (m_gapOverride)
		gm.gutterMm = m_gapOverride->v[SuneerSpacing::ModuleGapH];
	if (p)
		g = SuneerLayout::gridFor(gm, p->width(), p->height(), p->Margins.top(), p->Margins.bottom(), p->Margins.left(), p->Margins.right(),
		                          p->guides.verticalAutoCount() > 0 ? p->guides.verticalAutoCount() + 1 : 0);
	else
		g = SuneerLayout::gridFor(gm, m_doc->pageWidth(), m_doc->pageHeight(), m_doc->margins()->top(), m_doc->margins()->bottom(),
		                          m_doc->margins()->left(), m_doc->margins()->right(), 0);
	cols = qBound(1, cols, g.cols);
	colGapPt = g.gutter;
	totalW = g.width(cols);
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
	const SuneerSpacing::Values spv = spacingValues();
	const double gap  = qMax(2.0 * kMmToPt, spv.pt(SuneerSpacing::ModuleGapH));   // the model's gaps, also on the spike
	const double vgap = qMax(2.0 * kMmToPt, spv.pt(SuneerSpacing::ModuleGapV));
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
		// moveItem() drags the welded partners along (a photo and its
		// caption are welded), so a pair must move only once: moved as
		// its own item AND again as the partner's weld, the photo landed
		// a whole pasteboard-width to the right of its story - "text
		// placed, picture gone". Same rule as ScribusDoc::moveGroup().
		QList<PageItem*> movedByWeld;
		for (PageItem* it : pasteboardItems)
		{
			if (movedByWeld.contains(it))
				continue;
			if (it->isWelded())
				movedByWeld.append(it->itemsWeldedTo());
			m_doc->moveItem(delta, 0, it);
		}
		stack.translate(delta, 0);
	}
	else if (rightSide && stack.right() > rowRight + sc->right())
		m_doc->setScratchUndoable(sc->left(), stack.right() - rowRight + gap);
	for (const QList<PageItem*>& st : placedStories)
		for (PageItem* it : st)
			it->OwnPage = -1;

	recordPlaceUndo(stories);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);
	setStatus(tr("Placed %1 stories on the %2 pasteboard").arg(placedStories.size()).arg(rightSide ? tr("right") : tr("left")));
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
	struct IndexAfter { SuneerNewsPanel* p; const QList<PageItem*>& items; ~IndexAfter() { p->indexAdd(items); } } indexAfter { this, items };
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
		const int imgCount = mediaList(news).size();
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
			row->setForeground(QBrush());             // normal text; the USD tag says it
			row->setToolTip(tr("USED - placed%1 (server status).\n"
			                   "Tick it for 'Mark unused' or 'Mark balance'; it cannot be placed again until then.").arg(whereTxt) + "\n" + tip);
		}
		else if (status == "BALANCED")
		{
			row->setIcon(IconManager::instance().loadIcon("panel-bookmarks"));
			row->setForeground(QBrush());             // normal text; the BAL tag says it
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
	applyListFilter();
}

QString SuneerNewsPanel::listFilter() const
{
	return m_filterCombo ? m_filterCombo->currentData().toString() : QStringLiteral("ALL");
}

// Local filter over the fetched list: rows whose status does not match are
// hidden (no new request). The counts are for the whole page, filter or not.
void SuneerNewsPanel::applyListFilter(bool setStatusLine)
{
	if (!m_newsList)
		return;
	const QString f = listFilter();
	int used = 0, unused = 0, bal = 0, shown = 0;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		QListWidgetItem* row = m_newsList->item(i);
		const QString st = row->data(Qt::UserRole + 4).toString();
		if (st == "USED") ++used; else if (st == "BALANCED") ++bal; else ++unused;
		const bool hide = (f != "ALL") && (st != f);
		if (row->isHidden() != hide)
		{
			row->setHidden(hide);
			if (hide)
				row->setCheckState(Qt::Unchecked);   // never act on what is not shown
		}
		if (!hide) ++shown;
	}
	const QString showing = f == "ALL" ? tr("all") : f == "USED" ? QStringLiteral("USD") : f == "BALANCED" ? QStringLiteral("BAL") : QStringLiteral("UNUSD");
	// The status line describes the WHOLE page; it is rewritten only when
	// the filter changes or a fetch just finished, so other messages
	// (marks, placing) are not wiped by a routine refresh.
	const QString noteSuffix = m_fetchNote.isEmpty() ? QString() : QStringLiteral(" · ") + m_fetchNote;
	if (m_newsList->count() > 0 && shown == 0 && f != "ALL")
		setStatus(tr("No %1 stories (%n total) - change Show:", "", m_newsList->count()).arg(showing) + noteSuffix);
	else if (setStatusLine && m_newsList->count() > 0)
		setStatus(tr("Unused %1 · Used %2 · Balance %3").arg(unused).arg(used).arg(bal)
		                       + (f == "ALL" ? QString() : tr(" · showing: %1").arg(showing)) + noteSuffix);
	if (m_countLabel)
	{
		m_countLabel->setText(m_newsList->count() == 0 ? QString()
			: tr("%n stories", "", m_newsList->count()) + QStringLiteral(" - %1 USD, %2 UNUSD, %3 BAL").arg(used).arg(unused).arg(bal)
			  + tr(" (showing: %1, %2)").arg(showing).arg(shown));
	}
	updateTickButtons();
}

void SuneerNewsPanel::updateTickButtons()
{
	if (!m_newsList || !m_releaseBtn || !m_placeBtn)
		return;
	int ticked = 0, toUsed = 0, toUnused = 0, toBal = 0;
	QStringList whyUnused, whyBal;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		const QListWidgetItem* row = m_newsList->item(i);
		if (row->checkState() != Qt::Checked || row->isHidden())
			continue;
		++ticked;
		const QString st = row->data(Qt::UserRole + 4).toString();
		if (allowedTransition(st, "USED")) ++toUsed;
		if (allowedTransition(st, unusedStatus())) ++toUnused; else whyUnused << transitionWhy(st, unusedStatus());
		if (allowedTransition(st, "BALANCED")) ++toBal; else whyBal << transitionWhy(st, "BALANCED");
	}
	m_placeBtn->setText(toUsed > 0 ? tr("Place Selected (%1)").arg(toUsed) : tr("Place Selected"));
	m_placeBtn->setEnabled(!m_newsData.isEmpty());
	m_releaseBtn->setText(toUnused > 0 ? tr("Mark unused (%1)").arg(toUnused) : tr("Mark unused"));
	m_releaseBtn->setEnabled(toUnused > 0);
	m_releaseBtn->setToolTip(toUnused > 0 || ticked == 0
		? tr("Mark every TICKED used or balance story as unused on the server (available to everyone); optionally remove them from this page")
		: tr("Not possible for the ticked story(ies): %1").arg(whyUnused.join(", ")));
	if (m_balanceBtn)
	{
		m_balanceBtn->setText(toBal > 0 ? tr("Mark balance (%1)").arg(toBal) : tr("Mark balance"));
		m_balanceBtn->setEnabled(toBal > 0);
		m_balanceBtn->setToolTip(toBal > 0 || ticked == 0
			? tr("Mark every TICKED free or used story as BALANCE on the server (kept for a later day, listed under Balanced News); optionally remove them from this page")
			: tr("Not possible for the ticked story(ies): %1").arg(whyBal.join(", ")));
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
	const double gap = spacingValues().pt(SuneerSpacing::WrapPad);
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
	const double maxImgH = activeModel().photoMaxHeightMm * kMmToPt;
	const SuneerSpacing::Values sp = spacingValues();
	const double gap = sp.pt(SuneerSpacing::WrapPad);              // headline block -> photo (the wrap starts there)
	const double photoTextGap = sp.pt(SuneerSpacing::PhotoText);   // photo block <-> text (photo above / below)
	const double captionGap = sp.pt(SuneerSpacing::CaptionGap);
	const double captionTextGap = sp.pt(SuneerSpacing::CaptionText);
	const double inset = sp.pt(SuneerSpacing::Inset);

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
	const SuneerLayoutModel am = activeModel();
	const bool obitRules = am.isObituary() || am.obitPacking;   // obituary models: first baseline = font ascent, tight captions

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
			double natural = hgt;
			if (ph->pixm.width() > 0 && ph->pixm.height() > 0)
				natural = hgt = pw * (double) ph->pixm.height() / ph->pixm.width();
			else if (hgt <= 1.0)
				natural = hgt = defaultImgH;
			const bool cropped = (maxImgH > 0 && hgt > maxImgH);
			if (cropped)
				hgt = maxImgH;
			ph->setXYPos(px, cy);
			ph->setWidth(pw);
			ph->setHeight(hgt);
			ph->updateClip();
			if (ph->pixm.width() > 0)
			{
				// Like the reference: free scaling with the ratio kept, the
				// scale chosen so the picture fills the frame width; a
				// picture taller than the limit shows its middle.
				ph->setImageScalingMode(false, true);
				ph->adjustPictScale();
				ph->setImageScalingMode(true, true);
				ph->setImageXYOffset(0, 0);
				if (cropped)
				{
					// adjustPictScale() fitted the (taller) picture to the
					// frame height; scale up so the width is filled, then
					// move the picture up by half the overhang.
					const double shownW = ph->width() * hgt / natural;   // width after fit-to-height
					if (shownW > 0.1)
					{
						const double k = pw / shownW;
						ph->setImageXYScale(ph->imageXScale() * k, ph->imageYScale() * k);
						const double overhang = natural - hgt;               // pt, at the new scale
						ph->setImageXYOffset(0, -(overhang / 2.0) / ph->imageYScale());
					}
				}
			}
			setWrap(ph, wrap);
			cy += hgt;
			PageItem* cap = (i < f.captions.size()) ? f.captions[i] : nullptr;
			if (cap)
			{
				cy += captionGap;
				const double capW0 = cap->width();
				cap->setXYPos(px, cy);
				cap->setWidth(pw);
				cap->updateClip();
				if (cap->itemText.length() > 0)
				{
					fitFrame(cap);
					// A caption_* frame is fitted to its ink (bottom gap = top gap, bottom distance 0), always,
					// as the last step. PageItem::checkChanges() does that by itself - but only in a document with
					// a view and undo on, and only when it notices the photo's width change (sometimes before the
					// fit above, which then left the caption 2 pt taller). A Layout Gallery card's scratch copy has
					// no view: done here, the page and the copy end the same (2026-10-17: 13.5 / 21.5 pt, 10 / 8 pt).
					Q_UNUSED(capW0);
					if (cap->isTextFrame() && cap->itemName().startsWith(QLatin1String("caption_")))
						cap->asTextFrame()->suneerFitCaptionHeight(true);
					if (obitRules && qAbs(cap->textToFrameDistBottom()) > 0.01)
						cap->setTextToFrameDistBottom(0.0);
				}
				else
					cap->setHeight(10.0 * kMmToPt);
				setWrap(cap, wrap);
				if (wrap && captionTextGap > 0)
					cap->setWrapOffsetBottom(captionTextGap);   // caption -> the text wrapping under it
				cy = cap->yPos() + cap->height() + captionTextGap;
			}
			else
				cy += photoTextGap;
		}
		return cy;
	};

	// Story frame geometry (position is the caller's; width/columns ours).
	f.text->setXYPos(x, y);
	f.text->setWidth(totalW);
	f.text->setColumns(cols);
	f.text->setColumnGap(sp.pt(SuneerSpacing::ColumnGap));
	f.text->setTextToFrameDist(inset, inset, inset, inset);
	if (obitRules)
		tidyTextTop(f.text, pos == "topAbove" ? 0.0 : inset, true);   // first baseline = font ascent under the top edge
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
		stackPhotos(f.text->yPos() + f.text->height() + photoTextGap);
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
	// the whole story welded around its text frame (photo - caption welds above stay; no cycle is made)
	{
		QList<PageItem*> all { f.text };
		for (PageItem* p : f.photos) if (p) all << p;
		for (PageItem* c : f.captions) if (c) all << c;
		SuneerWeld::weldStory(m_doc, all, f.text);
	}
}

// A working copy of the picture no larger than ~300 ppi at the placed width
// (the print standard), made once from the cached original. Big camera files
// (6000 px and more) otherwise cost decode time and memory at every reload
// and make canvas painting slow when the preview is set to full resolution.
// The original stays in the cache. Returns the path to load.
static QString suneerWorkingCopy(ScribusDoc* doc, const QString& original, double placedWidthPt)
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
	// next to the original in the cache, or in edited/ of a saved document
	const QString editedDir = SuneerPhotoStore::editedDir(doc, true);
	const QString copy = editedDir.isEmpty() ? original + QStringLiteral(".w%1.jpg").arg(needed)
	                                         : editedDir + QStringLiteral("/") + QFileInfo(original).fileName() + QStringLiteral(".w%1.jpg").arg(needed);
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

	logTextJunk(news);
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
	// Headline style: the story's own "Place with" choice, else the engine's (m_forceTitle), else the
	// width rules (model / house table) for the placed width, else the settings' title style.
	const int reqCols = requestedColumns(news);
	const QString ovHead = requestedHeadStyle(news);
	QString wantTitle = panelTitleStyle();
	if (!ovHead.isEmpty() && m_doc->paragraphStyles().contains(ovHead))
		wantTitle = ovHead;
	else if (!m_forceTitle.isEmpty() && m_doc->paragraphStyles().contains(m_forceTitle))
		wantTitle = m_forceTitle;               // the model's headline style for this width
	else if (!headlineStyleForWidth(storyColumns(news)).isEmpty())
		wantTitle = headlineStyleForWidth(storyColumns(news));   // headline size by the placed width
	const QString titleStyle      = styleOrEmpty(wantTitle, tr("title style"), missing);
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
	const QString ovPos = requestedPhotoPos(news);
	const QString pos = ovPos.isEmpty() ? currentPhotoPos() : ovPos;   // the story's own "Place with" photo position wins
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
		if (!frame || SuneerTextClean::trimBlank(text).isEmpty())
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
		setStatus(tr("Error: could not create the story frame"));
		return created;
	}
	f.text->setItemName(title.left(30).trimmed().isEmpty() ? tr("News story") : title.left(30).trimmed());
	f.text->setColumns(cols);
	f.text->setColumnGap(spacingValues().pt(SuneerSpacing::ColumnGap));
	setAttr(f.text, "news.role", "text");
	if (reqCols > 0)
		setAttr(f.text, "news.columns", QString::number(cols));   // the desk's width: Re-layout / Layout Gallery Apply keep it
	if (!ovHead.isEmpty())
		setAttr(f.text, "news.headStyle", wantTitle);              // "Place with" headline: kept through Re-layout / Apply
	if (!ovPos.isEmpty())
		setAttr(f.text, "news.photoPosUser", QStringLiteral("1"));  // "Place with" photo position (news.photoPos): kept through Re-layout / Apply

	// Kicker + headline block first, fitted, so its height is known.
	if (!kicker.isEmpty())
	{
		addPara(f.text, kicker, kickerStyle);
		// optional extra space under the kicker / subheading (0 = the paragraph styles decide)
		const double kextra = spacingValues().pt(SuneerSpacing::KickerHeadline);
		if (kextra > 0.0 && f.text->itemText.length() > 0)
		{
			const int lastPara = f.text->itemText.length() - 1;
			ParagraphStyle ps;
			ps.setGapAfter(f.text->itemText.paragraphStyle(lastPara).gapAfter() + kextra);
			f.text->itemText.applyStyle(lastPara, ps, false);
		}
	}
	addPara(f.text, title, titleStyle);
	{
		// optional extra space under the headline (0 = the paragraph styles decide)
		const double extra = spacingValues().pt(SuneerSpacing::HeadlineBody);
		if (extra > 0.0 && f.text->itemText.length() > 0)
		{
			const int lastPara = f.text->itemText.length() - 1;
			ParagraphStyle ps;
			ps.setGapAfter(f.text->itemText.paragraphStyle(lastPara).gapAfter() + extra);
			f.text->itemText.applyStyle(lastPara, ps, false);
		}
	}
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
	if (!bodyParas.isEmpty() && collapseSpaces(bodyParas.first()) == collapseSpaces(title) && !title.trimmed().isEmpty())
	{
		apiLog(QStringLiteral("story ..%1: first body paragraph repeats the headline - dropped").arg(newsServerId(news).right(6)));
		bodyParas.removeFirst();
	}
	for (const QString& para : bodyParas)
	{
		addPara(f.text, para, firstBody && !firstBodyStyle.isEmpty() ? firstBodyStyle : bodyStyle);
		firstBody = false;
	}
	// no empty line under the last one: the paragraph mark after it goes (its style stays)
	SuneerTextClean::cleanStoryText(f.text->itemText);
	// A document without a GUI (a Layout Gallery card's scratch copy) has no hyphenator of its own:
	// calling through the null pointer crashed Scribus (2026-10-16, Hyphenator::slotHyphenate +0x53).
	if (m_doc->docHyphenator)
		m_doc->docHyphenator->slotHyphenate(f.text);
	else
	{
		Hyphenator hy(nullptr, m_doc);
		hy.slotHyphenate(f.text);
	}

	// Photos: separate frames, each with a caption frame (caption_<image>).
	const QJsonArray media = mediaList(news);
	if (pos != "none" && !media.isEmpty())
	{
		QStringList failed;
		for (int mi = 0; mi < media.size(); ++mi)
		{
			const QJsonObject m = media[mi].toObject();     // normalised by mediaList()
			const QJsonObject md = m["media"].toObject();
			const QString fileUrl = md["fileUrl"].toString();
			QString capText = collapseSpaces(m["caption"].toString());
			if (capText.isEmpty())
				capText = collapseSpaces(md["caption"].toString());
			if (capText.isEmpty() && mi == 0)
				capText = collapseSpaces(news["caption"].toString());

			setStatus(tr("Downloading picture %1 of %2...").arg(mi + 1).arg(media.size()));
			QString why;
			QString localImg = downloadImage(fileUrl, &why);
			QString placeholder;
			QString originalPath;
			if (localImg.isEmpty())
			{
				failed << QStringLiteral("%1: %2").arg(QFileInfo(QUrl(fileUrl).path()).fileName(), why);
				placeholder = missingImagePlaceholder(why);
			}
			else
			{
				// a saved document: the original next to it (never modified), the working copy in edited/
				QString iwhy;
				// (a Gallery card's scratch copy writes nothing next to the document)
				if (!m_preview)
					localImg = SuneerPhotoStore::importOriginal(m_doc, localImg, newsRefId(news).isEmpty() ? newsServerId(news) : newsRefId(news), mi, &iwhy);
				if (!iwhy.isEmpty())
					apiLog(QStringLiteral("photo store: %1").arg(iwhy));
				originalPath = localImg;
				const int n = (pos == "top" || pos == "topAbove") ? cols : qBound(1, imgCols, cols);
				const double colW = (totalW - (cols - 1) * colGapPt) / cols;
				localImg = suneerWorkingCopy(m_doc, localImg, colW * n + (n - 1) * colGapPt);
			}

			int ifIdx = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle,
			                           x, y, 50.0 * kMmToPt, 50.0 * kMmToPt, 0, CommonStrings::None, CommonStrings::None);
			PageItem* imgFrame = (ifIdx >= 0) ? m_doc->Items->at(ifIdx) : nullptr;
			if (!imgFrame)
				continue;
			created << imgFrame;
			setAttr(imgFrame, "news.role", "photo");
			setAttr(imgFrame, "news.index", QString::number(mi));
			SuneerWeld::ensurePhotoUid(imgFrame);   // the stable id its caption is linked by (news.photoUid)
			setAttr(imgFrame, SuneerPhotoStore::AttrFileUrl, fileUrl);
			if (!originalPath.isEmpty())
				setAttr(imgFrame, SuneerPhotoStore::AttrOriginal, originalPath);
			imgFrame->setFillColor(CommonStrings::None);
			if (!localImg.isEmpty())
			{
				m_doc->loadPict(localImg, imgFrame, false, true);
				// Screen preview at normal (72 dpi) resolution for this frame,
				// whatever the document default; export still uses the full data.
				if (imgFrame->pixm.imgInfo.lowResType == 0)
					imgFrame->setResolution(1);
				// never embedded: News Browser photos live in <doc>_photos/ (relative link)
				imgFrame->setLineColor(CommonStrings::None);
			}
			else
			{
				// Never silent: the frame shows the red "Image missing" text
				// (a generated picture, so it survives layout and re-arrange)
				// and remembers the reason for the status line / re-link.
				setAttr(imgFrame, "news.photoMissing", why);
				setAttr(imgFrame, "news.photoUrl", fileUrl);
				if (!placeholder.isEmpty())
					m_doc->loadPict(placeholder, imgFrame, false, true);
				const QString red = m_doc->PageColors.tryAddColor("Red", ScColor(0, 255, 255, 0));
				imgFrame->setLineColor(red);
				imgFrame->setLineShade(100);
				imgFrame->setLineWidth(1.0);
			}
			f.photos << imgFrame;

			PageItem* captFrame = capText.isEmpty() ? nullptr : makeTextFrame(x, y, 50.0 * kMmToPt, 10.0 * kMmToPt);
			if (captFrame)
			{
				captFrame->setItemName(QStringLiteral("caption_%1").arg(imgFrame->itemName()));
				setAttr(captFrame, "news.role", "caption");
				imgFrame->AutoName = false;   // the name is saved in the file (setItemName() skips an unchanged name) for the tools that look for caption_<photo>
				// attached caption: follows this photo's geometry. Linked by the photo's uid, not its name - a name the
				// file does not keep is made again from the item's place in the list on every load
				SuneerWeld::linkCaption(imgFrame, captFrame);
				setAttr(captFrame, "news.captionGap", QString::number(spacingValues().pt(SuneerSpacing::CaptionGap), 'f', 3));
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
			missing << tr("Image missing: %1").arg(failed.join("; "));
	}

	layoutStory(f, x, y, cols, imgCols, pos);
	tagStoryItems(created, title, pos, imgCols, cols, newsServerId(news));
	// the model and the gaps it was placed with (Adjust gaps / re-layout read them back)
	rememberGaps(created, spacingModel(), spacingValues(), false);
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
		setStatus(tr("Warning: placed, but: %1").arg(missing.join("; ")));
	else
		setStatus(tr("Placed: %1").arg(title.left(60)));
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
		setStatus(tr("Error: this story has no story frame to re-arrange"));
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
	setStatus(tr("Photo %1, %2 columns: %3").arg(photoPositionLabel(pos)).arg(imgCols).arg(id));
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

// The choices offered for placing (Settings and the panel); the other keys
// stay available for "Photo position" on a placed story.
QStringList SuneerNewsPanel::placePhotoPositionKeys()
{
	return { "right", "left", "top" };
}

QString SuneerNewsPanel::photoPositionLabel(const QString& key)
{
	if (key == "top")      return tr("Full width (below headline)");
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
	if (!m_forcePos.isEmpty())
		return m_forcePos;
	// the Layout Model decides (its Photo position; the Settings dialog edits the Normal model's)
	return activeModel().photoPosition;
}
int SuneerNewsPanel::storyImageCols(const QJsonObject& news) const
{
	if (m_forceImgCols > 0)
		return m_forceImgCols;
	return qBound(1, activeModel().photoColumnsFor(storyColumns(news)), 24);
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



void SuneerNewsPanel::tidyTextTop(PageItem* text, double topPt, bool withUndo)
{
	if (!text || !text->isTextFrame())
		return;
	SuneerTextClean::cleanStory(text, withUndo);         // no empty first / last lines
	if (text->firstLineOffset() != FLOPFontAscent)
		text->setFirstLineOffset(FLOPFontAscent);
	if (qAbs(text->textToFrameDistTop() - topPt) > 0.01)
		text->setTextToFrameDistTop(topPt);
	if (text->itemText.length() > 0 && text->itemText.paragraphStyle(0).gapBefore() > 0.01)
	{
		ParagraphStyle ps;
		ps.setGapBefore(0.0);
		if (withUndo && UndoManager::undoEnabled())
		{
			auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
			is->set("APPLY_PARASTYLE");
			is->set("POS", 0);
			is->setStates(text->itemText.paragraphStyle(0), ps);
			UndoManager::instance()->action(text, is);
		}
		text->itemText.applyStyle(0, ps, false);
	}
	text->invalidateLayout();
}

int SuneerNewsPanel::stackObitFrames(const QList<PageItem*>& items, double gapPt, bool withUndo)
{
	QList<PageItem*> texts;
	for (PageItem* it : items)
		if (it && it->isTextFrame() && !it->isGroupChild() && it->itemText.length() > 0)
			texts << it;
	if (texts.isEmpty())
		return 0;
	std::sort(texts.begin(), texts.end(), [](PageItem* a, PageItem* b) { return a->yPos() < b->yPos(); });
	int changed = 0;
	double nextTop = -1;
	for (PageItem* t : texts)
	{
		const double oldY = t->yPos(), oldH = t->height();
		if (nextTop >= 0 && qAbs(t->yPos() - nextTop) > 0.05)
			t->setYPos(nextTop);
		// a frame under another text frame (name / caption above it): no top inset, the gap is the frame edge
		tidyTextTop(t, nextTop >= 0 ? 0.0 : t->textToFrameDistTop(), withUndo);
		if (qAbs(t->textToFrameDistBottom()) > 0.01)
			t->setTextToFrameDistBottom(0.0);
		fitFrame(t);
		t->updateClip();
		if (qAbs(t->yPos() - oldY) > 0.05 || qAbs(t->height() - oldH) > 0.05)
			++changed;
		nextTop = t->yPos() + t->height() + gapPt;
	}
	return changed;
}

void SuneerNewsPanel::fitFrame(PageItem* frame)
{
	if (!frame || !m_mw)
		return;
	frame->invalidateLayout();
	frame->layout();
	// The frame is fitted directly (what suneerAutoFitHeight() does for one frame, undoable on its own),
	// never through the GUI selection: selecting every frame of a page during Layout Model Apply made the
	// Properties palette load each one, and its hidden text-distance box wrote a clamped bottom distance
	// (4.25 -> 0 pt) back into short caption frames; a Gallery card's copy has no GUI selection at all, so
	// card and page differed (2026-10-17). It also kept the palette rebuilding while Apply ran.
	// Fitted to the last line (SuneerTextClean::fitHeight): Fit Frame Height added the bottom
	// text distance a second time and 2 pt, which was blank space under every story (2026-10-18).
	if (frame->isTextFrame() && !frame->asTextFrame()->isTableItem)
		SuneerTextClean::fitHeight(frame->asTextFrame(), 1.0, 20000.0);
}

// Text wrap with a gap: the contour line is the frame rectangle grown by
// the gap, so the body text keeps its distance from photo and caption.

// The one layout routine, used by placement and by "Photo position" /
// "Photo columns" on a placed story. Headline frame first (full width),
// then photos and body according to the position; the headline is never
// inside a wrap area.


// Right-click on a placed story: ungroup, re-arrange with the one layout
// routine, group again. One undo step.

// ----------------------------------------------------------------------------
// Pictures: one reader for every reply shape
// ----------------------------------------------------------------------------

QJsonArray SuneerNewsPanel::mediaList(const QJsonObject& news)
{
	QJsonArray out;
	QSet<QString> seen;
	auto urlOf = [](const QJsonObject& o) -> QString {
		for (const char* k : { "fileUrl", "url", "fileURL", "file", "path", "src", "thumbUrl" })
		{
			const QString v = o[QLatin1String(k)].toString().trimmed();
			if (!v.isEmpty())
				return v;
		}
		return QString();
	};
	auto add = [&](const QJsonObject& entry) {
		// entry = {media:{...}, caption} or the media record itself
		QJsonObject md = entry["media"].isObject() ? entry["media"].toObject() : QJsonObject();
		if (urlOf(md).isEmpty())
		{
			// flat shape: the entry carries the file fields
			if (!urlOf(entry).isEmpty())
				md = entry;
			else if (entry["file"].isObject() && !urlOf(entry["file"].toObject()).isEmpty())
				md = entry["file"].toObject();
			else if (entry["image"].isObject() && !urlOf(entry["image"].toObject()).isEmpty())
				md = entry["image"].toObject();
		}
		QString fileUrl = md["fileUrl"].toString().trimmed();
		if (fileUrl.isEmpty())
			fileUrl = urlOf(md);
		if (fileUrl.isEmpty() || seen.contains(fileUrl))
			return;
		seen.insert(fileUrl);
		QJsonObject m;
		m["fileUrl"]  = fileUrl;
		m["thumbUrl"] = md["thumbUrl"].toString();
		m["caption"]  = md["caption"].toString();
		m["fileName"] = md["fileName"].toString().isEmpty() ? md["originalName"].toString() : md["fileName"].toString();
		m["id"]       = md["id"].toString();
		QJsonObject e;
		e["media"]   = m;
		e["caption"] = entry["caption"].toString();
		if (entry.contains("isSelected"))
			e["isSelected"] = entry["isSelected"];
		out.append(e);
	};
	auto addArray = [&](const QJsonValue& v) {
		if (v.isArray())
		{
			for (const QJsonValue& x : v.toArray())
			{
				if (x.isObject())
					add(x.toObject());
				else if (x.isString() && !x.toString().trimmed().isEmpty())
					add(QJsonObject{{"fileUrl", x.toString().trimmed()}});
			}
		}
		else if (v.isObject())
			add(v.toObject());
	};
	for (const char* k : { "newsMedia", "mediaFiles", "media", "images", "newsImages", "files", "attachments" })
		addArray(news[QLatin1String(k)]);
	// A sent story: the per-edition clone carries the pictures.
	for (const QJsonValue& te : news["targetEditions"].toArray())
		addArray(te.toObject()["clonedNews"].toObject()["newsMedia"]);
	if (news["clonedNews"].isObject())
		addArray(news["clonedNews"].toObject()["newsMedia"]);
	// Layout-page fields: a single picture URL on the story.
	for (const char* k : { "obitHeadshotUrl", "imageUrl", "fileUrl" })
	{
		const QString u = news[QLatin1String(k)].toString().trimmed();
		if (!u.isEmpty())
			add(QJsonObject{{"fileUrl", u}});
	}
	return out;
}

bool SuneerNewsPanel::mergeMedia(QJsonObject& into, const QJsonObject& from)
{
	if (!mediaList(into).isEmpty())
		return false;
	const QJsonArray ml = mediaList(from);
	if (ml.isEmpty())
		return false;
	into["newsMedia"] = ml;
	return true;
}

void SuneerNewsPanel::fetchMediaIfMissing(QJsonObject& news)
{
	if (!mediaList(news).isEmpty() || m_preview)
		return;                           // a Gallery card: no network while m_doc is the scratch copy
	const QString id = newsServerId(news);
	if (id.isEmpty() || m_accessToken.isEmpty())
	{
		apiLog(QStringLiteral("story ..%1: no picture in the list reply; %2").arg(id.right(6), m_accessToken.isEmpty() ? QStringLiteral("not logged in, so not asked again") : QStringLiteral("no id")));
		return;
	}
	// 1. The full record, which the web app's editor reads.
	QJsonObject full;
	QString err;
	int code = 0;
	apiGet(QUrl(apiUrl("/news/" + id)), [&](QNetworkReply* reply) {
		code = httpStatus(reply);
		if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
		{
			err = describeError(reply);
			return;
		}
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		full = root["data"].isObject() ? root["data"].toObject() : root;
		if (full["news"].isObject())
			full = full["news"].toObject();
	});
	const QJsonArray ml = mediaList(full);
	apiLog(QStringLiteral("GET /news/%1 (picture check) -> HTTP %2, %3 picture(s), newsMedia entries %4%5")
	       .arg(id.right(6)).arg(code).arg(ml.size()).arg(full["newsMedia"].toArray().size()).arg(err.isEmpty() ? QString() : " (" + err + ")"));
	if (!ml.isEmpty())
	{
		news["newsMedia"] = ml;
		return;
	}
	// 2. The media route (reply data.mediaFiles[]).
	QJsonObject mf;
	err.clear();
	code = 0;
	apiGet(QUrl(apiUrl("/news/" + id + "/media")), [&](QNetworkReply* reply) {
		code = httpStatus(reply);
		if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
		{
			err = describeError(reply);
			return;
		}
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		mf = root["data"].isObject() ? root["data"].toObject() : root;
	});
	const QJsonArray ml2 = mediaList(mf);
	apiLog(QStringLiteral("GET /news/%1/media (picture check) -> HTTP %2, %3 picture(s)%4").arg(id.right(6)).arg(code).arg(ml2.size()).arg(err.isEmpty() ? QString() : " (" + err + ")"));
	if (!ml2.isEmpty())
		news["newsMedia"] = ml2;
}

// A picture (PNG in the cache folder) that says in red why the photo is
// not here. Loaded into the photo frame so nothing is ever placed silently.
QString SuneerNewsPanel::missingImagePlaceholder(const QString& reason) const
{
	const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/news/";
	if (!QDir().mkpath(cacheDir))
		return QString();
	const QString text = tr("Image missing: %1").arg(reason.isEmpty() ? tr("unknown reason") : reason);
	const QString hash = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
	const QString path = cacheDir + "missing-" + hash + ".png";
	if (QFileInfo(path).size() > 0 && QImageReader(path).canRead())
		return path;
	QImage img(900, 600, QImage::Format_RGB32);
	img.fill(Qt::white);
	img.setDotsPerMeterX(int(300 / 0.0254));
	img.setDotsPerMeterY(int(300 / 0.0254));
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor(220, 0, 0), 8));
	p.drawRect(QRect(4, 4, img.width() - 8, img.height() - 8));
	p.drawLine(4, 4, img.width() - 4, img.height() - 4);
	p.drawLine(img.width() - 4, 4, 4, img.height() - 4);
	p.fillRect(QRect(40, 200, img.width() - 80, 200), Qt::white);
	QFont f = p.font();
	f.setPixelSize(44);
	f.setBold(true);
	p.setFont(f);
	p.setPen(QColor(220, 0, 0));
	p.drawText(QRect(50, 200, img.width() - 100, 200), Qt::AlignCenter | Qt::TextWordWrap, text);
	p.end();
	if (!img.save(path, "PNG"))
		return QString();
	return path;
}

QString SuneerNewsPanel::sniffImageFormat(const QByteArray& b)
{
	if (b.size() < 12)
		return QString();
	const unsigned char* u = reinterpret_cast<const unsigned char*>(b.constData());
	if (u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF)
		return QStringLiteral("jpg");
	if (b.startsWith("\x89PNG"))
		return QStringLiteral("png");
	if (b.startsWith("GIF8"))
		return QStringLiteral("gif");
	if (b.startsWith("RIFF") && b.mid(8, 4) == "WEBP")
		return QStringLiteral("webp");
	if (b.startsWith("II*\0") || b.startsWith("MM\0*"))
		return QStringLiteral("tif");
	if (b.startsWith("BM"))
		return QStringLiteral("bmp");
	if (b.startsWith("8BPS"))
		return QStringLiteral("psd");
	return QString();
}

QByteArray SuneerNewsPanel::convertWebpToPng(const QByteArray& webp, QString& how, QString& why) const
{
	why.clear();
	// 1. Qt's own decoder (qt6-image-formats-plugins).
	if (QImageReader::supportedImageFormats().contains("webp"))
	{
		qDebug() << "news image: Qt webp plugin present";
		QBuffer in;
		in.setData(webp);
		in.open(QIODevice::ReadOnly);
		QImageReader r(&in, "webp");
		const QImage img = r.read();
		if (!img.isNull())
		{
			QByteArray out;
			QBuffer ob(&out);
			ob.open(QIODevice::WriteOnly);
			QImageWriter w(&ob, "png");
			if (w.write(img))
			{
				how = QStringLiteral("Qt webp plugin");
				return out;
			}
		}
		why = tr("Qt webp plugin: %1").arg(r.errorString().isEmpty() ? QStringLiteral("read failed") : r.errorString());
	}
	else
		why = tr("no Qt webp plugin (formats: %1)").arg(QString::fromLatin1(QImageReader::supportedImageFormats().join(',')));
	// 2. A helper process writing PNG to a temp file.
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/news/";
	QTemporaryFile inFile(dir + "webp-XXXXXX.webp");
	if (!inFile.open() || inFile.write(webp) != webp.size())
	{
		why = tr("cannot write temp file in %1").arg(dir);
		return QByteArray();
	}
	inFile.close();
	const QString outPath = inFile.fileName() + ".png";
	struct Tool { QString name; QStringList args; };
	const QList<Tool> tools = {
		{ QStringLiteral("dwebp"), { inFile.fileName(), "-o", outPath } },
		{ QStringLiteral("python3"), { "-c", "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], 'PNG')", inFile.fileName(), outPath } },
	};
	QStringList tried;
	for (const Tool& t : tools)
	{
		if (QStandardPaths::findExecutable(t.name).isEmpty())
		{
			tried << t.name + ": " + tr("not installed");
			continue;
		}
		QProcess p;
		p.start(t.name, t.args);
		if (!p.waitForFinished(20000))
		{
			p.kill();
			tried << t.name + ": " + tr("timed out");
			continue;
		}
		if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && QFileInfo(outPath).size() > 0)
		{
			QFile f(outPath);
			f.open(QIODevice::ReadOnly);
			const QByteArray out = f.readAll();
			f.close();
			QFile::remove(outPath);
			how = t.name + (why.isEmpty() ? QString() : " (" + why + ")");
			return out;
		}
		tried << t.name + ": " + QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1).left(120);
		QFile::remove(outPath);
	}
	why = (why.isEmpty() ? QString() : why + "; ") + tried.join("; ");
	return QByteArray();
}

// Cache in ~/.cache/scribus/news/<hash>-<basename>. A server-side change is
// detected with ETag / Last-Modified (304 keeps the cached copy). Only a
// reply that really decodes as a picture is kept; a 0-byte or unreadable
// cache entry is thrown away and fetched again. Every step is logged
// (URL path, status, size - never the token).
// "Re-download original from Workflow": the picture is fetched again (cache
// bypassed), goes into original/ (the old file kept as *_prev) and every
// frame that showed the original is reloaded. For a missing file (document
// moved, share offline) the same, without a _prev.
int SuneerNewsPanel::redownloadPhotos(const QList<PageItem*>& items, bool force, QString* report)
{
	int done = 0;
	QStringList lines;
	int n = 0;
	for (PageItem* item : items)
	{
		statusProgress(tr("Re-downloading"), ++n, items.size());
		if (!item || !SuneerPhotoStore::isNewsPhoto(item))
			continue;
		const QString url = attr(item, SuneerPhotoStore::AttrFileUrl);
		if (url.isEmpty())
		{
			lines << tr("%1: no Workflow URL stored on the frame").arg(item->itemName());
			continue;
		}
		QString why;
		const QString cached = downloadImage(url, &why, force);
		if (cached.isEmpty())
		{
			lines << tr("%1: %2").arg(item->itemName(), why);
			continue;
		}
		const int idx = attr(item, SuneerPhotoStore::AttrMediaIndex).toInt();
		const QString key = SuneerPhotoStore::storyKey(item);
		QString target = attr(item, SuneerPhotoStore::AttrOriginal);
		if (target.isEmpty() || SuneerPhotoStore::originalDir(m_doc, false).isEmpty())
			target = SuneerPhotoStore::importOriginal(m_doc, cached, key, idx, &why);
		else
		{
			if (force && QFileInfo::exists(target))
			{
				const QString prev = QFileInfo(target).absolutePath() + QStringLiteral("/") + QFileInfo(target).completeBaseName() + QStringLiteral("_prev.") + QFileInfo(target).suffix();
				QFile::remove(prev);
				QFile::rename(target, prev);
			}
			QFile::remove(target);
			const QString tmp = target + QStringLiteral(".part");
			QFile::remove(tmp);
			if (!QFile::copy(cached, tmp) || !QFile::rename(tmp, target))
			{
				lines << tr("%1: could not write %2").arg(item->itemName(), target);
				continue;
			}
		}
		setAttr(item, SuneerPhotoStore::AttrOriginal, target);
		if (item->Pfile.isEmpty() || !QFileInfo::exists(item->Pfile) || QFileInfo(item->Pfile).absoluteFilePath() == QFileInfo(target).absoluteFilePath())
		{
			m_doc->loadPict(target, item, true, true);
			item->adjustPictScale();
			item->update();
		}
		++done;
		apiLog(QStringLiteral("re-download: %1 -> %2").arg(url, target));
	}
	statusProgressDone();
	if (report)
		*report = lines.join(QStringLiteral("\n"));
	if (done)
	{
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
	}
	setStatus(tr("Re-downloaded %1 of %2 photo(s)").arg(done).arg(items.size()) + (lines.isEmpty() ? QString() : QStringLiteral(" \u00b7 ") + tr("%n problem(s)", "", lines.size())), !lines.isEmpty());
	if (!lines.isEmpty())
		setStatusDetails(lines.join(QStringLiteral("\n")));
	return done;
}

QString SuneerNewsPanel::downloadImage(const QString& fileUrl, QString* why, bool force)
{
	// A local file (fixture tests, pre-downloaded pictures) needs no download.
	if (fileUrl.startsWith('/') && QFileInfo(fileUrl).isFile())
		return fileUrl;
	// A Gallery card: the pictures were fetched before the copy of the page was made; no network
	// (no event loop) while m_doc is the scratch copy.
	if (m_preview)
	{
		const QString cached = cachedImagePath(fileUrl);
		if (cached.isEmpty() && why)
			*why = tr("not downloaded yet");
		return cached;
	}
	auto fail = [&](const QString& reason) {
		if (why)
			*why = reason;
		apiLog(QStringLiteral("image %1: FAILED - %2").arg(QUrl(fileUrl).path(), reason));
		return QString();
	};
	if (fileUrl.trimmed().isEmpty())
		return fail(tr("no file URL in the story"));
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
	// Cache key without the server's extension: the real format comes from
	// the bytes (the server hands out WebP as ".jpg"), and the file gets
	// the extension Scribus' loaders expect.
	QString stem = base;
	if (stem.contains('.'))
		stem = stem.left(stem.lastIndexOf('.'));
	const QString cacheKey = hash + "-" + stem;
	const QString metaPath = cacheDir + cacheKey + ".meta";
	QString localPath;
	{
		const QStringList found = QDir(cacheDir).entryList({ cacheKey + ".*" }, QDir::Files);
		for (const QString& f : found)
			if (!f.endsWith(".meta"))
			{
				localPath = cacheDir + f;
				break;
			}
	}
	if (localPath.isEmpty())
		localPath = cacheDir + cacheKey + "." + (base.contains('.') ? base.section('.', -1) : QStringLiteral("jpg"));

	// Candidate URLs: filesBase + fileUrl, and when fileUrl already starts
	// with /files (or is absolute) the plain form, so a doubled "/files/files"
	// never hides the picture.
	QStringList candidates;
	if (fileUrl.startsWith("http://") || fileUrl.startsWith("https://"))
		candidates << fileUrl;
	else
	{
		const QString rel = fileUrl.startsWith('/') ? fileUrl : "/" + fileUrl;
		candidates << m_filesBase + rel;
		const QUrl fb(m_filesBase);
		const QString basePath = fb.path();
		if (!basePath.isEmpty() && basePath != "/" && rel.startsWith(basePath + "/"))
			candidates << QStringLiteral("%1://%2%3").arg(fb.scheme(), fb.authority(), rel);
	}

	// A cached copy counts only when it decodes as a picture.
	if (force)
	{
		QFile::remove(localPath);
		QFile::remove(metaPath);
	}
	bool haveCached = QFile::exists(localPath) && QFileInfo(localPath).size() > 0;
	QString cachedFmt;
	if (haveCached)
	{
		QFile f(localPath);
		f.open(QIODevice::ReadOnly);
		cachedFmt = sniffImageFormat(f.read(16));
	}
	// A cached WebP (saved before the converter existed) or a file that is
	// no picture at all is thrown away and fetched again.
	if (haveCached && (cachedFmt == "webp" || (cachedFmt.isEmpty() && !QImageReader(localPath).canRead())))
	{
		apiLog(QStringLiteral("image %1: cached file %2 (%3 bytes) is %4 - discarded").arg(QUrl(fileUrl).path(), localPath).arg(QFileInfo(localPath).size()).arg(cachedFmt == "webp" ? QStringLiteral("WebP, not converted") : QStringLiteral("not a readable picture")));
		QFile::remove(localPath);
		QFile::remove(metaPath);
		haveCached = false;
	}
	else if (QFile::exists(localPath) && QFileInfo(localPath).size() == 0)
	{
		QFile::remove(localPath);
		QFile::remove(metaPath);
	}

	QString lastReason;
	for (int ci = 0; ci < candidates.size(); ++ci)
	{
		const QString fullUrl = candidates[ci];
		// Through the one request builder; the Bearer header goes along (the
		// file server ignores it today and may require it later).
		QNetworkRequest req = apiRequest(QUrl(fullUrl), true);
		req.setRawHeader("Accept", "image/*,*/*;q=0.8");
		req.setTransferTimeout(20000);
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
		req.setMaximumRedirectsAllowed(5);
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
		const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
		const QByteArray bytes = (reply->error() == QNetworkReply::NoError && status == 200) ? reply->readAll() : QByteArray();
		apiLog(QStringLiteral("image GET %1 -> HTTP %2 %3 %4 bytes%5%6")
		       .arg(QUrl(fullUrl).host() + QUrl(fullUrl).path()).arg(status).arg(ctype).arg(bytes.size())
		       .arg(haveCached ? QStringLiteral(" (cached %1 bytes)").arg(QFileInfo(localPath).size()) : QString())
		       .arg(reply->error() == QNetworkReply::NoError ? QString() : " " + reply->errorString()));
		if (status == 304 && haveCached)
			return localPath;
		if (reply->error() != QNetworkReply::NoError || status != 200)
		{
			if (haveCached)
			{
				apiLog(QStringLiteral("image %1: server unavailable, cached copy used").arg(QUrl(fileUrl).path()));
				return localPath;
			}
			lastReason = status > 0 ? tr("HTTP %1 %2").arg(status).arg(serverMessage(reply->peek(reply->bytesAvailable()))).trimmed()
			                        : describeError(reply);
			continue;   // next candidate URL, if any
		}
		if (bytes.isEmpty())
		{
			lastReason = tr("empty reply");
			continue;
		}
		// The format from the bytes, never from the name or content-type.
		QString ext = sniffImageFormat(bytes);
		QByteArray data = bytes;
		if (ext == "webp")
		{
			QString how, cwhy;
			data = convertWebpToPng(bytes, how, cwhy);
			if (data.isEmpty())
			{
				lastReason = tr("WebP picture, no decoder (%1)").arg(cwhy);
				continue;
			}
			apiLog(QStringLiteral("image %1: WebP (%2 bytes) converted to PNG (%3 bytes) via %4").arg(QUrl(fileUrl).path()).arg(bytes.size()).arg(data.size()).arg(how));
			ext = QStringLiteral("png");
		}
		else if (ext.isEmpty())
		{
			QBuffer buf;
			buf.setData(bytes);
			buf.open(QIODevice::ReadOnly);
			ext = QString::fromLatin1(QImageReader::imageFormat(&buf)).toLower();
			if (ext.isEmpty())
			{
				lastReason = tr("reply is not a picture (%1, %2 bytes, starts with %3)").arg(ctype.isEmpty() ? tr("no content-type") : ctype).arg(bytes.size()).arg(QString::fromLatin1(bytes.left(8).toHex()));
				continue;
			}
		}
		if (ext == "jpeg")
			ext = QStringLiteral("jpg");
		localPath = cacheDir + cacheKey + "." + ext;
		// Write beside, then rename: a half-written file is never a cache hit.
		QTemporaryFile tmp(cacheDir + "dl-XXXXXX");
		if (!tmp.open() || tmp.write(data) != data.size())
			return fail(tr("cannot write %1: %2").arg(cacheDir, tmp.errorString()));
		tmp.close();
		for (const QString& f : QDir(cacheDir).entryList({ cacheKey + ".*" }, QDir::Files))
			if (!f.endsWith(".meta"))
				QFile::remove(cacheDir + f);
		if (!QFile::rename(tmp.fileName(), localPath))
		{
			if (!QFile::copy(tmp.fileName(), localPath))
				return fail(tr("cannot write %1").arg(localPath));
		}
		tmp.setAutoRemove(true);
		QFile::setPermissions(localPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
		QSettings meta(metaPath, QSettings::IniFormat);
		meta.setValue("etag", QString::fromUtf8(reply->rawHeader("ETag")));
		meta.setValue("lastModified", QString::fromUtf8(reply->rawHeader("Last-Modified")));
		meta.setValue("url", fullUrl);
		meta.setValue("size", bytes.size());
		meta.setValue("format", ext);
		apiLog(QStringLiteral("image %1: saved %2 (%3 bytes, %4)").arg(QUrl(fileUrl).path(), localPath).arg(data.size()).arg(ext));
		return localPath;
	}
	if (haveCached)
		return localPath;
	return fail(lastReason.isEmpty() ? tr("download failed") : lastReason);
}

// ============================================================================
// Settings dialog
// ============================================================================

void SuneerNewsPanel::updateHostLabel()
{
	if (!m_hostLabel)
		return;
	const QUrl u(m_apiHost);
	QString host = u.host();
	if (host.isEmpty())
		host = m_apiHost;
	m_hostLabel->setText(tr("Server: %1").arg(host));
	m_hostLabel->setToolTip(tr("API: %1\nFiles: %2").arg(m_apiHost, m_filesBase));
}

bool SuneerNewsPanel::validServerUrl(const QString& url, QString* why)
{
	const QUrl u(url.trimmed());
	if (url.trimmed().isEmpty())
	{
		if (why) *why = tr("it is empty");
		return false;
	}
	if (!u.isValid() || u.host().isEmpty())
	{
		if (why) *why = tr("it is not a valid URL");
		return false;
	}
	if (u.scheme() != "http" && u.scheme() != "https")
	{
		if (why) *why = tr("it must start with http:// or https://");
		return false;
	}
	return true;
}

QString SuneerNewsPanel::probeUrl(const QString& url)
{
	QNetworkRequest req{QUrl(url)};
	req.setTransferTimeout(5000);
	QNetworkReply* reply = m_nam->get(req);
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	reply->deleteLater();
	const int status = httpStatus(reply);
	if (status > 0)
	{
		const QString phrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
		return QStringLiteral("HTTP %1 %2").arg(status).arg(phrase).trimmed();
	}
	return reply->errorString();
}

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
	QPushButton* hostDefBtn = new QPushButton(tr("Use default"), connTab);
	hostDefBtn->setToolTip(kDefaultApiHost);
	QHBoxLayout* hostRow = new QHBoxLayout();
	hostRow->addWidget(hostEdit, 1);
	hostRow->addWidget(hostDefBtn);
	fl->addRow(tr("API base URL:"), hostRow);
	QLineEdit* filesEdit = new QLineEdit(m_filesBase, connTab);
	filesEdit->setPlaceholderText("http://IP/files");
	QPushButton* filesDefBtn = new QPushButton(tr("Use default"), connTab);
	filesDefBtn->setToolTip(tr("Derive the files base from the API server"));
	QHBoxLayout* filesRow = new QHBoxLayout();
	filesRow->addWidget(filesEdit, 1);
	filesRow->addWidget(filesDefBtn);
	fl->addRow(tr("Files base URL:"), filesRow);
	connect(hostDefBtn, &QPushButton::clicked, connTab, [hostEdit, filesEdit]() {
		hostEdit->setText(kDefaultApiHost);
		filesEdit->setText(defaultFilesBaseFor(kDefaultApiHost));
	});
	connect(filesDefBtn, &QPushButton::clicked, connTab, [hostEdit, filesEdit]() {
		filesEdit->setText(defaultFilesBaseFor(hostEdit->text().trimmed()));
	});
	// Default edition: the short name opened when the panel starts.
	QComboBox* editionCmb = new QComboBox(connTab);
	editionCmb->setEditable(true);
	{
		QStringList names;
		for (int i = 0; i < (m_editionCombo ? m_editionCombo->count() : 0); ++i)
			names << m_editionCombo->itemData(i).toString();
		if (names.isEmpty())
			names = m_editionShortNames.split(',', Qt::SkipEmptyParts);
		for (QString& n : names)
			n = n.trimmed();
		names.removeAll(QString());
		editionCmb->addItems(names);
		editionCmb->setEditText(m_lastEdition);
	}
	fl->addRow(tr("Default edition:"), editionCmb);
	QLabel* urlErr = new QLabel(connTab);
	urlErr->setWordWrap(true);
	urlErr->setStyleSheet("color:#C62828; font-size:11px;");
	urlErr->setVisible(false);
	fl->addRow(urlErr);
	QPushButton* testBtn = new QPushButton(tr("Test connection"), connTab);
	QLabel* testLbl = new QLabel(connTab);
	testLbl->setWordWrap(true);
	testLbl->setStyleSheet("font-size:11px;");
	QHBoxLayout* testRow = new QHBoxLayout();
	testRow->addWidget(testBtn);
	testRow->addWidget(testLbl, 1);
	fl->addRow("", testRow);
	connect(testBtn, &QPushButton::clicked, connTab, [this, hostEdit, filesEdit, testLbl, testBtn]() {
		QString host = hostEdit->text().trimmed();
		while (host.endsWith('/')) host.chop(1);
		QString files = filesEdit->text().trimmed();
		while (files.endsWith('/')) files.chop(1);
		QString why;
		if (!validServerUrl(host, &why))
		{
			testLbl->setText(tr("API base URL: %1").arg(why));
			return;
		}
		testBtn->setEnabled(false);
		testLbl->setText(tr("Testing..."));
		QApplication::setOverrideCursor(Qt::WaitCursor);
		const QString apiRes = probeUrl(host + "/api/v1/common/web/initial-data/static");
		const QString filesRes = validServerUrl(files, &why) ? probeUrl(files + "/") : why;
		QApplication::restoreOverrideCursor();
		testBtn->setEnabled(true);
		testLbl->setText(tr("API: %1\nFiles: %2").arg(apiRes, filesRes));
	});
	QLineEdit* shortNamesEdit = new QLineEdit(m_editionShortNames, connTab);
	shortNamesEdit->setToolTip(tr("Used only when the edition list cannot be loaded from the server"));
	fl->addRow(tr("Edition short names (fallback):"), shortNamesEdit);
	// Open page from workflow: <folder>/<edition>/<date>/<page>.sla
	QLineEdit* pagesEdit = new QLineEdit(m_pagesFolder, connTab);
	pagesEdit->setPlaceholderText(defaultPagesFolder());
	pagesEdit->setToolTip(tr("Open page from workflow saves <this folder>/<edition>/<date>/<page>.sla (pictures in <page>_images). Empty = %1").arg(defaultPagesFolder()));
	QPushButton* pagesBrowse = new QPushButton(tr("Browse..."), connTab);
	QHBoxLayout* pagesRow = new QHBoxLayout();
	pagesRow->addWidget(pagesEdit, 1);
	pagesRow->addWidget(pagesBrowse);
	fl->addRow(tr("Pages folder:"), pagesRow);
	connect(pagesBrowse, &QPushButton::clicked, connTab, [this, pagesEdit]() {
		const QString start = pagesEdit->text().trimmed().isEmpty() ? QDir::homePath() : pagesEdit->text().trimmed();
		const QString d = QFileDialog::getExistingDirectory(this, tr("Pages folder"), NetPathGuard::reachable(start) ? start : QDir::homePath());
		if (!d.isEmpty())
			pagesEdit->setText(d);
	});
	// Page sync: the "Keep" answers (used in the workflow, not on the page, left used) - forget them all
	QLabel* keptLbl = new QLabel(connTab);
	QPushButton* forgetBtn = new QPushButton(tr("Forget Keep..."), connTab);
	forgetBtn->setObjectName("forgetKeepBtn");
	forgetBtn->setToolTip(tr("The stories you answered Keep for (marked used but not on the page) are asked about again on the next sync"));
	auto showKept = [this, keptLbl, forgetBtn]() {
		const int n = m_syncKept.size();
		keptLbl->setText(n == 0 ? tr("No story kept as used") : n == 1 ? tr("1 story kept as used (Keep)") : tr("%1 stories kept as used (Keep)").arg(n));
		forgetBtn->setEnabled(!m_syncKept.isEmpty());
	};
	showKept();
	QHBoxLayout* keptRow = new QHBoxLayout();
	keptRow->addWidget(keptLbl, 1);
	keptRow->addWidget(forgetBtn);
	fl->addRow(tr("Page sync:"), keptRow);
	connect(forgetBtn, &QPushButton::clicked, connTab, [this, showKept]() {
		const bool test = m_syncTestAnswer.contains(QLatin1String("forget"));
		if (!test && QMessageBox::question(this, tr("Forget Keep"),
		        (m_syncKept.size() == 1 ? tr("Forget Keep for 1 story?") : tr("Forget Keep for %1 stories?").arg(m_syncKept.size()))
		        + QStringLiteral("\n\n") + tr("The next sync of their page lists them again under "
		           "\"marked used but not on the page\" (nothing is changed in the workflow now)."),
		        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;
		const int n = forgetSyncKept();
		m_syncTestLog << QStringLiteral("forgot keep: %1").arg(n);
		showKept();
		setStatus(n == 1 ? tr("Forgot Keep for 1 story") : tr("Forgot Keep for %1 stories").arg(n));
	});
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
	colsSpin->setValue(s.value("layout/storyColumns", 4).toInt());
	colsSpin->setToolTip(tr("Grid columns of the story frame (its inner columns use the column gap below)"));
	fl2->addRow(tr("Story columns:"), colsSpin);
	QSpinBox* imgColsSpin = new QSpinBox(layoutTab);
	imgColsSpin->setRange(1, 8);
	imgColsSpin->setValue(s.value("layout/photoColumns", 3).toInt());
	fl2->addRow(tr("Photo columns:"), imgColsSpin);
	QComboBox* posCmb = new QComboBox(layoutTab);
	for (const QString& key : placePhotoPositionKeys())
		posCmb->addItem(photoPositionLabel(key), key);
	{
		int pi = posCmb->findData(settingsString("layout/photoPosition", "right"));
		posCmb->setCurrentIndex(pi >= 0 ? pi : 0);
	}
	fl2->addRow(tr("Photo position:"), posCmb);
	QCheckBox* unusedOnDeleteChk = new QCheckBox(tr("Mark unused when deleted from page"), layoutTab);
	unusedOnDeleteChk->setChecked(s.value("news/markUnusedOnDelete", true).toBool());
	unusedOnDeleteChk->setToolTip(tr("A placed story whose last frame is deleted from the document (Delete key, Cut without Paste, Delete page) is marked unused on the server by itself, about 2 seconds later. Undo brings it back and marks it used again. Off: delete never touches the server."));
	fl2->addRow(QString(), unusedOnDeleteChk);
	QSpinBox* gridColsSpin = new QSpinBox(layoutTab);
	gridColsSpin->setRange(1, 16);
	gridColsSpin->setValue(s.value("layout/gridColumns", 8).toInt());
	gridColsSpin->setToolTip(tr("Used when the page has no automatic column guides"));
	fl2->addRow(tr("Page grid columns:"), gridColsSpin);
	QDoubleSpinBox* gutterSpin = new QDoubleSpinBox(layoutTab);
	gutterSpin->setRange(0.0, 30.0);
	gutterSpin->setSuffix(" mm");
	const SuneerLayoutModel normalModel = SuneerLayoutModels::byId(QStringLiteral("normal"));
	gutterSpin->setValue(normalModel.gutterMm);
	gutterSpin->setToolTip(tr("The Normal (4+3) layout model's value (Layout > Layout Models...)"));
	fl2->addRow(tr("Page grid gutter:"), gutterSpin);
	QDoubleSpinBox* innerGapSpin = new QDoubleSpinBox(layoutTab);
	innerGapSpin->setRange(0.0, 20.0);
	innerGapSpin->setSuffix(" mm");
	innerGapSpin->setValue(normalModel.columnGapMm);
	innerGapSpin->setToolTip(tr("The Normal (4+3) layout model's value (Layout > Layout Models...)"));
	fl2->addRow(tr("Column gap inside the story frame:"), innerGapSpin);
	QDoubleSpinBox* maxPhotoSpin = new QDoubleSpinBox(layoutTab);
	maxPhotoSpin->setRange(20.0, 400.0);
	maxPhotoSpin->setSuffix(" mm");
	maxPhotoSpin->setValue(s.value("layout/photoMaxHeightMm", 100.0).toDouble());
	fl2->addRow(tr("Photo height at most (taller: centre crop):"), maxPhotoSpin);
	QDoubleSpinBox* wrapGapSpin = new QDoubleSpinBox(layoutTab);
	wrapGapSpin->setRange(0.0, 10.0);
	wrapGapSpin->setSuffix(" mm");
	wrapGapSpin->setValue(normalModel.wrapPadMm);
	wrapGapSpin->setToolTip(tr("The Normal (4+3) layout model's value (Layout > Layout Models...)"));
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

	// ---- Obituary model (obituary.json) -----------------------------------
	QWidget* obitTab = new QWidget();
	QFormLayout* flo = new QFormLayout(obitTab);
	flo->setSpacing(10);
	const SuneerObitModel om = SuneerObitModel::load();
	QComboBox* obitAlignCmb = new QComboBox(obitTab);
	obitAlignCmb->addItem(tr("Centred (house style)"), "centre");
	obitAlignCmb->addItem(tr("Left aligned"), "left");
	obitAlignCmb->setCurrentIndex(qMax(0, obitAlignCmb->findData(om.textAlign)));
	obitAlignCmb->setToolTip(tr("Left aligned: name on its own line, then the details left aligned. Local paragraph alignment; the house styles are not edited."));
	flo->addRow(tr("Text alignment:"), obitAlignCmb);
	QCheckBox* obitRuninChk = new QCheckBox(tr("Run-in place: \"Place: details...\" on one line (character style %1)").arg(om.stylePlaceRunin), obitTab);
	obitRuninChk->setChecked(om.runInPlace);
	flo->addRow(QString(), obitRuninChk);
	QComboBox* obitBgCmb = new QComboBox(obitTab);
	obitBgCmb->setEditable(true);
	{
		QStringList names{QStringLiteral("Obit BG"), QStringLiteral("DB obit")};
		if (m_doc)
			for (auto it = m_doc->PageColors.cbegin(); it != m_doc->PageColors.cend(); ++it)
				if (!names.contains(it.key()))
					names << it.key();
		obitBgCmb->addItems(names);
		obitBgCmb->setItemText(0, QStringLiteral("Obit BG"));
		obitBgCmb->setCurrentText(om.photoBackground);
		obitBgCmb->setToolTip(tr("Uniform tone behind every obit photo. \"Obit BG\" = the measured grey (RGB %6 in the files), a document colour C%1 M%2 Y%3 K%4 added when missing (editable in the Colours palette); \"DB obit\" = the old house tone; or any document colour. Stored in %5")
		                      .arg(om.bgCMYK[0]).arg(om.bgCMYK[1]).arg(om.bgCMYK[2]).arg(om.bgCMYK[3]).arg(SuneerObitModel::filePath())
		                      .arg(QStringLiteral("%1,%2,%3").arg(om.bgRGB[0]).arg(om.bgRGB[1]).arg(om.bgRGB[2])));
	}
	flo->addRow(tr("Photo background:"), obitBgCmb);
	QSlider* obitToneSlider = new QSlider(Qt::Horizontal, obitTab);
	obitToneSlider->setRange(0, 100);
	obitToneSlider->setValue(om.toneStrength);
	obitToneSlider->setTickInterval(10);
	obitToneSlider->setTickPosition(QSlider::TicksBelow);
	QLabel* obitToneVal = new QLabel(QString::number(om.toneStrength), obitTab);
	connect(obitToneSlider, &QSlider::valueChanged, obitToneVal, [obitToneVal](int v) { obitToneVal->setText(QString::number(v)); });
	QHBoxLayout* toneRow = new QHBoxLayout();
	toneRow->addWidget(obitToneSlider, 1);
	toneRow->addWidget(obitToneVal);
	obitToneSlider->setToolTip(tr("Tone matching of the person after background removal: white balance, auto levels (1 % clip), gentle contrast, one target face brightness, slight sharpening. 0 = off, 100 = full."));
	flo->addRow(tr("Tone matching strength:"), toneRow);
	QComboBox* obitFillCmb = new QComboBox(obitTab);
	obitFillCmb->addItem(tr("Tighter (face box 66 % of the width)"), 0.66);
	obitFillCmb->addItem(tr("Reference (face box 60 % of the width)"), 0.60);
	obitFillCmb->addItem(tr("Looser (face box 54 % of the width)"), 0.54);
	{
		int best = 1;
		for (int k = 0; k < obitFillCmb->count(); ++k)
			if (qAbs(obitFillCmb->itemData(k).toDouble() - om.faceW) < 0.02)
				best = k;
		obitFillCmb->setCurrentIndex(best);
	}
	obitFillCmb->setToolTip(tr("Face crop in Haar frontal-face box terms (measured on the reference strip): the face box is this share of the frame width, centred, its top %1 %% of the frame width below the frame top - with a little room above the hair (tall hair: the crop moves up or zooms out a little, the top of the head is never cut), only neck and collar below the chin. Sources smaller than 300 ppi get the same crop and a red \"low res\" flag.").arg(qRound(om.faceTop * 100)));
	flo->addRow(tr("Face fill:"), obitFillCmb);
	QComboBox* obitShapeCmb = new QComboBox(obitTab);
	obitShapeCmb->addItem(tr("Square (%1 x %1 mm)").arg(om.photoMm), "square");
	obitShapeCmb->addItem(tr("Portrait (%1 x %2 mm)").arg(om.photoMm).arg(om.portraitHeightMm), "portrait");
	obitShapeCmb->setCurrentIndex(qMax(0, obitShapeCmb->findData(om.frameShape)));
	flo->addRow(tr("Photo frame:"), obitShapeCmb);
	QComboBox* obitBgPolicyCmb = new QComboBox(obitTab);
	obitBgPolicyCmb->addItem(tr("Auto"), "auto");
	obitBgPolicyCmb->addItem(tr("Ask when quality is low"), "ask");
	obitBgPolicyCmb->addItem(tr("Manual by default"), "manual");
	obitBgPolicyCmb->setCurrentIndex(qMax(0, obitBgPolicyCmb->findData(om.bgPolicy)));
	obitBgPolicyCmb->setToolTip(tr("Auto: AI removes every background. Ask: when the AI keeps less than 25% of the crop or cuts through the face, the photo is placed unremoved and flagged \"check BG\". Manual: no AI; photos are placed as they are and marked Manual for Edit photo."));
	flo->addRow(tr("Background removal:"), obitBgPolicyCmb);
	QCheckBox* obitAutoWfChk = new QCheckBox(tr("Process automatically when opening an obituary page from the workflow (asks once per page)"), obitTab);
	obitAutoWfChk->setChecked(s.value("obit/autoProcessWorkflow", true).toBool());
	obitAutoWfChk->setToolTip(tr("After \"Open page from workflow\": when its obituary photos have no background removal yet, ask \"N photos need background removal - process now?\" (once per page and date)"));
	flo->addRow(obitAutoWfChk);
	QLabel* obitNote = new QLabel(tr("Changes apply to the next Place and to \"Re-process photos\". Model file: %1").arg(SuneerObitModel::filePath()), obitTab);
	obitNote->setWordWrap(true);
	obitNote->setStyleSheet("color:#666; font-size:11px;");
	flo->addRow(obitNote);
	tabs->addTab(obitTab, tr("⚰ Obituary"));

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
	// OK is refused while either URL is unusable; the reason is shown in red
	// on the Connection tab (same pattern as the updater's settings dialog).
	connect(bb, &QDialogButtonBox::accepted, &dlg, [&dlg, hostEdit, filesEdit, urlErr, tabs, connTab]() {
		QString host = hostEdit->text().trimmed();
		if (!host.isEmpty() && !host.startsWith("http://") && !host.startsWith("https://"))
			host.prepend("http://");
		QString why;
		if (!validServerUrl(host, &why))
		{
			urlErr->setText(tr("The API base URL is not used because %1. Correct it or click Use default.").arg(why));
			urlErr->setVisible(true);
			tabs->setCurrentWidget(connTab);
			return;
		}
		const QString files = filesEdit->text().trimmed();
		if (!files.isEmpty() && !validServerUrl(files, &why))
		{
			urlErr->setText(tr("The files base URL is not used because %1. Clear it or click Use default.").arg(why));
			urlErr->setVisible(true);
			tabs->setCurrentWidget(connTab);
			return;
		}
		dlg.accept();
	});
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	mainVl->addWidget(bb);

	if (dlg.exec() != QDialog::Accepted)
		return;
	s.setValue("obit/autoProcessWorkflow", obitAutoWfChk->isChecked());

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
	m_pagesFolder = QDir::cleanPath(pagesEdit->text().trimmed());
	if (m_pagesFolder == QLatin1String("."))
		m_pagesFolder.clear();
	{
		const QString ed = editionCmb->currentText().trimmed();
		if (!ed.isEmpty())
			m_lastEdition = ed;
	}
	saveConfig();
	updateHostLabel();
	s.setValue("layout/storyColumns", colsSpin->value());
	s.setValue("layout/photoColumns", imgColsSpin->value());
	s.setValue("news/markUnusedOnDelete", unusedOnDeleteChk->isChecked());
	s.setValue("layout/placeTarget", targetCmb->currentData().toString());
	s.setValue("layout/photoPosition", posCmb->currentData().toString());
	{
		// these three are the Normal model's (one place for every number: the layout models)
		SuneerLayoutModel nm = SuneerLayoutModels::byId(QStringLiteral("normal"));
		const SuneerLayoutModel before = nm;
		nm.columnGapMm = innerGapSpin->value();
		nm.gutterMm = gutterSpin->value();
		nm.wrapPadMm = wrapGapSpin->value();
		nm.columns = gridColsSpin->value();
		nm.fixedColumns = colsSpin->value();
		nm.fixedPhotoColumns = imgColsSpin->value();
		nm.widthRules = { {999999, nm.fixedColumns} };
		nm.photoPosition = posCmb->currentData().toString();
		nm.photoMaxHeightMm = maxPhotoSpin->value();
		if (QJsonDocument(nm.toJson()).toJson() != QJsonDocument(before.toJson()).toJson())
			SuneerLayoutModels::save(nm);
	}
	s.setValue("layout/gridColumns", gridColsSpin->value());
	s.setValue("layout/photoMaxHeightMm", maxPhotoSpin->value());
	{
		SuneerObitModel om2 = SuneerObitModel::load();
		const QString bg = obitBgCmb->currentText().trimmed();
		if (!bg.isEmpty())
			om2.photoBackground = bg;
		om2.textAlign = obitAlignCmb->currentData().toString();
		om2.runInPlace = obitRuninChk->isChecked();
		om2.toneStrength = obitToneSlider->value();
		om2.faceW = obitFillCmb->currentData().toDouble();
		om2.frameShape = obitShapeCmb->currentData().toString();
		om2.bgPolicy = obitBgPolicyCmb->currentData().toString();
		QString err;
		if (!om2.save(&err))
			setStatus(err);
		updateLayoutModelInfo();
	}
	s.setValue("image/height", imgHspin->value());
	s.setValue("style/titleStyle", titleStyleEdit->text().trimmed());
	s.setValue("style/kickerStyle", kickerStyleEdit->text().trimmed());
	s.setValue("style/highlightsStyle", highlightStyleEdit->text().trimmed());
	s.setValue("style/bylineStyle", bylineStyleEdit->text().trimmed());
	s.setValue("style/firstBodyStyle", firstBodyStyleEdit->text().trimmed());
	s.setValue("style/bodyStyle", bodyStyleEdit->text().trimmed());
	s.setValue("style/captionStyle", captionStyleEdit->text().trimmed());
	s.setValue("style/datelineStyle", datelineStyleEdit->text().trimmed());
	setStatus(tr("Settings saved"));
	if (hostChanged)
	{
		// A session from one server means nothing to another.
		if (isLoggedIn())
			onLogout();
		return;
	}
	onRefresh();
}

// ── welded stories ──────────────────────────────────────────────────────────

int SuneerNewsPanel::weldStoryItems(const QList<PageItem*>& items)
{
	if (!m_doc)
		return 0;
	QList<PageItem*> frames;
	for (PageItem* it : items)
		if (it && !it->isGroup())
			frames << it;
	if (frames.size() < 2)
		return 0;
	// already one piece? (every frame reachable from the first)
	const QList<PageItem*> have = SuneerWeld::cluster(frames.first());
	bool whole = true;
	for (PageItem* it : frames)
		if (!have.contains(it)) { whole = false; break; }
	if (whole)
		return 0;
	const int made = SuneerWeld::weldStory(m_doc, frames);
	if (made)
	{
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
	}
	return made;
}

void SuneerNewsPanel::weldStoryOf(PageItem* item)
{
	if (!m_doc || !isNewsStory(item))
		return;
	const int made = weldStoryItems(storyItems(attr(item, "news.story")));
	setStatus(made ? tr("Story welded (%1 welds)").arg(made) : tr("Story is already welded"));
}

void SuneerNewsPanel::unweldStoryOf(PageItem* item)
{
	if (!m_doc || !isNewsStory(item))
		return;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, Um::UnweldItems, QString(), Um::IDelete);
	const int n = SuneerWeld::unweldStory(storyItems(attr(item, "news.story")));
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	setStatus(n ? tr("Story unwelded: every frame moves on its own now") : tr("Nothing to unweld"));
}

// A group that holds a story: tagged itself (obituary modules of earlier builds), or made by hand
// around tagged frames (Item > Group). Returns the story id, empty for other groups.
QString SuneerNewsPanel::groupStoryId(const PageItem* group)
{
	if (!group || !group->isGroup())
		return QString();
	const QString own = attr(group, "news.story");
	if (!own.isEmpty())
		return own;
	for (PageItem* c : const_cast<PageItem*>(group)->getAllChildren())
		if (c && !attr(c, "news.story").isEmpty())
			return attr(c, "news.story");
	return QString();
}

// Old documents: a story placed as a GROUP (obituary modules of earlier builds) becomes welded
// frames: ungroup, then weld. Selected groups, or every story group on the current page.
int SuneerNewsPanel::convertStoryGroupsToWeld(bool wholePage)
{
	if (!m_doc)
		return 0;
	QList<PageItem*> groups;
	if (wholePage)
	{
		if (!m_doc->currentPage())
			return 0;
		const ScPage* page = m_doc->currentPage();
		const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
		for (int i = 0; i < m_doc->Items->count(); ++i)
		{
			PageItem* it = m_doc->Items->at(i);
			if (it && it->isGroup() && !groupStoryId(it).isEmpty() && it->getBoundingRect().intersects(pageRect))
				groups << it;
		}
	}
	else
		for (int i = 0; i < m_doc->m_Selection->count(); ++i)
		{
			PageItem* it = m_doc->m_Selection->itemAt(i);
			if (it && it->isGroup() && !groupStoryId(it).isEmpty())
				groups << it;
		}
	if (groups.isEmpty())
	{
		setStatus(wholePage ? tr("No story groups on this page") : tr("Select a story group first"));
		return 0;
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Convert story groups to weld"), QString(), Um::IGroup);
	int done = 0;
	for (PageItem* g : groups)
	{
		const QString story = groupStoryId(g);
		QList<PageItem*> children = g->getAllChildren();
		Selection sel(this, false);
		sel.addItem(g);
		m_doc->itemSelection_UnGroupObjects(&sel);
		QList<PageItem*> frames;
		for (PageItem* c : children)
			if (c && !c->isGroup() && m_doc->Items->contains(c))
			{
				if (attr(c, "news.story").isEmpty())
					setAttr(c, "news.story", story);
				frames << c;
			}
		// stale welds from the grouped days (partners that no longer resolve) go first, then one clean star
		SuneerWeld::unweldStory(frames);
		SuneerWeld::weldStory(m_doc, frames);
		indexAdd(frames);
		++done;
	}
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->HaveNewSel();
	setStatus(tr("%1 story group(s) converted to welded frames").arg(done));
	return done;
}

// ============================================================================
// Open page from workflow
// ============================================================================
// The page's .sla as the workflow's layout editor exports it ("Download SLA"):
//   GET /news-layout-settings/page-context?pageId&publishDate  -> the day's template
//       (slaTemplateId; when the day was never settled, the page's last-used one,
//       defaultSlaTemplateId - exactly the editor's own rule; templateChosen with no
//       id = an explicit "no template" for the day)
//   GET /sla-templates/:id/download?date&pageId  -> application/octet-stream (.sla) or
//       application/zip (stored) = <page>_<date>.sla + images/<n>_<file> when photos are
//       placed; the .sla refers to them as PFILE="images/..." (relative to its folder).
// Both need the web login (Bearer) and layout.view (+ the edition's layoutEditor feature).
// Fonts are referenced by name only (they must be installed), nothing else is downloaded.
// Saved as <pages folder>/<edition>/<date>/<page>.sla with its pictures in <page>_images/,
// the workflow ids written into DOCUMENT (SuneerWf*), then opened with the page lock.

QString SuneerNewsPanel::defaultPagesFolder()
{
	return QDir::homePath() + QStringLiteral("/WorkflowPages");
}

// A page / edition name as a file name: letters (Malayalam too), digits, '-' and '_' stay,
// everything else (spaces, '/', ':', quotes, '&') becomes '_'. Never empty, never "." / "..".
QString SuneerNewsPanel::safeFileName(const QString& name)
{
	QString out;
	for (const QChar ch : name.trimmed())
		out += (ch.isLetterOrNumber() || ch.isMark() || ch == QLatin1Char('-') || ch == QLatin1Char('_')) ? ch : QLatin1Char('_');
	while (out.contains(QStringLiteral("__")))
		out.replace(QStringLiteral("__"), QStringLiteral("_"));
	while (out.startsWith(QLatin1Char('_')))
		out.remove(0, 1);
	while (out.endsWith(QLatin1Char('_')))
		out.chop(1);
	return out.isEmpty() ? QStringLiteral("page") : out;
}

QString SuneerNewsPanel::wfErrorText(QNetworkReply* reply, bool timedOut, int timeoutMs) const
{
	if (timedOut)
		return tr("The workflow did not answer within %1 s (%2)").arg(timeoutMs / 1000).arg(QUrl(m_apiHost).host());
	const int status = httpStatus(reply);
	const QString msg = serverMessage(reply->peek(reply->bytesAvailable()));
	switch (status)
	{
		case 401:
			return tr("Login expired or missing (HTTP 401) - log in again in the News Browser");
		case 403:
			return tr("Not allowed (HTTP 403): %1").arg(msg.isEmpty() ? tr("your account cannot read layout pages / templates") : msg);
		case 404:
			return tr("Not found on the workflow (HTTP 404): %1").arg(msg.isEmpty() ? reply->url().path() : msg);
		case 400:
			return tr("The workflow refused it (HTTP 400): %1").arg(msg.isEmpty() ? reply->errorString() : msg);
		default:
			break;
	}
	if (status >= 500)
		return tr("Workflow server error (HTTP %1)%2").arg(status).arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
	return describeError(reply);
}

void SuneerNewsPanel::wfGet(const QUrl& url, int timeoutMs, std::function<void(QNetworkReply*)> done, bool retried)
{
	QNetworkRequest req = apiRequest(url, true);
	req.setRawHeader("Accept", "application/json, application/octet-stream, application/zip");
	req.setTransferTimeout(timeoutMs);   // no data for that long -> aborted (OperationCanceledError)
	QNetworkReply* reply = m_nam->get(req);
	auto t = std::make_shared<QElapsedTimer>();
	t->start();
	connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
		if (total > 0)
			statusProgress(tr("Downloading (KB)"), int(got / 1024), int(qMax<qint64>(1, total / 1024)));
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, url, timeoutMs, done, retried, t]() {
		reply->deleteLater();
		const int status = httpStatus(reply);
		// path only: the query has ids and a date, never the token (that is a header)
		apiLog(QStringLiteral("open from workflow: GET %1 -> %2 (%3 ms, %4 bytes)").arg(url.path(),
		       status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString()).arg(t->elapsed()).arg(reply->bytesAvailable()));
		if ((status == 401 || status == 403) && !retried && !m_refreshToken.isEmpty()
		    && serverMessage(reply->peek(reply->bytesAvailable())).contains(QLatin1String("token"), Qt::CaseInsensitive))
		{
			m_quietAuth = true;
			const bool ok = refreshAccessToken();
			m_quietAuth = false;
			if (ok)
			{
				wfGet(url, timeoutMs, done, true);
				return;
			}
		}
		if (done)
			done(reply);
	});
}

bool SuneerNewsPanel::writeWorkflowPage(const QByteArray& body, const QString& slaPath, const QMap<QString, QString>& ids, QString* error, int* images)
{
	auto fail = [error](const QString& e) { if (error) *error = e; return false; };
	if (images)
		*images = 0;
	const QFileInfo fi(slaPath);
	const QString base = fi.completeBaseName();
	const QString imgDirName = base + QStringLiteral("_images");
	QByteArray xml;
	QList<QPair<QString, QByteArray>> pictures;   // name inside <base>_images/, bytes
	if (body.startsWith("PK\x03\x04"))
	{
		QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/scribus-wfpage-XXXXXX.zip"));
		if (!tmp.open() || tmp.write(body) != body.size())
			return fail(tr("could not keep the download for unpacking (%1)").arg(tmp.errorString()));
		tmp.flush();
		ScZipHandler zip;
		if (!zip.open(tmp.fileName()))
			return fail(tr("the workflow sent a zip that cannot be opened"));
		for (const QString& name : zip.files())
		{
			QByteArray data;
			if (!zip.read(name, data))
				return fail(tr("could not read %1 from the zip").arg(name));
			const QString clean = QDir::cleanPath(name);
			if (clean.startsWith(QLatin1String("..")) || clean.startsWith(QLatin1Char('/')) || clean.contains(QLatin1String("/../")))
				return fail(tr("unsafe name in the zip: %1").arg(name));
			if (clean.endsWith(QLatin1String(".sla"), Qt::CaseInsensitive) && !clean.contains(QLatin1Char('/')))
				xml = data;
			else if (clean.startsWith(QLatin1String("images/")) && clean.count(QLatin1Char('/')) == 1 && clean.size() > 7)
				pictures << qMakePair(clean.mid(7), data);
			else if (!clean.endsWith(QLatin1Char('/')))
				apiLog(QStringLiteral("open from workflow: zip entry %1 ignored").arg(name));
		}
		zip.close();
		if (xml.isEmpty())
			return fail(tr("the zip from the workflow has no .sla in it"));
	}
	else
		xml = body;
	if (!xml.contains("<SCRIBUSUTF8NEW"))
		return fail(tr("the workflow did not send a Scribus document (%1 bytes, starts with \"%2\")")
		            .arg(body.size()).arg(QString::fromUtf8(body.left(40)).simplified()));
	// The pictures go next to the .sla in <base>_images/ (a folder per page: two pages of one
	// day would otherwise share images/0_*.jpg), so the references move with them.
	if (!pictures.isEmpty())
		xml.replace("PFILE=\"images/", "PFILE=\"" + imgDirName.toUtf8() + "/");
	// The workflow ids on the document element (1.7: <Document>, 1.6: <DOCUMENT>).
	int at = xml.indexOf("<Document ");
	if (at < 0)
		at = xml.indexOf("<DOCUMENT ");
	if (at >= 0)
	{
		QByteArray attrs;
		for (const QString& key : ScribusDoc::suneerWorkflowKeys())
		{
			const QString v = ids.value(key);
			if (v.isEmpty())
				continue;
			QString esc = v.toHtmlEscaped();   // & < > "
			esc.replace(QLatin1Char('\''), QStringLiteral("&#39;"));
			attrs += ' ' + ScribusDoc::suneerWorkflowAttr(key).toUtf8() + "=\"" + esc.toUtf8() + '"';
		}
		xml.insert(at + 9, attrs);
	}
	if (!QDir().mkpath(fi.absolutePath()))
		return fail(tr("cannot create the folder %1").arg(fi.absolutePath()));
	if (!pictures.isEmpty())
	{
		const QString imgDir = fi.absolutePath() + QLatin1Char('/') + imgDirName;
		if (!QDir().mkpath(imgDir))
			return fail(tr("cannot create the folder %1").arg(imgDir));
		for (const auto& p : std::as_const(pictures))
		{
			QSaveFile f(imgDir + QLatin1Char('/') + p.first);
			if (!f.open(QIODevice::WriteOnly) || f.write(p.second) != p.second.size() || !f.commit())
				return fail(tr("cannot write %1 (%2)").arg(f.fileName(), f.errorString()));
		}
		if (images)
			*images = pictures.size();
	}
	QSaveFile f(slaPath);
	if (!f.open(QIODevice::WriteOnly) || f.write(xml) != xml.size() || !f.commit())
		return fail(tr("cannot write %1 (%2)").arg(slaPath, f.errorString()));
	return true;
}

bool SuneerNewsPanel::openWorkflowPage(const QString& path, const QMap<QString, QString>& ids)
{
	if (!m_mw)
		return false;
	// Already open here ("Open existing"): its window to the front, without loadDoc's
	// "Document is already opened" message.
	bool switched = false;
	for (QMdiSubWindow* sw : m_mw->mdiArea->subWindowList())
	{
		auto* win = qobject_cast<ScribusWin*>(sw->widget());
		if (win && win->doc() && QFileInfo(win->doc()->documentFileName()).canonicalFilePath() == QFileInfo(path).canonicalFilePath())
		{
			m_mw->mdiArea->setActiveSubWindow(sw);
			QCoreApplication::processEvents();
			switched = true;
			break;
		}
	}
	m_wfOpening = true;                        // noteDocOpened: this route syncs on its own
	const bool loaded = switched || m_mw->loadDoc(path);
	m_wfOpening = false;
	if (!loaded)
		return false;
	ScribusDoc* doc = m_mw->doc;
	if (!doc || QFileInfo(doc->documentFileName()).canonicalFilePath() != QFileInfo(path).canonicalFilePath())
		return false;
	// In memory too: an older file ("Open existing") or one read by another loader has none;
	// they reach the file with the next Save.
	if (doc->suneerWorkflow() != ids)
		doc->setSuneerWorkflow(ids);
	// The page lock goes with this document: closing it releases the page.
	if (m_heldPageId == ids.value("pageId") && m_heldDate == ids.value("date"))
	{
		m_heldDoc = doc;
		m_heldDocBound = true;
	}
	return true;
}

bool SuneerNewsPanel::confirmWorkflowPage(const QString& action)
{
	if (!m_doc || m_fetchPageId.isEmpty())
		return true;
	const QMap<QString, QString> wf = m_doc->suneerWorkflow();
	const QString pid = wf.value("pageId"), date = wf.value("date");
	if (pid.isEmpty() || (pid == m_fetchPageId && (date.isEmpty() || date == m_fetchDate)))
		return true;
	const QString key = QStringLiteral("%1|%2|%3").arg(quintptr(m_doc.data())).arg(m_fetchPageId, m_fetchDate);
	if (m_wfPageAccepted.contains(key))
		return true;
	int listIdx = m_pageCombo->findData(m_fetchPageId);
	const QString listName = listIdx >= 0 ? pageNameAt(listIdx) : m_fetchPageId;
	QMessageBox box(QMessageBox::Warning, tr("Another workflow page"),
	                tr("This document is %1 (%2, %3) in the workflow, but the news list is for %4 (%5).\n\n"
	                   "%6 for %4 anyway? The workflow then records the stories on %4.")
	                    .arg(wf.value("pageName", pid), wf.value("edition"), date, listName, m_fetchDate, action),
	                QMessageBox::NoButton, this);
	QPushButton* go = box.addButton(tr("%1 anyway").arg(action), QMessageBox::AcceptRole);
	QPushButton* stop = box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(stop);
	if (!m_wfTestAnswer.isEmpty())
		QTimer::singleShot(300, &box, [&box, go, stop, this]() {
			m_wfTestLog << QStringLiteral("question: ") + box.text().simplified();
			(m_wfTestAnswer == QLatin1String("cancel") ? stop : go)->click();
		});
	box.exec();
	if (box.clickedButton() != go)
	{
		setStatus(tr("Not done: the document is %1 of %2 in the workflow - fetch that page's news").arg(wf.value("pageName", pid), date), true);
		return false;
	}
	m_wfPageAccepted.insert(key);
	return true;
}

void SuneerNewsPanel::onOpenPageFromWorkflow()
{
	if (m_wfBusy)
		return;
	const int idx = m_pageCombo->currentIndex();
	const QString pageId = m_pageCombo->currentData().toString();
	const QString pageName = pageNameAt(idx);
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	const QString edition = m_lastEdition;
	if (pageId.isEmpty() || edition.isEmpty())
	{
		setStatus(tr("Choose the edition and the page first"), true);
		return;
	}
	if (m_accessToken.isEmpty() && !loginDialog())
	{
		setStatus(tr("Open page from workflow needs a login"), true);
		return;
	}
	// The page lock first, exactly as Fetch News: another user's page asks (Choose another page stops here).
	if (m_lockGateKey != currentPageKey())
	{
		m_openWfBtn->setEnabled(false);
		const QString key = currentPageKey();
		checkAndTakePageLock([this, key]() {
			m_openWfBtn->setEnabled(true);
			if (m_lockGateKey == key && currentPageKey() == key)
				onOpenPageFromWorkflow();
		});
		QTimer::singleShot(kLockTimeoutMs + 1000, m_openWfBtn, [this]() { if (!m_wfBusy) m_openWfBtn->setEnabled(true); });
		return;
	}
	// Where it goes; a network folder that does not answer is refused at once (never a hang).
	const QString root = pagesFolder();
	if (!NetPathGuard::reachable(root))
	{
		setStatus(tr("The pages folder does not answer: %1 (network share off?) - change it in Settings").arg(root), true);
		return;
	}
	const QString dir = root + QLatin1Char('/') + safeFileName(edition) + QLatin1Char('/') + date;
	QString path = dir + QLatin1Char('/') + safeFileName(pageName) + QStringLiteral(".sla");
	QMap<QString, QString> ids;
	ids.insert("pageId", pageId);
	ids.insert("date", date);
	ids.insert("pageName", pageName);
	ids.insert("edition", edition);

	if (QFileInfo::exists(path))
	{
		QString answer = m_wfTestAnswer;
		if (answer.isEmpty())
		{
			QMessageBox box(QMessageBox::Question, tr("Page already downloaded"),
			                tr("%1 of %2 (%3) is already in\n%4\n\nOpen that file, replace it with the workflow's page, or keep both?")
			                    .arg(pageName, edition, date, QDir::toNativeSeparators(path)),
			                QMessageBox::NoButton, this);
			QPushButton* openBtn = box.addButton(tr("Open existing"), QMessageBox::AcceptRole);
			QPushButton* replBtn = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
			QPushButton* keepBtn = box.addButton(tr("Keep both"), QMessageBox::ActionRole);
			box.addButton(QMessageBox::Cancel);
			box.setDefaultButton(openBtn);
			box.exec();
			answer = box.clickedButton() == openBtn ? QStringLiteral("open") : box.clickedButton() == replBtn ? QStringLiteral("replace")
			       : box.clickedButton() == keepBtn ? QStringLiteral("keep") : QStringLiteral("cancel");
		}
		m_wfTestLog << QStringLiteral("existing file %1: %2").arg(QFileInfo(path).fileName(), answer);
		if (answer == QLatin1String("cancel"))
			return;
		if (answer == QLatin1String("open"))
		{
			apiLog(QStringLiteral("open from workflow: %1 %2 %3 - the existing %4 opened").arg(edition, pageName, date, path));
			if (openWorkflowPage(path, ids))
			{
				setStatus(tr("Opened the existing %1").arg(QFileInfo(path).fileName()));
				// the list, then the used / unused sync; then the obituary photos question
				QTimer::singleShot(0, this, [this]() { onFetchNews(); maybeAutoProcessObitPhotos(); });
			}
			else
				setStatus(tr("Could not open %1").arg(path), true);
			return;
		}
		if (answer == QLatin1String("replace"))
		{
			// never under an open document's feet
			for (QMdiSubWindow* sw : m_mw->mdiArea->subWindowList())
			{
				auto* win = qobject_cast<ScribusWin*>(sw->widget());
				if (win && win->doc() && QFileInfo(win->doc()->documentFileName()).canonicalFilePath() == QFileInfo(path).canonicalFilePath())
				{
					setStatus(tr("%1 is open in Scribus - close it first, then Replace").arg(QFileInfo(path).fileName()), true);
					if (!m_wfTestAnswer.isEmpty())
						m_wfTestLog << QStringLiteral("replace refused: the file is open");
					return;
				}
			}
		}
		else
		{
			const QString stem = safeFileName(pageName);
			for (int n = 2; QFileInfo::exists(path) || QFileInfo::exists(dir + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral("_images")); ++n)
				path = dir + QLatin1Char('/') + stem + QStringLiteral("_%1.sla").arg(n);
		}
	}

	m_wfBusy = true;
	m_openWfBtn->setEnabled(false);
	auto finish = [this](const QString& msg, bool failed) {
		m_wfBusy = false;
		m_openWfBtn->setEnabled(true);
		statusProgressDone();
		setStatus(msg, failed);
		m_wfTestLog << (failed ? QStringLiteral("error: ") : QStringLiteral("status: ")) + msg;
	};
	const int timeoutMs = qEnvironmentVariableIsSet("SUNEER_WFOPEN_TIMEOUT_MS") ? qEnvironmentVariableIntValue("SUNEER_WFOPEN_TIMEOUT_MS") : 60000;
	statusProgress(tr("Asking the workflow for %1").arg(pageName), 0, 0);
	QUrl cu(apiUrl("/news-layout-settings/page-context"));
	QUrlQuery cq;
	cq.addQueryItem("pageId", pageId);
	cq.addQueryItem("publishDate", date);
	cu.setQuery(cq);
	wfGet(cu, qMin(timeoutMs, 15000), [this, finish, ids, pageId, pageName, date, edition, path, timeoutMs](QNetworkReply* reply) mutable {
		const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
		if (reply->error() != QNetworkReply::NoError || httpStatus(reply) != 200)
		{
			finish(tr("Open page from workflow: %1").arg(wfErrorText(reply, timedOut, qMin(timeoutMs, 15000))), true);
			return;
		}
		const QJsonObject ctx = QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject();
		const bool chosen = ctx.value("templateChosen").toBool();
		QString tid = ctx.value("slaTemplateId").toString();
		if (tid.isEmpty() && !chosen)
			tid = ctx.value("defaultSlaTemplateId").toString();   // the editor's own fallback for an unsettled day
		if (tid.isEmpty())
		{
			const QString why = chosen ? tr("the desk chose \"no template\" for this day")
			                           : tr("no template was ever chosen for this page");
			finish(tr("The workflow has no SLA template for %1 (%2) on %3: %4").arg(pageName, edition, date, why), true);
			if (m_wfTestAnswer.isEmpty())
				QMessageBox::information(this, tr("No template"), tr("The workflow has no SLA template for %1 (%2) on %3.\n\n%4.\n\nChoose one in the workflow's Layout editor first.")
				                         .arg(pageName, edition, date, why.left(1).toUpper() + why.mid(1)));
			return;
		}
		ids.insert("templateId", tid);
		statusProgress(tr("Downloading %1").arg(pageName), 0, 0);
		QUrl du(apiUrl(QStringLiteral("/sla-templates/%1/download").arg(QString::fromLatin1(QUrl::toPercentEncoding(tid)))));
		QUrlQuery dq;
		dq.addQueryItem("date", date);
		dq.addQueryItem("pageId", pageId);
		du.setQuery(dq);
		wfGet(du, timeoutMs, [this, finish, ids, pageName, date, edition, path, timeoutMs](QNetworkReply* r) {
			const bool to = r->error() == QNetworkReply::OperationCanceledError;
			if (r->error() != QNetworkReply::NoError || httpStatus(r) != 200)
			{
				finish(tr("Open page from workflow: %1").arg(wfErrorText(r, to, timeoutMs)), true);
				return;
			}
			const QByteArray body = r->readAll();
			QString err;
			int images = 0;
			statusProgress(tr("Saving %1").arg(pageName), 0, 0);
			if (!writeWorkflowPage(body, path, ids, &err, &images))
			{
				finish(tr("Open page from workflow: %1").arg(err), true);
				return;
			}
			apiLog(QStringLiteral("open from workflow: %1 %2 %3 saved as %4 (%5 bytes%6)").arg(edition, pageName, date, path).arg(body.size())
			       .arg(images ? QStringLiteral(", %1 pictures").arg(images) : QString()));
			statusProgress(tr("Opening %1").arg(pageName), 0, 0);
			if (!openWorkflowPage(path, ids))
			{
				finish(tr("Saved %1, but Scribus could not open it").arg(path), true);
				return;
			}
			finish(tr("Opened %1 (%2, %3) from the workflow%4").arg(pageName, edition, date,
			       images ? tr(", %n picture(s)", nullptr, images) : QString()), false);
			setStatusDetails(tr("Saved as %1").arg(QDir::toNativeSeparators(path)));
			// the page's news list, then the sync: frames linked to their stories, used / unused checked;
			// then an obituary page's unprocessed photos (asked once per page)
			QTimer::singleShot(0, this, [this]() { onFetchNews(); maybeAutoProcessObitPhotos(); });
		});
	});
}

// ---------------------------------------------------------------------------
// Take over another user's page lock
// ---------------------------------------------------------------------------
// The workflow keeps only who holds a page and since when (LayoutPageStatus.lockedById /
// lockedAt): no PC, no session, no heartbeat. Its PATCH /news/layout/pages/lock refuses another
// user's page; PATCH /news/layout/pages/:id/toggle-lock {isLocked:true} sets the caller as the
// holder whoever held it - the web shows that only on its Locked Pages screen, to users with the
// lockedPages.view right, so Scribus offers it to them only. A workflow with the heartbeat of the
// spec of 2026-10-30 also sends lastSeenAt / lockClient: a lock unseen for 3 minutes is then
// offered to everyone.

bool SuneerNewsPanel::lockIsStale(const PageLockInfo& info)
{
	return info.lastSeen.isValid() && info.lastSeen.secsTo(QDateTime::currentDateTimeUtc()) > 180;
}

bool SuneerNewsPanel::canTakeOver(const PageLockInfo& info, QString* why) const
{
	auto say = [why](const QString& s) { if (why) *why = s; };
	if (m_accessToken.isEmpty())
	{
		say(tr("log in first"));
		return false;
	}
	if (info.userId.isEmpty() || info.userId == m_userId)
	{
		say(tr("the page is yours already"));
		return false;
	}
	if (lockIsStale(info))
		return true;
	if (m_permissions.contains(QStringLiteral("lockedPages.view")))
		return true;
	say(tr("taking over needs the workflow's Locked Pages right (lockedPages.view) - ask %1 or an admin to unlock it").arg(info.userName));
	return false;
}

QString SuneerNewsPanel::pageLockDetails(const QString& pageId, const QString& date) const
{
	const int idx = m_pageCombo->findData(pageId);
	const QString name = idx >= 0 ? pageNameAt(idx) : pageId;
	const auto it = m_pageLocks.constFind(pageId);
	if (!m_pageLocksKnown || m_pageLocksDate != date || it == m_pageLocks.constEnd())
		return tr("Page lock - %1, %2: not locked (as last read)").arg(name, date);
	const PageLockInfo& info = *it;
	const QDateTime now = QDateTime::currentDateTimeUtc();
	QStringList l;
	l << tr("Page lock - %1, %2").arg(name, date);
	l << tr("  Owner: %1%2").arg(info.userName, info.userId == m_userId ? tr(" (you)") : QString());
	l << tr("  Since: %1").arg(info.since.isValid() ? info.since.toLocalTime().toString("yyyy-MM-dd HH:mm") : tr("unknown"));
	l << tr("  PC: %1").arg(info.client.isEmpty() ? tr("not recorded by the workflow") : info.client);
	if (info.lastSeen.isValid())
		l << tr("  Last heartbeat: %1 (%2 min ago)%3").arg(info.lastSeen.toLocalTime().toString("HH:mm:ss"))
		        .arg(info.lastSeen.secsTo(now) / 60).arg(lockIsStale(info) ? tr(" - stale (none for 3 min)") : QString());
	else
		l << tr("  Last heartbeat: not recorded by the workflow (it has no lock heartbeat)");
	if (info.userId != m_userId)
	{
		QString gw;
		const bool blocked = markGate(pageId, &gw) == MarkGate::Blocked;
		l << tr("  You: %1%2").arg(m_userName.isEmpty() ? tr("not logged in") : m_userName,
		        m_lockOverride.contains(pageId + QLatin1Char('|') + date) ? tr(" - continued anyway (the page is not locked for you)") : QString());
		l << tr("  Used / unused sync: %1").arg(blocked ? tr("skipped - %1").arg(gw) : tr("not blocked"));
		QString why;
		l << tr("  Take over: %1").arg(canTakeOver(info, &why)
		        ? (lockIsStale(info) ? tr("available (the lock is stale)") : tr("available (you have the Locked Pages right)"))
		        : tr("not available - %1").arg(why));
	}
	return l.join('\n');
}

void SuneerNewsPanel::onTakeOverPage()
{
	const int idx = m_pageCombo->currentIndex();
	const QString id = m_pageCombo->currentData().toString();
	const QString date = m_dateEdit->date().toString("yyyy-MM-dd");
	const QString name = pageNameAt(idx);
	const auto it = m_pageLocks.constFind(id);
	if (id.isEmpty() || it == m_pageLocks.constEnd())
		return;
	const PageLockInfo info = *it;
	QString why;
	if (!canTakeOver(info, &why))
	{
		setStatus(tr("Cannot take over %1: %2").arg(name, why), true);
		return;
	}
	QString answer = m_pageLockTakeTestAnswer;
	if (answer.isEmpty())
	{
		QMessageBox box(QMessageBox::Warning, tr("Take over page"),
		                tr("Take over %1 for %2?\n\n%3 will lose the lock (opened since %4%5).%6\n\n"
		                   "If %3 is still working on this page, one of you overwrites the other's work.")
		                    .arg(name, date, info.userName, formatLockSince(info.since),
		                         info.client.isEmpty() ? QString() : tr(" on %1").arg(info.client),
		                         lockIsStale(info) ? QStringLiteral("\n") + tr("No sign of life from that lock for %n min.", "", int(info.lastSeen.secsTo(QDateTime::currentDateTimeUtc()) / 60)) : QString()),
		                QMessageBox::NoButton, this);
		QPushButton* take = box.addButton(tr("Take over"), QMessageBox::DestructiveRole);
		QPushButton* cancel = box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(cancel);
		box.exec();
		answer = box.clickedButton() == take ? QStringLiteral("take") : QStringLiteral("cancel");
	}
	if (answer != QLatin1String("take"))
		return;
	m_takeOverBtn->setEnabled(false);
	QJsonObject body;
	body["publishDate"] = date;
	body["isLocked"] = true;
	const QString from = info.userName;
	lockCall("PATCH", QUrl(apiUrl(QStringLiteral("/news/layout/pages/%1/toggle-lock").arg(id))), QJsonDocument(body).toJson(QJsonDocument::Compact),
	         [this, id, name, date, from](QNetworkReply* reply) {
		m_takeOverBtn->setEnabled(true);
		const int http = httpStatus(reply);
		const QJsonObject st = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
		const bool ok = reply->error() == QNetworkReply::NoError && http == 200 && st["lockedById"].toString() == m_userId;
		syncLog(QStringLiteral("page lock: take over of %1 %2 from %3 -> %4").arg(name, date, from,
		        ok ? QStringLiteral("HTTP 200, yours now") : QStringLiteral("HTTP %1 %2").arg(http).arg(describeError(reply))));
		if (!ok)
		{
			setStatus(tr("Take over of %1 failed: %2").arg(name, http == 200 ? tr("the workflow did not give it to you") : describeError(reply)), true);
			refreshPageLocks();
			return;
		}
		// the page held so far is let go; this one is held like a page locked by choosing it
		if (!m_heldPageId.isEmpty() && (m_heldPageId != id || m_heldDate != date))
			releasePageLock(false, QStringLiteral("took over another page"));
		m_heldPageId = id;
		m_heldPageName = name;
		m_heldDate = date;
		m_heldWasMineBefore = false;
		m_heldDoc = m_doc;
		m_heldDocBound = (m_doc != nullptr);
		m_lockOverride.remove(id + QLatin1Char('|') + date);
		m_lockGateKey = id + QLatin1Char('|') + date;
		m_lockKeepAlive->start();
		sendLockHeartbeat();
		setStatus(tr("%1 is yours now (taken over from %2) - syncing used / unused").arg(name, from));
		m_syncTestLog << QStringLiteral("took over %1 from %2").arg(name, from);
		refreshPageLocks([this, id, date](bool, const QString&) {
			// the normal sync at once: stories on the page -> USED, the rest asked
			if (m_fetchPageId == id && m_fetchDate == date)
			{
				retryAutoMarks();
				reconcilePage(tr("Take over"));
			}
			else
				onFetchNews();
		});
	});
}

// While a page is held (every keep-alive and right after it is taken): tell the workflow this
// session is alive and on which PC (spec 2026-10-30). A workflow without the route answers 404
// once; it is not asked again in this session.
void SuneerNewsPanel::sendLockHeartbeat()
{
	if (m_heartbeatUnsupported || m_heldPageId.isEmpty() || m_accessToken.isEmpty() || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	QJsonObject body;
	body["publishDate"] = m_heldDate;
	body["client"] = QSysInfo::machineHostName();
	const QString page = m_heldPageName;
	lockCall("PATCH", QUrl(apiUrl(QStringLiteral("/news/layout/pages/%1/heartbeat").arg(m_heldPageId))), QJsonDocument(body).toJson(QJsonDocument::Compact),
	         [this, page](QNetworkReply* reply) {
		const int http = httpStatus(reply);
		if (http == 404)
		{
			m_heartbeatUnsupported = true;
			syncLog(QStringLiteral("page lock: the workflow has no lock heartbeat route (HTTP 404) - not sent again this session"));
		}
		else if (http != 200)
			syncLog(QStringLiteral("page lock: heartbeat for %1 -> HTTP %2").arg(page).arg(http));
	});
}

// ---------------------------------------------------------------------------
// Page sync: the stories in the document <-> used / unused of its workflow page
// ---------------------------------------------------------------------------
// The workflow keeps "which story is on which page" in news_pages (dtpStatus per story + page, no
// date) and the web editor's frames in NewsLayoutSetting (page + date + position). Its .sla download
// carries neither: every frame has a random ItemID and no name or attribute (checked on real
// downloads, see the change report). So a downloaded page's frames are linked here once, by the
// workflow's own layout rows (position), and every other untagged frame only on the user's word
// (headline match). From then on the frames carry news.story / news.serverId / news.pageId like
// placed ones, and the existing place / delete marks work on them.

// "Keep" (used but not on the page, left used) is remembered per page and story in the settings,
// so a restart does not ask again. The newest 2000 are kept.
void SuneerNewsPanel::saveSyncKept()
{
	QStringList kept;
	{
		QSettings ks("Faircode", "ScribusNews");
		kept = ks.value("sync/kept").toStringList();
	}
	for (const QString& k : std::as_const(m_syncKept))
		if (!kept.contains(k))
			kept << k;
	while (kept.size() > 2000)
		kept.removeFirst();
	QSettings ks("Faircode", "ScribusNews");
	ks.setValue("sync/kept", kept);
	ks.sync();
}

int SuneerNewsPanel::forgetSyncKept()
{
	QSettings ks("Faircode", "ScribusNews");
	QStringList all = ks.value("sync/kept").toStringList();
	for (const QString& k : std::as_const(m_syncKept))
		if (!all.contains(k))
			all << k;
	ks.remove("sync/kept");
	ks.sync();
	m_syncKept.clear();
	syncLog(QStringLiteral("sync: Forget Keep - %1 kept stor(ies) forgotten").arg(all.size()));
	return all.size();
}

void SuneerNewsPanel::syncLog(const QString& line) const
{
	apiLog(line);
	const QString dir = QDir::homePath() + QStringLiteral("/scribus-crashlogs");
	QDir().mkpath(dir);
	QFile f(dir + QStringLiteral("/news-sync.log"));
	if (f.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream out(&f);
		out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "  " << line << "\n";
	}
}

SuneerNewsPanel::MarkGate SuneerNewsPanel::markGate(const QString& pageId, QString* why) const
{
	auto say = [why](const QString& s) { if (why) *why = s; };
	if (!qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return MarkGate::Allowed;
	if (pageId.isEmpty())
	{
		say(tr("no page id"));
		return MarkGate::Deferred;
	}
	if (m_heldPageId == pageId)
		return MarkGate::Allowed;
	const int idx = m_pageCombo ? m_pageCombo->findData(pageId) : -1;
	const QString name = idx >= 0 ? pageNameAt(idx) : tr("page ..%1").arg(pageId.right(6));
	const auto it = m_pageLocks.constFind(pageId);
	if (m_pageLocksKnown && it != m_pageLocks.constEnd() && !it->userId.isEmpty() && it->userId != m_userId)
	{
		say(tr("%1 is opened by %2 - only they change its used / unused").arg(name, it->userName));
		return MarkGate::Blocked;
	}
	// The News Browser's own page whose lock could not be checked (network, older server): work
	// goes on as it always did there.
	if (m_lockGateKey.startsWith(pageId + QLatin1Char('|')))
	{
		say(tr("lock of %1 not known").arg(name));
		return MarkGate::Allowed;
	}
	say(tr("this Scribus does not hold the page lock of %1 - kept until it does").arg(name));
	return MarkGate::Deferred;
}

SuneerNewsPanel::SyncPage SuneerNewsPanel::docSyncPage(ScribusDoc* doc) const
{
	SyncPage sp;
	if (!doc)
		return sp;
	const QMap<QString, QString> wf = doc->suneerWorkflow();
	if (!wf.value("pageId").isEmpty())
	{
		sp.pageId = wf.value("pageId");
		sp.date = wf.value("date");
		sp.pageName = wf.value("pageName");
		sp.edition = wf.value("edition");
		sp.workflow = true;
		return sp;
	}
	// A document of placed stories: the page most of them came from.
	QHash<QString, int> count;
	for (int i = 0; i < doc->Items->count(); ++i)
	{
		PageItem* it = doc->Items->at(i);
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
			if (!attr(p, "news.serverId").isEmpty() && !attr(p, "news.pageId").isEmpty())
				++count[attr(p, "news.pageId")];
	}
	int best = 0;
	for (auto it = count.constBegin(); it != count.constEnd(); ++it)
		if (it.value() > best)
		{
			best = it.value();
			sp.pageId = it.key();
		}
	const int idx = sp.pageId.isEmpty() ? -1 : m_pageCombo->findData(sp.pageId);
	if (idx >= 0)
		sp.pageName = pageNameAt(idx);
	return sp;
}

// The status of a story for one page: page.dtpStatus (the list is per page), else its newsPages[] entry.
QString SuneerNewsPanel::pageStatusOf(const QJsonObject& news, const QString& pageId) const
{
	const QJsonObject pg = news["page"].toObject();
	const QString pid = pg["id"].toString().isEmpty() ? pg["pageId"].toString() : pg["id"].toString();
	if (!pg.isEmpty() && (pid.isEmpty() || pid == pageId) && !pg["dtpStatus"].toString().isEmpty())
		return pg["dtpStatus"].toString().toUpper();
	for (const QJsonValue& v : news["newsPages"].toArray())
	{
		const QJsonObject p = v.toObject();
		if (p["pageId"].toString() == pageId || p["page"].toObject()["id"].toString() == pageId)
			return p["dtpStatus"].toString().toUpper();
	}
	return QStringLiteral("UNUSED");
}

// A headline for comparing: NFC, without soft hyphens / zero-width marks (the workflow's .sla
// hyphenates its headlines with U+00AD), punctuation as space, lower case, single spaces.
QString SuneerNewsPanel::headlineKey(const QString& s)
{
	QString out;
	for (const QChar ch : s.normalized(QString::NormalizationForm_C))
	{
		const ushort u = ch.unicode();
		if (u == 0x00AD || u == 0x200B || u == 0x200C || u == 0x200D || u == 0x2060 || u == 0xFEFF)
			continue;
		out += (ch.isSpace() || ch.isPunct() || ch == SpecialChars::PARSEP || ch.category() == QChar::Other_Control) ? QChar(' ') : ch.toLower();
	}
	return out.simplified();
}

bool SuneerNewsPanel::headlinesMatch(const QString& a, const QString& b)
{
	const QString ka = headlineKey(a), kb = headlineKey(b);
	if (ka.isEmpty() || kb.isEmpty())
		return false;
	if (ka == kb)
		return true;
	const QString& shorter = ka.size() < kb.size() ? ka : kb;
	const QString& longer = ka.size() < kb.size() ? kb : ka;
	return shorter.size() >= 8 && longer.startsWith(shorter);
}

// The first paragraphs of a text frame, joined by PARSEP (a kicker may come before the headline).
QString SuneerNewsPanel::frameHeadline(PageItem* text)
{
	if (!text || !text->isTextFrame())
		return QString();
	const int len = qMin(text->itemText.length(), 600);
	const QStringList paras = text->itemText.text(0, len).split(SpecialChars::PARSEP);
	return paras.mid(0, 3).join(SpecialChars::PARSEP);
}

// The photo frames welded to a story's text frame and the captions welded to those (the
// workflow's export welds them so); nothing that already belongs to a story.
QList<PageItem*> SuneerNewsPanel::weldedStoryParts(PageItem* text) const
{
	QList<PageItem*> out { text };
	QList<PageItem*> todo { text };
	while (!todo.isEmpty())
	{
		PageItem* cur = todo.takeFirst();
		for (PageItem* w : cur->itemsWeldedTo())
		{
			if (!w || out.contains(w) || !attr(w, "news.story").isEmpty())
				continue;
			const bool photo = w->isImageFrame();
			const bool caption = w->isTextFrame() && w->itemName().startsWith(QLatin1String("caption_"));
			if (!photo && !caption)
				continue;
			out << w;
			if (photo)
				todo << w;
		}
	}
	return out;
}

void SuneerNewsPanel::linkStoryFrames(PageItem* text, const QString& title, const QString& serverId, const QString& pageId)
{
	const QList<PageItem*> parts = weldedStoryParts(text);
	QString base = collapseSpaces(title).left(30).trimmed();
	if (base.isEmpty())
		base = tr("News story");
	QString id = base;
	for (int n = 2; !storyItems(id).isEmpty() && n < 1000; ++n)
		id = QStringLiteral("%1 (%2)").arg(base).arg(n);
	for (PageItem* p : parts)
	{
		setAttr(p, "news.story", id);
		setAttr(p, "news.serverId", serverId);
		setAttr(p, "news.pageId", pageId);
		setAttr(p, "news.role", p == text ? QStringLiteral("text") : p->isImageFrame() ? QStringLiteral("photo") : QStringLiteral("caption"));
	}
	indexAdd(parts);
}

// GET /news-layout-settings rows (page-local pt, the workflow's own record of where each story's
// frame is) -> the untagged text frame the export wrote there: same left edge and width, top
// inside the row (an obituary cell's text starts under its photo). The export adds the page's
// position to the row (PAGE_X/Y_OFFSET_PT), so the first page's offsets are used here.
int SuneerNewsPanel::linkWorkflowFrames(const QJsonArray& rows, const QString& pageId)
{
	if (!m_doc || m_doc->Pages->isEmpty())
		return 0;
	const double xo = m_doc->Pages->at(0)->xOffset(), yo = m_doc->Pages->at(0)->yOffset();
	QList<PageItem*> free;
	for (int i = 0; i < m_doc->Items->count(); ++i)
	{
		PageItem* it = m_doc->Items->at(i);
		if (it->isTextFrame() && attr(it, "news.story").isEmpty() && !it->itemName().startsWith(QLatin1String("caption_")))
			free << it;
	}
	QHash<QString, QString> titles;
	for (const QJsonValue& v : m_newsData)
		titles.insert(newsServerId(v.toObject()), v.toObject()["title"].toString());
	int linked = 0;
	for (const QJsonValue& v : rows)
	{
		const QJsonObject r = v.toObject();
		const QString sid = r["news_id"].toString(r["newsId"].toString());
		if (sid.isEmpty() || r["frame_type"].toString(r["frameType"].toString("news")) != QLatin1String("news") || r["hidden"].toBool())
			continue;
		if (findStoryItemInDoc(sid))
			continue;
		const QJsonValue xv = r.contains("x_pos") ? r["x_pos"] : r["xPos"];
		const QJsonValue yv = r.contains("y_pos") ? r["y_pos"] : r["yPos"];
		const QJsonValue wv = r.contains("frame_width") ? r["frame_width"] : r["frameWidth"];
		const QJsonValue hv = r["height"];
		PageItem* best = nullptr;
		if (xv.isDouble() && wv.isDouble())
		{
			const double x = xo + xv.toDouble(), y = yo + yv.toDouble(0), w = wv.toDouble(), h = qMax(1.0, hv.toDouble(100));
			double bestDy = 1e9;
			for (PageItem* f : std::as_const(free))
			{
				if (qAbs(f->xPos() - x) > 1.5 || qAbs(f->width() - w) > 1.5)
					continue;
				const double dy = f->yPos() - y;
				if (dy < -1.5 || dy > h + 1.5)
					continue;
				if (qAbs(dy) < bestDy)
				{
					bestDy = qAbs(dy);
					best = f;
				}
			}
		}
		const QString title = titles.value(sid);
		if (!best && !title.isEmpty())
		{
			// a row without a saved position (grid default): its headline, when exactly one frame has it
			QList<PageItem*> hits;
			for (PageItem* f : std::as_const(free))
				for (const QString& h : frameHeadline(f).split(SpecialChars::PARSEP))
					if (headlinesMatch(h, title)) { hits << f; break; }
			if (hits.size() == 1)
				best = hits.first();
		}
		if (!best)
		{
			syncLog(QStringLiteral("sync: layout row of story ..%1 - no frame at its place").arg(sid.right(6)));
			continue;
		}
		bool headOk = title.isEmpty();
		for (const QString& h : frameHeadline(best).split(SpecialChars::PARSEP))
			headOk = headOk || headlinesMatch(h, title);
		linkStoryFrames(best, title.isEmpty() ? frameHeadline(best).section(SpecialChars::PARSEP, 0, 0) : title, sid, pageId);
		free.removeAll(best);
		++linked;
		syncLog(QStringLiteral("sync: frame at %1,%2 linked to story ..%3 by the workflow's layout row%4")
		        .arg(best->xPos() - xo, 0, 'f', 1).arg(best->yPos() - yo, 0, 'f', 1).arg(sid.right(6), headOk ? QString() : QStringLiteral(" (its headline differs from the list's)")));
	}
	return linked;
}

// Fetch News done (or a workflow page opened): the document's page is the list's page -> link a
// downloaded page's frames, mark what is on the page USED, ask about what is not.
void SuneerNewsPanel::reconcilePage(const QString& trigger)
{
	if (!m_doc || m_fetchPageId.isEmpty() || m_syncRunning || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	const SyncPage sp = docSyncPage(m_doc);
	if (sp.pageId.isEmpty())
		return;
	if (sp.pageId != m_fetchPageId || (!sp.date.isEmpty() && sp.date != m_fetchDate))
	{
		syncLog(QStringLiteral("sync (%1): the document is page ..%2 %3, the list is page ..%4 %5 - not compared")
		        .arg(trigger, sp.pageId.right(6), sp.date, m_fetchPageId.right(6), m_fetchDate));
		return;
	}
	const QString pageId = sp.pageId;
	const int idx = m_pageCombo->findData(pageId);
	const QString pageName = idx >= 0 ? pageNameAt(idx) : sp.pageName;
	if (m_accessToken.isEmpty())
	{
		setStatus(tr("Log in to sync used / unused of %1 with the workflow").arg(pageName), true);
		return;
	}
	QString why;
	if (markGate(pageId, &why) == MarkGate::Blocked)
	{
		setStatus(tr("Sync with the workflow skipped: %1").arg(why)
		          + (m_takeOverBtn && !m_takeOverBtn->isHidden() ? tr(" - 'Take over page' makes it yours") : QString()), true);
		setStatusDetails(pageLockDetails(pageId, m_fetchDate));
		syncLog(QStringLiteral("sync (%1) of %2 skipped: %3").arg(trigger, pageName, why));
		m_syncTestLog << QStringLiteral("blocked: ") + why;
		return;
	}
	retryAutoMarks();
	// A downloaded page: its frames carry no story ids. The workflow's layout rows for the page and
	// date give them, by position - once per document, right after it is opened.
	const QString linkKey = QStringLiteral("%1|%2|%3").arg(quintptr(m_doc.data())).arg(pageId, m_fetchDate);
	if (sp.workflow && !m_syncLinkTried.contains(linkKey))
	{
		m_syncLinkTried.insert(linkKey);
		m_syncRunning = true;
		QUrl u(apiUrl("/news-layout-settings"));
		QUrlQuery q;
		q.addQueryItem("pageId", pageId);
		q.addQueryItem("publishDate", m_fetchDate);
		u.setQuery(q);
		QPointer<ScribusDoc> doc = m_doc;
		wfGet(u, 15000, [this, doc, trigger, pageId](QNetworkReply* r) {
			m_syncRunning = false;
			if (!doc || doc != m_doc)
				return;
			if (r->error() == QNetworkReply::NoError && httpStatus(r) == 200)
			{
				const QJsonValue d = QJsonDocument::fromJson(r->readAll()).object()["data"];
				const QJsonArray rows = d.isArray() ? d.toArray() : d.toObject()["rows"].toArray();
				const bool wasModified = doc->isModified();
				const int n = linkWorkflowFrames(rows, pageId);
				syncLog(QStringLiteral("sync: %1 layout rows for page ..%2, %3 frame(s) linked").arg(rows.size()).arg(pageId.right(6)).arg(n));
				m_syncTestLog << QStringLiteral("linked by layout rows: %1 of %2").arg(n).arg(rows.size());
				if (n > 0)
				{
					doc->changed();
					// a page just downloaded and untouched: the ids go into its file at once
					if (!wasModified && doc->hasName && m_mw && m_mw->doc == doc)
						m_mw->slotFileSave();
				}
			}
			else
				syncLog(QStringLiteral("sync: layout rows not read (%1) - frames are linked by headline only").arg(wfErrorText(r, r->error() == QNetworkReply::OperationCanceledError, 15000)));
			reconcilePage(trigger);
		});
		return;
	}

	// what the document has
	QHash<QString, QString> onDoc;                 // serverId -> story name
	QStringList noId;
	QList<PageItem*> untagged;
	for (int i = 0; i < m_doc->Items->count(); ++i)
	{
		PageItem* it = m_doc->Items->at(i);
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
		{
			const QString st = attr(p, "news.story");
			if (st.isEmpty())
			{
				if (p->isTextFrame() && !p->itemName().startsWith(QLatin1String("caption_")) && p->itemText.length() > 0)
					untagged << p;
				continue;
			}
			const QString sid = attr(p, "news.serverId");
			const QString pid = attr(p, "news.pageId");
			if (sid.isEmpty())
			{
				if (!noId.contains(st))
					noId << st;
			}
			else if (pid.isEmpty() || pid == pageId)
				onDoc.insert(sid, st);
		}
	}
	// what the workflow says for this page
	QHash<QString, QString> srv, titles;
	for (const QJsonValue& v : m_newsData)
	{
		const QJsonObject n = v.toObject();
		const QString sid = newsServerId(n);
		if (sid.isEmpty())
			continue;
		srv.insert(sid, pageStatusOf(n, pageId));
		titles.insert(sid, collapseSpaces(n["title"].toString()));
	}
	auto pending = [this, &pageId](const QString& sid) {
		DeletedStory k;
		k.serverId = sid;
		k.pageId = pageId;
		const QString key = retryKey(k);
		return m_inFlightMarks.contains(key) ? m_inFlightMarks.value(key) : m_autoRetry.contains(key) ? m_autoRetry.value(key).title : QString();
	};
	QSet<QString> deciding;                        // deleted / cut a moment ago: the delete check decides
	for (const DeletedStory& d : std::as_const(m_deleted))
		deciding.insert(d.serverId);
	QList<DeletedStory> toUsed;
	QStringList notAssigned;
	for (auto it = onDoc.constBegin(); it != onDoc.constEnd(); ++it)
	{
		if (!srv.contains(it.key()))
		{
			notAssigned << it.value();
			continue;
		}
		if (srv.value(it.key()) == QLatin1String("USED") || !pending(it.key()).isEmpty())
			continue;
		DeletedStory d;
		d.storyId = it.value();
		d.serverId = it.key();
		d.pageId = pageId;
		toUsed << d;
	}
	QList<SyncFinding> stale, candidates;
	for (auto it = srv.constBegin(); it != srv.constEnd(); ++it)
		if (it.value() == QLatin1String("USED") && !onDoc.contains(it.key()) && !deciding.contains(it.key())
		    && pending(it.key()).isEmpty() && !m_syncKept.contains(pageId + QLatin1Char('|') + it.key()))
			stale << SyncFinding { it.key(), titles.value(it.key()), nullptr };
	QSet<QString> offered;
	for (PageItem* f : std::as_const(untagged))
	{
		const QStringList heads = frameHeadline(f).split(SpecialChars::PARSEP);
		for (auto it = titles.constBegin(); it != titles.constEnd(); ++it)
		{
			if (onDoc.contains(it.key()) || offered.contains(it.key()))
				continue;
			bool hit = false;
			for (const QString& h : heads)
				hit = hit || headlinesMatch(h, it.value());
			if (hit)
			{
				candidates << SyncFinding { it.key(), it.value(), f };
				offered.insert(it.key());
				break;
			}
		}
	}
	if (!toUsed.isEmpty())
		autoMarkStories(toUsed, QStringLiteral("USED"), QString(), tr("on the page, %1").arg(trigger));
	const QString summary = QStringLiteral("sync (%1) of %2 %3: %4 stories on the page, %5 marked used now, %6 used but not on the page, %7 to link by headline, %8 without id, %9 not in the page's list")
	                        .arg(trigger, pageName, m_fetchDate).arg(onDoc.size()).arg(toUsed.size()).arg(stale.size()).arg(candidates.size()).arg(noId.size()).arg(notAssigned.size());
	syncLog(summary);
	m_syncTestLog << summary;
	if (!toUsed.isEmpty())
		setStatus(tr("Sync: %n story(ies) on the page marked used in the workflow", "", toUsed.size()));
	// stories missing from the list only count when the list itself came without errors
	if (!m_fetchNote.isEmpty())
		notAssigned.clear();
	if (!stale.isEmpty() || !candidates.isEmpty() || !noId.isEmpty() || !notAssigned.isEmpty())
		showSyncDialog(pageId, pageName, stale, candidates, noId, notAssigned);
}

// Save / close: the stories in the document go USED (when the list is the document's page); no questions.
void SuneerNewsPanel::pushOnPage(const QString& why)
{
	if (!m_doc || m_accessToken.isEmpty() || m_newsData.isEmpty() || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	const SyncPage sp = docSyncPage(m_doc);
	if (sp.pageId.isEmpty() || sp.pageId != m_fetchPageId || (!sp.date.isEmpty() && sp.date != m_fetchDate))
		return;
	QHash<QString, QString> srv;
	for (const QJsonValue& v : m_newsData)
		srv.insert(newsServerId(v.toObject()), pageStatusOf(v.toObject(), sp.pageId));
	QHash<QString, DeletedStory> want;
	for (int i = 0; i < m_doc->Items->count(); ++i)
	{
		PageItem* it = m_doc->Items->at(i);
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* p : parts)
		{
			const QString sid = attr(p, "news.serverId");
			const QString pid = attr(p, "news.pageId");
			if (sid.isEmpty() || (!pid.isEmpty() && pid != sp.pageId) || !srv.contains(sid) || srv.value(sid) == QLatin1String("USED"))
				continue;
			DeletedStory d;
			d.storyId = attr(p, "news.story");
			d.serverId = sid;
			d.pageId = sp.pageId;
			if (m_inFlightMarks.contains(retryKey(d)) || m_autoRetry.contains(retryKey(d)))
				continue;
			want.insert(sid, d);
		}
	}
	if (!want.isEmpty())
		autoMarkStories(want.values(), QStringLiteral("USED"), QString(), tr("on the page, %1").arg(why));
}

void SuneerNewsPanel::showSyncDialog(const QString& pageId, const QString& pageName, const QList<SyncFinding>& stale,
                                     const QList<SyncFinding>& candidates, const QStringList& noId, const QStringList& notAssigned)
{
	if (m_syncDialog)
		m_syncDialog->close();
	auto* dlg = new QDialog(this);
	m_syncDialog = dlg;
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle(tr("Sync with the workflow - %1").arg(pageName));
	auto* lay = new QVBoxLayout(dlg);
	QPointer<ScribusDoc> doc = m_doc;
	auto stillThere = [doc](PageItem* f) {
		if (!doc || !f)
			return false;
		for (int i = 0; i < doc->Items->count(); ++i)
		{
			PageItem* it = doc->Items->at(i);
			if (it == f || (it->isGroup() && it->getAllChildren().contains(f)))
				return true;
		}
		return false;
	};
	QListWidget* staleList = nullptr;
	QPushButton* unusedBtn = nullptr;
	QPushButton* keepBtn = nullptr;
	if (!stale.isEmpty())
	{
		auto* l = new QLabel((stale.size() == 1 ? tr("This story is marked used for %1 in the workflow but is not on the page.").arg(pageName)
		                                        : tr("These %1 stories are marked used for %2 in the workflow but are not on the page.").arg(stale.size()).arg(pageName))
		                     + QLatin1Char('\n') + (stale.size() == 1 ? tr("It may be on another page or edition - nothing is changed unless you say so.")
		                                                               : tr("They may be on another page or edition - nothing is changed unless you say so.")), dlg);
		l->setWordWrap(true);
		lay->addWidget(l);
		staleList = new QListWidget(dlg);
		for (const SyncFinding& f : stale)
		{
			auto* it = new QListWidgetItem(f.title.isEmpty() ? f.serverId : f.title, staleList);
			it->setData(Qt::UserRole, f.serverId);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(Qt::Checked);
		}
		staleList->setMaximumHeight(qMin(8, staleList->count()) * staleList->sizeHintForRow(0) + 2 * staleList->frameWidth() + 4);
		lay->addWidget(staleList);
		auto* row = new QHBoxLayout;
		unusedBtn = new QPushButton(tr("Mark unused"), dlg);
		unusedBtn->setToolTip(tr("The ticked stories go back to the workflow's Unused list for %1").arg(pageName));
		keepBtn = new QPushButton(tr("Keep"), dlg);
		keepBtn->setToolTip(tr("Leave them used; not asked again for this page (also after a restart)"));
		row->addWidget(unusedBtn);
		row->addWidget(keepBtn);
		row->addStretch();
		lay->addLayout(row);
	}
	QListWidget* candList = nullptr;
	QPushButton* linkBtn = nullptr;
	if (!candidates.isEmpty())
	{
		auto* l = new QLabel(tr("These frames are on the page but carry no workflow story id, so they cannot be synced. "
		                        "Their headline matches a story of %1 - link them?").arg(pageName), dlg);
		l->setWordWrap(true);
		lay->addWidget(l);
		candList = new QListWidget(dlg);
		for (const SyncFinding& f : candidates)
		{
			const QString head = collapseSpaces(frameHeadline(f.frame).section(SpecialChars::PARSEP, 0, 0)).left(50);
			auto* it = new QListWidgetItem(QStringLiteral("%1  →  %2").arg(head, f.title.left(50)), candList);
			it->setData(Qt::UserRole, f.serverId);
			it->setData(Qt::UserRole + 1, QVariant::fromValue(static_cast<void*>(f.frame)));
			it->setData(Qt::UserRole + 2, f.title);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(Qt::Checked);
		}
		candList->setMaximumHeight(qMin(8, candList->count()) * candList->sizeHintForRow(0) + 2 * candList->frameWidth() + 4);
		lay->addWidget(candList);
		linkBtn = new QPushButton(tr("Link ticked"), dlg);
		auto* row = new QHBoxLayout;
		row->addWidget(linkBtn);
		row->addStretch();
		lay->addLayout(row);
	}
	if (!noId.isEmpty())
	{
		auto* l = new QLabel(tr("Story frames without a workflow id (cannot be synced): %1").arg(noId.join(QStringLiteral(", "))), dlg);
		l->setWordWrap(true);
		lay->addWidget(l);
	}
	if (!notAssigned.isEmpty())
	{
		auto* l = new QLabel(tr("On the page but not in the workflow's list for %1 (placed from another page?), not changed: %2")
		                     .arg(pageName, notAssigned.join(QStringLiteral(", "))), dlg);
		l->setWordWrap(true);
		lay->addWidget(l);
	}
	auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
	lay->addWidget(box);
	connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
	auto staleDone = [staleList, unusedBtn, keepBtn]() {
		staleList->setEnabled(false);
		unusedBtn->setEnabled(false);
		keepBtn->setEnabled(false);
	};
	if (unusedBtn)
	{
		connect(unusedBtn, &QPushButton::clicked, this, [this, staleList, staleDone, pageId]() {
			QList<DeletedStory> go;
			for (int i = 0; i < staleList->count(); ++i)
			{
				QListWidgetItem* it = staleList->item(i);
				if (it->checkState() != Qt::Checked)
				{
					m_syncKept.insert(pageId + QLatin1Char('|') + it->data(Qt::UserRole).toString());
					saveSyncKept();
					continue;
				}
				DeletedStory d;
				d.serverId = it->data(Qt::UserRole).toString();
				d.storyId = collapseSpaces(it->text()).left(30);
				d.pageId = pageId;
				go << d;
			}
			syncLog(QStringLiteral("sync: user chose Mark unused for %1 stor(ies) used but not on page ..%2").arg(go.size()).arg(pageId.right(6)));
			m_syncTestLog << QStringLiteral("answer: mark unused %1").arg(go.size());
			autoMarkStories(go, unusedStatus(), QStringLiteral("USED"), tr("not on the page"));
			staleDone();
		});
		connect(keepBtn, &QPushButton::clicked, this, [this, staleList, staleDone, pageId]() {
			for (int i = 0; i < staleList->count(); ++i)
				m_syncKept.insert(pageId + QLatin1Char('|') + staleList->item(i)->data(Qt::UserRole).toString());
			saveSyncKept();
			syncLog(QStringLiteral("sync: user chose Keep for %1 stor(ies) used but not on page ..%2").arg(staleList->count()).arg(pageId.right(6)));
			m_syncTestLog << QStringLiteral("answer: keep %1").arg(staleList->count());
			staleDone();
		});
	}
	if (linkBtn)
	{
		connect(linkBtn, &QPushButton::clicked, this, [this, candList, linkBtn, stillThere, pageId, doc]() {
			if (!doc || doc != m_doc)
				return;
			QList<DeletedStory> used;
			int n = 0;
			for (int i = 0; i < candList->count(); ++i)
			{
				QListWidgetItem* it = candList->item(i);
				auto* f = static_cast<PageItem*>(it->data(Qt::UserRole + 1).value<void*>());
				const QString sid = it->data(Qt::UserRole).toString();
				if (it->checkState() != Qt::Checked || !stillThere(f) || findStoryItemInDoc(sid) || !attr(f, "news.story").isEmpty())
					continue;
				linkStoryFrames(f, it->data(Qt::UserRole + 2).toString(), sid, pageId);
				++n;
				DeletedStory d;
				d.storyId = attr(f, "news.story");
				d.serverId = sid;
				d.pageId = pageId;
				for (const QJsonValue& v : m_newsData)
					if (newsServerId(v.toObject()) == sid && pageStatusOf(v.toObject(), pageId) != QLatin1String("USED"))
						used << d;
			}
			if (n > 0)
				m_doc->changed();
			syncLog(QStringLiteral("sync: user linked %1 frame(s) by headline on page ..%2").arg(n).arg(pageId.right(6)));
			m_syncTestLog << QStringLiteral("answer: linked %1").arg(n);
			autoMarkStories(used, QStringLiteral("USED"), QString(), tr("linked by headline"));
			candList->setEnabled(false);
			linkBtn->setEnabled(false);
			setStatus(tr("Linked %n frame(s) to their workflow story - save the document to keep the link", "", n));
		});
	}
	if (!m_syncTestAnswer.isEmpty())
	{
		const QString answer = m_syncTestAnswer;
		QTimer::singleShot(400, dlg, [this, dlg, answer, unusedBtn, keepBtn, linkBtn]() {
			QStringList seen;
			for (QLabel* l : dlg->findChildren<QLabel*>())
				seen << l->text().simplified();
			for (QListWidget* lw : dlg->findChildren<QListWidget*>())
				for (int i = 0; i < lw->count(); ++i)
					seen << QStringLiteral("[%1]").arg(lw->item(i)->text());
			m_syncTestLog << QStringLiteral("dialog: ") + seen.join(QStringLiteral(" | "));
			dlg->grab().save(qEnvironmentVariable("SUNEER_WFSYNC_SELFTEST") + QStringLiteral("-dialog-%1.png").arg(m_syncTestLog.size()));
			if (answer.contains(QLatin1String("link")) && linkBtn)
				linkBtn->click();
			if (answer.contains(QLatin1String("unused")) && unusedBtn)
				unusedBtn->click();
			else if (answer.contains(QLatin1String("keep")) && keepBtn)
				keepBtn->click();
			dlg->close();
		});
	}
	dlg->resize(560, dlg->sizeHint().height());
	dlg->show();                                    // not modal: the page stays workable
}

void SuneerNewsPanel::switchPanelToPage(const SyncPage& sp, std::function<void(bool)> then)
{
	if (!sp.edition.isEmpty() && sp.edition != m_lastEdition)
	{
		const int e = m_editionCombo->findData(sp.edition);
		if (e < 0)
		{
			then(false);
			return;
		}
		m_editionCombo->setCurrentIndex(e);
	}
	if (!sp.date.isEmpty())
	{
		const QDate d = QDate::fromString(sp.date, "yyyy-MM-dd");
		if (d.isValid() && d != m_dateEdit->date())
			m_dateEdit->setDate(d);
	}
	auto* t = new QTimer(this);
	auto el = std::make_shared<QElapsedTimer>();
	el->start();
	auto picked = std::make_shared<bool>(false);
	t->setInterval(100);
	connect(t, &QTimer::timeout, this, [this, t, el, sp, then, picked]() {
		const int idx = m_pageCombo->findData(sp.pageId);
		if (idx >= 0 && !*picked)
		{
			*picked = true;
			if (m_pageCombo->currentIndex() != idx)
				m_pageCombo->setCurrentIndex(idx);
			else if (m_lockGateKey != currentPageKey())
				checkAndTakePageLock();
		}
		const bool ready = *picked && m_lockGateKey == currentPageKey() && m_pageCombo->currentData().toString() == sp.pageId;
		if (!ready && el->elapsed() < 8000)
			return;
		t->stop();
		t->deleteLater();
		then(ready);
	});
	t->start();
}

void SuneerNewsPanel::noteDocOpened(ScribusDoc* doc)
{
	if (!doc || !ScCore->usingGUI() || m_wfOpening)
		return;                                     // Open page from workflow syncs on its own
	QPointer<ScribusDoc> d = doc;
	QTimer::singleShot(0, this, [this, d]() {
		if (!d || m_wfBusy)
			return;
		const SyncPage sp = docSyncPage(d);
		if (sp.pageId.isEmpty())
			return;
		const QString name = sp.pageName.isEmpty() ? tr("page ..%1").arg(sp.pageId.right(6)) : sp.pageName;
		syncLog(QStringLiteral("opened %1: workflow page %2 %3 %4 (%5)").arg(QFileInfo(d->documentFileName()).fileName(), name, sp.edition, sp.date,
		        sp.workflow ? QStringLiteral("downloaded page") : QStringLiteral("placed stories")));
		if (m_accessToken.isEmpty())
		{
			setStatus(tr("%1 is %2 in the workflow - log in and Fetch News to sync used / unused").arg(QFileInfo(d->documentFileName()).fileName(), name), true);
			return;
		}
		// asked every time a workflow page is opened (also when the News Browser is on that page already)
		{
			QString answer = m_syncTestAnswer;
			if (answer.isEmpty())
			{
				const int r = QMessageBox::question(this, tr("Sync with the workflow"),
				    tr("%1 is %2%3%4 in the workflow.\n\nSync its used / unused with the workflow now?\n"
				       "The News Browser shows that page and takes its page lock.")
				        .arg(QFileInfo(d->documentFileName()).fileName(), name,
				             sp.edition.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(sp.edition),
				             sp.date.isEmpty() ? QString() : QStringLiteral(", %1").arg(sp.date)),
				    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
				answer = r == QMessageBox::Yes ? QStringLiteral("sync") : QString();
			}
			m_syncTestLog << QStringLiteral("open question: %1").arg(answer.contains(QLatin1String("sync")) ? QStringLiteral("sync") : QStringLiteral("not now"));
			if (!answer.contains(QLatin1String("sync")))
				return;
		}
		switchPanelToPage(sp, [this, d, sp](bool ok) {
			if (!d)
				return;
			if (!ok)
			{
				setStatus(tr("Could not switch the News Browser to %1 - choose it and press Fetch News").arg(sp.pageName.isEmpty() ? sp.pageId : sp.pageName), true);
				return;
			}
			onFetchNews();                          // the list, then reconcilePage()
		});
	});
}

void SuneerNewsPanel::noteDocClosing(ScribusDoc* doc)
{
	if (!doc || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	QElapsedTimer t;
	t.start();
	if (doc == m_doc)
	{
		if (m_deleteCheckTimer && m_deleteCheckTimer->isActive())
		{
			m_deleteCheckTimer->stop();
			checkDeletedStories();                  // a delete of the last seconds still counts
		}
		pushOnPage(tr("close"));
	}
	retryAutoMarks();                               // the queue is per story and page, whatever document
	if (m_marksInFlight > 0)
	{
		// the page lock is let go right after this: the answers first, never more than 3 s
		QEventLoop loop;
		QTimer poll;
		poll.setInterval(20);
		connect(&poll, &QTimer::timeout, &loop, [this, &loop]() { if (m_marksInFlight <= 0) loop.quit(); });
		QTimer::singleShot(3000, &loop, &QEventLoop::quit);
		poll.start();
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	savePendingMarks();
	syncLog(QStringLiteral("closing %1: %2 ms, %3 mark(s) unanswered, %4 kept for later (pending-marks.json)")
	        .arg(QFileInfo(doc->documentFileName()).fileName()).arg(t.elapsed()).arg(m_marksInFlight).arg(m_autoRetry.size()));
}

// SUNEER_TAKEOVER_SELFTEST=<report prefix>: Take over page end to end against the mock workflow of
// tests/suneer/news_page_takeover.sh. This Scribus is user A (Locked Pages right); user B acts through
// the API with its own token. Page 2 held by B -> Continue anyway -> the sync is skipped -> Details ->
// Take over -> the sync runs. Without the right: no button. Same account elsewhere: nothing asked.
// Then the workflow learns the heartbeat (mock switch): A's heartbeat reaches it; B's stale lock is
// offered to A even without the right, B's fresh one is not.
void SuneerNewsPanel::runTakeOverSelfTest(const QString& report)
{
	struct Ctx
	{
		QStringList lines;
		int fails = 0;
		QList<std::function<void(std::function<void()>)>> steps;
		int step = 0;
		QString date;
		std::function<void(const QString&, const QString&)> measure;   // the lock line on screen
	};
	auto c = std::make_shared<Ctx>();
	c->date = qEnvironmentVariable("SUNEER_WFSYNC_DATE", "2026-11-02");
	auto check = [c](const QString& what, bool ok) {
		c->lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		c->fails += ok ? 0 : 1;
	};
	auto note = [c](const QString& l) { c->lines << QStringLiteral("  ") + l; };
	auto waitFor = [this](std::function<bool()> cond, int ms, std::function<void(bool)> done) {
		auto* t = new QTimer(this);
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		t->setInterval(100);
		connect(t, &QTimer::timeout, this, [t, el, cond, ms, done]() {
			const bool ok = cond();
			if (!ok && el->elapsed() < ms)
				return;
			t->stop();
			t->deleteLater();
			done(ok);
		});
		t->start();
	};
	// test-only and user B calls (token AT-B), straight to the mock
	auto call = [this](const QByteArray& verb, const QString& path, const QJsonObject& body, const QByteArray& token, std::function<void(int, QJsonObject)> done) {
		QNetworkRequest req(QUrl(apiUrl(path)));
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setRawHeader("Authorization", "Bearer " + token);
		QNetworkReply* r = m_nam->sendCustomRequest(req, verb, verb == "GET" ? QByteArray() : QJsonDocument(body).toJson());
		connect(r, &QNetworkReply::finished, this, [r, done]() {
			r->deleteLater();
			done(httpStatus(r), QJsonDocument::fromJson(r->readAll()).object());
		});
	};
	auto lockAs = [call, c](const QByteArray& token, const QString& page, std::function<void(int)> done) {
		QJsonObject b;
		b["publishDate"] = c->date;
		b["isLocked"] = true;
		call("PATCH", QStringLiteral("/news/layout/pages/%1/toggle-lock").arg(page), b, token, [done](int http, QJsonObject) { done(http); });
	};
	auto state = [call](std::function<void(QJsonObject)> done) {
		call("GET", QStringLiteral("/mock/news"), {}, "AT-A", [call, done](int, QJsonObject news) {
			call("GET", QStringLiteral("/mock/state"), {}, "AT-A", [news, done](int, QJsonObject locks) {
				QJsonObject o;
				o["news"] = news["data"].toObject()["news"];
				for (const QJsonValue& v : locks["data"].toArray())
					o[v.toObject()["pageId"].toString()] = v.toObject();
				done(o);
			});
		});
	};
	// choose a page; another user's -> the lock question answered "continue"
	auto choose = [this, waitFor](const QString& page, std::function<void(bool)> done) {
		m_pageLockTestAnswer = QStringLiteral("continue");
		m_pageCombo->setCurrentIndex(m_pageCombo->findData(page));
		waitFor([this]() { return m_lockGateKey == currentPageKey(); }, 8000, done);
	};
	// the panel really on screen (the Styles docker's News tab, switched on and in front): the lock
	// line must get every wrapped line. (An off-screen grab of the panel cut it - not the real layout.)
	c->measure = [this, check, note](const QString& what, const QString& shot) {
		m_mw->suneerSetNewsBrowserTabVisible(true);   // the test profile has the tab switched off
		for (QWidget* x = m_pageLockLabel; x && x->parentWidget(); x = x->parentWidget())
			if (auto* tw = qobject_cast<QTabWidget*>(x->parentWidget()->parentWidget()); tw && tw->indexOf(x) >= 0)
				tw->setCurrentWidget(x);
		for (int i = 0; i < 20; ++i)
			QCoreApplication::processEvents();
		const int lw = m_pageLockLabel->width(), lh = m_pageLockLabel->height(), need = m_pageLockLabel->heightForWidth(lw);
		const int line = m_pageLockLabel->fontMetrics().lineSpacing();
		note(QStringLiteral("%1 lock line on screen %2: width %3, height %4, needs %5 (~%6 lines)")
		     .arg(what).arg(m_pageLockLabel->isVisible()).arg(lw).arg(lh).arg(need).arg(qMax(1, need / qMax(1, line))));
		check(QStringLiteral("%1 lock line not cut on screen: height %2 >= %3 needed, at most one line (%4) of blank").arg(what).arg(lh).arg(need).arg(line),
		      m_pageLockLabel->isVisible() && lw > 40 && lh >= need && lh <= need + line);
		m_mw->grab().save(qEnvironmentVariable("SUNEER_TAKEOVER_SELFTEST") + QStringLiteral("-window-%1.png").arg(shot));
	};
	auto run = std::make_shared<std::function<void()>>();
	*run = [this, c, run, report]() {
		if (c->step >= c->steps.size())
		{
			c->lines << (c->fails ? QStringLiteral("FAILED: %1").arg(c->fails) : QStringLiteral("ALL PASS"));
			QFile f(report + "-report.txt");
			if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
				f.write(c->lines.join('\n').toUtf8() + "\n");
			f.close();
			fprintf(stderr, "TAKEOVER-SELFTEST done\n");
			QTimer::singleShot(300, this, [this]() { if (m_mw) m_mw->close(); else QApplication::exit(0); });
			return;
		}
		auto s = c->steps[c->step++];
		s([run]() { QTimer::singleShot(100, *run); });
	};
	for (UndoGui* g : m_mw->findChildren<UndoGui*>())
		UndoManager::instance()->removeGui(g);

	// 0. B opens Page 2 (and Page 4); A logs in, KNR, the test date
	c->steps << [this, c, check, waitFor, lockAs](std::function<void()> next) {
		lockAs("AT-B", "p-knr-2", [this, c, check, waitFor, lockAs, next](int h2) {
			lockAs("AT-B", "p-knr-4", [this, c, check, waitFor, next, h2](int h4) {
				for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
					if (auto* sw = qobject_cast<QStackedWidget*>(w->parentWidget()))
						sw->setCurrentWidget(w);
				m_dateEdit->blockSignals(true);
				m_dateEdit->setDate(QDate::fromString(c->date, "yyyy-MM-dd"));
				m_dateEdit->blockSignals(false);
				QNetworkReply* r = startLoginRequest(QStringLiteral("a@test.local"), QStringLiteral("S3cret-right"));
				connect(r, &QNetworkReply::finished, this, [this, r, check, waitFor, next, h2, h4]() {
					QString e;
					const bool ok = finishLogin(r, QStringLiteral("a@test.local"), false, 0, e);
					r->deleteLater();
					check(QStringLiteral("B locked Page 2 and 4 (HTTP %1/%2); login as A, rights: %3").arg(h2).arg(h4).arg(QStringList(m_permissions.values()).join(',')),
					      ok && h2 == 200 && h4 == 200 && m_permissions.contains("lockedPages.view"));
					setLocked(false);
					onFetchStatic();
					const int k = m_editionCombo->findData("KNR");
					if (k >= 0)
						m_editionCombo->setCurrentIndex(k);
					waitFor([this]() { return m_pageCombo->count() >= 8; }, 8000, [next](bool) { next(); });
				});
			});
		});
	};
	// 1. Page 2 (B's): Continue anyway -> download, the sync skipped, the button there, Details
	c->steps << [this, c, check, note, waitFor, choose](std::function<void()> next) {
		choose("p-knr-2", [this, c, check, note, waitFor, next](bool) {
			m_syncTestLog.clear();
			m_syncTestAnswer = QStringLiteral("unused,link");
			onOpenPageFromWorkflow();
			waitFor([this]() { for (const QString& l : m_syncTestLog) if (l.startsWith("blocked:")) return true; return false; }, 20000,
			        [this, c, check, note, next](bool skipped) {
				const QString line = m_pageLockLabel->text();
				note(QStringLiteral("lock line: ") + line);
				note(QStringLiteral("status: ") + statusText());
				// the panel really on screen (the Styles docker's News tab): the lock line gets every wrapped line
				c->measure(QStringLiteral("Page 2"), QStringLiteral("locked"));
				check(QStringLiteral("Continue anyway on B's Page 2: page opened, sync skipped, \"%1\" shown").arg(m_takeOverBtn->text()),
				      skipped && m_mw->doc && line.contains("otheruser") && !m_takeOverBtn->isHidden() && statusText().contains("skipped"));
				const QString d = pageLockDetails("p-knr-2", c->date);
				note(d);
				check(QStringLiteral("Details: owner, since, PC + heartbeat \"not recorded\", sync skipped and why, take over available"),
				      d.contains("Owner: otheruser") && d.contains("Since: ") && d.contains("PC: not recorded") && d.contains("Last heartbeat: not recorded")
				      && d.contains("Used / unused sync: skipped") && d.contains("only they change") && d.contains("Take over: available"));
				// the Details dialog itself (status line button)
				QTimer::singleShot(600, this, [this]() {
					for (QWidget* w : QApplication::topLevelWidgets())
						if (auto* dlg = qobject_cast<QDialog*>(w); dlg && dlg->isVisible() && dlg->windowTitle().contains("details"))
						{
							dlg->grab().save(qEnvironmentVariable("SUNEER_TAKEOVER_SELFTEST") + "-details.png");
							dlg->accept();
						}
				});
				onStatusDetails();
				next();
			});
		});
	};
	// 2. Take over -> the lock is A's on the server, the sync runs at once (n-1 USED, n-3 UNUSED, n-4 linked USED)
	c->steps << [this, c, check, note, waitFor, state, call](std::function<void()> next) {
		m_pageLockTakeTestAnswer = QStringLiteral("take");
		m_syncTestLog.clear();
		m_takeOverBtn->click();
		waitFor([this]() { for (const QString& l : m_syncTestLog) if (l.startsWith("answer: mark unused")) return true; return false; }, 15000,
		        [this, c, check, note, state, call, next](bool synced) {
			for (const QString& l : m_syncTestLog)
				note(l);
			QTimer::singleShot(1500, this, [this, c, check, note, state, call, next, synced]() {
				state([this, c, check, note, call, next, synced](QJsonObject s) {
					const QJsonObject n = s["news"].toObject();
					auto st = [&n](const char* id) { return n[id].toObject()["p-knr-2"].toString(); };
					check(QStringLiteral("server: Page 2 locked by %1; panel holds it (%2); button gone").arg(s["p-knr-2"].toObject()["lockedById"].toString(), m_heldPageId),
					      s["p-knr-2"].toObject()["lockedById"].toString() == "u-A" && m_heldPageId == "p-knr-2" && m_takeOverBtn->isHidden());
					check(QStringLiteral("sync ran right after: n-1=%1 n-2=%2 n-3=%3 n-4=%4").arg(st("n-1"), st("n-2"), st("n-3"), st("n-4")),
					      synced && st("n-1") == "USED" && st("n-2") == "USED" && st("n-3") == "UNUSED" && st("n-4") == "USED");
					note(QStringLiteral("lock line: ") + m_pageLockLabel->text());
					if (m_mw->doc && m_mw->doc->isModified())
						m_mw->slotFileSave();               // the headline link changed it: the quit asks nothing
					QJsonObject b;
					b["pageIds"] = QJsonArray { QStringLiteral("p-knr-2") };
					b["publishDate"] = c->date;
					call("PATCH", QStringLiteral("/news/layout/pages/lock"), b, "AT-B", [check, next](int http, QJsonObject) {
						check(QStringLiteral("B lost it: B's lock request now refused (HTTP %1)").arg(http), http == 400);
						next();
					});
				});
			});
		});
	};
	// 3. the workflow takes A's Locked Pages right: B's Page 4 shows no button, Details says why
	c->steps << [this, c, check, note, choose, call](std::function<void()> next) {
		QJsonObject perm;
		perm["user"] = QStringLiteral("u-A");
		perm["perm"] = QStringLiteral("lockedPages.view");
		perm["on"] = false;
		call("POST", QStringLiteral("/mock/perm"), perm, "AT-A", [this, c, check, note, choose, next](int, QJsonObject) {
		fetchCurrentUser();                         // /users/me: the rights as the workflow has them now
		choose("p-knr-4", [this, c, check, note, next](bool) {
			const QString d = pageLockDetails("p-knr-4", c->date);
			note(d.section('\n', -1));
			check(QStringLiteral("no Locked Pages right (rights now: %1): no button on B's Page 4; Details: take over not available (the right)")
			      .arg(QStringList(m_permissions.values()).join(',')),
			      !m_permissions.contains("lockedPages.view") && m_takeOverBtn->isHidden() && d.contains("Take over: not available") && d.contains("lockedPages.view"));
			next();
		});
		});
	};
	// 4. the same account elsewhere (A's own lock on Page 5): nothing asked, no button, sync allowed
	c->steps << [this, check, lockAs, choose](std::function<void()> next) {
		lockAs("AT-A", "p-knr-5", [this, check, choose, next](int h) {
			m_pageLockTestAnswer = QStringLiteral("choose");      // a question would go back - visible below
			m_pageCombo->setCurrentIndex(m_pageCombo->findData("p-knr-5"));
			QTimer::singleShot(2500, this, [this, check, next, h]() {
				check(QStringLiteral("own account's lock on Page 5 (HTTP %1): taken as yours, no button, sync allowed (%2)").arg(h).arg(m_pageLockLabel->text()),
				      m_pageCombo->currentData().toString() == "p-knr-5" && m_takeOverBtn->isHidden() && markGate("p-knr-5") == MarkGate::Allowed);
				next();
			});
		});
	};
	// 5. the workflow learns the heartbeat: A's goes out; B's stale Page 6 -> offered without the right; B's fresh Page 7 -> not
	c->steps << [this, c, check, note, call, lockAs, choose, waitFor, state](std::function<void()> next) {
		QJsonObject on;
		on["on"] = true;
		call("POST", QStringLiteral("/mock/heartbeat"), on, "AT-A", [this, c, check, note, call, lockAs, choose, waitFor, state, next](int, QJsonObject) {
			m_heartbeatUnsupported = false;
			sendLockHeartbeat();
			QTimer::singleShot(1000, this, [this, c, check, note, call, lockAs, choose, waitFor, state, next]() {
				state([this, c, check, note, call, lockAs, choose, waitFor, next](QJsonObject s) {
					const QJsonObject p5 = s["p-knr-5"].toObject();
					check(QStringLiteral("heartbeat route: A's Page 5 lastSeenAt %1, client \"%2\"").arg(p5["lastSeenAt"].toString(), p5["lockClient"].toString()),
					      !p5["lastSeenAt"].toString().isEmpty() && p5["lockClient"].toString() == QSysInfo::machineHostName());
					lockAs("AT-B", "p-knr-6", [this, c, check, note, call, lockAs, choose, waitFor, next](int) {
						lockAs("AT-B", "p-knr-7", [this, c, check, note, call, choose, waitFor, next](int) {
							QJsonObject st;
							st["pageId"] = QStringLiteral("p-knr-6");
							st["date"] = c->date;
							st["minutes"] = 5;
							st["client"] = QStringLiteral("CHN-DTP-B");
							call("POST", QStringLiteral("/mock/stale"), st, "AT-A", [this, c, check, note, choose, waitFor, next](int, QJsonObject) {
								choose("p-knr-7", [this, c, check, note, choose, waitFor, next](bool) {
									const bool freshHidden = m_takeOverBtn->isHidden();
									choose("p-knr-6", [this, c, check, note, waitFor, next, freshHidden](bool) {
										const QString d = pageLockDetails("p-knr-6", c->date);
										note(QStringLiteral("lock line: ") + m_pageLockLabel->text());
										c->measure(QStringLiteral("Page 6 (stale, the longest)"), QStringLiteral("stale"));
										note(d);
										check(QStringLiteral("no right: B's fresh Page 7 no button; B's stale Page 6 \"%1\"; Details: PC CHN-DTP-B, heartbeat 5 min ago - stale")
										      .arg(m_takeOverBtn->text()),
										      freshHidden && !m_takeOverBtn->isHidden() && m_takeOverBtn->text().contains("stale")
										      && d.contains("PC: CHN-DTP-B") && d.contains("5 min ago") && d.contains("stale"));
										m_pageLockTakeTestAnswer = QStringLiteral("take");
										m_takeOverBtn->click();
										waitFor([this]() { return m_heldPageId == QLatin1String("p-knr-6"); }, 8000, [check, next](bool ok) {
											check(QStringLiteral("stale Page 6 taken over without the right"), ok);
											next();
										});
									});
								});
							});
						});
					});
				});
			});
		});
	};
	(*run)();
}

// SUNEER_WFSYNC_SELFTEST=<report prefix>, SUNEER_WFSYNC_PHASE=1|2: the page sync end to end against
// the mock workflow of tests/suneer/news_page_sync.sh (fixtures/news_page_lock_mock.py), user A.
// Phase 1: Open page from workflow (KNR Page 2: two story frames the workflow's layout rows know,
// one frame only its headline links, one story used but not on the page), place 3 stories, a
// change on a page another user holds, delete 1 story while the server refuses the marks, Save,
// quit. Phase 2 (the saved file opened from disk, the server answering again): the open question,
// the sync, the waiting mark sent, the list; then another PC marks n-3 used again -> Keep.
// Phase 3 (restart, the News Browser already on Page 2): still asked; Keep remembered, no dialog.
// The script checks the server afterwards.
void SuneerNewsPanel::runSyncSelfTest(const QString& report)
{
	struct Ctx
	{
		QStringList lines;
		int fails = 0;
		QList<std::function<void(std::function<void()>)>> steps;
		int step = 0;
		QString date, path;
	};
	auto c = std::make_shared<Ctx>();
	const int phase = qEnvironmentVariableIntValue("SUNEER_WFSYNC_PHASE");
	c->date = qEnvironmentVariable("SUNEER_WFSYNC_DATE", "2026-11-02");
	c->path = pagesFolder() + QStringLiteral("/KNR/") + c->date + QStringLiteral("/Page_2.sla");
	auto check = [c](const QString& what, bool ok) {
		c->lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		c->fails += ok ? 0 : 1;
	};
	auto note = [c](const QString& l) { c->lines << QStringLiteral("  ") + l; };
	auto waitFor = [this](std::function<bool()> cond, int ms, std::function<void(bool)> done) {
		auto* t = new QTimer(this);
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		t->setInterval(100);
		connect(t, &QTimer::timeout, this, [t, el, cond, ms, done]() {
			const bool ok = cond();
			if (!ok && el->elapsed() < ms)
				return;
			t->stop();
			t->deleteLater();
			done(ok);
		});
		t->start();
	};
	// the mock's news_pages: "n-1" -> {"p-knr-2": "USED", ...}; plus every PATCH it took
	auto server = std::make_shared<QJsonObject>();
	auto poll = [this, server](std::function<void()> done) {
		QNetworkReply* r = m_nam->get(QNetworkRequest(QUrl(apiUrl(QStringLiteral("/mock/news")))));
		connect(r, &QNetworkReply::finished, this, [r, server, done]() {
			r->deleteLater();
			*server = QJsonDocument::fromJson(r->readAll()).object()["data"].toObject();
			done();
		});
	};
	auto st = [server](const QString& sid, const QString& page = QStringLiteral("p-knr-2")) {
		return (*server)["news"].toObject()[sid].toObject()[page].toString();
	};
	// wait until the server shows \a want ("n-1=USED", ...)
	auto waitServer = [this, poll, st, server](const QStringList& want, int ms, std::function<void(bool, QString)> done) {
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		auto again = std::make_shared<std::function<void()>>();
		*again = [this, poll, st, want, ms, done, el, again]() {
			poll([this, st, want, ms, done, el, again]() {
				QStringList got;
				bool ok = true;
				for (const QString& w : want)
				{
					const QString sid = w.section('=', 0, 0);
					got << sid + QLatin1Char('=') + st(sid);
					ok = ok && st(sid) == w.section('=', 1);
				}
				if (ok || el->elapsed() > ms)
				{
					done(ok, got.join(' '));
					return;
				}
				QTimer::singleShot(200, this, *again);
			});
		};
		(*again)();
	};
	auto mockPost = [this](const QString& path, const QJsonObject& body, const QByteArray& token, std::function<void()> done) {
		QNetworkRequest req(QUrl(apiUrl(path)));
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setRawHeader("Authorization", "Bearer " + token);
		QNetworkReply* r = path.endsWith(QLatin1String("/lock")) ? m_nam->sendCustomRequest(req, "PATCH", QJsonDocument(body).toJson())
		                                                         : m_nam->post(req, QJsonDocument(body).toJson());
		connect(r, &QNetworkReply::finished, this, [r, done]() { r->deleteLater(); done(); });
	};
	auto framesOf = [this](const QString& sid) {
		QList<PageItem*> out;
		if (!m_doc)
			return out;
		for (int i = 0; i < m_doc->Items->count(); ++i)
			if (attr(m_doc->Items->at(i), "news.serverId") == sid)
				out << m_doc->Items->at(i);
		return out;
	};
	auto syncLogHas = [this](const QString& s) {
		for (const QString& l : m_syncTestLog)
			if (l.contains(s))
				return true;
		return false;
	};
	auto run = std::make_shared<std::function<void()>>();
	*run = [this, c, run, report]() {
		if (c->step >= c->steps.size())
		{
			c->lines << (c->fails ? QStringLiteral("FAILED: %1").arg(c->fails) : QStringLiteral("ALL PASS"));
			QFile f(report + "-report.txt");
			if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
				f.write(c->lines.join('\n').toUtf8() + "\n");
			f.close();
			fprintf(stderr, "WFSYNC-SELFTEST done\n");
			QTimer::singleShot(300, this, [this]() { if (m_mw) m_mw->close(); else QApplication::exit(0); });
			return;
		}
		auto s = c->steps[c->step++];
		s([run]() { QTimer::singleShot(100, *run); });
	};
	for (UndoGui* g : m_mw->findChildren<UndoGui*>())
		UndoManager::instance()->removeGui(g);

	// 0. log in as A, KNR, the test date (phase 2: another date and page, so opening the file asks)
	c->steps << [this, c, check, waitFor, phase](std::function<void()> next) {
		for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
			if (auto* sw = qobject_cast<QStackedWidget*>(w->parentWidget()))
				sw->setCurrentWidget(w);
		m_dateEdit->blockSignals(true);
		m_dateEdit->setDate(phase == 2 ? QDate::fromString(c->date, "yyyy-MM-dd").addDays(1) : QDate::fromString(c->date, "yyyy-MM-dd"));
		m_dateEdit->blockSignals(false);
		QNetworkReply* r = startLoginRequest(QStringLiteral("a@test.local"), QStringLiteral("S3cret-right"));
		connect(r, &QNetworkReply::finished, this, [this, r, check, waitFor, next, phase]() {
			QString e;
			const bool ok = finishLogin(r, QStringLiteral("a@test.local"), false, 0, e);
			r->deleteLater();
			check(QStringLiteral("login as A"), ok);
			setLocked(false);
			onFetchStatic();
			const int k = m_editionCombo->findData("KNR");
			if (k >= 0)
				m_editionCombo->setCurrentIndex(k);
			waitFor([this]() { return m_pageCombo->count() >= 8; }, 8000, [this, waitFor, next, phase](bool) {
				m_pageCombo->setCurrentIndex(m_pageCombo->findData(phase == 2 ? "p-knr-5" : "p-knr-2"));   // phase 3: already the page
				waitFor([this]() { return m_lockGateKey == currentPageKey(); }, 8000, [next](bool) { next(); });
			});
		});
	};

	if (phase != 2 && phase != 3)
	{
		// 1. Open page from workflow: link by layout rows, mark the page's story USED, ask about the rest
		c->steps << [this, c, check, note, waitFor, waitServer, framesOf, syncLogHas](std::function<void()> next) {
			m_syncTestAnswer = QStringLiteral("unused,link");
			m_syncTestLog.clear();
			onOpenPageFromWorkflow();
			waitFor([syncLogHas]() { return syncLogHas(QStringLiteral("answer: linked")) && syncLogHas(QStringLiteral("answer: mark unused")); }, 25000,
			        [this, c, check, note, waitServer, framesOf, next](bool asked) {
				for (const QString& l : m_syncTestLog)
					note(l);
				const QString docFile = m_mw->doc ? m_mw->doc->documentFileName() : QString();
				check(QStringLiteral("Page 2 downloaded and opened (%1)").arg(docFile), docFile == c->path);
				check(QStringLiteral("the dialog asked (used but not on the page, headline link) and was answered"), asked);
				PageItem* f1 = framesOf("n-1").value(0);
				bool welded = false;
				for (PageItem* p : framesOf("n-1"))
					welded |= p->isImageFrame();
				QString head = f1 ? frameHeadline(f1).section(SpecialChars::PARSEP, 0, 0) : QString();
				bool ml = false;
				for (const QChar ch : head)
					ml |= ch.unicode() >= 0x0D00 && ch.unicode() <= 0x0D7F;
				check(QStringLiteral("layout rows linked frame 1 to n-1 (with its welded photo: %1) and frame 2 to n-2; Malayalam headline \"%2\"")
				      .arg(welded).arg(head.left(30)), f1 && welded && !framesOf("n-2").isEmpty() && ml);
				check(QStringLiteral("headline link: the moved frame is n-4 (%1 frame)").arg(framesOf("n-4").size()), framesOf("n-4").size() == 1);
				QFile sla(c->path);
				const QByteArray bytes = sla.open(QIODevice::ReadOnly) ? sla.readAll() : QByteArray();
				check(QStringLiteral("the downloaded file was saved with the linked ids (news.serverId n-1 in %1 bytes)").arg(bytes.size()),
				      bytes.contains("news.serverId") && bytes.contains("\"n-1\""));
				waitServer({ "n-1=USED", "n-2=USED", "n-3=UNUSED", "n-4=USED" }, 8000, [check, next](bool ok, QString got) {
					check(QStringLiteral("server: frame stories USED, the one not on the page UNUSED (%1)").arg(got), ok);
					next();
				});
			});
		};
		// 2. a page another user holds: no change goes out, nothing is queued
		c->steps << [this, c, check, note, mockPost, waitFor, poll, server](std::function<void()> next) {
			QJsonObject body;
			body["pageIds"] = QJsonArray { QStringLiteral("p-knr-1") };
			body["publishDate"] = c->date;
			mockPost(QStringLiteral("/news/layout/pages/lock"), body, "AT-B", [this, c, check, note, waitFor, poll, server, next]() {
				refreshPageLocks([this, c, check, note, waitFor, poll, server, next](bool ok, const QString&) {
					const int before = (*server)["patches"].toArray().size();
					DeletedStory d;
					d.storyId = QStringLiteral("other page story");
					d.serverId = QStringLiteral("n-8");
					d.pageId = QStringLiteral("p-knr-1");
					autoMarkStories({ d }, unusedStatus(), QStringLiteral("USED"), QStringLiteral("selftest"));
					QString err;
					const bool manual = markOnServer({ QStringLiteral("n-8") }, QStringLiteral("p-knr-1"), unusedStatus(), &err, QStringLiteral("USED"));
					note(QStringLiteral("manual mark answer: %1").arg(err));
					QTimer::singleShot(1500, this, [this, check, poll, server, next, before, ok, manual, d]() {
						poll([this, check, server, next, before, ok, manual, d]() {
							bool touched = false;
							for (const QJsonValue& v : (*server)["patches"].toArray())
								touched |= v.toObject()["newsIds"].toArray().contains(QStringLiteral("n-8"));
							check(QStringLiteral("Page 1 held by user B (lock list read %1): no PATCH for n-8 (%2 patches before), not queued, manual mark refused")
							      .arg(ok).arg(before), ok && !touched && !m_autoRetry.contains(retryKey(d)) && !manual
							      && (*server)["news"].toObject()["n-8"].toObject()["p-knr-1"].toString() == QLatin1String("USED"));
							next();
						});
					});
				});
			});
		};
		// 3. place three stories from the list -> USED
		c->steps << [this, check, note, waitFor, waitServer, framesOf](std::function<void()> next) {
			onFetchNews();
			int ticked = 0;
			for (int i = 0; i < m_newsList->count(); ++i)
			{
				QListWidgetItem* row = m_newsList->item(i);
				const int idx = row->data(Qt::UserRole).toInt();
				const QString sid = idx >= 0 && idx < m_newsData.size() ? newsServerId(m_newsData[idx].toObject()) : QString();
				const bool want = sid == QLatin1String("n-5") || sid == QLatin1String("n-6") || sid == QLatin1String("n-7");
				row->setCheckState(want ? Qt::Checked : Qt::Unchecked);
				ticked += want ? 1 : 0;
			}
			onPlaceSelected();
			note(QStringLiteral("ticked %1, status: %2").arg(ticked).arg(statusText()));
			check(QStringLiteral("placed n-5, n-6, n-7 (frames %1/%2/%3)").arg(framesOf("n-5").size()).arg(framesOf("n-6").size()).arg(framesOf("n-7").size()),
			      ticked == 3 && !framesOf("n-5").isEmpty() && !framesOf("n-6").isEmpty() && !framesOf("n-7").isEmpty());
			waitServer({ "n-5=USED", "n-6=USED", "n-7=USED" }, 8000, [check, next](bool ok, QString got) {
				check(QStringLiteral("server: the placed stories USED (%1)").arg(got), ok);
				next();
			});
		};
		// 4. the server refuses marks; delete n-6 -> queued, Save -> still queued (on disk)
		c->steps << [this, check, note, mockPost, waitFor, framesOf](std::function<void()> next) {
			QJsonObject on;
			on["on"] = true;
			mockPost(QStringLiteral("/mock/fail-patch"), on, "AT-A", [this, check, note, waitFor, framesOf, next]() {
				QList<PageItem*> items;
				const QString story = framesOf("n-6").isEmpty() ? QString() : attr(framesOf("n-6").first(), "news.story");
				if (!story.isEmpty())
					items = storyItems(story);
				Selection sel(this, false);
				for (PageItem* it : items)
					sel.addItem(it);
				m_doc->itemSelection_DeleteItem(&sel);
				DeletedStory k;
				k.serverId = QStringLiteral("n-6");
				k.pageId = QStringLiteral("p-knr-2");
				waitFor([this, k]() { return m_autoRetry.contains(retryKey(k)) && m_marksInFlight == 0; }, 12000, [this, check, note, k, framesOf, next](bool queued) {
					check(QStringLiteral("deleted n-6 (frames left %1) with the server refusing: UNUSED kept in the retry queue").arg(framesOf("n-6").size()),
					      queued && framesOf("n-6").isEmpty() && m_autoRetry.value(retryKey(k)).title == unusedStatus());
					m_mw->slotFileSave();
					QTimer::singleShot(2500, this, [this, check, note, k, next]() {
						QFile f(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news/pending-marks.json"));
						const QByteArray j = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
						note(QStringLiteral("pending-marks.json: ") + QString::fromUtf8(j).simplified());
						check(QStringLiteral("Save: the retry failed again, n-6 UNUSED still queued and on disk"),
						      m_autoRetry.contains(retryKey(k)) && j.contains("\"n-6\"") && j.contains("UNUSED"));
						m_syncTestAnswer.clear();
						next();
					});
				});
			});
		};
		// 5. quit: the close tries once more (the script checks the server: n-6 still USED, the file keeps it)
	}
	else if (phase == 3)
	{
		// 1. restart; the News Browser is on Page 2 already: still asked; n-3 was kept -> no dialog, stays USED
		c->steps << [this, c, check, note, waitFor, waitServer, syncLogHas](std::function<void()> next) {
			check(QStringLiteral("phase 3: Keep remembered after the restart (%1 kept: p-knr-2|n-3 %2)").arg(m_syncKept.size()).arg(m_syncKept.contains("p-knr-2|n-3")),
			      m_syncKept.contains("p-knr-2|n-3"));
			m_syncTestAnswer = QStringLiteral("sync,unused");   // a wrong question would mark n-3 unused - visible on the server
			m_syncTestLog.clear();
			noteDocOpened(m_mw->doc);
			waitFor([syncLogHas]() { return syncLogHas(QStringLiteral("sync (Fetch News)")); }, 20000, [this, c, check, note, waitServer, next](bool ok) {
				for (const QString& l : m_syncTestLog)
					note(l);
				bool dialog = false;
				for (const QString& l : m_syncTestLog)
					dialog |= l.startsWith(QLatin1String("dialog:"));
				check(QStringLiteral("asked on open although the News Browser was on Page 2 (%1); synced; no dialog for the kept story (%2)")
				      .arg(m_syncTestLog.value(0)).arg(dialog), ok && m_syncTestLog.value(0) == QLatin1String("open question: sync") && !dialog);
				QTimer::singleShot(1500, this, [waitServer, check, next]() {
					waitServer({ "n-3=USED", "n-5=USED", "n-6=UNUSED", "n-7=USED" }, 3000, [check, next](bool ok, QString got) {
						check(QStringLiteral("server: the kept n-3 still USED, the rest as before (%1)").arg(got), ok);
						next();
					});
				});
			});
		};
		// 2. Settings > Connection > Forget Keep -> forgotten (settings too); the next Check asks about n-3 again
		c->steps << [this, check, note, waitFor, syncLogHas](std::function<void()> next) {
			m_syncTestAnswer = QStringLiteral("forget");
			m_syncTestLog.clear();
			auto shown = std::make_shared<QString>();
			QTimer::singleShot(800, this, [this, shown]() {
				for (QWidget* w : QApplication::topLevelWidgets())
				{
					auto* dlg = qobject_cast<QDialog*>(w);
					QPushButton* b = dlg ? dlg->findChild<QPushButton*>("forgetKeepBtn") : nullptr;
					if (!b || !dlg->isVisible())
						continue;
					if (auto* tabs = dlg->findChild<QTabWidget*>())
						tabs->setCurrentIndex(0);
					QWidget* row = b->parentWidget();
					*shown = QStringLiteral("enabled %1").arg(b->isEnabled());
					for (QLabel* l : row->findChildren<QLabel*>())
						if (l->text().contains(QLatin1String("kept")))
							*shown += QStringLiteral(", \"%1\"").arg(l->text());
					dlg->grab().save(qEnvironmentVariable("SUNEER_WFSYNC_SELFTEST") + "-settings.png");
					b->click();
					for (QLabel* l : row->findChildren<QLabel*>())
						if (l->text().contains(QLatin1String("kept")))
							*shown += QStringLiteral(" -> \"%1\" (button enabled %2)").arg(l->text()).arg(b->isEnabled());
					dlg->reject();
				}
			});
			onSettings();
			const QStringList inConf = QSettings("Faircode", "ScribusNews").value("sync/kept").toStringList();
			note(QStringLiteral("Settings: ") + *shown);
			check(QStringLiteral("Settings > Forget Keep: %1; kept now %2, in the settings %3")
			      .arg(m_syncTestLog.join(' ')).arg(m_syncKept.size()).arg(inConf.size()),
			      shown->startsWith(QLatin1String("enabled 1")) && m_syncTestLog.contains(QStringLiteral("forgot keep: 1")) && m_syncKept.isEmpty() && inConf.isEmpty());
			m_syncTestAnswer = QStringLiteral("keep");
			m_syncTestLog.clear();
			onCheck();
			waitFor([syncLogHas]() { return syncLogHas(QStringLiteral("answer: keep")); }, 15000, [this, check, note, next](bool ok) {
				for (const QString& l : m_syncTestLog)
					note(l);
				check(QStringLiteral("after Forget Keep the next Check asks about n-3 again (answered Keep again)"),
				      ok && m_syncKept.contains("p-knr-2|n-3"));
				m_syncTestAnswer.clear();
				next();
			});
		};
	}
	else
	{
		// 1. the file opened from disk; logged in now -> the open question, the switch, the sync
		c->steps << [this, c, check, note, waitFor, syncLogHas](std::function<void()> next) {
			const QString docFile = m_mw->doc ? m_mw->doc->documentFileName() : QString();
			check(QStringLiteral("phase 2: Page_2.sla opened from disk (%1), %2 pending mark(s) loaded from the last session").arg(docFile).arg(m_autoRetry.size()),
			      docFile == c->path && m_autoRetry.size() == 1);
			m_syncTestAnswer = QStringLiteral("sync,keep");
			m_syncTestLog.clear();
			noteDocOpened(m_mw->doc);
			waitFor([syncLogHas]() { return syncLogHas(QStringLiteral("sync (Fetch News)")); }, 20000, [this, c, check, note, next](bool ok) {
				for (const QString& l : m_syncTestLog)
					note(l);
				bool dialog = false;
				for (const QString& l : m_syncTestLog)
					dialog |= l.startsWith(QLatin1String("dialog:"));
				check(QStringLiteral("asked to sync, switched to Page 2 %1 (panel: %2 %3), synced without a question (dialog shown: %4)")
				      .arg(c->date, m_pageCombo->currentText(), m_dateEdit->date().toString("yyyy-MM-dd")).arg(dialog),
				      ok && m_pageCombo->currentData().toString() == QLatin1String("p-knr-2") && !dialog
				      && m_syncTestLog.value(0) == QLatin1String("open question: sync"));
				next();
			});
		};
		// 2. the waiting UNUSED went out; the list shows the page as it is
		c->steps << [this, check, note, waitServer, waitFor](std::function<void()> next) {
			waitServer({ "n-5=USED", "n-6=UNUSED", "n-7=USED" }, 10000, [this, check, note, waitFor, next](bool ok, QString got) {
				check(QStringLiteral("server: 2 used, 1 unused of the 3 placed (%1)").arg(got), ok);
				onCheck();
				waitFor([this]() { return m_fetchBtn->isEnabled(); }, 10000, [this, check, note, next](bool) {
					QStringList rows;
					QHash<QString, QString> shown;
					for (const QJsonValue& v : m_newsData)
					{
						const QString sid = newsServerId(v.toObject());
						shown[sid] = newsDtpStatus(v.toObject());
						rows << sid + QLatin1Char('=') + shown[sid];
					}
					rows.sort();
					note(QStringLiteral("News Browser list: ") + rows.join(' '));
					check(QStringLiteral("News Browser shows n-5 USED, n-7 USED, n-6 UNUSED; queue empty (%1)").arg(m_autoRetry.size()),
					      shown.value("n-5") == "USED" && shown.value("n-7") == "USED" && shown.value("n-6") == "UNUSED" && m_autoRetry.isEmpty());
					check(QStringLiteral("whole page: n-1 n-2 n-4 USED, n-3 UNUSED"),
					      shown.value("n-1") == "USED" && shown.value("n-2") == "USED" && shown.value("n-4") == "USED" && shown.value("n-3") == "UNUSED");
					QWidget* page = m_stack;
					for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
						if (qobject_cast<QStackedWidget*>(w->parentWidget()))
							page = w;
					if (!page->isVisible())
						page->resize(320, 900);
					page->grab().save(qEnvironmentVariable("SUNEER_WFSYNC_SELFTEST") + "-panel.png");
					m_mw->grab().save(qEnvironmentVariable("SUNEER_WFSYNC_SELFTEST") + "-window.png");
					next();
				});
			});
		};
		// 3. another PC marks n-3 used for Page 2 again -> Check -> the dialog -> Keep (kept in the settings)
		c->steps << [this, check, note, mockPost, waitFor, syncLogHas](std::function<void()> next) {
			QJsonObject b;
			b["news"] = QStringLiteral("n-3");
			b["page"] = QStringLiteral("p-knr-2");
			b["status"] = QStringLiteral("USED");
			mockPost(QStringLiteral("/mock/set"), b, "AT-A", [this, check, note, waitFor, syncLogHas, next]() {
				m_syncTestAnswer = QStringLiteral("keep");
				m_syncTestLog.clear();
				onCheck();
				waitFor([syncLogHas]() { return syncLogHas(QStringLiteral("answer: keep")); }, 15000, [this, check, note, next](bool ok) {
					for (const QString& l : m_syncTestLog)
						note(l);
					const QStringList kept = QSettings("Faircode", "ScribusNews").value("sync/kept").toStringList();
					check(QStringLiteral("n-3 used again by another PC: asked, Keep -> in the settings (%1)").arg(kept.join(',')),
					      ok && kept.contains("p-knr-2|n-3"));
					m_syncTestAnswer.clear();
					next();
				});
			});
		};
	}
	(*run)();
}

// SUNEER_WFOPEN_SELFTEST=<report prefix>: Open page from workflow end to end against the mock
// workflow of tests/suneer/news_open_page.sh (fixtures/news_page_lock_mock.py): a plain .sla, an
// obituary page as a zip with its picture, no template for the day, the page's last-used template,
// HTTP 500 / 400 / 401, a server that never answers, the existing-file question (keep both / open /
// replace, replace refused while open), the page lock, the ids in the document, and placing for
// another page asking first. The script checks the files and the server state afterwards.
void SuneerNewsPanel::runWorkflowOpenSelfTest(const QString& report)
{
	struct Ctx
	{
		QStringList lines;
		int fails = 0;
		QList<std::function<void(std::function<void()>)>> steps;
		int step = 0;
		QString date, root;
	};
	auto c = std::make_shared<Ctx>();
	c->date = qEnvironmentVariable("SUNEER_WFOPEN_DATE", "2026-11-02");
	c->root = pagesFolder() + QStringLiteral("/KNR/") + c->date + QLatin1Char('/');
	auto check = [c](const QString& what, bool ok) {
		c->lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		c->fails += ok ? 0 : 1;
	};
	auto note = [c](const QString& l) { c->lines << QStringLiteral("  ") + l; };
	auto waitFor = [this](std::function<bool()> cond, int ms, std::function<void(bool)> done) {
		auto* t = new QTimer(this);
		auto el = std::make_shared<QElapsedTimer>();
		el->start();
		t->setInterval(50);
		connect(t, &QTimer::timeout, this, [t, el, cond, ms, done]() {
			const bool ok = cond();
			if (!ok && el->elapsed() < ms)
				return;
			t->stop();
			t->deleteLater();
			done(ok);
		});
		t->start();
	};
	// the server's lock table (mock only)
	auto lockState = [this](std::function<void(QHash<QString, QString>)> done) {
		QNetworkRequest req(QUrl(apiUrl(QStringLiteral("/mock/state"))));
		QNetworkReply* r = m_nam->get(req);
		connect(r, &QNetworkReply::finished, this, [r, done]() {
			r->deleteLater();
			QHash<QString, QString> h;
			for (const QJsonValue& v : QJsonDocument::fromJson(r->readAll()).object()["data"].toArray())
			{
				const QJsonObject o = v.toObject();
				h[o["pageId"].toString() + QLatin1Char('|') + o["date"].toString()] = o["lockedById"].toString();
			}
			done(h);
		});
	};
	// choose a page, wait for its lock check, press the button, wait until it is done
	auto openPage = [this, c, waitFor](const QString& pageId, std::function<void(bool)> done) {
		m_wfTestLog.clear();
		m_pageCombo->setCurrentIndex(m_pageCombo->findData(pageId));
		waitFor([this]() { return m_lockGateKey == currentPageKey(); }, 8000, [this, waitFor, done](bool) {
			onOpenPageFromWorkflow();
			waitFor([this]() { return !m_wfBusy && !m_wfTestLog.isEmpty(); }, 20000, done);
		});
	};
	auto docPath = [this]() { return m_mw && m_mw->doc ? m_mw->doc->documentFileName() : QString(); };
	auto readFile = [](const QString& p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
	auto run = std::make_shared<std::function<void()>>();
	*run = [this, c, run, report]() {
		if (c->step >= c->steps.size())
		{
			c->lines << (c->fails ? QStringLiteral("FAILED: %1").arg(c->fails) : QStringLiteral("ALL PASS"));
			QFile f(report + "-report.txt");
			if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
				f.write(c->lines.join('\n').toUtf8() + "\n");
			f.close();
			fprintf(stderr, "WFOPEN-SELFTEST done\n");
			QTimer::singleShot(300, this, [this]() { if (m_mw) m_mw->close(); else QApplication::exit(0); });
			return;
		}
		auto st = c->steps[c->step++];
		st([run]() { QTimer::singleShot(100, *run); });
	};
	m_wfTestAnswer = QStringLiteral("keep");

	// 0. log in as A, KNR, the test date
	c->steps << [this, c, check, waitFor](std::function<void()> next) {
		for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
			if (auto* sw = qobject_cast<QStackedWidget*>(w->parentWidget()))
				sw->setCurrentWidget(w);
		m_dateEdit->blockSignals(true);
		m_dateEdit->setDate(QDate::fromString(c->date, "yyyy-MM-dd"));
		m_dateEdit->blockSignals(false);
		QNetworkReply* r = startLoginRequest(QStringLiteral("a@test.local"), QStringLiteral("S3cret-right"));
		connect(r, &QNetworkReply::finished, this, [this, r, check, waitFor, next]() {
			QString e;
			const bool ok = finishLogin(r, QStringLiteral("a@test.local"), false, 0, e);
			r->deleteLater();
			check(QStringLiteral("login as A"), ok);
			setLocked(false);
			onFetchStatic();
			const int k = m_editionCombo->findData("KNR");
			if (k >= 0)
				m_editionCombo->setCurrentIndex(k);
			waitFor([this]() { return m_pageCombo->count() >= 8; }, 8000, [this, check, next](bool ok) {
				bool sideBySide = false;
				for (QHBoxLayout* hl : m_fetchBtn->parentWidget()->findChildren<QHBoxLayout*>())
					sideBySide |= hl->indexOf(m_fetchBtn) == 0 && hl->indexOf(m_openWfBtn) == 1;
				check(QStringLiteral("KNR pages listed (%1), button \"%2\" right of Fetch News in one row").arg(m_pageCombo->count()).arg(m_openWfBtn->text()),
				      ok && sideBySide);
				next();
			});
		});
	};
	// 1. Page 1: a plain .sla
	c->steps << [this, c, check, note, openPage, docPath, readFile, lockState](std::function<void()> next) {
		openPage("p-knr-1", [this, c, check, note, docPath, readFile, lockState, next](bool) {
			const QString p = c->root + "Page_1.sla";
			const QByteArray sla = readFile(p);
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("Page 1 saved as %1 (%2 bytes)").arg(p).arg(sla.size()), sla.contains("<SCRIBUSUTF8NEW"));
			check(QStringLiteral("Page 1 opened (%1)").arg(docPath()), docPath() == p);
			check(QStringLiteral("ids in the file: SuneerWfPageId/Date/TemplateId"),
			      sla.contains("SuneerWfPageId=\"p-knr-1\"") && sla.contains(QStringLiteral("SuneerWfDate=\"%1\"").arg(c->date).toUtf8()) && sla.contains("SuneerWfTemplateId=\"tpl-p1\""));
			check(QStringLiteral("ids in the open document: %1").arg(m_mw->doc ? QStringList(m_mw->doc->suneerWorkflow().values()).join(',') : QString()),
			      m_mw->doc && m_mw->doc->suneerWorkflow().value("pageId") == "p-knr-1" && m_mw->doc->suneerWorkflow().value("pageName") == "Page 1");
			check(QStringLiteral("Malayalam text kept (U+0D38 in the file)"), QString::fromUtf8(sla).contains(QChar(0x0D38)));
			lockState([this, c, check, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: Page 1 locked by u-A (%1), lock bound to the document (%2)").arg(h.value("p-knr-1|" + c->date)).arg(m_heldDoc == m_mw->doc),
				      h.value("p-knr-1|" + c->date) == "u-A" && m_heldDoc == m_mw->doc && m_heldDocBound);
				next();
			});
		});
	};
	// 2. Page 3 (obituary): a zip with the .sla and its picture
	c->steps << [this, c, check, note, openPage, docPath, readFile, lockState](std::function<void()> next) {
		openPage("p-knr-3", [this, c, check, note, docPath, readFile, lockState, next](bool) {
			const QString p = c->root + "Page_3.sla";
			const QByteArray sla = readFile(p);
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("obituary zip: Page_3.sla + Page_3_images/0_obit_photo.jpg"), sla.contains("<SCRIBUSUTF8NEW") && QFileInfo::exists(c->root + "Page_3_images/0_obit_photo.jpg"));
			check(QStringLiteral("picture reference rewritten: PFILE=\"Page_3_images/0_obit_photo.jpg\""), sla.contains("PFILE=\"Page_3_images/0_obit_photo.jpg\"") && !sla.contains("PFILE=\"images/"));
			bool shown = false;
			if (m_mw->doc)
				for (PageItem* it : *m_mw->doc->Items)
					if (it->isImageFrame() && it->imageIsAvailable)
						shown = true;
			check(QStringLiteral("Page 3 opened (%1), the picture loaded").arg(docPath()), docPath() == p && shown);
			m_mw->grab().save(qEnvironmentVariable("SUNEER_WFOPEN_SELFTEST") + "-obit-opened.png");
			lockState([this, c, check, next](QHash<QString, QString> h) {
				check(QStringLiteral("server: Page 3 locked by u-A; Page 1 now %1 (one held page per Scribus)").arg(h.value("p-knr-1|" + c->date).isEmpty() ? QStringLiteral("released") : h.value("p-knr-1|" + c->date)),
				      h.value("p-knr-3|" + c->date) == "u-A");
				next();
			});
		});
	};
	// 3. Page 4: the desk chose "no template" for the day
	c->steps << [this, c, check, note, openPage](std::function<void()> next) {
		openPage("p-knr-4", [this, c, check, note, next](bool) {
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("no template: said clearly, nothing saved"), m_wfTestLog.join(' ').contains("has no SLA template for Page 4") && !QFileInfo::exists(c->root + "Page_4.sla"));
			next();
		});
	};
	// 4. Page 5: day never settled -> the page's last-used template
	c->steps << [this, c, check, note, openPage, docPath, readFile](std::function<void()> next) {
		openPage("p-knr-5", [this, c, check, note, docPath, readFile, next](bool) {
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("Page 5: the page's last-used template (tpl-p5) opened"), docPath() == c->root + "Page_5.sla" && readFile(c->root + "Page_5.sla").contains("SuneerWfTemplateId=\"tpl-p5\""));
			next();
		});
	};
	// 5-7. HTTP 500, HTTP 400, no answer
	const QList<QPair<QString, QString>> errs = { { "p-knr-6", "HTTP 500" }, { "p-knr-8", "Template not found!" }, { "p-knr-7", "did not answer within" } };
	for (const auto& e : errs)
		c->steps << [this, c, check, note, openPage, e](std::function<void()> next) {
			openPage(e.first, [this, c, check, note, e, next](bool) {
				note(m_wfTestLog.join(" | "));
				check(QStringLiteral("%1: error \"%2\" shown, button usable again").arg(e.first, e.second), m_wfTestLog.join(' ').contains(e.second) && m_openWfBtn->isEnabled());
				next();
			});
		};
	// 8. a token the server refuses (no refresh possible) -> 401 text
	c->steps << [this, check, note, waitFor](std::function<void()> next) {
		const QString keepA = m_accessToken, keepR = m_refreshToken;
		m_accessToken = QStringLiteral("AT-wrong");
		m_refreshToken.clear();
		m_wfTestLog.clear();
		onOpenPageFromWorkflow();   // Page 7, its lock check already done
		waitFor([this]() { return !m_wfBusy && !m_wfTestLog.isEmpty(); }, 10000, [this, check, note, keepA, keepR, next](bool) {
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("401: \"log in again\" shown"), m_wfTestLog.join(' ').contains("HTTP 401"));
			m_accessToken = keepA;
			m_refreshToken = keepR;
			next();
		});
	};
	// 9. Page 1 again: Keep both -> Page_1_2.sla; Open existing -> the open Page_1.sla; Replace while open -> refused
	c->steps << [this, c, check, note, openPage, docPath](std::function<void()> next) {
		m_wfTestAnswer = QStringLiteral("keep");
		openPage("p-knr-1", [this, c, check, note, openPage, docPath, next](bool) {
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("Keep both: Page_1_2.sla made and opened, Page_1.sla untouched"), docPath() == c->root + "Page_1_2.sla" && QFileInfo::exists(c->root + "Page_1.sla"));
			m_wfTestAnswer = QStringLiteral("open");
			m_lockGateKey.clear();
			openPage("p-knr-1", [this, c, check, note, openPage, docPath, next](bool) {
				note(m_wfTestLog.join(" | "));
				check(QStringLiteral("Open existing: switched to Page_1.sla, no download"), docPath() == c->root + "Page_1.sla" && !m_wfTestLog.join(' ').contains("from the workflow"));
				m_wfTestAnswer = QStringLiteral("replace");
				m_lockGateKey.clear();
				openPage("p-knr-1", [this, check, note, next](bool) {
					note(m_wfTestLog.join(" | "));
					check(QStringLiteral("Replace while the file is open: refused"), m_wfTestLog.join(' ').contains("replace refused"));
					next();
				});
			});
		});
	};
	// 10. Page 5 closed, changed on disk, Replace -> the workflow's version again
	c->steps << [this, c, check, note, openPage, docPath, readFile, waitFor](std::function<void()> next) {
		const QString p5 = c->root + "Page_5.sla";
		bool closed = false;
		for (QMdiSubWindow* sw : m_mw->mdiArea->subWindowList())
		{
			auto* win = qobject_cast<ScribusWin*>(sw->widget());
			if (win && win->doc() && win->doc()->documentFileName() == p5)
			{
				m_mw->mdiArea->setActiveSubWindow(sw);
				QCoreApplication::processEvents();
				closed = m_mw->slotFileClose();
			}
		}
		{
			QFile f(p5);
			if (f.open(QIODevice::Append))
				f.write("<!-- local change -->\n");
		}
		check(QStringLiteral("Page_5.sla closed (%1) and changed locally").arg(closed), closed && readFile(p5).contains("local change"));
		m_wfTestAnswer = QStringLiteral("replace");
		openPage("p-knr-5", [this, p5, check, note, docPath, readFile, next](bool) {
			note(m_wfTestLog.join(" | "));
			check(QStringLiteral("Replace: Page_5.sla is the workflow's again and open"), docPath() == p5 && !readFile(p5).contains("local change"));
			next();
		});
	};
	// 11. the document is Page 5; the news list is Page 2 -> Place asks first
	c->steps << [this, c, check, note](std::function<void()> next) {
		m_fetchPageId = QStringLiteral("p-knr-2");
		m_fetchDate = c->date;
		m_wfTestLog.clear();
		m_wfTestAnswer = QStringLiteral("cancel");
		const bool a = confirmWorkflowPage(tr("Place"));
		note(m_wfTestLog.join(" | "));
		m_wfTestAnswer = QStringLiteral("go");
		const bool b = confirmWorkflowPage(tr("Place"));
		const bool again = confirmWorkflowPage(tr("Place"));
		m_fetchPageId = QStringLiteral("p-knr-5");
		const bool same = confirmWorkflowPage(tr("Place"));
		check(QStringLiteral("other list page: asks (Cancel stops=%1, anyway goes=%2, not asked twice=%3); same page never asks=%4").arg(!a).arg(b).arg(again).arg(same),
		      !a && b && again && same && m_wfTestLog.join(' ').contains("Page 5"));
		QWidget* page = m_stack;
		for (QWidget* w = m_stack; w && w->parentWidget(); w = w->parentWidget())
			if (qobject_cast<QStackedWidget*>(w->parentWidget()))
				page = w;
		if (!page->isVisible())
			page->resize(320, 900);
		page->grab().save(qEnvironmentVariable("SUNEER_WFOPEN_SELFTEST") + "-panel.png");
		next();
	};
	(*run)();
}
