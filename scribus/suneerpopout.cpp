#include "suneerpopout.h"
#include "suneerfilltextimage.h"

#include <cmath>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRadioButton>
#include <QTimer>
#include <QUrlQuery>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QWheelEvent>
#include <QPushButton>
#include <QVector>

#include "commonstrings.h"
#include "sccolorengine.h"
#include "fpointarray.h"
#include "pageitem.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "undomanager.h"
#include "undostate.h"
#include "util_math.h"

namespace
{
const QString kAttr     = QStringLiteral("SuneerPopOut");
const QString kAttrOf   = QStringLiteral("SuneerPopOutOf");
const QString kAttrPath = QStringLiteral("SuneerPopOutPath");
const QString kSuffix   = QStringLiteral(" popout");
const int     kMaskMax  = 900;     // the mask is traced at most this many pixels wide/high

QString attributeValue(const PageItem* item, const QString& name)
{
	const ObjAttrVector* all = const_cast<PageItem*>(item)->getObjectAttributes();
	if (!all)
		return QString();
	for (const ObjectAttribute& a : *all)
		if (a.name == name)
			return a.value;
	return QString();
}

void setAttribute(PageItem* item, const QString& name, const QString& value)
{
	ObjAttrVector* all = item->getObjectAttributes();
	for (ObjectAttribute& a : *all)
		if (a.name == name) { a.value = value; return; }
	ObjectAttribute a;
	a.name = name;
	a.type = QStringLiteral("string");
	a.value = value;
	a.relationship = QStringLiteral("none");
	a.autoaddto = QStringLiteral("none");
	all->append(a);
}

void recordAttributes(PageItem* item, const ObjAttrVector& before)
{
	if (!UndoManager::undoEnabled())
		return;
	auto* is = new ScItemState<QPair<ObjAttrVector, ObjAttrVector> >(QObject::tr("Pop-out settings"));
	is->set("SUNEER_ITEM_ATTRIBUTES");
	is->setItem(qMakePair(before, *item->getObjectAttributes()));
	UndoManager::instance()->action(item, is);
}

PageItem* itemNamed(ScribusDoc* doc, const QString& name)
{
	for (PageItem* candidate : std::as_const(*doc->Items))
		if (candidate->itemName() == name)
			return candidate;
	return nullptr;
}

QString effectsDir(const QString& documentFile)
{
	const QFileInfo fi(documentFile);
	QString base = fi.fileName();
	for (const char* ext : { ".sla.gz", ".sla", ".scd.gz", ".scd" })
		if (base.endsWith(QLatin1String(ext), Qt::CaseInsensitive)) { base.chop(int(strlen(ext))); break; }
	return fi.absolutePath() + "/" + base + "_effects";
}

// ------------------------------------------------------------- the mask

// Runs rembg on the picture, once per picture: the alpha channel is kept as
// a grey PNG in the effects folder, keyed by the file's path, size and time.
bool ensureMask(ScribusMainWindow* mw, ScribusDoc* doc, const QString& imageFile, const QString& model,
                QString& maskFile, QString* error)
{
	const QFileInfo src(imageFile);
	if (!src.isReadable())
	{
		if (error) *error = QObject::tr("Cannot read the picture:\n%1").arg(imageFile);
		return false;
	}
	QDir dir(effectsDir(doc->documentFileName()));
	if (!dir.exists() && !dir.mkpath("."))
	{
		if (error) *error = QObject::tr("Cannot create the folder %1").arg(dir.absolutePath());
		return false;
	}
	const QByteArray key = QCryptographicHash::hash((src.absoluteFilePath() + "|" + QString::number(src.size()) + "|"
	                                                 + QString::number(src.lastModified().toSecsSinceEpoch()) + "|" + model).toUtf8(),
	                                                QCryptographicHash::Md5).toHex().left(12);
	maskFile = dir.absoluteFilePath(QStringLiteral("popout_%1_mask.png").arg(QString::fromLatin1(key)));
	if (QFileInfo::exists(maskFile))
		return true;

	const QString script = QString(
		"from rembg import remove, new_session\n"
		"from PIL import Image\n"
		"session = new_session('%3')\n"
		"img = Image.open(r'%1').convert('RGBA')\n"
		"out = remove(img, session=session, only_mask=True, post_process_mask=True)\n"
		"out.convert('L').save(r'%2')\n").arg(imageFile, maskFile, model);

	QProgressDialog dlg(QObject::tr("Finding the person in the picture (rembg)..."), QObject::tr("Cancel"), 0, 0, mw);
	dlg.setWindowTitle(QObject::tr("Pop-out Subject"));
	dlg.setWindowModality(Qt::WindowModal);
	dlg.setMinimumDuration(300);
	dlg.setAutoClose(false);
	QProcess proc;
	QEventLoop loop;
	QObject::connect(&proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
	QObject::connect(&dlg, &QProgressDialog::canceled, &loop, [&] { proc.kill(); });
	proc.start("python3", QStringList() << "-c" << script);
	if (!proc.waitForStarted(5000))
	{
		if (error) *error = QObject::tr("python3 could not be started.");
		return false;
	}
	loop.exec();
	dlg.reset();
	if (dlg.wasCanceled())
	{
		QFile::remove(maskFile);
		if (error) *error = QObject::tr("Cancelled.");
		return false;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 || !QFileInfo::exists(maskFile))
	{
		QFile::remove(maskFile);
		QString err = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
		if (err.length() > 600) err = "..." + err.right(600);
		if (error) *error = QObject::tr("rembg could not make the mask.\n%1").arg(err);
		return false;
	}
	return true;
}

// A binary mask at working size, with the scale back to picture pixels.
struct Mask
{
	int w { 0 }, h { 0 };
	double scale { 1.0 };         // picture pixels per mask pixel
	QVector<uchar> on;
	bool at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && on[y * w + x]; }
};

bool loadMask(const QString& file, Mask& m, QSize& pictureSize)
{
	QImage img(file);
	if (img.isNull())
		return false;
	pictureSize = img.size();
	const int longest = qMax(img.width(), img.height());
	m.scale = longest > kMaskMax ? double(longest) / kMaskMax : 1.0;
	QImage small = m.scale > 1.0 ? img.scaled(qMax(1, qRound(img.width() / m.scale)), qMax(1, qRound(img.height() / m.scale)),
	                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : img;
	small = small.convertToFormat(QImage::Format_Grayscale8);
	m.w = small.width();
	m.h = small.height();
	m.scale = double(img.width()) / m.w;
	m.on.resize(m.w * m.h);
	for (int y = 0; y < m.h; ++y)
	{
		const uchar* line = small.constScanLine(y);
		for (int x = 0; x < m.w; ++x)
			m.on[y * m.w + x] = line[x] >= 128 ? 1 : 0;
	}
	return true;
}

// Where the head ends: the mask narrows at the neck and widens again at the
// shoulders. The cut goes a little below the narrowest row between the two.
double headLine(const Mask& m)
{
	QVector<int> width(m.h, 0);
	int top = -1;
	for (int y = 0; y < m.h; ++y)
	{
		int n = 0;
		for (int x = 0; x < m.w; ++x)
			n += m.on[y * m.w + x];
		width[y] = n;
		if (n > 0 && top < 0)
			top = y;
	}
	if (top < 0)
		return m.h * 0.4 * m.scale;
	// head: the widest row in the first part of the figure
	int headMax = 0, headMaxY = top;
	for (int y = top; y < m.h; ++y)
	{
		if (width[y] > headMax) { headMax = width[y]; headMaxY = y; }
		// stop looking for the head once the figure is much wider than before (shoulders)
		if (y > top + 20 && width[y] > headMax * 1.35)
			break;
	}
	// neck: narrowest row after the widest head row
	int neckY = headMaxY, neckW = headMax;
	for (int y = headMaxY; y < m.h; ++y)
	{
		if (width[y] < neckW) { neckW = width[y]; neckY = y; }
		if (width[y] > neckW * 1.25 && y > neckY + 3)
			break;    // widening again: shoulders
		if (width[y] == 0)
			break;
	}
	if (neckY <= headMaxY)
		return (top + (m.h - top) * 0.35) * m.scale;
	// shoulder start
	int shoulderY = neckY;
	for (int y = neckY; y < m.h; ++y)
		if (width[y] > neckW * 1.25 || width[y] == 0) { shoulderY = y; break; }
	const double cut = neckY + (shoulderY - neckY) * 0.6;
	return cut * m.scale;
}

// ------------------------------------------------------------- tracing

// Links the unit edges between on and off pixels into closed loops, then
// simplifies each loop. Holes come out as loops of their own (even-odd fill).
struct Edge { QPoint a, b; };

double perpDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
	const double dx = b.x() - a.x(), dy = b.y() - a.y();
	const double len2 = dx * dx + dy * dy;
	if (len2 <= 0.0)
		return std::hypot(p.x() - a.x(), p.y() - a.y());
	const double t = qBound(0.0, ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2, 1.0);
	return std::hypot(p.x() - (a.x() + t * dx), p.y() - (a.y() + t * dy));
}

void douglasPeucker(const QVector<QPointF>& pts, int first, int last, double tol, QVector<bool>& keep)
{
	double maxDist = 0.0;
	int index = -1;
	for (int i = first + 1; i < last; ++i)
	{
		const double d = perpDistance(pts[i], pts[first], pts[last]);
		if (d > maxDist) { maxDist = d; index = i; }
	}
	if (index >= 0 && maxDist > tol)
	{
		keep[index] = true;
		douglasPeucker(pts, first, index, tol, keep);
		douglasPeucker(pts, index, last, tol, keep);
	}
}

QVector<QPointF> simplifyLoop(const QVector<QPointF>& loop, double tol)
{
	if (loop.size() < 8)
		return loop;
	// Split the closed loop at the two points farthest apart so DP has two open runs.
	int far = 0;
	double best = -1.0;
	for (int i = 1; i < loop.size(); ++i)
	{
		const double d = std::hypot(loop[i].x() - loop[0].x(), loop[i].y() - loop[0].y());
		if (d > best) { best = d; far = i; }
	}
	QVector<bool> keep(loop.size(), false);
	keep[0] = keep[far] = true;
	douglasPeucker(loop, 0, far, tol, keep);
	QVector<QPointF> tail = loop.mid(far);
	tail.append(loop[0]);
	QVector<bool> keepTail(tail.size(), false);
	keepTail[0] = keepTail[tail.size() - 1] = true;
	douglasPeucker(tail, 0, tail.size() - 1, tol, keepTail);
	QVector<QPointF> out;
	for (int i = 0; i <= far; ++i)
		if (keep[i]) out.append(loop[i]);
	for (int i = 1; i < tail.size() - 1; ++i)
		if (keepTail[i]) out.append(tail[i]);
	return out;
}

// One round of corner cutting takes the staircase look off the traced edge.
QVector<QPointF> chaikin(const QVector<QPointF>& pts)
{
	if (pts.size() < 3)
		return pts;
	QVector<QPointF> out;
	out.reserve(pts.size() * 2);
	for (int i = 0; i < pts.size(); ++i)
	{
		const QPointF& p = pts[i];
		const QPointF& q = pts[(i + 1) % pts.size()];
		out.append(p * 0.75 + q * 0.25);
		out.append(p * 0.25 + q * 0.75);
	}
	return out;
}

// The subject outline in picture pixels.
QList<QVector<QPointF> > traceMask(const Mask& m)
{
	// Edges: each is a directed unit segment keeping the "on" side on its left.
	QHash<QPoint, QVector<QPoint> > next;   // vertex -> list of vertices it points to
	auto addEdge = [&](int ax, int ay, int bx, int by) { next[QPoint(ax, ay)].append(QPoint(bx, by)); };
	for (int y = 0; y < m.h; ++y)
		for (int x = 0; x < m.w; ++x)
		{
			if (!m.at(x, y))
				continue;
			if (!m.at(x, y - 1)) addEdge(x, y, x + 1, y);           // top edge, left to right
			if (!m.at(x + 1, y)) addEdge(x + 1, y, x + 1, y + 1);   // right edge, down
			if (!m.at(x, y + 1)) addEdge(x + 1, y + 1, x, y + 1);   // bottom edge, right to left
			if (!m.at(x - 1, y)) addEdge(x, y + 1, x, y);           // left edge, up
		}
	QList<QVector<QPointF> > loops;
	while (!next.isEmpty())
	{
		auto it = next.begin();
		const QPoint start = it.key();
		QPoint cur = start;
		QVector<QPointF> loop;
		int guard = m.w * m.h * 4;
		while (guard-- > 0)
		{
			auto at = next.find(cur);
			if (at == next.end() || at.value().isEmpty())
				break;
			// at a vertex with two ways out (pixels touching at a corner) take the
			// one that turns right, so the two shapes stay separate loops
			QPoint to = at.value().takeLast();
			if (at.value().isEmpty())
				next.erase(at);
			loop.append(QPointF(cur));
			cur = to;
			if (cur == start)
				break;
		}
		if (loop.size() >= 4)
			loops.append(loop);
	}
	QList<QVector<QPointF> > out;
	for (const QVector<QPointF>& loop : std::as_const(loops))
	{
		// drop specks
		double area = 0.0;
		for (int i = 0; i < loop.size(); ++i)
		{
			const QPointF& p = loop[i];
			const QPointF& q = loop[(i + 1) % loop.size()];
			area += p.x() * q.y() - q.x() * p.y();
		}
		if (std::fabs(area) / 2.0 < 12.0)
			continue;
		QVector<QPointF> simple = chaikin(simplifyLoop(loop, 1.1));
		for (QPointF& p : simple)
			p *= m.scale;
		out.append(simple);
	}
	return out;
}

QPainterPath pathFromLoops(const QList<QVector<QPointF> >& loops)
{
	QPainterPath path;
	path.setFillRule(Qt::OddEvenFill);
	for (const QVector<QPointF>& loop : loops)
	{
		if (loop.size() < 3)
			continue;
		path.moveTo(loop[0]);
		for (int i = 1; i < loop.size(); ++i)
			path.lineTo(loop[i]);
		path.closeSubpath();
	}
	return path;
}

QString loopsToString(const QPainterPath& path)
{
	QString s;
	for (const QPolygonF& poly : path.toSubpathPolygons())
	{
		if (!s.isEmpty()) s += ';';
		QStringList pts;
		for (const QPointF& p : poly)
			pts << QString::number(p.x(), 'f', 1) + ',' + QString::number(p.y(), 'f', 1);
		s += pts.join(' ');
	}
	return s;
}

QPainterPath loopsFromString(const QString& s)
{
	QPainterPath path;
	path.setFillRule(Qt::OddEvenFill);
	for (const QString& loop : s.split(';', Qt::SkipEmptyParts))
	{
		bool first = true;
		for (const QString& pt : loop.split(' ', Qt::SkipEmptyParts))
		{
			const QStringList xy = pt.split(',');
			if (xy.size() != 2) continue;
			const QPointF p(xy[0].toDouble(), xy[1].toDouble());
			if (first) { path.moveTo(p); first = false; } else path.lineTo(p);
		}
		if (!first) path.closeSubpath();
	}
	return path;
}

// Subject AND region, in picture pixels.
QPainterPath regionPath(const SuneerPopOut::Settings& s, const QSize& picture)
{
	QPainterPath r;
	switch (s.region)
	{
		case SuneerPopOut::Custom:
			if (s.shape == SuneerPopOut::Ellipse)
				r.addEllipse(s.custom);
			else
				r.addRect(s.custom);
			break;
		default:   // HeadOnly and AboveLine: everything above the line
			r.addRect(QRectF(-1.0, -1.0, picture.width() + 2.0, qMax(0.0, s.lineY) + 1.0));
			break;
	}
	return r;
}

// Picture pixels -> face frame coordinates.
QTransform pictureToFrame(const PageItem* face)
{
	QTransform t;
	t.scale(face->imageXScale(), face->imageYScale());
	t.translate(face->imageXOffset(), face->imageYOffset());
	return t;
}

// Lays the pop-out frame over the face from the stored pixel path.
void placePopOut(ScribusDoc* doc, PageItem* face, PageItem* pop, const QPainterPath& pixelPath)
{
	QPainterPath framePath = pictureToFrame(face).map(pixelPath);
	framePath.setFillRule(Qt::OddEvenFill);
	const QRectF b = framePath.boundingRect();
	if (b.width() < 0.5 || b.height() < 0.5)
		return;
	QTransform frameToPage;
	frameToPage.translate(face->xPos(), face->yPos());
	frameToPage.rotate(face->rotation());
	const QPointF origin = frameToPage.map(b.topLeft());
	pop->setXYPos(origin.x(), origin.y(), true);
	pop->setWidthHeight(b.width(), b.height(), true);
	pop->setRotation(face->rotation(), true);
	framePath.translate(-b.topLeft());
	pop->PoLine.resize(0);
	pop->PoLine.fromQPainterPath(framePath, true);
	pop->setFillEvenOdd(true);
	pop->ClipEdited = true;
	pop->FrameType = 3;
	pop->Clip = flattenPath(pop->PoLine, pop->Segments);
	pop->ContourLine = pop->PoLine.copy();
	// same picture placement as in the letters: the frame moved by b, so the
	// picture inside moves back by b (in picture units)
	pop->setImageXYScale(face->imageXScale(), face->imageYScale());
	pop->setImageXYOffset(face->imageXOffset() - b.left() / face->imageXScale(),
	                      face->imageYOffset() - b.top() / face->imageYScale());
	pop->OldB2 = pop->width();
	pop->OldH2 = pop->height();
	doc->setRedrawBounding(pop);
}

bool s_syncing = false;

}

