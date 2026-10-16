/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerphotobgdialog.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "pageitem.h"
#include "sccolorengine.h"
#include "scribusdoc.h"
#include "selection.h"
#include "suneerimagelinks.h"
#include "suneerphotoedit.h"
#include "suneer_obit_model.h"
#include "undomanager.h"
#include <QJsonDocument>
#include <QMessageBox>
#include "commonstrings.h"
#include "pageitem_group.h"

namespace {
QString attrOf(const PageItem* item, const QString& name)
{
	const QList<ObjectAttribute> l = item->getObjectAttributes(name);
	return l.isEmpty() ? QString() : l.first().value;
}
void setAttrOf(PageItem* item, const QString& name, const QString& value)
{
	ObjAttrVector attrs = *item->getObjectAttributes();
	for (int i = 0; i < attrs.size(); ++i)
		if (attrs[i].name == name)
		{
			attrs[i].value = value;
			item->setObjectAttributes(&attrs);
			return;
		}
	ObjectAttribute a;
	a.name = name; a.type = QStringLiteral("string"); a.value = value;
	attrs.append(a);
	item->setObjectAttributes(&attrs);
}
} // namespace

SuneerPhotoBgDialog::SuneerPhotoBgDialog(ScribusDoc* doc, const QList<PageItem*>& items, const QStringList& skipped, QWidget* parent)
	: QDialog(parent), m_doc(doc), m_skipped(skipped)
{
	setWindowTitle(tr("Photo background colour"));
	for (PageItem* it : items)
	{
		Photo ph;
		ph.item = it;
		ph.oldFile = it->Pfile;
		ph.manual = SuneerPhotoEdit::isManual(it);
		ph.cutPath = attrOf(it, QString::fromLatin1(SuneerPhotoEdit::AttrCut));
		if (ph.cutPath.isEmpty() || !QFileInfo::exists(ph.cutPath))
		{
			// the obituary script writes <out>.cut.png next to its output
			const QString out = attrOf(it, QStringLiteral("obit.out"));
			const QString cand = out.isEmpty() ? QString() : out.left(out.length() - 4) + QStringLiteral(".cut.png");
			ph.cutPath = (!cand.isEmpty() && QFileInfo::exists(cand)) ? cand : QString();
		}
		m_photos << ph;
	}
	// start colour: the Obit BG document colour, else the model default
	const SuneerObitModel model = SuneerObitModel::load();
	m_name = QStringLiteral("Obit BG");
	m_color = QColor(model.bgRGB[0], model.bgRGB[1], model.bgRGB[2]);
	if (!m_photos.isEmpty())
	{
		const SuneerPhotoEdit::Settings s = SuneerPhotoEdit::settingsOf(m_photos.first().item);
		if (!s.bgColor.isEmpty())
			m_color = QColor(s.bgColor);
		if (!s.bgName.isEmpty())
			m_name = s.bgName;
	}

	auto* vl = new QVBoxLayout(this);
	m_info = new QLabel(this);
	m_info->setWordWrap(true);
	QString info = tr("%n photo(s) with a removed background", "", m_photos.size());
	if (!m_skipped.isEmpty())
		info += QStringLiteral("\n") + tr("Skipped (no background removal / no cut-out): %1").arg(m_skipped.join(QStringLiteral(", ")));
	m_info->setText(info);
	vl->addWidget(m_info);
	auto* form = new QFormLayout();
	m_docCombo = new QComboBox(this);
	m_docCombo->addItem(tr("(custom)"), QString());
	for (auto it = doc->PageColors.constBegin(); it != doc->PageColors.constEnd(); ++it)
	{
		if (it.key() == CommonStrings::None)
			continue;
		QPixmap sw(14, 14);
		sw.fill(ScColorEngine::getRGBColor(it.value(), doc));
		m_docCombo->addItem(QIcon(sw), it.key(), it.key());
	}
	form->addRow(tr("Document colour:"), m_docCombo);
	m_nameEdit = new QLineEdit(m_name, this);
	m_nameEdit->setToolTip(tr("The document colour that gets this value and is used as the frame fill (created when missing)."));
	form->addRow(tr("Save as:"), m_nameEdit);
	auto* hexRow = new QHBoxLayout();
	m_hexEdit = new QLineEdit(this);
	m_hexEdit->setMaximumWidth(90);
	m_swatch = new QLabel(this);
	m_swatch->setFixedSize(60, 22);
	m_swatch->setFrameShape(QFrame::Box);
	m_pickBtn = new QPushButton(tr("Pick…"), this);
	m_pickBtn->setToolTip(tr("Colour dialog with a screen picker: take the colour from anywhere on the screen, the canvas included."));
	hexRow->addWidget(m_hexEdit); hexRow->addWidget(m_swatch); hexRow->addWidget(m_pickBtn); hexRow->addStretch(1);
	form->addRow(tr("Hex:"), hexRow);
	auto* rgbRow = new QHBoxLayout();
	const char* rgbNames[3] { "R", "G", "B" };
	for (int i = 0; i < 3; ++i)
	{
		m_rgb[i] = new QSpinBox(this);
		m_rgb[i]->setRange(0, 255);
		m_rgb[i]->setPrefix(QString::fromLatin1(rgbNames[i]) + QStringLiteral(" "));
		rgbRow->addWidget(m_rgb[i]);
	}
	form->addRow(tr("RGB:"), rgbRow);
	auto* cmykRow = new QHBoxLayout();
	const char* cmykNames[4] { "C", "M", "Y", "K" };
	for (int i = 0; i < 4; ++i)
	{
		m_cmyk[i] = new QSpinBox(this);
		m_cmyk[i]->setRange(0, 100);
		m_cmyk[i]->setSuffix(QStringLiteral("%"));
		m_cmyk[i]->setPrefix(QString::fromLatin1(cmykNames[i]) + QStringLiteral(" "));
		cmykRow->addWidget(m_cmyk[i]);
	}
	form->addRow(tr("CMYK:"), cmykRow);
	vl->addLayout(form);
	m_progress = new QProgressBar(this);
	m_progress->setVisible(false);
	vl->addWidget(m_progress);
	auto* btnRow = new QHBoxLayout();
	m_okBtn = new QPushButton(tr("OK"), this);
	m_okBtn->setDefault(true);
	m_cancelBtn = new QPushButton(tr("Cancel"), this);
	btnRow->addStretch(1); btnRow->addWidget(m_okBtn); btnRow->addWidget(m_cancelBtn);
	vl->addLayout(btnRow);

	connect(m_docCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SuneerPhotoBgDialog::onDocColor);
	connect(m_hexEdit, &QLineEdit::editingFinished, this, &SuneerPhotoBgDialog::onHex);
	for (auto* s : m_rgb)
		connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &SuneerPhotoBgDialog::onRgb);
	for (auto* s : m_cmyk)
		connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &SuneerPhotoBgDialog::onCmyk);
	connect(m_pickBtn, &QPushButton::clicked, this, &SuneerPhotoBgDialog::onPick);
	connect(m_okBtn, &QPushButton::clicked, this, &SuneerPhotoBgDialog::onOk);
	connect(m_cancelBtn, &QPushButton::clicked, this, &SuneerPhotoBgDialog::onCancel);
	m_previewTimer = new QTimer(this);
	m_previewTimer->setSingleShot(true);
	m_previewTimer->setInterval(400);
	connect(m_previewTimer, &QTimer::timeout, this, &SuneerPhotoBgDialog::previewNow);
	{
		const int di = m_docCombo->findData(m_name);
		QSignalBlocker b(m_docCombo);
		m_docCombo->setCurrentIndex(di < 0 ? 0 : di);
	}
	refreshFields(QString());
	m_okBtn->setEnabled(!m_photos.isEmpty());
}

