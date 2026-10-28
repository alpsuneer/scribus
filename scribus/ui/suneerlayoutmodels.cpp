/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerlayoutmodels.h"
#include "suneerpagemodel.h"
#include "suneerpagemodels.h"

#include <cmath>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "scribusdoc.h"
#include "scpage.h"
#include "scpaths.h"
#include "ui/suneer_obit_model.h"

namespace {
const double kMm = 72.0 / 25.4;
const double kVW = 560.0;   // diagram canvas (virtual units)
const double kVH = 470.0;

QStringList docColours(ScribusDoc* doc)
{
	QStringList out { QStringLiteral("Black") };
	if (doc)
		for (auto it = doc->PageColors.begin(); it != doc->PageColors.end(); ++it)
			if (!out.contains(it.key()))
				out << it.key();
	return out;
}

QStringList docParagraphStyles(ScribusDoc* doc)
{
	QStringList out;
	if (doc)
		for (int i = 0; i < doc->paragraphStyles().count(); ++i)
			out << doc->paragraphStyles()[i].name();
	if (out.isEmpty())
		out << QStringLiteral("16 M") << QStringLiteral("18 M") << QStringLiteral("20 M") << QStringLiteral("22 M") << QStringLiteral("24 M")
		    << QStringLiteral("28 M") << QStringLiteral("32 M") << QStringLiteral("36 M") << QStringLiteral("40 M") << QStringLiteral("48 M")
		    << QStringLiteral("68 B") << QStringLiteral("80 B");
	out.sort(Qt::CaseInsensitive);
	return out;
}

QDoubleSpinBox* mmSpin(QWidget* parent, double max = 100.0)
{
	auto* s = new QDoubleSpinBox(parent);
	s->setRange(0.0, max);
	s->setDecimals(1);
	s->setSingleStep(0.1);
	s->setSuffix(QStringLiteral(" mm"));
	s->setKeyboardTracking(false);
	return s;
}

QSpinBox* intSpin(QWidget* parent, int min, int max)
{
	auto* s = new QSpinBox(parent);
	s->setRange(min, max);
	s->setKeyboardTracking(false);
	return s;
}

const char* const kPhotoPos[] = { "right", "left", "middle", "top", "topAbove", "bottom", "none" };
QString photoPosLabel(const QString& k)
{
	if (k == QLatin1String("right")) return QObject::tr("Right, under the headline");
	if (k == QLatin1String("left")) return QObject::tr("Left, under the headline");
	if (k == QLatin1String("middle")) return QObject::tr("Middle, under the headline");
	if (k == QLatin1String("top")) return QObject::tr("Full width, under the headline");
	if (k == QLatin1String("topAbove")) return QObject::tr("Full width, above the headline");
	if (k == QLatin1String("bottom")) return QObject::tr("Below the text");
	return QObject::tr("No photo");
}
} // namespace

// ===========================================================================
// Spacing diagram
// ===========================================================================

SuneerSpacingDiagram::SuneerSpacingDiagram(QWidget* parent) : QWidget(parent)
{
	setMinimumSize(480, 420);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	const SuneerSpacing::Key keys[] = { SuneerSpacing::ModuleGapH, SuneerSpacing::ModuleGapV, SuneerSpacing::HeadlineBody, SuneerSpacing::PhotoText,
	                                    SuneerSpacing::WrapPad, SuneerSpacing::CaptionGap, SuneerSpacing::CaptionText, SuneerSpacing::Inset, SuneerSpacing::ColumnGap };
	for (SuneerSpacing::Key k : keys)
	{
		auto* s = mmSpin(this, 50.0);
		s->setToolTip(SuneerSpacing::keyTitle(k));
		s->setFixedWidth(78);
		s->setStyleSheet(QStringLiteral("QDoubleSpinBox { background: #FFFDE7; }"));
		connect(s, &QDoubleSpinBox::valueChanged, this, [this, k](double v) {
			if (m_loading)
				return;
			m_v.v[k] = v;
			placeEditors();
			update();
			emit changed();
		});
		m_spin.insert(k, s);
	}
	m_ruleWidth = new QComboBox(this);
	m_ruleWidth->setEditable(true);
	m_ruleWidth->addItem(tr("No rule"), 0.0);
	m_ruleWidth->addItem(QStringLiteral("0.25 pt"), 0.25);
	m_ruleWidth->addItem(QStringLiteral("0.5 pt"), 0.5);
	m_ruleWidth->setToolTip(tr("Separator rule between stacked modules (drawn in the middle of the vertical gap)"));
	m_ruleWidth->setFixedWidth(96);
	m_ruleColour = new QComboBox(this);
	m_ruleColour->setToolTip(tr("Colour of the separator rule"));
	m_ruleColour->setFixedWidth(120);
	auto ruleChanged = [this]() {
		if (m_loading)
			return;
		bool ok = false;
		double w = m_ruleWidth->currentData().toDouble(&ok);
		const QString t = m_ruleWidth->currentText();
		if (m_ruleWidth->findText(t) < 0 || !ok)
		{
			QString n = t;
			n.remove(QStringLiteral("pt")).replace(',', '.');
			w = n.trimmed().toDouble(&ok);
			if (!ok)
				w = 0.0;
		}
		m_v.v[SuneerSpacing::RuleWidth] = qBound(0.0, w, 5.0);
		m_v.ruleColor = m_ruleColour->currentText();
		update();
		emit changed();
	};
	connect(m_ruleWidth, &QComboBox::currentIndexChanged, this, ruleChanged);
	connect(m_ruleWidth->lineEdit(), &QLineEdit::editingFinished, this, ruleChanged);
	connect(m_ruleColour, &QComboBox::currentIndexChanged, this, ruleChanged);
}

void SuneerSpacingDiagram::setValues(const SuneerSpacing::Values& v, const QStringList& colours)
{
	m_loading = true;
	m_v = v;
	for (auto it = m_spin.begin(); it != m_spin.end(); ++it)
		it.value()->setValue(v.v[it.key()]);
	const double w = v.v[SuneerSpacing::RuleWidth];
	int ri = -1;
	for (int i = 0; i < m_ruleWidth->count(); ++i)
		if (qAbs(m_ruleWidth->itemData(i).toDouble() - w) < 0.001)
			ri = i;
	if (ri >= 0)
		m_ruleWidth->setCurrentIndex(ri);
	else
		m_ruleWidth->setEditText(QStringLiteral("%1 pt").arg(w));
	m_ruleColour->clear();
	m_ruleColour->addItems(colours);
	if (m_ruleColour->findText(v.ruleColor) < 0)
		m_ruleColour->addItem(v.ruleColor);
	m_ruleColour->setCurrentText(v.ruleColor);
	m_loading = false;
	placeEditors();
	update();
}

SuneerSpacingDiagram::Geo SuneerSpacingDiagram::geometry() const
{
	// gaps larger than life, growing with the value, so each stays visible
	auto g = [this](SuneerSpacing::Key k) { return qBound(9.0, 9.0 + m_v.v[k] * 5.0, 60.0); };
	Geo geo;
	const double top = 34;
	geo.neighbour = QRectF(16, top, 92, 300);
	const double mx = geo.neighbour.right() + g(SuneerSpacing::ModuleGapH);
	geo.module = QRectF(mx, top, kVW - 16 - mx, 300);
	const double in = m_v.v[SuneerSpacing::Inset] > 0 ? g(SuneerSpacing::Inset) : 0.0;
	geo.insetRect = geo.module.adjusted(in, in, -in, -in);
	geo.head = QRectF(geo.insetRect.left(), geo.insetRect.top(), geo.insetRect.width(), 44);
	const double bodyTop = geo.head.bottom() + g(SuneerSpacing::HeadlineBody);
	const double cg = g(SuneerSpacing::ColumnGap);
	const double cw = (geo.insetRect.width() - cg) / 2.0;
	geo.cols << QRectF(geo.insetRect.left(), bodyTop, cw, geo.insetRect.bottom() - bodyTop)
	         << QRectF(geo.insetRect.left() + cw + cg, bodyTop, cw, geo.insetRect.bottom() - bodyTop);
	const double wp = g(SuneerSpacing::WrapPad);
	geo.photo = QRectF(geo.cols[1].left() + wp, bodyTop + wp, geo.cols[1].width() - wp, 88);
	geo.caption = QRectF(geo.photo.left(), geo.photo.bottom() + qMax(3.0, g(SuneerSpacing::CaptionGap) - 6), geo.photo.width(), 22);
	const double belowTop = geo.module.bottom() + g(SuneerSpacing::ModuleGapV);
	geo.below = QRectF(geo.module.left(), belowTop, geo.module.width(), kVH - 8 - belowTop);

	geo.dims.insert(SuneerSpacing::ModuleGapH, QLineF(geo.neighbour.right(), top + 64, geo.module.left(), top + 64));
	geo.dims.insert(SuneerSpacing::ModuleGapV, QLineF(geo.module.left() + 40, geo.module.bottom(), geo.module.left() + 40, geo.below.top()));
	geo.dims.insert(SuneerSpacing::Inset, QLineF(geo.module.left(), geo.module.top() + 150, geo.insetRect.left() + (in > 0 ? 0 : 6), geo.module.top() + 150));
	geo.dims.insert(SuneerSpacing::HeadlineBody, QLineF(geo.cols[0].left() + 30, geo.head.bottom(), geo.cols[0].left() + 30, bodyTop));
	geo.dims.insert(SuneerSpacing::ColumnGap, QLineF(geo.cols[0].right(), geo.cols[0].bottom() - 26, geo.cols[1].left(), geo.cols[0].bottom() - 26));
	geo.dims.insert(SuneerSpacing::WrapPad, QLineF(geo.cols[1].left(), geo.photo.top() + 44, geo.photo.left(), geo.photo.top() + 44));
	geo.dims.insert(SuneerSpacing::CaptionGap, QLineF(geo.photo.right() - 14, geo.photo.bottom(), geo.photo.right() - 14, geo.caption.top()));
	const double textResume = geo.caption.bottom() + g(SuneerSpacing::CaptionText);
	geo.dims.insert(SuneerSpacing::CaptionText, QLineF(geo.photo.right() - 14, geo.caption.bottom(), geo.photo.right() - 14, textResume));
	const double bp = geo.below.top() + 8 + 46;
	geo.dims.insert(SuneerSpacing::PhotoText, QLineF(geo.below.left() + 150, bp, geo.below.left() + 150, bp + g(SuneerSpacing::PhotoText)));
	return geo;
}

void SuneerSpacingDiagram::placeEditors()
{
	const double s = qMin(width() / kVW, height() / kVH);
	const QPointF o((width() - kVW * s) / 2.0, (height() - kVH * s) / 2.0);
	auto map = [&](const QPointF& p) { return QPoint(qRound(o.x() + p.x() * s), qRound(o.y() + p.y() * s)); };
	const Geo geo = geometry();
	for (auto it = m_spin.begin(); it != m_spin.end(); ++it)
	{
		QDoubleSpinBox* sp = it.value();
		const QLineF l = geo.dims.value(it.key());
		const QPoint mid = map(l.center());
		const bool vertical = qAbs(l.dx()) < qAbs(l.dy()) || l.length() < 1;
		QPoint at;
		switch (it.key())
		{
			case SuneerSpacing::ModuleGapH: at = QPoint(mid.x() - sp->width() / 2, mid.y() + 8); break;
			case SuneerSpacing::Inset: at = QPoint(mid.x() + 4, mid.y() + 6); break;
			case SuneerSpacing::ColumnGap: at = QPoint(mid.x() - sp->width() / 2, mid.y() + 8); break;
			case SuneerSpacing::WrapPad: at = QPoint(mid.x() - sp->width() - 10, mid.y() - sp->height() / 2); break;
			case SuneerSpacing::CaptionGap:
			case SuneerSpacing::CaptionText: at = QPoint(mid.x() - sp->width() - 8, mid.y() - sp->height() / 2); break;
			default: at = vertical ? QPoint(mid.x() + 8, mid.y() - sp->height() / 2) : QPoint(mid.x() - sp->width() / 2, mid.y() + 6); break;
		}
		at.setX(qBound(0, at.x(), width() - sp->width()));
		at.setY(qBound(0, at.y(), height() - sp->height()));
		sp->move(at);
	}
	const QPoint rp = map(QPointF(16, geo.module.bottom() + 8));
	m_ruleWidth->move(rp);
	m_ruleColour->move(rp + QPoint(0, m_ruleWidth->height() + 4));
}

void SuneerSpacingDiagram::resizeEvent(QResizeEvent* e)
{
	QWidget::resizeEvent(e);
	placeEditors();
}

