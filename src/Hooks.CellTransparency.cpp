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
#include <InfantryClass.h>
#include <AircraftClass.h>

#include <Utilities/Debug.h>

#include <Utilities/Macro.h>
#include <Helpers/Macro.h>

#include <Helpers/Cast.h>
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
// otherwise defer to the original engine function.
//
// vtable 0x7F5D60 -> SetOccupyBit (code 0x7441B0)
// vtable 0x7F5D64 -> ClearOccupyBit (code 0x744210)
// ============================================================================

void __fastcall UnitClass_SetOccupyBit_TAExt(UnitClass* pThis, void*, CoordStruct* pCrd)
{
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

// ---------------------------------------------------------------------------
// OccupiesCell for NON-vehicle children.
//
// The pair above is UnitClass-only -- which is why OccupiesCell=no appeared to do
// nothing for infantry / aircraft / building children. Upstream PR #352 has the
// same gap and says so outright:
//     "TODO ^ same for TA for non-UnitClass, not needed so cba for now"
//
// These vtable slots are the SAME virtual as the pair above -- ObjectClass::
// MarkAllOccupationBits / UnmarkAllOccupationBits (the PR calls them Set/Clear
// OccupyBit, following Ares). Each class has its own implementation:
//
//   slot = <class IsOnFloor slot> + 0xA0 (mark) / +0xA4 (unmark)
//     UnitClass      0x7F5D60/0x7F5D64 -> 0x7441B0 / 0x744210   (already handled)
//     InfantryClass  0x7EB148/0x7EB14C -> 0x5217C0 / 0x521850
//     BuildingClass  0x7F4A50/0x7F4A54 -> 0x5F60A0 / 0x5F6120
//     AircraftClass  0x7E3FAC/0x7E3FB0 -> 0x453D60 / 0x453DC0
//     Building (alt) 0x7E8D84/0x7E8D88 -> same as BuildingClass
//
// Offsets calibrated against the verified UnitClass pair, and every address here
// is unhooked by Phobos/Antares/Kratos (registry-checked).
//
// Skipping mark AND unmark together stays balanced: a child that never marked has
// nothing to clear, exactly like the UnitClass pair.
// ---------------------------------------------------------------------------

void __fastcall InfantryClass_MarkOccupation_TAExt(InfantryClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(InfantryClass*, CoordStruct*)>(0x5217C0)(pThis, pCrd);
}

void __fastcall InfantryClass_UnmarkOccupation_TAExt(InfantryClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(InfantryClass*, CoordStruct*)>(0x521850)(pThis, pCrd);
}

void __fastcall BuildingClass_MarkOccupation_TAExt(BuildingClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(BuildingClass*, CoordStruct*)>(0x5F60A0)(pThis, pCrd);
}

void __fastcall BuildingClass_UnmarkOccupation_TAExt(BuildingClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(BuildingClass*, CoordStruct*)>(0x5F6120)(pThis, pCrd);
}

void __fastcall AircraftClass_MarkOccupation_TAExt(AircraftClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(AircraftClass*, CoordStruct*)>(0x453D60)(pThis, pCrd);
}

void __fastcall AircraftClass_UnmarkOccupation_TAExt(AircraftClass* pThis, void*, CoordStruct* pCrd)
{
	if (TechnoExt::DoesntOccupyCellAsChild(pThis))
		return;
	reinterpret_cast<void(__thiscall*)(AircraftClass*, CoordStruct*)>(0x453DC0)(pThis, pCrd);
}

DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB148, InfantryClass_MarkOccupation_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB14C, InfantryClass_UnmarkOccupation_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F4A50, BuildingClass_MarkOccupation_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F4A54, BuildingClass_UnmarkOccupation_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E8D84, BuildingClass_MarkOccupation_TAExt);   // alt vtable
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E8D88, BuildingClass_UnmarkOccupation_TAExt); // alt vtable
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E3FAC, AircraftClass_MarkOccupation_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E3FB0, AircraftClass_UnmarkOccupation_TAExt);

// ---------------------------------------------------------------------------
// Intangible: keep the child out of the cell CONTENT list.
//
// OccupiesCell=no only clears the occupation FLAG. Cell content membership --
// being in CellClass::FirstObject's linked list -- is consulted independently by
// placement checks, occupier lookups and cursor picking, so a child can still
// "be in the way" with the flag clear. Intangible=yes removes it from that list
// as well, which is the absolute "blocks nothing" guarantee.
//
// CellClass::AddContent    0x47E8A0, arg1 = ObjectClass* Content (EBP once loaded)
//   hook at 0x47E8C2 (test ebp,ebp / je) -- the vanilla null check, which we must
//   re-express because size 0x8 consumes it. Skip target 0x47EA80 is the null
//   branch's epilogue: pop esi/ebp/ebx; add esp,0x14; ret 8.
// CellClass::RemoveContent 0x47EA90, arg1 = ObjectClass* Content (ESI once loaded)
//   hook at 0x47EA9B (test esi,esi / je), skip target 0x47EB8F.
//
// Both functions are unhooked by Phobos/Antares/Kratos (registry-checked).
// Add and Remove are skipped together so the pair stays balanced.
//
// EXPERIMENTAL: if the renderer sources drawable objects from cell contents, an
// intangible child may also become invisible -- see docs/TESTING.md 1c. Opt-in.
// ---------------------------------------------------------------------------

DEFINE_HOOK(0x47E8C2, CellClass_AddContent_Intangible_TAExt, 0x8)
{
	enum { DoNothing = 0x47EA80, Continue = 0x0 };

	GET(ObjectClass*, pContent, EBP);

	if (!pContent)
		return DoNothing; // restored vanilla null check

	if (auto const pTechno = abstract_cast<TechnoClass*>(pContent))
	{
		if (TechnoExt::IsIntangibleAsChild(pTechno))
			return DoNothing;
	}

	return Continue;
}

DEFINE_HOOK(0x47EA9B, CellClass_RemoveContent_Intangible_TAExt, 0x8)
{
	enum { DoNothing = 0x47EB8F, Continue = 0x0 };

	GET(ObjectClass*, pContent, ESI);

	if (!pContent)
		return DoNothing; // restored vanilla null check

	if (auto const pTechno = abstract_cast<TechnoClass*>(pContent))
	{
		// Never added as intangible, so there is nothing to remove.
		if (TechnoExt::IsIntangibleAsChild(pTechno))
			return DoNothing;
	}

	return Continue;
}

#endif // TAEXT_ENABLE_CELLTRANSPARENCY
