/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "impositionpreviewwidget.h"

#include <QDate>
#include <QFileInfo>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace {
	const int PreviewPadding = 12;
	const double CropOffsetMm = 3.0;
	const double CropLengthMm = 5.0;

	QPointF mmToWidgetPoint(const QRectF& sheetRect, double sheetWidthMm, double xMm, double yMm)
	{
		double scale = sheetRect.width() / sheetWidthMm;
		return QPointF(sheetRect.x() + xMm * scale, sheetRect.y() + yMm * scale);
	}

	void drawCropMarkPair(QPainter& painter, const QRectF& sheetRect, double sheetWidthMm,
	                       double cornerXmm, double cornerYmm, double dirX, double dirY)
	{
		double hx1mm = cornerXmm + (dirX < 0 ? -(CropOffsetMm + CropLengthMm) : CropOffsetMm);
		double hx2mm = cornerXmm + (dirX < 0 ? -CropOffsetMm : (CropOffsetMm + CropLengthMm));
		QPointF h1 = mmToWidgetPoint(sheetRect, sheetWidthMm, hx1mm, cornerYmm + dirY * CropOffsetMm);
		QPointF h2 = mmToWidgetPoint(sheetRect, sheetWidthMm, hx2mm, cornerYmm + dirY * CropOffsetMm);
		painter.drawLine(h1, h2);

		double vy1mm = cornerYmm + (dirY < 0 ? -(CropOffsetMm + CropLengthMm) : CropOffsetMm);
		double vy2mm = cornerYmm + (dirY < 0 ? -CropOffsetMm : (CropOffsetMm + CropLengthMm));
		QPointF v1 = mmToWidgetPoint(sheetRect, sheetWidthMm, cornerXmm + dirX * CropOffsetMm, vy1mm);
		QPointF v2 = mmToWidgetPoint(sheetRect, sheetWidthMm, cornerXmm + dirX * CropOffsetMm, vy2mm);
		painter.drawLine(v1, v2);
	}

	void drawCropMarksForPage(QPainter& painter, const QRectF& sheetRect, double sheetWidthMm, const ScImpositionEngine::PlacedPage& placed)
	{
		double left = placed.pageX;
		double right = placed.pageX + placed.pageW;
		double top = placed.pageY;
		double bottom = placed.pageY + placed.pageH;
		drawCropMarkPair(painter, sheetRect, sheetWidthMm, left, top, -1, -1);
		drawCropMarkPair(painter, sheetRect, sheetWidthMm, right, top, +1, -1);
		drawCropMarkPair(painter, sheetRect, sheetWidthMm, left, bottom, -1, +1);
		drawCropMarkPair(painter, sheetRect, sheetWidthMm, right, bottom, +1, +1);
	}

	const double RegMarkInsetMm = 10.0;
	const double RegMarkDiameterMm = 6.0;
	const double RegMarkArmMm = 4.0;
	const double ColorBarPatchMm = 6.0;
	const double ColorBarStartXMm = 20.0;
	const double ColorBarBottomClearanceMm = 5.0;

	//! Approximate rendering of a registration target: circle + crosshair (✚).
	//! Real weight/geometry is applied at PDF-export time; this is a preview.
	void drawRegistrationMark(QPainter& painter, const QRectF& sheetRect, double sheetWidthMm, double cxMm, double cyMm)
	{
		double scale = sheetRect.width() / sheetWidthMm;
		QPointF center = mmToWidgetPoint(sheetRect, sheetWidthMm, cxMm, cyMm);
		double r = (RegMarkDiameterMm / 2.0) * scale;
		double arm = RegMarkArmMm * scale;
		painter.drawEllipse(center, r, r);
		painter.drawLine(QPointF(center.x() - arm, center.y()), QPointF(center.x() + arm, center.y()));
		painter.drawLine(QPointF(center.x(), center.y() - arm), QPointF(center.x(), center.y() + arm));
	}

	//! Approximate CMYK color bar: same 9-patch order as the PDF output
	//! (C/M/Y/K 100%, C/M/Y/K 50%, registration black), 6x6mm each, each
	//! labelled below -- mirrors ScImpositionEngine's addColorBar() patch list.
	void drawColorBar(QPainter& painter, const QRectF& sheetRect, const ImpositionSettings& settings)
	{
		const QColor patches[9] = {
			QColor(0, 174, 239), QColor(236, 0, 140), QColor(255, 241, 0), QColor(0, 0, 0),
			QColor(128, 214, 247), QColor(245, 128, 197), QColor(255, 248, 128), QColor(128, 128, 128),
			QColor(0, 0, 0)
		};
		const char* labels[9] = { "C100", "M100", "Y100", "K100", "C50", "M50", "Y50", "K50", "Reg" };
		double scale = sheetRect.width() / settings.sheetWidthMm;
		double yMm = settings.sheetHeightMm - ColorBarPatchMm - ColorBarBottomClearanceMm;

		QFont labelFont = painter.font();
		labelFont.setPointSizeF(6);

		for (int i = 0; i < 9; ++i)
		{
			double xMm = ColorBarStartXMm + i * ColorBarPatchMm;
			QRectF patchRect(sheetRect.x() + xMm * scale, sheetRect.y() + yMm * scale,
			                  ColorBarPatchMm * scale, ColorBarPatchMm * scale);
			painter.setPen(QPen(Qt::black, 0.5));
			painter.fillRect(patchRect, patches[i]);
			painter.drawRect(patchRect);

			QRectF labelRect(patchRect.x(), patchRect.bottom(), patchRect.width(), 4.0 * scale);
			painter.setFont(labelFont);
			painter.setPen(Qt::black);
			painter.drawText(labelRect, Qt::AlignCenter, QString::fromLatin1(labels[i]));
		}
	}

	//! Approximate slug line text, top-left of the plate.
	void drawSlugLine(QPainter& painter, const QRectF& sheetRect, const ImpositionSettings& settings)
	{
		QString text = QStringLiteral("%1 | %2 | Page %3-%4 | Edition %5")
			.arg(settings.pubCode.isEmpty() ? QStringLiteral("PUB") : settings.pubCode)
			.arg(QDate::currentDate().toString(QStringLiteral("dd-MM-yyyy")))
			.arg(settings.leftPageNumber)
			.arg(settings.rightPageNumber)
			.arg(settings.editionCode.isEmpty() ? QStringLiteral("ED") : settings.editionCode);

		QFont slugFont = painter.font();
		slugFont.setPointSizeF(8);
		painter.setFont(slugFont);
		painter.setPen(Qt::black);
		QRectF slugRect(sheetRect.x() + 10.0 * (sheetRect.width() / settings.sheetWidthMm),
		                 sheetRect.y(),
		                 sheetRect.width() - 20.0 * (sheetRect.width() / settings.sheetWidthMm),
		                 6.0 * (sheetRect.width() / settings.sheetWidthMm));
		painter.drawText(slugRect, Qt::AlignLeft | Qt::AlignVCenter, text);
	}
}

