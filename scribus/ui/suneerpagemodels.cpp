/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "suneerpagemodels.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include "scribusdoc.h"

namespace
{
const int kThumbW = 210;

bool darkPalette(const QWidget* w)
{
	return w->palette().color(QPalette::Window).lightness() < 128;
}

QString groupTitle(const SuneerPageGroup& g)
{
	return g.name.isEmpty() ? g.id : g.name;
}
}

// ============================================================================ Gallery

SuneerPageModelGallery::SuneerPageModelGallery(const Source& src, QWidget* parent)
	: QDialog(parent), m_pageAds(src.pageAds), m_src(src)
{
	setWindowTitle(tr("Page Models"));
	setMinimumSize(980, 640);
	resize(1180, 760);
	auto* hl = new QHBoxLayout(this);

	// left: groups
	auto* left = new QVBoxLayout();
	left->addWidget(new QLabel(tr("Page group:"), this));
	m_groups = new QListWidget(this);
	m_groups->setMinimumWidth(190);
	m_groups->setMaximumWidth(260);
	left->addWidget(m_groups, 1);
	m_groupNote = new QLabel(this);
	m_groupNote->setWordWrap(true);
	m_groupNote->setStyleSheet(QStringLiteral("color:#666;"));
	left->addWidget(m_groupNote);
	auto* gb = new QGridLayout();
	auto mk = [this](const QString& t, const QString& tip) { auto* b = new QPushButton(t, this); b->setToolTip(tip); b->setAutoDefault(false); return b; };
	auto* gAdd = mk(tr("New..."), tr("A new page group"));
	m_gRen = mk(tr("Rename..."), tr("Rename the group"));
	m_gEdit = mk(tr("Note / grid..."), tr("The group's note and grid type (normal 8 columns / obituary 10 x 30.3 mm)"));
	m_gDel = mk(tr("Delete"), tr("Delete the group (only an empty one, or move its models to another group)"));
	m_gUp = mk(tr("▲"), tr("Move the group up"));
	m_gDown = mk(tr("▼"), tr("Move the group down"));
	gb->addWidget(gAdd, 0, 0);
	gb->addWidget(m_gRen, 0, 1);
	gb->addWidget(m_gEdit, 1, 0);
	gb->addWidget(m_gDel, 1, 1);
	gb->addWidget(m_gUp, 2, 0);
	gb->addWidget(m_gDown, 2, 1);
	left->addLayout(gb);
	auto* pagesBtn = mk(tr("Page-N count..."), tr("How many Page-1 ... Page-N groups there are"));
	left->addWidget(pagesBtn);
	hl->addLayout(left);

	// right: cards
	auto* right = new QVBoxLayout();
	auto* top = new QHBoxLayout();
	auto* createBtn = mk(tr("Create Page Model..."), tr("Read old pages (.sla, 1.4 / 1.5 / 1.7) and save their layout as page models - the text and the pictures are never kept"));
	top->addWidget(createBtn);
	m_fitAds = new QCheckBox(tr("Fits current ads"), this);
	m_fitAds->setToolTip(tr("Only the models whose ad space matches the ads on this page now (%n ad(s))", "", m_pageAds.size()));
	m_fitAds->setEnabled(m_src.doc != nullptr);
	top->addWidget(m_fitAds);
	top->addStretch(1);
	auto* rmBtn = mk(tr("Remove empty slots"), tr("Delete every placeholder / ad outline left on the current page"));
	rmBtn->setEnabled(bool(m_src.removeEmptySlots) && m_src.doc != nullptr);
	top->addWidget(rmBtn);
	connect(rmBtn, &QPushButton::clicked, this, [this]() {
		if (!m_src.removeEmptySlots)
			return;
		const int n = m_src.removeEmptySlots();
		m_info->setText(n ? tr("%n empty slot(s) removed from the page.", "", n) : tr("No empty slot on this page."));
	});
	auto* exportBtn = mk(tr("Export..."), tr("This group or every group as one file (for the other office PCs)"));
	auto* importBtn = mk(tr("Import..."), tr("A page models file made with Export"));
	top->addWidget(exportBtn);
	top->addWidget(importBtn);
	right->addLayout(top);
	m_info = new QLabel(this);
	m_info->setWordWrap(true);
	m_info->setStyleSheet(QStringLiteral("color:#555;"));
	right->addWidget(m_info);
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_cardsHost = new QWidget(m_scroll);
	m_cardsGrid = new QGridLayout(m_cardsHost);
	m_cardsGrid->setSpacing(14);
	m_scroll->setWidget(m_cardsHost);
	right->addWidget(m_scroll, 1);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	right->addWidget(bb);
	hl->addLayout(right, 1);

	connect(m_groups, &QListWidget::currentRowChanged, this, [this](int row) {
		QListWidgetItem* it = m_groups->item(row);
		if (it)
			selectGroup(it->data(Qt::UserRole).toString());
	});
	connect(m_fitAds, &QCheckBox::toggled, this, [this]() { rebuildCards(); });
	connect(createBtn, &QPushButton::clicked, this, &SuneerPageModelGallery::createModels);
	connect(exportBtn, &QPushButton::clicked, this, &SuneerPageModelGallery::exportModels);
	connect(importBtn, &QPushButton::clicked, this, &SuneerPageModelGallery::importModels);
	connect(gAdd, &QPushButton::clicked, this, &SuneerPageModelGallery::groupAdd);
	connect(m_gRen, &QPushButton::clicked, this, &SuneerPageModelGallery::groupRename);
	connect(m_gEdit, &QPushButton::clicked, this, &SuneerPageModelGallery::groupEdit);
	connect(m_gDel, &QPushButton::clicked, this, &SuneerPageModelGallery::groupDelete);
	connect(m_gUp, &QPushButton::clicked, this, [this]() { groupMove(-1); });
	connect(m_gDown, &QPushButton::clicked, this, [this]() { groupMove(1); });
	connect(pagesBtn, &QPushButton::clicked, this, [this]() {
		bool ok = false;
		const int n = QInputDialog::getInt(this, tr("Page groups"), tr("Number of Page-1 ... Page-N groups:"), SuneerPageModels::defaultPageCount(), 1, 200, 1, &ok);
		if (!ok)
			return;
		SuneerPageModels::setDefaultPageCount(n);
		fillGroups(m_group);
	});

	// the matching group: the page's attribute, the page number / file name, else the last one used
	QString start = SuneerPageModels::groupForDocPage(m_src.savedGroup, m_src.docFile, m_src.pageIndex,
	                                                  m_src.doc ? m_src.doc->DocPages.count() : 0);
	if (m_src.doc == nullptr || start.isEmpty())
		start = QSettings("Faircode", "ScribusNews").value("pageModels/lastGroup").toString();
	fillGroups(start);
}

