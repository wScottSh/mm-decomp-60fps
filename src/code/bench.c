/**
 * Benchmark mode, see include/bench.h and tools/bench/README.md.
 */
#include "global.h"
#include "bench.h"
#include "buffers.h"
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
#include "libu64/pad.h"
#include "sys_flashrom.h"
#include "sys_ucode.h"
#include "PR/gs2dex.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"

typedef enum BenchKind {
    BENCH_KIND_PLAY,        // the debug save in a Play scene at entrance, day and time
    BENCH_KIND_FILE_SELECT, // the file-select main screen, cursor on File 1, with namedFiles named files
    BENCH_KIND_RETAIL       // the retail boot: console logo, then the title
} BenchKind;

typedef struct BenchScene {
    const char* name;
    u8 kind;
    u8 namedFiles;
    u16 entrance;
    u16 day;
    s32 time;
} BenchScene;

static BenchScene sBenchScenes[] = {
    { "south_clock_town", BENCH_KIND_PLAY, 0, ENTRANCE(SOUTH_CLOCK_TOWN, 0), 1, CLOCK_TIME(12, 0) },
    { "termina_field", BENCH_KIND_PLAY, 0, ENTRANCE(TERMINA_FIELD, 0), 1, CLOCK_TIME(10, 0) },
    { "mountain_village_winter", BENCH_KIND_PLAY, 0, ENTRANCE(MOUNTAIN_VILLAGE_WINTER, 0), 1, CLOCK_TIME(10, 0) },
    { "great_bay_coast", BENCH_KIND_PLAY, 0, ENTRANCE(GREAT_BAY_COAST, 0), 1, CLOCK_TIME(10, 0) },
    { "file_select_empty", BENCH_KIND_FILE_SELECT, 0, 0, 0, 0 },
    { "file_select_named", BENCH_KIND_FILE_SELECT, 2, 0, 0, 0 },
    { "retail_boot", BENCH_KIND_RETAIL, 0, 0, 0, 0 },
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
    // DPC counters (RCP clocks) from the previous frame's end to this one's: Bench_EndFrame reads, then clears them
    u32 dpcClock;
    u32 dpcBufBusy;
    u32 dpcPipeBusy;
    u32 dpcTmem;
} BenchRecord;

typedef enum BenchPhase {
    BENCH_PHASE_WARMUP,
    BENCH_PHASE_MEASURE,
    BENCH_PHASE_DONE
} BenchPhase;

// retraceTicks is the gIrqMgrRetraceTime that IrqMgr measures on the fps60 runner (BENCH START line)
BenchConfig gBenchConfig = {
    BENCH_CONFIG_MAGIC, 0, 120, BENCH_MAX_FRAMES, 0x12345678, BENCH_FLAG_RECORD | BENCH_FLAG_PIN_EFFECT_BUDGET,
    782715, 0, { { 0 } }, false,
};

BenchProbe gBenchProbe;

extern CfbInfo sGraphCfbInfos[3];

static BenchRecord sBenchRecords[BENCH_MAX_FRAMES];
static BenchScene* sBenchScene;
static s32 sBenchPhase = BENCH_PHASE_WARMUP;
static s32 sBenchFrame = 0;
static s32 sBenchEmux = false;
static s32 sBenchLastRetrace = 0;
static OSTime sBenchFrameStart = 0;
static OSTime sBenchGameStart = 0;
static OSTime sBenchGameEnd = 0;
static u32 sBenchOverheadTicks = 0;
static char sBenchLine[512];
static s32 sBenchSawFade = false;
static OSContPad sBenchPad;

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

/**
 * Writes files 1 and 2 to flash: the first namedFiles as the new save that name entry's Sram_InitSave
 * writes for the name "A", the others and both owl saves empty, so the screen does not depend on what
 * the cartridge's flash held before.
 */
