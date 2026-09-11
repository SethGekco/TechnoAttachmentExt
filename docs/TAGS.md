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

### Power — external structure (vanilla `PowersUnit` style)
The classic `[GAROBO]PowersUnit=[ROBO]` relationship, but expressed on the
**consumer** so it can differ per slot. Unlike `Prerequisite=` (which *hides* the
child), this only darkens it.

**Works on any techno.** Put it on a plain `[SomeUnit]` or `[SomeBuilding]`
TechnoType and that unit/building goes dark without the structure — it is not
attachment-only. On an attached child the resolution order still applies:
**TechnoType → AttachmentType → `AttachmentN.*`**.

```ini
[SomeAttachmentType]
PoweredBy=GAROBO,GATECH     ; dark unless the host's owner has one working
PoweredBy.RequireAll=no     ; no (default) = ANY of them; yes = ALL of them
PoweredBy.RequirePower=yes  ; the building must itself be online (default)
PoweredBy.Range=0           ; 0 = house-wide (vanilla); >0 = within N cells
PoweredBy.House=owner       ; whose buildings count (comma list, default owner)
```
`PoweredBy.House` relations:

| value | meaning |
|---|---|
| `owner` (`self`) | the host's own house — **default**, matches vanilla |
| `ally` | allied, excluding self |
| `team` | same multiplayer team (`TournamentTeamID`) |
| `enemy` | not allied, excluding passive/neutral houses |
| `neutral` | the Neutral country |
| `civilian` | `MultiplayPassive` countries |
| `special` | the Special country |
| `any` (`all`) | anybody at all |

Combine them: `PoweredBy.House=owner,ally` — powered by your own *or* an ally's
structure. `PoweredBy.House=enemy` gives you "runs on a captured/nearby enemy
facility", which pairs well with `PoweredBy.Range=`.

Two edges worth knowing:
- **Passive countries are excluded from `enemy`**, so neutrals/civilians never read
  as hostile — ask for them by name.
- **`team` needs a real team game** (non-zero `TournamentTeamID`); two teamless
  houses do *not* count as team-mates. In a plain skirmish use `ally`.
Per-slot, so one AttachmentType can be powered by GAROBO on one slot and not on
another:
```ini
[SomeParentTechnoType]
Attachment0.Type=Turret
Attachment0.PoweredBy=GAROBO      ; this one needs the robot control centre
Attachment1.Type=Turret           ; same AttachmentType...
                                  ; ...this one is unconditional
```
- **"Working"** = alive, on the field, `HasPower`, and not itself dark — so a
  shut-down or low-power plant stops powering things.
- **Owner only by default** — an ally's GAROBO does not run your robot tanks,
  matching vanilla. Widen it with `PoweredBy.House=` above.
- Composes with the other power gates through the same arbiter: a child can require
  a sibling generator *and* an external structure, and it goes dark if either fails.

---

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
PowerConsumer.House=owner,ally      ; whose network may power me (default owner,ally)
;PowerSource.House=owner,ally       ; ...and on the source: whom it will power
```
- A relay only re-broadcasts while it is itself reached, so a chain of pylons
  carries power outward from the source — cut one and everything past it goes dark.
- **`.House` uses the same vocabulary as `PoweredBy.House`** (owner/ally/team/enemy/
  neutral/civilian/special/any). Both directions must accept — the consumer says
  whose network may power it, the source says whom it is willing to power, exactly
  like the `.Types` filters. Default `owner,ally`.
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

### Ammo capacity from attachments
```ini
[SomeAttachmentType]
Ammo.Parent=2      ; while this child is active, the HOST holds +2 max ammo
```
Per-slot too (`AttachmentN.Ammo.Parent=`). Bonuses from several **active** slots
sum; a hidden or dead child stops contributing.

This is real **capacity**, not a top-up: the host reloads up to `base + bonus` and
only counts as full there. (A pure "give it N rounds now" tag would be almost
pointless — units auto-reload, so the rounds arrive anyway.)

`Ammo=` capacity lives on the *TechnoType* and is shared by every unit of that
type, so it cannot simply be written — the value is substituted where the engine
reads it: the two reload-path reads (which govern capacity) plus the pip-max read
(so the **pips show the extra rounds** too, added 2026-09-02).

> Remaining approximation: other readers of the capacity still see the base
> number. Nothing player-visible is known to be affected — firing, reloading,
> running dry and the pip display are all correct. Report anything that looks off.


### Motion — spin and bob
```ini
[SomeAttachmentType]
Spins=yes            ; the child rotates continuously
Spins.Period=32      ; frames per full revolution; NEGATIVE = anticlockwise
Spins.Orbit=no       ; no  = spins on the spot (centre of rotation = the child)
                     ; yes = the FLH offset sweeps around the parent instead
                     ;       (centre of rotation = the parent)

