# I — Gunner host profiles (design, not yet implemented)

Granting the **host** different stats (`Strength=`, `Speed=`, armor, `Primary=`)
depending on who is riding in it. The gunner mechanic, beyond what shipped.

Status: **I-a, I-b and I-c are BUILT** (`src/Ext/Techno/Body.TypeConversion.cpp`,
`TechnoExt::UpdateType`). I-b onward is still design.

**Already shipped and NOT part of this** — cargo-identity gating
(`RequiresPassenger.Type=` / `.Index=`), which lets a passenger switch an
*attachment* on and off. That covers the gunner-weapon case, because the
attachment child carries its own weapon and art. This document is only about the
part that cannot be expressed that way: changing the host itself.

---

## 1. Mechanism — there is only one real option

| Option | Verdict |
|---|---|
| **A. Mutate the host's `Type` pointer in place** | **The answer.** Every stat follows the type, so nothing has to be enumerated. |
| B. Hook each stat read and substitute | Not viable. Dozens of read sites, most heavily contested, and `Strength`/`Speed`/armor are read from everywhere. |
| C. Destroy and recreate the host | Loses passengers, orders, veterancy, target, position-in-formation. The gunner would be evicted by the very thing that is meant to be reading him. |
| D. Attachment child (shipped) | Already done; cannot touch host stats by construction. |

### Why A is known-viable rather than hopeful

**Antares already does exactly this**, in `TechnoExt::UpdateType`
(`src/Ext/Techno/Body.cpp`), driven from its `Convert.Deploy` feature at
`0x73DE90`. That is a working, shipped reference for mutating a live techno's
type, and it tells us precisely which derived state has to be repaired. We should
follow its sequence rather than rediscover it.

---

## 2. What `UpdateType` has to repair — read off Antares' implementation

In order, and every one of these is a thing that breaks if skipped:

1. **Pick the right field.** `Type` lives on the concrete class, not
   `TechnoClass` — `InfantryClass::Type`, `UnitClass::Type`, `AircraftClass::Type`
   are three different members. Buildings are absent from Antares' switch, which
   is a signal in itself (see §5).
2. **Reject a mismatched abstract type.** `UnitType` cannot become `InfantryType`.
3. **Release temporal.** If something is chrono-warping the host, `LetGo()` it
   first — detaching without releasing leaves the victim frozen with nothing left
   to warp it back.
4. **Deregister from the owner** (`RegisterLoss` + `RemoveTracking`) *before* the
   swap and **re-register after** (`AddTracking` + `RegisterGain`), plus
   `RecheckTechTree = true`. Otherwise house counts, tech tree and the score
   screen are computed against a type the unit no longer is.
5. **Preserve health as a RATIO**, not an absolute: capture
   `Health / oldType->Strength` before, `SetHealthPercentage(ratio)` after, and
   fix up `EstimatedHealth`. A unit swapping to a tougher type must not be
   instantly near-dead.
6. **Clamp ammo** to the new type's capacity.
7. **Drop type-granted effects.** Anything the old type conferred goes away with
   it, and stats derived from the type must be recalculated.

**Our additional obligations**, which Antares has no reason to handle:

8. **Attachments.** Slot lists are keyed to the *type's* `AttachmentData`, so the
   slot list changes shape under the swap. We already have
   `TechnoExt::HandleAttachmentConversion(pThis, pOldType, pNewType)` for exactly
   this — it must be called, and it is the single most likely thing to be
   forgotten.
9. **Our per-instance vectors** indexed by rule position
   (`InstantSpawnLastFired`, `InstantSpawnCyclePos`, `InstantSpawnLive*`) are
   sized against the combined TechnoType + AttachmentType rule list. A type swap
   changes that list, so they must be reset rather than left misaligned — a
   stale index would attribute one rule's cooldown to another.
10. **The base type must be remembered and serialized**, or the host can never
    revert when the passenger leaves.

---

## 3. The passenger question — the one that actually worries me

Converting a *transport* is not the same as converting a deployer, and Antares'
reference case (`Convert.Deploy`) never carries cargo.

| Concern | Assessment |
|---|---|
| Do passengers survive? | **Probably yes** — `Passengers` is instance state on `TechnoClass`, untouched by a type swap. Needs confirming in game, not assuming. |
| New type has smaller `Passengers=`? | **Overfull cargo.** The engine will not evict anyone; the transport just holds more than its type allows. Must be validated at parse time (§6) rather than handled at runtime. |
| New type has `Passengers=0`? | Same, worse. Parse-time rejection. |
| Does the gunner himself survive? | He must — he is the trigger. If he does not, the profile immediately reverts and the host oscillates every frame. **This is the first thing to test.** |
| `SizeLimit` / `Size` mismatch | A passenger legal in the old type may be illegal in the new one. Same treatment: validate at parse time. |
| OpenTopped differences | Passengers firing out may start or stop doing so mid-ride. Acceptable, and arguably the point. |

