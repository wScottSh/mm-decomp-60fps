# RDP levers that keep every pixel identical

This is the output of unit R3. It asks whether changes that keep every pixel identical can cut enough RDP time per image to meet the 60 fps budget, and answers with a cost model of where the pinned runner spends RDP time on Majora's Mask's real display lists, three prototypes measured on the corpus, and per-scene sums. The budget and the segment split it starts from are in [budget.md](../fps60/budget.md).

The short answer is no. Measured and bounded identical-output levers remove about 2.6 to 3.5 ms per image in the gameplay scenes, against gaps of 8.8 to 16.2 ms. The largest single term, about 3.6 RDP clocks per pixel of room geometry, is memory latency per span, and that part of the runner's model fails its own hardware checks.

## How to read this document

- **Labels.** Every number is **measured** (run on the pinned runner, sha256 `1f9d59f66929c5b5`), **cited** (read from a document or code), or **inferred** (derived from measured or cited numbers, not run). Tables say which once, in their caption.
- **Two runners.** Gains are measured on the pinned runner. The attribution of RDP time to causes (the cost model below) comes from a log-only build of the same fork source (`54f35f9c7`) with a host-side log added to `ares/n64/rdp/timed.cpp`. It reads RDP state and never writes it. On the same ROM its `stats.tsv` (which includes `trace_hash`, a hash of every event and of the whole machine state) is byte-identical to the pinned runner's for the sct run (measured), so its timeline is the pinned runner's timeline.
- **Two ROMs for attribution.** The census ROM is `BENCH=1` main `6dcd932c8` plus B1's segment instrumentation and one marker command per segment and buffer (an RDP `SetKeyR`, which MM never enables). Its RDP busy per image over the window is p50 26.0 / max 31.3 ms in sct against 25.7 / 31.2 for B1's instrumented ROM (measured). The prototypes are measured on clean `BENCH=1` builds: main is ROM sha256 `231160338bb5642e`, the same ROM as the B1 and H1b baselines.
- **Per tick and per image.** The perf tables use the harness's per-tick `t_rdp_buf`. In the gameplay scenes one gfx task runs per tick, so per tick equals per image (budget.md, Budget per scene).
- **Budget.** RDP busy per image must be at most 15.04 ms (0.90 of the 16.715 ms field, preferences line 3).

## Commands

The tools and patches are in the program store under `reports/R3-tools/` (not committed; preferences line 9 and the brief).

```sh
# log-only runner: the pinned fork source plus rdpattr.patch, built out of tree
git -C ~/repos/ares-wt/mm60-runner archive 54f35f9c7 | tar -x -C ares-log
cd ares-log && git apply rdpattr.patch && python3 tools/n64-timing/behaviors.py
cmake -S . -B ../ares-log-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARES_CORES=n64 -DARES_SKIP_DEPS=ON
cmake --build ../ares-log-build --target n64-run
# census ROM: B1 instrumentation plus segment markers
git apply census.patch && make -j24 BENCH=1
ARES_RDPATTR=attr-sct.txt FPS60_ANY_RUNNER=1 tools/fps60/run.py ROM --out OUT --scenes sct --jobs 1 --runner ares-log-build/rundir/bin/n64-run
ARES_RDPATTR_TRACE=trace-sct.txt ARES_RDPATTR_TASK=200 FPS60_ANY_RUNNER=1 tools/fps60/run.py ...   # one task, every command
python3 attr.py attr-sct.txt 198; python3 classsum.py 198 attr-*.txt; python3 redund.py trace-sct.txt
# prototypes: perf on the pinned runner, noise pin off
git apply sky.patch && make -j24 BENCH=1 && tools/fps60/run.py ROM --out perf-sky2 && python3 cmp.py perf-base perf-sky2
# F1 with the noise pin: PR 8's harness and BENCH hooks (h1c-src.patch) on both ROMs
f1.sh base-h1c.z64 sky2-h1c.z64 sky2
```

