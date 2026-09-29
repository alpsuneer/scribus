/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
                          scribus.h  -  description
                             -------------------
    begin                : Fre Apr  6 21:09:31 CEST 2001
    copyright            : (C) 2001 by Franz Schmid
    email                : Franz.Schmid@altmuehlnet.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SCRIBUS_H
#define SCRIBUS_H

#define VERS13x

// include from stl
#include <vector>

// include files for QT

#include <QActionGroup>
#include <QClipboard>
#include <QImage>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMap>
#include <QMultiHash>
#include <QPointer>
#include <QPushButton>
#include <QProcess>
#include <QString>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QMdiArea;
class QMdiSubWindow;
class QQuickView;

// application specific includes
#include "scribusapi.h"
#include "scribusdoc.h"
#include "manager/dock_manager.h"
#include "manager/widget_manager.h"
#include "styleoptions.h"
#include "ui/customfdialog.h"
#include "ui/scmessagebox.h"

class ActionManager;
class AlignDistributePalette;
class AppModeHelper;
class Autoforms;
class Biblio;
class BookPalette;
class CharSelect;
class CheckDocument;
class ColorCombo;
class ContentPalette;
class DockManager;
class DocumentLogManager;
class DocumentLogViewer;
class DownloadsPalette;
class ParagraphStylesPanel;
class SuneerControlBar;
class SuneerNewsPanel;
class EditToolBar;
class FileToolBar;
class FontCombo;
class FormatsManager;
class GuideManager;
class HelpBrowser;
class InlinePalette;
class LayerPalette;
class MarksManager;
class Measurements;
class ImageEraserOptions;
class RemovalToolWidget;
class ModeToolBar;
class NodePalette;
class NotesStylesEditor;
class OutlinePalette;
class PDFToolBar;
class PSLib;
class PageItem;
class PageItem_TextFrame;
class PagePalette;
class PageSelector;
class PrefsContext;
class PrefsManager;
class PropertiesPalette;
class ResourceManager;
class ScMWMenuManager;
class ScToolBar;
class ScrAction;
class ScrSpinBox;
class ScribusCore;
class ScribusDoc;
class ScribusMainWindow;
class ScribusQApp;
class ScribusWin;
class SimpleState;
class StoryEditor;
class StyleManager;
class SymbolPalette;
class TOCGenerator;
class UndoManager;
class UndoPalette;
class UndoState;
class ViewToolBar;

extern SCRIBUS_API ScribusQApp* ScQApp;

/**
  * \brief This Class is the base class for your application. It sets up the main
  * window and providing a menubar, toolbar
  * and statusbar. For the main view, an instance of class ScribusView is
  * created which creates your view.
  */
class SCRIBUS_API ScribusMainWindow : public QMainWindow, public UndoObject
{
	Q_OBJECT

public:
	/** \brief constructor */
	ScribusMainWindow();
	/** \brief destructor */
	~ScribusMainWindow();
	/*!
	* \retval 0 - ok, 1 - no fonts, ...
	*/
	//! What to do when the requested document is already open. Declared here
	//! rather than beside loadDoc(): that sits in a `public slots:` block, and
	//! moc rejects a type declaration inside one ("Parse error at enum").
	enum class AlreadyOpenAction
	{
		InformAndSwitch,   //!< stock: tell the user, then switch (File > Open)
		AskGoToOrCancel    //!< offer "Go to it" or "Cancel" (double-click handoff)
	};

	int initScMW(bool primaryMainWindow);
	/*! Queue paths handed over by another launch. NEVER opens them inline: a
	    modal dialog may be up, and a document must not be opened from inside a
	    blocked event loop. */
	void queueFilesFromOtherInstance(const QStringList& files);
private slots:
	void drainPendingFiles();
public:
	void setupMainWindow();
	int getScreenNumber() const;
	QScreen* getScreen() const;
	void getScreenPosition(int& xPos, int& yPos) const;
	void getScreenDPI(int& dpiX, int& dpiY) const;
	void addScToolBar(ScToolBar *tb, const QString& name, Qt::ToolBarArea area = Qt::ToolBarArea::TopToolBarArea);
	bool warningVersion(QWidget *parent);
	void startUpDialog();
	void setDefaultPrinter(const QString&, const QString&, const QString&);
	void getDefaultPrinter(QString& name, QString& file, QString& command) const;