void SuneerSpacingDiagram::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), palette().base());
	const double s = qMin(width() / kVW, height() / kVH);
	p.translate((width() - kVW * s) / 2.0, (height() - kVH * s) / 2.0);
	p.scale(s, s);
	const Geo geo = geometry();
	const QColor text(0x9E, 0xA7, 0xB0), head(0x37, 0x47, 0x4F), frame(0x90, 0xA4, 0xAE), dim(0xD8, 0x43, 0x15);
	QFont f = font();
	f.setPointSizeF(7.5);
	p.setFont(f);

	auto lines = [&](const QRectF& r, double step, const QList<QRectF>& holes = QList<QRectF>()) {
		p.setPen(QPen(text, 2.2));
		for (double y = r.top() + 4; y < r.bottom() - 2; y += step)
		{
			double x0 = r.left() + 2, x1 = r.right() - 2;
			bool skip = false;
			for (const QRectF& h : holes)
				if (y >= h.top() && y <= h.bottom())
				{
					if (h.left() <= x0 + 2)
						skip = true;
					else
						x1 = qMin(x1, h.left() - 2);
				}
			if (!skip && x1 > x0 + 4)
				p.drawLine(QPointF(x0, y), QPointF(x1, y));
		}
	};
	// neighbour module (left)
	p.setPen(QPen(frame, 1, Qt::DashLine));
	p.setBrush(QColor(0xF4, 0xF6, 0xF7));
	p.drawRect(geo.neighbour);
	p.setPen(head);
	p.drawText(geo.neighbour.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignTop, tr("Neighbour"));
	lines(geo.neighbour.adjusted(0, 18, 0, 0), 7);
	// the sample module
	p.setBrush(Qt::white);
	p.setPen(QPen(frame, 1));
	p.drawRect(geo.module);
	if (geo.insetRect != geo.module)
	{
		p.setPen(QPen(frame, 1, Qt::DotLine));
		p.setBrush(Qt::NoBrush);
		p.drawRect(geo.insetRect);
	}
	// headline: two heavy bars
	p.setPen(Qt::NoPen);
	p.setBrush(head);
	p.drawRect(QRectF(geo.head.left(), geo.head.top() + 4, geo.head.width() * 0.92, 15));
	p.drawRect(QRectF(geo.head.left(), geo.head.top() + 24, geo.head.width() * 0.55, 15));
	// photo + caption, wrap area
	const double wp = geo.photo.left() - geo.cols[1].left();
	const QRectF wrapArea = QRectF(geo.photo.left() - wp, geo.photo.top() - wp, geo.photo.width() + wp, geo.caption.bottom() - geo.photo.top() + 2 * wp);
	lines(geo.cols[0], 7, { wrapArea });
	const double resume = geo.dims.value(SuneerSpacing::CaptionText).p2().y();
	lines(QRectF(geo.cols[1].left(), resume, geo.cols[1].width(), geo.cols[1].bottom() - resume), 7);
	p.setPen(QPen(dim.lighter(150), 1, Qt::DashLine));
	p.setBrush(Qt::NoBrush);
	p.drawRect(wrapArea);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xB0, 0xBE, 0xC5));
	p.drawRect(geo.photo);
	p.setPen(QPen(Qt::white, 1.5));
	p.drawLine(geo.photo.topLeft(), geo.photo.bottomRight());
	p.drawLine(geo.photo.topRight(), geo.photo.bottomLeft());
	p.setPen(head);
	p.drawText(geo.photo, Qt::AlignCenter, tr("Photo"));
	p.setPen(QPen(QColor(0x78, 0x90, 0x9C), 1.6));
	p.drawLine(QPointF(geo.caption.left() + 2, geo.caption.top() + 6), QPointF(geo.caption.right() - 4, geo.caption.top() + 6));
	p.drawLine(QPointF(geo.caption.left() + 2, geo.caption.top() + 14), QPointF(geo.caption.left() + geo.caption.width() * 0.6, geo.caption.top() + 14));
	// module below: photo on top, text under it (photo-to-text gap), the rule in the gap
	p.setPen(QPen(frame, 1, Qt::DashLine));
	p.setBrush(QColor(0xF4, 0xF6, 0xF7));
	p.drawRect(geo.below);
	const QLineF pt = geo.dims.value(SuneerSpacing::PhotoText);
	const QRectF belowPhoto(geo.below.left() + 8, geo.below.top() + 8, 120, pt.p1().y() - geo.below.top() - 8);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xB0, 0xBE, 0xC5));
	p.drawRect(belowPhoto);
	lines(QRectF(geo.below.left() + 8, pt.p2().y(), 120, geo.below.bottom() - pt.p2().y()), 7);
	p.setPen(head);
	p.drawText(QRectF(geo.below.left() + 180, geo.below.top() + 4, 200, 14), Qt::AlignLeft, tr("Module below (photo above text)"));
	if (m_v.v[SuneerSpacing::RuleWidth] > 0)
	{
		const double y = (geo.module.bottom() + geo.below.top()) / 2.0;
		p.setPen(QPen(QColor(0x21, 0x21, 0x21), qMax(0.8, m_v.v[SuneerSpacing::RuleWidth] * 2.0)));
		p.drawLine(QPointF(geo.module.left(), y), QPointF(geo.module.right(), y));
	}
	// dimension lines
	for (auto it = geo.dims.constBegin(); it != geo.dims.constEnd(); ++it)
	{
		const QLineF l = it.value();
		p.setPen(QPen(dim, 1.2));
		p.drawLine(l);
		const bool vertical = qAbs(l.dx()) < qAbs(l.dy());
		const double t = 4;
		if (vertical)
		{
			p.drawLine(QPointF(l.x1() - t, l.y1()), QPointF(l.x1() + t, l.y1()));
			p.drawLine(QPointF(l.x2() - t, l.y2()), QPointF(l.x2() + t, l.y2()));
		}
		else
		{
			p.drawLine(QPointF(l.x1(), l.y1() - t), QPointF(l.x1(), l.y1() + t));
			p.drawLine(QPointF(l.x2(), l.y2() - t), QPointF(l.x2(), l.y2() + t));
		}
	}
	p.setPen(head);
	f.setBold(true);
	p.setFont(f);
	p.drawText(QRectF(16, 4, kVW - 32, 20), Qt::AlignLeft | Qt::AlignVCenter, tr("Spacing (mm) - gaps drawn larger than life"));
}

// ===========================================================================
// Model editor
// ===========================================================================

