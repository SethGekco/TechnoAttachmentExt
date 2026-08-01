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
  `AttachmentX`, `Dettachment2` — on rank-up, create/activate slot X; detach
  slot X. ❓ Elite only, or Veteran too? ❓ Reverse on de-vet (rare)? ❓ New
  `EliteAbilities` values vs. a dedicated tag like `VeterancyAttachments=`?
- 💡 **A2. Experience passing.** Route earned XP to: cargo members (all / a
  specific slot), parent, child, or siblings. ❓ Full or proportional share?
  ❓ Per-AttachmentType direction flags? **Overlaps PayloadExt** (cargo/bay
  veterancy) — decide which project owns cargo-XP.

### B. Conditional activation
- 💡 **B1. Activate only if a parent slot is occupied.** e.g. activated turrets
  for open-topped, or "pseudo-open-topped" where the parent is just cargo.
  Needs F0. ❓ "occupied" = a filled attachment slot, or a cargo/passenger
  present? The cargo reading **overlaps PayloadExt** (open-topped/gunner).

### C. Powered state
- 💡 **C1. Unit powered by child/sibling.** A unit is powered/functional only
  while a given child or sibling attachment is present/active (like building
  `Powered=` but keyed on attachment state). Needs F0. ❓ What does "unpowered"
  disable for a *unit* — firing, movement, abilities?

### D. Targeting
- 💡 **D1. Attachment-relative projectile/weapon targeting.** Let a
  weapon/projectile retarget to the child / parent / sibling(s) of its current
  target. Needs F0b. ❓ Redirect at fire time or at bullet-detonate time?
  ❓ New WeaponType/Warhead tags?

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
1. **F0 / F0b** foundations (unlock B1, C1, D1, E1, A1 cleanly).
2. **E1** (smallest — extends existing prereq engine).
3. **A1** veterancy attach/detach (self-contained, high flavor).
4. **B1 / C1** (need cargo-vs-attachment decision w/ PayloadExt).
5. **A2 / D1** (largest / most cross-cutting).
6. Earlier backlog: SortY render-sort; walk-anim toggle; hold-fire-while-moving.
