# Benchmark mode

> **Warning: the timing model is not calibrated against hardware yet.** `bench.py` runs the ROM on the
> ares timing-model fork ([wScottSh/ares](https://github.com/wScottSh/ares), `tools/n64-timing`), where
> RDP, RSP, bus and CPU time are emulated from published hardware references and every run is
> bit-deterministic. No run has been compared against a real console. Its spec,
> `docs/spec/n64-timing.md` in the fork, lists each timing behavior with its reference and status.

Repeatable per-frame performance measurement for the MM decomp. Background and sources:
[docs/research/n64-emulator-accuracy.md](../../docs/research/n64-emulator-accuracy.md),
[docs/research/n64-perf-tooling.md](../../docs/research/n64-perf-tooling.md) (in Scott's checkout, not in this branch).

## Quick start

```bash
make -j32 BENCH=1                                    # -> build/n64-us-bench/mm-n64-us.z64
tools/bench/bench.py --scene all --runs 1 --jobs 4 --label my-change
~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<dir>          # summary + plots
~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<A> bench-results/<B>   # A/B
tools/bench/memmap.py build/n64-us/mm-n64-us.elf build/n64-us-bench/mm-n64-us.elf  # static RAM
```

`make` without `BENCH=1` is unchanged and still matches (checked: `mm-n64-us.z64: OK`).

## Runner

`bench.py` runs each ROM in `n64-run`, the fork's headless runner. It needs no display, always has emux
and IS-Viewer on, always runs the CPU and RSP interpreters, reads no host clock, and exits when the
ROM requests an emux exit. Where the runner comes from:

| Setting | Effect |
|---|---|
| `--runner PATH` or `$N64_RUN` | Use this `n64-run` binary. |
| otherwise | Build it with `tools/n64-timing/build.sh` in the fork checkout and use that binary. A no-op when the build is up to date. |
| `--fork DIR` or `$ARES_FORK` | Fork checkout to build from. Default `~/repos/ares`. |
| `$N64_BUILD_DIR` | Build directory for `build.sh`. Default `~/n64-timing/build/<checkout name>`. |
| `--timeout S` | Host seconds per run, passed as `n64-run --wall-seconds`. Default 600; a run took up to 214 s on a loaded host. |

`meta.json` records the runner path and its sha256. When `bench.py` built the runner, it also records
`ares_fork`, the `git describe --tags --dirty` of the fork checkout. With `--runner`, `ares_fork` is
`null`, because the binary can come from any tree. Each result also has `runner_stop`, the runner's
`n64-run: stop=...` line. A run counts as ok only when it ends with `stop=emux-exit` and all frames were
recorded.

## What `BENCH=1` changes

All under `#if BENCH`. The build goes to `build/n64-us-bench`, with `COMPARE=0`.

- `title_setup.c`: boots straight into Play with the debug save at a fixed scene, day 1 10:00 (`Bench_Boot`).
- `z_play.c`: `Rand_Seed(gBenchConfig.seed)` instead of `osGetTime()`.
- `graph.c`: timestamps around `Graph_Update` and `GameState_Update`.
- `src/code/bench.c` and `bench_emux.s`: record buffer, IS-Viewer output, and emux opcodes.

`bench.py` patches the `BenchConfig` block (magic `BNCH`) in a copy of the ROM: scene, warmup, frames, seed. Changing these needs no rebuild. Records are kept in RAM during measurement and written out over IS-Viewer only at the end. The ROM then exits the emulator via emux `XIOCTL`.

`func_80173B48` keeps its retail code. It is the effect budget: the VI time for one game frame minus the
last frame's RDP time. Rain, snow and the dust Link kicks up scale with it. Stock ares took no emulated
time for RDP work, so the BENCH build used to pin the budget to ignore RDP time. The fork emulates RDP
time deterministically, so the pin is gone.

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
| `rdp_ticks` | Gfx task start to DP full sync (`gRDPTimeTotal`). Not RDP busy time: it includes the wait for the RSP |
| `opa/xlu/ovl_bytes` | Display-list bytes used this frame. Capacities: 105,472 / 16,384 / 8,192 |
| `zelda_free`, `zelda_max_free` | ZeldaArena free bytes and largest free block |
| `tha_free`, `sys_free` | Always 0 in Play: the arena is fully handed to ZeldaArena |
| `cpu_cycles` … `rdram_dp` | emux `XPROF` slot deltas: cache misses, RSP cycles and halted cycles, RDRAM bytes per bus master |
| `rsp_cycles`, `rsp_halted` | On the fork `rsp_cycles` counts only non-halted RSP time, in 750 MHz units, and `rsp_halted` is always 0 ([wScottSh/ares#82](https://github.com/wScottSh/ares/issues/82)). So `analyze.py`'s `rsp busy %` reads 100%; use `rsp_cycles / 750e6` s as RSP busy time |
| `rdram_dp` | `DP_DRAW` plus `DP_DMA` bytes. The fork tags the pixel engine's RDRAM bursts as `DP_DRAW` (`ares/n64/ri/bus.cpp`), so framebuffer, Z and texture traffic count here and in `rdram_total` |
| `bench_ticks` | The bench bookkeeping's own cost on the previous frame (about 0.1–0.2 ms) |

## Validity: what to trust

Measured on unicron (shared host, load average 2.6 before the runs and 6.2 after) on 2026-10-09, fork `nightly-234-g253e1c8ea`:

- **Runs are bit-deterministic.** Two `bench.py --scene all --runs 1` invocations of the same ROM wrote byte-identical CSV and log files for all four scenes, timing columns included. One run per variant is enough; extra runs repeat the same bytes.
- **The effect budget does not change these four scenes.** With the pin removed, actors, divisor and `opa/xlu/ovl_bytes` are identical frame for frame to a pinned build in all four scenes. Computed from `rdp_ticks`, the unpinned budget stays between 14.0 and 24.0 ms. Snow saturates at a budget of 14 ms or more (`z_object_kankyo.c`). No scene has rain, and Link stands still, so he kicks up no dust. A scene with rain or player movement would change with the RDP time.
- **The two builds still differ in timing.** Removing the pin changes the code, so the code layout and heap move (ZeldaArena free is 64 bytes lower). Median `game_ms` moved by -0.9% to +0.4% per scene. South Clock Town lagged on 7 frames instead of 6, at different frames. One run each, so this is one point per build, not a trend.
- **Host time.** A 600-frame run (boot, 120 warmup frames, 600 measured, about 2270 VI fields) took 151 to 214 s per scene, 4 runs in parallel. Single-run wall time was not measured.
- **Memory cost of the bench build.** It adds 66.7 KiB to code plus bss (mostly the 600-frame record buffer). The system heap, and so ZeldaArena free, is 66.7 KiB smaller than retail. (Measured by Scott on 2026-10-04.)
- **`cpu_cycles` is wall clock.** It counts the idle thread too, so it isn't a busy measure.
- **Not calibrated.** No number here has been compared against a console. `bench.c` writes IS-Viewer at `0x13FF0000`, which a SummerCart64 can forward over USB; that path is untested.

## Fork baseline (not hardware-calibrated): 2026-10-09, retail code + bench hooks

Fork `nightly-234-g253e1c8ea`, 1 run x 600 frames per scene, warmup 120, seed 0x12345678 (`analyze.py` output). Times are medians unless noted.

| metric | South Clock Town | Termina Field | Mountain Village | Great Bay Coast |
|---|---|---|---|---|
| fps (divisor) | 20.0 (3) | 20.0 (3) | 20.0 (3) | 20.0 (3) |
| lag frames | 1.17% | 0% | 0% | 0% |
| game ms (med / p95 / max) | 22.5 / 23.5 / 24.9 | 22.9 / 28.0 / 32.7 | 18.1 / 19.3 / 19.7 | 31.0 / 32.1 / 32.7 |
| 60fps CPU headroom (16.7 - p95) | -6.75 ms | -11.3 ms | -2.58 ms | -15.4 ms |
| RSP gfx / audio ms | 24.7 / 9.40 | 18.1 / 7.42 | 15.8 / 5.27 | 21.9 / 6.22 |
| RDP ms (gfx task start to full sync) | 34.2 | 26.1 | 27.3 | 27.6 |
| actors | 38 | 160 | 43 | 156 |
| POLY_OPA peak (% cap) | 17.1% | 13.5% | 10.5% | 27.4% |
| ZeldaArena free min | 950 KiB | 695 KiB | 1251 KiB | 932 KiB |
| I$ / D$ misses per frame | 21.1k / 20.3k | 18.9k / 21.6k | 16.6k / 14.5k | 21.3k / 23.6k |
| RDRAM traffic per frame (incl. RDP) | 5.59 MiB | 4.99 MiB | 4.79 MiB | 5.25 MiB |

The stock-ares baseline from 2026-10-04 (ares `a776c509b`) is in this file's history. It is not comparable: stock ares charged no time for RDP work and pinned the effect budget.

## Next steps (not done)

- Input replay: a per-frame pad table fed in at `GameState_GetInput` (`game.c`). Needs a recorded input source.
  `n64-run --script` can also drive the controller from outside the ROM (`input`, `wait`); `bench.py` passes no script yet.
- A scene with rain, and one with Link moving, so the effect budget's dependence on RDP time is exercised.
- More scenes: boss arena, pause menu (divisor changes to 2), indoor scenes.
- Per-system CPU breakdown with emux `XPROF` slots around actor update, draw, collision and the environment.
- Real hardware: SummerCart64 + `sc64deployer debug`, ≥ 10 runs per variant, DPC counters.
