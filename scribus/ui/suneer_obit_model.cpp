#include "suneer_obit_model.h"
#include "pageitem_group.h"
#include "pageitem.h"
#include "scribusdoc.h"
#include "canvas.h"
#include <QPainter>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

#include "prefsmanager.h"
#include "scpaths.h"

QString SuneerObitModel::filePath()
{
	QString base = PrefsManager::instance().preferencesLocation();
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.config/scribus");
	while (base.endsWith('/'))
		base.chop(1);
	return base + QStringLiteral("/layout-models/obituary.json");
}

QJsonObject SuneerObitModel::toJson() const
{
	QJsonObject grid{
		{"columns", gridColumns}, {"gapMm", gridGapMm}, {"contentTopMm", contentTopMm},
		{"obitColumns", QJsonArray{obitFirstCol, obitLastCol}},
		{"newsColumns", QJsonArray{newsFirstCol, newsLastCol}}};
	QJsonObject module{
		{"photoMm", photoMm}, {"gapMm", moduleGapMm}, {"textTopDistPt", textTopDistPt},
		{"photoFill", photoFill},
		{"photoFillCMYK", QJsonArray{photoFillCMYK[0], photoFillCMYK[1], photoFillCMYK[2], photoFillCMYK[3]}}};
	QJsonObject styles{
		{"name", styleName}, {"place", stylePlace}, {"body", styleBody},
		{"nameNoPic", styleNameNoPic}, {"placeNoPic", stylePlaceNoPic}, {"bodyNoPic", styleBodyNoPic}};
	QJsonArray heads;
	for (const HeadRule& r : newsHeadlines)
		heads << QJsonObject{{"maxChars", r.maxChars}, {"style", r.style}};
	QJsonObject news{
		{"textColumns", newsTextColumns}, {"colGapMm", newsColGapMm}, {"gapMm", newsGapMm},
		{"headlineStyles", heads}, {"dateline", newsDateline}, {"firstBody", newsFirstBody}, {"body", newsBody}};
	QJsonObject classify{{"titleMaxWords", titleMaxWords}, {"titleMaxChars", titleMaxChars}, {"bodyMaxChars", bodyMaxChars}};
	QJsonObject photo{
		{"sizePx", photoPx}, {"quality", photoQuality}, {"background", photoBackground},
		{"bgCMYK", QJsonArray{bgCMYK[0], bgCMYK[1], bgCMYK[2], bgCMYK[3]}},
		{"bgRGB", QJsonArray{bgRGB[0], bgRGB[1], bgRGB[2]}},
		{"model", rembgModel}, {"bgModels", QJsonArray::fromStringList(bgModels)}, {"bgPolicy", bgPolicy},
		{"eyeY", eyeY}, {"faceH", faceH}, {"headTop", headTop}, {"faceW", faceW}, {"faceTop", faceTop},
		{"toneStrength", toneStrength}, {"frameShape", frameShape}, {"portraitHeightMm", portraitHeightMm},
		{"minPpi", minPpi}, {"flagColor", flagColor}};
	QJsonObject text{{"align", textAlign}, {"runInPlace", runInPlace}, {"placeRuninStyle", stylePlaceRunin}};
	return QJsonObject{
		{"name", "Obituary"}, {"version", 1},
		{"_comment", "Obituary layout model of the Scribus News Browser. Sizes in mm unless the key says Pt/Px. Measured from Page-15_ObitKRN.sla."},
		{"grid", grid}, {"module", module}, {"styles", styles}, {"text", text}, {"news", news}, {"classify", classify}, {"photo", photo}};
}

