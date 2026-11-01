/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/

#include "suneertextgradient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "colorcombo.h"
#include "pageitem.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "sccolor.h"
#include "sccolorengine.h"
#include "scribus.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "selection.h"
#include "styles/charstyle.h"
#include "ui/scrspinbox.h"
#include "ui/suneertextcolour.h"
#include "undomanager.h"
#include "units.h"
#include "vgradient.h"
#include "scribusview.h"
#include "undogui.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTimer>

namespace
{
	const char* kEtea = "SUNEER_TEXTGRADIENT";

	void refreshPalettes(ScribusDoc* doc)
	{
		ScribusMainWindow* mw = doc ? doc->scMW() : nullptr;
		if (mw)
			mw->updateColorLists();
	}
}

SuneerTextGradientWidget::SuneerTextGradientWidget(Mode mode, QWidget* parent)
	: QWidget(parent), m_mode(mode)
{
	auto* top = new QVBoxLayout(this);
	top->setContentsMargins(3, 3, 3, 3);
	top->setSpacing(3);
	auto* grid = new QGridLayout();
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(4);
	grid->setVerticalSpacing(3);
	top->addLayout(grid);

	int row = 0;
	grid->addWidget(new QLabel(tr("Text fill:"), this), row, 0);
	m_fill = new QComboBox(this);
	m_fill->setObjectName("suneerTextFillType");
	if (m_mode == StyleEditor)
		m_fill->addItem(tr("Inherited"), -1);
	m_fill->addItem(tr("Solid"), 0);
	m_fill->addItem(tr("Gradient"), 1);
	m_fill->setToolTip(tr("Solid: the fill colour. Gradient: the text is filled with a document gradient and stays text (editable, searchable in the PDF)."));
	grid->addWidget(m_fill, row++, 1, 1, 2);

	m_gradientLabel = new QLabel(tr("Gradient:"), this);
	grid->addWidget(m_gradientLabel, row, 0);
	m_gradient = new QComboBox(this);
	m_gradient->setObjectName("suneerTextGradientName");
	m_gradient->setToolTip(tr("A gradient of this document (Edit > Colours and Fills > Gradients) or a preset; a preset is added to the document with its CMYK colours when chosen."));
	grid->addWidget(m_gradient, row++, 1, 1, 2);

	auto* btnRow = new QHBoxLayout();
	btnRow->setContentsMargins(0, 0, 0, 0);
	m_editGradients = new QPushButton(tr("Edit..."), this);
	m_editGradients->setToolTip(tr("Edit > Colours and Fills: change the stops, add your own gradients."));
	m_savePreset = new QPushButton(tr("Save as preset"), this);
	m_savePreset->setToolTip(tr("Keep this gradient, with its colours, as a preset for every document."));
	btnRow->addWidget(m_editGradients);
	btnRow->addWidget(m_savePreset);
	btnRow->addStretch(1);
	grid->addLayout(btnRow, row++, 1, 1, 2);

	m_typeLabel = new QLabel(tr("Type:"), this);
	grid->addWidget(m_typeLabel, row, 0);
	m_type = new QComboBox(this);
	m_type->addItem(tr("Linear"));
	m_type->addItem(tr("Radial"));
	grid->addWidget(m_type, row++, 1, 1, 2);

	m_angleLabel = new QLabel(tr("Angle:"), this);
	grid->addWidget(m_angleLabel, row, 0);
	m_angle = new QSpinBox(this);
	m_angle->setRange(0, 359);
	m_angle->setWrapping(true);
	m_angle->setSuffix(QStringLiteral(" °"));
	m_angle->setValue(90);
	m_angle->setToolTip(tr("Direction of a linear gradient: 90° = top to bottom, 0° = left to right."));
	grid->addWidget(m_angle, row++, 1, 1, 2);

	m_mappingLabel = new QLabel(tr("Spread:"), this);
	grid->addWidget(m_mappingLabel, row, 0);
	m_mapping = new QComboBox(this);
	m_mapping->addItem(tr("Per line"));
	m_mapping->addItem(tr("Per frame"));
	m_mapping->setToolTip(tr("Per line: every line gets the whole gradient (the e-paper headline). Per frame: one gradient across all the frame's text."));
	grid->addWidget(m_mapping, row++, 1, 1, 2);
	grid->setColumnStretch(1, 1);

	if (m_mode == ApplyToSelection)
	{
		m_shadow = new QGroupBox(tr("Soft shadow (frame)"), this);
		m_shadow->setCheckable(true);
		m_shadow->setChecked(false);
		m_shadow->setToolTip(tr("The frame's soft shadow: with no frame fill it shadows the text. Blurred, so it is an image in the PDF; PostScript printing does not draw it."));
		auto* sg = new QGridLayout(m_shadow);
		sg->setContentsMargins(4, 4, 4, 4);
		sg->setVerticalSpacing(3);
		m_shadowX = new ScrSpinBox(-200.0, 200.0, m_shadow, 0);
		m_shadowY = new ScrSpinBox(-200.0, 200.0, m_shadow, 0);
		m_shadowBlur = new ScrSpinBox(0.0, 100.0, m_shadow, 0);
		m_shadowColor = new ColorCombo(m_shadow);
		m_shadowOpacity = new QSpinBox(m_shadow);
		m_shadowOpacity->setRange(0, 100);
		m_shadowOpacity->setSuffix(QStringLiteral(" %"));
		sg->addWidget(new QLabel(tr("X:"), m_shadow), 0, 0);
		sg->addWidget(m_shadowX, 0, 1);
		sg->addWidget(new QLabel(tr("Y:"), m_shadow), 0, 2);
		sg->addWidget(m_shadowY, 0, 3);
		sg->addWidget(new QLabel(tr("Blur:"), m_shadow), 1, 0);
		sg->addWidget(m_shadowBlur, 1, 1);
		sg->addWidget(new QLabel(tr("Opacity:"), m_shadow), 1, 2);
		sg->addWidget(m_shadowOpacity, 1, 3);
		sg->addWidget(new QLabel(tr("Colour:"), m_shadow), 2, 0);
		sg->addWidget(m_shadowColor, 2, 1, 1, 3);
		top->addWidget(m_shadow);
		connect(m_shadow, &QGroupBox::toggled, this, [this]() { applyShadow(); });
		for (ScrSpinBox* sb : { m_shadowX, m_shadowY, m_shadowBlur })
			connect(sb, QOverload<double>::of(&ScrSpinBox::valueChanged), this, [this]() { applyShadow(); });
		connect(m_shadowOpacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { applyShadow(); });
		connect(m_shadowColor, QOverload<int>::of(&QComboBox::activated), this, [this]() { applyShadow(); });
	}

	connect(m_fill, QOverload<int>::of(&QComboBox::activated), this, [this]() { onFillChanged(); });
	connect(m_gradient, QOverload<int>::of(&QComboBox::activated), this, [this]() {
		if (m_doc)
			ensureGradient(m_doc, m_gradient->currentData().toString());
		fillGradientCombo(m_gradient->currentData().toString());
		apply();
	});
	connect(m_type, QOverload<int>::of(&QComboBox::activated), this, [this]() { updateEnabled(); apply(); });
	connect(m_angle, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { apply(); });
	connect(m_mapping, QOverload<int>::of(&QComboBox::activated), this, [this]() { apply(); });
	connect(m_savePreset, &QPushButton::clicked, this, [this]() { savePresetClicked(); });
	connect(m_editGradients, &QPushButton::clicked, this, [this]() {
		if (m_doc && m_doc->scMW())
			m_doc->scMW()->manageColorsAndFills();
		fillGradientCombo(m_gradient->currentData().toString());
	});

	fillGradientCombo(QString());
	updateEnabled();
}

