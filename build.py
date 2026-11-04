#!/usr/bin/env python3
"""Build the loader firmware image from your own factory flash dump.

Usage:
  python3 build.py factory_dump.bin         # writes charmera_loader.bin
  python3 tools/make_sd_update.py charmera_loader.bin      # SD card install file

The dump is the camera's whole 2 MB flash (flasher/ can read it). This applies two changes and
leaves every other byte as it was:
  - JPEG quality 85/75/80 becomes 92/85/90
  - the stock update check at RAM 0x21624 becomes the SD card loader (loader.S)
The dump and the output contain Kodak's firmware and your camera's settings and internal photos,
so keep them out of the repository (see .gitignore).
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ARDUINO_TOOLS = os.path.expanduser("~/Library/Arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin")

FLASH_SIZE = 0x200000
FIRMWARE_END = 0x169000           # firmware + resources; the rest holds settings and photos
FIRMWARE_SHA1 = "91499bd3bff3acde053cb8c8255a73cdeb6e3d33"  # factory 0x000000-0x168FFF

HDR = 0x200                       # file offset = RAM address + 0x200
LOADER_RAM = 0x21624              # stock update check, only called from the SD mount handler
LOADER_END = 0x216D0              # firmware version string starts here; keep it
QUALITY = {0xEA18: (85, 92), 0xEA2C: (75, 85), 0xEA34: (80, 90)}  # file offset: (stock, new)
FIRMWARE = {
    "fw_open": 0xE2E48, "fw_read": 0xE2EA8, "fw_close": 0xE28E0,
    "fw_malloc": 0x544, "fw_free": 0x560,
    "FW_OPEN": 0xE2E48, "FW_WRITE": 0xE3130, "FW_CLOSE": 0xE28E0,
    "FW_SPRINTF": 0xDE758, "FW_DELAY": 0x90170,
}
# First instructions of the original update check, to be sure the image is the expected one.
ORIGINAL_ENTRY = bytes.fromhex("10402de930d04de2")


def tool(name):
    """arm-none-eabi tool from PATH, or from the Adafruit Arduino board package."""
    return shutil.which("arm-none-eabi-" + name) or os.path.join(ARDUINO_TOOLS, "arm-none-eabi-" + name)


def assemble(src, base, tmp):
    obj, elf, out = (os.path.join(tmp, os.path.basename(src) + ext) for ext in (".o", ".elf", ".bin"))
    subprocess.run([tool("as"), "-mcpu=arm7tdmi", "-o", obj, src], check=True)
    syms = [f"--defsym={k}=0x{v:x}" for k, v in FIRMWARE.items()]
    subprocess.run([tool("ld"), "-Ttext=0x%x" % base, "--no-warn-mismatch", *syms, "-o", elf, obj], check=True)
    subprocess.run([tool("objcopy"), "-O", "binary", "-j", ".text", elf, out], check=True)
    listing = subprocess.run([tool("objdump"), "-d", elf], check=True, capture_output=True, text=True).stdout
    return open(out, "rb").read(), listing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", help="unmodified 2 MB factory flash dump")
    ap.add_argument("-o", "--output", default=os.path.join(HERE, "charmera_loader.bin"))
    ap.add_argument("--listing", action="store_true", help="print disassembly")
    args = ap.parse_args()

    image = bytearray(open(args.dump, "rb").read())
    if len(image) != FLASH_SIZE:
        sys.exit(f"{args.dump} is {len(image)} bytes; expected a 2 MB flash dump.")
    if hashlib.sha1(image[:FIRMWARE_END]).hexdigest() != FIRMWARE_SHA1:
        sys.exit("This is not the unmodified firmware these patches were written for "
                 "(JH5408 version 202505201029), so nothing was built.")
    off = LOADER_RAM + HDR
    if image[off:off + len(ORIGINAL_ENTRY)] != ORIGINAL_ENTRY:
        sys.exit("Unexpected code at the loader location.")

    for at, (stock, new) in QUALITY.items():
        if image[at] != stock:
            sys.exit(f"Unexpected JPEG quality at 0x{at:x}.")
        image[at] = new

    with tempfile.TemporaryDirectory() as tmp:
        loader, listing = assemble(os.path.join(HERE, "loader.S"), LOADER_RAM, tmp)
    room = LOADER_END - LOADER_RAM
    if len(loader) > room:
        sys.exit(f"Loader is {len(loader)} bytes; only {room} fit.")
    image[off:off + room] = loader + b"\x00" * (room - len(loader))

    open(args.output, "wb").write(image)
    print(f"loader  {len(loader)}/{room} bytes at RAM 0x{LOADER_RAM:x} (file 0x{off:x})")
    print(f"image   {args.output}")
    if args.listing:
        print(listing)


if __name__ == "__main__":
    main()
