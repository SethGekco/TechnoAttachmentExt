#include "Body.h"

#include <Utilities/Macro.h>

#include <AttachmentParsers.h>
#include <New/Type/AttachmentTypeClass.h>

RulesExt::ExtData RulesExt::Data {};

// Parses the [AttachmentTypes] enumerable list plus the two [General] globals.
// Must run before TechnoType data is read so that Attachment<N>.Type= indices
// resolve and so per-type layer heights can inherit these globals.
void RulesExt::LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	AttachmentTypeClass::LoadFromINIList(pINI);

	INI_EX exINI(pINI);
	Data.AttachmentTopLayerMinHeight.Read(exINI, "General", "AttachmentTopLayerMinHeight");
	Data.AttachmentUndergroundLayerMaxHeight.Read(exINI, "General", "AttachmentUndergroundLayerMaxHeight");
}

// ============================================================================
// Rules load hook — 0x679A15 (RulesData_LoadBeforeTypeData), verified Phobos.
// ECX = RulesClass*, [ESP+0x4] = CCINIClass*.
// ============================================================================

DEFINE_HOOK(0x679A15, RulesData_LoadBeforeTypeData_TAExt, 0x6)
{
	GET(RulesClass*, pItem, ECX);
	GET_STACK(CCINIClass*, pINI, 0x4);

	RulesExt::LoadBeforeTypeData(pItem, pINI);

	return 0;
}
