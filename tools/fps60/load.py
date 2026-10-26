"""Load metrics: CPU, RSP and RDP busy per field and per logic tick, and CPU time per engine system.

run.py adds these peeks to every scene's script and calls `analyze` on the result. The ROM side is
include/bench.h (BenchField, BenchTick, BenchProbe) and src/code/bench.c. See tools/fps60/README.md.
"""

import json
import math
import re
from pathlib import Path

# Clocks (include/bench.h). CPU times are PClocks; RSP times are the emulator's 750 MHz units
# (XPROFREAD 0x0200, ares/n64/timing/clock.hpp); RDP counters are RCP clocks.
PCLK_HZ = 93_750_000
RSP_HZ = 750_000_000
RCLK_HZ = 62_500_000

# BenchProbe offsets past H1a's words
PROBE_FIELDS, PROBE_TICKS, PROBE_TICK_COUNT, PROBE_ACTOR_TIME = 0x2C, 0x30, 0x34, 0x38
PROBE_LAYOUT, PROBE_ACTOR_IDS, PROBE_CLOCK_READ_COST = 0x3C, 0x40, 0x44

FIELD_WORDS = ["seq", "count", "switches", "idle", "graph", "audio", "rsp", "rsp_audio", "rdp_pipe", "rdp_buf"]
PHASES = ["pre", "update", "draw", "game", "post", "rcp_wait", "tail"]
SYSTEMS = ["other", "actor_update", "actor_draw", "collision", "room_draw", "skybox", "environment", "camera",
           "message", "effect", "audio_update", "bench"]
TICK_WORDS = (["tick", "start", "wall", "switches", "idle", "graph", "audio", "sched", "irqmgr", "padmgr", "dmamgr",
               "rsp", "rsp_audio", "rdp_pipe", "rdp_buf", "clock_reads"]
              + [f"wall_{p}" for p in PHASES] + [f"cpu_{p}" for p in PHASES] + [f"sys_{s}" for s in SYSTEMS])
TICK_RING = 640
FIELD_RING = 4
WRITING = 0xFFFFFFFF  # BENCH_RECORD_WRITING
ACTOR_IDS = 0x2B2  # ACTOR_ID_MAX, include/z64actor_dlftbls.h
LAYOUT = (4 * len(FIELD_WORDS)) << 24 | FIELD_RING << 20 | (4 * len(TICK_WORDS)) << 10 | TICK_RING

# Resources summarized per field and per tick: (name, unit, how to get it from a delta row)
RESOURCES = [
    ("cpu", "pclk", lambda d: d["wall"] - d["idle"]),
    ("rsp", "rsp", lambda d: d["rsp"]),
    ("rsp_gfx", "rsp", lambda d: d["rsp"] - d["rsp_audio"]),
    ("rsp_audio", "rsp", lambda d: d["rsp_audio"]),
    ("rdp_pipe", "rclk", lambda d: d["rdp_pipe"]),
    ("rdp_buf", "rclk", lambda d: d["rdp_buf"]),
]
UNIT_MS = {"pclk": 1000 / PCLK_HZ, "rsp": 1000 / RSP_HZ, "rclk": 1000 / RCLK_HZ}

U32 = 1 << 32

ACTOR_TABLE = Path(__file__).resolve().parents[2] / "include/tables/actor_table.h"


def actor_names():
    """Actor enum names by id from include/tables/actor_table.h. An id listed twice is under
    `#if MM_VERSION >= N64_US`, and the first entry is the US one."""
    names = {}
    for m in re.finditer(r"^/\* 0x(\w+) \*/ DEFINE_ACTOR(?:_INTERNAL\(\w+, |\(\w+, |_UNSET\()(\w+)",
                         ACTOR_TABLE.read_text(), flags=re.M):
        names.setdefault(int(m.group(1), 16), m.group(2))
    if sorted(names) != list(range(ACTOR_IDS)):
        raise SystemExit(f"{ACTOR_TABLE}: {len(names)} actor ids, expected 0 to {ACTOR_IDS - 1}")
    return [names[i] for i in range(ACTOR_IDS)]


def a(probe, off):
    return f"0x{probe + off:08X}"


def field_peeks(probe):
    """The whole retrace ring: a runner field can end before or after the scheduler's snapshot of the
    retrace at its end, so a field may hold zero or two new snapshots."""
    return [f"peek f{n}_{w} [{a(probe, PROBE_FIELDS)}]+0x{4 * (n * len(FIELD_WORDS) + i):X} w"
            for n in range(FIELD_RING) for i, w in enumerate(FIELD_WORDS)]


def actor_peeks(probe, tag):
    return [f"peek {tag}{i} [{a(probe, PROBE_ACTOR_TIME)}]+0x{4 * i:X} w" for i in range(2 * ACTOR_IDS)]


def start_steps(probe):
    """Steps right after `mark window`: the layout check, the field before the window, the actor totals."""
    return ([f"peek layout {a(probe, PROBE_LAYOUT)} w", f"peek actor_ids {a(probe, PROBE_ACTOR_IDS)} w",
             f"peek clock_read_cost {a(probe, PROBE_CLOCK_READ_COST)} w"]
            + field_peeks(probe) + actor_peeks(probe, "as"))


