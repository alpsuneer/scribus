/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef PROOF_FIT_H
#define PROOF_FIT_H

#include <QtGlobal>

/*!
 \brief How a proof page is reduced onto one sheet ("Reduce to fit paper").

 One place for the arithmetic, so the Proof Print dialog's "Scale: N%" and the
 PostScript PSLib writes cannot disagree. All sizes are in points.
 */
struct ProofFit
{
	double scale { 1.0 };    //!< never above 1.0: a small page is not enlarged
	bool   rotated { false }; //!< page turned 90 degrees on the sheet
};

//! Clear space kept on every side of the sheet, for the printer's unprintable edge.
constexpr double ProofFitMargin = 12.0;

/*!
 Fit a \a pageW x \a pageH page onto a \a paperW x \a paperH sheet, keeping its
 proportions. Tries the page upright and turned 90 degrees and keeps whichever
 is larger (upright on a tie). A page that already fits prints at 100%.
 */
inline ProofFit proofFitOnSheet(double pageW, double pageH, double paperW, double paperH)
{
	ProofFit fit;
	if (pageW <= 0.0 || pageH <= 0.0 || paperW <= 0.0 || paperH <= 0.0)
		return fit;
	const double availW = qMax(1.0, paperW - ProofFitMargin * 2.0);
	const double availH = qMax(1.0, paperH - ProofFitMargin * 2.0);
	const double upright = qMin(availW / pageW, availH / pageH);
	const double turned  = qMin(availW / pageH, availH / pageW);
	// A page that already fits upright stays upright at 100%: turning it
	// would gain nothing, as it is never enlarged.
	fit.rotated = (upright < 1.0) && (turned > upright);
	fit.scale = qMin(1.0, fit.rotated ? turned : upright);
	return fit;
}

#endif // PROOF_FIT_H
