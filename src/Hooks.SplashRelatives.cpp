// SplashToRelatives -- a warhead that spreads damage from its victim onto the
// victim's attachment relatives. See docs/DESIGN-SplashOntoRelatives.md.
//
// WHY THE CODE IS HERE AND NOT IN WeaponExt
// -----------------------------------------
// The tag lives on a WarheadType, which looks like WeaponExt's territory, and the
// D design originally said to hand it over. That was wrong: WeaponExt has
// BulletType/TechnoType/WeaponType containers and no attachment awareness at all,
// so it cannot reach the graph this feature is entirely about. A tag's natural
// INI home and a feature's natural CODE home are different questions. Reading a
// warhead section from here is no harder than reading a TechnoType one, and it
// avoids a permanent cross-DLL load-order dependency.
//
// HOW IT DIFFERS FROM Intercepts.Parent (already shipped)
// -------------------------------------------------------
//   Intercepts.Parent  fire time. The shot is REDIRECTED; the plate is hit
//                      INSTEAD of the hull. Requires LegalTarget, because the
//                      engine must be able to aim at the child.
//   SplashToRelatives  detonation. The shot lands as aimed and damage ALSO
//                      reaches the relatives. Needs no LegalTarget at all,
//                      because nothing is ever aimed at them -- damage is applied
//                      directly.
//
// That last point is the reason this is worth having: it reaches the
// Intangible, non-targetable children that are the NORMAL case for attachments,
// which interception structurally cannot.
//
// SEAT -- derived, not guessed
// ----------------------------
// ObjectClass::ReceiveDamage is virtual, so the vtable slot is wrapped (the same
// pattern already proven for GetYSort and DrawIt). The slot was found by anchor,
// not by counting YRpp virtuals, which has given a wrong answer on this project:
//   * 0x702050 is inside TechnoClass::ReceiveDamage (our existing
//     destroyed-by-damage hook). Scanning back for the function start gives
//     0x701900.
//   * .rdata contains exactly one pointer to 0x701900, at 0x7F4ACC -- which is
//     the Building vtable's GetYSort slot + 0xB4.
//   * Applying +0xB4 to the other three GetYSort slots yields a distinct
//     per-class override in each class's own code range (Aircraft 0x4165C0,
//     Infantry 0x517FA0, Unit 0x737C90), with Building inheriting the
//     TechnoClass implementation. Consistent across all four, so the delta is
//     believable.
//
// DETERMINISM: damage dealt, the relative list and slot order are all synced;
// there is no RNG here at all.

#include <TechnoClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <AircraftClass.h>
#include <BuildingClass.h>
#include <WarheadTypeClass.h>
#include <CCINIClass.h>

#include <unordered_map>

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <AttachmentParsers.h>
#include <Ext/Techno/Body.h>
#include <New/Entity/AttachmentClass.h>

namespace
{
	struct SplashRule
	{
		AttachmentRelation To = AttachmentRelation::AllChildren;
		int Percent = 50;
		int Max = 0;      // 0 = every match
		int MinDamage = 1;
		WarheadTypeClass* Warhead = nullptr; // null = reuse the original
	};

	// Warheads are few and this is static config, so a map keyed by type beats
	// standing up a whole new ExtContainer for five fields.
	std::unordered_map<WarheadTypeClass*, SplashRule> Rules;

	// Re-entrancy depth. Splashing applies damage, which re-enters ReceiveDamage;
	// if a child's own relatives also splash, that chains. Slot counts bound most
	// of it, but a mod can build a cycle, and a runaway would recurse until the
	// stack gives out rather than producing a diagnosable crash.
	int SplashDepth = 0;
	constexpr int MaxSplashDepth = 3;
}

// Parsed from the LoadAfterTypeData pass, where every WarheadType exists.
void TAExt_ReadSplashRules()
{
	Rules.clear();

	auto const pINI = CCINIClass::INI_Rules;
	if (!pINI)
		return;

	int configured = 0;

	for (int i = 0; i < WarheadTypeClass::Array->Count; ++i)
	{
		auto const pWH = WarheadTypeClass::Array->GetItem(i);
		if (!pWH)
			continue;

		char buffer[64];
		if (pINI->ReadString(pWH->ID, "SplashToRelatives", "", buffer, sizeof(buffer)) <= 0)
			continue;

		SplashRule rule;

		AttachmentRelation rel;
		if (!TAExt_ParseRelation(buffer, rel))
		{
			Debug::INIParseFailed(pWH->ID, "SplashToRelatives", buffer,
				"Expected children, siblings, parent or root");
			continue;
		}
		rule.To = rel;

		rule.Percent = pINI->ReadInteger(pWH->ID, "SplashToRelatives.Percent", rule.Percent);
		rule.Max = pINI->ReadInteger(pWH->ID, "SplashToRelatives.Max", rule.Max);
		rule.MinDamage = pINI->ReadInteger(pWH->ID, "SplashToRelatives.MinDamage", rule.MinDamage);

		char whBuf[64];
		if (pINI->ReadString(pWH->ID, "SplashToRelatives.Warhead", "", whBuf, sizeof(whBuf)) > 0)
		{
			rule.Warhead = WarheadTypeClass::Find(whBuf);
			if (!rule.Warhead)
				Debug::INIParseFailed(pWH->ID, "SplashToRelatives.Warhead", whBuf,
					"not a WarheadType; the original warhead will be used");
		}

		if (rule.Percent <= 0)
		{
			Debug::INIParseFailed(pWH->ID, "SplashToRelatives.Percent", "",
				"must be positive; rule ignored");
			continue;
		}

		Rules.emplace(pWH, rule);
		++configured;
	}

	if (configured)
		Debug::Log("[TAExt] SplashToRelatives: %d warhead(s) configured.\n", configured);
}

