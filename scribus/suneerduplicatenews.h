/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef SUNEERDUPLICATENEWS_H
#define SUNEERDUPLICATENEWS_H

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

/*! Suneer: the engine behind Extras > SR Tools > Duplicate News Checker.
 *
 *  Pure Qt Core so it can run on a worker thread. The GUI thread snapshots the
 *  stories of open documents into Story records; .sla files of a folder are
 *  read here, straight from the XML, without opening them as documents.
 *
 *  A story is one chain of linked text frames. Its headline is split from its
 *  body by paragraph style, then both are normalised (ZWJ/ZWNJ dropped, old and
 *  atomic chillus unified, spaces/punctuation/hyphenation ignored) and compared:
 *  headlines by edit distance, bodies by 7-character shingles. A body score is
 *  the share of characters inside passages both stories have; containment (the
 *  shorter story's share) catches a short story inside a longer one.
 */
namespace DupNews
{
	using Range = QPair<int, int>; //!< [first, end)

	struct Para
	{
		int start { 0 };        //!< raw story position, inclusive
		int end { 0 };          //!< exclusive; the PARSEP is not part of it
		QStringList styles;     //!< the paragraph's named style first, then its parents
		double fontSize { 0.0 }; //!< pt at the paragraph start, 0 = unknown
	};

	struct FrameRef
	{
		QString name;
		int page { -1 };        //!< 0-based page index, -1 = pasteboard
	};

	struct Story
	{
		int source { 0 };
		QString name;           //!< name of the first frame of the chain
		QList<FrameRef> frames;
		QString text;
		QList<Para> paras;
		bool captionLike { false };
		// filled by analyse()
		Range head { 0, 0 };
		Range body { 0, 0 };
		bool headByStyle { false };
		QString headNorm;
		QVector<int> headMap;   //!< normalised index -> raw story position
		QString bodyNorm;
		QVector<int> bodyMap;

		bool onPasteboard() const;
		int firstPage() const;
	};

	struct Source
	{
		QString label;
		QString filePath;
		bool live { false };    //!< stories were snapshotted from an open document
	};

	struct Options
	{
		int threshold { 85 };
		bool includePasteboard { false };
		bool includeCaptions { false };
		QSet<QByteArray> ignored; //!< fingerprint() of headlines/bodies the user said to ignore
	};

	enum class Kind { Headline, Content };

	struct Member
	{
		int story { -1 };
		int score { 0 };             //!< best match with another member, percent
		bool contained { false };    //!< its match is the shorter story inside a longer one
		bool headMatched { false };  //!< highlight the whole headline
		QList<Range> bodyNormRanges; //!< matching passages, in bodyNorm coordinates
	};

	struct Group
	{
		Kind kind { Kind::Content };
		int minScore { 0 };
		int maxScore { 0 };
		bool contained { false };
		QList<Member> members;
	};

	struct Progress
	{
		std::atomic<int> permille { 0 };
		std::atomic<bool> cancel { false };
	};

	struct Job
	{
		QList<Source> sources;
		QList<Story> stories;  //!< live stories, already snapshotted
		QList<int> slaSources; //!< sources read from disk on the worker
		Options options;
	};

	struct Result
	{
		QList<Source> sources;
		QList<Story> stories;
		QList<Group> groups;
		QStringList warnings;
		bool cancelled { false };
		qint64 elapsedMs { 0 };
		int storiesChecked { 0 };
	};

	QString normalise(const QString& text, int from, int to, QVector<int>* map);
	bool isHeadlineStyle(const QString& name);
	bool isCaptionStyle(const QString& name);
	bool isBylineStyle(const QString& name);
	void analyse(Story& story);
	bool readSla(const QString& path, int source, QList<Story>& out, QString* error);
	Result run(Job job, Progress* progress);
	QByteArray fingerprint(const QString& norm);
	QList<Range> toRaw(const QList<Range>& normRanges, const QVector<int>& map);
	QString firstWords(const Story& story, int maxChars = 60);
}

#endif
