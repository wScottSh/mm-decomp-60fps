#!/usr/bin/env python3
"""Run a BENCH=1 Majora's Mask ROM through the pinned n64-run over the fps60 scene corpus.

Writes per-field TSVs, the per-tick keyframe sequence, presentation and tick-cadence summaries, and the
load metrics (CPU, RSP and RDP busy per field and per tick, CPU time per engine system).
See tools/fps60/README.md.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus  # noqa: E402
import load  # noqa: E402

RUNNER = Path.home() / "n64-timing/build/mm60-runner/rundir/bin/n64-run"
RUNNER_SHA256_PREFIX = "1f9d59f6"  # preferences line 6
MAX_JOBS = 4  # preferences line 14

CONFIG_MAGIC = 0x424E4348  # BENCH_CONFIG_MAGIC, include/bench.h
PROBE_MAGIC = 0x50524F42  # BENCH_PROBE_MAGIC
FLAG_PIN_EFFECT_BUDGET = 1 << 1
FLAG_PIN_LOADS = 1 << 2  # BENCH_FLAG_PIN_LOADS: the load pin (README, Load pin)
SEED = 0x12345678
MAX_INPUTS = 512

# BenchConfig and BenchProbe layouts, include/bench.h
CFG_SCENE, CFG_SEED, CFG_FLAGS, CFG_RETRACE, CFG_INPUT_COUNT, CFG_INPUTS = 4, 8, 12, 16, 20, 24
PROBE_MAGIC_OFF, PROBE_TICK, PROBE_DIVISOR, PROBE_SETTLED, PROBE_SETTLE_TICK = 0x00, 0x04, 0x08, 0x0C, 0x10
PROBE_CUR_BUF, PROBE_CFBS, PROBE_CFB_SIZE, PROBE_CFB_TICK = 0x14, 0x18, 0x1C, 0x20
CFB_COUNT = 3

# The noise pin (Bench_PinNoise, src/code/bench.c): BenchConfig.noisePin, BenchProbe.cfbNoise[CFB_COUNT]
# (BenchNoiseRecord, parallel to cfbTick) and BenchProbe.noise (BenchNoiseTotals).
NOISE_PIN_SYMBOL = "Bench_PinNoise"
CFG_NOISE_PIN = CFG_INPUTS + 8 * MAX_INPUTS
PROBE_CFB_NOISE, NOISE_RECORD_SIZE = 0x48, 16
NOISE_RECORD = ["noise_hash", "noise_cmds", "noise_patched", "dl_cmds"]
PROBE_NOISE_TOTALS = 0x78
NOISE_TOTALS = ["walks", "visited", "commands", "patched", "unresolved", "unknown", "overflow", "mismatch"]
NOISE_MUST_BE_ZERO = ["unresolved", "unknown", "overflow", "mismatch"]
NOISE_PIN = {"off": 0, "on": 1, "check": 2, "noise-only": 3}  # BENCH_NOISE_PIN_*

# The load pin (Bench_DmaRecv, Bench_AudioBarrier): BenchProbe.load (BenchLoadTotals), read after the window.
LOAD_PIN_SYMBOL = "Bench_DmaRecv"
PROBE_LOAD_TOTALS = 0x9C
LOAD_TOTALS = ["dma_polls", "dma_held", "dma_waited", "dma_untracked", "dma_overflow", "audio_barriers",
               "audio_waits"]
LOAD_MUST_BE_ZERO = ["dma_untracked", "dma_overflow"]

# BenchProbe.tickState: a ring of BENCH_STATE_RING words, (tick << 12) | (divisor << 8) | BENCH_STATE_* flags,
# written when each tick's gfx task is set up and read once after the window.
PROBE_TICK_STATE, STATE_RING = 0x98, 1024
STATE_FLAGS = ["capture", "pause_filter", "pause", "page_turn", "motion_blur", "transition", "tile", "play"]
# Retail holds (preferences line 24): a capture tick never swaps, so the image before it stays up for that
# tick's fields; a tick whose render runs while the pause background filters shows only its keyframe.
HOLD_CAPTURE, HOLD_PAUSE_FILTER = 1 << 0, 1 << 1

# Optional: u32 gBenchCfbKeyframe[CFB_COUNT], nonzero when that CfbInfo's image is its tick's final
# image. A build without it presents one image per tick, so every image is a keyframe.
KEYFRAME_SYMBOL = "gBenchCfbKeyframe"
# u8 gBenchCfbImage[CFB_COUNT], defined with gBenchCfbKeyframe: the image's index 1..divisor within its tick.
# Without it, every image is index divisor, its tick's keyframe.
IMAGE_SYMBOL = "gBenchCfbImage"

DETERMINISTIC_EXCLUDE = {"wall.tsv", "host.txt"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def read_map(path):
    symbols = {}
    pattern = re.compile(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_]\w*)$")
    with open(path) as f:
        for line in f:
            m = pattern.match(line)
            if m:
                symbols[m.group(2)] = int(m.group(1), 16) & 0xFFFFFFFF
    return symbols


def patch_rom(rom, scene, noise_pin, load_pin):
    data = bytearray(rom)
    at = data.find(struct.pack(">I", CONFIG_MAGIC))
    if at < 0 or data.find(struct.pack(">I", CONFIG_MAGIC), at + 1) >= 0:
        raise SystemExit("ROM has no unique BenchConfig block; build it with make BENCH=1")
    inputs = scene["inputs"]
    if len(inputs) > MAX_INPUTS:
        raise SystemExit(f"{scene['name']}: {len(inputs)} inputs, the ROM holds {MAX_INPUTS}")
    if [i[0] for i in inputs] != sorted(i[0] for i in inputs):
        raise SystemExit(f"{scene['name']}: inputs are not sorted by tick")
    flags = FLAG_PIN_EFFECT_BUDGET | (FLAG_PIN_LOADS if load_pin else 0)
    struct.pack_into(">3I", data, at + CFG_SCENE, corpus.BOOT[scene["boot"]], SEED, flags)
    struct.pack_into(">I", data, at + CFG_INPUT_COUNT, len(inputs))
    if noise_pin:
        struct.pack_into(">I", data, at + CFG_NOISE_PIN, NOISE_PIN[noise_pin])
    for n, (tick, buttons, x, y) in enumerate(inputs):
        struct.pack_into(">HHbbH", data, at + CFG_INPUTS + 8 * n, tick, buttons, x, y, 0)
    retrace = struct.unpack_from(">I", data, at + CFG_RETRACE)[0]
    return bytes(data), retrace


def script(symbols, window, noise_pin):
    probe = symbols["gBenchProbe"]
    keyframe = symbols.get(KEYFRAME_SYMBOL)
    image = symbols.get(IMAGE_SYMBOL)

    def a(off):
        return f"0x{probe + off:08X}"

    steps = [f"until {a(PROBE_MAGIC_OFF)} w == 0x{PROBE_MAGIC:08X}", f"until {a(PROBE_SETTLED)} w != 0",
             "mark window", f"peek cfbs {a(PROBE_CFBS)} w", f"peek cfb_size {a(PROBE_CFB_SIZE)} w",
             f"peek settle_tick {a(PROBE_SETTLE_TICK)} w"] + load.start_steps(probe)
    # A peek logged at frame=N reads the state after N fields, so it belongs to stats row N-1.
    per_field = ["wait 1", f"peek tick {a(PROBE_TICK)} w", f"peek div {a(PROBE_DIVISOR)} w",
                 f"peek cur [{a(PROBE_CUR_BUF)}] w", f"peek fb [[{a(PROBE_CUR_BUF)}]] w"]
    per_field += [f"peek c{i} {a(PROBE_CFB_TICK + 4 * i)} w" for i in range(CFB_COUNT)] + load.field_peeks(probe)
    if keyframe is not None:
        per_field += [f"peek k{i} 0x{keyframe + 4 * i:08X} w" for i in range(CFB_COUNT)]
        per_field += [f"peek i{i} 0x{image + i:08X} b" for i in range(CFB_COUNT)]
    end = []
    if LOAD_PIN_SYMBOL in symbols:
        end += [f"peek load_{k} {a(PROBE_LOAD_TOTALS + 4 * j)} w" for j, k in enumerate(LOAD_TOTALS)]
    if noise_pin:
        per_field += [f"peek n{i}_{k} {a(PROBE_CFB_NOISE + NOISE_RECORD_SIZE * i + 4 * j)} w"
                      for i in range(CFB_COUNT) for j, k in enumerate(NOISE_RECORD)]
        end += [f"peek total_{k} {a(PROBE_NOISE_TOTALS + 4 * j)} w" for j, k in enumerate(NOISE_TOTALS)]
    end += [f"peek s{n} [{a(PROBE_TICK_STATE)}]+0x{4 * n:X} w" for n in range(STATE_RING)]
    return steps + per_field * window + load.end_steps(probe) + end + ["stop"]


def run_runner(runner, rom_bytes, steps, scene_dir, dump_frames=()):
    """Runs one scene; returns (stdout, events, other runner lines, (wall s, load before, load after))."""
    scene_dir.mkdir(parents=True, exist_ok=True)
    script_path = scene_dir / "script.txt"
    script_path.write_text("\n".join(steps) + "\n")
    with tempfile.TemporaryDirectory(dir=scene_dir) as tmp:
        rom_path = Path(tmp) / "rom.z64"
        rom_path.write_bytes(rom_bytes)
        cmd = [str(runner), str(rom_path), "--script", str(script_path), "--stats", str(scene_dir / "stats.tsv"),
               "--frames", "20000", "--wall-seconds", "1800"]
        for frame, path in dump_frames:
            cmd += ["--dump-frame", str(frame), str(path)]
        load = os.getloadavg()[0]
        began = time.perf_counter()
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        wall = (time.perf_counter() - began, load, os.getloadavg()[0])
    lines = [l[len("n64-run: "):] for l in proc.stderr.splitlines() if l.startswith("n64-run: ")]
    stop = next((l for l in lines if l.startswith("stop=")), None)
    if proc.returncode != 0 or stop is None or not stop.startswith("stop=script-stop "):
        raise SystemExit(f"{scene_dir.name}: runner exit {proc.returncode}, {stop or proc.stderr[-800:]}")
    events = [l for l in lines if l.startswith(("mark ", "peek "))]
    host = [l for l in lines if l not in events]
    failed = [l for l in lines if "unreadable" in l or "failed" in l]
    if failed:
        raise SystemExit(f"{scene_dir.name}: script step failed: {failed[0]}")
    return proc.stdout, events, host, wall


def parse_events(events):
    marks, peeks = {}, {}
    for e in events:
        verb, name, frame, *rest = e.split(" ")
        frame = int(frame.split("=")[1])
        if verb == "mark":
            marks[name] = frame
        else:
            peeks.setdefault(frame, {})[name] = int(rest[0], 16)
    return marks, peeks


def read_stats(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def per_field(name, stats, marks, peeks, window, has_keyframe, noise_pin):
    if "window" not in marks:
        raise SystemExit(f"{name}: no window mark")
    start = marks["window"]
    if len(stats) < start + window:
        raise SystemExit(f"{name}: {len(stats)} stats rows, the window needs {start + window}")
    static = peeks.get(start, {})
    cfbs, cfb_size = static.get("cfbs"), static.get("cfb_size")
    if cfbs is None or not cfb_size:
        raise SystemExit(f"{name}: missing the window-start peeks")
    rows, fb_offset, retaken = [], None, 0
    for i in range(window):
        frame = start + i
        p = peeks.get(frame + 1)
        need = ["tick", "div", "cur", "fb"] + [f"c{k}" for k in range(CFB_COUNT)]
        need += [f"{c}{k}" for c in "ki" for k in range(CFB_COUNT)] if has_keyframe else []
        need += [f"n{k}_{f}" for k in range(CFB_COUNT) for f in NOISE_RECORD] if noise_pin else []
        if p is None or any(k not in p for k in need):
            raise SystemExit(f"{name}: missing peeks for window field {i} (frame {frame})")
        index, rem = divmod(p["cur"] - cfbs, cfb_size)
        if rem or not 0 <= index < CFB_COUNT:
            raise SystemExit(f"{name}: field {i}: gScheduler.curBuf 0x{p['cur']:08X} is not an sGraphCfbInfos entry")
        origin = int(stats[frame]["origin"], 16)
        offset = origin - (p["fb"] & 0x00FFFFFF)
        row = {"field": i, "frame": frame, "origin": stats[frame]["origin"], "fb_hash": stats[frame]["fb_hash"],
               "tick": p["tick"]}
        if fb_offset is None:
            fb_offset = offset
        elif offset != fb_offset:
            # A capture tick takes a CfbInfo without swapping, so the next tick can take the CfbInfo the VI
            # still shows (graph.c sCfbIndex). Until the swap, the image is the previous field's.
            if not rows or stats[frame]["origin"] != rows[-1]["origin"]:
                raise SystemExit(f"{name}: field {i}: VI origin 0x{origin:06X} is not the shown CfbInfo's "
                                 f"framebuffer 0x{p['fb']:08X} + 0x{fb_offset:X}")
            rows.append(rows[-1] | row | {"divisor": p["div"]})
            retaken += 1
            continue
        rows.append(row | {
            "image_tick": p[f"c{index}"], "keyframe": int(p[f"k{index}"] != 0) if has_keyframe else 1,
            "image": p[f"i{index}"] if has_keyframe else None, "divisor": p["div"],
        } | ({f: (f"{p[f'n{index}_{f}']:08X}" if f == "noise_hash" else p[f"n{index}_{f}"]) for f in NOISE_RECORD}
             if noise_pin else {}))
    if not has_keyframe:
        # The divisor peeked in the last field while a tick was the latest: by then that tick's update has
        # normally ended, so it is the tick's own divisor.
        divisor = {r["tick"]: r["divisor"] for r in rows}
        for r in rows:
            r["image"] = divisor.get(r["image_tick"], r["divisor"])
    else:
        bad = next((r for r in rows if not 1 <= r["image"] <= 3), None)
        if bad:
            raise SystemExit(f"{name}: field {bad['field']}: {IMAGE_SYMBOL} {bad['image']} is not an image index")
    return rows, static["settle_tick"], retaken


def images(rows):
    """Complete images: runs of fields between VI origin changes inside the window."""
    changes = [i for i in range(1, len(rows)) if rows[i]["origin"] != rows[i - 1]["origin"]]
    out = []
    for a, b in zip(changes, changes[1:]):
        span = rows[a:b]
        tags = {(r["image_tick"], r["image"], r["keyframe"]) + tuple(r.get(f) for f in NOISE_RECORD) for r in span}
        if len(tags) != 1:
            raise SystemExit(f"field {a}: one image carries two tick tags {sorted(tags)}")
        out.append({"first_field": a, "fields": b - a, "image_tick": span[0]["image_tick"],
                    "image": span[0]["image"], "keyframe": span[0]["keyframe"], "fb_hash": span[-1]["fb_hash"],
                    "final_frame": span[-1]["frame"],
                    "hash_changes": sum(1 for x, y in zip(span, span[1:]) if x["fb_hash"] != y["fb_hash"])}
                   | {f: span[0][f] for f in NOISE_RECORD if f in span[0]})
    return out


def ticks(rows):
    """Ticks by the field their first image appears; a tick's length runs to the next tick's first image."""
    first = {}
    for r in rows:
        first.setdefault(r["image_tick"], r)
    order = sorted(first)
    out = []
    # The first tick may have started before the window; the last runs past its end.
    for t, nxt in zip(order[1:], order[2:]):
        a, b = first[t]["field"], first[nxt]["field"]
        out.append({"tick": t, "first_field": a, "fields": b - a, "divisor": first[t]["divisor"],
                    "skipped_after": nxt - t - 1})
    return out


def tick_states(name, peeks, rows):
    """BenchProbe.tickState for every tick from the window's first image to its last."""
    last = peeks[max(peeks)]
    first_tick, last_tick = min(r["image_tick"] for r in rows), max(r["image_tick"] for r in rows)
    if last_tick - first_tick >= STATE_RING:
        raise SystemExit(f"{name}: {last_tick - first_tick + 1} ticks in the window, the state ring holds {STATE_RING}")
    out = []
    for t in range(first_tick, last_tick + 1):
        word = last.get(f"s{t % STATE_RING}")
        if word is None or word >> 12 != t & 0xFFFFF:
            raise SystemExit(f"{name}: no tick state for tick {t} (ring word {word if word is None else hex(word)})")
        flags = word & 0xFF
        out.append({"tick": t, "divisor": (word >> 8) & 0xF, "flags": f"0x{flags:02X}",
                    **{k: int(bool(flags & (1 << i))) for i, k in enumerate(STATE_FLAGS)}})
    return out


