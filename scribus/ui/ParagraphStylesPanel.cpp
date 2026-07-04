#include "hyphenator.h"
#include <QScrollArea>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QGridLayout>
#include "ParagraphStylesPanel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include "scribusdoc.h"
#include "scribus.h"
#include "styles/paragraphstyle.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "pageitem_table.h"
#include "selection.h"
#include "appmodes.h"
#include <QMessageBox>
#include <QDebug>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeySequenceEdit>
#include <QCheckBox>
#include <QTimer>
#include <QShortcut>
#include <QSettings>

// Per-style shortcut storage (in-memory, saved to QSettings)
static QMap<QString, QKeySequence> s_styleShortcuts;

static void loadShortcuts()
{
	QSettings cfg("Scribus", "ParagraphStyleShortcuts");
	for (const QString& key : cfg.allKeys())
		s_styleShortcuts[key] = QKeySequence(cfg.value(key).toString());
}

static void saveShortcuts()
{
	QSettings cfg("Scribus", "ParagraphStyleShortcuts");
	cfg.clear();
	for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it)
		cfg.setValue(it.key(), it.value().toString());
}


// ============================================================
// Suneer Column Config Dialog
// ============================================================
struct SuneerColumnConfigEntry {
    bool useGuideGap = false;
	QStringList styles;      // p1, p2, p3, p4 styles
	QString columnSplitStyle;
	int columnNumber;
	double columnGutter;
	double columnWidth;
	QKeySequence shortcut;
	bool autoFit {false};
};

static QList<SuneerColumnConfigEntry> s_columnConfigs;

static void loadColumnConfigs()
{
	s_columnConfigs.clear();
	QSettings cfg("Scribus", "SuneerColumnConfig");
	int count = cfg.beginReadArray("configs");
	for (int i = 0; i < count; ++i)
	{
		cfg.setArrayIndex(i);
		SuneerColumnConfigEntry e;
		int styleCount = cfg.beginReadArray("styles");
		for (int j = 0; j < styleCount; ++j)
		{
			cfg.setArrayIndex(j);
			e.styles << cfg.value("style").toString();
		}
		cfg.endArray();
		e.columnSplitStyle = cfg.value("columnSplitStyle").toString();
		e.columnNumber = cfg.value("columnNumber", 2).toInt();
		e.columnGutter = cfg.value("columnGutter", 4.0).toDouble();
		e.useGuideGap = cfg.value("useGuideGap", false).toBool();
		e.columnWidth = cfg.value("columnWidth", 100.0).toDouble();
		e.shortcut = QKeySequence(cfg.value("shortcut").toString());
		e.autoFit = cfg.value("autoFit", false).toBool();
		s_columnConfigs.append(e);
	}
	cfg.endArray();
}

static void saveColumnConfigs()
{
	QSettings cfg("Scribus", "SuneerColumnConfig");
	cfg.beginWriteArray("configs");
	for (int i = 0; i < s_columnConfigs.size(); ++i)
	{
		cfg.setArrayIndex(i);
		const SuneerColumnConfigEntry& e = s_columnConfigs[i];
		cfg.beginWriteArray("styles");
		for (int j = 0; j < e.styles.size(); ++j)
		{
			cfg.setArrayIndex(j);
			cfg.setValue("style", e.styles[j]);
		}
		cfg.endArray();
		cfg.setValue("columnSplitStyle", e.columnSplitStyle);
		cfg.setValue("columnNumber", e.columnNumber);
		cfg.setValue("columnGutter", e.columnGutter);
		cfg.setValue("useGuideGap", e.useGuideGap);
		cfg.setValue("columnWidth", e.columnWidth);
		cfg.setValue("shortcut", e.shortcut.toString());
		cfg.setValue("autoFit", e.autoFit);
	}
	cfg.endArray();
}

class SuneerColumnConfigDialog : public QDialog
{
public:
	SuneerColumnConfigDialog(const QStringList& allStyles,
	                         const SuneerColumnConfigEntry& entry,
	                         double pageWidthMm = 0.0,
	                         double marginLeftMm = 0.0,
	                         double marginRightMm = 0.0,
	                         int guideCols = 0,
	                         double guideGap = 0.0,
	                         QWidget* parent = nullptr)
		: QDialog(parent), m_pageWidthMm(pageWidthMm),
		  m_marginLeftMm(marginLeftMm), m_marginRightMm(marginRightMm),
		  m_guideCols(guideCols), m_guideGap(guideGap)
	{
		setWindowTitle("Paragraph Styles Selecter");
		setMinimumWidth(520);

		QVBoxLayout* outerLay = new QVBoxLayout(this);
		QHBoxLayout* mainLay = new QHBoxLayout();

		// Left panel — style selectors
		QVBoxLayout* leftLay = new QVBoxLayout();
		// p1-p4 removed

		// AutoFit checkbox
		leftLay->addSpacing(10);
		m_autoFitCheck = new QCheckBox("Auto Fit Frame Height", this);
		m_autoFitCheck->setChecked(entry.autoFit);
		m_autoFitCheck->setToolTip("Automatically fit frame height after applying styles");
		leftLay->addWidget(m_autoFitCheck);
		// Shortcut section
		leftLay->addSpacing(10);
		leftLay->addWidget(new QLabel("<b>Set Short Key</b>"));
		QHBoxLayout* scRow = new QHBoxLayout();
		QPushButton* setKeyBtn = new QPushButton("Set Key", this);
		m_keyEdit = new QKeySequenceEdit(entry.shortcut, this);
		scRow->addWidget(setKeyBtn);
		scRow->addWidget(m_keyEdit, 1);
		leftLay->addLayout(scRow);
		leftLay->addStretch();
		mainLay->addLayout(leftLay, 1);

		// Right panel — column settings
		QVBoxLayout* rightLay = new QVBoxLayout();
		rightLay->addSpacing(10);
		rightLay->addWidget(new QLabel("<b>Columns</b>"));

		QHBoxLayout* numRow = new QHBoxLayout();
		numRow->addWidget(new QLabel("Number"));
		m_columnNumber = new QSpinBox(this);
		m_columnNumber->setRange(1, 10);
		m_columnNumber->setValue(entry.columnNumber);
		numRow->addWidget(m_columnNumber);
		// Auto calculate button
		QPushButton* autoCalcBtn = new QPushButton("Auto", this);
		autoCalcBtn->setToolTip("Auto calculate gutter and width from page");
		numRow->addWidget(autoCalcBtn);
		connect(autoCalcBtn, &QPushButton::clicked, this, [this]() { autoCalculate(); });
		connect(m_columnNumber, QOverload<int>::of(&QSpinBox::valueChanged),
		        this, [this]() { autoCalculate(); });
		rightLay->addLayout(numRow);

		QHBoxLayout* gutRow = new QHBoxLayout();
		gutRow->addWidget(new QLabel("Guter"));
		m_columnGutter = new QDoubleSpinBox(this);
		m_columnGutter->setRange(0, 100);
		m_columnGutter->setDecimals(2);
		m_columnGutter->setSuffix(" mm");
		m_columnGutter->setValue(entry.columnGutter);
		gutRow->addWidget(m_columnGutter);
		rightLay->addLayout(gutRow);

		// Use Guide Gap checkbox
		m_useGuideGapChk = new QCheckBox("Override Guide Gap", this);
		m_useGuideGapChk->setChecked(entry.useGuideGap);
		m_useGuideGapChk->setToolTip("Checked: use Gutter + Width values | Unchecked: use Guide Manager gap");
		rightLay->addWidget(m_useGuideGapChk);

		QHBoxLayout* widRow = new QHBoxLayout();
		widRow->addWidget(new QLabel("Total Width"));
		m_columnWidth = new QDoubleSpinBox(this);
		m_columnWidth->setRange(0, 1000);
		m_columnWidth->setDecimals(2);
		m_columnWidth->setSuffix(" mm");
		m_columnWidth->setValue(entry.columnWidth);
		widRow->addWidget(m_columnWidth);
		rightLay->addLayout(widRow);

		// Connect after both widgets created
		connect(m_useGuideGapChk, &QCheckBox::toggled, m_columnGutter, &QDoubleSpinBox::setEnabled);
		connect(m_useGuideGapChk, &QCheckBox::toggled, m_columnWidth, &QDoubleSpinBox::setEnabled);
		m_columnGutter->setEnabled(entry.useGuideGap);
		m_columnWidth->setEnabled(entry.useGuideGap);

		rightLay->addStretch();
		mainLay->addLayout(rightLay, 1);

		outerLay->addLayout(mainLay);

		QDialogButtonBox* bb = new QDialogButtonBox(
			QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
		outerLay->addWidget(bb);

		connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
		Q_UNUSED(setKeyBtn);
	}

	SuneerColumnConfigEntry result() const
	{
		SuneerColumnConfigEntry e;
		// m_styleCombos removed
		// m_splitStyleCombo removed
		e.columnNumber = m_columnNumber->value();
		e.columnGutter = m_columnGutter->value();
		e.columnWidth = m_columnWidth->value();
		e.useGuideGap = m_useGuideGapChk->isChecked();
		e.shortcut = m_keyEdit->keySequence();
		e.autoFit = m_autoFitCheck->isChecked();
		return e;
	}

private:
	QCheckBox* m_useGuideGapChk = nullptr;
	double m_pageWidthMm;
	double m_marginLeftMm;
	double m_marginRightMm;
	int m_guideCols = 0;
	double m_guideGap = 0.0;

	void autoCalculate() {
		if (m_pageWidthMm <= 0) return;
		double usableWidth = m_pageWidthMm - m_marginLeftMm - m_marginRightMm;
		double gutter = (m_guideGap > 0) ? m_guideGap : 4.0;
		if (usableWidth > 0) {
			m_columnGutter->blockSignals(true);
			m_columnWidth->blockSignals(true);
			m_useGuideGapChk->blockSignals(true);
			// Show calculated values in spinboxes
			m_columnGutter->setValue(gutter);
			m_columnWidth->setValue(usableWidth);
			// Uncheck override — use guide manager values
			m_useGuideGapChk->setChecked(false);
			// Keep spinboxes disabled (guide manager will be used)
			m_columnGutter->setEnabled(false);
			m_columnWidth->setEnabled(false);
			m_columnGutter->blockSignals(false);
			m_columnWidth->blockSignals(false);
			m_useGuideGapChk->blockSignals(false);
		}
	}

	QList<QComboBox*> m_styleCombos;
	QComboBox* m_splitStyleCombo;
	QSpinBox* m_columnNumber;
	QDoubleSpinBox* m_columnGutter;
	QDoubleSpinBox* m_columnWidth;
	QKeySequenceEdit* m_keyEdit;
	QCheckBox* m_autoFitCheck;
};


// ============================================================
// Suneer Column Config Manager Dialog
// ============================================================
class SuneerColumnConfigManager : public QDialog
{
public:
	SuneerColumnConfigManager(const QStringList& allStyles,
	                          double pageWidthMm = 0.0,
	                          double marginLeftMm = 0.0,
	                          double marginRightMm = 0.0,
	                          int guideCols = 0,
	                          double guideGap = 0.0,
	                          QWidget* parent = nullptr)
		: QDialog(parent), m_allStyles(allStyles),
		  m_pageWidthMm(pageWidthMm), m_marginLeftMm(marginLeftMm), m_marginRightMm(marginRightMm),
		  m_guideCols(guideCols), m_guideGap(guideGap)
	{
		setWindowTitle("Column Style Configurations");
		setMinimumSize(500, 400);
		QVBoxLayout* lay = new QVBoxLayout(this);

		lay->addWidget(new QLabel("<b>Saved Configurations:</b>"));

		m_list = new QListWidget(this);
		m_list->setAlternatingRowColors(true);
		lay->addWidget(m_list);

		QHBoxLayout* btnRow = new QHBoxLayout();
		QPushButton* addBtn = new QPushButton("+ Add", this);
		QPushButton* editBtn = new QPushButton("Edit", this);
		QPushButton* cloneBtn = new QPushButton("Clone", this);
		QPushButton* delBtn = new QPushButton("Delete", this);
		addBtn->setStyleSheet("QPushButton { padding: 6px 14px; background: #27ae60; color: white; border: none; border-radius: 4px; }");
		editBtn->setStyleSheet("QPushButton { padding: 6px 14px; background: #2980b9; color: white; border: none; border-radius: 4px; }");
		cloneBtn->setStyleSheet("QPushButton { padding: 6px 14px; background: #8e44ad; color: white; border: none; border-radius: 4px; }");
		delBtn->setStyleSheet("QPushButton { padding: 6px 14px; background: #e74c3c; color: white; border: none; border-radius: 4px; }");
		btnRow->addWidget(addBtn);
		btnRow->addWidget(editBtn);
		btnRow->addWidget(cloneBtn);
		btnRow->addWidget(delBtn);
		btnRow->addStretch();
		lay->addLayout(btnRow);

		QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
		connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
		lay->addWidget(bb);

		connect(addBtn, &QPushButton::clicked, this, &SuneerColumnConfigManager::addConfig);
		connect(editBtn, &QPushButton::clicked, this, &SuneerColumnConfigManager::editConfig);
		connect(cloneBtn, &QPushButton::clicked, this, &SuneerColumnConfigManager::cloneConfig);
		connect(delBtn, &QPushButton::clicked, this, &SuneerColumnConfigManager::deleteConfig);

		refreshList();
	}

private:
	void refreshList()
	{
		m_list->clear();
		for (int i = 0; i < s_columnConfigs.size(); ++i)
		{
			const SuneerColumnConfigEntry& e = s_columnConfigs[i];
			QString styles = e.styles.join(", ");
			QString sc = e.shortcut.isEmpty() ? "No shortcut" : e.shortcut.toString();
			QString label = QString("Config %1 | Styles: [%2] | Split: %3 | Cols: %4 | Key: %5")
				.arg(i+1).arg(styles).arg(e.columnSplitStyle).arg(e.columnNumber).arg(sc);
			QListWidgetItem* item = new QListWidgetItem(label);
			item->setData(Qt::UserRole, i);
			m_list->addItem(item);
		}
	}

