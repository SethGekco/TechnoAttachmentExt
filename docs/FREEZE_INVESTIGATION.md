# Vehicle-child-on-building freeze — investigation log

## STATUS: SOLVED (2026-07-27)

**Root cause:** during placement, `FootClass::Mark` (`0x4D37A2`) and
`MapClass::PickUp` (`0x568831`) run a locomotion **layer check** —
`call [vtable+0x78]; cmp eax,2`. For a unit riding the attachment locomotor,
`vtable[0x78]` (the layer query) resolves through the parent and the
Mark/PickUp → building-foundation traversal recurses forever. `vtable[0x78]`
is the exact recursion driver the watchdog caught inside the foundation visitor.

**Fix (`src/Hooks.AttachedLayer.cpp`):** two unconditional static jumps that
skip the layer check — `DEFINE_JUMP(LJMP, 0x4D37A2, 0x4D37AE)` and
`DEFINE_JUMP(LJMP, 0x568831, 0x568841)` — ported verbatim from PR #352. A
*conditional* `DEFINE_HOOK` is not viable here: the target instructions are
2–3 bytes, smaller than Syringe's 5-byte patch, so a small-size hook corrupts
the following `call [eax+0x78]` (that attempt crashed at `0x4D37AB`, C0000005
reading `0x10C`). The PR skips it unconditionally for the same reason.

Confirmed in-game: `GAPOWR` host + active `HTNK` child loads, renders, and does
not freeze. Remaining polish: the `SortY` render-sort wrapper (draw order of the
child relative to its host) is not yet ported.

The history below is retained as a record of the (mostly wrong) path taken.

---

Original framing: when an **active vehicle (UnitType) child is attached to a
building host**, the game hangs in a single-frame infinite loop ("freeze").
Everything else works: loading, all child types on vehicle hosts, **building**
children on buildings, infantry/vehicle children on vehicles, and the full
prerequisite suite.

This file records what the freeze is, how it was localized, every hypothesis
tried (including the wrong ones), and the remaining plan — so the work survives
context loss and doesn't repeat dead ends.

---

## 1. Symptom & reproduction

- Config: host `[GAPOWR]` (Allied Power Plant, a building) with
  `Attachment0.Type=TestAttach`, `Attachment0.TechnoType=HTNK` (Grizzly Tank, a
  vehicle), `Attachment0.FLH=100,0,0`; `[TestAttach]` with `RespawnAtCreation=yes`
  and `OccupiesCell` unset (defaults to **false**).
- Repro: build/place the `GAPOWR`; when the vehicle child becomes active the
  game hard-locks at ~1 frame. `debug.log` shows the hang immediately after the
  `PLACE` event.
- It is a **hang**, not an access violation: Phobos' exception handler never
  fires, so there is no `except.txt`.

## 2. How it was localized — the freeze watchdog

`src/Hooks.FreezeWatchdog.cpp` (toggle `TAEXT_ENABLE_FREEZE_WATCHDOG`). A
background thread; the game-logic thread bumps a heartbeat from the Foot/Building
per-frame tick hooks. If the heartbeat stalls 6 s, the watchdog suspends the
logic thread, dumps `EIP` + a raw stack scan of code-range return addresses,
then `ExitProcess(0)` so the log can be read. Symbolize offline against
`/home/rex/gamemd.exe` (`objdump -d`).

**Result (3+ byte-identical dumps):**
- Spin `EIP` bounces between `0x746E20` (a leaf `mov eax,1; ret`) and its caller
  loop at `0x483D7E`/`0x483D80`.
- The real loop is a **recursion**: the pattern `[0x411648, 0x4378E7, 0x43798E]`
  and a `0x4BAxxx` cluster repeat at regular ~0x80-byte stack spacing.
- Full call chain (outer → inner):
  `ObjectClass 0x5F698F → CellClass 0x47BAB5 → TechnoClass 0x70A6FD/0x70AA60 →
  0x4A5FED (call 0x434B90) → BuildingClass 0x434xxx → 0x437xxx recursion →
  0x4BAxxx → CellClass passability 0x483Cxx → leaf 0x746E20`.
