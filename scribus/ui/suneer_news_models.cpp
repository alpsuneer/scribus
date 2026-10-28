/*
 News Browser - Layout Models in use (Phase 2).

 Member functions of SuneerNewsPanel: the active model (the dropdown, or the
 model being applied), and the real arrangement of a page with a model:

   arrangeCore()   the Phase 1 engine (SuneerLayout::Job) measured to the end
                   with the real text engine, then - in ONE undo transaction -
                   placed stories moved and re-laid (layoutStory, their text
                   untouched), new stories placed at their slots (placeNews),
                   a settle pass with the real heights, and every story that
                   does not fit moved / placed on the pasteboard and listed.
                   Nothing is ever deleted.
*/
#include "suneer_news_panel.h"
#include "suneertextclean.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QSettings>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTimer>
#include <QTextStream>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QListWidget>
#include <QRegularExpression>

#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneer_obit_model.h"
#include "suneerlayoutengine.h"
#include "suneerlayoutmodel.h"
#include "text/boxes.h"
#include "undomanager.h"
#include "undostate.h"
#include "text/specialchars.h"
#include "undotransaction.h"

namespace {
const double kMm = 72.0 / 25.4;
}

// ---------------------------------------------------------------------------
// The active model
// ---------------------------------------------------------------------------

QString SuneerNewsPanel::activeModelId() const
{
	if (!m_modelOverride.isEmpty())
		return m_modelOverride;
	if (m_layoutModelCmb && m_layoutModelCmb->currentIndex() >= 0)
	{
		const QString id = m_layoutModelCmb->currentData().toString();
		if (!id.isEmpty())
			return id;
	}
	return QStringLiteral("normal");
}

SuneerLayoutModel SuneerNewsPanel::activeModel() const
{
	return SuneerLayoutModels::byId(activeModelId());
}

QString SuneerNewsPanel::activeObitModelId() const
{
	const SuneerLayoutModel m = activeModel();
	return m.isObituary() ? m.id : QStringLiteral("obituary");
}

SuneerObitModel SuneerNewsPanel::obitModel(QString* note) const
{
	const QString id = activeObitModelId();
	if (id == QLatin1String("obituary"))
		return SuneerObitModel::load(note);      // the built-in: its own load (writes defaults, migrations)
	SuneerObitModel om;
	const SuneerLayoutModel m = SuneerLayoutModels::byId(id);
	if (!m.obituary.isEmpty())
		om.fromJson(m.obituary);
	return om;
}

void SuneerNewsPanel::selectModelInCombo(const QString& id)
{
	SuneerLayoutModels::setLastModelFor(layoutModelKey(), id);
	preselectLayoutModel();
}

QList<QPair<QString, QString>> SuneerNewsPanel::pageModelChoices() const
{
	QList<QPair<QString, QString>> out;
	for (const SuneerLayoutModel& m : SuneerLayoutModels::all())
		out << qMakePair(m.id, m.name);
	return out;
}

// ---------------------------------------------------------------------------
// New stories
// ---------------------------------------------------------------------------

QList<QJsonObject> SuneerNewsPanel::galleryNewStoryJsons(QString* note)
{
	QList<QJsonObject> out;
	if (!m_newsList)
		return out;
	QList<int> ticked, free;
	for (int i = 0; i < m_newsList->count(); ++i)
	{
		QListWidgetItem* row = m_newsList->item(i);
		if (row->isHidden() || isPlacedRow(row))
			continue;
		const int idx = row->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_newsData.size())
			continue;
		if (findStoryItemInDoc(newsServerId(m_newsData[idx].toObject())))
			continue;                              // already in this document (placed, maybe not yet marked)
		free << idx;
		if (row->checkState() == Qt::Checked)
			ticked << idx;
	}
	// the ticked stories not yet placed; when every ticked one is placed, all free ones
	const QList<int> use = ticked.isEmpty() ? free : ticked;
	if (note)
		*note = use.isEmpty() ? tr("no free story in the list") : (ticked.isEmpty() ? tr("every free story in the list") : tr("the ticked stories"));
	for (int idx : use)
		out << m_newsData[idx].toObject();
	return out;
}

