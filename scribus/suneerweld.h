/*
 suneer: stories placed by the News Browser are WELDED (Scribus weld), not grouped: every
 frame stays a plain top-level item (click, edit, resize it directly), and moving one frame moves
 the whole story. The welds form a star around the story's text frame (a tree: Scribus walks
 welds recursively, so the welds must never form a cycle).
*/
#ifndef SUNEERWELD_H
#define SUNEERWELD_H

#include <QList>
#include <QString>

class PageItem;
class ScribusDoc;

namespace SuneerWeld
{
	//! every item reachable through welds from \a item, \a item first
	QList<PageItem*> cluster(PageItem* item);
	//! weld \a items into one star around \a anchor (the story text frame when none is given);
	//! existing welds between them are kept, nothing is welded twice. Returns the welds made.
	int weldStory(ScribusDoc* doc, const QList<PageItem*>& items, PageItem* anchor = nullptr);
	//! remove every weld of \a items
	int unweldStory(const QList<PageItem*>& items);
	//! true when the two frames belong to the same News Browser story (news.story)
	bool sameStory(const PageItem* a, const PageItem* b);
	//! the picture/caption pair that Scribus moves and sizes caption-style
	bool isCaptionPair(const PageItem* a, const PageItem* b);
	QString storyId(const PageItem* item);
	//! an item attribute (ItemAttribute Name/Value) - empty when absent
	QString attr(const PageItem* item, const QString& name);
	void setAttr(PageItem* item, const QString& name, const QString& value);

	/* Attached caption: the caption of a photo sits under it, as wide as the photo, a caption gap below
	   its bottom edge, its height fitted to its text - after EVERY change of the photo's geometry
	   (handles, W/H, fit frame to image, image toolbar, Edit photo, undo/redo).
	   The link is a stable id: the photo carries news.photoUid (a UUID, written at placement),
	   its caption news.captionOf = that uid. Item names are no link: a name the file does not store is
	   made again from the item's place in the list on every load (Image130 can be another photo after
	   frames were deleted). A photo without uid (an older file before relinkDocument, an older version's
	   paste) falls back to the old rules: news.captionOf = <photo name>, the name caption_<photo>, the same
	   story (news.story) and photo index (news.index) - never a caption that carries a uid. */
	//! the caption frames attached to \a photo (empty: none, or "Keep caption attached" is off)
	QList<PageItem*> captionsOf(ScribusDoc* doc, PageItem* photo, bool evenWhenDetached = false);
	//! the photo's stable id (news.photoUid); empty when it has none
	QString photoUid(const PageItem* photo);
	//! the photo's stable id, made (and stored on the photo) when it has none
	QString ensurePhotoUid(PageItem* photo);
	//! link \a caption to \a photo: news.captionOf = the photo's uid (made when it has none)
	void linkCaption(PageItem* photo, PageItem* caption);
	//! after a document load (and a page import): rebuild the photo <-> caption links from the attributes.
	//! Photos that share a uid (copied by an older version) are told apart - the caption goes with the photo it
	//! is welded to / sits under, the other photo gets a new uid; a second caption of one photo is detached;
	//! a caption linked the old way is linked by uid: a News caption by story + index, any other caption by the
	//! name it carries (caption_<photo>), by its weld to the photo, or by where it sits (right under the photo, as
	//! wide - an image-import caption whose photo's name the file did not keep). Weld points outside their frame
	//! are repaired. No undo, the document is not marked modified: saved with the next save.
	//! Returns the links made or changed.
	int relinkDocument(ScribusDoc* doc, QString* report = nullptr);
	//! the photo <-> caption weld points on the edge they share: the photo's at its bottom left (0, height), the
	//! caption's at its top left (0, 0), as a note frame under its text frame. Only when the two are welded.
	void fixCaptionWeldPoints(PageItem* photo, PageItem* caption);
	//! caption frames (a News caption, one linked by news.captionOf, one named caption_<photo>) that belong to no
	//! photo of the document any more - top-level frames only (not inside a group)
	QList<PageItem*> orphanCaptions(ScribusDoc* doc);
	//! the captions of the photos in \a items that are not among \a items themselves (Edit > Delete asks about them)
	QList<PageItem*> captionsLeftBehind(ScribusDoc* doc, const QList<PageItem*>& items);
	//! after a paste / duplicate / page copy (ScriXmlDoc::readElemToLayer): \a added are the new items. A copied
	//! photo whose uid another photo of the document has gets a new uid and its copied caption follows it; a
	//! copied caption whose photo was not copied is detached when that photo still has its own caption (a cut
	//! and paste keeps the link). Returns the links changed.
	int renewCopiedLinks(ScribusDoc* doc, const QList<PageItem*>& added);
	//! "Keep caption attached" (news.captionAttached; default on)
	bool captionAttached(const PageItem* photo);
	void setCaptionAttached(PageItem* photo, bool on);
	//! put every attached caption under \a photo again; returns how many were moved. No undo of its own:
	//! called inside the photo's own resize transaction, and replayed by the photo's undo / redo.
	//! While an undo / redo is being replayed it only notes the photo (see undoStepDone).
	int attachCaptions(ScribusDoc* doc, PageItem* photo, bool fitHeight = true);

	/* Undo / redo. The replay restores one frame after the other (PageItem::restoreResize sizes a photo
	   BEFORE it moves it back), so a caption attached in the middle lands under the photo's in-between
	   place. attachCaptions() therefore only notes the photo during the replay; undoStepDone() (after
	   every replayed step: UndoManager::undo / redo, ScribusDoc::undoRedoDone) attaches the captions of
	   those photos once the step is complete. A caption whose own position / size came back from its own
	   undo record in that step (a layout model's Apply, a re-layout) keeps it - only what was never
	   recorded (the attach after a photo resize) is derived from the photo again. */
	//! the replay restored \a item's position and / or size from its own undo record (restoreMove, restoreResize, restoreRotate)
	void noteRestored(const PageItem* item, bool position, bool size);
	//! one undo / redo step has been replayed: attach the captions of the photos it changed
	int undoStepDone();
}

#endif
