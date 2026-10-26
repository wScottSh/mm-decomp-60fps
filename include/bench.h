#ifndef BENCH_H
#define BENCH_H

/**
 * Benchmark mode. Only compiled when building with `make BENCH=1`; the matching build is unaffected.
 *
 * Boots straight into a fixed scene with a fixed RNG seed, drives controller 1 from a tick-keyed table,
 * and keeps load counters (CPU time per thread and per system, RSP and RDP busy) in RAM, where the
 * fps60 harness reads them between VI fields.
 *
 * See tools/fps60/README.md.
 */

#include "ultra64.h"
#include "scheduler.h"

struct GameState;
struct GraphicsContext;

#define BENCH_CONFIG_MAGIC 0x424E4348 // "BNCH"
#define BENCH_PROBE_MAGIC 0x50524F42  // "PROB"

#define BENCH_MAX_INPUTS 512

// BenchConfig.noisePin
#define BENCH_NOISE_PIN_OFF 0   // retail rendering (perf runs)
#define BENCH_NOISE_PIN_ON 1    // Bench_PinNoise takes RDP timing out of every gfx task's pixels (fidelity runs)
#define BENCH_NOISE_PIN_CHECK 2 // as ON, and every task is walked a second time to check the walk is idempotent

// BenchConfig.flags
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
 * Run configuration. tools/fps60/run.py finds this in a copy of the ROM by its magic and patches it,
 * so changing scenes doesn't need a rebuild.
 */
typedef struct BenchConfig {
    u32 magic;
    u32 scene;
    u32 seed;
    u32 flags;
    u32 retraceTicks; // the gIrqMgrRetraceTime that the pinned effect budget uses
    u32 inputCount;   // clamped to BENCH_MAX_INPUTS
    BenchInput inputs[BENCH_MAX_INPUTS];
    u32 noisePin;     // BENCH_NOISE_PIN_*
} BenchConfig;

extern BenchConfig gBenchConfig;

#define BENCH_THREAD_SLOTS 32 // thread ids are masked to this; MM's highest is Z_THREAD_ID_IRQMGR (19)

/**
 * CPU time per thread, in PClocks (93.75 MHz), kept by Bench_EnqueueAndYield (src/boot/idle.c) at every
 * thread switch that libultra's C code requests. A thread that an interrupt preempts is charged until
 * the woken thread resumes in its own yield, so time is charged once, to the thread that ran. The
 * exception handler's time goes to the thread that was running when it was entered.
 */
typedef struct BenchCpuClock {
    /* 0x00 */ u32 enabled;
    /* 0x04 */ u32 last;     // osGetCount() at the last switch
    /* 0x08 */ u32 current;  // the id of the thread that has run since `last`
    /* 0x0C */ u32 switches; // calls to Bench_CpuSwitch
    /* 0x10 */ u32 thread[BENCH_THREAD_SLOTS];
} BenchCpuClock;

extern BenchCpuClock gBenchCpuClock;

void Bench_EnqueueAndYield(OSThread** queue);
void Bench_CpuSwitch(u32 nextId);

/**
 * Cumulative load counters as the scheduler saw them at one VI retrace. CPU times are PClocks, RSP
 * times are the emulator's 750 MHz units (emux XPROFREAD 0x0200), RDP counters are RCP clocks.
 */
typedef struct BenchField {
    /* 0x00 */ u32 seq;      // retraces since boot
    /* 0x04 */ u32 count;    // osGetCount()
    /* 0x08 */ u32 switches; // thread switches (BenchCpuClock.switches)
    /* 0x0C */ u32 idle;
    /* 0x10 */ u32 graph;
    /* 0x14 */ u32 audio;    // the AudioMgr thread, which runs AudioThread_Update
    /* 0x18 */ u32 rsp;      // non-halted RSP time
    /* 0x1C */ u32 rspAudio; // non-halted RSP time inside audio tasks
    /* 0x20 */ u32 rdpPipe;  // DPC_PIPEBUSY
    /* 0x24 */ u32 rdpBuf;   // DPC_BUFBUSY
} BenchField;

#define BENCH_FIELD_RING 4
#define BENCH_RECORD_WRITING 0xFFFFFFFF // BenchField.seq and BenchTick.tick while the record is written

typedef enum BenchGraphPhase {
    BENCH_PHASE_PRE,      // Graph_Update start to GameState_Update: input, Audio_Update
    BENCH_PHASE_UPDATE,   // Play_Update
    BENCH_PHASE_DRAW,     // Play_Draw
    BENCH_PHASE_GAME,     // the rest of GameState_Update (all of it outside Play)
    BENCH_PHASE_POST,     // display list close and task setup, before the RCP wait
    BENCH_PHASE_RCP_WAIT, // Graph_TaskSet00 blocked on the previous gfx task
    BENCH_PHASE_TAIL,     // after the wait until the next Graph_Update
    BENCH_PHASE_MAX
} BenchGraphPhase;

/**
 * Graph thread systems. Time is exclusive: the graph thread's time always goes to exactly one system,
 * the innermost entered, so the systems of a tick sum to its graph thread time.
 */
