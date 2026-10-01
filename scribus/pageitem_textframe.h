/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
                          pageitem.h  -  description
                             -------------------
    copyright            : Scribus Team
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PAGEITEMTEXTFRAME_H
#define PAGEITEMTEXTFRAME_H

#include <QHash>
#include <QRectF>
#include <QString>
#include <QKeyEvent>

#include "scribusapi.h"
#include "pageitem.h"
#include "marks.h"
#include "notesstyles.h"
#include "textframespellchecker.h"

class PageItem_NoteFrame;
class ScPainter;
class ScribusDoc;

typedef QHash<PageItem_NoteFrame*, QList<TextNote *> > NotesInFrameMap;


//cezaryece: I remove static statement and made it public as this function is used also by PageItem_NoteFrame
double calculateLineSpacing (const ParagraphStyle &style, PageItem *item);

class SCRIBUS_API PageItem_TextFrame : public PageItem
{
    bool m_layoutInProgress = false;
    int m_layoutDepth = 0;
	Q_OBJECT

public:
	PageItem_TextFrame(ScribusDoc *pa, double x, double y, double w, double h, double w2, const QString& fill, const QString& outline);
	double m_spanYBottom = -1.0; ///< Bottom Y of span heading area (public for image push-down)
	PageItem_TextFrame(const PageItem & p);
	~PageItem_TextFrame();

	void init();

	PageItem_TextFrame * asTextFrame() override { return this; }
	const PageItem_TextFrame * asTextFrame() const override { return this; }
	bool isTextFrame() const override { return true; }
	bool isTextContainer() const override { return true; }

	void clearContents() override;
	void truncateContents() override;

	/**
	* \brief Handle keyboard interaction with the text frame while in edit mode
	* @param k key event
	* @param keyRepeat a reference to the keyRepeat property
	*/
	void handleModeEditKey(QKeyEvent *k, bool& keyRepeat) override;
	void deleteSelectedTextFromFrame();
	void ExpandSel(int oldPos);
	void deselectAll();

	//for speed up updates when changed was only one frame from chain
	virtual void invalidateLayout(bool wholeChain);
	virtual void invalidateLayout(int firstChar);
	using PageItem::invalidateLayout;
	void layout() override;

	//return true if all previous frames from chain are valid (including that one)
	bool isValidChainFromBegin();
	void setTextAnnotationOpen(bool open);

	double columnWidth();

	//enable/disable marks inserting actions depending on editMode
	void toggleEditModeActions();
	QRegion availableRegion() { return m_availableRegion; }
	int textPositionFromPoint(const QPointF& canvasPoint);

	// Suneer: caret-rendering affinity at a soft-wrap boundary. The story
	// position at a wrap is shared between "end of the line above" and
	// "start of the line below" (see NOTES.md) — cursorBiasBackward already
	// existed to record approach direction for ExpandSel() but was never
	// read; commonDrawTextCursor() (canvasmode.cpp) now consumes it through
	// this accessor to pick which of the two the blinking caret renders at.
	bool caretBiasBackward() const { return cursorBiasBackward; }

	void replaceSpellingErrorText(const SpellError& error, const QString& suggestion);


protected:
	QRegion calcAvailableRegion();
	QRegion m_availableRegion;
	void DrawObj_Item(ScPainter *p, const QRectF& e) override;
	void DrawObj_Post(ScPainter *p) override;
	void DrawObj_Decoration(ScPainter *p) override;
	//void drawOverflowMarker(ScPainter *p);
	void drawUnderflowMarker(ScPainter *p);
	void drawColumnBorders(ScPainter *p);
	void drawSpellCheckSquiggles(ScPainter* p, const QVector<SpellError>& errors);
	void drawSquiggleLine(ScPainter* p, double x, double y, double width);

	bool unicodeTextEditMode {false};
	int unicodeInputCount {0};
	QString unicodeInputString;

	void drawNoteIcon(ScPainter *p);
	bool createInfoGroup(QFrame *, QGridLayout *) override;
	void applicableActions(QStringList& actionList) override;
	QString infoDescription() const override;
	// Move incomplete lines from the previous frame if needed.
	bool moveLinesFromPreviousFrame ();
	void adjustParagraphEndings ();

private:
	bool cursorBiasBackward {false};
	// If the last paragraph had to be split, this is how many lines of the paragraph are in this frame.
	// Used for orphan/widow control
	int incompleteLines {0};
	// This holds the line splitting positions
	QList<int> incompletePositions;

	void setShadow();
	QString m_currentShadow;
	QMap<QString,StoryText> m_shadows;
	bool checkKeyIsShortcut(QKeyEvent *k);
	QRectF m_origAnnotPos;
	void updateBulletsNum();

private slots:
	void slotInvalidateLayout(int firstItem, int endItem);

public:
	//for footnotes/endnotes
	bool hasNoteMark(NotesStyle* NS = nullptr);
	bool hasNoteFrame(NotesStyle* NS, bool inChain = false);
	//bool hasNoteFrame(PageItem_NoteFrame* nF) { return m_notesFramesMap.contains(nF); }
	void delAllNoteFrames(bool doUpdate = false);
	void removeNoteFrame(PageItem_NoteFrame* nF) { m_notesFramesMap.remove(nF); }
	//layout notes frames /updates endnotes frames content if m_Doc->flag_updateEndNotes is set/
	void notesFramesLayout();
	//removing all marsk from text, returns number of removed marks
	int removeMarksFromText(bool doUndo);
	//return note frame for given notes style if current text frame has notes marks with this style
	PageItem_NoteFrame* itemNoteFrame(NotesStyle* nStyle);
	//list all notes frames for current text frame /with endnotes frames if endnotes marks are in that frame/
	QList<PageItem_NoteFrame*> notesFramesList() { return m_notesFramesMap.keys(); }
	//list of notes inserted by current text frame into given noteframe
	QList<TextNote*> notesList(PageItem_NoteFrame* nF) { return m_notesFramesMap.value(nF); }
	//insert note frame to list with empty notes list
	void setNoteFrame(PageItem_NoteFrame* nF);
	void invalidateNotesFrames();

private:
	NotesInFrameMap m_notesFramesMap;
	NotesInFrameMap updateNotesFrames(QMap<int, Mark*> noteMarksPosMap); //update notes frames content
	void updateNotesMarks(NotesInFrameMap notesMap);
	Mark* selectedMark(bool onlySelection = true);
	TextNote* selectedNoteMark(int& foundPos, bool onlySelection = true);
	TextNote* selectedNoteMark(bool onlySelection = true);
protected:
	// set text frame height to last line of text
	double maxY {0.0};
	void setMaxY(double y);

public:
	void setTextFrameHeight();
	void autoFitFrameHeight();

