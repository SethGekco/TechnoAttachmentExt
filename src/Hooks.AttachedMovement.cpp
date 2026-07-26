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

#include <Utilities/Macro.h>

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
