#pragma once

// Lightweight spin-loop diagnostic. Logs every ~1M calls to a tagged site,
// INDEPENDENT of game frames — so a single-frame infinite loop (which never
// advances the frame) still produces rapidly-escalating log lines that name
// exactly which site is being hammered. Normal play reaches 1M calls only over
// many minutes, so it stays quiet; a spin loop floods it within a second.

#include <Utilities/Debug.h>

#define TAEXT_DIAG_COUNT(tag)                                                   \
	do {                                                                       \
		static unsigned long long _taext_count = 0;                            \
		if ((++_taext_count & 0xFFFFFull) == 0)                                \
			Debug::Log("[TAExt-diag] " tag " x%llu\n", _taext_count);          \
	} while (0)
