#include "suneer_news_panel.h"
#include "scribus.h"
#include "hyphenator.h"
#include "scribusdoc.h"
#include "selection.h"
#include "pageitem.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QDate>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QEventLoop>
#include <QTimer>
#include <QToolButton>
#include "ui/faircodehelpviewer.h"
#include <QInputDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QFormLayout>
#include <QCryptographicHash>

SuneerNewsPanel::SuneerNewsPanel(ScribusMainWindow* parent)
    : QDockWidget("News Browser", parent)
    , m_mw(parent)
{
    m_nam = new QNetworkAccessManager(this);
    loadConfig();

    QWidget* w = new QWidget(this);
    QVBoxLayout* vl = new QVBoxLayout(w);
    vl->setSpacing(4);
    vl->setContentsMargins(4,4,4,4);

    // Top bar: Settings + Refresh
    QHBoxLayout* topBar = new QHBoxLayout();
    m_settingsBtn = new QPushButton("⚙", w);
    m_settingsBtn->setFixedWidth(30);
    m_settingsBtn->setToolTip("Settings (IP Config)");
    m_settingsBtn->setStyleSheet("font-size:14px;");
    connect(m_settingsBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onSettings);
    m_refreshBtn = new QPushButton("🔄", w);
    m_refreshBtn->setFixedWidth(30);
    m_refreshBtn->setToolTip("Refresh");
    m_refreshBtn->setStyleSheet("font-size:14px;");
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

    // Edition label (set in config)
    QHBoxLayout* edRow = new QHBoxLayout();
    edRow->addWidget(new QLabel("Edition:", w));
    m_editionLabel = new QLabel("", w);
    m_editionLabel->setStyleSheet("font-weight:bold; color:#1565C0;");
    edRow->addWidget(m_editionLabel);
    edRow->addStretch();
    vl->addLayout(edRow);
    m_editionCombo = new QComboBox(w);
    m_editionCombo->setVisible(false);

    // Page selector
    vl->addWidget(new QLabel("Page:", w));
    m_pageCombo = new QComboBox(w);
    vl->addWidget(m_pageCombo);

    // Date picker
    vl->addWidget(new QLabel("Date:", w));
    m_dateEdit = new QDateEdit(QDate::currentDate().addDays(1), w);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");
    m_dateEdit->setCalendarPopup(true);
    vl->addWidget(m_dateEdit);

    // Fetch button
    m_fetchBtn = new QPushButton("🔍 Fetch News", w);
    m_fetchBtn->setStyleSheet("background:#1565C0; color:white; font-weight:bold; padding:4px;");
    connect(m_fetchBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onFetchNews);
    vl->addWidget(m_fetchBtn);

    // Status
    m_statusLabel = new QLabel("", w);
    m_statusLabel->setStyleSheet("color:#666; font-size:11px;");
    vl->addWidget(m_statusLabel);

    // Select All checkbox
    m_selectAllChk = new QCheckBox("Select All", w);
    m_selectAllChk->setStyleSheet("font-size:11px; padding:2px;");
    connect(m_selectAllChk, &QCheckBox::toggled, this, &SuneerNewsPanel::onSelectAll);
    vl->addWidget(m_selectAllChk);
    // Splitter: news list + preview
    QSplitter* splitter = new QSplitter(Qt::Vertical, w);

    // News list — multi select
    m_newsList = new QListWidget();
    m_newsList->setWordWrap(false);
    m_newsList->setSpacing(2);
    m_newsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_newsList->setStyleSheet(
        "QListWidget { font-size:14px; }"
        "QListWidget::item { padding:6px; border-bottom:1px solid #ddd; }"
        "QListWidget::item:selected { background:#E3F2FD; color:#000; }"
        "QListWidget::item:hover { background:#F5F5F5; }"
        "QToolTip { font-size:14px; padding:6px; }");
    connect(m_newsList, &QListWidget::itemClicked,
            this, &SuneerNewsPanel::onNewsItemClicked);
    connect(m_newsList, &QListWidget::itemDoubleClicked,
            this, &SuneerNewsPanel::onPlaceNews);
    splitter->addWidget(m_newsList);



    vl->addWidget(splitter, 1);

    // Buttons row
    QHBoxLayout* btnRow = new QHBoxLayout();
    m_placeBtn = new QPushButton("📄 Place Selected", w);
    m_placeBtn->setStyleSheet("background:#2E7D32; color:white; font-weight:bold; padding:4px;");
    m_placeBtn->setEnabled(false);
    connect(m_placeBtn, &QPushButton::clicked, this, &SuneerNewsPanel::onPlaceSelected);
    // m_autoBtn removed
    btnRow->addWidget(m_placeBtn);
    vl->addLayout(btnRow);

    setWidget(w);
    setMinimumWidth(240);

    onFetchEditions();
}

