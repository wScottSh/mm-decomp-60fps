# MM frame pipeline and 60 fps interpolation hazards

This document explains how one Majora's Mask frame goes from VI retrace to a displayed image. It then lists every place found that breaks if Play logic keeps its 20 Hz tick while a new image is drawn on every VI field (60 Hz).

Read it before you design a split between the logic tick and the render pass. Every claim cites `file:line` in this repo at `caeb013ac` (origin/main). Labels:

- **cited**: read from code or from a document in this repo.
- **inferred**: a conclusion drawn from cited code. It was not run.
- **measured**: not used here. This unit ran nothing.

## Overview

MM has one game loop on one thread. Each pass of the graph thread does input, audio commands, game logic, display-list build and one RCP task, in that order (`src/code/graph.c:364-377`, `src/code/game.c:153-170`). Logic and draw are not separate passes. `Play_Main` calls `Play_Update`, then `Play_Draw`, then `CutsceneManager_Update` (`src/code/z_play.c:1490-1519`).

The loop is paced by the framebuffer, not by a timer. The game state's `framerateDivisor` is copied into each task's `CfbInfo.updateRate` (`src/code/graph.c:240`). The scheduler holds each image on screen for that many fields (`src/code/sched.c:45-48`, `src/code/sched.c:423-449`). It will not start the next gfx task while a swap is pending (`src/code/sched.c:243-253`). The graph thread blocks until the previous task completes (`src/code/graph.c:159-160`). Play runs at divisor 3, so one logic tick and one image happen every 3 fields (20 Hz) (`src/code/game.c:243`).

