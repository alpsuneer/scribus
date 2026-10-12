/*
 News Browser - "Obituary (ചരമം)" layout model.

 Member functions of SuneerNewsPanel kept in their own file. The model
 (grid, module sizes, styles, photo processing) is obituary.json, see
 suneer_obit_model.h. Flow of Place Selected with this model:

   placeObituaries()  classify -> download the obit pictures (cache) ->
                      SuneerObitPhotoWorker (python: rembg + face crop,
                      background, progress in the status line) ->
   obitPlaceNow()     one undo transaction: obit modules (photo + text frame,
                      grouped, tagged) in a masonry over the obit columns,
                      short news stacked in the news columns, nothing over
                      existing items; stops when a module does not fit and
                      lists the rest; then the usual USED marking.
*/
#include "suneer_news_panel.h"
#include "suneer_obit_model.h"

#include "commonstrings.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "sccolor.h"
#include "sccolorengine.h"
#include "scpage.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneerimagelinks.h"
#include "undomanager.h"
#include "undostate.h"
#include "undotransaction.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

const double kMm = 72.0 / 25.4;

QString obitCollapse(const QString& s)
{
	static const QRegularExpression ws(QStringLiteral("\\s+"));
	return QString(s).replace(ws, QStringLiteral(" ")).trimmed();
}

//! Place line of an obit: dateline / place field, else a short first line of
//! the content ("ചേർത്തല"). Returns it and strips it from \a paras.
QString obitPlaceLine(const QJsonObject& news, QStringList& paras)
{
	QString place = obitCollapse(news["dateline"].toString());
	if (place.isEmpty())
		place = obitCollapse(news["place"].toString());
	if (place.isEmpty() && !paras.isEmpty())
	{
		static const QRegularExpression sentenceEnd(QStringLiteral("[.!?।]"));
		const QString first = paras.first().trimmed();
		if (!first.isEmpty() && first.length() <= 25 && first.split(' ', Qt::SkipEmptyParts).size() <= 3 && !first.contains(sentenceEnd))
		{
			place = first;
			paras.removeFirst();
		}
	}
	return place;
}

QString obitSettingsKey(const QString& key)
{
	QString k = key.trimmed();
	k.replace('/', '_');
	return QStringLiteral("layoutModel/") + (k.isEmpty() ? QStringLiteral("_default") : k);
}

} // namespace

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

void SuneerNewsPanel::fillLayoutModelCombo()
{
	if (!m_layoutModelCmb)
		return;
	m_layoutModelCmb->blockSignals(true);
	m_layoutModelCmb->clear();
	m_layoutModelCmb->addItem(tr("Normal (current 4-col + 3-col photo)"), QStringLiteral("normal"));
	m_layoutModelCmb->addItem(QStringLiteral("Obituary (ചരമം)"), QStringLiteral("obituary"));
	m_layoutModelCmb->blockSignals(false);
}

QString SuneerNewsPanel::layoutModelKey() const
{
	if (!m_pageCombo)
		return QString();
	const QString pageId = m_pageCombo->currentData().toString();
	const QString type = m_pageTypes.value(pageId).trimmed();
	return type.isEmpty() ? m_pageCombo->currentText().trimmed() : type;
}

