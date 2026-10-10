/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerbgremove.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <QTransform>

#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "pagestructs.h"

namespace SuneerBgRemove
{
	const char* const AttrSource    = "SuneerBgSource";
	const char* const AttrMode      = "SuneerBgMode";
	const char* const AttrSelection = "SuneerBgSelection";
	const char* const AttrBrush     = "SuneerBgBrush";
	const char* const AttrCut       = "SuneerBgCut";
	const char* const AttrCutRect   = "SuneerBgCutRect";
	const char* const AttrModel     = "SuneerBgModel";
	const char* const AttrFeather   = "SuneerBgFeather";
}

QString SuneerBgRemove::modeName(Mode m)
{
	switch (m)
	{
		case Mode::Inside:      return QStringLiteral("inside");
		case Mode::KeepSubject: return QStringLiteral("keep");
		default:                return QStringLiteral("whole");
	}
}

SuneerBgRemove::Mode SuneerBgRemove::modeFromName(const QString& s)
{
	if (s == QLatin1String("inside")) return Mode::Inside;
	if (s == QLatin1String("keep"))   return Mode::KeepSubject;
	return Mode::Whole;
}

QString SuneerBgRemove::encodePath(const QPainterPath& path)
{
	QStringList polys;
	const QList<QPolygonF> list = path.toSubpathPolygons();
	for (const QPolygonF& poly : list)
	{
		if (poly.size() < 3)
			continue;
		QStringList pts;
		for (const QPointF& p : poly)
			pts << QString::number(p.x(), 'f', 1) + "," + QString::number(p.y(), 'f', 1);
		polys << pts.join(' ');
	}
	return polys.join(';');
}

QPainterPath SuneerBgRemove::decodePath(const QString& s)
{
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	const QStringList polys = s.split(';', Qt::SkipEmptyParts);
	for (const QString& ps : polys)
	{
		QPolygonF poly;
		const QStringList pts = ps.split(' ', Qt::SkipEmptyParts);
		for (const QString& pt : pts)
		{
			const QStringList xy = pt.split(',');
			if (xy.size() == 2)
				poly << QPointF(xy[0].toDouble(), xy[1].toDouble());
		}
		if (poly.size() >= 3)
		{
			path.addPolygon(poly);
			path.closeSubpath();
		}
	}
	return path;
}

QImage SuneerBgRemove::maskFromPath(const QPainterPath& path, const QSize& size)
{
	if (path.isEmpty() || size.isEmpty())
		return QImage();
	QImage mask(size, QImage::Format_Alpha8);
	mask.fill(0);
	QPainter p(&mask);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 255));
	p.drawPath(path);
	p.end();
	return mask;
}

QRect SuneerBgRemove::regionFor(const QImage& selMask, Mode mode, const QSize& imageSize)
{
	const QRect whole(QPoint(0, 0), imageSize);
	if (mode == Mode::Whole || selMask.isNull())
		return whole;
	int minX = imageSize.width(), minY = imageSize.height(), maxX = -1, maxY = -1;
	for (int y = 0; y < selMask.height(); ++y)
	{
		const uchar* line = selMask.constScanLine(y);
		for (int x = 0; x < selMask.width(); ++x)
			if (line[x] > 0)
			{
				minX = qMin(minX, x); maxX = qMax(maxX, x);
				minY = qMin(minY, y); maxY = qMax(maxY, y);
			}
	}
	if (maxX < 0)
		return whole;
	QRect r(minX, minY, maxX - minX + 1, maxY - minY + 1);
	// A margin gives the model some context around the subject.
	const int margin = qMax(24, qMax(r.width(), r.height()) / 10);
	r.adjust(-margin, -margin, margin, margin);
	return r.intersected(whole);
}

QImage SuneerBgRemove::compose(const QImage& sourceIn, const QImage& cutIn, const QRect& region,
                               const QImage& selMask, Mode mode, const QImage& brush)
{
	QImage source = sourceIn.convertToFormat(QImage::Format_ARGB32);
	QImage cut = cutIn.convertToFormat(QImage::Format_ARGB32);
	const int w = source.width(), h = source.height();
	if (cut.size() != region.size())
		cut = cut.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QImage out = source;
	out.detach();
	for (int y = 0; y < h; ++y)
	{
		QRgb* oline = reinterpret_cast<QRgb*>(out.scanLine(y));
		const QRgb* sline = reinterpret_cast<const QRgb*>(source.constScanLine(y));
		const uchar* mline = selMask.isNull() ? nullptr : selMask.constScanLine(y);
		const uchar* bline = brush.isNull() ? nullptr : brush.constScanLine(y);
		for (int x = 0; x < w; ++x)
		{
			const QRgb s = sline[x];
			const int sel = mline ? mline[x] : 255;
			QRgb c = 0;
			bool inRegion = region.contains(x, y);
			if (inRegion)
				c = reinterpret_cast<const QRgb*>(cut.constScanLine(y - region.y()))[x - region.x()];
			QRgb r;
			if (mode == Mode::Whole)
				r = inRegion ? c : qRgba(qRed(s), qGreen(s), qBlue(s), 0);
			else if (mode == Mode::Inside)
			{
				if (sel == 0 || !inRegion)
					r = s;
				else
				{
					// Inside the selection the cut-out's alpha wins; the edge
					// of the selection blends by its coverage.
					const int a = (qAlpha(c) * sel + qAlpha(s) * (255 - sel)) / 255;
					r = qRgba(qRed(c), qGreen(c), qBlue(c), a);
				}
			}
			else  // KeepSubject
			{
				if (sel == 0 || !inRegion)
					r = qRgba(qRed(s), qGreen(s), qBlue(s), 0);
				else
					r = qRgba(qRed(c), qGreen(c), qBlue(c), (qAlpha(c) * sel) / 255);
			}
			if (bline)
			{
				const int b = bline[x];
				if (b < 128)
					r = qRgba(qRed(r), qGreen(r), qBlue(r), (qAlpha(r) * b) / 128);           // erased
				else if (b > 128)
				{
					const int k = (b - 128) * 255 / 127;                                        // restored from source
					const int a = (qAlpha(s) * k + qAlpha(r) * (255 - k)) / 255;
					r = qRgba(qRed(s), qGreen(s), qBlue(s), a);
				}
			}
			oline[x] = r;
		}
	}
	return out;
}