static void Bench_WriteFiles(GameState* gameState, s32 namedFiles) {
    u8* buf = THA_AllocTailAlign16(&gameState->tha, SAVE_BUFFER_SIZE);
    s32 file;
    s32 i;

    for (file = 0; file < 2; file++) {
        bzero(buf, SAVE_BUFFER_SIZE);
        if (file < namedFiles) {
            Sram_InitNewSave();
            if (file == 0) {
                gSaveContext.save.cutsceneIndex = 0xFFF0;
            }
            // 'A' then blanks (D_808141F0 in z_file_nameset_data.c)
            gSaveContext.save.saveInfo.playerData.playerName[0] = 0x0A;
            for (i = 1; i < ARRAY_COUNT(gSaveContext.save.saveInfo.playerData.playerName); i++) {
                gSaveContext.save.saveInfo.playerData.playerName[i] = 0x3E;
            }
            gSaveContext.save.saveInfo.playerData.newf[0] = 'Z';
            gSaveContext.save.saveInfo.playerData.newf[1] = 'E';
            gSaveContext.save.saveInfo.playerData.newf[2] = 'L';
            gSaveContext.save.saveInfo.playerData.newf[3] = 'D';
            gSaveContext.save.saveInfo.playerData.newf[4] = 'A';
            gSaveContext.save.saveInfo.playerData.newf[5] = '3';
            gSaveContext.save.saveInfo.checksum = Sram_CalcChecksum(&gSaveContext.save, sizeof(Save));
            Lib_MemCpy(buf, &gSaveContext.save, sizeof(Save));
            Lib_MemCpy(&buf[0x2000], &gSaveContext.save, sizeof(Save));
        }
        SysFlashrom_WriteSync(buf, gFlashSaveStartPages[file * 2], gFlashSpecialSaveNumPages[file * 2]);
        bzero(buf, SAVE_BUFFER_SIZE);
        for (i = 0; i < 2; i++) {
            SysFlashrom_WriteSync(buf, gFlashSaveStartPages[4 + file * 2 + i], gFlashSaveNumPages[4 + file * 2 + i]);
        }
    }
}

static void Bench_BootFileSelect(GameState* gameState, BenchScene* scene) {
    // What ConsoleLogo_Init and the title screen's EnMag set on the way to file select
    gSaveContext.flashSaveAvailable = true;
    gSaveContext.fileNum = 0xFF;
    gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
    Bench_WriteFiles(gameState, scene->namedFiles);

    STOP_GAMESTATE(gameState);
    SET_NEXT_GAMESTATE(gameState, FileSelect_Init, sizeof(FileSelectState));
}

