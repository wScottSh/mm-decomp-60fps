# N64 emulator for accurate, headless, automated testing of the MM decomp

Research date: 2026-10-04. Host: unicron (Ubuntu 24.04, 32 cores, RADV GFX1151, no X display).

Legend: **[verified]** = checked on this machine or read in source at the cited commit/tag. **[cited]** = stated by a primary source (link), not re-checked here. **[unverified]** = inference or not checked.

## TL;DR

**Use ares (v148, upstream `ares-emulator/ares`), built from source in Docker, run under Xvfb with paraLLEl-RDP on the RADV GPU.**

Why:

1. ares has the most complete low-level model of the commonly-usable emulators: LLE RSP only (interpreter + recompiler), VR4300 I/D-cache emulation, TLB, and a compile-time "Reference" accuracy mode [verified, source]. Its own release notes say it passed nearly all n64-systemtest tests in 2022 (563 tests, 3 failures) [cited]. On the current successor suite (nemu64-test, 4637 base tests) it fails 445 (recompiler) / 423 (interpreter) — mostly COP1 exception edge cases [verified, ran it here].
2. It boots and runs the built `mm-n64-us.z64` headless in Docker + Xvfb at **~255 VI/s uncapped (≈4.25× real time) with the recompiler** and ~45 VI/s with the interpreter [verified, ares status bar].
3. Usable automation hooks exist: `--setting key=value` for every config value, `--save-state <slot>` on boot, IS-Viewer output goes to stdout (the test ROM's results were read from the log), and a GDB remote stub that reads memory [verified].

Main gaps: no input-movie / scripted-input facility in standalone ares [verified by source grep]; no real headless mode (GTK UI needs an X server → Xvfb) [verified]; ares fails almost all nemu64-test *timing* tests (1593/1604), so cycle-level CPU timing is **not** hardware-exact [verified]. For 60fps CPU/RCP budget work, ares is the best available emulator, but final lag-frame numbers still need real hardware (e.g. flashcart) confirmation.

If you need scripted input + memory access in one package: **BizHawk's "Ares64" core** (ares v138 + Angrylion software RDP, with .bk2 movies, Lua, sockets) is the secondary pick [cited, source] — not tested here.

## Comparison table

| | ares v148 | gopher64 v1.1.38 | mupen64plus 2.6.0 (+plugins) | simple64 | BizHawk 2.11.1 Ares64 | dgb-n64 | cen64 | RustyN64 |
|---|---|---|---|---|---|---|---|---|
| RSP | LLE only | LLE | HLE default; LLE via cxd4 / parallel-rsp | LLE (parallel-rsp) | LLE (ares v138) | LLE | LLE | LLE |
| RDP | paraLLEl-RDP (Vulkan) only; software RDP is stubbed | paraLLEl-RDP | any plugin incl. angrylion-plus (SW), parallel (RMG) | paraLLEl-RDP | **Angrylion** (software) | paraLLEl-RDP; SW "UNFINISHED" | Angrylion-derived SW | own |
| CPU caches | I+D cache emulated | I+D cache w/ cycle costs | none upstream (CountPerOp=2 approx) | I+D cache (fork of m64p core) | as ares v138 | ? | yes (RTL-level goal) | ? |
| nemu64-test base (here) | **445/4637 fail** (recomp), 423 (interp) | **panics** (cop0.rs index OOB) | not run | not run | not run | not run | not run | claims 0 fail CPU/RSP |
| Runs MM | yes (verified) | not tested | yes (widely known; unverified here) | n/a | not tested | not tested | not tested | **no** (commercial titles reach no frame) |
| Headless | Xvfb required | Xvfb required, needs audio device (`SDL_AUDIODRIVER=dummy`) | console UI; video plugin still needs a GL window [unverified] | GUI | Mono WinForms → Xvfb | Vulkan window | `-headless`, `-novideo` flags | test harness |
| Input scripting | none | none | via custom input plugin / frontend API | none | .bk2 movies, Lua, socket, `--movie` | `.m64` movies `-m` | `-controller` config only | harness |
| Mem access / debug | GDB stub (verified mem read) | none | debugger API (`DebugMemRead32`), `--debug` | ? | Lua memory API | GDB stub + HTTP API (`/read/:size/:addr`) | `-debug` | harness |
| Screenshots | via X (import/ffmpeg) | via X | `--testshots N,M` then quit | — | `--dump-type`, Lua | — | — | frame comparators |
| Last release | v148, 2026-05-30 | v1.1.38, 2026-10-03 | 2.6.0, 2024-07-15 (master active 2026-10) | archived 2025-02 | 2.11.1, 2026-05-01 | nightlies only | v0.3, 2017 | v0.7.0 |
| License | ISC | GPL-3.0 | GPL-2.0 (core) [unverified] | GPL-3.0 | MIT frontend; core per-upstream [unverified] | none file found | BSD-3 | MIT/Apache-2.0 |

## Per-emulator notes

### ares (recommended)
- Multi-system emulator descended from higan/bsnes, "focuses on accuracy and preservation" and trades speed for clarity [cited: README https://github.com/ares-emulator/ares/blob/master/README.md].
- `ares/n64/accuracy.hpp` (v148): `Reference` flag enables all accuracy options; CPU/RSP default to recompiler where supported, `AddressErrors` on, IPL2 checksum emulated [verified, https://github.com/ares-emulator/ares/blob/master/ares/n64/accuracy.hpp].
- CPU has `icache`/`dcache`, TLB, interpreter + recompiler (`ares/n64/cpu/`) [verified, source tree].
- **RDP: GPU only.** `ares/n64/rdp/render.cpp` parses commands but `fillRectangle`, `textureRectangle`, etc. are empty bodies; rendering only happens when `vulkan.enable` → paraLLEl-RDP [verified, https://github.com/ares-emulator/ares/blob/master/ares/n64/rdp/render.cpp]. So ares needs a working Vulkan device; RADV works here [verified: log "Vulkan Enabled: using paraLLEl-RDP"].
- paraLLEl-RDP aims to be bit-exact with Angrylion-Plus; its conformance suite (~150 fuzz tests) requires bit-exact output vs Angrylion; RADV ACO "passes conformance with bleeding edge drivers and `PARALLEL_RDP_SMALL_TYPES=0`"; it documents intentional deviations (e.g. first-cycle combined color cleared, 2-cycle alpha-test reads) [cited, https://github.com/Themaister/parallel-rdp-standalone/blob/master/README.md (vendored copy read in dgb-n64 tree)].
- Desktop UI is GTK (hiro); `--setting`, `--save-state`, `--system`, `--no-file-prompt`, `--dump-all-settings` exist [verified, `desktop-ui/desktop-ui.cpp`]. Setting names in v148: `DebugServer/Enabled|Port|UseIPv4`, `Boot/AwaitGDBClient`, `General/ForceInterpreter`, `Audio/Driver`, `Input/Driver`, `Video/Driver` [verified, `--dump-all-settings`].
- GDB server: `nall/nall/gdb` — designed so halting/reading "should not affect the game … down to the cycle" [cited, https://github.com/ares-emulator/ares/blob/master/nall/nall/gdb/Readme.md]. Memory read via `gdb-multiarch` worked here; reported PC was `0xffffffff` with `set architecture mips:4300` (register mapping not investigated) [verified / open].
- No TAS/movie or scripted-input feature found (`grep -ri movie desktop-ui` → only a Famicom tape tool) [verified].
- v148 changelog includes CPU/RSP JIT perf, AI DMA pop fix, GDB read fix for RCP/PI path, EEPROM write timing [cited, https://github.com/ares-emulator/ares/releases].
- Ubuntu 24.04 apt has `ares 134+dfsg` (old) [verified, `apt-cache policy`]; build from source is the way.

### gopher64
- Successor to simple64 by the same author [cited, simple64 README https://github.com/simple64/simple64]. Rust, GPL-3.0, "portions adapted from mupen64plus and/or ares" [cited, https://github.com/gopher64/gopher64].
- LLE RSP (`rsp_cpu.rs`, `rsp_*_instructions.rs`), paraLLEl-RDP, I/D cache with cycle costs (`cache.rs`) [verified, source].
- CLI has no headless, screenshot, scripting, or debugger options (full `--help` checked) [verified]. Panics without an audio device; with `SDL_AUDIODRIVER=dummy` it ran but **panicked on nemu64-test**: `src/device/cop0.rs: index out of bounds: the len is 32 but the index is 44` [verified, v1.1.38 Linux binary].
- Very active (release 2026-10-03). Contribution policy forbids AI-agent-authored PRs [cited, README].

### mupen64plus (core 2.6.0 + ui-console)
- Default plugins: video-rice, audio-sdl, **rsp-hle** [verified, `ui-console/src/main.c`]. Accuracy depends entirely on plugin choice: LLE RSP (cxd4/parallel-rsp) + angrylion-rdp-plus (MAME license, supports mupen64plus) [cited, https://github.com/ata4/angrylion-rdp-plus].
- Core timing is approximate: `DEFAULT_COUNT_PER_OP = 2`, configurable `CountPerOp` [verified, `src/main/rom.c`]; no VR4300 cache emulation in upstream core (only host-cache code in dynarec) [verified, grep].
- Automation is the strongest of the classic emulators: `--testshots <frames>` (screenshot then quit), `--savestate`, `--nospeedlimit`, `--debug` console debugger, `--core-compare-send/recv` [verified, `ui-console/src/main.c`]; frontend API has `M64CMD_ADVANCE_FRAME`, `M64CMD_SET_FRAME_CALLBACK`, `M64CMD_TAKE_NEXT_SCREENSHOT`, `M64CMD_STATE_SAVE`; debugger API `DebugMemRead32/Write32` [verified, `src/api/`].
- Last tagged release 2.6.0 (2024-07-15); master active (2026-10-02) [verified, GitHub API]. Headless video with angrylion still needs a GL context (Xvfb) [unverified].
- RMG (v0.9.0, 2026-06-14) is the maintained mupen64plus-based GUI frontend [cited, simple64 README; verified release date].

### simple64
- Archived; author moved to gopher64 [cited, https://github.com/simple64/simple64]. Its m64p core fork added I/D cache emulation [verified, `mupen64plus-core/src/device/r4300/icache.c`]. Not recommended (unmaintained).

### BizHawk Ares64 core
- BizHawk lists N64 cores "Ares64, Mupen64Plus"; Linux build exists (Mono), Mupen64Plus is the core *not* available on Linux [cited, https://github.com/TASEmulators/BizHawk README].
- Ares64 = `PortedCore(... "v138" ...)` [verified, `Ares64.cs`] with **Angrylion** RDP linked in (`thirdparty/angrylion-rdp`, `angrylion::OutFrameBuffer`) [verified, `waterbox/ares64/`]. PIF ROM is bundled as a resource [verified, `Resources.PIF_NTSC_ROM`]. Interpreter/recompiler selectable [verified].
- CLI: `--movie`, `--lua`, `--load-state`, `--dump-type/--dump-length/--dump-close`, `--socket-ip/port`, `--mmf`, `--chromeless` [verified, `src/BizHawk.Client.Common/ArgParser.cs`].
- Trade-off: older ares (v138 vs v148), Mono/WinForms needs Xvfb, slower software RDP. Not run here.

### dgb-n64 (Dillonb/n64)
- "Experimental … not ready for prime time. Compatibility is not high" [cited, https://github.com/Dillonb/n64]. Needs Vulkan; software RDP flag says "UNFINISHED!" [verified, `cli_frontend.c`].
- Interesting automation: `-m` Mupen64 `.m64` movies, `--exit-on-movie-end`, `--unlock-framerate`, GDB stub (`-d`), HTTP API (`/read/:size/:address`, `/registers`, `/control/step`) [verified, source]. Not tested with MM.

### cen64
- Aims for "perfect emulation … down to the register-transfer level" [cited, https://github.com/n64dev/cen64]. Has `-headless`, `-novideo`, `-noaudio`, `-debug`, `-is-viewer` [verified, `device/options.c`]. Requires a PIF IPL ROM dump as first arg [verified, usage string]. Last release v0.3 (2017); low activity [verified, GitHub API]. Known to be slow and low-compat [unverified].

### RustyN64 (new, 2026)
- Claims n64-systemtest CPU/COP0/TLB/COP1 and RSP with 0 failures, but "a commercial title boots … but does not yet reach a rendered frame" [cited, https://github.com/doublegate/RustyN64]. Not usable for MM.

## Accuracy test-suite results

- **n64-systemtest** is deprecated; successor is **nemu64-test** (MIT) [cited, https://github.com/lemmy-64/n64-systemtest, https://github.com/thelemmy/nemu64-test]. Neither publishes a cross-emulator results table [cited].
- ares v128 (2022-05-17) on n64-systemtest: "Tests: 563. Failed: 3" and claims "most accurate Nintendo 64 emulator at the time of publication" [cited, https://ares-emu.net/news/ares-v128-released].
- **Run here** — nemu64-test @ `9a8b9f7` (built with its pinned nightly in `rust:1-bookworm` Docker), on ares v148 (Docker, RADV):

| Build / mode | Base | Timing | Cycle | CP0-hazards |
|---|---|---|---|---|
| ares recompiler | 445 / 4637 fail | 1593 / 1604 fail | 5 / 13 fail | 5 / 5 fail |
| ares interpreter | 423 / 4637 fail | not run | not run | not run |
| gopher64 v1.1.38 | panic (no result) | — | — | — |

  ares base failures (recompiler): 207 "Reserved COP1", 179 "COP1 unusable", 24 icache (mostly self-modifying code; the interpreter fixes these), 14 cache/data-cache tag tests, plus a handful of COP0 (Random/Context/XContext masking), rdram-regs, RDP STATUS. Timing failures sampled are often off by one cycle (e.g. `MULT; MFLO` actual 7 vs expected 6) — could be a systematic COUNT-measurement offset; not analyzed.
- paraLLEl-RDP: bit-exact vs Angrylion on its own conformance suite, with documented exceptions [cited, above].
- krom / Peter Lemon / rasky test ROM results per emulator: **no primary-source results table found** [unverified/not found].
- No FPGA-free option found that passes nemu64-test timing/cycle sets for commercial-game use.

## Headless setup on unicron (recommended)

All of this ran in Docker; nothing installed on the host.

```dockerfile
# Dockerfile (ares v148, N64 core only)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq git cmake ninja-build g++ pkg-config \
    libgtk-3-dev libsdl2-dev libao-dev libopenal-dev libpulse-dev libasound2-dev libudev-dev libxrandr-dev \
    libgl-dev libvulkan-dev mesa-vulkan-drivers libgl1-mesa-dri xvfb x11-apps imagemagick xdotool vulkan-tools ca-certificates
RUN git clone -q --depth 1 --branch v148 https://github.com/ares-emulator/ares.git /src/ares
WORKDIR /src/ares
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARES_SKIP_DEPS=ON -DARES_CORES="n64" -DENABLE_CCACHE=OFF
RUN cmake --build build -j32     # binary: build/rundir/bin/ares
```

```bash
# run_ares.sh <rom> <seconds> <out.png> [extra ares args]
ROM=$1; SECS=$2; OUT=$3; shift 3
Xvfb :1 -screen 0 1280x1024x24 & sleep 1
export DISPLAY=:1 HOME=/tmp/home; mkdir -p $HOME
/src/ares/build/rundir/bin/ares --system "Nintendo 64" --no-file-prompt \
  --setting Audio/Driver=None --setting Input/Driver=None --setting Video/Driver="OpenGL 3.2" \
  --setting Boot/Fast=true "$@" "$ROM" > /out/ares.log 2>&1 &
PID=$!; sleep $SECS; import -window root "$OUT"; kill $PID
```

```bash
docker build -t ares-n64:v148 .
docker run --rm --device /dev/dri \
  --group-add $(getent group render | cut -d: -f3) --group-add $(getent group video | cut -d: -f3) \
  -v $PWD:/out -v $PWD/roms:/roms ares-n64:v148 /out/run_ares.sh /roms/mm-n64-us.z64 45 /out/mm.png
```

Verified:
- Build succeeds (~minutes on 32 cores). The ROM directory must be **writable** or ares shows a modal "save path is read-only" dialog and doesn't start.
- MM US (md5 `f46493ea…`) reaches the attract-mode cutscene; status bar 255 VPS (recompiler), 45 VPS (`--setting General/ForceInterpreter=true`). With `Audio/Driver=None` emulation is uncapped.
- IS-Viewer text (e.g. nemu64-test results) appears in ares stdout.
- GDB: add `--setting DebugServer/Enabled=true --setting DebugServer/Port=9123 --setting DebugServer/UseIPv4=true`, then `gdb-multiarch -ex "set architecture mips:4300" -ex "set endian big" -ex "target remote :9123"`; `x/4wx 0x80000000` returned RAM. (Run without `set endian big` the words printed byte-swapped.)

Unverified / to do:
- Driving controller input: candidates are `Input/Driver=SDL` or `Xlib` + `xdotool` key events into Xvfb, or GDB memory writes into the game's input buffer. Neither tried.
- Frame-exact screenshots: `import` grabs whatever is on the X screen; no frame-advance hook. `--save-state <slot>` loads at boot but slot files must be created first.
- Whether lavapipe (llvmpipe Vulkan) works for paraLLEl-RDP if the iGPU is unavailable.
- Determinism run-to-run (recompiler + threaded GPU).

## Caveats / open questions

- **Timing ≠ hardware.** For 60fps lag-frame and CPU/RCP budget measurements, even ares fails nearly all nemu64-test timing tests. Use ares to find relative regressions, and confirm absolute budgets on real hardware.
- ares's RDP is paraLLEl-RDP only (bit-exact *to Angrylion*, with documented deviations). Angrylion software rendering needs BizHawk Ares64 (older ares) or mupen64plus + angrylion-plus (less accurate CPU/RSP).
- No primary-source cross-emulator test-suite tables exist; the numbers above are from one run on this host.
- mupen64plus, simple64, BizHawk, dgb-n64, cen64 were evaluated from source/docs only, not run.

## Sources

- ares repo, README, LICENSE (ISC), source: https://github.com/ares-emulator/ares (tag v148)
- ares releases: https://github.com/ares-emulator/ares/releases
- ares v128 notes: https://ares-emu.net/news/ares-v128-released
- ares GDB server: https://github.com/ares-emulator/ares/blob/master/nall/nall/gdb/Readme.md
- paraLLEl-RDP: https://github.com/Themaister/parallel-rdp-standalone
- angrylion-rdp-plus: https://github.com/ata4/angrylion-rdp-plus
- gopher64: https://github.com/gopher64/gopher64 (release v1.1.38)
- simple64: https://github.com/simple64/simple64
- mupen64plus-core: https://github.com/mupen64plus/mupen64plus-core ; ui-console: https://github.com/mupen64plus/mupen64plus-ui-console
- RMG: https://github.com/Rosalie241/RMG
- BizHawk: https://github.com/TASEmulators/BizHawk (README, `waterbox/ares64/`, `Ares64.cs`, `ArgParser.cs`)
- dgb-n64: https://github.com/Dillonb/n64
- cen64: https://github.com/n64dev/cen64
- RustyN64: https://github.com/doublegate/RustyN64
- n64-systemtest: https://github.com/lemmy-64/n64-systemtest ; nemu64-test: https://github.com/thelemmy/nemu64-test