	inline bool scriptIsRunning(void) const { return (m_ScriptRunning > 0); }
	inline void setScriptRunning(bool value) { m_ScriptRunning += (value ? 1 : -1); }

	ScribusDoc* doFileNew(double width, double height, double topMargin, double leftMargin, double rightMargin, double bottomMargin, double columnDistance, double columnCount, bool autoTextFrames, int pageArrangement, int unitIndex, int firstPageLocation, int orientation, int firstPageNumber, const QString& defaultPageSize, bool requiresGUI, int pageCount = 1, bool showView = true, int marginPreset = 0);
	ScribusDoc* newDoc(double width, double height, double topMargin, double leftMargin, double rightMargin, double bottomMargin, double columnDistance, double columnCount, bool autoTextFrames, int pageArrangement, int unitIndex, int firstPageLocation, int orientation, int firstPageNumber, const QString& defaultPageSize, bool requiresGUI, int pageCount = 1, bool showView = true, int marginPreset = 0);
	bool DoFileSave(const QString& fileName, QString* savedFileName = nullptr, uint formatID = FORMATID_CURRENTEXPORT);

	void changeEvent(QEvent *e) override;
	void closeEvent(QCloseEvent *ce) override;
	void keyPressEvent(QKeyEvent *k) override;
	void keyReleaseEvent(QKeyEvent *k) override;
	void inputMethodEvent (QInputMethodEvent *event) override;
	QVariant inputMethodQuery ( Qt::InputMethodQuery query ) const override;

	void requestUpdate(int);
	void setTBvals(PageItem *currItem);
	int ShowSubs();
	void applyNewMaster(const QString& name);
	void updateRecent(const QString& fn);
	void doPasteRecent(const QString& data);
	bool getPDFDriver(const QString & filename, const std::vector<int> & pageNumbers, const QMap<int, QImage> & thumbs, QString& error, bool* cancelled = nullptr);
	bool DoSaveAsEps(const QString& fn, QString& error);
	QPair<QString, uint> CFileDialog(const QString& workingDirectory = ".", const QString& dialogCaption = "", const QString& fileFilter = "", const QString& defNa = "",
						int optionFlags = fdExistingFiles, bool *useCompression = 0, bool *useFonts = 0, bool *useProfiles = 0);
	/*! \brief Recalculate the colors after changing CMS settings.
	Call the appropriate document function and then update the GUI elements.
	\param dia optional progress widget */
	void recalcColors();
	void SwitchWin();
	void RestoreBookMarks();
	QStringList  scrapbookNames() const;
	void updateLayerMenu();
	void emergencySave();
	QStringList findRecoverableFile();
	bool recoverFile(const QStringList& foundFiles);

	/**
	 * @brief Returns true if an arrow key is pressed down.
	 * @return true if an arrow key is pressed down otherwise returns false
	 */
	//bool arrowKeyDown();

	/**
	 * @brief Returns true if application is in object specific undo mode, other wise returns false.
	 * @return true if application is in object specific undo mode, other wise returns false
	 */
	bool isObjectSpecificUndo() const;
	void restore(UndoState* state, bool isUndo) override;
	void restoreGrouping(SimpleState *state, bool isUndo);
	void restoreUngrouping(SimpleState *state, bool isUndo);
	void restoreAddPage(SimpleState *state, bool isUndo);
	void restoreDeletePage(SimpleState *state, bool isUndo);
	void setPreviewToolbar();
	void updateFromDrop();


	//! \brief allow SE to get the SM for edit styles
	StyleManager *styleMgr() const {return m_styleManager;}



	struct CopyContentsBuffer contentsBuffer;
	bool internalCopy { false };
	QString internalCopyBuffer;
	int  HaveDoc { 0 };
	PrefsContext* dirs {nullptr};
	/** \brief view is the main widget which represents your working area. The View
	 * class should handle all events of the view widget.  It is kept empty so
	 * you can create your view according to your application's needs by
	 * changing the view class.
	 */
	ScribusView *view {nullptr};
	/** \brief doc represents your actual document and is created only once. It keeps
	 * information such as filename and does the serialization of your files.
	 */
	ScribusDoc *doc {nullptr};
	/** \brief private doc for managing default patterns. */
	ScribusDoc* m_doc {nullptr};

