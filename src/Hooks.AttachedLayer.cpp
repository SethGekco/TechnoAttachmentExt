// Locomotion-layer-check skip for attached units (THE vehicle-on-building freeze fix).
//
// The freeze watchdog traced the hang to a building foundation-grid traversal
// whose recursion driver is a virtual call `call [vtable+0x78]` -- the unit's
// locomotion *layer* query (In_Which_Layer). During placement, both
// FootClass::Mark (0x4D37A2) and MapClass::PickUp (0x568831) issue that exact
// `call [eax+0x78]; cmp eax,2` layer check. For a unit riding the attachment
// locomotor, the layer query resolves through the parent and the
// Mark/PickUp -> foundation path recurses without terminating.
//
// PR #352 skips this check unconditionally (DEFINE_JUMP 0x4D37A2->0x4D37AE and
// 0x568831->0x568841). As a standalone coexisting with base Phobos we gate the
// skip on the attachment locomotor so vanilla unit movement is untouched. Both
// skip targets reload their registers, so bypassing the two-byte
// `mov eax,[this]` + the layer check is register-safe.

#include <FootClass.h>
#include <TechnoClass.h>

#include <Utilities/Macro.h>
#include <Helpers/Cast.h>

#include <Ext/Techno/Body.h>

// FootClass::Mark -- ESI = this.
DEFINE_HOOK(0x4D37A2, FootClass_Mark_SkipAttachmentLayerCheck_TAExt, 0x2)
{
	enum { SkipLayerCheck = 0x4D37AE, RunCheck = 0x0 };

	GET(FootClass*, pThis, ESI);

	return TechnoExt::HasAttachmentLoco(pThis) ? SkipLayerCheck : RunCheck;
}

// MapClass::PickUp -- EDI = the object being picked up.
DEFINE_HOOK(0x568831, MapClass_PickUp_SkipAttachmentLayerCheck_TAExt, 0x2)
{
	enum { SkipLayerCheck = 0x568841, RunCheck = 0x0 };

	GET(TechnoClass*, pThis, EDI);

	auto const pFoot = abstract_cast<FootClass*>(pThis);
	return (pFoot && TechnoExt::HasAttachmentLoco(pFoot)) ? SkipLayerCheck : RunCheck;
}
