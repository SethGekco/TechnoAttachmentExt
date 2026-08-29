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

// House-relation name parser for comma-separated lists (PoweredBy.House=).
// Free function for the same reason as TAExt_ParseRelation: Phobos's vector parser
// cannot handle an enum.
inline bool TAExt_ParseHouseRelation(const char* pValue, int& mask)
{
	if (_strcmpi(pValue, "owner") == 0 || _strcmpi(pValue, "self") == 0)
		mask |= TAExtHouse_Owner;
	else if (_strcmpi(pValue, "ally") == 0 || _strcmpi(pValue, "allies") == 0)
		mask |= TAExtHouse_Ally;
	else if (_strcmpi(pValue, "team") == 0)
		mask |= TAExtHouse_Team;
	else if (_strcmpi(pValue, "enemy") == 0 || _strcmpi(pValue, "enemies") == 0)
		mask |= TAExtHouse_Enemy;
	else if (_strcmpi(pValue, "neutral") == 0)
		mask |= TAExtHouse_Neutral;
	else if (_strcmpi(pValue, "civilian") == 0)
		mask |= TAExtHouse_Civilian;
	else if (_strcmpi(pValue, "special") == 0)
		mask |= TAExtHouse_Special;
	else if (_strcmpi(pValue, "any") == 0 || _strcmpi(pValue, "all") == 0)
		mask |= TAExtHouse_Any;
	else
		return false;
	return true;
}

// Parse a whole comma-separated relation list from one INI key. Returns false when
// the key is absent, so callers can tell "unset" from "set to something".
inline bool TAExt_ReadHouseRelationList(CCINIClass* pINI, const char* pSection,
	const char* pKey, int& maskOut)
{
	char buffer[128];
	if (pINI->ReadString(pSection, pKey, "", buffer, sizeof(buffer)) <= 0)
		return false;

	int mask = TAExtHouse_None;
	char* context = nullptr;
	for (char* tok = strtok_s(buffer, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
	{
		while (*tok == ' ')
			++tok;

		if (!TAExt_ParseHouseRelation(tok, mask))
			Debug::INIParseFailed(pSection, pKey, tok,
				"Expected houses (owner/ally/team/enemy/neutral/civilian/special/any)");
	}

	maskOut = mask;
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
