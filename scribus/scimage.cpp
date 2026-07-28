/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <csetjmp>

#include <QByteArray>
#include <QFile>
#include <QImageReader>
#include <QMessageBox>
#include <QList>
#include <QScopedPointer>

#include "autocorrectengine.h"
#include "cmsettings.h"
#include "commonstrings.h"
#include "documentlogmanager.h"
#include "exif.h"
#include "rawimage.h"
#include "sccolorengine.h"
#include "scimagecacheproxy.h"
#include "scstreamfilter.h"
#include "scimage.h"
#include "scpaths.h"
#include "scribuscore.h"
#include "scstreamfilter_jpeg.h"
#include "sctextstream.h"
#include "util.h"
#include "util_color.h"
#include "util_formats.h"
#include "util_ghostscript.h"

#include "imagedataloaders/scimgdataloader_gimp.h"
#ifdef GMAGICK_FOUND
#include "imagedataloaders/scimgdataloader_gmagick.h"
#endif
#include "imagedataloaders/scimgdataloader_jpeg.h"
#ifdef HAVE_JXL
#include "imagedataloaders/scimgdataloader_jpegxl.h"
#endif
#include "imagedataloaders/scimgdataloader_ora.h"
#include "imagedataloaders/scimgdataloader_kra.h"
#include "imagedataloaders/scimgdataloader_pict.h"
#include "imagedataloaders/scimgdataloader_pdf.h"
#include "imagedataloaders/scimgdataloader_pgf.h"
#include "imagedataloaders/scimgdataloader_png.h"
#include "imagedataloaders/scimgdataloader_ps.h"
#include "imagedataloaders/scimgdataloader_psd.h"
#include "imagedataloaders/scimgdataloader_qt.h"
#include "imagedataloaders/scimgdataloader_tiff.h"
#include "imagedataloaders/scimgdataloader_wpg.h"


using namespace std;

ScImage::ScImage(const QImage & image) : QImage(image)
{
}


// ScImage will use implicit sharing:
ScImage::ScImage(const ScImage & image) : QImage(image.copy())
{
}

ScImage::ScImage( int width, int height ) : QImage( width, height, QImage::Format_ARGB32 )
{
}

const QImage& ScImage::qImage() const
{
	return *this;
}

QImage* ScImage::qImagePtr()
{
	return this;
}

QImage ScImage::scaled(int w, int h, Qt::AspectRatioMode mode, Qt::TransformationMode transformMode) const
{
	return QImage::scaled(w, h, mode, transformMode);
}

void ScImage::initialize()
{
	imgInfo.xres = 72;
	imgInfo.yres = 72;
	imgInfo.colorspace = ColorSpaceRGB;
	imgInfo.valid = false;
	imgInfo.isRequest = false;
	imgInfo.isEmbedded = false;
	imgInfo.progressive = false;
	imgInfo.exifDataValid = false;
	imgInfo.lowResType = 1;
	imgInfo.lowResScale = 1.0;
	imgInfo.PDSpathData.clear();
	imgInfo.RequestProps.clear();
	imgInfo.clipPath.clear();
	imgInfo.usedPath.clear();
	imgInfo.profileName.clear();
	imgInfo.embeddedProfileName.clear();
	imgInfo.layerInfo.clear();
	imgInfo.duotoneColors.clear();
	imgInfo.exifInfo.cameraName.clear();
	imgInfo.exifInfo.cameraVendor.clear();
	imgInfo.exifInfo.thumbnail = QImage();
	imgInfo.BBoxX = 0;
	imgInfo.BBoxH = 0;
}

ScImage::~ScImage()
{
}

