#include "hyphenator.h"
#include <QScrollArea>
#include "ui/faircodehelpviewer.h"
#include <QToolButton>
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
#include "undomanager.h"
#include <QMessageBox>
#include <QDebug>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include "iconmanager.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeySequenceEdit>
#include <QCheckBox>
#include <QTimer>
#include <QShortcut>
#include <QSettings>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include "commonstrings.h"
#include "resourcecollection.h"
#include "scribusstructs.h"
#include "prefsmanager.h"
#include "scraction.h"
#include "ui/stylemanager.h"
#include "styles/charstyle.h"
#include <functional>
#include <QRegularExpression>
#include <QMenu>
#include <QApplication>

// ============================================================
// Template style source
//
// Scribus keeps no record of where a paragraph style came from, so a style
// pasted in from another .sla is indistinguishable from one defined in this
// document's own style set. Rather than guess from the name (production
// documents mix numbered styles like "02 BodyText" with plenty of
// non-numbered ones — "channel movie", "schedule", "Notification" — so a
// naming-pattern heuristic misclassifies real template styles), "template
// style" is defined as: any paragraph style name that appears in the
// canonical template file(s) on disk. Add a style to those files and it is
// recognized everywhere with no per-document or per-style upkeep.
static QString defaultTemplateSourcePath()
{
	return QDir::homePath() + "/Desktop/template";
}

static QString templateSourcePath()
{
	QSettings cfg("Scribus", "ParagraphStylesTemplateSource");
	return cfg.value("path", defaultTemplateSourcePath()).toString();
}

static void setTemplateSourcePath(const QString& path)
{
	QSettings cfg("Scribus", "ParagraphStylesTemplateSource");
	cfg.setValue("path", path);
}

static QStringList collectTemplateSlaFiles(const QString& path)
{
	QStringList result;
	QFileInfo fi(path);
	if (fi.isDir())
	{
		QDirIterator it(path, QStringList() << "*.sla", QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
			result << it.next();
	}
	else if (fi.isFile())
	{
		result << path;
	}
	return result;
}

// Cache keyed on the configured source path: the panel calls this on every
// list rebuild, and re-parsing every template .sla each time would be slow
// for a folder-based source. Reloaded on demand via reloadTemplateStyleNames().
struct TemplateStyleCache
{
	QSet<QString> names;
	QString sourcePath;
	bool loaded = false;
};
static TemplateStyleCache s_templateCache;

static const QSet<QString>& templateStyleNames(ScribusDoc* doc, bool forceReload = false)
{
	const QString path = templateSourcePath();
	if (!forceReload && s_templateCache.loaded && s_templateCache.sourcePath == path)
		return s_templateCache.names;

	QSet<QString> names;
	names.insert(CommonStrings::DefaultParagraphStyle);
	if (doc)
	{
		const QStringList files = collectTemplateSlaFiles(path);
		for (const QString& file : files)
		{
			// loadStylesFromFile() only fills the temp sets passed to it — it
			// never touches doc's own style set, colors or fonts, so reading
			// the template files here cannot affect the open document.
			StyleSet<ParagraphStyle> tempStyles;
			StyleSet<CharStyle> tempCharStyles;
			QHash<QString, MultiLine> tempLineStyles;
			doc->loadStylesFromFile(file, &tempStyles, &tempCharStyles, &tempLineStyles);
			for (int i = 0; i < tempStyles.count(); ++i)
				names.insert(tempStyles[i].name());
		}
	}
	s_templateCache.names = names;
	s_templateCache.sourcePath = path;
	s_templateCache.loaded = true;
	return s_templateCache.names;
}

// Per-style shortcut storage (in-memory, saved to QSettings)
static QMap<QString, QKeySequence> s_styleShortcuts;
// The document whose per-style shortcuts (ParagraphStyle::shortcut()) are
// live. Static because dynamicShortcuts() is consulted by the Keyboard
// Shortcuts preferences page without a panel instance.
static QPointer<ScribusDoc> s_shortcutDoc;

// Per-style keys as the Style Manager stores them: "" or a portable
// QKeySequence string on the style itself, saved with the .sla.
static QKeySequence styleOwnShortcut(const ScribusDoc* doc, const QString& styleName)
{
	if (!doc || !doc->paragraphStyles().contains(styleName))
		return QKeySequence();
	return QKeySequence(doc->paragraphStyles().get(styleName).shortcut(), QKeySequence::PortableText);
}

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
	QString designStyle;     // Design Style (by name) applied after the columns; empty = None
};

static QList<SuneerColumnConfigEntry> s_columnConfigs;

// Design Style names as the Design Style tab stores them (the "tooltip" field
// of the SuneerDesignStyle QSettings array), in array order.
static QStringList designStyleNames()
{
	QStringList names;
	QSettings dsCfg("Scribus", "SuneerDesignStyle");
	int count = dsCfg.beginReadArray("styles");
	for (int i = 0; i < count; ++i)
	{
		dsCfg.setArrayIndex(i);
		names << dsCfg.value("tooltip", QString("Style %1").arg(i + 1)).toString();
	}
	dsCfg.endArray();
	return names;
}

// Does design style name \a name stand for slot \a n (1-based)? Accepts
// "style-3", "style 3", "Style3", "3" - the names the Link button pairs up.
static bool designNameMatchesSlot(const QString& name, int n)
{
	static const QRegularExpression re("^(?:style[-_ ]?)?(\\d+)$", QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch m = re.match(name.trimmed());
	return m.hasMatch() && m.captured(1).toInt() == n;
}

static QString columnConfigLabel(int i, const SuneerColumnConfigEntry& e)
{
	QString styles = e.styles.join(", ");
	QString sc = e.shortcut.isEmpty() ? "No shortcut" : e.shortcut.toString();
	QString design = e.designStyle.isEmpty() ? QString("None") : e.designStyle;
	return QString("Config %1 | Styles: [%2] | Split: %3 | Cols: %4 | Design: %5 | Key: %6")
		.arg(i + 1).arg(styles).arg(e.columnSplitStyle).arg(e.columnNumber).arg(design).arg(sc);
}

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
		e.designStyle = cfg.value("designStyle").toString();
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
		cfg.setValue("designStyle", e.designStyle);
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

		// Design Style applied to the frame after the columns, as if its icon
		// had been clicked. "None" keeps the old columns-only behaviour.
		QHBoxLayout* designRow = new QHBoxLayout();
		designRow->addWidget(new QLabel("Design Style:", this));
		m_designCombo = new QComboBox(this);
		m_designCombo->addItem("None");
		m_designCombo->addItems(designStyleNames());
		if (!entry.designStyle.isEmpty())
		{
			int at = m_designCombo->findText(entry.designStyle);
			if (at < 0)
			{
				// Linked name no longer exists: keep it visible so the user sees
				// what the config points at and can pick something else.
				m_designCombo->addItem(entry.designStyle + " (missing)", entry.designStyle);
				at = m_designCombo->count() - 1;
			}
			m_designCombo->setCurrentIndex(at);
		}
		designRow->addWidget(m_designCombo, 1);
		leftLay->addLayout(designRow);
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
		if (m_designCombo && m_designCombo->currentIndex() > 0)
		{
			const QVariant data = m_designCombo->currentData();
			e.designStyle = data.isValid() ? data.toString() : m_designCombo->currentText();
		}
		return e;
	}