void SuneerTextGradientWidget::setDoc(ScribusDoc* doc)
{
	m_doc = doc;
	m_item = nullptr;
	if (m_shadowColor)
	{
		m_shadowX->setNewUnit(doc ? doc->unitIndex() : 0);
		m_shadowY->setNewUnit(doc ? doc->unitIndex() : 0);
		m_shadowBlur->setNewUnit(doc ? doc->unitIndex() : 0);
		if (doc)
			m_shadowColor->setColors(doc->PageColors, false);
	}
	fillGradientCombo(m_gradient->currentData().toString());
}

void SuneerTextGradientWidget::fillGradientCombo(const QString& current)
{
	const bool b = m_gradient->blockSignals(true);
	m_gradient->clear();
	QStringList docNames;
	if (m_doc)
	{
		docNames = m_doc->docGradients.keys();
		docNames.sort(Qt::CaseInsensitive);
	}
	for (const QString& n : docNames)
		m_gradient->addItem(n, n);
	for (const QString& name : presetNames())
		if (!docNames.contains(name))
			m_gradient->addItem(tr("%1 (preset)").arg(name), name);
	int idx = m_gradient->findData(current);
	if (idx < 0 && !current.isEmpty())
	{
		// a name the document no longer has: keep showing it
		m_gradient->addItem(tr("%1 (missing)").arg(current), current);
		idx = m_gradient->count() - 1;
	}
	m_gradient->setCurrentIndex(qMax(0, idx));
	m_gradient->blockSignals(b);
}

