/**
 * Benchmark mode, see include/bench.h and tools/fps60/README.md.
 */
#include "global.h"
#include "bench.h"
#include "buffers.h"
#include "carthandle.h"
#include "gfx.h"
#include "irqmgr.h"
#include "scheduler.h"
#include "z64actor_dlftbls.h"
#include "z64thread.h"
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

// retraceTicks is the gIrqMgrRetraceTime that IrqMgr measures on the fps60 runner (BENCH START line)
BenchConfig gBenchConfig = {
    BENCH_CONFIG_MAGIC, 0, 0x12345678, BENCH_FLAG_PIN_EFFECT_BUDGET, 782715, 0, { { 0 } }, BENCH_NOISE_PIN_OFF,
};

BenchProbe gBenchProbe;

static void Bench_AudioSettle(void);

extern CfbInfo sGraphCfbInfos[3];

static BenchScene* sBenchScene;
static s32 sBenchEmux = false;
static s32 sBenchSawFade = false;
static OSContPad sBenchPad;
static char sBenchLine[256];

// The harness reads the rings with runner peeks, which can see a record's cache lines in different
// states (measured: one stale 16-byte line between two new ones). Only uncached KSEG1 aliases of the
// rings are written, and their cached lines are flushed once at boot.
static BenchField sBenchFieldRing[BENCH_FIELD_RING];
static BenchTick sBenchTickRing[BENCH_TICK_RING];
static volatile BenchField* sBenchFields;
static volatile BenchTick* sBenchTicks;
static u32 sBenchFieldSeq = 0;
static u32 sBenchActorTime[2 * ACTOR_ID_MAX];
static u32 sBenchActorBegin;

// Graph thread accounting for the open tick
static s32 sBenchTickOpen = false;
static BenchTick sBenchTickStart; // the cumulative counters at the open tick's start
static u32 sBenchClockReads = 0;
static s32 sBenchPhase = BENCH_PHASE_TAIL;
static u32 sBenchPhaseWallMark;
static u32 sBenchPhaseCpuMark;
static u32 sBenchPhaseWall[BENCH_PHASE_MAX];
static u32 sBenchPhaseCpu[BENCH_PHASE_MAX];
static u8 sBenchSysStack[8] = { BENCH_SYS_OTHER };
static s32 sBenchSysDepth = 0;
static u32 sBenchSysMark;
static u32 sBenchSys[BENCH_SYS_MAX];

// RCP counters: the scheduler folds the 24-bit DPC counters into these at every retrace
static u32 sBenchRdpPipe = 0;
static u32 sBenchRdpBuf = 0;
static u32 sBenchRspAudio = 0;
static s32 sBenchRspAudioRunning = false;

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

/**
 * Starts gBenchCpuClock on this (the graph) thread and times Bench_CpuNow, the call every per-system
 * hook makes twice.
 */
static void Bench_StartCpuClock(void) {
    BenchCpuClock* clock = &gBenchCpuClock;
    OSIntMask mask = __osDisableInt();
    u32 start;
    s32 i;

    clock->last = osGetCount();
    clock->current = __osRunningThread->id;
    clock->enabled = true;
    __osRestoreInt(mask);

    start = osGetCount();
    for (i = 0; i < 64; i++) {
        Bench_CpuNow();
    }
    gBenchProbe.clockReadCost = (osGetCount() - start) * 2 / 64;
    sBenchClockReads = 0;
}

/**
 * The running thread's CPU time in PClocks. Every caller runs on the graph thread.
 */
u32 Bench_CpuNow(void) {
    BenchCpuClock* clock = &gBenchCpuClock;
    OSIntMask mask = __osDisableInt();
    u32 time = clock->thread[clock->current % BENCH_THREAD_SLOTS] + (osGetCount() - clock->last) * 2;

    __osRestoreInt(mask);
    sBenchClockReads++;
    return time;
}

void Bench_Phase(s32 phase) {
    u32 wall = osGetCount() * 2;
    u32 cpu = Bench_CpuNow();

    sBenchPhaseWall[sBenchPhase] += wall - sBenchPhaseWallMark;
    sBenchPhaseCpu[sBenchPhase] += cpu - sBenchPhaseCpuMark;
    sBenchPhaseWallMark = wall;
    sBenchPhaseCpuMark = cpu;
    sBenchPhase = phase;
}

