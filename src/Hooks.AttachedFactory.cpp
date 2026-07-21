// Attached-factory exit-cell redirect (standalone feature, beyond PR #352).
//
// A factory building (barracks/war factory) attached as an attachment child —
// typically with a 0-cell foundation so it doesn't block pathing — has no
// footprint for the vanilla exit-cell search to work from, so produced units
// can't reliably leave it. We chain into BuildingClass::FindExitCell (the same
// seam Phobos uses for its BarracksExitCell feature, at 0x44EFD8) and, for
// attached buildings only, anchor the exit cell on the PARENT vehicle: scan
// cells outward from the parent's current cell and return the first one the
// produced unit can legally occupy.
//
// The scan order is fixed/deterministic and uses only the game's own
// IsCellOccupied test, so it is multiplayer-sync-safe (no unsynced RNG).
// Returns 0 (pass-through) for every non-attached building, so normal
// factories are completely unaffected.

#include <BuildingClass.h>
#include <TechnoClass.h>
#include <CellClass.h>
#include <MapClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Macro.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>

DEFINE_HOOK(0x44EFD8, BuildingClass_FindExitCell_AttachedFactory, 0x6)
{
	enum { ReturnFromFunction = 0x44F037 };

	GET(BuildingClass*, pThis, EBX);

	// Only redirect for buildings that are attachment children.
	if (!TechnoExt::IsAttached(pThis))
		return 0;

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt || !pExt->ParentAttachment)
		return 0;

	auto const pParent = pExt->ParentAttachment->Parent;
	if (!pParent)
		return 0;

	GET(TechnoClass*, pTechno, ESI); // the unit being placed
	if (!pTechno)
		return 0;

	// Mark the produced unit to auto-scatter off the exit cell on its next
	// update — vanilla gives factory-exited infantry Mission::Area_Guard, so it
	// would otherwise sit on the exit cell and block the next unit (which then
	// refunds). Set regardless of whether our own scan below picks the cell.
	if (auto const pUnitExt = TechnoExt::ExtMap.Find(pTechno))
		pUnitExt->PendingExitScatter = true;

	REF_STACK(CellStruct, resultCell, STACK_OFFSET(0x30, -0x20));

	auto const pType = pTechno->GetTechnoType();

	// Anchor the exit search on the parent vehicle's current cell.
	CellStruct const anchor = pParent->GetMapCoords();

	// Deterministic outward scan (expanding rings around the parent). The first
	// cell the produced unit can legally occupy wins.
	static constexpr struct { int dx, dy; } offsets[] =
	{
		{  0, -1 }, {  1,  0 }, {  0,  1 }, { -1,  0 },
		{  1, -1 }, {  1,  1 }, { -1,  1 }, { -1, -1 },
		{  0, -2 }, {  2,  0 }, {  0,  2 }, { -2,  0 },
		{  2, -1 }, {  2,  1 }, { -2,  1 }, { -2, -1 },
		{  1, -2 }, {  1,  2 }, { -1,  2 }, { -1, -2 },
		{  2, -2 }, {  2,  2 }, { -2,  2 }, { -2, -2 },
	};

	for (auto const& off : offsets)
	{
		CellStruct cand = anchor;
		cand.X = (short)(anchor.X + off.dx);
		cand.Y = (short)(anchor.Y + off.dy);

		if (!MapClass::Instance.CoordinatesLegal(cand))
			continue;

		auto const pCell = MapClass::Instance.GetCellAt(cand);

		// Reject cells blocked by units/buildings...
		if (pTechno->IsCellOccupied(pCell, FacingType::None, -1, nullptr, true) != Move::OK)
			continue;

		// ...AND cells the unit can't stand on due to TERRAIN (trees, ore
		// drills / TIBTRE, cliffs, water). IsCellOccupied misses these, which
		// let units spawn onto impassable terrain and get stuck. IsClearToMove
		// ignores infantry/vehicles here (occupation handled above) and just
		// validates terrain passability for this unit's movement type.
		if (pType && !pCell->IsClearToMove(pType->SpeedType, true, true, -1, pType->MovementZone, -1, pCell->ContainsBridge()))
			continue;

		resultCell = cand;
		return ReturnFromFunction;
	}

	// Couldn't find a spot near the parent — let vanilla/Phobos try.
	return 0;
}
