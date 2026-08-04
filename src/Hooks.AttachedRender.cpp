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
// Encyclopedia-checked: none of these four addresses are hooked by any
// framework (collision-free).

#include <ObjectClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

int __fastcall TechnoClass_SortY_Wrapper_TAExt(ObjectClass* pThis)
{
	if (auto const pTechno = abstract_cast<TechnoClass*>(pThis))
	{
		auto const pExt = TechnoExt::ExtMap.Find(pTechno);
		if (pExt && pExt->ParentAttachment)
		{
			auto const ySort = pExt->ParentAttachment->GetType()->YSortPosition.Get();
			auto const pParent = pExt->ParentAttachment->Parent;

			if (ySort != AttachmentYSortPosition::Default && pParent)
			{
				int const parentYSort = pParent->GetYSort();
				return parentYSort
					+ (ySort == AttachmentYSortPosition::OverParent ? 1 : -1);
			}
		}
	}

	// Original, non-virtual (avoid recursing into ourselves via the vtable slots).
	return pThis->ObjectClass::GetYSort();
}

DEFINE_FUNCTION_JUMP(CALL,  0x449413, TechnoClass_SortY_Wrapper_TAExt); // BuildingClass draw
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E235C, TechnoClass_SortY_Wrapper_TAExt); // AircraftClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB110, TechnoClass_SortY_Wrapper_TAExt); // InfantryClass
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5D28, TechnoClass_SortY_Wrapper_TAExt); // UnitClass
