/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "importpdfsmartplugin.h"
#include "importpdfsmart.h"

#include "scraction.h"
#include "scribus.h"
#include "ui/scmwmenumanager.h"

int importpdfsmart_getPluginAPIVersion()
{
	return PLUGIN_API_VERSION;
}

ScPlugin* importpdfsmart_getPlugin()
{
	ImportPdfSmartPlugin* plug = new ImportPdfSmartPlugin();
	Q_CHECK_PTR(plug);
	return plug;
}

void importpdfsmart_freePlugin(ScPlugin* plugin)
{
	ImportPdfSmartPlugin* plug = qobject_cast<ImportPdfSmartPlugin*>(plugin);
	Q_ASSERT(plug);
	delete plug;
}

ImportPdfSmartPlugin::ImportPdfSmartPlugin() : LoadSavePlugin()
{
	languageChange();
}

ImportPdfSmartPlugin::~ImportPdfSmartPlugin()
{
	unregisterAll();
}

QString ImportPdfSmartPlugin::fullTrName() const
{
	return QObject::tr("Smart PDF Importer");
}

const ScActionPlugin::AboutData* ImportPdfSmartPlugin::getAboutData() const
{
	AboutData* about = new AboutData;
	about->authors = "Suneer A. <alp.suneer@gmail.com>";
	about->shortDescription = tr("Rebuilds PDF pages as editable Scribus content");
	about->description = tr("Imports a PDF page as editable text frames, images and vector objects, with optional column detection, text-flow linking, font substitution and Malayalam/Indic text correction. Separate from, and independent of, the standard PDF importer.");
	about->license = "GPL";
	Q_CHECK_PTR(about);
	return about;
}

void ImportPdfSmartPlugin::deleteAboutData(const AboutData* about) const
{
	Q_ASSERT(about);
	delete about;
}

void ImportPdfSmartPlugin::languageChange()
{
	if (m_actions.contains("fileImportPdfSmart") && m_actions["fileImportPdfSmart"])
		m_actions["fileImportPdfSmart"]->setText(tr("Smart PDF Import..."));
}

bool ImportPdfSmartPlugin::fileSupported(QIODevice* /* file */, const QString& /* fileName */) const
{
	// No FileFormat is registered for this plugin -- see the class comment
	// in importpdfsmartplugin.h. This is never called by the generic
	// dispatcher, but must exist to satisfy LoadSavePlugin.
	return false;
}

bool ImportPdfSmartPlugin::loadFile(const QString& /* fileName */, const FileFormat& /* fmt */, int /* flags */, int /* index */)
{
	return false;
}

void ImportPdfSmartPlugin::addToMainWindowMenu(ScribusMainWindow* mainWin)
{
	ScrAction* action = new ScrAction(tr("Smart PDF Import..."), QKeySequence(), this);
	m_actions.insert("fileImportPdfSmart", action);

	connect(action, SIGNAL(triggered()), this, SLOT(importFile()));

	// "FileImport" (File > Import) and the "fileImportVector" entry inside
	// it already exist by the time plugins are wired up -- ScribusMainWindow
	// builds the menu bar in createMenuBar() before calling
	// PluginManager::setupPluginActions(), which is what invokes this.
	mainWin->scrMenuMgr->addMenuItemStringAfter("fileImportPdfSmart", "fileImportVector", "FileImport");
	mainWin->scrMenuMgr->addMenuItemStringsToMenuBar("FileImport", m_actions);

	// PluginManager::setupPluginActions(ScribusMainWindow*) -- the very
	// function that just called us -- ends by doing, unconditionally:
	//   mw->scrMenuMgr->clearMenu("File");
	//   mw->scrMenuMgr->addMenuItemStringsToMenuBar("File", mw->scrActions);
	// (and the same for Edit/Insert/Item/Page/ItemTable/Extras/View/Help),
	// to pick up menu items registered by ScActionPlugin-style plugins.
	// clearMenu() wipes the whole "File" menu tree, and the rebuild
	// recreates "FileImport" as a brand-new QMenu populated only from
	// mw->scrActions -- so anything added above, living only in this
	// plugin's private m_actions map, gets silently dropped moments after
	// being added. Registering the same action in mw->scrActions as well
	// makes it survive that rebuild, exactly like every core File menu item.
	mainWin->scrActions.insert("fileImportPdfSmart", action);
}

bool ImportPdfSmartPlugin::importFile()
{
	ImportPdfSmart importer;
	return importer.run();
}
