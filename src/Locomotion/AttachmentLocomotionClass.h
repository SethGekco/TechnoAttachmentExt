#pragma once

#include <LocomotionClass.h>
#include <FootClass.h>
#include <VocClass.h>
#include <Utilities/Debug.h>
#include <MapClass.h>

#include <Interfaces.h>

#include <comip.h>
#include <comdef.h>

class AttachmentClass;


// Standalone port. IMPORTANT: the CLSID differs from the upstream Phobos PR
// ({C5D54B98-8C98-4275-8CE4-EF75CB0CBE3E}). We register our OWN private GUID so
// that if PR #352 is ever merged into Phobos, both DLLs register distinct
// locomotor factories and never fight over the same class object. The CLSID is
// assigned programmatically on attach and never appears in user INI, so a
// private GUID is completely transparent to modders.
class __declspec(uuid("5AB6997E-4511-4C43-A400-35F947865303"))
	AttachmentLocomotionClass : public LocomotionClass
	, public IPiggyback
{
public:

	//IUnknown
	virtual HRESULT __stdcall QueryInterface(REFIID iid, LPVOID* ppvObject)
	{
		HRESULT hr = LocomotionClass::QueryInterface(iid, ppvObject);
		if (hr != E_NOINTERFACE)
			return hr;

		if (iid == __uuidof(IPiggyback))
		{
			*ppvObject = static_cast<IPiggyback*>(this);
			this->AddRef();
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	virtual ULONG __stdcall AddRef() { return LocomotionClass::AddRef(); }
	virtual ULONG __stdcall Release() { return LocomotionClass::Release(); }

	//IPersist
	virtual HRESULT __stdcall GetClassID(CLSID* pClassID)
	{
		if (pClassID == nullptr)
			return E_POINTER;

		*pClassID = __uuidof(this);

		return S_OK;
	}

	//IPersistStream
	virtual HRESULT __stdcall Load(IStream* pStm)
	{
		// This loads the whole object
		HRESULT hr = LocomotionClass::Load(pStm);
		if (FAILED(hr))
			return hr;

		if (this)
		{
			this->Piggybacker.Detach();
			// this reconstructs the object in-place, no-init constructor just refreshes
			// the virtual function table pointers because most likely they will
			// point to incorrect place due to different base address or code changes
			new (this) AttachmentLocomotionClass(noinit_t());
		}

		bool piggybackerPresent;
		hr = pStm->Read(&piggybackerPresent, sizeof(piggybackerPresent), nullptr);
		if (!piggybackerPresent)
			return hr;

		hr = OleLoadFromStream(pStm, __uuidof(ILocomotion), reinterpret_cast<LPVOID*>(&this->Piggybacker));
		return hr;
	}

	virtual HRESULT __stdcall Save(IStream* pStm, BOOL fClearDirty)
	{
		// This saves the whole object
		HRESULT hr = LocomotionClass::Save(pStm, fClearDirty);
		if (FAILED(hr))
			return hr;

		// Piggybacker handling
		bool piggybackerPresent = this->Piggybacker != nullptr;
		hr = pStm->Write(&piggybackerPresent, sizeof(piggybackerPresent), nullptr);

		if (!piggybackerPresent)
			return hr;

		IPersistStreamPtr piggyPersist(this->Piggybacker);
		hr = OleSaveToStream(piggyPersist, pStm);
		return hr;
	}

	virtual bool __stdcall Is_Moving() override;
	virtual Matrix3D __stdcall Draw_Matrix(VoxelIndexKey* key) override;
	virtual Point2D __stdcall Draw_Point() override;
	virtual VisualType __stdcall Visual_Character(bool raw) override;
	virtual int __stdcall Z_Adjust() override;
	virtual ZGradient __stdcall Z_Gradient() override;
	virtual bool __stdcall Process() override;
	virtual bool __stdcall Is_Powered() override;
	virtual bool __stdcall Is_Ion_Sensitive() override;
	virtual Layer __stdcall In_Which_Layer() override;
	virtual bool __stdcall Is_Moving_Now() override;
	virtual int __stdcall Apparent_Speed() override;
	virtual FireError __stdcall Can_Fire() override;
	virtual int __stdcall Get_Status() override;
	virtual bool __stdcall Is_Surfacing() override;
	virtual bool __stdcall Is_Really_Moving_Now() override;
	virtual void __stdcall Limbo() override;

	//IPiggy
	virtual HRESULT __stdcall Begin_Piggyback(ILocomotion* pointer) override;
	virtual HRESULT __stdcall End_Piggyback(ILocomotion** pointer) override;
	virtual bool __stdcall Is_Ok_To_End() override;
	virtual HRESULT __stdcall Piggyback_CLSID(GUID* classid) override;
	virtual bool __stdcall Is_Piggybacking() override;

private:
	// Shortcut to attachment the LinkedTo is attached to.
	AttachmentClass* GetAttachment();

	// Shortcut to parent techno of this locomotor's owner.
	TechnoClass* GetAttachmentParent();

	// Shortcut to parent techno of this locomotor's owner.
	ILocomotionPtr GetAttachmentParentLoco();

	// Non-parent layer calculation.
	Layer CalculateLayer();

	// Should the LinkedTo be on bridge (when it's currently not)?
	// (yoinked from JumpjetLocomotionClass::In_Which_Layer)
	bool ShouldBeOnBridge();

public:
	inline AttachmentLocomotionClass() : LocomotionClass { }
		, PreviousLayer { Layer::None }
		, PreviousCell { CellStruct::Empty }
		, Piggybacker { nullptr }
	{ }

	inline AttachmentLocomotionClass(noinit_t) : LocomotionClass { noinit_t() } { }

	inline virtual ~AttachmentLocomotionClass() override = default;
	virtual int Size() override { return sizeof(*this); }

public:
		// The layer this locomotor's user was in previously.
		// Used for resubmitting the FootClass to another layer.
		Layer PreviousLayer;

		// The cell this locomotor's user was in previously.
		// Used for tracking the FootClass while it's in air.
		CellStruct PreviousCell;

		// The piggybacking locomotor.
		ILocomotionPtr Piggybacker;
};