static void Bench_SysCharge(void) {
    u32 now = Bench_CpuNow();

    sBenchSys[sBenchSysStack[sBenchSysDepth]] += now - sBenchSysMark;
    sBenchSysMark = now;
}

void Bench_SysEnter(s32 system) {
    Bench_SysCharge();
    sBenchSysStack[++sBenchSysDepth] = system;
}

void Bench_SysExit(void) {
    Bench_SysCharge();
    sBenchSysDepth--;
}

// Actor update and draw calls do not nest
void Bench_ActorBegin(void) {
    sBenchActorBegin = Bench_CpuNow();
}

void Bench_ActorEnd(s32 actorId, s32 draw) {
    sBenchActorTime[(draw ? ACTOR_ID_MAX : 0) + actorId] += Bench_CpuNow() - sBenchActorBegin;
}

/**
 * The audio task's RSP time so far. A running task is split at the read, so a field or tick gets the
 * part that ran inside it. Interrupts are disabled, or the caller is the scheduler.
 */
static u32 Bench_RspAudioNow(void) {
    if (sBenchRspAudioRunning) {
        Bench_XProfStop(1);
        sBenchRspAudio += Bench_XProfRead(1, XPROF_RSP_CYCLES);
        Bench_XProfClear(1);
        Bench_XProfStart(1);
    }
    return sBenchRspAudio;
}

void Bench_RspAudioStart(void) {
    if (sBenchEmux) {
        Bench_XProfClear(1);
        Bench_XProfStart(1);
        sBenchRspAudioRunning = true;
    }
}

void Bench_RspAudioStop(void) {
    if (sBenchRspAudioRunning) {
        Bench_XProfStop(1);
        sBenchRspAudio += Bench_XProfRead(1, XPROF_RSP_CYCLES);
        sBenchRspAudioRunning = false;
    }
}

/**
 * The cumulative CPU, RSP and RDP counters into `out`, which has BenchTick's layout for those fields.
 * Interrupts are disabled.
 */
static void Bench_ReadLoad(BenchTick* out, s32 foldDpc) {
    BenchCpuClock* clock = &gBenchCpuClock;
    u32 pipe = IO_READ(DPC_PIPEBUSY_REG);
    u32 buf = IO_READ(DPC_BUFBUSY_REG);

    if (foldDpc) {
        // 24-bit counters, 0.27 s at 62.5 MHz: folding them every retrace keeps them from wrapping
        IO_WRITE(DPC_STATUS_REG, DPC_CLR_PIPE_CTR | DPC_CLR_CMD_CTR);
        sBenchRdpPipe += pipe;
        sBenchRdpBuf += buf;
        pipe = buf = 0;
    }
    Bench_CpuSwitch(__osRunningThread->id);
    out->start = osGetCount();
    out->switches = clock->switches;
    out->idle = clock->thread[Z_THREAD_ID_IDLE];
    out->graph = clock->thread[Z_THREAD_ID_GRAPH];
    out->audio = clock->thread[Z_THREAD_ID_AUDIOMGR];
    out->sched = clock->thread[Z_THREAD_ID_SCHED];
    out->irqmgr = clock->thread[Z_THREAD_ID_IRQMGR];
    out->padmgr = clock->thread[Z_THREAD_ID_PADMGR];
    out->dmamgr = clock->thread[Z_THREAD_ID_DMAMGR];
    out->rsp = sBenchEmux ? Bench_XProfRead(-1, XPROF_RSP_CYCLES) : 0;
    out->rspAudio = Bench_RspAudioNow();
    out->rdpPipe = sBenchRdpPipe + pipe;
    out->rdpBuf = sBenchRdpBuf + buf;
}

/**
 * Runs on the scheduler thread at every VI retrace. Retrace N's record is ring entry N % BENCH_FIELD_RING;
 * the harness may read the ring at any instruction, so the record is valid once its seq is stored last.
 */
void Bench_FieldSnapshot(void) {
    volatile BenchField* field;
    BenchTick load;
    OSIntMask mask;

    if (!gBenchCpuClock.enabled) {
        return;
    }
    sBenchFieldSeq++;
    field = &sBenchFields[sBenchFieldSeq % BENCH_FIELD_RING];
    field->seq = BENCH_RECORD_WRITING;
    mask = __osDisableInt();
    Bench_ReadLoad(&load, true);
    __osRestoreInt(mask);
    field->count = load.start;
    field->switches = load.switches;
    field->idle = load.idle;
    field->graph = load.graph;
    field->audio = load.audio;
    field->rsp = load.rsp;
    field->rspAudio = load.rspAudio;
    field->rdpPipe = load.rdpPipe;
    field->rdpBuf = load.rdpBuf;
    field->seq = sBenchFieldSeq;
}

