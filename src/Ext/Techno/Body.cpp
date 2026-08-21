#include "Body.h"

#include <TechnoClass.h>
#include <FootClass.h>
#include <BulletClass.h>
#include <BulletTypeClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <RulesClass.h>
#include <Matrix3D.h>

#include <Helpers/Cast.h>
#include <Utilities/Macro.h>

#include <Ext/TechnoType/Body.h>
#include <New/Entity/AttachmentClass.h>

TechnoExt::ExtContainer TechnoExt::ExtMap;
TechnoClass* TechnoExt::DeployTransferSource = nullptr;

TechnoExt::ExtData::~ExtData() = default;

void TechnoExt::ExtData::InvalidatePointer(void* ptr, bool bRemoved)
{
	for (auto const& pAttachment : this->ChildAttachments)
		pAttachment->InvalidatePointer(ptr);

	for (auto& [key, vec] : this->DormantAttachments)
		for (auto const& pAttachment : vec)
			pAttachment->InvalidatePointer(ptr);
}

template <typename T>
void TechnoExt::ExtData::Serialize(T& Stm)
{
	// NOTE: matching the upstream PR, only AltOccupation is stream-serialized;
	// the live attachment vectors are not persisted across save/load yet.
	Stm
		.Process(this->AltOccupation)
		.Process(this->DeactivationReasons)
		;
}

void TechnoExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<TechnoClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TechnoExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<TechnoClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Ported Phobos TechnoExt helpers the attachment runtime needs.
// ============================================================================

CoordStruct TechnoExt::GetFLHAbsoluteCoords(TechnoClass* pThis, CoordStruct pCoord, bool isOnTurret)
{
	auto const pType = pThis->GetTechnoType();
	auto const pFoot = abstract_cast<FootClass*, true>(pThis);
	Matrix3D mtx;

	// Step 1: get body transform matrix
	if (pFoot && pFoot->Locomotor)
		mtx = pFoot->Locomotor->Draw_Matrix(nullptr);
	else // no locomotor means no rotation or transform of any kind (f.ex. buildings) - Kerbiter
		mtx.MakeIdentity();

	// Steps 2-3: turret offset and rotation
	if (isOnTurret && (pType->Turret || !pFoot)) // If building has no turret, its TurretFacing is TargetDirection
	{
		TechnoTypeExt::ApplyTurretOffset(pType, &mtx);

		const double turretRad = pThis->TurretFacing().GetRadian<32>();
		// For BuildingClass turret facing is equal to primary facing
		const float angle = pFoot ? (float)(turretRad - pThis->PrimaryFacing.Current().GetRadian<32>()) : (float)(turretRad);

		mtx.RotateZ(angle);
	}

	// Step 4: apply FLH offset
	mtx.Translate((float)pCoord.X, (float)pCoord.Y, (float)pCoord.Z);

	auto const result = mtx.GetTranslation();

	// Step 5: apply as an offset to global object coords
	// Resulting coords are mirrored along X axis, so we mirror it back
	auto const location = pThis->GetRenderCoords() + CoordStruct { (int)result.X, -(int)result.Y, (int)result.Z };

	return location;
}

// Self-contained port of Phobos's FireWeaponAtSelf (which routes through
// WeaponTypeExt::DetonateAt -> BulletExt::Detonate). We reproduce the core of
// BulletExt::Detonate directly with vanilla YRpp, omitting only the Phobos
// bullet-ext IsInstantDetonation guard (we UnInit immediately so it can't
// re-detonate anyway).
void TechnoExt::FireWeaponAtSelf(TechnoClass* pThis, WeaponTypeClass* pWeaponType)
{
	if (!pWeaponType || !pWeaponType->Projectile)
		return;

	auto const coords = pThis->GetCoords();

	auto const pBullet = pWeaponType->Projectile->CreateBullet(
		pThis, pThis, pWeaponType->Damage, pWeaponType->Warhead, pWeaponType->Speed, pWeaponType->Bright);

	if (!pBullet)
		return;

	pBullet->WeaponType = pWeaponType;
	pBullet->SetLocation(coords);
	pBullet->Explode(true);
	pBullet->UnInit();
}

void TechnoExt::Kill(TechnoClass* pThis, ObjectClass* pAttacker, HouseClass* pAttackingHouse)
{
	// NOTE: Phobos also invokes AresFunctions::SpawnSurvivors here when Ares is
	// present; omitted in this standalone build (optional enhancement).
	pThis->ReceiveDamage(&pThis->Health, 0, RulesClass::Instance->C4Warhead, pAttacker, true, false, pAttackingHouse);
}

void TechnoExt::Kill(TechnoClass* pThis, TechnoClass* pAttacker)
{
	TechnoExt::Kill(pThis, pAttacker, pAttacker ? pAttacker->Owner : nullptr);
}

// ============================================================================
// Container
// ============================================================================

TechnoExt::ExtContainer::ExtContainer()
	: Container("TechnoClass")
{ }

TechnoExt::ExtContainer::~ExtContainer() = default;

// ============================================================================
// Container lifecycle hooks — addresses verified from Phobos (develop).
// ============================================================================

DEFINE_HOOK(0x6F3260, TechnoClass_CTOR_TAExt, 0x5)
{
	GET(TechnoClass*, pItem, ESI);
	TechnoExt::ExtMap.TryAllocate(pItem);
	return 0;
}

DEFINE_HOOK(0x6F4500, TechnoClass_DTOR_TAExt, 0x5)
{
	GET(TechnoClass*, pItem, ECX);
	TechnoExt::ExtMap.Remove(pItem);
	return 0;
}

DEFINE_HOOK_AGAIN(0x70C250, TechnoClass_SaveLoad_Prefix_TAExt, 0x8)
DEFINE_HOOK(0x70BF50, TechnoClass_SaveLoad_Prefix_TAExt, 0x5)
{
	GET_STACK(TechnoClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);
	TechnoExt::ExtMap.PrepareStream(pItem, pStm);
	return 0;
}

DEFINE_HOOK(0x70C249, TechnoClass_Load_Suffix_TAExt, 0x5)
{
	TechnoExt::ExtMap.LoadStatic();
	return 0;
}

DEFINE_HOOK(0x70C264, TechnoClass_Save_Suffix_TAExt, 0x5)
{
	TechnoExt::ExtMap.SaveStatic();
	return 0;
}
