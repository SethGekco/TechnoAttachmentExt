#pragma once

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <TechnoClass.h>
#include <FootClass.h>

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <New/Entity/AttachmentClass.h>
#include <Ext/TechnoType/Body.h>

class WeaponTypeClass;

// NOTE: AttachmentRelation now lives in New/Type/AttachmentTypeClass.h so the
// INI parser shim (AttachmentParsers.h) can see its enumerators without pulling
// in this header (which would be circular).

// Reasons WE may hold a techno deactivated ("dark": no move/fire/orders -- the
// vanilla Robot-Tank state). A bitmask, so independent gates compose: ANY reason
// set keeps the techno dark, and it only wakes when ALL of ours clear. One arbiter
// owns the shared Deactivated flag, which is what keeps these features from
// fighting each other (and from fighting EMP / vanilla PoweredUnit). Add new gates
// as new bits -- no new system, no new flag owner.
enum TAExtDeactivateReason : int
{
	TAExtDeactivate_None            = 0,
	TAExtDeactivate_AttachmentPower = 1 << 0, // lost attachment/sibling/parent power
	TAExtDeactivate_SlotRequirement = 1 << 1, // required parent slot / passengers missing
	TAExtDeactivate_NetworkPower    = 1 << 2, // PowerConsumer not reached by the network
	TAExtDeactivate_BuildingPower   = 1 << 3, // PoweredBy building missing/offline
};

// Standalone port of Phobos's TechnoExt, stripped to ONLY the attachment
// state and helpers. Uses Container<T> in unordered_map mode (Canary defined,
// no ExtPointerOffset) so we claim no pointer slot in TechnoClass.
class TechnoExt
{
public:
	using base_type = TechnoClass;

	static constexpr DWORD Canary = 0x0A77EC77;
	// No ExtPointerOffset -> Container uses the unordered_map path.

	class ExtData final : public Extension<TechnoClass>
	{
	public:
		AttachmentClass* ParentAttachment;
		ValueableVector<std::unique_ptr<AttachmentClass>> ChildAttachments;
		std::map<int, ValueableVector<std::unique_ptr<AttachmentClass>>> DormantAttachments;

		// Ares: if the unit marks cell occupation flags, this is set to whether
		// it uses the "high" occupation members.
		std::optional<bool> AltOccupation;

		// Transient (not serialized): set on a unit freshly produced from an
		// attached factory building so its next FootClass update scatters it off
		// the exit cell, freeing that cell for the next produced unit.
		bool PendingExitScatter;

		// Deactivation arbiter (see TAExtDeactivateReason). Bitmask of OUR reasons
		// for holding this techno dark. Non-zero == we deactivated it; zero == we
		// have no claim. Single owner for the shared Deactivated flag, so every gate
		// (power, slot/passenger requirements, future radius power) composes instead
		// of fighting. Serialized so the state survives save/load deterministically.
		int DeactivationReasons;

		// Power-network result for this techno, republished every frame by the
		// solver (Hooks.PowerNetwork.cpp). Derived state -- deliberately NOT
		// serialized; it is recomputed on the first frame after a load.
		bool NetworkPowered;

		// A2 experience passing: this techno's veterancy as of the last tick. A
		// positive delta is newly-earned XP to route to relatives. Serialized so a
		// save/load doesn't look like a huge one-frame gain.
		float LastVeterancy;
		bool LastVeterancyValid;

		ExtData(TechnoClass* OwnerObject) : Extension<TechnoClass>(OwnerObject)
			, ParentAttachment {}
			, ChildAttachments {}
			, DormantAttachments {}
			, AltOccupation {}
			, PendingExitScatter { false }
			, DeactivationReasons { 0 }
			, NetworkPowered { false }
			, LastVeterancy { 0.0f }
			, LastVeterancyValid { false }
		{ }

		virtual ~ExtData() override;

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override;
		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<TechnoExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;

	// Set before deploy-target construction to skip InitializeAttachments.
	static TechnoClass* DeployTransferSource;

	// ---- Ported Phobos helpers the attachment runtime needs ----
	static CoordStruct GetFLHAbsoluteCoords(TechnoClass* pThis, CoordStruct flh, bool turretFLH = false);
	static void FireWeaponAtSelf(TechnoClass* pThis, WeaponTypeClass* pWeaponType);