void ScImage::applyEffect(const ScImageEffectList& effectsList, ColorList& colors, bool cmyk)
{
	if (effectsList.count() <= 0)
		return;
	ScribusDoc* doc = colors.document();

	for (int i = 0; i < effectsList.count(); ++i)
	{
		const ImageEffect& effect = effectsList.at(i);
		if (effect.effectCode == ImageEffect::EF_INVERT)
			invert(cmyk);
		if (effect.effectCode == ImageEffect::EF_GRAYSCALE)
			toGrayscale(cmyk);
		if (effect.effectCode == ImageEffect::EF_POSTERIZE)
		{
			QString tmpstr(effect.effectParameters);
			int levels = 4;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> levels;
			posterize(levels, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_LEVELS)
		{
			QString tmpstr(effect.effectParameters);
			int inBlack = 0, inWhite = 255, outBlack = 0, outWhite = 255;
			double gamma = 1.0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> inBlack;
			fp >> inWhite;
			fp >> gamma;
			fp >> outBlack;
			fp >> outWhite;
			levels(inBlack, inWhite, gamma, outBlack, outWhite, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_HUESAT)
		{
			QString tmpstr(effect.effectParameters);
			int hueShift = 0, satAdjust = 0, lightAdjust = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> hueShift;
			fp >> satAdjust;
			fp >> lightAdjust;
			hueSaturation(hueShift, satAdjust, lightAdjust, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_COLORBALANCE)
		{
			QString tmpstr(effect.effectParameters);
			int sr = 0, sg = 0, sb = 0, mr = 0, mg = 0, mb = 0, hr = 0, hg = 0, hb = 0, preserve = 1;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> sr; fp >> sg; fp >> sb;
			fp >> mr; fp >> mg; fp >> mb;
			fp >> hr; fp >> hg; fp >> hb;
			fp >> preserve;
			colorBalance(sr, sg, sb, mr, mg, mb, hr, hg, hb, preserve != 0, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_CMYKADJ)
		{
			QString tmpstr(effect.effectParameters);
			int cAdj = 0, mAdj = 0, yAdj = 0, kAdj = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> cAdj; fp >> mAdj; fp >> yAdj; fp >> kAdj;
			cmykAdjust(cAdj, mAdj, yAdj, kAdj, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_SELECTIVECOLOR)
		{
			QString tmpstr(effect.effectParameters);
			int adj[36];
			int relative = 1;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			for (int idx = 0; idx < 36; ++idx)
			{
				adj[idx] = 0;
				fp >> adj[idx];
			}
			fp >> relative;
			selectiveColor(adj, relative != 0, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_CHANNELMIXER)
		{
			QString tmpstr(effect.effectParameters);
			int mix[16];
			int monochrome = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			for (int idx = 0; idx < 16; ++idx)
			{
				mix[idx] = 0;
				fp >> mix[idx];
			}
			fp >> monochrome;
			channelMixer(mix, monochrome != 0, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_PHOTOFILTER)
		{
			QString tmpstr(effect.effectParameters);
			int fr = 236, fg = 138, fb = 0, density = 25, preserve = 1;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> fr; fp >> fg; fp >> fb; fp >> density; fp >> preserve;
			photoFilter(fr, fg, fb, density, preserve != 0, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_THRESHOLD)
		{
			QString tmpstr(effect.effectParameters);
			int level = 128;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> level;
			threshold(level, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_BLACKWHITE)
		{
			QString tmpstr(effect.effectParameters);
			int wgt[6] = { 40, 60, 40, 60, 20, 80 };
			int tint = 0, tr = 225, tg = 211, tb = 179;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			for (int idx = 0; idx < 6; ++idx)
				fp >> wgt[idx];
			fp >> tint;
			fp >> tr; fp >> tg; fp >> tb;
			blackWhite(wgt, tint != 0, tr, tg, tb, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_MOTIONBLUR)
		{
			QString tmpstr(effect.effectParameters);
			int angle = 0, distance = 10;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> angle; fp >> distance;
			motionBlur(angle, distance, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_RADIALBLUR)
		{
			QString tmpstr(effect.effectParameters);
			int amount = 10, mode = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> amount; fp >> mode;
			radialBlur(amount, mode, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_BOXBLUR)
		{
			QString tmpstr(effect.effectParameters);
			int radius = 3;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> radius;
			boxBlur(radius, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_AUTOTONE)
		{
			setFromRGBImage(AutoCorrectEngine::autoTone(asRGBImage(cmyk)), cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_AUTOCONTRAST)
		{
			setFromRGBImage(AutoCorrectEngine::autoContrast(asRGBImage(cmyk)), cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_AUTOCOLOR)
		{
			setFromRGBImage(AutoCorrectEngine::autoColor(asRGBImage(cmyk)), cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_AUTOENHANCE)
		{
			AutoCorrectOptions opts = AutoCorrectEngine::parseEnhance(effect.effectParameters);
			setFromRGBImage(AutoCorrectEngine::autoEnhanceCombined(asRGBImage(cmyk), opts), cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_AUTOCMYK)
		{
			CmykOptimizeOptions opts = AutoCorrectEngine::parseCmyk(effect.effectParameters);
			setFromRGBImage(AutoCorrectEngine::autoCmykOptimize(asRGBImage(cmyk), opts), cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_SHADOWHIGHLIGHT)
		{
			QString tmpstr(effect.effectParameters);
			int sAmt = 35, sTone = 50, hAmt = 0, hTone = 50, radius = 30, color = 20, midtone = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> sAmt; fp >> sTone; fp >> hAmt; fp >> hTone; fp >> radius; fp >> color; fp >> midtone;
			shadowsHighlights(sAmt, sTone, hAmt, hTone, radius, color, midtone, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_COLORIZE)
		{
			QString tmpstr(effect.effectParameters);
			QString col(CommonStrings::None);
			int shading = 100;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
		//	fp >> col;
			col = fp.readLine();
			fp >> shading;
			colorize(doc, colors[col], shading, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_BRIGHTNESS)
		{
			QString tmpstr(effect.effectParameters);
			int brightnessValue = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> brightnessValue;
			brightness(brightnessValue, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_CONTRAST)
		{
			QString tmpstr(effect.effectParameters);
			int contrastValue = 0;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> contrastValue;
			contrast(contrastValue, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_SHARPEN)
		{
			QString tmpstr(effect.effectParameters);
			double radius, sigma;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> radius;
			fp >> sigma;
			sharpen(radius, sigma);
		}
		if (effect.effectCode == ImageEffect::EF_BLUR)
		{
			QString tmpstr(effect.effectParameters);
			double radius, sigma;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> radius;
			fp >> sigma;
			blur(static_cast<int>(radius));
		}
		if (effect.effectCode == ImageEffect::EF_SOLARIZE)
		{
			QString tmpstr(effect.effectParameters);
			double sigma;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> sigma;
			solarize(sigma, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_DUOTONE)
		{
			QString tmpstr(effect.effectParameters);
			QString col1(CommonStrings::None);
			QString col2(CommonStrings::None);
			int shading1 = 100;
			int shading2 = 100;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			col1 = fp.readLine();
			col2 = fp.readLine();
			fp >> shading1;
			fp >> shading2;
			int numVals;
			double xval, yval;
			FPointArray curve1;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve1.addPoint(xval, yval);
			}
			int lin1;
			fp >> lin1;
			FPointArray curve2;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve2.addPoint(xval, yval);
			}
			int lin2;
			fp >> lin2;
			duotone(doc, colors[col1], shading1, curve1, lin1, colors[col2], shading2, curve2, lin2, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_TRITONE)
		{
			QString tmpstr(effect.effectParameters);
			QString col1(CommonStrings::None);
			QString col2(CommonStrings::None);
			QString col3(CommonStrings::None);
			int shading1 = 100;
			int shading2 = 100;
			int shading3 = 100;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			col1 = fp.readLine();
			col2 = fp.readLine();
			col3 = fp.readLine();
			fp >> shading1;
			fp >> shading2;
			fp >> shading3;
			int numVals;
			double xval, yval;
			FPointArray curve1;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve1.addPoint(xval, yval);
			}
			int lin1;
			fp >> lin1;
			FPointArray curve2;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve2.addPoint(xval, yval);
			}
			int lin2;
			fp >> lin2;
			FPointArray curve3;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve3.addPoint(xval, yval);
			}
			int lin3;
			fp >> lin3;
			tritone(doc, colors[col1], shading1, curve1, lin1, colors[col2], shading2, curve2, lin2, colors[col3], shading3, curve3, lin3, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_QUADTONE)
		{
			QString tmpstr(effect.effectParameters);
			QString col1(CommonStrings::None);
			QString col2(CommonStrings::None);
			QString col3(CommonStrings::None);
			QString col4(CommonStrings::None);
			int shading1 = 100;
			int shading2 = 100;
			int shading3 = 100;
			int shading4 = 100;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			col1 = fp.readLine();
			col2 = fp.readLine();
			col3 = fp.readLine();
			col4 = fp.readLine();
			fp >> shading1;
			fp >> shading2;
			fp >> shading3;
			fp >> shading4;
			int numVals;
			double xval, yval;
			FPointArray curve1;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve1.addPoint(xval, yval);
			}
			int lin1;
			fp >> lin1;
			FPointArray curve2;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve2.addPoint(xval, yval);
			}
			int lin2;
			fp >> lin2;
			FPointArray curve3;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve3.addPoint(xval, yval);
			}
			int lin3;
			fp >> lin3;
			FPointArray curve4;
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve4.addPoint(xval, yval);
			}
			int lin4;
			fp >> lin4;
			quadtone(doc, colors[col1], shading1, curve1, lin1, colors[col2], shading2, curve2, lin2, colors[col3], shading3, curve3, lin3, colors[col4], shading4, curve4, lin4, cmyk);
		}
		if (effect.effectCode == ImageEffect::EF_GRADUATE)
		{
			QString tmpstr(effect.effectParameters);
			int numVals;
			double xval, yval;
			FPointArray curve;
			ScTextStream fp(&tmpstr, QIODevice::ReadOnly);
			fp >> numVals;
			for (int nv = 0; nv < numVals; nv++)
			{
				fp >> xval;
				fp >> yval;
				curve.addPoint(xval, yval);
			}
			int lin;
			fp >> lin;
			doGraduate(curve, cmyk, lin);
		}
	}
}

void ScImage::solarize(double factor, bool cmyk)
{
	QVector<int> curveTable(256);
	int fk = qRound(255 / factor);
	for (int i = 0; i < 256; ++i)
	{
		curveTable[i] = qMin(255, static_cast<int>(i / fk) * fk);
	}
	applyCurve(curveTable, cmyk);
}

// Stack Blur Algorithm by Mario Klingemann <mario@quasimondo.com>
void ScImage::blur(int radius)
{
	if (radius < 1) {
		return;
	}

	QRgb *pix = (QRgb*) bits();
	int w   = width();
	int h   = height();
	int wm  = w - 1;
	int hm  = h - 1;
	int wh  = w * h;
	int div = radius + radius + 1;

	int *r = new int[wh];
	int *g = new int[wh];
	int *b = new int[wh];
	int *a = new int[wh];
	int rsum, gsum, bsum, asum, x, y, i, yp, yi, yw;
	QRgb p;
	int *vmin = new int[qMax(w, h)];

	int divsum = (div + 1) >> 1;
	divsum *= divsum;
	int *dv = new int[256*divsum];
	for (i = 0; i < 256 * divsum; ++i) {
		dv[i] = (i / divsum);
	}

	yw = yi = 0;

	int **stack = new int*[div];
	for (int i = 0; i < div; ++i) {
		stack[i] = new int[4];
	}

	int stackpointer;
	int stackstart;
	int *sir;
	int rbs;
	int r1 = radius + 1;
	int routsum, goutsum, boutsum, aoutsum;
	int rinsum, ginsum, binsum, ainsum;

	for (y = 0; y < h; ++y)
	{
		rinsum = ginsum = binsum = ainsum
			= routsum = goutsum = boutsum = aoutsum
			= rsum = gsum = bsum = asum = 0;
		for (i = -radius; i <= radius; ++i)
		{
			p = pix[yi + qMin(wm, qMax(i, 0))];
			sir = stack[i + radius];
			sir[0] = qRed(p);
			sir[1] = qGreen(p);
			sir[2] = qBlue(p);
			sir[3] = qAlpha(p);

			rbs = r1 - abs(i);
			rsum += sir[0] * rbs;
			gsum += sir[1] * rbs;
			bsum += sir[2] * rbs;
			asum += sir[3] * rbs;

			if (i > 0)
			{
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
				ainsum += sir[3];
			}
			else
			{
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
				aoutsum += sir[3];
			}
		}
		stackpointer = radius;

		for (x=0; x < w; ++x)
		{
			r[yi] = dv[rsum];
			g[yi] = dv[gsum];
			b[yi] = dv[bsum];
			a[yi] = dv[asum];

			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;
			asum -= aoutsum;

			stackstart = stackpointer - radius + div;
			sir = stack[stackstart % div];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];
			aoutsum -= sir[3];

			if (y == 0)
			{
				vmin[x] = qMin(x + radius + 1, wm);
			}
			p = pix[yw + vmin[x]];

			sir[0] = qRed(p);
			sir[1] = qGreen(p);
			sir[2] = qBlue(p);
			sir[3] = qAlpha(p);

			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];
			ainsum += sir[3];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			asum += ainsum;

			stackpointer = (stackpointer+1)%div;
			sir = stack[(stackpointer)%div];

			routsum += sir[0];
			goutsum += sir[1];
			boutsum += sir[2];
			aoutsum += sir[3];

			rinsum -= sir[0];
			ginsum -= sir[1];
			binsum -= sir[2];
			ainsum -= sir[3];

			++yi;
		}
		yw += w;
	}
	for (x=0; x < w; ++x)
	{
		rinsum = ginsum = binsum = ainsum
			= routsum = goutsum = boutsum = aoutsum
			= rsum = gsum = bsum = asum = 0;

		yp =- radius * w;

		for (i=-radius; i <= radius; ++i)
		{
			yi = qMax(0, yp) + x;

			sir = stack[i + radius];

			sir[0] = r[yi];
			sir[1] = g[yi];
			sir[2] = b[yi];
			sir[3] = a[yi];

			rbs = r1 - abs(i);

			rsum += r[yi]*rbs;
			gsum += g[yi]*rbs;
			bsum += b[yi]*rbs;
			asum += a[yi]*rbs;

			if (i > 0)
			{
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
				ainsum += sir[3];
			}
			else
			{
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
				aoutsum += sir[3];
			}

			if (i < hm)
			{
				yp += w;
			}
		}

		yi = x;
		stackpointer = radius;

		for (y=0; y < h; ++y)
		{
			pix[yi] = qRgba(dv[rsum], dv[gsum], dv[bsum], dv[asum]);

			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;
			asum -= aoutsum;

			stackstart = stackpointer-radius+div;
			sir = stack[stackstart%div];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];
			aoutsum -= sir[3];

			if (x==0)
			{
				vmin[y] = qMin(y + r1,hm)*w;
			}
			p = x + vmin[y];

			sir[0] = r[p];
			sir[1] = g[p];
			sir[2] = b[p];
			sir[3] = a[p];

			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];
			ainsum += sir[3];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			asum += ainsum;

			stackpointer = (stackpointer + 1) % div;
			sir = stack[stackpointer];

			routsum += sir[0];
			goutsum += sir[1];
			boutsum += sir[2];
			aoutsum += sir[3];

			rinsum -= sir[0];
			ginsum -= sir[1];
			binsum -= sir[2];
			ainsum -= sir[3];

			yi += w;
		}
	}
	delete [] r;
	delete [] g;
	delete [] b;
	delete [] a;
	delete [] vmin;
	delete [] dv;

	for (int i = 0; i < div; ++i)
	{
		delete [] stack[i];
	}
	delete [] stack;
}

bool ScImage::convolveImage(QImage *dest, const unsigned int order, const double *kernel)
{
	double red, green, blue, alpha;
	const double *k;
	unsigned int *q;
	int x, y, mx, my, sx, sy;
	long i;
	int mcx, mcy;
	long widthk = order;
	if ((widthk % 2) == 0)
		return false;
	double *normal_kernel = (double *)malloc(widthk*widthk*sizeof(double));
	if (!normal_kernel)
		return false;
	*dest = QImage(width(), height(), QImage::Format_ARGB32);
	double normalize = 0.0;
	for (i=0; i < (widthk * widthk); i++)
		normalize += kernel[i];
	if (fabs(normalize) <= 1.0e-12)
		normalize = 1.0;
	normalize = 1.0 / normalize;
	for (i = 0; i < (widthk * widthk); i++)
		normal_kernel[i] = normalize*kernel[i];
	for (y = 0; y < dest->height(); ++y)
	{
		sy = y - (widthk / 2);
		q = (unsigned int *) dest->scanLine(y);
		for (x = 0; x < dest->width(); ++x)
		{
			k = normal_kernel;
			red = green = blue = alpha = 0;
			sy = y - (widthk / 2);
			for (mcy = 0; mcy < widthk; ++mcy, ++sy)
			{
				my = sy < 0 ? 0 : sy > height() - 1 ? height() - 1 : sy;
				sx = x + (-widthk / 2);
				for (mcx=0; mcx < widthk; ++mcx, ++sx)
				{
					mx = sx < 0 ? 0 : sx > width()-1 ? width()-1 : sx;
					int px = pixel(mx, my);
					red += (*k)*(qRed(px) * 257);
					green += (*k)*(qGreen(px) * 257);
					blue += (*k)*(qBlue(px) * 257);
					alpha += (*k)*(qAlpha(px) * 257);
					++k;
				}
			}
			red = red < 0 ? 0 : red > 65535 ? 65535 : red + 0.5;
			green = green < 0 ? 0 : green > 65535 ? 65535 : green + 0.5;
			blue = blue < 0 ? 0 : blue > 65535 ? 65535 : blue + 0.5;
			alpha = alpha < 0 ? 0 : alpha > 65535 ? 65535 : alpha + 0.5;
			*q++ = qRgba((unsigned char)(red / 257UL),
			             (unsigned char)(green / 257UL),
			             (unsigned char)(blue / 257UL),
			             (unsigned char)(alpha / 257UL));
		}
	}
	free(normal_kernel);
	return(true);
}

int ScImage::getOptimalKernelWidth(double radius, double sigma)
{
	double normalize, value;
	long width;
	long u;
	assert(sigma != 0.0);
	if (radius > 0.0)
		return((int)(2.0 * ceil(radius) + 1.0));
	for (width = 5; ;)
	{
		normalize = 0.0;
		for (u= (-width / 2); u <= (width / 2); u++)
			normalize += exp(-((double) u * u) / (2.0 * sigma * sigma)) / (2.50662827463100024161235523934010416269302368164062 * sigma);
		u = width / 2;
		value = exp(-((double) u*u) / (2.0 * sigma * sigma)) / (2.50662827463100024161235523934010416269302368164062 * sigma) / normalize;
		if ((long)(65535 * value) <= 0)
			break;
		width += 2;
	}
	return ((int) width - 2);
}

void ScImage::sharpen(double radius, double sigma)
{
	long u, v;
	if (sigma == 0.0)
		return;

	int widthk = getOptimalKernelWidth(radius, sigma);
	if ((widthk <= 0) || (width() < widthk))
		return;

	double *kernel = (double *) malloc(widthk * widthk * sizeof(double));
	if (!kernel)
		return;

	long i = 0;
	double alpha;
	double normalize = 0.0;
	for (v= (-widthk / 2); v <= (widthk / 2); v++)
	{
		for (u= (-widthk / 2); u <= (widthk / 2); u++)
		{
			alpha = exp(-((double) u * u + v * v) / (2.0 * sigma * sigma));
			kernel[i] = alpha / (2.0 * 3.14159265358979323846264338327950288419716939937510 * sigma * sigma);
			normalize += kernel[i];
			i++;
		}
	}
	kernel[i / 2] = (-2.0) * normalize;

	QImage dest;
	convolveImage(&dest, widthk, kernel);
	free(kernel);

	for (int yi = 0; yi < dest.height(); ++yi)
	{
		QRgb *s = (QRgb*) dest.scanLine(yi);
		QRgb *d = (QRgb*) scanLine(yi);
		for (int xi = 0; xi < dest.width(); ++xi)
		{
			(*d) = (*s);
			s++;
			d++;
		}
	}
}

void ScImage::contrast(int contrastValue, bool cmyk)
{
	QVector<int> curveTable(256);
	QPoint p1(0,0 - contrastValue);
	QPoint p2(256, 256 + contrastValue);
	double mc = (p1.y() - p2.y()) / (double)(p1.x() - p2.x());
	for (int i = 0; i < 256; ++i)
	{
		curveTable[i] = qMin(255, qMax(0, int(i * mc) + p1.y()));
	}
	applyCurve(curveTable, cmyk);
}

void ScImage::brightness(int brightnessValue, bool cmyk)
{
	QVector<int> curveTable(256);
	QPoint p1(0,0 + brightnessValue);
	QPoint p2(256, 256 + brightnessValue);
	double mc = (p1.y() - p2.y()) / (double)(p1.x() - p2.x());
	for (int i = 0; i < 256; ++i)
	{
		curveTable[i] = qMin(255, qMax(0, int(i * mc) + p1.y()));
	}
	applyCurve(curveTable, cmyk);
}

void ScImage::doGraduate(FPointArray curve, bool cmyk, bool linear)
{
	QVector<int> curveTable(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve, x / 255.0, linear) * 255)));
	}
	applyCurve(curveTable, cmyk);
}

void ScImage::applyCurve(const QVector<int>& curveTable, bool cmyk)
{
	int h = height();
	int w = width();
	QRgb *s;
	QRgb r;
	int c, m, y, k;
	unsigned char *p;
	for (int yi=0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine( yi ));
		for (int xi=0; xi < w; ++xi)
		{
			r = *s;
			if (cmyk)
			{
				p = (unsigned char *) s;
				p[0] = 255 - curveTable[255 - p[0]];
				p[1] = 255 - curveTable[255 - p[1]];
				p[2] = 255 - curveTable[255 - p[2]];
				p[3] = 255 - curveTable[255 - p[3]];
			}
			else
			{
				c = curveTable[qRed(r)];
				m = curveTable[qGreen(r)];
				y = curveTable[qBlue(r)];
				k = qAlpha(r);
				*s = qRgba(c, m, y, k);
			}
			s++;
		}
	}
}

void ScImage::colorize(ScribusDoc* doc, ScColor color, int shade, bool cmyk)
{
	int h = height();
	int w = width();
	int cc, cm, cy, ck;
	int hu, sa, v;
	ScColor tmp2;
	QColor tmpR;
	QRgb *s;
	QRgb r;
	double k;
	int cc2, cm2, cy2, k2;
	if (cmyk)
	{
		CMYKColor cmykCol;
		ScColorEngine::getShadeColorCMYK(color, doc, cmykCol, shade);
		cmykCol.getValues(cc, cm, cy, ck);
	}
	else
	{
		ck = 0;
		RGBColor rgbCol;
		ScColorEngine::getShadeColorRGB(color, doc, rgbCol, shade);
		rgbCol.getValues(cc, cm, cy);
	}
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s;
			if (cmyk)
			{
				k = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r) + qAlpha(r)), 255) / 255.0;
				*s = qRgba(qMin(qRound(cc*k), 255), qMin(qRound(cm*k), 255), qMin(qRound(cy*k), 255), qMin(qRound(ck*k), 255));
			}
			else
			{
				k2 = 255 - qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
				tmpR.setRgb(cc, cm, cy);
				tmpR.getHsv(&hu, &sa, &v);
				tmpR.setHsv(hu, sa * k2 / 255, 255 - ((255 - v) * k2 / 255));
				tmpR.getRgb(&cc2, &cm2, &cy2);
				*s = qRgba(cc2, cm2, cy2, qAlpha(r));
			}
			s++;
		}
	}
}

void ScImage::duotone(ScribusDoc* doc, ScColor color1, int shade1, FPointArray curve1, bool lin1, ScColor color2, int shade2, FPointArray curve2, bool lin2, bool cmyk)
{
	int h = height();
	int w = width();
	int c, c1, m, m1, y, y1, k, k1;
	int cn, c1n, mn, m1n, yn, y1n, kn, k1n;
	uchar cb;
	QVector<int> curveTable1;
	QVector<int> curveTable2;
	CMYKColor cmykCol;
	ScColorEngine::getShadeColorCMYK(color1, doc, cmykCol, shade1);
	cmykCol.getValues(c, m, y, k);
	ScColorEngine::getShadeColorCMYK(color2, doc, cmykCol, shade2);
	cmykCol.getValues(c1, m1, y1, k1);
	curveTable1.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable1[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve1, x / 255.0, lin1) * 255)));
	}
	curveTable2.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable2[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve2, x / 255.0, lin2) * 255)));
	}
	for (int yi=0; yi < h; ++yi)
	{
		QRgb * s = (QRgb*)(scanLine( yi ));
		for (int xi=0; xi < w; ++xi)
		{
			QRgb r = *s;
			if (cmyk)
				cb = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r) + qAlpha(r)), 255);
			else
				cb = 255 - qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
			cn = qMin((c * curveTable1[(int)cb]) >> 8, 255);
			mn = qMin((m * curveTable1[(int)cb]) >> 8, 255);
			yn = qMin((y * curveTable1[(int)cb]) >> 8, 255);
			kn = qMin((k * curveTable1[(int)cb]) >> 8, 255);
			c1n = qMin((c1 * curveTable1[(int)cb]) >> 8, 255);
			m1n = qMin((m1 * curveTable2[(int)cb]) >> 8, 255);
			y1n = qMin((y1 * curveTable2[(int)cb]) >> 8, 255);
			k1n = qMin((k1 * curveTable2[(int)cb]) >> 8, 255);
			ScColor col = ScColor(qMin(cn + c1n, 255), qMin(mn + m1n, 255), qMin(yn + y1n, 255), qMin(kn + k1n, 255));
			if (cmyk)
				col.getCMYK(&cn, &mn, &yn, &kn);
			else
			{
				col.getRawRGBColor(&cn, &mn, &yn);
				kn = qAlpha(r);
			}
			*s = qRgba(cn, mn, yn, kn);
			s++;
		}
	}
}

