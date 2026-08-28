/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef INPAINTTESTS_H
#define INPAINTTESTS_H

#include <QtTest/QtTest>

/**
 * Unit tests for Telea fast-marching inpainting (InpaintTelea).
 *
 * The reconstruction tests are deliberately stated as properties of the
 * result - flat stays flat, a ramp stays a ramp, stripes stay banded - rather
 * than as fixed pixel values, because the method is an approximation and
 * pinning exact numbers would only pin this implementation's rounding.
 */
class InpaintTests : public QObject
{
	Q_OBJECT
public:
	InpaintTests() {}

private slots:
	void testSolidColourIsRestored();
	void testHorizontalGradientIsContinued();
	void testVerticalStripesStayBanded();
	void testEmptyMaskReturnsInputUnchanged();
	void testFullyMaskedImageDoesNotCrash();
	void testMaskTouchingBorder();
	void testCancelReturnsNullImage();
	void testProgressIsMonotonicAndCompletes();
	void testAlphaIsPreservedOnOpaqueFormat();
	void testRegionOfInterestKeepsLargeImagesFast();
};

#endif
