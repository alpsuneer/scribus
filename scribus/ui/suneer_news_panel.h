#ifndef SUNEER_NEWS_PANEL_H
#define SUNEER_NEWS_PANEL_H

#include <functional>

#include <QDockWidget>
#include "undoobject.h"
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QPair>
#include <QRectF>
#include <QMap>
#include <QSet>
#include <QList>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QEvent;
class QFontMetrics;
class QStackedWidget;
class ScribusDoc;
class ScribusMainWindow;
class PageItem;

/*!
 News Browser: pulls stories from the Deshabhimani workflow server and places
 them on the page as frames.

 Server contract (see result-20261007-1200-news-api-investigation.md):
   GET  /api/v1/common/web/initial-data/static              no login; edition list
   GET  /api/v1/external/edition-pages/<shortName>          no login; pages
   GET  /api/v1/external/edition-pages/<pageId>/news?publishDate=YYYY-MM-DD
   GET  <filesBase><media.fileUrl>                          picture bytes
 Login:  POST /api/v1/users/auth/login {email,password,deviceId} -> data.accessToken,
         data.user; Set-Cookie refreshToken (HttpOnly, 30 d). Access token 20 min.
 Refresh: POST /api/v1/users/auth/refresh, Cookie refreshToken + x-device-id -> accessToken
 Logout: POST /api/v1/users/auth/logout (Bearer)
 apiRequest() is the one place that builds requests and adds Authorization: Bearer.
 Secrets (tokens, deviceId, user name) live in the OS keychain via ScUpdateKeyStore.
 */
class UndoState;
class SuneerNewsPanel : public QDockWidget, public UndoObject
{
	Q_OBJECT
public:
	explicit SuneerNewsPanel(ScribusMainWindow* parent);
	void setDocument(ScribusDoc* doc);

	//! Plain paragraphs from the server's content field, which may be HTML.
	//! Tags are dropped, <p>/<br>/<div>/<li> ends become paragraph breaks and
	//! entities are decoded. The text itself is not normalised.
	static QStringList contentToParagraphs(const QString& content);

private slots:
	void onFetchStatic();
	void onEditionChanged(int index);
	void onFetchPages();
	void onFetchNews();
	void onNewsItemClicked(QListWidgetItem* item);
	void onSelectAll(bool checked);
	void onPlaceNews();
	void onPlaceSelected();
	void onRefresh();
	void onSettings();
	void onLogin();
	void onLogout();

private:
	// ---- UI ---------------------------------------------------------------
	ScribusMainWindow* m_mw {nullptr};
	QPointer<ScribusDoc> m_doc;   // nulls itself when the document is deleted

	QComboBox*   m_editionCombo  {nullptr};
	QComboBox*   m_pageCombo     {nullptr};
	QComboBox*   m_titleStyleCmb {nullptr};
	QComboBox*   m_photoPosCmb   {nullptr};
	QDateEdit*   m_dateEdit      {nullptr};
	QListWidget* m_newsList      {nullptr};
	QPushButton* m_fetchBtn      {nullptr};
	QPushButton* m_placeBtn      {nullptr};
	QPushButton* m_refreshBtn    {nullptr};
	QPushButton* m_settingsBtn   {nullptr};
	QLabel*      m_statusLabel   {nullptr};
	QLabel*      m_userLabel     {nullptr};
	QLabel*      m_lockLabel     {nullptr};
	QPushButton* m_loginBtn      {nullptr};
	QPushButton* m_logoutBtn     {nullptr};
	QStackedWidget* m_stack      {nullptr};   // 0 = locked, 1 = panel
	QCheckBox*   m_selectAllChk  {nullptr};

	QJsonArray m_editions;   // from initial-data/static
	QJsonArray m_pages;      // /external/edition-pages/<shortName>
	QJsonArray m_newsData;   // data[] of the last news fetch

	// ---- configuration (ScribusNews.conf, nothing secret) -----------------
	QString m_apiHost;     // http://10.199.100.133:3011
	QString m_filesBase;   // http://10.199.100.133/files
	QString m_lastEdition; // shortName
	QString m_lastPage;    // page name
	QString m_editionShortNames;   // fallback list, comma separated