static void Bench_BootPlay(GameState* gameState, BenchScene* scene) {
    // What the retail route to map select sets before MapSelect_LoadGame: ConsoleLogo's fileNum, and
    // MapSelect_Init's cutscene, form and age. SaveContext_Init leaves playerForm 0, Fierce Deity.
    gSaveContext.fileNum = 0xFF;
    gSaveContext.save.cutsceneIndex = 0;
    gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
    gSaveContext.save.linkAge = 0;
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

s32 Bench_Boot(GameState* gameState) {
    if (gBenchConfig.scene >= ARRAY_COUNT(sBenchScenes)) {
        gBenchConfig.scene = 0;
    }
    if (gBenchConfig.frames > BENCH_MAX_FRAMES) {
        gBenchConfig.frames = BENCH_MAX_FRAMES;
    }
    if (gBenchConfig.inputCount > BENCH_MAX_INPUTS) {
        gBenchConfig.inputCount = BENCH_MAX_INPUTS;
    }
    sBenchScene = &sBenchScenes[gBenchConfig.scene];

    gBenchProbe.curBuf = &gScheduler.curBuf;
    gBenchProbe.cfbs = sGraphCfbInfos;
    gBenchProbe.cfbSize = sizeof(CfbInfo);
    gBenchProbe.magic = BENCH_PROBE_MAGIC;

    sBenchEmux = Bench_EmuxDetect() != 0;

    sprintf(sBenchLine,
            "BENCH START scene=%d name=%s seed=0x%08X warmup=%d frames=%d emux=%d retrace_ticks=%u counter_hz=%u\n",
            gBenchConfig.scene, sBenchScene->name, gBenchConfig.seed, gBenchConfig.warmup, gBenchConfig.frames,
            sBenchEmux, (u32)gIrqMgrRetraceTime, (u32)OS_CPU_COUNTER);
    Bench_Puts(sBenchLine);

    Rand_Seed(gBenchConfig.seed);

    if (sBenchScene->kind == BENCH_KIND_FILE_SELECT) {
        Bench_BootFileSelect(gameState, sBenchScene);
    } else if (sBenchScene->kind == BENCH_KIND_PLAY) {
        Bench_BootPlay(gameState, sBenchScene);
    } else {
        return false;
    }
    return true;
}

/**
 * Whether this frame is one of the scene's: a Play frame, or a file-select frame on the idle main screen.
 * A file-select run has no other game state with frames (Setup stops before its first).
 */
static s32 Bench_InScene(GameState* gameState) {
    FileSelectState* fileSelect;

    if (sBenchScene->kind != BENCH_KIND_FILE_SELECT) {
        return gameState->destroy == Play_Destroy;
    }
    fileSelect = (FileSelectState*)gameState;
    return (fileSelect->menuMode == FS_MENU_MODE_CONFIG) && (fileSelect->configMode == CM_MAIN_MENU);
}

void Bench_MarkGameStart(GameState* gameState) {
    sBenchGameStart = osGetTime();
}

void Bench_MarkGameEnd(GameState* gameState) {
    sBenchGameEnd = osGetTime();
}

void Bench_BeginFrame(GameState* gameState) {
    gBenchProbe.tick++;
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
               "ic_miss dc_miss dc_wb rsp_cycles rsp_halted rdram_total rdram_cpu rdram_sp_dma rdram_dp "
               "dpc_clock dpc_bufbusy dpc_pipebusy dpc_tmem\n");

    for (i = 0; i < (s32)gBenchConfig.frames; i++) {
        BenchRecord* r = &sBenchRecords[i];

        sprintf(sBenchLine, "BENCH F %d %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
                i, r->vi, r->preTicks, r->gameTicks, r->cpuTicks, r->periodTicks, r->rspGfxTicks, r->rspAudTicks, r->rdpTicks, r->divisor,
                r->actors, r->opaBytes, r->xluBytes, r->ovlBytes, r->thaFree, r->zeldaFree, r->zeldaMaxFree,
                r->sysFree, r->benchTicks, r->cpuCycles, r->icMiss, r->dcMiss, r->dcWb, r->rspCycles, r->rspHalted,
                r->rdramTotal, r->rdramCpu, r->rdramSpDma, r->rdramDp, r->dpcClock, r->dpcBufBusy, r->dpcPipeBusy,
                r->dpcTmem);
        Bench_Puts(sBenchLine);
    }

    Bench_Puts("BENCH END\n");
}

/**
 * A scene settles on the tick that ends its entry: a Play scene's fade-in (transitionMode goes from
 * off to on and back to off) and any entrance cutscene, or the file-select main screen going idle.
 * The retail boot settles on its first Play scene, the title, when it has faded in.
 */
static void Bench_UpdateProbe(GameState* gameState) {
    gBenchProbe.divisor = gameState->framerateDivisor;

    if (gBenchProbe.settled || !Bench_InScene(gameState)) {
        return;
    }
    if (sBenchScene->kind != BENCH_KIND_FILE_SELECT) {
        PlayState* play = (PlayState*)gameState;

        if (play->transitionMode != TRANS_MODE_OFF) {
            sBenchSawFade = true;
            return;
        }
        if (!sBenchSawFade) {
            return;
        }
        // A first visit opens with the scene's entrance cutscene (Termina Field, Great Bay Coast). The
        // title is a cutscene throughout, so the retail boot settles at its fade-in.
        if ((sBenchScene->kind == BENCH_KIND_PLAY) && (play->csCtx.state != CS_STATE_IDLE)) {
            return;
        }
    }
    gBenchProbe.settleTick = gBenchProbe.tick;
    gBenchProbe.settled = true;
}

void Bench_TagCfb(CfbInfo* cfb) {
    gBenchProbe.cfbTick[cfb - sGraphCfbInfos] = gBenchProbe.tick;
}

/**
 * From the first tick after the scene settled, controller 1 reads gBenchConfig.inputs instead of the
 * pad, with prev, press and rel derived as PadMgr_GetInputNoLock derives them, so the game sees a pad
 * that changed on exactly that tick in every build.
 */
