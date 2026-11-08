#include "Body.h"

#include <algorithm>
#include <intrin.h>

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
std::vector<TechnoExt::ExtData*> TechnoExt::PointerHolders;
TechnoClass* TechnoExt::DeployTransferSource = nullptr;

TechnoExt::ExtData::~ExtData()
{
	this->UnmarkPointerHolder();
}

void TechnoExt::ExtData::MarkPointerHolder()
{
	if (this->PointerHolderIndex < 0)
	{
		this->PointerHolderIndex = static_cast<int>(TechnoExt::PointerHolders.size());
		TechnoExt::PointerHolders.push_back(this);
	}
}

void TechnoExt::ExtData::UnmarkPointerHolder()
{
	int const index = this->PointerHolderIndex;
	if (index < 0)
		return;

	auto& holders = TechnoExt::PointerHolders;
	auto const pLast = holders.back();
	holders[index] = pLast;
	pLast->PointerHolderIndex = index;
	holders.pop_back();
	this->PointerHolderIndex = -1;
}

bool TechnoExt::ExtData::HoldsNoPointers() const
{
	if (!this->InstantSpawnLive.empty() || !this->ChildAttachments.empty())
		return false;

	for (auto const& [key, vec] : this->DormantAttachments)
	{
		if (!vec.empty())
			return false;
	}

	return true;
}

// TAEXT_VERIFY_HOLDERS=1 in the game's environment (test runs only): after
// every holder walk the old full walk runs as well, and any techno outside
// PointerHolders that still holds something is counted as a miss. A miss
// would be a write without MarkPointerHolder; the full walk then clears it
// exactly as before, so a verify run is safe either way. Both walks are
// timed, which gives the old and new cost on the same deaths in one game.
static bool VerifyPass = false;
static unsigned VerifyMisses = 0;

static bool VerifyHoldersEnabled()
{
	static int const enabled = []
	{
		char value[8] {};
		return GetEnvironmentVariableA("TAEXT_VERIFY_HOLDERS", value, sizeof(value)) > 0 && value[0] == '1';
	}();

	return enabled != 0;
}

void TechnoExt::PointerGotInvalid(void* ptr, bool bRemoved)
{
	if (ExtMap.InvalidateExtDataIgnorable(ptr))
		return;

	bool const verify = VerifyHoldersEnabled();
	unsigned __int64 const start = verify ? __rdtsc() : 0;

	// Back to front, so pruning (which moves the last entry, already visited,
	// into this slot) never skips anyone.
	auto& holders = TechnoExt::PointerHolders;
	for (size_t i = holders.size(); i-- > 0; )
	{
		auto const pExt = holders[i];
		pExt->InvalidatePointer(ptr, bRemoved);

		if (pExt->HoldsNoPointers())
			pExt->UnmarkPointerHolder();
	}

	if (!verify)
		return;

	static unsigned walks = 0;
	static unsigned maxHolders = 0;
	static unsigned __int64 holderCycles = 0;
	static unsigned __int64 fullCycles = 0;

	unsigned __int64 const middle = __rdtsc();
	unsigned const missesBefore = VerifyMisses;
	VerifyPass = true;
	ExtMap.PointerGotInvalid(ptr, bRemoved);
	VerifyPass = false;
	unsigned __int64 const end = __rdtsc();

	++walks;
	holderCycles += middle - start;
	fullCycles += end - middle;
	maxHolders = std::max(maxHolders, static_cast<unsigned>(holders.size()));

	if (VerifyMisses != missesBefore || walks % 2000 == 0)
	{
		Debug::Log("[TAExt] pointer holders: %u walks, %u holders now (max %u), %u misses, "
			"holder walk %llu Mcycles, full walk %llu Mcycles\n",
			walks, static_cast<unsigned>(holders.size()), maxHolders, VerifyMisses,
			holderCycles / 1000000, fullCycles / 1000000);
	}
}

AttachmentClass* TechnoExt::ExtData::GetLiveParentAttachment()
{
	if (this->ParentAttachment && !AttachmentClass::IsLive(this->ParentAttachment))
		this->ParentAttachment = nullptr; // the slot is gone; stop pointing at it

	return this->ParentAttachment;
}

