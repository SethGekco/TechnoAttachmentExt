#pragma once

// H1a -- instant spawn. See docs/DESIGN-H1-InstantSpawn.md.
//
// One InstantSpawnRule is a trigger + a place + a payload. Rules come in groups:
// an unindexed group plus contiguous [0], [1], ... The unindexed group and [0]
// are SEPARATE rules, not aliases, matching the ExperienceTo precedent.
//
// Rules live on TechnoType and AttachmentType. Deliberately NOT per-slot: whole
// rule LISTS are AttachmentType-scoped in this codebase (as ExperienceTo and
// Convert already are); only single values get AttachmentN.* overrides.
//
// INI-derived and identical on every peer, so rule config is not serialized --
// only the per-instance cooldown is (see TechnoExt::InstantSpawnLastFired).

#include <vector>
#include <string>

#include <TechnoTypeClass.h>

class CCINIClass;

// Which event runs a rule. A bitmask so one rule can take several.
enum TAExtSpawnTrigger : int
{
	TAExtSpawn_None      = 0,
	TAExtSpawn_Fire      = 1 << 0, // the owner fired a weapon
	TAExtSpawn_Timer     = 1 << 1, // every On.Rate frames
	TAExtSpawn_Created   = 1 << 2, // the attachment child was created
	TAExtSpawn_Destroyed = 1 << 3, // the owner was removed (filtered by Reason)
};

// Why an object was removed. H1a ships ONLY Combat: the engine does not record a
// reason, so every other value needs its own verified discriminator and would
// otherwise be a trigger that silently never fires. The parser rejects the
// unimplemented names by name rather than ignoring them.
enum TAExtSpawnReason : int
{
	TAExtReason_None   = 0,
	TAExtReason_Combat = 1 << 0,
};

// Where the objects go.
enum class TAExtSpawnAt
{
	Self = 0,   // the owner's own cell
	Target = 1, // the owner's current target
};

// What to do when the chosen cell will not take the object.
enum class TAExtSpawnBlocked
{
	Nearest = 0, // search outward up to Range for a free cell  (default)
	Skip = 1,    // do not place this object
	Stack = 2,   // place anyway and accept the overlap
};

// Who owns what appears. Same value space as FreeUnitExt's FreeUnit.Owner=.
enum class TAExtSpawnOwner
{
	Invoker = 0,
	Civilian = 1,
	Special = 2,
	Neutral = 3,
};

struct InstantSpawnRule
{
	std::vector<TechnoTypeClass*> Types;

	int Triggers = TAExtSpawn_Fire;
	int Reasons = TAExtReason_Combat; // only consulted for TAExtSpawn_Destroyed
	int OnWeapon = -1;                // -1 = any weapon index
	int OnRate = 0;                   // frames, for TAExtSpawn_Timer

	TAExtSpawnAt At = TAExtSpawnAt::Self;
	bool NoTargetFallsBackToSelf = false; // At=target with no target: skip by default

	int Count = 1;
	TAExtSpawnBlocked OnBlocked = TAExtSpawnBlocked::Nearest;
	int Range = 1;

	TAExtSpawnOwner Owner = TAExtSpawnOwner::Invoker;
	int Facing = -1;  // -1 = inherit the owner's facing, -2 = random, else 0-255
	int Mission = -1; // -1 = the type's default

	int Cooldown = 0;
	int Chance = 100;
};

// Parse every InstantSpawn group in `section` and append to `out`.
// Shared by TechnoTypeExt and AttachmentTypeClass so the two cannot drift.
void TAExt_ReadInstantSpawnRules(CCINIClass* pINI, const char* section,
	std::vector<InstantSpawnRule>& out);