	void addConfig()
	{
		SuneerColumnConfigEntry entry;
		entry.columnNumber = 2;
		entry.columnGutter = 4.0;
		entry.columnWidth = 100.0;
		SuneerColumnConfigDialog dlg(m_allStyles, entry, m_pageWidthMm, m_marginLeftMm, m_marginRightMm, m_guideCols, m_guideGap, this);
		if (dlg.exec() != QDialog::Accepted) return;
		s_columnConfigs.append(dlg.result());
		saveColumnConfigs();
		refreshList();
	}

	void editConfig()
	{
		QListWidgetItem* item = m_list->currentItem();
		if (!item) return;
		int idx = item->data(Qt::UserRole).toInt();
		SuneerColumnConfigDialog dlg(m_allStyles, s_columnConfigs[idx], m_pageWidthMm, m_marginLeftMm, m_marginRightMm, m_guideCols, m_guideGap, this);
		if (dlg.exec() != QDialog::Accepted) return;
		s_columnConfigs[idx] = dlg.result();
		saveColumnConfigs();
		refreshList();
	}

	void cloneConfig()
	{
		QListWidgetItem* item = m_list->currentItem();
		if (!item) return;
		int idx = item->data(Qt::UserRole).toInt();
		SuneerColumnConfigEntry cloned = s_columnConfigs[idx];
		cloned.shortcut = QKeySequence(); // Clear shortcut for clone
		SuneerColumnConfigDialog dlg(m_allStyles, cloned, m_pageWidthMm, m_marginLeftMm, m_marginRightMm, m_guideCols, m_guideGap, this);
		if (dlg.exec() != QDialog::Accepted) return;
		s_columnConfigs.append(dlg.result());
		saveColumnConfigs();
		refreshList();
	}

	void deleteConfig()
	{
		QListWidgetItem* item = m_list->currentItem();
		if (!item) return;
		int idx = item->data(Qt::UserRole).toInt();
		s_columnConfigs.removeAt(idx);
		saveColumnConfigs();
		refreshList();
	}

	QListWidget* m_list;
	QStringList m_allStyles;
	double m_pageWidthMm = 0.0;
	double m_marginLeftMm = 0.0;
	double m_marginRightMm = 0.0;
	int m_guideCols = 0;
	double m_guideGap = 0.0;
};

// Chain Style Dialog
class ChainStyleDialog : public QDialog
{
public:
	ChainStyleDialog(const QString& rootStyle, const QStringList& allStyles,
	                 const QStringList& currentChain, const QKeySequence& currentShortcut,
	                 QWidget* parent = nullptr)
		: QDialog(parent)
	{
		setWindowTitle("Next Style Chain: " + rootStyle);
		setMinimumWidth(360);
		QVBoxLayout* lay = new QVBoxLayout(this);

		// Shortcut field
		QHBoxLayout* scRow = new QHBoxLayout();
		scRow->addWidget(new QLabel("Shortcut key:"));
		m_keyEdit = new QKeySequenceEdit(currentShortcut, this);
		scRow->addWidget(m_keyEdit, 1);
		QPushButton* clearBtn = new QPushButton("Clear", this);
		clearBtn->setFixedWidth(50);
		connect(clearBtn, &QPushButton::clicked, this, [this]() {
			m_keyEdit->clear();
		});
		scRow->addWidget(clearBtn);
		lay->addLayout(scRow);

		lay->addWidget(new QLabel("Chain (applied to paragraphs in order):"));
		m_list = new QListWidget(this);
		m_list->addItems(currentChain);
		lay->addWidget(m_list);

		QHBoxLayout* row = new QHBoxLayout();
		m_combo = new QComboBox(this);
		for (const QString& s : allStyles)
			m_combo->addItem(s);
		row->addWidget(m_combo, 1);
		QPushButton* addBtn = new QPushButton("+", this);
		addBtn->setFixedSize(30, 30);
		connect(addBtn, &QPushButton::clicked, this, [this]() {
			QString s = m_combo->currentText();
			if (!s.isEmpty()) m_list->addItem(s);
		});
		row->addWidget(addBtn);
		QPushButton* delBtn = new QPushButton("-", this);
		delBtn->setFixedSize(30, 30);
		connect(delBtn, &QPushButton::clicked, this, [this]() {
			delete m_list->currentItem();
		});
		row->addWidget(delBtn);
		lay->addLayout(row);

		m_conflictLabel = new QLabel("", this);
		m_conflictLabel->setStyleSheet("color: red; font-size: 9pt;");
		lay->addWidget(m_conflictLabel);
		connect(m_keyEdit, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence& ks) {
			if (ks.isEmpty()) { m_conflictLabel->setText(""); return; }
			// Check against existing style shortcuts
			for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it)
				if (!it.value().isEmpty() && it.value() == ks)
				{
					m_conflictLabel->setText("Warning: Key already used by style: " + it.key());
					return;
				}
			m_conflictLabel->setText("");
		});
		QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
		lay->addWidget(bb);
	}

	QStringList chain() const
	{
		QStringList result;
		for (int i = 0; i < m_list->count(); ++i)
			result << m_list->item(i)->text();
		return result;
	}

	QKeySequence shortcut() const { return m_keyEdit->keySequence(); }

private:
	QListWidget*      m_list;
	QComboBox*        m_combo;
	QKeySequenceEdit* m_keyEdit;
	QLabel* m_conflictLabel;
};

// Custom delegate to draw chain arrow + shortcut on right side
class StyleItemDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyledItemDelegate::paint(painter, option, index);
		bool hasChain = index.data(Qt::UserRole + 1).toBool();
		if (hasChain)
		{
			painter->save();
			bool selected = (option.state & QStyle::State_Selected);
			QColor arrowColor = selected ? Qt::white : QColor("#1a5fb4");

			// Draw shortcut text left of arrow
			QString styleName = index.data(Qt::UserRole).toString();
			QKeySequence sc = s_styleShortcuts.value(styleName);
			if (!sc.isEmpty())
			{
				painter->setPen(selected ? Qt::white : QColor("#888"));
				QFont sf = painter->font();
				sf.setPointSize(8);
				painter->setFont(sf);
				QRect scRect = option.rect.adjusted(0, 0, -42, 0);
				painter->drawText(scRect, Qt::AlignRight | Qt::AlignVCenter, sc.toString());
			}

			// Draw arrow icon
			painter->setPen(arrowColor);
			QFont f = painter->font();
			f.setBold(true);
			f.setPointSize(36);
			painter->setFont(f);
			QRect r = option.rect.adjusted(0, 0, -4, 0);
			painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8("\xF0\x9F\x94\x97"));
			painter->restore();
		}
	}
};

