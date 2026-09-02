# Test checklist — TechnoAttachmentExt

Ordered so that a failure early on explains failures later. Tag syntax:
`docs/TAGS.md`.

`[x]` = confirmed working in-game (date noted). Everything still `[ ]` is built and
deployed but **not yet played**.

## 0. Preflight (do this first — it invalidates everything else)

- [x] **The DLL is actually injected** — proven by the §3 sibling-power result.
      If it ever stops working, check `syringe.log` for a `TechnoAttachmentExt.dll`
      "Recognized DLL" line: it was **missing from the inject list** until
      2026-08-21 and features silently did nothing. The Linux inject list is
      `Resources/Compatibility/Unix/wine-game.sh`, *not* `ClientDefinitions.ini`.
- [ ] **Build stamp matches** — still worth checking per session. `debug.log` prints
      `[TechnoAttachmentExt] Build: <date> <time>`. The confirmed sibling-power
      result only proves *some* build loaded; the power network, veterancy/health
      gates and XP passing are newer, so confirm the stamp before testing those.
- [x] Game loads to a skirmish with no instant close and no `except.txt`.
      *(Instant close + no dump = CRT abort, usually an INI-key buffer issue.)*
- [ ] Baseline sanity: **select an MCV and deploy it.** This broke once before
      (selection was globally dead); it is the canary for the selection hooks.

## 1. Regression — previously working features
- [ ] Vehicle child rides a vehicle host at its FLH.
- [ ] Infantry child rides a host.
- [ ] Building child on a vehicle host.
- [ ] Vehicle child on a **building** host — no freeze (this was the big one).
- [ ] Attached factory still produces and units exit + scatter off the exit cell.
- [ ] Existing prerequisites still gate: `Prerequisite`, `.Negative`,
      `RequiredHouses`, `ForbiddenHouses`, `Prerequisite.Dynamic=no`.
- [ ] Save → load a game with attachments present; nothing crashes or duplicates.

## 1b. OccupiesCell for non-vehicle children (2026-09-01 fix)
Previously `OccupiesCell=no` was honoured for **vehicle children only** (upstream
gap). Verify per child type:
- [ ] **Infantry** child with `OccupiesCell=no` → no longer blocks unit movement or
      building placement on its cell.
- [ ] **Building** child with `OccupiesCell=no` → same.
- [ ] **Aircraft** child with `OccupiesCell=no` → same.
- [ ] **Vehicle** child still behaves as before (regression — this path was already
      working).
- [ ] `OccupiesCell=yes` (default) still blocks for every type — the skip must be
      opt-in, not always-on.
- [ ] Per-slot `AttachmentN.OccupiesCell=no` overrides the AttachmentType.
- [ ] **Balance check**: attach → detach → destroy a non-occupying child of each
      type; no cell stays permanently marked (a stuck flag shows up as an
      invisible wall units refuse to path through).
- [ ] Chrono/teleport a host carrying a non-occupying child → no stale flags left
      at the origin cell.
- [ ] Still no freeze with a vehicle child on a building host (the original bug
      these hooks were written for).

## 1c. Intangible (cell-content removal) — EXPERIMENTAL, answer this first
- [x] **ANSWERED 2026-09-01: the child stays VISIBLE.** Cell content does *not*
      feed the renderer, so `Intangible` is a pure "blocks nothing" tag and is safe
      on attachments you want seen. (Consequence: invisibility is still unbuilt and
      needs a real render-side approach — see ROADMAP.)
- [x] Units can be auto-targeted again through an attachment. **Confirmed 2026-09-01.**
- [ ] With `Intangible=yes`, a building can be placed **on** the child's cell.
- [ ] Units path straight through the child's cell.
- [ ] Clicking the child's cell selects/targets whatever is underneath it.
- [ ] Works for all four child kinds (infantry / vehicle / aircraft / building).
- [ ] Per-slot `AttachmentN.Intangible=yes` overrides the AttachmentType.
- [ ] **Balance check:** attach → detach → destroy an intangible child; no cell is
      left with a stale entry (symptom: a phantom object blocking a cell forever).
- [ ] **Regression:** `Intangible=no` (default) behaves exactly as before —
      confirm normal attachments are unaffected.
- [ ] Host moves across many cells with an intangible child → no leak, no slowdown
      (AddContent/RemoveContent are very hot paths).
- [ ] Save/load with an intangible child present.

