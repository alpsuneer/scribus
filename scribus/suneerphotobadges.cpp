/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this statement applies
to this file only.
*/

#include "suneerphotobadges.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QTransform>

#include "canvas.h"
#include "pageitem.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "scpage.h"
#include "scribus.h"
#include "scraction.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerimagelinks.h"

namespace
{
	// Below either of these a photo gets the dot (or nothing): no label is readable there.
	const double kMinPhotoPx = 40.0;
	const double kMinZoomPercent = 35.0;
	// A badge is at most this share of the photo's smaller side on screen ...
	const double kPhotoShare = 0.20;
	// ... and never smaller than this, or it becomes a dot.
	const double kMinBadgePx = 12.0;

	const QColor kLinkColor(255, 140, 0);
	const QColor kAlertColor(214, 30, 30);
	const QColor kManualColor(21, 101, 192);

	int s_smallMode = -1;

	PrefsContext* prefsContext()
	{
		PrefsFile* pf = PrefsManager::instance().prefsFile;
		return pf ? pf->getContext("suneer_images") : nullptr;
	}

	QString attrOf(const PageItem* item, const QString& name)
	{
		const ObjAttrVector* attrs = const_cast<PageItem*>(item)->getObjectAttributes();
		if (!attrs)
			return QString();
		for (const ObjectAttribute& a : *attrs)
			if (a.name == name)
				return a.value;
		return QString();
	}

	//! What one image frame carries.
	struct Marks
	{
		SuneerImageLinks::Status link { SuneerImageLinks::NotAnImage };
		QString check;        //!< obit.photoStatus noface / lowres / bg, else empty
		QString checkText;    //!< full flag text
		bool manual { false };
		bool linkBadge() const { return link == SuneerImageLinks::Linked || link == SuneerImageLinks::Missing; }
		bool any() const { return linkBadge() || !check.isEmpty() || manual; }
	};

	Marks marksOf(const PageItem* item)
	{
		Marks m;
		if (!item || !item->isImageFrame())
			return m;
		m.link = SuneerImageLinks::statusOf(item);
		const QString status = attrOf(item, QStringLiteral("obit.photoStatus"));
		if (status == QLatin1String("noface") || status == QLatin1String("lowres") || status == QLatin1String("bg"))
		{
			m.check = status;
			m.checkText = attrOf(item, QStringLiteral("obit.checkInfo"));
			if (m.checkText.isEmpty())
				m.checkText = status == QLatin1String("lowres") ? QStringLiteral("low res") : status == QLatin1String("bg") ? QStringLiteral("check BG") : QStringLiteral("no face – check");
		}
		m.manual = attrOf(item, QStringLiteral("suneer.photoEditBgMode")) == QLatin1String("manual");
		return m;
	}

	//! "low res: 187 ppi (548x548 px)" -> "187 ppi"; "check BG: ..." -> "check BG"; "no face - check" -> "no face".
	QString shortCheckText(const Marks& m)
	{
		if (m.check == QLatin1String("lowres"))
		{
			const QRegularExpressionMatch ppi = QRegularExpression(QStringLiteral("(\\d+)\\s*ppi")).match(m.checkText);
			return ppi.hasMatch() ? ppi.captured(1) + QStringLiteral(" ppi") : QStringLiteral("low res");
		}
		if (m.check == QLatin1String("bg"))
			return QStringLiteral("check BG");
		return QStringLiteral("no face");
	}

	//! Where the badges of one frame go, in screen pixels from the frame's top-left corner.
	struct Plan
	{
		enum Mode { Hidden, Dot, Labels };
		Mode mode { Hidden };
		double frameW { 0.0 }, frameH { 0.0 };
		// Dot
		QRectF dot;
		QColor dotColor;
		// Labels
		double fontPx { 0.0 };
		QRectF linkRect;  QString linkLabel;  bool linkSquare { false };
		QRectF checkRect; QString checkLabel; bool checkSquare { false };
		QRectF manualRect;
		bool selected { false };
	};

	double zoomPercent(const Canvas* canvas)
	{
		const double ds = PrefsManager::instance().appPrefs.displayPrefs.displayScale;
		return canvas->scale() / (ds > 0.0 ? ds : 1.0) * 100.0;
	}

	QFont badgeFont(double px)
	{
		QFont f(QStringLiteral("Sans"));
		f.setBold(true);
		f.setPixelSize(qMax(7, qRound(px)));
		return f;
	}

