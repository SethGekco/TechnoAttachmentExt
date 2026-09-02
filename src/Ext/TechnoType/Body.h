#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <TechnoTypeClass.h>

#include <New/Type/AttachmentTypeClass.h>
#include <Ext/Rules/Body.h>

class Matrix3D;
class BuildingTypeClass;
class HouseTypeClass;
class WeaponTypeClass;

// Standalone port of Phobos's TechnoTypeExt, stripped to ONLY the attachment
// fields. Uses Container<T> in unordered_map mode (Canary defined, no
// ExtPointerOffset) so we claim no pointer slot inside TechnoTypeClass and
// never collide with Phobos/Ares extension storage.
class TechnoTypeExt
{
public:
	using base_type = TechnoTypeClass;

	// Unique canary (distinct from Phobos's 0x11111111 and AITriggerTypeExt's).
	static constexpr DWORD Canary = 0x0A77AC77;
	// No ExtPointerOffset -> Container uses the unordered_map path.

	class ExtData final : public Extension<TechnoTypeClass>
	{
	public:
		Valueable<int> AttachmentTopLayerMinHeight;
		Valueable<int> AttachmentUndergroundLayerMaxHeight;

		// Needed by GetFLHAbsoluteCoords for turret-relative attachment FLH.
		// Read from the art INI [Image]TurretOffset= (same as Phobos).
		Valueable<PartialVector3D<int>> TurretOffset;

		// Experience income multiplier for ANY techno (attachments included, since an
		// attachment child is just a TechnoType). Scales veterancy the moment it is
		// gained, BEFORE any attachment experience-sharing distributes it -- so
		// "earns half XP" means everything downstream works from the halved figure.
		// 1.0 = vanilla, 0 = earns nothing.
		Valueable<double> Experience_Multiplier;

		// ---- Power network (regular TechnoTypes, not just attachments) ----
		// A source powers consumers within Range cells. Relays that are themselves
		// powered re-broadcast, so a chain of relays carries power across the map
		// ("power lines"). Capacity: a source sustains at most Count consumers;
		// past that, OverflowMode decides whether the whole group drops (all) or
		// only the ones past the cap (excess).
		Valueable<bool> PowerSource;
		Valueable<int> PowerSource_Range;              // in cells
		ValueableVector<TechnoTypeClass*> PowerSource_Types; // empty = powers any consumer
		Valueable<int> PowerSource_Count;              // 0 = unlimited
		Valueable<bool> PowerSource_OverflowAll;       // OverflowMode: all(yes) / excess(no)
		// Relay: extends the network. Range defaults to the source range when unset.
		Valueable<bool> PowerRelay;
		Valueable<int> PowerRelay_Range;
		// Consumer: dark unless the network reaches it.
		Valueable<bool> PowerConsumer;
		ValueableVector<TechnoTypeClass*> PowerConsumer_Types; // empty = any source satisfies
		// Whose network may power me / whom I am willing to power
		// (TAExtHouseRelation bitmask). Both sides must accept, same as the Types
		// filters. Default owner+ally preserves the original allied-only behaviour.
		Valueable<int> PowerConsumer_House;
		Valueable<int> PowerSource_House;

		// External-structure power for a PLAIN techno (unit or building), not just
		// attachment children. Same tags as the AttachmentType version; an attached
		// child's AttachmentType/slot settings override these.
		ValueableVector<BuildingTypeClass*> PoweredBy;
		Valueable<bool> PoweredBy_RequireAll;
		Valueable<bool> PoweredBy_RequirePower;
		Valueable<int> PoweredBy_Range;
		Valueable<int> PoweredBy_House;