// ------------------------------------------------------------- settings

QString SuneerPopOut::attributeName() { return kAttr; }
QString SuneerPopOut::ofAttributeName() { return kAttrOf; }
QString SuneerPopOut::pathAttributeName() { return kAttrPath; }

QString SuneerPopOut::Settings::toString() const
{
	QUrlQuery q;
	q.addQueryItem("region", QString::number(region));
	q.addQueryItem("lineY", QString::number(lineY));
	q.addQueryItem("shape", QString::number(shape));
	q.addQueryItem("custom", QString("%1,%2,%3,%4").arg(custom.x()).arg(custom.y()).arg(custom.width()).arg(custom.height()));
	q.addQueryItem("shadow", shadow ? "1" : "0");
	q.addQueryItem("group", group ? "1" : "0");
	q.addQueryItem("maskFile", QString::fromLatin1(maskFile.toUtf8().toPercentEncoding()));
	q.addQueryItem("model", model);
	return q.toString(QUrl::FullyEncoded);
}

SuneerPopOut::Settings SuneerPopOut::Settings::fromString(const QString& text)
{
	Settings s;
	QUrlQuery q(text);
	auto num = [&](const char* key, double def) { return q.hasQueryItem(key) ? q.queryItemValue(key).toDouble() : def; };
	s.region = int(num("region", HeadOnly));
	s.lineY = num("lineY", -1.0);
	s.shape = int(num("shape", Rectangle));
	const QStringList c = q.queryItemValue("custom").split(',');
	if (c.size() == 4)
		s.custom = QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble());
	s.shadow = q.queryItemValue("shadow") == "1";
	s.group = q.queryItemValue("group") == "1";
	s.maskFile = QString::fromUtf8(QByteArray::fromPercentEncoding(q.queryItemValue("maskFile", QUrl::FullyEncoded).toLatin1()));
	if (q.hasQueryItem("model"))
		s.model = q.queryItemValue("model");
	return s;
}