/**
 * Writes the record of the tick that ends now and starts the next one.
 */
static void Bench_TickBoundary(void) {
    BenchTick now;
    volatile BenchTick* rec;
    OSIntMask mask = __osDisableInt();
    s32 i;

    Bench_ReadLoad(&now, false);
    __osRestoreInt(mask);

    if (sBenchTickOpen) {
        BenchTick* from = &sBenchTickStart;

        rec = &sBenchTicks[gBenchProbe.tick % BENCH_TICK_RING];
        // The harness may read the ring at any instruction: a record is valid once its tick is stored last
        rec->tick = BENCH_RECORD_WRITING;
        rec->start = from->start;
        rec->wall = (now.start - from->start) * 2;
        rec->switches = now.switches - from->switches;
        rec->idle = now.idle - from->idle;
        rec->graph = now.graph - from->graph;
        rec->audio = now.audio - from->audio;
        rec->sched = now.sched - from->sched;
        rec->irqmgr = now.irqmgr - from->irqmgr;
        rec->padmgr = now.padmgr - from->padmgr;
        rec->dmamgr = now.dmamgr - from->dmamgr;
        rec->rsp = now.rsp - from->rsp;
        rec->rspAudio = now.rspAudio - from->rspAudio;
        rec->rdpPipe = now.rdpPipe - from->rdpPipe;
        rec->rdpBuf = now.rdpBuf - from->rdpBuf;
        rec->clockReads = sBenchClockReads;
        for (i = 0; i < BENCH_PHASE_MAX; i++) {
            rec->phaseWall[i] = sBenchPhaseWall[i];
            rec->phaseCpu[i] = sBenchPhaseCpu[i];
        }
        for (i = 0; i < BENCH_SYS_MAX; i++) {
            rec->sys[i] = sBenchSys[i];
        }
        rec->tick = gBenchProbe.tick;
        gBenchProbe.tickCount++;
    }
    sBenchTickStart = now;
    sBenchTickOpen = true;
    sBenchClockReads = 0;
    bzero(sBenchPhaseWall, sizeof(sBenchPhaseWall));
    bzero(sBenchPhaseCpu, sizeof(sBenchPhaseCpu));
    bzero(sBenchSys, sizeof(sBenchSys));
}