def holds(rows, imgs, states):
    """Window fields that repeat the previous image because retail holds it (HOLD_*): the fields of the
    capture ticks after an image's tick, and every field after the first of a pause-filter tick's image."""
    by_tick = {s["tick"]: (s["divisor"], int(s["flags"], 16)) for s in states}
    total = 0
    for img in imgs:
        tick, repeats = img["image_tick"], img["fields"] - 1
        next_tick = rows[img["first_field"] + img["fields"]]["image_tick"]
        if by_tick[tick][1] & HOLD_PAUSE_FILTER:
            total += repeats
            continue
        captured = sum(by_tick[t][0] for t in range(tick + 1, next_tick) if by_tick[t][1] & HOLD_CAPTURE)
        total += min(repeats, captured)
    return total


def distribution(values):
    c = Counter(min(v, 6) for v in values)
    return {f"n{k}": c[k] for k in range(1, 6)} | {"n6plus": c[6]}


def summarize(rows, imgs, tks):
    lengths = [i["fields"] for i in imgs]
    tick_lengths = [t["fields"] for t in tks]
    return {
        "fields": len(rows),
        "images": len(imgs),
        "fields_per_image": distribution(lengths),
        "image_share_at_1": round(sum(1 for l in lengths if l == 1) / len(lengths), 6) if lengths else 0,
        "image_fields_max": max(lengths, default=0),
        "ticks": len(tks),
        "fields_per_tick": distribution(tick_lengths),
        "lag_ticks": sum(1 for t in tks if t["fields"] > t["divisor"]),
        "skipped_ticks": sum(t["skipped_after"] for t in tks),
        "tick_fields_max": max(tick_lengths, default=0),
        "divisors": sorted({r["divisor"] for r in rows}),
        "keyframes": sum(1 for i in imgs if i["keyframe"]),
        "first_tick": tks[0]["tick"] if tks else None,
        "last_tick": tks[-1]["tick"] if tks else None,
    }


