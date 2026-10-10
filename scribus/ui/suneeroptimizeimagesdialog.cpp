/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneeroptimizeimagesdialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "pageitem.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneerimageresample.h"
#include "undomanager.h"

void SuneerOptimizeImagesDialog::openFor(ScribusDoc* doc, QWidget* parent)
{
	if (!doc)
		return;
	QList<PageItem*> items;
	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* it = doc->m_Selection->itemAt(i);
		if (it && it->isImageFrame() && it->imageIsAvailable && !it->Pfile.isEmpty())
			items << it;
	}
	if (items.isEmpty())
	{
		const QList<PageItem*> all = doc->getAllItems(doc->DocItems);
		for (PageItem* it : all)
			if (it && it->isImageFrame() && it->imageIsAvailable && !it->Pfile.isEmpty())
				items << it;
	}
	if (items.isEmpty())
	{
		QMessageBox::information(parent, tr("Optimize Images"), tr("No image frames with a picture in the selection or the document."));
		return;
	}
	SuneerOptimizeImagesDialog dlg(doc, items, parent);
	dlg.exec();
}

SuneerOptimizeImagesDialog::SuneerOptimizeImagesDialog(ScribusDoc* doc, const QList<PageItem*>& items, QWidget* parent)
	: QDialog(parent), m_doc(doc)
{
	for (PageItem* it : items)
		m_items << QPointer<PageItem>(it);
	setWindowTitle(tr("Optimize Images"));
	resize(820, 480);
	auto* lay = new QVBoxLayout(this);
	auto* top = new QHBoxLayout();
	top->addWidget(new QLabel(tr("Target resolution:"), this));
	m_targetSpin = new QSpinBox(this);
	m_targetSpin->setRange(72, 1200);
	m_targetSpin->setSuffix(tr(" dpi"));
	m_targetSpin->setValue(SuneerImageResample::settingInt(QStringLiteral("fitDpi"), 300));
	top->addWidget(m_targetSpin);
	auto* retickBtn = new QPushButton(tr("Tick those above target"), this);
	top->addWidget(retickBtn);
	top->addStretch(1);
	lay->addLayout(top);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(6);
	m_table->setHorizontalHeaderLabels({ tr("Resample"), tr("Frame"), tr("File"), tr("Pixels"), tr("Effective dpi"), tr("Size") });
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	lay->addWidget(m_table, 1);

	m_totals = new QLabel(this);
	lay->addWidget(m_totals);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	lay->addWidget(m_status);

	auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto* applyBtn = bb->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
	lay->addWidget(bb);
	connect(applyBtn, &QPushButton::clicked, this, &SuneerOptimizeImagesDialog::apply);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(retickBtn, &QPushButton::clicked, this, &SuneerOptimizeImagesDialog::retick);
	connect(m_targetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { SuneerImageResample::setSetting(QStringLiteral("fitDpi"), v); });
	fill();
	retick();
}

void SuneerOptimizeImagesDialog::fill()
{
	m_table->setRowCount(0);
	qint64 total = 0;
	for (int r = 0; r < m_items.size(); ++r)
	{
		PageItem* it = m_items.at(r);
		if (!it)
			continue;
		const SuneerImageResample::Info info = SuneerImageResample::probe(it);
		double effY = 0;
		const double eff = qMin(SuneerImageResample::effectiveDpi(it, &effY), effY);
		const int row = m_table->rowCount();
		m_table->insertRow(row);
		auto* tick = new QTableWidgetItem();
		tick->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		tick->setCheckState(Qt::Unchecked);
		tick->setData(Qt::UserRole, r);
		tick->setData(Qt::UserRole + 1, eff);
		m_table->setItem(row, 0, tick);
		m_table->setItem(row, 1, new QTableWidgetItem(it->itemName()));
		auto* fileItem = new QTableWidgetItem(QFileInfo(it->Pfile).fileName());
		fileItem->setToolTip(it->Pfile);
		m_table->setItem(row, 2, fileItem);
		m_table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1 x %2").arg(info.width).arg(info.height)));
		auto* dpiItem = new QTableWidgetItem(QStringLiteral("%1 (%2)").arg(qRound(eff)).arg(SuneerImageResample::dpiWord(eff)));
		dpiItem->setForeground(SuneerImageResample::dpiColor(eff));
		m_table->setItem(row, 4, dpiItem);
		m_table->setItem(row, 5, new QTableWidgetItem(QLocale().formattedDataSize(info.bytes, 1)));
		total += info.bytes;
	}
	m_table->resizeColumnsToContents();
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_totals->setText(tr("%n picture(s), %1 in total", "", m_table->rowCount()).arg(QLocale().formattedDataSize(total, 1)));
}

void SuneerOptimizeImagesDialog::retick()
{
	const int target = m_targetSpin->value();
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		QTableWidgetItem* tick = m_table->item(row, 0);
		tick->setCheckState(tick->data(Qt::UserRole + 1).toDouble() > target * 1.05 ? Qt::Checked : Qt::Unchecked);
	}
}

void SuneerOptimizeImagesDialog::apply()
{
	const int target = m_targetSpin->value();
	QList<PageItem*> todo;
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		QTableWidgetItem* tick = m_table->item(row, 0);
		if (tick->checkState() != Qt::Checked)
			continue;
		PageItem* it = m_items.value(tick->data(Qt::UserRole).toInt());
		if (it)
			todo << it;
	}
	if (todo.isEmpty())
	{
		m_status->setText(tr("Nothing ticked."));
		return;
	}
	qint64 before = 0, after = 0;
	for (PageItem* it : todo)
		before += QFileInfo(it->Pfile).size();
	SuneerImageResample::Options opt;
	opt.jpegQuality = SuneerImageResample::settingInt(QStringLiteral("jpegQuality"), 92);
	opt.parent = this;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::SelectionGroup, Um::IGroup, tr("Optimize Images"), QString(), Um::IGetImage);
	QStringList failed;
	int done = 0;
	QProgressDialog progress(tr("Optimizing pictures..."), tr("Stop"), 0, todo.size(), this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	for (PageItem* it : todo)
	{
		progress.setValue(done);
		progress.setLabelText(tr("%1 (%2 of %3)").arg(QFileInfo(it->Pfile).fileName()).arg(done + 1).arg(todo.size()));
		if (progress.wasCanceled())
			break;
		QString summary;
		const QString err = SuneerImageResample::fitToFrame(m_doc, it, target, opt, &summary);
		if (!err.isEmpty())
			failed << QStringLiteral("%1: %2").arg(QFileInfo(it->Pfile).fileName(), err);
		else
			++done;
		after += QFileInfo(it->Pfile).size();
	}
	progress.setValue(todo.size());
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	fill();
	retick();
	QString msg = tr("%n picture(s) resampled, %1 -> %2", "", done).arg(QLocale().formattedDataSize(before, 1), QLocale().formattedDataSize(after, 1));
	if (!failed.isEmpty())
		msg += "\n" + tr("Not done:") + "\n" + failed.join('\n');
	m_status->setText(msg);
}