	void loadConfig();
	void saveConfig();
	QString apiUrl(const QString& path) const;   // m_apiHost + "/api/v1" + path
	static QString defaultFilesBaseFor(const QString& apiHost);

	// ---- session (QtKeychain via ScUpdateKeyStore) -------------------------
	QString m_accessToken;
	QString m_refreshToken;
	QString m_deviceId;
	QString m_userName;
	bool    m_secretsLoaded {false};
	bool    m_loginDialogOpen {false};
	bool    m_quietAuth {false};   // startup: never pop the login dialog

	void ensureSecretsLoaded();
	void saveSession();
	void clearSession();
	void captureRefreshCookie(QNetworkReply* reply);
	bool isLoggedIn() const { return !m_accessToken.isEmpty(); }
	void setLocked(bool locked);
	bool refreshAccessToken();   // blocking
	bool loginDialog();          // modal; true when logged in
	bool doLogin(const QString& email, const QString& password, QString& error);
	void startupSession();

	// ---- allowed editions (from user.editionGroups; memory only) ---------
	QList<QPair<QString, QString>> m_allowedEditions;   // (edition id, name)
	QSet<QString> m_allowedShortNames;                  // filled when matched with the static list
	bool m_userInfoLoaded {false};
	QString m_clientIp;     // what the server saw at login / profile
	int     m_ipCode {0};   // approval code from the server's IP table
	bool    m_ipAllowed {true};
	void parseUser(const QJsonObject& user);
	bool fetchCurrentUser();   // GET /users/me -> name + allowed editions
	bool editionAllowed(const QString& shortName) const;
	void setEditionsEnabled(bool on);

	// ---- requests -----------------------------------------------------------
	//! Blocking GET through apiRequest(). On 401/403 the token is refreshed
	//! once and the request repeated; if that fails the login dialog is shown
	//! (unless quiet) and the request repeated after a successful login.
	void apiGet(const QUrl& url, std::function<void(QNetworkReply*)> done, bool retried = false);
	QNetworkReply* postBlocking(const QNetworkRequest& req, const QByteArray& body);

	QNetworkAccessManager* m_nam {nullptr};
	QNetworkRequest apiRequest(const QUrl& url, bool bearer) const;
	QString describeError(QNetworkReply* reply) const;
	static int httpStatus(QNetworkReply* reply);
	static QString serverMessage(const QByteArray& body);

	// ---- news ---------------------------------------------------------------
	QString m_fetchPageId;
	QString m_fetchDate;
	void showNewsList();
	void relabelNewsList();            // elide titles to the list width
	bool eventFilter(QObject* obj, QEvent* ev) override;
	static QString collapseSpaces(const QString& text);
	static QIcon photoIcon(int count);   // theme picture icon, count badge when > 1
	static QString elideGraphemes(const QString& text, const QFontMetrics& fm, int width);
	void showPreview(const QJsonObject& news);
	void fillTitleStyleCombo();

	// ---- placement ---------------------------------------------------------
	//! Places one story (headline frame, body frame, photos, captions) with
	//! its top-left at (x, y). Returns the created top-level items. With
	//! \a ownTransaction the story is one undo step by itself; otherwise the
	//! caller's transaction covers it.
	QList<PageItem*> placeNews(const QJsonObject& news, double x, double y, bool ownTransaction);

