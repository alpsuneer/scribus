#include "suneerposterstack.h"
#include "suneerfilltextimage.h"
#include "suneertexteffects.h"
#include "suneerpopout.h"

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <QUrlQuery>

#include "commonstrings.h"
#include "fpointarray.h"
#include "pageitem.h"
#include "pageitem_textframe.h"
#include "prefsmanager.h"
#include "scfonts.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "sclayer.h"
#include "selection.h"
#include "styles/paragraphstyle.h"
#include "ui/colorcombo.h"
#include "ui/layers.h"
#include "undomanager.h"
#include "undostate.h"
#include "util_math.h"
#include "util_formats.h"

namespace
{
const QString kAttr = QStringLiteral("SuneerPosterStack");
const double kScratchSize = 100.0;   // pt; the letters are drawn at this size and scaled afterwards

QString attributeValue(const PageItem* item, const QString& name)
{
	const ObjAttrVector* all = const_cast<PageItem*>(item)->getObjectAttributes();
	if (!all)
		return QString();
	for (const ObjectAttribute& a : *all)
		if (a.name == name)
			return a.value;
	return QString();
}

void setAttribute(PageItem* item, const QString& name, const QString& value)
{
	ObjAttrVector* all = item->getObjectAttributes();
	for (ObjectAttribute& a : *all)
		if (a.name == name) { a.value = value; return; }
	ObjectAttribute a;
	a.name = name;
	a.type = QStringLiteral("string");
	a.value = value;
	a.relationship = QStringLiteral("none");
	a.autoaddto = QStringLiteral("none");
	all->append(a);
}

// Same undo state Text Effects uses: PageItem::restore() knows SUNEER_ITEM_ATTRIBUTES.
void recordAttributes(PageItem* item, const ObjAttrVector& before)
{
	if (!UndoManager::undoEnabled())
		return;
	auto* is = new ScItemState<QPair<ObjAttrVector, ObjAttrVector> >(QObject::tr("Poster Stack settings"));
	is->set("SUNEER_ITEM_ATTRIBUTES");
	is->setItem(qMakePair(before, *item->getObjectAttributes()));
	UndoManager::instance()->action(item, is);
}

PageItem* itemNamed(ScribusDoc* doc, const QString& name)
{
	for (PageItem* candidate : std::as_const(*doc->Items))
		if (candidate->itemName() == name)
			return candidate;
	return nullptr;
}

bool isCombiningLike(QChar c)
{
	// Malayalam signs, virama, ZWJ/ZWNJ, anything Unicode calls a mark.
	if (c.unicode() == 0x200D || c.unicode() == 0x200C)
		return true;
	switch (c.category())
	{
		case QChar::Mark_NonSpacing:
		case QChar::Mark_SpacingCombining:
		case QChar::Mark_Enclosing:
			return true;
		default:
			return false;
	}
}

// Letters of \a text that \a face has no glyph for (spaces and controls ignored).
QString missingGlyphs(const ScFace& face, const QString& text)
{
	QString missing;
	if (face.isNone())
		return text;
	for (const QChar& c : text)
	{
		if (c.isSpace() || c.unicode() == 0x200D || c.unicode() == 0x200C || c.unicode() == 0x2029)
			continue;
		if (!face.canRender(c) && !missing.contains(c))
			missing += c;
	}
	return missing;
}

ScFace faceFor(ScribusDoc* doc, const QString& name, PageItem* fallbackFrame)
{
	SCFonts& fonts = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts;
	if (!name.isEmpty() && fonts.contains(name))
		return fonts[name];
	if (fallbackFrame && fallbackFrame->itemText.length() > 0)
		return fallbackFrame->itemText.charStyle(0).font();
	if (fallbackFrame)
		return fallbackFrame->itemText.defaultStyle().charStyle().font();
	return fonts.contains(doc->itemToolPrefs().textFont) ? fonts[doc->itemToolPrefs().textFont] : ScFace::none();
}

// The outline of one line, drawn at kScratchSize in the given font, letter
// spacing in tracking units. Same shaping as the page: a scratch text frame
// lays the line out and SuneerFillTextImage::lettersPath() collects the glyphs.
QPainterPath linePath(ScribusDoc* doc, const QString& line, const ScFace& face, double tracking)
{
	if (line.trimmed().isEmpty() || face.isNone())
		return QPainterPath();
	const QString fontName = face.scName();
	const bool wasUsed = doc->UsedFonts.contains(fontName);
	doc->AddFont(fontName, qRound(kScratchSize));

	std::unique_ptr<PageItem_TextFrame> scratch(new PageItem_TextFrame(doc, 0, 0, 100000.0, kScratchSize * 4.0, 0,
	                                                                   CommonStrings::None, CommonStrings::None));
	scratch->FrameType = PageItem::TextFrame;
	scratch->m_columns = 1;
	scratch->setTextToFrameDist(0, 0, 0, 0);
	ParagraphStyle style;
	style.charStyle().setFont(face);
	style.charStyle().setFontSize(kScratchSize * 10.0);
	style.charStyle().setTracking(tracking);
	style.setLineSpacingMode(ParagraphStyle::FixedLineSpacing);
	style.setLineSpacing(kScratchSize * 1.5);
	scratch->itemText.clear();
	scratch->itemText.insertChars(0, line);
	scratch->itemText.setDefaultStyle(style);
	scratch->SetRectFrame();
	scratch->setSampleItem(true);
	scratch->invalidateLayout();
	QPainterPath path = SuneerFillTextImage::lettersPath(scratch.get());
	scratch.reset();

	if (!wasUsed)
	{
		(*doc->AllFonts)[fontName].decreaseUsage();
		doc->UsedFonts.remove(fontName);
	}
	return path;
}
}