SuneerPhotoBgDialog::~SuneerPhotoBgDialog()
{
	for (const QString& f : m_previewFiles)
		QFile::remove(f);
}

void SuneerPhotoBgDialog::setColor(const QColor& c, const QString& from)
{
	if (!c.isValid())
		return;
	m_color = c;
	refreshFields(from);
	schedulePreview();
}

void SuneerPhotoBgDialog::refreshFields(const QString& from)
{
	m_updating = true;
	if (from != QLatin1String("hex"))
		m_hexEdit->setText(m_color.name().toUpper());
	if (from != QLatin1String("rgb"))
	{
		m_rgb[0]->setValue(m_color.red()); m_rgb[1]->setValue(m_color.green()); m_rgb[2]->setValue(m_color.blue());
	}
	if (from != QLatin1String("cmyk"))
	{
		int c, m, y, k;
		m_color.getCmyk(&c, &m, &y, &k);
		m_cmyk[0]->setValue(qRound(c / 2.55)); m_cmyk[1]->setValue(qRound(m / 2.55)); m_cmyk[2]->setValue(qRound(y / 2.55)); m_cmyk[3]->setValue(qRound(k / 2.55));
	}
	m_swatch->setStyleSheet(QStringLiteral("background:%1;").arg(m_color.name()));
	m_updating = false;
}