Slides=yes           ; travels back and forth along one axis
Slides.Axis=x        ; x | y | z
Slides.Range=256     ; leptons either side of the FLH point (sweep = 2 x Range)
Slides.Period=60     ; frames per full there-and-back cycle
Slides.Phase=0       ; 0-255, as Bobs.Phase

Bobs=yes             ; vertical oscillation
Bobs.Amplitude=48    ; leptons above/below the FLH point
Bobs.Period=45       ; frames per full up-down cycle
Bobs.Phase=0         ; 0-255; shifts the cycle so sibling attachments bob out of step
```
All per-slot too (`AttachmentN.Spins=` etc).

- **Period is in frames**, so smaller = faster. `Spins.Period=32` is one revolution
  every 32 logic frames; `-32` spins the other way.
- **Spin vs orbit** are the "centre of rotation" control: spin alone turns the
  child where it stands, `Spins.Orbit=yes` also swings its offset around the host.
  Use both together for a piece that circles the host while facing outward.
- **`Bobs.Phase`** is what stops a row of attachments bobbing in lockstep — give
  each slot a different value (e.g. 0, 64, 128, 192). `Slides.Phase` does the same.
- **The slide axis is host-relative**, because the FLH offset is already resolved
  through the host's transform. A sliding turret therefore tracks the machine's
  surface as it turns, instead of drifting out of alignment in world space.
- Order of composition: **orbit → slide → bob**, so a slide follows a rotated frame
  rather than fighting it.

> These are safe online: the motion is a pure function of the synced frame counter,
> computed with integer maths and a fixed-point sine table (never `std::sin`, never
> wall-clock), so every peer produces the same facing and position. It also means
> there is no extra saved state and no drift across save/load.

> Note this is the *attachment* half of the motion work. Spinning the body/turret/
> barrel of a **non-attachment** unit is render-side and still blocked, together
> with translucency.

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

Essentially everything: `PowersParent`, `Powered`, `PowersSibling(s)`,
`PoweredByParent`, `RequiresPassengers`, `PassSelection`, `TransparentToMouse`,
`YSortPosition`, the whole prerequisite family, `PoweredBy` (+ `.RequireAll`,
`.RequirePower`, `.Range`, `.House`), the list-valued companions
(`Powered.Type`, `PowersSiblings.Type/.Index`, `RequiresSlot.Index/.Type`),
`Prerequisite.MinRank/.MaxRank/.MinHealth/.MaxHealth`,
`Prerequisite.Sibling(s).Index/.Type`, and the behaviour family:
`RespawnAtCreation`, `RespawnDelay`, `InheritCommands.StopCommand/.DeployCommand`,
`InheritOwner`, `InheritStateEffects`, `InheritDestruction`,
`InheritHeightStatus`, `OccupiesCell`, `LowSelectionPriority`, `Decorative`,
`DestructionWeapon.Child/.Parent`, `ParentDestructionMission`,
`ParentDetachmentMission`, `Convert.KeepHealth/.KeepVeterancy`.

> Only the **rule groups** — `ExperienceTo[N]` and `Convert[N]` — remain
> AttachmentType-level, since they are whole rule lists rather than single values.

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

---

## Boolean values — what actually parses

Phobos's bool parser looks at the **first character only**, case-insensitively:

| accepted | means |
|---|---|
| `1`, `t…`, `y…` | **true** (`true`, `yes`) |
| `0`, `f…`, `n…` | **false** (`false`, `no`) |

So **`=false` and `=no` are exactly equivalent**, as are `=true` and `=yes`.

Two consequences worth knowing:
- **`on` / `off` do NOT parse.** `o` matches neither list, so the read *fails*, the
  tag keeps its **default**, and a parse error is logged. A tag written `=off`
  therefore looks exactly like "this feature does nothing".
- Only the first letter is checked, so `yellow` reads as **true** and `frog` as
  **false**. Typos can silently parse as a valid value.

A failed parse always logs, so when a tag seems inert, grep `debug.log` for the
key name before suspecting the feature.

### Reactive motion (`Move.*`)

Lets an attachment stray from its FLH anchor to keep a useful distance from its
target — a turret that creeps forward until it can shoot, a drone that backs off
when something gets close. Unlike `Spins`/`Slides`/`Bobs` (which are pure
functions of the frame counter), this eases toward a goal, so the current stray
is real saved state and survives save/load.

| Tag | Default | Meaning |
| --- | --- | --- |
| `Move.Radius` | `0` | Max stray from the anchor, in leptons (256 = one cell). `0` disables the feature. |
| `Move.Mode` | `approach` | `approach` \| `retreat` \| `hold` \| `maintain` |
| `Move.Range` | `0` | Desired distance to the target. `0` = the child's own primary weapon range. |
| `Move.Speed` | `8` | Leptons per frame, so it eases rather than snapping. |

Also available per slot as `AttachmentN.Move.Radius`, `AttachmentN.Move.Mode`,
`AttachmentN.Move.Range`, `AttachmentN.Move.Speed` (slot wins over AttachmentType).

- `approach` **only closes in**. It moves toward the target until it reaches
  `Move.Range` and then stops; it never backs off. (It used to also retreat when
  closer than the range, which read as "the drone flies away from its target" —
  the common case, since the host usually engages from inside the child's range.)
- `maintain` is the old ring behaviour, kept as its own mode: creeps out when too
  far *and* pulls back in when too close, settling at `Move.Range`.
- `retreat` simply backs away from the target as far as `Move.Radius` allows.
- `hold` (and *any* mode with no target) drifts back to the anchor.

A leashed child also gets its **reach extended for targeting purposes**: an
attachment normally drops any target it isn't already in range of (it can't path,
so it must not try to close the distance). With `Move.Radius` set it keeps targets
within `weapon range + Move.Radius` and walks into range instead.

The target is the **child's own** target if it has one, otherwise the host's.
The offset is world-space and horizontal only — it points at the enemy regardless
of which way the hull faces, and it won't try to climb toward an aircraft
(vertical motion stays `Bobs`'s job).

```ini
[SomeAttachment]
Move.Radius=256      ; may stray one cell
Move.Mode=approach
Move.Range=0         ; = its own weapon range
Move.Speed=6
```

### `Spins.Facing`

| Tag | Default | Meaning |
| --- | --- | --- |
| `Spins.Facing` | `yes` | Whether `Spins` also turns the sprite. |

`Spins` drives two things: the orbit (with `Spins.Orbit=yes`) and the child's own
facing. `Spins.Facing=no` separates them — the attachment circles the parent
without pirouetting, for a drone that keeps a fixed heading while orbiting.
Per slot: `AttachmentN.Spins.Facing`.

### `Prerequisite.LostAction`

What happens when a **dynamic** prerequisite (`Prerequisite.Dynamic=yes`) stops
being met. The old behaviour — silently blinking out of existence — is still the
default, but it reads as a bug in game.

| Value | Behaviour |
| --- | --- |
| `hide` (default) | Limbo'd; reappears when the prerequisite returns. |
| `kill` | Dies properly: death animation, debris, `DestructionWeapon.Child`. |
| `vanish` | Removed silently, no death effects. Permanent. |
| `detach` | Becomes a free-standing unit and goes its own way. |
| `deactivate` | Stays on the field but goes dark; wakes when the prerequisite returns. |

`kill`, `vanish` and `detach` leave the slot empty, so `RespawnDelay` (if set)
decides whether a fresh child appears once the prerequisite comes back.
`deactivate` goes through the same arbiter as the power gates, so it composes with
them rather than fighting over the deactivated flag.
Per slot: `AttachmentN.Prerequisite.LostAction`.

```ini
[DroneAttachment]
Prerequisite=GAROBO
Prerequisite.Dynamic=yes
Prerequisite.LostAction=kill   ; the drone falls out of the sky when the hub dies
RespawnDelay=150               ; ...and a new one launches if you rebuild it
```