def write_tsv(path, rows, keys=None):
    keys = keys or list(rows[0].keys())
    Path(path).write_text("\t".join(keys) + "\n" + "".join("\t".join(str(r[k]) for k in keys) + "\n" for r in rows))


def run_scene(rom, symbols, runner, scene, out, noise_pin, load_pin, dump_frames=()):
    name = scene["name"]
    scene_dir = out / name
    rom_bytes, retrace = patch_rom(rom, scene, noise_pin, load_pin)
    window = scene.get("window", corpus.WINDOW)
    stdout, events, host, wall = run_runner(runner, rom_bytes, script(symbols, window, noise_pin), scene_dir,
                                            dump_frames)
    (scene_dir / "guest.txt").write_text(stdout)
    (scene_dir / "events.txt").write_text("\n".join(events) + "\n")
    (scene_dir / "host.txt").write_text("\n".join(host) + "\n")
    m = re.search(r"BENCH START scene=(\d+) name=(\S+)", stdout)
    if not m or m.group(2) != scene["boot"]:
        raise SystemExit(f"{name}: the ROM booted {m.group(2) if m else 'nothing'}, not {scene['boot']}")
    marks, peeks = parse_events(events)
    stats_rows = read_stats(scene_dir / "stats.tsv")
    rows, settle_tick, retaken = per_field(name, stats_rows, marks, peeks, window, KEYFRAME_SYMBOL in symbols,
                                           noise_pin)
    imgs, tks = images(rows), ticks(rows)
    write_tsv(scene_dir / "fields.tsv", rows)
    keyframes = [i for i in imgs if i["keyframe"]]
    keys = ["image_tick", "first_field", "fields", "final_frame", "fb_hash", "hash_changes"]
    keys += NOISE_RECORD if noise_pin else []
    if keyframes:
        write_tsv(scene_dir / "keyframes.tsv", keyframes, keys)
    else:
        (scene_dir / "keyframes.tsv").write_text("\t".join(keys) + "\n")
    image_keys = ["image_tick", "image", "keyframe", "first_field", "fields", "final_frame", "fb_hash",
                  "hash_changes"] + (NOISE_RECORD if noise_pin else [])
    if imgs:
        write_tsv(scene_dir / "images.tsv", imgs, image_keys)
    else:
        (scene_dir / "images.tsv").write_text("\t".join(image_keys) + "\n")
    if tks:
        write_tsv(scene_dir / "ticks.tsv", tks)
    states = tick_states(name, peeks, rows)
    write_tsv(scene_dir / "states.tsv", states)
    summary = {"scene": name, "boot": scene["boot"], "window_start_frame": marks["window"],
               "settle_tick": settle_tick, "retrace_ticks": retrace, **summarize(rows, imgs, tks),
               "hold_fields": holds(rows, imgs, states), "retaken_cfb_fields": retaken,
               "state_ticks": {k: sum(s[k] for s in states) for k in STATE_FLAGS}, "noise_pin": bool(noise_pin),
               "noise_pin_mode": noise_pin or "off", "load_pin": load_pin}
    if LOAD_PIN_SYMBOL in symbols:
        summary["load_totals"] = totals(name, peeks, "load_", LOAD_TOTALS)
    noise = ""
    if noise_pin:
        noise_sum = noise_totals(name, peeks)
        summary |= {"noise_totals": noise_sum, "noise_cmds_per_keyframe_max": max(
            (k["noise_cmds"] for k in keyframes), default=0), "noise_patched_in_window": sum(
            k["noise_patched"] for k in keyframes)}
        noise = (f"; noise pin: {noise_sum['walks']} walks since boot, {noise_sum['visited']} commands visited, "
                 f"{noise_sum['commands']} noise commands, {noise_sum['patched']} patched, unresolved "
                 f"{noise_sum['unresolved']}, unknown {noise_sum['unknown']}, overflow {noise_sum['overflow']}, mismatch "
                 f"{noise_sum['mismatch']}")
    loads = ""
    if load_pin:
        t = summary["load_totals"]
        loads = (f"; load pin: {t['dma_polls']} DMA polls ({t['dma_held']} held, {t['dma_waited']} waited, untracked "
                 f"{t['dma_untracked']}, overflow {t['dma_overflow']}), {t['audio_waits']} of "
                 f"{t['audio_barriers']} audio barriers waited")
    (scene_dir / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    summary["load"] = load.analyze(name, peeks, marks["window"], window, stats_rows, scene_dir, write_tsv)
    print(f"{name}: measured {summary['fields']} fields, {summary['images']} images, {summary['ticks']} ticks "
          f"({wall[0]:.1f} s){noise}{loads}", flush=True)
    bad = [k for k in NOISE_MUST_BE_ZERO if noise_pin and summary["noise_totals"][k]]
    if bad:
        raise SystemExit(f"{name}: the noise pin's walk did not cover the display lists: "
                         + ", ".join(f"{k} {summary['noise_totals'][k]}" for k in bad))
    bad = [k for k in LOAD_MUST_BE_ZERO if load_pin and summary["load_totals"][k]]
    if bad:
        raise SystemExit(f"{name}: the load pin did not hold for every DMA poll: "
                         + ", ".join(f"{k} {summary['load_totals'][k]}" for k in bad))
    return summary, wall


def totals(name, peeks, prefix, keys):
    """Counters peeked after the window."""
    last = peeks[max(peeks)]
    if any(f"{prefix}{k}" not in last for k in keys):
        raise SystemExit(f"{name}: missing the {prefix}* totals peeks")
    return {k: last[f"{prefix}{k}"] for k in keys}


def noise_totals(name, peeks):
    """BenchProbe.noise as peeked after the window."""
    return totals(name, peeks, "total_", NOISE_TOTALS)


SUMMARY_COLS = ["scene", "fields", "images", "image_share_at_1", "image_fields_max", "ticks", "lag_ticks",
                "skipped_ticks", "tick_fields_max", "divisors", "window_start_frame", "settle_tick", "hold_fields"]


def flat(summary):
    row = {k: summary[k] for k in SUMMARY_COLS}
    row["divisors"] = ",".join(map(str, summary["divisors"]))
    row |= {f"img_{k}": v for k, v in summary["fields_per_image"].items()}
    row |= {f"tick_{k}": v for k, v in summary["fields_per_tick"].items()}
    row |= {f"state_{k}": v for k, v in summary["state_ticks"].items()}
    return row


def check_runner(runner):
    digest = sha256(runner)
    if not digest.startswith(RUNNER_SHA256_PREFIX) and not os.environ.get("FPS60_ANY_RUNNER"):
        raise SystemExit(f"{runner}: sha256 {digest[:16]}, the pinned runner is {RUNNER_SHA256_PREFIX}")
    return digest


def load_rom(rom_path, map_path):
    rom = Path(rom_path).read_bytes()
    symbols = read_map(map_path)
    if "gBenchProbe" not in symbols:
        raise SystemExit(f"{map_path}: no gBenchProbe; build the ROM with make BENCH=1")
    return rom, symbols


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=Path, help="BENCH=1 ROM, e.g. build/n64-us-bench/mm-n64-us.z64")
    p.add_argument("--map", type=Path, help="that build's map (default: mm-n64-us.map beside the ROM)")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--scenes", default=",".join(s["name"] for s in corpus.SCENES))
    p.add_argument("--jobs", type=int, default=MAX_JOBS)
    p.add_argument("--runner", type=Path, default=Path(os.environ.get("N64_RUN", RUNNER)))
    p.add_argument("--noise-pin", choices=list(NOISE_PIN), default="on",
                   help="off: retail RDP noise, for perf runs. on (default): fidelity runs. check: as on, and "
                   "each task is walked twice to check the walk is idempotent. noise-only: as on, but pipeline "
                   "mode stays as emitted, for stale_reads.py (README, Noise pin)")
    p.add_argument("--load-pin", choices=["on", "off"],
                   help="DMA and audio load state reach logic only at tick boundaries (README, Load pin). "
                   "Default: on, off with --noise-pin off")
    p.add_argument("--check-determinism", action="store_true",
                   help="run twice into OUT/run1 and OUT/run2; fail unless every output but wall time matches")
    args = p.parse_args()
    if not 1 <= args.jobs <= MAX_JOBS:
        raise SystemExit(f"--jobs must be 1 to {MAX_JOBS}")
    map_path = args.map or args.rom.with_suffix(".map")
    by_name = {s["name"]: s for s in corpus.SCENES}
    names = args.scenes.split(",")
    unknown = [n for n in names if n not in by_name]
    if unknown:
        raise SystemExit(f"unknown scenes {unknown}; the corpus has {list(by_name)}")

    runner_digest = check_runner(args.runner)
    rom, symbols = load_rom(args.rom, map_path)
    args.noise_pin = None if args.noise_pin == "off" else args.noise_pin
    if args.noise_pin and NOISE_PIN_SYMBOL not in symbols:
        raise SystemExit(f"{map_path}: no {NOISE_PIN_SYMBOL}; this ROM predates the noise pin, run it with "
                         "--noise-pin off")
    args.load_pin = (args.load_pin or ("on" if args.noise_pin else "off")) == "on"
    if args.load_pin and LOAD_PIN_SYMBOL not in symbols:
        raise SystemExit(f"{map_path}: no {LOAD_PIN_SYMBOL}; this ROM predates the load pin, run it with "
                         "--load-pin off")
    if (KEYFRAME_SYMBOL in symbols) != (IMAGE_SYMBOL in symbols):
        raise SystemExit(f"{map_path}: a build that presents interpolated images defines both {KEYFRAME_SYMBOL} "
                         f"and {IMAGE_SYMBOL}")
    if not args.check_determinism:
        measure(args, map_path, names, by_name, runner_digest, rom, symbols, args.out)
        return
    for run_dir in ("run1", "run2"):
        measure(args, map_path, names, by_name, runner_digest, rom, symbols, args.out / run_dir)
    files, differing = compare_trees(args.out / "run1", args.out / "run2")
    if differing:
        raise SystemExit(f"determinism: FAIL, {len(differing)} of {len(files)} files differ: "
                         + " ".join(map(str, differing)))
    print(f"determinism: PASS, {len(files)} files byte-identical ({', '.join(sorted(DETERMINISTIC_EXCLUDE))} "
          "excluded)")


