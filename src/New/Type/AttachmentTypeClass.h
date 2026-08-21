#pragma once

#include <vector>

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
		, RequiresPassengers { 0 }
		, RequiresSlot_Index { }
		, RequiresSlot_Type { }
		, Decorative { false }
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
