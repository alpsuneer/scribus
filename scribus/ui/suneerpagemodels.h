/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPAGEMODELS_H
#define SUNEERPAGEMODELS_H

#include <functional>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "scribusapi.h"
#include "suneerpagemodel.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProcess;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTableWidget;
class ScribusDoc;

/*!
 \brief Layout > Page Models...: the page groups (Page-1 ... Obituary, Sports ...) on the
 left, the group's page models as large cards on the right; create models from old
 pages, rename / move / duplicate / delete / export / import them, apply one to the page.
 */
class SCRIBUS_API SuneerPageModelGallery : public QDialog
{
	Q_OBJECT
public:
	struct Source
	{
		ScribusDoc* doc { nullptr };
		int pageIndex { -1 };
		QString docFile;                     //!< the document's file name (group suggestion)
		QString savedGroup;                  //!< the page's saved group attribute
		QList<QRectF> pageAds;               //!< the ads on the page now (mm, page relative) - "fits current ads"
		//! The model's skeleton on the current page; returns the report (empty: cancelled). Absent: no page.
		std::function<QString(const SuneerPageModel& model, const SuneerPageGroup& group)> apply;
		//! Layout > Remove Empty Slots on the current page; returns how many went.
		std::function<int()> removeEmptySlots;
	};
	SuneerPageModelGallery(const Source& src, QWidget* parent = nullptr);
	~SuneerPageModelGallery() override;
	QString currentGroup() const { return m_group; }
	//! Does the model's ad space match these ads (same count, each within 8 mm / 20 % area)?
	static bool adsMatch(const SuneerPageModel& m, const QList<QRectF>& pageAds, QString* why = nullptr);

private:
	struct Card
	{
		QString id;
		QFrame* frame { nullptr };
		QLabel* thumb { nullptr };
		QLabel* name { nullptr };
		QLabel* info { nullptr };
		QPushButton* apply { nullptr };
		SuneerPageModel model;
		bool fits { true };
	};
	void fillGroups(const QString& select);
	void selectGroup(const QString& id);
	void rebuildCards();
	void cardMenu(const Card& c, const QPoint& globalPos);
	void applyModel(const Card& c);
	void createModels();
	void exportModels();
	void importModels();
	void groupAdd();
	void groupRename();
	void groupEdit();
	void groupDelete();
	void groupMove(int delta);
	void updateGroupButtons();
	QList<QRectF> m_pageAds;
	Source m_src;
	QString m_group;
	QListWidget* m_groups { nullptr };
	QLabel* m_groupNote { nullptr };
	QCheckBox* m_fitAds { nullptr };
	QLabel* m_info { nullptr };
	QWidget* m_cardsHost { nullptr };
	QGridLayout* m_cardsGrid { nullptr };
	QScrollArea* m_scroll { nullptr };
	QList<Card> m_cards;
	QPushButton* m_gUp { nullptr };
	QPushButton* m_gDown { nullptr };
	QPushButton* m_gDel { nullptr };
	QPushButton* m_gRen { nullptr };
	QPushButton* m_gEdit { nullptr };
};

/*!
 \brief "Create Page Model...": pick old .sla files (or a folder), the pages are read by
 tools/autolayout/page_models.py (a separate process; the files are only read), then a
 review list - thumbnail | file / page | group | name | import - and one import run with a
 progress bar and the duplicate check.
 */
class SCRIBUS_API SuneerPageModelImportDialog : public QDialog
{
	Q_OBJECT
public:
	SuneerPageModelImportDialog(const QString& defaultGroup, QWidget* parent = nullptr);
	~SuneerPageModelImportDialog() override;
	int imported() const { return m_imported; }
	QStringList importedGroups() const { return m_importedGroups; }
	//! Pre-set files (tests); scan() runs them.
	void setFiles(const QStringList& files);
	void scan();
	//! Tests: waits until the reader has finished (false on timeout).
	bool waitForScan(int ms);
	//! Imports every ticked row; \a askDuplicates false: near-duplicates are skipped silently (tests).
	int importTicked(bool askDuplicates);
	struct Row
	{
		QString file;
		int page { 0 }, pages { 1 };
		SuneerPageModel model;
		QString error;
		QString suggested, why;
		QCheckBox* tick { nullptr };
		QComboBox* group { nullptr };
		QLineEdit* name { nullptr };
	};
	const QList<Row>& rows() const { return m_rows; }

private:
	void addFiles();
	void addFolder();
	void removeSelected();
	void onOutput();
	void onFinished(int code);
	void buildTable();
	void setBusy(bool on);
	QString m_defaultGroup;
	QStringList m_files;
	QListWidget* m_fileList { nullptr };
	QPushButton* m_scanBtn { nullptr };
	QPushButton* m_importBtn { nullptr };
	QPushButton* m_addBtn { nullptr };
	QPushButton* m_folderBtn { nullptr };
	QPushButton* m_removeBtn { nullptr };
	QProgressBar* m_progress { nullptr };
	QLabel* m_status { nullptr };
	QTableWidget* m_table { nullptr };
	QProcess* m_proc { nullptr };
	QByteArray m_stdoutBuf;
	QString m_stderrTail;
	QList<Row> m_rows;
	int m_imported { 0 };
	QStringList m_importedGroups;
};

/*!
 \brief Group editor: name, note, grid type (normal / obituary).
 */
class SCRIBUS_API SuneerPageGroupDialog : public QDialog
{
	Q_OBJECT
public:
	SuneerPageGroupDialog(const SuneerPageGroup& g, bool isNew, QWidget* parent = nullptr);
	SuneerPageGroup result() const;
private:
	SuneerPageGroup m_g;
	QLineEdit* m_name { nullptr };
	QLineEdit* m_note { nullptr };
	QComboBox* m_grid { nullptr };
};

#endif
