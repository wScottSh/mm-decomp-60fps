/**
 * Benchmark mode, see include/bench.h and tools/bench/README.md.
 */
#include "global.h"
#include "bench.h"
#include "carthandle.h"
#include "gfx.h"
#include "irqmgr.h"
#include "scheduler.h"
#include "thga.h"
#include "z64speed_meter.h"
#include "zelda_arena.h"
#include "libc64/malloc.h"
#include "libc64/qrand.h"
#include "libc64/sprintf.h"

typedef struct BenchScene {
    const char* name;
    u16 entrance;
    u16 day;
    s32 time;
} BenchScene;

static BenchScene sBenchScenes[] = {
    { "south_clock_town", ENTRANCE(SOUTH_CLOCK_TOWN, 0), 1, CLOCK_TIME(10, 0) },
    { "termina_field", ENTRANCE(TERMINA_FIELD, 0), 1, CLOCK_TIME(10, 0) },
    { "mountain_village_winter", ENTRANCE(MOUNTAIN_VILLAGE_WINTER, 0), 1, CLOCK_TIME(10, 0) },
    { "great_bay_coast", ENTRANCE(GREAT_BAY_COAST, 0), 1, CLOCK_TIME(10, 0) },
};

typedef struct BenchRecord {
    u32 vi;           // VI retraces since the previous frame ended
    u32 preTicks;     // Graph_Update start -> GameState_Update: input, Audio_Update, gfx pool setup (OSTime ticks)
    u32 gameTicks;    // GameState_Update wall time: game logic + display list build
    u32 cpuTicks;     // whole Graph_Update wall time, also includes waiting on the previous RCP task
    u32 periodTicks;  // gGraphUpdatePeriod: time between consecutive Graph_ExecuteAndDraw ends
    u32 rspGfxTicks;  // gRSPGfxTimeTotal
    u32 rspAudTicks;  // gRSPAudioTimeTotal
    u32 rdpTicks;     // gRDPTimeTotal (gfx task start -> DP full sync, not true RDP busy time)
    u8 divisor;
    u8 actors;
    u16 pad;
    u32 opaBytes;     // display list bytes used this frame
    u32 xluBytes;
    u32 ovlBytes;
    u32 thaFree;      // game state arena remaining
    u32 zeldaFree;
    u32 zeldaMaxFree;
    u32 sysFree;
    u32 benchTicks;   // time spent in Bench_EndFrame bookkeeping on the previous frame
    // emux XPROF slot 0 deltas for this frame (zero without emux)
    u32 cpuCycles;
    u32 icMiss;
    u32 dcMiss;
    u32 dcWb;
    u32 rspCycles;
    u32 rspHalted;
    u32 rdramTotal;
    u32 rdramCpu;     // icache + dcache + uncached
    u32 rdramSpDma;
    u32 rdramDp;      // DP draw + DP command DMA
} BenchRecord;

typedef enum BenchPhase {
    BENCH_PHASE_WARMUP,
    BENCH_PHASE_MEASURE,
    BENCH_PHASE_DONE
} BenchPhase;

BenchConfig gBenchConfig = { BENCH_CONFIG_MAGIC, 0, 120, BENCH_MAX_FRAMES, 0x12345678 };

static BenchRecord sBenchRecords[BENCH_MAX_FRAMES];
static s32 sBenchPhase = BENCH_PHASE_WARMUP;
static s32 sBenchFrame = 0;
static s32 sBenchEmux = false;
static s32 sBenchLastRetrace = 0;
static OSTime sBenchFrameStart = 0;
static OSTime sBenchGameStart = 0;
static OSTime sBenchGameEnd = 0;
static u32 sBenchOverheadTicks = 0;
static char sBenchLine[512];

/**
 * Write a NUL-terminated string to IS-Viewer at cart address 0x13FF0000.
 */
static void Bench_Puts(const char* str) {
    u32 len = 0;
    u32 i;

    while (str[len] != '\0') {
        len++;
    }

    for (i = 0; i < len; i += 4) {
        u32 word = 0;
        u32 j;

        for (j = 0; j < 4; j++) {
            word <<= 8;
            if (i + j < len) {
                word |= (u8)str[i + j];
            }
        }
        osEPiWriteIo(gCartHandle, 0x03FF0020 + i, word);
    }
    osEPiWriteIo(gCartHandle, 0x03FF0014, len);
}

