/*
 For general Scribus (>=1.3.2) copyright and licensing information please refer
 to the COPYING file provided with the program. Following this notice may exist
 a copyright and/or license notice that predates the release of Scribus 1.3.2
 for which a new license (GPL+exception) is in place.
 */
/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#undef NDEBUG

#include <cassert>
#include <utility>

#include "../styles/charstyle.h"
#include "prefsstructs.h"
#include "../styles/paragraphstyle.h"
#include "specialchars.h"
#include "storytext.h"
#include "textlayout.h"
#include "textlayoutpainter.h"
#include "screenpainter.h"
#include "boxes.h"
#include "itextcontext.h"

TextLayout::TextLayout(StoryText* text, ITextContext* frame)
          : m_story(text),
            m_frame(frame)
{
	m_box = new GroupBox(Box::D_Horizontal);
}

TextLayout::~TextLayout()
{
	delete m_box;
}

uint TextLayout::lines() const
{
	uint count = 0;
	for (auto box : std::as_const(m_box->boxes()))
	{
		count += box->boxes().count();
	}
	return count;
}

const LineBox* TextLayout::line(uint i) const
{
	uint count = 0;
	for (const Box *box : m_box->boxes())
	{
		if (i < count + box->boxes().count())
			return dynamic_cast<const LineBox*>(box->boxes()[i - count]);
		count += box->boxes().count();
	}
	assert(false);
	return nullptr;
}

const Box* TextLayout::box() const
{
	return m_box;
}

Box* TextLayout::box()
{
	return m_box;
}

void TextLayout::appendLine(LineBox* ls)
{
	assert(ls);
	assert(ls->firstChar() >= 0);
	assert(ls->firstChar() < story()->length());
	assert(ls->lastChar() < story()->length());
	assert(!m_box->boxes().empty());

	GroupBox* column = dynamic_cast<GroupBox*>(m_box->boxes().constLast());
	assert(column);
	if (ls->type() == Box::T_PathLine)
		ls->setAscent(ls->y() - column->naturalHeight());

	ls->setWidth(column->width());
	column->addBox(ls);
}

// Remove the last line from the list. Used when we need to backtrack on the layouting.
void TextLayout::removeLastLine ()
{
	const QList<Box*>& boxes = m_box->boxes();
	if (boxes.isEmpty())
		return;

	int columnIndex = boxes.size() - 1;
	while (columnIndex >= 0)
	{
		GroupBox* column = dynamic_cast<GroupBox*>(boxes[columnIndex]);
		assert(column);

		int lineCount = column->boxes().count();
		if (lineCount > 0)
		{
			column->removeBox(lineCount - 1);
			break;
		}
		--columnIndex;
	}
}

void TextLayout::render(ScreenPainter *p, ITextContext *ctx) const
{
	p->save();
	m_box->render(p, ctx);
	p->restore();
}

void TextLayout::renderBackground(TextLayoutPainter *p) const
{
	QString backColor;
	QString lastColor;
	double backShade;
	double lastShade = 100.0;
	QRectF lastRect;

	p->save();
	p->translate(m_box->x(), m_box->y());

	for (const Box* column : m_box->boxes())
	{
		const QList<const Box*>& lineBoxes = column->boxes();
		QRectF colBBox = column->bbox();

		for (int j = 0; j < lineBoxes.count(); ++j)
		{
			const Box* box = lineBoxes.at(j);
			
			const ParagraphStyle& style = m_story->paragraphStyle(box->firstChar());
			backColor = style.backgroundColor();
			backShade = style.backgroundShade();

			if ((lastColor != backColor) || (lastShade != backShade))
			{
				if (!lastRect.isEmpty())
				{
					TextLayoutColor bkColor(lastColor, lastShade);
					p->save();
					p->setFillColor(bkColor);
					p->setStrokeColor(bkColor);
					p->drawRect(lastRect);
					p->restore();
					lastRect = QRectF();
				}
			}

			if (backColor != CommonStrings::None)
			{
				double bgX = colBBox.x();
				double bgW = colBBox.width();

				if (style.spanColumns() != 0)
				{
					bgX = 0.0;
					bgW = m_box->width();
				}

				QRectF rect(bgX, box->y(), bgW, box->height());
				lastRect |= rect;
			}

			lastColor = backColor;
			lastShade = backShade;
		}

		if (!lastRect.isEmpty())
		{
			TextLayoutColor bkColor(lastColor, lastShade);
			p->save();
			p->setFillColor(bkColor);
			p->setStrokeColor(bkColor);
			p->drawRect(lastRect);
			p->restore();
		}

		lastColor.clear();
		lastShade = 100;
		lastRect = QRectF();
	}

	p->restore();
}