void SuneerNewsPanel::loadConfig()
{
    QSettings s("Faircode", "ScribusNews");
    // Defaults to localhost, matching the Settings tab (see initSettingsTab).
    // The office API address is site-specific and is not baked into the source;
    // it is configured once per machine and lives in QSettings from then on.
    m_baseUrl        = s.value("apiUrl", "http://localhost:3011/api/v1/external").toString();
    m_imageBase      = "http://localhost:9000";
    m_defaultEdition = s.value("defaultEdition", "TVM").toString();
}

void SuneerNewsPanel::saveConfig(const QString& apiUrl, const QString& edition)
{
    QSettings s("Faircode", "ScribusNews");
    s.setValue("apiUrl", apiUrl);
    s.setValue("defaultEdition", edition);
    m_baseUrl        = apiUrl;
    m_defaultEdition = edition;
    // imageBase fixed to port 9000
    m_imageBase = "http://localhost:9000";
    s.setValue("imageBase", m_imageBase);
}

bool SuneerNewsPanel::verifyPassword(const QString& entered)
{
    // Password: "faircode2026" — stored as SHA256
    QString hash = QCryptographicHash::hash(
        entered.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString correct = QCryptographicHash::hash(
        "faircode2026", QCryptographicHash::Sha256).toHex();
    return hash == correct;
}

void SuneerNewsPanel::onSettings()
{
    bool ok;
    QString pass = QInputDialog::getText(this, "Settings", "Enter password:",
        QLineEdit::Password, "", &ok);
    if (!ok || !verifyPassword(pass)) {
        if (ok) QMessageBox::warning(this, "Error", "Wrong password!");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("Faircode News - Configuration");
    dlg.setMinimumWidth(500);
    dlg.setMinimumHeight(400);
    QVBoxLayout* mainVl = new QVBoxLayout(&dlg);
    QSettings s("Faircode", "ScribusNews");

    // Tab Widget
    QTabWidget* tabs = new QTabWidget(&dlg);
    mainVl->addWidget(tabs);

    // ── Tab 1: Connection ──────────────────────────
    QWidget* connTab = new QWidget();
    QFormLayout* fl = new QFormLayout(connTab);
    fl->setSpacing(10);
    QString curUrl = s.value("apiUrl", "http://localhost:3011/api/v1/external").toString();
    QString curEd  = s.value("defaultEdition", "KNR").toString();
    QLineEdit* urlEdit = new QLineEdit(curUrl, connTab);
    urlEdit->setPlaceholderText("http://IP:port/api/v1/external");
    fl->addRow("API Base URL:", urlEdit);
    QComboBox* edCombo = new QComboBox(connTab);
    edCombo->setMinimumWidth(200);
    fl->addRow("Default Edition:", edCombo);
    auto fetchEditions = [&]() {
        QUrl url2(urlEdit->text().trimmed() + "/editions");
        QNetworkRequest req2(url2);
        QNetworkAccessManager nam2;
        QNetworkReply* reply2 = nam2.get(req2);
        QEventLoop loop2;
        connect(reply2, &QNetworkReply::finished, &loop2, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop2, &QEventLoop::quit);
        loop2.exec();
        edCombo->clear();
        if (reply2->error() == QNetworkReply::NoError) {
            QJsonDocument doc2 = QJsonDocument::fromJson(reply2->readAll());
            QJsonArray arr = doc2.object()["data"].toArray();
            for (const QJsonValue& v : arr) {
                QJsonObject o = v.toObject();
                edCombo->addItem(o["name"].toString(), o["shortName"].toString());
            }
        }
        reply2->deleteLater();
        int idx = edCombo->findData(curEd);
        if (idx >= 0) edCombo->setCurrentIndex(idx);
    };
    fetchEditions();
    QPushButton* reloadBtn = new QPushButton("Reload Editions", connTab);
    connect(reloadBtn, &QPushButton::clicked, this, [&]() { fetchEditions(); });
    fl->addRow("", reloadBtn);
    tabs->addTab(connTab, "🔗 Connection");

    // ── Tab 2: Layout ──────────────────────────────
    QWidget* layoutTab = new QWidget();
    QFormLayout* fl2 = new QFormLayout(layoutTab);
    fl2->setSpacing(10);
    // Columns
    QSpinBox* colsSpin = new QSpinBox(layoutTab);
    colsSpin->setRange(1, 8);
    colsSpin->setValue(s.value("layout/columns", 3).toInt());
    fl2->addRow("Default Columns:", colsSpin);
    // Font size
    QDoubleSpinBox* fontSizeSpin = new QDoubleSpinBox(layoutTab);
    fontSizeSpin->setRange(6, 72);
    fontSizeSpin->setValue(s.value("layout/fontSize", 10.0).toDouble());
    fontSizeSpin->setSuffix(" pt");
    fl2->addRow("Body Font Size:", fontSizeSpin);
    // Image position
    QComboBox* imgPosCmb = new QComboBox(layoutTab);
    imgPosCmb->addItems({"Right", "Left", "Top", "None"});
    imgPosCmb->setCurrentText(s.value("layout/imagePosition", "Right").toString());
    fl2->addRow("Image Position:", imgPosCmb);
    // Image columns
    QSpinBox* imgColsSpin = new QSpinBox(layoutTab);
    imgColsSpin->setRange(1, 4);
    imgColsSpin->setValue(s.value("layout/imageColumns", 2).toInt());
    fl2->addRow("Image Columns:", imgColsSpin);
    // Override Column Gap
    QCheckBox* overrideGapChk = new QCheckBox("Override Guide Gap", layoutTab);
    overrideGapChk->setChecked(s.value("layout/overrideGap", false).toBool());
    fl2->addRow("Column Gap:", overrideGapChk);
    QDoubleSpinBox* colGapSpin = new QDoubleSpinBox(layoutTab);
    colGapSpin->setRange(0.5, 20.0);
    colGapSpin->setValue(s.value("layout/colGapMm", 2.5).toDouble());
    colGapSpin->setSuffix(" mm");
    colGapSpin->setEnabled(overrideGapChk->isChecked());
    connect(overrideGapChk, &QCheckBox::toggled, colGapSpin, &QDoubleSpinBox::setEnabled);
    fl2->addRow("Gap Size:", colGapSpin);
    tabs->addTab(layoutTab, "📐 Layout");

    // ── Tab 3: Image Settings ──────────────────────
    QWidget* imgTab = new QWidget();
    QFormLayout* fl3 = new QFormLayout(imgTab);
    fl3->setSpacing(10);
    QDoubleSpinBox* imgWspin = new QDoubleSpinBox(imgTab);
    imgWspin->setRange(10, 500);
    imgWspin->setValue(s.value("image/width", 80.0).toDouble());
    imgWspin->setSuffix(" mm");
    fl3->addRow("Image Width:", imgWspin);
    QDoubleSpinBox* imgHspin = new QDoubleSpinBox(imgTab);
    imgHspin->setRange(10, 500);
    imgHspin->setValue(s.value("image/height", 60.0).toDouble());
    imgHspin->setSuffix(" mm");
    fl3->addRow("Image Height:", imgHspin);
    QSpinBox* dpiSpin = new QSpinBox(imgTab);
    dpiSpin->setRange(72, 600);
    dpiSpin->setValue(s.value("image/dpi", 300).toInt());
    dpiSpin->setSuffix(" DPI");
    fl3->addRow("Image DPI:", dpiSpin);
    tabs->addTab(imgTab, "🖼 Image");

    // ── Tab 4: Style Settings ──────────────────────
    QWidget* styleTab = new QWidget();
    QFormLayout* fl4 = new QFormLayout(styleTab);
    fl4->setSpacing(10);
    // Title style
    QComboBox* titleStyleCmb = new QComboBox(styleTab);
    titleStyleCmb->addItems({"44 M", "52 M", "60 M", "36 M", "48 M"});
    titleStyleCmb->setCurrentText(s.value("style/titleStyle", "44 M").toString());
    fl4->addRow("Title Style:", titleStyleCmb);
    // Body style
    QLineEdit* bodyStyleEdit = new QLineEdit(s.value("style/bodyStyle", "02 BodyText").toString(), styleTab);
    fl4->addRow("Body Style:", bodyStyleEdit);
    // Kicker style
    QLineEdit* kickerStyleEdit = new QLineEdit(s.value("style/kickerStyle", "12 Kicker").toString(), styleTab);
    fl4->addRow("Kicker Style:", kickerStyleEdit);
    // Byline style
    QLineEdit* bylineStyleEdit = new QLineEdit(s.value("style/bylineStyle", "07 Byline").toString(), styleTab);
    fl4->addRow("Byline Style:", bylineStyleEdit);
    tabs->addTab(styleTab, "🎨 Style");

    // ── Buttons ────────────────────────────────────
    QDialogButtonBox* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    mainVl->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted) {
        // Save connection
        QString selEd = edCombo->currentData().toString();
        saveConfig(urlEdit->text().trimmed(), selEd);
        // Save layout
        s.setValue("layout/columns", colsSpin->value());
        s.setValue("layout/fontSize", fontSizeSpin->value());
        s.setValue("layout/imagePosition", imgPosCmb->currentText());
        s.setValue("layout/imageColumns", imgColsSpin->value());
        s.setValue("layout/overrideGap", overrideGapChk->isChecked());
        s.setValue("layout/colGapMm", colGapSpin->value());
        // Save image
        s.setValue("image/width", imgWspin->value());
        s.setValue("image/height", imgHspin->value());
        s.setValue("image/dpi", dpiSpin->value());
        // Save style
        s.setValue("style/titleStyle", titleStyleCmb->currentText());
        s.setValue("style/bodyStyle", bodyStyleEdit->text());
        s.setValue("style/kickerStyle", kickerStyleEdit->text());
        s.setValue("style/bylineStyle", bylineStyleEdit->text());
        m_statusLabel->setText("Config saved!");
        onRefresh();
    }
}

void SuneerNewsPanel::onRefresh()
{
    m_editionCombo->clear();
    m_pageCombo->clear();
    m_newsList->clear();
    m_placeBtn->setEnabled(false);
    onFetchEditions();
}

void SuneerNewsPanel::setDocument(ScribusDoc* doc)
{
    m_doc = doc;
}

void SuneerNewsPanel::onFetchEditions()
{
    m_statusLabel->setText("Loading editions...");
    QUrl url(m_baseUrl + "/editions");
    QNetworkRequest req(url);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onEditionsReceived(reply);
    });
}

