/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include "erasermasktests.h"
#include "scimageerasermask.h"

void EraserMaskTests::testCreateForCapsLongEdge()
{
	QImage small = ScEraserMask::createFor(800, 600);
	QCOMPARE(small.size(), QSize(800, 600));
	QCOMPARE(small.format(), QImage::Format_Grayscale8);

	// Keeps .sla payloads sane for a full-page newspaper photo.
	QImage big = ScEraserMask::createFor(6000, 3000);
	QCOMPARE(big.size(), QSize(2048, 1024));

	QVERIFY(ScEraserMask::createFor(0, 100).isNull());
}

void EraserMaskTests::testEmptyMaskIsNotStored()
{
	QImage fresh = ScEraserMask::createFor(200, 200);
	QVERIFY(ScEraserMask::isEmptyMask(fresh));

	// An erased-then-fully-restored frame must save byte-identically to one
	// that was never erased.
	ScImageEffectList list;
	ScEraserMask::setMask(list, fresh);
	QVERIFY(list.isEmpty());
}

void EraserMaskTests::testHardBrush()
{
	QImage base = ScEraserMask::createFor(400, 400);
	QImage mask = base.copy();
	QImage coverage(mask.size(), QImage::Format_Grayscale8);
	coverage.fill(0);

	QRect touched = ScEraserMask::stamp(coverage, QPointF(200, 200), 50, 1.0);
	QVERIFY(!touched.isNull());
	ScEraserMask::applyStroke(mask, base, coverage, false, touched);

	QCOMPARE(qGray(mask.pixel(200, 200)), 0);
	QCOMPARE(qGray(mask.pixel(200, 300)), 255);
	QVERIFY(!ScEraserMask::isEmptyMask(mask));
}

void EraserMaskTests::testSoftBrushProfile()
{
	QImage base = ScEraserMask::createFor(400, 400);
	QImage mask = base.copy();
	QImage coverage(mask.size(), QImage::Format_Grayscale8);
	coverage.fill(0);

	QRect touched = ScEraserMask::stamp(coverage, QPointF(200, 200), 50, 0.0);
	ScEraserMask::applyStroke(mask, base, coverage, false, touched);

	int centre = qGray(mask.pixel(200, 200));
	int mid    = qGray(mask.pixel(235, 200));
	int outer  = qGray(mask.pixel(249, 200));

	QCOMPARE(centre, 0);
	QVERIFY(mid > 0 && mid < 255);   // genuinely feathered, not a hard disc
	QVERIFY(outer > mid);            // and fading outward
}

void EraserMaskTests::testRestoreIsTheInverse()
{
	QImage base = ScEraserMask::createFor(400, 400);
	QImage mask = base.copy();
	QImage coverage(mask.size(), QImage::Format_Grayscale8);
	coverage.fill(0);
	QRect touched = ScEraserMask::stamp(coverage, QPointF(200, 200), 50, 1.0);
	ScEraserMask::applyStroke(mask, base, coverage, false, touched);
	QCOMPARE(qGray(mask.pixel(200, 200)), 0);

	QImage erased = mask.copy();
	QImage restoreCoverage(mask.size(), QImage::Format_Grayscale8);
	restoreCoverage.fill(0);
	QRect rr = ScEraserMask::stamp(restoreCoverage, QPointF(200, 200), 50, 1.0);
	ScEraserMask::applyStroke(mask, erased, restoreCoverage, true, rr);

	QCOMPARE(qGray(mask.pixel(200, 200)), 255);
	QVERIFY(ScEraserMask::isEmptyMask(mask));
}

