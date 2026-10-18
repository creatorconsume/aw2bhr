#!/usr/bin/env python3
"""Build the browser playtest page (an Artifact) for Dead Wars and the AW2 Remix.

Inlines the gbajs emulator, its open-source BIOS replacement, the Dead Wars ROM
and the remix BPS patch into tools/playtest/template.html. The AW2 ROM itself is
never embedded: the page asks the player for their own copy.

usage: build_page.py --gbajs DIR --deadwars ROM --bps PATCH --out FILE
                     [--dw-version TEXT] [--remix-version TEXT]
  DIR is a checkout of https://github.com/endrift/gbajs (browser version).
"""
import argparse
import base64
import hashlib
import html
import os

HERE = os.path.dirname(os.path.abspath(__file__))
GBAJS_FILES = ["util.js", "core.js", "arm.js", "thumb.js", "mmu.js", "io.js", "audio.js", "video.js",
               "video/software.js", "irq.js", "keypad.js", "sio.js", "savedata.js", "gpio.js", "gba.js"]

DW_CHECKS = [
    "Mission 1: move, attack, Search a house, then End Day.",
    "Survive to the first night: fog, ambushes, and zombies moving one tile further.",
    "Recruit at a Workshop (mission 2) and Board/Drop with the Bus (mission 3).",
    "Suspend from the START menu, then Continue from the title.",
    "Tell us if a mission feels unfair or too easy, and on which day.",
]
AW_CHECKS = [
    "Start a new campaign: the first five Orange Star missions are remade to be very hard.",
    "Cleanup: 10 enemy units hold the river ford. Does the tutorial still play through?",
    "Border Skirmish and Orange Dawn now have fog of war and fortified bridges.",
    "Lash Out keeps its 7-day limit. Tell us if it is impossible or just brutal.",
    "Speed is your choice: Options > Visual Off (or B/C) doubles map speed, like the original.",
]


def b64(path):
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode()


def checks(items):
    return "\n".join("          <li>%s</li>" % html.escape(i) for i in items)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gbajs", required=True)
    ap.add_argument("--deadwars", required=True)
    ap.add_argument("--bps", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dw-version", default="dev")
    ap.add_argument("--remix-version", default="v0.1")
    a = ap.parse_args()

    js = []
    for name in GBAJS_FILES:
        with open(os.path.join(a.gbajs, "js", name), encoding="utf-8") as f:
            js.append("/* gbajs %s */\n%s" % (name, f.read()))
    js = "\n".join(js).replace("</script", "<\\/script")
    with open(os.path.join(a.gbajs, "COPYING"), encoding="utf-8") as f:
        js = "/* gbajs — " + f.read().replace("*/", "* /") + "*/\n" + js

    with open(a.deadwars, "rb") as f:
        dw = f.read()
    # CRC32 of the unmodified AW2 (USA) ROM, read from the BPS footer (source checksum)
    with open(a.bps, "rb") as f:
        bps = f.read()
    aw2_crc = int.from_bytes(bps[-12:-8], "little")

    with open(os.path.join(HERE, "template.html"), encoding="utf-8") as f:
        page = f.read()
    for key, val in {
        "/*@@GBAJS@@*/": js,
        "@@BIOS_B64@@": b64(os.path.join(a.gbajs, "resources", "bios.bin")),
        "@@DW_B64@@": base64.b64encode(dw).decode(),
        "@@BPS_B64@@": base64.b64encode(bps).decode(),
        "@@AW2_CRC@@": "%08X" % aw2_crc,
        "@@DW_SHA1@@": hashlib.sha1(dw).hexdigest(),
        "@@DW_VERSION@@": html.escape(a.dw_version),
        "@@REMIX_VERSION@@": html.escape(a.remix_version),
        "@@DW_CHECKS@@": checks(DW_CHECKS),
        "@@AW_CHECKS@@": checks(AW_CHECKS),
    }.items():
        page = page.replace(key, val)
    assert "@@" not in page, "unreplaced placeholder"
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(page)
    print("wrote %s (%d KB), AW2 source CRC32 %08X" % (a.out, len(page) // 1024, aw2_crc))


if __name__ == "__main__":
    main()