void SuneerNewsPanel::onEditionsReceived(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // Fallback: add TVM manually
        m_editionCombo->addItem("TVM", "TVM");
        m_statusLabel->setText("Using default edition");
        onFetchPages();
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    m_editions = doc.object()["data"].toArray();
    m_editionCombo->clear();
    for (const QJsonValue& e : m_editions) {
        QJsonObject ed = e.toObject();
        m_editionCombo->addItem(ed["name"].toString(), ed["id"].toString());
    }
    if (m_editions.isEmpty()) {
        m_editionCombo->addItem("TVM", "TVM");
    }
    // Auto-select default edition
    int defIdx = m_editionCombo->findData(m_defaultEdition);
    if (defIdx < 0) defIdx = m_editionCombo->findText(m_defaultEdition);
    if (defIdx >= 0) m_editionCombo->setCurrentIndex(defIdx);
    // Update edition label
    m_editionLabel->setText(m_editionCombo->currentText());
    m_statusLabel->setText(QString("%1 pages loading...").arg(m_editionCombo->currentText()));
    onFetchPages();
}

void SuneerNewsPanel::onFetchPages()
{
    if (m_editionCombo->count() == 0) return;
    QString edId = m_editionCombo->currentData().toString();
    if (edId.isEmpty()) edId = m_editionCombo->currentText();
    QUrl url(m_baseUrl + "/edition-pages/" + edId);
    QNetworkRequest req(url);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onPagesReceived(reply);
    });
}