s32 Bench_Boot(GameState* gameState) {
    if (gBenchConfig.scene >= ARRAY_COUNT(sBenchScenes)) {
        gBenchConfig.scene = 0;
    }
    if (gBenchConfig.inputCount > BENCH_MAX_INPUTS) {
        gBenchConfig.inputCount = BENCH_MAX_INPUTS;
    }
    sBenchScene = &sBenchScenes[gBenchConfig.scene];

    gBenchProbe.curBuf = &gScheduler.curBuf;
    gBenchProbe.cfbs = sGraphCfbInfos;
    gBenchProbe.cfbSize = sizeof(CfbInfo);
    osWritebackDCache(sBenchFieldRing, sizeof(sBenchFieldRing));
    osInvalDCache(sBenchFieldRing, sizeof(sBenchFieldRing));
    osWritebackDCache(sBenchTickRing, sizeof(sBenchTickRing));
    osInvalDCache(sBenchTickRing, sizeof(sBenchTickRing));
    sBenchFields = (volatile BenchField*)K0_TO_K1(sBenchFieldRing);
    sBenchTicks = (volatile BenchTick*)K0_TO_K1(sBenchTickRing);
    gBenchProbe.fields = (BenchField*)sBenchFields;
    gBenchProbe.ticks = (BenchTick*)sBenchTicks;
    gBenchProbe.actorTime = sBenchActorTime;
    gBenchProbe.layout = (sizeof(BenchField) << 24) | (BENCH_FIELD_RING << 20) | (sizeof(BenchTick) << 10) | BENCH_TICK_RING;
    gBenchProbe.actorIds = ACTOR_ID_MAX;

    sBenchEmux = Bench_EmuxDetect() != 0;
    Bench_StartCpuClock();
    gBenchProbe.magic = BENCH_PROBE_MAGIC;

    sprintf(sBenchLine,
            "BENCH START scene=%d name=%s seed=0x%08X emux=%d retrace_ticks=%u counter_hz=%u clock_read_pclk=%u\n",
            gBenchConfig.scene, sBenchScene->name, gBenchConfig.seed, sBenchEmux, (u32)gIrqMgrRetraceTime,
            (u32)OS_CPU_COUNTER, gBenchProbe.clockReadCost);
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

void Bench_BeginFrame(GameState* gameState) {
    if (!gBenchCpuClock.enabled) {
        gBenchProbe.tick++;
        return;
    }
    Bench_SysEnter(BENCH_SYS_BENCH);
    Bench_Phase(BENCH_PHASE_PRE);
    Bench_TickBoundary();
    gBenchProbe.tick++;
    Bench_SysExit();
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
    Bench_AudioSettle();
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
    Bench_UpdateProbe(gameState);
}

/**
 * The noise pin removes the two ways RDP timing reaches pixels, so a fidelity run's image depends on the
 * game alone:
 *
 *  - Noise. The RDP's noise LFSRs step once per RDP clock (the ares fork's ares/n64/rdp/timed.hpp, from
 *    Thar0/RDP-Noise console dumps), so a pixel that reads noise depends on when the RDP draws it.
 *  - Span buffer coherency. Without G_PM_1PRIMITIVE the RDP prefetches the next span's color and Z before
 *    the previous primitive's write-back lands (SDK 12.2.3; the fork's RDP::prefetch in timed.cpp), so where
 *    primitives overlap, a read sees old or new memory depending on RDRAM contention.
 *
 * Just before a gfx task is sent, Bench_PinNoise walks every display list the task reaches, hashes each
 * pinned command as the game wrote it, and rewrites it:
 *
 *  - G_CD_NOISE   -> G_CD_MAGICSQ. With alpha dither PATTERN or NOTPATTERN, both read the magic square.
 *  - G_AD_NOISE   -> G_AD_PATTERN.
 *  - G_AC_DITHER  -> G_AC_THRESHOLD (blend color alpha instead of a noise threshold).
 *  - combiner RGB A = NOISE -> 1. NOISE is abc100000 in units of 1/256, so 1 is its mean.
 *  - every write of the pipeline mode -> G_PM_1PRIMITIVE. Primitives drawn before a task's first such
 *    write keep the mode the RSP starts with.
 *
 * The noise rules are the noise reads of the fork's RDP engine (rdp_get_dither_values,
 * rdp_blender_alpha_reject and rdp_set_suba_input_rgb in ares/n64/rdp/engine/rdp_core.c and rdp_blend.c).
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
 * The command as the pin rewrites it. Equal to `cmd` when the pin leaves it alone.
 */
static Gfx Bench_PinnedCommand(Gfx cmd) {
    u32 w0 = cmd.words.w0;
    u32 w1 = cmd.words.w1;

    switch (w0 >> 24) {
        case G_SETOTHERMODE_H:
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_RGBDITHER, 2, 0);
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_ALPHADITHER, 2, 0);
            if (Bench_OtherModeMask(w0) & G_PM_1PRIMITIVE) {
                w1 |= G_PM_1PRIMITIVE;
            }
            break;

        case G_SETOTHERMODE_L:
            Bench_UnnoiseField(Bench_OtherModeMask(w0), &w1, G_MDSFT_ALPHACOMPARE, 3, 1);
            break;

        case G_RDPSETOTHERMODE:
            Bench_UnnoiseField(0xFFFFFF, &w0, G_MDSFT_RGBDITHER, 2, 0);
            Bench_UnnoiseField(0xFFFFFF, &w0, G_MDSFT_ALPHADITHER, 2, 0);
            w0 |= G_PM_1PRIMITIVE;
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

/**
 * Whether the command writes any field the pin rewrites, so that adding, dropping or changing it can change
 * the pinned image. G_RDPSETOTHERMODE and G_SETCOMBINE always do.
 */
static s32 Bench_PinCovers(Gfx cmd) {
    u32 w0 = cmd.words.w0;

    switch (w0 >> 24) {
        case G_SETOTHERMODE_H:
            return (Bench_OtherModeMask(w0) &
                    ((3 << G_MDSFT_RGBDITHER) | (3 << G_MDSFT_ALPHADITHER) | G_PM_1PRIMITIVE)) != 0;

        case G_SETOTHERMODE_L:
            return (Bench_OtherModeMask(w0) & (3 << G_MDSFT_ALPHACOMPARE)) != 0;

        default:
            return true;
    }
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
        pinned = Bench_PinnedCommand(orig);
        if ((pinned.words.w0 == orig.words.w0) && (pinned.words.w1 == orig.words.w1)) {
            if ((slot != NULL) && (slot->addr == addr)) {
                // The game rewrote this command since a walk pinned it; G_NOOP never matches a later read.
                slot->pinned.words.w0 = G_NOOP << 24;
            }
            if (!Bench_PinCovers(orig)) {
                return;
            }
        } else {
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
static void Bench_NoiseWalkTask(BenchNoiseWalk* walk, Gfx* taskList) {
    bzero(walk, sizeof(*walk));
    walk->ucode = BENCH_UCODE_F3DZEX2;
    walk->record.hash = BENCH_FNV_OFFSET;
    Bench_NoiseWalk(walk, OS_K0_TO_PHYSICAL(taskList));
}

void Bench_PinNoise(CfbInfo* cfb, Gfx* taskList) {
    BenchNoiseWalk* walk = &sBenchNoiseWalk;
    BenchNoiseRecord first;

    if (gBenchConfig.noisePin == BENCH_NOISE_PIN_OFF) {
        return;
    }

    bzero(sBenchNoisePoolEntries, sizeof(sBenchNoisePoolEntries));
    Bench_NoiseWalkTask(walk, taskList);

    if (gBenchConfig.noisePin == BENCH_NOISE_PIN_CHECK) {
        first = walk->record;
        Bench_NoiseWalkTask(walk, taskList);
        if ((walk->record.hash != first.hash) || (walk->record.commands != first.commands) ||
            (walk->record.visited != first.visited) || (walk->record.patched != 0)) {
            gBenchProbe.noise.mismatch++;
        }
        walk->record = first;
    }

    gBenchProbe.cfbNoise[cfb - sGraphCfbInfos] = walk->record;
    gBenchProbe.noise.walks++;
    gBenchProbe.noise.visited += walk->record.visited;
    gBenchProbe.noise.commands += walk->record.commands;
    gBenchProbe.noise.patched += walk->record.patched;
}

/**
 * Audio fidelity (F4). The audio thread runs one AudioThread_Update per VI retrace, and six paths carry
 * its timing into the PCM it synthesizes. The audio pin (gBenchAudioConfig.pin) fixes each one, so that a
 * fidelity run's audio depends on the game alone, as the noise pin does for pixels:
 *
 *  - Commands. Audio_Update queues tick N's commands on the graph thread, and the next AudioThread_Update
 *    to run takes them (thread.c, the threadCmdProcQueueP loop), so a tick that runs faster or slower
 *    moves its sounds by a frame. The game also reads audio-thread state in Audio_Update. With
 *    BENCH_AUDIO_PIN_CMDS the threads run in lockstep: tick N is due at audio frame D(N), with
 *    D(N + 1) = D(N) + framerateDivisor. Audio_Update(N) starts after frame D(N) - 1 ends, and frame D(N)
 *    starts after Audio_Update(N) ends and takes only commands sent up to then; commands sent later in the
 *    tick wait for D(N + 1). While the graph thread waits for an audio heap reset (thread.c),
 *    frames run on until the reset is done.
 *  - Buffer size. Each frame sizes its buffer from osAiGetLength, the AI's progress at the instant the
 *    thread reads it. BENCH_AUDIO_PIN_AI reads a model instead: the AI's two-buffer FIFO draining
 *    aiSamplingFreq * retraceTicks / OS_CPU_COUNTER samples per frame.
 *  - Randomness. audioRandom and AudioThread_NextRandom mix in osGetCount, and audioRandom also reads a
 *    cached word of an AI buffer that the RSP rewrites. The noise wave (gWaveSamples[8]) plays code bytes
 *    at a random offset from AudioThread_Update, so any code change changes it. BENCH_AUDIO_PIN_RANDOM
 *    uses a fixed sequence and a fixed noise table. Link's metal footstep (code_8019AF00.c) reads
 *    audioRandom mid-tick; under the pin it reads the value from the start of the tick's Audio_Update.
 *  - Async loads. A chunk is polled each frame (load.c, AudioLoad_ProcessAsyncLoad), so it lands on the
 *    frame after its DMA ends. BENCH_AUDIO_PIN_LOADS waits for the DMA on the first poll.
 *  - Sample DMAs. A frame's synthesis starts DMAs of sample data that its RSP task reads, and waits for
 *    them only at the start of the next frame, after AudioMgr has sent that task. So the task reads
 *    whatever part of the data has landed. BENCH_AUDIO_PIN_DMA waits at the end of the frame instead.
 *
 * Bench_AudioSubmit hashes each buffer as osAiSetNextBuffer receives it, read through KSEG1: RDRAM is
 * what the AI's DMA plays. AudioMgr waited for the RSP task that wrote it in the previous frame.
 */
BenchAudioConfig gBenchAudioConfig = { BENCH_AUDIO_MAGIC, 0 };

BenchAudio gBenchAudio;

static BenchAudioFrame sBenchAudioRing[BENCH_AUDIO_RING];
static volatile BenchAudioFrame* sBenchAudioFrames;
static s32 sBenchAudioReady = false;
static OSMesgQueue sBenchAudioGate; // wakes the audio thread
static OSMesg sBenchAudioGateMsg[1];
static OSMesgQueue sBenchGraphGate; // wakes the graph thread
static OSMesg sBenchGraphGateMsg[1];
static volatile u32 sBenchAudioDone = 0;    // frames ended
static volatile u32 sBenchAudioNextDue = 0; // D of the next Audio_Update; frames before it may run
static volatile u32 sBenchAudioSendDue = 0; // D of the commands the graph thread sends now
static volatile s32 sBenchAudioGraphWaiting = false;
static u32 sBenchAudioFrame;   // the running frame
static u32 sBenchAudioAiLen;   // its osAiGetLength
static u32 sBenchAudioAiUsed;
static u32 sBenchAudioTags[16]; // D of each batch in threadCmdProcQueueP, in queue order
static u32 sBenchAudioTagHead = 0;
static u32 sBenchAudioTagCount = 0;
static u32 sBenchAudioAiFifo[2]; // AI model: samples << 10 left in the playing and the queued buffer
static u32 sBenchAudioCount = 0x12345678;
static u32 sBenchAudioRandomTick;
static u32 sBenchAudioCmds[3]; // per AI buffer: commands processed in the frame that synthesized it
static u32 sBenchAudioCmdHash[3];
static s16 sBenchAudioNoise[0x2000];

static void Bench_AudioInit(void) {
    OSIntMask mask = __osDisableInt();
    u32 x = 0x9E3779B9;
    s32 i;

    if (!sBenchAudioReady) {
        osCreateMesgQueue(&sBenchAudioGate, sBenchAudioGateMsg, ARRAY_COUNT(sBenchAudioGateMsg));
        osCreateMesgQueue(&sBenchGraphGate, sBenchGraphGateMsg, ARRAY_COUNT(sBenchGraphGateMsg));
        osWritebackDCache(sBenchAudioRing, sizeof(sBenchAudioRing));
        osInvalDCache(sBenchAudioRing, sizeof(sBenchAudioRing));
        sBenchAudioFrames = (volatile BenchAudioFrame*)K0_TO_K1(sBenchAudioRing);
        gBenchAudio.ring = (BenchAudioFrame*)sBenchAudioFrames;
        gBenchAudio.layout = (sizeof(BenchAudioFrame) << 16) | BENCH_AUDIO_RING;
        for (i = 0; i < ARRAY_COUNT(sBenchAudioNoise); i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            sBenchAudioNoise[i] = x;
        }
        gBenchAudio.magic = BENCH_AUDIO_MAGIC;
        sBenchAudioReady = true;
    }
    __osRestoreInt(mask);
}

static s32 Bench_AudioPinned(u32 pin) {
    return (gBenchAudioConfig.pin & pin) != 0;
}

void Bench_AudioFrameBegin(void) {
    u32 frame = gBenchAudio.frames;
    s32 waited = false;

    Bench_AudioInit();
    if (Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS)) {
        while (!((frame < sBenchAudioNextDue) || (sBenchAudioGraphWaiting && (gAudioCtx.resetStatus != 0)))) {
            waited = true;
            osRecvMesg(&sBenchAudioGate, NULL, OS_MESG_BLOCK);
        }
        if (frame >= sBenchAudioNextDue) {
            gBenchAudio.resetFrames++;
        }
        if (waited) {
            gBenchAudio.audioWaits++;
        }
    }
    sBenchAudioFrame = frame;
    sBenchAudioAiLen = sBenchAudioAiUsed = 0;
    // AudioThread_UpdateImpl synthesizes into aiBuffers[(curAiBufferIndex + 1) % 3]
    sBenchAudioCmds[(gAudioCtx.curAiBufferIndex + 1) % 3] = 0;
    sBenchAudioCmdHash[(gAudioCtx.curAiBufferIndex + 1) % 3] = BENCH_FNV_OFFSET;
    gBenchAudio.frames = frame + 1;
}

void Bench_AudioFrameEnd(void) {
    // AudioThread_UpdateImpl itself takes these at the start of the next frame, after AudioMgr has sent
    // the task that reads them
    if (Bench_AudioPinned(BENCH_AUDIO_PIN_DMA)) {
        while (gAudioCtx.curAudioFrameDmaCount != 0) {
            osRecvMesg(&gAudioCtx.curAudioFrameDmaQueue, NULL, OS_MESG_BLOCK);
            gAudioCtx.curAudioFrameDmaCount--;
        }
    }
    sBenchAudioDone = sBenchAudioFrame + 1;
    osSendMesg(&sBenchGraphGate, NULL, OS_MESG_NOBLOCK);
}

void Bench_AudioUpdateBegin(GameState* gameState) {
    u32 due;
    s32 waited = false;

    Bench_AudioInit();
    if (!Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS)) {
        return;
    }
    due = sBenchAudioNextDue;
    sBenchAudioSendDue = due;
    while (sBenchAudioDone < due) {
        waited = true;
        osRecvMesg(&sBenchGraphGate, NULL, OS_MESG_BLOCK);
    }
    if (waited) {
        gBenchAudio.graphWaits++;
    }
    sBenchAudioRandomTick = gAudioCtx.audioRandom;
}

/**
 * audioRandom for game code that reads it outside Audio_Update (Link's metal footstep, code_8019AF00.c),
 * while audio frames run: with the pin, the value it had when this tick's Audio_Update began.
 */
u32 Bench_AudioGraphRandom(void) {
    return Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS) ? sBenchAudioRandomTick : gAudioCtx.audioRandom;
}

