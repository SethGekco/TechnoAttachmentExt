#pragma once

#include <vector>
#include <string>

#include <Utilities/Enumerable.h>
#include <Utilities/Template.h>

#include <TechnoTypeClass.h>

class BuildingTypeClass;
class HouseTypeClass;

// NOTE (standalone port): the upstream PR added AttachmentYSortPosition to
// Phobos's <Utilities/Enum.h>. We are a standalone DLL and must not shadow
// Phobos's Enum.h (it lives on the include path and holds hundreds of enums
// we still need), so the new enum is declared here instead.
enum class AttachmentYSortPosition
{
	Default = 0,
	UnderParent = 1,
	OverParent = 2
};

// House-relation filter: which houses' buildings count for a PoweredBy check.
// A bitmask so a modder can combine them (e.g. "owner,ally").
enum TAExtHouseRelation : int
{
	TAExtHouse_None     = 0,
	TAExtHouse_Owner    = 1 << 0, // the host's own house
	TAExtHouse_Ally     = 1 << 1, // allied, excluding self
	TAExtHouse_Team     = 1 << 2, // same multiplayer team (TournamentTeamID)
	TAExtHouse_Enemy    = 1 << 3, // not allied and not a passive/neutral house
	TAExtHouse_Neutral  = 1 << 4, // the Neutral country
	TAExtHouse_Civilian = 1 << 5, // MultiplayPassive countries (civvies)
	TAExtHouse_Special  = 1 << 6, // the Special country
	TAExtHouse_Any      = 1 << 7, // anybody at all
};

// F0b relationship descriptor: how to address a techno relative to an
// attachment graph. Child/Sibling take a slot (index or ID); the others
// ignore it. Shared foundation for prerequisites, targeting, XP/power passing.
enum class AttachmentRelation
{
	Self,             // the techno itself
	Parent,           // its immediate attachment parent
	TopLevelParent,   // the root of its parent chain
	Child,            // one child slot (by index/ID)
	Sibling,          // one co-slot on the same parent (by index/ID), excluding self
	AllChildren,      // every child slot
	AllSiblings,      // every co-slot on the same parent, excluding self
};


class AttachmentTypeClass final : public Enumerable<AttachmentTypeClass>
{
public:
	Valueable<bool> RespawnAtCreation; // whether to spawn the attachment initially
	Valueable<int> RespawnDelay;
	Valueable<bool> InheritCommands;
	Valueable<bool> InheritCommands_StopCommand;
	Valueable<bool> InheritCommands_DeployCommand;
	Valueable<bool> InheritOwner; // aka mind control inheritance
	Valueable<bool> InheritStateEffects; // phasing out, stealth etc.
	Valueable<bool> InheritDestruction;
	Valueable<bool> InheritHeightStatus;
	// C1 (attachment power): while this attachment's child is active, it powers
	// the host. A host with >=1 PowersParent slot is a "power consumer" and goes
	// dark (Deactivated) whenever it has no active powering child. Kept separate
	// from vanilla PoweredUnit/PowersUnit (house/building/type-based) on purpose;
	// see docs/ROADMAP.md C. This is the first, attachment-scoped case of a more
	// general power-source -> power-consumer primitive (units-power-units, count
	// caps and radius scope are planned expansions).
	Valueable<bool> PowersParent;
	// C1 sibling powering. A "sibling" is another attachment slot on the same
	// parent. SOURCE side (on the powering attachment): while this attachment's
	// child is active it powers eligible sibling consumers --
	//   PowersSiblings=yes            -> powers all NON-picky sibling consumers
	//   PowersSiblings.Type=<types>   -> powers sibling consumers of these child types
	//   PowersSiblings.Index=<idx>    -> powers sibling consumers at these slot indices
	// When several are set they union (a sibling is powered if it matches any).
	Valueable<bool> PowersSiblings;
	ValueableVector<TechnoTypeClass*> PowersSiblings_Type;
	ValueableVector<int> PowersSiblings_Index;
	// CONSUMER side (on the attachment that can go dark). Powered=yes -> this
	// attachment child is Deactivated unless an eligible sibling source powers it.
	// Powered.Type restricts which source CHILD types satisfy it ("defines a type
	// that powers them"); when set, the vague PowersSiblings=yes no longer counts
	// and only a source that both targets it and is of an accepted type powers it.
	Valueable<bool> Powered;
	ValueableVector<TechnoTypeClass*> Powered_Type;
	// Reverse direction: this child is dark unless its PARENT is alive, on the
	// field and not itself dark. Chains naturally (a dark parent darkens the
	// subtree), so a powered-down host takes its decorative/functional bits with it.
	Valueable<bool> PoweredByParent;