namespace
{
	// Apply the splash after the victim has taken its own damage.
	void ApplySplash(TechnoClass* pVictim, WarheadTypeClass* pWH, int dealt,
		TechnoClass* pAttacker, HouseClass* pAttackingHouse)
	{
		if (!pVictim || !pWH || dealt <= 0 || Rules.empty())
			return;

		auto const it = Rules.find(pWH);
		if (it == Rules.end())
			return;

		auto const& rule = it->second;

		int share = (dealt * rule.Percent) / 100;
		if (share < rule.MinDamage)
			share = rule.MinDamage;
		if (share <= 0)
			return;

		// Snapshot the relatives BEFORE applying anything: each application can
		// destroy an object, which re-enters our removal hooks and mutates the
		// very list we would otherwise be walking.
		auto relatives = TechnoExt::ResolveRelatives(pVictim, rule.To);
		if (relatives.empty())
			return;

		auto const pSplashWH = rule.Warhead ? rule.Warhead : pWH;

		int applied = 0;
		for (auto const pRelative : relatives)
		{
			if (rule.Max > 0 && applied >= rule.Max)
				break;

			// Re-validate every iteration: a previous application may have killed
			// this one, or the victim, or freed the entry.
			if (!pRelative || !pRelative->IsAlive || pRelative == pVictim)
				continue;

			int damage = share;
			pRelative->ReceiveDamage(&damage, 0, pSplashWH, pAttacker, false, false, pAttackingHouse);
			++applied;
		}
	}

	// A vtable slot is __thiscall, so each wrapper is __fastcall with a dummy EDX.
	// Signature: DamageState ReceiveDamage(int* pDamage, int distance,
	//   WarheadTypeClass*, ObjectClass* pAttacker, bool ignoreDefenses,
	//   bool preventPassengerEscape, HouseClass* pAttackingHouse)
	using ReceiveDamageFunc = int(__thiscall*)(TechnoClass*, int*, int,
		WarheadTypeClass*, ObjectClass*, bool, bool, HouseClass*);

	int DoReceiveDamage(DWORD original, TechnoClass* pThis, int* pDamage, int distance,
		WarheadTypeClass* pWH, ObjectClass* pAttacker, bool ignoreDefenses,
		bool preventPassengerEscape, HouseClass* pAttackingHouse)
	{
		// Health before/after is the damage ACTUALLY dealt, post-armour -- which is
		// what Percent should be of. The incoming *pDamage is pre-mitigation and
		// would over-splash against heavily armoured hulls.
		int const before = pThis->Health;

		int const result = reinterpret_cast<ReceiveDamageFunc>(original)(
			pThis, pDamage, distance, pWH, pAttacker, ignoreDefenses,
			preventPassengerEscape, pAttackingHouse);

		if (SplashDepth >= MaxSplashDepth)
			return result;

		int const dealt = before - pThis->Health;
		if (dealt <= 0)
			return result;

		++SplashDepth;
		ApplySplash(pThis, pWH, dealt, abstract_cast<TechnoClass*>(pAttacker), pAttackingHouse);
		--SplashDepth;

		return result;
	}
}

#define TAEXT_RECEIVEDAMAGE_WRAPPER(cls, original)                               \
	int __fastcall cls##_ReceiveDamage_Splash_TAExt(TechnoClass* pThis, void*,   \
		int* pDamage, int distance, WarheadTypeClass* pWH, ObjectClass* pAttacker,\
		bool ignoreDefenses, bool preventPassengerEscape, HouseClass* pHouse)    \
	{                                                                            \
		return DoReceiveDamage(original, pThis, pDamage, distance, pWH,          \
			pAttacker, ignoreDefenses, preventPassengerEscape, pHouse);          \
	}

TAEXT_RECEIVEDAMAGE_WRAPPER(UnitClass,     0x737C90)
TAEXT_RECEIVEDAMAGE_WRAPPER(InfantryClass, 0x517FA0)
TAEXT_RECEIVEDAMAGE_WRAPPER(AircraftClass, 0x4165C0)
TAEXT_RECEIVEDAMAGE_WRAPPER(BuildingClass, 0x701900)

DEFINE_FUNCTION_JUMP(VTABLE, 0x7F5DDC, UnitClass_ReceiveDamage_Splash_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7EB1C4, InfantryClass_ReceiveDamage_Splash_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7E2410, AircraftClass_ReceiveDamage_Splash_TAExt);
DEFINE_FUNCTION_JUMP(VTABLE, 0x7F4ACC, BuildingClass_ReceiveDamage_Splash_TAExt);
