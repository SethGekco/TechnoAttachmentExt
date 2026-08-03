# TechnoAttachmentExt — feature roadmap

Status legend: ✅ done · 🔷 planned/spec'd · 💡 idea (needs design) · ❓ open question

## Project-wide constraints (apply to EVERY feature below)
- **🔒 ONLINE / MULTIPLAYER SAFE (hard requirement).** Rex requires online play.
  Therefore all new logic must be **deterministic and lockstep-safe**:
  - Every new bit of runtime state must be serialized in save/load (and stay in
    sync across peers) — attachments, ammo/spawn modifiers, power state,
    veterancy-driven changes, gunner state, etc.
  - No per-machine randomness or wall-clock timing in **synced game logic**
    (render-only randomness is fine, but keep it strictly separated — this is
    exactly the KratosPP shared-RNG desync lesson).
  - Any RNG in synced logic must use the game's synced RNG (`ScenarioClass::
    Random`), never `rand()`/std RNG/time.
  - Prefer game-frame counters over real time for delays/timers.
  - Test plan for each feature: a 2-player skirmish that exercises it must not
    desync.
- **🧰 Toolchain = Antares, not Ares.** Target/reference **Antares** (open-source
  Ares superset) for all new work; when in doubt, ask rather than assume Ares.
  ✅ **Re-verified coexisting with Antares (2026-08-01):** loads clean, the
  freeze fix holds, and the shared `0x6F3283` (CanScatter) chained hook behaves
  (that was the one flagged coexistence risk). NOTE: Antares presents the
  "Ares 3.0p1" identity, so Phobos logs it as `Module Ares.dll` even though the
  injected file is `Antares.dll` — don't mistake that label for real Ares.
- **📖 Hook encyclopedia workflow (mandatory).** Before hooking any gamemd.exe
  address, check `~/Claude/YR-Hook-Encyclopedia` (registry for who-hooks-it +
  conflicts, Tier-2 for does/does-not). After using a hook, contribute a Tier-2
  entry. Findings so far → `encyclopedia/Attachment-Cell-Placement.md`.
  Registry-validated: our freeze-fix jumps `0x4D37A2`/`0x568831` are
  collision-free; DisallowMoving addrs (`0x740A93`/`0x744103`/`0x7414E0`) chain
  with base **Phobos release** (our returns win when Phobos returns 0).

## Shipped
- ✅ Standalone Syringe DLL coexisting with base Phobos (+Ares).
- ✅ Any TechnoType as a child (vehicle / infantry / aircraft / **building**).
- ✅ Attachment locomotor, respawn/inherit/turret/destruction options.
- ✅ Rich prerequisite suite (per-slot + AttachmentType, dynamic/static,
  Negative, RequiredHouses/ForbiddenHouses).
- ✅ Factory-exit handling for infantry/vehicle children.
- ✅ **Vehicle-child-on-building freeze** (see `FREEZE_INVESTIGATION.md`).

---

## Project-structure decision (PayloadExt convergence)
Rex is fine with a larger single DLL; the original separation was just to
isolate which DLL causes a bug. **Plan: converge by growth, not a merge.**
- Build the shared **foundations** (F0/F0b, veterancy/power passing, cargo)
  here, general enough to serve both attachment- and payload-driven features.
- Let PayloadExt's Bay / Deploy / Launched-Object features land in THIS DLL as
  we reach them; don't maintain a second DLL that duplicates the primitives.
- Defer any formal rename of the project until it clearly reads as broader.
- Debug isolation is preserved via compile-time toggles (`TAEXT_ENABLE_*`) and
  the whole-DLL rename-away test — no need to keep separate DLLs for that.

## Proposed scope (2026-07-27 brainstorm)

Grouped by mechanism. "Sibling" = another attachment on the same parent.
Several items need two shared primitives first (see Foundations).

### Foundations these build on
- ✅ **F0. Slot-occupancy query** (shipped) — `TechnoExt::GetChildSlot`,
  `IsSlotFilled`/`IsSlotActive`(+`ById`), `Count{Filled,Active}Slots`. "Active" =
  child exists, alive, not in limbo. Reused by B1, C1, E1, A1, G.
- ✅ **F0b. Relationship resolver** (shipped) — `AttachmentRelation` enum +
  `ResolveRelative`/`ResolveRelatives` (parent / top-level / child(slot) /
  sibling(slot) / all-children / all-siblings, by index or ID). Reused by D1,
  A2, E1, targeting. Both pure/deterministic → online-safe, no new state.

### A. Veterancy & experience
- 💡 **A1. Veterancy-driven attach/detach.** `EliteAbilities=Attachment0`,
  `AttachmentX`, `Dettachment2` — on rank-up, create/activate slot X; `DettachN`
  detaches slot N. **Decided:** triggers on **both Veteran and Elite**; **reverse
  on de-vet** (rare, but undo when it happens) → attachment state tracks current
  rank. ❓ still: new `EliteAbilities` values vs. a dedicated tag.
- 💡 **A2. Experience passing.** Route earned XP to: cargo members (all / a
  specific slot), parent, child, or siblings. ❓ Full or proportional share?
  ❓ Per-AttachmentType direction flags? **Overlaps PayloadExt** (cargo/bay
  veterancy) — decide which project owns cargo-XP.

### B. Conditional activation
- 💡 **B1. Activate only if a parent slot is occupied.** e.g. activated turrets
  for open-topped, or "pseudo-open-topped" where the parent is just cargo.
  Needs F0. ❓ "occupied" = a filled attachment slot, or a cargo/passenger
  present? The cargo reading **overlaps PayloadExt** (open-topped/gunner).

### C. Powered state  (extend PoweredUnit / PowersUnit)
Base game: `[ROBO] PoweredUnit=yes` + `[GAROBO] PowersUnit=ROBO` — if GAROBO is
lost or loses power, ROBO tanks go EMP-dark (no move/attack/orders). Rex wants
this system generalized:
- 💡 **C1. `PowersUnit=` on UNITS (incl. attached technos), not just buildings.**
  Especially: an attached child powers **its parent** (and/or the reverse).
  "Unpowered" mirrors the ROBO behavior — dark: no move, no fire, ignores orders
  (EMP-like).
- 💡 **C1b. `PowersUnits.Count=N`** — cap how many units a powerer sustains.
  Overflow policy is **per-powerer, modder-configurable** (a tag on the powering
  unit, e.g. `PowersUnits.OverflowMode=all|excess`): `all` shuts the whole group
  down past N, `excess` leaves only the (N+1)th-onward unpowered. Ordering for
  "which is the excess one" reuses the slot-priority scheme from sibling
  stacking (below). Default TBD.
- Needs F0 (slot-occupancy) + the relationship resolver for parent-powering.

### D. Targeting  (PARKED — last; Rex to organize thoughts)
- 💡 **D1. Attachment-relative projectile/weapon targeting.** Two flavors Rex
  described: (a) a weapon that **always** targets a fixed relative — the firer
  itself, or its parent / child / sibling; (b) **impact redirect** — you target
  the parent, but the hit lands on its children instead. No vanilla equivalent
  (closest is `AreaFire=yes`), so this is greenfield. Deferred to LAST until the
  design is clearer. Needs F0b. ❓ redirect at fire-time vs bullet-detonate.

### E. Prerequisite extension
- ✅ **E1a. Sibling prerequisite** (shipped) — `Prerequisite.Sibling.Index/.Type`
  (ANY of list, OR) and `Prerequisite.Siblings.Index/.Type` (ALL of list, AND);
  `.Index` = sibling slot index, `.Type` = sibling child TechnoType (excludes
  self). Folded into the dynamic `PrerequisitesMet()`. AttachmentType-level.
- ❌ **"Child" prerequisite — dropped (paradox).** An attachment IS a child;
  gating it on its own child is a bootstrap cycle (the child can't exist until
  the attachment is active, which is gated on the child), and gating on the
  parent's other children is just a sibling. Siblings cover the real cases.
- 🔷 **E1b. Numbered prerequisite groups** — `Prerequisite[N].*` (brackets,
  per Rex). Model: gate met if the un-numbered **base** (always AND-required) and
  **any one** numbered group are satisfied → `base AND (group0 OR group1 …)`.
  Within a group all conditions AND. Each group carries its own building
  `Prerequisite[N]=`, `.Negative`, houses, and `.Sibling(s).*`. Unifies with the
  planned Ares-style `Prerequisite.Lists` (former batch-2 item).
- 🔷 **E1c. Per-slot sibling override** — mirror the existing per-slot building
  prereq override (`AttachmentX.Prerequisite.Sibling.*`), later.

### G. Ammo & Spawn counts  (veterancy- and attachment-driven)
Rex's sketch (polish later):
```
[ORCA]
Ammo=1
Vet.Ammo=2          ; ammo capacity at Veteran
Elite.Ammo=3        ; ammo capacity at Elite
Attachments=AMMOBOX