void Bench_ApplyInput(GameState* gameState) {
    Input* input = &gameState->input[0];
    BenchInput* entry = NULL;
    u32 since;
    u32 i;
    s32 buttonDiff;

    if (!gBenchProbe.settled || (gBenchConfig.inputCount == 0)) {
        return;
    }
    since = gBenchProbe.tick - gBenchProbe.settleTick - 1;
    for (i = 0; (i < gBenchConfig.inputCount) && (gBenchConfig.inputs[i].tick <= since); i++) {
        entry = &gBenchConfig.inputs[i];
    }
    if (entry == NULL) {
        sBenchPad = input->cur;
        return;
    }

    input->prev = sBenchPad;
    input->cur.button = entry->button;
    input->cur.stick_x = entry->stickX;
    input->cur.stick_y = entry->stickY;
    buttonDiff = input->prev.button ^ input->cur.button;
    input->press.button = input->cur.button & buttonDiff;
    input->rel.button = input->prev.button & buttonDiff;
    PadUtils_UpdateRelXY(input);
    sBenchPad = input->cur;
}

void Bench_EndFrame(GraphicsContext* gfxCtx, GameState* gameState) {
    OSTime endTime = osGetTime();
    s32 retrace = gScheduler.retraceCount;
    BenchRecord* r;
    size_t maxFree;
    size_t free;
    size_t alloc;
    u32 dpcClock = IO_READ(DPC_CLOCK_REG);
    u32 dpcBufBusy = IO_READ(DPC_BUFBUSY_REG);
    u32 dpcPipeBusy = IO_READ(DPC_PIPEBUSY_REG);
    u32 dpcTmem = IO_READ(DPC_TMEM_REG);

    // The counters are 24 bits, 0.27 s at 62.5 MHz; clearing them each frame keeps one frame's count below that.
    IO_WRITE(DPC_STATUS_REG, DPC_CLR_CLOCK_CTR | DPC_CLR_CMD_CTR | DPC_CLR_PIPE_CTR | DPC_CLR_TMEM_CTR);

    if (sBenchEmux) {
        Bench_XProfStop(0);
    }

    Bench_UpdateProbe(gameState);

    if (!(gBenchConfig.flags & BENCH_FLAG_RECORD) || (sBenchPhase == BENCH_PHASE_DONE) || !Bench_InScene(gameState)) {
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
    r->actors = (sBenchScene->kind == BENCH_KIND_PLAY) ? ((PlayState*)gameState)->actorCtx.totalLoadedActors : 0;
    r->opaBytes = sizeof(((GfxPool*)NULL)->polyOpaBuffer) - THGA_GetRemaining(&gfxCtx->polyOpa);
    r->xluBytes = sizeof(((GfxPool*)NULL)->polyXluBuffer) - THGA_GetRemaining(&gfxCtx->polyXlu);
    r->ovlBytes = sizeof(((GfxPool*)NULL)->overlayBuffer) - THGA_GetRemaining(&gfxCtx->overlay);
    r->thaFree = GameState_GetArenaSize(gameState);
    // Only Play sets up ZeldaArena
    if (ZeldaArena_IsInitialized()) {
        ZeldaArena_GetSizes(&maxFree, &free, &alloc);
        r->zeldaFree = free;
        r->zeldaMaxFree = maxFree;
    }
    GetFreeArena(&maxFree, &free, &alloc);
    r->sysFree = free;
    r->benchTicks = sBenchOverheadTicks;
    r->dpcClock = dpcClock;
    r->dpcBufBusy = dpcBufBusy;
    r->dpcPipeBusy = dpcPipeBusy;
    r->dpcTmem = dpcTmem;

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

/**
 * The noise pin. The RDP's noise LFSRs step once per RDP clock (the ares fork's ares/n64/rdp/timed.hpp,
 * from Thar0/RDP-Noise console dumps), so a pixel that reads noise depends on when the RDP draws it, on a
 * console as well. Just before a gfx task is sent, Bench_PinNoise walks every display list the task
 * reaches, hashes each noise command as the game wrote it, and rewrites it to a fixed substitute:
 *
 *  - G_CD_NOISE   -> G_CD_MAGICSQ. With alpha dither PATTERN or NOTPATTERN, both read the magic square.
 *  - G_AD_NOISE   -> G_AD_PATTERN.
 *  - G_AC_DITHER  -> G_AC_THRESHOLD (blend color alpha instead of a noise threshold).
 *  - combiner RGB A = NOISE -> 1. NOISE is abc100000 in units of 1/256, so 1 is its mean.
 *
 * These are the noise reads of the fork's RDP engine (rdp_get_dither_values, rdp_blender_alpha_reject and
 * rdp_set_suba_input_rgb in ares/n64/rdp/engine/rdp_core.c and rdp_blend.c).
 *
 * Lists in asset and static memory are rewritten in place, and the rewrite stays. A log keeps each rewritten
 * command's original words, so a later walk that meets the substitute still hashes the original. The gfx
 * pools are rewritten every frame, so their log only lives for one walk.
 */
#define BENCH_NOISE_STACK 32
#define BENCH_NOISE_MAX_VISITS 0x40000
#define BENCH_NOISE_RDRAM_SIZE 0x800000
#define BENCH_NOISE_SEGMENT(addr) (((addr) >> 24) & 0xF)

#define BENCH_FNV_OFFSET 0x811C9DC5
#define BENCH_FNV_PRIME 0x01000193

typedef enum BenchUcode {
    BENCH_UCODE_F3DZEX2,
    BENCH_UCODE_S2DEX2
} BenchUcode;

typedef struct BenchNoisePatch {
    u32 addr; // physical address of the command, 0 for an empty slot
    Gfx orig;
    Gfx pinned;
} BenchNoisePatch;

typedef struct BenchNoiseLog {
    u32 mask; // entry count - 1, a power of two minus one
    BenchNoisePatch* entries;
} BenchNoiseLog;

static BenchNoisePatch sBenchNoiseStaticEntries[1024];
static BenchNoisePatch sBenchNoisePoolEntries[512];
static BenchNoiseLog sBenchNoiseStatic = { ARRAY_COUNT(sBenchNoiseStaticEntries) - 1, sBenchNoiseStaticEntries };
static BenchNoiseLog sBenchNoisePool = { ARRAY_COUNT(sBenchNoisePoolEntries) - 1, sBenchNoisePoolEntries };

typedef struct BenchNoiseWalk {
    u32 segments[16];
    u32 stack[BENCH_NOISE_STACK];
    s32 depth;
    u32 rdpHalf1;
    u8 ucode;
    BenchNoiseRecord record;
} BenchNoiseWalk;

static BenchNoiseWalk sBenchNoiseWalk;

static u32 Bench_Fnv(u32 hash, u32 word) {
    s32 i;

    for (i = 24; i >= 0; i -= 8) {
        hash = (hash ^ ((word >> i) & 0xFF)) * BENCH_FNV_PRIME;
    }
    return hash;
}

/**
 * The log slot for `addr`: its entry, or the empty slot it would take. NULL when the log is full.
 */
static BenchNoisePatch* Bench_NoiseSlot(u32 addr) {
    BenchNoiseLog* log = ((addr - OS_K0_TO_PHYSICAL(gGfxPools)) < sizeof(gGfxPools)) ? &sBenchNoisePool
                                                                                      : &sBenchNoiseStatic;
    u32 i = ((addr >> 3) * 0x9E3779B1) >> 20;
    u32 n;

    for (n = 0; n <= log->mask; n++) {
        BenchNoisePatch* slot = &log->entries[(i + n) & log->mask];

        if ((slot->addr == addr) || (slot->addr == 0)) {
            return slot;
        }
    }
    return NULL;
}

/**
 * The othermode bits a G_SETOTHERMODE_H/L writes (F3DEX2 encoding: shift = 32 - len - w0[15:8],
 * len = w0[7:0] + 1).
 */
static u32 Bench_OtherModeMask(u32 w0) {
    u32 len = (w0 & 0xFF) + 1;
    u32 shift = 32 - len - ((w0 >> 8) & 0xFF);

    return ((len >= 32) ? 0xFFFFFFFF : ((1 << len) - 1)) << shift;
}

/**
 * Replaces the 2-bit field at `shift` in `*bits` by `to` if `written` covers it and it holds `noise`.
 * A write that covers half the field leaves its result to the state before, so it counts as unknown.
 */
static void Bench_UnnoiseField(u32 written, u32* bits, u32 shift, u32 noise, u32 to) {
    u32 field = (written >> shift) & 3;

    if (field == 3) {
        if (((*bits >> shift) & 3) == noise) {
            *bits = (*bits & ~(3 << shift)) | (to << shift);
        }
    } else if (field != 0) {
        gBenchProbe.noise.unknown++;
    }
}

/**
 * The command with each noise selection replaced by its substitute. Equal to `cmd` when it selects none.
 */
static Gfx Bench_Unnoise(Gfx cmd) {
    u32 w0 = cmd.words.w0;
    u32 w1 = cmd.words.w1;

    switch (w0 >> 24) {
        case G_SETOTHERMODE_H:
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_RGBDITHER, 2, 0);
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_ALPHADITHER, 2, 0);
            break;

        case G_SETOTHERMODE_L:
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_ALPHACOMPARE, 3, 1);
            break;

        case G_RDPSETOTHERMODE:
            Bench_UnnoiseField(0xFFFFFF, &w0, G_MDSFT_RGBDITHER, 2, 0);
            Bench_UnnoiseField(0xFFFFFF, &w0, G_MDSFT_ALPHADITHER, 2, 0);
            Bench_UnnoiseField(0xFFFFFFFF, &w1, G_MDSFT_ALPHACOMPARE, 3, 1);
            break;

        case G_SETCOMBINE:
            // RGB A of cycle 0 is w0[23:20], of cycle 1 w0[8:5]
            if (((w0 >> 20) & 0xF) == G_CCMUX_NOISE) {
                w0 = (w0 & ~(0xF << 20)) | (G_CCMUX_1 << 20);
            }
            if (((w0 >> 5) & 0xF) == G_CCMUX_NOISE) {
                w0 = (w0 & ~(0xF << 5)) | (G_CCMUX_1 << 5);
            }
            break;

        default:
            break;
    }
    cmd.words.w0 = w0;
    cmd.words.w1 = w1;
    return cmd;
}