typedef enum BenchSystem {
    BENCH_SYS_OTHER,
    BENCH_SYS_ACTOR_UPDATE, // Actor_UpdateAll
    BENCH_SYS_ACTOR_DRAW,   // Actor_DrawAll without effects
    BENCH_SYS_COLLISION,    // CollisionCheck_AT, _OC, _Damage, _ClearContext
    BENCH_SYS_ROOM_DRAW,    // Scene_Draw, Room_Draw
    BENCH_SYS_SKYBOX,       // Skybox_Update, _Draw, _UpdateMatrix
    BENCH_SYS_ENVIRONMENT,  // z_kankyo: Environment_Update and its draws
    BENCH_SYS_CAMERA,       // Camera_Update
    BENCH_SYS_MESSAGE,      // Message_Update, _Draw, Interface_Update, _Draw
    BENCH_SYS_EFFECT,       // Effect_UpdateAll, _DrawAll, EffectSs_UpdateAll, _DrawAll
    BENCH_SYS_AUDIO_UPDATE, // Audio_Update
    BENCH_SYS_BENCH,        // this file's bookkeeping in Bench_BeginFrame
    BENCH_SYS_MAX
} BenchSystem;

/**
 * One logic tick: from its Graph_Update's start to the next one's. Times are PClocks except as noted
 * for BenchField.
 */
typedef struct BenchTick {
    /* 0x00 */ u32 tick;
    /* 0x04 */ u32 start; // osGetCount() at Graph_Update start
    /* 0x08 */ u32 wall;
    /* 0x0C */ u32 switches;
    /* 0x10 */ u32 idle;
    /* 0x14 */ u32 graph;
    /* 0x18 */ u32 audio;
    /* 0x1C */ u32 sched;
    /* 0x20 */ u32 irqmgr;
    /* 0x24 */ u32 padmgr;
    /* 0x28 */ u32 dmamgr;
    /* 0x2C */ u32 rsp;
    /* 0x30 */ u32 rspAudio;
    /* 0x34 */ u32 rdpPipe;
    /* 0x38 */ u32 rdpBuf;
    /* 0x3C */ u32 clockReads; // Bench_CpuNow calls, the cost of the per-system hooks
    /* 0x40 */ u32 phaseWall[BENCH_PHASE_MAX];
    /* 0x5C */ u32 phaseCpu[BENCH_PHASE_MAX];
    /* 0x78 */ u32 sys[BENCH_SYS_MAX];
} BenchTick; // size = 0xA8

#define BENCH_TICK_RING 640

/**
 * The noise pin's walk of one gfx task's display lists (Bench_PinNoise, src/code/bench.c). A noise command
 * is one that writes a field the pin rewrites, whatever value it writes: every G_RDPSETOTHERMODE and
 * G_SETCOMBINE, a G_SETOTHERMODE_H that covers the color dither, alpha dither or pipeline mode bits, and a
 * G_SETOTHERMODE_L that covers the alpha compare bits.
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
    /* 0x2C */ BenchField* fields;    // a ring of BENCH_FIELD_RING retraces, retrace N at N % BENCH_FIELD_RING
    /* 0x30 */ BenchTick* ticks;      // a ring of BENCH_TICK_RING ticks, tick N at N % BENCH_TICK_RING
    /* 0x34 */ u32 tickCount;   // BenchTick records written
    /* 0x38 */ u32* actorTime;  // per actor id: [0, ACTOR_ID_MAX) update, [ACTOR_ID_MAX, 2 * ACTOR_ID_MAX) draw
    /* 0x3C */ u32 layout;      // sizeof(BenchField) << 24 | BENCH_FIELD_RING << 20 | sizeof(BenchTick) << 10 | BENCH_TICK_RING
    /* 0x40 */ u32 actorIds;    // ACTOR_ID_MAX
    /* 0x44 */ u32 clockReadCost; // PClocks per Bench_CpuNow call, timed at boot
    /* 0x48 */ BenchNoiseRecord cfbNoise[3]; // that tick's noise-pin walk, zero with the pin off
    /* 0x78 */ BenchNoiseTotals noise;
} BenchProbe;

extern BenchProbe gBenchProbe;

s32 Bench_Boot(struct GameState* gameState);
void Bench_BeginFrame(struct GameState* gameState);
void Bench_EndFrame(struct GraphicsContext* gfxCtx, struct GameState* gameState);
void Bench_TagCfb(CfbInfo* cfb);
void Bench_PinNoise(CfbInfo* cfb, Gfx* taskList);
void Bench_ApplyInput(struct GameState* gameState);

u32 Bench_CpuNow(void);
void Bench_Phase(s32 phase);
void Bench_SysEnter(s32 system);
void Bench_SysExit(void);
void Bench_ActorBegin(void);
void Bench_ActorEnd(s32 actorId, s32 draw);
void Bench_FieldSnapshot(void);
void Bench_RspAudioStart(void);
void Bench_RspAudioStop(void);

