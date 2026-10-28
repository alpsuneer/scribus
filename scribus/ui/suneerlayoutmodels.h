/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERLAYOUTMODELS_H
#define SUNEERLAYOUTMODELS_H

#include <functional>
#include <memory>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QWidget>
#include <QJsonObject>
#include <QPixmap>

#include "scribusapi.h"
#include "suneerlayoutengine.h"
#include "suneerlayoutmodel.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QGridLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTimer;
class ScribusDoc;
class PageItem;

/*!
 \brief One sample module drawn as a diagram: headline, body columns, photo
 with caption, the neighbouring modules; every gap is a dimension line with
 its value field next to it (mm, step 0.1). Gaps are drawn larger than life
 (but grow with the value) so every one stays visible and clickable.
 */
class SCRIBUS_API SuneerSpacingDiagram : public QWidget
{
	Q_OBJECT
public:
	explicit SuneerSpacingDiagram(QWidget* parent = nullptr);
	void setValues(const SuneerSpacing::Values& v, const QStringList& colours);
	SuneerSpacing::Values values() const { return m_v; }
	QSize sizeHint() const override { return QSize(560, 470); }
	QSize minimumSizeHint() const override { return QSize(480, 420); }
signals:
	void changed();
protected:
	void paintEvent(QPaintEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
private:
	struct Geo
	{
		QRectF neighbour, module, head, photo, caption, below, insetRect;
		QList<QRectF> cols;
		QHash<int, QLineF> dims;        // key -> dimension line
	};
	Geo geometry() const;
	void placeEditors();
	SuneerSpacing::Values m_v {};
	QHash<int, QDoubleSpinBox*> m_spin;
	QComboBox* m_ruleWidth { nullptr };
	QComboBox* m_ruleColour { nullptr };
	bool m_loading { false };
};

//! Layout > Layout Models...: list of models, the model's settings, the spacing diagram and a live sample page.
class SCRIBUS_API SuneerLayoutModelEditor : public QDialog
{
	Q_OBJECT
public:
	struct PageHooks
	{
		std::function<QString()> pageModel;                                     //!< the model id the page uses
		std::function<void()> snapshot;                                         //!< remember the page
		std::function<void()> restore;                                          //!< put the page back
		std::function<int(const SuneerSpacing::Values&, bool final)> apply;     //!< re-space; final = one undo step + rules
	};
	SuneerLayoutModelEditor(ScribusDoc* doc, const QString& selectId, const PageHooks& hooks = PageHooks(), QWidget* parent = nullptr);
	~SuneerLayoutModelEditor() override;
	QString currentId() const { return m_id; }

signals:
	//! The model as edited (not saved yet), after every change: the Layout Gallery redraws its card.
	void edited(const SuneerLayoutModel& model);

protected:
	void reject() override;
	void keyPressEvent(QKeyEvent* e) override;

private:
	void fillList(const QString& select);
	void selectModel(const QString& id);
	void loadUi(const SuneerLayoutModel& m);
	SuneerLayoutModel fromUi() const;
	bool maybeSave();
	bool saveCurrent();
	void markDirty();
	void schedulePreview();
	void runPreview();
	void onNew();
	void onDuplicate();
	void onRename();
	void onDelete();
	void onExport();
	void onImport();
	void onRestore();
	void applyToPage();

	ScribusDoc* m_doc { nullptr };
	PageHooks m_hooks;
	QString m_id;
	SuneerLayoutModel m_model;      //!< as loaded (engine, origin, obituary part)
	bool m_dirty { false };
	bool m_loading { false };
	bool m_previewing { false };

