/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERLAYOUTENGINE_H
#define SUNEERLAYOUTENGINE_H

#include <memory>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "scribusapi.h"
#include "suneerlayoutmodel.h"

class PageItem;
class PageItem_TextFrame;
class ScribusDoc;
class StoryText;
class QTimer;

/*!
 \brief Arranges stories on a page the way a Layout Model says, measured with
 the real text engine (a scratch document carrying the page's styles), without
 touching the document. Used by the Layout Gallery thumbnails and the Model
 Editor's sample page.

 Everything runs on the GUI thread (Scribus text layout is not thread safe),
 in small steps driven by SuneerLayout::Runner, so the dialogs never freeze.
 Measurements are cached across models (same story, width and spacing).
 All geometry is in points, relative to the page's top-left corner.
 */
namespace SuneerLayout
{
	enum class Keep
	{
		PlacedFrames = 0,   //!< hand-placed frames (non-story frames, pinned modules) stay; every story is arranged
		EveryFrame,         //!< every frame on the page stays; only the new stories are arranged
		OnlyNew             //!< only the new stories, around the hand-placed frames (placed stories not counted)
	};
	SCRIBUS_API QString keepTitle(Keep k);
	SCRIBUS_API QString keepTip(Keep k);

	struct StyleNames
	{
		QString title, body, firstBody, kicker, byline, dateline, highlights, caption;
		//! The News Browser settings (Faircode/ScribusNews style/*), \a title the panel's headline style.
		static StyleNames fromSettings(const QString& title);
	};

	struct PhotoIn
	{
		QString path;          //!< picture file (may be empty: grey box)
		double aspect { 0.75 };  //!< height / width
		QString caption;
	};

	struct StoryIn
	{
		QString key;           //!< news.story id (placed) or "new:<server id>"
		QString title, kicker, byline, dateline;
		QStringList highlights, body;   //!< a new story's paragraphs
		int bodyChars { 0 };
		bool placed { false };
		bool manual { false }; //!< pinned with "Pin position"
		QRectF placedRect;     //!< placed: the module's bounding box on the page
		int placedCols { 0 };
		//! suneer 2026-11-01: the story's place in the workflow list (news.seq on a placed module, the list index of a
		//! new one); the Obituary model packs in this order, so a page placed here and a page opened from the
		//! workflow arrange the same. -1 = unknown (reading order)
		int seq { -1 };
		//! suneer: the desk's "Columns" (Workflow News.noOfColumns, the News Browser override, or the
		//! news.columns attribute of a placed story): the module is exactly this wide; 0 = the model's rule
		int requestedCols { 0 };
		//! suneer: "Place with…" headline style / photo position of the story (news.headStyle, news.photoPos
		//! with news.photoPosUser = 1 on a placed story); empty = the model's rule
		QString requestedHead, requestedPos;
		QPointer<PageItem> textItem;    //!< placed: the story text frame (read only)
		QList<QPointer<PageItem>> items;   //!< placed: every item of the module (rendered as is when it stays)
		QList<PhotoIn> photos;
		QString kind { QStringLiteral("news") };   //!< "news", "story", "obit", "group", "block"
		bool rigid { false };          //!< placed: moved as it is (its size stays)
		QList<QPointer<PageItem>> heads;   //!< placed (detected): headline frames stacked above the body
		QList<QPointer<PageItem>> photoItems, captionItems;
		QString moduleId;
		bool selected { false };
		//! placed: the story's own gaps (layout.gaps when layout.gapsUser = 1, "Adjust gaps"); empty = the model's
		QString userGaps;
	};

