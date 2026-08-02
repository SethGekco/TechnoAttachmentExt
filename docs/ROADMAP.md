# TechnoAttachmentExt — feature roadmap

Status legend: ✅ done · 🔷 planned/spec'd · 💡 idea (needs design) · ❓ open question

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
- **F0. Slot-occupancy query** — "is slot X on parent occupied / active?" A
  clean predicate many items below reuse (B1, C1, E1, A1).
- **F0b. Relationship resolver** — address a target as parent / child(slot) /
  sibling(slot) / all-siblings. Reused by targeting (D1), XP (A2), prereq (E1).

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
  ❓ overflow policy: all go offline, or only the (N+1)th? (both worth exposing.)
- Needs F0 (slot-occupancy) + the relationship resolver for parent-powering.

### D. Targeting  (PARKED — last; Rex to organize thoughts)
- 💡 **D1. Attachment-relative projectile/weapon targeting.** Two flavors Rex
  described: (a) a weapon that **always** targets a fixed relative — the firer
  itself, or its parent / child / sibling; (b) **impact redirect** — you target
  the parent, but the hit lands on its children instead. No vanilla equivalent
  (closest is `AreaFire=yes`), so this is greenfield. Deferred to LAST until the
  design is clearer. Needs F0b. ❓ redirect at fire-time vs bullet-detonate.

### E. Prerequisite extension
- 💡 **E1. Sibling/child prerequisite.** Extend the existing prerequisite suite
  so an attachment can require a sibling slot present, or a child of type Z.
  Builds directly on the shipped prerequisite engine + F0/F0b.

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
5. **B1 / A2** (cargo-touching — build the shared cargo primitive here per the
   convergence plan).
6. **D1** targeting — PARKED, last, pending Rex's design.
7. Earlier backlog: SortY render-sort; walk-anim toggle; hold-fire-while-moving.