## 1d. Cursor pass-through (2026-09-01 fix)
Previously the cursor still refused to interact with whatever sat under an
attachment, even with `TransparentToMouse=yes` — the cursor uses a different
resolution than click-to-select.
- [ ] With `TransparentToMouse=yes` (or `Intangible=yes`), hovering over the
      attachment shows the cursor for **what is underneath** (ground/unit), not a
      "no action" cursor.
- [ ] Clicking there actually issues that action (move onto the cell / select the
      unit beneath).
- [ ] With a unit selected, hovering an enemy under an attachment gives the
      **attack** cursor.
- [ ] **Regression:** an attachment WITHOUT either tag still catches the cursor as
      before (this is opt-in).
- [ ] The host itself is still hoverable/selectable normally.
- [ ] `PassSelection=yes` still selects the host when clicking the child.
- [ ] No cursor flicker while moving the pointer across the attachment.

## 2. Attachment power (`PowersParent`)
- [ ] **Vehicle host** with a `PowersParent=yes` child: host is dark with no active
      child, wakes when the child returns. *(Reported working.)*
- [ ] **Building host** — NEW, the fix for your report. Dark with no powering child,
      wakes when it returns.
- [ ] Kill the child → host goes dark. Respawn/restore → host wakes.
- [ ] Hide the child via a prerequisite (sell the required building) → host darkens;
      rebuild → wakes.
- [ ] Dark host really is dark: **can't move, can't fire, ignores orders.**

## 3. Sibling power (`Powered` / `PowersSiblings`)
Setup: one parent, slot A = source, slot B = consumer.
- [x] `PowersSiblings=yes` + `Powered=yes`: B dark until A is active.
      **Confirmed 2026-08-21** — consumer side (`Powered=yes`) and vague source
      side (`PowersSibling=yes` on the AttachmentType) both work.
- [ ] `PowersSiblings.Index=<B's index>`: powers only that slot; a third
      consumer slot NOT listed stays dark.
- [ ] `PowersSiblings.Type=<B's child type>`: powers by type; a different-typed
      consumer stays dark.
- [ ] Union: a source with both `.Index` and `.Type` powers consumers matching
      **either**.
- [ ] **Picky consumer:** B with `Powered.Type=<A's type>` — a vague
      `PowersSiblings=yes` source of a *different* type must NOT power it; one of
      the accepted type must.
- [x] Both spellings parse: `PowersSibling=` and `PowersSiblings=`. **Confirmed 2026-08-21.**
- [ ] Remaining singular aliases: `PowersSibling.Type=`, `PowersSibling.Index=`.
- [ ] **Detach safety:** detach a dark consumer child from its parent → it must
      wake up, not stay dark forever.

## 4. Reverse power (`PoweredByParent`)
- [ ] Child dark while parent is dark; wakes when parent wakes.
- [ ] **Chain:** parent → child → grandchild all with `PoweredByParent` — darkness
      propagates all the way down.
- [ ] Parent destroyed → child handles it without crashing.

## 4b. External-structure power (`PoweredBy`)
- [ ] `PoweredBy=GAROBO`: attachment dark with no GAROBO, lights up when one is
      built anywhere on the map (house-wide, no radius).
- [ ] Sell/destroy the GAROBO → dark again.
- [ ] **Power matters**: keep the GAROBO but take the base to low power → the
      attachment goes dark (`RequirePower=yes` default).
- [ ] `PoweredBy.RequirePower=no` → a powered-down GAROBO still counts.
- [ ] `PoweredBy=GAROBO,GATECH` with `RequireAll=no` (default) → either works.
- [ ] `RequireAll=yes` → needs both.
- [ ] `PoweredBy.Range=10` → only a GAROBO within 10 cells counts; drive away → dark.
- [ ] **Enemy/ally isolation**: an ALLY's GAROBO must NOT power your attachment.
- [ ] **Per-slot**: `Attachment0.PoweredBy=GAROBO` + `Attachment1` unset, same
      AttachmentType → only slot 0 depends on the building. ← the key new ability
- [ ] Combines with sibling power: a child needing both a sibling generator AND a
      GAROBO goes dark if *either* is missing.
- [ ] `PoweredBy.House=owner` (default) → only YOUR GAROBO powers it.
- [ ] `PoweredBy.House=ally` → an ally's GAROBO powers it, your own does NOT
      (ally excludes self — combine as `owner,ally` for both).
