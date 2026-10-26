/**
 * Benchmark mode, see include/bench.h and tools/fps60/README.md.
 */
#include "global.h"
#include "bench.h"
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
    BENCH_CONFIG_MAGIC, 0, 0x12345678, BENCH_FLAG_PIN_EFFECT_BUDGET, 782715, 0, { { 0 } },
};

BenchProbe gBenchProbe;

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
