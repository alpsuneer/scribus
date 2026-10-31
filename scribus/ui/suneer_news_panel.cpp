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
#include "sccolor.h"
#include "scupdatekeystore.h"
#include "selection.h"
#include "scribusview.h"
#include "pageitem_group.h"
#include "pagestructs.h"
#include "fpointarray.h"
#include "suneerimagelinks.h"
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
#include <QDir>
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
#include <QTime>
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
	m_pageCombo = new QComboBox(w);
	narrowCombo(m_pageCombo);
	connect(m_pageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { preselectLayoutModel(); });
	vl->addWidget(m_pageCombo);

	// Date
	vl->addWidget(new QLabel(tr("Date:"), w));
	m_dateEdit = new QDateEdit(QDate::currentDate().addDays(1), w);
	m_dateEdit->setDisplayFormat("yyyy-MM-dd");
	m_dateEdit->setCalendarPopup(true);
	vl->addWidget(m_dateEdit);

	// (Title style and Photo position are no longer chosen here: the headline size comes from the
	// placed width - Workflow columns -> width -> the model's / house headline rule - and the photo from
	// the Layout Model. Per story: right-click > Place with...)
	// Fetch
	m_fetchBtn = new SuneerElideButton(im.loadIcon("edit-find-replace"), tr("Fetch News"), w);
	m_fetchBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:4px;");
	connect(m_fetchBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onFetchNews);
	vl->addWidget(m_fetchBtn);

	// Status: a progress bar while something runs, then ONE short line;
	// everything long goes to the Details dialog (copyable).
	m_statusBar = new QProgressBar(w);
	m_statusBar->setFixedHeight(16);
	m_statusBar->setTextVisible(true);
	m_statusBar->setAlignment(Qt::AlignCenter);
	m_statusBar->setStyleSheet("QProgressBar { font-size:10px; } ");
	m_statusBar->setVisible(false);
	vl->addWidget(m_statusBar);
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
	if (doc != m_doc)
	{
		// another document: what was noted for the old one is void (the same document is set again
		// on every page change / undo - the pending deletes must survive that)
		m_deleted.clear();
		m_cutStories.clear();
		m_autoUnused.clear();
		m_autoRetry.clear();
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
	txt->setPlainText(m_statusLog.join(QStringLiteral("\n")));
	txt->moveCursor(QTextCursor::End);
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
// to the retry queue, which is tried again on the next delete and on Save.
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
			m_autoRetry.remove(d.storyId);
		}
		m_autoMarkLog << QStringLiteral("%1 %2 [%3]").arg(status, why, storyIds.join(','));
		const QString pageId = it.key();
		const QList<DeletedStory> group = it.value();
		if (fixture || pageId.isEmpty())
		{
			apiLog(QStringLiteral("auto mark %1 (%2): %3 - %4").arg(status, why, ids.join(','), fixture ? QStringLiteral("fixture, no request") : QStringLiteral("no page id, queued")));
			if (!fixture)
				for (const DeletedStory& d : group) { DeletedStory r = d; r.title = status; m_autoRetry.insert(d.storyId, r); }
			continue;
		}
		QJsonObject body;
		body["newsIds"]    = QJsonArray::fromStringList(ids);
		body["dtpStatus"]  = status;
		body["fromStatus"] = expected;
		body["pageId"]     = pageId;
		ensureSecretsLoaded();
		QNetworkRequest req = apiRequest(QUrl(apiUrl("/news/dtp-status")), true);
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setTransferTimeout(8000);               // short: a dead server never holds anything up
		QNetworkReply* reply = m_nam->sendCustomRequest(req, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
		const bool unused = status == unusedStatus();
		connect(reply, &QNetworkReply::finished, this, [this, reply, group, ids, status, why, unused, pageId]() {
			reply->deleteLater();
			const int http = httpStatus(reply);
			const bool ok = reply->error() == QNetworkReply::NoError && http >= 200 && http < 300;
			apiLog(QStringLiteral("PATCH /news/dtp-status {newsIds:[%1] (%2), dtpStatus:%3, pageId:..%4} -> HTTP %5 %6 (async, %7)")
			       .arg(ids.join(',')).arg(ids.size()).arg(status, pageId.right(6)).arg(http).arg(ok ? QStringLiteral("ok") : describeError(reply), why));
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
					m_autoRetry.insert(d.storyId, r);
				}
			}
			QString msg;
			if (ok)
				msg = unused ? tr("%n story(ies) marked unused on the server (%1) - Undo (Ctrl+Z) brings them back and marks them used again", "", names.size()).arg(why)
				             : tr("%n story(ies) marked used again on the server (%1)", "", names.size()).arg(why);
			else
				msg = tr("Warning: %n story(ies) could not be marked %1 on the server (%2) - kept for a retry on the next delete or Save: %3", "", names.size())
				          .arg(unused ? tr("unused") : tr("used"), describeError(reply), names.join(QStringLiteral(", ")));
			setStatus(msg, !ok);
			if (m_mw)
				m_mw->setStatusBarInfoText(msg);
			refreshUsedBadges();
		});
	}
}

void SuneerNewsPanel::refreshUsedBadges()
{
	if (m_newsList)
	{
		m_newsList->viewport()->update();
		updateTickButtons();
	}
}

// Retry of failed marks: called from the next delete check and from Save - never from a timer.
void SuneerNewsPanel::retryAutoMarks()
{
	if (m_autoRetry.isEmpty() || !qEnvironmentVariableIsEmpty("SCRIBUS_NEWS_FIXTURE"))
		return;
	QHash<QString, QList<DeletedStory>> byStatus;
	for (const DeletedStory& d : m_autoRetry)
		byStatus[d.title] << d;
	m_autoRetry.clear();                          // a failure puts them back
	for (auto it = byStatus.constBegin(); it != byStatus.constEnd(); ++it)
		autoMarkStories(it.value(), it.key(), it.key() == unusedStatus() ? QStringLiteral("USED") : unusedStatus(), tr("retry"));
}

//! Save: the moment to send what failed earlier.
void SuneerNewsPanel::noteSaved()
{
	retryAutoMarks();
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
		m_autoRetry.remove(d.storyId);        // a pending "unused" for it is void now
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
	setStatus(tr("Logged in as %1").arg(m_userName));
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
			QString type = pg["pageType"].toString();
			if (type.isEmpty())
				type = pg["type"].toString();
			if (!type.isEmpty())
				m_pageTypes[pg["id"].toString()] = type;
		}
		int idx = m_lastPage.isEmpty() ? -1 : m_pageCombo->findText(m_lastPage);
		if (idx >= 0)
			m_pageCombo->setCurrentIndex(idx);
		m_pageCombo->blockSignals(false);
		preselectLayoutModel();
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
	m_fetchPageId = m_pageCombo->currentData().toString();
	m_fetchDate   = m_dateEdit->date().toString("yyyy-MM-dd");
	m_lastPage    = m_pageCombo->currentText();
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
		setStatus(tr("No news for %1 on %2").arg(m_pageCombo->currentText(), m_fetchDate));
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
	obitFillCmb->setToolTip(tr("Face crop in Haar frontal-face box terms (measured on the reference strip): the face box is this share of the frame width, centred, its top %1 %% of the frame width below the frame top - with a little room above the hair (tall hair: the crop moves up or zooms out a little, the top of the head is never cut), only neck and collar below the chin. Sources smaller than 300 ppi get the same crop and an orange \"low res\" flag.").arg(qRound(om.faceTop * 100)));
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