SuneerLayoutModelEditor::SuneerLayoutModelEditor(ScribusDoc* doc, const QString& selectId, const PageHooks& hooks, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_hooks(hooks)
{
	setWindowTitle(tr("Layout Models"));
	resize(1460, 940);
	auto* root = new QHBoxLayout(this);

	// --- left: the models
	auto* left = new QVBoxLayout();
	m_list = new QListWidget(this);
	m_list->setMinimumWidth(230);
	m_list->setMaximumWidth(300);
	left->addWidget(new QLabel(tr("Models"), this));
	left->addWidget(m_list, 1);
	auto* bgrid = new QGridLayout();
	auto mk = [this](const QString& t, const QString& tip) { auto* b = new QPushButton(t, this); b->setToolTip(tip); b->setAutoDefault(false); return b; };
	QPushButton* newBtn = mk(tr("New"), tr("A new model with the Normal model's values"));
	QPushButton* dupBtn = mk(tr("Duplicate"), tr("A copy of the selected model"));
	QPushButton* renBtn = mk(tr("Rename"), tr("Rename the selected model"));
	m_deleteBtn = mk(tr("Delete"), tr("Delete the selected model (built-in models cannot be deleted)"));
	QPushButton* expBtn = mk(tr("Export..."), tr("Save the selected model as a .json file"));
	QPushButton* impBtn = mk(tr("Import..."), tr("Add a model from a .json file"));
	m_restoreBtn = mk(tr("Restore defaults"), tr("Put the shipped values back into this built-in model"));
	bgrid->addWidget(newBtn, 0, 0);
	bgrid->addWidget(dupBtn, 0, 1);
	bgrid->addWidget(renBtn, 1, 0);
	bgrid->addWidget(m_deleteBtn, 1, 1);
	bgrid->addWidget(expBtn, 2, 0);
	bgrid->addWidget(impBtn, 2, 1);
	bgrid->addWidget(m_restoreBtn, 3, 0, 1, 2);
	left->addLayout(bgrid);
	m_fileInfo = new QLabel(this);
	m_fileInfo->setWordWrap(true);
	m_fileInfo->setStyleSheet(QStringLiteral("color:#666; font-size:10px;"));
	m_fileInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
	left->addWidget(m_fileInfo);
	root->addLayout(left);

	// --- right
	auto* right = new QVBoxLayout();
	m_title = new QLabel(this);
	m_title->setStyleSheet(QStringLiteral("font-size:15px; font-weight:bold;"));
	right->addWidget(m_title);
	m_tabs = new QTabWidget(this);
	m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

	// grid & structure
	{
		auto* w = new QWidget(m_tabs);
		auto* g = new QGridLayout(w);
		m_columns = intSpin(w, 1, 24);
		m_colWidth = mmSpin(w, 200.0);
		m_colWidth->setSpecialValueText(tr("auto"));
		m_colWidth->setToolTip(tr("0 = from the page width, the margins and the gutter"));
		m_colWidthInfo = new QLabel(w);
		m_colWidthInfo->setStyleSheet(QStringLiteral("color:#666;"));
		m_gutter = mmSpin(w, 30.0);
		m_gutter->setToolTip(tr("The gap between grid columns = the horizontal gap between modules (also in the diagram)"));
		m_docMargins = new QCheckBox(tr("Page margins of the document"), w);
		m_fromGuides = new QCheckBox(tr("Columns from the page's column guides"), w);
		m_fromGuides->setToolTip(tr("When the page has automatic column guides, they give the column count (as Normal always did)"));
		const char* mnames[4] = { "Top", "Bottom", "Left", "Right" };
		g->addWidget(new QLabel(tr("Columns:")), 0, 0);
		g->addWidget(m_columns, 0, 1);
		g->addWidget(new QLabel(tr("Column width:")), 0, 2);
		g->addWidget(m_colWidth, 0, 3);
		g->addWidget(m_colWidthInfo, 0, 4);
		g->addWidget(new QLabel(tr("Gutter:")), 0, 5);
		g->addWidget(m_gutter, 0, 6);
		g->addWidget(m_fromGuides, 3, 4, 1, 3);
		g->addWidget(m_docMargins, 1, 0, 1, 2);
		for (int i = 0; i < 4; ++i)
			m_margin[i] = mmSpin(w, 200.0);
		auto* mrow = new QHBoxLayout();
		mrow->setSpacing(4);
		for (int i = 0; i < 4; ++i)
		{
			if (i > 0)
				mrow->addSpacing(10);
			mrow->addWidget(new QLabel(tr(mnames[i]) + QStringLiteral(":"), w));
			mrow->addWidget(m_margin[i]);
		}
		mrow->addStretch(1);
		g->addLayout(mrow, 1, 2, 1, 6);
		m_decks = intSpin(w, 0, 8);
		m_decks->setSpecialValueText(tr("free"));
		m_decks->setToolTip(tr("Horizontal decks under the lead: the modules of a deck share its top edge. 0 = free packing (each module as high up as it fits)."));
		m_leftRail = new QCheckBox(tr("Left rail (column 1 for briefs)"), w);
		m_rightRail = new QCheckBox(tr("Right rail (last column for briefs)"), w);
		m_leadPos = new QComboBox(w);
		m_leadPos->addItem(tr("No lead"), QStringLiteral("none"));
		m_leadPos->addItem(tr("Top left"), QStringLiteral("top-left"));
		m_leadPos->addItem(tr("Top centre"), QStringLiteral("top-centre"));
		m_leadPos->addItem(tr("Top right"), QStringLiteral("top-right"));
		m_leadPos->setToolTip(tr("Where the lead (the longest story) goes"));
		m_leadCols = intSpin(w, 1, 24);
		m_leadCols->setSuffix(tr(" col"));
		g->addWidget(new QLabel(tr("Decks:")), 2, 0);
		g->addWidget(m_decks, 2, 1);
		g->addWidget(m_leftRail, 2, 2, 1, 2);
		g->addWidget(m_rightRail, 2, 4, 1, 2);
		g->addWidget(new QLabel(tr("Lead:")), 3, 0);
		g->addWidget(m_leadPos, 3, 1);
		g->addWidget(new QLabel(tr("Lead width:")), 3, 2);
		g->addWidget(m_leadCols, 3, 3);
		g->setColumnStretch(7, 1);
		g->setRowStretch(4, 1);
		g->setVerticalSpacing(4);
		m_tabs->addTab(w, tr("Grid && structure"));
	}
	// stories & photos
	auto ruleTable = [this](QWidget* parent, const QStringList& heads) {
		auto* t = new QTableWidget(0, heads.size(), parent);
		t->setHorizontalHeaderLabels(heads);
		t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		t->verticalHeader()->hide();
		t->setMaximumHeight(96);
		t->setMinimumHeight(80);
		t->setMinimumWidth(220);
		connect(t, &QTableWidget::cellChanged, this, [this](int, int) { markDirty(); });
		return t;
	};
	auto tableBox = [this](QWidget* parent, const QString& title, QTableWidget* t) {
		auto* box = new QGroupBox(title, parent);
		auto* vl = new QVBoxLayout(box);
		vl->addWidget(t);
		auto* row = new QHBoxLayout();
		auto* add = new QToolButton(box);
		add->setText(QStringLiteral("+"));
		auto* del = new QToolButton(box);
		del->setText(QStringLiteral("−"));
		connect(add, &QToolButton::clicked, this, [this, t]() { t->insertRow(t->rowCount()); markDirty(); });
		connect(del, &QToolButton::clicked, this, [this, t]() { if (t->currentRow() >= 0) { t->removeRow(t->currentRow()); markDirty(); } });
		row->addWidget(add);
		row->addWidget(del);
		row->addStretch(1);
		vl->addLayout(row);
		return box;
	};
	{
		auto* w = new QWidget(m_tabs);
		auto* hl = new QHBoxLayout(w);
		auto* col1 = new QVBoxLayout();
		auto* f1 = new QFormLayout();
		m_fixedCols = intSpin(w, 0, 24);
		m_fixedCols->setSpecialValueText(tr("by length"));
		m_fixedCols->setToolTip(tr("Every story this many columns wide (0 = by body length, the table below)"));
		f1->addRow(tr("Story width:"), m_fixedCols);
		col1->addLayout(f1);
		m_widthRules = ruleTable(w, { tr("Body up to (chars)"), tr("Columns") });
		col1->addWidget(tableBox(w, tr("Story width by body length"), m_widthRules));
		hl->addLayout(col1);
		auto* col2 = new QVBoxLayout();
		m_headRules = ruleTable(w, { tr("From columns"), tr("Headline style") });
		col2->addWidget(tableBox(w, tr("Headline style by width (empty: News Browser style)"), m_headRules));
		auto* f2 = new QFormLayout();
		m_leadHead = new QComboBox(w);
		m_leadHead->setEditable(true);
		f2->addRow(tr("Lead headline:"), m_leadHead);
		col2->addLayout(f2);
		hl->addLayout(col2);
		auto* col3 = new QVBoxLayout();
		auto* f3 = new QFormLayout();
		m_fixedPhotoCols = intSpin(w, 0, 24);
		m_fixedPhotoCols->setSpecialValueText(tr("by story"));
		f3->addRow(tr("Photo width:"), m_fixedPhotoCols);
		m_photoPos = new QComboBox(w);
		for (const char* k : kPhotoPos)
			m_photoPos->addItem(photoPosLabel(QLatin1String(k)), QLatin1String(k));
		f3->addRow(tr("Photo position:"), m_photoPos);
		m_photoMaxH = mmSpin(w, 400.0);
		m_photoMaxH->setMinimum(10.0);
		f3->addRow(tr("Photo height at most:"), m_photoMaxH);
		col3->addLayout(f3);
		m_photoRules = ruleTable(w, { tr("From story columns"), tr("Photo columns") });
		col3->addWidget(tableBox(w, tr("Photo width by story width"), m_photoRules));
		hl->addLayout(col3);
		m_storiesTab = w;
		m_tabs->addTab(w, tr("Stories && photos"));
	}
	// obituary
	{
		m_obitTab = new QWidget(m_tabs);
		auto* g = new QGridLayout(m_obitTab);
		m_obitPhoto = mmSpin(m_obitTab, 100.0);
		m_obitShape = new QComboBox(m_obitTab);
		m_obitShape->addItem(tr("Square"), QStringLiteral("square"));
		m_obitShape->addItem(tr("Portrait"), QStringLiteral("portrait"));
		m_obitPortraitH = mmSpin(m_obitTab, 100.0);
		m_obitFaceW = intSpin(m_obitTab, 10, 100);
		m_obitFaceW->setSuffix(QStringLiteral(" %"));
		m_obitFaceW->setToolTip(tr("Face fill: face width / frame width"));
		m_obitFaceTop = intSpin(m_obitTab, 0, 60);
		m_obitFaceTop->setSuffix(QStringLiteral(" %"));
		m_obitFaceTop->setToolTip(tr("Face top below the frame top, in frame widths"));
		m_obitBg = new QComboBox(m_obitTab);
		m_obitBg->setEditable(true);
		m_obitAlign = new QComboBox(m_obitTab);
		m_obitAlign->addItem(tr("Centred"), QStringLiteral("centre"));
		m_obitAlign->addItem(tr("Left"), QStringLiteral("left"));
		m_obitRunin = new QCheckBox(tr("Place run in (bold + \":\")"), m_obitTab);
		for (int i = 0; i < 4; ++i)
			m_obitCols[i] = intSpin(m_obitTab, 1, 24);
		g->addWidget(new QLabel(tr("Photo size:")), 0, 0);
		g->addWidget(m_obitPhoto, 0, 1);
		g->addWidget(new QLabel(tr("Shape:")), 0, 2);
		g->addWidget(m_obitShape, 0, 3);
		g->addWidget(new QLabel(tr("Portrait height:")), 0, 4);
		g->addWidget(m_obitPortraitH, 0, 5);
		g->addWidget(new QLabel(tr("Face fill:")), 1, 0);
		g->addWidget(m_obitFaceW, 1, 1);
		g->addWidget(new QLabel(tr("Face top:")), 1, 2);
		g->addWidget(m_obitFaceTop, 1, 3);
		g->addWidget(new QLabel(tr("Background colour:")), 1, 4);
		g->addWidget(m_obitBg, 1, 5);
		g->addWidget(new QLabel(tr("Alignment:")), 2, 0);
		g->addWidget(m_obitAlign, 2, 1);
		g->addWidget(m_obitRunin, 2, 2, 1, 2);
		g->addWidget(new QLabel(tr("Obit columns:")), 3, 0);
		auto* oc = new QHBoxLayout();
		oc->addWidget(m_obitCols[0]);
		oc->addWidget(new QLabel(QStringLiteral("–")));
		oc->addWidget(m_obitCols[1]);
		g->addLayout(oc, 3, 1);
		g->addWidget(new QLabel(tr("Short news columns:")), 3, 2);
		auto* nc = new QHBoxLayout();
		nc->addWidget(m_obitCols[2]);
		nc->addWidget(new QLabel(QStringLiteral("–")));
		nc->addWidget(m_obitCols[3]);
		g->addLayout(nc, 3, 3);
		g->setColumnStretch(6, 1);
		g->setRowStretch(4, 1);
		m_tabs->addTab(m_obitTab, tr("Obituary"));
	}
	// gaps: every gap of a story module and between modules, mm, each with its house default
	{
		auto* w = new QWidget(m_tabs);
		auto* g = new QGridLayout(w);
		g->setVerticalSpacing(2);
		const QList<SuneerSpacing::Key> keys = SuneerSpacing::gapKeys();
		const int half = (keys.size() + 1) / 2;
		for (int i = 0; i < keys.size(); ++i)
		{
			const SuneerSpacing::Key k = keys[i];
			const int row = i % half, col = (i / half) * 4;
			QString src;
			const double house = SuneerSpacing::houseDefault(k, &src);
			auto* lbl = new QLabel(SuneerSpacing::gapLabel(k) + QLatin1Char(':'), w);
			auto* sp = mmSpin(w, 50.0);
			sp->setToolTip(SuneerSpacing::keyTitle(k));
			auto* reset = new QToolButton(w);
			reset->setText(QStringLiteral("\u21BA"));
			reset->setToolTip(tr("Reset to house default: %1 mm (%2)").arg(house, 0, 'f', 1).arg(src));
			connect(reset, &QToolButton::clicked, sp, [sp, house]() { sp->setValue(house); });
			connect(sp, &QDoubleSpinBox::valueChanged, this, [this, k](double v) { setGap(k, v); });
			g->addWidget(lbl, row, col);
			g->addWidget(sp, row, col + 1);
			g->addWidget(reset, row, col + 2);
			m_gapSpins.insert(k, sp);
		}
		auto* allHouse = new QPushButton(tr("All to house defaults"), w);
		allHouse->setAutoDefault(false);
		allHouse->setToolTip(tr("Every gap to the value measured from our own pages (resources/autolayout/autolayout-rules.json); a gap the rules do not measure takes the built-in default."));
		connect(allHouse, &QPushButton::clicked, this, [this]() {
			for (auto it = m_gapSpins.begin(); it != m_gapSpins.end(); ++it)
				it.value()->setValue(SuneerSpacing::houseDefault(SuneerSpacing::Key(it.key())));
		});
		g->addWidget(allHouse, half, 0, 1, 2);
		g->setColumnStretch(3, 1);
		g->setColumnStretch(7, 1);
		m_tabs->addTab(w, tr("Gaps"));
	}
	// page types / info
	{
		auto* w = new QWidget(m_tabs);
		auto* f = new QFormLayout(w);
		m_pageTypes = new QLineEdit(w);
		m_pageTypes->setPlaceholderText(tr("empty = every page"));
		m_pageTypes->setToolTip(tr("Page types or page names this model is for, comma separated (part of the name is enough, case does not matter): Pradeshikam, Obituary, Sports"));
		m_description = new QLineEdit(w);
		f->addRow(tr("Page types:"), m_pageTypes);
		f->addRow(tr("Description:"), m_description);
		m_tabs->addTab(w, tr("Page types"));
	}
	m_tabs->setMaximumHeight(250);
	right->addWidget(m_tabs);

	// diagram | preview
	auto* split = new QSplitter(Qt::Horizontal, this);
	m_diagram = new SuneerSpacingDiagram(split);
	split->addWidget(m_diagram);
	auto* pv = new QWidget(split);
	auto* pvl = new QVBoxLayout(pv);
	pvl->setContentsMargins(0, 0, 0, 0);
	auto* pvTitle = new QLabel(tr("Sample page (real text engine, Malayalam sample text)"), pv);
	pvTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
	pvl->addWidget(pvTitle);
	m_preview = new QLabel(pv);
	m_preview->setAlignment(Qt::AlignCenter);
	m_preview->setMinimumSize(300, 420);
	m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_preview->setStyleSheet(QStringLiteral("background:#ECEFF1;"));
	pvl->addWidget(m_preview, 1);
	m_previewInfo = new QLabel(pv);
	m_previewInfo->setWordWrap(true);
	pvl->addWidget(m_previewInfo);
	split->addWidget(pv);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	right->addWidget(split, 1);

	// bottom: page actions + save/close
	auto* bottom = new QHBoxLayout();
	m_liveChk = new QCheckBox(tr("Live spacing preview on this page"), this);
	m_liveChk->setToolTip(tr("Re-space the placed modules of the current page while you change the spacing (Close without saving puts the page back)."));
	auto* applyBtn = new QPushButton(tr("Apply spacing to this page"), this);
	applyBtn->setAutoDefault(false);
	applyBtn->setToolTip(tr("Re-space the placed modules of the current page with this model's spacing: vertical gaps, wrap padding, insets, column gaps, separator rules. Locked and pinned modules stay. One undo step."));
	m_pageInfo = new QLabel(this);
	m_pageInfo->setStyleSheet(QStringLiteral("color:#555;"));
	bottom->addWidget(m_liveChk);
	bottom->addWidget(applyBtn);
	bottom->addWidget(m_pageInfo, 1);
	m_saveBtn = new QPushButton(tr("Save"), this);
	m_saveBtn->setAutoDefault(false);
	auto* closeBtn = new QPushButton(tr("Close"), this);
	closeBtn->setAutoDefault(false);
	bottom->addWidget(m_saveBtn);
	bottom->addWidget(closeBtn);
	right->addLayout(bottom);
	root->addLayout(right, 1);

	const bool havePage = bool(m_hooks.apply);
	m_liveChk->setEnabled(havePage);
	applyBtn->setEnabled(havePage);
	if (!havePage)
		m_pageInfo->setText(tr("Open from the News Browser (⇕) to apply spacing to a page."));

	// preview machinery: the document's styles (when one is open), sample stories
	m_sample = SuneerLayout::samplePage(m_doc);
	m_measurer.reset(new SuneerLayout::Measurer(m_sample, 0.6));
	m_runner = new SuneerLayout::Runner(this);
	connect(m_runner, &SuneerLayout::Runner::progress, this, [this](SuneerLayout::Job* j) {
		m_previewInfo->setText(tr("Measuring %1 of %2 sample stories...").arg(j->done()).arg(j->total()));
	});
	connect(m_runner, &SuneerLayout::Runner::finished, this, [this](SuneerLayout::Job* j) {
		const int wpx = qMax(200, m_preview->width() - 8);
		const double ratio = m_sample.pageH / qMax(1.0, m_sample.pageW);
		const int w = qMin(wpx, (int) ((m_preview->height() - 8) / ratio));
		const qreal dpr = devicePixelRatioF();
		QImage img = SuneerLayout::renderPlan(j->plan(), m_sample, qRound(w * dpr), 0.6);
		img.setDevicePixelRatio(dpr);
		m_preview->setPixmap(QPixmap::fromImage(img));
		QString info = j->plan().fitLine();
		if (!m_doc)
			info += tr(" · no document open: built-in sample styles");
		if (!m_measurer->hasMalayalamFont() && !m_doc)
			info += tr(" · no Malayalam font found");
		m_previewInfo->setText(info);
		m_previewInfo->setStyleSheet(j->plan().allFit() ? QString() : QStringLiteral("color:#C62828;"));
	});
	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(350);
	connect(m_timer, &QTimer::timeout, this, &SuneerLayoutModelEditor::runPreview);
	m_liveTimer = new QTimer(this);
	m_liveTimer->setSingleShot(true);
	m_liveTimer->setInterval(400);
	connect(m_liveTimer, &QTimer::timeout, this, [this]() {
		if (!m_liveChk->isChecked() || !m_hooks.apply)
			return;
		if (!m_previewing && m_hooks.snapshot)
			m_hooks.snapshot();
		else if (m_previewing && m_hooks.restore)
			m_hooks.restore();
		m_previewing = true;
		const int n = m_hooks.apply(fromUi().spacing(), false);
		m_pageInfo->setText(tr("Page preview: %n module(s) re-spaced", "", n));
	});

	// every field marks the model changed and refreshes the preview
	auto dirtyOn = [this](QObject* o) {
		if (auto* s = qobject_cast<QSpinBox*>(o))
			connect(s, &QSpinBox::valueChanged, this, [this]() { markDirty(); });
		else if (auto* d = qobject_cast<QDoubleSpinBox*>(o))
			connect(d, &QDoubleSpinBox::valueChanged, this, [this]() { markDirty(); });
		else if (auto* c = qobject_cast<QCheckBox*>(o))
			connect(c, &QCheckBox::toggled, this, [this]() { markDirty(); });
		else if (auto* b = qobject_cast<QComboBox*>(o))
		{
			connect(b, &QComboBox::currentIndexChanged, this, [this]() { markDirty(); });
			if (b->isEditable())
				connect(b, &QComboBox::editTextChanged, this, [this]() { markDirty(); });
		}
		else if (auto* e = qobject_cast<QLineEdit*>(o))
			connect(e, &QLineEdit::textEdited, this, [this]() { markDirty(); });
	};
	for (QObject* o : QList<QObject*> { m_columns, m_fromGuides, m_colWidth, m_gutter, m_docMargins, m_margin[0], m_margin[1], m_margin[2], m_margin[3], m_decks,
	                                     m_leftRail, m_rightRail, m_leadPos, m_leadCols, m_fixedCols, m_leadHead, m_fixedPhotoCols, m_photoPos,
	                                     m_photoMaxH, m_pageTypes, m_description, m_obitPhoto, m_obitShape, m_obitPortraitH, m_obitFaceW,
	                                     m_obitFaceTop, m_obitBg, m_obitAlign, m_obitRunin, m_obitCols[0], m_obitCols[1], m_obitCols[2], m_obitCols[3] })
		dirtyOn(o);
	connect(m_docMargins, &QCheckBox::toggled, this, [this](bool on) { for (auto* s : m_margin) s->setEnabled(!on); });
	// the gutter is in the grid and in the diagram: one value
	connect(m_gutter, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (m_loading)
			return;
		SuneerSpacing::Values sv = m_diagram->values();
		if (qAbs(sv.v[SuneerSpacing::ModuleGapH] - v) > 0.001)
		{
			sv.v[SuneerSpacing::ModuleGapH] = v;
			m_loading = true;
			m_diagram->setValues(sv, docColours(m_doc));
			m_loading = false;
			syncGapSpins();
		}
	});
	connect(m_diagram, &SuneerSpacingDiagram::changed, this, [this]() {
		if (m_loading)
			return;
		const double h = m_diagram->values().v[SuneerSpacing::ModuleGapH];
		if (qAbs(m_gutter->value() - h) > 0.001)
		{
			m_loading = true;
			m_gutter->setValue(h);
			m_loading = false;
		}
		syncGapSpins();
		markDirty();
		if (m_liveChk->isChecked())
			m_liveTimer->start();
	});
	connect(m_liveChk, &QCheckBox::toggled, this, [this](bool on) {
		if (on)
			m_liveTimer->start();
		else if (m_previewing && m_hooks.restore)
		{
			m_hooks.restore();
			m_previewing = false;
			m_pageInfo->setText(tr("Page restored"));
		}
	});
	connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur, QListWidgetItem* prev) {
		if (m_loading || !cur)
			return;
		const QString id = cur->data(Qt::UserRole).toString();
		if (id == m_id)
			return;
		if (!maybeSave())
		{
			m_loading = true;
			m_list->setCurrentItem(prev);
			m_loading = false;
			return;
		}
		selectModel(id);
	});
	connect(newBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onNew);
	connect(dupBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onDuplicate);
	connect(renBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onRename);
	connect(m_deleteBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onDelete);
	connect(expBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onExport);
	connect(impBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onImport);
	connect(m_restoreBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::onRestore);
	connect(applyBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::applyToPage);
	connect(m_saveBtn, &QPushButton::clicked, this, [this]() { saveCurrent(); });
	connect(closeBtn, &QPushButton::clicked, this, &SuneerLayoutModelEditor::reject);

	// headline style choices
	const QStringList styles = docParagraphStyles(m_doc);
	m_leadHead->addItem(QString());
	m_leadHead->addItems(styles);
	m_obitBg->addItems(docColours(m_doc));
	if (m_obitBg->findText(QStringLiteral("Obit BG")) < 0)
		m_obitBg->addItem(QStringLiteral("Obit BG"));

	QString first = selectId;
	if (first.isEmpty() || !SuneerLayoutModels::exists(first))
		first = QStringLiteral("normal");
	fillList(first);
	selectModel(first);
}

