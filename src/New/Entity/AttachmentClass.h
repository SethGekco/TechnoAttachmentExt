#pragma once

#include <algorithm>

#include <GeneralStructures.h>

#include <New/Type/AttachmentTypeClass.h>
#include <Ext/TechnoType/Body.h>

class TechnoClass;

class AttachmentClass
{
public:
	static std::vector<AttachmentClass*> Array;

	TechnoTypeExt::ExtData::AttachmentDataEntry* Data;
	TechnoClass* Parent;
	TechnoClass* Child;
	CDTimerClass RespawnTimer;


	AttachmentClass(TechnoTypeExt::ExtData::AttachmentDataEntry* data,
		TechnoClass* pParent, TechnoClass* pChild = nullptr) :
		Data { data },
		Parent { pParent },
		Child { pChild },
		RespawnTimer { }
	{
		Array.push_back(this);
	}

	AttachmentClass() :
		Data { },
		Parent { },
		Child { },
		RespawnTimer { }
	{
		Array.push_back(this);
	}

	~AttachmentClass();

	AttachmentTypeClass* GetType();
	TechnoTypeClass* GetChildType();
	CoordStruct GetChildLocation();

	// True if the host owner satisfies the effective prerequisite (per-slot
	// override, else the AttachmentType's), or if there is none. Re-evaluated
	// each frame so the child hides/shows as the prerequisite is gained/lost.
	bool PrerequisitesMet();

	// Effective Prerequisite.Dynamic: per-slot override (if set) else the type's.
	bool PrerequisiteDynamic();

	void OnCreated();
	void CreateChild();

	// Convert-in-place: which TechnoType SHOULD this slot's child be right now?
	// Returns null when the feature is unused. Otherwise the first matching
	// ConvertRule's target, or the slot's configured type when none match (that is
	// what makes reverting automatic).
	// Per-slot override resolvers. Precedence: AttachmentN.* (slot) beats the
	// AttachmentType, matching the documented TechnoType -> AttachmentType ->
	// Attachment-tag order. Always go through these rather than reading
	// GetType()->X directly, or per-slot overrides silently do nothing.
	bool ResolvePowersParent();
	bool ResolvePowered();
	bool ResolvePowersSiblings();
	bool ResolvePoweredByParent();
	int  ResolveRequiresPassengers();
	const ValueableVector<BuildingTypeClass*>& ResolvePoweredBy();
	bool ResolvePoweredByRequireAll();
	bool ResolvePoweredByRequirePower();
	int  ResolvePoweredByRange();
	int  ResolvePoweredByHouse();
	// List-valued companions (non-empty slot vector overrides the AttachmentType).
	const ValueableVector<TechnoTypeClass*>& ResolvePoweredType();
	const ValueableVector<TechnoTypeClass*>& ResolvePowersSiblingsType();
	const ValueableVector<int>& ResolvePowersSiblingsIndex();
	const ValueableVector<int>& ResolveRequiresSlotIndex();
	const ValueableVector<TechnoTypeClass*>& ResolveRequiresSlotType();
	// Inherit / respawn / destruction family
	bool ResolveRespawnAtCreation();
	int  ResolveRespawnDelay();
	bool ResolveInheritStopCommand();
	bool ResolveInheritDeployCommand();
	bool ResolveInheritOwner();
	bool ResolveInheritStateEffects();
	bool ResolveInheritDestruction();
	bool ResolveInheritHeightStatus();
	int  ResolveAmmoParent();
	bool ResolveSpins();
	int  ResolveSpinsPeriod();
	bool ResolveSpinsOrbit();
	int  ResolveFacingMode();
	int  ResolvePrerequisiteLostAction();
	int  ResolveMoveRadius();
	int  ResolveMoveMode();
	int  ResolveMoveRange();
	int  ResolveMoveSpeed();
	bool ResolveSlides();
	int  ResolveSlidesAxis();
	int  ResolveSlidesRange();
	int  ResolveSlidesPeriod();
	int  ResolveSlidesPhase();
	bool ResolveBobs();
	int  ResolveBobsAmplitude();
	int  ResolveBobsPeriod();
	int  ResolveBobsPhase();

	// J1 motion, derived from the synced frame counter (integer maths only).
	// Spin offset in raw facing units; 0 when not spinning.
	int  GetSpinRaw();
	int  GetSpinRawAt(unsigned int frame);
	// Vertical bob offset in leptons; 0 when not bobbing.
	int  GetBobZ();
	int  GetBobZAt(unsigned int frame);
	// Slide offset in leptons along the configured axis; 0 when not sliding.
	int  GetSlideOffset();
	int  GetSlideOffsetAt(unsigned int frame);

	// Reactive motion: ease the child's world-space offset toward the point that
	// best satisfies Move.Mode. Call ONCE per tick -- it mutates stored state.
	void UpdateMoveOffset();

	// The child's position WITHOUT the reactive Move.* offset.
	CoordStruct GetChildAnchor();
	// Parent-local offset at an arbitrary frame (orbit+slide+bob are pure
	// functions of the frame), and the facing offset Facing.Mode implies.
	CoordStruct GetLocalOffsetAt(unsigned int frame);
	int  GetFacingModeRaw();
	bool ResolveIntangible();
	bool ResolveOccupiesCell();
	bool ResolveLowSelectionPriority();
	bool ResolveConvertKeepHealth();
	bool ResolveConvertKeepVeterancy();
	bool ResolvePassSelection();
	bool ResolveTransparentToMouse();

	TechnoTypeClass* ResolveDesiredChildType();
	// Swap the existing child for a fresh one of pNewType, keeping this slot and
	// its adoption by the parent. Quiet: no destruction weapons, no death effects.
	void ConvertChildTo(TechnoTypeClass* pNewType);
	void AI();
	void Destroy(TechnoClass* pSource);
	void ChildDestroyed();

	void Unlimbo();
	void Limbo();

	bool AttachChild(TechnoClass* pChild);
	bool DetachChild();

	// Core link/unlink without side effects (locomotor changes, missions, owner resets)
	void AttachChildCore(TechnoClass* pChild);
	void DetachChildCore();

	void InvalidatePointer(void* ptr);

	bool Load(PhobosStreamReader& stm, bool registerForChange);
	bool Save(PhobosStreamWriter& stm) const;

private:
	template <typename T>
	bool Serialize(T& stm);
};
