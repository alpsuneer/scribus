/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERSPACING_H
#define SUNEERSPACING_H

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

/*!
 \brief Layout spacing for every News Browser layout model, from ONE place:
 ~/.config/scribus/layout-spacing.json (global values + per-model overrides).

 Every placement path (Normal, Obituary, re-pack, gap check, the coming Auto
 Layout) asks forModel(<model>) and never uses its own numbers.
 */
namespace SuneerSpacing
{
	enum Key
	{
		ModuleGapV = 0,   //!< mm between stacked stories / modules
		ModuleGapH,       //!< mm gutter between modules (grid gutter)
		PhotoText,        //!< mm photo above / beside its text
		WrapPad,          //!< mm text wrap around photos and captions
		CaptionGap,       //!< mm photo -> caption
		CaptionText,      //!< mm caption -> text
		HeadlineBody,     //!< mm extra space after the headline (0 = the styles decide)
		Inset,            //!< mm inner padding of text frames
		ColumnGap,        //!< mm between text columns inside a story frame
		RuleWidth,        //!< pt separator rule between modules (0 = none)
		KeyCount
	};

	struct Values
	{
		double v[KeyCount];
		QString ruleColor { QStringLiteral("Black") };
		double operator[](Key k) const { return v[k]; }
		double pt(Key k) const { return k == RuleWidth ? v[k] : v[k] * 72.0 / 25.4; }   //!< in points
	};

	//! "normal", "obituary", "auto" (the models the dialog shows; any other key works too)
	SCRIBUS_API QStringList models();
	SCRIBUS_API QString modelTitle(const QString& model);
	SCRIBUS_API QString keyName(Key k);        //!< JSON key
	SCRIBUS_API QString keyTitle(Key k);       //!< label
	SCRIBUS_API QString keyUnit(Key k);        //!< "mm" / "pt"

	struct Store
	{
		Values global;
		QHash<QString, QMap<int, double>> overrides;   //!< model -> key -> value (absent = global)
		Values forModel(const QString& model) const;
	};

	SCRIBUS_API QString filePath();
	SCRIBUS_API Store defaults();
	//! The file, cached; written with defaults (migrated from the old settings) when missing.
	SCRIBUS_API const Store& store();
	SCRIBUS_API bool save(const Store& s, QString* error = nullptr);
	SCRIBUS_API bool exportTo(const QString& path, QString* error = nullptr);
	SCRIBUS_API bool importFrom(const QString& path, QString* error = nullptr);
	//! The values a model places with.
	SCRIBUS_API Values forModel(const QString& model);
}

#endif
