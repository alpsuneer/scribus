/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerfitgap.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <algorithm>
#include <QStringList>
#include <QTimer>

#include "commonstrings.h"
#include "pageitem.h"
#include "prefsmanager.h"
#include "text/specialchars.h"
#include "pageitem_textframe.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "suneerboxfit.h"
#include "suneertextclean.h"
#include "ui/suneercontrolbar.h"
#include "undomanager.h"
#include "undostate.h"

void SuneerFitGap::runSelfTestIfAsked(ScribusView* view, ScribusDoc* doc)
{
	const QString prefix = qEnvironmentVariable("SUNEER_FITGAP_SELFTEST");
	if (prefix.isEmpty() || !doc || !view)
		return;
	ScribusMainWindow* mw = doc->scMW();
	const QStringList paths { "menu", "toolbar", "placement", "caption", "box" };
	QStringList cases;
	auto mm = [](double pt) { return pt * 25.4 / 72.0; };
	for (int pg = 0; pg < doc->DocPages.count() && pg / 2 < paths.size(); ++pg)
	{
		const QString path = paths[pg / 2];   // two pages per path: Malayalam, English
		ScPage* page = doc->DocPages.at(pg);
		QList<PageItem*> frames;
		for (PageItem* it : *doc->Items)
			if (it->isTextFrame() && it->OwnPage == pg && it->itemName().startsWith(QStringLiteral("F_")))
				frames << it;
		for (PageItem* it : frames)
		{
			PageItem_TextFrame* tf = it->asTextFrame();
			if (path == QLatin1String("menu"))
			{
				Selection one(doc, false);
				one.addItem(it);
				doc->itemSelection_AdjustFrameHeightToText(&one);
			}
			else if (path == QLatin1String("toolbar"))
			{
				doc->m_Selection->clear();
				doc->m_Selection->addItem(it);
				mw->suneerAutoFitHeight();
				doc->m_Selection->clear();
			}
			else if (path == QLatin1String("placement"))
				SuneerTextClean::fitHeight(tf, 1.0, 20000.0);
			else if (path == QLatin1String("caption"))
			{
				doc->m_Selection->clear();
				doc->m_Selection->addItem(it);
				mw->suneerFitCaptionFrames();              // Item > Adjust > Fit Caption Frame
				doc->m_Selection->clear();
			}
			else
			{
				SuneerBorderSettings s;
				s.inset = 2.0 * 72.0 / 25.4;
				s.fit = true;
				s.wholeStory = true;
				SuneerBoxFit::apply(doc, { it }, s);
			}
			tf->invalidateLayout();
			tf->layout();
			cases << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
			         .arg(path, it->itemName()).arg(pg + 1)
			         .arg(mm(it->xPos() - page->xOffset()), 0, 'f', 3).arg(mm(it->yPos() - page->yOffset()), 0, 'f', 3)
			         .arg(mm(it->width()), 0, 'f', 3).arg(mm(it->height()), 0, 'f', 3)
			         .arg(tf->frameOverflows() ? 1 : 0).arg(mm(it->lineWidth()), 0, 'f', 3)
			         .arg(mm(tf->textToFrameDistTop()), 0, 'f', 3);
		}
	}
	// Paste: Malayalam text that ends in two empty paragraphs, pasted at the end of an empty frame in
	// text edit mode (Edit > Paste, plain text), then the toolbar Fit; and the undo of such a paste.
	QStringList checks;
	{
		const QString ml = QString::fromUtf8("സംസ്ഥാനത്ത് ഇന്ന് ശക്തമായ മഴയ്ക്ക് സാധ്യതയുണ്ടെന്ന് കാലാവസ്ഥാ കേന്ദ്രം അറിയിച്ചു.");
		ScPage* p1 = doc->DocPages.at(0);
		const double pt = 72.0 / 25.4;
		auto makeFrame = [&](double xmm, const QString& name) {
			const int k = doc->itemAdd(PageItem::TextFrame, PageItem::Unspecified, p1->xOffset() + xmm * pt, p1->yOffset() + 262 * pt, 60 * pt, 20 * pt, 0,
			                           CommonStrings::None, CommonStrings::None);
			PageItem_TextFrame* tf = doc->Items->at(k)->asTextFrame();
			tf->setItemName(name);
			ParagraphStyle ps;
			ps.charStyle().setFont(PrefsManager::instance().appPrefs.fontPrefs.AvailFonts[QStringLiteral("DBI Text Regular")]);
			ps.charStyle().setFontSize(100);
			tf->itemText.setDefaultStyle(ps);
			tf->setTextToFrameDistTop(pt);
			tf->setTextToFrameDistBottom(pt);
			tf->setTextToFrameDistLeft(pt);
			tf->setTextToFrameDistRight(pt);
			return tf;
		};
		auto pasteInto = [&](PageItem_TextFrame* tf) {
			view->requestMode(modeNormal);
			doc->m_Selection->clear();
			doc->m_Selection->addItem(tf);
			view->requestMode(modeEdit);
			tf->itemText.setCursorPosition(tf->itemText.length());
			QApplication::clipboard()->setText(ml + QStringLiteral("\n\n"));
			const int modeAtPaste = doc->appMode;
			mw->slotEditPaste();
			QStringList tail;
			for (int i = qMax(0, tf->itemText.length() - 3); i < tf->itemText.length(); ++i)
				tail << QStringLiteral("U+%1").arg(tf->itemText.text(i).unicode(), 4, 16, QLatin1Char('0'));
			checks << QStringLiteral("  %1: mode at paste %2 (edit = %3), clipboard \"%4\", story %5 chars, ends %6").arg(tf->itemName()).arg(modeAtPaste).arg(int(modeEdit))
			          .arg(QApplication::clipboard()->text().right(4).toUtf8().toHex().constData()).arg(tf->itemText.length()).arg(tail.join(' '));
			view->requestMode(modeNormal);
		};
		auto endsWithBreak = [](PageItem_TextFrame* tf) {
			const int n = tf->itemText.length();
			return n > 0 && (tf->itemText.text(n - 1) == SpecialChars::PARSEP || tf->itemText.text(n - 1).isSpace());
		};
		PageItem_TextFrame* a = makeFrame(12, QStringLiteral("F_paste_ml_text10_auto"));
		pasteInto(a);
		const QString pasted = a->itemText.text(0, a->itemText.length());
		const int ml_cp = std::count_if(pasted.cbegin(), pasted.cend(), [](QChar c) { return c.unicode() >= 0x0D00 && c.unicode() <= 0x0D7F; });
		checks << QStringLiteral("%1 paste: %2 chars, %3 Malayalam, ends in a paragraph break: %4").arg(endsWithBreak(a) ? "FAIL" : "PASS")
		          .arg(a->itemText.length()).arg(ml_cp).arg(endsWithBreak(a) ? "yes" : "no");
		doc->m_Selection->clear();
		doc->m_Selection->addItem(a);
		mw->suneerAutoFitHeight();
		doc->m_Selection->clear();
		a->invalidateLayout();
		a->layout();
		cases << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
		         .arg(QStringLiteral("paste"), a->itemName()).arg(1)
		         .arg(mm(a->xPos() - p1->xOffset()), 0, 'f', 3).arg(mm(a->yPos() - p1->yOffset()), 0, 'f', 3)
		         .arg(mm(a->width()), 0, 'f', 3).arg(mm(a->height()), 0, 'f', 3)
		         .arg(a->frameOverflows() ? 1 : 0).arg(mm(a->lineWidth()), 0, 'f', 3).arg(mm(a->textToFrameDistTop()), 0, 'f', 3);
		// one Ctrl+Z takes the paste and its clean-up together
		PageItem_TextFrame* b = makeFrame(80, QStringLiteral("U_paste_undo"));
		const bool undoOn = UndoManager::undoEnabled();
		pasteInto(b);
		const int afterPaste = b->itemText.length();
		UndoState* last = UndoManager::instance()->getLastUndo();
		checks << QStringLiteral("  undo enabled %1, last undo step after the paste: \"%2\" (%3)").arg(undoOn)
		          .arg(last ? last->getName() : QStringLiteral("none")).arg(last && dynamic_cast<TransactionState*>(last) ? QStringLiteral("transaction of %1").arg(dynamic_cast<TransactionState*>(last)->sizet()) : QStringLiteral("single"));
		QStringList steps;
		bool sawUntrimmed = false;
		for (int i = 0; i < 5 && b->itemText.length() > 0; ++i)
		{
			UndoState* top = UndoManager::instance()->getLastUndo();
			const QString name = top ? top->getName() : QStringLiteral("none");
			UndoManager::instance()->undo(1);
			steps << QStringLiteral("%1 -> %2 chars").arg(name).arg(b->itemText.length());
			sawUntrimmed = sawUntrimmed || b->itemText.length() > afterPaste;
		}
		checks << QStringLiteral("  undo steps: %1").arg(steps.join(QStringLiteral("; ")));
		checks << QStringLiteral("%1 paste undo: the paste and its clean-up are one step (%2 chars after the paste, 0 reached: %3, an untrimmed state seen: %4)")
		          .arg(b->itemText.length() == 0 && !sawUntrimmed ? "PASS" : "FAIL").arg(afterPaste)
		          .arg(b->itemText.length() == 0 ? "yes" : "no").arg(sawUntrimmed ? "yes" : "no");
		Selection del(doc, false);
		del.addItem(b);
		doc->itemSelection_DeleteItem(&del);
	}
	QFile fc(prefix + QStringLiteral("-checks.txt"));
	if (fc.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		fc.write(checks.join('\n').toUtf8() + "\n");
	fc.close();

	QFile f(prefix + QStringLiteral("-cases.txt"));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		f.write(cases.join('\n').toUtf8() + "\n");
	f.close();
	const QString saveAs = qEnvironmentVariable("SUNEER_FITGAP_SAVEAS");
	if (!saveAs.isEmpty())
		doc->save(saveAs);
	doc->setModified(false);
	fprintf(stderr, "FITGAP-SELFTEST done %lld cases\n", (long long) cases.size());
	QTimer::singleShot(300, mw, [mw]() { mw->close(); });
}
