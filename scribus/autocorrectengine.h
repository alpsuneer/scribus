/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOCORRECTENGINE_H
#define AUTOCORRECTENGINE_H

#include <QImage>

#include "scribusapi.h"
#include "scimagestructs.h"

struct SCRIBUS_API AutoCorrectOptions
{
	enum Algorithm
	{
		MonochromaticContrast = 0,   //!< stretch composite luminance
		PerChannelContrast    = 1,   //!< stretch R,G,B independently
		FindDarkAndLightColors = 2,  //!< smart, default
		EnhanceBrightnessContrast = 3 //!< gentle
	};
	Algorithm algorithm { FindDarkAndLightColors };
	bool snapNeutralMidtones { true };
	bool protectSkinTones { true };
	double shadowClip { 0.1 };
	double highlightClip { 0.1 };
	int midtoneTarget { 128 };
};

struct SCRIBUS_API CmykOptimizeOptions
{
	int totalInkLimit { 300 };   //!< percent (four channels sum, max 400)
	bool applyUCR { true };      //!< Under Color Removal
	double ucrAmount { 0.5 };    //!< 0.0–1.0
};

/*!
 \brief Pure-algorithmic automatic tone / colour correction. No ML, no external
        dependencies — Qt/C++ only. Every method is QImage in, QImage out.
 */
class SCRIBUS_API AutoCorrectEngine
{
public:
	static QImage autoTone(const QImage& in);
	static QImage autoContrast(const QImage& in);
	static QImage autoColor(const QImage& in);
	static QImage autoEnhanceCombined(const QImage& in, const AutoCorrectOptions& opts);
	static QImage autoCmykOptimize(const QImage& in, const CmykOptimizeOptions& opts);

	// --- Effect builders (for the non-destructive stack) --------------------
	static ImageEffect makeAutoTone();
	static ImageEffect makeAutoContrast();
	static ImageEffect makeAutoColor();
	static ImageEffect makeAutoEnhance(const AutoCorrectOptions& opts);
	static ImageEffect makeAutoCmyk(const CmykOptimizeOptions& opts);

	// Parse the parameter strings produced by the builders above.
	static AutoCorrectOptions parseEnhance(const QString& params);
	static CmykOptimizeOptions parseCmyk(const QString& params);
};

#endif // AUTOCORRECTENGINE_H
