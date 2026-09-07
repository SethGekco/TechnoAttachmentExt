#include "AttachmentClass.h"

#include <Dir.h>
#include <BulletClass.h>
#include <BulletTypeClass.h>
#include <WarheadTypeClass.h>
#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>

#include <ObjBase.h>

#include <Ext/Techno/Body.h>
#include <Locomotion/AttachmentLocomotionClass.h>

#include <TAExtDiag.h>

std::vector<AttachmentClass*> AttachmentClass::Array;

AttachmentTypeClass* AttachmentClass::GetType()
{
	return AttachmentTypeClass::Array[this->Data->Type].get();
}

TechnoTypeClass* AttachmentClass::GetChildType()
{
	return this->Data->TechnoType.isset()
		? TechnoTypeClass::Array[this->Data->TechnoType]
		: nullptr;
}

// ---- J1 motion helpers -----------------------------------------------------
//
// Spin and bob are computed from Unsorted::CurrentFrame with INTEGER maths only.
// Two reasons that matters:
//  * facing and position are SYNCED state, so a per-client value (wall clock, or
//    float drift) would desync -- the frame counter is identical on every peer.
//  * deriving from the absolute frame rather than accumulating means no new
//    serialized state and no drift after a save/load; the motion is a pure
//    function of the frame number.
//
// The sine table is fixed-point (-1024..1024 over 256 steps) rather than std::sin
// so the result is bit-identical everywhere, independent of FPU mode.

namespace
{
	// sin(i * 2pi / 256) * 1024, quarter table mirrored below.
	const short TAExt_SinQuarter[65] =
	{
		   0,   25,   50,   75,  100,  125,  150,  175,  200,  224,  249,  273,  297,  321,  345,  369,
		 392,  415,  438,  460,  483,  505,  526,  548,  569,  590,  610,  630,  650,  669,  688,  706,
		 724,  742,  759,  775,  792,  807,  822,  837,  851,  865,  878,  891,  903,  915,  926,  936,
		 946,  955,  964,  972,  980,  987,  993,  999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
		1024
	};

	// index 0..255 -> sin * 1024
	int TAExt_Sin1024(int index)
	{
		index &= 0xFF;
		if (index <= 64)   return  TAExt_SinQuarter[index];
		if (index <= 128)  return  TAExt_SinQuarter[128 - index];
		if (index <= 192)  return -TAExt_SinQuarter[index - 128];
		return              -TAExt_SinQuarter[256 - index];
	}

	int TAExt_Cos1024(int index) { return TAExt_Sin1024(index + 64); }
}

// Current spin offset in raw facing units (65536 = one full turn), or 0.
int AttachmentClass::GetSpinRaw()
{
	if (!this->ResolveSpins())
		return 0;

	int period = this->ResolveSpinsPeriod();
	if (period == 0)
		return 0; // a zero period would divide by zero; treat as "not spinning"

	bool const reverse = period < 0;
	if (reverse)
		period = -period;

	// Modulo FIRST so the multiply cannot overflow on long games.
	int const phase = static_cast<int>(Unsorted::CurrentFrame % static_cast<unsigned int>(period));
	int raw = static_cast<int>((static_cast<long long>(phase) * 65536) / period);

	return reverse ? -raw : raw;
}

// Current slide offset in leptons along the configured axis, or 0. Same
// frame-derived, integer, stateless construction as the spin and bob above.
int AttachmentClass::GetSlideOffset()
{
	if (!this->ResolveSlides())
		return 0;

	int const range = this->ResolveSlidesRange();
	int period = this->ResolveSlidesPeriod();
	if (range == 0 || period <= 0)
		return 0;

	int const phase = static_cast<int>(Unsorted::CurrentFrame % static_cast<unsigned int>(period));
	int const index = (phase * 256) / period + this->ResolveSlidesPhase();

	return (range * TAExt_Sin1024(index)) / 1024;
}

// Current vertical bob offset in leptons, or 0.
int AttachmentClass::GetBobZ()
{
	if (!this->ResolveBobs())
		return 0;

	int const amplitude = this->ResolveBobsAmplitude();
	int period = this->ResolveBobsPeriod();
	if (amplitude == 0 || period <= 0)
		return 0;

	int const phase = static_cast<int>(Unsorted::CurrentFrame % static_cast<unsigned int>(period));
	// Map the frame position into the 256-step table, plus the configured offset
	// so sibling attachments can be told to bob out of step.
	int const index = (phase * 256) / period + this->ResolveBobsPhase();

	return (amplitude * TAExt_Sin1024(index)) / 1024;
}

