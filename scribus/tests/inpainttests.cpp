/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <atomic>

#include "inpainttests.h"
#include "util_inpaint.h"

namespace
{
	//! A mask with one filled rectangle, in the form the kernel expects.
	QImage rectMask(const QSize& size, const QRect& hole)
	{
		QImage mask(size, QImage::Format_Grayscale8);
		mask.fill(0);
		for (int y = hole.top(); y <= hole.bottom(); ++y)
		{
			uchar* line = mask.scanLine(y);
			for (int x = hole.left(); x <= hole.right(); ++x)
				line[x] = 255;
		}
		return mask;
	}

	//! Punch \a hole out of \a image so the test cannot accidentally measure
	//! the original pixels still sitting under the mask.
	void clobber(QImage& image, const QRect& hole, QRgb with = qRgb(255, 0, 255))
	{
		for (int y = hole.top(); y <= hole.bottom(); ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int x = hole.left(); x <= hole.right(); ++x)
				line[x] = with;
		}
	}

	//! Largest per-channel difference between two pixels.
	int channelDistance(QRgb a, QRgb b)
	{
		return qMax(qMax(qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b))),
		            qAbs(qBlue(a) - qBlue(b)));
	}
}

void InpaintTests::testSolidColourIsRestored()
{
	const QRect hole(40, 40, 20, 20);
	QImage image(100, 100, QImage::Format_RGB32);
	image.fill(qRgb(90, 140, 200));
	clobber(image, hole);

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), hole));
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());

	// A flat field has nothing to reconstruct, so anything but the same flat
	// field means the weights are wrong.
	int worst = 0;
	for (int y = hole.top(); y <= hole.bottom(); ++y)
	{
		for (int x = hole.left(); x <= hole.right(); ++x)
			worst = qMax(worst, channelDistance(result.pixel(x, y), qRgb(90, 140, 200)));
	}
	QVERIFY2(worst <= 2, qPrintable(QStringLiteral("worst channel error %1").arg(worst)));

	// Nothing outside the mask may move at all.
	QCOMPARE(result.pixel(0, 0), qRgb(90, 140, 200));
	QCOMPARE(result.pixel(39, 40), qRgb(90, 140, 200));
}

void InpaintTests::testHorizontalGradientIsContinued()
{
	const QRect hole(40, 40, 20, 20);
	QImage image(100, 100, QImage::Format_RGB32);
	for (int y = 0; y < 100; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 100; ++x)
		{
			const int v = x * 2;   // 0 .. 198, a clean left-to-right ramp
			line[x] = qRgb(v, v, v);
		}
	}
	const QImage truth = image.copy();
	clobber(image, hole);

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), hole));
	QVERIFY(!result.isNull());

	// The ramp has to come through the hole, not be flattened into its mean.
	// Column means are the honest measure: they are what a flattened fill
	// would visibly get wrong.
	double worstColumn = 0.0;
	for (int x = hole.left(); x <= hole.right(); ++x)
	{
		double got = 0.0;
		double want = 0.0;
		for (int y = hole.top(); y <= hole.bottom(); ++y)
		{
			got += qRed(result.pixel(x, y));
			want += qRed(truth.pixel(x, y));
		}
		got /= hole.height();
		want /= hole.height();
		worstColumn = qMax(worstColumn, qAbs(got - want));
	}
	QVERIFY2(worstColumn < 8.0,
	         qPrintable(QStringLiteral("worst column mean error %1").arg(worstColumn)));

	// And it must still be a ramp: strictly rising left to right across the fill.
	int previous = -1;
	for (int x = hole.left(); x <= hole.right(); ++x)
	{
		const int v = qRed(result.pixel(x, hole.top() + hole.height() / 2));
		QVERIFY2(v >= previous, qPrintable(QStringLiteral("column %1 fell to %2").arg(x).arg(v)));
		previous = v;
	}
}

void InpaintTests::testVerticalStripesStayBanded()
{
	// Stripes of period 20, and a hole wide enough to span three of them but
	// short enough that the intact rows above and below stay within reach of
	// the brush radius. Telea continues structure across a short gap; it does
	// not invent texture across a long one, and the test says so.
	const QRect hole(35, 46, 30, 8);
	QImage image(100, 100, QImage::Format_RGB32);
	for (int y = 0; y < 100; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 100; ++x)
		{
			const int v = ((x / 10) % 2) ? 220 : 40;
			line[x] = qRgb(v, v, v);
		}
	}
	clobber(image, hole);

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), hole));
	QVERIFY(!result.isNull());

	// Every filled column must land on the side of the mid-grey its own stripe
	// belongs to: the banding survives even though the exact edges soften.
	for (int x = hole.left(); x <= hole.right(); ++x)
	{
		const bool light = ((x / 10) % 2) != 0;
		double mean = 0.0;
		for (int y = hole.top(); y <= hole.bottom(); ++y)
			mean += qRed(result.pixel(x, y));
		mean /= hole.height();

		if (light)
			QVERIFY2(mean > 130.0, qPrintable(QStringLiteral("light column %1 came out at %2").arg(x).arg(mean)));
		else
			QVERIFY2(mean < 130.0, qPrintable(QStringLiteral("dark column %1 came out at %2").arg(x).arg(mean)));
	}
}

