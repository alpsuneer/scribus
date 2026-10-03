/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
                          scribus.cpp  -  description
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


#include "DockWidget.h"
#include "ai/aitextservice.h"
#include "ai/aitextservicefactory.h"
#include "ui/aitextresultdialog.h"
#include "ui/aitranslatedialog.h"
#include "ui/ParagraphStylesPanel.h"
#include "ui/scshortcutregistry.h"
#include "ui/suneercontrolbar.h"
#include "ui/suneer_news_panel.h"
#include "ui/suneerduplicatenewsdialog.h"
#include <QAction>
#include <QtGui/QShortcut>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QCursor>
#include <QDomDocument>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

#include <QMouseEvent>
#include <QMultiMap>
#include <QPixmap>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QScopedPointer>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTableWidget>
#include <QTimer>
#include <QTranslator>
#include <QCheckBox>
#include <QWindow>
#include <QWheelEvent>


#ifdef DEBUG_LOAD_TIMES
#include <QDebug>
#include <QTime>
#include <sys/times.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "scconfig.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <array>
#include <iostream>
#include <sstream>
#include <csignal>
#include <string>

#include "actionmanager.h"
#include "actionsearch.h"
#include "api/api_application.h"
#include "appmodehelper.h"
#include "appmodes.h"
#include "canvasmode.h"
#include "canvasmode_imageimport.h"
#include "commonstrings.h"
#include "desaxe/digester.h"
#include "documentchecker.h"
#include "documentlogmanager.h"
#include "fileloader.h"
#include "filewatcher.h"
#include "fpoint.h"
#include "fpointarray.h"
#include "gtgettext.h"
#include "hyphenator.h"
#include "iconmanager.h"
#include "langmgr.h"
#include "localemgr.h"
#include "loadsaveplugin.h"
#include "marks.h"
#include "nfttemplate.h"
#include "notesstyles.h"
#include "pageitem_group.h"
#include "pageitem_imageframe.h"
#include "ui/scimageeditor.h"
#include "pageitem_latexframe.h"
#include "pageitem_placedpdf.h"
#include "pageitem_table.h"
#include "pageitem_textframe.h"
#include "pagesize.h"
#include "pdflib.h"
#include "pdfoptions.h"
#include "pdfpresets.h"
#include "pluginmanager.h"
#include "plugins/formatidlist.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "pslib.h"
#include "resourcecollection.h"
#include "scclipboardprocessor.h"
#include "scgtplugin.h"
#include "scimagecachemanager.h"
#include "scmimedata.h"
#include "scpage.h"
#include "scpaths.h"
#include "scprintengine_pdf.h"
#include "scprintengine_ps.h"
#include "scraction.h"
#include "scribusXml.h"
#include "scribusapp.h"
#include "singleinstance.h"
#include "documentlock.h"
#include "netpathguard.h"
#include "suneerimagelinks.h"
#include "ui/suneerlinkedimagesdialog.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "scribuswin.h"
#include "selection.h"
#include "serializer.h"
#include "storyloader.h"
#include "stylesearch.h"
#include "textframespellchecker.h"
#include "textnote.h"
#include "tocgenerator.h"
#include "ui/about.h"
#include "ui/aboutplugins.h"
#include "ui/actionsearchdialog.h"
#include "ui/aligndistribute.h"
#include "ui/annot.h"
#include "ui/annota.h"
#include "ui/applytemplatedialog.h"
#include "ui/basepointwidget.h"
#include "ui/bookmarkpalette.h"
#include "ui/charselect.h"
#include "ui/checkDocument.h"
#include "ui/collectforoutput_ui.h"
#include "ui/colorpicker/colorpicker.h"
#include "ui/contentpalette.h"
#include "ui/contextmenu.h"
#include "ui/copypagetomasterpagedialog.h"
#include "ui/customfdialog.h"
#include "ui/delpages.h"
#include "ui/documentlogviewer.h"
#include "ui/downloadspalette.h"
#include "ui/edittoolbar.h"
#include "ui/effectsdialog.h"
#include "ui/filetoolbar.h"
#include "ui/guidemanager.h"
#include "ui/helpbrowser.h"
#include "ui/hruler.h"
#include "ui/imageinfodialog.h"
#include "ui/inlinepalette.h"
#include "ui/insertaframe.h"
#include "ui/inspage.h"
#include "ui/javadocs.h"
#include "ui/layers.h"
#include "ui/loremipsum.h"
#include "ui/mark2item.h"
#include "ui/mark2mark.h"
#include "ui/markanchor.h"
#include "ui/markindex.h"
#include "ui/markinsert.h"
#include "ui/marknote.h"
#include "ui/marksmanager.h"
#include "ui/markvariabletext.h"
#include "ui/mergedoc.h"
#include "ui/contourdetectdialog.h"
#include "ui/frameshapemenu.h"
#include "ui/imageeraseroptions.h"
#include "ui/removaltoolwidget.h"
#include "ui/modetoolbar.h"
#include "ui/movepage.h"
#include "ui/multipleduplicate.h"
#include "ui/newdocdialog.h"
#include "ui/nftdialog.h"
#include "ui/nftwidget.h"
#include "ui/nodeeditpalette.h"
#include "ui/notesstyleseditor.h"
#include "ui/outlinepalette.h"
#include "ui/outputpreview_pdf.h"
#include "ui/outputpreview_ps.h"
#include "ui/pageitemattributes.h"
#include "ui/pagepalette.h"
#include "ui/pagepropertiesdialog.h"
#include "ui/pageselector.h"
#include "ui/colorsandfills.h"
#include "ui/pdfexportdialog.h"
#include "ui/preferences/prefs_keyboardshortcuts.h"
#include "ui/pdfseparationsviewer.h"
#include "ui/pdftoolbar.h"
#include "ui/picstatus.h"
#include "ui/preferences/preferencesdialog.h"
#include "ui/printdialog.h"
#include "ui/printpreview.h"
#include "ui/propertiespalette.h"
#include "ui/propertiespalette_xyz.h"
#include "ui/query.h"
#include "ui/recoverdialog.h"
#include "ui/replacecolors.h"
#include "ui/resourcemanager.h"
#include "ui/scmessagebox.h"
#include "ui/scmwmenumanager.h"
#include "ui/scrapbookpalette.h"
#include "ui/scrspinbox.h"
#include "ui/search.h"
#include "ui/selectobjects.h"
#include "ui/smcellstyle.h"
#include "ui/smlinestyle.h"
#include "ui/smtablestyle.h"
#include "ui/smtextstyles.h"
#include "ui/storyeditor.h"
#include "ui/stylemanager.h"
#include "ui/stylesearchdialog.h"
#include "ui/symbolpalette.h"
#include "ui/tabmanager.h"
#include "ui/transformdialog.h"
#include "ui/dialogs/autoarrangedialog.h"
#include "autoarrangeengine.h"
#include "ui/viewtoolbar.h"
#include "ui/factories/scribusproxystyle.h"
#include "undogui.h"
#include "undomanager.h"
#include "undostate.h"
#include "units.h"
#include "usertaskstructs.h"
#include "util.h"
#include "util_file.h"
#include "util_formats.h"
#include "util_printer.h"
#include "ui/proofprintdialog.h"
#include "proof_fit.h"
#include "ui/updatecheckdialog.h"
#include "ui/updatesettingsdialog.h"
#include "ui/impositiondialog.h"
#include "third_party/Qt-Advanced-Docking-System/src/DockAreaWidget.h"
#include "third_party/Qt-Advanced-Docking-System/src/IconProvider.h"

#ifdef HAVE_SVNVERSION
	#include "svnversion.h"
#endif

#ifdef HAVE_OSG
	#include "ui/osgeditor.h"
	#include <osgDB/ReaderWriter>
	#include <osgDB/PluginQuery>
#endif

#if defined(_WIN32)
#include "scdocoutput_ps2.h"
#include "scprintengine_gdi.h"
#endif

#include "sclimits.h"

using namespace std;
using namespace ads;

bool previewDinUse;
bool printDinUse;
extern bool emergencyActivated;

// Qt gives every QMdiSubWindow a system menu whose "Close" carries Ctrl+W.
// Our File > Close has the same key, and two claimants make Qt fire neither
// (ambiguous shortcut). The menu entry stays; only its key goes.
static void suneerStripMdiCloseKey(QMdiSubWindow* sub)
{
	if (!sub || !sub->systemMenu())
		return;
	const QList<QAction*> acts = sub->systemMenu()->actions();
	for (QAction* a : acts)
		if (a->shortcut() == QKeySequence(QKeySequence::Close))
			a->setShortcut(QKeySequence());
}


ScribusMainWindow::ScribusMainWindow() :
	m_documentLogManager(DocumentLogManager::instance()),
	m_prefsManager(PrefsManager::instance()),
	m_widgetManager(WidgetManager::instance())
{

#ifdef Q_OS_MACOS
	//commenting this out until this is resolved :https://bugreports.qt.io/browse/QTBUG-44565
	//ScQApp->setAttribute(Qt::AA_DontShowIconsInMenus);
	//noIcon = IconManager::instance().loadPixmap("no-icon");
#endif

}

/*
 * retval 0 - ok, 1 - no fonts, ...
 */
void ScribusMainWindow::queueFilesFromOtherInstance(const QStringList& files)
{
	if (files.isEmpty())
		return;
	m_pendingFilesFromOtherInstance += files;
	// Deliberately NOT opening here. This runs from a socket callback, and
	// loadDoc() puts up modal dialogs of its own; if the operator already has a
	// modal open (an unsaved-changes prompt, say) opening another from a blocked
	// event loop is at best confusing and at worst re-entrant. Defer instead.
	QTimer::singleShot(0, this, &ScribusMainWindow::drainPendingFiles);
}

void ScribusMainWindow::drainPendingFiles()
{
	if (m_pendingFilesFromOtherInstance.isEmpty())
		return;
	// A modal is up: leave the queue alone and try again shortly. The arriving
	// file waits politely rather than interrupting or auto-answering anything.
	if (QApplication::activeModalWidget() != nullptr)
	{
		QTimer::singleShot(250, this, &ScribusMainWindow::drainPendingFiles);
		return;
	}

	const QStringList files = m_pendingFilesFromOtherInstance;
	m_pendingFilesFromOtherInstance.clear();
	for (const QString& f : files)
	{
		if (QFileInfo::exists(f))
			loadDoc(f, AlreadyOpenAction::AskGoToOrCancel);
	}
	// Show the operator what they double-clicked.
	if (isMinimized())
		showNormal();
	raise();
	activateWindow();
}

int ScribusMainWindow::initScMW(bool primaryMainWindow)
{
	// Receive paths from later launches (null when opted out via
	// SCRIBUS_NO_SINGLE_INSTANCE, --no-single-instance, or a headless run).
	if (primaryMainWindow && ScQApp && ScQApp->singleInstance())
		connect(ScQApp->singleInstance(), &SingleInstance::filesReceived,
		        this, &ScribusMainWindow::queueFilesFromOtherInstance);

	// Documentation: https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/blob/master/doc/user-guide.md
	CDockManager::setConfigFlag(CDockManager::AlwaysShowTabs, false);
	CDockManager::setConfigFlag(CDockManager::FloatingContainerHasWidgetIcon, true);
	CDockManager::setConfigFlag(CDockManager::FloatingContainerHasWidgetTitle, true);
	CDockManager::setConfigFlag(CDockManager::HideSingleCentralWidgetTitleBar, false);
	CDockManager::setConfigFlag(CDockManager::OpaqueSplitterResize, true);
	CDockManager::setConfigFlag(CDockManager::TabCloseButtonIsToolButton, true);
	CDockManager::setConfigFlag(CDockManager::AllTabsHaveCloseButton, false);
//	CDockManager::setConfigFlag(CDockManager::DockAreaDynamicTabsMenuButtonVisibility, true);
	CDockManager::setConfigFlag(CDockManager::DockAreaHasCloseButton, false);
	CDockManager::setConfigFlag(CDockManager::DockAreaHasUndockButton, false);
	CDockManager::setConfigFlag(CDockManager::DockAreaHideDisabledButtons, true);
	CDockManager::setConfigFlag(CDockManager::FocusHighlighting, true);
	CDockManager::setConfigFlag(CDockManager::DisableTabTextEliding, true);
	CDockManager::setConfigFlag(CDockManager::ShowTabTextOnlyForActiveTab, !m_prefsManager.appPrefs.uiPrefs.showLabelsOfInactiveTabs);

	// Documentation: https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/blob/master/doc/user-guide.md#auto-hide-configuration-flags
	CDockManager::setAutoHideConfigFlags(CDockManager::DefaultAutoHideConfig);
//	CDockManager::setAutoHideConfigFlag(CDockManager::AutoHideShowOnMouseOver, false);
//	CDockManager::setAutoHideConfigFlag(CDockManager::AutoHideCloseButtonCollapsesDock, true);
//	CDockManager::setAutoHideConfigFlag(CDockManager::AutoHideButtonTogglesArea, true);
	CDockManager::setAutoHideConfigFlag(CDockManager::DockAreaHasAutoHideButton, true);

	IconManager &iconmanager = IconManager::instance();
	CDockManager::iconProvider().registerCustomIcon(TabCloseIcon, iconmanager.loadIcon("close", 12));
	CDockManager::iconProvider().registerCustomIcon(DockAreaCloseIcon, iconmanager.loadIcon("close", 12));
	CDockManager::iconProvider().registerCustomIcon(DockAreaMenuIcon, iconmanager.loadIcon("menu-down", 16));
	CDockManager::iconProvider().registerCustomIcon(DockAreaUndockIcon, iconmanager.loadIcon("dock-float", 16));
	CDockManager::iconProvider().registerCustomIcon(AutoHideIcon, iconmanager.loadIcon("dock-auto-hide", 16));
	CDockManager::iconProvider().registerCustomIcon(DockAreaMinimizeIcon, iconmanager.loadIcon("dock-minimize", 16));

	int retVal=0;
	//qsrand(1234);
	QByteArray stylesheet;
	if (loadRawText(ScPaths::preferencesDir() + "/stylesheet.css", stylesheet))
	{
		qApp->setStyleSheet(QString(stylesheet));
	}

	QApplication::setLayoutDirection(QLocale(ScCore->getGuiLanguage()).textDirection());
	previewDinUse = false;
	printDinUse = false;
	internalCopy = false;
	internalCopyBuffer.clear();
	m_doc = new ScribusDoc();
	m_doc->setup(0, 1, 1, 1, 1, "Custom", "Custom");
	m_doc->setPage(100, 100, 0, 0, 0, 0, 0, 0, false, false);
	m_doc->addPage(0);
	m_doc->setGUI(false, this, nullptr);
	CurrStED = nullptr;
	// Build stamp taken from the executable's own mtime, not __DATE__/__TIME__: those are
	// baked into this file's object file, so they only advance when scribus.cpp itself
	// recompiles and go stale whenever a build touches only other sources — exactly when
	// you most want to know whether the rebuild landed. The C locale keeps the month names
	// English and the string stable, matching what __DATE__ produced; it is a build
	// identifier, not user-facing text, so it stays outside tr().
	const QDateTime buildTime = QFileInfo(QCoreApplication::applicationFilePath()).lastModified();
	QString scribusTitle = QString("Faircode Scribus %1 [Build: %2]")
			.arg(ScribusAPI::getVersion(), QLocale::c().toString(buildTime, "MMM d yyyy HH:mm:ss"));
	if (ScribusAPI::isSVN() && ScribusAPI::haveSVNRevision())
		scribusTitle.append(QString(" (r%1)").arg(ScribusAPI::getSVNRevision()));
	setWindowTitle(scribusTitle);
	setAttribute(Qt::WA_KeyCompression, false);
	setAttribute(Qt::WA_InputMethodEnabled, true);
	setWindowIcon(IconManager::instance().loadIcon("app-icon"));
	setObjectName("MainWindow");
	scrActionGroups.clear();
	scrActions.clear();
	scrRecentFileActions.clear();
	scrRecentPasteActions.clear();
	scrWindowsActions.clear();
	scrLayersActions.clear();
	scrScrapActions.clear();
	actionManager = new ActionManager(this);
	m_widgetManager.setup(m_doc);
	appModeHelper = new AppModeHelper();
	appModeHelper->setup(actionManager, &scrActions, &scrRecentFileActions, &scrWindowsActions, &scrScrapActions, &scrLayersActions, &scrRecentPasteActions);
	scrMenuMgr = new ScMWMenuManager(menuBar(), actionManager);
	dockManager = new DockManager(this);
	m_formatsManager = FormatsManager::instance();
	m_objectSpecificUndo = false;

	m_undoManager = UndoManager::instance();
	PrefsContext *undoPrefs = m_prefsManager.prefsFile->getContext("undo");
	m_undoManager->setUndoEnabled(undoPrefs->getBool("enabled", true));
	m_tocGenerator = new TOCGenerator();
	m_marksCount = 0;

	initDefaultValues();
	initStatusBar();

	QApplication::processEvents();

	actionManager->init(this);
	initMdiArea();
	initMenuBar();
	createMenuBar();
	initToolBars();
	ScCore->pluginManager->setupPluginActions(this);
	ScCore->pluginManager->enableOnlyStartupPluginActions(this);
	ScCore->pluginManager->languageChange();
	if (primaryMainWindow)
		ScCore->setSplashStatus( tr("Applying User Shortcuts") );
	m_prefsManager.applyLoadedShortCuts();
	initKeyboardShortcuts();
	enforceClipboardShortcuts();
	resize(800, 600);
	connect(mdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(newActWin(QMdiSubWindow*)));
	//Connect windows cascade and tile actions to the workspace after its created. Only depends on mdiArea created.
	connect( scrActions["windowsCascade"], SIGNAL(triggered()) , mdiArea, SLOT(cascadeSubWindows()) );
	connect( scrActions["windowsTile"], SIGNAL(triggered()) , mdiArea, SLOT(tileSubWindows()) );

	if (primaryMainWindow)
		ScCore->setSplashStatus( tr("Initializing Workspaces") );
	initPalettes();
	dockManager->initWorkspaces();

	viewToolBar->previewQualitySwitcher->setCurrentIndex(m_prefsManager.appPrefs.itemToolPrefs.imageLowResType);
	if (primaryMainWindow)
		ScCore->setSplashStatus( tr("Initializing Story Editor") );
	storyEditor = new StoryEditor(this);

	if (primaryMainWindow)
		ScCore->setSplashStatus( tr("Initializing Languages") );
	LanguageManager::instance();

	if (primaryMainWindow)
		ScCore->setSplashStatus( tr("Reading Scrapbook") );
	initScrapbook();
	scrActions["helpTooltips"]->setChecked(m_prefsManager.appPrefs.displayPrefs.showToolTips);
	scrActions["showMouseCoordinates"]->setChecked(m_prefsManager.appPrefs.displayPrefs.showMouseCoordinates);
	scrActions["stickyTools"]->setChecked(m_prefsManager.appPrefs.uiPrefs.stickyTools);
	ToggleTips();
	ToggleMouseTips();
	propertiesPalette->setFontSize();
	contentPalette->setFontSize();
	if (scrActions["SaveAsDocumentTemplate"])
		scrActions["SaveAsDocumentTemplate"]->setEnabled(false);

	connect(ScQApp, SIGNAL(iconSetChanged()), this, SLOT(iconSetChange()));
	connect(ScQApp, SIGNAL(localeChanged()), this, SLOT(localeChange()));
	connect(ScCore->fileWatcher, SIGNAL(fileDeleted(QString)), this, SLOT(removeRecentFromWatcher(QString)));
	connect(ClipB, SIGNAL(dataChanged()), this, SLOT(ClipChange()));
	setAcceptDrops(true);
	QCoreApplication::instance()->installEventFilter(this);
	scrActions["toolsSelect"]->setChecked(true);

	ColorSetManager csm;
	csm.findPaletteLocations();
	csm.findPalettes();
	csm.findUserPalettes();
	QString Cpfad = QDir::toNativeSeparators(ScPaths::applicationDataDir())+"DefaultColors.xml";
	QFile fc(Cpfad);
	if (fc.exists())
		csm.loadPalette(Cpfad, m_doc, m_prefsManager.appPrefs.colorPrefs.DColors, m_prefsManager.appPrefs.defaultGradients, m_prefsManager.appPrefs.defaultPatterns, false);
	else
	{
		if (m_prefsManager.appPrefs.colorPrefs.DColorSet != "Scribus Small")
		{
			QStringList CustomColorSets = csm.userPaletteNames();
			if (CustomColorSets.contains(m_prefsManager.appPrefs.colorPrefs.DColorSet))
				Cpfad = csm.userPaletteFileFromName(m_prefsManager.appPrefs.colorPrefs.DColorSet);
			else
				Cpfad = csm.paletteFileFromName(m_prefsManager.appPrefs.colorPrefs.DColorSet);
			if (!Cpfad.isEmpty())
				csm.loadPalette(Cpfad, m_doc, m_prefsManager.appPrefs.colorPrefs.DColors, m_prefsManager.appPrefs.defaultGradients, m_prefsManager.appPrefs.defaultPatterns, false);
		}
	}
	appModeHelper->setStartupActionsEnabled(false);

	ScQApp->changeLabelVisibility(m_prefsManager.appPrefs.uiPrefs.showLabels);

	setStyleSheet();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, [this]()
	{
		emit ScQApp->iconSetChanged();
		// THIS IS A WORKAROUND!
		// It seems like once a widget has a custom style it doesn't redraw automatically with dark or light theme.
		// If we set the style sheet again it forces a redrawing with the current theme.
		setStyleSheet();
	});
#endif

	checkMalayalamDtpFirstRun();
	applyDefaultShortcutSet();
	// Locks this user left behind on template files (older builds locked a
	// template while a document made from it was open; a crash kept the lock).
	// Only our own locks whose process is gone; anyone else's stays.
	{
		const QStringList removed = DocumentLock::removeOwnDeadLocksIn(templateSearchPaths(), 1);
		for (const QString& l : removed)
			qWarning().noquote() << "[DocumentLock] removed leftover lock" << l;
	}
	// Once the event loop is up and every dock has made its shortcuts.
	QTimer::singleShot(0, this, [this] { suneerLogShortcutConflicts(QStringLiteral("startup")); });
	// The same duplicates, shown once in a dialog (with "don't show again
	// until something changes") instead of only in the log.
	QTimer::singleShot(1500, this, [this] { ScShortcutRegistry::instance().maybeShowDuplicatesDialog(this, tr("at startup")); });
	// Automatic update check: well after startup, asynchronous, at most once a
	// day, and silent unless a newer signed build exists.
	QTimer::singleShot(10000, this, [this] { UpdateCheckDialog::runStartupCheck(this); });

	return retVal;
}

void ScribusMainWindow::setupMainWindow()
{
	setDefaultPrinter(m_prefsManager.appPrefs.printerPrefs.PrinterName, m_prefsManager.appPrefs.printerPrefs.PrinterFile, m_prefsManager.appPrefs.printerPrefs.PrinterCommand);

	uint max = qMin(m_prefsManager.appPrefs.uiPrefs.recentDocCount, m_prefsManager.appPrefs.uiPrefs.RecentDocs.count());
	for (uint i = 0; i < max; ++i)
	{
		// A document on a share that is not answering stays in the list
		// without being looked at; loadRecent() checks again when it is chosen.
		const bool unreachable = !NetPathGuard::reachable(m_prefsManager.appPrefs.uiPrefs.RecentDocs.at(i));
		QFileInfo fd(m_prefsManager.appPrefs.uiPrefs.RecentDocs.at(i));
		if (unreachable || fd.exists())
		{
			m_recentDocsList.append(m_prefsManager.appPrefs.uiPrefs.RecentDocs.at(i));
			//#9845: ScCore->fileWatcher->addFile(appPrefs.uiPrefs.RecentDocs[m]);
		}
	}
	rebuildRecentFileMenu();
	suneerRebuildPdfPresetMenu();
	//For 1.3.5, we dump prefs first time around.
	if (!m_prefsManager.firstTimeIgnoreOldPrefs())
		m_prefsManager.readPrefsXML();
	if (m_prefsManager.appPrefs.verifierPrefs.checkerPrefsList.count() == 0)
	{
		m_prefsManager.initDefaultCheckerPrefs(m_prefsManager.appPrefs.verifierPrefs.checkerPrefsList);
		m_prefsManager.appPrefs.verifierPrefs.curCheckProfile = CommonStrings::PDF_1_4;
	}

	const WindowPrefs& mainWinSettings = m_prefsManager.appPrefs.uiPrefs.mainWinSettings;
	QWindow* w = windowHandle();
	QList<QScreen*> screens = QGuiApplication::screens();
	QScreen* s = nullptr;
	if (w != nullptr)
	{
		s = screens.at(qMin(mainWinSettings.screenNumber, QGuiApplication::screens().count() - 1));
		windowHandle()->setScreen(s);
	}
	else
		s = QGuiApplication::primaryScreen();
	QRect r(0, 0, 0, 0);
	if (s != nullptr)
		r = s->geometry();
	move(r.left() + abs(mainWinSettings.xPosition), r.top() + abs(mainWinSettings.yPosition));
	resize(mainWinSettings.width, mainWinSettings.height);

	if (mainWinSettings.maximized)
		this->setWindowState((this->windowState() & ~(Qt::WindowMinimized | Qt::WindowFullScreen)) | Qt::WindowMaximized);

	if (!m_prefsManager.appPrefs.uiPrefs.mainWinState.isEmpty())
		restoreState(m_prefsManager.appPrefs.uiPrefs.mainWinState);
}

int ScribusMainWindow::getScreenNumber() const
{
	QList<QScreen*> screens = QGuiApplication::screens();
	int screenNumber = -1;
	QScreen* s = ScCore->primaryMainWindow()->screen();
	screenNumber = screens.indexOf(s);
	if (screenNumber < 0)
		screenNumber = screens.indexOf(QGuiApplication::primaryScreen());
	if (screenNumber < 0)
		screenNumber = 0;
	return screenNumber;
}

QScreen* ScribusMainWindow::getScreen() const
{
	QList<QScreen*> screens = QGuiApplication::screens();
	return screens.at(getScreenNumber());
}

void ScribusMainWindow::getScreenPosition(int& xPos, int& yPos) const
{
	const QScreen* screen(getScreen());
	QRect screenGeom = screen->geometry();
	xPos = screenGeom.left();
	yPos = screenGeom.top();
}

void ScribusMainWindow::getScreenDPI(int& dpiX, int& dpiY) const
{
	const QScreen* screen(getScreen());
	dpiX = screen->physicalDotsPerInchX();
	dpiY = screen->physicalDotsPerInchY();
}

ScribusMainWindow::~ScribusMainWindow()
{
	delete actionManager;
	delete appModeHelper;
	delete m_doc;
	delete m_tocGenerator;
}

void ScribusMainWindow::addScToolBar(ScToolBar *tb, const QString & name, Qt::ToolBarArea area)
{
	if (!scrToolBars.contains(name))
		scrToolBars.insert(name, tb);
	addToolBar(area, tb);
}

void ScribusMainWindow::initToolBars()
{
	fileToolBar = new FileToolBar(this);
	editToolBar = new EditToolBar(this);
	UndoWidget* uWidget = new UndoWidget(editToolBar, "uWidget");
	m_undoManager->registerGui(uWidget);
	modeToolBar = new ModeToolBar(this);
	pdfToolBar = new PDFToolBar(this);
	viewToolBar = new ViewToolBar(this);

	// Deliberately not an ScToolBar and not registered with addScToolBar():
	// its visibility follows the canvas mode, not a saved preference.
	imageEraserOptions = new ImageEraserOptions(this);
	addToolBar(Qt::ToolBarArea::TopToolBarArea, imageEraserOptions);
	imageEraserOptions->hide();

	removalToolOptions = new RemovalToolWidget(this);
	addToolBar(Qt::ToolBarArea::TopToolBarArea, removalToolOptions);
	removalToolOptions->hide();

	addScToolBar(fileToolBar, fileToolBar->objectName());
	addScToolBar(editToolBar, editToolBar->objectName());
	addScToolBar(modeToolBar, modeToolBar->objectName(), Qt::ToolBarArea::LeftToolBarArea);
	addScToolBar(pdfToolBar, pdfToolBar->objectName());
	addScToolBar(viewToolBar, viewToolBar->objectName());
	connect(modeToolBar, SIGNAL(visibilityChanged(bool)), scrActions["toolsToolbarTools"], SLOT(setChecked(bool)));
	connect(scrActions["toolsToolbarPDF"], SIGNAL(toggled(bool)), pdfToolBar, SLOT(setVisible(bool)));
	connect(pdfToolBar, SIGNAL(visibilityChanged(bool)), scrActions["toolsToolbarPDF"], SLOT(setChecked(bool)));
	connect(scrActions["toolsToolbarTools"], SIGNAL(toggled(bool)), modeToolBar, SLOT(setVisible(bool)) );
	connect(viewToolBar, SIGNAL(visibilityChanged(bool)), scrActions["toolsToolbarView"], SLOT(setChecked(bool)));
	connect(scrActions["toolsToolbarView"], SIGNAL(toggled(bool)), viewToolBar, SLOT(setVisible(bool)) );
	connect(fileToolBar, SIGNAL(visibilityChanged(bool)), scrActions["toolsToolbarFile"], SLOT(setChecked(bool)));
	connect(scrActions["toolsToolbarFile"], SIGNAL(toggled(bool)), fileToolBar, SLOT(setVisible(bool)) );
	connect(editToolBar, SIGNAL(visibilityChanged(bool)), scrActions["toolsToolbarEdit"], SLOT(setChecked(bool)));
	connect(scrActions["toolsToolbarEdit"], SIGNAL(toggled(bool)), editToolBar, SLOT(setVisible(bool)) );
}

void ScribusMainWindow::setStyleSheet()
{
	QByteArray stylesheet;
	if (loadRawText(ScPaths::instance().libDir() + "scribus.css", stylesheet))
	{
		QString downArrow(IconManager::instance().pathForIcon("stylesheet/go-down.png"));
		QByteArray da;
		da.append(downArrow.toUtf8());
		stylesheet.replace("___downArrow___", da);
		QString toolbararrow(IconManager::instance().pathForIcon("stylesheet/down_arrow.png"));
		QByteArray tba;
		tba.append(toolbararrow.toUtf8());
		stylesheet.replace("___tb_menu_arrow___", tba);
	}

	dockManager->setStyleSheet(stylesheet); // style sheet should be enabled when theme manager is implemented to handle color palettes in css file.

	layerMenu->setStyleSheet(stylesheet);
	unitSwitcher->setStyleSheet(stylesheet);
	zoomDefaultToolbarButton->setStyleSheet(stylesheet);
	zoomInToolbarButton->setStyleSheet(stylesheet);
	zoomOutToolbarButton->setStyleSheet(stylesheet);
	zoomSpinBox->setStyleSheet(stylesheet);

	fileToolBar->setStyleSheet(stylesheet);
	editToolBar->setStyleSheet(stylesheet);
	modeToolBar->setStyleSheet(stylesheet);
	pdfToolBar->setStyleSheet(stylesheet);
	viewToolBar->setStyleSheet(stylesheet);
}


void ScribusMainWindow::initDefaultValues()
{
	HaveDoc = false;
	view = nullptr;
	doc = nullptr;
	m_DocNr = 1;
	m_PrinterUsed = false;
	PDef.Pname.clear();
	PDef.Dname.clear();
	PDef.Command.clear();
	//m_keyrep = false;
	//m_arrowKeyDown = false;
	ClipB = QApplication::clipboard();
	std::fill_n(m_palettesStatus, PAL_MAX, false);
	std::fill_n(m_guidesStatus, GS_MAX, false);
#ifdef HAVE_OSG
	QStringList supportedExts;
	supportedExts << "osg" << "dxf" << "flt" << "ive" << "geo" << "sta" << "stl" << "logo" << "3ds" << "ac" << "obj";
	QStringList realSupportedExts;
	QMap<QString, QString> formats;
	osgDB::FileNameList plugins = osgDB::listAllAvailablePlugins();
	for (osgDB::FileNameList::iterator itr = plugins.begin(); itr != plugins.end(); ++itr)
	{
		osgDB::ReaderWriterInfoList infoList;
		if (QString::fromStdString(*itr).contains("qfont"))
			continue;
		if (osgDB::queryPlugin(*itr, infoList))
		{
			for (auto rwi_itr = infoList.begin(); rwi_itr != infoList.end(); ++rwi_itr)
			{
				osgDB::ReaderWriterInfo& info = *(*rwi_itr);
				osgDB::ReaderWriter::FormatDescriptionMap::iterator fdm_itr;
				for (fdm_itr = info.extensions.begin(); fdm_itr != info.extensions.end(); ++fdm_itr)
				{
					if (supportedExts.contains(QString::fromStdString(fdm_itr->first)))
						formats.insert("*." + QString::fromStdString(fdm_itr->first) + " *." + QString::fromStdString(fdm_itr->first).toUpper(), QString::fromStdString(fdm_itr->second) + " (*." + QString::fromStdString(fdm_itr->first) + " *." + QString::fromStdString(fdm_itr->first).toUpper() + ")");
				}
			}
		}
	}
	realSupportedExts = formats.keys();
	QString docexts = realSupportedExts.join(" ");
	QStringList longList = formats.values();
	QString longDesc = longList.join(";;") + ";;";
	m_osgFilterString = tr("All Supported Formats (%1);;%2All Files (*)").arg(docexts).arg(longDesc);
#endif
}


void ScribusMainWindow::initKeyboardShortcuts()
{
	for (auto it = scrActions.begin(); it != scrActions.end(); ++it)
	{
		if ((ScrAction*)(it.value()) != nullptr)
		{
			QString accelerator = it.value()->shortcut().toString();
			m_prefsManager.setKeyEntry(it.key(), it.value()->cleanMenuText(), accelerator,0);
		}
		//else
		//	qDebug() << it.key();
		//qDebug() << QString("|-\n|%1||%2||%3").arg(it.key()).arg(it.value()->cleanMenuText()).arg(QString(it.data()->accel()));
	}
}


void ScribusMainWindow::initPalettes()
{
	//CB TODO hide the publicly available members of some palettes
	// these must be filtered too as they take control of the palettes events

	dockManager->setupDocks();

	// Outliner
	outlinePalette = dockManager->outlinePalette;
	outlinePalette->setMainWindow(this);
	outlinePalette->setToggleViewAction(scrActions["toolsOutline"]);

	// Frame Properties
	propertiesPalette = dockManager->propertiesPalette;
	propertiesPalette->setMainWindow(this);
	propertiesPalette->setToggleViewAction(scrActions["toolsProperties"]);
	emit UpdateRequest(reqDefFontListUpdate);
	propertiesPalette->installEventFilter(this);

	// Content Properties
	contentPalette = dockManager->contentPalette;
	contentPalette->setMainWindow(this);
	contentPalette->setToggleViewAction(scrActions["toolsContent"]);
	contentPalette->installEventFilter(this);

	// Nodes
	nodePalette = new NodePalette(this);
	nodePalette->installEventFilter(this);

	// Guides
	guidePalette = new GuideManager(this);

	// Character Selection
	charPalette = new CharSelect(this);

	// Layer
	layerPalette = dockManager->layerPalette;
	layerPalette->setToggleViewAction(scrActions["toolsLayers"]);
	layerPalette->installEventFilter(this);

	// Scrapbook
	scrapbookPalette = dockManager->scrapbookPalette;
	scrapbookPalette->setToggleViewAction(scrActions["toolsScrapbook"]);
	connect( scrapbookPalette, SIGNAL(pasteToActualPage(QString)), this, SLOT(pasteFromScrapbook(QString)));
	connect( scrapbookPalette, SIGNAL(scrapbookListChanged()), this, SLOT(rebuildScrapbookMenu()));
	scrapbookPalette->installEventFilter(this);

	// Pages
	pagePalette = dockManager->pagePalette;
	pagePalette->setMainWindow(this);
	pagePalette->setToggleViewAction(scrActions["toolsPages"]);
	pagePalette->installEventFilter(this);

	// Bookmark
	bookmarkPalette = dockManager->bookPalette;
	bookmarkPalette->setToggleViewAction(scrActions["toolsBookmarks"]);
	bookmarkPalette->installEventFilter(this);

	// Downloads
	downloadsPalette = new DownloadsPalette(this);
	connect( scrActions["toolsDownloads"], SIGNAL(toggled(bool)) , downloadsPalette, SLOT(setPaletteShown(bool)) );
	connect( downloadsPalette, SIGNAL(paletteShown(bool)), scrActions["toolsDownloads"], SLOT(setChecked(bool)));
	downloadsPalette->installEventFilter(this);
	connect( scrActions["toolsMeasurements"], SIGNAL(toggledData(bool,int)) , this, SLOT(setAppModeByToggle(bool,int)) );

	// Paragraph Styles Panel
	paragraphStylesPanelTabs = new ParagraphStylesPanel(this);
	m_suneerControlBar = new SuneerControlBar(this);
	m_suneerControlBar->setMovable(true);
	m_suneerControlBar->setFloatable(true);
	addToolBar(Qt::TopToolBarArea, m_suneerControlBar);
	m_suneerControlBar->show();
	// Impose Pages is hidden by default; only the preference (View menu
	// checkbox) brings its button back onto the control bar.
	{
		bool showImpose = m_prefsManager.appPrefs.uiPrefs.showImposePagesButton;
		scrActions["viewShowImposePagesButton"]->setChecked(showImpose);
		if (scrActions.contains("extrasImposition"))
			scrActions["extrasImposition"]->setVisible(showImpose);
	}
	// News Browser Panel
	m_suneerNewsPanel = new SuneerNewsPanel(this);
	m_suneerNewsPanel->setVisible(false);  // ADS tab-ൽ widget use ചെയ്യും
	// Not a view toggle any more: the News Browser is a tab in the Paragraph
	// Styles docker, so this menu entry just brings that tab to the front.
	scrActions["toolsNewsPanel"]->setCheckable(false);
	connect(scrActions["toolsNewsPanel"], &QAction::triggered, this, [this]() {
		if (m_suneerNewsPanel && paragraphStylesPanelTabs)
			paragraphStylesPanelTabs->showExtraTab(m_suneerNewsPanel->widget());
	});
	if (!PrefsManager::instance().appPrefs.experimentalFeaturePrefs.newsBrowserEnabled) {
		scrActions["toolsNewsPanel"]->setVisible(false);
	}
	paragraphStylesPanelTabs->setMainWindow(this);
	CDockWidget* stylesDock = new CDockWidget(tr("Paragraph Styles"), this);
	stylesDock->setWidget(paragraphStylesPanelTabs);
	stylesDock->setToggleViewAction(scrActions["toolsParagraphStyles"]);
	connect(scrActions["suneerApplyChain"], &QAction::triggered, paragraphStylesPanelTabs, &ParagraphStylesPanel::applyChainCurrentStyle);
	connect(scrActions["suneerAutoFitHeight"], &QAction::triggered, this, &ScribusMainWindow::suneerAutoFitHeight);
	connect(scrActions["itemFitCaptionFrame"], &QAction::triggered, this, &ScribusMainWindow::suneerFitCaptionFrames);
	connect(scrActions["suneerAutoFitText"], &QAction::toggled, this, &ScribusMainWindow::suneerAutoFitTextToggled);
	connect(this, &ScribusMainWindow::UpdateRequest, m_suneerControlBar, [this](int) { m_suneerControlBar->updateFromSelection(); });
	connect(this, &ScribusMainWindow::UpdateRequest, m_suneerControlBar, [this](int flag) {
		if (flag == reqTextStylesUpdate || flag == reqCharStylesUpdate || flag == 0)
			m_suneerControlBar->updateFromSelection();
	});
	// HaveNewSel is a slot, not signal — handled directly in HaveNewSel()
	connect(scrActions["suneerGetImage"], &QAction::triggered, this, &ScribusMainWindow::suneerGetImage);
	// The two preset export entries are available exactly when Save as PDF is.
	connect(scrActions["fileExportAsPDF"], &QAction::enabledChanged, this, [this](bool enabled) {
		scrActions["fileExportAsPDFDefault"]->setEnabled(enabled);
		scrMenuMgr->setMenuEnabled("FileExportPDFPreset", enabled);
	});
	scrActions["fileExportAsPDFDefault"]->setEnabled(scrActions["fileExportAsPDF"]->isEnabled());
	scrActions["viewShowImageLinkBadges"]->setChecked(SuneerImageLinks::badgesShown());
	connect(scrActions["suneerFocusFontCombo"], &QAction::triggered, this, [this]() { m_suneerControlBar->focusFontCombo(); });
	connect(scrActions["suneerEnlargeImageSize"], &QAction::triggered, this, &ScribusMainWindow::suneerEnlargeImageSize);
	connect(scrActions["suneerReduceImageSize"], &QAction::triggered, this, &ScribusMainWindow::suneerReduceImageSize);
	connect(scrActions["suneerEnlargeTextFrame"], &QAction::triggered, this, &ScribusMainWindow::suneerEnlargeTextFrame);
	connect(scrActions["suneerReduceTextFrame"], &QAction::triggered, this, &ScribusMainWindow::suneerReduceTextFrame);
	connect(scrActions["suneerTextToTable"], &QAction::triggered, this, &ScribusMainWindow::suneerTextToTable);
	connect(scrActions["suneerEnlargeTextSize"], &QAction::triggered, this, &ScribusMainWindow::suneerEnlargeTextSize);
	connect(scrActions["suneerReduceTextSize"], &QAction::triggered, this, &ScribusMainWindow::suneerReduceTextSize);
	connect(scrActions["suneerEnlargeLineSpacing"], &QAction::triggered, this, &ScribusMainWindow::suneerEnlargeLineSpacing);
	connect(scrActions["suneerReduceLineSpacing"], &QAction::triggered, this, &ScribusMainWindow::suneerReduceLineSpacing);

	// Ctrl+. / Ctrl+, — Image: scale, Text: font size
	{
		auto scaleUp = [this]() {
			if (!doc || doc->m_Selection->isEmpty()) return;
			PageItem* item = doc->m_Selection->itemAt(0);
			if (!item) return;
			if (item->isImageFrame() && item->imageIsAvailable)
			{
				if (doc->appMode == modeEdit)
					item->setImageXYScale(item->imageXScale() * 1.05, item->imageYScale() * 1.05);
				else
				{
					double ox = item->imageXOffset() * 1.05;
					double oy = item->imageYOffset() * 1.05;
					doc->sizeItem(item->width() * 1.05, item->height() * 1.05, item);
					item->setImageXYScale(item->imageXScale() * 1.05, item->imageYScale() * 1.05);
					item->setImageXYOffset(ox, oy);
				}
				item->update();
				doc->regionsChanged()->update(QRectF());
				doc->changed();
			}
			else if (item->isTextFrame())
			{
				if (doc->appMode == modeEdit)
				{
					if (item->itemText.hasSelection())
					{
						int curSz = qRound(item->itemText.charStyle(item->itemText.startOfSelection()).fontSize());
						doc->itemSelection_SetFontSize(curSz + 10);
					}
					else
					{
						int pos = item->itemText.cursorPosition();
						CharStyle cs = item->itemText.charStyle(pos);

						int curSz = qRound(cs.fontSize());
						cs.setFontSize(curSz + 10);

						item->itemText.applyCharStyle(pos, 1, cs);

						item->update();
						doc->regionsChanged()->update(QRectF());
						doc->changed();
					}
				}
				else
				{
					int sz = qRound(item->itemText.defaultStyle().charStyle().fontSize()) + 10;
					doc->itemSelection_SetFontSize(sz);
				}
			}
		};

		auto scaleDown = [this]() {
			if (!doc || doc->m_Selection->isEmpty()) return;
			PageItem* item = doc->m_Selection->itemAt(0);
			if (!item) return;
			if (item->isImageFrame() && item->imageIsAvailable)
			{
				if (doc->appMode == modeEdit)
					item->setImageXYScale(item->imageXScale() * 0.95, item->imageYScale() * 0.95);
				else
				{
					double ox = item->imageXOffset() * 0.95;
					double oy = item->imageYOffset() * 0.95;
					doc->sizeItem(item->width() * 0.95, item->height() * 0.95, item);
					item->setImageXYScale(item->imageXScale() * 0.95, item->imageYScale() * 0.95);
					item->setImageXYOffset(ox, oy);
				}
				item->update();
				doc->regionsChanged()->update(QRectF());
				doc->changed();
			}
			else if (item->isTextFrame())
			{
				if (doc->appMode == modeEdit)
				{
					if (item->itemText.hasSelection())
					{
						int curSz = qRound(item->itemText.charStyle(item->itemText.startOfSelection()).fontSize());
						doc->itemSelection_SetFontSize(qMax(10, curSz - 10));
					}
					else
					{
						int pos = item->itemText.cursorPosition();
						CharStyle cs = item->itemText.charStyle(pos);

						int curSz = qRound(cs.fontSize());
						cs.setFontSize(qMax(10, curSz - 10));

						item->itemText.applyCharStyle(pos, 1, cs);

						item->update();
						doc->regionsChanged()->update(QRectF());
						doc->changed();
					}
				}
				else
				{
					int sz = qMax(10, qRound(item->itemText.defaultStyle().charStyle().fontSize()) - 10);
					doc->itemSelection_SetFontSize(sz);
				}
			}
		};

		connect(scrActions["itemImageScaleUp"], &QAction::triggered, this, scaleUp);
		connect(scrActions["itemImageScaleDown"], &QAction::triggered, this, scaleDown);
	}

	// Image + Frame scale shortcuts



	auto* stylesArea = dockManager->addDockWidget(ads::CenterDockWidgetArea, stylesDock, dockManager->contentPalette->dockAreaWidget());

	// News Browser lives as the third tab inside the Paragraph Styles docker,
	// after Styles and Design Style. m_suneerNewsPanel stays alive as the owner
	// of the network manager and all the slots; only its visible widget moves.
	suneerSetNewsBrowserTabVisible(
		PrefsManager::instance().appPrefs.experimentalFeaturePrefs.newsBrowserEnabled);
	if (stylesArea)
	{
		for (auto* btn : stylesArea->findChildren<QAbstractButton*>())
		{
			const QString name = btn->objectName();
			if (name == "dockAreaCloseButton" || name == "detachGroupButton")
				btn->hide();
		}
	}

	// Preflight
	docCheckerPalette = new CheckDocument(this, false);
	connect( scrActions["toolsPreflightVerifier"], SIGNAL(toggled(bool)) , docCheckerPalette, SLOT(setPaletteShown(bool)) );
	connect( scrActions["toolsPreflightVerifier"], SIGNAL(toggled(bool)) , this, SLOT(docCheckToggle(bool)) );
	connect( docCheckerPalette, SIGNAL(paletteShown(bool)), scrActions["toolsPreflightVerifier"], SLOT(setChecked(bool)));
	connect( docCheckerPalette, SIGNAL(paletteShown(bool)), this, SLOT(docCheckToggle(bool)));
	docCheckerPalette->installEventFilter(this);
	docCheckerPalette->hide();

	// DocumentLog
	documentLogViewer = new DocumentLogViewer(this, false);
	documentLogViewer->setManagerConnections();
	connect( scrActions["toolsDocumentLog"], SIGNAL(toggled(bool)) , documentLogViewer, SLOT(setPaletteShown(bool)) );
	connect( documentLogViewer, SIGNAL(paletteShown(bool)), scrActions["toolsDocumentLog"], SLOT(setChecked(bool)));
	documentLogViewer->installEventFilter(this);
	documentLogViewer->hide();

	// Align & Distribute
	alignDistributePalette = dockManager->alignDistributePalette;
	alignDistributePalette->setToggleViewAction(scrActions["toolsAlignDistribute"]);
	connect( alignDistributePalette, SIGNAL(documentChanged()), this, SLOT(slotDocCh()));
	alignDistributePalette->installEventFilter(this);

	// Symbols
	symbolPalette = dockManager->symbolPalette;
	symbolPalette->setMainWindow(this);
	symbolPalette->setToggleViewAction(scrActions["toolsSymbols"]);
	connect(symbolPalette, SIGNAL(startEdit(QString)), this, SLOT(editSymbolStart(QString)));
	connect(symbolPalette, SIGNAL(endEdit()), this, SLOT(editSymbolEnd()));
	connect(symbolPalette, SIGNAL(objectDropped()), this, SLOT(PutToPatterns()));
	symbolPalette->installEventFilter(this);

	// Inline Elements
	inlinePalette = dockManager->inlinePalette;
	inlinePalette->setMainWindow(this);
	inlinePalette->setToggleViewAction(scrActions["toolsInline"]);
	connect(inlinePalette, SIGNAL(startEdit(int)), this, SLOT(editInlineStart(int)));
	connect(inlinePalette, SIGNAL(endEdit()), this, SLOT(editInlineEnd()));
	connect(inlinePalette, SIGNAL(objectDropped(QString)), this, SLOT(PutToInline(QString)));
	inlinePalette->installEventFilter(this);

	// Undo
	undoPalette = dockManager->undoPalette;
	undoPalette->setToggleViewAction(scrActions["toolsActionHistory"]);
	undoPalette->installEventFilter(this);
	m_undoManager->registerGui(undoPalette);
	connect(undoPalette, SIGNAL(objectMode(bool)), this, SLOT(setUndoMode(bool)));

	// initializing style manager here too even it's not strictly a palette
	m_styleManager = new StyleManager(this, "styleManager");
	SMCharacterStyle *tmpCS = new SMCharacterStyle();
	m_styleManager->addStyle(new SMParagraphStyle(tmpCS));
	m_styleManager->addStyle(tmpCS);
	m_styleManager->addStyle(new SMTableStyle());
	m_styleManager->addStyle(new SMCellStyle());
	m_styleManager->addStyle(new SMLineStyle());
	connect( scrActions["editStyles"], SIGNAL(toggled(bool)), m_styleManager, SLOT(setPaletteShown(bool)) );
	connect( m_styleManager, SIGNAL(paletteShown(bool)), scrActions["editStyles"], SLOT(setChecked(bool)));
	m_styleManager->installEventFilter(this);

	// initializing mark`s manager
	marksManager = new MarksManager(this, "marksManager");
	connect( scrActions["editMarks"], SIGNAL(toggled(bool)), marksManager, SLOT(setPaletteShown(bool)) );
	connect( marksManager, SIGNAL(paletteShown(bool)), scrActions["editMarks"], SLOT(setChecked(bool)));
	marksManager->installEventFilter(this);
	// initializing notes styles manager

	// Note Styles
	nsEditor = new NotesStylesEditor(this, "notesStylesEditor");
	connect( scrActions["editNotesStyles"], SIGNAL(toggled(bool)), nsEditor, SLOT(setPaletteShown(bool)) );
	connect( nsEditor, SIGNAL(paletteShown(bool)), scrActions["editNotesStyles"], SLOT(setChecked(bool)));
	nsEditor->installEventFilter(this);

	connect(docCheckerPalette, SIGNAL(selectElementByItem(PageItem*,bool)), this, SLOT(selectItemsFromOutlines(PageItem*,bool)));
	connect(docCheckerPalette, SIGNAL(selectElement(PageItem*,bool,int)), this, SLOT(selectItemFromOutlines(PageItem*,bool,int)));
	connect(docCheckerPalette, SIGNAL(selectPage(int)), this, SLOT(selectPagesFromOutlines(int)));
	connect(docCheckerPalette, SIGNAL(selectMasterPage(QString)), this, SLOT(editMasterPagesStart(QString)));
	connect(outlinePalette, SIGNAL(selectElementByItem(PageItem*,bool)), this, SLOT(selectItemsFromOutlines(PageItem*,bool)));
	connect(outlinePalette, SIGNAL(editElementByItem(PageItem*)), this, SLOT(editItemsFromOutlines(PageItem*)));
	connect(outlinePalette, SIGNAL(selectPage(int)), this, SLOT(selectPagesFromOutlines(int)));
	connect(outlinePalette, SIGNAL(selectMasterPage(QString)), this, SLOT(editMasterPagesStart(QString)));
	connect(nodePalette, SIGNAL(paletteClosed()), this, SLOT(slotSelect()));
	connect(nodePalette, SIGNAL(DocChanged()), this, SLOT(slotDocCh()));
	connect(layerPalette, SIGNAL(LayerChanged()), this, SLOT(showLayer()));

	connect(bookmarkPalette->BView, SIGNAL(markMoved()), this, SLOT(StoreBookmarks()));
	connect(bookmarkPalette->BView, SIGNAL(changed()), this, SLOT(slotDocCh()));
	connect(bookmarkPalette->BView, SIGNAL(selectElement(PageItem*,bool)), this, SLOT(selectItemsFromOutlines(PageItem*,bool)));
	// guides
	connect(scrActions["pageManageGuides"], SIGNAL(toggled(bool)), guidePalette, SLOT(setPaletteShown(bool)));
	connect(guidePalette, SIGNAL(paletteShown(bool)), scrActions["pageManageGuides"], SLOT(setChecked(bool)));
	// char palette
	connect(scrActions["insertGlyph"], SIGNAL(toggled(bool)), charPalette, SLOT(setPaletteShown(bool)));
	connect(charPalette, SIGNAL(paletteShown(bool)), scrActions["insertGlyph"], SLOT(setChecked(bool)));

}


// Scrapbooks the preferences list but whose folder did not answer at startup.
// Kept here, not in the class, so the main window's layout (which the plugins
// are compiled against) does not change.
static QStringList s_skippedScrapbooks;

void ScribusMainWindow::showNetworkPathNotes()
{
	const QStringList folders = NetPathGuard::takeNewlySkipped();
	if (folders.isEmpty())
		return;
	QStringList native;
	for (const QString& f : folders)
		native << QDir::toNativeSeparators(f);
	setStatusBarInfoText( tr("Network folder not reachable: %1 skipped").arg(native.join(", ")));
}

void ScribusMainWindow::initScrapbook()
{
	QString scrapbookFileO = QDir::toNativeSeparators(m_prefsManager.preferencesLocation()+"/scrap13.scs");
	QFileInfo scrapbookFileInfoO(scrapbookFileO);
	if (scrapbookFileInfoO.exists())
	{
		scrapbookPalette->readOldContents(scrapbookFileO, ScPaths::scrapbookDir(true) + "main");
		QDir d;
		d.rename(scrapbookFileO, QDir::toNativeSeparators(m_prefsManager.preferencesLocation()+"/scrap13.backup"));
	}
	QString scrapbookTemp = QDir::toNativeSeparators(ScPaths::scrapbookDir(true) + "tmp");
	QFileInfo scrapbookTempInfo(scrapbookTemp);
	if (scrapbookTempInfo.exists())
		scrapbookPalette->readTempContents(scrapbookTemp);
	QString scrapbookFile = QDir::toNativeSeparators(ScPaths::scrapbookDir(true) + "main");
	QFileInfo scrapbookFileInfo(scrapbookFile);
	if (scrapbookFileInfo.exists())
		scrapbookPalette->readContents(scrapbookFile);
	scrapbookPalette->setScrapbookFileName(scrapbookFile);
	ScCore->fileWatcher->addDir(scrapbookFile, true);
	// Scrapbooks on a share that is not answering are left closed for this
	// session and written back to the preferences untouched on exit.
	s_skippedScrapbooks.clear();
	QStringList openableScrapbooks;
	for (const QString& sb : std::as_const(m_prefsManager.appPrefs.scrapbookPrefs.RecentScrapbooks))
	{
		if (!NetPathGuard::reachable(sb))
			s_skippedScrapbooks << sb;
		else if (QFileInfo::exists(sb))
			openableScrapbooks << sb;
	}
	scrapbookPalette->setOpenScrapbooks(openableScrapbooks);
	QString systemScrapbook = QDir::toNativeSeparators(ScPaths::instance().shareDir() + "scrapbook/faircode-frames");
	if (QFileInfo(systemScrapbook).isDir())
		scrapbookPalette->openSystemScrapbook(systemScrapbook, "Faircode Frames");
	rebuildRecentPasteMenu();
	connect(scrapbookPalette, SIGNAL(updateRecentMenue()), this, SLOT(rebuildRecentPasteMenu()));
	connect(ScCore->fileWatcher, SIGNAL(dirChanged(QString)), scrapbookPalette, SLOT(reloadLib(QString)));
	connect(ScCore->fileWatcher, SIGNAL(dirDeleted(QString)), scrapbookPalette, SLOT(closeOnDel(QString)));
}

bool ScribusMainWindow::warningVersion(QWidget *parent)
{
	bool retval = false;
	int t = ScMessageBox::warning(parent, QObject::tr("Document Version Warning"), "<qt>" +
								 QObject::tr("The document you are working with was created by a previous version of Scribus. Saving the current file under a newer version will render it unable to be edited by that older version. To preserve the ability to edit the file with the older version, save this file under a different name and further edit the newly named file and the original will be untouched. Are you sure you wish to proceed with this operation?") + "</qt>",
								 QMessageBox::Ok | QMessageBox::Cancel,
								 QMessageBox::Cancel,	// GUI default
								 QMessageBox::Ok);	// batch default
	if (t == QMessageBox::Ok)
		retval = true;
	return retval;
}

void ScribusMainWindow::initMdiArea()
{
	mdiArea = new QMdiArea(this);
	mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	if (m_prefsManager.appPrefs.uiPrefs.useTabs)
	{
		mdiArea->setViewMode(QMdiArea::TabbedView);
		mdiArea->setTabsClosable(true);
		mdiArea->setDocumentMode(true);
	}
	else
		mdiArea->setViewMode(QMdiArea::SubWindowView);
	//setCentralWidget(mdiArea);
	dockManager->setCentralWidget(mdiArea);
}

void ScribusMainWindow::initMenuBar()
{
	m_recentDocsList.clear();
	scrMenuMgr->createMenu("File", ActionManager::defaultMenuNameEntryTranslated("File"));
	scrMenuMgr->addMenuItemString("fileNew", "File");
	scrMenuMgr->addMenuItemString("fileNewFromTemplate", "File");
	scrMenuMgr->addMenuItemString("fileOpen", "File");
	scrMenuMgr->addMenuItemString("FileOpenRecent", "File");
	scrMenuMgr->createMenu("FileOpenRecent", tr("Open &Recent"), "File", false, true);
	scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	scrMenuMgr->addMenuItemString("fileClose", "File");
	scrMenuMgr->addMenuItemString("fileSave", "File");
	scrMenuMgr->addMenuItemString("fileSaveAs", "File");
	scrMenuMgr->addMenuItemString("fileRevert", "File");
	scrMenuMgr->addMenuItemString("fileCollect", "File");
	scrMenuMgr->addMenuItemString("filePlace", "File");
	scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	scrMenuMgr->createMenu("FileImport", tr("&Import"), "File");
	scrMenuMgr->addMenuItemString("FileImport", "File");
	scrMenuMgr->addMenuItemString("fileImportText", "FileImport");
	scrMenuMgr->addMenuItemString("fileImportAppendText", "FileImport");
	scrMenuMgr->addMenuItemString("fileImportImage", "FileImport");
	scrMenuMgr->addMenuItemString("fileImportVector", "FileImport");
	scrMenuMgr->addMenuItemString("FileExport", "File");
	scrMenuMgr->createMenu("FileExport", tr("&Export"), "File");
	scrMenuMgr->addMenuItemString("fileExportText", "FileExport");
	scrMenuMgr->addMenuItemString("fileExportAsEPS", "FileExport");
	scrMenuMgr->addMenuItemString("fileExportAsPDF", "FileExport");
	scrMenuMgr->addMenuItemString("fileExportAsPDFDefault", "FileExport");
	scrMenuMgr->createMenu("FileExportPDFPreset", tr("Save as PDF with preset"), "FileExport", false, true);
	scrMenuMgr->addMenuItemString("FileExportPDFPreset", "FileExport");
	scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	scrMenuMgr->addMenuItemString("fileDocSetup150", "File");
	scrMenuMgr->addMenuItemString("filePreferences150", "File");
	scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	scrMenuMgr->addMenuItemString("filePrint", "File");
	scrMenuMgr->addMenuItemString("fileProofPrint", "File");
	if (ScCore->haveGS() || ScCore->isWinGUI())
		scrMenuMgr->addMenuItemString("PrintPreview", "File");
	scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	if (ScCore->haveGS())
	{
		scrMenuMgr->addMenuItemString("FileOutputPreview", "File");
		scrMenuMgr->createMenu("FileOutputPreview", tr("&Output Preview"), "File");
		scrMenuMgr->addMenuItemString("OutputPreviewPDF", "FileOutputPreview");
		scrMenuMgr->addMenuItemString("OutputPreviewPS", "FileOutputPreview");
		scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	}
	if (ScCore->haveGS())
	{
		// Unlike Output Preview above, these tools work on an arbitrary
		// external PDF and don't need a Scribus document open, so this menu
		// is never disabled the way FileOutputPreview is below.
		scrMenuMgr->addMenuItemString("PDFTools", "File");
		scrMenuMgr->createMenu("PDFTools", tr("PDF &Tools"), "File");
		scrMenuMgr->addMenuItemString("ViewPDFSeparations", "PDFTools");
		scrMenuMgr->addMenuItemString("SEPARATOR", "File");
	}
	scrMenuMgr->addMenuItemString("fileQuit", "File");

	scrMenuMgr->setMenuEnabled("FileImport", false);
	scrMenuMgr->setMenuEnabled("FileExport", false);
	scrMenuMgr->setMenuEnabled("FileOutputPreview", false);

	scrMenuMgr->createMenu("Edit", ActionManager::defaultMenuNameEntryTranslated("Edit"));
	scrMenuMgr->addMenuItemString("editUndoAction", "Edit");
	scrMenuMgr->addMenuItemString("editRedoAction", "Edit");
	scrMenuMgr->addMenuItemString("editActionMode", "Edit");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Edit");
	scrMenuMgr->addMenuItemString("editCut", "Edit");
	scrMenuMgr->addMenuItemString("editCopy", "Edit");
	scrMenuMgr->addMenuItemString("editPaste", "Edit");
	scrMenuMgr->addMenuItemString("editPasteOriginalPosition", "Edit");
	scrMenuMgr->addMenuItemString("editPastePlainText", "Edit");
	scrMenuMgr->addMenuItemString("editStyledCopy", "Edit");
	scrMenuMgr->addMenuItemString("editStyledPaste", "Edit");
	scrMenuMgr->createMenu("EditPasteRecent", tr("Paste Recent"), "Edit", false, true);
	scrMenuMgr->addMenuItemString("itemDelete", "Edit");
	scrMenuMgr->createMenu("EditContents", tr("Contents"), "Edit", false, true);
	scrMenuMgr->addMenuItemString("EditContents", "Edit");
	scrMenuMgr->addMenuItemString("editCopyContents", "EditContents");
	scrMenuMgr->addMenuItemString("editPasteContents", "EditContents");
	scrMenuMgr->addMenuItemString("editPasteContentsAbs", "EditContents");
	scrMenuMgr->addMenuItemString("editClearContents", "EditContents");
	scrMenuMgr->addMenuItemString("editTruncateContents", "EditContents");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Edit");
	scrMenuMgr->addMenuItemString("editSelectAll", "Edit");
	scrMenuMgr->addMenuItemString("editSelectAllOnLayer", "Edit");
	scrMenuMgr->addMenuItemString("editDeselectAll", "Edit");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Edit");
	scrMenuMgr->addMenuItemString("editSearchReplace", "Edit");
	scrMenuMgr->addMenuItemString("toolsEditWithStoryEditor", "Edit");
	scrMenuMgr->addMenuItemString("editEditWithImageEditor", "Edit");
	scrMenuMgr->addMenuItemString("imageEditor", "Edit");
	scrMenuMgr->addMenuItemString("editEditRenderSource", "Edit");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Edit");
	scrMenuMgr->addMenuItemString("editColorsAndFills", "Edit");
	scrMenuMgr->addMenuItemString("editReplaceColors", "Edit");
	scrMenuMgr->addMenuItemString("editStyles", "Edit");
	scrMenuMgr->addMenuItemString("editMarks", "Edit");
	scrMenuMgr->addMenuItemString("editNotesStyles", "Edit");
	scrMenuMgr->addMenuItemString("editMasterPages", "Edit");
	scrMenuMgr->addMenuItemString("editJavascripts", "Edit");
	scrMenuMgr->setMenuEnabled("EditPasteRecent", false);
	scrMenuMgr->setMenuEnabled("EditContents", false);


	//Item Menu
	scrMenuMgr->createMenu("Item", ActionManager::defaultMenuNameEntryTranslated("Item"));
	scrMenuMgr->createMenu("DuplicateTransform", tr("Duplicate/Transform"), "Item");
	scrMenuMgr->addMenuItemString("DuplicateTransform", "Item");
	scrMenuMgr->addMenuItemString("itemDuplicate", "DuplicateTransform");
	scrMenuMgr->addMenuItemString("itemMulDuplicate", "DuplicateTransform");
	scrMenuMgr->addMenuItemString("itemTransform", "DuplicateTransform");
	scrMenuMgr->createMenu("Grouping", tr("Grouping"), "Item");
	scrMenuMgr->addMenuItemString("Grouping", "Item");
	scrMenuMgr->addMenuItemString("itemGroup", "Grouping");
	scrMenuMgr->addMenuItemString("itemUngroup", "Grouping");
	scrMenuMgr->addMenuItemString("itemGroupAdjust", "Grouping");
	scrMenuMgr->createMenu("Locking", tr("Locking"), "Item");
	scrMenuMgr->addMenuItemString("Locking", "Item");
	scrMenuMgr->addMenuItemString("itemLock", "Locking");
	scrMenuMgr->addMenuItemString("itemLockSize", "Locking");
	scrMenuMgr->addMenuItemString("itemLockAspectRatio", "Locking");
	scrMenuMgr->createMenu("ItemLevel", tr("Level"), "Item");
	scrMenuMgr->addMenuItemString("ItemLevel", "Item");
	scrMenuMgr->addMenuItemString("itemRaise", "ItemLevel");
	scrMenuMgr->addMenuItemString("itemLower", "ItemLevel");
	scrMenuMgr->addMenuItemString("itemRaiseToTop", "ItemLevel");
	scrMenuMgr->addMenuItemString("itemLowerToBottom", "ItemLevel");
	scrMenuMgr->createMenu("ItemLayer", tr("Send to La&yer"), QString(), false, true);
	scrMenuMgr->addMenuItemString("ItemLayer", "Item");
	scrMenuMgr->createMenu("SendTo", tr("Send to"), "Item");
	scrMenuMgr->addMenuItemString("SendTo", "Item");
	scrMenuMgr->createMenu("ItemSendToScrapbook", tr("Scrapbook"), QString(), false, true);
	scrMenuMgr->addMenuItemString("ItemSendToScrapbook", "SendTo");
	scrMenuMgr->addMenuItemString("itemSendToPattern", "SendTo");
	scrMenuMgr->addMenuItemString("itemSendToInline", "SendTo");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Item");

	scrMenuMgr->createMenu("ItemConvertTo", tr("C&onvert to"), "Item");
	scrMenuMgr->addMenuItemString("ItemConvertTo", "Item");
	scrMenuMgr->addMenuItemString("itemConvertToTextFrame", "ItemConvertTo");
	scrMenuMgr->addMenuItemString("itemConvertToImageFrame", "ItemConvertTo");
	scrMenuMgr->addMenuItemString("itemConvertToPolygon", "ItemConvertTo");
	scrMenuMgr->addMenuItemString("itemConvertToBezierCurve", "ItemConvertTo");
	scrMenuMgr->addMenuItemString("itemConvertToOutlines", "ItemConvertTo");
	scrMenuMgr->addMenuItemString("itemConvertToSymbolFrame", "ItemConvertTo");
	/* AI Text Tools. Its own submenu rather than loose entries in Item: six
	   actions that all cost money and all send something out of the building
	   are worth grouping under a name that says so. */
	scrMenuMgr->createMenu("ItemAIText", tr("AI Te&xt Tools"), "Item");
	scrMenuMgr->addMenuItemString("ItemAIText", "Item");
	scrMenuMgr->addMenuItemString("itemAITextCaption", "ItemAIText");
	scrMenuMgr->addMenuItemString("itemAITextAltText", "ItemAIText");
	scrMenuMgr->addMenuItemString("SEPARATOR", "ItemAIText");
	scrMenuMgr->addMenuItemString("itemAITextHeadline", "ItemAIText");
	scrMenuMgr->addMenuItemString("itemAITextSummarize", "ItemAIText");
	scrMenuMgr->addMenuItemString("itemAITextTranslate", "ItemAIText");
	scrMenuMgr->addMenuItemString("itemAITextImprove", "ItemAIText");

	scrMenuMgr->createMenu("Adjust", tr("Adjust"), "Item");
	scrMenuMgr->addMenuItemString("Adjust", "Item");
	scrMenuMgr->addMenuItemString("itemAdjustFrameHeightToText", "Adjust");
	scrMenuMgr->addMenuItemString("itemFitCaptionFrame", "Adjust");
	scrMenuMgr->addMenuItemString("itemAdjustFrameToImage", "Adjust");
	scrMenuMgr->addMenuItemString("itemAdjustImageToFrame", "Adjust");
	scrMenuMgr->createMenu("ItemPathOps", tr("Shape && Paths"), "Item");
	scrMenuMgr->addMenuItemString("ItemPathOps", "Item");
	scrMenuMgr->addMenuItemString("itemShapeEdit", "ItemPathOps");
	scrMenuMgr->addMenuItemString("itemDetectContour", "ItemPathOps");
	scrMenuMgr->addMenuItemString("toolsRemoveObject", "ItemPathOps");
	scrMenuMgr->addMenuItemString("itemCombinePolygons", "ItemPathOps");
	scrMenuMgr->addMenuItemString("itemSplitPolygons", "ItemPathOps");
	scrMenuMgr->addMenuItemString("itemAttachTextToPath", "ItemPathOps");
	scrMenuMgr->addMenuItemString("itemDetachTextFromPath", "ItemPathOps");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Item");
	scrMenuMgr->createMenu("Image", tr("Image"), "Item");
	scrMenuMgr->addMenuItemString("Image", "Item");
	scrMenuMgr->addMenuItemString("itemUpdateImage", "Image");
	scrMenuMgr->addMenuItemString("styleImageEffects", "Image");
	scrMenuMgr->addMenuItemString("itemExtendedImageProperties", "Image");
	scrMenuMgr->addMenuItemString("itemToggleInlineImage", "Image");
	scrMenuMgr->createMenu("ItemPreviewSettings", tr("Preview Settings"));
	scrMenuMgr->addMenuItemString("ItemPreviewSettings", "Image");
	scrMenuMgr->addMenuItemString("itemImageIsVisible", "ItemPreviewSettings");
	scrMenuMgr->addMenuItemString("SEPARATOR", "ItemPreviewSettings");
	scrMenuMgr->addMenuItemString("itemPreviewFull", "ItemPreviewSettings");
	scrMenuMgr->addMenuItemString("itemPreviewNormal", "ItemPreviewSettings");
	scrMenuMgr->addMenuItemString("itemPreviewLow", "ItemPreviewSettings");
	scrMenuMgr->createMenu("TextFeatures", tr("Text Features", "Item"));
	scrMenuMgr->addMenuItemString("TextFeatures", "Item");
	scrMenuMgr->addMenuItemString("itemStyleSearch", "TextFeatures");
	scrMenuMgr->addMenuItemString("SEPARATOR", "TextFeatures");
	scrMenuMgr->addMenuItemString("alignLeft", "TextFeatures");
	scrMenuMgr->addMenuItemString("alignCenter", "TextFeatures");
	scrMenuMgr->addMenuItemString("alignRight", "TextFeatures");
	scrMenuMgr->addMenuItemString("alignBlock", "TextFeatures");
	scrMenuMgr->addMenuItemString("alignForced", "TextFeatures");
	scrMenuMgr->addMenuItemString("SEPARATOR", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectNormal", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectUnderline", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectUnderlineWords", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectStrikeThrough", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectAllCaps", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectSmallCaps", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectSuperscript", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectSubscript", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectOutline", "TextFeatures");
	scrMenuMgr->addMenuItemString("typeEffectShadow", "TextFeatures");
	scrMenuMgr->createMenu("TextLinking", tr("Text Frame Links"), "Item");
	scrMenuMgr->addMenuItemString("TextLinking", "Item");
	scrMenuMgr->addMenuItemString("toolsLinkTextFrame", "TextLinking");
	scrMenuMgr->addMenuItemString("toolsUnlinkTextFrame", "TextLinking");
	scrMenuMgr->addMenuItemString("toolsUnlinkTextFrameAndCutText", "TextLinking");
	scrMenuMgr->createMenu("TextTransformation", tr("Text Transformation"), "Item");
	scrMenuMgr->addMenuItemString("TextTransformation", "Item");
	scrMenuMgr->addMenuItemString("itemTextTransformLowercase", "TextTransformation");
	scrMenuMgr->addMenuItemString("itemTextTransformUppercase", "TextTransformation");
	scrMenuMgr->addMenuItemString("itemTextTransformSentencecase", "TextTransformation");
	scrMenuMgr->addMenuItemString("itemTextTransformCapitalize", "TextTransformation");
	scrMenuMgr->addMenuItemString("itemTextTransformToggleCase", "TextTransformation");
	scrMenuMgr->createMenu("ItemPDFOptions", tr("P&DF Options"));
	scrMenuMgr->addMenuItemString("ItemPDFOptions", "Item");
	scrMenuMgr->addMenuItemString("itemPDFIsAnnotation", "ItemPDFOptions");
	scrMenuMgr->addMenuItemString("itemPDFIsBookmark", "ItemPDFOptions");
	scrMenuMgr->addMenuItemString("itemPDFAnnotationProps", "ItemPDFOptions");
	scrMenuMgr->addMenuItemString("itemPDFFieldProps", "ItemPDFOptions");

	scrMenuMgr->addMenuItemString("SEPARATOR", "Item");
	scrMenuMgr->createMenu("Weld", tr("Welding"), "Item");
	scrMenuMgr->addMenuItemString("Weld", "Item");
	scrMenuMgr->addMenuItemString("itemWeld", "Weld");
	scrMenuMgr->addMenuItemString("itemsUnWeld", "Weld");
	scrMenuMgr->addMenuItemString("itemEditWeld", "Weld");
	scrMenuMgr->createMenu("Marks", tr("Marks"), "Item");
	scrMenuMgr->addMenuItemString("Marks", "Item");
	scrMenuMgr->addMenuItemString("editMark", "Marks");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Item");
	scrMenuMgr->addMenuItemString("itemAttributes", "Item");
	// "itemsAutoArrange" is deliberately NOT added — the feature is parked, see
	// the note in ContextMenu and NOTES.md.

	scrActions["itemPrintingEnabled"]->setEnabled(false);
	scrMenuMgr->setMenuEnabled("ItemConvertTo", false);

	//Insert menu
	scrMenuMgr->createMenu("Insert", ActionManager::defaultMenuNameEntryTranslated("Insert"));
	scrMenuMgr->addMenuItemString("insertFrame", "Insert");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertTextFrame", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertImageFrame", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertRenderFrame", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertTable", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertShape", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertPolygon", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertArc", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertSpiral", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertLine", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertBezier", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertFreehandLine", "Insert");
	scrMenuMgr->addMenuItemString("toolsInsertCalligraphicLine", "Insert");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Insert");
	scrMenuMgr->addMenuItemString("stickyTools", "Insert");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Insert");
	scrMenuMgr->addMenuItemString("insertGlyph", "Insert");

	scrMenuMgr->createMenu("InsertChar", tr("&Character"), "Insert");
	scrMenuMgr->addMenuItemString("InsertChar", "Insert");
	scrMenuMgr->addMenuItemString("unicodePageNumber", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodePageCount", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeSoftHyphen", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeNonBreakingHyphen", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeZWJ", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeZWNJ", "InsertChar");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeCopyRight", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeRegdTM", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeTM", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeSolidus", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeBullet", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeMidpoint", "InsertChar");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeDashEm", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeDashEn", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeDashFigure", "InsertChar");
	scrMenuMgr->addMenuItemString("unicodeDashQuotation", "InsertChar");

	scrMenuMgr->createMenu("InsertQuote", tr("&Quote"), "Insert");
	scrMenuMgr->addMenuItemString("InsertQuote", "Insert");
	scrMenuMgr->addMenuItemString("unicodeQuoteApostrophe", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteStraight", "InsertQuote");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteSingleLeft", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteSingleRight", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteDoubleLeft", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteDoubleRight", "InsertQuote");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteSingleReversed", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteDoubleReversed", "InsertQuote");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteLowSingleComma", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteLowDoubleComma", "InsertQuote");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteSingleLeftGuillemet", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteSingleRightGuillemet", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteDoubleLeftGuillemet", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteDoubleRightGuillemet", "InsertQuote");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteCJKSingleLeft", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteCJKSingleRight", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteCJKDoubleLeft", "InsertQuote");
	scrMenuMgr->addMenuItemString("unicodeQuoteCJKDoubleRight", "InsertQuote");

	scrMenuMgr->createMenu("InsertSpace", tr("S&paces && Breaks"), "Insert");
	scrMenuMgr->addMenuItemString("InsertSpace", "Insert");
	scrMenuMgr->addMenuItemString("unicodeNonBreakingSpace", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeNarrowNoBreakSpace", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceEN", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceEM", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceThin", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceThick", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceMid", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeSpaceHair", "InsertSpace");
	scrMenuMgr->addMenuItemString("SEPARATOR", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeZerowidthSpace", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeNewLine", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeColumnBreak", "InsertSpace");
	scrMenuMgr->addMenuItemString("unicodeFrameBreak", "InsertSpace");

	scrMenuMgr->createMenu("InsertLigature", tr("Liga&ture"), "Insert");
	scrMenuMgr->addMenuItemString("InsertLigature", "Insert");
	scrMenuMgr->addMenuItemString("unicodeLigature_ff", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_fi", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_fl", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_ffi", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_ffl", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_ft", "InsertLigature");
	scrMenuMgr->addMenuItemString("unicodeLigature_st", "InsertLigature");

	scrMenuMgr->addMenuItemString("SEPARATOR", "Insert");
	scrMenuMgr->addMenuItemString("insertSampleText", "Insert");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Insert");
	scrMenuMgr->createMenu("InsertMark", tr("Marks"), "Insert");
	scrMenuMgr->addMenuItemString("InsertMark", "Insert");
	scrMenuMgr->addMenuItemString("insertMarkAnchor", "InsertMark");
	scrMenuMgr->addMenuItemString("insertMarkNote", "InsertMark");
	scrMenuMgr->addMenuItemString("insertMarkItem", "InsertMark");
	scrMenuMgr->addMenuItemString("insertMark2Mark", "InsertMark");
	scrMenuMgr->addMenuItemString("insertMarkVariableText", "InsertMark");
	scrMenuMgr->addMenuItemString("insertMarkIndex", "InsertMark");

	//Page menu
	scrMenuMgr->createMenu("Page", ActionManager::defaultMenuNameEntryTranslated("Page"));
	scrMenuMgr->addMenuItemString("pageInsert", "Page");
	scrMenuMgr->addMenuItemString("pageImport", "Page");
	scrMenuMgr->addMenuItemString("pageDelete", "Page");
	scrMenuMgr->addMenuItemString("pageCopy", "Page");
	scrMenuMgr->addMenuItemString("pageMove", "Page");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Page");
	scrMenuMgr->addMenuItemString("pageApplyMasterPage", "Page");
	scrMenuMgr->addMenuItemString("pageCopyToMasterPage", "Page");
	scrMenuMgr->addMenuItemString("pageManageGuides", "Page");
	scrMenuMgr->addMenuItemString("pageManageProperties", "Page");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Page");
	scrMenuMgr->addMenuItemString("viewSnapToGrid", "Page");
	scrMenuMgr->addMenuItemString("viewSnapToGuides", "Page");
	scrMenuMgr->addMenuItemString("viewSnapToItems", "Page");

	//View menu
	scrMenuMgr->createMenu("View", ActionManager::defaultMenuNameEntryTranslated("View"));
	scrMenuMgr->createMenu("ViewZoom", tr("Zoom"), "View");
	scrMenuMgr->addMenuItemString("ViewZoom", "View");
	scrMenuMgr->addMenuItemString("viewFitInWindow", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFitWidth", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFit50", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFit75", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFit100", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFit200", "ViewZoom");
	scrMenuMgr->addMenuItemString("viewFit400", "ViewZoom");
	scrMenuMgr->createMenu("ViewPreview", tr("Preview"), "View");
	scrMenuMgr->addMenuItemString("ViewPreview", "View");
	scrMenuMgr->addMenuItemString("viewPreviewMode", "ViewPreview");
	scrMenuMgr->addMenuItemString("viewToggleWhiteSpaceMode", "ViewPreview");
	scrMenuMgr->createMenu("ViewMeasuring", tr("Measurement"), "View");
	scrMenuMgr->addMenuItemString("ViewMeasuring", "View");
	scrMenuMgr->addMenuItemString("viewShowRulers", "ViewMeasuring");
	scrMenuMgr->addMenuItemString("viewRulerMode", "ViewMeasuring");
	scrMenuMgr->addMenuItemString("showMouseCoordinates", "ViewMeasuring");
	scrMenuMgr->createMenu("ViewTextFrames", tr("Text Frames"), "View");
	scrMenuMgr->addMenuItemString("ViewTextFrames", "View");
	scrMenuMgr->addMenuItemString("viewShowBaseline", "ViewTextFrames");
	scrMenuMgr->addMenuItemString("viewShowColumnBorders", "ViewTextFrames");
	scrMenuMgr->addMenuItemString("viewShowTextChain", "ViewTextFrames");
	scrMenuMgr->addMenuItemString("viewShowTextControls", "ViewTextFrames");
	scrMenuMgr->createMenu("ViewImageFrames", tr("Image Frames"), "View");
	scrMenuMgr->addMenuItemString("ViewImageFrames", "View");
	scrMenuMgr->addMenuItemString("viewShowImages", "ViewImageFrames");
	scrMenuMgr->addMenuItemString("viewShowImageLinkBadges", "ViewImageFrames");
	scrMenuMgr->createMenu("ViewDocument", tr("Document"), "View");
	scrMenuMgr->addMenuItemString("ViewDocument", "View");
	scrMenuMgr->addMenuItemString("viewShowMargins", "ViewDocument");
	scrMenuMgr->addMenuItemString("viewShowBleeds", "ViewDocument");
	scrMenuMgr->addMenuItemString("viewShowFrames", "ViewDocument");
	scrMenuMgr->addMenuItemString("viewShowLayerMarkers", "ViewDocument");
	scrMenuMgr->createMenu("ViewGrids", tr("Grids and Guides"), "View");
	scrMenuMgr->addMenuItemString("ViewGrids", "View");
	scrMenuMgr->addMenuItemString("viewShowGrid", "ViewGrids");
	scrMenuMgr->addMenuItemString("viewShowGuides", "ViewGrids");
	scrMenuMgr->addMenuItemString("SEPARATOR", "View");
	scrMenuMgr->addMenuItemString("viewShowImposePagesButton", "View");

	//CB If this is viewNewView imeplemented, it should be on the windows menu
//	scrMenuMgr->addMenuItem(scrActions["viewNewView"], "View");

	// Table menu.
	scrMenuMgr->createMenu("ItemTable", ActionManager::defaultMenuNameEntryTranslated("Table"));
	scrMenuMgr->addMenuItemString("tableInsertRows", "ItemTable");
	scrMenuMgr->addMenuItemString("tableInsertColumns", "ItemTable");
	scrMenuMgr->addMenuItemString("tableDeleteRows", "ItemTable");
	scrMenuMgr->addMenuItemString("tableDeleteColumns", "ItemTable");
	scrMenuMgr->addMenuItemString("SEPARATOR", "ItemTable");
	scrMenuMgr->addMenuItemString("tableMergeCells", "ItemTable");
	scrMenuMgr->addMenuItemString("tableSplitCells", "ItemTable");
	scrMenuMgr->addMenuItemString("SEPARATOR", "ItemTable");
	scrMenuMgr->addMenuItemString("tableSetRowHeights", "ItemTable");
	scrMenuMgr->addMenuItemString("tableSetColumnWidths", "ItemTable");
	scrMenuMgr->addMenuItemString("tableDistributeRowsEvenly", "ItemTable");
	scrMenuMgr->addMenuItemString("tableDistributeColumnsEvenly", "ItemTable");
	scrMenuMgr->addMenuItemString("SEPARATOR", "ItemTable");
	scrMenuMgr->addMenuItemString("tableAdjustFrameToTable", "ItemTable");
	scrMenuMgr->addMenuItemString("tableAdjustTableToFrame", "ItemTable");

	//Extra menu
	scrMenuMgr->createMenu("Extras", ActionManager::defaultMenuNameEntryTranslated("Extras"));
	scrMenuMgr->addMenuItemString("extrasHyphenateText", "Extras");
	scrMenuMgr->addMenuItemString("extrasDeHyphenateText", "Extras");
	scrMenuMgr->addMenuItemString("extrasGenerateTableOfContents", "Extras");
	scrMenuMgr->addMenuItemString("itemUpdateMarks", "Extras");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Extras");
	scrMenuMgr->addMenuItemString("extrasManageImages", "Extras");
	scrMenuMgr->addMenuItemString("extrasEmbedAllImages", "Extras");
	scrMenuMgr->addMenuItemString("extrasWarnLinkedImages", "Extras");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Extras");
	scrMenuMgr->addMenuItemString("extrasAutoflowToNewPages", "Extras");
	scrMenuMgr->addMenuItemString("extrasFixOverflowFrames", "Extras");
	scrMenuMgr->addMenuItemString("extrasFixOverflowFramesDoc", "Extras");
	scrMenuMgr->addMenuItemString("extrasUpdateDocument", "Extras");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Extras");
	scrMenuMgr->addMenuItemString("extrasImposition", "Extras");
	scrMenuMgr->createMenu("SRTools", tr("SR Tools"), "Extras");
	scrMenuMgr->addMenuItemString("SRTools", "Extras");
	scrMenuMgr->addMenuItemString("SRDuplicateNewsCheck", "SRTools");
//	Disabled for release as it does nothing useful
//	scrMenuMgr->addMenuItemString("extrasTestQTQuick2_1", "Extras");

	//Window menu
	scrMenuMgr->createMenu("Windows", ActionManager::defaultMenuNameEntryTranslated("Windows"), QString(), true);

	//Help menu
	scrMenuMgr->createMenu("Help", ActionManager::defaultMenuNameEntryTranslated("Help"));
	scrMenuMgr->addMenuItemString("helpManual", "Help");
	scrMenuMgr->addMenuItemString("helpChat", "Help");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Help");
	scrMenuMgr->addMenuItemString("helpActionSearch", "Help");
	scrMenuMgr->addMenuItemString("helpTooltips", "Help");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Help");
	scrMenuMgr->addMenuItemString("helpOnlineWWW", "Help");
	scrMenuMgr->addMenuItemString("helpOnlineDocs", "Help");
	scrMenuMgr->addMenuItemString("helpOnlineWiki", "Help");
	scrMenuMgr->addMenuItemString("HelpOnlineTutorials", "Help");
	scrMenuMgr->addMenuItemString("helpOnlineTutorial1", "Help");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Help");
	scrMenuMgr->addMenuItemString("helpCheckUpdates", "Help");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Help");
	scrMenuMgr->addMenuItemString("helpAboutScribus", "Help");
	scrMenuMgr->addMenuItemString("helpAboutPlugins", "Help");
	scrMenuMgr->addMenuItemString("helpAboutQt", "Help");
}

void ScribusMainWindow::createMenuBar()
{
	scrMenuMgr->addMenuStringToMenuBar("File");
	scrMenuMgr->addMenuItemStringsToMenuBar("File", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Edit");
	scrMenuMgr->addMenuItemStringsToMenuBar("Edit", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Item");
	scrMenuMgr->addMenuItemStringsToMenuBar("Item", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Insert");
	scrMenuMgr->addMenuItemStringsToMenuBar("Insert", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Page");
	scrMenuMgr->addMenuItemStringsToMenuBar("Page", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("ItemTable");
	scrMenuMgr->addMenuItemStringsToMenuBar("ItemTable", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Extras");
	scrMenuMgr->addMenuItemStringsToMenuBar("Extras", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("View");
	scrMenuMgr->addMenuItemStringsToMenuBar("View", scrActions);
	scrMenuMgr->addMenuStringToMenuBar("Windows", true);
	addDefaultWindowMenuItems();
	menuBar()->addSeparator();
	scrMenuMgr->addMenuStringToMenuBar("Help");
	scrMenuMgr->addMenuItemStringsToMenuBar("Help", scrActions);

	connect(scrMenuMgr->getLocalPopupMenu("Extras"), SIGNAL(aboutToShow()), this, SLOT(extrasMenuAboutToShow()));
	connect(scrMenuMgr->getLocalPopupMenu("Windows"), SIGNAL(aboutToShow()), this, SLOT(windowsMenuAboutToShow()));

}


void ScribusMainWindow::addDefaultWindowMenuItems()
{
	scrMenuMgr->clearMenu("Windows");
	scrMenuMgr->addMenuItemString("windowsCascade", "Windows");
	scrMenuMgr->addMenuItemString("windowsTile", "Windows");
	scrMenuMgr->addMenuItemString("specialToggleAllPalettes", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemString("toolsProperties", "Windows");
	scrMenuMgr->addMenuItemString("toolsContent", "Windows");
	scrMenuMgr->addMenuItemString("toolsParagraphStyles", "Windows");
	scrMenuMgr->addMenuItemString("toolsNewsPanel", "Windows");
	scrMenuMgr->addMenuItemString("toolsActionHistory", "Windows");
	scrMenuMgr->addMenuItemString("toolsAlignDistribute", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemString("toolsOutline", "Windows");
	scrMenuMgr->addMenuItemString("toolsPages", "Windows");
	scrMenuMgr->addMenuItemString("toolsLayers", "Windows");
	scrMenuMgr->addMenuItemString("toolsBookmarks", "Windows");
//	scrMenuMgr->addMenuItemString("toolsDownloads", "Windows");
	scrMenuMgr->addMenuItemString("toolsResources", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemString("toolsScrapbook", "Windows");
	scrMenuMgr->addMenuItemString("toolsSymbols", "Windows");
	scrMenuMgr->addMenuItemString("toolsInline", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemString("toolsMeasurements", "Windows");
	scrMenuMgr->addMenuItemString("toolsPreflightVerifier", "Windows");
	scrMenuMgr->addMenuItemString("toolsDocumentLog", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemString("toolsToolbarTools", "Windows");
	scrMenuMgr->addMenuItemString("toolsToolbarPDF", "Windows");
	scrMenuMgr->addMenuItemString("toolsToolbarView", "Windows");
	scrMenuMgr->addMenuItemString("toolsToolbarFile", "Windows");
	scrMenuMgr->addMenuItemString("toolsToolbarEdit", "Windows");
	scrMenuMgr->addMenuItemString("SEPARATOR", "Windows");
	scrMenuMgr->addMenuItemStringsToMenuBar("Windows", scrActions);
}


void ScribusMainWindow::initStatusBar()
{
	QFont fo(font());
	int posi = fo.pointSize() - (ScCore->isWinGUI() ? 1 : 2);
	fo.setPointSize(posi);
	unitSwitcher = new QComboBox( this );
	unitSwitcher->setObjectName("unitSwitcher");
	unitSwitcher->setFocusPolicy(Qt::NoFocus);
	unitSwitcher->setFont(fo);
	int maxUindex = unitGetMaxIndex() - 2;
	for (int i = 0; i <= maxUindex; ++i)
		unitSwitcher->addItem(unitGetStrFromIndex(i));


	QWidget* zoomWidget = new QWidget( statusBar() );
	QHBoxLayout* zoomLayout = new QHBoxLayout( zoomWidget );
	zoomLayout->setContentsMargins(0, 0, 0, 0);
	zoomLayout->setSpacing(3);

	zoomSpinBox = new ScrSpinBox( 1, 32000, zoomWidget, 6 );
	zoomSpinBox->setFont(fo);
	zoomSpinBox->setValue( 100 );
	zoomSpinBox->setSingleStep(10);
	zoomSpinBox->setFocusPolicy(Qt::ClickFocus);
	zoomSpinBox->setSuffix( tr( " %" ) );
	layerMenu = new QComboBox( this );
	layerMenu->setObjectName("layerMenu");
	layerMenu->setEditable(false);
	layerMenu->setFont(fo);
	layerMenu->setFocusPolicy(Qt::NoFocus);
	layerMenu->setSizeAdjustPolicy(QComboBox::AdjustToContents);

	pageSelector = new PageSelector(this, 1);
	pageSelector->setObjectName("pageSelector");
	pageSelector->setFont(fo);
	pageSelector->setFocusPolicy(Qt::ClickFocus);

	zoomDefaultToolbarButton = new QPushButton(zoomWidget);
	zoomDefaultToolbarButton->setFocusPolicy(Qt::NoFocus);
	zoomDefaultToolbarButton->setDefault( false );
	zoomDefaultToolbarButton->setAutoDefault( false );
	zoomOutToolbarButton = new QPushButton(zoomWidget);
	zoomOutToolbarButton->setFocusPolicy(Qt::NoFocus);
	zoomOutToolbarButton->setDefault( false );
	zoomOutToolbarButton->setAutoDefault( false );
	zoomInToolbarButton = new QPushButton(zoomWidget);
	zoomInToolbarButton->setFocusPolicy(Qt::NoFocus);
	zoomInToolbarButton->setDefault( false );
	zoomInToolbarButton->setAutoDefault( false );

	zoomDefaultToolbarButton->setIcon(IconManager::instance().loadIcon("zoom-original"));
	zoomOutToolbarButton->setIcon(IconManager::instance().loadIcon("zoom-out"));
	zoomInToolbarButton->setIcon(IconManager::instance().loadIcon("zoom-in"));

	zoomLayout->addWidget( zoomSpinBox );
	zoomLayout->addWidget( zoomOutToolbarButton );
	zoomLayout->addWidget( zoomDefaultToolbarButton );
	zoomLayout->addWidget( zoomInToolbarButton );

	m_mainWindowStatusLabel = new QLabel( "           ", statusBar());
	m_mainWindowStatusLabel->setFont(fo);
	mainWindowProgressBar = new QProgressBar(statusBar());
	mainWindowProgressBar->setAlignment(Qt::AlignHCenter);
	mainWindowProgressBar->setFixedWidth( 100 );
	mainWindowProgressBar->reset();
	mainWindowXPosLabel = new QLabel( tr("X:"), statusBar());
	mainWindowXPosLabel->setFont(fo);
	mainWindowYPosLabel = new QLabel( tr("Y:"), statusBar());
	mainWindowYPosLabel->setFont(fo);
	mainWindowXPosDataLabel = new QLabel(QString(), statusBar());
	mainWindowXPosDataLabel->setFont(fo);
	mainWindowYPosDataLabel = new QLabel(QString(), statusBar());
	mainWindowYPosDataLabel->setFont(fo);
	mainWindowXPosDataLabel->setMinimumWidth(mainWindowXPosDataLabel->fontMetrics().horizontalAdvance("99999.999"));
	mainWindowYPosDataLabel->setMinimumWidth(mainWindowYPosDataLabel->fontMetrics().horizontalAdvance("99999.999"));
	statusBarLanguageChange();

	layerMenu->setObjectName("layerMenu");
	unitSwitcher->setObjectName("unitSwitcher");
	zoomDefaultToolbarButton->setObjectName("zoomDefaultToolbarButton");
	zoomInToolbarButton->setObjectName("zoomInToolbarButton");
	zoomOutToolbarButton->setObjectName("zoomOutToolbarButton");
	zoomSpinBox->setObjectName("zoomSpinBox");

	statusBar()->setFont(fo);
	statusBar()->addPermanentWidget(m_mainWindowStatusLabel, 5);
	QLabel *s = new QLabel(QString());
	QLabel *s2 = new QLabel(QString());
	QLabel *s3 = new QLabel(QString());
	statusBar()->addPermanentWidget(s,1);
	statusBar()->addPermanentWidget(s2,1);
	statusBar()->addPermanentWidget(zoomWidget,0);
	statusBar()->addPermanentWidget(pageSelector,0);
	statusBar()->addPermanentWidget(layerMenu,1);
	statusBar()->addPermanentWidget(s3,3);
	statusBar()->addPermanentWidget(mainWindowXPosLabel, 0);
	statusBar()->addPermanentWidget(mainWindowXPosDataLabel, 0);
	statusBar()->addPermanentWidget(mainWindowYPosLabel, 0);
	statusBar()->addPermanentWidget(mainWindowYPosDataLabel, 0);

	statusBar()->addPermanentWidget(unitSwitcher,0);
	statusBar()->addPermanentWidget(mainWindowProgressBar, 0);
	connect(statusBar(), SIGNAL(messageChanged(QString)), this, SLOT(setTempStatusBarText(QString)));

}


void ScribusMainWindow::setStatusBarMousePosition(double xp, double yp)
{
	if (!HaveDoc)
	{
		mainWindowXPosDataLabel->clear();
		mainWindowYPosDataLabel->clear();
		return;
	}
	if (doc->Pages->isEmpty())
		return;
	double xn = xp;
	double yn = yp;
	if (doc->guidesPrefs().rulerMode)
	{
		xn -= doc->currentPage()->xOffset();
		yn -= doc->currentPage()->yOffset();
	}
	xn -= doc->rulerXoffset;
	yn -= doc->rulerYoffset;
	mainWindowXPosDataLabel->setText(value2String(xn, doc->unitIndex(), true, false));
	mainWindowYPosDataLabel->setText(value2String(yn, doc->unitIndex(), true, false));
}

void ScribusMainWindow::setStatusBarTextPosition(double base, double xp)
{
	if (doc->Pages->isEmpty())
		return;
	mainWindowXPosDataLabel->setText(base + xp >= 0? value2String(xp, doc->unitIndex(), true, true): QString("-"));
	mainWindowYPosDataLabel->setText("-");
}

void ScribusMainWindow::setStatusBarTextSelectedItemInfo()
{
	const int docSelectionCount = doc->m_Selection->count();
	if (docSelectionCount == 0)
	{
		setStatusBarInfoText("");
		return;
	}
	QString widthTxt = value2String(doc->m_Selection->width(), doc->unitIndex(), true, true);
	QString heightTxt = value2String(doc->m_Selection->height(), doc->unitIndex(), true, true);
	if (docSelectionCount == 1)
	{
		QString whatSel = tr("Unknown");
		switch (doc->m_Selection->itemAt(0)->itemType())
		{
			case 2:
				whatSel = CommonStrings::itemType_ImageFrame;
				break;
			case 4:
				whatSel = CommonStrings::itemType_TextFrame;
				break;
			case 5:
				whatSel = CommonStrings::itemType_Line;
				break;
			case 6:
				whatSel = CommonStrings::itemType_Polygon;
				break;
			case 7:
				whatSel = CommonStrings::itemType_Polyline;
				break;
			case 8:
				whatSel = CommonStrings::itemType_PathText;
				break;
			case 9:
				whatSel = CommonStrings::itemType_LatexFrame;
				break;
			case 11:
				whatSel = CommonStrings::itemType_Symbol;
				break;
			case 12:
				whatSel = CommonStrings::itemType_Group;
				break;
			case 13:
				whatSel = CommonStrings::itemType_RegularPolygon;
				break;
			case 14:
				whatSel = CommonStrings::itemType_Arc;
				break;
			case 15:
				whatSel = CommonStrings::itemType_Spiral;
				break;
			case 16:
				whatSel = CommonStrings::itemType_Table;
				break;
			default:
				whatSel = "Unknown";
				break;
		}
		QString txtBody = tr("%1 selected").arg(whatSel) + " : " + tr("Size");
		setStatusBarInfoText( QString("%1 = %3 x %4").arg(txtBody, widthTxt, heightTxt));
	}
	else
	{
		setStatusBarInfoText( tr("%1 Objects selected, Selection Size = %2 x %3").arg(docSelectionCount).arg(widthTxt, heightTxt));
	}
}

void ScribusMainWindow::setTempStatusBarText(const QString &text)
{
	if (m_mainWindowStatusLabel)
	{
		if (text.isEmpty())
			m_mainWindowStatusLabel->setText(m_statusLabelText);
		else
			m_mainWindowStatusLabel->setText(text);
	}
}

void ScribusMainWindow::setStatusBarInfoText(const QString & newText)
{
	if (m_mainWindowStatusLabel)
		m_mainWindowStatusLabel->setText(newText);
	m_statusLabelText = newText;
}


//AV to be replaced with Selection::update and listener in contentPalette
void ScribusMainWindow::setTBvals(PageItem *currItem)
{
	scrActions["editMark"]->setEnabled(false);
	
	PageItem* item  = currItem;
	bool inEditMode = (doc->appMode == modeEdit);
	if (doc->appMode == modeEditTable)
	{
		if (currItem->isTable())
			item = currItem->asTable()->activeCell().textFrame();
		inEditMode = item->isTextFrame();
	}
	if (!item || item->itemText.length() <= 0)
		return;

	const ParagraphStyle& currPStyle(inEditMode ? item->currentStyle() : item->itemText.defaultStyle());
	setAlignmentValue(currPStyle.alignment());

	// Assignment operator does not perform style context assignment
	// Do it in this case, otherwise we might get some crashes if previous
	// text object was deleted or things like that
	const ParagraphStyle& curStyle = item->currentStyle();
	doc->currentStyle.setContext(curStyle.context());
	doc->currentStyle = curStyle;
	if (doc->appMode == modeEdit || doc->appMode == modeEditTable)
		item->currentTextProps(doc->currentStyle);
	else
		doc->currentStyle.charStyle().setStyle(item->currentCharStyle());
	emit TextStyle(doc->currentStyle);
	// to go: (av)
	contentPalette->update(doc->currentStyle);
	if (paragraphStylesPanelTabs)
		paragraphStylesPanelTabs->syncCurrentStyle();
	//check if mark in cursor place and enable editMark action
	if (doc->appMode == modeEdit && item->itemText.cursorPosition() < item->itemText.length())
	{
		if (item->itemText.hasMark(item->itemText.cursorPosition()))
		{
			Mark* mark = item->itemText.mark(item->itemText.cursorPosition());
			scrActions["editMark"]->setEnabled(true);
			if ((mark->isType(MARKNoteMasterType) || mark->isType(MARKNoteFrameType)) && (mark->getNotePtr() != nullptr))
				nsEditor->setNotesStyle(mark->getNotePtr()->notesStyle());
		}
		else
			scrActions["editMark"]->setEnabled(false);
	}
}

//Special keys assigned to actions are stolen by the action and not passed to
//keyPressEvent so process them here.
void ScribusMainWindow::specialActionKeyEvent(int unicodevalue)
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->count() != 1)
		return;
	if ((doc->appMode != modeEdit) && (doc->appMode != modeEditTable))
		return;

	PageItem* selItem = doc->m_Selection->itemAt(0);
	if (!selItem->isTextFrame() && !selItem->isTable())
		return;

	PageItem_TextFrame *currItem;
	if (doc->appMode == modeEditTable)
		currItem = selItem->asTable()->activeCell().textFrame();
	else
		currItem = selItem->asTextFrame();
	if (currItem == nullptr)
		return;

	if (unicodevalue!=-1)
	{
		UndoTransaction activeTransaction;
		if (currItem->HasSel)
		{
			if (UndoManager::undoEnabled())
				activeTransaction = m_undoManager->beginTransaction(Um::Selection, Um::IGroup, Um::ReplaceText, QString(), Um::IDelete);
			currItem->deleteSelectedTextFromFrame();
		}
		if (UndoManager::undoEnabled())
		{
			auto *ss = dynamic_cast<SimpleState*>(m_undoManager->getLastUndo());
			UndoObject *undoTarget = currItem;
			if (ss && (ss->get("ETEA") == "insert_frametext") && (ss->undoObject() == undoTarget))
				ss->set("TEXT_STR", ss->get("TEXT_STR") + QString(QChar(unicodevalue)));
			else
			{
				ss = new SimpleState(Um::InsertText, QString(), Um::ICreate);
				ss->set("INSERT_FRAMETEXT");
				ss->set("ETEA", QString("insert_frametext"));
				ss->set("TEXT_STR", QString(QChar(unicodevalue)));
				ss->set("START", currItem->itemText.cursorPosition());
				if (currItem->isNoteFrame())
				{
					undoTarget = doc;
					ss->set("noteframeName", currItem->getUName());
				}
				m_undoManager->action(undoTarget, ss);
			}
		}
		currItem->itemText.insertChars(QString(QChar(unicodevalue)), true);
		if (activeTransaction)
			activeTransaction.commit();
	}
	else if (unicodevalue == SpecialChars::SHYPHEN.unicode()) //ignore the char as we use an attribute if the text item, for now.
	{
		// this code is currently dead since unicodeSoftHyphen
		// doesn't have unicodevalue == -1 any more
		if (currItem->itemText.cursorPosition() <= 1)
			return;
#if 0
		StyleFlag fl = currItem->itemText.item(qMax(currItem->CPos-1,0))->effects();
		fl |= ScStyle_HyphenationPossible;
		currItem->itemText.item(qMax(currItem->CPos-1,0))->setEffects(fl);
#else
		if (UndoManager::undoEnabled())
		{
			auto *ss = dynamic_cast<SimpleState*>(m_undoManager->getLastUndo());
			UndoObject *undoTarget = currItem;
			if (ss && (ss->get("ETEA") == "insert_frametext") && (ss->undoObject() == undoTarget))
				ss->set("TEXT_STR", ss->get("TEXT_STR") + QString(SpecialChars::SHYPHEN));
			else
			{
				ss = new SimpleState(Um::InsertText, QString(), Um::ICreate);
				ss->set("INSERT_FRAMETEXT");
				ss->set("ETEA", QString("insert_frametext"));
				ss->set("TEXT_STR", QString(SpecialChars::SHYPHEN));
				ss->set("START", currItem->itemText.cursorPosition());
				if (currItem->isNoteFrame())
				{
					undoTarget = doc;
					ss->set("noteframeName", currItem->getUName());
				}
				m_undoManager->action(undoTarget, ss);
			}
		}
		currItem->itemText.insertChars(QString(SpecialChars::SHYPHEN), true);
#endif
	}
	if (doc->appMode == modeEditTable)
		selItem->asTable()->update();
	else
		currItem->update();
}

bool ScribusMainWindow::eventFilter( QObject* /*o*/, QEvent *e )
{
	if (e->type() == QEvent::ToolTip)
		return (!m_prefsManager.appPrefs.displayPrefs.showToolTips);

	bool retVal = false;
	if (e->type() == QEvent::KeyPress)
	{
		QKeyEvent *k = dynamic_cast<QKeyEvent *>(e);
		if (!k)
			return false;
		int keyMod=0;
		if (k->modifiers() & Qt::ShiftModifier)
			keyMod |= Qt::SHIFT;
		if (k->modifiers() & Qt::ControlModifier)
			keyMod |= Qt::CTRL;
		if (k->modifiers() & Qt::AltModifier)
			keyMod |= Qt::ALT;

		QKeySequence currKeySeq(k->key() | keyMod);
		if (QString(currKeySeq.toString()).isNull())
			return false;
		retVal = true;
		//Palette actions
		/*if (actionManager->compareKeySeqToShortcut(currKeySeq, "specialToggleAllPalettes"))
			scrActions["specialToggleAllPalettes"]->activate(QAction::Trigger);
		else*/ if (actionManager->compareKeySeqToShortcut(currKeySeq, "specialToggleAllGuides"))
			scrActions["specialToggleAllGuides"]->activate(QAction::Trigger);
		else
			retVal = false;
	}
	else if (e->type() == QEvent::KeyRelease)
	{
		QKeyEvent *k = dynamic_cast<QKeyEvent *>(e);
		if (!k)
			return false;
#if defined(Q_OS_MACOS)
		if ((k->key() == Qt::Key_QuoteLeft) && (k->modifiers() & Qt::ControlModifier))
		{
			if (k->modifiers() & Qt::ShiftModifier)
				mdiArea->activatePreviousSubWindow();
			else
				mdiArea->activateNextSubWindow();
			retVal = true;
		}
#endif
	}
	//Return false to pass event to object
	return retVal;
}


void ScribusMainWindow::inputMethodEvent ( QInputMethodEvent * event )
{
	//qDebug() << "IMEmw" << event->commitString() << event->preeditString() << "attributes:" << event->attributes().count();
}

QVariant ScribusMainWindow::inputMethodQuery ( Qt::InputMethodQuery query ) const
{
	//qDebug() << "IMQmw" << query;
	return QVariant();
}

//AV -> CanvasMode
void ScribusMainWindow::keyPressEvent(QKeyEvent *k)
{
	if (HaveDoc && view && (view->hasFocus() || view->widget()->hasFocus()))
	{
		view->canvasMode()->keyPressEvent(k);
		return;
	}

	QMainWindow::keyPressEvent(k);
}

void ScribusMainWindow::keyReleaseEvent(QKeyEvent *k)
{
	if (HaveDoc && view && (view->hasFocus() || view->widget()->hasFocus()))
	{
		view->canvasMode()->keyReleaseEvent(k);
		return;
	}

	QMainWindow::keyReleaseEvent(k);
}

void ScribusMainWindow::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange)
		languageChange();
	else
		QMainWindow::changeEvent(e);
}

void ScribusMainWindow::closeEvent(QCloseEvent *ce)
{
	//Do not quit if Preferences or new doc window is open
	PreferencesDialog *prefsDialog = findChild<PreferencesDialog *>(QString::fromLocal8Bit("PreferencesDialog"));
	NewDocDialog *newDocWin = findChild<NewDocDialog *>(QString::fromLocal8Bit("NewDocumentWindow"));
	if (prefsDialog != nullptr || newDocWin != nullptr)
	{
		ce->ignore();
		return;
	}

	disconnect(mdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(newActWin(QMdiSubWindow*)));

	QList<QMdiSubWindow *> windows = mdiArea->subWindowList();
	if (!windows.isEmpty())
	{
		ScribusWin* tw { nullptr };
		int windowCount = windows.count();
		for ( int i = 0; i < windowCount; ++i )
		{
			QMdiSubWindow *tws = windows.at(i);
			ScribusWin* scw = dynamic_cast<ScribusWin *>(tws->widget());
			if (!scw)
				continue;

			newActWin(windows.at(i));
			tw = ActWin;
			slotSelect();
			tws->close();
			if (tw == ActWin)
			{
				ce->ignore();
				connect(mdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(newActWin(QMdiSubWindow*)));
				return;
			}
		}
	}
	fileToolBar->connectPrefsSlot(false);
	editToolBar->connectPrefsSlot(false);
	modeToolBar->connectPrefsSlot(false);
	pdfToolBar->connectPrefsSlot(false);

	// if palettes are temporary hidden restore them before saving the workspace
	dockManager->restoreHiddenWorkspace();

	// Save GUI state
	dockManager->saveWorkspaceToPrefs();

	// We need to remove all docks from the DockManager to prevent a memory leak
	// in case a plugin has added a dock in the DockManager and is simply deleted
	// on cleanup before it is removed from the DockManager.
	// Removed Docks are not deleted directly.
	dockManager->removeAllDockWidgets();


	// Every document window is closed by now; drop any lock still held
	// (e.g. one whose document was closed some other way).
	DocumentLock::releaseAll();

	// Clean up plugins, THEN save prefs to disk
	ScCore->pluginManager->cleanupPlugins();
	if (!m_prefsManager.appPrefs.scrapbookPrefs.persistentScrapbook)
		scrapbookPalette->cleanUpTemp();
	m_prefsManager.appPrefs.scrapbookPrefs.RecentScrapbooks.clear();
	m_prefsManager.appPrefs.scrapbookPrefs.RecentScrapbooks = scrapbookPalette->getOpenScrapbooks();
	for (const QString& sb : std::as_const(s_skippedScrapbooks))
	{
		if (!m_prefsManager.appPrefs.scrapbookPrefs.RecentScrapbooks.contains(sb))
			m_prefsManager.appPrefs.scrapbookPrefs.RecentScrapbooks << sb;
	}
	if (!emergencyActivated)
		m_prefsManager.savePrefs();
	UndoManager::deleteInstance();
	FormatsManager::deleteInstance();
//	qApp->changeOverrideCursor(QCursor(Qt::ArrowCursor));
	ce->accept();
}

void ScribusMainWindow::requestUpdate(int val)
{
	emit UpdateRequest(val);
}

/////////////////////////////////////////////////////////////////////
// SLOT IMPLEMENTATION
/////////////////////////////////////////////////////////////////////

/*
bool ScribusMainWindow::arrowKeyDown()
{
	return m_arrowKeyDown;
}
*/

QStringList ScribusMainWindow::findRecoverableFile()
{
	QSet<QString> foundFiles;
	QDir::SortFlags sortflags = QDir::Name | QDir::Time;
	QDir::Filters filterflags = QDir::Files | QDir::NoSymLinks | QDir::NoDot | QDir::NoDotDot;
	const bool autoSaveDirOk = NetPathGuard::reachable(m_prefsManager.appPrefs.docSetupPrefs.AutoSaveDir);
	const bool documentDirOk = NetPathGuard::reachable(m_prefsManager.documentDir());
	if (!m_prefsManager.appPrefs.docSetupPrefs.AutoSaveDir.isEmpty() && autoSaveDirOk)
	{
		QDir dirAuto(m_prefsManager.appPrefs.docSetupPrefs.AutoSaveDir, "*_emergency_*.sla", sortflags, filterflags);
		QFileInfoList aList = dirAuto.entryInfoList();
		if (aList.count() > 0)
		{
			for (int i = 0; i < aList.count(); i++)
				foundFiles.insert(aList[i].absoluteFilePath());
		}
		QDir dirAuto2(m_prefsManager.appPrefs.docSetupPrefs.AutoSaveDir, "*_autosave_*.sla", sortflags, filterflags);
		QFileInfoList aList2 = dirAuto2.entryInfoList();
		if (aList2.count() > 0)
		{
			for (int i = 0; i < aList2.count(); i++)
				foundFiles.insert(aList2[i].absoluteFilePath());
		}
	}
	if (documentDirOk)
	{
		QDir dirDoc(m_prefsManager.documentDir(), "*_emergency_*.sla", sortflags, filterflags);
		QFileInfoList dList = dirDoc.entryInfoList();
		for (int i = 0; i < dList.count(); i++)
			foundFiles.insert(dList[i].absoluteFilePath());

		QDir dirDoc2(m_prefsManager.documentDir(), "*_autosave_*.sla", sortflags, filterflags);
		QFileInfoList dList2 = dirDoc2.entryInfoList();
		for (int i = 0; i < dList2.count(); i++)
			foundFiles.insert(dList2[i].absoluteFilePath());
	}

	QDir dirHome(QDir::toNativeSeparators(QDir::homePath()), "*_emergency_*.sla", sortflags, filterflags);
	QFileInfoList hList = dirHome.entryInfoList();
	for (int i = 0; i < hList.count(); i++)
		foundFiles.insert(hList[i].absoluteFilePath());

	QDir dirHome2(QDir::toNativeSeparators(QDir::homePath()), "*_autosave_*.sla", sortflags, filterflags);
	QFileInfoList hList2 = dirHome2.entryInfoList();
	for (int i = 0; i < hList2.count(); i++)
		foundFiles.insert(hList2[i].absoluteFilePath());

	return foundFiles.values();
}

bool ScribusMainWindow::recoverFile(const QStringList& foundFiles)
{
	appModeHelper->setStartupActionsEnabled(false);

	QScopedPointer<RecoverDialog> dia(new RecoverDialog(this, foundFiles));
	if (!dia->exec())
		return false;
	if (dia->recoverFiles.isEmpty())
		return false;

	for (int i = 0; i < dia->recoverFiles.count(); ++i)
	{
		bool docLoaded = loadDoc(dia->recoverFiles[i]);
		if (!docLoaded)
			continue;
		doc->setDocumentFileName(dia->recoverNames[i]);
		doc->hasName = true;
		updateActiveWindowCaption(doc->documentFileName());
		outlinePalette->setDoc(doc);
		if (outlinePalette->isVisible())
			outlinePalette->BuildTree();
	}

	return true;
}

void ScribusMainWindow::startUpDialog()
{
	bool docSet = false;
	PrefsContext* docContext = m_prefsManager.prefsFile->getContext("docdirs", false);
	NewDocDialog* dia = new NewDocDialog(this, m_recentDocsList, true, ScCore->getGuiLanguage());
	if (dia->exec())
	{
		if (dia->tabSelected() == NewDocDialog::NewDocumentTab)
		{
			int facingPages = dia->choosenLayout();
			int firstPage = dia->layoutFirstPage();
			docSet = dia->startDocSetup->isChecked();
			double topMargin = dia->marginGroup->margins().top();
			double bottomMargin = dia->marginGroup->margins().bottom();
			double leftMargin = dia->marginGroup->margins().left();
			double rightMargin = dia->marginGroup->margins().right();
			double columnDistance = dia->distance();
			double pageWidth = dia->pageWidth();
			double pageHeight = dia->pageHeight();
			double numberCols = dia->numberOfCols->value();
			bool autoframes = dia->autoTextFrame->isChecked();
			int orientation = dia->orientation();
			int pageCount = dia->pageCountSpinBox->value();
			QString pagesize = dia->pageSizeName();
			doFileNew(pageWidth, pageHeight, topMargin, leftMargin, rightMargin, bottomMargin, columnDistance, numberCols, autoframes, facingPages, dia->unitOfMeasureComboBox->currentIndex(), firstPage, orientation, 1, pagesize, true, pageCount, true, dia->marginGroup->marginPreset());
			doc->setPageSetFirstPage(facingPages, firstPage);
			doc->bleeds()->set(dia->bleedTop(), dia->bleedLeft(), dia->bleedBottom(), dia->bleedRight());
			HaveNewDoc();
			doc->reformPages(true);
			// Don's disturb user with "save?" dialog just after new doc
			// doc changing should be rewritten maybe... maybe later...
			doc->setModified(false);
			updateActiveWindowCaption(doc->documentFileName());
		}
		else if (dia->tabSelected() == NewDocDialog::NewFromTemplateTab)
		{
			QString fileName = QDir::cleanPath(dia->selectedFile());
			// A template is read, not opened for editing: no lock on it (see LockMode).
			if (!fileName.isEmpty() && loadDoc(fileName, AlreadyOpenAction::InformAndSwitch, LockMode::NoLock))
			{
				doc->hasName = false;
				UndoManager::instance()->renameStack(dia->nftGui->currentDocumentTemplate->name);
				doc->setDocumentFileName(dia->nftGui->currentDocumentTemplate->name);
				updateActiveWindowCaption(QObject::tr("Document Template: ") + dia->nftGui->currentDocumentTemplate->name);
				QDir::setCurrent(PrefsManager::instance().documentDir());
				removeRecent(fileName);
			}
		}
		else if (dia->tabSelected() == NewDocDialog::OpenExistingTab)
		{
			QString fileName = dia->selectedFile();
			if (!fileName.isEmpty())
			{
				QFileInfo fi(fileName);
				docContext->set("docsopen", fi.absolutePath());
				loadDoc(fileName);
			}
		}
		else // NewDocDialog::OpenRecentTab
		{
			QString fileName = dia->selectedFile();
			if (!fileName.isEmpty())
				loadRecent(fileName);
		}
	}
	else
	{
		appModeHelper->setStartupActionsEnabled(false);
	}
	m_prefsManager.setShowStartupDialog(!dia->startUpDialog->isChecked());
	delete dia;
	m_mainWindowStatusLabel->setText( tr("Ready"));
	if (docSet)
		slotDocSetup();
}

bool ScribusMainWindow::slotFileNew()
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);

	bool retVal = false;
	QScopedPointer<NewDocDialog> dia(new NewDocDialog(this, m_recentDocsList));
	if (!dia->exec())
		return false;

	int facingPages = dia->choosenLayout();
	int firstPage = dia->layoutFirstPage();
	bool docSet = dia->startDocSetup->isChecked();
	double topMargin = dia->marginGroup->margins().top();
	double bottomMargin = dia->marginGroup->margins().bottom();
	double leftMargin = dia->marginGroup->margins().left();
	double rightMargin = dia->marginGroup->margins().right();
	double columnDistance = dia->distance();
	double pageWidth = dia->pageWidth();
	double pageHeight = dia->pageHeight();
	double numberCols = dia->numberOfCols->value();
	bool autoframes = dia->autoTextFrame->isChecked();
	int orientation = dia->orientation();
	int pageCount = dia->pageCountSpinBox->value();
	QString pagesize = dia->pageSizeName();

	if (doFileNew(pageWidth, pageHeight, topMargin, leftMargin, rightMargin, bottomMargin, columnDistance, numberCols, autoframes, facingPages, dia->unitOfMeasureComboBox->currentIndex(), firstPage, orientation, 1, pagesize, true, pageCount, true, dia->marginGroup->marginPreset()))
	{
		doc->setPageSetFirstPage(facingPages, firstPage);
		doc->bleeds()->set(dia->bleedTop(), dia->bleedLeft(), dia->bleedBottom(), dia->bleedRight());
		m_mainWindowStatusLabel->setText( tr("Ready"));
		HaveNewDoc();
		doc->reformPages(true);
		retVal = true;
		// Don't disturb user with "save?" dialog just after new doc
		// doc changing should be rewritten maybe... maybe later...
		doc->setModified(false);
		updateActiveWindowCaption(doc->documentFileName());
	}

	if (docSet)
		slotDocSetup();
	return retVal;
}

//TODO move to core, assign doc to doc list, optionally create gui for it
ScribusDoc *ScribusMainWindow::newDoc(double width, double height, double topMargin, double leftMargin, double rightMargin, double bottomMargin, double columnDistance, double columnCount, bool autoTextFrames, int pageArrangement, int unitIndex, int firstPageLocation, int orientation, int firstPageNumber, const QString& defaultPageSize, bool requiresGUI, int pageCount, bool showView, int marginPreset)
{
	return doFileNew(width, height, topMargin, leftMargin, rightMargin, bottomMargin, columnDistance, columnCount, autoTextFrames, pageArrangement, unitIndex, firstPageLocation, orientation, firstPageNumber, defaultPageSize, requiresGUI, pageCount, showView, marginPreset);
}

ScribusDoc *ScribusMainWindow::doFileNew(double width, double height, double topMargin, double leftMargin, double rightMargin, double bottomMargin, double columnDistance, double columnCount, bool autoTextFrames, int pageArrangement, int unitIndex, int firstPageLocation, int orientation, int firstPageNumber, const QString& defaultPageSize, bool requiresGUI, int pageCount, bool showView, int marginPreset)
{
	if (HaveDoc)
		outlinePalette->buildReopenVals();
	m_undoManager->setUndoEnabled(false);
	MarginStruct margins(topMargin, leftMargin, bottomMargin, rightMargin);
	DocPagesSetup pagesSetup(pageArrangement, firstPageLocation, firstPageNumber, orientation, autoTextFrames, columnDistance, columnCount);
	QString newDocName( tr("Document")+"-"+QString::number(m_DocNr));
	ScribusDoc *tempDoc = new ScribusDoc();
	if (requiresGUI)
		doc = tempDoc;
	tempDoc->setLoading(true);
	outlinePalette->setDoc(tempDoc);
	ColorSetManager csm;
	csm.findPaletteLocations();
	csm.findPalettes();
	csm.findUserPalettes();
	ColorList colorList;
	QHash<QString, VGradient> gradientsList;
	QHash<QString, ScPattern> patternsList;
	QString Cpfad = QDir::toNativeSeparators(ScPaths::applicationDataDir())+"DefaultColors.xml";
	QFile fc(Cpfad);
	if (fc.exists())
	{
		csm.loadPalette(Cpfad, doc, colorList, gradientsList, patternsList, false);
		doc->PageColors = colorList;
		doc->docGradients = gradientsList;
		doc->docPatterns = patternsList;
	}
	else
	{
		if (m_prefsManager.appPrefs.colorPrefs.DColorSet != "Scribus Small")
		{
			QStringList CustomColorSets = csm.userPaletteNames();
			if (CustomColorSets.contains(m_prefsManager.appPrefs.colorPrefs.DColorSet))
				Cpfad = csm.userPaletteFileFromName(m_prefsManager.appPrefs.colorPrefs.DColorSet);
			else
				Cpfad = csm.paletteFileFromName(m_prefsManager.appPrefs.colorPrefs.DColorSet);
			if (!Cpfad.isEmpty())
				csm.loadPalette(Cpfad, doc, colorList, gradientsList, patternsList, false);
			doc->PageColors = colorList;
			doc->docGradients = gradientsList;
			doc->docPatterns = patternsList;
		}
		else
			doc->PageColors = m_prefsManager.appPrefs.colorPrefs.DColors;
	}
	tempDoc->PageColors.ensureDefaultColors();
	tempDoc->setup(unitIndex, pageArrangement, firstPageLocation, orientation, firstPageNumber, defaultPageSize, newDocName);
	if (requiresGUI)
	{
		HaveDoc++;
		m_DocNr++;
	}
	if (ScCore->haveCMS() && tempDoc->cmsSettings().CMSinUse)
		recalcColors();
	//CB NOTE should be all done now
	tempDoc->setPage(width, height, topMargin, leftMargin, rightMargin, bottomMargin, columnCount, columnDistance, autoTextFrames, pageArrangement);
	tempDoc->setMarginPreset(marginPreset);
	tempDoc->setMasterPageMode(false);
	tempDoc->createDefaultMasterPages();
	tempDoc->createNewDocPages(pageCount);
	tempDoc->addSection();
	tempDoc->setFirstSectionFromFirstPageNumber();
	tempDoc->setModified(false);
	tempDoc->OpenNodes.clear();
	if (requiresGUI)
	{
		actionManager->disconnectNewDocActions();
		actionManager->connectNewDocActions(tempDoc);
	}
	//<<View and window code
	QMdiArea* qwsp = nullptr;
	if (requiresGUI)
		qwsp = mdiArea;

	ScribusWin* w = new ScribusWin(qwsp, tempDoc);
	w->setMainWindow(this);
	if (requiresGUI && view != nullptr)
		actionManager->disconnectNewViewActions();

	ScribusView* tempView = new ScribusView(w, this, tempDoc);
	if (requiresGUI)
		view = tempView;
	tempDoc->setCurrentPage(tempDoc->Pages->at(0));
	tempDoc->setGUI(requiresGUI, this, tempView);
	if (requiresGUI)
	{
		tempDoc->createHyphenator();
		tempDoc->docHyphenator->ignoredWords = m_prefsManager.appPrefs.hyphPrefs.ignoredWords;
		tempDoc->docHyphenator->specialWords = m_prefsManager.appPrefs.hyphPrefs.specialWords;
	}
	tempDoc->setLoading(false);
	//run after setGUI to set up guidepalette ok

	tempView->setScale(m_prefsManager.displayScale());
	if (requiresGUI)
	{
		//done in newactinw actionManager->connectNewViewActions(tempView);
		alignDistributePalette->setDoc(tempDoc);
		docCheckerPalette->clearErrorList();
		symbolPalette->setDoc(tempDoc);
		inlinePalette->setDoc(tempDoc);
	}
	w->setView(tempView);
	ActWin = w;
	tempDoc->WinHan = w;
	if (requiresGUI)
		tempDoc->connectDocSignals(); //Must be before the first reformpages
	tempView->reformPages(true);
	//>>
	if (requiresGUI)
	{
		w->setSubWin(mdiArea->addSubWindow(w));
		suneerStripMdiCloseKey(w->getSubWin());
	}
	//Independent finishing tasks after tempDoc setup
	if (showView)
	{
		if (mdiArea->subWindowList().count() == 1)
			w->showMaximized();
		else
			w->show();
		tempView->show();
		// Seems to fix crash on loading
		ActWin = nullptr;
		newActWin(w->getSubWin());
	}
	if (requiresGUI)
	{
		connect(ScCore->fileWatcher, SIGNAL(fileChanged(QString)), tempDoc, SLOT(updatePict(QString)));
		connect(ScCore->fileWatcher, SIGNAL(fileDeleted(QString)), tempDoc, SLOT(removePict(QString)));
		connect(ScCore->fileWatcher, SIGNAL(dirChanged(QString)), tempDoc, SLOT(updatePictDir(QString)));
		connect(doc, SIGNAL(updateAutoSaveClock()), view->clockLabel, SLOT(resetTime()));
		view->clockLabel->resetTime();
		scrActions["viewToggleCMS"]->setChecked(tempDoc->HasCMS);
		m_undoManager->switchStack(tempDoc->documentFileName());
		m_styleManager->setDoc(tempDoc);
		marksManager->setDoc(tempDoc);
		nsEditor->setDoc(tempDoc);
		m_tocGenerator->setDoc(tempDoc);
	}
	m_undoManager->setUndoEnabled(true);
	return tempDoc;
}

void ScribusMainWindow::newFileFromTemplate()
{
	nftdialog* nftdia = new nftdialog(this, ScCore->getGuiLanguage());
	if (nftdia->exec() && nftdia->isTemplateSelected())
	{
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		nfttemplate* currentTemplate = nftdia->currentTemplate();
		// A template is read, not opened for editing: no lock on it (see LockMode).
		if (loadDoc(QDir::cleanPath(currentTemplate->file), AlreadyOpenAction::InformAndSwitch, LockMode::NoLock))
		{
			doc->hasName = false;
			UndoManager::instance()->renameStack(currentTemplate->name);
			doc->setDocumentFileName(currentTemplate->name);
			updateActiveWindowCaption(QObject::tr("Document Template: ") + currentTemplate->name);
			QDir::setCurrent(PrefsManager::instance().documentDir());
			removeRecent(QDir::cleanPath(currentTemplate->file));
		}
		QApplication::restoreOverrideCursor();
	}
	delete nftdia;
}

void ScribusMainWindow::newView()
{
	ScribusWin* w = new ScribusWin(mdiArea, doc);
	w->setMainWindow(this);
	view = new ScribusView(w, this, doc);
	view->setScale(m_prefsManager.displayScale());
	w->setView(view);
	ActWin = w;
	w->setCentralWidget(view);
	actionManager->connectNewViewActions(view);
	alignDistributePalette->setDoc(doc);
	connect(m_undoManager, SIGNAL(undoRedoDone()), view, SLOT(DrawNew()));
	view->show();
}

void ScribusMainWindow::windowsMenuAboutToShow()
{
	if (!scrWindowsActions.isEmpty())
	{
		for (auto it = scrWindowsActions.begin(); it != scrWindowsActions.end(); ++it)
		{
			scrMenuMgr->removeMenuItem(it.key(), it.value(), "Windows");
		}
		scrWindowsActions.clear();
	}
	QList<QMdiSubWindow *> windows = mdiArea->subWindowList();
	bool windowsListNotEmpty=!windows.isEmpty();
	scrActions["windowsCascade"]->setEnabled(windowsListNotEmpty);
	scrActions["windowsTile"]->setEnabled(windowsListNotEmpty);
	if (!windowsListNotEmpty)
		return;
	int windowCount = static_cast<int>(windows.count());
	for ( int i = 0; i < windowCount; ++i )
	{
		QString docInWindow(windows.at(i)->windowTitle());
		scrWindowsActions.insert(docInWindow, new ScrAction(ScrAction::Window, QString(), QString(), QString("&%1 %2").arg(i + 1).arg(docInWindow), QKeySequence(), this, i));
		scrWindowsActions[docInWindow]->setToggleAction(true);
		connect( scrWindowsActions[docInWindow], SIGNAL(triggeredData(int)), this, SLOT(windowsMenuActivated(int)) );
		scrWindowsActions[docInWindow]->setChecked(mdiArea->activeSubWindow() == windows.at(i));
		scrMenuMgr->addMenuItemString(docInWindow, "Windows");
	}
	if (windowCount>1)
		scrMenuMgr->addMenuItemStringsToRememberedMenu("Windows", scrWindowsActions);
}

void ScribusMainWindow::extrasMenuAboutToShow()
{
	// There is only Picture Manager handled now.
	// As it can be opened all the time of the document life.
	// This is only check for availability of any ImageFrame
	// in the doc.
	bool enablePicManager = false;
	if (HaveDoc)
	{
		QList<PageItem*> allItems;
		for (int i = 0; i < doc->Items->count(); ++i)
		{
			PageItem *currItem = doc->Items->at(i);
			if (currItem->isGroup())
				allItems = currItem->getAllChildren();
			else
				allItems.append(currItem);
			for (int j = 0; j < allItems.count(); j++)
			{
				PageItem* item = allItems.at(j);
				if ((item->itemType() == PageItem::ImageFrame) && (!item->isLatexFrame()) && (!item->isOSGFrame()))
				{
					enablePicManager = true;
					break;
				}
			}
			allItems.clear();
		}
	}
	scrActions["extrasManageImages"]->setEnabled(enablePicManager);

	// Autoflow: only when a single, not-yet-forward-linked text frame that overflows is selected.
	bool enableAutoflow = false;
	if (HaveDoc && doc->m_Selection->count() == 1)
	{
		const PageItem* currItem = doc->m_Selection->itemAt(0);
		enableAutoflow = currItem && currItem->isTextFrame()
		              && currItem->nextInChain() == nullptr
		              && currItem->frameOverflows();
	}
	scrActions["extrasAutoflowToNewPages"]->setEnabled(enableAutoflow);
	// Document-scoped: available whenever a document is open, selection or not.
	scrActions["extrasFixOverflowFrames"]->setEnabled(HaveDoc);
	scrActions["extrasFixOverflowFramesDoc"]->setEnabled(HaveDoc);
}


void ScribusMainWindow::duplicateNewsCheck()
{
	// One panel for the session: closing it hides it, so the last results are
	// still there when it is opened again.
	static QPointer<SuneerDuplicateNewsDialog> panel;
	if (!panel)
		panel = new SuneerDuplicateNewsDialog(this);
	panel->show();
	panel->raise();
	panel->activateWindow();
}

void ScribusMainWindow::newActWin(QMdiSubWindow *w)
{
	if (w == nullptr)
	{
		if (mdiArea->subWindowList().isEmpty())
			ActWin = nullptr;
		return;
	}
	if (w->widget() == nullptr)
		return;
	ScribusWin* scw = dynamic_cast<ScribusWin *>(w->widget());
	if (!scw)
		return;
	if (scw && scw->doc())
	{
		if (!scw->doc()->hasGUI())
			return;
	}
	if (scw == ActWin)
		return;
	ActWin = scw;
	if (ActWin->doc() == nullptr)
		return;
	if (doc != nullptr)
	{
		if (doc->appMode == modeEditClip)
			view->requestMode(submodeEndNodeEdit);
		if (HaveDoc && (doc != ActWin->doc()))
			outlinePalette->buildReopenVals();
	}
	docCheckerPalette->clearErrorList();
	if (HaveDoc && (doc != nullptr) && doc->hasGUI())
	{
		disconnect(m_undoManager, SIGNAL(undoRedoBegin()), doc, SLOT(undoRedoBegin()));
		disconnect(m_undoManager, SIGNAL(undoRedoDone()) , doc, SLOT(undoRedoDone()));
		disconnect(m_undoManager, SIGNAL(undoRedoDone()) , doc->view(), SLOT(DrawNew()));
		disconnect(doc, SIGNAL(addBookmark(PageItem *)), this, SLOT(AddBookMark(PageItem *)));
		disconnect(doc, SIGNAL(deleteBookmark(PageItem *)), this, SLOT(DelBookMark(PageItem *)));
		disconnect(unitSwitcher, SIGNAL(activated(int)), doc->view(), SLOT(ChgUnit(int)));
		unitSwitcher->setEnabled(false);
		disconnect(zoomSpinBox, SIGNAL(valueChanged(double)), doc->view(), SLOT(setZoom()));
		zoomSpinBox->setEnabled(false);
		disconnect(zoomDefaultToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoom100()));
		zoomDefaultToolbarButton->setEnabled(false);
		disconnect(zoomOutToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoomOut()));
		zoomDefaultToolbarButton->setEnabled(false);
		disconnect(zoomInToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoomIn()));
		zoomInToolbarButton->setEnabled(false);
		disconnect(layerMenu, SIGNAL(activated(int)), doc->view(), SLOT(GotoLayer(int)));
		layerMenu->setEnabled(false);
		disconnect(pageSelector, SIGNAL(pageChanged(int)), this, SLOT(setCurrentPage(int)));
		pageSelector->setEnabled(false);
	}
	doc = ActWin->doc();
	m_undoManager->switchStack(doc->documentFileName());
	if ((doc != nullptr) && doc->hasGUI())
	{
		connect(m_undoManager, SIGNAL(undoRedoBegin()), doc, SLOT(undoRedoBegin()));
		connect(m_undoManager, SIGNAL(undoRedoDone()) , doc, SLOT(undoRedoDone()));
		connect(m_undoManager, SIGNAL(undoRedoDone()) , doc->view(), SLOT(DrawNew()));
		connect(doc, SIGNAL(addBookmark(PageItem*)), this, SLOT(AddBookMark(PageItem*)));
		connect(doc, SIGNAL(deleteBookmark(PageItem*)), this, SLOT(DelBookMark(PageItem*)));
		connect(unitSwitcher, SIGNAL(activated(int)), doc->view(), SLOT(ChgUnit(int)));
		unitSwitcher->setEnabled(true);
		connect(zoomSpinBox, SIGNAL(valueChanged(double)), doc->view(), SLOT(setZoom()));
		zoomSpinBox->setEnabled(true);
		connect(zoomDefaultToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoom100()));
		zoomDefaultToolbarButton->setEnabled(true);
		connect(zoomOutToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoomOut()));
		zoomOutToolbarButton->setEnabled(true);
		connect(zoomInToolbarButton, SIGNAL(clicked()), doc->view(), SLOT(slotZoomIn()));
		zoomInToolbarButton->setEnabled(true);
		connect(layerMenu, SIGNAL(activated(int)), doc->view(), SLOT(GotoLayer(int)));
		layerMenu->setEnabled(true);
		scrActions["viewPreviewMode"]->blockSignals(true);
		scrActions["viewPreviewMode"]->setChecked(doc->drawAsPreview);
		scrActions["viewPreviewMode"]->blockSignals(false);
		appModeHelper->setPreviewMode(doc->drawAsPreview);
		scrActions["viewEditInPreview"]->setEnabled(doc->drawAsPreview);
		scrActions["viewToggleCMS"]->blockSignals(true);
		scrActions["viewToggleCMS"]->setChecked(doc->HasCMS);
		scrActions["viewToggleCMS"]->blockSignals(false);
		scrActions["viewToggleWhiteSpaceMode"]->blockSignals(true);
		scrActions["viewToggleWhiteSpaceMode"]->setChecked(doc->whiteSpaceModeEnabled);
		scrActions["viewToggleWhiteSpaceMode"]->blockSignals(false);
		pageSelector->setMaximum(doc->masterPageMode() ? 1 : doc->Pages->count());
		slotSetCurrentPage(doc->currentPageNumber());
		connect(pageSelector, SIGNAL(pageChanged(int)), this, SLOT(setCurrentPage(int)));
		pageSelector->setEnabled(true);
	}
	if (view != nullptr)
	{
		actionManager->disconnectNewViewActions();
		if (ScCore->usingGUI())
			doc->m_Selection->disconnect(SIGNAL(selectionChanged()), actionManager, SLOT(handleMultipleSelections()));
	}
	view = ActWin->view();
	const QSignalBlocker blocker(zoomSpinBox);
	zoomSpinBox->setValue(view->scale() * 100.0 / PrefsManager::instance().appPrefs.displayPrefs.displayScale);
	actionManager->connectNewViewActions(view);
	actionManager->disconnectNewDocActions();
	actionManager->connectNewDocActions(doc);
	if (ScCore->usingGUI())
		connect(doc->m_Selection, SIGNAL(selectionChanged()), actionManager, SLOT(handleMultipleSelections()));
	pagePalette->setView(view);
	alignDistributePalette->setDoc(doc);
	if (!doc->isLoading())
	{
		SwitchWin();
		view->requestMode(doc->appMode);
	}
	view->setFocus();
	view->setRulersShown(doc->guidesPrefs().rulersShown);
	scrActions["viewShowMargins"]->setChecked(doc->guidesPrefs().marginsShown);
	scrActions["viewShowBleeds"]->setChecked(doc->guidesPrefs().showBleed);
	scrActions["viewShowFrames"]->setChecked(doc->guidesPrefs().framesShown);
	scrActions["viewShowLayerMarkers"]->setChecked(doc->guidesPrefs().layerMarkersShown);
	scrActions["viewShowGrid"]->setChecked(doc->guidesPrefs().gridShown);
	scrActions["viewShowGuides"]->setChecked(doc->guidesPrefs().guidesShown);
	scrActions["viewShowColumnBorders"]->setChecked(doc->guidesPrefs().colBordersShown);
	scrActions["viewShowBaseline"]->setChecked(doc->guidesPrefs().baselineGridShown);
	scrActions["viewShowImages"]->setChecked(doc->guidesPrefs().showPic);
	scrActions["viewShowTextChain"]->setChecked(doc->guidesPrefs().linkShown);
	scrActions["viewShowTextControls"]->setChecked(doc->guidesPrefs().showControls);
	scrActions["viewShowRulers"]->setChecked(doc->guidesPrefs().rulersShown);
	scrActions["viewRulerMode"]->setChecked(doc->guidesPrefs().rulerMode);
	scrActions["extrasGenerateTableOfContents"]->setEnabled(doc->hasTOCSetup() || doc->hasIndexSetup());
	scrActions["extrasUpdateDocument"]->setEnabled(true);
	scrActions["extrasFixOverflowFrames"]->setEnabled(true);
	scrActions["extrasFixOverflowFramesDoc"]->setEnabled(true);
	if (!doc->masterPageMode())
		pagePalette->rebuild();
	outlinePalette->setDoc(doc);
	if (outlinePalette->isVisible())
	{
		outlinePalette->BuildTree(false);
		outlinePalette->reopenTree();
	}
	RestoreBookMarks();
	if (!doc->isLoading())
	{
		HaveNewSel();
		if (doc->m_Selection->isNotEmpty())
			doc->m_Selection->itemAt(0)->emitAllToGUI();
	}
	docCheckerPalette->setDoc(doc);
	m_tocGenerator->setDoc(doc);
	m_styleManager->setDoc(doc);
	marksManager->setDoc(doc);
	nsEditor->setDoc(doc);
	symbolPalette->setDoc(doc);
	inlinePalette->setDoc(doc);
	// Switching between open documents: our widgets follow the active one.
	if (paragraphStylesPanelTabs)
		paragraphStylesPanelTabs->setDocument(doc);
	if (m_suneerControlBar)
		m_suneerControlBar->setDocument(doc);
	if (m_suneerNewsPanel)
		m_suneerNewsPanel->setDocument(doc);
	// per-document: "Don't ask again" of the linked-images check
	scrActions["extrasWarnLinkedImages"]->setChecked(!doc->suneerLinkedImagesNoAsk());
	modeToolBar->setDoc(doc);
	viewToolBar->setDoc(doc);
	// Give plugins a chance to react on changing the current document
	PluginManager& pluginManager(PluginManager::instance());
	QStringList pluginNames(pluginManager.pluginNames(false));
	ScPlugin* plugin;
	QString pName;
	for (int i = 0; i < pluginNames.count(); ++i)
	{
		pName = pluginNames.at(i);
		plugin = pluginManager.getPlugin(pName, true);
		Q_ASSERT(plugin); // all the returned names should represent loaded plugins
		plugin->setDoc(doc);
	}
}

void ScribusMainWindow::windowsMenuActivated(int id)
{
	QMdiSubWindow* windowWidget = mdiArea->subWindowList().at(id);
	if (windowWidget)
	{
		if (windowWidget->isShaded() || windowWidget->isMinimized())
			windowWidget->showNormal();
		windowWidget->raise();
	}
	newActWin(windowWidget);
}


void ScribusMainWindow::SwitchWin()
{
	updateActiveWindowCaption(doc->documentFileName());
	propertiesPalette->setDoc(doc);
	contentPalette->setDoc(doc);
	if (paragraphStylesPanelTabs) paragraphStylesPanelTabs->setDocument(doc);
	marksManager->setDoc(doc);
	nsEditor->setDoc(doc);
	pagePalette->setView(view);
	layerPalette->setDoc(doc);
	guidePalette->setDoc(doc);
	charPalette->setDoc(doc);
	outlinePalette->setDoc(doc);
	symbolPalette->setDoc(doc);
	inlinePalette->setDoc(doc);
	documentLogViewer->setDocument(doc->uuidString());
	rebuildLayersList();
	updateLayerMenu();
	//Do not set this!, it doesn't get valid pointers unless its in EditClip mode and its not
	//if we are switching windows #4357
	//nodePalette->setDoc(doc, view);
	slotChangeUnit(doc->unitIndex(), false);
/*	FIXME: check if this is really superfluous now
	if (doc->appMode == modeEditClip)
	{
		doc->appMode = modeNormal;
		view->requestMode(submodeEndNodeEdit);
	} */
	appModeHelper->mainWindowSwitchWin(doc);

	if (doc->masterPageMode() || doc->symbolEditMode() || doc->inlineEditMode())
		pagePalette->enablePalette(false);
	else
	{
		if (doc->isModified())
			slotDocCh(false);
		pagePalette->enablePalette(true);
		setPreviewToolbar();
	}

	bool setter = !doc->layerLocked( doc->activeLayer() );
	scrMenuMgr->setMenuEnabled("EditPasteRecent", ((scrapbookPalette->tempBView->objectMap.count() > 0) && setter));
	scrMenuMgr->setMenuEnabled("Insert", setter);
	scrMenuMgr->setMenuEnabled("ItemLayer", doc->layerCount() > 1);
	appModeHelper->changeLayer(doc, (ScMimeData::clipboardHasScribusData() || (scrapbookPalette->tempHasContents())));
}

void ScribusMainWindow::HaveNewDoc()
{
	appModeHelper->mainWindowHasNewDoc(doc, (ScMimeData::clipboardHasScribusData()) || (scrapbookPalette->tempHasContents()));

	//Update palettes
	updateActiveWindowCaption(doc->documentFileName());
	propertiesPalette->setDoc(doc);
	contentPalette->setDoc(doc);
	if (paragraphStylesPanelTabs) paragraphStylesPanelTabs->setDocument(doc);
	nsEditor->setDoc(doc);

	marksManager->setDoc(doc);
	symbolPalette->setDoc(doc);
	inlinePalette->setDoc(doc);
	pagePalette->setView(view);
	layerPalette->setDoc(doc);
	guidePalette->setDoc(doc);
	charPalette->setDoc(doc);
	outlinePalette->setDoc(doc);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	rebuildLayersList();
	updateLayerMenu();
	slotChangeUnit(doc->unitIndex());
	windowsMenuAboutToShow();

	// #9275 : scripter must call HaveNewDoc() in case new doc has been created in a script
	// We may consequently have to call HaveNewDoc several times for the same doc.
	// Use Qt::UniqueConnection here to avoid multiple identical signal connections
	connect(view, SIGNAL(unitChanged(int)), this, SLOT(slotChangeUnit(int)), Qt::UniqueConnection);
	connect(view, SIGNAL(layerChanged(int)), layerPalette, SLOT(markActiveLayer(int)), Qt::UniqueConnection);
	connect(this, SIGNAL(changeLayers(int)), layerPalette, SLOT(markActiveLayer(int)), Qt::UniqueConnection);
	connect(view->horizRuler, SIGNAL(MarkerMoved(double,double)), this, SLOT(setStatusBarTextPosition(double,double)), Qt::UniqueConnection);
	connect(view->horizRuler, SIGNAL(DocChanged(bool)), this, SLOT(slotDocCh(bool)), Qt::UniqueConnection);
	connect(view, SIGNAL(ClipPo(double,double)), nodePalette, SLOT(SetXY(double,double)), Qt::UniqueConnection);
	connect(view, SIGNAL(PolyOpen()), nodePalette, SLOT(IsOpen()), Qt::UniqueConnection);
	connect(view, SIGNAL(ItemGeom()), propertiesPalette->xyzPal, SLOT(handleSelectionChanged()), Qt::UniqueConnection);
	connect(view, SIGNAL(ChBMText(PageItem*)), this, SLOT(BookMarkTxT(PageItem*)), Qt::UniqueConnection);
	connect(view, SIGNAL(HaveSel()), this, SLOT(HaveNewSel()), Qt::UniqueConnection);
	connect(view, SIGNAL(DocChanged()), this, SLOT(slotDocCh()), Qt::UniqueConnection);
	connect(view, SIGNAL(MousePos(double,double)), this, SLOT(setStatusBarMousePosition(double,double)), Qt::UniqueConnection);
	connect(view, SIGNAL(ItemCharStyle(CharStyle)), contentPalette, SLOT(update(CharStyle)), Qt::UniqueConnection);
	connect(view, SIGNAL(ItemCharStyle(CharStyle)), m_suneerControlBar, SLOT(updateFromSelection()), Qt::UniqueConnection);
	connect(view, SIGNAL(ItemTextEffects(int)), this, SLOT(setStyleEffects(int)), Qt::UniqueConnection);
	connect(view, SIGNAL(ItemTextAlign(int)), this, SLOT(setAlignmentValue(int)), Qt::UniqueConnection);
	connect(view, SIGNAL(LoadElem(QString,double,double,bool,bool,ScribusDoc*,ScribusView*)), this, SLOT(slotElemRead(QString,double,double,bool,bool,ScribusDoc*,ScribusView*)), Qt::UniqueConnection);
	connect(view, SIGNAL(AddBM(PageItem*)), this, SLOT(AddBookMark(PageItem*)), Qt::UniqueConnection);
	connect(view, SIGNAL(DelBM(PageItem*)), this, SLOT(DelBookMark(PageItem*)), Qt::UniqueConnection);
}

void ScribusMainWindow::HaveNewSel()
{
	if (m_suneerControlBar)
	{
		m_suneerControlBar->setDocument(doc);
		if (m_suneerNewsPanel) m_suneerNewsPanel->setDocument(doc);
		m_suneerControlBar->updateFromSelection();
	}
	if (doc == nullptr)
		return;
	int selectedType = -1;
	PageItem *currItem = nullptr;
	const int docSelectionCount = doc->m_Selection->count();
	if (docSelectionCount > 0)
	{
		currItem = doc->m_Selection->itemAt(0);
		selectedType = currItem->itemType();
	}
	assert (docSelectionCount == 0 || currItem != nullptr); // help coverity analysis

	setStatusBarTextSelectedItemInfo();

	actionManager->disconnectNewSelectionActions();

	charPalette->setEnabled(false, nullptr);
	view->horizRuler->textMode(false);
	view->horizRuler->update();
	if (!doc->inAnEditMode())
		appModeHelper->enableActionsForSelection(this, doc);

	switch (selectedType)
	{
	case -1: // None
		outlinePalette->slotShowSelect(doc->currentPageNumber(), nullptr);
		propertiesPalette->setGradientEditMode(false);
		break;
	case PageItem::TextFrame: //Text Frame
		if (doc->appMode == modeEdit)
		{
			setTBvals(currItem);
			charPalette->setEnabled(true, currItem);
			if (currItem->isTextFrame())
			{
				appModeHelper->enableTextActions(true, currItem->currentStyle().charStyle().font().scName());
				currItem->asTextFrame()->toggleEditModeActions();
			}
			view->horizRuler->setItem(currItem);
			view->horizRuler->update();
		}
		else
		{
			const ParagraphStyle& curStyle = currItem->itemText.defaultStyle();
			doc->currentStyle.setContext(curStyle.context());
			doc->currentStyle = curStyle;
			emit TextStyle(doc->currentStyle);
			// to go: (av)
			contentPalette->update(doc->currentStyle);
			setStyleEffects(doc->currentStyle.charStyle().effects());
		}
		break;
	case PageItem::Table:
		if (doc->appMode == modeEditTable)
		{
			charPalette->setEnabled(true, currItem);
			PageItem *cellItem = currItem->asTable()->activeCell().textFrame();
			setTBvals(cellItem);
			appModeHelper->enableTextActions(true, cellItem->currentCharStyle().font().scName());
		}
		break;
	case PageItem::PathText: //Path Text
		if (doc->appMode == modeEdit)
			setTBvals(currItem);
		else
		{
			const ParagraphStyle& curStyle = currItem->itemText.defaultStyle();
			doc->currentStyle.setContext(curStyle.context());
			doc->currentStyle = curStyle;
			emit TextStyle(doc->currentStyle);
			// to go: (av)
			contentPalette->update(doc->currentStyle);
			setStyleEffects(doc->currentStyle.charStyle().effects());
		}
		break;
	}
	propertiesPalette->xyzPal->basePointWidget->setSelectedAnchor(doc->rotationMode());

	if (docSelectionCount != 0)
	{
		actionManager->setPDFActions(view);
		updateItemLayerList();
		rebuildScrapbookMenu();
		propertiesPalette->setTextFlowMode(currItem->textFlowMode());
	}

	if (selectedType != -1)
	{
		outlinePalette->slotShowSelect(currItem->OwnPage, currItem);
		actionManager->connectNewSelectionActions(view, doc);
	}

	appModeHelper->updateActionPluginsActions(doc);
}

void ScribusMainWindow::slotDocCh(bool /*reb*/)
{
	if (!doc->isModified())
		doc->setModified(true);
	updateActiveWindowCaption(doc->documentFileName() + "*");
	if (!doc->masterPageMode())
	{
		if (!doc->symbolEditMode() && !doc->inlineEditMode())
		{
			if (doc->hasName)
				scrActions["fileRevert"]->setEnabled(true);
			bool multiPages = doc->DocPages.count() > 1;
			scrActions["pageDelete"]->setEnabled(multiPages);
			scrActions["pageMove"]->setEnabled(multiPages);
			scrActions["fileCollect"]->setEnabled(true);
		}
		scrActions["fileClose"]->setEnabled(true);
	}

	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	// Give plugins a chance to react on changes in the current document
	PluginManager& pluginManager(PluginManager::instance());
	QStringList pluginNames(pluginManager.pluginNames(false));
	ScPlugin* plugin;
	QString pName;
	for (int i = 0; i < pluginNames.count(); ++i)
	{
		pName = pluginNames.at(i);
		plugin = pluginManager.getPlugin(pName, true);
		Q_ASSERT(plugin); // all the returned names should represent loaded plugins
		plugin->changedDoc(doc);
	}
	if (doc->flag_NumUpdateRequest)
	{
		doc->setupNumerations();
		emit UpdateRequest(reqNumUpdate);
	}
	while (doc->flag_Renumber)
	{
		doc->updateNumbers();
		if (!doc->flag_Renumber)
			doc->regionsChanged()->update(QRect());
	}
	if (m_marksCount != doc->marksList().count() || doc->notesChanged() || doc->flag_updateEndNotes || doc->flag_updateMarksLabels)
	{
		bool sendUpdateReqest = false;
		if (m_marksCount != doc->marksList().count() || doc->flag_updateMarksLabels)
			sendUpdateReqest = true;
		m_marksCount = doc->marksList().count();
		doc->updateMarks(doc->notesChanged());
		doc->updateChangedEndNotesFrames();
		if (sendUpdateReqest)
			emit UpdateRequest(reqMarksUpdate);
		doc->setNotesChanged(false);
		doc->flag_updateEndNotes = false;
		doc->flag_updateMarksLabels = false;
	}
}

void ScribusMainWindow::slotPreviewCh()
{
	pagePalette->updatePagePreviews();
}

void ScribusMainWindow::updateRecent(const QString& fn)
{
	if (m_recentDocsList.indexOf(fn) != -1)
		m_recentDocsList.removeAll(fn);
	m_recentDocsList.prepend(fn);
	rebuildRecentFileMenu();
}

void ScribusMainWindow::removeRecent(const QString& fn, bool fromFileWatcher)
{
	if (m_recentDocsList.indexOf(fn) != -1)
	{
		m_recentDocsList.removeAll(fn);
		//#9845: if (!fromFileWatcher)
		//#9845:	ScCore->fileWatcher->removeFile(fn);
	}
	rebuildRecentFileMenu();
}

void ScribusMainWindow::removeRecentFromWatcher(const QString& filename)
{
	removeRecent(filename, true);
}

void ScribusMainWindow::loadRecent(const QString& filename)
{
	if (!NetPathGuard::reachable(filename))
	{
		showNetworkPathNotes();
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("The network folder holding this document is not reachable:\n%1").arg(QDir::toNativeSeparators(filename)));
		return;
	}
	QFileInfo fd(filename);
	if (!fd.exists())
		removeRecent(filename);
	else
		loadDoc(filename);
}

void ScribusMainWindow::rebuildRecentFileMenu()
{
	QString strippedName, localName;
	scrMenuMgr->clearMenuStrings("FileOpenRecent");
	scrRecentFileActions.clear();
	int max = qMin(m_prefsManager.appPrefs.uiPrefs.recentDocCount, m_recentDocsList.count());
	for (int i = 0; i < max; ++i)
	{
		strippedName = localName = QDir::toNativeSeparators(m_recentDocsList[i]);
		strippedName.remove(QDir::separator());
		strippedName.prepend(QString("%1").arg(i+1, 2, 10, QChar('0')));
		scrRecentFileActions.insert(strippedName, new ScrAction(ScrAction::RecentFile, QString(), QString(), QString("&%1 %2").arg(i + 1).arg(localName.replace("&","&&")), QKeySequence(), this, m_recentDocsList[i]));
		connect( scrRecentFileActions[strippedName], SIGNAL(triggeredData(QString)), this, SLOT(loadRecent(QString)) );
		scrMenuMgr->addMenuItemString(strippedName, "FileOpenRecent");
	}
	scrMenuMgr->addMenuItemStringsToRememberedMenu("FileOpenRecent", scrRecentFileActions);
	fileToolBar->rebuildRecentFileMenu();
}

void ScribusMainWindow::rebuildRecentPasteMenu()
{
	scrMenuMgr->clearMenuStrings("EditPasteRecent");

	scrRecentPasteActions.clear();
	int max = qMin(m_prefsManager.appPrefs.scrapbookPrefs.numScrapbookCopies, scrapbookPalette->tempBView->objectMap.count());
	if (max <= 0)
		return;

	auto it = scrapbookPalette->tempBView->objectMap.end();
	QString strippedName;
	for (int i = 0; i < max; ++i)
	{
		it--;
		strippedName = it.key();
		QPixmap pm = it.value().Preview;
		scrRecentPasteActions.insert(strippedName, new ScrAction(ScrAction::RecentPaste, pm, QString(), QString("&%1 %2").arg(i + 1).arg(strippedName), QKeySequence(), this, strippedName));
		connect( scrRecentPasteActions[strippedName], SIGNAL(triggeredData(QString)), this, SLOT(pasteRecent(QString)) );
		scrMenuMgr->addMenuItemString(strippedName, "EditPasteRecent");
	}
	scrMenuMgr->addMenuItemStringsToRememberedMenu("EditPasteRecent", scrRecentPasteActions);
}

void ScribusMainWindow::rebuildScrapbookMenu()
{
	scrMenuMgr->clearMenuStrings("ItemSendToScrapbook");
	scrScrapActions.clear();
	if (!HaveDoc)
		return;
	QStringList scrapNames = scrapbookPalette->getOpenScrapbooksNames();
	scrapNames.removeAt(1);
	for (int i = 0; i < scrapNames.count(); ++i)
	{
		ScrAction *act = new ScrAction( ScrAction::DataInt, QString(), QString(), scrapNames[i], QKeySequence(), this, i);
		scrScrapActions.insert(scrapNames[i], act);
		connect(act, SIGNAL(triggeredData(int)), this, SLOT(PutScrap(int)));
		scrMenuMgr->addMenuItemString(scrapNames[i], "ItemSendToScrapbook");
	}
	scrMenuMgr->addMenuItemStringsToRememberedMenu("ItemSendToScrapbook", scrScrapActions);
}

void ScribusMainWindow::pasteFromScrapbook(const QString& fn)
{
	if (!HaveDoc)
		return;
	view->dragX = 0;
	view->dragY = 0;
	doPasteRecent(scrapbookPalette->activeBView->objectMap[fn].Data);
}

void ScribusMainWindow::pasteRecent(const QString& fn)
{
	doPasteRecent(scrapbookPalette->tempBView->objectMap[fn].Data);
}

void ScribusMainWindow::doPasteRecent(const QString& data)
{
	if (!HaveDoc)
		return;
	QFileInfo fi(data);
	QString formatD(FormatsManager::instance()->extensionListForFormat(FormatsManager::RASTORIMAGES, 1));
	QStringList rasterFiles = formatD.split("|");
	QStringList vectorFiles = LoadSavePlugin::getExtensionsForPreview(FORMATID_FIRSTUSER);
	if (vectorFiles.contains(fi.suffix().toLower()))
	{
		FileLoader *fileLoader = new FileLoader(data);
		int testResult = fileLoader->testFile();
		delete fileLoader;
		if ((testResult != -1) && (testResult >= FORMATID_FIRSTUSER))
		{
			const FileFormat * fmt = LoadSavePlugin::getFormatById(testResult);
			if (fmt)
			{
				fmt->loadFile(data, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive|LoadSavePlugin::lfScripted);
			}
		}
		if (doc->m_Selection->isNotEmpty())
		{
			double x2, y2, w, h;
			doc->m_Selection->getGroupRect(&x2, &y2, &w, &h);
			doc->moveGroup(doc->currentPage()->xOffset() - x2, doc->currentPage()->yOffset() - y2);
			emit UpdateRequest(reqColorsUpdate|reqTextStylesUpdate|reqLineStylesUpdate);
		}
	}
	else if (rasterFiles.contains(fi.suffix().toLower()))
	{
		int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), 1, 1, doc->itemToolPrefs().shapeLineWidth, doc->itemToolPrefs().imageFillColor, doc->itemToolPrefs().imageStrokeColor);
		PageItem *b = doc->Items->at(z);
		b->m_layerID = doc->activeLayer();
		doc->loadPict(data, b);
		SuneerImageLinks::embedPlaced(doc, b);
		b->setWidth(b->OrigW * 72.0 / static_cast<double>(b->pixm.imgInfo.xres));
		b->setHeight(b->OrigH * 72.0 / static_cast<double>(b->pixm.imgInfo.yres));
		b->OldB2 = b->width();
		b->OldH2 = b->height();
		b->updateClip();
		b->adjustPictScale();
	}
	else
	{
		UndoTransaction pasteAction;
		if (UndoManager::undoEnabled())
			pasteAction = m_undoManager->beginTransaction(Um::SelectionGroup, Um::IGroup, Um::Create, QString(), Um::ICreate);
		view->deselectItems(true);
		int docItemCount = doc->Items->count();
		bool savedAlignGrid = doc->SnapGrid;
		bool savedAlignGuides = doc->SnapGuides;
		bool savedAlignElement = doc->SnapItems;
		doc->SnapGrid = false;
		doc->SnapGuides = false;
		doc->SnapItems = false;
		if ((view->dragX == 0.0) && (view->dragY == 0.0))
			slotElemRead(data, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), true, true, doc, view);
		else
			slotElemRead(data, view->dragX, view->dragY, true, false, doc, view);
		doc->SnapGrid = savedAlignGrid;
		doc->SnapGuides = savedAlignGuides;
		doc->SnapItems = savedAlignElement;
		Selection tmpSelection(this, false);
		tmpSelection.copy(*doc->m_Selection, true);
		for (int i = docItemCount; i < doc->Items->count(); ++i)
		{
			PageItem* currItem = doc->Items->at(i);
			doc->setRedrawBounding(currItem);
			tmpSelection.addItem(currItem);
			if (currItem->isBookmark)
				AddBookMark(currItem);
		}
		doc->m_Selection->copy(tmpSelection, false);
		if (pasteAction)
			pasteAction.commit();
	}
	slotDocCh(false);
	slotPreviewCh();
	doc->regionsChanged()->update(QRectF());
	view->dragX = 0;
	view->dragY = 0;
}

void ScribusMainWindow::importVectorFile()
{
	QStringList formats;
	QString allFormats = tr("All Supported Formats")+" (";
	int fmtCode = FORMATID_FIRSTUSER;
	const FileFormat *fmt = LoadSavePlugin::getFormatById(fmtCode);
	while (fmt != nullptr)
	{
		if (fmt->load)
		{
			formats.append(fmt->filter);
			int an = fmt->filter.indexOf("(");
			int en = fmt->filter.indexOf(")");
			while (an != -1)
			{
				allFormats += fmt->filter.mid(an+1, en-an-1)+" ";
				an = fmt->filter.indexOf("(", en);
				en = fmt->filter.indexOf(")", an);
			}
		}
		fmtCode++;
		fmt = LoadSavePlugin::getFormatById(fmtCode);
	}
	allFormats += "*.sce *.SCE);;";
	formats.append("Scribus Objects (*.sce *.SCE)");
	formats.sort(Qt::CaseInsensitive);
	allFormats += formats.join(";;");

	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString wdir = dirs->get("pastefile", ".");
	CustomFDialog dia(this, wdir, tr("Open"), allFormats, fdExistingFiles | fdDisableOk);
	if (dia.exec() != QDialog::Accepted)
		return;

	QString fileName = dia.selectedFile();
	if (fileName.isEmpty())
		return;

	PrefsManager::instance().prefsFile->getContext("dirs")->set("pastefile", fileName.left(fileName.lastIndexOf("/")));
	QFileInfo fi(fileName);
	QString suffix = fi.suffix().toLower();
	if ((suffix == "sce") || (suffix == "shape"))
	{
		QList<QUrl> urls;
		QMimeData* md = new QMimeData();
		urls.append( QUrl::fromLocalFile(fileName) );
		md->setUrls(urls);
		QDrag* dr = new QDrag(this);
		dr->setMimeData(md);
		const QPixmap& dragCursor = IconManager::instance().loadPixmap("cursor-drop-image");
		dr->setPixmap(dragCursor);
		dr->exec();
	}
	else
	{
		FileLoader *fileLoader = new FileLoader(fileName);
		int testResult = fileLoader->testFile();
		delete fileLoader;
		if ((testResult != -1) && (testResult >= FORMATID_FIRSTUSER))
		{
			const FileFormat * fmt = LoadSavePlugin::getFormatById(testResult);
			if (fmt)
			{
				doc->dontResize = true;
				fmt->loadFile(fileName, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive);
				doc->dontResize = false;
			}
		}
	}
	requestUpdate(reqColorsUpdate | reqSymbolsUpdate | reqLineStylesUpdate | reqTextStylesUpdate);
}

void ScribusMainWindow::slotFilePlace()
{
	if (!HaveDoc)
		return;

	PrefsContext* dirs = PrefsManager::instance().prefsFile->getContext("dirs");
	QString wdir = dirs->get("place", dirs->get("images", "."));
	CustomFDialog dia(this, wdir, tr("Place"), tr("PDF Files (*.pdf *.PDF)"), fdExistingFiles | fdDisableOk);
	if (dia.exec() != QDialog::Accepted)
		return;

	QString fileName = dia.selectedFile();
	if (fileName.isEmpty())
		return;
	dirs->set("place", fileName.left(fileName.lastIndexOf("/")));

	// Phase 1: no interactive "loaded cursor" click/drag placement (that
	// needs a dedicated CanvasMode, deferred). The frame lands at the
	// current page's top-left margin, sized to the source PDF page's native
	// size in points; the user repositions/resizes it like any other frame.
	ScPage* pg = doc->currentPage();
	double x = pg->xOffset() + pg->Margins.left();
	double y = pg->yOffset() + pg->Margins.top();

	UndoTransaction placeAction;
	if (UndoManager::undoEnabled())
		placeAction = m_undoManager->beginTransaction(Um::ImageFrame, Um::IImageFrame, Um::Create, QString(), Um::ICreate);

	int z = doc->itemAdd(PageItem::PlacedPDF, PageItem::Unspecified, x, y, 1, 1, doc->itemToolPrefs().shapeLineWidth, CommonStrings::None, CommonStrings::None);
	PageItem* newItem = doc->Items->at(z);
	newItem->m_layerID = doc->activeLayer();
	newItem->pixm.imgInfo.actualPageNumber = 1;
	doc->loadPict(fileName, newItem);
	if (!newItem->imageIsAvailable)
	{
		// Loading failed (corrupt/encrypted/not really a PDF): undo the
		// frame rather than leave an empty placeholder behind.
		Selection failedSel(this, false);
		failedSel.addItem(newItem);
		doc->itemSelection_DeleteItem(&failedSel);
		if (placeAction)
			placeAction.cancel();
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("Could not load \"%1\" as a PDF.").arg(fileName));
		return;
	}
	newItem->setWidth(newItem->OrigW * 72.0 / static_cast<double>(newItem->pixm.imgInfo.xres));
	newItem->setHeight(newItem->OrigH * 72.0 / static_cast<double>(newItem->pixm.imgInfo.yres));
	newItem->OldB2 = newItem->width();
	newItem->OldH2 = newItem->height();
	newItem->updateClip();
	newItem->adjustPictScale();
	newItem->setLineColor(CommonStrings::None);

	if (placeAction)
		placeAction.commit();

	doc->m_Selection->clear();
	doc->m_Selection->addItem(newItem);
	doc->setRedrawBounding(newItem);
	view->DrawNew();
	newItem->emitAllToGUI();
	slotDocCh(false);
}

void ScribusMainWindow::rebuildLayersList()
{
	if (!HaveDoc)
		return;
	scrMenuMgr->clearMenuStrings("ItemLayer");
	scrLayersActions.clear();
	ScLayers::iterator it;
	if (doc->Layers.count() != 0)
	{
		for (it = doc->Layers.begin(); it != doc->Layers.end(); ++it)
		{
			scrLayersActions.insert(QString("%1").arg((*it).ID), new ScrAction(ScrAction::Layer, QString(), QString(), (*it).Name, QKeySequence(), this, (*it).ID));
			scrLayersActions[QString("%1").arg((*it).ID)]->setToggleAction(true);
			QPixmap pm(20,15);
			pm.fill((*it).markerColor);
			scrLayersActions[QString("%1").arg((*it).ID)]->setIcon(pm);
		}
	}
	int currActiveLayer = doc->activeLayer();
	bool found = false;
	for (it = doc->Layers.begin(); it != doc->Layers.end(); ++it)
	{
		if ((*it).ID == currActiveLayer)
		{
			found = true;
			break;
		}
	}
	Q_ASSERT(found);
	scrLayersActions[QString("%1").arg((*it).ID)]->setChecked(true);

	for (auto it = scrLayersActions.begin(); it != scrLayersActions.end(); ++it )
	{
		scrMenuMgr->addMenuItemString(it.key(), "ItemLayer");
		connect( (*it), SIGNAL(triggeredData(int)), doc, SLOT(itemSelection_SendToLayer(int)) );
	}
	scrMenuMgr->addMenuItemStringsToRememberedMenu("ItemLayer", scrLayersActions);
}

void ScribusMainWindow::updateItemLayerList()
{
	if (!HaveDoc)
		return;
	QMap<QString, QPointer<ScrAction> >::Iterator itend = scrLayersActions.end();
	for (auto it = scrLayersActions.begin(); it != itend; ++it)
	{
		(*it)->disconnect(SIGNAL(triggeredData(int)));
		(*it)->setChecked(false);
	}
	if (doc->m_Selection->isNotEmpty() && doc->m_Selection->itemAt(0))
		scrLayersActions[QString("%1").arg(doc->m_Selection->itemAt(0)->m_layerID)]->setChecked(true);
	for (auto it = scrLayersActions.begin(); it != itend; ++it)
		connect( (*it), SIGNAL(triggeredData(int)), doc, SLOT(itemSelection_SendToLayer(int)) );
}

void ScribusMainWindow::updateColorLists()
{
	if (!HaveDoc)
		return;
	requestUpdate(reqColorsUpdate | reqLineStylesUpdate);
	m_styleManager->refreshLists();
}

bool ScribusMainWindow::slotFileOpen()
{
	PrefsContext* docContext = m_prefsManager.prefsFile->getContext("docdirs", false);
	QString docDir;
	QString prefsDocDir(m_prefsManager.documentDir());
	if (!prefsDocDir.isEmpty())
		docDir = docContext->get("docsopen", prefsDocDir);
	else
		docDir = docContext->get("docsopen", ".");
	QString formats(FileLoader::getLoadFilterString());
	QPair<QString, uint> fileNameVersion;
	fileNameVersion = CFileDialog( docDir, tr("Open"), formats, "", fdDisableOk);
	if (fileNameVersion.first.isEmpty()) // User cancelled
		return false;
	QFileInfo fi(fileNameVersion.first);
	docContext->set("docsopen", fi.absolutePath());
	return loadDoc(fileNameVersion.first);
}

bool ScribusMainWindow::slotPageImport()
{
	Q_ASSERT(!doc->masterPageMode());
	
	QScopedPointer<MergeDoc> dia(new MergeDoc(this, false, doc->DocPages.count(), doc->currentPage()->pageNr() + 1));
	if (!dia->exec())
		return false;

	bool ret = false;
	UndoTransaction activeTransaction;
	if (UndoManager::undoEnabled())
		activeTransaction = m_undoManager->beginTransaction(Um::ImportPage, Um::IGroup, Um::ImportPage, nullptr, Um::ILock);

	m_mainWindowStatusLabel->setText( tr("Importing Pages..."));
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	std::vector<int> pageNs;
	parsePagesString(dia->getPageNumbers(), &pageNs, dia->getPageCounter());
	int startPage = 0, nrToImport = pageNs.size();
	bool doIt = true;
	if (doc->masterPageMode())
	{
		if (nrToImport > 1)
			loadPage(dia->getFromDoc(), pageNs[0] - 1, false);
		doIt = false;
	}
	else if (dia->getCreatePageChecked())
	{
		int importWhere = dia->getImportWhere();
		if (importWhere == 0)
			startPage = dia->getImportWherePage();
		else if (importWhere == 1)
			startPage = dia->getImportWherePage() + 1;
		else
			startPage = doc->DocPages.count() + 1;
		addNewPages(dia->getImportWherePage(), importWhere, nrToImport, doc->pageHeight(), doc->pageWidth(), doc->pageOrientation(), doc->pageSize(), true);
	}
	else
	{
		startPage = doc->currentPage()->pageNr() + 1;
		if (nrToImport > (doc->DocPages.count() - doc->currentPage()->pageNr()))
		{
			QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
			ScMessageBox msgBox;
			msgBox.setIcon(QMessageBox::Information);
			msgBox.setText(tr("Import Page(s)"));
			msgBox.setInformativeText("<qt>" +
			QObject::tr("<p>You are trying to import more pages than there are available in the current document counting from the active page.</p>Choose one of the following:"
			"<ul><li><b>Create</b> missing pages</li>"
			"<li><b>Import</b> pages until the last page</li>"
			"<li><b>Cancel</b></li></ul>") + "</qt>");
			QPushButton *createButton = msgBox.addButton(tr("C&reate"), QMessageBox::AcceptRole);
			QPushButton *importButton = msgBox.addButton(tr("&Import"), QMessageBox::AcceptRole);
			QPushButton *cancelButton = msgBox.addButton(CommonStrings::tr_Cancel, QMessageBox::RejectRole);
			msgBox.setDefaultButton(cancelButton);
			msgBox.setDefaultBatchButton(createButton);
			msgBox.exec();
			if (msgBox.clickedButton() == createButton)
			{
				addNewPages(doc->DocPages.count(), 2,
							nrToImport - (doc->DocPages.count() - doc->currentPage()->pageNr()),
							doc->pageHeight(), doc->pageWidth(), doc->pageOrientation(), doc->pageSize(), true);
			}
			else if (msgBox.clickedButton() == importButton)
			{
				nrToImport = doc->DocPages.count() - doc->currentPage()->pageNr();
			}
			else
			{
				doIt = false;
				m_mainWindowStatusLabel->setText("");
			}
			QApplication::restoreOverrideCursor();
		}
	}
	if (doIt)
	{
		if (nrToImport > 0)
		{
			mainWindowProgressBar->reset();
			mainWindowProgressBar->setMaximum(nrToImport);
			int counter = startPage;
			for (int i = 0; i < nrToImport; ++i)
			{
				view->GotoPa(counter);
				loadPage(dia->getFromDoc(), pageNs[i] - 1, false);
				counter++;
				mainWindowProgressBar->setValue(i + 1);
			}
			view->GotoPa(startPage);
			mainWindowProgressBar->reset();
			m_mainWindowStatusLabel->setText( tr("Import done"));
		}
		else
		{
			m_mainWindowStatusLabel->setText( tr("Found nothing to import"));
			doIt = false;
		}
	}
	QApplication::restoreOverrideCursor();
	ret = doIt;

	if (activeTransaction)
		activeTransaction.commit();
	return ret;
}

bool ScribusMainWindow::loadPage(const QString& fileName, int Nr, bool Mpa, const QString& renamedPageName)
{
	if (fileName.isEmpty())
		return false;

	FileLoader *fl = new FileLoader(fileName);
	if (fl->testFile() == -1)
	{
		delete fl;
		return false;
	}
	doc->setLoading(true);
	int oldItemsCount = doc->Items->count();
	if (!fl->loadPage(doc, Nr, Mpa, renamedPageName))
	{
		delete fl;
		doc->setLoading(false);
		return false;
	}
	delete fl;
	if (ScCore->haveCMS() && doc->cmsSettings().CMSinUse)
	{
		recalcColors();
		doc->RecalcPictures(&ScCore->InputProfiles, &ScCore->InputProfilesCMYK);
	}
	int docItemsCount = doc->Items->count();
	for (int i = oldItemsCount; i < docItemsCount; ++i)
	{
		PageItem *ite = doc->Items->at(i);
		if ((ite->isTextFrame()) && (ite->isBookmark))
			AddBookMark(ite);
	}
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	emit UpdateRequest(reqArrowStylesUpdate | reqLineStylesUpdate | reqStyleComboDocUpdate | reqInlinePalUpdate);
	symbolPalette->updateSymbolList();
	slotDocCh();
	rebuildLayersList();
	updateLayerMenu();
	layerPalette->rebuildList();
	doc->setLoading(false);

	if (!Mpa)
		pagePalette->rebuild();
	view->reformPages();
	view->DrawNew();
	return true;
}

bool ScribusMainWindow::loadDoc(const QString& fileName)
{
	return loadDoc(fileName, AlreadyOpenAction::InformAndSwitch);
}

bool ScribusMainWindow::loadDoc(const QString& fileName, AlreadyOpenAction onAlreadyOpen)
{
	return loadDoc(fileName, onAlreadyOpen, LockMode::LockFile);
}

QStringList ScribusMainWindow::templateSearchPaths()
{
	QStringList dirs;
	dirs << ScPaths::instance().templateDir();
	dirs << ScPaths::userTemplateDir(false);
	dirs << ParagraphStylesPanel::templateSourceDir();
	QStringList out;
	for (const QString& d : dirs)
	{
		if (d.isEmpty())
			continue;
		const QString abs = QDir(d).absolutePath();
		// Asked before exists(): on a dead share exists() never returns.
		if (!out.contains(abs) && NetPathGuard::reachable(abs) && QDir(abs).exists())
			out << abs;
	}
	return out;
}

bool ScribusMainWindow::loadDoc(const QString& fileName, AlreadyOpenAction onAlreadyOpen, LockMode lockMode)
{
#ifdef DEBUG_LOAD_TIMES
	QTime t;
	struct tms tms1, tms2;
	t.start();
	times(&tms1);
#endif
	
	QFileInfo fi(fileName);
	if (!fi.exists())
	{
		ScMessageBox::warning(this, CommonStrings::trWarning, tr("File does not exist on the specified path :\n%1").arg(QDir::toNativeSeparators(fileName)), QMessageBox::Ok);
		return false;
	}
	
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	if (HaveDoc)
		outlinePalette->buildReopenVals();
	bool ret = false;
	QList<QMdiSubWindow *> windows = mdiArea->subWindowList();
	ScribusWin* ActWinOld = nullptr;
	if (windows.count() != 0)
	{
		ActWinOld = ActWin;
	}

	// PV - 5780: Scribus doesn't track what documents are already opened
	// The goal of this part of code is to disallow user to open one
	// doc multiple times.
	// Match on the CANONICAL path of the document each window actually holds,
	// not on the window title. Titles are decorated (a trailing "*" when
	// modified) and are not paths at all, and a plain absolute-path compare
	// treats /home/s1/Desktop/a.sla and a symlink to it as different documents —
	// opening the same file twice is exactly how documents get corrupted.
	// canonicalFilePath() resolves symlinks, "..", and duplicate separators.
	const QString filename = fi.absoluteFilePath();
	const QString wantedCanonical = fi.canonicalFilePath();
	int windowCount = windows.count();
	for (int i = 0; i < windowCount; ++i)
	{
		ScribusWin* mx = qobject_cast<ScribusWin*>(windows.at(i)->widget());
		if (!mx || !mx->doc())
			continue;
		const QString openPath = mx->doc()->documentFileName();
		if (openPath.isEmpty())
			continue;                       // never-saved document, cannot collide
		const QString openCanonical = QFileInfo(openPath).canonicalFilePath();
		// canonicalFilePath() is empty if the file vanished; fall back so a
		// deleted-but-open document still matches itself by absolute path.
		const bool same = (!openCanonical.isEmpty() && !wantedCanonical.isEmpty())
		                ? (openCanonical == wantedCanonical)
		                : (QFileInfo(openPath).absoluteFilePath() == fi.absoluteFilePath());
		if (!same)
			continue;

		QApplication::restoreOverrideCursor();
		if (onAlreadyOpen == AlreadyOpenAction::AskGoToOrCancel)
		{
			// The operator just double-clicked and is expecting something to
			// happen, so surface the window first — otherwise the dialog can
			// appear behind the file manager they clicked from.
			if (isMinimized())
				showNormal();
			raise();
			activateWindow();

			ScMessageBox mb(QMessageBox::Information,
			                tr("Already Open"),
			                tr("%1 is already open.").arg(fi.fileName()),
			                QMessageBox::NoButton, this);
			QPushButton* goTo = mb.addButton(tr("Go to it"), QMessageBox::AcceptRole);
			mb.addButton(QMessageBox::Cancel);
			mb.setDefaultButton(goTo);          // Enter = Go to it
			mb.exec();
			if (mb.clickedButton() != goTo)
				return true;                    // Cancel: dismiss, open nothing
		}
		else
		{
			ScMessageBox::information(this, tr("Document is already opened"), tr("This document is already open. It will be set as the active document."));
		}
		windowsMenuActivated(i);
		if (isMinimized())
			showNormal();
		raise();
		activateWindow();
		return true;
	}
	// ── Network lock ──────────────────────────────────────────────────────
	// Reached only when the document is NOT already open in this instance, so a
	// document open here can never present as "in use by someone else".
	bool openReadOnlyDueToLock = false;
	// NoLock (New from Template): the file is only read. Nothing is created
	// beside it, so a template on the server share is never "in use" for the
	// other PCs, and nothing is left behind if this session dies.
	if (lockMode == LockMode::LockFile)
	{
		DocumentLock::Info holder;
		if (!DocumentLock::acquire(filename, &holder))
		{
			if (DocumentLock::isMineThisProcess(holder) || DocumentLock::isMineAndDead(holder))
			{
				// Our own lock: either left behind by this very session (the
				// document is not open here, or we would have returned above -
				// a release that failed on the share), or by a session of ours
				// on this PC that is no longer running. Reclaim silently.
				if (!DocumentLock::takeOver(filename))
					openReadOnlyDueToLock = true;   // could not replace it: fail safe
			}
			else if (!holder.valid)
			{
				// The create failed for a reason other than an existing lock —
				// a read-only share, or no permission to write beside the file.
				// Fail safe: the operator asked never to risk two writers.
				QApplication::restoreOverrideCursor();
				ScMessageBox::warning(this, tr("Cannot Lock Document"),
					tr("Scribus could not create a lock file next to this document, so it "
					   "cannot tell whether another machine is editing it.\n\nOpening "
					   "read-only. Use Save As to keep any changes."));
				QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
				openReadOnlyDueToLock = true;
			}
			else
			{
				QApplication::restoreOverrideCursor();
				const QString who  = holder.user.isEmpty() ? tr("someone") : holder.user;
				const QString host = holder.host.isEmpty() ? tr("another PC") : holder.host;
				const QDateTime when = holder.opened.isValid() ? holder.opened : holder.fileTime;
				const QString since = !when.isValid() ? tr("an unknown time")
					: (when.date() == QDate::currentDate()) ? QLocale().toString(when.time(), QLocale::ShortFormat)
					: QLocale().toString(when, QLocale::ShortFormat);
				ScMessageBox mb(QMessageBox::Warning, tr("Document In Use"),
					tr("This file is open on %1 by %2 since %3.").arg(host, who, since),
					QMessageBox::NoButton, this);
				mb.setInformativeText(tr("%1\n\nOpen Anyway removes their lock. Only do that if you are "
				                         "sure the file is no longer open there, or both of you may "
				                         "overwrite each other's work.").arg(fi.fileName())
				                      + (DocumentLock::isStale(holder, DocumentLock::DefaultStaleHours)
				                         ? QStringLiteral("\n\n") + tr("The lock is more than %1 hours old.").arg(DocumentLock::DefaultStaleHours)
				                         : QString()));
				QPushButton* ro = mb.addButton(tr("Open Read-Only"), QMessageBox::AcceptRole);
				QPushButton* take = mb.addButton(tr("Open Anyway (remove lock)"), QMessageBox::DestructiveRole);
				mb.addButton(QMessageBox::Cancel);
				mb.setDefaultButton(ro);
				mb.exec();
				if (mb.clickedButton() == take)
				{
					if (!DocumentLock::takeOver(filename))
					{
						ScMessageBox::warning(this, tr("Document In Use"),
							tr("The lock could not be removed, so the document opens read-only."));
						openReadOnlyDueToLock = true;   // lost a race, or cannot delete: fail safe
					}
				}
				else if (mb.clickedButton() == ro)
					openReadOnlyDueToLock = true;
				else
					return false;                       // Cancel: open nothing
				QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			}
		}
	}

	UndoBlocker undoBlocker;
	//! Which native SLA generation wrote this file. Captured while the loader is
	//! still alive; used after loadDoc() returns to offer the legacy fix.
	int loadedFormatID = -1;
	if (!fileName.isEmpty())
	{
		FileLoader *fileLoader = new FileLoader(filename);
		int testResult = fileLoader->testFile();
		if (testResult == -1)
		{
			delete fileLoader;
			QApplication::restoreOverrideCursor();
			QString title = tr("Fatal Error") ;
			QString msg = "<qt>"+ tr("File %1 is not in an acceptable format").arg(filename)+"</qt>";
			QString infoMsg = "<qt>" + tr("The file may be damaged or may have been produced in a later version of Scribus.") + "</qt>";
			ScMessageBox msgBox(QMessageBox::Critical, title, msg, QMessageBox::Ok | QMessageBox::Help, this);
			msgBox.setInformativeText(infoMsg);
			int i = msgBox.exec();
			if (i == QMessageBox::Help)
				slotOnlineHelp("", "fileproblems.html");
			return false;
		}
		if (testResult == 0)
			QApplication::changeOverrideCursor(QCursor(Qt::ArrowCursor));
		QDir docProfileDir(fi.absolutePath() + "/profiles");
		ScCore->getCMSProfilesDir(fi.absolutePath() + "/", false, false);
		if (docProfileDir.exists())
			ScCore->getCMSProfilesDir(fi.absolutePath() + "/profiles", false, false);

		m_prefsManager.appPrefs.fontPrefs.AvailFonts.addScalableFonts(fi.absolutePath() + "/", filename);
		QDir docFontDir(fi.absolutePath() + "/fonts");
		if (docFontDir.exists())
			m_prefsManager.appPrefs.fontPrefs.AvailFonts.addScalableFonts(fi.absolutePath() + "/fonts", filename);
		QDir docFontDir2(fi.absolutePath() + "/Fonts");
		if (docFontDir2.exists())
			m_prefsManager.appPrefs.fontPrefs.AvailFonts.addScalableFonts(fi.absolutePath() + "/Fonts", filename);
		QDir docFontDir3(fi.absolutePath() + "/Document fonts");
		if (docFontDir3.exists())
			m_prefsManager.appPrefs.fontPrefs.AvailFonts.addScalableFonts(fi.absolutePath() + "/Document fonts", filename);
		m_prefsManager.appPrefs.fontPrefs.AvailFonts.updateFontMap();
		if (view != nullptr)
			actionManager->disconnectNewViewActions();
		doc = new ScribusDoc();
		doc->saveFilePermissions(QFile::permissions(fileName));
		doc->appMode = modeNormal;
		doc->HasCMS = false;
		doc->OpenNodes.clear();
		doc->setLoading(true);
		m_mainWindowStatusLabel->setText( tr("Loading..."));
		mainWindowProgressBar->reset();
		ScribusWin* w = new ScribusWin(mdiArea, doc);
		w->setMainWindow(this);
		view = new ScribusView(w, this, doc);
		doc->setGUI(true, this, view);
		view->setScale(m_prefsManager.displayScale());
		w->setView(view);
		alignDistributePalette->setDoc(doc);
		ActWin = w;
		doc->WinHan = w;
		w->setSubWin(mdiArea->addSubWindow(w));
		suneerStripMdiCloseKey(w->getSubWin());
		w->setUpdatesEnabled(false);
		view->updatesOn(false);
		doc->SoftProofing = false;
		doc->Gamut = false;
		setScriptRunning(true);
		bool loadSuccess = fileLoader->loadFile(doc);
		//Do the font replacement check from here, when we have a GUI. TODO do this also somehow without the GUI
		//This also gives the user the opportunity to cancel the load when finding there's a replacement required.
		if (loadSuccess && ScCore->usingGUI())
			loadSuccess = fileLoader->postLoad(doc);
		if (!loadSuccess)
		{
			view->close();
			delete fileLoader;
			delete doc;
			doc = nullptr;
			mdiArea->removeSubWindow(w->getSubWin());
			delete w;
			view = nullptr;
			doc = nullptr;
			setScriptRunning(false);
			QApplication::restoreOverrideCursor();
			m_mainWindowStatusLabel->setText("");
			mainWindowProgressBar->reset();
			ActWin = nullptr;
			if (windows.count() != 0)
			{
				newActWin(ActWinOld->getSubWin());
				if (ActWin)
				{
					if ((mdiArea->subWindowList().isEmpty()) || (mdiArea->subWindowList().count() == 1))
						ActWin->showMaximized();
				}
			}
			return false;
		}
		symbolPalette->setDoc(doc);
		outlinePalette->setDoc(doc);
		fileLoader->informReplacementFonts();
		setCurrentComboItem(unitSwitcher, unitGetStrFromIndex(doc->unitIndex()));
		const QSignalBlocker blocker(zoomSpinBox);
		zoomSpinBox->setValue(view->scale());
		view->unitChange();
		setScriptRunning(false);
		view->deselectItems(true);
		m_mainWindowStatusLabel->setText("");
		mainWindowProgressBar->reset();
		HaveDoc++;
		if (doc->checkerProfiles().isEmpty())
		{
			m_prefsManager.initDefaultCheckerPrefs(doc->checkerProfiles());
			doc->setCurCheckProfile(CommonStrings::PDF_1_4);
		}
		m_prefsManager.insertMissingCheckerProfiles(doc->checkerProfiles());
		if (doc->pdfOptions().LPISettings.isEmpty())
		{
			struct LPIData lpo;
			lpo.Frequency = 133;
			lpo.SpotFunc = 3;
			lpo.Angle = 105;
			doc->pdfOptions().LPISettings.insert("Cyan", lpo);
			lpo.Angle = 75;
			doc->pdfOptions().LPISettings.insert("Magenta", lpo);
			lpo.Angle = 90;
			doc->pdfOptions().LPISettings.insert("Yellow", lpo);
			lpo.Angle = 45;
			doc->pdfOptions().LPISettings.insert("Black", lpo);
		}
		if (!doc->cmsSettings().CMSinUse)
			doc->HasCMS = false;
		if ((ScCore->haveCMS()) && (doc->cmsSettings().CMSinUse))
		{
			QString missing, replacement;
			QMultiMap<QString, QString> missingMap;
			if (!ScCore->InputProfiles.contains(doc->cmsSettings().DefaultImageRGBProfile))
			{
				missing = doc->cmsSettings().DefaultImageRGBProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultImageRGBProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultImageRGBProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultImageRGBProfile;
			}
			if (!ScCore->InputProfilesCMYK.contains(doc->cmsSettings().DefaultImageCMYKProfile))
			{
				missing = doc->cmsSettings().DefaultImageCMYKProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultImageCMYKProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultImageCMYKProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultImageCMYKProfile;
			}
			if (!ScCore->InputProfiles.contains(doc->cmsSettings().DefaultSolidColorRGBProfile))
			{
				missing = doc->cmsSettings().DefaultSolidColorRGBProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultSolidColorRGBProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultSolidColorRGBProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultSolidColorRGBProfile;
			}
			if (!ScCore->InputProfilesCMYK.contains(doc->cmsSettings().DefaultSolidColorCMYKProfile))
			{
				missing = doc->cmsSettings().DefaultSolidColorCMYKProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultSolidColorCMYKProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultSolidColorCMYKProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultSolidColorCMYKProfile;
			}
			if (!ScCore->MonitorProfiles.contains(doc->cmsSettings().DefaultMonitorProfile))
			{
				missing = doc->cmsSettings().DefaultMonitorProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultMonitorProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultMonitorProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultMonitorProfile;
			}
			if (!ScCore->PrinterProfiles.contains(doc->cmsSettings().DefaultPrinterProfile))
			{
				missing = doc->cmsSettings().DefaultPrinterProfile;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultPrinterProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->cmsSettings().DefaultPrinterProfile = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultPrinterProfile;
			}
			if (!ScCore->PrinterProfiles.contains(doc->pdfOptions().PrintProf))
			{
				missing = doc->pdfOptions().PrintProf;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultPrinterProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->pdfOptions().PrintProf = doc->cmsSettings().DefaultPrinterProfile;
			}
			if (!ScCore->InputProfiles.contains(doc->pdfOptions().ImageProf))
			{
				missing = doc->pdfOptions().ImageProf;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultImageRGBProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->pdfOptions().ImageProf = doc->cmsSettings().DefaultImageRGBProfile;
			}
			if (!ScCore->InputProfiles.contains(doc->pdfOptions().SolidProf))
			{
				missing = doc->pdfOptions().SolidProf;
				replacement = m_prefsManager.appPrefs.colorPrefs.DCMSset.DefaultSolidColorRGBProfile;
				if (!missingMap.contains(missing, replacement))
					missingMap.insert(missing, replacement);
				doc->pdfOptions().SolidProf = doc->cmsSettings().DefaultSolidColorRGBProfile;
			}
			if (missingMap.count() > 0)
			{
				QApplication::changeOverrideCursor(QCursor(Qt::ArrowCursor));
				QString mess = tr("Some color profiles used by this document are not installed:")+"\n\n";
				for (auto it = missingMap.constBegin(); it != missingMap.constEnd(); ++it)
				{
					mess += it.key() + tr(" was replaced by: ") + it.value() +"\n";
				}
				ScMessageBox::warning(this, CommonStrings::trWarning, mess);
			}
			doc->SoftProofing = doc->cmsSettings().SoftProofOn;
			doc->Gamut        = doc->cmsSettings().GamutCheck;
			doc->IntentColors = doc->cmsSettings().DefaultIntentColors;
			doc->IntentImages = doc->cmsSettings().DefaultIntentImages;
			if (doc->OpenCMSProfiles(ScCore->InputProfiles, ScCore->InputProfilesCMYK, ScCore->MonitorProfiles, ScCore->PrinterProfiles))
			{
				doc->HasCMS = true;
				doc->pdfOptions().SComp = doc->cmsSettings().ComponentsInput2;
			}
			else
			{
				doc->SetDefaultCMSParams();
				doc->HasCMS = false;
			}
			if (doc->HasCMS)
			{
				recalcColors();
				doc->RecalcPictures(&ScCore->InputProfiles, &ScCore->InputProfilesCMYK);
			}
		}
		else
		{
			doc->cmsSettings().CMSinUse = false;
		}
		if (fileLoader->fileType() > FORMATID_NATIVEIMPORTEND)
		{
			doc->setDocumentFileName(filename+ tr("(converted)"));
			QFileInfo fi(doc->documentFileName());
			doc->setDocumentFileName(fi.fileName());
			doc->isConverted = true;
		}
		else
			doc->setDocumentFileName(filename);
		doc->setMasterPageMode(false);
		doc->createHyphenator();
		if (doc->docHyphenator)
		{
			doc->docHyphenator->ignoredWords = doc->hyphenatorPrefs().ignoredWords;
			doc->docHyphenator->specialWords = doc->hyphenatorPrefs().specialWords;
		}
		HaveNewDoc();
		doc->hasName = true;
		if (doc->MasterPages.isEmpty())
		{
			ScPage *docPage = doc->Pages->at(0);
			ScPage *addedPage = doc->addMasterPage(0, CommonStrings::masterPageNormal);
			addedPage->setSize(docPage->size());
			addedPage->setInitialHeight(docPage->height());
			addedPage->setInitialWidth(docPage->width());
			addedPage->setHeight(docPage->height());
			addedPage->setWidth(docPage->width());
			addedPage->initialMargins = docPage->initialMargins;
			addedPage->LeftPg = docPage->LeftPg;
			addedPage->setOrientation(docPage->orientation());
		}
		//Add doc sections if we have none
		if (doc->sections().count() == 0)
		{
			doc->addSection(-1);
			doc->setFirstSectionFromFirstPageNumber();
		}
		doc->RePos = true;
		doc->setMasterPageMode(true);
		doc->reformPages();
		doc->refreshGuides();
		doc->setLoading(false);
		for (PageItem* ite : std::as_const(doc->MasterItems))
		{
			// TODO fix that for Groups on Masterpages
//			if (ite->Groups.count() != 0)
//				view->GroupOnPage(ite);
//			qDebug() << QString("load M: %1 %2 %3").arg(azz).arg((uint)ite).arg(ite->itemType());
			ite->layout();
		}
		doc->setMasterPageMode(false);
		/*QTime t;
		t.start();*/
		doc->flag_Renumber = false;
		doc->updateNumbers(true);
		for (auto iti = doc->Items->begin(); iti != doc->Items->end(); ++iti)
		{
			PageItem* ite = *iti;
			if ((ite->nextInChain() == nullptr) && !ite->isNoteFrame())  //do not layout notes frames
				ite->layout();
		}
		if (!doc->marksList().isEmpty())
		{
			doc->setLoading(true);
			doc->updateMarks(true);
			doc->updateChangedEndNotesFrames();
			doc->setLoading(false);
		}
		for (auto itf = doc->FrameItems.begin(); itf != doc->FrameItems.end(); ++itf)
		{
			PageItem *ite = itf.value();
//			qDebug() << QString("load F: %1 %2 %3").arg(azz).arg((uint)ite).arg(ite->itemType());
			if (ite->nextInChain() == nullptr)
				ite->layout();
		}
		/*qDebug("Time elapsed: %d ms", t.elapsed());*/
		doc->RePos = false;
		doc->setModified(false);
		inlinePalette->setDoc(doc);
		updateRecent(filename);
		m_mainWindowStatusLabel->setText( tr("Ready"));
		ret = true;
		doc->setLoading(true);
		for (int i = 0; i < doc->DocPages.count(); ++i)
		{
			Apply_MasterPage(doc->DocPages.at(i)->masterPageName(), i, false);
		}
		view->reformPages(false);
		doc->setLoading(false);
		loadedFormatID = fileLoader->fileType();
		delete fileLoader;
		view->updatesOn(true);
		w->setUpdatesEnabled(true);
		disconnect(mdiArea, SIGNAL(subWindowActivated(QMdiSubWindow *)), this, SLOT(newActWin(QMdiSubWindow *)));
		if ((mdiArea->subWindowList().isEmpty()) || (mdiArea->subWindowList().count() == 1))
			w->showMaximized();
		else
			w->show();
		view->show();
		// Seems to fix crash on loading
		ActWin = nullptr;
		newActWin(w->getSubWin());
		emit UpdateRequest(reqNumUpdate);
		doc->setCurrentPage(doc->DocPages.at(0));
		scrActions["viewToggleCMS"]->setChecked(doc->HasCMS);
		scrActions["viewToggleWhiteSpaceMode"]->setChecked(doc->whiteSpaceModeEnabled);
		view->zoom();
		view->GotoPage(0);
		connect(mdiArea, SIGNAL(subWindowActivated(QMdiSubWindow*)), this, SLOT(newActWin(QMdiSubWindow*)));
		connect(ScCore->fileWatcher, SIGNAL(fileChanged(QString)), doc, SLOT(updatePict(QString)));
		connect(ScCore->fileWatcher, SIGNAL(fileDeleted(QString)), doc, SLOT(removePict(QString)));
		connect(ScCore->fileWatcher, SIGNAL(dirChanged(QString)), doc, SLOT(updatePictDir(QString)));
		connect(m_undoManager, SIGNAL(undoRedoBegin()), doc, SLOT(undoRedoBegin()));
		connect(m_undoManager, SIGNAL(undoRedoDone()), doc, SLOT(undoRedoDone()));
		connect(m_undoManager, SIGNAL(undoRedoDone()), view, SLOT(DrawNew()));
		doc->connectDocSignals();
		if (doc->autoSave())
			doc->autoSaveTimer->start(doc->autoSaveTime());
		connect(doc, SIGNAL(updateAutoSaveClock()), view->clockLabel, SLOT(resetTime()));
		view->clockLabel->resetTime();
		doc->NrItems = bookmarkPalette->BView->NrItems;
		doc->First = bookmarkPalette->BView->First;
		doc->Last = bookmarkPalette->BView->Last;
		if (doc->drawAsPreview)
			view->togglePreview(true);
	}
	else
	{
		pagePalette->setView(nullptr);
		pagePalette->rebuild();
	}

	m_undoManager->switchStack(doc->documentFileName());
	// No pagePalette->rebuild() here on the success path: newActWin() above
	// already rebuilt the palette for this document, and each rebuild renders
	// every page thumbnail (~1.4 s measured on a newspaper page). The current
	// page marker is kept up to date by GotoPage(0) -> slotSetCurrentPage().
	QApplication::restoreOverrideCursor();
	doc->setModified(false);
	foreach (NotesStyle* NS, doc->m_docNotesStylesList)
		doc->updateNotesFramesStyles(NS);
	// Deferred deliberately: undoBlocker above kills undo for the whole of this
	// function, so a fix run inline here would record nothing and Ctrl+Z would
	// not revert it. Firing on the next event loop pass also lets the window
	// paint before the dialog appears.
	if (ret && HaveDoc && doc)
		QTimer::singleShot(0, this, [this, loadedFormatID] { suneerMaybeOfferLegacyOverflowFix(loadedFormatID); });
	// Carry the lock decision onto the document now that it exists.
	if (ret && doc)
	{
		doc->setOpenedReadOnly(openReadOnlyDueToLock);
		// Close releases the lock this document was opened with, even if its
		// name changes (an imported file becomes "name(converted)").
		if (!openReadOnlyDueToLock && lockMode == LockMode::LockFile)
			DocumentLock::bindDocument(doc, filename);
	}
	// The load failed, so hold no lock: another machine must not be blocked by a
	// document that never opened here.
	if (!ret && !openReadOnlyDueToLock && lockMode == LockMode::LockFile)
		DocumentLock::release(filename);
#ifdef DEBUG_LOAD_TIMES
	times(&tms2);
	double ticks = sysconf(_SC_CLK_TCK);
	double user  = (tms2.tms_utime - tms1.tms_utime)/ticks;
	double sys   = (tms2.tms_stime - tms1.tms_stime)/ticks;
	double cuser = (tms2.tms_cutime - tms1.tms_cutime)/ticks;
	double csys  = (tms2.tms_cstime - tms1.tms_cstime)/ticks;
	qDebug("loaded document in %.3f seconds (%.3f user + %.3f sys = %.3f sec, child %.3f user + %.3f sys = %.3f sec)",
		t.elapsed()/1000.0, user, sys, user + sys, cuser, csys, cuser + csys);
#endif
	return ret;
}

bool ScribusMainWindow::postLoadDoc()
{
	//FIXME Just return for now, if we aren't using the GUI
	return ScCore->usingGUI();
}

// This method was once named slotFileOpen(...) but it hasn't had anything to
// do with file->open for a LONG time. It's used for get text / get picture.
void ScribusMainWindow::suneerGetImage()
{
	if (!HaveDoc) return;
	QString formatD(FormatsManager::instance()->fileDialogFormatList(FormatsManager::IMAGESIMGFRAME));
	QString prefsDocDir = m_prefsManager.documentDir();
	PrefsContext *dirsContext = m_prefsManager.prefsFile->getContext("dirs");
	QString docDir = dirsContext->get("images", prefsDocDir.isEmpty() ? "." : prefsDocDir);
	QStringList fileNames;
	CustomFDialog *dia = new CustomFDialog(QApplication::activeWindow(), docDir, tr("Open"), formatD, fdShowPreview | fdExistingFilesI | fdDisableOk, contextImages);
	if (dia->exec() == QDialog::Accepted)
		fileNames = dia->selectedFiles();
	delete dia;
	if (fileNames.isEmpty()) return;
	dirsContext->set("images", fileNames[0].left(fileNames[0].lastIndexOf("/")));
	// Selection clear ചെയ്ത് import mode activate
	view->deselectItems(true);
	view->requestMode(modeImportImage);
	QTimer::singleShot(150, this, [this, fileNames]() {
		CanvasMode_ImageImport* cii = qobject_cast<CanvasMode_ImageImport*>(view->canvasMode());
		if (cii) {
			cii->setCreateCaption(true);
			cii->setAutoWrap(true);
			cii->setImageList(fileNames);
		}
	});
}

void ScribusMainWindow::slotGetContent()
{
	if (doc->m_Selection->isEmpty())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (currItem->itemType() == PageItem::ImageFrame)
	{
		QString formatD(FormatsManager::instance()->fileDialogFormatList(FormatsManager::IMAGESIMGFRAME));

		QString prefsDocDir = m_prefsManager.documentDir();
		PrefsContext *dirsContext = m_prefsManager.prefsFile->getContext("dirs");
		QString docDir = dirsContext->get("images", prefsDocDir.isEmpty() ? "." : prefsDocDir);

		QStringList fileNames;
		CustomFDialog *dia = new CustomFDialog(QApplication::activeWindow(), docDir, tr("Open"), formatD, fdShowPreview | fdExistingFilesI | fdDisableOk, contextImages);
		if (dia->exec() == QDialog::Accepted)
			fileNames = dia->selectedFiles();
		delete dia;

		if (!fileNames.isEmpty())
		{
			dirsContext->set("images", fileNames[0].left(fileNames[0].lastIndexOf("/")));
			view->requestMode(modeImportImage);
			CanvasMode_ImageImport* cii = qobject_cast<CanvasMode_ImageImport*>(view->canvasMode());
			if (cii) {
				cii->setCreateCaption(false);
				cii->setAutoWrap(false);
				cii->setImageList(fileNames);
			}
		}
	}
	else if (currItem->isTextFrame())
	{
		gtGetText* gt = new gtGetText(doc);
		ImportSetup impsetup = gt->run();
		if (impsetup.runDialog)
		{
			if (currItem->itemText.length() != 0)
			{
				int t = ScMessageBox::warning(this, CommonStrings::trWarning, tr("Do you really want to clear all your text?"),
							QMessageBox::Yes | QMessageBox::No,
							QMessageBox::No,	// GUI default
							QMessageBox::Yes);	// batch default
				if (t == QMessageBox::No)
				{
					delete gt;
					return;
				}
			}
			gt->launchImporter(impsetup.importer, impsetup.filename, impsetup.textOnly, impsetup.encoding, false, impsetup.prefixNames);
		}
		delete gt;
		if (doc->docHyphenator->autoCheck())
			doc->docHyphenator->slotHyphenate(currItem);
		for (int a = 0; a < doc->Items->count(); ++a)
		{
			if (doc->Items->at(a)->isBookmark)
				bookmarkPalette->BView->changeText(doc->Items->at(a));
		}
		if (!impsetup.textOnly)
			doc->flag_NumUpdateRequest = true;
		view->DrawNew();
		slotDocCh();
		m_styleManager->setDoc(doc);
		marksManager->setDoc(doc);
		nsEditor->setDoc(doc);
		inlinePalette->unsetDoc();
		inlinePalette->setDoc(doc);
		if (outlinePalette->isVisible())
			outlinePalette->BuildTree();
		propertiesPalette->updateColorList();
		contentPalette->updateColorList();
		emit UpdateRequest(reqArrowStylesUpdate | reqLineStylesUpdate | reqStyleComboDocUpdate | reqInlinePalUpdate);
		symbolPalette->updateSymbolList();
	}
}

void ScribusMainWindow::updateFromDrop()
{
	m_styleManager->setDoc(doc);
	marksManager->setDoc(doc);
	nsEditor->setDoc(doc);
	inlinePalette->unsetDoc();
	inlinePalette->setDoc(doc);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	emit UpdateRequest(reqArrowStylesUpdate | reqLineStylesUpdate | reqStyleComboDocUpdate | reqInlinePalUpdate);
	symbolPalette->updateSymbolList();
}

void ScribusMainWindow::slotGetContent2() // kk2006
{
	if (doc->m_Selection->isEmpty())
		return; // nothing to do, no selection

	PageItem *currItem = doc->m_Selection->itemAt(0);

	if (!currItem->isTextFrame())
		return; // not a text frame

	ScGTPluginManager::instance()->run();
	if (doc->docHyphenator->autoCheck())
		doc->docHyphenator->slotHyphenate(currItem);
	for (int a = 0; a < doc->Items->count(); ++a)
	{
		if (doc->Items->at(a)->isBookmark)
			bookmarkPalette->BView->changeText(doc->Items->at(a));
	}
	view->DrawNew();
	slotDocCh();
}

void ScribusMainWindow::slotGetClipboardImage()
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;
	if (!QApplication::clipboard()->mimeData()->hasImage())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (currItem->itemType() != PageItem::ImageFrame)
		return;

	int t = QMessageBox::Yes;
	if (currItem->imageIsAvailable)
		t = ScMessageBox::warning(this, CommonStrings::trWarning, tr("Do you really want to replace your existing image?"),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No,	// GUI default
					QMessageBox::Yes);	// batch default
	if (t != QMessageBox::Yes)
		return;

	QImage img = QApplication::clipboard()->image();
	if (img.isNull())
		return;

	QTemporaryFile tempFile(QDir::tempPath() + "/scribus_temp_XXXXXX.png");
	if (!tempFile.open())
		return;
	QString fileName = getLongPathName(tempFile.fileName());
	tempFile.setAutoRemove(false);
	tempFile.close();

	if (!img.save(fileName, "PNG"))
	{
		QFile::remove(fileName);
		return;
	}
	
	currItem->pixm.imgInfo.isRequest = false;
	currItem->UseEmbedded = true;
	currItem->EmbeddedProfile.clear();
	currItem->ImageProfile = doc->cmsSettings().DefaultImageRGBProfile;
	currItem->ImageIntent = doc->cmsSettings().DefaultIntentImages;
	QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	
	currItem->isInlineImage = true;
	currItem->isTempFile = true;
	currItem->Pfile = fileName;
	doc->loadPict(fileName, currItem, false, true);
	// Call to showScaleAndOffset() is now very likely unnecessary
	// due to mechanisms used to update properties in PP in 1.5.x+
	//propertiesPalette->imagePal->showScaleAndOffset(currItem->imageXScale(), currItem->imageYScale(), currItem->imageXOffset(), currItem->imageYOffset());
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	view->DrawNew();
	emit UpdateRequest(reqColorsUpdate | reqCmsOptionsUpdate);
	currItem->emitAllToGUI();
	QApplication::restoreOverrideCursor();
}

void ScribusMainWindow::toogleInlineState()
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->isEmpty())
		return;
	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (currItem->itemType() != PageItem::ImageFrame)
		return;
	if (!currItem->imageIsAvailable)
		return;
	if (currItem->isImageInline())
	{
		QFileInfo fiB(currItem->Pfile);

		PrefsContext* docContext = m_prefsManager.prefsFile->getContext("docdirs", false);
		QString wdir = ".";
		if (doc->hasName)
		{
			QFileInfo fi(doc->documentFileName());
			wdir = QDir::fromNativeSeparators( fi.path() );
		}
		else
		{
			QString prefsDocDir = m_prefsManager.documentDir();
			if (!prefsDocDir.isEmpty())
				wdir = docContext->get("place_as", prefsDocDir);
			else
				wdir = docContext->get("place_as", ".");
			wdir = QDir::fromNativeSeparators( wdir );
		}
		QPair<QString, uint> fileNameVersion;
		fileNameVersion = CFileDialog(wdir, tr("Filename and Path for Image"), tr("All Files (*)"), fiB.fileName(), fdHidePreviewCheckBox);
		QString fileName(fileNameVersion.first);
		if (!fileName.isEmpty())
		{
			if (ScCore->fileWatcher->isWatching(currItem->Pfile))
				ScCore->fileWatcher->removeFile(currItem->Pfile);
			docContext->set("place_as", fileName.left(fileName.lastIndexOf("/")));
			if (overwrite(this, fileName))
			{
				currItem->makeImageExternal(fileName);
				ScCore->fileWatcher->addFile(currItem->Pfile);
				bool fho = currItem->imageFlippedH();
				bool fvo = currItem->imageFlippedV();
				doc->loadPict(currItem->Pfile, currItem, true);
				currItem->setImageFlippedH(fho);
				currItem->setImageFlippedV(fvo);
			}
		}
	}
	else
	{
		if (ScCore->fileWatcher->isWatching(currItem->Pfile))
			ScCore->fileWatcher->removeFile(currItem->Pfile);
		currItem->makeImageInline();
		ScCore->fileWatcher->addFile(currItem->Pfile);
		bool fho = currItem->imageFlippedH();
		bool fvo = currItem->imageFlippedV();
		doc->loadPict(currItem->Pfile, currItem, true);
		currItem->setImageFlippedH(fho);
		currItem->setImageFlippedV(fvo);
	}
	scrActions["itemToggleInlineImage"]->setChecked(currItem->isImageInline());
}

void ScribusMainWindow::slotFileAppend()
{
	if (doc->m_Selection->isNotEmpty())
	{
		gtGetText* gt = new gtGetText(doc);
		ImportSetup impsetup = gt->run();
		if (impsetup.runDialog)
		{
			gt->launchImporter(impsetup.importer, impsetup.filename, impsetup.textOnly, impsetup.encoding, true, impsetup.prefixNames);
		}
		delete gt;
		//CB Hyphenating now emits doc changed, plus we change lang as appropriate
		if (doc->docHyphenator->autoCheck())
			doc->itemSelection_DoHyphenate();
		view->DrawNew();
		//slotDocCh();
	}
}

void ScribusMainWindow::slotFileRevert()
{
	if ((doc->hasName) && (doc->isModified()) && (!doc->masterPageMode()) && (!doc->isConverted))
	{
		ScribusWin* tw = ActWin;
		if (!scriptIsRunning())
		{
			int t = ScMessageBox::warning(this, CommonStrings::trWarning, "<qt>" +
									 QObject::tr("The changes to your document have not been saved and you have requested to revert them. Do you wish to continue?") + "</qt>",
									 QMessageBox::Yes | QMessageBox::No,
									 QMessageBox::No,	// GUI default
									 QMessageBox::Yes);	// batch default
			if (t == QMessageBox::No)
				return;
		}

		mdiArea->setActiveSubWindow(tw->getSubWin());
		ActWin = tw;
		QString fn(doc->documentFileName());
		doc->setModified(false);
		if (doc == storyEditor->currentDocument())
			storyEditor->close();
		slotFileClose();
		QApplication::processEvents();
		loadDoc(fn);
		m_undoManager->clearStack();
	}
}

// True while slotFileSave() is running, so the Save As it falls through to
// for an unnamed document does not show the linked-images dialog a second time.
static bool s_suneerLinkCheckDone = false;

bool ScribusMainWindow::suneerLinkedImagesCheck(const QString& action)
{
	if (!HaveDoc || !doc || doc->suneerLinkedImagesNoAsk())
		return true;
	// Never stop a script or a headless run on a modal dialog.
	if (!ScCore->usingGUI() || scriptIsRunning())
		return true;
	const QList<SuneerImageLinks::Entry> entries = SuneerImageLinks::collect(doc, true);
	if (entries.isEmpty())
		return true;

	SuneerLinkedImagesDialog dia(this, action, entries);
	dia.exec();
	if (dia.choice() == SuneerLinkedImagesDialog::Cancel)
		return false;
	if (dia.dontAskAgain())
	{
		doc->setSuneerLinkedImagesNoAsk(true);
		scrActions["extrasWarnLinkedImages"]->setChecked(false);
		doc->changed();
	}
	if (dia.choice() == SuneerLinkedImagesDialog::EmbedAndContinue)
	{
		QStringList failed;
		const int embedded = SuneerImageLinks::embedAll(doc, &failed);
		view->DrawNew();
		QString msg = tr("Embedded %1 image(s).").arg(embedded);
		if (!failed.isEmpty())
			msg += " " + tr("%1 could not be embedded (missing file).").arg(failed.count());
		setStatusBarInfoText(msg);
	}
	return true;
}

void ScribusMainWindow::suneerEmbedAllImages()
{
	if (!HaveDoc || !doc)
		return;
	const QList<SuneerImageLinks::Entry> all = SuneerImageLinks::collect(doc, false);
	int already = 0;
	for (const auto& e : all)
		if (e.status == SuneerImageLinks::Embedded)
			++already;
	QStringList failed;
	const int embedded = SuneerImageLinks::embedAll(doc, &failed);
	view->DrawNew();

	QString msg = tr("Embedded %1 image(s).").arg(embedded);
	if (already > 0)
		msg += "\n" + tr("%1 were already embedded.").arg(already);
	if (!failed.isEmpty())
		msg += "\n\n" + tr("%1 could not be embedded because the file is missing (page / frame: file):").arg(failed.count())
		     + "\n" + failed.join("\n");
	if (embedded > 0)
		msg += "\n\n" + tr("One Undo takes all of them back. Save the document to write them into the file.");
	setStatusBarInfoText(tr("Embedded %1 image(s), %2 failed.").arg(embedded).arg(failed.count()));
	if (ScCore->usingGUI() && !scriptIsRunning())
		ScMessageBox::information(this, tr("Embed All Images"), msg);
}

void ScribusMainWindow::suneerToggleImageLinkBadges()
{
	SuneerImageLinks::setBadgesShown(scrActions["viewShowImageLinkBadges"]->isChecked());
	if (HaveDoc && view)
		view->DrawNew();
}

void ScribusMainWindow::suneerToggleWarnLinkedImages()
{
	if (!HaveDoc || !doc)
	{
		scrActions["extrasWarnLinkedImages"]->setChecked(true);
		return;
	}
	doc->setSuneerLinkedImagesNoAsk(!scrActions["extrasWarnLinkedImages"]->isChecked());
	doc->changed();
}

bool ScribusMainWindow::slotFileSave()
{
	// Opened read-only because another machine holds the lock: never write over
	// their work. Save As to a different name stays available.
	if (doc && doc->openedReadOnly())
	{
		ScMessageBox::information(this, tr("Read-Only Document"),
			tr("This document was opened read-only because it is in use on another "
			   "machine, so it cannot be saved over.\n\nUse Save As to keep your "
			   "changes under a different name."));
		return false;
	}
	if (!suneerLinkedImagesCheck(tr("Save")))
		return false;
	// Save As is reached from here for an unnamed document: asked once, above
	s_suneerLinkCheckDone = true;
	struct ResetFlag { ~ResetFlag() { s_suneerLinkCheckDone = false; } } resetFlag;
	bool ret = false;
	if ((doc->hasName) && (!doc->isConverted))
	{
		QString fn(doc->documentFileName());
		QString savedFileName;
		ret = DoFileSave(fn, &savedFileName);
		if (!ret && !savedFileName.isEmpty())
			ScMessageBox::warning(this, CommonStrings::trWarning, tr("Your document was saved to a temporary file and could not be moved: \n%1").arg( QDir::toNativeSeparators(savedFileName) ));
		else if (!ret)
			ScMessageBox::warning(this, CommonStrings::trWarning, tr("Cannot write the file: \n%1").arg( QDir::toNativeSeparators(fn) ));
	}
	else
		ret = slotFileSaveAs();
	return ret;
}

bool ScribusMainWindow::slotFileSaveAs()
{
	if (!s_suneerLinkCheckDone && !suneerLinkedImagesCheck(tr("Save As")))
		return false;
	bool ret = false;
	QString fileName;
	PrefsContext* docContext = m_prefsManager.prefsFile->getContext("docdirs", false);
	QString wdir;
	if (doc->hasName)
	{
		QFileInfo fi(doc->documentFileName());
		QString completeBaseName = fi.completeBaseName();
		if (completeBaseName.endsWith(".sla", Qt::CaseInsensitive))
			completeBaseName.chop(4);
		else if (completeBaseName.endsWith(".gz", Qt::CaseInsensitive))
			completeBaseName.chop(3);
		wdir = QDir::fromNativeSeparators( fi.path() );
		fileName  = QDir::fromNativeSeparators( fi.path()+"/"+completeBaseName+".sla" );
	}
	else
	{
		QString prefsDocDir = m_prefsManager.documentDir();
		if (!prefsDocDir.isEmpty())
			wdir = docContext->get("save_as", prefsDocDir);
		else
			wdir = docContext->get("save_as", ".");
		fileName = QDir::fromNativeSeparators( wdir );
		if (wdir.right(1) != "/")
			fileName += "/";
		fileName += doc->documentFileName() + ".sla";
	}
	bool saveCompressed = m_prefsManager.appPrefs.docSetupPrefs.saveCompressed;
	if (saveCompressed)
		fileName.append(".gz");

	//QString formats = tr("Documents (*.sla *.sla.gz);;All Files (*)");
	QString formats(FileLoader::getSaveAsFilterString());
	int optionFlags = fdCompressFile | fdHidePreviewCheckBox;
	QPair<QString, uint> fileNameVersion { fileName, FORMATID_CURRENTEXPORT };
	fileNameVersion = CFileDialog( wdir, tr("Save As"), formats, fileName, optionFlags, &saveCompressed);
	QString fn(fileNameVersion.first);
	if (!fn.isEmpty())
	{
		docContext->set("save_as", fn.left(fn.lastIndexOf("/")));
		fileName = fn;
		if (!((fn.endsWith(".sla")) || (fn.endsWith(".sla.gz"))))
			fileName = fn+".sla";
		if (overwrite(this, fileName))
		{
			QString savedFileName;
			ret = DoFileSave(fileName, &savedFileName, fileNameVersion.second);
			if (!ret && !savedFileName.isEmpty())
				ScMessageBox::warning(this, CommonStrings::trWarning, tr("Your document was saved to a temporary file and could not be moved: \n%1").arg( QDir::toNativeSeparators(savedFileName) ));
			else if (!ret)
				ScMessageBox::warning(this, CommonStrings::trWarning, tr("Cannot write the file: \n%1").arg( QDir::toNativeSeparators(fn) ));
			else
				doc->pdfOptions().fileName.clear(); // #1482 reset the pdf file name
		}
	}
	m_mainWindowStatusLabel->setText( tr("Ready"));
	return ret;
}

void ScribusMainWindow::moveDocumentLock(const QString& newFileName)
{
	// Save As to another name: the old file is no longer open here, so its
	// lock goes; the new file is, so it takes one. Without this the old lock
	// stayed on the share until Scribus exited.
	const QString oldPath = DocumentLock::boundPath(doc);
	if (!oldPath.isEmpty() && QFileInfo(oldPath).absoluteFilePath() == QFileInfo(newFileName).absoluteFilePath())
		return;   // same file: keep the lock we have
	DocumentLock::releaseDocument(doc);
	DocumentLock::Info holder;
	bool locked = DocumentLock::acquire(newFileName, &holder);
	if (!locked && (DocumentLock::isMineThisProcess(holder) || DocumentLock::isMineAndDead(holder)))
		locked = DocumentLock::takeOver(newFileName);
	if (locked)
	{
		DocumentLock::bindDocument(doc, newFileName);
		doc->setOpenedReadOnly(false);   // this copy is ours to save
	}
	else
	{
		doc->setOpenedReadOnly(true);
		ScMessageBox::warning(this, tr("Document In Use"),
			holder.valid ? tr("The file was saved, but it is open on %1 by %2, so this copy is now read-only.")
			                 .arg(holder.host.isEmpty() ? tr("another PC") : holder.host, holder.user.isEmpty() ? tr("someone") : holder.user)
			             : tr("The file was saved, but Scribus could not create a lock file next to it, so this copy is now read-only."));
	}
}

bool ScribusMainWindow::DoFileSave(const QString& fileName, QString* savedFileName, uint formatID)
{
	ScCore->fileWatcher->forceScan();
	ScCore->fileWatcher->stop();
	doc->reorganiseFonts();
	m_mainWindowStatusLabel->setText( tr("Saving..."));
	mainWindowProgressBar->reset();
	bool ret = doc->save(fileName, savedFileName, formatID);
	// Every Save As route (the menu, Collect for Output, scripts) ends here, so
	// this is where the network lock follows the document to its new name. A
	// plain Save to the same file keeps the lock it has.
	if (ret)
		moveDocumentLock(fileName);
	QApplication::processEvents();
	if (ret)
	{
		updateActiveWindowCaption(fileName);
		m_undoManager->renameStack(fileName);
		scrActions["fileRevert"]->setEnabled(false);
		updateRecent(fileName);
	}
	m_mainWindowStatusLabel->setText("");
	mainWindowProgressBar->reset();
	ScCore->fileWatcher->start();
	return ret;
}

bool ScribusMainWindow::slotFileClose()
{
	ScribusWin* tw = ActWin;
	mdiArea->closeActiveSubWindow();
	windowsMenuAboutToShow();
	return (tw != ActWin);
}


bool ScribusMainWindow::DoFileClose()
{
	// Give up the network lock before anything else: if this throws or returns
	// early, another machine would be blocked by a document nobody has open.
	if (doc && !DocumentLock::releaseDocument(doc) && !doc->documentFileName().isEmpty() && !doc->openedReadOnly())
		DocumentLock::release(doc->documentFileName());
	slotEndSpecialEdit();
	view->deselectItems(false);
	if (doc == storyEditor->currentDocument())
		storyEditor->close();
	actionManager->disconnectNewDocActions();
	actionManager->disconnectNewViewActions();
	m_undoManager->removeStack(doc->documentFileName());
	closeActiveWindowMasterPageEditor();
	slotSelect();
	doc->autoSaveTimer->stop();
	doc->disconnectDocSignals();
	disconnect(ScCore->fileWatcher, SIGNAL(fileChanged(QString )), doc, SLOT(updatePict(QString)));
	disconnect(ScCore->fileWatcher, SIGNAL(fileDeleted(QString )), doc, SLOT(removePict(QString)));
	disconnect(ScCore->fileWatcher, SIGNAL(dirChanged(QString )), doc, SLOT(updatePictDir(QString)));
	disconnect(viewToolBar->previewQualitySwitcher, SIGNAL(activated(int)), this, SLOT(changePreviewQuality(int)));
	disconnect(viewToolBar->visualMenu, SIGNAL(activated(int)), doc->view(), SLOT(switchPreviewVisual(int)));
	if (ScCore->haveCMS())
		doc->CloseCMSProfiles();
	//<<Palettes
	propertiesPalette->unsetDoc();
	contentPalette->unsetDoc();
	inlinePalette->unsetDoc();
	symbolPalette->unsetDoc();
	// Our own widgets forget the document now, before it is deleted below.
	if (paragraphStylesPanelTabs)
		paragraphStylesPanelTabs->unsetDocument();
	if (m_suneerControlBar)
		m_suneerControlBar->setDocument(nullptr);
	if (m_suneerNewsPanel)
		m_suneerNewsPanel->setDocument(nullptr);
	pagePalette->setView(nullptr);
	pagePalette->rebuild();
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	bookmarkPalette->BView->clear();
	bookmarkPalette->BView->NrItems = 0;
	bookmarkPalette->BView->First = 1;
	bookmarkPalette->BView->Last = 0;
	outlinePalette->unsetDoc();
	alignDistributePalette->setDoc(nullptr);
	//>>

	if ((mdiArea->subWindowList().isEmpty()) || (mdiArea->subWindowList().count() == 1))
	{
		PluginManager& pluginManager(PluginManager::instance());
		pluginManager.enableOnlyStartupPluginActions(this);
		appModeHelper->mainWindowCloseLastDoc();
		m_mainWindowStatusLabel->setText( tr("Ready"));
		m_PrinterUsed = false;
	}
	// Give plugins a chance to react on closing the document
	PluginManager& pluginManager(PluginManager::instance());
	QStringList pluginNames(pluginManager.pluginNames(false));
	ScPlugin* plugin;
	QString pName;
	for (int i = 0; i < pluginNames.count(); ++i)
	{
		pName = pluginNames.at(i);
		plugin = pluginManager.getPlugin(pName, true);
		Q_ASSERT(plugin); // all the returned names should represent loaded plugins
		plugin->unsetDoc();
	}
	view->close();
	//CB Yes, we are setting it to nullptr without deleting it. ActWin(ScribusWin) owns the view
	//due to it being the central widget and will delete it at the correct moment from its own pointer.
	view = nullptr;
	doc->setLoading(true);
	guidePalette->setDoc(nullptr);
	charPalette->setDoc(nullptr);
	m_tocGenerator->setDoc(nullptr);
	m_styleManager->setDoc(nullptr);
	marksManager->setDoc(nullptr);
	nsEditor->setDoc(nullptr);
	layerPalette->clearContent();
	docCheckerPalette->buildErrorList(nullptr);
	viewToolBar->setDoc(nullptr);
	HaveDoc--;
	// ActWin (== the ScribusWin whose closeEvent() called us, see scribuswin.cpp)
	// holds its own m_Doc pointer to this same object, and can still be found in
	// mdiArea->subWindowList() for a while after this: WA_DeleteOnClose only
	// schedules that widget's destruction, it doesn't happen synchronously.
	// Clear it before freeing doc so a reopen of this file during that window
	// doesn't read the duplicate-open check through a dangling pointer.
	if (ActWin)
		ActWin->clearDoc();
	delete doc;
	doc = nullptr;
	ActWin = nullptr;
	if (HaveDoc == 0)
	{
		QString prefsDocDir( PrefsManager::instance().documentDir() );
		if ( QDir().exists(prefsDocDir) )
			QDir::setCurrent( PrefsManager::instance().documentDir() );
		else
			QDir::setCurrent( QDir::homePath() );
	}
	disconnect(pageSelector, SIGNAL(pageChanged(int)), this, SLOT(setCurrentPage(int)));
	pageSelector->setMaximum(1);
	pageSelector->setEnabled(false);
	updateLayerMenu();
	updateTableMenuActions();
	rebuildScrapbookMenu();
	mainWindowXPosDataLabel->clear();
	mainWindowYPosDataLabel->clear();
	//not running view's togglePreview as we don't want to affect the doc settings.
	scrActions["viewPreviewMode"]->setChecked(false);
	scrActions["viewToggleWhiteSpaceMode"]->setChecked(false);
	appModeHelper->setPreviewMode(false);
	return true;
}

void ScribusMainWindow::slotFilePrint()
{
	if (!suneerLinkedImagesCheck(tr("Print")))
		return;
	if (doc->checkerProfiles()[doc->curCheckProfile()].autoCheck)
	{
		if (scanDocument())
		{
			if (doc->checkerProfiles()[doc->curCheckProfile()].ignoreErrors)
			{
				int t = ScMessageBox::warning(this, CommonStrings::trWarning,
											"<qt>"+ tr("Scribus has detected some errors. Consider using the Preflight Verifier to correct them")+"</qt>",
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default
											QMessageBox::Ignore);	// batch default
				if (t == QMessageBox::Abort)
					return;
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(slotReallyPrint()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkPrint;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	slotReallyPrint();
}

void ScribusMainWindow::slotReallyPrint()
{
	bool done;
	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->setIgnoreEnabled(false);
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(slotReallyPrint()));
	}
	QString printError;
	PrintOptions options;
	m_mainWindowStatusLabel->setText( tr("Printing..."));
	if (doc->Print_Options.firstUse)
	{
		doc->Print_Options.printer.clear();
		if (!doc->documentFileName().startsWith( tr("Document")))
		{
			QFileInfo fi(doc->documentFileName());
			QString completeBaseName = fi.completeBaseName();
			if (completeBaseName.endsWith(".sla", Qt::CaseInsensitive))
			{
				if (completeBaseName.length() > 4)
					completeBaseName.chop(4);
			}
			if (completeBaseName.endsWith(".gz", Qt::CaseInsensitive))
			{
				if (completeBaseName.length() > 3)
					completeBaseName.chop(3);
			}
			doc->Print_Options.filename = fi.path() + "/" + completeBaseName + ".ps";
		}
		else
		{
			doc->Print_Options.filename = QDir::currentPath() + "/" + doc->documentFileName() + ".ps";
		}
	}
	doc->Print_Options.copies = 1;

	PrintDialog *printer = new PrintDialog(this, doc, doc->Print_Options);
	printer->setMinMax(1, doc->Pages->count(), doc->currentPage()->pageNr()+1);
	printDinUse = true;
	connect(printer, SIGNAL(doPreview()), this, SLOT(doPrintPreview()));
	if (printer->exec())
	{
		ReOrderText(doc, view);
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		doc->Print_Options.pageNumbers.clear();
		if (printer->doPrintCurrentPage())
			doc->Print_Options.pageNumbers.push_back(doc->currentPage()->pageNr()+1);
		else
		{
			if (printer->doPrintAll())
				parsePagesString("*", &doc->Print_Options.pageNumbers, doc->DocPages.count());
			else
				parsePagesString(printer->getPageString(), &doc->Print_Options.pageNumbers, doc->DocPages.count());
		}
		m_PrinterUsed = true;
		doc->Print_Options.isProofPrint = printer->isProofPrint();
		// Proof print already asks the printer for draft/150dpi; without this
		// Scribus still rasterised every placed image at 300dpi CMYK into the
		// job (measured: ~61MB of PostScript per photo). Match the data to what
		// the printer is being told to do. Colour is left to the dialog's own
		// Color/Grayscale setting — an explicit choice there must win.
		doc->Print_Options.imageResolution = printer->isProofPrint() ? 150 : 300;
		// Same paper adaptation as the one-click proof: without the target sheet
		// the page is emitted at document size and an A4 printer just clips it.
		doc->Print_Options.proofPaperWidth = 0.0;
		doc->Print_Options.proofPaperHeight = 0.0;
		if (printer->isProofPrint())
		{
			QString dlgMedia;
			QSizeF dlgSheet;
			if (!PrinterUtil::getDefaultPaperSize(doc->Print_Options.printer, dlgMedia, &dlgSheet) || dlgSheet.isEmpty())
				dlgSheet = QSizeF(595.276, 841.89);   // A4 in points
			doc->Print_Options.proofPaperWidth = dlgSheet.width();
			doc->Print_Options.proofPaperHeight = dlgSheet.height();
		}
		done = doPrint(doc->Print_Options, printError);
		QApplication::restoreOverrideCursor();
		if (!done)
		{
			QString message = tr("Printing failed!");
			if (!printError.isEmpty())
				message += QString("\n%1").arg(printError);
			ScMessageBox::warning(this, CommonStrings::trWarning, message);
		}
		else
			doc->Print_Options.firstUse = false;
		getDefaultPrinter(PDef.Pname, PDef.Pname, PDef.Command);
	}
	printDinUse = false;
	disconnect(printer, SIGNAL(doPreview()), this, SLOT(doPrintPreview()));
	delete printer;
	m_mainWindowStatusLabel->setText( tr("Ready"));
}

void ScribusMainWindow::slotEndSpecialEdit()
{
	if (doc->symbolEditMode())
		editSymbolEnd();
	else if (doc->inlineEditMode())
		editInlineEnd();
	else if (doc->masterPageMode())
		editMasterPagesEnd();
}

void ScribusMainWindow::slotFileProofPrint()
{
	if (!HaveDoc)
		return;
	if (PrinterUtil::getPrinterNames().isEmpty())
	{
		ScMessageBox::warning(this, CommonStrings::trWarning,
			tr("No printers are set up, so there is nothing to proof to."));
		return;
	}
	if (!suneerLinkedImagesCheck(tr("Proof Print")))
		return;

	// F9 opens this: the paper a proof is scaled onto depends on what is
	// actually loaded in the tray, which only the operator knows.
	ProofPrintDialog dialog(this);
	dialog.setPageSize(QSizeF(doc->currentPage()->width(), doc->currentPage()->height()));
	if (dialog.exec() != QDialog::Accepted)
		return;
	dialog.saveChoices();

	// A proof checks layout, not colour or detail, so it goes out at half
	// resolution in grayscale. This is a throwaway copy of the document's
	// print options: production settings (300dpi, colour, separations, marks)
	// are never modified.
	PrintOptions proof = doc->Print_Options;
	proof.isProofPrint = true;
	proof.imageResolution = 150;
	proof.useColor = false;
	proof.outputSeparations = false;
	proof.separationName = "All";
	proof.toFile = false;
	proof.copies = dialog.copies();
	proof.cropMarks = false;
	proof.bleedMarks = false;
	proof.registrationMarks = false;
	proof.colorMarks = false;
	proof.useDocBleeds = false;
	proof.bleeds.resetToZero();
	proof.printer = dialog.printerName();
	proof.proofMedia = dialog.paperName();
	proof.inputSlot = dialog.inputSlot();
	proof.pageNumbers.clear();
	proof.pageNumbers.push_back(doc->currentPage()->pageNr() + 1);

	// Scaling follows the paper that was chosen, not the biggest the queue
	// could take: an A3 printer fed A4 must scale to A4.
	QSizeF sheet = dialog.paperSizePoints();
	if (sheet.isEmpty())
		sheet = QSizeF(595.276, 841.89);   // A4 in points
	proof.proofPaperWidth = sheet.width();
	proof.proofPaperHeight = sheet.height();
	// "Reduce to fit paper": one sheet per page, scaled down (and turned if
	// that fits larger) - see proof_fit.h.
	proof.proofReduceToFit = dialog.reduceToFit();
	// A reduced proof is one sheet per page, so the print settings' tiling
	// of a page larger than the paper is switched off for it.
	if (proof.proofReduceToFit)
		proof.offsetTileEnabled = false;

	m_mainWindowStatusLabel->setText( tr("Proof printing..."));
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	ReOrderText(doc, view);
	QString printError;
	bool done = doPrint(proof, printError);
	QApplication::restoreOverrideCursor();
	m_PrinterUsed = true;
	if (!done)
	{
		QString message = tr("Proof printing failed!");
		if (!printError.isEmpty())
			message += QString("\n%1").arg(printError);
		ScMessageBox::warning(this, CommonStrings::trWarning, message);
	}

	// Say what just went out. Status bar rather than a message box: the dialog
	// is already the interaction, nothing should follow it. Marks and bleeds
	// are off for proofs, so the page box here is the same one PSLib scales and
	// this figure matches the slug.
	if (done)
	{
		double srcW = doc->currentPage()->width();
		double srcH = doc->currentPage()->height();
		int scalePercent = 100;
		if (proof.proofReduceToFit)
			scalePercent = qRound(proofFitOnSheet(srcW, srcH, proof.proofPaperWidth, proof.proofPaperHeight).scale * 100.0);
		else if ((srcW > 0.0) && (srcH > 0.0))
		{
			double fit = qMin((proof.proofPaperWidth - 24.0) / srcW,
			                  (proof.proofPaperHeight - 24.0) / srcH);
			scalePercent = qRound(qMin(1.0, fit) * 100.0);
		}
		QString paperText = dialog.paperAssumed()
			? tr("%1 - paper unknown, assumed").arg(proof.proofMedia)
			: proof.proofMedia;
		QString slotText = proof.inputSlot.isEmpty() ? QString() : QString(", %1").arg(proof.inputSlot);
		m_mainWindowStatusLabel->setText(tr("Proof -> %1 (%2%3) at %4%")
			.arg(proof.printer, paperText, slotText, QString::number(scalePercent)));
	}
	else
		m_mainWindowStatusLabel->setText( tr("Ready"));
}

bool ScribusMainWindow::doPrint(PrintOptions &options, QString& error)
{
	bool printDone = false;
	QString filename(options.filename);
	if (options.toFile)
	{
		QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
		if (!overwrite(this, filename))
		{
			QApplication::restoreOverrideCursor();
			return true;
		}
		QApplication::restoreOverrideCursor();
	}
	ScCore->fileWatcher->forceScan();
	ScCore->fileWatcher->stop();
	ScPrintEngine* prnEngine = nullptr;
#if defined(_WIN32)
	if (doc->Print_Options.toFile && (options.prnLanguage == PrintLanguage::PDF))
		prnEngine = dynamic_cast<ScPrintEngine*>(new ScPrintEngine_PDF(*doc));
	else if (doc->Print_Options.toFile)
		prnEngine = dynamic_cast<ScPrintEngine*>(new ScPrintEngine_PS(*doc));
	else
		prnEngine = dynamic_cast<ScPrintEngine*>(new ScPrintEngine_GDI(*doc));
#else
	if (options.prnLanguage == PrintLanguage::PDF)
		prnEngine = qobject_cast<ScPrintEngine*>(new ScPrintEngine_PDF(*doc));
	else
		prnEngine = qobject_cast<ScPrintEngine*>(new ScPrintEngine_PS(*doc));
#endif
	if (prnEngine)
	{
		printDone = prnEngine->print(options);
		if (!printDone)
			error = prnEngine->errorMessage();
		delete prnEngine;
	}
	else
		error = tr( "Print engine initialization failed");
	ScCore->fileWatcher->start();
	return printDone;
}

void ScribusMainWindow::slotFileQuit()
{
	ScCore->pluginManager->savePreferences();
	close();
}

void ScribusMainWindow::slotEditCut()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	if (doc->m_Selection->isEmpty())
		return;
	int docSelectionCount = doc->m_Selection->count();
	UndoTransaction activeTransaction;
	PageItem *currItem;
	for (int i = 0; i < docSelectionCount; ++i)
	{
		currItem = doc->m_Selection->itemAt(i);
		if ((currItem->isTextFrame() || currItem->isPathText()) && currItem == storyEditor->currentItem() && doc == storyEditor->currentDocument())
		{
			ScMessageBox::critical(this, tr("Cannot Cut In-Use Item"), tr("The item %1 is currently being edited by Story Editor. The cut operation will be cancelled").arg(currItem->itemName()));
			return;
		}
	}
	if (UndoManager::undoEnabled())
	{
		if (docSelectionCount > 1)
			activeTransaction = m_undoManager->beginTransaction(Um::SelectionGroup, Um::IGroup, Um::Cut, QString(), Um::ICut);
		else
		{
			PageItem* item = doc->m_Selection->itemAt(0);
			activeTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(), Um::Cut, QString(), Um::ICut);
		}
	}
	currItem = doc->m_Selection->itemAt(0);
	if (((doc->appMode == modeEdit) || (doc->appMode == modeEditTable)) && (currItem->isTextFrame() || currItem->isTable()))
	{
		PageItem_TextFrame *cItem;
		if (doc->appMode == modeEditTable)
			cItem = currItem->asTable()->activeCell().textFrame();
		else
			cItem = currItem->asTextFrame();
		if (cItem->HasSel)
		{
			if (cItem->itemText.length() == 0)
				return;
			StoryText itemText(doc);
			itemText.setDefaultStyle(cItem->itemText.defaultStyle());
			itemText.insert(0, cItem->itemText, true);

			QByteArray storyData;
			QScopedPointer<StoryLoader> storyLoader(new StoryLoader());
			if (storyLoader->saveStory(storyData, *doc, itemText))
			{
				ScTextMimeData* mimeData = new ScTextMimeData();
				mimeData->setScribusText(storyData);
				mimeData->setText(itemText.text(0, itemText.length()));
				QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
				cItem->deleteSelectedTextFromFrame();
				if (doc->appMode == modeEditTable)
					currItem->asTable()->update();
				else
					cItem->update();
			}
		}
	}
	else
	{
		if ((currItem->isSingleSel) && (currItem->isGroup()))
			return;
		QString BufferS = ScriXmlDoc::writeElem(doc, doc->m_Selection);
		if ((m_prefsManager.appPrefs.scrapbookPrefs.doCopyToScrapbook) && (!internalCopy))
		{
			scrapbookPalette->objFromCopyAction(BufferS, currItem->itemName());
			rebuildRecentPasteMenu();
		}
		ScElemMimeData* mimeData = new ScElemMimeData();
		mimeData->setScribusElem(BufferS);
		QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
		for (int i=0; i < doc->m_Selection->count(); ++i)
		{
			PageItem* frame = doc->m_Selection->itemAt(i);
			if (frame->asTextFrame() && frame->prevInChain() == nullptr)
				frame->clearContents();
		}
		doc->itemSelection_DeleteItem();
	}
	slotDocCh();
	slotPreviewCh();
	scrActions["editPaste"]->setEnabled(true);
	scrActions["editPasteOriginalPosition"]->setEnabled(true);
	scrActions["editPastePlainText"]->setEnabled(true);
	scrMenuMgr->setMenuEnabled("EditPasteRecent", scrapbookPalette->tempBView->objectMap.count() != 0);
	if (activeTransaction)
		activeTransaction.commit();
}

// Suneer: in-memory styled copy/paste buffer.
// Holds one serialized StoryText fragment (SLA XML). Process lifetime, in-memory
// only (never persisted, never on the system clipboard). Survives selection changes,
// document switches and document closes because it is plain bytes re-parsed into the
// target document on paste; cross-document paste therefore works.
static QByteArray s_suneerStyledClip;

// Suneer: snapshot of the source text frame's geometry/layout properties, captured
// alongside the styled text on Ctrl+Shift+C and re-applied to the target frame on
// Ctrl+Shift+V (keeping the target's top-left position and PageItem identity).
namespace {
struct SuneerFrameSnapshot
{
	bool   valid = false;
	double width = 0.0, height = 0.0;
	int    columns = 1;
	double columnGap = 0.0;
	double distL = 0.0, distR = 0.0, distT = 0.0, distB = 0.0;   // internal padding
	double wrapT = 0.0, wrapB = 0.0, wrapL = 0.0, wrapR = 0.0;   // external wrap offsets
	int    textFlowMode = 0;                                     // PageItem::TextFlowMode
};
}
static SuneerFrameSnapshot s_suneerFrameSnap;

// Suneer: flatten a CharStyle into fully self-contained direct formatting.
// Every attribute is read through its getter (which validates against the source
// document's style context and returns the RESOLVED/effective value) and written
// back as a direct override with no named parent, so the result stays visually
// identical when pasted into a document with no matching style names.
static CharStyle suneerFlattenCharStyle(const CharStyle& src)
{
	CharStyle flat;
	flat.setParent(QString());
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT, attr_BREAKSHAPING) \
	flat.set##attr_NAME(src.attr_GETTER());
#include "styles/charstyle.attrdefs.cxx"
#undef ATTRDEF
	return flat;
}

// Suneer: flatten a ParagraphStyle (and its embedded char style) into resolved
// direct formatting, dropping the named-parent link. Name references to other
// styles that this style may carry (next style, numbering, paragraph-effect char
// style) are copied verbatim; since every visible attribute is now a direct
// override, a missing/mismatched parent in the target document is harmless.
static ParagraphStyle suneerFlattenParagraphStyle(const ParagraphStyle& src)
{
	ParagraphStyle flat;
	flat.setParent(QString());
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	flat.set##attr_NAME(src.attr_GETTER());
#include "styles/paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	flat.charStyle() = suneerFlattenCharStyle(src.charStyle());
	return flat;
}

// Suneer: bake fully-resolved direct formatting into every character run and paragraph of
// `clip`. The resolved values are read from the LIVE SOURCE story (`src`) at the matching
// positions, NOT from `clip` itself: a freshly copied clip keeps inherited attributes
// (e.g. font size taken from a paragraph/named style) which resolve against the clip's own
// context and wrongly fall back to the document default (18pt). `clip` was produced by
// `clip.insert(0, src, onlySelection=true)`, which copies the selected range contiguously,
// so clip position i maps to source position `srcOffset + i` (srcOffset = start of selection).
static void suneerFlattenStoryText(StoryText& clip, const StoryText& src, int srcOffset)
{
	const int len = clip.length();
	for (int i = 0; i < len; ++i)
		clip.setCharStyle(i, 1, suneerFlattenCharStyle(src.charStyle(srcOffset + i)));

	int pos = 0;
	while (true)
	{
		clip.setStyle(pos, suneerFlattenParagraphStyle(src.paragraphStyle(srcOffset + pos)));
		int e = pos;
		while (e < len && clip.text(e) != SpecialChars::PARSEP)
			++e;
		if (e >= len)
			break; // last (trailing) paragraph already flattened above
		pos = e + 1;
	}
}

void ScribusMainWindow::enforceClipboardShortcuts()
{
	auto& keyActions = m_prefsManager.appPrefs.keyShortcutPrefs.KeyActions;
	ScrAction* styledCopy = scrActions.value("editStyledCopy");
	ScrAction* styledPaste = scrActions.value("editStyledPaste");

	// Styled Copy/Paste keep whatever keys the user's set gives them; only
	// when a set leaves them with none (e.g. a stock keyset that predates
	// them) do they get their defaults back. This used to force
	// Ctrl+Shift+C/V on every startup, which overwrote a user's own choice.
	const QKeySequence copySeq = (styledCopy && !styledCopy->shortcut().isEmpty())
		? styledCopy->shortcut() : QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
	const QKeySequence pasteSeq = (styledPaste && !styledPaste->shortcut().isEmpty())
		? styledPaste->shortcut() : QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V);

	// Keep the two clipboard actions the sole owners of their keys, so Qt
	// cannot report an ambiguous shortcut.
	for (auto it = scrActions.constBegin(); it != scrActions.constEnd(); ++it)
	{
		const QString& name = it.key();
		ScrAction* a = it.value();
		if (!a)
			continue;
		if (name == "editStyledCopy" || name == "editStyledPaste")
			continue;
		if (a->shortcut() == copySeq || a->shortcut() == pasteSeq)
		{
			a->setShortcut(QKeySequence());
			if (keyActions.contains(name))
				keyActions[name].keySequence = QKeySequence();
		}
	}
	if (styledCopy)
		styledCopy->setShortcut(copySeq);
	if (styledPaste)
		styledPaste->setShortcut(pasteSeq);
	if (keyActions.contains("editStyledCopy"))
		keyActions["editStyledCopy"].keySequence = copySeq;
	if (keyActions.contains("editStyledPaste"))
		keyActions["editStyledPaste"].keySequence = pasteSeq;
}

void ScribusMainWindow::applyShortcutsFromPrefs()
{
	m_prefsManager.applyLoadedShortCuts();
	enforceClipboardShortcuts();
}

QString ScribusMainWindow::suneerActionOwningShortcut(const QKeySequence& key) const
{
	if (key.isEmpty())
		return QString();
	for (auto it = scrActions.constBegin(); it != scrActions.constEnd(); ++it)
	{
		if (it.value() && it.value()->shortcut() == key)
			return it.key();
	}
	return QString();
}

int ScribusMainWindow::suneerLogShortcutConflicts(const QString& when)
{
	// Only shortcuts that can actually fire in THIS window count. An action
	// that is attached to no widget never fires (the Style Manager keeps one
	// such "Apply" action per style key), and an action on a widget of
	// another top-level window (Story Editor, script console) is scoped to
	// that window, so neither can make a main-window key ambiguous.
	auto livesInThisWindow = [this](const QAction* a) -> bool
	{
		const QList<QObject*> objs = a->associatedObjects();
		for (const QObject* o : objs)
		{
			const QWidget* w = qobject_cast<const QWidget*>(o);
			for (; w; w = w->parentWidget())
			{
				if (w == this)
					return true;
				if (w->isWindow() && !qobject_cast<const QMenu*>(w))
					break;                      // some other top-level window
			}
		}
		return false;
	};

	QMap<QString, QStringList> owners;       // key -> live claimants
	QMap<QString, QStringList> everyone;     // key -> all claimants, for the debug dump
	QSet<const QObject*> seenActions;
	for (auto it = scrActions.constBegin(); it != scrActions.constEnd(); ++it)
	{
		if (!it.value())
			continue;
		seenActions.insert(it.value());
		const QKeySequence ks = it.value()->shortcut();
		if (ks.isEmpty())
			continue;
		const QString key = ks.toString(QKeySequence::PortableText);
		const bool attached = !it.value()->associatedObjects().isEmpty();
		everyone[key] << QString("action %1 (%2, %3)").arg(it.key(), attached ? "attached" : "NOT attached to any widget", it.value()->isEnabled() ? "enabled" : "disabled now");
		if (attached)
			owners[key] << QString("action %1").arg(it.key());
	}
	const QList<QAction*> otherActions = findChildren<QAction*>();
	for (const QAction* a : otherActions)
	{
		if (seenActions.contains(a) || a->shortcut().isEmpty())
			continue;
		const QString key = a->shortcut().toString(QKeySequence::PortableText);
		const QString label = QString("QAction '%1'").arg(a->objectName().isEmpty() ? a->text() : a->objectName());
		const bool live = livesInThisWindow(a);
		everyone[key] << label + (live ? " (in this window)" : " (other window or unattached - cannot collide)");
		if (live)
			owners[key] << label;
	}
	// Our own QShortcuts: paragraph style keys, Next Style Chain keys, Column
	// Style config keys - and anything else created with new QShortcut.
	const QList<QShortcut*> shortcuts = findChildren<QShortcut*>();
	for (const QShortcut* sc : shortcuts)
	{
		if (sc->key().isEmpty())
			continue;
		QString label = sc->objectName();
		if (label.isEmpty())
			label = QString("QShortcut on %1").arg(sc->parent() ? sc->parent()->metaObject()->className() : "?");
		const QString key = sc->key().toString(QKeySequence::PortableText);
		const QWidget* pw = qobject_cast<const QWidget*>(sc->parent());
		const bool live = sc->isEnabled() && (sc->context() == Qt::ApplicationShortcut || (pw && pw->window() == this));
		everyone[key] << label + (live ? "" : " (disabled or other window)");
		if (live)
			owners[key] << label;
	}

	// SUNEER_SHORTCUT_DEBUG="Ctrl+Alt+F,Ctrl+Alt+I": print every claimant of
	// these keys, live or not, each time the check runs.
	const QString debugKeys = qEnvironmentVariable("SUNEER_SHORTCUT_DEBUG");
	if (!debugKeys.isEmpty())
	{
		const QStringList keys = debugKeys.split(',', Qt::SkipEmptyParts);
		for (const QString& k : keys)
		{
			const QString norm = QKeySequence(k.trimmed()).toString(QKeySequence::PortableText);
			qWarning().noquote() << QString("[SHORTCUT DEBUG] %1 (%2): %3").arg(norm, when,
				everyone.value(norm).isEmpty() ? QString("nobody") : everyone.value(norm).join(" | "));
		}
	}

	QStringList report;
	for (auto it = owners.constBegin(); it != owners.constEnd(); ++it)
	{
		if (it.value().count() > 1)
			report << QString("[SHORTCUT CONFLICT] %1 is claimed by: %2").arg(it.key(), it.value().join(" | "));
	}
	// Same findings as last time: say nothing (this runs after every rebuild).
	static QString lastReport = QStringLiteral("-");
	const QString joined = report.join('\n');
	if (joined != lastReport)
	{
		lastReport = joined;
		if (report.isEmpty())
			qWarning().noquote() << QString("[SHORTCUT CHECK] %1: no duplicate shortcuts").arg(when);
		else
		{
			qWarning().noquote() << QString("[SHORTCUT CHECK] %1: %2 key(s) claimed more than once - Qt fires none of the claimants").arg(when).arg(report.count());
			for (const QString& line : report)
				qWarning().noquote() << line;
		}
	}
	return report.count();
}

void ScribusMainWindow::applyDefaultShortcutSet()
{
	// Last thing at startup that touches shortcuts: the Default set wins over
	// the shortcuts saved in scribus172.rc, the first-run keyset, and the
	// clipboard fix-up above.
	if (!Prefs_KeyboardShortcuts::applyDefaultSet(m_prefsManager.appPrefs.keyShortcutPrefs.KeyActions))
		return;
	applyShortcutsFromPrefs();
	// A newer shipped "dbi" than last time: already in effect if it is the
	// Default (built from the file just now); otherwise say so once.
	const QString note = Prefs_KeyboardShortcuts::noteShippedSetUpdate();
	if (!note.isEmpty())
	{
		qWarning().noquote() << "[Shortcuts]" << note;
		QTimer::singleShot(2500, this, [this, note] {
			if (Prefs_KeyboardShortcuts::defaultIsShippedSet())
				setStatusBarInfoText(note);
			else
				ScMessageBox::information(this, tr("Keyboard Shortcuts"), note);
		});
	}
}

void ScribusMainWindow::slotEditStyledCopy()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	if (doc->m_Selection->isEmpty())
		return;
	PageItem *currItem = doc->m_Selection->itemAt(0);
	// R4: only meaningful inside a text frame / table cell being edited
	if (!(((doc->appMode == modeEdit) || (doc->appMode == modeEditTable)) && (currItem->isTextFrame() || currItem->isTable())))
		return;

	PageItem_TextFrame *cItem;
	if (doc->appMode == modeEditTable)
		cItem = currItem->asTable()->activeCell().textFrame();
	else
		cItem = currItem->asTextFrame();
	// R1/R2: nothing selected -> do nothing, no dialog, no state mutation (buffer + snapshot kept).
	if (!cItem || !cItem->HasSel)
	{
		setStatusBarInfoText( tr("Nothing selected to copy"));
		return;
	}

	// Extract the styled selection into a private StoryText.
	StoryText itemText(doc);
	itemText.setDefaultStyle(cItem->itemText.defaultStyle());
	itemText.insert(0, cItem->itemText, true);

	// Bake resolved formatting so the buffer is self-contained across documents. Read the
	// resolved values from the live source frame (its style context resolves correctly),
	// not from the freshly-copied clip (whose inherited attributes fall back to the document
	// default). Selection is copied contiguously, so clip[i] == source[startOfSelection()+i].
	suneerFlattenStoryText(itemText, cItem->itemText, cItem->itemText.startOfSelection());

	// Snapshot the source frame's geometry/layout properties (R1: single snapshot from the
	// source frame, regardless of selection size). Read-only on the source (R9).
	SuneerFrameSnapshot snap;
	snap.valid        = true;
	snap.width        = currItem->width();
	snap.height       = currItem->height();
	snap.columns      = currItem->columns();
	snap.columnGap    = currItem->columnGap();
	snap.distL        = currItem->textToFrameDistLeft();
	snap.distR        = currItem->textToFrameDistRight();
	snap.distT        = currItem->textToFrameDistTop();
	snap.distB        = currItem->textToFrameDistBottom();
	snap.wrapT        = currItem->wrapOffsetTop();
	snap.wrapB        = currItem->wrapOffsetBottom();
	snap.wrapL        = currItem->wrapOffsetLeft();
	snap.wrapR        = currItem->wrapOffsetRight();
	snap.textFlowMode = static_cast<int>(currItem->textFlowMode());

	// Serialize to the in-memory buffer (not the system clipboard). Text + snapshot are
	// committed together so they never get out of sync.
	QByteArray storyData;
	QScopedPointer<StoryLoader> storyLoader(new StoryLoader());
	if (storyLoader->saveStory(storyData, *doc, itemText))
	{
		s_suneerStyledClip = storyData;
		s_suneerFrameSnap  = snap;
		scrActions["editStyledPaste"]->setEnabled(true);
		setStatusBarInfoText( tr("Styled text and frame properties copied"));
	}
}

void ScribusMainWindow::slotEditStyledPaste()
{
	if (!HaveDoc)
		return;
	// R6: empty buffer -> do nothing, no error
	if (s_suneerStyledClip.isEmpty())
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);

	// Two context-sensitive modes:
	//   CASE 2 (edit mode, cursor inside a text frame / table cell): insert the styled text at
	//          the cursor as standalone paragraph(s); no frame geometry is touched.
	//   CASE 1 (a text frame merely selected in normal mode): apply the source frame's layout
	//          (except width), overwrite the content, and auto-fit the height.
	const bool editMode = (doc->appMode == modeEdit || doc->appMode == modeEditTable);

	// R3/R5: the target must be a text frame (the edited frame / active cell, or the selected item).
	PageItem *selItem = doc->m_Selection->itemAt(0);
	PageItem_TextFrame *currItem = nullptr;
	if (selItem)
	{
		if (doc->appMode == modeEditTable && selItem->isTable())
			currItem = selItem->asTable()->activeCell().textFrame();
		else if (selItem->isTextFrame())
			currItem = selItem->asTextFrame();
	}
	if (!currItem)
		return;

	// Re-parse the buffer into the *target* document so styles bind to the
	// target's context (this is what makes cross-document paste safe).
	StoryText story(doc);
	QScopedPointer<StoryLoader> storyLoader(new StoryLoader());
	if (!storyLoader->loadStory(s_suneerStyledClip, *doc, story, currItem))
		return;
	if (story.length() == 0)
		return;

	// Avoid pasting note marks into notes frames (mirror standard paste).
	if (currItem->isNoteFrame())
	{
		for (int pos = story.length() - 1; pos >= 0; --pos)
		{
			if (story.hasMark(pos) && (story.mark(pos)->isNoteType()))
				story.removeChars(pos, 1);
		}
	}

	// R7: wrap the whole operation in ONE undo transaction (both cases).
	UndoTransaction activeTransaction;
	if (UndoManager::undoEnabled())
		activeTransaction = m_undoManager->beginTransaction(doc->currentPage()->getUName(), nullptr, Um::Paste, QString(), Um::IPaste);

	if (editMode)
	{
		// ---- CASE 2: insert at cursor as standalone paragraph(s); frame geometry untouched (R3). ----
		if (currItem->HasSel)
			currItem->deleteSelectedTextFromFrame();

		int start = currItem->itemText.cursorPosition();
		const int len = currItem->itemText.length();
		// Bracket the pasted block so it forms its own paragraph(s) without restyling neighbours:
		//  - a leading break if the cursor is not already at the start of a paragraph;
		//  - a trailing break (unless the story already ends in one) if the cursor is not at a
		//    paragraph end, so the remainder of the split paragraph continues on its own.
		const bool atParaStart  = (start == 0) || (currItem->itemText.text(start - 1) == SpecialChars::PARSEP);
		const bool atParaEnd    = (start >= len) || (currItem->itemText.text(start) == SpecialChars::PARSEP);
		const bool storyEndsPar = (story.text(story.length() - 1) == SpecialChars::PARSEP);
		// Style kept by the "before" half of the split (= the target paragraph's own style).
		ParagraphStyle keepPStyle(currItem->itemText.paragraphStyle(start));
		// Style of the pasted block's last paragraph (so it keeps its OWN style, not the target's).
		ParagraphStyle storyLastPStyle(story.paragraphStyle(story.length()));

		// Build the exact block to insert as ONE contiguous StoryText, so the single PASTE_TEXT
		// undo record covers it precisely (one Ctrl+Z reverts the whole insertion).
		StoryText ins(doc);
		ins.setDefaultStyle(story.defaultStyle());
		int p = 0;
		if (!atParaStart)
		{
			// Leading break terminates the "before" text -> carries the target paragraph style.
			ins.insertChars(p, SpecialChars::PARSEP);
			ins.applyStyle(p, keepPStyle);
			++p;
		}
		ins.insert(p, story);
		const int contentEnd = ins.length();   // cursor should land here (end of the pasted text)
		if (!storyEndsPar && !atParaEnd)
		{
			// Trailing break terminates the pasted block -> carries the block's OWN last-para style;
			// the "after" remainder then continues under the target paragraph style.
			ins.insertChars(ins.length(), SpecialChars::PARSEP);
			ins.applyStyle(contentEnd, storyLastPStyle);
		}

		if (UndoManager::undoEnabled())
		{
			auto *is = new ScItemState<StoryText>(Um::Paste);
			is->set("PASTE_TEXT");
			is->set("START", start);
			is->setItem(ins);
			m_undoManager->action(currItem, is);
		}
		currItem->itemText.insert(start, ins);
		currItem->itemText.setCursorPosition(start + contentEnd);
	}
	else
	{
		// ---- CASE 1: apply source frame layout (keep drawn width), overwrite content, autofit. ----
		const SuneerFrameSnapshot& snap = s_suneerFrameSnap;
		if (snap.valid)
		{
			// setWrapOffsets() now records its own state, so no explicit one here.

			// R2: keep the target's top-left AND its drawn WIDTH; take everything else from source.
			currItem->setColumns(snap.columns);
			currItem->setColumnGap(snap.columnGap);
			currItem->setTextToFrameDist(snap.distL, snap.distR, snap.distT, snap.distB);
			currItem->setTextFlowMode(static_cast<PageItem::TextFlowMode>(snap.textFlowMode));
			currItem->setWrapOffsets(snap.wrapT, snap.wrapB, snap.wrapL, snap.wrapR);
		}

		// R5: full content overwrite (records DELETE_FRAMETEXT + PASTE_TEXT in the transaction).
		currItem->itemText.selectAll();
		currItem->deleteSelectedTextFromFrame();
		currItem->itemText.setCursorPosition(0);
		if (UndoManager::undoEnabled())
		{
			auto *is = new ScItemState<StoryText>(Um::Paste);
			is->set("PASTE_TEXT");
			is->set("START", 0);
			is->setItem(story);
			m_undoManager->action(currItem, is);
		}
		currItem->itemText.insert(0, story);
		currItem->itemText.setCursorPosition(story.length());

		// Auto-fit the height to the new content at the kept width (nests its own resize undo).
		currItem->updateClip();
		currItem->invalidateLayout();
		currItem->autoFitFrameHeight();
	}

	if (activeTransaction)
		activeTransaction.commit();

	// Refresh: relayout the frame and repaint.
	currItem->invalidateLayout();
	currItem->updateClip();
	currItem->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
	view->DrawNew();
	slotDocCh(false);
	slotPreviewCh();
}

void ScribusMainWindow::slotEditCopy()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	if (doc->m_Selection->isEmpty())
		return;
	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (((doc->appMode == modeEdit) || (doc->appMode == modeEditTable)) && (currItem->isTextFrame() || currItem->isTable()))
	{
		PageItem_TextFrame *cItem;
		if (doc->appMode == modeEditTable)
			cItem = currItem->asTable()->activeCell().textFrame();
		else
			cItem = currItem->asTextFrame();
		if (cItem->HasSel)
		{
			StoryText itemText(doc);
			itemText.setDefaultStyle(cItem->itemText.defaultStyle());
			itemText.insert(0, cItem->itemText, true);

			QByteArray storyData;
			QScopedPointer<StoryLoader> storyLoader(new StoryLoader());
			if (storyLoader->saveStory(storyData, *doc, itemText))
			{
				ScTextMimeData* mimeData = new ScTextMimeData();
				mimeData->setScribusText(storyData);
				mimeData->setText(itemText.text(0, itemText.length()));
				QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
			}
		}
	}
	else
	{
		if ((currItem->isSingleSel) && (currItem->isGroup()))
			return;

		// Do not copy notes frames
		if ((doc->m_Selection->count() == 1) && currItem->isNoteFrame())
			return;
		
		// Sort items in Z-order
		QList<PageItem*> selectedItems = doc->m_Selection->items();
		std::stable_sort(selectedItems.begin(), selectedItems.end(), compareItemLevel);

		Selection tempSelection(this, false);
		for (int i = 0; i < selectedItems.count(); ++i)
			tempSelection.addItem(selectedItems.at(i));

		// Deselect notesframes
		for (int i = 0; i < doc->m_Selection->count(); ++i)
		{
			if (doc->m_Selection->itemAt(i)->isNoteFrame())
				tempSelection.removeItem(doc->m_Selection->itemAt(i));
		}

		QString BufferS = ScriXmlDoc::writeElem(doc, &tempSelection);
		if (!internalCopy)
		{
			if ((m_prefsManager.appPrefs.scrapbookPrefs.doCopyToScrapbook) && (!internalCopy))
			{
				scrapbookPalette->objFromCopyAction(BufferS, currItem->itemName());
				rebuildRecentPasteMenu();
			}
			ScElemMimeData* mimeData = new ScElemMimeData();
			mimeData->setScribusElem(BufferS);
			QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
		}
		else
			internalCopyBuffer = BufferS;
	}
	if (!internalCopy)
	{
		scrActions["editPaste"]->setEnabled(true);
	scrActions["editPasteOriginalPosition"]->setEnabled(true);
		scrActions["editPastePlainText"]->setEnabled(true);
		scrMenuMgr->setMenuEnabled("EditPasteRecent", scrapbookPalette->tempBView->objectMap.count() != 0);
	}
}

void ScribusMainWindow::slotEditPaste(bool forcePlainText)
{
	doEditPaste(forcePlainText, false);
}

void ScribusMainWindow::slotEditPasteOriginalPosition()
{
	doEditPaste(false, true);
}

void ScribusMainWindow::doEditPaste(bool forcePlainText, bool pasteInOriginalPosition)
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	UndoTransaction activeTransaction;
	if (!ScMimeData::clipboardHasScribusData() && (!internalCopy))
		return;
	if (UndoManager::undoEnabled())
		activeTransaction = m_undoManager->beginTransaction(doc->currentPage()->getUName(), nullptr, Um::Paste, QString(), Um::IPaste);
	PageItem* selItem = doc->m_Selection->itemAt(0);
	if (((doc->appMode == modeEdit) || (doc->appMode == modeEditTable)) && selItem && (selItem->isTextFrame() || selItem->isTable()))
	{
		PageItem_TextFrame *currItem;
		if (doc->appMode == modeEditTable)
			currItem = selItem->asTable()->activeCell().textFrame();
		else
			currItem = selItem->asTextFrame();
		assert(currItem != nullptr);
		if (currItem->HasSel)
		{
			//removing marks and notes from selected text
//			if (currItem->isTextFrame() && !currItem->asTextFrame()->removeMarksFromText(!ScCore->usingGUI()))
//				return;
			currItem->deleteSelectedTextFromFrame();
		}
		if (ScMimeData::clipboardHasScribusText())
		{
			StoryText story(doc);
			QScopedPointer<StoryLoader> storyLoader(new StoryLoader());

			QByteArray xml = ScMimeData::clipboardScribusText();
			if (storyLoader->loadStory(xml, *doc, story, currItem))
			{
				//avoid pasting notes marks into notes frames
				if (currItem->isNoteFrame())
				{
					for (int pos = story.length() - 1; pos >= 0; --pos)
					{
						if (story.hasMark(pos) && (story.mark(pos)->isNoteType()))
							story.removeChars(pos, 1);
					}
				}
				if (UndoManager::undoEnabled())
				{
					auto *is = new ScItemState<StoryText>(Um::Paste);
					is->set("PASTE_TEXT");
					is->set("START", currItem->itemText.cursorPosition());
					is->setItem(story);
					m_undoManager->action(currItem, is);
				}
				if (forcePlainText)
					currItem->itemText.insertChars(story.text(0, story.length()));
				else
					currItem->itemText.insertChars(story.text(0, story.length()));
			}
		}
		else if (ScMimeData::clipboardHasScribusElem() || ScMimeData::clipboardHasScribusFragment())
		{
			bool savedAlignGrid = doc->SnapGrid;
			bool savedAlignGuides = doc->SnapGuides;
			bool savedAlignElement = doc->SnapItems;
			int ac = doc->Items->count();
			bool isGroup = false;
			double gx, gy, gh, gw;
			FPoint minSize = doc->minCanvasCoordinate;
			FPoint maxSize = doc->maxCanvasCoordinate;
			doc->SnapGrid = false;
			doc->SnapGuides = false;
			doc->SnapItems = false;
			// HACK #6541 : undo does not handle text modification => do not record embedded item creation
			// if embedded item is deleted, undo system will not be aware of its deletion => crash - JG
			m_undoManager->setUndoEnabled(false);
			QString buffer  = ScMimeData::clipboardScribusElem();
			slotElemRead(buffer, 0, 0, false, true, doc, view);

			doc->SnapGrid = savedAlignGrid;
			doc->SnapGuides = savedAlignGuides;
			doc->SnapItems = savedAlignElement;
			Selection tempSelection(*doc->m_Selection);
			doc->m_Selection->clear();
			if (doc->Items->count() - ac > 1)
				isGroup = true;
			doc->m_Selection->delaySignalsOn();
			for (int as = ac; as < doc->Items->count(); ++as)
			{
				doc->m_Selection->addItem(doc->Items->at(as));
			}
			if (isGroup)
				doc->GroupCounter++;
			doc->m_Selection->getGroupRect(&gx, &gy, &gw, &gh);
			PageItem* currItem3 = doc->Items->at(ac);
			currItem3->isEmbedded = true;
			currItem3->setIsAnnotation(false);
			currItem3->isBookmark = false;
			currItem3->gXpos = currItem3->xPos() - gx;
			currItem3->gYpos = currItem3->yPos() - gy;
			currItem3->gWidth = gw;
			currItem3->gHeight = gh;
			int fIndex = doc->addToInlineFrames(currItem3);
			int acc = doc->Items->count();
			for (int as = ac; as < acc; ++as)
			{
				doc->Items->takeAt(ac);
			}
			doc->m_Selection->clear();
			*doc->m_Selection = tempSelection;
			doc->minCanvasCoordinate = minSize;
			doc->maxCanvasCoordinate = maxSize;
			if (outlinePalette->isVisible())
				outlinePalette->BuildTree();
			m_undoManager->setUndoEnabled(true);
			if (UndoManager::undoEnabled())
			{
				auto *is = new SimpleState(Um::Paste, QString(), Um::IPaste);
				is->set("PASTE_INLINE");
				is->set("START", currItem->itemText.cursorPosition());
				is->set("INDEX", fIndex);
				m_undoManager->action(currItem, is);
			}
			currItem->itemText.insertObject(fIndex);
			doc->m_Selection->delaySignalsOff();
		}
		else if (ScMimeData::clipboardHasHTML() && !forcePlainText)
		{
			ScClipboardProcessor scclipproc(doc, currItem);
			QString clipContent = QApplication::clipboard()->mimeData()->html();
			scclipproc.setContent(clipContent, ScClipboardProcessor::ContentType::HTML);
			scclipproc.process();
		}
		else if (ScMimeData::clipboardHasKnownData())
		{
			bool savedAlignGrid = doc->SnapGrid;
			bool savedAlignGuides = doc->SnapGuides;
			bool savedAlignElement = doc->SnapItems;
			FPoint minSize = doc->minCanvasCoordinate;
			FPoint maxSize = doc->maxCanvasCoordinate;
			doc->SnapGrid = false;
			doc->SnapGuides = false;
			doc->SnapItems = false;
			QString ext = ScMimeData::clipboardKnownDataExt();
			QByteArray bitsBits = ScMimeData::clipboardKnownDataData();
			double x0 = (view->contentsX() / view->scale()) + ((view->visibleWidth() / 2.0) / view->scale());
			double y0 = (view->contentsY() / view->scale()) + ((view->visibleHeight() / 2.0) / view->scale());
			PageItem *retObj = getVectorFileFromData(doc, bitsBits, ext, x0, y0);
			if (retObj != nullptr)
			{
				double x = (view->contentsX() / view->scale()) + ((view->visibleWidth() / 2.0) / view->scale()) - (retObj->width() / 2.0);
				double y = (view->contentsY() / view->scale()) + ((view->visibleHeight() / 2.0) / view->scale()) - (retObj->height() / 2.0);
				retObj->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
				retObj->setXYPos(x, y, true);
				doc->SnapGrid = savedAlignGrid;
				doc->SnapGuides = savedAlignGuides;
				doc->SnapItems = savedAlignElement;
				Selection tempSelection(*doc->m_Selection);
				doc->m_Selection->clear();
				doc->m_Selection->delaySignalsOn();
				doc->m_Selection->addItem(retObj);
				double gx, gy, gh, gw;
				doc->m_Selection->getGroupRect(&gx, &gy, &gw, &gh);
				retObj->isEmbedded = true;
				retObj->setIsAnnotation(false);
				retObj->isBookmark = false;
				retObj->gXpos = retObj->xPos() - gx;
				retObj->gYpos = retObj->yPos() - gy;
				retObj->gWidth = gw;
				retObj->gHeight = gh;
				int fIndex = doc->addToInlineFrames(retObj);
				doc->Items->removeAll(retObj);
				doc->m_Selection->clear();
				*doc->m_Selection = tempSelection;
				doc->minCanvasCoordinate = minSize;
				doc->maxCanvasCoordinate = maxSize;
				if (outlinePalette->isVisible())
					outlinePalette->BuildTree();
				m_undoManager->setUndoEnabled(true);
				if (UndoManager::undoEnabled())
				{
					auto *is = new SimpleState(Um::Paste, QString(), Um::IPaste);
					is->set("PASTE_INLINE");
					is->set("START", currItem->itemText.cursorPosition());
					is->set("INDEX", fIndex);
					m_undoManager->action(currItem, is);
				}
				currItem->itemText.insertObject(fIndex);
				doc->m_Selection->delaySignalsOff();
				inlinePalette->unsetDoc();
				inlinePalette->setDoc(doc);
			}
		}
		else
		{
			ScClipboardProcessor scclipproc(doc, currItem);
			QString clipContent = QApplication::clipboard()->text(QClipboard::Clipboard);
			scclipproc.setContent(clipContent, ScClipboardProcessor::ContentType::Text);
			scclipproc.process();
		}
		if (doc->appMode == modeEditTable)
			selItem->asTable()->update();
		else
		{
			currItem->update();
			if (doc->docHyphenator && currItem->isTextFrame())
				doc->docHyphenator->slotHyphenate(currItem);
		}
	}
	else if (ScMimeData::clipboardHasScribusElem() || ScMimeData::clipboardHasScribusFragment() || internalCopy)
	{
		view->deselectItems(true);
		int docItemCount = doc->Items->count();
		bool savedAlignGrid = doc->SnapGrid;
		bool savedAlignGuides = doc->SnapGuides;
		bool savedAlignElement = doc->SnapItems;
		doc->SnapGrid = false;
		doc->SnapGuides = false;
		doc->SnapItems = false;
		// ✅ Paste at cursor position
		// Step 1: Normal paste (original page position)
		if (internalCopy)
			slotElemRead(internalCopyBuffer,
				doc->currentPage()->xOffset(),
				doc->currentPage()->yOffset(), false, true, doc, view);
		else
		{
			QString buffer = ScMimeData::clipboardScribusElem();
			slotElemRead(buffer,
				doc->currentPage()->xOffset(),
				doc->currentPage()->yOffset(), false, true, doc, view);
		}

		doc->SnapGrid = savedAlignGrid;
		doc->SnapGuides = savedAlignGuides;
		doc->SnapItems = savedAlignElement;
		doc->m_Selection->delaySignalsOn();
		for (int i = docItemCount; i < doc->Items->count(); ++i)
		{
			PageItem* currItem = doc->Items->at(i);
			if (currItem->isBookmark)
				AddBookMark(currItem);
			doc->m_Selection->addItem(currItem);
		}
		doc->m_Selection->delaySignalsOff();
		if (doc->m_Selection->count() > 1)
			doc->m_Selection->setGroupRect();

		// Ctrl+V moves the pasted selection to the current canvas position.
		// Original-position paste reuses the same loader path but leaves its
		// page-relative coordinates untouched.
		if (!pasteInOriginalPosition && doc->m_Selection->count() > 0)
		{
			double targetX = view->dragX;
			double targetY = view->dragY;
			if (targetX > 0 || targetY > 0)
			{
				double gx, gy, gw, gh;
				doc->m_Selection->getGroupRect(&gx, &gy, &gw, &gh);
				doc->moveGroup(targetX - gx, targetY - gy, doc->m_Selection);
			}
		}
	}
	else if (ScMimeData::clipboardHasKnownData())
	{
		QString ext = ScMimeData::clipboardKnownDataExt();
		QByteArray bitsBits = ScMimeData::clipboardKnownDataData();
		double x0 = (view->contentsX() / view->scale()) + ((view->visibleWidth() / 2.0) / view->scale());
		double y0 = (view->contentsY() / view->scale()) + ((view->visibleHeight() / 2.0) / view->scale());
		PageItem *retObj = getVectorFileFromData(doc, bitsBits, ext, x0, y0);
		if (retObj != nullptr)
		{
			double x = (view->contentsX() / view->scale()) + ((view->visibleWidth() / 2.0) / view->scale()) - (retObj->width() / 2.0);
			double y = (view->contentsY() / view->scale()) + ((view->visibleHeight() / 2.0) / view->scale()) - (retObj->height() / 2.0);
			retObj->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
			retObj->setXYPos(x, y, true);
		}
	}
	if (activeTransaction)
		activeTransaction.commit();
	if (doc->notesChanged())
		doc->notesFramesUpdate();
	
	// update style lists:
	requestUpdate(reqColorsUpdate | reqArrowStylesUpdate | reqLineStylesUpdate | reqTextStylesUpdate);

	m_styleManager->setDoc(doc);
	propertiesPalette->unsetDoc();
	propertiesPalette->setDoc(doc);
	contentPalette->unsetDoc();
	contentPalette->setDoc(doc);
	marksManager->setDoc(doc);
	nsEditor->setDoc(doc);
	symbolPalette->unsetDoc();
	symbolPalette->setDoc(doc);
	inlinePalette->unsetDoc();
	inlinePalette->setDoc(doc);

	view->DrawNew();
	slotDocCh(false);
	slotPreviewCh();
}

void ScribusMainWindow::slotEditPastePlainText()
{
	slotEditPaste(true);
}

//CB-->Doc ?????
void ScribusMainWindow::SelectAllOnLayer()
{
	ColorList UsedC;
	doc->getUsedColors(UsedC);
	QScopedPointer<selectDialog> dia(new selectDialog(this, UsedC, doc->unitIndex()));
	if (!dia->exec())
		return;

	PageItem *currItem;
	view->deselectItems();
	int docItemsCount = doc->Items->count();
	int docCurrentPage = doc->currentPageNumber();
	doc->m_Selection->delaySignalsOn();
	int range = dia->getSelectionRange();
	for (int i = 0; i < docItemsCount; ++i)
	{
		currItem = doc->Items->at(i);
		if ((currItem->m_layerID == doc->activeLayer()) && (!doc->layerLocked(currItem->m_layerID)))
		{
			if ((range == 0) && (currItem->OwnPage != docCurrentPage))
				continue;
			if ((range == 2) && (currItem->OwnPage != -1))
				continue;
			if (dia->useAttributes())
			{
				bool useType = false;
				bool useFill = false;
				bool useLine = false;
				bool useLWidth = false;
				bool usePrint = false;
				bool useLocked = false;
				bool useResize = false;
				dia->getUsedAttributes(useType, useFill, useLine, useLWidth, usePrint, useLocked, useResize);
				int Type = 0;
				QString fill;
				QString line;
				double LWidth = 0.0;
				bool Print = false;
				bool Locked = false;
				bool Resize = false;
				dia->getUsedAttributesValues(Type, fill, line, LWidth, Print, Locked, Resize);
				LWidth = LWidth / doc->unitRatio();
				if (useType && (Type != currItem->realItemType()))
					continue;
				if (useFill && ((fill != currItem->fillColor()) || (currItem->GrType != 0)))
					continue;
				if (useLine && (line != currItem->lineColor()))
					continue;
				if (useLWidth && ((LWidth != currItem->lineWidth()) || (currItem->lineColor() == CommonStrings::None)))
					continue;
				if (usePrint && (Print != currItem->printEnabled()))
					continue;
				if (useLocked && (Locked != currItem->locked()))
					continue;
				if (useResize && (Resize != currItem->sizeLocked()))
					continue;
				doc->m_Selection->addItem(currItem);
			}
			else
				doc->m_Selection->addItem(currItem);
		}
	}
	doc->m_Selection->delaySignalsOff();
	if (doc->m_Selection->count() > 1)
		doc->m_Selection->setGroupRect();
	view->DrawNew();
}

void ScribusMainWindow::SelectAll(bool docWideSelect)
{
	if (doc->appMode == modeEdit || doc->appMode == modeEditTable)
	{
		PageItem *currItem = doc->m_Selection->itemAt(0);
		if (doc->appMode == modeEditTable)
		{
			// suneer: Ctrl+A selects all cells in table (two-level, Word/LibreOffice style).
			// 1st Ctrl+A selects the active cell's text; when that text is already fully
			// selected (or cells are already selected), a 2nd Ctrl+A selects every cell.
			PageItem_Table* table = currItem->asTable();
			PageItem_TextFrame* cellFrame = table->activeCell().textFrame();
			bool cellTextFullySelected =
				(cellFrame->itemText.length() == 0) ||
				(cellFrame->itemText.hasSelection() &&
				 cellFrame->itemText.startOfSelection() == 0 &&
				 cellFrame->itemText.endOfSelection() == cellFrame->itemText.length());
			if (table->hasSelection() || cellTextFullySelected)
			{
				cellFrame->itemText.deselectAll();
				cellFrame->HasSel = false;
				table->selectCells(0, 0, table->rows() - 1, table->columns() - 1);
				// suneer: Ctrl+A also selects text inside all cells for uniform formatting
				for (int r = 0; r < table->rows(); r++)
				{
					for (int c = 0; c < table->columns(); c++)
					{
						PageItem_TextFrame* tf = table->cellAt(r, c).textFrame();
						if (tf && tf->itemText.length() > 0)
						{
							tf->itemText.selectAll();
							tf->HasSel = true;
							tf->update();
						}
					}
				}
				table->update();
				setCopyCutEnabled(true);
				view->DrawNew();
				return;
			}
			currItem = cellFrame;
		}
		PageItem *nextItem = currItem;
		nextItem->itemText.selectAll();
		while (nextItem != nullptr)
		{
			if (nextItem->prevInChain() != nullptr)
				nextItem = nextItem->prevInChain();
			else
				break;
		}
		while (nextItem != nullptr)
		{
			nextItem->HasSel = true;
			nextItem = nextItem->nextInChain();
		}
		setCopyCutEnabled(true);
	}
	else
	{
		PageItem *currItem;
		view->deselectItems();
		doc->m_Selection->delaySignalsOn();
		int docItemsCount = doc->Items->count();
		int docCurrentPage = doc->currentPageNumber();
		for (int i = 0; i < docItemsCount; ++i)
		{
			currItem = doc->Items->at(i);
			if (doc->canSelectItemOnLayer(currItem->m_layerID))
			{
				if (docWideSelect)
					doc->m_Selection->addItem(currItem);
				else
				{
					if (currItem->OwnPage == docCurrentPage)
						doc->m_Selection->addItem(currItem);
				}
			}
		}
		doc->m_Selection->delaySignalsOff();
		if (doc->m_Selection->count() > 1)
			doc->m_Selection->setGroupRect();

	}
	view->DrawNew();
}

void ScribusMainWindow::deselectAll()
{
	if (!HaveDoc)
		return;
	if (doc->inAnEditMode())
	{
		if (doc->m_Selection->isEmpty())
			return;
		PageItem *currItem = doc->m_Selection->itemAt(0);
		if (doc->appMode == modeEditTable)
			currItem = currItem->asTable()->activeCell().textFrame();
		if (currItem->isTextFrame())
		{
			currItem->itemText.deselectAll();
			doc->regionsChanged()->update(currItem->getBoundingRect());
		}
		else
		{
			doc->view()->deselectItems(true);
			doc->view()->requestMode(modeNormal);
		}
	}
	else if (view != nullptr)
		view->deselectItems(true);
}

void ScribusMainWindow::ClipChange()
{
	bool textFrameEditMode = false;
	bool tableEditMode = false;
	bool hasScribusData = ScMimeData::clipboardHasScribusElem() || ScMimeData::clipboardHasScribusFragment();
	bool hasExternalData = ScMimeData::clipboardHasKnownData();
	if (HaveDoc && doc->m_Selection->isNotEmpty())
	{
		const PageItem *currItem = doc->m_Selection->itemAt(0);
		textFrameEditMode  = ((doc->appMode == modeEdit) && (currItem->isTextFrame()));
		tableEditMode = ((doc->appMode == modeEditTable) && (currItem->isTable()));
	}
	scrActions["editPaste"]->setEnabled(HaveDoc && (hasScribusData || textFrameEditMode || tableEditMode || hasExternalData));
	scrActions["editPasteOriginalPosition"]->setEnabled(HaveDoc && (hasScribusData || textFrameEditMode || tableEditMode || hasExternalData));
	scrActions["editPastePlainText"]->setEnabled(HaveDoc && (hasScribusData || textFrameEditMode || tableEditMode || hasExternalData));
	// Suneer: styled paste is available whenever a document is open and the styled buffer
	// has content. The edit-mode key handler only triggers *enabled* actions, so we must not
	// gate this on edit mode here; the slot itself no-ops outside a text context (R4).
	scrActions["editStyledPaste"]->setEnabled(HaveDoc && !s_suneerStyledClip.isEmpty());
}

void ScribusMainWindow::setCopyCutEnabled(bool b)
{
	scrActions["editCut"]->setEnabled(b);
	scrActions["editCopy"]->setEnabled(b);
	// Suneer: leave editStyledCopy always enabled so the ApplicationShortcut edit-mode
	// key handler (which only triggers *enabled* actions) always reaches the slot; the
	// slot itself no-ops when there is no selection (R1) or no text context (R4).
}

void ScribusMainWindow::slotHelpAbout()
{
	About dia(this);
	dia.exec();
}

void ScribusMainWindow::slotHelpAboutPlugins()
{
	AboutPlugins dia(this);
	dia.exec();
}

void ScribusMainWindow::slotHelpAboutQt()
{
	ScMessageBox::aboutQt(this, tr("About Qt"));
}

void ScribusMainWindow::slotHelpActionSearch()
{
	ActionSearch actionSearch(this->menuBar());
	actionSearch.update();

	QScopedPointer<ActionSearchDialog> dialog(new ActionSearchDialog(this, actionSearch.getActionNames()));
	dialog->setModal(true);

	int result = dialog->exec();
	if (result != QDialog::Accepted)
		return;

	QString actionName = dialog->getActionName();
	if (actionName.isEmpty())
		return;
	actionSearch.execute(actionName);
}

void ScribusMainWindow::slotHelpCheckUpdates()
{
	// Installed version, latest version, changelog, Update / Later. The server
	// URL and API key live behind its Settings... button.
	UpdateCheckDialog dia(this);
	dia.exec();
}

void ScribusMainWindow::slotOnlineHelp(const QString & jumpToSection, const QString & jumpToFile)
{
	if (!m_helpBrowser)
	{
		m_helpBrowser = new HelpBrowser(nullptr, tr("Scribus Manual"), ScCore->getGuiLanguage(), jumpToSection, jumpToFile);
		connect(m_helpBrowser, SIGNAL(closed()), this, SLOT(slotOnlineHelpClosed()));
	}
	else //just set the requested page
	{
		if (!jumpToSection.isNull() || !jumpToFile.isNull())
		{
			m_helpBrowser->jumpToHelpSection(jumpToSection, jumpToFile, true);
		}
	}
	slotRaiseOnlineHelp();
}

void ScribusMainWindow::slotRaiseOnlineHelp()
{
	if (m_helpBrowser)
		m_helpBrowser->show();
}

void ScribusMainWindow::slotOnlineHelpClosed()
{
	if (m_helpBrowser)
		m_helpBrowser->deleteLater();
}

void ScribusMainWindow::slotResourceManager()
{
	if (resourceManager) // in case its allocated???? maybe can remove in future
		return;
	resourceManager = new ResourceManager(this);
	resourceManager->exec();
	resourceManager->deleteLater();
	resourceManager = nullptr;
}

void ScribusMainWindow::slotItemStyleSearch()
{
	if (!HaveDoc)
		return;

	StyleSearch styleSearch(doc);
	styleSearch.update();

	if (!styleSearch.hasStyles())
		return;

	StyleSearchDialog dialog(this, styleSearch.getStyles());
	dialog.setModal(true);

	int result = dialog.exec();
	if (result != QDialog::Accepted)
		return;

	auto style = dialog.getStyle();
	if (style.name.isEmpty())
		return;
	styleSearch.execute(style);
}

void ScribusMainWindow::ToggleTips()
{
	m_prefsManager.appPrefs.displayPrefs.showToolTips = scrActions["helpTooltips"]->isChecked();
}

void ScribusMainWindow::ToggleMouseTips()
{
	m_prefsManager.appPrefs.displayPrefs.showMouseCoordinates = scrActions["showMouseCoordinates"]->isChecked();
}

void ScribusMainWindow::toggleImposePagesButton()
{
	bool visible = scrActions["viewShowImposePagesButton"]->isChecked();
	m_prefsManager.appPrefs.uiPrefs.showImposePagesButton = visible;
	if (scrActions.contains("extrasImposition"))
		scrActions["extrasImposition"]->setVisible(visible);
}

void ScribusMainWindow::SaveText()
{
	PrefsContext* dirsContext = m_prefsManager.prefsFile->getContext("dirs");
	PrefsContext* textContext = m_prefsManager.prefsFile->getContext("textsave_dialog");
	QString prefsDocDir = m_prefsManager.documentDir();
	QString workingDir = dirsContext->get("save_text", prefsDocDir.isEmpty() ? "." : prefsDocDir);
	QString textEncoding = textContext->get("encoding");

	CustomFDialog dia(this, workingDir, tr("Save as"), tr("Text Files (*.txt);;All Files (*)"), fdShowCodecs|fdHidePreviewCheckBox);
	dia.setTextCodec(textEncoding);
	if (dia.exec() != QDialog::Accepted)
		return;

	QString fileName = dia.selectedFile();
	if (fileName.isEmpty())
		return;
	textEncoding = dia.textCodec();

	dirsContext->set("save_text", fileName.left(fileName.lastIndexOf("/")));
	textContext->set("encoding", dia.textCodec());
	const StoryText& story (doc->m_Selection->itemAt(0)->itemText);
	Serializer::writeWithEncoding(fileName, textEncoding, story.plainText());
}

void ScribusMainWindow::applyNewMaster(const QString& name)
{
	Apply_MasterPage(name, doc->currentPage()->pageNr(), false);
	view->reformPages();
	view->DrawNew();
	pagePalette->rebuild();
}

void ScribusMainWindow::slotNewPageP(int wo, const QString& templ)
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	view->deselectItems(true);
	int where = 1;
	if (wo == 0)
		where = 0;
	else if (wo == doc->Pages->count())
		where = 2;
	slotNewPage(wo, templ); //master page is applied now
	//applyNewMaster(templ);
	if (where == 2)
	{
		doc->addPageToAnnotLinks(wo, where, 1);
		doc->addPageToSection(wo, where, 1);
	}
	else
	{
		doc->addPageToAnnotLinks(wo + 1, where, 1);
		doc->addPageToSection(wo + 1, where, 1);
	}

	doc->updateEndnotesFrames();
	doc->changed();
	updateGUIAfterPagesChanged();
}

/** Erzeugt eine neue Seite */
void ScribusMainWindow::slotNewPageM()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	view->deselectItems(true);
	InsPage *dia = new InsPage(this, doc, doc->currentPage()->pageNr(), doc->Pages->count());
	if (dia->exec())
	{
		QStringList base(dia->getMasterPages());
		double width = dia->pageWidth();
		double height = dia->pageHeight();
		int orientation = dia->orientation();
		addNewPages(dia->getWherePage(), dia->getWhere(), dia->getCount(), height, width, orientation, 
			dia->prefsPageSizeName, dia->moveObjects(), &base, dia->overrideMasterPageSizing());
	}
	delete dia;
}

void ScribusMainWindow::addNewPages(int wo, int where, int numPages, double height, double width, int orient, const QString& siz, bool mov, QStringList* basedOn, bool overrideMasterPageSizing)
{
	UndoTransaction activeTransaction;
	if (UndoManager::undoEnabled())
	{
		activeTransaction = m_undoManager->beginTransaction(doc->getUName(), Um::IDocument, (numPages == 1) ? Um::AddPage : Um::AddPages, QString(), Um::ICreate);
		auto *ss = new SimpleState(Um::AddPage, QString(), Um::ICreate);
		ss->set("ADD_PAGE");
		ss->set("PAGE", wo);
		ss->set("WHERE", where);
		ss->set("COUNT", numPages);
		ss->set("MASTER_PAGE_MODE",  doc->masterPageMode());
		if (basedOn != nullptr)
			ss->set("BASED", basedOn->join("|"));
		else
		{
			int setcol = doc->pageSets()[doc->pagePositioning()].Columns;
			if (setcol == 1)
				ss->set("BASED", CommonStrings::trMasterPageNormal);
			else if (setcol == 2)
				ss->set("BASED", CommonStrings::trMasterPageNormalLeft+"|"+ CommonStrings::trMasterPageNormalRight);
			else if ((setcol == 3) || (setcol == 4))
				ss->set("BASED", CommonStrings::trMasterPageNormalLeft+"|"+ CommonStrings::trMasterPageNormalMiddle+"|"+ CommonStrings::trMasterPageNormalRight);
		}
		ss->set("HEIGHT", height);
		ss->set("WIDTH", width);
		ss->set("ORIENT", orient);
		ss->set("SIZE", siz);
		ss->set("MOVED", mov);
		m_undoManager->action(this, ss);
	}
	// disable recording of undo actions related to new page creating
	// and object moving related to that
	m_undoManager->setUndoEnabled(false);

	QStringList base;
	if (basedOn != nullptr)
	{
		base = *basedOn;
		// #10211 case when restoring page deletion, basedOn contains only masterpage name
		if (base.count() == 1)
		{
			int setcol = doc->pageSets()[doc->pagePositioning()].Columns;
			while (base.count() < setcol)
				base.append (base.at(0));
		}
	}
	if (base.empty())
	{
		int setcol = doc->pageSets()[doc->pagePositioning()].Columns;
		if (setcol == 1)
			base.append( CommonStrings::trMasterPageNormal);
		else if (setcol == 2)
		{
			base.append( CommonStrings::trMasterPageNormalLeft);
			base.append( CommonStrings::trMasterPageNormalRight);
		}
		else if (setcol == 3)
		{
			base.append( CommonStrings::trMasterPageNormalLeft);
			base.append( CommonStrings::trMasterPageNormalMiddle);
			base.append( CommonStrings::trMasterPageNormalRight);
		}
		else if (setcol == 4)
		{
			base.append( CommonStrings::trMasterPageNormalLeft);
			base.append( CommonStrings::trMasterPageNormalMiddle);
			base.append( CommonStrings::trMasterPageNormalMiddle);
			base.append( CommonStrings::trMasterPageNormalRight);
		}
	}

	int wot = wo;
	if (where == 0)
		--wot;
	else if (where == 2)
		wot = doc->Pages->count();

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	view->updatesOn(false);
	const PageSet& pageSet = doc->pageSets()[doc->pagePositioning()];
	ScPage* currentPage = doc->currentPage();
	for (int i = 0; i < numPages; ++i)
	{
		slotNewPage(wot, base[(wot + pageSet.FirstPage) % pageSet.Columns], mov); //Avoid the master page application with QString()
//		slotNewPage(wot, QString(), mov); //Avoid the master page application with QString()
		//CB: #8212: added overrideMasterPageSizing, but keeping default to true for other calls for now, off for calls from InsPage
		if (overrideMasterPageSizing)
		{	
			doc->currentPage()->setInitialHeight(height);
			doc->currentPage()->setInitialWidth(width);
			doc->currentPage()->setOrientation(orient);
			doc->currentPage()->setSize(siz);
		}
		//CB If we want to add this master page setting into the slotnewpage call, the pagenumber must be +1 I think
	//Apply_MasterPage(base[(doc->currentPage()->pageNr()+doc->pageSets[doc->currentPageLayout].FirstPage) % doc->pageSets[doc->currentPageLayout].Columns],
//						 doc->currentPage()->pageNr(), false); // this Apply_MasterPage avoids DreawNew and PagePalette->ReBuild, which is much faster for 100 pp :-)
		++wot;
	}
	doc->setCurrentPage(currentPage);
	view->updatesOn(true);
	QApplication::restoreOverrideCursor();
	//Use wo, the dialog currently returns a page Index +1 due to old numbering scheme, function now does the -1 as required
	doc->changed();
	doc->addPageToAnnotLinks(wot, where, numPages);
	doc->addPageToSection(wo, where, numPages);
	doc->reformPages();
	doc->updateEndnotesFrames();
	updateGUIAfterPagesChanged();

	m_undoManager->setUndoEnabled(true);

	if (activeTransaction)
		activeTransaction.commit();
}

void ScribusMainWindow::slotNewMasterPage(int w, const QString& name)
{
	if (doc->masterPageMode())
	{
		doc->addMasterPage(w, name);
		view->addPage(w);
	}
}

void ScribusMainWindow::slotNewPage(int w, const QString& masterPageName, bool mov)
{
	doc->addPage(w, masterPageName, true);
	view->addPage(w, mov);
	slotSetCurrentPage(w);
}


void ScribusMainWindow::duplicateToMasterPage()
{
	if (!HaveDoc)
		return;
	view->deselectItems(true);
	int pageLocationIndex = -1;
	int pageLocationCount = 0;
	if (doc->pagePositioning() != singlePage)
	{
		QStringList locationEntries;
		QList<PageSet> pageSet(doc->pageSets());
		const QStringList& pageNames = pageSet[doc->pagePositioning()].pageNames;
		for (const QString& pageName : pageNames)
		{
			locationEntries << CommonStrings::translatePageSetLocString(pageName);
		}
		pageLocationIndex = doc->columnOfPage(doc->currentPageNumber());
		pageLocationCount = locationEntries.count();
	}

	CopyPageToMasterPageDialog copyDialog(doc->MasterNames.count(), doc->pageSets()[doc->pagePositioning()].pageNames, pageLocationIndex, this);
	if (copyDialog.exec())
	{
		bool copyFromMaster = false;
		QString masterPageName;
		int pageLocation = 0;
		copyDialog.values(masterPageName, copyFromMaster, pageLocation);
		bool badMasterPageName = doc->MasterNames.contains(masterPageName);
		badMasterPageName |= (masterPageName == CommonStrings::masterPageNormal);
		badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormal);
		badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalLeft);
		badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalMiddle);
		badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalRight);
		badMasterPageName |=  masterPageName.isEmpty();
		while (badMasterPageName)
		{
			if (!copyDialog.exec())
				return;
			copyDialog.values(masterPageName, copyFromMaster, pageLocation);
			badMasterPageName = doc->MasterNames.contains(masterPageName);
			badMasterPageName |= (masterPageName == CommonStrings::masterPageNormal);
			badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormal);
			badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalLeft);
			badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalMiddle);
			badMasterPageName |= (masterPageName == CommonStrings::trMasterPageNormalRight);
			badMasterPageName |=  masterPageName.isEmpty();
		}
		int currentPageNumber = doc->currentPage()->pageNr();
		bool ok = doc->copyPageToMasterPage(currentPageNumber, pageLocation, pageLocationCount, masterPageName, copyFromMaster);
		Q_ASSERT(ok); //TODO get a return value in case the copy was not possible
		pagePalette->rebuild();
	}
}

void ScribusMainWindow::slotZoom(double zoomFactor)
{
	double finalZoomFactor = 0.0;
	//Zoom to Fit
	if (zoomFactor == -100.0)
	{
		finalZoomFactor = (view->height() - 70) / (doc->currentPage()->height() + 30);
	}
	else if (zoomFactor == -200.0)
	{
		finalZoomFactor = (view->width() - 50) / (doc->currentPage()->width() + 30);
	}
	//Zoom to %
	else
		finalZoomFactor = zoomFactor*m_prefsManager.displayScale()/100.0;

	if (finalZoomFactor == view->scale())
		return;

	int x = qRound(qMax(view->contentsX() / view->scale(), 0.0));
	int y = qRound(qMax(view->contentsY() / view->scale(), 0.0));
	int w = qRound(qMin(view->visibleWidth() / view->scale(), doc->currentPage()->width()));
	int h = qRound(qMin(view->visibleHeight() / view->scale(), doc->currentPage()->height()));

	if (zoomFactor == -200.0)
		view->rememberOldZoomLocation(qRound(doc->currentPage()->xOffset() + doc->currentPage()->width() / 2.0), h / 2 + y);
	else if (zoomFactor == -100.0)
		view->rememberOldZoomLocation(w / 2 + x, qRound(doc->currentPage()->yOffset() + doc->currentPage()->height() / 2.0));
	else
		view->rememberOldZoomLocation(w / 2 + x, h / 2 + y);

	view->zoom(finalZoomFactor);
}

void ScribusMainWindow::ToggleStickyTools()
{
	m_prefsManager.appPrefs.uiPrefs.stickyTools = !m_prefsManager.appPrefs.uiPrefs.stickyTools;
	scrActions["stickyTools"]->setChecked(m_prefsManager.appPrefs.uiPrefs.stickyTools);
	if (HaveDoc && doc->appMode != modeNormal && !m_prefsManager.appPrefs.uiPrefs.stickyTools)
		view->requestMode(modeNormal);
}

void ScribusMainWindow::ToggleAllPalettes()
{	
	dockManager->toggleDocksVisibility();

	if (m_palettesStatus[PAL_ALL])
	{
		m_palettesStatus[PAL_ALL] = false;

		if (m_palettesStatus[PAL_VERIFIER])
			docCheckerPalette->show();
		if (m_palettesStatus[PAL_DOWNLOADS])
			downloadsPalette->show();
	}
	else
	{
		m_palettesStatus[PAL_VERIFIER] = docCheckerPalette->isVisible();
		m_palettesStatus[PAL_DOWNLOADS] = downloadsPalette->isVisible();

		docCheckerPalette->hide();
		downloadsPalette->hide();
		m_palettesStatus[PAL_ALL] = true;
	}
}

void ScribusMainWindow::toggleCheckPal()
{
	m_palettesStatus[PAL_ALL] = false;
}

void ScribusMainWindow::togglePagePalette()
{
	m_palettesStatus[PAL_ALL] = false;
}

void ScribusMainWindow::toggleImageVisibility()
{
	if (!doc)
		return;
	doc->guidesPrefs().showPic = !doc->guidesPrefs().showPic;
	QList<PageItem*> allItems;
	for (int i = 0; i < doc->DocItems.count(); ++i)
	{
		PageItem *currItem = doc->DocItems.at(i);
		if (currItem->isGroup())
			allItems = currItem->getAllChildren();
		else
			allItems.append(currItem);
		for (int j = 0; j < allItems.count(); j++)
		{
			PageItem* item = allItems.at(j);
			if (item->isImageFrame())
				item->setImageVisible(doc->guidesPrefs().showPic);
		}
	}
	for (int i=0; i<doc->MasterItems.count(); ++i)
	{
		PageItem *currItem = doc->MasterItems.at(i);
		if (currItem->isGroup())
			allItems = currItem->getAllChildren();
		else
			allItems.append(currItem);
		for (int j = 0; j < allItems.count(); j++)
		{
			PageItem* item = allItems.at(j);
			if (item->isImageFrame())
				item->setImageVisible(doc->guidesPrefs().showPic);
		}
	}
	view->DrawNew();
}

void ScribusMainWindow::ToggleAllGuides()
{
	if (!doc)
		return;
	//m_keyrep = false;
	if (m_guidesStatus[GS_ALL])
	{
		m_guidesStatus[GS_ALL] = false;
		doc->guidesPrefs().marginsShown = m_guidesStatus[GS_MARGINS];
		doc->guidesPrefs().framesShown = m_guidesStatus[GS_FRAMES];
		doc->guidesPrefs().gridShown = m_guidesStatus[GS_GRID];
		doc->guidesPrefs().guidesShown = m_guidesStatus[GS_GUIDES];
		doc->guidesPrefs().baselineGridShown = m_guidesStatus[GS_BASELINE];
		doc->guidesPrefs().linkShown = m_guidesStatus[GS_LINKS];
		doc->guidesPrefs().showControls = m_guidesStatus[GS_CONTROLS];
		doc->guidesPrefs().rulerMode = m_guidesStatus[GS_RULERMODE];
		doc->guidesPrefs().rulersShown = m_guidesStatus[GS_RULERS];
		doc->guidesPrefs().colBordersShown = m_guidesStatus[GS_COLUMNBORDERS];
		doc->guidesPrefs().layerMarkersShown = m_guidesStatus[GS_LAYERMARKERS] ;
		doc->guidesPrefs().showBleed = m_guidesStatus[GS_BLEED];
		toggleMarks();
		toggleFrames();
		toggleLayerMarkers();
		toggleGrid();
		toggleGuides();
		toggleColumnBorders();
		toggleBase();
		toggleTextLinks();
		toggleTextControls();
		toggleRulerMode();
		toggleRulers();
		toggleBleeds();
	}
	else
	{
		m_guidesStatus[GS_ALL] = true;
		m_guidesStatus[GS_MARGINS] = !doc->guidesPrefs().marginsShown;
		m_guidesStatus[GS_FRAMES] = !doc->guidesPrefs().framesShown;
		m_guidesStatus[GS_GRID] = !doc->guidesPrefs().gridShown;
		m_guidesStatus[GS_GUIDES] = !doc->guidesPrefs().guidesShown;
		m_guidesStatus[GS_BASELINE] = !doc->guidesPrefs().baselineGridShown;
		m_guidesStatus[GS_LINKS] = !doc->guidesPrefs().linkShown;
		m_guidesStatus[GS_CONTROLS] = !doc->guidesPrefs().showControls;
		m_guidesStatus[GS_RULERMODE] = !doc->guidesPrefs().rulerMode;
		m_guidesStatus[GS_RULERS] = !doc->guidesPrefs().rulersShown;
		m_guidesStatus[GS_COLUMNBORDERS] = !doc->guidesPrefs().colBordersShown;
		m_guidesStatus[GS_LAYERMARKERS] = !doc->guidesPrefs().layerMarkersShown;
		m_guidesStatus[GS_BLEED] = !doc->guidesPrefs().showBleed;
		doc->guidesPrefs().marginsShown = false;
		doc->guidesPrefs().framesShown = false;
		doc->guidesPrefs().gridShown = false;
		doc->guidesPrefs().guidesShown = false;
		doc->guidesPrefs().baselineGridShown = false;
		doc->guidesPrefs().linkShown = false;
		doc->guidesPrefs().showControls = false;
		doc->guidesPrefs().rulerMode = false;
		doc->guidesPrefs().rulersShown = false;
		doc->guidesPrefs().colBordersShown = false;
		doc->guidesPrefs().layerMarkersShown = false;
		doc->guidesPrefs().showBleed = false;
		view->setRulersShown(doc->guidesPrefs().rulersShown);
	}
	scrActions["viewShowMargins"]->setChecked(doc->guidesPrefs().marginsShown);
	scrActions["viewShowBleeds"]->setChecked(doc->guidesPrefs().showBleed);
	scrActions["viewShowFrames"]->setChecked(doc->guidesPrefs().framesShown);
	scrActions["viewShowLayerMarkers"]->setChecked(doc->guidesPrefs().layerMarkersShown);
	scrActions["viewShowGrid"]->setChecked(doc->guidesPrefs().gridShown);
	scrActions["viewShowGuides"]->setChecked(doc->guidesPrefs().guidesShown);
	scrActions["viewShowColumnBorders"]->setChecked(doc->guidesPrefs().colBordersShown);
	scrActions["viewShowBaseline"]->setChecked(doc->guidesPrefs().baselineGridShown);
	scrActions["viewShowTextChain"]->setChecked(doc->guidesPrefs().linkShown);
	scrActions["viewShowTextControls"]->setChecked(doc->guidesPrefs().showControls);
	scrActions["viewShowRulers"]->setChecked(doc->guidesPrefs().rulersShown);
	scrActions["viewRulerMode"]->setChecked(doc->guidesPrefs().rulerMode);
	view->DrawNew();
}

void ScribusMainWindow::toggleMarks()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().marginsShown = !doc->guidesPrefs().marginsShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleBleeds()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().showBleed = !doc->guidesPrefs().showBleed;
	view->DrawNew();
}

void ScribusMainWindow::toggleFrames()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().framesShown = !doc->guidesPrefs().framesShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleLayerMarkers()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().layerMarkersShown = !doc->guidesPrefs().layerMarkersShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleGrid()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().gridShown = !doc->guidesPrefs().gridShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleGuides()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().guidesShown = !doc->guidesPrefs().guidesShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleColumnBorders()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().colBordersShown = !doc->guidesPrefs().colBordersShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleBase()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().baselineGridShown = !doc->guidesPrefs().baselineGridShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleTextLinks()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().linkShown = !doc->guidesPrefs().linkShown;
	view->DrawNew();
}

void ScribusMainWindow::toggleTextControls()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().showControls = !doc->guidesPrefs().showControls;
	view->DrawNew();
}

void ScribusMainWindow::toggleRulers()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().rulersShown = !doc->guidesPrefs().rulersShown;
	view->setRulersShown(doc->guidesPrefs().rulersShown);
}

void ScribusMainWindow::toggleRulerMode()
{
	if (!doc)
		return;
	m_guidesStatus[GS_ALL] = false;
	doc->guidesPrefs().rulerMode = !doc->guidesPrefs().rulerMode;
	if (doc->guidesPrefs().rulerMode)
	{
		doc->rulerXoffset = 0;
		doc->rulerYoffset = 0;
	}
	else
	{
		doc->rulerXoffset += doc->currentPage()->xOffset();
		doc->rulerYoffset += doc->currentPage()->yOffset();
	}
	if (doc->m_Selection->count() == 1)
	{
		PageItem* currItem = doc->m_Selection->itemAt(0);
		if (currItem != nullptr)
			currItem->emitAllToGUI();
	}
	//TODO emit from selection, handle group widths
	guidePalette->setupPage();
	view->DrawNew();
}

void ScribusMainWindow::toggleSnapGrid()
{
	if (!doc)
		return;
	doc->SnapGrid = !doc->SnapGrid;
	slotDocCh();
}

void ScribusMainWindow::toggleSnapGuides()
{
	if (!doc)
		return;
	doc->SnapGuides = !doc->SnapGuides;
	slotDocCh();
}

void ScribusMainWindow::toggleSnapElements()
{
	if (!doc)
		return;
	doc->SnapItems = !doc->SnapItems;
	slotDocCh();
}

void ScribusMainWindow::setSnapElements(bool b)
{
	if (doc && doc->SnapItems != b)
		toggleSnapElements();
}


void ScribusMainWindow::toggleNodeEdit()
{
	if (!doc)
		return;
	int i = modeEditClip;
	if (doc->appMode == modeEditClip)
		i = submodeEndNodeEdit;
	view->requestMode(i);
}

void ScribusMainWindow::enablePalettes(bool b)
{
	if (doc->appMode == modeEdit) //Keep Palettes enabled when editing text
		return;
	layerPalette->setEnabled(b);
	outlinePalette->setEnabled(b);
	guidePalette->setEnabled(b);
	scrapbookPalette->setEnabled(b);
	pagePalette->setEnabled(b);
	bookmarkPalette->setEnabled(b);
	docCheckerPalette->setEnabled(b);
	inlinePalette->setEnabled(b);
	symbolPalette->setEnabled(b);
	alignDistributePalette->setEnabled(b);
	downloadsPalette->setEnabled(b);
}

void ScribusMainWindow::ToggleFrameEdit()
{
	if (!doc)
		return;

	nodePalette->setDoc(doc, view);
	nodePalette->MoveN();
	nodePalette->HaveNode(false);
	nodePalette->MoveNode->setChecked(true);
	nodePalette->show();
//	qDebug() << "nodepalette show:" << nodePalette->geometry();
	connect(view, SIGNAL(HavePoint(bool)), nodePalette, SLOT(HaveNode(bool)));
	connect(view, SIGNAL(PolyStatus(int,uint)), nodePalette, SLOT(PolyStatus(int,uint)));
	doc->nodeEdit.reset();
	appModeHelper->setFrameEditMode(true);
	enablePalettes(false);
	m_styleManager->setEnabled(false);
	pageSelector->setEnabled(false);
	layerMenu->setEnabled(false);
	if (doc->m_Selection->isNotEmpty())
	{
		PageItem *currItem = doc->m_Selection->itemAt(0);
		nodePalette->EditCont->setEnabled(!currItem->ContourLine.empty());
		nodePalette->ResetCont->setEnabled(false);
		nodePalette->ResetContClip->setEnabled(false);
		nodePalette->PolyStatus(currItem->itemType(), currItem->PoLine.size());
		nodePalette->setDefaults(currItem);
		if ((currItem->isImageFrame()) && (!currItem->imageClip.empty()))
		{
			nodePalette->ResetContClip->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));
			nodePalette->ResetContClip->show();
			nodePalette->ResetShape2Clip->setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));
			nodePalette->ResetShape2Clip->show();
			nodePalette->layout()->activate();
			nodePalette->resize(QSize(170, 380).expandedTo(nodePalette->minimumSizeHint()));
		}
		else
		{
			nodePalette->ResetContClip->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored));
			nodePalette->ResetShape2Clip->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored));
			nodePalette->layout()->activate();
			nodePalette->ResetContClip->hide();
			nodePalette->ResetShape2Clip->hide();
			nodePalette->layout()->activate();
			nodePalette->resize(QSize(170, 380).expandedTo(nodePalette->minimumSizeHint()));
		}
	}
}

void ScribusMainWindow::NoFrameEdit()
{
	disconnect(view, SIGNAL(HavePoint(bool)), nodePalette, SLOT(HaveNode(bool)));
	disconnect(view, SIGNAL(PolyStatus(int, uint)), nodePalette, SLOT(PolyStatus(int, uint)));
	actionManager->disconnectModeActions();
	nodePalette->setDoc(nullptr, nullptr);
	nodePalette->hide();
	appModeHelper->setFrameEditMode(false);
	scrActions["toolsSelect"]->setChecked(true);
	scrActions["toolsEditContents"]->setChecked(false);
	scrActions["toolsEditWithStoryEditor"]->setChecked(false);
	enablePalettes(true);
	m_styleManager->setEnabled(true);
	pageSelector->setEnabled(true);
	layerMenu->setEnabled(true);
	if (HaveDoc)
	{
		doc->nodeEdit.reset();
		HaveNewSel();
		if (doc->m_Selection->isNotEmpty())
		{
			doc->m_Selection->itemAt(0)->emitAllToGUI();
			view->DrawNew();
		}
	}
	actionManager->connectModeActions();
}

/** This is the safest method to return to modeNormal
*/
void ScribusMainWindow::slotSelect()
{
	if (doc)
		view->requestMode(modeNormal);
	else
		appModeHelper->resetApplicationMode(modeNormal);
}

void ScribusMainWindow::setAppModeByToggle(bool isOn, int newMode)
{
	//m_keyrep = false;

	if (newMode == modeDrawLatex && !m_prefsManager.renderFrameConfigured())
	{
		ScMessageBox::critical(this, "Render Frames Not Configured", "Your Render Frame configuration seems to be invalid. Please check the settings in the External Tools section of the Preferences dialog.");
		return;
	}

	if (doc && isOn)
		view->requestMode(newMode);
	else
		slotSelect();
}

void ScribusMainWindow::updateImageEraserOptions()
{
	if (imageEraserOptions)
		imageEraserOptions->refreshFromMode();
}

void ScribusMainWindow::setImageEraserOptionsVisible(bool visible)
{
	if (!imageEraserOptions)
		return;
	imageEraserOptions->setVisible(visible);
	if (visible)
		imageEraserOptions->refreshFromMode();
}

void ScribusMainWindow::updateRemovalToolOptions()
{
	if (removalToolOptions)
		removalToolOptions->refreshFromMode();
}

void ScribusMainWindow::setRemovalToolOptionsVisible(bool visible)
{
	if (!removalToolOptions)
		return;
	removalToolOptions->setVisible(visible);
	if (visible)
		removalToolOptions->refreshFromMode();
}

/*!
 \brief Run one AI Text Tools task on the selection.

 The whole feature funnels through here: one selection check, one service, one
 result dialog, one undo step. Nothing an AI produces reaches the document
 without a person choosing it in that dialog first, which is why this ends in
 exec() rather than in an insert.

 The service is created per invocation and destroyed with the dialog. That
 costs a thread each time and is worth it: Preferences can be changed between
 two uses, and a service built once at start-up would quietly keep using the
 key that was there then.
 */
void ScribusMainWindow::slotAITextTask(int task)
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;

	// Same order as the menu, and as the data set in ActionManager.
	static const char* const taskNames[] = {
		AITextService::TaskCaption,   AITextService::TaskAltText,
		AITextService::TaskHeadline,  AITextService::TaskSummarize,
		AITextService::TaskTranslate, AITextService::TaskImprove
	};
	if (task < 0 || task >= 6)
		return;
	const QString taskName = QLatin1String(taskNames[task]);

	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item)
		return;

	AITextService::Request request;
	request.task = taskName;
	if (AITextService::taskNeedsImage(taskName))
	{
		if (!item->isImageFrame() || !item->imageIsAvailable)
		{
			QMessageBox::information(this, tr("AI Text Tools"),
			                         tr("Select an image frame with a picture in it first."));
			return;
		}
		// The picture as it sits on disk, not as it is cropped on the page: the
		// model is being asked what the photograph shows.
		QImage source(item->Pfile);
		if (source.isNull())
		{
			QMessageBox::information(this, tr("AI Text Tools"),
			                         tr("Scribus could not read that picture."));
			return;
		}
		request.inputImage = source;
	}
	else
	{
		if (!item->isTextFrame())
		{
			QMessageBox::information(this, tr("AI Text Tools"),
			                         tr("Select a text frame with some text in it first."));
			return;
		}
		request.inputText = item->itemText.text(0, item->itemText.length());
		if (request.inputText.trimmed().isEmpty())
		{
			QMessageBox::information(this, tr("AI Text Tools"),
			                         tr("That frame has no text in it."));
			return;
		}
	}

	// Task-specific questions, asked before anything is spent.
	if (taskName == QLatin1String(AITextService::TaskTranslate))
	{
		AITranslateDialog picker(this);
		if (picker.exec() != QDialog::Accepted)
			return;
		request.parameters.insert(QStringLiteral("targetLang"), picker.targetLanguage());
	}

	QString reason;
	std::unique_ptr<AITextService> service =
		AITextServiceFactory::create(doc->prefsData().aiServicePrefs, reason);
	if (!service)
	{
		QMessageBox::information(this, tr("AI Text Tools"), reason);
		return;
	}

	// A modeless wait rather than a modal one: these take seconds, and the user
	// should be able to look at the rest of the page while it happens.
	QProgressDialog waiting(tr("Asking %1 ...")
	                        .arg(AITextServiceFactory::providerDescription(doc->prefsData().aiServicePrefs)),
	                        tr("Cancel"), 0, 0, this);
	waiting.setWindowTitle(tr("AI Text Tools"));
	waiting.setWindowModality(Qt::WindowModal);
	waiting.setMinimumDuration(0);

	AITextService::Response answer;
	QString failure;
	bool finished = false;
	AITextService* raw = service.get();
	connect(raw, &AITextService::completed, this, [&](const AITextService::Response& r) {
		answer = r;
		finished = true;
	});
	connect(raw, &AITextService::failed, this, [&](const QString& e) {
		failure = e;
		finished = true;
	});
	connect(raw, &AITextService::progressUpdate, this, [&](const QString& status) {
		waiting.setLabelText(status);
	});
	connect(&waiting, &QProgressDialog::canceled, this, [raw]() { raw->cancel(); });

	raw->execute(request);
	while (!finished)
	{
		qApp->processEvents(QEventLoop::AllEvents, 50);
		if (waiting.wasCanceled() && failure.isEmpty() && !finished)
			continue;
	}
	waiting.close();

	if (!failure.isEmpty())
	{
		// A cancel is something the user already knows about.
		if (AITextService::isCancelled(failure))
			return;
		if (AITextService::isRefusal(failure))
		{
			QMessageBox::warning(this, tr("AI Text Tools"),
			                     AITextService::strippedRefusal(failure)
			                     + QLatin1String("\n\n")
			                     + tr("Try another model in Preferences > AI Services."));
			return;
		}
		QMessageBox::critical(this, tr("AI Text Tools"), failure);
		return;
	}

	AITextResultDialog dialog(this, taskName, answer);
	// Regenerate runs the same request again and replaces what is shown; the
	// earlier answer is kept if the second attempt fails.
	connect(&dialog, &AITextResultDialog::regenerateRequested, this, [&]() {
		dialog.setBusy(true);
		failure.clear();
		finished = false;
		raw->execute(request);
		while (!finished)
			qApp->processEvents(QEventLoop::AllEvents, 50);
		if (!failure.isEmpty())
			dialog.showError(AITextService::strippedRefusal(failure));
		else
			dialog.setResponse(answer);
	});

	if (dialog.exec() != QDialog::Accepted)
		return;
	const QString chosen = dialog.chosenText();
	if (chosen.isEmpty())
		return;

	if (dialog.chosenAction() == AITextResultDialog::Replace)
	{
		if (!item->isTextFrame())
		{
			QMessageBox::information(this, tr("AI Text Tools"),
			                         tr("That can only replace the text of a text frame. "
			                            "Use Insert as New Frame instead."));
			return;
		}
		// Replacing what someone wrote is worth asking about once, even though
		// it is undoable: the frame may hold an hour's work.
		if (item->itemText.length() > 0
		    && QMessageBox::question(this, tr("AI Text Tools"),
		                             tr("Replace everything in this frame with the "
		                                "AI-generated text?"),
		                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		       != QMessageBox::Yes)
		{
			return;
		}

		UndoTransaction transaction;
		if (UndoManager::undoEnabled())
		{
			transaction = m_undoManager->beginTransaction(
				doc->currentPage()->getUName(), Um::ITextFrame,
				AITextResultDialog::titleForTask(taskName), chosen.left(60), Um::ITextFrame);
		}
		item->itemText.clear();
		item->itemText.insertChars(0, chosen);
		item->invalid = true;
		if (transaction)
			transaction.commit();
		doc->regionsChanged()->update(QRectF());
		slotDocCh();
	}
	else if (dialog.chosenAction() == AITextResultDialog::InsertNewFrame)
	{
		// Below the frame it came from, the same width, so a caption lands
		// where a caption goes.
		const double x = item->xPos();
		const double y = item->yPos() + item->height() + 4.0;
		const double w = item->width();
		const double h = qMax(20.0, item->height() / 4.0);

		UndoTransaction transaction;
		if (UndoManager::undoEnabled())
		{
			transaction = m_undoManager->beginTransaction(
				doc->currentPage()->getUName(), Um::ITextFrame,
				AITextResultDialog::titleForTask(taskName), chosen.left(60), Um::ICreate);
		}
		const int index = doc->itemAdd(PageItem::TextFrame, PageItem::Unspecified,
		                               x, y, w, h, doc->itemToolPrefs().shapeLineWidth,
		                               CommonStrings::None, doc->itemToolPrefs().textColor);
		if (index >= 0)
		{
			PageItem* created = doc->Items->at(index);
			created->itemText.insertChars(0, chosen);
			created->invalid = true;
		}
		if (transaction)
			transaction.commit();
		doc->regionsChanged()->update(QRectF());
		slotDocCh();
	}
}

void ScribusMainWindow::slotDetectContourFromImage()
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;
	PageItem* currItem = doc->m_Selection->itemAt(0);
	PageItem_ImageFrame* frame = currItem ? currItem->asImageFrame() : nullptr;
	if (!frame)
		return;

	if (!frame->canDetectContour())
	{
		// Nothing transparent anywhere: tracing would just return the image
		// rectangle, which is what the frame shape already gives you.
		statusBar()->showMessage(tr("This image has nothing transparent to trace."), 6000);
		return;
	}

	ContourDetectDialog dialog(this, frame);
	if (dialog.exec() != QDialog::Accepted)
		return;

	QString message;
	FPointArray contour = frame->detectContourFromMask(dialog.threshold(), dialog.tolerance(),
	                                                   dialog.mode(), &message);
	if (contour.size() < 4)
	{
		statusBar()->showMessage(message.isEmpty() ? tr("No visible region") : message, 6000);
		return;
	}

	FPointArray oldContour = currItem->ContourLine.copy();
	int oldFlow = static_cast<int>(currItem->textFlowMode());
	int newFlow = dialog.useForTextWrap()
		? static_cast<int>(PageItem::TextFlowUsesContourLine)
		: oldFlow;

	currItem->ContourLine = contour;
	currItem->ClipEdited = true;
	if (newFlow != oldFlow)
		currItem->setTextFlowMode(static_cast<PageItem::TextFlowMode>(newFlow));

	if (UndoManager::undoEnabled())
	{
		auto* state = new ScOldNewState<FPointArray>(Um::DetectContourLine, QString(), Um::IBorder);
		state->set("DETECT_CONTOUR");
		state->setStates(oldContour, contour);
		if (newFlow != oldFlow)
		{
			// The wrap mode is part of the same user action, so it has to undo
			// with it rather than as a separate step.
			state->set("DETECT_CONTOUR_FLOW", true);
			state->set("DETECT_CONTOUR_FLOW_OLD", oldFlow);
			state->set("DETECT_CONTOUR_FLOW_NEW", newFlow);
		}
		m_undoManager->action(currItem, state);
	}

	currItem->update();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	// Text on other frames only reflows once their layout is invalidated.
	doc->invalidateAll();
	view->DrawNew();

	if (!message.isEmpty())
		statusBar()->showMessage(message, 8000);
	else
		statusBar()->showMessage(tr("Contour detected: %n node(s)", "", contour.size() / 4), 5000);
}

bool ScribusMainWindow::frameShapeAppliesTo(const PageItem* item)
{
	if (!item)
		return false;
	/* A shape is an outline the frame's content is clipped to, so it only means
	   something where there is an area to clip. Lines and polylines have no
	   interior, text on a path is defined by the path it follows, and a group is
	   its children rather than an outline of its own - item_setFrameShape()
	   refuses the last three anyway, and this keeps the button from offering. */
	switch (item->itemType())
	{
	case PageItem::ImageFrame:
	case PageItem::TextFrame:
	case PageItem::Polygon:
		return !item->isGroup();
	default:
		return false;
	}
}

bool ScribusMainWindow::frameShapeSelectionIsUsable(QString* reason) const
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
	{
		if (reason)
			*reason = tr("Select a frame first");
		return false;
	}
	const int count = doc->m_Selection->count();
	int firstType = -1;
	for (int i = 0; i < count; ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!frameShapeAppliesTo(item))
		{
			if (reason)
				*reason = tr("Frame Shape works on image, text, or shape frames");
			return false;
		}
		if (item->locked())
		{
			// Its own answer rather than being folded into the type message,
			// which would send the operator looking for the wrong problem.
			if (reason)
				*reason = tr("That frame is locked");
			return false;
		}
		if (firstType == -1)
			firstType = item->itemType();
		else if (item->itemType() != firstType)
		{
			if (reason)
				*reason = tr("Select frames of the same type");
			return false;
		}
	}
	return true;
}

void ScribusMainWindow::applyFrameShape(const QString& shapeId)
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;
	const FrameShapeDef* def = FrameShapeMenu::shapeById(shapeId);
	if (!def)
		return;

	QString reason;
	if (!frameShapeSelectionIsUsable(&reason))
	{
		statusBar()->showMessage(reason, 6000);
		return;
	}

	QList<PageItem*> targets;
	for (int i = 0; i < doc->m_Selection->count(); ++i)
		targets << doc->m_Selection->itemAt(i);

	/* One transaction over the whole selection, named for the shape. Each
	   item_setFrameShape() opens its own transaction underneath and the corner
	   radius records a state of its own, so without this an operator who
	   reshaped four frames would have to press undo eight times to get back. */
	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
	{
		transaction = m_undoManager->beginTransaction(
			(targets.count() == 1) ? targets.first()->getUName() : Um::SelectionGroup,
			(targets.count() == 1) ? targets.first()->getUPixmap() : Um::IGroup,
			tr("Set Frame Shape: %1").arg(def->name()), QString(), Um::IBorder);
	}

	const QList<double> points = def->values();
	for (PageItem* item : std::as_const(targets))
	{
		doc->item_setFrameShape(item, def->frameType, points.count(), points.constData());
		if (def->frameType == 0)
		{
			/* Rounded corners are not a shape in Scribus, they are a radius on
			   a rectangle - which is why both rectangle entries go through
			   frameType 0 and differ only here, and why the Properties
			   Palette's radius box agrees with the result either way.
			   item_setFrameShape() leaves the radius alone for frameType 0, so
			   plain Rectangle has to clear it or a previously rounded frame
			   would quietly stay rounded. */
			const double radius = def->cornerRadiusFraction * qMin(item->width(), item->height());
			item->setCornerRadii(0.0, 0.0, 0.0, 0.0);
			item->setCornerRadius(radius);
			if (radius > 0.0)
				item->SetFrameRound();
			else
				item->SetRectFrame();
			doc->setRedrawBounding(item);
			item->update();
		}
	}

	if (transaction)
		transaction.commit();

	FrameShapeMenu::noteShapeUsed(shapeId);
	if (modeToolBar)
		modeToolBar->updateFrameShapeButton();

	doc->changed();
	doc->changedPagePreview();
	doc->regionsChanged()->update(QRectF());
	statusBar()->showMessage(tr("Frame shape: %1").arg(def->name()), 4000);
}

void ScribusMainWindow::startFrameShapeCrop(const QString& shapeId)
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;
	if (!FrameShapeMenu::shapeById(shapeId))
		return;

	QString reason;
	if (!frameShapeSelectionIsUsable(&reason))
	{
		statusBar()->showMessage(reason, 6000);
		return;
	}

	// Picked up by CanvasMode_ShapeCrop::activate(), which requestMode(modeShapeCrop) below
	// switches to next.
	m_pendingFrameShapeId = shapeId;
	setAppModeByToggle(true, modeShapeCrop);
}

void ScribusMainWindow::slotApplyLastFrameShape()
{
	QString reason;
	if (!frameShapeSelectionIsUsable(&reason))
	{
		// Reachable from the keyboard even when the button is greyed, so it
		// says why rather than doing nothing.
		statusBar()->showMessage(reason, 6000);
		return;
	}
	applyFrameShape(FrameShapeMenu::lastUsedShapeId());
}

void ScribusMainWindow::slotFrameShapeOptions()
{
	/* The menu carries eleven shapes; Scribus has forty-five of them, plus the
	   node editor, and they all live in the Properties Palette's Shape section.
	   This opens that rather than growing a second copy of it here. */
	if (!propertiesPalette)
		return;
	propertiesPalette->toggleView(true);
	propertiesPalette->setAsCurrentTab();
	propertiesPalette->raise();
}

void ScribusMainWindow::setMainWindowActive()
{
	activateWindow();
	if (!scriptIsRunning())
		raise();
}

void ScribusMainWindow::setItemTypeStyle(int id)
{
	int b = 0;
	if (id == 0)
	{
		scrActions["typeEffectNormal"]->setChecked(true);
		scrActions["typeEffectUnderline"]->setChecked(false);
		scrActions["typeEffectUnderlineWords"]->setChecked(false);
		scrActions["typeEffectStrikeThrough"]->setChecked(false);
		scrActions["typeEffectSmallCaps"]->setChecked(false);
		scrActions["typeEffectAllCaps"]->setChecked(false);
		scrActions["typeEffectSuperscript"]->setChecked(false);
		scrActions["typeEffectSubscript"]->setChecked(false);
		scrActions["typeEffectOutline"]->setChecked(false);
		scrActions["typeEffectShadow"]->setChecked(false);
	}
	else
	{
		scrActions["typeEffectNormal"]->setChecked(false);
		if (id == 4)
			scrActions["typeEffectSubscript"]->setChecked(false);
		if (id == 5)
			scrActions["typeEffectSuperscript"]->setChecked(false);
		if (id == 3)
			scrActions["typeEffectAllCaps"]->setChecked(false);
		if (id == 7)
			scrActions["typeEffectSmallCaps"]->setChecked(false);
		if (id == 8)
			scrActions["typeEffectUnderline"]->setChecked(false);
		if (id == 1)
			scrActions["typeEffectUnderlineWords"]->setChecked(false);
		if (scrActions["typeEffectUnderline"]->isChecked())
			b |= 8;
		if (scrActions["typeEffectUnderlineWords"]->isChecked())
			b |= 512;
		if (scrActions["typeEffectShadow"]->isChecked())
			b |= 256;
		if (scrActions["typeEffectStrikeThrough"]->isChecked())
			b |= 16;
		if (scrActions["typeEffectAllCaps"]->isChecked())
			b |= 32;
		if (scrActions["typeEffectSmallCaps"]->isChecked())
			b |= 64;
		if (scrActions["typeEffectSuperscript"]->isChecked())
			b |= 1;
		if (scrActions["typeEffectSubscript"]->isChecked())
			b |= 2;
		if (scrActions["typeEffectOutline"]->isChecked())
			b |= 4;
	}
	setItemEffects(b);
}

void ScribusMainWindow::setStyleEffects(int s)
{
	int i = s & 1919;
	scrActions["typeEffectNormal"]->setChecked(i==0);
	scrActions["typeEffectSuperscript"]->setChecked(i & 1);
	scrActions["typeEffectSubscript"]->setChecked(i & 2);
	scrActions["typeEffectOutline"]->setChecked(i & 4);
	scrActions["typeEffectUnderline"]->setChecked(i & 8);
	scrActions["typeEffectStrikeThrough"]->setChecked(i & 16);
	scrActions["typeEffectAllCaps"]->setChecked(i & 32);
	scrActions["typeEffectSmallCaps"]->setChecked(i & 64);
	scrActions["typeEffectShadow"]->setChecked(i & 256);
	scrActions["typeEffectUnderlineWords"]->setChecked(i & 512);
	emit TextEffects(s);
}

void ScribusMainWindow::setItemEffects(int h)
{
	if (doc->m_Selection->isEmpty())
		return;
	setStyleEffects(h);
	doc->itemSelection_SetEffects(h);
}

//CB-->Doc partly
void ScribusMainWindow::deletePage2(int pg)
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	view->deselectItems(true);
	if (doc->Pages->count() == 1)
		return;
	deletePage(pg + 1, pg + 1);
}

void ScribusMainWindow::deletePage()
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	view->deselectItems(true);
	DelPages *dia = new DelPages(this, doc->currentPage()->pageNr()+1, doc->Pages->count());
	if (dia->exec())
		deletePage(dia->getFromPage(), dia->getToPage());
	delete dia;
}

void ScribusMainWindow::deletePage(int from, int to)
{
	UndoTransaction activeTransaction;
	assert( from > 0 );
	assert( from <= to );
	assert( to <= static_cast<int>(doc->Pages->count()) );
	int oldPg = doc->currentPageNumber();
	guidePalette->setDoc(nullptr);
	if (UndoManager::undoEnabled())
		activeTransaction = m_undoManager->beginTransaction(doc->documentFileName(), Um::IDocument,
														  (from - to == 0) ? Um::DeletePage : Um::DeletePages, QString(),
														  Um::IDelete);
	PageItem* ite;
	doc->m_Selection->clear();
	Selection tmpSelection(this, false);
	for (int a = to - 1; a >= from - 1; a--)
	{
		for (int d = 0; d < doc->Items->count(); ++d)
		{
			ite = doc->Items->at(d);
			//do not delete notes frames
			if (ite->isAutoNoteFrame())
				continue;
			if (ite->OwnPage == a)
			{
				ite->setLocked(false);
				ite->isSingleSel = false;
				if (ite->isBookmark)
					DelBookMark(ite);
				ite->isBookmark = false;
				tmpSelection.addItem(ite);
			}
		}
		ScPage *page = doc->Pages->at(a); // need to remove guides too to get their undo/redo actions working
		page->guides.clearHorizontals(GuideManagerCore::Standard);
		page->guides.clearHorizontals(GuideManagerCore::Auto);
		page->guides.clearVerticals(GuideManagerCore::Standard);
		page->guides.clearVerticals(GuideManagerCore::Auto);
	}
	if (tmpSelection.count() != 0)
		doc->itemSelection_DeleteItem(&tmpSelection);
	const QSignalBlocker blocker(pageSelector);
	view->updatesOn(false);
	for (int a = to - 1; a >= from - 1; a--)
	{
		if (UndoManager::undoEnabled())
		{
			auto *ss = new SimpleState(Um::DeletePage, QString(), Um::ICreate);
			ss->set("DELETE_PAGE");
			ss->set("PAGENR", a + 1);
			ss->set("PAGENAME",   doc->Pages->at(a)->pageName());
			ss->set("MASTERPAGE", doc->Pages->at(a)->masterPageName());
			ss->set("MASTER_PAGE_MODE",  doc->masterPageMode());
			// replace the deleted page in the undostack by a dummy object that will
			// replaced with the "undone" page if user choose to undo the action
			auto *duo = new DummyUndoObject();
			uint id = static_cast<uint>(duo->getUId());
			m_undoManager->replaceObject(doc->Pages->at(a)->getUId(), duo);
			ss->set("DUMMY_ID", id);
			m_undoManager->action(this, ss);
		}
		bool isMasterPage = !(doc->Pages->at(a)->pageNameEmpty());
		if (doc->masterPageMode())
			doc->deleteMasterPage(a);
		else
			doc->deletePage(a);
		if (!isMasterPage) // Master pages are not added to sections when created
		{
			doc->removePageFromAnnotLinks(a);
			doc->removePageFromSection(a);
		}
	}
	pageSelector->setMaximum(doc->Pages->count());
	m_undoManager->setUndoEnabled(false); // ugly hack to disable object moving when undoing page deletion
	view->reformPagesView();
	m_undoManager->setUndoEnabled(true); // ugly hack continues
	view->updatesOn(true);
	view->GotoPage(qMin(doc->Pages->count()-1, oldPg));
	doc->updateEndnotesFrames();
	updateGUIAfterPagesChanged();
	doc->rebuildMasterNames();
	pagePalette->rebuildMasters();
	if (activeTransaction)
		activeTransaction.commit();
}

void ScribusMainWindow::movePage()
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	int currentPage = doc->currentPage()->pageNr();
	QScopedPointer<MovePages> dia(new MovePages(this, currentPage + 1, doc->Pages->count(), true));
	if (!dia->exec())
		return;
	int from = dia->getFromPage();
	int to = dia->getToPage();
	int wie = dia->getWhere();
	int wo = dia->getWherePage();
	if (wie == 3)
	{
		doc->swapPage(from-1, wo-1);
		updateGUIAfterPagesChanged();
		return;
	}
	if ((wie < 2) && (from <= wo) && (wo <= to))
		return;
	if (wie == 2 && to == signed(doc->Pages->count()))
		return;
	doc->movePage(from-1, to, wo-1, wie);
	updateGUIAfterPagesChanged();
	doc->updateEndnotesFrames();
}

void ScribusMainWindow::copyPage()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);

	QScopedPointer<MovePages> dia(new MovePages(this, doc->currentPage()->pageNr() + 1, doc->Pages->count(), false));
	if (!dia->exec())
		return;

	int pageNumberToCopy = dia->getFromPage() - 1;
	int whereToInsert = dia->getWhere();
	int copyCount = dia->getCopyCount();
	int wo = dia->getWherePage();
	doc->copyPage(pageNumberToCopy, wo, whereToInsert, copyCount);
	view->deselectItems(true);
	doc->updateEndnotesFrames();
	updateGUIAfterPagesChanged();
	slotDocCh();
}

void ScribusMainWindow::changePageProperties()
{
	if (!HaveDoc)
		return;
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	QString currPageMasterPageName(doc->currentPage()->masterPageName());
	QScopedPointer<PagePropertiesDialog> dia(new PagePropertiesDialog(this, doc));
	if (!dia->exec())
		return;

	int orientation = dia->getPageOrientation();
	double pageHeight = dia->getPageHeight();
	double pageWidth = dia->getPageWidth();
	QString pageSizeName = dia->getPrefsPageSizeName();
	int lp=0;
	if (doc->masterPageMode() && doc->pagePositioning() != singlePage)
		lp = dia->pageOrder();
	doc->changePageProperties(dia->top(), dia->bottom(), dia->left(), dia->right(),
							pageHeight, pageWidth, pageHeight, pageWidth, orientation,
							pageSizeName, dia->getMarginPreset(), dia->getMoveObjects(), doc->currentPage()->pageNr(), lp);
	if (!doc->masterPageMode() && dia->masterPage() != currPageMasterPageName)
		Apply_MasterPage(dia->masterPage(), doc->currentPage()->pageNr());
	doc->updateEndnotesFrames();
}

void ScribusMainWindow::setItemFontSize(int fontSize)
{
	int fs = fontSize;
	if (fs != -1)
		doc->itemSelection_SetFontSize(fs*10);
	else
	{
		bool ok = false;
		Query dia(this, "New", true, tr("&Size:"), tr("Size"));
		if (dia.exec())
		{
			fs = qRound(dia.getEditText().toDouble(&ok));
			if (ok && (fs < 1025) && (fs > 0))
				doc->itemSelection_SetFontSize(fs*10);
		}
	}
	contentPalette->updateTextFontSize(fs*10);
}

void ScribusMainWindow::setItemLanguage(const QString& language)
{
	Query dia(this, "New", true, tr("&Language:"), tr("Language"));
	if (dia.exec())
		doc->itemSelection_SetLanguage(language);

	contentPalette->updateTextLanguage(language);
}

//CB-->Doc
void ScribusMainWindow::setNewAlignment(int i)
{
	if (!HaveDoc)
		return;
	doc->itemSelection_SetAlignment(i);
	contentPalette->updateTextAlignment(i);
	PageItem *currItem = doc->m_Selection->itemAt(0);
	setTBvals(currItem);
}

void ScribusMainWindow::setNewDirection(int i)
{
	if (!HaveDoc)
		return;
	doc->itemSelection_SetDirection(i);
	contentPalette->updateTextDirection(i);
	PageItem *currItem = doc->m_Selection->itemAt(0);
	setTBvals(currItem);
}

void ScribusMainWindow::setNewParStyle(const QString& name)
{
	if (!HaveDoc)
		return;
	// ✅ Clear direct char formatting — selected paragraph only
	{
		int savedMode = doc->appMode;
		doc->appMode = modeEdit;
		for (int i = 0; i < doc->m_Selection->count(); ++i)
		{
			PageItem* item = doc->m_Selection->itemAt(i);
			if (!item || !item->isTextFrame()) continue;
			StoryText& st = item->itemText;
			int cursor = st.cursorPosition();
			int parNum = st.nrOfParagraph(cursor);
			int pStart = st.startOfParagraph(parNum);
			int pEnd = st.endOfParagraph(parNum);
			st.select(pStart, pEnd - pStart);
		}
		doc->itemSelection_EraseCharStyle();
		doc->appMode = savedMode;
	}
	doc->itemSelection_SetNamedParagraphStyle(name);
	PageItem *currItem = doc->m_Selection->itemAt(0);
	setTBvals(currItem);
}

void ScribusMainWindow::setNewCharStyle(const QString& name)
{
	if (!HaveDoc)
		return;
	doc->itemSelection_SetNamedCharStyle(name);
	PageItem *currItem = doc->m_Selection->itemAt(0);
	setTBvals(currItem);
}

void ScribusMainWindow::setAlignmentValue(int i)
{
	contentPalette->updateTextAlignment(i);
	std::array<QString, 5> alignment = {"Left", "Center", "Right", "Block", "Forced"};
	for (size_t j = 0; j < alignment.size(); ++j)
	{
		QString actionName = "align" + alignment[j];
		if (scrActions[actionName])
			scrActions[actionName]->setChecked(i == static_cast<int>(j));
	}
}

void ScribusMainWindow::setDirectionValue(int i)
{
	contentPalette->updateTextDirection(i);
	std::array<QString, 2> direction = {"Left", "Right"};
	for (size_t j = 0; j < direction.size(); ++j)
	{
		QString actionName = "direction" + direction[j];
		if (scrActions[actionName])
			scrActions[actionName]->setChecked(i == static_cast<int>(j));
	}
}

//CB-->??
void ScribusMainWindow::MakeFrame(int f, int c, double *vals)
{
	PageItem *currItem = doc->m_Selection->itemAt(0);
	switch (f)
	{
		case 0:
			currItem->SetRectFrame();
			doc->setRedrawBounding(currItem);
			break;
		case 1:
			currItem->SetOvalFrame();
			doc->setRedrawBounding(currItem);
			break;
		default:
			currItem->SetFrameShape(c, vals);
			doc->setRedrawBounding(currItem);
			currItem->FrameType = f + 2;
			break;
	}
	currItem->update();
	slotDocCh();
}

void ScribusMainWindow::duplicateItem()
{
	slotSelect();

	double shiftGapH = doc->opToolPrefs().dispX * doc->unitRatio();
	double shiftGapV = doc->opToolPrefs().dispY * doc->unitRatio();

	internalCopy = true;
	
	doc->itemSelection_Duplicate(shiftGapH, shiftGapV);

	internalCopy = false;
	internalCopyBuffer.clear();
}

void ScribusMainWindow::duplicateItemMulti()
{
	if (!HaveDoc)
		return;
	slotSelect();
	if (doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	internalCopy = true;
	QScopedPointer<MultipleDuplicate> dia(new MultipleDuplicate(this, doc));
	if (dia->exec())
	{
		ItemMultipleDuplicateData mdData;
		dia->getMultiplyData(mdData);
		doc->itemSelection_MultipleDuplicate(mdData);
	}
	internalCopy = false;
}

void ScribusMainWindow::editItemsFromOutlines(PageItem *ite)
{
	if (ite->locked())
		return;
	if (doc->m_Selection->isNotEmpty())
	{
		if (doc->m_Selection->itemAt(0) != ite)
			selectItemsFromOutlines(ite, ite->isGroup());
	}
	if (ite->isLatexFrame())
	{
		if (ite->imageVisible())
			view->requestMode(modeEdit);
	}
	else if (ite->isOSGFrame())
		view->requestMode(submodeEditExternal);
	else if ((ite->itemType() == PageItem::Polygon) || (ite->itemType() == PageItem::PolyLine) || (ite->itemType() == PageItem::Group) || (ite->itemType() == PageItem::ImageFrame) || (ite->itemType() == PageItem::PathText))
	{
		if (ite->itemType() == PageItem::ImageFrame)
		{
			if (ite->Pfile.isEmpty())
				view->requestMode(submodeLoadPic);
			else if (!ite->imageIsAvailable)
				view->requestMode(submodeStatusPic);
			else if (ite->imageVisible())
				view->requestMode(modeEdit);
		}
		else if (ite->itemType() == PageItem::TextFrame)
			view->requestMode(modeEdit);
		else
		{
			view->requestMode(modeEditClip);
			scrActions["itemUngroup"]->setEnabled(false);
			scrActions["itemGroupAdjust"]->setEnabled(false);
		}
	}
	else if (ite->itemType() == PageItem::TextFrame)
	{
		if (ite->isAnnotation())
		{
			view->requestMode(submodeAnnotProps);
		}
		else if (doc->appMode != modeEdit)
		{
			view->requestMode(modeEdit);
		}
	}
	else if (ite->isSymbol())
	{
		if (!doc->symbolEditMode())
			view->requestMode(submodeEditSymbol);
	}
	else if (ite->isArc())
		view->requestMode(modeEditArc);
	else if (ite->isRegularPolygon())
		view->requestMode(modeEditPolygon);
	else if (ite->isSpiral())
		view->requestMode(modeEditSpiral);
	else if (ite->isTable())
		view->requestMode(modeEditTable);
}

void ScribusMainWindow::selectItemsFromOutlines(PageItem* ite, bool single, int position)
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	activateWindow();
	view->deselectItems(true);
	if (!doc->symbolEditMode() && !doc->inlineEditMode())
	{
		int itemPage = ite->OwnPage;
		PageItem* parentItem = ite->Parent;
		while (parentItem && parentItem->isGroup())
		{
			itemPage = parentItem->OwnPage;
			parentItem = parentItem->Parent;
		}
		if ((itemPage != -1) && (itemPage != doc->currentPage()->pageNr()))
			view->GotoPage(itemPage);
	}
	doc->m_Selection->delaySignalsOn();
	view->selectItem(ite, true, single);
	doc->m_Selection->delaySignalsOff();

	if (doc->m_Selection->isEmpty())
		return;
	doc->m_Selection->connectItemToGUI();

	const PageItem *currItem = doc->m_Selection->itemAt(0);
	QTransform itemTrans = currItem->getTransform();
	double xOffset = 0.0;
	double yOffset = 0.0;
	switch (position)
	{
		case 1: //top left
			break;
		default: //center
			xOffset = currItem->width() / 2.0;
			yOffset = currItem->height() / 2.0;
			break;
	}

	QPointF point = itemTrans.map(QPointF(xOffset, yOffset));
	view->setCanvasCenterPos(point.x(), point.y());
}

void ScribusMainWindow::selectItemFromOutlines(PageItem *ite, bool single, int cPos)
{
	selectItemsFromOutlines(ite, single);
	if (doc->m_Selection->isEmpty())
		return;
	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (currItem->isTextFrame())
	{
		view->requestMode(modeEdit);
		currItem->itemText.setCursorPosition(cPos);
		currItem->update();
	}
}

void ScribusMainWindow::selectPagesFromOutlines(int Page)
{
	if (HaveDoc && doc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);
	activateWindow();
	view->deselectItems(true);
	if (Page < 0)
		return;
	view->GotoPage(Page);
}

void ScribusMainWindow::slotPrefsOrg()
{
	QString oldMonitorProfile(ScCore->monitorProfile.productDescription());
	slotSelect();
	struct ApplicationPrefs oldPrefs(m_prefsManager.appPrefs);
	// Not part of ApplicationPrefs (prefs context "suneer_images").
	const int oldLinkBadgeSize = SuneerImageLinks::badgeSize();
	PreferencesDialog prefsDialog(this, oldPrefs);
	int prefsResult = prefsDialog.exec();
	if (prefsResult != QDialog::Accepted)
		return;

	struct ApplicationPrefs newPrefs(prefsDialog.prefs());
	m_prefsManager.setNewPrefs(newPrefs);
	m_prefsManager.applyLoadedShortCuts();
	enforceClipboardShortcuts();
	// News Browser tab added/removed live — no restart needed.
	suneerSetNewsBrowserTabVisible(newPrefs.experimentalFeaturePrefs.newsBrowserEnabled);

	//TODO: and the other dirs?
	if (oldPrefs.pathPrefs.documents != newPrefs.pathPrefs.documents)
	{
		PrefsContext* docContext = m_prefsManager.prefsFile->getContext("docdirs", false);
		docContext->set("docsopen", newPrefs.pathPrefs.documents);
	}

	ScQApp->neverSplash(!m_prefsManager.appPrefs.uiPrefs.showSplashOnStartup);

	QString newUILanguage = m_prefsManager.uiLanguage();
	if (oldPrefs.uiPrefs.language != newUILanguage || ScQApp->currGUILanguage() != newUILanguage)
		ScQApp->changeGUILanguage(newUILanguage);
	m_prefsManager.appPrefs.uiPrefs.language = ScQApp->currGUILanguage();
	LocaleManager::instance().setUserPreferredLocale(m_prefsManager.appPrefs.uiPrefs.userPreferredLocale);
	ScQApp->setLocale();

	bool useDefaultScratchColor = false;
	if (m_prefsManager.appPrefs.displayPrefs.scratchColor == QApplication::palette().color(QPalette::Active, QPalette::Window))
		useDefaultScratchColor = true;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 8, 0))
	QString newUIStylePalette = m_prefsManager.appPrefs.uiPrefs.stylePalette;
	if (oldPrefs.uiPrefs.stylePalette != newUIStylePalette)
	{
		if (newUIStylePalette == "dark")
			ScribusProxyStyle::instance()->setApplicationTheme(ScribusProxyStyle::ApplicationTheme::Dark);
		else if (newUIStylePalette == "light")
			ScribusProxyStyle::instance()->setApplicationTheme(ScribusProxyStyle::ApplicationTheme::Light);
		else
			ScribusProxyStyle::instance()->setApplicationTheme(ScribusProxyStyle::ApplicationTheme::System);
	}
#endif

	bool forceIconUpdate = false;
	QString newUIStyle = m_prefsManager.guiStyle();
	if (oldPrefs.uiPrefs.style != newUIStyle)
	{
		forceIconUpdate = true;

		QString styleName = m_prefsManager.guiSystemStyle();
		if (!newUIStyle.isEmpty())
			styleName = newUIStyle;

		ScribusProxyStyle::instance()->setBaseStyleName(styleName);
		// QStyle * newStyle = QStyleFactory::create(styleName);
		// if (newStyle)
		// 	QApplication::setStyle(newStyle);
		// else
		// 	m_prefsManager.appPrefs.uiPrefs.style = oldPrefs.uiPrefs.style;
	}

	if (useDefaultScratchColor)
		m_prefsManager.appPrefs.displayPrefs.scratchColor = QApplication::palette().color(QPalette::Active, QPalette::Window);

	QString newIconSet = m_prefsManager.guiIconSet();
	// Recreate icons if icon set or GUI changed. For GUI change the icon recreation will automatically detect light and dark themes
	if (oldPrefs.uiPrefs.iconSet != newIconSet || forceIconUpdate == true)
		ScQApp->changeIconSet(newIconSet);

	ScQApp->changeLabelVisibility(m_prefsManager.appPrefs.uiPrefs.showLabels);

	int newUIFontSize = m_prefsManager.guiFontSize();
	if (oldPrefs.uiPrefs.applicationFontSize != newUIFontSize)
	{
		QFont apf = QApplication::font();
		apf.setPointSize(newUIFontSize);
		QApplication::setFont(apf);
	}
	emit UpdateRequest(reqDefFontListUpdate);
	if (m_prefsManager.appPrefs.uiPrefs.useTabs)
	{
		mdiArea->setViewMode(QMdiArea::TabbedView);
		mdiArea->setTabsClosable(true);
		mdiArea->setDocumentMode(true);
	}
	else
		mdiArea->setViewMode(QMdiArea::SubWindowView);
	bool shadowChanged = oldPrefs.displayPrefs.showPageShadow != m_prefsManager.showPageShadow();
	QList<QMdiSubWindow *> windows = mdiArea->subWindowList();
	if (!windows.isEmpty())
	{
		int windowCount = windows.count();
		for (int i = 0; i < windowCount; ++i)
		{
			QWidget* w = windows.at(i)->widget();
			ScribusWin* scw = dynamic_cast<ScribusWin *>(w);
			if (!scw)
			{
				qFatal("ScribusMainWindow::slotPrefsOrg !scw");
				continue;
			}
			ScribusView* scw_v = scw->view();
			if (oldPrefs.displayPrefs.displayScale != m_prefsManager.displayScale())
			{
				int x = qRound(qMax(scw_v->contentsX() / scw_v->scale(), 0.0));
				int y = qRound(qMax(scw_v->contentsY() / scw_v->scale(), 0.0));
				int w = qRound(qMin(scw_v->visibleWidth() / scw_v->scale(), scw->doc()->currentPage()->width()));
				int h = qRound(qMin(scw_v->visibleHeight() / scw_v->scale(), scw->doc()->currentPage()->height()));
				scw_v->rememberOldZoomLocation(w / 2 + x,h / 2 + y);
				scw_v->zoom((scw_v->scale() / oldPrefs.displayPrefs.displayScale) * m_prefsManager.displayScale());
				zoomSpinBox->setMaximum(doc->opToolPrefs().magMax);
			}
			// The badges are part of the canvas picture, so a new size
			// needs a repaint to show.
			if (shadowChanged || oldLinkBadgeSize != SuneerImageLinks::badgeSize())
				scw->view()->DrawNew();
		}
	}

	QString newMonitorProfile(newPrefs.colorPrefs.DCMSset.DefaultMonitorProfile);
	if (oldMonitorProfile != newMonitorProfile)
	{
		bool success = false;
		if (ScCore->MonitorProfiles.contains(newMonitorProfile))
		{
			QString profilePath = ScCore->MonitorProfiles[newMonitorProfile].file;
			ScColorProfile newProfile = ScCore->defaultEngine.openProfileFromFile(profilePath);
			if (!newProfile.isNull())
			{
				ScCore->monitorProfile = newProfile;
				success = true;
			}
		}
		if (!success)
		{
			newPrefs.colorPrefs.DCMSset.DefaultMonitorProfile = oldMonitorProfile;
			m_prefsManager.setNewPrefs(newPrefs);
			QString message = tr("An error occurred while opening monitor profile.\nFormer monitor profile will be used." );
			if (ScCore->usingGUI())
				ScMessageBox::warning(this, CommonStrings::trWarning, message);
			else
				qWarning( "%s", message.toLocal8Bit().data() );
		}
	}
	ScImageCacheManager & icm = ScImageCacheManager::instance();
	icm.setEnabled(newPrefs.imageCachePrefs.cacheEnabled);
	icm.setMaxCacheSizeMiB(newPrefs.imageCachePrefs.maxCacheSizeMiB);
	icm.setMaxCacheEntries(newPrefs.imageCachePrefs.maxCacheEntries);
	icm.setCompressionLevel(newPrefs.imageCachePrefs.compressionLevel);

	TextFrameSpellChecker* checker = TextFrameSpellChecker::instance();
	checker->setEnabled(newPrefs.spellCheckPrefs.liveSpellCheckEnabled);
	checker->setDebounceDelay(newPrefs.spellCheckPrefs.debounceDelay);

	m_prefsManager.savePrefs();
	m_mainWindowStatusLabel->setText( tr("Ready"));
}

void ScribusMainWindow::slotDocSetup()
{
	if (!doc)
		return;
	struct ApplicationPrefs oldDocPrefs(doc->prefsData());
	PreferencesDialog prefsDialog(this, oldDocPrefs, doc);
	int prefsResult = prefsDialog.exec();
	if (prefsResult != QDialog::Accepted)
		return;

	struct ApplicationPrefs newDocPrefs(prefsDialog.prefs());
	bool resizePages, resizeMasterPages, resizePageMargins, resizeMasterPageMargins;
	prefsDialog.getResizeDocumentPages(resizePages, resizeMasterPages, resizePageMargins, resizeMasterPageMargins);
	doc->setNewPrefs(newDocPrefs, oldDocPrefs, resizePages, resizeMasterPages, resizePageMargins, resizeMasterPageMargins);

	slotChangeUnit(doc->unitIndex(), false);

	if (oldDocPrefs.itemToolPrefs.imageLowResType != newDocPrefs.itemToolPrefs.imageLowResType)
	{
		setStatusBarInfoText( tr("Updating Images"));
		mainWindowProgressBar->reset();
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		QApplication::processEvents();
		doc->recalcPicturesRes(ScribusDoc::RecalcPicRes_ApplyNewRes);
		QApplication::restoreOverrideCursor();
		setStatusBarInfoText("");
		mainWindowProgressBar->reset();
		viewToolBar->setDoc(doc);
	}

//	Mark* mrk = doc->getMark("Document Title", MARKVariableTextType);
//	if (mrk)
//		mrk->setString(newDocPrefs.docInfo.title());

	emit UpdateRequest(reqDocFontListUpdate);
	scrActions["viewShowMargins"]->setChecked(doc->guidesPrefs().marginsShown);
	scrActions["viewShowBleeds"]->setChecked(doc->guidesPrefs().showBleed);
	scrActions["viewShowFrames"]->setChecked(doc->guidesPrefs().framesShown);
	scrActions["viewShowLayerMarkers"]->setChecked(doc->guidesPrefs().layerMarkersShown);
	scrActions["viewShowGrid"]->setChecked(doc->guidesPrefs().gridShown);
	scrActions["viewShowGuides"]->setChecked(doc->guidesPrefs().guidesShown);
	scrActions["viewShowColumnBorders"]->setChecked(doc->guidesPrefs().colBordersShown);
	scrActions["viewShowBaseline"]->setChecked(doc->guidesPrefs().baselineGridShown);
	scrActions["viewShowImages"]->setChecked(doc->guidesPrefs().showPic);
	scrActions["viewShowTextChain"]->setChecked(doc->guidesPrefs().linkShown);
	scrActions["viewShowTextControls"]->setChecked(doc->guidesPrefs().showControls);
	scrActions["viewShowRulers"]->setChecked(doc->guidesPrefs().rulersShown);
	scrActions["viewRulerMode"]->setChecked(doc->guidesPrefs().rulerMode);
	scrActions["extrasGenerateTableOfContents"]->setEnabled(doc->hasTOCSetup() || doc->hasIndexSetup());
	scrActions["extrasUpdateDocument"]->setEnabled(true);
	scrActions["extrasFixOverflowFrames"]->setEnabled(true);
	scrActions["extrasFixOverflowFramesDoc"]->setEnabled(true);
	scrActions["viewToggleCMS"]->setChecked(doc->HasCMS);
	scrActions["viewToggleWhiteSpaceMode"]->setChecked(doc->whiteSpaceModeEnabled);
	view->setRulersShown(doc->guidesPrefs().rulersShown);
	//doc emits changed() via this
	doc->setMasterPageMode(true);
	view->reformPages();
	doc->setMasterPageMode(false);
	view->reformPages();
	view->GotoPage(doc->currentPage()->pageNr());
	view->DrawNew();
	pagePalette->rebuildPages();
	emit UpdateRequest(reqCmsOptionsUpdate);
	doc->changed();
	modeToolBar->setDoc(doc);
}

int ScribusMainWindow::ShowSubs()
{
	propertiesPalette->startup();
	contentPalette->startup();
	outlinePalette->startup();
	scrapbookPalette->startup();
	bookmarkPalette->startup();
	downloadsPalette->startup();
	pagePalette->startup();
	layerPalette->startup();
	docCheckerPalette->startup();
	alignDistributePalette->startup();
	undoPalette->startup();
	guidePalette->startup();
	inlinePalette->startup();
	charPalette->startup();
	m_styleManager->startup();
	marksManager->startup();
	nsEditor->startup();
	symbolPalette->startup();

	// try to load custom layout from preferences
	dockManager->restoreWorkspaceFromPrefs();

	// init the toolbars
	// On a new user profile (nothing restored by restoreState) the stock
	// toolbars start hidden, leaving the control bar and the Tools side bar.
	// Only the *default* changes: once a toolbar has been toggled, ScToolBar
	// has stored IsVisible and that choice wins here and on every later start.
	// Existing profiles already have the key, so their layout is untouched.
	bool newProfile = m_prefsManager.appPrefs.uiPrefs.mainWinState.isEmpty();
	fileToolBar->initVisibility(!newProfile);
	editToolBar->initVisibility(!newProfile);
	modeToolBar->initVisibility();
	pdfToolBar->initVisibility(!newProfile);
	// viewToolBar never had its visibility restored from prefs at all; seed it
	// from whatever the restored window state produced so existing profiles
	// keep their current layout, and hide it on a new one.
	viewToolBar->initVisibility(newProfile ? false : viewToolBar->isVisible());

	activateWindow();
	if (!scriptIsRunning())
		raise();
	return 0;
}

void ScribusMainWindow::printPreview()
{
	const CheckerPrefs& checkerProfile = doc->checkerProfiles()[doc->curCheckProfile()];
	if (checkerProfile.autoCheck)
	{
		if (scanDocument())
		{
			if (checkerProfile.ignoreErrors)
			{
				int i = ScMessageBox::warning(this, CommonStrings::trWarning,
											"<qt>"+ tr("Scribus has detected some errors. Consider using the Preflight Verifier to correct them")+"</qt>",
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default
											QMessageBox::Ignore);	// batch default
				if (i == QMessageBox::Abort)
					return;
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doPrintPreview()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkPrintPreview;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	doPrintPreview();
}

void ScribusMainWindow::doPrintPreview()
{
	if (!HaveDoc)
		return;
	if (!( ScCore->haveGS() || ScCore->isWinGUI() ))
		return;
	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		docCheckerPalette->setIgnoreEnabled(false);
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doPrintPreview()));
	}
	PrefsContext* prefs = PrefsManager::instance().prefsFile->getContext("print_options");
	QString currentPrinter(prefs->get("CurrentPrn"));
	PrintLanguage currentEngine = (PrintLanguage) prefs->get("CurrentPrnEngine", "3").toInt();
	if (PrintPreview::usesGhostscript(currentPrinter, currentEngine) && (!ScCore->haveGS()) )
	{
		QString mess(tr("Ghostscript is missing : PostScript Print Preview is not available") + "\n\n");
		ScMessageBox::warning(this, CommonStrings::trWarning, mess);
		return;
	}
	PrintPreview *dia = new PrintPreview(this, doc, currentPrinter, currentEngine);
	previewDinUse = true;
	connect(dia, SIGNAL(doPrint()), this, SLOT(slotReallyPrint()));
	dia->exec();
	PrefsManager& prefsManager = PrefsManager::instance();
	prefsManager.appPrefs.printPreviewPrefs.PrPr_Mode = dia->isCMYKPreviewEnabled();
	prefsManager.appPrefs.printPreviewPrefs.PrPr_AntiAliasing = dia->isAntialiasingEnabled();
	prefsManager.appPrefs.printPreviewPrefs.PrPr_Transparency = dia->isTransparencyEnabled();
	if (ScCore->haveTIFFSep() && dia->useGhostscriptPreview())
	{
		prefsManager.appPrefs.printPreviewPrefs.PrPr_C = dia->isInkChannelVisible("Cyan");
		prefsManager.appPrefs.printPreviewPrefs.PrPr_M = dia->isInkChannelVisible("Magenta");
		prefsManager.appPrefs.printPreviewPrefs.PrPr_Y = dia->isInkChannelVisible("Yellow");
		prefsManager.appPrefs.printPreviewPrefs.PrPr_K = dia->isInkChannelVisible("Black");
		prefsManager.appPrefs.printPreviewPrefs.PrPr_InkCoverage = dia->isInkCoverageEnabled();
		prefsManager.appPrefs.printPreviewPrefs.PrPr_InkThreshold = dia->inkCoverageThreshold();
	}
	disconnect(dia, SIGNAL(doPrint()), this, SLOT(slotReallyPrint()));
	previewDinUse = false;
	delete dia;
}

void ScribusMainWindow::outputPreviewPDF()
{
	const CheckerPrefs& checkerProfile = doc->checkerProfiles()[doc->curCheckProfile()];
	if (checkerProfile.autoCheck)
	{
		if (scanDocument())
		{
			if (checkerProfile.ignoreErrors)
			{
				int i = ScMessageBox::warning(this, CommonStrings::trWarning,
											"<qt>"+ tr("Scribus has detected some errors. Consider using the Preflight Verifier to correct them")+"</qt>",
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default
											QMessageBox::Ignore);	// batch default
				if (i == QMessageBox::Abort)
					return;
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPDF()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkOutputPreviewPDF;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	doOutputPreviewPDF();
}

void ScribusMainWindow::doOutputPreviewPDF()
{
	if (!HaveDoc)
		return;
	if (!ScCore->haveGS())
		return;

	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		docCheckerPalette->setIgnoreEnabled(false);
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPDF()));
	}

	OutputPreview_PDF *dia = new OutputPreview_PDF(this, doc);
	previewDinUse = true;
	connect(dia, SIGNAL(doExport()), this, SLOT(doSaveAsPDF()));
	dia->exec();
	disconnect(dia, SIGNAL(doExport()), this, SLOT(doSaveAsPDF()));
	previewDinUse = false;

	PrefsManager& prefsManager = PrefsManager::instance();
	prefsManager.appPrefs.pdfOutputPreviewPrefs.cmykPreviewMode = dia->isCMYKPreviewEnabled();
	prefsManager.appPrefs.pdfOutputPreviewPrefs.enableAntiAliasing = dia->isAntialiasingEnabled();
	prefsManager.appPrefs.pdfOutputPreviewPrefs.showTransparency = dia->isTransparencyEnabled();
	if (ScCore->haveTIFFSep())
	{
		prefsManager.appPrefs.pdfOutputPreviewPrefs.isCyanVisible = dia->isInkChannelVisible("Cyan");
		prefsManager.appPrefs.pdfOutputPreviewPrefs.isMagentaVisible = dia->isInkChannelVisible("Magenta");
		prefsManager.appPrefs.pdfOutputPreviewPrefs.isYellowVisible = dia->isInkChannelVisible("Yellow");
		prefsManager.appPrefs.pdfOutputPreviewPrefs.isBlackVisible = dia->isInkChannelVisible("Black");
		prefsManager.appPrefs.pdfOutputPreviewPrefs.displayInkCoverage = dia->isInkCoverageEnabled();
		prefsManager.appPrefs.pdfOutputPreviewPrefs.inkCoverageThreshold = dia->inkCoverageThreshold();
	}

	delete dia;
}

void ScribusMainWindow::outputPreviewPS()
{
	const CheckerPrefs& checkerProfile = doc->checkerProfiles()[doc->curCheckProfile()];
	if (checkerProfile.autoCheck)
	{
		if (scanDocument())
		{
			if (checkerProfile.ignoreErrors)
			{
				int i = ScMessageBox::warning(this, CommonStrings::trWarning,
											"<qt>"+ tr("Scribus has detected some errors. Consider using the Preflight Verifier to correct them")+"</qt>",
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default
											QMessageBox::Ignore);	// batch default
				if (i == QMessageBox::Abort)
					return;
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPS()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkOutputPreviewPS;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	doOutputPreviewPS();
}

void ScribusMainWindow::doOutputPreviewPS()
{
	if (!HaveDoc)
		return;
	if (!ScCore->haveGS())
		return;

	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		docCheckerPalette->setIgnoreEnabled(false);
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPS()));
	}

	OutputPreview_PS *dia = new OutputPreview_PS(this, doc);
	previewDinUse = true;
	connect(dia, SIGNAL(doExport()), this, SLOT(slotReallyPrint()));
	dia->exec();
	disconnect(dia, SIGNAL(doExport()), this, SLOT(slotReallyPrint()));
	previewDinUse = false;

	PrefsManager& prefsManager = PrefsManager::instance();
	prefsManager.appPrefs.psOutputPreviewPrefs.psLevel = dia->postscriptLevel();
	prefsManager.appPrefs.psOutputPreviewPrefs.cmykPreviewMode = dia->isCMYKPreviewEnabled();
	prefsManager.appPrefs.psOutputPreviewPrefs.enableAntiAliasing = dia->isAntialiasingEnabled();
	prefsManager.appPrefs.psOutputPreviewPrefs.showTransparency = dia->isTransparencyEnabled();
	if (ScCore->haveTIFFSep())
	{
		prefsManager.appPrefs.psOutputPreviewPrefs.isCyanVisible = dia->isInkChannelVisible("Cyan");
		prefsManager.appPrefs.psOutputPreviewPrefs.isMagentaVisible = dia->isInkChannelVisible("Magenta");
		prefsManager.appPrefs.psOutputPreviewPrefs.isYellowVisible = dia->isInkChannelVisible("Yellow");
		prefsManager.appPrefs.psOutputPreviewPrefs.isBlackVisible = dia->isInkChannelVisible("Black");
		prefsManager.appPrefs.psOutputPreviewPrefs.displayInkCoverage = dia->isInkCoverageEnabled();
		prefsManager.appPrefs.psOutputPreviewPrefs.inkCoverageThreshold = dia->inkCoverageThreshold();
	}

	delete dia;
}

void ScribusMainWindow::viewPDFSeparations()
{
	// Unlike Print Preview / PostScript preview above, this checks an
	// arbitrary external PDF (e.g. an ad supplied by a client) rather than
	// the current document, so it doesn't need HaveDoc, Ghostscript's
	// availability is checked lazily when a file is actually opened, and the
	// dialog is left non-modal so several ads can be checked side by side.
	auto* dia = new PDFSeparationsViewer(this, this);
	dia->show();
}

bool ScribusMainWindow::DoSaveAsEps(const QString& fn, QString& error)
{
	QStringList spots;
	bool return_value = true;
	ReOrderText(doc, view);
	ScCore->fileWatcher->forceScan();
	ScCore->fileWatcher->stop();

	PrintOptions options;
	options.pageNumbers.push_back(doc->currentPage()->pageNr()+1);
	options.outputSeparations = false;
	options.separationName = "All";
	options.allSeparations = spots;
	options.useSpotColors = true;
	options.useColor = true;
	options.mirrorH = false;
	options.mirrorV = false;
	options.doGCR = m_prefsManager.appPrefs.printerPrefs.GCRMode;
	options.setDevParam = false;
	options.doClip = true;
	options.cropMarks = false;
	options.bleedMarks = false;
	options.registrationMarks = false;
	options.colorMarks = false;
	options.includePDFMarks = false;
	options.markLength = 20.0;
	options.markOffset = 0.0;
	options.bleeds.set(0, 0, 0, 0);

	PSLib *pslib = new PSLib(doc, options, PSLib::OutputEPS);
	if (pslib != nullptr)
	{
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		int psRet = pslib->createPS(fn);
		if (psRet == 1)
		{
			error = pslib->errorMessage();
			return_value = false;
		}
		delete pslib;
		QApplication::restoreOverrideCursor();
	}
	ScCore->fileWatcher->start();
	return return_value;
}

void ScribusMainWindow::SaveAsEps()
{
	if (doc->checkerProfiles()[doc->curCheckProfile()].autoCheck)
	{
		if (scanDocument())
		{
			if (doc->checkerProfiles()[doc->curCheckProfile()].ignoreErrors)
			{
				int t = ScMessageBox::warning(this, CommonStrings::trWarning,
											tr("Scribus detected some errors.\nConsider using the Preflight Verifier  to correct them."),
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default,
											QMessageBox::Ignore);	// batch default
				if (t == QMessageBox::Abort)
					return;
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(reallySaveAsEps()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkEPS;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	reallySaveAsEps();
}

void ScribusMainWindow::reallySaveAsEps()
{
	QString filename;
	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		docCheckerPalette->setIgnoreEnabled(false);
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(reallySaveAsEps()));
	}
	if (!doc->documentFileName().startsWith( tr("Document")))
	{
		QFileInfo fi(doc->documentFileName());
		if (doc->m_Selection->isNotEmpty())
			filename = fi.path() + "/" + fi.completeBaseName() + "_selection.eps";
		else
			filename = fi.path() + "/" + getFileNameByPage(doc, doc->currentPage()->pageNr(), "eps");
	}
	else
	{
		if (doc->m_Selection->isNotEmpty())
			filename = QDir::currentPath() + "/" + doc->documentFileName() + "_selection.eps";
		else
			filename = QDir::currentPath() + "/" + getFileNameByPage(doc, doc->currentPage()->pageNr(), "eps");
	}
	filename = QDir::toNativeSeparators(filename);

	PrefsContext* dirsContext = m_prefsManager.prefsFile->getContext("dirs");
	QString prefsDocDir = m_prefsManager.documentDir();
	QString workingDir = dirsContext->get("eps", prefsDocDir.isEmpty() ? "." : prefsDocDir);
	QPair<QString, uint> fileNameVersion;
	fileNameVersion =  CFileDialog(workingDir, tr("Save As"), tr("%1;;All Files (*)").arg(m_formatsManager->extensionsForFormat(FormatsManager::EPS)), filename, fdHidePreviewCheckBox | fdNone);
	if (fileNameVersion.first.isEmpty())
		return;
	QString fn (fileNameVersion.first);
	m_prefsManager.prefsFile->getContext("dirs")->set("eps", fn.left(fn.lastIndexOf("/")));
	if (!overwrite(this, fn))
		return;

	QString epsError;
	if (!DoSaveAsEps(fn, epsError))
	{
		QString message = tr("Cannot write the file: \n%1").arg(fn);
		if (!epsError.isEmpty())
			message += QString("\n%1").arg(epsError);
		ScMessageBox::warning(this, CommonStrings::trWarning, message);
	}
}

bool ScribusMainWindow::getPDFDriver(const QString &filename, const std::vector<int> & pageNumbers,
									 const QMap<int, QImage>& thumbs, QString& error, bool* cancelled)
{
	ScCore->fileWatcher->forceScan();
	ScCore->fileWatcher->stop();
	PDFlib pdflib(*doc);
	bool ret = pdflib.doExport(filename, pageNumbers, thumbs);
	if (!ret)
		error = pdflib.errorMessage();
	if (cancelled)
		*cancelled = pdflib.exportAborted();
	ScCore->fileWatcher->start();
	return ret;
}

// Preset for an export that skips the Save as PDF dialog; empty for the normal
// dialog. A file-static, not a member, so the main window's layout (which the
// plugins are compiled against) does not change.
static QString s_pdfDirectPreset;
static QMap<QString, QPointer<ScrAction> > s_pdfPresetActions;

void ScribusMainWindow::suneerRebuildPdfPresetMenu()
{
	scrMenuMgr->clearMenuStrings("FileExportPDFPreset");
	s_pdfPresetActions.clear();
	const QString defaultPreset = PdfPresets::defaultName();
	QStringList names = PdfPresets::allNames();
	const QStringList builtIn = PDFExportDialog::builtInPresets();
	for (const QString& name : builtIn)
	{
		if (!names.contains(name))
			names << name;
	}
	int i = 0;
	for (const QString& name : std::as_const(names))
	{
		// Keyed so the menu keeps this order.
		const QString key = QString("pdfPreset%1").arg(i++, 3, 10, QChar('0'));
		QString text = name;
		text.replace("&", "&&");
		if (name == defaultPreset)
			text += "  [" + tr("Default") + "]";
		s_pdfPresetActions.insert(key, new ScrAction(ScrAction::RecentFile, QString(), QString(), text, QKeySequence(), this, name));
		connect(s_pdfPresetActions[key], SIGNAL(triggeredData(QString)), this, SLOT(suneerSaveAsPDFWithPreset(QString)));
		scrMenuMgr->addMenuItemString(key, "FileExportPDFPreset");
	}
	scrMenuMgr->addMenuItemStringsToRememberedMenu("FileExportPDFPreset", s_pdfPresetActions);
}

void ScribusMainWindow::suneerSaveAsPDFDefault()
{
	if (!HaveDoc)
		return;
	const QString preset = PdfPresets::defaultName();
	if (preset.isEmpty() || (!PdfPresets::exists(preset) && !PDFExportDialog::builtInPresets().contains(preset)))
	{
		ScMessageBox::information(this, tr("Save as PDF (Default preset)"),
			tr("There is no Default preset yet.\nOpen File > Export > Save as PDF, choose your settings and press \"Use current settings as Default\"."));
		return;
	}
	suneerSaveAsPDFWithPreset(preset);
}

void ScribusMainWindow::suneerSaveAsPDFWithPreset(const QString& presetName)
{
	if (!HaveDoc || presetName.isEmpty())
		return;
	s_pdfDirectPreset = presetName;
	suneerSaveAsPDFChecked();
}

void ScribusMainWindow::SaveAsPDF()
{
	// A direct export that stopped at the Preflight Verifier and was then
	// abandoned must not turn this, the normal dialog, into a direct export.
	s_pdfDirectPreset.clear();
	suneerSaveAsPDFChecked();
}

void ScribusMainWindow::suneerSaveAsPDFChecked()
{
	if (!suneerLinkedImagesCheck(tr("PDF export")))
	{
		s_pdfDirectPreset.clear();
		return;
	}
	if (doc->checkerProfiles()[doc->curCheckProfile()].autoCheck)
	{
		if (scanDocument())
		{
			if (doc->checkerProfiles()[doc->curCheckProfile()].ignoreErrors)
			{
				int t = ScMessageBox::warning(this, CommonStrings::trWarning,
											tr("Detected some errors.\nConsider using the Preflight Verifier to correct them"),
											QMessageBox::Abort | QMessageBox::Ignore,
											QMessageBox::NoButton,	// GUI default
											QMessageBox::Ignore);	// batch default
				if (t == QMessageBox::Abort)
				{
					s_pdfDirectPreset.clear();
					return;
				}
			}
			else
			{
				connect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doSaveAsPDF()));
				docCheckerPalette->setIgnoreEnabled(true);
				docCheckerPalette->checkMode = CheckDocument::checkPDF;
				docCheckerPalette->buildErrorList(doc);
				docCheckerPalette->show();
				scrActions["toolsPreflightVerifier"]->setChecked(true);
				return;
			}
		}
	}
	doSaveAsPDF();
}

void ScribusMainWindow::doSaveAsPDF()
{
	if (docCheckerPalette->isIgnoreEnabled())
	{
		docCheckerPalette->hide();
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
		docCheckerPalette->setIgnoreEnabled(false);
		scrActions["toolsPreflightVerifier"]->setChecked(false);
		disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doSaveAsPDF()));
	}
	QMap<QString, int> ReallyUsed = doc->reorganiseFonts();
	if (doc->pdfOptions().EmbedList.count() != 0)
	{
		QList<QString> tmpEm;
		const auto& fontsToEmbed = doc->pdfOptions().EmbedList;
		for (const auto& fontName : fontsToEmbed)
		{
			if (ReallyUsed.contains(fontName))
				tmpEm.append(fontName);
		}
		doc->pdfOptions().EmbedList = tmpEm;
	}
	if (doc->pdfOptions().SubsetList.count() != 0)
	{
		QList<QString> tmpEm;
		const auto& fontsToSubset = doc->pdfOptions().SubsetList;
		for (const auto& fontName : fontsToSubset)
		{
			if (ReallyUsed.contains(fontName))
				tmpEm.append(fontName);
		}
		doc->pdfOptions().SubsetList = tmpEm;
	}
	const QString directPreset = s_pdfDirectPreset;
	s_pdfDirectPreset.clear();
	// Exporting with a preset must leave the PDF settings saved in the document
	// as they were: the export code below reads doc->pdfOptions(), so the
	// preset's values go in for the export and the document's own come back
	// when this function returns, by whichever path.
	struct DocPdfOptionsKeeper
	{
		ScribusDoc* doc;
		PDFOptions own;
		bool restore { true };
		bool keepFileName { false };
		~DocPdfOptionsKeeper()
		{
			if (!restore)
				return;
			const QString usedFileName = doc->pdfOptions().fileName;
			doc->pdfOptions() = own;
			// The file name is not a setting; remembering the last one is a convenience.
			if (keepFileName)
				doc->pdfOptions().fileName = usedFileName;
		}
	} docPdfOptions { doc, doc->pdfOptions() };

	MarginStruct optBleeds(doc->pdfOptions().bleeds);
	PDFExportDialog dia(this, doc->documentFileName(), ReallyUsed, view, doc->pdfOptions(), ScCore->PDFXProfiles, m_prefsManager.appPrefs.fontPrefs.AvailFonts, ScCore->PrinterProfiles);
	if (directPreset.isEmpty())
	{
		const int dialogResult = dia.exec();
		// Presets may have been added, deleted or made Default in the dialog.
		suneerRebuildPdfPresetMenu();
		if (!dialogResult)
			return;
	}
	else
	{
		// Straight to the file name, then export with the preset.
		QString fileName = dia.fileName();
		PrefsContext* dirs = m_prefsManager.prefsFile->getContext("dirs");
		const QString wdir = fileName.isEmpty() ? dirs->get("pdf", ScPaths::userDocumentDir()) : QFileInfo(fileName).absolutePath();
		fileName = CFileDialog(wdir, tr("Save as PDF (%1)").arg(directPreset), tr("PDF Files (*.pdf);;All Files (*)"), fileName, fdHidePreviewCheckBox).first;
		if (fileName.isEmpty())
			return;
		fileName = checkFileExtension(fileName, "pdf");
		if (!overwrite(this, fileName))
			return;
		dirs->set("pdf", fileName.left(fileName.lastIndexOf("/")));
		if (!dia.prepareDirectExport(directPreset, fileName))
		{
			ScMessageBox::warning(this, CommonStrings::trWarning, tr("The preset \"%1\" could not be read.").arg(directPreset));
			return;
		}
	}
	// No preset involved (or "Use this document's own saved settings instead"):
	// as in stock Scribus, what was exported with becomes the document's own.
	if (dia.keepsDocumentSettings())
		docPdfOptions.keepFileName = true;
	else
		docPdfOptions.restore = false;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	dia.updateDocOptions();
	doc->pdfOptions().firstUse = false;
	ReOrderText(doc, view);
	QString pageString(dia.getPagesString());
	std::vector<int> pageNs;
//	uint pageNumbersSize;
	QMap<int, QImage> allThumbs, thumbs;
	QString fileName = doc->pdfOptions().fileName;
	QString errorMsg;
	parsePagesString(pageString, &pageNs, doc->DocPages.count());
	if (doc->pdfOptions().useDocBleeds)
		doc->pdfOptions().bleeds = *doc->bleeds();

	// If necessary, generate thumbnails in one go : if color management is enabled
	// we gain lots of time by avoiding multiple color management settings change
	// and hence multiple reloading of images
	bool cmsCorr = false;
	if (doc->pdfOptions().Thumbnails &&
		doc->cmsSettings().CMSinUse &&
		doc->cmsSettings().GamutCheck)
	{
		cmsCorr = true;
		doc->cmsSettings().GamutCheck = false;
		doc->enableCMS(true);
	}

// MR: The following code has been replaced by view->PagesToPixmap(100, -1, flags);
//	pageNumbersSize = pageNs.size();
//	for (uint i = 0; i < pageNumbersSize; ++i)
//	{
//		QImage thumb(10, 10, QImage::Format_ARGB32_Premultiplied);
//		if (doc->pdfOptions().Thumbnails)
//		{
//			// No need to load full res images for drawing small thumbnail
//			PageToPixmapFlags flags = Pixmap_DontReloadImages | Pixmap_DrawWhiteBackground;
//			thumb = view->PageToPixmap(pageNs[i] - 1, 100, flags);
//		}
//		allThumbs.insert(pageNs[i], thumb);
//	}

	PageToPixmapFlags flags = Pixmap_DontReloadImages | Pixmap_DrawWhiteBackground;
	allThumbs = view->PagesToPixmap(100, -1, flags);

	if (cmsCorr)
	{
		doc->cmsSettings().GamutCheck = true;
		doc->enableCMS(true);
	}

	if (doc->pdfOptions().doMultiFile)
	{
		bool cancelled = false;
		QFileInfo fi(fileName);
		QString ext = fi.suffix();
		QString path = fi.path();
		QString name = fi.completeBaseName();
		uint aa = 0;
		while (aa < pageNs.size() && !cancelled)
		{
			thumbs.clear();
			std::vector<int> pageNs2;
			pageNs2.clear();
			pageNs2.push_back(pageNs[aa]);
//			pageNumbersSize = pageNs2.size();
			QImage thumb(10, 10, QImage::Format_ARGB32_Premultiplied);
			if (doc->pdfOptions().Thumbnails)
				thumb = allThumbs[pageNs[aa]];
			thumbs.insert(1, thumb);
			QString realName = QDir::toNativeSeparators(path + "/" + name + tr("-Page%1").arg(pageNs[aa], 3, 10, QChar('0')) + "." + ext);
			if (!getPDFDriver(realName, pageNs2, thumbs, errorMsg, &cancelled))
			{
				QApplication::restoreOverrideCursor();
				QString message = tr("Cannot write the file: \n%1").arg(doc->pdfOptions().fileName);
				if (!errorMsg.isEmpty())
					message = QString("%1\n%2").arg(message, errorMsg);
				ScMessageBox::warning(this, CommonStrings::trWarning, message);
				return;
			}
			aa++;
		}
	}
	else
	{
		if (!getPDFDriver(fileName, pageNs, allThumbs, errorMsg))
		{
			QApplication::changeOverrideCursor(QCursor(Qt::ArrowCursor));
			QString message = tr("Cannot write the file: \n%1").arg(doc->pdfOptions().fileName);
			if (!errorMsg.isEmpty())
				message = QString("%1\n%2").arg(message, errorMsg);
			ScMessageBox::warning(this, CommonStrings::trWarning, message);
		}
	}
	if (doc->pdfOptions().useDocBleeds)
		doc->pdfOptions().bleeds = optBleeds;
	QApplication::restoreOverrideCursor();
	if (errorMsg.isEmpty() && doc->pdfOptions().openAfterExport && !doc->pdfOptions().doMultiFile)
	{
		QString pdfViewer(PrefsManager::instance().appPrefs.extToolPrefs.pdfViewerExecutable);
		if (pdfViewer.isEmpty())
		{
			pdfViewer = QFileDialog::getOpenFileName(this, tr("Locate your PDF viewer"), QString(), QString());
			if (!QFileInfo::exists(pdfViewer))
				pdfViewer.clear();
			PrefsManager::instance().appPrefs.extToolPrefs.pdfViewerExecutable = pdfViewer;
		}
		if (!pdfViewer.isEmpty())
		{
			QStringList args;
			args << QDir::toNativeSeparators(doc->pdfOptions().fileName);
			QProcess::startDetached(pdfViewer, args);
		}
	}
}

//CB-->Doc, stop _storing_ bookmarks in the palette
void ScribusMainWindow::AddBookMark(PageItem *ite)
{
	bookmarkPalette->BView->addPageItem(ite);
}

//CB-->Doc, stop _storing_ bookmarks in the palette
void ScribusMainWindow::DelBookMark(PageItem *ite)
{
	bookmarkPalette->BView->deleteItem(ite);
}

//CB-->Doc, stop _storing_ bookmarks in the palette
void ScribusMainWindow::BookMarkTxT(PageItem *ite)
{
	bookmarkPalette->BView->changeText(ite);
}

//CB-->Doc, stop _storing_ bookmarks in the palette
void ScribusMainWindow::RestoreBookMarks()
{
	QList<ScribusDoc::BookMa>::Iterator it2 = doc->BookMarks.begin();
	bookmarkPalette->BView->clear();
	bookmarkPalette->BView->NrItems = 0;
	bookmarkPalette->BView->First = 1;
	bookmarkPalette->BView->Last = 0;
	if (doc->BookMarks.isEmpty())
		return;
	BookMItem* ip;
	BookMItem* ip2 = nullptr;
	BookMItem* ip3 = nullptr;
	BookMItem *ite = new BookMItem(bookmarkPalette->BView, &(*it2));
	bookmarkPalette->BView->NrItems++;
	++it2;
	for ( ; it2 != doc->BookMarks.end(); ++it2)
	{
		if ((*it2).Parent == 0)
		{
			ite = new BookMItem(bookmarkPalette->BView, ite, &(*it2));
			bookmarkPalette->BView->NrItems++;
		}
		else
		{
			QTreeWidgetItemIterator it3(bookmarkPalette->BView);
			while (*it3)
			{
				ip = (BookMItem*)(*it3);
				if ((*it2).Parent == ip->ItemNr)
				{
					ip2 = ip;
					break;
				}
				++it3;
			}
			if ((*it2).Prev == 0)
			{
				(void) new BookMItem(ip2, &(*it2));
				bookmarkPalette->BView->NrItems++;
			}
			else
			{
				QTreeWidgetItemIterator it4(bookmarkPalette->BView);
				while (*it4)
				{
					ip = (BookMItem*)(*it4);
					if ((*it2).Prev == ip->ItemNr)
					{
						ip3 = ip;
						break;
					}
					++it4;
				}
				(void) new BookMItem(ip2, ip3, &(*it2));
				bookmarkPalette->BView->NrItems++;
			}
		}
	}
	bookmarkPalette->BView->Last = bookmarkPalette->BView->NrItems;
	bookmarkPalette->BView->rebuildTree();
}

QStringList ScribusMainWindow::scrapbookNames() const
{
	return scrapbookPalette->getOpenScrapbooksNames();
}

void ScribusMainWindow::updateLayerMenu()
{
	const QSignalBlocker blocker(layerMenu);
	layerMenu->clear();
	if (doc == nullptr)
		return;

	QStringList newNames;
	doc->orderedLayerList(&newNames);
	for (const QString& newName : std::as_const(newNames))
	{
		QPixmap pm(20,15);
		pm.fill(doc->Layers.layerByName(newName)->markerColor);
		layerMenu->addItem(pm, newName);
	}

	if (layerMenu->count() != 0)
	{
		QString layerName = doc->activeLayerName();
		setCurrentComboItem(layerMenu, layerName);
	}
}


void ScribusMainWindow::gotoLayer(int l)
{
	if (!HaveDoc)
		return;
	int level = doc->layerCount()-l-1;
	int layerID = doc->layerIDFromLevel(level);
	if (layerID == -1)
		return;
	doc->setActiveLayer(layerID);
	changeLayer(doc->activeLayer());
	emit changeLayers(layerID);
}

//CB-->Doc, stop _storing_ bookmarks in the palette
void ScribusMainWindow::StoreBookmarks()
{
	doc->BookMarks.clear();
	BookMItem* ip;
	QTreeWidgetItemIterator it(bookmarkPalette->BView);
	struct ScribusDoc::BookMa Boma;
	while (*it)
	{
		ip = (BookMItem*)(*it);
		Boma.Title = ip->Title;
		Boma.Text = ip->text(0);
		Boma.Action = ip->Action;
		Boma.ItemNr = ip->ItemNr;
		Boma.PageObject = ip->PageObject;
//		Boma.Seite = ip->Seite;
// 		Boma.Element = ip->Element;
		Boma.Parent = ip->Pare;
		Boma.First = ip->First;
		Boma.Prev = ip->Prev;
		Boma.Next = ip->Next;
		Boma.Last = ip->Last;
		doc->BookMarks.append(Boma);
		++it;
	}
	doc->NrItems = bookmarkPalette->BView->NrItems;
	doc->First = bookmarkPalette->BView->First;
	doc->Last = bookmarkPalette->BView->Last;
}

void ScribusMainWindow::slotElemRead(const QString& xml, double x, double y, bool art, bool loca, ScribusDoc* docc, ScribusView* vie)
{
	if (doc == docc && docc->appMode == modeEditClip)
		view->requestMode(submodeEndNodeEdit);

	ScriXmlDoc ss;
	if (ss.readElem(xml, docc, x, y, art, loca))
	{
		vie->DrawNew();
		if (doc == docc)
		{
			emit UpdateRequest(reqColorsUpdate | reqTextStylesUpdate | reqLineStylesUpdate);
			slotDocCh();
		}
	}
}

void ScribusMainWindow::slotChangeUnit(int unitIndex, bool draw)
{
	// Hack
	QByteArray stylesheet;
	if (loadRawText(ScPaths::preferencesDir() + "/stylesheet.css", stylesheet))
	{
		qApp->setStyleSheet(QString(stylesheet));
	}
	doc->setUnitIndex(unitIndex);
	setCurrentComboItem(unitSwitcher, unitGetStrFromIndex(doc->unitIndex()));
	view->unitChange();
	propertiesPalette->unitChange();
	contentPalette->unitChange();
	nodePalette->unitChange();
	alignDistributePalette->unitChange();
	if (m_suneerControlBar)
		m_suneerControlBar->unitChange();
	guidePalette->setupPage();
	m_styleManager->unitChange();
	m_widgetManager.unitChange();
	if (draw)
		view->DrawNew();
}

void ScribusMainWindow::ManageJava()
{
	JavaDocs *dia = new JavaDocs(this, doc, view);
	connect(dia, SIGNAL(docChanged(bool)), this, SLOT(slotDocCh(bool)));
	dia->exec();
	disconnect(dia, SIGNAL(docChanged(bool)), this, SLOT(slotDocCh(bool)));
	delete dia;
}

void ScribusMainWindow::editSelectedSymbolStart()
{
	if (doc->m_Selection->count() > 0)
		editSymbolStart(doc->m_Selection->itemAt(0)->pattern());
}

void ScribusMainWindow::suneerEnlargeTextFrame()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isTextFrame()) return;
	doc->sizeItem(item->width() * 1.05, item->height() * 1.05, item);
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

void ScribusMainWindow::suneerReduceTextFrame()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isTextFrame()) return;
	doc->sizeItem(item->width() * 0.95, item->height() * 0.95, item);
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

// Convert the selected text frame into a table. Paragraphs (PARSEP) become rows,
// tab-separated fields become columns. The column count is taken from the row with
// the most fields. The table is created at the same position/size as the frame.
void ScribusMainWindow::suneerTextToTable()
{
	if (!doc || doc->m_Selection->isEmpty())
		return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isTextFrame())
		return;

	// 1. Extract the raw story text, preserving TAB and PARSEP characters.
	int len = item->itemText.length();
	if (len <= 0)
	{
		ScMessageBox::information(this, tr("Convert to Table"), tr("The selected text frame is empty."));
		return;
	}
	QString raw = item->itemText.text(0, len);

	// 2. Split into paragraphs -> rows.
	QStringList rows = raw.split(SpecialChars::PARSEP);
	// Drop a trailing empty paragraph (common when text ends with a paragraph break).
	while (rows.size() > 1 && rows.last().isEmpty())
		rows.removeLast();
	if (rows.isEmpty())
		return;

	// 3. Split each row by TAB -> columns, and find the maximum column count.
	QList<QStringList> grid;
	grid.reserve(rows.size());
	int numColumns = 0;
	for (const QString& row : rows)
	{
		QStringList cols = row.split(SpecialChars::TAB);
		numColumns = qMax(numColumns, static_cast<int>(cols.size()));
		grid.append(cols);
	}
	int numRows = grid.size();
	if (numRows < 1 || numColumns < 1)
		return;

	// 4. Remember the frame geometry and layer.
	double x = item->xPos();
	double y = item->yPos();
	double w = item->width();
	double h = item->height();
	int layerID = item->m_layerID;

	UndoTransaction trans;
	if (UndoManager::undoEnabled())
		trans = m_undoManager->beginTransaction(Um::SelectionGroup, Um::ITable, tr("Convert Text to Table"), QString(), Um::ITable);

	// 5. Create the table at the same position/size.
	doc->dontResize = true;
	int z = doc->itemAdd(PageItem::Table, PageItem::Unspecified, x, y, w, h, 0, CommonStrings::None, CommonStrings::None);
	PageItem_Table* table = doc->Items->at(z)->asTable();
	table->setLayer(layerID);
	table->insertRows(0, numRows - 1);
	table->insertColumns(0, numColumns - 1);
	table->adjustTableToFrame();
	table->adjustFrameToTable();
	doc->dontResize = false;

	// 6. Fill each cell with its corresponding text.
	for (int r = 0; r < numRows; ++r)
	{
		const QStringList& cols = grid.at(r);
		for (int c = 0; c < cols.size() && c < numColumns; ++c)
		{
			const QString& cellText = cols.at(c);
			if (cellText.isEmpty())
				continue;
			TableCell cell = table->cellAt(r, c);
			if (cell.isValid() && cell.textFrame())
				cell.textFrame()->itemText.insertChars(cellText);
		}
	}
	table->adjustFrameToTable();

	// 7. Optionally delete the original text frame.
	QMessageBox::StandardButton ret = ScMessageBox::question(this, tr("Convert to Table"),
			tr("Delete the original text frame?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

	// Drop the original frame from the current selection before touching it.
	doc->m_Selection->clear();

	if (ret == QMessageBox::Yes)
	{
		Selection tmpSelection(this, false);
		tmpSelection.addItem(item);
		doc->itemSelection_DeleteItem(&tmpSelection);
	}

	if (trans)
		trans.commit();

	doc->m_Selection->addItem(table);
	table->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

// StoryText has no undo hooks of its own, so applying styles straight into it
// leaves nothing for Ctrl+Z. These two mirror the states that
// ScribusDoc::itemSelection_ApplyCharStyle() and itemSelection_ApplyParagraphStyle()
// record, which PageItem::restoreCharStyle()/restoreParagraphStyle() already know
// how to reverse.
namespace
{
	void suneerRecordAndApplyCharStyle(UndoManager* um, PageItem* item, StoryText& text,
	                                   int start, int length, const CharStyle& cs)
	{
		if (length <= 0)
			return;
		if (UndoManager::undoEnabled())
		{
			auto* is = new ScOldNewState<CharStyle>(Um::ApplyTextStyle);
			is->set("APPLY_CHARSTYLE");
			is->set("START", start);
			is->set("LENGTH", length);
			is->setStates(text.charStyle(start), cs);
			um->action(item, is);
		}
		text.applyCharStyle(start, length, cs);
	}

	void suneerRecordAndApplyParaStyle(UndoManager* um, PageItem* item, StoryText& text,
	                                   int pos, const ParagraphStyle& ps)
	{
		if (UndoManager::undoEnabled())
		{
			auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
			is->set("APPLY_PARASTYLE");
			is->set("POS", pos);
			is->setStates(text.paragraphStyle(pos), ps);
			um->action(item, is);
		}
		text.applyStyle(pos, ps);
	}
}

void ScribusMainWindow::suneerEnlargeTextSize()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isTextFrame()) return;
	StoryText& text = item->itemText;
	int start = 0, end = text.length();
	if (doc->appMode == modeEdit)
	{
		if (text.endOfSelection() > text.startOfSelection())
		{
			start = text.startOfSelection();
			end = text.endOfSelection();
		}
		else
		{
			int cursorPos = text.cursorPosition();
			start = text.startOfParagraph(text.nrOfParagraph(cursorPos));
			end = text.endOfParagraph(text.nrOfParagraph(cursorPos));
		}
	}
	// One undo step for the whole press: the size change spans many runs and the
	// line spacing change rides along with it.
	UndoTransaction sizeTransaction;
	if (UndoManager::undoEnabled())
		sizeTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                  Um::ApplyTextStyle, QString(), Um::IFont);

	// Font size change. Applied per run of equal size rather than per character:
	// the new size depends only on the old one, so characters that share a size
	// share a result — and one undo state per run beats one per character.
	int runStart = start;
	while (runStart < end)
	{
		const double runSize = text.charStyle(runStart).fontSize();
		int runEnd = runStart;
		while (runEnd < end && text.charStyle(runEnd).fontSize() == runSize)
			++runEnd;
		CharStyle cs;
		double currentSize = runSize / 10.0;
		cs.setFontSize(qRound(currentSize * 10) + 3);
		suneerRecordAndApplyCharStyle(m_undoManager, item, text, runStart, runEnd - runStart, cs);
		runStart = runEnd;
	}
	// Line spacing proportionally change
	{
		ParagraphStyle ps = text.paragraphStyle(start);
		if (ps.lineSpacingMode() == ParagraphStyle::FixedLineSpacing)
		{
			double currentLS = ps.lineSpacing();
			ps.setLineSpacing(currentLS + 0.3);
			suneerRecordAndApplyParaStyle(m_undoManager, item, text, start, ps);
		}
	}

	if (sizeTransaction)
		sizeTransaction.commit();

	item->invalidateLayout();
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
	emit UpdateRequest(reqTextStylesUpdate);
}
void ScribusMainWindow::suneerReduceTextSize()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isTextFrame()) return;
	StoryText& text = item->itemText;
	int start = 0, end = text.length();
	if (doc->appMode == modeEdit)
	{
		if (text.endOfSelection() > text.startOfSelection())
		{
			start = text.startOfSelection();
			end = text.endOfSelection();
		}
		else
		{
			int cursorPos = text.cursorPosition();
			start = text.startOfParagraph(text.nrOfParagraph(cursorPos));
			end = text.endOfParagraph(text.nrOfParagraph(cursorPos));
		}
	}
	// One undo step for the whole press; see suneerEnlargeTextSize().
	UndoTransaction sizeTransaction;
	if (UndoManager::undoEnabled())
		sizeTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                  Um::ApplyTextStyle, QString(), Um::IFont);

	// Font size change, one state per run of equal size.
	int runStart = start;
	while (runStart < end)
	{
		const double runSize = text.charStyle(runStart).fontSize();
		int runEnd = runStart;
		while (runEnd < end && text.charStyle(runEnd).fontSize() == runSize)
			++runEnd;
		CharStyle cs;
		double currentSize = runSize / 10.0;
		cs.setFontSize(qMax(10, qRound(currentSize * 10) - 3));
		suneerRecordAndApplyCharStyle(m_undoManager, item, text, runStart, runEnd - runStart, cs);
		runStart = runEnd;
	}
	// Line spacing proportionally change
	{
		ParagraphStyle ps = text.paragraphStyle(start);
		if (ps.lineSpacingMode() == ParagraphStyle::FixedLineSpacing)
		{
			double currentLS = ps.lineSpacing();
			ps.setLineSpacing(qMax(1.0, currentLS - 0.3));
			suneerRecordAndApplyParaStyle(m_undoManager, item, text, start, ps);
		}
	}

	if (sizeTransaction)
		sizeTransaction.commit();

	item->invalidateLayout();
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
	emit UpdateRequest(reqTextStylesUpdate);
}
void ScribusMainWindow::suneerEnlargeLineSpacing()
{
    if (!doc || doc->m_Selection->isEmpty()) return;
    PageItem* item = doc->m_Selection->itemAt(0);
    if (!item || !item->isTextFrame()) return;
    StoryText& text = item->itemText;
    int start = 0;
    int end = 0;
    if (doc->appMode == modeEdit && text.endOfSelection() > text.startOfSelection())
    {
        start = text.startOfSelection();
        end = text.endOfSelection();
    }
    else
    {
        int cursorPos = text.cursorPosition();
        start = text.startOfParagraph(text.nrOfParagraph(cursorPos));
        end = start + 1;
    }
    int startPara = text.nrOfParagraph(start);
    int endPara = text.nrOfParagraph(qMax(start, end - 1));
    // One undo step per press, covering every paragraph in scope.
    UndoTransaction lsTransaction;
    if (UndoManager::undoEnabled())
        lsTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
                                                        Um::SetStyle, QString(), Um::IFont);
    for (int para = startPara; para <= endPara; para++)
    {
        int paraStart = text.startOfParagraph(para);
        ParagraphStyle ps = text.paragraphStyle(paraStart);
        double currentLS = ps.lineSpacing();
        ps.setLineSpacingMode(ParagraphStyle::FixedLineSpacing);
        ps.setLineSpacing(currentLS + 1.0);
        suneerRecordAndApplyParaStyle(m_undoManager, item, text, paraStart, ps);
    }
    if (lsTransaction)
        lsTransaction.commit();
    item->invalidateLayout();
    item->update();
    doc->regionsChanged()->update(QRectF());
    doc->changed();
    emit UpdateRequest(reqTextStylesUpdate);
}
void ScribusMainWindow::suneerReduceLineSpacing()
{
    if (!doc || doc->m_Selection->isEmpty()) return;
    PageItem* item = doc->m_Selection->itemAt(0);
    if (!item || !item->isTextFrame()) return;
    StoryText& text = item->itemText;
    int start = 0;
    int end = 0;
    if (doc->appMode == modeEdit && text.endOfSelection() > text.startOfSelection())
    {
        start = text.startOfSelection();
        end = text.endOfSelection();
    }
    else
    {
        int cursorPos = text.cursorPosition();
        start = text.startOfParagraph(text.nrOfParagraph(cursorPos));
        end = start + 1;
    }
    int startPara = text.nrOfParagraph(start);
    int endPara = text.nrOfParagraph(qMax(start, end - 1));
    // One undo step per press, covering every paragraph in scope.
    UndoTransaction lsTransaction;
    if (UndoManager::undoEnabled())
        lsTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
                                                        Um::SetStyle, QString(), Um::IFont);
    for (int para = startPara; para <= endPara; para++)
    {
        int paraStart = text.startOfParagraph(para);
        ParagraphStyle ps = text.paragraphStyle(paraStart);
        double currentLS = ps.lineSpacing();
        ps.setLineSpacingMode(ParagraphStyle::FixedLineSpacing);
        ps.setLineSpacing(qMax(1.0, currentLS - 1.0));
        suneerRecordAndApplyParaStyle(m_undoManager, item, text, paraStart, ps);
    }
    if (lsTransaction)
        lsTransaction.commit();
    item->invalidateLayout();
    item->update();
    doc->regionsChanged()->update(QRectF());
    doc->changed();
    emit UpdateRequest(reqTextStylesUpdate);
}
void ScribusMainWindow::suneerEnlargeImageSize()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || !item->imageIsAvailable) return;
	item->setImageXYScale(item->imageXScale() * 1.05, item->imageYScale() * 1.05);
	// setImageXYScale() records nothing on its own; checkChanges() turns the
	// change into an IMAGE_SCALE undo state. One state, so no transaction.
	item->checkChanges();
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

void ScribusMainWindow::suneerReduceImageSize()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || !item->imageIsAvailable) return;
	item->setImageXYScale(item->imageXScale() * 0.95, item->imageYScale() * 0.95);
	// setImageXYScale() records nothing on its own; checkChanges() turns the
	// change into an IMAGE_SCALE undo state. One state, so no transaction.
	item->checkChanges();
	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

void ScribusMainWindow::suneerScaleImageUp()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || !item->imageIsAvailable) return;

	// sizeItem() records the frame resize itself, but the image scale and offset
	// setters record nothing. Without checkChanges() below, undo restored the
	// frame and left the picture scaled and shifted inside it — a revert that
	// looks complete and is not. The transaction keeps the three together.
	UndoTransaction scaleTransaction;
	if (UndoManager::undoEnabled())
		scaleTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                   Um::ImageScale, QString(), Um::IMove);

	if (doc->appMode == modeEdit)
	{
		item->setImageXYScale(item->imageXScale() * 1.05, item->imageYScale() * 1.05);
	}
	else
	{
		double newOffX = item->imageXOffset() * 1.05;
		double newOffY = item->imageYOffset() * 1.05;
		doc->sizeItem(item->width() * 1.05, item->height() * 1.05, item);
		item->setImageXYScale(item->imageXScale() * 1.05, item->imageYScale() * 1.05);
		item->setImageXYOffset(newOffX, newOffY);
	}
	// Turns the mutations above into IMAGE_SCALE / IMAGE_OFFSET undo states.
	item->checkChanges();
	if (scaleTransaction)
		scaleTransaction.commit();

	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}

void ScribusMainWindow::suneerScaleImageDown()
{
	if (!doc || doc->m_Selection->isEmpty()) return;
	PageItem* item = doc->m_Selection->itemAt(0);
	if (!item || !item->isImageFrame() || !item->imageIsAvailable) return;

	// sizeItem() records the frame resize itself, but the image scale and offset
	// setters record nothing. Without checkChanges() below, undo restored the
	// frame and left the picture scaled and shifted inside it — a revert that
	// looks complete and is not. The transaction keeps the three together.
	UndoTransaction scaleTransaction;
	if (UndoManager::undoEnabled())
		scaleTransaction = m_undoManager->beginTransaction(item->getUName(), item->getUPixmap(),
		                                                   Um::ImageScale, QString(), Um::IMove);

	if (doc->appMode == modeEdit)
	{
		item->setImageXYScale(item->imageXScale() * 0.95, item->imageYScale() * 0.95);
	}
	else
	{
		double newOffX = item->imageXOffset() * 0.95;
		double newOffY = item->imageYOffset() * 0.95;
		doc->sizeItem(item->width() * 0.95, item->height() * 0.95, item);
		item->setImageXYScale(item->imageXScale() * 0.95, item->imageYScale() * 0.95);
		item->setImageXYOffset(newOffX, newOffY);
	}
	// Turns the mutations above into IMAGE_SCALE / IMAGE_OFFSET undo states.
	item->checkChanges();
	if (scaleTransaction)
		scaleTransaction.commit();

	item->update();
	doc->regionsChanged()->update(QRectF());
	doc->changed();
}


// Adds or removes the News Browser tab in the Paragraph Styles docker.
// The tab is absent entirely when the feature is off, so newsrooms that do not
// use the database see a clean two-tab docker rather than an empty third tab.
void ScribusMainWindow::suneerSetNewsBrowserTabVisible(bool visible)
{
	if (!m_suneerNewsPanel || !paragraphStylesPanelTabs)
		return;
	QWidget* newsInner = m_suneerNewsPanel->widget();
	if (!newsInner)
		return;
	if (scrActions.contains("toolsNewsPanel") && scrActions["toolsNewsPanel"])
		scrActions["toolsNewsPanel"]->setVisible(visible);
	if (visible)
	{
		newsInner->show();
		paragraphStylesPanelTabs->addExtraTab(newsInner, tr("News Browser"));
	}
	else
		paragraphStylesPanelTabs->removeExtraTab(newsInner);
}

void ScribusMainWindow::suneerAutoFitHeight()
{
	if (!HaveDoc) return;
	int count = doc->m_Selection->count();
	if (count == 0) return;

	// autoFitFrameHeight() opens a transaction of its own, so fitting a
	// multi-frame selection produced one undo step per frame. Nesting them in
	// an outer transaction makes the whole selection a single Ctrl+Z.
	UndoTransaction fitTransaction;
	if (UndoManager::undoEnabled())
		fitTransaction = m_undoManager->beginTransaction(Um::Selection, Um::IGroup,
		                                                Um::Resize, QString(), Um::IResize);
	for (int i = 0; i < count; ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (item && item->isTextFrame() && !item->asTextFrame()->isTableItem)
		{
			// Skip autoFit if frame has span columns + image wrap (causes infinite loop)
			bool hasSpan = false;
			PageItem_TextFrame* tf = item->asTextFrame();
			for (int ci = 0; ci < tf->itemText.length(); ++ci)
				if (tf->itemText.paragraphStyle(ci).spanColumns() != 0)
					{ hasSpan = true; break; }
			tf->autoFitFrameHeight();
		}
	}

	if (fitTransaction)
		fitTransaction.commit();
}

// Item > Adjust > Fit Caption Frame. Manual only: sets the height of every
// selected text frame so the space under the last line's ink equals the
// space above the first line's ink. One undo step for the whole selection.
void ScribusMainWindow::suneerFitCaptionFrames()
{
	if (!HaveDoc || !doc)
		return;
	const int count = doc->m_Selection->count();
	if (count == 0)
		return;

	UndoTransaction fitTransaction;
	if (UndoManager::undoEnabled())
		fitTransaction = m_undoManager->beginTransaction(Um::Selection, Um::IGroup, Um::Resize,
		                                                tr("Fit caption frame"), Um::IResize);
	int fitted = 0, skipped = 0;
	for (int i = 0; i < count; ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		PageItem_TextFrame* tf = (item && item->isTextFrame()) ? item->asTextFrame() : nullptr;
		// Empty frames, table cells and linked frames are left as they are.
		if (!tf || tf->isTableItem || tf->itemText.length() == 0)
		{
			++skipped;
			continue;
		}
		if (tf->suneerFitCaptionHeight(true))
			++fitted;
		else
			++skipped;
	}
	if (fitTransaction)
	{
		if (fitted > 0)
			fitTransaction.commit();
		else
			fitTransaction.cancel();
	}
	if (fitted > 0)
		view->DrawNew();
	setStatusBarInfoText(skipped > 0
		? tr("Fit Caption Frame: %1 fitted, %2 left unchanged").arg(fitted).arg(skipped)
		: tr("Fit Caption Frame: %1 fitted").arg(fitted));
}

void ScribusMainWindow::suneerAutoFitTextToggled(bool enabled)
{
	if (!HaveDoc || !doc)
		return;
	const int count = doc->m_Selection->count();
	if (count == 0)
		return;

	UndoTransaction fitTransaction;
	if (UndoManager::undoEnabled())
		fitTransaction = m_undoManager->beginTransaction(Um::Selection, Um::IGroup, Um::ApplyTextStyle,
		                                                 enabled ? tr("Auto fit text to frame")
		                                                         : tr("Stop auto fitting text"),
		                                                 Um::IFont);

	int fitted = 0, refused = 0;
	for (int i = 0; i < count; ++i)
	{
		PageItem* item = doc->m_Selection->itemAt(i);
		if (!item || !item->isTextFrame() || item->asTextFrame()->isTableItem)
			continue;
		PageItem_TextFrame* tf = item->asTextFrame();
		item->setAutoFitText(enabled);
		if (enabled)
		{
			if (!tf->autoFitTextEligible())
			{
				++refused;
				continue;
			}
			if (tf->autoFitTextToFrame(true))
				++fitted;
			else
				++refused;
		}
		else
		{
			// Give the operator their own typography back.
			tf->autoFitTextRestore(true);
		}
	}

	if (fitTransaction)
		fitTransaction.commit();

	doc->changed();
	doc->regionsChanged()->update(QRectF());
	view->DrawNew();

	if (!enabled)
	{
		setStatusBarInfoText(tr("Auto Fit Text off; original typography restored"));
		return;
	}
	QString msg = tr("Auto fitted %1 frame(s)").arg(fitted);
	if (refused > 0)
		msg += tr("; %1 could not be fitted within the readability limits").arg(refused);
	setStatusBarInfoText(msg);
}

void ScribusMainWindow::suneerAutoflowToNewPages()
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->count() != 1)
		return;
	PageItem* srcItem = doc->m_Selection->itemAt(0);
	if (!srcItem || !srcItem->isTextFrame())
		return;
	PageItem_TextFrame* src = srcItem->asTextFrame();

	// R2: do not silently reroute an existing chain.
	if (src->nextInChain() != nullptr)
	{
		setStatusBarInfoText( tr("Frame is already linked to another frame. Break the chain first."));
		return;
	}
	// Nothing to do unless it actually overflows.
	if (!src->frameOverflows() || src->OwnPage < 0)
		return;

	// Snapshot the source frame's layout (reuse the styled-copy snapshot getters).
	SuneerFrameSnapshot snap;
	snap.valid        = true;
	snap.width        = src->width();
	snap.height       = src->height();
	snap.columns      = src->columns();
	snap.columnGap    = src->columnGap();
	snap.distL        = src->textToFrameDistLeft();
	snap.distR        = src->textToFrameDistRight();
	snap.distT        = src->textToFrameDistTop();
	snap.distB        = src->textToFrameDistBottom();
	snap.wrapT        = src->wrapOffsetTop();
	snap.wrapB        = src->wrapOffsetBottom();
	snap.wrapL        = src->wrapOffsetLeft();
	snap.wrapR        = src->wrapOffsetRight();
	snap.textFlowMode = static_cast<int>(src->textFlowMode());

	ScPage* srcPage = doc->DocPages.at(src->OwnPage);
	const QString masterName = srcPage->masterPageName();
	// Position of the source frame relative to its page's top-left, so each new frame lands at
	// the same spot on its (same-master) page.
	const double relX = src->xPos() - srcPage->xOffset();
	const double relY = src->yPos() - srcPage->yOffset();

	// Prevent addPage()'s auto-text-frame from double-firing while we create/link our own frames.
	const bool savedAutoFrames = doc->usesAutomaticTextFrames();
	doc->setUsesAutomaticTextFrames(false);

	// R6: one undo transaction wraps every page-add, frame-add and link.
	UndoTransaction trans;
	if (UndoManager::undoEnabled())
		trans = m_undoManager->beginTransaction(doc->getUName(), Um::IDocument, Um::InsertFrame, tr("Autoflow to New Pages"), Um::ICreate);

	const int kSafetyCap = 500;   // R5
	int created = 0;
	PageItem_TextFrame* prev = src;
	QStringList masterList;
	masterList << masterName;

	while (prev->frameOverflows() && created < kSafetyCap)
	{
		const int newPageIndex = prev->OwnPage + 1;

		// Add a page immediately after prev's page using prev's master (records ADD_PAGE undo,
		// reflows page geometry). addNewPages restores the current page to the original afterwards.
		addNewPages(newPageIndex, 1, 1,
		            doc->pageHeight(), doc->pageWidth(), doc->pageOrientation(), doc->pageSize(),
		            false, &masterList);

		ScPage* newPage = doc->DocPages.at(newPageIndex);
		doc->setCurrentPage(newPage);   // itemAdd assigns OwnPage from the current page

		const double newX = newPage->xOffset() + relX;
		const double newY = newPage->yOffset() + relY;
		int z = doc->itemAdd(PageItem::TextFrame, PageItem::Unspecified,
		                     newX, newY, snap.width, snap.height,
		                     doc->itemToolPrefs().shapeLineWidth, CommonStrings::None,
		                     doc->itemToolPrefs().textFont);   // records CREATE_ITEM undo
		if (z < 0)
			break;
		PageItem_TextFrame* nf = doc->Items->at(z)->asTextFrame();
		if (!nf)
			break;

		// Replicate source layout (width/height already set at itemAdd; each setter records undo).
		nf->setColumns(snap.columns);
		nf->setColumnGap(snap.columnGap);
		nf->setTextToFrameDist(snap.distL, snap.distR, snap.distT, snap.distB);
		nf->setTextFlowMode(static_cast<PageItem::TextFlowMode>(snap.textFlowMode));
		nf->setWrapOffsets(snap.wrapT, snap.wrapB, snap.wrapL, snap.wrapR);

		if (!prev->canBeLinkedTo(nf))
			break;
		prev->link(nf);   // shares the StoryText into nf, records LINK_TEXT_FRAME undo

		// R8: reflow prev then nf so the NEXT overflow check reads accurate (not stale) state.
		prev->layout();
		nf->layout();

		++created;
		prev = nf;
	}

	if (trans)
		trans.commit();

	doc->setUsesAutomaticTextFrames(savedAutoFrames);

	// Refresh view / page palette.
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	updateGUIAfterPagesChanged();
	view->DrawNew();
	slotDocCh(false);

	const bool cappedStillOverflowing = (created >= kSafetyCap) && prev->frameOverflows();
	if (cappedStillOverflowing)
		setStatusBarInfoText( tr("Autoflow stopped at 500 pages — text may still overflow. Check content."));
	else
		setStatusBarInfoText( tr("Autoflow: created %1 new pages").arg(created));
}

// ---------------------------------------------------------------------------
// Fix Overflowing Frames — grow-only autofit over a batch of text frames.
//
// Only frames that actually overflow are touched, so frames that already fit
// keep the geometry they were saved with. That is the whole point: Select All
// + Ctrl+Alt+C also *shrinks* frames that are fine, which destroys a newspaper
// grid. Legacy 1.5.x pages typically need 1-4% more height on a handful of
// frames, and hunting those down by hand across a full page is the tedium this
// replaces.
//
// Never runs automatically. Per 1863b8f nothing may mutate saved geometry on
// document open; this is an explicit operator action with a single undo step.
// ---------------------------------------------------------------------------

//! Growth beyond this fraction of the frame's height is refused and reported
//! instead: that much overset is an editorial decision, not a layout nudge.
static const double SUNEER_OVERFLOW_GROWTH_CAP = 0.10;

namespace
{
	// Frames a batch grow must not touch. A frame that is not the tail of its
	// chain must not absorb its own overflow — that text belongs downstream.
	// Note frames are owned by the notes machinery, group children would need
	// the group's geometry rebuilt around them, and locked means locked.
	bool suneerOverflowFrameIneligible(const PageItem* item)
	{
		return !item
		    || !item->isTextFrame()
		    || item->isNoteFrame()
		    || item->isGroupChild()
		    || item->locked()
		    || item->nextInChain() != nullptr;
	}
}

double ScribusMainWindow::suneerMeasureFitHeight(PageItem_TextFrame* tf) const
{
	if (!tf)
		return -1.0;

	// The probe resizes and relayouts repeatedly. Undo is suspended for it so
	// the batch transaction records only the final height of each frame.
	const bool undoWasOn = UndoManager::undoEnabled();
	if (undoWasOn)
		m_undoManager->setUndoEnabled(false);

	const double origHeight = tf->height();
	auto tryHeight = [tf](double h) {
		tf->setHeight(h);
		tf->updateClip();
		tf->invalid = true;
		tf->layout();
		return !tf->frameOverflows();
	};

	// Grow geometrically until it fits, then bisect for the smallest height
	// that still fits. Bounded so a frame that can never fit (an image-wrapped
	// column narrower than a single word) terminates instead of spinning.
	double low = origHeight, high = origHeight;
	bool fits = false;
	while (high < 20000.0)
	{
		high = qMin(high * 1.5 + 1.0, 20000.0);
		if (tryHeight(high))
		{
			fits = true;
			break;
		}
		low = high;
	}
	double needed = -1.0;
	if (fits)
	{
		for (int i = 0; i < 20; ++i)
		{
			const double mid = (low + high) / 2.0;
			if (tryHeight(mid))
				high = mid;
			else
				low = mid;
		}
		// Same slack autoFitFrameHeight() applies, so the cap is measured
		// against the height that will actually be set.
		needed = high + tf->textToFrameDistBottom() + 2.0;
	}

	tryHeight(origHeight);
	if (undoWasOn)
		m_undoManager->setUndoEnabled(true);
	return needed;
}

void ScribusMainWindow::suneerFixOverflowFramesRun(bool wholeDocument)
{
	if (!HaveDoc || !doc)
		return;
	if (doc->masterPageMode())
	{
		setStatusBarInfoText( tr("Fix Overflowing Frames does not run in master page mode"));
		return;
	}

	// Scope: an explicit selection wins for the page-scoped action; otherwise
	// the current page. The document-wide entry always sweeps every page.
	QList<PageItem*> candidates;
	if (!wholeDocument && doc->m_Selection->count() > 0)
	{
		for (int i = 0; i < doc->m_Selection->count(); ++i)
			candidates.append(doc->m_Selection->itemAt(i));
	}
	else
	{
		const int currentPage = doc->currentPageNumber();
		for (PageItem* item : std::as_const(*doc->Items))
		{
			if (wholeDocument || item->OwnPage == currentPage)
				candidates.append(item);
		}
	}

	QList<PageItem*> tooBig;
	int fixed = 0;

	UndoTransaction batch;
	if (UndoManager::undoEnabled())
		batch = m_undoManager->beginTransaction(Um::Selection, Um::ITextFrame, Um::Resize,
		                                        tr("Fix overflowing frames"), Um::IResize);

	for (PageItem* item : std::as_const(candidates))
	{
		if (suneerOverflowFrameIneligible(item))
			continue;
		PageItem_TextFrame* tf = item->asTextFrame();
		if (!tf || !tf->frameOverflows())
			continue;

		const double origHeight = tf->height();
		const double needed = suneerMeasureFitHeight(tf);
		if (needed <= 0.0 || needed > origHeight * (1.0 + SUNEER_OVERFLOW_GROWTH_CAP))
		{
			tooBig.append(item);
			continue;
		}
		// Resize through the same path as Ctrl+Alt+C so the result is identical
		// to fixing the frame by hand. Its transaction nests inside the batch.
		tf->autoFitFrameHeight();
		++fixed;
	}

	if (batch)
		batch.commit();

	doc->changed();
	doc->regionsChanged()->update(QRectF());
	view->DrawNew();

	const int capPercent = qRound(SUNEER_OVERFLOW_GROWTH_CAP * 100.0);
	QString summary = tr("Fixed %1 overflowing frame(s)").arg(fixed);
	if (!tooBig.isEmpty())
		summary += tr("; %1 skipped (would grow more than %2%)").arg(tooBig.count()).arg(capPercent);
	setStatusBarInfoText(summary);

	if (tooBig.isEmpty())
		return;

	// Frames needing more than the cap are an editorial call, so name them and
	// offer to select them rather than growing them behind the operator's back.
	QStringList names;
	for (const PageItem* item : std::as_const(tooBig))
		names << item->itemName();
	ScMessageBox box(QMessageBox::Information, tr("Fix Overflowing Frames"), summary, QMessageBox::NoButton, this);
	box.setInformativeText(tr("These frames need more than %1% extra height and were left alone:\n%2")
	                       .arg(capPercent).arg(names.join(", ")));
	QPushButton* selectButton = box.addButton(tr("Select Them"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() != selectButton)
		return;

	view->deselectItems(true);
	doc->m_Selection->delaySignalsOn();
	for (PageItem* item : std::as_const(tooBig))
		doc->m_Selection->addItem(item);
	doc->m_Selection->delaySignalsOff();
	view->DrawNew();
}

// ---------------------------------------------------------------------------
// On-open prompt for legacy documents.
//
// 1.7.3 measures text a little taller than 1.5.6 did, so pages laid out by an
// older Scribus can open with frames a fraction of a line short. This offers to
// nudge them on open. It is stricter than the manual Extras command: the
// operator did not ask for this one, so the growth cap is tighter and a frame
// that would grow onto a neighbour is left alone and reported.
// ---------------------------------------------------------------------------

//! Tighter than SUNEER_OVERFLOW_GROWTH_CAP because this runs unprompted. Raise
//! it to match the manual command if legacy pages routinely need more.
static const double SUNEER_LEGACY_OVERFLOW_CAP = 0.05;

bool ScribusMainWindow::suneerGrowthWouldCollide(PageItem_TextFrame* tf, double newHeight) const
{
	if (!tf || !doc)
		return false;

	// Neighbours whose composition the grow could disturb, with the overflow
	// state they start from. A frame that is already overset is not our doing.
	QList<PageItem_TextFrame*> neighbours;
	QList<bool> wasOverflowing;
	for (PageItem* other : std::as_const(doc->DocItems))
	{
		if (other == tf || other->OwnPage != tf->OwnPage)
			continue;
		PageItem_TextFrame* ntf = other->asTextFrame();
		if (!ntf)
			continue;
		neighbours.append(ntf);
		wasOverflowing.append(ntf->frameOverflows());
	}

	// Probed on the real geometry so rotation, clip and text flow all count,
	// with undo suspended so the probe leaves nothing on the stack.
	const bool undoWasOn = UndoManager::undoEnabled();
	if (undoWasOn)
		m_undoManager->setUndoEnabled(false);

	const double origHeight = tf->height();
	const QRectF before = tf->getVisualBoundingRect();
	tf->setHeight(newHeight);
	tf->updateClip();
	const QRectF after = tf->getVisualBoundingRect();

	// A grown frame pushes text flow around it, so a neighbour can lose room
	// and go overset without the two boxes ever newly touching. Recompose them
	// at the trial height and see. This is the damage the geometric test below
	// is a proxy for, so it is worth the extra layout passes: the run happens
	// once, on open.
	bool damages = false;
	for (int i = 0; i < neighbours.count(); ++i)
	{
		PageItem_TextFrame* ntf = neighbours.at(i);
		ntf->invalid = true;
		ntf->layout();
		if (ntf->frameOverflows() && !wasOverflowing.at(i))
			damages = true;
	}

	tf->setHeight(origHeight);
	tf->updateClip();
	for (PageItem_TextFrame* ntf : std::as_const(neighbours))
	{
		ntf->invalid = true;
		ntf->layout();
	}

	if (undoWasOn)
		m_undoManager->setUndoEnabled(true);

	if (damages)
		return true;

	for (const PageItem* other : std::as_const(doc->DocItems))
	{
		// Same page only; pasteboard items (OwnPage -1) compare against each
		// other. An overlap that already existed is the operator's layout, not
		// damage we caused, so only newly created ones count.
		if (other == tf || other->OwnPage != tf->OwnPage)
			continue;
		const QRectF r = other->getVisualBoundingRect();
		if (after.intersects(r) && !before.intersects(r))
			return true;
	}
	return false;
}

void ScribusMainWindow::suneerMaybeOfferLegacyOverflowFix(int loadedFormatID)
{
	if (!HaveDoc || !doc || !view || m_suneerLegacyOverflowAsked)
		return;
	// Native SLA format ids run newest to oldest (171 = 45 ... 12x = 50), so a
	// file written by an older Scribus sorts *above* the current id. Past the
	// native range is a foreign importer, which is not a legacy Scribus file.
	if (loadedFormatID <= FORMATID_CURRENTIMPORT || loadedFormatID > FORMATID_NATIVEIMPORTEND)
		return;
	PrefsContext* prefs = PrefsManager::instance().prefsFile->getContext("suneer_legacy_overflow");
	if (prefs && !prefs->getBool("ask_on_open", true))
		return;

	// DocItems rather than *doc->Items: it is unambiguous right after load and
	// it includes the pasteboard, where OwnPage is -1.
	QList<PageItem_TextFrame*> overflowing;
	for (PageItem* item : std::as_const(doc->DocItems))
	{
		if (suneerOverflowFrameIneligible(item))
			continue;
		PageItem_TextFrame* tf = item->asTextFrame();
		if (tf && tf->frameOverflows())
			overflowing.append(tf);
	}
	if (overflowing.isEmpty())
		return;

	ScMessageBox box(QMessageBox::Question, tr("Legacy Document"),
	                 tr("This file was created in an older Scribus version."),
	                 QMessageBox::NoButton, this);
	box.setInformativeText(tr("%n text frame(s) overflow. Fix them now?", "", overflowing.count()));
	QPushButton* fixButton = box.addButton(tr("Fix"), QMessageBox::AcceptRole);
	box.addButton(tr("Skip"), QMessageBox::RejectRole);
	box.setDefaultButton(fixButton);
	QCheckBox* dontAsk = new QCheckBox(tr("Don't ask again this session"), &box);
	box.setCheckBox(dontAsk);
	box.exec();
	if (dontAsk->isChecked())
		m_suneerLegacyOverflowAsked = true;
	if (box.clickedButton() != fixButton)
		return;

	QList<PageItem*> tooBig;
	QList<PageItem*> wouldCollide;
	int fixed = 0;

	UndoTransaction batch;
	if (UndoManager::undoEnabled())
		batch = m_undoManager->beginTransaction(Um::Selection, Um::ITextFrame, Um::Resize,
		                                        tr("Fix legacy overflow"), Um::IResize);

	for (PageItem_TextFrame* tf : std::as_const(overflowing))
	{
		const double origHeight = tf->height();
		const double needed = suneerMeasureFitHeight(tf);
		if (needed <= 0.0 || needed > origHeight * (1.0 + SUNEER_LEGACY_OVERFLOW_CAP))
		{
			tooBig.append(tf);
			continue;
		}
		if (suneerGrowthWouldCollide(tf, needed))
		{
			wouldCollide.append(tf);
			continue;
		}
		// Same call the manual command and Ctrl+Alt+C use, so the result is
		// identical to fixing the frame by hand. It nests inside the batch.
		tf->autoFitFrameHeight();
		++fixed;
	}

	if (batch)
		batch.commit();

	if (fixed > 0)
	{
		// Mark dirty and redraw only. Never save on the operator's behalf: this
		// ran on open, and the file on disk is still their original.
		doc->changed();
		doc->regionsChanged()->update(QRectF());
		view->DrawNew();
	}

	const int capPercent = qRound(SUNEER_LEGACY_OVERFLOW_CAP * 100.0);
	QString summary = tr("Fixed %1 overflowing frame(s)").arg(fixed);
	const int skipped = tooBig.count() + wouldCollide.count();
	if (skipped > 0)
		summary += tr("; %1 left alone").arg(skipped);
	setStatusBarInfoText(summary);

	if (skipped == 0)
		return;

	// Anything we refused is an editorial call, so name it and offer to select
	// it rather than leaving the operator to hunt for it.
	QStringList details;
	if (!tooBig.isEmpty())
	{
		QStringList names;
		for (const PageItem* item : std::as_const(tooBig))
			names << item->itemName();
		details << tr("Would grow more than %1%: %2").arg(capPercent).arg(names.join(", "));
	}
	if (!wouldCollide.isEmpty())
	{
		QStringList names;
		for (const PageItem* item : std::as_const(wouldCollide))
			names << item->itemName();
		details << tr("Would overlap or reflow another object: %1").arg(names.join(", "));
	}

	ScMessageBox info(QMessageBox::Information, tr("Legacy Document"), summary, QMessageBox::NoButton, this);
	info.setInformativeText(details.join("\n\n"));
	QPushButton* selectButton = info.addButton(tr("Select Them"), QMessageBox::AcceptRole);
	info.addButton(QMessageBox::Close);
	info.exec();
	if (info.clickedButton() != selectButton)
		return;

	view->deselectItems(true);
	doc->m_Selection->delaySignalsOn();
	for (PageItem* item : std::as_const(tooBig))
		doc->m_Selection->addItem(item);
	for (PageItem* item : std::as_const(wouldCollide))
		doc->m_Selection->addItem(item);
	doc->m_Selection->delaySignalsOff();
	view->DrawNew();
}

void ScribusMainWindow::suneerFixOverflowFrames()
{
	suneerFixOverflowFramesRun(false);
}

void ScribusMainWindow::suneerFixOverflowFramesDoc()
{
	suneerFixOverflowFramesRun(true);
}

void ScribusMainWindow::suneerOpenImposition()
{
	if (!HaveDoc || !doc)
		return;
	if (doc->DocPages.count() < 1)
		return;
	ImpositionDialog dia(doc, this);
	dia.exec();
}

void ScribusMainWindow::suneerFitImageToFrame(PageItem* item)
{
	if (!item || !item->isImageFrame()) return;
	if (item->pixm.width() <= 0 || item->pixm.height() <= 0) return;
	double scaleX = item->width()  / item->pixm.width();
	double scaleY = item->height() / item->pixm.height();
	item->setImageXYScale(scaleX, scaleY);
	item->setImageXYOffset(0, 0);
	item->updateClip();
	item->update();
}

void ScribusMainWindow::editSymbolStart(const QString& temp)
{
	if (!HaveDoc || !doc->docPatterns.contains(temp))
		return;
	if (doc->symbolEditMode())
	{
		QString editedSymbol = doc->getEditedSymbol();
		if (editedSymbol == temp)
			return;
		editSymbolEnd();
	}
	m_WasAutoSave = doc->autoSave();
	if (m_WasAutoSave)
	{
		doc->autoSaveTimer->stop();
		doc->setAutoSave(false);
	}
	view->deselectItems(true);
	view->saveViewState();
	view->showSymbolPage(temp);
	appModeHelper->setSymbolEditMode(true, doc);
	pagePalette->enablePalette(false);
	layerPalette->setEnabled(false);
	patternsDependingOnThis.clear();
	QStringList mainPatterns = doc->docPatterns.keys();
	for (int a = 0; a < mainPatterns.count(); a++)
	{
		if (mainPatterns[a] != temp)
		{
			QStringList subPatterns;
			subPatterns = doc->getUsedPatternsHelper(mainPatterns[a], subPatterns);
			if (subPatterns.contains(temp))
				patternsDependingOnThis.prepend(mainPatterns[a]);
		}
	}
	patternsDependingOnThis.prepend(temp);
	symbolPalette->editingStart(patternsDependingOnThis);
//	propertiesPalette->colorPalette->hideEditedPatterns(patternsDependingOnThis);
	WidgetManager::instance().colorPicker()->hideEditedPatterns(patternsDependingOnThis);
//	propertiesPalette->transparencyPalette->hideEditedPatterns(patternsDependingOnThis);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree(false);
	updateActiveWindowCaption( tr("Editing Symbol: %1").arg(temp));
}

void ScribusMainWindow::editSymbolEnd()
{
	view->hideSymbolPage();
	if (m_WasAutoSave)
	{
		doc->setAutoSave(true);
		doc->restartAutoSaveTimer();
	}
	slotSelect();
	appModeHelper->setSymbolEditMode(false, doc);

	if ( ScCore->haveGS() || ScCore->isWinGUI() )
		scrActions["PrintPreview"]->setEnabled(true);
	if ( ScCore->haveGS() )
		scrActions["OutputPreviewPDF"]->setEnabled(true);
	pagePalette->enablePalette(true);
	pagePalette->rebuildMasters();
	view->restoreViewState();
	view->DrawNew();
	pagePalette->rebuild();
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	symbolPalette->editingFinished();
	layerPalette->setEnabled(true);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree(false);
	updateActiveWindowCaption(doc->documentFileName());
}

void ScribusMainWindow::editInlineStart(int id)
{
	if (!HaveDoc)
		return;
	m_WasAutoSave = doc->autoSave();
	if (m_WasAutoSave)
	{
		doc->autoSaveTimer->stop();
		doc->setAutoSave(false);
	}
	view->deselectItems(true);
	view->saveViewState();
	view->showInlinePage(id);
	appModeHelper->setInlineEditMode(true, doc);
	pagePalette->enablePalette(false);
	layerPalette->setEnabled(false);
	inlinePalette->editingStart(id);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree(false);
	updateActiveWindowCaption( tr("Editing Inline Item"));
}

void ScribusMainWindow::editInlineEnd()
{
	view->hideInlinePage();
	if (m_WasAutoSave)
	{
		doc->setAutoSave(true);
		doc->restartAutoSaveTimer();
	}
	slotSelect();
	appModeHelper->setInlineEditMode(false, doc);
	pagePalette->enablePalette(true);
	pagePalette->rebuildMasters();
	view->restoreViewState();
	doc->invalidateAll();
	view->DrawNew();
	pagePalette->rebuild();
	propertiesPalette->unsetItem();
	propertiesPalette->updateColorList();
	contentPalette->unsetItem();
	contentPalette->updateColorList();
	inlinePalette->editingFinished();
	layerPalette->setEnabled(true);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree(false);
	updateActiveWindowCaption(doc->documentFileName());
}

void ScribusMainWindow::editMasterPagesStart(const QString& temp)
{
	if (!HaveDoc)
		return;
	m_pagePaletteWasClosed = pagePalette->isClosed();
	QString mpName;
	if (temp.isEmpty())
		mpName = doc->currentPage()->masterPageName();
	else
		mpName = temp;
	view->deselectItems(true);
	if (doc->drawAsPreview)
	{
		view->togglePreview(false);
		scrActions["viewPreviewMode"]->setChecked(false);
	}
	if (doc->whiteSpaceModeEnabled)
	{
		view->toggleWhiteSpaceMode(false);
		scrActions["viewToggleWhiteSpaceMode"]->setChecked(false);
	}
	m_WasAutoSave = doc->autoSave();
	if (m_WasAutoSave)
	{
		doc->autoSaveTimer->stop();
		doc->setAutoSave(false);
	}

	if (doc->masterPageMode())
	{
		pagePalette->startMasterPageMode(mpName);
		return;
	}

	view->saveViewState();

	pagePalette->startMasterPageMode(mpName);
	if (pagePalette->isClosed())
	{
		auto* area = pagePalette->dockAreaWidget();
		auto* dockWidget = area->currentDockWidget();
		scrActions["toolsPages"]->setChecked(true);
		area->setCurrentDockWidget(dockWidget);
	}
	appModeHelper->setMasterPageEditMode(true, doc);
}

void ScribusMainWindow::editMasterPagesEnd()
{
	view->hideMasterPage();
	if (m_WasAutoSave)
	{
		doc->setAutoSave(true);
		doc->restartAutoSaveTimer();
	}
	slotSelect();
	appModeHelper->setMasterPageEditMode(false, doc);
	int pageCount = doc->DocPages.count();
	for (int i = 0; i < pageCount; ++i)
		Apply_MasterPage(doc->DocPages.at(i)->masterPageName(), i, false);

	pagePalette->endMasterPageMode();
	if (m_pagePaletteWasClosed && pagePalette->isFloating())
	{
		scrActions["toolsPages"]->setChecked(false);
	}

	ScribusView::ViewState viewState = view->topViewState();
	doc->setLoading(true);
	view->restoreViewState();
	view->reformPages(false);
	view->setContentsPos(viewState.contentX, viewState.contentY);
	doc->setLoading(false);
	view->DrawNew();
}

void ScribusMainWindow::ApplyMasterPage()
{
	Q_ASSERT(!doc->masterPageMode());

	QScopedPointer<ApplyMasterPageDialog> dia(new ApplyMasterPageDialog(this));
	dia->setup(doc, doc->currentPage()->masterPageName());
	if (!dia->exec())
		return;

	QString masterPageName(dia->getMasterPageName());
	int pageSelection = dia->getPageSelection(); //0=current, 1=even, 2=odd, 3=all

	int startPage, endPage;
	if (pageSelection == 0)
	{
		startPage = doc->currentPage()->pageNr();
		endPage = doc->currentPage()->pageNr() + 1;
	}
	else if (dia->usingRange())
	{
		startPage = dia->getFromPage() - 1; //Pages start from 0, not 1
		endPage = dia->getToPage();
	}
	else
	{
		startPage = pageSelection == 1 ? 1 : 0; //if even, startPage is 1 (real page 2)
		endPage = doc->DocPages.count();
	}

	UndoTransaction trans;
	if (UndoManager::undoEnabled() && (startPage + 1 < endPage))
		trans = m_undoManager->beginTransaction(QString(), nullptr, Um::ApplyMasterPage, masterPageName);

	for (int pageNum = startPage; pageNum < endPage; ++pageNum)
	{
		//Increment by 1 and not 2 even for even/odd application as user
		//can select to eg apply to even pages with a single odd page selected
		if (pageSelection == 1 && (pageNum % 2 == 0)) //Even, %2!=0 as 1st page is numbered 0
			continue;
		if (pageSelection == 2 && (pageNum % 2 != 0))  //Odd, %2==0 as 1st page is numbered 0
			continue;
		Apply_MasterPage(masterPageName, pageNum, false);
	}

	if (trans)
		trans.commit();

	view->reformPages();
	view->DrawNew();
	pagePalette->rebuild();
	// #9476 : call setupPage with false arg to setup only guidePalette GUI
	// Otherwise setupPage() will apply guides to current page, doesn't need that, 
	// Apply_MasterPage() has already done it
	guidePalette->setupPage(false);
}

void ScribusMainWindow::Apply_MasterPage(const QString& pageName, int pageNumber, bool reb)
{
	if (!HaveDoc)
		return;
	doc->applyMasterPage(pageName, pageNumber);
	if (reb)
		view->DrawNew();
}

//CB-->Doc
void ScribusMainWindow::GroupObj(bool showLockDia)
{
	if (!HaveDoc)
		return;
	Selection* itemSelection = doc->m_Selection;
	if (itemSelection->count() < 2)
		return;
	bool lockObject = false;
	bool modifyLock = false;
	int selectedItemCount = itemSelection->count();
	if (showLockDia)
	{
		int lockedCount=0;
		for (int i = 0; i < selectedItemCount; ++i)
		{
			if (itemSelection->itemAt(i)->locked())
				++lockedCount;
		}
		if (lockedCount != 0 && lockedCount != selectedItemCount)
		{
			ScMessageBox msgBox;
			QPushButton *abortButton = msgBox.addButton(QMessageBox::Cancel);
			QPushButton *lockButton = msgBox.addButton(tr("&Lock All"), QMessageBox::AcceptRole);
			msgBox.addButton(tr("&Unlock All"), QMessageBox::AcceptRole);
			msgBox.setIcon(QMessageBox::Warning);
			msgBox.setWindowTitle(CommonStrings::trWarning);
			msgBox.setText( tr("Some objects are locked."));
			msgBox.setDefaultBatchButton(lockButton);
			msgBox.exec();
			if (msgBox.clickedButton() == abortButton)
				return;
			if (msgBox.clickedButton() == lockButton)
				lockObject = true;
			modifyLock = true;
		}
	}
	doc->itemSelection_GroupObjects(modifyLock, lockObject);
}

//CB-->Doc
void ScribusMainWindow::UnGroupObj()
{
	if (HaveDoc)
		doc->itemSelection_UnGroupObjects();
}

void ScribusMainWindow::AdjustGroupObj()
{
	if (HaveDoc)
		doc->itemSelection_resizeGroupToContents();
}

void ScribusMainWindow::restore(UndoState* state, bool isUndo)
{
	auto *ss = dynamic_cast<SimpleState*>(state);
	if (ss)
	{
		if (ss->contains("ADD_PAGE"))
			restoreAddPage(ss, isUndo);
		else if (ss->contains("DELETE_PAGE"))
			restoreDeletePage(ss, isUndo);
	}
}

void ScribusMainWindow::restoreDeletePage(SimpleState *state, bool isUndo)
{
	int where, wo;
	int pagenr = state->getUInt("PAGENR");
	QStringList tmpl;
	tmpl << state->get("MASTERPAGE");
	QString pageName = state->get("PAGENAME");
	bool savedMasterPageMode = state->getBool("MASTER_PAGE_MODE");
	bool currMasterPageMode = doc->masterPageMode();
	if (currMasterPageMode != savedMasterPageMode)
		doc->setMasterPageMode(savedMasterPageMode);
	if (pagenr == 1)
	{
		where = 0;
		wo = 1;
	}
	else if (pagenr > doc->Pages->count())
	{
		where = 2;
		wo = doc->Pages->count();
	}
	else
	{
		where = 1;
		wo = pagenr - 1;
	}

	if (isUndo)
	{
		if (savedMasterPageMode)
		{
			slotNewMasterPage(wo, pageName);
		}
		else
		{
			addNewPages(wo, where, 1, doc->pageHeight(), doc->pageWidth(), doc->pageOrientation(), doc->pageSize(), true, &tmpl);
		}
		UndoObject *tmp =
			m_undoManager->replaceObject(state->getUInt("DUMMY_ID"), doc->Pages->at(pagenr - 1));
		delete tmp;
	}
	else
	{
		auto *duo = new DummyUndoObject();
		uint id = static_cast<uint>(duo->getUId());
		m_undoManager->replaceObject(doc->Pages->at(pagenr - 1)->getUId(), duo);
		state->set("DUMMY_ID", id);
		deletePage(pagenr, pagenr);
	}
	if (currMasterPageMode != savedMasterPageMode)
		doc->setMasterPageMode(currMasterPageMode);
	doc->rebuildMasterNames();
	pagePalette->updateMasterPageList();
	pagePalette->rebuildPages();
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
}

void ScribusMainWindow::restoreAddPage(SimpleState *state, bool isUndo)
{
	if (!HaveDoc)
		return;
	int wo    = state->getInt("PAGE");
	int where = state->getInt("WHERE");
	int count = state->getInt("COUNT");
	QStringList based = state->get("BASED").split("|", Qt::SkipEmptyParts);
	double height = state->getDouble("HEIGHT");
	double width = state->getDouble("WIDTH");
	int orient = state->getInt("ORIENT");
	QString siz = state->get("SIZE");
	bool mov = static_cast<bool>(state->getInt("MOVED"));
	bool savedMasterPageMode = state->getBool("MASTER_PAGE_MODE");

	int delFrom = 0;
	int delTo = 0;
	bool currMasterPageMode = doc->masterPageMode();
	if (currMasterPageMode != savedMasterPageMode)
		doc->setMasterPageMode(savedMasterPageMode);
	switch (where)
	{
		case 0:
			delTo = wo + count - 1;
			delFrom = delTo - count + 1;
			break;
		case 1:
			delFrom = wo + 1;
			delTo = wo + count;
			break;
		case 2:
			delTo = doc->Pages->count();
			delFrom = doc->Pages->count() - count + 1;
			if (!isUndo)
			{
				delFrom += count;
				delTo   += count;
			}
			break;
	}

	if (isUndo)
	{
		for (int i = delFrom - 1; i < delTo; ++i)
		{
			auto *duo = new DummyUndoObject();
			ulong did = duo->getUId();
			m_undoManager->replaceObject(doc->Pages->at(i)->getUId(), duo);
			state->set(QString("Page%1").arg(i), static_cast<uint>(did));
		}
		if (doc->appMode == modeEditClip)
			view->requestMode(submodeEndNodeEdit);
		view->deselectItems(true);
		deletePage(delFrom, delTo);
	}
	else
	{
		if (savedMasterPageMode)
		{
			assert (count == 1);
			slotNewMasterPage(wo, based[0]);
		}
		else
		{
			addNewPages(wo, where, count, height, width, orient, siz, mov, &based);
		}
		for (int i = delFrom - 1; i < delTo; ++i)
		{
			UndoObject *tmp = m_undoManager->replaceObject(state->getUInt(QString("Page%1").arg(i)), doc->Pages->at(i));
			delete tmp;
		}
	}
	if (currMasterPageMode != savedMasterPageMode)
		doc->setMasterPageMode(currMasterPageMode);
	doc->rebuildMasterNames();
	pagePalette->updateMasterPageList();
	pagePalette->rebuildPages();
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
}

void ScribusMainWindow::restoreGrouping(SimpleState *state, bool isUndo)
{
	int itemCount = state->getInt("itemcount");
	view->deselectItems();
	for (int i = 0; i < itemCount; ++i)
	{
		int itemNr = doc->getItemNrFromUniqueID(state->getUInt(QString("item%1").arg(i)));
		if (doc->Items->at(itemNr)->uniqueNr == state->getUInt(QString("item%1").arg(i)))
			view->selectItemByNumber(itemNr);
	}
	if (isUndo)
		UnGroupObj();
	else
		GroupObj(false);
}

void ScribusMainWindow::restoreUngrouping(SimpleState *state, bool isUndo)
{
	int itemCount = state->getInt("itemcount");
	view->deselectItems();
	for (int i = 0; i < itemCount; ++i)
	{
		int itemNr = doc->getItemNrFromUniqueID(state->getUInt(QString("item%1").arg(i)));
		if (doc->Items->at(itemNr)->uniqueNr == state->getUInt(QString("item%1").arg(i)))
			view->selectItemByNumber(itemNr);
	}
	if (isUndo)
		GroupObj(false);
	else
		UnGroupObj();
}

void ScribusMainWindow::StatusPic()
{
	if (!HaveDoc)
		return;
	PicStatus *dia = new PicStatus(this, doc);
	connect(dia, SIGNAL(selectPage(int)), this, SLOT(selectPagesFromOutlines(int)));
	connect(dia, SIGNAL(selectMasterPage(QString)), this, SLOT(editMasterPagesStart(QString)));
	connect(dia, SIGNAL(selectElementByItem(PageItem*,bool,int)), this, SLOT(selectItemsFromOutlines(PageItem*,bool,int)));
	dia->exec();
	delete dia;
}

QPair<QString, uint> ScribusMainWindow::CFileDialog(const QString& workingDirectory, const QString& dialogCaption, const QString& fileFilter, const QString& defaultFilename, int optionFlags, bool *useCompression, bool *useFonts, bool *useProfiles)
{
	// changed from "this" to qApp->activeWindow() to be sure it will be opened
	// with the current active window as parent. E.g. it won't hide StoryEditor etc. -- PV
	CustomFDialog *dia = new CustomFDialog(QApplication::activeWindow(), workingDirectory, dialogCaption, fileFilter, optionFlags);
	if (!defaultFilename.isEmpty())
	{
		QString tmpFileName = defaultFilename;
		if (tmpFileName.endsWith(".gz", Qt::CaseInsensitive))
			tmpFileName.chop(3);
		QFileInfo f(tmpFileName);
		dia->setExtension(f.suffix());
		dia->setZipExtension(f.suffix() + ".gz");
		dia->setSelection(defaultFilename);
		if (useCompression != nullptr)
			dia->setSaveZipFile(*useCompression);
	}
	if (optionFlags & fdDirectoriesOnly)
	{
		if (useCompression != nullptr)
			dia->setSaveZipFile(*useCompression);
		if (useFonts != nullptr)
			dia->setIncludeFonts(*useFonts);
		if (useProfiles != nullptr)
			dia->setIncludeProfiles(*useProfiles);
	}

	QPair<QString, uint> fileNameVersion;
	fileNameVersion.second = FORMATID_CURRENTEXPORT;
	if (dia->exec() == QDialog::Accepted)
	{
		if (!(optionFlags & fdDirectoriesOnly))
		{
			if (optionFlags & fdCompressFile)
			{
				if (dia->saveZipFile())
					dia->handleCompress();
			}
		}
		else
		{
			if (useCompression != nullptr && dia->isSaveZipFileShown())
				*useCompression = dia->saveZipFile();
			if (useFonts != nullptr)
				*useFonts = dia->includeFonts();
			if (useProfiles != nullptr)
				*useProfiles = dia->includeProfiles();
		}
		this->repaint();
		fileNameVersion.first = dia->selectedFile();
		//Qt 6.8 changes what selectedNameFilter returns.. now includes the file extension list. Remove it.
		QString formatName(dia->selectedNameFilter());
		int location = formatName.indexOf("(");
		if (location > 0)
			formatName.truncate(location);
		formatName = formatName.trimmed();
		fileNameVersion.second=FileLoader::findFormatIDFromDescription(formatName);
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	delete dia;
	return fileNameVersion;
}



void ScribusMainWindow::recalcColors()
{
	if (!HaveDoc)
		return;
	doc->recalculateColors();
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
}

void ScribusMainWindow::ModifyAnnot()
{
	if (doc->m_Selection->isEmpty())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	if ((currItem->annotation().Type() == 0) || (currItem->annotation().Type() == 1) || ((currItem->annotation().Type() > 9) && (currItem->annotation().Type() < 13)))
	{
		int AnType = currItem->annotation().Type();
		int AnActType = currItem->annotation().ActionType();
		QString AnAction = currItem->annotation().Action();
		QString An_Extern = currItem->annotation().Extern();
		Annota *dia = new Annota(this, currItem, doc, view);
		if (dia->exec())
			slotDocCh();
		else
		{
			currItem->annotation().setType(AnType);
			currItem->annotation().setActionType(AnActType);
			currItem->annotation().setAction(AnAction);
			currItem->annotation().setExtern(An_Extern);
		}
		delete dia;
	}
	else
	{
		ScAnnot *dia = new ScAnnot(this, currItem, doc, view);
		if (dia->exec())
			slotDocCh();
		delete dia;
	}
	currItem->update();
}

void ScribusMainWindow::applyKeySetFromFile(const QString& path)
{
	QMap<QString, Keys>& keyActionsRef = m_prefsManager.appPrefs.keyShortcutPrefs.KeyActions;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QDomDocument docXml("keymapentries");
	if (docXml.setContent(&f))
	{
		QDomElement root = docXml.documentElement();
		if (root.tagName() == "shortcutset")
		{
			for (QDomNode n = root.firstChild(); !n.isNull(); n = n.nextSibling())
			{
				QDomElement e = n.toElement();
				if (e.hasAttribute("name") && e.hasAttribute("shortcut"))
				{
					QString name = e.attribute("name");
					if (keyActionsRef.contains(name))
						keyActionsRef[name].keySequence = QKeySequence(e.attribute("shortcut"));
				}
			}
		}
	}
	f.close();
	// Push the (possibly changed) shortcuts onto the live actions.
	for (auto it = keyActionsRef.begin(); it != keyActionsRef.end(); ++it)
	{
		if (!it.value().actionName.isEmpty() && scrActions[it.value().actionName])
			scrActions[it.value().actionName]->setShortcut(it.value().keySequence);
	}
	// Keep the styled clipboard actions as the sole owners of Ctrl+Shift+C/V.
	enforceClipboardShortcuts();
}

void ScribusMainWindow::checkMalayalamDtpFirstRun()
{
	Prefs_KeyboardShortcuts::migrateOldMyDefault();

	QSettings settings;
	if (settings.contains(QStringLiteral("malayalamDtpFirstRun")))
		return;   // already prompted — never overwrite an established config

	// QSettings is keyed on the process name (scribus.conf vs scribus.bin.conf),
	// so the flag above can be missing for a profile that already has saved
	// shortcuts. The profile itself is the reliable signal: if scribus172.rc
	// exists, this is not a first run and the keyset must not touch it.
	if (QFile::exists(m_prefsManager.preferencesLocation() + "scribus172.rc"))
	{
		settings.setValue(QStringLiteral("malayalamDtpFirstRun"), true);
		return;
	}

	// A fresh profile starts from the installer's site-wide set, when there is
	// one, without asking.
	const QString siteSet = ScPaths::instance().shareDir() + "default-shortcuts.xml";
	if (QFile::exists(siteSet))
	{
		applyKeySetFromFile(siteSet);
		settings.setValue(QStringLiteral("malayalamDtpFirstRun"), true);
		return;
	}

	// A fresh profile on an office PC: the shipped set ("dbi") is the Default,
	// no question asked. It can be changed any time in Preferences >
	// Keyboard Shortcuts, where "Scribus Default" is still offered.
	applyKeySetFromFile(ScPaths::instance().shareDir() + "keysets/malayalam-dtp.xml");
	Prefs_KeyboardShortcuts::setDefaultSetName(Prefs_KeyboardShortcuts::newspaperDefaultSetName());
	settings.setValue(QStringLiteral("malayalamDtpFirstRun"), true);
}

void ScribusMainWindow::PutScrap(int scID)
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->isEmpty())
		return;

	QString objectString = ScriXmlDoc::writeElem(doc, doc->m_Selection);
	QDomDocument docu("scridoc");
	docu.setContent(objectString);
	QDomElement elem = docu.documentElement();
	QDomNode domNode = elem.firstChild();
	bool first = true;
	domNode = elem.firstChild();
	while (!domNode.isNull())
	{
		QDomElement pg = domNode.toElement();
		if ((pg.tagName() == "ITEM") || (pg.tagName() == "Item"))
		{
			if (first)
				pg.setAttribute("ANNAME", doc->m_Selection->itemAt(0)->itemName());
			first = false;
		}
		domNode = domNode.nextSibling();
	}
	objectString = docu.toString();
	scrapbookPalette->objFromMainMenu(objectString, scID);
 }

void ScribusMainWindow::changeLayer(int )
{
	if (doc->appMode == modeEdit)
		slotSelect();
	else if (doc->appMode == modeEditClip)
		NoFrameEdit();
	view->deselectItems(true);
	rebuildLayersList();
	layerPalette->rebuildList();
	layerPalette->markActiveLayer();
	updateLayerMenu();
	view->DrawNew();
	bool setter = !doc->layerLocked( doc->activeLayer() );
	scrMenuMgr->setMenuEnabled("EditPasteRecent", ((scrapbookPalette->tempBView->objectMap.count() > 0) && setter));
	scrMenuMgr->setMenuEnabled("Insert", setter);
	scrMenuMgr->setMenuEnabled("ItemLayer", doc->layerCount() > 1);
	appModeHelper->changeLayer(doc, (ScMimeData::clipboardHasScribusData() || (scrapbookPalette->tempHasContents())));
}

void ScribusMainWindow::setLayerMenuText(const QString &layerName)
{
	const QSignalBlocker blocker(layerMenu);
	if (layerMenu->count() != 0)
		setCurrentComboItem(layerMenu, layerName);
}

void ScribusMainWindow::showLayer()
{
	view->DrawNew();
}

//TODO: use this only from this class, or just from doc->setcurrentpage
void ScribusMainWindow::slotSetCurrentPage(int pageIndex)
{
	if (scriptIsRunning())
		return;
	const QSignalBlocker blocker(pageSelector);
	pageSelector->setMaximum(doc->masterPageMode() ? 1 : doc->Pages->count());
	if ((!doc->isLoading()) && (!doc->masterPageMode()))
		pageSelector->setGUIForPage(pageIndex);
}

void ScribusMainWindow::setCurrentPage(int p)
{
	doc->view()->deselectItems();
	int p0 = p - 1; //p is what the user sees.. p0 is our count from 0
	doc->setCurrentPage(doc->Pages->at(p0));
	if (scriptIsRunning())
		return;
	slotSetCurrentPage(p0);
	doc->view()->setCanvasPos(doc->currentPage()->xOffset() - 10, doc->currentPage()->yOffset() - 10);
	HaveNewSel();
	doc->view()->setFocus();
}

void ScribusMainWindow::ImageEffects()
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	// EffectsDialog reads the item's image data straight away, so it is only meaningful for
	// an image frame that actually has a picture. Reachable with anything selected when the
	// shortcut is ambiguous, so fail closed rather than trusting the caller.
	if (!currItem || !currItem->isImageFrame() || !currItem->imageIsAvailable)
		return;
	EffectsDialog* dia = new EffectsDialog(this, currItem, doc);
	if (dia->exec())
		doc->itemSelection_ApplyImageEffects(dia->effectsList);
	delete dia;
}

QString ScribusMainWindow::fileCollect(bool compress, bool withFonts, const bool withProfiles, const QString& )
{
	if ((doc->hasName) && doc->documentFileName().endsWith(".gz"))
		compress = true;
	CollectForOutput_UI c(this, doc, QString(), withFonts, withProfiles, compress);
	QString newFileName;
	QString errorMsg = c.collect(newFileName);
	qDebug() << errorMsg;
	return newFileName;
}

void ScribusMainWindow::docCheckToggle(bool visible)
{
	if (!visible)
	{
		if (docCheckerPalette->checkMode == CheckDocument::checkPDF)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doSaveAsPDF()));
		if (docCheckerPalette->checkMode == CheckDocument::checkEPS)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(reallySaveAsEps()));
		if (docCheckerPalette->checkMode == CheckDocument::checkPrint)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(slotReallyPrint()));
		if (docCheckerPalette->checkMode == CheckDocument::checkPrintPreview)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doPrintPreview()));
		if (docCheckerPalette->checkMode == CheckDocument::checkOutputPreviewPDF)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPDF()));
		if (docCheckerPalette->checkMode == CheckDocument::checkOutputPreviewPS)
			disconnect(docCheckerPalette, SIGNAL(ignoreAllErrors()), this, SLOT(doOutputPreviewPS()));
		docCheckerPalette->setIgnoreEnabled(false);
		docCheckerPalette->checkMode = CheckDocument::checkNULL;
	}
	else
	{
		if (HaveDoc)
		{
			scanDocument();
			docCheckerPalette->buildErrorList(doc);
		}
	}
}

bool ScribusMainWindow::scanDocument()
{
	return DocumentChecker::checkDocument(doc);
}

void ScribusMainWindow::slotStoryEditor(bool fromTable)
{
	if (doc->m_Selection->isEmpty())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	PageItem *i2 = currItem;
	if (fromTable)
		i2 = currItem->asTable()->activeCell().textFrame();
	PageItem *currItemSE = storyEditor->currentItem();
	ScribusDoc *currDocSE = storyEditor->currentDocument();
	storyEditor->activFromApp = true;
	//CB shouldn't these be after the if?
	//Why are we resetting the doc and item in this case. My original code didn't do this.
	storyEditor->setCurrentDocumentAndItem(doc, i2);
	if (i2 == currItemSE && doc == currDocSE)
	{
		storyEditor->show();
		storyEditor->raise();
		return;
	}
	CurrStED = storyEditor;
	connect(storyEditor, SIGNAL(DocChanged()), this, SLOT(slotDocCh()));
	storyEditor->show();
	storyEditor->raise();
}

void ScribusMainWindow::emergencySave()
{
	emergencyActivated = true;
	if (!m_prefsManager.appPrefs.miscPrefs.saveEmergencyFile)
		return;
	std::cout << "Calling Emergency Save" << std::endl;
	QList<QMdiSubWindow *> windows = mdiArea->subWindowList();
	if (windows.isEmpty())
		return;

	int windowCount = windows.count();
	for (int i = 0; i < windowCount; ++i)
	{
		ActWin = dynamic_cast<ScribusWin*>(windows.at(i)->widget());
		doc = ActWin->doc();
		view = ActWin->view();
		doc->autoSaveTimer->stop();
		doc->setMasterPageMode(false);
		doc->setModified(false);
		QString base = tr("Document");
		QString path = m_prefsManager.documentDir();
		if (doc->hasName)
		{
			QFileInfo fi(doc->documentFileName());
			base = fi.baseName();
			path = fi.absolutePath();
		}
		QDateTime dat = QDateTime::currentDateTime();
		if ((!doc->prefsData().docSetupPrefs.AutoSaveLocation) && (!doc->prefsData().docSetupPrefs.AutoSaveDir.isEmpty()))
			path = doc->prefsData().docSetupPrefs.AutoSaveDir;
		// A crash must not end in a hang: if the folder is on a share that
		// is not answering, the emergency copy goes to the home folder,
		// which findRecoverableFile() also searches.
		if (!NetPathGuard::reachable(path))
			path = QDir::homePath();
		QString fileName = QDir::cleanPath(path + "/" + base + QString("_emergency_%1.sla").arg(dat.toString("dd_MM_yyyy_hh_mm")));
		std::cout << "Saving: " << fileName.toStdString() << std::endl;
		FileLoader fl(fileName);
		fl.saveFile(fileName, doc, nullptr);
		// ActWin->close() will trigger ScribusWin::closeEvent()
		// so no need to manually close view or delete doc
		ActWin->getSubWin()->close();
	}
}

void ScribusMainWindow::EditTabs()
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;

	PageItem *currItem = doc->m_Selection->itemAt(0);
	TabManager *dia = new TabManager(this, doc->unitIndex(), currItem->itemText.defaultStyle().tabValues(), currItem->width());
	if (dia->exec())
	{
		ParagraphStyle newTabs(currItem->itemText.defaultStyle());
		newTabs.setTabValues(dia->tabList());
		currItem->itemText.setDefaultStyle(newTabs);
		currItem->update();
		slotDocCh();
	}
	delete dia;
}

void ScribusMainWindow::SearchText()
{
	TextFrameSpellChecker::instance()->pauseChecking();
	SearchReplace dia(this, doc);
	if (doc->appMode == modeEdit)
	{
		PageItem *currItem = doc->m_Selection->itemAt(0);
		dia.processCurrentSelection(currItem->itemText.selectedText());
	}
	dia.exec();
	TextFrameSpellChecker::instance()->resumeChecking();
}

/* call gimp and wait upon completion */
void ScribusMainWindow::slotOpenScImageEditor()
{
	if (!HaveDoc || doc->m_Selection->isEmpty())
		return;
	PageItem* currItem = doc->m_Selection->itemAt(0);
	if (!currItem || !currItem->isImageFrame())
		return;
	PageItem_ImageFrame* imgFrame = currItem->asImageFrame();
	if (!imgFrame || imgFrame->Pfile.isEmpty())
		return;
	ScImageEditor::openForFrame(imgFrame, this);
}

void ScribusMainWindow::callImageEditor()
{
	if (!HaveDoc || !doc || doc->m_Selection->isEmpty())
		return;

	//NOTE to reviewers: I added my code to this function,
	// - as it performs a similar function,
	// - when the frame is a latex frame it makes only sense
	//   to run a latex editor
	// - IMHO ScribusMainWindow has way to many slots already
	// - my code here is short and without sideeffects
	PageItem *currItem = doc->m_Selection->itemAt(0);
	if (currItem->isLatexFrame())
	{
		currItem->asLatexFrame()->runEditor();
		return; //Don't process the functions for imageframes!
	}
#ifdef HAVE_OSG
	if (currItem->isOSGFrame())
	{
		OSGEditorDialog *dia = new OSGEditorDialog(this, currItem->asOSGFrame(), m_osgFilterString);
		dia->exec();
		return;
	}
#endif
	QString imageEditorExecutable = m_prefsManager.imageEditorExecutable();
	if (currItem->imageIsAvailable)
	{
		bool startFailed = false;
	#ifdef Q_OS_MACOS
		QString osxcmd(imageEditorExecutable);
		if (osxcmd.endsWith(".app"))
			osxcmd.prepend("open -a \"");
		else
			osxcmd.prepend("\"");
		osxcmd.append("\" \"");
		osxcmd.append(QDir::toNativeSeparators(currItem->Pfile));
		osxcmd.append("\"");
		if (!QProcess::startDetached(osxcmd))
			startFailed = true;
	#else
		QStringList cmd(QDir::toNativeSeparators(currItem->Pfile));
		if (!QProcess::startDetached(QDir::fromNativeSeparators(imageEditorExecutable), cmd))
			startFailed = true;
	#endif
		if (startFailed)
			ScMessageBox::critical(this, CommonStrings::trWarning, "<qt>" + tr("The program %1 is missing or failed to open").arg(imageEditorExecutable) + "</qt>");
	}
}

void ScribusMainWindow::slotCharSelect()
{
	charPalette->setVisible(scrActions["insertGlyph"]->isChecked());
}

void ScribusMainWindow::setUndoMode(bool isObjectSpecific)
{
	m_objectSpecificUndo = isObjectSpecific;

	if (!m_objectSpecificUndo && HaveDoc)
		m_undoManager->showObject(Um::GLOBAL_UNDO_MODE);
	else if (HaveDoc)
	{
		int docSelectionCount = doc->m_Selection->count();
		if (docSelectionCount == 1)
			m_undoManager->showObject(doc->m_Selection->itemAt(0)->getUId());
		else if (docSelectionCount == 0)
			m_undoManager->showObject(doc->currentPage()->getUId());
		else
			m_undoManager->showObject(Um::NO_UNDO_STACK);
	}
}

bool ScribusMainWindow::isObjectSpecificUndo() const
{
	return m_objectSpecificUndo;
}

void ScribusMainWindow::getImageInfo()
{
	if ((!HaveDoc) || (doc->m_Selection->count() != 1))
		return;

	PageItem *pageItem = doc->m_Selection->itemAt(0);
	if (pageItem == nullptr)
		return;
	if (pageItem->itemType() == PageItem::ImageFrame)
	{
		ImageInfoDialog *dia = new ImageInfoDialog(this, &pageItem->pixm.imgInfo);
		dia->exec();
		delete dia;
	}
}

void ScribusMainWindow::objectAttributes()
{
	if ((!HaveDoc) || (doc->m_Selection->count() != 1))
		return;

	PageItem *pageItem = doc->m_Selection->itemAt(0);
	if (pageItem == nullptr)
		return;
	PageItemAttributes *pageItemAttrs = new PageItemAttributes( this );
	pageItemAttrs->setup(pageItem->getObjectAttributes(), &doc->itemAttributes());
	//CB TODO Probably want this non modal in the future
	if (pageItemAttrs->exec() == QDialog::Accepted)
	{
		pageItem->setObjectAttributes(pageItemAttrs->getNewAttributes());

		// If a plugin-editable item's attributes were edited, regenerate silently
		QString editAction = pageItem->getObjectAttribute("plugin-editAction").value;
		if (!editAction.isEmpty() && scrActions.contains(editAction))
		{
			pluginEditItem = pageItem;
			pluginEditSilent = true;
			scrActions[editAction]->trigger();
		}
	}
	delete pageItemAttrs;
}

void ScribusMainWindow::generateTableOfContents()
{
	if (!HaveDoc)
		return;

	m_tocGenerator->generateByAttribute();
	m_tocGenerator->generateByStyle();
	m_tocGenerator->generateIndex();
}

void ScribusMainWindow::updateDocument()
{
	if (!HaveDoc)
		return;
	doc->updateNumbers(true);
	doc->updateMarks(true);
	doc->regionsChanged()->update(QRect());
	emit UpdateRequest(reqNumUpdate);
}

void ScribusMainWindow::insertSampleText()
{
	if (!HaveDoc)
		return;
	LoremManager loremMgr(doc, this);
	if (m_prefsManager.appPrefs.miscPrefs.useStandardLI)
	{
		loremMgr.insertLoremIpsum("la.xml", m_prefsManager.appPrefs.miscPrefs.paragraphsLI);
		return;
	}

	if (loremMgr.exec())
		loremMgr.insertLoremIpsum(loremMgr.getName(), loremMgr.paragraphCount(), loremMgr.randomize());
}

void ScribusMainWindow::iconSetChange()
{
	IconManager& iconManager = IconManager::instance();

	setWindowIcon(iconManager.loadIcon("app-icon"));
	setStyleSheet();

	zoomDefaultToolbarButton->setIcon(iconManager.loadIcon("zoom-original"));
	zoomOutToolbarButton->setIcon(iconManager.loadIcon("zoom-out"));
	zoomInToolbarButton->setIcon(iconManager.loadIcon("zoom-in"));
}

void ScribusMainWindow::languageChange()
{
	if (!ScCore->initialized())
		return;

	//Update colours in case someone has a translated None colour in their preference settings
	//before changing the tr_NoneColor to the new value. See #9267, #5529
	m_prefsManager.languageChange();
	CommonStrings::languageChange();
	LanguageManager::instance()->languageChange();
	QApplication::setLayoutDirection(QLocale(ScCore->getGuiLanguage()).textDirection());
	//Update actions
	if (actionManager != nullptr)
	{
		actionManager->languageChange();
		ScCore->pluginManager->languageChange();
		initKeyboardShortcuts();
	}
	//Update menu texts
	if (scrMenuMgr != nullptr && !scrMenuMgr->empty())
		scrMenuMgr->languageChange();
	if (m_undoManager != nullptr)
		m_undoManager->languageChange();
	statusBarLanguageChange();
	viewToolBar->languageChange();
	documentLogViewer->languageChange();
}

void ScribusMainWindow::localeChange()
{
	const QLocale& l(LocaleManager::instance().userPreferredLocale());
	zoomSpinBox->setLocale(l);
	mainWindowXPosDataLabel->setText("         ");
	mainWindowYPosDataLabel->setText("         ");
}

void ScribusMainWindow::statusBarLanguageChange()
{
	zoomSpinBox->setToolTip( tr("Current zoom level"));
	zoomDefaultToolbarButton->setToolTip( tr("Zoom to 100%"));
	zoomOutToolbarButton->setToolTip( tr("Zoom out by the stepping value in Tools preferences"));
	zoomInToolbarButton->setToolTip( tr("Zoom in by the stepping value in Tools preferences"));
	layerMenu->setToolTip( tr("Select the current layer"));
	unitSwitcher->setToolTip( tr("Select the current unit"));
	mainWindowXPosLabel->setText( tr("X:"));
	mainWindowYPosLabel->setText( tr("Y:"));
	mainWindowXPosDataLabel->setText("         ");
	mainWindowYPosDataLabel->setText("         ");
	m_mainWindowStatusLabel->setText( tr("Ready"));
}

void ScribusMainWindow::setDefaultPrinter(const QString& name, const QString& file, const QString& command)
{
	PDef.Pname = name;
	PDef.Dname = file;
	PDef.Command = command;
}

void ScribusMainWindow::getDefaultPrinter(QString& name, QString& file, QString& command) const
{
	name = PDef.Pname;
	file = PDef.Dname;
	command = PDef.Command;
}

void ScribusMainWindow::closeActiveWindowMasterPageEditor()
{
	if (!HaveDoc)
		return;
	if (!doc->masterPageMode())
		return;
	editMasterPagesEnd();
	QApplication::processEvents();
}

void ScribusMainWindow::updateActiveWindowCaption(const QString &newCaption)
{
	if (!HaveDoc)
		return;
	ActWin->setWindowTitle(QDir::toNativeSeparators(newCaption));
}

void ScribusMainWindow::dragEnterEvent ( QDragEnterEvent* e)
{
	bool accepted = false;
	if ( e->mimeData()->hasFormat("text/uri-list"))
	{
		QString fileUrl;
		QList<QUrl> fileUrls = e->mimeData()->urls();
		for (int i = 0; i < fileUrls.count(); ++i)
		{
			fileUrl = fileUrls[i].toLocalFile().toLower();
			if (fileUrl.endsWith(".sla") || fileUrl.endsWith(".sla.gz") || fileUrl.endsWith(".shape") || fileUrl.endsWith(".sce"))
			{
				accepted = true;
				break;
			}
			QUrl url( fileUrls[i] );
			FileLoader *fileLoader = new FileLoader(url.path());
			int testResult = fileLoader->testFile();
			delete fileLoader;
			if ((testResult != -1) && (testResult >= FORMATID_FIRSTUSER))
			{
				accepted = true;
				break;
			}
		}
	}
	else if (e->mimeData()->hasText())
	{
		QString text = e->mimeData()->text();
		if ((text.startsWith("<SCRIBUSELEM")) || (text.startsWith("SCRIBUSELEMUTF8")) || (text.startsWith("ScribusElementUTF8")))
			accepted = true;
	}
	if (accepted)
		e->accept();
}

void ScribusMainWindow::dropEvent ( QDropEvent * e)
{
	bool accepted = false;
	if (e->mimeData()->hasFormat("text/uri-list"))
	{
		QString fileUrl;
		QList<QUrl> fileUrls = e->mimeData()->urls();
		for (int i = 0; i < fileUrls.count(); ++i)
		{
			fileUrl = fileUrls[i].toLocalFile().toLower();
			if (fileUrl.endsWith(".sla") || fileUrl.endsWith(".sla.gz"))
			{
				QUrl url( fileUrls[i] );
				QFileInfo fi(url.toLocalFile());
				if (fi.exists())
				{
					accepted = true;
					loadDoc( fi.absoluteFilePath() );
				}
			}
			else if (fileUrl.endsWith(".sce"))
			{
				QUrl url( fileUrls[i] );
				QFileInfo fi(url.toLocalFile());
				if (fi.exists())
				{
					accepted = true;
					QString data;
					QByteArray cf;
					loadRawText(url.toLocalFile(), cf);
					data = QString::fromUtf8(cf.data());
					double gx, gy, gw, gh;
					ScriXmlDoc ss;
					if (ss.readElemHeader(data, false, &gx, &gy, &gw, &gh))
					{
						doFileNew(gw, gh, 0, 0, 0, 0, 0, 0, false, false, 0, false, 0, 1, "Custom", true);
						HaveNewDoc();
						doc->reformPages(true);
						slotElemRead(data, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), false, false, doc, view);
						slotDocCh(false);
						doc->regionsChanged()->update(QRectF());
					}
				}
			}
			else
			{
				QUrl url( fileUrls[i] );
				FileLoader *fileLoader = new FileLoader(url.toLocalFile());
				int testResult = fileLoader->testFile();
				delete fileLoader;
				if ((testResult != -1) && (testResult >= FORMATID_FIRSTUSER))
				{
					QFileInfo fi(url.toLocalFile());
					if (fi.exists())
					{
						accepted = true;
						loadDoc( fi.absoluteFilePath() );
					}
				}
			}
		}
	}
	else
	{
		if (e->mimeData()->hasText())
		{
			QString text = e->mimeData()->text();
			if ((text.startsWith("<SCRIBUSELEM")) || (text.startsWith("SCRIBUSELEMUTF8")) || (text.startsWith("ScribusElementUTF8")))
			{
				double gx, gy, gw, gh;
				ScriXmlDoc ss;
				if (ss.readElemHeader(text, false, &gx, &gy, &gw, &gh))
				{
					doFileNew(gw, gh, 0, 0, 0, 0, 0, 0, false, false, 0, false, 0, 1, "Custom", true);
					HaveNewDoc();
					doc->reformPages(true);
					slotElemRead(text, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), false, false, doc, view);
					slotDocCh(false);
					doc->regionsChanged()->update(QRectF());
				}
				accepted = true;
			}
		}
	}
	if (accepted)
		e->accept();
}

void ScribusMainWindow::slotEditCopyContents()
{
	PageItem* currItem = HaveDoc ? doc->m_Selection->itemAt(0) : nullptr;
	if (!currItem || currItem->itemType() != PageItem::ImageFrame)
		return;

	const PageItem_ImageFrame* imageItem = currItem->asImageFrame();
	if (!imageItem->imageIsAvailable)
		return;
	contentsBuffer.contentsFileName.clear();
	contentsBuffer.sourceType = PageItem::ImageFrame;
	contentsBuffer.contentsFileName = imageItem->Pfile;
	contentsBuffer.LocalScX = imageItem->imageXScale();
	contentsBuffer.LocalScY = imageItem->imageYScale();
	contentsBuffer.LocalX   = imageItem->imageXOffset();
	contentsBuffer.LocalY   = imageItem->imageYOffset();
	contentsBuffer.LocalRot = imageItem->imageRotation();
	contentsBuffer.ItemX   = imageItem->xPos();
	contentsBuffer.ItemY   = imageItem->yPos();
	contentsBuffer.effects = imageItem->effectsInUse;
	contentsBuffer.inputProfile = imageItem->ImageProfile;
	contentsBuffer.useEmbedded  = imageItem->UseEmbedded;
	contentsBuffer.renderingIntent = imageItem->ImageIntent;
}

void ScribusMainWindow::slotEditPasteContents(int absolute)
{
	if (!HaveDoc || contentsBuffer.contentsFileName.isEmpty())
		return;
	PageItem *currItem = nullptr;
	if ((currItem = doc->m_Selection->itemAt(0)) == nullptr)
		return;
	if (contentsBuffer.sourceType != PageItem::ImageFrame || currItem->itemType() != PageItem::ImageFrame)
		return;

	PageItem_ImageFrame* imageItem = currItem->asImageFrame();
	int i = QMessageBox::Yes;
	if (imageItem->imageIsAvailable)
		i = ScMessageBox::warning(this, CommonStrings::trWarning,
								tr("Do you really want to replace your existing image?"),
								QMessageBox::Yes | QMessageBox::No,
								QMessageBox::No,	// GUI default
								QMessageBox::Yes);	// batch default
	if (i != QMessageBox::Yes)
		return;

	imageItem->pixm.imgInfo.isRequest = false;
	imageItem->EmbeddedProfile.clear();
	imageItem->ImageProfile = doc->cmsSettings().DefaultImageRGBProfile;
	imageItem->ImageIntent  = doc->cmsSettings().DefaultIntentImages;
	imageItem->effectsInUse = contentsBuffer.effects;
	QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	doc->loadPict(contentsBuffer.contentsFileName, imageItem);
	imageItem->setImageXYScale(contentsBuffer.LocalScX, contentsBuffer.LocalScY);
	if (absolute == 0)
		imageItem->setImageXYOffset(contentsBuffer.LocalX, contentsBuffer.LocalY);
	else
		imageItem->setImageXYOffset(((contentsBuffer.ItemX - imageItem->xPos()) / contentsBuffer.LocalScX) + contentsBuffer.LocalX,
		                            ((contentsBuffer.ItemY - imageItem->yPos()) / contentsBuffer.LocalScY) + contentsBuffer.LocalY);
	imageItem->setImageRotation(contentsBuffer.LocalRot);
	imageItem->ImageProfile = contentsBuffer.inputProfile;
	imageItem->ImageIntent = contentsBuffer.renderingIntent;
	imageItem->UseEmbedded = contentsBuffer.useEmbedded;
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	view->DrawNew();
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	emit UpdateRequest(reqCmsOptionsUpdate);
	currItem->emitAllToGUI();
	QApplication::restoreOverrideCursor();
}

void ScribusMainWindow::slotInsertFrame()
{
	if (!HaveDoc)
		return;

	view->requestMode(modeNormal);
	if (doc->m_Selection->isNotEmpty())
		view->deselectItems(false);

	InsertAFrame dia(this, doc);
	if (dia.exec())
	{
		InsertAFrameData iafData;
		dia.getNewFrameProperties(iafData);
		doc->itemAddUserFrame(iafData);
	}
}

void ScribusMainWindow::slotItemTransform()
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->isEmpty())
		return;

	TransformDialog td(this, doc);
	if (td.exec() == 0)
		return;
	UndoTransaction trans;
	if (UndoManager::undoEnabled())
		trans = m_undoManager->beginTransaction(Um::Selection, Um::IPolygon, Um::Transform, QString(), Um::IMove);
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	int count = td.getCount();
	QTransform matrix(td.getTransformMatrix());
	AnchorPoint basepoint = td.getBasepoint();
	doc->itemSelection_Transform(count, matrix, basepoint);
	QApplication::restoreOverrideCursor();
	if (trans)
	{
		trans.commit();
	}
}

void ScribusMainWindow::slotAutoArrangeFrames()
{
	if (!HaveDoc)
		return;

	// Dry run first, always: the dialog computes and displays the plan, and the
	// document is only written if the operator presses Apply. There is no code
	// path that moves a frame without showing the move first.
	AutoArrangeDialog dlg(doc, this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	const int moved = AutoArrangeEngine::applyPlan(doc, dlg.plan());
	QApplication::restoreOverrideCursor();

	if (moved > 0)
		ScMessageBox::information(this, tr("Auto Arrange Frames"),
			tr("%1 frame(s) moved. This is a single undo step — press Ctrl+Z to put "
			   "them all back.").arg(moved));
}

void ScribusMainWindow::PutToInline(const QString& buffer)
{
	if (!HaveDoc)
		return;
	Selection tempSelection(*doc->m_Selection);
	bool savedAlignGrid = doc->SnapGrid;
	bool savedAlignGuides = doc->SnapGuides;
	bool savedAlignElement = doc->SnapItems;
	int ac = doc->Items->count();
	bool isGroup = false;
	double gx, gy, gh, gw;
	FPoint minSize = doc->minCanvasCoordinate;
	FPoint maxSize = doc->maxCanvasCoordinate;
	doc->SnapGrid  = false;
	doc->SnapGuides = false;
	doc->SnapItems = false;
	m_undoManager->setUndoEnabled(false);
	slotElemRead(buffer, 0, 0, false, true, doc, view);
	doc->SnapGrid  = savedAlignGrid;
	doc->SnapGuides = savedAlignGuides;
	doc->SnapItems = savedAlignElement;
	doc->m_Selection->clear();
	if (doc->Items->count() - ac > 1)
		isGroup = true;
	doc->m_Selection->delaySignalsOn();
	for (int as = ac; as < doc->Items->count(); ++as)
	{
		doc->m_Selection->addItem(doc->Items->at(as));
	}
	if (isGroup)
		doc->GroupCounter++;
	doc->m_Selection->getGroupRect(&gx, &gy, &gw, &gh);
	PageItem* currItem3 = doc->Items->at(ac);
	currItem3->isEmbedded = true;
	currItem3->setIsAnnotation(false);
	currItem3->isBookmark = false;
	currItem3->gXpos = currItem3->xPos() - gx;
	currItem3->gYpos = currItem3->yPos() - gy;
	currItem3->gWidth = gw;
	currItem3->gHeight = gh;
	doc->addToInlineFrames(currItem3);
	int acc = doc->Items->count();
	for (int as = ac; as < acc; ++as)
	{
		doc->Items->takeAt(ac);
	}
	doc->m_Selection->clear();
	doc->m_Selection->delaySignalsOff();
	*doc->m_Selection = tempSelection;
	doc->minCanvasCoordinate = minSize;
	doc->maxCanvasCoordinate = maxSize;
	m_undoManager->setUndoEnabled(true);
	inlinePalette->unsetDoc();
	inlinePalette->setDoc(doc);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	view->deselectItems(false);
}

void ScribusMainWindow::PutToInline()
{
	if (!HaveDoc)
		return;
	Selection tempSelection(*doc->m_Selection);
	bool savedAlignGrid = doc->SnapGrid;
	bool savedAlignGuides = doc->SnapGuides;
	bool savedAlignElement = doc->SnapItems;
	int ac = doc->Items->count();
	bool isGroup = false;
	double gx, gy, gh, gw;
	FPoint minSize = doc->minCanvasCoordinate;
	FPoint maxSize = doc->maxCanvasCoordinate;
	doc->SnapGrid  = false;
	doc->SnapGuides = false;
	doc->SnapItems = false;
	m_undoManager->setUndoEnabled(false);
	internalCopy = true;
	slotEditCopy();
	slotElemRead(internalCopyBuffer, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), false, true, doc, view);
	internalCopy = false;
	doc->SnapGrid  = savedAlignGrid;
	doc->SnapGuides = savedAlignGuides;
	doc->SnapItems = savedAlignElement;
	doc->m_Selection->clear();
	if (doc->Items->count() - ac > 1)
		isGroup = true;
	doc->m_Selection->delaySignalsOn();
	for (int as = ac; as < doc->Items->count(); ++as)
	{
		doc->m_Selection->addItem(doc->Items->at(as));
	}
	if (isGroup)
		doc->GroupCounter++;
	doc->m_Selection->getGroupRect(&gx, &gy, &gw, &gh);
	PageItem* currItem3 = doc->Items->at(ac);
	currItem3->isEmbedded = true;
	currItem3->setIsAnnotation(false);
	currItem3->isBookmark = false;
	currItem3->gXpos = currItem3->xPos() - gx;
	currItem3->gYpos = currItem3->yPos() - gy;
	currItem3->gWidth = gw;
	currItem3->gHeight = gh;
	doc->addToInlineFrames(currItem3);
	int acc = doc->Items->count();
	for (int as = ac; as < acc; ++as)
	{
		doc->Items->takeAt(ac);
	}
	doc->m_Selection->clear();
	doc->m_Selection->delaySignalsOff();
	*doc->m_Selection = tempSelection;
	doc->minCanvasCoordinate = minSize;
	doc->maxCanvasCoordinate = maxSize;
	m_undoManager->setUndoEnabled(true);
	inlinePalette->unsetDoc();
	inlinePalette->setDoc(doc);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	view->deselectItems(false);
}

void ScribusMainWindow::PutToPatterns()
{
	if (!HaveDoc)
		return;

	QString patternName("Pattern_" + doc->m_Selection->itemAt(0)->itemName());
	patternName = patternName.trimmed().simplified().replace(" ", "_");
	patternName = doc->getUniquePatternName(patternName);

	bool savedAlignGrid = doc->SnapGrid;
	bool savedAlignGuides = doc->SnapGuides;
	bool savedAlignElement = doc->SnapItems;
	int ac = doc->Items->count();
	FPoint minSize = doc->minCanvasCoordinate;
	FPoint maxSize = doc->maxCanvasCoordinate;
	doc->SnapGrid  = false;
	doc->SnapGuides = false;
	doc->SnapItems = false;
	m_undoManager->setUndoEnabled(false);
	internalCopy = true;
	slotEditCopy();
	slotElemRead(internalCopyBuffer, doc->currentPage()->xOffset(), doc->currentPage()->yOffset(), false, true, doc, view);
	internalCopy = false;
	doc->SnapGrid  = savedAlignGrid;
	doc->SnapGuides = savedAlignGuides;
	doc->SnapItems = savedAlignElement;
	doc->m_Selection->clear();
	view->deselectItems(true);
	PageItem* currItem;
	doc->m_Selection->delaySignalsOn();
	for (int as = ac; as < doc->Items->count(); ++as)
	{
		doc->m_Selection->addItem(doc->Items->at(as));
	}
	if (doc->Items->count() - ac > 1)
		currItem = doc->groupObjectsSelection(doc->m_Selection);
	else
		currItem = doc->m_Selection->itemAt(0);
	QList<PageItem*> allItems;
	if (currItem->isGroup())
		allItems = currItem->getAllChildren();
	else
		allItems.append(currItem);
	QStringList results;
	for (int ii = 0; ii < allItems.count(); ii++)
	{
		PageItem *item = allItems.at(ii);
		if ((!results.contains(item->pattern())) && ((item->GrType == Gradient_Pattern) || (item->itemType() == PageItem::Symbol)))
			results.append(item->pattern());
		if (!item->strokePattern().isEmpty())
		{
			if (!results.contains(item->strokePattern()))
				results.append(item->strokePattern());
		}
		if (!item->patternMask().isEmpty())
		{
			if (!results.contains(item->patternMask()))
				results.append(item->patternMask());
		}
	}
	patternsDependingOnThis.clear();
	QStringList mainPatterns = doc->docPatterns.keys();
	for (int i = 0; i < results.count(); i++)
	{
		QString temp(results[i]);
		for (int j = 0; j < mainPatterns.count(); j++)
		{
			if (mainPatterns[j] != temp)
			{
				QStringList subPatterns;
				subPatterns = doc->getUsedPatternsHelper(mainPatterns[j], subPatterns);
				if (subPatterns.contains(temp))
					patternsDependingOnThis.prepend(mainPatterns[j]);
			}
		}
		patternsDependingOnThis.prepend(temp);
	}
	allItems.clear();

	Query dia(this, "tt", true, tr("&Name:"), tr("New Entry"));
	dia.setEditText(patternName, true);
	dia.setForbiddenList(patternsDependingOnThis);
	dia.setTestList(doc->docPatterns.keys());
	dia.setCheckMode(true);
	if (dia.exec() != QDialog::Accepted)
	{
		doc->m_Selection->clear();
		doc->m_Selection->delaySignalsOff();
		doc->Items->removeAll(currItem);
		delete currItem;
		doc->minCanvasCoordinate = minSize;
		doc->maxCanvasCoordinate = maxSize;
		if (outlinePalette->isVisible())
			outlinePalette->BuildTree();
		m_undoManager->setUndoEnabled(true);
		return;
	}
	patternName = dia.getEditText();

	double minx =  std::numeric_limits<double>::max();
	double miny =  std::numeric_limits<double>::max();
	double maxx = -std::numeric_limits<double>::max();
	double maxy = -std::numeric_limits<double>::max();
	double x1, x2, y1, y2;
	currItem->getVisualBoundingRect(&x1, &y1, &x2, &y2);
	minx = qMin(minx, x1);
	miny = qMin(miny, y1);
	maxx = qMax(maxx, x2);
	maxy = qMax(maxy, y2);

	ScPattern pat(doc);
	pat.pattern = currItem->DrawObj_toImage(qMin(qMax(maxx - minx, maxy - miny), 500.0));
	pat.width = maxx - minx;
	pat.height = maxy - miny;
	pat.items.append(currItem);

	// #11274 : OwnPage is not meaningful for pattern items
	// We set consequently pattern item's OwnPage to -1
	QList<PageItem*> patternItems = pat.items;
	while (patternItems.count() > 0)
	{
		PageItem* patItem = patternItems.takeAt(0);
		if (patItem->isGroup())
			patternItems += patItem->groupItemList;
		patItem->OwnPage = -1;
	}
	if (doc->docPatterns.contains(patternName))
		doc->docPatterns.remove(patternName);
	currItem->gXpos = currItem->xPos() - minx;
	currItem->gYpos = currItem->yPos() - miny;
	currItem->setXYPos(currItem->gXpos, currItem->gYpos, true);
	doc->addPattern(patternName, pat);
	doc->Items->removeAll(currItem);
	doc->m_Selection->clear();
	doc->m_Selection->delaySignalsOff();
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	symbolPalette->updateSymbolList();
	emit UpdateRequest(reqColorsUpdate);
	doc->minCanvasCoordinate = minSize;
	doc->maxCanvasCoordinate = maxSize;
	view->DrawNew();
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	m_undoManager->setUndoEnabled(true);
}

void ScribusMainWindow::ConvertToSymbol()
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->isEmpty())
		return;

	QString patternName("Pattern_" + doc->m_Selection->itemAt(0)->itemName());
	patternName = patternName.trimmed().simplified().replace(" ", "_");
	patternName = doc->getUniquePatternName(patternName);

	Query dia(this, "tt", true, tr("&Name:"), tr("New Entry"));
	dia.setEditText(patternName, true);
	patternsDependingOnThis.clear();
	dia.setForbiddenList(patternsDependingOnThis);
	dia.setTestList(doc->docPatterns.keys());
	dia.setCheckMode(true);
	if (!dia.exec())
		return;
	patternName = dia.getEditText();
	m_undoManager->setUndoEnabled(false);
	doc->itemSelection_convertItemsToSymbol(patternName);
	propertiesPalette->updateColorList();
	contentPalette->updateColorList();
	symbolPalette->updateSymbolList();
	emit UpdateRequest(reqColorsUpdate);
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
	view->DrawNew();
	m_undoManager->setUndoEnabled(true);
}

void ScribusMainWindow::manageColorsAndFills()
{
	ColorList colorlist;
	QHash<QString, VGradient>* gradients { nullptr };
	QHash<QString, ScPattern> *patterns { nullptr };
	ScribusDoc* tmpDoc { nullptr };
	if (HaveDoc)
	{
		gradients = &doc->docGradients;
		colorlist = doc->PageColors;
		patterns = &doc->docPatterns;
		tmpDoc = doc;
	}
	else
	{
		gradients = &m_prefsManager.appPrefs.defaultGradients;
		colorlist = m_prefsManager.colorSet();
		patterns = &m_prefsManager.appPrefs.defaultPatterns;
		tmpDoc = m_doc;
		doc = m_doc;
	}
	m_undoManager->setUndoEnabled(false);
	ColorsAndFillsDialog *dia = new ColorsAndFillsDialog(this, gradients, colorlist, m_prefsManager.colorSetName(), patterns, tmpDoc, this);
	if (dia->exec())
	{
		if (HaveDoc)
		{
			slotDocCh();
			doc->PageColors = dia->m_colorList;
			if (dia->replaceColorMap.isEmpty())
			{
				// invalidate all charstyles, as replaceNamedResources() won't do it if all maps are empty
				const StyleSet<CharStyle> dummy;
				doc->redefineCharStyles(dummy, false);
			}
			else
			{
				ResourceCollection colorrsc;
				colorrsc.mapColors(dia->replaceColorMap);
				// Update tools colors
				PrefsManager::replaceToolColors(doc->itemToolPrefs(), colorrsc.colors());
				// Update objects and styles colors
				doc->replaceNamedResources(colorrsc);
				// Temporary code until LineStyle is effectively used
				doc->replaceLineStyleColors(dia->replaceColorMap);
			}
			doc->setGradients(dia->dialogGradients);
			if (!dia->replaceMap.isEmpty())
			{
				ResourceCollection gradrsc;
				gradrsc.mapPatterns(dia->replaceMap);
				doc->replaceNamedResources(gradrsc);
			}
			doc->setPatterns(dia->dialogPatterns);
			if (!dia->replaceMapPatterns.isEmpty())
			{
				ResourceCollection colorrsc;
				colorrsc.mapPatterns(dia->replaceMapPatterns);
				doc->replaceNamedResources(colorrsc);
			}
			doc->recalculateColors();
			if (doc->useImageColorEffects())
				doc->recalcPicturesRes(ScribusDoc::RecalcPicRes_ImageWithColorEffectsOnly);
			symbolPalette->updateSymbolList();
			updateColorLists();
			if (doc->m_Selection->isNotEmpty())
				doc->m_Selection->itemAt(0)->emitAllToGUI();
			view->DrawNew();
		}
		else
		{
			// Update tools colors if needed
			m_prefsManager.replaceToolColors(dia->replaceColorMap);
			m_prefsManager.setColorSet(dia->m_colorList);
//			propertiesPalette->colorPalette->setColors(m_prefsManager.colorSet());
			WidgetManager::instance().colorPicker()->setColorList(m_prefsManager.colorSet());
			m_prefsManager.appPrefs.defaultGradients = dia->dialogGradients;
			m_prefsManager.appPrefs.defaultPatterns = dia->dialogPatterns;
			QString Cpfad = QDir::toNativeSeparators(ScPaths::applicationDataDir())+"DefaultColors.xml";
			const FileFormat *fmt = LoadSavePlugin::getFormatById(FORMATID_SLA171EXPORT);
			if (fmt)
			{
				ScribusDoc *s_doc = new ScribusDoc();
				s_doc->setup(0, 1, 1, 1, 1, "Custom", "Custom");
				s_doc->setPage(100, 100, 0, 0, 0, 0, 0, 0, false, false);
				s_doc->addPage(0);
				s_doc->setGUI(false, this, nullptr);
				s_doc->PageColors = dia->m_colorList;
				s_doc->setGradients(dia->dialogGradients);
				s_doc->setPatterns(dia->dialogPatterns);
				fmt->setupTargets(s_doc, nullptr, this, mainWindowProgressBar, &(PrefsManager::instance().appPrefs.fontPrefs.AvailFonts));
				fmt->savePalette(Cpfad);
				delete s_doc;
			}
			m_prefsManager.setColorSetName(dia->getColorSetName());
			doc = nullptr;
		}
	}
	if (!HaveDoc)
		doc = nullptr;
	delete dia;
	m_undoManager->setUndoEnabled(true);
}

void ScribusMainWindow::slotReplaceColors()
{
	if (!HaveDoc)
		return;

	ColorList UsedC;
	doc->getUsedColors(UsedC);
	QScopedPointer<replaceColorsDialog> dia2(new replaceColorsDialog(this, doc->PageColors, UsedC));
	if (!dia2->exec())
		return;

	ResourceCollection colorrsc;
	colorrsc.mapColors(dia2->replaceMap);
	PrefsManager::replaceToolColors(doc->itemToolPrefs(), colorrsc.colors());
	doc->replaceNamedResources(colorrsc);
	doc->replaceLineStyleColors(dia2->replaceMap);
	doc->recalculateColors();
	if (doc->useImageColorEffects())
		doc->recalcPicturesRes(ScribusDoc::RecalcPicRes_ImageWithColorEffectsOnly);
	requestUpdate(reqColorsUpdate | reqLineStylesUpdate);
	m_styleManager->refreshLists();
	if (doc->m_Selection->isNotEmpty())
		doc->m_Selection->itemAt(0)->emitAllToGUI();
	view->DrawNew();
}

void ScribusMainWindow::updateGUIAfterPagesChanged()
{
	view->DrawNew();
	pagePalette->rebuildPages();
	if (outlinePalette->isVisible())
		outlinePalette->BuildTree();
}

void ScribusMainWindow::updateTableMenuActions()
{
	appModeHelper->updateTableMenuActions(doc);
}

void ScribusMainWindow::insertMark(MarkType mType)
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->count() != 1)
		return;
	if (doc->appMode != modeEdit)
		return;

	PageItem* currItem = doc->m_Selection->itemAt(0);
	if (!currItem->isTextFrame())
		return;
	QString initialText;
	int startOfWord = -1;
	if (mType == MARKIndexType)
	{
		int startOfSelection = 0;
		// int endOfSelection = 0;
		if (currItem->itemText.hasSelection())
		{
			startOfSelection = currItem->itemText.startOfSelection();
			// endOfSelection = currItem->itemText.endOfSelection();
		}
		// qDebug()<<"start of selection:"<<startOfSelection<<currItem->itemText.text(startOfSelection);
		int cp = currItem->itemText.cursorPosition();
		// qDebug()<<"Cursor Position:"<<cp<<currItem->itemText.text(cp);
		if (SpecialChars::isBreakingSpace(currItem->itemText.text(cp)) || currItem->itemText.text(cp).isSpace())
		{
			// qDebug()<<"Found a space";
		}
		if (currItem->itemText.text(cp) == SpecialChars::OBJECT)
		{
			// qDebug()<<"Found an object";
			//detect when there is already a Mark
			if (currItem->itemText.hasMark(cp))
			{
				++cp;
				// qDebug()<<"Found a Mark";
			}
		}
		startOfWord = qMax(0, qMin(startOfSelection, cp));
		// qDebug()<<"minStartPos:"<<startOfWord<<currItem->itemText.text(startOfWord);

		// startOfWord = currItem->itemText.prevWord(cp);
		int endOfWord = currItem->itemText.endOfWord(cp);

		// qDebug()<<"startOfWord:"<<cp<<currItem->itemText.text(startOfWord)<<"end word"<<endOfWord;
		initialText = currItem->itemText.text(startOfWord, endOfWord - startOfWord);
		// qDebug()<<"Word for Index"<<initialText;
	}

	UndoTransaction trans;
	if (UndoManager::undoEnabled())
		trans = m_undoManager->beginTransaction();

	ScItemsState* is = nullptr;
	if (insertMarkDialog(currItem->asTextFrame(), mType, is, initialText, startOfWord))
	{
		Mark* mrk = nullptr;
		if (mType == MARKIndexType)
			mrk = currItem->itemText.mark(startOfWord);
		else
			mrk = currItem->itemText.mark(currItem->itemText.cursorPosition() - 1);
		view->updatesOn(false);
		currItem->invalidateLayout();
		currItem->layout();
		if (mType == MARKNoteMasterType)
		{
			doc->setNotesChanged(true);
			if (mrk->getNotePtr()->isEndNote())
				doc->flag_updateEndNotes = true;
			doc->setCursor2MarkPos(mrk->getNotePtr()->noteMark());
			nsEditor->setNotesStyle(mrk->getNotePtr()->notesStyle());
		}
		doc->changed();
		if (is != nullptr)
			is->set("label", mrk->label);
		view->updatesOn(true);
		view->DrawNew();
	}
	if (trans)
		trans.commit();
}

void ScribusMainWindow::slotEditMark()
{
	if (!HaveDoc)
		return;
	if (doc->m_Selection->count() != 1)
		return;
	if  (doc->appMode != modeEdit)
		return;
	PageItem * currItem = doc->m_Selection->itemAt(0);
	if (currItem->itemText.cursorPosition() < currItem->itemText.length())
	{
		if (currItem->itemText.hasMark(currItem->itemText.cursorPosition()))
		{
			Mark* mark = currItem->itemText.mark(currItem->itemText.cursorPosition());
			if (editMarkDlg(mark, currItem->asTextFrame()))
			{
				if (mark->isType(MARKVariableTextType))
					doc->flag_updateMarksLabels = true;
				else
					currItem->invalid = true;
				//doc->updateMarks();
				doc->changed();
				doc->regionsChanged()->update(QRectF());
				view->DrawNew();
			}
			if (mark->isNoteType())
				nsEditor->setNotesStyle(mark->getNotePtr()->notesStyle());
		}
	}
}

void ScribusMainWindow::slotUpdateMarks()
{
	if (!HaveDoc)
		return;
	if (doc->marksList().isEmpty())
		return;
	if (doc->updateMarks(true))
	{
		doc->changed();
		doc->regionsChanged()->update(QRectF());
	}
}

void ScribusMainWindow::slotInsertMarkNote()
{
	if (!HaveDoc)
		return;
	if (doc->m_docNotesStylesList.count() == 1)
	{ //fast insert note with the only default notes style available
		PageItem* currItem = doc->m_Selection->itemAt(0);
		Q_ASSERT(currItem->isTextFrame() && !currItem->isNoteFrame());
		UndoTransaction trans;
		if (currItem->HasSel)
		{
			if (UndoManager::undoEnabled())
				trans = m_undoManager->beginTransaction(Um::Selection, Um::IDelete, Um::Delete, QString(), Um::IDelete);
			//inserting mark replace some selected text
			currItem->asTextFrame()->deleteSelectedTextFromFrame();
		}
		NotesStyle* nStyle = doc->m_docNotesStylesList.at(0);
		QString label = "NoteMark_" + nStyle->name();
		if (nStyle->range() == NSRstory)
			label += " in " + currItem->firstInChain()->itemName();
		if (doc->getMark(label + "_1", MARKNoteMasterType) != nullptr)
			getUniqueName(label,doc->marksLabelsList(MARKNoteMasterType), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
		else
			label = label + "_1";
		Mark* mrk = doc->newMark();
		mrk->label = label;
		mrk->setType(MARKNoteMasterType);
		mrk->setNotePtr(doc->newNote(nStyle));
		mrk->getNotePtr()->setMasterMark(mrk);
		mrk->clearString();
		mrk->OwnPage = currItem->OwnPage;
		currItem->itemText.insertMark(mrk);
		currItem->invalidateLayout();
		currItem->layout();
		if (mrk->getNotePtr()->isEndNote())
			doc->flag_updateEndNotes = true;
		doc->regionsChanged()->update(QRectF());
		doc->changed();
		doc->setCursor2MarkPos(mrk->getNotePtr()->noteMark());
		if (UndoManager::undoEnabled())
		{
			auto* is = new ScItemsState(UndoManager::InsertNote);
			is->set("ETEA", mrk->label);
			is->set("MARK", QString("new"));
			is->set("label", mrk->label);
			is->set("type", (int) MARKNoteMasterType);
			is->set("strtxt", QString());
			is->set("nStyle", nStyle->name());
			is->set("at", currItem->itemText.cursorPosition() - 1);
			is->insertItem("inItem", currItem);
			m_undoManager->action(doc, is);
		}
		if (trans)
			trans.commit();
	}
	else
		insertMark(MARKNoteMasterType);
}

bool ScribusMainWindow::insertMarkDialog(PageItem_TextFrame* currItem, MarkType mrkType, ScItemsState* &is, const QString &initialText, int markInsertPosition)
{
	//avoid inserting in master pages other marks than Variable Text
	if (doc->masterPageMode() && (mrkType != MARKVariableTextType))
		return false;
	QString index;
	QScopedPointer<MarkInsert> insertMDialog;
	switch (mrkType)
	{
		case MARKAnchorType:
			insertMDialog.reset((MarkInsert*) new MarkAnchor(this));
			break;
		case MARKVariableTextType:
			insertMDialog.reset((MarkInsert*) new MarkVariableText(doc->marksList(), this));
			break;
		case MARK2ItemType:
			insertMDialog.reset((MarkInsert*) new Mark2Item(this));
			break;
		case MARK2MarkType:
			insertMDialog.reset((MarkInsert*) new Mark2Mark(doc->marksList(), nullptr, this));
			break;
		case MARKNoteMasterType:
			insertMDialog.reset((MarkInsert*) new MarkNote(doc->m_docNotesStylesList, this));
			break;
		case MARKIndexType:
			{
				QStringList indexList;
				const IndexSetupVector& indexSetups = doc->indexSetups();
				//TODO: maybe if indexSetups's empty, error out instead of opening the entry creation dialog?
				for (auto indexSetupIt = indexSetups.begin(); indexSetupIt != indexSetups.end(); ++indexSetupIt)
					indexList.append(indexSetupIt->name);
				insertMDialog.reset((MarkInsert*) new MarkIndex(initialText, index, indexList, this));
			}
			break;
		default:
			break;
	}

	if (insertMDialog.isNull())
	{
		qDebug() << "Dialog not implemented for marks of type " << mrkType;
		return false;
	}
	bool docWasChanged = false;

	insertMDialog->setWindowTitle(tr("Insert New ") + insertMDialog->windowTitle());
	if (insertMDialog->exec() != QDialog::Accepted)
		return false;
	UndoTransaction trans;
	if (currItem->HasSel && mrkType != MARKIndexType)
	{
		if (UndoManager::undoEnabled())
			trans = m_undoManager->beginTransaction(Um::Selection, Um::IDelete, Um::Delete, QString(), Um::IDelete);
		//inserting mark replace some selected text
		currItem->asTextFrame()->deleteSelectedTextFromFrame();
	}

	Mark* mrk = nullptr;
	Mark oldMark;
	MarkData markData;
	if (currItem != nullptr)
		markData.itemName = currItem->itemName();
	QString label, text;
	NotesStyle* NStyle = nullptr;
	bool insertExistedMark = false;
	switch (mrkType)
	{
		case MARKAnchorType:
			//only gets label for new mark
			insertMDialog->values(label);
			if (label.isEmpty())
				label = tr("Anchor mark");
			markData.itemPtr = currItem;
			break;
		case MARKVariableTextType:
			mrk = insertMDialog->values(label, text);
			if ((mrk == nullptr) && (text.isEmpty()))
				return false; //FIX ME here user should be warned that inserting of mark fails and why
			if (label.isEmpty())
				label = tr("Mark with <%1> variable text").arg(text);
			markData.text = text;
			break;
		case MARK2ItemType:
			insertMDialog->values(label, markData.itemPtr);
			if (markData.itemPtr == nullptr)
				return false; //FIX ME here user should be warned that inserting of mark fails and why
			if (label.isEmpty())
				label = tr("Mark to %1 item").arg(markData.itemPtr->itemName());
			markData.text = QString::number(markData.itemPtr->OwnPage +1);
			break;
		case MARK2MarkType:
			//gets pointer to referenced mark
			Mark* markPtr;
			insertMDialog->values(label, markPtr);
			if (markPtr == nullptr)
				return false; //FIX ME here user should be warned that inserting of mark fails and why
			if (label.isEmpty())
				label = tr("Mark to %1 mark").arg(markPtr->label);
			markData.text = QString::number(markPtr->OwnPage + 1);
			markData.destMarkName = markPtr->label;
			markData.destMarkType = markPtr->getType();
			break;
		case MARKNoteMasterType:
			//gets pointer to chosen notes style
			NStyle = insertMDialog->values();
			if (NStyle == nullptr)
				return false;

			markData.notePtr = doc->newNote(NStyle);
			label = "NoteMark_" + NStyle->name();
			if (NStyle->range() == NSRstory)
				label += " in " + currItem->firstInChain()->itemName();
			break;
		case MARKIndexType:
			{
				//Get the dialog pointer from the QScopedPointer
				MarkIndex* pMarkIndex = dynamic_cast<MarkIndex*>(insertMDialog.get());
				//Get the label data from the dialog widgets
				pMarkIndex->indexValues(label, index);
				if (label.isEmpty())
					label = tr("Index mark");
				markData.itemPtr = currItem;
				markData.text = label;
			}
			break;
		default:
			return false;
			break;
	}

	//We create a new mark entry if one exists by the same name.
	if (mrk == nullptr)
	{
		//check if label for new mark can be used as is
		switch (mrkType)
		{
			case MARKNoteMasterType:
				{
					if (doc->getMark(label + "_1", mrkType) != nullptr)
						getUniqueName(label, doc->marksLabelsList(mrkType), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
					else
						label = label + "_1";
				}
				break;
			case MARKIndexType:
				{
					Mark* foundMark = doc->getMark(label, mrkType);
					if (foundMark != nullptr)
						qDebug()<<"An existing Index Mark exists with label"<<label;
					//We'll just use the same code for now and add our label suffix
					getUniqueName(label, doc->marksLabelsList(mrkType), "_");
				}
				break;
			default:
				getUniqueName(label, doc->marksLabelsList(mrkType), "_");
				break;
		}
		mrk = doc->newMark();
		// qDebug()<<"New Mark:"<<label<<"on page"<<currItem->OwnPage;
		mrk->setValues(label, currItem->OwnPage, mrkType, markData);
	}
	else
	{ // that must be variable text mark
		oldMark = *mrk;
		mrk->setString(markData.text);
		mrk->label = label;
		mrk->OwnPage = currItem->OwnPage;
		insertExistedMark = true;
		doc->flag_updateMarksLabels = true;
	}

	if (mrkType == MARKIndexType)
		currItem->itemText.insertMark(mrk, markInsertPosition);
	else
		currItem->itemText.insertMark(mrk);

	if (mrkType == MARKNoteMasterType)
	{
		mrk->getNotePtr()->setMasterMark(mrk);
		mrk->clearString();
	}

	if (UndoManager::undoEnabled())
	{
		if (mrk->isType(MARKNoteMasterType))
			is = new ScItemsState(UndoManager::InsertNote);
		else if (insertExistedMark && ((oldMark.label != mrk->label) || (oldMark.getString() != mrk->getString())))
			is = new ScItemsState(UndoManager::EditMark);
		else
			is = new ScItemsState(UndoManager::InsertMark);
		is->set("ETEA", mrk->label);
		is->set("label", mrk->label);
		is->set("type", (int) mrk->getType());
		if (insertExistedMark)
		{
			is->set("MARK", QString("insert_existing"));
			if (mrk->label != oldMark.label)
			{
				is->set("labelOLD", oldMark.label);
				is->set("labelNEW", mrk->label);
				doc->flag_updateMarksLabels = true;
			}
			if (oldMark.getString() != mrk->getString())
			{
				is->set("strOLD", oldMark.getString());
				is->set("strNEW", mrk->getString());
			}
		}
		else
		{
			is->set("MARK", QString("new"));
			is->set("strtxt", mrk->getString());
			if (mrk->isType(MARK2MarkType))
			{
				QString dName = mrk->getDestMarkName();
				MarkType dType = mrk->getDestMarkType();
				is->set("dName", dName);
				is->set("dType", (int) dType);
			}
			if (mrk->isType(MARK2ItemType))
				is->insertItem("itemPtr", mrk->getItemPtr());
			if (mrk->isType(MARKNoteMasterType))
				is->set("nStyle", mrk->getNotePtr()->notesStyle()->name());
		}
		is->set("at", currItem->itemText.cursorPosition() - 1);
		if (currItem->isNoteFrame())
			is->set("noteframeName", currItem->getUName());
		else
			is->insertItem("inItem", currItem);
		m_undoManager->action(doc, is);
		docWasChanged = true;
	}

	if (trans)
		trans.commit();
	return docWasChanged;
}

bool ScribusMainWindow::editMarkDlg(Mark *mrk, PageItem_TextFrame* currItem)
{
	MarkInsert* editMDialog = nullptr;
	switch (mrk->getType())
	{
		case MARKAnchorType:
			editMDialog = (MarkInsert*) new MarkAnchor(this);
			editMDialog->setValues(mrk->label);
			break;
		case MARKVariableTextType:
			if (currItem == nullptr)
				//invoked from Marks Manager
				editMDialog = dynamic_cast<MarkInsert*>(new MarkVariableText(mrk, this));
			else
				//invoked from mark`s entry in text
				editMDialog = dynamic_cast<MarkInsert*>(new MarkVariableText(doc->marksList(), this));
			editMDialog->setValues(mrk->label, mrk->getString());
			break;
		case MARK2ItemType:
			editMDialog = (MarkInsert*) new Mark2Item(this);
			editMDialog->setValues(mrk->label, mrk->getItemPtr());
			break;
		case MARK2MarkType:
			{
				editMDialog = (MarkInsert*) new Mark2Mark(doc->marksList(), mrk, this);
				QString l = mrk->getDestMarkName();
				MarkType t = mrk->getDestMarkType();
				Mark* m = doc->getMark(l, t);
				editMDialog->setValues(mrk->label, m);
			}
			break;
		case MARKNoteMasterType:
			{
				//invoking editing note mark from master text
				//so we go to edit note
				TextNote* note = mrk->getNotePtr();
				if (note == nullptr)
				{
					qFatal("ScribusMainWindow::editMarkDlg - found note master mark with null pointer to note");
					return false;
				}
				Mark* noteMark = note->noteMark();
				doc->setCursor2MarkPos(noteMark);
			}
			break;
		case MARKNoteFrameType:
			{
				//invoking editing mark from note frame
				//so we go to master text
				TextNote* note = mrk->getNotePtr();
				if (note == nullptr)
				{
					qFatal("ScribusMainWindow::editMarkDlg - found note frame mark with null pointer to note");
					return false;
				}
				Mark* masterMark = note->masterMark();
				doc->setCursor2MarkPos(masterMark);
			}
			break;
		case MARKIndexType:
			{
				QString index;
				QStringList indexList;
				const IndexSetupVector& indexSetups = doc->indexSetups();
				//TODO: maybe if indexSetups's empty, error out instead of opening the entry creation dialog?
				for (auto indexSetupIt = indexSetups.begin(); indexSetupIt != indexSetups.end(); ++indexSetupIt)
					indexList.append(indexSetupIt->name);
				editMDialog = (MarkInsert*) new MarkIndex(mrk->label, index, indexList, this);
			}
			break;
		default:
			break;
	}
	if (editMDialog == nullptr)
		return false;

	bool docWasChanged = false;

	editMDialog->setWindowTitle(tr("Edit %1").arg(editMDialog->windowTitle()));
	if (editMDialog->exec())
	{
		QString  label;
		QString  text;
		QString  oldLabel = mrk->label;
		MarkData oldData = mrk->getData();
		QString  oldStr = mrk->getString();
		Mark* diaMark = nullptr;
		MarkData markData;
		if (currItem != nullptr)
			markData.itemName = currItem->itemName();
		bool newMark = false;
		bool replaceMark = false;
		switch (mrk->getType())
		{
			case MARKAnchorType:
				//only gets label for new mark
				editMDialog->values(label);
				if (label.isEmpty())
					label = tr("Anchor mark");
				if (mrk->label != label)
				{
					getUniqueName(label, doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
					mrk->label = label;
					emit UpdateRequest(reqMarksUpdate);
				}
				break;
			case MARKVariableTextType:
				diaMark = editMDialog->values(label, text);
				if (text.isEmpty())
					return false; //FIX ME here user should be warned that editing of mark fails and why
				if (label.isEmpty())
					label = tr("Mark with <%1> variable text").arg(text);
				if (diaMark != nullptr)
				{
					if (diaMark != mrk)
					{
						currItem->itemText.replaceMark(currItem->itemText.cursorPosition(), diaMark);
						mrk = diaMark;
						oldLabel = mrk->label;
						oldData = mrk->getData();
						replaceMark = true;
					}
					if (mrk->label != label)
					{
						getUniqueName(label, doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
						mrk->label = label;
						emit UpdateRequest(reqMarksUpdate);
					}
					if (text != oldStr)
					{
						mrk->setString(text);
						docWasChanged = true;
					}
				}
				else
				{
					markData.text = text;
					mrk = doc->newMark();
					getUniqueName(label, doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
					mrk->setValues(label, currItem->OwnPage, MARKVariableTextType, markData);
					currItem->itemText.replaceMark(currItem->itemText.cursorPosition(), mrk);
					docWasChanged = true;
					newMark = true;
				}
				break;
			case MARK2ItemType:
				editMDialog->values(label, markData.itemPtr);
				if (markData.itemPtr == nullptr)
					return false; //FIX ME here user should be warned that inserting of mark fails and why
				if (label.isEmpty())
					label = tr("Mark to %1 item").arg(markData.itemPtr->itemName());
				if (markData.itemPtr != mrk->getItemPtr())
				{
					mrk->setItemPtr(markData.itemPtr);
					mrk->setString(doc->getSectionPageNumberForPageIndex(markData.itemPtr->OwnPage));
					docWasChanged = true;
				}
				if (mrk->label != label)
				{
					getUniqueName(label, doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
					mrk->label = label;
				}
				break;
			case MARK2MarkType:
				{
					//gets pointer to referenced mark
					Mark* markPtr = nullptr;
					editMDialog->values(label, markPtr);
					if (markPtr == nullptr)
						return false; //FIX ME here user should be warned that inserting of mark fails and why
					if (label.isEmpty())
						label = tr("Mark to %1 mark").arg(markPtr->label);
					QString destLabel = markPtr->label;
					MarkType destType = markPtr->getType();
					if (markData.destMarkName != destLabel || markData.destMarkType != destType)
					{
						mrk->setDestMark(markPtr);
						mrk->setString(doc->getSectionPageNumberForPageIndex(markPtr->OwnPage));
						docWasChanged = true;
					}
					if (mrk->label != label)
					{
						getUniqueName(label,doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
						mrk->label = label;
					}
				}
				break;
			case MARKNoteMasterType:
				break;
			case MARKIndexType:
				//only gets label for new mark
				editMDialog->values(label);
				if (label.isEmpty())
					label = tr("Index mark");
				if (mrk->label != label)
				{
					getUniqueName(label, doc->marksLabelsList(mrk->getType()), "_"); //FIX ME here user should be warned that inserted mark`s label was changed
					mrk->label = label;
					emit UpdateRequest(reqMarksUpdate);
				}
				break;
			default:
				break;
		}
		if (UndoManager::undoEnabled())
		{
			ScItemsState* is = nullptr;
			if (newMark || replaceMark)
				is = new ScItemsState(UndoManager::InsertMark);
			else
				is = new ScItemsState(UndoManager::EditMark);
			is->set("ETEA", mrk->label);
			if (currItem != nullptr)
			{
				is->set("at", currItem->itemText.cursorPosition()-1);
				if (currItem->isNoteFrame())
					is->set("noteframeName", currItem->getUName());
				else
					is->insertItem("inItem", currItem);
			}
			is->set("label", mrk->label);
			is->set("type", (int) mrk->getType());
			is->set("strtxt", mrk->getString());
			if (newMark)
			{
				is->set("MARK", QString("new"));
				if (mrk->isType(MARK2MarkType))
				{
					is->set("dName", mrk->getDestMarkName());
					is->set("dType", (int) mrk->getDestMarkType());
				}
				if (mrk->isType(MARK2ItemType))
					is->insertItem("itemPtr", mrk->getItemPtr());
				if (mrk->isType(MARKNoteMasterType))
					is->set("nStyle", mrk->getNotePtr()->notesStyle()->name());
				doc->flag_updateMarksLabels = true;
			}
			else
			{
				if (replaceMark)
					is->set("MARK", QString("replace"));
				else
					is->set("MARK", QString("edit"));
				if (mrk->label != oldLabel)
				{
					is->set("labelOLD", oldLabel);
					is->set("labelNEW", mrk->label);
					doc->flag_updateMarksLabels = true;
				}
				if (mrk->getString() != oldData.text)
				{
					is->set("strtxtOLD", oldData.text);
					is->set("strtxtNEW", mrk->getString());
				}
				if (mrk->isType(MARK2MarkType))
				{
					const QString& dName = mrk->getDestMarkName();
					MarkType dType = mrk->getDestMarkType();
					if (dName != oldData.destMarkName || dType != oldData.destMarkType)
					{
						is->set("dNameOLD", oldData.destMarkName);
						is->set("dTypeOLD", (int) oldData.destMarkType);
						is->set("dNameNEW", dName);
						is->set("dTypeNEW", (int) dType);
					}
				}
				if (mrk->isType(MARK2ItemType) && mrk->getItemPtr() != oldData.itemPtr)
				{
					is->insertItem("itemPtrOLD", oldData.itemPtr);
					is->insertItem("itemPtrNEW", mrk->getItemPtr());
				}
			}
			m_undoManager->action(doc, is);
		}
	}
	delete editMDialog;
	return docWasChanged;
}

void ScribusMainWindow::setPreviewToolbar()
{
	modeToolBar->setEnabled(!doc->drawAsPreview);
	editToolBar->setEnabled(!doc->drawAsPreview);
	pdfToolBar->setEnabled(!doc->drawAsPreview);
	symbolPalette->setEnabled(!doc->drawAsPreview);
	inlinePalette->setEnabled(!doc->drawAsPreview);
	undoPalette->setEnabled(!doc->drawAsPreview);
	outlinePalette->setEnabled(!(doc->drawAsPreview && !doc->editOnPreview));
	propertiesPalette->setEnabled(!(doc->drawAsPreview && !doc->editOnPreview));
	contentPalette->setEnabled(!(doc->drawAsPreview && !doc->editOnPreview));
	scrMenuMgr->setMenuEnabled("Edit", !doc->drawAsPreview);
	scrMenuMgr->setMenuEnabled("Item", !doc->drawAsPreview);
	scrMenuMgr->setMenuEnabled("Insert", !doc->drawAsPreview);
	scrMenuMgr->setMenuEnabled("Page", !doc->drawAsPreview);
	scrMenuMgr->setMenuEnabled("Extras", !doc->drawAsPreview);
	HaveNewSel();
}

/*
void ScribusMainWindow::testQT_slot1(QString s)
{
	qDebug()<<"Signal data:"<<s;
}

void ScribusMainWindow::testQT_slot2(double d)
{
	qDebug()<<"Signal data:"<<d;
}

void ScribusMainWindow::testQT_slot3(int i)
{
	qDebug()<<"Signal data:"<<i;
}

void ScribusMainWindow::testQT_slot4()
{
	qDebug()<<"Signal data empty but received";
	QObject *rootObject = dynamic_cast<QObject*>(m_qqview->rootObject());
	QObject *q_xSpinBox = rootObject->findChild<QObject*>("xSpinBox");
	//if (q_xSpinBox == sender())
	{
		qDebug()<<"qov"<<q_xSpinBox->property("value").toDouble();
	}
	m_qqview->close();
	m_qqview->deleteLater();
}*/

void ScribusMainWindow::changePreviewQuality(int index)
{
	if (!HaveDoc)
		return;
	doc->allItems_ChangePreviewResolution(index);
	doc->view()->DrawNew();
}
