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
- [x] Singular alias `PowersSibling=` parses. **Confirmed 2026-08-21.**
- [ ] Remaining singular aliases: `PowersSibling.Type=`, `PowersSibling.Index=`.
- [ ] **Detach safety:** detach a dark consumer child from its parent → it must
      wake up, not stay dark forever.

## 4. Reverse power (`PoweredByParent`)
- [ ] Child dark while parent is dark; wakes when parent wakes.
- [ ] **Chain:** parent → child → grandchild all with `PoweredByParent` — darkness
      propagates all the way down.
- [ ] Parent destroyed → child handles it without crashing.

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

## 8b. Experience passing (`ExperienceTo`)
- [ ] `ExperienceTo=parent`: kill things with the **child** → the **host** gains
      veterancy (watch for the promotion chevron).
- [ ] `ExperienceTo=siblings`: other attachments on the same parent gain instead.
- [ ] `ExperienceTo.Share=50` → recipients gain roughly half of what the child earned.
- [ ] `ExperienceTo.Drain=yes` → the child's own rank stops climbing (it hands the
      XP over); with `Drain=no` (default) the XP is **duplicated**, not moved.
- [ ] XP from a **crate** or script, not just kills, also propagates.
- [ ] Save/load mid-game does **not** cause a phantom XP payout on the next frame.
- [ ] Bad relation name (`ExperienceTo=nonsense`) logs a parse error, no crash.

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
