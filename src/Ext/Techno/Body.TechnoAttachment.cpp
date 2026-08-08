#include "Body.h"

#include <TechnoClass.h>

#include <Helpers/Cast.h>
#include <Locomotion/AttachmentLocomotionClass.h>
#include <New/Entity/AttachmentClass.h>
#include <New/Type/AttachmentTypeClass.h>

#include <algorithm>
#include <ranges>
#include <cassert>

// Attaches this techno in a first available attachment "slot".
// Returns true if the attachment is successful.
bool TechnoExt::AttachTo(TechnoClass* pThis, TechnoClass* pParent)
{
	auto const pParentExt = TechnoExt::ExtMap.Find(pParent);

	for (auto const& pAttachment : pParentExt->ChildAttachments)
	{
		if (pAttachment->AttachChild(pThis))
			return true;
	}

	return false;
}

bool TechnoExt::DetachFromParent(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return pExt->ParentAttachment->DetachChild();
}

void TechnoExt::InitializeAttachments(TechnoClass* pThis)
{
	if (TechnoExt::DeployTransferSource)
		return;  // we handle that as part of the "conversion"

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	auto const pType = pThis->GetTechnoType();
	auto const pTypeExt = pType ? TechnoTypeExt::ExtMap.Find(pType) : nullptr;

	if (!pExt || !pTypeExt)
		return;

	for (auto& entry : pTypeExt->AttachmentData)
		pExt->ChildAttachments.emplace_back(std::make_unique<AttachmentClass>(&entry, pThis, nullptr))->OnCreated();
}

void TechnoExt::DestroyAttachments(TechnoClass* pThis, TechnoClass* pSource)
{
	// During deploy transfer the source object goes through Remove_This -> KillCargo after
	// attachments were moved. The vector is empty so this is normally a no-op, but guard for safety.
	if (TechnoExt::DeployTransferSource == pThis)
		return;

	auto const& pExt = TechnoExt::ExtMap.Find(pThis);

	for (auto const& pAttachment : pExt->ChildAttachments)
		pAttachment->Destroy(pSource);

	pExt->ChildAttachments.clear();
}

void TechnoExt::HandleDestructionAsChild(TechnoClass* pThis)
{
	// During deploy transfer the source goes through Remove_This which would notify the parent
	// that the child was destroyed. The source is being replaced, not destroyed.
	if (TechnoExt::DeployTransferSource == pThis)
		return;

	auto const& pExt = TechnoExt::ExtMap.Find(pThis);

	if (pExt->ParentAttachment)
		pExt->ParentAttachment->ChildDestroyed();
}

void TechnoExt::UnlimboAttachments(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	for (auto const& pAttachment : pExt->ChildAttachments)
		pAttachment->Unlimbo();
}

void TechnoExt::LimboAttachments(TechnoClass* pThis)
{
	// During deploy transfer the source object is Limbo'd before the transfer hook fires
	// (building->unit direction). Skip limbo-ing children - they will be moved to the new object.
	if (TechnoExt::DeployTransferSource == pThis)
		return;

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	for (auto const& pAttachment : pExt->ChildAttachments)
		pAttachment->Limbo();
}

void TechnoExt::TransferAttachments(TechnoClass* pThis, TechnoClass* pThat)
{
	auto const pThisExt = TechnoExt::ExtMap.Find(pThis);
	auto const pThatExt = TechnoExt::ExtMap.Find(pThat);

	for (auto& pAttachment : pThisExt->ChildAttachments)
	{
		pAttachment->Parent = pThat;
		pThatExt->ChildAttachments.push_back(std::move(pAttachment));
	}

	pThisExt->ChildAttachments.clear();
}