void InpaintTests::testEmptyMaskReturnsInputUnchanged()
{
	QImage image(64, 48, QImage::Format_ARGB32);
	image.fill(qRgba(1, 2, 3, 4));

	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(0);

	QImage result = InpaintTelea::inpaint(image, mask);
	QVERIFY(!result.isNull());
	QCOMPARE(result.format(), image.format());
	// Bit-identical, not merely equal-looking: an empty mask must not even
	// round-trip the picture through another format.
	QCOMPARE(result, image);
}

void InpaintTests::testFullyMaskedImageDoesNotCrash()
{
	QImage image(32, 32, QImage::Format_RGB32);
	image.fill(qRgb(10, 20, 30));

	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(255);

	// Degenerate: there is no known pixel anywhere to reconstruct from. The
	// only requirement is that it comes back rather than crashing or hanging.
	QImage result = InpaintTelea::inpaint(image, mask);
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());
}

void InpaintTests::testMaskTouchingBorder()
{
	QImage image(80, 60, QImage::Format_RGB32);
	image.fill(qRgb(200, 30, 30));

	// Every edge and both corners, so a missing bounds check anywhere in the
	// stencils shows up here.
	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(0);
	for (int x = 0; x < 12; ++x)
	{
		mask.scanLine(0)[x] = 255;
		mask.scanLine(59)[x] = 255;
		mask.scanLine(0)[79 - x] = 255;
	}
	for (int y = 0; y < 12; ++y)
	{
		mask.scanLine(y)[0] = 255;
		mask.scanLine(y)[79] = 255;
	}
	clobber(image, QRect(0, 0, 12, 1));

	QImage result = InpaintTelea::inpaint(image, mask);
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());
	QVERIFY(channelDistance(result.pixel(0, 0), qRgb(200, 30, 30)) <= 2);
	QVERIFY(channelDistance(result.pixel(79, 0), qRgb(200, 30, 30)) <= 2);
	QVERIFY(channelDistance(result.pixel(0, 59), qRgb(200, 30, 30)) <= 2);
}

void InpaintTests::testCancelReturnsNullImage()
{
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(qRgb(120, 120, 120));

	std::atomic<bool> cancel(true);
	InpaintTelea::Options opts;
	opts.cancel = &cancel;

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), QRect(50, 50, 100, 100)), opts);
	QVERIFY(result.isNull());
}

void InpaintTests::testProgressIsMonotonicAndCompletes()
{
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(qRgb(70, 80, 90));

	QList<int> seen;
	InpaintTelea::Options opts;
	opts.progress = [&seen](int percent) { seen.append(percent); };

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), QRect(60, 60, 80, 80)), opts);
	QVERIFY(!result.isNull());
	QVERIFY(!seen.isEmpty());

	for (int i = 1; i < seen.size(); ++i)
		QVERIFY2(seen.at(i) >= seen.at(i - 1), "progress went backwards");
	QVERIFY(seen.first() >= 0);
	QCOMPARE(seen.last(), 100);
}

void InpaintTests::testAlphaIsPreservedOnOpaqueFormat()
{
	QImage image(60, 60, QImage::Format_ARGB32);
	image.fill(qRgba(30, 60, 90, 200));

	QImage result = InpaintTelea::inpaint(image, rectMask(image.size(), QRect(20, 20, 20, 20)));
	QVERIFY(!result.isNull());
	QCOMPARE(result.format(), QImage::Format_ARGB32);
	// Alpha is a channel like any other and has to survive the fill.
	QVERIFY(qAbs(qAlpha(result.pixel(30, 30)) - 200) <= 2);
}

void InpaintTests::testRegionOfInterestKeepsLargeImagesFast()
{
	// A newspaper-sized photo with a small removal. Without confining the work
	// to the mask's neighbourhood this allocates and sweeps twelve million
	// pixels of marching state and takes tens of seconds.
	QImage image(4000, 3000, QImage::Format_RGB32);
	image.fill(qRgb(128, 128, 128));

	const QRect hole(1975, 1475, 50, 50);
	QImage mask = rectMask(image.size(), hole);

	QElapsedTimer timer;
	timer.start();
	QImage result = InpaintTelea::inpaint(image, mask);
	const qint64 elapsed = timer.elapsed();

	QVERIFY(!result.isNull());
	QVERIFY2(elapsed < 1000, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
	QVERIFY(channelDistance(result.pixel(2000, 1500), qRgb(128, 128, 128)) <= 2);
}

QTEST_APPLESS_MAIN(InpaintTests)