private:
	QCheckBox* m_useGuideGapChk = nullptr;
	QComboBox* m_designCombo = nullptr;
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
			// Check against other chains and the styles' own keys
			for (auto it = s_styleShortcuts.begin(); it != s_styleShortcuts.end(); ++it)
				if (!it.value().isEmpty() && it.value() == ks)
				{
					m_conflictLabel->setText("Warning: Key already used by chain of style: " + it.key());
					return;
				}
			if (s_shortcutDoc)
			{
				const StyleSet<ParagraphStyle>& st = s_shortcutDoc->paragraphStyles();
				for (int i = 0; i < st.count(); ++i)
					if (!st[i].shortcut().isEmpty() && QKeySequence(st[i].shortcut(), QKeySequence::PortableText) == ks)
					{
						m_conflictLabel->setText("Warning: Key is the shortcut of style: " + st[i].name());
						return;
					}
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

// Custom delegate to draw chain-link icon + shortcut on right side
class StyleItemDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	static constexpr int IconSize = 15;
	static constexpr int IconRightMargin = 4;

	// Clickable area around the chain-link icon: icon bounds padded ~5px, full row
	// height. Shared by the hover-cursor and click hit-tests in the panel's
	// eventFilter so the two can never drift apart.
	static QRect chainHitRect(const QRect& rowRect)
	{
		const int left = rowRect.right() - IconRightMargin - IconSize - 5;
		return QRect(left, rowRect.top(), rowRect.right() - left + 1, rowRect.height());
	}

	// The iconset SVG uses stroke="currentColor"; Qt's SVG renderer has no palette,
	// so substitute the color textually before rasterizing. Cached per color/size/dpr.
	static QPixmap chainLinkPixmap(const QColor& color, int size, qreal dpr)
	{
		static QHash<QString, QPixmap> cache;
		const QString key = QString("%1|%2|%3").arg(color.name()).arg(size).arg(dpr);
		auto it = cache.constFind(key);
		if (it != cache.constEnd())
			return *it;

		QPixmap pm(qRound(size * dpr), qRound(size * dpr));
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);

		QFile f(IconManager::instance().pathForIcon("16/next-style-chain.svg"));
		if (f.open(QIODevice::ReadOnly))
		{
			QByteArray svg = f.readAll();
			svg.replace("currentColor", color.name().toLatin1());
			QSvgRenderer renderer(svg);
			QPainter p(&pm);
			renderer.render(&p, QRectF(0, 0, size, size));
		}
		cache.insert(key, pm);
		return pm;
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyledItemDelegate::paint(painter, option, index);
		bool hasChain = index.data(Qt::UserRole + 1).toBool();
		// The style's own key (UserRole + 2, from ParagraphStyle::shortcut()).
		// Shown for every row; a chain key is shown next to the chain icon.
		QString ownKey = index.data(Qt::UserRole + 2).toString();
		if (!hasChain && ownKey.isEmpty())
			return;

		painter->save();
		bool selected = (option.state & QStyle::State_Selected);
		// Colors from the option's palette at paint time: the theme can
		// change while we run, and cached colors would go stale.
		QColor iconColor = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Link);
		painter->setPen(selected ? option.palette.color(QPalette::HighlightedText)
		                         : option.palette.color(QPalette::Disabled, QPalette::Text));
		QFont sf = painter->font();
		sf.setPointSize(8);
		painter->setFont(sf);

		int rightEdge = option.rect.right() - IconRightMargin;
		if (hasChain)
		{
			// Chain-link icon, right-aligned and vertically centered, with the
			// chain key to its left.
			const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
			QPoint iconPos(option.rect.right() - IconSize - IconRightMargin,
			               option.rect.top() + (option.rect.height() - IconSize) / 2);
			painter->drawPixmap(iconPos, chainLinkPixmap(iconColor, IconSize, dpr));
			rightEdge = iconPos.x() - 4;

			QString styleName = index.data(Qt::UserRole).toString();
			QKeySequence chainKey = s_styleShortcuts.value(styleName);
			if (!chainKey.isEmpty())
			{
				QString text = chainKey.toString(QKeySequence::NativeText);
				int w = painter->fontMetrics().horizontalAdvance(text);
				QRect r(rightEdge - w, option.rect.top(), w, option.rect.height());
				painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, text);
				rightEdge -= w + 10;
			}
		}
		if (!ownKey.isEmpty())
		{
			// With a chain key beside it, label this one so the two read apart.
			QString text = QKeySequence(ownKey, QKeySequence::PortableText).toString(QKeySequence::NativeText);
			if (hasChain && !s_styleShortcuts.value(index.data(Qt::UserRole).toString()).isEmpty())
				text = QString::fromUtf8("\xe2\x8c\xa8 ") + text; // ⌨ style key, vs. chain key
			int w = painter->fontMetrics().horizontalAdvance(text);
			QRect r(rightEdge - w, option.rect.top(), w, option.rect.height());
			painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, text);
		}
		painter->restore();
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

	// Theme-aware: palette() roles resolve against the active light or dark
	// palette and re-resolve when it changes, so nothing here can go
	// dark-on-dark. Hardcoded pairs are only allowed where BOTH foreground and
	// background are set together (the colored action buttons).
	setStyleSheet(
		"QDockWidget { background: palette(window); }"
		"QDockWidget::title { background: palette(highlight); color: palette(highlighted-text); padding: 8px; font-weight: bold; }"
	);

	QWidget* mainWidget = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(mainWidget);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	// ── Tab Widget ──
	QTabWidget* tabWidget = new QTabWidget(this);
	m_tabWidget = tabWidget;
	tabWidget->setStyleSheet(
		"QTabWidget::pane { border: 1px solid palette(mid); }"
		"QTabBar::tab { padding: 6px 12px; font-size: 10pt; background: palette(window); color: palette(window-text); }"
		"QTabBar::tab:selected { background: palette(highlight); color: palette(highlighted-text); }"
	);

	// Tab 1: Styles
	QWidget* stylesTab = new QWidget();
	QVBoxLayout* stylesLayout = new QVBoxLayout(stylesTab);
	stylesLayout->setContentsMargins(4, 4, 4, 4);
	stylesLayout->setSpacing(8);

	m_searchBox = new QLineEdit(this);
	m_searchBox->setPlaceholderText("Search styles...");
	m_searchBox->setStyleSheet(
		"QLineEdit { padding: 8px; border: 2px solid palette(mid); border-radius: 4px;"
		"  background: palette(base); color: palette(text); font-size: 11pt; }"
		"QLineEdit:focus { border-color: palette(highlight); }"
	);
	stylesLayout->addWidget(m_searchBox);

	QHBoxLayout* templateFilterLayout = new QHBoxLayout();
	templateFilterLayout->setSpacing(6);
	m_templateOnlyCheck = new QCheckBox("Template styles only", this);
	{
		QSettings cfg("Scribus", "ParagraphStylesPanel");
		m_templateOnlyCheck->setChecked(cfg.value("templateStylesOnly", true).toBool());
	}
	m_templateOnlyCheck->setToolTip(
		"Hide paragraph styles that aren't part of the template files at\n" + defaultTemplateSourcePath() +
		" (e.g. styles imported by pasting text from another document).\n"
		"Nothing is deleted — this only affects what's shown in this list.");
	templateFilterLayout->addWidget(m_templateOnlyCheck);
	templateFilterLayout->addStretch();
	QToolButton* templateSourceBtn = new QToolButton(this);
	templateSourceBtn->setText("\xe2\x9a\x99"); // ⚙
	templateSourceBtn->setToolTip("Template style source, style shortcut import/export");
	templateSourceBtn->setPopupMode(QToolButton::InstantPopup);
	{
		QMenu* gearMenu = new QMenu(templateSourceBtn);
		gearMenu->addAction("Template style source...", this, &ParagraphStylesPanel::openTemplateSourceSettings);
		gearMenu->addSeparator();
		gearMenu->addAction("Export style shortcuts...", this, &ParagraphStylesPanel::exportStyleShortcuts);
		gearMenu->addAction("Import style shortcuts...", this, &ParagraphStylesPanel::importStyleShortcuts);
		templateSourceBtn->setMenu(gearMenu);
	}
	templateFilterLayout->addWidget(templateSourceBtn);
	stylesLayout->addLayout(templateFilterLayout);
	connect(m_templateOnlyCheck, &QCheckBox::toggled, this, &ParagraphStylesPanel::toggleTemplateOnly);

	m_stylesList = new QListWidget(this);
	m_stylesList->setAlternatingRowColors(false);
	m_stylesList->setStyleSheet(
		"QListWidget { background: palette(base); color: palette(text); border: 1px solid palette(mid); border-radius: 4px; outline: none; }"
		"QListWidget::item { padding: 12px 12px; border-bottom: 1px solid palette(alternate-base); font-size: 11pt; }"
		"QListWidget::item:hover { background: palette(alternate-base); }"
		"QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); border: none; }"
	);
	stylesLayout->addWidget(m_stylesList);
	m_stylesList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_stylesList, &QListWidget::customContextMenuRequested, this, &ParagraphStylesPanel::showStylesContextMenu);

	QLabel* infoLabel = new QLabel("Frame: Para1->Style, Para2->Next\nEdit: From cursor down", this);
	infoLabel->setWordWrap(true);
	infoLabel->setStyleSheet(
		"QLabel { color: palette(text); font-size: 9pt; padding: 8px; background: palette(alternate-base); border-radius: 4px; }"
	);
	stylesLayout->addWidget(infoLabel);

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->setSpacing(4);

	QPushButton* setNextButton = new QPushButton("Next", this);
	setNextButton->setToolTip("Set Next Style Chain & Shortcut");
	setNextButton->setStyleSheet(
		"QPushButton { padding: 8px 16px; background: #3498db; color: white;"
		"  border: none; border-radius: 4px; font-size: 10pt; }"
		"QPushButton:hover { background: #2980b9; }"
		"QPushButton:pressed { background: #21618c; }"
	);

	// 32px circles (were 36): with the shortcut button added the row must
	// still fit the dock at its usual width, or the panel scrolls sideways.
	m_newButton = new QPushButton("+", this);
	m_newButton->setFixedSize(32, 32);
	m_newButton->setEnabled(false);
	m_newButton->setStyleSheet(
		"QPushButton { background: #95a5a6; color: white; border: none; border-radius: 16px; font-size: 14pt; }"
	);

	m_deleteButton = new QPushButton("-", this);
	m_deleteButton->setFixedSize(32, 32);
	m_deleteButton->setEnabled(false);
	m_deleteButton->setStyleSheet(m_newButton->styleSheet());

	m_editButton = new QPushButton("\xe2\x9c\x8e", this);
	m_editButton->setFixedSize(32, 32);
	m_editButton->setEnabled(false);
	m_editButton->setStyleSheet(m_newButton->styleSheet());

	m_shortcutButton = new QPushButton("\xe2\x8c\xa8", this); // ⌨
	m_shortcutButton->setFixedSize(32, 32);
	m_shortcutButton->setToolTip("Assign a keyboard shortcut to the selected style");
	m_shortcutButton->setStyleSheet(m_newButton->styleSheet());
	connect(m_shortcutButton, &QPushButton::clicked, this, &ParagraphStylesPanel::assignShortcut);

	QPushButton* cleanupButton = new QPushButton("Clean Up", this);
	cleanupButton->setToolTip(
		"Remove imported paragraph styles that aren't applied to any text in this document.\n"
		"Template styles and styles currently in use are never removed.");
	cleanupButton->setStyleSheet(
		"QPushButton { padding: 8px 12px; background: #7f8c8d; color: white;"
		"  border: none; border-radius: 4px; font-size: 10pt; }"
		"QPushButton:hover { background: #636e72; }"
	);
	connect(cleanupButton, &QPushButton::clicked, this, &ParagraphStylesPanel::cleanupImportedStyles);

	buttonLayout->addWidget(setNextButton);
	buttonLayout->addWidget(cleanupButton);
	// colConfigBtn removed — Column Style is now a tab
	buttonLayout->addStretch();
	buttonLayout->addWidget(m_newButton);
	buttonLayout->addWidget(m_deleteButton);
	buttonLayout->addWidget(m_editButton);
	buttonLayout->addWidget(m_shortcutButton);

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

	// One help button in the tab bar corner rather than one per feature area:
	// it follows the current tab, so it stays in one place and adds no clutter.
	QToolButton* helpBtn = FaircodeHelpViewer::makeHelpButton(tabWidget, tr("Help for this tab"));
	tabWidget->setCornerWidget(helpBtn, Qt::TopRightCorner);
	connect(helpBtn, &QToolButton::clicked, this, [this]() {
		QString anchor = QStringLiteral("top");
		if (m_tabWidget)
		{
			QWidget* page = m_tabWidget->currentWidget();
			// The News Browser arrives via addExtraTab(), which wraps its guest in
			// a scroll area and tags it; the two built-in tabs go by index.
			if (qobject_cast<QScrollArea*>(page) && page->property("suneerExtraTabPage").isValid())
				anchor = QStringLiteral("news-browser");
			else if (m_tabWidget->currentIndex() == 1)
				anchor = QStringLiteral("design-style");
			else if (m_tabWidget->currentIndex() == 0)
				anchor = QStringLiteral("styles-tab");
		}
		FaircodeHelpViewer::showTopic(this, anchor);
	});
	layout->addWidget(tabWidget);
	setWidget(mainWidget);

	connect(m_searchBox, &QLineEdit::textChanged, this, &ParagraphStylesPanel::filterStyles);
	connect(setNextButton, &QPushButton::clicked, this, &ParagraphStylesPanel::setNextStyle);

	m_stylesList->setItemDelegate(new StyleItemDelegate(m_stylesList));
	m_stylesList->viewport()->installEventFilter(this);
	// Needed for the hand-cursor hover feedback over the chain-link icon.
	m_stylesList->viewport()->setMouseTracking(true);
}

