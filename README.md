# Majora's Mask at a locked 60 fps

This fork of the [zeldaret/mm](https://github.com/zeldaret/mm) decompilation changes the game code so that Majora's Mask runs at a constant 60 fps on a retail N64 with the Expansion Pak. The frame rate never drops. Nothing else changes for the player: same resolution, textures, effects, audio, game logic, and real-world game speed.

## Rules

- **Pixel-exact.** Every image the game shows at a logic tick must match retail pixel for pixel under the same input. The in-between images must match a recorded golden set. Input may never appear on screen later than in retail. RDP timing is the one exception: the console's noise generator never repeats, and where primitives overlap the RDP can read stale memory depending on bus contention. Fidelity runs neutralize both identically in both builds (the noise pin) and still check that the game emits the same noise and pipeline-mode commands.
- **Same game speed.** Game logic keeps its retail tick, 20 Hz in gameplay. The extra images come from interpolating between the previous and current tick.
- **Real hardware.** NTSC N64 with the Expansion Pak, the retail flash save, and a 32 MiB ROM. Each optimization must name a hardware mechanism with a reference. No change may rely on a quirk of the emulator.
- **Measured, not guessed.** Each change is measured before and after on a pinned, deterministic build of the [ares timing-model fork](https://github.com/wScottSh/ares) (`tools/fps60/`). It is kept only if it helps and no scene gets worse. The target is at most 90% of a 16.7 ms field for each of the CPU, RSP and RDP, which leaves margin because the emulator is not yet calibrated against a console.

## Status

**Phase: building the harness.** No retail game code has changed yet. The only source changes are benchmark hooks behind `BENCH=1`.

- Research is done: [frame pipeline and interpolation hazards](docs/research/mm-frame-pipeline.md) and [60 fps techniques](docs/research/fps60-techniques.md).
- The architecture is chosen: [docs/fps60/architecture.md](docs/fps60/architecture.md). Game logic and drawing run once per tick, unchanged. The RCP then renders that tick's display list once per field: the in-between images use matrices blended between the previous and current tick, and the last image uses retail's exact bytes. It needs two more framebuffers, about 316 KiB in total (inferred).
- Known exceptions, matching retail: the ticks where retail itself freezes the picture (opening pause, picto photos, tile captures) freeze the same way, and so does the pause screen while its background is filtered. The Bombers' Notebook stays at 20 images per second, because interpolating it would need about 600 KB (inferred).
- The harness (`tools/fps60/`) runs six scenes deterministically and checks fidelity. It also measures load: CPU busy per field and per tick (per thread, engine system and actor), RSP busy split into graphics and audio, and RDP busy. Baseline: [docs/fps60/baseline.md](docs/fps60/baseline.md).
- The noise pin is in: fidelity runs neutralize RDP noise and stale span-buffer reads identically in both builds, and hash every noise-related and pipeline-mode command as the game emitted it, so a build that changes those commands still fails. With the pin on, a build that only adds dead code passes F1 in all six scenes. A one-pixel change fails at exactly its tick, and so does one added, dropped or changed noise or pipeline-mode command (measured).
- Retail today shows one image every 3 fields (20 fps) in every gameplay scene tested. In each gameplay scene its busiest resource needs 1.2 to 2.2 fields per tick at the median, measured. File select lags on some of its frames.
- Next: measure the per-scene budget gap, which decides where optimization starts.
- Main risk: interpolation does not reduce render work, so every field needs a full render. The architecture estimates (inferred, from medians measured on an older fork build) that each image's RCP time must drop 1.7 to 2.3 times. The research estimates (inferred) that CPU time per tick must drop 2 to 4 times. RDP busy time in retail is not measured yet, and it decides whether 60 fps is reachable.

## Measure

```bash
tools/fps60/worktree-setup.sh <unit> <branch>     # prepare a worktree for building
make -j32 BENCH=1                                  # build/n64-us-bench/mm-n64-us.z64
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS
tools/fps60/fidelity.py BASE_RESULTS RESULTS       # F1: keyframes against the reference
```

See [tools/fps60/README.md](tools/fps60/README.md).

---

# Legend of Zelda: Majora's Mask (US) 1.0

[![Build Status][gha-badge]][gha] [![Decompilation Progress][progress-badge]][progress] [![Contributors][contributors-badge]][contributors] [![Discord Channel][discord-badge]][discord]

[gha]: https://github.com/zeldaret/mm/actions/workflows/ci.yml?query=branch%3Amain+event%3Apush
[gha-badge]: https://img.shields.io/github/actions/workflow/status/zeldaret/mm/ci.yml

[progress]: https://zelda.deco.mp/games/mm
[progress-badge]: https://img.shields.io/endpoint?url=https://zelda.deco.mp/assets/csv/progress-mm-shield.json

[contributors]: https://github.com/zeldaret/mm/graphs/contributors
[contributors-badge]: https://img.shields.io/github/contributors/zeldaret/mm

[discord]: https://discord.zelda.deco.mp
[discord-badge]: https://img.shields.io/discord/688807550715560050?color=%237289DA&logo=discord&logoColor=%23FFFFFF

```diff
- WARNING! -

This repository is a work in progress, and while it can be used to make certain changes, it's 
still constantly evolving. If you wish to use it for modding purposes in its current state,
please be aware that the codebase could drastically change at any time. Also note that some
parts of the ROM may not be 'shiftable' yet, so modifying them could currently be difficult.
```

This is a WIP **decompilation** of ***The Legend of Zelda: Majora's Mask***. The purpose of the project is to recreate a source code base for the game from scratch, using information found inside the game along with static and/or dynamic analysis. **It is not, and will not, produce a PC port.** For frequently asked questions, you can visit [our website](https://zelda.deco.mp/games/mm), and for more information you can get in touch with the team on our [Discord server](https://discord.zelda.deco.mp).

The only version currently supported is N64 US, but we intend to eventually support every retail version of the original game (i.e. not versions of MM3D, which is a totally different game).

It currently builds the following ROM and compressed ROM:

* mm-n64-us.z64 `md5: f46493eaa0628827dbd6ad3ecd8d65d6`
* mm-n64-us-compressed.z64 `md5: 2a0a8acb61538235bc1094d297fb6556`

**This repo does not include any assets or assembly code necessary for compiling the ROM. A prior copy of the game is required to extract the required assets.**

Please refer to the following for more information:

- [Website](https://zelda.deco.mp/)
- [Discord](https://discord.zelda.deco.mp/)
- [How to Contribute](docs/CONTRIBUTING.md)

## Installation

### Windows

For Windows 10, install WSL and a distribution by following this
[Windows Subsystem for Linux Installation Guide](https://docs.microsoft.com/en-us/windows/wsl/install-win10).
We recommend using Debian or Ubuntu 20.04 Linux distributions.

### MacOS

Preparation is covered in a [separate document](docs/BUILDING_MACOS.md).

### Docker

Preparation is covered in [Building Docker](docs/BUILDING_DOCKER.md).

### Linux (Native or under WSL / VM)

#### 1. Install build dependencies

The build process has the following package requirements:

* git
* build-essential
* binutils-mips-linux-gnu
* curl
* python3
* python3-pip
* python3-venv
* libpng-dev
* libxml2-dev

Under Debian / Ubuntu (which we recommend using), you can install them with the following commands:

```bash
sudo apt update
sudo apt install git build-essential binutils-mips-linux-gnu curl python3 python3-pip python3-venv libpng-dev libxml2-dev
```

#### 2. Clone the repository

Create your own fork of the repository at `https://github.com/zeldaret/mm`. Then clone your fork where you wish to have the project, with the command:

```bash
git clone https://github.com/<YOUR_USERNAME>/mm.git
```

This will copy the GitHub repository contents into a new folder in the current directory called `mm`. Change into this directory before doing anything else:

```bash
cd mm
```

#### 3. Prepare a base ROM

Place a copy of the US ROM inside the `baseroms/n64-us/` folder.

Rename the file to `baserom.z64`, `baserom.n64` or `baserom.v64`, depending on the original extension.

#### 4. Make and Build the ROM

For a first time build, run the following command, where `N` is the number of cores your processor has (see the note at the bottom for more information):

```bash
make init -j N
```

After the very first build you'll want to run `make -j N` instead, since you likely won't need all the setup the `init` command provides.

The extraction/build process:
1. Prepares build environment:
    - Creates a Python virtual environment
    - Downloads necessary tools from pip
    - Compiles tools for the build process
2. Extracts ROM contents:
    - Decompresses the ROM
    - Extracts individual files
    - Extracts archive files
3. Extracts assets:
    - Extracts assets based on the XML files found in `assets/xml`
4. Disassembles code:
    - Disassembles code-containing files
    - Disassembles data (data, rodata, and bss)
5. Builds the ROM:
    - Compiles the code and assets into a new ROM
    - Generates a compressed version of the ROM

If all goes well, the new ROM should be built at `build/n64-us/mm-n64-us.z64`, a compressed version generated at `build/n64-us/mm-n64-us-compressed.z64`, and the following text printed:

```bash
build/n64-us/mm-n64-us.z64: OK
```
and
```bash
build/n64-us/mm-n64-us-compressed.z64: OK
```

If you instead see the following:

```bash
build/n64-us/mm-n64-us.z64: FAILED
md5sum: WARNING: 1 computed checksum did NOT match
```
or
```bash
build/n64-us/mm-n64-us-compressed.z64: FAILED
md5sum: WARNING: 1 computed checksum did NOT match
```

This means that something is wrong with the ROM's contents. Either the baserom files are incorrect due to a bad ROM, or some of the code is not matching.

Running `make init` will also make the `./expected` directory and copy all of the files there, which will be useful when running the diff script. The diff script is useful in decompiling functions and can be run with this command: `./tools/asm-differ/diff.py -wmo3 <insert_function_here>`

**Note**: to speed up the build, you can pass `-jN` to `make setup` and `make`, where N is the number of threads to use in the build, e.g. `make -j4`. The generally-accepted wisdom is to use the number of virtual cores your computer has, which is the output of `nproc` (which should be installed as part of `coreutils`).
The disadvantage that the ordering of the terminal output is scrambled, so for debugging it is best to stick to one thread (i.e. not pass `-jN`).
(`-j` also exists, which uses unlimited jobs, but is generally slower.)

## Contributing

All contributions are welcome. This is a group effort, and even small contributions can make a difference.
Some work also doesn't require much knowledge to get started.

Please note that is is our strict policy that *Anyone who wishes to contribute to the OOT or MM projects **must not have accessed leaked source code at any point in time** for Nintendo 64 SDK, iQue player SDK, libultra, Ocarina of Time, Majora's Mask, Animal Crossing/Animal Forest, or any other game that shares the same game engine or significant portions of code to a Zelda 64 game or any other console similar to the Nintendo 64.*

Most discussions happen on our [Discord Server](https://discord.zelda.deco.mp), where you are welcome to ask if you need help getting started, or if you have any questions regarding this project or ZeldaRET's other decompilation projects.

For more information on getting started, see our [Contributing Guide](docs/CONTRIBUTING.md), [Style Guide](docs/STYLE.md) and our [Code Review Guidelines](docs/REVIEWING.md) to see what code quality guidelines we follow.
