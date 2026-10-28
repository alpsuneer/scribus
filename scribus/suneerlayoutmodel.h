/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERLAYOUTMODEL_H
#define SUNEERLAYOUTMODEL_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "scribusapi.h"
#include "suneerspacing.h"

/*!
 \brief A Layout Model: grid, structure, story / photo rules and every spacing
 value of one way to arrange a page. One JSON file per model in
   ~/.config/scribus/layout-models/<id>.json
 { "format": "faircode-layout-model", "version": 1, "model": {...},
   "obituary": {...} }   (the "obituary" part only for the Obituary engine; it
 is exactly what SuneerObitModel reads and writes).

 Sizes are mm unless the name says Pt.
 */
struct SCRIBUS_API SuneerLayoutModel
{
	QString id;                    //!< file name without .json
	QString name;
	QString engine { QStringLiteral("news") };   //!< "news" (grid of story modules) or "obituary"
	QString origin { QStringLiteral("user") };   //!< "built-in", "user", "archive", "imported"
	QString description;
	QStringList pageTypes;         //!< page types / page names it applies to (empty: every page)

	// grid
	int    columns { 8 };
	bool   gridFromGuides { false };   //!< the page's column guides give the column count when it has them
	double columnWidthMm { 0.0 };  //!< 0: from the page width, the margins and the gutter
	double gutterMm { 8.0 };       //!< = horizontal gap between modules
	bool   useDocMargins { true };
	double marginTopMm { 26.0 }, marginBottomMm { 15.0 }, marginLeftMm { 10.0 }, marginRightMm { 10.0 };

	// structure
	int  decks { 0 };              //!< horizontal decks under the lead (0: free packing)
	bool leftRail { false };       //!< column 1 kept for single-column briefs
	bool rightRail { false };      //!< last column kept for single-column briefs
	QString leadPosition { QStringLiteral("none") };   //!< none, top-left, top-centre, top-right
	int  leadColumns { 5 };

	// story modules
	struct WidthRule { int maxChars; int columns; };
	QList<WidthRule> widthRules;   //!< body length -> width in columns (first rule whose maxChars >= length)
	int fixedColumns { 0 };        //!< > 0: every story this wide (the old Normal 4+3)
	struct HeadRule { int columns; QString style; };
	QList<HeadRule> headlineRules; //!< headline paragraph style by module width (empty: the News Browser style)
	QString leadHeadlineStyle;

	// photos
	struct PhotoRule { int storyColumns; int photoColumns; };
	QList<PhotoRule> photoRules;   //!< photo width by story width
	int fixedPhotoColumns { 0 };
	QString photoPosition { QStringLiteral("right") };   //!< top, topAbove, right, left, middle, bottom, none
	double photoMaxHeightMm { 100.0 };

	// spacing (SuneerSpacing keys, mm; rule in pt)
	double gapVMm { 4.8 };
	double headlineBodyMm { 0.0 };
	double kickerHeadlineMm { 0.0 };   //!< extra space after the kicker / subheading (0 = the styles decide)
	double photoTextMm { 0.0 };
	double wrapPadMm { 2.0 };
	double captionGapMm { 0.0 };
	double captionTextMm { 2.0 };
	double insetMm { 0.0 };
	double columnGapMm { 2.5 };
	double ruleWidthPt { 0.0 };
	QString ruleColor { QStringLiteral("Black") };

	//! The Obituary engine's own values (SuneerObitModel JSON); empty for "news".
	QJsonObject obituary;
	/*! Not saved. A "news" model applied to an obituary page (SuneerLayout::obitVariant):
	    the obituary model's grid and gaps, column packing in reading order. */
	bool obitPacking { false };

	bool isObituary() const { return engine == QLatin1String("obituary"); }
	bool builtIn() const { return origin == QLatin1String("built-in"); }
	SuneerSpacing::Values spacing() const;
	void setSpacing(const SuneerSpacing::Values& v);
	//! Story width for a body of \a chars characters.
	int columnsFor(int chars, bool lead) const;
	int photoColumnsFor(int storyColumns) const;
	//! Empty: keep the News Browser title style.
	QString headlineStyleFor(int columns, bool lead) const;
	//! Does the model apply to a page of this type / name (case-insensitive part match)?
	bool appliesTo(const QString& pageKey) const;

	QJsonObject toJson() const;
	static SuneerLayoutModel fromJson(const QJsonObject& root);
};

namespace SuneerLayoutModels
{
	SCRIBUS_API QString dir();
	SCRIBUS_API QString filePath(const QString& id);
	//! Every model, built-ins first; migrates the old files on first use.
	SCRIBUS_API QList<SuneerLayoutModel> all();
	SCRIBUS_API bool exists(const QString& id);
	//! The model, or the Normal model when \a id is unknown.
	SCRIBUS_API SuneerLayoutModel byId(const QString& id);
	SCRIBUS_API bool save(const SuneerLayoutModel& m, QString* error = nullptr);
	SCRIBUS_API bool remove(const QString& id, QString* error = nullptr);
	//! A free id made from \a name ("my-model", "my-model-2", ...).
	SCRIBUS_API QString newId(const QString& name);
	SCRIBUS_API bool exportTo(const QString& id, const QString& path, QString* error = nullptr);
	//! Reads a model file; the new id is returned (a copy when the id exists), empty on error.
	SCRIBUS_API QString importFrom(const QString& path, QString* error = nullptr);
	//! The built-in model of that id as shipped (for "Restore defaults").
	SCRIBUS_API SuneerLayoutModel builtInDefault(const QString& id);
	/*! While the Model Editor is open over the Layout Gallery: byId() of that id gives the
	    edited, not yet saved model, so its Gallery card redraws as the values change.
	    nullptr ends it. Only the Gallery sets it, and only while the editor is open. */
	SCRIBUS_API void setEditing(const SuneerLayoutModel* model);
	//! The model setEditing() holds (nullptr: none).
	SCRIBUS_API const SuneerLayoutModel* editing();
	//! byId() as saved in the file, never the edited copy (the Model Editor itself).
	SCRIBUS_API SuneerLayoutModel savedById(const QString& id);
	//! Re-read the files after an outside change.
	SCRIBUS_API void reload();
	//! Writes the built-ins (migrated from layout-spacing.json / the old obituary.json) when missing.
	SCRIBUS_API void ensureMigrated();
	//! Remembered model per page type (News Browser / Gallery).
	SCRIBUS_API QString lastModelFor(const QString& pageKey);
	SCRIBUS_API void setLastModelFor(const QString& pageKey, const QString& id);
}

#endif
