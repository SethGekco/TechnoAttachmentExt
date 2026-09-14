#include "GunnerProfileRule.h"

#include <cstring>

#include <CCINIClass.h>
#include <Utilities/Debug.h>

#include <Ext/TechnoType/Body.h>

namespace
{
	char* TrimLeft(char* p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		return p;
	}
}

void TAExt_ReadGunnerProfileRules(CCINIClass* pINI, const char* section,
	std::vector<GunnerProfileRule>& out)
{
	// Longest key formatted below is "GunnerProfile.KeepVeterancy[NN]".
	char key[64];
	char buffer[256];

	auto const readGroup = [&](const char* suffix) -> bool
	{
		_snprintf_s(key, sizeof(key), "GunnerProfile%s", suffix);
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
			return false; // no such group -- stop scanning indices

		GunnerProfileRule rule;

		char* const target = TrimLeft(buffer);
		rule.To = TechnoTypeClass::Find(target);

		if (!rule.To)
		{
			// A group naming a type that does not exist would silently never fire.
			Debug::INIParseFailed(section, key, target, "not a TechnoType; rule ignored");
			return true; // the key existed, so keep scanning further indices
		}

		auto const sub = [&](const char* name) -> const char*
		{
			_snprintf_s(key, sizeof(key), "GunnerProfile.%s%s", name, suffix);
			return key;
		};

		{
			char passengers[256];
			if (pINI->ReadString(section, sub("Passenger"), "", passengers, sizeof(passengers)) > 0)
			{
				char* context = nullptr;
				for (char* tok = strtok_s(passengers, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
				{
					tok = TrimLeft(tok);
					if (auto const pType = TechnoTypeClass::Find(tok))
						rule.Passenger.push_back(pType);
					else
						Debug::INIParseFailed(section, sub("Passenger"), tok, "not a TechnoType");
				}
			}
		}

		rule.Index = pINI->ReadInteger(section, sub("Index"), rule.Index);
		rule.KeepHealth = pINI->ReadBool(section, sub("KeepHealth"), rule.KeepHealth);
		rule.KeepVeterancy = pINI->ReadBool(section, sub("KeepVeterancy"), rule.KeepVeterancy);
		rule.MinDwell = pINI->ReadInteger(section, sub("MinDwell"), rule.MinDwell);

		if (rule.MinDwell < 0)
			rule.MinDwell = 0;

		// A rule with neither a passenger type nor an index would match the moment
		// anything at all boards, which is almost certainly not what was meant --
		// but it IS expressible ("any passenger converts me"), so allow it and say
		// so rather than guessing.
		if (rule.Passenger.empty() && rule.Index < 0)
		{
			Debug::Log("[TAExt] [%s] %s has neither .Passenger nor .Index, so ANY "
				"passenger will trigger it.\n", section, key);
		}

		out.emplace_back(std::move(rule));
		return true;
	};

	readGroup(""); // the unindexed group

	for (int i = 0; ; ++i)
	{
		char suffix[16];
		_snprintf_s(suffix, sizeof(suffix), "[%d]", i);
		if (!readGroup(suffix))
			break;
	}
}

// ---------------------------------------------------------------------------
// I-c -- cross-type validation, run ONCE after all type data is loaded.
//
// It cannot live in the per-type parser: a profile named in [IFV] may belong to
// a section that has not been read yet, so its capacity and its own rule list
// are not knowable at that point. Ordering makes per-type validation
// unreliable in a way that would show up only in some mods.
//
// Every problem below is knowable at load and produces a confusing symptom if
// left to runtime, so each one REMOVES the offending rule and says why. A rule
// that silently misbehaves in game is worse than one that refused to load.
// ---------------------------------------------------------------------------
void TAExt_ValidateGunnerProfiles()
{
	int checked = 0;
	int dropped = 0;

	for (int i = 0; i < TechnoTypeClass::Array.Count; ++i)
	{
		auto const pHost = TechnoTypeClass::Array.GetItem(i);
		if (!pHost)
			continue;

		auto const pHostExt = TechnoTypeExt::ExtMap.Find(pHost);
		if (!pHostExt || pHostExt->GunnerProfiles.empty())
			continue;

		auto& rules = pHostExt->GunnerProfiles;

		// A building host can never convert -- UpdateType rejects it -- so its
		// rules would be silently inert. Say so once and drop the lot.
		if (pHost->WhatAmI() == AbstractType::BuildingType)
		{
			Debug::Log("[TAExt] [%s] GunnerProfile is not supported on buildings "
				"(foundation, occupancy and power are fixed at placement). "
				"%d rule(s) dropped.\n", pHost->ID, static_cast<int>(rules.size()));
			dropped += static_cast<int>(rules.size());
			rules.clear();
			continue;
		}

		for (size_t r = rules.size(); r-- > 0; )
		{
			auto const& rule = rules[r];
			auto const pTo = rule.To;
			const char* reason = nullptr;

			if (!pTo)
			{
				reason = "profile type is null";
			}
			else if (pTo == pHost)
			{
				reason = "profile is the host itself, so it could never change anything";
			}
			else if (pTo->WhatAmI() != pHost->WhatAmI())
			{
				// UpdateType would refuse this at runtime; refuse it now instead.
				reason = "profile is a different kind of type (a vehicle cannot "
					"become infantry)";
			}
			else if (pTo->Passengers < pHost->Passengers)
			{
				// The engine does not evict anyone on a type swap, so a full
				// transport would simply be carrying more than its type allows.
				reason = "profile has FEWER Passengers= than the host, which would "
					"leave a full transport overfull after converting";
			}
			else if (pTo->SizeLimit < pHost->SizeLimit)
			{
				reason = "profile has a smaller SizeLimit= than the host, so a "
					"passenger legal before the swap would be illegal after it";
			}
			else
			{
				// Cycle: converting into another host that would immediately want
				// to convert again. Only checkable here, once every type is parsed.
				auto const pToExt = TechnoTypeExt::ExtMap.Find(pTo);
				if (pToExt && !pToExt->GunnerProfiles.empty())
				{
					reason = "profile is itself a GunnerProfile host, which would "
						"chain conversions";
				}
			}

			if (reason)
			{
				Debug::Log("[TAExt] [%s] GunnerProfile -> %s rejected: %s.\n",
					pHost->ID, pTo ? pTo->ID : "<null>", reason);
				rules.erase(rules.begin() + r);
				++dropped;
			}
			else
			{
				++checked;
			}
		}
	}

	// Unconditional when anything was configured at all. This doubles as the
	// liveness proof for the 0x679CAF seat: three frameworks already chain there,
	// and "legal to chain" is not the same as "actually ran".
	if (checked || dropped)
	{
		Debug::Log("[TAExt] GunnerProfile validation: %d rule(s) accepted, %d rejected.\n",
			checked, dropped);
	}
}