ParagraphStylesPanel::ParagraphStylesPanel(QWidget* parent)
	: QDockWidget(parent),
	  m_doc(nullptr),
	  m_mainWindow(nullptr)
{
	setWindowTitle("Paragraph Styles");
	setObjectName("ParagraphStylesPanel");
	setFeatures(QDockWidget::NoDockWidgetFeatures);
	loadShortcuts();

	// Auto-load Column Styles
	{
		QString path = QDir::homePath() + "/.config/scribus/scribus_column_styles.json";
		QFile file(path);

		if (file.open(QIODevice::ReadOnly))
		{
			QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
			file.close();

			if (doc.isObject())
			{
				QJsonObject root = doc.object();

				if (root.contains("columnStyles"))
				{
					QSettings cfg("Scribus", "SuneerColumnConfig");
					cfg.clear();

					QJsonArray arr = root["columnStyles"].toArray();

					cfg.beginWriteArray("configs", arr.size());

					for (int i = 0; i < arr.size(); ++i)
					{
						cfg.setArrayIndex(i);

						QJsonObject obj = arr[i].toObject();

						QJsonArray sArr = obj["styles"].toArray();

						cfg.beginWriteArray("styles", sArr.size());
						for (int j = 0; j < sArr.size(); ++j)
						{
							cfg.setArrayIndex(j);
							cfg.setValue("style", sArr[j].toString());
						}
						cfg.endArray();

						cfg.setValue("columnNumber",     obj["columnNumber"].toInt());
						cfg.setValue("columnSplitStyle", obj["columnSplitStyle"].toString());
						cfg.setValue("columnGutter",     obj["columnGutter"].toDouble());
						cfg.setValue("columnWidth",      obj["columnWidth"].toDouble());
						cfg.setValue("useGuideGap",      obj["useGuideGap"].toBool());
						cfg.setValue("autoFit",          obj["autoFit"].toBool());
						cfg.setValue("shortcut",         obj["shortcut"].toString());
					}

					cfg.endArray();
					cfg.sync();
				}
			}
		}
	}


	loadColumnConfigs();
	QTimer::singleShot(500, this, &ParagraphStylesPanel::rebuildShortcuts);

	setStyleSheet(
		"QDockWidget { background: #f5f5f5; }"
		"QDockWidget::title { background: #cce0ff; color: #0055aa; padding: 8px; font-weight: bold; }"
	);

	QWidget* mainWidget = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(mainWidget);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	// ── Tab Widget ──
	QTabWidget* tabWidget = new QTabWidget(this);
	tabWidget->setStyleSheet(
		"QTabWidget::pane { border: 1px solid #ddd; }"
		"QTabBar::tab { padding: 6px 12px; font-size: 10pt; }"
		"QTabBar::tab:selected { background: #3498db; color: white; }"
	);

	// Tab 1: Styles
	QWidget* stylesTab = new QWidget();
	QVBoxLayout* stylesLayout = new QVBoxLayout(stylesTab);
	stylesLayout->setContentsMargins(4, 4, 4, 4);
	stylesLayout->setSpacing(8);

	m_searchBox = new QLineEdit(this);
	m_searchBox->setPlaceholderText("Search styles...");
	m_searchBox->setStyleSheet(
		"QLineEdit { padding: 8px; border: 2px solid #ddd; border-radius: 4px;"
		"  background: white; font-size: 11pt; }"
		"QLineEdit:focus { border-color: #3498db; }"
	);
	stylesLayout->addWidget(m_searchBox);

	m_stylesList = new QListWidget(this);
	m_stylesList->setAlternatingRowColors(false);
	m_stylesList->setStyleSheet(
		"QListWidget { background: white; border: 1px solid #ddd; border-radius: 4px; outline: none; }"
		"QListWidget::item { padding: 12px 12px; border-bottom: 1px solid #f0f0f0; font-size: 11pt; }"
		"QListWidget::item:hover { background: #e8f4f8; }"
		"QListWidget::item:selected { background: #3498db; color: white; border: none; }"
	);
	stylesLayout->addWidget(m_stylesList);

	QLabel* infoLabel = new QLabel("Frame: Para1->Style, Para2->Next\nEdit: From cursor down", this);
	infoLabel->setWordWrap(true);
	infoLabel->setStyleSheet(
		"QLabel { color: #7f8c8d; font-size: 9pt; padding: 8px; background: #ecf0f1; border-radius: 4px; }"
	);
	stylesLayout->addWidget(infoLabel);

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->setSpacing(6);

	QPushButton* setNextButton = new QPushButton("Next", this);
	setNextButton->setToolTip("Set Next Style Chain & Shortcut");
	setNextButton->setStyleSheet(
		"QPushButton { padding: 8px 16px; background: #3498db; color: white;"
		"  border: none; border-radius: 4px; font-size: 10pt; }"
		"QPushButton:hover { background: #2980b9; }"
		"QPushButton:pressed { background: #21618c; }"
	);

	m_newButton = new QPushButton("+", this);
	m_newButton->setFixedSize(36, 36);
	m_newButton->setEnabled(false);
	m_newButton->setStyleSheet(
		"QPushButton { background: #95a5a6; color: white; border: none; border-radius: 18px; font-size: 16pt; }"
	);

	m_deleteButton = new QPushButton("-", this);
	m_deleteButton->setFixedSize(36, 36);
	m_deleteButton->setEnabled(false);
	m_deleteButton->setStyleSheet(m_newButton->styleSheet());

	m_editButton = new QPushButton("\xe2\x9c\x8e", this);
	m_editButton->setFixedSize(36, 36);
	m_editButton->setEnabled(false);
	m_editButton->setStyleSheet(m_newButton->styleSheet());

	buttonLayout->addWidget(setNextButton);
	// colConfigBtn removed — Column Style is now a tab
	buttonLayout->addStretch();
	buttonLayout->addWidget(m_newButton);
	buttonLayout->addWidget(m_deleteButton);
	buttonLayout->addWidget(m_editButton);

	stylesLayout->addLayout(buttonLayout);

	// Tab 2: Design Style
	QWidget* designTab = new QWidget();
	m_designTab = designTab;
	QVBoxLayout* designLayout = new QVBoxLayout(designTab);
	designLayout->setContentsMargins(8, 8, 8, 8);
	designLayout->setSpacing(8);

	// Load icons from QSettings dynamically
	QSettings dsCfg("Scribus", "SuneerDesignStyle");
	int dsCount = dsCfg.beginReadArray("styles");
	dsCfg.endArray();
	m_iconsGrid = new QGridLayout();
	m_iconsGrid->setSpacing(6);
	for (int i = 0; i < dsCount; ++i) {
		dsCfg.beginReadArray("styles");
		dsCfg.setArrayIndex(i);
		QString iconPath = dsCfg.value("iconPath").toString();
		QString tooltip = dsCfg.value("tooltip", QString("Style %1").arg(i+1)).toString();
		QPushButton* btn = new QPushButton(designTab);
		if (!iconPath.isEmpty()) btn->setIcon(QIcon(iconPath));
		btn->setIconSize(QSize(100, 100));
		btn->setFixedSize(110, 110);
		btn->setToolTip(tooltip);
		btn->setStyleSheet("QPushButton { border: 2px solid #ddd; border-radius: 4px; }QPushButton:hover { border-color: #3498db; }");
		// Apply styles on click
		QStringList styles = dsCfg.value("styles").toStringList();
		QString imgPos = dsCfg.value("imagePosition", "Right").toString();
		int cols = dsCfg.value("columns", 3).toInt();
		QString colBreak = dsCfg.value("colBreakStyle").toString();
		QString shortcut = dsCfg.value("shortcut").toString();
		// Load img settings before endArray
		double sImgX = dsCfg.value("imgOffsetX", 0.0).toDouble();
		double sImgY = dsCfg.value("imgOffsetY", 0.0).toDouble();
		double sImgW = dsCfg.value("imgWidth", 0.0).toDouble();
		double sImgH = dsCfg.value("imgHeight", 0.0).toDouble();
		dsCfg.endArray();
		connect(btn, &QPushButton::clicked, this, [this, styles, imgPos, cols, colBreak, sImgX, sImgY, sImgW, sImgH]() {
			applyDesignStyle(styles, imgPos, cols, colBreak, sImgX, sImgY, sImgW, sImgH);
		});
		m_iconsGrid->addWidget(btn, i/2, i%2);
	}
	designLayout->addLayout(m_iconsGrid);
	designLayout->addStretch();

	// Settings button
	QPushButton* settingsBtn = new QPushButton("⚙ Settings", designTab);
	settingsBtn->setStyleSheet(
		"QPushButton { padding: 6px 12px; background: #7f8c8d; color: white;"
		"  border: none; border-radius: 4px; font-size: 10pt; }"
		"QPushButton:hover { background: #636e72; }"
	);
	designLayout->addWidget(settingsBtn);
	connect(settingsBtn, &QPushButton::clicked, this, &ParagraphStylesPanel::openDesignStyleSettings);

	tabWidget->addTab(stylesTab, "Styles");
	tabWidget->addTab(designTab, "Design Style");
	layout->addWidget(tabWidget);
	setWidget(mainWidget);

	connect(m_searchBox, &QLineEdit::textChanged, this, &ParagraphStylesPanel::filterStyles);
	connect(setNextButton, &QPushButton::clicked, this, &ParagraphStylesPanel::setNextStyle);

	m_stylesList->setItemDelegate(new StyleItemDelegate(m_stylesList));
	m_stylesList->viewport()->installEventFilter(this);
}

bool ParagraphStylesPanel::eventFilter(QObject* obj, QEvent* event)
{
	if (obj == m_stylesList->viewport() && event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* me = static_cast<QMouseEvent*>(event);
		QListWidgetItem* item = m_stylesList->itemAt(me->pos());
		if (item)
		{
			QString styleName = item->data(Qt::UserRole).toString();
			bool hasChain = item->data(Qt::UserRole + 1).toBool();
			bool clickedChain = hasChain && (me->pos().x() > m_stylesList->viewport()->width() - 60);
			if (clickedChain)
			{
				applyChainFromStyle(styleName);
				return true;
			}
			else
			{
				// Apply the style captured directly from the clicked item
				// (styleName, from item->data(UserRole) above) instead of
				// re-reading m_stylesList->currentItem() via applyStyle().
				// applyStyle() derives the name from the list's "current"
				// state, which the 150ms sync timer can mutate between the
				// click and the apply — the source of the wrong-style-on-
				// first-click bug. Keep setCurrentItem for immediate visual
				// highlight (applyStyleByName does not update it directly).
				m_stylesList->setCurrentItem(item);
				applyStyleByName(styleName);
				return true;
			}
		}
	}
	// Handle keyboard shortcuts for chain styles
	if (obj == m_stylesList->viewport() && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* ke = static_cast<QKeyEvent*>(event);
		QKeySequence pressed(ke->key() | ke->modifiers());
		for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it)
		{
			if (!it.value().isEmpty() && it.value() == pressed)
			{
				applyChainFromStyle(it.key());
				return true;
			}
		}
	}
	return QDockWidget::eventFilter(obj, event);
}

void ParagraphStylesPanel::setDocument(ScribusDoc* doc)
{
	if (m_doc && m_doc != doc)
		disconnect(m_doc, nullptr, this, nullptr);
	m_doc = doc;
	if (m_doc)
	{
		connect(m_doc, &ScribusDoc::docChanged, this, &ParagraphStylesPanel::updateStylesList);
		if (!m_syncTimer)
		{
			m_syncTimer = new QTimer(this);
			m_syncTimer->setInterval(150);
			connect(m_syncTimer, &QTimer::timeout, this, &ParagraphStylesPanel::syncCurrentStyle);
		}
		m_syncTimer->start();
	}
	else if (m_syncTimer)
	{
		m_syncTimer->stop();
	}
	updateStylesList();
	rebuildShortcuts();
}