bool SuneerPopOut::canRunOn(const PageItem* item)
{
	return item && item->isImageFrame() && !item->isGroupChild() && !item->Pfile.isEmpty() && item->imageIsAvailable
	       && attributeValue(item, kAttrOf).isEmpty();
}

// ------------------------------------------------------------- apply

PageItem* SuneerPopOut::apply(ScribusMainWindow* mw, PageItem* faceIn, const Settings& settingsIn, QString* error)
{
	auto fail = [error](const QString& why) -> PageItem* { if (error) *error = why; return nullptr; };
	// rembg may run below with a local event loop; the frame is held weakly
	// and checked again afterwards.
	QPointer<PageItem> face = faceIn;
	if (!mw || !mw->doc || !face || !face->isImageFrame())
		return fail(QObject::tr("Select the image frame with the letters first."));
	ScribusDoc* doc = mw->doc;
	if (face->locked() || doc->layerLocked(face->m_layerID))
		return fail(QObject::tr("The frame or its layer is locked."));
	if (face->imageXScale() <= 0.0 || face->imageYScale() <= 0.0)
		return fail(QObject::tr("The frame has no picture."));
	Settings settings = settingsIn;
	if (settings.maskFile.isEmpty() || !QFileInfo::exists(settings.maskFile))
		if (!ensureMask(mw, doc, face->Pfile, settings.model, settings.maskFile, error))
			return nullptr;
	if (!face || mw->doc != doc)
		return fail(QObject::tr("The frame is gone."));
	Mask mask;
	QSize picture;
	if (!loadMask(settings.maskFile, mask, picture))
		return fail(QObject::tr("The mask file could not be read:\n%1").arg(settings.maskFile));
	bool anyOn = false;
	for (uchar v : std::as_const(mask.on))
		if (v) { anyOn = true; break; }
	if (!anyOn)
		return fail(QObject::tr("rembg found no person in the picture (the mask is empty)."));
	if (settings.region == HeadOnly || settings.lineY < 0.0)
		settings.lineY = headLine(mask);
	if (settings.region == Custom && settings.custom.isEmpty())
		return fail(QObject::tr("Draw the rectangle or ellipse on the preview first."));

	QPainterPath subject = pathFromLoops(traceMask(mask));
	if (subject.isEmpty())
		return fail(QObject::tr("rembg found no person in the picture."));
	QPainterPath pop = subject.intersected(regionPath(settings, picture));
	pop.setFillRule(Qt::OddEvenFill);
	if (pop.isEmpty() || pop.boundingRect().width() < 2.0)
		return fail(QObject::tr("Nothing of the person lies inside the chosen region."));

	const QString popName = face->itemName() + kSuffix;
	PageItem* oldPop = itemNamed(doc, popName);

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(face->getUName(), face->getUPixmap(),
		                                                        QObject::tr("Pop-out Subject"), QString(), nullptr);
	if (oldPop)
	{
		Selection gone(doc, false);
		gone.addItem(oldPop);
		doc->itemSelection_DeleteItem(&gone);
	}
	if (settings.setImage && settings.imageScale > 0.0)
	{
		// the picture placement the preview showed, inside the letters (undoable)
		s_syncing = true;
		face->setImageXYScale(settings.imageScale, settings.imageScale);
		face->setImageXYOffset(settings.imageOffX, settings.imageOffY);
		s_syncing = false;
	}
	const int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, face->xPos(), face->yPos(), 10, 10, 0.0,
	                           CommonStrings::None, CommonStrings::None);
	PageItem* popItem = doc->Items->at(z);
	popItem->setItemName(popName);
	popItem->m_layerID = face->m_layerID;
	popItem->setTextFlowMode(PageItem::TextFlowDisabled);
	doc->loadPict(face->Pfile, popItem, false, false);
	if (!popItem->imageIsAvailable)
	{
		{
			UndoBlocker block;
			Selection gone(doc, false);
			gone.addItem(popItem);
			doc->itemSelection_DeleteItem(&gone);
		}
		if (transaction)
			transaction.cancel();
		return fail(QObject::tr("The picture could not be loaded into the pop-out frame."));
	}
	popItem->setImageScalingMode(true, true);
	{
		s_syncing = true;
		placePopOut(doc, face, popItem, pop);
		s_syncing = false;
	}
	if (settings.shadow)
	{
		popItem->setHasSoftShadow(true);
		popItem->setSoftShadowColor(doc->PageColors.contains("Black") ? QStringLiteral("Black") : CommonStrings::None);
		popItem->setSoftShadowXOffset(2.0);
		popItem->setSoftShadowYOffset(3.0);
		popItem->setSoftShadowBlurRadius(4.0);
		popItem->setSoftShadowOpacity(0.6);
		popItem->setSoftShadowErasedByObject(true);
	}
	{
		const ObjAttrVector before = *popItem->getObjectAttributes();
		setAttribute(popItem, kAttrOf, face->itemName());
		setAttribute(popItem, kAttrPath, loopsToString(pop));
		recordAttributes(popItem, before);
	}
	{
		const ObjAttrVector before = *face->getObjectAttributes();
		setAttribute(face, kAttr, settings.toString());
		recordAttributes(face, before);
	}
	PageItem* result = popItem;
	if (settings.group)
	{
		Selection grp(doc, false);
		PageItem* back = itemNamed(doc, face->itemName().left(face->itemName().length() - 6) + QStringLiteral(" poster back"));
		if (back) grp.addItem(back);
		grp.addItem(face);
		grp.addItem(popItem);
		if (PageItem* g = doc->groupObjectsSelection(&grp))
			result = g;
	}
	if (transaction)
		transaction.commit();
	doc->m_Selection->clear();
	doc->m_Selection->addItem(result);
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	if (mw->view)
		mw->view->DrawNew();
	return popItem;
}

