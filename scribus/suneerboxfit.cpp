/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerboxfit.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_group.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneertextclean.h"
#include "suneerweld.h"
#include "text/boxes.h"
#include "text/specialchars.h"
#include "ui/suneercontrolbar.h"
#include "undomanager.h"

namespace {

const QString kFitAttr    = QStringLiteral("SuneerBoxFit");          // one fitted frame: inner space (pt)
const QString kBoxAttr    = QStringLiteral("SuneerStoryBox");        // the story box: its id
const QString kMemberAttr = QStringLiteral("SuneerStoryBoxMember");  // a frame inside a story box: the id
const QString kInsetAttr  = QStringLiteral("SuneerStoryBoxInset");   // the story box: inner space (pt)
const QString kBoxFitAttr = QStringLiteral("SuneerStoryBoxFit");     // the story box: "1" = fit to the text

QString attrOf(const PageItem* item, const QString& name)
{
	return SuneerWeld::attr(item, name);
}

void setAttrOf(PageItem* item, const QString& name, const QString& value)
{
	SuneerWeld::setAttr(item, name, value);
}

void removeAttr(PageItem* item, const QString& name)
{
	ObjAttrVector* all = item ? item->getObjectAttributes() : nullptr;
	if (!all)
		return;
	for (int i = all->count() - 1; i >= 0; --i)
		if (all->at(i).name == name)
			all->remove(i);
}

// every page item of the document, group children included
QList<PageItem*> allItems(ScribusDoc* doc)
{
	QList<PageItem*> out;
	QList<PageItem*> todo = *doc->Items;
	while (!todo.isEmpty())
	{
		PageItem* it = todo.takeFirst();
		if (!it)
			continue;
		out << it;
		if (it->isGroup())
			todo << it->asGroupFrame()->groupItemList;
	}
	return out;
}

QList<PageItem*> leavesOf(PageItem* group)
{
	QList<PageItem*> out;
	for (PageItem* c : group->asGroupFrame()->groupItemList)
	{
		if (!c)
			continue;
		if (c->isGroup())
			out << leavesOf(c);
		else
			out << c;
	}
	return out;
}

PageItem* topGroup(PageItem* item)
{
	PageItem* top = nullptr;
	for (PageItem* p = item ? item->Parent : nullptr; p; p = p->Parent)
		if (p->isGroup())
			top = p;
	return top;
}

bool isStoryBox(const PageItem* item)
{
	return !attrOf(item, kBoxAttr).isEmpty();
}

// the item's rectangle in document coordinates (group children through their group's transform)
QRectF docRect(const PageItem* it)
{
	return const_cast<PageItem*>(it)->getTransform().mapRect(QRectF(0.0, 0.0, it->width(), it->height()));
}

QRectF toDoc(const PageItem* it, const QRectF& local)
{
	return const_cast<PageItem*>(it)->getTransform().mapRect(local);
}

bool frameFits(PageItem_TextFrame* tf, double height, double bottomDist)
{
	tf->setHeight(height);
	tf->updateClip();   // the lines follow the frame's shape, not only its height
	tf->setTextToFrameDistBottom(bottomDist);
	tf->invalidateLayout();
	tf->layout();
	return !tf->frameOverflows();
}

} // namespace

QRectF SuneerBoxFit::inkRect(PageItem_TextFrame* tf)
{
	QRectF r;
	if (!tf || tf->textLayout.lines() == 0)
		return r;
	const Box* frameBox = tf->textLayout.box();
	for (const Box* column : frameBox->boxes())
		for (const Box* b : column->boxes())
		{
			const LineBox* line = dynamic_cast<const LineBox*>(b);
			if (!line)
				continue;
			QRectF t = line->textRect();
			if (t.isEmpty())
				continue;
			t.translate(frameBox->x() + column->x() + line->x(), frameBox->y() + column->y() + line->y());
			r = r.isNull() ? t : r.united(t);
		}
	return r;
}