	/*!
	 \brief One module of a page: a News Browser story (news.*), a module the user checked
	 (layout.module / layout.role / layout.fixed attributes) or one found by detectModules().
	 */
	struct Module
	{
		QString id;
		QString kind;                 //!< "news", "story", "obit", "group", "block", "fixed"
		QString reason;               //!< why fixed: locked, rule, masthead/folio, advert, picture, chained, text, user, master
		bool fixed { false };
		bool rigid { false };         //!< moved as a block (group, obit, chain parts, several bodies, no body)
		bool remembered { false };    //!< read from layout.* / news.* attributes (not guessed)
		QList<QPointer<PageItem>> items;      //!< top-level items
		QPointer<PageItem> body;
		QList<QPointer<PageItem>> heads;      //!< separate headline / kicker frames above the body
		QList<QPointer<PageItem>> photos;
		QList<QPointer<PageItem>> captions;   //!< parallel to photos (may hold nullptr)
		QRectF rect;                          //!< page relative
		QString title;
	};
	//! The modules of a page; \a master gets the master-page items (always fixed).
	SCRIBUS_API QList<Module> detectModules(ScribusDoc* doc, int pageIndex, const StyleNames& styles);
	//! Writes layout.module / layout.role / layout.fixed on the items, so the next detection is instant.
	SCRIBUS_API void rememberModules(const QList<Module>& modules);
	//! Clears the layout.* attributes of the page's items (detect again from scratch).
	SCRIBUS_API void forgetModules(ScribusDoc* doc, int pageIndex);
	//! A new module id.
	SCRIBUS_API QString newModuleId();

	struct PageIn
	{
		ScribusDoc* doc { nullptr };    //!< styles, colours and the kept items (may be null: default styles)
		int pageIndex { -1 };
		double pageW { 0 }, pageH { 0 };
		double mTop { 0 }, mBottom { 0 }, mLeft { 0 }, mRight { 0 };   //!< the page's margins
		int guideCols { 0 };            //!< columns of the page's automatic column guides (0: none)
		struct Kept { QRectF rect; QList<QPointer<PageItem>> items; bool master { false }; QPointF masterOffset; bool obstacle { true }; };
		QList<Kept> always;             //!< frames that are not news stories (ads, masthead, folio, master items)
		QList<StoryIn> stories;         //!< placed ones first, then the new ones
		QString pageKey;
		StyleNames styles;
		QString note;                   //!< where the stories came from (for the dialogs)
		QRectF area;                    //!< "Apply to selection only": the area the selected modules may use (page relative); null = the page
		int detected { 0 };             //!< modules found by detection (not News Browser, not remembered)
		int looseRules { 0 };           //!< rules of the old arrangement: left where they are, but they do not block the new one
		QList<Module> modules;          //!< every module of the page (for the overlay / remember)
	};

	struct ModuleOut
	{
		int story { -1 };
		QRectF rect;
		int cols { 1 };
		int photoCols { 0 };
		bool lead { false };
		bool kept { false };            //!< a placed module that stays where it is
		QImage image;                   //!< rendered at Measurer::scale() px per pt
		QList<QRectF> photos;           //!< module relative
	};

	struct PlanOut
	{
		QString modelId;
		QRectF content;
		QList<double> colX;
		double colW { 0 };
		QList<ModuleOut> modules;
		QList<int> notFit;              //!< story indexes
		QList<int> keptStories;
		int total { 0 };
		int obits { 0 }, news { 0 };    //!< Obituary engine: obit modules / 2-column news among the arranged stories
		//! suneer 2026-10-26, Obituary engine: the 2-column stories (workflow count 2) that had no room in the
		//! reserved news columns (they are never moved into the obit columns; also in notFit), the band
		//! stories placed, and the two areas as 1-based grid columns for the messages
		QList<int> notFitBand;
		int bandPlaced { 0 };
		int bandFirst { 0 }, bandLast { 0 }, areaFirst { 0 }, areaLast { 0 };
		double freePt { 0 };
		double rulePt { 0 };
		QString ruleColor;
		QString fitLine() const;        //!< "24 of 34 fit · 38 pt free"
		bool allFit() const { return notFit.isEmpty(); }
	};