void Bench_AudioUpdateEnd(GameState* gameState) {
    u32 next;

    if (!Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS)) {
        return;
    }
    next = sBenchAudioSendDue + gameState->framerateDivisor;
    // Frames that ran during an audio heap reset wait
    if (next < gBenchAudio.frames) {
        next = gBenchAudio.frames;
    }
    sBenchAudioSendDue = next;
    sBenchAudioNextDue = next;
    osSendMesg(&sBenchAudioGate, NULL, OS_MESG_NOBLOCK);
}

void Bench_AudioGraphWait(s32 waiting) {
    sBenchAudioGraphWaiting = waiting;
    if (waiting) {
        osSendMesg(&sBenchAudioGate, NULL, OS_MESG_NOBLOCK);
    }
}

/**
 * Called after each send to threadCmdProcQueueP, sent or refused. The audio thread's own send (thread.c,
 * after AUDIOCMD_OP_GLOBAL_STOP_AUDIOCMDS) is due on its next frame.
 */
void Bench_AudioCmdsSent(s32 sent) {
    OSIntMask mask;

    if (!sent) {
        gBenchAudio.sendRefused++;
        return;
    }
    if (!Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS)) {
        return;
    }
    mask = __osDisableInt();
    if (sBenchAudioTagCount == ARRAY_COUNT(sBenchAudioTags)) {
        gBenchAudio.tagOverflow++;
    } else {
        sBenchAudioTags[(sBenchAudioTagHead + sBenchAudioTagCount) % ARRAY_COUNT(sBenchAudioTags)] =
            (osGetThreadId(NULL) == Z_THREAD_ID_AUDIOMGR) ? sBenchAudioFrame + 1 : sBenchAudioSendDue;
        sBenchAudioTagCount++;
    }
    __osRestoreInt(mask);
}

