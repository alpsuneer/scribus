#include "suneertable.h"

#include <QSet>

#include "pageitem.h"
#include "pageitem_table.h"
#include "pageitem_textframe.h"
#include "pagestructs.h"
#include "scribusdoc.h"
#include "selection.h"
#include "tablecell.h"
#include "text/textlayout.h"
#include "text/boxes.h"
#include "undomanager.h"
#include "undotransaction.h"
#include "undostate.h"
#include "ui/scrspinbox.h"
#include <QSignalBlocker>

namespace SuneerTable
{

bool rowsAuto(const PageItem* table)
{
	if (!table)
		return true;
	const QList<ObjectAttribute> a = table->getObjectAttributes(QStringLiteral("table.rowHeight"));
	return a.isEmpty() || a.first().value != QLatin1String("fixed");
}

void setRowsAuto(PageItem* table, bool on)
{
	if (!table)
		return;
	ObjAttrVector* attrs = table->getObjectAttributes();
	for (int i = attrs->size() - 1; i >= 0; --i)
		if (attrs->at(i).name == QLatin1String("table.rowHeight"))
			attrs->removeAt(i);
	if (!on)
	{
		ObjectAttribute oa;
		oa.name = QStringLiteral("table.rowHeight");
		oa.type = QStringLiteral("string");
		oa.value = QStringLiteral("fixed");
		attrs->append(oa);
	}
}

static double lineHeightOf(PageItem_TextFrame* tf)
{
	double lineH = 14.0;
	if (tf && tf->itemText.length() > 0)
	{
		const double fs = tf->itemText.charStyle(0).fontSize() / 10.0;
		const ParagraphStyle& ps = tf->itemText.paragraphStyle(0);
		lineH = (ps.lineSpacingMode() == ParagraphStyle::FixedLineSpacing) ? ps.lineSpacing() : fs * 1.2;
	}
	return qMax(2.0, lineH);
}

int fitRowsToText(ScribusDoc* doc, PageItem_Table* table)
{
	if (!doc || !table)
		return 0;
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(table->getUName(), table->getUPixmap(), QObject::tr("Fit rows to text"), QString(), nullptr);
	int changed = 0;
	const QList<double> oldHeights = table->rowHeights();
	for (int r = 0; r < table->rows(); ++r)
	{
		// the cells of this row that hold text (spans excluded)
		QList<PageItem_TextFrame*> frames;
		for (int c = 0; c < table->columns(); ++c)
		{
			TableCell cell = table->cellAt(r, c);
			if (!cell.isValid() || cell.row() != r || cell.column() != c || cell.rowSpan() != 1)
				continue;
			if (cell.textFrame() && cell.textFrame()->itemText.length() > 0)
				frames << cell.textFrame();
		}
		if (frames.isEmpty())
			continue;   // an empty row keeps its height
		const double before = table->rowHeight(r);
		// 1. shrink to the tallest laid-out text (plus insets), never below the minimum
		double need = PageItem_Table::MinimumRowHeight;
		for (PageItem_TextFrame* tf : frames)
		{
			tf->layout();
			double h = tf->textLayout.box() ? tf->textLayout.box()->naturalHeight() : 0.0;
			need = qMax(need, h + tf->textToFrameDistTop() + tf->textToFrameDistBottom() + 1.0);
		}
		if (need < before - 0.05)
			table->resizeRow(r, need, PageItem_Table::MoveFollowing);
		// 2. grow line by line while any cell of the row still overflows (the layout only knows the
		//    lines that fit, so the height is found by growing, as typing does)
		for (int guard = 0; guard < 400; ++guard)
		{
			PageItem_TextFrame* over = nullptr;
			for (PageItem_TextFrame* tf : frames)
			{
				tf->layout();
				if (tf->frameOverflows()) { over = tf; break; }
			}
			if (!over)
				break;
			table->resizeRow(r, table->rowHeight(r) + lineHeightOf(over), PageItem_Table::MoveFollowing);
		}
		if (qAbs(table->rowHeight(r) - before) > 0.05)
			++changed;
	}
	if (changed)
	{
		doc->dontResize = true;
		table->adjustFrameToTable();
		doc->dontResize = false;
		table->update();
		doc->changed();
		doc->regionsChanged()->update(QRectF());
		// resizeRow() records nothing: one state with every row height, restored by PageItem::restore
		if (UndoManager::undoEnabled())
		{
			QStringList o, n;
			for (double h : oldHeights) o << QString::number(h, 'f', 4);
			for (double h : table->rowHeights()) n << QString::number(h, 'f', 4);
			auto* ss = new SimpleState(QObject::tr("Fit rows to text"), QString(), nullptr);
			ss->set("SUNEER_ROWHEIGHTS");
			ss->set("OLD", o.join(' '));
			ss->set("NEW", n.join(' '));
			UndoManager::instance()->action(table, ss);
		}
	}
	if (tx)
		tx.commit();
	return changed;
}

void restoreRowHeights(PageItem_Table* table, const QString& packed)
{
	if (!table)
		return;
	const QStringList parts = packed.split(' ', Qt::SkipEmptyParts);
	for (int r = 0; r < qMin(parts.size(), table->rows()); ++r)
		table->resizeRow(r, parts[r].toDouble(), PageItem_Table::MoveFollowing);
	if (table->doc())
	{
		table->doc()->dontResize = true;
		table->adjustFrameToTable();
		table->doc()->dontResize = false;
		table->update();
		table->doc()->regionsChanged()->update(QRectF());
	}
}

QList<PageItem_TextFrame*> cellFrames(PageItem_Table* table, bool editMode)
{
	QList<PageItem_TextFrame*> out;
	if (!table)
		return out;
	if (editMode)
	{
		if (table->hasSelection())
		{
			for (const TableCell& cell : table->selectedCells())
				if (cell.textFrame()) out << cell.textFrame();
		}
		else
		{
			// suneer: the text of several cells selected without a cell selection (the old canvas
			// Ctrl+A state, or a document that still carries it): those cells are the target, whole
			QList<PageItem_TextFrame*> textSel;
			for (int r = 0; r < table->rows(); ++r)
				for (int c = 0; c < table->columns(); ++c)
				{
					TableCell cell = table->cellAt(r, c);
					if (!cell.isValid() || cell.row() != r || cell.column() != c)
						continue;
					PageItem_TextFrame* tf = cell.textFrame();
					if (tf && tf->HasSel && tf->itemText.length() > 0 && tf->itemText.hasSelection()
					    && tf->itemText.startOfSelection() == 0 && tf->itemText.endOfSelection() == tf->itemText.length())
						textSel << tf;
				}
			if (textSel.size() >= 2)
				return textSel;
			if (table->activeCell().textFrame())
				out << table->activeCell().textFrame();
		}
		return out;
	}
	for (int r = 0; r < table->rows(); ++r)
		for (int c = 0; c < table->columns(); ++c)
		{
			TableCell cell = table->cellAt(r, c);
			if (cell.isValid() && cell.row() == r && cell.column() == c && cell.textFrame())
				out << cell.textFrame();
		}
	return out;
}

PageItem* applyTarget(ScribusDoc* doc, PageItem* shownItem)
{
	if (!doc || !doc->m_Selection || doc->m_Selection->count() != 1)
		return shownItem;
	PageItem* sel = doc->m_Selection->itemAt(0);
	if (sel && sel->isTable())
		return sel;   // the document's apply funnels spread it over the selected / all cells
	return shownItem;
}

} // namespace SuneerTable