void ParagraphStylesPanel::syncCurrentStyle()
{
	if (!m_doc || (m_doc->appMode != modeEdit && m_doc->appMode != modeEditTable))
		return;
	if (m_doc->m_Selection->isEmpty())
		return;

	PageItem* item = m_doc->m_Selection->itemAt(0);
	if (!item)
		return;
	if (item->isTable() && m_doc->appMode == modeEditTable)
		item = item->asTable()->activeCell().textFrame();
	if (!item || !item->isTextFrame())
		return;

	PageItem_TextFrame* tf = item->asTextFrame();
	const ParagraphStyle& currentStyle = tf->currentStyle();
	QString styleName = currentStyle.parent();
	if (styleName.isEmpty())
		styleName = currentStyle.name();

	if (styleName == m_lastHighlightedStyle && m_stylesList->currentItem()
		&& m_stylesList->currentItem()->data(Qt::UserRole).toString() == styleName)
		return;
	m_lastHighlightedStyle = styleName;

	QSignalBlocker blocker(m_stylesList);
	for (int i = 0; i < m_stylesList->count(); ++i)
	{
		QListWidgetItem* listItem = m_stylesList->item(i);
		if (listItem->data(Qt::UserRole).toString() == styleName)
		{
			m_stylesList->setCurrentItem(listItem, QItemSelectionModel::ClearAndSelect);
			if (!listItem->isHidden())
				m_stylesList->scrollToItem(listItem, QAbstractItemView::EnsureVisible);
			return;
		}
	}

	m_stylesList->setCurrentItem(nullptr);
}

void ParagraphStylesPanel::setMainWindow(ScribusMainWindow* mw)
{
	m_mainWindow = mw;
}

void ParagraphStylesPanel::updateStylesList()
{
	m_stylesList->clear();
	if (!m_doc)
	{
		QListWidgetItem* item = new QListWidgetItem("No Document", m_stylesList);
		item->setForeground(Qt::gray);
		m_stylesList->addItem(item);
		return;
	}
	int styleCount = m_doc->paragraphStyles().count();
	if (styleCount == 0)
	{
		QListWidgetItem* item = new QListWidgetItem("No Styles", m_stylesList);
		item->setForeground(Qt::red);
		m_stylesList->addItem(item);
		return;
	}
	QList<QString> styleNames;
	for (int i = 0; i < styleCount; ++i)
		styleNames.append(m_doc->paragraphStyles()[i].name());
	styleNames.sort();

	for (const QString& styleName : styleNames)
	{
		if (!m_doc->paragraphStyles().contains(styleName))
			continue;
		const ParagraphStyle& style = m_doc->paragraphStyles().get(styleName);
		QString nextStyle = style.nextStyle();
		bool hasChain = !nextStyle.isEmpty();

		QListWidgetItem* item = new QListWidgetItem(styleName);
		item->setData(Qt::UserRole, styleName);
		item->setData(Qt::UserRole + 1, hasChain);
		if (hasChain)
		{
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
			item->setForeground(QColor("#1a5fb4"));
			QStringList chainList = nextStyle.split(";");
			item->setToolTip(styleName + " -> " + chainList.join(" -> "));
		}
		m_stylesList->addItem(item);
	}
}

void ParagraphStylesPanel::filterStyles(const QString& filter)
{
	for (int i = 0; i < m_stylesList->count(); ++i)
	{
		QListWidgetItem* item = m_stylesList->item(i);
		QString name = item->data(Qt::UserRole).toString();
		item->setHidden(!name.contains(filter, Qt::CaseInsensitive));
	}
}

void ParagraphStylesPanel::applyStyle()
{
	if (!m_doc || !m_mainWindow) return;
	QListWidgetItem* item = m_stylesList->currentItem();
	if (!item) return;
	QString styleName = item->data(Qt::UserRole).toString();
	if (styleName.isEmpty()) return;
	if (!m_doc->paragraphStyles().contains(styleName)) { updateStylesList(); return; }
	m_mainWindow->setNewParStyle(styleName);
}

void ParagraphStylesPanel::applySingleParagraph(PageItem_TextFrame* textFrame, const QString& styleName)
{
	int cursorPos = textFrame->itemText.cursorPosition();
	int paraStart = cursorPos;
	int paraEnd = cursorPos;
	while (paraStart > 0 && textFrame->itemText.text(paraStart - 1) != SpecialChars::PARSEP)
		paraStart--;
	while (paraEnd < textFrame->itemText.length() && textFrame->itemText.text(paraEnd) != SpecialChars::PARSEP)
		paraEnd++;
	const ParagraphStyle& style = m_doc->paragraphStyles().get(styleName);
	for (int pos = paraStart; pos <= paraEnd && pos < textFrame->itemText.length(); ++pos)
		textFrame->itemText.applyStyle(pos, style);
}

void ParagraphStylesPanel::applySequentialStyles(PageItem_TextFrame*, const QString&) {}

void ParagraphStylesPanel::setNextStyle()
{
	if (!m_doc) return;
	QListWidgetItem* item = m_stylesList->currentItem();
	if (!item) { QMessageBox::warning(this, "Warning", "Select a style"); return; }
	QString styleName = item->data(Qt::UserRole).toString();
	if (styleName.isEmpty()) return;

	QStringList allNames;
	for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
		allNames.append(m_doc->paragraphStyles()[i].name());
	allNames.sort();

	QString existing;
	if (m_doc->paragraphStyles().contains(styleName))
		existing = m_doc->paragraphStyles().get(styleName).nextStyle();
	QStringList currentChain = existing.isEmpty() ? QStringList() : existing.split(";");
	QKeySequence currentSC = s_styleShortcuts.value(styleName);

	ChainStyleDialog dlg(styleName, allNames, currentChain, currentSC, this);
	if (dlg.exec() != QDialog::Accepted) return;

	// Save chain
	QStringList chain = dlg.chain();
	QString chainStr = chain.join(";");
	StyleSet<ParagraphStyle> tmpStyles;
	tmpStyles.redefine(m_doc->paragraphStyles(), true);
	if (tmpStyles.contains(styleName))
	{
		ParagraphStyle& ps = tmpStyles[tmpStyles.find(styleName)];
		ps.setNextStyle(chainStr);
		m_doc->redefineStyles(tmpStyles, false);
		m_doc->changed();
	}

	// Save shortcut
	QKeySequence sc = dlg.shortcut();
	if (sc.isEmpty())
		s_styleShortcuts.remove(styleName);
	else
		s_styleShortcuts[styleName] = sc;
	saveShortcuts();
	rebuildShortcuts();
	updateStylesList();
}

void ParagraphStylesPanel::applyChainFromStyle(const QString& startStyle)
{
	if (!m_doc || !m_mainWindow) return;
	PageItem* selItem = m_doc->m_Selection->isEmpty() ? nullptr : m_doc->m_Selection->itemAt(0);
	PageItem_TextFrame* textFrame = selItem ? selItem->asTextFrame() : nullptr;
	if (!textFrame) { QMessageBox::warning(this, "Chain Apply", "Select a text frame first!"); return; }

	int len = textFrame->itemText.length();
	int paraStart = 0;
	if (m_doc->appMode == modeEdit)
	{
		int cursorPos = textFrame->itemText.cursorPosition();
		paraStart = cursorPos;
		while (paraStart > 0 && textFrame->itemText.text(paraStart - 1) != SpecialChars::PARSEP)
			paraStart--;
	}

	QString chainStr;
	if (m_doc->paragraphStyles().contains(startStyle))
		chainStr = m_doc->paragraphStyles().get(startStyle).nextStyle();
	QStringList chain;
	chain << startStyle;
	if (!chainStr.isEmpty())
		chain << chainStr.split(";");

	int pos = paraStart;
	for (int ci = 0; ci < chain.count() && pos <= len; ++ci)
	{
		const QString& sn = chain[ci];
		int pEnd = pos;
		while (pEnd < len && textFrame->itemText.text(pEnd) != SpecialChars::PARSEP)
			pEnd++;
		if (m_doc->paragraphStyles().contains(sn))
		{
			const ParagraphStyle& ps = m_doc->paragraphStyles().get(sn);
			for (int p = pos; p <= pEnd && p < len; ++p)
				textFrame->itemText.applyStyle(p, ps);
		}
		pos = pEnd + 1;
	}

	textFrame->update();
	textFrame->invalidateLayout(true);
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void ParagraphStylesPanel::applyChainCurrentStyle()
{
	QListWidgetItem* item = m_stylesList->currentItem();
	if (!item) return;
	QString styleName = item->data(Qt::UserRole).toString();
	if (styleName.isEmpty()) return;
	bool hasChain = item->data(Qt::UserRole + 1).toBool();
	if (hasChain)
		applyChainFromStyle(styleName);
	else
		applyStyle();
}

void ParagraphStylesPanel::applyStyleByName(const QString& styleName)
{
	if (!m_doc || !m_mainWindow) return;
	m_mainWindow->setNewParStyle(styleName);
}

void ParagraphStylesPanel::refreshColTabList()
{
    if (!m_colList) return;
    m_colList->clear();
    for (int i = 0; i < s_columnConfigs.size(); ++i) {
        const SuneerColumnConfigEntry& e = s_columnConfigs[i];
        QString styles = e.styles.join(", ");
        QString sc = e.shortcut.isEmpty() ? "No shortcut" : e.shortcut.toString();
        QString label = QString("Config %1 | Styles: [%2] | Split: %3 | Cols: %4 | Key: %5")
            .arg(i+1).arg(styles).arg(e.columnSplitStyle).arg(e.columnNumber).arg(sc);
        QListWidgetItem* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i);
        m_colList->addItem(item);
    }
}

void ParagraphStylesPanel::colTabAdd()
{
    openColumnConfig();
    refreshColTabList();
}

void ParagraphStylesPanel::colTabEdit()
{
    if (!m_colList || !m_doc) return;
    QListWidgetItem* item = m_colList->currentItem();
    if (!item) return;
    int idx = item->data(Qt::UserRole).toInt();
    QStringList allStyles;
    for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
        allStyles.append(m_doc->paragraphStyles()[i].name());
    allStyles.sort();
    double pageW=0, marginL=0, marginR=0, guideGap=0; int guideCols=0;
    const double PT2MM = 1.0/2.8346;
    if (m_doc) {
        pageW   = m_doc->pageWidth()        * PT2MM;
        marginL = m_doc->margins()->left()  * PT2MM;
        marginR = m_doc->margins()->right() * PT2MM;
        ScPage* pg = m_doc->currentPage();
        if (pg) { guideCols = pg->guides.verticalAutoCount(); guideGap = pg->guides.verticalAutoGap()*PT2MM; }
    }
    SuneerColumnConfigDialog dlg(allStyles, s_columnConfigs[idx], pageW, marginL, marginR, guideCols, guideGap, this);
    if (dlg.exec() != QDialog::Accepted) return;
    s_columnConfigs[idx] = dlg.result();
    saveColumnConfigs();
    refreshColTabList();
}

