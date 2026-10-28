/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPAGEMODEL_H
#define SUNEERPAGEMODEL_H

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "scribusapi.h"

/*!
 \brief A Page Model: the layout of one whole old page as reusable storySlots -
 every story as a slot on the grid (columns, top, height, headline size and
 lines, body columns, photo storySlots), obituary modules, ad areas, fixed items
 (masthead, folio), rules and boxes. Never the old text or pictures.

 Files: <prefs>/layout-models/pages/<group>/<model>.json (+ <model>.png thumbnail);
 the groups in <prefs>/layout-models/pages/groups.json. All sizes mm, page relative.
 */
struct SCRIBUS_API SuneerPageSlot
{
	struct Photo
	{
		QRectF rect;                 //!< page relative, mm
		int cols { 1 };
		QString pos { QStringLiteral("right") };   //!< placeNews photo position key
		double captionH { 0 };
		bool caption { false };
	};
	int n { 0 };                     //!< 1 = lead
	QString kind { QStringLiteral("story") };
	QRectF rect;                     //!< the module (headline + body + photos), mm
	int col0 { 0 };
	int cols { 1 };
	int bodyCols { 1 };
	QString headStyle;
	double headSize { 0 };
	int headLines { 1 };
	int headChars { 0 };
	bool kicker { false }, byline { false }, dateline { false }, split { false };
	int bodyChars { 0 };
	QList<Photo> photos;
	double areaMm2() const { return rect.width() * rect.height(); }
};

struct SCRIBUS_API SuneerPageModel
{
	QString id;                      //!< file name without .json
	QString name;
	QString group;                   //!< group id (folder)
	QString note;
	QString created;                 //!< ISO date
	// source (no text, only where it came from)
	QString sourceFile;
	int sourcePage { 0 }, sourcePages { 1 }, sourcePageNum { 0 };
	QString sourceVersion;
	// page
	double pageW { 350 }, pageH { 538 };
	double mTop { 26 }, mBottom { 15 }, mLeft { 10 }, mRight { 10 };
	// grid
	int gridCols { 8 };
	double gridColW { 34.25 }, gridGutter { 8.0 };
	QString gridType { QStringLiteral("normal") };   //!< "normal" | "obituary"
	int obitCols { 0 };
	double obitColW { 0 }, obitGap { 0 }, obitTop { 0 };
	QStringList styles;
	// content
	QList<SuneerPageSlot> storySlots;     //!< story storySlots, importance order (slot 1 = lead)
	struct Obit { QRectF rect; bool photo { true }; };
	QList<Obit> obits;
	struct Box { QRectF rect; QString role; QString name; int chars { 0 }; int type { 0 }; };
	QList<Box> ads;                  //!< "ad space" only
	QList<Box> fixed;                //!< masthead, folio, locked
	QList<Box> rules;
	QList<Box> boxes;                //!< other text / groups (fact boxes)
	QList<Box> images;               //!< loose photos (no story)

	int storyCount() const { return storySlots.size(); }
	int photoCount() const;
	double adAreaMm2() const;
	//! "3 stories · 4 photos · ad 120 x 80 mm"
	QString summary() const;
	QString adSummary() const;
	bool isObituary() const { return gridType == QLatin1String("obituary"); }

	QJsonObject toJson() const;
	static SuneerPageModel fromJson(const QJsonObject& root);
	/*! 0..1: how alike two models are (same story slot count and matching slot rectangles,
	    columns and photo count; ads and obits count a little). >= 0.9 = "almost identical". */
	static double similarity(const SuneerPageModel& a, const SuneerPageModel& b);
	//! The model as a diagram: grey boxes and placeholder lines, never text or pictures.
	QImage thumbnail(int widthPx, bool dark = false) const;
};

struct SCRIBUS_API SuneerPageGroup
{
	QString id;                      //!< folder name
	QString name;
	QString note;
	QString grid { QStringLiteral("normal") };   //!< "normal" (8 columns, 34.25 mm, 8 mm gutter) or "obituary" (10 x 30.3 mm)
	int order { 0 };
	bool isObituary() const { return grid == QLatin1String("obituary"); }
};

