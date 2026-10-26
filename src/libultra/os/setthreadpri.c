#include "ultra64.h"
#if BENCH
#include "bench.h"
#endif

void osSetThreadPri(OSThread* thread, OSPri p) {
    register u32 saveMask = __osDisableInt();

    if (thread == NULL) {
        thread = __osRunningThread;
    }

    if (thread->priority != p) {
        thread->priority = p;

        if (thread != __osRunningThread && thread->state != OS_STATE_STOPPED) {
            __osDequeueThread(thread->queue, thread);
            __osEnqueueThread(thread->queue, thread);
        }

        if (__osRunningThread->priority < __osRunQueue->priority) {
            __osRunningThread->state = OS_STATE_RUNNABLE;
#if BENCH
            Bench_EnqueueAndYield(&__osRunQueue);
#else
            __osEnqueueAndYield(&__osRunQueue);
#endif
        }
    }

    __osRestoreInt(saveMask);
}
