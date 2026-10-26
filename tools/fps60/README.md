# fps60 harness

The fps60 harness measures how a `BENCH=1` build of this tree presents images, runs its logic ticks, and loads the CPU, RSP and RDP. It runs each scene of a fixed corpus on the pinned ares runner and writes per-field data, every image with its tick and image index, load per field and per tick, and summaries. `fidelity.py` compares two runs tick by tick (check F1). `golden.py` compares every image of a run against the golden set (check F2). `stale_reads.py` measures a candidate's stale-read exposure against a same-layout control (preferences line 26). The program's rules are in `~/.claude/orchestrate/mm-60fps/preferences.md`. After unit H1 lands, `tools/fps60/` is frozen (preferences line 7).

## Run

```sh
tools/fps60/worktree-setup.sh UNIT BRANCH [BASE_REF]   # prints ~/repos/mm-wt/UNIT, ready to build
make -j32 BENCH=1                                       # build/n64-us-bench/mm-n64-us.z64 and .map
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --noise-pin off   # perf runs: retail RDP noise
tools/fps60/fidelity.py BASE_RESULTS CAND_RESULTS
tools/fps60/golden.py record RESULTS        # F2: write tools/fps60/golden.tsv from the accepted reference run
tools/fps60/golden.py check CAND_RESULTS    # F2: every image against the golden set
tools/fps60/run.py ROM --out RESULTS --noise-pin noise-only                # base, candidate and control, then:
tools/fps60/stale_reads.py BASE_RESULTS CAND_RESULTS CONTROL_RESULTS
tools/fps60/test_harness.py                 # host tests of the F1 tick-set rule, F2 and the stale-read verdict
```

- `worktree-setup.sh` creates the worktree (or reuses it), copies the untracked build inputs from `~/repos/mm-decomp-60fps` (baseroms, `extracted/`, the IDO recompilation, `.venv`, `.make_options.mk`), and builds `tools/`. It only reads the main checkout. It copies rather than hard-links, so `make setup` in a worktree cannot write into the main checkout. A worktree takes 723 MB before its first build and 1.19 GB after a `BENCH=1` build. Setup took 7 s on a new worktree and 1 s on a rerun (measured on unicron).
- `run.py` reads symbols from the map beside the ROM (`--map` overrides). It checks that the runner's sha256 starts with `1f9d59f6` (set `FPS60_ANY_RUNNER=1` only for experiments). It runs at most 4 runner processes (`--jobs`, preferences line 14), and `--scenes title,sct` selects scenes.
- A full corpus run took 149 s of wall time with 4 jobs, and each scene took 41 to 102 s (measured on unicron, see `docs/fps60/baseline.md`).
- `run.py --check-determinism` runs the corpus into `RESULTS/run1` and `RESULTS/run2` and fails unless every file except `wall.tsv` and `host.txt` is byte-identical.
- `run.py --noise-pin on|off|check|noise-only` sets the noise pin (see Noise pin below). The default is `on`, for fidelity runs. Perf runs use `off`. `check` is `on` plus a second walk of every gfx task, which must give the same record and patch nothing. `noise-only` neutralizes noise but leaves the pipeline mode as emitted, for `stale_reads.py` (see Stale reads). With the pin on, `run.py` fails a scene whose walk met an unresolved target, an unknown opcode, an overflow or a check mismatch, and prints the totals.
- `run.py --load-pin on|off` sets the load pin (see Load pin below). The default is `on`, and `off` with `--noise-pin off`, so fidelity runs pin loads and perf runs play as retail. With the pin on, `run.py` fails a scene in which a DMA poll found no issue record or the issue table dropped one of the tick's records.
- `fidelity.py` exits 0 only when every scene passes. Both runs must have the same noise pin (`check` counts as `on`) and load pin settings. On a failure it runs that scene again from both ROMs, dumps both frames, and writes the ares `framediff.py` report under `CAND_RESULTS/fidelity/<scene>/`. The ROM that a result directory names in `meta.json` must still have the recorded sha256, and the rerun must show the recorded `fb_hash` at the dumped field.
- `golden.py` exits 0 on PASS, 1 on FAIL, and 2 when F2 does not apply: a retail-architecture build (no `gBenchCfbKeyframe` in the map) presents keyframes only, which F1 covers. Both commands need the noise pin `on` or `check` and the load pin on, and `record` needs at least one interpolated image in every scene. On a failure `check` dumps the first differing image from the golden run's ROM, while that ROM still exists with its recorded sha256, and from the candidate's, under `CAND_RESULTS/golden/<scene>/`.
- `stale_reads.py` exits 0 when no scene's candidate count exceeds the control's, else 1. All three runs must be `noise-only` with the same load pin. It reruns each scene that differs from each ROM once, dumps every differing image, and counts differing pixels of the runner's 640x480 dumps (each framebuffer pixel is a 2x2 block there).

`run.py` fails loudly when the runner exits nonzero, the stop reason is not `script-stop`, the ROM boots a scene other than the one asked for, the window mark is missing, a peek is missing or unreadable, the stats file is short, or the VI origin does not belong to the framebuffer that the harness attributes to it. It prints the fields, images, and ticks it measured per scene and in total.

## Scenes

`corpus.py` defines the corpus as data. Each scene boots one `BENCH` scene (`sBenchScenes` in `src/code/bench.c`), waits until it settles, and measures a window of 600 fields.