- Trigger: the **`PLACE` event** → building placement → occupation/foundation
  marking. **Not** pathfinding.

### The recursion core
- `F (0x437380)` and `0x4373B0` are **mutually recursive** via a throwaway
  **visitor object** (vtable `0x7E2070`, built on the stack at `0x437313` /
  `0x43724B`; its first vmethod is `0x411650`). `F` calls a virtual on the
  visited object (`call [edx+0x78]`), whose result is recursed into `0x4373B0`.
- A per-frame recursion **probe** on `0x4373B0` (`src/Hooks.RecursionProbe.cpp`,
  now compiled out) confirmed >3000 re-entries in one frame; the runaway `ECX`
  had vtable `0x7E2070`.
- **`0x4373B0` is a central, hot engine function used in RENDERING too** —
  hooking it directly corrupted *all* vehicle voxels + the cursor. So it is NOT
  a safe cut point. This is the key constraint: the fix must keep the attached
  unit *out of* this traversal, not intercept the traversal.

## 3. Key addresses (symbolized from gamemd.exe, YR 1.001)

| Address | What it is |
|---|---|
| `0x746E20` | leaf virtual returning `true` (spin EIP; a red herring) |
| `0x483Cxx` (hook pt `0x483D8E`) | `CellClass::CheckPassability`-ish; iterates a cell's object list |
| `0x4373B0` + `0x437380 (F)` | BuildingClass foundation/cell **visitor traversal** (recursion core); hot, also used in rendering |
| vtable `0x7E2070` | throwaway visitor/iterator class; first vmethod `0x411650` |
| `0x434B90` / `0x434xxx` | BuildingClass placement/foundation entry into the recursion |
| `0x4A5FED` | calls `0x434B90` |
| `0x47BAB5` | CellClass method in the chain (CellClass CTOR is `0x47BDA1`) |
| `0x5F698F` | ObjectClass method (near `RemoveThis` `0x5F6609`) — likely `Mark` |
| SetOccupyBit / ClearOccupyBit | vtable `0x7F5D60`→`0x7441B0` / `0x7F5D64`→`0x744210` |

## 4. Hypotheses tried — and why each was WRONG

Ordered; all falsified by a subsequent identical watchdog dump unless noted.

1. **Movement suppression / do-nothing loco spin** (earliest). Wrong: the loco
   is fully ported (27 methods) and the spin never touches our loco/AI/movement.
2. **Cell-lookup visibility** — ported `CellTechno` exclusion
   (`FindTechnoNearestTo` wrappers `0x47C432` + call sites) and ClearFactoryBib.
   Helps building *lookups* but did **not** stop the freeze.
3. **Occupation flag (0x20)** — ported `SetOccupyBit`/`ClearOccupyBit` skip for
   attached children (wrapper confirmed live via log; Ares does NOT override
   these vtable slots). Freeze unchanged → occupation bit is not the driver.
4. **`IsChildOf` recursion cycle** — made `IsChildOf` iterative + depth-capped.
   Correct hardening, but not this freeze.
5. **Scatter loop** — ported `CanScatter` (`0x6F3283`), `CellClass::Incoming`
   (`0x4817A8/C3`), `InfantryClass::Scatter` (`0x51D0DD`). Freeze unchanged.
6. **Attached unit's own AI pathing** — ported the missing 13 DisallowMoving /
   mission / approach hooks (Mission_Hunt `0x73EFC4`, AreaGuard `0x744103`,
   ApproachTarget `0x7414E0`, etc.). Freeze **byte-identical** → not AI-driven.