- [ ] `PoweredBy.House=owner,ally` → either works.
- [ ] `PoweredBy.House=enemy` → an ENEMY's GAROBO powers it; a neutral/civilian one
      does not (passives are excluded from `enemy`).
- [ ] `PoweredBy.House=neutral` / `civilian` / `special` against a map with those
      houses present.
- [ ] `PoweredBy.House=any` → anybody's GAROBO works.
- [ ] `AttachmentN.PoweredBy.House=` per-slot override.
- [ ] Bad value (`PoweredBy.House=nonsense`) → parse error in the log, no crash.
- [ ] `team`: in a plain skirmish (no teams) it should power NOTHING — confirm it
      doesn't accidentally behave like `ally`.

## 5. Requirement gating (open-topped style)
- [ ] `RequiresPassengers=1`: attachment dark until a passenger boards the host;
      dark again when the last one leaves.
- [ ] `RequiresPassengers=2`: still dark with only 1 aboard.
- [ ] `RequiresSlot.Index=0`: dark unless slot 0 has an **active** child (test with
      slot 0 hidden by a prerequisite, not just empty).
- [ ] `RequiresSlot.Type=<type>`: dark unless a child of that type is active.
- [ ] Combined with `RequiresPassengers` → both must be satisfied (AND).

## 6. Power network (radius / relays / capacity) — biggest new surface
- [ ] `PowerSource` + `PowerConsumer`: consumer inside `Range` is lit, outside is dark.
- [ ] Move a **mobile** consumer in and out of range → it toggles live.
- [ ] Move/destroy the **source** → consumers go dark.
- [ ] **Relay chain:** source → pylon → pylon → distant consumer is lit.
      **Destroy a middle pylon → everything past it goes dark.** (The headline test.)
- [ ] `PowerRelay.Range` unset → relay inherits the feeder's range.
- [ ] **Capacity `excess`:** `PowerSource.Count=2` with 4 consumers → exactly 2 lit.
- [ ] **Capacity `all`:** same setup with `OverflowMode=all` → **all 4** go dark.
- [ ] `PowerSource.Types` / `PowerConsumer.Types` filters exclude non-listed types.
- [ ] `PowerConsumer.House=owner` → an ally's network no longer powers you
      (default is `owner,ally`, so this is the behaviour change to verify).
- [ ] `PowerConsumer.House=any` → an enemy's network powers you.
- [ ] `PowerSource.House=owner` on the source → it refuses to power allies.
- [ ] **Enemy isolation:** an enemy's source must NOT power your consumer.
      An **ally's** source should.
- [ ] Works on all four kinds: infantry, vehicle, aircraft, **building** consumers.
- [ ] **Performance:** big skirmish, many technos — watch for frame-rate loss (the
      solver runs every frame over all technos).
- [ ] **Fail-safe:** a consumer with no source anywhere on the map — confirm the
      intended behaviour (it should be dark, not crash).

## 7. Veterancy + health gates
- [ ] `Prerequisite.MinRank=veteran`: attachment absent at rookie, **appears on
      promotion** to veteran.
- [ ] `Prerequisite.MaxRank=veteran`: disappears on promotion to elite.
- [ ] De-vet (if you can trigger it) reverses the change.
- [ ] `Prerequisite.MinHealth=50`: attachment vanishes when the host drops below
      half health, returns when repaired.
- [ ] **Damaged-variant pair** (the use case): `IntactArmor` MinHealth=50 +
      `WreckedArmor` MaxHealth=49 on two slots → they swap at the 50% line, and
      never both show at once.
- [ ] Bad value handling: `Prerequisite.MinRank=nonsense` logs a parse error and
      doesn't crash.

## 8. Decorative profile
- [ ] `Decorative=yes` on an attachment whose child is a normal unit: clicking it
      selects the **host**, it isn't mouse-solid, and it doesn't block cells.
- [ ] It doesn't accidentally turn a child you *want* selectable into a decoration.

## 8b. Experience — multiplier + passing
- [ ] `Experience.Multiplier=0.5` on a **normal unit** (no attachments): it ranks up
      at roughly half the usual rate.
- [ ] `Experience.Multiplier=0` → unit never gains veterancy.
- [ ] Multiplier also works on an **attachment child** and on a **building**.
- [ ] `ExperienceTo=parent`: kill things with the **child** → the **host** gains
      veterancy (watch for the promotion chevron).