void ScImage::tritone(ScribusDoc* doc, ScColor color1, int shade1, FPointArray curve1, bool lin1, ScColor color2, int shade2, FPointArray curve2, bool lin2, ScColor color3, int shade3, const FPointArray& curve3, bool lin3, bool cmyk)
{
	int h = height();
	int w = width();
	int c, c1, c2, m, m1, m2, y, y1, y2, k, k1, k2;
	int cn, c1n, c2n, mn, m1n, m2n, yn, y1n, y2n, kn, k1n, k2n;
	uchar cb;
	CMYKColor cmykCol;
	QVector<int> curveTable1;
	QVector<int> curveTable2;
	QVector<int> curveTable3;
	ScColorEngine::getShadeColorCMYK(color1, doc, cmykCol, shade1);
	cmykCol.getValues(c, m, y, k);
	ScColorEngine::getShadeColorCMYK(color2, doc, cmykCol, shade2);
	cmykCol.getValues(c1, m1, y1, k1);
	ScColorEngine::getShadeColorCMYK(color3, doc, cmykCol, shade3);
	cmykCol.getValues(c2, m2, y2, k2);
	curveTable1.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable1[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve1, x / 255.0, lin1) * 255)));
	}
	curveTable2.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable2[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve2, x / 255.0, lin2) * 255)));
	}
	curveTable3.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable3[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve2, x / 255.0, lin3) * 255)));
	}
	for (int yi = 0; yi < h; ++yi)
	{
		QRgb * s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			QRgb r = *s;
			if (cmyk)
				cb = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r) + qAlpha(r)), 255);
			else
				cb = 255 - qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
			cn = qMin((c * curveTable1[(int)cb]) >> 8, 255);
			mn = qMin((m * curveTable1[(int)cb]) >> 8, 255);
			yn = qMin((y * curveTable1[(int)cb]) >> 8, 255);
			kn = qMin((k * curveTable1[(int)cb]) >> 8, 255);
			c1n = qMin((c1 * curveTable2[(int)cb]) >> 8, 255);
			m1n = qMin((m1 * curveTable2[(int)cb]) >> 8, 255);
			y1n = qMin((y1 * curveTable2[(int)cb]) >> 8, 255);
			k1n = qMin((k1 * curveTable2[(int)cb]) >> 8, 255);
			c2n = qMin((c2 * curveTable3[(int)cb]) >> 8, 255);
			m2n = qMin((m2 * curveTable3[(int)cb]) >> 8, 255);
			y2n = qMin((y2 * curveTable3[(int)cb]) >> 8, 255);
			k2n = qMin((k2 * curveTable3[(int)cb]) >> 8, 255);
			ScColor col = ScColor(qMin(cn+c1n+c2n, 255), qMin(mn+m1n+m2n, 255), qMin(yn+y1n+y2n, 255), qMin(kn+k1n+k2n, 255));
			if (cmyk)
				col.getCMYK(&cn, &mn, &yn, &kn);
			else
			{
				col.getRawRGBColor(&cn, &mn, &yn);
				kn = qAlpha(r);
			}
			*s = qRgba(cn, mn, yn, kn);
			s++;
		}
	}
}

void ScImage::quadtone(ScribusDoc* doc, ScColor color1, int shade1, FPointArray curve1, bool lin1, ScColor color2, int shade2, FPointArray curve2, bool lin2, ScColor color3, int shade3, FPointArray curve3, bool lin3, ScColor color4, int shade4, FPointArray curve4, bool lin4, bool cmyk)
{
	int h = height();
	int w = width();
	int c, c1, c2, c3, m, m1, m2, m3, y, y1, y2, y3, k, k1, k2, k3;
	int cn, c1n, c2n, c3n, mn, m1n, m2n, m3n, yn, y1n, y2n, y3n, kn, k1n, k2n, k3n;
	uchar cb;
	CMYKColor cmykCol;
	QVector<int> curveTable1;
	QVector<int> curveTable2;
	QVector<int> curveTable3;
	QVector<int> curveTable4;
	ScColorEngine::getShadeColorCMYK(color1, doc, cmykCol, shade1);
	cmykCol.getValues(c, m, y, k);
	ScColorEngine::getShadeColorCMYK(color2, doc, cmykCol, shade2);
	cmykCol.getValues(c1, m1, y1, k1);
	ScColorEngine::getShadeColorCMYK(color3, doc, cmykCol, shade3);
	cmykCol.getValues(c2, m2, y2, k2);
	ScColorEngine::getShadeColorCMYK(color4, doc, cmykCol, shade4);
	cmykCol.getValues(c3, m3, y3, k3);
	curveTable1.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable1[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve1, x / 255.0, lin1) * 255)));
	}
	curveTable2.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable2[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve2, x / 255.0, lin2) * 255)));
	}
	curveTable3.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable3[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve3, x / 255.0, lin3) * 255)));
	}
	curveTable4.resize(256);
	for (int x = 0 ; x < 256 ; x++)
	{
		curveTable4[x] = qMin(255, qMax(0, qRound(getCurveYValue(curve4, x / 255.0, lin4) * 255)));
	}
	for (int yi=0; yi < h; ++yi)
	{
		QRgb * s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			QRgb r = *s;
			if (cmyk)
				cb = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r) + qAlpha(r)), 255);
			else
				cb = 255 - qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
			cn = qMin((c * curveTable1[(int)cb]) >> 8, 255);
			mn = qMin((m * curveTable1[(int)cb]) >> 8, 255);
			yn = qMin((y * curveTable1[(int)cb]) >> 8, 255);
			kn = qMin((k * curveTable1[(int)cb]) >> 8, 255);
			c1n = qMin((c1 * curveTable2[(int)cb]) >> 8, 255);
			m1n = qMin((m1 * curveTable2[(int)cb]) >> 8, 255);
			y1n = qMin((y1 * curveTable2[(int)cb]) >> 8, 255);
			k1n = qMin((k1 * curveTable2[(int)cb]) >> 8, 255);
			c2n = qMin((c2 * curveTable3[(int)cb]) >> 8, 255);
			m2n = qMin((m2 * curveTable3[(int)cb]) >> 8, 255);
			y2n = qMin((y2 * curveTable3[(int)cb]) >> 8, 255);
			k2n = qMin((k2 * curveTable3[(int)cb]) >> 8, 255);
			c3n = qMin((c3 * curveTable4[(int)cb]) >> 8, 255);
			m3n = qMin((m3 * curveTable4[(int)cb]) >> 8, 255);
			y3n = qMin((y3 * curveTable4[(int)cb]) >> 8, 255);
			k3n = qMin((k3 * curveTable4[(int)cb]) >> 8, 255);
			ScColor col = ScColor(qMin(cn+c1n+c2n+c3n, 255), qMin(mn+m1n+m2n+m3n, 255), qMin(yn+y1n+y2n+y3n, 255), qMin(kn+k1n+k2n+k3n, 255));
			if (cmyk)
				col.getCMYK(&cn, &mn, &yn, &kn);
			else
			{
				col.getRawRGBColor(&cn, &mn, &yn);
				kn = qAlpha(r);
			}
			*s = qRgba(cn, mn, yn, kn);
			s++;
		}
	}
}

void ScImage::invert(bool cmyk)
{
	int h = height();
	int w = width();
	unsigned char *p;
	QRgb * s;
	unsigned char c, m, y, k;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			if (cmyk)
			{
				p = (unsigned char *) s;
				c = 255 - qMin(255, p[0] + p[3]);
				m = 255 - qMin(255, p[1] + p[3]);
				y = 255 - qMin(255, p[2] + p[3]);
				k = qMin(qMin(c, m), y);
				p[0] = c - k;
				p[1] = m - k;
				p[2] = y - k;
				p[3] = k;
			}
			else
				*s ^= 0x00ffffff;
			s++;
		}
	}
}

void ScImage::posterize(int levels, bool cmyk)
{
	if (levels < 2)
		levels = 2;
	if (levels > 255)
		levels = 255;
	// Lookup table: quantize each 0..255 channel value to `levels` steps.
	unsigned char lut[256];
	for (int i = 0; i < 256; ++i)
	{
		int step = qRound((i * (levels - 1)) / 255.0);
		int v = qRound(step * 255.0 / (levels - 1));
		lut[i] = static_cast<unsigned char>(qBound(0, v, 255));
	}
	int h = height();
	int w = width();
	unsigned char *p;
	QRgb *s;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			p = (unsigned char *) s;
			p[0] = lut[p[0]];
			p[1] = lut[p[1]];
			p[2] = lut[p[2]];
			if (cmyk)
				p[3] = lut[p[3]];
			s++;
		}
	}
}

void ScImage::levels(int inBlack, int inWhite, double gamma, int outBlack, int outWhite, bool cmyk)
{
	inBlack  = qBound(0, inBlack, 254);
	inWhite  = qBound(inBlack + 1, inWhite, 255);
	outBlack = qBound(0, outBlack, 255);
	outWhite = qBound(0, outWhite, 255);
	if (gamma < 0.01)
		gamma = 0.01;
	const double invGamma = 1.0 / gamma;
	const double inRange = static_cast<double>(inWhite - inBlack);
	QVector<int> curveTable(256);
	for (int i = 0; i < 256; ++i)
	{
		double t = (i - inBlack) / inRange;
		t = qBound(0.0, t, 1.0);
		t = std::pow(t, invGamma);
		int out = qRound(outBlack + t * (outWhite - outBlack));
		curveTable[i] = qBound(0, out, 255);
	}
	applyCurve(curveTable, cmyk);
}

