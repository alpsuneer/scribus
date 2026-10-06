#include "suneerpopout.h"
#include "suneerfilltextimage.h"

#include <cmath>
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
#include <QVector>

#include "commonstrings.h"
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

// ------------------------------------------------------------- preview widget

class PopPreview : public QWidget
{
public:
	PopPreview(const QImage& picture, const QImage& mask, QWidget* parent) : QWidget(parent), m_picture(picture), m_mask(mask)
	{
		setMinimumSize(420, 300);
		setMouseTracking(true);
	}
	SuneerPopOut::Settings settings;
	std::function<void()> changed;

	QRectF pictureRect() const
	{
		const QSizeF s = QSizeF(m_picture.size()).scaled(QSizeF(width() - 8, height() - 8), Qt::KeepAspectRatio);
		return QRectF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0, s.width(), s.height());
	}
	double k() const { return pictureRect().width() / m_picture.width(); }
	QPointF toPixel(const QPointF& w) const { const QRectF r = pictureRect(); return QPointF((w.x() - r.left()) / k(), (w.y() - r.top()) / k()); }
	QPointF toWidget(const QPointF& p) const { const QRectF r = pictureRect(); return QPointF(r.left() + p.x() * k(), r.top() + p.y() * k()); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(), palette().window());
		const QRectF r = pictureRect();
		p.drawImage(r, m_picture);
		// mask tint: what rembg calls the person
		if (!m_mask.isNull())
		{
			QImage tint(m_mask.size(), QImage::Format_ARGB32);
			tint.fill(Qt::transparent);
			for (int y = 0; y < m_mask.height(); ++y)
			{
				const uchar* src = m_mask.constScanLine(y);
				QRgb* dst = reinterpret_cast<QRgb*>(tint.scanLine(y));
				for (int x = 0; x < m_mask.width(); ++x)
					dst[x] = src[x] >= 128 ? qRgba(0, 160, 255, 70) : qRgba(0, 0, 0, 110);
			}
			p.drawImage(r, tint);
		}
		p.setPen(QPen(QColor(255, 90, 0), 2));
		p.setBrush(QColor(255, 90, 0, 40));
		if (settings.region == SuneerPopOut::Custom)
		{
			const QRectF c(toWidget(settings.custom.topLeft()), toWidget(settings.custom.bottomRight()));
			if (settings.shape == SuneerPopOut::Ellipse) p.drawEllipse(c); else p.drawRect(c);
		}
		else
		{
			const double y = toWidget(QPointF(0, settings.lineY)).y();
			p.setBrush(Qt::NoBrush);
			p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
			p.drawText(QPointF(r.left() + 6, y - 6), settings.region == SuneerPopOut::HeadOnly
			           ? QObject::tr("Head only (automatic)") : QObject::tr("Drag the line"));
		}
	}
	void mousePressEvent(QMouseEvent* e) override
	{
		m_press = toPixel(e->position());
		m_dragging = true;
		if (settings.region == SuneerPopOut::Custom)
			settings.custom = QRectF(m_press, QSizeF(0, 0));
		else
		{
			settings.region = SuneerPopOut::AboveLine;
			settings.lineY = qBound(0.0, m_press.y(), double(m_picture.height()));
		}
		update();
		if (changed) changed();
	}
	void mouseMoveEvent(QMouseEvent* e) override
	{
		if (!m_dragging) return;
		const QPointF now = toPixel(e->position());
		if (settings.region == SuneerPopOut::Custom)
			settings.custom = QRectF(m_press, now).normalized().intersected(QRectF(QPointF(0, 0), QSizeF(m_picture.size())));
		else
			settings.lineY = qBound(0.0, now.y(), double(m_picture.height()));
		update();
		if (changed) changed();
	}
	void mouseReleaseEvent(QMouseEvent*) override { m_dragging = false; }

private:
	QImage m_picture, m_mask;
	QPointF m_press;
	bool m_dragging { false };
};
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