// The remembered choice for this page type (or page name); without one, an
// OBIT page (pageType or name says obit / ചരമ) starts with the Obituary model.
void SuneerNewsPanel::preselectLayoutModel()
{
	if (!m_layoutModelCmb)
		return;
	const QString key = layoutModelKey();
	QSettings s("Faircode", "ScribusNews");
	QString model = s.value(obitSettingsKey(key)).toString();
	if (model.isEmpty())
	{
		const QString probe = (key + ' ' + (m_pageCombo ? m_pageCombo->currentText() : QString())).toLower();
		model = (probe.contains(QLatin1String("obit")) || probe.contains(QStringLiteral("ചരമ"))) ? QStringLiteral("obituary") : QStringLiteral("normal");
	}
	const int idx = m_layoutModelCmb->findData(model);
	m_layoutModelCmb->blockSignals(true);
	m_layoutModelCmb->setCurrentIndex(idx < 0 ? 0 : idx);
	m_layoutModelCmb->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Classification and paths
// ---------------------------------------------------------------------------

bool SuneerNewsPanel::obitIsObituary(const QJsonObject& news, const SuneerObitModel& model)
{
	const QString rawTitle = news["title"].toString().trimmed();
	const QString title = obitCollapse(rawTitle);
	if (rawTitle.contains('\n'))
		return false;                       // a two-line headline
	if (title.split(' ', Qt::SkipEmptyParts).size() > model.titleMaxWords)
		return false;
	if (title.length() > model.titleMaxChars)
		return false;
	QString body;
	for (const QString& p : contentToParagraphs(news["content"].toString()))
		body += p;
	return body.length() <= model.bodyMaxChars;
}

QString SuneerNewsPanel::obitImageDir() const
{
	QString dir;
	if (m_doc && !m_doc->documentFileName().isEmpty())
	{
		const QFileInfo fi(m_doc->documentFileName());
		dir = fi.absolutePath() + '/' + fi.completeBaseName() + QStringLiteral("_images/obit");
	}
	else
		dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/news/obit");
	QDir().mkpath(dir);
	return dir;
}

QColor SuneerNewsPanel::obitBackgroundColor(const SuneerObitModel& model, QString* fillName) const
{
	QString name = model.photoBackground.trimmed();
	if (name.isEmpty())
		name = model.photoFill;
	if (!m_doc)
	{
		if (fillName)
			*fillName = name;
		return QColor(190, 196, 200);
	}
	if (name.compare(QLatin1String("light grey"), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String("light gray"), Qt::CaseInsensitive) == 0)
		name = m_doc->PageColors.tryAddColor(QStringLiteral("Obit Light Grey"), ScColor(0, 0, 0, 51));
	else if (!m_doc->PageColors.contains(name) && name == model.photoFill)
		name = m_doc->PageColors.tryAddColor(name, ScColor(qRound(model.photoFillCMYK[0] * 2.55), qRound(model.photoFillCMYK[1] * 2.55), qRound(model.photoFillCMYK[2] * 2.55), qRound(model.photoFillCMYK[3] * 2.55)));
	else if (!m_doc->PageColors.contains(name))
		name = m_doc->PageColors.tryAddColor(model.photoFill, ScColor(qRound(model.photoFillCMYK[0] * 2.55), qRound(model.photoFillCMYK[1] * 2.55), qRound(model.photoFillCMYK[2] * 2.55), qRound(model.photoFillCMYK[3] * 2.55)));
	if (fillName)
		*fillName = name;
	return ScColorEngine::getRGBColor(m_doc->PageColors[name], m_doc);
}

// ---------------------------------------------------------------------------
// Step 1: pictures in the background
// ---------------------------------------------------------------------------

void SuneerNewsPanel::placeObituaries(const QList<QJsonObject>& stories)
{
	if (!m_doc || stories.isEmpty())
		return;
	if (m_obitWorker && m_obitWorker->running())
	{
		m_statusLabel->setText(tr("Obituary photos are still being processed - wait for the previous placement"));
		return;
	}
	QString note;
	const SuneerObitModel model = SuneerObitModel::load(&note);
	if (!note.isEmpty())
		apiLog(QStringLiteral("obituary.json: %1").arg(note));

	m_obitPending = stories;
	m_obitPhotoOut.clear();
	m_obitPhotoStatus.clear();
	m_obitPhotoWhy.clear();
	m_obitNewsPhoto.clear();
	m_obitPhotoFailed = 0;

	QList<SuneerObitPhotoWorker::Item> items;
	const QString dir = obitImageDir();
	int obits = 0, news = 0;
	int downloaded = 0;
	for (const QJsonObject& n : stories)
	{
		const bool isObit = obitIsObituary(n, model);
		if (isObit)
			++obits;
		else
			++news;
		const QString sid = newsServerId(n);
		const QJsonArray media = mediaList(n);
		if (media.isEmpty())
		{
			m_obitPhotoWhy[sid] = tr("no media in the story");
			apiLog(QStringLiteral("obit model: ..%1 '%2': no media in the API reply").arg(sid.right(6), obitCollapse(n["title"].toString()).left(24)));
			continue;
		}
		const QString fileUrl = media[0].toObject()["media"].toObject()["fileUrl"].toString();
		m_statusLabel->setText(tr("Downloading picture %1...").arg(++downloaded));
		QString why;
		const QString local = downloadImage(fileUrl, &why);
		if (local.isEmpty())
		{
			m_obitPhotoStatus[sid] = QStringLiteral("fail");
			m_obitPhotoWhy[sid] = tr("download failed: %1").arg(why);
			apiLog(QStringLiteral("obit model: ..%1 picture download failed - %2").arg(sid.right(6), why));
			++m_obitPhotoFailed;
			continue;
		}
		if (!isObit)
		{
			m_obitNewsPhoto[sid] = local;      // short news: the picture as it is, no processing
			continue;
		}
		SuneerObitPhotoWorker::Item it;
		it.id = sid.isEmpty() ? QString::number(items.size()) : sid;
		it.src = local;
		it.out = dir + '/' + it.id + QStringLiteral(".jpg");
		items << it;
	}
	apiLog(QStringLiteral("obituary model: %1 obits, %2 short news, %3 pictures to process in %4").arg(obits).arg(news).arg(items.size()).arg(dir));
	if (items.isEmpty())
	{
		obitPlaceNow();
		return;
	}
	if (!m_obitWorker)
		m_obitWorker = new SuneerObitPhotoWorker(this);
	QString fillName;
	const QColor bg = obitBackgroundColor(model, &fillName);
	QString why;
	if (!m_obitWorker->start(items, model, bg, dir, &why))
	{
		m_statusLabel->setText(tr("Obituary photos could not be processed (%1); placing with the original pictures").arg(why));
		apiLog(QStringLiteral("obit photo worker not started: %1").arg(why));
		for (const SuneerObitPhotoWorker::Item& it : items)
		{
			m_obitPhotoStatus[it.id] = QStringLiteral("fail");
			m_obitPhotoWhy[it.id] = tr("photo processing not available: %1").arg(why);
			++m_obitPhotoFailed;
		}
		obitPlaceNow();
		return;
	}
	m_placeBtn->setEnabled(false);
	m_statusLabel->setText(tr("Processing %n obituary photo(s) (background removal, face crop)...", "", items.size()));
	m_obitWorker->disconnect(this);
	connect(m_obitWorker, &SuneerObitPhotoWorker::progress, this, [this](int done, int total, const QString&, const QString& status, const QString&) {
		m_statusLabel->setText(tr("Obituary photos: %1 of %2 (%3)").arg(done).arg(total).arg(status));
	});
	connect(m_obitWorker, &SuneerObitPhotoWorker::finished, this, [this, items](bool ok, const QString& error) {
		for (const SuneerObitPhotoWorker::Result& r : m_obitWorker->results())
		{
			m_obitPhotoStatus[r.id] = r.status;
			if (r.status != QLatin1String("fail") && QFileInfo::exists(r.out))
				m_obitPhotoOut[r.id] = r.out;
			else
				m_obitPhotoWhy[r.id] = tr("photo processing failed: %1").arg(r.message.isEmpty() ? r.status : r.message);
		}
		// Whatever the worker did not deliver goes in without a photo, with the reason.
		for (const SuneerObitPhotoWorker::Item& it : items)
			if (!m_obitPhotoOut.contains(it.id))
			{
				if (!m_obitPhotoStatus.contains(it.id))
					m_obitPhotoStatus[it.id] = QStringLiteral("fail");
				if (!m_obitPhotoWhy.contains(it.id))
					m_obitPhotoWhy[it.id] = ok ? tr("photo processing gave no result") : tr("photo worker failed: %1").arg(error);
				++m_obitPhotoFailed;
			}
		if (!ok)
			apiLog(QStringLiteral("obit photo worker: %1").arg(error));
		m_placeBtn->setEnabled(true);
		obitPlaceNow();
	});
}

// ---------------------------------------------------------------------------
// Step 2: the page
// ---------------------------------------------------------------------------

void SuneerNewsPanel::obitPlaceNow()
{
	const QList<QJsonObject> stories = m_obitPending;
	m_obitPending.clear();
	if (!m_doc || stories.isEmpty())
		return;
	ScPage* page = m_doc->currentPage();
	if (!page)
		return;
	const SuneerObitModel model = SuneerObitModel::load();
	QString fillName;
	obitBackgroundColor(model, &fillName);

	// -- geometry -----------------------------------------------------------
	const double pageX = page->xOffset();
	const double pageY = page->yOffset();
	const double usableW = page->width() - page->Margins.left() - page->Margins.right();
	const int gridCols = qMax(1, model.gridColumns);
	const double gap = model.gridGapMm * kMm;
	const double colW = (usableW - gap * (gridCols - 1)) / gridCols;
	auto colX = [&](int col1) { return pageX + page->Margins.left() + (col1 - 1) * (colW + gap); };
	const double top = pageY + model.contentTopMm * kMm;
	const double bottom = pageY + page->height() - page->Margins.bottom();
	const double moduleGap = model.moduleGapMm * kMm;
	const double photoH = model.photoMm * kMm;

	// -- what is already there: every item touching this page, master items too
	QList<QRectF> obstacles;
	const QRectF pageRect(pageX, pageY, page->width(), page->height());
	for (int i = 0; i < m_doc->DocItems.count(); ++i)
	{
		PageItem* it = m_doc->DocItems.at(i);
		if (!it || it->isGroupChild() || (it->isAutoText && it->itemText.length() == 0))
			continue;                                   // an empty automatic text frame is not an obstacle
		const QRectF r = it->getBoundingRect();
		if (r.intersects(pageRect))
			obstacles << r;
	}
	if (m_doc->MasterNames.contains(page->masterPageName()))
	{
		const ScPage* mp = m_doc->MasterPages.at(m_doc->MasterNames[page->masterPageName()]);
		for (int i = 0; i < m_doc->MasterItems.count(); ++i)
		{
			PageItem* it = m_doc->MasterItems.at(i);
			if (!it || it->isGroupChild() || it->OnMasterPage != page->masterPageName())
				continue;
			if (it->isTextFrame() && it->itemText.length() == 0)
				continue;                               // the master's empty (automatic) text frame
			QRectF r = it->getBoundingRect();
			if (mp)
				r.translate(pageX - mp->xOffset(), pageY - mp->yOffset());
			obstacles << r;
		}
	}
	{
		QStringList ob;
		for (const QRectF& r : obstacles)
			ob << QStringLiteral("(%1,%2 %3x%4)").arg((r.x() - pageX) / kMm, 0, 'f', 1).arg((r.y() - pageY) / kMm, 0, 'f', 1).arg(r.width() / kMm, 0, 'f', 1).arg(r.height() / kMm, 0, 'f', 1);
		apiLog(QStringLiteral("obituary model: page %1 colW %2 mm top %3 bottom %4 mm; %5 obstacles mm: %6").arg(page->pageNr()).arg(colW / kMm, 0, 'f', 2).arg((top - pageY) / kMm, 0, 'f', 1).arg((bottom - pageY) / kMm, 0, 'f', 1).arg(obstacles.size()).arg(ob.join(' ')));
	}
	const double tol = 0.5;   // pt: touching is not overlapping
	auto blockedBelow = [&](const QRectF& r, double* pushTo) -> bool {
		bool hit = false;
		double maxBottom = r.top();
		for (const QRectF& o : obstacles)
		{
			const QRectF oi = o.adjusted(tol, tol, -tol, -tol);
			if (oi.isValid() && oi.intersects(r))
			{
				hit = true;
				maxBottom = qMax(maxBottom, o.bottom());
			}
		}
		if (hit && pushTo)
			*pushTo = maxBottom + moduleGap;
		return hit;
	};

	// -- styles -------------------------------------------------------------
	QStringList missingStyles;
	auto styleOr = [&](const QString& name) -> QString {
		if (name.isEmpty() || m_doc->paragraphStyles().contains(name))
			return name;
		if (!missingStyles.contains(name))
			missingStyles << name;
		return QString();
	};
	auto makeTextFrame = [&](double fx, double fy, double fw, double fh) -> PageItem* {
		const int idx = m_doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, fx, fy, fw, fh, 0, CommonStrings::None, CommonStrings::None);
		PageItem* it = (idx >= 0) ? m_doc->Items->at(idx) : nullptr;
		if (it)
		{
			ParagraphStyle defStyle(it->itemText.defaultStyle());
			defStyle.setHyphenationMode(ParagraphStyle::HyphenationMode::AutomaticHyphenation);
			defStyle.charStyle().setLanguage(QStringLiteral("ml"));
			it->itemText.setDefaultStyle(defStyle);
			it->setTextToFrameDist(0.0, 0.0, 0.0, 0.0);
			it->setLineColor(CommonStrings::None);
			it->setFillColor(CommonStrings::None);
		}
		return it;
	};
	int lastParaStart = 0;
	QString lastParaStyle;
	auto addPara = [&](PageItem* frame, const QString& text, const QString& styleName) {
		if (!frame || text.trimmed().isEmpty())
			return;
		StoryText& story = frame->itemText;
		const int startPos = story.length();
		story.insertChars(text);
		story.insertChars(QChar(13));
		const QString st = styleOr(styleName);
		if (!st.isEmpty())
		{
			ParagraphStyle pStyle;
			pStyle.setParent(st);
			story.applyStyle(startPos, pStyle, story.length() - startPos);
		}
		lastParaStart = startPos;
		lastParaStyle = st;
	};
	auto trimTrailingParSep = [&](PageItem* frame) {
		// The last paragraph break would add an empty line to the fitted
		// height; its style lives on that break, so the last paragraph (now
		// the trailing one) gets the style again.
		if (frame && frame->itemText.length() > 0 && frame->itemText.text(frame->itemText.length() - 1, 1) == QStringLiteral("\r"))
		{
			frame->itemText.removeChars(frame->itemText.length() - 1, 1);
			if (!lastParaStyle.isEmpty() && lastParaStart < frame->itemText.length())
			{
				ParagraphStyle pStyle;
				pStyle.setParent(lastParaStyle);
				frame->itemText.applyStyle(lastParaStart, pStyle, frame->itemText.length() - lastParaStart);
			}
		}
	};
	auto removeItems = [&](QList<PageItem*> items) {
		for (PageItem* it : items)
		{
			if (!it)
				continue;
			m_doc->m_Selection->clear();
			m_doc->m_Selection->addItem(it);
			m_doc->itemSelection_DeleteItem(m_doc->m_Selection);
		}
		m_doc->m_Selection->clear();
	};
	auto flagColorName = [&]() -> QString {
		if (m_doc->PageColors.contains(model.flagColor))
			return model.flagColor;
		return m_doc->PageColors.tryAddColor(QStringLiteral("Obit Check"), ScColor(0, 128, 255, 0));
	};

	// -- one undo step for everything ---------------------------------------
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Place %1 obituaries").arg(stories.size()), QString(), Um::ICreate);
	m_doc->m_Selection->clear();

	QList<QJsonObject> placed, remaining;
	QStringList remainingTitles;
	QList<int> obitCols, newsCols;
	for (int c = qMax(1, model.obitFirstCol); c <= qMin(gridCols, model.obitLastCol); ++c)
		obitCols << c;
	for (int c = qMax(1, model.newsFirstCol); c <= qMin(gridCols, model.newsLastCol); ++c)
		newsCols << c;
	if (obitCols.isEmpty())
		obitCols << 1;
	QHash<int, double> cursor;
	for (int c : obitCols)
		cursor[c] = top;
	double newsCursor = top;
	bool obitsStopped = false, newsStopped = false;
	int noFace = 0, obitsPlaced = 0, obitsWithPhoto = 0, newsPlaced = 0, newsWithPhoto = 0;
	QStringList photoProblems;      // "title: reason" for every placed story without its picture
	auto captionStyle = [&]() { QSettings cfg("Faircode", "ScribusNews"); const QString v = cfg.value("style/captionStyle", "09 Caption").toString().trimmed(); return v.isEmpty() ? QStringLiteral("09 Caption") : v; }();

	for (const QJsonObject& n : stories)
	{
		const QString sid = newsServerId(n);
		const QString title = obitCollapse(n["title"].toString());
		QStringList paras = contentToParagraphs(n["content"].toString());
		const bool isObit = obitIsObituary(n, model);

		if (isObit)
		{
			if (obitsStopped)
			{
				remaining << n;
				remainingTitles << title;
				continue;
			}
			const QString photoPath = m_obitPhotoOut.value(sid);
			const QString photoStatus = m_obitPhotoStatus.value(sid);
			const bool hasPhoto = !photoPath.isEmpty() && QFileInfo::exists(photoPath);
			if (!hasPhoto)
				photoProblems << QStringLiteral("%1: %2").arg(title.left(22), m_obitPhotoWhy.value(sid, tr("picture missing")));
			const QString place = obitPlaceLine(n, paras);

			// Build at the top of the shortest column, then slide down past obstacles.
			int bestCol = obitCols.first();
			for (int c : obitCols)
				if (cursor[c] < cursor[bestCol])
					bestCol = c;
			double x = colX(bestCol), y = cursor[bestCol];
			QList<PageItem*> items;
			PageItem* photo = nullptr;
			if (hasPhoto)
			{
				const int pi = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle, x, y, colW, photoH, 0, fillName, CommonStrings::None);
				photo = (pi >= 0) ? m_doc->Items->at(pi) : nullptr;
				if (photo)
				{
					photo->setFillColor(fillName);
					photo->setLineColor(CommonStrings::None);
					photo->setImageScalingMode(false, true);   // scale to frame, keep ratio
					m_doc->loadPict(photoPath, photo, false, true);
					photo->adjustPictScale();
					if (photo->pixm.imgInfo.lowResType == 0)
						photo->setResolution(1);
					SuneerImageLinks::embedPlaced(m_doc, photo);
					setAttr(photo, "news.role", "photo");
					setAttr(photo, "news.index", "0");
					setAttr(photo, "obit.photoStatus", photoStatus);
					items << photo;
				}
			}
			PageItem* text = makeTextFrame(x, y + (photo ? photoH : 0.0), colW, 60.0 * kMm);
			if (!text)
			{
				removeItems(items);
				remaining << n;
				remainingTitles << title;
				continue;
			}
			text->setTextToFrameDist(0.0, 0.0, model.textTopDistPt, 0.0);
			text->setColumns(1);
			text->setItemName(title.left(30).isEmpty() ? tr("Obituary") : title.left(30));
			setAttr(text, "news.role", "text");
			addPara(text, title, photo ? model.styleName : model.styleNameNoPic);
			addPara(text, place, photo ? model.stylePlace : model.stylePlaceNoPic);
			for (const QString& p : paras)
				addPara(text, p, photo ? model.styleBody : model.styleBodyNoPic);
			trimTrailingParSep(text);
			fitFrame(text);
			items << text;
			const double moduleH = (photo ? photoH : 0.0) + text->height();

			// Find a column and y where the module is free of other items.
			bool fitted = false;
			QList<int> order = obitCols;
			std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cursor[a] < cursor[b]; });
			for (int c : order)
			{
				double cy = cursor[c];
				const double cx = colX(c);
				for (int guard = 0; guard < 50; ++guard)
				{
					if (cy + moduleH > bottom + tol)
						break;
					double pushTo = cy;
					if (!blockedBelow(QRectF(cx, cy, colW, moduleH), &pushTo))
					{
						x = cx;
						y = cy;
						bestCol = c;
						fitted = true;
						break;
					}
					cy = pushTo;
				}
				if (fitted)
					break;
			}
			if (!fitted)
			{
				apiLog(QStringLiteral("obituary model: '%1' does not fit (module %2 mm high, photo %3, text frame %4 mm, cursors %5)").arg(title.left(20)).arg(moduleH / kMm, 0, 'f', 1).arg(photo ? "yes" : "no").arg(text->height() / kMm, 0, 'f', 1).arg([&]() { QStringList c; for (int cc : obitCols) c << QString::number((cursor[cc] - pageY) / kMm, 'f', 0); return c.join('/'); }()));
				removeItems(items);
				obitsStopped = true;
				remaining << n;
				remainingTitles << title;
				continue;
			}
			if (photo)
				photo->setXYPos(x, y);
			text->setXYPos(x, y + (photo ? photoH : 0.0));
			cursor[bestCol] = y + moduleH + moduleGap;
			obstacles << QRectF(x, y, colW, moduleH);

			// No face found: a small orange, non-printing "check" flag on the photo.
			if (photo && photoStatus == QLatin1String("noface"))
			{
				++noFace;
				PageItem* flag = makeTextFrame(x, y, 12.0 * kMm, 4.0 * kMm);
				if (flag)
				{
					flag->setFillColor(flagColorName());
					flag->setPrintEnabled(false);
					flag->setItemName(QStringLiteral("check_%1").arg(text->itemName()));
					flag->itemText.insertChars(QStringLiteral("check"));
					CharStyle cs;
					cs.setFontSize(70);
					flag->itemText.applyCharStyle(0, flag->itemText.length(), cs);
					setAttr(flag, "news.role", "flag");
					setAttr(flag, "obit.check", "noface");
					items << flag;
				}
			}
			// Group photo + text (+ flag) and tag everything with the story.
			PageItem* group = nullptr;
			if (items.size() > 1)
			{
				Selection sel(this, false);
				for (PageItem* it : items)
					sel.addItem(it);
				group = m_doc->itemSelection_GroupObjects(false, false, &sel);
			}
			QList<PageItem*> tagged = items;
			if (group)
				tagged << group;
			tagStoryItems(tagged, title, photo ? QStringLiteral("top") : QStringLiteral("none"), 1, 1, sid);
			for (PageItem* it : tagged)
			{
				setAttr(it, "news.model", "obituary");
				if (!newsRefId(n).isEmpty())
					setAttr(it, "news.refId", newsRefId(n));
			}
			placed << n;
			++obitsPlaced;
			if (photo)
				++obitsWithPhoto;
		}
		else
		{
			// Short news in the news columns: one frame spanning them, text columns inside.
			if (newsStopped || newsCols.isEmpty())
			{
				remaining << n;
				remainingTitles << title;
				continue;
			}
			const double x = colX(newsCols.first());
			const double w = colW * newsCols.size() + gap * (newsCols.size() - 1);
			const QString place = obitPlaceLine(n, paras);
			QList<PageItem*> items;
			PageItem* text = makeTextFrame(x, newsCursor, w, 60.0 * kMm);
			if (!text)
			{
				remaining << n;
				remainingTitles << title;
				continue;
			}
			items << text;
			text->setColumns(model.newsTextColumns);
			text->setColumnGap(model.newsColGapMm * kMm);
			text->setItemName(title.left(30).isEmpty() ? tr("News story") : title.left(30));
			setAttr(text, "news.role", "text");
			QString headStyle;
			for (const SuneerObitModel::HeadRule& r : model.newsHeadlines)
				if (title.length() <= r.maxChars)
				{
					headStyle = r.style;
					break;
				}
			if (headStyle.isEmpty() && !model.newsHeadlines.isEmpty())
				headStyle = model.newsHeadlines.last().style;
			addPara(text, title, headStyle);
			{
				// The headline spans the text columns, as on the reference page.
				ParagraphStyle span;
				span.setSpanColumns(-1);
				text->itemText.applyStyle(0, span, qMax(1, title.length()));
			}
			addPara(text, place, model.newsDateline);
			for (int i = 0; i < paras.size(); ++i)
				addPara(text, paras[i], i == 0 ? model.newsFirstBody : model.newsBody);
			trimTrailingParSep(text);

			// The picture: one column wide at the top right of the frame, the
			// text wrapping around it with the wrap gap; caption under it.
			PageItem* photo = nullptr;
			PageItem* caption = nullptr;
			double photoBlockH = 0.0;
			const QString newsPhotoPath = m_obitNewsPhoto.value(sid);
			if (!newsPhotoPath.isEmpty() && QFileInfo::exists(newsPhotoPath))
			{
				QImageReader rd(newsPhotoPath);
				rd.setAutoTransform(true);
				const QSize sz = rd.size();
				double ph = colW;                                       // square when the size is unknown
				if (sz.isValid() && sz.width() > 0)
					ph = qBound(colW * 0.6, colW * sz.height() / double(sz.width()), colW * 1.3);   // taller pictures are cropped
				const double px = x + w - colW;
				const int pi = m_doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle, px, newsCursor, colW, ph, 0, CommonStrings::None, CommonStrings::None);
				photo = (pi >= 0) ? m_doc->Items->at(pi) : nullptr;
				if (photo)
				{
					items << photo;
					photo->setFillColor(CommonStrings::None);
					photo->setLineColor(CommonStrings::None);
					photo->setImageScalingMode(false, true);        // fill the frame, keep ratio (centre crop)
					m_doc->loadPict(newsPhotoPath, photo, false, true);
					photo->adjustPictScale();
					if (photo->pixm.imgInfo.lowResType == 0)
						photo->setResolution(1);
					SuneerImageLinks::embedPlaced(m_doc, photo);
					setAttr(photo, "news.role", "photo");
					setAttr(photo, "news.index", "0");
					setWrap(photo, true);
					photoBlockH = ph;
					const QString capText = newsCaption(n);
					if (!capText.isEmpty())
					{
						caption = makeTextFrame(px, newsCursor + ph, colW, 8.0 * kMm);
						if (caption)
						{
							items << caption;
							caption->setItemName(QStringLiteral("caption_%1").arg(text->itemName()));
							setAttr(caption, "news.role", "caption");
							setAttr(caption, "news.index", "0");
							addPara(caption, capText, captionStyle);
							trimTrailingParSep(caption);
							fitFrame(caption);
							setWrap(caption, true);
							photoBlockH += caption->height();
						}
					}
				}
			}
			else if (!m_obitPhotoWhy.value(sid).isEmpty())
				photoProblems << QStringLiteral("%1: %2").arg(title.left(22), m_obitPhotoWhy.value(sid));
			fitFrame(text);
			const double h = qMax(text->height(), photoBlockH);
			bool fitted = false;
			double cy = newsCursor;
			for (int guard = 0; guard < 50; ++guard)
			{
				if (cy + h > bottom + tol)
					break;
				double pushTo = cy;
				if (!blockedBelow(QRectF(x, cy, w, h), &pushTo))
				{
					fitted = true;
					break;
				}
				cy = pushTo;
			}
			if (!fitted)
			{
				apiLog(QStringLiteral("obituary model: news '%1' does not fit (%2 mm high)").arg(title.left(20)).arg(h / kMm, 0, 'f', 1));
				removeItems(items);
				newsStopped = true;
				remaining << n;
				remainingTitles << title;
				continue;
			}
			const double dy = cy - newsCursor;
			if (dy != 0.0)
			{
				for (PageItem* it : items)
					it->setXYPos(it->xPos(), it->yPos() + dy);
				text->invalidateLayout();
				text->layout();
			}
			newsCursor = cy + h + model.newsGapMm * kMm;
			obstacles << QRectF(x, cy, w, h);
			PageItem* group = nullptr;
			if (items.size() > 1)
			{
				Selection sel(this, false);
				for (PageItem* it : items)
					sel.addItem(it);
				group = m_doc->itemSelection_GroupObjects(false, false, &sel);
			}
			QList<PageItem*> tagged = items;
			if (group)
				tagged << group;
			tagStoryItems(tagged, title, photo ? QStringLiteral("right") : QStringLiteral("none"), 1, newsCols.size(), sid);
			for (PageItem* it : tagged)
			{
				setAttr(it, "news.model", "obituary-news");
				if (!newsRefId(n).isEmpty())
					setAttr(it, "news.refId", newsRefId(n));
			}
			placed << n;
			++newsPlaced;
			if (photo)
				++newsWithPhoto;
		}
	}

	recordPlaceUndo(placed);
	if (tx)
		tx.commit();
	m_doc->m_Selection->clear();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	if (m_mw)
		m_mw->emitUpdateRequest(0);

	// Server marks for what was placed; the status line lists the rest.
	markPlaced(placed);
	QString line = tr("Placed %1: %2 obits (%3 with photo, %4 no photo), %5 news (%6 with photo)")
		.arg(placed.size()).arg(obitsPlaced).arg(obitsWithPhoto).arg(obitsPlaced - obitsWithPhoto).arg(newsPlaced).arg(newsWithPhoto);
	if (!m_statusLabel->text().isEmpty())
		line += QStringLiteral("\n") + m_statusLabel->text().section('\n', 0, 0);   // the server-mark result
	if (!photoProblems.isEmpty())
		line += QStringLiteral("\n") + tr("No picture: ") + photoProblems.mid(0, 3).join(QStringLiteral("; ")) + (photoProblems.size() > 3 ? QStringLiteral(" ...") : QString());
	QString tip;
	if (!photoProblems.isEmpty())
		tip = tr("Placed without their picture:") + QStringLiteral("\n\u2022 ") + photoProblems.join(QStringLiteral("\n\u2022 ")) + QStringLiteral("\n");
	if (!remaining.isEmpty())
	{
		QStringList t;
		for (const QString& s : remainingTitles)
			t << s.left(28);
		line += QStringLiteral("\n") + tr("Not placed (no room): %n story(ies): ", "", remaining.size()) + t.mid(0, 3).join(QStringLiteral(", ")) + (t.size() > 3 ? QStringLiteral(" ...") : QString());
		tip += tr("Not placed (no room on this page):") + QStringLiteral("\n\u2022 ") + t.join(QStringLiteral("\n\u2022 "));
	}
	m_statusLabel->setToolTip(tip);
	if (noFace > 0)
		line += QStringLiteral("\n") + tr("%n photo(s) without a detected face - see the orange 'check' flag", "", noFace);
	if (!missingStyles.isEmpty())
		line += QStringLiteral("\n") + tr("Missing paragraph styles: %1").arg(missingStyles.join(", "));
	m_statusLabel->setText(line);
	apiLog(QStringLiteral("obituary model: placed %1 (%2 obits, %3 with photo; %4 news, %5 with photo), remaining %6, noface %7, photo failures %8: %9").arg(placed.size()).arg(obitsPlaced).arg(obitsWithPhoto).arg(newsPlaced).arg(newsWithPhoto).arg(remaining.size()).arg(noFace).arg(m_obitPhotoFailed).arg(photoProblems.join("; ")));
}