ImpositionPreviewWidget::ImpositionPreviewWidget(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(600, 400);
}

QSize ImpositionPreviewWidget::sizeHint() const
{
	return QSize(700, 460);
}

void ImpositionPreviewWidget::setSettings(const ImpositionSettings& settings)
{
	m_settings = settings;
	update();
}

QRectF ImpositionPreviewWidget::sheetRectInWidget() const
{
	QRectF avail(PreviewPadding, PreviewPadding, width() - 2.0 * PreviewPadding, height() - 2.0 * PreviewPadding);
	if (avail.width() <= 0 || avail.height() <= 0 || m_settings.sheetWidthMm <= 0 || m_settings.sheetHeightMm <= 0)
		return QRectF();
	double scale = std::min(avail.width() / m_settings.sheetWidthMm, avail.height() / m_settings.sheetHeightMm);
	double w = m_settings.sheetWidthMm * scale;
	double h = m_settings.sheetHeightMm * scale;
	return QRectF(avail.x() + (avail.width() - w) / 2.0, avail.y() + (avail.height() - h) / 2.0, w, h);
}

QRectF ImpositionPreviewWidget::mmRectToWidget(const QRectF& sheetRect, double xMm, double yMm, double wMm, double hMm) const
{
	double scale = sheetRect.width() / m_settings.sheetWidthMm;
	return QRectF(sheetRect.x() + xMm * scale, sheetRect.y() + yMm * scale, wMm * scale, hMm * scale);
}

void ImpositionPreviewWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette().window());

	QRectF sheetRect = sheetRectInWidget();
	if (sheetRect.isEmpty())
		return;

	// Sheet.
	painter.setPen(QPen(palette().text().color(), 1));
	painter.setBrush(QColor(250, 250, 250));
	painter.drawRect(sheetRect);

	// Margin guides, light blue -- non-printing, preview only.
	if (m_settings.showGuidelines)
	{
		QRectF marginRect = mmRectToWidget(sheetRect,
			m_settings.marginLeftMm, m_settings.marginTopMm,
			m_settings.sheetWidthMm - m_settings.marginLeftMm - m_settings.marginRightMm,
			m_settings.sheetHeightMm - m_settings.marginTopMm - m_settings.marginBottomMm);
		painter.setPen(QPen(QColor(120, 170, 230), 1, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(marginRect);
	}

	ScImpositionEngine::PlacedPage left = ScImpositionEngine::computeLeftPlacement(
		m_settings.leftPageWidthMm, m_settings.leftPageHeightMm, m_settings);
	ScImpositionEngine::PlacedPage right = ScImpositionEngine::computeRightPlacement(
		m_settings.leftPageWidthMm, m_settings.leftPageHeightMm,
		m_settings.rightPageWidthMm, m_settings.rightPageHeightMm, m_settings);

	auto drawHalf = [&](const ScImpositionEngine::PlacedPage& placed, bool pageValid, const QString& filePath, const QString& label)
	{
		if (m_settings.showPrintArea)
		{
			QRectF pageRect = mmRectToWidget(sheetRect, placed.pageX, placed.pageY, placed.pageW, placed.pageH);
			painter.setBrush(QColor(235, 242, 250));
			painter.setPen(placed.overflows ? QPen(QColor(200, 40, 40), 2) : QPen(palette().text().color(), 1));
			painter.drawRect(pageRect);

			painter.setPen(palette().text().color());
			QString fileLabel = pageValid ? QFileInfo(filePath).fileName() : tr("(no file)");
			painter.drawText(pageRect, Qt::AlignCenter,
				tr("%1 PAGE\n(%2)").arg(label, fileLabel));
		}

		if (m_settings.showAutoMarks && pageValid)
		{
			painter.setPen(QPen(Qt::black, 1));
			drawCropMarksForPage(painter, sheetRect, m_settings.sheetWidthMm, placed);
		}
	};

	drawHalf(left, m_settings.leftPageValid, m_settings.leftFilePath, tr("LEFT"));
	drawHalf(right, m_settings.rightPageValid, m_settings.rightFilePath, tr("RIGHT"));

	// Gutter, labelled between the two pages (landscape spread only).
	if (m_settings.showPrintArea && m_settings.leftPageValid && m_settings.rightPageValid && m_settings.gutterMm > 0.0)
	{
		QRectF gutterRect = mmRectToWidget(sheetRect, left.pageX + left.pageW, left.pageY, m_settings.gutterMm, left.pageH);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 230, 150));
		painter.drawRect(gutterRect);
	}

	// Plate-level marks: registration targets, color bar, slug line. Drawn
	// once for the whole sheet (not per-page), on top of everything else --
	// each independently gated by its own Elements-panel checkbox. Barcodes
	// has no content/symbology specified yet, so it draws nothing even when
	// showBarcodes is set.
	if (m_settings.showRegmarks)
	{
		painter.setPen(QPen(Qt::black, 1));
		painter.setBrush(Qt::NoBrush);
		const double inset = RegMarkInsetMm;
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, inset, inset);
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, m_settings.sheetWidthMm - inset, inset);
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, inset, m_settings.sheetHeightMm - inset);
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, m_settings.sheetWidthMm - inset, m_settings.sheetHeightMm - inset);
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, m_settings.sheetWidthMm / 2.0, inset);
		drawRegistrationMark(painter, sheetRect, m_settings.sheetWidthMm, m_settings.sheetWidthMm / 2.0, m_settings.sheetHeightMm - inset);
	}

	if (m_settings.showColourBar)
		drawColorBar(painter, sheetRect, m_settings);

	if (m_settings.showFurnitures)
		drawSlugLine(painter, sheetRect, m_settings);
}
