/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "ui/tools/imagetool.h"

ScImageSelection::Mode ImageTool::modeFromModifiers(Qt::KeyboardModifiers mods) const
{
	const bool shift = mods.testFlag(Qt::ShiftModifier);
	const bool alt = mods.testFlag(Qt::AltModifier);
	if (shift && alt)
		return ScImageSelection::Intersect;
	if (shift)
		return ScImageSelection::Add;
	if (alt)
		return ScImageSelection::Subtract;
	return ScImageSelection::Replace;
}
