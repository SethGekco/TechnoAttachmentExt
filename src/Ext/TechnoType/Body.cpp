#include "Body.h"

#include <set>

#include <Matrix3D.h>
#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>
#include <Utilities/Macro.h>

#include <AttachmentParsers.h>

TechnoTypeExt::ExtContainer TechnoTypeExt::ExtMap;

void TechnoTypeExt::ExtData::ApplyTurretOffset(Matrix3D* mtx, double factor)
{
	// Does not verify if the offset actually has all values parsed as it makes
	// no difference, it will be 0 for the unparsed ones either way.
	const auto offset = this->TurretOffset.GetEx();
	const float x = static_cast<float>(offset->X * factor);
	const float y = static_cast<float>(offset->Y * factor);
	const float z = static_cast<float>(offset->Z * factor);

	mtx->Translate(x, y, z);
}

void TechnoTypeExt::ApplyTurretOffset(TechnoTypeClass* pType, Matrix3D* mtx, double factor)
{
	TechnoTypeExt::ExtMap.Find(pType)->ApplyTurretOffset(mtx, factor);
}

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	const char* pSection = this->OwnerObject()->ID;

	if (!pINI->GetSection(pSection))
		return;

	INI_EX exINI(pINI);

	// Turret offset lives in the art INI, keyed on the type's Image name.
	const auto pArtINI = &CCINIClass::INI_Art;
	INI_EX exArtINI(pArtINI);
	auto pArtSection = this->OwnerObject()->ImageFile;
	this->TurretOffset.Read(exArtINI, pArtSection, "TurretOffset");

	this->AttachmentTopLayerMinHeight.Read(exINI, pSection, "AttachmentTopLayerMinHeight");
	this->AttachmentUndergroundLayerMaxHeight.Read(exINI, pSection, "AttachmentUndergroundLayerMaxHeight");

	this->Experience_Multiplier.Read(exINI, pSection, "Experience.Multiplier");

	// ---- power network ----
	this->PowerSource.Read(exINI, pSection, "PowerSource");
	this->PowerSource_Range.Read(exINI, pSection, "PowerSource.Range");
	this->PowerSource_Types.Read(exINI, pSection, "PowerSource.Types");
	this->PowerSource_Count.Read(exINI, pSection, "PowerSource.Count");
	this->PowerRelay.Read(exINI, pSection, "PowerRelay");
	this->PowerRelay_Range.Read(exINI, pSection, "PowerRelay.Range");
	this->PowerConsumer.Read(exINI, pSection, "PowerConsumer");
	this->PowerConsumer_Types.Read(exINI, pSection, "PowerConsumer.Types");

	{
		int houseMask = TAExtHouse_None;
		if (TAExt_ReadHouseRelationList(pINI, pSection, "PowerConsumer.House", houseMask))
			this->PowerConsumer_House = houseMask;
		if (TAExt_ReadHouseRelationList(pINI, pSection, "PowerSource.House", houseMask))
			this->PowerSource_House = houseMask;
	}

	// External-structure power on a plain TechnoType (unit or building).
	this->PoweredBy.Read(exINI, pSection, "PoweredBy");
	this->PoweredBy_RequireAll.Read(exINI, pSection, "PoweredBy.RequireAll");
	this->PoweredBy_RequirePower.Read(exINI, pSection, "PoweredBy.RequirePower");
	this->PoweredBy_Range.Read(exINI, pSection, "PoweredBy.Range");
	{
		int houseMask = TAExtHouse_None;
		if (TAExt_ReadHouseRelationList(pINI, pSection, "PoweredBy.House", houseMask))
			this->PoweredBy_House = houseMask;
	}

	// OverflowMode=all|excess -- "all" drops the whole group past the cap,
	// "excess" (default) drops only the ones past it.
	char overflowBuffer[32];
	if (pINI->ReadString(pSection, "PowerSource.OverflowMode", "", overflowBuffer, sizeof(overflowBuffer)) > 0)
		this->PowerSource_OverflowAll = (_strcmpi(overflowBuffer, "all") == 0);

	// The following loop iterates over size + 1 INI entries so that the
	// vector contents can be properly overriden via scenario rules - Kerbiter
	for (size_t i = 0; i <= this->AttachmentData.size(); ++i)
	{
		// Must fit the longest key we format below, e.g.
		// "Attachment0.Prerequisite.Negative" (33 chars + null). A 32-byte buffer
		// overflowed here: _snprintf_s then invokes the CRT invalid-parameter
		// handler, terminating the process instantly with no exception dump.
		char tempBuffer[64];
		NullableIdx<AttachmentTypeClass> type;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.Type", static_cast<int>(i));
		type.Read(exINI, pSection, tempBuffer);

		if (!type.isset())
			continue;

		NullableIdx<TechnoTypeClass> technoType;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.TechnoType", static_cast<int>(i));
		technoType.Read(exINI, pSection, tempBuffer);

		Valueable<CoordStruct> flh;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.FLH", static_cast<int>(i));
		flh.Read(exINI, pSection, tempBuffer);

		Valueable<bool> isOnTurret;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.IsOnTurret", static_cast<int>(i));
		isOnTurret.Read(exINI, pSection, tempBuffer);

		Valueable<DirType> rotationAdjust;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.RotationAdjust", static_cast<int>(i));
		rotationAdjust.Read(exINI, pSection, tempBuffer);

		PhobosFixedString<32> id;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.ID", static_cast<int>(i));
		id.Read(pINI, pSection, tempBuffer);

		ValueableVector<BuildingTypeClass*> prereq;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.Prerequisite", static_cast<int>(i));
		prereq.Read(exINI, pSection, tempBuffer);

		ValueableVector<BuildingTypeClass*> prereqNeg;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.Prerequisite.Negative", static_cast<int>(i));
		prereqNeg.Read(exINI, pSection, tempBuffer);

		ValueableVector<HouseTypeClass*> reqHouses;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.RequiredHouses", static_cast<int>(i));
		reqHouses.Read(exINI, pSection, tempBuffer);

		ValueableVector<HouseTypeClass*> forbHouses;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.ForbiddenHouses", static_cast<int>(i));
		forbHouses.Read(exINI, pSection, tempBuffer);

		Nullable<bool> prereqDynamic;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.Prerequisite.Dynamic", static_cast<int>(i));
		prereqDynamic.Read(exINI, pSection, tempBuffer);

		Nullable<AttachmentYSortPosition> ySortPosition;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.YSortPosition", static_cast<int>(i));
		ySortPosition.Read(exINI, pSection, tempBuffer);

		// Per-slot behaviour overrides (slot has the final word over AttachmentType).
		Nullable<bool> slotPowersParent;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PowersParent", static_cast<int>(i));
		slotPowersParent.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotPowered;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.Powered", static_cast<int>(i));
		slotPowered.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotPowersSiblings;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PowersSibling", static_cast<int>(i));
		slotPowersSiblings.Read(exINI, pSection, tempBuffer);
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PowersSiblings", static_cast<int>(i));
		slotPowersSiblings.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotPoweredByParent;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredByParent", static_cast<int>(i));
		slotPoweredByParent.Read(exINI, pSection, tempBuffer);

		Nullable<int> slotRequiresPassengers;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.RequiresPassengers", static_cast<int>(i));
		slotRequiresPassengers.Read(exINI, pSection, tempBuffer);

		ValueableVector<BuildingTypeClass*> slotPoweredBy;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredBy", static_cast<int>(i));
		slotPoweredBy.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotPoweredByRequireAll;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredBy.RequireAll", static_cast<int>(i));
		slotPoweredByRequireAll.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotPoweredByRequirePower;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredBy.RequirePower", static_cast<int>(i));
		slotPoweredByRequirePower.Read(exINI, pSection, tempBuffer);

		Nullable<int> slotPoweredByRange;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredBy.Range", static_cast<int>(i));
		slotPoweredByRange.Read(exINI, pSection, tempBuffer);

		Valueable<int> slotPoweredByHouse { -1 };
		{
			int houseMask = TAExtHouse_None;
			_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PoweredBy.House", static_cast<int>(i));
			if (TAExt_ReadHouseRelationList(pINI, pSection, tempBuffer, houseMask))
				slotPoweredByHouse = houseMask;
		}

		Nullable<bool> slotPassSelection;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.PassSelection", static_cast<int>(i));
		slotPassSelection.Read(exINI, pSection, tempBuffer);

		Nullable<bool> slotTransparentToMouse;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Attachment%d.TransparentToMouse", static_cast<int>(i));
		slotTransparentToMouse.Read(exINI, pSection, tempBuffer);

		AttachmentDataEntry const entry { ValueableIdx<AttachmentTypeClass>(type), technoType, flh, isOnTurret, rotationAdjust, id, prereq, prereqNeg, reqHouses, forbHouses, prereqDynamic, ySortPosition,
			slotPowersParent, slotPowered, slotPowersSiblings, slotPoweredByParent,
			slotRequiresPassengers,
			slotPoweredBy, slotPoweredByRequireAll, slotPoweredByRequirePower, slotPoweredByRange, slotPoweredByHouse,
			slotPassSelection, slotTransparentToMouse };
		if (i == this->AttachmentData.size())
			this->AttachmentData.push_back(entry);
		else
			this->AttachmentData[i] = entry;
	}

	// Validate attachment ID uniqueness
	std::set<PhobosFixedString<32>> usedIds;
	for (size_t i = 0; i < this->AttachmentData.size(); ++i)
	{
		const auto& id = this->AttachmentData[i].ID;

		if (!id)
			continue;

		if (!usedIds.insert(id).second)
		{
			Debug::FatalErrorAndExit("[%s] Duplicate Attachment ID '%s'\n",
				pSection, id);
		}
	}
}

