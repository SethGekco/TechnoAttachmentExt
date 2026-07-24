#include "Body.h"

#include <set>

#include <Matrix3D.h>
#include <BuildingTypeClass.h>
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

	// The following loop iterates over size + 1 INI entries so that the
	// vector contents can be properly overriden via scenario rules - Kerbiter
	for (size_t i = 0; i <= this->AttachmentData.size(); ++i)
	{
		char tempBuffer[32];
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

		AttachmentDataEntry const entry { ValueableIdx<AttachmentTypeClass>(type), technoType, flh, isOnTurret, rotationAdjust, id, prereq };
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
