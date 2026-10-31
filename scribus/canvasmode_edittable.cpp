/*
Copyright (C) 2011 Elvis Stansvik <elvstone@gmail.com>

For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QCursor>
#include <QDebug>
#include <QPainter>
#include <QPointF>
#include <QTimer>

#include "appmodes.h"
#include "canvas.h"
#include "canvasgesture_cellselect.h"
#include "canvasgesture_columnresize.h"
#include "canvasgesture_rowresize.h"
#include "canvasgesture_tableresize.h"
#include "canvasmode_edittable.h"
#include "suneertable.h"
#include "fpoint.h"
#include "iconmanager.h"
#include "pageitem_table.h"
#include "text/specialchars.h"
#include "commonstrings.h"
#include "hyphenator.h"
#include "undostate.h"
#include "undomanager.h"
#include <QSettings>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QGuiApplication>
#include "scribus.h"
#include "scribusdoc.h"
#include "ui/suneercontrolbar.h"
#include "scribusview.h"
#include "selection.h"
#include "tablehandle.h"
#include "ui/contextmenu.h"

// TODO: We should have a preference for this instead.
#ifdef Q_OS_MACOS
	static const Qt::KeyboardModifiers CellNavigationModifiers = Qt::AltModifier | Qt::KeypadModifier;
#else
	static const Qt::KeyboardModifiers CellNavigationModifiers = Qt::AltModifier;
#endif

CanvasMode_EditTable::CanvasMode_EditTable(ScribusView* view) : CanvasMode(view),
	m_canvasUpdateTimer(new QTimer(view)),
	m_selectRowCursor(IconManager::instance().loadCursor("cursor-select-row")),
	m_selectColumnCursor(IconManager::instance().loadCursor("cursor-select-column")),
	m_tableResizeGesture(new TableResize(this)),
	m_rowResizeGesture(new RowResize(this)),
	m_columnResizeGesture(new ColumnResize(this)),
	m_cellSelectGesture(new CellSelect(this)),
	m_ScMW(view->m_ScMW)
{
	connect(m_canvasUpdateTimer, SIGNAL(timeout()), this, SLOT(updateCanvas()));
}

CanvasMode_EditTable::~CanvasMode_EditTable()
{
	delete m_tableResizeGesture;
	delete m_rowResizeGesture;
	delete m_columnResizeGesture;
	delete m_cellSelectGesture;
}

void CanvasMode_EditTable::activate(bool fromGesture)
{
	CanvasMode::activate(fromGesture);

	PageItem *item = m_doc->m_Selection->itemAt(0);
	Q_ASSERT(item && item->isTable());
	m_table = item->asTable();

	if (fromGesture)
		m_view->setCursor(Qt::ArrowCursor);

	m_lastCursorPos = -1;
	m_blinkTime.start();
	m_canvasUpdateTimer->start(200);
	makeLongTextCursorBlink();
	m_preeditText.clear();
	m_preeditPos = -1;
	applyMalayalamFallback();

	m_view->m_ScMW->updateTableMenuActions();
}

// The input method (fcitx5 + m17n Malayalam) sends its text as QInputMethodEvent, never as key
// presses: without this the composition and the committed text were dropped in table edit mode.
// Same rules as CanvasMode_Edit::inputMethodEvent, on the active cell's text frame.
void CanvasMode_EditTable::inputMethodEvent(QInputMethodEvent* event)
{
	PageItem_TextFrame* tf = (m_table && !m_table->hasSelection()) ? m_table->activeCell().textFrame() : nullptr;
	if (!tf)
	{
		event->accept();
		return;
	}
	StoryText& text = tf->itemText;
	// the composition shown so far goes away; the event brings the new one
	if (!m_preeditText.isEmpty() && m_preeditPos >= 0 && m_preeditPos + m_preeditText.length() <= text.length())
	{
		text.removeChars(m_preeditPos, m_preeditText.length());
		text.setCursorPosition(m_preeditPos);
	}
	m_preeditText.clear();
	m_preeditPos = -1;

	UndoTransaction replaceTrans;
	if (text.hasSelection() && (!event->commitString().isEmpty() || !event->preeditString().isEmpty()))
	{
		CharStyle replacedStyle(text.charStyle(text.startOfSelection()));
		if (UndoManager::undoEnabled())
			replaceTrans = undoManager->beginTransaction(Um::Selection, Um::IGroup, Um::ReplaceText, "", Um::IDelete);
		tf->deleteSelectedTextFromFrame();
		text.setPendingCharStyle(replacedStyle);
		tf->HasSel = false;
	}
	if (event->replacementLength() > 0)
	{
		const int pos = text.cursorPosition() + event->replacementStart();
		if (pos >= 0 && pos + event->replacementLength() <= text.length())
		{
			text.removeChars(pos, event->replacementLength());
			text.setCursorPosition(pos);
		}
	}
	if (!event->commitString().isEmpty())
	{
		const int pos = text.cursorPosition();
		if (UndoManager::undoEnabled())
		{
			SimpleState* ss = dynamic_cast<SimpleState*>(undoManager->getLastUndo());
			if (ss && ss->get("ETEA") == "insert_frametext" && ss->undoObject() == tf)
				ss->set("TEXT_STR", ss->get("TEXT_STR") + event->commitString());
			else
			{
				ss = new SimpleState(Um::InsertText, "", Um::ICreate);
				ss->set("INSERT_FRAMETEXT");
				ss->set("ETEA", QString("insert_frametext"));
				ss->set("TEXT_STR", event->commitString());
				ss->set("START", pos);
				undoManager->action(tf, ss);
			}
		}
		text.insertChars(pos, event->commitString(), true);
		text.setCursorPosition(pos + event->commitString().length());
		// hyphenation as in a text frame: the word just finished
		if (m_doc->docHyphenator && m_doc->docHyphenator->autoCheck() && event->commitString().contains(QLatin1Char(' ')))
			m_doc->docHyphenator->slotHyphenate(tf);
	}
	if (replaceTrans)
		replaceTrans.commit();
	if (!event->preeditString().isEmpty())
	{
		m_preeditPos = text.cursorPosition();
		m_preeditText = event->preeditString();
		text.insertChars(m_preeditPos, m_preeditText, true);
		text.setCursorPosition(m_preeditPos + m_preeditText.length());
	}
	tf->invalidateLayout();
	tf->layout();
	growActiveCellIfNeeded();
	if (!event->commitString().isEmpty())
		m_doc->changed();
	makeLongTextCursorBlink();
	updateCanvas(true);
	m_view->suneerKeepCursorVisible();
	QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
	event->accept();
}

void CanvasMode_EditTable::growActiveCellIfNeeded()
{
	PageItem_TextFrame* tf = m_table ? m_table->activeCell().textFrame() : nullptr;
	if (!tf)
		return;
	tf->layout();
	if (!tf->frameOverflows())
		return;
	if (!SuneerTable::rowsAuto(m_table))   // "Row height: Fixed": the cell shows the overflow marker instead
		return;
	const int row = m_table->activeCell().row();
	// Calculate exact height needed
	double lineH = 14.0;
	if (tf->itemText.length() > 0)
	{
		const double fs = tf->itemText.charStyle(0).fontSize() / 10.0;
		const ParagraphStyle& ps = tf->itemText.paragraphStyle(0);
		if (ps.lineSpacingMode() == ParagraphStyle::FixedLineSpacing)
			lineH = ps.lineSpacing();
		else
			lineH = fs * 1.2;
	}
	// Add exactly one line height
	const double currentH = m_table->rowHeight(row);
	m_table->resizeRow(row, currentH + lineH);
	// the table frame grows with its rows (downward), as the Tab-new-row path does
	m_doc->dontResize = true;
	m_table->adjustFrameToTable();
	m_doc->dontResize = false;
	tf->layout();
	tf->update();
	m_doc->changed();
}

// Boxes instead of Malayalam: a cell whose default font cannot show U+0D15 takes the house body
// style when the document has it and that style's font can. The frame default and the paragraphs
// still on the document default style change; a paragraph styled by hand is left alone.
void CanvasMode_EditTable::applyMalayalamFallback()
{
	if (!m_table || !m_doc)
		return;
	QSettings cfg("Faircode", "ScribusNews");
	const QString body = cfg.value("style/bodyStyle", "02 BodyText").toString().trimmed();
	if (body.isEmpty() || !m_doc->paragraphStyles().contains(body))
		return;
	const QChar ka(0x0D15);
	if (!m_doc->paragraphStyles().get(body).charStyle().font().canRender(ka))
		return;
	int changed = 0;
	for (int r = 0; r < m_table->rows(); ++r)
		for (int c = 0; c < m_table->columns(); ++c)
		{
			TableCell cell = m_table->cellAt(r, c);
			if (!cell.isValid() || cell.row() != r || cell.column() != c)
				continue;
			PageItem_TextFrame* tf = cell.textFrame();
			if (!tf)
				continue;
			StoryText& st = tf->itemText;
			if (st.defaultStyle().charStyle().font().canRender(ka))
				continue;
			ParagraphStyle ps(st.defaultStyle());
			ps.setParent(body);
			st.setDefaultStyle(ps);
			for (int pos = 0; pos < st.length(); )
			{
				int end = pos;
				while (end < st.length() && st.text(end) != SpecialChars::PARSEP)
					++end;
				const QString parent = st.paragraphStyle(pos).parent();
				if (parent.isEmpty() || parent == CommonStrings::DefaultParagraphStyle || parent == CommonStrings::trDefaultParagraphStyle)
				{
					ParagraphStyle p;
					p.setParent(body);
					st.applyStyle(pos, p, false);
				}
				pos = end + 1;
			}
			tf->invalidateLayout();
			++changed;
		}
	if (changed > 0)
	{
		m_table->update();
		m_doc->changed();
	}
}

void CanvasMode_EditTable::deactivate(bool forGesture)
{
	if (!forGesture)
		m_canvasUpdateTimer->stop();

	m_view->m_ScMW->updateTableMenuActions();
	CanvasMode::deactivate(forGesture);
}

void CanvasMode_EditTable::keyPressEvent(QKeyEvent* event)
{
	event->accept();

	// Handle some keys specific to this mode.
	if (event->key() == Qt::Key_Escape)
	{
		// Deselect text in active frame.
		PageItem_TextFrame* activeFrame = m_table->activeCell().textFrame();
		activeFrame->itemText.deselectAll();
		activeFrame->HasSel = false;
		// Go back to normal mode.
		m_view->requestMode(modeNormal);
	}

	// Long text cursor blink on PgUp/PgDown/Up/Down/Home/End.
	switch (event->key())
	{
		case Qt::Key_PageUp:
		case Qt::Key_PageDown:
		case Qt::Key_Up:
		case Qt::Key_Down:
		case Qt::Key_Home:
		case Qt::Key_End:
			makeLongTextCursorBlink();
			break;
	}

	// Handle keyboard navigation between cells.
	if (event->modifiers() == CellNavigationModifiers)
	{
		switch (event->key())
		{
			case Qt::Key_Left:
				// Move left
				m_table->moveLeft();
				makeLongTextCursorBlink();
				return;
			case Qt::Key_Right:
				// Move right.
				m_table->moveRight();
				makeLongTextCursorBlink();
				return;
			case Qt::Key_Up:
				// Move up.
				m_table->moveUp();
				makeLongTextCursorBlink();
				return;
			case Qt::Key_Down:
				// Move down.
				m_table->moveDown();
				makeLongTextCursorBlink();
				return;
		}
	}

	// Ctrl+A → every cell selected + the text of every cell (one multi-cell text target for the
	// Text Properties palette, the toolbar and every document funnel; before: text only, so a
	// setter fell back to the cursor character of the active cell)
	if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier))
	{
		SuneerTable::selectAllCells(m_table);
		updateCanvas(true);
		return;
	}

	// Tab key → move to next cell (LibreOffice style)
	if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ShiftModifier))
	{
		int curCol = m_table->activeCell().column();
		int curRow = m_table->activeCell().row();
		int cols = m_table->columns();
		int rows = m_table->rows();
		if (curCol < cols - 1) {
			// Move to next column
			m_table->moveRight();
		} else if (curRow < rows - 1) {
			// End of row → go to first cell of next row
			m_table->moveDown();
			for (int i = 0; i < curCol; i++)
				m_table->moveLeft();
		} else {
			// Last cell → add new row
			double defaultRowH = m_table->rowHeight(curRow);
			m_table->insertRows(rows, 1);
			// Resize new row same as previous row
			m_table->resizeRow(rows, defaultRowH);
			// Grow frame by one row height
			m_table->setHeight(m_table->height() + defaultRowH);
			// Same as manual resize — adjustFrameToTable
			m_doc->dontResize = true;
			m_table->adjustFrameToTable();
			m_doc->dontResize = false;
			m_table->moveDown();
			for (int i = 0; i < curCol; i++)
				m_table->moveLeft();
			m_table->update();
			m_doc->changed();
			m_doc->regionsChanged()->update(QRectF());
			updateCanvas(true);
		}
		makeLongTextCursorBlink();
		updateCanvas(true);
		return;
	}
	// Shift+Tab → move to previous cell
	if (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::ShiftModifier))
	{
		int curCol = m_table->activeCell().column();
		int curRow = m_table->activeCell().row();
		int cols = m_table->columns();
		if (curCol > 0) {
			m_table->moveLeft();
		} else if (curRow > 0) {
			m_table->moveUp();
			for (int i = 0; i < cols - 1; i++)
				m_table->moveRight();
		}
		makeLongTextCursorBlink();
		updateCanvas(true);
		return;
	}
	// Pass all other keys to text frame of active cell.
	if (!m_table->hasSelection())
	{
		bool repeat;
		// a composition in progress is dropped by a plain key (as the input method does)
		if (!m_preeditText.isEmpty() && m_preeditPos >= 0)
		{
			PageItem_TextFrame* pf = m_table->activeCell().textFrame();
			if (pf && m_preeditPos + m_preeditText.length() <= pf->itemText.length())
				pf->itemText.removeChars(m_preeditPos, m_preeditText.length());
			m_preeditText.clear();
			m_preeditPos = -1;
		}
		m_table->activeCell().textFrame()->handleModeEditKey(event, repeat);
		// Auto-grow: only when text needs a new line
		growActiveCellIfNeeded();
	}
	updateCanvas(true);
	m_view->suneerKeepCursorVisible();
}

void CanvasMode_EditTable::mouseMoveEvent(QMouseEvent* event)
{
	event->accept();

	QPointF canvasPoint = m_canvas->globalToCanvas(event->globalPosition()).toQPointF();
	double threshold = m_doc->guidesPrefs().grabRadius / m_canvas->scale();
	TableHandle handle = m_table->hitTest(canvasPoint, threshold);

	if (event->buttons() & Qt::LeftButton)
	{
		// Mouse is being dragged with left button pressed.
		handleMouseDrag(event);
	}
	else
	{
		// Set an appropriate cursor.
		QCursor cursor(Qt::ArrowCursor);
		switch (handle.type())
		{
			case TableHandle::RowSelect:
				cursor = m_selectRowCursor;
				break;
			case TableHandle::RowResize:
				cursor = Qt::SizeVerCursor;
				break;
			case TableHandle::ColumnSelect:
				cursor = m_selectColumnCursor;
				break;
			case TableHandle::ColumnResize:
				cursor = Qt::SizeHorCursor;
				break;
			case TableHandle::TableResize:
				cursor = Qt::SizeFDiagCursor;
				break;
			case TableHandle::CellSelect:
				cursor = Qt::IBeamCursor;
				break;
			case TableHandle::None:
				break;
			default:
				qWarning("Unknown hit target");
				break;
		}
		m_view->setCursor(cursor);
	}
}

void CanvasMode_EditTable::mousePressEvent(QMouseEvent* event)
{
	PageItem_TextFrame* activeFrame;

	event->accept();
	QPointF canvasPoint = m_canvas->globalToCanvas(event->globalPosition()).toQPointF();
	QPoint globalPos = event->globalPosition().toPoint();
	double threshold = m_doc->guidesPrefs().grabRadius / m_canvas->scale();
	TableHandle handle = m_table->hitTest(canvasPoint, threshold);
	TableCell cell;

	if (event->button() == Qt::LeftButton)
	{
		switch (handle.type())
		{
			case TableHandle::RowSelect:
				// Not implemented.
				m_table->clearSelection();
				cell = m_table->cellAt(canvasPoint);
				if (!cell.isValid())
					break;
				// Deselect text in active frame.
				activeFrame = m_table->activeCell().textFrame();
				activeFrame->itemText.deselectAll();
				activeFrame->HasSel = false;
				// Select row and pre-position text cursor
				m_table->moveTo(cell);
				m_table->selectRow(cell.row());
				m_view->slotSetCurs(globalPos.x(), globalPos.y());
				m_lastCursorPos = -1;
				updateCanvas(true);
				break;
			case TableHandle::RowResize:
				// Start row resize gesture.
				m_rowResizeGesture->setup(m_table, handle.index());
				m_view->startGesture(m_rowResizeGesture);
				break;
			case TableHandle::ColumnSelect:
				// Not implemented.
				m_table->clearSelection();
				cell = m_table->cellAt(canvasPoint);
				if (!cell.isValid())
					break;
				// Deselect text in active frame.
				activeFrame = m_table->activeCell().textFrame();
				activeFrame->itemText.deselectAll();
				activeFrame->HasSel = false;
				// Select column and pre-position text cursor
				m_table->moveTo(cell);
				m_table->selectColumn(cell.column());
				m_view->slotSetCurs(globalPos.x(), globalPos.y());
				m_lastCursorPos = -1;
				updateCanvas(true);
				break;
			case TableHandle::ColumnResize:
				// Start column resize gesture.
				m_columnResizeGesture->setup(m_table, handle.index());
				m_view->startGesture(m_columnResizeGesture);
				break;
			case TableHandle::TableResize:
				// Start table resize gesture.
				m_tableResizeGesture->setup(m_table);
				m_view->startGesture(m_tableResizeGesture);
				break;
			case TableHandle::CellSelect:
				// suneer: Ctrl+click adds / removes one cell (non-adjacent selections)
				if (event->modifiers() & Qt::ControlModifier)
				{
					cell = m_table->cellAt(canvasPoint);
					if (cell.isValid())
					{
						if (!m_table->hasSelection())
							m_table->selectCell(m_table->activeCell().row(), m_table->activeCell().column());
						if (m_table->selectedCells().contains(cell))
							m_table->deselectCell(cell.row(), cell.column());
						else
							m_table->selectCell(cell.row(), cell.column());
						m_table->moveTo(cell);
						m_view->m_ScMW->setTBvals(m_table->activeCell().textFrame());
						if (m_view->m_ScMW->m_suneerControlBar)
							m_view->m_ScMW->m_suneerControlBar->updateFromSelection();
						updateCanvas(true);
					}
					break;
				}
				// Move to the pressed cell and position the text cursor.
				m_table->clearSelection();
				m_table->moveTo(m_table->cellAt(canvasPoint));
				m_view->slotSetCurs(globalPos.x(), globalPos.y());
				m_lastCursorPos = m_table->activeCell().textFrame()->itemText.cursorPosition();
				m_view->m_ScMW->setTBvals(m_table->activeCell().textFrame());
				// Update SuneerControlBar
				if (m_view->m_ScMW->m_suneerControlBar)
					m_view->m_ScMW->m_suneerControlBar->updateFromSelection();
				makeLongTextCursorBlink();
				updateCanvas(true);
				break;
			case TableHandle::None:
				// Deselect text in active frame.
				activeFrame = m_table->activeCell().textFrame();
				activeFrame->itemText.deselectAll();
				activeFrame->HasSel = false;
				// Deselect the table and go back to normal mode.
				m_view->deselectItems(true);
				m_view->requestMode(modeNormal);
				m_view->canvasMode()->mousePressEvent(event);
				break;
			default:
				qWarning("Unknown hit target");
				break;
		}
	}
	else if (event->button() == Qt::RightButton)
	{
		// Show the table popup menu.
		const FPoint mousePointDoc(m_canvas->globalToCanvas(event->globalPosition()));
		createContextMenu(m_table, mousePointDoc.x(), mousePointDoc.y());
	}
}

void CanvasMode_EditTable::mouseReleaseEvent(QMouseEvent* event)
{
	m_lastCursorPos = -1;
}

void CanvasMode_EditTable::mouseDoubleClickEvent(QMouseEvent* event)
{
	TableCell hitCell = m_table->cellAt(m_canvas->globalToCanvas(event->globalPosition()).toQPointF());
	if (!hitCell.isValid())
		return;

	event->accept();

	PageItem_TextFrame* textFrame = hitCell.textFrame();
	if (event->modifiers() & Qt::ControlModifier)
	{
		int start = 0, end = 0;

		if (event->modifiers() & Qt::ShiftModifier)
		{
			// Double click with Ctrl+Shift to select multiple paragraphs.
			uint oldParNr = textFrame->itemText.nrOfParagraph(m_lastCursorPos);
			uint newParNr = textFrame->itemText.nrOfParagraph();

			start = textFrame->itemText.startOfParagraph(qMin(oldParNr, newParNr));
			end = textFrame->itemText.endOfParagraph(qMax(oldParNr, newParNr));
		}
		else
		{
			// Double click with Ctrl to select a single paragraph.
			m_lastCursorPos = textFrame->itemText.cursorPosition();
			uint parNr = textFrame->itemText.nrOfParagraph(m_lastCursorPos);
			start = textFrame->itemText.startOfParagraph(parNr);
			end = textFrame->itemText.endOfParagraph(parNr);
		}
		textFrame->itemText.deselectAll();
		textFrame->itemText.extendSelection(start, end);
		textFrame->itemText.setCursorPosition(end);
	}
	else
	{
		// Regular double click selects a word.
		m_lastCursorPos = textFrame->itemText.cursorPosition();
		textFrame->itemText.selectWord(textFrame->itemText.cursorPosition());
	}

	updateCanvas(true);
}

void CanvasMode_EditTable::drawControls(QPainter* p)
{
	p->save();
	commonDrawControls(p, false);
	if (!m_table->hasSelection())
		drawTextCursor(p);
	p->restore();

	if (m_table->hasSelection())
		paintCellSelection(p);
}

void CanvasMode_EditTable::paintCellSelection(QPainter* p)
{
	if (!m_table || !m_canvas || !p)
		return;

	p->save();
	p->scale(m_canvas->scale(), m_canvas->scale());
	p->translate(-m_doc->minCanvasCoordinate.x(), -m_doc->minCanvasCoordinate.y());
	p->setTransform(m_table->getTransform(), true);
	p->setRenderHint(QPainter::Antialiasing);
	p->setPen(QPen(QColor(100, 200, 255), 3.0 / m_canvas->scale(), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
	p->setBrush(QColor(100, 200, 255, 50));

	/*
	* The code below makes selectionPath a union of the cell rectangles of the selected cells.
	* Since the cell rectangles are adjacent, they must be expanded slightly (1.0) for the
	* uniting to work. This may not be the fastest way to compose the path of the selection,
	* but it makes for some very simple code. And the result looks good.
	*/

	const QPointF offset = m_table->gridOffset();
	QPainterPath selectionPath;

	for (const TableCell& cell : m_table->selectedCells())
	{
		QRectF cellRect = cell.boundingRect();
		cellRect.translate(offset);
		cellRect.adjust(-1.0, -1.0, 1.0, 1.0);
		QPainterPath cellPath;
		cellPath.addRect(cellRect);
		selectionPath = selectionPath.united(cellPath);
	}

	p->drawPath(selectionPath);
	p->restore();
}