SuneerLayoutModelEditor::~SuneerLayoutModelEditor()
{
	if (m_runner)
		m_runner->clear();
}

void SuneerLayoutModelEditor::keyPressEvent(QKeyEvent* e)
{
	// Enter confirms a value (and previews it); it never closes the dialog
	if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !qobject_cast<QPushButton*>(focusWidget()))
	{
		e->accept();
		return;
	}
	QDialog::keyPressEvent(e);
}

void SuneerLayoutModelEditor::fillList(const QString& select)
{
	m_loading = true;
	m_list->clear();
	for (const SuneerLayoutModel& m : SuneerLayoutModels::all())
	{
		QString label = m.name;
		if (m.builtIn())
			label += tr("  (built-in)");
		else if (m.origin == QLatin1String("archive"))
			label += tr("  (archive)");
		auto* it = new QListWidgetItem(label, m_list);
		it->setData(Qt::UserRole, m.id);
		it->setToolTip(m.description.isEmpty() ? m.id : m.description);
		if (m.id == select)
			m_list->setCurrentItem(it);
	}
	m_loading = false;
}

void SuneerLayoutModelEditor::selectModel(const QString& id)
{
	m_id = id;
	m_model = SuneerLayoutModels::savedById(id);
	loadUi(m_model);
	m_dirty = false;
	m_saveBtn->setEnabled(false);
	schedulePreview();
}

void SuneerLayoutModelEditor::loadUi(const SuneerLayoutModel& m)
{
	m_loading = true;
	m_title->setText(m.name + (m.builtIn() ? tr("  — built-in") : QString()));
	m_deleteBtn->setEnabled(!m.builtIn());
	m_restoreBtn->setEnabled(m.builtIn());
	m_fileInfo->setText(tr("File: %1").arg(SuneerLayoutModels::filePath(m.id)));
	m_columns->setValue(m.columns);
	m_fromGuides->setChecked(m.gridFromGuides);
	m_colWidth->setValue(m.columnWidthMm);
	m_gutter->setValue(m.gutterMm);
	m_docMargins->setChecked(m.useDocMargins);
	m_margin[0]->setValue(m.marginTopMm);
	m_margin[1]->setValue(m.marginBottomMm);
	m_margin[2]->setValue(m.marginLeftMm);
	m_margin[3]->setValue(m.marginRightMm);
	for (auto* s : m_margin)
		s->setEnabled(!m.useDocMargins);
	m_decks->setValue(m.decks);
	m_leftRail->setChecked(m.leftRail);
	m_rightRail->setChecked(m.rightRail);
	m_leadPos->setCurrentIndex(qMax(0, m_leadPos->findData(m.leadPosition)));
	m_leadCols->setValue(m.leadColumns);
	m_fixedCols->setValue(m.fixedColumns);
	m_widthRules->setRowCount(0);
	for (const SuneerLayoutModel::WidthRule& r : m.widthRules)
	{
		const int row = m_widthRules->rowCount();
		m_widthRules->insertRow(row);
		m_widthRules->setItem(row, 0, new QTableWidgetItem(QString::number(r.maxChars)));
		m_widthRules->setItem(row, 1, new QTableWidgetItem(QString::number(r.columns)));
	}
	m_headRules->setRowCount(0);
	for (const SuneerLayoutModel::HeadRule& r : m.headlineRules)
	{
		const int row = m_headRules->rowCount();
		m_headRules->insertRow(row);
		m_headRules->setItem(row, 0, new QTableWidgetItem(QString::number(r.columns)));
		m_headRules->setItem(row, 1, new QTableWidgetItem(r.style));
	}
	m_leadHead->setCurrentText(m.leadHeadlineStyle);
	m_fixedPhotoCols->setValue(m.fixedPhotoColumns);
	m_photoRules->setRowCount(0);
	for (const SuneerLayoutModel::PhotoRule& r : m.photoRules)
	{
		const int row = m_photoRules->rowCount();
		m_photoRules->insertRow(row);
		m_photoRules->setItem(row, 0, new QTableWidgetItem(QString::number(r.storyColumns)));
		m_photoRules->setItem(row, 1, new QTableWidgetItem(QString::number(r.photoColumns)));
	}
	m_photoPos->setCurrentIndex(qMax(0, m_photoPos->findData(m.photoPosition)));
	m_photoMaxH->setValue(m.photoMaxHeightMm);
	m_pageTypes->setText(m.pageTypes.join(QStringLiteral(", ")));
	m_description->setText(m.description);
	// obituary part
	const int obitIdx = m_tabs->indexOf(m_obitTab);
	m_tabs->setTabVisible(obitIdx, m.isObituary());
	m_tabs->setTabVisible(m_tabs->indexOf(m_storiesTab), !m.isObituary());
	for (QWidget* w : QList<QWidget*> { m_decks, m_leftRail, m_rightRail, m_leadPos, m_leadCols })
	{
		w->setEnabled(!m.isObituary());
		w->setToolTip(m.isObituary() ? tr("Not used by the Obituary engine (obit columns, short news columns: Obituary tab)") : QString());
	}
	if (m.isObituary())
	{
		SuneerObitModel om;
		if (!m.obituary.isEmpty())
			om.fromJson(m.obituary);
		m_obitPhoto->setValue(om.photoMm);
		m_obitShape->setCurrentIndex(qMax(0, m_obitShape->findData(om.frameShape)));
		m_obitPortraitH->setValue(om.portraitHeightMm);
		m_obitFaceW->setValue(qRound((om.faceW > 0 ? om.faceW : om.faceH) * 100));
		m_obitFaceTop->setValue(qRound(om.faceTop * 100));
		if (m_obitBg->findText(om.photoBackground) < 0)
			m_obitBg->addItem(om.photoBackground);
		m_obitBg->setCurrentText(om.photoBackground);
		m_obitAlign->setCurrentIndex(qMax(0, m_obitAlign->findData(om.textAlign)));
		m_obitRunin->setChecked(om.runInPlace);
		m_obitCols[0]->setValue(om.obitFirstCol);
		m_obitCols[1]->setValue(om.obitLastCol);
		m_obitCols[2]->setValue(om.newsFirstCol);
		m_obitCols[3]->setValue(om.newsLastCol);
		m_margin[0]->setValue(om.contentTopMm);
	}
	// the computed column width
	double pageW = m_sample.pageW, ml = m_sample.mLeft, mr = m_sample.mRight;
	if (!m.useDocMargins)
	{
		ml = m.marginLeftMm * kMm;
		mr = m.marginRightMm * kMm;
	}
	const double colW = (pageW - ml - mr - m.gutterMm * kMm * (m.columns - 1)) / qMax(1, m.columns);
	m_colWidthInfo->setText(tr("(auto: %1 mm on this page)").arg(colW / kMm, 0, 'f', 2));
	m_diagram->setValues(m.spacing(), docColours(m_doc));
	syncGapSpins();
	m_loading = false;
}