def rerun(result, scene_name, dump_frames, out):
    """Runs one scene of a result directory again, with its ROM and settings, dumping `dump_frames`
    ([(stats row, path)]) into `out`. Fails unless the rerun shows the recorded fb_hash at every dumped row."""
    meta = json.loads((result / "meta.json").read_text())
    if sha256(meta["rom"]) != meta["rom_sha256"]:
        raise SystemExit(f"{meta['rom']} changed since {result} was measured; cannot dump its frames")
    rom, symbols = load_rom(meta["rom"], meta["map"])
    scene = next(s for s in corpus.SCENES if s["name"] == scene_name)
    noise_pin = None if meta.get("noise_pin_mode", "on" if meta.get("noise_pin") else "off") == "off" \
        else meta.get("noise_pin_mode", "on")
    run_scene(rom, symbols, Path(meta["runner"]), scene, out, noise_pin, meta.get("load_pin", False), dump_frames)
    recorded = read_stats(result / scene_name / "stats.tsv")
    again = read_stats(out / scene_name / "stats.tsv")
    for frame, _ in dump_frames:
        if recorded[frame]["fb_hash"] != again[frame]["fb_hash"]:
            raise SystemExit(f"{scene_name}: the rerun of {result} shows fb_hash {again[frame]['fb_hash']} at row "
                             f"{frame}, the run recorded {recorded[frame]['fb_hash']}")


