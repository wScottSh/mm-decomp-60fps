# Techniques for a locked 60 fps Majora's Mask on retail N64

Survey date: 2026-10-25. Unit R1 of the MM 60 fps program.

This survey lists every technique found that could get MM to a new image on every NTSC VI field, on a retail N64 with the Expansion Pak, while each logic tick's keyframe stays pixel-identical to retail and game speed stays unchanged. It ranks the techniques by expected gain on each resource (CPU, RSP, RDP, RDRAM bandwidth) and classifies each one's fidelity risk under the program's rule: pixel-exact or reject.

## How to read this document

- **Labels.** Every number is labeled. **cited** means a fetched source states it. **measured** means a research agent ran it this session. **inferred** means our arithmetic or reasoning, shown where it is short.
- **Fidelity classes.** **identical** means the output is the same by construction. **may differ** means it is identical only under a condition that must be proven per scene. **differs** means it changes pixels, audio or logic, so the program rejects it.
- **Code links.** Links to this repo point at main `caeb013ac597d556367c30e6f5d292100f93e58f` (base URL `https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/`). A bare MM path such as `src/code/z_rcp.c L290-L298` refers to that tree. Third-party links are permalinks at the commit each section names.
- **Detail lives in the appendices.** Sections A to E are the five research slices, each with its own technique table, sources list and gaps. The front sections are the synthesis.
- **Retail numbers.** Retail per-tick numbers come from [tools/bench/README.md](../../tools/bench/README.md), section "Fork baseline". They come from the ares timing fork and are not hardware-calibrated.

## Bottom line

1. **Interpolation does not reduce RSP or RDP work.** Every presented field needs a full render. So per-field RSP and RDP work is about retail's per-tick work, done three times as often (inferred; appendices A to D agree). Retail per tick on the fork: RSP gfx 15.8 to 24.7 ms, RSP audio 5.27 to 9.40 ms, gfx task start to full sync 26.1 to 34.2 ms (cited, bench README). The 90% budget per field is 15.0 ms of a 16.68 ms field (inferred from preferences line 3).
2. **The CPU is the most tractable resource.** Both PC ports build the display list once per tick and replay it with patched matrices (cited code, appendices A and B). On N64 the same idea saves two of three display-list builds per tick: build once, then overwrite the `Mtx` words in place before each field's gfx task (inferred). Cache stalls cost about 14 to 25 ms per tick (inferred from cited miss counts and per-miss costs, appendix E). Code placement that keeps logic ops unchanged is the largest identical CPU lever found, at about 4 to 7 ms per tick if I-cache misses halve (inferred).
3. **The RSP needs a 1.3x to 2.1x cut in gfx time with identical output** (inferred, appendix C). The only route found is to optimize MM's own microcode while keeping the RDP command bytes identical. A matching source for MM's `F3DZEX.NoN fifo 2.08I` exists, and it was built byte-identical this session (measured, appendix C). F3DEX3 as shipped changes Z, decal, point-light, clip and lookat math. Reject it as a drop-in (cited docs, appendix C). Audio RSP time per field does not change at 60 fps, because MM runs one audio task per VI retrace (cited code, appendix C).
4. **The RDP decides feasibility, and its retail busy time is not yet known.** One full-screen pass in MM's world render mode costs 4.856 ms on hardware (cited, Thar0 RDP-Timing-Tests). With the Z clear, the floor is about 5.2 ms per field (inferred). No pixel-reducing RDP lever keeps fidelity. The identical ones only remove stalls (syncs, redundant loads, RDRAM contention), at sub-ms each (inferred, appendix D). The bench's 26 to 34 ms "RDP" figure includes RSP feed time, so it is not RDP busy time (cited, bench README). The `BENCH=1` build already records `dpc_pipebusy`, but no published baseline reports it yet. B1 must measure it first.
5. **No console precedent exists.** Every OoT and MM 60 fps cheat code found sets the update divisor from 3 to 1. That runs logic three times as often and breaks physics and timers (cited forum posts, appendix E). No prior work keeps a 20 Hz tick and interpolates on console.
6. **New hazard: logic reads RDP time.** `func_80173B48` returns the tick's VI time minus `gRDPTimeTotal` ([game.c L300-L304](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/game.c#L300-L304)). Rain, snow and Player dust counts scale with it ([z_kankyo.c L2080](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/z_kankyo.c#L2080), [z_object_kankyo.c L521](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L521), [z_player.c L10320-L10346](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/overlays/actors/ovl_player_actor/z_player.c#L10320-L10346)). `gRDPTimeTotal` is written once per Graph frame ([graph.c L352](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/graph.c#L352)). So any change in RDP timing, from three gfx tasks per tick or from any RSP or RDP speedup, changes particle counts and the logic that follows them (inferred). The architecture unit (A1) must decide what value logic sees. This is a gate candidate.

## Budget: which resource each technique attacks

Per field at 60 fps, from retail per-tick numbers (all inferred from cited bench numbers):

| Resource | Retail per tick (fork) | Need per field (90%) | Gap if every field renders fully | Levers that keep fidelity |
|---|---|---|---|---|
| CPU | game update 18.1 to 31.0 ms median, includes one display-list build | One tick of logic plus three presentations within three fields (45.0 ms per tick) | Fits only if the two extra presentations are much cheaper than a full draw | Build once and patch matrices (1), code placement (4), cache ops (5), GCC for new code only (6) |
| RSP gfx | 15.8 to 24.7 ms | about 12 ms, after 1.8 to 3.1 ms of audio per field | 1.3x to 2.1x, before removing FIFO-stall time | Microcode work on the matching 2.08I source (10, 11), display-list hygiene (13, 14), larger FIFO (12) |
| RSP audio | 5.27 to 9.40 ms per tick, 3 tasks | unchanged per field, 1.8 to 3.1 ms | none | none needed |
| RDP | not measured. Gfx start to full sync is 26.1 to 34.2 ms and includes RSP feed | 15.0 ms | unknown until `dpc_pipebusy` is reported | stall removal only (18 to 22) |
| RDRAM | 4.79 to 5.59 MiB per tick, all masters | about 333 MB/s if every field redraws, about 67% of a 500 MB/s peak (upper-bound sketch, appendix D) | unknown | contention cuts (19), fewer DL refills and loads (13, 20) |

## Ranked techniques

Ranked within each resource by expected gain. Numbers in the first column match the budget table. "Own code" in the license column means the idea is reimplemented, not copied.

| # | Technique | Resource | Expected gain | Fidelity risk | Effort | License if code is reused | Detail |
|---|---|---|---|---|---|---|---|
| 0a | Measure true RDP busy per tick (`dpc_pipebusy`, `dpc_bufbusy`) in the bench baseline | diagnostic | Decides RDP feasibility | identical (BENCH only) | S | n/a | D |
| 0b | Count RSP cycles with PC in the FIFO-full spin loops (IMEM 0x1298, 0x12A8 to 0x12BC) on the fork | diagnostic | Splits "RSP gfx" into work and RDP wait | identical (bench only) | S | n/a | C |
| 0c | Count `G_MTX` loads and CPU-written vertex blocks per tick | diagnostic | Sizes the per-field patch cost of technique 1 | identical (BENCH only) | S | n/a | A, B |
| 1 | Build the display list once per tick. Before each field's gfx task, overwrite each recorded `Mtx` (and `G_SETTILESIZE` words for scrolls) with values lerped from the previous tick to the current one. The last field uses retail's bits. | CPU | Saves two of three display-list builds per tick (inferred). RSP and RDP unchanged | identical on keyframes if the last field uses the unpatched retail list. Intermediate fields are new by design | H. Needs double-buffered matrices or a wait for the RSP, and the list must stay valid for three tasks | 2S2H CC0-1.0. Shipwright no license. Zelda64Recomp GPL-3.0. RT64 MIT. Reimplement | A, B |
| 2 | Interpolation keys from a spawn counter plus limb index (Zelda64Recomp), effect slot epochs (2S2H). Explicit IDs only, no heuristic matching | CPU | Makes 1 correct. No perf gain itself | identical keyframes | M | own code | A, B |
| 3 | Component-wise matrix lerp, or lerp of inputs, instead of decompose and slerp | CPU | Large per matrix versus decomposition (inferred) | intermediate fields only | L | RT64 MIT | B |
| 4 | Place hot code so it does not alias in the 16 KB direct-mapped I-cache (overlay allocation alignment, or link hot overlay code resident) | CPU, RDRAM | About 4 to 7 ms per tick if I-cache misses halve (inferred) | identical ops. Heap and code addresses move, so layout-dependent UB must be ruled out by a lockstep check | M | own code | E |
| 5 | `cache` Create Dirty Exclusive on write-only buffers (display lists, matrices) to skip the fill read | CPU, RDRAM | Saves a D-cache fill per written line (inferred, unquantified) | identical | L to M | own code | E |
| 6 | GCC only for new presentation code. IDO stays for game logic | CPU | Lets new code use a modern optimizer | identical logic | L to M | n/a. HackerOoT mixes IDO and GCC objects | E |
| 7 | GCC for gameplay code with strict IEEE plus `-fno-strict-aliasing -fwrapv` | CPU | Not published by any source found | may differ (UB, layout, compiler). Needs a lockstep RAM diff against IDO | H | n/a | E |
| 8 | Upstream mm GCC flags as they are (`-Os -ffast-math -ftrapping-math -fno-associative-math`) | CPU | unknown | differs (reciprocal math, signed zeros, finite math, strict aliasing) | L | n/a | E |
| 9 | Kaze-style math or collision replacement (polynomial sine, new collision) | CPU | About 0.2 ms per frame for sine (cited, secondary) | differs | M | Kaze repo has no license | E |
| 10 | Build MM's gfx microcode from the matching `F3DZEX.NoN fifo 2.08I` source | RSP | 0 by itself. Enables 11 | identical (byte-identical build, measured) | L | Nintendo-derived, same posture as the decomp's extracted asm. Flag for Scott | C |
| 11 | Output-preserving optimizations on the 2.08I source: command dispatch, small RDP commands, cull paths, buffer sizes, DL prefetch | RSP | Unknown. F3DEX3's author counts, as a reference: tri draw 172 to 149 cycles, dispatch 12 to 10, small RDP command 14 to 4 (cited, different code) | identical if the RDP FIFO bytes match per frame | H | as 10 | C |
| 12 | Larger gfx FIFO than 96 KiB | RSP scheduling | The RSP finishes earlier when the RDP is behind. No RDP gain (inferred) | identical | L | n/a | C |
| 13 | Display-list hygiene: merge `gSPVertex` batches, drop redundant state, segment and matrix reloads, flatten tiny DLs | RSP, RDRAM | Unquantified. About 12 cycles per removed command (cited for F3DEX2 dispatch) | identical if the RDP state at every primitive is unchanged. Syncs excluded | M | n/a | C |
| 14 | More `gSPCullDisplayList` | RSP | Skips vertex and tri work of offscreen DLs | identical if scissor is within viewport | M | n/a | C |
| 15 | Swap in F3DEX3 (any variant) | RSP, RDP | Cited author counts above. Occlusion planes save 3 to 4 ms of RDP (cited) | differs (Z scale, decal Z, point lights, clipping, lookat rounding) | M | public domain mods on a Nintendo base | C |
| 16 | XBUS gfx microcode | RDRAM | About half the RDP-command RDRAM I/O (cited, SDK) | identical pixels, but the RSP stalls on the RDP and audio cannot overlap. Reject | L | n/a | C |
| 17 | Audio: `specUnk4 = 2`, or a faster NEAD-ABI microcode | RSP | Up to part of 1.8 to 3.1 ms per field (inferred) | may differ (`specUnk4`). Identical only with per-command verification for a new microcode. No source found | L / very H | Nintendo binary | C |
| 18 | Keep FB0, FB1 and Z in separate RDRAM banks. Verify the runtime Z address | RDP, RDRAM | Likely 0, since Z is probably in bank 3 already (inferred). Up to 24% per full pass if Z shares the drawn FB's bank (cited Thar0 numbers) | identical | S | n/a | D |
| 19 | Cut CPU and RSP RDRAM traffic while the RDP runs | RDP, RDRAM | VI alone costs 9% of a full pass (cited). CPU and RSP share not measured | identical | M to L | n/a | D, E |
| 20 | Skip redundant texture and TLUT loads | RDP, RDRAM | About 9 us per 4 KiB load avoided (inferred) | identical if TMEM bytes and tile descriptors match at every draw | M | n/a | D |
| 21 | LoadTile to LoadBlock where the layout allows | RDP, RDRAM | Not quantified ("faster", cited) | identical only if TMEM bytes match, including the odd-line word swap | M | n/a | D |
| 22 | Remove redundant `gDPPipeSync`, tile and load syncs | RDP | 25 to 50 clk each, about 0.24 ms per 300 pipe syncs (inferred) | may differ. Emulators do not model missing-sync bugs (cited), so an emulator `fb_hash` cannot prove it. Needs hardware evidence | M | n/a | C, D |
| 23 | Front-to-back sort of opaque draws | RDP, RDRAM | Z-fail 3.014 vs Z-pass 4.856 ms per full screen (cited) | may differ (AA edge blend and the `ZMODE_OPA` dz rule are order-dependent) | M | n/a | D |
| 24 | Cull fully hidden geometry | RSP, RDP | Up to its Z-fail cost | may differ | L | n/a | D |
| 25 | RSP-DMA Z clear instead of FILL (libdragon) | RDP to RSP | About 0.31 to 0.35 ms of RDP per field (inferred), moved to the RSP, which is also over budget | may differ (hidden 9th-bit content not found) | S to M | libdragon public domain | D |
| 26 | Drop `G_PM_1PRIMITIVE` (HUD, transitions) | RDP | 30 to 40 clk per primitive (cited) | may differ | S | n/a | D |
| 27 | Skip the Z clear, AA off, `IM_RD` off, 1-cycle fog, narrower scissor | RDP | `IM_RD` off saves 21% of a full pass (cited numbers) | differs. Reject | S | n/a | D |
| 28 | Port render-path swaps (stars as quads, textbox and letterbox as triangles, glow by vertex Z test, motion-blur alpha rescale) | n/a | n/a | differs at keyframes. Reject | n/a | GPL-3.0 / CC0 | A, B |
| 29 | Divisor cheat code (`R_UPDATE_RATE` 3 to 1) | n/a | n/a | differs (logic runs 3x, physics and timers break) | trivial | n/a | E |

Reading the table (inferred). Techniques 1, 4, 10 and 11 carry most of the identical-by-construction gain. Technique 1 solves the CPU side's structure. Technique 4 is the largest identical CPU speedup found. Techniques 10 and 11 are the only identical RSP route of any size. The RDP column has no large identical lever. If B1 finds retail RDP busy above about 15 ms in a corpus scene, no technique found here closes that scene's gap without a fidelity change. B1 would then report it as a gate (plan.md, B1).

## MM interpolation hazards found by the ports

Both PC ports run MM's logic at 20 Hz and make extra frames by interpolating matrices. Their hazard handling is the best catalog of what goes wrong. Full per-call-site tables with permalinks are in appendix A (2S2H, "MM interpolation hazards found by 2S2H") and appendix B (Zelda64Recomp, "MM interpolation hazards found by Zelda64Recomp"). Prefix `S` is 2S2H at `b30a286c`, `Z` is Zelda64Recomp at `b65c482e`, `R` is RT64 at `23cab603`.