void EraserMaskTests::testStrokeIndependentOfEventSplit()
{
	// Regression: applying each mouse-move event's coverage cumulatively let
	// overlapping dab feathers stack up, and restarting the dab phase per
	// event moved the dabs, so the same gesture came out differently
	// depending on how many events the pointer happened to generate.
	const double radius = 30.0;
	const double hardness = 0.65;
	const QPointF from(80, 100);
	const QPointF to(520, 100);

	QImage baseOne = ScEraserMask::createFor(600, 200);
	QImage one = baseOne.copy();
	QImage covOne(baseOne.size(), QImage::Format_Grayscale8);
	covOne.fill(0);
	double carryOne = 0.0;
	QRect r1 = ScEraserMask::stampLine(covOne, from, to, radius, hardness, carryOne);
	ScEraserMask::applyStroke(one, baseOne, covOne, false, r1);

	QImage baseMany = ScEraserMask::createFor(600, 200);
	QImage many = baseMany.copy();
	QImage covMany(baseMany.size(), QImage::Format_Grayscale8);
	covMany.fill(0);
	double carryMany = 0.0;
	QPointF prev = from;
	for (int i = 1; i <= 11; ++i)
	{
		QPointF cur(from.x() + (to.x() - from.x()) * i / 11.0, from.y());
		QRect r = ScEraserMask::stampLine(covMany, prev, cur, radius, hardness, carryMany);
		ScEraserMask::applyStroke(many, baseMany, covMany, false, r);
		prev = cur;
	}

	QCOMPARE(one.size(), many.size());
	int worst = 0;
	for (int y = 0; y < one.height(); ++y)
	{
		for (int x = 0; x < one.width(); ++x)
			worst = qMax(worst, qAbs(qGray(one.pixel(x, y)) - qGray(many.pixel(x, y))));
	}
	QCOMPARE(worst, 0);
}

void EraserMaskTests::testStrokeEdgeHasNoScallops()
{
	QImage base = ScEraserMask::createFor(600, 200);
	QImage mask = base.copy();
	QImage coverage(base.size(), QImage::Format_Grayscale8);
	coverage.fill(0);

	double carry = 0.0;
	QPointF prev(80, 100);
	for (int i = 1; i <= 11; ++i)
	{
		QPointF cur(80 + 440.0 * i / 11.0, 100);
		QRect r = ScEraserMask::stampLine(coverage, prev, cur, 30.0, 0.65, carry);
		ScEraserMask::applyStroke(mask, base, coverage, false, r);
		prev = cur;
	}

	// Walk the top edge of the stroke: a scallop shows up as a step.
	int prevTop = -1;
	int maxJump = 0;
	for (int x = 120; x < 480; ++x)
	{
		int top = 0;
		for (int y = 0; y < mask.height(); ++y)
		{
			if (qGray(mask.pixel(x, y)) < 250) { top = y; break; }
		}
		if (prevTop >= 0)
			maxJump = qMax(maxJump, qAbs(top - prevTop));
		prevTop = top;
	}
	QVERIFY2(maxJump <= 1, qPrintable(QString("stroke edge steps by %1 px").arg(maxJump)));
}

void EraserMaskTests::testCodecRoundTrip()
{
	QImage base = ScEraserMask::createFor(300, 200);
	QImage mask = base.copy();
	QImage coverage(mask.size(), QImage::Format_Grayscale8);
	coverage.fill(0);
	QRect r = ScEraserMask::stamp(coverage, QPointF(150, 100), 60, 0.4);
	ScEraserMask::applyStroke(mask, base, coverage, false, r);

	QString params = ScEraserMask::encode(mask);
	QVERIFY(!params.isEmpty());
	QVERIFY(params.startsWith(QLatin1String("1 ")));

	// The payload is written straight into the .sla as an XML attribute value.
	QString payload = params.mid(2);
	QVERIFY(!payload.contains(QLatin1Char('"')));
	QVERIFY(!payload.contains(QLatin1Char('<')));
	QVERIFY(!payload.contains(QLatin1Char('&')));

	QImage decoded = ScEraserMask::decode(params);
	QCOMPARE(decoded.size(), mask.size());
	QCOMPARE(decoded.format(), QImage::Format_Grayscale8);
	for (int y = 0; y < mask.height(); ++y)
	{
		for (int x = 0; x < mask.width(); ++x)
			QCOMPARE(qGray(decoded.pixel(x, y)), qGray(mask.pixel(x, y)));
	}
}

void EraserMaskTests::testCodecRejectsBadInput()
{
	QVERIFY(ScEraserMask::decode(QString()).isNull());
	QVERIFY(ScEraserMask::decode(QStringLiteral("2 garbage")).isNull());
	QVERIFY(ScEraserMask::decode(QStringLiteral("1 !!!notbase64!!!")).isNull());
	QVERIFY(ScEraserMask::decode(QStringLiteral("nospace")).isNull());
	QVERIFY(ScEraserMask::encode(QImage()).isEmpty());
}

