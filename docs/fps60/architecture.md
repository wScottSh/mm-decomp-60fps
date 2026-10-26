# MM 60 fps architecture: Replay

This document is the design for a Majora's Mask build that presents a new image on every NTSC VI field while game logic keeps its retail tick and stays bit-identical to retail. It is the output of unit A1 of the 60 fps program. Units P2-ref and the P2 hazard units implement it. Units B1 and P3 use its load projection to measure progress before P2-ref lands.

Read this first if you are about to change `graph.c`, `sched.c`, `sys_matrix.c`, or any code that emits framebuffer addresses into a display list.

## How to read this document

- **Labels.** Every number is **cited** (read from code or a document), **measured** (run on the pinned runner, sha256 prefix `1f9d59f6`), or **inferred** (derived, not run).
- **Citations.** `file:line` refers to this repo at `301c734cf` (origin/main when A1 started). `H1a:` marks a file read from branch `origin/harness/H1a` at `f8db2ebd2` (unit H1a, not merged when this was written). R1 is `docs/research/fps60-techniques.md` (branch `origin/research/R1`, PR 4). R2 is `docs/research/mm-frame-pipeline.md`.
- **Standing orders.** "Preferences line N" refers to the program's standing orders, `~/.claude/orchestrate/mm-60fps/preferences.md`. F1, F2 and F3 are defined in its line 4. Line 20 refines them for RDP noise: every fidelity run uses the BENCH noise pin (unit H1c) in reference and candidate, and F1 and F2 also compare a per-tick hash of every noise-related display-list command as the game emitted it. Every pixel claim in this document is under that pin.
- **Terms.** A *tick* is one logic update (one `Graph_Update`). The *divisor* `d` is the tick's `framerateDivisor`. A tick presents `d` *images*, one per field. Image `d` is the *keyframe* (K). Images 1 to `d-1` are *interpolated images* (I). `S_k` is the retrace at which tick k starts.

## Summary