void TechnoExt::HandleAttachmentConversion(TechnoClass* pThis, TechnoTypeClass* pOldType, TechnoTypeClass* pNewType)
{
	auto const pThisExt = TechnoExt::ExtMap.Find(pThis);
	auto const pNewTypeExt = TechnoTypeExt::ExtMap.Find(pNewType);

	const int oldTypeIndex = TechnoTypeClass::Array.FindItemIndex(pOldType);
	const int newTypeIndex = TechnoTypeClass::Array.FindItemIndex(pNewType);

	// Helper to resolve a NullableIdx<TechnoTypeClass> to a TechnoTypeClass pointer
	auto resolveChildType = [](const NullableIdx<TechnoTypeClass>& idx) -> TechnoTypeClass*
	{
		return idx.isset() ? TechnoTypeClass::Array[idx] : nullptr;
	};

	// Step 1: Store current (old type) mount points as dormant (without limboing yet)
	// We preserve old attachments as is so we can then restore them as is
	pThisExt->DormantAttachments[oldTypeIndex] = std::move(pThisExt->ChildAttachments);

	bool areNewAttachments = false;
	// Step 2: Establish new mount points - restore from dormant or create fresh
	if (auto node = pThisExt->DormantAttachments.extract(newTypeIndex))
	{
		pThisExt->ChildAttachments = std::move(node.mapped());
	}
	else
	{
		// Do NOT call OnCreated() here - we do not consider all of "technically" new attachments as new.
		for (auto& entry : pNewTypeExt->AttachmentData)
			pThisExt->ChildAttachments.emplace_back(std::make_unique<AttachmentClass>(&entry, pThis, nullptr));

		areNewAttachments = true;
	}

	// Step 3: Match old mount points to new active ones by ID; transfer children and synchronize timers.
	// Assumes each attachment ID is unique per TechnoType, which is enforced on parsing.
	auto& oldMounts = pThisExt->DormantAttachments[oldTypeIndex];

	// Shallow copy of old mount pointers for consumed-entry tracking; originals remain in DormantAttachments.
	std::vector<AttachmentClass*> oldMountsCopy;
	std::ranges::transform(oldMounts, std::back_inserter(oldMountsCopy), [](const auto& p) { return p.get(); });

	for (auto& pNewMount : pThisExt->ChildAttachments)
	{
		const auto& newID = pNewMount->Data->ID;
		if (!newID)
			continue;

		bool gotConverted = false;
		for (auto it = oldMountsCopy.begin(); it != oldMountsCopy.end(); ++it)
		{
			const auto& oldID = (*it)->Data->ID;
			if (!oldID || _strcmpi(oldID, newID) != 0)
				continue;

			// Transfer child techno if present
			assert(!pNewMount->Child && "ID-matched new attachment mount already has a child before conversion illegally!");
			if (TechnoClass* pChild = (*it)->Child)
			{
				(*it)->DetachChildCore();

				// NOTE (standalone port): Phobos re-types the child here via
				// TechnoExt::ConvertToType when the mount's TechnoType differs.
				// That helper pulls in Ares interop + the full Phobos TypeExtData
				// (UpdateTypeData) which this standalone DLL intentionally omits.
				// The child is transferred as-is; its own type is not converted.
				// TODO: revisit if child-retyping-on-parent-deploy is needed.

				pNewMount->AttachChildCore(pChild);
			}

			// Synchronize respawn timer if both attachment types have respawn enabled.
			// Preserves the completion percentage: newRemaining/newDelay == oldRemaining/oldDelay.
			int oldDelay = (*it)->GetType()->RespawnDelay;
			int newDelay = pNewMount->GetType()->RespawnDelay;
			if (oldDelay > 0 && newDelay > 0 && (*it)->RespawnTimer.HasStarted())
			{
				int oldRemaining = (*it)->RespawnTimer.GetTimeLeft();
				int newRemaining = (oldRemaining * newDelay) / oldDelay;
				pNewMount->RespawnTimer.TimeLeft = newDelay;
				pNewMount->RespawnTimer.StartTime = static_cast<int>(Unsorted::CurrentFrame) - (newDelay - newRemaining);

				(*it)->RespawnTimer.Stop();  // old must giveth teh state to teh new
			}

			oldMountsCopy.erase(it);
			gotConverted = true;
			break;
		}

		// now that's what I call new - Kerbiter
		if (!gotConverted && areNewAttachments)
			pNewMount->OnCreated();
	}

	// Step 4: Limbo all old mount points (matched ones have no child, so Limbo is a no-op for them);
	// unlimbo the new active ones
	for (auto& pOldMount : oldMounts)
		pOldMount->Limbo();

	if (pThis->InLimbo)
		return;  // if parent is in limbo, leave new attachments in limbo as well and skip unlimboing

	for (auto& pNewMount : pThisExt->ChildAttachments)
		pNewMount->Unlimbo();
}

