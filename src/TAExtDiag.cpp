#include "TAExtDiag.h"

#include <TechnoClass.h>
#include <TechnoTypeClass.h>

#include <Ext/Techno/Body.h>

#include <set>
#include <utility>

// Reports the first time each (site, TechnoType) pair takes an attachment
// "NoMove" override, together with whether the unit is genuinely attached.
// See TAExtDiag.h for why `parent=NO` is the signal we are hunting.
void TAExtDiag_ReportNoMove(const char* const tag, TechnoClass* const pThis)
{
	if (!pThis || !tag)
		return;

	auto const pType = pThis->GetTechnoType();
	if (!pType)
		return;

	// Tags are string literals, so the pointer is stable per call site.
	static std::set<std::pair<const void*, const void*>> reported;
	if (!reported.emplace(static_cast<const void*>(tag),
			static_cast<const void*>(pType)).second)
	{
		return; // already reported for this site + type
	}

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	bool const hasParent = pExt && pExt->ParentAttachment;
	auto const children = pExt ? pExt->ChildAttachments.size() : 0u;

	Debug::Log("[TAExt-diag] NoMove override: site=%s type=%s parent=%s children=%u%s\n",
		tag,
		pType->ID,
		hasParent ? "yes" : "NO",
		static_cast<unsigned int>(children),
		hasParent
			? ""
			: "   <<< ORPHAN: has the attachment locomotor but no ParentAttachment;"
			  " it will never move or auto-acquire, though it still fires on command");
}