SETTLE_FIELDS = 2


def end_steps(probe):
    """Steps after the window's last field: two more reads of the retrace ring, then the tick ring and the
    actor totals."""
    ring = [f"peek t{n}_{w} [{a(probe, PROBE_TICKS)}]+0x{4 * (n * len(TICK_WORDS) + i):X} w"
            for n in range(TICK_RING) for i, w in enumerate(TICK_WORDS)]
    settle = (["wait 1"] + field_peeks(probe)) * SETTLE_FIELDS
    return settle + [f"peek tick_count {a(probe, PROBE_TICK_COUNT)} w"] + ring + actor_peeks(probe, "ae")


def pctl(values, q):
    """Nearest-rank percentile."""
    s = sorted(values)
    return s[max(0, math.ceil(q * len(s)) - 1)]


def stats(values, unit, field_ms):
    ms = [v * UNIT_MS[unit] for v in values]
    out = {}
    for name, v in (("max", max(ms)), ("p99", pctl(ms, 0.99)), ("p50", pctl(ms, 0.50))):
        out[f"{name}_ms"] = round(v, 4)
        out[f"{name}_share"] = round(v / field_ms, 4)
    return out


def delta(b, a_, keys):
    return {k: (b[k] - a_[k]) % U32 for k in keys}


def analyze(name, peeks, start_frame, window, stats_rows, out_dir, write_tsv):
    """Writes load_fields.tsv, load_ticks.tsv, load_actors.tsv and load_summary.json; returns the summary."""
    first = peeks.get(start_frame, {})
    if first.get("layout") != LAYOUT or first.get("actor_ids") != ACTOR_IDS:
        raise SystemExit(f"{name}: BenchProbe layout 0x{first.get('layout', 0):08X}, actor ids "
                         f"{first.get('actor_ids')}; load.py expects 0x{LAYOUT:08X} and {ACTOR_IDS}")
    end = peeks.get(start_frame + window + SETTLE_FIELDS, {})

    # Fields: the intervals between consecutive retrace snapshots, from the latest at the window's start
    # to the latest at its end. A peek can see a record's cache lines from before and after the store
    # that completed it (measured: one line stale while the next line and `seq` were new), so a record
    # counts only once two consecutive reads agree; it stays in the ring for 4 retraces.
    reads, latest = {}, {}
    for frame in range(start_frame, start_frame + window + SETTLE_FIELDS + 1):
        p = peeks.get(frame, {})
        recs = [{w: p.get(f"f{n}_{w}") for w in FIELD_WORDS} for n in range(FIELD_RING)]
        if any(v is None for r in recs for v in r.values()):
            raise SystemExit(f"{name}: missing BenchField peeks at frame {frame}")
        valid = [r for r in recs if r["seq"] != WRITING]
        for r in valid:
            reads.setdefault(r["seq"], []).append(r)
        latest[frame] = max(r["seq"] for r in valid)
    seen = {q: rs[-1] for q, rs in reads.items() if len(rs) >= 2 and rs[-1] == rs[-2]}
    s0, s1 = latest[start_frame], latest[start_frame + window]
    missing = [q for q in range(s0, s1 + 1) if q not in seen]
    if missing:
        raise SystemExit(f"{name}: retrace snapshots {missing[:5]} had no two agreeing reads")
    runner = [int(stats_rows[f]["cpu_cycles"]) - int(stats_rows[f - 1]["cpu_cycles"])
              for f in range(start_frame, start_frame + window)]
    fields = []
    for q in range(s0 + 1, s1 + 1):
        prev, cur = seen[q - 1], seen[q]
        d = delta(cur, prev, FIELD_WORDS[1:])
        fields.append({"seq": q, "wall": d["count"] * 2, "cpu_busy": d["count"] * 2 - d["idle"], "idle": d["idle"],
                       "graph": d["graph"], "audio": d["audio"], "switches": d["switches"],
                       "rsp": d["rsp"], "rsp_gfx": d["rsp"] - d["rsp_audio"], "rsp_audio": d["rsp_audio"],
                       "rdp_pipe": d["rdp_pipe"], "rdp_buf": d["rdp_buf"]})
    snaps = [seen[s0], seen[s1]]
    write_tsv(out_dir / "load_fields.tsv", fields)
    field_ms = pctl(runner, 0.5) * UNIT_MS["pclk"]

    # Ticks wholly inside the window, by their start and end counts against the window's snapshots
    count = end.get("tick_count")
    if count is None:
        raise SystemExit(f"{name}: missing the tick ring peeks")
    w0, w1 = snaps[0]["count"], snaps[-1]["count"]
    span = (w1 - w0) % U32
    ticks = []
    for n in range(TICK_RING):
        t = {w: end[f"t{n}_{w}"] for w in TICK_WORDS}
        off = (t["start"] - w0) % U32
        if t["tick"] == WRITING or t["wall"] == 0 or off >= span or off + t["wall"] // 2 > span:
            continue
        ticks.append(t)
    ticks.sort(key=lambda t: t["tick"])
    if not ticks:
        raise SystemExit(f"{name}: no complete tick inside the window")
    if ticks[-1]["tick"] - ticks[0]["tick"] + 1 != len(ticks):
        raise SystemExit(f"{name}: tick ring holds {len(ticks)} window ticks from {ticks[0]['tick']} to "
                         f"{ticks[-1]['tick']}; it overran ({count} written, ring of {TICK_RING})")
    tick_rows = []
    for t in ticks:
        row = dict(t)
        row["cpu_busy"] = t["wall"] - t["idle"]
        row["rsp_gfx"] = t["rsp"] - t["rsp_audio"]
        row["other_threads"] = (t["wall"] - t["idle"] - t["graph"] - t["audio"] - t["sched"] - t["irqmgr"]
                                - t["padmgr"] - t["dmamgr"])
        tick_rows.append(row)
    write_tsv(out_dir / "load_ticks.tsv", tick_rows)

    # Actors: the window's update and draw time per actor id
    actors = []
    names = actor_names()
    for i in range(ACTOR_IDS):
        upd = (end[f"ae{i}"] - first[f"as{i}"]) % U32
        drw = (end[f"ae{ACTOR_IDS + i}"] - first[f"as{ACTOR_IDS + i}"]) % U32
        if upd or drw:
            actors.append({"actor_id": f"0x{i:03X}", "actor": names[i], "update_pclk": upd, "draw_pclk": drw,
                           "total_pclk": upd + drw})
    actors.sort(key=lambda r: (-r["total_pclk"], r["actor_id"]))
    keys = ["actor_id", "actor", "update_pclk", "draw_pclk", "total_pclk"]
    if actors:
        write_tsv(out_dir / "load_actors.tsv", actors, keys)
    else:
        (out_dir / "load_actors.tsv").write_text("\t".join(keys) + "\n")

    n_ticks = len(ticks)
    clock_cost = first["clock_read_cost"]
    summary = {
        "scene": name,
        "field_pclk_p50": pctl(runner, 0.5),
        "field_pclk_min": min(runner),
        "field_pclk_max": max(runner),
        "snapshot_wall_pclk_min": min(f["wall"] for f in fields),
        "snapshot_wall_pclk_max": max(f["wall"] for f in fields),
        "field_ms": round(field_ms, 4),
        "fields": len(fields),
        "ticks": n_ticks,
        "first_tick": ticks[0]["tick"],
        "last_tick": ticks[-1]["tick"],
        "per_field": {r: stats([fn(f) for f in fields], unit, field_ms) for r, unit, fn in RESOURCES},
        "per_tick": {r: stats([fn(t) for t in ticks], unit, field_ms) for r, unit, fn in RESOURCES},
        "tick_cpu": {k: stats([t[k] for t in ticks], "pclk", field_ms)
                     for k in ["graph", "audio", "sched", "irqmgr", "padmgr", "dmamgr"]},
        "tick_switches": stats_counts([t["switches"] for t in ticks]),
        "tick_phase_wall": {p: stats([t[f"wall_{p}"] for t in ticks], "pclk", field_ms) for p in PHASES},
        "tick_phase_cpu": {p: stats([t[f"cpu_{p}"] for t in ticks], "pclk", field_ms) for p in PHASES},
        "tick_system": {s: stats([t[f"sys_{s}"] for t in ticks], "pclk", field_ms) for s in SYSTEMS},
        "top_actors": [{"actor_id": r["actor_id"], "actor": r["actor"],
                        "update_ms_per_tick": round(r["update_pclk"] * UNIT_MS["pclk"] / n_ticks, 4),
                        "draw_ms_per_tick": round(r["draw_pclk"] * UNIT_MS["pclk"] / n_ticks, 4)}
                       for r in actors[:10]],
        "hook_clock_read_pclk": clock_cost,
        "hook_clock_reads_per_tick": stats_counts([t["clock_reads"] for t in ticks]),
        "hook_ms_per_tick": stats([t["clock_reads"] * clock_cost for t in ticks], "pclk", field_ms),
    }
    (out_dir / "load_summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    return summary


def stats_counts(values):
    return {"max": max(values), "p99": pctl(values, 0.99), "p50": pctl(values, 0.5)}


LOAD_COLS = ["cpu", "rsp", "rsp_gfx", "rsp_audio", "rdp_pipe", "rdp_buf"]


def flat(summary):
    """One load.tsv row: per field and per tick max, p99 and p50 of each resource as a share of a field."""
    row = {"scene": summary["scene"], "field_ms": summary["field_ms"], "fields": summary["fields"],
           "ticks": summary["ticks"]}
    for scope in ("per_field", "per_tick"):
        tag = "f" if scope == "per_field" else "t"
        for r in LOAD_COLS:
            for q in ("max", "p99", "p50"):
                row[f"{tag}_{r}_{q}"] = summary[scope][r][f"{q}_share"]
    row["hook_ms_p50"] = summary["hook_ms_per_tick"]["p50_ms"]
    return row
