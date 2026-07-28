/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AUTOARRANGEDIALOG_H
#define AUTOARRANGEDIALOG_H

#include <QDialog>

#include "scribusapi.h"
#include "autoarrangeengine.h"

class QCheckBox;
class QLabel;
class QRadioButton;
class ScribusDoc;

/*!
 \brief Minimal modal dialog for "Auto Arrange Frames".

 Shows what was detected on the current page (columns, arrangeable frames,
 content-area size), the behaviour toggles (preserve column, auto-fit text
 heights, zero gap), the always-on protection notes, and the page scope.
 */
class SCRIBUS_API AutoArrangeDialog : public QDialog
{
	Q_OBJECT

public:
	AutoArrangeDialog(ScribusDoc* doc, QWidget* parent = nullptr);

	ArrangeOptions options() const;

private:
	ScribusDoc* m_doc { nullptr };

	QCheckBox* m_preserveColumn { nullptr };
	QCheckBox* m_zeroGap { nullptr };
	QCheckBox* m_includeGroups { nullptr };
	QCheckBox* m_dryRun { nullptr };
	// Frame height behaviour
	QRadioButton* m_heightKeep { nullptr };
	QRadioButton* m_heightFill { nullptr };
	QRadioButton* m_heightFit { nullptr };
	QRadioButton* m_scopeCurrent { nullptr };
	QRadioButton* m_scopeAll { nullptr };
};

#endif // AUTOARRANGEDIALOG_H
