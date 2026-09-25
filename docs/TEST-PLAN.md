# Test plan — prioritised

`TESTING.md` is the exhaustive per-feature checklist and has grown to several
hundred items across many sessions. This is the ordered version: what to run
first, and why that order.

Nothing below is a formality. Of the bugs found so far, **every single one was
found in game and none were catchable by reading the code** — the zeroed
`XScale` that killed orbit and FLH, the varargs crash, the facing mirror, the
attachments exploding on sale. Reading found the *causes* afterwards; only
playing found the *symptoms*.

---

## Tier 0 — three questions that are blocking work

These are not just bug hunts. Each one has code currently written defensively
*because the answer is unknown*, and knowing it would let that code be simpler
and more certain.

### 0.1 After a save/load, what happens to attachment children?

Save a unit with attachments, reload, and look. The answer is one of:

* **duplicated** — the saved children AND freshly created ones
* **orphaned** — present but no longer following the host
* **absent** — gone entirely
* **correct** — restored and following

*Why it matters:* `RestoreAttachmentsAfterLoad` currently reconciles against all
of these because `TechnoClass::Init`'s behaviour on savegame load could not be
determined by reading. Knowing the real answer lets the restore be a targeted
fix instead of a defensive one.

### 0.2 Does a gunner survive his own trigger?

Load a GI into an IFV with `GunnerProfile.Passenger=GI`. The IFV must convert
**and keep the GI aboard**.

*Why it matters:* if the conversion ejects him, the profile reverts immediately
and the host oscillates convert→revert every `MinDwell` frames. That presents as
a **stutter or freeze, not a crash**. If you see it, report it — do not lower
`MinDwell`, which would only make it a faster freeze.

### 0.3 Does leash mode reintroduce the building-host freeze?

Put a `Motion=leashed` attachment on a **building** host and play normally.

*Why it matters:* the historic freeze came from exactly this class of
interaction — a child being told to relocate. A child that *can* move may be
safer, or may open new failure modes. Unknown either way.

---

## Tier 1 — recently reported, recently fixed

Confirm the fixes actually took, since these were all deployed untested.

1. **Sell a building with attachments** — they must NOT explode (`SoldAction`
   defaults to `vanish`). This was the reported bug.
2. **Orbit + FLH** — a `Spins.Orbit=yes` attachment orbits and sits at its FLH.
   Both broke together when `XScale` defaulted to 0.
3. **`Facing.Mode=travel`** — the drone points *along* its orbit, not against it.
4. **Grinder** — feed an attached unit to a Grinder; `AbsorbedAction` governs it.
5. **Deploy an MCV with attachments** — they transfer to the ConYard by default.

---

## Tier 2 — never tested at all

Large surface, deployed across many sessions without a single confirmation.

* **Instant spawn (H1a–e)** — triggers, counts, modes, anims, attach, the `Max`
  cap. Start with `On=destroyed` firing on a kill but **not** on a sale.
* **Spawn counts (H2/H3)** — `Spawns.Parent`, `Spawns.PerAmmo`, the cull.
* **Gunner cargo gating** — `RequiresPassenger.Type`/`.Index`.
* **Motion** — `Move.*`, `Slides`, `Bobs`, the orbit shaping tags.
* **Item 9** — `Sequence.Force` (and that a dying child still plays its death
  animation), `HoldFire.WhileMoving`, `YSortAdjust`.
* **Ammo** — capacity bonus and pip display.

---

## Tier 3 — regression sweep

Things that worked before and could have been disturbed by later work.

* Power gates (`Powered`, `PowersSiblings`, `PoweredBy`, the network).
* Prerequisites, including the dynamic gate — **the diagnostic is now live**, so
  look for `[TAExt] prereq <name>: ...` when building or selling a GAROBO. A line
  means the gate evaluated and reports its decision; silence means the rule never
  reaches the test, which points at creation/limbo instead.
* Selection, cursor pass-through, cell occupation, `Intangible`.
* Experience sharing.

---

## Online

Worth one dedicated session once Tier 0–1 pass. Everything built this cycle reads
synced state and uses `ScenarioClass::Random`, so a desync would indicate a real
mistake rather than a known risk. The highest-value cases are the ones with
serialized state: `Mode=cycle`, `InstantSpawn.Max`, `Move.*` offsets, and the
gunner base type.

---

## What a good report looks like

The reports that have been fastest to act on gave: **what you did, what happened,
and the log or `except.txt`**. Two things in particular:

* A faulting address made of **printable ASCII** (e.g. `0x6E6F7244` = `"Dron"`)
  means text is being used as a pointer — say so, it identifies the bug class
  immediately.
* **Which DLL build** — the PE timestamp, or just when you copied it. One
  diagnosis this cycle went down the wrong path because the newest CI artifact
  did not match the deployed binary.