void SuneerPhotoBgDialog::onDocColor(int idx)
{
	const QString name = m_docCombo->itemData(idx).toString();
	if (name.isEmpty() || !m_doc->PageColors.contains(name))
		return;
	m_nameEdit->setText(name);
	setColor(ScColorEngine::getRGBColor(m_doc->PageColors[name], m_doc), QStringLiteral("doc"));
}

void SuneerPhotoBgDialog::onHex()
{
	if (m_updating)
		return;
	const QColor c(m_hexEdit->text().trimmed().startsWith('#') ? m_hexEdit->text().trimmed() : QStringLiteral("#") + m_hexEdit->text().trimmed());
	if (c.isValid())
		setColor(c, QStringLiteral("hex"));
}

void SuneerPhotoBgDialog::onRgb()
{
	if (m_updating)
		return;
	setColor(QColor(m_rgb[0]->value(), m_rgb[1]->value(), m_rgb[2]->value()), QStringLiteral("rgb"));
}

void SuneerPhotoBgDialog::onCmyk()
{
	if (m_updating)
		return;
	setColor(QColor::fromCmyk(qRound(m_cmyk[0]->value() * 2.55), qRound(m_cmyk[1]->value() * 2.55), qRound(m_cmyk[2]->value() * 2.55), qRound(m_cmyk[3]->value() * 2.55)), QStringLiteral("cmyk"));
}

void SuneerPhotoBgDialog::onPick()
{
	QColorDialog dlg(m_color, this);
	dlg.setOption(QColorDialog::DontUseNativeDialog, true);
	dlg.setWindowTitle(tr("Photo background colour - use the screen picker to take a colour from the canvas"));
	if (dlg.exec() == QDialog::Accepted)
		setColor(dlg.selectedColor(), QStringLiteral("pick"));
}

QImage SuneerPhotoBgDialog::composite(const Photo& ph, const QColor& c) const
{
	if (ph.item && ph.manual)
	{
		// manual edit: the same crop and settings, only the background colour changes
		SuneerPhotoEdit::Settings s = SuneerPhotoEdit::settingsOf(ph.item);
		s.bgColor = c.name();
		const QImage src = SuneerPhotoEdit::loadSource(SuneerPhotoEdit::sourcePathOf(ph.item));
		QImage cut, brush;
		if (!ph.cutPath.isEmpty())
			cut = QImage(ph.cutPath).convertToFormat(QImage::Format_ARGB32);
		const QString bp = attrOf(ph.item, QString::fromLatin1(SuneerPhotoEdit::AttrBrush));
		if (!bp.isEmpty() && QFileInfo::exists(bp))
			brush = QImage(bp).convertToFormat(QImage::Format_Grayscale8);
		QImage protect;
		const QString pp = attrOf(ph.item, QString::fromLatin1(SuneerPhotoEdit::AttrProtect));
		if (!pp.isEmpty() && QFileInfo::exists(pp))
			protect = QImage(pp).convertToFormat(QImage::Format_Grayscale8);
		return SuneerPhotoEdit::render(src, cut, brush, s, false, nullptr, protect);
	}
	QImage cut(ph.cutPath);
	if (cut.isNull())
		return QImage();
	QImage out(cut.size(), QImage::Format_ARGB32);
	out.fill(c);
	QPainter p(&out);
	p.drawImage(0, 0, cut.convertToFormat(QImage::Format_ARGB32));
	p.end();
	return out;
}