static void Bench_NoiseCommand(BenchNoiseWalk* walk, Gfx* cmd, u32 addr) {
    BenchNoisePatch* slot = Bench_NoiseSlot(addr);
    Gfx orig = *cmd;
    Gfx pinned;

    if ((slot != NULL) && (slot->addr == addr) && (slot->pinned.words.w0 == cmd->words.w0) &&
        (slot->pinned.words.w1 == cmd->words.w1)) {
        orig = slot->orig;
        pinned = *cmd;
    } else {
        pinned = Bench_Unnoise(orig);
        if ((pinned.words.w0 == orig.words.w0) && (pinned.words.w1 == orig.words.w1)) {
            if ((slot != NULL) && (slot->addr == addr)) {
                // The game rewrote this command since a walk pinned it; G_NOOP never matches a later read.
                slot->pinned.words.w0 = G_NOOP << 24;
            }
            return;
        }
        if (slot == NULL) {
            gBenchProbe.noise.overflow++;
        } else {
            slot->addr = addr;
            slot->orig = orig;
            slot->pinned = pinned;
        }
        *cmd = pinned;
        walk->record.patched++;
    }
    walk->record.commands++;
    walk->record.hash = Bench_Fnv(Bench_Fnv(walk->record.hash, orig.words.w0), orig.words.w1);
}

