# N64 performance tooling for the MM decomp (60fps-on-hardware work)

Research date: 2026-10-04. Host: unicron (Ubuntu 24.04, 32 cores, RADV GFX1151, no X display).
Companion doc: [`n64-emulator-accuracy.md`](n64-emulator-accuracy.md) (emulator choice → **ares**). This doc covers *measurement*, not emulator choice.

Legend: **[verified]** = checked on this machine or read in source (file:line / commit given). **[cited]** = stated by a primary source (link), not re-checked here. **[unverified]** = inference or not checked.

## TL;DR

1. **The measurement backbone has to live in the ROM, not in the emulator.** No available emulator models RDP time (see §5), so the same instrumented ROM must run on ares (fast, deterministic iteration) and on real hardware (ground truth). Output goes through **IS-Viewer at ROM offset `0x03FF0000`** (PI `0x13FF0000`). ares prints it to stdout [verified by the companion doc; ares `cartridge/isviewer.cpp:3`], and SummerCart64 forwards it over USB (`sc64deployer debug --isv 0x03FF0000`) [cited, SC64 `docs/04_config_options.md`, `sw/deployer/src/main.rs:210`].
2. **The decomp already has a CPU/RSP/RDP timer**: the "SpeedMeter" accumulators in `sched.c` / `graph.c`. They are compiled into the retail build but only drawn when `SREG(0)` is set, and nothing in retail can set it [verified, §1]. Its "RDP time" is really the wall time from gfx-task start to RDP full-sync, not RDP busy time.
3. **Big confound [verified]: MM scales its own workload from measured RDP time.** `func_80173B48` (`src/code/game.c:300`) returns *frame budget − gRDPTimeTotal*. Rain (`z_kankyo.c:2080`), snow (`z_object_kankyo.c:521`) and Player dust/grass particle counts (`z_player.c:10320/10336/10346`) are scaled by it. So a faster RDP means more particles get drawn, emulators (RDP takes ~0 time) draw more than hardware, and the RNG gets consumed differently. **Benchmark builds must pin this to a constant.**
4. **Use a "benchmark mode" build flag** for: boot straight into a scene (the `MapSelect` gamestate overlay is in retail [verified]); fixed `Rand_Seed` (currently `Rand_Seed(osGetTime())`, `z_play.c:2260`); a scripted input table fed in at `GameState_GetInput` (`game.c:149`); per-frame metric records over IS-Viewer; exit at the end through emux `XIOCTL exit` on ares.
5. **Per-frame metrics to record:**
   - lag (VIs between framebuffer swaps)
   - CPU time per thread (port the HackerOoT profiler)
   - RSP gfx/audio time (existing accumulators)
   - RDP `DPC_CLOCK/BUFBUSY/PIPEBUSY/TMEM` (hardware only)
   - arena and display-list high-water marks
   - on ares: **emux `XPROF`** counters (CPU cycles, I/D-cache hits/misses, RSP cycles, RDRAM bytes per bus master). RSP counters need ares master ≥ `6fcaa213` (2026-06-02), which is newer than v148 [verified, GitHub API].
6. **Trust boundaries:**
   - ares is good for CPU-side relative work: algorithmic cost, cache-miss *counts*, instruction counts, RSP compute.
   - Hardware is required for anything that involves RDP fill/texture cost, RSP↔RDP FIFO back-pressure, RDRAM contention, absolute lag-frame counts, and every final 60fps claim.
   - Recommended hardware: **SummerCart64** (open source, USB, IS-Viewer passthrough) [cited].

## Recommended toolset