	/*! \a full: the photo is selected (and not being dragged): full labels at the
	    configured size, in a row just above the photo, so the face stays clear. */
	Plan planFor(const PageItem* item, const Marks& m, double scale, double zoomPct, bool full)
	{
		Plan p;
		p.frameW = item->width() * scale;
		p.frameH = item->height() * scale;
		p.selected = full;
		const double side = qMin(p.frameW, p.frameH);
		const double configured = SuneerImageLinks::badgePixels();
		const double h = p.selected ? configured : qMin(configured, side * kPhotoShare);
		const bool tiny = !p.selected && (side < kMinPhotoPx || zoomPct < kMinZoomPercent || h < kMinBadgePx);
		if (tiny)
		{
			if (SuneerPhotoBadges::smallMode() == SuneerPhotoBadges::SmallNothing)
				return p;
			p.mode = Plan::Dot;
			const double d = qBound(6.0, side * 0.15, 10.0);
			const double inset = qMin(3.0, qMax(0.0, (side - d) / 2.0));
			p.dot = QRectF(inset, inset, d, d);
			const bool alert = !m.check.isEmpty() || m.link == SuneerImageLinks::Missing;
			p.dotColor = alert ? kAlertColor : m.linkBadge() ? kLinkColor : kManualColor;
			return p;
		}
		p.mode = Plan::Labels;
		const double inset = qBound(2.0, h * 0.3, 6.0);
		const double room = p.frameW - 2.0 * inset;  // width a label may take without leaving the photo
		const double rowGap = 2.0;
		double x = inset;
		double y = inset;
		if (m.linkBadge())
		{
			// The LINK tab is the quieter one: smaller than the check flag.
			const double lh = qMax(kMinBadgePx, qRound(h * 0.8) * 1.0);
			const QString word = m.link == SuneerImageLinks::Missing ? QStringLiteral("MISSING") : QStringLiteral("LINK");
			const QFontMetricsF fm(badgeFont(lh * 0.55));
			const double ww = fm.horizontalAdvance(word) + lh * 0.6;
			p.fontPx = lh * 0.55;
			if (ww <= room || p.selected)
			{
				p.linkLabel = word;
				p.linkRect = QRectF(x, y, ww, lh);
			}
			else
			{
				p.linkSquare = true;
				p.linkRect = QRectF(x, y, lh, lh);
			}
		}
		if (!m.check.isEmpty())
		{
			const QFontMetricsF fm(badgeFont(h * 0.55));
			const double pad = h * 0.5;
			QStringList candidates;
			candidates << m.checkText;
			if (!p.selected)
				candidates << shortCheckText(m);
			QString chosen;
			double cw = 0.0;
			for (const QString& c : candidates)
			{
				const double w = fm.horizontalAdvance(c) + pad;
				if (p.selected || w <= room)
				{
					chosen = c;
					cw = w;
					break;
				}
			}
			if (chosen.isEmpty())
			{
				p.checkSquare = true;
				chosen = QStringLiteral("!");
				cw = h;
			}
			p.checkLabel = chosen;
			// Beside the LINK tab when both fit across the photo, else on the next row.
			double cx = x, cy = y;
			if (!p.linkRect.isNull())
			{
				if (p.linkRect.right() + rowGap + cw <= inset + room && !p.selected)
					cx = p.linkRect.right() + rowGap;
				else
					cy = p.linkRect.bottom() + rowGap;
			}
			p.checkRect = QRectF(cx, cy, cw, h);
		}
		if (m.manual)
		{
			const double mh = qMax(kMinBadgePx, h * 0.7);
			p.manualRect = QRectF(p.frameW - inset - mh, inset, mh, mh);
			// no room beside the others on a narrow photo: under the first row
			const QRectF first = p.linkRect.isNull() ? p.checkRect : p.linkRect;
			if (!first.isNull() && p.manualRect.intersects(first.united(p.checkRect)))
				p.manualRect.moveTop(qMax(first.bottom(), p.checkRect.isNull() ? 0.0 : p.checkRect.bottom()) + rowGap);
		}
		if (p.selected)
		{
			// One row above the top edge, clear of the selection handles (they reach 3 px out).
			double rx = 0.0;
			for (QRectF* r : { &p.linkRect, &p.checkRect, &p.manualRect })
			{
				if (r->isNull())
					continue;
				r->moveTo(rx, -5.0 - r->height());
				rx = r->right() + 3.0;
			}
		}
		return p;
	}

	bool fullLabels(const PageItem* item, const Canvas* canvas)
	{
		// while dragging, the labels outside the frame would leave trails
		return item->isSelected() && !canvas->isItemMoving() && !canvas->isItemResizing();
	}