void SuneerNewsPanel::onPagesReceived(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    m_pages = doc.object()["data"].toArray();
    m_pageCombo->clear();
    for (const QJsonValue& p : m_pages) {
        QJsonObject pg = p.toObject();
        m_pageCombo->addItem(pg["name"].toString(), pg["id"].toString());
    }
    m_statusLabel->setText(QString("%1 pages loaded").arg(m_pages.size()));
}

void SuneerNewsPanel::onFetchNews()
{
    if (m_pageCombo->count() == 0) return;
    QString pageId = m_pageCombo->currentData().toString();
    QString date   = m_dateEdit->date().toString("yyyy-MM-dd");
    m_statusLabel->setText("Fetching news...");
    m_newsList->clear();
    m_placeBtn->setEnabled(false);
    QString url = QString("%1/edition-pages/%2/news?publishDate=%3")
        .arg(m_baseUrl).arg(pageId).arg(date);
    QUrl newsUrl(url);
    QNetworkRequest req(newsUrl);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onNewsReceived(reply);
    });
}

void SuneerNewsPanel::onNewsReceived(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText("❌ Error fetching news");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    m_newsData = doc.object()["data"].toArray();
    m_newsList->clear();
    for (int i = 0; i < m_newsData.size(); i++) {
        QJsonObject news = m_newsData[i].toObject();
        QString title  = news["title"].toString();
        QString kicker = news["kicker"].toString();
        bool hasImg    = !news["newsMedia"].toArray().isEmpty();
        QString display = kicker.isEmpty()
            ? title
            : QString("[%1]\n%2").arg(kicker).arg(title);
        // One line display, full text as tooltip
        QString oneLine = display.replace("\n", " ");
        QListWidgetItem* item = new QListWidgetItem(oneLine);
        item->setToolTip(display);
        item->setData(Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        if (hasImg)
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        m_newsList->addItem(item);
    }
    m_statusLabel->setText(QString("✓ %1 news items — Ctrl+Click to multi-select").arg(m_newsData.size()));
    if (m_newsData.size() > 0) {
        m_placeBtn->setEnabled(true);
    }
}

void SuneerNewsPanel::onNewsItemClicked(QListWidgetItem* item)
{
    // Check click position — if within checkbox area (~20px), don't open preview
    QPoint pos = m_newsList->viewport()->mapFromGlobal(QCursor::pos());
    QRect itemRect = m_newsList->visualItemRect(item);
    if (pos.x() - itemRect.left() < 24) {
        return; // checkbox area click, only toggle
    }
    int idx = item->data(Qt::UserRole).toInt();
    QJsonObject news = m_newsData[idx].toObject();
    showPreview(news);
}

void SuneerNewsPanel::showPreview(const QJsonObject& news)
{
    QString kicker  = news["kicker"].toString();
    QString title   = news["title"].toString();
    QString byline  = news["byline"].toString();
    QString newsContent = news["content"].toString();
    bool hasImg     = !news["newsMedia"].toArray().isEmpty();
    QString html;
    if (!kicker.isEmpty())
        html += QString("<span style='color:#E65100; font-size:26px; font-weight:bold;'>%1</span><br>").arg(kicker.toHtmlEscaped());
    html += QString("<b style='font-size:30px;'>%1</b>").arg(title.toHtmlEscaped());
    if (!byline.isEmpty())
        html += QString("<br><span style='color:#666; font-size:24px;'>By %1</span>").arg(byline.toHtmlEscaped());
    if (hasImg)
        html += "<br><span style='color:#1565C0; font-size:24px;'>\U0001f4f7 Has image</span>";
    html += QString("<br><br><span style='font-size:26px;'>%1</span>")
        .arg(newsContent.toHtmlEscaped());
    // Modal dialog
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(title);
    dlg->setMinimumSize(900, 700);
    QVBoxLayout* vl = new QVBoxLayout(dlg);
    QTextEdit* te = new QTextEdit(dlg);
    te->setReadOnly(true);
    te->setHtml(html);
    vl->addWidget(te);
    QPushButton* closeBtn = new QPushButton(tr("Close"), dlg);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    vl->addWidget(closeBtn);
    dlg->exec();
    delete dlg;
}

void SuneerNewsPanel::onPlaceNews()
{
    QListWidgetItem* sel = m_newsList->currentItem();
    if (!sel || !m_doc) return;
    int idx = sel->data(Qt::UserRole).toInt();
    QJsonObject news = m_newsData[idx].toObject();
    double mmToPt = 2.8346;
    double pageX = m_doc->currentPage()->xOffset() + m_doc->margins()->left();
    double pageY = m_doc->currentPage()->yOffset() + m_doc->margins()->top();
    double w = 70.0 * mmToPt;
    double h = 75.0 * mmToPt;
    downloadAndPlaceNews(news, pageX, pageY, w, h);
}

void SuneerNewsPanel::onPlaceSelected()
{
    if (!m_doc) {
        QMessageBox::warning(this, "Error", "No document open!");
        return;
    }
    QList<QListWidgetItem*> selected;
    for (int i = 0; i < m_newsList->count(); i++) {
        if (m_newsList->item(i)->checkState() == Qt::Checked)
            selected.append(m_newsList->item(i));
    }
    if (selected.isEmpty()) return;
    double mmToPt = 2.8346;

    double marginL = m_doc->margins()->left();
    double baseX = m_doc->currentPage()->xOffset() + marginL;
    double marginT = m_doc->margins()->top();
    double baseY = m_doc->currentPage()->yOffset() + marginT + 15.0 * mmToPt;
    int count = selected.size();
    double curY = baseY;

    for (int i = 0; i < count; i++) {
        int idx = selected[i]->data(Qt::UserRole).toInt();
        QJsonObject news = m_newsData[idx].toObject();
        int beforeCount = m_doc->DocItems.count();
        downloadAndPlaceNews(news, baseX, curY, 0, 0);
        // Get max bottom of all placed frames
        m_doc->changed();
        m_doc->regionsChanged()->update(QRectF());
        double maxBottom = curY;
        for (int di = beforeCount; di < m_doc->DocItems.count(); di++) {
            PageItem* pi = m_doc->DocItems.at(di);
            pi->layout();
            double bottom = pi->yPos() + pi->height();
            if (bottom > maxBottom) maxBottom = bottom;
        }
        curY = maxBottom;

    }
    m_statusLabel->setText(QString("Done!"));
}
void SuneerNewsPanel::onAutoLayout()
{
    if (!m_doc) {
        QMessageBox::warning(this, "Error", "No document open!");
        return;
    }
    QList<QListWidgetItem*> selected = m_newsList->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Auto Layout", "Select news items first!");
        return;
    }

    double pageX = m_doc->currentPage()->xOffset() + 10;
    double pageY = m_doc->currentPage()->yOffset() + 10;
    double pageW = m_doc->pageWidth() - 20;
    double pageH = m_doc->pageHeight() - 20;
    int count    = selected.size();

    // Auto layout grid
    int cols = (count <= 2) ? count : (count <= 4 ? 2 : 3);
    int rows = (count + cols - 1) / cols;
    double cellW = pageW / cols - 5;
    double cellH = pageH / rows - 5;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        double x = pageX + col * (cellW + 5);
        double y = pageY + row * (cellH + 5);
        int idx = selected[i]->data(Qt::UserRole).toInt();
        QJsonObject news = m_newsData[idx].toObject();
        downloadAndPlaceNews(news, x, y, cellW, cellH);
    }
    m_statusLabel->setText(QString("✓ Auto layout: %1 items (%2×%3 grid)").arg(count).arg(cols).arg(rows));
}

