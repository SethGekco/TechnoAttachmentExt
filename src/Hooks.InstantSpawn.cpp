// H1a -- instant spawn runtime. Design: docs/DESIGN-H1-InstantSpawn.md.
//
// Trigger seats:
//   fire       0x6FDD77  TechnoClass::Fire -- a free 6-byte gap between Phobos'
//                        0x6FDD6F (+8) and 0x6FDD7D (+5). Kratos owns the entry
//                        (0x6FDD50) so the entry is avoided.
//   destroyed  0x702050  TechnoClass::ReceiveDamage, destroyed-by-damage. The
//                        encyclopedia has this verified: the unit is still
//                        present (coords and owner valid) and three frameworks
//                        already chain here reading `this` and returning 0.
//                        It is "destroyed by damage", NOT "removed" -- which is
//                        exactly the combat-only semantics H1a wants.
//   timer / created      no hook; driven from our own synced tick and CreateChild.
//
// ⚠ PLACEMENT IS DESTRUCTIVE. Unlimbo destroys an object whose placement fails,
// and that destruction re-enters our removal hooks and can null members we hold.
// This is the mechanism behind the AttachmentClass::AI() null-Child crash. So:
// the rule list is snapshotted before placing, nothing iterates a live engine
// collection across a placement, and every pointer held across one is re-read.
//
// DETERMINISM: Chance and random facing use ScenarioClass::Random (synced). The
// free-cell search is a fixed spiral, so ties resolve identically on every peer.

#include <algorithm>

#include <TechnoClass.h>
#include <MapClass.h>
#include <CellClass.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>
#include <UnitTypeClass.h>
#include <InfantryTypeClass.h>
#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>
#include <New/Entity/AttachmentClass.h>

namespace
{
	HouseClass* ResolveSpawnOwner(TechnoClass* pInvoker, TAExtSpawnOwner owner)
	{
		switch (owner)
		{
		case TAExtSpawnOwner::Civilian: return HouseClass::FindCivilianSide();
		case TAExtSpawnOwner::Special:  return HouseClass::FindSpecial();
		case TAExtSpawnOwner::Neutral:  return HouseClass::FindNeutral();
		default:                        return pInvoker->Owner;
		}
	}

	// Play one anim from `list` at `cell`. Cosmetic failure is silent on purpose:
	// a missing puff of smoke must never cost the delivery.
	//
	// The pick uses ScenarioClass::Random rather than a hashed/render source
	// because an AnimClass is SYNCED state, not decoration -- an AnimType with
	// MakeInfantry= creates a real unit. For the same reason this is called
	// unconditionally on every peer; never gate it on anything client-side.
	void PlayAnim(std::vector<AnimTypeClass*> const& list, CellStruct const& cell,
		HouseClass* pOwner, bool requireClear)
	{
		if (list.empty())
			return;

		auto const pCell = MapClass::Instance.TryGetCellAt(cell);
		if (!pCell)
			return;

		// Draw FIRST, then apply RequireClear. Both orders happen to be safe here
		// (cell contents are synced, so every peer would skip the same cells), but
		// drawing before any early-out keeps the RNG consumption independent of map
		// state, which is the habit that stays correct if a future condition is ever
		// client-side. A single-entry list needs no draw at all.
		int const pick = (list.size() == 1)
			? 0
			: ScenarioClass::Instance->Random.RandomRanged(0, static_cast<int>(list.size()) - 1);

		// RequireClear: only play where nothing is standing.
		if (requireClear && pCell->FirstObject)
			return;

		if (auto const pAnimType = list[pick])
		{
			if (auto const pAnim = GameCreate<AnimClass>(pAnimType, pCell->GetCoordsWithBridge()))
			{
				// Owner drives house remap AND is what an AnimType with MakeInfantry=
				// hands its new infantry to.
				pAnim->Owner = pOwner;
			}
		}
	}

	// Fixed spiral around `origin`, out to `range` cells. Deterministic order, so
	// two peers pick the same cell. Returns false if nothing suitable was found.
	bool FindPlacementCell(TechnoTypeClass* pType, HouseClass* pOwner,
		CellStruct const& origin, int range, CellStruct& out)
	{
		auto const speed = pType->SpeedType;
		auto const mzone = pType->MovementZone;

		for (int r = 0; r <= range; ++r)
		{
			for (int dy = -r; dy <= r; ++dy)
			{
				for (int dx = -r; dx <= r; ++dx)
				{
					// Only the ring at radius r, so cells come out nearest-first.
					if (r != 0 && std::max(std::abs(dx), std::abs(dy)) != r)
						continue;

					CellStruct cell { static_cast<short>(origin.X + dx),
					                  static_cast<short>(origin.Y + dy) };

					if (MapClass::Instance.IsWithinUsableArea(cell, false)
						&& MapClass::Instance.GetCellAt(cell)->IsClearToMove(
							speed, false, false, -1, mzone, -1, false))
					{
						out = cell;
						return true;
					}
				}
			}
		}

		return false;
	}