void ParagraphStylesPanel::colTabClone()
{
    if (!m_colList || !m_doc) return;
    QListWidgetItem* item = m_colList->currentItem();
    if (!item) return;
    int idx = item->data(Qt::UserRole).toInt();
    SuneerColumnConfigEntry cloned = s_columnConfigs[idx];
    cloned.shortcut = QKeySequence();
    QStringList allStyles;
    for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
        allStyles.append(m_doc->paragraphStyles()[i].name());
    allStyles.sort();
    double pageW=0, marginL=0, marginR=0, guideGap=0; int guideCols=0;
    const double PT2MM = 1.0/2.8346;
    if (m_doc) {
        pageW   = m_doc->pageWidth()        * PT2MM;
        marginL = m_doc->margins()->left()  * PT2MM;
        marginR = m_doc->margins()->right() * PT2MM;
        ScPage* pg = m_doc->currentPage();
        if (pg) { guideCols = pg->guides.verticalAutoCount(); guideGap = pg->guides.verticalAutoGap()*PT2MM; }
    }
    SuneerColumnConfigDialog dlg(allStyles, cloned, pageW, marginL, marginR, guideCols, guideGap, this);
    if (dlg.exec() != QDialog::Accepted) return;
    s_columnConfigs.append(dlg.result());
    saveColumnConfigs();
    refreshColTabList();
}

void ParagraphStylesPanel::colTabDelete()
{
    if (!m_colList) return;
    QListWidgetItem* item = m_colList->currentItem();
    if (!item) return;
    int idx = item->data(Qt::UserRole).toInt();
    s_columnConfigs.removeAt(idx);
    saveColumnConfigs();
    rebuildColumnShortcuts();
    refreshColTabList();
}

void ParagraphStylesPanel::openColumnConfig()
{
	if (!m_doc) { QMessageBox::warning(this, "Warning", "Open a document first!"); return; }
	QStringList allStyles;
	for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
		allStyles.append(m_doc->paragraphStyles()[i].name());
	allStyles.sort();
	// Page info from document + guide manager
	double pageW = 0, marginL = 0, marginR = 0;
	double guideGap = 0; int guideCols = 0;
	if (m_doc) {
		const double PT2MM = 1.0 / 2.8346;
		pageW   = m_doc->pageWidth()         * PT2MM;
		marginL = m_doc->margins()->left()   * PT2MM;
		marginR = m_doc->margins()->right()  * PT2MM;
		// Guide manager values
		ScPage* curPage = m_doc->currentPage();
		if (curPage) {
			guideCols = curPage->guides.verticalAutoCount();
			guideGap  = curPage->guides.verticalAutoGap() * PT2MM;
		}
	}
	SuneerColumnConfigManager mgr(allStyles, pageW, marginL, marginR, guideCols, guideGap, this);
	mgr.exec();
	rebuildColumnShortcuts();
}

void ParagraphStylesPanel::applyColumnConfig(int configIndex)
{
	if (!m_doc || !m_mainWindow) return;
	if (configIndex >= s_columnConfigs.size()) return;
	const SuneerColumnConfigEntry& e = s_columnConfigs[configIndex];
	PageItem* selItem = m_doc->m_Selection->isEmpty() ? nullptr : m_doc->m_Selection->itemAt(0);
	PageItem_TextFrame* tf = selItem ? selItem->asTextFrame() : nullptr;
	if (!tf) { QMessageBox::warning(this, "Warning", "Select a text frame first!"); return; }

	// Set frame columns
	// Convert mm to points (1 mm = 2.8346 pts)
	const double MM2PT = 2.8346;
	const double PT2MM = 1.0 / MM2PT;
	qDebug() << "applyColumnConfig: cols=" << e.columnNumber << "gutter=" << e.columnGutter << "useGuideGap=" << e.useGuideGap << "width=" << e.columnWidth;
	if (e.columnNumber >= 1)
	{
		tf->setColumns(e.columnNumber);
		// Get guide info from page
		ScPage* curPage = m_doc->currentPage();
		int guideColCount = curPage ? curPage->guides.verticalAutoCount() : 0;
		double guideGapPt = curPage ? curPage->guides.verticalAutoGap() : 0;
		// useGuideGap=true → Gutter value, false → Guide Manager gap
		double colGapPt = e.useGuideGap
		                  ? e.columnGutter * MM2PT
		                  : (guideGapPt > 0 ? guideGapPt : e.columnGutter * MM2PT);
		tf->setColumnGap(e.columnNumber > 1 ? colGapPt : 0);
		// Calculate frame width from Guide Manager columns
		double pageW = m_doc->pageWidth();
		double marginL = m_doc->margins()->left();
		double marginR = m_doc->margins()->right();
		double usableW = pageW - marginL - marginR;
		if (e.useGuideGap && e.columnWidth > 0) {
			// Override mode:
			// columnWidth = full print area (236mm)
			// Single col width = (total - gap×7) / 8  ← always from 8-col grid
			// Frame total = n × singleColW + (n-1) × gap
			double fullWidth = e.columnWidth * MM2PT;
			int maxCols = 8; // base grid = 8 columns
			double singleColW = (fullWidth - colGapPt * (maxCols - 1)) / maxCols;
			double frameTotal = singleColW * e.columnNumber
			                  + colGapPt * (e.columnNumber - 1);
			m_doc->sizeItem(frameTotal, tf->height(), tf);
		} else if (guideColCount > 0) {
			// Guide Manager mode: calculate from guide grid
			int actualGuideCols = guideColCount + 1;
			double singleColW = (usableW - guideGapPt * guideColCount) / actualGuideCols;
			double totalWidth = singleColW * e.columnNumber + colGapPt * (e.columnNumber - 1);
			m_doc->sizeItem(totalWidth, tf->height(), tf);
		} else {
			// No guide manager — use column gutter from config
			double gap = e.columnGutter * MM2PT;
			double singleColW = (usableW - gap * (e.columnNumber - 1)) / e.columnNumber;
			double frameTotal = singleColW * e.columnNumber + gap * (e.columnNumber - 1);
			tf->setColumnGap(gap);
			m_doc->sizeItem(frameTotal, tf->height(), tf);
		}
	}

	// Apply styles to paragraphs
	int len = tf->itemText.length();
	int pos = 0;
	int paraIndex = 0;
	while (pos <= len)
	{
		int pEnd = pos;
		while (pEnd < len && tf->itemText.text(pEnd) != SpecialChars::PARSEP)
			pEnd++;
			if (paraIndex < e.styles.size())
		{
			const QString& sn = e.styles[paraIndex];
					if (!sn.isEmpty() && m_doc->paragraphStyles().contains(sn))
			{
				const ParagraphStyle& ps = m_doc->paragraphStyles().get(sn);
				for (int p = pos; p <= pEnd && p < len; ++p)
					tf->itemText.applyStyle(p, ps);
						}
		}
		pos = pEnd + 1;
		paraIndex++;
		if (pos > len) break;
	}
	tf->invalidateLayout(true);
	tf->layout();
	tf->update();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	// AutoFit
	if (e.autoFit)
		tf->autoFitFrameHeight();
	// Force redraw
}

void ParagraphStylesPanel::rebuildColumnShortcuts()
{
	for (QShortcut* sc : m_columnShortcuts)
		delete sc;
	m_columnShortcuts.clear();
	// Use mainWindow as parent so shortcut works app-wide
	QWidget* parent = m_mainWindow ? m_mainWindow : (parentWidget() ? parentWidget() : this);
	for (int i = 0; i < s_columnConfigs.size(); ++i)
	{
		const SuneerColumnConfigEntry& e = s_columnConfigs[i];
		if (e.shortcut.isEmpty()) continue;
		int idx = i;
		QShortcut* sc = new QShortcut(e.shortcut, parent, nullptr, nullptr, Qt::ApplicationShortcut);
		connect(sc, &QShortcut::activated, this, [this, idx]() {
			applyColumnConfig(idx);
		});
		m_columnShortcuts.append(sc);
	}
}

void ParagraphStylesPanel::editStyle() {}
void ParagraphStylesPanel::newStyle() {}
void ParagraphStylesPanel::deleteStyle() {}
void ParagraphStylesPanel::setupShortcuts() {}
void ParagraphStylesPanel::clearShortcuts() {}

void ParagraphStylesPanel::rebuildShortcuts()
{
	for (QShortcut* sc : m_shortcuts)
		delete sc;
	m_shortcuts.clear();

	QWidget* parent = parentWidget() ? parentWidget() : this;

	// Chain style shortcuts — only if style exists in current document
	for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it)
	{
		if (it.value().isEmpty()) continue;
		if (!m_doc || !m_doc->paragraphStyles().contains(it.key())) continue;
		const QString styleName = it.key();
		QShortcut* sc = new QShortcut(it.value(), parent, nullptr, nullptr, Qt::ApplicationShortcut);
		connect(sc, &QShortcut::activated, this, [this, styleName]() {
			applyChainFromStyle(styleName);
		});
		m_shortcuts.append(sc);
	}

	// Auto Fit shortcut — handled via action in canvasmode
	rebuildColumnShortcuts();
}

