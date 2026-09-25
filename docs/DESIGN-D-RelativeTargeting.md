# D — Attachment-relative targeting (design, not yet implemented)

The last parked roadmap item. Two distinct features that got filed together
because both involve "the thing hit is not the thing aimed at":

* **D1a — fixed-relative targeting.** A weapon that always fires at a relative of
  the firer (itself, its parent, a child, a sibling) regardless of what was
  clicked. The repair drone that heals its own host; the overcharge that damages
  the thing it is bolted to.
* **D1b — impact redirect.** You target the parent, but the damage lands on its
  children instead. Armour plating that eats hits meant for the hull.

Status: **D1a and D1b are BUILT.** The WeaponExt handoff (splash-onto-relatives)
is still design.

D1b shipped WITHOUT first answering the legality question below, because the
design changed to make that question moot: only `LegalTarget=yes` children may
intercept, so the engine is never handed an illegal target and how it would react
stops mattering. Same move as the ordering-independent save/load restore --
when a blocking unknown can be designed around, that beats waiting on it.

---

## 1. The open question, answered

The roadmap left one thing undecided: *does the redirect happen at fire-time or
at bullet-detonate?*

**Fire-time belongs here. Detonate-time belongs to WeaponExt.** Same split, and
for the same reason, as the `hit` trigger handed over in
`DESIGN-H1-InstantSpawn.md` §3.1:

| | fire-time | detonate-time |
|---|---|---|
| What it knows | the firer, its attachment graph, the intended target | the impact point and the victim it actually reached |
| Whose subject | the **firer** — an attachment property | the **projectile** — a warhead property |
| Seats needed | `TechnoClass::Fire`, which we already hold at `0x6FDD77` | bullet detonation, which is WeaponExt's territory |
| Survives the firer dying? | irrelevant, it has already resolved | must, so it cannot depend on firer state |

D1a is unambiguously fire-time: "always shoot my parent" is decided before a
projectile exists.

D1b is the interesting one, and **fire-time covers the useful case**. Retargeting
the shot at a child means the projectile genuinely flies at the child and the
engine's own damage, armour and warhead handling all apply unchanged. The
detonate-time variant — land on the parent, then *spread* damage onto children —
is a genuinely different effect (splash-onto-relatives) and should be a warhead
tag, not a firer tag.

**Recommendation: build fire-time redirect here; hand splash-onto-relatives to
WeaponExt** alongside `SpawnOnDetonate`. Two tags, two homes, no overlap.

---

## 2. D1a — fixed-relative targeting

```ini
[SomeAttachment]
Weapon.TargetsRelative=parent     ; self | parent | root | child | sibling | none
Weapon.TargetsRelative.Slot=0     ; which child/sibling, 0-based
Weapon.TargetsRelative.ID=        ; ...or address the slot by its ID
Weapon.TargetsRelative.Index=-1   ; only this weapon index; -1 = all weapons
Weapon.TargetsRelative.OnMissing=hold   ; hold | normal
```

Relations reuse **F0b** (`TechnoExt::ResolveRelative`) exactly as `ExperienceTo`
does — same vocabulary, same slot/ID addressing, no second dialect.

`OnMissing` decides what happens when the relative does not exist (no child in
that slot, no parent):

* `hold` *(default)* — do not fire at all. A repair drone with nothing to repair
  should sit idle, not shoot whatever is nearby.
* `normal` — fall through to ordinary targeting.

**Seat.** `TechnoClass::Fire` at `0x6FDD77`, which this DLL already owns for the
`InstantSpawn` fire trigger. The target argument is reachable there (`[ebp+0x8]`,
read off the function rather than assumed). Substituting it before the original
runs means the engine builds the projectile against the substituted target and
everything downstream — range, armour, warhead — behaves normally.

⚠ That seat's stolen bytes are a **relative branch**, so the hook must never
return 0. The existing code already handles this; any addition must keep it.

**Ordering note.** `CanFire`/`GetFireError` run *before* `Fire` and will have
judged the ORIGINAL target. A weapon that always hits its own parent at zero
range may be refused for being too close, or for targeting an ally. This is the
part most likely to need a second seat, and it should be found by testing rather
than pre-emptively hooked.

---

## 3. D1b — fire-time impact redirect

```ini
[SomeAttachment]
Redirect.Incoming=children        ; children | child | sibling | parent | none
Redirect.Incoming.Slot=0
Redirect.Incoming.Chance=100      ; percent, synced RNG
Redirect.Incoming.Mode=first      ; first | random | healthiest | weakest
```

Declared on the **target's** attachment config, not the shooter's — it is a
property of the thing being shot at ("my plating takes hits for me"), and the
shooter should not have to know.

`Mode` picks among several eligible children: `first` by slot order (fully
deterministic), `random`/`healthiest`/`weakest` for variety. All read synced
state; `random` uses `ScenarioClass::Random`.

**Seat.** The same `0x6FDD77`, from the other side: when the resolved target is a
techno whose attachment config declares `Redirect.Incoming`, swap in the chosen
child before the projectile is built.

**Why not at detonation:** see §1. Landing on the parent and then spreading to
children is a *different* effect and belongs to the warhead.

---

## 4. What this must not break

* **Attachment children are usually `LegalTarget=no`.** A redirect that hands the
  engine an illegal target will be refused. Redirect targets may need a temporary
  legality override, or the modder must make the child targetable. **Unresolved —
  test before committing to an approach.**
* **`Intangible` / `OccupiesCell=no` children** are absent from cell content
  lists. A projectile aimed at one may not find it. Same test.
* Redirecting onto a child that is destroyed mid-flight leaves a bullet with a
  dead target — the engine handles this for ordinary targets, and nothing here
  makes it worse, but it is worth confirming rather than assuming.
* **Determinism.** Every input (slot order, health, the attachment graph) is
  synced; `Chance` and `random` use the synced RNG. No new state, so nothing to
  serialize.

---

## 5. Build order

1. ~~**D1a** with `OnMissing=hold`.~~ **DONE.** Substitutes the target at the
   `0x6FDD77` seat, one instruction before the function reads it into EDI.
2. **Test the `CanFire` interaction** (§2) and the legality question (§4) before
   writing D1b — both features depend on the same answers, and guessing would
   mean building D1b twice.
3. ~~**D1b** fire-time redirect.~~ **DONE** as `Intercepts.Parent`, gated on
   `LegalTarget` so the §4 unknown never arises.
4. Hand **splash-onto-relatives** to WeaponExt as a warhead tag.

D1a is small. D1b is small *if* the legality question resolves kindly, and may be
considerably larger if it does not — which is exactly why step 2 exists rather
than being folded into step 3.