void SuneerLayoutModelEditor::syncGapSpins()
{
	const SuneerSpacing::Values v = m_diagram->values();
	const bool was = m_loading;
	m_loading = true;
	for (auto it = m_gapSpins.begin(); it != m_gapSpins.end(); ++it)
		if (qAbs(it.value()->value() - v.v[it.key()]) > 0.001)
			it.value()->setValue(v.v[it.key()]);
	m_loading = was;
}

void SuneerLayoutModelEditor::setGap(int key, double mm)
{
	if (m_loading)
		return;
	SuneerSpacing::Values v = m_diagram->values();
	if (qAbs(v.v[key] - mm) < 0.001)
		return;
	v.v[key] = mm;
	m_loading = true;
	m_diagram->setValues(v, docColours(m_doc));
	if (key == SuneerSpacing::ModuleGapH)
		m_gutter->setValue(mm);
	m_loading = false;
	markDirty();
	if (m_liveChk->isChecked())
		m_liveTimer->start();
}

SuneerLayoutModel SuneerLayoutModelEditor::fromUi() const
{
	SuneerLayoutModel m = m_model;
	m.columns = m_columns->value();
	m.gridFromGuides = m_fromGuides->isChecked();
	m.columnWidthMm = m_colWidth->value();
	m.useDocMargins = m_docMargins->isChecked();
	m.marginTopMm = m_margin[0]->value();
	m.marginBottomMm = m_margin[1]->value();
	m.marginLeftMm = m_margin[2]->value();
	m.marginRightMm = m_margin[3]->value();
	m.decks = m_decks->value();
	m.leftRail = m_leftRail->isChecked();
	m.rightRail = m_rightRail->isChecked();
	m.leadPosition = m_leadPos->currentData().toString();
	m.leadColumns = m_leadCols->value();
	m.fixedColumns = m_fixedCols->value();
	auto cellInt = [](QTableWidget* t, int r, int c) { return t->item(r, c) ? t->item(r, c)->text().trimmed().toInt() : 0; };
	m.widthRules.clear();
	for (int r = 0; r < m_widthRules->rowCount(); ++r)
		if (cellInt(m_widthRules, r, 0) > 0 && cellInt(m_widthRules, r, 1) > 0)
			m.widthRules << SuneerLayoutModel::WidthRule { cellInt(m_widthRules, r, 0), cellInt(m_widthRules, r, 1) };
	std::sort(m.widthRules.begin(), m.widthRules.end(), [](const auto& a, const auto& b) { return a.maxChars < b.maxChars; });
	m.headlineRules.clear();
	for (int r = 0; r < m_headRules->rowCount(); ++r)
	{
		const QString st = m_headRules->item(r, 1) ? m_headRules->item(r, 1)->text().trimmed() : QString();
		if (cellInt(m_headRules, r, 0) > 0 && !st.isEmpty())
			m.headlineRules << SuneerLayoutModel::HeadRule { cellInt(m_headRules, r, 0), st };
	}
	std::sort(m.headlineRules.begin(), m.headlineRules.end(), [](const auto& a, const auto& b) { return a.columns < b.columns; });
	m.leadHeadlineStyle = m_leadHead->currentText().trimmed();
	m.fixedPhotoColumns = m_fixedPhotoCols->value();
	m.photoRules.clear();
	for (int r = 0; r < m_photoRules->rowCount(); ++r)
		if (cellInt(m_photoRules, r, 0) > 0 && cellInt(m_photoRules, r, 1) > 0)
			m.photoRules << SuneerLayoutModel::PhotoRule { cellInt(m_photoRules, r, 0), cellInt(m_photoRules, r, 1) };
	std::sort(m.photoRules.begin(), m.photoRules.end(), [](const auto& a, const auto& b) { return a.storyColumns < b.storyColumns; });
	m.photoPosition = m_photoPos->currentData().toString();
	m.photoMaxHeightMm = m_photoMaxH->value();
	m.pageTypes.clear();
	for (const QString& t : m_pageTypes->text().split(',', Qt::SkipEmptyParts))
		if (!t.trimmed().isEmpty())
			m.pageTypes << t.trimmed();
	m.description = m_description->text().trimmed();
	SuneerSpacing::Values v = m_diagram->values();
	v.v[SuneerSpacing::ModuleGapH] = m_gutter->value();
	m.setSpacing(v);
	if (m.isObituary())
	{
		SuneerObitModel om;
		if (!m.obituary.isEmpty())
			om.fromJson(m.obituary);
		om.photoMm = m_obitPhoto->value();
		om.frameShape = m_obitShape->currentData().toString();
		om.portraitHeightMm = m_obitPortraitH->value();
		om.faceW = m_obitFaceW->value() / 100.0;
		om.faceTop = m_obitFaceTop->value() / 100.0;
		om.photoBackground = m_obitBg->currentText().trimmed();
		om.textAlign = m_obitAlign->currentData().toString();
		om.runInPlace = m_obitRunin->isChecked();
		om.obitFirstCol = m_obitCols[0]->value();
		om.obitLastCol = qMax(om.obitFirstCol, m_obitCols[1]->value());
		om.newsFirstCol = m_obitCols[2]->value();
		om.newsLastCol = qMax(om.newsFirstCol, m_obitCols[3]->value());
		om.gridColumns = m.columns;
		om.contentTopMm = m.marginTopMm;
		om.gridGapMm = m.gutterMm;
		om.moduleGapMm = m.gapVMm;
		m.obituary = om.toJson();
	}
	return m;
}

void SuneerLayoutModelEditor::markDirty()
{
	if (m_loading)
		return;
	m_dirty = true;
	m_saveBtn->setEnabled(true);
	m_title->setText(m_model.name + (m_model.builtIn() ? tr("  — built-in") : QString()) + tr("  • not saved"));
	schedulePreview();
	emit edited(fromUi());
}

bool SuneerLayoutModelEditor::saveCurrent()
{
	if (m_id.isEmpty())
		return true;
	SuneerLayoutModel m = fromUi();
	QString err;
	if (!SuneerLayoutModels::save(m, &err))
	{
		QMessageBox::warning(this, tr("Layout Models"), tr("Could not save: %1").arg(err));
		return false;
	}
	m_model = SuneerLayoutModels::savedById(m_id);
	m_dirty = false;
	m_saveBtn->setEnabled(false);
	m_title->setText(m_model.name + (m_model.builtIn() ? tr("  — built-in") : QString()));
	return true;
}

bool SuneerLayoutModelEditor::maybeSave()
{
	if (!m_dirty)
		return true;
	const int r = QMessageBox::question(this, tr("Layout Models"), tr("Save the changes to \"%1\"?").arg(m_model.name),
	                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (r == QMessageBox::Cancel)
		return false;
	if (r == QMessageBox::Save)
		return saveCurrent();
	m_dirty = false;
	return true;
}

void SuneerLayoutModelEditor::schedulePreview()
{
	m_timer->start();
}

void SuneerLayoutModelEditor::runPreview()
{
	m_runner->clear();
	// obituaries for the Obituary engine, news stories for the others
	const bool obit = m_model.isObituary();
	if (m_sampleObit != obit || m_sample.stories.isEmpty())
	{
		m_sample.stories = obit ? SuneerLayout::sampleObituaries() : SuneerLayout::sampleStories();
		m_sampleObit = obit;
	}
	m_previewInfo->setText(tr("Measuring..."));
	m_runner->add(new SuneerLayout::Job(fromUi(), &m_sample, m_measurer.get(), SuneerLayout::Keep::PlacedFrames));
}

void SuneerLayoutModelEditor::onNew()
{
	if (!maybeSave())
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("New layout model"), tr("Name:"), QLineEdit::Normal, tr("New model"), &ok).trimmed();
	if (!ok || name.isEmpty())
		return;
	SuneerLayoutModel m = SuneerLayoutModels::savedById(QStringLiteral("normal"));
	m.id = SuneerLayoutModels::newId(name);
	m.name = name;
	m.origin = QStringLiteral("user");
	m.description.clear();
	m.fixedColumns = 0;
	m.fixedPhotoColumns = 0;
	m.widthRules = { {450, 1}, {900, 2}, {1300, 3}, {999999, 4} };
	m.photoRules = { {1, 1}, {4, 2} };
	if (SuneerLayoutModels::save(m))
	{
		fillList(m.id);
		selectModel(m.id);
	}
}

void SuneerLayoutModelEditor::onDuplicate()
{
	if (!maybeSave())
		return;
	SuneerLayoutModel m = SuneerLayoutModels::savedById(m_id);
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Duplicate layout model"), tr("Name of the copy:"), QLineEdit::Normal, m.name + tr(" copy"), &ok).trimmed();
	if (!ok || name.isEmpty())
		return;
	m.id = SuneerLayoutModels::newId(name);
	m.name = name;
	m.origin = QStringLiteral("user");
	if (SuneerLayoutModels::save(m))
	{
		fillList(m.id);
		selectModel(m.id);
	}
}

void SuneerLayoutModelEditor::onRename()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Rename layout model"), tr("Name:"), QLineEdit::Normal, m_model.name, &ok).trimmed();
	if (!ok || name.isEmpty() || name == m_model.name)
		return;
	SuneerLayoutModel m = fromUi();
	m.name = name;               // the id (file name) stays, so remembered choices keep working
	if (SuneerLayoutModels::save(m))
	{
		fillList(m.id);
		selectModel(m.id);
	}
}

void SuneerLayoutModelEditor::onDelete()
{
	if (m_model.builtIn())
		return;
	if (QMessageBox::question(this, tr("Delete layout model"), tr("Delete \"%1\"?\n%2").arg(m_model.name, SuneerLayoutModels::filePath(m_id))) != QMessageBox::Yes)
		return;
	QString err;
	if (!SuneerLayoutModels::remove(m_id, &err))
	{
		QMessageBox::warning(this, tr("Layout Models"), err);
		return;
	}
	m_dirty = false;
	fillList(QStringLiteral("normal"));
	selectModel(QStringLiteral("normal"));
}

void SuneerLayoutModelEditor::onExport()
{
	if (!maybeSave())
		return;
	QSettings s("Faircode", "ScribusNews");
	const QString dir = s.value("layoutModels/exportDir", QDir::homePath()).toString();
	const QString path = QFileDialog::getSaveFileName(this, tr("Export layout model"), dir + QStringLiteral("/") + m_id + QStringLiteral(".json"), tr("Layout model (*.json)"));
	if (path.isEmpty())
		return;
	s.setValue("layoutModels/exportDir", QFileInfo(path).absolutePath());
	QString err;
	if (!SuneerLayoutModels::exportTo(m_id, path, &err))
		QMessageBox::warning(this, tr("Layout Models"), err);
}

void SuneerLayoutModelEditor::onImport()
{
	if (!maybeSave())
		return;
	QSettings s("Faircode", "ScribusNews");
	const QString dir = s.value("layoutModels/exportDir", QDir::homePath()).toString();
	const QString path = QFileDialog::getOpenFileName(this, tr("Import layout model"), dir, tr("Layout model (*.json)"));
	if (path.isEmpty())
		return;
	QString err;
	const QString id = SuneerLayoutModels::importFrom(path, &err);
	if (id.isEmpty())
	{
		QMessageBox::warning(this, tr("Layout Models"), err);
		return;
	}
	fillList(id);
	selectModel(id);
}