		struct AttachmentDataEntry
		{
			ValueableIdx<AttachmentTypeClass> Type;
			NullableIdx<TechnoTypeClass> TechnoType;
			Valueable<CoordStruct> FLH;
			Valueable<bool> IsOnTurret;
			Valueable<DirType> RotationAdjust;
			PhobosFixedString<32> ID;
			// Per-slot prerequisite overrides. Each takes precedence over the
			// AttachmentType's matching entry when set (non-empty / isset).
			ValueableVector<BuildingTypeClass*> Prerequisite;
			ValueableVector<BuildingTypeClass*> Prerequisite_Negative;
			ValueableVector<HouseTypeClass*> RequiredHouses;
			ValueableVector<HouseTypeClass*> ForbiddenHouses;
			Nullable<bool> Prerequisite_Dynamic;
			// Per-slot draw-order override; takes precedence over the
			// AttachmentType's YSortPosition when set.
			Nullable<AttachmentYSortPosition> YSortPosition;
			// Per-slot overrides for the behaviour flags, so ONE AttachmentType can
			// be reused across slots that need different power/selection settings.
			// Precedence is TechnoType -> AttachmentType -> AttachmentN.* (the slot
			// has the final word), matching the documented resolution order.
			// Covers the flags, the list-valued companions, and the inherit/respawn/
			// destruction family. Vectors: non-empty = override. Nullables: isset =
			// override. Still AttachmentType-only: the ExperienceTo[N] / Convert[N]
			// rule GROUPS (they are whole rule lists, not single values).
			Nullable<bool> PowersParent;
			Nullable<bool> Powered;
			Nullable<bool> PowersSiblings;
			Nullable<bool> PoweredByParent;
			Nullable<int> RequiresPassengers;
			ValueableVector<BuildingTypeClass*> PoweredBy; // non-empty = overrides the type
			Nullable<bool> PoweredBy_RequireAll;
			Nullable<bool> PoweredBy_RequirePower;
			Nullable<int> PoweredBy_Range;
			Valueable<int> PoweredBy_House; // -1 = unset (inherit the AttachmentType)
			Nullable<bool> PassSelection;
			Nullable<bool> TransparentToMouse;
			// List-valued companions
			ValueableVector<TechnoTypeClass*> Powered_Type;
			ValueableVector<TechnoTypeClass*> PowersSiblings_Type;
			ValueableVector<int> PowersSiblings_Index;
			ValueableVector<int> RequiresSlot_Index;
			ValueableVector<TechnoTypeClass*> RequiresSlot_Type;
			// Host rank / health prerequisite windows
			Nullable<Rank> Prerequisite_MinRank;
			Nullable<Rank> Prerequisite_MaxRank;
			Nullable<int> Prerequisite_MinHealth;
			Nullable<int> Prerequisite_MaxHealth;
			// Sibling prerequisites
			ValueableVector<int> Prerequisite_Sibling_Index;
			ValueableVector<TechnoTypeClass*> Prerequisite_Sibling_Type;
			ValueableVector<int> Prerequisite_Siblings_Index;
			ValueableVector<TechnoTypeClass*> Prerequisite_Siblings_Type;
			// Inherit / respawn / destruction family
			Nullable<bool> RespawnAtCreation;
			Nullable<int> RespawnDelay;
			Nullable<bool> InheritCommands_StopCommand;
			Nullable<bool> InheritCommands_DeployCommand;
			Nullable<bool> InheritOwner;
			Nullable<bool> InheritStateEffects;
			Nullable<bool> InheritDestruction;
			Nullable<bool> InheritHeightStatus;
			Nullable<int> Ammo_Parent;
			Nullable<bool> Intangible;
			Nullable<bool> OccupiesCell;
			Nullable<bool> LowSelectionPriority;
			Nullable<bool> Decorative;
			Nullable<WeaponTypeClass*> DestructionWeapon_Child;
			Nullable<WeaponTypeClass*> DestructionWeapon_Parent;
			Nullable<Mission> ParentDestructionMission;
			Nullable<Mission> ParentDetachmentMission;
			Nullable<bool> Convert_KeepHealth;
			Nullable<bool> Convert_KeepVeterancy;

			bool Load(PhobosStreamReader& stm, bool registerForChange);
			bool Save(PhobosStreamWriter& stm) const;

		private:
			template <typename T>
			bool Serialize(T& stm);
		};

		ValueableVector<AttachmentDataEntry> AttachmentData;

		ExtData(TechnoTypeClass* OwnerObject) : Extension<TechnoTypeClass>(OwnerObject)
			, AttachmentTopLayerMinHeight { RulesExt::Global()->AttachmentTopLayerMinHeight }
			, AttachmentUndergroundLayerMaxHeight { RulesExt::Global()->AttachmentUndergroundLayerMaxHeight }
			, TurretOffset { { 0, 0, 0 } }
			, Experience_Multiplier { 1.0 }
			, PowerSource { false }
			, PowerSource_Range { 0 }
			, PowerSource_Types { }
			, PowerSource_Count { 0 }
			, PowerSource_OverflowAll { false }
			, PowerRelay { false }
			, PowerRelay_Range { -1 }
			, PowerConsumer { false }
			, PowerConsumer_Types { }
			, PowerConsumer_House { TAExtHouse_Owner | TAExtHouse_Ally }
			, PowerSource_House { TAExtHouse_Owner | TAExtHouse_Ally }
			, PoweredBy { }
			, PoweredBy_RequireAll { false }
			, PoweredBy_RequirePower { true }
			, PoweredBy_Range { 0 }
			, PoweredBy_House { TAExtHouse_Owner }
			, AttachmentData {}
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		void ApplyTurretOffset(Matrix3D* mtx, double factor = 1.0);

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<TechnoTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;

	static void ApplyTurretOffset(TechnoTypeClass* pType, Matrix3D* mtx, double factor = 1.0);
};