namespace SuneerPageModels
{
	SCRIBUS_API QString dir();
	SCRIBUS_API QString groupDir(const QString& groupId);
	SCRIBUS_API QString filePath(const QString& groupId, const QString& modelId);
	SCRIBUS_API QString thumbPath(const QString& groupId, const QString& modelId);

	// groups
	//! Every group in order; the defaults (Page-1..Page-N, Obituary, Editorial, Sports, Business, Supplement) are written on first use.
	SCRIBUS_API QList<SuneerPageGroup> groups();
	SCRIBUS_API bool saveGroups(const QList<SuneerPageGroup>& groups, QString* error = nullptr);
	SCRIBUS_API SuneerPageGroup group(const QString& id);
	SCRIBUS_API bool hasGroup(const QString& id);
	SCRIBUS_API QList<SuneerPageGroup> defaultGroups(int pageCount);
	SCRIBUS_API int defaultPageCount();
	SCRIBUS_API void setDefaultPageCount(int n);
	//! A new group; the id is made from the name. Returns the id (empty on error).
	SCRIBUS_API QString addGroup(const QString& name, const QString& grid, const QString& note, QString* error = nullptr);
	SCRIBUS_API bool renameGroup(const QString& id, const QString& name, QString* error = nullptr);
	SCRIBUS_API bool setGroupInfo(const QString& id, const QString& note, const QString& grid, QString* error = nullptr);
	SCRIBUS_API bool moveGroup(const QString& id, int delta, QString* error = nullptr);
	//! Deletes an empty group, or one whose models are first moved to \a moveTo.
	SCRIBUS_API bool removeGroup(const QString& id, const QString& moveTo, QString* error = nullptr);
	SCRIBUS_API QString slug(const QString& name);

	// models
	SCRIBUS_API QList<SuneerPageModel> models(const QString& groupId);
	SCRIBUS_API int modelCount(const QString& groupId);
	SCRIBUS_API SuneerPageModel load(const QString& groupId, const QString& modelId, bool* ok = nullptr);
	//! Saves the model (and its thumbnail PNG). An empty id gets a free one made from the name.
	SCRIBUS_API bool save(SuneerPageModel& m, QString* error = nullptr);
	SCRIBUS_API bool remove(const QString& groupId, const QString& modelId, QString* error = nullptr);
	SCRIBUS_API bool rename(const QString& groupId, const QString& modelId, const QString& name, QString* error = nullptr);
	SCRIBUS_API bool moveModel(const QString& groupId, const QString& modelId, const QString& toGroup, QString* error = nullptr);
	SCRIBUS_API QString duplicate(const QString& groupId, const QString& modelId, QString* error = nullptr);
	SCRIBUS_API QString newId(const QString& groupId, const QString& name);
	//! The most alike model of the group and its similarity (0 when the group is empty).
	SCRIBUS_API double mostAlike(const SuneerPageModel& m, const QString& groupId, QString* otherName = nullptr, QString* otherId = nullptr);

	// export / import: one file for a group or for every group
	SCRIBUS_API bool exportGroups(const QStringList& groupIds, const QString& path, QString* error = nullptr);
	//! Imports the file; new groups are created, models with the same id get "-2". Returns how many models came in.
	SCRIBUS_API int importFile(const QString& path, QString* error = nullptr, QStringList* report = nullptr);

	/*! The group a page most likely belongs to: from the file name ("21page1.sla" -> page-1,
	    "Page-15_ObitKRN.sla" -> obituary, "sports" ...), from the page number inside a multi-page
	    file, or from the content (>= 6 obituary modules -> obituary). Empty when nothing fits. */
	SCRIBUS_API QString suggestGroup(const QString& fileName, int pageIndex, int pageCount, int pageNumAttr, int obitCount, QString* why = nullptr);
	//! The group for a document page: its saved group attribute, else suggestGroup() from the file name / page number.
	SCRIBUS_API QString groupForDocPage(const QString& savedGroup, const QString& docFileName, int pageIndex, int pageCount);
	//! A name for a model made of a page: "<file stem> p<n>" / "<file stem>".
	SCRIBUS_API QString suggestName(const QString& fileName, int pageIndex, int pageCount);
	//! The script: <share>/autolayout/page_models.py (or the source tree when run from the build dir).
	SCRIBUS_API QString extractorScript();
	SCRIBUS_API void reload();
}

#endif
