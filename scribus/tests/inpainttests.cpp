/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include <QtTest/QtTest>

#include <atomic>
#include <cmath>

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

	Inpaint::Options withMethod(Inpaint::Method m)
	{
		Inpaint::Options o;
		o.method = m;
		return o;
	}

	//! Mean gradient magnitude over \a area. The measure of how much detail a
	//! region carries, and so of whether a fill is a picture or a smudge.
	double gradientEnergy(const QImage& img, const QRect& area)
	{
		double sum = 0.0;
		long n = 0;
		const QRect r = area.intersected(img.rect().adjusted(1, 1, -1, -1));
		for (int y = r.top(); y <= r.bottom(); ++y)
		{
			for (int x = r.left(); x <= r.right(); ++x)
			{
				double gx = 0.0;
				double gy = 0.0;
				const QRgb l = img.pixel(x - 1, y);
				const QRgb rr = img.pixel(x + 1, y);
				const QRgb u = img.pixel(x, y - 1);
				const QRgb d = img.pixel(x, y + 1);
				const int lc[3] = { qRed(l), qGreen(l), qBlue(l) };
				const int rc[3] = { qRed(rr), qGreen(rr), qBlue(rr) };
				const int uc[3] = { qRed(u), qGreen(u), qBlue(u) };
				const int dc[3] = { qRed(d), qGreen(d), qBlue(d) };
				for (int c = 0; c < 3; ++c)
				{
					gx += double(rc[c] - lc[c]) * double(rc[c] - lc[c]);
					gy += double(dc[c] - uc[c]) * double(dc[c] - uc[c]);
				}
				sum += std::sqrt(gx + gy);
				++n;
			}
		}
		return n ? sum / double(n) : 0.0;
	}

	/*! \brief A repeatable pseudo-random texture.

	    Deliberately not QRandomGenerator: these tests assert on measured
	    detail, so the picture they measure has to be the same picture on every
	    machine and every run. */
	quint32 nextRandom(quint32& state)
	{
		state = state * 1664525u + 1013904223u;
		return state;
	}

	//! Coloured noise over a base colour: something with real detail in it.
	QImage texturedField(int w, int h, int amplitude = 60, quint32 seed = 12345u)
	{
		QImage img(w, h, QImage::Format_RGB32);
		quint32 state = seed;
		for (int y = 0; y < h; ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < w; ++x)
			{
				const int r = 110 + int(nextRandom(state) % quint32(amplitude)) - amplitude / 2;
				const int g = 130 + int(nextRandom(state) % quint32(amplitude)) - amplitude / 2;
				const int b = 90 + int(nextRandom(state) % quint32(amplitude)) - amplitude / 2;
				line[x] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
			}
		}
		return img;
	}

	/*! \brief A stand-in for the photograph this whole exercise is about.

	    Bands of different material - sky, a hard horizon line, a row of
	    figures, textured ground - which is the structure that matters: a
	    diffusion runs them all into one another, and that is what a reader
	    notices. */
	QImage crowdLikeScene(int w, int h)
	{
		QImage img(w, h, QImage::Format_RGB32);
		quint32 state = 777u;
		const int horizon = h / 3;
		const int groundTop = (2 * h) / 3;
		for (int y = 0; y < h; ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < w; ++x)
			{
				if (y < horizon)
				{
					// Sky: a smooth vertical ramp, barely any detail.
					const int v = 150 + (y * 40) / qMax(1, horizon);
					line[x] = qRgb(v - 30, v - 10, v + 20);
				}
				else if (y < horizon + 3)
				{
					line[x] = qRgb(40, 40, 45);     // the horizon rail
				}
				else if (y < groundTop)
				{
					// Figures: alternating blocks of clothing colour, each with
					// its own noise, so neighbouring blocks must not merge.
					const int block = x / 17;
					const int base = (block % 3 == 0) ? 200 : (block % 3 == 1 ? 70 : 140);
					const int n = int(nextRandom(state) % 40u) - 20;
					line[x] = qRgb(qBound(0, base + n, 255),
					               qBound(0, base - 20 + n, 255),
					               qBound(0, base - 40 + n, 255));
				}
				else
				{
					// Ground: strong fine texture.
					const int n = int(nextRandom(state) % 70u);
					line[x] = qRgb(40 + n / 2, 90 + n, 30 + n / 3);
				}
			}
		}
		return img;
	}
}