	//! Frame coordinates in screen pixels: the item's own rotation, the canvas zoom and scroll.
	QTransform screenTransform(const PageItem* item, const Canvas* canvas, const ScribusDoc* doc)
	{
		QTransform t;
		t.scale(canvas->scale(), canvas->scale());
		t.translate(-doc->minCanvasCoordinate.x(), -doc->minCanvasCoordinate.y());
		const double s = qMax(canvas->scale(), 0.0001);
		return QTransform::fromScale(1.0 / s, 1.0 / s) * item->getTransform() * t;
	}

	void drawTab(QPainter* painter, const QRectF& r, const QColor& fill, int alpha, const QString& text, double fontPx, bool cross)
	{
		QColor c = fill;
		c.setAlpha(alpha);
		const double outline = qMax(1.0, r.height() / 18.0);
		QColor white(255, 255, 255, qMin(255, alpha + 40));
		painter->setPen(QPen(white, outline));
		painter->setBrush(c);
		const double radius = r.height() * 0.18;
		painter->drawRoundedRect(r.adjusted(outline / 2, outline / 2, -outline / 2, -outline / 2), radius, radius);
		if (cross)
		{
			painter->setPen(QPen(Qt::white, r.height() * 0.12, Qt::SolidLine, Qt::RoundCap));
			painter->drawLine(QPointF(r.left() + r.width() * 0.30, r.top() + r.height() * 0.30), QPointF(r.left() + r.width() * 0.70, r.top() + r.height() * 0.70));
			painter->drawLine(QPointF(r.left() + r.width() * 0.70, r.top() + r.height() * 0.30), QPointF(r.left() + r.width() * 0.30, r.top() + r.height() * 0.70));
		}
		if (!text.isEmpty())
		{
			painter->setFont(badgeFont(fontPx));
			painter->setPen(Qt::white);
			painter->drawText(r, Qt::AlignCenter, text);
		}
	}

	void paintOne(QPainter* painter, const Canvas* canvas, ScribusDoc* doc, PageItem* item, const Marks& m, double zoomPct)
	{
		const Plan p = planFor(item, m, canvas->scale(), zoomPct, fullLabels(item, canvas));
		if (p.mode == Plan::Hidden)
			return;
		painter->save();
		painter->setTransform(screenTransform(item, canvas, doc), false);
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setRenderHint(QPainter::TextAntialiasing, true);
		if (p.mode == Plan::Dot)
		{
			painter->setPen(QPen(Qt::white, 1.2));
			painter->setBrush(p.dotColor);
			painter->drawEllipse(p.dot);
			painter->restore();
			return;
		}
		// Semi-transparent LINK so the face shows through; the problems are near opaque.
		if (!p.linkRect.isNull())
		{
			const bool missing = m.link == SuneerImageLinks::Missing;
			const int alpha = missing ? 230 : (p.selected ? 220 : 140);
			drawTab(painter, p.linkRect, missing ? kAlertColor : kLinkColor, alpha, p.linkLabel, p.fontPx, missing && p.linkSquare);
		}
		if (!p.checkRect.isNull())
			drawTab(painter, p.checkRect, kAlertColor, 235, p.checkLabel, p.checkRect.height() * 0.55, false);
		if (!p.manualRect.isNull())
			drawTab(painter, p.manualRect, kManualColor, 220, QStringLiteral("M"), p.manualRect.height() * 0.62, false);
		painter->restore();
	}

	template <typename F>
	void forEachImage(const QList<PageItem*>& items, ScribusDoc* doc, F&& f)
	{
		for (PageItem* item : items)
		{
			if (!item || !doc->layerVisible(item->m_layerID))
				continue;
			if (item->isGroup())
			{
				forEachImage(item->groupItemList, doc, f);
				continue;
			}
			if (item->isImageFrame())
				f(item);
		}
	}