QString SuneerPosterStack::attributeName()
{
	return kAttr;
}

QString SuneerPosterStack::Settings::toString() const
{
	QUrlQuery q;
	q.addQueryItem("text", QString::fromLatin1(text.toUtf8().toPercentEncoding()));
	q.addQueryItem("autoSplit", QString::number(autoSplit));
	q.addQueryItem("charsPerLine", QString::number(charsPerLine));
	q.addQueryItem("lines", QString::number(lines));
	q.addQueryItem("font", QString::fromLatin1(font.toUtf8().toPercentEncoding()));
	q.addQueryItem("letterGap", QString::number(letterGap));
	q.addQueryItem("lineGap", QString::number(lineGap));
	q.addQueryItem("margin", QString::number(margin));
	q.addQueryItem("fillMode", QString::number(fillMode));
	q.addQueryItem("rowHeights", QString::number(rowHeights));
	q.addQueryItem("imageFile", QString::fromLatin1(imageFile.toUtf8().toPercentEncoding()));
	q.addQueryItem("backColor", QString::fromLatin1(backColor.toUtf8().toPercentEncoding()));
	return q.toString(QUrl::FullyEncoded);
}

SuneerPosterStack::Settings SuneerPosterStack::Settings::fromString(const QString& text)
{
	Settings s;
	QUrlQuery q(text);
	auto str = [&](const char* key, const QString& def) {
		return q.hasQueryItem(key) ? QString::fromUtf8(QByteArray::fromPercentEncoding(q.queryItemValue(key, QUrl::FullyEncoded).toLatin1())) : def;
	};
	auto num = [&](const char* key, double def) { return q.hasQueryItem(key) ? q.queryItemValue(key).toDouble() : def; };
	s.text = str("text", QString());
	s.autoSplit = int(num("autoSplit", Manual));
	s.charsPerLine = qMax(1, int(num("charsPerLine", 3)));
	s.lines = qMax(1, int(num("lines", 2)));
	s.font = str("font", QString());
	s.letterGap = num("letterGap", 0.0);
	s.lineGap = num("lineGap", 4.0);
	s.margin = num("margin", 0.0);
	s.fillMode = int(num("fillMode", Stretch));
	s.rowHeights = int(num("rowHeights", Equal));
	s.imageFile = str("imageFile", QString());
	s.backColor = str("backColor", QStringLiteral("None"));
	return s;
}

QStringList SuneerPosterStack::graphemeClusters(const QString& text)
{
	QStringList clusters;
	QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
	int start = 0;
	while (finder.toNextBoundary() >= 0)
	{
		const int end = finder.position();
		if (end <= start)
			continue;
		// Qt's grapheme rules may still cut after a virama (ക് | ഷ) or before a
		// ZWJ/mark; such a cut would break a conjunct, so the piece is glued to
		// the one before it.
		const bool glue = !clusters.isEmpty()
		                  && (clusters.last().endsWith(QChar(0x0D4D)) || isCombiningLike(text.at(start)));
		if (glue)
			clusters.last() += text.mid(start, end - start);
		else
			clusters.append(text.mid(start, end - start));
		start = end;
	}
	return clusters;
}