void SuneerObitModel::fromJson(const QJsonObject& o)
{
	auto num = [](const QJsonObject& ob, const char* k, double def) { return ob.contains(QLatin1String(k)) ? ob[QLatin1String(k)].toDouble(def) : def; };
	auto str = [](const QJsonObject& ob, const char* k, const QString& def) { const QString v = ob[QLatin1String(k)].toString(); return v.isEmpty() ? def : v; };
	const QJsonObject grid = o["grid"].toObject();
	gridColumns  = qBound(1, int(num(grid, "columns", gridColumns)), 30);
	gridGapMm    = num(grid, "gapMm", gridGapMm);
	contentTopMm = num(grid, "contentTopMm", contentTopMm);
	if (grid["obitColumns"].toArray().size() == 2)
	{
		obitFirstCol = grid["obitColumns"].toArray()[0].toInt(obitFirstCol);
		obitLastCol  = grid["obitColumns"].toArray()[1].toInt(obitLastCol);
	}
	if (grid["newsColumns"].toArray().size() == 2)
	{
		newsFirstCol = grid["newsColumns"].toArray()[0].toInt(newsFirstCol);
		newsLastCol  = grid["newsColumns"].toArray()[1].toInt(newsLastCol);
	}
	const QJsonObject module = o["module"].toObject();
	photoMm       = num(module, "photoMm", photoMm);
	moduleGapMm   = num(module, "gapMm", moduleGapMm);
	textTopDistPt = num(module, "textTopDistPt", textTopDistPt);
	photoFill     = str(module, "photoFill", photoFill);
	if (module["photoFillCMYK"].toArray().size() == 4)
		for (int i = 0; i < 4; ++i)
			photoFillCMYK[i] = qBound(0, module["photoFillCMYK"].toArray()[i].toInt(), 100);
	const QJsonObject styles = o["styles"].toObject();
	styleName       = str(styles, "name", styleName);
	stylePlace      = str(styles, "place", stylePlace);
	styleBody       = str(styles, "body", styleBody);
	styleNameNoPic  = str(styles, "nameNoPic", styleNameNoPic);
	stylePlaceNoPic = str(styles, "placeNoPic", stylePlaceNoPic);
	styleBodyNoPic  = str(styles, "bodyNoPic", styleBodyNoPic);
	const QJsonObject news = o["news"].toObject();
	newsTextColumns = qBound(1, int(num(news, "textColumns", newsTextColumns)), 6);
	newsColGapMm    = num(news, "colGapMm", newsColGapMm);
	newsGapMm       = num(news, "gapMm", newsGapMm);
	if (!news["headlineStyles"].toArray().isEmpty())
	{
		newsHeadlines.clear();
		for (const QJsonValue& v : news["headlineStyles"].toArray())
			newsHeadlines << HeadRule{v.toObject()["maxChars"].toInt(9999), v.toObject()["style"].toString()};
	}
	newsDateline  = str(news, "dateline", newsDateline);
	newsFirstBody = str(news, "firstBody", newsFirstBody);
	newsBody      = str(news, "body", newsBody);
	const QJsonObject classify = o["classify"].toObject();
	titleMaxWords = int(num(classify, "titleMaxWords", titleMaxWords));
	titleMaxChars = int(num(classify, "titleMaxChars", titleMaxChars));
	bodyMaxChars  = int(num(classify, "bodyMaxChars", bodyMaxChars));
	const QJsonObject photo = o["photo"].toObject();
	photoPx         = qBound(100, int(num(photo, "sizePx", photoPx)), 4000);
	photoQuality    = qBound(50, int(num(photo, "quality", photoQuality)), 100);
	photoBackground = str(photo, "background", photoBackground);
	rembgModel      = str(photo, "model", rembgModel);
	bgPolicy        = str(photo, "bgPolicy", bgPolicy);
	if (bgPolicy != QLatin1String("auto") && bgPolicy != QLatin1String("ask") && bgPolicy != QLatin1String("manual"))
		bgPolicy = QStringLiteral("auto");
	eyeY            = qBound(0.2, num(photo, "eyeY", eyeY), 0.6);
	faceH           = qBound(0.3, num(photo, "faceH", faceH), 0.8);
	headTop         = qBound(0.0, num(photo, "headTop", headTop), 0.2);
	faceW           = qBound(0.0, num(photo, "faceW", faceW), 0.9);
	faceTop         = qBound(0.0, num(photo, "faceTop", faceTop), 0.5);
	if (photo["bgModels"].isArray())
	{
		bgModels.clear();
		for (const QJsonValue& v : photo["bgModels"].toArray())
			if (!v.toString().isEmpty())
				bgModels << v.toString();
	}
	toneStrength    = qBound(0, int(num(photo, "toneStrength", toneStrength)), 100);
	frameShape      = str(photo, "frameShape", frameShape);
	portraitHeightMm = qBound(20.0, num(photo, "portraitHeightMm", portraitHeightMm), 60.0);
	minPpi          = qBound(72, int(num(photo, "minPpi", minPpi)), 1200);
	if (photo["bgCMYK"].toArray().size() == 4)
	{
		for (int i = 0; i < 4; ++i)
			bgCMYK[i] = qBound(0, photo["bgCMYK"].toArray()[i].toInt(), 100);
		// Earlier defaults (C0 M0 Y0 K12 of 2026-10-12, C3 M2 Y0 K27,
		// C3 M1 Y0 K22 and C2 M1 Y0 K20 of 2026-10-13) were replaced by
		// C2 M1 Y0 K27 (#B6B8B9, measured on the reference strip); an old
		// file still carrying one of them moves on. Any other value is the
		// user's.
		if (SuneerObitModel::isOldBgCMYK(bgCMYK))
		{
			bgCMYK[0] = 2; bgCMYK[1] = 1; bgCMYK[2] = 0; bgCMYK[3] = 27;
		}
	}
	if (photo["bgRGB"].toArray().size() == 3)
	{
		for (int i = 0; i < 3; ++i)
			bgRGB[i] = qBound(0, photo["bgRGB"].toArray()[i].toInt(), 255);
		if (SuneerObitModel::isOldBgRGB(bgRGB))   // #B5B8BB / #C2C6C7 defaults
		{
			bgRGB[0] = 182; bgRGB[1] = 184; bgRGB[2] = 185;
		}
	}
	flagColor       = str(photo, "flagColor", flagColor);
	const QJsonObject text = o["text"].toObject();
	textAlign       = str(text, "align", textAlign);
	runInPlace      = text.contains(QLatin1String("runInPlace")) ? text["runInPlace"].toBool(runInPlace) : runInPlace;
	stylePlaceRunin = str(text, "placeRuninStyle", stylePlaceRunin);
}