	QRectF visibleDocRect(const Canvas* canvas)
	{
		const FPoint a = canvas->localToCanvas(QPointF(0, 0));
		const FPoint b = canvas->localToCanvas(QPointF(canvas->width(), canvas->height()));
		return QRectF(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
	}

	bool usable(const Canvas* canvas, ScribusDoc* doc)
	{
		return canvas && doc && !doc->drawAsPreview && SuneerImageLinks::badgesShown();
	}
}

int SuneerPhotoBadges::smallMode()
{
	if (s_smallMode < 0)
	{
		PrefsContext* ctx = prefsContext();
		if (!ctx)
			return SmallDot; // prefs not up yet: the default, and do not cache it
		s_smallMode = qBound(int(SmallDot), ctx->getInt("badge_small_mode", SmallDot), int(SmallNothing));
	}
	return s_smallMode;
}

void SuneerPhotoBadges::setSmallMode(int mode)
{
	s_smallMode = qBound(int(SmallDot), mode, int(SmallNothing));
	if (PrefsContext* ctx = prefsContext())
		ctx->set("badge_small_mode", s_smallMode);
}

void SuneerPhotoBadges::paint(QPainter* painter, const Canvas* canvas, ScribusDoc* doc)
{
	if (!painter || !usable(canvas, doc))
		return;
	const QRectF visible = visibleDocRect(canvas);
	const double zoomPct = zoomPercent(canvas);
	forEachImage(*doc->Items, doc, [&](PageItem* item) {
		const Marks m = marksOf(item);
		if (!m.any() || !item->getBoundingRect().intersects(visible))
			return;
		paintOne(painter, canvas, doc, item, m, zoomPct);
	});
}

QString SuneerPhotoBadges::toolTipAt(const Canvas* canvas, ScribusDoc* doc, const QPointF& widgetPos)
{
	if (!usable(canvas, doc))
		return QString();
	const FPoint dp = canvas->localToCanvas(widgetPos);
	const QPointF docPt(dp.x(), dp.y());
	PageItem* hit = nullptr;
	// the last one hit is the topmost
	forEachImage(*doc->Items, doc, [&](PageItem* item) {
		bool ok = false;
		const QPointF local = item->getTransform().inverted(&ok).map(docPt);
		if (ok && QRectF(0, 0, item->width(), item->height()).contains(local) && marksOf(item).any())
			hit = item;
	});
	if (!hit)
		return QString();
	const Marks m = marksOf(hit);
	QStringList lines;
	lines << QStringLiteral("<b>%1</b>").arg(hit->itemName().toHtmlEscaped());
	if (!m.check.isEmpty())
		lines << QStringLiteral("<span style='color:#d61e1e'><b>%1</b></span>").arg(m.checkText.toHtmlEscaped());
	if (m.link == SuneerImageLinks::Linked)
		lines << QObject::tr("Linked (not embedded): %1").arg(QDir::toNativeSeparators(hit->Pfile).toHtmlEscaped());
	else if (m.link == SuneerImageLinks::Missing)
		lines << QObject::tr("Missing file: %1").arg(QDir::toNativeSeparators(hit->Pfile).toHtmlEscaped());
	else if (m.link == SuneerImageLinks::Embedded)
		lines << QObject::tr("Embedded");
	if (m.manual)
		lines << QObject::tr("Background mask: manual (Edit Photo)");
	return lines.join(QStringLiteral("<br>"));
}

QStringList SuneerPhotoBadges::describe(const Canvas* canvas, ScribusDoc* doc)
{
	QStringList out;
	if (!canvas || !doc)
		return out;
	const double zoomPct = zoomPercent(canvas);
	const bool shown = SuneerImageLinks::badgesShown();
	forEachImage(*doc->Items, doc, [&](PageItem* item) {
		const Marks m = marksOf(item);
		if (!m.any())
			return;
		const Plan p = planFor(item, m, canvas->scale(), zoomPct, fullLabels(item, canvas));
		QString what;
		if (!shown)
			what = QStringLiteral("off");
		else if (p.mode == Plan::Hidden)
			what = QStringLiteral("nothing");
		else if (p.mode == Plan::Dot)
			what = QStringLiteral("dot %1px %2").arg(p.dot.width(), 0, 'f', 0).arg(p.dotColor.name());
		else
		{
			QStringList parts;
			auto share = [&](const QRectF& r) { return QStringLiteral("%1x%2px=%3%").arg(r.width(), 0, 'f', 0).arg(r.height(), 0, 'f', 0).arg(100.0 * r.width() * r.height() / qMax(1.0, p.frameW * p.frameH), 0, 'f', 1); };
			if (!p.linkRect.isNull())
				parts << QStringLiteral("link[%1] %2").arg(p.linkSquare ? QStringLiteral("square") : p.linkLabel, share(p.linkRect));
			if (!p.checkRect.isNull())
				parts << QStringLiteral("check[%1] %2").arg(p.checkLabel, share(p.checkRect));
			if (!p.manualRect.isNull())
				parts << QStringLiteral("M %1").arg(share(p.manualRect));
			what = parts.join(QStringLiteral("; "));
		}
		out << QStringLiteral("%1 photo=%2x%3px sel=%4: %5").arg(item->itemName()).arg(p.frameW, 0, 'f', 0).arg(p.frameH, 0, 'f', 0).arg(p.selected ? 1 : 0).arg(what);
	});
	return out;
}

void SuneerPhotoBadges::runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc)
{
	static bool started = false;
	const QString prefix = qEnvironmentVariable("SUNEER_BADGE_SELFTEST");
	if (started || prefix.isEmpty() || !view || !doc)
		return;
	started = true;
	QTimer::singleShot(2500, view, [view, doc, prefix]() {
		QFile rf(prefix + QStringLiteral("-report.txt"));
		if (!rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return;
		QTextStream ts(&rf);
		Canvas* canvas = view->m_canvas;
		const double ds = PrefsManager::instance().appPrefs.displayPrefs.displayScale;
		const bool wasShown = SuneerImageLinks::badgesShown();
		const int wasMode = smallMode();
		SuneerImageLinks::setBadgesShown(true);
		setSmallMode(SmallDot);
		// the badged photo nearest the page's top-left corner is the one to look at
		PageItem* focus = nullptr;
		forEachImage(*doc->Items, doc, [&](PageItem* item) {
			if (!focus && marksOf(item).any() && !marksOf(item).check.isEmpty())
				focus = item;
		});
		auto shot = [&](const QString& name, double pct) {
			if (focus)
				view->zoom(qRound(focus->xPos() + focus->width() / 2.0), qRound(focus->yPos() + focus->height() / 2.0), pct / 100.0 * ds, false);
			else
				view->zoom(pct / 100.0 * ds);
			view->DrawNew();
			for (int i = 0; i < 6; ++i)
				QApplication::processEvents();
			const QString png = prefix + QStringLiteral("-") + name + QStringLiteral(".png");
			const bool saved = view->viewport()->grab().save(png);
			ts << "== " << name << " zoom=" << QString::number(zoomPercent(canvas), 'f', 1) << "% canvas=" << canvas->width() << "x" << canvas->height()
			   << " saved=" << (saved ? 1 : 0) << " " << png << "\n";
			for (const QString& l : describe(canvas, doc))
				ts << "  " << l << "\n";
			ts.flush();
		};
		doc->m_Selection->clear();
		shot(QStringLiteral("z025"), 25);
		shot(QStringLiteral("z050"), 50);
		shot(QStringLiteral("z100"), 100);
		shot(QStringLiteral("z200"), 200);
		if (focus)
		{
			doc->m_Selection->addItem(focus);
			shot(QStringLiteral("z050-selected"), 50);
			shot(QStringLiteral("z025-selected"), 25);
			ts << "tooltip(" << focus->itemName() << "): "
			   << toolTipAt(canvas, doc, canvas->canvasToLocal(QPointF(focus->xPos() + focus->width() / 2.0, focus->yPos() + focus->height() / 2.0))) << "\n";
			doc->m_Selection->clear();
		}
		setSmallMode(SmallNothing);
		shot(QStringLiteral("z025-nothing"), 25);
		setSmallMode(SmallDot);
		SuneerImageLinks::setBadgesShown(false);
		shot(QStringLiteral("z100-off"), 100);
		// timing of the overlay itself at 25 %, where every photo of the page is on screen
		SuneerImageLinks::setBadgesShown(true);
		view->zoom(0.25 * ds);
		for (int i = 0; i < 4; ++i)
			QApplication::processEvents();
		{
			QImage img(canvas->size(), QImage::Format_ARGB32_Premultiplied);
			QElapsedTimer t;
			t.start();
			for (int i = 0; i < 20; ++i)
			{
				QPainter qp(&img);
				paint(&qp, canvas, doc);
			}
			ts << "overlay paint at 25%: " << QString::number(t.nsecsElapsed() / 20.0 / 1e6, 'f', 3) << " ms per paint\n";
		}
		SuneerImageLinks::setBadgesShown(wasShown);
		setSmallMode(wasMode);
		if (ScribusMainWindow* mw = ScCore->primaryMainWindow())
			if (mw->scrActions.contains(QStringLiteral("viewShowPhotoBadges")))
				ts << "action viewShowPhotoBadges: '" << mw->scrActions[QStringLiteral("viewShowPhotoBadges")]->text() << "' key '"
				   << mw->scrActions[QStringLiteral("viewShowPhotoBadges")]->shortcut().toString() << "'\n";
		ts << "done\n";
		rf.close();
		if (!qEnvironmentVariableIsSet("SUNEER_BADGE_SELFTEST_HOLD"))
			QTimer::singleShot(200, qApp, []() { QApplication::exit(0); });
	});
}