void SuneerTextGradientWidget::updateEnabled()
{
	const bool gradient = m_fill->currentData().toInt() == 1;
	for (QWidget* w : std::initializer_list<QWidget*>{ m_gradient, m_type, m_mapping, m_savePreset, m_gradientLabel, m_typeLabel, m_mappingLabel })
		w->setEnabled(gradient);
	m_angle->setEnabled(gradient && m_type->currentIndex() == 0);
	m_angleLabel->setEnabled(m_angle->isEnabled());
}

void SuneerTextGradientWidget::onFillChanged()
{
	if (m_fill->currentData().toInt() == 1)
	{
		// first time: the red headline preset
		QString name = m_gradient->currentData().toString();
		if (name.isEmpty())
			name = QStringLiteral("Red headline");
		if (m_doc)
			ensureGradient(m_doc, name);
		fillGradientCombo(name);
	}
	updateEnabled();
	apply();
}

void SuneerTextGradientWidget::showStyle(const CharStyle& style, bool inherited)
{
	m_updating = true;
	const bool b1 = m_fill->blockSignals(true), b2 = m_type->blockSignals(true), b3 = m_angle->blockSignals(true), b4 = m_mapping->blockSignals(true);
	const int fill = (m_mode == StyleEditor && inherited) ? -1 : (style.fillGradient().isEmpty() ? 0 : 1);
	m_fill->setCurrentIndex(qMax(0, m_fill->findData(fill)));
	fillGradientCombo(style.fillGradient().isEmpty() ? m_gradient->currentData().toString() : style.fillGradient());
	m_type->setCurrentIndex(qBound(0, style.fillGradientType(), 1));
	m_angle->setValue(qRound(style.fillGradientAngle()) % 360);
	m_mapping->setCurrentIndex(qBound(0, style.fillGradientMapping(), 1));
	m_fill->blockSignals(b1); m_type->blockSignals(b2); m_angle->blockSignals(b3); m_mapping->blockSignals(b4);
	updateEnabled();
	m_updating = false;
}

void SuneerTextGradientWidget::showItem(PageItem* item)
{
	m_item = item;
	if (!m_shadow)
		return;
	m_shadow->setEnabled(item != nullptr);
	if (!item)
		return;
	m_updating = true;
	const QList<QWidget*> ws { m_shadow, m_shadowX, m_shadowY, m_shadowBlur, m_shadowColor, m_shadowOpacity };
	QList<bool> blocked;
	for (QWidget* w : ws)
		blocked << w->blockSignals(true);
	m_shadow->setChecked(item->hasSoftShadow());
	m_shadowX->setValue(item->softShadowXOffset() * m_shadowX->unitRatio());
	m_shadowY->setValue(item->softShadowYOffset() * m_shadowY->unitRatio());
	m_shadowBlur->setValue(item->softShadowBlurRadius() * m_shadowBlur->unitRatio());
	m_shadowColor->setCurrentColor(item->softShadowColor());
	m_shadowOpacity->setValue(qRound((1.0 - item->softShadowOpacity()) * 100.0));
	for (int i = 0; i < ws.count(); ++i)
		ws[i]->blockSignals(blocked[i]);
	m_updating = false;
}