bool ParagraphStylesPanel::eventFilter(QObject* obj, QEvent* event)
{
	// While the pointer is pressed on the list, suppress the background sync
	// timer / docChanged rebuilds so they can't reselect or scroll the list
	// out from under the click (which would make itemAt(pos) resolve to the
	// wrong row). Qt grabs the mouse on press, so the matching release is
	// always delivered to this viewport and the flag is cleared reliably.
	if (obj == m_stylesList->viewport() && event->type() == QEvent::MouseButtonPress)
		m_userInteracting = true;

	// Hand cursor while hovering the chain-link icon's (padded) hit area, so it
	// reads as clickable. Same rect as the click test below via chainHitRect().
	if (obj == m_stylesList->viewport() && event->type() == QEvent::MouseMove)
	{
		QWidget* vp = m_stylesList->viewport();
		QMouseEvent* me = static_cast<QMouseEvent*>(event);
		QListWidgetItem* item = m_stylesList->itemAt(me->pos());
		bool overChain = item && item->data(Qt::UserRole + 1).toBool()
			&& StyleItemDelegate::chainHitRect(m_stylesList->visualItemRect(item)).contains(me->pos());
		if (overChain != (vp->cursor().shape() == Qt::PointingHandCursor))
		{
			if (overChain)
				vp->setCursor(Qt::PointingHandCursor);
			else
				vp->unsetCursor();
		}
	}
	if (obj == m_stylesList->viewport() && event->type() == QEvent::Leave)
		m_stylesList->viewport()->unsetCursor();

	if (obj == m_stylesList->viewport() && event->type() == QEvent::MouseButtonRelease)
	{
		// Clear before applying: the apply below emits docChanged, and we
		// want the resulting updateStylesList() rebuild to run normally.
		m_userInteracting = false;
		QMouseEvent* me = static_cast<QMouseEvent*>(event);
		QListWidgetItem* item = m_stylesList->itemAt(me->pos());
		if (item)
		{
			QString styleName = item->data(Qt::UserRole).toString();
			bool hasChain = item->data(Qt::UserRole + 1).toBool();
			bool clickedChain = hasChain
				&& StyleItemDelegate::chainHitRect(m_stylesList->visualItemRect(item)).contains(me->pos());
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
	s_shortcutDoc = doc;
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
	// Don't reselect/scroll the list while the user is clicking in it —
	// doing so moves rows under the pointer and breaks the click's
	// itemAt(pos) lookup (wrong-style-on-first-click bug).
	if (m_userInteracting)
		return;
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

void ParagraphStylesPanel::changeEvent(QEvent* event)
{
	QDockWidget::changeEvent(event);
	// The stylesheet's palette() references re-resolve on repolish, but the
	// chained-style item foregrounds were read from the palette when the list
	// was filled — refresh them so a live light/dark switch recolors them too.
	if (event->type() == QEvent::PaletteChange)
		updateStylesList();
}

void ParagraphStylesPanel::updateStylesList()
{
	// Skip the clear()+rebuild while the user is pressing on the list: a
	// docChanged fired mid-click would otherwise delete the row being
	// clicked. The post-release apply clears the flag first, so the list
	// is still rebuilt right after the click completes.
	if (m_userInteracting)
		return;
	// clear() drops the current row; the buttons below the list (Next, edit,
	// shortcut) act on it, so put it back by name after the rebuild.
	const QString previousCurrent = m_stylesList->currentItem()
		? m_stylesList->currentItem()->data(Qt::UserRole).toString() : QString();
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
		item->setData(Qt::UserRole + 2, style.shortcut());
		if (!style.shortcut().isEmpty())
			item->setToolTip("Shortcut: " + QKeySequence(style.shortcut(), QKeySequence::PortableText).toString(QKeySequence::NativeText));
		if (hasChain)
		{
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
			item->setForeground(m_stylesList->palette().color(QPalette::Link));
			QStringList chainList = nextStyle.split(";");
			item->setToolTip("Next style: " + chainList.join(" -> "));
		}
		m_stylesList->addItem(item);
		if (styleName == previousCurrent)
			m_stylesList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
	}
	applyListFilters();
}

void ParagraphStylesPanel::filterStyles(const QString& filter)
{
	Q_UNUSED(filter);
	applyListFilters();
}

void ParagraphStylesPanel::applyListFilters()
{
	const QString filter = m_searchBox->text();
	const bool templateOnly = m_templateOnlyCheck && m_templateOnlyCheck->isChecked();
	const QSet<QString>& templateNames = templateOnly ? templateStyleNames(m_doc) : QSet<QString>();
	for (int i = 0; i < m_stylesList->count(); ++i)
	{
		QListWidgetItem* item = m_stylesList->item(i);
		QString name = item->data(Qt::UserRole).toString();
		bool matchesText = name.contains(filter, Qt::CaseInsensitive);
		bool matchesTemplate = !templateOnly || templateNames.contains(name);
		item->setHidden(!(matchesText && matchesTemplate));
	}
}

void ParagraphStylesPanel::toggleTemplateOnly(bool checked)
{
	QSettings cfg("Scribus", "ParagraphStylesPanel");
	cfg.setValue("templateStylesOnly", checked);
	applyListFilters();
}

void ParagraphStylesPanel::openTemplateSourceSettings()
{
	QDialog dlg(this);
	dlg.setWindowTitle("Template Style Source");
	QVBoxLayout* layout = new QVBoxLayout(&dlg);

	QLabel* info = new QLabel(
		"A paragraph style counts as a \"template style\" if its name appears in\n"
		"one of the .sla files below (a single file, or every .sla under a folder).\n"
		"Anything else — e.g. a style pulled in by pasting text from another\n"
		"document — is treated as imported and hidden when \"Template styles only\" is on.",
		&dlg);
	info->setWordWrap(true);
	layout->addWidget(info);

	QHBoxLayout* pathLayout = new QHBoxLayout();
	QLineEdit* pathEdit = new QLineEdit(templateSourcePath(), &dlg);
	QPushButton* browseFileBtn = new QPushButton("File...", &dlg);
	QPushButton* browseFolderBtn = new QPushButton("Folder...", &dlg);
	pathLayout->addWidget(pathEdit);
	pathLayout->addWidget(browseFileBtn);
	pathLayout->addWidget(browseFolderBtn);
	layout->addLayout(pathLayout);

	connect(browseFileBtn, &QPushButton::clicked, &dlg, [&dlg, pathEdit]() {
		QString path = QFileDialog::getOpenFileName(&dlg, "Choose Template File", pathEdit->text(), "Scribus Files (*.sla)");
		if (!path.isEmpty())
			pathEdit->setText(path);
	});
	connect(browseFolderBtn, &QPushButton::clicked, &dlg, [&dlg, pathEdit]() {
		QString path = QFileDialog::getExistingDirectory(&dlg, "Choose Template Folder", pathEdit->text());
		if (!path.isEmpty())
			pathEdit->setText(path);
	});

	QLabel* countLabel = new QLabel(&dlg);
	auto refreshCount = [this, countLabel, pathEdit]() {
		setTemplateSourcePath(pathEdit->text());
		const QSet<QString>& names = templateStyleNames(m_doc, true);
		countLabel->setText(QString("%1 template style name(s) found.").arg(names.count()));
	};
	refreshCount();
	layout->addWidget(countLabel);

	QPushButton* refreshBtn = new QPushButton("Refresh", &dlg);
	connect(refreshBtn, &QPushButton::clicked, &dlg, [refreshCount]() { refreshCount(); });
	layout->addWidget(refreshBtn);

	QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	layout->addWidget(box);
	connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	QString previousPath = templateSourcePath();
	if (dlg.exec() == QDialog::Accepted)
	{
		setTemplateSourcePath(pathEdit->text());
		templateStyleNames(m_doc, true);
	}
	else
	{
		setTemplateSourcePath(previousPath);
	}
	applyListFilters();
}

void ParagraphStylesPanel::cleanupImportedStyles()
{
	if (!m_doc)
		return;

	const QSet<QString>& templateNames = templateStyleNames(m_doc);
	ResourceCollection usedResources;
	m_doc->getUsedStylesFromItems(usedResources);

	QStringList toRemove;
	int styleCount = m_doc->paragraphStyles().count();
	for (int i = 0; i < styleCount; ++i)
	{
		const ParagraphStyle& ps = m_doc->paragraphStyles()[i];
		if (ps.isDefaultStyle() || !ps.hasName())
			continue;
		if (templateNames.contains(ps.name()))
			continue; // template style: never auto-removed, used or not
		if (usedResources.styles().contains(ps.name()))
			continue; // still applied to text somewhere: never remove
		toRemove.append(ps.name());
	}

	if (toRemove.isEmpty())
	{
		QMessageBox::information(this, "Clean Up Imported Styles",
			"No unused imported styles found. Nothing to remove.");
		return;
	}

	QMessageBox::StandardButton reply = QMessageBox::question(this, "Clean Up Imported Styles",
		QString("Remove %1 unused imported style(s)?\n\n%2").arg(toRemove.count()).arg(toRemove.join("\n")),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (reply != QMessageBox::Yes)
		return;

	StyleSet<ParagraphStyle> keptStyles;
	for (int i = 0; i < styleCount; ++i)
	{
		const ParagraphStyle& ps = m_doc->paragraphStyles()[i];
		if (!toRemove.contains(ps.name()))
			keptStyles.create(ps);
	}
	m_doc->redefineStyles(keptStyles, true);
	m_doc->changed();
	updateStylesList();
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

namespace
{
	// StoryText records no undo of its own, so a paragraph style written straight
	// into it is invisible to Ctrl+Z. This mirrors the state that
	// ScribusDoc::itemSelection_ApplyParagraphStyle() records and that
	// PageItem::restoreParagraphStyle() already knows how to reverse.
	void suneerRecordAndApplyParaStyle(PageItem* item, StoryText& text,
	                                   int pos, const ParagraphStyle& ps)
	{
		if (UndoManager::undoEnabled())
		{
			auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
			is->set("APPLY_PARASTYLE");
			is->set("POS", pos);
			is->setStates(text.paragraphStyle(pos), ps);
			UndoManager::instance()->action(item, is);
		}
		text.applyStyle(pos, ps);
	}
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

	// StoryText has no undo hooks of its own, so applying styles straight into
	// itemText records nothing and Ctrl+Z cannot reach it. Wrap the whole chain
	// in one transaction and log a state per paragraph, exactly as
	// ScribusDoc::itemSelection_ApplyParagraphStyle() does.
	UndoManager* undoManager = UndoManager::instance();
	UndoTransaction chainTransaction;
	if (UndoManager::undoEnabled())
		chainTransaction = undoManager->beginTransaction(textFrame->getUName(), textFrame->getUPixmap(),
		                                                 Um::ApplyTextStyle, startStyle, Um::IFont);

	int pos = paraStart;
	for (int ci = 0; ci < chain.count() && pos <= len; ++ci)
	{
		const QString& sn = chain[ci];
		int pEnd = pos;
		while (pEnd < len && textFrame->itemText.text(pEnd) != SpecialChars::PARSEP)
			pEnd++;
		if (pos < len && m_doc->paragraphStyles().contains(sn))
		{
			const ParagraphStyle& ps = m_doc->paragraphStyles().get(sn);
			if (UndoManager::undoEnabled())
			{
				auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
				is->set("APPLY_PARASTYLE");
				is->set("POS", pos);
				is->setStates(textFrame->itemText.paragraphStyle(pos), ps);
				undoManager->action(textFrame, is);
			}
			// applyStyle() scans forward to the paragraph's terminator and applies
			// there, so one call per paragraph does the whole paragraph. The old
			// loop over every character position repeated the same work.
			textFrame->itemText.applyStyle(pos, ps);
		}
		pos = pEnd + 1;
	}

	if (chainTransaction)
		chainTransaction.commit();

	textFrame->update();
	textFrame->invalidateLayout(true);
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

// The News Browser tab is optional and its widget belongs to SuneerNewsPanel,
// which stays alive as the owner of the network manager and every slot behind
// the UI. Only the visible widget is reparented in here; nothing about the
// panel's own QShortcut lifetime handling is touched.
void ParagraphStylesPanel::addExtraTab(QWidget* page, const QString& label)
{
	if (!m_tabWidget || !page || m_tabWidget->indexOf(page) >= 0)
		return;
	// A scroll area keeps a wide guest widget from imposing its minimum width on
	// the whole docker, which would make the panel jump when switching tabs.
	QScrollArea* scroller = new QScrollArea(m_tabWidget);
	scroller->setWidgetResizable(true);
	scroller->setFrameShape(QFrame::NoFrame);
	scroller->setWidget(page);
	scroller->setProperty("suneerExtraTabPage", QVariant::fromValue<QObject*>(page));
	m_tabWidget->addTab(scroller, label);
}

void ParagraphStylesPanel::removeExtraTab(QWidget* page)
{
	if (!m_tabWidget || !page)
		return;
	for (int i = m_tabWidget->count() - 1; i >= 0; --i)
	{
		QScrollArea* scroller = qobject_cast<QScrollArea*>(m_tabWidget->widget(i));
		if (!scroller || scroller->property("suneerExtraTabPage").value<QObject*>() != page)
			continue;
		// Hand the widget back before the scroll area dies, or takeWidget()'s owner
		// would take it down with the tab.
		scroller->takeWidget();
		page->setParent(nullptr);
		page->hide();
		m_tabWidget->removeTab(i);
		scroller->deleteLater();
		return;
	}
}

bool ParagraphStylesPanel::showExtraTab(QWidget* page)
{
	if (!m_tabWidget || !page)
		return false;
	for (int i = 0; i < m_tabWidget->count(); ++i)
	{
		QScrollArea* scroller = qobject_cast<QScrollArea*>(m_tabWidget->widget(i));
		if (scroller && scroller->property("suneerExtraTabPage").value<QObject*>() == page)
		{
			m_tabWidget->setCurrentIndex(i);
			return true;
		}
	}
	return false;
}

bool ParagraphStylesPanel::hasExtraTab(QWidget* page) const
{
	if (!m_tabWidget || !page)
		return false;
	for (int i = 0; i < m_tabWidget->count(); ++i)
	{
		QScrollArea* scroller = qobject_cast<QScrollArea*>(m_tabWidget->widget(i));
		if (scroller && scroller->property("suneerExtraTabPage").value<QObject*>() == page)
			return true;
	}
	return false;
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
        QListWidgetItem* item = new QListWidgetItem(columnConfigLabel(i, s_columnConfigs[i]));
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
	// configIndex was captured when the shortcut was created; the config list can have been
	// edited or shrunk since, so re-validate against the current size (and guard negatives).
	if (configIndex < 0 || configIndex >= s_columnConfigs.size()) return;
	const SuneerColumnConfigEntry& e = s_columnConfigs[configIndex];
	PageItem* selItem = m_doc->m_Selection->isEmpty() ? nullptr : m_doc->m_Selection->itemAt(0);
	PageItem_TextFrame* tf = selItem ? selItem->asTextFrame() : nullptr;
	if (!tf) { QMessageBox::warning(this, "Warning", "Select a text frame first!"); return; }
	const bool outerCall = !m_inColumnConfig;
	m_inColumnConfig = true;

	// One undo step for the whole config: column count, gap, frame width, the
	// paragraph styles, auto-fit and the linked Design Style all belong to a
	// single key press. (The transaction used to start after the column
	// changes, so sizeItem() left its own separate undo step.)
	UndoTransaction cfgTransaction;
	if (UndoManager::undoEnabled())
		cfgTransaction = UndoManager::instance()->beginTransaction(tf->getUName(), tf->getUPixmap(),
		                                                           Um::SetStyle, QString(), Um::IFont);

	// Set frame columns
	// Convert mm to points (1 mm = 2.8346 pts)
	const double MM2PT = 2.8346;
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
				// applyStyle() scans forward to the paragraph terminator, so one call
				// per paragraph does the whole paragraph and keeps one undo state.
				if (pos < len)
					suneerRecordAndApplyParaStyle(tf, tf->itemText, pos, ps);
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
	// AutoFit — inside the transaction so the height change undoes with the rest.
	if (e.autoFit)
		tf->autoFitFrameHeight();

	// Linked Design Style, applied to the same frame as if its icon had been
	// clicked - still inside the transaction, so one Ctrl+Z reverts both.
	// Not when a Design Style is what called us (its own "columns" field):
	// that would loop config -> design -> config.
	if (!e.designStyle.isEmpty() && !m_applyingDesignStyle)
	{
		const int designIdx = designStyleNames().indexOf(e.designStyle);
		if (designIdx < 0)
			m_mainWindow->setStatusBarInfoText(QString("Design style '%1' not found").arg(e.designStyle));
		else
			applyDesignStyleByIndex(designIdx, /*withColumns=*/false);
	}
	if (cfgTransaction)
		cfgTransaction.commit();
	if (outerCall)
		m_inColumnConfig = false;
	// Force redraw
}

bool ParagraphStylesPanel::applyDesignStyleByIndex(int index, bool withColumns)
{
	QSettings dsCfg("Scribus", "SuneerDesignStyle");
	const int count = dsCfg.beginReadArray("styles");
	if (index < 0 || index >= count)
	{
		dsCfg.endArray();
		return false;
	}
	dsCfg.setArrayIndex(index);
	QStringList styles = dsCfg.value("styles").toStringList();
	QString imgPos = dsCfg.value("imagePosition", "Right").toString();
	int cols = withColumns ? dsCfg.value("columns", 3).toInt() : -1;
	QString colBreak = dsCfg.value("colBreakStyle").toString();
	double sImgX = dsCfg.value("imgOffsetX", 0.0).toDouble();
	double sImgY = dsCfg.value("imgOffsetY", 0.0).toDouble();
	double sImgW = dsCfg.value("imgWidth", 0.0).toDouble();
	double sImgH = dsCfg.value("imgHeight", 0.0).toDouble();
	dsCfg.endArray();
	applyDesignStyle(styles, imgPos, cols, colBreak, sImgX, sImgY, sImgW, sImgH);
	return true;
}

QMap<QString, QKeySequence> ParagraphStylesPanel::dynamicShortcuts()
{
	QMap<QString, QKeySequence> out;
	for (auto it = s_styleShortcuts.constBegin(); it != s_styleShortcuts.constEnd(); ++it)
	{
		if (it.value().isEmpty())
			continue;
		out.insert(tr("style chain \"%1\"").arg(it.key()), it.value());
	}
	if (s_shortcutDoc)
	{
		const StyleSet<ParagraphStyle>& styles = s_shortcutDoc->paragraphStyles();
		for (int i = 0; i < styles.count(); ++i)
		{
			if (styles[i].shortcut().isEmpty())
				continue;
			out.insert(tr("paragraph style \"%1\"").arg(styles[i].name()),
			           QKeySequence(styles[i].shortcut(), QKeySequence::PortableText));
		}
	}
	for (int i = 0; i < s_columnConfigs.size(); ++i)
	{
		const SuneerColumnConfigEntry& e = s_columnConfigs[i];
		if (e.shortcut.isEmpty())
			continue;
		out.insert(tr("column config %1").arg(i + 1), e.shortcut);
	}
	return out;
}

void ParagraphStylesPanel::rebuildColumnShortcuts()
{
	// A rebuild can be reached from inside a shortcut's own activated() handler (the slots
	// below open modal dialogs, which spin a nested event loop). Deleting the emitting
	// QShortcut outright would unwind into freed memory, so disable it now so it cannot
	// fire again, and let the event loop reclaim it once the stack is clear.
	for (QShortcut* sc : m_columnShortcuts)
	{
		if (!sc)
			continue;
		sc->setEnabled(false);
		sc->deleteLater();
	}
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
	// See rebuildColumnShortcuts(): never delete a QShortcut that may be mid-emit.
	for (QShortcut* sc : m_shortcuts)
	{
		if (!sc)
			continue;
		sc->setEnabled(false);
		sc->deleteLater();
	}
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
	rebuildStyleShortcuts();
}

void ParagraphStylesPanel::rebuildStyleShortcuts()
{
	// See rebuildColumnShortcuts(): never delete a QShortcut that may be mid-emit.
	for (QShortcut* sc : m_styleShortcuts)
	{
		if (!sc)
			continue;
		sc->setEnabled(false);
		sc->deleteLater();
	}
	m_styleShortcuts.clear();
	if (!m_doc)
		return;

	// One QShortcut per style that carries a key in ParagraphStyle::shortcut().
	// The Style Manager makes ScrActions for the same keys, but never attaches
	// them to a widget, so they cannot fire and cannot make ours ambiguous.
	QWidget* parent = m_mainWindow ? m_mainWindow : (parentWidget() ? parentWidget() : this);
	const StyleSet<ParagraphStyle>& styles = m_doc->paragraphStyles();
	for (int i = 0; i < styles.count(); ++i)
	{
		QKeySequence key(styles[i].shortcut(), QKeySequence::PortableText);
		if (key.isEmpty())
			continue;
		const QString styleName = styles[i].name();
		QShortcut* sc = new QShortcut(key, parent, nullptr, nullptr, Qt::ApplicationShortcut);
		connect(sc, &QShortcut::activated, this, [this, styleName]() {
			if (!m_doc || !m_doc->paragraphStyles().contains(styleName))
				return;
			// Edit mode: the paragraph at the cursor / all selected paragraphs.
			// Frame selected: the whole frame. Both with undo, as the Style
			// Manager's Apply does.
			m_doc->itemSelection_SetNamedParagraphStyle(styleName);
		});
		m_styleShortcuts.append(sc);
	}
}

// ---------------------------------------------------------------------------
// Per-style shortcuts (item's own key, saved in the .sla)
// ---------------------------------------------------------------------------

namespace
{
	// "Assign Shortcut" dialog: press a key, Clear / OK / Cancel.
	class StyleShortcutDialog : public QDialog
	{
	public:
		StyleShortcutDialog(const QString& styleName, const QKeySequence& current, QWidget* parent)
			: QDialog(parent)
		{
			setWindowTitle("Shortcut: " + styleName);
			setMinimumWidth(320);
			QVBoxLayout* lay = new QVBoxLayout(this);
			QHBoxLayout* row = new QHBoxLayout();
			row->addWidget(new QLabel("Press shortcut:", this));
			m_keyEdit = new QKeySequenceEdit(current, this);
			m_keyEdit->setMaximumSequenceLength(1);
			row->addWidget(m_keyEdit, 1);
			QPushButton* clearBtn = new QPushButton("Clear", this);
			clearBtn->setFixedWidth(50);
			connect(clearBtn, &QPushButton::clicked, this, [this]() { m_keyEdit->clear(); });
			row->addWidget(clearBtn);
			lay->addLayout(row);
			QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
			connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
			connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
			lay->addWidget(bb);
			m_keyEdit->setFocus();
			// The key editor keeps focus for a second after a key, so an Enter
			// pressed right after the shortcut would be recorded as the
			// shortcut. Hand focus to OK as soon as a key is recorded, and
			// never accept a bare Enter/Escape as the result.
			QPushButton* okBtn = bb->button(QDialogButtonBox::Ok);
			connect(m_keyEdit, &QKeySequenceEdit::editingFinished, this, [this, okBtn]() {
				if (isBareConfirmKey(m_keyEdit->keySequence()))
					m_keyEdit->setKeySequence(m_previous);
				else
					m_previous = m_keyEdit->keySequence();
				okBtn->setFocus();
			});
			m_previous = current;
		}
		QKeySequence key() const
		{
			QKeySequence k = m_keyEdit->keySequence();
			return isBareConfirmKey(k) ? m_previous : k;
		}
	private:
		static bool isBareConfirmKey(const QKeySequence& k)
		{
			if (k.count() != 1)
				return false;
			const int key = k[0].key();
			return k[0].keyboardModifiers() == Qt::NoModifier
				&& (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape);
		}
		QKeySequenceEdit* m_keyEdit;
		QKeySequence m_previous;
	};

	QString paraStyleShortcutsFilter() { return QString("Style shortcuts (*.json)"); }
}

void ParagraphStylesPanel::showStylesContextMenu(const QPoint& pos)
{
	QListWidgetItem* item = m_stylesList->itemAt(pos);
	if (!item || item->data(Qt::UserRole).toString().isEmpty())
		return;
	m_stylesList->setCurrentItem(item);
	QMenu menu(this);
	menu.addAction("Assign Shortcut...", this, &ParagraphStylesPanel::assignShortcut);
	menu.addAction("Next Style Chain...", this, &ParagraphStylesPanel::setNextStyle);
	menu.exec(m_stylesList->viewport()->mapToGlobal(pos));
}

void ParagraphStylesPanel::assignShortcut()
{
	if (!m_doc)
		return;
	QListWidgetItem* item = m_stylesList->currentItem();
	if (!item) { QMessageBox::warning(this, "Warning", "Select a style"); return; }
	QString styleName = item->data(Qt::UserRole).toString();
	if (styleName.isEmpty() || !m_doc->paragraphStyles().contains(styleName))
		return;

	StyleShortcutDialog dlg(styleName, styleOwnShortcut(m_doc, styleName), this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	QKeySequence key = dlg.key();
	if (key == styleOwnShortcut(m_doc, styleName))
		return;
	if (!key.isEmpty() && !resolveShortcutConflict(key, styleName))
		return;
	setStyleShortcut(styleName, key);
}

bool ParagraphStylesPanel::resolveShortcutConflict(const QKeySequence& key, const QString& forStyle)
{
	// Collect everything holding this key: label for the user, and a clearer
	// to run on "Replace".
	QStringList owners;
	QList<std::function<void()>> clearers;

	const StyleSet<ParagraphStyle>& pstyles = m_doc->paragraphStyles();
	for (int i = 0; i < pstyles.count(); ++i)
	{
		if (pstyles[i].name() == forStyle || pstyles[i].shortcut().isEmpty())
			continue;
		if (QKeySequence(pstyles[i].shortcut(), QKeySequence::PortableText) != key)
			continue;
		const QString other = pstyles[i].name();
		owners << QString("paragraph style \"%1\"").arg(other);
		clearers << [this, other]() { setStyleShortcut(other, QKeySequence()); };
	}
	const StyleSet<CharStyle>& cstyles = m_doc->charStyles();
	for (int i = 0; i < cstyles.count(); ++i)
	{
		if (cstyles[i].shortcut().isEmpty())
			continue;
		if (QKeySequence(cstyles[i].shortcut(), QKeySequence::PortableText) != key)
			continue;
		const QString other = cstyles[i].name();
		owners << QString("character style \"%1\" (Style Manager)").arg(other);
		clearers << [this, other]() {
			StyleSet<CharStyle> tmp;
			tmp.redefine(m_doc->charStyles(), true);
			if (tmp.contains(other))
			{
				tmp[tmp.find(other)].setShortcut(QString());
				m_doc->redefineCharStyles(tmp, false);
			}
		};
	}
	for (auto it = s_styleShortcuts.constBegin(); it != s_styleShortcuts.constEnd(); ++it)
	{
		if (it.value().isEmpty() || it.value() != key)
			continue;
		const QString other = it.key();
		owners << QString("Next Style Chain of \"%1\"").arg(other);
		clearers << [other]() { s_styleShortcuts.remove(other); saveShortcuts(); };
	}
	for (int i = 0; i < s_columnConfigs.size(); ++i)
	{
		if (s_columnConfigs[i].shortcut.isEmpty() || s_columnConfigs[i].shortcut != key)
			continue;
		owners << QString("column config %1").arg(i + 1);
		clearers << [i]() { if (i < s_columnConfigs.size()) { s_columnConfigs[i].shortcut = QKeySequence(); saveColumnConfigs(); } };
	}
	// Actions in the active keyboard shortcut set (e.g. Newspaper Default).
	QMap<QString, Keys>& keyActions = PrefsManager::instance().appPrefs.keyShortcutPrefs.KeyActions;
	bool actionConflict = false;
	for (auto it = keyActions.constBegin(); it != keyActions.constEnd(); ++it)
	{
		if (it.value().keySequence.isEmpty() || key.matches(it.value().keySequence) != QKeySequence::ExactMatch)
			continue;
		actionConflict = true;
		const QString actionName = it.key();
		QString label = it.value().cleanMenuText.isEmpty() ? actionName : it.value().cleanMenuText;
		if (!it.value().menuName.isEmpty())
			label = it.value().menuName + " > " + label;
		owners << QString("action \"%1\" in the current shortcut set").arg(label);
		clearers << [this, actionName]() {
			QMap<QString, Keys>& ka = PrefsManager::instance().appPrefs.keyShortcutPrefs.KeyActions;
			if (ka.contains(actionName))
				ka[actionName].keySequence = QKeySequence();
			if (m_mainWindow && m_mainWindow->scrActions.contains(actionName) && m_mainWindow->scrActions[actionName])
				m_mainWindow->scrActions[actionName]->setShortcut(QKeySequence());
		};
	}
	if (owners.isEmpty())
		return true;

	QString text = QString("%1 is already used by:\n\n  %2\n\nReplace it? The other owner loses the key.")
		.arg(key.toString(QKeySequence::NativeText), owners.join("\n  "));
	if (actionConflict)
		text += "\n\nNote: an action's key is cleared for this session only. A Default "
		        "shortcut set is applied again at the next start; change the set in "
		        "Preferences > Keyboard Shortcuts to make it permanent.";
	QMessageBox box(QMessageBox::Warning, "Shortcut in use", text, QMessageBox::NoButton, this);
	QPushButton* replaceBtn = box.addButton("Replace", QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();
	if (box.clickedButton() != replaceBtn)
		return false;
	for (const auto& clear : clearers)
		clear();
	// The cleared owner's QShortcut must go now: left alive next to the new
	// one, Qt sees an ambiguous key and fires neither.
	rebuildShortcuts();
	return true;
}

void ParagraphStylesPanel::setStyleShortcut(const QString& styleName, const QKeySequence& key)
{
	if (!m_doc)
		return;
	// Same write path as the chain: redefine the whole set with the one field
	// changed. Style Manager reads the same field, so it shows the same key.
	StyleSet<ParagraphStyle> tmpStyles;
	tmpStyles.redefine(m_doc->paragraphStyles(), true);
	if (!tmpStyles.contains(styleName))
		return;
	tmpStyles[tmpStyles.find(styleName)].setShortcut(key.isEmpty() ? QString() : key.toString(QKeySequence::PortableText));
	m_doc->redefineStyles(tmpStyles, false);
	m_doc->changed();
	// The Style Manager keeps its own action list keyed at load; re-seat it
	// on the doc so its Shortcut column matches. Its actions never fire (no
	// widget), so this is display only.
	if (m_mainWindow && m_mainWindow->styleMgr())
		m_mainWindow->styleMgr()->setDoc(m_doc);
	rebuildStyleShortcuts();
	updateStylesList();
}

void ParagraphStylesPanel::exportStyleShortcuts()
{
	if (!m_doc)
		return;
	QJsonObject styles;
	const StyleSet<ParagraphStyle>& pstyles = m_doc->paragraphStyles();
	for (int i = 0; i < pstyles.count(); ++i)
		if (!pstyles[i].shortcut().isEmpty())
			styles.insert(pstyles[i].name(), pstyles[i].shortcut());
	if (styles.isEmpty())
	{
		QMessageBox::information(this, "Export style shortcuts", "No paragraph style in this document has a shortcut.");
		return;
	}
	QString fn = QFileDialog::getSaveFileName(this, "Export style shortcuts",
		QDir::homePath() + "/style-shortcuts.json", paraStyleShortcutsFilter());
	if (fn.isEmpty())
		return;
	if (!fn.endsWith(".json", Qt::CaseInsensitive))
		fn += ".json";
	QJsonObject root;
	root.insert("format", "scribus-style-shortcuts");
	root.insert("version", 1);
	root.insert("paragraphStyles", styles);
	QFile f(fn);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		QMessageBox::warning(this, "Export style shortcuts", "Could not write " + fn);
		return;
	}
	f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	f.close();
	QMessageBox::information(this, "Export style shortcuts",
		QString("%1 shortcut(s) written to\n%2").arg(styles.size()).arg(fn));
}

void ParagraphStylesPanel::importStyleShortcuts()
{
	if (!m_doc)
		return;
	QString fn = QFileDialog::getOpenFileName(this, "Import style shortcuts", QDir::homePath(), paraStyleShortcutsFilter());
	if (fn.isEmpty())
		return;
	QFile f(fn);
	if (!f.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(this, "Import style shortcuts", "Could not read " + fn);
		return;
	}
	QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
	QJsonObject styles = root.value("paragraphStyles").toObject();
	if (styles.isEmpty())
	{
		QMessageBox::warning(this, "Import style shortcuts", "No style shortcuts found in " + fn);
		return;
	}

	// Apply what matches a style here; report the rest. Conflicts with keys
	// already in the document are asked about one by one, so nothing is
	// silently overwritten.
	StyleSet<ParagraphStyle> tmpStyles;
	tmpStyles.redefine(m_doc->paragraphStyles(), true);
	QStringList applied, missing, skipped;
	for (auto it = styles.constBegin(); it != styles.constEnd(); ++it)
	{
		const QString styleName = it.key();
		QKeySequence key(it.value().toString(), QKeySequence::PortableText);
		if (!tmpStyles.contains(styleName))
		{
			missing << styleName;
			continue;
		}
		if (key.isEmpty())
			continue;
		if (key == styleOwnShortcut(m_doc, styleName))
			continue;
		if (!resolveShortcutConflict(key, styleName))
		{
			skipped << styleName;
			continue;
		}
		// resolveShortcutConflict() may have cleared another style via
		// setStyleShortcut(); pick the set up again so that is kept.
		tmpStyles.redefine(m_doc->paragraphStyles(), true);
		tmpStyles[tmpStyles.find(styleName)].setShortcut(key.toString(QKeySequence::PortableText));
		m_doc->redefineStyles(tmpStyles, false);
		applied << QString("%1 = %2").arg(styleName, key.toString(QKeySequence::NativeText));
	}
	if (!applied.isEmpty())
	{
		m_doc->changed();
		if (m_mainWindow && m_mainWindow->styleMgr())
			m_mainWindow->styleMgr()->setDoc(m_doc);
	}
	rebuildStyleShortcuts();
	updateStylesList();

	QString report = QString("Applied %1 shortcut(s).").arg(applied.size());
	if (!applied.isEmpty())
		report += "\n  " + applied.join("\n  ");
	if (!missing.isEmpty())
		report += QString("\n\nNot in this document (%1):\n  %2").arg(missing.size()).arg(missing.join("\n  "));
	if (!skipped.isEmpty())
		report += QString("\n\nCancelled (%1):\n  %2").arg(skipped.size()).arg(skipped.join("\n  "));
	QMessageBox::information(this, "Import style shortcuts", report);
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
            obj["designStyle"]      = e.designStyle;
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
                // Older export files have no designStyle: that reads as None.
                colCfg.setValue("designStyle",      obj.value("designStyle").toString());
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
            QListWidgetItem* item = new QListWidgetItem(columnConfigLabel(i, s_columnConfigs[i]));
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
    QPushButton* cmLink  = new QPushButton("Link Config N \xe2\x86\x92 style-N", colTabW);
    cmLink->setToolTip("For each config 1-8, link the Design Style named style-N (or \"Style N\", \"N\") if one exists.\n"
                       "Configs without a matching style are left as they are.");
    cmLink->setStyleSheet("QPushButton{padding:5px 10px;background:#16a085;color:white;border:none;border-radius:4px;}");
    colMgrBtns->addWidget(cmLink);
    colMgrBtns->addStretch();
    colTabLay->addLayout(colMgrBtns);
    connect(cmLink, &QPushButton::clicked, [this, refreshColMgrList, colTabW]() {
        const QStringList names = designStyleNames();
        QStringList linked, unmatched;
        for (int i = 0; i < qMin(8, s_columnConfigs.size()); ++i) {
            QString match;
            for (const QString& n : names)
                if (designNameMatchesSlot(n, i + 1)) { match = n; break; }
            if (match.isEmpty()) { unmatched << QString::number(i + 1); continue; }
            s_columnConfigs[i].designStyle = match;
            linked << QString("Config %1 \xe2\x86\x92 %2").arg(i + 1).arg(match);
        }
        saveColumnConfigs();
        refreshColMgrList();
        refreshColTabList();
        QString msg = linked.isEmpty() ? QString("No Design Style named style-1 .. style-8 found.")
                                       : linked.join("\n");
        if (!unmatched.isEmpty())
            msg += QString("\n\nNo matching style for config %1 (left unchanged).").arg(unmatched.join(", "));
        QMessageBox::information(colTabW, "Link Design Styles", msg);
    });

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
        // Name this entry had when the dialog opened (empty for a new one), so
        // Save can move column-config links along with a rename or clear them
        // on a removal.
        grp->setProperty("originalName", idx < count ? savedName : QString());
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
    connect(bb, &QDialogButtonBox::accepted, dlg, [this, dlg, scrollWidget, refreshColMgrList]() {
        QSettings cfg("Scribus", "SuneerDesignStyle");
        cfg.remove("styles");
        QList<QGroupBox*> allGroups = scrollWidget->findChildren<QGroupBox*>();
        QList<QGroupBox*> groups;
        for (auto* g : allGroups)
            if (!g->property("removed").toBool()) groups.append(g);

        // Column configs link Design Styles by name: follow renames, drop
        // links to removed entries.
        {
            QMap<QString, QString> renamed;   // old name -> new name ("" = removed)
            for (auto* g : allGroups) {
                const QString oldName = g->property("originalName").toString();
                if (oldName.isEmpty())
                    continue;
                QLineEdit* nameEd = g->findChild<QLineEdit*>("styleName");
                const QString newName = g->property("removed").toBool() ? QString()
                                      : (nameEd ? nameEd->text() : oldName);
                if (newName != oldName)
                    renamed.insert(oldName, newName);
            }
            if (!renamed.isEmpty()) {
                bool changed = false;
                for (SuneerColumnConfigEntry& e : s_columnConfigs) {
                    if (e.designStyle.isEmpty() || !renamed.contains(e.designStyle))
                        continue;
                    e.designStyle = renamed.value(e.designStyle);
                    changed = true;
                }
                if (changed) {
                    saveColumnConfigs();
                    refreshColMgrList();
                    refreshColTabList();
                }
            }
        }
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
        // Read the style at click time: the settings may change again before then.
        connect(btn, &QPushButton::clicked, this, [this, i]() { applyDesignStyleByIndex(i, true); });
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

    // Re-entry guard: applyColumnConfig() applies a linked Design Style, and a
    // Design Style applies a column config. Whichever started, the other side
    // must not start the loop again.
    if (m_applyingDesignStyle)
        return;
    m_applyingDesignStyle = true;
    // Standalone (icon click): one undo step of our own. From
    // applyColumnConfig() the caller's transaction is already open.
    UndoTransaction designTransaction;
    if (!m_inColumnConfig && UndoManager::undoEnabled())
        designTransaction = UndoManager::instance()->beginTransaction(tf->getUName(), tf->getUPixmap(),
                                                                      Um::SetStyle, QString(), Um::IFont);

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
            // Recorded, so Ctrl+Z reverts the styles (the bare applyStyle()
            // this used to call left no undo state).
            if (pos < len)
                suneerRecordAndApplyParaStyle(tf, tf->itemText, pos, ps);
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
    if (designTransaction)
        designTransaction.commit();
    m_applyingDesignStyle = false;
}
