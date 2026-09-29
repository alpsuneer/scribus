/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "pageitem_placedpdf.h"

#include <QFileInfo>

#include "scribusdoc.h"
#include "undomanager.h"

PageItem_PlacedPDF::PageItem_PlacedPDF(ScribusDoc *pa, double x, double y, double w, double h, double w2, const QString& fill, const QString& outline)
	: PageItem_ImageFrame(pa, x, y, w, h, w2, fill, outline)
{
	setUPixmap(Um::IImageFrame);
	m_itemName = tr("Placed PDF") + QString::number(m_Doc->TotalItems);
	setUName(m_itemName);
}

QString PageItem_PlacedPDF::infoDescription() const
{
	QString htmlText;
	htmlText.append(tr("Placed PDF") + "<br/>");
	if (imageIsAvailable)
	{
		QFileInfo fi(Pfile);
		if (isInlineImage)
			htmlText.append(tr("Embedded PDF") + "<br/>");
		else
			htmlText.append(tr("File:") + " " + fi.fileName() + "<br/>");
		htmlText.append(tr("Page:") + " " + QString::number(pixm.imgInfo.actualPageNumber)
			+ " " + tr("of") + " " + QString::number(pixm.imgInfo.numberOfPages) + "<br/>");
	}
	else
		htmlText.append(tr("No PDF loaded") + "<br/>");
	return htmlText;
}