- **Draw runs once, the display list runs d times.** Each tick runs `Play_Update` and `Play_Draw` exactly once, unmodified. The RCP renders the resulting display list `d` times. Before each interpolated render, the scheduler overwrites the matrices in the tick's gfx pool with values blended between the previous tick and this one. Before the keyframe render, it writes back retail's bytes. No game draw code runs twice, so every draw-time hazard in R2 section 4, including the 231 non-idempotent functions in 135 overlays, has no effect by construction.
- **The keyframe is retail's command stream in retail's framebuffer.** It renders the bytes retail's code wrote, into the framebuffer retail chose, which holds the same prior contents retail's did. A lerp is never evaluated at t = 1.
- **Ticks start on a retrace at a fixed phase.** At divisor 3 the first image of tick k shows 4 fields after its input read and the keyframe shows at 6. Retail's measured minimum is 6, so F3 holds by construction.
- **The CPU problem nearly disappears. The RCP problem stays.** The CPU builds one display list per tick plus a matrix patch per image, about 1.7 ms per tick extra at 187 matrices (inferred). The RSP and RDP render a full retail-size list on every field, so per field they need what retail needs per tick. Each tick's first image needs its whole RCP task time `W_t` within one field: 26.1 to 34.2 ms today against a 15.0 ms budget, a 1.7x to 2.3x cut (cited medians from the older fork, inferred ratio). Closing that gap is P3's job, and B1 measures the RDP side on the pinned runner.
- **New memory is about 316 KiB**, mostly two extra framebuffers. Termina Field keeps about 379 KiB of ZeldaArena free on a BENCH build (inferred).
- **Fidelity runs use the H1c noise pin** (preferences line 20). Command streams are timing-invariant, and pixels are timing-invariant only under that pin.
- **Five gates** need a product call. Each has a default that applies until Scott rules. See [Gates](#gates).

## Measurements taken for this design

All on the pinned runner (sha256 `1f9d59f6`), using a `BENCH=1` build of `origin/harness/H1a` at `f8db2ebd2` and its six-scene corpus, 600 fields per scene. Commands are in the A1 report.

| Quantity | title | filesel | sct | mountain | field | greatbay |
|---|---|---|---|---|---|---|
| Fields from a tick's input read to its first displayed field | 7 (all) | 1 to 4 (mostly 2) | 7, and 6 on 20 of 197 ticks | 7 (all) | 7 (all) | 7 (all) |
| Images held 1 / 2 / 3 fields | 0 / 0 / 198 | 341 / 128 / 0 | 0 / 0 / 198 | 0 / 0 / 198 | 0 / 0 / 198 | 0 / 0 / 198 |
| Matrices per tick, median / max (`Matrix_MtxFToMtx` calls) | 26 / 26 | 2 / 2 | 113 / 134 | 41 / 49 | 52 / 65 | 187 / 193 |
| Skin matrices per tick, max (`SkinMatrix_MtxFToNewMtx` calls) | 0 | 0 | 11 | 1 | 15 | 10 |

The first row is the F3 bound. It comes from H1a's per-field `tick` (latest tick started) and `image_tick` (tick whose image is shown) columns. The matrix counts come from a throwaway probe: a counter in each function, published through a BENCH probe field. It was not committed. Retail file select (divisor 1) already fails to present every field. That is a perf problem for P3 in a divisor-1 state, not an interpolation problem.

## Retail pipeline, in one table

Verified this session against the code (cited unless marked):

| Step | Where |
|---|---|
| A tick reads input, increments frames, runs `Audio_Update` | `src/code/graph.c:256-262` |
| `Play_Main` runs `Play_Update`, `Play_Draw` (controller 1 blanked), `CutsceneManager_Update` | `src/code/z_play.c:1490-1519` |
| After building, `Graph_TaskSet00` blocks for the previous task's done message | `src/code/graph.c:159-160` |
| It then runs `gfxCtx->callback`, which reads the Z-buffer at the sun and glow points. That Z-buffer is the previous task's, at the screen positions this tick's draw computed | `src/code/graph.c:188-190`, `src/code/z_kankyo.c:532-537` |
| The task gets a `CfbInfo` with `updateRate = framerateDivisor` | `src/code/graph.c:219-243` |
| A gfx task that needs the RDP starts only when RSP and RDP are idle, no swap is pending, and its framebuffer is not on screen | `src/code/sched.c:243-253`, `286-294` |
| The scheduler writes back the data cache before each non-audio task load | `src/code/sched.c:354-356` |
| `osViSwapBuffer` only marks the next buffer. The VI manager writes `VI_ORIGIN` at the next VI interrupt | `src/libultra/io/viswapbuf.c`, `src/libultra/io/vimgr.c:82`, `src/libultra/io/viswapcontext.c:47` |
| `gRDPTimeAcc` is set when the RDP finishes, as RDP done minus RSP start | `src/code/sched.c:390`, `529` |

So in retail, tick k+1 is read and built while task k waits for its framebuffer and renders. The measured result is 7 fields from input read to first display (6 on a CPU-heavy tick).

## Design

### Data shape

The core shape is "one display list, d patch sets". Per tick, the graph thread records a short table of what differs between images. The scheduler applies one patch set before each render.

```c
/* include/replay.h: the public surface. */

typedef u32 ReplayOwner;            // kind in bits 31..24, id in 23..0
typedef enum ReplayOwnerKind {
    REPLAY_OWNER_DEFAULT = 0,       // matched by order inside the default scope
    REPLAY_OWNER_ACTOR = 1,         // id: spawn serial, never reused within a PlayState
    REPLAY_OWNER_CAMERA = 2,        // id: camera index
    REPLAY_OWNER_SKYBOX = 3,
    REPLAY_OWNER_EFFECT_SS = 4,     // P2-fx: id = slot | epoch << 8
    REPLAY_OWNER_KIND_COUNT
} ReplayOwnerKind;
#define REPLAY_OWNER(kind, id) ((ReplayOwner)(((u32)(kind) << 24) | ((u32)(id) & 0xFFFFFF)))

void Replay_WaitTickStart(GameState* state);                // graph.c: block until S_k
void Replay_SubmitTick(GraphicsContext* gfxCtx, GameState* state); // replaces Graph_TaskSet00
void Replay_RecordMtx(Mtx* dst);                            // matrix choke points
void Replay_PushOwner(ReplayOwner owner);
void Replay_PopOwner(void);
void Replay_RecordFbWord(Gfx* cmd);                         // w1 holds the keyframe FB address
void Replay_RecordInterpEnd(Gfx* cmd);                      // interpolated renders stop here
ReplayOwner Replay_ActorOwner(Actor* actor);
void Replay_OnActorSpawn(Actor* actor);
void Replay_OnActorDestroy(Actor* actor);
void Replay_OnTaskStart(OSScTask* task);                    // sched.c only
```

```c
/* src/code/replay.c: private. */

#define REPLAY_MAX_MTX 384          // per tick. Measured max 193 + 15 skin
#define REPLAY_MAX_GFX 32           // FB words and interp ends per tick
#define REPLAY_MAX_IMAGES 3         // divisor <= 3

typedef struct ReplayMtxRecord {
    /* 0x0 */ Mtx* dst;             // inside this tick's gfx pool, checked at record time
    /* 0x4 */ ReplayOwner owner;
    /* 0x8 */ u16 ordinal;          // nth matrix of this owner this tick
    /* 0xA */ u16 flags;            // REPLAY_MTX_PATCHED
} ReplayMtxRecord;                  // size = 0xC

typedef struct ReplayGfxRecord {
    /* 0x0 */ Gfx* dst;
    /* 0x4 */ u32 kind;             // REPLAY_GFX_FB_WORD or REPLAY_GFX_INTERP_END
    /* 0x8 */ Gfx retail;           // the command as built, restored for the keyframe
} ReplayGfxRecord;                  // size = 0x10

/** One per gfx pool. Graph thread writes it until submit, scheduler reads it after. */
typedef struct ReplayRecording {
    u32 tick;
    u16 numMtx, numGfx, numStaged;
    u8 divisor;
    u8 flags;                       // REPLAY_REC_SNAP_ALL, REPLAY_REC_OVERFLOW
    Mtx* retailCopy;                // in the pool's free POLY_OPA gap: retail bits of record i
    Mtx* interp[REPLAY_MAX_IMAGES - 1]; // in the gap: image j's values
    ReplayMtxRecord mtx[REPLAY_MAX_MTX];
    ReplayGfxRecord gfx[REPLAY_MAX_GFX];
} ReplayRecording;                  // 5,144 B

typedef struct ReplayImage {
    OSScTask task;                  // data_ptr = the tick's master list, same for every image
    u16* framebuffer;               // keyframe: retail's FB. Interpolated: gReplayInterpFb[n]
    s32 slotRetrace;                // earliest retrace at which it may be shown
    u8 index;                       // 1..d
    u8 isKeyframe;
    u8 state;                       // queued, running, done, shown
    u8 cfbIndex;                    // sGraphCfbInfos entry bound at task start
} ReplayImage;

typedef struct ReplayTick {
    ReplayRecording* rec;
    s32 startRetrace;               // S_k
    s32 windowRetrace;              // W_k = S_k + B_k + 1, the first display field
    u8 divisor, phase, numImages;
    u8 flags;                       // REPLAY_TICK_NO_SWAP, REPLAY_TICK_KEYFRAME_ONLY
    ReplayImage images[REPLAY_MAX_IMAGES];
} ReplayTick;
```

Pure functions, testable on the host:

```c
/** out = a + trunc((b - a) * num / den) per element, without 64-bit math. False on s32 overflow. */
s32 Replay_LerpMtx(Mtx* out, const Mtx* a, const Mtx* b, s32 num, s32 den);
/** B_k: fields between a tick's start retrace and the retrace before its first image. */
u8 Replay_PhaseFor(const GameState* state, u8 divisor);
/** S_{k+1} = S_k + B_k + d_k - B_{k+1}, with B_{k+1} = PhaseFor(d_k). Keeps W_{k+1} = W_k + d_k. */
s32 Replay_NextTickStart(s32 start, u8 phase, u8 divisor, u8 nextPhase);
```

### Call sites

| Site | Change |
|---|---|
| `Graph_Update` (`src/code/graph.c`) | `Replay_WaitTickStart` before input is read. `Replay_SubmitTick` replaces `Graph_TaskSet00`. |
| `Matrix_MtxFToMtx` (`src/code/sys_matrix.c:1159`) | `Replay_RecordMtx(dest)` as the last statement. `Matrix_ToMtx`, `Matrix_Finalize` and `Matrix_MtxFToNewMtx` all call it (`:1243`, `:1256`, `:1271`), so skeleton limb matrices are covered. |
| `SkinMatrix_MtxFToMtx` (`src/code/z_skin_matrix.c:512`) | Same. |
| `gu*` calls in `src/code/z_view.c` (`341`, `359`, `392`, `436`, `486`, `501`, `517`, `544`) | `Replay_RecordMtx` after each. Persistent destinations fail the pool check and are skipped. |
| `Actor_Draw` (`src/code/z_actor.c:2795`), `View_Apply*`, skybox draw | Push and pop an owner. |
| Actor spawn and destroy | Fill and clear the serial side table. `Actor` has no padding (`include/z64actor.h:156`, size 0x144), so the serial lives outside it. |
| `GameState_SetFrameBuffer` (`src/code/game.c:124-128`), `syncSegments` (`src/code/z_rcp.c:1528-1536`), PreRender color-image and texture-image words that carry `this->fbuf` (`src/code/PreRender.c:78`, `114`, `203`) | `Replay_RecordFbWord`. |
| Motion blur save (`src/code/z_play.c:93`) | `Replay_RecordInterpEnd` at the first save command. |
| `Sched_RunTask` (`src/code/sched.c:354-356`) | `Replay_OnTaskStart(spTask)` before the retail `osWritebackDCacheAll`, on a first start only (not a yield resume). |

### Flow of one tick

1. The scheduler posts `TickStart(k)` at retrace `S_k`. Pool `k % 2` is free by then, because tick k-2's keyframe has rendered.
2. The graph thread reads input and runs logic and draw as retail. Each matrix written into the pool is recorded with its owner and ordinal. A destination outside the active pool's POLY_OPA buffer is dropped. This is the module's only validation, per **boundary-discipline**.
3. `Replay_SubmitTick` closes the recording, copies each recorded `Mtx` into `retailCopy` in the pool's free gap, matches records against tick k-1's recording by `(owner, ordinal)`, and computes the `d-1` interpolated copies into the gap. It then raises the graph thread to priority 12, waits for keyframe k-1 to finish, runs `gfxCtx->callback`, copies the RSP, RDP and period accumulators to their totals, submits `d` images, and drops back to priority 9.
4. The scheduler starts each image as an ordinary gfx task. `Replay_OnTaskStart` writes that image's patch set. Image j copies `interp[j-1]` to each patched `dst`, rewrites each FB word from the keyframe framebuffer to its own, and writes `gSPEndDisplayList` at each interp end. The keyframe copies `retailCopy` back and restores every recorded `Gfx`. The retail cache writeback that follows flushes the patch.
5. The presenter shows images in order, never drops one, and binds a `sGraphCfbInfos` entry when an image starts.

## Q1. Draw and logic split

**One pass, and it is retail.** `Graph_Update` runs `Graph_UpdateGame` and `GameState_Update` once per tick, unchanged (`src/code/graph.c:256-262`, `277`). `Play_Main` keeps retail order (`src/code/z_play.c:1490-1519`). So each item in R2's hazard table runs exactly as often as in retail:

| Hazard | Why it stays retail | Source |
|---|---|---|
| Textbox and ocarina state | `Message_Draw` runs once per tick | R2 section 4, interface table |
| HUD timers, moon crash | `Interface_DrawTimers` runs once per tick | `src/code/z_parameter.c:5663-6000` via R2 |
| `Camera_Update` in draw | Runs once, when `view.unk164` is set | `src/code/z_play.c:1433-1439` |
| RNG in draw | Same draw count, same stream | R2 sections 4 and 5 |
| Player colliders, leg IK | Limb callbacks run once | `src/code/z_player_lib.c:2162-2166`, `2694-2748` via R2 |
| Cull flags, `isDrawn`, `projectedPos` | Written once by the one draw | `src/code/z_actor.c:3190-3203` via R2 |
| Skin parity | Flips once per tick | `src/code/z_skin.c:132` |
| SFX and spawns in draw | Issued once | R2 overlay table |
| 135 overlays, 2,798 scan hits | No per-site work | R2 section 4, `reports/R2-tools/overlay-hits.tsv` |

**How the keyframe stays retail's exact list.** The pool bytes are what retail's code wrote. Before the keyframe render, the scheduler restores every patched `Mtx` from `retailCopy`, a byte copy taken after the build, and every patched `Gfx` from its saved command. The keyframe renders into `gfxCtx->curFrameBuffer`, chosen by retail's `framebufferIndex % 2` logic including the `SREG(33)` rewind (`src/code/graph.c:74`, `212-216`). Interpolated images go to two extra framebuffers, so the keyframe's target still holds tick k-2's keyframe, as in retail. That matters because Play skips the color clear when a skybox or an image room draws (`src/code/z_play.c:1147-1152`), so pixels the list does not cover keep the buffer's old contents.

**Framebuffer feedback.** An interpolated render must not write a buffer a later render reads.

| Site | What it writes | Treatment |
|---|---|---|
| Motion blur save, `PreRender_SaveFramebuffer` (`src/code/z_play.c:93`) | `unk_18E64`, read by the next tick's blend (`src/code/z_play.c:88-90`) | Interp end at the first save command. Interpolated images blend the previous keyframe's save. The keyframe blends, then saves, as retail. |
| Motion blur blend `func_80170AE0` | Draws into `this->fbuf`, a direct address of the keyframe FB (`src/code/z_play.c:84`) | FB words on PreRender's color-image and texture-image commands. |
| Pause, picto and tile captures (`src/code/z_play.c:1387-1421`) | Z-buffer, `unk_18E58` | These ticks set `SREG(33)`. Replay gives them one render and no swap, which is retail. |
| Lens of truth PreRender (`src/code/z_actor.c:2951`) | Reads and writes `curFrameBuffer` inside one render | FB words. Same-render feedback is safe once retargeted. |
| VisMono, VisFbuf, wipe5 (`src/code/z_vismono.c:76`, `src/code/z_visfbuf.c:318-322`, `src/overlays/fbdemos/ovl_fbdemo_wipe5/z_fbdemo_wipe5.c:101-117`) | Address the FB through segment 0x0F | Follow the segment. Whether wipe5's work-buffer copy crosses a tick is not verified (P2-fb). |

**Post-task reads stay in retail's position.** `Replay_SubmitTick(k)` waits for keyframe k-1, then runs the graph callback, then submits tick k's first image. So the callback reads keyframe k-1's Z at the sun and glow points that tick k's draw computed, exactly as retail reads task k-1's Z at tick k's points. `Sched_FlushTaskQueue` queues a null task behind all queued tasks (`src/code/z_nulltask.c:8-29`). Replay images are ordinary queued tasks, so the picto, pause and hi-res flushes wait for every image of the previous tick, as retail waits for its task.

## Q2. Interpolation

**What P2-ref interpolates.** Every `Mtx` in the active gfx pool written through the choke points in [Call sites](#call-sites). That is model, limb, skin-limb, billboard (`src/code/z_play.c:1206-1210`), projection and viewing matrices. Measured counts are 2 to 208 per tick (see the measurement table).

**What P2-ref does not interpolate.** These show the tick's own value on all `d` images, as retail shows it for the whole tick: texrects (HUD, stars, textbox), texture scroll words, CPU-written vertices (skin `Vtx`, sword trails), light structs, fog and combiner colors, and `gu*` matrices outside `z_view.c`. Hazard units add each through `ReplayGfxRecord` or a new record kind with the same apply-and-restore cycle.

**Identity.** A matrix's key is `(owner, ordinal)`.

- `Actor_Draw` pushes `REPLAY_OWNER(ACTOR, serial)`. The serial comes from a side table filled at spawn and cleared at destroy, from a counter that never repeats within a PlayState. A new actor in freed memory never pairs with the old one. This is Zelda64Recomp's scheme (R1 appendix B section 3) without changing `Actor`'s layout.
- `View_Apply*` pushes the camera. Skybox draws push `SKYBOX`.
- Everything else is `DEFAULT`, matched by order.

**Fallback is always retail.** A record that does not match renders retail bits on every image. That is what retail shows on those fields, so a fallback can cost smoothness but never fidelity. RT64 uses the same rule (R1 appendix B section 2, item 6).

| Case | P2-ref rule |
|---|---|
| Spawn | No previous key, so it snaps. |
| Despawn | Absent from the list from image 1. Retail also drops it on the tick's first field. |
| An owner's matrix count changed (skeleton swap, conditional limb, held item) | The whole owner snaps. |
| First tick of a game state, scene or room change (`roomCtx.curRoom.num` differs), active camera index change, first tick after a no-swap tick, previous recording not resident | `REPLAY_REC_SNAP_ALL`: every record snaps. |
| Matrix table overflow or pool-gap shortfall | The tail matrix records snap and a counter increments. |
| Gfx table overflow (FB words, interp ends) | The whole tick becomes keyframe-only. A partial snap would let an interpolated render write the keyframe framebuffer. |
| Actor serial table full | That actor gets no serial, and its matrices snap every tick. |
| One destination recorded twice (`View_UpdateViewingMatrix` re-runs `guLookAt` on the pool viewing matrix, `src/code/z_view.c:517`, and `Skybox_UpdateMatrix` rewrites the skybox matrix, `src/code/z_play.c:1438`, when `view.unk164` is set) | Records are deduplicated by destination. The first owner keeps the record, and `retailCopy` takes the final bytes. Without this, DEFAULT's count would change on those ticks and every DEFAULT matrix would snap. |
| Camera cut within one camera, teleport, effect slot reuse, turns near 180 degrees | P2-cam, P2-actor and P2-fx (R1 hazard table). P2-ref lerps them, which can smear an interpolated image but never a keyframe. |

**Math.** For each of the 16 s15.16 elements, with `a` from tick k-1's `retailCopy` and `b` from tick k's:

```
delta = b - a                      (s32; on overflow the record snaps)
q = delta / d, r = delta - q*d     (C truncation, |r| < d <= 3)
out_j = a + q*j + (r*j)/d          for j = 1 .. d-1
```

This equals `a + trunc(delta*j/d)` (inferred: `q*j` is exact and `r*j/d` truncates toward zero with the sign of `delta`). The result is split into the integer and fraction halves of the `Mtx` layout that `Matrix_MtxFToMtx` writes (`src/code/sys_matrix.c:1160-1230`). Keyframes are bit-exact because they are never computed. Interpolated values are reproducible because integer math has no rounding mode and depends only on the two recordings and `j/d`. Component lerp shrinks a rotating matrix mid-turn, as in both PC ports (R1 appendix A section 1). P2-actor replaces actor root matrices with an actor-relative lerp of position and angles.

## Q3. Schedule

**Threads** (MM's priorities, `include/z64thread.h:16-27` via R2 section 1):

| Thread | Pri | Role |
|---|---|---|
| sched | 16 | Presenter: slots, swaps, `CfbInfo` binding, `TickStart` posts, patch apply in `Sched_RunTask` |
| padmgr | 15 | Unchanged |
| graph, during submit | 12 (new) | Wait for K(k-1), callback, submit. Above audiomgr so audio synthesis does not widen the RCP gap |
| audiomgr | 11 | Unchanged. One audio task per field (R1 appendix C section 4) |
| graph | 9 | Logic, draw, record, stage, lerp |

**Per field:** one gfx render, one audio task, one patch apply. **Per tick:** one `Graph_Update` and one submit.

**Tick start and phase.** Two divisors matter per tick. `B_k` is the phase, chosen from the divisor in effect when tick k starts, which is the one tick k-1 left set. `d_k` is the number of images, read after tick k's logic, the same value retail copies into `CfbInfo.updateRate` (`src/code/graph.c:240`). Tick k starts at retrace `S_k`. Its images show in fields `W_k` to `W_k + d_k - 1`, with `W_k = S_k + B_k + 1`. Image j renders no later than field `W_k + j - 2`. The next start is `S_{k+1} = S_k + B_k + d_k - B_{k+1}`, with `B_{k+1} = Replay_PhaseFor(state, d_k)`. Tick k's CPU deadline is `B_k` fields.

| State | d | B | CPU deadline per tick | First image | Keyframe |
|---|---|---|---|---|---|
| Play | 3 | 3 | 3 fields | S+4 | S+6 |
| Play, pause menu | 2 | 2 | 2 fields | S+3 | S+4 |
| Play, tile transition | 1 | 1 | 1 field | S+2 | S+2 |
| File select, title setup, map select, day telop, console logo, PreNMI | 1 | none | retail's elastic loop | as retail | as retail |

Inside Play `B_{k+1} = d_k`, so the formula reduces to `S_{k+1} = S_k + B_k` and `W_{k+1} = W_k + d_k`. The display windows stay contiguous across a divisor change, and the tick that follows a change gets the old divisor's deadline (inferred algebra). Examples: unpause 2 to 3 gives that tick 2 fields. The end of a tile transition, 1 to 3, gives it 1 field, as retail's divisor-1 tick also had. Host tests cover 3 to 2, 2 to 3, 3 to 1 and 1 to 3. When a start would fall before the next retrace, the tick starts at the next retrace, the window shifts by the difference, and `lateTicks` counts it.

**Divisor-1 states outside Play keep retail's loop.** Title, file select, map select, day telop, console logo and PreNMI present one image per tick already. Replay is inactive there: the graph thread does not wait for a tick start, `Replay_SubmitTick` submits one image the way `Graph_TaskSet00` does, and the swap rule is retail's. Their latency is retail's by identity. A retrace-locked loop would lose retail's overlap of build and render and fall behind retail's measured 1 to 4 fields. A switch into Play anchors `S` at the first retrace after the previous state's last submit.

**Framebuffers: four.** `gHiBuffer` and `gLoBuffer` stay retail's (`src/code/sys_cfb.c:8`, `include/buffers.h:10-34`) and receive only keyframes. `gReplayInterpFb[2]` alternate for interpolated images. At d = 3 the rotation is K(k-1) in FB-b, I(k,1) in I0, I(k,2) in I1, K(k) in FB-a, I(k+1,1) in I0. Each target was last shown at least two fields earlier (inferred from the rotation). `sGraphCfbInfos` stays 3 entries (`src/code/graph.c:11`), so harness offsets do not move. An image starts only when a `CfbInfo` is free, which caps render-ahead at two undisplayed images.

**Gfx pools: two, as retail.** Pool `k % 2` is written by tick k from `S_k` and read by its renders until K(k) finishes in field `S_k + 5`. Tick k+2 starts at `S_k + 6`, and `Graph_SetNextGfxPool` rewrites that pool's arenas and master list first thing (`src/code/graph.c:53-85`). That is zero margin, not slack. So `Replay_WaitTickStart` waits for both the start retrace and K(k-2) done. A late K(k-2) delays tick k's start and counts as a late tick. It never corrupts a live list.

**The scheduler's swap gate changes.** Retail allows one pending swap (`pendingSwapBuf1`) and refuses to start a task while it is set (`src/code/sched.c:243-253`). That gate would serialize one render per field and forbid render-ahead. Replay replaces it with an ordered queue of done, unshown images. A gfx task may start when RSP and RDP are idle, a `CfbInfo` is free, and its target framebuffer is neither on screen nor queued for display. Audio task priority and yields stay retail (`src/code/sched.c:411-415`).

**Presenter rule.** The VI manager writes `VI_ORIGIN` at the VI interrupt and only then sends the retrace message (`src/libultra/io/vimgr.c:82-90`). A swap requested during field r therefore shows from field r+1, and a swap requested in the retrace-r handler also shows from r+1. So the presenter swaps on completion, as retail does (`src/code/sched.c:307-317`). When an image completes during field r, it swaps at once if it is the next image in order and its `slotRetrace <= r + 1`. Otherwise it waits in the queue, and the retrace handler swaps it at the first retrace `r` with `slotRetrace <= r + 1`. Images show in order.

**Overload policy.** A keyframe is never dropped. If image j of tick k is not done at its slot, the VI holds the current image, which is retail's lag behavior (R2 section 3), and `lateFields` counts it. Once any later image of tick k is done, the presenter skips the late interpolated images of that tick and shows the newest done one. If tick k+1's first slot arrives before K(k) is done, the remaining interpolated images of tick k+1 are dropped and K(k), then K(k+1), show as soon as each is done. So a backlog never grows past one tick, and latency recovers within one tick of the load dropping. Tick starts stay on their retrace schedule unless the pool wait above delays them.

**Late fields are detected, and impossible under the load bounds.** If the graph thread is not waiting when `S_k` arrives, `lateTicks` increments and the tick starts at the next retrace. Lateness cannot occur when every tick meets the bounds in [Q7](#q7-load-projection) at its worst case, not its median. The BENCH build records the minimum slack of each bound per scene, so a unit sees its margin before it reaches zero.

**Divisor 2 and 1.** At divisor 2 (pause) a tick presents a half-step image and the keyframe. The pause background is restored from the Z-buffer copy (`src/code/z_play.c:1268-1279`). On the PROCESS tick `PreRender_ApplyFiltersSlowlyInit` (`src/code/z_play.c:1263`) starts the low-priority slowly thread, which filters that copy in place (`src/code/PreRender.c:525`, `678`) across several ticks on idle CPU. Two renders of one tick would read it at different moments, and Replay's different idle pattern changes the filter's progress against retail. So every tick is keyframe-only while the pause background state is not OFF or its filter is not done. Pause presents at retail's rate until then (gate G2 covers the hold). Kaleido interpolation starts once the background is static. At divisor 1 inside Play (tile transitions) Replay presents one image per tick with B = 1. Hi-res Bombers' Notebook ticks (`src/code/z_play.c:1452-1477`) are keyframe-only, held `d` fields (gate G3).

## Q4. Latency (F3)

**Definition.** The field in which tick k's `Graph_Update` starts (it reads input first) to the first field that shows any of tick k's images. This is the quantity measured in the table above. It matches the harness, which keys input to ticks (`H1a:tools/fps60/README.md`, "How the harness reads the game"). Retail Play reads 7 fields, and 6 on CPU-heavy ticks, so the bound is 6.

**Divisor 3, field by field, with `S_k` = field 0.** "Field n" begins at retrace n.

| Field | Graph thread | RCP | VI shows | Tick k on screen |
|---|---|---|---|---|
| 0 | `TickStart(k)` at retrace 0. Input read, `Audio_Update`, logic k | I(k-1,1) | K(k-2) | no |
| 1 | Logic and draw k, recording | I(k-1,2) | I(k-1,1) | no |
| 2 | Draw k ends. Stage, match, lerp. Wait for K(k-1), which finishes in this field. Callback reads its Z. Submit tick k | K(k-1), then I(k,1) may start | I(k-1,2) | no |
| 3 | `TickStart(k+1)` at retrace 3. Logic k+1 | I(k,1) finishes by retrace 4 | K(k-1) | no |
| 4 | Logic k+1 | I(k,2) | I(k,1) | yes: all non-matrix state of tick k, matrices at 1/3 |
| 5 | Logic k+1 | K(k) | I(k,2) | matrices at 2/3 |
| 6 | Stage k+1 | I(k+1,1) | K(k) | keyframe |

Image 1 renders tick k's own list. Any texrect, color, spawn, despawn, text or menu change from input k shows at field 4. A change that exists only as a matrix delta can round to the same pixels at 1/3 or 2/3. It then shows at field 5 or at the keyframe at field 6. Field 6 equals the retail minimum, so F3 holds for every kind of change whenever no field is late (inferred from the table, which is fixed by construction).

**Divisor 2.** Input at field 0, first image at field 3, keyframe at 4. Retail pause latency is not measured. By the retail pipeline argument it is `3d - R` = `6 - R` fields, at least 4 when the task takes at most 2 fields (a guess until measured). While the pause background filters, ticks are keyframe-only, so latency is 4. P2-ref part a measures a pause scene against retail.

**Divisor 1.** Outside Play the loop is retail's, so latency is retail's by identity (1 to 4 fields in file select, measured). In a Play tile transition, latency is 2. Retail's tile-transition latency is not measured, and P2-ref measures it on a transition scene.

**A real press, not a tick-keyed input.** A human press waits up to 3 fields for the next tick read in both builds, and the two phases are unrelated. So press-to-change is about 4 to 7 fields here against about 6 to 10 in retail (inferred). The audio path is unchanged, so the picture now arrives about 3 fields earlier relative to sound than in retail (gate G5).

## Q5. Effect budget and other timing paths

**`func_80173B48`.** The shipping source stays retail's: `OS_CYCLES_TO_NSEC(div * gIrqMgrRetraceTime) - OS_CYCLES_TO_NSEC(gRDPTimeTotal)` (`src/code/game.c:300-303`). Replay changes only what feeds `gRDPTimeTotal`. The scheduler assigns `gRDPTimeAcc` (`src/code/sched.c:529`) only when the finishing task is a keyframe, and `Replay_SubmitTick(k)` copies it to the total after waiting for K(k-1), where retail copies it after `Graph_TaskSet00` (`src/code/graph.c:347-361`). So logic sees "keyframe k-1, RSP start to RDP full sync", retail's quantity for retail's list. The logic divisor stays in the formula.

Under `BENCH` the H1a pin replaces the whole expression with `div * retraceTicks` (`H1a:tools/fps60/README.md`, Pins). Reference and candidate share it, so F1 is unaffected.

**The residual is a gate (G1).** Every RSP or RDP speedup in P3 shortens the keyframe's measured time. A shipping ROM then draws more rain (`budget/3e7`, `src/code/z_kankyo.c:2080-2085`) and more of Link's dust (`src/overlays/actors/ovl_player_actor/z_player.c:10320`, `10336`, `10346`) than a retail console would. Snow does not move. It saturates at 14 ms, and a no-lag retail tick leaves at least 50 - 34.2 = 15.8 ms (inferred from the cited fork maximum). No formula can reproduce a console's RDP time without running retail's renderer. The pinned BENCH value is the maximum budget, so the load the harness measures is an upper bound on what the shipping formula draws (inferred).

This is a logic change, not only a particle count. Dust count drives `Rand_CenteredFloat` and `EffectSsDust_Spawn` calls in Player (`src/overlays/actors/ovl_player_actor/z_player.c:10320-10330`), so the RNG stream diverges from a console's. And it is not gradual: 60 fps needs `W_t <= T_f`, so once P3 succeeds the budget is at least 33.3 ms. Rain then clamps at full (`src/code/z_kankyo.c:2080-2081`), and dust roughly doubles against retail's 16 to 24 ms budgets (inferred). Default: ship the formula above, and accept that shipping logic RNG diverges from a retail console once P3 succeeds.

**Other paths** (R2 section 5 and the unpinned table in `H1a:tools/fps60/README.md`):

| Path | Where | Shipping behavior |
|---|---|---|
| `Rand_Seed(osGetTime())` | `src/code/z_play.c:2264-2268` | Unchanged. Boot-time dependent in retail too. |
| Minigame, moon-crash, postman and bottle timers on `osGetTime` | `src/code/z_parameter.c:5663-6187` | Unchanged. They run in draw once per tick, so they read wall time once per tick as retail does. |
| Postman 10-second A press, per field | `src/code/z_parameter.c:823-832`, `src/code/padmgr.c:672-675` | Unchanged. |
| Flash write 2 s | `src/code/z_sram_NES.c:1925-1978` | Unchanged. |
| Audio `osGetCount` randomness | `src/audio/lib/thread.c:156`, `964-973` | Unchanged. |
| Ocarina note timing per audio task | `src/audio/code_8019AF00.c:3229` | Unchanged, per field. |
| Sun glare and glow Z reads | `src/code/z_kankyo.c:532-537` | Keyframe k-1's Z, as retail ([Q1](#q1-draw-and-logic-split)). |
| DMA polling: object, room, mask and get-item loads, story, notebook | `src/code/z_scene.c:93`, `src/code/z_room.c:608`, `src/code/z_player_lib.c:208`, `2946`, `src/code/z_parameter.c:6791`, `src/code/z_play_hireso.c:1167` | Tick starts are exactly `d` fields apart. Retail's are `d` apart with load-dependent phase. Replay moves tick phase against DMA completion everywhere, so the first in-window load that lands on a different tick fails F1 for the rest of the run against the deterministic reference. This is a determinism problem for the harness, not a residual (risk R4, follow-up for a harness unit). |
| `AudioSeq_GetActiveSeqId`, `Audio_IsSequencePlaying` | 15 call sites (H1a) | Same class as DMA polling (risk R4). |
| Speed meter | `src/code/game.c:114-115` | Debug only. |
| Graph 3 s watchdog | `src/code/graph.c:159` | Kept, on the wait for K(k-1). |

## Q6. Memory

| Item | Bytes | Where |
|---|---|---|
| `gReplayInterpFb[2]` | 307,263 (2 x 153,600 + 63 alignment) | System heap in `Graph_ThreadEntry`, next to the Z-buffer (`src/code/graph.c:385-388`) |
| `ReplayRecording` x 2 | 10,288 | bss |
| Staging: retail copy and interpolated copies | 0 new | Each pool's POLY_OPA gap after the build. Needs 3 x 64 x 208 = 39,936 B at the measured maximum. The gap is at least 76,572 B in the bench scenes (inferred from the cited 27.4% peak use). |
| Match hash scratch, 512 x u16 | 1,024 | bss |
| Actor serial table, 512 x 8 | 4,096 | bss |
| `ReplayTick` x 2, presenter, recorder, stats | about 900 | bss |
| **Total new** | **about 323,600 B (316 KiB)** | |

Play hands all remaining system heap to ZeldaArena (`src/code/game.c:206-214`, `src/code/z_play.c:2154`, `2312-2317`, via R2 section 6). So both the heap buffers and the bss come out of ZeldaArena. Termina Field's minimum free is 695 KiB on a BENCH build (`tools/bench/README.md`), which leaves about 379 KiB (inferred). A shipping build has 66.7 KiB more heap than BENCH (cited, same README), so about 446 KiB (inferred).

- No third gfx pool is needed (Q3).
- The fourth framebuffer exists only so the keyframe sees retail's prior contents without a coverage proof. If P2-mem proves every Play pixel is written each frame, it drops to three and saves 153,600 B.
- The worst scene in the whole game is unknown (risk R5). ZeldaArena allocations need contiguous blocks, so total free is the wrong measure. Every Play corpus scene must report the minimum of its largest free block (`zelda_max_free`) and its largest allocation request. The memory rule is: after this design's 316 KiB, no allocation that succeeds in retail may fail. B1 derives the per-scene floor from those two numbers. The 695 KiB input above comes from a 600-frame window on the older fork and is a starting point only.
- The new framebuffers go next to the Z-buffer. Their RDRAM bank against the Z-buffer and the shown buffer affects RDP speed (R1 technique 18). P2-ref records their addresses, and P3's `mem` track can move them.

## Q7. Load projection

Perf units measure the retail architecture and project the Replay load with these formulas until P2-ref lands. Inputs are per tick k on the retail architecture. B1 measured them on the pinned runner; [budget.md](budget.md) has the per-scene tables and the commands.

| Symbol | Meaning | Source |
|---|---|---|
| `C_up` | Graph-thread CPU for input, `Audio_Update` and logic (`Play_Update`, cutscene manager) | `load_ticks.tsv` `cpu_pre + cpu_update + cpu_game` (H1b phases) |
| `C_dr` | Graph-thread CPU for draw and list close | `cpu_draw + cpu_post` |
| `C_hp` | CPU of the other threads during the tick's `d` fields (AudioMgr, scheduler, IRQ, pad, DMA) | `cpu_busy - graph` per tick |
| `N_m` | Pool matrices per tick | The probe in the measurement table (measured 2 to 208) |
| `R_g` | RSP gfx busy for the tick's task, outside audio tasks | B1 task ring (`rsp - rsp_audio` from first RSP start to RDP full sync); the harness's per-tick `rsp_gfx` approximates it |
| `R_a` | RSP audio busy per field | `load_fields.tsv` `rsp_audio` |
| `P` | RDP busy for the tick's task | B1 task ring `rdp_buf` (`DPC_BUFBUSY`). Not `DPC_PIPEBUSY`, which in the fork holds until `SYNC_FULL` and so counts RDP time spent waiting on the RSP (H1b) |
| `W_t` | Gfx task wall time, RSP start to RDP full sync | B1 task ring (`rdp_done - start`, retail's `gRDPTimeAcc` quantity) |

Constants that P2-ref measures once with a BENCH microbench, with initial estimates (inferred, low confidence): `c_rec` 0.5 us per record, `c_save` 0.7 us per 64 B copy into the gap, `c_match` 0.5 us, `c_lerp` 2.5 us per matrix per interpolated image, `c_apply` 1.5 us per matrix per image (cold D-cache reads from the gap), `c_sub` 50 us per tick, `c_start` 30 us per image, `c_gap` 100 us per tick for the graph-thread wake, the callback, the post and the context switches between K(k-1) done and I(k,1) start. `c_lerp` holds only with the lerp specialized for d = 2 and d = 3 through constant-reciprocal multiplies. With two VR4300 `DIV` instructions per element (about 37 cycles each, inferred) it would be about 13 us. `T_f` is one field: 16.715 ms, the median runner field (measured, H1b), which the harness uses for shares. The CPU-fit conclusion below stands only after the microbench confirms these values.

```
G   = C_up + C_dr + N_m*(c_rec + c_save + c_match + (d-1)*c_lerp) + c_sub   // graph thread, per tick
A   = d * (N_m*c_apply + c_start)                                            // scheduler, per tick

CPU tick deadline:   G + A + C_hp <= 0.9 * B * T_f
CPU busy, field i:   min(T_f, max(0, G + A + C_hp - i*T_f)),  i = 0 .. d-1
RSP per field:       R_g + R_a <= 0.9 * T_f
RDP per field:       P <= 0.9 * T_f
First image:         W_t + N_m*c_apply + c_start + c_gap <= 0.9 * T_f
Image j (cumulative): c_gap + sum over images 1..j of (W_t + N_m*c_apply + c_start) <= 0.9 * j * T_f
```

Every image executes the same commands as the keyframe, so per-field RSP and RDP equal the retail per-task values. Matrix changes can move RSP clipping and culling slightly (inferred, small). Every bound is evaluated on the worst tick in the window (max, with p99 alongside), never on the median. `W_t` runs from RSP start to RDP full sync, so the first-image bound already covers RSP and RDP together, and it is the headline RCP gate. The separate RSP and RDP lines attribute the gap to a resource.

**BENCH counters under Replay.** Retail's `gRSPGfxTimeAcc` adds every gfx task's RSP time, so under Replay it would sum `d` renders, while `gRDPTimeAcc` is keyframe-only. The BENCH build therefore records per-image counters (RSP gfx, RDP busy, task wall time) and leaves the retail totals' meaning as retail's per task, so retail-against-Replay comparisons stay like for like.

**Worked numbers.** Measured on the pinned runner (sha256 `1f9d59f66929c5b5`) over each scene's 600-field window, at `d = 3`, `B = 3`, budget 15.04 ms per field and 45.13 ms per tick. CPU per tick is measured retail CPU busy (main ROM, which includes `C_hp`) plus the inferred Replay terms at the scene's measured `N_m` maximum. RSP, RDP and `W_t` are per task from the B1 task ring. Max / p99, in ms:

| Scene | `N_m` max | CPU per tick, projected (Replay terms) | RSP per field (`R_g + R_a`) | RDP per field (`P`) | `W_t` + first-image terms |
|---|---|---|---|---|---|
| title | 26 | 16.38 / 16.31 (0.43) | 6.31 / 6.27 | 26.75 / 26.56 | 27.10 / 26.90 |
| South Clock Town (sct) | 145 | 44.42 / 41.91 (1.76) | 30.73 / 29.87 | 31.22 / 31.04 | 39.48 / 35.79 |
| Termina Field | 80 | 37.60 / 37.39 (1.04) | 19.73 / 19.51 | 23.82 / 23.77 | 25.12 / 24.90 |
| Mountain Village | 50 | 24.50 / 24.48 (0.70) | 15.99 / 15.85 | 25.38 / 25.37 | 25.80 / 25.79 |
| Great Bay Coast | 203 | 40.57 / 40.11 (2.41) | 24.65 / 24.55 | 25.15 / 25.14 | 29.41 / 29.19 |

At `N_m` = 187 the Replay terms are 1.30 ms for the graph thread (`G` minus `C_up + C_dr`) and 2.23 ms with the scheduler's `A` (inferred from the constants above).

- **CPU fits the per-tick deadline in every Play scene**, with 0.71 ms to spare in sct at max (0.98x). That margin is within the BENCH hook cost and the measured code-layout sensitivity (budget.md), so sct still needs a CPU cut. The per-field form reads 100% busy in the first field of every Play tick (inferred).
- **The RCP does not fit.** `W_t` is 1.67x to 2.62x over budget at max (10.1 to 24.4 ms). RDP busy alone is 1.58x to 2.08x over and binds every Play scene. RSP gfx alone (`R_g`, max) is 1.07x (field) to 1.74x (sct) over in the outdoor scenes and under budget in mountain (0.96x) and title (0.32x); 3.9 ms (field, p50) to 8.2 ms (sct, max) of it per task is the RSP spinning on RDP back-pressure (`ARES_DPLOG`, measured in budget.md).

**Per-field CPU rule (gate G4).** Decided by preferences line 23: CPU is judged per tick (`G + A + C_hp <= 0.9 * B * T_f`) and no image may be presented late. The per-field form is reported as information.

## Q8. Harness interface

- **Keyframe symbol.** `u32 gBenchCfbKeyframe[3]`, BENCH only, parallel to `sGraphCfbInfos[3]` and `gBenchProbe.cfbTick[3]`, as H1a defines (`H1a:tools/fps60/README.md`, "Keyframes in a 60 fps build"). The presenter sets `cfbTick[i] = tick` and `gBenchCfbKeyframe[i] = isKeyframe` when it binds entry i at task start, the point where `Bench_TagCfb` tags today (`H1a:src/code/graph.c:243-245`). For an interpolated image `cfbTick` is the tick it blends toward. `CfbInfo.framebuffer` is the image's real target, so `run.py`'s check that `VI_ORIGIN - curBuf->framebuffer` is constant still holds. No-swap ticks are never bound, so they read as skipped ticks, as in retail.
- **Image index symbol (harness unit).** `u8 gBenchCfbImage[3]`, the image index 1 to `d` within its tick, set at the same moment. It makes the F2 key explicit instead of derived from field order. The probe layout is a harness contract (preferences line 7), so a harness unit adds it, not P2-ref.
- **Diagnostics, outside the frozen contract.** `ReplayStats gReplayStats` (BENCH): `lateFields`, `lateTicks`, `recorded`, `patched`, `snappedOwners`, `snapAllTicks`, `gapShortfall`, `overflow`, and the minimum slack of each Q7 bound.
- **F2 golden set.** An interpolated image's command stream depends only on the two recordings and `j/d`, never on timing (Q2). Its pixels are timing-invariant under the H1c noise pin (preferences line 20), provided no render reads memory that concurrent logic writes (risks R3 and R8, which P2-ref part a tests for). So the golden set is keyed by `(image_tick, image index)`, one `fb_hash` per image taken at the last field it shows, from P2-ref's run. A perf unit that only changes timing cannot change a golden hash, and a slow P2-ref run with late fields records the same values as a fast one. F2 passes when every key matches. Lag is judged separately by the presentation metric. The harness unit owns `golden.tsv` and the F2 comparison.

## Q9. P2 unit breakdown

**P2-ref part a: the pipeline with interpolation off.** Presenter, four framebuffers, `ReplayTick` lifecycle, retrace-locked ticks with the phase table, keyframe-only `gRDPTimeAcc`, callback ordering, submit priority, FB words, motion-blur interp end, no-swap and keyframe-only ticks, `gBenchCfbKeyframe`, `gReplayStats`. Recording runs, and every record snaps.

- F1 passes on every corpus scene against the retail-architecture BENCH reference.
- **Entry criteria.** The corpus adds a pause scene, a Play tile transition, a scene with Link moving and a motion-blur scene before part a can pass. The six H1a scenes exercise none of them.
- All fidelity runs use the H1c noise pin (preferences line 20).
- **Keyframe byte check (BENCH, deterministic, no pixels):** at each keyframe task start, a hash of the pool's display-list region and every recorded `Mtx` equals the hash taken when the build closed. This proves the keyframe runs the bytes the game wrote.
- **Images are really shown:** in every Play window, keyframe=0 images cover at least `(d-1)/d` of the fields minus `lateFields`, and `gBenchCfbKeyframe` reads 0 on them. Without this, a presenter that never shows an interpolated image passes everything below.
- **Idempotence test:** in every scene, every interpolated image's `fb_hash` equals its tick's keyframe `fb_hash`. With all records snapped this proves that resubmission has no side effects, that the FB word list is complete for the corpus, and that motion blur feeds back only keyframes. A failure in a skybox scene can also mean the list leaves pixels uncovered, because interpolated buffers hold different prior contents. Each failure is diagnosed to one of those causes. A coverage failure is the expected reason for the fourth framebuffer, not a test bug.
- **FB word scan (BENCH):** after each build, a scan of the pool's display-list region fails on any color-image or texture-image command whose address falls inside the keyframe framebuffer's range and is not recorded. Ranges, not exact addresses, so offset and row addresses are caught.
- **RCP timing perturbation test:** a BENCH flag delays each keyframe task start by a fixed amount (0.5 ms and 2 ms), and a second flag delays the graph thread instead. F1, F2 and the idempotence test still pass in both. The keyframe renders 2 to 3 fields into the next tick's logic, far later than retail's task does, so any non-pool render input that the next tick writes shows up here (risk R3). A graph-thread delay alone would hide the hazard, because P2-ref runs are RCP-bound.
- **Audit of non-pool render inputs:** a list of every buffer outside the pool that the display list reads and that logic or draw writes (font glyphs loaded by `Font_LoadChar`, `src/code/z_kanfont.c:9`, is the first candidate), each with its writer and reader timing.
- Determinism passes. Speed is ignored: late fields are reported, not gated.

**P2-ref part b: matrix interpolation.** `Replay_RecordMtx` at the choke points, owners for actors, camera and skybox, the matching and snap rules in Q2, `Replay_LerpMtx`, gap staging.

- F1 passes on every scene.
- Host tests for `Replay_LerpMtx`: `j = 0` gives `a`, `num = den` is rejected, overflow snaps, hand-computed s15.16 cases with negative deltas and deltas not divisible by `d`, and an exhaustive check of the reciprocal-multiply specialization against `a + trunc(delta*j/d)` over the s32 range of `delta` that does not overflow.
- An audit of every pool pointer kept across ticks (risk R1). Known ones are `sSkyboxDrawMatrix` (`src/code/z_vr_box_draw.c:3`), `view->projectionPtr` and `viewingPtr` (`src/code/z_view.c:335`, `353`) and `play->billboardMtx`. Each is shown to be rewritten before the next tick reads it, or fixed.
- `gReplayStats` gates: in moving scenes, patched records are at least 80% of recorded records, snapped owners stay under 10% per tick outside snap-all ticks, and interpolated hashes differ from keyframe hashes. Otherwise a broken matcher that snaps everything would pass F1 and become the golden set.
- A verifier reviews frame dumps of three consecutive fields in sct and field.
- The Q7 constants microbench runs, and its values replace the estimates in this document.
- The run's hashes become the F2 golden set.

**Hazard units.** Each is labeled `interp-fix`. Acceptance for each: F1 passes, the F2 golden diff changes only images of its category, and a verifier signs off on frame dumps of a scene that exercises it.

| Unit | Adds |
|---|---|
| P2-cam | Camera-cut and teleport detection (setting or mode change, eye jump, door camera, cutscene cuts; R1 hazard table). Camera lerp on eye, at, up and fov through `guLookAt` and `guPerspective` for interpolated images only. Skybox keyed to a camera epoch. Culling: actors and rooms are culled with tick k's camera (`src/code/z_actor.c:3190-3203`), so an interpolated image with a camera nearer tick k-1 can lose edge geometry. Decide a policy, for example cull with the union of both ticks' frusta for drawing only, never for logic. |
| P2-actor | Actor-relative lerp of root matrices (2S2H's fix for the 180-degree "paper" turn), the 90 and 122 degree snap limits, per-actor teleport flags, billboards. |
| P2-skel | Limb-count guard per skeleton, held items, skin `Vtx` lerp written into a presenter buffer with the `gSPVertex` word retargeted. Never into the actor's parity buffers, which logic reads (`src/code/z_skin.c:271`). |
| P2-fx | `EFFECT_SS` owners with epochs, weather wrap (`Object_Kankyo`), `EffectBlure` trail vertices. |
| P2-scroll | `G_SETTILESIZE` words from `Gfx_TexScroll*` (`src/code/z_rcp.c:1396-1410`, 119 call lines in 77 files) as Gfx records, with modulo-2048 wrap detection. |
| P2-ui | HUD and textbox texrect coordinates where elements slide. Kaleido page and cursor snaps at divisor 2. |
| P2-env | Light structs, fog words, lens flare and glow positions. |
| P2-room | Room swaps, image rooms, scene transitions. |
| P2-fb | Audit of every FB or work-buffer feedback path outside the corpus (wipe5, VisFbuf, lens), the draw-time Z readers in risk R8 with a decision on a second Z-buffer, and a BENCH scan that fails on any color-image or texture-image word in the pool that points into the keyframe FB and is not recorded. |
| P2-mem | Optional. Prove full-coverage drawing and drop to three framebuffers. |

Order: part a, part b, then P2-cam, P2-actor and P2-skel, because they cover the motion that dominates the corpus (inferred).

## Alternatives considered

The design came from an architect-skill bakeoff between two full candidate designs, written independently by an opus runner and a sonnet runner, then cross-judged and interrogated. The rejected whole-shape alternative and the rejected variants inside the chosen shape follow.

### Rejected: render pass (re-run draw over interpolated inputs)

Each tick runs logic plus the retail draw once as the keyframe. Each interpolated image comes from a separate render-only run of the game's draw code over interpolated actor, joint and camera state. Draw-side mutations are suppressed by a checkpoint of the RNG, `PlayState`, `SaveContext`, the REG array, the SFX index and the EffectSs table, plus a per-actor save and restore of the instance around its draw, plus guards on the SFX, spawn and alloc funnels.

It lost on three counts:

- **CPU.** It pays one draw per image. With an assumed 40% logic share, sct and field need each extra pass to cost about 0.55 to 0.58 of a keyframe draw to fit 45 ms, and Great Bay about 0.19 (the candidate's own inferred numbers). Replay's extra images cost about 0.5 ms each at 187 matrices.
- **Fidelity by construction.** Instance restore covers writes through `this`, 1,110 of 1,884 actor hits by the candidate's count. The rest, including cross-actor writes such as Player writing a held actor's position (`src/code/z_player_lib.c:3743`) and actor-owned arena buffers such as Epona's skin table, rely on a regex-level scan and a verifier that only proves what a scene exercises. Replay has no per-site judgment at all.
- **Memory.** About 563 KiB new: a third gfx pool, a second Z-buffer and the checkpoint arenas, against Replay's 316 KiB.

Its advantages are better interpolation quality (input-space lerp has no rotation shrink) and interpolated HUD motion without texrect patching. Replay recovers the first through P2-cam and P2-actor, which lerp inputs only for interpolated images.

### Rejected variants of Replay

| Variant | Why it lost |
|---|---|
| A retrace-locked loop for divisor-1 states outside Play | Loses retail's overlap of build and render, so it falls behind retail's measured 1 to 4 field latency in file select. Those states already present every tick. |
| Render the keyframe first, then the interpolated images | The first image renders one field later (S+5, keyframe S+7), which breaks F3's 6-field bound for matrix-only changes. The interpolated renders also clear keyframe k-1's Z before tick k's graph callback can read it, and saving a full Z copy costs 150 KiB plus a copy per tick. |
| Two framebuffers, alternating per field | The keyframe would render into the buffer that holds this tick's first interpolated image. Play skips the color clear under a skybox (`src/code/z_play.c:1147-1152`), so any pixel the list leaves untouched would differ from retail. Needs a coverage proof first (P2-mem). |
| Segment-indirected matrix banks, one bank per image | No patching between renders, but every keyframe command stream changes, segment 0x0D limb tables hold raw base pointers that need patching anyway, and CPU code that dereferences a returned `Mtx*` breaks. It exposes address encoding to every matrix producer. |
| Staging in a fixed bss arena instead of the pool gap | Costs 147,456 B and duplicates a lifetime the pool already has. It wins only if some scene's POLY_OPA use exceeds about 62%. |
| Compute the lerp in the scheduler at task start | Saves gap space but grows the RCP-idle gap by about 0.47 ms per image at 187 matrices (inferred). The RCP is the scarce resource. |
| Pin the effect budget in shipping builds | Matches F1's reference, but draws more rain than any retail console from the first tick, rather than only as P3 speeds up the RCP. Kept as the gate G1 alternative. |
| Pad latency to retail's 7 fields | Keeps retail's audio-to-picture offset but needs a third gfx pool (131,856 B) and gives up F3 margin. Kept as the gate G5 alternative. |

## Tradeoffs accepted

- We accept two extra framebuffers (300 KiB) in exchange for keyframes that see retail's exact prior framebuffer contents without a coverage proof.
- We accept an RCP-idle patch gap of about 0.17 ms per image at 187 matrices (inferred) in exchange for keyframes that run byte-identical command streams.
- We accept stepped 20 Hz motion for everything that is not a pool matrix in P2-ref, in exchange for a pipeline that the idempotence test proves before any interpolation lands.
- We accept component-wise matrix lerp in P2-ref, which shrinks matrices mid-turn, in exchange for exact integer math with no float state. P2-actor fixes the visible case.
- We accept fixed latency (4 and 6 fields at divisor 3) instead of retail's elastic 6 to 7, in exchange for F3 by construction.
- We accept snapping whole owners whose matrix count changes, which freezes some actors' interpolated images, in exchange for never pairing unrelated matrices.

## Interrogate findings

Two reviewers (opus, sonnet) read this design and the code with no knowledge of the architect panel's verdict. Each finding below has the lead's call and where the doc now answers it.

| # | Finding | Severity | Call | Resolution |
|---|---|---|---|---|
| 1 | RDP noise steps per RDP clock, so F2's "pixels are timing-invariant" and the idempotence test fail on any noise-using scene | critical | act | All fidelity claims run under the H1c noise pin with the noise-command hash (preferences line 20). F2 restated. Deterministic keyframe byte check added ([Q9](#q9-p2-unit-breakdown)). |
| 2 | The presenter as written adds a field: a swap in the retrace-r handler shows from r+1, not r | critical | act | Swap on completion with the slot gate, as retail does; the retrace handler only flushes waiting images ([Q3](#q3-schedule)). |
| 3 | The tick-start formula mixed tick k's and k+1's divisors; at a divisor change the windows overlapped or gapped | high | act | `B_k` from the divisor at tick start, `d_k` after logic; algebra corrected; host tests for four transitions ([Q3](#q3-schedule)). |
| 4 | Divisor-1 states under a retrace lock lose retail's build-render overlap and fall behind retail latency | high | act | Replay is inactive outside Play; retail's loop runs there ([Q3](#q3-schedule)). |
| 5 | No overload policy: a backlog grows without bound | high | act | Late interpolated images are skipped, never keyframes; backlog capped at one tick ([Q3](#q3-schedule)). |
| 6 | The two pools have zero margin, not one field of slack | high | act | Claim corrected; tick start also waits K(k-2) done ([Q3](#q3-schedule)). |
| 7 | Pause background is filtered in place by the slowly thread across ticks, so renders of one tick differ | high | act | Keyframe-only while the pause background filters (gate G2 widened). |
| 8 | Vacuous tests: a presenter that never shows interpolated images, or a matcher that snaps everything, passes F1 and idempotence | high | act | Shown-image coverage check and `gReplayStats` ratio gates ([Q9](#q9-p2-unit-breakdown)). |
| 9 | The perturbation test delays only the graph thread; P2-ref is RCP-bound, so it cannot expose renders reading next-tick memory | high | act | Delay keyframe task start too, both directions; audit of non-pool render inputs ([Q9](#q9-p2-unit-breakdown)). |
| 10 | Gfx-record overflow snapping a tail can leave an FB word unpatched, so an interpolated render writes the keyframe buffer | high | act | Gfx overflow makes the tick keyframe-only ([Data shape](#data-shape)). |
| 11 | G1 understated: once `W_t <= T_f`, rain clamps full, dust about doubles, and Player's RNG stream diverges | medium | act | G1 and Q5 restated as a logic divergence. |
| 12 | R4 is not a rare residual: Replay moves tick phase against DMA everywhere, so F1 fails for the rest of a run | medium | act | R4 restated; follow-up for a harness pin on load-completion visibility. |
| 13 | Duplicate records for one destination (`z_view.c:517`, skybox `z_play.c:1438`) change DEFAULT's count and snap it | medium | act | Dedupe by destination. |
| 14 | The headline RCP gap is `W_t`, 1.7x to 2.3x, not RSP's 1.2x to 1.9x; worked numbers are medians from an old fork | medium | act | Headline restated; bounds on max and p99; B1 recomputes on the pinned runner; `c_gap` added. |
| 15 | BENCH `gRSPGfxTimeAcc` sums `d` renders under Replay while `gRDPTimeAcc` is keyframe-only | medium | act | Per-image BENCH counters ([Q7](#q7-load-projection)). |
| 16 | `c_lerp` assumes no divides; two VR4300 `DIV` per element make it about 13 us | medium | act | Lerp specialized for d = 2, 3 with reciprocal multiplies; exhaustive host test; microbench gates the CPU claim. |
| 17 | Corpus lacks pause, tile transition, motion blur and moving Link, so part a's tests pass vacuously | medium | act | Added as part a entry criteria. |
| 18 | Memory rule used total free; ZeldaArena needs contiguous blocks | medium | act | Rule on largest free block and largest request ([Q6](#q6-memory)). |
| 19 | Culling with tick k's camera can drop edge geometry in interpolated images | medium | consider | Policy decision moved into P2-cam's scope. |
| 20 | R1 audit by grep is too weak; name the retained pool pointers | medium | act | Named pointers listed with rewrite-before-read criterion. |
| 21 | FB word scan should compare ranges, not exact addresses, and run in part a | low | act | Range scan added to part a. |
| 22 | Framebuffer bank placement against the Z-buffer affects RDP speed | low | consider | Noted in Q6; P3 `mem` track can move them. |
| 23 | Wait for tick start before `Bench_BeginFrame` so BENCH timing excludes the wait | low | noted | Implementation detail for P2-ref; does not change the design. |
| 24 | Nits: 60% vs 62% gap figure, serial-table-full behavior, lerp tests for negative and non-divisible deltas | low | act | Fixed. |

## Gates

Each gate needs a product call from Scott. The default applies until he rules (preferences line 17). The coordinator copies these into `gates.md`.

| Gate | Question | Default | Alternative |
|---|---|---|---|
| G1 effect budget | Rain and dust counts follow the keyframe's measured RCP time. Once P3 succeeds, rain is at full and dust about doubles, and Player's dust spawns change the RNG stream, so shipping logic diverges from a retail console. Accept that, or ship the BENCH pin? | Ship retail's formula on keyframe time. Snow is unaffected. | Ship the pin: same counts as F1's reference, more rain than a console from the start. |
| G2 capture ticks and pause filtering | Retail freezes on the previous image for the pause-open, picto and tile capture ticks, because those ticks never swap (`src/code/graph.c:212-216`). Replay reproduces the freeze, and it presents pause at retail's rate while the background filters. Both break "new image every field" on those fields. | Reproduce retail. The harness reports those fields as retail-identical holds. | Show interpolated images of a tick retail never shows, or interpolate pause during filtering after proving the filter cannot race. |
| G3 Bombers' Notebook | The hi-res notebook would need 600 KB interpolated framebuffers (inferred). | Keyframe only, 20 images per second, as retail. | Find the memory. |
| G4 per-field CPU rule | No design that runs logic once per tick passes "CPU busy per field at most 90%" until logic plus draw fits 15 ms. | Report both forms. The per-tick deadline is the design's real constraint. | Keep the per-field rule as the gate. |
| G5 picture-to-sound offset | The picture arrives about 3 fields earlier relative to sound than in retail, because video latency drops from 7 to 4 fields and audio is unchanged. | Keep latency 4. F3 asks for "never later". | Pad to retail's 7 with a third gfx pool. |

## Open risks

- **R1, logic reading a previous tick's pool.** Between renders, Replay overwrites pool `Mtx` that retail leaves intact until the pool is rebuilt. Logic that keeps a `Mtx*` or `Vtx*` from last tick's pool would diverge. None is known. The P2-ref part b audit and the F1 corpus run are the detectors.
- **R2, divisor-1 latency and load.** Non-Play divisor-1 states need build plus render in one field. Retail file select misses that on 128 of 469 images (measured).
- **R3, renders that read memory the next tick writes.** Retail builds tick k+1 while task k renders, so data outside the pool that logic mutates can reach the RDP at timing-dependent moments in retail too. Replay keeps the overlap with different timing. 37 `Matrix_ToMtx` call lines write outside `GRAPH_ALLOC` (grep count, mostly pool arrays). The timing perturbation test in P2-ref part a is the detector.
- **R4, DMA and audio-state polling.** A load or sequence state can land on a different tick than in the deterministic reference, and F1 then fails for the rest of the run (Q5). Follow-up: a harness unit proposes a BENCH pin that makes load completion visible only at tick boundaries in both builds.
- **R5, worst-scene memory.** 316 KiB comes out of ZeldaArena. The game-wide minimum is unknown until the corpus grows.
- **R6, pool-gap shortfall** in a scene with POLY_OPA above about 62% use (inferred). The tail records snap, so quality drops, not correctness.
- **R8, draw-time Z-buffer reads.** Two overlays read the Z-buffer on the CPU during draw: the alien and UFO lens-flare check (`src/overlays/actors/ovl_En_Invadepoh/z_en_invadepoh.c:1286-1300`, called from draw at `5300` and `5414`) and its demo twin (`src/overlays/actors/ovl_En_Invadepoh_Demo/z_en_invadepoh_demo.c:582`). In retail they read whatever task k-1 or k-2 left, which is already timing-dependent. Under Replay with one Z-buffer they can read an interpolated image's depth, which can change a logic-visible flare decision. The fix is a second Z-buffer for interpolated images (153,600 B, total about 466 KiB new), so the keyframe Z-buffer only ever holds keyframes. P2-fb decides it on a Romani Ranch alien scene. It is not in P2-ref, because no corpus scene has these actors. Raised by the cross-judge, grafted from the render-pass candidate.
- **R7, the RCP.** Replay does nothing for RSP or RDP load. The first image of each tick has a hard one-field deadline after submit. 60 fps depends on P3 cutting `W_t` 1.7x to 2.3x with identical output (RSP gfx alone 1.2x to 1.9x), and the RDP share is not measured.

## Next step

Build P2-ref part a: the presenter in `sched.c` and the four-framebuffer submit in `graph.c`, with interpolation off, and get the idempotence test green on the six-scene corpus.
