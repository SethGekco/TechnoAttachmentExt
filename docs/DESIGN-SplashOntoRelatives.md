# Splash-onto-relatives — design, and a correction

A warhead that, on detonation, spreads damage from the victim onto its
attachment relatives: a hull hit that also rattles every plate bolted to it.

Status: **BUILT** (`src/Hooks.SplashRelatives.cpp`), via option **B** below.
The recommendation here differs from `DESIGN-D-RelativeTargeting.md` §1, which
was wrong; §1 explains why.

---

## 1. The correction

`DESIGN-D-RelativeTargeting.md` §1 recommended handing this to **WeaponExt**,
reasoning that a detonate-time effect is a property of the projectile and so
belongs with the warhead. The *reasoning* still holds. The *conclusion* does not,
for a reason that only shows up when you look at WeaponExt:

**WeaponExt cannot see the attachment graph.** Its extension containers are
`BulletType`, `TechnoType` and `WeaponType`; nothing in it references attachments,
parents or children. The graph lives in this DLL's `TechnoExt::ExtMap` — a
separate module's `Container`, in a separate DLL's memory.

So the earlier recommendation would have handed WeaponExt a feature it has no
data to implement. Worth catching here rather than after somebody tried.

### The three ways out

| | Approach | Verdict |
|---|---|---|
| **A** | WeaponExt implements it; this DLL exports a query for "the children of X" | Works, but creates a cross-DLL call and a **load-order dependency** between two independently-versioned Syringe modules. The coupling is permanent and the failure mode (one DLL updated, the other not) is silent. |
| **B** | **This DLL implements it, reading a tag off the WarheadType** | **Recommended.** We already own the graph. Reading a `WarheadTypeClass` tag is no harder than reading a `TechnoTypeClass` one — the tag living on a warhead does not oblige the *code* to live in WeaponExt. |
| **C** | Do not build it | Defensible; see §4. |

A tag's natural INI home and a feature's natural code home are different
questions, and conflating them is what produced the wrong call the first time.

---

## 2. What it is, precisely — and how it differs from what already shipped

These are complements, not variants, and it is worth being exact because they
sound alike:

| | `Intercepts.Parent` *(built, D1b)* | splash-onto-relatives *(this)* |
|---|---|---|
| When | **fire time** — before the projectile exists | **detonation** — after it lands |
| Effect | the shot is **redirected**; the plate is hit *instead of* the hull | the shot lands as aimed; damage **also** reaches the relatives |
| Hull takes damage? | No | Yes |
| Needs the child to be `LegalTarget`? | Yes — it becomes the target | No — it is never targeted, only damaged |

That last row is the interesting one. Interception had to be gated on
`LegalTarget` because handing the engine an illegal target gets the shot refused.
Splash has no such constraint: it applies damage directly and never asks the
engine to aim at anything. **So splash reaches children that interception
cannot** — including the `LegalTarget=no`, `Intangible` ones that are the normal
case for attachments.

That is the real argument for building it: it covers the case D1b structurally
cannot.

---

## 3. Shape

```ini
[SomeWarhead]
SplashToRelatives=children        ; children | siblings | parent | root
SplashToRelatives.Percent=50      ; share of the dealt damage each receives
SplashToRelatives.Max=0           ; 0 = every match; else cap the count
SplashToRelatives.Warhead=        ; optional different warhead for the splash
SplashToRelatives.MinDamage=1     ; floor, so a big unit's plates still feel it
```

Relations reuse the F0b vocabulary, as everything else in this DLL does.

`Percent` is of the damage **actually dealt** to the victim, after its own armour
— so a shot that barely scratched the hull barely scratches the plating, which is
the intuitive reading.

`.Warhead` matters more than it looks: plates and hulls usually want different
armour interactions, and without it a modder has to choose one armour type for
both roles.

---

## 4. Should it be built at all?

Answered: **yes, built** — Rex confirmed. The deciding argument turned out to be
the `LegalTarget` asymmetry in §2: splash reaches children interception cannot.

`Intercepts.Parent` already covers the common want ("my plating soaks hits").
Splash adds the case where damage should reach *both*, and the case of reaching
children that cannot be legal targets. Whether that is a real design need or a
completeness itch is a question about the mod, not the engine — so it is Rex's to
answer, not mine to assume.

If it is wanted, §1 option **B** is the way, and the work is small: one
detonation-side seat plus damage application over a list this DLL already knows
how to build.

---

## 5. What DOES still belong to WeaponExt

Nothing here — but the earlier handoff stands on its own:
`DESIGN-H1-InstantSpawn.md` §3.1's **`SpawnOnDetonate`** genuinely belongs to
WeaponExt, because placing an object at an impact point needs the impact and the
invoking house and nothing else. No attachment data, no coupling.

One coordination note if both are ever built: keep the tag namespaces distinct
(`SpawnOnDetonate.*` in WeaponExt, `SplashToRelatives.*` here) so two DLLs reading
warhead sections never collide on a key.
