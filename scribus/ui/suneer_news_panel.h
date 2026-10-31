#ifndef SUNEER_NEWS_PANEL_H
#define SUNEER_NEWS_PANEL_H

#include <QPointer>
#include <QDateTime>
#include "suneerspacing.h"
#include "suneerlayoutmodel.h"
#include "ui/suneerlayoutmodels.h"
#include <functional>
#include <optional>

#include <QDockWidget>
#include <QListWidget>
#include <QMimeData>
#include "undoobject.h"
#include <QPointer>
#include <QColor>
#include <QHash>
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
class QDialog;
struct SuneerDupItem;
struct SuneerDupPair;
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
class SimpleState;
struct SuneerObitModel;
class SuneerObitPhotoWorker;
namespace SuneerLayout { struct StoryIn; }
struct SuneerPageModel;
struct SuneerPageGroup;
class QToolButton;
class SuneerNewsPanel : public QDockWidget, public UndoObject
{
	Q_OBJECT
public:
	//! Fetch the Workflow originals of \a items again (force: cache bypassed, old file kept as *_prev). Returns the count done.
	int redownloadPhotos(const QList<PageItem*>& items, bool force, QString* report = nullptr);
	//! Extra lines for the Details log / tooltip (no-photo list, checks, server errors).
	void setStatusDetails(const QString& text);
	explicit SuneerNewsPanel(ScribusMainWindow* parent);
	void setDocument(ScribusDoc* doc);
	//! Exit: release the workflow page lock this Scribus holds and wait (at most 3 s) for releases on the way.
	void releasePageLockOnExit();

	//! Plain paragraphs from the server's content field, which may be HTML.
	//! Tags are dropped, <p>/<br>/<div>/<li> ends become paragraph breaks and
	//! entities are decoded. The text itself is not normalised.
	static QStringList contentToParagraphs(const QString& content);
	/*! The desk's column count for a story: the News Browser override ("_colsOverride", right-click
	    Place as N columns), else Workflow's "Columns" field (News.noOfColumns, 1..8). 0 = not set. */
	static int requestedColumns(const QJsonObject& news);
	//! Headline style for a story \a cols wide: the model's headline rule, else the house table (autolayout-rules.json modes); empty = the panel's style
	QString headlineStyleForWidth(int cols) const;
	//! Per-story "Place with…" overrides stored on the story JSON (_headStyle / _photoPos); empty = automatic
	static QString requestedHeadStyle(const QJsonObject& news) { return news.value(QStringLiteral("_headStyle")).toString(); }
	static QString requestedPhotoPos(const QJsonObject& news) { return news.value(QStringLiteral("_photoPos")).toString(); }
	//! The row badge ("3 col* · 36 M · Left") for the story's column count / "Place with" overrides
	static QString overrideBadge(const QJsonObject& news);
	void relabelOverrideBadge(QListWidgetItem* row, const QJsonObject& news);

	//! Layout > Layout Models...: the model editor (page hooks for "Apply spacing to this page").
	void openLayoutModels();
	//! Layout > Auto Layout... / News Browser "Gallery...": the current page arranged by every model.
	void openLayoutGallery();
	//! Layout > Page Models...: groups, cards, create from old pages, skeleton on this page (ui/suneer_news_pagemodels.cpp).
	void openPageModelGallery();
	/*! The skeleton of a page model on the current page: one non-printing placeholder frame per slot and an outline per ad
	    space on the layer "Page model"; nothing on the page is moved or deleted. \a interactive asks "Replace skeleton?".
	    One undo step. Returns the report; empty = cancelled. */
	QString makePageSkeleton(const SuneerPageModel& model, const SuneerPageGroup& group, bool interactive);
	//! A story built exactly inside the placeholder (headline by slot width, photo by slot, text fitted, overflow / short marked); the placeholder goes.
	QString fillSlot(PageItem* placeholder, QJsonObject news);
	//! Canvas drop of News Browser row \a newsIndex (m_newsData) on \a item: fills the slot when \a item is a placeholder.
	bool dropStoryOnItem(PageItem* item, int newsIndex);
	//! A News Browser row dropped on a page model slot (ScribusView): the duplicate check first
	//! ([Place anyway] [Skip] [Show]), then dropStoryOnItem - right after the drop event, not inside it.
	void handleStoryDrop(PageItem* slot, int newsIndex);
	//! Right-click on a placeholder: the one ticked story into it (Place Selected with the placeholder selected).
	void placeTickedStoryInto(PageItem* placeholder);
	//! Layout > Remove Empty Slots: every placeholder / ad outline left on the current page. Returns how many.
	int removeEmptySlots(bool interactive);
	/*! suneer 2026-10-28 Layout > Re-pack Page (Ctrl+Alt+R, Edit toolbar, page context menu; ui/suneer_news_repack.cpp):
	    every module's text frame fitted to its last line, then the modules moved so no gap is wider than the
	    model's module gap and nothing overlaps. Obituary page: obit modules in columns 1-8 in reading order to
	    the highest free slot of their own column, 2-column news only in the news columns. Normal
	    page: only News Browser stories, up in their own columns; ads, hand-made, locked and pinned frames stay.
	    What no longer fits goes to the pasteboard. One undo step. Returns the one-line result. */
	QString repackPage(bool interactive);
	//! Layout > Re-pack Automatically (news/repackAuto, default off)
	static bool repackAutoPref();
	static void setRepackAutoPref(bool on);
	//! the document deletes \a items: an obituary module among them -> its columns re-packed ~1 s later (own undo step)
	void noteRepackDelete(const QList<PageItem*>& items);
	//! text editing of \a item ended: an obituary module's text frame -> fitted and its columns re-packed ~1 s later
	void noteRepackTextEdit(PageItem* item);
	static bool isSlotPlaceholder(const PageItem* item);
	QList<PageItem*> skeletonItems(int pageIdx) const;
	//! The ads on a page now (mm, page relative).
	QList<QRectF> pageAdRectsMm(int pageIdx) const;
	//! A story known only from the server's list has no body: fetch the full record (GET /news/:id) first. False: not loaded (told).
	bool ensureFullStory(QJsonObject& news);
	void runPageModelSelfTest(const QString& outPath);
	void newsItemDiagnostics(int pageIdx, const std::function<void(const QString&)>& line);
	/*! The top of a text frame that follows a photo, caption or name frame: first line offset = font ascent
	    (the first baseline is then the font's ascent below the top inset, not a whole line or the glyphs'
	    own height), top inset \a topPt, no space before on the first paragraph, no empty first lines. */
	void tidyTextTop(PageItem* text, double topPt, bool withUndo);
	/*! Obituary module (photo + name/caption/text frames stacked): every text frame fitted to its last line
	    with a 0 bottom inset, each next frame \a gapPt under the previous one; the first stays where it is.
	    Returns the number of frames moved or resized. */
	int stackObitFrames(const QList<PageItem*>& items, double gapPt, bool withUndo);
	QString m_lastSlotReport;                      //!< fillSlot's last report line (tests)
	//! Place Selected with exactly one placeholder selected on the page: the one ticked story goes into it. False: not that case.
	bool placeIntoSelectedSlot(const QList<QJsonObject>& stories);
	//! SUNEER_PAGEMODEL_IMPORT=<files/folders;...>: the import dialog run unattended, every page's group written to the report.
	void runPageModelImportTest(const QString& outPath);
	//! Re-arranges the stories placed on the current page with \a modelId (hand-placed frames stay). One undo step; the report is shown.
	void relayoutPage(const QString& modelId);
	//! Layout > Page Modules...: the modules of the current page as outlines, merge / split / keep fixed.
	void openPageModules();
	//! Gallery Apply: the current page arranged with \a modelId the way the card showed it (\a keep, the new stories). Returns the report.
	QString arrangePage(const QString& modelId, int keep, bool withNew, bool selectionOnly = false);
	/*!
	 \brief A Layout Gallery card: the current page copied into a hidden document and arranged there
	 with \a modelId by arrangePage() - the code Apply runs - then drawn \a widthPx wide. The real
	 document is not touched (no undo step, no network while the copy is arranged).
	 */
	SuneerLayoutGallery::Preview previewArrange(const QString& modelId, int keep, bool selectionOnly, const QList<PageItem*>& selection, int widthPx);
	/*!
	 \brief Regression test (SUNEER_LAYOUT_SELFTEST=<report file>, run once after a document is opened):
	 for every layout model the Gallery card, Apply on the real page (compared with the card), then Undo
	 (the page must be exactly as before). Writes one line per model and quits Scribus (exit 0 = all passed).
	 */
	void runLayoutSelfTest(const QString& outPath);
	//! Every layout model (id, name), for the Re-layout menus.
	QList<QPair<QString, QString>> pageModelChoices() const;
	//! The active layout model (the dropdown, or the one being applied).
	QString activeModelId() const;

public slots:
	//! The configuration dialog (server, layout, styles). Also reachable from
	//! Windows → News Browser Settings… so a wrong server address can always
	//! be corrected, logged in or not.
	void onSettings();

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
	void onLogin();
	void onLogout();

private:
	// ---- UI ---------------------------------------------------------------
	ScribusMainWindow* m_mw {nullptr};
	QPointer<ScribusDoc> m_doc;   // nulls itself when the document is deleted