7. **Pathfinder occupier check (`CanEnterCell`)** — this was the strongest
   hypothesis. Ported the full `CanEnterCell` cluster (UnitClass
   `0x73F520/528/A92`, InfantryClass `0x51C249/251/78F`) with helpers
   `AssumeNoVehicleByDefault` / `IsOccupierIgnorable` (the `AccountForMovingInto`
   / `IncomingUnit` part stubbed). Freeze **byte-identical** → the recursion is
   NOT pathfinding; it is placement-time occupation/foundation marking.

### Non-freeze wrong turns worth remembering
- **The load-time close was NOT any of the above** — it was a `char[32]` buffer
  overflow formatting `"Attachment0.Prerequisite.Negative"` (33 chars) →
  `_snprintf_s` invalid-parameter handler → silent process kill, no `except.txt`.
  Fixed by enlarging to `char[64]`. Lesson: **instant close + no except dump ⇒
  suspect a CRT abort (buffer/format/assert), not an SEH fault.**
- Blamed the user's INI and `AITriggerTypeExt.dll` before isolation tests
  (rename-DLL-away) proved it was our DLL alone. Lesson: **isolate before
  theorizing.**
- The recursion **probe** on `0x4373B0` broke rendering — never hook a function
  that also runs in the render path.

## 5. Why the PR (#352) doesn't have this problem

The PR is compiled *inside* Phobos with full access to its cell/occupation
framework. It does **not** hook the recursion functions (`0x434xxx`, `0x437xxx`,
`0x4BAxxx`) at all — it prevents the attached unit from *entering* the traversal
by keeping it out of the occupation *state*. That state lives in a `CellClass`
extension (`CellExt`, `ExtPointerOffset = 0x144`) with `IncomingUnit` /
`IncomingUnitAlt`, populated by the full `SetOccupyBit`/`ClearOccupyBit` reimpl.
A standalone can't reuse offset `0x144` (base Phobos already owns that cell ext),
so we must reconstruct an equivalent in **map mode**.

## 6. What is already ported (and works / is retained)

CellTechno exclusion, ClearFactoryBib, SetOccupyBit/ClearOccupyBit skip,
CanScatter, CellClass::Incoming, InfantryClass::Scatter, all DisallowMoving +
mission/approach hooks, the CanEnterCell cluster (IncomingUnit stubbed), the full
27-method locomotor, all container/lifecycle/prereq systems.

## 7. Remaining plan (Option 2 — occupation infrastructure)

The freeze is placement-time occupation/foundation marking, so the next port is
the cell-occupation state the PR relies on:

- **Phase A:** map-mode `CellExt` (Canary, no `ExtPointerOffset`) holding only
  `IncomingUnit` / `IncomingUnitAlt`, with CellClass CTOR/DTOR/save-load +
  pointer-invalidation hooks. (PR uses offset mode at `0x144`; we can't.)
- **Phase B:** replace the `SetOccupyBit`/`ClearOccupyBit` *skip* with the full
  reimpl that populates `IncomingUnit`, and un-stub `AccountForMovingInto`.
- **Open risk:** the recursion is BuildingClass *foundation* code, while the
  occupation state is UnitClass *movement* state — they may not intersect. If a
  post-Phase-B watchdog dump is still identical, the real cut is likely in how
  the attached unit is added to the **cell object list** during host Unlimbo
  (timing/re-entrancy: the child is Unlimbo'd *during* the host's placement).
  Next probe target if so: the `PLACE`/`Unlimbo` ordering and the cell
  object-list insertion for the child.

## 8. Methodology notes (what actually worked)

- **Watchdog > guessing.** Every "obvious" fix failed; the watchdog gave ground
  truth. Keep it as the primary tool.
- **Symbolize offline** against `/home/rex/gamemd.exe`; YRpp does not expose
  these internal addresses.
- **Verify the running build** via the `[TechnoAttachmentExt] Build: <ts>` stamp
  in `debug.log` before trusting any dump.
- Install a build by copying **both** `TechnoAttachmentExt.dll` and `.map` from
  the CI artifact into
  `/home/rex/snap/cncra2yr/common/.wine/drive_c/Westwood/RA2/`.
