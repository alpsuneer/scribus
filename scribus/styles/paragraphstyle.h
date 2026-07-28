/*
 For general Scribus (>=1.3.2) copyright and licensing information please refer
 to the COPYING file provided with the program. Following this notice may exist
 a copyright and/or license notice that predates the release of Scribus 1.3.2
 for which a new license (GPL+exception) is in place.
 */
/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#ifndef PARAGRAPHSTYLE_H
#define PARAGRAPHSTYLE_H

#include "style.h"
#include "charstyle.h"
#include "opticalmarginlookup.h"
#include "styles/stylecontextproxy.h"


class ResourceCollection;


class SCRIBUS_API ParagraphStyle : public BaseStyle
{
public:
	enum LineSpacingMode
	{
		FixedLineSpacing        = 0, 
		AutomaticLineSpacing    = 1,
		BaselineGridLineSpacing = 2
	};

	enum AlignmentType
	{
		LeftAligned  = 0,
		Centered     = 1,
		RightAligned = 2,
		Justified    = 3,
		Extended     = 4
	};

	enum DirectionType
	{
		LTR  = 0,
		RTL  = 1
	};

	enum OpticalMarginType
	{
		OM_None  = 0,
		OM_LeftProtruding    = 1,
		OM_RightProtruding   = 2,
		OM_LeftHangingPunct  = 4,
		OM_RightHangingPunct = 8,
		OM_Default           = OM_RightProtruding + OM_LeftHangingPunct + OM_RightHangingPunct
	};

	enum HyphenationMode
	{
		NoHyphenation        = 0,
		ManualHyphenation    = 1,
		AutomaticHyphenation = 2
	};

	enum TabType
	{
		LeftTab = 0,
		RightTab = 1,
		PeriodTab = 2,
		CommaTab = 3,
		CenterTab = 4
	};

	/** Line style of a paragraph rule (rule above / rule below) */
	enum RuleType
	{
		RuleSolid     = 0,
		RuleDashed    = 1,
		RuleDotted    = 2,
		RuleDouble    = 3,
		RuleThickThin = 4,
		RuleThinThick = 5
	};

	/** Horizontal extent of a paragraph rule */
	enum RuleWidthType
	{
		RuleWidthColumn = 0,
		RuleWidthText   = 1
	};

	/** Sentinel colour name for paragraph rules: the rule inherits the fill
	    colour of the first character of the paragraph. Stored verbatim so it
	    survives colour palette changes. */
	static const QString RuleTextColor;

	/** How far a nested style rule reaches relative to its delimiter */
	enum NestedStyleMode
	{
		NestedUpTo   = 0,  //!< the delimiter itself is *not* styled
		NestedThrough = 1  //!< the delimiter itself *is* styled
	};

	/** What a nested style rule scans for. Anything other than NestedDelimChar
	    is a named preset that has no single obvious literal spelling. */
	enum NestedDelimiterType
	{
		NestedDelimChar         = 0, //!< the literal codepoint in NestedStyleRule::delimiter
		NestedDelimEndOfPara    = 1,
		NestedDelimSpace        = 2,
		NestedDelimTab          = 3,
		NestedDelimEnSpace      = 4,
		NestedDelimEmSpace      = 5
	};

	/** One InDesign-style nested style rule. Rules consume the paragraph in
	    order: rule N starts where rule N-1 stopped and runs up to / through the
	    countth occurrence of its delimiter. */
	struct SCRIBUS_API NestedStyleRule
	{
		QString charStyleName;
		NestedStyleMode mode { NestedThrough };
		int count { 1 };
		NestedDelimiterType delimiterType { NestedDelimChar };
		char32_t delimiter { 0 };

		bool operator==(const NestedStyleRule& other) const;
		bool operator!=(const NestedStyleRule& other) const { return !(*this == other); }

		/** The codepoint this rule actually scans for, presets resolved.
		    Returns 0 for NestedDelimEndOfPara, which has no character. */
		char32_t effectiveDelimiter() const;
	};

	/** Maximum number of nested style rules kept on one paragraph style. */
	static const int MaxNestedStyleRules = 4;

	/** Parse / build the encoded nestedStyles() attribute. The encoding is
	    rules joined by ';', fields by ',':
	        <percent-encoded char style name>,<U|T>,<count>,<delim spec>
	    where <delim spec> is EOP, SP, TAB, ENSP, EMSP or Uxxxx (hex codepoint).
	    Storing the delimiter as a codepoint keeps non-Latin delimiters and
	    delimiters that collide with the separators themselves safe. */
	static QList<NestedStyleRule> parseNestedStyles(const QString& encoded);
	static QString encodeNestedStyles(const QList<NestedStyleRule>& rules);

