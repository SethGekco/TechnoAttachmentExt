# H1 — Instant spawn (design, not yet implemented)

Immediate placement of objects at the firer, at its target, or at a point derived
from either, with configurable animations. Distinct from vanilla `Spawns=`, which
launches aircraft from a bay over time via `SpawnManagerClass`.

Status: **design for review**. Nothing here is built.

---

## 1. The shape of the feature

An **instant spawn rule** is a trigger, a place, a payload, and a set of effects:

```
trigger  ──▶  resolve location  ──▶  resolve count  ──▶  place N objects  ──▶  anims
  §3             §4                     §5                  §6                §7
```

Every stage is independently configurable, and every stage has a "what if it
can't" answer (§6.2 especially — that is the stage with teeth).

### Indexed and unindexed groups

Following the `ExperienceTo` precedent: an unindexed group plus independent
`[0]`, `[1]`, … groups. **The unindexed group and `[0]` are separate rules, not
aliases**, so both can be used together.

```ini
InstantSpawn=DRON                 ; unindexed rule
InstantSpawn.On=fire
InstantSpawn[0]=TERROR,TERROR     ; a second, independent rule
InstantSpawn.On[0]=destroyed
```

Available on **TechnoType**, **AttachmentType**, and per slot as
`AttachmentN.InstantSpawn*`, resolving in the documented order
TechnoType → AttachmentType → `AttachmentN.*`.

---

## 2. Vocabulary reuse — deliberate

FreeUnitExt already ships a delivery vocabulary that modders will have learned:
`.Facing=`, `.Cell=`, `.Spacing=`, `.Range=`, `.Anim=`, `.Mission=`, `.Owner=`,
`.Script=`, `.Team=`, `.Limbo=`, with `Owner=` taking
`Invoker|Civilian|Special|Neutral|Random|RandomAlly|RandomEnemy`.

**H1 reuses those key names and value spaces exactly.** A modder who knows
`FreeUnit.Facing=random` should be able to write `InstantSpawn.Facing=random`
and be right. Inventing a third dialect for "put an object on the map" is the
single easiest way to make three good DLLs feel like three unrelated hacks.

Where H1 needs something FreeUnitExt has no concept of (a target, a cooldown, a
count that scales with ammo) it adds new keys rather than redefining old ones.

---

## 3. Trigger — `InstantSpawn.On=`

| Value | Fires when | Notes |
|---|---|---|
| `fire` *(default)* | the owner fires a weapon | filterable by `.On.Weapon=` (index) |
| `hit` | the owner's projectile detonates | location defaults to the impact point |
| `created` | the attachment child is created | AttachmentType/slot rules only |
| `destroyed` | the owner or child dies | the "it bursts into swarm" case |
| `detached` | the child is detached | distinct from `destroyed` |
| `timer` | every `.On.Rate=` frames | a passive generator |
| `deploy` | the deploy gesture | pairs with the existing Deploy work |
| `manual` | never automatically | for a future hotkey/SW driver |

Multiple triggers per rule via a comma list (`InstantSpawn.On=fire,deploy`).

Gates that apply to every trigger:

| Tag | Default | Meaning |
|---|---|---|
| `InstantSpawn.Cooldown=` | `0` | frames before this rule may fire again |
| `InstantSpawn.Chance=` | `100` | percent, synced RNG |
| `InstantSpawn.ConsumeAmmo=` | `0` | ammo spent per activation; blocks if short |
| `InstantSpawn.RequiresSlot.Index=` | none | reuses the B1 requirement primitive |
| `InstantSpawn.RequiresSlot.Type=` | none | ditto |

**Open question 1.** `hit` is arguably a *warhead* feature, not a techno feature —
it belongs on the projectile that lands, not the unit that fired. WeaponExt is
the more natural home. Recommend **cutting `hit` from H1** and revisiting it as a
warhead tag; keeping it here means duplicating detonation plumbing.

---

## 4. Location — `InstantSpawn.At=`

| Value | Meaning |
|---|---|
| `self` *(default)* | the owner's own cell |
| `target` | the owner's current target's cell |
| `parent` / `child` | the other end of the attachment link |
| `cell` | an absolute offset from `self`, via `.Offset=` |

