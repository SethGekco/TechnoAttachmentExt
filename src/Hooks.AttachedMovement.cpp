// Stop attached units from trying to move/path.
//
// A child attached via the AttachmentLocomotionClass keeps its normal unit AI,
// including movement missions. With a do-nothing locomotor it can never reach a
// destination, so it re-attempts pathing every frame — a severe per-frame spin
// (the "freeze" when an active vehicle child sits on a building's occupied
// foundation). PR #352 suppresses this inside Phobos's own DisallowMoving hooks;
// as a standalone DLL we chain our own hooks at the SAME addresses. Base Phobos
// already hooks them for its CannotMove feature and returns "continue" (0) for
// an attached-loco unit (it isn't CannotMove), so our non-zero return wins and
// the attachment handling takes effect. Non-attached units are untouched.

#include <UnitClass.h>
#include <TechnoClass.h>
#include <ScenarioClass.h>
#include <MissionClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <Ext/Techno/Body.h>
#include <TAExtDiag.h>

// UnitClass::Mission_Move — force an attached unit idle instead of moving.
DEFINE_HOOK(0x740A93, TechnoAttachmentExt_Mission_Move_ForceIdle, 0x6)
{
	enum { ReturnTrue = 0x740AFD };

	GET(UnitClass*, pThis, ESI);

	TAEXT_DIAG_COUNT("Mission_Move");

	if (TechnoExt::HasAttachmentLoco(pThis))
	{
		pThis->EnterIdleMode(false, true);
		return ReturnTrue;
	}

	return 0;
}

// UnitClass::AssignDestination — refuse a destination for an attached unit.
DEFINE_HOOK(0x741AA7, TechnoAttachmentExt_AssignDestination_Clear, 0x6)
{
	enum { ClearNavComsAndReturn = 0x743173 };

	GET(UnitClass*, pThis, EBP);

	TAEXT_DIAG_COUNT("AssignDestination");

	return TechnoExt::HasAttachmentLoco(pThis) ? ClearNavComsAndReturn : 0;
}

// UnitClass::Scatter — an attached unit doesn't scatter (it rides its parent).
DEFINE_HOOK(0x743B4B, TechnoAttachmentExt_Scatter_Release, 0x6)
{
	enum { ReleaseReturn = 0x74408E };

	GET(UnitClass*, pThis, EBP);

	TAEXT_DIAG_COUNT("Scatter");

	return TechnoExt::HasAttachmentLoco(pThis) ? ReleaseReturn : 0;
}

// ============================================================================
// AI mission / target-approach suppression.
//
// The building-host + active-vehicle-child freeze (found via the freeze
// watchdog: a pathfinding recursion through building/cell code) is triggered by
// the attached unit's own AI trying to path toward an auto-acquired target
// (AreaGuard -> ApproachTarget -> pathfinder from a cell it can't legally
// occupy). The direct-move hooks above don't cover the AI approach path, so we
// port PR #352's mission/approach suppression here, gated on the attachment
// locomotor (base Phobos gates the same addresses on its CannotMove feature and
// returns 0 for our units, so our non-zero return wins).
// ============================================================================

// UnitClass::Mission_Hunt — an attached unit can't hunt (would path); guard instead.
DEFINE_HOOK(0x73EFC4, TechnoAttachmentExt_Mission_Hunt_NoMove, 0x6)
{
	GET(UnitClass*, pThis, ESI);

	if (TechnoExt::HasAttachmentLoco(pThis))
	{
		pThis->QueueMission(Mission::Guard, false);
		pThis->NextMission();
		R->EAX(pThis->Mission_Guard());
		return 0x73F091;
	}

	return 0;
}