void SuneerPopOut::itemChanged(PageItem* item)
{
	if (s_syncing || !item || !item->isImageFrame() || !item->doc())
		return;
	const ObjAttrVector* all = item->getObjectAttributes();
	if (!all || all->isEmpty())
		return;
	bool isFace = false;
	for (const ObjectAttribute& a : *all)
		if (a.name == kAttr) { isFace = true; break; }
	if (!isFace)
		return;
	ScribusDoc* doc = item->doc();
	PageItem* pop = itemNamed(doc, item->itemName() + kSuffix);
	if (!pop || !pop->isImageFrame())
		return;
	const QPainterPath pixelPath = loopsFromString(attributeValue(pop, kAttrPath));
	if (pixelPath.isEmpty())
		return;
	s_syncing = true;
	{
		UndoBlocker block;
		placePopOut(doc, item, pop, pixelPath);
		pop->update();
	}
	s_syncing = false;
}

// ------------------------------------------------------------- dialog

void SuneerPopOut::runForSelection(ScribusMainWindow* mw)
{
	if (!mw || !mw->HaveDoc || !mw->doc)
		return;
	PageItem* item = mw->doc->m_Selection->count() == 1 ? mw->doc->m_Selection->itemAt(0) : nullptr;
	// opened on the pop-out itself: edit its face
	if (item && !attributeValue(item, kAttrOf).isEmpty())
		item = itemNamed(mw->doc, attributeValue(item, kAttrOf));
	if (!canRunOn(item))
	{
		QMessageBox::information(mw, QObject::tr("Pop-out Subject"), QObject::tr("Select the image frame with the letters (a Fill Text with Image or Poster Stack result) first."));
		return;
	}
	runOn(mw, item);
}


// ------------------------------------------------------------- dialog