	QComboBox*   m_editionCombo  {nullptr};
	QComboBox*   m_pageCombo     {nullptr};
	QDateEdit*   m_dateEdit      {nullptr};

	// ---- page lock: the workflow's LayoutPageStatus (lockedById / lockedAt per page + publish date),
	// the same lock the web Layout page sets. GET /news/layout/pages reads it, PATCH
	// /news/layout/pages/lock sets the user's whole set of locked pages for a date (the server refuses a
	// page another user holds). The server has no expiry: a lock lasts until it is released.
	struct PageLockInfo
	{
		QString userId;
		QString userName;
		QDateTime since;
		bool completed {false};
		// Not in the workflow yet (spec of 2026-10-30): its last heartbeat and the PC. Empty until it sends them.
		QDateTime lastSeen;
		QString client;
	};
	QHash<QString, PageLockInfo> m_pageLocks;   // page id -> lock on m_pageLocksDate (no entry = free)
	QString m_pageLocksDate;
	bool    m_pageLocksKnown {false};           // m_pageLocks answered for the current edition + date
	QString m_userId;                           // the logged-in user's id (login reply / users/me)
	QString m_heldPageId;                       // the page this Scribus holds the lock for
	QString m_heldPageName;
	QString m_heldDate;
	bool    m_heldWasMineBefore {false};        // already this user's (e.g. in the browser): never released here
	bool    m_heldDocBound {false};             // taken with a document open: released when it closes
	QPointer<ScribusDoc> m_heldDoc;
	QSet<QString> m_lockOverride;               // "pageId|date": Continue anyway was chosen
	QString m_lockGateKey;                      // "pageId|date" whose lock check finished (Fetch News may go on)
	int     m_lockCheckGen {0};                 // newer page / date choice: an older answer is dropped
	int     m_prevPageIndex {-1};
	bool    m_pageLockAsking {false};
	class QTimer* m_lockKeepAlive {nullptr};
	class QTimer* m_lockDateDebounce {nullptr};
	QLabel* m_pageLockLabel {nullptr};          // the line under the Page box
	QPushButton* m_takeOverBtn {nullptr};       // under that line: another user's page -> take its lock
	QSet<QString> m_permissions;                // the login's permissions (user.permissions, granted ones)
	bool m_heartbeatUnsupported {false};        // the workflow has no lock heartbeat route (404): not tried again
	QString m_pageLockTakeTestAnswer;           // self-test: "take" / "cancel" for the take-over question
	//! A lock with a heartbeat older than 3 min (only when the workflow sends lastSeenAt).
	static bool lockIsStale(const PageLockInfo& info);
	//! May this user take \a info's page over? (another user's lock + the Locked Pages right, or a stale lock)
	bool canTakeOver(const PageLockInfo& info, QString* why = nullptr) const;
	void onTakeOverPage();
	//! Owner, PC, since, last heartbeat, why the sync is skipped, take over or not - for Details.
	QString pageLockDetails(const QString& pageId, const QString& date) const;
	void sendLockHeartbeat();
	void runTakeOverSelfTest(const QString& report);
	// ---- Process obituary photos (pages opened from the workflow) ----
	void maybeAutoProcessObitPhotos();
	void finishObitWfBatch(bool cancelled, bool ok, const QString& error);
	//! A "Process obituary photos" that ends before the worker: the reason shown (status, Details, a box) and logged.
	void obitWfNothing(const QString& msg, const QString& details = QString());
	void showObitWfResult(const QString& text, bool problem);
	QString m_obitWfSaveTo;                     // self-test: where an unsaved page is saved
	bool restoreObitWfPhotos(UndoState* state, bool isUndo);
	QPointer<ScribusDoc> m_obitWfDoc;           // the document of the running batch
	QHash<QString, PageItem*> m_obitWfFrames;   // worker item id -> frame
	QHash<QString, QString> m_obitWfOrig;       // id -> the original in <sla>_photos/original
	QHash<QString, QString> m_obitWfUrl;        // id -> workflow URL (when it was downloaded)
	QStringList m_obitWfSkipped, m_obitWfFailed;   // "frame: reason"
	bool m_obitWfRunning {false};
	QPushButton* m_obitCancelBtn {nullptr};
	QPushButton* m_obitWfBtn {nullptr};
	QString m_obitWfTestAnswer;                 // self-test: "process" / "later" for the auto question
	QStringList m_obitWfTestLog;
	QString m_obitWfLastReport;
	void runObitWfSelfTest(const QString& report);
	// ---- duplicate news / photos (suneer_news_dup.cpp) ----
	static void dupCollectDocument(ScribusDoc* doc, QList<SuneerDupItem>& items, QList<QList<PageItem*>>& frames);
	static SuneerDupItem dupListItem(const QJsonObject& n, int index, std::function<QString(const QString&)> photoOf);
	//! After Fetch News: the list against itself and the document, in the background -> DUP badges.
	void startDuplicateScan();
	void applyDuplicateBadges(const QList<SuneerDupItem>& items, int listCount, const QList<SuneerDupPair>& pairs, qint64 ms);
	//! Before placing: [Place anyway] [Skip] [Show] for a story / photo already on the page; skipped ones leave \a stories.
	bool confirmDuplicatesBeforePlacing(QList<QJsonObject>& stories);
	int m_dupGen {0};
	bool m_dupScanDone {false};
	QString m_dupLog;
	QStringList m_dupPagePairs;                 // the last "Check duplicates on page" (self-test)
	QPointer<QDialog> m_dupDialog;
	QString m_dupTestAnswer;                    // self-test: "place" / "skip" / "show"
	QStringList m_dupTestLog;
	void runDupSelfTest(const QString& report);
	QString pageNameAt(int index) const;
	QString currentPageKey() const;             // "pageId|date" of the Page box and date
	static QString formatLockSince(const QDateTime& since);
	void lockCall(const QByteArray& verb, const QUrl& url, const QByteArray& body,
	              std::function<void(QNetworkReply*)> done, bool retried = false);
	//! Async GET /news/layout/pages for the edition + date (5 s); updates m_pageLocks and the Page box.
	void refreshPageLocks(std::function<void(bool ok, const QString& error)> done = {});
	void applyPageLockLabels();
	void showPageLockLine();
	//! The user's locked pages for \a date over all their editions, \a add / \a remove applied, as one PATCH.
	void syncMyPageLocks(const QString& date, const QString& add, const QString& remove,
	                     std::function<void(bool ok, const QSet<QString>& mine, const QString& error)> done);
	//! The PATCH carries the whole set: two read-modify-writes at once would undo each other
	//! (a release and a take racing put the released page back). They run one after another.
	void syncMyPageLocksNow(const QString& date, const QString& add, const QString& remove,
	                        std::function<void(bool ok, const QSet<QString>& mine, const QString& error)> done);
	QList<std::function<void()>> m_lockQueue;
	bool m_lockQueueBusy {false};
	void onPageSelected(int index);
	void onLockDateChanged();
	//! Check the Page box's page + date; ask when another user holds it; take the lock when free.
	//! \a then runs afterwards unless the user chose another page.
	void checkAndTakePageLock(std::function<void()> then = {});
	bool askLockedPage(const QString& pageName, const PageLockInfo& info);   // true = Continue anyway
	void takePageLock(const QString& pageId, const QString& pageName, const QString& date);
	//! Releases the lock this Scribus took; \a blocking waits up to 3 s (exit, logout).
	void releasePageLock(bool blocking = false, const QString& why = QString());
	void keepPageLockAlive();
	//! SUNEER_PAGELOCK_SELFTEST=<report>: scripted steps against a (mock) server (tests/suneer/news_page_lock.sh).
	void runPageLockSelfTest(const QString& report);
	QStringList m_pageLockTestLog;
	QString m_pageLockTestAnswer;               // self-test: "continue" / "choose" for the question
	int     m_lockReleasesInFlight {0};
	QListWidget* m_newsList      {nullptr};
	QPushButton* m_fetchBtn      {nullptr};
	// ---- "Open page from workflow": the page's .sla from the workflow ----------------
	// GET /news-layout-settings/page-context (template of the page + date), then
	// GET /sla-templates/:id/download?date&pageId (.sla, or a zip with images/), saved to
	// <pages folder>/<edition>/<date>/<page>.sla, opened, the page lock taken and the ids
	// kept in the document (ScribusDoc::suneerWorkflow).
	QPushButton* m_openWfBtn     {nullptr};
	QString      m_pagesFolder;              // QSettings pagesFolder; empty = defaultPagesFolder()
	bool         m_wfBusy {false};
	QSet<QString> m_wfPageAccepted;          // "doc|pageId|date": a different list page was accepted for the document
	static QString defaultPagesFolder();
	QString pagesFolder() const { return m_pagesFolder.isEmpty() ? defaultPagesFolder() : m_pagesFolder; }
	static QString safeFileName(const QString& name);
	void onOpenPageFromWorkflow();
	//! One async GET with the login token: progress, inactivity timeout, one token refresh on 401/403.
	void wfGet(const QUrl& url, int timeoutMs, std::function<void(QNetworkReply*)> done, bool retried = false);
	QString wfErrorText(QNetworkReply* reply, bool timedOut, int timeoutMs) const;
	//! Writes the download (.sla or zip) as \a slaPath (+ <base>_images/), the workflow ids in DOCUMENT.
	bool writeWorkflowPage(const QByteArray& body, const QString& slaPath, const QMap<QString, QString>& ids, QString* error, int* images);
	//! Opens \a path, keeps \a ids in the document, binds the held page lock to it.
	bool openWorkflowPage(const QString& path, const QMap<QString, QString>& ids);
	//! Placing / marking for the list page (m_fetchPageId, m_fetchDate) into a document opened from another
	//! workflow page asks first. True = go on.
	bool confirmWorkflowPage(const QString& action);
	void runWorkflowOpenSelfTest(const QString& report);
	QStringList m_wfTestLog;
	QString m_wfTestAnswer;                  // self-test: the existing-file answer (open / replace / keep / cancel)
	QPushButton* m_placeBtn      {nullptr};
	QPushButton* m_refreshBtn    {nullptr};
	QPushButton* m_settingsBtn   {nullptr};
	QLabel*      m_hostLabel     {nullptr};   // "Server: host" under the title
	void updateHostLabel();
	//! One blocking GET with a short timeout; returns "HTTP 200 OK" or the error.
	QString probeUrl(const QString& url);
	//! http(s) URL with a host; *why says what is wrong.
	static bool validServerUrl(const QString& url, QString* why);
	QLabel*      m_statusLabel   {nullptr};   // ONE short line (full text in the tooltip and in Details)
	class QProgressBar* m_statusBar {nullptr};   // shown while fetching / downloading / processing / placing
	class QToolButton*  m_statusDetailsBtn {nullptr};
	QString      m_statusFull;                 // the current full message
	QStringList  m_statusLog;                  // every message since start, with time, for the Details dialog
	//! Short status line; the full text goes to the tooltip and the Details log. failed: red.
	void setStatus(const QString& text, bool failed = false);
	QString statusText() const { return m_statusFull; }
	//! Progress bar with a short step text ("Photos 12/26"); total 0 = busy.
	void statusProgress(const QString& step, int done, int total);
	void statusProgressDone();
	void onStatusDetails();
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
	// The keyring is not usable (no default keyring, locked, no service, no
	// answer): the tokens stay in memory for this Scribus session only.
	bool    m_secretsLoading {false};  // the keyring check / read is running (a login may happen meanwhile)
	int     m_sessionGen {0};          // +1 on every login / logout: startup work never undoes a newer one
	bool    m_startupDone {false};
	bool    m_sessionOnly {false};
	bool    m_sessionOnlyNoticeShown {false};
	QString m_keyringWhy;          // why the keyring is not used
	QString m_sessionOnlyNotice;   // the text shown once (the self-test reads it)