The same divisor value sets three things: how long an image is held, the step size for part of the logic, and the effect budget (see [Framerate divisor](#2-the-framerate-divisor)). To get 60 images per second at 20 ticks per second, you must split it into a render rate and a logic rate.

The hard part is the draw path. Play's draw code runs state machines, consumes the global RNG, registers colliders, spawns effects, issues sound requests and writes positions that the next tick's logic reads. If draw runs 3 times per tick, each of these runs 3 times. The [hazard table](#4-draw-time-state-mutation-hazard-table) lists them.

## Key concepts

- **Field.** One VI retrace, 1/60 s on NTSC. irqmgr, sched, padmgr and audiomgr wake on it. Graph does not.
- **Tick or game frame.** One `Graph_Update` pass. It runs logic and draw once each and submits one gfx task (`src/code/graph.c:364-377`).
- **Divisor.** `GameState.framerateDivisor`, a u8 (`include/z64game.h:60`). Fields per tick. It is mirrored in `R_UPDATE_RATE` (`SREG(30)`, `include/regs.h:73`) and in four globals (`src/code/game.c:22-25`).
- **CfbInfo.** A per-task record of which framebuffer to show and for how many fields (`include/scheduler.h:19-29`). There are 3, used in rotation (`src/code/graph.c:11`, `src/code/graph.c:222-225`).
- **Effect budget.** `func_80173B48`, the field time for one tick minus the last tick's RDP time (`src/code/game.c:300-303`). Rain, snow and Link's dust scale with it.

## 1. Threads, priorities and per-field work

Priorities are MM's own values in `include/z64thread.h:16-27`. A higher number preempts a lower one. Graph, at 9, is below audio, pad and the scheduler.

| Thread | Pri | Created | Per field | Per tick |
|---|---|---|---|---|
| fault | 127 | `src/boot/fault.c:1101-1102` | Blocks until a CPU fault. | |
| VI manager (libultra) | 254 | `src/boot/idle.c:103` | Raises the VI event. | |
| irqmgr | 18 | `src/boot/idle.c:94` (`retraceCount` 1) | Sends `IRQ_RETRACE_MSG` to each client (`src/boot/irqmgr.c:198-209`). Measures `gIrqMgrRetraceTime` once, from the first two retraces, then never again (`src/boot/irqmgr.c:199-205`). | |
| dmamgr | 17 | `src/boot/z_std_dma.c:329-330` | On demand. | |
| sched | 16 | `src/code/main.c:83` | `Sched_HandleRetrace` (`src/code/sched.c:423-450`) counts down `updateTimer`, swaps when it reaches 0, and starts queued tasks. Also wakes on SP and DP done (`src/code/sched.c:455`, `src/code/sched.c:517`). | Receives one gfx task (`src/code/graph.c:251-253`). |
| padmgr | 15 | `src/code/main.c:96` | Polls controllers and ORs press and release edges into the pending input (`src/code/padmgr.c:517-520`). Runs the retrace callback (`src/code/padmgr.c:672-675`). | Game reads input and clears the pending edges (`src/code/padmgr.c:728-733`). |
| flashrom | 13 | `src/code/sys_flashrom.c:230` | On demand. | |
| main | 12 | `src/boot/idle.c:135-136` | Waits for NMI only (`src/code/main.c:106-124`). | |
| audiomgr | 11 | `src/code/main.c:93` | Submits the audio RSP task built last field, runs CPU synthesis (`AudioThread_Update`), and waits up to 32 ms for the task (`src/code/audio_thread_manager.c:28-68`). Drops extra queued retraces (`src/code/audio_thread_manager.c:109-115`). | Game-side `Audio_Update` runs on the graph thread (`src/code/graph.c:259-261`). |
| graph | 9 | `src/code/main.c:101` | Does not wake on retrace. | The whole game loop (below). |
| slowly | 5 | `src/code/PreRender.c:723` | Background PreRender filtering. | |

Audio synthesis runs per field and is independent of the divisor. Samples per update are `samplingFreq / refreshRate` (`src/audio/lib/heap.c:945-946`). So audio output is already paced by VI time, not by ticks (inferred).

### The graph loop, one tick

1. `Graph_UpdateGame` reads input, increments `state.frames` and runs `Audio_Update` (`src/code/graph.c:256-262`, `src/code/game.c:172-175`).
2. `Graph_SetNextGfxPool` picks pool `gfxPoolIdx % 2` and framebuffer `framebufferIndex % 2` (`src/code/graph.c:53-85`).
3. `GameState_Update` calls `main` (logic then draw for Play) and `GameState_Draw` (`src/code/game.c:153-170`). Draw is skipped for one tick when `R_PAUSE_BG_PRERENDER_STATE == PROCESS` (`src/code/game.c:164-167`).
4. The display lists are closed. The master list ends with `gDPFullSync` (`src/code/graph.c:282-305`). Pool magic and overflow are checked (`src/code/graph.c:309-339`).
5. `Graph_TaskSet00` **blocks** for the previous task's done message, with a 3 s timeout that retries or crashes (`src/code/graph.c:157-180`). This is the only place the CPU waits for the RCP. It waits after the build, so the CPU builds tick N while the RCP runs tick N-1 (cited for the order; the overlap is inferred).
6. The task gets `OS_SC_SWAPBUFFER` and a `CfbInfo` with `updateRate = framerateDivisor` (`src/code/graph.c:209-243`). It is sent to the scheduler (`src/code/graph.c:251-253`).
7. The RSP, RDP and period accumulators are copied into `gRSPGfxTimeTotal`, `gRSPAudioTimeTotal` and `gRDPTimeTotal`, and `gGraphUpdatePeriod` is set (`src/code/graph.c:347-361`).

### Scheduler gating

- A pending audio task makes a running gfx task yield (`src/code/sched.c:411-415`).
- A gfx task that needs the RDP starts only when RSP and RDP are both idle and `Sched_TaskFramebuffersValid` passes (`src/code/sched.c:286-294`). The check fails while any swap is pending, or while the target framebuffer is on screen and not about to be replaced (`src/code/sched.c:243-253`).
- `gRDPTimeAcc` is assigned, not summed, as RDP done minus gfx RSP start (`src/code/sched.c:390`, `src/code/sched.c:529`). It includes time the RDP waits on the RSP (cited in `tools/bench/README.md:91`).

## 2. The framerate divisor

`GameState_SetFramerateDivisor` writes `R_UPDATE_RATE`, the state field and the globals `gFramerateDivisor`, `gFramerateDivisorF`, `gFramerateDivisorHalf` (div/2) and `gFramerateDivisorThird` (div/3) (`src/code/game.c:33-44`). Every `GameState_Init` sets 3 first (`src/code/game.c:243`).

| Game state | Divisor | Where |
|---|---|---|
| Play | 3 | Default, `src/code/game.c:243` |
| Play, tile transition ready | 1 | `src/code/z_play.c:956` |
| Play, back after transitions | 3 | `src/code/z_play.c:740`, `787`, `821`, `850`, `885`, `908` |
| Pause menu open | 2 | `src/code/z_kaleido_setup.c:137`; owl-warp ocarina `src/code/z_message.c:5801` |
| Pause menu close | 3 | `src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c:3173` and 4 more sites |
| Opening (TitleSetup) | 1 | `src/overlays/gamestates/ovl_opening/z_opening.c:55` |
| Console logo | 1 | `src/overlays/gamestates/ovl_title/z_title.c:160` |
| Map select | 1 | `src/overlays/gamestates/ovl_select/z_select.c:1090` |
| File select | 1 | `src/overlays/gamestates/ovl_file_choose/z_file_choose_NES.c:2512` |
| Day telop | 1 | `src/overlays/gamestates/ovl_daytelop/z_daytelop.c:229` |
| PreNMI | 1 | `src/code/z_prenmi.c:54` |

Divisor-1 states already show a new image every field. Only Play (3) and pause (2) need interpolation (inferred from the table).

### What scales by the divisor

| System | Mechanism | Where |
|---|---|---|
| Actor position | `sActorMovementScale = div * 0.5`; `Actor_UpdatePos` multiplies velocity by it. Gravity and collision displacement are not scaled. | `src/code/z_actor.c:1202-1217`, set at `src/code/z_play.c:941` |
| Actor SkelAnime | `updateRate = gFramerateDivisorThird` | `src/code/z_skelanime.c:1678` (and 5 more to 1793) |
| Player animation | `framerateDivisor * 0.5` | `src/code/z_skelanime.c:1321-1372`, `1578` |
| FCurve skeletons | `playSpeed * div * 0.5` | `src/code/z_fcurve_data_skelanime.c:117` |
| Rotation steps | `Math_ScaledStepToS` step times Half | `src/code/z_lib.c:69-80` |
| SubS tracking | step times Half | `src/code/z_sub_s.c:1041` |
| Player special moves | `R_UPDATE_RATE / 2` | `src/overlays/actors/ovl_player_actor/z_player.c:10001`, `10130`, `10270`, `10824` |
| A few actors | Half or field times 0.5 | `ovl_En_Ot/z_en_ot.c:862`, `ovl_En_Dno/z_en_dno.c:845`, `ovl_En_Goroiwa/z_en_goroiwa.c:1002` |
| Letterbox | step 10 at div 3, else 30/div | `src/code/z_shrink_window.c:62-63` |
| Fades and wipes | fade timer += rate; wipes use `*3/updateRate` | `src/code/z_fbdemo_fade.c:80`, `src/overlays/fbdemos/ovl_fbdemo_wipe1/z_fbdemo_wipe1.c:62` |
| Display hold | `cfb->updateRate` | `src/code/graph.c:240` |
| Effect budget | `div * gIrqMgrRetraceTime - gRDPTimeTotal` | `src/code/game.c:301` |

Most logic is per tick and does not scale: camera (no divisor reference in `src/code/z_camera.c`, by grep), clock time (`src/code/z_kankyo.c:1212-1214`), gravity, frame-count timers and `Animation_OnFrame` (`src/code/z_skelanime.c:2018-2019`). Setting Play to divisor 1 therefore does not give 60 fps at the same speed. Part of the game would stay at the same speed and the rest would run 3 times as fast (inferred). `gFramerateDivisor` and `gFramerateDivisorF` are written but never read (`grep -rnw` over `src` and `include`).

### The effect budget, `func_80173B48`

`src/code/game.c:300-303` returns `div * gIrqMgrRetraceTime - gRDPTimeTotal` in ns. At divisor 3 that is about 50 ms minus the last tick's RDP time (inferred from the formula). Consumers:

- Rain drop count: `budget / 3e7`, clamped, minimum 5 (`src/code/z_kankyo.c:2080-2085`). The rain draw then calls `Rand_ZeroOne` per drop (`src/code/z_kankyo.c:2122-2123`, `2143`).
- Snow density: smooth-steps a static toward `budget / 1.4e7` (`src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c:521-523`).
- Link's dust and grass particles: `budget / 2e7` on snow (`src/overlays/actors/ovl_player_actor/z_player.c:10320`), others nearby to `10352`. Each particle consumes RNG and spawns an EffectSs.

Leave this function on the logic divisor and the logic tick's RDP time. If it reads the render rate, budgets fall to 16.7 ms or less, and particle counts and the RNG stream change (inferred).

## 3. Framebuffer chain and how lag appears

- **Two framebuffers.** `gFramebuffers[2]` (`src/code/sys_cfb.c:8`). `SysCfb_GetFramebuffer` returns NULL for index 2 or higher (`src/code/sys_cfb.c:123-128`). Index 1 is `gLoBuffer.framebuffer`. Index 0 is `gHiBuffer.framebuffer` (`include/buffers.h:10-34`). Each is 320x240x16-bit, 153,600 bytes.
- **Fixed addresses.** `gLoBuffer` is at 0x80000500 and `gHiBuffer` at 0x80780000 (cited from the matching build's map, `expected/build/n64-us/mm-n64-us.map`, in the coordinator checkout). `FRAMEBUFFERS_START_ADDR` is asserted to 0x80780000 (`include/buffers.h:53-55`).
- **Z-buffer and work buffer.** These are `malloc`'d from the system heap when the graph thread starts, 153,600 bytes each plus alignment (`src/code/graph.c:385-388`). The hi-res Z-buffer is never set, so `gZBufferPtr` is NULL in hi-res mode (checked at `src/code/z_play.c:1482`).
- **Swap.** When a task completes, `Sched_TaskComplete` sets `pendingSwapBuf1`. It swaps at once only if the shown buffer's `updateTimer` is already 0 (`src/code/sched.c:307-331`). Otherwise each retrace decrements the timer and swaps at 0 (`src/code/sched.c:439-446`). `osViSwapBuffer` resets `updateTimer = updateRate` (`src/code/sched.c:45-48`). VI mode changes are applied at the same point (`src/code/sched.c:50-57`).
- **Pipeline depth.** One image is on screen, one task is pending or running, and the CPU builds one more. `pendingSwapBuf2` exists but is unused (`include/scheduler.h:47`; inferred from grep).
- **Lag.** If no buffer is pending when the timer reaches 0, the VI keeps showing the old image. The late task then swaps on completion (`src/code/sched.c:310-314`), so that image lasts 4 or more fields. Later ticks are not shortened to catch up, and there is no frame skip (inferred from the same lines). The bench `vi` column reads above the divisor on lag ticks (`tools/bench/README.md:86`).
- **Skipped swaps.** Pause-background, picto and transition-tile captures set `SREG(33) |= 1` (`src/code/z_play.c:1420`). That drops `OS_SC_SWAPBUFFER` for one task and rewinds `framebufferIndex` (`src/code/graph.c:212-216`).

**Consequence for 60 fps (inferred).** At `updateRate = 1` with two buffers, the next task cannot start until the previous image is on screen (`src/code/sched.c:247-249`). The RCP gets about one field per image. A third buffer would remove that coupling. It needs `gFramebuffers[3]`, the index check, every `% 2` site (`src/code/graph.c:74`; `src/code/z_play.c:1459`, `1471`) and probably a second pending slot.

## 4. Draw-time state mutation hazard table

Method:

- **Core engine (`src/code`).** Traced by hand from `Play_Draw` (`src/code/z_play.c:1449`).
- **Overlays.** Scanned with a script that finds every draw-reachable function, follows calls and limb callbacks, and flags persistent writes. It covers 618 overlays, 1,584 draw-reachable functions and 476 limb callbacks. 489 functions in 249 overlays write persistent state. 231 functions in 135 overlays do something that is not idempotent: accumulate, RNG, SFX, spawn, animate, smooth-step, register a collider or set up lights. In a hand check of 11 hits, all 11 were real writes, and 8 were real hazards (cited from the scan; script path in the report).

Severity is **inferred** in every row.

- **High**: changes logic state, the RNG stream or the collision lists, or repeats an event (SFX, spawn). It breaks per-tick bit-identity with retail.
- **Med**: animation or fade speed depends on the draw count. Logic is safe, but the image on the keyframe field differs from retail (F1).
- **Low**: last-writer-wins or idempotent. Safe only if the render pass writes the same value each time.

"Read by logic" means an `Update` path reads the value on the next tick.

### Play frame plumbing (`src/code/z_play.c`, `src/code/game.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Cutscene manager | `src/code/z_play.c:1517-1518` | `CutsceneManager_Update` runs after draw in `Play_Main`. It is logic. | High |
| Camera in draw | `src/code/z_play.c:1433-1439` | Full `Camera_Update` of the active camera when `view.unk164` is set (set at `src/code/z_camera.c:4226`, `4468`, `4510`, `5089`). | High |
| View matrices | `src/code/z_play.c:1192-1204` | `billboardMtxF`, `viewProjectionMtxF` and `projectionMtxFDiagonal` written into PlayState. Read by logic: `Attention_Update` (`src/code/z_actor.c:707`), `Actor_GetScreenPos` (`src/code/z_actor.c:2187-2205`), `Play_GetScreenPos` (`src/code/z_play.c:1608`), sound sources (`src/code/z_sound_source.c:22`). | High |
| Stars rebuild matrices | `src/code/z_kankyo.c:3183-3185` | Overwrites `billboardMtxF` and `viewProjectionMtxF` again with untransposed values. | High |
| Pause bg capture | `src/code/z_play.c:1397`, `1260-1266`, `1142-1145` | `R_PAUSE_BG_PRERENDER_STATE` SETUP to PROCESS to READY. PROCESS skips the next draw (`src/code/game.c:164`). | High |
| Picto capture | `src/code/z_play.c:1400-1403` | `R_PICTO_PHOTO_STATE` to PROCESS; Update consumes it (`src/code/z_play.c:934-939`). | High |
| Transition tile | `src/code/z_play.c:1405` | `gTransitionTileState` to PROCESS; Update consumes it (`src/code/z_play.c:948-956`). | High |
| Skip swap | `src/code/z_play.c:1419-1420` | `unk_18B49 = 2`, `SREG(33) |= 1`. | High |
| Motion blur | `src/code/z_play.c:53-93` | Status machine; blends the previous image each draw, so trail decay is per image. | Med |
| Game-over lights | `src/code/z_play.c:1107-1109` to `src/code/z_kankyo.c:2593-2631` | `sGameOverLightsIntensity += 2`, `adjLightSettings` decremented, `fillScreen` set. | Med |
| Segments | `src/code/z_play.c:1160-1162` | `gSegments[2,4,5]`; Update rewrites them (`src/code/z_play.c:930-932`). | Low |

### View and camera (`src/code/z_view.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Distortion lerp | `src/code/z_view.c:258-284` | `curDistortionOrientation` and `curDistortionScale` lerp toward the target on each `View_Apply`. | Med |
| Perspective | `src/code/z_view.c:334-362` | `view->viewing` and `view->projection` stored; reused by star draw and logic. | Low |
| Eye nudge | `src/code/z_view.c:356` | `eye.z += 2` when eye equals at; compounds. | Med |

### Actors core (`src/code/z_actor.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Projection | `src/code/z_actor.c:3190-3191` | `projectedPos` and `projectedW`. Used for SFX positions, and 81 overlay files reference it (grep). | High |
| Culling flag | `src/code/z_actor.c:3197-3201` | `ACTOR_FLAG_INSIDE_CULLING_VOLUME`. `Actor_UpdateAll` uses it to decide which actors update (`src/code/z_actor.c:2709-2711`). | High |
| isDrawn | `src/code/z_actor.c:3203` | Reset each draw, then set when drawn. Read for delete decisions. | High |
| Flagged audio | `src/code/z_actor.c:3193-3195`, `2871-2899` | `Audio_PlaySfx` and `AudioSfx_PlaySfx` per draw. | High |
| Lens mask | `src/code/z_actor.c:3227-3233` | `Math_StepToC(lensMaskSize, ...)` per draw; `Actor_DeactivateLens` writes magic state. | Med |
| Shadow raycast | `src/code/z_actor.c:344-352` | `floorHeight`, `floorPoly` and `floorBgId` set by a BgCheck raycast. | High |
| Foot shadows | `src/code/z_actor.c:252-327` | `shape.feetFloorFlags` (read by `z_player.c:16294`); `EffFootmark_Add` (`src/code/z_actor.c:272`). | High |
| Attention reticle | `src/code/z_actor.c:545-575` | Fade alpha `-= 120`; reticle ring index moves each draw. | Med |
| Damage effects | `src/code/z_actor.c:5261`, `5299-5303` | `Rand_CenteredFloat` and `Rand_ZeroFloat` on the global RNG. | High |

### Player limb callbacks (`src/code/z_player_lib.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Leg IK | `src/code/z_player_lib.c:2162-2166` | Writes `skelAnime->jointTable[...]`, which it read earlier. Repeat draws compound. Also raycasts and `EffectSsGFire_Spawn` (`2171`). | High |
| Sword AT | `src/code/z_player_lib.c:2694-2719` | `Collider_SetQuadVertices` and `CollisionCheck_SetAT`. `SetAT` appends without dedupe (`src/code/z_collision_check.c:1179-1180`). An unchanged second draw calls `Collider_ResetQuadAT` (`2710`). | High |
| Shield AC/AT | `src/code/z_player_lib.c:2735-2748` | Quad vertices plus `SetAC` and `SetAT` per draw. | High |
| Sword trail | `src/code/z_player_lib.c:2898-2916` | `EffectBlure_AddVertex` per draw. | Med |
| Counter | `src/code/z_player_lib.c:2985` | `player->unk_ADD++`. | High |
| Fog phase | `src/code/z_player_lib.c:136` | `player->unk_B5F += ...`. | Med |
| Body parts | `src/code/z_player_lib.c:3717`, `4114` | `bodyPartsPos[]` and `focus.pos` from the matrix. Read by logic. | High |
| Hands and shield | `src/code/z_player_lib.c:3721`, `3779`, `3833` | `leftHandWorld.pos`, `leftHandMf`, `shieldMf`. Read by logic. | High |
| Held actor | `src/code/z_player_lib.c:3743` | Writes another actor's `world.pos`. | High |
| Stray-fairy mask | `src/code/z_player_lib.c:3414`, `3435` | RNG plus `EffectSsKirakira` spawn. | High |
| Circus Leader's mask | `src/code/z_player_lib.c:3253-3254` | `EffectSsDtBubble` spawn and budget `-= 400`. | High |
| Great Fairy mask | `src/code/z_player_lib.c:3438-3545` | Hair chain physics integrates per draw. | Med |

### Skin and skelanime

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Skin vertex parity | `src/code/z_skin.c:132` | `vtxEntry->index` flips each draw. `Skin_GetVertexPos` reads it (`src/code/z_skin.c:271`), called from Epona logic (`ovl_En_Horse/z_en_horse.c:4483`). | High |
| Skin limb matrices | `src/code/z_skin_awb.c:149-200` | Global `gSkinLimbMatrices` (`src/code/z_skin.c:11`). `Skin_GetLimbPos` reads it (`src/code/z_skin.c:262-267`) for Epona colliders and rider position. | High |
| SkelAnime draw | `src/code/z_skelanime.c:30-860` | Matrix stack and GRAPH_ALLOC only. The callbacks are the hazard. | Low |

### Environment (`src/code/z_kankyo.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Skybox change | called `src/code/z_play.c:1287`; `src/code/z_kankyo.c:1052`, `1078`, `1086-1110` | `changeSkyboxTimer--`, state, async DMA kick, `skyboxBlend`. | High |
| Lightning strike | `src/code/z_kankyo.c:2251-2327` | `Rand_ZeroOne` (`2256`, `2260`, `2270`); strike state and timers; ambient color; `Audio_SetAmbienceChannelIO` (`2288`). | High |
| Lightning bolts | `src/code/z_kankyo.c:2354-2402` | Bolt state, `delayTimer--`, texture index, `Rand_ZeroOne`. | High |
| Rain | `src/code/z_kankyo.c:2122-2143` | `Rand_ZeroOne` per drop. | High |
| Lens flare | `src/code/z_kankyo.c:1933-1936`, `1976-1978` | `sSunDepthTestX/Y`; smooth-step of `lensFlareAlphaScale`. The graph callback reads the depth test after the task (`src/code/z_kankyo.c:532-537`, run at `src/code/graph.c:188-190`). | Med |
| Sandstorm | `src/code/z_kankyo.c:2781-2834`, `2864-2865`, `2940` | `sandstormState` advances; alpha steps; scroll `+=`. | High |
| Stars | `src/code/z_kankyo.c:3224-3243` | Seeded LCG on `gRandFloat` only, not the logic RNG. | Low |

### Effects, scene, room

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Spark | `src/code/z_eff_spark.c:202` | `Rand_ZeroOne` on the global RNG (`src/boot/libc64/qrand.c:47-51`). | High |
| EffectSs bounds | `src/code/z_effect_soft_sprite.c:271-274` | Deletes out-of-bounds effects in draw. Idempotent. | Low |
| Room image | `src/code/z_room.c:376` | Writes `player->actor.params` low byte. Idempotent. | Low |
| Animated materials | `src/code/z_scene_proc.c:414-449` | Steps on `gameplayFrames`, so they freeze within a tick. | Med |

### Interface and message (`src/code/z_parameter.c`, `src/code/z_message.c`)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Timers | `src/code/z_parameter.c:5663-6000`, called `6650` | Timer state machine, `gSaveContext.timer*`, `health = 0` (`5960`), moon crash `day = 4` and `transitionTrigger` (`5868-5875`), countdown SFX (`5971-5980`). | High |
| C-up blink | `src/code/z_parameter.c:4105-4109` | `sCUpTimer--`. | Med |
| Clock | `src/code/z_parameter.c:4590-4976` | Clock alpha, diamond and final-hours color timers. | Med |
| Picto box | `src/code/z_parameter.c:6697-6716` | Compresses the photo into `gSaveContext`; `Message_StartTextbox`; `player->stateFlags1 |=`. | High |
| Message state | `src/code/z_message.c:4249-5204` | `msgMode`, `stateTimer--`, `textDrawPos += textDelay` (`4317`, `4327`), ocarina mode and song, `SET_EVENTINF` (`4530`), `Audio_PlaySfx`. | High |
| Text draw | `src/code/z_message.c:1148-1530` | `textDrawPos++`, `msgMode`, `textDelayTimer`, character SFX. | High |

### Overlays (representative; the full list comes from the scan)

| System | file:line | What mutates | Sev |
|---|---|---|---|
| Limb focus and bodyPartsPos | `Matrix_MultVec3f(&this->actor.focus.pos)` in about 105 overlays; `bodyPartsPos[]` in about 59 (scan count) | Last writer wins. Read by targeting and effects. | High |
| Collider spheres | `Collider_UpdateSpheres` in about 35 PostLimbDraws, for example `ovl_En_Wf/z_en_wf.c:1583` | Collider geometry from the drawn pose. | High |
| Epona OC | `ovl_En_Horse/z_en_horse.c:4659` | `CollisionCheck_SetOC` in PostDraw. | High |
| Epona dust | `ovl_En_Horse/z_en_horse.c:4508` | RNG sets `dustFlags`. | High |
| Iron Knuckle | `ovl_En_Ik/z_en_ik.c:1039` | `Rand_Next` in PostLimbDraw. | High |
| Wizrobe | `ovl_En_Wiz/z_en_wiz.c:1429` | `SoundSource_PlaySfxAtFixedWorldPos` in PostLimbDraw. | High |
| Bio Deku Baba | `ovl_En_Pp/z_en_pp.c:1570` | `Actor_Spawn` in PostLimbDraw, capped at 6. | High |
| Dinofos | `ovl_En_Dinofos/z_en_dinofos.c:1536` | `EffectSsDFire_Spawn` in PostLimbDraw. | High |
| Stalchild | `ovl_En_Hint_Skb/z_en_hint_skb.c:907` | `Actor_SpawnBodyParts`. | High |
| Pushblock | `ovl_Obj_Oshihiki/z_obj_oshihiki.c:375` | `world.pos.x += underDistX` in draw. | High |
| Door warp | `ovl_Door_Warp1/z_door_warp1.c:961` | `SkelAnime_Update` in draw. | High |
| Majora's Mask | `ovl_Boss_07/z_boss_07.c:6755` | Tentacle physics simulated from draw. | High |
| Goht | `ovl_Boss_Hakugin/z_boss_hakugin.c:3149` | `sCurrentMalfunctionType--`. | High |
| River sound | `ovl_En_River_Sound/z_en_river_sound.c:87` | `Actor_PlaySfx` from draw. | High |
| Water wheel, chest wall | `ovl_Bg_Dblue_Movebg/z_bg_dblue_movebg.c:856`, `ovl_Obj_Takaraya_Wall/z_obj_takaraya_wall.c:490` | SFX from draw. | High |
| Fishing | `ovl_En_Fishing/z_en_fishing.c:1778`, `1919` | `sLurePos.x +=`; `D_80911E28--`. | High |
| Skullfish | `ovl_En_Sb/z_en_sb.c:431`, `416` | `colorFilterTimer++`; `actor.draw = NULL`. | High |
| Ice damage ramps | `ovl_En_Bsb/z_en_bsb.c:1792` and about 8 enemies | `drawDmgEffScale += 0.3f`. | Med |
| NPC head tracking | `ovl_En_Dnk/z_en_dnk.c:347`, `ovl_Obj_Um/z_obj_um.c:1841`, `ovl_En_Jg/z_en_jg.c:1009` | `Math_SmoothStepToS` on persistent rotation in limb draw. | Med |
| Texture scroll | `ovl_En_An/z_en_an.c:1329`, `ovl_En_Fall/z_en_fall.c:851`, `ovl_En_Mag/z_en_mag.c:734` | Scroll counters `++` or `+=`. | Med |
| Song of Time clock | `ovl_En_Test6/z_en_test6.c:1285` | `clockAngle += clockSpeed`. | Med |
| Poe Sisters | `ovl_En_Po_Sisters/z_en_po_sisters.c:1172-1175` | RNG plus light setup. | High |
| World map | `ovl_kaleido_scope/z_kaleido_map.c:691` | `gSaveContext` cloud visibility `|=`. | High |
| Boss local RNG | `ovl_Boss_01/z_boss_01.c:3556`, `ovl_Boss_06/z_boss_06.c:699`, `ovl_Boss_07/z_boss_07.c:3715` | Reseeded at the top of draw. Deterministic. | Low |
| Per-draw cursors | `src/code/z_player_lib.c:2427` (reset at `2375`) | `sPlayerCurBodyPartPos++`, reset each draw. | Low |

All overlay paths above are under `src/overlays/actors/`, except the world map, which is under `src/overlays/kaleido_scope/`.

### Draw that reads tick counters

`gameplayFrames` increments once per unpaused tick (`src/code/z_play.c:987`). Draw reads it in animated materials (`src/code/z_scene_proc.c:414-449`), the attention arrow (`src/code/z_actor.c:630`), damage effects (`src/code/z_actor.c:5084`) and 230 overlay sites (grep). The sandstorm reads `state.frames` (`src/code/z_kankyo.c:2755`). Within a tick these stay constant, so the animation steps once per tick unless the render pass interpolates the counter (inferred).

## 5. Timing feedback paths

Real-time reads, grouped by what they affect. A faster ROM changes each of them.

| Group | Read | Decision | Where | Live in retail |
|---|---|---|---|---|
| Logic | `Rand_Seed(osGetTime())` | Global RNG seed at each Play entry | `src/code/z_play.c:2264-2268` | Yes. BENCH uses a fixed seed. |
| Logic | `osGetTime` | Minigame, moon-crash and postman timers; moon crash at timer 0 | `src/code/z_parameter.c:5663-6118`, in `Interface_DrawTimers` | Yes |
| Logic | `osGetTime` | Bottle timers (hot spring water) | `src/code/z_parameter.c:6125-6187` | Yes |
| Logic | `osGetTime`, per field | Postman 10-second A press, sampled in the PadMgr retrace callback | `src/code/z_parameter.c:823-832`, run at `src/code/padmgr.c:672-675` | Yes |
| Logic | `osGetTime` | Flash write treated as done after 2 s | `src/code/z_sram_NES.c:1925` and `1940`, `1955`, `1978` | Yes |
| Logic and visual | `gRDPTimeTotal`, `gIrqMgrRetraceTime` | Effect budget: rain, snow, dust, so RNG draw count | `src/code/game.c:301` | Yes |
| Visual | Z-buffer read after the task | Sun glare occlusion | `src/code/z_kankyo.c:532-537` | Yes |
| Audio | `osGetCount` | `audioRandom` per audio task; random velocity, gate and branch in sequences | `src/audio/lib/thread.c:156` | Yes |
| Audio | `osGetCount` | `AudioThread_NextRandom` | `src/audio/lib/thread.c:964-973` | Yes |
| Audio | audio task count (per field) | Ocarina note timing | `src/audio/code_8019AF00.c:3229` | Yes |
| Debug | RSP, RDP and period totals | Speed meter, only if `R_ENABLE_ARENA_DBG` | `src/code/game.c:114-115` | No |
| Debug | `osGetTime` | Graph 3 s hang timer | `src/code/graph.c:159` | Watchdog only |

The HUD and bottle timers run on wall time. The game logic they gate (health to 0, moon crash) runs inside draw, so the draw pass is the one that moves those states (`src/code/z_parameter.c:5960`, `5868-5875`).

## 6. Memory

| Item | Size | Where |
|---|---|---|
| Framebuffers | 2 x 153,600 B | `gLoBuffer` 0x80000500, `gHiBuffer` 0x80780000 (cited from map) |
| `gLoBuffer` extra | `skyboxBuffer` 0x5A360 B | `include/buffers.h:14` |
| `gHiBuffer` extra | `pictoPhotoI8` plus `D_80784600` (0x56200 B, coverage save) | `include/buffers.h:30-31`, used at `src/code/z_play.c:2256-2258` |
| Z-buffer plus work buffer | 2 x 153,600 B + 63, system heap | `src/code/graph.c:385-388` |
| RDP output buffer | 0x18000 B, system heap | `src/code/graph.c:390` |
| Gfx pools | 2 x 0x20310 B (131,856) | `include/gfx.h:33-42`. POLY_OPA 105,472, POLY_XLU 16,384, OVERLAY 8,192 B |
| Audio heap | 0x138000 B | `include/buffers.h:24` |
| System heap | `SEGMENT_END(buffers)` to 0x80780000 | `src/code/main.c:66-71`. 0x803824c0 to 0x80780000 = 4,184,896 B (cited from map) |
| Expansion Pak | Required | `src/code/sys_initial_check.c:106-118` |
| Play arena | Largest free heap block, then all of it to ZeldaArena | `src/code/game.c:206-214`, `src/code/z_play.c:2154`, `2312-2317` |

The fixed buffers have no free space. Every region is used, and hi-res mode uses each whole union (`include/buffers.h:10-34`). Any extra buffer comes from the system heap, and because Play gives the whole remaining heap to ZeldaArena, it comes out of ZeldaArena (inferred from the cited allocation chain).

- **One extra framebuffer** costs 153,600 B (150 KiB). The lowest bench ZeldaArena minimum is 695 KiB, in Termina Field (`tools/bench/README.md:129`, uncalibrated runner, BENCH build with a heap 66.7 KiB smaller than retail per `tools/bench/README.md:109`). About 545 KiB would remain (inferred). The worst case over the whole game is unknown.
- **A third gfx pool** would cost 131,856 B more. Two pools are enough if each render pass is its own `Graph_Update` (inferred, `src/code/graph.c:54`).

## Gotchas

- **Draw is not read-only.** Play's draw path runs the textbox and ocarina state machine, timers that can kill Link or start the moon crash, the camera, RNG and collider registration. Pure re-rendering needs a render-only path that skips all of that. Nothing like it exists today.
- **Update reads the last draw.** Culling (`src/code/z_actor.c:3197-3201` to `2709-2711`), view matrices, `projectedPos`, `bodyPartsPos`, `focus.pos`, Player's sword and shield colliders, and Epona's skin matrices are all written in draw and read by the next tick's logic. If interpolated frames write them, logic sees the last interpolated pose, not the logic pose. For F1, those writes must come from the keyframe pass, or from a pass that writes the same values retail does.
- **Blank input in draw.** `Play_Draw` runs with controller 1 replaced by an empty `Input` (`src/code/z_play.c:1508-1513`). Draw code that reads input sees nothing.
- **Input edges.** PadMgr ORs presses across fields and clears them on read (`src/code/padmgr.c:517-520`, `728-733`). A render pass that calls `GameState_GetInput` would steal presses from the logic tick.
- **Divisor coupling.** The divisor is both the logic step and the display hold time. Changing it alone changes game speed.
- **Effect budget.** It reads RDP time. A faster RDP gives more rain and dust and a different RNG stream. Fidelity needs it to stay on the retail value, or on a value that does not depend on time.
- **Retail RNG seed.** Play seeds the RNG from `osGetTime` (`src/code/z_play.c:2267`). Tests of per-tick bit-identity must use the BENCH seed.
- **Pause is divisor 2.** Kaleido needs its own interpolation, or a 2-to-1 render split (`src/code/z_kaleido_setup.c:137`).

## Verification

- **Citation existence.** A checker script extracted all 262 backticked `file:line` citations. It confirmed that each file exists and each line range is inside the file, and printed the first cited line of each. 262 of 262 resolved. I read every printed line against its claim and fixed 5 that pointed at the wrong file or started one line early.
- **Full reads.** I opened and read about 60 of the cited ranges in full. These include the scheduler swap and retrace path, `Graph_TaskSet00`, `Play_Main`, `Play_DrawMain`, the thread priorities, `IrqMgr_HandleRetrace`, `func_80173B48` and its rain, snow and dust consumers, the PadMgr edge clear, the audio random sources, Player's sword collider and leg IK, skin parity, view distortion, the skybox timer, star matrices, moon crash, culling, flagged audio, and the pushblock.
- **Greps.** I reran these myself: `gFramerateDivisor` and `gFramerateDivisorF` have no readers outside `src/code/game.c` and their `extern` lines. `pendingSwapBuf2` has no use outside its declaration. 81 overlay files read `projectedPos`. There are 230 `gameplayFrames` sites in overlays. `src/code/z_camera.c` has no divisor reference.
- **Overlay scan.** The overlay rows come from a scan whose output records the source text of each hit. I reran it and got the same counts. Rows I did not open by hand are labeled with scan counts.
- **Not verified.** Nothing was built or run. libultra VI swap timing, that is, which field first shows a swapped buffer, was not read. The addresses 0x80000500, 0x80780000 and 0x803824c0 come from the matching-build map in the coordinator checkout, not from a build of this branch.