CoordStruct AttachmentClass::GetChildLocation()
{
	// COPY, never a reference: Data->FLH is the shared TYPE config, and the motion
	// offsets below would otherwise corrupt it permanently for every user of it.
	CoordStruct flh = this->Data->FLH.Get();

	// Orbit: sweep the offset around the parent, so the centre of rotation is the
	// PARENT rather than the child. (Spin without orbit turns it on the spot.)
	if (this->ResolveSpins() && this->ResolveSpinsOrbit())
	{
		int const index = this->GetSpinRaw() >> 8; // raw facing -> 256-step table
		int const cos = TAExt_Cos1024(index);
		int const sin = TAExt_Sin1024(index);

		int const x = flh.X;
		int const y = flh.Y;
		flh.X = (x * cos - y * sin) / 1024;
		flh.Y = (x * sin + y * cos) / 1024;
	}

	// Slide along one host-relative axis. Applied AFTER any orbit rotation so the
	// slide follows the rotated frame rather than fighting it.
	if (int const slide = this->GetSlideOffset())
	{
		switch (this->ResolveSlidesAxis())
		{
		case 1:  flh.Y += slide; break;
		case 2:  flh.Z += slide; break;
		default: flh.X += slide; break;
		}
	}

	flh.Z += this->GetBobZ();

	return TechnoExt::GetFLHAbsoluteCoords(this->Parent, flh, this->Data->IsOnTurret);
}

AttachmentClass::~AttachmentClass()
{
	// clean up non-owning references
	if (this->Child)
	{
		auto const& pChildExt = TechnoExt::ExtMap.Find(Child);
		pChildExt->ParentAttachment = nullptr;
	}

	auto position = std::find(Array.begin(), Array.end(), this);
	if (position != Array.end())
		Array.erase(position);
}

void AttachmentClass::OnCreated()
{
	if (this->Child)
		return;

	if (this->ResolveRespawnAtCreation())
		this->CreateChild();
}