namespace
{
// Everything the preview needs, as plain data: the worker thread sees only
// copies of this, never a PageItem or a widget.
struct Scene
{
	QSizeF frameSize;              // face frame, points
	QPainterPath letters;          // face outline, frame coords
	QColor backColor;              // invalid = none
	QImage picture;                // downscaled copy
	double previewScale { 1.0 };   // picture.width() / full picture width
	QSize pictureSize;             // full picture, pixels
	double imgScale { 1.0 };       // face image scale (points per pixel)
	QPointF imgOffset;             // face image offset, pixels
	QSize maskSize;                // working mask size
	double maskScale { 1.0 };      // picture pixels per mask pixel
};

Mask maskFromImage(const QImage& img, double scale)
{
	Mask m;
	m.w = img.width();
	m.h = img.height();
	m.scale = scale;
	m.on.resize(m.w * m.h);
	for (int y = 0; y < m.h; ++y)
	{
		const uchar* line = img.constScanLine(y);
		for (int x = 0; x < m.w; ++x)
			m.on[y * m.w + x] = line[x] >= 128 ? 1 : 0;
	}
	return m;
}

class CompositePreview : public QWidget
{
public:
	enum Tool { ToolRegion = 0, ToolBrushAdd, ToolBrushErase, ToolImage };

	CompositePreview(QWidget* parent) : QWidget(parent)
	{
		setMinimumSize(560, 420);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMouseTracking(true);
		setFocusPolicy(Qt::WheelFocus);
	}
	Scene scene;
	SuneerPopOut::Settings settings;      // lineY / custom in picture pixels
	QImage mask;                          // working mask, Grayscale8, editable by the brush
	QPainterPath subjectPixels;           // traced from mask, picture pixels
	bool showOutlines { true };
	bool showTint { true };
	int tool { ToolRegion };
	int brushRadius { 20 };               // picture pixels
	std::function<void()> maskEdited;     // brush stroke finished / in progress
	std::function<void()> settingsChanged;
	std::function<void()> imageChanged;

	void zoomFit()
	{
		const QRectF content = contentRect();
		if (content.isEmpty()) return;
		const double zx = (width() - 24) / content.width();
		const double zy = (height() - 24) / content.height();
		m_zoom = qMin(zx, zy);
		m_pan = QPointF((width() - content.width() * m_zoom) / 2.0 - content.left() * m_zoom,
		                (height() - content.height() * m_zoom) / 2.0 - content.top() * m_zoom);
		update();
	}
	void zoom100()
	{
		const QRectF content = contentRect();
		m_zoom = 1.0;
		m_pan = QPointF((width() - content.width()) / 2.0 - content.left(), (height() - content.height()) / 2.0 - content.top());
		update();
	}
	// Frame coords of the whole composite: frame plus whatever the pop-out adds.
	QRectF contentRect() const
	{
		QRectF r(QPointF(0, 0), scene.frameSize);
		const QPainterPath pop = popFrame();
		if (!pop.isEmpty()) r |= pop.boundingRect();
		return r;
	}
	QTransform pictureToFrame() const
	{
		QTransform t;
		t.scale(scene.imgScale, scene.imgScale);
		t.translate(scene.imgOffset.x(), scene.imgOffset.y());
		return t;
	}
	QPainterPath popPixels() const
	{
		if (subjectPixels.isEmpty()) return QPainterPath();
		QPainterPath p = subjectPixels.intersected(regionPath(settings, scene.pictureSize));
		p.setFillRule(Qt::OddEvenFill);
		return p;
	}
	QPainterPath popFrame() const { return pictureToFrame().map(popPixels()); }

protected:
	QTransform view() const { QTransform v; v.translate(m_pan.x(), m_pan.y()); v.scale(m_zoom, m_zoom); return v; }
	QPointF toFrame(const QPointF& w) const { return view().inverted().map(w); }
	QPointF toPixel(const QPointF& w) const { return pictureToFrame().inverted().map(toFrame(w)); }
	QPointF pixelToWidget(const QPointF& p) const { return view().map(pictureToFrame().map(p)); }