QList<PageItem*> SuneerBoxFit::storyMembers(PageItem* item)
{
	QList<PageItem*> out;
	if (!item)
		return out;
	auto add = [&out](PageItem* it) {
		if (it && !it->isGroup() && !isStoryBox(it) && !out.contains(it))
			out << it;
	};
	if (item->isGroup())
	{
		for (PageItem* c : leavesOf(item))
			add(c);
		return out;
	}
	if (PageItem* g = topGroup(item))
	{
		for (PageItem* c : leavesOf(g))
			add(c);
		return out;
	}
	QList<PageItem*> todo = SuneerWeld::cluster(item);
	while (!todo.isEmpty())
	{
		PageItem* it = todo.takeFirst();
		if (!it || out.contains(it) || isStoryBox(it))
			continue;
		add(it);
		if (it->isTextFrame())
		{
			for (PageItem* c = it->firstInChain(); c; c = c->nextInChain())
				if (!out.contains(c))
					todo << SuneerWeld::cluster(c);
		}
	}
	return out;
}

bool SuneerBoxFit::fitFrame(PageItem_TextFrame* tf, double inset)
{
	if (!tf || tf->nextInChain() || tf->prevInChain() || tf->itemText.length() == 0 || tf->sizeLocked())
		return false;
	SuneerTextClean::cleanStory(tf, true);
	tf->setTextToFrameDistLeft(inset);
	tf->setTextToFrameDistRight(inset);
	tf->setTextToFrameDistTop(inset);
	// The space above the letters is the top inset: the first line starts at its real glyph height.
	tf->setFirstLineOffset(FLOPRealGlyphHeight);

	// The bottom: the one ink fit (PageItem_TextFrame::suneerFitToInk) - the space under the last
	// letters equals the space above the first ones (2026-10-31: was "ink bottom + inset", which left the
	// two unequal when the first line did not sit exactly at the inset, e.g. English 18 pt: 2.31 / 1.93 mm).
	// The bottom distance starts at the inset and is lowered only as much as the font's descent needs.
	tf->setTextToFrameDistBottom(inset);
	PageItem_TextFrame::SuneerFitOptions o;
	const bool fitted = tf->suneerFitToInk(o);
	tf->checkChanges(true);
	tf->invalidateLayout();
	tf->layout();
	tf->update();
	return fitted;
}

QRectF SuneerBoxFit::storyContentRect(const QList<PageItem*>& members)
{
	QRectF r;
	for (PageItem* it : members)
	{
		QRectF part;
		if (it->isTextFrame())
		{
			PageItem_TextFrame* tf = it->asTextFrame();
			tf->layout();
			const QRectF ink = inkRect(tf);
			if (ink.isEmpty())
				continue;
			part = toDoc(it, ink);
		}
		else
			part = docRect(it);
		if (part.isEmpty() && !it->isLine())
			continue;
		r = r.isNull() ? part : r.united(part);
	}
	return r;
}

PageItem* SuneerBoxFit::storyBoxOf(ScribusDoc* doc, const QList<PageItem*>& members)
{
	QString id;
	for (PageItem* m : members)
		if (!(id = attrOf(m, kMemberAttr)).isEmpty())
			break;
	if (id.isEmpty())
		return nullptr;
	for (PageItem* it : allItems(doc))
		if (attrOf(it, kBoxAttr) == id)
			return it;
	return nullptr;
}

static void placeBox(ScribusDoc* doc, PageItem* box, const QRectF& want)
{
	// Moved on its own (doc->moveItem would drag the welded story along).
	const QRectF now = docRect(box);
	if (box->isGroupChild())
	{
		box->gXpos += want.x() - now.x();
		box->gYpos += want.y() - now.y();
	}
	box->moveBy(want.x() - now.x(), want.y() - now.y());
	box->setWidthHeight(want.width(), want.height());
	box->updateClip();
	box->checkChanges(true);
	if (PageItem* g = topGroup(box))
		doc->resizeGroupToContents(g);
	doc->setRedrawBounding(box);
	box->update();
}