QStringList SuneerPosterStack::linesFor(const Settings& settings)
{
	QStringList lines;
	if (settings.autoSplit == Manual)
	{
		for (const QString& line : settings.text.split(QRegularExpression("[\\r\\n\\x{2029}]")))
			if (!line.trimmed().isEmpty())
				lines << line.trimmed();
		return lines;
	}
	// Auto split works on the words as one run, letters only.
	QString flat = settings.text;
	flat.replace(QRegularExpression("[\\r\\n\\x{2029}\\s]+"), QString());
	QStringList clusters = graphemeClusters(flat);
	if (clusters.isEmpty())
		return lines;
	int perLine = settings.charsPerLine;
	if (settings.autoSplit == Balanced)
		perLine = qMax(1, int(std::ceil(double(clusters.size()) / qMax(1, settings.lines))));
	for (int i = 0; i < clusters.size(); i += perLine)
	{
		QString line;
		for (int j = i; j < clusters.size() && j < i + perLine; ++j)
			line += clusters.at(j);
		lines << line;
	}
	return lines;
}

QPainterPath SuneerPosterStack::buildPath(ScribusDoc* doc, const Settings& settings, const QSizeF& frameSize, QString* error)
{
	auto fail = [error](const QString& why) { if (error) *error = why; return QPainterPath(); };
	const QStringList lines = linesFor(settings);
	if (lines.isEmpty())
		return fail(QObject::tr("There is no text for the poster."));
	const QRectF content(settings.margin, settings.margin,
	                     frameSize.width() - 2.0 * settings.margin, frameSize.height() - 2.0 * settings.margin);
	const int n = lines.size();
	const double rowsHeight = content.height() - (n - 1) * settings.lineGap;
	if (content.width() < 2.0 || rowsHeight < 2.0 * n)
		return fail(QObject::tr("The frame is too small for %n line(s) with these gaps and margin.", nullptr, n));

	ScFace face = faceFor(doc, settings.font, nullptr);
	if (face.isNone())
		return fail(QObject::tr("The font \"%1\" is not available.").arg(settings.font));

	// Row heights.
	QVector<double> weights(n, 1.0);
	if (settings.rowHeights == ByCharacters)
		for (int i = 0; i < n; ++i)
			weights[i] = qMax(1, int(graphemeClusters(lines.at(i)).size()));
	double weightSum = 0.0;
	for (double w : weights)
		weightSum += w;

	QPainterPath all;
	all.setFillRule(Qt::WindingFill);
	double y = content.top();
	for (int i = 0; i < n; ++i)
	{
		const double rowHeight = rowsHeight * weights[i] / weightSum;
		const QRectF row(content.left(), y, content.width(), rowHeight);
		y += rowHeight + settings.lineGap;

		// Pass 1 without letter spacing gives the natural width; from it the
		// spacing that ends up as exactly letterGap points after stretching.
		QPainterPath path = linePath(doc, lines.at(i), face, 0.0);
		if (path.isEmpty())
			continue;
		if (settings.letterGap != 0.0)
		{
			const int gaps = graphemeClusters(lines.at(i)).size() - 1;
			const double naturalWidth = path.boundingRect().width();
			if (gaps > 0 && naturalWidth > 0.0)
			{
				const double sx = (row.width() - gaps * settings.letterGap) / naturalWidth;
				if (sx > 0.0)
				{
					// tracking units: fontSize(1/10 pt) * tracking / 10000 = points
					const double trackingUnits = (settings.letterGap / sx) * 10000.0 / (kScratchSize * 10.0);
					QPainterPath spaced = linePath(doc, lines.at(i), face, trackingUnits);
					if (!spaced.isEmpty())
						path = spaced;
				}
			}
		}
		const QRectF box = path.boundingRect();   // real glyph box: no side bearings, no line height
		if (box.width() <= 0.0 || box.height() <= 0.0)
			continue;
		double sx = row.width() / box.width();
		double sy = row.height() / box.height();
		QTransform t;
		if (settings.fillMode == KeepProportions)
		{
			const double s = qMin(sx, sy);
			sx = sy = s;
			t.translate(row.left() + (row.width() - box.width() * s) / 2.0,
			            row.top() + (row.height() - box.height() * s) / 2.0);
		}
		else
			t.translate(row.left(), row.top());
		t.scale(sx, sy);
		t.translate(-box.left(), -box.top());
		all.addPath(t.map(path));
	}
	if (all.isEmpty())
		return fail(QObject::tr("The text has no outlines in this font."));
	return all.simplified();
}

PageItem* SuneerPosterStack::textFrameFor(ScribusDoc* doc, PageItem* item)
{
	if (!doc || !item)
		return nullptr;
	if (item->isTextFrame())
		return item;
	// A poster image frame is "<text frame name> image"; its text is parked.
	const QString suffix = QStringLiteral(" image");
	if (item->isImageFrame() && item->itemName().endsWith(suffix))
	{
		PageItem* text = itemNamed(doc, item->itemName().left(item->itemName().length() - suffix.length()));
		if (text && text->isTextFrame() && !attributeValue(text, kAttr).isEmpty())
			return text;
	}
	return nullptr;
}