| Scene | Boot | Settles when | Input in the window |
|---|---|---|---|
| `title` | `retail_boot`: the retail route, console logo then the title | the title Play scene has faded in | none |
| `filesel` | `file_select_named`: File 1 and File 2 named "A", cursor on File 1 | the main screen is idle (`CM_MAIN_MENU`) | none |
| `sct` | South Clock Town, day 1 12:00, human Link | the fade-in is done | stick up for 20 ticks, then a circle every 60 ticks |
| `field` | Termina Field | the fade-in and the entrance cutscene are done | stick up |
| `mountain` | Mountain Village (winter) | the fade-in is done | none |
| `greatbay` | Great Bay Coast | the fade-in and the entrance cutscene are done | none |

The routes are mmbench's (`tools/n64-timing/mmbench` in the ares fork) moved to ticks. `field` keeps the stick up because mmbench's turn runs Link into a wall after the entrance cutscene. Neither moving route opens a textbox (checked on frame dumps at window fields 5, 300 and 599).

## How the harness reads the game

A displayed image is a change of `VI_ORIGIN`. To key images by logic tick, the `BENCH` build publishes `gBenchProbe` (`include/bench.h`). `run.py` reads it between fields with the runner's `peek`. A peek logged at `frame=N` reads the state after N fields, so it belongs to stats row N-1.

- `tick` counts logic ticks since boot. `Bench_BeginFrame` increments it at the start of each `Graph_Update`.
- `Bench_TagCfb` (`graph.c`) stores the current tick in `cfbTick[i]` when a task takes `sGraphCfbInfos[i]`. `gScheduler.curBuf` names the `CfbInfo` the VI shows, so `cfbTick[curBuf index]` is the tick of the displayed image. `run.py` checks that `VI_ORIGIN` minus `curBuf->framebuffer` is the same in every window field.
- `settled` and `settleTick` mark the end of the scene's entry. The window starts with the first field after the field in which `settled` became nonzero.
- Input does not come from the runner. `run.py` patches the scene's inputs into `gBenchConfig.inputs`, and `Bench_ApplyInput` (`game.c`, `GameState_GetInput`) writes them into controller 1 from the first tick after `settleTick`. It derives `prev`, `press` and `rel` as `PadMgr_GetInputNoLock` does. An input therefore reaches the same tick in every build, however long ticks take. A runner script cannot guarantee that, because the next tick's `GameState_GetInput` can run within the field in which the previous tick ended.

### Keyframes in a 60 fps build

A retail-architecture build presents one image per logic tick, so every image is a keyframe. A build that presents interpolated images defines `u32 gBenchCfbKeyframe[3]` and `u8 gBenchCfbImage[3]` (declared in `include/bench.h`), parallel to `cfbTick`, and sets them when it binds a `CfbInfo` to an image (architecture.md, Q8). `gBenchCfbKeyframe[i]` holds nonzero for a `CfbInfo` whose image is its tick's final image and zero for an interpolated image. `gBenchCfbImage[i]` holds the image's index, 1 to `d` within its tick, where `d` is the tick's divisor and the keyframe is index `d`. When the map has the symbols, `run.py` peeks both every field, marks only those images as keyframes, and takes the image index from `gBenchCfbImage`. A map with only one of them fails the run. Without them, an image's index is its tick's divisor: the divisor peeked in the last field in which that tick was the latest. Ticks and tick lengths still come from `cfbTick`.

## Output

| File | Contents |
|---|---|
| `meta.json` | ROM, map and runner paths and sha256s, the `gBenchProbe` address, whether the keyframe symbol exists, the noise pin (`noise_pin`, `noise_pin_mode`) and load pin (`load_pin`) settings, the scenes. |
| `summary.tsv` | One row per scene: the columns of `summary.json` below, flattened. |
| `wall.tsv` | Host wall time per scene and in total, the job count, and the 1-minute load average before and after each scene. Not deterministic. |
| `<scene>/fields.tsv` | One row per window field. Columns are listed below. |
| `<scene>/images.tsv` | Every complete image: `image_tick`, `image` (index within its tick), `keyframe`, `first_field`, `fields`, `final_frame`, `fb_hash`, `hash_changes`, and with the noise pin on the noise record columns of `keyframes.tsv`. F2 and `stale_reads.py` read it. |
| `<scene>/keyframes.tsv` | The per-tick image sequence: one row per complete keyframe image, with `image_tick`, `first_field`, `fields` shown, `final_frame` (stats row of its last field), `fb_hash` at its last field, and `hash_changes` (times `fb_hash` changed while it was shown, 0 when the framebuffer was not written while displayed). With the noise pin on, also that tick's walk: `noise_hash`, `noise_cmds`, `noise_patched` and `dl_cmds` (`BenchNoiseRecord`). |
| `<scene>/ticks.tsv` | One row per complete tick: `tick`, `first_field` its first image appears, `fields` until the next tick's first image, `divisor`, and `skipped_after` (ticks after it that never appeared). |
| `<scene>/summary.json` | `noise_pin`, `noise_pin_mode`, `load_pin`, `load_totals` (`BenchLoadTotals` after the window, see Load pin), and with the noise pin on `noise_totals` (`BenchNoiseTotals` after the window), `noise_cmds_per_keyframe_max` and `noise_patched_in_window`. `fields`, `images`, `fields_per_image` (counts at 1 to 5 and 6 or more), `image_share_at_1`, `image_fields_max`, `ticks`, `fields_per_tick`, `lag_ticks`, `skipped_ticks`, `tick_fields_max`, `divisors`, `keyframes`, `first_tick`, `last_tick`, `window_start_frame`, `settle_tick`, `retrace_ticks`. |
| `<scene>/stats.tsv` | The runner's per-field stats for the whole run (the runner's README, Per-field stats). |
| `<scene>/script.txt`, `events.txt` | The generated runner script, and its mark and peek lines. |
| `<scene>/guest.txt` | The ROM's IS-Viewer output (the `BENCH START` line). |
| `<scene>/host.txt` | The runner's stop line and host-time lines. Not deterministic. |
| `load.tsv` | One row per scene: `field_ms` (the median runner field), and for each load resource the max, p99 and p50 per field (`f_`) and per tick (`t_`) as a share of one field, and `hook_ms_p50`. |
| `<scene>/load_summary.json` | Per resource, per field and per tick: `max`, `p99`, `p50` in ms and as a share of one field. Per tick also: CPU time per thread (`tick_cpu`), per graph phase in wall and CPU time (`tick_phase_wall`, `tick_phase_cpu`), per system (`tick_system`), thread switches, the top 10 actors by update plus draw time, and the hook cost. |
| `<scene>/load_fields.tsv` | One row per retrace interval in the window (see Load metrics). |
| `<scene>/load_ticks.tsv` | One row per logic tick that starts and ends inside the window: the `BenchTick` record (`include/bench.h`) plus `cpu_busy`, `rsp_gfx` and `other_threads`. |
| `<scene>/load_actors.tsv` | Graph thread time per actor id over the window, in update and draw, sorted by the sum. |