namespace
{
	/** Everything needed to paint one paragraph rule, with the "(Text Color)"
	    sentinel already resolved against the paragraph's first character. */
	struct ResolvedRule
	{
		double weight { 1.0 };
		TextLayoutColor color;
		TextLayoutColor gapColor;
		bool hasGapColor { false };
		bool overprint { false };
		bool gapOverprint { false };
		ParagraphStyle::RuleType type { ParagraphStyle::RuleSolid };
		ParagraphStyle::RuleWidthType widthType { ParagraphStyle::RuleWidthColumn };
		double offset { 0.0 };
		double leftIndent { 0.0 };
		double rightIndent { 0.0 };
		bool keepInFrame { true };
	};

	void fillRuleBand(TextLayoutPainter* p, const QRectF& band, const TextLayoutColor& color, bool overprint)
	{
		if (band.width() <= 0.0 || band.height() <= 0.0)
			return;
		if (color.color.isEmpty() || color.color == CommonStrings::None)
			return;
		p->save();
		p->setOverprint(overprint);
		p->setFillColor(color);
		p->setStrokeColor(color);
		p->drawRect(band);
		p->restore();
	}

	/** Paints a dashed or dotted band as a run of solid segments. Doing the dash
	    geometry here rather than in the painters keeps every output backend
	    (screen, PDF, PS, XPS, SVG) pixel-identical, none of them has to know
	    about dash patterns. */
	void fillDashedBand(TextLayoutPainter* p, const QRectF& band, const TextLayoutColor& color, bool overprint, double dashLen, double gapLen)
	{
		if (dashLen <= 0.0 || gapLen <= 0.0)
		{
			fillRuleBand(p, band, color, overprint);
			return;
		}
		const double right = band.x() + band.width();
		for (double x = band.x(); x < right; x += dashLen + gapLen)
			fillRuleBand(p, QRectF(x, band.y(), qMin(dashLen, right - x), band.height()), color, overprint);
	}

	/** Paints one rule, occupying [yTop, yTop + weight] over [x, x + width]. */
	void paintRule(TextLayoutPainter* p, const ResolvedRule& rule, double x, double width, double yTop)
	{
		const QRectF full(x, yTop, width, rule.weight);

		switch (rule.type)
		{
		case ParagraphStyle::RuleDashed:
		case ParagraphStyle::RuleDotted:
			if (rule.hasGapColor)
				fillRuleBand(p, full, rule.gapColor, rule.gapOverprint);
			if (rule.type == ParagraphStyle::RuleDotted)
				fillDashedBand(p, full, rule.color, rule.overprint, rule.weight, rule.weight * 2.0);
			else
				fillDashedBand(p, full, rule.color, rule.overprint, rule.weight * 4.0, rule.weight * 2.0);
			break;
		case ParagraphStyle::RuleDouble:
		{
			const double part = rule.weight / 3.0;
			if (rule.hasGapColor)
				fillRuleBand(p, QRectF(x, yTop + part, width, part), rule.gapColor, rule.gapOverprint);
			fillRuleBand(p, QRectF(x, yTop, width, part), rule.color, rule.overprint);
			fillRuleBand(p, QRectF(x, yTop + 2.0 * part, width, part), rule.color, rule.overprint);
			break;
		}
		case ParagraphStyle::RuleThickThin:
		case ParagraphStyle::RuleThinThick:
		{
			const double thick = rule.weight * 0.5;
			const double thin  = rule.weight * 0.25;
			const double gap   = rule.weight * 0.25;
			const double first  = (rule.type == ParagraphStyle::RuleThickThin) ? thick : thin;
			const double second = (rule.type == ParagraphStyle::RuleThickThin) ? thin : thick;
			if (rule.hasGapColor)
				fillRuleBand(p, QRectF(x, yTop + first, width, gap), rule.gapColor, rule.gapOverprint);
			fillRuleBand(p, QRectF(x, yTop, width, first), rule.color, rule.overprint);
			fillRuleBand(p, QRectF(x, yTop + first + gap, width, second), rule.color, rule.overprint);
			break;
		}
		case ParagraphStyle::RuleSolid:
		default:
			fillRuleBand(p, full, rule.color, rule.overprint);
			break;
		}
	}
}