void SuneerNewsPanel::downloadAndPlaceNews(const QJsonObject& news,
    double x, double y, double w, double h)
{
    QString title    = news["title"].toString();
    QString content  = news["content"].toString();
    QString kicker   = news["kicker"].toString();
    QString byline   = news["byline"].toString();

    // Layout style
    QString layoutStyle = news["titleStyle"].toString().trimmed();
    if (layoutStyle.isEmpty()) layoutStyle = "L1-44M";

    bool hasKicker = layoutStyle.startsWith("L1-");
    // Settings-ൽ നിന്ന് styles read ചെയ്യൂ
    // Default fixed settings
    QSettings cfg("Faircode", "ScribusNews");
    QString titleStyleName = "44 M";
    if (layoutStyle.contains("52M")) titleStyleName = "52 M";
    else if (layoutStyle.contains("60M")) titleStyleName = "60 M";
    else if (layoutStyle.contains("36M")) titleStyleName = "36 M";
    else if (layoutStyle.contains("48M")) titleStyleName = "48 M";
    QString bodyStyle   = "02 BodyText";
    QString kickerStyle = "12 Kicker";
    QString bylineStyle = "07 Byline";
    // layoutTemplateId-ൽ നിന്ന് template settings fetch ചെയ്യൂ
    int cols = news["noOfColumns"].toInt();
    if (cols <= 0) cols = 4; // Default 4 columns

    QString layoutTemplateId = news["layoutTemplateId"].toString();
    qDebug() << "NEWS layoutTemplateId:" << layoutTemplateId << "keys:" << news.keys();
    if (!layoutTemplateId.isEmpty()) {
        QString dbgUrl = QString("%1/layout-templates/%2").arg(m_baseUrl).arg(layoutTemplateId);
        qDebug() << "Template URL:" << dbgUrl;
        QString baseUrlClean = QString(m_baseUrl).replace("/external", "");
        QString templateUrl = QString("%1/external/layout-templates/%2").arg(baseUrlClean).arg(layoutTemplateId);
        QNetworkAccessManager nam(this);
        QUrl tUrl(templateUrl);
        QNetworkRequest req(tUrl);
        QNetworkReply* reply = nam.get(req);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        qDebug() << "Reply error:" << reply->error() << reply->errorString();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument tdoc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject tmpl = tdoc.object()["data"].toObject();
            qDebug() << "Template data:" << tmpl;
            if (!tmpl.isEmpty()) {
                // Title Style override
                QString tTitle = tmpl["scribusTitleStyle"].toString();
                if (!tTitle.isEmpty()) titleStyleName = tTitle;
                // Columns override
                int tCols = tmpl["scribusColumns"].toInt();
                if (tCols > 0) cols = tCols;
            }
        }
        reply->deleteLater();
    }



    // Use page full width
    double pageW = m_doc->pageWidth();
    double marginL = m_doc->margins()->left();
    double marginR = m_doc->margins()->right();
    w = pageW - marginL - marginR;
    x = m_doc->currentPage()->xOffset() + marginL;
    if (y <= 0) y = m_doc->currentPage()->yOffset() + m_doc->margins()->top() + 16.0 * 2.8346; // top margin + date bar
    double mmToPt = 2.8346;
    double frameH = 200.0 * mmToPt;

    m_doc->m_Selection->clear();

    // Single text frame
    PageItem* textFrame = m_doc->createPageItem(
        PageItem::TextFrame, PageItem::Rectangle,
        x, y, w, frameH,
        0, CommonStrings::None, CommonStrings::None);

    if (textFrame) {
        // Read matching config from SuneerColumnConfig
        {
            QSettings cfgCol("Scribus", "SuneerColumnConfig");
            int numConfigs = cfgCol.beginReadArray("configs");
            double colGapMm = 4.0;
            double colWidthMm = 0.0;
            for (int ci = 0; ci < numConfigs; ++ci) {
                cfgCol.setArrayIndex(ci);
                int cn = cfgCol.value("columnNumber", 0).toInt();
                if (cn == cols) {
                    colGapMm   = cfgCol.value("columnGutter", 4.0).toDouble();
                    colWidthMm = cfgCol.value("columnWidth", 0.0).toDouble();
                    break;
                }
            }
            cfgCol.endArray();
            double mmToPt2 = 2.8346;
            // Get guide manager values from current page
            ScPage* curPage = m_doc->currentPage();
            double guideGapPt = 0;
            int guideColCount = 0;
            if (curPage) {
                guideColCount = curPage->guides.verticalAutoCount();
                guideGapPt = curPage->guides.verticalAutoGap();
            }
            // Use Guide Gap or Gutter based on config
            bool useGuideGap = false;
            for (int ci = 0; ci < numConfigs; ++ci) {
                cfgCol.setArrayIndex(ci);
                if (cfgCol.value("columnNumber", 0).toInt() == cols) {
                    useGuideGap = cfgCol.value("useGuideGap", false).toBool();
                    break;
                }
            }
            // useGuideGap=true → Gutter value, false → Guide Manager gap
            double colGapPt = useGuideGap ? colGapMm * mmToPt2 : (guideGapPt > 0 ? guideGapPt : colGapMm * mmToPt2);
            // Calculate single column width from guide manager
            double usableW = w; // page usable width
            double singleColW = 0;
            if (guideColCount > 0) {
                int actualGuideCols = guideColCount + 1;
                singleColW = (usableW - colGapPt * guideColCount) / actualGuideCols;
            } else {
                singleColW = (usableW - colGapPt * (cols - 1)) / cols;
            }
            double totalW = singleColW * cols + colGapPt * (cols - 1);
            textFrame->setWidth(totalW);
            textFrame->updateClip();
            textFrame->setColumns(cols);
            textFrame->setColumnGap(colGapPt);
        }
        textFrame->updateClip();

        ParagraphStyle hypStyle(textFrame->itemText.defaultStyle());
        hypStyle.setHyphenationMode(ParagraphStyle::HyphenationMode::AutomaticHyphenation);
        textFrame->itemText.setDefaultStyle(hypStyle);

        StoryText& story = textFrame->itemText;
        QChar para(13);

        auto applyStyle = [&](const QString& text, const QString& styleName) {
            if (text.trimmed().isEmpty()) return;
            int startPos = story.length();
            story.insertChars(text);
            story.insertChars(para);
            int endPos = story.length();
            ParagraphStyle pStyle;
            pStyle.setParent(styleName);
            story.applyStyle(startPos, pStyle, endPos - startPos);
        };

        // Kicker
        if (hasKicker && !kicker.isEmpty())
            applyStyle(kicker, kickerStyle);

        // Title
        applyStyle(title, titleStyleName);

        // Byline
        if (!byline.isEmpty())
            applyStyle(byline, bylineStyle);

        // Body
        QStringList bodyParas = content.split("\n");
        bool firstBody = true;
        for (const QString& para_text : bodyParas) {
            if (para_text.trimmed().isEmpty()) continue;
            applyStyle(para_text, firstBody ? "03 BodyNoInd" : bodyStyle);
            firstBody = false;
        }
        m_doc->DocItems.append(textFrame);
        m_doc->docHyphenator->slotHyphenate(textFrame);
        // Auto fit frame height
        m_doc->m_Selection->clear();
        m_doc->m_Selection->addItem(textFrame);
        if (m_mw)
            m_mw->suneerAutoFitHeight();
        m_doc->changed();
        m_doc->regionsChanged()->update(QRectF());
    }

    // Image frame — beside text
    int imgCols = cfg.value("layout/imageColumns", 2).toInt();
    QJsonArray media = news["newsMedia"].toArray();
    if (!media.isEmpty()) {
        QJsonObject mediaItem = media[0].toObject();
        qDebug() << "mediaItem keys:" << mediaItem.keys();
        qDebug() << "mediaItem:" << mediaItem;
        QJsonObject imgData = mediaItem["media"].toObject();
        qDebug() << "imgData keys:" << imgData.keys();
        qDebug() << "fileUrl:" << imgData["fileUrl"].toString();
        qDebug() << "fileName:" << imgData["fileName"].toString();
        QString localImg = downloadImage(imgData["fileUrl"].toString(), imgData["fileName"].toString());
        qDebug() << "localImg:" << localImg;
        // Image: second column, below heading
        int numCols = textFrame ? textFrame->columns() : cols;
        double colGap = textFrame ? textFrame->columnGap() : 4.0 * mmToPt;
        double colW = (w - (numCols - 1) * colGap) / numCols;
        qDebug() << "numCols:" << numCols << "colGap:" << colGap << "colW:" << colW << "w:" << w << "imgX:" << (x + colW + colGap);
        // Image width based on selected columns
        double imgW = colW * imgCols + (imgCols - 1) * colGap;
        // X = start of second column
        double imgX = x + colW + colGap;
        // Y = below kicker only
        double kickerH = (hasKicker ? 8.0 * mmToPt : 0.0);
        double imgY = y + kickerH;
        double imgH = cfg.value("image/height", 50.0).toDouble() * mmToPt;
        PageItem* imgFrame = m_doc->createPageItem(
            PageItem::ImageFrame, PageItem::Rectangle,
            imgX, imgY, imgW, imgH,
            0, CommonStrings::None, CommonStrings::None);
        if (imgFrame) {
            if (!localImg.isEmpty()) {
                m_doc->loadPict(localImg, imgFrame, false, true);
                // Fit frame height to image aspect ratio
                if (imgFrame->pixm.width() > 0 && imgFrame->pixm.height() > 0) {
                    double ratio = (double)imgFrame->pixm.height() / imgFrame->pixm.width();
                    imgFrame->setWidth(imgW);
                    imgFrame->setHeight(imgW * ratio);
                    imgFrame->updateClip();
                }
                // Use Scribus built-in fit: scale to frame
                imgFrame->setImageScalingMode(false, true); // free scaling, proportional
                imgFrame->adjustPictScale();
                imgFrame->setImageXYOffset(0, 0);
            }
            // Text wrap around image
            imgFrame->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
            m_doc->DocItems.append(imgFrame);
        }
    }

    m_doc->changed();
    m_doc->regionsChanged()->update(QRectF());
    if (m_mw) m_mw->emitUpdateRequest(0);
    if (m_mw) m_mw->suneerAutoFitHeight();
}


QString SuneerNewsPanel::downloadImage(const QString& url, const QString& filename)
{
    QString cacheDir = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + "/scribus_news/";
    QDir().mkpath(cacheDir);
    QString localPath = cacheDir + filename;
    if (QFile::exists(localPath)) return localPath;
    QString fullUrl = m_imageBase + url;
    QUrl imgUrl2(fullUrl);
    QNetworkRequest req(imgUrl2);
    QNetworkReply* reply = m_nam->get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    qDebug() << "Image download URL:" << fullUrl;
    qDebug() << "Image reply error:" << reply->error();
    qDebug() << "Image reply HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError) {
        QFile f(localPath);
        if (f.open(QIODevice::WriteOnly)) {
            qint64 written = f.write(reply->readAll());
            f.close();
            qDebug() << "Image written bytes:" << written;
        } else {
            qDebug() << "Image file open failed:" << f.errorString();
        }
    } else {
        qDebug() << "Image download error:" << reply->errorString();
    }
    reply->deleteLater();
    return localPath;
}

void SuneerNewsPanel::onSelectAll(bool checked)
{
    for (int i = 0; i < m_newsList->count(); i++) {
        m_newsList->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}