	DockManager * dockManager {nullptr};

	QProgressBar* mainWindowProgressBar {nullptr};
	ScrSpinBox* zoomSpinBox {nullptr}; //zoom spinbox at bottom of view
	PageSelector* pageSelector {nullptr}; //Page selector at bottom of view
	QPushButton *zoomDefaultToolbarButton {nullptr};
	QPushButton *zoomOutToolbarButton {nullptr};
	QPushButton *zoomInToolbarButton {nullptr};
	QComboBox *layerMenu {nullptr}; //Menu for layers at bottom of view
	QComboBox *unitSwitcher {nullptr}; //Menu for units at bottom of view
	EditToolBar *editToolBar {nullptr};
	FileToolBar *fileToolBar {nullptr};
	ModeToolBar* modeToolBar {nullptr};
	//! Brush controls for the image eraser; shown only while that mode is active.
	ImageEraserOptions* imageEraserOptions {nullptr};
	//! Brush and Apply controls for object removal; shown only while that mode
	//! is active.
	RemovalToolWidget* removalToolOptions {nullptr};
	PDFToolBar* pdfToolBar {nullptr};
	ViewToolBar* viewToolBar {nullptr};
	QLabel* mainWindowXPosLabel {nullptr};
	QLabel* mainWindowXPosDataLabel {nullptr};
	QLabel* mainWindowYPosLabel {nullptr};
	QLabel* mainWindowYPosDataLabel {nullptr};

	GuideManager *guidePalette {nullptr};
	CharSelect *charPalette {nullptr};
	PropertiesPalette *propertiesPalette {nullptr};
	ContentPalette *contentPalette {nullptr};
	MarksManager *marksManager {nullptr};
	NotesStylesEditor *nsEditor {nullptr};
	NodePalette *nodePalette {nullptr};
	OutlinePalette *outlinePalette {nullptr};
	Biblio *scrapbookPalette {nullptr};
	LayerPalette* layerPalette {nullptr};
	PagePalette *pagePalette {nullptr};
	BookPalette *bookmarkPalette {nullptr};
	DownloadsPalette *downloadsPalette {nullptr};
	ParagraphStylesPanel *paragraphStylesPanelTabs {nullptr};
	SuneerControlBar *m_suneerControlBar {nullptr};
	SuneerControlBar* suneerControlBar() { return m_suneerControlBar; }
	/*! \brief The shape a Frame Shape menu pick is waiting to become an interactive crop overlay
	    for. Set by startFrameShapeCrop() just before switching into modeShapeCrop;
	    CanvasMode_ShapeCrop::activate() consumes it via takePendingFrameShapeId(). */
	QString m_pendingFrameShapeId;
	//! Read and clear m_pendingFrameShapeId in one step, so a later activation - after a cancel,
	//! or entered some other way - can never see a stale id.
	QString takePendingFrameShapeId() { QString id = m_pendingFrameShapeId; m_pendingFrameShapeId.clear(); return id; }
	//! Push the shortcuts in the prefs onto the live actions and menus.
	void applyShortcutsFromPrefs();
	SuneerNewsPanel *m_suneerNewsPanel {nullptr};
	SymbolPalette *symbolPalette {nullptr};
	InlinePalette *inlinePalette {nullptr};
	Measurements* measurementPalette {nullptr};
	CheckDocument * docCheckerPalette {nullptr};
	UndoPalette* undoPalette {nullptr};
	AlignDistributePalette *alignDistributePalette {nullptr};
	ResourceManager *resourceManager {nullptr};
	DocumentLogViewer *documentLogViewer {nullptr};

	StoryEditor* storyEditor {nullptr};
	StoryEditor* CurrStED {nullptr};
	QMdiArea *mdiArea {nullptr};
	ScribusWin* ActWin {nullptr};
	QClipboard *ClipB {nullptr};
	AppModeHelper *appModeHelper {nullptr};

	QMap<QString, QPointer<ScrAction> > scrActions;
	QMap<QString, QPointer<ScrAction> > scrRecentFileActions;
	QMap<QString, QPointer<ScrAction> > scrWindowsActions;
	QMap<QString, QPointer<ScrAction> > scrScrapActions;
	QMap<QString, QPointer<ScrAction> > scrLayersActions;
	QMap<QString, QPointer<ScrAction> > scrRecentPasteActions;
	QMap<QString, QPointer<ScToolBar> > scrToolBars;
	QMultiHash<QString, QActionGroup*> scrActionGroups;
	ScMWMenuManager* scrMenuMgr {nullptr};
	ActionManager* actionManager {nullptr};
	QStringList m_recentDocsList;
	QStringList patternsDependingOnThis;