void EraserMaskTests::testDigestIsShort()
{
	QImage mask = ScEraserMask::createFor(512, 512);
	mask.setPixel(0, 0, qRgb(0, 0, 0));
	QString params = ScEraserMask::encode(mask);

	// getImageEffectsModifier() concatenates parameters into the image cache
	// key, so the raw payload must never reach it.
	QVERIFY(params.size() > 200);
	QCOMPARE(ScEraserMask::digest(params).size(), 16);
	QCOMPARE(ScEraserMask::digest(params), ScEraserMask::digest(params));
	QVERIFY(ScEraserMask::digest(QString()).isEmpty());
}

void EraserMaskTests::testEffectListPlumbing()
{
	QImage mask = ScEraserMask::createFor(100, 100);
	mask.setPixel(10, 10, qRgb(0, 0, 0));

	ScImageEffectList list;
	ImageEffect grayscale;
	grayscale.effectCode = ImageEffect::EF_GRAYSCALE;
	list.append(grayscale);

	ScEraserMask::setMask(list, mask);
	QCOMPARE(list.count(), 2);
	QCOMPARE(ScEraserMask::indexIn(list), 1);
	QVERIFY(!ScEraserMask::maskOf(list).isNull());

	ScEraserMask::setMask(list, mask);
	QCOMPARE(list.count(), 2);   // replaced, not duplicated

	ScEraserMask::removeFrom(list);
	QCOMPARE(list.count(), 1);
	QCOMPARE(ScEraserMask::indexIn(list), -1);
	QCOMPARE(list.at(0).effectCode, static_cast<int>(ImageEffect::EF_GRAYSCALE));
}

void EraserMaskTests::testMergeIntoAlphaBytes()
{
	// Format_Grayscale8 carries no colour table in Qt6, so setPixel() takes a
	// QRgb rather than a grey level.
	QImage mask = ScEraserMask::createFor(4, 2);
	mask.fill(255);
	mask.setPixel(0, 0, qRgb(0, 0, 0));         // fully erased
	mask.setPixel(1, 0, qRgb(128, 128, 128));   // half erased

	QByteArray alpha;
	ScEraserMask::mergeIntoAlphaBytes(alpha, mask, 4, 2);
	QCOMPARE(alpha.size(), qsizetype(8));
	QCOMPARE(uchar(alpha[0]), uchar(0));
	QCOMPARE(uchar(alpha[1]), uchar(128));
	QCOMPARE(uchar(alpha[2]), uchar(255));

	// An alpha channel already loaded from the file must be combined with, not
	// replaced by, the erasure.
	QByteArray existing(8, char(uchar(128)));
	ScEraserMask::mergeIntoAlphaBytes(existing, mask, 4, 2);
	QCOMPARE(uchar(existing[0]), uchar(0));
	QCOMPARE(uchar(existing[1]), uchar(64));
	QCOMPARE(uchar(existing[2]), uchar(128));
}

void EraserMaskTests::testMergeIntoPdfImageMask()
{
	QImage mask = ScEraserMask::createFor(4, 2);
	mask.fill(255);
	mask.setPixel(0, 0, qRgb(0, 0, 0));
	mask.setPixel(1, 0, qRgb(128, 128, 128));   // >= threshold, stays painted

	QByteArray stencil;
	ScEraserMask::mergeIntoPdfImageMask(stencil, mask, 4, 2);
	QCOMPARE(stencil.size(), qsizetype(2));   // one padded byte per row
	// Sense is inverted for PDF /ImageMask: bit 1 means "masked out".
	QCOMPARE(uchar(stencil[0]), uchar(0x80));
	QCOMPARE(uchar(stencil[1]), uchar(0x00));
}

void EraserMaskTests::testApplyToAlpha()
{
	QImage mask = ScEraserMask::createFor(4, 2);
	mask.fill(255);
	mask.setPixel(0, 0, qRgb(0, 0, 0));

	QImage image(4, 2, QImage::Format_ARGB32);
	image.fill(qRgba(10, 20, 30, 255));
	ScEraserMask::applyToAlpha(image, mask);

	QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
	QCOMPARE(qAlpha(image.pixel(2, 0)), 255);
	// Colour channels must survive: only alpha carries the erasure.
	QCOMPARE(qRed(image.pixel(0, 0)), 10);
	QCOMPARE(qGreen(image.pixel(0, 0)), 20);
	QCOMPARE(qBlue(image.pixel(0, 0)), 30);
}

QTEST_APPLESS_MAIN(EraserMaskTests)
