// Inherit-status hooks (ported from PR #352).
//
// InheritHeightStatus: an attached child reports its HOST's height/layer state
// (on-floor / in-air / surfaced) instead of its own, so e.g. a child of an
// airborne host is treated as airborne too. Wraps ObjectClass::IsOnFloor
// (0x5F6B60), IsInAir (0x5F6B90) and IsSurfaced (0x5F6C10) via the per-class
// vtable slots. Non-attached objects (or InheritHeightStatus=no) fall through to
// the original, called non-virtually to avoid self-recursion.
//
// Encyclopedia-checked: all fifteen vtable slots are collision-free.

#include <ObjectClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

bool __fastcall TechnoClass_OnGround_TAExt(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return (pExt && pExt->ParentAttachment
			&& pExt->ParentAttachment->GetType()->InheritHeightStatus && pExt->ParentAttachment->Parent)
		? pExt->ParentAttachment->Parent->IsOnFloor()
		: pThis->ObjectClass::IsOnFloor();
}

bool __fastcall TechnoClass_InAir_TAExt(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return (pExt && pExt->ParentAttachment
			&& pExt->ParentAttachment->GetType()->InheritHeightStatus && pExt->ParentAttachment->Parent)
		? pExt->ParentAttachment->Parent->IsInAir()
		: pThis->ObjectClass::IsInAir();
}

bool __fastcall TechnoClass_IsSurfaced_TAExt(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return (pExt && pExt->ParentAttachment
			&& pExt->ParentAttachment->GetType()->InheritHeightStatus && pExt->ParentAttachment->Parent)
		? pExt->ParentAttachment->Parent->IsSurfaced()
		: pThis->ObjectClass::IsSurfaced();
}

// BuildingClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F49B0, TechnoClass_OnGround_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F49B4, TechnoClass_InAir_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F49DC, TechnoClass_IsSurfaced_TAExt);

// AircraftClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E3F0C, TechnoClass_OnGround_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E3F10, TechnoClass_InAir_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E3F38, TechnoClass_IsSurfaced_TAExt);

// BuildingClass (alt vtable)
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E8CE4, TechnoClass_OnGround_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E8CE8, TechnoClass_InAir_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E8D10, TechnoClass_IsSurfaced_TAExt);

// UnitClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5CC0, TechnoClass_OnGround_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5CC4, TechnoClass_InAir_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5CEC, TechnoClass_IsSurfaced_TAExt);

// InfantryClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB0A8, TechnoClass_OnGround_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB0AC, TechnoClass_InAir_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB0D4, TechnoClass_IsSurfaced_TAExt);
