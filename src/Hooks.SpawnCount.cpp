// H2/H3 -- dynamic spawn count.
//
// [AttachmentType] / AttachmentN.Spawns.Parent=N
//   While this attachment's child is active, the host may keep N more spawns
//   alive. Bonuses from several active slots sum. (H2)
//
// [TechnoType] Spawns.Base= / Spawns.PerAmmo= / Spawns.Cull=
//   Spawns.Base is the count with nothing attached; Spawns.PerAmmo additionally
//   clamps the cap to (Ammo * N). (H3)
//
// WHY THE CAP WORKS DOWNWARD
// --------------------------
// SpawnManagerClass allocates its node slots ONCE, in its constructor, from
// TechnoTypeClass::SpawnsNumber. The vector never grows, and attachments do not
// even exist yet when that constructor runs -- so there is no honest way to make
// an attachment ADD a slot. The modder therefore sets SpawnsNumber to the
// maximum and Spawns.Base to the unattached count; attachments unlock the
// difference. Documented in TAGS.md rather than left as a surprise.
//
// TWO HALVES
// ----------
//   * regeneration is gated here (a dead node is not allowed to come back while
//     the host is at its cap),
//   * and the surplus is culled from the synced techno tick when the cap DROPS
//     (TechnoExt::UpdateSpawnCap). Without the cull, lowering the cap appears to
//     do nothing until spawns happen to die, which reads as a broken tag.
//
// The cull deliberately does NOT live in a hook inside the SpawnManager's own
// update: killing a spawn runs destruction logic, and doing that while the
// engine is mid-iteration over SpawnedNodes is the same re-entrancy hazard that
// produced the null-Child crash in AttachmentClass::AI().
//
// DETERMINISM: the cap is integer arithmetic over synced state (active slots,
// current ammo), and the cull walks SpawnedNodes from the end, so the same spawn
// dies on every peer. No RNG, no wall clock.

#include <SpawnManagerClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>

// SpawnManagerClass::Update -- the "this dead node's regen timer has finished,
// bring the spawn back" decision.
//
// Seat: 0x6B78CD is the `jnz 0x6B795A` that skips a node whose timer is still
// running. It covers 0x6B78CD-0x6B78D2 and abuts Phobos' 0x6B78D3
// (SpawnManagerClass_Update_Spawns, 0x6) exactly without overlapping it. Antares
// is at 0x6B783B and Kratos at 0x6B78E4, both clear.
//
// ⚠ THIS HOOK MUST NEVER RETURN 0. The stolen bytes are a RELATIVE branch, and
// Syringe copies stub bytes without relocating them -- returning 0 would execute
// a jnz with the original displacement from the stub's address and land in the
// weeds. So the original branch is reproduced explicitly below and every path
// returns a concrete address.
DEFINE_HOOK(0x6B78CD, SpawnManagerClass_Update_RegenCap_TAExt, 0x6)
{
	enum { StillWaiting = 0x6B795A, Respawn = 0x6B78D3 };

	GET(SpawnManagerClass* const, pThis, ESI);
	GET(int const, remaining, EDX);

	// The original test: a node whose timer has not elapsed is skipped.
	if (remaining != 0)
		return StillWaiting;

	// Timer is up. Hold the slot dead while the host is at its cap; the node is
	// re-examined next frame, so it comes back on its own once the cap rises.
	int const cap = TechnoExt::GetEffectiveSpawnCap(pThis->Owner);
	if (cap >= 0 && pThis->CountAliveSpawns() >= cap)
		return StillWaiting;

	return Respawn;
}
