#include "Body.h"

#include <set>

#include <AttachmentParsers.h>

TechnoTypeExt::ExtContainer TechnoTypeExt::ExtMap;

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	const char* pSection = this->AttachedToObject->ID;

	if (!pINI->GetSection(pSection))
		return;

	INI_EX exINI(pINI);

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

		AttachmentDataEntry const entry { ValueableIdx<AttachmentTypeClass>(type), technoType, flh, isOnTurret, rotationAdjust, id };
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
		.Success();
}

// ============================================================================
// Container
// ============================================================================

TechnoTypeExt::ExtContainer::ExtContainer()
	: Container("TechnoTypeClass")
{ }

TechnoTypeExt::ExtContainer::~ExtContainer() = default;
