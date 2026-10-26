"""The fps60 scene corpus. run.py reads SCENES; every other file in tools/fps60 treats it as data.

A scene boots one BENCH scene (BOOT, the sBenchScenes index in src/code/bench.c), waits until the
scene settles (gBenchProbe.settled: the Play fade-in is done, or the file-select main screen is idle),
then measures WINDOW fields. `inputs` drive controller 1 by logic tick, counted from the first tick
after the settle tick: (tick, buttons, stick_x, stick_y) holds from that tick until the next entry.
Stick values are N64 units; positive y is up. ares' gamepad reaches 85 on an axis and 69 on a diagonal
(ares/n64/controller/gamepad/gamepad.cpp), so the routes stay inside what a pad can send.
"""

import math

WINDOW = 600

# sBenchScenes in src/code/bench.c, by index. run.py checks the name the ROM prints at boot.
BOOT = {
    "south_clock_town": 0,
    "termina_field": 1,
    "mountain_village_winter": 2,
    "great_bay_coast": 3,
    "file_select_empty": 4,
    "file_select_named": 5,
    "retail_boot": 6,
    "south_clock_town_deku": 7,
}

BUTTONS = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "Start": 0x1000, "R": 0x0010, "C-down": 0x0004}


def press(tick, button):
    # One tick down, then released: the game sees the press on exactly that tick.
    return [(tick, BUTTONS[button], 0, 0), (tick + 1, 0, 0, 0)]


def circle(start, ticks, period, radius=80):
    # mmbench's sct loops: the stick turns a full circle every `period` ticks, which keeps Link
    # running loops in the plaza instead of reaching an exit, a wall, or an NPC that opens a textbox.
    return [(start + t, 0, round(radius * math.sin(2 * math.pi * t / period)),
             round(radius * math.cos(2 * math.pi * t / period))) for t in range(ticks)]


SCENES = [
    {"name": "title", "boot": "retail_boot", "inputs": []},
    {"name": "filesel", "boot": "file_select_named", "inputs": []},
    # mmbench sct: stick up for 60 fields, then loops of 180 fields; Play ticks every 3 fields.
    {"name": "sct", "boot": "south_clock_town", "inputs": [(0, 0, 0, 85)] + circle(20, 180, 60)},
    # mmbench field runs up, then up-right. After the entrance cutscene that turn runs Link into the
    # wall by the Clock Town gate, so the route keeps the stick up: out of the gate and into the field.
    {"name": "field", "boot": "termina_field", "inputs": [(0, 0, 0, 85)]},
    {"name": "mountain", "boot": "mountain_village_winter", "inputs": []},
    {"name": "greatbay", "boot": "great_bay_coast", "inputs": []},
    # P2-ref part a entry criteria (docs/fps60/architecture.md Q9); README.md, Scenes, says what each exercises.
    # pause: open, turn to the map page, close. Kaleido runs at divisor 2 and opening captures the background.
    {"name": "pause", "boot": "south_clock_town", "inputs": press(10, "Start") + press(80, "R") + press(150, "Start")},
    # Back through the gate tunnel into West Clock Town. MM never starts a tile transition, so a scene change.
    {"name": "transition", "boot": "termina_field", "inputs": [(0, 0, 0, -85)]},
    # Taking off the Deku Mask: the transformation enables priority motion blur (Player_Action_86).
    {"name": "blur", "boot": "south_clock_town_deku", "inputs": press(10, "C-down")},
    # Out of the gate, then one slow circle: bridge, town wall and grass while the camera turns.
    {"name": "moving", "boot": "termina_field", "inputs": [(0, 0, 0, 85)] + circle(40, 300, 300)},
]