SuneerLayout::StoryIn SuneerNewsPanel::storyInFromNews(const QJsonObject& news, const QString& key) const
{
	SuneerLayout::StoryIn s;
	s.key = key;
	logTextJunk(news);
	s.title = collapseSpaces(news["title"].toString());
	s.kicker = collapseSpaces(news["kicker"].toString());
	s.byline = collapseSpaces(news["byline"].toString());
	s.dateline = collapseSpaces(news["dateline"].toString());
	if (s.dateline.isEmpty())
		s.dateline = collapseSpaces(news["place"].toString());
	QStringList paras = contentToParagraphs(news["content"].toString());
	if (s.dateline.isEmpty() && !paras.isEmpty())
	{
		// the same rule as placeNews(): a short first line is the place
		const QString first = paras.first();
		static const QRegularExpression sentenceEnd(QStringLiteral("[.!?।]"));
		if (first.length() <= 25 && first.split(' ', Qt::SkipEmptyParts).size() <= 3 && !first.contains(sentenceEnd))
		{
			s.dateline = first;
			paras.removeFirst();
		}
	}
	if (!paras.isEmpty() && collapseSpaces(paras.first()) == s.title && !s.title.isEmpty())
		paras.removeFirst();
	const QString hl = newsHighlights(news);
	if (!hl.isEmpty())
		s.highlights = hl.split('\n', Qt::SkipEmptyParts);
	s.body = paras;
	for (const QString& p : paras)
		s.bodyChars += p.length();
	s.requestedCols = requestedColumns(news);
	s.requestedHead = requestedHeadStyle(news);
	s.requestedPos = requestedPhotoPos(news);
	const QJsonArray media = mediaList(news);
	for (int mi = 0; mi < media.size(); ++mi)
	{
		const QJsonObject m = media[mi].toObject();
		const QJsonObject md = m["media"].toObject();
		SuneerLayout::PhotoIn ph;
		ph.path = cachedImagePath(md["fileUrl"].toString());
		ph.caption = collapseSpaces(m["caption"].toString());
		if (ph.caption.isEmpty())
			ph.caption = collapseSpaces(md["caption"].toString());
		if (ph.caption.isEmpty() && mi == 0)
			ph.caption = collapseSpaces(news["caption"].toString());
		const QSize sz = ph.path.isEmpty() ? QSize() : QImageReader(ph.path).size();
		if (sz.isValid() && sz.width() > 0)
			ph.aspect = (double) sz.height() / sz.width();
		s.photos << ph;
	}
	return s;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

void SuneerNewsPanel::relayoutPage(const QString& modelId)
{
	if (!m_doc || !m_doc->currentPage())
		return;
	const QString report = arrangePage(modelId, (int) SuneerLayout::Keep::PlacedFrames, false);
	QMessageBox::information(this, tr("Re-layout page"), report);
}

QString SuneerNewsPanel::arrangePage(const QString& modelId, int keepI, bool withNew, bool selectionOnly)
{
	if (!m_doc || !m_doc->currentPage())
		return tr("No page");
	const SuneerLayout::Keep keep = (SuneerLayout::Keep) keepI;
	if (!m_preview)
		selectModelInCombo(modelId);      // remembered for this page type; the dropdown shows it
	const SuneerLayoutModel model = SuneerLayoutModels::byId(modelId);
	{
		// the model first: a broken file must not reach the arrangement
		const ScPage* cp = m_doc->currentPage();
		const int guideCols = cp->guides.verticalAutoCount() > 0 ? cp->guides.verticalAutoCount() + 1 : 0;
		const QStringList bad = SuneerLayout::checkModel(model, cp->width(), cp->height(), cp->Margins.top(), cp->Margins.bottom(), cp->Margins.left(), cp->Margins.right(), guideCols);
		if (!bad.isEmpty())
		{
			if (m_arrangeOut)
				m_arrangeOut->refused = true;
			return tr("\"%1\" was not applied - nothing on the page was changed:").arg(model.name.isEmpty() ? modelId : model.name)
			       + QStringLiteral("\n\u2022 ") + bad.join(QStringLiteral("\n\u2022 ")) + QStringLiteral("\n") + tr("Correct the model in Layout > Layout Models...");
		}
	}
	QString note;
	// a Gallery card: the new stories were read (and their pictures fetched) before the copy was made
	const QList<QJsonObject> fresh = (withNew && !selectionOnly) ? (m_preview ? m_previewFresh : galleryNewStoryJsons(&note)) : QList<QJsonObject>();
	// the News Browser's own obit modules are re-packed by the obit engine; a page made by hand (or an
	// old SLA) is arranged from its detected modules like any other model
	bool nbObits = false;
	if (model.isObituary() && !selectionOnly)
	{
		const ScPage* cp = m_doc->currentPage();
		const QRectF pr(cp->xOffset(), cp->yOffset(), cp->width(), cp->height());
		for (PageItem* it : m_doc->DocItems)
			if (it && attr(it, "news.model").startsWith(QLatin1String("obituary")) && it->getBoundingRect().intersects(pr))
				nbObits = true;
	}
	if (model.isObituary() && !fresh.isEmpty())
	{
		// new stories for the Obituary engine: placed by it (photos processed in the background);
		// a page without new stories is re-arranged by the layout engine below, so "news left" /
		// "news right" can be switched on the modules as they are
		QStringList lines;
		lines << tr("%1 on page %2:").arg(model.name).arg(m_doc->currentPageNumber() + 1);
		if (keep == SuneerLayout::Keep::PlacedFrames)
		{
			int moved = 0;
			bool changed = false;
			lines << obitRepackPage(&moved, &changed);
		}
		if (!fresh.isEmpty() && m_preview)
			lines << tr("%n new story(ies) will be placed by the Obituary engine on Apply (not in this picture).", "", fresh.size());
		else if (!fresh.isEmpty())
		{
			lines << tr("%n new story(ies) are being placed (pictures are processed in the background; the result comes in the status line).", "", fresh.size());
			placeObituaries(fresh);
		}
		if (lines.size() == 1)
			lines << tr("Nothing to do: \"%1\" keeps every frame and there is no new story.").arg(SuneerLayout::keepTitle(keep));
		if (m_arrangeOut)
		{
			m_arrangeOut->obitEngine = true;
			m_arrangeOut->model = model;
		}
		return lines.join('\n');
	}
	bool changed = false;
	return arrangeCore(model, keepI, fresh, &changed, selectionOnly);
}

// ---------------------------------------------------------------------------
// Gallery cards: the arrangement in a scratch copy of the page
// ---------------------------------------------------------------------------

SuneerLayoutGallery::Preview SuneerNewsPanel::previewArrange(const QString& modelId, int keepI, bool selectionOnly, const QList<PageItem*>& selection, int widthPx)
{
	using namespace SuneerLayout;
	SuneerLayoutGallery::Preview out;
	if (!m_doc || !m_doc->currentPage())
	{
		out.error = tr("No page");
		return out;
	}
	if (m_preview)
	{
		out.error = tr("busy");
		return out;
	}
	QElapsedTimer clock;
	clock.start();
	ScribusDoc* real = m_doc;
	const int pageIdx = real->currentPageNumber();

	// the new stories first, with the real document in place: the record, the pictures (network)
	QList<QJsonObject> fresh;
	if (!selectionOnly)
	{
		QString note;
		fresh = galleryNewStoryJsons(&note);
		for (QJsonObject& n : fresh)
		{
			fetchMediaIfMissing(n);
			for (const QJsonValue& v : mediaList(n))
			{
				QString why;
				downloadImage(v.toObject()["media"].toObject()["fileUrl"].toString(), &why);
			}
		}
	}

	UndoBlocker blocker;
	std::unique_ptr<ScratchPage> sp = makeScratchPage(real, pageIdx);
	if (!sp->error.isEmpty())
	{
		out.error = tr("The copy of the page failed: %1").arg(sp->error);
		return out;
	}
	// the selection, as copies
	QSet<PageItem*> selTop;
	for (PageItem* it : selection)
	{
		PageItem* top = it;
		while (top && top->isGroupChild())
			top = top->Parent;
		if (top)
			selTop.insert(top);
	}
	sp->doc->m_Selection->clear();
	if (selectionOnly)
		for (int i = 0; i < sp->source.size(); ++i)
			if (selTop.contains(sp->source[i]) && sp->copies[i])
				sp->doc->m_Selection->addItem(sp->copies[i]);

	ArrangeOut ao;
	QString report;
	{
		struct Back
		{
			SuneerNewsPanel* p;
			ScribusDoc* doc;
			~Back() { p->m_doc = doc; p->m_preview = false; p->m_arrangeOut = nullptr; p->m_previewFresh.clear(); }
		} back { this, real };
		m_doc = sp->doc;
		m_preview = true;
		m_arrangeOut = &ao;
		m_previewFresh = fresh;
		report = arrangePage(modelId, keepI, true, selectionOnly);
	}
	if (ao.refused)
	{
		out.error = report;
		return out;
	}
	const SuneerLayoutModel model = SuneerLayoutModels::byId(modelId);
	if (ao.content.isEmpty())
	{
		// the Obituary engine: the model's grid on the page
		const ScPage* pg = sp->doc->Pages->at(0);
		const int guideCols = pg->guides.verticalAutoCount() > 0 ? pg->guides.verticalAutoCount() + 1 : 0;
		const Grid g = gridFor(model, pg->width(), pg->height(), pg->Margins.top(), pg->Margins.bottom(), pg->Margins.left(), pg->Margins.right(), guideCols);
		ao.content = g.content;
		for (int c = 0; c < g.cols; ++c)
			ao.colX << g.x(c);
		ao.colW = g.colW;
		ao.gapV = model.spacing().pt(SuneerSpacing::ModuleGapV);
	}
	out.image = renderDocPage(sp->doc, 0, sp->masters, widthPx);
	out.signature = resultSignature(sp->doc, 0, sp->copies);
	const QStringList baseline = resultSignature(real, pageIdx, sp->source);
	out.noChange = out.signature == baseline;
	int overflow = 0;
	const double freePt = freeSpacePt(sp->doc, 0, sp->masters, ao.content, ao.colX, ao.colW, ao.gapV, &overflow);
	if (ao.obitEngine)
		out.fitLine = tr("Obituary engine · %1 pt free").arg(qRound(freePt));
	else if (ao.model.isObituary() && ao.total > 0)
		out.fitLine = tr("%1 obits + %2 news · %3 · %4 mm free").arg(ao.obits).arg(ao.news)
		                  .arg(ao.notFit.isEmpty() ? tr("fits") : tr("%n not fitting", "", ao.notFit.size())).arg(qRound(freePt / kMm));
	else if (ao.total == 0)
		out.fitLine = tr("No stories to arrange · %1 pt free").arg(qRound(freePt));
	else
		out.fitLine = tr("%1 of %2 fit · %3 pt free").arg(ao.fit).arg(ao.total).arg(qRound(freePt));
	if (overflow > 0)
		out.fitLine += tr(" · %n overflow", "", overflow);
	if (ao.parked > 0)
		out.fitLine += tr(" · %n moved off to make room", "", ao.parked);
	if (ao.obitPacked)
		out.fitLine += tr("\nObituary page: column packing, %1 mm gaps").arg(ao.model.gapVMm, 0, 'f', 1);
	out.allFit = ao.notFit.isEmpty() && overflow == 0;
	out.notFit = ao.notFit;
	out.report = report;
	out.ms = clock.elapsed();
	out.ok = true;
	if (debugOn())
	{
		const int moved = signatureDiff(out.signature, baseline);
		fprintf(stderr, "LAYOUT-CARD %s: copy %lld ms, total %lld ms, %d of %d frame(s) changed against the page, fit=\"%s\" thumbnail=rendered from the arranged copy (%d items)\n",
		        qPrintable(modelId), (long long) sp->ms, (long long) out.ms, moved, (int) baseline.size(), qPrintable(out.fitLine), (int) sp->doc->Items->count());
		for (int i = 0; i < out.signature.size(); ++i)
			if (i >= baseline.size() || out.signature[i] != baseline[i])
				fprintf(stderr, "LAYOUT-CARD %s:   %s  (was %s)\n", qPrintable(modelId), qPrintable(out.signature[i]), i < baseline.size() ? qPrintable(baseline[i]) : "-");
	}
	return out;
}

// ---------------------------------------------------------------------------
// The arrangement
// ---------------------------------------------------------------------------

void SuneerNewsPanel::relayoutPlacedStory(const SuneerLayout::StoryIn& s, double x, double y, int cols, int imgCols, const QString& pos, const QString& headStyle)
{
	QList<PageItem*> items;
	bool grouped = false;
	for (const QPointer<PageItem>& p : s.items)
		if (p)
		{
			items << p.data();
			grouped = grouped || p->isGroup();
		}
	if (items.isEmpty())
		return;
	if (!s.textItem || grouped || !s.textItem->isTextFrame())
	{
		// a module this layout routine does not know (an obit group, a hand-made story): moved as it is
		const QRectF r = itemsRect(items);
		moveStory(items, x - r.left(), y - r.top());
		return;
	}
	// a story with its own gaps ("Adjust gaps"): those inside the story, the model's between stories
	const std::optional<SuneerSpacing::Values> savedOverride = m_gapOverride;
	struct RestoreGaps { SuneerNewsPanel* p; std::optional<SuneerSpacing::Values> v; ~RestoreGaps() { p->m_gapOverride = v; } } restoreGaps { this, savedOverride };
	const bool userGaps = attr(s.textItem.data(), "layout.gapsUser") == QLatin1String("1");
	if (userGaps)
	{
		SuneerSpacing::Values v = spacingValues();
		SuneerSpacing::Values own = v;
		if (SuneerSpacing::gapsFromString(attr(s.textItem.data(), "layout.gaps"), own))
		{
			for (SuneerSpacing::Key k : SuneerSpacing::gapKeys())
				if (SuneerSpacing::isStoryGap(k))
					v.v[k] = own.v[k];
			m_gapOverride = v;
		}
	}
	StoryFrames f;
	f.text = s.textItem.data();
	if (s.kind == QLatin1String("news"))
	{
		QMap<int, PageItem*> photos, captions;
		for (PageItem* it : items)
		{
			const QString role = attr(it, "news.role");
			if (role == QLatin1String("photo"))
				photos.insert(attr(it, "news.index").toInt(), it);
			else if (role == QLatin1String("caption"))
				captions.insert(attr(it, "news.index").toInt(), it);
		}
		for (auto it = photos.constBegin(); it != photos.constEnd(); ++it)
		{
			f.photos << it.value();
			f.captions << captions.value(it.key(), nullptr);
		}
	}
	else
	{
		// a detected module: its photos and captions as found on the page
		for (int i = 0; i < s.photoItems.size(); ++i)
			if (s.photoItems[i])
			{
				f.photos << s.photoItems[i].data();
				f.captions << (i < s.captionItems.size() ? s.captionItems[i].data() : nullptr);
			}
	}
	// empty lines at the ends of the text / captions go before anything is measured (undoable)
	{
		const SuneerTextClean::StoryReport cr = SuneerTextClean::cleanStory(f.text, true);
		if (cr.changed())
			apiLog(QStringLiteral("re-layout: \"%1\" cleaned: %2").arg(f.text->itemName(), cr.text()));
		for (PageItem* c : std::as_const(f.captions))
			if (c)
				SuneerTextClean::cleanStory(c, true);
	}
	// separate headline / kicker frames: full module width, fitted, stacked above the body frame
	if (!s.heads.isEmpty())
	{
		double totalW0 = 0, g0 = 0;
		storyGeometry(cols, totalW0, g0);
		double cy = y;
		for (const QPointer<PageItem>& h : s.heads)
		{
			if (!h || !h->isTextFrame())
				continue;
			h->setXYPos(x, cy);
			h->setWidth(totalW0);
			h->setColumns(1);
			h->updateClip();
			fitFrame(h);
			cy = h->yPos() + h->height();
		}
		cy += spacingValues().pt(SuneerSpacing::HeadlineBody);
		y = cy;
	}
	// the model's headline style for this width (undoable), only on a News Browser / model headline
	const SuneerLayout::StyleNames names = SuneerLayout::StyleNames::fromSettings(panelTitleStyle());
	if (!headStyle.isEmpty() && m_doc->paragraphStyles().contains(headStyle))
	{
		int headlinePos = -1;
		SuneerLayout::storyHead(f.text->itemText, names, &headlinePos);
		StoryText& st = f.text->itemText;
		if (headlinePos >= 0 && SuneerLayout::isModelHeadlineStyle(st.paragraphStyle(headlinePos).parent(), names)
		    && st.paragraphStyle(headlinePos).parent() != headStyle)
		{
			int end = headlinePos;
			while (end < st.length() && st.text(end) != SpecialChars::PARSEP)
				++end;
			end = qMin(end, st.length());
			ParagraphStyle ps;
			ps.setParent(headStyle);
			if (UndoManager::undoEnabled())
			{
				auto* is = new ScOldNewState<ParagraphStyle>(Um::SetStyle);
				is->set("APPLY_PARASTYLE");
				is->set("POS", end);
				is->setStates(st.paragraphStyle(end), ps);
				UndoManager::instance()->action(f.text, is);
			}
			st.applyStyle(end, ps, false);
			f.text->invalidateLayout();
		}
	}
	// headline -> body, kicker -> headline: the story's / the model's gaps on the head paragraphs
	if (s.kind == QLatin1String("news") && s.heads.isEmpty())
		applyHeadGaps(f.text, spacingValues());
	// the new width first, so the headline block height is the one at this width
	double totalW = 0, colGap = 0;
	storyGeometry(cols, totalW, colGap);
	const SuneerSpacing::Values sp = spacingValues();
	f.text->setXYPos(x, y);
	f.text->setWidth(totalW);
	f.text->setColumns(qMax(1, cols));
	f.text->setColumnGap(sp.pt(SuneerSpacing::ColumnGap));
	f.text->setHeight(qMax(f.text->height(), 200.0 * kMm));
	f.text->updateClip();
	f.text->invalidateLayout();
	f.text->layout();
	const int firstBody = SuneerLayout::storyHead(f.text->itemText, SuneerLayout::StyleNames::fromSettings(panelTitleStyle()), nullptr);
	f.headH = attr(f.text, "news.headH").toDouble();
	if (firstBody > 0 && firstBody < f.text->itemText.length())
	{
		const QLineF caret = f.text->textLayout.positionToPoint(firstBody);
		f.headH = qMax(0.0, qMin(caret.y1(), caret.y2()));
	}
	layoutStory(f, x, y, cols, imgCols, pos);
	for (PageItem* it : items)
	{
		setAttr(it, "news.cols", QString::number(cols));
		setAttr(it, "news.photoCols", QString::number(imgCols));
		setAttr(it, "news.photoPos", pos);
	}
	setAttr(f.text, "news.headH", QString::number(f.headH, 'f', 3));
	if (s.kind == QLatin1String("news"))
		rememberGaps(items, spacingModel(), spacingValues(), userGaps);
}

void SuneerNewsPanel::parkOnPasteboard(const QList<QList<PageItem*>>& stories)
{
	ScPage* page = m_doc->currentPage();
	if (!page || stories.isEmpty())
		return;
	const SuneerSpacing::Values sp = spacingValues();
	const double gap = qMax(2.0 * kMm, sp.pt(SuneerSpacing::ModuleGapH));
	const double vgap = qMax(2.0 * kMm, sp.pt(SuneerSpacing::ModuleGapV));
	double rowRight = page->xOffset() + page->width();
	for (int i = 0; i < m_doc->Pages->count(); ++i)
	{
		ScPage* p = m_doc->Pages->at(i);
		if (qAbs(p->yOffset() - page->yOffset()) < 1.0)
			rowRight = qMax(rowRight, p->xOffset() + p->width());
	}
	// right of everything already lying beside the page
	QSet<PageItem*> moving;
	for (const QList<PageItem*>& st : stories)
		for (PageItem* it : st)
			moving.insert(it);
	double edge = rowRight + gap;
	const QRectF band(rowRight, page->yOffset(), 1e6, page->height());
	for (PageItem* it : *m_doc->Items)
	{
		if (moving.contains(it))
			continue;
		const QRectF r = it->getVisualBoundingRect();
		if (r.intersects(band))
			edge = qMax(edge, r.right() + gap);
	}
	double curY = page->yOffset();
	double colW = 0;
	double stackRight = edge;
	for (const QList<PageItem*>& st : stories)
	{
		QRectF r = itemsRect(st);
		if (curY > page->yOffset() + 1.0 && curY + r.height() > page->yOffset() + page->height())
		{
			edge += colW + gap;           // a new stack further out
			curY = page->yOffset();
			colW = 0;
		}
		moveStory(st, edge - r.left(), curY - r.top());
		for (PageItem* it : st)
			it->OwnPage = -1;
		r = itemsRect(st);
		curY = r.bottom() + vgap;
		colW = qMax(colW, r.width());
		stackRight = qMax(stackRight, r.right());
	}
	const MarginStruct* sc = m_doc->scratch();
	if (stackRight > rowRight + sc->right())
		m_doc->setScratchUndoable(sc->left(), stackRight - rowRight + gap);
}

QString SuneerNewsPanel::arrangeCore(const SuneerLayoutModel& modelIn, int keepI, QList<QJsonObject> fresh, bool* changed, bool selectionOnly)
{
	using namespace SuneerLayout;
	if (changed)
		*changed = false;
	if (!m_doc || !m_doc->currentPage())
		return tr("No page");
	const Keep keep = (Keep) keepI;
	ScPage* page = m_doc->currentPage();
	const int pageIdx = m_doc->currentPageNumber();
	SuneerLayoutModel model = modelIn;

	// New stories: the full record and the pictures first, so they are measured with the real photo sizes.
	// (A Gallery card fetched them before the copy of the page was made: no network here.)
	for (int i = 0; i < fresh.size() && !m_preview; ++i)
	{
		statusProgress(tr("Pictures"), i, fresh.size());
		fetchMediaIfMissing(fresh[i]);
		const QJsonArray media = mediaList(fresh[i]);
		for (const QJsonValue& v : media)
		{
			QString why;
			downloadImage(v.toObject()["media"].toObject()["fileUrl"].toString(), &why);
		}
		qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	QList<StoryIn> freshIn;
	QHash<QString, int> freshIdx;
	for (int i = 0; i < fresh.size(); ++i)
	{
		const QString sid = newsServerId(fresh[i]);
		const QString key = QStringLiteral("new:") + (sid.isEmpty() ? QString::number(i) : sid) + QStringLiteral("#%1").arg(i);
		freshIn << storyInFromNews(fresh[i], key);
		freshIdx.insert(key, i);
	}
	const QString titleStyle = panelTitleStyle();
	QList<PageItem*> selected;
	if (selectionOnly)
		for (int i = 0; i < m_doc->m_Selection->count(); ++i)
			selected << m_doc->m_Selection->itemAt(i);
	PageIn pin = collectPage(m_doc, pageIdx, freshIn, StyleNames::fromSettings(titleStyle), layoutModelKey(), selectionOnly ? &selected : nullptr);
	// An obituary page arranged with a news model: the model's story widths and styles, but the
	// Obituary model's grid and gaps and column packing - no row alignment, no news gap between obit
	// modules. byId(model.id) returns this variant while the page is arranged, so every geometry
	// routine (storyGeometry, spacingValues, layoutStory) uses the same grid as the plan.
	const bool obitPacked = !model.isObituary() && isObitPage(pin);
	std::unique_ptr<SuneerLayoutModel> editingBefore;
	if (const SuneerLayoutModel* e = SuneerLayoutModels::editing())
		editingBefore = std::make_unique<SuneerLayoutModel>(*e);
	struct RestoreEditing
	{
		bool active;
		std::unique_ptr<SuneerLayoutModel>& before;
		~RestoreEditing() { if (active) SuneerLayoutModels::setEditing(before.get()); }
	} restoreEditing { obitPacked, editingBefore };
	if (obitPacked)
	{
		model = obitVariant(model, pin);
		SuneerLayoutModels::setEditing(&model);
	}
	if (m_arrangeOut)
		m_arrangeOut->obitPacked = obitPacked;
	{
		// every module and the model on this page, before anything is changed
		const QStringList bad = checkArrange(model, pin);
		if (!bad.isEmpty())
		{
			if (m_arrangeOut)
				m_arrangeOut->refused = true;
			if (!m_preview)
				apiLog(QStringLiteral("layout model %1: refused - %2").arg(model.id, bad.join(QStringLiteral("; "))));
			return tr("\"%1\" was not applied - nothing on the page was changed:").arg(model.name) + QStringLiteral("\n\u2022 ")
			       + bad.join(QStringLiteral("\n\u2022 "));
		}
	}

	// From here on: the active model is the one being applied (geometry, spacing, wrap, photo sizes).
	m_modelOverride = model.id;
	struct Reset
	{
		SuneerNewsPanel* p;
		~Reset() { p->m_modelOverride.clear(); p->m_forceCols = 0; p->m_forceImgCols = 0; p->m_forcePos.clear(); p->m_forceTitle.clear(); }
	} reset { this };
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Layout model: %1").arg(model.name), QString(), Um::IMove);

	// The Obituary engine's 2-column news modules are laid out again in place before they are measured:
	// photo frame = the picture's height at the column width, caption cleaned and fitted to its ink,
	// the model's gaps (photo -> caption, caption -> text, headline -> body); pictures, background, tone
	// and hyphenation stay. The modules are then moved as blocks with their new size.
	int tidied = 0, tidiedObits = 0;
	if ((model.isObituary() || model.obitPacking) && keep != Keep::EveryFrame)
		for (int i = 0; i < pin.stories.size(); ++i)
		{
			StoryIn& s = pin.stories[i];
			if (!s.placed || s.manual || !s.textItem || !s.textItem->isTextFrame())
				continue;
			if (selectionOnly && !s.selected)
				continue;
			if (attr(s.textItem.data(), "news.model") != QLatin1String("obituary-news"))
			{
				// a single obit module (the engine's, or one found on the page): its text frames stacked
				// under the photo - name / caption frame, then the text with the caption -> text gap
				if (s.kind != QLatin1String("obit"))
					continue;
				QList<PageItem*> tops;
				for (const QPointer<PageItem>& p : s.items)
					if (p)
						tops << p.data();
				QList<PageItem*> frames = tops;
				for (PageItem* p : tops)
					if (p->isGroup())
						frames << p->getAllChildren();
				// the text frames under the photo (never the orange check flag or anything beside / above the photo)
				double photoBottom = -1;
				for (PageItem* p : frames)
					if (p->isImageFrame())
						photoBottom = qMax(photoBottom, p->yPos() + p->height());
				QList<PageItem*> texts;
				for (PageItem* p : frames)
					if (p->isTextFrame() && p->itemText.length() > 0 && attr(p, "news.role") != QLatin1String("flag") && !p->itemName().startsWith(QLatin1String("check_"))
					    && (photoBottom < 0 || p->yPos() >= photoBottom - 1.0))
						texts << p;
				const double gapPt = spacingValues().pt(SuneerSpacing::CaptionText);
				int changed = 0;
				std::sort(texts.begin(), texts.end(), [](PageItem* a, PageItem* b) { return a->yPos() < b->yPos(); });
				double nextTop = -1;
				for (int ti = 0; ti < texts.size(); ++ti)
				{
					PageItem* t = texts[ti];
					const double oldY = t->yPos(), oldH = t->height();
					if (nextTop >= 0 && qAbs(t->yPos() - nextTop) > 0.05)
					{
						// the undo state is written from old* by checkChanges(): make them current first
						t->oldXpos = t->xPos(); t->oldYpos = t->yPos(); t->oldWidth = t->width(); t->oldHeight = t->height();
						m_doc->moveItem(0, nextTop - t->yPos(), t);          // undoable
					}
					tidyTextTop(t, nextTop >= 0 ? 0.0 : t->textToFrameDistTop(), true);
					if (qAbs(t->textToFrameDistBottom()) > 0.01)
						t->setTextToFrameDistBottom(0.0);
					// a name / caption frame with text under it: fitted to its ink, so the gap below is the frame edge;
					// the last one to its last line. The new height goes through sizeItem (undoable).
					const double hBefore = t->height();
					if (!(ti + 1 < texts.size() && t->asTextFrame()->suneerFitCaptionHeight(false)))
						fitFrame(t);
					const double hAfter = t->height();
					if (qAbs(hAfter - hBefore) > 0.01)
					{
						t->setHeight(hBefore);
						t->oldXpos = t->xPos(); t->oldYpos = t->yPos(); t->oldWidth = t->width(); t->oldHeight = hBefore;
						m_doc->sizeItem(t->width(), hAfter, t, false, true, false);
						t->checkChanges(true);
					}
					t->updateClip();
					if (qAbs(t->yPos() - oldY) > 0.05 || qAbs(t->height() - oldH) > 0.05)
						++changed;
					nextTop = t->yPos() + t->height() + gapPt;
				}
				for (PageItem* p : tops)
					if (p->isGroup())
					{
						// the group frame follows its children; recorded for undo like a resize (old* -> checkChanges)
						p->oldXpos = p->xPos(); p->oldYpos = p->yPos(); p->oldWidth = p->width(); p->oldHeight = p->height();
						m_doc->resizeGroupToContents(p);
						p->checkChanges(true);
					}
				if (changed > 0)
				{
					s.placedRect = itemsRect(tops).translated(-page->xOffset(), -page->yOffset());
					for (Module& m : pin.modules)
						if (m.id == s.moduleId)
							m.rect = s.placedRect;
					++tidiedObits;
				}
				continue;
			}
			StoryFrames f;
			f.text = s.textItem.data();
			QMap<int, PageItem*> photos, captions;
			QList<PageItem*> parts;
			PageItem* group = nullptr;
			for (const QPointer<PageItem>& p : s.items)
				if (p)
				{
					if (p->isGroup())
					{
						group = p.data();
						parts << p->getAllChildren();
					}
					else
						parts << p.data();
				}
			for (PageItem* it : parts)
			{
				const QString role = attr(it, "news.role");
				if (role == QLatin1String("photo"))
					photos.insert(attr(it, "news.index").toInt(), it);
				else if (role == QLatin1String("caption"))
					captions.insert(attr(it, "news.index").toInt(), it);
			}
			for (auto it = photos.constBegin(); it != photos.constEnd(); ++it)
			{
				f.photos << it.value();
				f.captions << captions.value(it.key(), nullptr);
			}
			for (PageItem* c : std::as_const(f.captions))
				if (c)
					SuneerTextClean::cleanStory(c, true);
			SuneerTextClean::cleanStory(f.text, true);
			f.headH = attr(f.text, "news.headH").toDouble();
			if (f.headH <= 0)
			{
				f.text->invalidateLayout();
				f.text->layout();
				const int firstBody = SuneerLayout::storyHead(f.text->itemText, StyleNames::fromSettings(titleStyle), nullptr);
				if (firstBody > 0 && firstBody < f.text->itemText.length())
				{
					const QLineF caret = f.text->textLayout.positionToPoint(firstBody);
					f.headH = qMax(0.0, qMin(caret.y1(), caret.y2()));
				}
			}
			const int cols = qMax(1, f.text->columns());
			applyHeadGaps(f.text, spacingValues());
			layoutStory(f, f.text->xPos(), f.text->yPos(), cols, 1, f.photos.isEmpty() ? QStringLiteral("none") : QStringLiteral("right"));
			if (group)
			{
				group->oldXpos = group->xPos(); group->oldYpos = group->yPos(); group->oldWidth = group->width(); group->oldHeight = group->height();
				m_doc->resizeGroupToContents(group);
				group->checkChanges(true);
			}
			QList<PageItem*> tops;
			for (const QPointer<PageItem>& p : s.items)
				if (p)
					tops << p.data();
			s.placedRect = itemsRect(tops).translated(-page->xOffset(), -page->yOffset());
			for (Module& m : pin.modules)
				if (m.id == s.moduleId)
					m.rect = s.placedRect;
			++tidied;
		}

	// Measure everything with the real text engine (a scratch document; this document is not touched yet).
	Measurer meas(pin, 0.0);
	Job job(model, &pin, &meas, keep);
	int steps = 0;
	while (job.step())
		if ((++steps % 4) == 0 && !m_preview)
		{
			statusProgress(tr("Measuring"), job.done(), job.total());
			qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
		}
	statusProgressDone();
	const PlanOut& plan = job.plan();
	debugPlan(m_preview ? "preview" : "apply", model, pin, plan);
	const QPointF off(page->xOffset(), page->yOffset());
	m_doc->m_Selection->clear();
	// the modules found on the page are written down (layout.module ...), so the next re-layout is instant
	rememberModules(pin.modules);

	auto itemsOf = [](const StoryIn& s) {
		QList<PageItem*> l;
		for (const QPointer<PageItem>& p : s.items)
			if (p)
				l << p.data();
		return l;
	};
	QList<QRectF> fixedRects;                     // what stays (absolute)
	for (const PageIn::Kept& k : pin.always)
		if (k.obstacle)
			fixedRects << k.rect.translated(off);
	struct Done { QList<PageItem*> items; QString title; bool fresh; int json; QRectF plan; };
	QList<Done> arranged;
	QList<QJsonObject> placedNew, spill;
	QList<QList<PageItem*>> toPark;
	QStringList notFitNames, parkedNames, keptNames;
	QStringList bandNoRoomNames;                  // Obituary engine: 2-column news with no room in the reserved news columns
	QSet<int> inPlan;
	int movedPlaced = 0;

	// The placed stories that move: their photos / captions stop wrapping text until each is laid out
	// again. Moved one by one, a story laid out at its new place would otherwise wrap around the
	// pictures of stories that are still where they were (measured 417 pt, real 700 pt).
	QHash<PageItem*, PageItem::TextFlowMode> flowBefore;
	QSet<PageItem*> relaid;                       // photos / captions whose wrap layoutStory has set again
	{
		QSet<int> moving;
		for (const ModuleOut& mo : plan.modules)
			if (!mo.kept && pin.stories[mo.story].placed)
				moving.insert(mo.story);          // blocks too: their photos must not wrap a story laid out at their old place
		for (int idx : plan.notFit)
			if (pin.stories[idx].placed)
				moving.insert(idx);
		for (int i = 0; i < pin.stories.size(); ++i)
			if (pin.stories[i].placed && !pin.stories[i].manual && keep == Keep::OnlyNew)
				moving.insert(i);
		for (int i : moving)
			for (PageItem* it : itemsOf(pin.stories[i]))
			{
				QList<PageItem*> parts { it };
				if (it->isGroup())
					parts << it->getAllChildren();
				for (PageItem* c : parts)
					if (c->textFlowMode() != PageItem::TextFlowDisabled)
					{
						flowBefore.insert(c, c->textFlowMode());
						c->setTextFlowMode(PageItem::TextFlowDisabled);
					}
			}
	}
	for (const ModuleOut& mo : plan.modules)
	{
		const StoryIn& s = pin.stories[mo.story];
		inPlan.insert(mo.story);
		if (mo.kept)
		{
			fixedRects << s.placedRect.translated(off);
			keptNames << s.title;
			continue;
		}
		const double x = off.x() + mo.rect.x(), y = off.y() + mo.rect.y();
		if (s.placed && s.rigid)
		{
			// a block (group, obit, several frames): moved as it is
			const QList<PageItem*> items = itemsOf(s);
			const QRectF r = itemsRect(items);
			moveStory(items, x - r.left(), y - r.top());
			arranged << Done { items, s.title, false, -1, mo.rect.translated(off) };
			++movedPlaced;
		}
		else if (s.placed)
		{
			relayoutPlacedStory(s, x, y, mo.cols, mo.photoCols,
			                    s.requestedPos.isEmpty() ? model.photoPosition : s.requestedPos,
			                    s.requestedHead.isEmpty() ? model.headlineStyleFor(mo.cols, mo.lead) : s.requestedHead);
			if (s.textItem && !s.textItem->isGroup())
			{
				// laid out again: layoutStory set the wrap of its photos / captions; the story text frame itself keeps its own
				for (const QPointer<PageItem>& p : s.photoItems)
					if (p) relaid.insert(p.data());
				for (const QPointer<PageItem>& p : s.captionItems)
					if (p) relaid.insert(p.data());
				for (PageItem* it : itemsOf(s))
				{
					const QString role = attr(it, "news.role");
					if (role == QLatin1String("photo") || role == QLatin1String("caption"))
						relaid.insert(it);
				}
			}
			arranged << Done { itemsOf(s), s.title, false, -1, mo.rect.translated(off) };
			++movedPlaced;
		}
		else
		{
			const int ji = freshIdx.value(s.key, -1);
			if (ji < 0)
				continue;
			m_forceCols = mo.cols;
			m_forceImgCols = mo.photoCols;
			m_forcePos = s.requestedPos.isEmpty() ? model.photoPosition : s.requestedPos;
			m_forceTitle = s.requestedHead.isEmpty() ? model.headlineStyleFor(mo.cols, mo.lead) : s.requestedHead;
			const QList<PageItem*> items = placeNews(fresh[ji], x, y, false);
			m_forceCols = 0;
			m_forceImgCols = 0;
			m_forcePos.clear();
			m_forceTitle.clear();
			if (!items.isEmpty())
			{
				arranged << Done { items, s.title, true, ji, mo.rect.translated(off) };
				placedNew << fresh[ji];
			}
		}
	}
	for (int idx : plan.notFit)
	{
		const StoryIn& s = pin.stories[idx];
		inPlan.insert(idx);
		if (plan.notFitBand.contains(idx))
			bandNoRoomNames << s.title;           // never moved into the obit columns: listed on their own
		else
			notFitNames << s.title;
		if (s.placed)
			toPark << itemsOf(s);
		else if (freshIdx.contains(s.key))
			spill << fresh[freshIdx.value(s.key)];
	}
	// "Only new stories": the placed stories make room (moved beside the page, listed)
	for (int i = 0; i < pin.stories.size(); ++i)
		if (!inPlan.contains(i) && pin.stories[i].placed && !pin.stories[i].manual)
		{
			toPark << itemsOf(pin.stories[i]);
			parkedNames << pin.stories[i].title;
		}

	// Settle with the real heights: a module that now overlaps the one above moves down;
	// one that then runs past the bottom goes to the pasteboard too.
	{
		const Grid grid = gridFor(model, pin);
		const double bottom = off.y() + (pin.area.isNull() ? grid.content.bottom() : pin.area.bottom());
		const double gapV = model.spacing().pt(SuneerSpacing::ModuleGapV);
		// top first, then left: modules at the same height settle in the same order on the page and in a
		// Gallery card's copy (std::sort is not stable; equal tops came out in different orders)
		std::stable_sort(arranged.begin(), arranged.end(), [](const Done& a, const Done& b) {
			const QRectF ra = itemsRect(a.items), rb = itemsRect(b.items);
			if (qAbs(ra.top() - rb.top()) > 0.25)
				return ra.top() < rb.top();
			return ra.left() < rb.left() - 0.25;
		});
		QList<QRectF> settled = fixedRects;
		QList<Done> stay;
		for (Done& d : arranged)
		{
			QRectF r = itemsRect(d.items);
			// measured (plan) against real: the difference is what the settle pass absorbs
			const QString planVsReal = QStringLiteral("layout model %1: '%2' plan %3x%4 at %5,%6 real %7x%8 at %9,%10 pt")
			           .arg(model.id, d.title.left(24)).arg(d.plan.width(), 0, 'f', 1).arg(d.plan.height(), 0, 'f', 1).arg(d.plan.x() - off.x(), 0, 'f', 1)
			           .arg(d.plan.y() - off.y(), 0, 'f', 1).arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1).arg(r.x() - off.x(), 0, 'f', 1).arg(r.y() - off.y(), 0, 'f', 1);
			if (!m_preview)
				apiLog(planVsReal);
			if (debugOn())
				fprintf(stderr, "LAYOUT-REAL %s %s\n", m_preview ? "preview" : "apply", qPrintable(planVsReal));
			for (int guard = 0; guard < 50; ++guard)
			{
				double push = 0;
				for (const QRectF& o : settled)
				{
					const bool hOverlap = r.right() > o.left() + 1.0 && r.left() < o.right() - 1.0;
					const bool vOverlap = r.top() < o.bottom() - 0.5 && r.bottom() > o.top() + 0.5;
					if (hOverlap && vOverlap && o.top() <= r.top() + 0.5)
						push = qMax(push, o.bottom() + gapV - r.top());
				}
				if (push <= 0.01)
					break;
				moveStory(d.items, 0, push);
				r = itemsRect(d.items);
			}
			if (model.decks == 0)
			{
				// free packing: a module that was planned under a taller-measured neighbour closes up to
				// gapV under the nearest settled module above it (plan vs real heights; never into a deck)
				double ceiling = off.y() + plan.content.top();
				for (const QRectF& o : settled)
				{
					const bool hOverlap = r.right() > o.left() + 1.0 && r.left() < o.right() - 1.0;
					if (hOverlap && o.bottom() <= r.top() + 0.5)
						ceiling = qMax(ceiling, o.bottom() + gapV);
				}
				const double pull = r.top() - ceiling;
				if (pull > 0.5)
				{
					moveStory(d.items, 0, -pull);
					r = itemsRect(d.items);
				}
			}
			if (r.bottom() > bottom + 1.0)
			{
				notFitNames << d.title;
				toPark << d.items;
				continue;
			}
			settled << r;
			stay << d;
		}
		arranged = stay;
	}
	parkOnPasteboard(toPark);
	// stories that were not laid out again (on the pasteboard now, or moved as they are) get their wrap back
	for (auto it = flowBefore.constBegin(); it != flowBefore.constEnd(); ++it)
		if (!relaid.contains(it.key()))
			it.key()->setTextFlowMode(it.value());
	if (!spill.isEmpty() && !m_preview)
		placeOnPasteboard(spill, true);
	if (!selectionOnly)
		respaceRules(model.spacing());
	if (!placedNew.isEmpty() && !m_preview)
		recordPlaceUndo(placedNew);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	if (!m_preview)
	{
		m_doc->changed();
		m_doc->regionsChanged()->update(QRectF());
		if (m_mw)
			m_mw->emitUpdateRequest(0);
		if (!placedNew.isEmpty() || !spill.isEmpty())
			markPlaced(placedNew + spill);
	}
	if (changed)
		*changed = true;

	QStringList lines;
	lines << tr("%1 on page %2 (keep: %3%4):").arg(model.name).arg(pageIdx + 1).arg(keepTitle(keep), selectionOnly ? tr(", selection only") : QString());
	if (obitPacked)
		lines << tr("\u2022 Obituary page: packed in columns with the obituary gaps (%1 mm between modules, %2 mm between columns), no row alignment")
		             .arg(model.gapVMm, 0, 'f', 1).arg(model.gutterMm, 0, 'f', 1);
	if (tidied > 0)
		lines << tr("\u2022 %n news module(s) laid out again: photo at the picture's height, caption fitted, model gaps", "", tidied);
	if (tidiedObits > 0)
		lines << tr("\u2022 %n obit module(s): text frames fitted and stacked (first line at the font ascent, caption -> text gap)", "", tidiedObits);
	if (pin.looseRules > 0)
		lines << tr("\u2022 %n rule(s) of the old arrangement left where they were - check them", "", pin.looseRules);
	if (pin.detected > 0)
		lines << tr("\u2022 %n module(s) found by detection (now remembered on the frames)", "", pin.detected);
	int onPagePlaced = 0, onPageNew = 0;
	for (const Done& d : arranged)
		(d.fresh ? onPageNew : onPagePlaced)++;
	Q_UNUSED(movedPlaced);
	lines << tr("• %n placed story(ies) arranged on the page", "", onPagePlaced);
	if (onPageNew > 0)
		lines << tr("• %n new story(ies) placed on the page", "", onPageNew);
	if (!keptNames.isEmpty())
		lines << tr("• %n story(ies) kept where they are", "", keptNames.size());
	if (model.isObituary() && plan.bandFirst > 0)
	{
		// suneer 2026-10-26: the reserved news columns take only the 2-column news (workflow count 2)
		const QString bandCols = QStringLiteral("%1-%2").arg(plan.bandFirst).arg(plan.bandLast);
		const QString areaCols = QStringLiteral("%1-%2").arg(plan.areaFirst).arg(plan.areaLast);
		bool keptInBand = false;
		{
			const Grid g = gridFor(model, pin);
			const double bx0 = g.x(plan.bandFirst - 1), bx1 = g.x(plan.bandLast - 1) + g.colW;
			for (int idx : plan.keptStories)
				if (pin.stories[idx].placedRect.right() > bx0 + 1.0 && pin.stories[idx].placedRect.left() < bx1 - 1.0)
					keptInBand = true;
		}
		if (plan.bandPlaced == 0 && bandNoRoomNames.isEmpty() && !keptInBand)
			lines << tr("• Columns %1: no 2-column news (workflow count 2) - left blank").arg(bandCols);
		if (!bandNoRoomNames.isEmpty())
			lines << tr("• 2-column news with no room in columns %1 - on the pasteboard at the right, not moved into columns %2, nothing deleted (%3):").arg(bandCols, areaCols).arg(bandNoRoomNames.size())
			      << QStringLiteral("    ") + bandNoRoomNames.join(QStringLiteral("\n    "));
		if (!notFitNames.isEmpty())
			lines << tr("• No room in columns %1 - on the pasteboard at the right, never moved into columns %2, nothing deleted (%3):").arg(areaCols, bandCols).arg(notFitNames.size())
			      << QStringLiteral("    ") + notFitNames.join(QStringLiteral("\n    "));
	}
	else if (!notFitNames.isEmpty())
		lines << tr("• Not fitting on the page - on the pasteboard at the right, nothing deleted (%1):").arg(notFitNames.size())
		      << QStringLiteral("    ") + notFitNames.join(QStringLiteral("\n    "));
	if (!parkedNames.isEmpty())
		lines << tr("• Placed stories moved to the pasteboard to make room (%1):").arg(parkedNames.size())
		      << QStringLiteral("    ") + parkedNames.join(QStringLiteral("\n    "));
	lines << tr("One undo step (Edit > Undo).");
	const QString report = lines.join('\n');
	if (m_arrangeOut)
	{
		ArrangeOut& o = *m_arrangeOut;
		o.model = model;
		o.total = arranged.size() + keptNames.size() + notFitNames.size() + bandNoRoomNames.size();
		o.fit = arranged.size() + keptNames.size();
		o.notFit = notFitNames + bandNoRoomNames;
		o.parked = parkedNames.size();
		const Grid grid = gridFor(model, pin);
		o.content = pin.area.isNull() ? grid.content : QRectF(plan.content.left(), pin.area.top(), plan.content.width(), pin.area.height());
		o.colX = plan.colX;
		o.colW = plan.colW;
		o.gapV = model.spacing().pt(SuneerSpacing::ModuleGapV);
		o.obits = plan.obits;
		o.news = plan.news;
	}
	if (m_preview)
		return report;
	if (model.isObituary())
	{
		// the obituary model last applied: the default card on the next obituary page
		QSettings st("Faircode", "ScribusNews");
		st.setValue(QStringLiteral("layoutModel/_obituaryDefault"), model.id);
	}
	setStatus(tr("%1: %2 arranged, %3 not fitting").arg(model.name).arg(arranged.size()).arg(notFitNames.size() + bandNoRoomNames.size()), !notFitNames.isEmpty() || !bandNoRoomNames.isEmpty());
	setStatusDetails(report);
	apiLog(QStringLiteral("layout model %1: %2").arg(model.id, report.simplified()));
	return report;
}

// ---------------------------------------------------------------------------
// Regression test: every model, card -> Apply -> Undo (tests/suneer/layout_models_apply.sh)
// ---------------------------------------------------------------------------

// The geometry inside the obituary 2-column news items (news.model obituary-news): the body's columns
// (lines per column, where each ends), the photo (frame vs picture: letterbox band), the caption (lines,
// ink, wrap offsets) and the gaps after the caption. Groups are looked into.
void SuneerNewsPanel::newsItemDiagnostics(int pageIdx, const std::function<void(const QString&)>& line)
{
	using namespace SuneerLayout;
	const ScPage* pg = m_doc->Pages->at(pageIdx);
	const QPointF off(pg->xOffset(), pg->yOffset());
	QHash<QString, QList<PageItem*>> stories;
	QList<PageItem*> onPage;
	std::function<void(PageItem*)> collect = [&](PageItem* it) {
		if (it->isGroup())
		{
			for (PageItem* c : it->groupItemList)
				collect(c);
			return;
		}
		onPage << it;
		if (attr(it, "news.model") == QLatin1String("obituary-news") && !attr(it, "news.story").isEmpty())
			stories[attr(it, "news.story")] << it;
	};
	for (PageItem* it : pageTopItems(m_doc, pageIdx))
		collect(it);
	const SuneerSpacing::Values sp = SuneerSpacing::forModel(activeObitModelId());
	line(QStringLiteral("NEWSDIAG gaps of model %1: wrapPad %2 captionGap %3 captionText %4 photoText %5 headlineBody %6 moduleGapV %7 (mm)")
	         .arg(activeObitModelId()).arg(sp.v[SuneerSpacing::WrapPad]).arg(sp.v[SuneerSpacing::CaptionGap]).arg(sp.v[SuneerSpacing::CaptionText])
	         .arg(sp.v[SuneerSpacing::PhotoText]).arg(sp.v[SuneerSpacing::HeadlineBody]).arg(sp.v[SuneerSpacing::ModuleGapV]));
	auto mm = [](double pt) { return QString::number(pt / kMm, 'f', 1); };
	// every module: text frames stacked one under another (name / caption frame -> text frame): the frame gap,
	// the lower frame's top inset and first line offset, and the visual distance upper frame bottom -> first line top
	{
		const PageIn p = collectPage(m_doc, pageIdx, QList<StoryIn>(), StyleNames::fromSettings(panelTitleStyle()), layoutModelKey(), nullptr);
		int shown = 0;
		for (const StoryIn& s : p.stories)
		{
			if (!s.placed || shown >= 12)
				continue;
			QList<PageItem*> texts;
			for (const QPointer<PageItem>& it : s.items)
				if (it)
				{
					QList<PageItem*> parts { it.data() };
					if (it->isGroup())
						parts = it->getAllChildren();
					for (PageItem* c : parts)
						if (c->isTextFrame() && c->itemText.length() > 0)
							texts << c;
				}
			texts.erase(std::remove_if(texts.begin(), texts.end(), [](PageItem* t) { return attr(t, "news.role") == QLatin1String("flag") || t->itemName().startsWith(QLatin1String("check_")); }), texts.end());
			if (texts.size() < 2)
				continue;
			std::sort(texts.begin(), texts.end(), [](PageItem* a, PageItem* b) { return a->yPos() < b->yPos(); });
			for (int k = 1; k < texts.size(); ++k)
			{
				PageItem* up = texts[k - 1];
				PageItem* lo = texts[k];
				if (lo->xPos() + lo->width() <= up->xPos() + 1 || lo->xPos() >= up->xPos() + up->width() - 1)
					continue;
				lo->invalidateLayout();
				lo->layout();
				double firstTop = -1, firstBase = -1, firstAsc = 0;
				if (const Box* root = lo->textLayout.box())
				{
					std::function<void(const Box*, double)> walk = [&](const Box* b, double y0) {
						if (firstTop >= 0) return;
						if (b->type() == Box::T_Line) { firstTop = y0 + b->y(); firstAsc = b->ascent(); firstBase = y0 + b->y() + b->ascent(); }
						else for (const Box* c : b->boxes()) walk(c, y0 + (b == root ? 0 : b->y()));
					};
					walk(root, 0);
				}
				const ParagraphStyle& ps = lo->itemText.paragraphStyle(0);
				const double frameGap = lo->yPos() - (up->yPos() + up->height());
				QString head;
				for (int c = 0; c < qMin(3, lo->itemText.length()); ++c)
					head += QStringLiteral("U+%1 ").arg(lo->itemText.text(c).unicode(), 4, 16, QLatin1Char('0'));
				const PageItem_TextFrame::SuneerInkMetrics um = up->asTextFrame()->suneerInkMetrics();
				QString others;
				for (PageItem* t : texts)
					others += QStringLiteral(" [%1 y %2 h %3 flow %4]").arg(t->itemName().left(10), mm(t->yPos() - off.y()), mm(t->height())).arg((int) t->textFlowMode());
				line(QStringLiteral("STACKDIAG    lower starts with %1; upper frame h %2 mm, ink bottom %3 mm, last baseline %4 mm, font descent %5 mm, lines %6 (fromInk %7); frames:%8")
				         .arg(head, mm(up->height()), mm(um.lastInkBottom), mm(um.lastBaseline), mm(um.lastFontDescent)).arg(um.lines).arg(um.fromInk ? 1 : 0).arg(others));
				// anything with text flow over the lower frame's area (the reason a first line can start lower)
				QString obst;
				const QRectF lr(lo->xPos(), lo->yPos(), lo->width(), lo->height());
				for (PageItem* o : onPage)
					if (o != lo && o->textFlowMode() != PageItem::TextFlowDisabled && QRectF(o->xPos(), o->yPos(), o->width(), o->height()).adjusted(-4, -4, 4, 4).intersects(lr))
						obst += QStringLiteral(" [%1 type %2 at %3,%4 %5x%6 flow %7 offsets %8/%9]").arg(o->itemName().left(12)).arg((int) o->itemType()).arg(mm(o->xPos() - off.x()), mm(o->yPos() - off.y()), mm(o->width()), mm(o->height())).arg((int) o->textFlowMode()).arg(mm(o->wrapOffsetTop()), mm(o->wrapOffsetBottom()));
				line(QStringLiteral("STACKDIAG    lower frame at %1,%2 %3x%4 mm; items with text flow over it:%5").arg(mm(lr.x() - off.x()), mm(lr.y() - off.y()), mm(lr.width()), mm(lr.height()), obst.isEmpty() ? QStringLiteral(" none") : obst));
				{
					int nPar = 0;
					QString lastStyle;
					for (int c = 0; c < lo->itemText.length(); ++c)
						if (lo->itemText.text(c) == SpecialChars::PARSEP)
							++nPar;
					if (lo->itemText.length() > 0)
						lastStyle = lo->itemText.paragraphStyle(lo->itemText.length() - 1).parent();
					const QChar lastCh = lo->itemText.length() > 0 ? lo->itemText.text(lo->itemText.length() - 1) : QChar();
					int nLines = 0;
					if (const Box* root = lo->textLayout.box())
					{
						std::function<void(const Box*)> walk = [&](const Box* b) { if (b->type() == Box::T_Line) ++nLines; else for (const Box* c : b->boxes()) walk(c); };
						walk(root);
					}
					const PageItem_TextFrame::SuneerInkMetrics lm = lo->asTextFrame()->suneerInkMetrics();
					line(QStringLiteral("STACKDIAG    lower frame: vertical alignment %1, group child %2, %3 chars, %4 paragraph breaks, last char U+%5, last para style '%6', %7 lines, overflow %8; INK first ink top %9 mm, first baseline %10 mm, font ascent %11 mm -> VISUAL (ink) upper bottom -> first ink top %12 mm")
					         .arg(lo->verticalAlignment()).arg(lo->isGroupChild() ? 1 : 0).arg(lo->itemText.length()).arg(nPar).arg(lastCh.unicode(), 4, 16, QLatin1Char('0')).arg(lastStyle).arg(nLines).arg(lo->frameOverflows() ? 1 : 0)
					         .arg(mm(lm.firstInkTop), mm(lm.firstBaseline), mm(lm.firstFontAscent), mm(frameGap + lm.firstInkTop)));
				}
				line(QStringLiteral("STACKDIAG '%1' (%2): upper frame '%3' bottom -> lower frame top %4 mm; lower top inset %5 pt, first line offset %6, first para '%7' before %8 pt; first line top %9 mm / baseline %10 mm below the frame top; VISUAL upper bottom -> first line top %11 mm; upper bottom inset %12 pt")
				         .arg(s.title.left(18), s.kind, up->itemName().left(16), mm(frameGap)).arg(lo->textToFrameDistTop(), 0, 'f', 2).arg((int) lo->firstLineOffset()).arg(ps.parent()).arg(ps.gapBefore(), 0, 'f', 1)
				         .arg(firstTop < 0 ? QStringLiteral("-") : mm(firstTop), firstBase < 0 ? QStringLiteral("-") : mm(firstBase), firstTop < 0 ? QStringLiteral("-") : mm(frameGap + firstTop)).arg(up->textToFrameDistBottom(), 0, 'f', 2));
				++shown;
			}
		}
	}
	// single obit modules (photo + one text frame: name, place, body): photo -> text frame, the frame's top
	// inset, first line offset, the first baseline, the head paragraphs' styles and gaps, line 1 -> line 2
	{
		QHash<QString, QList<PageItem*>> obits;
		for (PageItem* it : onPage)
			if (attr(it, "news.model") == QLatin1String("obituary") && !attr(it, "news.story").isEmpty())
				obits[attr(it, "news.story")] << it;
		int shown = 0;
		for (auto it = obits.constBegin(); it != obits.constEnd() && shown < 8; ++it)
		{
			PageItem *text = nullptr, *photo = nullptr;
			for (PageItem* i : it.value())
			{
				const QString role = attr(i, "news.role");
				if (role == QLatin1String("text") && i->isTextFrame()) text = i;
				else if (role == QLatin1String("photo")) photo = i;
			}
			if (!text || !photo)
				continue;
			++shown;
			text->invalidateLayout();
			text->layout();
			const double frameGap = text->yPos() - (photo->yPos() + photo->height());
			QString firstLines;
			if (const Box* root = text->textLayout.box())
			{
				QList<const Box*> lines;
				std::function<void(const Box*, double)> walk = [&](const Box* b, double y0) {
					if (b->type() == Box::T_Line)
					{
						lines << b;
						firstLines += QStringLiteral(" [line %1: top %2 asc %3 desc %4 -> baseline %5]").arg(lines.size()).arg(mm(y0 + b->y())).arg(mm(b->ascent())).arg(mm(b->descent())).arg(mm(y0 + b->y() + b->ascent()));
					}
					else
						for (const Box* c : b->boxes())
							walk(c, y0 + (b == root ? 0 : b->y()));
					if (lines.size() >= 3) return;
				};
				walk(root, 0);
			}
			QString paras;
			int pos = 0;
			for (int k = 0; k < 3 && pos < text->itemText.length(); ++k)
			{
				const ParagraphStyle& ps = text->itemText.paragraphStyle(pos);
				int end = pos;
				while (end < text->itemText.length() && text->itemText.text(end) != SpecialChars::PARSEP)
					++end;
				paras += QStringLiteral(" [para %1 '%2' %3 chars: before %4 after %5 pt, line spacing %6 pt mode %7, size %8 pt]")
				             .arg(k + 1).arg(ps.parent()).arg(end - pos).arg(ps.gapBefore(), 0, 'f', 1).arg(ps.gapAfter(), 0, 'f', 1).arg(ps.lineSpacing(), 0, 'f', 1).arg((int) ps.lineSpacingMode()).arg(ps.charStyle().fontSize() / 10.0, 0, 'f', 1);
				pos = end + 1;
			}
			line(QStringLiteral("OBITDIAG '%1': photo bottom -> text frame top %2 mm; top inset %3 pt (%4 mm), bottom inset %5 pt; first line offset %6 (0 real glyph height, 1 font ascent, 2 line spacing); frame h %7 mm")
			         .arg(it.key().left(20), mm(frameGap)).arg(text->textToFrameDistTop(), 0, 'f', 2).arg(mm(text->textToFrameDistTop())).arg(text->textToFrameDistBottom(), 0, 'f', 2)
			         .arg((int) text->firstLineOffset()).arg(mm(text->height())));
			line(QStringLiteral("OBITDIAG    lines (mm below the frame top):%1").arg(firstLines));
			line(QStringLiteral("OBITDIAG    paragraphs:%1").arg(paras));
		}
	}
	for (auto it = stories.constBegin(); it != stories.constEnd(); ++it)
	{
		PageItem *text = nullptr, *photo = nullptr, *caption = nullptr;
		for (PageItem* i : it.value())
		{
			const QString role = attr(i, "news.role");
			if (role == QLatin1String("text")) text = i;
			else if (role == QLatin1String("photo")) photo = i;
			else if (role == QLatin1String("caption")) caption = i;
		}
		if (!text)
			continue;
		const QRectF tr(text->xPos() - off.x(), text->yPos() - off.y(), text->width(), text->height());
		line(QStringLiteral("NEWSDIAG story '%1': text frame at %2,%3 %4x%5 mm, %6 column(s), col gap %7 mm, dist T/B %8/%9 pt, overflow %10")
		         .arg(it.key().left(20), mm(tr.x()), mm(tr.y()), mm(tr.width()), mm(tr.height())).arg(text->columns()).arg(mm(text->columnGap()))
		         .arg(text->textToFrameDistTop(), 0, 'f', 1).arg(text->textToFrameDistBottom(), 0, 'f', 1).arg(text->frameOverflows() ? QStringLiteral("YES") : QStringLiteral("no")));
		// lines per column from the layout box tree: root -> columns -> lines
		text->invalidateLayout();
		text->layout();
		const Box* root = text->textLayout.box();
		if (root)
		{
			int col = 0;
			for (const Box* cb : root->boxes())
			{
				QList<const Box*> lines;
				std::function<void(const Box*)> walk = [&](const Box* b) {
					if (b->type() == Box::T_Line)
						lines << b;
					else
						for (const Box* c : b->boxes())
							walk(c);
				};
				walk(cb);
				if (lines.isEmpty())
				{
					line(QStringLiteral("NEWSDIAG    column %1: no lines").arg(col + 1));
				}
				else
				{
					const Box* f = lines.first();
					const Box* l = lines.last();
					line(QStringLiteral("NEWSDIAG    column %1: %2 lines, first y %3 (asc %4) last y %5 (desc %6) -> last line bottom %7 mm below the frame top, %8 mm of the frame left under it")
					         .arg(col + 1).arg(lines.size()).arg(mm(cb->y() + f->y())).arg(mm(f->ascent())).arg(mm(cb->y() + l->y())).arg(mm(l->descent()))
					         .arg(mm(cb->y() + l->y() - l->descent())).arg(mm(text->height() - (cb->y() + l->y() - l->descent()))));
				}
				++col;
			}
		}
		if (photo)
		{
			const QRectF pr(photo->xPos() - off.x(), photo->yPos() - off.y(), photo->width(), photo->height());
			double fitH = 0;
			if (photo->pixm.width() > 0)
				fitH = photo->width() * photo->pixm.height() / double(photo->pixm.width());
			const double shownW = photo->pixm.width() * photo->imageXScale();
			const double shownH = photo->pixm.height() * photo->imageYScale();
			line(QStringLiteral("NEWSDIAG    photo at %1,%2 %3x%4 mm; picture %5x%6 px; height at frame width %7 mm; band (frame - picture) %8 mm; shown %9x%10 mm offset %11,%12; free scale %13 keep ratio %14; wrap %15 offsets %16")
			         .arg(mm(pr.x()), mm(pr.y()), mm(pr.width()), mm(pr.height())).arg(photo->pixm.width()).arg(photo->pixm.height()).arg(mm(fitH)).arg(mm(photo->height() - fitH))
			         .arg(mm(shownW), mm(shownH), mm(photo->imageXOffset() * photo->imageXScale()), mm(photo->imageYOffset() * photo->imageYScale()))
			         .arg(photo->ScaleType ? 1 : 0).arg(photo->AspectRatio ? 1 : 0).arg((int) photo->textFlowMode()).arg(mm(photo->wrapOffsetTop())));
		}
		if (caption)
		{
			const QRectF cr(caption->xPos() - off.x(), caption->yPos() - off.y(), caption->width(), caption->height());
			int nLines = 0;
			double lastBottom = 0;
			caption->invalidateLayout();
			caption->layout();
			if (const Box* root2 = caption->textLayout.box())
			{
				std::function<void(const Box*, double)> walk = [&](const Box* b, double y0) {
					if (b->type() == Box::T_Line)
					{
						++nLines;
						lastBottom = qMax(lastBottom, y0 + b->y() - b->descent());
					}
					else
						for (const Box* c : b->boxes())
							walk(c, y0 + (b == root2 ? 0 : b->y()));
				};
				walk(root2, 0);
			}
			QString t = caption->itemText.plainText();
			int trailing = 0;
			while (!t.isEmpty() && (t.endsWith(QLatin1Char('\r')) || t.endsWith(QLatin1Char('\n')) || t.endsWith(QChar(0x2029)) || t.endsWith(QLatin1Char(' '))))
			{
				t.chop(1);
				++trailing;
			}
			line(QStringLiteral("NEWSDIAG    caption at %1,%2 %3x%4 mm; %5 line(s), last line bottom %6 mm below its top (%7 mm empty under it); %8 chars, %9 trailing blank char(s); dist T/B %10/%11 pt; wrap %12 offsets %13 mm; photo -> caption %14 mm")
			         .arg(mm(cr.x()), mm(cr.y()), mm(cr.width()), mm(cr.height())).arg(nLines).arg(mm(lastBottom)).arg(mm(caption->height() - lastBottom)).arg(caption->itemText.length()).arg(trailing)
			         .arg(caption->textToFrameDistTop(), 0, 'f', 1).arg(caption->textToFrameDistBottom(), 0, 'f', 1).arg((int) caption->textFlowMode()).arg(mm(caption->wrapOffsetBottom()))
			         .arg(photo ? mm(cr.top() - (photo->yPos() - off.y() + photo->height())) : QStringLiteral("-")));
			// what comes next under the caption: the body's column 2 first line below it, the next item
			double nextTop = -1;
			QString nextWhat;
			const double capBottom = cr.bottom();
			for (PageItem* o : onPage)
			{
				if (it.value().contains(o))
					continue;
				const QRectF orr(o->xPos() - off.x(), o->yPos() - off.y(), o->width(), o->height());
				if (orr.right() > cr.left() + 1 && orr.left() < cr.right() - 1 && orr.top() >= capBottom - 0.5 && (nextTop < 0 || orr.top() < nextTop))
				{
					nextTop = orr.top();
					nextWhat = o->itemName();
				}
			}
			// the body text under the caption: the first line of the text column(s) the caption covers, below it
			double firstUnder = -1;
			if (const Box* root3 = text->textLayout.box())
				for (const Box* cb : root3->boxes())
				{
					const double colL = tr.left() + cb->x(), colR = colL + cb->width();
					if (colR <= cr.left() + 1 || colL >= cr.right() - 1)
						continue;
					std::function<void(const Box*)> walk = [&](const Box* b) {
						if (b->type() == Box::T_Line)
						{
							const double top = tr.top() + cb->y() + b->y();
							if (top >= capBottom - 0.5 && (firstUnder < 0 || top < firstUnder))
								firstUnder = top;
						}
						else
							for (const Box* c : b->boxes())
								walk(c);
					};
					walk(cb);
				}
			line(QStringLiteral("NEWSDIAG    SPACE after caption: -> body text under it %1 mm (caption -> text gap %2 + line top); -> text frame bottom %3 mm; -> next frame below %4 mm (%5)")
			         .arg(firstUnder < 0 ? QStringLiteral("none") : mm(firstUnder - capBottom), mm(caption->wrapOffsetBottom()), mm(tr.bottom() - capBottom)).arg(nextTop < 0 ? QStringLiteral("-") : mm(nextTop - capBottom), nextWhat));
		}
	}
}

void SuneerNewsPanel::runLayoutSelfTest(const QString& outPath)
{
	using namespace SuneerLayout;
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		fprintf(stderr, "LAYOUT-SELFTEST cannot write %s\n", qPrintable(outPath));
		QTimer::singleShot(0, qApp, []() { qApp->exit(3); });
		return;
	}
	QTextStream ts(&out);
	auto line = [&](const QString& l) { ts << l << '\n'; ts.flush(); fprintf(stderr, "LAYOUT-SELFTEST %s\n", qPrintable(l)); };
	if (!m_doc || !m_doc->currentPage())
	{
		line(QStringLiteral("FAIL no document"));
		QTimer::singleShot(0, qApp, []() { qApp->exit(2); });
		return;
	}
	const QString file = m_doc->documentFileName();
	const int pageIdx = m_doc->currentPageNumber();
	line(QStringLiteral("document %1 page %2, %3 item(s)").arg(file).arg(pageIdx + 1).arg(m_doc->Items->count()));
	// SUNEER_LAYOUT_SELFTEST_OUTDIR: before.png, <model>.png after every Apply, and a GAPS line per model
	const QString outDir = qEnvironmentVariable("SUNEER_LAYOUT_SELFTEST_OUTDIR");
	const QString titleStyle = panelTitleStyle();
	auto masters = [&]() {
		QList<PageIn::Kept> ms;
		const PageIn p = collectPage(m_doc, pageIdx, QList<StoryIn>(), StyleNames::fromSettings(titleStyle), layoutModelKey(), nullptr);
		for (const PageIn::Kept& k : p.always)
			if (k.master)
				ms << k;
		return ms;
	};
	auto gapLine = [&](const QString& id, double gapV, bool packed) {
		const PageIn p = collectPage(m_doc, pageIdx, QList<StoryIn>(), StyleNames::fromSettings(titleStyle), layoutModelKey(), nullptr);
		QList<QRectF> rects;
		QStringList titles;
		const ScPage* pg = m_doc->Pages->at(pageIdx);
		for (const StoryIn& s : p.stories)
		{
			if (!s.placed || s.placedRect.left() >= p.pageW || s.placedRect.right() <= 0)
				continue;
			// the frames' geometry (what the packing places), not the visual bounds with the frame borders
			QList<PageItem*> its;
			for (const QPointer<PageItem>& it : s.items)
				if (it)
					its << it.data();
			rects << itemsRect(its).translated(-pg->xOffset(), -pg->yOffset());
			titles << s.title.left(24).simplified();
		}
		int over = 0, worst = -1;
		const double g = maxGapAbove(rects, gapV, &over, &worst);
		if (worst >= 0)
		{
			const QRectF& w = rects[worst];
			line(QStringLiteral("GAP-DETAIL %1: '%2' at %3,%4 %5x%6 mm; above it:").arg(id, titles[worst]).arg(w.left() / kMm, 0, 'f', 1).arg(w.top() / kMm, 0, 'f', 1).arg(w.width() / kMm, 0, 'f', 1).arg(w.height() / kMm, 0, 'f', 1));
			for (int j = 0; j < rects.size(); ++j)
			{
				const QRectF& a = rects[j];
				if (j != worst && a.right() > w.left() + 0.5 && a.left() < w.right() - 0.5 && a.bottom() <= w.top() + 0.5)
					line(QStringLiteral("    '%1' at %2,%3 %4x%5 mm (gap %6 mm)").arg(titles[j]).arg(a.left() / kMm, 0, 'f', 1).arg(a.top() / kMm, 0, 'f', 1).arg(a.width() / kMm, 0, 'f', 1).arg(a.height() / kMm, 0, 'f', 1).arg((w.top() - a.bottom()) / kMm, 0, 'f', 1));
			}
			// what else lies above it (fixed modules, kept frames): the gap may be filled by something that is not a story
			for (const Module& m : p.modules)
				if (m.rect.right() > w.left() + 0.5 && m.rect.left() < w.right() - 0.5)
					line(QStringLiteral("    module '%1' kind %2 fixed %3 (%4) rigid %5 at %6,%7 %8x%9 mm").arg(m.title.left(24).simplified(), m.kind).arg(m.fixed ? 1 : 0).arg(m.reason).arg(m.rigid ? 1 : 0)
					         .arg(m.rect.left() / kMm, 0, 'f', 1).arg(m.rect.top() / kMm, 0, 'f', 1).arg(m.rect.width() / kMm, 0, 'f', 1).arg(m.rect.height() / kMm, 0, 'f', 1));
		}
		line(QStringLiteral("GAPS %1: %2 module(s) on the page, max gap above a module %3 mm (%4), %5 gap(s) over the model gap, model gap %6 mm, obituary packing %7, obituary page %8")
		         .arg(id).arg(rects.size()).arg(g / kMm, 0, 'f', 2).arg(worst >= 0 ? titles[worst] : QStringLiteral("-")).arg(over).arg(gapV / kMm, 0, 'f', 2)
		         .arg(packed ? QStringLiteral("yes") : QStringLiteral("no")).arg(isObitPage(p) ? QStringLiteral("yes") : QStringLiteral("no")));
	};
	if (!outDir.isEmpty())
	{
		QDir().mkpath(outDir);
		renderDocPage(m_doc, pageIdx, masters(), 1000).save(outDir + QStringLiteral("/before.png"));
		gapLine(QStringLiteral("before"), 0.0, false);
	}
	int failures = 0;
	// SUNEER_LAYOUT_SELFTEST_NEWSDIAG=1: the geometry inside every obituary 2-column news item (text columns,
	// photo band, caption, gaps), then exit
	const QStringList seq = qEnvironmentVariable("SUNEER_LAYOUT_SELFTEST_SEQ").split(',', Qt::SkipEmptyParts);
	const bool newsDiag = qEnvironmentVariableIsSet("SUNEER_LAYOUT_SELFTEST_NEWSDIAG");
	if (newsDiag)
	{
		line(QStringLiteral("NEWSDIAG ---- before"));
		newsItemDiagnostics(pageIdx, line);
		if (seq.isEmpty())
		{
			line(QStringLiteral("RESULT PASS"));
			out.close();
			m_doc->setModified(false);
			QTimer::singleShot(500, qApp, []() { qApp->exit(0); });
			return;
		}
	}
	// SUNEER_LAYOUT_SELFTEST_SEQ=id1,id2,...: the models applied one after the other on the real page
	// (no undo), a PNG and a GAPS line after each, plus overlaps and modules on the pasteboard
	for (const QString& id : seq)
	{
		if (!SuneerLayoutModels::exists(id))
		{
			line(QStringLiteral("FAIL SEQ %1: no such model").arg(id));
			++failures;
			continue;
		}
		ArrangeOut ao;
		m_arrangeOut = &ao;
		const QString report = arrangePage(id, (int) Keep::PlacedFrames, true, false);
		m_arrangeOut = nullptr;
		qApp->processEvents();
		const PageIn p = collectPage(m_doc, pageIdx, QList<StoryIn>(), StyleNames::fromSettings(titleStyle), layoutModelKey(), nullptr);
		int onPage = 0, offPage = 0, overlaps = 0;
		QList<QRectF> rects;
		for (const StoryIn& st : p.stories)
			if (st.placed)
			{
				if (st.placedRect.left() >= p.pageW || st.placedRect.right() <= 0)
					++offPage;
				else
				{
					++onPage;
					QList<PageItem*> its;
					for (const QPointer<PageItem>& it : st.items)
						if (it)
							its << it.data();
					rects << itemsRect(its).translated(-m_doc->Pages->at(pageIdx)->xOffset(), -m_doc->Pages->at(pageIdx)->yOffset());
				}
			}
		for (int i = 0; i < rects.size(); ++i)
			for (int j = i + 1; j < rects.size(); ++j)
				if (rects[i].adjusted(0.5, 0.5, -0.5, -0.5).intersects(rects[j]))
					++overlaps;
		const bool ok = !ao.refused && offPage == 0 && overlaps == 0;
		if (!ok)
			++failures;
		line(QStringLiteral("%1 SEQ %2: %3 module(s) on the page, %4 on the pasteboard, %5 overlap(s), %6 obits + %7 news, %8 not fitting%9")
		         .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), id).arg(onPage).arg(offPage).arg(overlaps).arg(ao.obits).arg(ao.news).arg(ao.notFit.size())
		         .arg(ao.refused ? QStringLiteral(" REFUSED: ") + report.simplified() : QString()));
		if (!outDir.isEmpty())
		{
			static int n = 0;
			renderDocPage(m_doc, pageIdx, masters(), 1000).save(outDir + QStringLiteral("/seq%1-%2.png").arg(++n).arg(id));
			gapLine(QStringLiteral("seq-") + id, ao.gapV > 0 ? ao.gapV : ao.model.gapVMm * kMm, ao.obitPacked);
		}
		if (newsDiag)
		{
			line(QStringLiteral("NEWSDIAG ---- after %1").arg(id));
			newsItemDiagnostics(pageIdx, line);
		}
	}
	if (!seq.isEmpty())
	{
		line(failures == 0 ? QStringLiteral("RESULT PASS") : QStringLiteral("RESULT FAIL %1").arg(failures));
		out.close();
		m_doc->setModified(false);
		QTimer::singleShot(500, qApp, [failures]() { qApp->exit(failures == 0 ? 0 : 1); });
		return;
	}
	for (const SuneerLayoutModel& m : SuneerLayoutModels::all())
	{
		QElapsedTimer clock;
		clock.start();
		const QList<PageItem*> items = pageTopItems(m_doc, pageIdx);
		const QStringList before = resultSignature(m_doc, pageIdx, items);
		const int countBefore = m_doc->Items->count();
		// 1. the Gallery card (scratch copy)
		const SuneerLayoutGallery::Preview card = previewArrange(m.id, (int) Keep::PlacedFrames, false, QList<PageItem*>(), 200);
		const bool cardUnchanged = resultSignature(m_doc, pageIdx, items) == before;      // the card must not touch the page
		// 2. Apply on the real page (what the Gallery's Apply does)
		ArrangeOut ao;
		m_arrangeOut = &ao;
		const QString report = arrangePage(m.id, (int) Keep::PlacedFrames, true, false);
		m_arrangeOut = nullptr;
		qApp->processEvents();
		if (!outDir.isEmpty())
		{
			renderDocPage(m_doc, pageIdx, masters(), 1000).save(outDir + QStringLiteral("/") + m.id + QStringLiteral(".png"));
			gapLine(m.id, ao.gapV > 0 ? ao.gapV : ao.model.gapVMm * kMm, ao.obitPacked);
		}
		const QStringList after = resultSignature(m_doc, pageIdx, items);
		const bool refused = report.contains(QStringLiteral("was not applied"));
		const int cardDiff = card.ok ? signatureDiff(card.signature, after) : -1;
		// 3. Undo
		if (!refused && after != before)
		{
			UndoManager::instance()->undo(1);
			qApp->processEvents();
		}
		const QStringList undone = resultSignature(m_doc, pageIdx, items);
		const int undoDiff = signatureDiff(before, undone);
		const bool undoOk = undoDiff == 0 && m_doc->Items->count() == countBefore;
		bool ok = cardUnchanged && undoOk;
		QString verdict;
		if (refused)
		{
			ok = ok && after == before && !card.ok;
			verdict = QStringLiteral("refused with a message (page unchanged: %1)").arg(after == before ? QStringLiteral("yes") : QStringLiteral("NO"));
		}
		else
		{
			ok = ok && card.ok && cardDiff == 0;
			verdict = QStringLiteral("card %1, page vs card %2 differ, %3 of %4 entries changed by Apply")
			              .arg(card.ok ? card.fitLine : QStringLiteral("FAILED: ") + card.error).arg(cardDiff).arg(signatureDiff(before, after)).arg(before.size());
		}
		if (!ok)
			++failures;
		line(QStringLiteral("%1 %2: %3; undo %4 (%5 differ, items %6 -> %7); page untouched by the card: %8; %9 ms")
		         .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), m.id, verdict, undoOk ? QStringLiteral("ok") : QStringLiteral("NOT RESTORED"))
		         .arg(undoDiff).arg(countBefore).arg(m_doc->Items->count()).arg(cardUnchanged ? QStringLiteral("yes") : QStringLiteral("NO")).arg(clock.elapsed()));
		if (refused)
			line(QStringLiteral("    message: ") + report.simplified());
		if (card.ok && cardDiff != 0)
			for (int i = 0; i < qMax(card.signature.size(), after.size()); ++i)
				if (i >= card.signature.size() || i >= after.size() || card.signature[i] != after[i])
					line(QStringLiteral("    card %1  page %2  (%3)").arg(i < card.signature.size() ? card.signature[i] : QStringLiteral("-"), i < after.size() ? after[i] : QStringLiteral("-"),
					     i < items.size() && items[i] ? items[i]->itemName() + QLatin1Char(' ') + QString::number((int) items[i]->itemType()) : QString()));
		if (!undoOk)
			for (int i = 0; i < qMax(before.size(), undone.size()); ++i)
				if (i >= before.size() || i >= undone.size() || before[i] != undone[i])
					line(QStringLiteral("    undo: before %1  now %2  (%3 %4 %5)").arg(i < before.size() ? before[i] : QStringLiteral("-"), i < undone.size() ? undone[i] : QStringLiteral("-"),
					     i < items.size() && items[i] ? items[i]->itemName() : QString(), i < items.size() && items[i] ? attr(items[i], "news.role") + QLatin1Char('/') + attr(items[i], "layout.role") + QLatin1Char('/') + attr(items[i], "layout.kind") : QString(),
					     i < items.size() && items[i] && items[i]->isTextFrame() ? QStringLiteral("flop %1 top %2").arg((int) items[i]->firstLineOffset()).arg(items[i]->textToFrameDistTop(), 0, 'f', 1) : QString()));
	}
	line(failures == 0 ? QStringLiteral("RESULT PASS") : QStringLiteral("RESULT FAIL %1").arg(failures));
	out.close();
	m_doc->setModified(false);
	QTimer::singleShot(500, qApp, [failures]() { qApp->exit(failures == 0 ? 0 : 1); });
}
