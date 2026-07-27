// Locomotion-layer-check skip (THE vehicle-on-building freeze fix).
//
// The freeze watchdog traced the hang to a building foundation-grid traversal
// whose recursion driver is a virtual call `call [vtable+0x78]` -- the unit's
// locomotion layer query. During placement, both FootClass::Mark (0x4D37A2)
// and MapClass::PickUp (0x568831) issue that exact `call [eax+0x78]; cmp eax,2`
// check; for a unit riding the attachment locomotor it resolves through the
// parent and the Mark/PickUp -> building-foundation traversal recurses forever.
//
// Ported verbatim from PR #352 as an unconditional static jump (skip the check
// for all units). A conditional DEFINE_HOOK is NOT viable here: the target
// instructions (mov eax,[esi] / mov ecx,esi / call [eax+0x78]) are 2-3 bytes,
// smaller than Syringe's 5-byte hook patch, so a small-size hook corrupts the
// instruction stream (it crashed at 0x4D37AB reading a bogus address). The PR
// skips it unconditionally for the same reason; this matches the behavior the
// mod already ran under PR-Phobos.

#include <Utilities/Macro.h>

DEFINE_JUMP(LJMP, 0x4D37A2, 0x4D37AE); // Skip locomotion layer check in FootClass::Mark
DEFINE_JUMP(LJMP, 0x568831, 0x568841); // Skip locomotion layer check in MapClass::PickUp
