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

class AnimTypeClass;

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

// How the payload list is consumed.
enum class TAExtSpawnMode
{
	All = 0,      // the whole list, Count times over  (default)
	Random = 1,   // one at random per object, synced RNG
	Weighted = 2, // as Random, biased by Weights
	Cycle = 3,    // step through the list across activations (needs saved state)
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

	// H1c -- how many. Base plus scaling terms that ride the existing
	// foundations: active attachment slots (F0), current ammo (G1), veterancy.
	int Count = 1;
	int CountPerSlot = 0;   // + N per ACTIVE attachment slot on the owner
	std::vector<TechnoTypeClass*> CountPerSlotType; // empty = any child type counts
	int CountPerAmmo = 0;   // + N per round of current ammo
	int CountPerRank = 0;   // + N per veterancy rank (rookie 0 / vet 1 / elite 2)
	int CountMax = 0;       // 0 = uncapped; else clamp the computed total

	// H1c -- which payload. `all` places the whole list Count times over; the
	// others place ONE type per object.
	TAExtSpawnMode Mode = TAExtSpawnMode::All;
	std::vector<int> Weights; // for Mode=weighted; short lists pad with 1

	TAExtSpawnBlocked OnBlocked = TAExtSpawnBlocked::Nearest;
	int Range = 1;

	TAExtSpawnOwner Owner = TAExtSpawnOwner::Invoker;
	int Facing = -1;  // -1 = inherit the owner's facing, -2 = random, else 0-255
	int Mission = -1; // -1 = the type's default

	int Cooldown = 0;
	int Chance = 100;

	// H1b -- animations. Four seats, each a list from which ONE is picked per
	// activation with synced RNG.
	//
	// These are NOT decoration-only: an AnimType with MakeInfantry= creates a real
	// unit, so an AnimClass is synced game state. That is why the pick uses
	// ScenarioClass::Random and why creation must never be conditional on anything
	// client-side -- both peers must create the same anim in the same order.
	std::vector<AnimTypeClass*> AnimSource;    // on the spawner
	std::vector<AnimTypeClass*> AnimDest;      // at the resolved location, once
	std::vector<AnimTypeClass*> AnimPerObject; // at each placed object's cell
	std::vector<AnimTypeClass*> AnimBlocked;   // when a placement fails

	// H1d -- attach the new object to the spawner instead of placing it loose.
	//
	// Slots are NOT created at runtime. AttachmentDataEntry lives in the TYPE's
	// AttachmentData vector, shared by every unit of that type, so appending one
	// would grow every unit's slot list at once -- and AttachmentClass holds a raw
	// pointer into that vector, so per-instance entries would be a lifetime
	// problem. Attach therefore FILLS AN EXISTING DECLARED SLOT that is currently
	// empty. The modder declares AttachmentN.Type= slots as capacity and this fills
	// them, exactly the "ceiling in INI, runtime fills it" shape that SpawnsNumber
	// and Spawns.Base already use.
	bool Attach = false;
	int AttachSlot = -1;      // -1 = the first empty slot; else that slot index
	bool AttachReplace = false; // slot occupied: no = skip, yes = destroy and replace

	TAExtSpawnOwner AnimOwner = TAExtSpawnOwner::Invoker;
	bool AnimRequireClear = false; // only play where the cell is clear
};

// Parse every InstantSpawn group in `section` and append to `out`.
// Shared by TechnoTypeExt and AttachmentTypeClass so the two cannot drift.
void TAExt_ReadInstantSpawnRules(CCINIClass* pINI, const char* section,
	std::vector<InstantSpawnRule>& out);
