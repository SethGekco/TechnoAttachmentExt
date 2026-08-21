#include "AttachmentTypeClass.h"

#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>

#include <AttachmentParsers.h>

const char* Enumerable<AttachmentTypeClass>::GetMainSection()
{
	return "AttachmentTypes";
}

void AttachmentTypeClass::LoadFromINI(CCINIClass* pINI)
{
	const char* section = this->Name;

	INI_EX exINI(pINI);

	this->RespawnAtCreation.Read(exINI, section, "RespawnAtCreation");
	this->RespawnDelay.Read(exINI, section, "RespawnDelay");
	this->InheritCommands.Read(exINI, section, "InheritCommands");
	this->InheritCommands_StopCommand.Read(exINI, section, "InheritCommands.StopCommand");
	this->InheritCommands_DeployCommand.Read(exINI, section, "InheritCommands.DeployCommand");
	this->InheritOwner.Read(exINI, section, "InheritOwner");
	this->InheritStateEffects.Read(exINI, section, "InheritStateEffects");
	this->InheritDestruction.Read(exINI, section, "InheritDestruction");
	this->InheritHeightStatus.Read(exINI, section, "InheritHeightStatus");
	this->PowersParent.Read(exINI, section, "PowersParent");
	// Sibling powering -- accept both singular and plural spellings (plural wins if
	// both present, since it is read last).
	this->PowersSiblings.Read(exINI, section, "PowersSibling");
	this->PowersSiblings.Read(exINI, section, "PowersSiblings");
	this->PowersSiblings_Type.Read(exINI, section, "PowersSibling.Type");
	this->PowersSiblings_Type.Read(exINI, section, "PowersSiblings.Type");
	this->PowersSiblings_Index.Read(exINI, section, "PowersSibling.Index");
	this->PowersSiblings_Index.Read(exINI, section, "PowersSiblings.Index");
	this->Powered.Read(exINI, section, "Powered");
	this->Powered_Type.Read(exINI, section, "Powered.Type");
	this->PoweredByParent.Read(exINI, section, "PoweredByParent");
	this->RequiresPassengers.Read(exINI, section, "RequiresPassengers");
	this->RequiresSlot_Index.Read(exINI, section, "RequiresSlot.Index");
	this->RequiresSlot_Type.Read(exINI, section, "RequiresSlot.Type");
	this->Decorative.Read(exINI, section, "Decorative");
	this->OccupiesCell.Read(exINI, section, "OccupiesCell");
	this->LowSelectionPriority.Read(exINI, section, "LowSelectionPriority");
	this->PassSelection.Read(exINI, section, "PassSelection");
	this->TransparentToMouse.Read(exINI, section, "TransparentToMouse");
	this->YSortPosition.Read(exINI, section, "YSortPosition");
	this->DestructionWeapon_Child.Read(exINI, section, "DestructionWeapon.Child");
	this->DestructionWeapon_Parent.Read(exINI, section, "DestructionWeapon.Parent");
	this->ParentDestructionMission.Read(exINI, section, "ParentDestructionMission");
	this->ParentDetachmentMission.Read(exINI, section, "ParentDetachmentMission");
	this->Prerequisite.Read(exINI, section, "Prerequisite");
	this->Prerequisite_Negative.Read(exINI, section, "Prerequisite.Negative");
	this->RequiredHouses.Read(exINI, section, "RequiredHouses");
	this->ForbiddenHouses.Read(exINI, section, "ForbiddenHouses");
	this->Prerequisite_MinRank.Read(exINI, section, "Prerequisite.MinRank");
	this->Prerequisite_MaxRank.Read(exINI, section, "Prerequisite.MaxRank");
	this->Prerequisite_MinHealth.Read(exINI, section, "Prerequisite.MinHealth");
	this->Prerequisite_MaxHealth.Read(exINI, section, "Prerequisite.MaxHealth");
	this->Prerequisite_Dynamic.Read(exINI, section, "Prerequisite.Dynamic");

	this->Prerequisite_Sibling_Index.Read(exINI, section, "Prerequisite.Sibling.Index");
	this->Prerequisite_Sibling_Type.Read(exINI, section, "Prerequisite.Sibling.Type");
	this->Prerequisite_Siblings_Index.Read(exINI, section, "Prerequisite.Siblings.Index");
	this->Prerequisite_Siblings_Type.Read(exINI, section, "Prerequisite.Siblings.Type");

	// E1b: alternative OR building lists Prerequisite[0], Prerequisite[1], ...
	// read contiguously; the first unset (empty) index ends the list.
	this->Prerequisite_Lists.clear();
	for (int i = 0; ; ++i)
	{
		char key[32];
		_snprintf_s(key, sizeof(key), "Prerequisite[%d]", i);

		ValueableVector<BuildingTypeClass*> list;
		list.Read(exINI, section, key);
		if (list.empty())
			break;

		this->Prerequisite_Lists.emplace_back(std::move(list));
	}

	// Decorative profile: force the "this is a cosmetic/functional piece, not a
	// unit" bundle. Applied AFTER the individual reads so it wins over the
	// defaults; it only switches the bundle on, never off.
	if (this->Decorative)
	{
		this->PassSelection = true;
		this->TransparentToMouse = true;
		this->LowSelectionPriority = true;
		this->OccupiesCell = false;
	}
}

template <typename T>
void AttachmentTypeClass::Serialize(T& Stm)
{
	Stm
		.Process(this->RespawnAtCreation)
		.Process(this->RespawnDelay)
		.Process(this->InheritCommands)
		.Process(this->InheritCommands_StopCommand)
		.Process(this->InheritCommands_DeployCommand)
		.Process(this->InheritOwner)
		.Process(this->InheritStateEffects)
		.Process(this->InheritDestruction)
		.Process(this->InheritHeightStatus)
		.Process(this->PowersParent)
		.Process(this->PowersSiblings)
		.Process(this->PowersSiblings_Type)
		.Process(this->PowersSiblings_Index)
		.Process(this->Powered)
		.Process(this->Powered_Type)
		.Process(this->PoweredByParent)
		.Process(this->RequiresPassengers)
		.Process(this->RequiresSlot_Index)
		.Process(this->RequiresSlot_Type)
		.Process(this->Decorative)
		.Process(this->OccupiesCell)
		.Process(this->LowSelectionPriority)
		.Process(this->PassSelection)
		.Process(this->TransparentToMouse)
		.Process(this->YSortPosition)
		.Process(this->DestructionWeapon_Child)
		.Process(this->DestructionWeapon_Parent)
		.Process(this->ParentDestructionMission)
		.Process(this->ParentDetachmentMission)
		.Process(this->Prerequisite)
		.Process(this->Prerequisite_Negative)
		.Process(this->RequiredHouses)
		.Process(this->ForbiddenHouses)
		.Process(this->Prerequisite_MinRank)
		.Process(this->Prerequisite_MaxRank)
		.Process(this->Prerequisite_MinHealth)
		.Process(this->Prerequisite_MaxHealth)
		.Process(this->Prerequisite_Dynamic)
		.Process(this->Prerequisite_Sibling_Index)
		.Process(this->Prerequisite_Sibling_Type)
		.Process(this->Prerequisite_Siblings_Index)
		.Process(this->Prerequisite_Siblings_Type)
		;
}

void AttachmentTypeClass::LoadFromStream(PhobosStreamReader& Stm)
{
	this->Serialize(Stm);
}

void AttachmentTypeClass::SaveToStream(PhobosStreamWriter& Stm)
{
	this->Serialize(Stm);
}