	//! The model's grid on a page (page relative): content area, columns, column width, gutter.
	struct Grid
	{
		QRectF content;
		int cols { 1 };
		double colW { 0 }, gutter { 0 };
		double x(int col) const { return content.left() + col * (colW + gutter); }
		double width(int span) const { return span * colW + (span - 1) * gutter; }
	};
	/*! suneer 2026-11-01: THE column packer - the one slot search of Place selected (obit cells, the band news,
	    the news in the obit columns, its re-pack pass), the Layout Gallery / Re-layout job and Re-pack Page.
	    A module goes to the first free y (>= yMin) where its rect meets nothing in \a occupied, with \a gapV
	    under whatever it follows; the candidates are yMin and every obstacle bottom + gapV (so a hole higher
	    up is taken when the module fits there). Touching within 0.5 pt is not overlapping. */
	class SCRIBUS_API ColumnPacker
	{
	public:
		QRectF content;                 //!< the content area (top / bottom limits of a slot)
		QList<double> colX;             //!< left edge of every grid column (0-based index)
		double colW { 0 }, gutter { 0 }, gapV { 0 };
		QList<QRectF> occupied;         //!< everything placed or fixed, in the same coordinates as colX
		double tol { 0.5 };

		QRectF rectAt(int c0, int k, double y, double h) const { return QRectF(colX[c0], y, k * colW + (k - 1) * gutter, h); }
		double width(int k) const { return k * colW + (k - 1) * gutter; }
		//! the rect meets nothing in \a occupied (+ \a extra) and stays inside the content area's top / bottom
		bool free(const QRectF& r, const QList<QRectF>* extra = nullptr) const;
		//! first free y in [yMin, yMax - h] for a rect of \a w at \a x, -1 when nowhere
		double lowestAt(double x, double w, double h, double yMin, double yMax, const QList<QRectF>* extra = nullptr) const;
		//! first free y for \a k neighbouring columns from column \a c0 (0-based)
		double lowest(int c0, int k, double h, double yMin, double yMax) const { return (c0 < 0 || c0 + k > colX.size()) ? -1 : lowestAt(colX[c0], width(k), h, yMin, yMax); }
		//! the slot of \a k neighbouring columns among \a allowed (0-based, ascending) that starts highest: its first column, -1 when none; \a yOut its y
		int best(const QList<int>& allowed, int k, double h, double yMin, double yMax, double* yOut) const;
		//! the primitive without a packer: first free y from \a yMin for the rect against \a list with \a gap (1e9 when nowhere before yMax)
		static double firstFree(double x, double w, double h, double yMin, double yMax, const QList<QRectF>& list, double gap, double tol = 0.5);
	};
	SCRIBUS_API Grid gridFor(const SuneerLayoutModel& model, double pageW, double pageH, double mTop, double mBottom, double mLeft, double mRight, int guideCols);
	SCRIBUS_API Grid gridFor(const SuneerLayoutModel& model, const PageIn& page);

	/*! An obituary page: the page type is the Obituary model's, or the modules on it are
	    obituary modules (at least 6, and at least half of the placed stories). */
	SCRIBUS_API bool isObitPage(const PageIn& page);
	//! suneer 2026-11-01: the workflow page type of \a doc ("Open page from workflow": SuneerWfPageType, else the page name); empty = not a workflow page
	SCRIBUS_API QString workflowPageType(ScribusDoc* doc);
	//! \a doc is a workflow page whose type / name says obituary (obit, ചരമ, or a type the Obituary model applies to)
	SCRIBUS_API bool workflowPageIsObituary(ScribusDoc* doc);
	//! A model made for obituary pages: the Obituary engine, or one whose page types include the Obituary type.
	SCRIBUS_API bool isObitModel(const SuneerLayoutModel& model);
	/*! A "news" model on an obituary page: the same model with the Obituary model's grid
	    (columns, gutter = horizontal gap) and vertical module gap, no decks / rails, and
	    obitPacking set, so the modules are packed in columns (masonry, reading order,
	    shortest column first) with no row alignment. An Obituary model is returned as it is. */
	SCRIBUS_API SuneerLayoutModel obitVariant(const SuneerLayoutModel& model, const PageIn& page);
	/*! Gap check of placed modules (page relative rects): the largest vertical gap between a
	    module and the nearest module above it that overlaps it horizontally;  over counts
	    the gaps larger than  gapV + 0.5 pt;  worst gets the index of the module below. */
	SCRIBUS_API double maxGapAbove(const QList<QRectF>& rects, double gapV, int* over = nullptr, int* worst = nullptr);