template <typename T>
void TechnoTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->AttachmentTopLayerMinHeight)
		.Process(this->AttachmentUndergroundLayerMaxHeight)
		.Process(this->TurretOffset)
		.Process(this->Experience_Multiplier)
		.Process(this->PowerSource)
		.Process(this->PowerSource_Range)
		.Process(this->PowerSource_Types)
		.Process(this->PowerSource_Count)
		.Process(this->PowerSource_OverflowAll)
		.Process(this->PowerRelay)
		.Process(this->PowerRelay_Range)
		.Process(this->PowerConsumer)
		.Process(this->PowerConsumer_Types)
		.Process(this->PowerConsumer_House)
		.Process(this->PowerSource_House)
		.Process(this->PoweredBy)
		.Process(this->PoweredBy_RequireAll)
		.Process(this->PoweredBy_RequirePower)
		.Process(this->PoweredBy_Range)
		.Process(this->PoweredBy_House)
		.Process(this->AttachmentData)
		;
}

void TechnoTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<TechnoTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TechnoTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<TechnoTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// AttachmentDataEntry save/load
// ============================================================================

bool TechnoTypeExt::ExtData::AttachmentDataEntry::Load(PhobosStreamReader& stm, bool registerForChange)
{
	return this->Serialize(stm);
}