QString SuneerBgRemove::attr(const PageItem* item, const char* name)
{
	if (!item)
		return QString();
	const QList<ObjectAttribute> list = item->getObjectAttributes(QString::fromLatin1(name));
	return list.isEmpty() ? QString() : list.first().value;
}

void SuneerBgRemove::setAttr(PageItem* item, const char* name, const QString& value)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = attrs.count() - 1; i >= 0; --i)
		if (attrs.at(i).name == QString::fromLatin1(name))
			attrs.removeAt(i);
	if (!value.isEmpty())
	{
		ObjectAttribute a;
		a.name = QString::fromLatin1(name);
		a.type = QStringLiteral("string");
		a.value = value;
		attrs.append(a);
	}
	item->setObjectAttributes(&attrs);
}

void SuneerBgRemove::clearAttrs(PageItem* item)
{
	for (const char* n : { AttrSource, AttrMode, AttrSelection, AttrBrush, AttrCut, AttrCutRect, AttrModel, AttrFeather })
		setAttr(item, n, QString());
}

QPainterPath SuneerBgRemove::shapesToImagePath(const PageItem* frame, const QList<PageItem*>& shapes)
{
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	const auto* imgFrame = dynamic_cast<const PageItem_ImageFrame*>(frame);
	if (!imgFrame)
		return path;
	// item-local points of the frame -> full-resolution image pixels
	const QTransform frameToImage = imgFrame->imagePixelToLocal().inverted();
	const QTransform pageToFrame = frame->getTransform().inverted();
	for (const PageItem* shape : shapes)
	{
		if (!shape || shape == frame)
			continue;
		QPainterPath local = shape->PoLine.toQPainterPath(true);
		if (local.isEmpty())
			continue;
		const QTransform t = shape->getTransform() * pageToFrame * frameToImage;
		QPainterPath mapped = t.map(local);
		mapped.closeSubpath();
		path.addPath(mapped);
	}
	return path;
}

// ---------------------------------------------------------------------------

SuneerBgRemover::SuneerBgRemover(QObject* parent) : QObject(parent)
{
}

SuneerBgRemover::~SuneerBgRemover()
{
	cancel();
}

bool SuneerBgRemover::isRunning() const
{
	return m_proc && m_proc->state() != QProcess::NotRunning;
}

void SuneerBgRemover::cancel()
{
	if (!m_proc)
		return;
	m_cancelled = true;
	if (m_proc->state() != QProcess::NotRunning)
	{
		m_proc->kill();
		m_proc->waitForFinished(2000);
	}
	m_proc->deleteLater();
	m_proc = nullptr;
}

void SuneerBgRemover::start(const QImage& source, const QRect& region, const QString& model, int featherPx)
{
	cancel();
	m_cancelled = false;
	m_region = region.intersected(source.rect());
	if (!m_dir.isValid() || source.isNull() || m_region.isEmpty())
	{
		emit failed(tr("Nothing to process."));
		return;
	}
	m_inPath = m_dir.filePath("in.png");
	m_outPath = m_dir.filePath("out.png");
	QFile::remove(m_outPath);
	QImage crop = source.copy(m_region).convertToFormat(QImage::Format_ARGB32);
	if (!crop.save(m_inPath, "PNG"))
	{
		emit failed(tr("Could not write the temporary picture."));
		return;
	}
	const QString script = QString(
		"from rembg import remove, new_session\n"
		"from PIL import Image\n"
		"import PIL.ImageFilter as _IF\n"
		"session = new_session('%3')\n"
		"img = Image.open(r'%1').convert('RGBA')\n"
		"result = remove(img, session=session, alpha_matting=True, alpha_matting_foreground_threshold=240, alpha_matting_background_threshold=10, alpha_matting_erode_size=10)\n"
		"if %4 > 0:\n"
		"    r,g,b,a = result.split()\n"
		"    a = a.filter(_IF.GaussianBlur(radius=%4))\n"
		"    result = Image.merge('RGBA',(r,g,b,a))\n"
		"result.save(r'%2')\n"
	).arg(m_inPath, m_outPath, model).arg(featherPx);
	m_proc = new QProcess(this);
	connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &SuneerBgRemover::onProcessDone);
	m_proc->start("python3", QStringList() << "-c" << script);
}

void SuneerBgRemover::onProcessDone(int code, QProcess::ExitStatus status)
{
	if (m_cancelled || !m_proc)
		return;
	const QString err = QString::fromLocal8Bit(m_proc->readAllStandardError()).trimmed();
	QProcess* p = m_proc;
	m_proc = nullptr;
	p->deleteLater();
	if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(m_outPath))
	{
		emit failed(err.isEmpty() ? tr("rembg failed (exit %1)").arg(code) : err.section('\n', -3));
		return;
	}
	QImage cut(m_outPath);
	if (cut.isNull())
	{
		emit failed(tr("rembg wrote no readable picture."));
		return;
	}
	emit finished(cut.convertToFormat(QImage::Format_ARGB32));
}