	void ensureSecretsLoaded();
	void saveSession();
	void clearSession();
	void showSessionOnlyNotice();
	void captureRefreshCookie(QNetworkReply* reply);
	bool isLoggedIn() const { return !m_accessToken.isEmpty(); }
	void setLocked(bool locked);
	bool refreshAccessToken();   // blocking
	bool loginDialog();          // modal; the request inside is asynchronous; true when logged in
	QNetworkReply* startLoginRequest(const QString& email, const QString& password);
	bool finishLogin(QNetworkReply* reply, const QString& email, bool timedOut, qint64 ms, QString& error);
	QString loginErrorText(QNetworkReply* reply, bool timedOut) const;
	void startupSession();
	//! One line to ~/scribus-crashlogs/login.log with a timestamp; never a password or token.
	static void loginLog(const QString& line);
	static QString maskEmail(const QString& email);

	//! SUNEER_LOGIN_SELFTEST=<report>: drives the login dialog (tests/suneer/news_login.sh).
	void runLoginSelfTest(const QString& report);
	void writeLoginSelfTestReport(const QString& report, int keyringState, qint64 dialogMs);
	bool    m_loginSelfTest {false};
	QString m_loginSelfTestError;                     // the dialog's last error text
	int     m_loginSelfTestTicks {0};                 // event-loop ticks while the request ran
	bool    m_loginSelfTestLoginEnabledBusy {false};  // Login button enabled at a tick while busy
	bool    m_loginSelfTestCancelDisabledBusy {false};// Cancel disabled at a tick while busy
	qint64  m_loginSelfTestRequestMs {-1};            // click -> reply handled
	qint64  m_loginSelfTestCancelCloseMs {-1};        // Cancel click -> dialog closed