bool TechnoTypeExt::ExtData::AttachmentDataEntry::Save(PhobosStreamWriter& stm) const
{
	return const_cast<AttachmentDataEntry*>(this)->Serialize(stm);
}

template <typename T>
bool TechnoTypeExt::ExtData::AttachmentDataEntry::Serialize(T& stm)
{
	return stm
		.Process(this->Type)
		.Process(this->TechnoType)
		.Process(this->FLH)
		.Process(this->IsOnTurret)
		.Process(this->RotationAdjust)
		.Process(this->ID)
		.Process(this->Prerequisite)
		.Process(this->Prerequisite_Negative)
		.Process(this->RequiredHouses)
		.Process(this->ForbiddenHouses)
		.Process(this->Prerequisite_Dynamic)
		.Process(this->YSortPosition)
		.Process(this->PowersParent)
		.Process(this->Powered)
		.Process(this->PowersSiblings)
		.Process(this->PoweredByParent)
		.Process(this->RequiresPassengers)
		.Process(this->PoweredBy)
		.Process(this->PoweredBy_RequireAll)
		.Process(this->PoweredBy_RequirePower)
		.Process(this->PoweredBy_Range)
		.Process(this->PoweredBy_House)
		.Process(this->PassSelection)
		.Process(this->TransparentToMouse)
		.Success();
}

// ============================================================================
// Container
// ============================================================================

TechnoTypeExt::ExtContainer::ExtContainer()
	: Container("TechnoTypeClass")
{ }

TechnoTypeExt::ExtContainer::~ExtContainer() = default;

// ============================================================================
// Container lifecycle hooks — addresses verified from Phobos (develop).
// map-mode container: TryAllocate on ctor, Remove on dtor, Prepare/Static on
// save/load, LoadFromINI on the type's INI read.
// ============================================================================

DEFINE_HOOK(0x711835, TechnoTypeClass_CTOR_TAExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, ESI);
	TechnoTypeExt::ExtMap.TryAllocate(pItem);
	return 0;
}

DEFINE_HOOK(0x711AE0, TechnoTypeClass_DTOR_TAExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, ECX);
	TechnoTypeExt::ExtMap.Remove(pItem);
	return 0;
}

DEFINE_HOOK_AGAIN(0x716DC0, TechnoTypeClass_SaveLoad_Prefix_TAExt, 0x5)
DEFINE_HOOK(0x7162F0, TechnoTypeClass_SaveLoad_Prefix_TAExt, 0x6)
{
	GET_STACK(TechnoTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);
	TechnoTypeExt::ExtMap.PrepareStream(pItem, pStm);
	return 0;
}

DEFINE_HOOK(0x716DAC, TechnoTypeClass_Load_Suffix_TAExt, 0xA)
{
	TechnoTypeExt::ExtMap.LoadStatic();
	return 0;
}

DEFINE_HOOK(0x717094, TechnoTypeClass_Save_Suffix_TAExt, 0x5)
{
	TechnoTypeExt::ExtMap.SaveStatic();
	return 0;
}

DEFINE_HOOK(0x716123, TechnoTypeClass_LoadFromINI_TAExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, EBP);
	GET_STACK(CCINIClass*, pINI, 0x380);
	TechnoTypeExt::ExtMap.LoadFromINI(pItem, pINI);
	return 0;
}
