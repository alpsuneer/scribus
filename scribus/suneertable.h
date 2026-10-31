/* suneer: table helpers - row height mode (auto-grow / fixed), fit rows to text, and the item the
   Properties palette / toolbar should format when a table is selected. */
#ifndef SUNEERTABLE_H
#define SUNEERTABLE_H

#include <QList>
#include <QSet>
class PageItem;
class PageItem_Table;
class PageItem_TextFrame;
class ScribusDoc;
class Selection;
class ScrSpinBox;

namespace SuneerTable
{
	//! true (default) when the rows of \a table grow while typing; false = fixed height, overflow marker
	bool rowsAuto(const PageItem* table);
	void setRowsAuto(PageItem* table, bool on);
	//! every row as high as its tallest cell text; returns rows changed. One undo step.
	int fitRowsToText(ScribusDoc* doc, PageItem_Table* table);
	//! the text frames to format: selected cells in table edit mode (active cell when none), every cell otherwise
	QList<PageItem_TextFrame*> cellFrames(PageItem_Table* table, bool editMode);
	//! undo/redo of fitRowsToText: apply the packed row heights (space separated points)
	void restoreRowHeights(PageItem_Table* table, const QString& packed);
	//! the frames a text operation on \a sel works on: cells of a table (selected in edit mode, all otherwise,
	//! whole text), the item itself for anything else
	void expandTargets(ScribusDoc* doc, Selection* sel, QList<PageItem*>& targets, QSet<PageItem*>& wholeText);
	//! the cell frames the Properties / toolbar read from for the selected table (empty: not a table)
	QList<PageItem_TextFrame*> readFrames(ScribusDoc* doc);
	//! true when \a value differs between the frames (compare by \a get)
	template <class F> bool mixed(const QList<PageItem_TextFrame*>& frames, F get)
	{
		if (frames.size() < 2) return false;
		auto first = get(frames.first());
		for (PageItem_TextFrame* f : frames) if (get(f) != first) return true;
		return false;
	}
	//! the frames of readFrames() that hold text (an empty cell has no value to compare)
	QList<PageItem_TextFrame*> readFramesWithText(ScribusDoc* doc);
	//! show "\u2014" in \a sb (on) or a normal value again (off); signals blocked
	void showMixedSpin(ScrSpinBox* sb, bool on);
	//! true while \a sb shows "\u2014" (the handler must not apply the minimum)
	bool spinIsMixed(const ScrSpinBox* sb);
	//! Ctrl+A in table edit mode: every cell selected AND the text of every cell selected, so the
	//! palette, the toolbar and the document funnels all see one multi-cell text target
	void selectAllCells(PageItem_Table* table);
	//! what a Properties widget applies to: the selected table itself when the shown item is one of its cells
	PageItem* applyTarget(ScribusDoc* doc, PageItem* shownItem);
}

#endif
