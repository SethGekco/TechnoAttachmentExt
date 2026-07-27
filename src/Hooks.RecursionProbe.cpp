// Diagnostic probe for the building-host + active-vehicle-child freeze.
//
// The freeze watchdog traced the spin to a BuildingClass foundation/cell
// visitor traversal at 0x4373B0 that recurses without terminating. This probe
// counts entries to that function within a single game frame (during a freeze
// the frame never advances, so the count explodes) and, once it crosses a
// threshold, logs the arguments + caller of the runaway entry so the recursing
// object can be identified. Pure diagnostic; returns 0 (continue) always.
//
// Compile out with TAEXT_ENABLE_RECURSION_PROBE=0.

#include <Unsorted.h>

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#ifndef TAEXT_ENABLE_RECURSION_PROBE
#define TAEXT_ENABLE_RECURSION_PROBE 0
#endif

#if TAEXT_ENABLE_RECURSION_PROBE

DEFINE_HOOK(0x4373B0, TechnoAttachmentExt_TraversalRecursionProbe, 0x3)
{
	static int lastFrame = -1;
	static int count = 0;
	static bool logged = false;

	const int frame = Unsorted::CurrentFrame;
	if (frame != lastFrame)
	{
		lastFrame = frame;
		count = 0;
		logged = false;
	}

	if (++count == 3000 && !logged)
	{
		logged = true;

		GET(DWORD, ecx, ECX);
		GET(DWORD, edx, EDX);
		GET_STACK(DWORD, caller, 0x0); // return address at function entry

		// vtable pointers identify the classes involved (map offline).
		DWORD ecxVt = (ecx >= 0x10000) ? *reinterpret_cast<DWORD*>(ecx) : 0;
		DWORD edxVt = (edx >= 0x10000) ? *reinterpret_cast<DWORD*>(edx) : 0;

		Debug::Log("[TAExt-recur] 0x4373B0 x3000 in frame %d: "
			"ECX=0x%08X (vt=0x%08X) EDX=0x%08X (vt=0x%08X) caller=0x%08X\n",
			frame, ecx, ecxVt, edx, edxVt, caller);
	}

	return 0;
}

#endif // TAEXT_ENABLE_RECURSION_PROBE