	// External-structure power -- the vanilla [ROBO]PoweredUnit / [GAROBO]PowersUnit
	// relationship, expressed from the CONSUMER side so it can differ per slot.
	// The child is dark unless the host's owner has a working building from this
	// list. Unlike Prerequisite= (which HIDES the child), this only darkens it.
	//   PoweredBy=GAROBO,GATECH  -> buildings that power this attachment
	//   PoweredBy.RequireAll=no  -> no (default) = ANY of them; yes = ALL of them
	//   PoweredBy.RequirePower=yes -> the building must itself be online (default)
	//   PoweredBy.Range=0        -> 0 = house-wide (vanilla behaviour); >0 = the
	//                               building must be within N cells
	ValueableVector<BuildingTypeClass*> PoweredBy;
	Valueable<bool> PoweredBy_RequireAll;
	Valueable<bool> PoweredBy_RequirePower;
	Valueable<int> PoweredBy_Range;
	// Which houses' buildings count (TAExtHouseRelation bitmask). Default: owner
	// only, matching vanilla PowersUnit. e.g. PoweredBy.House=owner,ally
	Valueable<int> PoweredBy_House;

	// B1 -- requirement gating ("activate only if ..."). The child is dark unless
	// the parent satisfies these. Open-topped turret style:
	//   RequiresPassengers=N     -> parent must carry at least N passengers
	//   RequiresSlot.Index=<idx> -> those parent slots must hold an ACTIVE child
	//   RequiresSlot.Type=<types>-> parent must have an active child of these types
	// Index/Type union (any match satisfies); passengers is a separate AND gate.
	Valueable<int> RequiresPassengers;
	ValueableVector<int> RequiresSlot_Index;
	ValueableVector<TechnoTypeClass*> RequiresSlot_Type;

	// F1-lite -- "decorative" profile. Forces the behaviour bundle that makes a
	// regular TechnoType usable as a pure cosmetic/functional piece rather than a
	// standalone unit: click-through to the host, no mouse-solidity, no cell
	// occupation, low selection priority. Explicit tags still win where they
	// tighten it; this only turns the bundle ON.
	Valueable<bool> Decorative;

	// A2 -- experience passing. When THIS attachment's child earns veterancy, route
	// shares of the gain to relatives (F0b relations). Config comes in independent
	// GROUPS: one unindexed group plus contiguous [0], [1], ... groups. The
	// unindexed group and [0] are SEPARATE rules, not aliases, so both can be used
	// together:
	//
	//   ExperienceTo=parent            ; unindexed group
	//   ExperienceTo.Share=100
	//   ExperienceTo.Drain=no
	//   ExperienceTo[0]=children       ; a second, independent group
	//   ExperienceTo.Share[0]=50
	//   ExperienceTo.Drain[0]=yes
	//   ExperienceTo.Slot[0]=2         ; which slot singular child/sibling means
	//   ExperienceTo.ID[0]=SomeSlotID  ; ...or address that slot by its ID
	//
	// Relations: parent | root | child | sibling | children | siblings. ("self" is
	// accepted by the shared parser but is a no-op here -- an earner cannot pay
	// itself.) Singular child/sibling pick ONE slot, chosen by .Slot/.ID; the
	// plural forms take every slot.
	//
	// Detected by watching veterancy each synced tick, so no new game hook and
	// deterministic. Parsed by hand (Phobos's ValueableVector parser assumes an
	// AbstractType with a Find(), which an enum cannot provide). INI-derived config,
	// identical across peers -> not serialized, same as Prerequisite_Lists.
	struct ExperienceRule
	{
		std::vector<AttachmentRelation> To;
		int Share = 100;    // percent of the (multiplied) gain each recipient gets
		bool Drain = false; // yes = a move rather than a copy: earner loses what it gave
		int Slot = 0;       // slot for singular child/sibling
		std::string ID;     // ...or slot ID; non-empty wins over Slot
	};
	std::vector<ExperienceRule> ExperienceRules;