	/** \brief Temporary parameter storage for plugin import pipelines (cleared after use) */
	QMap<QString, QString> pluginEditParams;
	/** \brief Item being re-edited by a plugin dialog; cleared after use */
	PageItem* pluginEditItem {nullptr};
	/** \brief When true, plugin should regenerate silently without showing UI */
	bool pluginEditSilent {false};

	/** \brief Whether Frame Shape can reshape this item at all. */
	static bool frameShapeAppliesTo(const PageItem* item);
	/** \brief Whether the current selection can be reshaped, and why not if it cannot.
	    \param reason filled with a user-facing explanation when the answer is false. */
	bool frameShapeSelectionIsUsable(QString* reason = nullptr) const;

public slots:
	void iconSetChange();
	void languageChange();
	void localeChange();
	void statusBarLanguageChange();
	void specialActionKeyEvent(int unicodevalue);
	void newView();
	void ToggleStickyTools();
	void ToggleAllGuides();
	void ToggleAllPalettes();
	void slotStoryEditor(bool fromTable);
	void slotCharSelect();
	void ImageEffects();
	QString fileCollect(const bool compress = false, const bool withFonts = false, const bool withProfiles = false, const QString& newDirectory = QString());
	void AddBookMark(PageItem *ite);
	void DelBookMark(PageItem *ite);
	void BookMarkTxT(PageItem *ite);
	void StoreBookmarks();
	void setStatusBarMousePosition(double xp, double yp);
	void setStatusBarTextPosition(double base, double xp);
	void setStatusBarTextSelectedItemInfo();
	void setTempStatusBarText(const QString &text);
	void setStatusBarInfoText(const QString& newText);
	bool DoFileClose();
	void windowsMenuAboutToShow();
	//! \brief Handle the Extras menu for its items availability.
	void extrasMenuAboutToShow();
	void duplicateContentCheck();
	void newActWin(QMdiSubWindow *w);
	void closeActiveWindowMasterPageEditor();
	void updateActiveWindowCaption(const QString &newCaption);
	void windowsMenuActivated(int id);
	void PutScrap(int scID);
	void PutToInline(const QString& buffer);
	void PutToInline();
	void PutToPatterns();
	void ConvertToSymbol();
	void changeLayer(int);
	void setLayerMenuText(const QString &);
	void showLayer();
	void slotSetCurrentPage(int pageIndex);
	void setCurrentPage(int p);
	void ManageJava();
	void editSelectedSymbolStart();
	void suneerAutoFitHeight();
	void suneerAutoFitTextToggled(bool enabled);
	void suneerFitImageToFrame(PageItem* item);
	void suneerGetImage();
	void suneerScaleImageUp();
	void suneerScaleImageDown();
	void suneerEnlargeImageSize();
	void suneerReduceImageSize();
	void suneerEnlargeTextFrame();
	void suneerReduceTextFrame();
	void suneerTextToTable();
	void suneerEnlargeTextSize();
	void suneerReduceTextSize();
	void suneerEnlargeLineSpacing();
	void suneerReduceLineSpacing();
	void editSymbolStart(const QString& temp);
	void editSymbolEnd();
	void editInlineStart(int id);
	void editInlineEnd();
	void editMasterPagesStart(const QString& temp = "");
	void editMasterPagesEnd();
	/** \brief generate a new document in the current view */
	bool slotFileNew();
	void newFileFromTemplate();
	bool slotPageImport();
	bool loadPage(const QString& fileName, int Nr, bool Mpa, const QString& renamedPageName = QString());
	void gotoLayer(int l);
	void slotGetContent();
	void slotGetContent2(); // kk2006
	void slotGetClipboardImage();
	void toogleInlineState();
	/*!
	\author Franz Schmid
	\brief Appends a Textfile to the Text in the selected Textframe at the Cursorposition
	*/
	void slotFileAppend();