void Bench_AudioCmd(u32 opArgs, u32 data) {
    u32 i = gAudioCtx.curAiBufferIndex;

    sBenchAudioCmds[i]++;
    sBenchAudioCmdHash[i] = (((sBenchAudioCmdHash[i] ^ opArgs) * BENCH_FNV_PRIME) ^ data) * BENCH_FNV_PRIME;
}

/**
 * Whether the audio thread may take the next batch from threadCmdProcQueueP in this frame.
 */
s32 Bench_AudioCmdsDue(void) {
    OSIntMask mask;
    s32 due = false;

    if (!Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS)) {
        return true;
    }
    mask = __osDisableInt();
    if ((sBenchAudioTagCount != 0) && (sBenchAudioTags[sBenchAudioTagHead] <= sBenchAudioFrame)) {
        sBenchAudioTagHead = (sBenchAudioTagHead + 1) % ARRAY_COUNT(sBenchAudioTags);
        sBenchAudioTagCount--;
        due = true;
    }
    __osRestoreInt(mask);
    return due;
}

u32 Bench_AudioAiLength(u32 aiLen) {
    u32 used = aiLen;

    if (Bench_AudioPinned(BENCH_AUDIO_PIN_AI)) {
        u32 left = (u64)gAudioCtx.audioBufferParameters.aiSamplingFreq * gBenchConfig.retraceTicks * 1024 /
                   OS_CPU_COUNTER;

        while ((left != 0) && (sBenchAudioAiFifo[0] != 0)) {
            u32 take = MIN(left, sBenchAudioAiFifo[0]);

            sBenchAudioAiFifo[0] -= take;
            left -= take;
            if (sBenchAudioAiFifo[0] == 0) {
                sBenchAudioAiFifo[0] = sBenchAudioAiFifo[1];
                sBenchAudioAiFifo[1] = 0;
            }
        }
        // osAiGetLength's units: bytes of 16-bit stereo
        used = (sBenchAudioAiFifo[0] >> 10) * 4;
    }
    sBenchAudioAiLen = aiLen;
    sBenchAudioAiUsed = used;
    return used;
}