/**
 * A display list address as the RSP resolves it (segment base + offset, 24 bits), or 0 when it is outside
 * RDRAM, unaligned, or in a segment that was never set.
 */
static u32 Bench_NoiseResolve(BenchNoiseWalk* walk, u32 addr) {
    u32 segment = BENCH_NOISE_SEGMENT(addr);
    u32 phys;

    if ((segment != 0) && (walk->segments[segment] == 0)) {
        return 0;
    }
    phys = (walk->segments[segment] + (addr & 0xFFFFFF)) & 0xFFFFFF;
    if ((phys == 0) || (phys >= BENCH_NOISE_RDRAM_SIZE) || (phys & 7)) {
        return 0;
    }
    return phys;
}

static s32 Bench_NoiseKnownOpcode(BenchNoiseWalk* walk, u32 op) {
    if (op >= G_SPECIAL_3) {
        return true;
    }
    if (walk->ucode == BENCH_UCODE_F3DZEX2) {
        return op <= G_LINE3D;
    }
    // S2DEX2 (gs2dex.h, F3DEX_GBI_2): 0x03 is unused and G_SELECT_DL's branch is not followed
    return (op <= G_OBJ_RENDERMODE) && (op != 0x03) && (op != G_SELECT_DL);
}

/**
 * Pushes the return address for a call, or counts an overflow. Returns whether the call can be followed.
 */
