#include <YRCom.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <Locomotion/AttachmentLocomotionClass.h>

// Standalone COM registration. Mirrors Phobos.COM.h but uses the value-form
// Game::COMClasses.AddItem (our pinned YRpp defines COMClasses as a reference
// via DEFINE_REFERENCE; the base-Phobos header still uses the old pointer
// form and would not compile here).
template <typename T>
static void RegisterFactoryForClass(IClassFactory* pFactory)
{
	DWORD dwRegister = 0;
	HRESULT hr = CoRegisterClassObject(__uuidof(T), pFactory, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &dwRegister);

	if (FAILED(hr))
		Debug::Log("[TechnoAttachmentExt] CoRegisterClassObject for %s failed, error %d.\n", typeid(T).name(), GetLastError());
	else
		Debug::Log("[TechnoAttachmentExt] Class factory for %s registered.\n", typeid(T).name());

	Game::COMClasses.AddItem((ULONG)dwRegister);
}

template <typename T>
static void RegisterFactoryForClass()
{
	RegisterFactoryForClass<T>(GameCreate<TClassFactory<T>>());
}

// WinMain COM registration point (verified in Phobos). Runs once at startup,
// alongside the game's own locomotor factory registrations, so our private
// AttachmentLocomotionClass CLSID becomes CoCreateInstance-able.
DEFINE_HOOK(0x6BD68D, WinMain_RegisterAttachmentLoco, 0x6)
{
	Debug::Log("[TechnoAttachmentExt] Registering attachment locomotor COM factory...\n");
	RegisterFactoryForClass<AttachmentLocomotionClass>();
	return 0;
}