SuneerObitModel SuneerObitModel::load(QString* note)
{
	SuneerObitModel m;
	const QString path = filePath();
	QFile f(path);
	if (!f.exists())
	{
		QString err;
		if (m.save(&err) && note)
			*note = QStringLiteral("written with defaults: ") + path;
		else if (note)
			*note = err;
		return m;
	}
	if (!f.open(QIODevice::ReadOnly))
	{
		if (note)
			*note = QStringLiteral("cannot read ") + path;
		return m;
	}
	QJsonParseError pe;
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
	if (pe.error != QJsonParseError::NoError || !doc.isObject())
	{
		if (note)
			*note = QStringLiteral("%1: %2 (defaults used)").arg(path, pe.errorString());
		return m;
	}
	m.fromJson(doc.object());
	// Persist a migrated default (old Obit BG values) so the file shows the truth.
	const QJsonObject photo = doc.object()["photo"].toObject();
	const QJsonArray oldBg = photo["bgCMYK"].toArray();
	const QJsonArray oldRgb = photo["bgRGB"].toArray();
	int oldCmyk[4] { -1, -1, -1, -1 };
	if (oldBg.size() == 4)
		for (int i = 0; i < 4; ++i)
			oldCmyk[i] = oldBg[i].toInt();
	int oldRgbV[3] { -1, -1, -1 };
	if (oldRgb.size() == 3)
		for (int i = 0; i < 3; ++i)
			oldRgbV[i] = oldRgb[i].toInt();
	if (!photo.contains(QLatin1String("bgRGB")) || !photo.contains(QLatin1String("faceW")) || isOldBgCMYK(oldCmyk) || isOldBgRGB(oldRgbV))
		m.save();
	return m;
}

bool SuneerObitModel::isOldBgCMYK(const int* c)
{
	return (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 12)
	    || (c[0] == 3 && c[1] == 2 && c[2] == 0 && c[3] == 27)
	    || (c[0] == 3 && c[1] == 1 && c[2] == 0 && c[3] == 22)
	    || (c[0] == 2 && c[1] == 1 && c[2] == 0 && c[3] == 20);
}

bool SuneerObitModel::isOldBgRGB(const int* c)
{
	return (c[0] == 181 && c[1] == 184 && c[2] == 187)
	    || (c[0] == 194 && c[1] == 198 && c[2] == 199);
}

bool SuneerObitModel::save(QString* error) const
{
	const QString path = filePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error)
			*error = QStringLiteral("cannot write ") + path;
		return false;
	}
	f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
	return true;
}

// ---------------------------------------------------------------------------
// Flag overlay
// ---------------------------------------------------------------------------