void CanvasMode_EditTable::updateCanvas(bool forceRedraw)
{
	// Do not let update timer cancel forced redraw otherwise
	// we get refresh issues  when typing or selecting text
	if (!m_canvas->isForcedRedraw())
		m_canvas->setForcedRedraw(forceRedraw);
	m_canvas->update(m_canvas->canvasToLocal(m_table->getBoundingRect()));
}

void CanvasMode_EditTable::handleMouseDrag(QMouseEvent* event)
{
	TableCell hitCell = m_table->cellAt(m_canvas->globalToCanvas(event->globalPosition()).toQPointF());
	TableCell activeCell = m_table->activeCell();
	PageItem_TextFrame* activeFrame = activeCell.textFrame();

	if ((!hitCell.isValid() || hitCell == activeCell) && m_lastCursorPos != -1)
	{
		// Select text in active cell text frame.
		activeFrame->itemText.deselectAll();
		activeFrame->HasSel = false;

		QPoint globalPos = event->globalPosition().toPoint();
		m_view->slotSetCurs(globalPos.x(), globalPos.y());

		PageItem_TextFrame* newActiveFrame = m_table->activeCell().textFrame();
		if (activeFrame == newActiveFrame)
		{
			const int selectionStart = qMin(activeFrame->itemText.cursorPosition(), m_lastCursorPos);
			const int selectionLength = qAbs(activeFrame->itemText.cursorPosition() - m_lastCursorPos);

			activeFrame->itemText.select(selectionStart, selectionLength);
			activeFrame->HasSel = (selectionLength > 0);
		}
		else
		{
			m_lastCursorPos = -1;
			m_cellSelectGesture->setup(m_table, activeCell);
			m_view->startGesture(m_cellSelectGesture);
		}
		m_view->m_ScMW->setTBvals(newActiveFrame);
	}
	else
	{
		/*
		 * Mouse moved into another cell, so deselect all text and start
		 * cell selection gesture.
		 */
		activeFrame->itemText.deselectAll();
		activeFrame->HasSel = false;

		m_cellSelectGesture->setup(m_table, activeCell);
		m_view->startGesture(m_cellSelectGesture);
	}

	updateCanvas(true);
}

