// Cell-transparency (ported from PR #352).
//
// Makes attached children invisible to a building's cell-occupier lookups.
// Buildings call CellClass::FindTechnoNearestTo (aka "CellTechno") to find what
// sits on a cell — e.g. to clear its bib or validate placement. When an active
// vehicle child is attached to a building it sits on that cell but can't be
// relocated (do-nothing locomotor), so the building's clear-cell logic spins
// forever ("active vehicle child on a building host freezes the game"). Here we
// wrap those specific call sites to set a mode that excludes attached children
// from the result, so the building sees the cell as clear and its loop ends.
//
// The spin is entirely game-side (confirmed via diagnostics — it never touches
// our locomotor/AI/movement hooks), which is why this cell-lookup exclusion is
// the fix rather than anything on the attachment's own update path.
//
// All six addresses verified clear of base-Phobos hooks (no coexistence issue).

#include <CellClass.h>
#include <BuildingClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>

#include <Utilities/Debug.h>

#include <Utilities/Macro.h>
#include <Helpers/Macro.h>

#include <Ext/Techno/Body.h>

// BISECT TOGGLE: set to 0 to compile out the entire cell-transparency system.
// Used to determine whether these hooks are responsible for the load-time close.
#ifndef TAEXT_ENABLE_CELLTRANSPARENCY
#define TAEXT_ENABLE_CELLTRANSPARENCY 1
#endif

#if TAEXT_ENABLE_CELLTRANSPARENCY

enum class CellTechnoMode
{
	NoAttachments,
	NoVirtualOrRelatives,
	NoVirtual,
	NoRelatives,
	All,

	DefaultBehavior = All,
};

namespace TechnoAttachmentTemp
{
	CellTechnoMode currentMode = CellTechnoMode::DefaultBehavior;
}

// Wrapper installed in place of a CALL to FindTechnoNearestTo: set the exclusion
// mode, run the real lookup (which hits the hook below), then restore the mode.
#define DEFINE_CELLTECHNO_WRAPPER(mode)                                                                 \
	TechnoClass* __fastcall CellTechno_##mode(CellClass* pThis, void*, Point2D* a2, bool check_alt,     \
		TechnoClass* techno)                                                                            \
	{                                                                                                   \
		TechnoAttachmentTemp::currentMode = CellTechnoMode::mode;                                       \
		auto const retval = pThis->FindTechnoNearestTo(*a2, check_alt, techno);                         \
		TechnoAttachmentTemp::currentMode = CellTechnoMode::DefaultBehavior;                            \
		return retval;                                                                                  \
	}

DEFINE_CELLTECHNO_WRAPPER(NoVirtual);
DEFINE_CELLTECHNO_WRAPPER(NoVirtualOrRelatives);

#undef DEFINE_CELLTECHNO_WRAPPER

// Inside FindTechnoNearestTo: skip the current occupier when the active mode
// says attached children should be ignored.
DEFINE_HOOK(0x47C432, CellClass_CellTechno_HandleAttachments_TAExt, 0x0)
{
	enum { Continue = 0x47C437, IgnoreOccupier = 0x47C4A7 };

	GET(TechnoClass*, pOccupier, ESI);
	GET_BASE(TechnoClass*, pSelf, 0x10);

	using namespace TechnoAttachmentTemp;
	const bool noAttachments =
		currentMode == CellTechnoMode::NoAttachments;
	const bool noVirtual =
		currentMode == CellTechnoMode::NoVirtual ||
		currentMode == CellTechnoMode::NoVirtualOrRelatives;
	const bool noRelatives =
		currentMode == CellTechnoMode::NoRelatives ||
		currentMode == CellTechnoMode::NoVirtualOrRelatives;

	if (pOccupier == pSelf // restored code
		|| noAttachments && TechnoExt::IsAttached(pOccupier)
		|| noVirtual && TechnoExt::DoesntOccupyCellAsChild(pOccupier)
		|| noRelatives && TechnoExt::IsChildOf(pOccupier, (TechnoClass*)pSelf))
	{
		return IgnoreOccupier;
	}

	return Continue;
}

// Building placement occupation checks: ignore virtual (non-occupying) children.
DEFINE_FUNCTION_JUMP(CALL, 0x47C805, CellTechno_NoVirtual);
DEFINE_FUNCTION_JUMP(CALL, 0x47C738, CellTechno_NoVirtual);

// Building bib clear: ignore attached children / relatives sitting on the bib.
DEFINE_FUNCTION_JUMP(CALL, 0x4495F2, CellTechno_NoVirtualOrRelatives);
DEFINE_FUNCTION_JUMP(CALL, 0x44964E, CellTechno_NoVirtualOrRelatives);

DEFINE_HOOK(0x4495F7, BuildingClass_ClearFactoryBib_SkipCreatedUnitAttachments_TAExt, 0x0)
{
	enum { BibClear = 0x44969B, NotClear = 0x4495FF };

	GET(TechnoClass*, pBibTechno, EAX);

	if (!pBibTechno)
		return BibClear;

	GET(BuildingClass*, pThis, ESI);

	TechnoClass* pBuiltTechno = pThis->GetNthLink(0);
	if (TechnoExt::IsChildOf(pBibTechno, pBuiltTechno))
		return BibClear;

	return NotClear;
}

// ============================================================================
// Occupation-flag skip for attached children.
//
// The CellTechno wrappers above only hide children from techno *lookups*. The
// freeze (building host + active vehicle child) is driven by the cell's
// occupation *flag* (0x20 on OccupationFlags/AltOccupationFlags): the child
// sets it via UnitClass::SetOccupyBit, then the building's clear-cell logic
// spins trying to relocate an occupier that can't move (do-nothing loco).
//
// Skip setting/clearing the flag for a child that DoesntOccupyCellAsChild;
// otherwise defer to the original engine function. A one-time log confirms the
// wrapper actually runs (Ares is known to override these vtable slots).
//
// vtable 0x7F5D60 -> SetOccupyBit (code 0x7441B0)
// vtable 0x7F5D64 -> ClearOccupyBit (code 0x744210)
// ============================================================================

static bool TAExt_LoggedOccupyWrap = false;

void __fastcall UnitClass_SetOccupyBit_TAExt(UnitClass* pThis, void*, CoordStruct* pCrd)
{
	if (!TAExt_LoggedOccupyWrap)
	{
		TAExt_LoggedOccupyWrap = true;
		Debug::Log("[TAExt] SetOccupyBit wrapper is live (Ares did not override it)\n");
	}

	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return; // attached child: don't mark the cell occupied

	reinterpret_cast<void(__thiscall*)(UnitClass*, CoordStruct*)>(0x7441B0)(pThis, pCrd);
}

void __fastcall UnitClass_ClearOccupyBit_TAExt(UnitClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return; // never set the bit as a child, so nothing to clear

	reinterpret_cast<void(__thiscall*)(UnitClass*, CoordStruct*)>(0x744210)(pThis, pCrd);
}

DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5D60, UnitClass_SetOccupyBit_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5D64, UnitClass_ClearOccupyBit_TAExt);

#endif // TAEXT_ENABLE_CELLTRANSPARENCY