// True if the host owner satisfies the effective prerequisite, or if there is
// none. The per-slot AttachmentX.Prerequisite on the host overrides the
// AttachmentType's Prerequisite when it is non-empty. Evaluated every frame in
// AI() so the child is hidden (limbo'd) while the prerequisite is unmet and
// shown again once it is regained.
bool AttachmentClass::PrerequisitesMet()
{
	auto const pType = this->GetType();

	// Resolve each gate: per-slot override (when set) else the AttachmentType's.
	auto const& prereq = (this->Data && !this->Data->Prerequisite.empty())
		? this->Data->Prerequisite : pType->Prerequisite;
	auto const& prereqNeg = (this->Data && !this->Data->Prerequisite_Negative.empty())
		? this->Data->Prerequisite_Negative : pType->Prerequisite_Negative;
	auto const& reqHouses = (this->Data && !this->Data->RequiredHouses.empty())
		? this->Data->RequiredHouses : pType->RequiredHouses;
	auto const& forbHouses = (this->Data && !this->Data->ForbiddenHouses.empty())
		? this->Data->ForbiddenHouses : pType->ForbiddenHouses;

	// Sibling gates (E1) come from the AttachmentType. Singular ".Sibling" =
	// ANY of the list satisfies; plural ".Siblings" = ALL must be satisfied.
	auto const& sibIdxAny  = (this->Data && !this->Data->Prerequisite_Sibling_Index.empty())
		? this->Data->Prerequisite_Sibling_Index : pType->Prerequisite_Sibling_Index;
	auto const& sibTypeAny = (this->Data && !this->Data->Prerequisite_Sibling_Type.empty())
		? this->Data->Prerequisite_Sibling_Type : pType->Prerequisite_Sibling_Type;
	auto const& sibIdxAll  = (this->Data && !this->Data->Prerequisite_Siblings_Index.empty())
		? this->Data->Prerequisite_Siblings_Index : pType->Prerequisite_Siblings_Index;
	auto const& sibTypeAll = (this->Data && !this->Data->Prerequisite_Siblings_Type.empty())
		? this->Data->Prerequisite_Siblings_Type : pType->Prerequisite_Siblings_Type;
	bool const anySibling = !sibIdxAny.empty() || !sibTypeAny.empty()
		|| !sibIdxAll.empty() || !sibTypeAll.empty();

	// Host rank / health gates (checked at the end, but they count as gating here
	// or the early-out below would skip them entirely).
	auto const& minRank = (this->Data && this->Data->Prerequisite_MinRank.isset())
		? this->Data->Prerequisite_MinRank : pType->Prerequisite_MinRank;
	auto const& maxRank = (this->Data && this->Data->Prerequisite_MaxRank.isset())
		? this->Data->Prerequisite_MaxRank : pType->Prerequisite_MaxRank;
	auto const& minHealth = (this->Data && this->Data->Prerequisite_MinHealth.isset())
		? this->Data->Prerequisite_MinHealth : pType->Prerequisite_MinHealth;
	auto const& maxHealth = (this->Data && this->Data->Prerequisite_MaxHealth.isset())
		? this->Data->Prerequisite_MaxHealth : pType->Prerequisite_MaxHealth;

	bool const anyHostState = minRank.isset() || maxRank.isset()
		|| minHealth.isset() || maxHealth.isset();

	if (prereq.empty() && pType->Prerequisite_Lists.empty()
		&& prereqNeg.empty() && reqHouses.empty() && forbHouses.empty()
		&& !anySibling && !anyHostState)
		return true; // no gating at all

	// ---- sibling gates: siblings are the parent's other active child slots ----
	if (anySibling)
	{
		TechnoClass* const pParent = this->Parent;
		auto const pParentExt = pParent ? TechnoExt::ExtMap.Find(pParent) : nullptr;
		if (!pParentExt)
			return false; // sibling gate set but no parent context

		// Is there an active sibling of this exact type? (excludes our own child)
		auto const activeSiblingOfType = [&](TechnoTypeClass* pWanted) -> bool
		{
			if (!pWanted)
				return false;
			for (auto const& pSlot : pParentExt->ChildAttachments)
			{
				auto const pSib = pSlot ? pSlot->Child : nullptr;
				if (pSib && pSib != this->Child && pSib->IsAlive && !pSib->InLimbo
					&& pSib->GetTechnoType() == pWanted)
					return true;
			}
			return false;
		};

		// Sibling.Index (ANY): at least one listed slot active.
		if (!sibIdxAny.empty())
		{
			bool ok = false;
			for (int const idx : sibIdxAny)
				if (idx >= 0 && TechnoExt::IsSlotActive(pParent, static_cast<size_t>(idx)))
				{ ok = true; break; }
			if (!ok)
				return false;
		}

		// Sibling.Type (ANY): at least one active sibling of a listed type.
		if (!sibTypeAny.empty())
		{
			bool ok = false;
			for (auto const pT : sibTypeAny)
				if (activeSiblingOfType(pT)) { ok = true; break; }
			if (!ok)
				return false;
		}

		// Siblings.Index (ALL): every listed slot must be active.
		for (int const idx : sibIdxAll)
			if (!(idx >= 0 && TechnoExt::IsSlotActive(pParent, static_cast<size_t>(idx))))
				return false;

		// Siblings.Type (ALL): every listed type must be present among siblings.
		for (auto const pT : sibTypeAll)
			if (!activeSiblingOfType(pT))
				return false;
	}

	// ---- owner-based building / house gates ----
	auto const& prereqLists = pType->Prerequisite_Lists;
	bool const hasBuildingReq = !prereq.empty() || !prereqLists.empty();

	if (hasBuildingReq || !prereqNeg.empty() || !reqHouses.empty() || !forbHouses.empty())
	{
		auto const pOwner = this->Parent ? this->Parent->Owner : nullptr;
		if (!pOwner)
			return false;

		// Building requirement (Ares-style): satisfy the primary Prerequisite OR
		// any Prerequisite[N] alternative list. Each list is AND-within.
		if (hasBuildingReq)
		{
			auto const listPresent = [pOwner](const ValueableVector<BuildingTypeClass*>& list) -> bool
			{
				if (list.empty())
					return false; // empty = not a real alternative
				for (auto const pBld : list)
					if (pBld && pOwner->CountOwnedAndPresent(pBld) <= 0)
						return false;
				return true;
			};

			bool met = listPresent(prereq);
			if (!met)
				for (auto const& list : prereqLists)
					if (listPresent(list)) { met = true; break; }
			if (!met)
				return false;
		}

		// Negative buildings: NONE may be present.
		for (auto const pBld : prereqNeg)
			if (pBld && pOwner->CountOwnedAndPresent(pBld) > 0)
				return false;

		// Required houses: owner's country must be listed.
		if (!reqHouses.empty())
		{
			bool listed = false;
			for (auto const pHT : reqHouses)
				if (pHT && pHT == pOwner->Type) { listed = true; break; }
			if (!listed)
				return false;
		}

		// Forbidden houses: owner's country must NOT be listed.
		for (auto const pHT : forbHouses)
			if (pHT && pHT == pOwner->Type)
				return false;
	}

	// ---- host rank / health gates (A1-lite veterancy + damaged-variant) ----
	if (auto const pParent = this->Parent)
	{
		if (minRank.isset() || maxRank.isset())
		{
			auto const rank = pParent->Veterancy.GetRemainingLevel();

			if (minRank.isset() && static_cast<int>(rank) < static_cast<int>(minRank.Get()))
				return false;
			if (maxRank.isset() && static_cast<int>(rank) > static_cast<int>(maxRank.Get()))
				return false;
		}

		if (minHealth.isset() || maxHealth.isset())
		{
			int const hp = static_cast<int>(pParent->GetHealthPercentage() * 100.0);

			if (minHealth.isset() && hp < minHealth.Get())
				return false;
			if (maxHealth.isset() && hp > maxHealth.Get())
				return false;
		}
	}

	return true;
}

bool AttachmentClass::PrerequisiteDynamic()
{
	return (this->Data && this->Data->Prerequisite_Dynamic.isset())
		? this->Data->Prerequisite_Dynamic.Get()
		: this->GetType()->Prerequisite_Dynamic;
}