Modifiers, all reusing FreeUnitExt semantics where they exist:

| Tag | Default | Meaning |
|---|---|---|
| `InstantSpawn.FLH=` | `0,0,0` | offset from the anchor, in the owner's local frame |
| `InstantSpawn.Cell=` | nearest free | `N NE E SE S SW W NW` / `random` — which side |
| `InstantSpawn.Spacing=` | `0` | cells left empty between consecutive objects |
| `InstantSpawn.Range=` | `1` | how far from the anchor placement may search |
| `InstantSpawn.Scatter=` | `0` | random cell offset up to N, synced RNG |
| `InstantSpawn.Facing=` | owner's | direction / `random` / `0`-`255` |

`At=target` with no target: governed by `.NoTarget=skip|self` (default `skip`).

---

## 5. Count — `InstantSpawn.Count=`

Fixed base plus scaling terms, so the count can ride the foundations H2/H3 built:

| Tag | Default | Meaning |
|---|---|---|
| `InstantSpawn.Count=` | `1` | base number of objects |
| `InstantSpawn.Count.PerSlot=` | `0` | + N per **active** attachment slot (F0) |
| `InstantSpawn.Count.PerSlotType=` | none | restrict which child types count |
| `InstantSpawn.Count.PerAmmo=` | `0` | + N per round of current ammo |
| `InstantSpawn.Count.PerRank=` | `0` | + N per veterancy rank |
| `InstantSpawn.Count.Max=` | `0` | `0` = uncapped; else clamp the total |
| `InstantSpawn.Max=` | `0` | `0` = uncapped; max ALIVE from this rule at once |

`InstantSpawn.Max` needs per-instance bookkeeping of what this rule spawned —
see §8.

## 5.1 Payload selection — `InstantSpawn.Mode=`

| Value | Meaning |
|---|---|
| `all` *(default)* | spawn the whole list, `Count` times over |
| `random` | pick one at random per object, synced RNG |
| `weighted` | as `random`, using `.Weights=` |
| `cycle` | step through the list across activations |

---

## 6. Placement

### 6.1 The object

| Tag | Default | Meaning |
|---|---|---|
| `InstantSpawn.Owner=` | `Invoker` | same value space as `FreeUnit.Owner=` |
| `InstantSpawn.Mission=` | `Hunt` for units | starting mission |
| `InstantSpawn.Script=` / `.Team=` | none | as FreeUnitExt |
| `InstantSpawn.Veterancy=` | `0` | rank to grant |
| `InstantSpawn.Health=` | `100` | percent of max |
| `InstantSpawn.Lifetime=` | `0` | `0` = permanent; else frames before it dies |
| `InstantSpawn.Attach=` | `no` | attach the new object to the spawner as a child |

`InstantSpawn.Attach=yes` is the interesting one: it makes instant spawn a way to
*grow attachments at runtime*, which no current tag can do. It needs a free slot;
`.Attach.Slot=` picks one, else the first free.

### 6.2 When the cell is not free — `InstantSpawn.OnBlocked=`

**This is the stage that requires care, not the fun one.**

| Value | Meaning |
|---|---|
| `nearest` *(default)* | search outward up to `.Range=` for a free cell |
| `skip` | do not spawn this object |
| `stack` | place anyway, accepting overlap |
| `destroy` | place anyway and let the engine resolve it |

**Why this matters more than it looks.** A failed placement in this engine is
*destructive* — the engine destroys the object rather than leaving it half
placed. That destruction runs our own removal hooks re-entrantly and can null
pointers held by the caller. That is exactly the mechanism behind the
`AttachmentClass::AI()` null-`Child` crash (see the encyclopedia's
`Techno-Instance-Lifecycle.md`).

Implementation rules, to be treated as binding rather than advisory:

1. Every placement call is assumed to be able to destroy the object. Re-validate
   **every** pointer held across it — the spawner, the list being iterated, and
   the object itself.
2. Never place while iterating a live engine collection. Build a plan, then
   execute it.
3. `stack` and `destroy` are opt-in for a reason and will be documented as
   "may destroy the object; that is the point".