bool SuneerBoxFit::refitStoryBox(ScribusDoc* doc, PageItem* box)
{
	const QString id = attrOf(box, kBoxAttr);
	if (id.isEmpty())
		return false;
	QList<PageItem*> members;
	for (PageItem* it : allItems(doc))
		if (attrOf(it, kMemberAttr) == id && !it->isGroup())
			members << it;
	if (members.isEmpty())
		return false;
	const double inset = attrOf(box, kInsetAttr).toDouble();
	const QRectF content = storyContentRect(members);
	if (content.isEmpty())
		return false;
	const QRectF want = content.adjusted(-inset, -inset, inset, inset);
	const QRectF now = docRect(box);
	if (qAbs(want.x() - now.x()) < 0.01 && qAbs(want.y() - now.y()) < 0.01
	    && qAbs(want.width() - now.width()) < 0.01 && qAbs(want.height() - now.height()) < 0.01)
		return false;
	placeBox(doc, box, want);
	return true;
}

void SuneerBoxFit::apply(ScribusDoc* doc, const QList<PageItem*>& items, const SuneerBorderSettings& s)
{
	QList<PageItem*> done;
	const bool anySide = s.top || s.bottom || s.left || s.right;
	for (PageItem* item : items)
	{
		if (!item || done.contains(item))
			continue;
		const QList<PageItem*> members = s.wholeStory ? storyMembers(item) : QList<PageItem*>{ item };
		if (members.size() > 1)
		{
			done << members;
			PageItem* box = storyBoxOf(doc, members);
			// a frame of the story boxed on its own before: that border goes, the story box replaces it
			for (PageItem* m : members)
				if (m->isTextFrame() && !attrOf(m, kFitAttr).isEmpty())
				{
					m->setSideBorders(false, false, false, false);
					m->setLineColor(CommonStrings::None);
					removeAttr(m, kFitAttr);
				}
			if (!anySide)
			{
				if (box)
				{
					Selection gone(doc, false);
					gone.addItem(box);
					doc->itemSelection_DeleteItem(&gone, true);
				}
				for (PageItem* m : members)
					removeAttr(m, kMemberAttr);
				continue;
			}
			const QRectF content = storyContentRect(members);
			if (content.isEmpty())
				continue;
			const QRectF want = content.adjusted(-s.inset, -s.inset, s.inset, s.inset);
			if (!box)
			{
				const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
				const int z = doc->itemAdd(PageItem::Polygon, PageItem::Rectangle, want.x(), want.y(), want.width(), want.height(),
				                           0.5, CommonStrings::None, QStringLiteral("Black"));
				box = doc->Items->at(z);
				box->setItemName(QStringLiteral("StoryBox_%1").arg(id.left(8)));
				box->setTextFlowMode(PageItem::TextFlowDisabled);
				box->setFillColor(CommonStrings::None);
				setAttrOf(box, kBoxAttr, id);
				for (PageItem* m : members)
					setAttrOf(m, kMemberAttr, id);
				if (PageItem* g = topGroup(members.first()))
				{
					doc->addToGroup(g, box);
					auto& list = g->asGroupFrame()->groupItemList;
					list.removeAll(box);
					list.prepend(box);          // under the story's frames
					box->setLayer(g->m_layerID);
					doc->resizeGroupToContents(g);   // the box reaches past the frames: the group's clip grows
				}
				else
				{
					// under the lowest frame of the story, so clicks still reach the frames
					int lowest = doc->Items->indexOf(box);
					for (PageItem* m : members)
					{
						const int i = doc->Items->indexOf(m);
						if (i >= 0 && i < lowest)
							lowest = i;
					}
					doc->Items->move(doc->Items->indexOf(box), lowest);
					PageItem* anchor = nullptr;
					for (PageItem* m : members)
						if (m->isTextFrame() && !m->weldList.isEmpty())
						{
							anchor = m;
							break;
						}
					if (anchor)
						box->weldTo(anchor);   // moves with the welded story
				}
			}
			setAttrOf(box, kInsetAttr, QString::number(s.inset, 'f', 3));
			setAttrOf(box, kBoxFitAttr, s.fit ? QStringLiteral("1") : QStringLiteral("0"));
			SuneerControlBar::applyBorderToItem(box, s, doc);
			placeBox(doc, box, want);
			continue;
		}
		done << item;
		if (!item->isTextFrame())
			continue;
		SuneerControlBar::applyBorderToItem(item, s, doc);
		if (s.fit && anySide && fitFrame(item->asTextFrame(), s.inset))
			setAttrOf(item, kFitAttr, QString::number(s.inset, 'f', 3));
		else
			removeAttr(item, kFitAttr);
	}
}

