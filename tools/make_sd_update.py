#!/usr/bin/env python3
"""Build an SD-card update file for the camera's stock updater.

The stock updater (RAM 0x2117c) flashes JH_5408<version>_<sum>.bin from the card root
starting at flash address 0, then erases the 64 KB block at (end of file + 64 KB).
Official files end just after the resources, so that erase lands on the settings block
(0x170000) and resets the camera's settings. A file that reaches the end of the chip
makes the erase wrap to 0x010000 (firmware code) and bricks the camera.

This tool therefore writes only 0x000000-0x168FFF (firmware + resources):
  - the post-update erase hits 0x170000 (settings reset, as with an official update)
  - internal photos at 0x180000+ are never touched
  - no personal data from the source image is included

Usage:
  python3 make_sd_update.py charmera_loader.bin            # writes JH_5408<ver>_<sum>.bin
  python3 make_sd_update.py image.bin --version 202511031500 -o outdir
  python3 make_sd_update.py factory_dump.bin --restore     # factory restore file

A restore file keeps the factory version in its name. On a camera with the loader, it needs
HACK.BIN on the card too (mods/restore.c starts the updater).
"""
import argparse
import os
import sys
import time

END = 0x169000                  # end of firmware + resources (resource pack ends 0x168F00)
SETTINGS = 0x170000             # block the updater erases after an END-sized file
VERSION_AT = 0x218D0            # 12-digit firmware version string (file offset)
STOCK_VERSION = b"202505201029"
STOCK_CHECK_ENTRY = bytes.fromhex("10402de930d04de2")  # stock update check, replaced by the loader


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="2 MB flash image to take firmware + resources from")
    ap.add_argument("--version", default=time.strftime("%Y%m%d%H%M"),
                    help="12-digit version for the new firmware (default: now)")
    ap.add_argument("-o", "--outdir", default=".")
    ap.add_argument("--restore", action="store_true",
                    help="build a factory restore file from an unmodified dump")
    args = ap.parse_args()

    img = bytearray(open(args.image, "rb").read())
    if len(img) != 0x200000:
        sys.exit("expected a 2 MB flash image")
    if args.restore:
        # Refuse anything but unmodified factory firmware: check each place the mods patch.
        if bytes(img[VERSION_AT:VERSION_AT + 12]) != STOCK_VERSION \
                or bytes(img[0x21824:0x2182C]) != STOCK_CHECK_ENTRY \
                or (img[0xEA18], img[0xEA2C], img[0xEA34]) != (85, 75, 80):
            sys.exit("--restore needs an unmodified factory dump (JH5408 202505201029)")
        args.version = STOCK_VERSION.decode()
    ver = args.version.encode()
    if len(ver) != 12 or not ver.isdigit():
        sys.exit("version must be 12 digits, e.g. 202511031500")
    if img[VERSION_AT:VERSION_AT + 12] != STOCK_VERSION and not img[VERSION_AT:VERSION_AT + 12].isdigit():
        sys.exit("no firmware version string at 0x218d0: not the JH5408 firmware")
    if ver == STOCK_VERSION and not args.restore:
        sys.exit("the version must differ from the stock one, or the camera will not update")
    if any(b != 0xFF for b in img[END:SETTINGS]):
        sys.exit("image has data between 0x169000 and 0x170000 that this file would drop")

    img[VERSION_AT:VERSION_AT + 12] = ver
    data = bytes(img[:END])
    name = "JH_5408%s_%08X.bin" % (ver.decode(), sum(data) & 0xFFFFFFFF)
    path = os.path.join(args.outdir, name)
    open(path, "wb").write(data)
    print(f"{path}  ({len(data)} bytes, flashes 0x000000-0x{END - 1:06X}, then the camera erases "
          f"0x{SETTINGS:06X}: settings reset)")


if __name__ == "__main__":
    main()