bool SuneerPosterStack::canRunOn(const PageItem* item)
{
	return item && !item->isGroupChild() && (item->isTextFrame() || item->isImageFrame());
}

PageItem* SuneerPosterStack::apply(ScribusMainWindow* mw, PageItem* textFrame, const Settings& settings, QString* error)
{
	auto fail = [error](const QString& why) -> PageItem* { if (error) *error = why; return nullptr; };
	if (!mw || !mw->doc || !textFrame || !textFrame->isTextFrame())
		return fail(QObject::tr("Select one text frame first."));
	ScribusDoc* doc = mw->doc;
	if (doc->layerLocked(textFrame->m_layerID))
		return fail(QObject::tr("The layer of the text frame is locked."));
	if (textFrame->locked())
		return fail(QObject::tr("The text frame is locked."));
	if (textFrame->isGroupChild())
		return fail(QObject::tr("The text frame is inside a group. Ungroup it first."));
	if (linesFor(settings).isEmpty())
		return fail(QObject::tr("There is no text for the poster."));
	if (!QFileInfo(settings.imageFile).isReadable())
		return fail(QObject::tr("Cannot read the image file:\n%1").arg(settings.imageFile));
	QString pathError;
	QPainterPath letters = buildPath(doc, settings, QSizeF(textFrame->width(), textFrame->height()), &pathError);
	if (letters.isEmpty())
		return fail(pathError);

	const QString backName = textFrame->itemName() + QStringLiteral(" poster back");
	PageItem* oldBack = itemNamed(doc, backName);

	UndoTransaction transaction;
	if (UndoManager::undoEnabled())
		transaction = UndoManager::instance()->beginTransaction(textFrame->getUName(), textFrame->getUPixmap(),
		                                                        QObject::tr("Poster Stack"), QString(), nullptr);
	if (oldBack)
	{
		Selection gone(doc, false);
		gone.addItem(oldBack);
		doc->itemSelection_DeleteItem(&gone);
	}

	// The parked text shows the poster's words, so a later edit starts from them.
	const QString joined = linesFor(settings).join(QChar(0x2029));
	// plainText() gives the story with '\n' for paragraph ends; the story
	// itself wants PARSEP, or the restored words show box glyphs.
	QString oldWords = textFrame->itemText.plainText();
	oldWords.replace(QChar('\r'), QChar(0x2029)).replace(QChar('\n'), QChar(0x2029));
	if (oldWords != joined)
	{
		const ParagraphStyle keep = textFrame->itemText.defaultStyle();
		textFrame->itemText.clear();
		textFrame->itemText.insertChars(0, joined);
		textFrame->itemText.setDefaultStyle(keep);
		textFrame->invalidateLayout();
	}

	SuneerFillTextImage::Options options;
	options.imageFile = settings.imageFile;
	options.letters = letters;
	QString fillError;
	PageItem* image = SuneerFillTextImage::apply(mw, textFrame, options, &fillError);
	if (!image)
	{
		// Never undo(1) from inside an open transaction: the undo stack is
		// mid-transaction and the canvas redraw it triggers crashed (20:19
		// crash). Fill Text already took its own frame back; put the words
		// back by hand and drop the records.
		if (oldWords != joined)
		{
			const ParagraphStyle keep = textFrame->itemText.defaultStyle();
			textFrame->itemText.clear();
			textFrame->itemText.insertChars(0, oldWords);
			textFrame->itemText.setDefaultStyle(keep);
			textFrame->invalidateLayout();
		}
		if (transaction)
			transaction.cancel();
		doc->regionsChanged()->update(QRectF());
		return fail(fillError);
	}

	{
		const ObjAttrVector before = *textFrame->getObjectAttributes();
		setAttribute(textFrame, kAttr, settings.toString());
		recordAttributes(textFrame, before);
	}

	if (settings.backColor != CommonStrings::None && doc->PageColors.contains(settings.backColor))
	{
		const int z = doc->itemAdd(PageItem::Polygon, PageItem::Rectangle, textFrame->xPos(), textFrame->yPos(),
		                           textFrame->width(), textFrame->height(), 0.0, settings.backColor, CommonStrings::None);
		PageItem* back = doc->Items->at(z);
		back->setRotation(textFrame->rotation());
		back->setItemName(backName);
		back->m_layerID = image->m_layerID;
		doc->setRedrawBounding(back);
		// The new frame is on top; the letters go back above it (undoable).
		doc->m_Selection->delaySignalsOn();
		doc->m_Selection->clear();
		doc->m_Selection->addItem(image);
		doc->bringItemSelectionToFront();
		doc->m_Selection->delaySignalsOff();
	}

	if (transaction)
		transaction.commit();
	doc->m_Selection->clear();
	doc->m_Selection->addItem(image);
	doc->changed();
	doc->regionsChanged()->update(QRectF());
	if (mw->view)
		mw->view->DrawNew();
	return image;
}

