#!/usr/bin/env python3
"""Build all mods (mods/*.c, entry point main() in mods/main.c) into HACK.BIN.

Usage:
  python3 build_payload.py             # -> HACK.BIN
  python3 build_payload.py --listing   # also print the disassembly
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.expanduser("~/Library/Arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin")
SDK = os.path.join(HERE, "sdk")
CFLAGS = ["-mcpu=arm7tdmi", "-marm", "-Os", "-fPIE", "-ffreestanding", "-fno-builtin",
          "-fno-tree-loop-distribute-patterns", "-ffunction-sections", "-Wall", "-I", SDK]
MAX_SIZE = 0x10000  # the loader reads at most 64 KB


def tool(name):
    """arm-none-eabi tool from PATH, or from the Adafruit Arduino board package."""
    return shutil.which("arm-none-eabi-" + name) or os.path.join(TOOLS, "arm-none-eabi-" + name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listing", action="store_true")
    ap.add_argument("-o", "--output", default=os.path.join(HERE, "HACK.BIN"))
    args = ap.parse_args()

    srcs = sorted(glob.glob(os.path.join(HERE, "mods", "*.c")))
    with tempfile.TemporaryDirectory() as tmp:
        elf = os.path.join(tmp, "payload.elf")
        subprocess.run([tool("gcc"), *CFLAGS, "-nostdlib", "-Wl,--gc-sections", "-Wl,--no-warn-mismatch",
                        "-T", os.path.join(SDK, "payload.ld"), "-o", elf,
                        os.path.join(SDK, "crt0.S"), os.path.join(SDK, "lib.c"), *srcs, "-lgcc"], check=True)
        subprocess.run([tool("objcopy"), "-O", "binary", "-R", ".bss", elf, args.output], check=True)
        nm = subprocess.run([tool("nm"), elf], capture_output=True, text=True, check=True).stdout
        bss_end = int(next(l.split()[0] for l in nm.splitlines() if l.endswith(" __bss_end")), 16)
        open(os.path.splitext(args.output)[0] + ".map", "w").write(nm)  # symbols for emulate.py
        if args.listing:
            print(subprocess.run([tool("objdump"), "-d", "-h", elf], capture_output=True, text=True).stdout)
    size = os.path.getsize(args.output)
    if bss_end > MAX_SIZE:
        sys.exit(f"Payload needs {bss_end} bytes with .bss; the loader's buffer is {MAX_SIZE}.")
    print(f"{args.output}  {size} bytes (+{bss_end - size} bytes .bss)")


if __name__ == "__main__":
    main()
