#include "../WhateverXe/kern_tgl_context_teardown.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

alignas(16) uint8_t mockScheduler[0x200];
alignas(16) uintptr_t mockVtable[64];
alignas(16) uint8_t mockContext[0x100];
alignas(16) uint8_t mockTask[0x100];

int contextPollCount = 0;
int taskPollCount = 0;
int contextIdleThreshold = 0;
int taskIdleThreshold = 0;

bool testIsContextIdle(void *sched, void *ctx) {
	assert(sched == mockScheduler);
	assert(ctx == mockContext);
	contextPollCount++;
	return contextPollCount >= contextIdleThreshold;
}

bool testIsGpuIdle(void *sched) {
	assert(sched == mockScheduler);
	taskPollCount++;
	return taskPollCount >= taskIdleThreshold;
}

} // namespace

int main() {
	// Setup mock scheduler vtable
	memset(mockScheduler, 0, sizeof(mockScheduler));
	memset(mockVtable, 0, sizeof(mockVtable));
	*reinterpret_cast<uintptr_t **>(mockScheduler) = mockVtable;

	// Slot 0x160 / 8 = 44: isGpuIdle
	mockVtable[0x160 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&testIsGpuIdle);
	// Slot 0x168 / 8 = 45: isContextIdle
	mockVtable[0x168 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&testIsContextIdle);

	TGLContextTeardown::Counters counters {};

	// Case 1: Null safety
	TGLContextTeardown::drainContext(nullptr, nullptr, counters);
	TGLContextTeardown::drainTask(nullptr, nullptr, counters);
	assert(counters.contextFrees == 0);
	assert(counters.taskFrees == 0);

	// Case 2: Context is already idle
	contextPollCount = 0;
	contextIdleThreshold = 1; // 1st poll returns true
	TGLContextTeardown::drainContext(mockContext, mockScheduler, counters);
	assert(counters.contextFrees == 1);
	assert(counters.contextWaits == 0);
	assert(counters.contextDrained == 0);
	assert(contextPollCount == 1);

	// Case 3: Context is busy, drains after 3 iterations
	contextPollCount = 0;
	contextIdleThreshold = 4; // returns false 3 times, then true on 4th
	TGLContextTeardown::drainContext(mockContext, mockScheduler, counters);
	assert(counters.contextFrees == 2);
	assert(counters.contextWaits == 1);
	assert(counters.contextDrained == 1);
	assert(contextPollCount == 4);

	// Case 4: GPU is already idle
	taskPollCount = 0;
	taskIdleThreshold = 1; // 1st poll returns true
	TGLContextTeardown::drainTask(mockTask, mockScheduler, counters);
	assert(counters.taskFrees == 1);
	assert(counters.taskWaits == 0);
	assert(counters.taskDrained == 0);
	assert(taskPollCount == 1);

	// Case 5: GPU is busy, drains after 5 iterations
	taskPollCount = 0;
	taskIdleThreshold = 6;
	TGLContextTeardown::drainTask(mockTask, mockScheduler, counters);
	assert(counters.taskFrees == 2);
	assert(counters.taskWaits == 1);
	assert(counters.taskDrained == 1);
	assert(taskPollCount == 6);

	puts("TGLContextTeardown unit tests passed successfully!");
	return 0;
}
