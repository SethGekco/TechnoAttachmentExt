#include "TechnoAttachmentExt.h"

#include <Phobos.h>
#include <Syringe.h>
#include <Utilities/Patch.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

HANDLE TechnoAttachmentExtDLL::hInstance = nullptr;

char TechnoAttachmentExtDLL::readBuffer[TechnoAttachmentExtDLL::readLength];
wchar_t TechnoAttachmentExtDLL::wideBuffer[TechnoAttachmentExtDLL::readLength];

// Defined in Hooks.AttachedSelection.cpp. Must run AFTER ApplyStatic so that
// any static patches are in place, and it deliberately reads the Select vtable
// slots before overwriting them so it chains to whatever is already there
// (e.g. SquadExt's selection wrapper) instead of erasing it.
extern void TAExt_InstallSelectWrappers();

void TechnoAttachmentExtDLL::ExeRun()
{
    Patch::ApplyStatic();
    TAExt_InstallSelectWrappers();
}

bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        TechnoAttachmentExtDLL::hInstance = hInstance;
        Phobos::hInstance = hInstance; // needed by Patch::ApplyStatic
    }
    return true;
}

SYRINGE_HANDSHAKE(pInfo)
{
    pInfo->Message = const_cast<char*>("TechnoAttachmentExt");
    return S_OK;
}

// Hook into the game's main loop start so our patches apply at the right time
DEFINE_HOOK(0x7CD810, ExeRun, 0x9)
{
    TechnoAttachmentExtDLL::ExeRun();
    return 0;
}

// Trigger deferred debug log flush after command line parse
DEFINE_HOOK(0x52F639, CmdLineParse, 0x5)
{
    Debug::LogDeferredFinalize();
    return 0;
}