void SuneerPosterStack::runForSelection(ScribusMainWindow* mw)
{
	if (!mw || !mw->HaveDoc || !mw->doc)
		return;
	ScribusDoc* doc = mw->doc;
	PageItem* selected = nullptr;
	QString selectedImageFile;
	for (int i = 0; i < doc->m_Selection->count(); ++i)
	{
		PageItem* it = doc->m_Selection->itemAt(i);
		if (!selected && textFrameFor(doc, it))
			selected = it;
		else if (it->isImageFrame() && it->imageIsAvailable && !it->Pfile.isEmpty())
			selectedImageFile = it->Pfile;
	}
	QPointer<PageItem> textFrame = textFrameFor(doc, selected);
	if (!textFrame)
	{
		QMessageBox::information(mw, QObject::tr("Poster Stack"), QObject::tr("Select one text frame first."));
		return;
	}
	if (doc->layerLocked(textFrame->m_layerID) || textFrame->locked())
	{
		QMessageBox::information(mw, QObject::tr("Poster Stack"), QObject::tr("The text frame or its layer is locked."));
		return;
	}

	QSettings stored("Faircode", "ScribusPosterStack");
	Settings s;
	const QString saved = attributeValue(textFrame, kAttr);
	if (!saved.isEmpty())
		s = Settings::fromString(saved);
	else
	{
		s.text = textFrame->itemText.plainText().replace(QChar(0x2029), QChar('\n')).replace(QChar('\r'), QChar('\n'));
		s.lineGap = stored.value("lineGap", 4.0).toDouble();
		s.letterGap = stored.value("letterGap", 0.0).toDouble();
		s.margin = stored.value("margin", 0.0).toDouble();
		s.fillMode = stored.value("fillMode", Stretch).toInt();
		s.rowHeights = stored.value("rowHeights", Equal).toInt();
		s.font = stored.value("font").toString();
	}
	if (!selectedImageFile.isEmpty())
		s.imageFile = selectedImageFile;

	// Fonts: the saved one, else a bold condensed face if there is one, else the frame's.
	SCFonts& fonts = PrefsManager::instance().appPrefs.fontPrefs.AvailFonts;
	QStringList fontNames = fonts.keys();
	fontNames.sort(Qt::CaseInsensitive);
	// A font chosen here must have glyphs for the words: a bold condensed Latin
	// face is useless for Malayalam, so the frame's own font wins then.
	if (s.font.isEmpty() || !fonts.contains(s.font) || !missingGlyphs(fonts[s.font], s.text).isEmpty())
	{
		s.font.clear();
		for (const QString& name : std::as_const(fontNames))
			if (name.contains("Condensed", Qt::CaseInsensitive) && name.contains("Bold", Qt::CaseInsensitive)
			    && missingGlyphs(fonts[name], s.text).isEmpty())
			{ s.font = name; break; }
		if (s.font.isEmpty())
			s.font = faceFor(doc, QString(), textFrame).scName();
	}

	QDialog dlg(mw);
	dlg.setObjectName("suneerPosterStackDialog");
	dlg.setWindowTitle(QObject::tr("Poster Stack"));
	auto* grid = new QGridLayout(&dlg);
	int r = 0;
	grid->addWidget(new QLabel(QObject::tr("Text (Enter = new line):"), &dlg), r, 0, Qt::AlignTop);
	auto* textEdit = new QPlainTextEdit(&dlg);
	textEdit->setObjectName("text");
	textEdit->setPlainText(s.text);
	textEdit->setMinimumSize(320, 70);
	grid->addWidget(textEdit, r, 1, 1, 3);
	++r;
	auto* splitBox = new QGroupBox(QObject::tr("Auto split"), &dlg);
	auto* splitLay = new QHBoxLayout(splitBox);
	auto* manualRb = new QRadioButton(QObject::tr("As typed"), splitBox);
	auto* perLineRb = new QRadioButton(QObject::tr("Characters per line:"), splitBox);
	auto* perLineSpin = new QSpinBox(splitBox);
	perLineSpin->setRange(1, 50);
	perLineSpin->setValue(s.charsPerLine);
	auto* balancedRb = new QRadioButton(QObject::tr("Balanced, lines:"), splitBox);
	auto* linesSpin = new QSpinBox(splitBox);
	linesSpin->setRange(1, 12);
	linesSpin->setValue(s.lines);
	splitLay->addWidget(manualRb);
	splitLay->addWidget(perLineRb);
	splitLay->addWidget(perLineSpin);
	splitLay->addWidget(balancedRb);
	splitLay->addWidget(linesSpin);
	(s.autoSplit == CharsPerLine ? perLineRb : s.autoSplit == Balanced ? balancedRb : manualRb)->setChecked(true);
	grid->addWidget(splitBox, r, 0, 1, 4);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Font:"), &dlg), r, 0);
	auto* fontCombo = new QComboBox(&dlg);
	fontCombo->setObjectName("font");
	fontCombo->addItems(fontNames);
	fontCombo->setCurrentText(s.font);
	grid->addWidget(fontCombo, r, 1, 1, 3);
	++r;
	auto makeSpin = [&](const char* name, double min, double max, double value) {
		auto* sp = new QDoubleSpinBox(&dlg);
		sp->setObjectName(name);
		sp->setRange(min, max);
		sp->setDecimals(2);
		sp->setSingleStep(0.5);
		sp->setSuffix(" pt");
		sp->setValue(value);
		return sp;
	};
	grid->addWidget(new QLabel(QObject::tr("Letter gap:"), &dlg), r, 0);
	auto* letterGapSpin = makeSpin("letterGap", -50.0, 200.0, s.letterGap);
	grid->addWidget(letterGapSpin, r, 1);
	grid->addWidget(new QLabel(QObject::tr("Line gap:"), &dlg), r, 2);
	auto* lineGapSpin = makeSpin("lineGap", 0.0, 500.0, s.lineGap);
	grid->addWidget(lineGapSpin, r, 3);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Outer margin:"), &dlg), r, 0);
	auto* marginSpin = makeSpin("margin", 0.0, 500.0, s.margin);
	grid->addWidget(marginSpin, r, 1);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Fill mode:"), &dlg), r, 0);
	auto* fillCombo = new QComboBox(&dlg);
	fillCombo->addItems({ QObject::tr("Stretch to fill (width and height separately)"), QObject::tr("Keep proportions, centred") });
	fillCombo->setCurrentIndex(s.fillMode == KeepProportions ? 1 : 0);
	grid->addWidget(fillCombo, r, 1, 1, 3);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Row heights:"), &dlg), r, 0);
	auto* rowsCombo = new QComboBox(&dlg);
	rowsCombo->addItems({ QObject::tr("Equal"), QObject::tr("Proportional to the number of characters") });
	rowsCombo->setCurrentIndex(s.rowHeights == ByCharacters ? 1 : 0);
	grid->addWidget(rowsCombo, r, 1, 1, 3);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Image:"), &dlg), r, 0);
	auto* fileEdit = new QLineEdit(s.imageFile, &dlg);
	fileEdit->setObjectName("imageFile");
	grid->addWidget(fileEdit, r, 1, 1, 2);
	auto* browse = new QPushButton(QObject::tr("Browse..."), &dlg);
	grid->addWidget(browse, r, 3);
	++r;
	grid->addWidget(new QLabel(QObject::tr("Background:"), &dlg), r, 0);
	auto* backCombo = new ColorCombo(false, &dlg);
	backCombo->setColors(doc->PageColors, true);
	backCombo->setCurrentColor(doc->PageColors.contains(s.backColor) ? s.backColor : CommonStrings::None);
	grid->addWidget(backCombo, r, 1, 1, 2);
	auto* previewChk = new QCheckBox(QObject::tr("Preview"), &dlg);
	previewChk->setChecked(stored.value("preview", true).toBool());
	grid->addWidget(previewChk, r, 3);
	++r;
	auto* popOutChk = new QCheckBox(QObject::tr("Pop-out subject after OK (the head comes in front of the letters)"), &dlg);
	popOutChk->setChecked(stored.value("popOut", false).toBool());
	grid->addWidget(popOutChk, r, 0, 1, 4);
	++r;
	auto* status = new QLabel(&dlg);
	status->setWordWrap(true);
	status->setMinimumHeight(40);
	grid->addWidget(status, r, 0, 1, 4);
	++r;
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	grid->addWidget(buttons, r, 0, 1, 4);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(browse, &QPushButton::clicked, &dlg, [&]() {
		const QString start = fileEdit->text().isEmpty() ? stored.value("lastFolder").toString() : QFileInfo(fileEdit->text()).absolutePath();
		const QString file = QFileDialog::getOpenFileName(&dlg, QObject::tr("Image to Show Through the Letters"), start,
			FormatsManager::instance()->fileDialogFormatList(FormatsManager::IMAGESIMGFRAME));
		if (!file.isEmpty())
			fileEdit->setText(file);
	});

	auto readSettings = [&]() {
		Settings v;
		v.text = textEdit->toPlainText();
		v.autoSplit = perLineRb->isChecked() ? CharsPerLine : balancedRb->isChecked() ? Balanced : Manual;
		v.charsPerLine = perLineSpin->value();
		v.lines = linesSpin->value();
		v.font = fontCombo->currentText();
		v.letterGap = letterGapSpin->value();
		v.lineGap = lineGapSpin->value();
		v.margin = marginSpin->value();
		v.fillMode = fillCombo->currentIndex() == 1 ? KeepProportions : Stretch;
		v.rowHeights = rowsCombo->currentIndex() == 1 ? ByCharacters : Equal;
		v.imageFile = fileEdit->text().trimmed();
		v.backColor = backCombo->currentColor();
		return v;
	};

	// ---- live preview: a scratch image frame in the letter shape, no undo, no
	// modified mark. The text frame is parked on the hidden layer meanwhile so
	// the black words do not show through; Cancel puts everything back.
	const bool docWasModified = doc->isModified();
	int parkLayer = doc->layerIDFromName(SuneerFillTextImage::originalTextLayerName());
	{
		UndoBlocker block;
		if (parkLayer < 0)
		{
			const int activeLayer = doc->activeLayer();
			parkLayer = doc->addLayer(SuneerFillTextImage::originalTextLayerName(), false);
			doc->setActiveLayer(activeLayer);
			// A layer without its "Send to Layer" action is a crash waiting in the
			// context menu (scrLayersActions[id] hands back a null pointer).
			mw->layerPalette->setDoc(doc);
			mw->rebuildLayersList();
		}
		doc->setLayerPrintable(parkLayer, false);
		doc->setLayerVisible(parkLayer, false);
	}
	const int textLayerBefore = textFrame->m_layerID;
	// A poster made earlier from this text (and its background) is hidden while
	// the preview is shown, so the new shape is not drawn over the old one.
	QList<QPair<QPointer<PageItem>, int> > hiddenOld;
	for (const QString& name : { textFrame->itemName() + QStringLiteral(" image"), textFrame->itemName() + QStringLiteral(" poster back") })
		if (PageItem* old = itemNamed(doc, name))
			hiddenOld.append(qMakePair(QPointer<PageItem>(old), old->m_layerID));
	QPointer<PageItem> preview;
	QString previewImage;
	auto removePreview = [&]() {
		UndoBlocker block;
		if (preview)
		{
			Selection gone(doc, false);
			gone.addItem(preview);
			doc->itemSelection_DeleteItem(&gone);
		}
		preview = nullptr;
		if (textFrame)
			textFrame->m_layerID = textLayerBefore;
		for (const auto& old : std::as_const(hiddenOld))
			if (old.first)
				old.first->m_layerID = old.second;
		doc->regionsChanged()->update(QRectF());
		doc->setModified(docWasModified);
	};
	auto updatePreview = [&]() {
		if (!textFrame)
			return;
		if (!previewChk->isChecked())
		{
			removePreview();
			return;
		}
		const Settings v = readSettings();
		QString why;
		QPainterPath letters = buildPath(doc, v, QSizeF(textFrame->width(), textFrame->height()), &why);
		if (why.isEmpty() && fonts.contains(v.font))
		{
			const QString missing = missingGlyphs(fonts[v.font], linesFor(v).join(QString()));
			if (!missing.isEmpty())
				why = QObject::tr("The font \"%1\" has no glyphs for: %2. Choose another font.").arg(v.font, missing);
		}
		status->setText(why);
		UndoBlocker block;
		if (letters.isEmpty())
		{
			removePreview();
			return;
		}
		const QRectF bounds = letters.boundingRect();
		QTransform frameToPage;
		frameToPage.translate(textFrame->xPos(), textFrame->yPos());
		frameToPage.rotate(textFrame->rotation());
		const QPointF origin = frameToPage.map(bounds.topLeft());
		if (!preview)
		{
			const int z = doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, origin.x(), origin.y(),
			                           bounds.width(), bounds.height(), 0.0, CommonStrings::None, CommonStrings::None);
			preview = doc->Items->at(z);
			preview->setItemName(QStringLiteral("__poster preview__"));
			preview->setRotation(textFrame->rotation());
			preview->m_layerID = textLayerBefore;
			previewImage.clear();
		}
		preview->setXYPos(origin.x(), origin.y());
		preview->setWidthHeight(bounds.width(), bounds.height());
		letters.translate(-bounds.topLeft());
		preview->PoLine.resize(0);
		preview->PoLine.fromQPainterPath(letters, true);
		preview->setFillEvenOdd(false);
		preview->ClipEdited = true;
		preview->FrameType = 3;
		preview->Clip = flattenPath(preview->PoLine, preview->Segments);
		preview->ContourLine = preview->PoLine.copy();
		if (QFileInfo(v.imageFile).isReadable())
		{
			if (previewImage != v.imageFile)
			{
				doc->loadPict(v.imageFile, preview, false, false);
				previewImage = v.imageFile;
			}
			if (preview->imageIsAvailable && preview->OrigW > 0 && preview->OrigH > 0)
			{
				const double scale = qMax(bounds.width() / preview->OrigW, bounds.height() / preview->OrigH);
				preview->setImageScalingMode(true, true);
				preview->setImageXYScale(scale, scale);
				preview->setImageXYOffset(((bounds.width() - preview->OrigW * scale) / 2.0) / scale,
				                          ((bounds.height() - preview->OrigH * scale) / 2.0) / scale);
			}
			preview->setFillColor(CommonStrings::None);
		}
		else
		{
			// No image yet: show the shape in grey.
			previewImage.clear();
			preview->setFillColor(doc->PageColors.contains("Black") ? QStringLiteral("Black") : CommonStrings::None);
			preview->setFillShade(40.0);
		}
		textFrame->m_layerID = parkLayer;
		for (const auto& old : std::as_const(hiddenOld))
			if (old.first)
				old.first->m_layerID = parkLayer;
		doc->setRedrawBounding(preview);
		preview->update();
		doc->regionsChanged()->update(QRectF());
		doc->setModified(docWasModified);
	};
	QTimer previewTimer;
	previewTimer.setSingleShot(true);
	previewTimer.setInterval(80);
	QObject::connect(&previewTimer, &QTimer::timeout, &dlg, updatePreview);
	auto schedule = [&]() { previewTimer.start(); };
	QObject::connect(textEdit, &QPlainTextEdit::textChanged, &dlg, schedule);
	for (QRadioButton* rb : { manualRb, perLineRb, balancedRb })
		QObject::connect(rb, &QRadioButton::toggled, &dlg, [&](bool) { schedule(); });
	for (QSpinBox* sp : { perLineSpin, linesSpin })
		QObject::connect(sp, qOverload<int>(&QSpinBox::valueChanged), &dlg, [&](int) { schedule(); });
	for (QDoubleSpinBox* sp : { letterGapSpin, lineGapSpin, marginSpin })
		QObject::connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [&](double) { schedule(); });
	for (QComboBox* cb : { fontCombo, fillCombo, rowsCombo })
		QObject::connect(cb, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int) { schedule(); });
	QObject::connect(fileEdit, &QLineEdit::textChanged, &dlg, [&](const QString&) { schedule(); });
	QObject::connect(previewChk, &QCheckBox::toggled, &dlg, [&](bool) { schedule(); });
	schedule();

	const int result = dlg.exec();
	previewTimer.stop();
	removePreview();
	stored.setValue("preview", previewChk->isChecked());
	if (result != QDialog::Accepted || !textFrame)
		return;

	Settings v = readSettings();
	stored.setValue("lineGap", v.lineGap);
	stored.setValue("letterGap", v.letterGap);
	stored.setValue("margin", v.margin);
	stored.setValue("fillMode", v.fillMode);
	stored.setValue("rowHeights", v.rowHeights);
	stored.setValue("font", v.font);
	if (!v.imageFile.isEmpty())
		stored.setValue("lastFolder", QFileInfo(v.imageFile).absolutePath());

	QString error;
	PageItem* made = apply(mw, textFrame, v, &error);
	if (!made)
	{
		QMessageBox::warning(mw, QObject::tr("Poster Stack"), error);
		return;
	}
	mw->setStatusBarInfoText(QObject::tr("Poster made. The words are kept on the hidden layer \"%1\"; run Poster Stack again on the poster to change them.")
	                         .arg(SuneerFillTextImage::originalTextLayerName()));
	stored.setValue("popOut", popOutChk->isChecked());
	if (popOutChk->isChecked())
		SuneerPopOut::runOn(mw, made);
}