void SuneerLayoutModelEditor::onRestore()
{
	if (!m_model.builtIn())
		return;
	if (QMessageBox::question(this, tr("Restore defaults"), tr("Put the shipped values back into \"%1\"? Save keeps them, Close without saving keeps the old values.").arg(m_model.name),
	                          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
		return;
	SuneerLayoutModel m = SuneerLayoutModels::builtInDefault(m_id);
	m.id = m_id;
	loadUi(m);
	m_model.obituary = m.obituary;
	markDirty();
}

void SuneerLayoutModelEditor::applyToPage()
{
	if (!m_hooks.apply)
		return;
	m_liveTimer->stop();
	if (m_previewing && m_hooks.restore)
		m_hooks.restore();
	m_previewing = false;
	const int n = m_hooks.apply(fromUi().spacing(), true);
	m_pageInfo->setText(tr("Applied \"%1\" spacing: %n module(s) re-spaced (one undo step)", "", n).arg(m_model.name));
}

void SuneerLayoutModelEditor::reject()
{
	if (!maybeSave())
		return;
	m_timer->stop();
	m_liveTimer->stop();
	if (m_previewing && m_hooks.restore)
		m_hooks.restore();
	m_previewing = false;
	m_runner->clear();
	QDialog::reject();
}

// ===========================================================================
// Gallery
// ===========================================================================

namespace {
const int kThumbW = 250;
}

SuneerLayoutGallery::SuneerLayoutGallery(const Source& src, QWidget* parent) : QDialog(parent), m_src(src)
{
	setWindowTitle(tr("Layout Gallery"));
	resize(1180, 860);
	auto* vl = new QVBoxLayout(this);
	auto* top = new QHBoxLayout();
	m_typeCmb = new QComboBox(this);
	m_typeCmb->setMinimumWidth(200);
	m_keepCmb = new QComboBox(this);
	for (SuneerLayout::Keep k : { SuneerLayout::Keep::PlacedFrames, SuneerLayout::Keep::EveryFrame, SuneerLayout::Keep::OnlyNew })
	{
		m_keepCmb->addItem(SuneerLayout::keepTitle(k), (int) k);
		m_keepCmb->setItemData(m_keepCmb->count() - 1, SuneerLayout::keepTip(k), Qt::ToolTipRole);
	}
	QSettings s("Faircode", "ScribusNews");
	m_keepCmb->setCurrentIndex(qBound(0, s.value("layoutGallery/keep", 0).toInt(), 2));
	m_keepCmb->setToolTip(SuneerLayout::keepTip(keep()));
	auto* editBtn = new QPushButton(tr("Layout Models..."), this);
	editBtn->setAutoDefault(false);
	top->addWidget(new QLabel(tr("Page type:"), this));
	top->addWidget(m_typeCmb);
	top->addSpacing(16);
	top->addWidget(new QLabel(tr("Keep:"), this));
	top->addWidget(m_keepCmb);
	top->addSpacing(12);
	m_selOnly = new QCheckBox(tr("Apply to selection only (%n frame(s))", "", m_src.selection.size()), this);
	m_selOnly->setEnabled(!m_src.selection.isEmpty());
	m_selOnly->setToolTip(tr("Arrange only the stories of the frames selected on the page, inside the area they take now; everything else stays"));
	top->addWidget(m_selOnly);
	m_compare = new QCheckBox(tr("Compare"), this);
	m_compare->setToolTip(tr("Show the page as it is now (before) instead of the arrangement (after)"));
	top->addWidget(m_compare);
	m_obitOnly = new QCheckBox(tr("Show all models"), this);
	m_obitOnly->setChecked(s.value("layoutGallery/obitShowAll", false).toBool());
	m_obitOnly->setToolTip(tr("This is an obituary page: only the two obituary models (news left / news right) are shown. On: every model; a news model then packs the obit modules in columns with the obituary gaps (no row alignment)."));
	m_obitOnly->hide();
	top->addWidget(m_obitOnly);
	connect(m_obitOnly, &QCheckBox::toggled, this, [this](bool on) {
		QSettings st("Faircode", "ScribusNews");
		st.setValue("layoutGallery/obitShowAll", on);
		rebuild();
	});
	top->addStretch(1);
	auto* checkBtn = new QPushButton(tr("Check modules..."), this);
	checkBtn->setAutoDefault(false);
	checkBtn->setToolTip(tr("Show the modules found on this page as outlines on the page; merge, split or keep frames fixed (the gallery closes)"));
	checkBtn->setEnabled(bool(m_src.checkModules));
	top->addWidget(checkBtn);
	connect(checkBtn, &QPushButton::clicked, this, [this]() {
		auto hook = m_src.checkModules;
		reject();
		if (hook)
			hook();
	});
	connect(m_selOnly, &QCheckBox::toggled, this, [this]() { loadPage(); rebuild(); });
	connect(m_compare, &QCheckBox::toggled, this, [this](bool on) { showCompare(on); });
	auto* createPmBtn = new QPushButton(tr("Create Page Model..."), this);
	createPmBtn->setAutoDefault(false);
	createPmBtn->setToolTip(tr("Save the layout of whole old pages (.sla) as page models, in page groups (Page-1 ... Obituary, Sports ...). Only the frames are kept, never the text or the pictures."));
	top->addWidget(createPmBtn);
	connect(createPmBtn, &QPushButton::clicked, this, [this]() {
		const QString g = SuneerPageModels::groupForDocPage(m_src.doc && m_src.pageIndex >= 0 && m_src.pageIndex < m_src.doc->DocPages.count() ? m_src.doc->DocPages.at(m_src.pageIndex)->suneerGroup() : QString(),
		                                                   m_src.doc ? m_src.doc->documentFileName() : QString(), m_src.pageIndex, m_src.doc ? m_src.doc->DocPages.count() : 0);
		SuneerPageModelImportDialog dlg(g, this);
		dlg.exec();
	});
	auto* pmBtn = new QPushButton(tr("Page Models..."), this);
	pmBtn->setAutoDefault(false);
	pmBtn->setToolTip(tr("The page models of this page's group: large cards, apply one to this page (the gallery closes)"));
	pmBtn->setEnabled(bool(m_src.openPageModels));
	top->addWidget(pmBtn);
	connect(pmBtn, &QPushButton::clicked, this, [this]() {
		auto hook = m_src.openPageModels;
		reject();
		if (hook)
			hook();
	});
	m_generateBtn = new QPushButton(tr("Generate from archive..."), this);
	m_generateBtn->setAutoDefault(false);
	m_generateBtn->setToolTip(tr("Archive variants: learn new layout models from our own pages (a folder of .sla files of this page type). The pages are only read; nothing goes over the network. The new models appear as extra cards."));
	top->addWidget(m_generateBtn);
	top->addWidget(editBtn);
	vl->addLayout(top);
	m_info = new QLabel(this);
	m_info->setWordWrap(true);
	m_info->setStyleSheet(QStringLiteral("color:#555;"));
	vl->addWidget(m_info);
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_cardsHost = new QWidget(m_scroll);
	m_cardsGrid = new QGridLayout(m_cardsHost);
	m_cardsGrid->setSpacing(14);
	m_scroll->setWidget(m_cardsHost);
	vl->addWidget(m_scroll, 1);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	vl->addWidget(bb);

	// the page: its modules (News Browser stories, checked or detected modules) + the News Browser's new ones
	if (m_src.newStories)
		m_fresh = m_src.newStories(&m_freshNote);
	loadPage();
	m_obitOnly->setVisible(SuneerLayout::isObitPage(m_page));
	m_measurer.reset(new SuneerLayout::Measurer(m_page, 0.6));
	m_runner = new SuneerLayout::Runner(this);
	connect(m_runner, &SuneerLayout::Runner::progress, this, &SuneerLayoutGallery::onJobProgress);
	connect(m_runner, &SuneerLayout::Runner::finished, this, &SuneerLayoutGallery::onJobFinished);
	m_spinTimer = new QTimer(this);
	m_spinTimer->setInterval(120);
	connect(m_spinTimer, &QTimer::timeout, this, &SuneerLayoutGallery::spin);

	// page types: every model's first type, the page's own
	m_typeCmb->addItem(tr("All page types"), QString());
	QStringList types;
	if (!m_src.pageKey.trimmed().isEmpty())
		types << m_src.pageKey.trimmed();
	for (const SuneerLayoutModel& m : SuneerLayoutModels::all())
	{
		if (m.pageTypes.isEmpty())
			continue;
		const QString t = m.pageTypes.first();     // the model's main type; the others are aliases
		bool have = false;
		for (const QString& x : types)
			if (x.compare(t, Qt::CaseInsensitive) == 0)
				have = true;
		if (!have)
			types << t;
	}
	for (const QString& t : types)
		m_typeCmb->addItem(t, t);
	if (!m_src.pageKey.trimmed().isEmpty())
		m_typeCmb->setCurrentIndex(1);
	connect(m_typeCmb, &QComboBox::currentIndexChanged, this, [this]() { rebuild(); });
	connect(m_keepCmb, &QComboBox::currentIndexChanged, this, [this]() {
		m_after.clear();
		QSettings cfg("Faircode", "ScribusNews");
		cfg.setValue("layoutGallery/keep", m_keepCmb->currentIndex());
		m_keepCmb->setToolTip(SuneerLayout::keepTip(keep()));
		m_keptLayer = QImage();
		updateInfo();
		startJobs();
	});
	connect(m_generateBtn, &QPushButton::clicked, this, &SuneerLayoutGallery::generateFromArchive);
	connect(editBtn, &QPushButton::clicked, this, [this]() {
		SuneerLayoutModelEditor dlg(m_src.doc, m_cards.isEmpty() ? QString() : m_cards.first().id, SuneerLayoutModelEditor::PageHooks(), this);
		// the card of the model being edited follows every change (the edited values, not saved yet)
		QTimer redraw;
		redraw.setSingleShot(true);
		redraw.setInterval(400);
		SuneerLayoutModel editing;
		connect(&dlg, &SuneerLayoutModelEditor::edited, this, [&](const SuneerLayoutModel& m) {
			editing = m;
			redraw.start();
		});
		connect(&redraw, &QTimer::timeout, this, [&]() {
			SuneerLayoutModels::setEditing(&editing);
			startPreviews();
		});
		dlg.exec();
		redraw.stop();
		SuneerLayoutModels::setEditing(nullptr);
		SuneerLayoutModels::reload();
		rebuild();
	});
	rebuild();
}

void SuneerLayoutGallery::loadPage()
{
	const bool sel = m_selOnly && m_selOnly->isChecked();
	m_page = SuneerLayout::collectPage(m_src.doc, m_src.pageIndex, m_fresh, SuneerLayout::StyleNames::fromSettings(m_src.titleStyle), m_src.pageKey,
	                                   sel ? &m_src.selection : nullptr);
	m_keptLayer = QImage();
	m_before = QImage();
	m_baseline = SuneerLayout::resultSignature(m_src.doc, m_src.pageIndex, SuneerLayout::pageTopItems(m_src.doc, m_src.pageIndex));
	updateInfo();
}

void SuneerLayoutGallery::updateInfo()
{
	int placed = 0;
	for (const SuneerLayout::StoryIn& st : m_page.stories)
		if (st.placed)
			++placed;
	const int fresh = m_page.stories.size() - placed;
	QString info;
	if (m_src.doc && m_src.pageIndex >= 0)
		info = tr("Page %1: %n module(s) to arrange", "", placed).arg(m_src.pageIndex + 1);
	else
		info = tr("No page: the stories on an empty broadsheet page");
	if (m_page.detected > 0)
		info += tr(" (%n found by detection - check them with \"Check modules...\")", "", m_page.detected);
	info += tr(", %n new from the News Browser", "", fresh);
	if (!m_freshNote.isEmpty() && fresh > 0)
		info += QStringLiteral(" (") + m_freshNote + QStringLiteral(")");
	info += tr(", %n frame(s) kept where they are", "", m_page.always.size());
	if (m_selOnly && m_selOnly->isChecked())
		info += tr(" \u2014 selection only: inside %1 x %2 mm").arg(m_page.area.width() / kMm, 0, 'f', 0).arg(m_page.area.height() / kMm, 0, 'f', 0);
	if (m_page.stories.isEmpty())
		info += QStringLiteral(" \u2014 ") + tr("nothing to arrange");
	bool warn = false;
	if (m_keepCmb && keep() == SuneerLayout::Keep::EveryFrame && placed > 0 && fresh == 0)
	{
		// every module stays and there is nothing new: every model gives the page as it is
		info += QStringLiteral("<br><b>") + tr("Keep = \"%1\": the %n module(s) on the page stay where they are and only new News Browser stories are arranged. "
		                                       "There are none, so every card shows the page unchanged. Choose \"%2\" to arrange the page with a model.", "", placed)
		                                        .arg(SuneerLayout::keepTitle(SuneerLayout::Keep::EveryFrame), SuneerLayout::keepTitle(SuneerLayout::Keep::PlacedFrames))
		        + QStringLiteral("</b>");
		warn = true;
	}
	if (m_info)
	{
		m_info->setTextFormat(Qt::RichText);
		m_info->setText(info);
		m_info->setStyleSheet(warn ? QStringLiteral("color:#B71C1C;") : QStringLiteral("color:#555;"));
	}
}

void SuneerLayoutGallery::showCompare(bool before)
{
	if (before && m_before.isNull())
	{
		const qreal dpr = devicePixelRatioF();
		const double scale = kThumbW * dpr / qMax(1.0, m_page.pageW);
		m_before = SuneerLayout::renderPageNow(m_page, scale);
		m_before.setDevicePixelRatio(dpr);
	}
	for (Card& c : m_cards)
	{
		if (!c.thumb || c.id.isEmpty())
			continue;
		if (before)
			c.thumb->setPixmap(QPixmap::fromImage(m_before));
		else if (m_after.contains(c.id))
			c.thumb->setPixmap(m_after.value(c.id));
	}
}

SuneerLayoutGallery::~SuneerLayoutGallery()
{
	++m_generation;                  // a queued card does not run any more
	if (m_runner)
		m_runner->clear();
	if (m_learner && m_learner->state() != QProcess::NotRunning)
	{
		m_learner->kill();
		m_learner->waitForFinished(2000);
	}
}

QString SuneerLayoutGallery::learnerScript()
{
	const QStringList candidates {
		ScPaths::instance().shareDir() + QStringLiteral("autolayout/learn_models.py"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/autolayout/learn_models.py"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/autolayout/learn_models.py") };
	for (const QString& c : candidates)
		if (QFileInfo::exists(c))
			return QFileInfo(c).absoluteFilePath();
	return candidates.first();
}

void SuneerLayoutGallery::generateFromArchive()
{
	if (m_learner && m_learner->state() != QProcess::NotRunning)
		return;
	QString type = m_typeCmb->currentData().toString().trimmed();
	if (type.isEmpty())
		type = m_src.pageKey.trimmed();
	if (type.isEmpty())
		type = QStringLiteral("General");

	// the small form: folder, page type, optional file-name filter, how many variants
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Generate layout models from archive pages"));
	auto* f = new QFormLayout(&dlg);
	QSettings cfg("Faircode", "ScribusNews");
	QString typeKey = type;
	typeKey.replace('/', '_');
	auto* folder = new QLineEdit(cfg.value(QStringLiteral("layoutGallery/archiveFolder/") + typeKey, cfg.value("layoutGallery/archiveFolder/_last").toString()).toString(), &dlg);
	folder->setMinimumWidth(420);
	auto* browse = new QPushButton(tr("Browse..."), &dlg);
	auto* frow = new QHBoxLayout();
	frow->addWidget(folder, 1);
	frow->addWidget(browse);
	auto* typeEdit = new QLineEdit(type, &dlg);
	typeEdit->setToolTip(tr("The new models are for this page type (Layout Models... > Page types)"));
	auto* filter = new QLineEdit(&dlg);
	filter->setPlaceholderText(tr("every .sla file in the folder"));
	filter->setToolTip(tr("Only files whose name contains this text (e.g. \"page\" or \"Obit\"); empty: all"));
	auto* variants = new QSpinBox(&dlg);
	variants->setRange(1, 6);
	variants->setValue(cfg.value("layoutGallery/archiveVariants", 3).toInt());
	variants->setToolTip(tr("The pages are grouped by structure (lead position, brief rails, decks); one model per group, the largest groups first"));
	f->addRow(tr("Folder with our pages:"), frow);
	f->addRow(tr("Page type:"), typeEdit);
	f->addRow(tr("File name contains:"), filter);
	f->addRow(tr("Variants (at most):"), variants);
	auto* note = new QLabel(tr("The pages are read only. Models with the same names from an earlier run are replaced."), &dlg);
	note->setStyleSheet(QStringLiteral("color:#666;"));
	f->addRow(note);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	bb->button(QDialogButtonBox::Ok)->setText(tr("Generate"));
	f->addRow(bb);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	connect(browse, &QPushButton::clicked, &dlg, [&dlg, folder]() {
		const QString d = QFileDialog::getExistingDirectory(&dlg, tr("Folder with archive pages (.sla)"), folder->text());
		if (!d.isEmpty())
			folder->setText(d);
	});
	if (dlg.exec() != QDialog::Accepted)
		return;
	const QString dir = folder->text().trimmed();
	type = typeEdit->text().trimmed().isEmpty() ? type : typeEdit->text().trimmed();
	if (dir.isEmpty() || !QFileInfo(dir).isDir())
	{
		QMessageBox::warning(this, tr("Generate"), tr("Choose a folder with .sla pages."));
		return;
	}
	typeKey = type;
	typeKey.replace('/', '_');
	cfg.setValue(QStringLiteral("layoutGallery/archiveFolder/") + typeKey, dir);
	cfg.setValue("layoutGallery/archiveFolder/_last", dir);
	cfg.setValue("layoutGallery/archiveVariants", variants->value());

	const QString script = learnerScript();
	if (!QFileInfo::exists(script))
	{
		QMessageBox::warning(this, tr("Generate"), tr("The learner is missing: %1").arg(script));
		return;
	}
	m_learner = new QProcess(this);
	m_learnStderr.clear();
	m_learner->setProgram(QStringLiteral("python3"));
	m_learner->setArguments({ script, QStringLiteral("--folder"), dir, QStringLiteral("--page-type"), type, QStringLiteral("--out-dir"), SuneerLayoutModels::dir(),
	                          QStringLiteral("--name-filter"), filter->text().trimmed(), QStringLiteral("--variants"), QString::number(variants->value()) });
	m_generateBtn->setEnabled(false);
	const QString oldInfo = m_info->text();
	m_info->setText(tr("Reading the archive pages..."));
	connect(m_learner, &QProcess::readyReadStandardError, this, [this]() {
		const QString chunk = QString::fromUtf8(m_learner->readAllStandardError());
		m_learnStderr += chunk;
		for (const QString& line : chunk.split('\n', Qt::SkipEmptyParts))
		{
			const QStringList parts = line.split(' ');
			if (parts.size() >= 4 && parts[0] == QLatin1String("PROGRESS"))
				m_info->setText(tr("Archive: reading page %1 of %2 - %3").arg(parts[1].toInt() + 1).arg(parts[2]).arg(parts.mid(3).join(' ')));
			else if (line.startsWith(QLatin1String("GRID")) || line.startsWith(QLatin1String("MODEL")))
				m_info->setText(tr("Archive: %1").arg(line));
		}
	});
	connect(m_learner, &QProcess::finished, this, [this, type, oldInfo](int code, QProcess::ExitStatus) {
		const QByteArray out = m_learner->readAllStandardOutput();
		m_learner->deleteLater();
		m_learner = nullptr;
		m_generateBtn->setEnabled(true);
		m_info->setText(oldInfo);
		const QJsonObject r = QJsonDocument::fromJson(out).object();
		if (code != 0 || r.contains("error") || r["models"].toArray().isEmpty())
		{
			QString why = r["error"].toString();
			if (why.isEmpty())
				why = m_learnStderr.right(800);
			QMessageBox::warning(this, tr("Generate"), tr("No model was learned:\n%1").arg(why));
			return;
		}
		QStringList lines;
		const QJsonObject grid = r["grid"].toObject();
		lines << tr("%1 of %2 page(s) read; grid %3 columns, gutter %4 mm.").arg(r["pages"].toInt()).arg(r["read"].toInt())
		         .arg(grid["columns"].toInt()).arg(grid["gutter"].toDouble());
		for (const QJsonValue& v : r["models"].toArray())
		{
			const QJsonObject m = v.toObject();
			QStringList files;
			for (const QJsonValue& fv : m["files"].toArray())
				files << fv.toString();
			lines << QStringLiteral("\u2022 %1: %2 page(s), %3 modules (%4)").arg(m["name"].toString()).arg(m["pages"].toInt()).arg(m["modules"].toInt()).arg(files.join(QStringLiteral(", ")));
		}
		const QStringList skipped = m_learnStderr.split('\n').filter(QRegularExpression(QStringLiteral("^SKIP ")));
		if (!skipped.isEmpty())
			lines << tr("Skipped (no story modules found): %1").arg(skipped.size());
		SuneerLayoutModels::reload();
		// show the page type of the new models
		int ti = m_typeCmb->findData(type);
		if (ti < 0)
		{
			m_typeCmb->addItem(type, type);
			ti = m_typeCmb->count() - 1;
		}
		m_typeCmb->blockSignals(true);
		m_typeCmb->setCurrentIndex(ti);
		m_typeCmb->blockSignals(false);
		rebuild();
		QMessageBox::information(this, tr("Generate"), lines.join('\n'));
	});
	m_learner->start();
	if (!m_learner->waitForStarted(5000))
	{
		QMessageBox::warning(this, tr("Generate"), tr("python3 could not be started: %1").arg(m_learner->errorString()));
		m_learner->deleteLater();
		m_learner = nullptr;
		m_generateBtn->setEnabled(true);
		m_info->setText(oldInfo);
	}
}

SuneerLayout::Keep SuneerLayoutGallery::keep() const
{
	return (SuneerLayout::Keep) m_keepCmb->currentData().toInt();
}

void SuneerLayoutGallery::rebuild()
{
	m_runner->clear();
	m_after.clear();
	for (Card& c : m_cards)
		delete c.frame;
	m_cards.clear();
	const QString type = m_typeCmb->currentData().toString();
	const double ratio = m_page.pageH / qMax(1.0, m_page.pageW);
	const int thumbH = qRound(kThumbW * ratio);
	int n = 0;
	QString last = SuneerLayoutModels::lastModelFor(m_src.pageKey);
	const bool obitPage = m_obitOnly && !m_obitOnly->isHidden();
	const bool obitOnly = obitPage && !m_obitOnly->isChecked();
	if (obitPage && (last.isEmpty() || !SuneerLayoutModels::byId(last).isObituary()))
	{
		// an obituary page: the obituary model applied last (news left / right) is the default card
		QSettings st("Faircode", "ScribusNews");
		const QString d = st.value(QStringLiteral("layoutModel/_obituaryDefault")).toString();
		last = SuneerLayoutModels::exists(d) ? d : QStringLiteral("obituary");
	}
	for (const SuneerLayoutModel& m : SuneerLayoutModels::all())
	{
		if (!type.isEmpty() && !m.appliesTo(type) && m.id != last && !(obitPage && m.isObituary()))
			continue;                              // the model in use is always shown
		if (obitOnly && !m.isObituary() && m.id != last)
			continue;                              // obituary page: only the obituary models
		Card c;
		c.id = m.id;
		c.frame = new QFrame(m_cardsHost);
		c.frame->setFrameShape(QFrame::StyledPanel);
		c.frame->setStyleSheet(m.id == last ? QStringLiteral("QFrame { border: 2px solid #2E7D32; border-radius: 4px; }") : QString());
		auto* cl = new QVBoxLayout(c.frame);
		auto* name = new QLabel(QStringLiteral("<b>%1</b>").arg(m.name.toHtmlEscaped()) + (m.builtIn() ? tr(" <span style='color:#888'>built-in</span>") : m.origin == QLatin1String("archive") ? tr(" <span style='color:#6A1B9A'>archive</span>") : QString())
		                        + (m.id == last ? tr(" <span style='color:#2E7D32'>✔ in use</span>") : QString()), c.frame);
		name->setStyleSheet(QStringLiteral("border:none;"));
		name->setToolTip(m.description);
		cl->addWidget(name);
		c.thumb = new QLabel(c.frame);
		c.thumb->setFixedSize(kThumbW, thumbH);
		c.thumb->setAlignment(Qt::AlignCenter);
		c.thumb->setStyleSheet(QStringLiteral("background:#ECEFF1; border:none; color:#777;"));
		c.thumb->setText(tr("waiting..."));
		cl->addWidget(c.thumb);
		c.fit = new QLabel(c.frame);
		c.fit->setStyleSheet(QStringLiteral("border:none;"));
		c.fit->setWordWrap(true);
		cl->addWidget(c.fit);
		c.same = new QLabel(c.frame);
		c.same->setStyleSheet(QStringLiteral("border:none; color:#6A1B9A;"));
		c.same->setWordWrap(true);
		c.same->hide();
		cl->addWidget(c.same);
		c.name = m.name;
		auto* row = new QHBoxLayout();
		c.apply = new QPushButton(tr("Apply"), c.frame);
		c.apply->setAutoDefault(false);
		c.apply->setToolTip(tr("Arrange this page with \"%1\" as the thumbnail shows it (Keep: as chosen above), and use the model for %2 pages from now on. One undo step; stories that do not fit go to the pasteboard and are listed.")
		                    .arg(m.name, m_src.pageKey.isEmpty() ? tr("these") : QStringLiteral("\"") + m_src.pageKey + QStringLiteral("\"")));
		const QString id = m.id;
		connect(c.apply, &QPushButton::clicked, this, [this, id]() { applyModel(id); });
		row->addStretch(1);
		row->addWidget(c.apply);
		cl->addLayout(row);
		m_cardsGrid->addWidget(c.frame, n / 4, n % 4, Qt::AlignTop | Qt::AlignLeft);
		m_cards << c;
		++n;
	}
	m_cardsGrid->setRowStretch(n / 4 + 1, 1);
	m_cardsGrid->setColumnStretch(4, 1);
	if (m_cards.isEmpty())
	{
		auto* none = new QLabel(tr("No layout model is set for this page type. Choose \"All page types\" or add the type to a model (Layout Models... > Page types)."), m_cardsHost);
		Card c;
		c.frame = new QFrame(m_cardsHost);
		auto* l = new QVBoxLayout(c.frame);
		none->setParent(c.frame);
		l->addWidget(none);
		m_cardsGrid->addWidget(c.frame, 0, 0, 1, 4);
		m_cards << c;
	}
	startJobs();
}

void SuneerLayoutGallery::startJobs()
{
	if (m_src.preview && m_src.doc && m_src.pageIndex >= 0)
	{
		startPreviews();
		return;
	}
	m_runner->clear();
	if (m_keptLayer.isNull())
		m_keptLayer = SuneerLayout::renderKept(m_page, kThumbW * 2.0 / qMax(1.0, m_page.pageW), keep());
	int tag = 0;
	for (Card& c : m_cards)
	{
		c.job = nullptr;
		if (c.id.isEmpty())
			continue;
		auto* job = new SuneerLayout::Job(SuneerLayoutModels::byId(c.id), &m_page, m_measurer.get(), keep());
		job->tag = tag++;
		c.job = job;
		c.fit->setText(tr("Measuring..."));
		c.fit->setStyleSheet(QStringLiteral("border:none; color:#555;"));
		m_runner->add(job);
	}
}

void SuneerLayoutGallery::onJobProgress(SuneerLayout::Job* job)
{
	for (Card& c : m_cards)
		if (c.job == job && !job->finished())
			c.fit->setText(tr("Measuring %1 of %2...").arg(job->done()).arg(job->total()));
}

void SuneerLayoutGallery::onJobFinished(SuneerLayout::Job* job)
{
	for (Card& c : m_cards)
	{
		if (c.job != job)
			continue;
		const qreal dpr = devicePixelRatioF();
		QImage img = SuneerLayout::renderPlan(job->plan(), m_page, qRound(kThumbW * dpr), 0.6, m_keptLayer);
		img.setDevicePixelRatio(dpr);
		m_after.insert(c.id, QPixmap::fromImage(img));
		if (m_compare && m_compare->isChecked())
		{
			showCompare(true);
		}
		else
			c.thumb->setPixmap(m_after.value(c.id));
		const SuneerLayout::PlanOut& p = job->plan();
		SuneerLayout::debugPlan("gallery", job->model(), m_page, p);
		if (SuneerLayout::debugOn())
			fprintf(stderr, "LAYOUT-PLAN gallery   thumbnail: kept layer (%d always-frames) + %d module image(s) at the plan rectangles\n", (int) m_page.always.size(), (int) p.modules.size());
		QString text = p.fitLine();
		if (!p.notFit.isEmpty())
		{
			QStringList names;
			for (int i : p.notFit)
				if (i >= 0 && i < m_page.stories.size())
					names << m_page.stories[i].title.left(30);
			c.fit->setToolTip(tr("Not fitting:\n%1").arg(names.join(QStringLiteral("\n"))));
		}
		else
			c.fit->setToolTip(QString());
		c.fit->setText(text);
		c.fit->setStyleSheet(p.allFit() ? QStringLiteral("border:none; color:#2E7D32; font-weight:bold;")
		                                : QStringLiteral("border:none; color:#E65100; font-weight:bold;"));
	}
}

void SuneerLayoutGallery::applyModel(const QString& id)
{
	const SuneerLayoutModel m = SuneerLayoutModels::byId(id);
	if (m_src.apply)
	{
		m_runner->clear();
		QApplication::setOverrideCursor(Qt::WaitCursor);
		QStringList expect;
		for (const Card& c : m_cards)
			if (c.id == id && !c.pending)
				expect = c.signature;
		++m_generation;                  // the cards still waiting are not needed any more
		m_queue.clear();
		const QString report = m_src.apply(id, (int) keep(), m_selOnly && m_selOnly->isChecked(), expect);
		QApplication::restoreOverrideCursor();
		QMessageBox::information(this, tr("Layout Gallery"), report);
		accept();                          // the page has changed: the thumbnails are old now
		return;
	}
	SuneerLayoutModels::setLastModelFor(m_src.pageKey, id);
	if (m_src.applied)
		m_src.applied(id);
	QMessageBox::information(this, tr("Layout Gallery"),
		tr("\"%1\" is now the model for %2 pages.").arg(m.name, m_src.pageKey.isEmpty() ? tr("these") : QStringLiteral("\"") + m_src.pageKey + QStringLiteral("\"")));
	rebuild();
}

// ---------------------------------------------------------------------------
// Cards from scratch copies: every card is the page really arranged with its model
// (SuneerNewsPanel::previewArrange: a hidden copy of the page, the code Apply runs).
// Scribus text layout is not thread safe, so the cards are made one after the other
// on the GUI thread; the dialog is redrawn between two cards.
// ---------------------------------------------------------------------------

namespace {
struct CachedPreview
{
	SuneerLayoutGallery::Preview preview;
	qint64 used { 0 };
};
QHash<QString, CachedPreview>& previewCache()
{
	static QHash<QString, CachedPreview> cache;
	return cache;
}
const char* const kSpin[] = { "◐", "◓", "◑", "◒" };
} // namespace

QString SuneerLayoutGallery::cacheKey(const QString& modelId) const
{
	// the page as it is + the model as it is now (its file may have changed) + how it is applied
	const QByteArray model = QJsonDocument(SuneerLayoutModels::byId(modelId).toJson()).toJson(QJsonDocument::Compact);
	return m_stateKey + QLatin1Char('|') + modelId + QLatin1Char('|') + QString::fromLatin1(QCryptographicHash::hash(model, QCryptographicHash::Sha1).toHex())
	       + QStringLiteral("|k%1|s%2").arg((int) keep()).arg(m_selOnly && m_selOnly->isChecked() ? 1 : 0);
}

void SuneerLayoutGallery::startPreviews()
{
	m_runner->clear();
	++m_generation;
	m_queue.clear();
	m_stateKey = m_src.stateKey ? m_src.stateKey() : QString();
	for (Card& c : m_cards)
	{
		c.job = nullptr;
		if (c.id.isEmpty())
			continue;
		c.signature.clear();
		c.same->hide();
		const QString key = cacheKey(c.id);
		if (!m_stateKey.isEmpty() && previewCache().contains(key))
		{
			previewCache()[key].used = QDateTime::currentMSecsSinceEpoch();
			if (SuneerLayout::debugOn())
				fprintf(stderr, "LAYOUT-CARD %s: from the cache\n", qPrintable(c.id));
			showPreview(c, previewCache().value(key).preview);
			continue;
		}
		c.pending = true;
		c.thumb->setPixmap(QPixmap());
		c.thumb->setText(tr("waiting..."));
		c.fit->setText(QString());
		c.fit->setStyleSheet(QStringLiteral("border:none; color:#555;"));
		c.apply->setEnabled(false);
		m_queue << c.id;
	}
	updateSameAs();
	if (!m_queue.isEmpty())
	{
		m_spinTimer->start();
		const int gen = m_generation;
		QTimer::singleShot(0, this, [this, gen]() { runNextPreview(gen); });
	}
}

void SuneerLayoutGallery::spin()
{
	m_spinFrame = (m_spinFrame + 1) % 4;
	for (Card& c : m_cards)
		if (c.pending && c.thumb)
		{
			const bool now = !m_queue.isEmpty() && m_queue.first() == c.id;
			c.thumb->setText(QString::fromUtf8(kSpin[m_spinFrame]) + QLatin1Char(' ') + (now ? tr("arranging...") : tr("waiting...")));
		}
}

void SuneerLayoutGallery::runNextPreview(int generation)
{
	if (generation != m_generation)
		return;                       // the cards were made again (Keep, page type ...) or the dialog closes
	if (m_queue.isEmpty())
	{
		m_spinTimer->stop();
		updateSameAs();
		return;
	}
	const QString id = m_queue.first();
	Card* card = nullptr;
	for (Card& c : m_cards)
		if (c.id == id)
			card = &c;
	if (!card)
	{
		m_queue.removeFirst();
		QTimer::singleShot(0, this, [this, generation]() { runNextPreview(generation); });
		return;
	}
	card->thumb->setText(QString::fromUtf8(kSpin[m_spinFrame]) + QLatin1Char(' ') + tr("arranging..."));
	card->thumb->repaint();
	const qreal dpr = devicePixelRatioF();
	const int widthPx = qRound(kThumbW * dpr);
	const Preview p = m_src.preview(id, (int) keep(), m_selOnly && m_selOnly->isChecked(), widthPx);
	if (generation != m_generation)
		return;
	m_queue.removeFirst();
	for (Card& c : m_cards)
		if (c.id == id)
			card = &c;
	if (p.ok && !m_stateKey.isEmpty())
	{
		auto& cache = previewCache();
		if (cache.size() > 60)
		{
			// the oldest half goes
			QList<qint64> ages;
			for (const CachedPreview& cp : cache)
				ages << cp.used;
			std::sort(ages.begin(), ages.end());
			const qint64 cut = ages[ages.size() / 2];
			for (auto it = cache.begin(); it != cache.end();)
				it = it->used <= cut ? cache.erase(it) : std::next(it);
		}
		cache.insert(cacheKey(id), CachedPreview { p, QDateTime::currentMSecsSinceEpoch() });
	}
	showPreview(*card, p);
	updateSameAs();
	QTimer::singleShot(0, this, [this, generation]() { runNextPreview(generation); });
}

void SuneerLayoutGallery::showPreview(Card& c, const Preview& p)
{
	c.pending = false;
	c.apply->setEnabled(true);
	if (!p.ok)
	{
		c.thumb->setPixmap(QPixmap());
		c.thumb->setText(tr("Could not arrange a copy of the page"));
		c.fit->setText(p.error);
		c.fit->setStyleSheet(QStringLiteral("border:none; color:#B71C1C;"));
		return;
	}
	QImage img = p.image;
	img.setDevicePixelRatio(devicePixelRatioF());
	m_after.insert(c.id, QPixmap::fromImage(img));
	if (m_compare && m_compare->isChecked())
		showCompare(true);
	else
		c.thumb->setPixmap(m_after.value(c.id));
	c.signature = p.signature;
	c.fit->setText(p.fitLine + (p.noChange ? QStringLiteral("\n") + tr("Already applied - the page is in this layout") : QString()));
	c.fit->setToolTip((p.notFit.isEmpty() ? QString() : tr("Not fitting (to the pasteboard on Apply):\n%1\n\n").arg(p.notFit.join(QStringLiteral("\n"))))
	                  + tr("Apply will report:\n%1\n\n(arranged in a copy of the page in %2 ms)").arg(p.report).arg(p.ms));
	c.fit->setStyleSheet(p.noChange ? QStringLiteral("border:none; color:#555; font-weight:bold;")
	                     : p.allFit ? QStringLiteral("border:none; color:#2E7D32; font-weight:bold;")
	                                : QStringLiteral("border:none; color:#E65100; font-weight:bold;"));
}

void SuneerLayoutGallery::updateSameAs()
{
	for (int i = 0; i < m_cards.size(); ++i)
	{
		Card& c = m_cards[i];
		if (!c.same)
			continue;
		QString first;
		if (!c.pending && !c.signature.isEmpty())
			for (int j = 0; j < i && first.isEmpty(); ++j)
				if (!m_cards[j].pending && m_cards[j].signature == c.signature)
					first = m_cards[j].name;
		c.same->setText(first.isEmpty() ? QString() : tr("Same result as \"%1\"").arg(first));
		c.same->setVisible(!first.isEmpty());
	}
}
