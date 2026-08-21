# TechnoAttachmentExt — tag reference (standalone additions)

Tags added by this DLL on top of Phobos PR #352. Everything here is
**lockstep-deterministic** (synced tick, serialized state, no RNG/wall-clock), so
it is safe in online games.

Resolution order for anything that exists in more than one place:
**TechnoType → AttachmentType → per-slot `AttachmentN.*`** (last one wins).

---

## Deactivation ("dark") system

All of these make a techno go **dark** — the vanilla Robot-Tank state: no movement,
no firing, ignores orders. They share **one arbiter**: any active reason keeps the
techno dark, and it only wakes when every reason clears. That is why they compose
with each other, with EMP, and with vanilla `PoweredUnit` instead of fighting over
the flag. A techno is never revived by us unless *we* were the one that darkened it,
and never while EMP still holds it.

> Keep a unit on **either** this system **or** vanilla `PoweredUnit=`, not both.

### Power — child powers parent
```ini
[SomeAttachmentType]
PowersParent=yes        ; host is dark unless this slot has an active child
```
Works on **vehicle and building hosts**.

### Power — sibling powers sibling
A "sibling" is another attachment slot on the same parent.
```ini
; --- on the CONSUMER (the attachment that can go dark) ---
[TurretAttachment]
Powered=yes                    ; dark unless an eligible sibling source powers it
Powered.Type=GENERATOR,REACTOR ; optional: ONLY these source child-types power me

; --- on the SOURCE (the attachment that powers its siblings) ---
[GeneratorAttachment]
PowersSiblings=yes             ; powers all NON-picky sibling consumers
PowersSiblings.Type=RADAR,SAM  ; and/or: powers consumers whose child is one of these
PowersSiblings.Index=2,3       ; and/or: powers the consumers at these slot indices
```
- Several source tags **union** — a sibling is powered if it matches **any**.
- A consumer that sets `Powered.Type` is **picky**: the vague `PowersSiblings=yes`
  no longer satisfies it; it needs a source that both targets it *and* is of an
  accepted type.
- Singular spellings (`PowersSibling`, `PowersSibling.Type`, `PowersSibling.Index`)
  are accepted as aliases.

### Power — parent powers child (reverse)
```ini
[SomeAttachmentType]
PoweredByParent=yes     ; child is dark while its parent is gone or itself dark
```
Darkness **propagates down** the attachment subtree, so powering down a host takes
its attached pieces with it.

### Requirement gating (open-topped style)
```ini
[TurretAttachment]
RequiresPassengers=1        ; dark unless the parent carries >= N passengers
RequiresSlot.Index=0,1      ; dark unless one of these parent slots has an ACTIVE child
RequiresSlot.Type=GUNNER    ; dark unless the parent has an active child of these types
```
`Index`/`Type` **union** (any match satisfies); `RequiresPassengers` is a separate
AND gate.

---

## Presence gating (prerequisites)

These control whether the child is **present at all** (hidden/shown live when
`Prerequisite.Dynamic=yes`, the default) rather than dark.

```ini
[SomeAttachmentType]
Prerequisite=GAPILL                 ; all listed buildings must be present
Prerequisite[0]=NAPOWR,NAREFN       ; alternative OR-lists (Ares style)
Prerequisite.Negative=GATECH        ; blocked while ANY listed building is present
RequiredHouses=Americans,British    ; host owner's country must be listed
ForbiddenHouses=Russians            ; host owner's country must NOT be listed
Prerequisite.Dynamic=yes            ; live hide/show vs. one-time gate at spawn

; sibling prerequisites -- singular = ANY (OR), plural = ALL (AND)
Prerequisite.Sibling.Index=1
Prerequisite.Sibling.Type=HTNK
Prerequisite.Siblings.Index=0,2
Prerequisite.Siblings.Type=HTNK,GGI

; host veterancy -- with Prerequisite.Dynamic this IS veterancy attach/detach
Prerequisite.MinRank=veteran        ; rookie | veteran | elite
Prerequisite.MaxRank=elite

; host health percentage (0-100) -- for pristine vs. damaged/wrecked variants
Prerequisite.MinHealth=50
Prerequisite.MaxHealth=100
```

Example — a piece that only exists once the host is **veteran or better**, and is
replaced by a wrecked version below half health:
```ini
[IntactArmor]
Prerequisite.MinRank=veteran
Prerequisite.MinHealth=50

[WreckedArmor]
Prerequisite.MinRank=veteran
Prerequisite.MaxHealth=49
```

---

## Presentation / behaviour

```ini
[SomeAttachmentType]
Decorative=yes          ; profile: forces the "cosmetic piece, not a unit" bundle --
                        ; PassSelection + TransparentToMouse + LowSelectionPriority
                        ; + no cell occupation
PassSelection=no        ; clicking the child selects the HOST instead
TransparentToMouse=no   ; child is invisible to mouse picking; clicks pass through
YSortPosition=default   ; default | underparent | overparent -- draw order vs. host
InheritHeightStatus=yes ; child reports the host's on-floor/in-air/surfaced state
InheritCommands.StopCommand=yes
InheritCommands.DeployCommand=yes
```

Per-slot variants exist for the prerequisite family and `YSortPosition`, e.g.:
```ini
[SomeParentTechnoType]
Attachment0.Type=TestAttach
Attachment0.TechnoType=HTNK
Attachment0.FLH=100,0,0
Attachment0.YSortPosition=overparent
Attachment0.Prerequisite.Negative=NAWEAP
Attachment0.Prerequisite.Dynamic=no
```
