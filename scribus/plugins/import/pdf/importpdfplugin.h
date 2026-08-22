/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMPORTPDFPLUGIN_H
#define IMPORTPDFPLUGIN_H

#include "pluginapi.h"
#include "loadsaveplugin.h"
#include "../../formatidlist.h"

class ScrAction;

class PLUGIN_API ImportPdfPlugin : public LoadSavePlugin
{
	Q_OBJECT

	public:
		// Standard plugin implementation
		ImportPdfPlugin();
		virtual ~ImportPdfPlugin();
		/*!
		\author Franz Schmid
		\date
		\brief Returns name of plugin
		\retval QString containing name of plugin: Import EPS/PDF/PS...
		*/
		QString fullTrName() const override;
		const AboutData* getAboutData() const override;
		void deleteAboutData(const AboutData* about) const override;
		void languageChange() override;
		bool fileSupported(QIODevice* file, const QString& fileName = QString()) const override;
		bool loadFile(const QString & fileName, const FileFormat & fmt, int flags, int index = 0) override;
		QImage readThumbnail(const QString& fileName) override;
		void addToMainWindowMenu(ScribusMainWindow *) override {};

	public slots:
		/*!
		\author Franz Schmid
		\date
		\brief Run the EPS import
		\param fileName input filename, or QString() to prompt.
		\retval bool always true
		 */
		//! explicitDoc: if non-null, import targets this document instead of
		//! ScCore->primaryMainWindow()->doc. Used by loadFile() (the FileFormat/
		//! LoadSavePlugin generic interface) to honour a target set via
		//! setupTargets() -- needed by callers importing into a document other
		//! than whichever one is interactively active (e.g. a headless scratch
		//! doc). Direct callers (the interactive Import menu action) never pass
		//! this, so their behaviour is unchanged.
		virtual bool importFile(QString fileName = QString(), int flags = lfUseCurrentPage|lfInteractive, ScribusDoc* explicitDoc = nullptr);

	private:
		void registerFormats();
		ScrAction* importAction;
};

extern "C" PLUGIN_API int importpdf_getPluginAPIVersion();
extern "C" PLUGIN_API ScPlugin* importpdf_getPlugin();
extern "C" PLUGIN_API void importpdf_freePlugin(ScPlugin* plugin);

#endif
