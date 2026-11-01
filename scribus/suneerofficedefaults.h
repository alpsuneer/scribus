/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/
#ifndef SUNEEROFFICEDEFAULTS_H
#define SUNEEROFFICEDEFAULTS_H

#include <QString>
#include <QStringList>
#include "scribusapi.h"

class QWidget;

/*! suneer 2026-11-01: the office's default settings travel with the package.
    <share>/office-defaults/ holds SuneerDesignStyle.conf, ParagraphStyleShortcuts.conf,
    suneer-text-gradients.json and office-defaults.json (version, sha256 of each file now and of
    every earlier shipped copy). At every start, for each shipped file:
      - the profile already received this version                -> nothing;
      - the user's file is missing, or equals the version last received (or any earlier shipped
        copy)                                                       -> replaced (old file kept as .bak-<date>);
      - the user's file differs from every shipped copy             -> asked once per shipped version:
        Use office version / Keep mine / Merge (new styles, shortcuts, presets added; theirs kept).
    What a profile received is in <prefs>/office-defaults-received.json. The dbi shortcut set is
    made the Default when the profile never chose a Default set.
    Env: SUNEER_OFFICE_DEFAULTS_DIR (shipped folder for tests), SUNEER_OFFICEDEF_ANSWER=office|keep|merge
    (the question answered without a dialog), SUNEER_OFFICEDEF_SELFTEST=<report file> (what happened,
    then Scribus quits). */
namespace SuneerOfficeDefaults
{
	//! The shipped folder (env override, else <share>/office-defaults).
	SCRIBUS_API QString shippedDir();
	//! Where the user's copy of a shipped file lives.
	SCRIBUS_API QString userPathFor(const QString& fileName);
	//! Runs the sync; returns one line per file for the log / self-test report.
	SCRIBUS_API QStringList syncAtStartup(QWidget* parent);
}

#endif