SuneerPageModelGallery::~SuneerPageModelGallery()
{
	QSettings("Faircode", "ScribusNews").setValue("pageModels/lastGroup", m_group);
}

void SuneerPageModelGallery::fillGroups(const QString& select)
{
	const QSignalBlocker block(m_groups);
	m_groups->clear();
	int sel = 0;
	for (const SuneerPageGroup& g : SuneerPageModels::groups())
	{
		auto* it = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(groupTitle(g)).arg(SuneerPageModels::modelCount(g.id)), m_groups);
		it->setData(Qt::UserRole, g.id);
		it->setToolTip(g.note.isEmpty() ? (g.isObituary() ? tr("Obituary grid") : tr("8-column grid")) : g.note);
		if (g.id == select)
			sel = m_groups->count() - 1;
	}
	m_groups->setCurrentRow(sel);
	QListWidgetItem* it = m_groups->item(sel);
	selectGroup(it ? it->data(Qt::UserRole).toString() : QString());
}

void SuneerPageModelGallery::selectGroup(const QString& id)
{
	m_group = id;
	const SuneerPageGroup g = SuneerPageModels::group(id);
	QString note = g.note;
	if (note.isEmpty())
		note = g.isObituary() ? tr("Obituary grid: 10 x 30.3 mm, the Obituary module rules") : tr("8-column grid (34.25 mm, 8 mm gutter)");
	m_groupNote->setText(note);
	updateGroupButtons();
	rebuildCards();
}

void SuneerPageModelGallery::updateGroupButtons()
{
	const int row = m_groups->currentRow();
	m_gUp->setEnabled(row > 0);
	m_gDown->setEnabled(row >= 0 && row < m_groups->count() - 1);
	m_gDel->setEnabled(row >= 0);
	m_gRen->setEnabled(row >= 0);
	m_gEdit->setEnabled(row >= 0);
}

bool SuneerPageModelGallery::adsMatch(const SuneerPageModel& m, const QList<QRectF>& pageAds, QString* why)
{
	if (m.ads.size() != pageAds.size())
	{
		if (why)
			*why = tr("the model has %1 ad area(s), the page %2").arg(m.ads.size()).arg(pageAds.size());
		return false;
	}
	QList<bool> used;
	for (int i = 0; i < pageAds.size(); ++i)
		used << false;
	for (const SuneerPageModel::Box& b : m.ads)
	{
		bool found = false;
		for (int i = 0; i < pageAds.size() && !found; ++i)
		{
			if (used[i])
				continue;
			const QRectF& p = pageAds[i];
			const double areaA = b.rect.width() * b.rect.height(), areaB = p.width() * p.height();
			if (qAbs(p.x() - b.rect.x()) <= 8.0 && qAbs(p.y() - b.rect.y()) <= 8.0 && qAbs(p.right() - b.rect.right()) <= 8.0
			    && qAbs(p.bottom() - b.rect.bottom()) <= 8.0 && qMin(areaA, areaB) >= 0.8 * qMax(areaA, areaB))
			{
				used[i] = true;
				found = true;
			}
		}
		if (!found)
		{
			if (why)
				*why = tr("the ad at %1,%2 (%3 x %4 mm) has no match on the page").arg(qRound(b.rect.x())).arg(qRound(b.rect.y())).arg(qRound(b.rect.width())).arg(qRound(b.rect.height()));
			return false;
		}
	}
	return true;
}