static s32 Bench_NoisePush(BenchNoiseWalk* walk, u32 ret) {
    if (walk->depth >= BENCH_NOISE_STACK) {
        gBenchProbe.noise.overflow++;
        return false;
    }
    walk->stack[walk->depth++] = ret;
    return true;
}

static void Bench_NoiseWalk(BenchNoiseWalk* walk, u32 pc) {
    u32 visits = 0;

    while (pc != 0) {
        Gfx* cmd = OS_PHYSICAL_TO_K0(pc);
        u32 op = cmd->words.w0 >> 24;
        u32 next = pc + sizeof(Gfx);
        u32 target;

        if (++visits > BENCH_NOISE_MAX_VISITS) {
            gBenchProbe.noise.overflow++;
            break;
        }
        walk->record.visited++;

        if (!Bench_NoiseKnownOpcode(walk, op)) {
            gBenchProbe.noise.unknown++;
        }

        switch (op) {
            case G_DL:
                target = Bench_NoiseResolve(walk, cmd->words.w1);
                if (target == 0) {
                    gBenchProbe.noise.unresolved++;
                } else if (((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH) {
                    next = target;
                } else if (Bench_NoisePush(walk, next)) {
                    next = target;
                }
                break;

            case G_ENDDL:
                next = (walk->depth > 0) ? walk->stack[--walk->depth] : 0;
                break;

            case G_BRANCH_Z:
                // Whether the RSP branches depends on a vertex's depth, so the walk takes both paths: the
                // branch first, then the commands after it.
                if (walk->ucode == BENCH_UCODE_F3DZEX2) {
                    target = Bench_NoiseResolve(walk, walk->rdpHalf1);
                    if (target == 0) {
                        gBenchProbe.noise.unresolved++;
                    } else if (Bench_NoisePush(walk, next)) {
                        next = target;
                    }
                }
                break;

            case G_RDPHALF_1:
                walk->rdpHalf1 = cmd->words.w1;
                break;

            case G_MOVEWORD:
                if (((cmd->words.w0 >> 16) & 0xFF) == G_MW_SEGMENT) {
                    walk->segments[((cmd->words.w0 & 0xFFFF) >> 2) & 0xF] = cmd->words.w1;
                }
                break;

            case G_LOAD_UCODE:
                if ((cmd->words.w1 & 0xFFFFFF) == (OS_K0_TO_PHYSICAL(gspS2DEX2_fifoTextStart) & 0xFFFFFF)) {
                    walk->ucode = BENCH_UCODE_S2DEX2;
                } else if ((cmd->words.w1 & 0xFFFFFF) == (OS_K0_TO_PHYSICAL(SysUcode_GetUCode()) & 0xFFFFFF)) {
                    walk->ucode = BENCH_UCODE_F3DZEX2;
                } else {
                    gBenchProbe.noise.unknown++;
                }
                break;

            case G_SETOTHERMODE_H:
            case G_SETOTHERMODE_L:
            case G_RDPSETOTHERMODE:
            case G_SETCOMBINE:
                Bench_NoiseCommand(walk, cmd, pc);
                break;

            default:
                break;
        }
        pc = next;
    }
}

/**
 * With gBenchConfig.noisePin set, walks the task's display lists (see above) and records the walk for the
 * tick whose image `cfb` will hold. Runs after the previous gfx task finished, so the RCP reads none of
 * the lists it rewrites, and before Sched writes the data cache back for the task.
 */
void Bench_PinNoise(CfbInfo* cfb, Gfx* taskList) {
    BenchNoiseWalk* walk = &sBenchNoiseWalk;

    if (!gBenchConfig.noisePin) {
        return;
    }

    bzero(walk, sizeof(*walk));
    bzero(sBenchNoisePoolEntries, sizeof(sBenchNoisePoolEntries));
    walk->ucode = BENCH_UCODE_F3DZEX2;
    walk->record.hash = BENCH_FNV_OFFSET;

    Bench_NoiseWalk(walk, OS_K0_TO_PHYSICAL(taskList));

    gBenchProbe.cfbNoise[cfb - sGraphCfbInfos] = walk->record;
    gBenchProbe.noise.walks++;
    gBenchProbe.noise.visited += walk->record.visited;
    gBenchProbe.noise.commands += walk->record.commands;
    gBenchProbe.noise.patched += walk->record.patched;
}