[AMMOBOX]
Parent.Ammo=2       ; override parent's ammo (capacity/current?)
Parent.AddAmmo=3    ; add 3 to parent's ammo
```
- 💡 **G1. Veterancy-scaled ammo/spawns.** `Vet.Ammo=` / `Elite.Ammo=` (and the
  spawn equivalents `Vet.SpawnsNumber=` / `Elite.SpawnsNumber=`) change the count
  by rank. Pairs with A1 (veterancy engine); reverse on de-vet.
- 💡 **G2. Attachment modifies parent's ammo/spawns.** `Parent.Ammo=` (override)
  and `Parent.AddAmmo=` on the child adjust the parent while attached; spawn
  equivalents too. Uses F0b (relationship resolver → parent).
- **Decided:** support **both capacity and current** modes — usually you want
  capacity (`Vet.Ammo` raises the max), but allow setting current ammo on
  level-up too (so a slow-firing unit doesn't suddenly become full/instant-fire
  on rank-up). Two tag variants or a mode flag.
- **Decided:** `Parent.AddAmmo` **reverts on detach / de-vet.** (A far-future
  "unit picks up & keeps attachments" mode — the original AttachmentTypes
  author's larger vision — is explicitly out of scope for now.)
- **Decided:** sibling stacking **sums by default**, with a modder opt-out. For
  the non-stacking case use a **priority/fallback order**: lowest slot
  (Attachment0/1…) wins; if it's disabled/destroyed, the next takes over. Same
  ordering reused by C1b's overflow "extra one". Touches Techno `Ammo` +
  `SpawnManagerClass`.

### H. Spawner extensions  (this DLL now touches ammo/spawns)
- 💡 **H1. Instant spawn on self or target**, with an animation on the firer
  and/or the spawn location. (Vanilla spawns launch from a bay over time; this
  is an immediate placement with configurable anims.)
- 💡 **H2. Spawn count driven by attachments** — number of spawns = f(which/how
  many attachment slots are active). Uses F0 (slot-occupancy).
- 💡 **H3. Spawn count driven by ammo** — spawn count scales with current ammo.
  Pairs with G1 (veterancy/attachment ammo scaling) for combos.
- ❓ synced-RNG for any random spawn placement; ❓ overlaps PayloadExt
  spawner/bay design — build the shared spawner primitive here.

### I. Advanced gunner system  (big; heavy PayloadExt overlap)
Vanilla IFV: `WeaponN=` picks a weapon by the passenger's gunner index. Rex
wants each index to carry a **full profile**, not just a weapon — so entering
infantry can grant designators, attachments, superweapons, cash production, a
gap generator, etc.
```
Gunner.Inheritance.1=GUNNER01IN
[GUNNER01IN]
Strength=
Speed=
Primary=
...
```
- 💡 **I1. Per-index inheritance profile** — gunner index N inherits a named
  pseudo-type's stats/weapons/abilities onto the host while that passenger is
  aboard.
- 💡 **I2. Multiple gunner slots** (`Gunner=yes` on more than one slot).
  Scope for v1: **assume all gunner vehicles use cargo index 1** to keep the
  infantry-interaction model simple; generalize later.
- 💡 **I3. Cargo-count-driven evolution** — the host changes progressively as
  more infantry board (an attachment-like transform per passenger count). Rex
  notes this is "very similar / maybe the same" as B1 (activate-if-slot-occupied)
  and the attachment-per-cargo idea — likely one unified mechanic.
- This is squarely **PayloadExt gunner/open-topped territory**; per the
  convergence plan it lands here. Needs F0/F0b + the cargo primitive. Define the
  gunner state carefully for save/load + online sync.

### F. Inheritance
- 💡 **F1. AttachmentType force-inheritance profile.** A profile that forces a
  set of inheritance/behavior flags — non-selectable, no cargo, etc. — so a
  *regular* unit can be attached as a pure functional/cosmetic piece without
  behaving like a standalone unit. Extends the existing `Inherit*` options into
  a reusable named profile.

---

## Cross-project note (PayloadExt overlap)
Items A2, B1 (and parts of C1) touch **cargo / open-topped / gunner /
veterancy-index** mechanics that the separate **PayloadExt** project already
scopes. Before implementing, decide per-feature: does it live here
(attachment-driven) or in PayloadExt (cargo/bay-driven)? Attachments and cargo
are different primitives; the XP/open-topped bits are where they meet.

## Suggested sequencing
1. **F0 / F0b** foundations (unlock A1, B1, C1, E1 cleanly).
2. **E1** sibling/child prerequisite (smallest — extends existing prereq engine).
3. **A1** veterancy attach/detach — Veteran+Elite, reverse on de-vet (self-
   contained, high flavor).
4. **C1** PoweredUnit/PowersUnit extension (concrete, well-defined mechanic;
   C1b count cap is a cheap add-on).
5. **G1/G2** ammo/spawn counts (ride the veterancy + relationship foundations).
6. **H1–H3** spawner extensions (shared spawner primitive; synced RNG).
7. **B1 / A2 / I (gunner)** — the cargo cluster; build the shared cargo/gunner
   primitive here per the convergence plan. B1 and I3 are likely one mechanic.
8. **D1** targeting — PARKED, last, pending Rex's design.
9. Earlier backlog: SortY render-sort; walk-anim toggle; hold-fire-while-moving.

Every item above must satisfy the online/multiplayer constraint at the top.