void TechnoExt::ExtData::InvalidatePointer(void* ptr, bool bRemoved)
{
	if (VerifyPass)
	{
		if (this->PointerHolderIndex >= 0)
			return; // the holder walk already did this one

		if (!this->HoldsNoPointers())
			++VerifyMisses;
	}

	// H1e: drop any live-spawn record for the object going away, BEFORE it is
	// freed. The two vectors are parallel, so they must be erased in lockstep.
	for (size_t i = this->InstantSpawnLive.size(); i-- > 0; )
	{
		if (this->InstantSpawnLive[i] == ptr)
		{
			this->InstantSpawnLive.erase(this->InstantSpawnLive.begin() + i);
			if (i < this->InstantSpawnLiveRule.size())
				this->InstantSpawnLiveRule.erase(this->InstantSpawnLiveRule.begin() + i);
		}
	}

	for (auto const& pAttachment : this->ChildAttachments)
		pAttachment->InvalidatePointer(ptr);

	for (auto& [key, vec] : this->DormantAttachments)
		for (auto const& pAttachment : vec)
			pAttachment->InvalidatePointer(ptr);
}

template <typename T>
void TechnoExt::ExtData::Serialize(T& Stm)
{
	// ChildAttachments / DormantAttachments are still NOT streamed directly --
	// AttachmentClass::Serialize processes its Data pointer, which points into the
	// TYPE's AttachmentData vector and cannot be swizzled or rebuilt by the stream.
	// Instead SavedChildren/SavedRespawn below carry the restorable part (which
	// child is in which slot) and the slots themselves are rebuilt from the type.
	//
	// AttachmentClass::Serialize (which exists, and is never called) does
	// `.Process(this->Data)`. `Data` is an AttachmentDataEntry* pointing INTO the
	// TYPE's AttachmentData vector. That is not a game object, so the swizzler
	// cannot repair it, and the vector is rebuilt from INI on load -- the restored
	// pointer would dangle into freed storage and be dereferenced on the very next
	// AI tick.
	//
	// The fix is to serialize the slot INDEX and re-derive Data from the rebuilt
	// type vector after load, not to add ChildAttachments to this chain. See
	// docs/ROADMAP.md "attachment save/load".
	Stm
		.Process(this->AltOccupation)
		.Process(this->DeactivationReasons)
		.Process(this->InstantSpawnLastFired)
		.Process(this->InstantSpawnCyclePos)
		.Process(this->SavedChildren)
		.Process(this->SavedRespawn)
		.Process(this->AttachmentsPendingRestore)
		.Process(this->GunnerBaseType)
		.Process(this->GunnerLastChange)
		.Process(this->InstantSpawnLive)
		.Process(this->InstantSpawnLiveRule)
		.Process(this->AttachmentMoveOffset)
		.Process(this->LastVeterancy)
		.Process(this->LastVeterancyValid)
		;
}

void TechnoExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<TechnoClass>::LoadFromStream(Stm);
	this->Serialize(Stm);

	// Do not re-link here: the children read above are still being swizzled, and
	// the slot list may or may not exist yet. Defer to the first synced tick,
	// where everything is known to be present.
	this->AttachmentsPendingRestore = !this->SavedChildren.empty();

	// InstantSpawnLive was just read back. Register unconditionally; the next
	// pointer walk drops the ones that hold nothing.
	this->MarkPointerHolder();
}

void TechnoExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	TechnoExt::CaptureAttachmentsForSave(this->OwnerObject());

	Extension<TechnoClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Ported Phobos TechnoExt helpers the attachment runtime needs.
// ============================================================================

CoordStruct TechnoExt::GetFLHAbsoluteCoords(TechnoClass* pThis, CoordStruct pCoord, bool isOnTurret)
{
	// Public static over a raw TechnoClass*, dereferenced on line one. An
	// attachment with a null Parent reached it and read address 0
	// (HANDOFF-crash-20261030-203952). Guarding here makes every present and
	// future caller safe, including AttachmentClass::GetChildAnchor, which makes
	// the identical call.
	if (!pThis)
		return CoordStruct::Empty;

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
