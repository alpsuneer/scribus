/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerlayoutengine.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <functional>
#include <memory>
#include <QCryptographicHash>
#include <QDateTime>
#include <QImageReader>
#include <QSet>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QCoreApplication>

#include "commonstrings.h"
#include "hyphenator.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "prefsmanager.h"
#include "scpage.h"
#include "scpainter.h"
#include "scribuscore.h"
#include "scribusdoc.h"
#include "scribus.h"
#include "ui/guidemanager.h"
#include "scribusXml.h"
#include "suneerclusterlevel.h"
#include "suneertextclean.h"
#include "selection.h"
#include "styles/styleset.h"
#include "text/specialchars.h"
#include "ui/suneer_obit_model.h"
#include "undomanager.h"

namespace SuneerLayout
{
namespace {
const double kMm = 72.0 / 25.4;

QString settingsStyle(const char* key, const char* def)
{
	QSettings cfg("Faircode", "ScribusNews");
	const QString v = cfg.value(QLatin1String(key)).toString().trimmed();
	return v.isEmpty() ? QString::fromLatin1(def) : v;
}

bool hasStyle(ScribusDoc* doc, const QString& name)
{
	return doc && !name.isEmpty() && doc->paragraphStyles().contains(name);
}

// "18 M" -> 18
double sizeFromName(const QString& name, double def)
{
	static const QRegularExpression re(QStringLiteral("^\\s*(\\d+(?:\\.\\d+)?)"));
	const QRegularExpressionMatch m = re.match(name);
	return m.hasMatch() ? m.captured(1).toDouble() : def;
}

QString pickFont(const QStringList& wanted)
{
	const SCFonts& fonts = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts;
	for (const QString& w : wanted)
		for (auto it = fonts.constBegin(); it != fonts.constEnd(); ++it)
			if (it.value().usable() && it.key().contains(w, Qt::CaseInsensitive))
				return it.key();
	return QString();
}
} // namespace

QString keepTitle(Keep k)
{
	switch (k)
	{
		case Keep::PlacedFrames: return QObject::tr("Placed frames");
		case Keep::EveryFrame: return QObject::tr("Every frame on the page");
		case Keep::OnlyNew: return QObject::tr("Only new stories");
	}
	return QString();
}

QString keepTip(Keep k)
{
	switch (k)
	{
		case Keep::PlacedFrames: return QObject::tr("Frames placed by hand stay where they are (ads, masthead, folio, other non-story frames and modules pinned with \"Pin position\"). Every News Browser story - on the page and new - is arranged by the model.");
		case Keep::EveryFrame: return QObject::tr("Every frame already on the page stays where it is. Only the stories not yet on the page are arranged, in the free space.");
		case Keep::OnlyNew: return QObject::tr("Only the new stories are arranged, around the hand-placed frames. The stories already on the page are left out of the count (they would make room).");
	}
	return QString();
}

StyleNames StyleNames::fromSettings(const QString& title)
{
	StyleNames s;
	s.title = title.trimmed().isEmpty() ? settingsStyle("style/titleStyle", "32 M") : title.trimmed();
	s.body = settingsStyle("style/bodyStyle", "02 BodyText");
	s.firstBody = settingsStyle("style/firstBodyStyle", "03 BodyNoInd");
	s.kicker = settingsStyle("style/kickerStyle", "12 Kicker");
	s.byline = settingsStyle("style/bylineStyle", "07 Byline");
	s.dateline = settingsStyle("style/datelineStyle", "01 Dateline");
	s.highlights = settingsStyle("style/highlightsStyle", "05 Highlights");
	s.caption = settingsStyle("style/captionStyle", "09 Caption");
	return s;
}

QString PlanOut::fitLine() const
{
	if (total == 0)
		return QObject::tr("No stories to arrange · %1 pt free").arg(qRound(freePt));
	const int fit = total - notFit.size();
	return QObject::tr("%1 of %2 fit · %3 pt free").arg(fit).arg(total).arg(qRound(freePt));
}

Grid gridFor(const SuneerLayoutModel& model, double pageW, double pageH, double mTop, double mBottom, double mLeft, double mRight, int guideCols)
{
	double mt = mTop, mb = mBottom, ml = mLeft, mr = mRight;
	if (!model.useDocMargins)
	{
		mt = model.marginTopMm * kMm;
		mb = model.marginBottomMm * kMm;
		ml = model.marginLeftMm * kMm;
		mr = model.marginRightMm * kMm;
	}
	if (model.isObituary())
	{
		SuneerObitModel om;
		if (!model.obituary.isEmpty())
			om.fromJson(model.obituary);
		mt = om.contentTopMm * kMm;
	}
	Grid g;
	g.content = QRectF(ml, mt, qMax(50.0, pageW - ml - mr), qMax(50.0, pageH - mt - mb));
	g.cols = qMax(1, (model.gridFromGuides && guideCols > 0) ? guideCols : model.columns);
	g.gutter = model.gutterMm * kMm;
	g.colW = model.columnWidthMm > 0 ? model.columnWidthMm * kMm : (g.content.width() - g.gutter * (g.cols - 1)) / g.cols;
	return g;
}

Grid gridFor(const SuneerLayoutModel& model, const PageIn& page)
{
	return gridFor(model, page.pageW, page.pageH, page.mTop, page.mBottom, page.mLeft, page.mRight, page.guideCols);
}

bool isObitModel(const SuneerLayoutModel& model)
{
	if (model.isObituary())
		return true;
	if (!SuneerLayoutModels::exists(QStringLiteral("obituary")))
		return false;
	const SuneerLayoutModel ob = SuneerLayoutModels::savedById(QStringLiteral("obituary"));
	for (const QString& t : ob.pageTypes)
		if (!t.trimmed().isEmpty() && !model.pageTypes.isEmpty() && model.appliesTo(t))
			return true;
	return false;
}

bool isObitPage(const PageIn& page)
{
	int obits = 0, placed = 0;
	for (const StoryIn& s : page.stories)
	{
		if (!s.placed)
			continue;
		++placed;
		if (s.kind == QLatin1String("obit"))
			++obits;
	}
	if (obits >= 6 && obits * 2 >= placed)
		return true;
	if (!page.pageKey.trimmed().isEmpty() && SuneerLayoutModels::exists(QStringLiteral("obituary")))
	{
		const SuneerLayoutModel ob = SuneerLayoutModels::savedById(QStringLiteral("obituary"));
		if (ob.appliesTo(page.pageKey) && obits > 0)
			return true;
	}
	return false;
}

SuneerLayoutModel obitVariant(const SuneerLayoutModel& model, const PageIn& page)
{
	Q_UNUSED(page);
	if (model.isObituary() || model.obitPacking || !SuneerLayoutModels::exists(QStringLiteral("obituary")))
		return model;
	const SuneerLayoutModel ob = SuneerLayoutModels::savedById(QStringLiteral("obituary"));
	SuneerLayoutModel v = model;
	v.obitPacking = true;
	// the obituary grid: its columns and its horizontal gap; the page's margins as the Obituary model uses them
	v.columns = ob.columns;
	v.gridFromGuides = false;
	v.columnWidthMm = 0.0;
	v.gutterMm = ob.gutterMm;
	v.useDocMargins = ob.useDocMargins;
	v.marginTopMm = ob.marginTopMm;
	v.marginBottomMm = ob.marginBottomMm;
	v.marginLeftMm = ob.marginLeftMm;
	v.marginRightMm = ob.marginRightMm;
	// the obituary vertical gap between modules; the story's inner gaps stay the model's
	v.gapVMm = ob.gapVMm;
	// no row alignment: free column packing
	v.decks = 0;
	v.leftRail = false;
	v.rightRail = false;
	return v;
}

double maxGapAbove(const QList<QRectF>& rects, double gapV, int* over, int* worst)
{
	double best = 0;
	int n = 0, w = -1;
	for (int i = 0; i < rects.size(); ++i)
	{
		const QRectF& b = rects[i];
		double g = -1;
		for (int j = 0; j < rects.size(); ++j)
		{
			if (i == j)
				continue;
			const QRectF& a = rects[j];
			const bool hOverlap = a.right() > b.left() + 0.5 && a.left() < b.right() - 0.5;
			if (!hOverlap || a.bottom() > b.top() + 0.5)
				continue;
			const double d = b.top() - a.bottom();
			if (g < 0 || d < g)
				g = d;
		}
		if (g < 0)
			continue;                // nothing above: the top of a column
		if (g > gapV + 0.5)
			++n;
		if (g > best)
		{
			best = g;
			w = i;
		}
	}
	if (over)
		*over = n;
	if (worst)
		*worst = w;
	return best;
}

bool isModelHeadlineStyle(const QString& name, const StyleNames& styles)
{
	static const QRegularExpression re(QStringLiteral("^\\s*\\d+(\\.\\d+)?\\s*[MB]\\s*$"));
	return !name.isEmpty() && (name == styles.title || re.match(name).hasMatch());
}

int storyHead(const StoryText& st, const StyleNames& styles, int* headlinePos)
{
	const QStringList bodyish { styles.body, styles.firstBody, styles.byline, styles.dateline, styles.highlights, styles.caption };
	int headline = -1;
	int firstBody = -1;
	int pos = 0;
	while (pos < st.length())
	{
		const QString parent = st.paragraphStyle(pos).parent();
		if (bodyish.contains(parent))
		{
			firstBody = pos;
			break;
		}
		if (parent != styles.kicker)
			headline = pos;
		int end = pos;
		while (end < st.length() && st.text(end) != SpecialChars::PARSEP)
			++end;
		if (end >= st.length())
			break;
		pos = end + 1;
	}
	if (headlinePos)
		*headlinePos = headline;
	return firstBody;
}

// ---------------------------------------------------------------------------
// Measurer
// ---------------------------------------------------------------------------

QString Measurer::Spec::key() const
{
	return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
	    .arg(width, 0, 'f', 1).arg(cols).arg(colGap, 0, 'f', 2).arg(inset, 0, 'f', 2).arg(headStyle).arg(headExtra, 0, 'f', 2)
	    .arg(photoPos).arg(photoCols).arg(gridColW, 0, 'f', 1)
	  + QStringLiteral("|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
	    .arg(gutter, 0, 'f', 2).arg(wrap, 0, 'f', 2).arg(captionGap, 0, 'f', 2).arg(captionText, 0, 'f', 2).arg(photoText, 0, 'f', 2)
	    .arg(maxPhotoH, 0, 'f', 1).arg(obit).arg(obitName + obitPlace + obitBody).arg(obitPhotoH, 0, 'f', 1).arg(maxH, 0, 'f', 0)
	  + QStringLiteral("|k%1").arg(kickerExtra, 0, 'f', 2);
}

Measurer::Measurer(const PageIn& page, double scale) : m_src(page.doc), m_styles(page.styles), m_scale(scale)
{
	UndoBlocker blocker;
	if (page.doc && page.pageIndex >= 0 && page.pageIndex < page.doc->Pages->count())
		m_origin = QPointF(page.doc->Pages->at(page.pageIndex)->xOffset(), page.doc->Pages->at(page.pageIndex)->yOffset());
	m_doc = new ScribusDoc();
	m_doc->setup(0, 1, 1, 1, 1, QStringLiteral("Custom"), QStringLiteral("Custom"));
	m_doc->setPage(qMax(10.0, page.pageW), qMax(10.0, page.pageH), 0, 0, 0, 0, 0, 0, false, false);
	m_doc->addPage(0);
	m_doc->setGUI(false, ScCore->primaryMainWindow(), nullptr);
	m_doc->setHyphAutomatic(true);
	m_doc->setHyphAutoCheck(false);
	// a document from an older Scribus breaks Malayalam lines the old way: measured the same way
	suneerSetDocUsesCharClusters(m_doc, suneerDocUsesCharClusters(m_src));
	if (m_src)
	{
		m_doc->PageColors = m_src->PageColors;
		m_doc->redefineCharStyles(m_src->charStyles(), false);
		m_doc->redefineStyles(m_src->paragraphStyles(), false);
		QStringList fonts;
		for (int i = 0; i < m_src->paragraphStyles().count(); ++i)
			fonts << m_src->paragraphStyles()[i].charStyle().font().scName();
		for (int i = 0; i < m_src->charStyles().count(); ++i)
			fonts << m_src->charStyles()[i].font().scName();
		fonts << m_src->usedFonts().keys();          // fonts set on the text itself
		fonts.removeDuplicates();
		for (const QString& f : fonts)
			if (!f.isEmpty() && PrefsManager::instance().appPrefs.fontPrefs.AvailFonts.contains(f))
				m_doc->AddFont(f);
		m_defaultFont = m_src->itemToolPrefs().textFont;
	}
	else
	{
		// No document: house-like styles with a Malayalam font, under the names the News Browser uses.
		m_defaultFont = pickFont({ QStringLiteral("DBI Text"), QStringLiteral("Noto Sans Malayalam"), QStringLiteral("Noto Serif Malayalam"),
		                           QStringLiteral("Malayalam"), QStringLiteral("Manjari"), QStringLiteral("Rachana"), QStringLiteral("Meera") });
		QString headFont = pickFont({ QStringLiteral("DZ DB Display"), QStringLiteral("Noto Sans Malayalam Bold"), QStringLiteral("Noto Serif Malayalam Bold"), QStringLiteral("Manjari Bold") });
		if (headFont.isEmpty())
			headFont = m_defaultFont;
		const SCFonts& av = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts;
		StyleSet<ParagraphStyle> set;
		auto add = [&](const QString& name, const QString& font, double size, double lead, ParagraphStyle::AlignmentType align, double after) {
			if (name.isEmpty() || set.contains(name))
				return;
			ParagraphStyle ps;
			ps.setName(name);
			if (!font.isEmpty() && av.contains(font))
				ps.charStyle().setFont(av[font]);
			ps.charStyle().setFontSize(size * 10.0);
			ps.charStyle().setLanguage(QStringLiteral("ml"));
			ps.setLineSpacingMode(ParagraphStyle::FixedLineSpacing);
			ps.setLineSpacing(lead);
			ps.setAlignment(align);
			ps.setGapAfter(after);
			ps.setHyphenationMode(ParagraphStyle::HyphenationMode::AutomaticHyphenation);
			set.create(ps);
		};
		add(m_styles.body, m_defaultFont, 9.3, 10.3, ParagraphStyle::Justified, 0);
		add(m_styles.firstBody, m_defaultFont, 9.3, 10.3, ParagraphStyle::Justified, 0);
		add(m_styles.dateline, headFont, 9.3, 10.3, ParagraphStyle::LeftAligned, 0);
		add(m_styles.byline, headFont, 9.3, 10.3, ParagraphStyle::LeftAligned, 0);
		add(m_styles.highlights, headFont, 10.0, 11.5, ParagraphStyle::LeftAligned, 2);
		add(m_styles.kicker, headFont, 12.0, 14.0, ParagraphStyle::LeftAligned, 0);
		add(m_styles.caption, m_defaultFont, 8.5, 9.5, ParagraphStyle::LeftAligned, 0);
		for (const QString& h : { m_styles.title, QStringLiteral("16 M"), QStringLiteral("18 M"), QStringLiteral("20 M"), QStringLiteral("22 M"), QStringLiteral("24 M"),
		                          QStringLiteral("28 M"), QStringLiteral("32 M"), QStringLiteral("36 M"), QStringLiteral("40 M"), QStringLiteral("48 M"), QStringLiteral("68 B"), QStringLiteral("80 B") })
		{
			const double sz = sizeFromName(h, 24.0);
			add(h, headFont, sz, sz * 0.88, ParagraphStyle::LeftAligned, sz >= 60 ? 10 : 1.5);
		}
		m_doc->redefineStyles(set, false);
		if (!m_defaultFont.isEmpty())
			m_doc->AddFont(m_defaultFont);
		if (!headFont.isEmpty() && headFont != m_defaultFont)
			m_doc->AddFont(headFont);
	}
	m_text = newFrame();
}

Measurer::~Measurer()
{
	UndoBlocker blocker;
	delete m_doc;           // deletes its items
}

PageItem_TextFrame* Measurer::newFrame()
{
	auto* t = new PageItem_TextFrame(m_doc, 0, 0, 100, 100, 0, CommonStrings::None, CommonStrings::None);
	t->setSampleItem(true);
	t->OwnPage = 0;
	ParagraphStyle def(t->itemText.defaultStyle());
	def.setHyphenationMode(ParagraphStyle::HyphenationMode::AutomaticHyphenation);
	def.charStyle().setLanguage(QStringLiteral("ml"));
	if (!m_src && !m_defaultFont.isEmpty())
	{
		def.charStyle().setFont(PrefsManager::instance().appPrefs.fontPrefs.AvailFonts[m_defaultFont]);
		def.charStyle().setFontSize(93.0);
	}
	t->itemText.setDefaultStyle(def);
	t->setTextToFrameDist(0.0, 0.0, 0.0, 0.0);
	t->setTextFlowMode(PageItem::TextFlowDisabled);
	m_doc->Items->append(t);
	return t;
}

void Measurer::fillText(PageItem_TextFrame* t, const StoryIn& s, const Spec& spec, int* firstBodyPos)
{
	StoryText& st = t->itemText;
	st.clear();
	*firstBodyPos = -1;
	auto addPara = [&](const QString& text, const QString& style) {
		if (SuneerTextClean::trimBlank(text).isEmpty())
			return;
		const int start = st.length();
		st.insertChars(start, text);
		st.insertChars(st.length(), SpecialChars::PARSEP);
		if (hasStyle(m_doc, style))
		{
			ParagraphStyle ps;
			ps.setParent(style);
			st.applyStyle(start, ps, false);
		}
	};
	if (s.placed && s.textItem)
	{
		// the placed text with every edit, as it is in the document - cleaned of empty lines
		// at the ends, as the re-layout cleans the real story
		st.insert(0, s.textItem->itemText);
		SuneerTextClean::cleanStoryText(st);
		if (spec.obit)
			return;
		// a placed story keeps its text; its headline takes the model's style for the width
		// only while it still has a News Browser / model headline style (as the real re-layout does)
		int headlinePos = -1;
		*firstBodyPos = storyHead(st, m_styles, &headlinePos);
		if (headlinePos >= 0 && hasStyle(m_doc, spec.headStyle) && isModelHeadlineStyle(st.paragraphStyle(headlinePos).parent(), m_styles))
		{
			ParagraphStyle ps;
			ps.setParent(spec.headStyle);
			st.applyStyle(headlinePos, ps, false);
		}
		return;
	}
	if (spec.obit)
	{
		addPara(s.title, spec.obitName);
		addPara(s.dateline, spec.obitPlace);
		addPara(s.body.join(QStringLiteral(" ")), spec.obitBody);
	}
	else
	{
		addPara(s.kicker, m_styles.kicker);
		if (spec.kickerExtra > 0.0 && !s.kicker.trimmed().isEmpty() && st.length() > 0)
		{
			// extra space under the kicker / subheading (the placement does the same)
			const int lastPara = st.length() - 1;
			ParagraphStyle ps;
			ps.setGapAfter(st.paragraphStyle(lastPara).gapAfter() + spec.kickerExtra);
			st.applyStyle(lastPara, ps, false);
		}
		addPara(s.title, spec.headStyle.isEmpty() ? m_styles.title : spec.headStyle);
		if (spec.headExtra > 0.0 && st.length() > 0)
		{
			const int lastPara = st.length() - 1;
			ParagraphStyle ps;
			ps.setGapAfter(st.paragraphStyle(lastPara).gapAfter() + spec.headExtra);
			st.applyStyle(lastPara, ps, false);
		}
		*firstBodyPos = st.length();
		for (const QString& h : s.highlights)
			addPara(h, hasStyle(m_doc, m_styles.highlights) ? m_styles.highlights : m_styles.firstBody);
		addPara(s.byline, m_styles.byline);
		addPara(s.dateline, m_styles.dateline);
		bool first = true;
		for (const QString& p : s.body)
		{
			addPara(p, first ? m_styles.firstBody : m_styles.body);
			first = false;
		}
		if (*firstBodyPos >= st.length())
			*firstBodyPos = -1;
	}
	// no empty line under the last one (the placement does the same)
	SuneerTextClean::cleanStoryText(st);
	if (*firstBodyPos >= st.length())
		*firstBodyPos = -1;
	if (st.length() > 0)
	{
		Hyphenator hy(nullptr, m_doc);
		hy.slotHyphenate(t);
		m_doc->DoDrawing = true;
	}
}

double Measurer::fitHeight(PageItem_TextFrame* t, double lo, double hi, bool* overflow)
{
	// to the last line, exactly as the placement fits (SuneerNewsPanel::fitFrame)
	return SuneerTextClean::fitHeight(t, lo, hi, overflow);
}

bool Measurer::cached(const StoryIn& s, const Spec& spec) const
{
	return m_cache.contains(s.key + QLatin1Char('#') + spec.key());
}

const Measurer::Result& Measurer::measure(const StoryIn& s, const Spec& spec)
{
	const QString key = s.key + QLatin1Char('#') + spec.key();
	auto found = m_cache.find(key);
	if (found != m_cache.end())
		return found.value();
	if (s.placed && s.rigid)
	{
		// moved as a block: its own size, its real items as the picture
		Result r;
		r.h = s.placedRect.height();
		r.w = s.placedRect.width();
		if (m_scale > 0 && m_src)
		{
			QImage img(qMax(1, (int) std::ceil(r.w * m_scale)), qMax(1, (int) std::ceil(r.h * m_scale)), QImage::Format_ARGB32_Premultiplied);
			img.fill(Qt::white);
			renderItems(img, m_scale, s.items, m_origin + s.placedRect.topLeft());
			r.image = img;
		}
		return m_cache.insert(key, r).value();
	}
	UndoBlocker blocker;
	QImage headImg;
	const double headStack = (s.placed && !s.heads.isEmpty()) ? measureHeads(s, qMax(10.0, spec.width), &headImg) + spec.headExtra : 0.0;
	Result r = measureBody(s, spec);
	if (headStack > 0)
	{
		// separate headline frames above the body frame: the module grows by their height
		for (QRectF& ph : r.photos)
			ph.translate(0, headStack);
		r.headH += headStack;
		if (m_scale > 0 && !r.image.isNull())
		{
			QImage img(r.image.width(), qMax(1, (int) std::ceil((r.h + headStack) * m_scale)), QImage::Format_ARGB32_Premultiplied);
			img.fill(Qt::white);
			QPainter qp(&img);
			if (!headImg.isNull())
				qp.drawImage(0, 0, headImg);
			qp.drawImage(0, qRound(headStack * m_scale), r.image);
			qp.end();
			r.image = img;
		}
		r.h += headStack;
	}
	if (debugOn() && s.placed && s.textItem)
	{
		const StoryText& st = s.textItem->itemText;
		QStringList tail, heads;
		for (int i = qMax(0, st.length() - 3); i < st.length(); ++i)
			tail << QString::number(st.text(i).unicode(), 16);
		for (const QPointer<PageItem>& h : s.heads)
			if (h)
				heads << QStringLiteral("%1 chars").arg(h->itemText.length());
		fprintf(stderr, "LAYOUT-MEASURE src=%p \"%s\" w=%.1f cols=%d text=%d chars (last %s, clusters %s) heads=[%s] headStack=%.1f h=%.1f pt\n", (void*) m_src,
		        qPrintable(s.title.left(16)), spec.width, spec.cols, st.length(), qPrintable(tail.join(',')),
		        s.textItem->getObjectAttributes(QStringLiteral("SuneerClusterLevel")).isEmpty() ? (m_src && m_src->property("suneerCharClusters").toBool() ? "doc-chars" : "graphemes") : "item-chars",
		        qPrintable(heads.join(',')), headStack, r.h);
	}
	return m_cache.insert(key, r).value();
}

double Measurer::measureHeads(const StoryIn& s, double width, QImage* image)
{
	double y = 0;
	QList<PageItem_TextFrame*> used;
	for (int i = 0; i < s.heads.size(); ++i)
	{
		if (!s.heads[i] || !s.heads[i]->isTextFrame())
			continue;
		while (m_headFrames.size() <= i)
			m_headFrames << newFrame();
		PageItem_TextFrame* h = m_headFrames[i];
		h->itemText.clear();
		h->itemText.insert(0, s.heads[i]->itemText);
		h->setXYPos(0, y);
		h->setWidth(width);
		h->setColumns(1);
		h->setTextToFrameDist(0, 0, 0, 0);
		h->SetRectFrame();
		bool over = false;
		y += fitHeight(h, 3.0, 600.0, &over);
		used << h;
	}
	if (image && m_scale > 0 && y > 0)
	{
		QImage img(qMax(1, (int) std::ceil(width * m_scale)), qMax(1, (int) std::ceil(y * m_scale)), QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::white);
		std::unique_ptr<ScPainter> p(new ScPainter(&img, img.width(), img.height(), 1.0, 0));
		p->setZoomFactor(m_scale);
		for (PageItem_TextFrame* h : used)
			h->DrawObj(p.get(), QRectF(-10000, -10000, 20000, 20000));
		p->end();
		*image = img;
	}
	for (PageItem_TextFrame* h : used)
		h->setXYPos(-5000, -5000);
	return y;
}

Measurer::Result Measurer::measureBody(const StoryIn& s, const Spec& spec)
{
	Result r;
	PageItem_TextFrame* t = m_text;
	const double w = qMax(10.0, spec.width);
	t->setXYPos(0, 0);
	t->setWidth(w);
	t->setHeight(spec.maxH);
	t->setColumns(qMax(1, spec.cols));
	t->setColumnGap(spec.colGap);
	t->setTextToFrameDist(spec.inset, spec.inset, spec.inset, spec.inset);
	t->SetRectFrame();
	t->updateClip();
	for (PageItem_TextFrame* o : m_obstacles)
	{
		o->setTextFlowMode(PageItem::TextFlowDisabled);
		o->setXYPos(-5000, -5000);
	}
	for (PageItem_TextFrame* c : m_captions)
	{
		c->setTextFlowMode(PageItem::TextFlowDisabled);
		c->setXYPos(-5000, -5000);
	}
	int firstBody = -1;
	fillText(t, s, spec, &firstBody);
	QList<QRectF> captionRects;
	QList<PhotoIn> photos = (spec.photoPos == QLatin1String("none")) ? QList<PhotoIn>() : s.photos;

	if (spec.obit)
	{
		// photo frame on top (when the obit has a picture), the text under it
		double ty = 0;
		if (!photos.isEmpty() && spec.obitPhotoH > 0)
		{
			r.photos << QRectF(0, 0, w, spec.obitPhotoH);
			ty = spec.obitPhotoH + spec.photoText;
			photos = photos.mid(0, 1);
		}
		else
			photos.clear();
		t->setXYPos(0, ty);
		t->setColumns(1);
		bool over = false;
		const double th = fitHeight(t, 5.0, spec.maxH, &over);
		r.overflow = over;
		r.h = ty + th;
		render(r, t, nullptr, photos, captionRects);
		return r;
	}

	// the headline block height: where the first body line starts
	t->invalidateLayout();
	t->layout();
	if (firstBody > 0 && firstBody < t->itemText.length())
	{
		const QLineF caret = t->textLayout.positionToPoint(firstBody);
		r.headH = qMax(0.0, qMin(caret.y1(), caret.y2()));
	}
	if (photos.isEmpty())
	{
		bool over = false;
		r.h = fitHeight(t, 5.0, spec.maxH, &over);
		r.overflow = over;
		render(r, t, nullptr, photos, captionRects);
		return r;
	}

	const bool fullWidth = (spec.photoPos == QLatin1String("top") || spec.photoPos == QLatin1String("topAbove"));
	const int gridCols = qMax(1, qRound((w + spec.gutter) / (spec.gridColW + spec.gutter)));
	const int n = fullWidth ? gridCols : qBound(1, spec.photoCols, gridCols);
	int startCol = 0;
	if (spec.photoPos == QLatin1String("right"))
		startCol = gridCols - n;
	else if (spec.photoPos == QLatin1String("middle") || spec.photoPos == QLatin1String("bottom"))
		startCol = (gridCols - n) / 2;
	const double pw = fullWidth ? w : spec.gridColW * n + (n - 1) * spec.gutter;
	const double px = fullWidth ? 0 : startCol * (spec.gridColW + spec.gutter);
	const bool wrap = !(spec.photoPos == QLatin1String("topAbove") || spec.photoPos == QLatin1String("bottom"));

	while (m_obstacles.size() < photos.size())
		m_obstacles << newFrame();
	while (m_captions.size() < photos.size())
		m_captions << newFrame();

	auto stack = [&](double y0) {
		double cy = y0;
		for (int i = 0; i < photos.size(); ++i)
		{
			double ph = pw * qBound(0.2, photos[i].aspect, 3.0);
			if (spec.maxPhotoH > 0)
				ph = qMin(ph, spec.maxPhotoH);
			PageItem_TextFrame* o = m_obstacles[i];
			o->setXYPos(px, cy);
			o->setWidth(pw);
			o->setHeight(ph);
			o->SetRectFrame();
			o->updateClip();
			o->setWrapOffsets(spec.wrap, spec.wrap, spec.wrap, spec.wrap);
			o->setTextFlowMode(wrap ? PageItem::TextFlowUsesBoundingBox : PageItem::TextFlowDisabled);
			r.photos << QRectF(px, cy, pw, ph);
			cy += ph;
			PageItem_TextFrame* c = m_captions[i];
			c->itemText.clear();
			if (!photos[i].caption.trimmed().isEmpty())
			{
				cy += spec.captionGap;
				c->itemText.insertChars(0, photos[i].caption);
				if (hasStyle(m_doc, m_styles.caption))
				{
					ParagraphStyle cs;
					cs.setParent(m_styles.caption);
					c->itemText.applyStyle(0, cs, false);
				}
				c->setXYPos(px, cy);
				c->setWidth(pw);
				c->setColumns(1);
				c->SetRectFrame();
				bool over = false;
				const double ch = fitHeight(c, 3.0, 200.0, &over);
				c->setWrapOffsets(spec.wrap, spec.wrap, spec.wrap, spec.wrap);
				c->setTextFlowMode(wrap ? PageItem::TextFlowUsesBoundingBox : PageItem::TextFlowDisabled);
				captionRects << QRectF(px, cy, pw, ch);
				cy += ch + spec.captionText;
			}
			else
			{
				captionRects << QRectF();
				cy += spec.photoText;
			}
		}
		return cy;
	};

	bool over = false;
	if (spec.photoPos == QLatin1String("topAbove"))
	{
		const double bottom = stack(0);
		t->setXYPos(0, bottom);
		r.h = bottom + fitHeight(t, 5.0, spec.maxH, &over);
	}
	else if (spec.photoPos == QLatin1String("bottom"))
	{
		const double th = fitHeight(t, 5.0, spec.maxH, &over);
		const double bottom = stack(th + spec.photoText);
		r.h = bottom;
	}
	else
	{
		const double bottom = stack(r.headH + spec.wrap);
		const double th = fitHeight(t, qMax(5.0, r.headH), spec.maxH, &over);
		r.h = qMax(th, bottom - spec.captionText);
	}
	r.overflow = over;
	render(r, t, nullptr, photos, captionRects);
	return r;
}

QImage Measurer::picture(const QString& path, const QSize& size)
{
	if (path.isEmpty() || size.isEmpty())
		return QImage();
	const QString key = path + QStringLiteral("@%1x%2").arg(size.width()).arg(size.height());
	auto it = m_pictures.constFind(key);
	if (it != m_pictures.constEnd())
		return it.value();
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const QSize src = reader.size();
	QImage img;
	if (src.isValid() && src.width() > 0 && src.height() > 0)
	{
		// fill the frame, centre crop (like the placed photos)
		const double k = qMax((double) size.width() / src.width(), (double) size.height() / src.height());
		const QSize scaled(qMax(1, qRound(src.width() * k)), qMax(1, qRound(src.height() * k)));
		reader.setScaledSize(scaled);
		const QImage full = reader.read();
		if (!full.isNull())
			img = full.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2, size.width(), size.height());
	}
	m_pictures.insert(key, img);
	return img;
}

void Measurer::render(Result& r, PageItem_TextFrame* text, PageItem_TextFrame* /*caption*/, const QList<PhotoIn>& photos, const QList<QRectF>& captionRects)
{
	const double s = m_scale;
	if (s <= 0)
		return;                    // measure only
	const int wpx = qMax(1, (int) std::ceil(text->width() * s));
	const int hpx = qMax(1, (int) std::ceil(r.h * s));
	QImage img(wpx, hpx, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::white);
	m_doc->DoDrawing = true;
	{
		std::unique_ptr<ScPainter> p(new ScPainter(&img, wpx, hpx, 1.0, 0));
		p->setZoomFactor(s);
		const QRectF cull(-10000, -10000, 20000, 20000);
		text->DrawObj(p.get(), cull);
		for (int i = 0; i < captionRects.size() && i < m_captions.size(); ++i)
			if (!captionRects[i].isNull())
				m_captions[i]->DrawObj(p.get(), cull);
		p->end();
	}
	QPainter qp(&img);
	for (int i = 0; i < r.photos.size(); ++i)
	{
		const QRectF pr(r.photos[i].x() * s, r.photos[i].y() * s, r.photos[i].width() * s, r.photos[i].height() * s);
		const QImage pic = (i < photos.size()) ? picture(photos[i].path, pr.size().toSize()) : QImage();
		if (!pic.isNull())
			qp.drawImage(pr.topLeft(), pic);
		else
		{
			qp.fillRect(pr, QColor(0xC9, 0xCE, 0xD3));
			qp.setPen(QPen(QColor(0x9A, 0xA1, 0xA8), 1));
			qp.drawLine(pr.topLeft(), pr.bottomRight());
			qp.drawLine(pr.topRight(), pr.bottomLeft());
		}
	}
	qp.end();
	r.image = img;
}

void Measurer::renderItems(QImage& img, double scale, const QList<QPointer<PageItem>>& items, const QPointF& origin)
{
	if (QThread::currentThread() != qApp->thread())
	{
		qWarning("SuneerLayout: %s called outside the GUI thread - refused (document items are not thread safe)", Q_FUNC_INFO);
		return;
	}
	if (img.isNull())
		return;
	std::unique_ptr<ScPainter> p(new ScPainter(&img, img.width(), img.height(), 1.0, 0));
	p->setZoomFactor(scale);
	p->translate(-origin.x(), -origin.y());
	const QRectF cull(-100000, -100000, 200000, 200000);
	for (const QPointer<PageItem>& it : items)
	{
		if (!it)
			continue;
		p->save();
		it->DrawObj(p.get(), cull);
		p->restore();
	}
	p->end();
}

// ---------------------------------------------------------------------------
// Job
// ---------------------------------------------------------------------------

Job::Job(const SuneerLayoutModel& model, const PageIn* page, Measurer* measurer, Keep keep)
    : m_model(model), m_page(page), m_meas(measurer), m_keep(keep)
{
	m_plan.modelId = model.id;
}

Measurer::Spec Job::specFor(const StoryIn& s, int cols, bool lead) const
{
	Measurer::Spec sp;
	SuneerSpacing::Values v = m_model.spacing();
	if (!s.userGaps.isEmpty())
	{
		// a placed story with its own gaps ("Adjust gaps"): those inside the story; story to story stays the model's
		SuneerSpacing::Values own = v;
		if (SuneerSpacing::gapsFromString(s.userGaps, own))
			for (SuneerSpacing::Key k : SuneerSpacing::gapKeys())
				if (SuneerSpacing::isStoryGap(k))
					v.v[k] = own.v[k];
	}
	sp.width = cols * m_colW + (cols - 1) * m_gutter;
	sp.cols = cols;
	sp.colGap = v.pt(SuneerSpacing::ColumnGap);
	sp.inset = v.pt(SuneerSpacing::Inset);
	QString head = s.requestedHead.isEmpty() ? m_model.headlineStyleFor(cols, lead) : s.requestedHead;   // "Place with" headline wins
	if (!hasStyle(m_page->doc, head) && m_page->doc)
		head.clear();
	sp.headStyle = head;
	sp.headExtra = v.pt(SuneerSpacing::HeadlineBody);
	sp.kickerExtra = v.pt(SuneerSpacing::KickerHeadline);
	sp.photoPos = s.requestedPos.isEmpty() ? m_model.photoPosition : s.requestedPos;   // "Place with" photo position wins
	sp.photoCols = m_model.photoColumnsFor(cols);
	sp.gridColW = m_colW;
	sp.gutter = m_gutter;
	sp.wrap = v.pt(SuneerSpacing::WrapPad);
	sp.captionGap = v.pt(SuneerSpacing::CaptionGap);
	sp.captionText = v.pt(SuneerSpacing::CaptionText);
	sp.photoText = v.pt(SuneerSpacing::PhotoText);
	sp.maxPhotoH = m_model.photoMaxHeightMm * kMm;
	sp.maxH = qMax(100.0, m_content.height() * 1.5);
	return sp;
}

void Job::prepare()
{
	m_prepared = true;
	const PageIn& pg = *m_page;
	SuneerObitModel om;
	if (m_model.isObituary() && !m_model.obituary.isEmpty())
		om.fromJson(m_model.obituary);
	const Grid grid = gridFor(m_model, pg);
	m_content = grid.content;
	m_cols = grid.cols;
	m_gutter = grid.gutter;
	m_colW = grid.colW;
	QList<double> colX;
	for (int i = 0; i < m_cols; ++i)
		colX << grid.x(i);
	if (!pg.area.isNull())
	{
		// "Apply to selection only": the grid columns inside the selected modules' area, nothing outside it
		QList<double> inside;
		for (double x : colX)
			if (x >= pg.area.left() - 2.0 && x + m_colW <= pg.area.right() + 2.0)
				inside << x;
		if (inside.isEmpty())
			inside << pg.area.left();
		colX = inside;
		m_cols = colX.size();
		m_content = QRectF(colX.first(), pg.area.top(), colX.last() + m_colW - colX.first(), pg.area.height());
		m_model.leftRail = m_model.rightRail = false;
		m_model.leadPosition = QStringLiteral("none");
		m_model.decks = 0;
	}
	m_plan.content = m_content;
	m_plan.colW = m_colW;
	m_plan.colX = colX;
	m_plan.rulePt = m_model.ruleWidthPt;
	m_plan.ruleColor = m_model.ruleColor;
	for (const PageIn::Kept& k : pg.always)
		if (k.obstacle)
			m_blocked << k.rect;

	// which stories are arranged, which stay
	QList<int> arrange;
	for (int i = 0; i < pg.stories.size(); ++i)
	{
		const StoryIn& s = pg.stories[i];
		bool stays = false;
		if (s.placed && s.manual)
			stays = true;
		else if (s.placed && m_keep == Keep::EveryFrame)
			stays = true;
		else if (s.placed && m_keep == Keep::OnlyNew)
			continue;                              // not counted
		if (debugOn())
			fprintf(stderr, "LAYOUT-PREP model=%s story=%d placed=%d manual=%d rigid=%d kind=%s keep=%d -> %s\n", qPrintable(m_model.id), i, s.placed ? 1 : 0, s.manual ? 1 : 0,
			        s.rigid ? 1 : 0, qPrintable(s.kind), (int) m_keep, stays ? "STAYS" : "arranged");
		if (stays)
		{
			m_blocked << s.placedRect;
			m_plan.keptStories << i;
			ModuleOut mo;
			mo.story = i;
			mo.kept = true;
			mo.rect = s.placedRect;
			mo.cols = s.placedCols;
			m_plan.modules << mo;
			continue;
		}
		arrange << i;
	}
	m_plan.total = arrange.size() + m_plan.keptStories.size();

	if (m_model.isObituary())
	{
		const SuneerSpacing::Values v = m_model.spacing();
		for (int i : arrange)
		{
			const StoryIn& s = pg.stories[i];
			bool obit = !s.title.contains('\n') && s.title.split(' ', Qt::SkipEmptyParts).size() <= om.titleMaxWords
			            && s.title.length() <= om.titleMaxChars && s.bodyChars <= om.bodyMaxChars;
			if (s.placed && s.kind == QLatin1String("obit"))
				obit = true;                       // detected on the page as an obit module
			else if (s.placed && (s.kind == QLatin1String("story") || s.kind == QLatin1String("block")))
				obit = false;
			// suneer 2026-10-26: the reserved news columns take ONLY the stories whose workflow column count
			// is 2 (news.columns on a placed module); a placed module with no count stored is a band story
			// when it is a 2-column news module (pages placed before the rule, or made by hand). A count of
			// 3 or more is news in the obit columns; everything else stays an obit or news there.
			const int placedSpan = !s.placed ? 0 : (s.placedCols > 0 ? s.placedCols
			                       : qMax(1, (int) std::lround((s.placedRect.width() + m_gutter) / qMax(1.0, m_colW + m_gutter))));
			const bool band = s.requestedCols > 0 ? s.requestedCols == SuneerObitModel::kBandCount
			                                      : (s.placed && !obit && placedSpan == SuneerObitModel::kBandCount);
			if (band || s.requestedCols > SuneerObitModel::kBandCount)
				obit = false;
			Todo t;
			t.story = i;
			t.lead = false;
			t.obitKind = obit ? 1 : (band ? 2 : 3);
			if (obit)
			{
				t.cols = 1;
				t.photoCols = 1;
				t.spec = specFor(s, 1, false);
				t.spec.obit = true;
				const bool pic = !s.photos.isEmpty();
				t.spec.obitName = pic ? om.styleName : om.styleNameNoPic;
				t.spec.obitPlace = pic ? om.stylePlace : om.stylePlaceNoPic;
				t.spec.obitBody = pic ? om.styleBody : om.styleBodyNoPic;
				t.spec.obitPhotoH = om.photoFrameHeightMm() * kMm;
				t.spec.photoText = v.pt(SuneerSpacing::PhotoText);
				t.spec.obitAlign = om.textAlign;
				t.spec.maxH = m_content.height();
			}
			else
			{
				int cols = qMax(1, om.newsLastCol - om.newsFirstCol + 1);
				if (band && s.requestedCols > 0)
					cols = qBound(1, s.requestedCols, cols);   // the desk's "Columns" in obituary-grid columns, never wider than the news band
				else if (!band)
					cols = qBound(1, s.requestedCols > 0 ? s.requestedCols : (s.placed ? placedSpan : 1), qMax(1, om.obitLastCol - om.obitFirstCol + 1));
				if (s.placed && s.rigid)
					cols = qBound(1, (int) std::ceil((s.placedRect.width() - 2.0 + m_gutter) / (m_colW + m_gutter)), m_cols);
				t.cols = cols;
				t.photoCols = cols;
				t.spec = specFor(s, cols, false);
				t.spec.cols = band || cols == 2 ? qMax(1, om.newsTextColumns) : cols;   // one text column per grid column outside the band
				t.spec.colGap = om.newsColGapMm * kMm;
				t.spec.photoPos = QStringLiteral("top");
				QString head;
				for (const SuneerObitModel::HeadRule& hr : om.newsHeadlines)
					if (s.title.length() <= hr.maxChars)
					{
						head = hr.style;
						break;
					}
				t.spec.headStyle = hasStyle(m_page->doc, head) || !m_page->doc ? head : QString();
			}
			m_todo << t;
		}
		// reading order (column by column, top to bottom; new stories last): the packing fills the
		// shortest column, so the page keeps its order as far as the columns allow
		const double pitch = qMax(1.0, m_colW + m_gutter);
		std::stable_sort(m_todo.begin(), m_todo.end(), [&](const Todo& a, const Todo& b) {
			const StoryIn& sa = m_page->stories[a.story];
			const StoryIn& sb = m_page->stories[b.story];
			if (sa.placed != sb.placed)
				return sa.placed;
			if (!sa.placed)
				return false;
			const int ca = qRound((sa.placedRect.left() - m_content.left()) / pitch), cb = qRound((sb.placedRect.left() - m_content.left()) / pitch);
			if (ca != cb)
				return ca < cb;
			return sa.placedRect.top() < sb.placedRect.top() - 0.5;
		});
		for (const Todo& t : m_todo)
			(t.obitKind == 1 ? m_plan.obits : m_plan.news)++;
		return;
	}

	// lead: the longest story, when the model has one
	if (m_model.leadPosition != QLatin1String("none"))
	{
		int best = -1;
		for (int i : arrange)
			if (!(pg.stories[i].placed && pg.stories[i].rigid) && (best < 0 || pg.stories[i].bodyChars > pg.stories[best].bodyChars))
				best = i;
		m_leadStory = best;
	}
	const int rails = (m_model.leftRail ? 1 : 0) + (m_model.rightRail ? 1 : 0);
	const int mainCols = qMax(1, m_cols - rails);
	for (int i : arrange)
	{
		const StoryIn& s = pg.stories[i];
		const bool lead = (i == m_leadStory) && !s.rigid;
		int cols = s.requestedCols > 0 ? s.requestedCols : m_model.columnsFor(s.bodyChars, lead);   // the desk's "Columns" beats the model's width rule
		cols = qBound(1, cols, cols == 1 ? m_cols : mainCols);
		if (s.placed && s.rigid)          // a block keeps its width: the grid columns it covers
			cols = qBound(1, (int) std::ceil((s.placedRect.width() - 2.0 + m_gutter) / (m_colW + m_gutter)), m_cols);
		if (m_model.obitPacking && s.kind == QLatin1String("obit"))
			cols = 1;                     // an obit module is one obituary column, whatever its measured width
		Todo t;
		t.story = i;
		t.lead = lead;
		t.cols = cols;
		t.photoCols = m_model.photoColumnsFor(cols);
		t.obitKind = 0;
		t.spec = specFor(s, cols, lead);
		m_todo << t;
	}
	if (m_model.obitPacking)
	{
		// obituary page: the lead first, then the modules in their reading order (column by column,
		// top to bottom; new stories after the placed ones) - the packing fills the shortest column
		const double pitch = qMax(1.0, m_colW + m_gutter);
		auto colOf = [&](const Todo& t) {
			const StoryIn& s = m_page->stories[t.story];
			return s.placed ? qRound((s.placedRect.left() - m_content.left()) / pitch) : 1000;
		};
		std::stable_sort(m_todo.begin(), m_todo.end(), [&](const Todo& a, const Todo& b) {
			if (a.lead != b.lead)
				return a.lead;
			const StoryIn& sa = m_page->stories[a.story];
			const StoryIn& sb = m_page->stories[b.story];
			if (sa.placed != sb.placed)
				return sa.placed;
			const int ca = colOf(a), cb = colOf(b);
			if (ca != cb)
				return ca < cb;
			return sa.placedRect.top() < sb.placedRect.top() - 0.5;
		});
		return;
	}
	// big modules first, so the packing has room for them
	std::stable_sort(m_todo.begin(), m_todo.end(), [this](const Todo& a, const Todo& b) {
		if (a.lead != b.lead)
			return a.lead;
		if (a.cols != b.cols)
			return a.cols > b.cols;
		return m_page->stories[a.story].bodyChars > m_page->stories[b.story].bodyChars;
	});
}

bool Job::step()
{
	if (m_finished)
		return false;
	if (!m_prepared)
	{
		prepare();
		return true;
	}
	if (m_done < m_todo.size())
	{
		const Todo& t = m_todo[m_done];
		m_meas->measure(m_page->stories[t.story], t.spec);
		++m_done;
		return true;
	}
	if (m_model.isObituary())
		packObituary();
	else
		pack();
	// the modules that stay: their real items, rendered as they are
	for (ModuleOut& mo : m_plan.modules)
	{
		if (!mo.kept || mo.story < 0)
			continue;
		const StoryIn& s = m_page->stories[mo.story];
		const double sc = m_meas->scale();
		if (sc <= 0)
			continue;
		QImage img(qMax(1, (int) std::ceil(s.placedRect.width() * sc)), qMax(1, (int) std::ceil(s.placedRect.height() * sc)), QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::white);
		QPointF origin = s.placedRect.topLeft();
		if (m_page->doc && m_page->pageIndex >= 0 && m_page->pageIndex < m_page->doc->Pages->count())
		{
			const ScPage* p = m_page->doc->Pages->at(m_page->pageIndex);
			origin += QPointF(p->xOffset(), p->yOffset());
		}
		Measurer::renderItems(img, sc, s.items, origin);
		mo.image = img;
	}
	// free space: the content area not covered, as full-width height
	{
		const double cell = 2.0;
		QList<QRectF> used = m_blocked;
		for (const ModuleOut& mo : m_plan.modules)
			if (!mo.kept)
				used << mo.rect;
		double freeArea = 0;
		const SuneerSpacing::Values v = m_model.spacing();
		const double gapV = v.pt(SuneerSpacing::ModuleGapV);
		for (int c = 0; c < m_cols; ++c)
		{
			const double x = m_plan.colX[c] + m_colW / 2.0;
			int run = 0;
			for (double y = m_content.top(); y < m_content.bottom(); y += cell)
			{
				bool covered = false;
				for (const QRectF& r : used)
					if (r.adjusted(-0.1, -gapV - 0.1, 0.1, gapV + 0.1).contains(QPointF(x, y)))
					{
						covered = true;
						break;
					}
				if (!covered)
					++run;
			}
			freeArea += run * cell * m_colW;
		}
		m_plan.freePt = freeArea / qMax(1.0, m_content.width());
	}
	m_finished = true;
	return false;
}

namespace {
struct Packer
{
	QRectF content;
	QList<double> colX;
	double colW { 0 }, gutter { 0 }, gapV { 0 };
	QList<QRectF> occupied;

