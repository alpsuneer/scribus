/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef IMPOSITIONPREVIEWWIDGET_H
#define IMPOSITIONPREVIEWWIDGET_H

#include "scribusapi.h"
#include "scimpositionengine.h"

#include <QRectF>
#include <QWidget>

/**
 * Schematic (geometry-only) preview of the imposed plate: plate outline,
 * margin area, left/right page rectangles with filename labels, gutter gap,
 * and mark symbols (regmarks, colour bar, slug line, auto/trim marks) drawn
 * as simple shapes -- never the actual PDF content. This has no PDF/font
 * rendering dependency of its own: page sizes come from
 * ImpositionSettings::leftPageWidthMm/HeightMm etc. (populated by the dialog
 * via ScImpositionEngine::scanPdfInfo(), a lightweight `pdfinfo` query), and
 * everything drawn here is vector shapes and QPainter text.
 */
class SCRIBUS_API ImpositionPreviewWidget : public QWidget
{
	Q_OBJECT

public:
	explicit ImpositionPreviewWidget(QWidget* parent = nullptr);

	//! Recomputes and repaints from the given settings.
	void setSettings(const ImpositionSettings& settings);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QRectF sheetRectInWidget() const;
	QRectF mmRectToWidget(const QRectF& sheetRect, double xMm, double yMm, double wMm, double hMm) const;

	ImpositionSettings m_settings;
};

#endif
