/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERDUPLICATENEWSDIALOG_H
#define SUNEERDUPLICATENEWSDIALOG_H

#include <QColor>
#include <QDialog>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSet>

#include <memory>

#include "scribusapi.h"
#include "suneerduplicatenews.h"

class Canvas;
class PageItem;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPainter;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class ScribusDoc;
class ScribusMainWindow;

/*! Suneer: the Duplicate News Checker's marks on the canvas. Canvas::paintEvent
 *  draws them on the widget, over the page buffer, so they never reach print,
 *  PDF, export or page thumbnails. */
class SCRIBUS_API SuneerDupHighlights
{
public:
	struct Mark
	{
		QPointer<PageItem> item;
		QColor color;
		int group { 0 };                 //!< 1-based number shown on the frame's badge
		QList<QPair<int, int>> ranges;   //!< story positions [first, end) to tint
	};

	static void setMarks(ScribusDoc* doc, const QList<Mark>& marks);
	static void clearAll();
	static bool isEmpty();
	static void paint(QPainter* painter, const Canvas* canvas, ScribusDoc* doc);
};

class SuneerDuplicateNewsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit SuneerDuplicateNewsDialog(ScribusMainWindow* mainWindow);
	~SuneerDuplicateNewsDialog() override;

protected:
	void closeEvent(QCloseEvent* event) override;
	void showEvent(QShowEvent* event) override;

public slots:
	void reject() override;

private slots:
	void startCheck();
	void cancelCheck();
	void checkFinished();
	void pollProgress();
	void itemClicked(QTreeWidgetItem* item, int column);
	void toggleHighlights();
	void browseFolder();
	void scopeChanged();
	void showContextMenu(const QPoint& pos);
	void forgetIgnored();

private:
	enum Scope { ScopePage = 0, ScopeDocument, ScopeOpenDocuments, ScopeFolder };

	//! Frames of a story that is in an open document, and the story as analysed
	//! there (its normalised-to-raw maps place the highlights).
	struct LiveStory
	{
		QPointer<ScribusDoc> doc;
		QList<QPointer<PageItem>> frames;
		DupNews::Story analysed;
	};

	QList<ScribusDoc*> openDocuments() const;
	ScribusDoc* openDocumentFor(const QString& filePath) const;
	void collectDocument(ScribusDoc* doc, int source, int onlyPage, QList<DupNews::Story>& stories, QList<LiveStory>& live) const;
	bool resolveSource(int source);
	void fillResults();
	void applyMarks();
	void clearMarks();
	void jumpTo(int story);
	bool activateDocument(ScribusDoc* doc);
	QString whereText(const DupNews::Story& story) const;
	void setRunning(bool running);
	void saveSettings() const;
	void updateIgnoredLabel();

	ScribusMainWindow* m_mainWindow { nullptr };
	QComboBox* m_scope { nullptr };
	QLineEdit* m_folder { nullptr };
	QPushButton* m_browse { nullptr };
	QSpinBox* m_threshold { nullptr };
	QCheckBox* m_pasteboard { nullptr };
	QCheckBox* m_captions { nullptr };
	QPushButton* m_check { nullptr };
	QPushButton* m_cancel { nullptr };
	QProgressBar* m_progressBar { nullptr };
	QLabel* m_status { nullptr };
	QTreeWidget* m_tree { nullptr };
	QCheckBox* m_pin { nullptr };
	QPushButton* m_highlightButton { nullptr };
	QLabel* m_ignoredLabel { nullptr };
	QPushButton* m_forgetIgnored { nullptr };
	QTimer* m_pollTimer { nullptr };

	QFutureWatcher<DupNews::Result> m_watcher;
	std::shared_ptr<DupNews::Progress> m_progress;
	QHash<int, LiveStory> m_pendingLive; //!< live stories of the running check
	QHash<int, LiveStory> m_live;        //!< result story index -> where it is open
	DupNews::Result m_result;
	QSet<int> m_hiddenGroups;
	QSet<QByteArray> m_ignored;
	bool m_marksShown { false };
	bool m_haveResult { false };
};

#endif
