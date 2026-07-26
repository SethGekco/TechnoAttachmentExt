#pragma once

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
	// yes = child hides/shows live as the prerequisite is gained/lost.
	// no  = prerequisite is only checked at spawn (static gate; no live toggle).
	Valueable<bool> Prerequisite_Dynamic;

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
		, OccupiesCell { true }
		, LowSelectionPriority { true }
		, PassSelection { true }
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
		, Prerequisite_Dynamic { true }
	{ }

	virtual ~AttachmentTypeClass() = default;

	virtual void LoadFromINI(CCINIClass* pINI);
	virtual void LoadFromStream(PhobosStreamReader& Stm);
	virtual void SaveToStream(PhobosStreamWriter& Stm);

private:
	template <typename T>
	void Serialize(T& Stm);
};