### 6.3 Determinism

`Chance`, `Scatter`, `random` facing and `random`/`weighted` selection all use
`ScenarioClass::Random`, never a render-side or hashed source. Cell search order
is a fixed spiral, so ties resolve identically on every peer. `cycle` state is
per-instance and **must be serialized** (§8).

---

## 7. Animations — `InstantSpawn.Anim*`

| Tag | Where it plays |
|---|---|
| `InstantSpawn.Anim.Source=` | on the spawner |
| `InstantSpawn.Anim.Dest=` | at the resolved location, once |
| `InstantSpawn.Anim.PerObject=` | at each object's own cell |
| `InstantSpawn.Anim.Blocked=` | when placement fails |
| `InstantSpawn.Anim.Owner=` | as `FreeUnit.Anims.Owner=` |
| `InstantSpawn.Anim.RequireClear=` | as `FreeUnit.Anims.RequireClear=` |

Lists are allowed; one is picked per activation with synced RNG. Animations are
render objects but their *creation* is synced state, so they are created
unconditionally on every peer.

---

## 8. New runtime state (and therefore new save/load surface)

Three of the above need per-instance state, which means serialization — the same
trap `Move.*` hit:

| State | For | Serialized |
|---|---|---|
| cooldown timer, per rule | `.Cooldown=` | yes |
| cycle position, per rule | `.Mode=cycle` | yes |
| list of live spawned objects, per rule | `.Max=` | yes, and needs `InvalidatePointer` |

The third is the expensive one. It needs a pointer list per rule per instance,
`InvalidatePointer` participation, and — per the container invalidate-gate trap —
the `ExtContainer` must override `InvalidateExtDataIgnorable`, or the
invalidation never runs at all.

**Recommendation: ship `.Max=` in a second pass.** Everything else in H1 is
stateless or a single integer. `.Max=` alone drags in pointer tracking,
invalidation and a save/load contract, and it is the least essential tag in the
set.

---

## 9. Cross-project boundary — needs a decision before coding

Three DLLs now place objects on the map:

| DLL | Trigger | Primitive |
|---|---|---|
| FreeUnitExt | a building is created | delivery entry |
| PayloadExt | cargo / bay / gunner | bay |
| TechnoAttachmentExt (H1) | an action (fire, death, timer) | instant spawn |

The ROADMAP already says the shared spawner primitive should be built **here**.
That is still the right call — H1 is the most general of the three — but it is
worth being honest that "shared" currently means "same tag names, three
implementations". A genuinely shared implementation would need a common library,
and the project decided against a shared lib for the Squad/Attachment/GiftBox
split.

**Recommendation:** keep three implementations, hold the vocabulary identical,
and write the delivery semantics down once (here) so the other two can converge
on it. Revisit an actual shared library only if a fourth consumer appears.

---

## 10. Suggested build order

1. **H1a — core.** `On=fire|timer|created|destroyed`, `At=self|target`, `Count=`,
   `OnBlocked=`, `Owner/Facing/Mission`. The whole feature for most mods.
2. **H1b — anims.** All four anim seats.
3. **H1c — scaling.** `Count.PerSlot/PerAmmo/PerRank`, `Mode=`, `Weights=`.
4. **H1d — attach.** `InstantSpawn.Attach=`, runtime attachment growth.
5. **H1e — `.Max=`.** Only with the pointer-tracking and invalidation work.

H1a is the honest MVP. H1d is the one with the most new capability per line, and
H1e is the one most likely to introduce a save/load or invalidation bug.

---

## 11. Open questions for Rex

1. **Cut `hit`?** It reads as a warhead feature and would fit WeaponExt better.
2. **Is `Attach=yes` wanted?** It is the only route to runtime attachment growth,
   but it means an attachment slot can be filled by something other than the INI.
3. **Default `OnBlocked`** — `nearest` is forgiving, `skip` is predictable.
   Recommend `nearest` with `Range=1`.
4. **Does `destroyed` fire for every death**, or only combat deaths? Vanilla's
   destructor runs for sold/undeployed/transformed too, so "bursts into swarm"
   would trigger on selling the unit unless filtered.