void SuneerPageModelGallery::rebuildCards()
{
	for (Card& c : m_cards)
		if (c.frame)
			c.frame->deleteLater();
	m_cards.clear();
	const QList<SuneerPageModel> models = SuneerPageModels::models(m_group);
	const bool dark = darkPalette(this);
	const bool onlyFit = m_fitAds && m_fitAds->isChecked();
	int shown = 0, fitting = 0;
	const int perRow = qMax(1, (width() - 300) / (kThumbW + 60));
	for (const SuneerPageModel& m : models)
	{
		Card c;
		c.id = m.id;
		c.model = m;
		c.fits = adsMatch(m, m_pageAds);
		if (c.fits)
			++fitting;
		if (onlyFit && !c.fits)
			continue;
		c.frame = new QFrame(m_cardsHost);
		c.frame->setFrameShape(QFrame::StyledPanel);
		c.frame->setStyleSheet(c.fits && !m_pageAds.isEmpty() ? QStringLiteral("QFrame { border: 2px solid #4a8; border-radius: 6px; }") : QStringLiteral("QFrame { border: 1px solid #999; border-radius: 6px; }"));
		c.frame->setContextMenuPolicy(Qt::CustomContextMenu);
		auto* vl = new QVBoxLayout(c.frame);
		c.name = new QLabel(QStringLiteral("<b>%1</b>").arg(m.name.toHtmlEscaped()), c.frame);
		c.name->setWordWrap(true);
		c.name->setStyleSheet(QStringLiteral("border:none;"));
		vl->addWidget(c.name);
		c.thumb = new QLabel(c.frame);
		QImage img;
		img.load(SuneerPageModels::thumbPath(m.group, m.id));
		if (img.isNull() || img.width() < kThumbW)
			img = m.thumbnail(kThumbW * 2, dark);
		c.thumb->setPixmap(QPixmap::fromImage(img.scaledToWidth(kThumbW, Qt::SmoothTransformation)));
		c.thumb->setStyleSheet(QStringLiteral("border:none;"));
		c.thumb->setToolTip(tr("%1\nfrom %2%3\n%4").arg(m.name, m.sourceFile, m.sourcePages > 1 ? tr(" page %1").arg(m.sourcePage + 1) : QString(), m.summary()));
		vl->addWidget(c.thumb, 0, Qt::AlignHCenter);
		c.info = new QLabel(m.summary() + (m_pageAds.isEmpty() ? QString() : (c.fits ? tr("\n✔ fits the current ads") : tr("\n⚠ other ad space"))), c.frame);
		c.info->setWordWrap(true);
		c.info->setStyleSheet(QStringLiteral("border:none; color:#555;"));
		vl->addWidget(c.info);
		c.apply = new QPushButton(tr("Skeleton on this page"), c.frame);
		c.apply->setToolTip(tr("One placeholder frame per slot on this page (non-printing layer \"Page model\"); nothing on the page is moved. Then drag stories onto the slots."));
		c.apply->setAutoDefault(false);
		c.apply->setEnabled(bool(m_src.apply) && m_src.doc != nullptr);
		vl->addWidget(c.apply);
		const int idx = m_cards.size();
		connect(c.apply, &QPushButton::clicked, this, [this, idx]() { if (idx < m_cards.size()) applyModel(m_cards[idx]); });
		connect(c.frame, &QWidget::customContextMenuRequested, this, [this, idx](const QPoint& p) { if (idx < m_cards.size()) cardMenu(m_cards[idx], m_cards[idx].frame->mapToGlobal(p)); });
		m_cardsGrid->addWidget(c.frame, shown / perRow, shown % perRow);
		m_cards << c;
		++shown;
	}
	m_cardsGrid->setRowStretch(shown / perRow + 1, 1);
	m_cardsGrid->setColumnStretch(perRow, 1);
	if (models.isEmpty())
		m_info->setText(tr("No page model in this group yet. \"Create Page Model...\" reads old pages (.sla) and saves their layout here."));
	else
		m_info->setText(tr("%n model(s) in %1", "", models.size()).arg(groupTitle(SuneerPageModels::group(m_group)))
		                + (m_pageAds.isEmpty() ? (m_src.doc ? tr(" · no ads on this page") : QString()) : tr(" · %1 fit the current ads (%n ad(s))", "", m_pageAds.size()).arg(fitting))
		                + tr(" · right-click a card: rename, move, duplicate, delete, export"));
}