| Tool | Purpose | Install | Status |
|---|---|---|---|
| In-ROM benchmark/profiler module (new, behind a `BENCH` build flag) | Deterministic scene + input, per-frame metric records, works on emu and HW | Write it; model on HackerOoT `src/debug/profiler.c`, `include/debug/profiler_inline.h`, `exceptasm.s` thread hooks | design [unverified] |
| Existing SpeedMeter accumulators | RSP gfx/audio/other time, gfx-task wall time, Graph_Update period | In tree (`src/code/speed_meter.c`, `sched.c`, `graph.c`) | [verified, source] |
| ares (master ≥ 6fcaa213) + Homebrew Mode | Fast deterministic runs; emux XPROF (cycles, cache, RDRAM, RSP); XLOG; XIOCTL exit; IS-Viewer; GDB stub; RDP command tracer | Build from source in Docker (companion doc) at a post-v148 commit | v148 runs MM [verified by companion doc]; emux in host build [verified, strings]; XPROF output not yet exercised [unverified] |
| ares GDB stub + `gdb-multiarch` | Read/write RAM (e.g. toggle `SREG(0)`, read accumulators, overlay table) | `gdb-multiarch` already installed [verified] | memory read works [verified, companion doc]; PC register read wrong (`0xffffffff`) [verified, companion doc] |
| SummerCart64 + `sc64deployer` | Real-hardware runs; IS-Viewer → USB; ROM upload | Buy hardware; deployer from SC64 releases | [cited]; not owned/tested |
| UNFLoader (`usb.c`/`debug.c`) | Alternative USB I/O for 64drive / EverDrive X7 / SC64 | `apt` deps + `make` (below) | [cited, README] |
| `mapfile_parser` 2.13.2 | Symbol sizes, sym lookup, JSON map | Already in `.venv` [verified] | `symbol_sizes_csv`, `sym_info` ran OK [verified] |
| `mips-linux-gnu-{nm,objdump,addr2line}` + `build/n64-us/mm-n64-us.elf` | Symbolizing PCs / profiles (153,062 symbols) | Already installed [verified] | [verified] |
| libgfxd (in tree, `tools/ZAPD/lib/libgfxd`) | Disassemble display lists dumped from RDRAM (F3DEX2 family) | Already built (`libgfxd.a`) [verified] | F3DZEX-specific opcodes not checked [unverified] |
| libdragon `rdpq_debug.c` (`rdpq_validate`, `rdpq_debug_disasm`) | Disassemble/validate raw RDP command streams (e.g. from ares RDP tracer) | Copy/compile host-side; the file guards N64-only includes with `#ifdef N64` (`src/rdpq/rdpq_debug.c:9`) | host build [unverified] |
| F3DEX3 (Sauraen) | Microcode with 21 built-in RSP/RDP perf counters (FIFO-full stall cycles, vtx/tri cycles, DMA stalls) and also an optimization candidate | Port from OoT guide; MM uses `gspF3DZEX2_NoN_PosLight_fifo` (`src/code/sys_ucode.c:10`) | OoT-targeted; MM port effort [unverified] |
| Python: numpy/scipy/pandas/matplotlib | Distributions, bootstrap CIs, paired tests, plots | `uv` (present at `~/.local/bin/uv`) | not installed [verified] |
| BizHawk Ares64 (optional) | `.bk2` input movies + Lua if in-ROM input proves insufficient | Mono build under Xvfb | not tested (companion doc) |

