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

// TechnoClass::Reload -- `mov eax,[eax+0x684]` (EAX = type, ESI = this).
// We replace the read, so EAX must be set to the effective capacity here.
DEFINE_HOOK(0x6FB01C, TechnoClass_Reload_AmmoCapacity_TAExt, 0x6)
{
	GET(TechnoClass*, pThis, ESI);
	GET(TechnoTypeClass*, pType, EAX);

	R->EAX(EffectiveAmmoCapacity(pThis, pType));
	return 0;
}

// The ammo-state helper called from Reload (0x6FB080) -- same instruction shape,
// same registers. This is the "am I already full?" comparison; without it a unit
// would reload past base but still be treated as full at base.
DEFINE_HOOK(0x6FB08E, TechnoClass_AmmoState_AmmoCapacity_TAExt, 0x6)
{
	GET(TechnoClass*, pThis, ESI);
	GET(TechnoTypeClass*, pType, EAX);

	R->EAX(EffectiveAmmoCapacity(pThis, pType));
	return 0;
}
