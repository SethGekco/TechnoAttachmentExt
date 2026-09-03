// G1 -- ammo CAPACITY from attachments.
//
// [AttachmentType] Ammo.Parent=N  (per-slot: AttachmentN.Ammo.Parent=)
// While the attachment's child is active, the HOST's maximum ammo is raised by N.
// Bonuses from several active slots sum.
//
// WHY THIS IS A HOOK AND NOT A FIELD WRITE
// ----------------------------------------
// `TechnoTypeClass::Ammo` (offset 0x684) is the capacity, and it lives on the
// TYPE -- one value shared by every unit of that type. Writing it to give ONE
// tank more ammo would raise it for every tank in the game. So the capacity has
// to be substituted at the point the engine READS it, per instance.
//
// SCOPE -- deliberately targeted, not exhaustive
// ---------------------------------------------
// A census of the exe finds ~42 sites reading [type+0x684]. Hooking all of them
// would be enormously invasive and several sit inside functions Antares/Ares have
// already rewritten (they ship a whole Hooks.Ammo.cpp: 0x6FB05B reload amount,
// 0x6FCA0D CanFire, 0x6FF656 Fire, plus per-class reload updates).
//
// Instead we hook the two reads that actually DEFINE capacity -- the reload path:
//   0x6FB01C  TechnoClass::Reload      "is my ammo below max? then reload"
//   0x6FB08E  the ammo-state helper    "am I full?"  (called from Reload)
// Raising the value at these two makes a unit genuinely reload past its base
// capacity and only count as full at base+bonus, which is the entire gameplay
// effect. Both are UNHOOKED by Phobos/Antares/Kratos (registry-checked), and
// neither is one of the instructions Antares rewrote.
//
// KNOWN APPROXIMATION: other readers of [type+0x684] still see the BASE value.
// The visible one is the ammo pip display, which is drawn from the type and so
// will not show the extra rounds. Firing, reloading and "out of ammo" all behave
// correctly. Documented in docs/TAGS.md rather than silently accepted.
//
// DETERMINISM: the bonus is summed from synced attachment state (active children
// only), integer maths, no RNG or wall-clock -> online-safe.

#include <TechnoClass.h>
#include <TechnoTypeClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>

namespace
{
	// Substitute the effective capacity for a specific host. Leaves the vanilla
	// value alone when there is no bonus, and never touches -1 (unlimited ammo).
	int EffectiveAmmoCapacity(TechnoClass* pThis, TechnoTypeClass* pType)
	{
		int const base = pType ? pType->Ammo : -1;
		if (base < 0 || !pThis)
			return base; // -1 = unlimited; leave it exactly as-is

		int const bonus = TechnoExt::GetAmmoCapacityBonus(pThis);
		if (bonus <= 0)
			return base;

		return base + bonus;
	}
}

// BOTH hooks below MUST return an explicit address, never 0.
//
// They REPLACE the read `mov eax,[eax+0x684]`: on entry EAX is the
// TechnoTypeClass*, on exit EAX must be the capacity VALUE. Returning 0 makes
// Syringe's stub run the stolen instruction after us, which re-reads
// [EAX+0x684] -- but EAX is now the capacity we just wrote, not a pointer.
//
// That is not a corner case, it is the common one: `Ammo` is -1 for every type
// with unlimited ammo, so EAX becomes 0xFFFFFFFF and the stub dereferences
// [0xFFFFFFFF+0x684]. C0000005 at the stub's copied-bytes offset, on launch,
// for essentially every unit. Vanilla's own next instruction is
// `cmp eax,0xFFFFFFFF / je` precisely because -1 is expected here.
//
// Returning addr+6 is safe: Syringe patches 5 bytes + 1 NOP for a size-6 hook,
// so 0x6FB022 / 0x6FB094 are past the patched window, not inside it.

// TechnoClass::Reload -- `mov eax,[eax+0x684]` (EAX = type, ESI = this).
DEFINE_HOOK(0x6FB01C, TechnoClass_Reload_AmmoCapacity_TAExt, 0x6)
{
	enum { ReadReplaced = 0x6FB022 }; // `cmp eax,0xFFFFFFFF`

	GET(TechnoClass*, pThis, ESI);
	GET(TechnoTypeClass*, pType, EAX);

	R->EAX(EffectiveAmmoCapacity(pThis, pType));
	return ReadReplaced;
}

