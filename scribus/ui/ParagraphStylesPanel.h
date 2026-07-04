// DESH: Paragraph styles dock panel for Deshabhimani. Lists document styles with
// search/filter, applies them to selected text, and triggers auto-fit after apply. See FEATURES.md section 4.
#ifndef PARAGRAPHSTYLESPANEL_H
#define PARAGRAPHSTYLESPANEL_H
#include <QPointer>
#include <QDockWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QSpinBox>
#include <QDoubleSpinBox>
class ScribusDoc;
class ScribusMainWindow;
class PageItem_TextFrame;
class QShortcut;

class ParagraphStylesPanel : public QDockWidget
{
	Q_OBJECT
public:
	explicit ParagraphStylesPanel(QWidget* parent = nullptr);
	bool eventFilter(QObject* obj, QEvent* event) override;
	
	void setDocument(ScribusDoc* doc);
	void setMainWindow(ScribusMainWindow* mw);
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
private:
	void applySingleParagraph(PageItem_TextFrame* textFrame, const QString& styleName);
	void applySequentialStyles(PageItem_TextFrame* textFrame, const QString& startStyleName);
	void applyStyleByName(const QString& styleName);
	void applyChainFromStyle(const QString& startStyle);
	void setupShortcuts();
	void clearShortcuts();
	
	QPointer<ScribusDoc> m_doc;
	ScribusMainWindow* m_mainWindow;
	
	QListWidget* m_stylesList;
	QLineEdit* m_searchBox;
	QGridLayout* m_iconsGrid = nullptr;
	QWidget*     m_designTab  = nullptr;
	QListWidget* m_colList    = nullptr;
	QPushButton* m_newButton;
	QPushButton* m_deleteButton;
	QPushButton* m_editButton;
	
	QList<QShortcut*> m_shortcuts;
	QList<QShortcut*> m_columnShortcuts;
	QMap<QString, QString> m_nextStyles; // styleName → nextStyleName
	QTimer* m_syncTimer = nullptr;
	QString m_lastHighlightedStyle;
	bool m_userInteracting = false; // true while pointer is pressed on the styles list
};
#endif