	// ---- allowed editions (from user.editionGroups; memory only) ---------
	QList<QPair<QString, QString>> m_allowedEditions;   // (edition id, name)
	QHash<QString, QString> m_editionGroupIds;           // edition id / short name / name -> user edition group id
	QString m_fetchSources;                              // "N ext + M layout" of the last fetch (status/report)
	QString m_fetchNote;                                 // "Used: HTTP 401 - log in" etc., shown after the counts
	QString editionGroupIdFor(const QString& edition) const;
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
public:
	static QString elideGraphemes(const QString& text, const QFontMetrics& fm, int width);
private:
	void showPreview(const QJsonObject& news);
	//! The headline style of the News Browser settings ("style/titleStyle"): the fallback when no width rule applies
	QString panelTitleStyle() const;

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
	//! welded stories (see suneerweld.h): weld / unweld the story of \a item, convert old story groups
	int  weldStoryItems(const QList<PageItem*>& items);
	void weldStoryOf(PageItem* item);
	void unweldStoryOf(PageItem* item);
	int  convertStoryGroupsToWeld(bool wholePage);
	static QString groupStoryId(const PageItem* group);
	//! Right-click "Adjust gaps...": the gaps of the placed story \a item belongs to (or of every
	//! story on the page), live on the page, one undo step. See suneer_news_gaps.cpp.
	void adjustGaps(PageItem* item);
	//! The gaps a placed story uses now: its own (layout.gaps, after "Adjust gaps"), else its model's.
	SuneerSpacing::Values storyGaps(const PageItem* storyText, QString* modelId = nullptr) const;
	struct GapResult
	{
		int stories {0};          //!< stories re-laid
		int moved {0};            //!< modules moved by the story-to-story gaps
		QStringList overflow;     //!< stories whose text no longer fits (overflow marker)
		QStringList parked;       //!< stories with no room left on the page: moved to the pasteboard
	};
	/*!
	 Re-lays the placed stories \a storyTexts (their text frames) in place with the gaps \a v:
	 headline / kicker space, column gutter, photo / caption gaps. With \a pageGaps the story-to-story
	 gaps too: every story on the current page to the grid with the horizontal gap, then stacked with
	 the vertical one. A story never runs into another frame: its text frame stops there (overflow
	 marker). \a remember writes layout.model / layout.gaps / layout.gapsUser on the stories.
	 No undo transaction of its own (the caller makes one).
	 */
	GapResult applyGaps(const QList<PageItem*>& storyTexts, const SuneerSpacing::Values& v, bool pageGaps, bool remember);
	//! The text frames of the placed News Browser stories on the current page (not locked / pinned / parked).
	QList<PageItem*> storyTextsOnPage() const;
	//! SUNEER_GAPS_SELFTEST=<report>: Adjust gaps on every story of the page, overlap / undo checks, then quits.
	void runGapSelfTest(const QString& outPath);
	static QStringList photoPositionKeys();
	static QStringList placePhotoPositionKeys();
	static QString photoPositionLabel(const QString& key);
private:
	//! Frame width and column gap for \a cols columns from the column config.
	void storyGeometry(int cols, double& totalW, double& colGapPt) const;
	int storyColumns(const QJsonObject& news) const;
	//! Tags the items of one story with a common story id (no grouping).
	QString tagStoryItems(const QList<PageItem*>& items, const QString& title, const QString& pos, int imgCols, int cols, const QString& serverId);
	// ---- "placed" state: the SERVER's news_pages.dtp_status is the truth ----
	QSet<QString> m_pendingMarks;              // server ids placed locally but not yet marked USED
public:
	/*! Deleting placed story frames from the page marks the story unused on the server by itself
	    (pref news/markUnusedOnDelete, default on): the document calls noteDeleting() with the frames
	    about to go (ScribusDoc::itemSelection_DeleteItem - the Delete key, Cut, Delete page), a check
	    ~2 s later marks every story that has no frame left anywhere in the document (pages and
	    pasteboard) in ONE request per page. Frames cut to the clipboard are held until they are pasted
	    or the clipboard changes. Undo / redo re-evaluates: frames back -> USED again, gone again ->
	    UNUSED. A failed request is retried every minute (up to 10 times) with a warning shown. */
	void noteDeleting(const QList<PageItem*>& items);
	bool markOnServerBatch(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error, const QString& expected);
	//! Redraw the used / unused badges of the list after a status change.
	void refreshUsedBadges();
	void noteCut(const QList<PageItem*>& items);
	void notePasted();
	//! Save: failed automatic marks are sent again (the retry queue is also tried on the next delete).
	void noteSaved();
	//! A document was opened (any route): a workflow page offers to sync its used / unused.
	void noteDocOpened(ScribusDoc* doc);
	//! A document is about to close: pending deletes checked, marks pushed (at most 3 s), the rest kept on disk.
	void noteDocClosing(ScribusDoc* doc);
	//! "Process obituary photos": the page's obituary photos not processed yet (or the selected ones) through the
	//! obituary pipeline (background, face crop, Obit BG colour, tone) in the background; one undo step.
	void processObitPhotos(bool selectionOnly);
	//! "Check duplicates on page": every duplicate pair of the document (stories, photos, a story placed twice), click to jump.
	void checkDuplicatesOnPage();
	//! An obituary photo frame "Process obituary photos" takes (\a why: why not - edited by hand, already processed, ...).
	bool obitPhotoNeedsProcessing(PageItem* item, QString* why = nullptr) const;
	static bool markUnusedOnDeletePref();
private slots:
	void checkDeletedStories();
	void retryAutoMarks();
private:
	struct DeletedStory { QString storyId, serverId, pageId, title; bool marked { false }; };
	QHash<QString, DeletedStory> m_deleted;     // story id -> what was deleted (pending the check)
	QSet<QString> m_cutStories;                 // story ids whose frames sit on the clipboard
	QTimer* m_deleteCheckTimer {nullptr};       // the one timer: a single-shot debounce of ~2 s after the last delete
	QHash<QString, DeletedStory> m_autoUnused;  // story id -> marked UNUSED by a delete (re-marked USED when its frames come back)
	QHash<QString, DeletedStory> m_autoRetry;   // retryKey -> failed / deferred server mark (title = wanted status), kept on disk
	static QString retryKey(const DeletedStory& d) { return d.serverId + QLatin1Char('|') + d.pageId; }
	int m_marksInFlight {0};                    // automatic PATCHes not answered yet (close waits for them)
	QHash<QString, QString> m_inFlightMarks;    // retryKey -> status of a PATCH on its way (not sent twice)
	void loadPendingMarks();
	void savePendingMarks() const;

