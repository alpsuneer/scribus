#ifndef SUNEERTEXTEFFECTS_H
#define SUNEERTEXTEFFECTS_H

#include <QString>
#include <QStringList>

class PageItem;
class ScribusDoc;
class ScribusMainWindow;

/*! Item > Text Effects...  (Bevel & Emboss)
 *
 * Shades the letters of a headline as if they were raised from, or pressed
 * into, the page. Works on a Fill Text with Image result, on a text frame, on
 * a shape and on a group of shapes (a headline converted to outlines).
 *
 * The result is always a "face": ONE image frame in the shape of the letters.
 * A text frame, shape or group is kept on the hidden, non-printing layer of
 * Fill Text with Image and a face with a plain-colour image is made from it.
 *
 * "Bake" writes highlight and shadow into a copy of the face image (and, for
 * the styles that reach outside the letters, into an opaque frame behind
 * them): nothing transparent reaches the PDF. "Live" leaves the face alone and
 * lays a Screen and a Multiply frame over it.
 *
 * The generated images live in "<document>_effects/" next to the .sla. The
 * settings are item attributes of the face, so the dialog opens with them.
 */
namespace SuneerTextEffects
{
	enum Style { InnerBevel = 0, OuterBevel, Emboss, PillowEmboss, Deboss };
	enum Technique { Smooth = 0, ChiselHard };
	enum OutputMode { Bake = 0, Live };

	struct Settings
	{
		int     style { InnerBevel };
		int     technique { Smooth };
		double  depth { 100.0 };            //!< percent
		bool    up { true };
		double  size { 3.0 };               //!< points
		double  soften { 0.0 };             //!< points
		double  angle { 120.0 };            //!< degrees, where the light comes from
		double  altitude { 30.0 };          //!< degrees
		QString highlightColor { "White" };
		double  highlightOpacity { 75.0 };  //!< percent
		QString shadowColor { "Black" };
		double  shadowOpacity { 75.0 };     //!< percent
		int     dpi { 300 };
		int     mode { Bake };
		QString backColor { "White" };      //!< Bake: what is behind the letters, for the styles that reach outside them

		QString toString() const;
		static Settings fromString(const QString& text);
	};

	QStringList presetNames();
	//! Changes the look in \a settings to the preset; resolution, output mode and background stay.
	bool applyPreset(const QString& name, Settings& settings);

	//! \return true when the dialog can be opened for \a item.
	bool canRunOn(const PageItem* item);

	//! Opens the Text Effects dialog for the selected item.
	void runForSelection(ScribusMainWindow* mw);

	//! Replaces the Bevel & Emboss of \a item. One undo step. \a warnings gets what the user should know.
	bool apply(ScribusMainWindow* mw, PageItem* item, const Settings& settings, QString* error, QStringList* warnings = nullptr);

	//! Takes the Bevel & Emboss of \a item away again. One undo step.
	bool remove(ScribusMainWindow* mw, PageItem* item, QString* error);

	//! Deletes the frames Bevel & Emboss put behind / over \a face. The caller owns the undo transaction.
	void deletePieces(ScribusDoc* doc, PageItem* face);

	//! Reads \a documentFile (.sla or .sla.gz) as it is on disk. False when it cannot be read.
	bool savedDocumentText(const QString& documentFile, QByteArray& text);

	//! Deletes the generated images the saved document does not use. Called when a document closes.
	void sweepUnusedFiles(const QString& documentFile);
}

#endif
