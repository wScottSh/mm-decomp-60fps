#include "ultra64.h"
#if BENCH
#include "bench.h"
#endif

void osYieldThread(void) {
    register u32 saveMask = __osDisableInt();

    __osRunningThread->state = OS_STATE_RUNNABLE;
#if BENCH
    Bench_EnqueueAndYield(&__osRunQueue);
#else
    __osEnqueueAndYield(&__osRunQueue);
#endif

    __osRestoreInt(saveMask);
}
