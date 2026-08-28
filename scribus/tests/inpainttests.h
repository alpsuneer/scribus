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
 * Unit tests for the inpainting kernel (Inpaint).
 *
 * The reconstruction tests are deliberately stated as properties of the
 * result - flat stays flat, a ramp stays a ramp, an edge stays an edge, a
 * texture stays as detailed as its surroundings - rather than as fixed pixel
 * values, because both methods are approximations and pinning exact numbers
 * would only pin this implementation's rounding.
 *
 * The quality tests lean on gradient energy rather than on PSNR. PSNR is
 * actively misleading here: blurring a hole *raises* it while making the
 * result worse, which is measurable in this very file - see
 * testBlurringScoresBetterOnPsnr(), which exists to stop anyone tuning
 * against that number again.
 */
class InpaintTests : public QObject
{
	Q_OBJECT
public:
	InpaintTests() {}

private slots:
	//! \name Fast marching (Telea) behaviour
	//@{
	void testSolidColourIsRestored();
	void testHorizontalGradientIsContinued();
	void testVerticalStripesStayBanded();
	//@}

	//! \name Contract, shared by both methods
	//@{
	void testEmptyMaskReturnsInputUnchanged();
	void testFullyMaskedImageDoesNotCrash();
	void testExemplarWithNoSourceDoesNotCrash();
	void testMaskTouchingBorder();
	void testExemplarMaskTouchingBorder();
	void testCancelReturnsNullImage();
	void testExemplarCancelReturnsNullImage();
	void testProgressIsMonotonicAndCompletes();
	void testExemplarProgressIsMonotonicAndCompletes();
	void testAlphaIsPreservedOnOpaqueFormat();
	void testRegionOfInterestKeepsLargeImagesFast();
	//@}

	//! \name Choosing a method
	//@{
	void testAutoUsesFastMarchingForThinMasks();
	void testAutoUsesFastMarchingForFlatSurroundings();
	void testAutoUsesExemplarForTexturedSurroundings();
	//@}

	//! \name Reconstruction quality
	//@{
	void testTextureEnergySurvivesInExemplarFill();
	void testSharpVerticalEdgeIsNotBlurredAway();
	void testDiagonalEdgeIsNotBlurredAway();
	void testColouredObjectsDoNotBleedIntoEachOther();
	void testRepeatedPatternIsContinued();
	void testTextLikeStructureDoesNotGoGrey();
	void testCirclesOnTextureKeepBackgroundClean();
	void testBlurringScoresBetterOnPsnr();
	void testCrowdLikeSceneKeepsItsTexture();
	//@}
};

#endif