// bench_emux.s
s32 Bench_EmuxDetect(void);
void Bench_XProfStart(u32 slot);
void Bench_XProfStop(u32 slot);
void Bench_XProfClear(u32 slot);
u32 Bench_XProfRead(s32 slot, u32 metric);
void Bench_EmuxExit(void);

// emux XPROFREAD metric ids (ares/n64/cpu/emux.cpp); slot -1 reads the totals since power-on
#define XPROF_RSP_CYCLES 0x0200     // non-halted RSP time, 750 MHz units

/**
 * Audio fidelity (F4, tools/fps60/README.md). Every buffer the audio thread hands to the AI is hashed as
 * RDRAM holds it, which is what the AI's DMA plays. The audio pin makes that PCM a function of the game
 * alone: each pinned path replaces a value that audio-thread timing would otherwise feed into the output.
 * run.py finds gBenchAudioConfig in the ROM by its magic and patches `pin`.
 */
#define BENCH_AUDIO_MAGIC 0x41554449 // "AUDI"

// BenchAudioConfig.pin
#define BENCH_AUDIO_HASH (1 << 0)        // record every AI buffer's hash
#define BENCH_AUDIO_PIN_CMDS (1 << 1)    // lockstep: tick N's audio commands reach a fixed audio frame
#define BENCH_AUDIO_PIN_AI (1 << 2)      // osAiGetLength comes from a model of the AI, not the clock
#define BENCH_AUDIO_PIN_RANDOM (1 << 3)  // audio randomness and the noise wave do not read osGetCount or code
#define BENCH_AUDIO_PIN_LOADS (1 << 4)   // an async load's chunk is done on the frame after it was started
#define BENCH_AUDIO_PIN_DMA (1 << 5)     // a frame's sample DMAs are done before its RSP task can start

typedef struct BenchAudioConfig {
    u32 magic;
    u32 pin; // BENCH_AUDIO_*
} BenchAudioConfig;

extern BenchAudioConfig gBenchAudioConfig;

typedef struct BenchAudioFrame {
    /* 0x00 */ u32 frame;   // AudioThread_Update calls before this one; BENCH_RECORD_WRITING while written
    /* 0x04 */ u32 retrace; // VI retraces since boot (BenchField.seq)
    /* 0x08 */ u32 tick;    // gBenchProbe.tick
    /* 0x0C */ u32 len;     // bytes handed to osAiSetNextBuffer
    /* 0x10 */ u32 hash;    // FNV-1a over the buffer's words
    /* 0x14 */ u32 aiLen;   // osAiGetLength() this frame
    /* 0x18 */ u32 aiUsed;  // the length the frame computed its size from (the model's with the AI pin)
    /* 0x1C */ u32 due;     // with BENCH_AUDIO_PIN_CMDS, frames before this one may run
    /* 0x20 */ u32 cmds;    // audio commands processed two frames before, when this buffer was synthesized
    /* 0x24 */ u32 cmdHash; // FNV-1a over their two words
} BenchAudioFrame; // size = 0x28

#define BENCH_AUDIO_RING 1024

/**
 * What the harness reads after the window (run.py, AUDIO_*). Frame N's record is ring entry
 * N % BENCH_AUDIO_RING, written only for frames that hand the AI a buffer.
 */
typedef struct BenchAudio {
    /* 0x00 */ u32 magic;
    /* 0x04 */ u32 frames;       // AudioThread_Update calls started
    /* 0x08 */ u32 anchor;       // with BENCH_AUDIO_PIN_CMDS, the due frame of tick settleTick + 1
    /* 0x0C */ BenchAudioFrame* ring;
    /* 0x10 */ u32 layout;       // sizeof(BenchAudioFrame) << 16 | BENCH_AUDIO_RING
    /* 0x14 */ u32 sendRefused;   // command batches the full queue refused (the game merges them)
    /* 0x18 */ u32 tagOverflow;  // batches sent while the tag ring was full
    /* 0x1C */ u32 graphWaits;   // ticks whose Audio_Update waited for the audio thread
    /* 0x20 */ u32 audioWaits;   // frames that waited for a tick's Audio_Update
    /* 0x24 */ u32 resetFrames;  // frames run while the graph thread waited on an audio heap reset
    /* 0x28 */ u32 aiDropped;    // buffers the AI model had no room for
} BenchAudio;

extern BenchAudio gBenchAudio;

void Bench_AudioFrameBegin(void);
void Bench_AudioFrameEnd(void);
u32 Bench_AudioAiLength(u32 aiLen);
void Bench_AudioSubmit(void* buf, u32 size);
s32 Bench_AudioCmdsDue(void);
void Bench_AudioCmdsSent(s32 sent);
void Bench_AudioCmd(u32 opArgs, u32 data);
u32 Bench_AudioRandom(u32 random, u32 taskCount);
s16* Bench_AudioNoiseWave(u32 random);
u32 Bench_AudioCount(u32 count);
s32 Bench_AudioPinLoads(void);
void Bench_AudioUpdateBegin(struct GameState* gameState);
void Bench_AudioUpdateEnd(struct GameState* gameState);
void Bench_AudioGraphWait(s32 waiting);

#endif