// The ammo-state helper called from Reload (0x6FB080) -- same instruction shape,
// same registers. This is the "am I already full?" comparison; without it a unit
// would reload past base but still be treated as full at base.
DEFINE_HOOK(0x6FB08E, TechnoClass_AmmoState_AmmoCapacity_TAExt, 0x6)
{
	enum { ReadReplaced = 0x6FB094 }; // `mov ecx,[esi+0x2FC]`

	GET(TechnoClass*, pThis, ESI);
	GET(TechnoTypeClass*, pType, EAX);

	R->EAX(EffectiveAmmoCapacity(pThis, pType));
	return ReadReplaced;
}


// ---------------------------------------------------------------------------
// Ammo PIP DISPLAY.
//
// Without this the extra rounds are real but invisible: the pips are drawn from
// TechnoTypeClass::GetPipMax (0x716290), which takes only the TYPE -- ECX is the
// TechnoTypeClass and there is no instance in sight. Adding the bonus there
// directly would show it on every unit of the type, the exact army-wide problem
// this feature exists to avoid.
//
// So we give GetPipMax a draw-time context: TechnoClass::DrawPipscale (0x709A90)
// has the instance in ECX at entry, and the ammo case of GetPipMax runs inside
// that call. Record the techno on the way in, consume it in the GetPipMax hook.
//
// The context is validated, not trusted: it must still be alive AND its type must
// match the ECX the game is asking about. GetPipMax has other callers (sidebar
// UI), so a stale pointer must not be able to add a bonus to an unrelated type.
// With bonus == 0 -- every mod not using Ammo.Parent -- both hooks are pure
// pass-throughs.
//
// Seats: 0x709A90 and 0x7162A9, both unhooked by Phobos/Antares/Kratos. The
// GetPipMax patch is 6 bytes ending at 0x7162AE, so it stops short of Antares'
// GetPipMax_MindControl hook at 0x7162B0.
// ---------------------------------------------------------------------------

namespace
{
	// Which techno the pip renderer is currently drawing. Render-only, per-client,
	// never read by synced logic -> cannot desync.
	TechnoClass* PipDrawContext = nullptr;
}

// TechnoClass::DrawPipscale entry -- ECX = the techno being drawn.
// NOTE: this one MUST return 0. The stolen bytes are `sub esp,0x64; push ebx;
// push ebp` -- frame setup that has to execute. Returning an address here would
// skip it and corrupt the stack. (The opposite of the GetPipMax hook below: the
// rule is about whether the stolen bytes are still valid to run, not a blanket
// "always return an address".)
DEFINE_HOOK(0x709A90, TechnoClass_DrawPipscale_SetContext_TAExt, 0x5)
{
	GET(TechnoClass*, pThis, ECX);
	PipDrawContext = pThis;
	return 0;
}

// TechnoTypeClass::GetPipMax, the Ammo case -- `mov eax,[ecx+0x684]`.
// ECX = TechnoTypeClass. We REPLACE the read and write EAX, so this must return
// an explicit address; returning 0 would re-run the read on the value we just
// wrote (see the note at the top of this file -- that is exactly the launch
// crash this file already caused once).
DEFINE_HOOK(0x7162A9, TechnoTypeClass_GetPipMax_AmmoCapacity_TAExt, 0x6)
{
	enum { ReadReplaced = 0x7162AF }; // the `ret` that followed the stolen read

	GET(TechnoTypeClass*, pType, ECX);

	int capacity = pType ? pType->Ammo : 0;

	// Only trust the context when it is alive and really is of this type.
	auto const pThis = PipDrawContext;
	if (capacity > 0 && pThis && pThis->IsAlive && pThis->GetTechnoType() == pType)
	{
		int const bonus = TechnoExt::GetAmmoCapacityBonus(pThis);
		if (bonus > 0)
			capacity += bonus;
	}

	R->EAX(capacity);
	return ReadReplaced;
}