void ParagraphStylesPanel::openDesignStyleSettings()
{
    QDialog* dlg = new QDialog(this);

    // Auto-load Design Styles
    {
        QString path = QDir::homePath() + "/.config/scribus/scribus_design_styles.json";
        QFile file(path);

        if (file.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            if (doc.isObject())
            {
                QJsonObject root = doc.object();

                if (root.contains("designStyles"))
                {
                    QSettings dsCfg("Scribus", "SuneerDesignStyle");
                    dsCfg.remove("styles");

                    QJsonArray dsArr = root["designStyles"].toArray();

                    dsCfg.beginWriteArray("styles", dsArr.size());

                    for (int i = 0; i < dsArr.size(); ++i)
                    {
                        dsCfg.setArrayIndex(i);

                        QJsonObject obj = dsArr[i].toObject();

                        dsCfg.setValue("tooltip",       obj["tooltip"].toString());
                        dsCfg.setValue("iconPath",      obj["iconPath"].toString());
                        dsCfg.setValue("styles",        obj["styles"].toVariant());
                        dsCfg.setValue("imagePosition", obj["imagePosition"].toString());
                        dsCfg.setValue("columns",       obj["columns"].toInt());
                        dsCfg.setValue("colBreakStyle", obj["colBreakStyle"].toString());
                        dsCfg.setValue("shortcut",      obj["shortcut"].toString());
                        dsCfg.setValue("imgOffsetX",    obj["imgOffsetX"].toDouble());
                        dsCfg.setValue("imgOffsetY",    obj["imgOffsetY"].toDouble());
                        dsCfg.setValue("imgWidth",      obj["imgWidth"].toDouble());
                        dsCfg.setValue("imgHeight",     obj["imgHeight"].toDouble());
                    }

                    dsCfg.endArray();
                    dsCfg.sync();

                    refreshDesignIcons();
                }
            }
        }
    }
    dlg->setWindowTitle("Style Settings");
    dlg->setMinimumWidth(520);
    dlg->setMinimumHeight(460);

    QVBoxLayout* outerLay = new QVBoxLayout(dlg);
    outerLay->setContentsMargins(6, 6, 6, 6);
    outerLay->setSpacing(6);

    // ✅ Export/Import buttons
    QHBoxLayout* exportImportLay = new QHBoxLayout();
    QPushButton* exportBtn    = new QPushButton("⬆ Export Column", dlg);
    QPushButton* importBtn    = new QPushButton("⬇ Import Column", dlg);
    QPushButton* exportDsBtn  = new QPushButton("⬆ Export Design", dlg);
    QPushButton* importDsBtn  = new QPushButton("⬇ Import Design", dlg);
    exportBtn->setStyleSheet("QPushButton{padding:4px 8px;background:#2980b9;color:white;border:none;border-radius:4px;}");
    importBtn->setStyleSheet("QPushButton{padding:4px 8px;background:#27ae60;color:white;border:none;border-radius:4px;}");
    exportDsBtn->setStyleSheet("QPushButton{padding:4px 8px;background:#8e44ad;color:white;border:none;border-radius:4px;}");
    importDsBtn->setStyleSheet("QPushButton{padding:4px 8px;background:#e67e22;color:white;border:none;border-radius:4px;}");
    exportImportLay->addWidget(exportBtn);
    exportImportLay->addWidget(importBtn);
    exportImportLay->addSpacing(8);
    exportImportLay->addWidget(exportDsBtn);
    exportImportLay->addWidget(importDsBtn);
    exportImportLay->addStretch();
    outerLay->addLayout(exportImportLay);

    // Export Column
    connect(exportBtn, &QPushButton::clicked, dlg, [dlg](){
        QString path = QFileDialog::getSaveFileName(dlg, "Export Column Style",
            QDir::homePath() + "/scribus_column_styles.json", "JSON Files (*.json)");
        if (path.isEmpty()) return;
        QJsonObject root;
        QJsonArray colArr;
        for (const SuneerColumnConfigEntry& e : s_columnConfigs) {
            QJsonObject obj;
            QJsonArray stylesArr;
            for (const QString& s : e.styles) stylesArr.append(s);
            obj["styles"]           = stylesArr;
            obj["columnNumber"]     = e.columnNumber;
            obj["columnSplitStyle"] = e.columnSplitStyle;
            obj["columnGutter"]     = e.columnGutter;
            obj["columnWidth"]      = e.columnWidth;
            obj["useGuideGap"]      = e.useGuideGap;
            obj["autoFit"]          = e.autoFit;
            obj["shortcut"]         = e.shortcut.toString();
            colArr.append(obj);
        }
        root["columnStyles"] = colArr;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            QMessageBox::information(dlg, "Export", "Column Style exported!");
        }
    });
    // Export Design
    connect(exportDsBtn, &QPushButton::clicked, dlg, [dlg](){
        QString path = QFileDialog::getSaveFileName(dlg, "Export Design Style",
            QDir::homePath() + "/scribus_design_styles.json", "JSON Files (*.json)");
        if (path.isEmpty()) return;
        QJsonObject root;
        QSettings dsCfg("Scribus", "SuneerDesignStyle");
        QJsonArray dsArr;
        int dsCount = dsCfg.beginReadArray("styles");
        for (int i = 0; i < dsCount; ++i) {
            dsCfg.setArrayIndex(i);
            QJsonObject obj;
            obj["tooltip"]       = dsCfg.value("tooltip").toString();
            obj["iconPath"]      = dsCfg.value("iconPath").toString();
            obj["styles"]        = QJsonValue::fromVariant(dsCfg.value("styles"));
            obj["imagePosition"] = dsCfg.value("imagePosition").toString();
            obj["columns"]       = dsCfg.value("columns").toInt();
            obj["colBreakStyle"] = dsCfg.value("colBreakStyle").toString();
            obj["shortcut"]      = dsCfg.value("shortcut").toString();
            obj["imgOffsetX"]    = dsCfg.value("imgOffsetX", 0.0).toDouble();
            obj["imgOffsetY"]    = dsCfg.value("imgOffsetY", 0.0).toDouble();
            obj["imgWidth"]      = dsCfg.value("imgWidth",   0.0).toDouble();
            obj["imgHeight"]     = dsCfg.value("imgHeight",  0.0).toDouble();
            dsArr.append(obj);
        }
        dsCfg.endArray();
        root["designStyles"] = dsArr;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            QMessageBox::information(dlg, "Export", "Design Style exported!");
        }
    });

    // Import Column
    connect(importBtn, &QPushButton::clicked, dlg, [dlg](){
        QString path = QFileDialog::getOpenFileName(dlg, "Import Column Style",
            QDir::homePath(), "JSON Files (*.json)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) { QMessageBox::warning(dlg, "Import", "Invalid file!"); return; }
        QJsonObject root = doc.object();
        if (root.contains("columnStyles")) {
            QSettings colCfg("Scribus", "SuneerColumnConfig");
            colCfg.remove("configs");
            QJsonArray colArr = root["columnStyles"].toArray();
            colCfg.beginWriteArray("configs", colArr.size());
            for (int i = 0; i < colArr.size(); ++i) {
                colCfg.setArrayIndex(i);
                QJsonObject obj = colArr[i].toObject();
                QJsonArray sArr = obj["styles"].toArray();
                colCfg.beginWriteArray("styles", sArr.size());
                for (int j = 0; j < sArr.size(); ++j) {
                    colCfg.setArrayIndex(j);
                    colCfg.setValue("style", sArr[j].toString());
                }
                colCfg.endArray();
                colCfg.setValue("columnNumber",     obj["columnNumber"].toInt());
                colCfg.setValue("columnSplitStyle", obj["columnSplitStyle"].toString());
                colCfg.setValue("columnGutter",     obj["columnGutter"].toDouble());
                colCfg.setValue("columnWidth",      obj["columnWidth"].toDouble());
                colCfg.setValue("useGuideGap",      obj["useGuideGap"].toBool());
                colCfg.setValue("autoFit",          obj["autoFit"].toBool());
                colCfg.setValue("shortcut",         obj["shortcut"].toString());
            }
            colCfg.endArray();
            colCfg.sync();
        }
        QMessageBox::information(dlg, "Import", "Column Style imported!");
        dlg->reject();
    });
    // Import Design
    connect(importDsBtn, &QPushButton::clicked, dlg, [dlg](){
        QString path = QFileDialog::getOpenFileName(dlg, "Import Design Style",
            QDir::homePath(), "JSON Files (*.json)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) { QMessageBox::warning(dlg, "Import", "Invalid file!"); return; }
        QJsonObject root = doc.object();
        if (root.contains("designStyles")) {
            QSettings dsCfg("Scribus", "SuneerDesignStyle");
            dsCfg.remove("styles");
            QJsonArray dsArr = root["designStyles"].toArray();
            dsCfg.beginWriteArray("styles", dsArr.size());
            for (int i = 0; i < dsArr.size(); ++i) {
                dsCfg.setArrayIndex(i);
                QJsonObject obj = dsArr[i].toObject();
                dsCfg.setValue("tooltip",       obj["tooltip"].toString());
                dsCfg.setValue("iconPath",      obj["iconPath"].toString());
                dsCfg.setValue("styles",        obj["styles"].toVariant());
                dsCfg.setValue("imagePosition", obj["imagePosition"].toString());
                dsCfg.setValue("columns",       obj["columns"].toInt());
                dsCfg.setValue("colBreakStyle", obj["colBreakStyle"].toString());
                dsCfg.setValue("shortcut",      obj["shortcut"].toString());
                dsCfg.setValue("imgOffsetX",    obj["imgOffsetX"].toDouble());
                dsCfg.setValue("imgOffsetY",    obj["imgOffsetY"].toDouble());
                dsCfg.setValue("imgWidth",      obj["imgWidth"].toDouble());
                dsCfg.setValue("imgHeight",     obj["imgHeight"].toDouble());
            }
            dsCfg.endArray();
            dsCfg.sync();
        }
        QMessageBox::information(dlg, "Import", "Design Style imported!");
        dlg->reject();
    });

    // ✅ Tab Widget
    QTabWidget* settingsTab = new QTabWidget(dlg);

    // ---- Tab 1: Column Style ----
    QWidget* colTabW = new QWidget();
    QVBoxLayout* colTabLay = new QVBoxLayout(colTabW);
    colTabLay->setContentsMargins(8, 8, 8, 8);
    colTabLay->setSpacing(6);

    QListWidget* colMgrList = new QListWidget(colTabW);
    colMgrList->setAlternatingRowColors(true);

    auto refreshColMgrList = [colMgrList]() {
        colMgrList->clear();
        for (int i = 0; i < s_columnConfigs.size(); ++i) {
            const SuneerColumnConfigEntry& e = s_columnConfigs[i];
            QString styles = e.styles.join(", ");
            QString sc = e.shortcut.isEmpty() ? "No shortcut" : e.shortcut.toString();
            QString label = QString("Config %1 | Styles: [%2] | Split: %3 | Cols: %4 | Key: %5")
                .arg(i+1).arg(styles).arg(e.columnSplitStyle).arg(e.columnNumber).arg(sc);
            QListWidgetItem* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, i);
            colMgrList->addItem(item);
        }
    };
    refreshColMgrList();
    colTabLay->addWidget(colMgrList);

    QHBoxLayout* colMgrBtns = new QHBoxLayout();
    QPushButton* cmAdd   = new QPushButton("+ Add",  colTabW);
    QPushButton* cmEdit  = new QPushButton("Edit",   colTabW);
    QPushButton* cmClone = new QPushButton("Clone",  colTabW);
    QPushButton* cmDel   = new QPushButton("Delete", colTabW);
    cmAdd->setStyleSheet("QPushButton{padding:5px 10px;background:#27ae60;color:white;border:none;border-radius:4px;}");
    cmEdit->setStyleSheet("QPushButton{padding:5px 10px;background:#2980b9;color:white;border:none;border-radius:4px;}");
    cmClone->setStyleSheet("QPushButton{padding:5px 10px;background:#8e44ad;color:white;border:none;border-radius:4px;}");
    cmDel->setStyleSheet("QPushButton{padding:5px 10px;background:#e74c3c;color:white;border:none;border-radius:4px;}");
    colMgrBtns->addWidget(cmAdd);
    colMgrBtns->addWidget(cmEdit);
    colMgrBtns->addWidget(cmClone);
    colMgrBtns->addWidget(cmDel);
    colMgrBtns->addStretch();
    colTabLay->addLayout(colMgrBtns);

    auto getDocInfo = [this](QStringList& allStyles, double& pageW, double& marginL,
                              double& marginR, int& guideCols, double& guideGap) {
        const double PT2MM = 1.0/2.8346;
        if (m_doc) {
            for (int i = 0; i < m_doc->paragraphStyles().count(); ++i)
                allStyles.append(m_doc->paragraphStyles()[i].name());
            allStyles.sort();
            pageW   = m_doc->pageWidth()        * PT2MM;
            marginL = m_doc->margins()->left()  * PT2MM;
            marginR = m_doc->margins()->right() * PT2MM;
            ScPage* pg = m_doc->currentPage();
            if (pg) {
                guideCols = pg->guides.verticalAutoCount();
                guideGap  = pg->guides.verticalAutoGap() * PT2MM;
            }
        }
    };

    connect(cmAdd, &QPushButton::clicked, [this, refreshColMgrList, getDocInfo, colTabW]() {
        QStringList allStyles; double pageW=0,marginL=0,marginR=0,guideGap=0; int guideCols=0;
        getDocInfo(allStyles, pageW, marginL, marginR, guideCols, guideGap);
        SuneerColumnConfigEntry entry; entry.columnNumber=2; entry.columnGutter=4.0;
        SuneerColumnConfigDialog dlg2(allStyles, entry, pageW, marginL, marginR, guideCols, guideGap, colTabW);
        if (dlg2.exec() != QDialog::Accepted) return;
        s_columnConfigs.append(dlg2.result());
        saveColumnConfigs(); refreshColTabList(); refreshColMgrList();
    });

    connect(cmEdit, &QPushButton::clicked, [this, colMgrList, refreshColMgrList, getDocInfo, colTabW]() {
        QListWidgetItem* item = colMgrList->currentItem(); if (!item) return;
        int idx = item->data(Qt::UserRole).toInt();
        QStringList allStyles; double pageW=0,marginL=0,marginR=0,guideGap=0; int guideCols=0;
        getDocInfo(allStyles, pageW, marginL, marginR, guideCols, guideGap);
        SuneerColumnConfigDialog dlg2(allStyles, s_columnConfigs[idx], pageW, marginL, marginR, guideCols, guideGap, colTabW);
        if (dlg2.exec() != QDialog::Accepted) return;
        s_columnConfigs[idx] = dlg2.result();
        saveColumnConfigs(); refreshColTabList(); refreshColMgrList();
    });

    connect(cmClone, &QPushButton::clicked, [this, colMgrList, refreshColMgrList, getDocInfo, colTabW]() {
        QListWidgetItem* item = colMgrList->currentItem(); if (!item) return;
        int idx = item->data(Qt::UserRole).toInt();
        SuneerColumnConfigEntry cloned = s_columnConfigs[idx]; cloned.shortcut = QKeySequence();
        QStringList allStyles; double pageW=0,marginL=0,marginR=0,guideGap=0; int guideCols=0;
        getDocInfo(allStyles, pageW, marginL, marginR, guideCols, guideGap);
        SuneerColumnConfigDialog dlg2(allStyles, cloned, pageW, marginL, marginR, guideCols, guideGap, colTabW);
        if (dlg2.exec() != QDialog::Accepted) return;
        s_columnConfigs.append(dlg2.result());
        saveColumnConfigs(); refreshColTabList(); refreshColMgrList();
    });

    connect(cmDel, &QPushButton::clicked, [this, colMgrList, refreshColMgrList]() {
        QListWidgetItem* item = colMgrList->currentItem(); if (!item) return;
        int idx = item->data(Qt::UserRole).toInt();
        s_columnConfigs.removeAt(idx);
        saveColumnConfigs(); rebuildColumnShortcuts(); refreshColTabList(); refreshColMgrList();
    });

    settingsTab->addTab(colTabW, "Column Style");

    // ---- Tab 2: Design Style ----
    QWidget* designTabW = new QWidget();
    QVBoxLayout* mainLay = new QVBoxLayout(designTabW);

    // Scroll area for multiple style configs
    QScrollArea* scroll = new QScrollArea(designTabW);
    scroll->setWidgetResizable(true);
    QWidget* scrollWidget = new QWidget();
    QVBoxLayout* scrollLay = new QVBoxLayout(scrollWidget);
    scrollLay->setSpacing(12);

    // Load existing configs from QSettings
    QSettings cfg("Scribus", "SuneerDesignStyle");
    int count = cfg.beginReadArray("styles");
    cfg.endArray();
    if (count == 0) count = 1;

    auto addStyleEntry = [&, this](int idx) {
        // Load existing values
        QSettings sCfg("Scribus", "SuneerDesignStyle");
        sCfg.beginReadArray("styles");
        sCfg.setArrayIndex(idx);
        QString savedName = sCfg.value("tooltip", QString("Style %1").arg(idx+1)).toString();
        QString savedIcon = sCfg.value("iconPath").toString();
        QStringList savedStyles = sCfg.value("styles").toStringList();
        QString savedImgPos = sCfg.value("imagePosition", "Right").toString();
        int savedCols = sCfg.value("columns", 3).toInt();
        QString savedColBreak = sCfg.value("colBreakStyle").toString();
        QString savedShortcut = sCfg.value("shortcut").toString();
        sCfg.endArray();
        QGroupBox* grp = new QGroupBox("", scrollWidget);
        grp->setObjectName(QString::number(idx));
        QGridLayout* gl = new QGridLayout(grp);

        // Name + Remove button
        QHBoxLayout* nameRow = new QHBoxLayout();
        QPushButton* removeBtn = new QPushButton("Remove", grp);
        removeBtn->setStyleSheet("QPushButton { padding: 4px 8px; background: #e74c3c; color: white; border: none; border-radius: 4px; }");
        QPushButton* rmBtn = removeBtn;
        QGroupBox* rmGrp = grp;
        QObject::connect(rmBtn, &QPushButton::clicked, [rmGrp]() {
            qDebug() << "REMOVE CLICKED!!!";
            rmGrp->setVisible(false);
            rmGrp->setProperty("removed", true);
        });
        nameRow->addStretch();
        removeBtn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        removeBtn->setEnabled(true);
        removeBtn->raise();
        nameRow->addWidget(removeBtn);
        gl->addLayout(nameRow, 0, 0, 1, 2);
        // Name field — separate row
        gl->addWidget(new QLabel("Name:"), 1, 0);
        QLineEdit* nameField = new QLineEdit(savedName, grp);
        nameField->setObjectName("styleName");
        gl->addWidget(nameField, 1, 1);
        // Icon
        QPushButton* iconBtn = new QPushButton("📁 Upload Icon", grp);
        QLabel* iconPathLabel = new QLabel(savedIcon.isEmpty() ? "No icon selected" : savedIcon, grp);
        iconPathLabel->setObjectName("iconPathLabel");
        iconPathLabel->setStyleSheet("color: #7f8c8d; font-size: 9pt;");
        gl->addWidget(new QLabel("Icon:"), 2, 0);
        QHBoxLayout* iconRow = new QHBoxLayout();
        iconRow->addWidget(iconBtn);
        iconRow->addWidget(iconPathLabel, 1);
        gl->addLayout(iconRow, 2, 1);
        connect(iconBtn, &QPushButton::clicked, [iconBtn, iconPathLabel]() {
            QString file = QFileDialog::getOpenFileName(
                nullptr, "Select Icon", "/home/s1/Desktop/workflow/icon",
                "Images (*.png *.jpg *.svg)");
            if (!file.isEmpty()) {
                iconBtn->setIcon(QIcon(file));
                iconBtn->setIconSize(QSize(32, 32));
                iconPathLabel->setText(file);
                iconPathLabel->setToolTip(file);
            }
        });

        // Paragraph styles — p1..p8 dropdowns
        QStringList allStyles;
        if (m_doc) {
            for (int si = 0; si < m_doc->paragraphStyles().count(); ++si)
                allStyles << m_doc->paragraphStyles()[si].name();
            allStyles.sort();
        }
        for (int pi = 0; pi < 8; ++pi) {
            gl->addWidget(new QLabel(QString("p%1:").arg(pi+1)), pi+3, 0);
            QComboBox* scb = new QComboBox(grp);
            scb->addItem("");
            for (const QString& sn : allStyles)
                scb->addItem(sn);
            if (pi < savedStyles.size()) scb->setCurrentText(savedStyles[pi]);
            gl->addWidget(scb, pi+3, 1);
        }

        // Image position
        gl->addWidget(new QLabel("Image Position:"), 12, 0);
        QComboBox* imgPos = new QComboBox(grp);
        imgPos->addItems({"Right", "Left", "Top", "None"});
        imgPos->setCurrentText(savedImgPos);
        gl->addWidget(imgPos, 12, 1);

        // Image frame settings
        QSettings iCfg2("Scribus", "SuneerDesignStyle");
        iCfg2.beginReadArray("styles"); iCfg2.setArrayIndex(idx);
        auto mkSpin = [&](const QString& key, double def, const QString& lbl, int row) {
            gl->addWidget(new QLabel(lbl), row, 0);
            QDoubleSpinBox* sp = new QDoubleSpinBox(grp);
            sp->setRange(-500,500); sp->setDecimals(2); sp->setSuffix(" mm");
            sp->setValue(iCfg2.value(key, def).toDouble()); sp->setObjectName(key);
            gl->addWidget(sp, row, 1);
        };
        mkSpin("imgOffsetX", 0.0, "Img X:", 16);
        mkSpin("imgOffsetY", 0.0, "Img Y:", 17);
        mkSpin("imgWidth",   0.0, "Img W:", 18);
        mkSpin("imgHeight",  0.0, "Img H:", 19);
        iCfg2.endArray();
        // Image frame size + position (relative to text frame)
        // Columns
        gl->addWidget(new QLabel("Col Config:"), 13, 0);
        QComboBox* colConfigCmb = new QComboBox(grp);
        colConfigCmb->setObjectName("colConfigCmb");
        colConfigCmb->addItem("-- None --", -1);
        {
            QSettings ccfg("Scribus", "SuneerColumnConfig");
            int nc = ccfg.beginReadArray("configs");
            for (int ci = 0; ci < nc; ++ci) {
                ccfg.setArrayIndex(ci);
                int cn = ccfg.value("columnNumber", 0).toInt();
                QString sc = ccfg.value("shortcut").toString();
                colConfigCmb->addItem(QString("%1 col (%2)").arg(cn).arg(sc), ci);
            }
            ccfg.endArray();
        }
        // Find by data value (not index) — avoids off-by-one with "None" item
        {
            int foundIdx = colConfigCmb->findData(savedCols);
            colConfigCmb->setCurrentIndex(foundIdx >= 0 ? foundIdx : 0);
        }
        gl->addWidget(colConfigCmb, 13, 1);
        // Column Break Style

        // Shortcut
        gl->addWidget(new QLabel("Shortcut:"), 15, 0);
        QKeySequenceEdit* scEdit = new QKeySequenceEdit(grp);
        scEdit->setObjectName("shortcutEdit");
        QLabel* scConflictLabel = new QLabel("", grp);
        scConflictLabel->setStyleSheet("color: #e74c3c; font-size: 9pt;");
        connect(scEdit, &QKeySequenceEdit::keySequenceChanged, [scConflictLabel](const QKeySequence& ks) {
            if (ks.isEmpty()) { scConflictLabel->setText(""); return; }
            QString ksStr = ks.toString();
            // Check against existing style shortcuts
            for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it) {
                if (!it.value().isEmpty() && it.value().toString() == ksStr) {
                    scConflictLabel->setText("⚠ Conflict: " + it.key());
                    return;
                }
            }
            scConflictLabel->setText("✓ OK");
            scConflictLabel->setStyleSheet("color: #27ae60; font-size: 9pt;");
        });
        QHBoxLayout* scRow = new QHBoxLayout();
        scRow->addWidget(scEdit, 1);
        scRow->addWidget(scConflictLabel);
        gl->addLayout(scRow, 15, 1);

        scrollLay->addWidget(grp);
    };

    for (int i = 0; i < count; ++i)
        addStyleEntry(i);

    // Add button
    QPushButton* addBtn = new QPushButton("+ Add Style", scrollWidget);
    addBtn->setStyleSheet("QPushButton { padding: 8px; background: #27ae60; color: white; border: none; border-radius: 4px; }");
    scrollLay->addWidget(addBtn);
    scrollLay->addStretch();
    int* entryCount = new int(count);
    connect(addBtn, &QPushButton::clicked, [this, scrollLay, scrollWidget, addBtn, entryCount, &addStyleEntry = addStyleEntry]() {
        addStyleEntry(*entryCount);
        (*entryCount)++;
        scrollLay->removeWidget(addBtn);
        scrollLay->addWidget(addBtn);
        scrollWidget->adjustSize();
    });

    scroll->setWidget(scrollWidget);
    mainLay->addWidget(scroll);

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dlg);
    connect(bb, &QDialogButtonBox::accepted, dlg, [this, dlg, scrollWidget]() {
        QSettings cfg("Scribus", "SuneerDesignStyle");
        cfg.remove("styles");
        QList<QGroupBox*> allGroups = scrollWidget->findChildren<QGroupBox*>();
        QList<QGroupBox*> groups;
        for (auto* g : allGroups)
            if (!g->property("removed").toBool()) groups.append(g);
        cfg.beginWriteArray("styles");
        for (int i = 0; i < groups.size(); ++i) {
            cfg.setArrayIndex(i);
            QGroupBox* grp = groups[i];
            QLabel* iconLbl = grp->findChild<QLabel*>("iconPathLabel");
            if (iconLbl) cfg.setValue("iconPath", iconLbl->text());
            QLineEdit* nameEd = grp->findChild<QLineEdit*>("styleName");
            cfg.setValue("tooltip", nameEd ? nameEd->text() : QString("Style %1").arg(i+1));
            QList<QComboBox*> combos = grp->findChildren<QComboBox*>();
            QStringList styles;
            for (int j = 0; j < qMin(8, combos.size()-2); ++j)
                styles << combos[j]->currentText();
            cfg.setValue("styles", styles);
            // Image position = second to last combo
            if (combos.size() >= 2)
                cfg.setValue("imagePosition", combos[combos.size()-2]->currentText());
            // Col break style = last combo
            QLineEdit* colBreak = grp->findChild<QLineEdit*>("colBreakStyle");
            if (colBreak) cfg.setValue("colBreakStyle", colBreak->text());
            QComboBox* colCfgCmb = grp->findChild<QComboBox*>("colConfigCmb");
            if (colCfgCmb) cfg.setValue("columns", colCfgCmb->currentData().toInt());
            // Shortcut
            QKeySequenceEdit* scEdit = grp->findChild<QKeySequenceEdit*>("shortcutEdit");
            if (scEdit) cfg.setValue("shortcut", scEdit->keySequence().toString());
            // Image frame settings
            QList<QDoubleSpinBox*> dspins = grp->findChildren<QDoubleSpinBox*>();
            for (auto* sp : dspins) {
                if (!sp->objectName().isEmpty())
                    cfg.setValue(sp->objectName(), sp->value());
            }
            // Name
            if (nameEd) cfg.setValue("tooltip", nameEd->text());
        }
        cfg.endArray();
        cfg.sync(); // ✅ Force write to disk
        dlg->accept();
        refreshDesignIcons();
    });
    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    mainLay->addWidget(bb);

    settingsTab->addTab(designTabW, "Design Style");
    outerLay->addWidget(settingsTab);
    settingsTab->setCurrentIndex(1);
    dlg->exec();
}