	// H1c -- how many objects this activation places.
	//
	// Every term reads SYNCED state (active slots, ammo, veterancy) with integer
	// maths, so both peers compute the same number. Clamped at zero so a negative
	// scaling term cannot produce a nonsense loop bound.
	int EffectiveCount(TechnoClass* pOwner, InstantSpawnRule const& rule)
	{
		int count = rule.Count;

		if (rule.CountPerSlot != 0)
		{
			int slots = 0;
			if (auto const pExt = TechnoExt::ExtMap.Find(pOwner))
			{
				// Uses the public slot API (IsSlotActive) rather than the container's
				// file-local active test, so "active" means exactly what it means
				// everywhere else: the child exists, is alive and is not in limbo.
				size_t const total = pExt->ChildAttachments.size();
				for (size_t idx = 0; idx < total; ++idx)
				{
					if (!TechnoExt::IsSlotActive(pOwner, idx))
						continue;

					// An empty type filter counts every active slot.
					if (!rule.CountPerSlotType.empty())
					{
						auto const pSlot = TechnoExt::GetChildSlot(pOwner, idx);
						auto const pChildType = (pSlot && pSlot->Child)
							? pSlot->Child->GetTechnoType() : nullptr;

						if (!pChildType || std::find(rule.CountPerSlotType.begin(),
							rule.CountPerSlotType.end(), pChildType) == rule.CountPerSlotType.end())
							continue;
					}

					++slots;
				}
			}
			count += slots * rule.CountPerSlot;
		}

		if (rule.CountPerAmmo != 0)
		{
			// Ammo is -1 for unlimited-ammo types; that must not scale anything.
			int const ammo = pOwner->Ammo;
			if (ammo > 0)
				count += ammo * rule.CountPerAmmo;
		}

		if (rule.CountPerRank != 0)
			count += static_cast<int>(pOwner->Veterancy.GetRemainingLevel()) * rule.CountPerRank;

		if (rule.CountMax > 0)
			count = std::min(count, rule.CountMax);

		return std::max(count, 0);
	}

	// H1c -- which type this object should be. `cyclePos` is read AND advanced for
	// Mode=cycle, which is why it comes in by reference from serialized state.
	TechnoTypeClass* PickType(InstantSpawnRule const& rule, int& cyclePos)
	{
		if (rule.Types.empty())
			return nullptr;

		int const n = static_cast<int>(rule.Types.size());

		switch (rule.Mode)
		{
		case TAExtSpawnMode::Random:
			return rule.Types[ScenarioClass::Instance->Random.RandomRanged(0, n - 1)];

		case TAExtSpawnMode::Weighted:
		{
			int total = 0;
			for (int i = 0; i < n && i < static_cast<int>(rule.Weights.size()); ++i)
				total += (rule.Weights[i] > 0) ? rule.Weights[i] : 0;

			if (total <= 0)
				return rule.Types[0]; // parser should have prevented this

			int roll = ScenarioClass::Instance->Random.RandomRanged(1, total);
			for (int i = 0; i < n && i < static_cast<int>(rule.Weights.size()); ++i)
			{
				int const w = (rule.Weights[i] > 0) ? rule.Weights[i] : 0;
				roll -= w;
				if (roll <= 0)
					return rule.Types[i];
			}
			return rule.Types[n - 1];
		}

		case TAExtSpawnMode::Cycle:
		{
			if (cyclePos < 0 || cyclePos >= n)
				cyclePos = 0;
			auto const pType = rule.Types[cyclePos];
			cyclePos = (cyclePos + 1) % n;
			return pType;
		}

		default:
			return nullptr; // All: the caller walks the whole list itself
		}
	}

