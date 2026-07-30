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


//Syntax: ATTRDEF( datatype, gettername, name, defaultvalue)

ATTRDEF(ParagraphStyle::LineSpacingMode, lineSpacingMode, LineSpacingMode, ParagraphStyle::FixedLineSpacing)
ATTRDEF(double, lineSpacing, LineSpacing, 0.0)
ATTRDEF(ParagraphStyle::AlignmentType, alignment, Alignment, LeftAligned)
ATTRDEF(ParagraphStyle::DirectionType, direction, Direction, LTR)
ATTRDEF(int, opticalMargins, OpticalMargins, 0)
ATTRDEF(int, hyphenationMode, HyphenationMode, 2)
ATTRDEF(double, minWordTracking, MinWordTracking, 1.0)
//ATTRDEF(double, maxWordTracking, MaxWordTracking, 1.0)
ATTRDEF(double, minGlyphExtension, MinGlyphExtension, 1.0)
ATTRDEF(double, maxGlyphExtension, MaxGlyphExtension, 1.0)
ATTRDEF(double, leftMargin, LeftMargin, 0.0)
ATTRDEF(double, rightMargin, RightMargin, 0.0)
ATTRDEF(double, firstIndent, FirstIndent, 0.0)
ATTRDEF(double, gapBefore, GapBefore, 0.0)
ATTRDEF(double, gapAfter, GapAfter, 0.0)
ATTRDEF(QList<TabRecord>, tabValues, TabValues, QList<TabRecord>())
ATTRDEF(bool, useBaselineGrid, UseBaselineGrid, false)
ATTRDEF(int, keepLinesStart, KeepLinesStart, 0)
ATTRDEF(int, keepLinesEnd, KeepLinesEnd, 0)
ATTRDEF(bool, keepWithNext, KeepWithNext, false)
ATTRDEF(bool, keepTogether, KeepTogether, false)
ATTRDEF(bool, hasDropCap, HasDropCap, false)
ATTRDEF(int, dropCapLines, DropCapLines, 2)
ATTRDEF(double, parEffectOffset, ParEffectOffset, 0.0)
ATTRDEF(bool, parEffectIndent, ParEffectIndent, false)
ATTRDEF(QString, peCharStyleName, PeCharStyleName,"")
ATTRDEF(bool, hasBullet, HasBullet, false)
ATTRDEF(QString, bulletStr, BulletStr, QString(QChar(0x2022)))
// Image bullets: a raster file drawn instead of the bullet character(s).
// Size is in points; 0.0 means automatic (0.8 x the bullet char style size).
ATTRDEF(bool, bulletUseImage, BulletUseImage, false)
ATTRDEF(QString, bulletImagePath, BulletImagePath, "")
ATTRDEF(double, bulletImageSize, BulletImageSize, 0.0)
// Vertical offset of the image bullet in points; positive lowers the image.
ATTRDEF(double, bulletImageOffset, BulletImageOffset, 0.0)
// Sizing mode: 0 = from bulletImageSize (0 = auto 0.8em, >0 = fixed points),
// 1 = bulletImageScale percent of the automatic size. Mode 0 keeps old files
// behaving exactly as before this attribute existed.
ATTRDEF(int, bulletImageScaleMode, BulletImageScaleMode, 0)
ATTRDEF(double, bulletImageScale, BulletImageScale, 100.0)
ATTRDEF(bool, hasNum, HasNum, false)
ATTRDEF(QString, numName, NumName, "")
ATTRDEF(int, numFormat, NumFormat, 0)
ATTRDEF(QString, numPrefix, NumPrefix, "")
ATTRDEF(QString, numSuffix, NumSuffix, ".")
ATTRDEF(int, numLevel, NumLevel, 0)
ATTRDEF(int, numStart, NumStart, 1)
ATTRDEF(int, numRestart, NumRestart, 0)
ATTRDEF(bool, numOther, NumOther, false)
ATTRDEF(bool, numHigher, NumHigher, true)
ATTRDEF(QString, backgroundColor, BackgroundColor, "None")
ATTRDEF(double, backgroundShade, BackgroundShade, 100)
ATTRDEF(int, hyphenConsecutiveLines, HyphenConsecutiveLines, 2)
ATTRDEF(QString, opticalMarginSetId, OpticalMarginSetId, OpticalMarginLookup::instance().defaultSetId())
ATTRDEF(int, spanColumns, SpanColumns, 0)
ATTRDEF(QString, nextStyle, NextStyle, "")
// Paragraph rules (InDesign-style "Rule Above" / "Rule Below").
// Offsets and indents are stored in points like every other paragraph metric.
ATTRDEF(bool, ruleAboveOn, RuleAboveOn, false)
ATTRDEF(double, ruleAboveWeight, RuleAboveWeight, 1.0)
ATTRDEF(QString, ruleAboveColor, RuleAboveColor, "Black")
ATTRDEF(bool, ruleAboveOverprint, RuleAboveOverprint, false)
ATTRDEF(QString, ruleAboveGapColor, RuleAboveGapColor, "None")
ATTRDEF(bool, ruleAboveGapOverprint, RuleAboveGapOverprint, false)
ATTRDEF(ParagraphStyle::RuleType, ruleAboveType, RuleAboveType, ParagraphStyle::RuleSolid)
ATTRDEF(int, ruleAboveTint, RuleAboveTint, 100)
ATTRDEF(int, ruleAboveGapTint, RuleAboveGapTint, 100)
ATTRDEF(ParagraphStyle::RuleWidthType, ruleAboveWidthType, RuleAboveWidthType, ParagraphStyle::RuleWidthColumn)
ATTRDEF(double, ruleAboveOffset, RuleAboveOffset, 0.0)
ATTRDEF(double, ruleAboveLeftIndent, RuleAboveLeftIndent, 0.0)
ATTRDEF(double, ruleAboveRightIndent, RuleAboveRightIndent, 0.0)
ATTRDEF(bool, ruleAboveKeepInFrame, RuleAboveKeepInFrame, true)
ATTRDEF(bool, ruleBelowOn, RuleBelowOn, false)
ATTRDEF(double, ruleBelowWeight, RuleBelowWeight, 1.0)
ATTRDEF(QString, ruleBelowColor, RuleBelowColor, "Black")
ATTRDEF(bool, ruleBelowOverprint, RuleBelowOverprint, false)
ATTRDEF(QString, ruleBelowGapColor, RuleBelowGapColor, "None")
ATTRDEF(bool, ruleBelowGapOverprint, RuleBelowGapOverprint, false)
ATTRDEF(ParagraphStyle::RuleType, ruleBelowType, RuleBelowType, ParagraphStyle::RuleSolid)
ATTRDEF(int, ruleBelowTint, RuleBelowTint, 100)
ATTRDEF(int, ruleBelowGapTint, RuleBelowGapTint, 100)
ATTRDEF(ParagraphStyle::RuleWidthType, ruleBelowWidthType, RuleBelowWidthType, ParagraphStyle::RuleWidthColumn)
ATTRDEF(double, ruleBelowOffset, RuleBelowOffset, 0.0)
ATTRDEF(double, ruleBelowLeftIndent, RuleBelowLeftIndent, 0.0)
ATTRDEF(double, ruleBelowRightIndent, RuleBelowRightIndent, 0.0)
ATTRDEF(bool, ruleBelowKeepInFrame, RuleBelowKeepInFrame, true)
// InDesign-style nested styles: an ordered list of character-style rules that
// is resolved at layout time. Encoded as a single string so it rides the normal
// attribute machinery; see ParagraphStyle::parseNestedStyles().
ATTRDEF(QString, nestedStyles, NestedStyles, "")
