#!/usr/bin/env python3
"""Finalize a GBA ROM header: optional Nintendo logo (copied from a ROM you own),
complement checksum, and padding to a 4-byte multiple.

usage: gbafix.py ROM [--logo-from OTHER_ROM]
Emulators do not need the logo; only real hardware / a real BIOS boot intro does."""
import sys

def main():
    path = sys.argv[1]
    rom = bytearray(open(path, "rb").read())
    if "--logo-from" in sys.argv:
        src = sys.argv[sys.argv.index("--logo-from") + 1]
        try:
            rom[0x04:0xA0] = open(src, "rb").read()[0x04:0xA0]
        except OSError:
            print("gbafix: logo source not found, leaving logo blank")
    rom[0xB2] = 0x96
    chk = 0
    for b in rom[0xA0:0xBD]:
        chk = (chk - b) & 0xFF
    rom[0xBD] = (chk - 0x19) & 0xFF
    while len(rom) % 4:
        rom.append(0)
    open(path, "wb").write(rom)

if __name__ == "__main__":
    main()