// ---------------------------------------------- fast marching (Telea)

void InpaintTests::testSolidColourIsRestored()
{
	const QRect hole(40, 40, 20, 20);
	QImage image(100, 100, QImage::Format_RGB32);
	image.fill(qRgb(90, 140, 200));
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::FastMarching));
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

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::FastMarching));
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
	// Measures about 5; a flat fill would score 20, which is what this bounds.
	QVERIFY2(worstColumn < 8.0,
	         qPrintable(QStringLiteral("worst column mean error %1").arg(worstColumn)));

	// And it must still be a ramp: rising left to right across the fill.
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

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::FastMarching));
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

// ------------------------------------------------------------ contract

void InpaintTests::testEmptyMaskReturnsInputUnchanged()
{
	QImage image(64, 48, QImage::Format_ARGB32);
	image.fill(qRgba(1, 2, 3, 4));

	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(0);

	QImage result = Inpaint::inpaint(image, mask);
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
	QImage result = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::FastMarching));
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());
}

void InpaintTests::testExemplarWithNoSourceDoesNotCrash()
{
	// The same degenerate case through the exemplar method, which has further
	// to fall: there is no complete patch of untouched picture to copy from
	// anywhere, so every round has to take the fallback and still terminate.
	QImage image(40, 40, QImage::Format_RGB32);
	image.fill(qRgb(10, 20, 30));

	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(255);

	QImage result = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::Exemplar));
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

	QImage result = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::FastMarching));
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());
	QVERIFY(channelDistance(result.pixel(0, 0), qRgb(200, 30, 30)) <= 2);
	QVERIFY(channelDistance(result.pixel(79, 0), qRgb(200, 30, 30)) <= 2);
	QVERIFY(channelDistance(result.pixel(0, 59), qRgb(200, 30, 30)) <= 2);
}

void InpaintTests::testExemplarMaskTouchingBorder()
{
	// The exemplar method reads whole patches around each candidate, so it has
	// more ways to run off the end of the picture than the marching one does.
	QImage image = texturedField(120, 90);
	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(0);
	for (int y = 0; y < 14; ++y)
		for (int x = 0; x < 14; ++x)
		{
			mask.scanLine(y)[x] = 255;                                  // top left
			mask.scanLine(y)[119 - x] = 255;                            // top right
			mask.scanLine(89 - y)[x] = 255;                             // bottom left
			mask.scanLine(89 - y)[119 - x] = 255;                       // bottom right
		}

	QImage result = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());
	QCOMPARE(result.size(), image.size());
}

void InpaintTests::testCancelReturnsNullImage()
{
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(qRgb(120, 120, 120));

	std::atomic<bool> cancel(true);
	Inpaint::Options opts = withMethod(Inpaint::Method::FastMarching);
	opts.cancel = &cancel;

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), QRect(50, 50, 100, 100)), opts);
	QVERIFY(result.isNull());
}

void InpaintTests::testExemplarCancelReturnsNullImage()
{
	QImage image = texturedField(200, 200);

	std::atomic<bool> cancel(true);
	Inpaint::Options opts = withMethod(Inpaint::Method::Exemplar);
	opts.cancel = &cancel;

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), QRect(60, 60, 80, 80)), opts);
	QVERIFY(result.isNull());
}

void InpaintTests::testProgressIsMonotonicAndCompletes()
{
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(qRgb(70, 80, 90));

	QList<int> seen;
	Inpaint::Options opts = withMethod(Inpaint::Method::FastMarching);
	opts.progress = [&seen](int percent) { seen.append(percent); };

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), QRect(60, 60, 80, 80)), opts);
	QVERIFY(!result.isNull());
	QVERIFY(!seen.isEmpty());

	for (int i = 1; i < seen.size(); ++i)
		QVERIFY2(seen.at(i) >= seen.at(i - 1), "progress went backwards");
	QVERIFY(seen.first() >= 0);
	QCOMPARE(seen.last(), 100);
}

