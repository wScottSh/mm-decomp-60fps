# Adding hardware-accurate timing to an N64 emulator (for MM perf work)

Research date: 2026-10-04. Host: unicron (Ubuntu 24.04, 32 cores, RADV GFX1151).

Companion docs (not repeated here):

- [`n64-emulator-accuracy.md`](n64-emulator-accuracy.md): emulator choice → ares; nemu64-test results.
- [`n64-perf-tooling.md`](n64-perf-tooling.md): in-ROM benchmark design, emux, and ares §5 timing-validity table.

Legend:

- **[verified here]**: read in source at the stated commit, or run on this machine.
- **[cited]**: a primary source says so (link given). Not re-checked unless noted.
- **[estimate]** / **[inference]**: my judgment. Not a fact.

Source snapshots:

- ares `a776c50` (`~/src/ares`)
- gopher64 `1ab3793`
- simple64 (archived)
- cen64 `e0641c8`
- MiSTer N64_MiSTer `5725381` (2026-09-27)
- nemu64-test `9a8b9f7`
- rasky/n64-systembench `845635c`
- libdragon `e356bf3`

All clones are in the session scratchpad.

## TL;DR

**Verdict: feasible as a *calibrated, cycle-approximate* model. Not feasible as a hardware-exact one, and not verifiable to hardware-exact without hardware [estimate].**

What is feasible:

- A timing layer on ares that produces RDP busy time, RSP FIFO back-pressure, RDRAM contention and lag frames.
- Plausible accuracy is roughly ±10–25 % on per-frame RDP time and ±5–10 % on CPU time [estimate].
- It should keep benchmark runs at about 1–3 minutes [estimate].

What blocks "must match a real N64":

- No public, hardware-measured per-command RDP cost data exists [cited, see §2].
- The MiSTer core is not a timing oracle for the RDP or memory [verified here, §2a].
- So the RDP half of the model can only be checked against theory, a few game-level symptoms, and any hardware runs you can borrow.

**Recommended base: ares master.** Reasons:

- It already has LLE RSP with pipeline stalls.
- It has an I/D-cache model and per-bus-master RDRAM byte counters.
- It has emux XPROF hooks.
- The scheduler can already stall the CPU inside I/O (`ad16c4a67`, "introduce timing in memory handlers").
- It is ISC-licensed.
- Its maintainers have open issues that this work would fix, MM's own included (ares #2320, below).

gopher64 bans AI-agent-co-authored PRs [cited, README]. simple64 is archived. mupen64plus has no cache model. cen64 is dormant and has no RDP timing [verified here].

**Approach.** Keep paraLLEl-RDP for pixels and add a separate timing model that runs on the same command stream:

1. **RDP timing model (new, CPU-side C++).** Decode commands and walk triangle and rectangle edges to get per-scanline span lengths. Charge cycles by cycle type:
   - setup cost
   - span cost per mode: 1-cycle 1 px/clk, 2-cycle 2 clk/px, copy 4 px/clk, fill 8 B/clk
   - TMEM load cost
   - sync cost
   - framebuffer/Z read-modify-write traffic, issued as RDRAM requests

   No pixels are computed.
2. **Make the RDP a time-stepped device** instead of `flushCommands()` doing everything at `DPC_END`:
   - `DPC_CURRENT` advances as commands retire.
   - `DPC_CLOCK/BUFBUSY/PIPEBUSY/TMEM` count for real.
   - `SYNC_FULL` raises the DP interrupt at the modeled time.

   MM's `_fifo` microcode then sees real back-pressure through its 96 KiB RDRAM FIFO.
3. **Add an RDRAM arbitration model** with one shared bus timeline:
   - requesters: CPU, SP DMA, DP command fetch, DP framebuffer/Z/TMEM, VI, AI, PI, SI
   - bank/page state
   - refresh

   CPU misses and uncached accesses and the DMAs pay modeled latency.