void ScImage::hueSaturation(int hueShift, int satAdjust, int lightAdjust, bool cmyk)
{
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				// Convert the stored CMYK back to RGB, adjust, then re-encode.
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			QColor col(r, g, b);
			int hh, ss, ll;
			col.getHsl(&hh, &ss, &ll);
			const bool achromatic = (hh < 0 || ss == 0);
			if (!achromatic)
			{
				hh = (hh + hueShift) % 360;
				if (hh < 0)
					hh += 360;
				ss = qBound(0, ss + satAdjust * 255 / 100, 255);
			}
			ll = qBound(0, ll + lightAdjust * 255 / 100, 255);
			QColor out = achromatic ? QColor::fromHsl(0, 0, ll) : QColor::fromHsl(hh, ss, ll);
			int nr = out.red(), ng = out.green(), nb = out.blue();

			if (cmyk)
			{
				p = (unsigned char *) s;
				int nk = 255 - qMax(nr, qMax(ng, nb));
				if (nk >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - nr - nk) * 255 / (255 - nk);
					p[1] = (255 - ng - nk) * 255 / (255 - nk);
					p[2] = (255 - nb - nk) * 255 / (255 - nk);
					p[3] = nk;
				}
			}
			else
			{
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::colorBalance(int sr, int sg, int sb, int mr, int mg, int mb, int hr, int hg, int hb, bool preserveLum, bool cmyk)
{
	// Build a per-channel lookup table from smooth shadow/midtone/highlight
	// tonal weights. The three sliders for a channel are fixed, so each channel
	// collapses to a 256-entry LUT.
	auto buildLut = [](double shadow, double mid, double high, int lut[256])
	{
		for (int i = 0; i < 256; ++i)
		{
			double v = i / 255.0;
			double wS = std::exp(-(v * v) / (2.0 * 0.30 * 0.30));         // peaks at black
			double dm = v - 0.5;
			double wM = std::exp(-(dm * dm) / (2.0 * 0.28 * 0.28));       // peaks at mid grey
			double dh = v - 1.0;
			double wH = std::exp(-(dh * dh) / (2.0 * 0.30 * 0.30));       // peaks at white
			double adj = (shadow * wS + mid * wM + high * wH) * 64.0;     // full slider ≈ 64 levels
			lut[i] = qBound(0, qRound(i + adj), 255);
		}
	};
	int lutR[256], lutG[256], lutB[256];
	buildLut(sr / 100.0, mr / 100.0, hr / 100.0, lutR);
	buildLut(sg / 100.0, mg / 100.0, hg / 100.0, lutG);
	buildLut(sb / 100.0, mb / 100.0, hb / 100.0, lutB);

	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			int nr = lutR[r], ng = lutG[g], nb = lutB[b];
			if (preserveLum)
			{
				int oh, os, ol;
				QColor(r, g, b).getHsl(&oh, &os, &ol);
				int nh, ns, nl;
				QColor(nr, ng, nb).getHsl(&nh, &ns, &nl);
				QColor adj = QColor::fromHsl(nh < 0 ? 0 : nh, ns, ol);   // keep new hue/sat, original lightness
				nr = adj.red();
				ng = adj.green();
				nb = adj.blue();
			}

			if (cmyk)
			{
				p = (unsigned char *) s;
				int nk = 255 - qMax(nr, qMax(ng, nb));
				if (nk >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - nr - nk) * 255 / (255 - nk);
					p[1] = (255 - ng - nk) * 255 / (255 - nk);
					p[2] = (255 - nb - nk) * 255 / (255 - nk);
					p[3] = nk;
				}
			}
			else
			{
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::cmykAdjust(int cAdj, int mAdj, int yAdj, int kAdj, bool cmyk)
{
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int c, m, y, k, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				c = p[0]; m = p[1]; y = p[2]; k = p[3];
			}
			else
			{
				// RGB → CMYK.
				QRgb rgb = *s;
				int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
				alpha = qAlpha(rgb);
				k = 255 - qMax(r, qMax(g, b));
				if (k >= 255)
				{
					c = 0; m = 0; y = 0;
				}
				else
				{
					c = (255 - r - k) * 255 / (255 - k);
					m = (255 - g - k) * 255 / (255 - k);
					y = (255 - b - k) * 255 / (255 - k);
				}
			}

			// Additive per-channel adjustment: full slider (±100) = ±255 levels.
			c = qBound(0, c + cAdj * 255 / 100, 255);
			m = qBound(0, m + mAdj * 255 / 100, 255);
			y = qBound(0, y + yAdj * 255 / 100, 255);
			k = qBound(0, k + kAdj * 255 / 100, 255);

			if (cmyk)
			{
				p = (unsigned char *) s;
				p[0] = c; p[1] = m; p[2] = y; p[3] = k;
			}
			else
			{
				int nr = (255 - c) * (255 - k) / 255;
				int ng = (255 - m) * (255 - k) / 255;
				int nb = (255 - y) * (255 - k) / 255;
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::selectiveColor(const int* adj, bool relative, bool cmyk)
{
	// Range order matches the dialog: 0..5 chromatic (Reds, Yellows, Greens,
	// Cyans, Blues, Magentas), 6 Whites, 7 Neutrals, 8 Blacks.
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			// Range membership weights.
			double weight[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			int hue, sat, light;
			QColor(r, g, b).getHsl(&hue, &sat, &light);
			const double satF = sat / 255.0;
			if (hue >= 0 && satF > 0.0)
			{
				// Triangular hue membership between the two nearest primaries,
				// scaled by saturation. Centres: R0° Y60° G120° C180° B240° M300°.
				double seg = hue / 60.0;
				int lower = static_cast<int>(std::floor(seg)) % 6;
				int upper = (lower + 1) % 6;
				double frac = seg - std::floor(seg);
				weight[lower] += (1.0 - frac) * satF;
				weight[upper] += frac * satF;
			}
			// Tonal ranges by lightness (apply to all pixels).
			double l = light / 255.0;
			double wBlack = qBound(0.0, 1.0 - l * 2.0, 1.0);
			double wWhite = qBound(0.0, l * 2.0 - 1.0, 1.0);
			double wNeutral = qBound(0.0, 1.0 - wBlack - wWhite, 1.0);
			weight[6] += wWhite;
			weight[7] += wNeutral;
			weight[8] += wBlack;

			// Accumulate CMYK adjustment (fraction, -1..1 per channel).
			double adjC = 0, adjM = 0, adjY = 0, adjK = 0;
			for (int rng = 0; rng < 9; ++rng)
			{
				double wgt = weight[rng];
				if (wgt <= 0.0)
					continue;
				adjC += wgt * adj[rng * 4 + 0] / 100.0;
				adjM += wgt * adj[rng * 4 + 1] / 100.0;
				adjY += wgt * adj[rng * 4 + 2] / 100.0;
				adjK += wgt * adj[rng * 4 + 3] / 100.0;
			}

			// Pixel to CMYK.
			int k = 255 - qMax(r, qMax(g, b));
			int c, m, y;
			if (k >= 255)
			{
				c = 0; m = 0; y = 0;
			}
			else
			{
				c = (255 - r - k) * 255 / (255 - k);
				m = (255 - g - k) * 255 / (255 - k);
				y = (255 - b - k) * 255 / (255 - k);
			}

			// Relative: scale by existing amount; Absolute: additive.
			if (relative)
			{
				c = qRound(c + c * adjC);
				m = qRound(m + m * adjM);
				y = qRound(y + y * adjY);
				k = qRound(k + k * adjK);
			}
			else
			{
				c = qRound(c + adjC * 255.0);
				m = qRound(m + adjM * 255.0);
				y = qRound(y + adjY * 255.0);
				k = qRound(k + adjK * 255.0);
			}
			c = qBound(0, c, 255);
			m = qBound(0, m, 255);
			y = qBound(0, y, 255);
			k = qBound(0, k, 255);

			if (cmyk)
			{
				p = (unsigned char *) s;
				p[0] = c; p[1] = m; p[2] = y; p[3] = k;
			}
			else
			{
				int nr = (255 - c) * (255 - k) / 255;
				int ng = (255 - m) * (255 - k) / 255;
				int nb = (255 - y) * (255 - k) / 255;
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

QImage ScImage::asRGBImage(bool cmyk) const
{
	if (!cmyk)
		return qImage().copy();
	// Stored data is C,M,Y,K bytes — convert to an RGB image for analysis.
	int w = width();
	int h = height();
	QImage out(w, h, QImage::Format_ARGB32);
	for (int y = 0; y < h; ++y)
	{
		const unsigned char* sl = constScanLine(y);
		QRgb* o = (QRgb*)out.scanLine(y);
		for (int x = 0; x < w; ++x)
		{
			const unsigned char* p = sl + x * 4;
			int c = p[0], m = p[1], yv = p[2], k = p[3];
			int r = (255 - c) * (255 - k) / 255;
			int g = (255 - m) * (255 - k) / 255;
			int b = (255 - yv) * (255 - k) / 255;
			o[x] = qRgba(r, g, b, 255);
		}
	}
	return out;
}

void ScImage::setFromRGBImage(const QImage& rgb, bool cmyk)
{
	int w = qMin(width(), rgb.width());
	int h = qMin(height(), rgb.height());
	for (int y = 0; y < h; ++y)
	{
		const QRgb* s = (const QRgb*)rgb.constScanLine(y);
		if (!cmyk)
		{
			QRgb* d = (QRgb*)scanLine(y);
			for (int x = 0; x < w; ++x)
				d[x] = s[x];
		}
		else
		{
			unsigned char* d = scanLine(y);
			for (int x = 0; x < w; ++x)
			{
				int r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
				int k = 255 - qMax(r, qMax(g, b));
				unsigned char* p = d + x * 4;
				if (k >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - r - k) * 255 / (255 - k);
					p[1] = (255 - g - k) * 255 / (255 - k);
					p[2] = (255 - b - k) * 255 / (255 - k);
					p[3] = k;
				}
			}
		}
	}
}

void ScImage::shadowsHighlights(int shadowAmount, int shadowTone, int highlightAmount, int highlightTone,
                                int radius, int color, int midtone, bool cmyk)
{
	QImage img = asRGBImage(cmyk);
	int w = img.width();
	int h = img.height();
	if (w == 0 || h == 0)
		return;

	QVector<float> lum(w * h), lumB(w * h), tmp(w * h);
	for (int y = 0; y < h; ++y)
	{
		const QRgb* s = (const QRgb*)img.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			QRgb p = s[x];
			lum[y * w + x] = (0.299f * qRed(p) + 0.587f * qGreen(p) + 0.114f * qBlue(p)) / 255.0f;
		}
	}

	// Local reference = box-blurred luminance (sliding window, O(n) per pass).
	if (radius > 0)
	{
		int r = radius;
		float div = 2 * r + 1;
		for (int y = 0; y < h; ++y)   // horizontal
		{
			float sum = 0;
			for (int k = -r; k <= r; ++k)
				sum += lum[y * w + qBound(0, k, w - 1)];
			for (int x = 0; x < w; ++x)
			{
				tmp[y * w + x] = sum / div;
				sum -= lum[y * w + qBound(0, x - r, w - 1)];
				sum += lum[y * w + qBound(0, x + r + 1, w - 1)];
			}
		}
		for (int x = 0; x < w; ++x)   // vertical
		{
			float sum = 0;
			for (int k = -r; k <= r; ++k)
				sum += tmp[qBound(0, k, h - 1) * w + x];
			for (int y = 0; y < h; ++y)
			{
				lumB[y * w + x] = sum / div;
				sum -= tmp[qBound(0, y - r, h - 1) * w + x];
				sum += tmp[qBound(0, y + r + 1, h - 1) * w + x];
			}
		}
	}
	else
	{
		lumB = lum;
	}

	double sA = shadowAmount / 100.0;
	double hA = highlightAmount / 100.0;
	double sTh = 0.5 * (shadowTone / 100.0) + 0.02;
	double hTh = 0.5 * (highlightTone / 100.0) + 0.02;
	double colorAdj = color / 100.0;
	double midAdj = midtone / 100.0;

	for (int y = 0; y < h; ++y)
	{
		QRgb* d = (QRgb*)img.scanLine(y);
		for (int x = 0; x < w; ++x)
		{
			int idx = y * w + x;
			QRgb p = d[x];
			int r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);
			float L = lum[idx];
			float Lb = lumB[idx];
			double shadowMask = qBound(0.0, 1.0 - Lb / sTh, 1.0);
			double highlightMask = qBound(0.0, 1.0 - (1.0 - Lb) / hTh, 1.0);

			double newL = L;
			newL += sA * shadowMask * (1.0 - L) * 0.9;       // lift shadows toward white
			newL -= hA * highlightMask * newL * 0.9;         // pull highlights toward black
			if (midAdj != 0.0)                               // gentle midtone contrast
			{
				double dv = newL - 0.5;
				newL += midAdj * 0.5 * dv * (1.0 - 4.0 * dv * dv);
			}
			newL = qBound(0.0, newL, 1.0);

			double add = (newL - L) * 255.0;   // luminance shift, applied additively (hue-preserving)
			double nr = r + add, ng = g + add, nb = b + add;
			if (colorAdj != 0.0)
			{
				double gray = 0.299 * nr + 0.587 * ng + 0.114 * nb;
				double sat = 1.0 + colorAdj;
				nr = gray + (nr - gray) * sat;
				ng = gray + (ng - gray) * sat;
				nb = gray + (nb - gray) * sat;
			}
			d[x] = qRgba(qBound(0, qRound(nr), 255), qBound(0, qRound(ng), 255), qBound(0, qRound(nb), 255), a);
		}
	}
	setFromRGBImage(img, cmyk);
}

void ScImage::motionBlur(int angle, int distance, bool cmyk)
{
	if (distance < 1)
		return;
	const double pi = 3.14159265358979323846;
	QImage src = qImage().copy();
	int w = width();
	int h = height();
	double rad = angle * pi / 180.0;
	double dx = std::cos(rad);
	double dy = -std::sin(rad);   // 0° = →, 90° = ↑
	int half = distance / 2;
	for (int y = 0; y < h; ++y)
	{
		QRgb *d = (QRgb*)scanLine(y);
		for (int x = 0; x < w; ++x)
		{
			long r = 0, g = 0, b = 0, a = 0;
			int cnt = 0;
			for (int t = -half; t <= half; ++t)
			{
				int sx = qBound(0, qRound(x + dx * t), w - 1);
				int sy = qBound(0, qRound(y + dy * t), h - 1);
				QRgb pv = ((const QRgb*)src.constScanLine(sy))[sx];
				r += qRed(pv); g += qGreen(pv); b += qBlue(pv); a += qAlpha(pv);
				++cnt;
			}
			d[x] = qRgba(r / cnt, g / cnt, b / cnt, a / cnt);
		}
	}
}

void ScImage::radialBlur(int amount, int mode, bool cmyk)
{
	if (amount < 1)
		return;
	const double pi = 3.14159265358979323846;
	QImage src = qImage().copy();
	int w = width();
	int h = height();
	double cx = w / 2.0;
	double cy = h / 2.0;
	int samples = qBound(3, amount / 2 + 3, 64);
	for (int y = 0; y < h; ++y)
	{
		QRgb *d = (QRgb*)scanLine(y);
		for (int x = 0; x < w; ++x)
		{
			double relx = x - cx;
			double rely = y - cy;
			long r = 0, g = 0, b = 0, a = 0;
			int cnt = 0;
			for (int sIdx = 0; sIdx < samples; ++sIdx)
			{
				double f = static_cast<double>(sIdx) / (samples - 1) - 0.5;   // -0.5..0.5
				int sx, sy;
				if (mode == 0)   // spin
				{
					double ang = f * (amount * pi / 180.0);
					double ca = std::cos(ang), sa = std::sin(ang);
					sx = qRound(cx + relx * ca - rely * sa);
					sy = qRound(cy + relx * sa + rely * ca);
				}
				else             // zoom
				{
					double scale = 1.0 + f * (amount / 100.0);
					sx = qRound(cx + relx * scale);
					sy = qRound(cy + rely * scale);
				}
				sx = qBound(0, sx, w - 1);
				sy = qBound(0, sy, h - 1);
				QRgb pv = ((const QRgb*)src.constScanLine(sy))[sx];
				r += qRed(pv); g += qGreen(pv); b += qBlue(pv); a += qAlpha(pv);
				++cnt;
			}
			d[x] = qRgba(r / cnt, g / cnt, b / cnt, a / cnt);
		}
	}
}

void ScImage::boxBlur(int radius, bool cmyk)
{
	if (radius < 1)
		return;
	int w = width();
	int h = height();
	int div = 2 * radius + 1;
	QImage src = qImage().copy();
	QImage tmp(w, h, QImage::Format_ARGB32);

	// Horizontal pass.
	for (int y = 0; y < h; ++y)
	{
		const QRgb *s = (const QRgb*)src.constScanLine(y);
		QRgb *t = (QRgb*)tmp.scanLine(y);
		for (int x = 0; x < w; ++x)
		{
			long r = 0, g = 0, b = 0, a = 0;
			for (int k = -radius; k <= radius; ++k)
			{
				QRgb pv = s[qBound(0, x + k, w - 1)];
				r += qRed(pv); g += qGreen(pv); b += qBlue(pv); a += qAlpha(pv);
			}
			t[x] = qRgba(r / div, g / div, b / div, a / div);
		}
	}
	// Vertical pass.
	for (int x = 0; x < w; ++x)
	{
		for (int y = 0; y < h; ++y)
		{
			long r = 0, g = 0, b = 0, a = 0;
			for (int k = -radius; k <= radius; ++k)
			{
				QRgb pv = ((const QRgb*)tmp.constScanLine(qBound(0, y + k, h - 1)))[x];
				r += qRed(pv); g += qGreen(pv); b += qBlue(pv); a += qAlpha(pv);
			}
			((QRgb*)scanLine(y))[x] = qRgba(r / div, g / div, b / div, a / div);
		}
	}
}

void ScImage::blackWhite(const int* w, bool tint, int tr, int tg, int tb, bool cmyk)
{
	// Per-range weights as fractions; a pure primary maps to weight×255 gray.
	double wf[6];
	for (int i = 0; i < 6; ++i)
		wf[i] = w[i] / 100.0;

	int tintH = 0, tintS = 0, tintL = 0;
	if (tint)
	{
		QColor(tr, tg, tb).getHsl(&tintH, &tintS, &tintL);
		if (tintH < 0)
			tintH = 0;
	}

	int h = height();
	int wd = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < wd; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			int maxc = qMax(r, qMax(g, b));
			int minc = qMin(r, qMin(g, b));
			int chroma = maxc - minc;

			double gray;
			if (chroma == 0)
			{
				gray = minc;   // neutral greys pass through
			}
			else
			{
				double hue;
				if (maxc == r)
					hue = 60.0 * std::fmod(static_cast<double>(g - b) / chroma, 6.0);
				else if (maxc == g)
					hue = 60.0 * (static_cast<double>(b - r) / chroma + 2.0);
				else
					hue = 60.0 * (static_cast<double>(r - g) / chroma + 4.0);
				if (hue < 0.0)
					hue += 360.0;
				double seg = hue / 60.0;
				int lower = static_cast<int>(std::floor(seg)) % 6;
				int upper = (lower + 1) % 6;
				double frac = seg - std::floor(seg);
				double wHue = wf[lower] * (1.0 - frac) + wf[upper] * frac;
				gray = minc + chroma * wHue;
			}
			int grayI = qBound(0, qRound(gray), 255);

			int nr, ng, nb;
			if (tint)
			{
				QColor tc = QColor::fromHsl(tintH, tintS, grayI);
				nr = tc.red(); ng = tc.green(); nb = tc.blue();
			}
			else
			{
				nr = ng = nb = grayI;
			}

			if (cmyk)
			{
				p = (unsigned char *) s;
				int nk = 255 - qMax(nr, qMax(ng, nb));
				if (nk >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - nr - nk) * 255 / (255 - nk);
					p[1] = (255 - ng - nk) * 255 / (255 - nk);
					p[2] = (255 - nb - nk) * 255 / (255 - nk);
					p[3] = nk;
				}
			}
			else
			{
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::threshold(int level, bool cmyk)
{
	level = qBound(1, level, 255);
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			int lum = qRound(0.299 * r + 0.587 * g + 0.114 * b);
			bool white = (lum >= level);

			if (cmyk)
			{
				p = (unsigned char *) s;
				p[0] = 0; p[1] = 0; p[2] = 0;
				p[3] = white ? 0 : 255;   // K only
			}
			else
			{
				int v = white ? 255 : 0;
				*s = qRgba(v, v, v, alpha);
			}
			s++;
		}
	}
}

void ScImage::photoFilter(int fr, int fg, int fb, int density, bool preserveLum, bool cmyk)
{
	const double d = qBound(0, density, 100) / 100.0;
	const double nfr = fr / 255.0;
	const double nfg = fg / 255.0;
	const double nfb = fb / 255.0;
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			// Multiply the pixel by the filter colour, blended in at `density`.
			double outR = r * (1.0 - d) + (r * nfr) * d;
			double outG = g * (1.0 - d) + (g * nfg) * d;
			double outB = b * (1.0 - d) + (b * nfb) * d;

			if (preserveLum)
			{
				double origLum = 0.299 * r + 0.587 * g + 0.114 * b;
				double newLum  = 0.299 * outR + 0.587 * outG + 0.114 * outB;
				if (newLum > 0.01)
				{
					double scale = origLum / newLum;
					outR *= scale;
					outG *= scale;
					outB *= scale;
				}
			}

			int nr = qBound(0, qRound(outR), 255);
			int ng = qBound(0, qRound(outG), 255);
			int nb = qBound(0, qRound(outB), 255);

			if (cmyk)
			{
				p = (unsigned char *) s;
				int nk = 255 - qMax(nr, qMax(ng, nb));
				if (nk >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - nr - nk) * 255 / (255 - nk);
					p[1] = (255 - ng - nk) * 255 / (255 - nk);
					p[2] = (255 - nb - nk) * 255 / (255 - nk);
					p[3] = nk;
				}
			}
			else
			{
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::channelMixer(const int* mix, bool monochrome, bool cmyk)
{
	int h = height();
	int w = width();
	QRgb *s;
	unsigned char *p;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine(yi));
		for (int xi = 0; xi < w; ++xi)
		{
			int r, g, b, alpha = 255;
			if (cmyk)
			{
				p = (unsigned char *) s;
				int cc = p[0], mm = p[1], yy = p[2], kk = p[3];
				r = (255 - cc) * (255 - kk) / 255;
				g = (255 - mm) * (255 - kk) / 255;
				b = (255 - yy) * (255 - kk) / 255;
			}
			else
			{
				QRgb rgb = *s;
				r = qRed(rgb);
				g = qGreen(rgb);
				b = qBlue(rgb);
				alpha = qAlpha(rgb);
			}

			int nr, ng, nb;
			if (monochrome)
			{
				int gray = qBound(0, (mix[12] * r + mix[13] * g + mix[14] * b) / 100 + mix[15] * 255 / 100, 255);
				nr = ng = nb = gray;
			}
			else
			{
				nr = qBound(0, (mix[0] * r + mix[1] * g + mix[2]  * b) / 100 + mix[3]  * 255 / 100, 255);
				ng = qBound(0, (mix[4] * r + mix[5] * g + mix[6]  * b) / 100 + mix[7]  * 255 / 100, 255);
				nb = qBound(0, (mix[8] * r + mix[9] * g + mix[10] * b) / 100 + mix[11] * 255 / 100, 255);
			}

			if (cmyk)
			{
				p = (unsigned char *) s;
				int nk = 255 - qMax(nr, qMax(ng, nb));
				if (nk >= 255)
				{
					p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
				}
				else
				{
					p[0] = (255 - nr - nk) * 255 / (255 - nk);
					p[1] = (255 - ng - nk) * 255 / (255 - nk);
					p[2] = (255 - nb - nk) * 255 / (255 - nk);
					p[3] = nk;
				}
			}
			else
			{
				*s = qRgba(nr, ng, nb, alpha);
			}
			s++;
		}
	}
}

void ScImage::toGrayscale(bool cmyk)
{
	int h = height();
	int w = width();
	int k;
	QRgb * s;
	QRgb r;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s;
			if (cmyk)
			{
				k = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r) + qAlpha(r)), 255);
				*s = qRgba(0, 0, 0, k);
			}
			else
			{
				k = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
				*s = qRgba(k, k, k, qAlpha(r));
			}
			s++;
		}
	}
}