	void removeRecent(const QString& fn, bool fromFileWatcher = false);
	void removeRecentFromWatcher(const QString& filename);
	void loadRecent(const QString& filename);
	void rebuildRecentFileMenu();
	void rebuildRecentPasteMenu();
	void rebuildScrapbookMenu();
	void pasteRecent(const QString& fn);
	void pasteFromScrapbook(const QString& fn);
	void importVectorFile();
	void slotFilePlace();
	void rebuildLayersList();
	bool slotFileOpen();
	bool loadDoc(const QString& );
	bool loadDoc(const QString& fileName, AlreadyOpenAction onAlreadyOpen);
	/**
	 * @brief Do post loading functions
	 */
	bool postLoadDoc();
	/** \brief save a document */
	bool slotFileSave();
	/** \brief save a document under a different filename*/
	bool slotFileSaveAs();
	void slotFileRevert();
	/** \brief Sichert den Text eines Elements */
	void SaveText();
	/** \brief close the actual file */
	bool slotFileClose();
	/** \brief print the actual file */
	void slotFilePrint();
	void slotReallyPrint();
	//! One-click proof: 150dpi grayscale to the last/default printer, no dialog.
	void slotFileProofPrint();
	void slotEndSpecialEdit();
	/*!
	\author Franz Schmid
	\brief Generate and print PostScript from a doc
	\param options PrintOptions struct to control all settings
	\param error   Error Message in case of failure
	\sa ScribusMainWindow::slotFilePrint()
	\retval bool True for success */
	bool doPrint(PrintOptions &options, QString& error);
	/** \brief exits the application */
	void slotFileQuit();
	/** \brief put the marked text/object into the clipboard and remove
	 * it from the document */
	void slotEditCut();
	/** \brief put the marked text/object into the clipboard*/
	void slotEditCopy();
	/** \brief paste the clipboard into the document*/
	void slotEditPaste(bool forcePlainText = false);
	void slotEditPasteOriginalPosition();
	void slotEditPastePlainText();
	void doEditPaste(bool forcePlainText, bool pasteInOriginalPosition);
	void slotEditCopyContents();
	//! \brief Suneer: autoflow an overflowing text frame onto new pages (linked frames, same master/layout)
	void suneerAutoflowToNewPages();
	//! \brief Grow-only autofit over every overflowing text frame in scope.
	void suneerFixOverflowFrames();
	//! \brief Add or remove the News Browser tab in the Paragraph Styles docker.
	void suneerSetNewsBrowserTabVisible(bool visible);
	void suneerFixOverflowFramesDoc();
	//! \brief Suneer: open the two-page CTP imposition dialog
	void suneerOpenImposition();
	//! \brief Suneer: styled (formatting-preserving) copy of the current text-frame selection
	void slotEditStyledCopy();
	//! \brief Suneer: styled paste of the previously styled-copied text at the cursor
	void slotEditStyledPaste();
	void slotEditPasteContents(int absolute=0);
	void SelectAll(bool docWideSelect = false);
	void SelectAllOnLayer();
	void deselectAll();
	void ClipChange();
	void setCopyCutEnabled(bool b);
	/** \brief shows an about dialog*/
	void slotHelpAbout();
	void slotHelpAboutPlugins();
    void slotHelpAboutQt();
	void slotHelpActionSearch();
	void slotHelpCheckUpdates();
	void slotRaiseOnlineHelp();
	void slotOnlineHelp(const QString& jumpToSection = QString(), const QString& jumpToFile = QString());
	void slotOnlineHelpClosed();
	void slotResourceManager();
	void slotItemStyleSearch();
	void ToggleTips();
	void ToggleMouseTips();
	/** \brief Erzeugt eine neue Seite */
	void slotNewPageP(int wo, const QString& templ);
	void slotNewPageM();
	void slotNewMasterPage(int w, const QString &);
	void slotNewPage(int w, const QString& masterPageName = QString(), bool mov = true);
	void duplicateToMasterPage();
	/** \brief Loescht die aktuelle Seite */
	void deletePage();
	/**
	 * \brief Delete pages
	 * @param from First page to delete
	 * @param to Last page to delete
	 */
	void deletePage(int from, int to);
	void deletePage2(int pg);
	/** \brief Verschiebt Seiten */
	void movePage();
	void copyPage();
	void changePageProperties();
	/*!
	\author Craig Bradney
	\date Sun 30 Jan 2005
	\brief Zoom the view.
	Take the main window zoom actions and pass the view a %. Actions have whole number values like 20.0, 100.0, etc. Zoom to Fit uses -100 as a marker.
	\param zoomFactor Value stored in the ScrAction.
	 */
	void slotZoom(double zoomFactor); // 20, 50, 100, or -100 for Fit
	/** \brief Schaltet Raender ein/aus */
	void toggleMarks();
	void toggleBleeds();
	void toggleFrames();
	void toggleLayerMarkers();
	void toggleTextLinks();
	void toggleTextControls();
	void toggleColumnBorders();
	void toggleRulers();
	void toggleRulerMode();
	void togglePagePalette();
	void toggleCheckPal();
	/** \brief Schaltet M_ViewShowImages ein/aus */
	void toggleImageVisibility();
	/** \brief Schaltet Raster ein/aus */
	void toggleGrid();
	/** \brief Schaltet Rasterbenutzung ein/aus */
	void toggleSnapGrid();
	/** \brief Schaltet Rahmenbearbeitung ein/aus */
	void toggleNodeEdit();
	void slotSelect();
	/** \brief Switch appMode
	\param mode TODO learn modes*/
	void setAppModeByToggle(bool isOn, int newMode);
	/** \brief Re-read the eraser brush settings into the options bar, after the
	    canvas-side [ and ] keys have changed them. */
	void updateImageEraserOptions();
	/** \brief Trace the selected image's visible region into its contour line. */
	void slotDetectContourFromImage();
	/** \brief Apply one of the Frame Shape menu's shapes to the whole selection.
	    \param shapeId an id from FrameShapeMenu::catalogue(). */
	void applyFrameShape(const QString& shapeId);
	/** \brief Apply whatever shape the Frame Shape button is currently offering. */
	void slotApplyLastFrameShape();
	/** \brief Enter the interactive Photoshop-style crop overlay (CanvasMode_ShapeCrop) for
	    \a shapeId, instead of applying it to the frame outright. The overlay starts centred over
	    the current selection; the user drags it into place and commits with Enter, or cancels
	    with Escape or a click outside it.
	    \param shapeId an id from FrameShapeMenu::catalogue(). */
	void startFrameShapeCrop(const QString& shapeId);
	/** \brief Open the Properties Palette, which holds the shapes the menu leaves out. */
	void slotFrameShapeOptions();
	/*! \brief Run one AI Text Tools task on the selection.

	    \param task the index carried as the action's data; the mapping lives
	           in the slot, next to the menu order it mirrors. */
	void slotAITextTask(int task);
	/** \brief Show or hide the eraser options bar with the mode. */
	void setImageEraserOptionsVisible(bool visible);
	/** \brief Re-read the object removal tool's state into its options bar:
	    the brush settings after the canvas-side [ and ] keys, and whether
	    Apply is possible yet. */
	void updateRemovalToolOptions();
	/** \brief Show or hide the object removal options bar with the mode. */
	void setRemovalToolOptionsVisible(bool visible);
	/** \brief Neues Dokument erzeugt */
	void HaveNewDoc();
	void HaveNewSel();
	/** Dokument ist geaendert worden */
	void slotDocCh(bool reb = true);
	/** Page preview changed */
	void slotPreviewCh();
	/** Setzt die Abstufung */
	//void setItemShade(int id);
	/** Setz die Zeichensatzgroesse */
	void setItemFontSize(int fontSize);
	void setItemLanguage(const QString& language);
	/** Color Replacement */
	void slotReplaceColors();
	/** Style Manager */