void Bench_Boot(GameState* gameState) {
    BenchScene* scene;

    if (gBenchConfig.scene >= ARRAY_COUNT(sBenchScenes)) {
        gBenchConfig.scene = 0;
    }
    if (gBenchConfig.frames > BENCH_MAX_FRAMES) {
        gBenchConfig.frames = BENCH_MAX_FRAMES;
    }
    scene = &sBenchScenes[gBenchConfig.scene];

    sBenchEmux = Bench_EmuxDetect() != 0;

    sprintf(sBenchLine,
            "BENCH START scene=%d name=%s seed=0x%08X warmup=%d frames=%d emux=%d retrace_ticks=%u counter_hz=%u\n",
            gBenchConfig.scene, scene->name, gBenchConfig.seed, gBenchConfig.warmup, gBenchConfig.frames, sBenchEmux,
            (u32)gIrqMgrRetraceTime, (u32)OS_CPU_COUNTER);
    Bench_Puts(sBenchLine);

    Rand_Seed(gBenchConfig.seed);
    Sram_InitDebugSave();

    gSaveContext.buttonStatus[EQUIP_SLOT_B] = BTN_ENABLED;
    gSaveContext.buttonStatus[EQUIP_SLOT_C_LEFT] = BTN_ENABLED;
    gSaveContext.buttonStatus[EQUIP_SLOT_C_DOWN] = BTN_ENABLED;
    gSaveContext.buttonStatus[EQUIP_SLOT_C_RIGHT] = BTN_ENABLED;
    gSaveContext.buttonStatus[EQUIP_SLOT_A] = BTN_ENABLED;
    gSaveContext.hudVisibilityForceButtonAlphasByStatus = false;
    gSaveContext.nextHudVisibility = HUD_VISIBILITY_IDLE;
    gSaveContext.hudVisibility = HUD_VISIBILITY_IDLE;
    gSaveContext.hudVisibilityTimer = 0;

    gSaveContext.save.entrance = scene->entrance;
    gSaveContext.save.day = scene->day;
    gSaveContext.save.time = scene->time;

    gSaveContext.respawn[RESPAWN_MODE_DOWN].entrance = 0xFFFF;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.ambienceId = AMBIENCE_ID_DISABLED;
    gSaveContext.showTitleCard = true;
    gSaveContext.respawnFlag = 0;
    gSaveContext.respawn[RESPAWN_MODE_GORON].entrance = 0xFF;
    gSaveContext.respawn[RESPAWN_MODE_ZORA].entrance = 0xFF;
    gSaveContext.respawn[RESPAWN_MODE_DEKU].entrance = 0xFF;
    gSaveContext.respawn[RESPAWN_MODE_HUMAN].entrance = 0xFF;
    gWeatherMode = WEATHER_MODE_CLEAR;

    STOP_GAMESTATE(gameState);
    SET_NEXT_GAMESTATE(gameState, Play_Init, sizeof(PlayState));
}

void Bench_MarkGameStart(GameState* gameState) {
    sBenchGameStart = osGetTime();
}

void Bench_MarkGameEnd(GameState* gameState) {
    sBenchGameEnd = osGetTime();
}

void Bench_BeginFrame(GameState* gameState) {
    sBenchFrameStart = osGetTime();

    if (sBenchEmux) {
        Bench_XProfClear(0);
        Bench_XProfStart(0);
    }
}

static void Bench_Dump(void) {
    s32 i;

    Bench_Puts("BENCH COLUMNS frame vi pre_ticks game_ticks cpu_ticks period_ticks rsp_gfx_ticks rsp_aud_ticks rdp_ticks divisor actors "
               "opa_bytes xlu_bytes ovl_bytes tha_free zelda_free zelda_max_free sys_free bench_ticks cpu_cycles "
               "ic_miss dc_miss dc_wb rsp_cycles rsp_halted rdram_total rdram_cpu rdram_sp_dma rdram_dp\n");

    for (i = 0; i < (s32)gBenchConfig.frames; i++) {
        BenchRecord* r = &sBenchRecords[i];

        sprintf(sBenchLine, "BENCH F %d %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
                i, r->vi, r->preTicks, r->gameTicks, r->cpuTicks, r->periodTicks, r->rspGfxTicks, r->rspAudTicks, r->rdpTicks, r->divisor,
                r->actors, r->opaBytes, r->xluBytes, r->ovlBytes, r->thaFree, r->zeldaFree, r->zeldaMaxFree,
                r->sysFree, r->benchTicks, r->cpuCycles, r->icMiss, r->dcMiss, r->dcWb, r->rspCycles, r->rspHalted,
                r->rdramTotal, r->rdramCpu, r->rdramSpDma, r->rdramDp);
        Bench_Puts(sBenchLine);
    }

    Bench_Puts("BENCH END\n");
}