	// ---- Attachment API (defined in Body.TechnoAttachment.cpp / hooks) ----
	static bool AttachTo(TechnoClass* pThis, TechnoClass* pParent);
	static bool DetachFromParent(TechnoClass* pThis);

	// Deactivation arbiter: recompute every gate for this techno (attachment power,
	// slot/passenger requirements) and Deactivate/Reactivate accordingly. Runs each
	// frame from the techno's own tick; deterministic + EMP-aware. No-op unless some
	// gate applies. Call site never changes as gates are added.
	static void UpdateAttachmentGates(TechnoClass* pThis);

	// A2 experience passing: detect veterancy this techno gained since the last
	// tick and route a share to the relatives its AttachmentType lists. Polled
	// from the synced tick, so no new game hook and no ordering hazard.
	static void UpdateExperienceSharing(TechnoClass* pThis);

	static void InitializeAttachments(TechnoClass* pThis);
	static void DestroyAttachments(TechnoClass* pThis, TechnoClass* pSource);
	static void HandleDestructionAsChild(TechnoClass* pThis);
	static void UnlimboAttachments(TechnoClass* pThis);
	static void LimboAttachments(TechnoClass* pThis);
	static void TransferAttachments(TechnoClass* pThis, TechnoClass* pThat);
	static void HandleAttachmentConversion(TechnoClass* pThis, TechnoTypeClass* pOldType, TechnoTypeClass* pNewType);
	static void HandleAttachmentDeployTransfer(TechnoClass* pFrom, TechnoClass* pTo);

	static bool IsAttached(TechnoClass* pThis);
	static bool HasAttachmentLoco(FootClass* pThis);
	static bool DoesntOccupyCellAsChild(TechnoClass* pThis);
	static bool IsChildOf(TechnoClass* pThis, TechnoClass* pParent, bool deep = true);
	static bool AreRelatives(TechnoClass* pThis, TechnoClass* pThat);
	static TechnoClass* GetTopLevelParent(TechnoClass* pThis);

	// ====================================================================
	// F0 — slot-occupancy query. A "slot" is an attachment position on a
	// parent (ChildAttachments[index], corresponding to Attachment<index>.*,
	// also addressable by AttachmentX.ID). "Filled" = a child instance exists;
	// "Active" = that child exists, is alive, and is not in limbo (i.e. it is
	// really present on the field, not hidden by a dynamic prerequisite or the
	// parent being in limbo). Pure reads of synced state — online-safe.
	// ====================================================================
	static AttachmentClass* GetChildSlot(TechnoClass* pParent, size_t index);
	static AttachmentClass* GetChildSlotById(TechnoClass* pParent, const char* id);
	static int GetChildSlotIndexById(TechnoClass* pParent, const char* id);
	static bool IsSlotFilled(TechnoClass* pParent, size_t index);
	static bool IsSlotActive(TechnoClass* pParent, size_t index);
	static bool IsSlotActiveById(TechnoClass* pParent, const char* id);
	static size_t CountFilledSlots(TechnoClass* pParent);
	static size_t CountActiveSlots(TechnoClass* pParent);

	// ====================================================================
	// F0b — relationship resolver. Address technos relative to the attachment
	// graph (see AttachmentRelation). Single-target relations resolve to one
	// techno (or null); AllChildren/AllSiblings resolve to a set. `slot` selects
	// the Child/Sibling position (negative index = treat `id` as a slot ID).
	// ====================================================================
	static TechnoClass* ResolveRelative(TechnoClass* pThis, AttachmentRelation rel,
		int slot = 0, const char* id = nullptr);
	static std::vector<TechnoClass*> ResolveRelatives(TechnoClass* pThis, AttachmentRelation rel,
		int slot = 0, const char* id = nullptr);

	// Kill helpers used by destruction weapons/missions.
	static void Kill(TechnoClass* pThis, ObjectClass* pAttacker, HouseClass* pAttackingHouse);
	static void Kill(TechnoClass* pThis, TechnoClass* pAttacker);
};
