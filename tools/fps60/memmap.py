#!/usr/bin/env python3
"""
Static RDRAM layout from a build's ELF: boot/code segment sizes and the system heap.

    tools/fps60/memmap.py [build/n64-us/mm-n64-us.elf] [other.elf ...]

The system heap runs from the end of the `buffers` segment to the framebuffers at the top of
RAM (src/boot/main.c), so anything added to code/bss shrinks it one-for-one.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NM = "mips-linux-gnu-nm"


def symbols(elf: Path) -> dict[str, int]:
    out = subprocess.run([NM, str(elf)], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def layout(elf: Path) -> dict[str, int]:
    s = symbols(elf)
    heap_start = s["_buffersSegmentEnd"]
    heap_end = s["gHiBuffer"]  # FRAMEBUFFERS_START_ADDR
    return {
        "boot segment": s["_bootSegmentEnd"] - s["_bootSegmentStart"],
        "code .text": s["_codeSegmentTextSize"],
        "code .data": s["_codeSegmentDataSize"],
        "code .rodata": s["_codeSegmentRoDataSize"],
        "code .bss": s["_codeSegmentBssSize"],
        "code total": s["_codeSegmentEnd"] - s["_codeSegmentStart"],
        "buffers segment": s["_buffersSegmentEnd"] - s["_buffersSegmentStart"],
        "system heap": heap_end - heap_start,
        "framebuffers + hi buffers": 0x80800000 - heap_end,
    }


def main():
    elfs = [Path(a) for a in sys.argv[1:]] or [ROOT / "build/n64-us/mm-n64-us.elf"]
    tables = [layout(e) for e in elfs]
    print("| region | " + " | ".join(str(e.relative_to(ROOT) if e.is_absolute() else e) for e in elfs) + " |")
    print("|---|" + "---:|" * len(elfs))
    for key in tables[0]:
        print(f"| {key} | " + " | ".join(f"{t[key]:,} B ({t[key] / 1024:.1f} KiB)" for t in tables) + " |")
    if len(tables) == 2:
        print()
        for key in tables[0]:
            d = tables[1][key] - tables[0][key]
            if d:
                print(f"- {key}: {d:+,} B ({d / 1024:+.1f} KiB)")


if __name__ == "__main__":
    main()