`ARES_RDPATTR` writes one block per `SYNC_FULL`. It splits the RDP's busy time into states (pixels in the pipeline, waiting for a span's color and Z reads, waiting for span slots to drain, texture-load memory wait, texture-load transfer, each sync, primitive setup, command fetch) and charges each interval to the segment and buffer of the primitive or command that owns it. `redund.py` replays one task's command trace and finds texture loads and syncs that change nothing, as defined in [Lever 3](#lever-3-redundant-texture-loads-and-syncs).

## Q1. Where RDP time goes, per primitive

### The runner's cost terms and their evidence

The runner's RDP is a timeline actor (`ares/n64/rdp/timed.cpp`). A primitive costs the command processor a fixed setup, and its spans run in a pixel pipeline that waits on RDRAM. Each term is a row of `ares/n64/timing/behaviors.tsv`. The status column says what hardware evidence supports it, from the fork's own spec (`docs/spec/n64-timing.md`, `n64-timing-results.tsv`, standing run on `d1874254e`). "Fit only" means the only passing check is the data the value was fitted from. Every `hw:*` check is pending because no console capture exists yet (calibration #16).

| Term | Value | Status | What it covers |
|---|---|---|---|
| `rdp.span-1cycle`, `rdp.span-2cycle` | 1 and 0.5 px/clk | 1-cycle pass, 2-cycle fit only | pixel rate (SDK Table 12-1) |
| `rdp.span-dead-pixels`, `rdp.span-line-gap` | 2 px, 2 clk | fit only | tail per span: 4 clk in 1-cycle, 6 in 2-cycle |
| `rdp.primitive-base` | 13 clk | fit only | setup per primitive; no row covers triangle setup, `hw:triangle-setup` pending |
| `rdp.span-read-latency` | 12.5 clk | fail (Thar0 +4.0%, -5.3%, -4.7%, -4.9%) | a span's color or Z read, issue to data |
| `rdp.mem-overhead-read`, `-write` | 21 and 19.5 clk per 64 B | fail (same checks) | the RDP memory interface's own cost per burst |
| `rdp.span-slots`, `rdp.port-lookahead` | 4, 8 | fail | how far shading runs ahead of memory |
| `rdp.read-gate` | IM_RD, Z_CMP | rule passes, its cost fails (+2.5% to +8.1%) | which spans read color and Z |
| `rdp.tmem-load-rate` | 8 B/clk | pending; model gives 0.70 clk/B on a 4 KiB block where MiSTer RTL gives 0.125 and jgemu 0.418 | load transfer |
| `rdp.sync-pipe`, `-tile`, `-load` | 50, 33, 25 clk | pending (the bench checks assert the n64brew values they came from) | sync stalls |
| `rdp.setter` | 1 clk | pending | every other command |
| `rdp.cmd-fifo-dwords`, `rdp.cmd-fetch-burst` | 30 dwords, 128 B | pending, model choice | command fetch |
| `ri.bank-of`, `ri.row-of`, `ri.retry-clean`, `ri.arbitration` | | fail | RDRAM rows, banks and arbitration |
| `ri.read-hit` | 10 tc | pass | RDRAM read on an open row |
| `rdp.pipeline-depth`, `rdp.attribute-stage`, `rdp.span-ram-half`, `-segment`, `rdp.write-run`, `rdp.noise-step`, `-reset` | | pass (functional: pixel output) | what pixels come out, not when |

(cited: behaviors.tsv rows and the spec status columns, read by a sub-agent of this unit; Thar0 = RDP-Timing-Tests `sample_results.txt`)

So the only cycle counts with hardware support are the per-pixel rates and the per-line and per-primitive constants, and those were fitted to one test: a full-screen 320x240 rectangle, 240 spans of 320 pixels (Thar0 `test_main.c`, cited). Every memory term fails its Thar0 checks by 2 to 8%. Syncs, setters, loads and command fetch have no hardware value. No test anywhere measures short spans or small triangles.

### What MM's images cost in the runner

RDP busy per image, mean over the 198 window images, split by state (measured, census ROM on the log-only runner, ms):

| scene | total | pixels | span memory wait | texture load memory | texture load transfer | syncs | setup, setters, fetch |
|---|---|---|---|---|---|---|---|
| title | 20.83 | 8.53 | 5.16 | 5.61 | 1.05 | 0.45 | 0.03 |
| sct | 25.86 | 8.37 | 10.32 | 5.25 | 0.91 | 0.96 | 0.06 |
| field | 19.75 | 7.42 | 7.31 | 3.76 | 0.62 | 0.61 | 0.04 |
| mountain | 24.90 | 10.28 | 11.29 | 2.48 | 0.38 | 0.44 | 0.03 |
| greatbay | 24.92 | 7.92 | 9.41 | 5.63 | 0.98 | 0.92 | 0.07 |

"Pixels" is time the pixel pipeline is running a span, including the per-span tail. "Span memory wait" is time the pipeline has a span ready but waits for its color and Z reads, or waits for earlier spans' write-backs to free a slot. Primitive setup costs almost nothing as a state: its 13 clocks overlap the previous primitive's spans.

Two causes dominate, and neither is per-triangle work. Span memory latency is 37 to 45% of RDP time in the gameplay scenes, and texture loads, almost all of it memory wait, are 11 to 27%.

**Texture loads.** Per byte, the runner charges (measured, all scenes): LoadBlock 0.126 clk/B transfer plus 0.61 to 0.63 clk/B memory wait, LoadTLUT 0.127 plus 0.64 to 0.68, LoadTile 0.126 plus 1.19 to 1.29. A load waits until the pipeline is empty and then fetches its rows from RDRAM with nothing overlapping. LoadTile fetches one burst per row, so a 32x32 CI8 sky tile of 32 B rows costs about twice a LoadBlock per byte. A gameplay image loads 230 to 480 KiB of texture.

### The room's 6.7 clocks per pixel against the hardware rect's 3.95

Room opaque geometry (`ROOM/OPA`, every primitive a shaded, textured, Z-buffered triangle in 2-cycle mode with color and Z read, render mode `G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2`), per Z-tested pixel, mean over the window (measured, log-only runner; the span count and pixel count are the rasterizer's):

| scene | px per image | px per span | clk/px total | pixel | span tail | span memory wait | texture loads | syncs | memory wait per span |
|---|---|---|---|---|---|---|---|---|---|
| sct | 100,726 | 20.5 | 6.69 | 2.01 | 0.29 | 3.62 | 0.68 | 0.09 | 74 clk |
| field | 98,119 | 29.1 | 5.63 | 2.01 | 0.21 | 2.74 | 0.60 | 0.08 | 80 clk |
| mountain | 109,976 | 18.2 | 6.15 | 2.00 | 0.33 | 3.66 | 0.14 | 0.02 | 67 clk |
| greatbay | 94,477 | 20.3 | 7.14 | 2.02 | 0.30 | 3.71 | 1.02 | 0.11 | 75 clk |

(B1's 6.1 clk/px for sct used depth-read bytes as the pixel count; this table counts the rasterizer's pixels.)

The hardware reference is Thar0 L307: the same memory traffic (Z read and write, color read and write, 2-cycle, color, Z and VI in separate banks) on a full-screen rectangle, 3.952 clk/px, 1264.7 clk per 320-pixel line (cited). In sct the room averages 20.5 pixels per span. Per span the runner spends 41 clk in pixels, 6 in the tail and 74 waiting for memory: about 121 clk for 20.5 pixels, which at the rect's rate would cost 81. So the gap is about 40 clk per span, and it is memory latency. Each span issues its own color and Z reads (an 82 B round trip in sct) and waits for them, and the next span's prefetch cannot hide a read that takes longer than the current span's 41 pixel clocks. Texture loads add 0.68 clk/px and syncs 0.09 (inferred from the measured split).

The framebuffer and Z bank layout is not the cause. The two framebuffers are at `0x000500` (bank 0) and `0x7DA800` (bank 7) and the Z buffer is at `0x3B0740` (bank 3), and RDP busy per image is the same within 0.16 ms for images drawn into either framebuffer in every scene (measured: `cimg`/`zimg` in `stats.tsv`, and per-task totals by parity). Backlog row B1-15 therefore gains nothing.

**What this means for the program.** The runner says MM's rooms are bound by memory latency per narrow span. That is the term the fork's spec marks as failing, fitted only on 320-pixel spans. On hardware the cost could be anywhere from the rect's 3.95 clk/px, which would make the room about 4.9 ms per image in sct instead of 10.8 (inferred), to above the runner's. Only a console measurement of short-span cost can settle it.

## Q2. Levers that keep every pixel identical

Each lever below names its mechanism, its reference, why the pixels cannot change, its gain and its F1 result. "F1" means PR 8's harness with the noise pin on (`run.py --noise-pin on`, `fidelity.py`), base ROM = main plus PR 8's BENCH hooks, candidate = the same plus the prototype. Gains are per tick on the pinned runner with the noise pin off.

### Lever 1: draw the sky in one cycle when the second texture is a no-op

**Mechanism.** `Skybox_Draw` (`src/code/z_vr_box_draw.c`) draws every sky in 2-cycle mode with combiner cycle 1 = `(TEXEL1 - TEXEL0) * PRIM_ALPHA + TEXEL0` and cycle 2 = `(PRIM - ENV) * COMBINED + ENV`, and loads two 32x32 CI8 tiles per quad. `PRIM_ALPHA` is the day-night blend. In every corpus scene it is 0 or 255 for the whole window: 0 in sct and mountain, 255 in field and greatbay (read from the command trace, `SetPrimColor` alpha). The other two call sites pass 0 by construction (`z_play.c:1348`, `1399`). File select draws a sky too and gains (below).

- At blend 0, cycle 1 is `((T1 - T0) * 0 + (T0 << 8) + 0x80) >> 8 = T0` exactly, so one cycle of `(PRIM - ENV) * TEXEL0 + ENV` computes the same color. The blender is `G_RM_OPA_SURF` in both cycles: `CLR_IN * 0 + CLR_IN * 1`, a pass-through, so the 1-cycle blender gives the same result. Color dither is `G_CD_MAGICSQ`, which depends on position only, and alpha does not reach memory (no alpha compare, coverage from the rasterizer). TEXEL1 is unused, so its loads can go too.
- At blend 255 the same rewrite uses only TEXEL1. It is exact only where `|T0 - T1| < 128` in every channel at every pixel. That holds for the corpus skies (F1 below) but it is a property of the texture pair, not of the code, so a landing unit must check it for every skybox texture pair, or limit the lever to blend 0.

The prototype builds two extra face display lists at `Skybox_Init` (one that loads only texture 0, one only texture 1, 28,800 B of THA in total) and selects them with `G_CYC_1CYCLE` and the 1-cycle combiner when blend is 0 or 255.

**Reference.** SDK Table 12-1 (1-cycle 1 px/clk, 2-cycle 0.5 px/clk; `rdp.span-1cycle` pass, `rdp.span-2cycle` fit only). The sky reads neither color nor Z, so it is pixel-bound and the rate applies: in the runner the sky's 2-cycle spans cost 2.0 clk/px with no memory wait (measured). Half the texture bytes go with TEXEL1 (fewer bytes is a gain under any load rate). The combiner arithmetic `((A - B) * C + (D << 8) + 0x80) >> 8` is the fork engine's (`ares/n64/rdp/engine/rdp_core.c:5670-5693`, read).

**Gain** (measured, RDP busy per tick, base -> prototype, max / p50 ms):

| scene | max | p50 | RSP gfx max | CPU max |
|---|---|---|---|---|
| title | 26.77 -> 25.75 (-1.02) | 19.89 -> 18.76 (-1.13) | -0.02 | +0.09 |
| filesel | 18.56 -> 14.14 (-4.42) | 15.62 -> 13.64 (-1.98) | -1.25 | -0.96; lag ticks 87 -> 0 |
| sct | 31.65 -> 30.03 (-1.61) | 26.23 -> 24.05 (-2.18) | -1.44 | +0.42 |
| field | 23.93 -> 22.25 (-1.68) | 19.59 -> 18.02 (-1.56) | -1.05 | +0.48 |
| mountain | 25.40 -> 23.71 (-1.68) | 24.80 -> 23.13 (-1.67) | -1.71 | -0.38 |
| greatbay | 25.56 -> 24.22 (-1.34) | 25.19 -> 23.56 (-1.62) | -1.40 | +0.11 |

The RSP gain is the back-pressure the RDP no longer causes plus the dropped load commands. The CPU moves by -0.96 to +0.48 ms, inside the layout sensitivity B1 measured (up to 1.66 ms), so a landing unit must judge it against a same-layout control. File select stops lagging: its 87 lag ticks were RDP-bound.

**F1.** `fb_hash` is identical on every tick of all six scenes (measured: 0 differing ticks of 198 to 396 per scene). F1 still reports FAIL, because the per-tick noise-command hash differs on every tick. The prototype emits a new `SetCombine` and `SetOtherMode` per sky draw and drops the texture-1 load commands. Neither command uses noise (`G_CD_MAGICSQ`, `G_AD_PATTERN`, no `NOISE` combiner input), but the hash covers every combiner and other-mode command as emitted, so any lever that rewrites render state fails it. That is a harness limit, recorded as a follow-up for H2: hash the noise-reading fields, not the whole commands.

**Hardware flag** (preferences line 5): the 1-cycle rate has a vendor reference and passes; the 2-cycle rate is fit only but is the SDK's own figure. The gain does not rest on a failing behavior.

### Lever 2: load the seagull wing texture once per bird

**Mechanism.** Each `EN_TANRON4` seagull (Great Bay Coast) draws five limbs. Its four wing limb display lists each start with the same `gsDPLoadTextureBlock(gSeagullWingTex, ...)`, which writes the same 1 KiB to TMEM 0 and sets the same load and render tile (`object_tanron4.c`, cited). After the first wing, TMEM and both tile descriptors already hold exactly what the load writes. The prototype's `OverrideLimbDraw` gives the second to fourth wings a display list that copies the commands before the load and branches past its seven commands (`SetTextureImage, SetTile, LoadSync, LoadBlock, PipeSync, SetTile, SetTileSize`). Nothing else is skipped, and a body limb drawn in between resets the rule.

**Reference.** n64brew RDP Commands (Load Block, Load Sync, Tile Sync; the sync costs are pending in the fork). Removing a load whose bytes and descriptors are already in place cannot change a texel.

**Gain** (measured): greatbay RDP busy per tick 25.56 -> 24.55 max (-1.00), 25.19 -> 24.17 p50 (-1.02); RSP gfx -0.06; CPU +0.82 ms max (the per-limb copy and the overlay's new code; inside the layout band, see above). Other scenes do not draw seagulls and do not change (sct -0.17 max, +0.04 p50).

**F1.** See the [results table](#f1-results).

**Hardware flag:** the gain is load time, whose rate and the syncs it skips are pending rows in the fork. A load that is not executed costs nothing on any hardware, so the direction is certain and only its size is model-dependent.

### Lever 3: redundant texture loads and syncs

**Mechanism.** The trace of one task per scene (task 200 in sct, 350 in field and greatbay, 150 in mountain; `redund.py`) replays every command. A load is redundant when an earlier load with the same source image, the same load-tile descriptor and the same load command wrote the same TMEM range and no load since has touched it, and the source is not a framebuffer. A Pipe or Tile sync is redundant when no primitive or load has dispatched since the previous one of its kind. A Load sync is redundant when no primitive has dispatched since the previous Load sync. These rules follow what each sync waits for (n64brew RDP Commands, as cited by B1-06): Pipe and Tile syncs wait for primitives in flight, Load sync for primitives that may still read TMEM. Loads count as primitives for Pipe and Tile syncs, because a load runs through the tile machinery.

| scene | loads | redundant loads (KiB) | Pipe syncs, redundant | Load syncs, redundant | bound (ms, inferred) |
|---|---|---|---|---|---|
| sct | 334 | 37 (39.0) | 675, 175 | 334, 172 | 0.49 loads + 0.21 syncs |
| field | 287 | 38 (27.0) | 543, 151 | 287, 185 | 0.34 + 0.20 |
| greatbay | 455 | 133 (123.3) | 881, 183 | 455, 212 | 1.54 + 0.23, of which lever 2 measured 1.00 |
| mountain | 214 | 37 (25.4) | 418, 97 | 214, 107 | 0.31 + 0.12 |

The bound charges each redundant byte the runner's measured cost for its load type (0.755 clk/B for LoadBlock, 0.80 for LoadTLUT) and each redundant sync its fixed cost (50 and 25 clk). Most redundant loads sit in the opaque actor and room segments (greatbay: 112 of 133 in `ACTORS/OPA`, most of them the seagulls of lever 2). Dropping one keeps the draw order; it only skips a load whose bytes are already in TMEM.

**Hardware flag:** load rate and sync costs are pending rows. Removing a sync is safe on hardware only by the hazard rules above, which an emulator cannot check (libdragon `rdpq.c`, cited in B1-06), so the sync part needs a hazard proof per site and is not landable on fb_hash evidence alone. The load part is safe by construction.

### Lever 4: LoadBlock for the sky tiles that remain

**Mechanism.** After lever 1 each sky draws 32 `LoadTile` loads of 32x32 CI8 tiles from 128-wide images (`z_vr_box.c:160-210`). A tile-ordered copy of each sky texture loaded with `LoadBlock` writes the same TMEM bytes (B1-10). Gain bound: 32 KiB x (about 1.40 - 0.76) clk/B = 0.34 ms per sky layer (inferred from the measured per-byte costs). Not prototyped. The rate difference rests on `rdp.tmem-load-rate` (pending) and the RDRAM row model (fail); LoadTile moving one row per burst is the RTL structure (MiSTer, cited in the fork's rdp-command-timing.md), so it needs that second reference.

### Lever 5: one rectangle for a flat-colored sky

**Mechanism.** The sky's second combiner cycle is `(PRIM - ENV) * COMBINED + ENV`. When the primitive and environment colors are equal, it is `0 * COMBINED + ENV`, which is exactly `ENV` by the arithmetic above, so every sky pixel is the one color ENV whatever the textures hold. In mountain both are black (`SetPrimColor 000000`, `SetEnvColor 000000`, read from the trace) under a full-screen white fog filter. The title's sky takes the same branch (its measured gain is below). The prototype draws, in place of the sky's faces, one 1-cycle `FillRectangle` over the screen with combiner `ENV` in the sky's own render mode (`G_RM_OPA_SURF`) and dither (`G_CD_MAGICSQ`, position-only), so color and dither match. Coverage: the rectangle writes full coverage with `CVG_DST_CLAMP`, and the sky's triangles do the same in their interiors. At the seams between sky triangles the two partial coverages sum to full, so the clamped result is full there too, but that is inferred and F1 decides it.

**Reference.** SDK Table 12-1 (1-cycle rate); the rectangle reads no texture and no memory, so its cost is pixels and the per-line tail. Gain does not rest on a failing behavior.

**Gain** (measured, RDP busy per tick, max / p50 ms): mountain 25.40 -> 22.90 (-2.50) / 24.80 -> 22.25 (-2.55), RSP gfx -2.59; title 26.77 -> 25.31 (-1.46) / 19.89 -> 18.29 (-1.59). sct, field and greatbay do not take the branch and move by -0.26 to +0.16 (layout). It replaces lever 1 where both apply; the two were not measured together.

**F1.** `fb_hash` is identical on every tick of all six scenes (measured), so the seams inference holds on the corpus. The noise-command hash differs only in title and mountain, the two scenes that take the branch, for the same harness reason as lever 1: the lever emits new `SetCombine` and `SetOtherMode` commands. The other four scenes pass F1.

### Rejected or not identical

- **Front-to-back room order** (B1-05): changes AA edge blending and coverage. Not identical by construction.
- **Dropping IM_RD or Z_CMP on the room**: `AA_ZB_OPA_SURF` reads memory color and coverage for edge AA, and Z compare sets the blend flags. Rejected.
- **Texture rectangles for quads**: no corpus geometry is a screen-aligned quad drawn as two triangles in the hot segments (sky quads are perspective). Nothing to gain.
- **Culling off-screen room meshes earlier** (B1-04): the off-screen share is not measured here, and the room's cost is per span of visible pixels, which culling cannot reduce.
- **Bank placement** (B1-15): already separate, 0 gain (measured above).
- **G_PM_1PRIMITIVE in the HUD** (B1-11): waits for H2 (preferences line 26).

### F1 results

| prototype | title | filesel | sct | field | mountain | greatbay |
|---|---|---|---|---|---|---|
| lever 1, sky one cycle | fb identical, noise hash differs | fb identical, noise hash differs | fb identical, noise hash differs | fb identical, noise hash differs | fb identical, noise hash differs | fb identical, noise hash differs |
| lever 2, seagull wing | PASS | PASS | PASS | PASS | PASS | PASS |
| lever 5, flat sky | fb identical, noise hash differs | PASS | PASS | PASS | fb identical, noise hash differs | PASS |

(measured; "fb identical" = `fb_hash` equal on every tick of the window; PASS = `fb_hash` and the noise-command hash equal on every tick, 197 to 396 ticks per scene)

## Q3. Does it close the gap?

Per image, RDP busy at max (measured base from budget.md and this unit's base run; lever gains as labeled):

| scene | retail max | gap to 15.04 | sky: lever 1 or 5 (measured) | lever 2 (measured) | lever 3 (inferred bound) | lever 4 (inferred bound) | sum | after | closes |
|---|---|---|---|---|---|---|---|---|---|
| title | 26.77 | 11.73 | 1.46 (lever 5) | 0 | not traced | 0 | 1.46 | 25.31 | 12% |
| sct | 31.65 | 16.61 | 1.61 (lever 1) | 0 | 0.70 | 0.34 | 2.65 | 29.00 | 16% |
| field | 23.93 | 8.89 | 1.68 (lever 1) | 0 | 0.54 | 0.34 | 2.56 | 21.37 | 29% |
| mountain | 25.40 | 10.36 | 2.50 (lever 5) | 0 | 0.43 | 0 | 2.93 | 22.47 | 28% |
| greatbay | 25.56 | 10.52 | 1.34 (lever 1) | 1.00 | 0.77 | 0.34 | 3.45 | 22.11 | 33% |

(retail max is the per-tick `t_rdp_buf` of this unit's base run, which equals B1's main-ROM cross-check; per image B1 measured sct 31.22, a 16.2 ms gap. Lever 3 for greatbay excludes the seagull loads lever 2 removes; mountain's lever 3 is from task 150. Lever 4 is 0 where lever 5 removes the sky's loads. Sums treat the levers as additive, which is inferred.)

**No scene closes.** The identical-output levers found here remove 12 to 33% of the gap (1.5 to 3.5 ms per image). What remains, 6 to 14 ms per image, is the per-span memory latency of room and actor geometry (7.3 to 11.3 ms of span memory wait per image) and the pixel time itself (7.4 to 10.3 ms), which no change that keeps retail's geometry, render modes and draw order can remove.

This is a gate (preferences line 17), proposed as **G-R3-rdp** for the program's `gates.md` and as backlog row R3-07:

- The 60 fps budget cannot be met in any gameplay scene on the pinned runner with identical-output RDP levers alone.
- The dominant remaining term rests on runner behaviors that fail their hardware checks and were fitted only on full-width spans (`rdp.span-read-latency`, `rdp.mem-overhead-read`/`-write`, `rdp.span-slots`, `rdp.port-lookahead`). If a console's cost on short spans is near Thar0's full-span rate, room opaque would be about 4.9 ms instead of 10.8 in sct (inferred) and the picture changes.
- Default: keep landing the identical-output levers (each is a gain under any plausible memory model), and route a console measurement of short-span cost (spans of 8 to 64 pixels with Z and color read-modify-write, 1- and 2-cycle) to the ares calibration program (fork calibration #16), because the runner is the instrument and is paused (preferences line 6). Until that lands, no RDP plan can be shown to reach the budget, and the program's done predicate cannot be met on the pinned runner.

## Backlog rows

R3 adds rows R3-01 to R3-07 to `backlog.tsv` in the program store:

- R3-01 is lever 1, R3-02 lever 2 and R3-03 lever 5. These three carry measured gains.
- R3-04 is the generic form of lever 3's load part, R3-05 its sync part, and R3-06 lever 4. These carry inferred bounds.
- R3-07 is the console measurement the gate asks for.

This unit measured the bank-layout row B1-15 at 0 gain.