	// Convert-in-place. The slot keeps its index and its adoption by the parent
	// while the CHILD swaps to a different TechnoType -- for upgrades and for
	// damaged/wrecked variants. Same group syntax as ExperienceTo: one unindexed
	// group plus contiguous [0], [1], ... (unindexed and [0] are separate rules):
	//
	//   Convert=DAMAGEDHULL          ; become this type while the window holds
	//   Convert.MaxHealth=66
	//   Convert[0]=WRECKEDHULL
	//   Convert.MaxHealth[0]=33
	//
	// Rules are evaluated in order and the FIRST match wins, so list the most
	// specific (lowest health / highest rank) last-to-first as needed. When NO rule
	// matches, the slot reverts to its configured TechnoType -- reverting is
	// inherent, there is no separate revert flag.
	//
	// Conditions read the HOST's health percentage / rank, matching
	// Prerequisite.MinHealth and Prerequisite.MinRank.
	struct ConvertRule
	{
		TechnoTypeClass* To = nullptr;
		int MinHealth = -1; // -1 = unset. Percentages, 0-100.
		int MaxHealth = -1;
		int MinRank = -1;   // -1 = unset. Rank as int (rookie 0 / veteran 1 / elite 2).
		int MaxRank = -1;
	};
	std::vector<ConvertRule> ConvertRules;
	// Carry state across a conversion. Health is carried as a PERCENTAGE, so the
	// replacement keeps the same damage ratio even with a different Strength.
	Valueable<bool> Convert_KeepHealth;
	Valueable<bool> Convert_KeepVeterancy;

	// "Intangible": keep the child OUT of the cell's content list entirely
	// (CellClass::AddContent/RemoveContent), so nothing that walks cell contents --
	// placement checks, occupier lookups, cursor picking -- can see it. This is the
	// absolute "blocks nothing" guarantee, stronger than OccupiesCell=no (which only
	// clears the occupation FLAG).
	// EXPERIMENTAL: if the renderer also sources objects from cell contents, an
	// intangible child may additionally become invisible. That is either a bug or
	// exactly the requested invisibility feature depending on intent -- see
	// docs/TESTING.md 1c, which resolves it with one test. Off by default.
	// G1 -- ammo capacity. While this attachment's child is active, the HOST's
	// maximum ammo is raised by this much (bonuses from several slots sum).
	// This is real CAPACITY, not a one-off top-up: the host reloads up to
	// base + bonus and only counts as "full" there.
	// Note: TechnoTypeClass::Ammo is shared by every unit of a type, so the
	// capacity cannot simply be written -- it is substituted at the reload
	// check instead. See Hooks.AttachedAmmo.cpp.
	Valueable<int> Ammo_Parent;
	Valueable<bool> Intangible;
	Valueable<bool> OccupiesCell;
	Valueable<bool> LowSelectionPriority;
	Valueable<bool> PassSelection;
	Valueable<bool> TransparentToMouse;
	Valueable<AttachmentYSortPosition> YSortPosition;
	Nullable<WeaponTypeClass*> DestructionWeapon_Child;
	Nullable<WeaponTypeClass*> DestructionWeapon_Parent;
	Nullable<Mission> ParentDestructionMission;
	Nullable<Mission> ParentDetachmentMission;
	// Standalone extension: the child is only (re)spawned while the host's owner
	// house has ALL of these buildings present. Empty = no prerequisite.
	ValueableVector<BuildingTypeClass*> Prerequisite;
	// Negative prerequisite: the child is blocked while ANY of these buildings
	// is present (opposite of Prerequisite).
	ValueableVector<BuildingTypeClass*> Prerequisite_Negative;
	// Owner-country gating. RequiredHouses (if non-empty): host owner's country
	// must be listed. ForbiddenHouses: host owner's country must NOT be listed.
	ValueableVector<HouseTypeClass*> RequiredHouses;
	ValueableVector<HouseTypeClass*> ForbiddenHouses;
	// A1-lite: veterancy gating. The child is only present while the HOST's rank is
	// within [MinRank, MaxRank]. Combined with Prerequisite.Dynamic (default yes)
	// this gives veterancy-driven attach/detach -- an attachment that appears on
	// promotion to veteran/elite and disappears on de-vet -- without a separate
	// attach/detach mechanism.
	Nullable<Rank> Prerequisite_MinRank;
	Nullable<Rank> Prerequisite_MaxRank;
	// Damaged/destroyed-variant gating: the child is only present while the host's
	// health percentage is within [MinHealth, MaxHealth] (0-100). Lets one host show
	// a pristine piece at high HP and a wrecked one at low HP.
	Nullable<int> Prerequisite_MinHealth;
	Nullable<int> Prerequisite_MaxHealth;