void SuneerBoxFit::noteTextEdit(PageItem* item)
{
	if (!item || !item->isTextFrame())
		return;
	ScribusDoc* doc = item->doc();
	QPointer<PageItem> it(item);
	const bool fitted = !attrOf(item, kFitAttr).isEmpty();
	const QString boxId = attrOf(item, kMemberAttr);
	if (!fitted && boxId.isEmpty())
		return;
	QTimer::singleShot(0, qApp, [it, doc, fitted, boxId]() {
		if (!it || !doc)
			return;
		UndoTransaction tx;
		if (UndoManager::undoEnabled())
			tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IResize, QObject::tr("Fit box to text"), QString(), Um::IResize);
		bool changed = false;
		if (fitted)
		{
			const double h = it->height();
			changed = fitFrame(it->asTextFrame(), attrOf(it, kFitAttr).toDouble()) && qAbs(it->height() - h) > 0.001;
		}
		else
		{
			for (PageItem* b : allItems(doc))
				if (attrOf(b, kBoxAttr) == boxId && attrOf(b, kBoxFitAttr) == QLatin1String("1"))
					changed = refitStoryBox(doc, b) || changed;
		}
		if (tx)
		{
			if (changed)
				tx.commit();
			else
				tx.cancel();
		}
		if (changed)
		{
			doc->changed();
			doc->regionsChanged()->update(QRectF());
		}
	});
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

