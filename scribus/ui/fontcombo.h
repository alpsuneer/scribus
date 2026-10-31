/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
/***************************************************************************
                          fontcombo.h  -  description
                             -------------------
    begin                : Die Jun 17 2003
    copyright            : (C) 2003 by Franz Schmid
    email                : Franz.Schmid@altmuehlnet.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef FONTCOMBO_H
#define FONTCOMBO_H

#include <QWidget>
#include <QComboBox>
#include <QFontDatabase>
#include <QValidator>

#include "scribusapi.h"
#include "ui/widgets/form_widget.h"

class QEvent;
class QFont;
class QGridLayout;
class QPixmap;
class QLabel;
class QTimer;
class PrefsManager;
class ScFace;
class ScribusDoc;


/**
  *@author Franz Schmid
  */

class SCRIBUS_API FontCombo : public QComboBox
{
	Q_OBJECT
public:
	FontCombo(QWidget* pa);
	~FontCombo() {}

	void RebuildList(ScribusDoc *currentDoc, bool forAnnotation = false, bool forSubstitute = false);

private:
	PrefsManager& prefsManager;
	QPixmap ttfFont;
	QPixmap otfFont;
	QPixmap psFont;
	QPixmap substFont;

private slots:
	void iconSetChange();
};

class SCRIBUS_API FontComboH : public QWidget
{
	Q_OBJECT

public:
	FontComboH(QWidget* parent, bool labels = false);
	~FontComboH() {}
	
	QString currentFont() const;
	void rebuildList(ScribusDoc *currentDoc, bool forAnnotation = false, bool forSubstitute = false);

	/*! suneer: the boxes are narrow and long font names are cut off, so the full name is shown in an
	 *  instant frameless popup (no tooltip delay, never takes focus): on hover over either box, while
	 *  stepping with Up/Down/wheel in a focused box (hides 1.5 s after the last step or on focus out),
	 *  and for the item under the mouse / arrow keys in an open list. The list itself is widened to the
	 *  longest name. A mixed selection shows "Mixed: A, B" instead of a name. */
	void setMixedFonts(const QStringList& fonts);
	//! both boxes empty (mixed selection); the popup lists the fonts
	void showMixed(const QStringList& fonts);
	QStringList mixedFonts() const { return m_mixedFonts; }
	bool eventFilter(QObject* obj, QEvent* ev) override;

	void setGuestWidget( QWidget * widget);
	QWidget* guestWidget();

public slots:
	void setCurrentFont(const QString& f);
	void languageChange();
	void iconSetChange();
	void toggleLabelVisibility(bool v);

signals:
	void fontSelected(QString);

protected slots:
	void familySelected(int id);
	void styleSelected(int id);
	void familyHighlighted(int id);
	void styleHighlighted(int id);

protected:
	// suneer: instant full-name popup
	class FontNamePopup* m_namePopup { nullptr };
	QTimer* m_popupHideTimer { nullptr };
	QStringList m_mixedFonts;
	bool m_popupKeyDriven { false };
	QString popupTextFor(const QString& fullName) const;
	void showNamePopup(QWidget* anchor, const QString& fullName, bool keyDriven, const QPoint* globalPos = nullptr);
	void hideNamePopup();
	void fitListWidth(QComboBox* box);
	QString fullNameForFamilyRow(int row) const;

protected:
	QComboBox* fontFamily { nullptr };
	QComboBox* fontStyle { nullptr };
	FormWidget *fontFaceLabel { nullptr };
	FormWidget *fontStyleLabel { nullptr };
	QGridLayout* fontComboLayout { nullptr };
	PrefsManager& prefsManager;
	ScribusDoc *currDoc { nullptr };
	bool showLabels { false };
	bool isForAnnotation { true };  // this is merely to ensure that the list is rebuilt
	bool isForSubstitute { false };
	QPixmap ttfFont;
	QPixmap otfFont;
	QPixmap psFont;
	QPixmap substFont;

	void changeEvent(QEvent *e) override;
};

/*! suneer: frameless, focus-free popup with the full font name, a short Malayalam/Latin sample drawn
 *  in that font, and the box's hint line. */
class SCRIBUS_API FontNamePopup : public QWidget
{
	Q_OBJECT
public:
	explicit FontNamePopup(QWidget* parent);
	void setContent(const QString& title, const QString& fullName, const QString& hint);
	void placeAt(const QPoint& globalTopLeft);
	//! beside the anchor: right of it, else left of it, else below - never over the anchor or the box under it
	void placeBeside(const QRect& anchorGlobal);
private:
	QLabel* m_title { nullptr };
	QLabel* m_sample { nullptr };
	QLabel* m_hint { nullptr };
};

// based on Qfontcombobox class from Qt project
class FontFamilyDelegate : public QAbstractItemDelegate
{
	Q_OBJECT

	public:
		explicit FontFamilyDelegate(QObject *parent);

		// painting
		void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
		bool helpEvent(QHelpEvent * event, QAbstractItemView * view, const QStyleOptionViewItem & option, const QModelIndex & index) override;
		QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

		QFontDatabase::WritingSystem writingSystem { QFontDatabase::Any };

	protected:
		QFont m_font;
};

class FontComboValidator : public QValidator
{
	Q_OBJECT

	public:
		FontComboValidator(QObject* parent = nullptr);
		virtual State validate(QString & input, int & pos) const;
};
#endif