void AttachmentClass::CreateChild()
{
	// Static prerequisite (Prerequisite.Dynamic=no): gate creation on the
	// prerequisite being currently satisfied. Dynamic prerequisites create
	// unconditionally and are hidden/shown in AI() instead.
	if (!this->PrerequisiteDynamic() && !this->PrerequisitesMet())
		return;

	if (auto const pChildType = this->GetChildType())
	{
		// Standalone extension (beyond PR #352, which was UnitType-only):
		// allow any TechnoType child — vehicles, infantry, aircraft, buildings.
		// CreateObject yields the matching techno instance; AttachChild sorts
		// out the loco swap (Foot only) vs building handling.
		if (const auto pTechno = abstract_cast<TechnoClass*>(pChildType->CreateObject(this->Parent->Owner)))
		{
			this->AttachChild(pTechno);
		}
		else
		{
			Debug::Log("[" __FUNCTION__ "] Failed to create child %s of parent %s!\n",
				pChildType->ID, this->Parent->GetTechnoType()->ID);
		}
	}
}

// ---- per-slot override resolvers -------------------------------------------
// AttachmentN.<tag> on the host wins over the AttachmentType's value, so one
// AttachmentType can be reused across slots that need different behaviour.

bool AttachmentClass::ResolvePowersParent()
{
	return (this->Data && this->Data->PowersParent.isset())
		? this->Data->PowersParent.Get() : this->GetType()->PowersParent;
}

bool AttachmentClass::ResolvePowered()
{
	return (this->Data && this->Data->Powered.isset())
		? this->Data->Powered.Get() : this->GetType()->Powered;
}

bool AttachmentClass::ResolvePowersSiblings()
{
	return (this->Data && this->Data->PowersSiblings.isset())
		? this->Data->PowersSiblings.Get() : this->GetType()->PowersSiblings;
}

bool AttachmentClass::ResolvePoweredByParent()
{
	return (this->Data && this->Data->PoweredByParent.isset())
		? this->Data->PoweredByParent.Get() : this->GetType()->PoweredByParent;
}

int AttachmentClass::ResolveRequiresPassengers()
{
	return (this->Data && this->Data->RequiresPassengers.isset())
		? this->Data->RequiresPassengers.Get() : this->GetType()->RequiresPassengers;
}

const ValueableVector<BuildingTypeClass*>& AttachmentClass::ResolvePoweredBy()
{
	return (this->Data && !this->Data->PoweredBy.empty())
		? this->Data->PoweredBy : this->GetType()->PoweredBy;
}

bool AttachmentClass::ResolvePoweredByRequireAll()
{
	return (this->Data && this->Data->PoweredBy_RequireAll.isset())
		? this->Data->PoweredBy_RequireAll.Get() : this->GetType()->PoweredBy_RequireAll;
}

bool AttachmentClass::ResolvePoweredByRequirePower()
{
	return (this->Data && this->Data->PoweredBy_RequirePower.isset())
		? this->Data->PoweredBy_RequirePower.Get() : this->GetType()->PoweredBy_RequirePower;
}

int AttachmentClass::ResolvePoweredByRange()
{
	return (this->Data && this->Data->PoweredBy_Range.isset())
		? this->Data->PoweredBy_Range.Get() : this->GetType()->PoweredBy_Range;
}

int AttachmentClass::ResolvePoweredByHouse()
{
	// -1 on the slot means "not set" -> fall back to the AttachmentType.
	return (this->Data && this->Data->PoweredBy_House >= 0)
		? this->Data->PoweredBy_House : this->GetType()->PoweredBy_House;
}

bool AttachmentClass::ResolvePassSelection()
{
	return (this->Data && this->Data->PassSelection.isset())
		? this->Data->PassSelection.Get() : this->GetType()->PassSelection;
}

bool AttachmentClass::ResolveTransparentToMouse()
{
	return (this->Data && this->Data->TransparentToMouse.isset())
		? this->Data->TransparentToMouse.Get() : this->GetType()->TransparentToMouse;
}

const ValueableVector<TechnoTypeClass*>& AttachmentClass::ResolvePoweredType()
{
	return (this->Data && !this->Data->Powered_Type.empty())
		? this->Data->Powered_Type : this->GetType()->Powered_Type;
}

const ValueableVector<TechnoTypeClass*>& AttachmentClass::ResolvePowersSiblingsType()
{
	return (this->Data && !this->Data->PowersSiblings_Type.empty())
		? this->Data->PowersSiblings_Type : this->GetType()->PowersSiblings_Type;
}

const ValueableVector<TechnoTypeClass*>& AttachmentClass::ResolveRequiresSlotType()
{
	return (this->Data && !this->Data->RequiresSlot_Type.empty())
		? this->Data->RequiresSlot_Type : this->GetType()->RequiresSlot_Type;
}