	/** Erzeugt einen Rahmen */
	void MakeFrame(int f, int c, double *vals);
	/** Duplicate current item */
	void duplicateItem();
	/** Multiple duplicate current item*/
	void duplicateItemMulti();

	void objectAttributes();
	void getImageInfo();
	void generateTableOfContents();
	void updateDocument();

	void setNewAlignment(int i);
	void setNewDirection(int i);
	void setNewParStyle(const QString& name);
	void setNewCharStyle(const QString& name);
	void setAlignmentValue(int i);
	void setDirectionValue(int i);
	void editItemsFromOutlines(PageItem *ite);
	//0= center, 1 = top left.
	void selectItemsFromOutlines(PageItem *ite, bool single = false, int position = 0);
	void selectItemFromOutlines(PageItem *ite, bool single, int cPos);
	void selectPagesFromOutlines(int ScPage);
	void doPrintPreview();
	void printPreview();
	void doOutputPreviewPDF();
	void outputPreviewPDF();
	void doOutputPreviewPS();
	void outputPreviewPS();
	void viewPDFSeparations();
	void SaveAsEps();
	void reallySaveAsEps();
	void SaveAsPDF();
	void doSaveAsPDF();
	void setMainWindowActive();
	void setItemEffects(int h);
	void setStyleEffects(int s);
	void setItemTypeStyle(int id);
	void slotElemRead(const QString& Name, double x, double y, bool art, bool loca, ScribusDoc* docc, ScribusView* vie);
	void slotChangeUnit(int art, bool draw = true);
	/*!
	 * @brief Apply master pages from the Apply Master Page dialog
	 * @todo Make this work with real page numbers, negative numbers and document sections when they are implemented
	*/
	void ApplyMasterPage();
	void Apply_MasterPage(const QString& pageName, int pageNumber, bool reb = true);
	void GroupObj(bool showLockDia = true);
	void UnGroupObj();
	void AdjustGroupObj();
	void StatusPic();
	void ModifyAnnot();
	void toggleGuides();
	void toggleBase();
	void toggleSnapGuides();
	void toggleSnapElements();
	void setSnapElements(bool b);
	void EditTabs();
	void SearchText();
	/*! \brief call gimp and wait upon completion */
	void callImageEditor();
	/*! \brief open the built-in Scribus Image Editor for the selected image frame */
	void slotOpenScImageEditor();
	/*! \brief Apply a keyset .xml file to the live actions (name/shortcut pairs). */
	void applyKeySetFromFile(const QString& path);
	/*! \brief One-time opt-in prompt to activate the Malayalam DTP shortcut set. */
	void checkMalayalamDtpFirstRun();
	void applyDefaultShortcutSet();
	//! After Save As: release the old file's network lock and take the new file's.
	void moveDocumentLock(const QString& newFileName);
	void docCheckToggle(bool visible);
	//! \brief Scan a document for errors, return true on errors found
	bool scanDocument();
	void setUndoMode(bool isObjectSpecific);
	//! \brief Apply a Lorem Ipsum to the each item in a selection
	void insertSampleText();
	void updateItemLayerList();
	void updateColorLists();
	/*! \brief Change Preferences dialog.
	See prefsOrg for more info. It's very similar to docSetup/slotDocSetup. */
	void slotPrefsOrg();
	/** \brief Refromat the document when user click "OK" in ReformDoc dialog.
	See docSetup() for more info. */
	void slotDocSetup();
	//! \brief Insert a frame friendly dialog
	void slotInsertFrame();
	//! \brief Transform an item
	void slotItemTransform();
	//! \brief Auto-arrange frames into a newspaper-style layout
	void slotAutoArrangeFrames();
	//! \brief manages paints
	void manageColorsAndFills();
	//! \brief drawnew, call palettes to update for new page layout
	void updateGUIAfterPagesChanged();
	/**
	 * Enables/disables the actions in the "Table" menu.
	 *
	 * This has a functions of its own, since it needs to be called from the table edit
	 * canvas modes/gestures.
	 */
	void updateTableMenuActions();
	void emitUpdateRequest(int updateFlags) { emit UpdateRequest(updateFlags); }