void ParagraphStylesPanel::refreshDesignIcons()
{
    if (!m_iconsGrid || !m_designTab) return;
    // Clear existing icons
    QLayoutItem* item;
    while ((item = m_iconsGrid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    // Reload from QSettings
    QSettings dsCfg("Scribus", "SuneerDesignStyle");
    int count = dsCfg.beginReadArray("styles");
    dsCfg.endArray();
    for (int i = 0; i < count; ++i) {
        dsCfg.beginReadArray("styles");
        dsCfg.setArrayIndex(i);
        QString iconPath = dsCfg.value("iconPath").toString();
        QString tooltip  = dsCfg.value("tooltip", QString("Style %1").arg(i+1)).toString();
        dsCfg.endArray();
        QPushButton* btn = new QPushButton(m_designTab);
        if (!iconPath.isEmpty()) btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(100, 100));
        btn->setFixedSize(110, 110);
        btn->setToolTip(tooltip);
        btn->setStyleSheet("QPushButton { border: 2px solid #ddd; border-radius: 4px; }"
                           "QPushButton:hover { border-color: #3498db; }");
        btn->show();
        m_iconsGrid->addWidget(btn, i/2, i%2);
    }
}

void ParagraphStylesPanel::applyDesignStyle(const QStringList& styles, const QString& imgPos, int cols, const QString& colBreak, double savedImgOffX, double savedImgOffY, double savedImgW, double savedImgH)
{
    if (!m_doc || !m_mainWindow) return;
    PageItem* selItem = m_doc->m_Selection->isEmpty() ? nullptr : m_doc->m_Selection->itemAt(0);
    PageItem_TextFrame* tf = selItem ? selItem->asTextFrame() : nullptr;
    if (!tf) { QMessageBox::warning(this, "Warning", "Select a text frame first!"); return; }

    // Apply paragraph styles
    int len = tf->itemText.length();
    int pos = 0;
    int paraIndex = 0;
    while (pos <= len && paraIndex < styles.size()) {
        int pEnd = pos;
        while (pEnd < len && tf->itemText.text(pEnd) != SpecialChars::PARSEP)
            pEnd++;
        const QString& sn = styles[paraIndex];
        if (!sn.isEmpty() && m_doc->paragraphStyles().contains(sn)) {
            const ParagraphStyle& ps = m_doc->paragraphStyles().get(sn);
            for (int p = pos; p <= pEnd && p < len; ++p)
                tf->itemText.applyStyle(p, ps);
            // Insert column break after this paragraph if style matches colBreak
            // Insert column break if style matches
            if (!colBreak.isEmpty() && pEnd < len) {
                QStringList colBreakStyles = colBreak.split(",", Qt::SkipEmptyParts);
                for (const QString& cbs : colBreakStyles) {
                    if (sn.trimmed() == cbs.trimmed()) {
                        tf->itemText.insertChars(pEnd, QString(SpecialChars::COLBREAK));
                        len = tf->itemText.length();
                        break;
                    }
                }
            }
        }
        pos = pEnd + 1;
        paraIndex++;
        if (pos > len) break;
    }
    // Apply column config by index
    if (cols >= 0) {
        loadColumnConfigs(); // Ensure latest config
        applyColumnConfig(cols);
    }
    // Create image frame if imgPos is not None
    if (imgPos != "None" && !imgPos.isEmpty()) {
        const double MM2PT = 2.8346;
        double tfX = tf->xPos();
        double tfY = tf->yPos();
        double tfW = tf->width();
        double tfH = tf->height();
        // Calculate column width and gap
        int numCols = tf->columns();
        double colGap = tf->columnGap();
        double singleColW = (numCols > 1) ? (tfW - colGap * (numCols - 1)) / numCols : tfW;
        // Default positions
        double imgX = tfX, imgY = tfY;
        double imgW = tfW - singleColW - colGap;
        double imgH = tfH;
        if (imgPos == "Right")
            imgX = tfX + singleColW + colGap;
        else if (imgPos == "Left")
            imgW = singleColW;
        else if (imgPos == "Top") {
            imgW = tfW;
            imgH = tfH * 0.4;
        }
        // Override with saved settings if non-zero
        if (savedImgOffX != 0.0) imgX = tfX + savedImgOffX * MM2PT;
        if (savedImgOffY != 0.0) imgY = tfY + savedImgOffY * MM2PT;
        if (savedImgW > 0.0) imgW = savedImgW * MM2PT;
        if (savedImgH > 0.0) imgH = savedImgH * MM2PT;
        PageItem* imgFrame = m_doc->createPageItem(
            PageItem::ImageFrame, PageItem::Rectangle,
            imgX, imgY, imgW, imgH,
            0, CommonStrings::None, CommonStrings::None);
        if (imgFrame) {
            imgFrame->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
            // ✅ Image border
            imgFrame->setLineWidth(0.5);
            imgFrame->setLineColor(CommonStrings::None);
            m_doc->DocItems.append(imgFrame);

            // ✅ Caption frame (same as canvasmode_imageimport)
            double captH = 10.0 * MM2PT;
            QString captName = QString("caption_%1").arg(imgFrame->itemName());
            PageItem* captFrame = m_doc->createPageItem(
                PageItem::TextFrame, PageItem::Rectangle,
                imgX, imgY + imgH, imgW, captH,
                0, CommonStrings::None, CommonStrings::None);
            if (captFrame) {
                captFrame->setItemName(captName);
                captFrame->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
                // Text distance 1.5mm
                captFrame->setTextToFrameDist(0.0, 0.0, 4.2519, 4.2519);
                // Caption border
                captFrame->setLineWidth(0.5);
                captFrame->setLineColor(CommonStrings::None);
                // Caption style
                if (m_doc->paragraphStyles().contains("09 Caption")) {
                    const ParagraphStyle& capStyle = m_doc->paragraphStyles().get("09 Caption");
                    captFrame->itemText.insertChars(0, QString(QChar(0x20)));
                    captFrame->itemText.applyStyle(0, capStyle);
                }
                m_doc->DocItems.append(captFrame);
                // ✅ Weld caption to image (move together)
                imgFrame->weldTo(captFrame);
                // Caption update via signal (exif load after image set)
                // updateCaptionFrame called when image is loaded via Ctrl+I
            }
        }
    }
    // Auto hyphenate
    if (m_doc->docHyphenator)
        m_doc->docHyphenator->slotHyphenate(tf);
    tf->invalidateLayout(true);
    tf->update();
    m_doc->changed();
    m_doc->regionsChanged()->update(QRectF());
    if (m_mainWindow) m_mainWindow->emitUpdateRequest(0);
    // Auto fit frame height
    if (m_mainWindow) m_mainWindow->suneerAutoFitHeight();
}