const ValueableVector<int>& AttachmentClass::ResolvePowersSiblingsIndex()
{
	return (this->Data && !this->Data->PowersSiblings_Index.empty())
		? this->Data->PowersSiblings_Index : this->GetType()->PowersSiblings_Index;
}

const ValueableVector<int>& AttachmentClass::ResolveRequiresSlotIndex()
{
	return (this->Data && !this->Data->RequiresSlot_Index.empty())
		? this->Data->RequiresSlot_Index : this->GetType()->RequiresSlot_Index;
}

bool AttachmentClass::ResolveRespawnAtCreation()
{
	return (this->Data && this->Data->RespawnAtCreation.isset())
		? this->Data->RespawnAtCreation.Get() : this->GetType()->RespawnAtCreation;
}

int AttachmentClass::ResolveRespawnDelay()
{
	return (this->Data && this->Data->RespawnDelay.isset())
		? this->Data->RespawnDelay.Get() : this->GetType()->RespawnDelay;
}

bool AttachmentClass::ResolveInheritStopCommand()
{
	return (this->Data && this->Data->InheritCommands_StopCommand.isset())
		? this->Data->InheritCommands_StopCommand.Get() : this->GetType()->InheritCommands_StopCommand;
}

bool AttachmentClass::ResolveInheritDeployCommand()
{
	return (this->Data && this->Data->InheritCommands_DeployCommand.isset())
		? this->Data->InheritCommands_DeployCommand.Get() : this->GetType()->InheritCommands_DeployCommand;
}

bool AttachmentClass::ResolveInheritOwner()
{
	return (this->Data && this->Data->InheritOwner.isset())
		? this->Data->InheritOwner.Get() : this->GetType()->InheritOwner;
}

bool AttachmentClass::ResolveInheritStateEffects()
{
	return (this->Data && this->Data->InheritStateEffects.isset())
		? this->Data->InheritStateEffects.Get() : this->GetType()->InheritStateEffects;
}

bool AttachmentClass::ResolveInheritDestruction()
{
	return (this->Data && this->Data->InheritDestruction.isset())
		? this->Data->InheritDestruction.Get() : this->GetType()->InheritDestruction;
}

bool AttachmentClass::ResolveInheritHeightStatus()
{
	return (this->Data && this->Data->InheritHeightStatus.isset())
		? this->Data->InheritHeightStatus.Get() : this->GetType()->InheritHeightStatus;
}

bool AttachmentClass::ResolveSpins()
{
	return (this->Data && this->Data->Spins.isset())
		? this->Data->Spins.Get() : this->GetType()->Spins;
}

int AttachmentClass::ResolveSpinsPeriod()
{
	return (this->Data && this->Data->Spins_Period.isset())
		? this->Data->Spins_Period.Get() : this->GetType()->Spins_Period;
}

bool AttachmentClass::ResolveSpinsOrbit()
{
	return (this->Data && this->Data->Spins_Orbit.isset())
		? this->Data->Spins_Orbit.Get() : this->GetType()->Spins_Orbit;
}

bool AttachmentClass::ResolveSlides()
{
	return (this->Data && this->Data->Slides.isset())
		? this->Data->Slides.Get() : this->GetType()->Slides;
}

int AttachmentClass::ResolveSlidesAxis()
{
	return (this->Data && this->Data->Slides_Axis.isset())
		? this->Data->Slides_Axis.Get() : this->GetType()->Slides_Axis;
}

int AttachmentClass::ResolveSlidesRange()
{
	return (this->Data && this->Data->Slides_Range.isset())
		? this->Data->Slides_Range.Get() : this->GetType()->Slides_Range;
}

int AttachmentClass::ResolveSlidesPeriod()
{
	return (this->Data && this->Data->Slides_Period.isset())
		? this->Data->Slides_Period.Get() : this->GetType()->Slides_Period;
}

int AttachmentClass::ResolveSlidesPhase()
{
	return (this->Data && this->Data->Slides_Phase.isset())
		? this->Data->Slides_Phase.Get() : this->GetType()->Slides_Phase;
}

bool AttachmentClass::ResolveBobs()
{
	return (this->Data && this->Data->Bobs.isset())
		? this->Data->Bobs.Get() : this->GetType()->Bobs;
}

int AttachmentClass::ResolveBobsAmplitude()
{
	return (this->Data && this->Data->Bobs_Amplitude.isset())
		? this->Data->Bobs_Amplitude.Get() : this->GetType()->Bobs_Amplitude;
}

int AttachmentClass::ResolveBobsPeriod()
{
	return (this->Data && this->Data->Bobs_Period.isset())
		? this->Data->Bobs_Period.Get() : this->GetType()->Bobs_Period;
}

int AttachmentClass::ResolveBobsPhase()
{
	return (this->Data && this->Data->Bobs_Phase.isset())
		? this->Data->Bobs_Phase.Get() : this->GetType()->Bobs_Phase;
}