	void drawPicture(QPainter& p) const
	{
		QTransform t = pictureToFrame();
		t.scale(1.0 / scene.previewScale, 1.0 / scene.previewScale);
		p.save();
		p.setTransform(t, true);
		p.drawImage(QPointF(0, 0), scene.picture);
		p.restore();
	}
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(90, 90, 90));
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setTransform(view());
		const QRectF frame(QPointF(0, 0), scene.frameSize);
		// paper
		p.fillRect(contentRect().adjusted(-20, -20, 20, 20), Qt::white);
		if (scene.backColor.isValid())
			p.fillRect(frame, scene.backColor);
		// letters with the picture
		p.save();
		p.setClipPath(scene.letters);
		drawPicture(p);
		p.restore();
		// pop-out on top
		const QPainterPath pop = popFrame();
		if (!pop.isEmpty())
		{
			p.save();
			p.setClipPath(pop);
			drawPicture(p);
			p.restore();
		}
		// overlays
		p.setBrush(Qt::NoBrush);
		if (showOutlines)
		{
			QPen pen(QColor(0, 200, 255), 1.0 / m_zoom);
			p.setPen(pen);
			p.drawPath(scene.letters);
			p.drawRect(frame);
		}
		if (showTint && !pop.isEmpty())
			p.fillPath(pop, QColor(255, 120, 0, 60));
		// person outline (what rembg found) faintly
		if (showTint && !subjectPixels.isEmpty())
		{
			p.setPen(QPen(QColor(0, 120, 255, 160), 1.0 / m_zoom, Qt::DashLine));
			p.drawPath(pictureToFrame().map(subjectPixels));
		}
		// region
		p.setPen(QPen(QColor(255, 60, 0), 2.0 / m_zoom));
		if (settings.region == SuneerPopOut::Custom)
		{
			const QRectF c = pictureToFrame().mapRect(settings.custom);
			if (settings.shape == SuneerPopOut::Ellipse) p.drawEllipse(c); else p.drawRect(c);
			p.setBrush(Qt::white);
			const double h = 5.0 / m_zoom;
			for (const QPointF& corner : { c.topLeft(), c.topRight(), c.bottomLeft(), c.bottomRight() })
				p.drawRect(QRectF(corner.x() - h, corner.y() - h, 2 * h, 2 * h));
			p.setBrush(Qt::NoBrush);
		}
		else
		{
			const double y = pictureToFrame().map(QPointF(0, settings.lineY)).y();
			const QRectF cr = contentRect();
			p.drawLine(QPointF(cr.left() - 10, y), QPointF(cr.right() + 10, y));
		}
		// brush cursor
		if ((tool == ToolBrushAdd || tool == ToolBrushErase) && underMouse())
		{
			p.setPen(QPen(tool == ToolBrushAdd ? QColor(0, 220, 0) : QColor(255, 0, 0), 1.0 / m_zoom));
			const double r = brushRadius * scene.imgScale;
			p.drawEllipse(toFrame(m_mouse), r, r);
		}
	}
	int hitHandle(const QPointF& w) const
	{
		if (settings.region != SuneerPopOut::Custom) return -1;
		const QRectF c = settings.custom;
		const QPointF corners[4] = { c.topLeft(), c.topRight(), c.bottomLeft(), c.bottomRight() };
		for (int i = 0; i < 4; ++i)
			if ((pixelToWidget(corners[i]) - w).manhattanLength() <= 9) return i;
		return -1;
	}
	void paintBrush(const QPointF& pixel)
	{
		if (mask.isNull()) return;
		QPainter mp(&mask);
		mp.setRenderHint(QPainter::Antialiasing, false);
		mp.setPen(Qt::NoPen);
		mp.setBrush(tool == ToolBrushAdd ? Qt::white : Qt::black);
		const double r = brushRadius / scene.maskScale;
		mp.drawEllipse(pixel / scene.maskScale, r, r);
	}
	void mousePressEvent(QMouseEvent* e) override
	{
		m_mouse = e->position();
		m_press = m_mouse;
		m_dragging = true;
		if (e->button() == Qt::MiddleButton) { m_drag = DragPan; m_panStart = m_pan; return; }
		const QPointF pix = toPixel(m_mouse);
		switch (tool)
		{
			case ToolImage:
				m_drag = DragImage;
				m_imgStart = scene.imgOffset;
				break;
			case ToolBrushAdd:
			case ToolBrushErase:
				m_drag = DragBrush;
				paintBrush(pix);
				update();
				if (maskEdited) maskEdited();
				break;
			default:
				if (settings.region == SuneerPopOut::Custom)
				{
					m_handle = hitHandle(m_mouse);
					if (m_handle >= 0) { m_drag = DragResize; m_customStart = settings.custom; }
					else if (settings.custom.contains(pix)) { m_drag = DragMove; m_customStart = settings.custom; }
					else { m_drag = DragDraw; settings.custom = QRectF(pix, QSizeF(0, 0)); }
				}
				else
				{
					m_drag = DragLine;
					settings.region = SuneerPopOut::AboveLine;
					settings.lineY = qBound(0.0, pix.y(), double(scene.pictureSize.height()));
					if (settingsChanged) settingsChanged();
				}
				break;
		}
		update();
	}
	void mouseMoveEvent(QMouseEvent* e) override
	{
		m_mouse = e->position();
		if (!m_dragging) { if (tool == ToolBrushAdd || tool == ToolBrushErase) update(); return; }
		const QPointF pix = toPixel(m_mouse);
		const QPointF startPix = toPixel(m_press);
		switch (m_drag)
		{
			case DragPan: m_pan = m_panStart + (m_mouse - m_press); break;
			case DragImage:
				scene.imgOffset = m_imgStart + (toFrame(m_mouse) - toFrame(m_press)) / scene.imgScale;
				if (imageChanged) imageChanged();
				break;
			case DragBrush: paintBrush(pix); if (maskEdited) maskEdited(); break;
			case DragLine:
				settings.lineY = qBound(0.0, pix.y(), double(scene.pictureSize.height()));
				if (settingsChanged) settingsChanged();
				break;
			case DragDraw:
				settings.custom = QRectF(startPix, pix).normalized();
				if (settingsChanged) settingsChanged();
				break;
			case DragMove:
				settings.custom = m_customStart.translated(pix - startPix);
				if (settingsChanged) settingsChanged();
				break;
			case DragResize:
			{
				QRectF c = m_customStart;
				switch (m_handle) { case 0: c.setTopLeft(pix); break; case 1: c.setTopRight(pix); break;
				                    case 2: c.setBottomLeft(pix); break; default: c.setBottomRight(pix); break; }
				settings.custom = c.normalized();
				if (settingsChanged) settingsChanged();
				break;
			}
			default: break;
		}
		update();
	}
	void mouseReleaseEvent(QMouseEvent*) override
	{
		m_dragging = false;
		if (m_drag == DragBrush && maskEdited) maskEdited();
		m_drag = DragNone;
	}
	void wheelEvent(QWheelEvent* e) override
	{
		const double steps = e->angleDelta().y() / 120.0;
		if (steps == 0.0) return;
		if (tool == ToolImage)
		{
			// zoom the picture about the cursor, inside the letters and the pop-out alike
			const QPointF pixUnder = toPixel(e->position());
			const double factor = std::pow(1.1, steps);
			scene.imgScale = qBound(0.01, scene.imgScale * factor, 100.0);
			const QPointF frameUnder = toFrame(e->position());
			// keep the picture point under the cursor where it is
			scene.imgOffset = QPointF(frameUnder.x() / scene.imgScale - pixUnder.x(), frameUnder.y() / scene.imgScale - pixUnder.y());
			if (imageChanged) imageChanged();
		}
		else
		{
			const QPointF before = toFrame(e->position());
			m_zoom = qBound(0.05, m_zoom * std::pow(1.15, steps), 40.0);
			const QPointF after = toFrame(e->position());
			m_pan += (after - before) * m_zoom;
		}
		update();
		e->accept();
	}
	void resizeEvent(QResizeEvent*) override { if (m_firstResize) { m_firstResize = false; zoomFit(); } }

private:
	enum Drag { DragNone, DragPan, DragImage, DragBrush, DragLine, DragDraw, DragMove, DragResize };
	double m_zoom { 1.0 };
	QPointF m_pan, m_panStart, m_mouse, m_press, m_imgStart;
	QRectF m_customStart;
	int m_handle { -1 };
	bool m_dragging { false };
	bool m_firstResize { true };
	Drag m_drag { DragNone };
};
}

