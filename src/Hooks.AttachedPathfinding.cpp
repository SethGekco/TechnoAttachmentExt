// CanEnterCell attachment-awareness (ported from PR #352).
//
// THE building-host + active-vehicle-child freeze fix. The freeze watchdog
// traced the hang to the engine's pathfinding/passability recursion; the root
// cause is that an attached vehicle sitting on a cell is seen by the
// pathfinder's occupier check as a *blocking vehicle*, which drives the
// building/cell traversal into a cycle. These hooks, inside UnitClass /
// InfantryClass CanEnterCell, make attached children invisible to that occupier
// check (assume no vehicle, then skip occupiers that don't occupy the cell as a
// child or are our own descendants).
//
// NOTE: the PR also accounts for a *moving* unit "incoming" into the target
// cell using a CellClass extension (CellExt::IncomingUnit). That refinement is
// stubbed here (AccountForMovingInto is a no-op) because it needs a cell
// extension that would collide with base Phobos's cell ext offset; it only
// affects moving-vehicle collision accounting, not the static freeze. A
// map-mode CellExt can be added later for full parity.

#include <UnitClass.h>
#include <InfantryClass.h>
#include <CellClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <Ext/Techno/Body.h>

namespace TAExtPathfinding
{
	// Saved across the occupier loop of a single CanEnterCell evaluation: the
	// original "vehicle present" occupation bit, restored only if a real
	// (non-attached) vehicle occupier is found.
	unsigned char storedVehicleFlag = 0;
}

// Start of the occupier loop: assume there is no vehicle here, remembering the
// real flag so a genuine vehicle occupier can restore it.
static void AssumeNoVehicleByDefault(unsigned char& occupyFlags, bool& isVehicleFlagSet)
{
	TAExtPathfinding::storedVehicleFlag = occupyFlags & 0x20;

	occupyFlags &= ~0x20;
	isVehicleFlagSet = false;
}

// Whether this occupier should be ignored by the pathing check: ourselves, a
// child that doesn't occupy the cell, or one of our own descendants. A real
// vehicle occupier restores the vehicle flag.
static bool IsOccupierIgnorable(TechnoClass* pThis, ObjectClass* pOccupier,
	unsigned char& occupyFlags, bool& isVehicleFlagSet)
{
	if (pThis == pOccupier)
		return true;

	auto const pTechno = abstract_cast<TechnoClass*>(pOccupier);
	if (pTechno &&
		(TechnoExt::DoesntOccupyCellAsChild(pTechno) || TechnoExt::IsChildOf(pTechno, pThis)))
	{
		return true;
	}

	if (abstract_cast<UnitClass*>(pOccupier))
	{
		occupyFlags |= TAExtPathfinding::storedVehicleFlag;
		isVehicleFlagSet = (occupyFlags & 0x20) != 0;
	}

	return false;
}

// Moving-unit "incoming" accounting. Stubbed (needs a cell extension); only
// affects moving-vehicle collision accounting, not the freeze.
static void AccountForMovingInto(CellClass*, bool, TechnoClass*,
	unsigned char&, bool&)
{
}

// ============================================================================
// UnitClass::CanEnterCell — frame base 0x90.
// ============================================================================

DEFINE_HOOK(0x73F520, UnitClass_CanEnterCell_AssumeNoVehicleByDefault_TAExt, 0x0)
{
	enum { Check = 0x73F528, Skip = 0x73FA92 };

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x90, -0x7C));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x90, -0x7B));

	GET(TechnoClass*, pOccupier, ESI);

	if (!pOccupier) // restored code
		return Skip;

	AssumeNoVehicleByDefault(occupyFlags, isVehicleFlagSet);

	return Check;
}

DEFINE_HOOK(0x73F528, UnitClass_CanEnterCell_SkipChildren_TAExt, 0x0)
{
	enum { SkipToNextOccupier = 0x73FA87, ContinueCheck = 0x73F530 };

	GET(UnitClass*, pThis, EBX);
	GET(ObjectClass*, pOccupier, ESI);

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x90, -0x7C));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x90, -0x7B));

	return IsOccupierIgnorable(pThis, pOccupier, occupyFlags, isVehicleFlagSet)
		? SkipToNextOccupier
		: ContinueCheck;
}

DEFINE_HOOK(0x73FA92, UnitClass_CanEnterCell_CheckMovingInto_TAExt, 0x0)
{
	GET_STACK(CellClass*, into, STACK_OFFSET(0x90, 0x4));
	GET_STACK(bool const, isAlt, STACK_OFFSET(0x90, -0x7D));
	GET(UnitClass*, pThis, EBX);

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x90, -0x7C));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x90, -0x7B));

	AccountForMovingInto(into, isAlt, pThis, occupyFlags, isVehicleFlagSet);

	// restored code
	if (!isAlt)
		return 0x73FA9E;

	return 0x73FC24;
}

// ============================================================================
// InfantryClass::CanEnterCell — frame base 0x34.
// ============================================================================

DEFINE_HOOK(0x51C249, InfantryClass_CanEnterCell_AssumeNoVehicleByDefault_TAExt, 0x0)
{
	enum { Check = 0x51C251, Skip = 0x51C78F };

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x34, -0x21));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x34, -0x22));

	GET(TechnoClass*, pOccupier, ESI);

	if (!pOccupier) // restored code
		return Skip;

	AssumeNoVehicleByDefault(occupyFlags, isVehicleFlagSet);

	return Check;
}

DEFINE_HOOK(0x51C251, InfantryClass_CanEnterCell_SkipChildren_TAExt, 0x0)
{
	enum { IgnoreOccupier = 0x51C70F, Continue = 0x51C259 };

	GET(InfantryClass*, pThis, EBP);
	GET(ObjectClass*, pOccupier, ESI);

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x34, -0x21));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x34, -0x22));

	return IsOccupierIgnorable(pThis, pOccupier, occupyFlags, isVehicleFlagSet)
		? IgnoreOccupier
		: Continue;
}

DEFINE_HOOK(0x51C78F, InfantryClass_CanEnterCell_CheckMovingInto_TAExt, 0x6)
{
	GET_STACK(CellClass*, into, STACK_OFFSET(0x34, 0x4));
	GET_STACK(bool const, isAlt, STACK_OFFSET(0x34, -0x23));
	GET(InfantryClass*, pThis, EBP);

	REF_STACK(unsigned char, occupyFlags, STACK_OFFSET(0x34, -0x21));
	REF_STACK(bool, isVehicleFlagSet, STACK_OFFSET(0x34, -0x22));

	AccountForMovingInto(into, isAlt, pThis, occupyFlags, isVehicleFlagSet);

	return 0;
}
