#include "Body.h"

#include <AttachmentParsers.h>
#include <New/Type/AttachmentTypeClass.h>

RulesExt::ExtData RulesExt::Data {};

// Parses the [AttachmentTypes] enumerable list. Must run before TechnoType
// data is read so that Attachment<N>.Type= indices resolve.
void RulesExt::LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	AttachmentTypeClass::LoadFromINIList(pINI);
}

void RulesExt::LoadFromINIFile(RulesClass* pThis, CCINIClass* pINI)
{
	INI_EX exINI(pINI);

	Data.AttachmentTopLayerMinHeight.Read(exINI, "General", "AttachmentTopLayerMinHeight");
	Data.AttachmentUndergroundLayerMaxHeight.Read(exINI, "General", "AttachmentUndergroundLayerMaxHeight");
}
