// SPDX-License-Identifier: BSD-3-Clause
// Gen12 context teardown drain barrier (Issue #62).
// Prevents GPU fault (FAULT_TLB_DATA0 / BCS RESET_CTRL bit 2) when live GPU contexts
// terminate while post-sync stamp writes (MI_FLUSH_DW / PIPE_CONTROL) are in flight.
#ifndef kern_tgl_context_teardown_hpp
#define kern_tgl_context_teardown_hpp

#include <stdint.h>
#include <stddef.h>

#ifdef KERNEL
#include <IOKit/IOLib.h>
#else
#include <chrono>
#include <thread>
inline void IODelay(unsigned us) {
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}
#endif

namespace TGLContextTeardown {

struct Counters {
	uint32_t contextFrees {};
	uint32_t contextWaits {};
	uint32_t contextDrained {};
	uint32_t taskFrees {};
	uint32_t taskWaits {};
	uint32_t taskDrained {};
};

inline void drainContext(void *context, void *scheduler, Counters &counters) {
	if (!context || !scheduler) return;
	__atomic_fetch_add(&counters.contextFrees, 1, __ATOMIC_RELAXED);
	auto vtable = *reinterpret_cast<uintptr_t **>(scheduler);
	if (!vtable) return;
	// scheduler->isContextIdle(context) is at slot 0x168 / sizeof(uintptr_t) = 45
	auto isIdle = reinterpret_cast<bool (*)(void *, void *)>(vtable[0x168 / sizeof(uintptr_t)]);
	if (!isIdle) return;
	if (!isIdle(scheduler, context)) {
		__atomic_fetch_add(&counters.contextWaits, 1, __ATOMIC_RELAXED);
		for (unsigned i = 0; i < 500; ++i) {
			IODelay(100);
			if (isIdle(scheduler, context)) {
				__atomic_fetch_add(&counters.contextDrained, 1, __ATOMIC_RELAXED);
				break;
			}
		}
	}
}

inline void drainTask(void *task, void *scheduler, Counters &counters) {
	if (!task || !scheduler) return;
	__atomic_fetch_add(&counters.taskFrees, 1, __ATOMIC_RELAXED);
	auto vtable = *reinterpret_cast<uintptr_t **>(scheduler);
	if (!vtable) return;
	// scheduler->isGpuIdle() is at slot 0x160 / sizeof(uintptr_t) = 44
	auto isGpuIdle = reinterpret_cast<bool (*)(void *)>(vtable[0x160 / sizeof(uintptr_t)]);
	if (!isGpuIdle) return;
	if (!isGpuIdle(scheduler)) {
		__atomic_fetch_add(&counters.taskWaits, 1, __ATOMIC_RELAXED);
		for (unsigned i = 0; i < 500; ++i) {
			IODelay(100);
			if (isGpuIdle(scheduler)) {
				__atomic_fetch_add(&counters.taskDrained, 1, __ATOMIC_RELAXED);
				break;
			}
		}
	}
}

} // namespace TGLContextTeardown

#endif // kern_tgl_context_teardown_hpp
