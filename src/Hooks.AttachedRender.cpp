// Attachment draw-order (Y-sort) — ported from PR #352.
//
// By default an attached child sorts at its own Y position, so a taller host
// (a building especially) draws over it and hides it. The AttachmentType's
// YSortPosition lets the child sort relative to its host instead:
//   Default     = own YSort (vanilla; can be overlapped)
//   OverParent  = host's YSort + 1  (draws just over the host)
//   UnderParent = host's YSort - 1  (draws just under the host)
//
// GetYSort (ObjectClass vtable, 0x5F6BD0) is wrapped: for BuildingClass at its
// draw call site (0x449413), and for Unit/Infantry/Aircraft via their GetYSort
// vtable slots (all currently 0x5F6BD0). Non-attached objects and Default
// children fall through to the original GetYSort.
//
// LayerClass::AddObject calls GetYSort for every item its insert scan passes
// (thousands per new anim or unit), so the ExtData comes from
// TechnoExt::EverAttached instead of an ExtMap lookup (that hash probe was ~40%
// of the scan in 1,000-unit games): a techno outside it can have no parent and
// goes straight to the original.
//
// Encyclopedia-checked: none of these four addresses are hooked by any
// framework (collision-free).

#include <ObjectClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>
#include <Utilities/Debug.h>

#include <intrin.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

static int SortYAttached(ObjectClass* pThis, TechnoExt::ExtData* pExt)
{
	if (pExt && pExt->GetLiveParentAttachment())
	{
		auto const pAtt = pExt->GetLiveParentAttachment();
		// Per-slot Attachment<N>.YSortPosition overrides the AttachmentType's.
		auto const ySort = (pAtt->Data && pAtt->Data->YSortPosition.isset())
			? pAtt->Data->YSortPosition.Get()
			: pAtt->GetType()->YSortPosition.Get();
		auto const pParent = pAtt->Parent;

		auto const adjust = (pAtt->Data && pAtt->Data->YSortAdjust.isset())
			? pAtt->Data->YSortAdjust.Get()
			: pAtt->GetType()->YSortAdjust.Get();

		if (ySort != AttachmentYSortPosition::Default && pParent)
		{
			int const parentYSort = pParent->GetYSort();
			return parentYSort
				+ (ySort == AttachmentYSortPosition::OverParent ? 1 : -1)
				+ adjust;
		}

		// YSortAdjust is also useful on its own: bias the child's OWN sort
		// without tying it to the host's.
		if (adjust)
			return pThis->ObjectClass::GetYSort() + adjust;
	}

	// Original, non-virtual (avoid recursing into ourselves via the vtable slots).
	return pThis->ObjectClass::GetYSort();
}

// The ExtData the old path looked up for every call.
static TechnoExt::ExtData* LookUpSortY(ObjectClass* pThis)
{
	auto const pTechno = abstract_cast<TechnoClass*>(pThis);
	return pTechno ? TechnoExt::ExtMap.Find(pTechno) : nullptr;
}

// TAEXT_VERIFY_YSORT=1 in the game's environment (test runs only): every call
// also does the old lookup and counts a miss when the two would answer
// differently: EverAttached gives another ExtData than ExtMap.Find, or none
// for a techno with a live parent. The answer then comes from the old lookup,
// so a verify run is safe either way. Both lookups are timed (the object's
// first line is read before either, as the call reads it anyway); the fast
// path replaces the one with the other on every call, so their difference is
// what it saves, the timer's own cost cancelling out.
static int VerifySortY = -1;

static void CheckSortY(ObjectClass* pThis)
{
	static unsigned calls = 0;
	static unsigned listedCalls = 0;
	static unsigned misses = 0;
	static unsigned __int64 mapCycles = 0;
	static unsigned __int64 lookupCycles = 0;

	auto const pTechno = abstract_cast<TechnoClass*>(pThis);

	unsigned __int64 const start = __rdtsc();
	auto const pListed = static_cast<TechnoExt::ExtData*>(TechnoExt::EverAttached.Find(pThis));
	unsigned __int64 const middle = __rdtsc();
	auto const pExt = pTechno ? TechnoExt::ExtMap.Find(pTechno) : nullptr;
	unsigned __int64 const end = __rdtsc();

	++calls;
	listedCalls += pListed ? 1 : 0;
	mapCycles += middle - start;
	lookupCycles += end - middle;

	bool const miss = pListed ? pListed != pExt : pExt && pExt->GetLiveParentAttachment();
	misses += miss ? 1 : 0;

	if (miss || calls % 1048576 == 0)
	{
		Debug::Log("[TAExt] y-sort: %u calls, %u on listed technos (%u listed now), %u misses, "
			"EverAttached lookup %llu Mcycles, ExtMap lookup %llu Mcycles\n",
			calls, listedCalls, static_cast<unsigned>(TechnoExt::EverAttached.Size()), misses,
			mapCycles / 1000000, lookupCycles / 1000000);
	}
}

int __fastcall TechnoClass_SortY_Wrapper_TAExt(ObjectClass* pThis)
{
	if (VerifySortY)
	{
		if (VerifySortY < 0)
		{
			char value[8] {};
			VerifySortY = GetEnvironmentVariableA("TAEXT_VERIFY_YSORT", value, sizeof(value)) > 0 && value[0] == '1';
		}

		if (VerifySortY)
		{
			CheckSortY(pThis);
			return SortYAttached(pThis, LookUpSortY(pThis));
		}
	}

	// Never attached since its ExtData was made: ParentAttachment is null and
	// SortYAttached would only end in the original.
	auto const pExt = static_cast<TechnoExt::ExtData*>(TechnoExt::EverAttached.Find(pThis));
	if (!pExt)
		return pThis->ObjectClass::GetYSort();

	return SortYAttached(pThis, pExt);
}

DEFINE_FUNCTION_JUMP(CALL,  0x449413, TechnoClass_SortY_Wrapper_TAExt); // BuildingClass draw
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E235C, TechnoClass_SortY_Wrapper_TAExt); // AircraftClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB110, TechnoClass_SortY_Wrapper_TAExt); // InfantryClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5D28, TechnoClass_SortY_Wrapper_TAExt); // UnitClass
