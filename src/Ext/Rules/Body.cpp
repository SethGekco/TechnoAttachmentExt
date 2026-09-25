#include "Body.h"

#include <New/Type/GunnerProfileRule.h>

// Hooks.SplashRelatives.cpp -- warhead config, parsed once every type exists.
void TAExt_ReadSplashRules();

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

// ============================================================================
// I-c -- cross-type validation, after ALL type data is loaded.
//
// Seat: 0x679CAF (RulesData_LoadAfterTypeData). Antares, Ares and Phobos all
// chain here already, each reading and returning 0, so it is an established
// multi-consumer address rather than one to contest.
//
// This has to run here rather than in the per-type parser: a GunnerProfile named
// in [IFV] may point at a section that has not been read yet, so neither its
// capacity nor its own rule list is knowable during that type's own parse.
//
// TAExt_ValidateGunnerProfiles logs a one-line summary whenever any profile was
// configured, which doubles as the liveness proof for this seat -- "legal to
// chain" and "actually ran" are not the same thing.
// ============================================================================

DEFINE_HOOK(0x679CAF, RulesData_LoadAfterTypeData_TAExt, 0x5)
{
	TAExt_ValidateGunnerProfiles();
	TAExt_ReadSplashRules();
	return 0;
}