void InpaintTests::testExemplarProgressIsMonotonicAndCompletes()
{
	QImage image = texturedField(200, 200);

	QList<int> seen;
	Inpaint::Options opts = withMethod(Inpaint::Method::Exemplar);
	opts.progress = [&seen](int percent) { seen.append(percent); };

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), QRect(70, 70, 60, 60)), opts);
	QVERIFY(!result.isNull());
	QVERIFY(!seen.isEmpty());

	for (int i = 1; i < seen.size(); ++i)
		QVERIFY2(seen.at(i) >= seen.at(i - 1), "progress went backwards");
	QCOMPARE(seen.last(), 100);
}

void InpaintTests::testAlphaIsPreservedOnOpaqueFormat()
{
	QImage image(60, 60, QImage::Format_ARGB32);
	image.fill(qRgba(30, 60, 90, 200));

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), QRect(20, 20, 20, 20)),
	                                 withMethod(Inpaint::Method::FastMarching));
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
	QImage result = Inpaint::inpaint(image, mask);
	const qint64 elapsed = timer.elapsed();

	QVERIFY(!result.isNull());
	QVERIFY2(elapsed < 1000, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
	QVERIFY(channelDistance(result.pixel(2000, 1500), qRgb(128, 128, 128)) <= 2);
}

// -------------------------------------------------- choosing a method

void InpaintTests::testAutoUsesFastMarchingForThinMasks()
{
	// A scratch across a busy picture. Thin enough that every pixel of it is
	// a pixel or two from real data, so the cheap method is also the right
	// one, however textured the surroundings are.
	QImage image = texturedField(200, 200);
	QImage mask(image.size(), QImage::Format_Grayscale8);
	mask.fill(0);
	for (int y = 40; y < 160; ++y)
	{
		mask.scanLine(y)[100] = 255;
		mask.scanLine(y)[101] = 255;
	}

	Inpaint::Method chosen = Inpaint::Method::Exemplar;
	Inpaint::Options opts;
	opts.chosenMethod = &chosen;
	QImage result = Inpaint::inpaint(image, mask, opts);
	QVERIFY(!result.isNull());
	QCOMPARE(chosen, Inpaint::Method::FastMarching);
}

void InpaintTests::testAutoUsesFastMarchingForFlatSurroundings()
{
	// A wide hole, but in flat colour. There is no texture to reproduce, the
	// diffusion is exactly right, and it is several times cheaper. Running the
	// exemplar method here would be worse than useless: it would paste detail
	// into a region that has none.
	QImage image(200, 200, QImage::Format_RGB32);
	image.fill(qRgb(120, 150, 190));
	const QRect hole(70, 70, 60, 60);
	clobber(image, hole);

	Inpaint::Method chosen = Inpaint::Method::Exemplar;
	Inpaint::Options opts;
	opts.chosenMethod = &chosen;
	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole), opts);
	QVERIFY(!result.isNull());
	QCOMPARE(chosen, Inpaint::Method::FastMarching);

	// And it really did stay flat.
	QVERIFY(channelDistance(result.pixel(100, 100), qRgb(120, 150, 190)) <= 2);
}

void InpaintTests::testAutoUsesExemplarForTexturedSurroundings()
{
	// Wide and busy: the case the whole exemplar path exists for.
	QImage image = texturedField(220, 220, 90);
	const QRect hole(80, 80, 60, 60);
	clobber(image, hole);

	Inpaint::Method chosen = Inpaint::Method::FastMarching;
	Inpaint::Options opts;
	opts.chosenMethod = &chosen;
	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole), opts);
	QVERIFY(!result.isNull());
	QCOMPARE(chosen, Inpaint::Method::Exemplar);
}

// ------------------------------------------------ reconstruction quality

