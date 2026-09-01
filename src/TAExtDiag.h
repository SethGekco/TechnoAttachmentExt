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

class TechnoClass;

// ---------------------------------------------------------------------------
// Targeted "who is taking the NoMove overrides?" diagnostic.
//
// Every attachment movement/targeting override is gated on HasAttachmentLoco(),
// which only inspects the unit's CURRENT locomotor CLSID. It says nothing about
// whether the unit actually has a parent to ride. A unit that carries the
// attachment locomotor but has NO ParentAttachment is an orphan: it will refuse
// to move, refuse to path, and take the range-suppressed auto-target path
// forever, while still firing perfectly well when ordered to. That is exactly
// the "units gradually stop noticing enemies but still fire on command" report.
//
// Unlike TAEXT_DIAG_COUNT (which only speaks at 1,048,576 hits and so stays
// silent for anything short of a spin loop), this reports the FIRST time each
// (site, type) pair takes an override — so a path firing merely thousands of
// times is still visible.
//
// Output, one line per site per TechnoType:
//   [TAExt-diag] NoMove override: site=CanAutoTargetObject type=GRIZZLY
//                parent=NO children=1   <<< ORPHAN ...
//
// `parent=NO` is the interesting case. `parent=yes` is a genuine child and the
// override is correct.
// ---------------------------------------------------------------------------
void TAExtDiag_ReportNoMove(const char* tag, TechnoClass* pThis);