void TechnoExt::HandleAttachmentDeployTransfer(TechnoClass* pFrom, TechnoClass* pTo)
{
	auto const pFromExt = TechnoExt::ExtMap.Find(pFrom);
	auto const pToExt = TechnoExt::ExtMap.Find(pTo);

	// The flag is consumed here - clear it now that the transfer is happening.
	TechnoExt::DeployTransferSource = nullptr;
	assert(pToExt->ChildAttachments.empty() && "pTo should have no mounts before deploy transfer");

	// Move pFrom's active and dormant attachments into pTo so they live on the surviving object.
	pToExt->ChildAttachments = std::move(pFromExt->ChildAttachments);
	pToExt->DormantAttachments = std::move(pFromExt->DormantAttachments);

	// Re-parent all active mounts to point at the new parent techno.
	for (auto& pAttachment : pToExt->ChildAttachments)
		pAttachment->Parent = pTo;

	// Re-parent dormant mounts as well, since they may be restored on a future conversion.
	for (auto& [typeIdx, mounts] : pToExt->DormantAttachments)
	{
		for (auto& pAttachment : mounts)
			pAttachment->Parent = pTo;
	}

	// Now handle conversion from pFrom's type to pTo's type on the new object.
	HandleAttachmentConversion(pTo, pFrom->GetTechnoType(), pTo->GetTechnoType());
}

bool TechnoExt::IsAttached(TechnoClass* pThis)
{
	auto const& pExt = TechnoExt::ExtMap.Find(pThis);
	return pExt && pExt->ParentAttachment;
}

bool TechnoExt::HasAttachmentLoco(FootClass* pThis)
{
	IPersistPtr pPersist = pThis->Locomotor;
	CLSID locoCLSID {};
	return pPersist && SUCCEEDED(pPersist->GetClassID(&locoCLSID))
		&& locoCLSID == __uuidof(AttachmentLocomotionClass);
}

bool TechnoExt::DoesntOccupyCellAsChild(TechnoClass* pThis)
{
	auto const& pExt = TechnoExt::ExtMap.Find(pThis);
	return pExt && pExt->ParentAttachment
		&& !pExt->ParentAttachment->GetType()->OccupiesCell;
}

bool TechnoExt::IsChildOf(TechnoClass* pThis, TechnoClass* pParent, bool deep)
{
	if (!pThis || !pParent)  // sanity check, sometimes crashes because ext is null - Kerbiter
		return false;

	// Iterative walk up the parent chain with a hard depth cap. The original PR
	// recursed here; with the standalone allowing *any* TechnoType (including
	// buildings) as a child, an accidental parent cycle (A child-of B, B child-of
	// A, or a self-attach) turns that recursion into unbounded stack growth ->
	// stack overflow -> the game closes with no exception dump. Bounding the walk
	// makes a cycle a no-match instead of a crash.
	constexpr int MaxDepth = 64;

	TechnoClass* pCurrent = pThis;
	for (int depth = 0; depth < MaxDepth; ++depth)
	{
		auto const pCurExt = TechnoExt::ExtMap.Find(pCurrent);
		if (!pCurExt || !pCurExt->ParentAttachment)
			return false;

		TechnoClass* pNextParent = pCurExt->ParentAttachment->Parent;
		if (pNextParent == pParent)
			return true;

		if (!deep || !pNextParent || pNextParent == pCurrent)
			return false;  // shallow check, chain end, or self-cycle

		pCurrent = pNextParent;
	}

	return false;  // depth cap hit (likely a cycle) -> treat as not a child
}

