#include "suneerfilltextimage.h"
#include "suneertexteffects.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QTransform>

#include "commonstrings.h"
#include "fpointarray.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "selection.h"
#include "text/glyphcluster.h"
#include "text/textlayoutpainter.h"
#include "ui/colorcombo.h"
#include "ui/layers.h"
#include "undomanager.h"
#include "util.h"
#include "util_formats.h"

namespace
{
// Collects the outline of every glyph the layout draws into one path, in the
// text frame's own coordinates. Same placement as "Convert to Outlines"
// (TextToPathPainter), but one path instead of one polygon item per glyph.
class GlyphPathCollector : public TextLayoutPainter
{
public:
	explicit GlyphPathCollector(const PageItem* item) : m_item(item) {}
	QPainterPath path;

	void drawGlyph(const GlyphCluster& gc) override { add(gc); }
	void drawGlyphOutline(const GlyphCluster& gc, bool) override { add(gc); }
	void drawLine(const QPointF&, const QPointF&) override {}
	void drawRect(const QRectF&) override {}
	void drawObject(PageItem*) override {}
	void drawObjectDecoration(PageItem*) override {}

private:
	void add(const GlyphCluster& gc)
	{
		double currentX = 0.0;
		for (const GlyphLayout& gl : gc.glyphs())
		{
			if (gl.glyph < ScFace::CONTROL_GLYPHS)
			{
				FPointArray outline = font().glyphOutline(gl.glyph);
				if (outline.size() >= 4)   // a space has no outline; the next glyph still does
				{
					QTransform transform;
					if (m_item->imageFlippedH())
					{
						transform.translate(m_item->width(), 0);
						transform.scale(-1, 1);
					}
					if (m_item->imageFlippedV())
					{
						transform.translate(0, m_item->height());
						transform.scale(1, -1);
					}
					transform.translate(x() + gl.xoffset + currentX, y() + gl.yoffset);
					transform.translate(0, -(fontSize() * gl.scaleV));
					transform.scale(gl.scaleH * fontSize() / 10.0, gl.scaleV * fontSize() / 10.0);
					outline.map(transform);
					QPainterPath glyph = outline.toQPainterPath(true);
					glyph.setFillRule(Qt::WindingFill);
					path.addPath(glyph);
				}
			}
			currentX += gl.xadvance * gl.scaleH;
		}
	}

	const PageItem* m_item;
};
}

QString SuneerFillTextImage::originalTextLayerName()
{
	return QStringLiteral("Original text");
}

QPainterPath SuneerFillTextImage::lettersPath(PageItem* textFrame)
{
	if (!textFrame || !textFrame->isTextFrame())
		return QPainterPath();
	textFrame->layout();
	GlyphPathCollector collector(textFrame);
	textFrame->textLayout.render(&collector);
	collector.path.setFillRule(Qt::WindingFill);
	// One shape: letters that overlap (a vowel sign over its consonant) are united.
	return collector.path.simplified();
}