CharStyle SuneerTextGradientWidget::currentValues() const
{
	CharStyle cs;
	const bool gradient = m_fill->currentData().toInt() == 1;
	cs.setFillGradient(gradient ? m_gradient->currentData().toString() : QString());
	cs.setFillGradientType(m_type->currentIndex());
	cs.setFillGradientAngle(m_angle->value());
	cs.setFillGradientMapping(m_mapping->currentIndex());
	return cs;
}

void SuneerTextGradientWidget::writeTo(CharStyle& style) const
{
	if (m_fill->currentData().toInt() == -1)
	{
		style.resetFillGradient();
		style.resetFillGradientType();
		style.resetFillGradientAngle();
		style.resetFillGradientMapping();
		return;
	}
	const CharStyle cs = currentValues();
	style.setFillGradient(cs.fillGradient());
	style.setFillGradientType(cs.fillGradientType());
	style.setFillGradientAngle(cs.fillGradientAngle());
	style.setFillGradientMapping(cs.fillGradientMapping());
}

void SuneerTextGradientWidget::apply()
{
	if (m_updating)
		return;
	if (m_mode == StyleEditor)
	{
		emit changed();
		return;
	}
	if (!m_doc)
		return;
	Selection* sel = m_selection ? m_selection() : nullptr;
	Selection* target = sel ? sel : m_doc->m_Selection;
	if (!target || target->isEmpty())
		return;
	m_doc->itemSelection_ApplyCharStyle(currentValues(), sel, kEtea);
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	emit changed();
}

void SuneerTextGradientWidget::applyShadow()
{
	if (m_updating || !m_doc || !m_shadow)
		return;
	Selection* sel = m_selection ? m_selection() : nullptr;
	Selection* target = sel ? sel : m_doc->m_Selection;
	if (!target || target->isEmpty())
		return;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::SelectionGroup, Um::IGroup, tr("Soft shadow"), QString(), Um::IGroup);
	for (int i = 0; i < target->count(); ++i)
	{
		PageItem* it = target->itemAt(i);
		if (!it)
			continue;
		it->setSoftShadowXOffset(m_shadowX->value() / m_shadowX->unitRatio());
		it->setSoftShadowYOffset(m_shadowY->value() / m_shadowY->unitRatio());
		it->setSoftShadowBlurRadius(m_shadowBlur->value() / m_shadowBlur->unitRatio());
		QString col = m_shadowColor->currentColor();
		if (col.isEmpty() || col == CommonStrings::tr_NoneColor)
			col = QStringLiteral("Black");
		it->setSoftShadowColor(col);
		it->setSoftShadowOpacity(1.0 - m_shadowOpacity->value() / 100.0);
		it->setHasSoftShadow(m_shadow->isChecked());
	}
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerTextGradientWidget::savePresetClicked()
{
	if (!m_doc)
		return;
	const QString current = m_gradient->currentData().toString();
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Save as preset"), tr("Preset name:"), QLineEdit::Normal, current, &ok).trimmed();
	if (!ok || name.isEmpty())
		return;
	if (!ensureGradient(m_doc, current))
		return;
	if (name != current)
	{
		m_doc->docGradients.insert(name, m_doc->docGradients.value(current));
		refreshPalettes(m_doc);
	}
	if (!savePreset(m_doc, name))
		QMessageBox::warning(this, tr("Save as preset"), tr("Could not save the preset."));
	fillGradientCombo(name);
	apply();
}

QStringList SuneerTextGradientWidget::presetNames()
{
	QStringList out;
	for (const SuneerGradientPresets::Preset& p : SuneerGradientPresets::all())
		out << p.name;
	return out;
}

