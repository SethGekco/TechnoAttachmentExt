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
#include <EventClass.h>

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

// ============================================================================
// InheritCommands -- stop / deploy propagation (partial).
//
// When the host receives a Stop or Deploy command, propagate it to children
// whose AttachmentType opts in (InheritCommands.StopCommand / .DeployCommand).
// Done by queuing the child's ClickedEvent (Idle / Deploy), which goes through
// the synced event queue -> online-safe.
//
// NOTE: the PR's *general* InheritCommands (move/attack-click inheritance)
// reimplements DisplayClass::ActiveClickWith at 0x4AE7B3 (full-function
// replacement). That is NOT portable to a standalone: base Phobos AND Kratos
// both hook 0x4AE95E (DisallowBuildingNonAttackPlanning) INSIDE that function,
// and 0x4AE7B3 also collides with Phobos PR #1993 (Distribution Mode). A
// full replacement would bypass those release hooks. Move-click inheritance is
// also moot for attached children (they can't move), so only stop/deploy is
// ported here.
// ============================================================================

namespace TAExtCommands
{
	bool stopPressed = false;
	bool deployPressed = false;
	TechnoClass* pParent = nullptr;
}

DEFINE_HOOK(0x730EA0, StopCommand_Context_Set_TAExt, 0x5)
{
	TAExtCommands::stopPressed = true;
	return 0;
}

DEFINE_HOOK(0x730AF0, DeployCommand_Context_Set_TAExt, 0x8)
{
	TAExtCommands::deployPressed = true;
	return 0;
}

DEFINE_HOOK(0x730F1C, StopCommand_Context_Unset_TAExt, 0x5)
{
	TAExtCommands::stopPressed = false;
	return 0;
}

DEFINE_HOOK(0x730D55, DeployCommand_Context_Unset_TAExt, 0x7)
{
	TAExtCommands::deployPressed = false;
	return 0;
}

DEFINE_HOOK(0x6FFE00, TechnoClass_ClickedEvent_Context_Set_TAExt, 0x5)
{
	TAExtCommands::pParent = R->ECX<TechnoClass*>();
	return 0;
}

DEFINE_HOOK_AGAIN(0x6FFEB1, TechnoClass_ClickedEvent_HandleChildren_TAExt, 0x6)
DEFINE_HOOK(0x6FFE4F, TechnoClass_ClickedEvent_HandleChildren_TAExt, 0x6)
{
	if ((TAExtCommands::stopPressed || TAExtCommands::deployPressed) && TAExtCommands::pParent)
	{
		if (auto const pExt = TechnoExt::ExtMap.Find(TAExtCommands::pParent))
		{
			for (auto const& pAttachment : pExt->ChildAttachments)
			{
				if (!pAttachment->Child)
					continue;

				if (TAExtCommands::stopPressed && pAttachment->GetType()->InheritCommands_StopCommand)
					pAttachment->Child->ClickedEvent(EventType::Idle);

				if (TAExtCommands::deployPressed && pAttachment->GetType()->InheritCommands_DeployCommand)
					pAttachment->Child->ClickedEvent(EventType::Deploy);
			}
		}
	}

	return 0;
}