/**
 Draws the paragraph rules (rule above / rule below) of every paragraph that
 begins resp. ends inside this frame. Called by every output backend right
 after renderBackground(), so canvas, print, PDF, PS, XPS and SVG all get the
 same geometry from the same code.
 */
void TextLayout::renderParagraphRules(TextLayoutPainter *p) const
{
	if (!m_story || !m_box || m_box->boxes().isEmpty())
		return;

	const int storyLength = m_story->length();
	if (storyLength <= 0)
		return;

	// The painter is translated by the layout box origin below, so the frame
	// edges live at these coordinates in the drawing space.
	const double frameTop = -m_box->y();
	const double frameBottom = frameTop + (m_frame ? m_frame->height() : 0.0);

	p->save();
	p->translate(m_box->x(), m_box->y());

	for (const Box* column : m_box->boxes())
	{
		const QRectF colBBox = column->bbox();

		for (const Box* box : column->boxes())
		{
			// Text on a path has no column to hang a rule on.
			if (box->type() == Box::T_PathLine)
				continue;

			const int firstChar = box->firstChar();
			const int lastChar = box->lastChar();
			if (firstChar > lastChar || firstChar >= storyLength)
				continue;

			const ParagraphStyle& style = m_story->paragraphStyle(firstChar);
			if (!style.ruleAboveOn() && !style.ruleBelowOn())
				continue;

			// A paragraph can be split over several columns or over several
			// linked frames. The rule above belongs to its very first line and
			// the rule below to its very last one, so a paragraph that merely
			// passes through this frame or column gets no rule at all (R8).
			const bool isParaFirstLine = (firstChar == 0) || (m_story->text(firstChar - 1) == SpecialChars::PARSEP);
			const bool isParaLastLine = (lastChar >= storyLength - 1) || (m_story->text(lastChar) == SpecialChars::PARSEP);
			if (!isParaFirstLine && !isParaLastLine)
				continue;

			// An empty paragraph has no text extent, so a text-width rule has
			// nothing to span; a column-width rule is still drawn.
			const bool isEmptyParagraph = (firstChar == lastChar) && (m_story->text(firstChar) == SpecialChars::PARSEP);

			for (int side = 0; side < 2; ++side)
			{
				const bool above = (side == 0);
				if (above ? (!style.ruleAboveOn() || !isParaFirstLine) : (!style.ruleBelowOn() || !isParaLastLine))
					continue;

				ResolvedRule rule;
				rule.weight       = above ? style.ruleAboveWeight() : style.ruleBelowWeight();
				rule.overprint    = above ? style.ruleAboveOverprint() : style.ruleBelowOverprint();
				rule.gapOverprint = above ? style.ruleAboveGapOverprint() : style.ruleBelowGapOverprint();
				rule.type         = above ? style.ruleAboveType() : style.ruleBelowType();
				rule.widthType    = above ? style.ruleAboveWidthType() : style.ruleBelowWidthType();
				rule.offset       = above ? style.ruleAboveOffset() : style.ruleBelowOffset();
				rule.leftIndent   = above ? style.ruleAboveLeftIndent() : style.ruleBelowLeftIndent();
				rule.rightIndent  = above ? style.ruleAboveRightIndent() : style.ruleBelowRightIndent();
				rule.keepInFrame  = above ? style.ruleAboveKeepInFrame() : style.ruleBelowKeepInFrame();

				if (rule.weight <= 0.0)
					continue;

				// "(Text Color)" means the fill colour of the paragraph's first
				// character, resolved at paint time so it follows the text.
				QString colorName = above ? style.ruleAboveColor() : style.ruleBelowColor();
				QString gapColorName = above ? style.ruleAboveGapColor() : style.ruleBelowGapColor();
				if (colorName == ParagraphStyle::RuleTextColor)
					colorName = m_story->layoutCharStyle(firstChar).fillColor();
				if (gapColorName == ParagraphStyle::RuleTextColor)
					gapColorName = m_story->layoutCharStyle(firstChar).fillColor();
				rule.color = TextLayoutColor(colorName, above ? style.ruleAboveTint() : style.ruleBelowTint());
				rule.gapColor = TextLayoutColor(gapColorName, above ? style.ruleAboveGapTint() : style.ruleBelowGapTint());
				rule.hasGapColor = !gapColorName.isEmpty() && gapColorName != CommonStrings::None;

				if (rule.color.color.isEmpty() || rule.color.color == CommonStrings::None)
					continue;

				double ruleX = 0.0;
				double ruleWidth = 0.0;
				if (rule.widthType == ParagraphStyle::RuleWidthText)
				{
					if (isEmptyParagraph)
						continue;
					ruleX = colBBox.x() + box->x();
					ruleWidth = box->naturalWidth();
				}
				else if (style.spanColumns() != 0)
				{
					ruleX = 0.0;
					ruleWidth = m_box->width();
				}
				else
				{
					ruleX = colBBox.x();
					ruleWidth = colBBox.width();
				}

				ruleX += rule.leftIndent;
				ruleWidth -= (rule.leftIndent + rule.rightIndent);
				if (ruleWidth <= 0.0)
					continue;

				// A positive offset pushes the rule away from the paragraph:
				// upwards above it, downwards below it (InDesign convention).
				double yTop = above ? (box->y() - rule.offset - rule.weight)
				                    : (box->y() + box->height() + rule.offset);

				// Keep In Frame pulls a rule that would fall outside the frame
				// back against the nearest edge rather than letting it draw
				// outside or be cut in half.
				if (rule.keepInFrame)
				{
					if ((frameBottom - frameTop) < rule.weight)
						continue; // frame too short to hold the rule at all
					yTop = qBound(frameTop, yTop, frameBottom - rule.weight);
				}

				paintRule(p, rule, ruleX, ruleWidth, yTop);
			}
		}
	}

	p->restore();
}