void InpaintTests::testTextureEnergySurvivesInExemplarFill()
{
	// The heart of the matter. A diffusion averages, an average of pixels has
	// less detail than the pixels it averaged, and after enough rounds of that
	// the middle of a wide hole is a smooth blob whatever surrounds it. This
	// pins the difference in the only terms that describe what a reader
	// actually notices.
	QImage image = texturedField(220, 220, 90);
	const QRect hole(80, 80, 60, 60);
	clobber(image, hole);
	QImage mask = rectMask(image.size(), hole);

	const QImage marched = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::FastMarching));
	const QImage copied = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!marched.isNull());
	QVERIFY(!copied.isNull());

	// Measured well inside the hole, where a diffusion has had the most rounds
	// to flatten things out.
	const QRect core(95, 95, 30, 30);
	const QRect surroundings(30, 30, 40, 40);   // untouched picture
	const double reference = gradientEnergy(image, surroundings);
	const double marchedEnergy = gradientEnergy(marched, core);
	const double copiedEnergy = gradientEnergy(copied, core);

	QVERIFY2(reference > 20.0, "the test texture is not textured enough to measure");

	// The diffusion loses most of it...
	QVERIFY2(marchedEnergy < 0.5 * reference,
	         qPrintable(QStringLiteral("fast marching kept %1 of %2, expected it to lose most")
	                    .arg(marchedEnergy).arg(reference)));
	// ...and copying real patches keeps essentially all of it.
	QVERIFY2(copiedEnergy > 0.7 * reference,
	         qPrintable(QStringLiteral("exemplar kept only %1 of %2")
	                    .arg(copiedEnergy).arg(reference)));
	QVERIFY2(copiedEnergy > 2.0 * marchedEnergy,
	         qPrintable(QStringLiteral("exemplar %1 vs fast marching %2 - not the improvement claimed")
	                    .arg(copiedEnergy).arg(marchedEnergy)));
}

void InpaintTests::testSharpVerticalEdgeIsNotBlurredAway()
{
	// Two flat fields either side of a hard vertical edge, with a hole
	// straddling it. Averaging across the edge is what makes the muddy band a
	// reader spots immediately; the fill has to stay on one side or the other.
	QImage image(200, 160, QImage::Format_RGB32);
	for (int y = 0; y < 160; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 200; ++x)
			line[x] = (x < 100) ? qRgb(30, 40, 200) : qRgb(230, 200, 40);
	}
	const QRect hole(70, 50, 60, 60);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	// Well to the left of the edge it must still be blue, well to the right
	// still yellow, and neither may have picked up the other.
	for (int y = hole.top() + 5; y <= hole.bottom() - 5; y += 7)
	{
		const QRgb left = result.pixel(78, y);
		const QRgb right = result.pixel(122, y);
		QVERIFY2(qBlue(left) > qRed(left) + 60,
		         qPrintable(QStringLiteral("left of the edge went muddy at y=%1: %2,%3,%4")
		                    .arg(y).arg(qRed(left)).arg(qGreen(left)).arg(qBlue(left))));
		QVERIFY2(qRed(right) > qBlue(right) + 60,
		         qPrintable(QStringLiteral("right of the edge went muddy at y=%1: %2,%3,%4")
		                    .arg(y).arg(qRed(right)).arg(qGreen(right)).arg(qBlue(right))));
	}

	// And the transition must still be quick rather than a wide gradient: at
	// most a few columns in the middle are neither one colour nor the other.
	int mixedColumns = 0;
	const int midY = hole.top() + hole.height() / 2;
	for (int x = hole.left(); x <= hole.right(); ++x)
	{
		const QRgb p = result.pixel(x, midY);
		const bool blue = qBlue(p) > qRed(p) + 60;
		const bool yellow = qRed(p) > qBlue(p) + 60;
		if (!blue && !yellow)
			++mixedColumns;
	}
	QVERIFY2(mixedColumns <= 8,
	         qPrintable(QStringLiteral("%1 columns of neither colour - the edge was smeared")
	                    .arg(mixedColumns)));
}

void InpaintTests::testDiagonalEdgeIsNotBlurredAway()
{
	// The same again on the diagonal, where an implementation that only looks
	// along the axes has an easier time cheating.
	QImage image(200, 200, QImage::Format_RGB32);
	for (int y = 0; y < 200; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 200; ++x)
			line[x] = (x > y) ? qRgb(20, 180, 60) : qRgb(200, 40, 140);
	}
	const QRect hole(70, 70, 60, 60);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	// Sample well clear of the diagonal on each side.
	for (int k = 0; k < 5; ++k)
	{
		const int y = hole.top() + 8 + k * 10;
		const QRgb above = result.pixel(y + 22, y);      // comfortably x > y
		const QRgb below = result.pixel(y - 22, y);      // comfortably x < y
		QVERIFY2(qGreen(above) > qRed(above) + 50,
		         qPrintable(QStringLiteral("above the diagonal went muddy at y=%1").arg(y)));
		QVERIFY2(qRed(below) > qGreen(below) + 50,
		         qPrintable(QStringLiteral("below the diagonal went muddy at y=%1").arg(y)));
	}
}