int AttachmentClass::ResolveAmmoParent()
{
	return (this->Data && this->Data->Ammo_Parent.isset())
		? this->Data->Ammo_Parent.Get() : this->GetType()->Ammo_Parent;
}

bool AttachmentClass::ResolveIntangible()
{
	return (this->Data && this->Data->Intangible.isset())
		? this->Data->Intangible.Get() : this->GetType()->Intangible;
}

bool AttachmentClass::ResolveOccupiesCell()
{
	return (this->Data && this->Data->OccupiesCell.isset())
		? this->Data->OccupiesCell.Get() : this->GetType()->OccupiesCell;
}

bool AttachmentClass::ResolveLowSelectionPriority()
{
	return (this->Data && this->Data->LowSelectionPriority.isset())
		? this->Data->LowSelectionPriority.Get() : this->GetType()->LowSelectionPriority;
}

bool AttachmentClass::ResolveConvertKeepHealth()
{
	return (this->Data && this->Data->Convert_KeepHealth.isset())
		? this->Data->Convert_KeepHealth.Get() : this->GetType()->Convert_KeepHealth;
}

bool AttachmentClass::ResolveConvertKeepVeterancy()
{
	return (this->Data && this->Data->Convert_KeepVeterancy.isset())
		? this->Data->Convert_KeepVeterancy.Get() : this->GetType()->Convert_KeepVeterancy;
}

// ---- convert-in-place ------------------------------------------------------

TechnoTypeClass* AttachmentClass::ResolveDesiredChildType()
{
	auto const pType = this->GetType();
	if (!pType || pType->ConvertRules.empty())
		return nullptr; // feature unused by this AttachmentType

	auto const pHost = this->Parent;
	if (!pHost)
		return nullptr;

	// Conditions read the HOST, matching Prerequisite.MinHealth/.MinRank.
	int const hp = static_cast<int>(pHost->GetHealthPercentage() * 100.0);
	int const rank = static_cast<int>(pHost->Veterancy.GetRemainingLevel());

	for (auto const& rule : pType->ConvertRules)
	{
		if (!rule.To)
			continue;
		if (rule.MinHealth >= 0 && hp < rule.MinHealth)
			continue;
		if (rule.MaxHealth >= 0 && hp > rule.MaxHealth)
			continue;
		if (rule.MinRank >= 0 && rank < rule.MinRank)
			continue;
		if (rule.MaxRank >= 0 && rank > rule.MaxRank)
			continue;

		return rule.To; // first match wins
	}

	// Nothing matches -> back to the slot's configured type. This is what makes
	// reverting automatic, so there is no separate revert flag.
	return this->GetChildType();
}

void AttachmentClass::ConvertChildTo(TechnoTypeClass* pNewType)
{
	auto const pOld = this->Child;
	if (!pOld || !pNewType)
		return;

	auto const pType = this->GetType();

	// Capture carry-over state before the old instance goes away.
	double const healthPercent = pOld->GetHealthPercentage();
	float const veterancy = pOld->Veterancy.Veterancy;
	HouseClass* const pOwner = pOld->Owner ? pOld->Owner : this->Parent->Owner;

	// Unlink FIRST so removing the old child cannot re-enter this slot through the
	// destruction path (HandleDestructionAsChild -> ChildDestroyed), then remove it
	// quietly: this is a replacement, not a death, so no destruction weapons, no
	// ParentDestructionMission, no kill credit.
	this->DetachChildCore();
	pOld->Limbo();   // virtual: dispatches to the real per-class Limbo
	pOld->UnInit();

	auto const pNew = abstract_cast<TechnoClass*>(pNewType->CreateObject(pOwner));
	if (!pNew)
	{
		Debug::Log("[" __FUNCTION__ "] Failed to create converted child %s for parent %s!\n",
			pNewType->ID, this->Parent->GetTechnoType()->ID);
		return; // slot is now empty; the respawn path will refill it
	}

	if (!this->AttachChild(pNew))
	{
		pNew->UnInit(); // could not adopt it -- don't leak an orphan on the map
		return;
	}

	if (this->ResolveConvertKeepHealth())
	{
		// Carried as a PERCENTAGE so the replacement keeps the same damage ratio
		// even when the two types have different Strength.
		int const strength = pNew->GetTechnoType()->Strength;
		int hp = static_cast<int>(strength * healthPercent);
		if (hp < 1)
			hp = 1; // never spawn it pre-dead
		if (hp > strength)
			hp = strength;
		pNew->Health = hp;
	}

	if (this->ResolveConvertKeepVeterancy())
		pNew->Veterancy.Veterancy = veterancy;

	// The new child is left for the next AI tick to unlimbo and position, exactly
	// as a freshly created one is.
}

