/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMPORTPDFSMARTPLUGIN_H
#define IMPORTPDFSMARTPLUGIN_H

#include <QMap>
#include <QPointer>

#include "pluginapi.h"
#include "loadsaveplugin.h"
#include "../../formatidlist.h"

class ScrAction;
class ScribusMainWindow;

//! \brief Smart PDF Import plugin registration.
//!
//! Deliberately does NOT register a FileFormat via registerFormat(): this
//! importer is reachable only from its own "Smart PDF Import..." entry under
//! File > Import, never from the generic "Get Vector File..." dispatcher or
//! drag-and-drop, so it can never compete with plugins/import/pdf over the
//! .pdf extension.
class PLUGIN_API ImportPdfSmartPlugin : public LoadSavePlugin
{
	Q_OBJECT

	public:
		ImportPdfSmartPlugin();
		virtual ~ImportPdfSmartPlugin();

		QString fullTrName() const override;
		const AboutData* getAboutData() const override;
		void deleteAboutData(const AboutData* about) const override;
		void languageChange() override;
		bool fileSupported(QIODevice* file, const QString& fileName = QString()) const override;
		bool loadFile(const QString& fileName, const FileFormat& fmt, int flags, int index = 0) override;
		void addToMainWindowMenu(ScribusMainWindow* mainWin) override;

	public slots:
		bool importFile();

	private:
		QMap<QString, QPointer<ScrAction> > m_actions;
};

extern "C" PLUGIN_API int importpdfsmart_getPluginAPIVersion();
extern "C" PLUGIN_API ScPlugin* importpdfsmart_getPlugin();
extern "C" PLUGIN_API void importpdfsmart_freePlugin(ScPlugin* plugin);

#endif