void InpaintTests::testColouredObjectsDoNotBleedIntoEachOther()
{
	// Saturated blocks side by side, and a hole entirely inside one of them.
	// A method that averages over a neighbourhood pulls its neighbours' colour
	// in; a method that copies a patch cannot.
	QImage image(240, 120, QImage::Format_RGB32);
	const QRgb colours[4] = { qRgb(220, 30, 30), qRgb(30, 200, 40),
	                          qRgb(40, 60, 220), qRgb(230, 210, 40) };
	quint32 state = 99u;
	for (int y = 0; y < 120; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 240; ++x)
		{
			const QRgb base = colours[(x / 60) % 4];
			const int n = int(nextRandom(state) % 30u) - 15;
			line[x] = qRgb(qBound(0, qRed(base) + n, 255),
			               qBound(0, qGreen(base) + n, 255),
			               qBound(0, qBlue(base) + n, 255));
		}
	}
	// Hole inside the green block, which runs x = 60..119.
	const QRect hole(72, 40, 34, 40);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	for (int y = hole.top(); y <= hole.bottom(); y += 6)
	{
		for (int x = hole.left(); x <= hole.right(); x += 6)
		{
			const QRgb p = result.pixel(x, y);
			QVERIFY2(qGreen(p) > qRed(p) + 60 && qGreen(p) > qBlue(p) + 60,
			         qPrintable(QStringLiteral("(%1,%2) is not green any more: %3,%4,%5")
			                    .arg(x).arg(y).arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p))));
		}
	}
}

void InpaintTests::testRepeatedPatternIsContinued()
{
	// A regular grid, which is the one thing an exemplar method should be
	// unambiguously good at: somewhere else in the picture there is always a
	// patch that fits exactly.
	const int period = 16;
	QImage image(240, 240, QImage::Format_RGB32);
	for (int y = 0; y < 240; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < 240; ++x)
		{
			const bool on = ((x % period) < 6) || ((y % period) < 6);
			line[x] = on ? qRgb(40, 40, 60) : qRgb(215, 210, 190);
		}
	}
	const QImage truth = image.copy();
	const QRect hole(90, 90, 50, 50);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	// Most of the fill should land on the right side of mid-grey for its
	// position in the pattern. Not all of it: the method is free to pick a
	// patch one period over, which is correct-looking but shifted.
	int agree = 0;
	int total = 0;
	for (int y = hole.top() + 4; y <= hole.bottom() - 4; ++y)
	{
		for (int x = hole.left() + 4; x <= hole.right() - 4; ++x)
		{
			const bool wantDark = qRed(truth.pixel(x, y)) < 128;
			const bool gotDark = qRed(result.pixel(x, y)) < 128;
			if (wantDark == gotDark)
				++agree;
			++total;
		}
	}
	const double fraction = total ? double(agree) / double(total) : 0.0;
	QVERIFY2(fraction > 0.75,
	         qPrintable(QStringLiteral("only %1 of the pattern came back in phase").arg(fraction)));

	// Whatever it chose, it must still be a pattern rather than a grey wash:
	// both extremes have to be present.
	int dark = 0;
	int light = 0;
	for (int y = hole.top() + 4; y <= hole.bottom() - 4; ++y)
	{
		for (int x = hole.left() + 4; x <= hole.right() - 4; ++x)
		{
			const int v = qRed(result.pixel(x, y));
			if (v < 90) ++dark;
			if (v > 180) ++light;
		}
	}
	QVERIFY2(dark > total / 10 && light > total / 10,
	         qPrintable(QStringLiteral("fill was washed out: %1 dark, %2 light of %3")
	                    .arg(dark).arg(light).arg(total)));
}

