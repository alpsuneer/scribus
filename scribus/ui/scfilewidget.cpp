/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <cstdlib>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include "filedialogeventcatcher.h"
#include "netpathguard.h"
#include "scfilewidget.h"
#include "scribus.h"
#include "scribuscore.h"

namespace
{
	// Places taken out of QtProject.conf because their folder did not answer.
	// They go back in when the widget is destroyed, so a bookmark to the office
	// share survives a day spent off the network.
	QStringList s_hiddenShortcuts;
	QStringList s_hiddenHistory;

	QSettings* qtDialogSettings()
	{
		// The same file and group QFileDialog itself reads and writes.
		QSettings* settings = new QSettings(QSettings::UserScope, QStringLiteral("QtProject"));
		settings->beginGroup(QStringLiteral("FileDialog"));
		return settings;
	}

	QString localPathOf(const QString& urlString)
	{
		const QUrl url(urlString);
		return url.isLocalFile() ? url.toLocalFile() : QString();
	}

	// Mount points below /media, read from the kernel's table. Unlike
	// QStorageInfo::mountedVolumes() this does not statfs() every mount, which
	// is what hung every file dialog while an NFS server was away.
	QStringList mediaMountPoints()
	{
		QStringList points;
		QFile f(QStringLiteral("/proc/self/mountinfo"));
		if (!f.open(QIODevice::ReadOnly))
			return points;
		const QList<QByteArray> lines = f.readAll().split('\n');
		for (const QByteArray& line : lines)
		{
			const QList<QByteArray> fields = line.split(' ');
			if (fields.count() < 5)
				continue;
			// The mount point is field 5; space, tab, newline and backslash
			// are written as \ooo octal escapes.
			const QByteArray raw = fields.at(4);
			QByteArray path;
			for (int i = 0; i < raw.size(); ++i)
			{
				if (raw.at(i) == '\\' && i + 3 < raw.size())
				{
					bool ok = false;
					const int code = raw.mid(i + 1, 3).toInt(&ok, 8);
					if (ok)
					{
						path += char(code);
						i += 3;
						continue;
					}
				}
				path += raw.at(i);
			}
			const QString point = QFile::decodeName(path);
			if (point.startsWith(QLatin1String("/media")) && !points.contains(point))
				points << point;
		}
		return points;
	}
}

QString ScFileWidget::safeStartDirectory(const QString& dir)
{
	if (dir.isEmpty() || NetPathGuard::reachable(dir))
		return dir;
	QString fallback = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	if (fallback.isEmpty() || !NetPathGuard::reachable(fallback))
		fallback = QDir::homePath();
	return fallback;
}

QWidget* ScFileWidget::hideUnreachablePlaces(QWidget* parent)
{
	// Qt also looks at the process's current folder; Scribus moves that to the
	// folder of whatever was opened last.
	const QString cwd = QDir::currentPath();

	QScopedPointer<QSettings> settings(qtDialogSettings());
	const QStringList shortcuts = settings->value(QStringLiteral("shortcuts")).toStringList();
	const QStringList history = settings->value(QStringLiteral("history")).toStringList();
	const QString lastVisited = settings->value(QStringLiteral("lastVisited")).toString();

	// Ask everything at once, so several dead places cost one timeout.
	QStringList all;
	all << cwd << localPathOf(lastVisited);
	for (const QString& u : shortcuts)
		all << localPathOf(u);
	for (const QString& u : history)
		all << localPathOf(u);
	all.removeAll(QString());
	NetPathGuard::precheck(all);

	if (!NetPathGuard::reachable(cwd))
		QDir::setCurrent(QDir::homePath());

	QStringList keptShortcuts, keptHistory;
	for (const QString& u : shortcuts)
	{
		if (NetPathGuard::reachable(localPathOf(u)))
			keptShortcuts << u;
		else if (!s_hiddenShortcuts.contains(u))
			s_hiddenShortcuts << u;
	}
	for (const QString& u : history)
	{
		if (NetPathGuard::reachable(localPathOf(u)))
			keptHistory << u;
		else if (!s_hiddenHistory.contains(u))
			s_hiddenHistory << u;
	}
	if (keptShortcuts.count() != shortcuts.count())
		settings->setValue(QStringLiteral("shortcuts"), keptShortcuts);
	if (keptHistory.count() != history.count())
		settings->setValue(QStringLiteral("history"), keptHistory);
	if (!NetPathGuard::reachable(localPathOf(lastVisited)))
		settings->setValue(QStringLiteral("lastVisited"), QUrl::fromLocalFile(QDir::homePath()).toString());
	return parent;
}

void ScFileWidget::restoreHiddenPlaces()
{
	if (s_hiddenShortcuts.isEmpty() && s_hiddenHistory.isEmpty())
		return;
	QScopedPointer<QSettings> settings(qtDialogSettings());
	QStringList shortcuts = settings->value(QStringLiteral("shortcuts")).toStringList();
	for (const QString& u : std::as_const(s_hiddenShortcuts))
	{
		if (!shortcuts.contains(u))
			shortcuts << u;
	}
	settings->setValue(QStringLiteral("shortcuts"), shortcuts);
	QStringList history = settings->value(QStringLiteral("history")).toStringList();
	for (const QString& u : std::as_const(s_hiddenHistory))
	{
		if (!history.contains(u))
			history << u;
	}
	settings->setValue(QStringLiteral("history"), history);
	s_hiddenShortcuts.clear();
	s_hiddenHistory.clear();
}