void TextLayout::render(TextLayoutPainter *p) const
{
	p->save();
	m_box->render(p);
	p->restore();
}

void TextLayout::addColumn(double colLeft, double colWidth)
{
	GroupBox *newBox = new GroupBox(Box::D_Vertical);
	newBox->moveTo(colLeft, 0.0);
	newBox->setWidth(colWidth);
	newBox->setAscent(m_frame->height());
	// Prevent duplicate columns
	for (const Box* b : m_box->boxes()) {
		if (qAbs(b->x() - colLeft) < 0.1) { delete newBox; return; }
	}
	m_box->boxes().append(newBox); // suppress update() to prevent layout loop

	// Update the box width and height, any better place to do this?
	m_box->setAscent(m_frame->height());
	m_box->setWidth(m_frame->width());
}

void TextLayout::clear() 
{
	delete m_box;
	m_box = new GroupBox(Box::D_Horizontal);
}

void TextLayout::setStory(StoryText *story)
{
	m_story = story;
	clear();
}

int TextLayout::startOfLine(int pos) const
{
	for (uint i = 0; i < lines(); ++i)
	{
		const LineBox* ls = line(i);
		if (ls->firstChar() <= pos && pos <= ls->lastChar())
			return ls->firstChar();
	}
	return 0;
}

int TextLayout::endOfLine(int pos) const
{
	for (uint i = 0; i < lines(); ++i)
	{
		const LineBox* ls = line(i);
		if (ls->containsPos(pos))
			return story()->text(ls->lastChar()) == SpecialChars::PARSEP ? ls->lastChar() :
				story()->text(ls->lastChar()) == ' ' ? ls->lastChar() : ls->lastChar() + 1;
	}
	return story()->length();
}

int TextLayout::prevLine(int pos) const
{
	bool isRTL = (m_story->paragraphStyle().direction() == ParagraphStyle::RTL);
	for (uint i = 0; i < lines(); ++i)
	{
		// find line for pos
		const LineBox* ls = line(i);
		if (ls->containsPos(pos))
		{
			if (i == 0)
				return startOfLine(pos);
			// find current xpos
			qreal xpos = ls->positionToPoint(pos, *m_story).x1();
			if (pos != m_lastMagicPos || xpos > m_magicX)
				m_magicX = xpos;

			const LineBox* ls2 = line(i-1);
			// find new cpos
			for (int j = ls2->firstChar(); j <= ls2->lastChar(); ++j)
			{
				xpos = ls2->positionToPoint(j, *m_story).x1();
				if ((isRTL && xpos <= m_magicX) || (!isRTL && xpos >= m_magicX))
				{
					m_lastMagicPos = j;
					return j;
				}
			}
			m_lastMagicPos = ls2->lastChar();
			return ls2->lastChar();
		}
	}
	return m_box->firstChar();
}

