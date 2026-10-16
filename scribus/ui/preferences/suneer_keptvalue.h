/*
 Faircode: no rounding drift when Preferences is closed with OK.

 A length is stored in points and shown in the document unit with a few
 decimals (20 pt = 7.056 mm). Converting the shown, rounded number back gave
 20.001 pt, so every OK changed values nobody had touched (an office PC,
 2026-10-15: 20 -> 20.001, 100 -> 100.001, 20 -> 20.0013).
 suneerKeptValue() returns the stored value while the box still shows what it
 was given, and the box's own value only after the user changed it.
*/
#ifndef SUNEER_KEPTVALUE_H
#define SUNEER_KEPTVALUE_H

#include <cmath>

#include <QDoubleSpinBox>

//! \a oldValue is the stored value (points for lengths), \a ratio what the box
//! multiplies it by (the unit ratio; 1 for unitless boxes).
inline double suneerKeptValue(const QDoubleSpinBox* box, double oldValue, double ratio = 1.0)
{
	if (ratio == 0.0)
		ratio = 1.0;
	const double scale = std::pow(10.0, box->decimals());
	const double shownWhenLoaded = qBound(box->minimum(), oldValue * ratio, box->maximum());
	if (std::llround(box->value() * scale) == std::llround(shownWhenLoaded * scale))
		return oldValue;
	return box->value() / ratio;
}

#endif
