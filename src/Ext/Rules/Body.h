#pragma once

#include <RulesClass.h>
#include <Utilities/TemplateDef.h>

// Standalone port: a deliberately tiny stand-in for Phobos's RulesExt.
// We only need the two attachment layer-height globals plus a hook to load
// the [AttachmentTypes] list. It is a plain singleton (no Container needed):
// there is exactly one RulesClass, and these values are read once at rules
// load. Keyed on nothing, claims no offset — fully coexistence-safe.
class RulesExt
{
public:
	struct ExtData
	{
		Valueable<int> AttachmentTopLayerMinHeight { 500 };
		Valueable<int> AttachmentUndergroundLayerMaxHeight { -256 };
	};

	static ExtData Data;

	static ExtData* Global() { return &Data; }

	// Called from a rules-load hook: parses the [AttachmentTypes] list and the
	// two [General] globals.
	static void LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI);
	static void LoadFromINIFile(RulesClass* pThis, CCINIClass* pINI);
};