	/** Convenience wrappers around the encoded nestedStyles() attribute. */
	QList<NestedStyleRule> nestedStyleRules() const { return parseNestedStyles(nestedStyles()); }
	void setNestedStyleRules(const QList<NestedStyleRule>& rules) { setNestedStyles(encodeNestedStyles(rules)); }
	bool hasNestedStyles() const { return !nestedStyles().isEmpty(); }

	struct TabRecord
	{
		qreal tabPosition {0.0};
		int tabType {0};
		QChar tabFillChar;
		bool operator==(const TabRecord& other) const;
		bool operator<(const TabRecord& other)  const { return tabPosition < other.tabPosition; }
		bool operator<=(const TabRecord& other) const { return tabPosition <= other.tabPosition; }
		bool operator>(const TabRecord& other)  const { return tabPosition > other.tabPosition; }
		bool operator>=(const TabRecord& other) const { return tabPosition >= other.tabPosition; }
	};

	ParagraphStyle();
	ParagraphStyle(const ParagraphStyle& other);
	ParagraphStyle& operator=(const ParagraphStyle& other);
	~ParagraphStyle() = default;

	static const Xml_string saxxDefaultElem;
	static void  desaxeRules(const Xml_string& prefixPattern, desaxe::Digester& ruleset, const Xml_string& elemtag = saxxDefaultElem);
	
	void saxx(SaxHandler& handler, const Xml_string& elemtag) const override;
	void saxx(SaxHandler& handler) const override { saxx(handler, saxxDefaultElem); }

	void getNamedResources(ResourceCollection& lists) const;
	void replaceNamedResources(ResourceCollection& newNames);

	QString displayName() const override;

	void setContext(const StyleContext* context) override;
	void update(const StyleContext*) override;
	
	bool equiv(const BaseStyle& other) const override;
	
	void applyStyle(const ParagraphStyle& other);
	void eraseStyle(const ParagraphStyle& other);
	void setStyle(const ParagraphStyle& other);
	void erase() override { eraseStyle(*this); }

	StyleContext* charStyleContext() { return & m_cstyleContext; }
	const StyleContext* charStyleContext() const { return & m_cstyleContext; }
	CharStyle & charStyle() { return m_cstyle; }
	const CharStyle& charStyle() const { return m_cstyle; }
	/** Normally the context for charStyle() is parentStyle()->charStyleContext()
		Use this method to break that relation and set charStyle()'s context manually
	*/
	void breakImplicitCharStyleInheritance(bool val = true);
	/** used internally to establish the implicit relation 
		charStyle().context() == parentStyle()->charStyleContext()
		Done implicitly in setContext(), update(), operator= and breakImplicitCharStyleInheritance()
	*/
	void repairImplicitCharStyleInheritance();
	
	/** getter: validates and returns the attribute's value */
	
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	const attr_TYPE &attr_GETTER() const { validate(); return m_##attr_NAME; }
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	
	/** setter: sets the attribute's value and clears inherited flag */
	
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	void set##attr_NAME(attr_TYPE v) { m_##attr_NAME = v; inh_##attr_NAME = false; }
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	void appendTabValue(const TabRecord& tab) { validate(); m_TabValues.append(tab); inh_TabValues = false; }
	
	
	/** setter: resets the attribute's value and sets inherited flag */
	
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	void reset##attr_NAME() { m_##attr_NAME = attr_DEFAULT; inh_##attr_NAME = true; }
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	
	/** isInherited: returns true if the attribute is inherited */
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	bool isInh##attr_NAME() const { return inh_##attr_NAME; }
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	
	
	/** isDefined: returns true if the attribute is defined in this style or any parent */
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
	bool isDef##attr_NAME() const { \
		if ( !inh_##attr_NAME ) return true; \
		const ParagraphStyle * par = dynamic_cast<const ParagraphStyle*>(parentStyle()); \
		return par && par->isDef##attr_NAME(); \
	}
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
	
	
private:
		
	// member declarations:
	StyleContextProxy m_cstyleContext { nullptr };
	bool m_cstyleContextIsInh { true };
	CharStyle m_cstyle;
	
#define ATTRDEF(attr_TYPE, attr_GETTER, attr_NAME, attr_DEFAULT) \
		attr_TYPE m_##attr_NAME; \
			bool inh_##attr_NAME;
#include "paragraphstyle.attrdefs.cxx"
#undef ATTRDEF
		
};

#endif