PageItem* SuneerFillTextImage::apply(ScribusMainWindow* mw, PageItem* textFrame, const Options& options, QString* error)
{
	auto fail = [error](const QString& why) -> PageItem* {
		if (error)
			*error = why;
		return nullptr;
	};
	if (!mw || !mw->doc || !textFrame || !textFrame->isTextFrame())
		return fail(QObject::tr("Select one text frame first."));
	ScribusDoc* doc = mw->doc;
	if (textFrame->isGroupChild())
		return fail(QObject::tr("The text frame is inside a group. Ungroup it, or enter the group and take the frame out, first."));
	if (textFrame->prevInChain() || textFrame->nextInChain())
		return fail(QObject::tr("The text frame is linked to other frames. Use a frame of its own for the headline."));
	if (textFrame->itemText.length() == 0 && options.letters.isEmpty())
		return fail(QObject::tr("The text frame is empty."));
	if (!QFileInfo(options.imageFile).isReadable())
		return fail(QObject::tr("Cannot read the image file:\n%1").arg(options.imageFile));

	// The outline of the text exactly as the page shows it (or the shape a
	// caller such as Poster Stack built from it).
	QPainterPath letters = options.letters.isEmpty() ? lettersPath(textFrame) : options.letters;
	const QRectF bounds = letters.boundingRect();
	if (letters.isEmpty() || bounds.width() < 1.0 || bounds.height() < 1.0)
		return fail(QObject::tr("The text has no outlines (only spaces, or the font has no glyphs for it)."));

	// The parking layer is made outside the undo step and its two flags are set
	// every time. Neither flag has an undo state, so a layer re-created by
	// "redo" came back visible, with the black original text on top of the image.
	int layerID = doc->layerIDFromName(originalTextLayerName());
	const int textLayer = textFrame->m_layerID;
	{
		const bool undoWasOn = UndoManager::undoEnabled();
		UndoManager::instance()->setUndoEnabled(false);
		if (layerID < 0)
		{
			const int activeLayer = doc->activeLayer();
			layerID = doc->addLayer(originalTextLayerName(), false);
			doc->setActiveLayer(activeLayer);
		}
		doc->setLayerPrintable(layerID, false);
		doc->setLayerVisible(layerID, false);
		UndoManager::instance()->setUndoEnabled(undoWasOn);
	}
	// Run again on a text frame that is already parked (the words were changed):
	// the image frame made from it last time is replaced, on the layer it was on.
	const QString imageName = textFrame->itemName() + QStringLiteral(" image");
	PageItem* oldImage = nullptr;
	if (textLayer == layerID)
	{
		for (PageItem* candidate : std::as_const(*doc->Items))
			if (candidate->itemName() == imageName && candidate->isImageFrame())
				oldImage = candidate;
	}
	int targetLayer = oldImage ? oldImage->m_layerID : textLayer;
	if (targetLayer == layerID)
	{
		// parked text without its image: the lowest layer that is not the parking layer
		for (const ScLayer& layer : std::as_const(doc->Layers))
			if (layer.ID != layerID && (targetLayer == layerID || layer.Level < doc->layerLevelFromID(targetLayer)))
				targetLayer = layer.ID;
	}

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(textFrame->getUName(), textFrame->getUPixmap(),
		                                                        QObject::tr("Fill Text with Image"), QString(), nullptr);
	if (oldImage)
	{
		// a Bevel & Emboss made on the old image goes with it
		SuneerTextEffects::deletePieces(doc, oldImage);
		Selection gone(doc, false);
		gone.addItem(oldImage);
		doc->itemSelection_DeleteItem(&gone);
	}

	// The image frame sits exactly where the letters are, turned like the text frame.
	QTransform frameToPage;
	frameToPage.translate(textFrame->xPos(), textFrame->yPos());
	frameToPage.rotate(textFrame->rotation());
	const QPointF origin = frameToPage.map(bounds.topLeft());
	const bool outline = options.outline && options.outlineWidth > 0.0 && doc->PageColors.contains(options.outlineColor);
	const int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, origin.x(), origin.y(), bounds.width(), bounds.height(),
	                           outline ? options.outlineWidth : 0.0, CommonStrings::None,
	                           outline ? options.outlineColor : CommonStrings::None);
	PageItem* image = doc->Items->at(z);
	letters.translate(-bounds.topLeft());
	image->PoLine.resize(0);
	image->PoLine.fromQPainterPath(letters, true);
	image->setFillEvenOdd(false);
	image->setRotation(textFrame->rotation());
	image->ClipEdited = true;
	image->FrameType = 3;
	image->OldB2 = image->width();
	image->OldH2 = image->height();
	image->Clip = flattenPath(image->PoLine, image->Segments);
	image->ContourLine = image->PoLine.copy();
	image->setTextFlowMode(textFrame->textFlowMode());
	image->setItemName(imageName);
	image->m_layerID = targetLayer;
	doc->setRedrawBounding(image);

	doc->loadPict(options.imageFile, image, false, false);
	if (!image->imageIsAvailable || image->OrigW <= 0 || image->OrigH <= 0)
	{
		// Take back the frame just made; the text frame was not touched yet.
		// Not with undo(1): a caller (Poster Stack) may hold an outer
		// transaction, and undoing inside an open transaction corrupts the
		// stack and crashed in the canvas redraw. Delete it quietly and drop
		// the records instead.
		{
			UndoBlocker block;
			Selection gone(doc, false);
			gone.addItem(image);
			doc->itemSelection_DeleteItem(&gone);
		}
		if (transaction)
			transaction.cancel();
		doc->regionsChanged()->update(QRectF());
		return fail(QObject::tr("The image could not be loaded:\n%1").arg(options.imageFile));
	}
	// Cover the letters, keep the proportions, centred: the smaller excess is cut off evenly on both sides.
	const double scale = qMax(bounds.width() / image->OrigW, bounds.height() / image->OrigH);
	image->setImageScalingMode(true, true);
	image->setImageXYScale(scale, scale);
	image->setImageXYOffset(((bounds.width() - image->OrigW * scale) / 2.0) / scale,
	                        ((bounds.height() - image->OrigH * scale) / 2.0) / scale);
	if (options.shadow)
	{
		image->setHasSoftShadow(true);
		image->setSoftShadowColor(doc->PageColors.contains("Black") ? QStringLiteral("Black") : CommonStrings::None);
		image->setSoftShadowXOffset(1.5);
		image->setSoftShadowYOffset(1.5);
		image->setSoftShadowBlurRadius(2.5);
	}

	// The original text goes to the layer that is neither shown nor printed.
	doc->m_Selection->delaySignalsOn();
	doc->m_Selection->clear();
	doc->m_Selection->addItem(textFrame);
	if (textLayer != layerID)
		doc->itemSelection_SendToLayer(layerID);
	doc->m_Selection->clear();
	doc->m_Selection->addItem(image);
	doc->m_Selection->delaySignalsOff();

	if (transaction)
		transaction.commit();

	mw->layerPalette->setDoc(doc);
	mw->rebuildLayersList();
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	if (mw->view)
		mw->view->DrawNew();
	return image;
}