	// ---- story layout engine -------------------------------------------
	//! The frames of one story. The headline frame is full story width and
	//! single column; the body frame carries the columns. Photos never
	//! overlap the headline frame, so a headline is never wrapped.
	struct StoryFrames
	{
		PageItem* text {nullptr};    // the one story frame (kicker, headline, dateline, body)
		QList<PageItem*> photos;
		QList<PageItem*> captions;   // parallel to photos (may hold nullptr)
		double headH {0.0};          // laid-out height of kicker + headline block
	};
	//! Arranges the frames: photo position key is one of
	//! top, topAbove, right, left, middle, bottom, none.
	void layoutStory(StoryFrames& f, double x, double y, int cols, int imgCols, const QString& pos);
	void fitFrame(PageItem* frame);
	void setWrap(PageItem* item, bool on);
	static void setAttr(PageItem* item, const QString& name, const QString& value);
	static QString attr(const PageItem* item, const QString& name);
	QString currentPhotoPos() const;
	int storyImageCols(const QJsonObject& news) const;

public:
	//! True for an item the News Browser placed (carries the news.story id).
	static bool isNewsStory(const PageItem* item);
	//! Re-arranges the photos of the story \a item belongs to: \a pos (key, or
	//! empty to keep) and \a imgCols (<= 0 to keep). One undo step.
	void rearrangeStory(PageItem* item, const QString& pos, int imgCols);
	//! Selects every item of the story \a item belongs to.
	void selectWholeStory(PageItem* item);
	static QStringList photoPositionKeys();
	static QString photoPositionLabel(const QString& key);
private:
	//! Frame width and column gap for \a cols columns from the column config.
	void storyGeometry(int cols, double& totalW, double& colGapPt) const;
	int storyColumns(const QJsonObject& news) const;
	//! Tags the items of one story with a common story id (no grouping).
	QString tagStoryItems(const QList<PageItem*>& items, const QString& title, const QString& pos, int imgCols, int cols, const QString& serverId);
	// ---- "placed" state: the SERVER's news_pages.dtp_status is the truth ----
	QSet<QString> m_pendingMarks;              // server ids placed locally but not yet marked USED
	QPushButton* m_checkBtn {nullptr};
	QPushButton* m_releaseBtn {nullptr};
	QTimer* m_placedTimer {nullptr};           // unused (kept for ABI of this class only)
	static bool newsIsUsed(const QJsonObject& news);
	bool apiPatch(const QUrl& url, const QJsonObject& body, std::function<void(QNetworkReply*)> done, bool retried = false);
	bool markOnServer(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error);
	void markPlaced(const QList<QJsonObject>& placedStories);
	void onCheck();
	void onReleaseRows();
	//! Bulk "Mark unused (N)": every ticked USED row, one confirmation, optional
	//! removal from this document in one undo step, per-story result summary.
	void onMarkUnusedTicked();
	//! Enable Place / Mark unused according to what is ticked (free vs used).
	void updateTickButtons();
	//! Deletes the story's frames and records the server mark for undo/redo.
	//! Runs inside the caller's undo transaction; no question asked here.
	bool removeStoryFrames(PageItem* item, QString* error);
	QCheckBox*   m_selectUsedChk {nullptr};
public:
	//! "Mark as unused" (server dtpStatus BALANCED) for the story \a item belongs to; asks first.
	void releaseStory(PageItem* item);
	//! "Remove story (mark unused)": deletes every item of the story (one undo
	//! step) and marks it unused on the server; undo restores and re-marks used.
	void removeStory(PageItem* item);
	//! UndoObject: replays the server mark on undo/redo of removeStory.
	void restore(UndoState* state, bool isUndo) override;
	//! One-time hint when story frames are deleted with the plain Delete key.
	void deleteKeyHint();
	bool m_removing {false};
	int  m_placeUsedSkipped {0};
	bool m_deleteHintShown {false};
	void setRowStatus(const QString& serverId, const QString& dtpStatus);   // update one row, no re-fetch
private:
	QTimer* m_previewTimer {nullptr};     // single click waits for a possible double-click
	QListWidgetItem* m_previewRow {nullptr};
	static QString newsServerId(const QJsonObject& news);   // server news.id
	static QString newsRefId(const QJsonObject& news);
	void refreshPlacedMarks();                 // list icons/colours/tooltips from server status
	bool isPlacedRow(const QListWidgetItem* item) const;
	PageItem* findStoryItemInDoc(const QString& serverId) const;
	QList<PageItem*> storyItems(const QString& storyId) const;
	static QRectF itemsRect(const QList<PageItem*>& items);
	void moveStory(const QList<PageItem*>& items, double dx, double dy);
	//! Stacks the stories on the pasteboard beside the current page.
	void placeOnPasteboard(const QList<QJsonObject>& stories, bool rightSide);
	void showPlacedArea(const QRectF& stackRect);
	//! Downloads into the per-user cache. Empty on failure; \a why then says
	//! what happened ("HTTP 403 …", "cache not writable"), with no secrets.
	QString downloadImage(const QString& fileUrl, QString* why = nullptr);
	static QString newsCaption(const QJsonObject& news);
	static QString newsHighlights(const QJsonObject& news);
	QString styleOrEmpty(const QString& name, const QString& what, QStringList& missing) const;
};
#endif