namespace {
QString obitAttr(const PageItem* item, const QString& name)
{
	const ObjAttrVector* attrs = const_cast<PageItem*>(item)->getObjectAttributes();
	if (!attrs)
		return QString();
	for (const ObjectAttribute& a : *attrs)
		if (a.name == name)
			return a.value;
	return QString();
}

void paintFlagsIn(QPainter* painter, const Canvas* canvas, ScribusDoc* doc, const QList<PageItem*>& items, const QRectF& visible)
{
	for (PageItem* item : items)
	{
		if (!item)
			continue;
		if (item->isGroup())
		{
			paintFlagsIn(painter, canvas, doc, item->groupItemList, visible);
			continue;
		}
		if (!item->isImageFrame() || !doc->layerVisible(item->m_layerID))
			continue;
		const QString status = obitAttr(item, QStringLiteral("obit.photoStatus"));
		const bool manualMask = obitAttr(item, QStringLiteral("suneer.photoEditBgMode")) == QLatin1String("manual");
		const bool flagged = status == QLatin1String("noface") || status == QLatin1String("lowres") || status == QLatin1String("bg");
		if (!flagged && !manualMask)
			continue;
		QRectF br = item->getBoundingRect();
		if (!br.intersects(visible))
			continue;
		if (manualMask)
		{
			// small "M" badge, top right: the background mask is manual (never re-processed)
			QTransform tm;
			tm.scale(canvas->scale(), canvas->scale());
			tm.translate(-doc->minCanvasCoordinate.x(), -doc->minCanvasCoordinate.y());
			const QPointF tr = (item->getTransform() * tm).map(QPointF(item->width(), 0.0));
			painter->save();
			painter->resetTransform();
			QFont mf = painter->font();
			mf.setPixelSize(10);
			mf.setBold(true);
			painter->setFont(mf);
			const QRectF mb(tr.x() - 15, tr.y() + 1, 14, 13);
			painter->setPen(Qt::NoPen);
			painter->setBrush(QColor(21, 101, 192, 230));
			painter->drawRect(mb);
			painter->setPen(Qt::white);
			painter->drawText(mb, Qt::AlignCenter, QStringLiteral("M"));
			painter->restore();
		}
		if (!flagged)
			continue;
		QString text = obitAttr(item, QStringLiteral("obit.checkInfo"));
		if (text.isEmpty())
			text = status == QLatin1String("lowres") ? QStringLiteral("low res") : status == QLatin1String("bg") ? QStringLiteral("check BG") : QStringLiteral("no face");
		// top-left corner of the frame in widget pixels, then a fixed-size badge
		QTransform t;
		t.scale(canvas->scale(), canvas->scale());
		t.translate(-doc->minCanvasCoordinate.x(), -doc->minCanvasCoordinate.y());
		const QPointF tl = (item->getTransform() * t).map(QPointF(0.0, 0.0));
		painter->save();
		painter->resetTransform();
		QFont f = painter->font();
		f.setPixelSize(10);
		f.setBold(true);
		painter->setFont(f);
		const QFontMetrics fm(f);
		const int w = qMin(int(item->width() * canvas->scale()), fm.horizontalAdvance(text) + 8);
		const QRectF badge(tl.x(), tl.y(), qMax(w, 24), fm.height() + 2);
		painter->setRenderHint(QPainter::Antialiasing, false);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(255, 138, 0, 230));
		painter->drawRect(badge);
		painter->setPen(Qt::black);
		painter->drawText(badge.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(text, Qt::ElideRight, int(badge.width()) - 6));
		painter->restore();
	}
}
} // namespace