void SuneerPopOut::runOn(ScribusMainWindow* mw, PageItem* faceIn)
{
	QPointer<PageItem> face = faceIn;
	if (!mw || !mw->doc || !face)
		return;
	ScribusDoc* doc = mw->doc;
	const QString title = QObject::tr("Pop-out Subject");
	if (!doc->hasName || doc->documentFileName().isEmpty())
	{
		if (QMessageBox::question(mw, title, QObject::tr("The document has not been saved yet.\n\nThe person mask is kept in a folder next to the document, so it has to be saved first. Save it now?"),
		                          QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save) != QMessageBox::Save)
			return;
		mw->slotFileSaveAs();
		if (!mw->HaveDoc || mw->doc != doc || !doc->hasName || !face)
			return;
	}
	Settings s;
	const QString saved = attributeValue(face, kAttr);
	if (!saved.isEmpty())
		s = Settings::fromString(saved);

	QString error;
	if (s.maskFile.isEmpty() || !QFileInfo::exists(s.maskFile))
		if (!ensureMask(mw, doc, face->Pfile, s.model, s.maskFile, &error))
		{
			if (error != QObject::tr("Cancelled."))
				QMessageBox::warning(mw, title, error);
			return;
		}
	if (!face || mw->doc != doc)
		return;

	// ---- the scene: plain data copied out of the document
	Scene scene;
	scene.frameSize = QSizeF(face->width(), face->height());
	scene.letters = face->PoLine.toQPainterPath(true);
	scene.letters.setFillRule(face->fillRule ? Qt::OddEvenFill : Qt::WindingFill);
	scene.imgScale = face->imageXScale();
	scene.imgOffset = QPointF(face->imageXOffset(), face->imageYOffset());
	{
		const QString faceName = face->itemName();
		const QString textName = faceName.endsWith(QLatin1String(" image")) ? faceName.left(faceName.length() - 6) : faceName;
		if (PageItem* back = itemNamed(doc, textName + QStringLiteral(" poster back")))
			if (back->fillColor() != CommonStrings::None && doc->PageColors.contains(back->fillColor()))
				scene.backColor = ScColorEngine::getDisplayColor(doc->PageColors[back->fillColor()], doc);
	}
	QImage maskFull(s.maskFile);
	if (maskFull.isNull())
	{
		QMessageBox::warning(mw, title, QObject::tr("The mask file could not be read:\n%1").arg(s.maskFile));
		return;
	}
	maskFull = maskFull.convertToFormat(QImage::Format_Grayscale8);
	scene.pictureSize = maskFull.size();
	QImage picture(face->Pfile);
	if (picture.isNull())
	{
		picture = QImage(scene.pictureSize, QImage::Format_RGB32);
		picture.fill(Qt::gray);
	}
	if (picture.size() != scene.pictureSize)
		picture = picture.scaled(scene.pictureSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	if (picture.width() > 1600)
		picture = picture.scaledToWidth(1600, Qt::SmoothTransformation);
	scene.picture = picture;
	scene.previewScale = double(picture.width()) / scene.pictureSize.width();
	// working mask (the brush paints here)
	const int longest = qMax(maskFull.width(), maskFull.height());
	QImage maskWork = longest > kMaskMax ? maskFull.scaled(qMax(1, qRound(maskFull.width() * double(kMaskMax) / longest)),
	                                                        qMax(1, qRound(maskFull.height() * double(kMaskMax) / longest)),
	                                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : maskFull;
	maskWork = maskWork.convertToFormat(QImage::Format_Grayscale8);
	scene.maskSize = maskWork.size();
	scene.maskScale = double(scene.pictureSize.width()) / maskWork.width();
	const Mask firstMask = maskFromImage(maskWork, scene.maskScale);
	const double autoLine = headLine(firstMask);
	if (s.lineY < 0.0) s.lineY = autoLine;
	if (s.custom.isEmpty())
		s.custom = QRectF(scene.pictureSize.width() * 0.25, 0, scene.pictureSize.width() * 0.5, scene.pictureSize.height() * 0.45);

	// ---- dialog
	QDialog dlg(mw);
	dlg.setObjectName("suneerPopOutDialog");
	dlg.setWindowTitle(title);
	dlg.setSizeGripEnabled(true);
	dlg.resize(1100, 820);
	auto* outer = new QVBoxLayout(&dlg);
	auto* preview = new CompositePreview(&dlg);
	preview->scene = scene;
	preview->settings = s;
	preview->mask = maskWork;
	outer->addWidget(preview, 1);

	// view buttons + overlay toggles
	auto* viewRow = new QHBoxLayout();
	auto* fitBtn = new QPushButton(QObject::tr("Zoom to fit"), &dlg);
	auto* hundredBtn = new QPushButton(QObject::tr("100%"), &dlg);
	auto* outlinesChk = new QCheckBox(QObject::tr("Show letter outlines"), &dlg);
	outlinesChk->setChecked(true);
	auto* tintChk = new QCheckBox(QObject::tr("Show subject mask"), &dlg);
	tintChk->setChecked(true);
	viewRow->addWidget(fitBtn);
	viewRow->addWidget(hundredBtn);
	viewRow->addSpacing(20);
	viewRow->addWidget(outlinesChk);
	viewRow->addWidget(tintChk);
	viewRow->addStretch();
	auto* status = new QLabel(&dlg);
	viewRow->addWidget(status);
	outer->addLayout(viewRow);

	// region
	auto* regionRow = new QHBoxLayout();
	auto* headRb = new QRadioButton(QObject::tr("Head only (automatic)"), &dlg);
	auto* lineRb = new QRadioButton(QObject::tr("Above a line (drag it)"), &dlg);
	auto* customRb = new QRadioButton(QObject::tr("Custom:"), &dlg);
	auto* shapeCombo = new QComboBox(&dlg);
	shapeCombo->addItems({ QObject::tr("Rectangle"), QObject::tr("Ellipse") });
	shapeCombo->setCurrentIndex(s.shape == Ellipse ? 1 : 0);
	// Two exclusive groups: Qt would otherwise make every radio button of the
	// dialog one group, and picking a tool would untick the region.
	auto* regionGroup = new QButtonGroup(&dlg);
	for (QRadioButton* rb : { headRb, lineRb, customRb })
		regionGroup->addButton(rb);
	(s.region == AboveLine ? lineRb : s.region == Custom ? customRb : headRb)->setChecked(true);
	regionRow->addWidget(new QLabel(QObject::tr("In front:"), &dlg));
	regionRow->addWidget(headRb);
	regionRow->addWidget(lineRb);
	regionRow->addWidget(customRb);
	regionRow->addWidget(shapeCombo);
	regionRow->addStretch();
	outer->addLayout(regionRow);

	// tools
	auto* toolRow = new QHBoxLayout();
	auto* toolRegionRb = new QRadioButton(QObject::tr("Adjust line / shape"), &dlg);
	auto* toolAddRb = new QRadioButton(QObject::tr("Brush: add"), &dlg);
	auto* toolEraseRb = new QRadioButton(QObject::tr("Brush: erase"), &dlg);
	auto* toolImageRb = new QRadioButton(QObject::tr("Move / zoom picture (drag, wheel)"), &dlg);
	auto* toolGroup = new QButtonGroup(&dlg);
	for (QRadioButton* rb : { toolRegionRb, toolAddRb, toolEraseRb, toolImageRb })
		toolGroup->addButton(rb);
	toolRegionRb->setChecked(true);
	auto* brushSize = new QSlider(Qt::Horizontal, &dlg);
	brushSize->setRange(2, 200);
	brushSize->setValue(20);
	brushSize->setFixedWidth(140);
	auto* zoomSlider = new QSlider(Qt::Horizontal, &dlg);
	zoomSlider->setRange(10, 400);
	zoomSlider->setValue(100);
	zoomSlider->setFixedWidth(160);
	zoomSlider->setToolTip(QObject::tr("Picture zoom, relative to now"));
	toolRow->addWidget(new QLabel(QObject::tr("Tool:"), &dlg));
	toolRow->addWidget(toolRegionRb);
	toolRow->addWidget(toolAddRb);
	toolRow->addWidget(toolEraseRb);
	toolRow->addWidget(new QLabel(QObject::tr("size"), &dlg));
	toolRow->addWidget(brushSize);
	toolRow->addWidget(toolImageRb);
	toolRow->addWidget(zoomSlider);
	toolRow->addStretch();
	outer->addLayout(toolRow);

	auto* shadowChk = new QCheckBox(QObject::tr("Soft drop shadow under the pop-out (adds a soft mask to the PDF; not PDF/X-1a safe)"), &dlg);
	shadowChk->setChecked(s.shadow);
	auto* groupChk = new QCheckBox(QObject::tr("Group background, letters and pop-out (Poster Stack and Text Effects then need the group opened first)"), &dlg);
	groupChk->setChecked(s.group);
	outer->addWidget(shadowChk);
	outer->addWidget(groupChk);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	auto* resetMaskBtn = buttons->addButton(QObject::tr("Reset brush edits"), QDialogButtonBox::ResetRole);
	outer->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(fitBtn, &QPushButton::clicked, &dlg, [preview] { preview->zoomFit(); });
	QObject::connect(hundredBtn, &QPushButton::clicked, &dlg, [preview] { preview->zoom100(); });
	QObject::connect(outlinesChk, &QCheckBox::toggled, &dlg, [preview](bool on) { preview->showOutlines = on; preview->update(); });
	QObject::connect(tintChk, &QCheckBox::toggled, &dlg, [preview](bool on) { preview->showTint = on; preview->update(); });
	QObject::connect(brushSize, &QSlider::valueChanged, &dlg, [preview](int v) { preview->brushRadius = v; preview->update(); });

	// ---- tracing off the GUI thread: the worker gets a copy of the mask image,
	// returns polygons; the GUI applies the newest result only.
	QFutureWatcher<QList<QVector<QPointF> > > watcher;
	QTimer traceTimer;
	traceTimer.setSingleShot(true);
	traceTimer.setInterval(120);
	int pendingTrace = 0;
	bool traceAgain = false;
	auto startTrace = [&]() {
		if (watcher.isRunning()) { traceAgain = true; return; }
		const QImage copy = preview->mask.copy();
		const double scale = scene.maskScale;
		status->setText(QObject::tr("Tracing..."));
		++pendingTrace;
		watcher.setFuture(QtConcurrent::run([copy, scale]() { return traceMask(maskFromImage(copy, scale)); }));
	};
	QObject::connect(&watcher, &QFutureWatcher<QList<QVector<QPointF> > >::finished, &dlg, [&]() {
		preview->subjectPixels = pathFromLoops(watcher.result());
		status->setText(preview->subjectPixels.isEmpty() ? QObject::tr("No person found in the mask.") : QString());
		preview->update();
		if (traceAgain) { traceAgain = false; startTrace(); }
	});
	QObject::connect(&traceTimer, &QTimer::timeout, &dlg, startTrace);
	preview->maskEdited = [&]() { traceTimer.start(); };
	preview->settingsChanged = [&]() {
		if (preview->settings.region == AboveLine && !lineRb->isChecked()) lineRb->setChecked(true);
	};
	preview->imageChanged = [&]() {
		const bool blocked = zoomSlider->blockSignals(true);
		zoomSlider->setValue(qBound(10, qRound(preview->scene.imgScale / scene.imgScale * 100.0), 400));
		zoomSlider->blockSignals(blocked);
	};
	QObject::connect(zoomSlider, &QSlider::valueChanged, &dlg, [&](int v) {
		// zoom about the centre of the frame
		const QPointF centre(scene.frameSize.width() / 2.0, scene.frameSize.height() / 2.0);
		const QPointF pixUnder = preview->pictureToFrame().inverted().map(centre);
		preview->scene.imgScale = scene.imgScale * v / 100.0;
		preview->scene.imgOffset = QPointF(centre.x() / preview->scene.imgScale - pixUnder.x(), centre.y() / preview->scene.imgScale - pixUnder.y());
		preview->update();
	});
	auto syncRadios = [&]() {
		if (headRb->isChecked()) { preview->settings.region = HeadOnly; preview->settings.lineY = headLine(maskFromImage(preview->mask, scene.maskScale)); }
		else if (lineRb->isChecked()) preview->settings.region = AboveLine;
		else preview->settings.region = Custom;
		preview->settings.shape = shapeCombo->currentIndex() == 1 ? Ellipse : Rectangle;
		preview->update();
	};
	for (QRadioButton* rb : { headRb, lineRb, customRb })
		QObject::connect(rb, &QRadioButton::toggled, &dlg, [&](bool on) { if (on) syncRadios(); });
	QObject::connect(shapeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int) { syncRadios(); });
	auto syncTool = [&]() {
		preview->tool = toolAddRb->isChecked() ? CompositePreview::ToolBrushAdd
		              : toolEraseRb->isChecked() ? CompositePreview::ToolBrushErase
		              : toolImageRb->isChecked() ? CompositePreview::ToolImage : CompositePreview::ToolRegion;
		preview->setCursor(preview->tool == CompositePreview::ToolImage ? Qt::SizeAllCursor
		                   : preview->tool == CompositePreview::ToolRegion ? Qt::ArrowCursor : Qt::CrossCursor);
		preview->update();
	};
	for (QRadioButton* rb : { toolRegionRb, toolAddRb, toolEraseRb, toolImageRb })
		QObject::connect(rb, &QRadioButton::toggled, &dlg, [&](bool on) { if (on) syncTool(); });
	QObject::connect(resetMaskBtn, &QPushButton::clicked, &dlg, [&]() { preview->mask = maskWork; startTrace(); preview->update(); });
	startTrace();

	const int result = dlg.exec();
	watcher.waitForFinished();
	if (result != QDialog::Accepted || !face || mw->doc != doc)
		return;

	// ---- apply exactly what the preview shows
	Settings v = preview->settings;
	v.shadow = shadowChk->isChecked();
	v.group = groupChk->isChecked();
	v.model = s.model;
	v.maskFile = s.maskFile;
	if (preview->mask != maskWork)
	{
		// brush edits: saved as a mask of their own, at the picture's size, so
		// the applied path and a later reopening use the edited person shape
		const QFileInfo fi(s.maskFile);
		const QString edited = fi.absolutePath() + "/" + fi.completeBaseName() + QStringLiteral("_edit_%1.png").arg(QDateTime::currentSecsSinceEpoch());
		QImage full = preview->mask.scaled(scene.pictureSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (full.save(edited))
			v.maskFile = edited;
	}
	v.setImage = true;
	v.imageScale = preview->scene.imgScale;
	v.imageOffX = preview->scene.imgOffset.x();
	v.imageOffY = preview->scene.imgOffset.y();
	if (!apply(mw, face, v, &error))
	{
		QMessageBox::warning(mw, title, error);
		return;
	}
	mw->setStatusBarInfoText(QObject::tr("Pop-out made: the marked part is in front of the letters."));
}