void InpaintTests::testTextLikeStructureDoesNotGoGrey()
{
	// Bars of "text" on paper. The failure this catches is the characteristic
	// one: black and white averaged into a uniform grey patch that no longer
	// reads as anything at all.
	QImage image(240, 160, QImage::Format_RGB32);
	image.fill(qRgb(240, 238, 230));
	for (int row = 0; row < 8; ++row)
	{
		const int top = 10 + row * 18;
		for (int y = top; y < top + 9; ++y)
		{
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int x = 12; x < 228; ++x)
			{
				// Word-like runs with gaps between them.
				if (((x / 7) % 5) != 4)
					line[x] = qRgb(25, 25, 30);
			}
		}
	}
	const QRect hole(90, 50, 50, 50);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	int dark = 0;
	int light = 0;
	int grey = 0;
	for (int y = hole.top() + 3; y <= hole.bottom() - 3; ++y)
	{
		for (int x = hole.left() + 3; x <= hole.right() - 3; ++x)
		{
			const int v = qRed(result.pixel(x, y));
			if (v < 80) ++dark;
			else if (v > 190) ++light;
			else ++grey;
		}
	}
	QVERIFY2(dark > 0 && light > 0,
	         qPrintable(QStringLiteral("the fill lost one of the two tones: %1 dark, %2 light")
	                    .arg(dark).arg(light)));
	QVERIFY2(grey < (dark + light),
	         qPrintable(QStringLiteral("%1 middling pixels against %2 that committed - it went grey")
	                    .arg(grey).arg(dark + light)));
}

void InpaintTests::testCirclesOnTextureKeepBackgroundClean()
{
	// Discs of solid colour on a textured ground, and a hole in the ground
	// between them. The fill has to look like ground, not like a smeared disc.
	QImage image = texturedField(240, 240, 70, 4242u);
	auto disc = [&image](int cx, int cy, int r, QRgb colour) {
		for (int y = cy - r; y <= cy + r; ++y)
			for (int x = cx - r; x <= cx + r; ++x)
			{
				if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
					continue;
				const int dx = x - cx;
				const int dy = y - cy;
				if (dx * dx + dy * dy <= r * r)
					image.setPixel(x, y, colour);
			}
	};
	disc(60, 60, 26, qRgb(230, 60, 60));
	disc(180, 70, 26, qRgb(60, 80, 230));
	disc(70, 180, 26, qRgb(240, 220, 60));
	disc(180, 180, 26, qRgb(40, 200, 90));

	// Between the discs, entirely on the textured ground.
	const QRect hole(105, 105, 34, 34);
	clobber(image, hole);

	QImage result = Inpaint::inpaint(image, rectMask(image.size(), hole),
	                                 withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!result.isNull());

	// None of the four disc colours may have been dragged into the gap: every
	// filled pixel should stay near the muted ground colour.
	for (int y = hole.top(); y <= hole.bottom(); y += 4)
	{
		for (int x = hole.left(); x <= hole.right(); x += 4)
		{
			const QRgb p = result.pixel(x, y);
			const int spread = qMax(qMax(qRed(p), qGreen(p)), qBlue(p)) -
			                   qMin(qMin(qRed(p), qGreen(p)), qBlue(p));
			QVERIFY2(spread < 110,
			         qPrintable(QStringLiteral("(%1,%2) picked up a disc colour: %3,%4,%5")
			                    .arg(x).arg(y).arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p))));
		}
	}
}

void InpaintTests::testBlurringScoresBetterOnPsnr()
{
	/* A guard rail rather than a feature test.

	   Tuning this kernel against PSNR is a trap, and one that was very nearly
	   walked into: on a textured photograph the fast marching method scores
	   *better* on PSNR than the exemplar method while looking obviously and
	   embarrassingly worse, because a smooth grey blob sits closer to the mean
	   of the truth than any honest texture does. If someone later "improves"
	   the kernel by chasing this number they will reintroduce the smear. This
	   test states the trap out loud so that it fails, loudly, if anyone
	   removes the texture measure and goes back to PSNR. */
	QImage image = texturedField(220, 220, 90);
	const QImage truth = image.copy();
	const QRect hole(80, 80, 60, 60);
	clobber(image, hole);
	QImage mask = rectMask(image.size(), hole);

	const QImage marched = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::FastMarching));
	const QImage copied = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::Exemplar));
	QVERIFY(!marched.isNull());
	QVERIFY(!copied.isNull());

	auto psnr = [&](const QImage& got) {
		double se = 0.0;
		long n = 0;
		for (int y = hole.top(); y <= hole.bottom(); ++y)
			for (int x = hole.left(); x <= hole.right(); ++x)
			{
				const QRgb a = truth.pixel(x, y);
				const QRgb b = got.pixel(x, y);
				se += std::pow(qRed(a) - qRed(b), 2) + std::pow(qGreen(a) - qGreen(b), 2) +
				      std::pow(qBlue(a) - qBlue(b), 2);
				n += 3;
			}
		const double mse = n ? se / double(n) : 0.0;
		return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
	};

	const QRect core(95, 95, 30, 30);
	const double reference = gradientEnergy(truth, QRect(30, 30, 40, 40));

	// The blurred answer wins on PSNR...
	QVERIFY2(psnr(marched) >= psnr(copied) - 0.01,
	         qPrintable(QStringLiteral("PSNR: marching %1, exemplar %2 - if the exemplar method "
	                                   "now wins on PSNR too, this guard rail can be retired")
	                    .arg(psnr(marched)).arg(psnr(copied))));
	// ...and loses badly on the thing a reader actually sees.
	QVERIFY(gradientEnergy(copied, core) > 2.0 * gradientEnergy(marched, core));
	QVERIFY(gradientEnergy(marched, core) < 0.5 * reference);
}