| Hazard | 2S2H | Zelda64Recomp | Pointers |
|---|---|---|---|
| Camera cut (shot change, warp, respawn) | Velocity-prediction check skips recording for the whole tick. View-delta heuristics bump a camera epoch | Eye/at velocity predictor plus per-scene overrides (Majora fight, Pirates' Fortress moat, Stone Tower, Music Box House) | [S CameraInterpolationFixes.cpp L33-L107](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/Camera/CameraInterpolationFixes.cpp#L33-L107), [Z camera_transform_tagging.c L33-L268](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_transform_tagging.c#L33-L268) |
| Scripted camera jump (doors) | Explicit `DontInterpolateCamera` | Covered by the predictor | [S z_camera.c L6880-L6883](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_camera.c#L6880-L6883) |
| Skybox | Keyed on camera epoch | Own ID, skip-all on camera skip | [S z_vr_box_draw.c L24-L27](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_vr_box_draw.c#L24-L27), [Z sky_transform_tagging.c L44-L102](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L44-L102) |
| Stars (texrects, no matrix) | Drawn as projected quads when fps is not 20. Changes the list | Drawn as 3D billboards at a smaller max size at any fps | [S z_kankyo.c L3394-L3401](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_kankyo.c#L3394-L3401), [Z sky_transform_tagging.c L105-L290](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L105-L290) |
| Billboards | Not separately handled | Tracks billboard matrices through every `Matrix_*` op. Turns off rotation lerp on a camera cut | [Z billboard_tagging.c L36-L169](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/billboard_tagging.c#L36-L169) |
| Actor identity and memory reuse | Keyed by actor pointer. No spawn guard, so a new actor at a freed address can blend from the old one (inferred). Shipwright tags every spawn | Monotonic spawn counter times 512 plus limb index | [S z_actor.c L2925-L2960](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L2925-L2960), [Z actor_data.c L48-L52](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c#L48-L52) |
| Actor turns near 180 degrees | Actor-relative lerp. Snap above 90 degrees, about 122 for Deku spin | Decomposed lerp | [S FrameInterpolation.cpp L234-L267](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L234-L267) |
| Skeleton limbs, skinned meshes (Epona) | Matrices recorded. CPU-written skin vertices not handled (open PR 1986) | Per-limb IDs in all 12 `SkelAnime_Draw*`. Skin gets vertex lerp | [Z actor_transform_tagging.c L232-L345](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L232-L345), [Z L1239-L1295](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L1239-L1295) |
| Teleports and resets (cutscene cues, boulders, item pickup, chuchu revive) | Per-actor key toggles | Cue-distance threshold sets a one-frame skip flag. Per-actor flags | [S z_en_goroiwa.c L1615-L1629](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Goroiwa/z_en_goroiwa.c#L1615-L1629), [Z cutscene_patches.c L9-L130](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/cutscene_patches.c#L9-L130) |
| Particle and effect slot reuse | Per-slot epoch counters (28 writes) | Reset flag gives one skip frame. ID is slot based | [S z_effect_soft_sprite.c L160-L170](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_effect_soft_sprite.c#L160-L170), [Z particle_transform_tagging.c L12-L100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/particle_transform_tagging.c#L12-L100) |
| Weather particles wrapping around the camera | Epoch bump on wrap | Skip on move or wrap | [S z_object_kankyo.c L282-L316](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L282-L316), [Z effect_transform_tagging.c L15-L250](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_transform_tagging.c#L15-L250) |
| Sword and weapon trails (CPU-written vertices) | Never interpolated | Per-vertex lerp with previous positions | [S z_eff_blure.c L717-L722](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_eff_blure.c#L717-L722), [Z sword_trail_transform_tagging.c L11-L140](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sword_trail_transform_tagging.c#L11-L140) |
| Scrolling textures | New opcode lerps x to x + step. At t = 1 that draws one tick ahead of retail (inferred). Breaks keyframes | RT64 tile interpolation with wrap detection | [S z_rcp.c L1378-L1430](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_rcp.c#L1378-L1430), [R rt64_game_frame.cpp L586-L656](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_game_frame.cpp#L586-L656) |
| Terrain and rooms streaming in | Not separate | Rooms never match. Only tile scroll interpolates | [Z terrain_transform_tagging.c L10-L40](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/terrain_transform_tagging.c#L10-L40) |
| HUD and textbox | No special handling. Texrect elements are not lerped | Textbox and letterbox redrawn as triangles (changes output) | [Z ui_patches.c L892-L1162](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L892-L1162) |
| Pause menu (kaleido) | Page turns and cursor jumps snap | All 4 pages drawn. Cursor skips on jumps | [S z_kaleido_scope_NES.c L662](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c#L662), [Z ui_transform_tagging.c L57-L600](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_transform_tagging.c#L57-L600) |
| Lens flares, light glow | Not separate | Flares skip on camera cut. Glow replaces the CPU Z read with a vertex Z test (changes output) | [Z effect_transform_tagging.c L696-L765](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_transform_tagging.c#L696-L765), [Z culling.c L76-L150](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/culling.c#L76-L150) |
| Motion blur (framebuffer feedback) | Copy once per tick | Alpha rescaled by framerate (changes output) | [S z_play.c L124-L129](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_play.c#L124-L129), [Z effect_patches.c L54-L126](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L54-L126) |
| Transition circle, lens overlay, ocarina wipes | Not separate | Rebuilt as quads or custom vertices | [Z effect_patches.c L141-L247](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L141-L247), [Z ocarina_effect_patches.c L1033-L1100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ocarina_effect_patches.c#L1033-L1100) |
| Giants cutscene timed to console lag | Not handled | Extra VI periods inserted on measured lag frames so music stays in sync | [Z fixes.c L263-L297](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/fixes.c#L263-L297) |
| Scene transitions | No explicit handling found | No dedicated patch found | gap in both |

### Hazards specific to an N64 build

The PC ports render on a host GPU, so some hazards only appear on N64 (all inferred unless cited):

- **Keyframe must be the retail list.** RT64's "skip" and "no match" paths draw the current tick's matrix exactly ([R rt64_math.cpp L177-L198](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/common/rt64_math.cpp#L177-L198)), which suits F1. A lerp evaluated at t = 1 may differ from the original matrix by rounding, so the last field must use the original `Mtx` bits, not a lerp output.
- **Lerp from the previous tick to the current one.** Never lerp toward a predicted next value. 2S2H's scroll opcode shows why: its keyframe is one tick ahead.
- **Framebuffer feedback.** Motion blur and the pause-background prerender blend the previous displayed image. If intermediate fields enter that chain, the keyframe's pixels change and F1 fails. The feedback must see retail's previous keyframe.
- **Texrects and screen-space draws** (stars, HUD, textbox) cannot follow a matrix. Both ports change their render path to interpolate them, which our rule rejects. On N64 they can only be redrawn with the current tick's coordinates on every field.
- **CPU-written vertices** (Epona skin, trails, Woodfall water) need a second vertex buffer per field.
- **Logic reads RDP time** (`func_80173B48`, see Bottom line item 6).
- **Audio is not a hazard.** It runs per VI retrace with 544 samples per task at 32 kHz ([audio_thread_manager.c L12-L45](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/audio_thread_manager.c#L12-L45), [heap.c L945-L953](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/audio/lib/heap.c#L945-L953)). The scheduler yields the gfx task for each audio task ([sched.c L410-L415](https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/sched.c#L410-L415)). With three gfx tasks per tick there are three times as many yields.

## Licenses

| Project | License | Reuse note | Source |
|---|---|---|---|
| 2Ship2Harkinian | CC0-1.0 | Its interpolation code started in Shipwright, so the CC0 grant's provenance is unverified. Reimplement | appendix A |
| Shipwright | none (GitHub reports no license) | All rights reserved by default. Do not copy | appendix A |
| libultraship | MIT | | appendix A |
| Zelda64Recomp, its `patches/`, N64ModernRuntime | GPL-3.0 | Copying patch code brings GPL obligations. Reimplement | appendix B |
| RT64, N64Recomp | MIT | | appendix B |
| F3DEX3 | Sauraen's modifications public domain, on a Nintendo base | Same legal posture as the decomp's own extracted microcode. Flag for Scott | appendix C |
| libdragon | public domain dedication | | appendix D |
| Thar0/RDP-Timing-Tests | MIT | Data only | appendix D |
| HackerSM64, HackerOoT, rasky/n64-systembench, Kaze's SM64 repo | none found | Reimplement | appendices D, E |
| zeldaret/mm | none found | | appendices B, E |

## Open measurements

These are the gaps the slices could not close. Each blocks a ranking above.

1. Retail RDP busy per tick (`dpc_pipebusy`) per corpus scene. Owner: B1.
2. Share of "RSP gfx" spent in the FIFO-full spin loops. Owner: B1, through the fork's RSP PC (no fork edit; a gate if it needs one).
3. Matrices and CPU-written vertex blocks per tick. Owner: A1 or B1.
4. Runtime Z-buffer address (bank). Owner: B1.
5. Cache-miss cost under RCP contention, and whether XPROF slot 0 counts all threads. Owner: B1.
6. Hidden 9th-bit content after an RSP DMA write (decides technique 25). Needs hardware.
7. Whether a GCC build of current mm boots (no MIPS GCC is installed on unicron, measured).
8. Kaze's video transcripts could not be read, so his `-O2`/`-Os`, inlining and memcpy findings are not cited from primary text.

## Appendix A. 2Ship2Harkinian and Shipwright frame interpolation

Clones (2026-10-25). All code claims below were read from these trees.
- 2S2H: `2ship2harkinian/2ship2harkinian` (HarbourMasters URL redirects there) at `b30a286c55bd94f0868e6a103d0e4c511f7003a9` (committed 2026-10-24).
- Shipwright: `HarbourMasters/Shipwright` at `2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41` (2026-10-20).
- libultraship: `Kenix3/libultraship` at `7f9b86a593c526fc42261d7fe197100cecf57178` (2026-08-30). This is the submodule SHA 2S2H pins (`git ls-tree HEAD libultraship`). Shipwright pins `da6daf1a50a5e46d6641408d52227e31e85e46df`. I read only the 2S2H pin.

Permalink prefixes used below:
- `S` = https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9
- `W` = https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41
- `L` = https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178

### Summary

- Both ports use one design (Shipwright PR #309 "Experimental interpolation" by Emill, merged 2022-05-13, then copied into 2S2H). Play_Update and Play_Draw run once per 20 Hz tick. The display list is built once. During Play_Draw the port records every CPU matrix-stack op and every matrix written to an `Mtx`, into a tree keyed by `(pointer, int)` labels. For each displayed frame the host walks the previous and current trees together, lerps the recorded matrices, and returns a map `Mtx* -> MtxF`. The Fast3D interpreter then re-executes the same display list and swaps in the replacement whenever a `gSPMatrix` points at a mapped address.
- Draw passes per tick: one game Play_Draw (DL build) plus N interpreter passes over that DL, N = target fps / 20 (3 at 60 fps). Source: [S/mm/2s2h/BenPort.cpp#L1170-L1250](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1170-L1250).
- Keyframes. The last pass of each tick has t = 1 and an empty replacement map, so matrices come straight from the DL. This matches the non-interpolated render for matrices. It is not a pixel-identical keyframe for everything. 2S2H changes the DL itself when fps != 20 (stars), and the lerped texture scroll opcode shows the next tick's scroll offset at t = 1 at every fps (details below).
- Hazards are handled by four mechanisms. A whole-tick snap (camera heuristics stop recording). A camera epoch that re-keys camera and skybox nodes. Per-object epoch counters that re-key reused effect slots. `IgnoreActorMtx` and the wider-angle flag for actor-relative lerp. Unmatched nodes snap to the new value.
- Transfer to N64. The recording and matrix lerp are cheap CPU work and transfer. The core trick (replay one DL N times with patched matrices) moves the whole RSP and RDP cost to every field. Today one tick costs 15.8-24.7 ms RSP gfx (cited, bench README lines 125-126). Three fields per tick would need about 47-74 ms of RSP per 50 ms tick. The design does nothing for RSP or RDP load (inferred).
- Licenses. 2S2H is CC0-1.0. Shipwright has no LICENSE file and GitHub reports no license (all rights reserved by default). libultraship is MIT. The interpolation files carry no per-file license header.

### 1. How state is recorded

#### Hook points (2S2H)
- `OPEN_DISPS` / `CLOSE_DISPS` open and close a tree child keyed by `(__FILE__, __LINE__)`. [S/mm/include/gfx.h#L138-L165](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/include/gfx.h#L138-L165). MM code has 1000 `OPEN_DISPS` lines under `mm/src` (measured, `grep -rw OPEN_DISPS --include=*.c src | wc -l`).
- Explicit `FrameInterpolation_RecordOpenChild(ptr, int)` / `RecordCloseChild` give identity to things drawn from one call site in a loop (actor, effect slot, grass blade). 107 open/close pairs in total (measured), listed by category in section 2.
- `sys_matrix.c` records every stack op: Push, Pop, Put, Mult, Translate, Scale, RotateX/Y/Z, RotateZYX, TranslateRotateZYX, SetTranslateRotateYXZ, MtxFToMtx, ToMtx, ReplaceRotation, RotateAxis. 16 call sites at [S/mm/src/code/sys_matrix.c#L106](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/sys_matrix.c#L106) through `#L1688`. `Matrix_ToMtx` records at [S/mm/src/code/sys_matrix.c#L1202-L1203](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/sys_matrix.c#L1202-L1203). Skin matrices record at [S/mm/src/code/z_skin_matrix.c#L515](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_skin_matrix.c#L515).
- `guLookAt`, `guPerspective` and `guOrtho` are re-implemented to go through `Matrix_MtxFToMtx`, so view and projection matrices are recorded. [S/mm/src/code/stubs.c#L686-L692](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/stubs.c#L686-L692), `#L731-L735`, `#L797-L802`.
- `Play_Main` brackets the draw with `FrameInterpolation_StartRecord()` and `StopRecord()`. [S/mm/src/code/z_play.c#L1640-L1642](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_play.c#L1640-L1642). Title and file select do the same.

#### Data structures
[S/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L51-L171](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L51-L171).
- `enum class Op` has 17 op kinds. `union Data` holds each op's arguments. A `MatrixToMtx` record holds the `Mtx*` destination plus two `MtxF` (actor-relative `src` and absolute `raw`), so about 136 bytes before container overhead (inferred from the struct).
- `struct Path { map<label, vector<Path>> children; map<Op, vector<Data>> ops; vector<pair<Op,size_t>> items; }`. `label = pair<const void*, int>`.
- Two `Recording`s are kept, `previous_recording` and `current_recording`. `StartRecord` moves current into previous. `#L453-L471`.
- Matching between ticks is by label, then by occurrence index of that label under the same parent, then by op kind and per-kind index. `interpolate_branch`, `#L275-L434`. A child with no match in the old tree is paired with itself, so it draws at the new value (snap). `#L281-L290`.

#### Producing an interpolated frame
- `FrameInterpolation_Interpolate(step)` walks the tree. It replays the stack ops on the CPU matrix stack with lerped arguments. It lerps final matrices elementwise for `MtxFToMtx` and `ToMtx` and stores them in `mtx_replacements[dest]`. `#L439-L445`.
- Two lerp modes exist. The header comment states only the second is used: lerp the final matrix, but for actors first remove the actor's own translate-rotate (recorded via `RecordActorPosRotMatrix` before `Matrix_SetTranslateRotateYXZ` in `Actor_Draw`), lerp in model space, then re-apply a matrix built from the lerped position and angles. This avoids the "paper effect" on ~180 degree turns. `#L13-L43`, `#L584-L618`, [S/mm/src/code/z_actor.c#L2925-L2960](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L2925-L2960).
- Angle lerp snaps to the new angle when the change exceeds 0x4000 (90 degrees), or 0x5700 (about 122 degrees) with the wider-angle flag. `#L234-L267`.
- The host frame loop. `Graph_ProcessGfxCommands` computes how many displayed frames this tick gets. `RunCommands` loops `count` times with t = time/denom. It passes an empty map when t == 1. [S/mm/2s2h/BenPort.cpp#L1170-L1250](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1170-L1250). At 60 fps with `R_UPDATE_RATE` 3, t = 1/3, 2/3, 1 (inferred by tracing the integer arithmetic).
- libultraship side. `Interpreter::GfxSpMatrix` looks the DL's matrix address up in the map and quantizes the replacement to 1/65536. [L/src/fast/interpreter.cpp#L1485-L1497](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/src/fast/interpreter.cpp#L1485-L1497). Two extra custom opcodes are also t-aware. `G_SETTILESIZE_LERP` lerps tile coordinates by `mInterpolationT` ([L/src/fast/interpreter.cpp#L4320-L4342](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/src/fast/interpreter.cpp#L4320-L4342), macro at [L/include/libultraship/libultra/gbi.h#L3280-L3300](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/include/libultraship/libultra/gbi.h#L3280-L3300)). `G_MW_SEGMENT_INTERP` lets a DL carry one segment address per displayed-frame index ([L/src/fast/interpreter.cpp#L2393-L2399](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/src/fast/interpreter.cpp#L2393-L2399)).

#### Shipwright differences
Same core file ([W/soh/soh/frame_interpolation.cpp](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/frame_interpolation.cpp), 912 lines vs 704). Additions:
- `FrameInterpolation_DontInterpolateChild(ptr)` tags an object for one gameplay frame via `ObjectExtension`. Used on every actor spawn ([W/soh/src/code/z_actor.c#L3416](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/src/code/z_actor.c#L3416)), on Link's big yaw-from-animation turns ([W/soh/src/overlays/actors/ovl_player_actor/z_player.c#L1612-L1615](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/src/overlays/actors/ovl_player_actor/z_player.c#L1612-L1615)), and Phantom Ganon. Definition [W/soh/soh/frame_interpolation.cpp#L198-L208](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/frame_interpolation.cpp#L198-L208), `#L557-L562`.
- Vertex blending for CPU-written geometry, which matrix lerp cannot reach. Skinned limbs (Epona) keep two poses and blend them into the game's vertex buffer before each displayed frame. Ribbon trails (sword, arrows, boomerang) slide only the newest end. [W/soh/soh/frame_interpolation.cpp#L226-L248](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/frame_interpolation.cpp#L226-L248), `#L688-L778`. Driven from [W/soh/soh/OTRGlobals.cpp#L1808-L1820](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/OTRGlobals.cpp#L1808-L1820). Call sites [W/soh/src/code/z_skin.c#L130](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/src/code/z_skin.c#L130) and four in [W/soh/src/code/z_eff_blure.c](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/src/code/z_eff_blure.c).
- 2S2H has an open port of the vertex blending, PR #1986 (https://github.com/2ship2harkinian/2ship2harkinian/pull/1986, state open on 2026-10-25). Its body states: "Matrix interpolation covers neither of these, because both are vertex blocks written by the CPU with no recorded matrix".

### 2. How they decide not to interpolate (2S2H, MM code)

Counts are measured with grep on the clone. "Call sites" means source lines.

| Category | Mechanism | Call sites | Representative permalink |
|---|---|---|---|
| Camera cut, whole frame | `AfterCameraUpdate` hook computes expected eye, yaw, pitch from last velocity. Yaw jump 90-270 deg, pitch > 60 deg or eye > 200 units off prediction sets `ShouldInterpolateFrame(false)`. Battle and Parallel camera funcs are exempt. Next `StartRecord` then skips recording, so every node snaps. | 1 hook | [S/mm/2s2h/Enhancements/Camera/CameraInterpolationFixes.cpp#L33-L107](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/Camera/CameraInterpolationFixes.cpp#L33-L107), effect at [S/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L462-L467](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L462-L467) |
| Camera cut, view only | Heuristics on eye, at, up and direction deltas in `View_ApplyPerspective` bump the camera epoch. Skipped while the pause BG prerender is active. | 1 | [S/mm/src/code/z_view.c#L387-L459](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_view.c#L387-L459) |
| Camera cut, scripted | Door camera jump (`Camera_Special9`) calls `DontInterpolateCamera`. | 1 | [S/mm/src/code/z_camera.c#L6880-L6883](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_camera.c#L6880-L6883) |
| Camera-keyed nodes | View matrix and skybox draw are children keyed `(NULL, cameraEpoch)`, so an epoch bump makes them unmatched. | 2 | [S/mm/src/code/z_vr_box_draw.c#L24-L27](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_vr_box_draw.c#L24-L27) |
| Skybox stars | Stars are texrects, which cannot be lerped. When fps != 20 the port draws each star as a quad through the projection matrix instead. This changes the DL. | 1 branch | [S/mm/src/code/z_kankyo.c#L3394-L3401](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_kankyo.c#L3394-L3401), helper `#L3205-L3215` |
| Actor identity | `Actor_Draw` opens child `(actor, 0)` and records the actor pos-rot matrix. No spawn guard: a new actor at a freed actor's address would match the old one (inferred; 2S2H has no equivalent of Shipwright's spawn call, grep found none). | 1 + 2 lock-on reticle | [S/mm/src/code/z_actor.c#L2925-L2960](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L2925-L2960), reticles `#L550`, `#L647` |
| Actor-relative lerp opt-out | `IgnoreActorMtx` stores matrices absolute instead of actor-relative. Used for non-rotating shadows, foot shadows, bunny hood, item00 moving to over-head, several boss effects. | 19 (src 16, enhancements 3) | [S/mm/src/code/z_actor.c#L140-L145](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L140-L145), `#L193-L194`, [S/mm/src/code/z_player_lib.c#L3464-L3466](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_player_lib.c#L3464-L3466) |
| Wide turns | Deku spin and Deku flower entry raise the angle snap limit to about 122 deg. | 1 | [S/mm/src/code/z_actor.c#L2942-L2949](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L2942-L2949) |
| Teleport or reset of a specific actor | Child key changes on reset so the node is unmatched (boulder reset, item on player, chu revive). | 3 | [S/mm/src/overlays/actors/ovl_En_Goroiwa/z_en_goroiwa.c#L1615-L1629](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Goroiwa/z_en_goroiwa.c#L1615-L1629), [S/mm/src/code/z_en_item00.c#L713-L718](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_en_item00.c#L713-L718) |
| Particles / effects, slot reuse | An `epoch` counter is added to effect structs and bumped on spawn or on position wrap, and used in the child key. Soft sprites (`EffectSs`), Object_Kankyo, Demo_Kankyo, Boss_02, Tanron5. | 28 epoch writes, 9 keyed opens | [S/mm/src/code/z_effect_soft_sprite.c#L160-L170](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_effect_soft_sprite.c#L160-L170), `#L218`, `#L259-L266`, [S/mm/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L282-L288](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L282-L288) |
| Particles / effects, per-element identity | Actor-local effect arrays keyed `(element ptr, index or type)`. Grass, Hugebombiwa, water effect, bosses, Death, Clear_Tag, Bigslime, Eff_Dust, footmarks, lights, sparks, shield particles. | 77 opens in 32 actor files, 19 in 11 `src/code` files | [S/mm/src/overlays/actors/ovl_En_Clear_Tag/z_en_clear_tag.c#L819](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Clear_Tag/z_en_clear_tag.c#L819), [S/mm/src/overlays/actors/ovl_Obj_Grass/z_obj_grass.c#L492](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Obj_Grass/z_obj_grass.c#L492) |
| Swept trails (`EffectBlure`) | Keyed by an epoch incremented every call, so never matched. Trails never interpolate. | 1 | [S/mm/src/code/z_eff_blure.c#L717-L722](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_eff_blure.c#L717-L722) |
| Body-part effects (fire, ice on enemies) | Keyed `(bodyPartsPos, type)`. | 5 | [S/mm/src/code/z_actor.c#L5421](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L5421) |
| Scrolling textures | `Gfx_TexScrollEx` / `Gfx_TwoTexScrollEx` emit `G_SETTILESIZE_LERP` from x to x + step. Callers were edited to pass a per-tick step. 106 call lines in 72 files. | 106 | [S/mm/src/code/z_rcp.c#L1378-L1430](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_rcp.c#L1378-L1430), example [S/mm/src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c#L127-L129](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c#L127-L129) |
| UI / HUD / textbox | No explicit hazard calls in `z_parameter.c` or `z_message*.c` (grep found none). Their matrices come from `guOrtho` under a shared `("ortho", 0)` child. Texrect-drawn HUD elements are not matrices and are drawn with current-tick coordinates on every displayed frame (inferred). | 1 (`guOrtho`) | [S/mm/src/code/stubs.c#L686-L692](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/stubs.c#L686-L692) |
| Pause menu (kaleido) | Child keyed by pause state + page index so page turns snap. Cursor keyed by a moved flag so cursor jumps snap. | 2 | [S/mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c#L662](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c#L662), `#L3049-L3068` |
| File select, title | Own Start/Stop record pair. Config mode in the key. | 1 + 2 pairs | [S/mm/src/overlays/gamestates/ovl_file_choose/z_file_choose_NES.c#L1892](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/gamestates/ovl_file_choose/z_file_choose_NES.c#L1892), `#L2468-L2471` |
| Motion blur (framebuffer feedback) | Copy to the blur buffer once per game tick unless a "MotionBlur.Interpolate" CVar is set. | 1 | [S/mm/src/code/z_play.c#L124-L129](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_play.c#L124-L129) |
| Scene transitions | No explicit handling found (grep for interpolation calls in transition code returned nothing). Covered only by the camera heuristics and unmatched nodes (inferred). | 0 | not found |
| Crash guards | Some early returns had to call the close-disps helpers or the tree goes unbalanced. | 3 comments | [S/mm/src/code/z_player_lib.c#L3180](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_player_lib.c#L3180), [S/mm/src/code/speed_meter.c#L126](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/speed_meter.c#L126) |

#### MM interpolation hazards found by 2S2H

| Hazard | How 2S2H handles it | Code pointer |
|---|---|---|
| Hard camera cut (cutscene shot change, warp, respawn) | Prediction-error heuristic on camera; whole tick not recorded, all nodes snap | [S/mm/2s2h/Enhancements/Camera/CameraInterpolationFixes.cpp#L85-L101](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/Camera/CameraInterpolationFixes.cpp#L85-L101) |
| Camera jump that only the view sees | Distance and direction thresholds bump camera epoch; view and skybox re-keyed | [S/mm/src/code/z_view.c#L416-L453](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_view.c#L416-L453) |
| Door camera switch | Explicit `DontInterpolateCamera` | [S/mm/src/code/z_camera.c#L6882](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_camera.c#L6882) |
| Skybox follows camera | Keyed on camera epoch | [S/mm/src/code/z_vr_box_draw.c#L26](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_vr_box_draw.c#L26) |
| Stars drawn as texrects | Replaced by projected quads when fps != 20 (changes output) | [S/mm/src/code/z_kankyo.c#L3396-L3400](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_kankyo.c#L3396-L3400) |
| Actor turns near 180 deg ("paper effect") | Actor-relative lerp; snap above 90 deg | [S/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L234-L267](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L234-L267) |
| Deku spin and flower entry | Wider angle limit (0x5700) | [S/mm/src/code/z_actor.c#L2944-L2949](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L2944-L2949) |
| Ground shadows and foot shadows wobble | `IgnoreActorMtx` | [S/mm/src/code/z_actor.c#L143-L144](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L143-L144), `#L193-L194` |
| Bunny hood ears | `IgnoreActorMtx` | [S/mm/src/code/z_player_lib.c#L3465-L3466](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_player_lib.c#L3465-L3466) |
| Item jumps from ground to over Link's head | Key toggles, `IgnoreActorMtx` | [S/mm/src/code/z_en_item00.c#L713-L718](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_en_item00.c#L713-L718) |
| Rolling boulder reset to home | Key toggles on reset | [S/mm/src/overlays/actors/ovl_En_Goroiwa/z_en_goroiwa.c#L1615-L1629](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Goroiwa/z_en_goroiwa.c#L1615-L1629) |
| Chuchu revive | Keyed by revive func, `IgnoreActorMtx` | [S/mm/src/overlays/actors/ovl_En_Slime/z_en_slime.c#L1245-L1246](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Slime/z_en_slime.c#L1245-L1246) |
| Soft-sprite effect slot reused | Slot epoch in key | [S/mm/src/code/z_effect_soft_sprite.c#L165-L169](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_effect_soft_sprite.c#L165-L169), `#L263` |
| Weather / ambient particles wrap position | Epoch bumped on wrap | [S/mm/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L282-L316](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Object_Kankyo/z_object_kankyo.c#L282-L316) |
| Twinmold sand effects | Epoch and type in key, `IgnoreActorMtx` | [S/mm/src/overlays/actors/ovl_Boss_02/z_boss_02.c#L1573-L1575](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Boss_02/z_boss_02.c#L1573-L1575) |
| Gyorg effects | `IgnoreActorMtx` | [S/mm/src/overlays/actors/ovl_Boss_03/z_boss_03.c#L2428-L2429](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Boss_03/z_boss_03.c#L2428-L2429) |
| Sword and weapon trails | Never interpolated (fresh epoch per call) | [S/mm/src/code/z_eff_blure.c#L721-L722](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_eff_blure.c#L721-L722) |
| Grass culling changes draw order | Per-blade key | [S/mm/src/overlays/actors/ovl_Obj_Grass/z_obj_grass.c#L492](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Obj_Grass/z_obj_grass.c#L492) |
| Treasure chest game walls | Per-cell key | [S/mm/src/overlays/actors/ovl_Obj_Takaraya_Wall/z_obj_takaraya_wall.c#L471](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_Obj_Takaraya_Wall/z_obj_takaraya_wall.c#L471) |
| Shooting gallery Deku scrubs | Per-index key, `IgnoreActorMtx` | [S/mm/src/overlays/actors/ovl_En_Syateki_Dekunuts/z_en_syateki_dekunuts.c#L487-L488](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/actors/ovl_En_Syateki_Dekunuts/z_en_syateki_dekunuts.c#L487-L488) |
| Lock-on reticles and arrow hover | Keyed by target actor | [S/mm/src/code/z_actor.c#L550](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_actor.c#L550), `#L647` |
| Pause page turn and cursor jump | Key from state and page, cursor moved flag | [S/mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c#L662](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c#L662), `#L3068` |
| Scrolling textures | Lerped tile-size opcode with a per-tick step at every caller | [S/mm/src/code/z_rcp.c#L1378-L1430](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_rcp.c#L1378-L1430) |
| Scroll step not linear (falling rocks in ISTT) | Interpolation disabled on that actor; PR body says it may cause crashes | https://github.com/2ship2harkinian/2ship2harkinian/pull/1712 |
| Float-scrolled chimney smoke | Not interpolated (PR #1332 body) | https://github.com/2ship2harkinian/2ship2harkinian/pull/1332 |
| Motion blur feedback | Copy once per tick by default | [S/mm/src/code/z_play.c#L124-L129](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_play.c#L124-L129) |
| Epona skin, CPU vertex trails | Not handled in 2S2H main; open PR #1986 ports Shipwright's vertex blend | https://github.com/2ship2harkinian/2ship2harkinian/pull/1986 |
| Early return leaves tree unbalanced | Call close-disps helpers manually | [S/mm/src/code/z_player_lib.c#L3180](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_player_lib.c#L3180) |

2S2H PR titles show the long tail of per-actor fixes: about 30 interpolation PRs (Wart, Freezard, Goht, Twinmold, Gyorg, Igos, minislime, lava rocks, archery enemies, bubbles, and more). Source: GitHub search `repo:2ship2harkinian/2ship2harkinian is:pr interpolation in:title`, run 2026-10-25.

### 3. Cost

- No author statement with CPU or memory numbers was found. I searched the README files, Shipwright `docs/`, and interpolation PR bodies (#309 has an empty body).
- Shipwright PR #6901 "optimize frame interpolation memory" (merged 2026-07-12) states the earlier code generated "all matrices for game tick upfront". It changed to "interpolate per frame". https://github.com/HarbourMasters/Shipwright/pull/6901. 2S2H's driver also computes the map per displayed frame ([S/mm/2s2h/BenPort.cpp#L1189-L1193](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1189-L1193)).
- CPU per tick (inferred from code). Recording appends one `Data` per matrix op into `std::map` and `std::vector`, with heap allocation, plus a 4x4 inverse per actor (`invert_matrix`) and one 4x4 multiply per actor-relative `ToMtx`. Per displayed frame, a full tree walk with map lookups, replay of every stack op, a 16-element lerp per output matrix, and an extra 4x4 multiply for actor-relative ones. On the host, the interpreter then re-runs the whole DL N times. MM code has 852 `MATRIX_FINALIZE` / `Matrix_NewMtx` / `Matrix_ToMtx` source lines (measured grep); the per-tick runtime matrix count is not measured.
- Memory (inferred). Two recordings live at once. A `ToMtx` record is about 136 bytes of payload plus container overhead. The replacement map holds one 64-byte `MtxF` per interpolated matrix.

#### What transfers to N64 (inferred unless cited)
- The DL is built once per tick and references each matrix by address. On N64 the same DL could be resubmitted per field with the CPU writing lerped fixed-point `Mtx` (64 bytes each) into those addresses between RSP tasks. That avoids rebuilding the DL. It needs either a second matrix buffer the DL can be pointed at (for example by segment) or a strict wait for the previous RSP task, because the RSP reads matrices while it runs.
- The RSP and RDP cost does not drop. Each field runs the full gfx task. The bench README (`tools/bench/README.md` lines 125-126, read this session) lists 15.8-24.7 ms RSP gfx and 26.1-34.2 ms gfx task start to full sync per tick (fork, not hardware-calibrated). Three fields per tick at that cost do not fit in 50 ms. The RSP and RDP savings in appendices C and D would have to land first.
- The recorder is C++ STL (maps, vectors, heap). An N64 version would need a fixed-size arena and flat arrays. The 64-bit float lerp on VR4300 is cheap per matrix, but replaying the matrix stack ops and the tree matching on 4 MB-ish budgets is unmeasured.
- Non-matrix motion needs DL patching. The ports added custom opcodes (`G_SETTILESIZE_LERP`, `G_MW_SEGMENT_INTERP`) that the real F3DZEX2 microcode does not have. On N64 these would become CPU-side rewrites of `G_SETTILESIZE` words or segment tables per field, or a microcode change.
- CPU-written vertices (Epona skin, blure trails, Bigslime, `EnRaf`) need per-field vertex blending into a second buffer, as Shipwright does. On N64 that is CPU and RDRAM bandwidth per field.
- Texrect UI and stars cannot be lerped by matrix replacement. 2S2H changes the star draw path, which is a fidelity change under our rule.

### 4. Are keyframes identical to the non-interpolated render?

- Matrices: yes. When t == 1 the driver passes an empty replacement map ([S/mm/2s2h/BenPort.cpp#L1191-L1192](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1191-L1192)), so the interpreter decodes matrices from the DL as is ([L/src/fast/interpreter.cpp#L1488-L1497](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/src/fast/interpreter.cpp#L1488-L1497)). The keyframe is the last displayed frame of the tick, as in our F1 rule. Interpolated frames lie between the previous and the current tick, so presentation lags by up to one tick (inferred).
- Stars: no, when fps != 20. The DL itself differs ([S/mm/src/code/z_kankyo.c#L3398-L3401](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/code/z_kankyo.c#L3398-L3401)).
- Scrolling textures: no, at any fps. `Gfx_TwoTexScrollEx` lerps from x to x + step. The interpreter evaluates `coords[0] + t * (coords[4] - coords[0])`, so t = 1 gives x + step, one tick ahead of the retail `Gfx_TwoTexScroll(..., x, ...)` call (compare [S/mm/src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c#L127-L129](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c#L127-L129) with the local decomp `src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c:129`). `mInterpolationT` is 1 at 20 fps too ([S/mm/2s2h/BenPort.cpp#L1193](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1193)). This is inferred from code, not run. Shipwright PR #6224 lists a Deku Tree shimmer where the texture "keeps trying to interpolate to the next frame", which is consistent. https://github.com/HarbourMasters/Shipwright/pull/6224
- Motion blur: with the default CVar the copy happens once per tick. Whether that copy is taken on the keyframe pass or the first pass was not verified (`FB_CopyToFramebuffer` body not read).
- Lesson for our program (inferred). Lerp from the previous tick to the current one, never toward a predicted next value, and leave the keyframe DL byte-identical to retail.

### 5. Licenses

| Item | License | Source |
|---|---|---|
| 2S2H repo | CC0-1.0 | [S/LICENSE](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/LICENSE) (read), GitHub API `license.spdx_id = CC0-1.0` |
| 2S2H `FrameInterpolation.cpp/.h` | No header; repo license applies (CC0-1.0) | [S/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L1-L12](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp#L1-L12) |
| Shipwright repo | None. No root LICENSE file; GitHub API returns `license: null`. Only `soh/soh/Enhancements/randomizer/3drando/LICENSE.md` exists | `find` on clone, `gh api repos/HarbourMasters/Shipwright` |
| Shipwright `frame_interpolation.cpp/.h` | No header, no repo license. Treat as all rights reserved. The 2S2H copy is CC0, but the code originates in Shipwright PR #309 by Emill; provenance of the CC0 grant for that code is unverified | [W/soh/soh/frame_interpolation.cpp#L1-L41](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/frame_interpolation.cpp#L1-L41), https://github.com/HarbourMasters/Shipwright/pull/309 |
| libultraship | MIT, (c) 2022 kenix3 | [L/LICENSE](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/LICENSE) |

### Techniques

| Technique | Resource | Expected gain | Fidelity risk | Effort | License if reused | Sources |
|---|---|---|---|---|---|---|
| Record matrix ops during the tick's draw; per field, lerp matrices and patch `Mtx` buffers, resubmit the same DL | CPU (avoids rebuilding DL per field) | Saves the 2 extra Play_Draw runs per tick (inferred). Does not cut RSP or RDP | Identical on keyframes if keyframe uses unpatched DL; in-between fields new by design | High (C-only arena recorder, matrix double-buffer, sync with RSP) | Design is free to reimplement; 2S2H code CC0; Shipwright code unlicensed | [S/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/Enhancements/FrameInterpolation/FrameInterpolation.cpp), [S/mm/2s2h/BenPort.cpp#L1170-L1250](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/2s2h/BenPort.cpp#L1170-L1250) |
| Tree keyed by (OPEN_DISPS site, explicit ptr+int labels) with unmatched-node snap | CPU | Correct pairing across ticks | Identical keyframes; mismatches only affect in-between fields | Medium | as above | [S/mm/include/gfx.h#L138-L165](https://github.com/2ship2harkinian/2ship2harkinian/blob/b30a286c55bd94f0868e6a103d0e4c511f7003a9/mm/include/gfx.h#L138-L165) |
| Actor-relative lerp (invert actor pos-rot, lerp, reapply) | CPU | Fixes ~180 deg "paper" turns | In-between only | Medium | as above | `FrameInterpolation.cpp#L584-L618` |
| Camera cut heuristics plus camera epoch | CPU | Avoids smeared cuts | In-between only | Low | as above | `z_view.c#L387-L459`, `CameraInterpolationFixes.cpp` |
| Per-slot epoch counters for effects | CPU, small RAM | Avoids lerp across slot reuse | In-between only; adds fields to structs (RAM layout change, not visual) | Medium, many files (28 writes) | as above | `z_effect_soft_sprite.c#L160-L170` |
| Vertex blending for CPU-skinned limbs and trails (Shipwright) | CPU, RDRAM bw | Smooth Epona and trails | In-between only if keyframe copies current pose | Medium | Shipwright unlicensed; open 2S2H PR #1986 | [W/soh/soh/frame_interpolation.cpp#L226-L778](https://github.com/HarbourMasters/Shipwright/blob/2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41/soh/soh/frame_interpolation.cpp#L226-L778) |
| Lerped tile-size scroll (`G_SETTILESIZE_LERP`) | CPU (DL patch per field on N64) | Smooth scrolls | Differs as implemented (keyframe one tick ahead); must lerp prev to current instead | Medium, 106 call lines | libultraship MIT, 2S2H CC0 | `z_rcp.c#L1378-L1430`, [L/src/fast/interpreter.cpp#L4320-L4342](https://github.com/Kenix3/libultraship/blob/7f9b86a593c526fc42261d7fe197100cecf57178/src/fast/interpreter.cpp#L4320-L4342) |
| Alternate star draw via projected quads | RDP different path | Smooth stars | Differs (changes DL) | Low | CC0 | `z_kankyo.c#L3396-L3401` |

### Sources fetched

- https://github.com/HarbourMasters/2ship2harkinian.git (clone, redirects to 2ship2harkinian/2ship2harkinian), HEAD b30a286c55bd94f0868e6a103d0e4c511f7003a9, 2026-10-25.
- https://github.com/HarbourMasters/Shipwright.git (clone), HEAD 2827fc2a0f56a9cdc18a87adf8c1bb7e91315f41, 2026-10-25.
- https://github.com/HarbourMasters/libultraship.git (clone, Kenix3/libultraship), checked out 7f9b86a593c526fc42261d7fe197100cecf57178, 2026-10-25.
- GitHub API: `repos/HarbourMasters/Shipwright` (license null), `repos/HarbourMasters/2ship2harkinian` (CC0-1.0), `repos/Kenix3/libultraship`, commit lookups for all three SHAs, 2026-10-25.
- https://github.com/HarbourMasters/Shipwright/pull/309, /pull/6901, /pull/6224 (via API), 2026-10-25.
- https://github.com/2ship2harkinian/2ship2harkinian/pull/1986, /pull/1076, /pull/1332, /pull/479, /pull/1712 (via API), 2026-10-25.
- GitHub search API for interpolation PRs in both repos, 2026-10-25.
- Local: src/overlays/effects/ovl_Effect_Ss_Fire_Tail/z_eff_ss_fire_tail.c (retail call for comparison).

### Gaps

- No runtime measurement. Per-tick matrix count, recording size, and interpolation CPU time in 2S2H are not measured. No author numbers found.
- Shipwright's pinned libultraship (da6daf1a) was not read. I assumed its interpreter matches the 2S2H pin for these hooks.
- `FB_CopyToFramebuffer` (motion blur copy timing) not read, so which pass takes the copy is unknown.
- Scrolling-texture keyframe offset is inferred from code. It was not confirmed by running 2S2H.
- Scene transitions and `TransitionTile` / fades: no explicit handling found; whether they cause artifacts was not checked.
- Provenance of the CC0 grant for code that started in unlicensed Shipwright is unverified. Ask a human before reusing either file verbatim.

## Appendix B. Zelda64Recomp and RT64 matrix-group interpolation

### Summary

Zelda64Recomp runs MM's game logic at its original rate. RT64, the renderer, captures each tick's display list on the host and makes the extra frames itself. It replays the same draw calls with interpolated matrices on the host GPU. The game side only marks matrices with extended-GBI commands (`gEXMatrixGroup*`) that carry an ID and a per-component interpolate/skip choice. The C patch set against the decomp holds the MM knowledge: what to tag, how to give objects stable IDs, and when to skip interpolation (camera cuts, teleports, respawned particles, cursor jumps). That knowledge carries over to an N64 build. The cost model does not. On N64 nothing replays the list for free. The RSP and RDP must run the whole display list again for every field, and the CPU must produce the interpolated matrices. The RT64 design has two traits that suit a pixel-exact program. A "skip" component, and any unmatched transform, renders the current tick's matrix unchanged. The weight-1.0 display frame is the tick's own state. Zelda64Recomp's output is not pixel-identical to retail even at the original framerate. Several patches change the render path (stars, textbox, letterbox, glow, motion-blur alpha clamp, kaleido pages), and RT64 is a GPU renderer, not an RDP-exact one.

Clones (fetched 2026-10-25):
- Zelda64Recomp at `b65c482ed672258eb684ab94a4e8c8aa662615ee` (commit date 2026-09-25). Base for links below: `Z = https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee`.
- RT64 at `23cab603c4f9f4a8b369b38e036f1aa484603878` (2025-08-25). This is the submodule commit Zelda64Recomp pins (`git ls-tree HEAD lib/` in the Zelda64Recomp clone). Base: `R = https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878`. RT64 HEAD `d9817c68ffd7328cd0af383af124971489366e18` (2026-10-23) was also cloned. It adds float-matrix commands, vertex-velocity segments, look-at matching and mirrored-transform biasing (from `git diff --stat` and `git diff` between the two SHAs). All line numbers below are at the pinned commit.
- N64Recomp at `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` (2026-05-27). License only.

Links labeled Z/ and R/ use the bases above.

### 1. RT64 extended GBI (gEX*)

#### Activation and encoding
- The enable hook is a `G_SPNOOP` (0xE0 on F3DEX2) carrying magic 0x525464 ("RT"+0x64). It registers an extended opcode, 0x64 by default. [R/include/rt64_extended_gbi.h#L8-L27](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L8-L27), `#L192-L195`. RT64 parses the hook in [R/src/gbi/rt64_gbi_extended.cpp#L299-L339](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/gbi/rt64_gbi_extended.cpp#L299-L339).
- RT64's README says the activation "is encoded into a NOOP command so it should be safe to run it in real hardware" (cited, [R/README.md#L86](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/README.md#L86)). After activation, opcode 0x64 commands are not valid F3DEX2 commands. This only matters if a ROM emits them, and our program has no consumer for them anyway (inferred).
- Zelda64Recomp emits `gEXEnable` and `gEXSetRDRAMExtended` at the start of every gfx pool. [Z/patches/ui_patches.c#L59-L63](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L59-L63).
- Command list at the pinned commit: 0x00 to 0x2D. These cover texrect, fillrect, viewport, scissor and alignment for widescreen, `SETREFRESHRATE`, `VERTEXZTEST`, `MATRIXGROUP`/`POPMATRIXGROUP`, `EDITGROUPBYADDRESS`, `VERTEX` (with previous positions), push/pop of viewport, scissor, othermode, combine, projection, colors and geometry mode, plus dither-noise strength, extended RDRAM and near clipping. [R/include/rt64_extended_gbi.h#L29-L77](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L29-L77).

#### gEXMatrixGroup fields
`gEXMatrixGroup(cmd, id, mode, push, proj, pos, rot, scale, skew, persp, vert, tile, order, edit)` is 2 Gfx words (16 bytes). [R/include/rt64_extended_gbi.h#L300-L306](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L300-L306). The decoder is [R/src/gbi/rt64_gbi_extended.cpp#L132-L148](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/gbi/rt64_gbi_extended.cpp#L132-L148).
- `id` is 32 bits. `G_EX_ID_IGNORE = 0` never matches. `G_EX_ID_AUTO = 0xFFFFFFFF` uses heuristic matching. Anything else is an explicit ID. [R/include/rt64_extended_gbi.h#L87-L88](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L87-L88).
- `mode`: `G_EX_INTERPOLATE_SIMPLE` (component-wise matrix lerp) or `G_EX_INTERPOLATE_DECOMPOSE` (decompose into translation, rotation, scale, skew and perspective, interpolate each, then recompose). `#L94-L95`.
- Per-component 2-bit setting for pos, rot, scale, skew, persp, vert (per-vertex) and tile (texture scroll): `SKIP = 0`, `INTERPOLATE = 1`, `AUTO = 2`. `#L90-L92`. The default group has vertex = SKIP and all others AUTO. [R/src/hle/rt64_transform_group.h#L12-L24](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_transform_group.h#L12-L24).
- `order`: `G_EX_ORDER_LINEAR` (match by ID in submission order) or `G_EX_ORDER_AUTO` (an ID used only as a filter for heuristic matching). [R/include/rt64_extended_gbi.h#L97-L98](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L97-L98).
- `edit`: `G_EX_EDIT_ALLOW` lets a later `gEXEditGroupByAddress` override the group. `#L100-L101`.
- `push` pushes onto RT64's own group stack (separate stacks for modelview and projection). `gEXPopMatrixGroup[N]` pops. [R/src/hle/rt64_rsp.cpp#L1126-L1155](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rsp.cpp#L1126-L1155).
- `gEXEditGroupByAddress(addr, ...)` finds every transform recorded this frame from the `Mtx` at that RDRAM address and rewrites its group's settings, only if the group allowed edits. [R/src/hle/rt64_rsp.cpp#L1108-L1124](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rsp.cpp#L1108-L1124). Possible quirk, inferred from code and not run: the macro writes the edit bit at bit 18 ([R/include/rt64_extended_gbi.h#L363](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/include/rt64_extended_gbi.h#L363)). The decoder reads `order` as 2 bits at 17 and `edit` at bit 19 ([R/src/gbi/rt64_gbi_extended.cpp#L145-L146](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/gbi/rt64_gbi_extended.cpp#L145-L146)). So an edit sets `ordering` to 2 or 3, which is neither LINEAR nor AUTO.
- Groups attach to transforms lazily. A changed group stack is recorded only when vertices are next loaded under a new model matrix ([R/src/hle/rt64_rsp.cpp#L427-L468](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rsp.cpp#L427-L468)), or when a projection is added (`#L394-L419`). Every recorded world transform stores a group index and its source `Mtx` physical address.

### 2. How RT64 matches and interpolates

Code path: [R/src/hle/rt64_game_frame.cpp](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_game_frame.cpp).

1. **Workloads** are paired by index. The code assumes the same order every frame. `#L262-L270`.
2. **Explicit IDs, linear order.** `buildTransformIdMap` puts every transform whose group has an explicit ID and `ORDER_LINEAR` into a `std::multimap<id, transformIndex>`. IGNORE goes to an ignore list. AUTO is left for the heuristic pass. `#L722-L740`. The current and previous maps are walked in lockstep by key, and equal keys are paired. `#L294-L310`.
   - **ID collisions.** Equal keys in a `std::multimap` keep insertion order, so the k-th transform with ID X this frame pairs with the k-th transform with ID X last frame. If the counts differ, the extras stay unmatched (inferred from `#L298-L310`). One ID can therefore cover several matrices only if they come in the same order every frame.
   - **Missing group.** A transform with no partner stays `mapped = false`. The transform processor then draws it with the current matrix on every interpolated frame. [R/src/render/rt64_transform_processor.cpp#L41-L60](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/render/rt64_transform_processor.cpp#L41-L60).
   - **IGNORE** is forced unmapped and its rigid-body state is reset. `rt64_game_frame.cpp#L316-L322`.
3. **Scenes (projections)** are matched greedily by the smallest view+projection matrix difference, separately for perspective and ortho. `#L328-L370`. The camera (projection group) interpolates only if both frames carry the same explicit ID, or the ID is AUTO. Default camera mode is SIMPLE ("Cameras usually look better with simple interpolation"). `#L451-L484`.
4. **Heuristic (AUTO, or an ID with ORDER_AUTO).** Draw calls are hashed by color combiner, othermode, geometry mode, triangle count and the hash of their matrix IDs. `#L702-L720`, `#L742-L757`. Candidate transform pairs from calls with equal hashes are scored by position difference (plus last velocity), orientation difference and screen-space difference. Pairs whose 3x3 determinant changes sign (mirroring) are rejected. The pairs are sorted, then assigned greedily. `#L219-L253`, `#L506-L580`.
5. **Per-component decision.** `RigidBody::updateLinear/Angular/Perspective`. [R/src/hle/rt64_rigid_body.cpp#L19-L76](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rigid_body.cpp#L19-L76). INTERPOLATE interpolates. SKIP does not. AUTO for translation compares the new velocity to the old one (tolerance 5.0 units, ratio threshold 10). For rotation AUTO always interpolates ("FIXME: Defaults to always interpolate", `#L55-L56`).
6. **What "skip" renders.** `lerpMatrixComponents` starts from `b`, the current matrix, and lerps only the enabled parts. [R/src/common/rt64_math.cpp#L177-L198](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/common/rt64_math.cpp#L177-L198). So a skipped component snaps to the current tick's value on the first interpolated frame. No component ever holds the previous value.
7. **Vertex interpolation** applies when `vert != SKIP` and the vertex count is unchanged. Velocities come from the per-vertex position difference. `rt64_game_frame.cpp#L681-L699`.
8. **Texture scroll (tile) interpolation** applies when the tile format, size, masks, shifts and clamp modes match. Wrap-around is detected modulo the mask, so a wrapped scroll keeps its delta. `#L586-L656`.
9. **Frame weights.** With a target rate above the game's rate, `displayFrames = (logicalTicks - displayTicks)/viOriginalRate`. Each frame gets `curFrameWeight = (target + displayTicks - logicalTicks)/target`, clamped to 0..1. [R/src/hle/rt64_workload_queue.cpp#L971-L985](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_workload_queue.cpp#L971-L985), `#L1053-L1056`. For 60 over 20 the weights are 1/3, 2/3, 1.0 (inferred from that arithmetic). The game reports its rate each frame with `gEXSetRefreshRate(60 / (framerateDivisor + extra_vis))`. [Z/patches/ui_patches.c#L99-L100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L99-L100). RT64 uses it in place of the VI-derived rate. [R/src/hle/rt64_state.cpp#L1562-L1567](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_state.cpp#L1562-L1567).

**When RT64 does not interpolate at all.** The workload is paused. The target rate is not above the original rate. No presented color framebuffer is an interpolation candidate. [R/src/hle/rt64_workload_queue.cpp#L958-L959](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_workload_queue.cpp#L958-L959). It also drops display frames when GPU time runs past the window. `#L1036-L1048`. RT64's README calls draw-call matching "highly experimental" and says it "requires a lot of processing to get right" (cited, [R/README.md#L80](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/README.md#L80)).

### 3. How Zelda64Recomp tags MM

- **ID space.** [Z/patches/transform_ids.h#L6-L43](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/transform_ids.h#L6-L43). Fixed IDs cover the camera (0x10), textbox, letterbox, pause cursor and info panel. Ranges cover skybox (0x100), EffectSs particles (0x200 + slot), stars (0x1000 + i), Effect sparks, blures, shield particles and tire marks (0x2000 + slot, opa/xlu offset by 0x800), and special effects (0x800 + i). Actors start at 0x1000000 with 512 IDs each, one per limb plus one per post-limb draw.
- **Actor identity.** `actor_transform_id = spawn_index * 512 + 0x1000000`. [Z/patches/actor_data.c#L48-L52](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c#L48-L52). `spawn_index` comes from a counter that only increments when an actor's extension data is allocated. [Z/src/game/recomp_actor_api.cpp#L148-L149](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/game/recomp_actor_api.cpp#L148-L149). A new actor that reuses freed memory still gets a new ID, so it can never pair with the old actor. The slot handle is stored in 4 bytes of compiler padding inside `Actor`. [Z/patches/actor_data.c#L6-L30](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c#L6-L30).
- **Ordering.** Every Zelda64Recomp tag uses `G_EX_ORDER_LINEAR` with an explicit or IGNORE ID. The only `ORDER_AUTO` macro is defined and never called (`grep G_EX_ID_AUTO|G_EX_ORDER_AUTO` over `patches/` finds only the definition at [Z/patches/patches.h#L68-L69](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/patches.h#L68-L69)). So Zelda64Recomp needs no heuristic matching for anything it tags. Untagged draws fall back to RT64's default AUTO group (inferred from [R/src/hle/rt64_transform_group.h#L13](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_transform_group.h#L13) and the absence of any base group push).
- **Macro presets.** [Z/patches/patches.h#L47-L69](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/patches.h#L47-L69). `DecomposedNormal` interpolates pos, rot, scale, skew and tile and skips persp and vert. `SkipAll` keeps only tile. `SkipRot`, `SkipPosRot`, `Verts` (adds per-vertex) and `NoInterpolation` (IGNORE ID) round out the set.

#### Counts (measured: grep over `patches/*.c` at the cloned SHA)

| file | RECOMP_PATCH fns | gEXMatrixGroup* calls | of which skip/ignore presets | gEXPopMatrixGroup | other gEX |
|---|---|---|---|---|---|
| actor_transform_tagging.c | 20 | 10 | 3 | 4 | 0 |
| specific_actor_transform_tagging.c | 24 | 32 | 5 | 26 | 0 |
| effect_transform_tagging.c | 6 | 19 | 3 | 13 | 0 |
| particle_transform_tagging.c | 8 | 14 | 6 | 7 | 0 |
| song_transform_tagging.c | 2 | 6 | 1 | 5 | 0 |
| terrain_transform_tagging.c | 3 | 5 | 2 | 5 | 0 |
| camera_transform_tagging.c | 4 | 4 | 0 (skip via explicit SKIP args) | 0 | 0 |
| sky_transform_tagging.c | 2 | 4 | 1 | 2 | 0 |
| ui_patches.c | 8 | 4 | 0 | 4 | 50 |
| ui_transform_tagging.c | 4 | 3 | 1 | 2 | 0 |
| item_transform_tagging.c | 3 | 3 | 0 | 3 | 0 |
| effect_patches.c | 4 | 2 | 0 | 2 | 5 |
| ocarina_effect_patches.c | 8 | 2 | 0 | 1 | 0 |
| billboard_tagging.c | 20 | 0 | 0 | 0 | 2 (EditGroupByAddress) |
| sword_trail_transform_tagging.c | 3 | 0 | 0 | 0 | 3 (gEXVertex) |
| culling.c | 4 | 0 | 0 | 0 | 2 (VertexZTest) |
| cutscene_patches.c + 3 gibdo_mask_cutscene_*.c | 8 | 0 | 0 | 0 | 0 (set actor skip flag) |

Totals: 108 `gEXMatrixGroup*` call sites. By preset they are DecomposedNormal 41, Simple 20, DecomposedSkipAll 19, DecomposedVerts 14, DecomposedSkipRot 7, DecomposedSkipPosRot 4, InterpolateOnlyTiles 2 and NoInterpolation 1. The "skip/ignore presets" column counts SkipAll, NoInterpolation and InterpolateOnlyTiles. Many SKIP choices are made at runtime through `if (skip) SkipAll else Normal` pairs, so this column undercounts skip logic. The actor skip flag `actor_set_interpolation_skipped` has 13 references across 6 files. "Category" counts below are hazard rows, one per distinct mechanism.

### 4. MM interpolation hazards found by Zelda64Recomp

| hazard | how the patch handles it | code pointer (permalink) |
|---|---|---|
| Camera cut or teleport | `View_Apply` tags the projection with `CAMERA_TRANSFORM_ID`, SIMPLE mode. A velocity predictor on eye/at decides interpolate or skip. The thresholds are 3/100/10/300/50 units, "picked via testing". The skip result is recorded for other systems to read. | [Z/patches/camera_transform_tagging.c#L153-L206](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_transform_tagging.c#L153-L206), `#L211-L268` |
| Camera heuristic wrong in specific places | Per-scene and per-setting overrides. Majora fight, Z-target modes and Pirates' Fortress moat are forced to interpolate. Stone Tower block puzzles and the post-Majora zoom are forced too. The Music Box House Gibdo-mask frame (cutscene frame 525) is forced to skip. `Camera_Fixed1` forces interpolation while approaching. | [Z/patches/camera_transform_tagging.c#L33-L92](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_transform_tagging.c#L33-L92), `#L385-L391` |
| Pause (kaleido) enter/exit, file select | Kaleido and file-select views force camera interpolation and bypass the tracker. The first view after leaving kaleido forces skip. | [Z/patches/camera_transform_tagging.c#L109-L151](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_transform_tagging.c#L109-L151), `#L229-L232` |
| Pause menu pages, cursor, info panel | All 4 pages are always drawn so the rotating cube interpolates. Vertices are allocated for all pages. The cursor skips interpolation when it moved (jump between slots). The info panel uses vertex interpolation. | [Z/patches/ui_transform_tagging.c#L57-L60](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_transform_tagging.c#L57-L60), `#L140-L197`, `#L221-L381`, `#L589-L600` |
| Skybox | Tagged `SKYBOX_TRANSFORM_ID_START`. Uses SkipAll when the camera skipped this frame. Also skips drawing skyboxes with null textures (perf). | [Z/patches/sky_transform_tagging.c#L44-L102](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L44-L102) |
| Stars (screen-space rects) | Rects cannot follow a matrix. They are redrawn as 3D billboarded quads with per-star IDs that skip on camera skip. The max star size is also scaled from 8+2 to 4+2. This changes the render path. | [Z/patches/sky_transform_tagging.c#L105-L290](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L105-L290) |
| Billboards | All `Matrix_*` stack ops are patched to track whether the current matrix holds `play->billboardMtxF` rotation. `Matrix_NewMtx` records billboarded `Mtx` addresses, up to 2048. At frame end, if the camera skipped, `gEXEditGroupByAddress` turns off rotation interpolation for those matrices. | [Z/patches/billboard_tagging.c#L36-L78](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/billboard_tagging.c#L36-L78), `#L122-L169`; caller [Z/patches/ui_patches.c#L102-L106](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L102-L106) |
| Terrain pieces swapping as rooms stream in | Rooms are tagged `InterpolateOnlyTiles` (IGNORE ID, tile only). Terrain moves only through the camera, and only texture scroll interpolates. | [Z/patches/terrain_transform_tagging.c#L10-L40](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/terrain_transform_tagging.c#L10-L40) |
| Texture scrolling | Handled by RT64 tile interpolation, with wrap detection. Enabled for nearly every preset (tile = INTERPOLATE). | [R/src/hle/rt64_game_frame.cpp#L586-L656](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_game_frame.cpp#L586-L656); [Z/patches/patches.h#L47-L69](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/patches.h#L47-L69) |
| Generic actors with one matrix | `Actor_Draw` reserves two `gDPNoOp` slots in OPA and XLU, then runs `actor->draw` and counts `G_MTX` commands it wrote. If each list has at most 1 and at least one exists, it back-fills the slot with the actor's ID tag and appends a pop. Actors with more matrices are left untagged. | [Z/patches/actor_transform_tagging.c#L1297-L1352](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L1297-L1352), `#L1355-L1437` |
| Skeleton limbs | All 12 `SkelAnime_Draw*` variants are patched. Each limb pushes ID `actor + limbIndex`, and post-limb draws push `actor + limbIndex + 256`. Each uses DecomposedNormal, or SkipAll if the actor's skip flag is set. | [Z/patches/actor_transform_tagging.c#L232-L284](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L232-L284), example `#L289-L345` |
| Skinned meshes (Skin) | Pushes an identity `Mtx` per limb so each limb gets its own group, with vertex interpolation (`DecomposedVerts`). | [Z/patches/actor_transform_tagging.c#L1239-L1295](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L1239-L1295) |
| Actor reuse of memory or slot | IDs come from a monotonic spawn counter, not the actor pointer or table slot. | [Z/patches/actor_data.c#L48-L52](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c#L48-L52); [Z/src/game/recomp_actor_api.cpp#L148-L149](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/game/recomp_actor_api.cpp#L148-L149) |
| Cutscene teleports (actor cues) | On a new cue whose start is more than `CUE_DIST_THRESHOLD` away, or on Epona or Link cue starts, sets the actor skip flag for one frame. `Actor_Draw` clears it. | [Z/patches/cutscene_patches.c#L9-L35](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/cutscene_patches.c#L9-L35), `#L41-L130`; clear at [Z/patches/actor_transform_tagging.c#L1434-L1435](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c#L1434-L1435) |
| Actor-specific discontinuities | Happy Mask Salesman animation changes, item pickup on collect, Goron boulders reset to home, and the Gibdo-mask cutscene actors each set the skip flag. | [Z/patches/specific_actor_transform_tagging.c#L692-L750](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/specific_actor_transform_tagging.c#L692-L750), `#L914-L952`, `#L1063-L1075`; [Z/patches/gibdo_mask_cutscene_father_human.c](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/gibdo_mask_cutscene_father_human.c) |
| EffectSs particles (slot reuse) | `EffectSS_ResetEntry` marks the slot reset. On the next draw the particle uses SkipAll, then normal. ID is `0x200 + slot`. | [Z/patches/particle_transform_tagging.c#L12-L40](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/particle_transform_tagging.c#L12-L40), `#L68-L100` |
| Actor-owned effect arrays | Same pattern with per-element IDs and reset flags set at spawn. Covers Gyorg effects, water effects, falling fire rocks, fireworks, Lost Woods sparkles, Goht rocks, Wart bubbles, torch moths, fire-eye flames and grass. | [Z/patches/specific_actor_transform_tagging.c#L31-L32](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/specific_actor_transform_tagging.c#L31-L32), `#L159-L330`, `#L1383-L1440`; [Z/patches/particle_transform_tagging.c#L104-L176](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/particle_transform_tagging.c#L104-L176), `#L388-L520` |
| Effect system (sparks, blures, shield, tire marks) | Sparks and shield particles interpolate by slot ID. Blures (sword trails) and tire marks use SKIP ("doesn't work well with it"). | [Z/patches/effect_transform_tagging.c#L300-L390](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_transform_tagging.c#L300-L390) |
| Sword trail geometry | Replaces `gSPVertex` with `gEXVertex` (VertexEX carries previous positions) so the trail tip interpolates per vertex. A newly spawned trail seeds previous positions. | [Z/patches/sword_trail_transform_tagging.c#L11-L140](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sword_trail_transform_tagging.c#L11-L140), `#L351-L480` |
| Environment particles wrapping around the camera | `ObjectKankyo` particles moved or wrapped this tick are marked skip. They also draw past the screen edge. | [Z/patches/effect_transform_tagging.c#L15-L156](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_transform_tagging.c#L15-L156), `#L158-L250` |
| Rain splashes | `gEXMatrixGroupNoInterpolation` (IGNORE). | [Z/patches/specific_actor_transform_tagging.c#L1037-L1061](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/specific_actor_transform_tagging.c#L1037-L1061) |
| Lens flares (sun, custom, soaring) | Whole effect tagged with order linear, SkipAll when the camera skipped. | [Z/patches/effect_transform_tagging.c#L696-L765](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_transform_tagging.c#L696-L765); [Z/patches/song_transform_tagging.c#L63-L73](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/song_transform_tagging.c#L63-L73) |
| Light glow needs depth readback | Replaces the CPU Z-buffer read, which is 1 frame stale, with `gEXVertexZTest`. Glow is enabled when projected z > 1. | [Z/patches/culling.c#L76-L150](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/culling.c#L76-L150) |
| Textbox (screen rects) | Textbox redrawn as ortho triangles under an interpolated projection and model group. Pushes and pops RDP/RSP state. | [Z/patches/ui_patches.c#L892-L1020](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L892-L1020) |
| Letterbox bars (scissor) | Scissor letterbox disabled. Bars drawn as ortho tris every frame so they interpolate from the first frame. | [Z/patches/ui_patches.c#L1029-L1035](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L1029-L1035), `#L1096-L1162` |
| HUD | No matrix groups in `Interface_Draw`. Its gEX use is rect, viewport and scissor alignment for widescreen. The 50 "other gEX" calls in ui_patches.c are mostly these. README: "most HUD elements" render at high framerate. That means redrawn each frame, not interpolated (inferred). | [Z/patches/ui_patches.c#L242](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L242); [Z/README.md#L60](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/README.md#L60) |
| Motion blur (framebuffer feedback) | Alpha is raised to the power `20/target_fps` so decay per second matches. It is clamped to 0.825 on 8-bit framebuffers. Dither noise is scaled with resolution. | [Z/patches/effect_patches.c#L54-L126](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L54-L126) |
| Transition circle and lens overlay (texrect) | Rebuilt as ortho quads with a scale matrix so they interpolate and cover widescreen. | [Z/patches/effect_patches.c#L141-L247](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L141-L247) |
| Ocarina wipes, Skull Kid curse | Custom vertices and vertex flags. EffStk uses SkipPosRot on camera skip, and edits are disallowed so billboard editing leaves it alone. | [Z/patches/ocarina_effect_patches.c#L1033-L1100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ocarina_effect_patches.c#L1033-L1100), `#L1446-L1484` |
| Held items (hookshot chain, reticle, bowstring) | Hookshot tip and chain tagged. Reticle tagged in OVERLAY. Bowstring DL hooked with tag slots. Player forced to closest LOD. | [Z/patches/item_transform_tagging.c#L19-L67](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/item_transform_tagging.c#L19-L67), `#L76-L117`, `#L146-L177` |
| Song of Soaring, three-day reset clocks | Wings and capsule tagged. Clocks use SkipRot per clock. | [Z/patches/song_transform_tagging.c#L18-L75](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/song_transform_tagging.c#L18-L75), `#L107-L171` |
| Intended stepped animation look | Beaver race rings and the intro-cutscene ground use SkipRot. Clock Town chimney and smoke skip rotation when the camera skipped. | [Z/patches/specific_actor_transform_tagging.c#L957-L1033](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/specific_actor_transform_tagging.c#L957-L1033), `#L1080-L1100`; [Z/patches/terrain_transform_tagging.c#L53-L80](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/terrain_transform_tagging.c#L53-L80) |
| Dynamic water mesh (Woodfall rising) | Vertex interpolation on `DmChar01`. | [Z/patches/terrain_transform_tagging.c#L132-L190](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/terrain_transform_tagging.c#L132-L190) |
| Console lag that game timing depends on | Giants cutscene: extra VI periods inserted on measured console-lag frames so music stays in sync. `framerateDivisor` and the reported refresh rate include them. | [Z/patches/fixes.c#L263-L297](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/fixes.c#L263-L297); [Z/patches/ui_patches.c#L99-L100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L99-L100), `#L164-L175` |
| Scene transitions | No dedicated patch found. A grep for "transition" in patches finds only the aspect-ratio wipe and camera-input fixes. Cuts are presumably caught by the camera predictor, and new actors get new spawn IDs (inferred). | [Z/patches/effect_patches.c#L11-L20](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L11-L20); [Z/patches/camera_patches.c#L1207](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_patches.c#L1207) |

### 5. Cost: what transfers to an N64

**What RT64 spends, and where.** RT64 runs all RSP vertex work in a GPU compute shader. Because transforms are deferred to that step, "it's very easy ... to patch transformations ... and produce a new frame very quickly" (cited, [R/README.md#L58](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/README.md#L58)). A second renderer "replays all the draw calls ... at a different rate when using interpolation" (cited, [R/README.md#L66](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/README.md#L66)). Matching runs on a host thread (`curFrame.match`, [R/src/hle/rt64_workload_queue.cpp#L951-L956](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_workload_queue.cpp#L951-L956)). The game side adds only tag commands and the bookkeeping in the patches. **No statement of game-side CPU cost was found.** I searched both READMEs, `Zelda64Recomp/docs/` (it holds one image) and the patch comments for "cost", "perf", "interpol" and "framerate".

**What transfers (inferred).**
- The **hazard catalog and the ID scheme** transfer as-is. N64 interpolation needs the same answers: stable per-object keys, spawn-reset flags for slot-reused effects, a camera-cut detector, actor teleport flags and billboard handling. Zelda64Recomp uses only explicit IDs with linear order. So an N64 implementation needs no draw-call hashing or heuristic matching, which is the expensive, error-prone part of RT64. It needs a table keyed by ID that holds the previous tick's matrix (or its inputs).
- The **"skip = render current state"** semantics transfer and are the fidelity-safe default ([R/src/common/rt64_math.cpp#L177-L198](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/common/rt64_math.cpp#L177-L198)). An object with no prior key, or one flagged skip, draws the keyframe matrix on all three fields. That is what retail shows on those fields.
- The **RSP and RDP cost does not shrink.** On N64 the display list must run once per field. Interpolation saves nothing on the RSP gfx task (16-25 ms per execution at retail, cited, `tools/bench/README.md` "Fork baseline", lines 113-131) or on the RDP (26-34 ms). Both must fit in 16.7 ms per field. RT64 sidesteps both on a host GPU.
- **CPU options per field.** (a) Re-run the draw pass with interpolated inputs. This costs the draw share of the 18-31 ms game ms every field. (b) Build the list once per tick, then for each field overwrite the `Mtx` words in place with interpolated values and resubmit. RT64 already keys transforms by `Mtx` RDRAM address (`physicalAddressTransformMap`, [R/src/hle/rt64_rsp.cpp#L410](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rsp.cpp#L410), `#L466`), and `gEXEditGroupByAddress` relies on that. That suggests a "matrix patch table" of (Mtx address, prev MtxF, cur MtxF, flags). CPU cost is then about the matrix count times (lerp plus float-to-s15.16 conversion). Option (b) needs the matrix memory rewritten between RSP runs, so it needs per-field matrix copies or sync. It also needs the tick's display list to stay valid for 3 executions.
- **SIMPLE vs DECOMPOSE.** Zelda64Recomp uses decomposition for actors (41 DecomposedNormal sites) and SIMPLE for the camera. Decomposition means polar or QR decomposition plus quaternion slerp per matrix ([R/src/hle/rt64_rigid_body.cpp#L78-L104](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rigid_body.cpp#L78-L104)). On the VR4300 that is far costlier than component lerp. Interpolating the inputs (position, rotation, joint table) and re-deriving matrices is the alternative (see appendix A).
- **Vertex and tile interpolation.** Sword trails, skin and Woodfall water need CPU per-vertex lerp of Vtx arrays per field. Texture scroll needs `G_SETTILESIZE` words rewritten per field. Tile coordinates are 10.2 fixed point, so 1/3-step scroll values quantize to quarter texels (inferred).
- **Display-list space.** Retail POLY_OPA peaks at 10.5-27.4% of capacity in the bench scenes (cited, bench README line 128). Zelda64Recomp enlarged the pools about 10x ("10 times bigger than the game's normal buffers", [Z/patches/ui_patches.c#L12-L22](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L12-L22); retail sizes in `include/gfx.h` lines 36-40). The patch does not say whether tags caused that.

### 6. Is the native-rate (keyframe) render identical to the original?

No, for Zelda64Recomp. Several reasons come from the code.
- **Render-path substitutions** apply at any framerate. Stars become 3D billboards with a reduced max size ([Z/patches/sky_transform_tagging.c#L105-L290](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L105-L290)). The textbox and letterbox become triangles ([Z/patches/ui_patches.c#L892-L1162](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L892-L1162)). Glow occlusion moves from the CPU Z read to `gEXVertexZTest`, which drops the 1-frame lag ([Z/patches/culling.c#L76-L150](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/culling.c#L76-L150)). Null-texture skyboxes are not drawn ([Z/patches/sky_transform_tagging.c#L54-L60](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L54-L60)). All kaleido pages are drawn, and kaleido is drawn as strips with bilerp compensation ([Z/patches/fixes.c#L27-L28](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/fixes.c#L27-L28), `#L214-L215`). Motion-blur alpha is clamped to 0.825 even at 20 fps: the exponent is 1, but the clamp still applies on 8-bit framebuffers ([Z/patches/effect_patches.c#L89-L101](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L89-L101)). Several widescreen changes scale effects.
- **RT64 rendering** is a GPU reimplementation of the RDP, not a bit-exact one. The README claims effects render "exactly as they did originally" (cited, [Z/README.md#L54](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/README.md#L54)). No pixel-hash comparison was found or run.
- **Matrix values at weight 1.0.** With decomposition on, the final frame is recomposed from decomposed parts ([R/src/hle/rt64_rigid_body.cpp#L90-L104](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rigid_body.cpp#L90-L104)). Its bits can differ from the game's matrix. SIMPLE lerp at t=1 computes `lerp(a,b,1)`, which may differ from `b` by rounding (inferred). Unmatched or skipped transforms use the current matrix exactly.
- **Timing structure does match F1.** For 60 over 20 the three display frames of tick N have weights 1/3, 2/3 and 1.0 ([R/src/hle/rt64_workload_queue.cpp#L1053-L1056](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_workload_queue.cpp#L1053-L1056)). The tick's own state lands on its final field, as F1 in preferences line 4 defines. The first visible move toward tick N happens on that tick's first field, the same field where retail first shows tick N (inferred, relevant to F3).

Lesson for this program (inferred). Keyframes must be the retail display list and retail `Mtx` bits, not lerp(t=1) output. Any substitution such as rect to tris or depth-read replacement breaks F1 unless only intermediate fields use it. **Framebuffer-feedback effects are a new F1 hazard on N64.** Motion blur (`Play_DrawMotionBlur`) and the pause-background prerender blend the previous displayed framebuffer. If intermediate fields are drawn into the same chain, the keyframe blends an interpolated image instead of retail's previous keyframe, and its pixels change. Zelda64Recomp only compensates the decay rate ([Z/patches/effect_patches.c#L54-L126](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L54-L126)). That approach does not give pixel identity.

### Techniques

| technique | resource | expected gain | fidelity risk | effort | license if reused | sources |
|---|---|---|---|---|---|---|
| Explicit stable interpolation keys (spawn counter x 512 + limb, slot-indexed effects) | CPU (small bookkeeping) | Enables interpolation. No perf gain by itself. | Identical keyframes if the keyframe stays the retail DL. Interpolated fields are new images by definition. | M | GPL-3.0 if code copied. Idea free. | [Z/patches/actor_data.c#L48-L52](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c#L48-L52), [Z/patches/transform_ids.h#L6-L43](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/transform_ids.h#L6-L43) |
| Skip = draw current-tick matrix; unmatched = current | none | Fidelity guard | Identical to retail for skipped objects | L | MIT (RT64 semantics) | [R/src/common/rt64_math.cpp#L177-L198](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/common/rt64_math.cpp#L177-L198), [R/src/render/rt64_transform_processor.cpp#L41-L60](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/render/rt64_transform_processor.cpp#L41-L60) |
| Camera cut detector (eye/at velocity predictor plus per-scene overrides) | CPU (trivial) | n/a | Misses cause one bad intermediate frame. Keyframe unaffected. | M (needs scene list) | GPL-3.0 if copied | [Z/patches/camera_transform_tagging.c#L33-L92](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/camera_transform_tagging.c#L33-L92), `#L153-L268` |
| Spawn-reset flags for slot-reused particles and effects | CPU (trivial) | n/a | Same as above | M (many effect systems) | GPL-3.0 if copied | [Z/patches/particle_transform_tagging.c#L12-L100](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/particle_transform_tagging.c#L12-L100) |
| Billboard tracking through `Matrix_*` stack plus rotation skip on camera cut | CPU (small per Matrix op) | n/a | Intermediate only | M | GPL-3.0 if copied | [Z/patches/billboard_tagging.c#L36-L169](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/billboard_tagging.c#L36-L169) |
| Build DL once per tick; patch `Mtx` (and tile-size words) in place per field | CPU (avoids rebuilding DL 2 of 3 fields) | Saves the draw-build CPU on 2 of 3 fields (inferred, not measured). RSP and RDP unchanged per field. | Keyframe identical only if the final field restores the original `Mtx` bits | H | own work | inferred from [R/src/hle/rt64_rsp.cpp#L410](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rsp.cpp#L410), `#L466`, `#L1108-L1124` |
| Component lerp (SIMPLE) instead of decomposition | CPU | Large per-matrix saving vs decompose and slerp (inferred) | Intermediate only. May show shear artifacts RT64 avoids. | L | MIT | [R/src/common/rt64_math.cpp#L165-L198](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/common/rt64_math.cpp#L165-L198), [R/src/hle/rt64_rigid_body.cpp#L90-L104](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_rigid_body.cpp#L90-L104) |
| Render-path swaps (rects to tris, Z-read to vertex Z test, stars as 3D) | n/a | n/a | Differs at keyframe. Reject. | n/a | GPL-3.0 | [Z/patches/ui_patches.c#L892-L1162](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/ui_patches.c#L892-L1162), [Z/patches/culling.c#L76-L150](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/culling.c#L76-L150), [Z/patches/sky_transform_tagging.c#L105-L290](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/sky_transform_tagging.c#L105-L290) |
| Motion-blur alpha rescale by framerate | RDP neutral | n/a | Differs (keyframe pixels change through the feedback chain). Reject. Blur feedback must see retail's previous keyframe. | n/a | GPL-3.0 | [Z/patches/effect_patches.c#L54-L126](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/effect_patches.c#L54-L126) |
| RT64 heuristic draw-call matching | CPU (heavy, host) | n/a | Mismatches are visible | H | MIT | [R/src/hle/rt64_game_frame.cpp#L219-L580](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/src/hle/rt64_game_frame.cpp#L219-L580), [R/README.md#L80](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/README.md#L80) |

### Licenses

- Zelda64Recomp: GPL-3.0 (`COPYING`, read in the clone at `b65c482`). `patches/` has no license file of its own (`ls patches`), so it falls under the repo's GPL-3.0 (inferred). The patches copy whole decomp functions with edits.
- RT64: MIT, "Copyright (c) 2024 RT64 Contributors" ([R/LICENSE](https://github.com/rt64/rt64/blob/23cab603c4f9f4a8b369b38e036f1aa484603878/LICENSE)). This covers `rt64_extended_gbi.h`.
- N64Recomp: MIT, "Copyright (c) 2024 Wiseguy" (`LICENSE` at `ffb39cd`).
- N64ModernRuntime (Zelda64Recomp submodule, pinned `df7e820d8c55e4fcb4616c210cbb2c01b25cd48c`): GPL-3.0 (`COPYING`, fetched by raw URL).
- zeldaret/mm (submodule pinned `5607eec18bae68e4cd38ef6d1fa69d7f1d84bfc8`): not found. `LICENSE` returned 404, the contents API listing at that ref has no license/copying file, and `/repos/zeldaret/mm/license` returned "Not Found".
- Implication (inferred): copying patch code into our ROM source would bring GPL-3.0 obligations. Reimplementing the mechanisms from this description avoids that. RT64's GBI header and semantics are MIT.

### Sources fetched

- git clone https://github.com/Zelda64Recomp/Zelda64Recomp, HEAD `b65c482ed672258eb684ab94a4e8c8aa662615ee`, 2026-10-25.
- git clone https://github.com/rt64/rt64, HEAD `d9817c68ffd7328cd0af383af124971489366e18`, plus `git fetch` of `23cab603c4f9f4a8b369b38e036f1aa484603878` (pinned, read), 2026-10-25.
- git clone https://github.com/N64Recomp/N64Recomp, HEAD `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`, 2026-10-25.
- https://raw.githubusercontent.com/N64Recomp/N64ModernRuntime/df7e820d8c55e4fcb4616c210cbb2c01b25cd48c/COPYING (fetched 2026-10-25).
- https://raw.githubusercontent.com/N64Recomp/N64ModernRuntime/df7e820d8c55e4fcb4616c210cbb2c01b25cd48c/LICENSE (404).
- https://raw.githubusercontent.com/zeldaret/mm/5607eec18bae68e4cd38ef6d1fa69d7f1d84bfc8/LICENSE (404).
- https://api.github.com/repos/N64Recomp/N64ModernRuntime/contents?ref=df7e820d8c55e4fcb4616c210cbb2c01b25cd48c
- https://api.github.com/repos/zeldaret/mm/contents?ref=5607eec18bae68e4cd38ef6d1fa69d7f1d84bfc8
- https://api.github.com/repos/zeldaret/mm/license
- Local: `tools/bench/README.md` (Fork baseline, lines 113-131); `include/gfx.h` lines 36-40.

### Gaps

- Not run. No Zelda64Recomp build or session, so no measurement of matched or unmatched fraction, matrices per frame, or RT64 matching time.
- No statement of game-side cost found (searched as described in section 5).
- Matrices per MM frame (needed to size the N64 per-field patch cost) were not counted. A bench hook counting `G_MTX` per tick would answer it.
- The `gEXEditGroupByAddress` bit-18/bit-19 overlap is inferred from code only. Its runtime effect is not verified.
- Large files were scanned by comment and command lines, not read line by line. This applies to `camera_patches.c` (dual-analog camera, judged not interpolation-related), `ocarina_effect_patches.c`, `effect_transform_tagging.c` 400-695 and `ui_patches.c` 242-890. Some hazards inside them may be missing from the table.
- Changes between the pinned RT64 and HEAD (float matrices, look-at matching, mirrored biasing) were noted from the diff and not analyzed.
- zeldaret/mm license not found.

## Appendix C. RSP: graphics microcode, FIFO, audio microcode, display-list levers

Written 2026-10-25. Decomp read at wScottSh/mm-decomp-60fps commit `caeb013ac597d556367c30e6f5d292100f93e58f` (the R1 worktree; its source tree equals main caeb013ac). Decomp permalinks below use that SHA.

### Summary

- MM's gfx microcode is `F3DZEX.NoN fifo 2.08I`. A matching, documented armips source for it exists in the early history of the F3DEX3 repo. I built it and it is byte-identical to MM's extracted binary (measured). This is the base for any pixel-exact RSP microcode work.
- F3DEX3 itself is not pixel-identical to F3DZEX2. It changes Z coefficient math, decal Z, point lighting, clipping, fog timing and the viewport encoding. It also needs GBI and display-list changes. Reject it as a drop-in under the fidelity rule.
- Several F3DEX3 speedups touch only control flow (command dispatch, small RDP commands, tri cull paths, DL fetch hints, vertex cache size). Porting those into the matching 2.08I source can keep the RDP command stream bit-identical. The check is direct: diff the RDP FIFO bytes per frame against stock. Effort is high.
- The gfx FIFO is 0x3000 u64 = 96 KiB. The RSP stalls only when it gets about 96 KiB of RDP commands ahead of the RDP (about 558 full tris, inferred). The bench's "RSP gfx" number (task start to done) therefore includes FIFO-full stalls when the RDP is behind. The matching source gives the IMEM addresses of the stall loops, so the emulator can measure stall time.
- Audio runs one RSP task per VI retrace, not per game frame. All 21 audio specs have `unk_04 == 1`. Each task covers 1/60 s of audio (544 samples at 32 kHz). So audio RSP time per field does not change at 60 fps. The bench's 5.3 to 9.4 ms "RSP audio" per tick is the sum of 3 tasks, about 1.8 to 3.1 ms per field (inferred).
- MM's audio ucode is the "NEAD" ABI family (mupen64plus HLE `alist_process_nead_mm`). No matching disassembly or published optimized version was found. Any faster audio ucode must reproduce every ABI command bit-exactly. I found no published bit-identical audio speedup.

### 1. What MM ships and how it is linked

- The `code` segment links `rsp/aspMain.o`, `gspF3DZEX2.NoN.PosLight.fifo.o` (plus `fifo_legacy` for pre-US), `gspS2DEX2.fifo.o` and `njpgdspMain.o`. Source: https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/spec/spec#L782-L788
- Each `rsp/*.s` is an `.incbin` of the binary extracted from the baserom (read `rsp/gspF3DZEX2.NoN.PosLight.fifo.s`, `rsp/aspMain.s`).
- The boot gfx ucode is F3DZEX2 PosLight fifo. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/sys_ucode.c#L10-L11
- S2DEX2 fifo is swapped in mid-DL with `gSPLoadUcodeL` for room backgrounds, the HUD, prerender and wipes, e.g. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/z_room.c#L329
- ID string in the extracted data (measured, `strings`): `RSP Gfx ucode F3DZEX.NoN  fifo 2.08I Yoshitaka Yasumoto/Kawasedo 1999.`
- MD5 of the extracted binaries (measured): text `ca0a31df36dbeda69f09e9850e68c7f7` (5680 B), data `d31cea0e173c6a4a09e4dfe8f259c91b` (1056 B). aspMain text `250a0b6f93359f0da87f7097554a026c` (4096 B), data `2de5e68f7291811c6a34bdbf3c68c80c` (736 B).
- The gfx task is built in `Graph_TaskSet00`. It sets `output_buff` / `output_buff_size` to the FIFO and a yield buffer. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/graph.c#L192-L207

#### Matching source for MM's gfx ucode

- The F3DEX3 repo started (2020-09) as "Matching and (partially) documented disassemblies of the f3dex2/f3dzex N64 RSP microcode families", listing "F3DZEX 2.08I (Majora's Mask)". https://github.com/HackerN64/F3DEX3/blob/da6ee054c93f2e3986d43c68976a64bfbae79c0f/README.md
- At commit `59bf0916ee8f0cb0c4c7ab165c1d25acd8cd5d85` (2023-02-24), `ucodes_database.mk` defines `F3DZEX_NoN_2.08I`, "Majora's Mask", with options `CFG_G_BRANCH_W CFG_NoN CFG_POINT_LIGHTING BUG_WRONG_INIT_VZERO` and the same MD5s as MM's binary. https://github.com/HackerN64/F3DEX3/blob/59bf0916ee8f0cb0c4c7ab165c1d25acd8cd5d85/ucodes_database.mk#L136-L146
- Measured: I built armips (Kingcom/armips `62adab4ef30da765f5cf22a451eb08a59c54dc8b`) and ran `make F3DZEX_NoN_2.08I` at `59bf0916`. The build printed "code matches" and "data matches". `cmp` against `extracted/n64-us/incbin/gspF3DZEX2_NoN_PosLight_fifo{Text,Data}` reported identical. Build output is in `/tmp/claude-1000/-home-wscottsh-repos-mm-decomp-60fps/77f24093-6691-428b-a96d-42e2d8b400bc/scratchpad/f3dzex-build/F3DZEX_NoN_2.08I/`.
- The source is one file, `f3dex2.s`, 2534 lines (measured). It also has a `CFG_XBUS` option. Key constants: input DL buffer `0xA8` bytes (21 commands), two DMEM RDP buffers of `0x158` bytes each. https://github.com/HackerN64/F3DEX3/blob/59bf0916ee8f0cb0c4c7ab165c1d25acd8cd5d85/f3dex2.s#L502-L513

### 2. F3DEX3 (HackerN64)

Cloned `https://github.com/HackerN64/F3DEX3` at `91a85284821181af627d838e47d245718804f388` (2025-10-04). Status: "F3DEX3 is in beta." https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/README.md#L7-L8

#### Performance claims (all cited from the repo, hand-counted by the author, not measured by me)

- "F3DEX3_NOC matches or beats the RSP performance of F3DEX2 on all critical paths". https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/docs/Documentation/Performance.md#L5-L9
- Cycle table (F3DEX2 / F3DEX3_NOC / F3DEX3): command dispatch 12 / 10 / 10; small RDP command 14 / 4 / 4; tri to offscreen 27 / 20 / 20; tri to backface 38 / 31 / 31; tri to draw 172 / 149 / 151; vertex pair no lighting 54 / 54 / 70; vertex pair 1 dir light 73 / 70 / 86; 2 dir lights 76 / 77 / 93; vertex pair 1 point light 276 / 194 / 210; light dir xfrm 1 light 141 / 92 / 92. https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/docs/Documentation/Performance.md#L33-L91
- So the occlusion-plane build (non-NOC) is slower per vertex than F3DEX2 (70 vs 54 cycles per unlit pair). Same table.
- Point lights: "77 cycles per point light per vertex pair, while F3DEX2_PL takes 144." https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/README.md#L102-L105
- 56-vertex buffer, up from 32. https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/README.md#L71-L74
- DL "hints" save "maybe around 100 us per frame" of DRAM traffic. README.md#L97-L101 (same commit).
- Occlusion plane: "RDP time savings of 3-4 ms are common in scenes with reasonable occlusion planes". https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/docs/Documentation/Configuration.md#L10-L31
- Tri snakes "save about 70 us of RDRAM/RDP time but cost about 400 us of RSP time" for a typical scene. Performance.md#L93-L116.
- Profiling: 21 performance counters across the default and `_PA`, `_PB`, `_PC` builds, including RSP FIFO-full stall cycles and RDP stall on RDRAM ("often half to two thirds of the total RDP time"). Profiling builds turn `SPLightToRDP` into no-ops and remove flat shading. Configuration.md#L33-L55.

#### Variants

- `NOC`: removes occlusion-plane math from vertex processing. Configuration.md#L10-L31.
- `BrZ` / `BrW`: `BrW` for F3DZEX games (OoT, MM); `SPBranchLessZ` uses W. Configuration.md#L57-L64.
- `PA` / `PB` / `PC`: profiling. `PC` "is specially designed to work with S2DEX for OoT but other microcodes are not guaranteed to work". https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/docs/Documentation/Backwards%20Compatibility.md#L71
- `LVP`: removed. It was merged into a dual "basic"/"advanced" lighting path. https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/docs/Documentation/Removed.md#L7-L47
- FIFO only. There is no XBUS build option in the current Makefile (`ALL_OPTIONS` lists only branch, NOC, profiling). https://github.com/HackerN64/F3DEX3/blob/91a85284821181af627d838e47d245718804f388/Makefile#L15-L20

#### Feature-by-feature fidelity versus F3DZEX2

| Change | Doc quote / evidence | Pixel-identical to F3DZEX2? |
|---|---|---|
| Z coefficients | "Max Z value changed from 0x03FF to 0x7FFF"; scale moved to the viewport, "an extra 5 bits of Z precision ... retained". `G_MAXZ` is a compile error; viewports must change. gbi.h#L1152-L1191 | No. Z values written to the RDP differ, so depth tests and Z buffer contents can differ. |
| Viewport | "Y scale is negated"; `Vp` encoding changed. gbi.h#L1152-L1191 | No by itself it is equivalent only after the CPU change, and Z still differs. |
| Decal Z fix | "Z-fighting of decals has been nearly eliminated ... automatically enabling it for all decals with no code or DL changes." README.md#L124-L129. Code clamps DzDy for `ZMODE_DEC` tris. f3dex3.s#L1341-L1346 | No. Every decal tri changes. MM uses decals widely. |
| Point lighting | "redesigned ... Fixed a bug in F3DEX2/ZEX point lighting where a Z component was accidentally doubled"; `kq` now E3M5. README.md#L132-L136. `G_LIGHTING_POSITIONAL` ignored. | No. MM enables RSP point lights per room: `if (room->enablePosLights ...) gSPSetGeometryMode(..., G_LIGHTING_POSITIONAL)`. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/z_room.c#L105-L108 |
| Directional "basic" lighting | Basic path "roughly the same speed as F3DEX2"; "vertex normals are treated the same way as in F3DEX2". README.md#L147-L154, Design Tradeoffs.md#L86-L88 | Unknown. The docs claim the same treatment, not bit-identical output. Not verified. Lights layout and encodings changed (`Light_t` stride 16, ambient last). Backwards Compatibility.md#L112-L150 |
| Lookat / texgen | Rewritten; source comment "This is slightly wrong ... Basically rounding error." f3dex3.s#L3147-L3149 | No (rounding differs per the author's own comment). |
| Clipping | "Far clipping is completely removed"; NoN mandatory. Design Tradeoffs.md#L40-L61. `SPClipRatio` "Converted into no-op"; ratio fixed at 2. Backwards Compatibility.md#L108. Clip conditions "in a different order from vanilla". f3dex3.s#L1986 | Likely no. MM sets `FRUSTRATIO_2` (z_rcp.c#L850), so ratio matches. Far clip removal and the reworked clipper can change clipped-tri output. Not verified per scene. |
| Fog | Fog is now applied with tri-time state: "Changing fog settings ... between loading verts and drawing tris ... will lead to incorrect fog values". Design Tradeoffs.md#L141-L147 | Identical only if MM never changes fog state between `SPVertex` and its tris. Not verified. |
| Triangle write | Rewritten (149 vs 172 cycles), Z path moved, attribute rounding init "0x8000". f3dex3.s#L1374-L1378 | No guarantee. Coupled to the Z change above. |
| Non-textured tris | No longer skips texture math; still does not send tex coefficients. Design Tradeoffs.md#L116-L125 | Output likely same; RSP slower for untextured tris. |
| 56-vertex buffer | Larger cache. README.md#L71-L74 | Cache size alone does not change output. Gain needs re-exported DLs, which changes assets. |
| Auto-batched texture-load skip | "If this system incorrectly culls supposedly repeated texture loads which actually differ due to segment manipulation". README.md#L79-L85 | Identical only if no segment remap between repeated materials. MM remaps segments 8 to 0xD for animated textures. Risky. |
| Relative segments | "Segment 0 is now reserved"; `gsSPSegment(0x08, 0x07001000)` resolves relative. Legacy SPSegment encoding keeps old behavior. Design Tradeoffs.md#L105-L114, Backwards Compatibility.md#L73 | Identical if segment 0 stays 0 and legacy encoding is used. |
| DL hints | "will never affect on-screen output". Backwards Compatibility.md#L64 | Identical (needs DL changes to gain). |
| `SPMemset` | Fills RDRAM "faster than the RDP can in fill mode". README.md#L111-L114. In overlay 3. | Identical memory contents if the same 16-bit value fills the same range (inferred). Needs DL changes. |
| `SPFlush` | Perf only. Backwards Compatibility.md#L76 | Identical. |
| Matrix push / pop / mul | Moved to overlay 3 (slower). Encoding of `SPMatrix` changed. Backwards Compatibility.md#L84-L96 | Output same math claimed; slower for MM's push/mul uses. |
| Yield check | Now once per input buffer refill ("typically once every 21 commands"), worst case "a few hundred microseconds" delay to audio start. Design Tradeoffs.md#L127-L139 | Pixels unaffected. Audio start latency changes (audio content unchanged, inferred). |
| `G_RDPHALF_1`, `SPTexture` after S2DEX | Tri draw overwrites RDPHALF_1; `SPTexture` / `SPFogFactor` "left as garbage values" after returning from S2DEX. Design Tradeoffs.md#L148-L154 | Risk for MM, which returns from S2DEX2 mid-frame (room backgrounds, HUD). Needs fixups. |
| Binary DL compatibility | "generally binary backwards compatible with OoT-style display lists". Backwards Compatibility.md#L10-L13 | Not a pixel claim. |

There is no "F3DZEX2 compatibility mode" in the docs or Makefile (searched README, docs/, Makefile; not found).

Verdict: F3DEX3 as shipped differs. Reject under the pixel-exact rule.

#### OoT / MM integration and license

- HackerOoT enables it by default: `#define ENABLE_F3DEX3 true`, "only works on real console or LLE emulators". https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/include/config/config_graphics.h#L9-L23
- HackerOoT ships F3DEX3 as BPS patches against the extracted `f3dzex2.code` / `f3dzex2.data` (`create_f3dex3_patches`). https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/.make_hackeroot.mk#L193-L214. This suggests the project treats the binary as a derivative of Nintendo's microcode (inferred).
- MM port of F3DEX3: not found (web search "F3DEX3 Majora's Mask decomp port microcode" returned nothing relevant).
- License: no LICENSE file in either F3DEX3 commit I read. README: "F3DEX3 modifications from F3DEX2 are by Sauraen and are dedicated to the public domain." Credit request "F3DEX3 Microcode - Sauraen". README.md#L166-L174. Build ID string says "by Sauraen & Yoshitaka Yasumoto/Nintendo". Makefile#L176. The base code is Nintendo's. Reuse carries the same legal posture as the decomp's own extracted asm. Flag for Scott.

### 3. FIFO versus XBUS

- SDK definitions (Nintendo programming manual, fetched): FIFO method: "the RDP commands are expanded in a FIFO buffer in RDRAM and then passed to the RDP." XBUS: "passed from the RSP to the RDP via the XBUS ... this method makes no use of RDRAM." XBUS drawbacks: "the internal buffer fills up and the RSP halts until the internal buffer becomes free again" and "audio processing by the RSP cannot proceed in parallel with the RDP's graphics processing." XBUS benefit: "I/O to RDRAM is smaller than with FIFO (around 1/2)". Minimum FIFO for F3DEX2 / .NoN is 0x410 bytes; S2DEX2 0x800. https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro25/25-04.html
- Hardware: DPC_STATUS bit 0 "0: DMA transfer source is RDRAM; 1: DMA transfer source is DMEM (via XBUS)." DPC_START/END are double-buffered; one pending transfer. https://n64brew.dev/wiki/Reality_Display_Processor/Interface
- MM's FIFO: `u64 (*gGfxSPTaskOutputBufferLoRes)[0x3000]` = 98,304 bytes. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/sys_cfb.c#L19 and allocation https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/graph.c#L390-L393. The scheduler zeroes it per gfx task. sched.c#L195-L197.
- How the stall works (matching source, `flush_rdp_buffer`): each 0x158-byte DMEM buffer is DMA'd to `rdpFifoPos`. On wrap, the ucode spins until `DPC_CURRENT` leaves the buffer start. Then it spins while `DPC_CURRENT` lies inside the next destination range. https://github.com/HackerN64/F3DEX3/blob/59bf0916ee8f0cb0c4c7ab165c1d25acd8cd5d85/f3dex2.s#L936-L975. The stall loops sit at IMEM `0x1298` and `0x12A8` to `0x12BC` in the 0x1000-based sym file (measured from the build's `.sym` and labels).
- A full shade+texture+Z tri is 32+64+64+16 = 176 bytes (n64brew RDP commands page, fetched: https://n64brew.dev/wiki/Reality_Display_Processor/Commands). 98,304 / 176 is about 558 tris of lead (inferred).
- Effect on numbers (inferred): the bench's `rsp_gfx_ticks` is RSP task start to done, from the scheduler accumulators (graph.c#L347-L354, sched.c#L468-L476). When the RDP is the limiter, the RSP sits in the spin loops near the end of the frame, and that time counts as "RSP gfx". The bench's `rdp_ticks` (start to full sync, 26 to 34 ms) exceeds RSP gfx (16 to 25 ms), so the RDP finishes later. That alone does not tell how much of RSP gfx is stall. Measure it: count RSP cycles with PC in the stall-loop range in the ares fork.
- Bigger FIFO: pixel-identical by construction (same commands; inferred). It lets the RSP finish earlier and free the RSP for the next field's audio or gfx task. It does not reduce RDP work. Cost is RDRAM (Expansion Pak has room; the memory map belongs to other units).
- XBUS build of F3DZEX2: the matching source has `CFG_XBUS`. Same command bytes, so identical pixels (inferred). It would couple RSP speed to the RDP and block audio overlap. Bad for 60 fps. Reject.

### 4. Audio microcode and its scheduling

#### Which ucode

- MM links `aspMain` with `task->type = M_AUDTASK`, `ucode = aspMainTextStart`, `data_ptr = abiCmdBufs[...]`. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/audio/lib/thread.c#L168-L186
- mupen64plus-rsp-hle classifies it as the "nead" ABI: `0x1f681230 /* Zelda OoT / Zelda MM (J, J RevA) */ -> alist_process_nead_oot`, `0x1f801250 /* Zelda MM (except J, J RevA, E Beta) ... */ -> alist_process_nead_mm`. https://github.com/mupen64plus/mupen64plus-rsp-hle/blob/8a7a472a7172eb2c8725b305eae26818ed7b51a2/src/hle.c#L286-L291 (GPL-2.0, read file header).
- SDK: "we recommend using n_audio because it is faster." No numbers. https://ultra64.ca/files/documentation/online-manuals/man-v5-1/tutorial/audio/1/1_1.htm. The NEAD ABI is the "n_audio" lineage (inferred from the name; not confirmed by a primary source).

#### Cadence: per VI retrace, not per game frame

- The IRQ manager registers for every retrace: `IrqMgr_Init(..., 1)` then `osViSetEvent(&irqMgr->queue, IRQ_RETRACE_MSG, retraceCount)`. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/boot/idle.c#L94 and https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/boot/irqmgr.c#L276
- The audio manager thread calls `AudioMgr_HandleRetrace` on each `OS_SC_RETRACE_MSG`. That sends the previous `AudioTask` to the scheduler and calls `AudioThread_Update()` to build the next. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/audio_thread_manager.c#L12-L45 and #L108-L110
- `AudioThread_UpdateImpl` builds a task only when `totalTaskCount % specUnk4 == 0`. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/audio/lib/thread.c#L36-L47. `specUnk4 = spec->unk_04` (heap.c#L985). All 21 entries of `gAudioSpecs` have `unk_04 == 1` (measured by grep, session_config.c#L101).
- Samples per task: `ALIGN16(samplingFreq / refreshRate)`; `refreshRate = 60` on NTSC. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/audio/lib/heap.c#L945-L953 and https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/audio/lib/load.c#L1253-L1268. At 32 kHz that is 544 samples and 3 updates per task (inferred arithmetic from those lines).
- The scheduler yields the running gfx task whenever an audio task is queued. https://github.com/wScottSh/mm-decomp-60fps/blob/caeb013ac597d556367c30e6f5d292100f93e58f/src/code/sched.c#L410-L415, and refuses to yield audio (sched.c#L228-L237).
- Conclusion: audio RSP time per field is set by VI rate and audio content, not by fps. It is the same at 60 fps. The bench accumulator resets once per Graph frame (graph.c#L347-L354), so at 20 fps "RSP audio" 5.27 to 9.40 ms (README "Fork baseline") covers 3 tasks. Per field that is about 1.8 to 3.1 ms (inferred). At 60 fps the per-field audio share is the same; what changes is that gfx gets 3x as many tasks in the same time.
- Same holds for the audio thread's CPU work (it runs per retrace). Appendix E covers CPU.

#### Published optimizations and bit-identity

- SDK tuning guide: RSP handles "a maximum of 160 samples per frame" per pass; pick rates near multiples of 160; avoid heavy reverb and frequent pitch bends; double buffers when the game runs at 30 fps or less. https://ultra64.ca/files/documentation/online-manuals/man/developerNews/news-03-5.html. Every one of these changes content or the sample rate except buffer doubling.
- Buffer doubling in MM terms is `specUnk4 == 2` (paths exist at heap.c#L821 and #L853). It halves task count and per-task overhead. It changes the number of samples per task and update timing, so output may differ (inferred, not verified). Reject unless proven bit-identical.
- Kaze / HackerSM64 / HackerOoT audio ucode speedups: not found (web searches for Kaze audio RSP, aspMain disassembly, OoT/MM audio ucode optimization returned nothing primary).
- libdragon mixer: a different synthesis engine. Replacing MM's ABI with it changes audio. Not fetched; not pursued.
- A faster NEAD-ABI ucode that is bit-identical is possible in principle. It needs a matching disassembly first (not found) and a per-command output diff harness. Effort very high. Gain at most the 1.8 to 3.1 ms per field (inferred upper bound, minus the irreducible work).

### 5. Other RSP-side levers, by construction

The RDP sees only the command stream. If the stream is byte-identical, pixels are identical. If the stream differs only in redundant state writes, pixels are identical when the RDP state at every primitive is unchanged (inferred). Rules below are inferred from that.

- Merge `gSPVertex` loads. The vertex transform is per vertex and depends only on the vertex and the current matrix, lights, fog and viewport. Loading the same vertices in fewer batches gives the same tri output in the same order (inferred). The 32-entry F3DZEX2 cache caps this. Identical. Effort medium (asset DL rewrite or runtime batching).
- Drop redundant RDP state commands (repeated `SetCombine`, `SetOtherMode`, `SetPrimColor` with equal values). Identical if the state at each tri is unchanged. Remove `gDPPipeSync` only with care: the sync protects state changes while the pipeline is busy. Removing a needed sync can corrupt pixels on hardware (HackerOoT warns its NOSYNCS option is "buggy (graphical issues / crashes)", config_graphics.h#L17-L23). Treat syncs as not removable without a hardware test.
- Fewer matrix loads. Reuse an already-loaded matrix rather than reloading the same `Mtx`. Identical when the bytes are equal. Do not move `G_MTX_MUL` work from RSP to CPU or the reverse. Fixed-point products differ from float products. MM has 16 `G_MTX_MUL` sites in `src` (measured by grep). Keep them.
- `gSPCullDisplayList`. MM uses it once (skybox, z_vr_box.c#L158). Room geometry uses CPU-side bounds culling. RSP culling skips a DL only when all bound vertices are outside one clip plane, so those tris could not have produced pixels inside the viewport (inferred). Pixel risk: if the scissor is larger than the viewport, culled tris could have drawn into the extra scissor area. Check per call site. RSP saving is the vertex and tri work of the skipped DL.
- Segment changes. Fewer `gSPSegment` writes when the value is unchanged. Identical. Small gain (one moveword each).
- Fewer DL calls / branches. Each `gSPDisplayList` costs a DMA refill of the 21-command input buffer (0xA8 bytes). Flattening tiny DLs cuts DMA latency. Identical if the command sequence is unchanged.
- Bigger RSP-side wins need microcode changes on the 2.08I matching source. Candidates that keep output bytes: F3DEX3-style faster command dispatch (12 to 10 cycles) and small-RDP-command path (14 to 4), faster tri cull paths (offscreen 27 to 20, backface 38 to 31), larger DMEM RDP buffers, larger input buffer, DL fetch hints. These are author-counted F3DEX3 cycle figures, not proof the same savings carry over without the F3DEX3 Z and tri-write changes.

#### Interpolated frames and RSP budget (inferred)

- With presentation interpolation, each field draws a full DL of about retail size. RSP gfx per field then is about the retail per-tick figure, 16 to 25 ms on the fork. With audio at 1.8 to 3.1 ms per field and the 90% target (15 ms), gfx RSP must fit about 12 ms. That is a 1.3x to 2.1x cut, before subtracting FIFO-stall time that is really RDP time.
- First action: measure FIFO-stall cycles. If most of "RSP gfx" is stall, the RSP is not the limiter and RSP work should be booked as RDP work.

### Techniques

| technique | resource | expected gain | fidelity risk | effort | license if reused | sources |
|---|---|---|---|---|---|---|
| Swap in F3DEX3 (any variant) | RSP, RDP | RSP tri draw 172 to 149 cycles; point light 276 to 194 per pair; occlusion plane 3-4 ms RDP (cited) | differs (Z, decal, point lights, clipping, viewport) | medium | public domain mods on Nintendo base | F3DEX3 README, Performance.md, gbi.h, Design Tradeoffs.md |
| Build MM gfx ucode from matching 2.08I source | none by itself | 0; enables everything below | identical (byte-identical, measured) | low | Nintendo-derived; same posture as decomp | F3DEX3@59bf0916 ucodes_database.mk, f3dex2.s |
| Output-preserving ucode optimizations on 2.08I (dispatch, small RDP cmds, cull paths, buffers, DL prefetch) | RSP | unknown; F3DEX3 per-path deltas of 2 to 10 cycles per command and 7 per culled tri are an upper reference (cited, different code) | identical if RDP FIFO bytes match per frame | high | as above | Performance.md, f3dex2.s |
| Instrument FIFO-full stall (PC in 0x1298 to 0x12BC) in ares fork | measurement | splits RSP gfx into work vs RDP wait | none (bench only) | low | n/a | f3dex2.s#L936-L975, build .sym |
| Larger gfx FIFO | RSP idle time, scheduling | RSP finishes earlier when RDP-bound; no RDP gain (inferred) | identical | low | n/a | sys_cfb.c#L19, SDK 25-04 |
| XBUS gfx ucode | RDRAM bandwidth | "around 1/2" RDP-command RDRAM I/O (cited) | identical pixels | low | n/a | SDK 25-04, f3dex2.s CFG_XBUS |
| (reject) XBUS: RSP stalls on RDP and blocks audio overlap | | negative for 60 fps | | | | SDK 25-04 |
| Audio `specUnk4 = 2` | RSP overhead, CPU | fewer audio tasks, same samples (inferred) | may differ | low | n/a | thread.c#L36-L47, heap.c#L821 |
| Bit-exact faster NEAD audio ucode | RSP | up to part of 1.8 to 3.1 ms per field (inferred) | identical only with per-command verification | very high | Nintendo binary; no source found | hle.c#L286-L291 |
| Merge `gSPVertex` batches | RSP, RDRAM | fewer DMAs, fewer reloaded verts (unquantified) | identical (inferred) | medium | n/a | F3DZEX2 32-vertex cache |
| Drop redundant state, segment, matrix reloads | RSP, RDRAM | small per command (dispatch about 12 cycles per command, cited for F3DEX2) | identical if state at each tri unchanged; syncs excluded | medium | n/a | Performance.md |
| Remove `gDPPipeSync` | RDP | few % per material (cited for F3DEX3 NOSYNCS) | may differ (hardware corruption) | medium | n/a | F3DEX3 Porting doc L110-L117, HackerOoT config_graphics.h |
| More `gSPCullDisplayList` | RSP | skips vertex and tri work of offscreen DLs | identical if scissor within viewport | medium | n/a | z_vr_box.c#L158 |
| Move matrix mul between CPU and RSP | RSP or CPU | small | differs (fixed vs float) | n/a | n/a | F3DEX3 gbi.h Accuracy note |

### Sources fetched

- https://github.com/HackerN64/F3DEX3 cloned at `91a85284821181af627d838e47d245718804f388` (commit date 2025-10-04), read 2026-10-25. Files: README.md, docs/Documentation/{Performance, Configuration, Backwards Compatibility, Design Tradeoffs, Removed, Porting your Romhack}.md, gbi.h, f3dex3.s, Makefile.
- HackerN64/F3DEX3 at `59bf0916ee8f0cb0c4c7ab165c1d25acd8cd5d85` (2023-02-24), fetched and built: ucodes_database.mk, f3dex2.s, Makefile, README.md.
- HackerN64/F3DEX3 at `da6ee054c93f2e3986d43c68976a64bfbae79c0f` (2020-11-13) README.md via GitHub API. Also `77669fddfc668da5a492d3628d28aca6e890a697` README.md. Commit list via `gh api repos/HackerN64/F3DEX3/commits`.
- https://github.com/HackerN64/HackerOoT at `e77e6683d9a775df52c35121180712edcedd37fe` (2025-11-27; clone already present in scratch clones dir). Files: include/config/config_graphics.h, .make_hackeroot.mk, Makefile, F3DEX3/n64/.
- https://github.com/mupen64plus/mupen64plus-rsp-hle at `8a7a472a7172eb2c8725b305eae26818ed7b51a2` (2026-06-23). Files: src/hle.c, src/ucodes.h, src/alist_nead.c header.
- https://github.com/Kingcom/armips at `62adab4ef30da765f5cf22a451eb08a59c54dc8b` (built as a tool only).
- https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro25/25-04.html
- https://ultra64.ca/files/documentation/online-manuals/man/developerNews/news-03-5.html
- https://ultra64.ca/files/documentation/online-manuals/man-v5-1/tutorial/audio/1/1_1.htm
- https://n64brew.dev/wiki/Reality_Signal_Processor
- https://n64brew.dev/wiki/Reality_Display_Processor
- https://n64brew.dev/wiki/Reality_Display_Processor/Interface
- https://n64brew.dev/wiki/Reality_Display_Processor/Commands
- https://n64brew.dev/wiki/Microcode returned 404.
- Local decomp at `caeb013ac597d556367c30e6f5d292100f93e58f`; extracted binaries at `~/repos/mm-decomp-60fps/extracted/n64-us/incbin/` (md5 measured).

### Gaps

- FIFO-stall share of "RSP gfx": not measured. Needs the PC-range counter in the ares fork.
- F3DEX3 basic-lighting bit-equality with F3DZEX2: not tested. Moot given the Z and decal changes.
- No per-scene RSP profile of where 2.08I spends cycles (vertex vs tri vs dispatch vs DMA wait). F3DEX3's `PA`/`PB`/`PC` counter code could be ported to the 2.08I source for a bench-only build.
- aspMain: no matching disassembly or open source found. Its per-command cycle costs are unknown.
- Kaze / HackerSM64 audio work, libdragon mixer numbers: no primary source fetched.
- Asset-side `G_CULLDL` and `gSPVertex` batch statistics: not counted (assets are generated at build time; I grepped only `src`).
- Whether MM changes fog state between `SPVertex` and tris: not checked.
- Legal status of shipping a modified Nintendo microcode built from a disassembly: not assessed.

## Appendix D. RDP and RDRAM bandwidth

Researched 2026-10-25. Labels: **cited** (a fetched source states it), **measured** (I ran it; nothing in this slice is measured by me), **inferred** (my arithmetic or reasoning, shown).

### Summary

- The RDP's raw rate is 1 px/clk in 1-cycle and 0.5 px/clk in 2-cycle at 62.5 MHz. FILL writes 64 bits per clock (4 px at 16 bpp). COPY writes 4 px per clock (cited, SDK 12.1.2 to 12.1.5, n64brew Pipeline).
- MM's world geometry uses the most expensive common mode. That mode is 2-cycle, `G_RM_FOG_SHADE_A` + `G_RM_AA_ZB_OPA_SURF2`, so IM_RD, Z_CMP and Z_UPD are all on (cited, MM `z_rcp.c` SETUPDL_25 and `gbi.h`).
- Thar0 measured this mode on hardware. A full 320x240 RGBA16 rectangle, Z pass, VI on, FB, ZB and VI in separate banks, takes **4.856 ms**. The same draw with Z failing takes 3.014 ms. The compute-only floor is 2.481 ms (cited, Thar0 RDP-Timing-Tests sample_results.txt).
- With memory on, the RDP is RDRAM-bound. 2-cycle costs about the same as 1-cycle (4.856 vs 4.957 ms). Thar0 also says so directly. So a cycle-mode change would buy little even if it were allowed. It is not allowed, because it changes pixels.
- The minimum RDP time for one MM field (Z clear plus one full-screen opaque pass) is about **5.2 ms of a 16.68 ms field**. That is 31% (inferred, arithmetic below). Under the 90% budget (15.0 ms) about 9.8 ms remains, or about 2 more full-screen Z-pass layers at the ideal large-span efficiency.
- Almost every RDP lever that saves real time changes pixels. Examples are AA off, IM_RD off, 1-cycle fog, skipping the Z clear, and front-to-back reordering under AA. Only these levers are identical-by-construction: buffer bank placement, RDRAM contention reduction, redundant-load and redundant-sync removal, LoadTile to LoadBlock with identical TMEM, and XBUS or direct command feed. Each saves little (inferred, sub-ms per field).
- The deciding unknown is MM's actual RDP busy time per frame. The fork reports "gfx task start to full sync" at 26 to 34 ms per tick. That figure includes waiting on the RSP, so it is not RDP busy. If true RDP busy per frame is above about 15 ms, 60 fps is not reachable under the no-fidelity-change rule by RDP work alone. Measure DPC_BUF_BUSY / DPC_PIPE_BUSY / DPC_CLOCK per frame first.

### 1. Fill rate per cycle mode

| Mode | Rate | Label | Source |
|---|---|---|---|
| 1-cycle | 1 px per clock at 62.5 MHz, pipelined | cited | n64brew Pipeline (oldid 5674); SDK 12.1.2 |
| 2-cycle | 1 px per 2 clocks | cited | n64brew Pipeline; SDK 12.1.3 ("one pixel is generated every 2 clocks") |
| FILL | 64 bits per clock. That is 4 px at 16 bpp. Writes go straight to RDRAM, not through span buffers | cited | SDK 12.1.4; n64brew Pipeline "Fill Pipeline" |
| COPY | 64 bits or 4 px per clock. 16 bpp only | cited | SDK 12.1.5; libdragon `rdpq_mode.h` L333-L337 ("approximately 4 times faster", "only works with 16-bpp", crashes on 32 bpp) |

Hardware timings for a 320x240 RGBA16 rectangle (76,800 px), Thar0, 1000 runs, PIPEBUSY average (cited, sample_results.txt):

| Config | 1-cycle | 2-cycle | Lines |
|---|---|---|---|
| Alpha compare fails every pixel, no IM_RD, no Z (compute only) | 1.2443 ms | 2.4808 ms | L359, L362 |
| No Z, no VI, IM_RD off | 1.2881 ms | 2.5183 ms | L26, L29 |
| No Z, no VI, IM_RD on | 2.6173 ms | 2.5934 ms | L33, L36 |
| Z R/W, Z pass, no VI, FB+ZB separate, IM_RD on | (not used) | 4.4457 ms | L228 |
| Z R/W, Z pass, VI on, FB+ZB+VI separate, IM_RD off | 3.9317 ms | 3.8365 ms | L297, L300 |
| **Z R/W, Z pass, VI on, FB+ZB+VI separate, IM_RD on (MM's mode)** | 4.9573 ms | **4.8564 ms** | L304, L307 |
| Same, FB+VI same bank, ZB separate | (not used) | 5.3296 ms | L322 |
| Same, FB+ZB same bank, VI separate | (not used) | 6.3521 ms | L337 |
| Z R/W, Z fail, VI on, all separate, IM_RD on | (not used) | 3.0136 ms | L245 |

Derived per-pixel cost (inferred). The compute-only 1-cycle case is 1.2443 ms x 62.5 MHz = 77,769 clk for 76,800 px, about 1.01 clk/px. The ares fork fits this as 240 x 324 + 13. It models 320 + 2 dead px plus a 2 clk line gap (ares spec rows `rdp.primitive-base`, `rdp.span-dead-pixels`, `rdp.span-line-gap`, all "fit only"). MM's mode is 4.8564 ms x 62.5 MHz = 303,525 clk, about 3.95 clk/px. That is about 2x the 2-cycle compute rate. The extra half is RDRAM stall.

Thar0 README says it directly: "without any RDRAM accesses 2-cycle mode is almost exactly twice as slow as 1-cycle mode. However when RDRAM access is considered, 2-cycle mode rapidly catches up ... essentially equal in speed when Z-buffering is enabled and more pixels are passing the Z-Buffer test" (cited).

These are large rectangles: long spans and an ideal row order. SDK 12.1.2 warns: "Reaching peak bandwidth is difficult ... In small triangles, it is rare to have long horizontal runs ... the pipeline is often stalled, pending memory access" (cited). Real MM triangles will therefore cost more per pixel than the Thar0 numbers. The factor is not found.

### 2. Z-buffer, blending (RMW) and coverage/AA cost

- **Z read.** Z_CMP on, Z fail, all separate, VI on, 2-cycle, IM_RD on: 3.014 ms. Turning IM_RD off cuts this to 2.838 ms. Without Z, IM_RD on, no VI: 2.593 ms (cited, Thar0 L245, L238, L36).
- **Z write.** A Z pass adds the color and Z write-back. In the MM mode, 3.014 ms becomes 4.856 ms, or +61% (cited numbers, inferred ratio).
- **Z fail skips writes.** Thar0 README says Z fail "allows the RDP to skip writing color/depth back to RDRAM". It recommends near-to-far order. SDK 12.7.2 says the same: front-to-back gives better fill rate "because the Z buffer test is a read only (no write) for obscured pixels" (cited).
- **Blending / IM_RD.** IM_RD on makes the RDP fetch color spans "irrespective of whether the span contains any edge pixels". force_blend does not change timing (cited, Thar0 sample_results L14-L16 and README). In MM's mode, IM_RD costs 4.856 vs 3.837 ms, or +27% (cited numbers, inferred ratio).
- **Coverage/AA.** `G_RM_AA_ZB_OPA_SURF` = `AA_EN | Z_CMP | Z_UPD | IM_RD | CVG_DST_CLAMP | ZMODE_OPA | ALPHA_CVG_SEL` (cited, MM `include/PR/gbi.h` L627-L630). The AA cost on the RDP is the IM_RD read above. Coverage lives in the RDRAM 9th bit, which only the RDP and VI use (cited, n64brew RDRAM oldid 5529, line 3; SDK 12.8.1).
- **Z format.** The depth image is a fixed 18 bits per pixel, regardless of color format (cited, n64brew Commands "Set Depth Image"). In RDRAM bytes it occupies 16 bits plus 2 hidden bits per pixel (cited, SDK 12.8.1 "9-bit DRAMs ... two extra bits per color or z pixel").

### 3. Framebuffer and Z bandwidth at 320x240 16-bit (inferred arithmetic)

- One buffer is 320 x 240 x 2 B = 153,600 B (150 KiB).
- One full-screen pass in MM's mode touches about 4 x 153,600 = 614,400 B: color read, Z read, color write and Z write.
- VI scanout of one 16-bit field is 153,600 B. That is 9.2 MB/s at 59.94 fields/s.
- Peak RDRAM bandwidth is 2 B per RDRAM clock at RCLK 250 MHz, so 500 MB/s. This is derived from the ares spec rows `ri.octbyte` (NEC uPD488170L "2 B per tc") and `clock.unit` (RCLK 250 MHz). It is consistent with n64brew RDRAM: the iQue's 1536 MB/s is "about 3x that of N64's RDRAM" (cited).
- The fork's uncalibrated baseline is 4.79 to 5.59 MiB of RDRAM traffic per tick, including RDP (cited, MM `tools/bench/README.md` "Fork baseline"). Suppose every field redraws the full scene, and roughly that much traffic per field minus 2 extra VI fields stays. Then 5.59 MiB - 2 x 0.146 MiB = 5.3 MiB per 16.68 ms, or about 333 MB/s. That is about 67% of the theoretical 500 MB/s peak (inferred). Not all of that traffic repeats per field, because CPU logic stays at 20 Hz. This is an upper-bound sketch, not a measurement.

### 4. RDRAM page and latency behavior

- Each 1 MiB bank keeps one 2 KiB row open. A miss gets a NAck and opens the new row. A dirty row takes longer. The RI tracks bank state for the bottom 8 MiB only (cited, n64brew RDRAM_Interface oldid 5302, "Bank Status Tracking").
- Datasheet costs as modeled by the fork: read hit 10 tc, write hit 4 tc, retry on a clean miss 22 tc, retry on a dirty miss 30 tc, max burst 128 B (cited, ares spec `ri.*` rows, NEC uPD488170L). Several of these rows are "fail" or "pending" against hardware checks in that spec.
- Refresh fires once per VI HSYNC, during HBLANK, with a 52 or 54 cycle delay (cited, n64brew RDRAM_Interface RI_REFRESH).
- Thar0 README says the RDP's memory order is color read, depth read, color write, depth write. It buffers about 8 RGBA16 px per span segment. With FB and ZB in one bank, rows close and reopen "several times per ~8 pixels" (cited). Measured cost of a shared FB+ZB bank in MM's mode: 6.352 vs 4.856 ms, or +31% (cited numbers, inferred ratio). SDK 12.8.1 also tells developers to keep color and Z in different banks (cited).
- **MM's bank layout** (cited code, inferred banks):
  - Framebuffer 1 is `gLoBuffer.framebuffer` at 0x80000500, in bank 0 (MM `spec/spec` L8-L11, `sys_cfb.c` L103-L107).
  - Framebuffer 0 is `gHiBuffer.framebuffer`. Its address is 0x80780000 + 0x4600 + 0x56200 = 0x807DA800, in bank 7 (`include/buffers.h` L26-L55).
  - The Z buffer is `malloc`ed from the system heap in `Graph_ThreadEntry` (`graph.c` L385-L386). The heap starts at `_buffersSegmentEnd` = 0x803824C0, in bank 3 (from a local build map, `~/repos/mm-wt/H1a/build/n64-us/mm-n64-us.map` L38553).
  - So the Z buffer is probably in bank 3, separate from both framebuffers and from VI scanout (inferred; the runtime address was not checked). If that holds, MM is already on Thar0's best row, and bank placement buys nothing.
- CPU uncached read latency is 34 CPU cycles for a 32-bit word (cited, n64-systembench `main.c` L579). n64-systembench has no RDP benchmarks (cited, the file's benchmark table L574-L612 lists none).

### 5. Contention: RDP stalls on CPU, RSP and VI traffic

- DPC_STATUS GCLK "goes to zero any time the RDP is stalled for RDRAM". TMEM_BUSY and the TMEM counter stop during RDRAM stalls (cited, n64brew RDP Interface oldid 5765, lines 213-214 and 354).
- VI has priority over the RDP (cited, Thar0 README; the ares fork treats VI first as a model-choice inference, `ri.rank.vi`). In MM's mode, VI on vs off costs 4.856 vs 4.446 ms, or +9.2% (cited numbers, inferred ratio).
- CPU and RSP contention was not measured on hardware in any source I fetched. Kaze Emanuar states it qualitatively: "whenever you use the CPU, you also slow down the rendering" (cited, SED1666 transcript line 69). Also: "every time you optimize the CPU with the GPU, it gets faster ... you never have to like, use the RAM and the GPU goes very fast" (line 81).
- Under libultra, RDP commands go RSP to RDRAM to RDP. That is "a double memory bandwidth impact" (cited, n64brew RDP Interface line 28-31). libdragon also keeps commands in RDRAM rather than using XBUS. Its reason is that RDP back-pressure would stall the RSP and then the CPU (cited, libdragon `src/rdpq/rdpq.c` L174-L188).

### 6. Texture loads, TMEM, palettes

- TMEM is 4 KiB, in 4 banks, and gives 4 texels per clock (cited, SDK 12.4.1).
- Load Block is "the fastest way to move data from RDRAM into TMEM". It needs 64-bit line padding and has a dxt height limit. Load Tile is slower but more flexible (cited, n64brew Commands, Load Block / Load Tile; libdragon `rdpq.h` L712-L717).
- Load rate is 8 B per clock in the fork. The basis is MiSTer RTL. It conflicts with a reverted cen64 law and has no vendor figure (cited, ares spec `rdp.tmem-load-rate`, pending).
- The loading pipeline cannot run in parallel with rendering. It needs syncs between load and draw (cited, n64brew Pipeline "Loading Pipeline").
- Palettes: TMEM holds 256 16-bit colors, each replicated 4 times, filling the upper 2 KiB (cited, libdragon `rdpq.h` L600-L612). The TLUT load clock cost was not found.
- MM's `gDPLoadTextureBlock` emits SetTextureImage, SetTile, LoadSync, LoadBlock, PipeSync, SetTile, SetTileSize (cited, MM `include/PR/gbi.h` L3917-L3934). Each load therefore costs at least 25 + 50 = 75 clk of fixed sync stall plus the load. A 4 KiB texture is 4096 / 8 = 512 clk + 75, about 9.4 us (inferred, using the fork's pending 8 B/clk). 100 such loads per field would be about 0.94 ms (inferred).

### 7. Sync costs

- Sync Pipe stalls exactly 50 clk, Sync Tile 33 clk, Sync Load 25 clk. "If a Sync ... is queued when it is not needed it simply wastes the full length of time" (cited, n64brew Commands oldid 5941, lines 489-535; Pipeline line 74). The fork models these and passes its sweep (cited, ares spec `rdp.sync-*`).
- Sync Full has no fixed count. It waits for all work and raises the DP interrupt (cited, n64brew Commands 0x29; ares spec `rdp.sync-full`, model-choice).
- Prim Color, Prim Depth, Scissor and Tile Size need no sync (cited, n64brew Pipeline "Synchronization").
- Hazard: emulators do not implement the bugs caused by missing syncs (cited, libdragon `src/rdpq/rdpq.c` L63-L67). **So an emulator fb_hash cannot prove that removing a sync is safe on hardware** (inferred).
- Example scale: every `Gfx_SetupDL*` begins with `gsDPPipeSync` (cited, MM `z_rcp.c` L291). 300 pipe syncs per field would be 15,000 clk, about 0.24 ms (inferred). The actual sync count per MM frame was not found.
- `G_PM_1PRIMITIVE` adds 30 to 40 dead cycles per primitive (cited, SDK 12.2.3). MM uses it in 6 files: `z_parameter.c` (HUD), `z_fbdemo*.c`, `z_vismono.c` and the file-select screens (grep of MM src; assets not checked).

### 8. Span buffer behavior

- For RMW the RDP prefetches a span into an on-chip span buffer, and has "enough on-chip RAM to hold several span buffers" (cited, SDK 12.2.3).
- There is no buffering across rows. Wide triangles are more efficient than thin tall ones (cited, n64brew Commands "Set Color Image").
- The fork models a 64 B span-RAM half (measured by snapper64, cited via the ares spec `rdp.span-ram-half`) and 16 B segments. The 16 bpp color half is unmeasured (32 px, model-choice).

### 9. Clears and scissor

- **MM clears Z every Play frame** with FILL mode: fill color `GPACK_RGBA5551(255,255,240,0)` replicated, then a full-screen fill rectangle and a pipe sync (cited, MM `z_rcp.c` L1483-L1496, L1512-L1516).
- **MM clears color only when there is no skybox and the room is not an image room.** Otherwise the skybox overwrites the screen (cited, MM `z_play.c` L1147-L1156).
- Fill clear cost: 153,600 B / 8 B per clk = 19,200 clk = 0.307 ms (inferred from SDK 12.1.4). The RDRAM write side, using the fork's 128 B write (17 clk wire + 1 clk gap per the `ri.overhead-write` note), gives 1,200 bursts x 18 clk = 21,600 clk = 0.346 ms (inferred; fork values, not hardware). Fill mode itself is not measured on hardware in any source fetched (ares spec `rdp.fill-copy-rate` is pending).
- **Skipping the Z clear:** the skybox and other no-Z draws do not write Z. Opaque geometry then compares against last frame's Z, so pixels change. Classified: **differs**.
- **RSP DMA Z clear** (libdragon `RDPQCmd_ClearZBuffer`, "quite a bit faster than using RDP", `src/rdpq/rsp_rdpq.S` L893-L942; host side `rdpq_attach.c` L38-L63). This moves the clear off the RDP onto RSP and RDRAM. The fill writes replicated bits into the hidden 9th bits (SDK 12.8.2). What an RSP DMA write puts in the hidden bits was not found. libdragon also needs an RDP fence first. Classified: **may differ** until the hidden-bit result is proven on hardware. The RSP is also over budget (see Budget above).
- **Scissor:** FILL and COPY rectangles scissor to 4-pixel boundaries. 1- and 2-cycle scissor the same as triangles (cited, SDK 12.3.1). Narrowing the scissor changes pixels. MM's scissor is already the full screen (`z_rcp.c` L1480), so there is nothing to gain.

### 10. Minimum RDP time per MM field (inferred)

Assumptions:
- MM's Z buffer is in a bank apart from both framebuffers (inferred above).
- VI scans the other framebuffer.
- One opaque layer covers the screen in SETUPDL_25 mode at Thar0's large-rectangle efficiency.
- No texture load cost is counted.

Steps:
1. NTSC field = 1 / 59.94 Hz = 16.683 ms. The 90% budget is 15.015 ms (preferences line 3).
2. Z clear (FILL): 0.307 to 0.346 ms (section 9).
3. One full-screen pass, 2-cycle, AA_ZB_OPA_SURF, Z pass, VI on, separate banks: 4.856 ms (Thar0 L307).
4. Floor = 0.35 + 4.86 = **5.21 ms = 31% of a field**.
5. Headroom under budget = 15.015 - 5.21 = 9.81 ms. That is 9.81 / 4.856 = 2.0 more Z-pass full-screen layers, or 9.81 / 3.014 = 3.3 Z-fail layers.
6. If the screen has no skybox, the color clear adds another 0.31 to 0.35 ms. If a skybox is drawn instead, its cost replaces the clear; skybox cost was not checked.

Reading: the RDP fits about 3 screen-equivalents of Z-tested opaque fill per field, at best-case span efficiency. Small triangles, texture loads, syncs, translucent layers, CPU/RSP contention and overdraw all eat into that. Whether retail MM fits depends on retail RDP busy per frame. The bench does not isolate that number today. The 26 to 34 ms "gfx task start to full sync" includes RSP-feed time. This slice cannot tell how much of it is RDP-bound.

One key consequence (inferred): interpolated presentation means every field renders a full image. So per-field RDP work equals retail per-tick RDP work, plus interpolation costs. No fidelity-preserving lever in this slice reduces pixel work. Fidelity-preserving RDP gains come only from removing stalls: syncs, loads, contention and command traffic. RDP busy per retail frame must already be under about 15 ms, minus whatever those stalls recover.

### Techniques

| Technique | Resource | Expected gain | Fidelity risk | Effort | License if reused | Sources |
|---|---|---|---|---|---|---|
| Measure true RDP busy per frame (DPC_BUF_BUSY, PIPE_BUSY, CMD_BUSY, DPC_CLOCK) per tick in the bench | RDP (diagnostic) | Decides feasibility; no direct gain | identical (instrumentation in BENCH builds only) | S | n/a | n64brew RDP Interface |
| Keep FB0, FB1 and Z in distinct banks; verify the runtime Z address | RDRAM, RDP | 0 if already separate (inferred likely). Up to -24% per full-screen pass if Z shares a bank with the drawn FB (6.352 to 4.856 ms) | identical (same pixels; memory layout only; check that heap shifts do not change logic) | S | n/a | Thar0 L307, L337; SDK 12.8.1; MM graph.c |
| Cut CPU and RSP RDRAM traffic while the RDP runs (cache-friendly CPU code, fewer DMAs) | RDRAM, RDP | VI alone costs +9% on a full pass; CPU/RSP share not measured | identical | M to L (overlaps slices on CPU/RSP) | n/a | Thar0 VI on/off; Kaze SED1666 L69, L81 |
| Remove redundant Sync Pipe / Tile / Load | RDP | 25 to 50 clk each; ~0.24 ms per 300 pipe syncs (inferred) | identical only if each removed sync is provably unneeded; **emulator fb_hash cannot prove it** (libdragon rdpq.c L66-L67). Needs a hazard rule plus hardware evidence | M | n/a | n64brew Commands, Pipeline |
| Skip redundant texture and TLUT loads (TMEM already holds the bytes) | RDP, RDRAM | ~9 us per 4 KiB load avoided (inferred) | identical if TMEM contents and tile descriptors are byte-identical at every draw | M | n/a | n64brew Commands; MM gbi.h L3917-L3934 |
| LoadTile to LoadBlock where the texture layout allows | RDP, RDRAM | Not quantified; n64brew says only "faster" | identical only if the resulting TMEM bytes are identical, including odd-line word swap | M | n/a | n64brew Commands Load Block/Load Tile; libdragon rdpq.h L712-L717 |
| Feed RDP commands without the RDRAM round trip (XBUS or a direct-DL path) | RDRAM | Saves the command bytes once; size per frame not found | identical (same command stream) if back-pressure does not change ordering; risk of RSP stalls (libdragon rdpq.c L174-L188) | L (microcode) | libdragon: public domain (Unlicense text in LICENSE.md) | n64brew RDP Interface L28-L31 |
| Front-to-back sort of opaque draws | RDP, RDRAM | Z-fail pass 3.014 vs Z-pass 4.856 ms per full screen (-38% on occluded pixels) | **may differ**: AA_ZB_OPA_SURF blends edge pixels with memory color and accumulates coverage. ZMODE_OPA treats depth within dz as the same surface. Order changes results. Accept only if fb_hash is identical per scene | M | n/a | Thar0 README; SDK 12.7.2; MM gbi.h L627 |
| Cull geometry that is fully hidden | RSP, RDP | Up to that geometry's Z-fail cost | identical only if the culled triangles would fail Z on every pixel beyond the AA dz tolerance; otherwise **may differ** | L | n/a | SDK 12.7.2 |
| Drop G_PM_1PRIMITIVE in HUD and other DLs | RDP | 30 to 40 clk per primitive | **may differ** (it exists for span coherency on overlapping primitives) | S | n/a | SDK 12.2.3 |
| RSP-DMA Z clear instead of FILL | RDP to RSP | ~0.31 to 0.35 ms of RDP per field moved to RSP | **may differ** (hidden 9th-bit content of an RSP DMA write not found) | S to M | libdragon: public domain | libdragon rsp_rdpq.S L893-L942; SDK 12.8.2 |
| Skip the Z clear | RDP | ~0.33 ms per field | **differs** (no-Z draws leave stale Z) | S | n/a | MM z_play.c L1147-L1156 |
| 1-cycle fog / non-AA / IM_RD off / Z modes changes | RDP | IM_RD off: -21% (4.856 to 3.837 ms); 1-cycle: ~0 when memory-bound | **differs** (render mode changes pixels and VI AA coverage) | S | n/a | Thar0 L300, L304, L307; SDK 12.7.5 |
| Narrow the scissor or the region drawn | RDP | proportional to pixels | **differs** | S | n/a | SDK 12.3.1 |

### Sources fetched

Web (fetched 2026-10-25):
- https://n64brew.dev/index.php?title=Reality_Display_Processor&action=raw (oldid 5675)
- https://n64brew.dev/index.php?title=Reality_Display_Processor/Pipeline&action=raw (oldid 5674)
- https://n64brew.dev/index.php?title=Reality_Display_Processor/Commands&action=raw (oldid 5941)
- https://n64brew.dev/index.php?title=Reality_Display_Processor/Interface&action=raw (oldid 5765)
- https://n64brew.dev/index.php?title=RDRAM&action=raw (oldid 5529)
- https://n64brew.dev/index.php?title=RDRAM_Interface&action=raw (oldid 5302)
- Revision IDs from https://n64brew.dev/w/api.php (query prop=revisions). Permalink form: `index.php?title=<page>&oldid=<revision>` on n64brew.dev
- https://n64brew.dev/index.php?title=Reality_Display_Processor/Timing&action=raw (empty: no such page)
- https://ultra64.ca/files/documentation/online-manuals/man-v5-1/pro-man/pro12/12-01.htm through 12-08.htm (N64 SDK Programming Manual ch. 12)
- https://softwareengineeringdaily.com/wp-content/uploads/2024/03/SED1666-Kaze_Emanuar.txt (Kaze Emanuar interview transcript)

Repos (shallow clones in scratchpad/clones, 2026-10-25):
- https://github.com/DragonMinded/libdragon branch preview @ 39d0d6096130836a65710da6730290701d060272. License: public domain dedication (LICENSE.md). Cited: https://github.com/DragonMinded/libdragon/blob/39d0d6096130836a65710da6730290701d060272/include/rdpq_mode.h#L333-L337 , .../src/rdpq/rdpq.c#L63-L67 , .../src/rdpq/rdpq.c#L96-L97 , .../src/rdpq/rdpq.c#L174-L188 , .../include/rdpq.h#L600-L612 , .../include/rdpq.h#L712-L717 , .../src/rdpq/rsp_rdpq.S#L893-L942 , .../src/rdpq/rdpq_attach.c#L38-L63
- https://github.com/Thar0/RDP-Timing-Tests @ a81ced93b28d51b920b5f9470d0bcc5f03726ae4. License: MIT (2023 Tharo). Cited: https://github.com/Thar0/RDP-Timing-Tests/blob/a81ced93b28d51b920b5f9470d0bcc5f03726ae4/sample_results.txt (lines given in tables), .../README.md , .../src/test_main.c#L4-L5 (320x240), #L128 (16-bit VI)
- https://github.com/rasky/n64-systembench @ 845635c68c759c92595edaf5369e76332ebb321d. No LICENSE file found. Cited: https://github.com/rasky/n64-systembench/blob/845635c68c759c92595edaf5369e76332ebb321d/src/main.c#L574-L612
- https://github.com/HackerN64/HackerSM64 @ 8953c07aa4592d65ffc974b596c97ae42778a221 (clone pre-existed; read include/config/config_graphics.h, src/game/game_init.c). No RDP throughput figures found.

Local (read-only):
- ares fork spec ~/repos/ares-wt/mm60-runner/docs/spec/n64-timing.md @ 54f35f9c707b63edcb61841b2d7846fea4bca1b7 (sections clock, ri, rdp)
- MM decomp wScottSh/mm-decomp-60fps @ caeb013ac597d556367c30e6f5d292100f93e58f: src/code/z_rcp.c L290-L298, L1450-L1553; src/code/z_play.c L1147-L1156; src/code/sys_cfb.c L103-L107; src/code/graph.c L385-L386; src/code/main.c L66-L71; include/buffers.h L10-L55; include/PR/gbi.h L627-L630, L942, L3917-L3934; spec/spec L8-L11; tools/bench/README.md L113-L131
- ~/repos/mm-wt/H1a/build/n64-us/mm-n64-us.map L38553 (`_buffersSegmentEnd = 0x803824c0`)

### Gaps

- **Retail MM RDP busy time per frame: not found.** The bench reports task start to full sync only. This is the number that decides RDP feasibility.
- Runtime Z-buffer address not verified. Bank 3 is inferred from the heap start, not read from a run.
- Hidden 9th-bit content after RSP or CPU writes: not found in n64brew RDRAM, the SDK ch. 12 or libdragon.
- FILL and COPY throughput not measured on hardware in any fetched source. The fork marks it pending.
- TLUT load cost and LoadTile vs LoadBlock clock counts: no numbers found. The fork's 8 B/clk is RTL-based and pending.
- CPU and RSP contention cost to the RDP: no hardware measurement found. Thar0 covers only VI.
- Per-frame counts for MM (syncs, texture loads, pixels drawn or depth complexity, RDP command bytes): not measured. The fork could count them.
- Kaze Emanuar: only the SED1666 transcript was fetched. No primary RDP-specific text or numbers from him were found (searched the web for Kaze RDP/fillrate/Z-buffer). HackerSM64 config has no RDP throughput figures.
- rasky n64-systembench has no RDP tests.
- Small-triangle span efficiency vs Thar0's full-screen rectangles: factor not found.

## Appendix E. CPU, compilers, and prior 60 fps attempts

### Summary

- Cache stalls are a big share of CPU time per tick. Hardware per-miss costs (cited) times the fork's miss counts (cited) give roughly 14 to 25 ms of stall per 50 ms tick window (inferred). Shrinking the hot working set is the CPU lever most likely to pay without touching logic.
- zeldaret/mm has had a GCC build since PR #1791 (merged 2025-02-02). A GCC build was tested on hardware and played through. The upstream CI does not build it, so nothing keeps it working (cited).
- That GCC build is not logic-safe as configured. Its flags still enable `-freciprocal-math`, `-fno-signed-zeros` and `-ffinite-math-only`, and GCC enables `-fstrict-aliasing` at `-Os`. All of these can change float or aliasing results. A logic-safe GCC build needs strict IEEE flags plus `-fno-strict-aliasing -fwrapv`, and a lockstep check against IDO (inferred).
- FP contraction cannot change results on this CPU. The VR4300 has no fused multiply-add instruction (cited, manual instruction list and FPU table).
- HackerSM64 and HackerOoT both use GCC at `-Os` with partly relaxed float math. Each has a CPU/RCP profiler. Neither repo has a LICENSE (cited).
- Kaze's primary text that I could read stresses that CPU and RCP share RDRAM, so CPU work slows rendering, and that code should fit in cache lines. His "How Optimizations made Mario 64 SLOWER" repo rolls unrolled code back into loops (cited). I could not read his video transcripts (gap).
- Every console or emulator 60 fps code for OoT and MM I found changes the update divisor (R_UPDATE_RATE) from 3 to 1. That runs logic three times as often. Only position integration and animation scale with the divisor, so physics, timers and jumps break (cited forum posts plus decomp code). None keeps game speed. Kaze's 2018 SM64 60 fps mod kept speed by running physics every other frame (secondary source), on emulator only.
- I did not try `make COMPILER=gcc`. No MIPS GCC is installed (measured).

### 1. Kaze Emanuar's CPU findings

Primary text I could read:

- SED podcast transcript (2024). Kaze: "the CPU and the GPU ... share the same memory. So, whenever you use the CPU, you also slow down the rendering". Also: "a lot of my optimizations is like making sure that stuff is like better aligned to cache lines, making sure that like, an entire thread on the CPU ... if it's into just one block of cache ... you never have to like, use the RAM and the GPU goes very fast" (cited, transcript lines 69 and 81). He says his optimized mods "run like 60 FPS" and that "on the hardware, the max is 60" (cited, lines 73 and 77).
- Repo `KazeEmanuar/SM64-but-some-optimizations-are-removed`, the git repo for the video "How Optimizations made Mario 64 SLOWER" (2024-09-14, per the YouTube description). The README says "This makes the game run better" and links commit `b46813647`. In that commit `mtxf_mul` and `mtxf_billboard` go from hand-unrolled code to small loops. It also deletes `cur_obj_within_12k_bounds`, and it adds a profiling FPS counter. The point is that smaller code beats "optimized" unrolled code on this machine (inferred from the diff plus the video title). The repo is IDO by default (`COMPILER ?= ido`) and uses `-O2` when built with gcc (cited, Makefile L23, L171).
- Bit-identity of that demo: the new `mtxf_mul` writes into `dest` while it still reads `a`. The original goes through a temp. So in-place calls (`dest == a`) now produce different results. That is a behavior change (inferred from the diff).
- "Finding the BEST sine function for Nintendo 64" (2023-06-24). The description only lists chapters, from "Mario 64's sine" and "Ocarina of Time's sine" to 3rd, 4th and 5th order approximations. HN commenters summarize the result as "200 microseconds saved" and "1KB of RAM saved" (cited, HN thread, secondary). A polynomial sine gives different bits from the table, so it changes logic. Fidelity: differs.
- Secondary notes (sawaiz TAS notes) list collision partitioning, early outs, cache-friendly layouts and shared billboard matrices. These are not primary text, and most of them change behavior.

Bit-identical vs behavior-changing, inferred for MM:
- Identical in principle: code layout and alignment (`-falign-functions`, hot/cold placement), loop rolling or unrolling that keeps the same operation order, data placement that keeps struct layout, and prefetch or cache ops (`cache` instruction). These keep identical results only if the compiler keeps the same IEEE op sequence.
- Changes behavior: new sin/cos/atan approximations, collision algorithm rewrites that change which surface wins a tie, culling that skips updates, `-freciprocal-math` and `-fassociative-math`.

### 2. HackerSM64 and HackerOoT

HackerSM64 @ `8953c07a` (2026-09-12):
- Default `COMPILER ?= gcc`, clang is optional (Makefile L44). Global `-Os` (L164). `DEFAULT_OPT_FLAGS` adds `-ffinite-math-only -fno-signed-zeros -fno-math-errno` (L167). `SAFETY_OPT_FLAGS = -ftrapping-math -fno-associative-math` (L171).
- Per-file tuning: collision gets `-fno-inline`, `max-unrolled-insns=100` and `-falign-functions=32`. math_util gets no unroll and `-falign-functions=32`. The graph node renderer gets similar flags. The main code gets `-finline-limit=1`, `-ffunction-sections` and `-fdata-sections` (L174-219). A Makefile comment says any `-finline-limit` made math_util slower (L206-207). Actors and levels build at `-Ofast -mlong-calls` (L453-454). The global C flags include `-fno-strict-aliasing -fno-inline-functions -fwrapv` (L512).
- Profiler: `src/game/profiling.c/.h` reads the CP0 Count register inline (`OS_GET_COUNT_INLINE`, profiling.h L16). It times per subsystem (controllers, behaviors, Mario, gfx, collision, camera, audio, RSP gfx/audio, RDP TMEM/pipe/cmd) (profiling.h). It is on by default (`#define USE_PROFILER`, config_debug.h L25).
- Claims: the README says only "software optimizations" and "detailed performance profiling information" (README L3, L25). It publishes no speedup number. CREDITS lists Kaze for "Graph node optimisations, automatic optimal collision distance" (CREDITS.md L16).
- License: no LICENSE file. The GitHub license API returns 404 (measured).

HackerOoT @ `e77e6683` (2025-11-27):
- GCC only (Makefile L16, L186-189). It always defines `-DNON_MATCHING -DAVOID_UB` (L187). `OPTFLAGS := -Os` plus `-ffast-math -fno-unsafe-math-optimizations` (L228, L239).
- Per-overlay overrides: `ovl_Item_Shield` and `ovl_En_Part` at `-O2`, and `ovl_Item_B_Heart` and `ovl_Bg_Mori_Hineri` at `-O0` (L530-533). This suggests those files misbehave under `-Os` (inferred, no comment says why).
- Its `iso` target builds a few libultra files with IDO (`CC := $(CC_IDO)`, Makefile L536-560), so mixing IDO and GCC objects in one ROM has precedent.
- Profiler: `src/debug/profiler.c` records per-thread events via `osGetTime` into triple-buffered states, with a "virtual FPS" mode (profiler.c L1-40). `ENABLE_PROFILER true` in config_debug.h L26.
- It uses F3DEX3 by default and adds `occlusionplanes.c`, which is RSP/RDP-side culling (README L32, L56).
- License: no LICENSE file. The GitHub license API returns 404 (measured).

### 3. IDO vs GCC for zeldaret/mm

Local R1 Makefile (R1 HEAD `6d2e0053`, contains upstream PR #1791 merge `84a89c8f`, measured) matches upstream @ `710e1a7d`:
- `COMPILER ?= ido` (L49). For gcc it sets `-DCOMPILER_GCC` and forces `NON_MATCHING := 1` (L94-98), and `NON_MATCHING` adds `-DAVOID_UB` (L100-104).
- GCC flags: `-G 0 -march=vr4300 -mtune=vr4300 -mfix4300 -mabi=32 -mno-abicalls -mdivide-breaks`, `-fno-builtin-sinf -fno-builtin-cosf -funsigned-char`, `OPTFLAGS := -Os -ffast-math -ftrapping-math -fno-associative-math`, `-mips3` (L221-236). Overlays add `-fno-merge-constants -mno-explicit-relocs -mno-split-addresses` (L498). `src/gcc_fix/missing_gcc_functions.c` supplies `memcmp` and the 64-bit helpers, because GCC expects them even when freestanding.
- IDO flags: `-O2 -g3 -mips2` (R1 Makefile L261-262), with per-directory `-O1`/`-O2` overrides (L447-493).

Support status:
- PR #1791 "GCC Compiler Support" (Thar0, merged 2025-02-02): "Thanks to fig02 for testing a build on hardware" (cited).
- PR #1785 (merged 2025-01-20) added AVOID_UB fixes found in "the GCC playthrough". PR #1789 (merged 2025-01-27) documents vanilla bugs found in that playthrough (cited).
- Upstream CI (`.github/workflows/ci.yml`) builds n64-us matching and n64-jp-1.1 with `NON_MATCHING`. Both use IDO. No GCC job exists (cited). So the GCC path is supported but not maintained by CI. It booted on hardware once, in early 2025.

Float and UB risk under GCC (bit-identical logic required):
- GCC docs say `-ffast-math` sets `-fno-math-errno -funsafe-math-optimizations -ffinite-math-only -fno-rounding-math -fno-signaling-nans -fcx-limited-range -fexcess-precision=fast`, and that `-funsafe-math-optimizations` enables `-fno-signed-zeros -fno-trapping-math -fassociative-math -freciprocal-math` (cited). The mm flags turn back on only trapping math and turn off only associative math. So `-freciprocal-math`, `-fno-signed-zeros` and `-ffinite-math-only` stay on (inferred from the docs plus the flags).
  - `-freciprocal-math` can turn `x / c` into `x * (1/c)`. That changes low bits. On VR4300 `div.s` costs 29 cycles and `mul.s` 5 (cited, Table 7-14), so GCC has a strong reason to do it.
  - `-fno-signed-zeros` can fold `x + 0.0f` to `x` and `0 * x` to `0`. That changes the sign of zero, which matters for atan2-style yaw math (inferred).
  - `-ffinite-math-only` lets NaN and Inf compares fold (inferred).
- FP contraction: GCC defaults to `-ffp-contract=fast` outside strict ISO modes (cited). The VR4300 ISA has no `madd.s`/`msub.s`. The manual's instruction list has no MADD and Table 7-14 lists none (cited). So contraction cannot happen on this target (inferred).
- Excess precision: MIPS single ops are IEEE single, so register vs stack placement does not change results (inferred).
- Aliasing: GCC enables `-fstrict-aliasing` at `-Os` (cited). The mm GCC flags do not pass `-fno-strict-aliasing`. Decomp code type-puns in places (PR #1483 "Fix aliasing in Cutscene_SetActorCue"). HackerSM64 passes `-fno-strict-aliasing -fwrapv` for this reason (inferred).
- UB that depends on data layout: retail reads out of bounds into whatever data follows. Under AVOID_UB the decomp pads arrays so those reads see the retail values. Examples: `z_player.c` L18608, `z_player_lib.c` L1311, `z_en_dnq.c` L465, `z_parameter.c` L4529 (cited). These only cover the reads people found. Any other OOB or uninitialized read changes under a new layout (inferred).
- Heap layout: overlay sizes change under GCC, so ZeldaArena addresses change (see section 4). Logic that depends on addresses or on uninitialized heap contents could diverge (inferred).
- Verdict (inferred): a GCC build can only be used for logic if (a) it uses strict IEEE (`-fno-fast-math`, or at least `-fno-reciprocal-math -fsigned-zeros -fno-finite-math-only`) plus `-fno-strict-aliasing -fwrapv`, and (b) it passes a lockstep RAM-state diff against the IDO build across long input replays. A middle path keeps IDO for gameplay code and uses GCC only for new presentation code, such as interpolation. That carries no logic risk.

GCC trial: not run. `command -v mips-linux-gnu-gcc mips64-elf-gcc mips64-linux-gnu-gcc` found none. Only `mips-linux-gnu` binutils are installed (measured). No worktree was created.

### 4. VR4300 caches, RDRAM cost, overlays

Cited, NEC VR4300 User's Manual U10504EJ7V0UM00 (from n64brew):
- I-cache 16 KB, D-cache 8 KB. Each is 64 bits wide with 1-clock access, and the two are accessed in parallel (sec. 1.4.7, p.47).
- I-cache: direct-mapped, virtually indexed, physically tagged, 32-byte line (sec. 11.2.1, p.276).
- D-cache: write-back, direct-mapped, virtually indexed, physically tagged, 16-byte line (sec. 11.2.2, p.277). A dirty line is written back only when it is replaced or by a CACHE op (sec. 11.3.1). The refill is critical-doubleword-first (sec. 11.3.2).
- I-miss stall = 1 + 1 + (1-2) + 2 + M + 8 + 1 PClocks, where M is memory access time (Table 11-2).
- TLB: 32 fully associative entries, each mapping a pair of 4 KB to 16 MB pages (sec. 1.5.1).
- FPU: add.s 3, mul.s 5, div.s 29, sqrt.s 29 cycles. Double precision: mul 8, div 58 (Table 7-14).
- CPU clock 93.75 MHz (n64brew VR4300 page).

Hardware cycle costs (cited, hacktarux/n64-systembench `src/main.c` @ `7c2f96d4` L1497-1540; these are expected values in the benchmark table, and the ares fork spec reads them as hardware-derived):
- Uncached RDRAM read: 34 CPU cycles (U8R through U32R), 35 for U64R. Uncached 32-bit write: 46.
- I-cache line fill: 49 to 61 cycles (Begin Hit 49, Fill Hit 54, Fill Miss 61).
- D-cache miss: 35 cycles (W0), 40 with a second word, 44 on an RDRAM row miss.
- The ares fork spec also cites a nemu64 hardware load-miss mean of 42.5 cycles (`~/repos/ares/docs/spec/n64-timing.md` L549, fork HEAD `54f35f9c`).

Overlay loading in MM (upstream @ `710e1a7d`):
- `Actor_LoadOverlay` allocates overlay RAM from ZeldaArena. It uses `ZeldaArena_Malloc` for normal overlays, `ZeldaArena_MallocR` for permanent ones, and one shared `absoluteSpace` block of `AM_FIELD_SIZE` for absolute ones. Then it calls `Overlay_Load` (z_actor.c L3457-3497).
- `Overlay_Load` DMAs, relocates, zeroes bss, then calls `osWritebackDCache` and `osInvalICache` on the range (loadfragment2.c L144-191).
- Effect (inferred): every actor overlay's I-cache index depends on heap state at spawn time. With a 16 KB direct-mapped I-cache, two hot overlays 16 KB apart evict each other on every call. Layout differs per scene and per spawn order, so conflict misses are not designed, they are luck. The ares fork's own spec treats code layout as a variable and runs each bench at 8 pads within one 32 B I-cache line, because results changed with layout (n64-timing.md L7).
- Lever (inferred): place hot code (actor update and draw loops, `Actor_UpdateAll`, collision, `SkelAnime`, matrix code, `Gfx` builders) so it does not alias in the I-cache. Two ways: align overlay allocations to I-cache-friendly offsets, or link hot code from overlays into a resident segment. Either changes code addresses and heap addresses, but not the logic ops. The risk comes from UB that depends on layout (section 3).

Estimate of what the measured misses cost (inferred; inputs cited):
- Inputs: the fork baseline gives I$/D$ misses per frame of 21.1k/20.3k (South Clock Town), 18.9k/21.6k (Termina Field), 16.6k/14.5k (Mountain Village), 21.3k/23.6k (Great Bay Coast) (tools/bench/README.md, "Fork baseline"). The XPROF slot 0 window runs from `Bench_BeginFrame` to `Bench_EndFrame` (src/code/bench.c L263-311). One frame there is one 20 Hz tick, a 50 ms window, and it likely counts misses from every thread that runs in it, audio included (inferred).
- I$: 21.1k x 54 cycles = 1.14 M cycles; / 93.75 MHz = 12.2 ms. Range over scenes and 49-61 cycles: 16.6k x 49 = 0.81 M = 8.7 ms, up to 21.3k x 61 = 1.30 M = 13.9 ms.
- D$: 20.3k x 35-44 = 0.71-0.89 M cycles = 7.6-9.5 ms (SCT). Range: 14.5k x 35 = 5.4 ms (Mountain Village) to 23.6k x 44 = 11.1 ms (GBC). Dirty-line writebacks add more (the `dc_wb` column exists but the README does not report it).
- Total stall per tick: about 14 ms (Mountain Village) to 25 ms (GBC). SCT is about 20 ms. Compare game ms 18-31 per tick. Even if half of these misses fall in other threads, stalls are plausibly a third or more of game-thread time (inferred).
- Caveats: the per-miss costs come from an idle-bus microbenchmark. During a frame the RDP and RSP compete for RDRAM, so real per-miss cost is higher (inferred). The fork is not hardware-calibrated (README). Miss counts are the fork's model.
- Implication (inferred): halving I$ misses through layout would save about 4-7 ms per tick. That is real money against the 2-4x CPU cut the program needs, but it does not close the gap alone.

### 5. Prior 60 fps attempts (OoT, MM, SM64)

- OoT US 1.0 "60FPS+JumpFix+CrawlFix" GameShark code (Project64 forum, 2015-07-28, posted by Tasoulis, credited to theboy181; retroben says HyperLamer found the "Game Execution Speed" code and retroben did the jump and crawl fixes) (cited, Wayback copy of pj64 thread 5208; also the Emulation General wiki "30/60 FPS cheat codes" page).
  - Code: `D11C8680 0000 / 801C6F2D 009C / D11C8680 0000 / 801C6FA1 0003 / D31C8680 0000 / 801C6F2D 00D8 / D31C8680 0000 / 801C6FA1 0001`. It writes 3 or 1 into the byte at 0x801C6FA1, depending on a flag at 0x801C8680. That matches R_UPDATE_RATE (SREG(30)) being set to divisor 3 or 1 (inferred; R_UPDATE_RATE = SREG(30) cited from the OoT and MM `include/regs.h`). The second write tunes another register for jumps.
  - Reported effects: "the physics get all messed up, whereas SM64's code just runs faster with unaltered physics". Deku Scrub nuts explode, and "deku sticks DOES burn fast". Users fell back to 30 fps mode (divisor 2) (cited, same thread). EGW notes "Some animations are still awkward or too quick" (cited).
- MM: the same thread gives only a "Lag Fix" `8009B252 000D` for MM (U). It rewrites a 0x020D value to 0x0D0D. 0x20D = 525 is the NTSC line count, so this likely alters VI timing (inferred), and it was tested only on emulator. mm-rando issue #142 (2021-03-26) describes finding the 1/3/2 fps-mode byte in Project64 and setting it to 1 for 60 fps. Its "minor issues" are "mostly that item animations do not run at 60fps" (cited). I found no MM 60 fps code verified on console. Search terms: "Majora's Mask 60fps GameShark code N64", "Majora's Mask 60 fps cheat code N64 console framerate".
- Why divisor codes break game speed (MM decomp @ `710e1a7d`, cited code, inferred effect):
  - `GameState_SetFramerateDivisor` sets R_UPDATE_RATE and `gFramerateDivisor*` (game.c L33-44).
  - Only 13 source files read the divisor (measured by grep).
  - `Actor_UpdatePos` scales velocity by `divisor * 0.5` (z_actor.c L1202-1217, set from z_play.c L937). SkelAnime scales animation by `divisor * 0.5` (z_skelanime.c L1321).
  - Accelerations, gravity, frame timers, cutscene frames, the three-day clock step and most actor state machines count ticks and do not scale. At divisor 1, gravity applies 3x as often while position moves 1/3 as far per tick. So jump height drops to about 1/3 and every timer runs 3x fast.
  - None of these codes reached a locked 60 on console. The forum users ran them on emulators with overclock or counter-factor settings.
- SM64 (Kaze, 2018): an emulator-only 60 fps mod. Game speed was kept by running "the game's physics only on every other frame", with interpolated animations, and later fixes to "physics so they can work at proper speed" and "global timer stuff" (cited, TechSpot and DSOGaming, secondary). DSOGaming states the original console could not run it at 60. Kaze's 2024 demo has a comment "remove the command below to unlimit the framerate (up to 60fps)" on the second vblank wait in `display_and_vsync` (cited, commit `b46813647`, game_init.c). Vanilla SM64 runs one logic update per loop iteration, so unlocking it this way speeds up the game (inferred). That matches the forum's "SM64's code just runs faster".
- Lesson for this program (inferred): every prior attempt changed the logic rate. None kept a 20 Hz tick with interpolated presentation. The program's model (logic at divisor 3, extra fields interpolated) has no console precedent for OoT or MM that I found. Its closest relatives are the PC ports (appendices A and B).

### Techniques

| technique | resource | expected gain | fidelity risk | effort | license if reused | sources |
|---|---|---|---|---|---|---|
| I-cache-aware placement of hot code (align overlay allocs, or link hot overlay code resident) | CPU (I$), RDRAM bandwidth | up to about 4-7 ms per tick if I$ misses halve (inferred) | identical logic ops. May differ if layout-dependent UB exists. Needs lockstep check | medium | none (own code) | VR4300 manual 11.2.1; systembench L1521-1524; z_actor.c L3457-3497; bench README |
| D-cache working-set shrink that keeps struct layouts (allocation order, fewer passes over big arrays) | CPU (D$), RDRAM | a few ms (inferred) | identical if struct layouts and op order are kept | medium-high | none | VR4300 manual 11.2.2; systembench L1526-1531 |
| `cache` instruction prefetch / Create Dirty Exclusive for write-only buffers (display lists, matrices) | CPU (D$ fill), RDRAM | saves the fill read on write-only lines (inferred) | identical (no logic change) | low-medium | none | VR4300 manual ch. 11 (CACHE ops) |
| GCC build with strict IEEE + `-fno-strict-aliasing -fwrapv`, gameplay code | CPU | unknown. Published HackerSM64/OoT gains are not quantified (gap) | may differ (UB, layout, compiler bugs). Needs lockstep RAM diff | high | mm: none found (repo has no LICENSE) | mm Makefile L221-236; GCC docs; PRs #1785, #1789, #1791 |
| GCC only for new presentation code (interpolation), IDO for logic | CPU | enables modern code for new work | identical logic | low-medium | none | HackerOoT Makefile L536-560 sets `CC := $(CC_IDO)` per file, a precedent for mixing compilers in one link |
| upstream mm GCC flags as-is (`-ffast-math -ftrapping-math -fno-associative-math`) | CPU | unknown | differs (reciprocal, signed zeros, finite math, strict aliasing) | low | n/a | GCC docs; mm Makefile L235 |
| Kaze-style math replacement (polynomial sin/cos, new collision) | CPU | about 0.2 ms per frame for sine (HN estimate, secondary) | differs | medium | Kaze repo has no LICENSE | YouTube xFKFoGiGlXQ desc; HN 36496691 |
| HackerSM64-style per-file flags (`-falign-functions=32`, `-fno-inline` for collision) | CPU (I$) | not published | identical only if no float flag relaxation | low under GCC | no LICENSE | HackerSM64 Makefile L164-219 |
| Profiler: CP0 Count per subsystem (HackerSM64) or per-thread event log (HackerOoT) | measurement | n/a | identical (debug only) | low | no LICENSE: reimplement, don't copy | profiling.h L16; profiler.c L1-40 |
| GameShark divisor code (R_UPDATE_RATE 3 to 1) | n/a | n/a | differs: logic 3x rate, physics and timers break | trivial | n/a | pj64 thread 5208; mm-rando #142; game.c L33-44 |

### Sources fetched

All fetched or cloned 2026-10-25.

Repos cloned (`git clone --depth 1`, SHA recorded):
- https://github.com/zeldaret/mm @ `710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d` (commit date 2026-10-20). No LICENSE file. GitHub license API 404.
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/Makefile#L94-L104
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/Makefile#L221-L236
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/Makefile#L497-L498
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/.github/workflows/ci.yml
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_actor.c#L1202-L1217
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_actor.c#L3457-L3497
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/boot/libu64/loadfragment2.c#L144-L191
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/game.c#L22-L44
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_play.c#L937
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_skelanime.c#L1321
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/overlays/actors/ovl_player_actor/z_player.c#L18608
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_player_lib.c#L1311
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/overlays/actors/ovl_En_Dnq/z_en_dnq.c#L465
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/src/code/z_parameter.c#L4529
  - https://github.com/zeldaret/mm/blob/710e1a7d50a1f987f5fc55abca37f7fdcc8ee43d/include/regs.h#L73
- https://github.com/HackerN64/HackerSM64 @ `8953c07aa4592d65ffc974b596c97ae42778a221` (2026-09-12). No LICENSE.
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/Makefile#L160-L227
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/Makefile#L450-L454
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/Makefile#L510-L512
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/src/game/profiling.h#L16
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/include/config/config_debug.h#L25
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/CREDITS.md#L16
  - https://github.com/HackerN64/HackerSM64/blob/8953c07aa4592d65ffc974b596c97ae42778a221/README.md
- https://github.com/HackerN64/HackerOoT @ `e77e6683d9a775df52c35121180712edcedd37fe` (2025-11-27). No LICENSE.
  - https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/Makefile#L186-L189
  - https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/Makefile#L227-L239
  - https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/Makefile#L528-L533
  - https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/src/debug/profiler.c#L1-L40
  - https://github.com/HackerN64/HackerOoT/blob/e77e6683d9a775df52c35121180712edcedd37fe/include/config/config_debug.h#L26
- https://github.com/KazeEmanuar/SM64-but-some-optimizations-are-removed @ `32261770a15b623d437a4afbb66ec77a387bd925` (2024-09-18). No LICENSE file.
  - https://github.com/KazeEmanuar/SM64-but-some-optimizations-are-removed/commit/b46813647ec403a3542fb590b8383583eed57a7f (via `gh api`)
  - https://github.com/KazeEmanuar/SM64-but-some-optimizations-are-removed/blob/32261770a15b623d437a4afbb66ec77a387bd925/Makefile#L23-L182

GitHub PRs and issues (via `gh`):
- https://github.com/zeldaret/mm/pull/1791 (merge `84a89c8f`, 2025-02-02), plus its review comments.
- https://github.com/zeldaret/mm/pull/1785 (merge `2b069011`, 2025-01-20)
- https://github.com/zeldaret/mm/pull/1789 (merge `b9267f05`, 2025-01-27)
- https://github.com/ZoeyZolotova/mm-rando/issues/142 (2021-03-26)

Docs and pages:
- https://softwareengineeringdaily.com/podcasts/bonus-episode-optimizing-nintendo-64-code-kaze-emanuar/ and transcript https://softwareengineeringdaily.com/wp-content/uploads/2024/03/SED1666-Kaze_Emanuar.txt
- https://www.youtube.com/watch?v=Ca1hHC2EctY (title, description, 2024-09-14)
- https://www.youtube.com/watch?v=t_rzYnXEQlE (title, description, 2022-04-09)
- https://www.youtube.com/watch?v=xFKFoGiGlXQ (title, description, 2023-06-24)
- https://www.youtube.com/watch?v=nA_zZiiH9Jc (title, description, 2025-12-06)
- https://news.ycombinator.com/item?id=36496691
- https://github.com/sawaiz/superMario64-TAS/blob/main/notes/kaze-emanuar.md (secondary notes)
- https://n64brew.dev/wiki/VR4300 and manual https://static.wikitide.net/n64wiki/5/55/VR4300-Users-Manual.pdf (NEC U10504EJ7V0UM00)
- https://raw.githubusercontent.com/hacktarux/n64-systembench/7c2f96d4/src/main.c (L1497-1540)
- https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- https://emulation.gametechwiki.com/index.php?title=30/60_FPS_cheat_codes&action=raw
- https://web.archive.org/web/20230724174432/http://forum.pj64-emu.com/archive/index.php/t-5208.html
- https://www.dsogaming.com/news/you-can-finally-play-super-mario-64-with-60fps-and-proper-physics-on-the-pc-via-n64-emulators/ (2018-07-04, secondary)
- https://www.techspot.com/community/topics/check-out-super-mario-64-running-at-60fps-in-widescreen-hd.247698/ (secondary)

Local files read:
- `Makefile` (HEAD `6d2e0053`), `tools/bench/README.md`, `src/code/bench.c` L255-368
- `~/repos/ares/docs/spec/n64-timing.md` (fork HEAD `54f35f9c`)
- `~/repos/oot-decomp/include/regs.h` (HEAD `14759abf`)

### Gaps

- Kaze video transcripts: not read. YouTube timedtext returned empty and yt-dlp is broken locally. Specific claims about `-O2` vs `-Os`, inlining, memcpy and collision numbers from his videos are therefore not cited from primary text. Return to Yoshi's Island source is not public (not found).
- HackerSM64 and HackerOoT publish no measured speedups. Not found in README, config or CREDITS.
- GCC version used for the mm hardware test: not stated in PR #1791. No published claim that a GCC mm build is logic-equivalent to IDO.
- IDO's own float transforms at `-O2` (whether IDO ever reassociates or uses reciprocals): not researched. The retail binary defines truth anyway.
- `make COMPILER=gcc`: not attempted (no MIPS GCC installed). Boot status of the current GCC build is unknown.
- Miss cost under RCP contention: not measured. The estimate uses idle-bus microbenchmark numbers. The bench's `dc_wb` (dirty writebacks) is not in the README, so writeback cost is excluded.
- Whether XPROF slot 0 counts only the main thread or all threads: inferred (all), not verified in the fork's emux code.
- OoT address 0x801C6FA1 = R_UPDATE_RATE: inferred from the 3/1 values, not checked against an ntsc-1.0 map.
- No console-verified MM 60 fps code found.