	/*!
	 \brief The head of a placed story text: the position of its first body
	 paragraph (-1: none) and of its headline paragraph (\a headlinePos, -1: none).
	 Head paragraphs are the leading ones not in a body / byline / dateline /
	 highlights / caption style; the last non-kicker one is the headline.
	 */
	SCRIBUS_API int storyHead(const StoryText& text, const StyleNames& styles, int* headlinePos);

	//! A headline style the News Browser or a layout model sets ("44 M", "80 B", the panel's title style):
	//! only such a headline is restyled by a model; anything else is the user's and stays.
	SCRIBUS_API bool isModelHeadlineStyle(const QString& name, const StyleNames& styles);

	//! The scratch document and the measurement cache.
	class SCRIBUS_API Measurer
	{
	public:
		//! \a scale <= 0: measure only (no pictures; used when the page is really arranged).
		Measurer(const PageIn& page, double scale);
		~Measurer();
		Measurer(const Measurer&) = delete;
		Measurer& operator=(const Measurer&) = delete;

		struct Spec
		{
			double width { 0 };         //!< module width
			int cols { 1 };             //!< text columns
			double colGap { 0 };        //!< between text columns
			double inset { 0 };
			QString headStyle;          //!< headline paragraph style (empty: keep)
			double headExtra { 0 };     //!< headline -> body
			double kickerExtra { 0 };   //!< kicker / subheading -> headline
			QString photoPos;           //!< top, topAbove, right, left, middle, bottom, none
			int photoCols { 0 };
			double gridColW { 0 }, gutter { 0 };
			double wrap { 0 }, captionGap { 0 }, captionText { 0 }, photoText { 0 };
			double maxPhotoH { 0 };
			double maxH { 2000 };
			bool obit { false };        //!< Obituary module: photo on top, obit styles
			QString obitName, obitPlace, obitBody;   //!< styles (obit)
			double obitPhotoH { 0 };
			QString obitAlign;
			QString key() const;
		};
		struct Result
		{
			double h { 0 };
			double w { 0 };             //!< > 0: a rigid module keeps this width
			double headH { 0 };
			bool overflow { false };    //!< did not fit in maxH
			QList<QRectF> photos;
			QImage image;
		};
		const Result& measure(const StoryIn& s, const Spec& spec);
		bool cached(const StoryIn& s, const Spec& spec) const;
		double scale() const { return m_scale; }
		//! Draws the real document items \a items (page relative \a origin) into \a p's image.
		static void renderItems(QImage& img, double scale, const QList<QPointer<PageItem>>& items, const QPointF& origin);
		//! A picture scaled for the thumbnails (cached).
		QImage picture(const QString& path, const QSize& size);
		bool hasMalayalamFont() const { return !m_defaultFont.isEmpty(); }

	private:
		PageItem_TextFrame* newFrame();
		Result measureBody(const StoryIn& s, const Spec& spec);
		double measureHeads(const StoryIn& s, double width, QImage* image);
		void fillText(PageItem_TextFrame* t, const StoryIn& s, const Spec& spec, int* firstBodyPos);
		double fitHeight(PageItem_TextFrame* t, double lo, double hi, bool* overflow);
		void render(Result& r, PageItem_TextFrame* text, PageItem_TextFrame* caption, const QList<PhotoIn>& photos, const QList<QRectF>& captionRects);