void ScImage::swapRGBA()
{
	unsigned int *ptr;
	unsigned char *p;
	unsigned char r, b;
	for (int i = 0; i < height(); ++i)
	{
		ptr = (unsigned int *) scanLine(i);
		for (int j = 0; j < width(); ++j)
		{
			p = (unsigned char *) ptr;
			r = p[0];
			b = p[2];
			p[2] = r;
			p[0] = b;
			ptr++;
		}
	}
}

bool ScImage::createLowRes(double scale)
{
	int w = qRound(width() / scale);
	int h = qRound(height() / scale);
	if (w >= width() && h >= height())  // don't do unnecessary scaling
		return false;
	QImage tmp = scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	if (tmp.format() != QImage::Format_ARGB32)
		tmp = tmp.convertToFormat(QImage::Format_ARGB32);
	QImage::operator=(tmp);
	return true;
}

bool ScImage::convert2JPG(const QString& fn, int Quality, bool isCMYK, bool isGray)
{
	QFile file(fn);
	if (!file.open(QIODevice::WriteOnly))
		return false;

	bool success = false;
	ScJpegEncodeFilter::Color imgColor = ScJpegEncodeFilter::GRAY;
	if (isCMYK)
		imgColor = ScJpegEncodeFilter::CMYK;
	else if (!isGray)
		imgColor = ScJpegEncodeFilter::RGB;
	int qual[] = { 95, 85, 75, 50, 25 };  // These are the JPEG Quality settings 100 means best, 0 .. don't discuss
	QDataStream dataStream(&file);
	ScJpegEncodeFilter jpegFilter(&dataStream, width(), height(), imgColor);
	jpegFilter.setQuality(qual[Quality]);
	if (jpegFilter.openFilter())
	{
		if (isCMYK)
			success = writeCMYKDataToFilter(&jpegFilter);
		else if (isGray)
			success = writeGrayDataToFilter(&jpegFilter, true);
		else
			success = writeRGBDataToFilter(&jpegFilter);
		success &= jpegFilter.closeFilter();
	}
	file.close();

	return success;
}

QByteArray ScImage::ImageToArray() const
{
	int i = 0;
	int h = height();
	int w = width();
	unsigned char u;
	const QRgb *rgb;
	QByteArray imgArray(3 * h * w, ' ');
	if (imgArray.isNull())
		return imgArray;
	for (int yi = 0; yi < h; ++yi)
	{
		rgb = (QRgb*) this->constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			u = qRed(*rgb);
			imgArray[i++] = u;
			u = qGreen(*rgb);
			imgArray[i++] = u;
			u = qBlue(*rgb);
			imgArray[i++] = u;
			++rgb;
		}
	}
	return imgArray;
}

void ScImage::convertToGray()
{
	int k;
	int h = height();
	int w = width();
	QRgb *s, r;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (QRgb*)(scanLine( yi ));
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s;
			k = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
			*s++ = qRgba(k, 0, 0, 0);
		}
	}
}

bool ScImage::writeRGBDataToFilter(ScStreamFilter* filter) const
{
	QRgb r;
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  h = height();
	int  w = width();
	int  pending = 0;
	int  scanLineSize = (3 * w);
	int  bufferSize   = qMax(scanLineSize, (65536 - 65536 % scanLineSize));
	buffer.resize(bufferSize + 16);
	if (buffer.isNull()) // Memory allocation failure
		return false;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s++;
			buffer[pending++] = static_cast<unsigned char>(qRed(r));
			buffer[pending++] = static_cast<unsigned char>(qGreen(r));
			buffer[pending++] = static_cast<unsigned char>(qBlue(r));
		}
		if (pending >= bufferSize)
		{
			success &= filter->writeData(buffer.constData(), pending);
			pending = 0;
		}
	}
	if (pending)
		success &= filter->writeData(buffer.constData(), pending);
	return success;
}

bool ScImage::writeGrayDataToFilter(ScStreamFilter* filter, bool precal) const
{
	QRgb r;
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  h = height();
	int  w = width();
	int  pending = 0, k;
	int  scanLineSize = w;
	int  bufferSize   = qMax(scanLineSize, (65536 - 65536 % scanLineSize));
	buffer.resize(bufferSize + 16);
	if (buffer.isNull()) // Memory allocation failure
		return false;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		if (precal) // image data is already grayscale, no need for weighted conversion
		{
			for (int xi = 0; xi < w; ++xi)
			{
				r = *s;
				k = qRed(r);
				buffer[pending++] = k;
				s++;
			}
		}
		else
		{
			for (int xi = 0; xi < w; ++xi)
			{
				r = *s;
				k = qMin(qRound(0.3 * qRed(r) + 0.59 * qGreen(r) + 0.11 * qBlue(r)), 255);
				buffer[pending++] = k;
				s++;
			}
		}
		if (pending >= bufferSize)
		{
			success &= filter->writeData(buffer.constData(), pending);
			pending = 0;
		}
	}
	if (pending)
		success &= filter->writeData(buffer.constData(), pending);
	return success;
}

bool ScImage::writeMonochromeDataToFilter(ScStreamFilter* filter, bool fromCmyk) const
{
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  h = height();
	int  w = width();
	int  byteCount = 0;
	int  bufferSize = (w + 7) / 8 * h;
	int  value;
	const unsigned char threshold = 127;
	buffer.resize(bufferSize);
	if (buffer.isNull()) // Memory allocation failure
		return false;
	for (int yi = 0; yi < h; ++yi)
	{
		char curByte = 0;
		int bitCount = 0;
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			curByte <<= 1;
			value = fromCmyk ? (255 - qAlpha(*s)) : qRed(*s);
			if (value > threshold) // In monochrome images all elements have the same value.
				curByte |= 1;
			++bitCount;
			if (bitCount == 8)
			{
				buffer[byteCount++] = curByte;
				curByte = 0;
				bitCount = 0;
			}
			++s;
		}
		// End of line is aligned to byte.
		if (bitCount > 0) {
			curByte <<=  8-bitCount;
			buffer[byteCount++] = curByte;
		}
	}
	assert(byteCount == bufferSize);
	success = filter->writeData(buffer.constData(), byteCount);
	return success;
}

bool ScImage::writeCMYKDataToFilter(ScStreamFilter* filter) const
{
	QRgb r;
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  h = height();
	int  w = width();
	int  pending = 0;
	int  scanLineSize = (4 * w);
	int  bufferSize   = qMax(scanLineSize, (65536 - 65536 % scanLineSize));
	buffer.resize(bufferSize + 16);
	if (buffer.isNull()) // Memory allocation failure
		return false;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s++;
			buffer[pending++] = static_cast<unsigned char> (qRed(r));
			buffer[pending++] = static_cast<unsigned char> (qGreen(r));
			buffer[pending++] = static_cast<unsigned char> (qBlue(r));
			buffer[pending++] = static_cast<unsigned char> (qAlpha(r));
		}
		if (pending >= bufferSize)
		{
			success &= filter->writeData(buffer.constData(), pending);
			pending = 0;
		}
	}
	if (pending)
		success &= filter->writeData(buffer.constData(), pending);
	return success;
}

bool ScImage::writePSImageToFilter(ScStreamFilter* filter, int pl) const
{
	QRgb r;
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  c, m, y, k;
	int  h = height();
	int  w = width();
	int  pending = 0;
	int  scanLineSize = (pl == -1) ? (4 * w) : w;
	int  bufferSize   = qMax(scanLineSize, (65536 - 65536 % scanLineSize));
	buffer.resize(bufferSize + 16);
	if (buffer.isNull()) // Memory allocation failure
		return false;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s++;
			c = qRed(r);
			m = qGreen(r);
			y = qBlue(r);
			k = qAlpha(r);
			if (pl == -1)
			{
				buffer[pending++] = static_cast<unsigned char> (c);
				buffer[pending++] = static_cast<unsigned char> (m);
				buffer[pending++] = static_cast<unsigned char> (y);
				buffer[pending++] = static_cast<unsigned char> (k);
			}
			else
			{
				if (pl == -2)
					buffer[pending++] = static_cast<unsigned char> (qMin(255, qRound(0.3 * c + 0.59 * m + 0.11 * y + k)));
				if (pl == 1)
					buffer[pending++] = static_cast<unsigned char> (c);
				if (pl == 2)
					buffer[pending++] = static_cast<unsigned char> (m);
				if (pl == 3)
					buffer[pending++] = static_cast<unsigned char> (y);
				if (pl == 0)
					buffer[pending++] = static_cast<unsigned char> (k);
			}
		}
		if (pending >= bufferSize)
		{
			success &= filter->writeData(buffer.constData(), pending);
			pending = 0;
		}
	}
	if (pending)
		success &= filter->writeData(buffer.constData(), pending);
	return success;
}

bool ScImage::writePSImageToFilter(ScStreamFilter* filter, const QByteArray& mask, int pl) const
{
	QRgb r;
	const QRgb *s;
	QByteArray buffer;
	bool success = true;
	int  c, m, y, k;
	int  h = height();
	int  w = width();
	int  pending = 0;
	int  scanLineSize = (pl == -1) ? (5 * w) : (2 * w);
	int  bufferSize   = qMax(scanLineSize, (65536 - 65536 % scanLineSize));
	if (mask.size() < (h * w))  // Check if mask provide enough data
		return false;
	buffer.resize(bufferSize + 16);
	if (buffer.isNull()) // Check for memory allocation failure
		return false;
	unsigned char* maskData = (unsigned char*) mask.constData();
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			r = *s++;
			c = qRed(r);
			m = qGreen(r);
			y = qBlue(r);
			k = qAlpha(r);
			if (pl == -1)
			{
				buffer[pending++] = *maskData++;
				buffer[pending++] = static_cast<unsigned char> (c);
				buffer[pending++] = static_cast<unsigned char> (m);
				buffer[pending++] = static_cast<unsigned char> (y);
				buffer[pending++] = static_cast<unsigned char> (k);
			}
			else
			{
				buffer[pending++] = *maskData++;
				if (pl == -2)
					buffer[pending++] = static_cast<unsigned char> (qMin(255, qRound(0.3 * c + 0.59 * m + 0.11 * y + k)));
				if (pl == 1)
					buffer[pending++] = static_cast<unsigned char> (c);
				if (pl == 2)
					buffer[pending++] = static_cast<unsigned char> (m);
				if (pl == 3)
					buffer[pending++] = static_cast<unsigned char> (y);
				if (pl == 0)
					buffer[pending++] = static_cast<unsigned char> (k);
			}
		}
		if (pending >= bufferSize)
		{
			success &= filter->writeData(buffer.constData(), pending);
			pending = 0;
		}
	}
	if (pending)
		success &= filter->writeData(buffer.constData(), pending);
	return success;
}

void ScImage::scaleImage(int nwidth, int nheight)
{
	int depth = this->depth();
	if (depth == 32)
	{
		scaleImage32bpp(nwidth, nheight);
		return;
	}
	scaleImageGeneric(nwidth, nheight);
}