bool TechnoExt::AreRelatives(TechnoClass* pThis, TechnoClass* pThat)
{
	return TechnoExt::GetTopLevelParent(pThis)
		== TechnoExt::GetTopLevelParent(pThat);
}

// Returns this if no parent.
TechnoClass* TechnoExt::GetTopLevelParent(TechnoClass* pThis)
{
	auto const pThisExt = TechnoExt::ExtMap.Find(pThis);

	return pThis && pThisExt  // sanity check, sometimes crashes because ext is null - Kerbiter
		&& pThisExt->ParentAttachment
		? TechnoExt::GetTopLevelParent(pThisExt->ParentAttachment->Parent)
		: pThis;
}

// ============================================================================
// F0 — slot-occupancy query
// ============================================================================

AttachmentClass* TechnoExt::GetChildSlot(TechnoClass* pParent, size_t index)
{
	auto const pExt = TechnoExt::ExtMap.Find(pParent);
	if (!pExt || index >= pExt->ChildAttachments.size())
		return nullptr;
	return pExt->ChildAttachments[index].get();
}

int TechnoExt::GetChildSlotIndexById(TechnoClass* pParent, const char* id)
{
	auto const pExt = TechnoExt::ExtMap.Find(pParent);
	if (!pExt || !id || !*id)
		return -1;

	for (size_t i = 0; i < pExt->ChildAttachments.size(); ++i)
	{
		auto const& pSlot = pExt->ChildAttachments[i];
		if (pSlot && pSlot->Data && !_strcmpi(pSlot->Data->ID, id))
			return static_cast<int>(i);
	}
	return -1;
}

AttachmentClass* TechnoExt::GetChildSlotById(TechnoClass* pParent, const char* id)
{
	int const idx = TechnoExt::GetChildSlotIndexById(pParent, id);
	return idx < 0 ? nullptr : TechnoExt::GetChildSlot(pParent, static_cast<size_t>(idx));
}

// A child is "active" when it exists, is alive, and is really on the field
// (not limbo'd by a dynamic prerequisite or by the parent being in limbo).
static bool TAExt_ChildActive(AttachmentClass* pSlot)
{
	auto const pChild = pSlot ? pSlot->Child : nullptr;
	return pChild && pChild->IsAlive && !pChild->InLimbo;
}

// C1 -- attachment power. Deliberately SEPARATE from vanilla PoweredUnit/PowersUnit
// (house/building/type-based): a unit uses one system or the other, so nothing
// fights over the shared Deactivated flag. A "consumer" is Deactivated (dark: no
// move/fire/orders -- the vanilla Robot-Tank state) while unpowered and reactivated
// when powered again. We own only what we turned off (AttachmentPowerOff) and never
// revive a unit still under EMP.
//
// Reconciled PER UNIT from that unit's own tick (not the parent's), so a consumer
// that stops being managed -- detached, roles removed -- still gets released. Two
// consumer roles, both applicable requirements must be met (AND):
//   * Host role  (PowersParent): pThis has >=1 PowersParent slot; powered while any
//                 such slot holds an active child (child-powers-parent).
//   * Sibling role (Powered):    pThis is a Powered child of a parent; powered while
//                 an eligible active sibling source powers it.
//
// v1 is presence-based (one live source powers the consumer). The source scan is
// written as the general "does this consumer have a valid live source?" query so the
// planned expansions -- count caps, units-power-units, radius scope -- slot in here.

