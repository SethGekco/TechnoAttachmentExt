#pragma once

// Lightweight per-frame call counter for diagnosing within-frame spin loops.
// Logs once (at most) per game frame, and only when a tagged site is hit an
// abnormal number of times in a single frame — so normal play stays quiet but
// a tight loop that hammers a hook shows up as a huge count on the last logged
// frame before a freeze.

#include <Unsorted.h>
#include <Utilities/Debug.h>

#define TAEXT_DIAG_COUNT(tag)                                                   \
	do {                                                                       \
		static unsigned _taext_lastFrame = ~0u;                                \
		static unsigned _taext_count = 0;                                      \
		unsigned const _taext_f = (unsigned)Unsorted::CurrentFrame;            \
		if (_taext_f != _taext_lastFrame) {                                    \
			if (_taext_count > 50)                                             \
				Debug::Log("[TAExt-diag] frame %u: " tag " x%u\n",             \
					_taext_lastFrame, _taext_count);                           \
			_taext_lastFrame = _taext_f;                                       \
			_taext_count = 0;                                                  \
		}                                                                      \
		++_taext_count;                                                        \
	} while (0)