	// Place ONE object. Returns true if it is on the map afterwards.
	//
	// Every caller must assume this can destroy the object: Unlimbo failing is
	// destructive, and the destruction re-enters our hooks.
	bool PlaceOne(TechnoClass* pInvoker, TechnoTypeClass* pType,
		InstantSpawnRule const& rule, CellStruct const& anchor, CellStruct& usedCell)
	{
		auto const pOwner = ResolveSpawnOwner(pInvoker, rule.Owner);
		if (!pOwner)
			return false;

		CellStruct cell = anchor;
		if (rule.OnBlocked != TAExtSpawnBlocked::Stack)
		{
			int const range = (rule.OnBlocked == TAExtSpawnBlocked::Nearest) ? rule.Range : 0;
			if (!FindPlacementCell(pType, pOwner, anchor, range, cell))
				return false; // Skip, or Nearest exhausted its range
		}

		auto const pObject = static_cast<TechnoClass*>(pType->CreateObject(pOwner));
		if (!pObject)
			return false;

		// Facing: -1 inherit, -2 random, else the literal value.
		int facing = rule.Facing;
		if (facing == -2)
			facing = ScenarioClass::Instance->Random.RandomRanged(0, 255);
		else if (facing < 0)
			facing = static_cast<int>(pInvoker->PrimaryFacing.Current().GetDir());

		auto const coords = MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge();

		++Unsorted::ScenarioInit; // suppress placement side effects, as Unlimbo callers do
		bool const placed = pObject->Unlimbo(coords, static_cast<DirType>(facing));
		--Unsorted::ScenarioInit;

		if (!placed)
		{
			// The object is still ours and still un-placed; dispose of it rather
			// than leaking it into the game's object arrays.
			pObject->UnInit();
			return false;
		}

		if (rule.Mission >= 0)
			pObject->QueueMission(static_cast<Mission>(rule.Mission), false);

		usedCell = cell;
		return true;
	}
}