PageItem* SuneerPopOut::apply(ScribusMainWindow* mw, PageItem* face, const Settings& settingsIn, QString* error)
{
	auto fail = [error](const QString& why) -> PageItem* { if (error) *error = why; return nullptr; };
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
	Mask mask;
	QSize picture;
	if (!loadMask(settings.maskFile, mask, picture))
		return fail(QObject::tr("The mask file could not be read:\n%1").arg(settings.maskFile));
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
	const int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, face->xPos(), face->yPos(), 10, 10, 0.0,
	                           CommonStrings::None, CommonStrings::None);
	PageItem* popItem = doc->Items->at(z);
	popItem->setItemName(popName);
	popItem->m_layerID = face->m_layerID;
	popItem->setTextFlowMode(PageItem::TextFlowDisabled);
	doc->loadPict(face->Pfile, popItem, false, false);
	if (!popItem->imageIsAvailable)
	{
		if (transaction) { transaction.commit(); UndoManager::instance()->undo(1); }
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
	if (!face)
		return;
	Mask mask;
	QSize pictureSize;
	if (!loadMask(s.maskFile, mask, pictureSize))
	{
		QMessageBox::warning(mw, title, QObject::tr("The mask file could not be read:\n%1").arg(s.maskFile));
		return;
	}
	const double autoLine = headLine(mask);
	if (s.lineY < 0.0)
		s.lineY = autoLine;
	if (s.custom.isEmpty())
		s.custom = QRectF(pictureSize.width() * 0.25, 0, pictureSize.width() * 0.5, pictureSize.height() * 0.45);

	QImage picture(face->Pfile);
	if (picture.isNull())
		picture = QImage(pictureSize, QImage::Format_RGB32), picture.fill(Qt::gray);
	else if (picture.width() > 1200)
		picture = picture.scaledToWidth(1200, Qt::SmoothTransformation);
	QImage maskImg = QImage(s.maskFile).convertToFormat(QImage::Format_Grayscale8);
	if (maskImg.size() != picture.size())
		maskImg = maskImg.scaled(picture.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	// preview works in picture pixels; the picture may be downscaled above
	const double previewScale = double(picture.width()) / pictureSize.width();

	QDialog dlg(mw);
	dlg.setObjectName("suneerPopOutDialog");
	dlg.setWindowTitle(title);
	auto* grid = new QGridLayout(&dlg);
	auto* preview = new PopPreview(picture, maskImg, &dlg);
	preview->settings = s;
	preview->settings.lineY = s.lineY * previewScale;
	preview->settings.custom = QRectF(s.custom.topLeft() * previewScale, s.custom.size() * previewScale);
	grid->addWidget(preview, 0, 0, 1, 4);
	auto* headRb = new QRadioButton(QObject::tr("Head only (automatic)"), &dlg);
	auto* lineRb = new QRadioButton(QObject::tr("Above a line (drag it)"), &dlg);
	auto* customRb = new QRadioButton(QObject::tr("Custom:"), &dlg);
	auto* shapeCombo = new QComboBox(&dlg);
	shapeCombo->addItems({ QObject::tr("Rectangle"), QObject::tr("Ellipse") });
	shapeCombo->setCurrentIndex(s.shape == Ellipse ? 1 : 0);
	(s.region == AboveLine ? lineRb : s.region == Custom ? customRb : headRb)->setChecked(true);
	grid->addWidget(headRb, 1, 0);
	grid->addWidget(lineRb, 1, 1);
	grid->addWidget(customRb, 1, 2);
	grid->addWidget(shapeCombo, 1, 3);
	auto* shadowChk = new QCheckBox(QObject::tr("Soft drop shadow under the pop-out (adds a soft mask to the PDF; not PDF/X-1a safe)"), &dlg);
	shadowChk->setChecked(s.shadow);
	grid->addWidget(shadowChk, 2, 0, 1, 4);
	auto* groupChk = new QCheckBox(QObject::tr("Group background, letters and pop-out (Poster Stack and Text Effects then need the group opened first)"), &dlg);
	groupChk->setChecked(s.group);
	grid->addWidget(groupChk, 3, 0, 1, 4);
	auto* note = new QLabel(QObject::tr("Blue: what rembg calls the person. Orange: the part that comes in front of the letters. "
	                                    "Moving or zooming the picture inside the letters moves the pop-out with it."), &dlg);
	note->setWordWrap(true);
	grid->addWidget(note, 4, 0, 1, 4);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	grid->addWidget(buttons, 5, 0, 1, 4);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	auto syncRadios = [&]() {
		if (headRb->isChecked())
		{
			preview->settings.region = HeadOnly;
			preview->settings.lineY = autoLine * previewScale;
		}
		else if (lineRb->isChecked())
			preview->settings.region = AboveLine;
		else
			preview->settings.region = Custom;
		preview->settings.shape = shapeCombo->currentIndex() == 1 ? Ellipse : Rectangle;
		preview->update();
	};
	for (QRadioButton* rb : { headRb, lineRb, customRb })
		QObject::connect(rb, &QRadioButton::toggled, &dlg, [&](bool) { syncRadios(); });
	QObject::connect(shapeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int) { syncRadios(); });
	preview->changed = [&]() {
		// dragging the line switches the mode to "Above a line"
		if (preview->settings.region == AboveLine && !lineRb->isChecked())
			lineRb->setChecked(true);
	};
	if (dlg.exec() != QDialog::Accepted || !face)
		return;

	Settings v = preview->settings;
	v.lineY = v.lineY / previewScale;
	v.custom = QRectF(v.custom.topLeft() / previewScale, v.custom.size() / previewScale);
	v.shadow = shadowChk->isChecked();
	v.group = groupChk->isChecked();
	v.maskFile = s.maskFile;
	v.model = s.model;
	if (!apply(mw, face, v, &error))
	{
		QMessageBox::warning(mw, title, error);
		return;
	}
	mw->setStatusBarInfoText(QObject::tr("Pop-out made: the part above the line is in front of the letters."));
}