int TextLayout::nextLine(int pos) const
{
	bool isRTL = (m_story->paragraphStyle().direction() == ParagraphStyle::RTL);
	for (uint i = 0; i < lines(); ++i)
	{
		// find line for pos
		const LineBox* ls = line(i);
		if (ls->containsPos(pos))
		{
			if (i+1 == lines())
				return endOfLine(pos);
			// find current xpos
			qreal xpos = ls->positionToPoint(pos, *m_story).x1();

			if (pos != m_lastMagicPos || xpos > m_magicX)
				m_magicX = xpos;

			const LineBox* ls2 = line(i+1);
			// find new cpos
			for (int j = ls2->firstChar(); j <= ls2->lastChar(); ++j)
			{
				xpos = ls2->positionToPoint(j, *m_story).x1();
				if ((isRTL && xpos <= m_magicX) || (!isRTL && xpos >= m_magicX))
				{
					m_lastMagicPos = j;
					return j;
				}
			}
			m_lastMagicPos = ls2->lastChar() + 1;
			return ls2->lastChar() + 1;
		}
	}
	return m_box->lastChar();
}

int TextLayout::startOfFrame() const
{
	if (m_box->isEmpty())
		return 0;
	const QList<Box*>& boxes = m_box->boxes();

	const GroupBox* column = dynamic_cast<const GroupBox*>(boxes.first());
	assert(column);

	// Beware of columns hidden by other objects
	if (column->boxCount() > 0)
		return column->firstChar();
	
	int columnCount = boxes.count();
	for (int i = 1; i < columnCount; ++i)
	{
		column = dynamic_cast<const GroupBox*>(boxes.at(i));
		assert(column);

		if (!column->isEmpty())
			return column->firstChar();
	}

	return 0;
}

int TextLayout::endOfFrame() const
{
	if (m_box->isEmpty())
		return 0;
	const QList<Box*>& boxes = m_box->boxes();

	// Beware of columns hidden by other objects
	const GroupBox* column = nullptr;
	int columnIndex = boxes.count() - 1;
	do
	{
		column = dynamic_cast<const GroupBox*>(boxes.at(columnIndex));
		assert(column);

		if (!column->isEmpty())
			return column->lastChar() + 1;
		--columnIndex;
	}
	while (columnIndex >= 0);

	return 0;
}

int TextLayout::pointToPosition(const QPointF& coord) const
{
	int position = m_box->pointToPosition(coord, *m_story);
	return position;
}

QLineF TextLayout::positionToPoint(int pos) const
{
	QLineF result;
	bool isRTL = (m_story->paragraphStyle().direction() == ParagraphStyle::RTL);

	result = m_box->positionToPoint(pos, *m_story);
	if (!result.isNull())
		return result;

	qreal x;
	qreal y1;
	qreal y2;
	if (lines() > 0)
	{
		// TODO: move this branch to GroupBox::positionToPoint()
		// last glyph box in last line
		Box* column = m_box->boxes().constLast();
		if (!column->boxes().isEmpty())
		{
			const Box* line = column->boxes().constLast();
			const Box* glyph = line->boxes().empty() ? nullptr : line->boxes().last();
			QChar ch = story()->text(line->lastChar());
			if (ch == SpecialChars::PARSEP || ch == SpecialChars::LINEBREAK)
			{
				// last character is a newline, draw the cursor on the next line.
				if (isRTL)
					x = line->width();
				else
					x = 1;
				y1 = line->y() + line->height();
				y2 = y1 + line->height();
			}
			else
			{
				// draw the cursor at the end of last line.
				if (isRTL || glyph == nullptr)
					x = line->x();
				else
					x = line->x() + glyph->x() + glyph->width();
				y1 = line->y();
				y2 = y1 + line->height();
			}
		}
		else
		{
			const ParagraphStyle& pstyle(story()->paragraphStyle(qMin(pos, story()->length())));
			if (isRTL)
				x = column->width();
			else
				x = 1;
			y1 = 0;
			y2 = pstyle.lineSpacing();
		}
		result.setLine(x, y1, x, y2);
		result.translate(column->x(), column->y());
	}
	else
	{
		// rather the trailing style than a segfault.
		const ParagraphStyle& pstyle(story()->paragraphStyle(qMin(pos, story()->length())));
		if (isRTL)
			x = m_box->width();
		else
			x = 1;
		y1 = 0;
		y2 = pstyle.lineSpacing();
		result.setLine(x, y1, x, y2);
	}
	result.translate(m_box->x(), m_box->y());

	return result;
}