Not useful for N64 representativeness: **2Ship2Harkinian** (https://github.com/2ship2harkinian/2ship2harkinian) and **Zelda64Recomp** (https://github.com/Zelda64Recomp/Zelda64Recomp). Both are PC ports or recompilations that run on a host GPU (Zelda64Recomp uses RT64), so their frame times tell you nothing about the N64 CPU, RSP or RDP [cited, READMEs]. They are still useful as references for *game-logic* 60fps decoupling.

## 1. What the decomp already has

All paths are relative to repo root.

| What | Where | Retail build? |
|---|---|---|
| `gRSPAudioTimeAcc/GfxTimeAcc/OtherTimeAcc` accumulate RSP time per task type at RSP-done; start stamped at task start | `src/code/sched.c:362-376` (start), `sched.c:468-481` (accumulate) | compiled in [verified] |
| `gRDPTimeAcc = osGetTime() - sRDPStartTime` (assignment, not `+=`); `sRDPStartTime = sRSPGFXStartTime` | `sched.c:529`, `sched.c:388-391` | compiled in. Measures **gfx-task start → DP full-sync wall time** (includes RSP time and audio-preemption time). Not RDP busy time [verified, source reading] |
| Per-frame latch into `gRSPGfxTimeTotal`, `gRSPAudioTimeTotal`, `gRDPTimeTotal`, `gGraphUpdatePeriod` (time between Graph_Update ends) | `src/code/graph.c:337-351` | compiled in [verified] |
| Variable definitions and docs; `gRSPOtherTimeAcc`/`gUnkTimeAcc` are never latched | `src/code/speed_meter.c:12-45`, `include/z64speed_meter.h` | [verified] |
| SpeedMeter bars (time + arena/THA/THGA usage) | `speed_meter.c:104` (`DrawTimeEntries`), `:213` (`DrawAllocEntries`), called from `game.c:114-117` when `R_ENABLE_ARENA_DBG != 0` | compiled in, **off**: `R_ENABLE_ARENA_DBG` = `SREG(0)` (`include/regs.h:70`), zeroed at `src/code/main.c:75`. Retail has no reg-editor input; `gRegEditor` is only initialized in `src/code/z_debug.c` [verified]. A known bug returns early without `CLOSE_DISPS` (`speed_meter.c:121`) |
| VI period `gIrqMgrRetraceTime` (measured once at boot) | `src/boot/irqmgr.c:198-205`; `sIrqMgrRetraceCount` `irqmgr.c:207`; `sched->retraceCount` `sched.c:425` | compiled in [verified] |
| Frame-rate divisor: `R_UPDATE_RATE` = `SREG(30)`, `gameState->framerateDivisor`, `gFramerateDivisor*` | `game.c:22-44`; default 3 (=20 fps) at `game.c:243`; set to 2/3 in `z_play.c:736-881`, `z_kaleido_setup.c:137`, `z_message.c:5801` | [verified] |
| Frame present / lag mechanics: swap happens when `updateTimer` (= divisor) reaches 0 on retrace | `sched.c:45-58`, `sched.c:302-315`, `sched.c:440-444`; `cfb->updateRate` set `graph.c:236` | [verified] |
| **Workload feedback**: `func_80173B48` = ns(divisor × VI) − ns(gRDPTimeTotal) | `game.c:300-304`; users `z_kankyo.c:2080`, `ovl_Object_Kankyo/z_object_kankyo.c:521`, `ovl_player_actor/z_player.c:10320,10336,10346` | [verified]; must be neutralized for benchmarks |
| Arena stats: `ZeldaArena_GetSizes`, `GetFreeArena` (system), `__osGetSizes`, `__osCheckArena`, `ArenaImpl_GetAllocFailures` | `src/code/z_malloc.c:40`, `src/boot/libc64/malloc.c:32`, `src/boot/libc64/__osMalloc.c:644,676,701` | compiled in. Gives current free/alloc/max-free only. **No high-water mark** tracked [verified]. No DebugArena in MM (grep) [verified] |
| Game arena `THA` and display-list arenas `THGA` (polyOpa/polyXlu/overlay/work/debug) + overflow checks | `graph.c:310-329` (`THGA_IsCrash`), sizes in `include/gfx.h:33-42` (GfxPool 0x20310 ×2) | [verified] |
| `osSyncPrintf`/`rmonPrintf` are empty stubs; `PRINTF` compiles out (`DEBUG_FEATURES` undefined) | `src/boot/syncprintf.c`, `include/macros.h:90-104` | [verified]. No log output path exists in retail; you must add an IS-Viewer writer |
| Frame advance (Z+R) exists but cannot be enabled in game | `src/code/z_pause.c:1-40`, used `z_play.c:939` | [verified] |
| RNG seeded from time at Play init | `z_play.c:2260`; audio RNG mixes `osGetCount()` (`src/audio/lib/thread.c:156,967`) | [verified]. Non-deterministic on hardware |
| DPC counter register defines (no code reads them) | `include/PR/rcp.h:277-286`; `osDpGetStatus/osDpSetStatus` exist | [verified] |
| Map select gamestate in retail (boot-to-scene path) | `src/overlays/gamestates/ovl_select/z_select.c` (`MapSelect_LoadGame`); first state is `Setup_InitImpl`, `src/code/title_setup.c:55` | [verified] |
| Timer units | `OS_CPU_COUNTER` = 46.875 MHz (`include/PR/os_convert.h:7`); RCP counters 62.5 MHz | [verified] / [cited, n64brew] |

Handy addresses from the current US build map [verified via `mapfile_parser sym_info`]:

| Symbol | Address |
|---|---|
| `gRSPAudioTimeTotal` | `0x801FBAE0` |
| `gRSPGfxTimeTotal` | `0x801FBAE8` |
| `gRDPTimeTotal` | `0x801FBAF0` |
| `gGraphUpdatePeriod` | `0x801FBAF8` |
| `gIrqMgrRetraceTime` | `0x80096B70` |
| `gRegEditor` (pointer) | `0x801F3F60` |
| `gScheduler` | `0x801F81C0` |
| `gActorOverlayTable` | `0x801AEFD0` |

`SREG(0)` lives at `*gRegEditor + 0x14 + 96*2 = +0xD4`. This is computed from `include/regs.h:12-31`, not tested.

## 2. Measuring CPU vs RSP vs RDP per frame on N64

- **CPU**: COP0 `Count` runs at half the CPU clock (`osGetCount`/`osGetTime`, 46.875 MHz) [verified, `os_convert.h`]. Wall time alone overstates any one thread's cost because the scheduler, audio, padmgr and IRQ threads preempt it. HackerOoT handles this by logging thread switch events from `exceptasm.s` (`__osRecordProfilerEvent` on dispatch/interrupt) and computing per-thread time [verified, HackerOoT `src/libultra/os/exceptasm.s`, `src/debug/profiler.c`]. HackerSM64 subtracts audio preemption from game-thread intervals [verified, HackerSM64 `src/game/profiling.c:39-57`].
- **RSP**: stamp `osGetTime()` at task start and at RSP-done, as MM already does (§1). F3DEX3 can additionally report RSP-internal counters, including "cycles the RSP was stalled because the RDP FIFO was full", by writing them to the yield-buffer footer [cited, https://hackern64.github.io/F3DEX3/counters.html; verified struct in HackerOoT `include/debug/profiler.h`].
- **RDP**: four 24-bit counters at 62.5 MHz [cited, https://n64brew.dev/wiki/Reality_Display_Processor/Interface]:
  - `DPC_CLOCK` is free-running.
  - `DPC_BUFBUSY` counts cycles where the command FIFO is non-empty.
  - `DPC_PIPEBUSY` counts the "gross" time from the first command to `SYNC_FULL`.
  - `DPC_TMEM` counts net TMEM-load cycles and stops during RDRAM stalls.

  Clear them with `DPC_STATUS` `CLR_*` bits at gfx-task start and read them at DP-done. 24 bits at 62.5 MHz wraps after about 268 ms, so per-frame reads are safe. HackerSM64 does exactly this: it reads TMEM/BUFBUSY/PIPEBUSY, clears them, and reports `max(pipe,tmem,cmd)` as RDP time (`src/game/profiling.c:171-176`, conversion `:11`) [verified]. HackerOoT clears them at main-gfx start (`profiler_inline.h`, `osDpSetStatus(DPC_CLR_CLOCK_CTR|...)`) [verified].
- **Frame/lag**: count VIs between framebuffer swaps. Record `sched->retraceCount` in `Sched_SwapFramebufferImpl` (`sched.c:45`); a delta greater than `updateRate` is a lag frame. This can also be observed *outside* the ROM through ares's VI I/O tracer (`VI_ORIGIN` writes) [unverified, tracer exists `ares/n64/vi/debugger.cpp:2`].
- **Which emulators implement these faithfully: none.** Details in §5.

## 3. Community practice (what to borrow)

| Source | What | Relevance |
|---|---|---|
| **HackerOoT profiler** (https://github.com/HackerN64/HackerOoT, `src/debug/profiler.c`, `include/debug/profiler_inline.h`, `exceptasm.s`) | Event timeline of thread switches, RSP gfx/audio/other start/end, RDP end; DPC counters; F3DEX3 counters; on-screen trace | **Closest match**: same Zelda64 engine (sched/graph). Port target [verified, source]. Last commit to `profiler.c` 2025-07-02 [verified] |
| **HackerSM64** `profiling.c` / `puppyprint.c` (v2.4.0) | Ring buffer over 64 frames (`PROFILING_BUFFER_SIZE`), CPU per subsystem, audio preemption subtraction, DPC counters; `ENABLE_CREDITS_BENCHMARK` = run the credits as a fixed benchmark (`include/config/config_benchmark.h`) | Methodology precedent: fixed, input-free benchmark scene [verified] |
| **F3DEX3** (https://github.com/HackerN64/F3DEX3) | Counter sets default/A/B/C; hand-counted cycle tables (F3DEX2 tri draw 172 vs F3DEX3 151 cycles) | Measurement and optimization [cited, docs `Performance.md`] |
| **libdragon** | `rdpq_debug` validator/disassembler (`include/rdpq_debug.h`); `debugf` to IS-Viewer/USB/SD (`include/debug.h:40-137`); rspq profiler (`rspq_profile_start/next_frame`) only on the `preview` branch (`include/rspq_profile.h`) | Validator/disasm reusable host-side [unverified]; rspq profiler is libdragon-ucode only, **not applicable** to MM's libultra + F3DZEX2 [verified] |
| **emux spec** (Giovanni Bajo / rasky) (https://n64brew.dev/wiki/Emux) | Unused COP0 opcodes as emulator hooks: XLOG, XHEXDUMP, XPROF slots, XPROFREAD metrics, XIOCTL exit/fast/slow | Spec says the opcodes are "completely harmless when run on real hardware" [cited]. Implemented in ares [verified, `ares/n64/cpu/emux.cpp`]. gopher64 has only a no-op `emux_xdetect` stub (`src/device/cop0.rs:184`) [verified] |
| **libultra `osProfile*`** (decompals/ultralib `src/debug/profile.c`) | Timer-interrupt PC histogram per text range, shipped to a dev host | Only in non-`_FINALROM` builds and needs dev-kit host I/O [verified, source]. The *pattern* (COMPARE-interrupt EPC histogram) is reusable over IS-Viewer/USB [unverified] |
| **Kaze Emanuar** (SM64 optimization) | The main themes he reports are RDRAM/bus usage and cache behavior, tested on real hardware | His own methodology videos were **not** checked here. A podcast transcript (https://softwareengineeringdaily.com/wp-content/uploads/2024/03/SED1666-Kaze_Emanuar.txt) has no tool details [verified]. Other claims are from secondary sources [unverified] |
| OoT/MM 60fps **on N64** | No primary-source N64 implementation found. Discussion only (e.g. https://github.com/ZoeyZolotova/mm-rando/issues/142). 60fps exists only in PC ports | [unverified / not found] |

## 4. Emulator-side profiling

**ares** (recommended; source read at `a776c50`, 2026-09-24; host binary `~/.local/bin/ares` = `0aafd85` 2026-05-28):

- **emux** needs Homebrew Mode. The setting is `General/HomebrewMode` in the host build [verified, `--dump-all-settings`]. On master it is bound as `Developer/HomebrewMode` (`desktop-ui/settings/settings.cpp:125`) [verified, source].
  - **XPROF** slots (start/stop/clear/reset) and **XPROFREAD** metrics: CPU cycles and exception cycles, I-cache/D-cache hits/misses/writebacks, RSP cycles and halted cycles, and RDRAM bytes per bus master (CPU icache/dcache/uncached, SP DMA, PI, SI, DP, AI, VI) (`ares/n64/cpu/emux.cpp:88-185`) [verified].
  - The RSP metrics were added in `6fcaa213` on 2026-06-02, after v148 (2026-05-30) [verified, GitHub API]. So **build master**.
  - **XIOCTL** 0x1 exits the emulator, which gives clean scripted termination (`emux.cpp:187-200`) [verified].
  - **XLOG** writes to the EMUX tracer/terminal [verified, source]. End-to-end output was not exercised on MM [unverified].
- **Tracers**:
  - CPU instruction (works with the recompiler by enabling an instruction prologue, `cpu/debugger.cpp:5-9`), CPU exception/interrupt/TLB, RSP instruction/I/O, **RDP command** (every RDP opcode logged, `rdp/render.cpp` + `rdp/debugger.cpp`), VI/AI/PI/SI/MI I/O [verified, source].
  - They are toggled from the GUI Tools → Tracer window (can log to file, `desktop-ui/tools/tracer.cpp:39`). **No CLI switch found** [verified, `desktop-ui.cpp` arg list]. Headless use needs xdotool or a small patch [unverified].
- **GDB stub**: memory read verified (companion doc). PC/register mapping was off (`0xffffffff`), so **GDB-based PC sampling is unproven** [verified, companion doc].
- **Savestates**: `--save-state <slot>` loads at boot (`desktop-ui.cpp:105`) [verified]. Savestates don't transfer to hardware, so prefer in-ROM scene setup.
- **Input**: no movie/scripted-input feature (companion doc) [verified there]. Use in-ROM scripted input.

**mupen64plus-core** (`ba95bab`, 2026-10-02):

- The debugger API (`src/api/debugger.c`) offers memory read/write, breakpoints and a per-instruction callback (`DebugSetCallbacks(..., dbg_frontend_update(pc), ...)`) when built with `DEBUGGER=1`. That gives exact per-function **instruction** counts through a custom frontend [verified, source].
- Timing is `CountPerOp`-approximate (companion doc), so the counts are not cycles.

**gopher64**: no debugger/scripting CLI (companion doc); emux not implemented (stub) [verified].

**Symbolizing PCs** [verified unless noted]:

- `build/n64-us/mm-n64-us.elf` has full symbols, but **overlay symbols sit at link-time VRAM** (e.g. `EnHorse_Update` at `0x808872a4`). At runtime overlays are relocated.
- To map a runtime PC, read each loaded overlay's `loadedRamAddr` (+`vramStart`) from:
  - `gActorOverlayTable` (`include/z64actor_dlftbls.h:37`)
  - `gGameStateOverlayTable` (`include/z64game.h:34`)
  - effect-SS table (`include/z64effect_ss.h:45`)
  - Kaleido manager (`include/kaleido_manager.h:16`)
- Then subtract and look the result up in the ELF [design, unverified].

**Display lists / RDP streams**:

- Dump the gfx master DL / pools from RDRAM via GDB and decode with in-tree libgfxd [unverified for F3DZEX opcodes].
- Dump the RDP command stream from the ares RDP tracer and decode/validate with libdragon `rdpq_debug_disasm`/`rdpq_validate` built host-side [unverified].
- Use this for TMEM-load and fill-mode analysis, since there is no RDP *time* on the emulator.

## 5. Emulator timing validity vs real hardware

| Aspect | ares | gopher64 | mupen64plus | Implication |
|---|---|---|---|---|
| RDP execution time | **Zero**. `flushCommands` renders synchronously, then clears busy (`rdp/io.cpp:196-209`); `SYNC_FULL` raises DP IRQ immediately (`rdp/render.cpp:617-624`) [verified] | Heuristic: DP IRQ after `scissor_area/2` cycles regardless of content (`parallel-rdp/interface.cpp:1017-1024`) [verified] | DP IRQ raised right after processing (`rdp_core.c:117-121`) [verified] | **No emulator measures fill rate / RDP cost.** `gRDPTimeTotal` ≈ RSP gfx time on ares |
| DPC counters | `DPC_CLOCK` free-running; `BUFBUSY`/`PIPEBUSY` read back as 0/1 flags; `TMEM` read has a bug (`if(data == 7)` instead of address) so it returns 0 (`rdp/io.cpp:38-62`) [verified] | `DPC_CLOCK` returns constant `0xFFFFFF` (`rdp.rs:70`); writes ignored [verified] | Never incremented (`rdp_core.c:101-106`) [verified] | DPC-based RDP numbers are hardware-only |
| RSP timing | Pipeline/dual-issue/stall model in interpreter and recompiler (`rsp/rsp.cpp:49-112`, `rsp/rsp.hpp:190-240`) [verified] | LLE (companion doc) | HLE by default | ares RSP compute time is plausible [unverified vs HW]. **No RDP back-pressure**, so FIFO-full stalls never occur on ares (MM uses the `_fifo` ucode) |
| CPU caches | I/D cache emulated with **fixed** fill costs (D-cache line fill/writeback `cpu.step(40*2)`, `cpu/dcache.cpp:7,16`; `cpu.hpp:209,217` 48×2) [verified] | I/D cache with cycle costs (companion doc) | none | Miss *counts* are usable [unverified]. Miss *latency* ignores RDRAM contention |
| RDRAM contention / refresh | Not modeled (fixed latencies; only byte counters) [verified, grep] | not checked | no | On hardware, RDP stalls on RDRAM (n64brew: TMEM counter "stopped during cycles in which the RDP is stalled for RDRAM") [cited]. CPU misses during heavy RDP work cost more [unverified magnitude] |
| CPU instruction timing | Fails 1593/1604 nemu64-test timing tests (companion doc) [verified there] | — | `CountPerOp` approx | Absolute CPU µs are indicative only |

When real hardware is mandatory:

- any RDP-bound question (fill rate, overdraw, texture loads, 1- vs 2-cycle mode, AA/Z cost)
- RSP time when the RDP is the bottleneck (FIFO full)
- absolute lag-frame counts
- anything involving `func_80173B48` feedback
- final acceptance

ares remains the right place to iterate on CPU-side code and get exact, repeatable *relative* deltas.

**Flashcarts** [cited]:

- **SummerCart64**: open source, about 23.8 MiB/s USB, IS-Viewer 64 buffer at a movable ROM offset (`0x03FF0000` typical). UNFLoader protocol through `sc64deployer debug` (https://github.com/Polprzewodnikowy/SummerCart64 README, `docs/00_quick_startup_guide.md:73-76`, `docs/04_config_options.md:95-105`).
- **UNFLoader** supports 64drive HW1/HW2 (fw 2.05+), EverDrive 3.0 and X7 (OS 3.04+), and SC64. It provides `usb.c`/`debug.c` for libultra and libdragon, plus a GDB stub (`USE_RDBTHREAD`) (https://github.com/buu342/N64-UNFLoader README, `USB+Debug Library/README.md:209-214`).
- SC64 is the pick: it is open, and the same IS-Viewer writes work on ares and on SC64, so there is one code path. With UNFLoader `usb.c` you would need a second backend.

## 6. Memory analysis

- **Layout** [verified]:
  - MM refuses to run below 8 MB (`Check_ExpansionPak`, `src/code/sys_initial_check.c:106-118`).
  - Framebuffers are fixed at `0x80780000` up to 8 MB end (`include/buffers.h:38-55`, static_assert).
  - System heap = from end of `buffers` segment to framebuffers (`src/code/main.c:60-70`).
  - Audio heap is 0x138000 bytes (`buffers.h:24`).
  - Two GfxPools of 0x20310 bytes each (`include/gfx.h:33-42`).
  - ZeldaArena is carved out of the Play THA remainder (`z_play.c:2304-2309`).
- **Static sizes**: `.venv/bin/python -m mapfile_parser symbol_sizes_csv build/n64-us/mm-n64-us.map` produced 4,975 rows [verified]. Also `sym_info` [verified] and `jsonify` (takes an `-o`-style arg; the positional output failed [verified]). Existing `tools/get_actor_sizes.py` and `tools/sizes/*.csv` are in tree [not examined].
- **Runtime arenas**:
  - Sample `ZeldaArena_GetSizes`, `GetFreeArena`, `THA_GetRemaining(&state->tha)` and `THGA_GetRemaining` for polyOpa/polyXlu/overlay/work/debug every frame. Keep a max in the benchmark module, since retail tracks no peaks.
  - `ArenaImpl_GetAllocFailures` flags failed allocations.
  - Peak display-list usage is critical at 60fps, because DL arenas are reused every frame.
- **TMEM**: there is no counter for occupancy (4 KB). Derive loads from `LoadBlock/LoadTile/LoadTLUT` in the RDP stream (ares tracer) and load *time* from `DPC_TMEM` on hardware [design].

## Benchmark methodology

**Build variants.**

- Add a `BENCH` knob to the Makefile, defaulting off so the matching build is untouched. Any instrumentation requires `COMPARE=0` (or `NON_MATCHING=1`); see `Makefile:42-103` [verified].
- **Layout confound**: adding code shifts addresses and changes I-cache conflicts. Keep *identical* instrumentation in both arms of an A/B. Optionally run a "padding-only" control to size layout noise [unverified magnitude].

**Scene setup (in ROM, `#if BENCH`)**:

1. In `Setup_InitImpl` (`title_setup.c:55`), go straight to Play with a fixed entrance, time of day, form and save data, the way `MapSelect_LoadGame` does (`ovl_select/z_select.c:21`).
2. Replace `Rand_Seed(osGetTime())` (`z_play.c:2260`) with a constant. Consider the audio RNG too (`audio/lib/thread.c:967`).
3. Make `func_80173B48` return a constant budget, e.g. the value for a 60fps frame at RDP=0. Otherwise particle counts depend on RDP speed (§1).
4. Feed inputs from a per-frame table at `GameState_GetInput` (`game.c:149`), recorded once by logging real pad input.
5. Set the divisor under test (1 for 60fps).

**Run protocol**:

1. Warm up W frames (≥ 60; covers overlay loads, object DMA, audio start, first-frame cache misses), then measure N frames (e.g. 600 = 10 s at 60 Hz).
2. Write one fixed-format record per frame to IS-Viewer:
   - frame#
   - VI delta between swaps
   - CPU per-thread µs
   - RSP gfx/audio µs
   - gfx-task wall µs
   - DPC clock/buf/pipe/tmem (on hardware)
   - THGA/THA/Zelda/System arena used
   - actor count
   - optionally emux XPROF deltas
3. Print `END` and call `XIOCTL exit` on ares.
4. Host side: `ares … --setting General/HomebrewMode=true` under Xvfb, capture stdout, then parse.

**Repeats**:

- **ares**: run 3× and diff the logs. If byte-identical (expected but [unverified]: recompiler + threaded GPU), one run per variant is enough.
- **Hardware**: ≥ 10 runs per variant, power-cycling or resetting between runs. Even with fixed seeds, hardware is not cycle-deterministic (VI/refresh phase, `osGetCount`-mixed audio RNG).

**Metrics**:

- frame time distribution (VI-quantized: report lag-frame count and % of frames with VI delta > divisor)
- CPU-thread µs: median / p95 / p99 / max
- RSP µs and RDP pipe-busy µs, same statistics
- RDP stall share `(pipe − max(cmd,tmem))`
- arena high-water marks
- emux cache-miss and RDRAM byte totals (ares)

Unit conversions:

- `OSTime` ticks × 64/3000 = µs (`OS_CYCLES_TO_USEC`, `os_convert.h:13`)
- RCP cycles / 62.5 = µs
- The 60fps budget is one VI: read the actual `gIrqMgrRetraceTime` (≈16.7 ms NTSC) and don't hard-code it.

**Statistics**:

- Because inputs are scripted, frames align across variants. Use **paired per-frame differences**: report the median Δ with a **bootstrap 95% CI** (resampling runs, then frames, for hardware), and a Wilcoxon signed-rank test.
- Compare tails (p95/p99) and lag-frame counts, not just means. Lag is what players see.
- Report results per scene. Never average across scenes.

**Scene set** (start small):

- an input-free cutscene (like HackerSM64's credits benchmark)
- Clock Town (South) at day with NPC crowds
- a rain/snow scene (exercises the §1 feedback path)
- Termina Field (large draw distance)
- a boss arena
- the pause menu (Kaleido; the divisor changes to 2 at `z_kaleido_setup.c:137`)

## Install list for unicron

Already present [verified]:

- `docker` 29.1.3
- `Xvfb`/`xvfb-run`
- `gdb-multiarch`
- `mips-linux-gnu-{nm,objdump,addr2line}`
- `perf` 7.0.14
- `vulkaninfo`
- `uv` (`~/.local/bin/uv`)
- `.venv` with `mapfile_parser` 2.13.2
- an ares binary at `~/.local/bin/ares` → `~/.local/opt/ares` (`0aafd85`, 2026-05-28, has emux strings)
- `tools/ares/ares-headless.sh` (untracked)

These appear to come from the parallel emulator work. I did not create them.

To add (not executed):

```bash
# 1. ares at master (>= 6fcaa213, emux RSP counters) - reuse the companion doc's Dockerfile,
#    replacing the clone line with:
#    RUN git clone -q https://github.com/ares-emulator/ares.git /src/ares && git -C /src/ares checkout a776c509bdecdafad9aefac677b57abbb77ea703
#    (the Dockerfile text lives in n64-emulator-accuracy.md; save it to a scratch dir first)
docker build -t ares-n64:master <dir-containing-that-Dockerfile>

# 2. Python analysis env (separate from the build venv)
uv venv ~/.venvs/n64perf && uv pip install --python ~/.venvs/n64perf numpy scipy pandas matplotlib

# 3. Only if a 64drive/EverDrive is used (UNFLoader, per its README)
sudo apt-get install libncurses5-dev libncursesw5-dev libftdi1-dev libusb-1.0-0-dev libudev-dev
git clone https://github.com/buu342/N64-UNFLoader && make -C N64-UNFLoader/UNFLoader

# 4. SummerCart64: download sc64deployer from https://github.com/Polprzewodnikowy/SummerCart64/releases
#    usage: sc64deployer upload build/n64-us/mm-n64-us.z64 && sc64deployer debug --isv 0x03FF0000
#    (asset names / udev rules not checked)
```

Host-side `perf` is only useful for profiling **the emulator** (e.g. to raise ares throughput, about 255 VI/s per the companion doc). It tells you nothing about N64 cost.

## Open questions

1. Does ares's emux XPROF/XLOG work end-to-end with an IDO-built libultra ROM? Inline COP0 `.word` encodings are needed. The spec's encoding was read from n64brew [cited], but not yet exercised.
2. Are ares runs of MM byte-deterministic run-to-run (recompiler, threaded paraLLEl-RDP)?
3. How close is ares RSP time to hardware for F3DZEX2 *without* FIFO back-pressure? Needs a hardware A/B with `DPC` counters.
4. Can ares tracers (RDP command, CPU instruction) be enabled headless without a GUI patch?
5. How much I-cache/layout noise does instrumentation add on hardware?
6. Is porting F3DEX3 to MM (from F3DZEX2 `_fifo`) worth it for its counters alone, or should the RSP be measured with timestamps only?
7. What is the full list of MM code paths that read timing (only `func_80173B48` and its 5 callers were found via `gRDPTimeTotal` grep), and do any read `osGetTime` for gameplay?
8. Kaze's own profiling workflow: primary videos not reviewed.

## Sources

Repo (this checkout, HEAD `56fa21dd0`):

- `src/code/{speed_meter.c,sched.c,graph.c,game.c,main.c,z_play.c,z_pause.c,z_debug.c,z_kankyo.c,title_setup.c,sys_initial_check.c,sys_ucode.c,z_malloc.c}`
- `src/boot/{irqmgr.c,syncprintf.c,libc64/*}`
- `include/{z64speed_meter.h,regs.h,macros.h,buffers.h,gfx.h,PR/rcp.h,PR/os_convert.h}`
- `Makefile`, `build/n64-us/mm-n64-us.{map,elf}`

External:

- ares: https://github.com/ares-emulator/ares. Source at `a776c50`: `ares/n64/{rdp,rsp,cpu,cartridge}/*`, `desktop-ui/*`. emux commits `a0f76298`, `6fcaa213`. Releases: https://github.com/ares-emulator/ares/releases
- Emux spec: https://n64brew.dev/wiki/Emux (moved from https://hackmd.io/@rasky/r1k7na6Jn)
- RDP interface registers: https://n64brew.dev/wiki/Reality_Display_Processor/Interface
- RDRAM: https://n64brew.dev/wiki/RDRAM
- gopher64: https://github.com/gopher64/gopher64 (`1ab3793`): `src/device/rdp.rs`, `parallel-rdp/interface.cpp`, `src/device/cop0.rs`
- mupen64plus-core: https://github.com/mupen64plus/mupen64plus-core (`ba95bab`): `src/device/rcp/rdp/rdp_core.c`, `src/api/debugger.c`
- HackerSM64 v2.4.0: https://github.com/HackerN64/HackerSM64 (`src/game/profiling.c`, `include/config/config_{debug,benchmark}.h`)
- HackerOoT: https://github.com/HackerN64/HackerOoT (`src/debug/profiler.c`, `include/debug/profiler{,_inline}.h`, `src/libultra/os/exceptasm.s`)
- F3DEX3: https://github.com/HackerN64/F3DEX3, https://hackern64.github.io/F3DEX3/counters.html, `docs/Documentation/{Performance,Porting your Romhack}.md`
- libdragon: https://github.com/DragonMinded/libdragon (trunk `e356bf3`: `include/rdpq_debug.h`, `src/rdpq/rdpq_debug.c`, `include/debug.h`; `preview` branch: `include/rspq_profile.h`)
- ultralib: https://github.com/decompals/ultralib (`src/debug/profile.c`)
- UNFLoader: https://github.com/buu342/N64-UNFLoader (`3821703`)
- SummerCart64: https://github.com/Polprzewodnikowy/SummerCart64 (`a1e7996`)
- libgfxd: https://github.com/glankk/libgfxd (vendored at `tools/ZAPD/lib/libgfxd`)
- PC ports: https://github.com/2ship2harkinian/2ship2harkinian, https://github.com/Zelda64Recomp/Zelda64Recomp
- Kaze Emanuar interview transcript: https://softwareengineeringdaily.com/wp-content/uploads/2024/03/SED1666-Kaze_Emanuar.txt
- Community 60fps discussion: https://github.com/ZoeyZolotova/mm-rando/issues/142
