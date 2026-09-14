#pragma once

// I-b -- gunner host profiles. See docs/DESIGN-I-GunnerProfiles.md.
//
// While a matching passenger rides, the HOST becomes a different TechnoType, so
// Strength / Speed / armor / weapons all change at once. The swap itself is
// TechnoExt::UpdateType (I-a); this is the trigger that drives it.
//
// Rule groups follow the Convert / InstantSpawn grammar: an unindexed group plus
// contiguous [0], [1], ... The unindexed group and [0] are SEPARATE rules.
// First match wins; NO match reverts to the base type, so reverting is inherent
// and needs no separate flag.
//
// Rules live on the HOST's TechnoType. INI-derived and identical on every peer,
// so the rule list is not serialized -- but the base type and the last-change
// frame are (see TechnoExt::GunnerBaseType).

#include <vector>

#include <TechnoTypeClass.h>

class CCINIClass;

struct GunnerProfileRule
{
	TechnoTypeClass* To = nullptr;
	std::vector<TechnoTypeClass*> Passenger; // empty = any passenger satisfies
	int Index = -1;                          // 0-based cargo position; -1 = any

	bool KeepHealth = true;
	bool KeepVeterancy = true;

	// Frames that must pass after ANY change before this host may change again.
	//
	// This is the oscillation brake, not a cosmetic delay. The failure mode it
	// exists for: convert -> the gunner is somehow displaced by the swap ->
	// condition false -> revert -> he re-registers -> convert, every frame,
	// forever. That costs a frame each time and presents as a freeze rather than
	// a crash, which is far harder to diagnose. A non-zero default means a mod
	// cannot hit it by accident.
	int MinDwell = 15;
};

// Parse every GunnerProfile group in `section` and append to `out`.
void TAExt_ReadGunnerProfileRules(CCINIClass* pINI, const char* section,
	std::vector<GunnerProfileRule>& out);

// I-c: cross-type validation, once after ALL type data is loaded. Cannot be
// done per-type: a named profile may belong to a section not yet parsed.
void TAExt_ValidateGunnerProfiles();
