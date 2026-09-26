# Test checklist

Everything currently set up in `rulesmd.ini`, in the order worth running.
Configs are already installed — see `TEST-RULES.ini` for what each block contains.

Tick as you go. Where a failure is ambiguous, the meaning is spelled out, because
"it didn't work" costs a round trip and "it did X instead of Y" usually doesn't.

---

## 0. First load — before any test

- [ ] Game launches. (Every crash this cycle appeared at launch, not in play.)
- [ ] `GunnerProfile validation: N accepted, M rejected` appears once Test 10 is on
- [ ] `SplashToRelatives: N warhead(s) configured` appears once Test 5 is on
- [ ] No `[TAExt] ... rejected:` lines you didn't expect

A **missing** log line matters as much as a wrong one — it means the feature never
got as far as having an opinion.

---

## 1. Selling must not detonate attachments  ← *the bug you reported*

*Live now. Build a GAROBO with its drone, then sell it.*

- [ ] The drone **vanishes silently** — no explosion, no debris
- [ ] Set `SoldAction=kill` in `[T_Sell]` → it **does** explode

The second half matters: it proves the tag is being read rather than the whole
path being dead.

---

## 2. Orbit, FLH, travel-facing  — *three fixes, none confirmed*

*`[MTNK]` → uncomment the Test 2 block.*

- [ ] Drone sits at the **FLH offset**, not on the tank's centre
- [ ] It **orbits** rather than sitting still
- [ ] It points **along** its path, not backwards
- [ ] The path is a visible **oval that breathes**, not a plain circle
- [ ] `Spins.Orbit.Reverse=yes` flips the orbit without changing the spin

Failure meanings: (a) or (b) → the zeroed-scale bug is back; (c) → the mirror fix
is wrong in the other direction.

---

## 3. Save/load  — **Tier 0**

*`[MTNK]` → Test 3 block. Build the tank, save, reload.*

- [ ] Attachment is **present**, exactly **one** of it, still **following** the host

If not, report **which**: `duplicated` / `orphaned` / `absent`. Each points
somewhere different, and the restore currently handles all three defensively only
because the answer is unknown.

- [ ] Kill the drone, save **during** the 150-frame respawn wait, reload → the
      timer resumes mid-count rather than restarting

---

## 4. Per-viewer visibility

*`[MTNK]` → Test 4 block. Skirmish, so there's an enemy.*

- [ ] You see the drone on your own tank
- [ ] An **enemy-owned** MTNK shows no drone
- [ ] **The hidden drone still takes damage and dies** when that enemy shoots it

That last one is the real test. If it's invulnerable or unhittable, the
render-only rule has been broken and it's a genuine bug.

- [ ] `VisibleTo=owner` instead → allies also lose sight of it

---

## 5. Splash onto relatives

*`[MTNK]` → Test 5 block, **and** uncomment `Primary=105mmSPLASH` in `[MTNK]`.*

- [ ] Shoot a host: host takes damage **and** the drone loses health
- [ ] **Set `LegalTarget=no` on DRONEDUMMY2 — splash must STILL reach it**
- [ ] `SplashToRelatives.MinDamage=5` floors it on a tough hull

The `LegalTarget=no` case is the entire reason this and `Intercepts.Parent` both
exist. If it fails, one of them is redundant.

---

## 6. Interception

*`[MTNK]` → Test 6 block. DRONEDUMMY2 must be `LegalTarget=yes`.*

- [ ] Shoot the host → the **plate** takes it, the hull doesn't
- [ ] **`LegalTarget=no` → interception is SKIPPED and the HOST takes damage**

If the host goes **invulnerable** instead, the legality gate has failed — that's
precisely the failure the gate exists to prevent.

---

## 7. Instant spawn on death, not on sale

*`[MTNK]` → Test 7 block.*

- [ ] **Kill** the tank → two drones appear
- [ ] **Sell / undeploy** an equivalent host → **nothing** spawns
- [ ] `InstantSpawn.On.Reason=sold` logs *"not implemented yet"* rather than
      silently doing nothing

---

## 8. `Sequence.Force`

*`[MTNK]` → Test 8 block (child is `E1`, infantry).*

- [ ] Driving the tank: the rider does **not** play the walk animation
- [ ] **Kill the rider → it plays its DEATH animation**, not frozen at attention

The death guard is the non-obvious half of the feature.

---

## 9. Leash on a building  — **Tier 0, freeze risk**

*`[GAROBO]` → comment Test 1, uncomment Test 9. Then play a few minutes.*

- [ ] Drone moves under its own power, recalled past ~2 cells
- [ ] **No hang or freeze**

The historic building-host freeze came from exactly this — a child being told to
relocate. If it freezes, stop and say so; that result decides whether leash mode
is usable on buildings at all.

---

## 10. Gunner host profile  — **Tier 0, may oscillate**

*`[FV]` → uncomment the three `GunnerProfile` lines. `IFV_TEST` is ready.*

- [ ] Load an **E1** → the IFV becomes IFV_TEST (Strength 1200, different weapon)
- [ ] **The E1 stays aboard**
- [ ] Unload → it reverts
- [ ] Damaged IFV keeps its damage **ratio** across the swap
- [ ] House unit counts / sidebar stay correct after several swaps

**If you see a stutter every ~15 frames**, the conversion is ejecting the
passenger. Report it — do **not** lower `MinDwell`, which only makes it a faster
freeze.

---

## Regression sweep — once the above pass

Things that worked before and could have been disturbed:

- [ ] Power gates: `Powered`, `PowersSiblings`, `PoweredBy`, the network
- [ ] **Dynamic prerequisite** — build/sell a GAROBO and watch for
      `[TAExt] prereq <name>: ...`. A line means the gate ran and reports its
      decision; **silence** means it never reached the test, which points at
      creation/limbo instead. This one is still undiagnosed.
- [ ] Selection, cursor pass-through, cell occupation, `Intangible`
- [ ] Experience sharing
- [ ] Ammo capacity bonus and pip display

---

## Online — one dedicated session, after Tier 0 passes

Highest value are the features carrying **serialized state**, since that's where
a desync would actually come from:

- [ ] `Mode=cycle` (InstantSpawn)
- [ ] `InstantSpawn.Max`
- [ ] `Move.*` offsets
- [ ] Gunner base type across a conversion
- [ ] Per-viewer visibility — **two clients with deliberately opposite views must
      stay in sync.** A desync here means something synced is reading the
      per-viewer answer.

---

## What to include in a report

- What you did, what happened
- The log, or `except.txt` on a crash
- **Which build** — the PE timestamp or just when you copied it. One diagnosis
  this cycle went down the wrong path because the newest CI artifact didn't match
  the deployed binary.
- If a faulting address looks like **printable ASCII** (e.g. `0x6E6F7244` =
  `"Dron"`), say so — that identifies the bug class immediately.
