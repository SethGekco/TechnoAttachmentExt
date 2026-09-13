#include "InstantSpawnRule.h"

#include <cstring>

#include <CCINIClass.h>
#include <AnimTypeClass.h>
#include <Utilities/Debug.h>

namespace
{
	// Trim leading spaces; the INI tokeniser leaves them after commas.
	char* TAExt_Trim(char* p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		return p;
	}

	// Comma list -> bitmask, via a name table. Returns false and names the bad
	// token if anything does not match, so a typo is loud rather than silent.
	bool TAExt_ReadFlagList(CCINIClass* pINI, const char* section, const char* key,
		const char* const* names, const int* values, int count, int& out)
	{
		char buffer[256];
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
			return false;

		int mask = 0;
		char* context = nullptr;
		for (char* tok = strtok_s(buffer, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
		{
			tok = TAExt_Trim(tok);
			bool matched = false;
			for (int i = 0; i < count; ++i)
			{
				if (_strcmpi(tok, names[i]) == 0)
				{
					mask |= values[i];
					matched = true;
					break;
				}
			}

			if (!matched)
				Debug::INIParseFailed(section, key, tok, "unrecognised value");
		}

		if (mask == 0)
			return false;

		out = mask;
		return true;
	}

	// Comma list of AnimTypes. A bad name is reported and skipped rather than
	// killing the rule: a missing puff of smoke must never cost the delivery.
	void TAExt_ReadAnimList(CCINIClass* pINI, const char* section, const char* key,
		std::vector<AnimTypeClass*>& out)
	{
		char buffer[256];
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
			return;

		out.clear();
		char* context = nullptr;
		for (char* tok = strtok_s(buffer, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
		{
			tok = TAExt_Trim(tok);
			if (auto const pAnim = AnimTypeClass::Find(tok))
				out.push_back(pAnim);
			else
				Debug::INIParseFailed(section, key, tok, "not an AnimType");
		}
	}

	const char* const TriggerNames[] = { "fire", "timer", "created", "destroyed" };
	const int TriggerValues[] = {
		TAExtSpawn_Fire, TAExtSpawn_Timer, TAExtSpawn_Created, TAExtSpawn_Destroyed };

	// Reasons that exist in the design but are NOT implemented yet. Listed so the
	// parser can say "not implemented" rather than "unrecognised" -- the modder
	// wrote something meaningful, it just does not work yet, and a trigger that
	// silently never fires is worse than one that is absent.
	const char* const UnimplementedReasons[] = {
		"crushed", "sunk", "crashed", "suicide", "erased", "warpfail", "expired",
		"sold", "absorbed", "abandoned", "deployed", "converted", "mutated",
		"limbo", "leftmap", "scenario", "cleanup" };
}

void TAExt_ReadInstantSpawnRules(CCINIClass* pINI, const char* section,
	std::vector<InstantSpawnRule>& out)
{
	// Longest key formatted below is "InstantSpawn.NoTarget[NN]"; 64 is ample.
	// (An undersized buffer here once caused a silent CRT abort at load, so keep
	// the headroom.)
	char key[64];
	char buffer[512];

	auto const readGroup = [&](const char* suffix) -> bool
	{
		_snprintf_s(key, sizeof(key), "InstantSpawn%s", suffix);
		if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
			return false; // no such group -- stop scanning indices

		InstantSpawnRule rule;

		// --- the payload list ---
		char* context = nullptr;
		for (char* tok = strtok_s(buffer, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
		{
			tok = TAExt_Trim(tok);
			if (auto const pType = TechnoTypeClass::Find(tok))
				rule.Types.push_back(pType);
			else
				Debug::INIParseFailed(section, key, tok, "not a TechnoType");
		}

		// A group whose list is entirely unparseable would silently do nothing
		// every activation; drop it but keep scanning further indices.
		if (rule.Types.empty())
		{
			Debug::INIParseFailed(section, key, "", "no valid TechnoTypes; rule ignored");
			return true;
		}

		auto const sub = [&](const char* name) -> const char*
		{
			_snprintf_s(key, sizeof(key), "InstantSpawn.%s%s", name, suffix);
			return key;
		};

		// --- trigger ---
		TAExt_ReadFlagList(pINI, section, sub("On"),
			TriggerNames, TriggerValues, 4, rule.Triggers);

		rule.OnWeapon = pINI->ReadInteger(section, sub("On.Weapon"), rule.OnWeapon);
		rule.OnRate = pINI->ReadInteger(section, sub("On.Rate"), rule.OnRate);

		if ((rule.Triggers & TAExtSpawn_Timer) && rule.OnRate <= 0)
		{
			Debug::INIParseFailed(section, sub("On.Rate"), "",
				"On=timer needs a positive On.Rate; timer trigger dropped");
			rule.Triggers &= ~TAExtSpawn_Timer;
		}

		// --- removal reason (only `combat` is implemented; see the header) ---
		{
			char reasons[256];
			if (pINI->ReadString(section, sub("On.Reason"), "", reasons, sizeof(reasons)) > 0)
			{
				int mask = 0;
				char* rctx = nullptr;
				for (char* tok = strtok_s(reasons, ",", &rctx); tok; tok = strtok_s(nullptr, ",", &rctx))
				{
					tok = TAExt_Trim(tok);
					if (_strcmpi(tok, "combat") == 0)
					{
						mask |= TAExtReason_Combat;
						continue;
					}

					bool known = false;
					for (auto const* name : UnimplementedReasons)
					{
						if (_strcmpi(tok, name) == 0) { known = true; break; }
					}

					Debug::INIParseFailed(section, sub("On.Reason"), tok, known
						? "a designed reason that is NOT implemented yet -- only `combat` works today"
						: "unrecognised reason");
				}

				if (mask != 0)
					rule.Reasons = mask;
			}
		}

		// --- location ---
		{
			char at[32];
			if (pINI->ReadString(section, sub("At"), "", at, sizeof(at)) > 0)
			{
				if (_strcmpi(at, "self") == 0)        rule.At = TAExtSpawnAt::Self;
				else if (_strcmpi(at, "target") == 0) rule.At = TAExtSpawnAt::Target;
				else Debug::INIParseFailed(section, sub("At"), at, "Expected self or target");
			}

			char noTarget[32];
			if (pINI->ReadString(section, sub("NoTarget"), "", noTarget, sizeof(noTarget)) > 0)
			{
				if (_strcmpi(noTarget, "self") == 0)      rule.NoTargetFallsBackToSelf = true;
				else if (_strcmpi(noTarget, "skip") == 0) rule.NoTargetFallsBackToSelf = false;
				else Debug::INIParseFailed(section, sub("NoTarget"), noTarget, "Expected skip or self");
			}
		}

		// --- count / placement ---
		rule.Count = pINI->ReadInteger(section, sub("Count"), rule.Count);
		rule.Range = pINI->ReadInteger(section, sub("Range"), rule.Range);

		{
			char blocked[32];
			if (pINI->ReadString(section, sub("OnBlocked"), "", blocked, sizeof(blocked)) > 0)
			{
				if (_strcmpi(blocked, "nearest") == 0)    rule.OnBlocked = TAExtSpawnBlocked::Nearest;
				else if (_strcmpi(blocked, "skip") == 0)  rule.OnBlocked = TAExtSpawnBlocked::Skip;
				else if (_strcmpi(blocked, "stack") == 0) rule.OnBlocked = TAExtSpawnBlocked::Stack;
				else Debug::INIParseFailed(section, sub("OnBlocked"), blocked,
					"Expected nearest, skip or stack");
			}
		}

		// --- the delivered object ---
		{
			char owner[32];
			if (pINI->ReadString(section, sub("Owner"), "", owner, sizeof(owner)) > 0)
			{
				if (_strcmpi(owner, "invoker") == 0)       rule.Owner = TAExtSpawnOwner::Invoker;
				else if (_strcmpi(owner, "civilian") == 0) rule.Owner = TAExtSpawnOwner::Civilian;
				else if (_strcmpi(owner, "special") == 0)  rule.Owner = TAExtSpawnOwner::Special;
				else if (_strcmpi(owner, "neutral") == 0)  rule.Owner = TAExtSpawnOwner::Neutral;
				else Debug::INIParseFailed(section, sub("Owner"), owner,
					"Expected Invoker, Civilian, Special or Neutral");
			}

			// Facing: a direction name, `random`, or a raw 0-255. Same value space
			// as FreeUnitExt's FreeUnit.Facing=.
			char facing[32];
			if (pINI->ReadString(section, sub("Facing"), "", facing, sizeof(facing)) > 0)
			{
				static const char* const dirs[] = { "N","NE","E","SE","S","SW","W","NW" };
				bool matched = false;

				if (_strcmpi(facing, "random") == 0)
				{
					rule.Facing = -2;
					matched = true;
				}
				else
				{
					for (int i = 0; i < 8 && !matched; ++i)
					{
						if (_strcmpi(facing, dirs[i]) == 0)
						{
							rule.Facing = i * 32; // 256 / 8
							matched = true;
						}
					}
				}

				if (!matched)
				{
					char* end = nullptr;
					long const raw = strtol(facing, &end, 10);
					if (end && end != facing && raw >= 0 && raw <= 255)
						rule.Facing = static_cast<int>(raw);
					else
						Debug::INIParseFailed(section, sub("Facing"), facing,
							"Expected N/NE/E/SE/S/SW/W/NW, random, or 0-255");
				}
			}

			rule.Mission = pINI->ReadInteger(section, sub("Mission"), rule.Mission);
		}

		// --- animations (H1b) ---
		TAExt_ReadAnimList(pINI, section, sub("Anim.Source"), rule.AnimSource);
		TAExt_ReadAnimList(pINI, section, sub("Anim.Dest"), rule.AnimDest);
		TAExt_ReadAnimList(pINI, section, sub("Anim.PerObject"), rule.AnimPerObject);
		TAExt_ReadAnimList(pINI, section, sub("Anim.Blocked"), rule.AnimBlocked);

		{
			char animOwner[32];
			if (pINI->ReadString(section, sub("Anim.Owner"), "", animOwner, sizeof(animOwner)) > 0)
			{
				if (_strcmpi(animOwner, "invoker") == 0)       rule.AnimOwner = TAExtSpawnOwner::Invoker;
				else if (_strcmpi(animOwner, "civilian") == 0) rule.AnimOwner = TAExtSpawnOwner::Civilian;
				else if (_strcmpi(animOwner, "special") == 0)  rule.AnimOwner = TAExtSpawnOwner::Special;
				else if (_strcmpi(animOwner, "neutral") == 0)  rule.AnimOwner = TAExtSpawnOwner::Neutral;
				else Debug::INIParseFailed(section, sub("Anim.Owner"), animOwner,
					"Expected Invoker, Civilian, Special or Neutral");
			}
		}

		rule.AnimRequireClear = pINI->ReadBool(section, sub("Anim.RequireClear"), rule.AnimRequireClear);

		// --- gates ---
		rule.Cooldown = pINI->ReadInteger(section, sub("Cooldown"), rule.Cooldown);
		rule.Chance = pINI->ReadInteger(section, sub("Chance"), rule.Chance);

		if (rule.Count > 0 && rule.Triggers != TAExtSpawn_None)
			out.emplace_back(std::move(rule));

		return true; // the key existed: keep scanning further indices
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