void AttachmentClass::AI()
{
	TAEXT_DIAG_COUNT("AttachmentClass::AI");
	AttachmentTypeClass* pType = this->GetType();

	if (!this->Child)
	{
		if (this->ResolveRespawnDelay() == 0)
		{
			this->CreateChild();
		}
		else if (this->ResolveRespawnDelay() > 0)
		{
			if (!this->RespawnTimer.HasStarted())
			{
				this->RespawnTimer.Start(this->ResolveRespawnDelay());
			}
			else if (this->RespawnTimer.Completed())
			{
				this->CreateChild();
				this->RespawnTimer.Stop();
			}
		}
	}

	if (this->Child)
	{
		// Convert-in-place: if the child should currently be a different TechnoType
		// (damaged/wrecked variant, upgrade), swap it now and let the next tick
		// unlimbo and position the replacement -- exactly like a fresh child.
		if (auto const pDesired = this->ResolveDesiredChildType())
		{
			if (this->Child->GetTechnoType() != pDesired)
			{
				this->ConvertChildTo(pDesired);
				return;
			}
		}

		// Hide the child (limbo) while the host is in limbo OR (for a dynamic
		// prerequisite) the prerequisite is unmet; show it again otherwise.
		// Static prerequisites don't hide/show live (handled at create time).
		bool const prereqHide = this->PrerequisiteDynamic() && !this->PrerequisitesMet();
		bool const hide = this->Parent->InLimbo || prereqHide;

		if (this->Child->InLimbo && !hide)
			this->Unlimbo();
		else if (!this->Child->InLimbo && hide)
			this->Limbo();

		// Don't position/sync a hidden (limbo'd) child.
		if (this->Child->InLimbo)
			return;

		this->Child->SetLocation(this->GetChildLocation());

		DirStruct childDir = this->Data->IsOnTurret
			? this->Parent->SecondaryFacing.Current() : this->Parent->PrimaryFacing.Current();

		childDir.Raw += DirStruct(this->Data->RotationAdjust).Raw; // overflow = free modulo for rotation
		childDir.Raw += static_cast<unsigned short>(this->GetSpinRaw()); // J1 spin

		this->Child->PrimaryFacing.SetCurrent(childDir);
		// TODO handle secondary facing in case the turret is idle

		FootClass* pParentAsFoot = abstract_cast<FootClass*>(this->Parent);
		FootClass* pChildAsFoot = abstract_cast<FootClass*>(this->Child);
		if (pParentAsFoot && pChildAsFoot)
		{
			pChildAsFoot->TubeIndex = pParentAsFoot->TubeIndex;
		}

		if (this->ResolveInheritStateEffects())
		{
			this->Child->IsFallingDown = this->Parent->IsFallingDown;
			this->Child->WasFallingDown = this->Parent->WasFallingDown;
			this->Child->CloakState = this->Parent->CloakState;
			this->Child->WarpingOut = this->Parent->WarpingOut;
			this->Child->unknown_280 = this->Parent->unknown_280; // sth related to teleport
			this->Child->BeingWarpedOut = this->Parent->BeingWarpedOut;
			this->Child->Deactivated = this->Parent->Deactivated;
			//this->Child->Flash(this->Parent->Flashing.DurationRemaining);

			this->Child->IronCurtainTimer = this->Parent->IronCurtainTimer;
			this->Child->IdleActionTimer = this->Parent->IdleActionTimer;
			this->Child->IronTintTimer = this->Parent->IronTintTimer;
			this->Child->CloakDelayTimer = this->Parent->CloakDelayTimer;
			this->Child->ChronoLockRemaining = this->Parent->ChronoLockRemaining;
			this->Child->Berzerk = this->Parent->Berzerk;
			this->Child->BerzerkDurationLeft = this->Parent->BerzerkDurationLeft;
			this->Child->ChronoWarpedByHouse = this->Parent->ChronoWarpedByHouse;
			this->Child->EMPLockRemaining = this->Parent->EMPLockRemaining;
			this->Child->ShouldLoseTargetNow = this->Parent->ShouldLoseTargetNow;
		}

		if (this->ResolveInheritOwner())
			this->Child->SetOwningHouse(this->Parent->GetOwningHouse(), false);
	}
}

// Called in Kill_Cargo, handles logics for parent destruction on children
void AttachmentClass::Destroy(TechnoClass* pSource)
{
	if (this->Child)
	{
		auto const pChildExt = TechnoExt::ExtMap.Find(this->Child);
		pChildExt->ParentAttachment = nullptr;

		auto pType = this->GetType();

		auto const& dwChild = (this->Data && this->Data->DestructionWeapon_Child.isset())
			? this->Data->DestructionWeapon_Child : pType->DestructionWeapon_Child;
		if (dwChild.isset())
			TechnoExt::FireWeaponAtSelf(this->Child, dwChild.Get());

		if (this->ResolveInheritDestruction() && this->Child)
			TechnoExt::Kill(this->Child, pSource);
		else
		{
			auto const& pdm = (this->Data && this->Data->ParentDestructionMission.isset())
				? this->Data->ParentDestructionMission : pType->ParentDestructionMission;
			if (!this->Child->InLimbo && pdm.isset())
				this->Child->QueueMission(pdm.Get(), false);
		}

		this->Child = nullptr;
	}
}