	QRectF rectAt(int c0, int k, double y, double h) const
	{
		return QRectF(colX[c0], y, k * colW + (k - 1) * gutter, h);
	}
	bool free(const QRectF& r) const
	{
		if (r.bottom() > content.bottom() + 0.5 || r.top() < content.top() - 0.5)
			return false;
		const QRectF in = r.adjusted(0.5, 0.5, -0.5, -0.5);
		for (const QRectF& o : occupied)
			if (in.intersects(o))
				return false;
		return true;
	}
	//! Lowest top in [yMin, yMax - h] where the module fits, -1 when nowhere.
	double lowest(int c0, int k, double h, double yMin, double yMax) const
	{
		if (c0 < 0 || c0 + k > colX.size())
			return -1;
		QList<double> cand { yMin };
		const QRectF span = rectAt(c0, k, yMin, 1);
		for (const QRectF& o : occupied)
			if (o.right() > span.left() + 0.5 && o.left() < span.right() - 0.5 && o.bottom() + gapV >= yMin)
				cand << o.bottom() + gapV;
		std::sort(cand.begin(), cand.end());
		for (double y : cand)
		{
			if (y + h > yMax + 0.5)
				break;
			if (free(rectAt(c0, k, y, h)))
				return y;
		}
		return -1;
	}
};
} // namespace

void Job::pack()
{
	const SuneerSpacing::Values v = m_model.spacing();
	Packer pk;
	pk.content = m_content;
	pk.colX = m_plan.colX;
	pk.colW = m_colW;
	pk.gutter = m_gutter;
	pk.gapV = v.pt(SuneerSpacing::ModuleGapV);
	pk.occupied = m_blocked;
	const int mainFirst = m_model.leftRail ? 1 : 0;
	const int mainLast = qMax(mainFirst, m_model.rightRail ? m_cols - 2 : m_cols - 1);
	double leadBottom = m_content.top();
	bool leadPlaced = false;

	auto place = [&](const Todo& t, int c0, double y) {
		const Measurer::Result& r = m_meas->measure(m_page->stories[t.story], t.spec);
		ModuleOut mo;
		mo.story = t.story;
		mo.cols = t.cols;
		mo.photoCols = t.photoCols;
		mo.lead = t.lead;
		mo.rect = pk.rectAt(c0, t.cols, y, r.h);
		if (r.w > 0)
			mo.rect.setWidth(r.w);           // a block keeps its own width
		mo.image = r.image;
		mo.photos = r.photos;
		pk.occupied << mo.rect;
		m_plan.modules << mo;
		return mo.rect;
	};

	for (const Todo& t : m_todo)
	{
		const Measurer::Result& r = m_meas->measure(m_page->stories[t.story], t.spec);
		const double h = r.h;
		const int k = t.cols;
		if (r.overflow || h > m_content.height())
		{
			m_plan.notFit << t.story;
			continue;
		}
		if (t.lead)
		{
			const int span = mainLast - mainFirst + 1;
			int c0 = mainFirst;
			if (m_model.leadPosition == QLatin1String("top-right"))
				c0 = mainLast - k + 1;
			else if (m_model.leadPosition == QLatin1String("top-centre"))
				c0 = mainFirst + qMax(0, (span - k) / 2);
			c0 = qBound(0, c0, m_cols - k);
			double y = pk.lowest(c0, k, h, m_content.top(), m_content.bottom());
			if (y < 0)
			{
				for (int c = mainFirst; c + k - 1 <= mainLast && y < 0; ++c)
					if ((y = pk.lowest(c, k, h, m_content.top(), m_content.bottom())) >= 0)
						c0 = c;
			}
			if (y < 0)
			{
				m_plan.notFit << t.story;
				continue;
			}
			leadBottom = place(t, c0, y).bottom();
			leadPlaced = true;
			continue;
		}
		// single-column briefs go to the rails first
		if (k == 1 && (m_model.leftRail || m_model.rightRail))
		{
			int bestC = -1;
			double bestY = -1;
			QList<int> rails;
			if (m_model.leftRail)
				rails << 0;
			if (m_model.rightRail)
				rails << m_cols - 1;
			for (int c : rails)
			{
				const double y = pk.lowest(c, 1, h, m_content.top(), m_content.bottom());
				if (y >= 0 && (bestY < 0 || y < bestY - 0.5))
				{
					bestY = y;
					bestC = c;
				}
			}
			if (bestC >= 0)
			{
				place(t, bestC, bestY);
				continue;
			}
		}
		int bestC = -1;
		double bestY = -1;
		auto search = [&](double yMin, double yMax) {
			for (int c = mainFirst; c + k - 1 <= mainLast; ++c)
			{
				const double y = pk.lowest(c, k, h, yMin, yMax);
				if (y >= 0 && (bestY < 0 || y < bestY - 0.5))
				{
					bestY = y;
					bestC = c;
				}
			}
		};
		if (m_model.decks > 0)
		{
			// horizontal decks under the lead: the modules of a deck share its top edge
			const double top0 = leadPlaced ? leadBottom + pk.gapV : m_content.top();
			const double bandH = (m_content.bottom() - top0 + pk.gapV) / m_model.decks;
			for (int d = 0; d < m_model.decks && bestC < 0; ++d)
			{
				const double bt = top0 + d * bandH;
				const double bb = (h > bandH - pk.gapV) ? m_content.bottom() : bt + bandH - pk.gapV;
				search(bt, bb);
			}
		}
		else
			search(m_content.top(), m_content.bottom());
		if (bestC < 0)
		{
			m_plan.notFit << t.story;
			continue;
		}
		place(t, bestC, bestY);
	}
}

void Job::packObituary()
{
	SuneerObitModel om;
	if (!m_model.obituary.isEmpty())
		om.fromJson(m_model.obituary);
	const SuneerSpacing::Values v = m_model.spacing();
	Packer pk;
	pk.content = m_content;
	pk.colX = m_plan.colX;
	pk.colW = m_colW;
	pk.gutter = m_gutter;
	pk.gapV = v.pt(SuneerSpacing::ModuleGapV);
	pk.occupied = m_blocked;
	// suneer 2026-10-26: the plan's columns (all of them, or those inside "Apply to selection only") of the
	// obit area and of the reserved news columns; a story is packed in its own area only - no spill from
	// one into the other (a 2-column story without room in the band and an obit without room in the obit
	// columns are both listed as not fitting)
	const Grid pageGrid = gridFor(m_model, *m_page);
	const double pagePitch = qMax(1.0, pageGrid.colW + pageGrid.gutter);
	QList<int> areaIdx, bandIdx;
	for (int i = 0; i < m_plan.colX.size(); ++i)
	{
		const int pc = qRound((m_plan.colX[i] - pageGrid.content.left()) / pagePitch) + 1;   // 1-based grid column of the page
		if (pc >= om.newsFirstCol && pc <= om.newsLastCol)
			bandIdx << i;                  // the band wins where a model overlaps the two ranges
		else if (pc >= om.obitFirstCol && pc <= om.obitLastCol)
			areaIdx << i;
	}
	m_plan.bandFirst = om.newsFirstCol;
	m_plan.bandLast = om.newsLastCol;
	m_plan.areaFirst = om.obitFirstCol;
	m_plan.areaLast = om.obitLastCol;
	for (const Todo& t : m_todo)
	{
		const Measurer::Result& r = m_meas->measure(m_page->stories[t.story], t.spec);
		const bool inBand = t.obitKind == 2;
		const QList<int>& allowed = inBand ? bandIdx : areaIdx;
		int k = t.obitKind == 1 ? 1 : qMax(1, t.cols);
		if (r.w > 0)
		{
			// a block moved as it is keeps its width: it takes every column it covers (it overlapped the next one)
			k = qBound(1, (int) std::ceil((r.w - 1.0 + m_gutter) / (m_colW + m_gutter)), m_cols);
		}
		int bestC = -1;
		double bestY = -1;
		for (int j = 0; j + k - 1 < allowed.size(); ++j)
		{
			const int c = allowed[j];
			if (allowed[j + k - 1] != c + k - 1)
				continue;                      // not neighbouring columns
			const double y = pk.lowest(c, k, r.h, m_content.top(), m_content.bottom());
			if (y >= 0 && (bestY < 0 || y < bestY - 0.5))
			{
				bestY = y;
				bestC = c;
			}
		}
		if (bestC < 0 || r.overflow)
		{
			m_plan.notFit << t.story;
			if (inBand)
				m_plan.notFitBand << t.story;
			continue;
		}
		if (inBand)
			++m_plan.bandPlaced;
		ModuleOut mo;
		mo.story = t.story;
		mo.cols = k;
		mo.rect = pk.rectAt(bestC, k, bestY, r.h);
		if (r.w > 0)
			mo.rect.setWidth(r.w);
		mo.image = r.image;
		mo.photos = r.photos;
		pk.occupied << mo.rect;
		m_plan.modules << mo;
	}
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

Runner::Runner(QObject* parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setInterval(0);
	connect(m_timer, &QTimer::timeout, this, &Runner::tick);
}

Runner::~Runner()
{
	qDeleteAll(m_jobs);
}

void Runner::add(Job* job)
{
	m_jobs << job;
	if (!m_timer->isActive())
		m_timer->start();
}

void Runner::clear()
{
	m_timer->stop();
	qDeleteAll(m_jobs);
	m_jobs.clear();
	m_next = 0;
}

bool Runner::busy() const
{
	for (const Job* j : m_jobs)
		if (!j->finished())
			return true;
	return false;
}

void Runner::tick()
{
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < 25)
	{
		Job* job = nullptr;
		for (int i = 0; i < m_jobs.size(); ++i)
		{
			Job* j = m_jobs[(m_next + i) % m_jobs.size()];
			if (!j->finished())
			{
				job = j;
				m_next = (m_next + i + 1) % m_jobs.size();
				break;
			}
		}
		if (!job)
		{
			m_timer->stop();
			emit idle();
			return;
		}
		const bool debug = debugOn();
		QElapsedTimer one;
		one.start();
		job->step();
		if (debug)
			fprintf(stderr, "LAYOUT step model=%s done=%d/%d ms=%lld\n", qPrintable(job->model().id), job->done(), job->total(), (long long) one.elapsed());
		emit progress(job);
		if (job->finished())
			emit finished(job);
	}
}

bool debugOn()
{
	static const bool on = qEnvironmentVariableIsSet("SUNEER_LAYOUT_DEBUG");
	return on;
}

void debugPlan(const char* who, const SuneerLayoutModel& m, const PageIn& page, const PlanOut& plan)
{
	if (!debugOn())
		return;
	const SuneerSpacing::Values v = m.spacing();
	const Grid g = gridFor(m, page);
	fprintf(stderr, "LAYOUT-PLAN %s model=%s file=%s engine=%s obitPacking=%d\n", who, qPrintable(m.id), qPrintable(SuneerLayoutModels::filePath(m.id)), qPrintable(m.engine), m.obitPacking ? 1 : 0);
	fprintf(stderr, "LAYOUT-PLAN %s   grid cols=%d (model %d, fromGuides=%d, page guides %d) colW=%.1fmm gutter=%.1fmm content=%.1f,%.1f %.1fx%.1fmm\n", who,
	        g.cols, m.columns, m.gridFromGuides ? 1 : 0, page.guideCols, g.colW / kMm, g.gutter / kMm,
	        g.content.x() / kMm, g.content.y() / kMm, g.content.width() / kMm, g.content.height() / kMm);
	fprintf(stderr, "LAYOUT-PLAN %s   gapV=%.1fmm gapH=%.1fmm photoPos=%s fixedPhotoCols=%d photoRules=%d photoMaxH=%.1fmm fixedCols=%d widthRules=%d lead=%s/%d\n", who,
	        v.pt(SuneerSpacing::ModuleGapV) / kMm, m.gutterMm, qPrintable(m.photoPosition), m.fixedPhotoColumns, (int) m.photoRules.size(), m.photoMaxHeightMm,
	        m.fixedColumns, (int) m.widthRules.size(), qPrintable(m.leadPosition), m.leadColumns);
	for (const ModuleOut& mo : plan.modules)
	{
		const StoryIn* s = (mo.story >= 0 && mo.story < page.stories.size()) ? &page.stories[mo.story] : nullptr;
		fprintf(stderr, "LAYOUT-PLAN %s   module story=%d %s cols=%d photoCols=%d rect=%.1f,%.1f %.1fx%.1fmm was=%.1f,%.1f %.1fx%.1fmm rigid=%d kind=%s image=%s \"%s\"\n", who,
		        mo.story, mo.kept ? "KEPT" : "NEW", mo.cols, mo.photoCols,
		        mo.rect.x() / kMm, mo.rect.y() / kMm, mo.rect.width() / kMm, mo.rect.height() / kMm,
		        s ? s->placedRect.x() / kMm : -1.0, s ? s->placedRect.y() / kMm : -1.0, s ? s->placedRect.width() / kMm : 0.0, s ? s->placedRect.height() / kMm : 0.0,
		        s && s->rigid ? 1 : 0, s ? qPrintable(s->kind) : "-",
		        mo.image.isNull() ? "none" : (mo.kept ? "page-items" : "re-laid"), s ? qPrintable(s->title.left(30).simplified()) : "");
	}
	QStringList nf;
	for (int i : plan.notFit)
		nf << QString::number(i);
	fprintf(stderr, "LAYOUT-PLAN %s   notFit=[%s] fit=\"%s\"\n", who, qPrintable(nf.join(',')), qPrintable(plan.fitLine()));
}

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

QImage renderKept(const PageIn& page, double scale, Keep keep)
{
	if (QThread::currentThread() != qApp->thread())
	{
		qWarning("SuneerLayout: %s called outside the GUI thread - refused (document items are not thread safe)", Q_FUNC_INFO);
		return QImage();
	}
	QImage img(qMax(1, (int) std::ceil(page.pageW * scale)), qMax(1, (int) std::ceil(page.pageH * scale)), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	if (!page.doc || page.pageIndex < 0 || page.pageIndex >= page.doc->Pages->count())
		return img;
	Q_UNUSED(keep);
	const ScPage* p = page.doc->Pages->at(page.pageIndex);
	QList<QPointer<PageItem>> normal;
	for (const PageIn::Kept& k : page.always)
	{
		if (k.master)
			Measurer::renderItems(img, scale, k.items, k.masterOffset);
		else
			normal << k.items;
	}
	Measurer::renderItems(img, scale, normal, QPointF(p->xOffset(), p->yOffset()));
	return img;
}

QImage renderPlan(const PlanOut& plan, const PageIn& page, int widthPx, double moduleScale, const QImage& keptLayer)
{
	Q_UNUSED(moduleScale);
	const double s = widthPx / qMax(1.0, page.pageW);
	const int hpx = qMax(1, qRound(page.pageH * s));
	QImage img(widthPx, hpx, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::white);
	QPainter p(&img);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setRenderHint(QPainter::Antialiasing);
	// grid columns, faint
	p.setPen(Qt::NoPen);
	for (double x : plan.colX)
		p.fillRect(QRectF(x * s, plan.content.top() * s, plan.colW * s, plan.content.height() * s), QColor(0xF3, 0xF6, 0xFA));
	if (!keptLayer.isNull())
		p.drawImage(QRectF(0, 0, widthPx, hpx), keptLayer);
	for (const ModuleOut& m : plan.modules)
	{
		const QRectF r(m.rect.x() * s, m.rect.y() * s, m.rect.width() * s, m.rect.height() * s);
		if (!m.image.isNull())
			p.drawImage(r, m.image);
		else
			p.fillRect(r, QColor(0xE6, 0xE9, 0xEC));
		if (m.kept)
		{
			p.setPen(QPen(QColor(0x60, 0x7D, 0x8B), 1, Qt::DashLine));
			p.setBrush(Qt::NoBrush);
			p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
		}
	}
	// separator rules between stacked modules
	if (plan.rulePt > 0)
	{
		p.setPen(QPen(QColor(0x20, 0x20, 0x20), qMax(0.6, plan.rulePt * s)));
		for (const ModuleOut& a : plan.modules)
			for (const ModuleOut& b : plan.modules)
			{
				if (&a == &b || b.rect.top() < a.rect.bottom() - 0.5 || b.rect.top() - a.rect.bottom() > 30)
					continue;
				const double l = qMax(a.rect.left(), b.rect.left()), rr = qMin(a.rect.right(), b.rect.right());
				if (rr - l < 5)
					continue;
				const double y = (a.rect.bottom() + b.rect.top()) / 2.0 * s;
				p.drawLine(QPointF(l * s, y), QPointF(rr * s, y));
			}
	}
	p.setPen(QPen(QColor(0xB0, 0xB8, 0xC0), 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(QRectF(0.5, 0.5, widthPx - 1, hpx - 1));
	return img;
}

// ---------------------------------------------------------------------------
// The page as it is
// ---------------------------------------------------------------------------

namespace {
QString itemAttr(const PageItem* item, const QString& name)
{
	const QList<ObjectAttribute> l = item->getObjectAttributes(name);
	return l.isEmpty() ? QString() : l.first().value;
}
} // namespace

namespace {
void setItemAttr(PageItem* item, const QString& name, const QString& value)
{
	if (!item)
		return;
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = attrs.size() - 1; i >= 0; --i)
		if (attrs[i].name == name)
			attrs.removeAt(i);
	if (!value.isEmpty())
	{
		ObjectAttribute a;
		a.name = name;
		a.type = QStringLiteral("string");
		a.value = value;
		attrs.append(a);
	}
	item->setObjectAttributes(&attrs);
}

enum class TKind { Body, Head, Caption, Other };

struct TextInfo
{
	TKind kind { TKind::Other };
	int chars { 0 };
	int bodyChars { 0 };
	bool headline { false };
	bool advert { false };
	bool obitStyle { false };
	double maxSize { 0 };
	QString firstText;
	int firstChars { 0 };
};

QString paraKind(const QString& style, const StyleNames& st)
{
	static const QRegularExpression head(QStringLiteral("^\\s*\\d+(\\.\\d+)?\\s*[MB]\\b|^Headings|[Hh]eadline"));
	if (style.isEmpty())
		return QStringLiteral("body");
	if (style == st.kicker || style.contains(QLatin1String("Kicker"), Qt::CaseInsensitive))
		return QStringLiteral("kicker");
	if (style == st.title || head.match(style).hasMatch())
		return QStringLiteral("headline");
	if (style == st.byline || style.contains(QLatin1String("Byline"), Qt::CaseInsensitive))
		return QStringLiteral("byline");
	if (style == st.dateline || style.contains(QLatin1String("Dateline"), Qt::CaseInsensitive))
		return QStringLiteral("dateline");
	if (style == st.caption || style.contains(QLatin1String("Caption"), Qt::CaseInsensitive) || style.contains(QLatin1String("Cutline"), Qt::CaseInsensitive))
		return QStringLiteral("caption");
	if (style.contains(QLatin1String("Advt"), Qt::CaseInsensitive))
		return QStringLiteral("advert");
	return QStringLiteral("body");
}

TextInfo textInfo(PageItem* t, const StyleNames& st)
{
	TextInfo ti;
	const StoryText& text = t->itemText;
	static const QRegularExpression obitRe(QStringLiteral("obit ?(name|place|body)|^9[1-6] "), QRegularExpression::CaseInsensitiveOption);
	int pos = 0;
	int paraNo = 0;
	bool allCaption = true;
	bool any = false;
	while (pos < text.length())
	{
		int end = pos;
		while (end < text.length() && text.text(end) != SpecialChars::PARSEP)
			++end;
		const int n = end - pos;
		if (n > 0)
		{
			const QString style = text.paragraphStyle(pos).parent();
			QString k = paraKind(style, st);
			double size = 0;
			for (int i = pos; i < end && i < pos + 40; ++i)
				size = qMax(size, text.charStyle(i).fontSize() / 10.0);
			if (k == QLatin1String("body") && size >= 13.0 && n < 120)
				k = QStringLiteral("headline");         // a locally formatted headline
			ti.maxSize = qMax(ti.maxSize, size);
			ti.chars += n;
			if (k == QLatin1String("body"))
				ti.bodyChars += n;
			if (k == QLatin1String("headline") || k == QLatin1String("kicker"))
				ti.headline = true;
			if (k == QLatin1String("advert"))
				ti.advert = true;
			if (k != QLatin1String("caption"))
				allCaption = false;
			if (paraNo < 2 && obitRe.match(style).hasMatch())
				ti.obitStyle = true;
			if (paraNo == 0)
			{
				ti.firstText = text.text(pos, qMin(n, 60));
				ti.firstChars = n;
			}
			any = true;
			++paraNo;
		}
		pos = end + 1;
	}
	if (t->itemName().contains(QLatin1String("advt"), Qt::CaseInsensitive))
		ti.advert = true;
	if (any && allCaption)
		ti.kind = TKind::Caption;
	else if (ti.bodyChars > 60)
		ti.kind = TKind::Body;
	else if (ti.headline && ti.chars < 220)
		ti.kind = TKind::Head;
	return ti;
}

double hOverlap(const QRectF& a, const QRectF& b) { return qMin(a.right(), b.right()) - qMax(a.left(), b.left()); }

QRectF unionOf(const QList<QPointer<PageItem>>& items, const QPointF& off)
{
	QRectF r;
	for (const QPointer<PageItem>& it : items)
		if (it)
			r = r.isNull() ? it->getBoundingRect().translated(-off) : r.united(it->getBoundingRect().translated(-off));
	return r;
}
} // namespace

QString newModuleId()
{
	static int counter = 0;
	return QStringLiteral("m%1-%2").arg(QDateTime::currentMSecsSinceEpoch() % 100000000).arg(++counter);
}

QList<Module> detectModules(ScribusDoc* doc, int pageIndex, const StyleNames& st)
{
	QList<Module> out;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return out;
	const ScPage* page = doc->Pages->at(pageIndex);
	const QPointF off(page->xOffset(), page->yOffset());
	const QRectF pageRect(off, QSizeF(page->width(), page->height()));
	const double mm = kMm;
	const double contentTop = off.y() + page->Margins.top(), contentBottom = off.y() + page->height() - page->Margins.bottom();

	QList<PageItem*> tops;
	for (PageItem* it : doc->DocItems)
	{
		if (!it || it->isGroupChild() || !it->getBoundingRect().intersects(pageRect))
			continue;
		if (itemAttr(it, QStringLiteral("news.rule")) == QLatin1String("1") || itemAttr(it, QStringLiteral("news.parked")) == QLatin1String("1"))
			continue;                              // News Browser rules are drawn again; parked modules are not on the page
		if (it->isTextFrame() && it->itemText.length() == 0 && it->fillColor() == CommonStrings::None && it->lineColor() == CommonStrings::None)
			continue;                              // an empty invisible frame prints nothing
		tops << it;
	}
	QSet<PageItem*> used;
	auto finish = [&](Module& m) {
		m.rect = unionOf(m.items, off);
		if (m.title.isEmpty() && m.body && m.body->isTextFrame())
			m.title = textInfo(m.body, st).firstText.simplified();
		if (m.title.isEmpty() && !m.heads.isEmpty() && m.heads.first())
			m.title = textInfo(m.heads.first(), st).firstText.simplified();
		if (m.title.isEmpty() && !m.items.isEmpty() && m.items.first())
			m.title = m.items.first()->itemName();
		// an inline object / mark is a control character in the text: not part of the title (it showed as a box)
		m.title.removeIf([](QChar c) { return c.category() == QChar::Other_Control || c == QChar::ObjectReplacementCharacter; });
		m.title = m.title.trimmed().left(40);
		for (const QPointer<PageItem>& p : m.items)
			if (p)
				used.insert(p.data());
		out << m;
	};

	// 1. News Browser stories and modules checked earlier (attributes)
	QHash<QString, int> byId;
	for (PageItem* it : tops)
	{
		const QString story = itemAttr(it, QStringLiteral("news.story"));
		const QString lm = itemAttr(it, QStringLiteral("layout.module"));
		const QString id = !story.isEmpty() ? QStringLiteral("news:") + story : (!lm.isEmpty() ? lm : QString());
		if (id.isEmpty())
			continue;
		int idx = byId.value(id, -1);
		if (idx < 0)
		{
			Module m;
			m.id = !story.isEmpty() ? story : lm;
			m.kind = !story.isEmpty() ? QStringLiteral("news") : QStringLiteral("story");
			m.remembered = true;
			if (!story.isEmpty())
				m.title = story;
			idx = out.size();
			byId.insert(id, idx);
			out << m;
		}
		Module& m = out[idx];
		m.items << QPointer<PageItem>(it);
		used.insert(it);
		const QString fixedWhy = itemAttr(it, QStringLiteral("layout.fixed"));
		if (!fixedWhy.isEmpty())
		{
			m.fixed = true;
			m.reason = fixedWhy == QLatin1String("1") ? QStringLiteral("user") : fixedWhy;
		}
		if (itemAttr(it, QStringLiteral("layout.rigid")) == QLatin1String("1") || it->isGroup())
			m.rigid = m.kind != QLatin1String("news") || it->isGroup();
		if (!itemAttr(it, QStringLiteral("layout.kind")).isEmpty())
			m.kind = itemAttr(it, QStringLiteral("layout.kind"));
		// the Obituary engine's own modules are moved as blocks: photo, background, tone, hyphenation stay
		const QString newsModel = itemAttr(it, QStringLiteral("news.model"));
		if (newsModel == QLatin1String("obituary"))
		{
			m.kind = QStringLiteral("obit");
			m.rigid = true;
		}
		else if (newsModel == QLatin1String("obituary-news"))
		{
			m.kind = QStringLiteral("story");
			m.rigid = true;
		}
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts = it->getAllChildren();
		for (PageItem* c : parts)
		{
			const QString role = !story.isEmpty() ? itemAttr(c, QStringLiteral("news.role")) : itemAttr(c, QStringLiteral("layout.role"));
			if ((role == QLatin1String("text") || role == QLatin1String("body")) && c->isTextFrame() && !m.body)
				m.body = c;
			else if (role == QLatin1String("head") && c->isTextFrame())
				m.heads << QPointer<PageItem>(c);
			else if (role == QLatin1String("photo") && c->isImageFrame())
			{
				m.photos << QPointer<PageItem>(c);
				m.captions << QPointer<PageItem>();
			}
		}
		for (PageItem* c : parts)
		{
			const QString role = !story.isEmpty() ? itemAttr(c, QStringLiteral("news.role")) : itemAttr(c, QStringLiteral("layout.role"));
			if (role != QLatin1String("caption") || !c->isTextFrame())
				continue;
			// the caption of the photo it sits under (News Browser: by news.index)
			int best = -1;
			for (int i = 0; i < m.photos.size(); ++i)
			{
				if (!story.isEmpty() && itemAttr(m.photos[i], QStringLiteral("news.index")) == itemAttr(c, QStringLiteral("news.index")))
					best = i;
				else if (story.isEmpty() && m.photos[i] && qAbs(c->yPos() - (m.photos[i]->yPos() + m.photos[i]->height())) < 6 * mm)
					best = i;
			}
			if (best >= 0)
				m.captions[best] = c;
		}
	}
	for (Module& m : out)
	{
		m.rect = unionOf(m.items, off);
		if (!m.body && !m.fixed && m.kind != QLatin1String("news"))
			m.rigid = true;
		if (m.title.isEmpty() && m.body)
			m.title = textInfo(m.body, st).firstText.simplified().left(40);
	}

	// 2. Everything else: fixed items, groups, then stories assembled from frames
	QList<PageItem*> texts, images;
	QHash<PageItem*, TextInfo> info;
	for (PageItem* it : tops)
	{
		if (used.contains(it))
			continue;
		const QRectF r = it->getBoundingRect();
		Module m;
		m.id = newModuleId();
		m.items << QPointer<PageItem>(it);
		if (it->locked() || it->locked())
		{
			m.fixed = true; m.kind = QStringLiteral("fixed"); m.reason = QStringLiteral("locked");
		}
		else if (it->isLine() || it->isPolyLine() || qMin(r.width(), r.height()) < 1.5 * mm)
		{
			m.fixed = true; m.kind = QStringLiteral("fixed"); m.reason = QStringLiteral("rule");
		}
		else if (r.bottom() <= contentTop + 1.0 || r.top() >= contentBottom - 1.0)
		{
			m.fixed = true; m.kind = QStringLiteral("fixed"); m.reason = QStringLiteral("masthead/folio");
		}
		else if (it->isTextFrame() && (it->nextInChain() || it->prevInChain()))
		{
			m.fixed = true; m.kind = QStringLiteral("fixed"); m.reason = QStringLiteral("chained");
		}
		else if (it->isGroup())
		{
			m.rigid = true;
			m.kind = QStringLiteral("group");
			int imgs = 0, txt = 0;
			for (PageItem* c : it->getAllChildren())
			{
				if (c->isImageFrame()) ++imgs;
				if (c->isTextFrame() && c->itemText.length() > 0) { ++txt; if (!m.body) m.body = c; }
			}
			if (imgs >= 1 && txt >= 1 && r.width() < 48 * mm)
				m.kind = QStringLiteral("obit");
		}
		else if (it->isTextFrame() && !it->isTable())
		{
			texts << it;
			info.insert(it, textInfo(it, st));
			continue;
		}
		else if (it->isImageFrame())
		{
			images << it;
			continue;
		}
		else
		{
			m.fixed = true; m.kind = QStringLiteral("fixed"); m.reason = QStringLiteral("other");
		}
		finish(m);
	}
	auto rectOf = [](PageItem* it) { return it->getBoundingRect(); };
	// obituary modules: a narrow text frame right under a photo of the same width (or in the obit styles)
	QSet<PageItem*> usedT, usedI;
	for (PageItem* t : texts)
	{
		const TextInfo& ti = info[t];
		const QRectF tr = rectOf(t);
		if (tr.width() < 18 * mm || tr.width() > 45 * mm || ti.kind == TKind::Caption)
			continue;
		PageItem* photo = nullptr;
		for (PageItem* im : images)
		{
			if (usedI.contains(im))
				continue;
			const QRectF ir = rectOf(im);
			const double gap = tr.top() - ir.bottom();
			if (qAbs(ir.left() - tr.left()) < 2.5 * mm && qAbs(ir.width() - tr.width()) < 3 * mm && gap >= -1 * mm && gap < 8 * mm)
			{
				photo = im;
				break;
			}
		}
		const bool byGeometry = photo && ti.chars < 700 && ti.firstChars < 45 && !ti.headline
		                        && rectOf(photo).height() / qMax(1.0, rectOf(photo).width()) >= 0.75 && rectOf(photo).height() / qMax(1.0, rectOf(photo).width()) <= 1.5;
		if (!(ti.obitStyle || byGeometry))
			continue;
		Module m;
		m.id = newModuleId();
		m.kind = QStringLiteral("obit");
		m.rigid = true;
		m.body = t;
		m.items << QPointer<PageItem>(t);
		usedT.insert(t);
		if (photo)
		{
			m.items.prepend(QPointer<PageItem>(photo));
			m.photos << QPointer<PageItem>(photo);
			m.captions << QPointer<PageItem>();
			usedI.insert(photo);
		}
		finish(m);
	}
	// captions: under a photo, all caption styled or short small text
	QHash<PageItem*, PageItem*> captionOf;    // image -> caption
	for (PageItem* t : texts)
	{
		if (usedT.contains(t))
			continue;
		const TextInfo& ti = info[t];
		const bool small = ti.kind == TKind::Caption || (ti.chars <= 220 && ti.maxSize <= 10.5 && ti.kind != TKind::Body);
		if (!small)
			continue;
		const QRectF tr = rectOf(t);
		for (PageItem* im : images)
		{
			if (usedI.contains(im) || captionOf.contains(im))
				continue;
			const QRectF ir = rectOf(im);
			const double gap = tr.top() - ir.bottom();
			if (gap >= -1 * mm && gap < 4 * mm && hOverlap(tr, ir) > 0.6 * qMin(tr.width(), ir.width()))
			{
				captionOf.insert(im, t);
				break;
			}
		}
	}
	QSet<PageItem*> captionFrames;
	for (auto it = captionOf.constBegin(); it != captionOf.constEnd(); ++it)
		captionFrames.insert(it.value());
	// stories: a body frame + its head frames above + the photos on / right above it + their captions
	QList<PageItem*> bodies, heads;
	for (PageItem* t : texts)
	{
		if (usedT.contains(t) || captionFrames.contains(t))
			continue;
		if (info[t].kind == TKind::Body && !info[t].advert)
			bodies << t;
		else if (info[t].kind == TKind::Head)
			heads << t;
	}
	std::sort(bodies.begin(), bodies.end(), [&](PageItem* a, PageItem* b) {
		return rectOf(a).top() < rectOf(b).top() - 1 || (qAbs(rectOf(a).top() - rectOf(b).top()) <= 1 && rectOf(a).left() < rectOf(b).left());
	});
	for (PageItem* b : bodies)
	{
		if (usedT.contains(b))
			continue;
		const QRectF br = rectOf(b);
		Module m;
		m.id = newModuleId();
		m.kind = QStringLiteral("story");
		m.body = b;
		usedT.insert(b);
		QList<PageItem*> hs;
		for (PageItem* h : heads)
		{
			if (usedT.contains(h))
				continue;
			const QRectF hr = rectOf(h);
			const double d = br.top() - hr.bottom();
			if (d >= -4 * mm && d < 26 * mm && hOverlap(hr, br) > 0.5 * qMin(hr.width(), br.width()))
				hs << h;
		}
		for (bool changed = true; changed; )
		{
			changed = false;
			for (PageItem* h : heads)
			{
				if (usedT.contains(h) || hs.contains(h))
					continue;
				for (PageItem* hh : hs)
				{
					const double d = rectOf(hh).top() - rectOf(h).bottom();
					if (d >= -2 * mm && d < 10 * mm && hOverlap(rectOf(h), rectOf(hh)) > 0.5 * qMin(rectOf(h).width(), rectOf(hh).width()))
					{
						hs << h;
						changed = true;
						break;
					}
				}
			}
		}
		std::sort(hs.begin(), hs.end(), [&](PageItem* a, PageItem* c) { return rectOf(a).top() < rectOf(c).top(); });
		// more body frames side by side under the same headline: one story (moved as a block)
		QList<PageItem*> extra;
		for (PageItem* o : bodies)
		{
			if (o == b || usedT.contains(o) || hs.isEmpty())
				continue;
			if (qAbs(rectOf(o).top() - br.top()) < 6 * mm)
				for (PageItem* h : hs)
					if (hOverlap(rectOf(h), rectOf(o)) > 0.5 * rectOf(o).width())
					{
						extra << o;
						break;
					}
		}
		for (PageItem* h : hs)
		{
			usedT.insert(h);
			m.heads << QPointer<PageItem>(h);
			m.items << QPointer<PageItem>(h);
		}
		m.items << QPointer<PageItem>(b);
		for (PageItem* o : extra)
		{
			usedT.insert(o);
			m.items << QPointer<PageItem>(o);
			m.rigid = true;
		}
		QRectF modRect = br;
		for (PageItem* h : hs)
			modRect = modRect.united(rectOf(h));
		for (PageItem* im : images)
		{
			if (usedI.contains(im))
				continue;
			const QRectF ir = rectOf(im);
			const QRectF inter = ir.intersected(br);
			const bool inside = inter.width() * inter.height() > 0.5 * ir.width() * ir.height();
			const double gapAbove = modRect.top() - ir.bottom();
			// a photo (with its caption) right above the headline; it may be wider than the story under it
			const bool above = gapAbove >= 0 && gapAbove < 14 * mm && hOverlap(ir, modRect) > 0.5 * qMin(ir.width(), modRect.width());
			if (!(inside || above))
				continue;
			usedI.insert(im);
			m.items << QPointer<PageItem>(im);
			m.photos << QPointer<PageItem>(im);
			PageItem* cap = captionOf.value(im, nullptr);
			m.captions << QPointer<PageItem>(cap);
			if (cap)
			{
				usedT.insert(cap);
				m.items << QPointer<PageItem>(cap);
			}
			if (above)
				m.rigid = true;          // photo stacked above the headline: kept as the editor made it
		}
		if (qAbs(b->rotation()) > 0.01)
			m.rigid = true;
		finish(m);
	}
	// whatever is left stays where it is
	for (PageItem* t : texts)
	{
		if (usedT.contains(t))
			continue;
		Module m;
		m.id = newModuleId();
		m.kind = QStringLiteral("fixed");
		m.fixed = true;
		m.reason = info[t].advert ? QStringLiteral("advert") : QStringLiteral("text");
		m.items << QPointer<PageItem>(t);
		finish(m);
	}
	for (PageItem* im : images)
	{
		if (usedI.contains(im))
			continue;
		Module m;
		m.id = newModuleId();
		m.kind = QStringLiteral("fixed");
		m.fixed = true;
		m.reason = QStringLiteral("picture");
		m.items << QPointer<PageItem>(im);
		PageItem* cap = captionOf.value(im, nullptr);
		if (cap && !usedT.contains(cap))
		{
			m.items << QPointer<PageItem>(cap);
			usedT.insert(cap);
		}
		finish(m);
	}
	return out;
}

void rememberModules(const QList<Module>& modules)
{
	for (const Module& m : modules)
	{
		if (m.kind == QLatin1String("news"))
		{
			// a News Browser story keeps its news.* tags; only "keep fixed" is added
			for (const QPointer<PageItem>& it : m.items)
				if (it)
					setItemAttr(it, QStringLiteral("layout.fixed"), m.fixed && m.reason == QLatin1String("user") ? QStringLiteral("user") : QString());
			continue;
		}
		for (const QPointer<PageItem>& it : m.items)
		{
			if (!it)
				continue;
			QString role = QStringLiteral("part");
			if (it == m.body) role = QStringLiteral("body");
			else if (m.heads.contains(it)) role = QStringLiteral("head");
			else if (m.photos.contains(it)) role = QStringLiteral("photo");
			else if (m.captions.contains(it)) role = QStringLiteral("caption");
			setItemAttr(it, QStringLiteral("layout.module"), m.id);
			setItemAttr(it, QStringLiteral("layout.role"), role);
			setItemAttr(it, QStringLiteral("layout.kind"), m.kind);
			setItemAttr(it, QStringLiteral("layout.fixed"), m.fixed ? (m.reason.isEmpty() ? QStringLiteral("user") : m.reason) : QString());
			setItemAttr(it, QStringLiteral("layout.rigid"), m.rigid ? QStringLiteral("1") : QString());
			if (it->isGroup())
				for (PageItem* c : it->getAllChildren())
				{
					QString cr;
					if (c == m.body) cr = QStringLiteral("body");
					else if (m.photos.contains(c)) cr = QStringLiteral("photo");
					setItemAttr(c, QStringLiteral("layout.role"), cr);
				}
		}
	}
}

void forgetModules(ScribusDoc* doc, int pageIndex)
{
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return;
	const ScPage* page = doc->Pages->at(pageIndex);
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	for (PageItem* it : doc->DocItems)
	{
		if (!it || !it->getBoundingRect().intersects(pageRect))
			continue;
		QList<PageItem*> parts { it };
		if (it->isGroup())
			parts << it->getAllChildren();
		for (PageItem* c : parts)
			for (const char* n : { "layout.module", "layout.role", "layout.kind", "layout.fixed", "layout.rigid" })
				setItemAttr(c, QString::fromLatin1(n), QString());
	}
}

namespace {
//! Gallery scratch copies: the real page's master items, handed to collectPage() as fixed areas.
QHash<const ScribusDoc*, QList<PageIn::Kept>>& scratchMasters()
{
	static QHash<const ScribusDoc*, QList<PageIn::Kept>> m;
	return m;
}
} // namespace

QList<PageIn::Kept> masterKept(ScribusDoc* doc, int pageIndex)
{
	QList<PageIn::Kept> out;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return out;
	const ScPage* page = doc->Pages->at(pageIndex);
	if (!doc->MasterNames.contains(page->masterPageName()))
		return out;
	const ScPage* mp = doc->MasterPages.at(doc->MasterNames[page->masterPageName()]);
	for (PageItem* it : doc->MasterItems)
	{
		if (!it || it->isGroupChild() || it->OnMasterPage != page->masterPageName() || !mp)
			continue;
		if (it->isTextFrame() && it->itemText.length() == 0)
			continue;
		PageIn::Kept k;
		k.rect = it->getBoundingRect().translated(-mp->xOffset(), -mp->yOffset());
		k.items << QPointer<PageItem>(it);
		k.master = true;
		k.masterOffset = QPointF(mp->xOffset(), mp->yOffset());
		out << k;
	}
	return out;
}

PageIn collectPage(ScribusDoc* doc, int pageIndex, const QList<StoryIn>& newStories, const StyleNames& styles, const QString& pageKey,
                   const QList<PageItem*>* selection)
{
	PageIn p;
	p.doc = doc;
	p.styles = styles;
	p.pageKey = pageKey;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
	{
		p = samplePage(nullptr);
		p.stories = newStories;
		p.styles = styles;
		p.pageKey = pageKey;
		p.doc = doc;
		return p;
	}
	const ScPage* page = doc->Pages->at(pageIndex);
	p.pageIndex = pageIndex;
	p.pageW = page->width();
	p.pageH = page->height();
	p.mTop = page->Margins.top();
	p.mBottom = page->Margins.bottom();
	p.mLeft = page->Margins.left();
	p.mRight = page->Margins.right();
	p.guideCols = page->guides.verticalAutoCount() > 0 ? page->guides.verticalAutoCount() + 1 : 0;
	const QPointF off(page->xOffset(), page->yOffset());

	p.modules = detectModules(doc, pageIndex, styles);
	QSet<PageItem*> sel;
	if (selection)
		for (PageItem* it : *selection)
		{
			sel.insert(it);
			if (it && it->isGroupChild())
				for (PageItem* g = it->Parent; g; g = g->Parent)
					sel.insert(g);
		}
	QList<StoryIn> placed;
	for (const Module& m : p.modules)
	{
		bool selected = false;
		for (const QPointer<PageItem>& it : m.items)
			if (it && sel.contains(it.data()))
				selected = true;
		if (m.fixed || (selection && !selected))
		{
			PageIn::Kept k;
			k.rect = m.rect;
			k.items = m.items;
			// a loose (unlocked) rule belongs to the old arrangement: it stays, but the new one may cover it
			if (m.fixed && m.reason == QLatin1String("rule"))
			{
				k.obstacle = false;
				++p.looseRules;
			}
			p.always << k;
			continue;
		}
		if (!m.remembered)
			++p.detected;
		StoryIn s;
		s.key = m.id;
		s.moduleId = m.id;
		s.title = m.title;
		s.placed = true;
		s.selected = selected;
		s.kind = m.kind;
		s.rigid = m.rigid || !m.body;
		s.placedRect = m.rect;
		s.items = m.items;
		s.heads = m.heads;
		s.photoItems = m.photos;
		s.captionItems = m.captions;
		s.textItem = m.body;
		if (m.body && m.body->isTextFrame())
			s.bodyChars = m.body->itemText.length();
		for (const QPointer<PageItem>& it : m.items)
			if (it && itemAttr(it, QStringLiteral("news.manual")) == QLatin1String("1"))
				s.manual = true;
		s.placedCols = m.body ? itemAttr(m.body, QStringLiteral("news.cols")).toInt() : 0;
		s.requestedCols = m.body ? itemAttr(m.body, QStringLiteral("news.columns")).toInt() : 0;   // the desk's width stays through Re-layout / Apply
		if (m.body)
		{
			s.requestedHead = itemAttr(m.body, QStringLiteral("news.headStyle"));
			if (itemAttr(m.body, QStringLiteral("news.photoPosUser")) == QLatin1String("1"))
				s.requestedPos = itemAttr(m.body, QStringLiteral("news.photoPos"));
		}
		if (m.body && itemAttr(m.body, QStringLiteral("layout.gapsUser")) == QLatin1String("1"))
			s.userGaps = itemAttr(m.body, QStringLiteral("layout.gaps"));
		for (int i = 0; i < m.photos.size(); ++i)
		{
			PageItem* c = m.photos[i];
			if (!c)
				continue;
			PhotoIn ph;
			ph.path = c->Pfile;
			if (c->pixm.width() > 0 && c->pixm.height() > 0)
				ph.aspect = (double) c->pixm.height() / c->pixm.width();
			else if (c->width() > 0)
				ph.aspect = c->height() / c->width();
			if (i < m.captions.size() && m.captions[i])
				ph.caption = m.captions[i]->itemText.plainText();
			s.photos << ph;
		}
		if (selected && !s.placedRect.isNull())
			p.area = p.area.isNull() ? s.placedRect : p.area.united(s.placedRect);
		if (debugOn())
		{
			QStringList ph;
			for (int i = 0; i < s.photos.size(); ++i)
			{
				const PageItem* c = i < m.photos.size() ? m.photos[i].data() : nullptr;
				ph << QStringLiteral("%1 (pixm %2x%3, frame %4x%5, caption %6)").arg(s.photos[i].aspect, 0, 'f', 3).arg(c ? c->pixm.width() : -1).arg(c ? c->pixm.height() : -1)
				          .arg(c ? c->width() : 0, 0, 'f', 1).arg(c ? c->height() : 0, 0, 'f', 1).arg(s.photos[i].caption.length());
			}
			fprintf(stderr, "LAYOUT-STORY doc=%p %d \"%s\" body=%d chars (frame %s, cols %d) heads=%d photos=[%s]\n", (void*) doc, (int) placed.size(), qPrintable(s.title.left(20)),
			        s.bodyChars, m.body ? qPrintable(QStringLiteral("%1x%2").arg(m.body->width(), 0, 'f', 1).arg(m.body->height(), 0, 'f', 1)) : "-",
			        m.body ? m.body->columns() : 0, (int) s.heads.size(), qPrintable(ph.join(QStringLiteral("; "))));
		}
		placed << s;
	}
	// master page items (masthead, folio) stay where they are; a Gallery scratch copy gets the real page's
	p.always << masterKept(doc, pageIndex);
	if (scratchMasters().contains(doc))
		p.always << scratchMasters().value(doc);
	Q_UNUSED(off);
	p.stories = placed + (selection ? QList<StoryIn>() : newStories);
	return p;
}

QImage renderPageNow(const PageIn& page, double scale)
{
	if (QThread::currentThread() != qApp->thread())
	{
		qWarning("SuneerLayout: %s called outside the GUI thread - refused (document items are not thread safe)", Q_FUNC_INFO);
		return QImage();
	}
	QImage img(qMax(1, (int) std::ceil(page.pageW * scale)), qMax(1, (int) std::ceil(page.pageH * scale)), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::white);
	if (!page.doc || page.pageIndex < 0 || page.pageIndex >= page.doc->Pages->count())
		return img;
	const ScPage* p = page.doc->Pages->at(page.pageIndex);
	QList<QPointer<PageItem>> normal;
	for (const PageIn::Kept& k : page.always)
	{
		if (k.master)
			Measurer::renderItems(img, scale, k.items, k.masterOffset);
		else
			normal << k.items;
	}
	for (const StoryIn& s : page.stories)
		if (s.placed)
			normal << s.items;
	Measurer::renderItems(img, scale, normal, QPointF(p->xOffset(), p->yOffset()));
	return img;
}

// ---------------------------------------------------------------------------
// Checks before an arrangement
// ---------------------------------------------------------------------------

QStringList checkModel(const SuneerLayoutModel& m, double pageW, double pageH, double mTop, double mBottom, double mLeft, double mRight, int guideCols)
{
	QStringList out;
	auto bad = [](double v) { return !std::isfinite(v); };
	if (m.id.isEmpty() || !SuneerLayoutModels::exists(m.id))
		out << QObject::tr("the model \"%1\" is not in the layout models folder any more").arg(m.id);
	if (m.columns < 1 || m.columns > 40)
		out << QObject::tr("columns = %1 (1 to 40)").arg(m.columns);
	for (double v : { m.gutterMm, m.gapVMm, m.columnWidthMm, m.photoMaxHeightMm, m.marginTopMm, m.marginBottomMm, m.marginLeftMm, m.marginRightMm,
	                  m.headlineBodyMm, m.photoTextMm, m.wrapPadMm, m.captionGapMm, m.captionTextMm, m.insetMm, m.columnGapMm })
		if (bad(v) || v < 0)
		{
			out << QObject::tr("a size is negative or not a number (%1)").arg(v);
			break;
		}
	if (m.gutterMm > 60 || m.gapVMm > 60)
		out << QObject::tr("gap between modules %1 / %2 mm (at most 60)").arg(m.gutterMm).arg(m.gapVMm);
	if (m.photoMaxHeightMm <= 0)
		out << QObject::tr("photo maximum height is 0");
	if (pageW <= 0 || pageH <= 0)
		out << QObject::tr("no page size");
	else
	{
		const Grid g = gridFor(m, pageW, pageH, mTop, mBottom, mLeft, mRight, guideCols);
		if (g.cols < 1 || bad(g.colW) || g.colW < 5.0)
			out << QObject::tr("the grid does not fit the page: %1 column(s) of %2 mm").arg(g.cols).arg(g.colW / kMm, 0, 'f', 1);
		if (g.content.left() < -0.5 || g.content.top() < -0.5 || g.content.right() > pageW + 0.5 || g.content.bottom() > pageH + 0.5
		    || g.content.width() < 50.0 || g.content.height() < 50.0 || bad(g.content.width()) || bad(g.content.height()))
			out << QObject::tr("the content area is %1 x %2 mm (margins larger than the page?)").arg(g.content.width() / kMm, 0, 'f', 0).arg(g.content.height() / kMm, 0, 'f', 0);
	}
	return out;
}

QStringList checkArrange(const SuneerLayoutModel& model, const PageIn& page)
{
	QStringList out = checkModel(model, page.pageW, page.pageH, page.mTop, page.mBottom, page.mLeft, page.mRight, page.guideCols);
	for (const StoryIn& s : page.stories)
	{
		if (!s.placed)
			continue;
		const QString name = s.title.left(30);
		bool gone = s.items.isEmpty();
		for (const QPointer<PageItem>& it : s.items)
			if (!it || (page.doc && it->doc() != page.doc))
				gone = true;
		if (gone)
		{
			out << QObject::tr("module \"%1\": a frame of it was deleted or belongs to another document - detect the modules again (Layout > Page Modules...)").arg(name);
			continue;
		}
		const QRectF& r = s.placedRect;
		if (!std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width()) || !std::isfinite(r.height()) || r.width() <= 0 || r.height() <= 0
		    || r.width() > 20 * qMax(1.0, page.pageW) || r.height() > 20 * qMax(1.0, page.pageH))
			out << QObject::tr("module \"%1\": size %2 x %3 pt is not usable").arg(name).arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1);
		if (s.textItem && (!s.textItem->isTextFrame() || s.textItem->doc() != page.doc))
			out << QObject::tr("module \"%1\": its text frame is not a text frame of this document").arg(name);
	}
	return out;
}

// ---------------------------------------------------------------------------
// Scratch copy of a page (Layout Gallery cards)
// ---------------------------------------------------------------------------

QList<PageItem*> pageTopItems(ScribusDoc* doc, int pageIndex)
{
	QList<PageItem*> out;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return out;
	const ScPage* page = doc->Pages->at(pageIndex);
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	for (PageItem* it : doc->DocItems)
		if (it && !it->isGroupChild() && it->getBoundingRect().intersects(pageRect))
			out << it;
	return out;
}

ScratchPage::~ScratchPage()
{
	UndoBlocker blocker;
	scratchMasters().remove(doc);
	// a palette must never keep the copy (see makeScratchPage): give it the real document back while the copy still exists
	ScribusMainWindow* mw = ScCore->primaryMainWindow();
	if (doc && mw && mw->guidePalette && mw->guidePalette->doc() == doc)
	{
		qWarning("SuneerLayout: the Guide Manager pointed at a Gallery scratch copy - reset");
		mw->guidePalette->setDoc(mw->HaveDoc ? mw->doc : nullptr);
	}
	delete doc;              // deletes its items
}

std::unique_ptr<ScratchPage> makeScratchPage(ScribusDoc* src, int pageIndex)
{
	QElapsedTimer clock;
	clock.start();
	auto sp = std::make_unique<ScratchPage>();
	if (QThread::currentThread() != qApp->thread())
	{
		sp->error = QObject::tr("not on the GUI thread");
		return sp;
	}
	if (!src || pageIndex < 0 || pageIndex >= src->Pages->count())
	{
		sp->error = QObject::tr("no page");
		return sp;
	}
	UndoBlocker blocker;
	const ScPage* page = src->Pages->at(pageIndex);
	ScribusDoc* d = new ScribusDoc();
	sp->doc = d;
	d->setup(0, 1, 1, 1, 1, QStringLiteral("Custom"), QStringLiteral("Custom"));
	// what the text layout reads from the document
	d->typographicPrefs() = src->typographicPrefs();
	d->guidesPrefs() = src->guidesPrefs();
	d->itemToolPrefs() = src->itemToolPrefs();
	d->setHyphAutomatic(src->hyphAutomatic());
	d->setHyphAutoCheck(false);
	// a document without a GUI gets no hyphenator (ScribusDoc::createHyphenator); the placement code calls it
	if (!d->docHyphenator)
		d->docHyphenator = new Hyphenator(nullptr, d);      // deleted with the document
	if (d->docHyphenator && src->docHyphenator)
	{
		d->docHyphenator->slotNewSettings(src->hyphAutomatic(), false);
		d->docHyphenator->ignoredWords = src->docHyphenator->ignoredWords;
		d->docHyphenator->specialWords = src->docHyphenator->specialWords;
	}
	d->setPage(page->width(), page->height(), page->Margins.top(), page->Margins.left(), page->Margins.right(), page->Margins.bottom(), 0, 0, false, 0);
	d->addPage(0);
	ScPage* dp = d->Pages->at(0);
	dp->Margins = page->Margins;
	dp->initialMargins = page->initialMargins;
	page->guides.copy(&dp->guides);
	// addPage() made the page current while the copy had no main window. ScribusDoc::setCurrentPage() after
	// setGUI() hands the document to the Guide Manager palette (and the page palette); the copy is deleted a
	// moment later and the palette kept a dangling pointer, then wrote into the freed page (storePageValues):
	// heap corruption, and Scribus crashed later somewhere else (2026-10-16 21:16, ASAN 2026-10-17).
	d->setGUI(false, ScCore->primaryMainWindow(), nullptr);
	suneerSetDocUsesCharClusters(d, suneerDocUsesCharClusters(src));   // a page from an older Scribus breaks lines the old way
	d->PageColors = src->PageColors;
	d->redefineCharStyles(src->charStyles(), false);
	d->redefineStyles(src->paragraphStyles(), false);
	d->docLineStyles = src->docLineStyles;
	d->Layers = src->Layers;
	{
		QStringList fonts;
		for (int i = 0; i < src->paragraphStyles().count(); ++i)
			fonts << src->paragraphStyles()[i].charStyle().font().scName();
		for (int i = 0; i < src->charStyles().count(); ++i)
			fonts << src->charStyles()[i].font().scName();
		fonts << src->itemToolPrefs().textFont;
		// fonts set on the text itself (not through a style) too: without them a caption broke into two lines
		// in the copy and one on the page (2026-10-17, Page-15_ObitKRN, Normal)
		fonts << src->usedFonts().keys();
		fonts.removeDuplicates();
		for (const QString& f : fonts)
			if (!f.isEmpty() && PrefsManager::instance().appPrefs.fontPrefs.AvailFonts.contains(f))
				d->AddFont(f);
	}
	sp->masters = masterKept(src, pageIndex);
	scratchMasters().insert(d, sp->masters);
	sp->source = pageTopItems(src, pageIndex);

	// one paste per layer, in document order (the items keep their order and so their stacking)
	QList<int> layerOrder;
	QHash<int, QList<PageItem*>> byLayer;
	for (PageItem* it : sp->source)
	{
		if (!byLayer.contains(it->m_layerID))
			layerOrder << it->m_layerID;
		byLayer[it->m_layerID] << it;
	}
	QHash<PageItem*, PageItem*> copyOf;
	const QPointF srcOff(page->xOffset(), page->yOffset()), dstOff(dp->xOffset(), dp->yOffset());
	for (int layer : layerOrder)
	{
		const QList<PageItem*>& batch = byLayer[layer];
		Selection sel(nullptr, false);
		for (PageItem* it : batch)
			sel.addItem(it);
		const QString xml = ScriXmlDoc::writeElem(src, &sel);
		const int n0 = d->Items->count();
		ScriXmlDoc reader;
		reader.readElemToLayer(xml, d, dstOff.x(), dstOff.y(), false, true, layer);
		const int n1 = d->Items->count();
		if (n1 - n0 != batch.size())
		{
			sp->error = QObject::tr("the copy of layer %1 has %2 item(s) instead of %3").arg(layer).arg(n1 - n0).arg(batch.size());
			return sp;
		}
		Selection moved(nullptr, false);
		for (int i = 0; i < batch.size(); ++i)
		{
			PageItem* c = d->Items->at(n0 + i);
			copyOf.insert(batch[i], c);
			moved.addItem(c);
		}
		// the paste puts the batch at its own corner: move it to where it is on the real page
		PageItem* first = d->Items->at(n0);
		const double dx = batch.first()->xPos() - srcOff.x() + dstOff.x() - first->xPos();
		const double dy = batch.first()->yPos() - srcOff.y() + dstOff.y() - first->yPos();
		if (qAbs(dx) > 0.001 || qAbs(dy) > 0.001)
			d->moveGroup(dx, dy, &moved);
	}
	for (PageItem* it : sp->source)
		sp->copies << copyOf.value(it, nullptr);
	// The paste does not carry every text exactly: the last paragraph and its style, a frame's default
	// style (measured: a story 1133 -> 1132 characters, two headline frames 64 pt -> 114 / 288 pt).
	// Every text frame that is not part of a chain gets the real frame's text as it is.
	int textsFixed = 0;
	std::function<void(PageItem*, PageItem*)> syncText = [&](PageItem* a, PageItem* b) {
		if (!a || !b)
			return;
		if (a->isGroup() && b->isGroup())
		{
			for (int i = 0; i < qMin(a->groupItemList.size(), b->groupItemList.size()); ++i)
				syncText(a->groupItemList[i], b->groupItemList[i]);
			return;
		}
		if (!a->isTextFrame() || !b->isTextFrame() || a->nextInChain() || a->prevInChain() || a->asTextFrame()->isTableItem)
			return;
		if (b->itemText.length() != a->itemText.length() || b->itemText.plainText() != a->itemText.plainText()
		    || !b->itemText.defaultStyle().equiv(a->itemText.defaultStyle())
		    || !b->itemText.paragraphStyle(b->itemText.length()).equiv(a->itemText.paragraphStyle(a->itemText.length())))
			++textsFixed;
		b->itemText.clear();
		b->itemText.setDefaultStyle(a->itemText.defaultStyle());
		b->itemText.insert(0, a->itemText);
		b->invalidateLayout();
	};
	for (int i = 0; i < sp->source.size(); ++i)
		syncText(sp->source[i], sp->copies[i]);
	sp->ms = clock.elapsed();
	if (debugOn())
		fprintf(stderr, "LAYOUT-SCRATCH page %d: %d item(s) copied in %d layer batch(es), %d master area(s), %d text(s) differed after the paste (set from the page), %lld ms%s%s\n",
		        pageIndex + 1, (int) sp->copies.size(), (int) layerOrder.size(), (int) sp->masters.size(), textsFixed, (long long) sp->ms,
		        sp->error.isEmpty() ? "" : " ERROR ", qPrintable(sp->error));
	return sp;
}

QString pageStateKey(ScribusDoc* doc, int pageIndex)
{
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return QString();
	QCryptographicHash h(QCryptographicHash::Sha1);
	auto add = [&](const QString& v) { h.addData(v.toUtf8()); h.addData("\x1f", 1); };
	auto num = [&](double v) { add(QString::number(v, 'f', 2)); };
	const ScPage* page = doc->Pages->at(pageIndex);
	add(QString::number((quintptr) doc, 16));
	add(QString::number(pageIndex));
	num(page->width()); num(page->height());
	num(page->Margins.top()); num(page->Margins.bottom()); num(page->Margins.left()); num(page->Margins.right());
	add(QString::number(page->guides.verticalAutoCount()));
	add(page->masterPageName());
	std::function<void(PageItem*)> item = [&](PageItem* it) {
		add(QString::number((int) it->itemType()));
		num(it->xPos()); num(it->yPos()); num(it->width()); num(it->height()); num(it->rotation());
		add(QString::number(it->m_layerID));
		add(QString::number((int) it->textFlowMode()));
		add(QString::number(it->locked() ? 1 : 0));
		add(it->Pfile);
		num(it->imageXScale()); num(it->imageYScale()); num(it->imageXOffset()); num(it->imageYOffset());
		for (const ObjectAttribute& a : *it->getObjectAttributes())
			add(a.name + QLatin1Char('=') + a.value);
		if (it->isTextFrame())
		{
			add(QString::number(it->itemText.length()));
			add(QString::number(qHash(it->itemText.plainText())));
			add(QString::number(it->columns()));
			num(it->columnGap());
			add(QString::number(it->nextInChain() ? 1 : 0));
			add(QString::number(it->prevInChain() ? 1 : 0));
		}
		if (it->isGroup())
			for (PageItem* c : it->groupItemList)
				item(c);
	};
	for (PageItem* it : pageTopItems(doc, pageIndex))
		item(it);
	add(QString::number(masterKept(doc, pageIndex).size()));
	for (int i = 0; i < doc->paragraphStyles().count(); ++i)
	{
		const ParagraphStyle& ps = doc->paragraphStyles()[i];
		add(ps.name()); add(ps.parent());
		add(ps.charStyle().font().scName());
		num(ps.charStyle().fontSize()); num(ps.lineSpacing()); num(ps.gapBefore()); num(ps.gapAfter());
		add(QString::number((int) ps.lineSpacingMode()));
		add(QString::number((int) ps.alignment()));
	}
	return QString::fromLatin1(h.result().toHex());
}

QStringList resultSignature(ScribusDoc* doc, int pageIndex, const QList<PageItem*>& items)
{
	QStringList out;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return out;
	const ScPage* page = doc->Pages->at(pageIndex);
	const QRectF pageRect(page->xOffset(), page->yOffset(), page->width(), page->height());
	auto half = [](double v) { return QString::number(qRound(v * 2.0) / 2.0, 'f', 1); };
	auto rectOf = [&](PageItem* it) {
		QString r = half(it->xPos() - pageRect.x()) + QLatin1Char(',') + half(it->yPos() - pageRect.y()) + QLatin1Char(' ')
		            + half(it->width()) + QLatin1Char('x') + half(it->height());
		if (it->isTextFrame())
			r += QStringLiteral(" c%1 db%2").arg(it->columns()).arg(half(it->textToFrameDistBottom()));
		return r;
	};
	QSet<PageItem*> listed;
	for (int i = 0; i < items.size(); ++i)
	{
		PageItem* it = items[i];
		listed.insert(it);
		if (!it || !doc->Items->contains(it))
			out << QStringLiteral("%1:gone").arg(i);
		else if (!it->getBoundingRect().intersects(pageRect))
			out << QStringLiteral("%1:off").arg(i);
		else
			out << QStringLiteral("%1:").arg(i) + rectOf(it);
	}
	QStringList fresh;
	for (PageItem* it : pageTopItems(doc, pageIndex))
		if (!listed.contains(it))
			fresh << QStringLiteral("new:") + rectOf(it);
	fresh.sort();
	return out + fresh;
}

int signatureDiff(const QStringList& a, const QStringList& b)
{
	int diff = qAbs(a.size() - b.size());
	for (int i = 0; i < qMin(a.size(), b.size()); ++i)
		if (a[i] != b[i])
			++diff;
	return diff;
}

QImage renderDocPage(ScribusDoc* doc, int pageIndex, const QList<PageIn::Kept>& masters, int widthPx)
{
	if (QThread::currentThread() != qApp->thread())
	{
		qWarning("SuneerLayout: %s called outside the GUI thread - refused (document items are not thread safe)", Q_FUNC_INFO);
		return QImage();
	}
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count())
		return QImage();
	const ScPage* page = doc->Pages->at(pageIndex);
	const double scale = widthPx / qMax(1.0, page->width());
	QImage img(qMax(1, widthPx), qMax(1, qRound(page->height() * scale)), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::white);
	for (const PageIn::Kept& k : masters)
		Measurer::renderItems(img, scale, k.items, k.master ? k.masterOffset : QPointF(page->xOffset(), page->yOffset()));
	QList<PageItem*> items = pageTopItems(doc, pageIndex);
	std::stable_sort(items.begin(), items.end(), [doc](PageItem* a, PageItem* b) { return doc->layerLevelFromID(a->m_layerID) < doc->layerLevelFromID(b->m_layerID); });
	QList<QPointer<PageItem>> draw;
	for (PageItem* it : items)
		if (doc->layerVisible(it->m_layerID))
			draw << QPointer<PageItem>(it);
	Measurer::renderItems(img, scale, draw, QPointF(page->xOffset(), page->yOffset()));
	QPainter p(&img);
	p.setPen(QPen(QColor(0xB0, 0xB8, 0xC0), 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(QRectF(0.5, 0.5, img.width() - 1, img.height() - 1));
	return img;
}

double freeSpacePt(ScribusDoc* doc, int pageIndex, const QList<PageIn::Kept>& masters, const QRectF& content, const QList<double>& colX, double colW, double gapV, int* overflow)
{
	if (overflow)
		*overflow = 0;
	if (!doc || pageIndex < 0 || pageIndex >= doc->Pages->count() || content.isEmpty())
		return 0;
	const ScPage* page = doc->Pages->at(pageIndex);
	const QPointF off(page->xOffset(), page->yOffset());
	QList<QRectF> used;
	for (const PageIn::Kept& k : masters)
		used << k.rect;
	std::function<void(PageItem*)> countOverflow = [&](PageItem* it) {
		if (it->isGroup())
			for (PageItem* c : it->groupItemList)
				countOverflow(c);
		else if (it->isTextFrame() && !it->nextInChain() && it->frameOverflows() && overflow)
			++*overflow;
	};
	for (PageItem* it : pageTopItems(doc, pageIndex))
	{
		countOverflow(it);
		const QRectF r = it->getBoundingRect().translated(-off);
		if (r.width() > 2.0 && r.height() > 2.0)    // rules do not fill a column
			used << r;
	}
	const double cell = 2.0;
	double freeArea = 0;
	for (double x0 : colX)
	{
		const double x = x0 + colW / 2.0;
		int run = 0;
		for (double y = content.top(); y < content.bottom(); y += cell)
		{
			bool covered = false;
			for (const QRectF& r : used)
				if (r.adjusted(-0.1, -gapV - 0.1, 0.1, gapV + 0.1).contains(QPointF(x, y)))
				{
					covered = true;
					break;
				}
			if (!covered)
				++run;
		}
		freeArea += run * cell * colW;
	}
	return freeArea / qMax(1.0, content.width());
}

// ---------------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------------

QList<StoryIn> sampleStories()
{
	static const QStringList sentences {
		QStringLiteral("സംസ്ഥാനത്ത് അടുത്ത മൂന്നു ദിവസം ശക്തമായ മഴയ്ക്ക് സാധ്യതയുണ്ടെന്ന് കാലാവസ്ഥാ നിരീക്ഷണ കേന്ദ്രം അറിയിച്ചു."),
		QStringLiteral("തീരപ്രദേശങ്ങളിൽ താമസിക്കുന്നവർ ജാഗ്രത പാലിക്കണമെന്നും മത്സ്യത്തൊഴിലാളികൾ കടലിൽ പോകരുതെന്നും നിർദേശമുണ്ട്."),
		QStringLiteral("ജില്ലാ ഭരണകൂടം അടിയന്തര യോഗം ചേർന്ന് സ്ഥിതിഗതികൾ വിലയിരുത്തി."),
		QStringLiteral("പഞ്ചായത്ത് തലത്തിൽ ദുരിതാശ്വാസ ക്യാമ്പുകൾ തുറക്കാനുള്ള ഒരുക്കങ്ങൾ പൂർത്തിയായതായി അധികൃതർ പറഞ്ഞു."),
		QStringLiteral("റോഡുകളുടെ അറ്റകുറ്റപ്പണി അടുത്ത മാസം ആരംഭിക്കുമെന്ന് മന്ത്രി നിയമസഭയെ അറിയിച്ചു."),
		QStringLiteral("കർഷകർക്ക് നഷ്ടപരിഹാരം നൽകുന്നതിനായി പ്രത്യേക പദ്ധതി തയ്യാറാക്കും."),
		QStringLiteral("നഗരത്തിലെ ഗതാഗതക്കുരുക്ക് പരിഹരിക്കാൻ പുതിയ മേൽപ്പാലം നിർമിക്കാൻ തീരുമാനമായി."),
		QStringLiteral("വിദ്യാലയങ്ങൾക്ക് വെള്ളിയാഴ്ച അവധി പ്രഖ്യാപിച്ചിട്ടുണ്ട്.") };
	struct S { const char16_t* title; const char16_t* place; int chars; int photos; double aspect; };
	static const S samples[] = {
		{ u"ശക്തമായ മഴ തുടരും; ജാഗ്രതാ നിർദേശം", u"തിരുവനന്തപുരം", 2300, 1, 0.66 },
		{ u"മേൽപ്പാലം നിർമാണം അടുത്ത മാസം", u"കൊച്ചി", 1500, 1, 0.75 },
		{ u"കർഷകർക്ക് പ്രത്യേക പാക്കേജ്", u"കോഴിക്കോട്", 1150, 0, 0 },
		{ u"ദുരിതാശ്വാസ ക്യാമ്പുകൾ തുറന്നു", u"കണ്ണൂർ", 950, 1, 0.7 },
		{ u"റോഡ് നവീകരണത്തിന് 40 കോടി", u"തൃശൂർ", 800, 0, 0 },
		{ u"സ്കൂളുകൾക്ക് നാളെ അവധി", u"പാലക്കാട്", 650, 0, 0 },
		{ u"കുടിവെള്ള പദ്ധതിക്ക് തുടക്കം", u"ആലപ്പുഴ", 520, 1, 1.0 },
		{ u"യുവജനോത്സവം സമാപിച്ചു", u"കൊല്ലം", 420, 0, 0 },
		{ u"ആശുപത്രിയിൽ പുതിയ ബ്ലോക്ക്", u"മലപ്പുറം", 360, 0, 0 },
		{ u"വായനശാല വാർഷികം", u"കാസർഗോഡ്", 300, 0, 0 },
		{ u"മത്സ്യബന്ധനത്തിന് വിലക്ക്", u"വിഴിഞ്ഞം", 260, 0, 0 },
		{ u"പുസ്തകോത്സവത്തിന് തുടക്കം", u"കോട്ടയം", 1250, 1, 0.8 } };
	QList<StoryIn> out;
	int si = 0;
	int n = 0;
	for (const S& s : samples)
	{
		StoryIn st;
		st.key = QStringLiteral("sample:%1").arg(n++);
		st.title = QString::fromUtf16(s.title);
		st.dateline = QString::fromUtf16(s.place);
		QString para;
		int total = 0;
		while (total < s.chars)
		{
			const QString& sen = sentences[si++ % sentences.size()];
			para += (para.isEmpty() ? QString() : QStringLiteral(" ")) + sen;
			total += sen.length() + 1;
			if (para.length() > 320)
			{
				st.body << para;
				para.clear();
			}
		}
		if (!para.isEmpty())
			st.body << para;
		st.bodyChars = total;
		for (int i = 0; i < s.photos; ++i)
		{
			PhotoIn ph;
			ph.aspect = s.aspect;
			ph.caption = sentences[(si + 3) % sentences.size()].left(70);
			st.photos << ph;
		}
		out << st;
	}
	return out;
}

QList<StoryIn> sampleObituaries()
{
	struct O { const char16_t* name; const char16_t* place; int age; bool photo; };
	static const O obits[] = {
		{ u"കെ. ജി. രാഘവൻ", u"ചേർത്തല", 78, true },
		{ u"മേരി ജോസഫ്", u"കോട്ടയം", 84, true },
		{ u"പി. കെ. നാരായണൻ", u"തളിപ്പറമ്പ്", 69, true },
		{ u"സരോജിനി അമ്മ", u"പാലക്കാട്", 91, true },
		{ u"അബ്ദുൽ ഖാദർ", u"തിരൂർ", 72, true },
		{ u"തോമസ് വർഗീസ്", u"തിരുവല്ല", 66, true },
		{ u"ലീല", u"കണ്ണൂർ", 58, true },
		{ u"ഗോപാലകൃഷ്ണൻ", u"വടകര", 80, true },
		{ u"ആയിഷ", u"മലപ്പുറം", 63, true },
		{ u"കുഞ്ഞികൃഷ്ണൻ നായർ", u"ആലപ്പുഴ", 87, true },
		{ u"ശാരദ", u"കൊല്ലം", 75, false },
		{ u"ജോൺ മാത്യു", u"ഇടുക്കി", 70, true },
		{ u"വിശ്വനാഥൻ", u"കാസർഗോഡ്", 61, true },
		{ u"ഫാതിമ", u"കോഴിക്കോട്", 82, true },
		{ u"രാധാകൃഷ്ണൻ", u"തൃശൂർ", 67, true },
		{ u"അർജുനൻ", u"പത്തനംതിട്ട", 74, false } };
	// "<age> അന്തരിച്ചു. ഭാര്യ/ഭർത്താവ്: ... മക്കൾ: ... സംസ്കാരം ... വീട്ടുവളപ്പിൽ."
	const QString died = QStringLiteral("അന്തരിച്ചു.");
	const QString family = QStringLiteral("മക്കൾ: സുരേഷ്, ബീന, അനിൽ. മരുമക്കൾ: സിനി, മധു.");
	const QString funeral = QStringLiteral("സംസ്കാരം വ്യാഴാഴ്ച രാവിലെ 10ന് വീട്ടുവളപ്പിൽ.");
	QList<StoryIn> out;
	int n = 0;
	for (const O& o : obits)
	{
		StoryIn st;
		st.key = QStringLiteral("sample-obit:%1").arg(n);
		st.title = QString::fromUtf16(o.name);
		st.dateline = QString::fromUtf16(o.place);
		QString body = QStringLiteral("(%1) ").arg(o.age) + died + QLatin1Char(' ') + family;
		if (n % 3 == 0)
			body += QLatin1Char(' ') + family;
		body += QLatin1Char(' ') + funeral;
		st.body << body;
		st.bodyChars = body.length();
		if (o.photo)
		{
			PhotoIn ph;
			ph.aspect = 1.0;
			st.photos << ph;
		}
		out << st;
		++n;
	}
	// three short news for the news columns
	QList<StoryIn> news = sampleStories();
	for (int i = 6; i < 9 && i < news.size(); ++i)
		out << news[i];
	return out;
}

PageIn samplePage(ScribusDoc* doc)
{
	PageIn p;
	p.doc = doc;
	if (doc && doc->currentPage())
	{
		const ScPage* pg = doc->currentPage();
		p.pageW = pg->width();
		p.pageH = pg->height();
		p.mTop = pg->Margins.top();
		p.mBottom = pg->Margins.bottom();
		p.mLeft = pg->Margins.left();
		p.mRight = pg->Margins.right();
		p.guideCols = pg->guides.verticalAutoCount() > 0 ? pg->guides.verticalAutoCount() + 1 : 0;
	}
	else
	{
		p.pageW = 350 * kMm;
		p.pageH = 538 * kMm;
		p.mTop = 26 * kMm;
		p.mBottom = 15 * kMm;
		p.mLeft = 10 * kMm;
		p.mRight = 10 * kMm;
	}
	p.styles = StyleNames::fromSettings(QString());
	p.stories = sampleStories();
	p.note = QObject::tr("Sample page: %n Malayalam sample stories", "", p.stories.size());
	return p;
}
} // namespace SuneerLayout
