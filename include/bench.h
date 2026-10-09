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

struct GameState;
struct GraphicsContext;

// Size of the per-frame record buffer
#define BENCH_MAX_FRAMES 600

#define BENCH_CONFIG_MAGIC 0x424E4348 // "BNCH"

/**
 * Run configuration. tools/bench/bench.py finds this in a copy of the ROM by its magic and patches it,
 * so changing scenes doesn't need a rebuild.
 */
typedef struct BenchConfig {
    u32 magic;
    u32 scene;
    u32 warmup;
    u32 frames; // clamped to BENCH_MAX_FRAMES
    u32 seed;
} BenchConfig;

extern BenchConfig gBenchConfig;

void Bench_Boot(struct GameState* gameState);
void Bench_BeginFrame(struct GameState* gameState);
void Bench_MarkGameStart(struct GameState* gameState);
void Bench_MarkGameEnd(struct GameState* gameState);
void Bench_EndFrame(struct GraphicsContext* gfxCtx, struct GameState* gameState);

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