void SuneerPhotoBgDialog::schedulePreview()
{
	m_previewTimer->start();
}

void SuneerPhotoBgDialog::previewNow()
{
	// Live preview: composites in temp files, loaded without undo; Cancel or
	// OK put the frames back first.
	if (m_photos.isEmpty())
		return;
	UndoManager::instance()->setUndoEnabled(false);
	m_progress->setVisible(true);
	m_progress->setRange(0, m_photos.size());
	int n = 0;
	for (const Photo& ph : m_photos)
	{
		m_progress->setValue(++n);
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		if (!ph.item)
			continue;
		const QImage img = composite(ph, m_color);
		if (img.isNull())
			continue;
		QString tmp = m_previewFiles.value(ph.oldFile);
		if (tmp.isEmpty())
		{
			tmp = QDir::tempPath() + QStringLiteral("/scribus_bgpreview_%1_%2.png").arg(QCoreApplication::applicationPid()).arg(n);
			m_previewFiles[ph.oldFile] = tmp;
		}
		img.save(tmp, "PNG");
		m_doc->loadPict(tmp, ph.item, true, true);
		ph.item->adjustPictScale();
		ph.item->update();
	}
	m_progress->setVisible(false);
	UndoManager::instance()->setUndoEnabled(true);
	m_previewed = true;
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerPhotoBgDialog::restoreAll()
{
	if (!m_previewed)
		return;
	UndoManager::instance()->setUndoEnabled(false);
	for (const Photo& ph : m_photos)
		if (ph.item && ph.item->Pfile != ph.oldFile)
		{
			m_doc->loadPict(ph.oldFile, ph.item, true, true);
			ph.item->adjustPictScale();
			ph.item->update();
		}
	UndoManager::instance()->setUndoEnabled(true);
	m_previewed = false;
	m_doc->regionsChanged()->update(QRectF());
}

void SuneerPhotoBgDialog::onCancel()
{
	m_previewTimer->stop();
	restoreAll();
	reject();
}

void SuneerPhotoBgDialog::onOk()
{
	m_previewTimer->stop();
	restoreAll();
	m_name = m_nameEdit->text().trimmed();
	if (m_name.isEmpty())
		m_name = QStringLiteral("Obit BG");
	// the named document colour gets this value (CMYK from the fields)
	ScColor col;
	col.setCmykColorF(m_cmyk[0]->value() / 100.0, m_cmyk[1]->value() / 100.0, m_cmyk[2]->value() / 100.0, m_cmyk[3]->value() / 100.0);
	m_doc->PageColors[m_name] = col;
	// remembered as the Obituary model default
	{
		SuneerObitModel model = SuneerObitModel::load();
		model.bgCMYK[0] = m_cmyk[0]->value(); model.bgCMYK[1] = m_cmyk[1]->value(); model.bgCMYK[2] = m_cmyk[2]->value(); model.bgCMYK[3] = m_cmyk[3]->value();
		model.bgRGB[0] = m_color.red(); model.bgRGB[1] = m_color.green(); model.bgRGB[2] = m_color.blue();
		model.photoBackground = m_name;
		model.save();
	}
	UndoTransaction tx;
	if (UndoManager::undoEnabled())
		tx = UndoManager::instance()->beginTransaction(Um::Selection, Um::IGroup, tr("Photo background colour"), QString(), Um::IGetImage);
	m_progress->setVisible(true);
	m_progress->setRange(0, m_photos.size());
	m_okBtn->setEnabled(false);
	int n = 0, done = 0;
	for (const Photo& ph : m_photos)
	{
		m_progress->setValue(++n);
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		if (!ph.item)
			continue;
		const QImage img = composite(ph, m_color);
		if (img.isNull())
			continue;
		const QString src = SuneerPhotoEdit::sourcePathOf(ph.item);
		const QString out = SuneerPhotoEdit::outputPathFor(m_doc, src, QStringLiteral("_bg"));
		QImage toSave = img.convertToFormat(QImage::Format_ARGB32);
		toSave.setDotsPerMeterX(qRound(503 / 0.0254));
		toSave.setDotsPerMeterY(qRound(503 / 0.0254));
		if (!toSave.save(out, "PNG"))
			continue;
		if (!m_doc->loadPict(out, ph.item, false, true))
			continue;
		ph.item->setImageScalingMode(false, true);
		ph.item->adjustPictScale();
		ph.item->setImageXYOffset(0, 0);
		SuneerImageLinks::embedPlaced(m_doc, ph.item);
		ph.item->setFillColor(m_name);
		if (ph.manual)
		{
			SuneerPhotoEdit::Settings s = SuneerPhotoEdit::settingsOf(ph.item);
			s.bgColor = m_color.name();
			s.bgName = m_name;
			setAttrOf(ph.item, QString::fromLatin1(SuneerPhotoEdit::AttrSettings), QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact)));
			setAttrOf(ph.item, QString::fromLatin1(SuneerPhotoEdit::AttrOutput), out);
		}
		if (!attrOf(ph.item, QStringLiteral("obit.out")).isEmpty())
			setAttrOf(ph.item, QStringLiteral("obit.out"), out);
		ph.item->update();
		++done;
	}
	if (tx)
		tx.commit();
	m_doc->changed();
	m_doc->regionsChanged()->update(QRectF());
	m_info->setText(tr("%1 of %2 photo(s) re-rendered with %3").arg(done).arg(m_photos.size()).arg(m_name));
	accept();
}

