/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerspacingdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace SuneerSpacing;

namespace {
QDoubleSpinBox* makeSpin(QWidget* parent, Key k)
{
	auto* s = new QDoubleSpinBox(parent);
	if (k == RuleWidth)
	{
		s->setRange(0.0, 2.0);
		s->setSingleStep(0.25);
		s->setDecimals(2);
		s->setSpecialValueText(QObject::tr("none"));
	}
	else
	{
		s->setRange(0.0, 50.0);
		s->setSingleStep(0.1);
		s->setDecimals(1);
	}
	s->setSuffix(QStringLiteral(" ") + keyUnit(k));
	s->setKeyboardTracking(false);
	return s;
}
} // namespace

SuneerSpacingEditor::SuneerSpacingEditor(QWidget* parent) : QWidget(parent)
{
	auto* vl = new QVBoxLayout(this);
	vl->setContentsMargins(0, 0, 0, 0);
	auto* intro = new QLabel(tr("One set of spacing values for every News Browser layout model. A model uses the global value unless \"Use global\" is off for it. Stored in %1.").arg(filePath()), this);
	intro->setWordWrap(true);
	intro->setStyleSheet("color:#555;");
	vl->addWidget(intro);
	const QStringList ms = models();
	m_table = new QTableWidget(KeyCount, 1 + ms.size(), this);
	QStringList heads { tr("Global") };
	for (const QString& m : ms)
		heads << modelTitle(m);
	m_table->setHorizontalHeaderLabels(heads);
	QStringList rows;
	for (int k = 0; k < KeyCount; ++k)
		rows << keyTitle(Key(k));
	m_table->setVerticalHeaderLabels(rows);
	m_table->setSelectionMode(QAbstractItemView::NoSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	for (int k = 0; k < KeyCount; ++k)
	{
		m_global[k] = makeSpin(m_table, Key(k));
		m_table->setCellWidget(k, 0, m_global[k]);
		connect(m_global[k], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, k](double v) {
			// models that use the global value follow it on screen
			for (auto& col : m_cells)
				if (col[k].useGlobal->isChecked())
				{
					QSignalBlocker b(col[k].spin);
					col[k].spin->setValue(v);
				}
			if (!m_loading)
				emit changed();
		});
	}
	for (int mi = 0; mi < ms.size(); ++mi)
	{
		QList<Cell> col;
		for (int k = 0; k < KeyCount; ++k)
		{
			auto* w = new QWidget(m_table);
			auto* hl = new QHBoxLayout(w);
			hl->setContentsMargins(2, 0, 2, 0);
			Cell c;
			c.useGlobal = new QCheckBox(tr("global"), w);
			c.useGlobal->setToolTip(tr("Use the global value"));
			c.spin = makeSpin(w, Key(k));
			hl->addWidget(c.useGlobal);
			hl->addWidget(c.spin, 1);
			m_table->setCellWidget(k, 1 + mi, w);
			connect(c.useGlobal, &QCheckBox::toggled, this, [this, mi, k](bool on) {
				m_cells[mi][k].spin->setEnabled(!on);
				if (on)
				{
					QSignalBlocker b(m_cells[mi][k].spin);
					m_cells[mi][k].spin->setValue(m_global[k]->value());
				}
				if (!m_loading)
					emit changed();
			});
			connect(c.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!m_loading) emit changed(); });
			col << c;
		}
		m_cells << col;
	}
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(360);
	vl->addWidget(m_table, 1);
	auto* ruleRow = new QHBoxLayout();
	ruleRow->addWidget(new QLabel(tr("Separator rule colour (document colour name):"), this));
	m_ruleColor = new QLineEdit(this);
	m_ruleColor->setMaximumWidth(160);
	connect(m_ruleColor, &QLineEdit::editingFinished, this, [this]() { if (!m_loading) emit changed(); });
	ruleRow->addWidget(m_ruleColor);
	ruleRow->addStretch(1);
	vl->addLayout(ruleRow);
	auto* fileRow = new QHBoxLayout();
	auto* exportBtn = new QPushButton(tr("Export…"), this);
	exportBtn->setToolTip(tr("Save these values to a file (for the office PCs)"));
	auto* importBtn = new QPushButton(tr("Import…"), this);
	importBtn->setToolTip(tr("Load values from an exported file"));
	auto* defaultsBtn = new QPushButton(tr("House defaults"), this);
	defaultsBtn->setToolTip(tr("Global 4.8 / 8.0 mm, wrap 2.0, caption-to-text 2.0, column gap 2.5; Obituary 2.3 / 3.0"));
	fileRow->addWidget(exportBtn);
	fileRow->addWidget(importBtn);
	fileRow->addStretch(1);
	fileRow->addWidget(defaultsBtn);
	vl->addLayout(fileRow);
	connect(exportBtn, &QPushButton::clicked, this, [this]() {
		const QString p = QFileDialog::getSaveFileName(this, tr("Export spacing"), QDir::homePath() + QStringLiteral("/layout-spacing.json"), tr("JSON (*.json)"));
		if (p.isEmpty())
			return;
		// what is on screen, not only what was saved
		const Store keep = store();
		QString err;
		save(current(), &err);
		const bool ok = exportTo(p, &err);
		save(keep);
		if (!ok)
			QMessageBox::warning(this, tr("Export spacing"), err);
	});
	connect(importBtn, &QPushButton::clicked, this, [this]() {
		const QString p = QFileDialog::getOpenFileName(this, tr("Import spacing"), QDir::homePath(), tr("JSON (*.json)"));
		if (p.isEmpty())
			return;
		const Store keep = store();
		QString err;
		if (!importFrom(p, &err))
		{
			QMessageBox::warning(this, tr("Import spacing"), err);
			return;
		}
		const Store imported = store();
		save(keep);   // saved only on OK
		load(imported);
		emit changed();
	});
	connect(defaultsBtn, &QPushButton::clicked, this, [this]() { load(defaults()); emit changed(); });
}

