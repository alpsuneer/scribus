// DESH: Paragraph styles dock panel for Deshabhimani. Lists document styles with
// search/filter, applies them to selected text, and triggers auto-fit after apply. See FEATURES.md section 4.
#ifndef PARAGRAPHSTYLESPANEL_H
#define PARAGRAPHSTYLESPANEL_H
#include <QPointer>
#include <QDockWidget>
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QSpinBox>
#include <QDoubleSpinBox>
class ScribusDoc;
class ScribusMainWindow;
class PageItem_TextFrame;
class QShortcut;
class QCheckBox;

class ParagraphStylesPanel : public QDockWidget
{
	Q_OBJECT
public:
	explicit ParagraphStylesPanel(QWidget* parent = nullptr);
	bool eventFilter(QObject* obj, QEvent* event) override;
	void changeEvent(QEvent* event) override;
	
	void setDocument(ScribusDoc* doc);
	void setMainWindow(ScribusMainWindow* mw);

	// Shortcuts owned by this panel (paragraph-style chains and column configs) are plain
	// QShortcuts, so they never appear in the ScrAction keymap the Keyboard Shortcuts
	// preferences page checks against. Expose them so that page can still detect conflicts.
	// Returns a human-readable owner label -> key sequence map; empty sequences are omitted.
	static QMap<QString, QKeySequence> dynamicShortcuts();

	//! \brief Append a tab after the built-in Styles and Design Style tabs.
	//! Used for the News Browser, which is optional and owned elsewhere: the
	//! widget is only reparented in, never taken ownership of beyond Qt's
	//! normal parenting, and removeExtraTab() puts it back where it was.
	void addExtraTab(QWidget* page, const QString& label);
	void removeExtraTab(QWidget* page);
	bool hasExtraTab(QWidget* page) const;
	//! \brief Bring an extra tab to the front. Returns false if absent.
	bool showExtraTab(QWidget* page);
public slots:
	void updateStylesList();
	void applyChainCurrentStyle();
	void rebuildShortcuts();
	void openColumnConfig();
	void refreshColTabList();
	void colTabAdd();
	void colTabEdit();
	void colTabClone();
	void colTabDelete();
	void openDesignStyleSettings();
	void refreshDesignIcons();
	void applyDesignStyle(const QStringList& styles, const QString& imgPos, int cols, const QString& colBreak, double savedImgOffX=0, double savedImgOffY=0, double savedImgW=0, double savedImgH=0);
	void applyColumnConfig(int configIndex);
	void rebuildColumnShortcuts();
	void syncCurrentStyle();
private slots:
	void filterStyles(const QString& filter);
	void applyStyle();
	void setNextStyle();
	void editStyle();
	void newStyle();
	void deleteStyle();
	//! \brief "Assign Shortcut..." for the current row: the style's own key,
	//! stored in ParagraphStyle::shortcut() (the .sla / Style Manager field).
	void assignShortcut();
	void showStylesContextMenu(const QPoint& pos);
	void exportStyleShortcuts();
	void importStyleShortcuts();
	void toggleTemplateOnly(bool checked);
	void openTemplateSourceSettings();
	void cleanupImportedStyles();
private:
	void applySingleParagraph(PageItem_TextFrame* textFrame, const QString& styleName);
	void applySequentialStyles(PageItem_TextFrame* textFrame, const QString& startStyleName);
	void applyStyleByName(const QString& styleName);
	void applyChainFromStyle(const QString& startStyle);
	void setupShortcuts();
	void clearShortcuts();
	//! \brief Write \a key into the named paragraph style's shortcut field and
	//! refresh everything that shows or listens for it. Empty key clears.
	void setStyleShortcut(const QString& styleName, const QKeySequence& key);
	//! \brief Ask about anything else already holding \a key (another style,
	//! a chain, a column config, or an action in the active shortcut set).
	//! Returns false if the user cancelled; on "Replace" the other owner is cleared.
	bool resolveShortcutConflict(const QKeySequence& key, const QString& forStyle);
	void rebuildStyleShortcuts();
	//! \brief Apply Design Style number \a index from the SuneerDesignStyle
	//! settings to the selected frame. \a withColumns false skips its own
	//! "columns" config step (used when a column config is what applies it).
	bool applyDesignStyleByIndex(int index, bool withColumns);
	//! \brief Re-applies the search text and (if enabled) the template-styles-only
	//! filter to the already-populated list. Call after any rebuild or after either
	//! filter's state changes.
	void applyListFilters();

	QPointer<ScribusDoc> m_doc;
	ScribusMainWindow* m_mainWindow;
	
	QListWidget* m_stylesList;
	QTabWidget* m_tabWidget {nullptr};
	QLineEdit* m_searchBox;
	QCheckBox* m_templateOnlyCheck {nullptr};
	QGridLayout* m_iconsGrid = nullptr;
	QWidget*     m_designTab  = nullptr;
	QListWidget* m_colList    = nullptr;
	QPushButton* m_newButton;
	QPushButton* m_deleteButton;
	QPushButton* m_editButton;
	
	QList<QShortcut*> m_shortcuts;
	QList<QShortcut*> m_columnShortcuts;
	QList<QShortcut*> m_styleShortcuts; // per-style keys from ParagraphStyle::shortcut()
	QPushButton* m_shortcutButton {nullptr};
	QMap<QString, QString> m_nextStyles; // styleName → nextStyleName
	QTimer* m_syncTimer = nullptr;
	QString m_lastHighlightedStyle;
	bool m_userInteracting = false; // true while pointer is pressed on the styles list
	bool m_inColumnConfig = false;      // applyColumnConfig() running (owns the undo transaction)
	bool m_applyingDesignStyle = false; // applyDesignStyle() running (breaks config<->design loops)
};
#endif