	// ---- page sync: the stories in the document <-> used / unused of its workflow page ----
	struct SyncPage { QString pageId, date, pageName, edition; bool workflow {false}; };
	//! The workflow page of \a doc: its SuneerWf* ids, else the page most of its story frames were placed from.
	SyncPage docSyncPage(ScribusDoc* doc) const;
	enum class MarkGate { Allowed, Deferred, Blocked };
	//! Who may change used / unused of \a pageId: the holder of its page lock only (another user's -> Blocked;
	//! a page this Scribus has not locked -> Deferred, kept in the retry queue).
	MarkGate markGate(const QString& pageId, QString* why = nullptr) const;
	//! After Fetch News: link a downloaded page's frames, mark what is on the page USED, ask about the rest.
	void reconcilePage(const QString& trigger);
	//! Save / close: what is on the page and not USED yet is sent (no questions).
	void pushOnPage(const QString& why);
	//! Layout-settings rows of the page (GET /news-layout-settings) -> its untagged frames, by position.
	int linkWorkflowFrames(const QJsonArray& rows, const QString& pageId);
	void linkStoryFrames(PageItem* text, const QString& title, const QString& serverId, const QString& pageId);
	QList<PageItem*> weldedStoryParts(PageItem* text) const;
	static QString headlineKey(const QString& s);
	static QString frameHeadline(PageItem* text);
	static bool headlinesMatch(const QString& a, const QString& b);
	QString pageStatusOf(const QJsonObject& news, const QString& pageId) const;
	struct SyncFinding { QString serverId, title; PageItem* frame {nullptr}; };
	void showSyncDialog(const QString& pageId, const QString& pageName, const QList<SyncFinding>& stale,
	                    const QList<SyncFinding>& candidates, const QStringList& noId, const QStringList& notAssigned);
	//! api.log and ~/scribus-crashlogs/news-sync.log (never a token)
	void syncLog(const QString& line) const;
	QSet<QString> m_syncLinkTried;              // "doc|pageId|date": layout-settings link done for this document
	QSet<QString> m_syncKept;                   // "pageId|serverId": Keep chosen (QSettings sync/kept, survives a restart)
	void saveSyncKept();
	//! Settings > Forget Keep: every Keep answer forgotten (memory and settings); returns how many.
	int forgetSyncKept();
	bool m_syncRunning {false};
	bool m_wfOpening {false};                   // openWorkflowPage() is in loadDoc()
	QPointer<QDialog> m_syncDialog;
	QStringList m_syncTestLog;                  // self-test: what the sync decided / asked
	QString m_syncTestAnswer;                   // self-test: "unused" / "keep" (+ ",link"), "sync" for the open question
	//! The Page box (edition, date, page) set to \a sp; \a then once its lock check is done (8 s at most).
	void switchPanelToPage(const SyncPage& sp, std::function<void(bool)> then);
	void runSyncSelfTest(const QString& report);
	QHash<QString, QSet<PageItem*>> m_storyIndex;   // story id -> its live frames (see indexBuild)
	ScribusDoc* m_storyIndexDoc {nullptr};
	qint64 m_autoHookNs {0};                    // UI time spent in noteDeleting + checkDeletedStories (tests)
	void indexBuild();
	void indexAdd(const QList<PageItem*>& items);
	void indexRemove(const QList<PageItem*>& items);
	int indexCount(const QString& storyId);
	void indexResync(const QString& storyId);
	void autoMarkStories(const QList<DeletedStory>& stories, const QString& status, const QString& expected, const QString& why);
	QStringList m_autoMarkLog;                  // tests: "want status why ids" per batch decision
	//! tests/suneer/mark_unused_on_delete.sh: the delete scenarios on the opened page (fixture mode), report to \a outPath
	void runUnusedSelfTest(const QString& outPath);
	void reevaluateAfterUndo();
	QPushButton* m_checkBtn {nullptr};
	QPushButton* m_releaseBtn {nullptr};
	QTimer* m_placedTimer {nullptr};           // unused (kept for ABI of this class only)
	static bool newsIsUsed(const QJsonObject& news);
	//! "USED", "BALANCED" or "UNUSED" (the server's DtpStatusEnum) for the fetched page.
	static QString newsDtpStatus(const QJsonObject& news);
	static bool newsIsBalance(const QJsonObject& news) { return newsDtpStatus(news) == "BALANCED"; }
	//! What "Mark unused" sends. Needs the backend patch of 2026-10-08
	//! (dtp-status accepts UNUSED); an older backend answers 400 and the
	//! summary shows it. One place to change.
	static QString unusedStatus() { return QStringLiteral("UNUSED"); }
	//! Mark \a rows (news objects) as \a status on the server: one confirmation
	//! listing the headlines, optional removal from this document (one undo
	//! step), per-story result summary. Shared by the buttons and the menus.
	void markStories(const QList<QJsonObject>& rows, const QString& status);
	void onMarkBalanceTicked();
	QPushButton* m_balanceBtn {nullptr};
	bool isBalanceRow(const QListWidgetItem* item) const;
	bool apiPatch(const QUrl& url, const QJsonObject& body, std::function<void(QNetworkReply*)> done, bool retried = false);
	//! Change the status of each story on the server. Right before each
	//! change the story's CURRENT status is read back (GET /news/:id) and sent
	//! as fromStatus; when it differs from \a expected (what the user saw;
	//! empty = unknown) the story is left alone, its row refreshed and the
	//! error says who changed it and to what. Never retried blindly.
	bool markOnServer(const QStringList& newsIds, const QString& pageId, const QString& dtpStatus, QString* error, const QString& expected = QString());
	//! Current server status of one story on one page (fresh read), plus the
	//! user that last touched it when the server says so. Empty = not readable.
	QString serverStatus(const QString& newsId, const QString& pageId, QString* who = nullptr, QString* error = nullptr);
	//! Request/response trail for the status calls (no tokens): ~/.cache/scribus/news/api.log
	void apiLog(const QString& line) const;
	//! api.log: which API fields of \a news had blank lines / zero-width characters at the ends (once per story)
	void logTextJunk(const QJsonObject& news) const;
	//! Per-story outcome of the last markOnServer(): fresh from-status and error text.
	QHash<QString, QString> m_lastFrom, m_lastMarkError;
	//! Undo states of the placement in progress (SUNEER_NEWS_PLACE), so the
	//! previous status read during marking can be stored in them.
	QList<SimpleState*> m_placeStates;
	//! Inside the placement transaction: one undo record per story (frames +
	//! server status go back together).
	void recordPlaceUndo(const QList<QJsonObject>& stories);
	//! Mark the given pending (placed, not marked) stories USED again; empty = all.
	void retryPendingMarks(const QStringList& ids = QStringList());
	//! The status changes the server allows (one place; see result-20261008-2200).
	static bool allowedTransition(const QString& from, const QString& to);
	static QString transitionWhy(const QString& from, const QString& to);
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
	bool removeStoryFrames(PageItem* item, const QString& status, QString* error);
	QCheckBox*   m_selectUsedChk {nullptr};
	QComboBox*   m_filterCombo {nullptr};     // Show: all / USD / UNUSD / BAL (local, remembered)
	QLabel*      m_countLabel {nullptr};      // whole-page counts + what is shown
	QString listFilter() const;
	void applyListFilter(bool setStatusLine = false);
public:
	//! "Mark as unused" (server dtpStatus BALANCED) for the story \a item belongs to; asks first.
	void releaseStory(PageItem* item);
	//! "Mark as balance" (server dtpStatus BALANCED, frames stay); asks first.
	void balanceStory(PageItem* item);
	//! "Remove story (mark unused)": deletes every item of the story (one undo
	//! step) and marks it unused on the server; undo restores and re-marks used.
	void removeStory(PageItem* item);
	//! UndoObject: replays the server mark on undo/redo of removeStory.
	void restore(UndoState* state, bool isUndo) override;
	//! One-time hint when story frames are deleted with the plain Delete key.
	void deleteKeyHint();
	bool m_removing {false};
	int  m_placeUsedSkipped {0};
	bool m_markingBalance {false};   // markStories(): the caller asked for balance (wording), set by the menus
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
	QString downloadImage(const QString& fileUrl, QString* why = nullptr, bool force = false);
	QHash<QString, QString> m_obitPhotoUrl;      // story id -> Workflow file URL
	//! The story's pictures, whatever the reply shape: newsMedia[].media,
	//! newsMedia[] flat, mediaFiles[], media[], images[], the edition clone's
	//! newsMedia, obitHeadshotUrl / imageUrl. Every entry comes back as
	//! {media:{fileUrl,thumbUrl,caption,fileName,id}, caption}; entries
	//! without any URL are dropped, duplicates (same fileUrl) folded.
	static QJsonArray mediaList(const QJsonObject& news);
	//! Copies \a from's pictures into \a into when \a into has none. True when changed.
	static bool mergeMedia(QJsonObject& into, const QJsonObject& from);
	//! Story has no picture in the list reply: ask the server once
	//! (GET /news/:id, then /news/:id/media). Logged; never throws.
	void fetchMediaIfMissing(QJsonObject& news);
	//! A generated picture carrying the red "Image missing: <reason>" text,
	//! loaded into the photo frame when the download failed. Empty on failure.
	QString missingImagePlaceholder(const QString& reason) const;
	//! File extension from the first bytes (jpg, png, gif, tif, bmp, psd,
	//! webp) or empty when unknown. The server names WebP files ".jpg".
	static QString sniffImageFormat(const QByteArray& bytes);
	//! WebP bytes -> PNG bytes: Qt's webp plugin when present, else a
	//! helper process (dwebp, python3 + Pillow). Empty with \a why on failure.
	QByteArray convertWebpToPng(const QByteArray& webp, QString& how, QString& why) const;
	static QString newsCaption(const QJsonObject& news);
	static QString newsHighlights(const QJsonObject& news);
	QString styleOrEmpty(const QString& name, const QString& what, QStringList& missing) const;

