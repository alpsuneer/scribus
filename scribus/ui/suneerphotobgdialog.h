/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERPHOTOBGDIALOG_H
#define SUNEERPHOTOBGDIALOG_H

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QStringList>

#include "scribusapi.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class PageItem;
class ScribusDoc;

/*!
 \brief "Photo background colour": one colour behind every selected cut-out
 photo (or every obituary photo of the page), live preview on the canvas,
 OK re-renders the files from the stored cut-outs, sets the frame fill to the
 named document colour and remembers it as the Obituary default.
 */
class SCRIBUS_API SuneerPhotoBgDialog : public QDialog
{
	Q_OBJECT
public:
	SuneerPhotoBgDialog(ScribusDoc* doc, const QList<PageItem*>& items, const QStringList& skipped, QWidget* parent = nullptr);
	~SuneerPhotoBgDialog() override;

private slots:
	void onDocColor(int idx);
	void onHex();
	void onRgb();
	void onCmyk();
	void onPick();
	void schedulePreview();
	void previewNow();
	void onOk();
	void onCancel();

private:
	struct Photo { QPointer<PageItem> item; QString cutPath; QString oldFile; bool manual; };
	void setColor(const QColor& c, const QString& from);
	void refreshFields(const QString& from);
	QImage composite(const Photo& ph, const QColor& c) const;
	void restoreAll();

	ScribusDoc* m_doc { nullptr };
	QList<Photo> m_photos;
	QStringList m_skipped;
	QColor m_color;
	QString m_name;
	bool m_previewed { false };
	bool m_updating { false };
	QHash<QString, QString> m_previewFiles;

	QComboBox* m_docCombo;
	QLineEdit* m_nameEdit;
	QLineEdit* m_hexEdit;
	QSpinBox* m_rgb[3];
	QSpinBox* m_cmyk[4];
	QPushButton* m_pickBtn;
	QLabel* m_swatch;
	QLabel* m_info;
	QProgressBar* m_progress;
	QPushButton *m_okBtn, *m_cancelBtn;
	QTimer* m_previewTimer;
};

#endif