`fields.tsv` columns:

| Column | Meaning |
|---|---|
| `field` | Window field, 0 to 599. |
| `frame` | Stats row (runner field index). |
| `origin`, `fb_hash` | From `stats.tsv`: `VI_ORIGIN` and the hash of the displayed pixels. |
| `tick` | `gBenchProbe.tick` after the field: the latest logic tick that started. |
| `image_tick` | The logic tick whose image the VI shows. |
| `keyframe` | 1 when the shown image is its tick's final image (always 1 without `gBenchCfbKeyframe`). |
| `image` | The shown image's index within its tick (`gBenchCfbImage`, or the tick's divisor without it). |
| `divisor` | `framerateDivisor` after the latest tick. |

### Metric definitions

- **Fields per displayed image.** A complete image is a run of fields between two `VI_ORIGIN` changes inside the window. The partial images at the window edges are left out. The pass rule is that every image lasts 1 field (`image_share_at_1` = 1).
- **Fields per logic tick.** A tick's length is the number of fields from the first field showing any of its images to the first field showing the next tick's image. This is VI-locked, unlike the start time of `Graph_Update`, which moves with CPU and RDP time inside a field. The first and last tick in the window are left out. A **lag tick** lasts longer than the divisor at its first field. A **skipped tick** never appears. Its time counts toward the tick before it.
- **Load.** See Load metrics below.
- **F1.** `fidelity.py` takes each scene's keyframes by `image_tick`. Every tick with a keyframe on both sides is compared: it passes when the two `fb_hash` values are equal and, with the noise pin on, the two `noise_hash` values are equal. The scene also fails when the candidate has no keyframe for a reference tick inside the reference window, which is every reference tick except its first and last 3 (`EDGE_TICKS`): a window edge can fall on either side of a tick in a run with other timing. The output lists those missing ticks. Ticks only the candidate shows pass and are listed: the reference skipped them, as retail file select does when it lags, so a faster candidate shows more ticks. Both runs must have the same noise pin and load pin settings. The output is pass or fail, the first differing tick and which hash differs there, and the number of differing ticks for each hash.
- **F2.** `golden.py check` takes each scene's images by `(image_tick, image)` and applies the same rules against `golden.tsv`: every common image must have the golden `fb_hash` and `noise_hash`, and every golden image whose tick is inside the golden window must be present. An interpolated image's command stream depends only on the two ticks' recordings and the image index, so its pixels under the noise pin do not depend on timing (architecture.md, Q8): a slow run with late fields records the same hashes as a fast one. Lag is judged by the presentation metric, not by F2. `golden.tsv` starts with a `#` line naming the run, ROM, map and runner it was recorded from.

## Load metrics

The load counters are always on in a `BENCH` build and cost nothing to read: the runner peeks RAM between fields without touching emulated timing. Perf runs use them, with `--noise-pin off`: with the pin on, the load includes the pin's walk on the graph thread and the RDP time that `G_PM_1PRIMITIVE` adds, which retail does not have. Units in the TSVs: CPU times are PClocks (93.75 MHz), RSP times are the runner's 750 MHz units (`XPROFREAD 0x0200`), RDP counters are RCP clocks (62.5 MHz). The summaries convert to ms. A share is ms divided by the median runner field (`stats.tsv` `cpu_cycles` per field, 16.715 ms measured).

### CPU time per thread

`Bench_EnqueueAndYield` (`src/boot/idle.c`) replaces `__osEnqueueAndYield` at its 7 libultra call sites under `#if BENCH` (`osRecvMesg`, `osSendMesg`, `osJamMesg`, `osStartThread`, `osSetThreadPri`, `osStopThread`, `osYieldThread`). Every thread switch in MM goes through one of them, or through an interrupt whose woken thread resumes inside one of them. At each switch, `Bench_CpuSwitch` charges `2 * (COUNT - last)` PClocks to the thread that has been running and records which thread runs next: the run queue's head, or the yielding thread when it goes back on the run queue ahead of that head. When an interrupt preempts a thread, nothing runs the hook until the woken thread resumes in its own yield, and that charge goes to the preempted thread. So each PClock is charged once, to the thread that ran it.