ScFileWidget::~ScFileWidget()
{
	// QFileDialog remembers the last folder of any dialog in a process-wide
	// variable and stats it while building the next one. Leave it on a local
	// folder; Scribus always passes the folder it wants, so nothing is lost.
	// Done every time, not only when the folder is dead now: the server can
	// go away between this dialog and the next.
	setDirectory(QDir::homePath());
}

ScFileWidget::ScFileWidget(QWidget * parent, fwContextFlags contextFlags) : QFileDialog(hideUnreachablePlaces(parent), Qt::Widget)
{
	// After ~QFileDialog has written its settings back.
	connect(this, &QObject::destroyed, [] { ScFileWidget::restoreHiddenPlaces(); });
	setOption(QFileDialog::DontUseNativeDialog);
	setSizeGripEnabled(false);
	setModal(false);
	setViewMode(QFileDialog::List);
	setWindowFlags(Qt::Widget);

	// The margins' content should be set both on the widget and its layout.
	setContentsMargins(0, 0, 0, 0);
	layout()->setContentsMargins(0, 0, 0, 0);

#ifdef Q_OS_MACOS
	QList<QUrl> urls;
	QUrl macOSUrl(QUrl::fromLocalFile(QLatin1String("")));
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	macOSUrl = QUrl::fromLocalFile("/Volumes");
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	macOSUrl = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	macOSUrl = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	macOSUrl = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	macOSUrl = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
	if (!urls.contains(macOSUrl))
		urls << macOSUrl;
	if (contextFlags & contextImages)
	{
		macOSUrl = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
		if (!urls.contains(macOSUrl))
			urls << macOSUrl;
	}
	setSidebarUrls(urls);
#endif

#ifdef Q_OS_LINUX
	QList<QUrl> urls(sidebarUrls());
	QUrl linuxOSUrl;
	const QStringList volumes = NetPathGuard::reachableOnly(mediaMountPoints());
	for (const QString& volume : volumes)
	{
		linuxOSUrl = QUrl::fromLocalFile(volume);
		if (!urls.contains(linuxOSUrl))
			urls << linuxOSUrl;
	}
	setSidebarUrls(urls);
#endif
	if (ScCore && ScCore->primaryMainWindow())
		ScCore->primaryMainWindow()->showNetworkPathNotes();

	FileDialogEventCatcher* keyCatcher = new FileDialogEventCatcher(this);
	QList<QListView *> childListViews = findChildren<QListView *>();
	for (QListView * lvi : std::as_const(childListViews))
		lvi->installEventFilter(keyCatcher);
	connect(keyCatcher, SIGNAL(escapePressed()), this, SLOT(reject()));
	connect(keyCatcher, SIGNAL(dropLocation(QString)), this, SLOT(locationDropped(QString)));
	connect(keyCatcher, SIGNAL(desktopPressed()), this, SLOT(gotoDesktopDirectory()));
	connect(keyCatcher, SIGNAL(homePressed()), this, SLOT(gotoHomeDirectory()));
	connect(keyCatcher, SIGNAL(parentPressed()), this, SLOT(gotoParentDirectory()));
	connect(keyCatcher, SIGNAL(enterSelectedPressed()), this, SLOT(gotoSelectedDirectory()));

	QList<QPushButton *> childPushButtons = findChildren<QPushButton *>();
	for (QPushButton* pb : std::as_const(childPushButtons))
		pb->setVisible(false);
	setMinimumSize(QSize(480, 310));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void ScFileWidget::forceDoubleClickActivation(bool force)
{
	// Hack to make the previews in our file dialogs usable again,
	// needed e.g on OpenSuse KDE. Otherwise file would open on first
	// click, leaving user no time to see preview.
	if (m_forceDoubleClickActivation == force)
		return;

	if (force)
		setStyleSheet(QStringLiteral("QAbstractItemView { activate-on-singleclick: 0; }"));
	else
		setStyleSheet(QString());
	m_forceDoubleClickActivation = force;
}

QString ScFileWidget::selectedFile()
{
	QStringList l(selectedFiles());
	if (l.count() == 0)
		return QString();
	return l.at(0);
}

void ScFileWidget::locationDropped(const QString& fileUrl)
{
	QFileInfo fi(fileUrl);
	if (fi.isDir())
	{
		setDirectory(fi.absoluteFilePath());
		return;
	}

	QString absFilePath = fi.absolutePath();
	QString fileName = fi.fileName();
		
	setDirectory(absFilePath);
	selectFile(fileName);
}

void ScFileWidget::gotoParentDirectory()
{
	QDir d(directory());
	d.cdUp();
	setDirectory(d);
}

void ScFileWidget::gotoSelectedDirectory()
{
	QStringList s(selectedFiles());
	if (s.isEmpty())
		return;
	QFileInfo fi(s.first());
//	qDebug()<<s.first()<<fi.absoluteFilePath();
	if (fi.isDir())
		setDirectory(fi.absoluteFilePath());
}

void ScFileWidget::gotoDesktopDirectory()
{
	QString dp = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	QFileInfo fi(dp);
	if (fi.exists())
		setDirectory(dp);
}

void ScFileWidget::gotoHomeDirectory()
{
	QString dp = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
	QFileInfo fi(dp);
	if (fi.exists())
		setDirectory(dp);
}