	//inserting marks
	void slotInsertMark2Mark() { insertMark(MARK2MarkType); }
	void slotInsertMarkAnchor() { insertMark(MARKAnchorType); }
	void slotInsertMarkVariableText() { insertMark(MARKVariableTextType); }
	void slotInsertMarkItem() { insertMark(MARK2ItemType); }
	void slotInsertMarkNote();
	void slotInsertMarkIndex() { insertMark(MARKIndexType); }
	void slotEditMark();
	//connected to signal emitted by actions when "Update Marks" menu item is triggered
	void slotUpdateMarks();
	bool editMarkDlg(Mark *mrk, PageItem_TextFrame* currItem = nullptr);
//	void testQT_slot1(QString);
//	void testQT_slot2(double);
//	void testQT_slot3(int);
//	void testQT_slot4();
	void changePreviewQuality(int index);
	void enablePalettes(bool b);
	void ToggleFrameEdit();
	void NoFrameEdit();

signals:
	void TextStyle(const ParagraphStyle&);
//deprecated: (av)
	void TextEffects(int);
	void UpdateRequest(int updateFlags);
	void changeLayers(int);

protected:
	/*!
	\brief Receive key events from palettes such as palette hiding events. Possibly easier way but this is cleaner than before. No need to modify all those palettes and each new one in future.
	 */
	bool eventFilter( QObject *o, QEvent *e ) override;
	void dragEnterEvent( QDragEnterEvent* e) override;
	void dropEvent( QDropEvent* e) override;

private:
	//! \brief Shared worker for the two Fix Overflowing Frames entry points.
	void suneerFixOverflowFramesRun(bool wholeDocument);
	//! \brief Smallest height at which the frame stops overflowing, or -1 if it
	//! never does. Leaves the frame at its original height.
	double suneerMeasureFitHeight(PageItem_TextFrame* tf) const;
	//! \brief True if growing the frame to newHeight would cover something it
	//! does not already touch, or push a neighbouring frame into overflow.
	//! Leaves the frame at its original height.
	bool suneerGrowthWouldCollide(PageItem_TextFrame* tf, double newHeight) const;
	//! \brief Offer to fix overflowing frames in a file written by an older
	//! Scribus. Must run outside loadDoc() so undo is live; see the call site.
	void suneerMaybeOfferLegacyOverflowFix(int loadedFormatID);
	//! \brief Set by the prompt's "Don't ask again this session" checkbox.
	bool m_suneerLegacyOverflowAsked { false };
    /** init methods */
	void initSplash(bool showSplash);
	void initMdiArea();
	void initMenuBar(); // initMenuBar creates the menu_bar and inserts the menuitems
	void createMenuBar();
	void addDefaultWindowMenuItems(); // addDefaultWindowMenuItems adds the basic Windows menu items, excluding the actual list of windows
	void initStatusBar(); // setup the statusbar
	void initToolBars(); // setup the toolbars
	void setStyleSheet(); //set stylesheet for app
	void initDefaultValues();
	void initKeyboardShortcuts();
	//! \brief Keep Ctrl+Shift+C for styled copy and Ctrl+Shift+V for original-position
	//! paste, clearing conflicting assignments after every shortcut (re)load.
	void enforceClipboardShortcuts();
	void initPalettes();
	void initScrapbook();
	void updateColorMenu(QProgressBar* progressBar = nullptr);