void ScImage::scaleImage32bpp(int nwidth, int nheight)
{
	QImage dst(nwidth, nheight, QImage::Format_ARGB32);
	QRgb* xelrow = nullptr;
	QRgb* tempxelrow = nullptr;
	QRgb* xP = nullptr;
	QRgb* nxP = nullptr;
	int rows, cols, rowsread, newrows, newcols;
	int row, col, needtoreadrow;
	const uchar maxval = 255;
	double xscale, yscale;
	long sxscale, syscale;
	long fracrowtofill, fracrowleft;
	long* as = nullptr;
	long* rs = nullptr;
	long* gs = nullptr;
	long* bs = nullptr;
	int rowswritten = 0;

	int depth = this->depth();
	if (depth != 32)
	{
		QImage::operator=(QImage::scaled(nwidth, nheight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		return;
	}

	cols = width();
	rows = height();
	newcols = dst.width();
	newrows = dst.height();
	long SCALE;
	long HALFSCALE;
	if (cols > 4096)
	{
		SCALE = 4096;
		HALFSCALE = 2048;
	}
	else
	{
		int fac = 4096;
		while ((cols * fac) > 4096)
		{
			fac /= 2;
		}
		SCALE = fac * cols;
		HALFSCALE = fac * cols / 2;
	}
	xscale = (double) newcols / (double) cols;
	yscale = (double) newrows / (double) rows;
	sxscale = (long) (xscale * SCALE);
	syscale = (long) (yscale * SCALE);
	if ( newrows != rows )	/* shortcut Y scaling if possible */
		tempxelrow = new QRgb[cols];
	as = new long[cols];
	rs = new long[cols];
	gs = new long[cols];
	bs = new long[cols];
	rowsread = 0;
	fracrowleft = syscale;
	needtoreadrow = 1;
	for (col = 0; col < cols; ++col)
		rs[col] = gs[col] =  as[col] = bs[col] = HALFSCALE;
	fracrowtofill = SCALE;
	for (row = 0; row < newrows; ++row)
	{
		if ( newrows == rows )
			tempxelrow = xelrow = (QRgb*) scanLine(rowsread++);
		else
		{
			while ( fracrowleft < fracrowtofill )
			{
				if ( needtoreadrow && rowsread < rows )
					xelrow = (QRgb*) scanLine(rowsread++);
				for ( col = 0, xP = xelrow; col < cols; ++col, ++xP )
				{
					as[col] += fracrowleft * qAlpha( *xP );
					rs[col] += fracrowleft * qRed( *xP );
					gs[col] += fracrowleft * qGreen( *xP );
					bs[col] += fracrowleft * qBlue( *xP );
				}
				fracrowtofill -= fracrowleft;
				fracrowleft = syscale;
				needtoreadrow = 1;
			}
			if (needtoreadrow && rowsread < rows)
			{
				xelrow = (QRgb*) scanLine(rowsread++);
				needtoreadrow = 0;
			}
			long a = 0;
			for (col = 0, xP = xelrow, nxP = tempxelrow; col < cols; ++col, ++xP, ++nxP)
			{
				long r, g, b;
				a = as[col] + fracrowtofill * qAlpha( *xP );
				r = rs[col] + fracrowtofill * qRed( *xP );
				g = gs[col] + fracrowtofill * qGreen( *xP );
				b = bs[col] + fracrowtofill * qBlue( *xP );
				r /= SCALE;
				if (r > maxval)
					r = maxval;
				g /= SCALE;
				if (g > maxval)
					g = maxval;
				b /= SCALE;
				if (b > maxval)
					b = maxval;
				a /= SCALE;
				if (a > maxval)
					a = maxval;
				*nxP = qRgba((int) r, (int) g, (int) b , (int) a);
				rs[col] = as[col] = gs[col] = bs[col] = HALFSCALE;
			}
			fracrowleft -= fracrowtofill;
			if (fracrowleft == 0)
			{
				fracrowleft = syscale;
				needtoreadrow = 1;
			}
			fracrowtofill = SCALE;
		}
		if (newcols == cols)
			memcpy(dst.scanLine(rowswritten++), tempxelrow, newcols*4);
		else
		{
			long a, r, g, b;
			long fraccoltofill, fraccolleft = 0;
			int needcol;
			nxP = (QRgb*) dst.scanLine(rowswritten++);
			QRgb *nxPEnd = nxP + newcols;
			fraccoltofill = SCALE;
			a = r = g = b = HALFSCALE;
			needcol = 0;
			for (col = 0, xP = tempxelrow; col < cols; ++col, ++xP)
			{
				fraccolleft = sxscale;
				while (fraccolleft >= fraccoltofill)
				{
					if (needcol)
					{
						++nxP;
						a = r = g = b = HALFSCALE;
					}
					a += fraccoltofill * qAlpha(*xP);
					r += fraccoltofill * qRed(*xP);
					g += fraccoltofill * qGreen(*xP);
					b += fraccoltofill * qBlue(*xP);
					r /= SCALE;
					if (r > maxval)
						r = maxval;
					g /= SCALE;
					if (g > maxval)
						g = maxval;
					b /= SCALE;
					if (b > maxval)
						b = maxval;
					a /= SCALE;
					if (a > maxval)
						a = maxval;
					*nxP = qRgba((int) r, (int) g, (int) b, (int) a);
					fraccolleft -= fraccoltofill;
					fraccoltofill = SCALE;
					needcol = 1;
				}
				if (fraccolleft > 0)
				{
					if (needcol)
					{
						++nxP;
						a = r = g = b = HALFSCALE;
						needcol = 0;
					}
					a += fraccolleft * qAlpha(*xP);
					r += fraccolleft * qRed(*xP);
					g += fraccolleft * qGreen(*xP);
					b += fraccolleft * qBlue(*xP);
					fraccoltofill -= fraccolleft;
				}
			}
			if (fraccoltofill > 0)
			{
				--xP;
				a += fraccoltofill * qAlpha(*xP);
				r += fraccoltofill * qRed(*xP);
				g += fraccoltofill * qGreen(*xP);
				b += fraccoltofill * qBlue(*xP);
			}
			if (nxP < nxPEnd)
			{
				r /= SCALE;
				if (r > maxval)
					r = maxval;
				g /= SCALE;
				if (g > maxval)
					g = maxval;
				b /= SCALE;
				if (b > maxval)
					b = maxval;
				a /= SCALE;
				if (a > maxval)
					a = maxval;
				*nxP = qRgba((int) r, (int) g, (int) b, (int) a);
				while (++nxP != nxPEnd)
					nxP[0] = nxP[-1];
			}
		}
	}
	if (newrows != rows && tempxelrow) // Robust, tempxelrow might be 0 1 day
		delete [] tempxelrow;
	delete [] as;
	delete [] rs;
	delete [] gs;
	delete [] bs;
	QImage::operator=(QImage(nwidth, nheight, QImage::Format_ARGB32));
	for (int yi = 0; yi < dst.height(); ++yi)
	{
		QRgb *s = (QRgb*) dst.scanLine(yi);
		QRgb *d = (QRgb*) scanLine(yi);
		for (int xi = 0; xi < dst.width(); ++xi)
		{
			(*d) = (*s);
			s++;
			d++;
		}
	}
}

void ScImage::scaleImageGeneric(int nwidth, int nheight)
{
	unsigned char* xelrow = nullptr;
	unsigned char* tempxelrow = nullptr;
	unsigned char* xP;
	unsigned char* nxP;
	int rows, cols, rowsread, newrows, newcols;
	int row, col, needtoreadrow;
	const uchar maxval = 255;
	double xscale, yscale;
	long sxscale, syscale;
	long fracrowtofill, fracrowleft;
	long* ps;
	int rowswritten = 0;

	int depth = this->depth();
	Format imgFormat = this->format();
	bool execScaled = (depth == 1 || depth == 4 || depth == 16);
	execScaled |= (imgFormat == QImage::Format_ARGB8565_Premultiplied);
	execScaled |= (imgFormat == QImage::Format_RGB666);
	execScaled |= (imgFormat == QImage::Format_ARGB6666_Premultiplied);
	execScaled |= (imgFormat == QImage::Format_ARGB8555_Premultiplied);
	if (execScaled)
	{
		QImage::operator=(QImage::scaled(nwidth, nheight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		return;
	}

	QImage dst(nwidth, nheight, this->format());
	int nChannels = this->depth() / 8;

	cols = width();
	rows = height();
	newcols = dst.width();
	newrows = dst.height();
	long SCALE;
	long HALFSCALE;
	if (cols > 4096)
	{
		SCALE = 4096;
		HALFSCALE = 2048;
	}
	else
	{
		int fac = 4096;
		while ((cols * fac) > 4096)
		{
			fac /= 2;
		}
		SCALE = fac * cols;
		HALFSCALE = fac * cols / 2;
	}
	xscale = (double) newcols / (double) cols;
	yscale = (double) newrows / (double) rows;
	sxscale = (long)(xscale * SCALE);
	syscale = (long)(yscale * SCALE);
	if (newrows != rows)	/* shortcut Y scaling if possible */
		tempxelrow = new unsigned char[cols * nChannels];
	ps = new long[cols];

	for (int chIndex = 0; chIndex < nChannels; ++chIndex)
	{
		xelrow = nullptr;
		rowsread = rowswritten = 0;
		fracrowleft = syscale;
		needtoreadrow = 1;
		for (col = 0; col < cols; ++col)
			ps[col] = HALFSCALE;
		fracrowtofill = SCALE;
		for (row = 0; row < newrows; ++row)
		{
			if (newrows == rows)
				tempxelrow = xelrow = scanLine(rowsread++);
			else
			{
				while (fracrowleft < fracrowtofill)
				{
					if (needtoreadrow && rowsread < rows)
						xelrow = scanLine(rowsread++);
					for (col = 0, xP = xelrow + chIndex; col < cols; ++col, xP += nChannels)
						ps[col] += fracrowleft * (*xP);
					fracrowtofill -= fracrowleft;
					fracrowleft = syscale;
					needtoreadrow = 1;
				}
				if (needtoreadrow && rowsread < rows)
				{
					xelrow = scanLine(rowsread++);
					needtoreadrow = 0;
				}
				long p = 0;
				xP  = xelrow + chIndex;
				nxP = tempxelrow + chIndex;
				for (col = 0; col < cols; ++col, xP += nChannels, nxP += nChannels)
				{
					p = ps[col] + fracrowtofill * (*xP);
					p /= SCALE;
					if (p > maxval)
						p = maxval;
					*nxP = (unsigned char) p;
					ps[col] = HALFSCALE;
				}
				fracrowleft -= fracrowtofill;
				if (fracrowleft == 0)
				{
					fracrowleft = syscale;
					needtoreadrow = 1;
				}
				fracrowtofill = SCALE;
			}
			if (newcols == cols)
			{
				memcpy(dst.scanLine(rowswritten++), tempxelrow, newcols * nChannels);
			}
			else
			{
				long p;
				long fraccoltofill, fraccolleft = 0;
				int needcol;
				nxP = dst.scanLine(rowswritten++) + chIndex;
				unsigned char *nxPEnd = nxP + newcols * nChannels;
				fraccoltofill = SCALE;
				p = HALFSCALE;
				needcol = 0;
				for (col = 0, xP = tempxelrow + chIndex; col < cols; ++col, xP += nChannels)
				{
					fraccolleft = sxscale;
					while (fraccolleft >= fraccoltofill)
					{
						if (needcol)
						{
							nxP += nChannels;
							p = HALFSCALE;
						}
						p += fraccoltofill * (*xP);
						p /= SCALE;
						if (p > maxval)
							p = maxval;
						*nxP = (unsigned char) p;
						fraccolleft -= fraccoltofill;
						fraccoltofill = SCALE;
						needcol = 1;
					}
					if (fraccolleft > 0)
					{
						if (needcol)
						{
							nxP += nChannels;
							p = HALFSCALE;
							needcol = 0;
						}
						p += fraccolleft * (*xP);
						fraccoltofill -= fraccolleft;
					}
				}
				if (fraccoltofill > 0)
				{
					xP -= nChannels;
					p += fraccoltofill * (*xP);
				}
				if (nxP < nxPEnd)
				{
					p /= SCALE;
					if (p > maxval)
						p = maxval;
					*nxP = (unsigned char) p;
					while ((nxP += nChannels) != nxPEnd)
						nxP[0] = nxP[-nChannels];
				}
			}
		}
	}
	if (newrows != rows && tempxelrow)// Robust, tempxelrow might be 0 1 day
		delete [] tempxelrow;
	delete [] ps;

	int scanWidth = dst.width() * nChannels;
	QImage::operator=(QImage(nwidth, nheight, this->format()));
	for (int yi = 0; yi < dst.height(); ++yi)
	{
		uchar *s = dst.scanLine(yi);
		uchar *d = scanLine(yi);
		memcpy(d, s, scanWidth);
	}
}

bool ScImage::getAlpha(const QString& fn, int page, QByteArray& alpha, bool PDF, bool pdf14, int gsRes, int scaleXSize, int scaleYSize)
{
	bool gotAlpha = false;
	QScopedPointer<ScImgDataLoader> pDataLoader;
	imgInfo.valid = false;
	imgInfo.clipPath.clear();
	imgInfo.PDSpathData.clear();
	imgInfo.layerInfo.clear();
	QFileInfo fi(fn);
	if (!fi.exists())
		return false;
	alpha.resize(0);
	QString ext = fi.suffix().toLower();
	QString ext2 = getImageType(fn);
	if (ext.isEmpty() || (!ext2.isEmpty() && (ext2 != ext)))
		ext = ext2;
	QList<QByteArray> fmtList = QImageReader::supportedImageFormats();
	QStringList fmtImg;
	for (int i = 0; i < fmtList.count(); i++)
	{
		fmtImg.append( QString(fmtList[i].toLower()) );
	}
	if (extensionIndicatesJPEG(ext))
		return true;
	if (extensionIndicatesJPEGXL(ext))
		return true;
	if (extensionIndicatesPDF(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_PDF() );
	}
	else if (extensionIndicatesEPSorPS(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_PS() );
	}
	else if (extensionIndicatesPNG(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_PNG() );
		if	(pDataLoader.data())
			pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (extensionIndicatesPSD(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_PSD() );
		if	(pDataLoader.data())
			pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (extensionIndicatesTIFF(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_TIFF() );
		if	(pDataLoader.data())
			pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (ext == "ora")
	{
		pDataLoader.reset( new ScImgDataLoader_ORA() );
		if	(pDataLoader.data())
			pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (ext == "kra")
	{
		pDataLoader.reset( new ScImgDataLoader_KRA() );
		if	(pDataLoader.data())
			pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (ext == "pat")
		pDataLoader.reset( new ScImgDataLoader_GIMP() );
	else if (ext == "pgf")
		pDataLoader.reset( new ScImgDataLoader_PGF() );
	else if ((ext == "pct") || (ext == "pic") || (ext == "pict"))
		pDataLoader.reset( new ScImgDataLoader_PICT() );
	else if (ext == "wpg")
		pDataLoader.reset( new ScImgDataLoader_WPG() );
#ifdef GMAGICK_FOUND
#warning "Compiling with GraphicsMagick support!"
	else if (fmtImg.contains(ext))
		pDataLoader.reset( new ScImgDataLoader_QT() );
	else
		pDataLoader.reset( new ScImgDataLoader_GMagick() );
#else
	else
		pDataLoader.reset( new ScImgDataLoader_QT() );
#endif

	if (pDataLoader.data())
	{
		bool hasAlpha    = false;
		bool alphaLoaded = pDataLoader->preloadAlphaChannel(fn, page, gsRes, hasAlpha);
		if (!alphaLoaded || !hasAlpha)
			return alphaLoaded;
		QImage rImage;
		if (pDataLoader->useRawImage() || extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext))
		{
			if (pDataLoader->imageInfoRecord().valid)
			{
				if ((pDataLoader->r_image.channels() == 5) || (pDataLoader->imageInfoRecord().colorspace == ColorSpaceCMYK))
					rImage = pDataLoader->r_image.convertToQImage(true);
				else
					rImage = pDataLoader->r_image.convertToQImage(false);
			}
		}
		else
			rImage = pDataLoader->image();
		if (rImage.isNull())
			return false;
		if ((scaleXSize != 0) && (scaleYSize != 0) && (scaleXSize != rImage.width() || scaleYSize != rImage.height()))
			rImage = rImage.scaled(scaleXSize, scaleYSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		int i = 0, w2;
		unsigned char u;
		int hm = rImage.height();
		int wm = rImage.width();
		QRgb r, *s;
		if (pdf14)
		{
			alpha.resize(hm * wm);
			if (alpha.size() > 0) // 
			{
				for (int yi = 0; yi < hm; ++yi)
				{
					s = (QRgb*) rImage.scanLine(yi);
					for (int xi = 0; xi < wm; ++xi)
					{
						r = *s++;
						u = qAlpha(r);
						alpha[i++] = u;
					}
				}
				gotAlpha = true;
			}
		}
		else
		{
			uchar * s;
			QImage iMask = rImage.createAlphaMask();
			iMask = iMask.convertToFormat(QImage::Format_Mono);
			hm = iMask.height();
			wm = iMask.width();
			w2 = wm / 8;
			if ((wm % 8) != 0)
				w2++;
			alpha.resize(hm * w2);
			if (alpha.size() > 0)
			{
				for (int yi = 0; yi < hm; ++yi)
				{
					s = iMask.scanLine( yi );
					for (int xi = 0; xi < w2; ++xi)
					{
						u = *(s + xi);
						if (PDF) u = ~u;
						alpha[i++] = u;
					}
				}
				gotAlpha = true;
			}
		}
	}
	return gotAlpha;
}

void ScImage::getEmbeddedProfile(const QString & fn, QByteArray *profile, int *components, int page)
{
	Q_ASSERT(profile);
	Q_ASSERT(components);
	ScColorProfile prof;
	ScImgDataLoader* pDataLoader = nullptr;

	profile->resize(0);
	*components = 0;

	QFileInfo fi(fn);
	if (!fi.exists())
		return;

	QString ext = fi.suffix().toLower();
	QString ext2 = getImageType(fn);
	if (ext.isEmpty() || (!ext2.isEmpty() && (ext2 != ext)))
		ext = ext2;

	QList<QByteArray> fmtList = QImageReader::supportedImageFormats();
	QStringList fmtImg;
	for (int i = 0; i < fmtList.count(); i++)
		fmtImg.append( QString(fmtList[i].toLower()) );

	if (extensionIndicatesPSD(ext))
		pDataLoader = new ScImgDataLoader_PSD();
	else if (extensionIndicatesEPSorPS(ext))
		pDataLoader = new ScImgDataLoader_PS();
	else if (extensionIndicatesJPEG(ext))
		pDataLoader = new ScImgDataLoader_JPEG();
	else if (extensionIndicatesPNG(ext))
		pDataLoader = new ScImgDataLoader_PNG();
	else if (extensionIndicatesTIFF(ext))
		pDataLoader = new ScImgDataLoader_TIFF();
#ifdef GMAGICK_FOUND
	else if (fmtImg.contains(ext))
		pDataLoader = new ScImgDataLoader_QT();
	else
		pDataLoader = new ScImgDataLoader_GMagick();
#else
	else
		pDataLoader = new ScImgDataLoader_QT();
#endif

	if (pDataLoader)
	{
		pDataLoader->loadEmbeddedProfile(fn, page);
		QByteArray embeddedProfile = pDataLoader->embeddedProfile();
		if	(!embeddedProfile.isEmpty())
		{
			prof = ScCore->defaultEngine.openProfileFromMem(embeddedProfile);
			if (prof)
			{
				if (prof.colorSpace() == ColorSpace_Rgb)
					*components = 3;
				if (prof.colorSpace() == ColorSpace_Cmyk)
					*components = 4;
				if (prof.colorSpace() == ColorSpace_Gray)
					*components = 1;
				*profile = embeddedProfile;
			}
		}
		delete pDataLoader;
	}
}

void ScImage::addProfileToCacheModifiers(ScImageCacheProxy & cache, const QString & prefix, const ScColorProfile & profile) const
{
	if (profile)
	{
		cache.addModifier(prefix + "ProfileDescription", profile.productDescription());
		QString hash = profile.dataHash();
		if (!hash.isEmpty())
			cache.addModifier(prefix + "ProfileHash", hash);
	}
}

bool ScImage::loadPicture(ScImageCacheProxy & cache, bool & fromCache, int page, const CMSettings& cmSettings,
						  RequestType requestType, int gsRes, bool *realCMYK, bool showMsg)
{
	if (cache.enabled())
	{
		ScColorMgmtEngine engine(cmSettings.doc() ? cmSettings.doc()->colorEngine : ScCore->defaultEngine);
		cache.addModifier("cmEngineID", QString::number(engine.engineID()));
		cache.addModifier("cmEngineDescription", engine.description());
		cache.addModifier("useEmbeddedProfile", QString::number(static_cast<int>(cmSettings.useEmbeddedProfile())));
		cache.addModifier("softProofingAllowed", QString::number(static_cast<int>(cmSettings.softProofingAllowed())));
		cache.addModifier("requestType", QString::number(static_cast<int>(requestType)));
		cache.addModifier("gsRes", QString::number(gsRes));
		cache.addModifier("useColorManagement", QString::number(static_cast<int>(cmSettings.useColorManagement())));
		cache.addModifier("doSoftProofing", QString::number(static_cast<int>(cmSettings.doSoftProofing())));
		cache.addModifier("doGamutCheck", QString::number(static_cast<int>(cmSettings.doGamutCheck())));
		cache.addModifier("useBlackPoint", QString::number(static_cast<int>(cmSettings.useBlackPoint())));
		cache.addModifier("imageRenderingIntent", QString::number(static_cast<int>(cmSettings.imageRenderingIntent())));
		addProfileToCacheModifiers(cache, "monitor", cmSettings.monitorProfile());
		addProfileToCacheModifiers(cache, "printer", cmSettings.printerProfile());

		fromCache = imgInfo.lowResType != 0 && cache.canUseCachedImage() && cache.load(*this) && imgInfo.deserialize(cache);

		if (fromCache)
		{
			cache.touch();
			return true;
		}
	}
	else
		fromCache = false;

	return loadPicture(cache.getFilename(), page, cmSettings, requestType, gsRes, realCMYK, showMsg);
}

bool ScImage::saveCache(ScImageCacheProxy & cache)
{
	return cache.enabled() && imgInfo.serialize(cache) && cache.save(*this);
}

bool ScImage::loadPicture(const QString & fn, int page, const CMSettings& cmSettings,
						  RequestType requestType, int gsRes, bool *realCMYK, bool showMsg)
{
	// requestType - 0: CMYK, 1: RGB, 3 : RawData, 4: Thumbnail
	// gsRes - is the resolution that ghostscript will render at
	bool isCMYK = false;
	bool ret = false;
//	bool inputProfIsEmbedded = false;
	if (realCMYK != nullptr)
		*realCMYK = false;
	bool bilevel = false;
	RequestType reqType = requestType;
	int cmsFlags = 0;
	int cmsProofFlags = 0;

	QString profileName;
	bool hasEmbeddedProfile = false;
	ScColorTransform xform;
	ScColorProfile inputProf;

	QFileInfo fi(fn);
	if (!fi.exists())
		return ret;
	QString ext = fi.suffix().toLower();

	QStringList fmtImg;
	QList<QByteArray> fmtList = QImageReader::supportedImageFormats();
	for (int i = 0; i < fmtList.count(); i++)
		fmtImg.append( QString(fmtList[i].toLower()) );

	// Do some basic checks when requestType is OutputProfile
	if (requestType == OutputProfile)
	{
		if (!cmSettings.useColorManagement())
			return false;
		ScColorProfile prof = cmSettings.outputProfile();
		if (prof.isNull())
			return false;
		eColorSpaceType cspace = prof.colorSpace();
		if (cspace != ColorSpace_Rgb && cspace != ColorSpace_Cmyk)
			return false;
	}
	QString ext2 = getImageType(fn);
	if (ext.isEmpty() || (!ext2.isEmpty() && (ext2 != ext)))
		ext = ext2;

	QScopedPointer<ScImgDataLoader> pDataLoader;
	if (extensionIndicatesPDF(ext))
		pDataLoader.reset( new ScImgDataLoader_PDF() );
	else if (extensionIndicatesEPSorPS(ext))
		pDataLoader.reset( new ScImgDataLoader_PS() );
	else if (extensionIndicatesJPEG(ext))
		pDataLoader.reset( new ScImgDataLoader_JPEG() );
	#ifdef HAVE_JXL
	else if (extensionIndicatesJPEGXL(ext))
		pDataLoader.reset( new ScImgDataLoader_JPEGXL() );
	#endif
	else if (extensionIndicatesPNG(ext))
		pDataLoader.reset( new ScImgDataLoader_PNG() );
	else if (extensionIndicatesPSD(ext))
	{
		pDataLoader.reset( new ScImgDataLoader_PSD() );
		pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (extensionIndicatesTIFF(ext))
		pDataLoader.reset( new ScImgDataLoader_TIFF() );
	else if (ext == "pat")
		pDataLoader.reset( new ScImgDataLoader_GIMP() );
	else if (ext == "pgf")
		pDataLoader.reset( new ScImgDataLoader_PGF() );
	else if ((ext == "pct") || (ext == "pic") || (ext == "pict"))
		pDataLoader.reset( new ScImgDataLoader_PICT() );
	else if (ext == "wpg")
		pDataLoader.reset( new ScImgDataLoader_WPG() );
	else if (ext == "ora")
	{
		pDataLoader.reset( new ScImgDataLoader_ORA() );
		pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
	else if (ext == "kra")
	{
		pDataLoader.reset( new ScImgDataLoader_KRA() );
		pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
#ifdef GMAGICK_FOUND
	else if (fmtImg.contains(ext))
		pDataLoader.reset( new ScImgDataLoader_QT() );
	else
	{
		pDataLoader.reset( new ScImgDataLoader_GMagick() );
		pDataLoader->setRequest(imgInfo.isRequest, imgInfo.RequestProps);
	}
#else
	else
		pDataLoader.reset( new ScImgDataLoader_QT() );
#endif

	if (pDataLoader->loadPicture(fn, page, gsRes, (requestType == Thumbnail)))
	{
		QImage::operator=(pDataLoader->image());
		imgInfo = pDataLoader->imageInfoRecord();
		if (requestType == Thumbnail)
			reqType = RGBData;
	//	if (!cmSettings.useColorManagement() || !useProf)
	//	{
	//		imgInfo.isEmbedded = false;
	//		imgInfo.profileName = "";
	//	}
		if (imgInfo.colorspace == ColorSpaceCMYK)
		{
			isCMYK = true;
			if (realCMYK)
				*realCMYK = true;
		}
		else if (imgInfo.colorspace == ColorSpaceGray)
			bilevel = true;
		pDataLoader->image() = QImage();
	}
	else
	{
		ScribusDoc* doc = cmSettings.doc();
		if (!doc && ScCore->primaryMainWindow())
			doc = ScCore->primaryMainWindow()->doc;
		if (doc)
		{
			DocumentLogManager::instance().addLog(doc->uuidString(),
												  DocumentLogLevel::Error, "Image Loader",
												  pDataLoader->issuedErrorMsg() ? pDataLoader->getMessage()
																				: DocumentLogManager::msgFileImportFailed(fn));
		}
		if	(ScCore->usingGUI() && pDataLoader->issuedErrorMsg() && showMsg)
		{
			ScMessageBox::critical(ScCore->primaryMainWindow(), CommonStrings::trWarning, pDataLoader->getMessage());
		}
		else if (pDataLoader->issuedErrorMsg())
		{
			QString msg = pDataLoader->getMessage();
			qWarning("%s", msg.toLocal8Bit().data() );
		}
		return false;
	}

	if (!(extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())) //TODO: Unsure about this one!
	{
		if (isNull())
			return  ret;
	}

	QByteArray embeddedProfile = pDataLoader->embeddedProfile();
	if (cmSettings.useColorManagement())
	{
		if ((embeddedProfile.size() > 0 ) && (cmSettings.useEmbeddedProfile()))
		{
			inputProf = cmSettings.doc()->colorEngine.openProfileFromMem(embeddedProfile);
		//	inputProfIsEmbedded = true;
		}
		else
		{
			QString profilePath;
			//CB If this is null, customfiledialog/picsearch/ScPreview might be sending it
			Q_ASSERT(cmSettings.doc() != nullptr);
			if (isCMYK)
			{
				if (ScCore->InputProfilesCMYK.contains(cmSettings.profileName()) && (cmSettings.profileName() != cmSettings.doc()->cmsSettings().DefaultImageCMYKProfile))
				{
					imgInfo.profileName = cmSettings.profileName();
				//	inputProfIsEmbedded = true;
					profilePath = ScCore->InputProfilesCMYK[imgInfo.profileName].file;
					inputProf =  cmSettings.doc()->colorEngine.openProfileFromFile(profilePath);
				}
				else
				{
					inputProf = cmSettings.doc()->DocInputImageCMYKProf;
					imgInfo.profileName = cmSettings.doc()->cmsSettings().DefaultImageCMYKProfile;
				//	inputProfIsEmbedded = false;
				}
			}
			else if (bilevel && (reqType == CMYKData))
				inputProf = nullptr; // Workaround to map directly gray to K channel
			else if (ScCore->InputProfiles.contains(cmSettings.profileName()) && (cmSettings.profileName() != cmSettings.doc()->cmsSettings().DefaultImageRGBProfile))
			{
				imgInfo.profileName = cmSettings.profileName();
				profilePath = ScCore->InputProfiles[imgInfo.profileName].file;
			//	inputProfIsEmbedded = true;
				inputProf = cmSettings.doc()->colorEngine.openProfileFromFile(profilePath);
			}
			else
			{
				inputProf = cmSettings.doc()->DocInputImageRGBProf;
				imgInfo.profileName = cmSettings.doc()->cmsSettings().DefaultImageRGBProfile;
			//	inputProfIsEmbedded = false;
			}
		}
	}
	else if ((cmSettings.useColorManagement() && embeddedProfile.size() > 0) && (cmSettings.useEmbeddedProfile()))
	{
		inputProf = cmSettings.doc()->colorEngine.openProfileFromMem(embeddedProfile);
	//	inputProfIsEmbedded = true;
	}
	else if (cmSettings.colorManagementAllowed() && isCMYK)
		inputProf = ScCore->defaultCMYKProfile;
	else if (cmSettings.colorManagementAllowed() && bilevel && (reqType == CMYKData))
		inputProf = nullptr; // Workaround to map directly gray to K channel
	else if (cmSettings.colorManagementAllowed())
		inputProf = ScCore->defaultRGBProfile;
	ScColorProfile screenProf  = cmSettings.monitorProfile() ? cmSettings.monitorProfile() : ScCore->defaultRGBProfile;
	ScColorProfile printerProf = cmSettings.printerProfile() ? cmSettings.printerProfile() : ScCore->defaultCMYKProfile;
	if (cmSettings.colorManagementAllowed() && inputProf && screenProf && printerProf)
	{
		ScColorMgmtEngine engine(cmSettings.doc() ? cmSettings.doc()->colorEngine : ScCore->defaultEngine);
		eColorFormat inputProfFormat  = pDataLoader->pixelFormat();
		eColorFormat outputProfFormat = Format_YMCK_8;
		eColorSpaceType inputProfColorSpace  = inputProf.colorSpace();
		eColorSpaceType outputProfColorSpace = printerProf.colorSpace();
		if (inputProfColorSpace == ColorSpace_Gray)
			inputProfFormat  = Format_GRAY_8; // Grayscale is still a bit tricky
		if (outputProfColorSpace == ColorSpace_Rgb)
			outputProfFormat = Format_BGRA_8;
		else if (outputProfColorSpace == ColorSpace_Cmyk)
			outputProfFormat = Format_YMCK_8;
		ScColorSpace inputCSpace  = engine.createColorSpace(inputProf, inputProfFormat);
		ScColorSpace screenCSpace = engine.createColorSpace(screenProf, Format_BGRA_8);
		ScColorSpace outputCSpace;
		if (cmSettings.useColorManagement() && cmSettings.doSoftProofing())
		{
			cmsProofFlags |= Ctf_Softproofing;
			if (cmSettings.doGamutCheck())
			{
				cmsProofFlags |= Ctf_GamutCheck;
			}
		}
		if (!cmSettings.useColorManagement() || cmSettings.useBlackPoint())
			cmsFlags |= Ctf_BlackPointCompensation;
		switch (reqType)
		{
		case CMYKData: // CMYK
//			if ((!isCMYK && (outputProfColorSpace == icSigCmykData)) || (isCMYK && (outputProfColorSpace == icSigRgbData)) )
				xform = inputCSpace.createTransform(printerProf, outputProfFormat, cmSettings.imageRenderingIntent(), cmsFlags);
			if (outputProfColorSpace != ColorSpace_Cmyk )
				*realCMYK = isCMYK = false;
			outputCSpace = engine.createColorSpace(printerProf, outputProfFormat);
			break;
		case Thumbnail:
		case RGBData: // RGB
			if (cmSettings.useColorManagement() && cmSettings.doSoftProofing())
			{
				if ((imgInfo.profileName == cmSettings.defaultImageRGBProfile()) || (imgInfo.profileName == cmSettings.defaultImageCMYKProfile()))
				{
					if (isCMYK && (inputProfFormat == Format_CMYK_8))
						xform = cmSettings.cmykImageProofingTransform();
					else if (inputProfFormat == Format_BGRA_8)
						xform = cmSettings.rgbImageProofingTransform();
				}
				if (!xform)
				{
					xform = inputCSpace.createProofingTransform(screenCSpace, printerProf,
				                     cmSettings.intent(), Intent_Relative_Colorimetric, cmsFlags | cmsProofFlags);
				}
			}
			else if (cmSettings.softProofingAllowed() || isCMYK)
				xform = inputCSpace.createTransform(screenCSpace, cmSettings.intent(), cmsFlags);
			else
			{
				if (pDataLoader->useRawImage())
				{
					QImage::operator=(pDataLoader->r_image.convertToQImage(false));
					profileName = imgInfo.profileName;
					hasEmbeddedProfile = imgInfo.isEmbedded;
					imgInfo = pDataLoader->imageInfoRecord();
					imgInfo.profileName = profileName;
					imgInfo.isEmbedded = hasEmbeddedProfile;
					// JG : this line overwrite image profile info and should not be needed here!!!!
					// imgInfo = pDataLoader->imageInfoRecord();
				}
			}
			outputProfColorSpace = ColorSpace_Rgb;
			outputCSpace = screenCSpace;
			break;
		case RawData: // no Conversion just raw Data
			xform = nullptr;
			if (pDataLoader->useRawImage())
			{
				QImage::operator=(pDataLoader->r_image.convertToQImage(true, true));
				profileName = imgInfo.profileName;
				hasEmbeddedProfile = imgInfo.isEmbedded;
				imgInfo = pDataLoader->imageInfoRecord();
				imgInfo.profileName = profileName;
				imgInfo.isEmbedded = hasEmbeddedProfile;
				// JG : this line overwrite image profile info and should not be needed here!!!!
				// imgInfo = pDataLoader->imageInfoRecord();
			}
			break;
		case OutputProfile: // CMYK
			ScColorProfile outputProfile = cmSettings.outputProfile();
			outputProfColorSpace = outputProfile.colorSpace();
			if ( outputProfColorSpace == ColorSpace_Rgb )
				outputProfFormat = Format_BGRA_8;
			else if ( outputProfColorSpace == ColorSpace_Cmyk )
				outputProfFormat = Format_YMCK_8;
			xform = inputCSpace.createTransform(outputProfile, outputProfFormat, cmSettings.imageRenderingIntent(), cmsFlags);
			isCMYK = (outputProfColorSpace == ColorSpace_Cmyk);
			if (realCMYK)
				*realCMYK = isCMYK;
			outputCSpace = engine.createColorSpace(outputProfile, outputProfFormat);
			break;
		}
		if (xform && outputCSpace)
		{
			if (pDataLoader->useRawImage())
			{
				QImage::operator=(QImage(pDataLoader->r_image.width(), pDataLoader->r_image.height(), QImage::Format_ARGB32));
				profileName = imgInfo.profileName;
				hasEmbeddedProfile = imgInfo.isEmbedded;
				imgInfo = pDataLoader->imageInfoRecord();
				imgInfo.profileName = profileName;
				imgInfo.isEmbedded = hasEmbeddedProfile;
				// JG : this line overwrite image profile info and should not be needed here!!!!
				// imgInfo = pDataLoader->imageInfoRecord();
			}
			uchar* ptr2 = nullptr;
			for (int i = 0; i < height(); i++)
			{
				uchar* ptr = scanLine(i);
				ptr2 = pDataLoader->useRawImage() ? pDataLoader->r_image.scanLine(i) : nullptr;
				if ((inputProfFormat == Format_GRAY_8) && (outputProfColorSpace != ColorSpace_Cmyk))
				{
					unsigned char* ucs = ptr2 ? (ptr2 + 1) : (ptr + 1);
					unsigned char* uc = new unsigned char[width()];
					for (int uci = 0; uci < width(); ++uci)
					{
						uc[uci] = *ucs;
						ucs += 4;
					}
					xform.apply(uc, ptr, width());
					delete[] uc;
				}
				else if ((inputProfFormat == Format_GRAY_8) && (outputProfColorSpace == ColorSpace_Cmyk))
				{
					unsigned char  value;
					unsigned char* ucs = ptr2 ? ptr2 : ptr;
					unsigned char* uc  = ptr;
					for (int uci = 0; uci < width(); ++uci, uc += 4)
					{
						value = 255 - *(ucs + 1);
						uc[0] = uc[1] = uc[2] = 0;
						uc[3] = value;
						ucs += 4;
					}
				}
				else
				{
					inputCSpace.convert(outputCSpace, (eRenderIntent) 0, 0, ptr2 ? ptr2 : ptr, ptr, width(), &xform);
				}
				if (pDataLoader->useRawImage())
				{
					// This might fix Bug #6328, please test.
					/*if (outputProfColorSpace != ColorSpace_Cmyk && bilevel)
					{
						QRgb alphaFF;
						QRgb *p;
						p = (QRgb *) ptr;
						for (int j = 0; j < width(); j++, p++)
						{
							alphaFF = qRgba(0,0,0,ptr2[3]);
							*p |= alphaFF;
							ptr2 += 4;
						}
					}*/
					// FIXME not valid if input or output colorspace are not 8bit / channels
					if (inputCSpace.hasAlphaChannel() && outputCSpace.hasAlphaChannel())
					{
						uint inputAlphaI  = inputCSpace.alphaIndex();
						uint outputAlphaI = outputCSpace.alphaIndex();
						uint inputBytes   = inputCSpace.bytesPerChannel()  * inputCSpace.numChannels();
						uint outputBytes  = outputCSpace.bytesPerChannel() * outputCSpace.numChannels();
						uchar* in  = ptr2 + inputAlphaI  * inputCSpace.bytesPerChannel();
						uchar* out = ptr  + outputAlphaI * outputCSpace.bytesPerChannel();
						for (int j = 0; j < width(); ++j)
						{
							*out = *in;
							in  += inputBytes;
							out += outputBytes;
						}
					}
				}
			}
		}
	}
	else
	{
		switch (reqType)
		{
		case CMYKData:
			if (!isCMYK)
			{
				if (extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())
				{
					QImage::operator=(pDataLoader->r_image.convertToQImage(false));
					profileName = imgInfo.profileName;
					hasEmbeddedProfile = imgInfo.isEmbedded;
					imgInfo = pDataLoader->imageInfoRecord();
					imgInfo.profileName = profileName;
					imgInfo.isEmbedded = hasEmbeddedProfile;
					// JG : this line overwrite image profile info and should not be needed here!!!!
					// imgInfo = pDataLoader->imageInfoRecord();
				}
				unsigned char cc, cm, cy ,ck;
				QRgb *ptr;
				for (int i = 0; i < height(); i++)
				{
					ptr = (QRgb *) scanLine(i);
					for (int j = 0; j < width(); j++)
					{
						cc = 255 - qRed(*ptr);
						cm = 255 - qGreen(*ptr);
						cy = 255 - qBlue(*ptr);
						ck = qMin(qMin(cc, cm), cy);
						*ptr++ = qRgba(cc-ck,cm-ck,cy-ck,ck);
					}
				}
			}
			else
			{
				if (extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())
				{
					QImage::operator=(pDataLoader->r_image.convertToQImage(true, true));
					profileName = imgInfo.profileName;
					hasEmbeddedProfile = imgInfo.isEmbedded;
					imgInfo = pDataLoader->imageInfoRecord();
					imgInfo.profileName = profileName;
					imgInfo.isEmbedded = hasEmbeddedProfile;
					// JG : this line overwrite image profile info and should not be needed here!!!!
					// imgInfo = pDataLoader->imageInfoRecord();
				}
			}
			break;
		case RGBData:
		case Thumbnail:
			if (isCMYK)
			{
				if (extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())
				{
					QImage::operator=(pDataLoader->r_image.convertToQImage(true));
					profileName = imgInfo.profileName;
					hasEmbeddedProfile = imgInfo.isEmbedded;
					imgInfo = pDataLoader->imageInfoRecord();
					imgInfo.profileName = profileName;
					imgInfo.isEmbedded = hasEmbeddedProfile;
					// JG : this line overwrite image profile info and should not be needed here!!!!
					// imgInfo = pDataLoader->imageInfoRecord();
				}
				else
				{
					unsigned char cr, cg, cb, ck;
					QRgb *ptr;
					for (int i = 0; i < height(); i++)
					{
						ptr = (QRgb *) scanLine(i);
						for (int j = 0; j < width(); j++)
						{
							ck = qAlpha(*ptr);
							cr = 255 - qMin(255, qRed(*ptr) + ck);
							cg = 255 - qMin(255, qGreen(*ptr) + ck);
							cb = 255 - qMin(255, qBlue(*ptr) + ck);
							*ptr++ = qRgba(cr,cg,cb,255);
						}
					}
				}
			}
			else
			{
				if (extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())
				{
					QImage::operator=(pDataLoader->r_image.convertToQImage(false));
					profileName = imgInfo.profileName;
					hasEmbeddedProfile = imgInfo.isEmbedded;
					imgInfo = pDataLoader->imageInfoRecord();
					imgInfo.profileName = profileName;
					imgInfo.isEmbedded = hasEmbeddedProfile;
					// JG : this line overwrite image profile info and should not be needed here!!!!
					// imgInfo = pDataLoader->imageInfoRecord();
				}
			}
			break;
		case RawData:
			if (extensionIndicatesPSD(ext) || extensionIndicatesTIFF(ext) || pDataLoader->useRawImage())
			{
				QImage::operator=(pDataLoader->r_image.convertToQImage(true, true));
				profileName = imgInfo.profileName;
				hasEmbeddedProfile = imgInfo.isEmbedded;
				imgInfo = pDataLoader->imageInfoRecord();
				imgInfo.profileName = profileName;
				imgInfo.isEmbedded = hasEmbeddedProfile;
				// JG : this line overwrite image profile info and should not be needed here!!!!
				// imgInfo = pDataLoader->imageInfoRecord();
			}
			break;
		default:	// just to silence compiler
			break;
		}
	}
//	if (((reqType == CMYKData) || ((reqType == RawData) && isCMYK)) && !bilevel)
//		setAlphaBuffer(false);
	setDotsPerMeterX (qMax(2834, (int) (imgInfo.xres / 0.0254)));
	setDotsPerMeterY (qMax(2834, (int) (imgInfo.yres / 0.0254)));
	imgInfo.isEmbedded &= cmSettings.useEmbeddedProfile();
	if	(ScCore->usingGUI() && pDataLoader->issuedWarningMsg() && showMsg)
	{
		ScMessageBox::warning(ScCore->primaryMainWindow(), CommonStrings::trWarning, pDataLoader->getMessage());
	}
	else if (pDataLoader->issuedWarningMsg())
	{
		QString msg = pDataLoader->getMessage();
		qWarning("%s", msg.toLocal8Bit().data() );
	}
	return true;
}

bool ScImage::hasSmoothAlpha() const
{
	int h = height();
	int w = width();
	QSet<int> alpha;
	const QRgb *s;
	for (int yi = 0; yi < h; ++yi)
	{
		s = (const QRgb*) constScanLine(yi);
		for (int xi = 0; xi < w; ++xi)
		{
			alpha.insert(qAlpha(*s));
			if (alpha.count() > 2)
				return true;
			s++;
		}
	}
	return (alpha.count() > 2);
}
