// Attachment selection / mouse behavior (ported from PR #352).
//
// PassSelection: clicking an attached child selects its host instead (the child
// behaves as part of the parent). Wraps TechnoClass::Select via the Unit /
// Infantry / Building / Aircraft vtable slots (all currently 0x6FBFA0).
//
// TransparentToMouse: the child is invisible to mouse selection on its cell, so
// clicks pass through to whatever else is there. Two TacticalClass::SelectAt
// hooks. NOTE: 0x6DA3FF is ALSO hooked by Kratos (SelectAt_VirtualUnit) -- the
// two chain; ours returns "continue" for non-transparent technos, so it composes
// with Kratos's virtual-unit handling.
//
// Encyclopedia-checked: the four Select vtable slots and 0x6DA4FB are clean;
// only 0x6DA3FF collides (with Kratos, handled by chaining).
//
// Defaults are OFF for both so existing setups are unchanged (children stay
// individually selectable and mouse-solid unless the modder opts in).

#include <TechnoClass.h>
#include <CellClass.h>
#include <ObjectClass.h>
#include <DisplayClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

// ---- PassSelection ---------------------------------------------------------

// The real ObjectClass::Select. NOTE: YRpp declares ObjectClass::Select() as an
// R0 stub ({ return 0; }) -- unlike most virtuals it has NO JMP_THIS trampoline.
// So a qualified non-virtual call (pThis->TechnoClass::Select()) binds to that
// stub and silently no-ops: the unit never actually selects. We must invoke the
// game function at its address directly.
bool __fastcall TechnoClass_Select_Wrapper_TAExt(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	auto const pAtt = pExt ? pExt->ParentAttachment : nullptr;

	// PassSelection: select the host instead. Virtual call re-enters this wrapper
	// for the parent and cascades up; the base case (non-attached ancestor) lands
	// on the real Select below. The parent chain is acyclic by construction.
	if (pAtt && pAtt->ResolvePassSelection() && pAtt->Parent)
		return pAtt->Parent->Select();

	return reinterpret_cast<bool(__thiscall*)(TechnoClass*)>(0x6FBFA0)(pThis);
}

DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5DBC, TechnoClass_Select_Wrapper_TAExt); // UnitClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB1A4, TechnoClass_Select_Wrapper_TAExt); // InfantryClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E4008, TechnoClass_Select_Wrapper_TAExt); // BuildingClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E23F0, TechnoClass_Select_Wrapper_TAExt); // AircraftClass

// ---- TransparentToMouse ----------------------------------------------------

DEFINE_HOOK(0x6DA3FF, TacticalClass_SelectAt_TransparentToMouse_TAExt, 0x6)
{
	enum { SkipTechno = 0x6DA440, ContinueCheck = 0x0 };

	GET(TechnoClass*, pTechno, EAX);

	auto const pExt = TechnoExt::ExtMap.Find(pTechno);
	return (pExt && pExt->ParentAttachment && pExt->ParentAttachment->ResolveTransparentToMouse())
		? SkipTechno
		: ContinueCheck;
}

DEFINE_HOOK(0x6DA4FB, TacticalClass_SelectAt_TransparentToMouse_Occupier_TAExt, 0x6)
{
	GET(CellClass*, pCell, EAX);

	ObjectClass* pFound = nullptr;
	for (ObjectClass* pOcc = pCell->FirstObject; pOcc; pOcc = pOcc->NextObject)
	{
		if (auto const pT = abstract_cast<TechnoClass*>(pOcc))
		{
			auto const pExt = TechnoExt::ExtMap.Find(pT);
			if (pExt && pExt->ParentAttachment
				&& pExt->ParentAttachment->ResolveTransparentToMouse())
				continue; // skip transparent children, keep looking
		}

		pFound = pOcc;
		break;
	}

	R->EAX<ObjectClass*>(pFound);
	return 0x6DA501;
}

// ---- cursor pass-through ----------------------------------------------------
//
// The SelectAt hooks above cover CLICK-TO-SELECT. They do NOT cover the CURSOR:
// its shape comes from a different resolution entirely. The mouse handler calls
// DisplayClass::ProcessClickCoords (0x692300) at 0x4AACD4, and the ObjectClass*
// it writes back through `Target` is what the cursor reacts to. With an
// attachment sitting under the pointer, that object is the CHILD -- so the
// cursor showed "nothing useful here" and the player could not interact with
// whatever was underneath, even with TransparentToMouse=yes set.
//
// Fix: wrap the call and clear `Target` when it resolved to a mouse-transparent
// (or Intangible) attachment child. The cursor then falls through to the cell,
// and an actual click still resolves through the SelectAt hooks above, which
// already skip these children and pick the next real occupier.
//
// Both 0x4AACD4 (the call site) and 0x692300 (the function) are unhooked by
// Phobos/Antares/Kratos -- registry-checked. Wrapping the CALL rather than the
// function keeps every other caller of ProcessClickCoords untouched.
//
// Render-only/UI state; no synced logic is read or written here, so this is
// online-safe (see the determinism note in the encyclopedia entry for 0x692300).

static bool TAExt_HiddenFromCursor(ObjectClass* pObject)
{
	auto const pTechno = abstract_cast<TechnoClass*>(pObject);
	if (!pTechno)
		return false;

	auto const pExt = TechnoExt::ExtMap.Find(pTechno);
	auto const pAtt = pExt ? pExt->ParentAttachment : nullptr;
	if (!pAtt)
		return false;

	// Intangible implies mouse pass-through: a child the engine is not meant to
	// see in the world should not be catching the pointer either.
	return pAtt->ResolveTransparentToMouse() || pAtt->ResolveIntangible();
}

bool __fastcall DisplayClass_ProcessClickCoords_Wrapper_TAExt(
	DisplayClass* pThis, void*, Point2D* pSrc, CellStruct* pCellOut,
	CoordStruct* pCoordOut, ObjectClass** ppTarget, BYTE* a5, BYTE* a6)
{
	bool const result = reinterpret_cast<bool(__thiscall*)(
		DisplayClass*, Point2D*, CellStruct*, CoordStruct*, ObjectClass**, BYTE*, BYTE*)>(0x692300)
		(pThis, pSrc, pCellOut, pCoordOut, ppTarget, a5, a6);

	if (ppTarget && *ppTarget && TAExt_HiddenFromCursor(*ppTarget))
		*ppTarget = nullptr; // cursor falls through to the cell underneath

	return result;
}

DEFINE_FUNCTION_JUMP(CALL, 0x4AACD4, DisplayClass_ProcessClickCoords_Wrapper_TAExt);