void SuneerSpacingEditor::load(const Store& s)
{
	m_loading = true;
	const QStringList ms = models();
	for (int k = 0; k < KeyCount; ++k)
		m_global[k]->setValue(s.global.v[k]);
	for (int mi = 0; mi < ms.size(); ++mi)
	{
		const QMap<int, double> ov = s.overrides.value(ms[mi]);
		for (int k = 0; k < KeyCount; ++k)
		{
			const bool has = ov.contains(k);
			m_cells[mi][k].useGlobal->setChecked(!has);
			m_cells[mi][k].spin->setEnabled(has);
			m_cells[mi][k].spin->setValue(has ? ov.value(k) : s.global.v[k]);
		}
	}
	m_ruleColor->setText(s.global.ruleColor);
	m_loading = false;
}

Store SuneerSpacingEditor::current() const
{
	Store s = store();
	for (int k = 0; k < KeyCount; ++k)
		s.global.v[k] = m_global[k]->value();
	s.global.ruleColor = m_ruleColor->text().trimmed().isEmpty() ? QStringLiteral("Black") : m_ruleColor->text().trimmed();
	const QStringList ms = models();
	for (int mi = 0; mi < ms.size(); ++mi)
	{
		QMap<int, double> ov;
		for (int k = 0; k < KeyCount; ++k)
			if (!m_cells[mi][k].useGlobal->isChecked())
				ov[k] = m_cells[mi][k].spin->value();
		s.overrides[ms[mi]] = ov;
	}
	return s;
}

// ---------------------------------------------------------------------------

Prefs_SuneerSpacing::Prefs_SuneerSpacing(QWidget* parent, ScribusDoc* doc) : Prefs_Pane(parent)
{
	Q_UNUSED(doc);
	m_caption = tr("Faircode Layout: Spacing");
	m_icon = QStringLiteral("frame-text");
	m_keywords << tr("spacing") << tr("gap") << tr("gutter") << tr("Faircode") << tr("News Browser") << tr("obituary");
	auto* vl = new QVBoxLayout(this);
	auto* title = new QLabel(QStringLiteral("<b>") + tr("Faircode Layout — Spacing") + QStringLiteral("</b>"), this);
	vl->addWidget(title);
	m_editor = new SuneerSpacingEditor(this);
	vl->addWidget(m_editor, 1);
	m_editor->load(store());
}

void Prefs_SuneerSpacing::restoreDefaults(struct ApplicationPrefs* prefsData)
{
	Q_UNUSED(prefsData);
	m_editor->load(store());
}

void Prefs_SuneerSpacing::saveGuiToPrefs(struct ApplicationPrefs* prefsData) const
{
	Q_UNUSED(prefsData);
	save(m_editor->current());
}

// ---------------------------------------------------------------------------

