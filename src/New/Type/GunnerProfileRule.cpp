#include "GunnerProfileRule.h"

#include <cstring>

#include <CCINIClass.h>
#include <Utilities/Debug.h>

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
