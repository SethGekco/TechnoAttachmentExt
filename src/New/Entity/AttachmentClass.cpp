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

CoordStruct AttachmentClass::GetChildLocation()
{
	auto& flh = this->Data->FLH.Get();
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

	if (this->GetType()->RespawnAtCreation)
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
	auto const& sibIdxAny  = pType->Prerequisite_Sibling_Index;
	auto const& sibTypeAny = pType->Prerequisite_Sibling_Type;
	auto const& sibIdxAll  = pType->Prerequisite_Siblings_Index;
	auto const& sibTypeAll = pType->Prerequisite_Siblings_Type;
	bool const anySibling = !sibIdxAny.empty() || !sibTypeAny.empty()
		|| !sibIdxAll.empty() || !sibTypeAll.empty();

	if (prereq.empty() && prereqNeg.empty() && reqHouses.empty() && forbHouses.empty()
		&& !anySibling)
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
	if (!prereq.empty() || !prereqNeg.empty() || !reqHouses.empty() || !forbHouses.empty())
	{
		auto const pOwner = this->Parent ? this->Parent->Owner : nullptr;
		if (!pOwner)
			return false;

		// Required buildings: ALL must be present.
		for (auto const pBld : prereq)
			if (pBld && pOwner->CountOwnedAndPresent(pBld) <= 0)
				return false;

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

void AttachmentClass::AI()
{
	TAEXT_DIAG_COUNT("AttachmentClass::AI");
	AttachmentTypeClass* pType = this->GetType();

	if (!this->Child)
	{
		if (pType->RespawnDelay == 0)
		{
			this->CreateChild();
		}
		else if (pType->RespawnDelay > 0)
		{
			if (!this->RespawnTimer.HasStarted())
			{
				this->RespawnTimer.Start(pType->RespawnDelay);
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

		this->Child->PrimaryFacing.SetCurrent(childDir);
		// TODO handle secondary facing in case the turret is idle

		FootClass* pParentAsFoot = abstract_cast<FootClass*>(this->Parent);
		FootClass* pChildAsFoot = abstract_cast<FootClass*>(this->Child);
		if (pParentAsFoot && pChildAsFoot)
		{
			pChildAsFoot->TubeIndex = pParentAsFoot->TubeIndex;
		}

		if (pType->InheritStateEffects)
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

		if (pType->InheritOwner)
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

		if (pType->DestructionWeapon_Child.isset())
			TechnoExt::FireWeaponAtSelf(this->Child, pType->DestructionWeapon_Child);

		if (pType->InheritDestruction && this->Child)
			TechnoExt::Kill(this->Child, pSource);
		else if (!this->Child->InLimbo && pType->ParentDestructionMission.isset())
			this->Child->QueueMission(pType->ParentDestructionMission.Get(), false);

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
		if (pType->DestructionWeapon_Parent.isset())
			TechnoExt::FireWeaponAtSelf(this->Parent, pType->DestructionWeapon_Parent);

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

	if (pType->InheritOwner)
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

		if (!this->Child->InLimbo && pType->ParentDetachmentMission.isset())
			this->Child->QueueMission(pType->ParentDetachmentMission.Get(), false);

		// FIXME this won't work probably
		if (pType->InheritOwner)
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