SuneerSpacingDialog::SuneerSpacingDialog(const PageHooks& hooks, QWidget* parent) : QDialog(parent), m_hooks(hooks)
{
	setWindowTitle(tr("Faircode Layout — Spacing"));
	resize(1000, 620);
	m_original = store();
	auto* vl = new QVBoxLayout(this);
	m_editor = new SuneerSpacingEditor(this);
	m_editor->load(m_original);
	vl->addWidget(m_editor, 1);
	auto* pageRow = new QHBoxLayout();
	m_liveChk = new QCheckBox(tr("Live preview on this page"), this);
	m_liveChk->setToolTip(tr("Re-space the placed modules of the current page while you change values (Cancel puts the page back)."));
	auto* applyBtn = new QPushButton(tr("Apply to this page"), this);
	applyBtn->setToolTip(tr("Re-space the placed modules of the current page with these values: vertical gaps, wrap padding, insets, column gaps, separator rules. Locked and pinned modules stay. One undo step."));
	m_info = new QLabel(this);
	m_info->setStyleSheet("color:#555;");
	pageRow->addWidget(m_liveChk);
	pageRow->addWidget(applyBtn);
	pageRow->addWidget(m_info, 1);
	vl->addLayout(pageRow);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	// Enter in a value field confirms the value (and previews it), it never closes the dialog
	for (QAbstractButton* b : bb->buttons())
		if (auto* pb = qobject_cast<QPushButton*>(b))
		{
			pb->setAutoDefault(false);
			pb->setDefault(false);
		}
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	vl->addWidget(bb);
	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(400);
	connect(m_timer, &QTimer::timeout, this, &SuneerSpacingDialog::preview);
	connect(m_editor, &SuneerSpacingEditor::changed, this, &SuneerSpacingDialog::schedulePreview);
	connect(m_liveChk, &QCheckBox::toggled, this, [this](bool on) {
		if (on)
			schedulePreview();
		else if (m_previewing && m_hooks.restore)
		{
			m_hooks.restore();
			m_previewing = false;
			m_info->setText(tr("Page restored"));
		}
	});
	connect(applyBtn, &QPushButton::clicked, this, &SuneerSpacingDialog::applyToPage);
	const bool havePage = bool(m_hooks.apply);
	m_liveChk->setEnabled(havePage);
	applyBtn->setEnabled(havePage);
	if (havePage && m_hooks.pageModel)
		m_info->setText(tr("This page uses the %1 values.").arg(modelTitle(m_hooks.pageModel())));
}

void SuneerSpacingDialog::keyPressEvent(QKeyEvent* e)
{
	if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !qobject_cast<QPushButton*>(focusWidget()))
	{
		e->accept();
		return;
	}
	QDialog::keyPressEvent(e);
}

void SuneerSpacingDialog::schedulePreview()
{
	if (m_liveChk->isChecked())
		m_timer->start();
}

void SuneerSpacingDialog::preview()
{
	if (!m_hooks.apply || !m_hooks.pageModel)
		return;
	if (!m_previewing && m_hooks.snapshot)
		m_hooks.snapshot();
	else if (m_previewing && m_hooks.restore)
		m_hooks.restore();
	m_previewing = true;
	const int n = m_hooks.apply(m_editor->current().forModel(m_hooks.pageModel()), false);
	m_info->setText(tr("Preview: %n module(s) re-spaced", "", n));
}

void SuneerSpacingDialog::applyToPage()
{
	if (!m_hooks.apply || !m_hooks.pageModel)
		return;
	m_timer->stop();
	if (m_previewing && m_hooks.restore)
		m_hooks.restore();
	m_previewing = false;
	save(m_editor->current());
	const int n = m_hooks.apply(store().forModel(m_hooks.pageModel()), true);
	m_original = store();
	m_info->setText(tr("Applied: %n module(s) re-spaced (one undo step)", "", n));
}

void SuneerSpacingDialog::accept()
{
	m_timer->stop();
	save(m_editor->current());
	if (m_previewing)
	{
		// the previewed state becomes the real one, as one undo step
		if (m_hooks.restore)
			m_hooks.restore();
		m_previewing = false;
		if (m_hooks.apply && m_hooks.pageModel)
			m_hooks.apply(store().forModel(m_hooks.pageModel()), true);
	}
	QDialog::accept();
}

void SuneerSpacingDialog::reject()
{
	m_timer->stop();
	if (m_previewing && m_hooks.restore)
		m_hooks.restore();
	m_previewing = false;
	save(m_original);
	QDialog::reject();
}