**Oscillation is the failure mode to design against.** Convert → gunner is
somehow displaced → condition false → revert → gunner re-registers → convert.
That is an infinite loop that costs a frame each time and looks like a freeze.
Mitigations: a minimum dwell time before reverting, and a conversion-depth guard
of the same kind `InstantSpawn` already carries.

---

## 4. Tag shape

Same rule-group grammar as `Convert` and `InstantSpawn` — unindexed group plus
contiguous `[N]`, both independent. On the **TechnoType** of the host.

```ini
[IFV]
GunnerProfile=IFV_ROCKET          ; become this type...
GunnerProfile.Passenger=GI        ; ...while a GI is aboard
GunnerProfile.Index=0             ; 0-based, -1 = any position (default)

GunnerProfile[0]=IFV_MEDIC
GunnerProfile.Passenger[0]=MEDIC

GunnerProfile.KeepHealth=yes      ; ratio-preserving (default yes)
GunnerProfile.KeepVeterancy=yes
GunnerProfile.MinDwell=15         ; frames before a revert may happen
```

First matching rule wins; **no match reverts to the base type**, so reverting is
inherent and needs no separate flag — the same shape as the existing `Convert`
rules for children.

`GunnerProfile.Index` is **0-based**, consistent with `Attachment0`,
`RequiresSlot.Index`, `ExperienceTo.Slot` and `RequiresPassenger.Index`.

---

## 5. Buildings are out of scope

Antares' `UpdateType` handles Infantry, Unit and Aircraft and deliberately omits
Building. Buildings hold foundation, occupancy, base-node and power state that is
computed at placement, so mutating `BuildingClass::Type` under a live structure
is a different and much larger problem. Garrison "gunners" are a distinct
mechanic. **Out of scope; state it in the docs rather than letting someone find
out.**

---

## 6. Validate at parse time, not at runtime

Every one of these is knowable when the INI loads, and each produces a confusing
in-game symptom if left to runtime:

- profile type's abstract type must match the host's (`UnitType` → `UnitType`)
- profile `Passengers=` must be **>= the host's**, or a full transport converts
  into an overfull one
- profile `SizeLimit=` must not be smaller than the host's
- `GunnerProfile.Passenger=` must name a real InfantryType
- a profile that is itself a `GunnerProfile` host is a cycle — reject it

Loud rejection at load beats a silent misbehaviour at runtime; this is the same
principle as `On.Reason` naming its unimplemented values.

---

## 7. Build order

1. ~~**I-a — the conversion primitive.**~~ **DONE.** `TechnoExt::UpdateType` in
   `src/Ext/Techno/Body.TypeConversion.cpp`, following the §2 checklist plus our
   attachment and rule-vector obligations, and keeping the attachment locomotor
   on a converted child so it is not cut loose from its parent. No trigger is
   wired to it yet, so it is currently unreachable from INI by design.
2. ~~**I-b — the cargo trigger.**~~ **DONE.** `GunnerProfile.*` on the host's
   TechnoType, evaluated each synced tick. Rules are read from the BASE type so a
   converted host is not stranded, and `MinDwell` brakes both directions.
3. ~~**I-c — parse-time validation** (§6).~~ **DONE**, but as a post-typedata
   pass at `0x679CAF` rather than in the per-type parser: a named profile may
   belong to a section not yet read, so capacity and cycle checks are not
   knowable during the host's own parse.
4. **I-d — save/load.** Base type serialized; verify a saved converted host
   reloads converted and can still revert.

I-a is the piece with the risk; I-b onward is bookkeeping.

---

## 8. Open questions for Rex

1. **Should the profile apply to the host, or is a "profile attachment" enough?**
   Cargo-identity gating already ships and covers weapons/art with none of the
   above risk. This document exists for `Strength=`/`Speed=`/armor specifically —
   confirm that is genuinely wanted before I take on type mutation.
2. **Revert delay.** Is an instant revert wanted when the gunner steps out, or a
   grace period (`MinDwell`) so a quick unload/reload does not thrash?
3. **Multiple passengers, multiple matches.** First rule wins is proposed. The
   alternative — most-specific wins, or a priority number — is more control but
   more to explain.
4. **Should this live here at all?** It is arguably PayloadExt's cargo territory
   rather than an attachment feature. The roadmap says the cargo primitive lands
   here, but host type mutation is a big thing to own; worth reconfirming.
