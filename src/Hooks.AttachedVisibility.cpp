// Per-viewer attachment visibility -- HiddenFrom= / VisibleTo=.
//
// WHY THIS IS NOT THE BLOCKED TRANSLUCENCY PROBLEM
// ------------------------------------------------
// J2 translucency needs to change the BLITTER FLAGS mid-draw, and those are only
// assembled at the instant Phobos overwrites (its tint seats are full-region
// replacements jumping 200-350 bytes). That remains blocked.
//
// This feature does not need to change HOW the object draws -- only WHETHER it
// draws at all. That is the DrawIt entry, and ObjectClass::DrawIt is virtual
// (YRpp/ObjectClass.h:140), so the vtable slot can be wrapped. Same pattern
// Hooks.AttachedRender.cpp already uses for GetYSort, and collision-free for the
// same reason: nobody hooks a vtable slot we are replacing wholesale.
//
// HOW THE SLOTS WERE FOUND -- calibration, not virtual-counting
// -------------------------------------------------------------
// Counting virtuals in YRpp to derive a slot index has produced a WRONG answer on
// this project before. So these were derived from a known-good anchor and then
// verified against the exe:
//
//   1. The GetYSort slots are already proven in game (Hooks.AttachedRender.cpp):
//      Aircraft 0x7E235C, Infantry 0x7EB110, Unit 0x7F5D28 -- all -> 0x5F6BD0.
//   2. Phobos hooks sit INSIDE each class's DrawIt: 0x4148F4 (Aircraft),
//      0x51933B (Infantry). Scanning each vtable for the slot whose target
//      CONTAINS its anchor gives one consistent delta: GetYSort + 0x5C.
//   3. Verified: every target is a real function prologue, and each anchor falls
//      inside it -- Aircraft 0x4144B0 (anchor +0x444), Infantry 0x518F90
//      (anchor +0x3AB), Unit 0x73CEC0.
//
// A first attempt landed on +0xD4 and looked plausible until the Aircraft slot
// pointed into FootClass rather than AircraftClass. The lesson is in the method:
// a candidate delta is only believable when it holds for EVERY class with an
// independent anchor.
//
// ⚠ BUILDINGS ARE NOT COVERED. The derived Building slot (0x7F4A74) points at a
// shared implementation rather than building-specific draw code, so it is not
// confidently DrawIt. Shipping three verified slots beats four with one guess --
// an unverified vtable replacement is exactly what produced the C0000005 at
// 0x7FA9A0 earlier in this project. Building children stay visible to everyone
// until that slot is confirmed.
//
// ⚠ RENDER-ONLY, AND THAT IS A HARD RULE. The answer depends on WHO IS WATCHING
// (HouseClass::CurrentPlayer), which differs per client by design. Nothing synced
// may ever consult it: targeting, collision, damage and AI must all continue to
// see the object normally, or the clients diverge. This file therefore only ever
// declines to DRAW.

#include <ObjectClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <AircraftClass.h>
#include <HouseClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <AttachmentParsers.h>
#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>

namespace
{
	// Should the LOCAL player be denied a view of this object?
	bool HiddenFromLocalPlayer(ObjectClass* pObject)
	{
		auto const pTechno = abstract_cast<TechnoClass*>(pObject);
		if (!pTechno)
			return false;

		auto const pExt = TechnoExt::ExtMap.Find(pTechno);
		auto const pSlot = pExt ? pExt->ParentAttachment : nullptr;
		if (!pSlot)
			return false; // not an attachment child

		int const hiddenFrom = pSlot->ResolveHiddenFrom();
		int const visibleTo = pSlot->ResolveVisibleTo();

		if (hiddenFrom == TAExtHouse_None && visibleTo == TAExtHouse_None)
			return false; // nothing configured: the common case, costs one compare

		// The observer. In a replay or an observer slot this can be null; when we
		// cannot tell who is watching, show the object rather than hide it -- a
		// missing attachment is a worse failure than an extra one.
		auto const pViewer = HouseClass::CurrentPlayer;
		if (!pViewer)
			return false;

		// Judge against the HOST's owner. A child can be mind-controlled or have
		// inherited a different owner, and "hidden from the enemy" should mean the
		// enemy of whoever fields the parent.
		auto const pOwnerTechno = pSlot->Parent ? pSlot->Parent : pTechno;
		auto const pOwner = pOwnerTechno->Owner;
		if (!pOwner)
			return false;

		// VisibleTo is the allow-list form: anyone not named is denied.
		if (visibleTo != TAExtHouse_None)
		{
			if (!TAExt_HouseRelationMatches(pOwner, pViewer, visibleTo))
				return true;
		}

		// HiddenFrom is the deny-list form, and it wins over VisibleTo so the two
		// can be combined to carve an exception out of a broad allow.
		if (hiddenFrom != TAExtHouse_None
			&& TAExt_HouseRelationMatches(pOwner, pViewer, hiddenFrom))
			return true;

		return false;
	}
}

// A vtable slot is invoked as __thiscall -- `this` in ECX, arguments on the
// stack. MSVC will not accept __thiscall on a free function, so each wrapper is
// __fastcall with a DUMMY EDX parameter. Getting this wrong pushes the arguments
// one slot out and destroys the stack; see the encyclopedia's Weapon-Selection
// page for what that looks like when it happens.
#define TAEXT_DRAWIT_WRAPPER(cls, original)                                      \
	void __fastcall cls##_DrawIt_Visibility_TAExt(ObjectClass* pThis, void*,     \
		Point2D* pLocation, RectangleStruct* pBounds)                            \
	{                                                                            \
		if (HiddenFromLocalPlayer(pThis))                                        \
			return; /* render-only: the object still exists in every other way */\
                                                                                 \
		reinterpret_cast<void(__thiscall*)(ObjectClass*, Point2D*,               \
			RectangleStruct*)>(original)(pThis, pLocation, pBounds);             \
	}

TAEXT_DRAWIT_WRAPPER(UnitClass,     0x73CEC0)
TAEXT_DRAWIT_WRAPPER(InfantryClass, 0x518F90)
TAEXT_DRAWIT_WRAPPER(AircraftClass, 0x4144B0)

DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5D84, UnitClass_DrawIt_Visibility_TAExt);     // UnitClass::DrawIt
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB16C, InfantryClass_DrawIt_Visibility_TAExt); // InfantryClass::DrawIt
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E23B8, AircraftClass_DrawIt_Visibility_TAExt); // AircraftClass::DrawIt
