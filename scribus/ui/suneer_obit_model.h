/*
 Obituary layout model for the News Browser ("Layout model: Obituary").

 The model is an editable JSON file,
   <prefs>/layout-models/obituary.json      (~/.config/scribus/layout-models/)
 written with the house defaults (measured from Page-15_ObitKRN.sla) when it
 does not exist. Every size here is in mm unless the name says Pt or Px.

 SuneerObitPhotoWorker runs resources/obit/suneer_obit_photo.py once for a
 whole batch (rembg + face-centred crop), in the background, and reports
 progress per picture. Results are cached by the script (sidecar .json).
*/
#ifndef SUNEER_OBIT_MODEL_H
#define SUNEER_OBIT_MODEL_H

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

struct SuneerObitModel
{
	// grid
	int    gridColumns   {10};
	double gridGapMm     {3.0};
	double contentTopMm  {26.0};     // first module top (under the masthead)
	int    obitFirstCol  {1};        // 1-based, inclusive
	int    obitLastCol   {8};
	int    newsFirstCol  {9};
	int    newsLastCol   {10};
	// obit module
	double photoMm       {30.3};     // square photo frame
	double moduleGapMm   {2.3};      // vertical gap between modules
	double textTopDistPt {1.6};
	QString photoFill    {QStringLiteral("DB obit")};
	int    photoFillCMYK[4] {23, 0, 0, 23};
	QString styleName    {QStringLiteral("91 Obit Name")};
	QString stylePlace   {QStringLiteral("92 ObitPlace")};
	QString styleBody    {QStringLiteral("93 ObitBody")};
	QString styleNameNoPic  {QStringLiteral("Obit name no pic")};
	QString stylePlaceNoPic {QStringLiteral("95 ObitPlace no pic")};
	QString styleBodyNoPic  {QStringLiteral("96 ObitBody no pic")};
	// short news (columns 9-10)
	int    newsTextColumns {2};
	double newsColGapMm    {2.5};
	double newsGapMm       {2.0};
	struct HeadRule { int maxChars; QString style; };
	QList<HeadRule> newsHeadlines { {16, QStringLiteral("22 M")}, {22, QStringLiteral("20 M")}, {9999, QStringLiteral("18 M")} };
	QString newsDateline  {QStringLiteral("01 Dateline")};
	QString newsFirstBody {QStringLiteral("03 BodyNoInd")};
	QString newsBody      {QStringLiteral("02 BodyText")};
	// classification: obituary vs short news
	int titleMaxWords {3};
	int titleMaxChars {24};
	int bodyMaxChars  {450};
	// text options
	QString textAlign    {QStringLiteral("centre")};       // "centre" (house style) or "left"
	bool    runInPlace   {false};                           // place in bold + ":" and the details on the same line
	QString stylePlaceRunin {QStringLiteral("92 ObitPlace Runin")};   // character style, created when missing
	// photo processing
	int     photoPx      {600};
	int     photoQuality {95};
	QString photoBackground {QStringLiteral("Obit BG")};   // "Obit BG" (neutral light grey, created), "DB obit", or any document colour
	int     bgCMYK[4]    {2, 1, 0, 27};                     // the "Obit BG" document colour (C2 M1 Y0 K27 = #B6B8B9, reference strip 2026-10-13)
	//! An earlier default "Obit BG" CMYK (percent) that migrates to the current one.
	static bool isOldBgCMYK(const int* c);
	//! An earlier default "Obit BG" RGB that migrates to the current one.
	static bool isOldBgRGB(const int* c);
	int     bgRGB[3]     {182, 184, 185};                   // the exact tone written into every processed file (#B6B8B9)
	QString rembgModel   {QStringLiteral("u2net_human_seg")};
	QStringList bgModels {QStringLiteral("isnet-general-use"), QStringLiteral("u2net")};   // tried in turn when the first model keeps the background
	double  eyeY         {0.40};                            // unused since the face-fill crop; kept for old files
	double  faceH        {0.55};                            // old rule: hairline-chin / frame height (used when faceW is 0)
	double  headTop      {0.03};                            // old rule: top of the head below the frame top
	double  faceW        {0.60};                            // Haar face box width / frame width (reference strip: 60 %, face centred)
	double  faceTop      {0.18};                            // Haar face box top below the frame top, in frame widths (reference: 18 %)
	int     toneStrength {60};                              // 0-100
	QString frameShape   {QStringLiteral("square")};        // "square" (30.3 x 30.3) or "portrait" (30.3 x portraitHeightMm)
	double  portraitHeightMm {34.0};
	int     minPpi       {300};
	QString flagColor    {QStringLiteral("DB Orange")};
	double  photoFrameHeightMm() const { return frameShape == QLatin1String("portrait") ? portraitHeightMm : photoMm; }

	static QString filePath();                 // <prefs>/layout-models/obituary.json
	static SuneerObitModel load(QString* note = nullptr);   // defaults + file; writes the file when missing
	bool save(QString* error = nullptr) const;
	QJsonObject toJson() const;
	void fromJson(const QJsonObject& o);
};

//! One background run of suneer_obit_photo.py over a batch of pictures.
class SuneerObitPhotoWorker : public QObject
{
	Q_OBJECT
public:
	struct Item { QString id; QString src; QString out; };
	struct Result { QString id; QString status; QString out; QString message; };

	explicit SuneerObitPhotoWorker(QObject* parent = nullptr);
	~SuneerObitPhotoWorker() override;

	//! Where the script is: <share>/obit/suneer_obit_photo.py (or the source tree).
	static QString scriptPath();
	//! Starts the batch; false (with \a why) when nothing could be started.
	bool start(const QList<Item>& items, const SuneerObitModel& model, const QColor& background, const QString& workDir, QString* why);
	void cancel();
	bool running() const;
	const QList<Result>& results() const { return m_results; }

signals:
	void progress(int done, int total, const QString& id, const QString& status, const QString& message);
	void finished(bool ok, const QString& error);

private:
	void readOutput();
	QProcess* m_proc {nullptr};
	QList<Result> m_results;
	QString m_jobFile;
	QString m_stderr;
	bool m_done {false};
};

#endif