	// ---- Layout model (Normal / Obituary) ---------------------------------
	//! "Normal" = the 4-col story + 3-col photo placement above; "Obituary" =
	//! the masonry obit modules of obituary.json (see suneer_news_obit.cpp).
	QComboBox* m_layoutModelCmb {nullptr};
	QHash<QString, QString> m_pageTypes;          // page id -> pageType from the page list
	SuneerObitPhotoWorker* m_obitWorker {nullptr};
	QList<QJsonObject> m_obitPending;             // stories waiting for the photo worker
	QHash<QString, QString> m_obitPhotoOut;       // story id -> processed photo path
	QHash<QString, QString> m_obitPhotoStatus;    // story id -> ok / noface / fail / cached
	int m_obitPhotoFailed {0};
	QHash<QString, QString> m_obitPhotoWhy;       // story id -> why the photo is missing / unprocessed
	QHash<QString, QString> m_obitNewsPhoto;      // story id -> downloaded (unprocessed) photo of a short news
	QHash<QString, QString> m_obitPhotoSrc;       // story id -> original (cached) picture handed to the worker
	QPushButton* m_obitReprocessBtn {nullptr};
	QPushButton* m_obitRepackBtn {nullptr};
	QPushButton* m_obitPinBtn {nullptr};
	QLabel* m_layoutModelInfo {nullptr};          // current Obituary settings next to the dropdown
	QHash<QString, QString> m_obitPhotoInfo;      // story id -> "180 ppi (240x240 px)"
	void updateLayoutModelInfo();
	//! The run-in place character style name, created in the document when missing.
	QString obitRuninStyle(const SuneerObitModel& model);
	//! Re-applies the current photo settings to the selected placed obit photos.
	void onObitReprocess();
	//! "Spacing...": the Layout Models editor with live preview / apply on the current page.
	void onSpacing();
	//! The stories of the current page not yet placed: the ticked rows, else every free row.
	QList<SuneerLayout::StoryIn> galleryNewStories(QString* note);
	//! The cached download of a picture URL (no download), empty when not cached.
	QString cachedImagePath(const QString& fileUrl) const;
	//! The spacing model of what is being placed ("obituary" while the Obituary model places, else "normal").
	QString spacingModel() const { return m_placingModel.isEmpty() ? activeModelId() : m_placingModel; }
	SuneerLayoutModel activeModel() const;
	//! The Obituary-engine model in use: the active model when it is one, else the built-in "obituary".
	QString activeObitModelId() const;
	SuneerObitModel obitModel(QString* note = nullptr) const;
	SuneerLayout::StoryIn storyInFromNews(const QJsonObject& news, const QString& key) const;
	QList<QJsonObject> galleryNewStoryJsons(QString* note);
	//! The real arrangement: Job to the end, then placed stories moved / re-laid, new stories placed, the rest to the pasteboard.
	QString arrangeCore(const SuneerLayoutModel& model, int keep, QList<QJsonObject> fresh, bool* changed, bool selectionOnly = false);
	void relayoutPlacedStory(const SuneerLayout::StoryIn& s, double x, double y, int cols, int imgCols, const QString& pos, const QString& headStyle);
	void parkOnPasteboard(const QList<QList<PageItem*>>& stories);
	void selectModelInCombo(const QString& id);
	QStringList m_restoreFailures;             // stories whose server status failed in this undo / redo step
	QString m_modelOverride;                   // the model being applied (wins over the dropdown)
	//! "Adjust gaps" / a story's own gaps: wins over the model's spacing while set (spacingValues()).
	std::optional<SuneerSpacing::Values> m_gapOverride;
	//! The spacing placement uses: m_gapOverride, else the model being placed / the active model.
	SuneerSpacing::Values spacingValues() const;
	//! Headline -> body and kicker -> headline space on the story frame's head paragraphs (style + extra, undoable).
	void applyHeadGaps(PageItem* text, const SuneerSpacing::Values& v);
	//! Writes layout.model / layout.gaps (/ layout.gapsUser) on every item of a story.
	void rememberGaps(const QList<PageItem*>& items, const QString& modelId, const SuneerSpacing::Values& v, bool user);
	// Gallery cards: arrangePage() runs on a scratch copy of the page (m_doc points at it meanwhile)
	bool m_preview {false};
	QList<QJsonObject> m_previewFresh;         // the new stories, read and their pictures fetched before the copy
	struct ArrangeOut
	{
		SuneerLayoutModel model;
		bool obitEngine {false};
		bool obitPacked {false};               // a news model on an obituary page: column packing with the obituary gaps
		bool refused {false};                  // checkModel / checkArrange said no: nothing was changed
		int total {0}, fit {0}, parked {0};
		int obits {0}, news {0};               // Obituary engine: obit modules / 2-column news arranged
		QStringList notFit;
		QRectF content;
		QList<double> colX;
		double colW {0}, gapV {0};
	};
	ArrangeOut* m_arrangeOut {nullptr};
	int m_forceCols {0};
	int m_forceImgCols {0};
	QString m_forcePos;
	QString m_forceTitle;
	QToolButton* m_relayoutBtn {nullptr};
	//! The model the current page was placed with (obituary modules on it -> "obituary").
	QString pageSpacingModel() const;
	struct SpacingSnap { QPointer<PageItem> item; double x, y; double wrap[4]; double dist[4]; double colGap; };
	QList<SpacingSnap> m_spacingSnap;
	void spacingSnapshot();
	void spacingRestore();
	//! Re-space the placed modules of the current page (final: one undo step + separator rules). Returns the modules touched.
	int applySpacingToPage(const SuneerSpacing::Values& v, bool final);
	//! Separator rules between stacked modules of the current page (deleted and drawn again; none when the width is 0).
	void respaceRules(const SuneerSpacing::Values& v);
	QString m_placingModel;
	//! "Re-pack page": obit modules up in their columns, then the gap / empty-area check. Returns the report lines.
	QStringList obitRepackPage(int* moved = nullptr, bool* changed = nullptr);
	void onObitRepack();
	void onObitPinSelected();
	//! Re-pack Page: what to work on (the page, the columns an automatic run is limited to, the frames it fits)
	struct RepackScope
	{
		int pageIndex { -1 };
		bool autoRun { false };                     //!< Re-pack Automatically: own columns only, no column change
		QList<QPair<double, double>> xRanges;       //!< automatic: the columns of the deleted / edited module (doc x)
		QList<QPointer<PageItem>> fitOnly;          //!< automatic: only these text frames are fitted
	};
	struct RepackResult
	{
		int modules { 0 }, moved { 0 }, fitted { 0 }, notFitted { 0 }, overlapsAfter { 0 };
		int gapsBefore { 0 }, gapsAfter { 0 };
		double gapsBeforeMm { 0 }, gapsAfterMm { 0 };
		QStringList parked, details;
		bool obituary { false };
	};
	RepackResult repackCore(const RepackScope& scope);
	void runAutoRepack();
	QTimer* m_repackTimer { nullptr };
	RepackScope m_repackPending;
	bool m_repacking { false };
	void runRepackSelfTest(const QString& outPath);
	//! Settings key of the current page: its pageType, else its name.
	QString layoutModelKey() const;
	void fillLayoutModelCombo();
	void preselectLayoutModel();
	//! Obituary vs short news (long body or headline-like title).
	static bool obitIsObituary(const QJsonObject& news, const SuneerObitModel& model);
	/*! suneer 2026-10-26: where a story goes on an obituary page. The model's news columns (9-10
	    "news right", 1-2 "news left") are reserved for the stories whose workflow column count
	    (News.noOfColumns, or the News Browser's "Place as" override) is SuneerObitModel::kBandCount; every other
	    story goes to the obit columns and never into the band: an obituary as a single module, any
	    other news at its count (1 column when the workflow sends none). Nothing spills from one area
	    into the other - what has no room is parked and listed. \a cols gets the width in grid columns. */
	enum ObitRole { ObitRoleObit = 0, ObitRoleBand = 1, ObitRoleAreaNews = 2 };
	static ObitRole obitRole(const QJsonObject& news, const SuneerObitModel& model, int* cols = nullptr);
	//! Entry point of the Obituary model: pictures first (background), then obitPlaceNow().
	void placeObituaries(const QList<QJsonObject>& stories);
	void obitPlaceNow();
	//! "<docname>_images/obit/" beside the document, or the news cache when unsaved.
	QString obitImageDir() const;
	//! Background tone for the obit photos: a document colour (added when
	//! needed) and its RGB;  fillName receives the colour name.
	QColor obitBackgroundColor(const SuneerObitModel& model, QString* fillName) const;
};

/*! The News Browser list: rows can be dragged onto the canvas (mime "application/x-suneer-news" = the row's
    m_newsData index); a drop on a page model placeholder builds the story in that slot (ScribusView). */
class SCRIBUS_API SuneerNewsListWidget : public QListWidget
{
public:
	static const char* mimeType() { return "application/x-suneer-news"; }
	explicit SuneerNewsListWidget(QWidget* parent = nullptr);
protected:
	QStringList mimeTypes() const override;
	QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
	Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
};
#endif