def compare_trees(a, b):
    def listing(root):
        return {p.relative_to(root) for p in root.rglob("*") if p.is_file() and p.name not in DETERMINISTIC_EXCLUDE}
    files = sorted(listing(a) | listing(b))
    return files, [f for f in files if not (a / f).is_file() or not (b / f).is_file()
                   or (a / f).read_bytes() != (b / f).read_bytes()]


def measure(args, map_path, names, by_name, runner_digest, rom, symbols, out):
    out.mkdir(parents=True, exist_ok=True)
    meta = {"rom": str(args.rom.resolve()), "rom_sha256": sha256(args.rom), "map": str(map_path.resolve()),
            "map_sha256": sha256(map_path), "runner": str(args.runner), "runner_sha256": runner_digest,
            "gBenchProbe": f"0x{symbols['gBenchProbe']:08X}", "keyframe_symbol": KEYFRAME_SYMBOL in symbols,
            "noise_pin": bool(args.noise_pin), "noise_pin_mode": args.noise_pin or "off", "load_pin": args.load_pin,
            "scenes": names}
    (out / "meta.json").write_text(json.dumps(meta, indent=1) + "\n")

    load_before = os.getloadavg()[0]
    began = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {n: pool.submit(run_scene, rom, symbols, args.runner, by_name[n], out, args.noise_pin,
                                  args.load_pin) for n in names}
        results = {n: f.result() for n, f in futures.items()}
    total = time.perf_counter() - began

    summaries = [results[n][0] for n in names]
    write_tsv(out / "summary.tsv", [flat(s) for s in summaries])
    write_tsv(out / "load.tsv", [load.flat(s["load"]) for s in summaries])
    (out / "wall.tsv").write_text(
        "run\twall_s\tjobs\tload_before\tload_after\n"
        + "".join(f"{n}\t{results[n][1][0]:.1f}\t{args.jobs}\t{results[n][1][1]:.2f}\t{results[n][1][2]:.2f}\n"
                  for n in names)
        + f"total\t{total:.1f}\t{args.jobs}\t{load_before:.2f}\t{os.getloadavg()[0]:.2f}\n")
    fields = sum(s["fields"] for s in summaries)
    tick_count = sum(s["ticks"] for s in summaries)
    print(f"measured {fields} fields and {tick_count} ticks over {len(names)} scenes; results in {out}")


if __name__ == "__main__":
    main()