- [ ] `ExperienceTo=siblings`: other attachments on the same parent gain instead.
- [ ] `ExperienceTo.Share=50` → recipients gain roughly half of the child's gain.
- [ ] **Groups coexist:** unindexed `ExperienceTo=parent` *plus* `ExperienceTo[0]=children`
      → **both** fire (they are separate rules, not aliases). This is the key new behaviour.
- [ ] Per-group shares differ: `.Share=100` and `.Share[0]=25` pay out different amounts.
- [ ] `ExperienceTo[1]=child` + `ExperienceTo.Slot[1]=2` → only the slot-2 child gains,
      not slot 0.
- [ ] `ExperienceTo.ID[1]=<slot ID>` addresses the same slot by ID and wins over `.Slot`.
- [ ] `ExperienceTo.Drain[0]=yes` on one group only → the earner loses that group's
      share but keeps what a non-draining group copied.
- [ ] **Drain clamp:** several draining groups summing over 100% → earner drops to
      zero gain, never negative veterancy.
- [ ] Multiplier + sharing together: with `Experience.Multiplier=0.5` and
      `.Share=100`, the recipient gets the **halved** amount (multiplier applies first).
- [ ] `root` vs `parent` differ only with **nested** attachments (child of a child).
- [ ] XP from a **crate** or script, not just kills, also propagates.
- [ ] Save/load mid-game does **not** cause a phantom XP payout on the next frame.
- [ ] Bad relation name (`ExperienceTo=nonsense`) logs a parse error, no crash.
- [ ] A gap in the index list (`[0]` and `[2]`, no `[1]`) stops at the gap — `[2]`
      is ignored. Confirm that matches your expectation.

## 8c. Convert-in-place (`Convert`)
- [ ] `Convert=DAMAGEDHULL` + `Convert.MaxHealth=66`: damage the **host** below 66%
      → the attachment child visibly becomes the other TechnoType at the same slot.
- [ ] **Reverts automatically**: repair the host above 66% → the child returns to
      its configured `AttachmentN.TechnoType`.
- [ ] **Two-stage chain**: unindexed `MaxHealth=66` + `[0]` `MaxHealth=33` → the
      child steps through pristine → damaged → wrecked as the host takes damage.
- [ ] **First-match-wins ordering**: with both rules matching (host at 20%), the
      FIRST listed rule is used. Confirm that matches your expectation.
- [ ] Rank conditions: `Convert.MinRank[1]=veteran` swaps the child on promotion.
- [ ] `Convert.KeepHealth=yes` (default): a damaged child stays proportionally
      damaged after the swap; `=no` → it arrives at full health.
- [ ] `Convert.KeepVeterancy=yes` (default): the child's rank survives the swap.
- [ ] Converting a child of a **different Strength** doesn't produce a dead or
      over-healed child.
- [ ] **Quiet swap**: `DestructionWeapon.Child` on the AttachmentType does NOT fire
      on a conversion (it is a replacement, not a death), and the host gets no kill
      credit / no death animation.
- [ ] Convert while the child is **hidden** by a prerequisite → no crash, correct
      type once it reappears.
- [ ] Convert to an **invalid/missing** TechnoType → parse error at load, no crash.
- [ ] Rapid oscillation: park the host's health right on a threshold and let it
      tick → no flicker storm, no crash, no leaked units piling up on the map.
- [ ] Save/load right after a conversion → correct type, no duplicate child.
- [ ] **Multiplayer**: conversions on both peers stay in step (no desync).

## 4c. PoweredBy on plain TechnoTypes (NOT attachments)
- [ ] `PoweredBy=GAROBO` on a normal **unit** TechnoType → that unit goes dark
      without a GAROBO, with no attachments involved at all.
- [ ] Same on a normal **building** TechnoType.
- [ ] `PoweredBy.House=` / `.Range=` / `.RequirePower=` all work in this plain form.
- [ ] **Precedence**: a techno with `PoweredBy` on its TechnoType, used as an
      attachment child whose AttachmentType also sets `PoweredBy` → the
      AttachmentType wins; add `AttachmentN.PoweredBy` and the slot wins.

## 8d. Per-slot overrides (`AttachmentN.*` has the final word)
One AttachmentType reused on two slots, with the slot overriding it:
- [ ] `AttachmentN.Powered=no` on a slot whose AttachmentType has `Powered=yes`
      → that slot ignores power while the other slot still goes dark.
- [ ] `AttachmentN.PowersParent=yes` on a slot whose AttachmentType doesn't set it
      → only that slot powers the host.