void SuneerFillTextImage::runForSelection(ScribusMainWindow* mw)
{
	if (!mw || !mw->HaveDoc || !mw->doc)
		return;
	ScribusDoc* doc = mw->doc;
	PageItem* item = (doc->m_Selection->count() == 1) ? doc->m_Selection->itemAt(0) : nullptr;
	if (!item || !item->isTextFrame())
	{
		QMessageBox::information(mw, QObject::tr("Fill Text with Image"), QObject::tr("Select one text frame first."));
		return;
	}

	QSettings settings("Faircode", "ScribusFillTextImage");
	QDialog dlg(mw);
	dlg.setObjectName("suneerFillTextImageDialog");
	dlg.setWindowTitle(QObject::tr("Fill Text with Image"));
	auto* grid = new QGridLayout(&dlg);
	grid->addWidget(new QLabel(QObject::tr("Image:"), &dlg), 0, 0);
	auto* fileEdit = new QLineEdit(&dlg);
	fileEdit->setObjectName("imageFile");
	fileEdit->setMinimumWidth(340);
	grid->addWidget(fileEdit, 0, 1, 1, 2);
	auto* browse = new QPushButton(QObject::tr("Browse..."), &dlg);
	grid->addWidget(browse, 0, 3);
	auto* outlineChk = new QCheckBox(QObject::tr("Thin outline around the letters"), &dlg);
	outlineChk->setObjectName("outline");
	outlineChk->setChecked(settings.value("outline", false).toBool());
	grid->addWidget(outlineChk, 1, 0, 1, 2);
	auto* widthSpin = new QDoubleSpinBox(&dlg);
	widthSpin->setObjectName("outlineWidth");
	widthSpin->setRange(0.1, 20.0);
	widthSpin->setDecimals(2);
	widthSpin->setSingleStep(0.25);
	widthSpin->setSuffix(" pt");
	widthSpin->setValue(settings.value("outlineWidth", 0.5).toDouble());
	grid->addWidget(widthSpin, 1, 2);
	auto* colorCombo = new ColorCombo(false, &dlg);
	colorCombo->setObjectName("outlineColor");
	colorCombo->setColors(doc->PageColors, false);
	colorCombo->setCurrentColor(doc->PageColors.contains(settings.value("outlineColor").toString())
	                            ? settings.value("outlineColor").toString() : QStringLiteral("Black"));
	grid->addWidget(colorCombo, 1, 3);
	auto* shadowChk = new QCheckBox(QObject::tr("Drop shadow behind the letters"), &dlg);
	shadowChk->setObjectName("shadow");
	shadowChk->setChecked(settings.value("shadow", false).toBool());
	grid->addWidget(shadowChk, 2, 0, 1, 4);
	auto* note = new QLabel(QObject::tr("The text becomes an image frame in the shape of the letters. The text frame itself is kept on the "
	                                    "hidden, non-printing layer \"%1\": show that layer to change the words, then run this command again.")
	                        .arg(originalTextLayerName()), &dlg);
	note->setWordWrap(true);
	grid->addWidget(note, 3, 0, 1, 4);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	grid->addWidget(buttons, 4, 0, 1, 4);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(outlineChk, &QCheckBox::toggled, widthSpin, &QWidget::setEnabled);
	QObject::connect(outlineChk, &QCheckBox::toggled, colorCombo, &QWidget::setEnabled);
	widthSpin->setEnabled(outlineChk->isChecked());
	colorCombo->setEnabled(outlineChk->isChecked());
	QObject::connect(browse, &QPushButton::clicked, &dlg, [&]() {
		const QString start = fileEdit->text().isEmpty() ? settings.value("lastFolder").toString() : QFileInfo(fileEdit->text()).absolutePath();
		const QString file = QFileDialog::getOpenFileName(&dlg, QObject::tr("Image to Fill the Text With"), start,
			FormatsManager::instance()->fileDialogFormatList(FormatsManager::IMAGESIMGFRAME));
		if (!file.isEmpty())
			fileEdit->setText(file);
	});
	// The dialog is no use without a file, so ask for it straight away.
	QMetaObject::invokeMethod(browse, "click", Qt::QueuedConnection);
	if (dlg.exec() != QDialog::Accepted || fileEdit->text().trimmed().isEmpty())
		return;

	Options options;
	options.imageFile = fileEdit->text().trimmed();
	options.outline = outlineChk->isChecked();
	options.outlineWidth = widthSpin->value();
	options.outlineColor = colorCombo->currentColor();
	options.shadow = shadowChk->isChecked();
	settings.setValue("outline", options.outline);
	settings.setValue("outlineWidth", options.outlineWidth);
	settings.setValue("outlineColor", options.outlineColor);
	settings.setValue("shadow", options.shadow);
	settings.setValue("lastFolder", QFileInfo(options.imageFile).absolutePath());

	QString error;
	if (!apply(mw, item, options, &error))
	{
		QMessageBox::warning(mw, QObject::tr("Fill Text with Image"), error);
		return;
	}
	mw->setStatusBarInfoText(QObject::tr("Text filled with the image. The original text frame is on the hidden layer \"%1\".").arg(originalTextLayerName()));
}