	QListWidget* m_list { nullptr };
	QPushButton* m_saveBtn { nullptr };
	QPushButton* m_deleteBtn { nullptr };
	QPushButton* m_restoreBtn { nullptr };
	QLabel* m_title { nullptr };
	QTabWidget* m_tabs { nullptr };
	// grid / structure
	QSpinBox* m_columns { nullptr };
	QCheckBox* m_fromGuides { nullptr };
	QDoubleSpinBox* m_colWidth { nullptr };
	QLabel* m_colWidthInfo { nullptr };
	QDoubleSpinBox* m_gutter { nullptr };
	QCheckBox* m_docMargins { nullptr };
	QDoubleSpinBox* m_margin[4] {};
	QSpinBox* m_decks { nullptr };
	QCheckBox* m_leftRail { nullptr };
	QCheckBox* m_rightRail { nullptr };
	QComboBox* m_leadPos { nullptr };
	QSpinBox* m_leadCols { nullptr };
	// stories / photos
	QSpinBox* m_fixedCols { nullptr };
	QTableWidget* m_widthRules { nullptr };
	QTableWidget* m_headRules { nullptr };
	QComboBox* m_leadHead { nullptr };
	QSpinBox* m_fixedPhotoCols { nullptr };
	QTableWidget* m_photoRules { nullptr };
	QComboBox* m_photoPos { nullptr };
	QDoubleSpinBox* m_photoMaxH { nullptr };
	// info
	QLineEdit* m_pageTypes { nullptr };
	QLineEdit* m_description { nullptr };
	QLabel* m_fileInfo { nullptr };
	// obituary
	QWidget* m_obitTab { nullptr };
	QWidget* m_storiesTab { nullptr };
	bool m_sampleObit { false };
	QDoubleSpinBox* m_obitPhoto { nullptr };
	QComboBox* m_obitShape { nullptr };
	QDoubleSpinBox* m_obitPortraitH { nullptr };
	QSpinBox* m_obitFaceW { nullptr };
	QSpinBox* m_obitFaceTop { nullptr };
	QComboBox* m_obitBg { nullptr };
	QComboBox* m_obitAlign { nullptr };
	QCheckBox* m_obitRunin { nullptr };
	QSpinBox* m_obitCols[4] {};
	// spacing + preview
	SuneerSpacingDiagram* m_diagram { nullptr };
	QHash<int, QDoubleSpinBox*> m_gapSpins;   //!< "Gaps" tab, per SuneerSpacing key (same values as the diagram)
	//! The Gaps tab from the diagram's values.
	void syncGapSpins();
	//! A Gaps tab value into the diagram (marks the model changed, live preview).
	void setGap(int key, double mm);
	QLabel* m_preview { nullptr };
	QLabel* m_previewInfo { nullptr };
	QCheckBox* m_liveChk { nullptr };
	QLabel* m_pageInfo { nullptr };
	QTimer* m_timer { nullptr };
	QTimer* m_liveTimer { nullptr };
	SuneerLayout::PageIn m_sample;
	std::unique_ptr<SuneerLayout::Measurer> m_measurer;
	SuneerLayout::Runner* m_runner { nullptr };
};

/*!
 \brief Layout Gallery: one card per model with the current page arranged by
 it (thumbnail, fit line, Apply). Thumbnails are measured and drawn in small
 steps on the event loop; the dialog stays usable meanwhile.
 */
class SCRIBUS_API SuneerLayoutGallery : public QDialog
{
	Q_OBJECT
public:
	//! One card: the page really arranged with the model in a scratch copy (SuneerNewsPanel::previewArrange).
	struct Preview
	{
		bool ok { false };
		QString error;
		QImage image;                                       //!< the arranged copy of the page
		QString fitLine;                                    //!< "7 of 9 fit · 120 pt free · 1 overflow"
		bool allFit { false };
		bool noChange { false };                            //!< the arrangement leaves the page as it is
		QStringList notFit;                                 //!< titles
		QStringList signature;                              //!< SuneerLayout::resultSignature of the copy
		QString report;                                     //!< what Apply would report
		qint64 ms { 0 };
	};
	struct Source
	{
		ScribusDoc* doc { nullptr };
		int pageIndex { -1 };
		QString pageKey;                                    //!< page type / name of the News Browser page
		QString titleStyle;                                 //!< the News Browser headline style
		std::function<QList<SuneerLayout::StoryIn>(QString* note)> newStories;   //!< not yet placed (News Browser)
		std::function<void(const QString& modelId)> applied;                    //!< after Apply
		//! Apply = arrange the page with the model (keep, with the new stories); returns the report. Empty: Apply only remembers the model.
		//! \a expect: the card's signature; the report says whether the page now matches it.
		std::function<QString(const QString& modelId, int keep, bool selectionOnly, const QStringList& expect)> apply;
		//! The card of a model: the page arranged in a scratch copy with the code Apply uses. Empty: estimated cards (SuneerLayout::Job).
		std::function<Preview(const QString& modelId, int keep, bool selectionOnly, int widthPx)> preview;
		//! The page's state (cache key of the cards).
		std::function<QString()> stateKey;
		QList<PageItem*> selection;                         //!< frames selected on the page ("Apply to selection only")
		std::function<void()> checkModules;                 //!< open Page Modules (the gallery closes)
		std::function<void()> openPageModels;               //!< "Page Models..." (whole-page models; the gallery closes)
	};
	SuneerLayoutGallery(const Source& src, QWidget* parent = nullptr);
	~SuneerLayoutGallery() override;

private:
	struct Card
	{
		QString id;
		QFrame* frame { nullptr };
		QLabel* thumb { nullptr };
		QLabel* fit { nullptr };
		QPushButton* apply { nullptr };
		QLabel* same { nullptr };                           //!< "same result as ..."
		SuneerLayout::Job* job { nullptr };
		bool pending { false };
		QStringList signature;
		QString name;
	};
	void startPreviews();
	void runNextPreview(int generation);
	void showPreview(Card& c, const Preview& p);
	void updateSameAs();
	void spin();
	QString cacheKey(const QString& modelId) const;
	void rebuild();
	void startJobs();
	void onJobProgress(SuneerLayout::Job* job);
	void onJobFinished(SuneerLayout::Job* job);
	void applyModel(const QString& id);
	SuneerLayout::Keep keep() const;
	//! "Generate from archive...": tools/autolayout/learn_models.py over a folder of our pages, in the background.
	void generateFromArchive();
	void loadPage();
	void showCompare(bool before);
	void updateInfo();
	static QString learnerScript();

	Source m_src;
	SuneerLayout::PageIn m_page;
	std::unique_ptr<SuneerLayout::Measurer> m_measurer;
	SuneerLayout::Runner* m_runner { nullptr };
	QImage m_keptLayer;
	QComboBox* m_typeCmb { nullptr };
	QComboBox* m_keepCmb { nullptr };
	QLabel* m_info { nullptr };
	QWidget* m_cardsHost { nullptr };
	QGridLayout* m_cardsGrid { nullptr };
	QScrollArea* m_scroll { nullptr };
	QList<Card> m_cards;
	QHash<QString, QPixmap> m_after;
	QImage m_before;
	class QCheckBox* m_selOnly { nullptr };
	class QCheckBox* m_compare { nullptr };
	class QCheckBox* m_obitOnly { nullptr };               //!< obituary pages: hide the models made for news pages
	QList<QJsonObject> m_freshJson;
	QString m_freshNote;
	QList<SuneerLayout::StoryIn> m_fresh;
	class QProcess* m_learner { nullptr };
	QPushButton* m_generateBtn { nullptr };
	QString m_learnStderr;
	// cards from scratch copies
	QTimer* m_spinTimer { nullptr };
	int m_spinFrame { 0 };
	int m_generation { 0 };
	QStringList m_queue;
	QString m_stateKey;
	QStringList m_baseline;                                 //!< the page as it is (signature)
};

#endif