void AttachmentClass::ChildDestroyed()
{
	if (this->Child)
	{
		if (auto const pChildExt = TechnoExt::ExtMap.Find(this->Child))
			pChildExt->ParentAttachment = nullptr;

		AttachmentTypeClass* pType = this->GetType();
		auto const& dwParent = (this->Data && this->Data->DestructionWeapon_Parent.isset())
			? this->Data->DestructionWeapon_Parent : pType->DestructionWeapon_Parent;
		if (dwParent.isset())
			TechnoExt::FireWeaponAtSelf(this->Parent, dwParent.Get());

		this->Child = nullptr;
	}
}

void AttachmentClass::Unlimbo()
{
	if (this->Child)
	{
		CoordStruct childCoord = TechnoExt::GetFLHAbsoluteCoords(
			this->Parent, this->Data->FLH, this->Data->IsOnTurret);

		DirStruct childDir = this->Data->IsOnTurret
			? this->Parent->SecondaryFacing.Current() : this->Parent->PrimaryFacing.Current();

		childDir.Raw += DirStruct(this->Data->RotationAdjust).Raw; // overflow = free modulo for rotation

		++Unsorted::ScenarioInit;
		this->Child->Unlimbo(childCoord, childDir.GetDir());
		--Unsorted::ScenarioInit;
	}
}

void AttachmentClass::Limbo()
{
	if (this->Child)
		this->Child->Limbo();
}

bool AttachmentClass::AttachChild(TechnoClass* pChild)
{
	if (this->Child)
		return false;

	// Standalone extension: accept any techno instance (vehicle/infantry/
	// aircraft/building), not just vehicles as in PR #352. The locomotor swap
	// below only applies to FootClass children; buildings have no locomotor.
	switch (pChild->WhatAmI())
	{
	case AbstractType::Unit:
	case AbstractType::Infantry:
	case AbstractType::Aircraft:
	case AbstractType::Building:
		break;
	default:
		return false;
	}

	if (auto const pChildAsFoot = abstract_cast<FootClass*>(pChild))
	{
		if (IPersistPtr pLocoPersist = pChildAsFoot->Locomotor)
		{
			CLSID locoCLSID { };
			if (SUCCEEDED(pLocoPersist->GetClassID(&locoCLSID))
				&& locoCLSID != __uuidof(AttachmentLocomotionClass))
			{
				LocomotionClass::ChangeLocomotorTo(pChildAsFoot,
					__uuidof(AttachmentLocomotionClass));
			}
		}
	}

	this->AttachChildCore(pChild);

	AttachmentTypeClass* pType = this->GetType();

	if (this->ResolveInheritOwner())
	{
		if (auto pController = this->Child->MindControlledBy)
			pController->CaptureManager->FreeUnit(this->Child);
	}

	return true;
}

bool AttachmentClass::DetachChild()
{
	if (this->Child)
	{
		AttachmentTypeClass* pType = this->GetType();

		auto const& pdetm = (this->Data && this->Data->ParentDetachmentMission.isset())
			? this->Data->ParentDetachmentMission : pType->ParentDetachmentMission;
		if (!this->Child->InLimbo && pdetm.isset())
			this->Child->QueueMission(pdetm.Get(), false);

		// FIXME this won't work probably
		if (this->ResolveInheritOwner())
			this->Child->SetOwningHouse(this->Parent->GetOriginalOwner(), false);

		// remove the attachment locomotor manually just to be safe
		if (auto const pChildAsFoot = abstract_cast<FootClass*>(this->Child))
			LocomotionClass::End_Piggyback(pChildAsFoot->Locomotor);

		this->DetachChildCore();

		return true;
	}

	return false;
}


void AttachmentClass::AttachChildCore(TechnoClass* pChild)
{
	this->Child = pChild;
	TechnoExt::ExtMap.Find(pChild)->ParentAttachment = this;
}

void AttachmentClass::DetachChildCore()
{
	if (this->Child)
	{
		TechnoExt::ExtMap.Find(this->Child)->ParentAttachment = nullptr;
		this->Child = nullptr;
	}
}

void AttachmentClass::InvalidatePointer(void* ptr)
{
	AnnounceInvalidPointer(this->Parent, ptr);
	AnnounceInvalidPointer(this->Child, ptr);
}

#pragma region Save/Load

template <typename T>
bool AttachmentClass::Serialize(T& stm)
{
	return stm
		.Process(this->Data)
		.Process(this->Parent)
		.Process(this->Child)
		.Process(this->RespawnTimer)
		.Success();
}

bool AttachmentClass::Load(PhobosStreamReader& stm, bool RegisterForChange)
{
	return Serialize(stm);
}

bool AttachmentClass::Save(PhobosStreamWriter& stm) const
{
	return const_cast<AttachmentClass*>(this)->Serialize(stm);
}

#pragma endregion
