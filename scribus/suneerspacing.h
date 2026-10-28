/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERSPACING_H
#define SUNEERSPACING_H

#include <QList>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

/*!
 \brief Layout spacing values. They live in the Layout Models
 (~/.config/scribus/layout-models/<id>.json, see suneerlayoutmodel.h); the
 old one-file store layout-spacing.json is migrated into them once.

 Every placement path (Normal, Obituary, re-pack, gap check) asks
 forModel(<model id>) and never uses its own numbers.
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
		KickerHeadline,   //!< mm extra space after the kicker / subheading, before the headline (0 = the styles decide)
		KeyCount
	};

	struct Values
	{
		double v[KeyCount];
		QString ruleColor { QStringLiteral("Black") };
		double operator[](Key k) const { return v[k]; }
		double pt(Key k) const { return k == RuleWidth ? v[k] : v[k] * 72.0 / 25.4; }   //!< in points
	};

	SCRIBUS_API QString keyName(Key k);        //!< JSON key
	SCRIBUS_API QString keyTitle(Key k);       //!< label
	SCRIBUS_API QString keyUnit(Key k);        //!< "mm" / "pt"

	/*! The house value of \a k: resources/autolayout/autolayout-rules.json (installed in
	    <share>/autolayout/), measured from our own pages; the built-in default when the
	    file has no number for it. \a source gets "house rules" or "built-in default". */
	SCRIBUS_API double houseDefault(Key k, QString* source = nullptr);
	SCRIBUS_API Values houseDefaults();

	//! The gaps a placed story / module can change ("Adjust gaps", Layout Model "Gaps"), in display order.
	SCRIBUS_API QList<Key> gapKeys();
	//! Gaps inside one story (the rest - story to story - belong to the page).
	SCRIBUS_API bool isStoryGap(Key k);
	//! Short label for the Gaps lists ("Headline → body text").
	SCRIBUS_API QString gapLabel(Key k);

	//! JSON of the gap values ({"headlineBody": 1.5, ...}, mm) - the layout.gaps item attribute.
	SCRIBUS_API QString gapsToString(const Values& v);
	//! \a base with the keys found in \a json; false when \a json is not a gaps object.
	SCRIBUS_API bool gapsFromString(const QString& json, Values& base);

	//! The values a Layout Model places with (Layout > Layout Models...); an unknown id gives the Normal model's.
	SCRIBUS_API Values forModel(const QString& modelId);
}

#endif
