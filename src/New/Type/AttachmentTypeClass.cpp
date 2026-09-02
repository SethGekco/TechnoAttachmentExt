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
	this->PoweredBy.Read(exINI, section, "PoweredBy");
	this->PoweredBy_RequireAll.Read(exINI, section, "PoweredBy.RequireAll");
	this->PoweredBy_RequirePower.Read(exINI, section, "PoweredBy.RequirePower");
	this->PoweredBy_Range.Read(exINI, section, "PoweredBy.Range");
	{
		int houseMask = TAExtHouse_None;
		if (TAExt_ReadHouseRelationList(pINI, section, "PoweredBy.House", houseMask))
			this->PoweredBy_House = houseMask;
	}
	this->RequiresPassengers.Read(exINI, section, "RequiresPassengers");
	this->RequiresSlot_Index.Read(exINI, section, "RequiresSlot.Index");
	this->RequiresSlot_Type.Read(exINI, section, "RequiresSlot.Type");
	this->Decorative.Read(exINI, section, "Decorative");
	// ---- A2 experience rules -------------------------------------------------
	// One unindexed group plus contiguous [0], [1], ... groups. The unindexed group
	// and [0] are INDEPENDENT rules (deliberately not aliases), so a modder can use
	// both. The [N] scan stops at the first index with no ExperienceTo key.
	this->ExperienceRules.clear();
	{
		// Longest key formatted below is "ExperienceTo.Share[NN]" -- 64 bytes is
		// ample. (An undersized buffer here is what once caused a silent CRT abort
		// at load, so keep the headroom.)
		char key[64];
		char buffer[256];

		auto const readGroup = [&](const char* suffix) -> bool
		{
			_snprintf_s(key, sizeof(key), "ExperienceTo%s", suffix);
			if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
				return false; // no such group

			ExperienceRule rule;

			char* context = nullptr;
			for (char* tok = strtok_s(buffer, ",", &context); tok; tok = strtok_s(nullptr, ",", &context))
			{
				while (*tok == ' ')
					++tok;

				AttachmentRelation rel;
				if (TAExt_ParseRelation(tok, rel))
					rule.To.push_back(rel);
				else
					Debug::INIParseFailed(section, key, tok,
						"Expected relations (parent/root/child/sibling/children/siblings)");
			}

			_snprintf_s(key, sizeof(key), "ExperienceTo.Share%s", suffix);
			rule.Share = pINI->ReadInteger(section, key, 100);

			_snprintf_s(key, sizeof(key), "ExperienceTo.Drain%s", suffix);
			rule.Drain = pINI->ReadBool(section, key, false);

			_snprintf_s(key, sizeof(key), "ExperienceTo.Slot%s", suffix);
			rule.Slot = pINI->ReadInteger(section, key, 0);

			char idBuffer[64];
			_snprintf_s(key, sizeof(key), "ExperienceTo.ID%s", suffix);
			if (pINI->ReadString(section, key, "", idBuffer, sizeof(idBuffer)) > 0)
				rule.ID = idBuffer;

			if (!rule.To.empty())
				this->ExperienceRules.emplace_back(std::move(rule));

			return true; // key existed: keep scanning further indices
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
	// ---- convert-in-place rules -----------------------------------------------
	// Same group shape as ExperienceTo: unindexed group, then contiguous [0], [1]...
	// First matching rule wins; no match reverts to the slot's configured type.
	this->ConvertRules.clear();
	this->Convert_KeepHealth.Read(exINI, section, "Convert.KeepHealth");
	this->Convert_KeepVeterancy.Read(exINI, section, "Convert.KeepVeterancy");
	{
		char key[64];

		auto const readConvertGroup = [&](const char* suffix) -> bool
		{
			Nullable<TechnoTypeClass*> to;
			_snprintf_s(key, sizeof(key), "Convert%s", suffix);
			to.Read(exINI, section, key);
			if (!to.isset())
				return false; // no such group -> stop scanning

			ConvertRule rule;
			rule.To = to.Get();

			Nullable<int> minHealth, maxHealth;
			_snprintf_s(key, sizeof(key), "Convert.MinHealth%s", suffix);
			minHealth.Read(exINI, section, key);
			_snprintf_s(key, sizeof(key), "Convert.MaxHealth%s", suffix);
			maxHealth.Read(exINI, section, key);

			Nullable<Rank> minRank, maxRank;
			_snprintf_s(key, sizeof(key), "Convert.MinRank%s", suffix);
			minRank.Read(exINI, section, key);
			_snprintf_s(key, sizeof(key), "Convert.MaxRank%s", suffix);
			maxRank.Read(exINI, section, key);

			rule.MinHealth = minHealth.isset() ? minHealth.Get() : -1;
			rule.MaxHealth = maxHealth.isset() ? maxHealth.Get() : -1;
			rule.MinRank = minRank.isset() ? static_cast<int>(minRank.Get()) : -1;
			rule.MaxRank = maxRank.isset() ? static_cast<int>(maxRank.Get()) : -1;

			if (rule.To)
				this->ConvertRules.emplace_back(rule);

			return true;
		};

		readConvertGroup(""); // unindexed group

		for (int i = 0; ; ++i)
		{
			char suffix[16];
			_snprintf_s(suffix, sizeof(suffix), "[%d]", i);
			if (!readConvertGroup(suffix))
				break;
		}
	}

	this->Ammo_Parent.Read(exINI, section, "Ammo.Parent");
	this->Intangible.Read(exINI, section, "Intangible");
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
		.Process(this->PoweredBy)
		.Process(this->PoweredBy_RequireAll)
		.Process(this->PoweredBy_RequirePower)
		.Process(this->PoweredBy_Range)
		.Process(this->PoweredBy_House)
		.Process(this->RequiresPassengers)
		.Process(this->RequiresSlot_Index)
		.Process(this->RequiresSlot_Type)
		.Process(this->Decorative)
		.Process(this->Convert_KeepHealth)
		.Process(this->Convert_KeepVeterancy)
		.Process(this->Ammo_Parent)
		.Process(this->Intangible)
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
