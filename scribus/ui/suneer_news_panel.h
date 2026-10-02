#ifndef SUNEER_NEWS_PANEL_H
#define SUNEER_NEWS_PANEL_H
#include <QPointer>
#include <QDockWidget>
#include "scraction.h"
#include <QListWidget>
#include <QCheckBox>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDateEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QSplitter>
#include <QSettings>
#include <QDialog>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>

class ScribusDoc;
class ScribusMainWindow;

class SuneerNewsPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit SuneerNewsPanel(ScribusMainWindow* parent);
    void setDocument(ScribusDoc* doc);

private slots:
    void onFetchEditions();
    void onEditionsReceived(QNetworkReply* reply);
    void onFetchPages();
    void onPagesReceived(QNetworkReply* reply);
    void onFetchNews();
    void onNewsReceived(QNetworkReply* reply);
    void onNewsItemClicked(QListWidgetItem* item);
    void onSelectAll(bool checked);
    void onPlaceNews();
    void onPlaceSelected();
    void onAutoLayout();
    void onRefresh();
    void onSettings();

private:
    ScribusMainWindow* m_mw {nullptr};
    QPointer<ScribusDoc> m_doc;   // nulls itself when the document is deleted
    QNetworkAccessManager* m_nam {nullptr};

    QComboBox*   m_editionCombo  {nullptr};
    QComboBox*   m_pageCombo     {nullptr};
    QDateEdit*   m_dateEdit      {nullptr};
    QListWidget* m_newsList      {nullptr};
    QPushButton* m_fetchBtn      {nullptr};
    QPushButton* m_placeBtn      {nullptr};
    QPushButton* m_autoBtn       {nullptr};
    QPushButton* m_refreshBtn    {nullptr};
    QPushButton* m_settingsBtn   {nullptr};
    QLabel*      m_statusLabel   {nullptr};
    QCheckBox*   m_selectAllChk  {nullptr};
    Qt::CheckState m_lastCheckState {Qt::Unchecked};
    QLabel*      m_editionLabel  {nullptr};

    QJsonArray m_editions;
    QJsonArray m_pages;
    QJsonArray m_newsData;

    QString m_baseUrl;
    QString m_imageBase;
    QString m_defaultEdition;

    void loadConfig();
    void saveConfig(const QString& apiUrl, const QString& edition);
    bool verifyPassword(const QString& entered);
    void showPreview(const QJsonObject& news);
    void downloadAndPlaceNews(const QJsonObject& news, double x, double y, double w, double h);
    QString downloadImage(const QString& url, const QString& filename);
};
#endif