void Bench_AudioSubmit(void* buf, u32 size) {
    volatile BenchAudioFrame* rec;
    volatile u32* word = (volatile u32*)K0_TO_K1(buf);
    u32 hash = BENCH_FNV_OFFSET;
    u32 i;

    if (Bench_AudioPinned(BENCH_AUDIO_PIN_AI)) {
        if (sBenchAudioAiFifo[0] == 0) {
            sBenchAudioAiFifo[0] = (size / 4) << 10;
        } else if (sBenchAudioAiFifo[1] == 0) {
            sBenchAudioAiFifo[1] = (size / 4) << 10;
        } else {
            gBenchAudio.aiDropped++;
        }
    }
    if (!Bench_AudioPinned(BENCH_AUDIO_HASH)) {
        return;
    }
    for (i = 0; i < size / 4; i++) {
        hash = (hash ^ word[i]) * BENCH_FNV_PRIME;
    }
    rec = &sBenchAudioFrames[sBenchAudioFrame % BENCH_AUDIO_RING];
    rec->frame = BENCH_RECORD_WRITING;
    rec->retrace = sBenchFieldSeq;
    rec->tick = gBenchProbe.tick;
    rec->len = size;
    rec->hash = hash;
    rec->aiLen = sBenchAudioAiLen;
    rec->aiUsed = sBenchAudioAiUsed;
    rec->due = sBenchAudioNextDue;
    for (i = 0; i < ARRAY_COUNT(gAudioCtx.aiBuffers); i++) {
        if (gAudioCtx.aiBuffers[i] == buf) {
            rec->cmds = sBenchAudioCmds[i];
            rec->cmdHash = sBenchAudioCmdHash[i];
        }
    }
    rec->frame = sBenchAudioFrame;
}

static u32 Bench_AudioNext(void) {
    sBenchAudioCount = sBenchAudioCount * 1664525 + 1013904223;
    return sBenchAudioCount;
}

/**
 * audioRandom's update in AudioThread_UpdateImpl, without osGetCount and the AI buffer read.
 */
u32 Bench_AudioRandom(u32 random, u32 taskCount) {
    return (random + taskCount) * ((taskCount * 0x9E3779B9) | 1) + 0x6A09E667;
}

s16* Bench_AudioNoiseWave(u32 random) {
    return &sBenchAudioNoise[(random & 0xFF0) / sizeof(s16)];
}

u32 Bench_AudioCount(u32 count) {
    return Bench_AudioPinned(BENCH_AUDIO_PIN_RANDOM) ? Bench_AudioNext() : count;
}

s32 Bench_AudioPinLoads(void) {
    return Bench_AudioPinned(BENCH_AUDIO_PIN_LOADS);
}

/**
 * The settle tick's Audio_Update has ended: the next due frame is the first that can carry the window's
 * input.
 */
static void Bench_AudioSettle(void) {
    gBenchAudio.anchor = Bench_AudioPinned(BENCH_AUDIO_PIN_CMDS) ? sBenchAudioNextDue : gBenchAudio.frames;
}