- [ ] `AttachmentN.PowersSibling(s)=` override (both spellings accepted).
- [ ] `AttachmentN.PoweredByParent=` override.
- [ ] `AttachmentN.RequiresPassengers=` override.
- [ ] `AttachmentN.PassSelection=` / `.TransparentToMouse=` override.
- [ ] **Unset = inherit:** a slot that sets none of these behaves exactly as the
      AttachmentType says (regression check on everything already confirmed).
- [ ] Save/load preserves per-slot overrides.
- [ ] **List-valued overrides**: `AttachmentN.Powered.Type=`,
      `AttachmentN.PowersSiblings.Type/.Index=`, `AttachmentN.RequiresSlot.Index/.Type=`.
- [ ] **Prerequisite windows per slot**: `AttachmentN.Prerequisite.MinRank=`,
      `.MaxRank=`, `.MinHealth=`, `.MaxHealth=`.
- [ ] **Sibling prerequisites per slot**: `AttachmentN.Prerequisite.Sibling(s).Index/.Type=`.
- [ ] **Behaviour family per slot**: `AttachmentN.RespawnDelay=`,
      `.RespawnAtCreation=`, `.InheritOwner=`, `.InheritDestruction=`,
      `.InheritHeightStatus=`, `.InheritStateEffects=`,
      `.InheritCommands.StopCommand/.DeployCommand=`, `.OccupiesCell=`,
      `.LowSelectionPriority=`, `.DestructionWeapon.Child/.Parent=`,
      `.ParentDestructionMission=`, `.ParentDetachmentMission=`,
      `.Convert.KeepHealth/.KeepVeterancy=`.
- [ ] **Regression sweep** — this pass rewired MANY existing call sites to go
      through resolvers. Re-check that the previously-working behaviours are
      unchanged when no per-slot tag is set: respawn timing, destruction weapons,
      InheritHeightStatus, cell occupation, scatter, and stop/deploy inheritance.

## 8e. Ammo capacity (`Ammo.Parent`)
Use a host with a real ammo count (aircraft are easiest — they fire N times then
return to reload).
- [ ] `Ammo.Parent=2` on an active attachment → the host fires **base+2** shots per
      sortie before running dry.
- [ ] Remove/kill the child → capacity returns to base. If the host was carrying
      more than base at the time, confirm it behaves sanely (fires the surplus, or
      clamps — either is acceptable, note which).
- [ ] Two active slots each granting +1 → **+2** total (bonuses sum).
- [ ] A child hidden by a prerequisite grants **nothing** (only ACTIVE children count).
- [ ] Per-slot `AttachmentN.Ammo.Parent=` overrides the AttachmentType.
- [ ] **Unlimited ammo unaffected:** a host with `Ammo=-1` is untouched.
- [ ] **Known approximation:** the ammo **pip display** shows only the base count.
      Confirm this is acceptable — if not, the pip-drawing site needs its own hook.
- [ ] **Regression:** units with no `Ammo.Parent` anywhere reload exactly as before
      (these hooks are on the shared reload path, so this is the important one).
- [ ] Aircraft return-to-reload logic still works normally.
- [ ] Save/load with a bonus active.
- [ ] Multiplayer: no desync with ammo bonuses in play.

## 9. Interaction / safety (where bugs hide)
- [ ] **EMP a dark consumer**, then restore its power while EMP is still active →
      it must **stay dark** until EMP expires (we must not revive an EMP'd unit).
- [ ] **EMP a lit unit** → EMP recovery still wakes it normally.
- [ ] A unit using **vanilla `PoweredUnit=`** near our sources is unaffected by us
      (don't mix the two systems on one unit; verify we don't hijack it).
- [ ] **Save/load while units are dark** → they stay dark for the right reason and
      wake correctly afterwards.
- [ ] Mind-control / owner change of a source or consumer → network re-resolves.
- [ ] Sell/destroy a host with dark attachments → no crash.

## 10. Multiplayer (the hard requirement)
- [ ] 2-player skirmish (or LAN) exercising power + gates on both sides for several
      minutes → **no desync**. Do this before trusting any of it online.
- [ ] Same again with save/load mid-game.

---

### Reporting a failure
Include: the **Build:** stamp line from `debug.log`, the relevant INI section, and
if it crashed, the whole `debug/snapshot-*/` folder (it has `except.txt`,
`syringe.log` and the minidump). If the game **closes instantly with no
`except.txt`**, say so explicitly — that's a different failure class (CRT abort)
and points straight at INI parsing.
