// I-a -- the in-place type conversion primitive.
//
// Swaps a live techno's TechnoType, so every stat (Strength, Speed, armor,
// weapons, art) follows at once. Substituting stats individually is not viable:
// they are read from dozens of contested sites across the exe.
//
// This is NOT speculative. Antares does the same thing in TechnoExt::UpdateType
// (src/Ext/Techno/Body.cpp) to drive its Convert.Deploy feature, and the repair
// sequence below is taken from that working implementation rather than derived
// from first principles. Where we differ from Antares it is because we own
// different state (attachments, InstantSpawn rule vectors) or because we
// deliberately do not touch theirs (AttachEffects, spotlights, TurretROT ext).
//
// SCOPE: Infantry, Unit and Aircraft only. Antares omits Building from its own
// switch and so do we -- a building's foundation, cell occupancy, base-node
// membership and power contribution are all computed at placement, so mutating
// BuildingClass::Type under a live structure is a different and much larger
// problem. Garrison "gunners" are a separate mechanic.
//
// DETERMINISM: no RNG, no wall clock. Every input is synced state, so all peers
// convert on the same frame with the same result.

#include "Body.h"

#include <InfantryClass.h>
#include <UnitClass.h>
#include <AircraftClass.h>
#include <HouseClass.h>
#include <TemporalClass.h>
#include <LocomotionClass.h>

#include <Locomotion/AttachmentLocomotionClass.h>

bool TechnoExt::UpdateType(TechnoClass* pThis, TechnoTypeClass* pToType,
	bool keepHealth, bool keepVeterancy)
{
	if (!pThis || !pToType)
		return false;

	// --- 1. Find the Type field. It lives on the CONCRETE class, not on
	// TechnoClass, so there are three different members and no common one. ---
	TechnoTypeClass** ppType = nullptr;
	auto wanted = AbstractType::None;

	switch (pThis->WhatAmI())
	{
	case AbstractType::Infantry:
		ppType = reinterpret_cast<TechnoTypeClass**>(&static_cast<InfantryClass*>(pThis)->Type);
		wanted = AbstractType::InfantryType;
		break;
	case AbstractType::Unit:
		ppType = reinterpret_cast<TechnoTypeClass**>(&static_cast<UnitClass*>(pThis)->Type);
		wanted = AbstractType::UnitType;
		break;
	case AbstractType::Aircraft:
		ppType = reinterpret_cast<TechnoTypeClass**>(&static_cast<AircraftClass*>(pThis)->Type);
		wanted = AbstractType::AircraftType;
		break;
	default:
		return false; // buildings and anything else: out of scope, see the header
	}

	// --- 2. A UnitType cannot become an InfantryType. ---
	if (pToType->WhatAmI() != wanted)
		return false;

	auto const pFromType = *ppType;
	if (pFromType == pToType)
		return true; // already there; not a failure

	// --- 3. Release temporal BEFORE the swap. Detaching without releasing
	// leaves the victim frozen with nothing left to warp it back in. ---
	if (auto const pTemporal = pThis->TemporalImUsing)
	{
		if (pTemporal->Target)
			pTemporal->LetGo();
	}

	// --- 4. Deregister from the owner around the swap, or house counts, the
	// tech tree and the score screen are all computed against a type this
	// object is no longer. ---
	auto const pOwner = pThis->Owner;
	bool const onField = !pThis->InLimbo;

	if (pOwner)
	{
		if (onField)
			pOwner->RegisterLoss(pThis, false);
		pOwner->RemoveTracking(pThis);
	}

	// --- 5. Health as a RATIO, captured before and restored after. An absolute
	// carry-over would leave a unit swapping to a tougher type near-dead, and one
	// swapping to a frailer type over-full. ---
	double const ratio = keepHealth && pFromType->Strength > 0
		? pThis->Health / static_cast<double>(pFromType->Strength)
		: 1.0;

	float const veterancy = pThis->Veterancy.Veterancy;

	// --- 6. THE SWAP ---
	*ppType = pToType;

	pThis->SetHealthPercentage(ratio);
	pThis->EstimatedHealth = pThis->Health;

	if (keepVeterancy)
		pThis->Veterancy.Veterancy = veterancy;
	else
		pThis->Veterancy.Veterancy = 0.0f;

	// --- 7. Re-register. ---
	if (pOwner)
	{
		pOwner->AddTracking(pThis);
		if (onField)
			pOwner->RegisterGain(pThis, true);
		pOwner->RecheckTechTree = true;
	}

	// --- 8. Ammo cannot exceed the new capacity. ---
	if (pToType->Ammo >= 0 && pToType->Ammo < pThis->Ammo)
		pThis->Ammo = pToType->Ammo;

	// --- 9. Turn rates come from the type. ---
	pThis->PrimaryFacing.SetROT(pToType->ROT);
	pThis->SecondaryFacing.SetROT(pToType->ROT);

	// The barrel keeps whatever angle the old type set, so a conversion between
	// types with different FireAngle leaves it pointing wrong until it next fires.
	if (pThis->WhatAmI() == AbstractType::Unit)
		pThis->BarrelFacing.SetCurrent(DirStruct(0x4000 - (pToType->FireAngle << 8)));

	// --- 10. Locomotor, if the type changed it. ---
	//
	// CRITICAL EXCEPTION, and one Antares has no reason to have: if this techno is
	// an attachment CHILD it is riding the attachment locomotor, which is what
	// pins it to its parent. Swapping in the new type's locomotor would cut it
	// loose and it would wander off as a free unit. Attachment children keep the
	// attachment locomotor across a conversion.
	if (auto const pFoot = abstract_cast<FootClass*>(pThis))
	{
		if (pToType->Locomotor != pFromType->Locomotor
			&& !TechnoExt::HasAttachmentLoco(pFoot))
		{
			LocomotionClass::ChangeLocomotorTo(pFoot, pToType->Locomotor);
		}
	}

	// --- 11. OUR state. Antares has none of this. ---

	// Attachment slots are keyed to the TYPE's AttachmentData, so the slot list
	// changes shape under the swap. This is the single easiest thing to forget.
	TechnoExt::HandleAttachmentConversion(pThis, pFromType, pToType);

	// Our per-instance InstantSpawn vectors are indexed by position in the
	// combined TechnoType + AttachmentType rule list. A type swap changes that
	// list, so a stale index would attribute one rule's cooldown, cycle position
	// or live-object record to a different rule. Clear them; they re-size
	// themselves on the next activation.
	if (auto const pExt = TechnoExt::ExtMap.Find(pThis))
	{
		pExt->InstantSpawnLastFired.clear();
		pExt->InstantSpawnCyclePos.clear();
		pExt->InstantSpawnLive.clear();
		pExt->InstantSpawnLiveRule.clear();
	}

	Debug::Log("[TAExt] converted %s -> %s\n", pFromType->ID, pToType->ID);
	return true;
}
