# Benchmark mode

> **Warning: ares is not a hardware-accurate timing model.** It renders RDP work in zero emulated time
> (`rdp/io.cpp` `flushCommands`, `rdp/render.cpp` `syncFull`) and fails most nemu64-test timing tests.
> So the timing numbers below (frame time, RSP and RDP time, lag) do **not** reflect a real N64, and
> must not drive optimization decisions. The in-ROM instrumentation (`BENCH=1`) is
> emulator-independent and meant to run on whatever hardware-accurate environment replaces ares;
> see docs/research/n64-cycle-accurate-sim.md (in progress).

Repeatable per-frame performance measurement for the MM decomp on ares. Background and sources:
[docs/research/n64-emulator-accuracy.md](../../docs/research/n64-emulator-accuracy.md),
[docs/research/n64-perf-tooling.md](../../docs/research/n64-perf-tooling.md).

## Quick start

```bash
make -j32 BENCH=1                                    # -> build/n64-us-bench/mm-n64-us.z64
tools/bench/bench.py --scene all --runs 5 --jobs 8 --label my-change
~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<dir>          # summary + plots
~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<A> bench-results/<B>   # A/B
tools/bench/memmap.py build/n64-us/mm-n64-us.elf build/n64-us-bench/mm-n64-us.elf  # static RAM
```

`make` without `BENCH=1` is unchanged and still matches (checked: `mm-n64-us.z64: OK`).

## What `BENCH=1` changes

All under `#if BENCH`. The build goes to `build/n64-us-bench`, with `COMPARE=0`.

- `title_setup.c`: boots straight into Play with the debug save at a fixed scene, day 1 10:00 (`Bench_Boot`).
- `z_play.c`: `Rand_Seed(gBenchConfig.seed)` instead of `osGetTime()`.
- `game.c`: `func_80173B48` ignores RDP time. In retail it scales rain, snow and dust counts by the last frame's RDP time, so the workload would depend on how fast the RDP (or emulator) is.
- `graph.c`: timestamps around `Graph_Update` and `GameState_Update`.
- `src/code/bench.c` and `bench_emux.s`: record buffer, IS-Viewer output, and emux opcodes.

`bench.py` patches the `BenchConfig` block (magic `BNCH`) in a copy of the ROM: scene, warmup, frames, seed. Changing these needs no rebuild. Records are kept in RAM during measurement and written out over IS-Viewer only at the end. On ares, the ROM then exits the emulator via emux `XIOCTL`.

Scenes (`sBenchScenes` in `bench.c`; keep `SCENES` in `bench.py` in sync):

| # | scene |
|---|---|
| 0 | South Clock Town |
| 1 | Termina Field |
| 2 | Mountain Village (winter, snow) |
| 3 | Great Bay Coast |

There is no input yet: Link stands at the spawn point.

## Columns

Times are OSTime ticks (46.875 MHz); `analyze.py` converts them to ms.

| column | meaning |
|---|---|
| `vi` | VI retraces between consecutive frame ends. Equals the divisor (3 = 20 fps) when not lagging |
| `pre_ticks` | `Graph_Update` start to `GameState_Update` start: input, `Audio_Update`, gfx pool setup |
| `game_ticks` | `GameState_Update` wall time: game logic plus display-list build. **The main CPU metric.** Includes preemption by higher-priority threads (audio, scheduler) |
| `cpu_ticks` | Whole `Graph_Update`, including waiting for the previous RCP task |
| `rsp_gfx_ticks`, `rsp_aud_ticks` | Scheduler's existing accumulators: RSP task start to done |
| `rdp_ticks` | Gfx task start to DP full sync. **Not RDP busy time** (see validity) |
| `opa/xlu/ovl_bytes` | Display-list bytes used this frame. Capacities: 105,472 / 16,384 / 8,192 |
| `zelda_free`, `zelda_max_free` | ZeldaArena free bytes and largest free block |
| `tha_free`, `sys_free` | Always 0 in Play: the arena is fully handed to ZeldaArena |
| `cpu_cycles` … `rdram_dp` | ares emux `XPROF` slot deltas: cache misses, RSP cycles and halted cycles, RDRAM bytes per bus master |
| `rdram_dp` caveat | ares never feeds its `DP_DRAW` counter, so this is only RDP command DMA. Framebuffer and Z traffic are missing, and `rdram_total` undercounts by the same amount |
| `bench_ticks` | The bench bookkeeping's own cost on the previous frame (about 0.1–0.2 ms) |

## Validity: what to trust

Verified on this machine:

- **Game logic is deterministic.** Across runs, `vi`, actors, display-list bytes and arena values are identical frame for frame.
- **Timing jitters slightly.** Run-to-run CV of median `game_ms` is ≤ 0.1%. Use ≥ 3–5 runs and `analyze.py` compare.
- **Parallel runs are fine.** 4-way parallel ares runs showed no significant difference from sequential runs (A/A test, all CIs include 0).
- **Interpreter vs recompiler.** `--interpreter` gives about 20% more I-cache misses and 1–3% higher `game_ms` than the default recompiler; game logic is identical. Inference: the interpreter models instruction fetch more faithfully. Use the recompiler to iterate and the interpreter to confirm cache-sensitive changes.
- **Memory cost of the bench build.** It adds 66.7 KiB to code plus bss (mostly the 600-frame record buffer). The system heap, and so ZeldaArena free, is 66.7 KiB smaller than retail.
- **`cpu_cycles` is wall clock.** It counts the idle thread too, so it isn't a busy measure.
- **RSP cycle units.** `rsp_cycles` runs at about 2× the `cpu_cycles` rate, so its units aren't real RSP clocks. Use `rsp busy %` = 1 − halted/cycles; it agrees with the scheduler's tick totals.

Not trustworthy on ares (per the research docs and ares source, not measured against hardware):

- **RDP timing.** ares finishes RDP work instantly, so fill rate, RDP stalls and `rdp_ticks` don't reflect hardware.
- **Bus timing.** RDRAM contention isn't modeled, and cache misses have fixed costs. Absolute lag counts at 60fps need real hardware. `bench.c` writes IS-Viewer at `0x13FF0000`, which a SummerCart64 can forward over USB; that path is untested.
- **CPU timing.** ares fails most nemu64-test timing tests. Treat ares numbers as relative (A/B), not absolute.

## ares baseline (NOT hardware-valid): 2026-10-04, retail code + bench hooks

ares `a776c509b` recompiler, 5 runs × 600 frames, warmup 120. Times are medians unless noted.

| metric | South Clock Town | Termina Field | Mountain Village | Great Bay Coast |
|---|---|---|---|---|
| fps (divisor) | 19.9 (3) | 20.0 (3) | 19.9 (3) | 19.9 (3) |
| lag frames | 0% | 0% | 0% | 0.83% |
| game ms (med / p95 / max) | 23.8 / 30.6 / 32.0 | 30.5 / 31.7 / 41.2 | 20.8 / 22.2 / 26.2 | 35.4 / 36.4 / 37.4 |
| 60fps CPU headroom (16.7 − p95) | −13.8 ms | −15.0 ms | −5.5 ms | −19.7 ms |
| RSP gfx / audio ms | 18.2 / 8.8 | 11.9 / 7.0 | 7.7 / 5.1 | 16.5 / 5.9 |
| RSP busy | 54% | 38% | 25% | 44% |
| actors | 38 | 160 | 43 | 156 |
| POLY_OPA peak (% cap) | 17% | 14% | 11% | 27% |
| ZeldaArena free min | 950 KiB | 695 KiB | 1251 KiB | 932 KiB |
| I$ / D$ misses per frame | 17.7k / 19.7k | 16.1k / 21.6k | 13.9k / 14.5k | 18.2k / 23.6k |
| RDRAM traffic per frame (excl. RDP pixel traffic) | 2.03 MiB | 1.69 MiB | 1.26 MiB | 2.03 MiB |

Reading it (**ares only: RDP cost is zero here, so expect real hardware to be slower; how much slower is unknown**):
- **CPU.** For 60 fps, `GameState_Update` alone runs 1.3–2.2× over the 16.7 ms VI budget in every scene. The CPU is the first wall.
- **RSP.** RSP gfx alone exceeds one VI in Clock Town and Great Bay (18.2 and 16.5 ms), so microcode and display-list cost is the second wall. RDP cost can't be judged on ares.
- **Warmup.** Termina Field has a step change around measured frame 92 (game +7 ms, RSP gfx −4 ms); the scene hasn't settled by frame 120. Consider a longer warmup for it.

## Next steps (not done)

- Input replay: a per-frame pad table fed in at `GameState_GetInput` (`game.c`). Needs a recorded input source.
- More scenes: boss arena, pause menu (divisor changes to 2), indoor scenes.
- Per-system CPU breakdown with emux `XPROF` slots around actor update, draw, collision and the environment.
- Real hardware: SummerCart64 + `sc64deployer debug`, ≥ 10 runs per variant, DPC counters.
