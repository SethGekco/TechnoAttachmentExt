// Freeze watchdog (diagnostic).
//
// The building-host + active-vehicle-child case hangs the game in a one-frame
// spin. A per-frame hook can't observe it (the frame never completes), so we
// use a background thread: the game logic thread bumps a heartbeat each tick
// (see TAExt_FreezeHeartbeat, called from the Foot/Building attachment tick
// hooks). If the heartbeat stalls, the logic thread is stuck; the watchdog
// suspends it, captures EIP and the call stack (raw code addresses, to be
// symbolized offline against gamemd.exe + the .map), logs them, and exits the
// process so the log can be retrieved.
//
// Compile out by defining TAEXT_ENABLE_FREEZE_WATCHDOG=0.

#include <Utilities/Debug.h>

#ifndef TAEXT_ENABLE_FREEZE_WATCHDOG
#define TAEXT_ENABLE_FREEZE_WATCHDOG 1
#endif

#if TAEXT_ENABLE_FREEZE_WATCHDOG

#include <windows.h>

namespace
{
	// gamemd.exe .text: VMA 0x401000, size 0x3DF38D -> [0x401000, 0x7E038D)
	constexpr DWORD kTextLo = 0x401000;
	constexpr DWORD kTextHi = 0x7E038D;

	volatile DWORD g_lastHeartbeat = 0;
	volatile bool  g_started = false;
	volatile bool  g_dumped = false;
	HANDLE g_mainThread = nullptr;

	bool IsCodeAddr(DWORD a) { return a >= kTextLo && a < kTextHi; }

	DWORD SafeReadDword(DWORD addr, bool& ok)
	{
		DWORD v = 0;
		ok = false;
		__try { v = *reinterpret_cast<DWORD*>(addr); ok = true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return v;
	}

	void DumpAndExit(DWORD stalledMs)
	{
		if (!g_mainThread)
			return;

		SuspendThread(g_mainThread);

		CONTEXT ctx;
		ctx.ContextFlags = CONTEXT_CONTROL;
		if (GetThreadContext(g_mainThread, &ctx))
		{
			Debug::Log("[TAExt-freeze] ===== FREEZE DETECTED (no heartbeat for %u ms) =====\n", stalledMs);
			Debug::Log("[TAExt-freeze] EIP=0x%08X ESP=0x%08X EBP=0x%08X\n", ctx.Eip, ctx.Esp, ctx.Ebp);

			// EBP frame-pointer walk (works when frames use a normal prologue).
			DWORD ebp = ctx.Ebp;
			for (int i = 0; i < 32 && ebp; ++i)
			{
				bool ok;
				DWORD ret = SafeReadDword(ebp + 4, ok);
				if (!ok) break;
				if (IsCodeAddr(ret))
					Debug::Log("[TAExt-freeze]  frame[%d] ret=0x%08X\n", i, ret);
				DWORD next = SafeReadDword(ebp, ok);
				if (!ok || next <= ebp) break;
				ebp = next;
			}

			// Raw stack scan for anything that looks like a return address, in
			// case of frame-pointer omission in the spinning function.
			Debug::Log("[TAExt-freeze] --- raw stack scan (code-range values) ---\n");
			int found = 0;
			for (int i = 0; i < 512 && found < 64; ++i)
			{
				bool ok;
				DWORD val = SafeReadDword(ctx.Esp + i * 4, ok);
				if (!ok) break;
				if (IsCodeAddr(val))
				{
					Debug::Log("[TAExt-freeze]  stack[+0x%X]=0x%08X\n", i * 4, val);
					++found;
				}
			}
			Debug::Log("[TAExt-freeze] ===== end of dump; terminating so the log can be read =====\n");
		}

		ExitProcess(0);
	}

	DWORD WINAPI WatchdogProc(LPVOID)
	{
		for (;;)
		{
			Sleep(500);
			if (!g_started || g_dumped)
				continue;

			DWORD now = GetTickCount();
			DWORD last = g_lastHeartbeat;
			DWORD stalled = now - last;

			// 6 s with no logic tick = spin. Normal gameplay never stalls this long.
			if (stalled >= 6000)
			{
				g_dumped = true;
				DumpAndExit(stalled);
			}
		}
		return 0;
	}
}

// Called from the game logic thread every tick (Foot/Building attachment hooks).
void TAExt_FreezeHeartbeat()
{
	g_lastHeartbeat = GetTickCount();

	if (!g_started)
	{
		// The caller is the game logic thread; capture a real handle to it so
		// the watchdog thread can suspend/inspect it.
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
		CreateThread(nullptr, 0, WatchdogProc, nullptr, 0, nullptr);
		g_started = true;
		Debug::Log("[TAExt-freeze] watchdog armed\n");
	}
}

#else

void TAExt_FreezeHeartbeat() {}

#endif // TAEXT_ENABLE_FREEZE_WATCHDOG
