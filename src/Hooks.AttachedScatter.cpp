// Scatter/Incoming handling for attached children (ported from PR #352).
//
// Root of the building-host + active-vehicle-child freeze (found via the freeze
// watchdog: the game spun in the Cell passability / scatter path): when a cell
// occupied by an attached child is "wanted", the engine repeatedly tells the
// child to scatter. An attached child rides a do-nothing locomotor and can't
// move, so the request never succeeds and the frame spins forever.
//
// TechnoClass::CanScatter returning false for any attached child is the fix for
// the common OccupiesCell=false case (the engine simply stops asking). The
// CellClass::Incoming redirect additionally handles OccupiesCell=true children
// by scattering the whole stack (top-level parent) instead.

#include <CellClass.h>
#include <TechnoClass.h>
#include <InfantryClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

// TechnoClass::CanScatter (0x6F3280) — ECX/ESI = this at 0x6F3283.
// An attached child can't scatter; return false so nothing keeps asking.
DEFINE_HOOK(0x6F3283, TechnoClass_CanScatter_CheckIfAttached_TAExt, 0x8)
{
	enum { ReturnFalse = 0x6F32C5, ContinueCheck = 0x0 };

	GET(TechnoClass*, pThis, ECX);

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return (pExt && pExt->ParentAttachment) ? ReturnFalse : ContinueCheck;
}

// CellClass::Incoming — a cell-occupying attached child (OccupiesCell=true)
// should scatter the whole stack, not itself.
DEFINE_HOOK(0x4817A8, CellClass_Incoming_CheckIfTechnoOccupies_TAExt, 0x6)
{
	enum { ConditionIsTrue = 0x4817C3, ContinueCheck = 0x0 };

	GET(TechnoClass*, pTechno, ESI);

	auto const pExt = TechnoExt::ExtMap.Find(pTechno);
	return (pExt && pExt->ParentAttachment && pExt->ParentAttachment->ResolveOccupiesCell())
		? ConditionIsTrue
		: ContinueCheck;
}

DEFINE_HOOK(0x4817C3, CellClass_Incoming_HandleScatterWithAttachments_TAExt, 0x0)
{
	GET(TechnoClass*, pTechno, ESI);

	GET(CoordStruct*, pThreatCoord, EBP);
	GET(bool, isForced, EBX);
	GET_STACK(bool, isNoKidding, STACK_OFFSET(0x2C, 0xC));

	// The check hook above already confirmed this occupies the cell as a child.
	TechnoExt::GetTopLevelParent(pTechno)->Scatter(*pThreatCoord, isForced, isNoKidding);

	return 0x4817D9;
}

// InfantryClass::Scatter (0x51D0DD) — an attached-loco infantry child can't scatter.
DEFINE_HOOK(0x51D0DD, InfantryClass_Scatter_CheckAttachments_TAExt, 0x6)
{
	enum { Bail = 0x51D6E6, Continue = 0x0 };

	GET(InfantryClass*, pThis, ESI);

	return TechnoExt::HasAttachmentLoco(pThis) ? Bail : Continue;
}
