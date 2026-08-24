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

### Power network — radius, relay chains, capacity
These go on **regular TechnoTypes** (infantry, vehicles, aircraft, buildings), not
just attachments.

```ini
; --- a power source ---
[GAPOWR]
PowerSource=yes
PowerSource.Range=10                ; cells
PowerSource.Types=ROBO,SOMEUNIT     ; optional: only these consumer types
PowerSource.Count=4                 ; optional cap; 0/unset = unlimited
PowerSource.OverflowMode=excess     ; excess (default) = only those past the cap go
                                    ; dark;  all = the source's WHOLE group goes dark

; --- a relay ("power line" pylon): extends the network ---
[POWERPYLON]
PowerRelay=yes
PowerRelay.Range=8                  ; unset = inherit the range of whatever feeds it

; --- a consumer ---
[ROBO]
PowerConsumer=yes
PowerConsumer.Types=GAPOWR          ; optional: only these source types satisfy it
```
- A relay only re-broadcasts while it is itself reached, so a chain of pylons
  carries power outward from the source — cut one and everything past it goes dark.
- Only **allied** networks power a techno; an enemy plant won't light up your units.
- Capacity is counted **per source**, including consumers reached through its relays.
- Solved once per frame; **fails safe** — if the solver never runs, consumers stay
  powered rather than all going dark.

> Keep a unit on **either** this system **or** vanilla `PoweredUnit=`, not both.

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

### Experience: income multiplier + passing

**Income multiplier — on any TechnoType** (units, buildings, and attachment
children alike, since a child is just a TechnoType):
```ini
[SomeUnit]
Experience.Multiplier=0.5   ; earns 50% XP. 1.0 = vanilla, 0 = earns none
```
Applied the moment XP lands, **before** any sharing — so everything downstream
works from the multiplied figure.

**Passing — on AttachmentType, in independent rule groups.** One unindexed group
plus contiguous `[0]`, `[1]`, … The unindexed group and `[0]` are **separate
rules, not aliases**, so you can use both:
```ini
[SomeAttachmentType]
ExperienceTo=parent            ; unindexed group
ExperienceTo.Share=100
ExperienceTo.Drain=no

ExperienceTo[0]=children       ; a second, independent group
ExperienceTo.Share[0]=50
ExperienceTo.Drain[0]=yes

ExperienceTo[1]=child          ; a third: one specific slot
ExperienceTo.Slot[1]=2         ; which slot the singular child/sibling means
;ExperienceTo.ID[1]=SomeSlotID ; ...or name the slot by its ID (wins over .Slot)
```
Relations: `parent` | `root` | `child` | `sibling` | `children` | `siblings`.
- `parent` = one level up; `root` = all the way to the top of the chain (they
  differ only when attachments are **nested**).
- Singular `child`/`sibling` take **one** slot, chosen by `.Slot`/`.ID`; the plural
  forms take **every** slot.
- `self` is accepted by the parser but does nothing here — an earner can't pay
  itself.

`Drain=no` (default) **copies** XP — the recipient gains and the earner keeps its
own. `Drain=yes` **moves** it: the earner loses what that group paid out, summed
across draining groups and clamped so it can never lose more than it earned that
tick. Multiplier and Drain are different knobs — the multiplier decides how much
is earned at all, Drain decides copy-vs-move per group.

Detected by watching veterancy each synced tick, so it catches XP from **any**
source (kills, crates, script). De-vet is not propagated.

### Convert-in-place (upgrades / damaged variants)
The slot keeps its index and its adoption by the parent while the **child swaps to
a different TechnoType**. Same group syntax as `ExperienceTo`: an unindexed group
plus contiguous `[0]`, `[1]`, … (separate rules, not aliases).

```ini
[SomeAttachmentType]
Convert=DAMAGEDHULL          ; become this while the window holds
Convert.MaxHealth=66         ; host health percentage window (0-100)
Convert.MinHealth=

Convert[0]=WRECKEDHULL       ; a second, more specific rule
Convert.MaxHealth[0]=33

Convert[1]=VETERANHULL       ; conditions can be rank instead
Convert.MinRank[1]=veteran   ; rookie | veteran | elite
Convert.MaxRank[1]=elite

Convert.KeepHealth=yes       ; carry the damage RATIO across (default yes)
Convert.KeepVeterancy=yes    ; carry veterancy across (default yes)
```
- **First matching rule wins**, so order matters — put the most specific rule
  (lowest health) first.
- When **no** rule matches, the slot reverts to its configured `AttachmentN.TechnoType`.
  Reverting is automatic; there is no revert flag.
- Conditions read the **host**, matching `Prerequisite.MinHealth`/`.MinRank`.
- The swap is **quiet** — a replacement is not a death, so no destruction weapons,
  no `ParentDestructionMission`, no kill credit.
- `KeepHealth` carries a **percentage**, so a replacement with different `Strength`
  keeps the same damage ratio (clamped so it never arrives pre-dead).

> Versus the two-slot prerequisite trick (`IntactArmor` + `WreckedArmor`): converting
> keeps **one** slot and carries state across, so use it for upgrade chains and
> progressive damage. Use two slots when you want both variants to be able to exist.

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

### Per-slot overrides
`AttachmentN.<tag>` on the host **overrides** the AttachmentType, so one
AttachmentType can be reused across slots that need different behaviour
(precedence: TechnoType → AttachmentType → `AttachmentN.*`). Available for:

`PowersParent`, `Powered`, `PowersSibling(s)`, `PoweredByParent`,
`RequiresPassengers`, `PassSelection`, `TransparentToMouse`, `YSortPosition`,
and the prerequisite family.

> The **list-valued** companions (`Powered.Type`, `PowersSiblings.Type/.Index`,
> `RequiresSlot.*`) are still AttachmentType-level only.

Example:
```ini
[SomeParentTechnoType]
Attachment0.Type=TestAttach
Attachment0.TechnoType=HTNK
Attachment0.FLH=100,0,0
Attachment0.YSortPosition=overparent
Attachment0.Prerequisite.Negative=NAWEAP
Attachment0.Prerequisite.Dynamic=no
```