void Bench_EndFrame(GraphicsContext* gfxCtx, GameState* gameState) {
    OSTime endTime = osGetTime();
    s32 retrace = gScheduler.retraceCount;
    BenchRecord* r;
    size_t maxFree;
    size_t free;
    size_t alloc;

    if (sBenchEmux) {
        Bench_XProfStop(0);
    }

    if ((sBenchPhase == BENCH_PHASE_DONE) || (gameState->destroy != Play_Destroy)) {
        return;
    }

    if (sBenchPhase == BENCH_PHASE_WARMUP) {
        if (++sBenchFrame >= (s32)gBenchConfig.warmup) {
            sBenchPhase = BENCH_PHASE_MEASURE;
            sBenchFrame = 0;
            sBenchLastRetrace = retrace;
            sBenchOverheadTicks = 0;
        }
        return;
    }

    r = &sBenchRecords[sBenchFrame];

    r->vi = retrace - sBenchLastRetrace;
    sBenchLastRetrace = retrace;
    r->preTicks = sBenchGameStart - sBenchFrameStart;
    r->gameTicks = sBenchGameEnd - sBenchGameStart;
    r->cpuTicks = endTime - sBenchFrameStart;
    r->periodTicks = gGraphUpdatePeriod;
    r->rspGfxTicks = gRSPGfxTimeTotal;
    r->rspAudTicks = gRSPAudioTimeTotal;
    r->rdpTicks = gRDPTimeTotal;
    r->divisor = gameState->framerateDivisor;
    r->actors = ((PlayState*)gameState)->actorCtx.totalLoadedActors;
    r->opaBytes = sizeof(((GfxPool*)NULL)->polyOpaBuffer) - THGA_GetRemaining(&gfxCtx->polyOpa);
    r->xluBytes = sizeof(((GfxPool*)NULL)->polyXluBuffer) - THGA_GetRemaining(&gfxCtx->polyXlu);
    r->ovlBytes = sizeof(((GfxPool*)NULL)->overlayBuffer) - THGA_GetRemaining(&gfxCtx->overlay);
    r->thaFree = GameState_GetArenaSize(gameState);
    ZeldaArena_GetSizes(&maxFree, &free, &alloc);
    r->zeldaFree = free;
    r->zeldaMaxFree = maxFree;
    GetFreeArena(&maxFree, &free, &alloc);
    r->sysFree = free;
    r->benchTicks = sBenchOverheadTicks;

    if (sBenchEmux) {
        r->cpuCycles = Bench_XProfRead(0, XPROF_CPU_CYCLES);
        r->icMiss = Bench_XProfRead(0, XPROF_ICACHE_MISSES);
        r->dcMiss = Bench_XProfRead(0, XPROF_DCACHE_MISSES);
        r->dcWb = Bench_XProfRead(0, XPROF_DCACHE_WRITEBACKS);
        r->rspCycles = Bench_XProfRead(0, XPROF_RSP_CYCLES);
        r->rspHalted = Bench_XProfRead(0, XPROF_RSP_HALTED_CYCLES);
        r->rdramTotal = Bench_XProfRead(0, XPROF_RDRAM_TOTAL);
        r->rdramCpu = Bench_XProfRead(0, XPROF_RDRAM_ICACHE) + Bench_XProfRead(0, XPROF_RDRAM_DCACHE) +
                      Bench_XProfRead(0, XPROF_RDRAM_UNCACHED);
        r->rdramSpDma = Bench_XProfRead(0, XPROF_RDRAM_SP_DMA);
        r->rdramDp = Bench_XProfRead(0, XPROF_RDRAM_DP_DRAW) + Bench_XProfRead(0, XPROF_RDRAM_DP_DMA);
    }

    if (++sBenchFrame >= (s32)gBenchConfig.frames) {
        sBenchPhase = BENCH_PHASE_DONE;
        Bench_Dump();
        if (sBenchEmux) {
            Bench_EmuxExit();
        }
        return;
    }

    sBenchOverheadTicks = osGetTime() - endTime;
}