void suneerPaintObitFlags(QPainter* painter, const Canvas* canvas, ScribusDoc* doc)
{
	if (!painter || !canvas || !doc || doc->drawAsPreview || doc->masterPageMode())
		return;
	// the visible part of the canvas in document coordinates
	const FPoint a = canvas->localToCanvas(QPointF(0, 0));
	const FPoint b = canvas->localToCanvas(QPointF(canvas->width(), canvas->height()));
	const QRectF visible(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
	paintFlagsIn(painter, canvas, doc, doc->DocItems, visible);
}

// ---------------------------------------------------------------------------

SuneerObitPhotoWorker::SuneerObitPhotoWorker(QObject* parent) : QObject(parent)
{
}

SuneerObitPhotoWorker::~SuneerObitPhotoWorker()
{
	cancel();
}

QString SuneerObitPhotoWorker::scriptPath()
{
	QStringList candidates;
	candidates << ScPaths::instance().shareDir() + QStringLiteral("obit/suneer_obit_photo.py");
	candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../../resources/obit/suneer_obit_photo.py");
	candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/obit/suneer_obit_photo.py");
	for (const QString& c : candidates)
		if (QFileInfo::exists(c))
			return QFileInfo(c).absoluteFilePath();
	return candidates.first();
}

bool SuneerObitPhotoWorker::running() const
{
	return m_proc && m_proc->state() != QProcess::NotRunning;
}

void SuneerObitPhotoWorker::cancel()
{
	if (m_proc)
	{
		m_proc->disconnect(this);
		if (m_proc->state() != QProcess::NotRunning)
		{
			m_proc->kill();
			m_proc->waitForFinished(2000);
		}
		m_proc->deleteLater();
		m_proc = nullptr;
	}
}

bool SuneerObitPhotoWorker::start(const QList<Item>& items, const SuneerObitModel& model, const QColor& background, const QString& workDir, QString* why)
{
	cancel();
	m_results.clear();
	m_stderr.clear();
	m_done = false;
	const QString script = scriptPath();
	if (!QFileInfo::exists(script))
	{
		if (why)
			*why = QStringLiteral("photo script not installed: ") + script;
		return false;
	}
	QDir().mkpath(workDir);
	QJsonArray arr;
	for (const Item& it : items)
		arr << QJsonObject{{"id", it.id}, {"src", it.src}, {"out", it.out}, {"protect", it.protect}};
	const int outH = qRound(model.photoPx * model.photoFrameHeightMm() / model.photoMm);
	const int minPx = qRound(model.photoMm / 25.4 * model.minPpi);
	QJsonObject job{
		{"size", model.photoPx}, {"outH", outH}, {"quality", model.photoQuality},
		{"bg", QJsonArray{background.red(), background.green(), background.blue()}},
		{"model", model.rembgModel}, {"bgModels", QJsonArray::fromStringList(model.bgModels)}, {"bgPolicy", model.bgPolicy},
		{"eyeY", model.eyeY}, {"faceH", model.faceH}, {"headTop", model.headTop},
		{"faceW", model.faceW}, {"faceTop", model.faceTop},
		{"toneStrength", model.toneStrength}, {"minPx", minPx}, {"mm", model.photoMm},
		{"matting", false}, {"items", arr}};
	m_jobFile = workDir + QStringLiteral("/obit-photos.job.json");
	QFile jf(m_jobFile);
	if (!jf.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (why)
			*why = QStringLiteral("cannot write ") + m_jobFile;
		return false;
	}
	jf.write(QJsonDocument(job).toJson(QJsonDocument::Compact));
	jf.close();

	m_proc = new QProcess(this);
	m_proc->setProgram(QStringLiteral("python3"));
	m_proc->setArguments({QStringLiteral("-u"), script, QStringLiteral("--jobs"), m_jobFile});
	m_proc->setProcessChannelMode(QProcess::SeparateChannels);
	connect(m_proc, &QProcess::readyReadStandardOutput, this, &SuneerObitPhotoWorker::readOutput);
	connect(m_proc, &QProcess::readyReadStandardError, this, [this]() { m_stderr += QString::fromUtf8(m_proc->readAllStandardError()); });
	connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus st) {
		readOutput();
		const bool ok = (st == QProcess::NormalExit && code == 0 && m_done);
		QString err;
		if (!ok)
		{
			err = QStringLiteral("photo worker exit %1").arg(code);
			const QString tail = m_stderr.trimmed().section('\n', -3);
			if (!tail.isEmpty())
				err += QStringLiteral(": ") + tail;
		}
		emit finished(ok, err);
	});
	connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (m_proc && m_proc->state() == QProcess::NotRunning && !m_done)
			emit finished(false, QStringLiteral("python3 could not be started: ") + m_proc->errorString());
	});
	m_proc->start();
	return true;
}

void SuneerObitPhotoWorker::readOutput()
{
	if (!m_proc)
		return;
	while (m_proc->canReadLine())
	{
		const QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
		if (line == QLatin1String("DONE"))
		{
			m_done = true;
			continue;
		}
		if (!line.startsWith(QLatin1String("PROGRESS ")))
			continue;
		// PROGRESS {"i":..,"n":..,"id":..,"status":..,"out":..,"message":..}
		const QJsonObject o = QJsonDocument::fromJson(line.mid(9).toUtf8()).object();
		if (o.isEmpty())
			continue;
		Result r;
		r.id = o["id"].toString();
		r.status = o["status"].toString();
		r.out = o["out"].toString();
		r.message = o["message"].toString();
		if (r.status == QLatin1String("cached"))      // "status|ppi (WxH px)" from the sidecar
		{
			r.status = r.message.section('|', 0, 0).isEmpty() ? QStringLiteral("ok") : r.message.section('|', 0, 0);
			r.message = r.message.section('|', 1);
		}
		m_results << r;
		emit progress(o["i"].toInt(), o["n"].toInt(), r.id, r.status, r.message);
	}
}