// UnitClass::Mission_AreaGuard — acquire targets in place, never path to them.
DEFINE_HOOK(0x744103, TechnoAttachmentExt_Mission_AreaGuard_NoMove, 0x6)
{
	GET(UnitClass*, pThis, ESI);

	if (TechnoExt::HasAttachmentLoco(pThis))
	{
		if (pThis->CanPassiveAcquireTargets() && pThis->TargetingTimer.Completed())
			pThis->TargetAndEstimateDamage(pThis->Location, ThreatType::Range);

		int delay = 1;

		if (!pThis->Target)
		{
			pThis->UpdateIdleAction();
			auto const control = &MissionControlClass::Array[(int)Mission::Area_Guard];
			delay = static_cast<int>(control->Rate * 900) + ScenarioClass::Instance->Random(1, 5);
		}

		R->EAX(delay);
		return 0x744173;
	}

	return 0;
}

// UnitClass::GetFireError — report ILLEGAL instead of RANGE, so nothing tries to
// close the distance (which would path) for an attached unit.
DEFINE_HOOK(0x74132B, TechnoAttachmentExt_GetFireError_NoRange, 0x7)
{
	GET(UnitClass*, pThis, ESI);
	GET(const FireError, result, EAX);

	if (result == FireError::RANGE && TechnoExt::HasAttachmentLoco(pThis))
		R->EAX(FireError::ILLEGAL);

	return 0;
}

namespace TAExt_ApproachTargetTemp
{
	int WeaponIndex = -1;
}

// UnitClass::ApproachTarget — the direct pathfinder call. An attached unit never
// approaches: if not already in range, drop the target; otherwise just fire.
DEFINE_HOOK(0x7414E0, TechnoAttachmentExt_ApproachTarget_NoMove, 0xA)
{
	GET(UnitClass*, pThis, ECX);

	int weaponIndex = -1;

	if (TechnoExt::HasAttachmentLoco(pThis))
	{
		const auto pTarget = pThis->Target;
		weaponIndex = pThis->SelectWeapon(pTarget);

		if (!pThis->IsCloseEnough(pTarget, weaponIndex))
		{
			pThis->SetTarget(nullptr);
			return 0x741690;
		}
	}

	TAExt_ApproachTargetTemp::WeaponIndex = weaponIndex;
	return 0;
}

DEFINE_HOOK(0x7415A9, TechnoAttachmentExt_ApproachTarget_SetWeaponIndex, 0x6)
{
	if (TAExt_ApproachTargetTemp::WeaponIndex != -1)
	{
		GET(UnitClass*, pThis, ESI);

		R->EDI(VTable::Get(pThis));
		R->EAX(TAExt_ApproachTargetTemp::WeaponIndex);
		TAExt_ApproachTargetTemp::WeaponIndex = -1;

		return 0x7415BA;
	}

	return 0;
}

// TechnoClass::CanAutoTargetObject — use the (range-suppressed) fire error.
DEFINE_HOOK(0x6F7CE2, TechnoAttachmentExt_CanAutoTargetObject_NoMove, 0x6)
{
	GET(TechnoClass* const, pThis, EDI);
	GET(AbstractClass* const, pTarget, ESI);
	GET(const int, weaponIndex, EBX);

	if (const auto pUnit = abstract_cast<UnitClass*, true>(pThis))
	{
		if (TechnoExt::HasAttachmentLoco(pUnit))
		{
			R->EAX(pUnit->GetFireError(pTarget, weaponIndex, true));
			return 0x6F7CEE;
		}
	}

	return 0;
}

// TechnoClass::ShouldRetaliate — likewise.
DEFINE_HOOK(0x7088E3, TechnoAttachmentExt_ShouldRetaliate_NoMove, 0x6)
{
	GET(TechnoClass* const, pThis, EDI);
	GET(AbstractClass* const, pTarget, EBP);
	GET(const int, weaponIndex, EBX);

	if (const auto pUnit = abstract_cast<UnitClass*, true>(pThis))
	{
		if (TechnoExt::HasAttachmentLoco(pUnit))
		{
			R->Stack(STACK_OFFSET(0x18, 0x4), weaponIndex);
			R->EAX(pUnit->GetFireError(pTarget, weaponIndex, true));
			return 0x7088F3;
		}
	}

	return 0;
}
