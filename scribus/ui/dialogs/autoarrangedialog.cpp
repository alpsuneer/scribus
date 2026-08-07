/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/dialogs/autoarrangedialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "pageitem.h"
#include "scribusdoc.h"
#include "units.h"

AutoArrangeDialog::AutoArrangeDialog(ScribusDoc* doc, QWidget* parent)
	: QDialog(parent), m_doc(doc)
{
	setWindowTitle(tr("Auto Arrange Frames"));
	setModal(true);

	auto* lay = new QVBoxLayout(this);
	lay->setSpacing(8);

	auto* intro = new QLabel(tr(
		"<b>Nothing has been changed yet.</b><br>"
		"Only the frames you selected are considered, and only their vertical "
		"position changes — never their size, never their column."), this);
	intro->setWordWrap(true);
	lay->addWidget(intro);

	auto* gl = new QHBoxLayout();
	gl->addWidget(new QLabel(tr("Gutter between stacked blocks:"), this));
	m_gutter = new QDoubleSpinBox(this);
	m_gutter->setRange(0.0, 200.0);
	m_gutter->setDecimals(1);
	m_gutter->setSingleStep(1.0);
	m_gutter->setSuffix(tr(" pt"));
	m_gutter->setValue(AutoArrangeEngine::DefaultGutter);
	gl->addWidget(m_gutter);
	gl->addStretch(1);
	lay->addLayout(gl);

	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);
	lay->addWidget(m_summary);

	m_table = new QTableWidget(0, 5, this);
	m_table->setHorizontalHeaderLabels(QStringList()
		<< tr("Block") << tr("Frame") << tr("Type") << tr("From (x, y)") << tr("To (x, y)"));
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setMinimumSize(620, 260);
	lay->addWidget(m_table, 1);

	auto* bb = new QDialogButtonBox(this);
	m_apply = bb->addButton(tr("Apply These Moves"), QDialogButtonBox::AcceptRole);
	bb->addButton(QDialogButtonBox::Cancel)->setText(tr("Close, Change Nothing"));
	lay->addWidget(bb);

	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_gutter, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	        this, &AutoArrangeDialog::recompute);

	recompute();
}

void AutoArrangeDialog::recompute()
{
	m_plan = AutoArrangeEngine::planForSelection(m_doc, m_gutter->value());
	populate();
}

void AutoArrangeDialog::populate()
{
	m_table->setRowCount(0);

	const double ratio = m_doc ? unitGetRatioFromIndex(m_doc->unitIndex()) : 1.0;
	const QString suffix = m_doc ? unitGetSuffixFromIndex(m_doc->unitIndex()) : QString();

	int row = 0;
	for (const ArrangeMove& mv : m_plan.moves)
	{
		if (!mv.item)
			continue;
		m_table->insertRow(row);
		auto put = [&](int col, const QString& text) {
			m_table->setItem(row, col, new QTableWidgetItem(text));
		};
		put(0, QString::number(mv.block + 1));
		put(1, mv.item->itemName());
		put(2, mv.item->isTextFrame() ? tr("Text")
		     : mv.item->isImageFrame() ? tr("Image")
		     : mv.item->isGroup() ? tr("Group") : tr("Other"));
		put(3, QString("%1, %2%3").arg(mv.from.x() * ratio, 0, 'f', 1)
		                          .arg(mv.from.y() * ratio, 0, 'f', 1).arg(suffix));
		put(4, QString("%1, %2%3").arg(mv.to.x() * ratio, 0, 'f', 1)
		                          .arg(mv.to.y() * ratio, 0, 'f', 1).arg(suffix));
		++row;
	}
	m_table->resizeColumnsToContents();

	if (!m_plan.note.isEmpty())
		m_summary->setText(QString("<i>%1</i>").arg(m_plan.note));
	else
		m_summary->setText(tr("%1 frame(s) in %2 block(s) would move. "
		                      "%3 frame(s) selected, %4 skipped as protected.")
			.arg(m_plan.moves.count())
			.arg(m_plan.blocks.count())
			.arg(m_plan.framesConsidered)
			.arg(m_plan.framesSkipped));

	m_apply->setEnabled(!m_plan.isEmpty());
}
