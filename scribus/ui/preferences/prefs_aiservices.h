/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#ifndef PREFS_AISERVICES_H
#define PREFS_AISERVICES_H

#include "ui_prefs_aiservicesbase.h"
#include "prefs_pane.h"
#include "scribusapi.h"

class LamaInpaintService;
class OpenRouterInpaintService;
class ScribusDoc;

/*!
 \brief Preferences for optional AI services.

 A master switch, a choice of provider, and the settings that provider needs.
 The switch is off on a fresh profile and the feature is invisible until it is
 turned on, because this is the only part of Scribus that sends a picture
 anywhere and it should be the user who decides that rather than a default.

 The two providers are not equivalent and the page does not pretend they are.
 LaMa is a server on this machine: free, private, and nothing leaves. OpenRouter
 is a paid account on the internet, and the picture goes to it and on to a model
 vendor. Only one provider's settings are shown at a time, and the OpenRouter
 side says plainly what it costs and where the picture goes.

 Test Connection deliberately tests what is *typed in the boxes*, not what is
 saved: the reason to press it is to find out whether what was just entered is
 right, and answering about the previous value would be worse than useless.
 */
class SCRIBUS_API Prefs_AIServices : public Prefs_Pane, Ui::Prefs_AIServices
{
	Q_OBJECT

	public:
		Prefs_AIServices(QWidget* parent, ScribusDoc* doc = nullptr);
		~Prefs_AIServices();

		void restoreDefaults(struct ApplicationPrefs *prefsData) override;
		void saveGuiToPrefs(struct ApplicationPrefs *prefsData) const override;

	public slots:
		void languageChange();

	private slots:
		//! Enable or grey everything below the master switch.
		void enabledToggled(bool on);
		//! Show the settings for the chosen provider and hide the other's.
		void providerChanged(int index);
		//! Update the one-line note under the model dropdown.
		void modelChanged(int index);
		//! Let the key be read back, for checking a paste.
		void showKeyToggled(bool on);

		void testConnectionClicked();
		void connectionTested(bool ok, const QString& detail);

		void testOpenRouterClicked();
		void openRouterTested(bool ok, const QString& detail);

	private:
		void showTestResult(const QString& text, const QString& colour);
		void showOpenRouterResult(const QString& text, const QString& colour);

		//! Built on demand for a test and kept for the life of the page, so
		//! that repeated presses do not each start a thread.
		LamaInpaintService* m_service {nullptr};
		OpenRouterInpaintService* m_openRouterService {nullptr};
};

#endif // PREFS_AISERVICES_H