void SuneerPageModelGallery::cardMenu(const Card& c, const QPoint& globalPos)
{
	QMenu menu(this);
	QAction* rename = menu.addAction(tr("Rename..."));
	QMenu* moveMenu = menu.addMenu(tr("Move to group"));
	QHash<QAction*, QString> moveTo;
	for (const SuneerPageGroup& g : SuneerPageModels::groups())
	{
		if (g.id == m_group)
			continue;
		QAction* a = moveMenu->addAction(groupTitle(g));
		moveTo.insert(a, g.id);
	}
	QAction* dup = menu.addAction(tr("Duplicate"));
	QAction* del = menu.addAction(tr("Delete..."));
	menu.addSeparator();
	QAction* exp = menu.addAction(tr("Export this model..."));
	QAction* a = menu.exec(globalPos);
	if (!a)
		return;
	QString err;
	const QString id = c.id;
	if (a == rename)
	{
		bool ok = false;
		const QString n = QInputDialog::getText(this, tr("Rename page model"), tr("Name:"), QLineEdit::Normal, c.model.name, &ok);
		if (ok && !n.trimmed().isEmpty() && !SuneerPageModels::rename(m_group, id, n, &err))
			QMessageBox::warning(this, tr("Page Models"), err);
	}
	else if (a == dup)
	{
		if (SuneerPageModels::duplicate(m_group, id, &err).isEmpty())
			QMessageBox::warning(this, tr("Page Models"), err);
	}
	else if (a == del)
	{
		if (QMessageBox::question(this, tr("Delete page model"), tr("Delete \"%1\" from %2? This cannot be undone.").arg(c.model.name, groupTitle(SuneerPageModels::group(m_group)))) == QMessageBox::Yes)
			if (!SuneerPageModels::remove(m_group, id, &err))
				QMessageBox::warning(this, tr("Page Models"), err);
	}
	else if (a == exp)
	{
		const QString path = QFileDialog::getSaveFileName(this, tr("Export page model"), QDir::homePath() + QStringLiteral("/%1.pagemodel.json").arg(id), tr("Page model (*.json)"));
		if (!path.isEmpty())
		{
			QFile f(path);
			if (f.open(QIODevice::WriteOnly))
				f.write(QJsonDocument(c.model.toJson()).toJson(QJsonDocument::Indented));
			else
				QMessageBox::warning(this, tr("Page Models"), tr("Cannot write %1").arg(path));
		}
		return;
	}
	else if (moveTo.contains(a))
	{
		if (!SuneerPageModels::moveModel(m_group, id, moveTo.value(a), &err))
			QMessageBox::warning(this, tr("Page Models"), err);
	}
	fillGroups(m_group);
}

void SuneerPageModelGallery::applyModel(const Card& c)
{
	if (!m_src.apply)
		return;
	const SuneerPageGroup g = SuneerPageModels::group(m_group);
	const QString report = m_src.apply(c.model, g);
	if (report.isEmpty())
		return;                                  // cancelled
	QMessageBox box(this);
	box.setWindowTitle(tr("Page model skeleton"));
	box.setIcon(QMessageBox::Information);
	box.setText(report);
	box.setStandardButtons(QMessageBox::Ok);
	box.exec();
	accept();
}

void SuneerPageModelGallery::createModels()
{
	SuneerPageModelImportDialog dlg(m_group, this);
	dlg.exec();
	if (dlg.imported() > 0)
	{
		const QStringList gs = dlg.importedGroups();
		fillGroups(gs.contains(m_group) || gs.isEmpty() ? m_group : gs.first());
	}
}

void SuneerPageModelGallery::exportModels()
{
	QMessageBox q(this);
	q.setWindowTitle(tr("Export page models"));
	q.setText(tr("Export which groups?"));
	QPushButton* one = q.addButton(tr("This group (%1)").arg(groupTitle(SuneerPageModels::group(m_group))), QMessageBox::AcceptRole);
	QPushButton* all = q.addButton(tr("All groups"), QMessageBox::AcceptRole);
	q.addButton(QMessageBox::Cancel);
	q.exec();
	QStringList ids;
	if (q.clickedButton() == one)
		ids << m_group;
	else if (q.clickedButton() != all)
		return;
	const QString stem = ids.isEmpty() ? QStringLiteral("page-models-all") : QStringLiteral("page-models-%1").arg(m_group);
	const QString path = QFileDialog::getSaveFileName(this, tr("Export page models"), QDir::homePath() + QStringLiteral("/%1.json").arg(stem), tr("Page models (*.json)"));
	if (path.isEmpty())
		return;
	QString err;
	if (!SuneerPageModels::exportGroups(ids, path, &err))
		QMessageBox::warning(this, tr("Page Models"), err);
	else
		QMessageBox::information(this, tr("Page Models"), tr("Written: %1").arg(path));
}

void SuneerPageModelGallery::importModels()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Import page models"), QDir::homePath(), tr("Page models (*.json)"));
	if (path.isEmpty())
		return;
	QString err;
	QStringList report;
	const int n = SuneerPageModels::importFile(path, &err, &report);
	if (!err.isEmpty())
		QMessageBox::warning(this, tr("Page Models"), err);
	QMessageBox::information(this, tr("Page Models"), tr("%n model(s) imported.", "", n) + (report.isEmpty() ? QString() : QStringLiteral("\n\n") + report.mid(0, 30).join('\n')));
	fillGroups(m_group);
}