		ScribusDoc* m_doc { nullptr };
		ScribusDoc* m_src { nullptr };
		PageItem_TextFrame* m_text { nullptr };
		QList<PageItem_TextFrame*> m_obstacles;   //!< photo / caption wrap frames
		QList<PageItem_TextFrame*> m_captions;
		QList<PageItem_TextFrame*> m_headFrames;
		QPointF m_origin;            //!< the page's top-left in the source document
		StyleNames m_styles;
		QString m_defaultFont;
		double m_scale { 0.5 };
		QHash<QString, Result> m_cache;
		QHash<QString, QImage> m_pictures;
	};

	//! Arranges one model on one page, a step at a time.
	class SCRIBUS_API Job
	{
	public:
		Job(const SuneerLayoutModel& model, const PageIn* page, Measurer* measurer, Keep keep);
		//! One measurement (or the packing at the end); false when finished.
		bool step();
		bool finished() const { return m_finished; }
		int done() const { return m_done; }
		int total() const { return m_todo.size(); }
		const PlanOut& plan() const { return m_plan; }
		const SuneerLayoutModel& model() const { return m_model; }
		int tag { 0 };

	private:
		//! obitKind (Obituary engine): 1 = obit module (obit columns), 2 = 2-column story (the reserved news
		//! columns only), 3 = any other news (obit columns only, at its width); 0 = not the Obituary engine
		struct Todo { int story; Measurer::Spec spec; bool lead; int cols; int photoCols; int obitKind; };
		void prepare();
		void pack();
		void packObituary();
		Measurer::Spec specFor(const StoryIn& s, int cols, bool lead) const;

		SuneerLayoutModel m_model;
		const PageIn* m_page;
		Measurer* m_meas;
		Keep m_keep;
		QList<Todo> m_todo;
		int m_done { 0 };
		bool m_prepared { false };
		bool m_finished { false };
		PlanOut m_plan;
		// geometry
		QRectF m_content;
		double m_colW { 0 }, m_gutter { 0 };
		int m_cols { 1 };
		QList<QRectF> m_blocked;     //!< kept frames + placed modules
		int m_leadStory { -1 };
	};

	//! Runs jobs round robin on the event loop (about 25 ms per slice).
	class SCRIBUS_API Runner : public QObject
	{
		Q_OBJECT
	public:
		explicit Runner(QObject* parent = nullptr);
		~Runner() override;
		void add(Job* job);          //!< takes ownership
		void clear();
		bool busy() const;
	signals:
		void progress(SuneerLayout::Job* job);
		void finished(SuneerLayout::Job* job);
		void idle();
	private:
		void tick();
		QList<Job*> m_jobs;
		QTimer* m_timer { nullptr };
		int m_next { 0 };
	};

	//! SUNEER_LAYOUT_DEBUG=1: the model, its key values and every module rectangle of \a plan on stderr ("LAYOUT-PLAN ...").
	SCRIBUS_API bool debugOn();
	SCRIBUS_API void debugPlan(const char* who, const SuneerLayoutModel& model, const PageIn& page, const PlanOut& plan);