- **CPU busy** = wall time minus the idle thread's time. The idle thread (`Idle_ThreadEntry` after it sets `OS_PRIORITY_IDLE`) runs only when no other thread can, including while the graph thread waits on the RCP in `Graph_TaskSet00`.
- The exception handler's time goes to the thread that was running when the exception was taken, and the dispatch after a switch goes to the thread that runs next. So a thread's time includes the kernel time of the interrupts that hit it. emux has an exception-level cycle counter (`XPROFREAD 0x0001`), but the fork credits pending cycles to it in batches at count flushes (`ares/n64/cpu/cpu.cpp` `stepCount`), so it is not used.
- `switches` counts hook calls (two per switch).
- Graph thread brackets use `Bench_CpuNow`, the running thread's charged time plus the time since its last switch, read with interrupts disabled. A system bracket therefore excludes the time of every thread that preempted the graph thread inside it.

### Per field

`Bench_FieldSnapshot` runs in the scheduler thread at every VI retrace (`Sched_HandleRetrace`) and writes the cumulative counters into a ring of 4 `BenchField` records. It also folds the 24-bit DPC counters into 32-bit totals and clears them, so they never wrap. The runner peeks the whole ring after every field. A runner field can end just before or just after the scheduler takes the snapshot of the retrace at its end, so the harness keys fields by retrace, not by runner field: `load_fields.tsv` has one row per interval between consecutive retrace snapshots, from the latest snapshot at the window's start to the latest at its end (600 intervals in the reference corpus, measured). The interval's own wall time (`wall`) varies with the scheduler's latency by about ±0.7% around the runner field (measured, `snapshot_wall_pclk_min` and `_max` in `load_summary.json`), so a per-field share can exceed 1.0 by that much.

A peek can see a record's cache lines in different states (measured: one 16-byte line stale while the lines before and after it, `seq` included, were new). So the ROM writes both rings only through their uncached KSEG1 aliases, after flushing their cached lines at boot, and stores a record's `seq` (or `tick`) as `0xFFFFFFFF` first and its number last. The harness also uses a retrace record only when two consecutive reads agree. A record stays in the ring for 4 retraces, so each is read 3 or 4 times, and `run.py` runs two fields past the window to read the last ones. The actor totals stay cached, so a line of them read at the window's end can be up to one tick old (inferred from the same behavior).

| Column | Meaning |
|---|---|
| `seq` | The retrace that ends the interval. |
| `wall` | PClocks between the two snapshots. |
| `cpu_busy`, `idle` | Wall minus the idle thread's time, and the idle thread's time. |
| `graph`, `audio` | The graph thread's and the AudioMgr thread's time (`AudioThread_Update` runs there). |
| `switches` | Thread switch hooks. |
| `rsp`, `rsp_gfx`, `rsp_audio` | Non-halted RSP time: all of it (emux global `XPROFREAD 0x0200`), the part outside audio tasks, and the part inside them. |
| `rdp_pipe`, `rdp_buf` | `DPC_PIPEBUSY` and `DPC_BUFBUSY` RCP clocks. |

**RSP split.** `Sched_RunTask` opens emux profile slot 1 just before `osSpTaskStartGo` of an audio task, and `Sched_HandleRSPDone` closes it and adds its non-halted RSP time. The RSP runs one task at a time, and the scheduler yields the gfx task before it starts an audio task, so the slot holds only audio time. A snapshot or tick boundary that falls inside an audio task splits the slot there. Gfx is total minus audio. These come from emux, so on hardware both read 0.

**RDP.** `rdp_pipe` is RDP pipe busy (`DPC_PIPEBUSY`). In the fork, `PIPE_BUSY` holds from a `DPC_END` write until `SYNC_FULL` retires (`ares/n64/rdp/timed.hpp`, `Dpc::busy` and `syncFullRetired`), so it includes time the RDP waits for the RSP to send more commands. `rdp_buf` (`BUFBUSY`: the command FIFO is not empty or a command executes) leaves out most of that wait. Which of the two the budget uses is for B1 to decide.

### Per tick

A tick runs from its `Graph_Update` start (`Bench_BeginFrame`) to the next one. `Bench_BeginFrame` writes the ending tick's `BenchTick` into a ring of 640 records, and `run.py` reads the ring once after the window. `load_ticks.tsv` has the ticks that start and end inside the window.

- `wall`, `idle`, `graph`, `audio`, `sched`, `irqmgr`, `padmgr`, `dmamgr`: as per field. `other_threads` is the rest (PI manager, main, flash).
- `rsp`, `rsp_audio`, `rdp_pipe`, `rdp_buf`: as per field, with the DPC counters read live at the boundary.
- `wall_<phase>`, `cpu_<phase>`: the graph thread's wall and CPU time per phase of `Graph_Update`. `pre` is input and `Audio_Update`, `update` is `Play_Update`, `draw` is `Play_Draw`, `game` is the rest of `GameState_Update` (all of it outside Play), `post` is the display list close and task setup, `rcp_wait` is `Graph_TaskSet00` blocked on the previous gfx task, and `tail` is the rest until the next tick.
- `sys_<system>`: graph thread CPU time per engine system. Time is exclusive: the innermost entered system gets it, and `other` gets the rest, so the systems sum to the tick's `graph`. The brackets sit at the call sites in `z_play.c`, `z_actor.c` and `graph.c`: `actor_update` (`Actor_UpdateAll`), `actor_draw` (`Actor_DrawAll` without its effect draws), `collision` (`CollisionCheck_AT`, `_OC`, `_Damage`, `_ClearContext`), `room_draw` (`Scene_Draw`, `Room_Draw`), `skybox`, `environment` (`Environment_Update` and the `Environment_Draw*` calls), `camera` (`Camera_Update`), `message` (`Message_*` and `Interface_*` update and draw), `effect` (`Effect_*`, `EffectSs_*`, `EffFootmark_*`), `audio_update` (`Audio_Update`), and `bench` (the tick bookkeeping). Audio synthesis is the AudioMgr thread, not a graph system.
- Actor time: `Actor_UpdateActor` brackets each `actor->update` call and `Actor_Draw` brackets itself, keyed by actor id.