// Run every rule on `pOwner` that listens for `trigger`. `weaponIndex` is only
// meaningful for the fire trigger (-1 = not applicable).
void TAExt_RunInstantSpawns(TechnoClass* pOwner, int trigger, int weaponIndex)
{
	if (!pOwner)
		return;

	auto const pExt = TechnoExt::ExtMap.Find(pOwner);
	if (!pExt)
		return;

	auto const pType = pOwner->GetTechnoType();
	auto const pTypeExt = pType ? TechnoTypeExt::ExtMap.Find(pType) : nullptr;

	// Rules come from the TechnoType and, if this techno is an attachment child,
	// from its AttachmentType. Both lists run; they are additive, not overriding,
	// because they are whole rules rather than single values.
	std::vector<InstantSpawnRule const*> rules;

	if (pTypeExt)
	{
		for (auto const& rule : pTypeExt->InstantSpawnRules)
			rules.push_back(&rule);
	}

	if (auto const pSlot = pExt->ParentAttachment)
	{
		if (auto const pAttType = pSlot->GetType())
		{
			for (auto const& rule : pAttType->InstantSpawnRules)
				rules.push_back(&rule);
		}
	}

	if (rules.empty())
		return;

	// Cooldown bookkeeping is indexed by position in this combined list, so it
	// must be sized before any rule runs.
	if (pExt->InstantSpawnLastFired.size() != rules.size())
		pExt->InstantSpawnLastFired.assign(rules.size(), -1);
	if (pExt->InstantSpawnCyclePos.size() != rules.size())
		pExt->InstantSpawnCyclePos.assign(rules.size(), 0);

	int const now = static_cast<int>(Unsorted::CurrentFrame);

	for (size_t i = 0; i < rules.size(); ++i)
	{
		auto const& rule = *rules[i];

		if (!(rule.Triggers & trigger))
			continue;

		if (trigger == TAExtSpawn_Fire && rule.OnWeapon >= 0 && rule.OnWeapon != weaponIndex)
			continue;

		if (trigger == TAExtSpawn_Timer)
		{
			// Frame-derived, so it needs no state and cannot drift.
			if (rule.OnRate <= 0 || (now % rule.OnRate) != 0)
				continue;
		}

		if (rule.Cooldown > 0)
		{
			int const last = pExt->InstantSpawnLastFired[i];
			if (last >= 0 && now - last < rule.Cooldown)
				continue;
		}

		if (rule.Chance < 100
			&& ScenarioClass::Instance->Random.RandomRanged(1, 100) > rule.Chance)
			continue;

		// --- resolve the anchor cell ---
		CellStruct anchor {};
		if (rule.At == TAExtSpawnAt::Target)
		{
			auto const pTarget = pOwner->Target;
			if (!pTarget)
			{
				if (!rule.NoTargetFallsBackToSelf)
					continue;
				anchor = pOwner->GetMapCoords();
			}
			else
			{
				anchor = CellClass::Coord2Cell(pTarget->GetCoords());
			}
		}
		else
		{
			anchor = pOwner->GetMapCoords();
		}

		pExt->InstantSpawnLastFired[i] = now;

		// H1b: the two once-per-activation anims. Source plays on the spawner even
		// if every placement then fails -- it represents the ACT, not the result.
		auto const pAnimOwner = ResolveSpawnOwner(pOwner, rule.AnimOwner);
		PlayAnim(rule.AnimSource, pOwner->GetMapCoords(), pAnimOwner, rule.AnimRequireClear);
		PlayAnim(rule.AnimDest, anchor, pAnimOwner, rule.AnimRequireClear);

		// --- place ---
		// The type list is copied by value into locals before placing: placement
		// can destroy objects and re-enter our hooks, and `rules` points into
		// container-owned storage that a re-entrant path could touch.
		auto const types = rule.Types;
		auto const ruleCopy = rule;
		int const count = EffectiveCount(pOwner, ruleCopy);

		// Placing one object, with the shared re-validation and anim handling.
		auto const placeOneObject = [&](TechnoTypeClass* pSpawnType) -> bool
		{
			if (!pSpawnType)
				return true;

			// Re-validate the invoker each time: a previous placement may have
			// killed it (it can be standing in the cell we just filled).
			if (!pOwner->IsAlive)
				return false;

			CellStruct used {};
			if (PlaceOne(pOwner, pSpawnType, ruleCopy, anchor, used))
				PlayAnim(ruleCopy.AnimPerObject, used, pAnimOwner, ruleCopy.AnimRequireClear);
			else
				PlayAnim(ruleCopy.AnimBlocked, anchor, pAnimOwner, ruleCopy.AnimRequireClear);

			return true;
		};

		for (int n = 0; n < count; ++n)
		{
			if (ruleCopy.Mode == TAExtSpawnMode::All)
			{
				// `all` means the WHOLE list per iteration, so Count=3 on a two-type
				// list places six objects.
				for (auto const pSpawnType : types)
				{
					if (!placeOneObject(pSpawnType))
						return;
				}
			}
			else
			{
				// The other modes place exactly one type per iteration. Cycle state is
				// advanced through the ext, not the copy, so it persists.
				if (!placeOneObject(PickType(ruleCopy, pExt->InstantSpawnCyclePos[i])))
					return;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Trigger seats
// ---------------------------------------------------------------------------

// TechnoClass::Fire (entry 0x6FDD50). At 0x6FDD77 the weapon has been resolved
// into EBX and the "no weapon" branch is about to be taken.
//
// ⚠ MUST NOT RETURN 0. The stolen bytes are `jz near 0x6FDE03` -- a RELATIVE
// branch, which Syringe copies into its stub without relocating. Returning 0
// would execute that jz with the original displacement measured from the stub's
// address. The original test is reproduced below and every path returns an
// explicit address.
DEFINE_HOOK(0x6FDD77, TechnoClass_Fire_InstantSpawn_TAExt, 0x6)
{
	enum { NoWeapon = 0x6FDE03, Continue = 0x6FDD7D };

	GET(TechnoClass* const, pThis, ESI);
	GET(void* const, pWeapon, EBX);

	if (!pWeapon)
		return NoWeapon; // the original branch

	// The frame is EBP-based here (`push ebp; mov ebp,esp` at 0x6FDD50), and the
	// disassembly reads the arguments as [ebp+0x8] and [ebp+0xC] two and four
	// instructions later -- so these offsets are read off the function itself,
	// not assumed.
	DWORD const ebp = R->EBP();
	int const weaponIndex = *reinterpret_cast<int*>(ebp + 0xC);

	TAExt_RunInstantSpawns(pThis, TAExtSpawn_Fire, weaponIndex);

	return Continue;
}

// TechnoClass::ReceiveDamage -- the destroyed-by-damage site. ESI = the dying
// techno, still present with valid coords and owner. Registry-confirmed as a
// multi-consumer address (Antares/Kratos/Phobos all read `this` and return 0),
// so returning 0 chains correctly.
//
// This is "destroyed by damage", not "removed": selling, undeploying,
// transforming and script removal do NOT pass through here. That is precisely
// the combat-only default H1a wants, and it is why no reason discrimination is
// needed for `combat`.
DEFINE_HOOK(0x702050, TechnoClass_ReceiveDamage_InstantSpawn_TAExt, 0x6)
{
	GET(TechnoClass* const, pThis, ESI);

	TAExt_RunInstantSpawns(pThis, TAExtSpawn_Destroyed, -1);

	return 0;
}
