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

// F0b relationship descriptor: how to address a techno relative to an
// attachment graph. Child/Sibling take a slot (index or ID); the others
// ignore it. Shared foundation for prerequisites, targeting, XP/power passing.
enum class AttachmentRelation
{
	Self,             // the techno itself
	Parent,           // its immediate attachment parent
	TopLevelParent,   // the root of its parent chain
	Child,            // one child slot (by index/ID)
	Sibling,          // one co-slot on the same parent (by index/ID), excluding self
	AllChildren,      // every child slot
	AllSiblings,      // every co-slot on the same parent, excluding self
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

		// C1 (attachment power): true iff WE deactivated this host because it lost
		// attachment power. Ownership flag so we only ever Reactivate() a unit we
		// turned off (never one EMP or another system shut down). Serialized so the
		// off-state survives save/load deterministically (online-safe).
		bool AttachmentPowerOff;

		ExtData(TechnoClass* OwnerObject) : Extension<TechnoClass>(OwnerObject)
			, ParentAttachment {}
			, ChildAttachments {}
			, DormantAttachments {}
			, AltOccupation {}
			, PendingExitScatter { false }
			, AttachmentPowerOff { false }
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

	// C1 (attachment power): resolve a host's power state from its PowersParent
	// attachment slots and Deactivate/Reactivate it accordingly. Runs each frame
	// from the host's tick; deterministic + EMP-aware. No-op for non-consumers.
	static void UpdateAttachmentPower(TechnoClass* pHost);

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
