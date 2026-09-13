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

#include <TechnoClass.h>
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

					if (MapClass::Instance->IsWithinUsableArea(cell, false)
						&& MapClass::Instance->GetCellAt(cell)->IsClearToMove(
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

	// Place ONE object. Returns true if it is on the map afterwards.
	//
	// Every caller must assume this can destroy the object: Unlimbo failing is
	// destructive, and the destruction re-enters our hooks.
	bool PlaceOne(TechnoClass* pInvoker, TechnoTypeClass* pType,
		InstantSpawnRule const& rule, CellStruct const& anchor)
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
			facing = pInvoker->PrimaryFacing.Current().GetDir();

		auto const coords = MapClass::Instance->GetCellAt(cell)->GetCoordsWithBridge();

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

		// --- place ---
		// The type list is copied by value into locals before placing: placement
		// can destroy objects and re-enter our hooks, and `rules` points into
		// container-owned storage that a re-entrant path could touch.
		auto const types = rule.Types;
		auto const ruleCopy = rule;

		for (int n = 0; n < ruleCopy.Count; ++n)
		{
			for (auto const pSpawnType : types)
			{
				if (!pSpawnType)
					continue;

				// Re-validate the invoker each time: a previous placement may have
				// killed it (it can be standing in the cell we just filled).
				if (!pOwner->IsAlive)
					return;

				PlaceOne(pOwner, pSpawnType, ruleCopy, anchor);
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
