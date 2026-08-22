/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef ELLIPSEMARQUEETOOL_H
#define ELLIPSEMARQUEETOOL_H

#include "scribusapi.h"
#include "ui/tools/rectmarqueetool.h"

/*!
 \brief Elliptical marquee — same drag interaction as RectMarqueeTool but the
        rubber band and committed selection are elliptical.
 */
class SCRIBUS_API EllipseMarqueeTool : public RectMarqueeTool
{
	Q_OBJECT

public:
	using RectMarqueeTool::RectMarqueeTool;

	QString name() const override { return tr("Elliptical Marquee"); }

	QCursor cursor() const override;

protected:
	void createRubber(const QRectF& r) override;
	void setRubber(const QRectF& r) override;
	void commit(const QRect& r, ScImageSelection::Mode mode) override;
};

#endif // ELLIPSEMARQUEETOOL_H
