#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <TechnoTypeClass.h>

#include <New/Type/AttachmentTypeClass.h>
#include <Ext/Rules/Body.h>

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

		struct AttachmentDataEntry
		{
			ValueableIdx<AttachmentTypeClass> Type;
			NullableIdx<TechnoTypeClass> TechnoType;
			Valueable<CoordStruct> FLH;
			Valueable<bool> IsOnTurret;
			Valueable<DirType> RotationAdjust;
			PhobosFixedString<32> ID;

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
			, AttachmentData {}
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

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
};