// Does active sibling source pSrc power the active sibling consumer pCon (which has
// Powered=yes, sits at slot cIndex; both children of the same parent; pSrc != pCon)?
static bool TAExt_SiblingSourcePowers(
	AttachmentClass* pSrc, AttachmentTypeClass* pSrcType,
	AttachmentClass* pCon, AttachmentTypeClass* pConType, size_t cIndex)
{
	auto const pConChild = pCon->Child;
	auto const pSrcChild = pSrc->Child;
	auto const conChildType = pConChild ? pConChild->GetTechnoType() : nullptr;
	auto const srcChildType = pSrcChild ? pSrcChild->GetTechnoType() : nullptr;

	auto const& sTypes = pSrcType->PowersSiblings_Type;
	auto const& sIdx = pSrcType->PowersSiblings_Index;

	bool const byType = conChildType
		&& std::find(sTypes.begin(), sTypes.end(), conChildType) != sTypes.end();
	bool const byIndex =
		std::find(sIdx.begin(), sIdx.end(), static_cast<int>(cIndex)) != sIdx.end();
	bool const specific = byType || byIndex;

	if (!pConType->Powered_Type.empty())
	{
		// Picky consumer: only a source of an accepted child type that targets it
		// specifically -- the vague PowersSiblings=yes does not power picky consumers.
		auto const& acc = pConType->Powered_Type;
		bool const accepted = srcChildType
			&& std::find(acc.begin(), acc.end(), srcChildType) != acc.end();
		return specific && accepted;
	}

	// Non-picky consumer: any targeting mode powers it.
	return pSrcType->PowersSiblings || specific;
}

void TechnoExt::UpdateAttachmentPower(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt)
		return;

	bool isConsumer = false;
	bool powered = true; // AND across whichever roles apply

	// --- Host role: pThis has PowersParent slots -> needs an active such child. ---
	{
		bool hostRole = false;
		bool hostPowered = false;
		for (auto const& pSlot : pExt->ChildAttachments)
		{
			auto const pType = pSlot ? pSlot->GetType() : nullptr;
			if (!pType || !pType->PowersParent)
				continue;
			hostRole = true;
			if (TAExt_ChildActive(pSlot.get()))
			{
				hostPowered = true;
				break;
			}
		}
		if (hostRole)
		{
			isConsumer = true;
			powered = powered && hostPowered;
		}
	}

	// --- Sibling role: pThis is a Powered child -> needs an eligible sibling source. ---
	if (auto const pSelfSlot = pExt->ParentAttachment)
	{
		auto const pSelfType = pSelfSlot->GetType();
		if (pSelfType && pSelfType->Powered)
		{
			isConsumer = true;
			bool sibPowered = false;
			auto const pParent = pSelfSlot->Parent;
			auto const pParExt = pParent ? TechnoExt::ExtMap.Find(pParent) : nullptr;
			if (pParExt)
			{
				auto const& sibs = pParExt->ChildAttachments;
				size_t selfIdx = sibs.size(); // invalid sentinel
				for (size_t k = 0; k < sibs.size(); ++k)
					if (sibs[k].get() == pSelfSlot) { selfIdx = k; break; }

				for (size_t j = 0; j < sibs.size() && !sibPowered; ++j)
				{
					auto const& pSrc = sibs[j];
					if (pSrc.get() == pSelfSlot)
						continue;
					auto const pSrcType = pSrc ? pSrc->GetType() : nullptr;
					if (!pSrcType || !TAExt_ChildActive(pSrc.get()))
						continue;
					if (TAExt_SiblingSourcePowers(pSrc.get(), pSrcType, pSelfSlot, pSelfType, selfIdx))
						sibPowered = true;
				}
			}
			powered = powered && sibPowered;
		}
	}

	// --- Reconcile (single owner; EMP-guarded reactivation). ---
	if (!isConsumer)
	{
		// No longer managed (detached / roles gone): release any power-off we set.
		if (pExt->AttachmentPowerOff)
		{
			pExt->AttachmentPowerOff = false;
			if (pThis->Deactivated && pThis->EMPLockRemaining == 0)
				pThis->Reactivate();
		}
		return;
	}

	if (!powered)
	{
		if (!pThis->Deactivated)
			pThis->Deactivate();
		pExt->AttachmentPowerOff = true;
	}
	else if (pExt->AttachmentPowerOff)
	{
		pExt->AttachmentPowerOff = false;
		// Revive only what we turned off, never a unit still under EMP -- the game's
		// own EMP recovery reactivates that later.
		if (pThis->Deactivated && pThis->EMPLockRemaining == 0)
			pThis->Reactivate();
	}
}

bool TechnoExt::IsSlotFilled(TechnoClass* pParent, size_t index)
{
	auto const pSlot = TechnoExt::GetChildSlot(pParent, index);
	return pSlot && pSlot->Child;
}