	int m_ScriptRunning {0};

	QLabel* m_mainWindowStatusLabel {nullptr};
	QString m_statusLabelText;
	//QPixmap noIcon;

	bool m_palettesStatus[14] { false };
	bool m_guidesStatus[13] { false };

	//bool m_keyrep;
	/** @brief Tells if an arrow key is pressed down */
	//bool m_arrowKeyDown;
	/** @brief tells the undo mode */
	bool m_objectSpecificUndo { false };

	//CB: #8212: add overrideMasterPageSizing, however default to true for compatibility with other calls.. for now
	void addNewPages(int wo, int where, int numPages, double height, double width, int orient, const QString& siz, bool mov, QStringList* basedOn = 0, bool overrideMasterPageSizing = true);

	int m_DocNr { 1 };
	//! Paths handed over by other launches, waiting for a modal-free moment.
	QStringList m_pendingFilesFromOtherInstance;
	bool m_PrinterUsed { false };
	struct PDe {
					QString Pname;
					QString Dname;
					QString Command;
				} PDef ;
	TOCGenerator *m_tocGenerator {nullptr};

	StyleManager *m_styleManager {nullptr};
	UndoManager *m_undoManager {nullptr};
	DocumentLogManager& m_documentLogManager;
	PrefsManager& m_prefsManager;
	WidgetManager &m_widgetManager;
	FormatsManager *m_formatsManager {nullptr};	

	QPointer<HelpBrowser> m_helpBrowser;
	QString m_osgFilterString;

	void insertMark(MarkType);
	bool insertMarkDialog(PageItem_TextFrame* item, MarkType mT, ScItemsState* &is, const QString &text, int markInsertPosition);
	int m_marksCount { 0 }; //remember marks count from last call
	bool m_WasAutoSave { false };
	bool m_pagePaletteWasClosed { true };
};

#endif
