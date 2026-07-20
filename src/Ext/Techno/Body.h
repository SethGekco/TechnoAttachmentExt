#pragma once

#include <map>
#include <memory>
#include <optional>

#include <TechnoClass.h>
#include <FootClass.h>

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <New/Entity/AttachmentClass.h>
#include <Ext/TechnoType/Body.h>

class WeaponTypeClass;

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

		ExtData(TechnoClass* OwnerObject) : Extension<TechnoClass>(OwnerObject)
			, ParentAttachment {}
			, ChildAttachments {}
			, DormantAttachments {}
			, AltOccupation {}
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

	// Kill helpers used by destruction weapons/missions.
	static void Kill(TechnoClass* pThis, ObjectClass* pAttacker, HouseClass* pAttackingHouse);
	static void Kill(TechnoClass* pThis, TechnoClass* pAttacker);
};