void SuneerPageModelGallery::groupAdd()
{
	SuneerPageGroup g;
	SuneerPageGroupDialog dlg(g, true, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	const SuneerPageGroup r = dlg.result();
	QString err;
	const QString id = SuneerPageModels::addGroup(r.name, r.grid, r.note, &err);
	if (id.isEmpty())
		QMessageBox::warning(this, tr("Page Models"), err);
	fillGroups(id.isEmpty() ? m_group : id);
}

void SuneerPageModelGallery::groupRename()
{
	const SuneerPageGroup g = SuneerPageModels::group(m_group);
	bool ok = false;
	const QString n = QInputDialog::getText(this, tr("Rename group"), tr("Name:"), QLineEdit::Normal, g.name, &ok);
	if (!ok || n.trimmed().isEmpty())
		return;
	QString err;
	if (!SuneerPageModels::renameGroup(m_group, n, &err))
		QMessageBox::warning(this, tr("Page Models"), err);
	fillGroups(m_group);
}

void SuneerPageModelGallery::groupEdit()
{
	const SuneerPageGroup g = SuneerPageModels::group(m_group);
	SuneerPageGroupDialog dlg(g, false, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	const SuneerPageGroup r = dlg.result();
	QString err;
	if (!SuneerPageModels::setGroupInfo(m_group, r.note, r.grid, &err) || (r.name != g.name && !SuneerPageModels::renameGroup(m_group, r.name, &err)))
		QMessageBox::warning(this, tr("Page Models"), err);
	fillGroups(m_group);
}

void SuneerPageModelGallery::groupDelete()
{
	const SuneerPageGroup g = SuneerPageModels::group(m_group);
	const int n = SuneerPageModels::modelCount(m_group);
	QString moveTo;
	if (n > 0)
	{
		QStringList names, ids;
		for (const SuneerPageGroup& o : SuneerPageModels::groups())
			if (o.id != m_group)
			{
				names << groupTitle(o);
				ids << o.id;
			}
		if (ids.isEmpty())
		{
			QMessageBox::information(this, tr("Delete group"), tr("The group holds %n model(s) and there is no other group to move them to.", "", n));
			return;
		}
		bool ok = false;
		const QString pick = QInputDialog::getItem(this, tr("Delete group"), tr("\"%1\" holds %n model(s). Move them to:", "", n).arg(groupTitle(g)), names, 0, false, &ok);
		if (!ok)
			return;
		moveTo = ids[qMax(0, names.indexOf(pick))];
	}
	else if (QMessageBox::question(this, tr("Delete group"), tr("Delete the empty group \"%1\"?").arg(groupTitle(g))) != QMessageBox::Yes)
		return;
	QString err;
	if (!SuneerPageModels::removeGroup(m_group, moveTo, &err))
		QMessageBox::warning(this, tr("Page Models"), err);
	fillGroups(moveTo.isEmpty() ? QString() : moveTo);
}

void SuneerPageModelGallery::groupMove(int delta)
{
	QString err;
	SuneerPageModels::moveGroup(m_group, delta, &err);
	fillGroups(m_group);
}

// ============================================================================ Group dialog

SuneerPageGroupDialog::SuneerPageGroupDialog(const SuneerPageGroup& g, bool isNew, QWidget* parent)
	: QDialog(parent), m_g(g)
{
	setWindowTitle(isNew ? tr("New page group") : tr("Page group: %1").arg(g.name));
	auto* fl = new QFormLayout(this);
	m_name = new QLineEdit(g.name, this);
	m_note = new QLineEdit(g.note, this);
	m_grid = new QComboBox(this);
	m_grid->addItem(tr("Normal pages: 8 columns (34.25 mm, 8 mm gutter)"), QStringLiteral("normal"));
	m_grid->addItem(tr("Obituary: 10 x 30.3 mm obit grid (Obituary module rules)"), QStringLiteral("obituary"));
	m_grid->setCurrentIndex(g.isObituary() ? 1 : 0);
	fl->addRow(tr("Name:"), m_name);
	fl->addRow(tr("Note:"), m_note);
	fl->addRow(tr("Grid:"), m_grid);
	auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	fl->addRow(bb);
}

SuneerPageGroup SuneerPageGroupDialog::result() const
{
	SuneerPageGroup g = m_g;
	g.name = m_name->text().trimmed();
	g.note = m_note->text().trimmed();
	g.grid = m_grid->currentData().toString();
	return g;
}

// ============================================================================ Import dialog

SuneerPageModelImportDialog::SuneerPageModelImportDialog(const QString& defaultGroup, QWidget* parent)
	: QDialog(parent), m_defaultGroup(defaultGroup)
{
	setWindowTitle(tr("Create Page Models from old pages"));
	setMinimumSize(900, 600);
	resize(1100, 720);
	auto* vl = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("1. Add old pages (.sla files or a whole folder; Scribus 1.4 / 1.5 / 1.7).  2. Read pages.  3. Check group and name in the list, untick what you do not want.  4. Import.\n"
	                            "Only the layout is kept (frames, columns, sizes, ad areas) - never the text or the pictures."), this);
	intro->setWordWrap(true);
	vl->addWidget(intro);
	auto* fileRow = new QHBoxLayout();
	m_fileList = new QListWidget(this);
	m_fileList->setMaximumHeight(110);
	m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	fileRow->addWidget(m_fileList, 1);
	auto* fb = new QVBoxLayout();
	m_addBtn = new QPushButton(tr("Add files..."), this);
	m_folderBtn = new QPushButton(tr("Add folder..."), this);
	m_removeBtn = new QPushButton(tr("Remove"), this);
	m_scanBtn = new QPushButton(tr("Read pages"), this);
	m_scanBtn->setDefault(true);
	for (QPushButton* b : { m_addBtn, m_folderBtn, m_removeBtn, m_scanBtn })
	{
		if (b != m_scanBtn)
			b->setAutoDefault(false);
		fb->addWidget(b);
	}
	fb->addStretch(1);
	fileRow->addLayout(fb);
	vl->addLayout(fileRow);
	m_progress = new QProgressBar(this);
	m_progress->setVisible(false);
	vl->addWidget(m_progress);
	m_status = new QLabel(tr("No pages read yet."), this);
	m_status->setWordWrap(true);
	vl->addWidget(m_status);
	m_table = new QTableWidget(0, 6, this);
	m_table->setHorizontalHeaderLabels({ tr("Import"), tr("Page"), tr("File / page"), tr("Group"), tr("Model name"), tr("Content") });
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	m_table->verticalHeader()->setDefaultSectionSize(130);
	m_table->setSelectionMode(QAbstractItemView::NoSelection);
	vl->addWidget(m_table, 1);
	auto* bb = new QDialogButtonBox(this);
	m_importBtn = bb->addButton(tr("Import ticked pages"), QDialogButtonBox::AcceptRole);
	m_importBtn->setEnabled(false);
	m_importBtn->setAutoDefault(false);
	auto* allBtn = bb->addButton(tr("Tick all"), QDialogButtonBox::ActionRole);
	auto* noneBtn = bb->addButton(tr("Untick all"), QDialogButtonBox::ActionRole);
	allBtn->setAutoDefault(false);
	noneBtn->setAutoDefault(false);
	bb->addButton(QDialogButtonBox::Close);
	vl->addWidget(bb);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(allBtn, &QPushButton::clicked, this, [this]() { for (Row& r : m_rows) if (r.tick) r.tick->setChecked(r.error.isEmpty()); });
	connect(noneBtn, &QPushButton::clicked, this, [this]() { for (Row& r : m_rows) if (r.tick) r.tick->setChecked(false); });
	connect(m_importBtn, &QPushButton::clicked, this, [this]() { importTicked(true); });
	connect(m_addBtn, &QPushButton::clicked, this, &SuneerPageModelImportDialog::addFiles);
	connect(m_folderBtn, &QPushButton::clicked, this, &SuneerPageModelImportDialog::addFolder);
	connect(m_removeBtn, &QPushButton::clicked, this, &SuneerPageModelImportDialog::removeSelected);
	connect(m_scanBtn, &QPushButton::clicked, this, &SuneerPageModelImportDialog::scan);
}

SuneerPageModelImportDialog::~SuneerPageModelImportDialog()
{
	if (m_proc)
	{
		m_proc->disconnect();
		if (m_proc->state() != QProcess::NotRunning)
		{
			m_proc->kill();
			m_proc->waitForFinished(2000);
		}
	}
}

void SuneerPageModelImportDialog::setFiles(const QStringList& files)
{
	m_files = files;
	m_fileList->clear();
	for (const QString& f : m_files)
		m_fileList->addItem(f);
}

void SuneerPageModelImportDialog::addFiles()
{
	QSettings s("Faircode", "ScribusNews");
	const QString start = s.value("pageModels/lastDir", QDir::homePath()).toString();
	const QStringList files = QFileDialog::getOpenFileNames(this, tr("Old pages"), start, tr("Scribus pages (*.sla *.sla.gz)"));
	if (files.isEmpty())
		return;
	s.setValue("pageModels/lastDir", QFileInfo(files.first()).absolutePath());
	QStringList all = m_files;
	for (const QString& f : files)
		if (!all.contains(f))
			all << f;
	setFiles(all);
}

void SuneerPageModelImportDialog::addFolder()
{
	QSettings s("Faircode", "ScribusNews");
	const QString start = s.value("pageModels/lastDir", QDir::homePath()).toString();
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of old pages"), start);
	if (dir.isEmpty())
		return;
	s.setValue("pageModels/lastDir", dir);
	QStringList all = m_files;
	const QStringList found = QDir(dir).entryList({ QStringLiteral("*.sla"), QStringLiteral("*.sla.gz") }, QDir::Files, QDir::Name);
	for (const QString& f : found)
		if (!all.contains(dir + '/' + f))
			all << dir + '/' + f;
	setFiles(all);
	if (found.isEmpty())
		m_status->setText(tr("No .sla file in %1").arg(dir));
}

void SuneerPageModelImportDialog::removeSelected()
{
	QStringList keep;
	for (int i = 0; i < m_fileList->count(); ++i)
		if (!m_fileList->item(i)->isSelected())
			keep << m_fileList->item(i)->text();
	setFiles(keep);
}

void SuneerPageModelImportDialog::setBusy(bool on)
{
	m_scanBtn->setEnabled(!on);
	m_addBtn->setEnabled(!on);
	m_folderBtn->setEnabled(!on);
	m_removeBtn->setEnabled(!on);
	m_progress->setVisible(on);
	if (on)
		m_importBtn->setEnabled(false);
}

void SuneerPageModelImportDialog::scan()
{
	if (m_files.isEmpty())
	{
		m_status->setText(tr("Add files or a folder first."));
		return;
	}
	if (m_proc && m_proc->state() != QProcess::NotRunning)
		return;
	m_rows.clear();
	m_table->setRowCount(0);
	m_stdoutBuf.clear();
	m_stderrTail.clear();
	const QString script = SuneerPageModels::extractorScript();
	if (!QFileInfo::exists(script))
	{
		m_status->setText(tr("The page reader is missing: %1").arg(script));
		return;
	}
	if (!m_proc)
	{
		m_proc = new QProcess(this);
		connect(m_proc, &QProcess::readyReadStandardOutput, this, &SuneerPageModelImportDialog::onOutput);
		connect(m_proc, &QProcess::readyReadStandardError, this, &SuneerPageModelImportDialog::onOutput);
		connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
	}
	setBusy(true);
	m_progress->setRange(0, 0);
	m_status->setText(tr("Reading %n file(s)...", "", m_files.size()));
	m_proc->start(QStringLiteral("python3"), QStringList() << QStringLiteral("-I") << script << m_files);
	if (!m_proc->waitForStarted(5000))
	{
		setBusy(false);
		m_status->setText(tr("Cannot start python3: %1").arg(m_proc->errorString()));
	}
}

bool SuneerPageModelImportDialog::waitForScan(int ms)
{
	QElapsedTimer t;
	t.start();
	while (m_proc && m_proc->state() != QProcess::NotRunning && t.elapsed() < ms)
		qApp->processEvents(QEventLoop::AllEvents, 50);
	return !m_proc || m_proc->state() == QProcess::NotRunning;
}

void SuneerPageModelImportDialog::onOutput()
{
	if (!m_proc)
		return;
	const QByteArray err = m_proc->readAllStandardError();
	if (!err.isEmpty())
	{
		for (const QByteArray& l : err.split('\n'))
		{
			const QString line = QString::fromUtf8(l).trimmed();
			if (line.startsWith(QLatin1String("PROGRESS ")))
			{
				const QStringList parts = line.split(' ');
				if (parts.size() >= 3)
				{
					m_progress->setRange(0, qMax(1, parts[2].toInt()));
					m_progress->setValue(parts[1].toInt());
					m_status->setText(tr("Reading page %1 of %2: %3").arg(parts[1].toInt() + 1).arg(parts[2].toInt()).arg(parts.mid(3).join(' ')));
				}
			}
			else if (!line.isEmpty())
				m_stderrTail = (m_stderrTail + '\n' + line).right(2000);
		}
	}
	m_stdoutBuf += m_proc->readAllStandardOutput();
	int nl;
	while ((nl = m_stdoutBuf.indexOf('\n')) >= 0)
	{
		const QByteArray line = m_stdoutBuf.left(nl);
		m_stdoutBuf.remove(0, nl + 1);
		const QJsonObject o = QJsonDocument::fromJson(line).object();
		if (o.isEmpty())
			continue;
		Row r;
		r.file = o["file"].toString();
		r.page = o["page"].toInt();
		r.pages = o["pages"].toInt(1);
		r.error = o["error"].toString();
		if (r.error.isEmpty())
		{
			r.model = SuneerPageModel::fromJson(o["model"].toObject());
			r.pages = r.model.sourcePages;
			r.model.name = SuneerPageModels::suggestName(r.file, r.page, r.pages);
			r.suggested = SuneerPageModels::suggestGroup(r.file, r.page, r.pages, r.model.sourcePageNum, r.model.obits.size(), &r.why);
			if (r.suggested.isEmpty())
			{
				r.suggested = r.model.isObituary() && SuneerPageModels::hasGroup(QStringLiteral("obituary")) ? QStringLiteral("obituary") : m_defaultGroup;
				r.why = tr("no hint - the current group");
			}
		}
		m_rows << r;
	}
}

void SuneerPageModelImportDialog::onFinished(int code)
{
	onOutput();
	setBusy(false);
	buildTable();
	int bad = 0;
	for (const Row& r : m_rows)
		if (!r.error.isEmpty())
			++bad;
	QString s = tr("%n page(s) read", "", m_rows.size());
	if (bad)
		s += tr(", %n could not be read", "", bad);
	if (code != 0)
		s += tr(" (reader exit code %1)").arg(code);
	if (m_rows.isEmpty() && !m_stderrTail.isEmpty())
		s += QStringLiteral("\n") + m_stderrTail.right(600);
	s += tr(". Check the group of every page, then Import.");
	m_status->setText(s);
	m_importBtn->setEnabled(!m_rows.isEmpty());
}

void SuneerPageModelImportDialog::buildTable()
{
	const bool dark = darkPalette(this);
	const QList<SuneerPageGroup> groups = SuneerPageModels::groups();
	m_table->setRowCount(m_rows.size());
	for (int i = 0; i < m_rows.size(); ++i)
	{
		Row& r = m_rows[i];
		r.tick = new QCheckBox(m_table);
		r.tick->setChecked(r.error.isEmpty());
		r.tick->setEnabled(r.error.isEmpty());
		auto* tw = new QWidget(m_table);
		auto* tl = new QHBoxLayout(tw);
		tl->setContentsMargins(6, 0, 0, 0);
		tl->addWidget(r.tick);
		tl->addStretch(1);
		m_table->setCellWidget(i, 0, tw);
		auto* thumb = new QLabel(m_table);
		if (r.error.isEmpty())
			thumb->setPixmap(QPixmap::fromImage(r.model.thumbnail(160, dark)).scaledToHeight(120, Qt::SmoothTransformation));
		else
			thumb->setText(tr("(error)"));
		thumb->setAlignment(Qt::AlignCenter);
		m_table->setCellWidget(i, 1, thumb);
		QString where = QFileInfo(r.file).fileName();
		if (r.pages > 1)
			where += tr("\npage %1 of %2").arg(r.page + 1).arg(r.pages);
		if (!r.model.sourceVersion.isEmpty())
			where += QStringLiteral("\nScribus %1").arg(r.model.sourceVersion);
		if (!r.error.isEmpty())
			where += QStringLiteral("\n") + r.error;
		else
			where += QStringLiteral("\n") + r.why;
		auto* whereItem = new QTableWidgetItem(where);
		whereItem->setFlags(Qt::ItemIsEnabled);
		whereItem->setToolTip(r.file);
		m_table->setItem(i, 2, whereItem);
		r.group = new QComboBox(m_table);
		for (const SuneerPageGroup& g : groups)
			r.group->addItem(groupTitle(g), g.id);
		r.group->setCurrentIndex(qMax(0, r.group->findData(r.suggested)));
		r.group->setEnabled(r.error.isEmpty());
		m_table->setCellWidget(i, 3, r.group);
		r.name = new QLineEdit(r.model.name, m_table);
		r.name->setEnabled(r.error.isEmpty());
		m_table->setCellWidget(i, 4, r.name);
		auto* content = new QTableWidgetItem(r.error.isEmpty() ? r.model.summary().replace(QStringLiteral(" · "), QStringLiteral("\n")) : QString());
		content->setFlags(Qt::ItemIsEnabled);
		m_table->setItem(i, 5, content);
	}
	m_table->resizeColumnToContents(1);
	m_table->resizeColumnToContents(3);
}

int SuneerPageModelImportDialog::importTicked(bool askDuplicates)
{
	QList<int> todo;
	for (int i = 0; i < m_rows.size(); ++i)
		if (m_rows[i].tick && m_rows[i].tick->isChecked() && m_rows[i].error.isEmpty())
			todo << i;
	if (todo.isEmpty())
	{
		m_status->setText(tr("Nothing ticked."));
		return 0;
	}
	QProgressDialog prog(tr("Importing page models..."), tr("Stop"), 0, todo.size(), this);
	prog.setWindowModality(Qt::WindowModal);
	prog.setMinimumDuration(0);
	int done = 0, skipped = 0, failed = 0;
	enum { Ask, SkipAll, KeepAll } dupMode = askDuplicates ? Ask : SkipAll;
	QStringList lines;
	for (int k = 0; k < todo.size(); ++k)
	{
		if (prog.wasCanceled())
			break;
		Row& r = m_rows[todo[k]];
		prog.setValue(k);
		prog.setLabelText(tr("Importing %1 (%2 of %3)").arg(r.name->text()).arg(k + 1).arg(todo.size()));
		qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
		SuneerPageModel m = r.model;
		m.group = r.group->currentData().toString();
		m.name = r.name->text().trimmed();
		m.id.clear();
		const SuneerPageGroup g = SuneerPageModels::group(m.group);
		if (g.isObituary() && m.gridType != QLatin1String("obituary") && !m.obits.isEmpty())
			m.gridType = QStringLiteral("obituary");
		QString otherName;
		const double alike = SuneerPageModels::mostAlike(m, m.group, &otherName);
		if (alike >= 0.9)
		{
			bool skip = dupMode == SkipAll;
			if (dupMode == Ask)
			{
				QMessageBox q(this);
				q.setWindowTitle(tr("Almost the same model"));
				q.setIcon(QMessageBox::Question);
				q.setText(tr("\"%1\" is almost identical (%2 %) to \"%3\" already in %4.").arg(m.name).arg(qRound(alike * 100)).arg(otherName, groupTitle(g)));
				QPushButton* bSkip = q.addButton(tr("Skip"), QMessageBox::RejectRole);
				QPushButton* bKeep = q.addButton(tr("Keep both"), QMessageBox::AcceptRole);
				QPushButton* bSkipAll = q.addButton(tr("Skip all duplicates"), QMessageBox::RejectRole);
				QPushButton* bKeepAll = q.addButton(tr("Keep all"), QMessageBox::AcceptRole);
				q.exec();
				if (q.clickedButton() == bSkipAll)
					dupMode = SkipAll;
				else if (q.clickedButton() == bKeepAll)
					dupMode = KeepAll;
				skip = q.clickedButton() == bSkip || q.clickedButton() == bSkipAll;
				Q_UNUSED(bKeep);
			}
			if (skip)
			{
				++skipped;
				lines << tr("%1: skipped, %2 % like \"%3\" in %4").arg(m.name).arg(qRound(alike * 100)).arg(otherName, groupTitle(g));
				r.tick->setChecked(false);
				continue;
			}
		}
		QString err;
		if (SuneerPageModels::save(m, &err))
		{
			++done;
			if (!m_importedGroups.contains(m.group))
				m_importedGroups << m.group;
			lines << tr("%1 -> %2").arg(m.name, groupTitle(g));
			r.tick->setChecked(false);
			r.tick->setEnabled(false);
			r.name->setText(m.name);
			r.name->setEnabled(false);
			r.group->setEnabled(false);
		}
		else
		{
			++failed;
			lines << tr("%1: %2").arg(m.name, err);
		}
	}
	prog.setValue(todo.size());
	m_imported += done;
	QString s = tr("%n model(s) imported", "", done);
	if (skipped)
		s += tr(", %n skipped as duplicates", "", skipped);
	if (failed)
		s += tr(", %n failed", "", failed);
	m_status->setText(s + QStringLiteral("\n") + lines.mid(0, 12).join('\n'));
	m_importBtn->setEnabled(false);
	for (const Row& r : m_rows)
		if (r.tick && r.tick->isEnabled())
			m_importBtn->setEnabled(true);
	return done;
}