bool SuneerTextGradientWidget::ensureGradient(ScribusDoc* doc, const QString& name)
{
	// the presets live in SuneerGradientPresets (shared with the Text colour popup)
	return SuneerGradientPresets::ensureGradient(doc, name);
}

bool SuneerTextGradientWidget::savePreset(ScribusDoc* doc, const QString& name)
{
	if (!doc || !doc->docGradients.contains(name))
		return false;
	return SuneerGradientPresets::save(SuneerGradientPresets::fromDocument(doc, name, 0, 90.0));
}

void SuneerTextGradientWidget::runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc)
{
	static bool started = false;
	const QString prefix = qEnvironmentVariable("SUNEER_TEXTGRAD_SELFTEST");
	if (started || prefix.isEmpty() || !view || !doc)
		return;
	started = true;
	QTimer::singleShot(2500, view, [view, doc, prefix]() {
		QFile rf(prefix + QStringLiteral("-report.txt"));
		if (!rf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			return;
		QTextStream ts(&rf);
		int failed = 0;
		auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "TEXTGRAD-SELFTEST %s\n", qPrintable(l)); };
		auto check = [&](const QString& what, bool ok) { line(QStringLiteral("%1 %2").arg(ok ? "PASS" : "FAIL", what)); if (!ok) ++failed; };
		if (ScribusMainWindow* mw = doc->scMW())
			for (UndoGui* g : mw->findChildren<UndoGui*>())
				UndoManager::instance()->removeGui(g);

		auto frame = [&](const QString& name) -> PageItem* {
			for (PageItem* it : doc->DocItems)
				if (it && it->itemName() == name)
					return it;
			return nullptr;
		};
		auto describe = [&](PageItem* it) {
			QString out;
			int ml = 0;
			for (int i = 0; i < it->itemText.length(); ++i)
			{
				const QChar c = it->itemText.text(i);
				if (c.unicode() >= 0x0D00 && c.unicode() <= 0x0D7F)
					++ml;
			}
			const CharStyle& cs = it->itemText.charStyle(0);
			const CharStyle& ce = it->itemText.charStyle(qMax(0, it->itemText.length() - 1));
			out = QStringLiteral("%1: %2 chars, %3 Malayalam, lines %4; first [%5 t%6 a%7 m%8] last [%9]")
			      .arg(it->itemName()).arg(it->itemText.length()).arg(ml).arg(it->textLayout.lines())
			      .arg(cs.fillGradient()).arg(cs.fillGradientType()).arg(cs.fillGradientAngle()).arg(cs.fillGradientMapping()).arg(ce.fillGradient());
			return out;
		};
		auto applyTo = [&](PageItem* it, const QString& grad, int type, double angle, int mapping, int rangeLen) {
			if (!it)
				return;
			ensureGradient(doc, grad);
			CharStyle cs;
			cs.setFillGradient(grad);
			cs.setFillGradientType(type);
			cs.setFillGradientAngle(angle);
			cs.setFillGradientMapping(mapping);
			doc->m_Selection->clear();
			doc->m_Selection->addItem(it);
			const int oldMode = doc->appMode;
			if (rangeLen > 0)
			{
				// a character range, as typed-in selection in the story
				doc->appMode = modeEdit;
				it->itemText.deselectAll();
				it->itemText.select(0, rangeLen);
			}
			doc->itemSelection_ApplyCharStyle(cs, nullptr, QString());
			if (rangeLen > 0)
			{
				it->itemText.deselectAll();
				doc->appMode = oldMode;
			}
			it->invalidateLayout();
			it->layout();
		};

		PageItem* gLine = frame(QStringLiteral("GradLine"));
		PageItem* gFrame = frame(QStringLiteral("GradFrame"));
		PageItem* gRadial = frame(QStringLiteral("GradRadial"));
		PageItem* gRange = frame(QStringLiteral("GradRange"));
		check(QStringLiteral("test frames found"), gLine && gFrame && gRadial && gRange);
		if (!(gLine && gFrame && gRadial && gRange))
		{
			rf.close();
			QTimer::singleShot(200, qApp, []() { QApplication::exit(2); });
			return;
		}
		const int wordLen = qMax(1, gRange->itemText.plainText().indexOf(QChar(' ')));
		applyTo(gLine, QStringLiteral("Red headline"), 0, 90, 0, 0);
		applyTo(gFrame, QStringLiteral("Red headline"), 0, 90, 1, 0);
		applyTo(gRadial, QStringLiteral("Grey headline"), 1, 90, 1, 0);
		applyTo(gRange, QStringLiteral("Red headline"), 0, 90, 0, wordLen);
		doc->m_Selection->clear();
		for (PageItem* it : { gLine, gFrame, gRadial, gRange })
			line(describe(it));
		check(QStringLiteral("presets added as document gradients"), doc->docGradients.contains(QStringLiteral("Red headline")) && doc->docGradients.contains(QStringLiteral("Grey headline")));
		const ScColor& dark = doc->PageColors.value(QStringLiteral("Headline Dark Red"));
		check(QStringLiteral("preset colours are CMYK"), dark.getColorModel() == colorModelCMYK);
		check(QStringLiteral("GradLine whole text per line"), gLine->itemText.charStyle(0).fillGradient() == QLatin1String("Red headline") && gLine->itemText.charStyle(gLine->itemText.length() - 1).fillGradient() == QLatin1String("Red headline") && gLine->itemText.charStyle(0).fillGradientMapping() == 0);
		check(QStringLiteral("GradFrame per frame"), gFrame->itemText.charStyle(0).fillGradientMapping() == 1);
		check(QStringLiteral("GradRadial radial"), gRadial->itemText.charStyle(0).fillGradientType() == 1);
		const bool rangeOk = gRange->itemText.charStyle(0).fillGradient() == QLatin1String("Red headline") && gRange->itemText.charStyle(wordLen + 1).fillGradient().isEmpty();
		check(QStringLiteral("GradRange only the first word (%1 chars)").arg(wordLen), rangeOk);
		check(QStringLiteral("multi-line test frames"), gLine->textLayout.lines() >= 2 && gFrame->textLayout.lines() >= 2);

		// undo the range step, then redo it
		UndoManager::instance()->undo(1);
		const bool undone = gRange->itemText.charStyle(0).fillGradient().isEmpty();
		check(QStringLiteral("undo removes the range gradient"), undone);
		UndoManager::instance()->redo(1);
		check(QStringLiteral("redo brings it back"), gRange->itemText.charStyle(0).fillGradient() == QLatin1String("Red headline"));

		doc->changed();
		view->DrawNew();
		for (int i = 0; i < 6; ++i)
			QApplication::processEvents();
		const double ds = PrefsManager::instance().appPrefs.displayPrefs.displayScale;
		auto shot = [&](const QString& name, PageItem* centre, double pct) {
			view->zoom(qRound(centre->xPos() + centre->width() / 2.0), qRound(centre->yPos() + centre->height() / 2.0), pct / 100.0 * ds, false);
			view->DrawNew();
			for (int i = 0; i < 6; ++i)
				QApplication::processEvents();
			const QString png = prefix + QStringLiteral("-") + name + QStringLiteral(".png");
			line(QStringLiteral("picture %1: %2").arg(png, view->viewport()->grab().save(png) ? "saved" : "FAILED"));
		};
		shot(QStringLiteral("line"), gLine, 50);
		shot(QStringLiteral("frame"), gFrame, 50);
		shot(QStringLiteral("radial-range"), gRadial, 50);
		shot(QStringLiteral("page"), gLine, 20);

		const QString saveAs = qEnvironmentVariable("SUNEER_TEXTGRAD_SAVEAS");
		if (!saveAs.isEmpty())
			check(QStringLiteral("saved %1").arg(saveAs), doc->save(saveAs));
		line(failed == 0 ? QStringLiteral("ALL PASS") : QStringLiteral("FAILED: %1").arg(failed));
		rf.close();
		if (!qEnvironmentVariableIsSet("SUNEER_TEXTGRAD_SELFTEST_HOLD"))
			QTimer::singleShot(200, qApp, [failed]() { QApplication::exit(failed ? 1 : 0); });
	});
}
