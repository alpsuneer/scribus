#ifndef SUNEERGROUPEDIT_H
#define SUNEERGROUPEDIT_H

// Suneer: commands that act on the CHILDREN of one selected group.
//
// Text layout, the selection and the per-type commands only ever see the
// top-level group; its children are not in ScribusDoc::Items. A command that
// is meant for image or text frames therefore finds its targets here, and
// re-fits the group's bounds afterwards when it changed a child's geometry.
// Header-only on purpose: no new symbol in the binary for the plugins.

#include <QList>

#include "pageitem.h"
#include "pageitem_group.h"
#include "scribusdoc.h"
#include "selection.h"
#include "undomanager.h"
#include "undostate.h"

namespace SuneerGroupEdit
{
	//! The group, when the document selection is exactly one group.
	inline PageItem* soleGroup(const ScribusDoc* doc)
	{
		if (!doc || doc->m_Selection->count() != 1)
			return nullptr;
		PageItem* item = doc->m_Selection->itemAt(0);
		return (item && item->isGroup()) ? item : nullptr;
	}

	//! Image frames inside \a group, nested groups included.
	inline QList<PageItem*> imageChildren(const PageItem* group, bool withImageOnly = true)
	{
		QList<PageItem*> res;
		if (!group)
			return res;
		const QList<PageItem*> children = group->getAllChildren();
		for (PageItem* child : children)
			if (child->isImageFrame() && (!withImageOnly || child->imageIsAvailable))
				res.append(child);
		return res;
	}

	//! Text frames inside \a group, nested groups included.
	inline QList<PageItem*> textChildren(const PageItem* group)
	{
		QList<PageItem*> res;
		if (!group)
			return res;
		const QList<PageItem*> children = group->getAllChildren();
		for (PageItem* child : children)
			if (child->isTextFrame())
				res.append(child);
		return res;
	}

	//! Fits the bounds of \a group, innermost groups first, to its children.
	inline void refitDeep(PageItem* group)
	{
		PageItem_Group* g = group ? group->asGroupFrame() : nullptr;
		if (!g)
			return;
		for (PageItem* child : std::as_const(g->groupItemList))
			if (child->isGroup())
				refitDeep(child);
		group->doc()->resizeGroupToContents(group);
		// resizeGroupToContents() intersects the old outline with the new one,
		// so a group that GREW keeps its smaller outline and clips the child
		// that outgrew it. The outline is the full rectangle again here.
		group->SetRectFrame();
		group->ContourLine = group->PoLine.copy();
		group->doc()->setRedrawBounding(group);
	}

	//! Call BEFORE (refitNow = false) and AFTER (refitNow = true) changing the
	//! geometry of children, inside one undo transaction. resizeGroupToContents()
	//! records nothing itself; the two markers re-run it once the children are
	//! back, whichever direction the transaction is replayed in.
	inline void markRefit(PageItem* group, bool refitNow)
	{
		if (!group)
			return;
		if (refitNow)
			refitDeep(group);
		if (!UndoManager::undoEnabled())
			return;
		auto* ss = new SimpleState(Um::Resize, QString(), Um::IResize);
		ss->set("SUNEER_GROUP_REFIT");
		UndoManager::instance()->action(group, ss);
	}
}

#endif