bool TechnoExt::IsSlotActive(TechnoClass* pParent, size_t index)
{
	return TAExt_ChildActive(TechnoExt::GetChildSlot(pParent, index));
}

bool TechnoExt::IsSlotActiveById(TechnoClass* pParent, const char* id)
{
	return TAExt_ChildActive(TechnoExt::GetChildSlotById(pParent, id));
}

size_t TechnoExt::CountFilledSlots(TechnoClass* pParent)
{
	auto const pExt = TechnoExt::ExtMap.Find(pParent);
	if (!pExt)
		return 0;

	size_t n = 0;
	for (auto const& pSlot : pExt->ChildAttachments)
		if (pSlot && pSlot->Child)
			++n;
	return n;
}

size_t TechnoExt::CountActiveSlots(TechnoClass* pParent)
{
	auto const pExt = TechnoExt::ExtMap.Find(pParent);
	if (!pExt)
		return 0;

	size_t n = 0;
	for (auto const& pSlot : pExt->ChildAttachments)
		if (TAExt_ChildActive(pSlot.get()))
			++n;
	return n;
}

// ============================================================================
// F0b — relationship resolver
// ============================================================================

static TechnoClass* TAExt_ParentOf(TechnoClass* pThis)
{
	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	return (pExt && pExt->ParentAttachment) ? pExt->ParentAttachment->Parent : nullptr;
}

TechnoClass* TechnoExt::ResolveRelative(TechnoClass* pThis, AttachmentRelation rel,
	int slot, const char* id)
{
	if (!pThis)
		return nullptr;

	switch (rel)
	{
	case AttachmentRelation::Self:
		return pThis;

	case AttachmentRelation::Parent:
		return TAExt_ParentOf(pThis);

	case AttachmentRelation::TopLevelParent:
	{
		auto const pTop = TechnoExt::GetTopLevelParent(pThis);
		return pTop == pThis ? nullptr : pTop; // null when we have no parent
	}

	case AttachmentRelation::Child:
	{
		auto const pSlot = (slot < 0)
			? TechnoExt::GetChildSlotById(pThis, id)
			: TechnoExt::GetChildSlot(pThis, static_cast<size_t>(slot));
		return pSlot ? pSlot->Child : nullptr;
	}

	case AttachmentRelation::Sibling:
	{
		auto const pParent = TAExt_ParentOf(pThis);
		if (!pParent)
			return nullptr;
		auto const pSlot = (slot < 0)
			? TechnoExt::GetChildSlotById(pParent, id)
			: TechnoExt::GetChildSlot(pParent, static_cast<size_t>(slot));
		auto const pSib = pSlot ? pSlot->Child : nullptr;
		return pSib == pThis ? nullptr : pSib; // never resolve to self
	}

	default: // AllChildren / AllSiblings are multi-target
		return nullptr;
	}
}

std::vector<TechnoClass*> TechnoExt::ResolveRelatives(TechnoClass* pThis, AttachmentRelation rel,
	int slot, const char* id)
{
	std::vector<TechnoClass*> out;
	if (!pThis)
		return out;

	auto const pushChildren = [&out](TechnoClass* pOf, TechnoClass* pExclude)
	{
		auto const pExt = TechnoExt::ExtMap.Find(pOf);
		if (!pExt)
			return;
		for (auto const& pSlot : pExt->ChildAttachments)
			if (pSlot && pSlot->Child && pSlot->Child != pExclude)
				out.push_back(pSlot->Child);
	};

	switch (rel)
	{
	case AttachmentRelation::AllChildren:
		pushChildren(pThis, nullptr);
		break;

	case AttachmentRelation::AllSiblings:
		if (auto const pParent = TAExt_ParentOf(pThis))
			pushChildren(pParent, pThis);
		break;

	default: // single-target relations -> 0 or 1 element
		if (auto const pOne = TechnoExt::ResolveRelative(pThis, rel, slot, id))
			out.push_back(pOne);
		break;
	}

	return out;
}
