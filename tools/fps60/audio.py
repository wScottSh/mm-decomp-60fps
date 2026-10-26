"""Audio fidelity (F4): the AI buffer hashes a BENCH ROM records, and their comparison.

run.py patches gBenchAudioConfig.pin, adds the ring peeks after the window, and calls `analyze`.
fidelity.py calls `compare`. The ROM side is the audio pin in src/code/bench.c. See tools/fps60/README.md.
"""

import struct
from pathlib import Path

CONFIG_SYMBOL = "gBenchAudioConfig"
AUDIO_SYMBOL = "gBenchAudio"
MAGIC = 0x41554449  # BENCH_AUDIO_MAGIC, include/bench.h
BENCH_CONFIG_MAGIC = 0x424E4348

# BenchAudioConfig.pin
HASH, PIN_CMDS, PIN_AI, PIN_RANDOM, PIN_LOADS = 1, 2, 4, 8, 16
PIN = {"off": 0, "hash": HASH, "on": HASH | PIN_CMDS | PIN_AI | PIN_RANDOM | PIN_LOADS}

# BenchAudio and BenchAudioFrame layouts
HEADER = ["magic", "frames", "anchor", "ring", "layout", "send_refused", "tag_overflow", "graph_waits",
          "audio_waits", "reset_frames", "ai_dropped"]
RING_OFF = 0x0C
FRAME_WORDS = ["frame", "retrace", "tick", "len", "hash", "ai_len", "ai_used", "due"]
RING = 1024
WRITING = 0xFFFFFFFF
# A refused or untagged batch, or a buffer the AI model dropped, means the pin did not hold
MUST_BE_ZERO = ["send_refused", "tag_overflow", "ai_dropped"]

# F4 compares at least this many frames from the anchor, about half a 600-field window
MIN_FRAMES = 300


def parse_pin(value):
    if value in PIN:
        return PIN[value]
    mask = int(value, 0)
    if not 0 <= mask <= PIN["on"]:
        raise SystemExit(f"--audio-pin {value}: not off, hash, on or a mask of 0x00 to 0x{PIN['on']:02X}")
    return mask


def patch_rom(rom, symbols, pin):
    """Returns the ROM with gBenchAudioConfig.pin set. BenchConfig and it both live in bench.c's .data, so
    the config sits at BenchConfig's ROM offset plus their RAM distance."""
    if CONFIG_SYMBOL not in symbols:
        if pin:
            raise SystemExit(f"the map has no {CONFIG_SYMBOL}; this ROM predates the audio pin, run it with "
                             "--audio-pin off")
        return rom
    data = bytearray(rom)
    base = data.find(struct.pack(">I", BENCH_CONFIG_MAGIC))
    at = base + symbols[CONFIG_SYMBOL] - symbols["gBenchConfig"]
    if struct.unpack_from(">I", data, at)[0] != MAGIC:
        raise SystemExit(f"no BenchAudioConfig at ROM 0x{at:X}")
    struct.pack_into(">I", data, at + 4, pin)
    return bytes(data)


def end_steps(symbols):
    base = symbols[AUDIO_SYMBOL]
    head = [f"peek au_{w} 0x{base + 4 * i:08X} w" for i, w in enumerate(HEADER)]
    ring = [f"peek ar{n}_{w} [0x{base + RING_OFF:08X}]+0x{4 * (n * len(FRAME_WORDS) + i):X} w"
            for n in range(RING) for i, w in enumerate(FRAME_WORDS)]
    return head + ring


def analyze(name, peeks, pin, out_dir, write_tsv):
    """Writes audio.tsv (one row per recorded frame, `rel` counted from the anchor) and returns the
    summary."""
    last = peeks[max(peeks)]
    if any(f"au_{w}" not in last for w in HEADER):
        raise SystemExit(f"{name}: missing the gBenchAudio peeks")
    head = {w: last[f"au_{w}"] for w in HEADER}
    if head["magic"] != MAGIC or head["layout"] != (len(FRAME_WORDS) * 4) << 16 | RING:
        raise SystemExit(f"{name}: gBenchAudio magic 0x{head['magic']:08X} layout 0x{head['layout']:08X}")
    rows = []
    for n in range(RING):
        r = {w: last.get(f"ar{n}_{w}") for w in FRAME_WORDS}
        if None in r.values():
            raise SystemExit(f"{name}: missing audio ring peeks")
        if r["frame"] == WRITING or r["frame"] % RING != n or r["frame"] >= head["frames"] or r["len"] == 0:
            continue
        rows.append(r)
    rows.sort(key=lambda r: r["frame"])
    for r in rows:
        r["rel"] = r["frame"] - head["anchor"]
        r["hash"] = f"{r['hash']:08X}"
    keys = ["rel"] + FRAME_WORDS
    if rows:
        write_tsv(out_dir / "audio.tsv", rows, keys)
    else:
        (out_dir / "audio.tsv").write_text("\t".join(keys) + "\n")
    after = [r for r in rows if r["rel"] >= 0]
    summary = {"pin": pin, **{k: head[k] for k in HEADER if k not in ("magic", "ring", "layout")},
               "recorded": len(rows), "recorded_from_anchor": len(after)}
    bad = [k for k in MUST_BE_ZERO if pin & ~HASH and head[k]]
    if bad:
        raise SystemExit(f"{name}: the audio pin did not hold: " + ", ".join(f"{k} {head[k]}" for k in bad))
    return summary


def frames(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    rows = [dict(zip(header, l.split("\t"))) for l in lines[1:]]
    return {int(r["rel"]): r for r in rows if int(r["rel"]) >= 0}


def compare(base_path, cand_path):
    """F4 over the frames from the anchor that both runs recorded without a gap. Returns (frames compared,
    the first differing frame's rows or None, differing count, why it failed or None)."""
    b, c = frames(base_path), frames(cand_path)
    n = 0
    while n in b and n in c:
        n += 1
    if n < MIN_FRAMES:
        return n, None, 0, f"only {n} frames from the anchor in both runs, F4 needs {MIN_FRAMES}"
    differing = [k for k in range(n) if (b[k]["len"], b[k]["hash"]) != (c[k]["len"], c[k]["hash"])]
    first = (b[differing[0]], c[differing[0]]) if differing else None
    return n, first, len(differing), None