// ---------------------------------------------------------------------------

void SuneerPhotoEdit::openBackgroundDialog(ScribusDoc* doc, QWidget* parent)
{
	if (!doc)
		return;
	QList<PageItem*> items;
	QStringList skipped;
	auto consider = [&](PageItem* it) {
		if (!it)
			return;
		if (it->isGroup())
		{
			for (PageItem* c : it->groupItemList)
				if (c && c->isImageFrame())
					items << c;
			return;
		}
		if (it->isImageFrame())
			items << it;
	};
	if (doc->m_Selection->count() > 0)
		for (int i = 0; i < doc->m_Selection->count(); ++i)
			consider(doc->m_Selection->itemAt(i));
	else
	{
		for (int i = 0; i < doc->DocItems.count(); ++i)
		{
			PageItem* it = doc->DocItems.at(i);
			if (it && it->OwnPage == doc->currentPageNumber())
				consider(it);
		}
		items.erase(std::remove_if(items.begin(), items.end(), [](PageItem* it) { return !SuneerPhotoEdit::isObitPhoto(it); }), items.end());
	}
	QList<PageItem*> ok;
	for (PageItem* it : items)
	{
		const bool manualCut = !it->getObjectAttributes(QString::fromLatin1(AttrCut)).isEmpty() && QFileInfo::exists(it->getObjectAttributes(QString::fromLatin1(AttrCut)).first().value);
		const QString out = it->getObjectAttributes(QStringLiteral("obit.out")).isEmpty() ? QString() : it->getObjectAttributes(QStringLiteral("obit.out")).first().value;
		const bool scriptCut = !out.isEmpty() && out.endsWith(QLatin1String(".png")) && QFileInfo::exists(out.left(out.length() - 4) + QStringLiteral(".cut.png"));
		if (manualCut || scriptCut)
			ok << it;
		else
			skipped << it->itemName();
	}
	if (ok.isEmpty())
	{
		QMessageBox::information(parent, QObject::tr("Photo background colour"),
			items.isEmpty() ? QObject::tr("Select image frames (or nothing for every obituary photo of this page).")
			                : QObject::tr("None of the selected photos has a removed background with a cut-out (run Re-process photos or Edit photo first): %1").arg(skipped.join(QStringLiteral(", "))));
		return;
	}
	SuneerPhotoBgDialog dlg(doc, ok, skipped, parent);
	dlg.exec();
}