### Hook cost

Every bracket calls `Bench_CpuNow` twice. The ROM times 64 calls at boot (`clock_read_pclk` in the `BENCH START` line and `hook_clock_read_pclk` in `load_summary.json`), and each tick counts its calls (`clock_reads`). `hook_ms_per_tick` is their product. It leaves out the switch hooks (`switches` calls of `Bench_CpuSwitch`). The hooks are the same code in every `BENCH` build and run once per call site, so two builds that play the same game pay the same count. A candidate must keep every bracket where it is: moving one moves time between systems. The H1b report has the measured cost.

## Pins

A pin replaces a timing value that feeds game logic or visuals with a fixed value, so that a faster candidate plays the same game as the reference. Reference and candidates are both `BENCH=1` builds and share every pin. A pin that changes retail behavior is a fidelity confound: F1 compares against the pinned reference, not against a console.

| Site | Retail | Under `BENCH` | Changes retail behavior |
|---|---|---|---|
| `src/code/z_play.c:2265` (`Play_Init`) | `Rand_Seed(osGetTime())` | `Rand_Seed(gBenchConfig.seed)` | Yes. Retail's seed depends on boot time. |
| `src/code/bench.c:458` (`Bench_Boot`) | none | `Rand_Seed(gBenchConfig.seed)` before the first game state | Yes. Retail has the default seed there. |
| `src/code/game.c:311` (`func_80173B48`, the effect budget for snow, rain and Link's dust: `src/code/z_kankyo.c:2087`, `src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c:521`, `src/overlays/actors/ovl_player_actor/z_player.c:10320`, `10336`, `10346`) | `divisor * gIrqMgrRetraceTime - gRDPTimeTotal`, in ns | `divisor * gBenchConfig.retraceTicks`, in ns, while `BENCH_FLAG_PIN_EFFECT_BUDGET` is set (default) | Yes. It drops the RDP-time term. Otherwise a faster RDP draws more particles. |
| `src/code/game.c:312` (`gIrqMgrRetraceTime` in the effect budget) | measured at boot by `IrqMgr` (`src/boot/irqmgr.c:199-203`) | `gBenchConfig.retraceTicks`, default 782715, which is the value the pinned runner measures (`BENCH START` line) | No on the runner. The effect budget is its only game-logic consumer. |
| `src/code/game.c:156` (`GameState_GetInput`) | the pad | `gBenchConfig.inputs` from the tick after `settleTick` | No change to game logic. Input timing is exact rather than human. |
| DMA polls (see Load pin) | a poll sees the transfer done when the PI has finished it | done from the first poll after the issuing tick, or the second poll in it, while `BENCH_FLAG_PIN_LOADS` is set (`--load-pin on`) | Yes. A transfer retail sees in its issuing tick is seen one tick later. |
| `src/code/graph.c:271` (`Bench_AudioBarrier`, before `Audio_Update`) | `Audio_Update` reads audio state wherever the audio thread has got to | it first waits until the audio thread has applied the previous tick's commands, while `BENCH_FLAG_PIN_LOADS` is set | No change to commands. The graph thread waits up to a field when the audio thread is behind. |

Paths that feed timing into game logic and are not pinned. Each one is outside this unit's scope (`src/` beyond the `BENCH` hook files), or pinning it would change the game. F1 catches any divergence they cause in a corpus scene.

| Site | Feedback | Corpus exposure |
|---|---|---|
| Audio state that changes with playback time: `gAudioCtx.seqPlayers[].enabled` when a sequence ends or a fade-out finishes (`src/audio/sequence.c:535`, `541`), tempo (`601`, `638`), channel script IO (`src/audio/code_8019AF00.c:3660`, `4534`), ocarina notes per audio task (`src/audio/code_8019AF00.c:3229`). Game-side audio code reads `gAudioCtx` at 26 lines of `code_8019AF00.c`, 6 of `sequence.c` and 3 of `sfx.c` (grep count). | The audio thread advances playback once per retrace, so the tick at which logic sees it depends on the tick's phase against retraces. Logic reads the result through `AudioSeq_GetActiveSeqId` and `Audio_IsSequencePlaying` (see Load pin). | The load pin covers only command-driven state. No sequence ends or fades out in a corpus window (inferred: every corpus scene loops its music). |
| `src/code/z_parameter.c:832, 893, 5695-6190`, `src/overlays/actors/ovl_En_Mm3/z_en_mm3.c:416` | Minigame, postman and bottle timers count `osGetTime()`, not ticks. | No timer runs in the corpus. |
| `src/code/z_sram_NES.c:1925-1978` | Flash save writes wait for 2 s of `osGetTime()`. | No save in the windows. |
| `src/audio/lib/thread.c:156`, `967` | `osGetCount()` seeds audio randomness. | Audio only. Not visible. |
| `src/code/speed_meter.c:122-128` | Draws `gRSPGfxTimeTotal`, `gRDPTimeTotal` and `gGraphUpdatePeriod`. | Only when `R_ENABLE_ARENA_DBG` is nonzero (`game.c:115`). 0 in the corpus. |
| RDP noise (hardware, no source line) | The noise LFSRs step once per RDP clock (the fork's `ares/n64/rdp/timed.hpp`, from Thar0/RDP-Noise console dumps). Dithered pixels (alpha dither, color dither, the combiner's NOISE input) depend on when the RDP draws them. | Every scene except `filesel`. With the noise pin off, a change that moves RDP timing changes those pixels by one 5-bit step while the game stays identical, so F1 fails (see `docs/fps60/baseline.md`). The noise pin removes this path for fidelity runs. |
| `src/code/sched.c:245, 427`, `src/code/graph.c:159` | Frame pacing itself: swaps, retraces, and the 3 s RCP hang timeout. | This is what the harness measures, not a confound. |
| `src/code/z_nmi_buff.c:32`, `src/boot/libc64/__osMalloc.c:37`, `src/boot/fault.c:553` | Reset time, allocation debug stamps, the crash screen. | Not read by game logic. |

`BENCH` also changes the boot (`Bench_BootPlay` and `Bench_BootFileSelect` in `bench.c` take shortcuts that the console logo, title and file select would otherwise take), adds RAM (the probe, the 4 KiB input table, the load rings: 111 KiB of bss, against 73 KiB for the per-frame record buffer they replace), and adds the load hooks' CPU time (see Load metrics). Base and candidate share these. `tools/fps60/memmap.py ELF...` prints the static RAM layout of builds side by side.

## Noise pin

Preferences line 20: retail never shows the same noise-dithered pixels twice, so noise patterns are not part of retail's image. Fidelity runs neutralize noise identically in reference and candidate, and F1 also compares how the game used noise.

The pin also removes a second path from RDP timing to pixels: span buffer coherency. Without `G_PM_1PRIMITIVE`, the RDP prefetches the next span's color and Z before the previous primitive's write-back lands (SDK 12.2.3; the fork models it in `RDP::prefetch`, `ares/n64/rdp/timed.cpp`). Where primitives overlap and read memory (`IM_RD` or `Z_CMP`), the read sees old or new memory depending on RDRAM contention from the CPU, RSP, VI and AI. With noise pinned and the game state identical at every tick (actor positions, rotations, scales and camera, checked by a throwaway per-tick hash), a dead-code build still differed from the reference by single edge pixels in `sct` (25 ticks) and `field` (1 tick). Forcing `G_PM_1PRIMITIVE` removed every one (H1c report). The fork's spec lists `rdp.read-gate` as fail and `hw:stale-read` as pending, so this path is modeled but not yet checked against a console.

`BenchConfig.noisePin` (`include/bench.h`, `BENCH_NOISE_PIN_*`) selects it. The ROM's default is off, so a run that does not set it renders as retail. `run.py` patches it per run.

What it does. `Bench_PinNoise` (`src/code/bench.c`, called from `Graph_TaskSet00` in `graph.c` after the previous gfx task finished and before the task is sent) walks every display-list command the task reaches, in RSP order:

- `G_DL` calls and branches, `G_ENDDL`, the segment table (`G_MOVEWORD G_MW_SEGMENT`), and `G_LOAD_UCODE` switches between F3DZEX2 and S2DEX2. `G_BRANCH_Z` and `G_CULLDL` depend on vertex depth, so the walk takes both paths of a `G_BRANCH_Z` and never culls. A target outside RDRAM or in a segment that was never set counts as `unresolved`. An opcode the active microcode does not define, and S2DEX2's `G_SELECT_DL`, count as `unknown`.
- A noise command (the name covers both paths) is a command that writes a field the pin rewrites, whatever value it writes. That is every `G_RDPSETOTHERMODE` and every `G_SETCOMBINE`, a `G_SETOTHERMODE_H` whose bits cover the color dither (`G_MDSFT_RGBDITHER`), alpha dither (`G_MDSFT_ALPHADITHER`) or pipeline mode (`G_MDSFT_PIPELINE`) bits, and a `G_SETOTHERMODE_L` whose bits cover the alpha compare bits (`G_MDSFT_ALPHACOMPARE`). A noise command that selects one of the noise reads of the fork's RDP engine (`ares/n64/rdp/engine/rdp_core.c`, `rdp_blend.c`), or writes `G_PM_NPRIMITIVE`, is rewritten in RDRAM to a fixed substitute:

| Noise read | Substitute | Why this one |
|---|---|---|
| `G_CD_NOISE` (RGB dither) | `G_CD_MAGICSQ` | Alpha dither `PATTERN` and `NOTPATTERN` read the magic square under both, so alpha is unchanged. |
| `G_AD_NOISE` (alpha dither) | `G_AD_PATTERN` | Ordered, and the same mean dither. |
| `G_AC_DITHER` (alpha compare against a noise threshold) | `G_AC_THRESHOLD` | Keeps the compare, against the blend color's alpha. |
| combiner RGB input A = `NOISE`, either cycle | `G_CCMUX_1` | `NOISE` is abc100000 in 1/256 units; its mean is 1. |
| pipeline mode (`G_PM_NPRIMITIVE` or `G_PM_1PRIMITIVE`) | `G_PM_1PRIMITIVE` | Each primitive waits for the previous one's write-back, so no read is stale. Primitives drawn before a task's first pipeline-mode write keep the RSP's starting mode. |

- The per-tick record (`BenchProbe.cfbNoise[i]`, parallel to `cfbTick[i]`) holds `noise_hash`, FNV-1a over both words of every noise command the walk met, rewritten or not, as the game wrote it, in walk order. It also holds `noise_cmds`, the number of noise commands; `noise_patched`, how many of them this walk rewrote; and `dl_cmds`, every command visited. Only `noise_hash` takes part in F1. No other command is hashed: a `G_SETOTHERMODE_H/L` that covers none of the bits above (texture filter, render mode and the like) only counts toward `dl_cmds`, and its effect on the image shows in `fb_hash`. `run.py` reads the record of the displayed `CfbInfo` each field, like `cfbTick`.
- Idempotent. Display lists in asset, object and static memory are rewritten in place, and the rewrite stays. A log (`sBenchNoiseStatic`) keeps each rewritten command's address, original and substitute. A later walk that finds the substitute at that address hashes the original. If the game has rewritten the command since, the walk treats it as new. The gfx pools are rebuilt every frame, so their log (`sBenchNoisePool`) is cleared at each walk. `--noise-pin check` proves the property on a run: every task is walked twice, and the second walk must hash the same and patch nothing (`mismatch` 0).

What it detects. With the pin on, a tick fails F1 when its image differs anywhere (pixels outside noise, and the substitutes' deterministic dither) or when the game emitted different noise commands: a noise mode added, removed or changed (for example `G_CD_MAGICSQ` written where retail writes `G_CD_NOISE`), a changed combiner that reads or stops reading `NOISE`, a changed pipeline mode, or the same commands in a different order or count. A noise command is hashed whether or not the pin rewrites it, so a change that adds, drops or reorders one also fails, even when it already holds the substitute's value and the image could not show it (for example an extra `G_PM_1PRIMITIVE` write, or an extra `G_CD_MAGICSQ` write where the game writes no dither; H1c-fix report).

What it cannot detect.

- The noise patterns themselves, and which overlapping pixels read stale memory. A change that only moves RDP timing passes, which is the point (line 20).
- Edge pixels where retail would read stale memory: the pin always draws them as 1PRIMITIVE does.
- Pixels in noise-dithered draws that differ only by the dither the substitute replaced. The substitute draws them with a fixed dither, so a change in the colors, geometry or textures of a noise draw still shows. But a difference that retail's noise would have hidden or shown at random shows here as the substitute renders it.
- A command at an address that the walk patched earlier, which the game later overwrites with exactly the substitute's words. The walk then hashes the old original. Reference and candidate do the same, so F1 only misses a candidate that changes this alone. Gfx-pool addresses are immune, because their log lives for one walk.
- A rewritten command that the game reads back as data and copies elsewhere. The copy then holds the substitute and is not a noise command. No such read is known in the corpus.
- Noise commands in display lists that no gfx task reaches, and noise in a task other than `Graph_TaskSet00`'s (MM sends no other gfx task).
- Partial othermode writes that cover half of a 2-bit dither or compare field count as `unknown`, and fail the run, because their result depends on state before.

Cost. The walk visits about 11,500 commands per tick in `sct` (measured, see the H1c report) on the CPU before the task is sent, and 1PRIMITIVE adds RDP time per primitive. Both change timing, but with the pin on timing no longer reaches the image. File select, which already lags in retail, shows fewer ticks in its window with the pin on. Perf runs use `--noise-pin off`, where `Bench_PinNoise` returns at once.

Pin off is retail rendering, not H1a's bytes. Any code or data layout change moves CPU timing and so the RDP clock at each pixel, so a pin-off run of this tree does not reproduce H1a's `fb_hash` values even though the pin-off path only returns (H1c report).

## Load pin

Architecture risk R4: game logic polls async DMA requests with `OS_MESG_NOBLOCK` and reads sequence state that the audio thread derives from its commands. The tick at which logic sees a load finish therefore depends on PI, audio-thread and CPU timing, in retail as on the runner, and a candidate that moves it diverges from the reference for the rest of the run. With `BENCH_FLAG_PIN_LOADS` set (`--load-pin on`), load state reaches logic only at tick boundaries, by a rule that depends on ticks alone, identically in every build.

DMA. `Bench_DmaIssued` (called from `DmaMgr_RequestAsync`, `src/boot/z_std_dma.c:286`, once `Bench_Boot` has run) records the tick that issues each request, keyed by its message queue. `Bench_DmaRecv` replaces `osRecvMesg` at every game-logic poll of an async request. The first poll of a queue in its issuing tick answers "not done". Every later poll takes the message, waiting for the transfer if it is still running. A second poll in the issuing tick is a wait loop: `Play_Init` spins on `Room_ProcessRoomRequest` (`src/code/z_play.c:2454`), and retail leaves that loop only when the transfer is done. The polls, found by a grep of `osRecvMesg` without `OS_MESG_BLOCK` in `src/code`, `src/overlays` and `src/boot` (the others are the scheduler, graph, audio manager and audio thread queues):

| Poll (this tree) | Request | What logic does with it |
|---|---|---|
| `src/code/z_scene.c:97` (`Object_UpdateEntries`) | `z_scene.c:93` | Object slot becomes loaded; actors that need it can spawn. |
| `src/code/z_room.c:612` (`Room_ProcessRoomRequest`) | `z_room.c:593` (`Room_RequestNewRoom`) | Room becomes current; its scene commands run and transition actors spawn. |
| `src/code/z_kankyo.c:1109` (`Environment_UpdateSkybox`, skybox texture change) | `z_kankyo.c:1092`, `1101` | `skyboxDmaState` returns to inactive. |
| `src/code/z_parameter.c:6776` (`Interface_LoadStory`, called with `OS_MESG_NOBLOCK` at `6798`) | `z_parameter.c:6770` | Story textures ready. |
| `src/code/z_player_lib.c:212` (`func_801229FC`, mask object) | `z_player_lib.c:206` | Mask object ready. |
| `src/code/z_player_lib.c:2954` (`Player_DrawGetItem`) | `src/overlays/actors/ovl_player_actor/z_player.c:7639` | Get-item model drawn. |
| `src/code/z_play_hireso.c:1129` (`BombersNotebook_LoadFiles`, called with `OS_MESG_NOBLOCK` at `1174`) | `z_play_hireso.c:1123` | Notebook schedule loaded. |

Requests whose message is received only with `OS_MESG_BLOCK` (skybox setup `src/code/z_vr_box.c:300-333`, do-action textures `src/code/z_parameter.c:3304`, `3352`, `3397`, player animation frames `src/code/z_skelanime.c:1036` received at `1140`, and `DmaMgr_RequestSync`) are timing-independent already. Their records stay in the 128-entry issue table until their queue is reused or the oldest record is dropped; `DmaMgr_RequestSync` clears its own (`z_std_dma.c:320`).

Audio. `Bench_AudioBarrier` (`src/code/graph.c:271`) runs before `Audio_Update`. It waits until the audio thread has finished an update that took the message the previous tick's `Audio_Update` sent (`AudioThread_ScheduleProcessCmds`, noted by `Bench_AudioSent` at `graph.c:277`). So every command effect, including sequence starts and their synchronous sequence and font loads (`AudioLoad_SyncInitSeqPlayer`), is in place before logic reads it, and the font-load result (`AudioThread_GetExternalLoadQueueMsg` in `AudioSeq_UpdateActiveSequences`, `src/audio/sequence.c:547`) is read at the tick boundary. Logic reads sequence state at these 15 sites, all through `gActiveSeqs`, which `Audio_Update` maintains: `AudioSeq_GetActiveSeqId` at `src/code/z_kankyo.c:2450`, `2557`, `2558`, `3066`, `3080`, `src/code/z_game_over.c:77`, `src/code/z_play.c:597`, `610`, `src/code/z_scene.c:468`, `src/code/z_demo.c:507`, `src/overlays/actors/ovl_Obj_Demo/z_obj_demo.c:61`, `src/overlays/actors/ovl_En_Aob_01/z_en_aob_01.c:804`; `Audio_IsSequencePlaying` at `src/overlays/actors/ovl_En_Toto/z_en_toto.c:593`, `src/overlays/actors/ovl_player_actor/z_player.c:17215`, `src/overlays/actors/ovl_En_Horse/z_en_horse.c:2942`. The barrier does not change audio code or commands. It cannot pin state that changes with playback time (see the unpinned table).

`BenchProbe.load` (`BenchLoadTotals`, read after the window into `summary.json` `load_totals`): `dma_polls`, `dma_held` (first polls in the issuing tick), `dma_waited` (later polls), `dma_untracked` (polls with no issue record, answered as retail), `dma_overflow` (records of the current tick dropped by a full table), `audio_barriers`, `audio_waits` (barriers that had to wait). `run.py` fails a pinned scene with a nonzero `dma_untracked` or `dma_overflow`.

The pin changes the game against retail: loads land one tick later than a fast retail transfer would. Reference and candidate share it, like the effect budget pin. Perf runs leave it off (`--noise-pin off` turns it off unless `--load-pin on` is given): the barrier and the waits are graph-thread time that retail does not spend.

## Stale reads

Preferences line 26: without `G_PM_1PRIMITIVE` the RDP can read color and Z before the previous primitive's write-back lands, so edge pixels depend on timing on a console too. The noise pin forces `G_PM_1PRIMITIVE`, which hides that path from F1. A unit that changes RDP command order, primitive timing or pipeline mode must also show that its stale-read exposure does not rise:

1. Build the candidate, and a control with the candidate's layout and the reference's behavior. The control is the candidate's patch with every change made unreachable at run time, so code and data keep the same addresses: guard each change with a condition the game never meets, such as `gBenchProbe.tick == 0xFFFFFFFF` (H1a report, control patch). Data tables the candidate changes keep their size and take the reference's values. Check the layout with `tools/fps60/memmap.py` on both ELFs, or by diffing the symbol lines of the two maps: they must be equal.
2. Run the reference, the candidate and the control with `--noise-pin noise-only`.
3. `tools/fps60/stale_reads.py REF CAND CONTROL`. It prints, per scene, the differing pixels and images of the candidate and of the control against the reference. The unit lands only if no scene's candidate count exceeds the control's. Images whose `noise_hash` differs are reported too; F1 rejects those on its own.

The control's count is the exposure that a layout change alone causes, because any change in code size moves CPU and RDP timing.

## Gotchas

- The harness reads `gBenchProbe` by offset (`run.py`, `PROBE_*`, `CFG_NOISE_PIN`, `PROBE_LOAD_TOTALS`). A change to `BenchProbe` or `BenchConfig` in `include/bench.h` is a harness change. `run.py` checks the probe's magic and the config's magic, not the layout.
- `run.py` never clears `BENCH_FLAG_PIN_EFFECT_BUDGET`. The ROM never exits on its own: the runner script stops it.
- `title` settles 127 ticks after boot. The Play scenes with an entrance cutscene (`field`, `greatbay`) settle about 200 ticks after boot. Their runs take longer for that reason.
- The first image of a window often belongs to a tick before `settleTick`, because the image the VI shows lags the latest tick by about two ticks.
- With the noise pin off, F1 is sensitive to RDP timing through the noise generator (see the table above). A planted change that draws one pixel on tick 100 of `sct` fails F1 in `sct` only when it is compared with a build of the same layout. Its first differing tick is 99, because the extra CPU work during tick 100 moves the RDP's render of tick 99 and so its noise. Compared with the reference, the same build fails in 5 scenes, each within its first 12 compared ticks. The H1a report has the patches and numbers.