	// Suneer: caption frames. Ink bounds of the laid-out text (glyph outlines
	// of the first and last line, so Malayalam signs above and below count),
	// and a height fit that makes the gap under the last line's ink equal
	// to the gap above the first line's ink. See suneerFitCaptionHeight().
	struct SuneerInkMetrics
	{
		bool   valid { false };
		bool   fromInk { false };      // false: no visible glyph, font metrics used
		int    lines { 0 };
		double firstBaseline { 0.0 };
		double firstInkTop { 0.0 };    // frame-top -> top of first line's ink
		double firstFontAscent { 0.0 };
		double lastBaseline { 0.0 };
		double lastInkBottom { 0.0 };  // frame-top -> bottom of last line's ink
		double lastFontDescent { 0.0 };
	};
	SuneerInkMetrics suneerInkMetrics() const;
	bool suneerFitCaptionHeight(bool withUndo = true);

	/**
	 * @brief Fit the story inside this frame by adjusting typography alone.
	 *
	 * Shrinks, in order and only as far as it has to: font size, glyph
	 * scaling, tracking, word spacing. Never touches the frame's position or
	 * size, and never lets the text flow onward. Stops at the first stage that
	 * makes the text fit. Every reduction is bounded by the readability floors
	 * in the "autofit_text" preference context.
	 *
	 * @param withUndo record the style changes so one Ctrl+Z reverts them
	 * @return true if the text now fits
	 */
	bool autoFitTextToFrame(bool withUndo = true);
	//! \brief True if Auto Fit Text can run here: a text frame carrying text
	//! that nothing downstream is waiting for.
	bool autoFitTextEligible() const;
	//! \brief Undo every reduction Auto Fit has applied, returning the story to
	//! the typography the operator set.
	void autoFitTextRestore(bool withUndo = true);

	QString autoFitBaselineToString() const override;
	void autoFitBaselineFromString(const QString& packed) override;

private:
	/**
	 * @brief One style run of the story as the operator wrote it, before any
	 * Auto Fit reduction, snapped to the 1/10 pt and 1/10 % grid the file
	 * format stores.
	 *
	 * Captured once per fit. Every trial in the search is computed from these
	 * numbers rather than from the previous trial's output, so the rounding
	 * that keeps the values file-representable cannot accumulate across the
	 * seventy-odd passes a four-stage search makes.
	 */
	struct AutoFitRun
	{
		int    start {0};
		int    length {0};
		double fontSize {0.0};
		double scaleH {0.0};
		double scaleV {0.0};
		double tracking {0.0};
		double wordTracking {1.0};
	};
	QList<AutoFitRun> m_autoFitBaseline;

	//! \brief Story length the snapshot was taken at; a mismatch means the
	//! text was edited and the snapshot has to be retaken.
	int m_autoFitBaselineLength {-1};

	//! \brief Take a snapshot only if we do not already hold a valid one.
	void autoFitEnsureBaseline();
	//! \brief Recover the operator's own typography by dividing the recorded
	//! factors out of the current styles — the only place that division happens.
	void autoFitCaptureBaseline();
	//! \brief Lay the given factors over the captured baseline, per style run,
	//! and record them on the item.
	void applyAutoFitFactors(double fontScale, double glyphScale, double tracking, double wordScale, bool withUndo);
	//! \brief Apply the factors with undo off, recompose, and report whether
	//! the frame still overflows.
	bool autoFitTrialFits(double fontScale, double glyphScale, double tracking, double wordScale);
	//! \brief Smallest font size and glyph scale, and largest tracking, read off
	//! the captured baseline; these set how far the whole frame may be reduced.
	void autoFitBaselineExtremes(double& minFontSize, double& minScaleH, double& maxTracking, double& minWordTracking) const;

	/**
	 * @brief What the frame looked like when Auto Fit last ran on it.
	 *
	 * layout() re-queues a fit only when one of these has moved, so a fit that
	 * settles cannot re-trigger itself. The overflow flag is part of the stamp
	 * on purpose: a frame that could not be fitted within the readability
	 * floors records that it is still overflowing and is left alone, while a
	 * later style change that newly overflows a fitted frame does re-queue it.
	 */
	int    m_autoFitStampLength {-1};
	double m_autoFitStampWidth {-1.0};
	double m_autoFitStampHeight {-1.0};
	bool   m_autoFitStampOverflow {false};
	//! \brief Set while the fit's trial layouts run, so they never re-queue.
	bool   m_autoFitRunning {false};
	//! \brief Record the frame's state after a fit pass, fitted or not.
	void autoFitStamp(bool fitted);
};

#endif