void SuneerBoxFit::runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc)
{
	const QString prefix = qEnvironmentVariable("SUNEER_BOXFIT_SELFTEST");
	if (prefix.isEmpty() || !doc)
		return;
	QStringList lines;
	int fails = 0;
	auto check = [&](const QString& what, bool ok) {
		lines << (ok ? QStringLiteral("PASS ") : QStringLiteral("FAIL ")) + what;
		fails += ok ? 0 : 1;
	};
	auto byName = [doc](const QString& name) -> PageItem* {
		for (PageItem* it : allItems(doc))
			if (it->itemName() == name)
				return it;
		return nullptr;
	};
	auto mm = [](double pt) { return pt / 2.8346; };
	SuneerBorderSettings s;
	s.inset = 2.0 * 2.8346;
	s.fit = true;
	s.wholeStory = true;
	auto applyOne = [&](PageItem* it) {
		UndoTransaction tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, QStringLiteral("Frame border"), QString(), Um::IBorder);
		apply(doc, { it }, s);
		tx.commit();
	};
	ScPage* page = doc->DocPages.at(0);
	QStringList cases;   // name x y w h (mm, page) of each box: the script measures the ink inside
	auto addCase = [&](const QString& name, PageItem* boxed) {
		const QRectF r = docRect(boxed);
		cases << QStringLiteral("%1 %2 %3 %4 %5").arg(name).arg(mm(r.x() - page->xOffset()), 0, 'f', 3).arg(mm(r.y() - page->yOffset()), 0, 'f', 3)
		                                          .arg(mm(r.width()), 0, 'f', 3).arg(mm(r.height()), 0, 'f', 3);
	};

	// 1. single frames (each too tall, with empty paragraphs at the end)
	int singles = 0;
	for (PageItem* it : allItems(doc))
	{
		if (!it->itemName().startsWith(QLatin1String("S_")) || !it->isTextFrame())
			continue;
		const double h0 = it->height();
		applyOne(it);
		++singles;
		const bool over = it->asTextFrame()->frameOverflows();
		const int len = it->itemText.length();
		const QChar last = len > 0 ? it->itemText.text(len - 1) : QChar();
		check(QStringLiteral("%1: fitted %2 -> %3 mm, no overflow, no empty paragraph at the end, first line offset real glyph height")
		          .arg(it->itemName()).arg(mm(h0), 0, 'f', 2).arg(mm(it->height()), 0, 'f', 2),
		      !over && it->height() < h0 && last != SpecialChars::PARSEP && last != SpecialChars::LINEBREAK
		      && it->firstLineOffset() == FLOPRealGlyphHeight && it->TopLine && it->BottomLine && it->LeftLine && it->RightLine);
		addCase(it->itemName(), it);
	}
	check(QStringLiteral("%1 single frames boxed").arg(singles), singles >= 6);

	// 2. stories: welded, linked (chain), grouped -> one story box each
	PageItem* stHead = byName("ST_head");
	PageItem* stBody = byName("ST_body");
	if (stHead && stBody)
		SuneerWeld::weldStory(doc, { stHead, stBody }, stBody);
	QMap<QString, QPointF> before;   // every frame's position before the story boxes
	for (PageItem* it : allItems(doc))
		before[it->itemName()] = QPointF(it->xPos(), it->yPos());
	struct StoryCase { QString name; PageItem* pick; };
	PageItem* grItem = nullptr;
	for (PageItem* it : *doc->Items)
		if (it->isGroup())
			grItem = it;
	const QList<StoryCase> stories { { "STORY_welded", stBody }, { "STORY_linked", byName("CH_1") }, { "STORY_group", grItem } };
	for (const StoryCase& sc : stories)
	{
		if (!sc.pick)
		{
			check(sc.name + QStringLiteral(": test frame missing"), false);
			continue;
		}
		const QList<PageItem*> members = storyMembers(sc.pick);
		applyOne(sc.pick);
		PageItem* box = storyBoxOf(doc, members);
		const QRectF content = storyContentRect(members);
		bool even = false;
		if (box)
		{
			const QRectF b = docRect(box);
			even = qAbs(content.top() - b.top() - s.inset) < 0.05 && qAbs(b.bottom() - content.bottom() - s.inset) < 0.05
			    && qAbs(content.left() - b.left() - s.inset) < 0.05 && qAbs(b.right() - content.right() - s.inset) < 0.05;
			addCase(sc.name, box);
		}
		check(QStringLiteral("%1: %2 frames, one story box %3 x %4 mm, inner space %5 mm on all sides")
		          .arg(sc.name).arg(members.size()).arg(box ? mm(box->width()) : 0, 0, 'f', 2).arg(box ? mm(box->height()) : 0, 0, 'f', 2).arg(mm(s.inset), 0, 'f', 1),
		      box && members.size() >= 2 && even);
	}
	bool unmoved = true;
	QStringList moved;
	for (PageItem* it : allItems(doc))
		if (!it->isGroup() && before.contains(it->itemName()) && (qAbs(before[it->itemName()].x() - it->xPos()) > 0.01 || qAbs(before[it->itemName()].y() - it->yPos()) > 0.01))
		{
			unmoved = false;
			moved << it->itemName();
		}
	check(QStringLiteral("story boxes moved no frame (%1)").arg(moved.join(", ")), unmoved);

	// 3. text edited -> fitted again on leaving edit mode, one undo step, nothing else moves
	const QString more = QStringLiteral(" കൂടുതൽ വിവരങ്ങൾ പിന്നാലെ അറിയിക്കുമെന്ന് അധികൃതർ വ്യക്തമാക്കി.");
	if (PageItem* e = byName("S_10_auto_left"))
	{
		QMap<QString, QPointF> pos;
		for (PageItem* it : allItems(doc))
			pos[it->itemName()] = QPointF(it->xPos(), it->yPos());
		const double h0 = e->height();
		e->itemText.insertChars(e->itemText.length(), more, true);   // the style of the text before it, as typing does
		e->invalidateLayout();
		noteTextEdit(e);
		for (int i = 0; i < 5; ++i)
			QApplication::processEvents();
		const double h1 = e->height();
		bool others = true;
		for (PageItem* it : allItems(doc))
			if (it != e && pos.contains(it->itemName()) && (qAbs(pos[it->itemName()].x() - it->xPos()) > 0.01 || qAbs(pos[it->itemName()].y() - it->yPos()) > 0.01))
				others = false;
		check(QStringLiteral("edited text: S_10_auto_left re-fitted %1 -> %2 mm, no overflow, other items unmoved").arg(mm(h0), 0, 'f', 2).arg(mm(h1), 0, 'f', 2),
		      h1 > h0 + 1.0 && !e->asTextFrame()->frameOverflows() && others);
		UndoManager::instance()->undo(1);
		check(QStringLiteral("one Ctrl+Z takes back the fit only (height %1 mm, text kept)").arg(mm(e->height()), 0, 'f', 2),
		      qAbs(e->height() - h0) < 0.01 && e->itemText.plainText().endsWith(more.trimmed()));
		UndoManager::instance()->redo(1);
		check(QStringLiteral("redo fits again (%1 mm)").arg(mm(e->height()), 0, 'f', 2), qAbs(e->height() - h1) < 0.01);
		for (int i = 0; i < cases.size(); ++i)
			if (cases[i].startsWith(QLatin1String("S_10_auto_left ")))
				cases.removeAt(i--);
		addCase("S_10_auto_left", e);
	}
	if (stBody)
	{
		PageItem* box = storyBoxOf(doc, storyMembers(stBody));
		const double bh0 = box ? box->height() : 0.0;
		const QPointF bodyPos(stBody->xPos(), stBody->yPos());
		const double bodyH = stBody->height();
		stBody->itemText.insertChars(stBody->itemText.length(), more, true);
		stBody->invalidateLayout();
		noteTextEdit(stBody);
		for (int i = 0; i < 5; ++i)
			QApplication::processEvents();
		check(QStringLiteral("edited story text: the story box follows (%1 -> %2 mm), the frames stay (body %3,%4 %5 -> %6,%7 %8)")
		          .arg(mm(bh0), 0, 'f', 2).arg(box ? mm(box->height()) : 0, 0, 'f', 2)
		          .arg(bodyPos.x(), 0, 'f', 2).arg(bodyPos.y(), 0, 'f', 2).arg(bodyH, 0, 'f', 2)
		          .arg(stBody->xPos(), 0, 'f', 2).arg(stBody->yPos(), 0, 'f', 2).arg(stBody->height(), 0, 'f', 2),
		      box && box->height() > bh0 + 1.0 && qAbs(stBody->xPos() - bodyPos.x()) < 0.01 && qAbs(stBody->yPos() - bodyPos.y()) < 0.01
		      && qAbs(stBody->height() - bodyH) < 0.01);
		for (int i = 0; i < cases.size(); ++i)
			if (cases[i].startsWith(QLatin1String("STORY_welded ")))
				cases.removeAt(i--);
		if (box)
			addCase("STORY_welded", box);
	}

	// 4. the story box moves with its welded story
	if (stBody)
	{
		PageItem* box = storyBoxOf(doc, storyMembers(stBody));
		const QPointF b0 = box ? QPointF(box->xPos(), box->yPos()) : QPointF();
		doc->moveItem(10.0, 0.0, stBody);
		const QPointF b1 = box ? QPointF(box->xPos(), box->yPos()) : QPointF();
		check(QStringLiteral("moving the welded story moves its box (dx %1 pt)").arg(b1.x() - b0.x(), 0, 'f', 2), box && qAbs(b1.x() - b0.x() - 10.0) < 0.01);
		doc->moveItem(-10.0, 0.0, stBody);
	}

	doc->changed();
	view->DrawNew();
	QFile cf(prefix + "-cases.txt");
	if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		cf.write(cases.join('\n').toUtf8() + "\n");
	cf.close();
	const QString saveAs = qEnvironmentVariable("SUNEER_BOXFIT_SAVEAS");
	if (!saveAs.isEmpty())
		check(QStringLiteral("saved %1").arg(saveAs), doc->save(saveAs));
	lines << (fails ? QStringLiteral("FAILED: %1").arg(fails) : QStringLiteral("ALL PASS"));
	QFile f(prefix + "-report.txt");
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		f.write(lines.join('\n').toUtf8() + "\n");
	f.close();
	fprintf(stderr, "BOXFIT-SELFTEST done\n");
	QTimer::singleShot(200, qApp, [fails]() { QApplication::exit(fails ? 1 : 0); });
}
