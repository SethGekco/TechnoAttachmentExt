#pragma once

// Standalone port shim.
//
// The upstream PR added an INI parser for AttachmentYSortPosition directly
// inside Phobos's <Utilities/TemplateDef.h>. We cannot edit that submodule
// header, so we provide the same detail::read<> specialization here. Any
// translation unit that reads a Valueable<AttachmentYSortPosition> from INI
// must include THIS header (not <Utilities/TemplateDef.h> directly) so the
// explicit specialization is visible before the point of instantiation.

#include <Utilities/TemplateDef.h>
#include <New/Type/AttachmentTypeClass.h>

// Relation-name parser for comma-separated relation lists (e.g. ExperienceTo=).
// A free function rather than a detail::read<> specialization because Phobos's
// ValueableVector parser routes through Parser<T>, whose non-specialized form
// requires an AbstractType with a static Find() -- an enum cannot provide one.
inline bool TAExt_ParseRelation(const char* pValue, AttachmentRelation& value)
{
	if (_strcmpi(pValue, "self") == 0)                  value = AttachmentRelation::Self;
	else if (_strcmpi(pValue, "parent") == 0)           value = AttachmentRelation::Parent;
	else if (_strcmpi(pValue, "toplevelparent") == 0
		|| _strcmpi(pValue, "root") == 0)               value = AttachmentRelation::TopLevelParent;
	else if (_strcmpi(pValue, "child") == 0)            value = AttachmentRelation::Child;
	else if (_strcmpi(pValue, "sibling") == 0)          value = AttachmentRelation::Sibling;
	else if (_strcmpi(pValue, "children") == 0
		|| _strcmpi(pValue, "allchildren") == 0)        value = AttachmentRelation::AllChildren;
	else if (_strcmpi(pValue, "siblings") == 0
		|| _strcmpi(pValue, "allsiblings") == 0)        value = AttachmentRelation::AllSiblings;
	else return false;
	return true;
}

namespace detail
{
	// Rank (rookie/veteran/elite) has no parser in Phobos's TemplateDef either, so
	// veterancy gates get the same treatment. Accepts the INI-friendly names.
	template <>
	inline bool read<Rank>(Rank& value, INI_EX& parser, const char* pSection, const char* pKey)
	{
		if (parser.ReadString(pSection, pKey))
		{
			if (_strcmpi(parser.value(), "rookie") == 0 || _strcmpi(parser.value(), "green") == 0)
				value = Rank::Rookie;
			else if (_strcmpi(parser.value(), "veteran") == 0)
				value = Rank::Veteran;
			else if (_strcmpi(parser.value(), "elite") == 0)
				value = Rank::Elite;
			else
			{
				Debug::INIParseFailed(pSection, pKey, parser.value(), "Expected a rank (rookie/veteran/elite)");
				return false;
			}
			return true;
		}
		return false;
	}

	template <>
	inline bool read<AttachmentYSortPosition>(AttachmentYSortPosition& value, INI_EX& parser, const char* pSection, const char* pKey)
	{
		if (parser.ReadString(pSection, pKey))
		{
			if (_strcmpi(parser.value(), "default") == 0)
			{
				value = AttachmentYSortPosition::Default;
			}
			else if (_strcmpi(parser.value(), "underparent") == 0)
			{
				value = AttachmentYSortPosition::UnderParent;
			}
			else if (_strcmpi(parser.value(), "overparent") == 0)
			{
				value = AttachmentYSortPosition::OverParent;
			}
			else
			{
				Debug::INIParseFailed(pSection, pKey, parser.value(), "Expected an attachment YSort position");
				return false;
			}
			return true;
		}
		return false;
	}
}