	/*!
	 \brief A copy of one page in a hidden document, for the Layout Gallery cards: the page's
	 items pasted with Scribus' own copy / paste (one batch per layer), with the document's
	 styles, colours, layers, guides and text settings. The real page's master items (masthead,
	 folio) are not copied; collectPage() gives them to the copy as the same fixed areas.
	 The arrangement runs on it with the very code Apply runs on the real page.
	 */
	struct SCRIBUS_API ScratchPage
	{
		ScribusDoc* doc { nullptr };
		QList<PageItem*> source;          //!< the real page's top-level items, in document order
		QList<PageItem*> copies;          //!< parallel to source
		QList<PageIn::Kept> masters;      //!< the real page's master items (fixed areas, drawn from the real document)
		QString error;
		qint64 ms { 0 };
		ScratchPage() = default;
		ScratchPage(const ScratchPage&) = delete;
		ScratchPage& operator=(const ScratchPage&) = delete;
		~ScratchPage();
	};
	SCRIBUS_API std::unique_ptr<ScratchPage> makeScratchPage(ScribusDoc* src, int pageIndex);
	//! The top-level items on a page (what detectModules looks at), in document order.
	SCRIBUS_API QList<PageItem*> pageTopItems(ScribusDoc* doc, int pageIndex);
	//! The master items of a page as fixed areas (page relative).
	SCRIBUS_API QList<PageIn::Kept> masterKept(ScribusDoc* doc, int pageIndex);
	//! Changes whenever an item on the page, its text, a picture or a paragraph style changes (Gallery cache).
	SCRIBUS_API QString pageStateKey(ScribusDoc* doc, int pageIndex);
	/*!
	 \brief Where \a items are now: per item its page relative rectangle (0.5 pt), "off" when it is
	 not on the page, then the items on the page that are not in the list (new stories), sorted.
	 Equal lists = the same arrangement (card against card, card against Apply).
	 */
	SCRIBUS_API QStringList resultSignature(ScribusDoc* doc, int pageIndex, const QList<PageItem*>& items);
	//! How many entries of two signatures differ.
	SCRIBUS_API int signatureDiff(const QStringList& a, const QStringList& b);
	//! A page of \a doc as a picture \a widthPx wide: \a masters, then every item on the page in layer order.
	SCRIBUS_API QImage renderDocPage(ScribusDoc* doc, int pageIndex, const QList<PageIn::Kept>& masters, int widthPx);
	/*!
	 \brief The free space left in \a content (page relative) by the items on the page and \a masters,
	 as full-width height in pt (the measure the fit line uses); \a overflow gets the text frames
	 on the page whose text does not fit.
	 */
	SCRIBUS_API double freeSpacePt(ScribusDoc* doc, int pageIndex, const QList<PageIn::Kept>& masters, const QRectF& content, const QList<double>& colX, double colW, double gapV, int* overflow);

	/*!
	 \brief What would make an arrangement unsafe or meaningless, checked before anything is changed:
	 the model's grid on this page (columns, column width, content area), its gaps and photo height,
	 the Obituary engine's part, and every module (its frames still exist, belong to this document, have
	 a finite size). Empty: fine. Apply shows the list instead of arranging.
	 */
	SCRIBUS_API QStringList checkArrange(const SuneerLayoutModel& model, const PageIn& page);
	SCRIBUS_API QStringList checkModel(const SuneerLayoutModel& model, double pageW, double pageH, double mTop, double mBottom, double mLeft, double mRight, int guideCols);

	//! The arranged page as a picture \a widthPx wide (kept frames, modules, rules).
	SCRIBUS_API QImage renderPlan(const PlanOut& plan, const PageIn& page, int widthPx, double moduleScale, const QImage& keptLayer = QImage());
	//! The kept frames (ads, masthead, master items) rendered once, page sized at \a scale px per pt.
	SCRIBUS_API QImage renderKept(const PageIn& page, double scale, Keep keep);

	/*!
	 \brief The page as it is: placed News Browser stories (grouped by news.story),
	 every other frame on it and on its master page (kept), then \a newStories.
	 */
	SCRIBUS_API PageIn collectPage(ScribusDoc* doc, int pageIndex, const QList<StoryIn>& newStories, const StyleNames& styles, const QString& pageKey,
	                               const QList<PageItem*>* selection = nullptr);
	//! The whole page as it is now (every item), page sized, at \a scale px per pt ("Compare").
	SCRIBUS_API QImage renderPageNow(const PageIn& page, double scale);

	//! Sample stories (Malayalam) for the Model Editor's preview page.
	SCRIBUS_API QList<StoryIn> sampleStories();
	//! Sample obituaries (name, place, a short notice, a photo) and three short news, for the Obituary engine.
	SCRIBUS_API QList<StoryIn> sampleObituaries();
	//! A page for the sample stories: the current document page when there is one, else a broadsheet 350 x 538 mm.
	SCRIBUS_API PageIn samplePage(ScribusDoc* doc);
}

#endif
