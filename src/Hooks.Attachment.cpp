// Core attachment behaviour hooks (standalone port of the essential subset of
// Phobos PR #352's Hooks.TechnoAttachment.cpp + the per-frame drivers it adds
// to Body.Update/Building/Foot). These are what make attachments actually
// spawn, follow their parent each frame, and clean up on death/limbo.
//
// All hooks return 0 (pass-through, Syringe-chainable) except RemoveThis, which
// re-expresses stolen code. Addresses/registers verified against the branch.
//
// Deferred to a follow-up pass: the ~35 edge-behaviour hooks (cell occupation,
// scatter, selection transparency, command inheritance, chrono-warp, firing
// rotation lock). The feature is functional without them.

#include <TechnoClass.h>
#include <FootClass.h>
#include <BuildingClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>

// ============================================================================
// Init — create a parent's child attachments when the techno initialises.
// TechnoClass::Init, ESI = this. (Phobos also hooks here; Syringe chains.)
// ============================================================================
DEFINE_HOOK(0x6F42F7, TechnoClass_Init_InitAttachments, 0x2)
{
	GET(TechnoClass*, pThis, ESI);

	if (!pThis->GetTechnoType()) // critical sanity check during save/load
		return 0;

	TechnoExt::InitializeAttachments(pThis);

	return 0;
}

// ============================================================================
// Per-frame AI tick — drive each child attachment (position, facing, state).
// Foot parents: FootClass update after locomotor process, ESI = this.
// Building parents: BuildingClass::AI, ESI = this.
// ============================================================================
DEFINE_HOOK(0x4DA8A0, FootClass_Update_TickAttachments, 0x6)
{
	GET(FootClass* const, pThis, ESI);

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt)
		return 0;
	for (auto const& pAttachment : pExt->ChildAttachments)
		pAttachment->AI();

	return 0;
}

DEFINE_HOOK(0x43FE69, BuildingClass_AI_TickAttachments, 0xA)
{
	GET(BuildingClass*, pThis, ESI);

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt)
		return 0;
	for (auto const& pAttachment : pExt->ChildAttachments)
		pAttachment->AI();

	return 0;
}

// ============================================================================
// Destruction — a destroyed parent destroys/notifies its children, and a
// destroyed child notifies its parent.
// ============================================================================
DEFINE_HOOK(0x707CB3, TechnoClass_KillCargo_HandleAttachments, 0x6)
{
	GET(TechnoClass*, pThis, EBX);
	GET_STACK(TechnoClass*, pSource, STACK_OFFSET(0x4, 0x4));

	TechnoExt::DestroyAttachments(pThis, pSource);

	return 0;
}

DEFINE_HOOK(0x5F6609, ObjectClass_RemoveThis_NotifyParent, 0x9)
{
	GET(TechnoClass*, pThis, ESI);

	pThis->KillPassengers(nullptr);  // restored code
	TechnoExt::HandleDestructionAsChild(pThis);

	return 0x5F6612;
}

DEFINE_HOOK(0x4DEBB4, FootClass_OnDestroyed_NotifyParent, 0x8)
{
	GET(FootClass*, pThis, ESI);

	TechnoExt::HandleDestructionAsChild(pThis);

	return 0;
}

// ============================================================================
// Limbo / Unlimbo — keep children in sync with the parent's limbo state.
// ============================================================================
DEFINE_HOOK(0x6F6F20, TechnoClass_Unlimbo_UnlimboAttachments, 0x6)
{
	GET(TechnoClass*, pThis, ESI);

	TechnoExt::UnlimboAttachments(pThis);

	return 0;
}

DEFINE_HOOK(0x6F6B1C, TechnoClass_Limbo_LimboAttachments, 0x6)
{
	GET(TechnoClass*, pThis, ESI);

	TechnoExt::LimboAttachments(pThis);

	return 0;
}