void CanvasMode_EditTable::drawTextCursor(QPainter* p)
{
	if ((!m_longBlink && m_blinkTime.elapsed() > QApplication::cursorFlashTime() / 2)
		|| (m_longBlink && m_blinkTime.elapsed() > QApplication::cursorFlashTime()))
	{
		// Reset blink timer
		m_blinkTime.restart();
		m_longBlink = false;
		m_cursorVisible = !m_cursorVisible;
	}

	if (m_cursorVisible)
	{
		// Paint text cursor.
		p->save();
		p->setTransform(m_table->getTransform(), true);
		commonDrawTextCursor(p, m_table->activeCell().textFrame(), m_table->gridOffset());
		p->restore();
	}
}

void CanvasMode_EditTable::makeLongTextCursorBlink()
{
	m_cursorVisible = true;
	m_longBlink = true;
	m_blinkTime.restart();
}

void CanvasMode_EditTable::createContextMenu(PageItem *currItem, double mx, double my)
{
	ContextMenu* cmen = nullptr;
	m_view->setCursor(QCursor(Qt::ArrowCursor));
	m_view->setObjectUndoMode();
	if (currItem != nullptr)
		cmen = new ContextMenu(*(m_doc->m_Selection), m_ScMW, m_doc);
	else
		cmen = new ContextMenu(m_ScMW, m_doc, mx, my);
	if (cmen)
		cmen->exec(QCursor::pos());
	m_view->setGlobalUndoMode();
	delete cmen;
}