namespace SuneerTable
{

void expandTargets(ScribusDoc* doc, Selection* sel, QList<PageItem*>& targets, QSet<PageItem*>& wholeText)
{
	targets.clear();
	wholeText.clear();
	if (!doc || !sel)
		return;
	const bool editing = doc->appMode == modeEditTable;
	for (int i = 0; i < sel->count(); ++i)
	{
		PageItem* it = sel->itemAt(i);
		if (!it)
			continue;
		if (!it->isTable())
		{
			targets << it;
			continue;
		}
		PageItem_Table* table = it->asTable();
		const QList<PageItem_TextFrame*> frames = cellFrames(table, editing);
		// one cell, no cell selection (plain text editing): the cursor / text selection of that cell
		if (editing && !table->hasSelection() && frames.size() == 1)
		{
			targets << frames.first();
			continue;
		}
		for (PageItem_TextFrame* cf : frames)
		{
			targets << cf;
			wholeText.insert(cf);
		}
	}
}

QList<PageItem_TextFrame*> readFrames(ScribusDoc* doc)
{
	QList<PageItem_TextFrame*> out;
	if (!doc || !doc->m_Selection || doc->m_Selection->count() != 1)
		return out;
	PageItem* it = doc->m_Selection->itemAt(0);
	if (!it || !it->isTable())
		return out;
	return cellFrames(it->asTable(), doc->appMode == modeEditTable);
}

void selectAllCells(PageItem_Table* table)
{
	if (!table || table->rows() < 1 || table->columns() < 1)
		return;
	if (PageItem_TextFrame* active = table->activeCell().textFrame())
	{
		active->itemText.deselectAll();
		active->HasSel = false;
	}
	table->selectCells(0, 0, table->rows() - 1, table->columns() - 1);
	for (int r = 0; r < table->rows(); ++r)
		for (int c = 0; c < table->columns(); ++c)
		{
			PageItem_TextFrame* tf = table->cellAt(r, c).textFrame();
			if (tf && tf->itemText.length() > 0)
			{
				tf->itemText.selectAll();
				tf->HasSel = true;
				tf->update();
			}
		}
	table->update();
}

QList<PageItem_TextFrame*> readFramesWithText(ScribusDoc* doc)
{
	QList<PageItem_TextFrame*> out;
	for (PageItem_TextFrame* f : readFrames(doc))
		if (f && f->itemText.length() > 0)
			out << f;
	return out;
}

void showMixedSpin(ScrSpinBox* sb, bool on)
{
	if (!sb)
		return;
	QSignalBlocker b(sb);
	if (on)
	{
		sb->setSpecialValueText(QStringLiteral("\u2014"));
		sb->setValue(sb->minimum());
	}
	else if (!sb->specialValueText().isEmpty())
		sb->setSpecialValueText(QString());
}

bool spinIsMixed(const ScrSpinBox* sb)
{
	return sb && !sb->specialValueText().isEmpty() && sb->value() <= sb->minimum();
}

} // namespace SuneerTable