4. **Fix the known CPU costs.** Examples: uncached RDRAM read is ~0 now vs ~32–36 cycles on hardware; there is no write buffer. Use nemu64-test timing and n64-systembench as tests.
5. **Calibrate** the RDP and bus parameters with:
   - theory and SDK peak rates
   - n64brew sync costs
   - the MM/OoT file-menu slowdown seen on hardware (ares #2320 has hardware videos)
   - ideally one borrowed hardware run of a DPC-counter microbenchmark ROM

**Effort [estimate]:**

- Size: about 5–9 k lines of C++.
- Time: about 3–5 person-months for an experienced N64 emulator developer to reach a calibrated v1, or about 6–8 weeks for a "first useful" v0 (RDP model + back-pressure, coarse contention).
- Calibration and validation is the long pole, not the code.

**Top risks:**

1. There is no ground truth for RDP or contention without hardware.
2. RDRAM contention needs finer CPU↔RCP interleaving than the JIT's 4096-clock slices, which costs speed.
3. Lag frames are threshold effects, so a small error flips frames that sit near the 3-VI budget.
4. The upstream ares fit is unknown. Expect a fork first.

## 1. Gap inventory

### 1.1 ares (`a776c50`)

| Component | What exists (file:line) [verified here] | What's missing for hardware timing |
|---|---|---|
| **VR4300 pipeline** | 1 cycle per instruction (`cpu/memory.cpp:158` `step(1*2)` in `fetch`). Explicit MULT/DIV/FPU latencies (`cpu/interpreter-ipu.cpp:289-647`, `interpreter-fpu.cpp:470+`). | No load-use, branch or register-dependency interlocks beyond these. Fails 1593/1604 nemu64-test timing tests, 5/13 cycle tests and 5/5 COP0-hazard tests (companion doc). Many failures look like systematic one-cycle offsets [inference, companion doc]. |
| **I-cache miss** | Fixed `step(48*2)` per line fill and writeback (`cpu/cpu.hpp:209,217`). | Contention-dependent latency. No critical-word-first / restart timing. |
| **D-cache miss** | Fixed `step(40*2)` per fill and per writeback (`cpu/dcache.cpp:7,16`). | Hardware median is 41 (VI off) / 42 (VI on), but the measured range is 38–103 cycles (nemu64-test `src/tests/timing/cache.rs:197-260`) [verified here: values in test source]. The spread is contention, and none of it is modeled. |
| **Uncached RDRAM access** | `MI::readRdram` → `rdram.ram.read` charges **no cycles** (`mi/bus.hpp:2-12`, `rdram/rdram.hpp:41-58`). Only the 1-cycle instruction step applies. | Hardware ≈ 32–36 CPU cycles per uncached load. It is bank-dependent: 36 in the VI framebuffer's bank, 32 elsewhere (nemu64-test `cache.rs:292-345`). systembench expects 34 cycles per U32 read and 134 per 4 sequential reads (`n64-systembench/src/main.c:577-584`) [verified here: source]. This matters for libultra/MM uncached accesses (`osVirtualToPhysical` buffers, `K1` reads). Count in MM not measured. |
| **Uncached writes / write buffer** | `DefaultWriteCycles = 0`, with the comment "not implemented until we implement the CPU write queue" (`memory/io.hpp:3-4`). | 4-entry write buffer (nemu64-test "4 element write-buffer for uncached memory", `timing/mod.rs:8622`). |
| **RCP register (MMIO) access** | Flat `DefaultReadCycles = 20` (`memory/io.hpp:3,8`). PI bus `250*2` (`pi/bus.hpp:63`). | systembench expects RCP I/O read = 24 CPU cycles, PI I/O read 144 / write 134 RCP cycles (`main.c:586-595`) [verified here: source]. Small fixes. |
| **COP0 hazards** | Not modeled (5/5 fail). | Low perf relevance for MM [inference]. |
| **PI DMA** | Formula from BSD domain timing (`pi/dma.cpp:106-111`). A more accurate block-split version (`bea395b24`, rasky) was **reverted** (`807ef0300`, 2025-08) [verified here: git log; revert reason not recorded in the message]. | Check against systembench PI DMA values (8 B: 193, 128 B: 1591, 1 KiB: 12168, 64 KiB: 777807 RCP cycles). MM streams objects and audio via PI, so this matters for load hitches. |
| **SI / joybus** | `pif.estimateTiming()` (`si/io.cpp:86`). | systembench has 12 joybus timing expectations. Low priority for MM. |
| **RSP core** | Interpreter/recompiler with dual-issue and stall model (`rsp/rsp.cpp:49-112`, `rsp.hpp:154-240`). | Plausible, but not validated against hardware [unverified]. nemu64-test has only 4 RSP-timing tests. |
| **SP DMA** | Fixed `(len+8)/8*3` clocks: 8 B per RCP cycle, no setup latency, no contention (`rsp/dma.cpp:1-73`). | n64brew: "about 3.7 bytes per VR4300 cycle (plus some small fixed overhead)" [cited, https://n64brew.dev/wiki/Reality_Signal_Processor/Interface]. ares (8 B/RCP-clk ≈ 5.3 B/CPU-clk) is therefore ~40 % too fast before any contention [inference from the two figures]. |
| **RSP→RDP FIFO (MM uses `gspF3DZEX2_NoN_PosLight_fifo`, DRAM mode)** | RSP `MTC0` to DPC regs → `rdp.writeWord` (`rsp/interpreter-scc.cpp:4,10`). A `DPC_END` write calls `flushCommands()`, which renders everything and sets `current=end` at once (`rdp/io.cpp:78-87,195-209`; `vulkan/vulkan.cpp:82-148`). | The FIFO never fills, so **RSP back-pressure stalls never happen**. MM's FIFO is 0x3000×8 B = 96 KiB in RDRAM (`include/sys_cfb.h:25,30`, set at `graph.c:202-203`) [verified here]. XBUS mode is functionally handled (DMEM source) but has no timing either. |
| **RDP execution time** | **Zero.** Commands are rendered on the GPU synchronously, and `SyncFull` waits for the GPU timeline then raises the DP IRQ (`vulkan/vulkan.cpp:136-139`, `rdp/render.cpp:617-624`). The software renderer bodies are empty (MAME RDP removed in `5f9804fb6`, 2023: "too slow to be usable"). | Everything: setup, span rates, TMEM loads, framebuffer/Z traffic, syncs. |
| **DPC counters** | `DPC_CLOCK` counts from scheduler time (`rdp/io.cpp:39-44`, `rdp.cpp:29-33`; `a10b9ce0e`). `BUFBUSY`/`PIPEBUSY` hold 0/1 flags. The `TMEM` read has a bug, `if(data == 7)` instead of `address` (`rdp/io.cpp:58`), so it never returns. | Real counting. |
| **RDRAM / RI** | Chip registers, `delay`, `refreshInterval` and `refreshRow` are stored as state only (`rdram/rdram.cpp:39,125-175`; `ri/io.cpp`). Per-device byte counters exist only in Homebrew Mode (`rdram/rdram.hpp:53-56`). The `RBusDevice::DP_DRAW` counter exists but is **never fed**, because paraLLEl writes RDRAM on the GPU (grep: only `n64.hpp:115`, `rdram.hpp:230`, `emux.cpp:179-181`). | No arbitration, bank/page model, refresh or contention. RDP framebuffer/Z traffic is invisible even to counters. |
| **VI scanout** | Vulkan path: paraLLEl scanout. The software path reads with `VI_DMA` (`vi/vi.cpp:200,218`). | No bandwidth cost. |
| **Scheduler** | CPU-led catch-up. Other threads run in `CPU::synchronize()` (`cpu/cpu.cpp:83-98`). JIT slice `JitInterleaving = 4096` clocks (`accuracy.hpp`). | Contention needs shorter slices, or an analytical "bus occupancy in window" model (§4). |

### 1.2 Other bases (briefly)

| | RDP time | CPU caches | Bus / contention | Notes |
|---|---|---|---|---|
| **gopher64** `1ab3793` | DP IRQ after `scissor_area/2` (`parallel-rdp/interface.cpp` ~1017-1024) [verified, companion doc] | I/D cache. Miss = `31 + len/3` cycles, citing hcs64 and rasky/n64-systembench (`src/device/rdram.rs:175-176`, `cache.rs:28,82`) [verified here] | none | AI-co-authored PRs banned [cited, README]. Panicked on nemu64-test (companion doc). |
| **simple64** (archived) | DP IRQ delay = VI width × height × 2 (`parallel-rdp-standalone/parallel_imp.cpp:428-430`, `rdp_core.c:153`) [verified here] | I/D cache | none | Unmaintained. |
| **mupen64plus** | IRQ immediately, counters never advance (companion doc) | none (CountPerOp) | none | Maintainers acknowledge CountPerOp is a hack (mupen64plus-core #953) [cited via sub-research]. |
| **cen64** `e0641c8` | none (Angrylion-derived renderer) | Fixed `DCACHE_ACCESS_DELAY (46-2)`, `ICACHE_ACCESS_DELAY (50-2)` (`vr4300/fault.h:15-16`), MCI interlocks (`cp1.c`) [verified here] | none | Most cycle-faithful VR4300 pipeline, but slow and dormant (companion doc). |

None of them models RDP duration or RDRAM contention [verified here for ares/gopher64/simple64/cen64; companion doc for mupen64plus].

## 2. Sources of truth for the missing timing

### 2a. MiSTer N64 core RTL (`N64_MiSTer` `5725381`)

The RTL was read in source by a sub-agent. The key claims below were spot-checked by me [verified here].

- **Clocks are real**: RCP 62.5 MHz, CPU 93.75 MHz, DDR3 side 125 MHz (`rtl/pll/pll_0002.v:31-37`). Per-clock *compute* rates therefore map 1:1 to hardware cycles. Memory rates do not.
- **RDP (~7.9 k VHDL lines):**
  - setup ≈ 1 clk per command dword (`RDP_command.vhd:166-172,500-664`)
  - edge walker 4 clk per scanline (`RDP_raster.vhd:623-735`)
  - 1-cycle 1 px/clk, 2-cycle 2 clk/px, copy 4 px/clk (16-bit only), fill 8 B/clk (`RDP_raster.vhd:913-1160`)
  - 10-stage pixel pipeline (`RDP_pipeline.vhd:149-159`)
  - TMEM load: 256 B DDR3 burst per line, then 1 dword/clk (`RDP.vhd:633-637`, `RDP_raster.vhd:1283-1340`)
- **Where the RDP is not hardware-like:**
  - **Every primitive waits for all pixel writes to drain** (`RDP_command.vhd:665-675`). That is an implicit full sync per primitive.
  - Framebuffer/Z data is fetched as whole-span DDR3 bursts.
  - Sync load/pipe/tile are `null; -- todo` (`RDP_command.vhd:220-231`).
  - **`DPC_CLOCK` is frozen for CIC-6105 games (MM, OoT).** The author's comment reads: "some games like OOT depend on them being either zero or very accurate — as timing is not yet accurate enough, only activate it outside of CIC 6105" (`RDP.vhd:484-488`).
  - BUFBUSY/PIPEBUSY are hacks, and TMEM is never counted.
- **Memory:**
  - RDRAM lives in DDR3 at 125 MHz × 64-bit (~1 GB/s peak vs RDRAM's ~500 MB/s) [inference on peak].
  - Arbitration is ad hoc: RSP DMA write FIFO > PI write > request ports by index (DD > VI > AI > CPU > SI > PI > savestate > RDP reads > RSP DMA reads) > RDP color/Z writes > VI. Bursts are non-preemptible (`DDR3Mux.vhd:245-375`).
  - The only deliberate latency is a per-region floor in `memorymux.vhd:288-441`, in 62.5 MHz clocks: RDRAM read ≥ 15, write ≥ 6; PI 137; PIF 230.
  - Optional debug "DDR3 Delay" and "Cache Delay" settings exist, default off. The README warns they "may cause games to hang".
  - RI/RDRAM registers have no timing effect.
  - "Turbo" builds are an ~80 MHz overclock.
- **Author's own position:**
  - The README makes no cycle-accuracy claim.
  - Peip on PSX (misterfpga.org t=7198, 2023-10-20): "cannot fulfill all timings 100% accurate, due to the DDR3" [cited via sub-research].
  - MiSTer issue #27 reports MM/OoT menus running too fast. #31 reports KI Gold slower than hardware [cited via sub-research].
- **Use:**
  - **Yes** as a readable reference for per-clock compute rates (span modes, edge-walk, setup, TMEM write rate, pipeline depth).
  - **No** for RDRAM latency, contention, framebuffer/Z access patterns or the counters.
  - Simulating it in NVC (~3000× slower) for small RDP microbenchmarks is possible: about 1 ms of RDP work ≈ 3 s simulated [estimate]. It would only cross-check compute rates, which are already known from theory.

### 2b. n64brew wiki [cited, retrieved via sub-research]

- RDP pipeline (https://n64brew.dev/wiki/Reality_Display_Processor/Pipeline):
  - "1 cycle (@ 62.5 MHz) per pixel"; 2-cycle "2 cycles … per pixel".
  - Sync costs: "Pipe Sync … 50 cycles … Tile Sync (33 cycles) … Load Sync (25 cycles)".
  - A "cycles corrupted" table per mode change.
  - Nothing on triangle setup cost, span buffer or FIFO depth.
- RDP interface counters (https://n64brew.dev/wiki/Reality_Display_Processor/Interface): exact definitions of CLOCK, CMD_BUSY ("any RCP cycle in which the internal RDP command FIFO is not empty"), PIPE_BUSY (gross, first command → SYNC_FULL) and TMEM_BUSY (net, stops during RDRAM stalls).
- RDRAM (https://n64brew.dev/wiki/RDRAM): ReadDelay 7, WriteDelay 1, AckDelay 3 (TCycles), packet formats. No absolute bandwidth.
- RDRAM Interface (https://n64brew.dev/wiki/RDRAM_Interface):
  - Refresh happens in HBLANK, with post-refresh retry delays of 54 (dirty) / 52 (clean) cycles.
  - All rows refresh in ~15.6 ms NTSC.
  - "RCP supports DMA bursts up to a maximum of 128 bytes".
  - VI HSYNC setting affects refresh rate and bandwidth.
- SP DMA ≈ 3.7 B per CPU cycle + fixed overhead (RSP Interface page).
- PI domain timings (Peripheral Interface page).
- **Not documented anywhere checked:** bus arbitration priority among CPU/RSP/RDP/VI/AI/PI, and VR4300 miss latency (only the tests below give it).

### 2c. Test ROMs with hardware-derived expectations

| Suite | Timing coverage | RDP / bus? |
|---|---|---|
| **nemu64-test** `timing` / `cycle` / `cop0hazard` features | ~1604 timing cases: NOPs, MULT/DIV interlock, per-instruction CPU and COP1 timing, register dependencies, exceptions, cached load/store, write buffer, D-cache size. **D-cache miss and uncached load with VI on/off and same/other bank** (`src/tests/timing/{mod,cache}.rs`) [verified here: source]. 4 RSP-timing tests (`src/tests/rsp_timing/mod.rs`). | Only VI-vs-CPU bank contention. `experimental_rdp` is functional, "not stable on hardware yet" [cited via sub-research]. Values look hardware-measured (medians/averages, "re-measured … ~36.3"), but the README doesn't say so [inference]. |
| **rasky/n64-systembench** | Hardware-expected cycle counts for: cached and uncached RDRAM reads (8–64-bit, seq/random/multi-bank), RCP I/O read, PI DMA (4 sizes), PI I/O, SI DMA/IO, 12 joybus cases, an RSP loop (`src/main.c:571-613`) [verified here: source]. gopher64 cites it [verified here]. | Bus latency, but no contention or RDP. |
| **PeterLemon/krom N64** | CPU/CP1 `TIMINGNTSC` ROMs timed against the VI; hardware reliability questioned (PeterLemon/N64 #17) [cited via sub-research]. | No RDP timing ROM found. |
| **libdragon** | rdpq tests check pixels only [verified here: no DPC/timing refs in tests]. | none |
| **Angrylion rrhotel13-2** (ares #967) | RDP internal per-cycle *state* (combiner feedback), not duration [cited]. | none |

**Gap:** no public ROM or dataset gives hardware-measured RDP durations or RDRAM contention under RDP load [cited: none found by sub-research; absence ≠ proof].

### 2d. Published hardware RDP throughput

- Nintendo SDK programming manual, ch. 12 (https://ultra64.ca/files/documentation/online-manuals/man-v5-1/pro-man/pro12/12-01.htm) [cited]. It gives vendor peak figures:
  - fill mode "64 bits per clock"
  - copy mode "64 bits or 4 pixels per clock"
  - 1-cycle mode "often stalled at MI, waiting for the framebuffer when accessing both color and z"
  - "In small triangles … the pipeline is often stalled, pending memory access"

  These are peak figures, not measurements.
- F3DEX3 docs: RSP cycle counts are hand-counted. RDP is mentioned only qualitatively [cited].
- No measured fill-rate tables from rasky, HailToDodongo, Kaze or Snacchus were found [cited: searched, not found].

**Implication [inference]:** the per-mode compute rates are well known. What is unknown is the memory-stall component, i.e. how much framebuffer/Z/texture traffic and contention slow each span. That is exactly what dominates MM's RDP time: 1-cycle/2-cycle with Z, AA and transparency.

## 3. Validation without owning hardware

1. **CPU and bus (good coverage):**
   - nemu64-test `timing` (+ `cycle`, `cop0hazard`), currently 1593/1604 failing on ares.
   - n64-systembench (~40 cases).

   Target: ≥ 95 % pass on timing and all systembench within its tolerance. Both run headless with IS-Viewer output on ares (companion doc).
2. **RDP compute rates (theory only):** synthetic command lists (fill rect, copy rect, 1/2-cycle textured/Z triangles, LoadBlock/LoadTile) checked against:
   - SDK peak rates
   - n64brew sync costs
   - MiSTer per-clock rules (optionally NVC-simulated for a few µs of work)
3. **Game-level symptoms with hardware video (the closest thing to a corpus):**
   - **ares #2320**: the MM/OoT file-select menu runs slower on NTSC hardware than on ares. It covers MM USA/JP/EU, has hardware YouTube captures and a 2026-07-28 hardware capture, and MiSTer #27 shows the same [verified here: issue text and comments via `gh`]. A maintainer comment says hardware shows "a ton of slowdown" vs 60 draws/s on ares. **This is an MM, RDP-bound, hardware-referenced test.** Measure distinct frames per second from the videos and compare with the model's output. Frame-counting from the video is [unverified feasibility].
   - Other cases: ares #451 (Star Fox 64 slowdown), #2367 (StarCraft 64 menus), #1652 (DK64 cutscene), gopher64 #562 (Knife Edge) [cited via sub-research].
4. **Commercial-game lag corpus:**
   - No console-verified MM TAS was found.
   - No published MM per-frame lag data was found (speedrun.com/ZSR searched by sub-research).
   - TASVideos console-verification threads say N64 emulator lag doesn't match console, and "getting any major improvements to the timings is probably many years into the future" (JosJuice 2023, https://tasvideos.org/Forum/Topics/22449) [cited].

   **No usable corpus exists [cited: not found].**
5. **Borrowed hardware (strongly recommended, cheap):**
   - Write a calibration ROM that brackets synthetic RDP lists and the MM benchmark scenes with `DPC_CLOCK/CMD_BUSY/PIPE_BUSY/TMEM_BUSY` and prints over IS-Viewer (perf-tooling doc).
   - Ask an n64brew or ares-issue contributor who has a flashcart to run it. The #2320 reporter posts hardware captures.
   - An N64 + SummerCart64 costs roughly a few hundred USD [estimate].

   Without step 5, RDP numbers stay "plausible", not "verified".

## 4. Engineering estimate [all estimate]

### Design

- **A. RDP timing model** (`ares/n64/rdp/timing.{hpp,cpp}`, ~2–3.5 k LOC):
  - Reuse ares's existing command parser (`rdp/render.cpp` already decodes every field).
  - Track state: other modes, tiles, images, scissor.
  - Walk edges per scanline. Port the Angrylion or paraLLEl CPU-reference edge walker, about 400 lines.
  - For each span:
    - cycles = setup + span length × mode rate
    - stalls from framebuffer/Z traffic: bytes per pixel × (read if Z-compare or blend/AA, write), coalesced into RDRAM transactions and submitted to the bus model
  - TMEM loads: bytes / 8 per clock + RDRAM fetch.
  - Syncs per n64brew.
  - A small command FIFO and the DP command-fetch DMA.
  - Pixels still come from paraLLEl.
- **B. Time-stepped RDP device** (~0.5–1 k LOC):
  - `flushCommands()` enqueues commands. `RDP::main()` retires them against modeled time, advancing `DPC_CURRENT` and the counters, and raises the DP IRQ on `SYNC_FULL` at the modeled time.
  - paraLLEl can still render eagerly. Functional output arrives early, which is only visible if the CPU reads the framebuffer mid-frame [inference: rare in MM, needs checking].
  - FIFO back-pressure then emerges naturally: `_fifo` ucode polls `DPC_CURRENT`.
  - Fix the `TMEM` read bug.
- **C. RDRAM bus model** (`rdram/bus-timing`, ~1–1.5 k LOC):
  - One timeline with per-bank open-row state, a refresh schedule tied to VI HSYNC, and transaction costs (RAS/CAS-ish, from n64brew delay fields).
  - Assumed fixed requester priority (to calibrate).
  - The CPU (I/D miss, uncached, write buffer), SP DMA, DP, VI, AI, PI and SI submit transactions.
  - Two fidelity tiers:
    - (i) *windowed*: the RCP devices book bandwidth per window; CPU accesses pay base + expected wait given window occupancy. This keeps the JIT slice.
    - (ii) *lockstep*: shrink JIT interleaving so ordering is exact. Slower.
- **D. CPU cost fixes** (~0.5–1.5 k LOC):
  - uncached and MMIO costs, write buffer, interlocks driven by nemu64-test
  - the recompiler must emit the same costs as the interpreter
- **E. Harness** (~1 k LOC + ROMs): CI runner for nemu64-test timing and systembench, RDP microbenchmark ROM, the #2320 menu check.

**Total: ~5–9 k LOC.**

### Effort

- **v0, ~6–8 person-weeks:** A + B, plus the simplest C(i) and the uncached/SP-DMA fixes. This gives lag frames that respond to RDP load.
- **v1, ~3–5 person-months:** full C, the D pass to a high nemu64-test timing pass rate, and calibration.

These figures are for someone already fluent in ares and N64 internals. Add 30–50 % otherwise.

### Runtime cost

- **Baseline:** ares recompiler runs MM at ~255 VI/s (≈ 4.25× real time) [verified, companion doc].
- **RDP model cost:**
  - MM is 320×240. Assume a few thousand triangles and ~2–4× overdraw, i.e. ~10^5 pixels in ~10^4 spans per frame.
  - At span granularity that is roughly ≤ 1 ms of host time per frame, so ≤ 10–20 % slowdown.
- **Contention cost:**
  - Windowed C(i): small, ≤ 10–20 %.
  - Lockstep C(ii) with ~100–500-clock slices: plausibly 2–4× slower, i.e. about real time.
- **Benchmark example:** 600 game frames at 20 fps = 1800 VIs.
  - Fast tier: ~10–20 s.
  - Lockstep: ~30–90 s.

Minutes per run is realistic. The interpreter (45 VI/s) would also fit, at ~40 s per 1800 VIs.

### Plausible accuracy (error bars)

| Metric | v0 | v1, uncalibrated | v1 + one hardware calibration set |
|---|---|---|---|
| CPU time per frame | ±10–15 % | ±5–10 % | ±3–5 % |
| RSP gfx time (incl. FIFO stalls) | ±20–30 % | ±10–20 % | ±5–10 % |
| RDP busy per frame | ±30–50 % | ±15–30 % | ±10 % |
| Lag-frame count | only frames > ~30 % from the budget are trustworthy | frames > ~15–20 % from the budget | frames > ~10 % from the budget |

Use: relative A/B deltas will be more trustworthy than absolutes in every column.

### Main risks

1. **No RDP and contention ground truth.** The model can be self-consistent and still wrong by tens of percent on Z/AA-heavy spans.
2. **Unknown arbitration rules and refresh interaction.** They are not documented, and the MiSTer core doesn't model them.
3. **Scheduler granularity vs speed.**
4. **Recompiler/interpreter cost parity.**
5. **Determinism:** paraLLEl runs on a GPU thread, but the timing model must be pure CPU-side and deterministic.
6. **The game's feedback loop.** `func_80173B48` scales particles by measured RDP time (perf-tooling doc), so the model changes game behavior. That's correct, but benchmarks must still pin it.
7. **Early-visible framebuffer** if eager rendering is kept (B).
8. **Upstream divergence** if the work is kept as a fork.

## 5. Upstream reception

- **ares:**
  - Open **#2320** (2025-12-19) is about MM/OoT menus being faster than hardware. A maintainer-side comment (flagrama, 2026-07-28) frames it as an RDP-timing issue to "keep in mind when RDP timing issues start being resolved" [verified here via `gh`].
  - In #967 (2023), rasky distinguishes emulating *per-command duration* (desirable, vs "an infinitely fast RDP like we do now") from per-cycle internal RDP emulation, which he called out of scope [cited via sub-research; the quote's exact wording was not re-read by me].
  - rasky (Giovanni Bajo) authored ares's I/O-timing refactor, PI DMA timing, the emux spec and RDRAM chip emulation (`ad16c4a67`, `7f9346e5e`, `bea395b24`, `3cac8a186`) [verified here: git log]. He is the natural reviewer.
  - A timing PR was reverted before (`807ef0300`), so regressions in game compatibility get pushed back [inference].
  - No CONTRIBUTING file or AI policy is in the repo [cited via sub-research].
  - **Likely reception [inference]:** positive for small, verifiable pieces such as nemu64-test-driven CPU fixes, the `TMEM` read bug, systembench-driven uncached/MMIO costs and the SP DMA rate. Cautious for a large RDP/contention model unless it is toggleable, cheap and fixes known issues (#2320, #451, #2367).
  - **Plan on a fork,** and upstream piecewise.
- **gopher64:** forbids "vibe coded and/or co-authored by AI agents" PRs and asks contributors to contact the author before substantial work [cited, README]. Not a target for this work.
- **simple64:** archived. **mupen64plus:** maintainers acknowledge the timing hack (#953) but have no cache model. **Project64:** discussed an optional RSP/RDP timing mode in 2020 (#1268) [cited via sub-research].

## 6. Open questions

1. Can the MM file-menu slowdown (ares #2320) be quantified from the hardware videos (distinct frames per second), and does the slowdown come from the RDP, the RSP FIFO or the CPU?
2. What are the real RDRAM arbitration priorities and refresh stall behavior under RDP load? Answering this needs hardware experiments.
3. How much of MM's frame time is uncached RDRAM / MMIO access, given that ares currently charges 0 for uncached RDRAM?
4. Why was rasky's PI DMA timing commit reverted?
5. Does MM ever read framebuffer/Z data mid-frame (would eager paraLLEl rendering leak future pixels)?
6. Can a borrowed hardware run be arranged? It is the single biggest accuracy lever.
7. Do nemu64-test's 1593 timing failures on ares collapse to a few systematic causes (e.g. a COUNT read offset)? Not analyzed.

## Sources

Local source (scratchpad clones / `~/src/ares`):

- ares `a776c50`: `ares/n64/{accuracy.hpp, cpu/{cpu.hpp,cpu.cpp,dcache.cpp,memory.cpp,interpreter-ipu.cpp}, memory/{io.hpp,bus.hpp}, mi/bus.hpp, rdram/{rdram.hpp,rdram.cpp}, rsp/{dma.cpp,rsp.cpp,rsp.hpp,interpreter-scc.cpp}, rdp/{io.cpp,render.cpp,rdp.cpp}, vulkan/vulkan.cpp, pi/{dma.cpp,bus.hpp}, si/io.cpp, vi/vi.cpp}`. Commits `ad16c4a67`, `7f9346e5e`, `bea395b24`, `807ef0300`, `5f9804fb6`, `a10b9ce0e`, `3cac8a186`, `6fcaa213f`.
- gopher64 `1ab3793`: `src/device/{rdram.rs,cache.rs,rdp.rs}`, `parallel-rdp/interface.cpp`.
- simple64: `parallel-rdp-standalone/parallel_imp.cpp:428-430`, `mupen64plus-core/src/device/rcp/rdp/rdp_core.c:138-155`.
- cen64 `e0641c8`: `vr4300/fault.h:15-16`, `vr4300/cp1.c`.
- MiSTer N64_MiSTer `5725381`: `rtl/{RDP.vhd,RDP_command.vhd,RDP_raster.vhd,RDP_pipeline.vhd,DDR3Mux.vhd,memorymux.vhd,RSP.vhd,cpu.vhd,cpu_datacache.vhd}`, `rtl/pll/pll_0002.v`, `README.md`.
- nemu64-test `9a8b9f7`: `Cargo.toml`, `src/tests/timing/{mod.rs,cache.rs}`, `src/tests/rsp_timing/mod.rs`.
- rasky/n64-systembench `845635c`: `src/main.c`. https://github.com/rasky/n64-systembench
- MM decomp: `include/sys_cfb.h:25,30`, `src/code/graph.c:202-203`, `src/code/sys_ucode.c:10`.

Web (primary):

- n64brew: https://n64brew.dev/wiki/Reality_Display_Processor/Pipeline, …/Reality_Display_Processor/Interface, …/RDRAM, …/RDRAM_Interface, …/Reality_Signal_Processor/Interface, …/Peripheral_Interface
- Nintendo SDK manual ch. 12: https://ultra64.ca/files/documentation/online-manuals/man-v5-1/pro-man/pro12/12-01.htm
- ares issues: https://github.com/ares-emulator/ares/issues/2320, /967, /451, /2367, /1652; PR /2584
- MiSTer N64 issues #27, #30, #31, #13: https://github.com/MiSTer-devel/N64_MiSTer/issues ; forum https://misterfpga.org/viewtopic.php?t=7198
- gopher64 README: https://github.com/gopher64/gopher64 ; issue #562
- mupen64plus-core #953; Project64 #1268
- parallel-rdp README: https://github.com/Themaister/parallel-rdp-standalone
- PeterLemon N64 #17: https://github.com/PeterLemon/N64/issues/17
- TASVideos: https://tasvideos.org/Forum/Topics/22449, https://tasvideos.org/Forum/Posts/539632, https://tasvideos.org/Forum/Posts/456100
- F3DEX3 performance: https://hackern64.github.io/F3DEX3/performance.html

"[cited via sub-research]" items were gathered by a delegated web-research pass. Quotes were checked by me only where marked [verified here].