void InpaintTests::testCrowdLikeSceneKeepsItsTexture()
{
	/* The nearest thing to the case this was all reported against: a scene in
	   bands - sky, a hard horizon, a row of figures, textured ground - with a
	   figure-sized hole taken out of the middle of it.

	   Set SCRIBUS_INPAINT_TEST_OUT to a directory to have the before, the two
	   fills and the truth written there as PNGs to look at. Nothing in the
	   automated run depends on that; it is there because this is the case that
	   has to be judged by eye as well as measured. */
	QImage truth = crowdLikeScene(300, 240);
	QImage image = truth.copy();
	const QRect hole(120, 60, 60, 120);
	clobber(image, hole);
	QImage mask = rectMask(image.size(), hole);

	Inpaint::Method chosen = Inpaint::Method::FastMarching;
	Inpaint::Options autoOpts;
	autoOpts.chosenMethod = &chosen;

	const QImage automatic = Inpaint::inpaint(image, mask, autoOpts);
	const QImage marched = Inpaint::inpaint(image, mask, withMethod(Inpaint::Method::FastMarching));
	QVERIFY(!automatic.isNull());
	QVERIFY(!marched.isNull());

	// A wide hole in a busy scene is exactly what the exemplar method is for.
	QCOMPARE(chosen, Inpaint::Method::Exemplar);

	const QString outDir = qEnvironmentVariable("SCRIBUS_INPAINT_TEST_OUT");
	if (!outDir.isEmpty())
	{
		truth.save(outDir + QStringLiteral("/crowd_truth.png"));
		image.save(outDir + QStringLiteral("/crowd_holed.png"));
		marched.save(outDir + QStringLiteral("/crowd_fastmarching.png"));
		automatic.save(outDir + QStringLiteral("/crowd_exemplar.png"));
	}

	// Measured over the ground band inside the hole, which is where a
	// diffusion produces its most obvious smear.
	const QRect groundCore(130, 165, 40, 12);
	const double reference = gradientEnergy(truth, QRect(20, 165, 60, 12));
	const double marchedEnergy = gradientEnergy(marched, groundCore);
	const double autoEnergy = gradientEnergy(automatic, groundCore);

	QVERIFY2(reference > 15.0, "the test scene's ground is not textured enough to measure");
	QVERIFY2(autoEnergy > 2.0 * marchedEnergy,
	         qPrintable(QStringLiteral("ground texture: exemplar %1, fast marching %2, real %3")
	                    .arg(autoEnergy).arg(marchedEnergy).arg(reference)));

	// The horizon line has to survive as a line rather than dissolve into the
	// sky: a dark row should still be there, inside the hole, near where it
	// started.
	bool foundHorizon = false;
	for (int y = 76; y <= 88 && !foundHorizon; ++y)
	{
		int dark = 0;
		for (int x = hole.left() + 4; x <= hole.right() - 4; ++x)
			if (qRed(automatic.pixel(x, y)) < 110)
				++dark;
		if (dark > (hole.width() - 8) / 2)
			foundHorizon = true;
	}
	QVERIFY2(foundHorizon, "the horizon line did not survive the fill");
}

QTEST_APPLESS_MAIN(InpaintTests)
