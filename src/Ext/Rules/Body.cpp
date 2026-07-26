#include "Body.h"

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <AttachmentParsers.h>
#include <New/Type/AttachmentTypeClass.h>

RulesExt::ExtData RulesExt::Data {};

// Parses the [AttachmentTypes] enumerable list plus the two [General] globals.
// Must run before TechnoType data is read so that Attachment<N>.Type= indices
// resolve and so per-type layer heights can inherit these globals.
void RulesExt::LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	Debug::Log("[TAExt-trace] LoadBeforeTypeData: AttachmentTypeClass::LoadFromINIList begin\n");
	AttachmentTypeClass::LoadFromINIList(pINI);
	Debug::Log("[TAExt-trace] LoadBeforeTypeData: LoadFromINIList end (count=%u)\n",
		(unsigned)AttachmentTypeClass::Array.size());

	INI_EX exINI(pINI);
	Data.AttachmentTopLayerMinHeight.Read(exINI, "General", "AttachmentTopLayerMinHeight");
	Data.AttachmentUndergroundLayerMaxHeight.Read(exINI, "General", "AttachmentUndergroundLayerMaxHeight");
	Debug::Log("[TAExt-trace] LoadBeforeTypeData end\n");
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