	// yes = child hides/shows live as the prerequisite is gained/lost.
	// no  = prerequisite is only checked at spawn (static gate; no live toggle).
	Valueable<bool> Prerequisite_Dynamic;

	// Sibling prerequisites (E1). A "sibling" is another attachment slot on the
	// same parent. Singular "Sibling" = ANY listed one satisfies (OR); plural
	// "Siblings" = ALL listed must be satisfied (AND). ".Index" keys on the
	// sibling slot index, ".Type" on the sibling child's TechnoType. Inherently
	// runtime, so meaningful only with Prerequisite.Dynamic (the default).
	ValueableVector<int> Prerequisite_Sibling_Index;
	ValueableVector<TechnoTypeClass*> Prerequisite_Sibling_Type;
	ValueableVector<int> Prerequisite_Siblings_Index;
	ValueableVector<TechnoTypeClass*> Prerequisite_Siblings_Type;

	// E1b: alternative OR building lists Prerequisite[0], Prerequisite[1], ...
	// (Ares-style). The building requirement is met if the primary Prerequisite
	// OR any of these lists is fully present; each list is AND-within. Negative/
	// Houses/Sibling gates stay global (always AND). INI-derived config, not
	// serialized (type globals aren't hooked to save/load and are identical
	// across peers -> online-safe).
	std::vector<ValueableVector<BuildingTypeClass*>> Prerequisite_Lists;

	AttachmentTypeClass(const char* pTitle = NONE_STR) : Enumerable<AttachmentTypeClass>(pTitle)
		, RespawnAtCreation { true }
		, RespawnDelay { -1 }
		, InheritCommands { true }
		, InheritCommands_StopCommand { true }
		, InheritCommands_DeployCommand { true }
		, InheritOwner { true }
		, InheritStateEffects { true }
		, InheritDestruction { true }
		, InheritHeightStatus { true }
		, PowersParent { false }
		, PowersSiblings { false }
		, PowersSiblings_Type { }
		, PowersSiblings_Index { }
		, Powered { false }
		, Powered_Type { }
		, PoweredByParent { false }
		, PoweredBy { }
		, PoweredBy_RequireAll { false }
		, PoweredBy_RequirePower { true }
		, PoweredBy_Range { 0 }
		, PoweredBy_House { TAExtHouse_Owner }
		, RequiresPassengers { 0 }
		, RequiresSlot_Index { }
		, RequiresSlot_Type { }
		, Decorative { false }
		, ExperienceRules { }
		, ConvertRules { }
		, Convert_KeepHealth { true }
		, Convert_KeepVeterancy { true }
		, Ammo_Parent { 0 }
		, Intangible { false }
		, OccupiesCell { true }
		, LowSelectionPriority { true }
		, PassSelection { false }
		, TransparentToMouse { false }
		, YSortPosition { AttachmentYSortPosition::Default }
		, DestructionWeapon_Child { }
		, DestructionWeapon_Parent { }
		, ParentDestructionMission { }
		, ParentDetachmentMission { }
		, Prerequisite { }
		, Prerequisite_Negative { }
		, RequiredHouses { }
		, ForbiddenHouses { }
		, Prerequisite_MinRank { }
		, Prerequisite_MaxRank { }
		, Prerequisite_MinHealth { }
		, Prerequisite_MaxHealth { }
		, Prerequisite_Dynamic { true }
		, Prerequisite_Sibling_Index { }
		, Prerequisite_Sibling_Type { }
		, Prerequisite_Siblings_Index { }
		, Prerequisite_Siblings_Type { }
		, Prerequisite_Lists { }
	{ }

	virtual ~AttachmentTypeClass() = default;

	virtual void LoadFromINI(CCINIClass* pINI);
	virtual void LoadFromStream(PhobosStreamReader& Stm);
	virtual void SaveToStream(PhobosStreamWriter& Stm);

private:
	template <typename T>
	void Serialize(T& Stm);
};
