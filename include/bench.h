#ifndef BENCH_H
#define BENCH_H

/**
 * Benchmark mode. Only compiled when building with `make BENCH=1`; the matching build is unaffected.
 *
 * Boots straight into a fixed scene with a fixed RNG seed, lets it warm up, then records per-frame
 * timing, memory, and (on ares) emux profiler counters into RAM. At the end the records are written
 * out over IS-Viewer as text and, on ares, the emulator is asked to exit.
 *
 * See tools/bench/README.md.
 */

#include "ultra64.h"
#include "scheduler.h"

struct GameState;
struct GraphicsContext;

// Size of the per-frame record buffer
#define BENCH_MAX_FRAMES 600

#define BENCH_CONFIG_MAGIC 0x424E4348 // "BNCH"
#define BENCH_PROBE_MAGIC 0x50524F42  // "PROB"

#define BENCH_MAX_INPUTS 512

// BenchConfig.noisePin
#define BENCH_NOISE_PIN_OFF 0   // retail RDP noise (perf runs)
#define BENCH_NOISE_PIN_ON 1    // Bench_PinNoise neutralizes RDP noise in every gfx task (fidelity runs)
#define BENCH_NOISE_PIN_CHECK 2 // as ON, and every task is walked a second time to check the walk is idempotent

// BenchConfig.flags
#define BENCH_FLAG_RECORD (1 << 0)            // keep per-frame records, dump them and exit (tools/bench)
#define BENCH_FLAG_PIN_EFFECT_BUDGET (1 << 1) // func_80173B48 returns its no-lag value

/**
 * Controller 1 from the tick `tick` ticks after the scene settled (BenchProbe.settleTick) until the
 * next entry's tick. Entries are sorted by tick.
 */
typedef struct BenchInput {
    u16 tick;
    u16 button;
    s8 stickX;
    s8 stickY;
    u16 pad;
} BenchInput;

/**
 * Run configuration. tools/bench/bench.py and tools/fps60/run.py find this in a copy of the ROM by its
 * magic and patch it, so changing scenes doesn't need a rebuild.
 */
typedef struct BenchConfig {
    u32 magic;
    u32 scene;
    u32 warmup;
    u32 frames; // clamped to BENCH_MAX_FRAMES
    u32 seed;
    u32 flags;
    u32 retraceTicks; // the gIrqMgrRetraceTime that the pinned effect budget uses
    u32 inputCount;   // clamped to BENCH_MAX_INPUTS
    BenchInput inputs[BENCH_MAX_INPUTS];
    u32 noisePin;     // BENCH_NOISE_PIN_*
} BenchConfig;

extern BenchConfig gBenchConfig;

/**
 * The noise pin's walk of one gfx task's display lists (Bench_PinNoise, src/code/bench.c). A noise command
 * is one the pin rewrites: a G_SETOTHERMODE_H/L, G_RDPSETOTHERMODE or G_SETCOMBINE that selects G_CD_NOISE,
 * G_AD_NOISE, G_AC_DITHER or the combiner's NOISE input, or that writes the pipeline mode.
 */
typedef struct BenchNoiseRecord {
    u32 hash;     // FNV-1a over both words of every noise command as the game wrote it, in walk order
    u32 commands; // noise commands the walk met
    u32 patched;  // of those, the ones this walk rewrote; the others an earlier walk rewrote
    u32 visited;  // display-list commands the walk visited
} BenchNoiseRecord;

// Since boot, over every walk
typedef struct BenchNoiseTotals {
    u32 walks;
    u32 visited;
    u32 commands;
    u32 patched;
    u32 unresolved; // G_DL, G_BRANCH_Z targets outside RDRAM or in a segment that was never set
    u32 unknown;    // opcodes the walker cannot follow, and noise fields a partial othermode write leaves undecided
    u32 overflow;   // walks cut short by the call stack or the visit cap, and patches the log had no room for
    u32 mismatch;   // BENCH_NOISE_PIN_CHECK: second walks whose record differs from the first's or that patched
} BenchNoiseTotals;

/**
 * What the fps60 harness reads between VI fields (tools/fps60/README.md). The layout is the harness
 * contract: run.py reads these words by offset from the map address of gBenchProbe.
 */
typedef struct BenchProbe {
    /* 0x00 */ u32 magic;
    /* 0x04 */ u32 tick;        // logic ticks since boot; tick N runs from its Graph_Update's start
    /* 0x08 */ u32 divisor;     // framerateDivisor after the latest tick
    /* 0x0C */ u32 settled;     // the scene finished entering (Play fade-in done, file select idle)
    /* 0x10 */ u32 settleTick;  // the tick that settled it
    /* 0x14 */ CfbInfo** curBuf;      // &gScheduler.curBuf, the CfbInfo the VI shows
    /* 0x18 */ CfbInfo* cfbs;         // sGraphCfbInfos
    /* 0x1C */ u32 cfbSize;     // sizeof(CfbInfo)
    /* 0x20 */ u32 cfbTick[3];  // the tick whose image each sGraphCfbInfos entry holds
    /* 0x2C */ BenchNoiseRecord cfbNoise[3]; // that tick's noise-pin walk, zero with the pin off
    /* 0x5C */ BenchNoiseTotals noise;
} BenchProbe;

extern BenchProbe gBenchProbe;

s32 Bench_Boot(struct GameState* gameState);
void Bench_BeginFrame(struct GameState* gameState);
void Bench_MarkGameStart(struct GameState* gameState);
void Bench_MarkGameEnd(struct GameState* gameState);
void Bench_EndFrame(struct GraphicsContext* gfxCtx, struct GameState* gameState);
void Bench_TagCfb(CfbInfo* cfb);
void Bench_PinNoise(CfbInfo* cfb, Gfx* taskList);
void Bench_ApplyInput(struct GameState* gameState);

// bench_emux.s
s32 Bench_EmuxDetect(void);
void Bench_XProfStart(u32 slot);
void Bench_XProfStop(u32 slot);
void Bench_XProfClear(u32 slot);
u32 Bench_XProfRead(s32 slot, u32 metric);
void Bench_EmuxExit(void);

// emux XPROFREAD metric ids (ares/n64/cpu/emux.cpp)
#define XPROF_CPU_CYCLES 0x0000
#define XPROF_ICACHE_MISSES 0x0011
#define XPROF_DCACHE_MISSES 0x0021
#define XPROF_DCACHE_WRITEBACKS 0x0022
#define XPROF_RSP_CYCLES 0x0200
#define XPROF_RSP_HALTED_CYCLES 0x0201
#define XPROF_RDRAM_TOTAL 0x0300
#define XPROF_RDRAM_ICACHE 0x0310
#define XPROF_RDRAM_DCACHE 0x0320
#define XPROF_RDRAM_UNCACHED 0x0330
#define XPROF_RDRAM_SP_DMA 0x0340
#define XPROF_RDRAM_PI_DMA 0x0350
#define XPROF_RDRAM_DP_DRAW 0x0370
#define XPROF_RDRAM_AI_DMA 0x0380
#define XPROF_RDRAM_VI_DMA 0x0390
#define XPROF_RDRAM_DP_DMA 0x03A0

#endif
