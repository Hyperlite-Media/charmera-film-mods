#!/usr/bin/env python3
"""Run the camera's stock SD updater (RAM 0x2117c) on a simulated flash chip.

The firmware's own update code runs as ARM code: name parsing, checksum, the
erase/program/verify loop and the final erase. Only I/O is stubbed: the SD file,
memory allocation, the 2 MB SPI flash (64 KB erase, 256-byte program/read, with
addresses wrapping at 2 MB like the real chip), and screen/sound/delay calls.

Needs: pip install unicorn capstone
Usage: python3 emulate_updater.py UPDATE.bin --flash current_flash.bin [--out after.bin]
"""
import argparse
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, \
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

HDR, RAM_SIZE, CHIP = 0x200, 0x1B0000, 0x200000
UPGRADE = 0x2117C
DIRENT = 0x110C54            # find-first result used by the updater; name at +0x12
HEAP, STACK_TOP, STOP = 0x400000, 0x510000, 0x600000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("update", help="JH_5408<version>_<sum>.bin")
    ap.add_argument("--flash", required=True, help="2 MB image the simulated chip starts with")
    ap.add_argument("--out", help="write the flash contents after the update here")
    args = ap.parse_args()

    flash = bytearray(open(args.flash, "rb").read())
    before = bytes(flash)
    upd = open(args.update, "rb").read()
    name = os.path.basename(args.update).encode()

    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(0, 0x200000)
    mu.mem_write(0, before[HDR:HDR + RAM_SIZE])      # firmware as the boot ROM loads it
    mu.mem_map(HEAP, 0x100000)
    mu.mem_map(STACK_TOP - 0x10000, 0x10000)
    mu.mem_map(STOP, 0x1000)
    for base in (0xC0000000, 0xD0000000, 0xD0800000):
        mu.mem_map(base, 0x100000 if base == 0xC0000000 else 0x10000)
    mu.mem_write(DIRENT + 0x12, name + b"\0")

    def arg(i):
        if i < 4:
            return mu.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i])
        sp = mu.reg_read(UC_ARM_REG_SP)
        return struct.unpack("<I", mu.mem_read(sp + 4 * (i - 4), 4))[0]

    pos = [0]
    heap = [HEAP]
    stats = {"erase": [], "program": 0, "read": 0}

    def f_open():  return 3
    def f_close(): return 0

    def f_fstat():
        mu.mem_write(arg(1) + 4, struct.pack("<I", len(upd)))
        return 0

    def f_read():
        chunk = upd[pos[0]:pos[0] + arg(2)]
        mu.mem_write(arg(1), chunk)
        pos[0] += len(chunk)
        return len(chunk)

    def f_lseek():
        pos[0] = arg(1)
        return pos[0]

    def malloc():
        p = heap[0]
        heap[0] += (arg(0) + 31) & ~31
        return p

    def erase():
        a = (arg(0) % CHIP) & ~0xFFFF
        stats["erase"].append((arg(0), a))
        flash[a:a + 0x10000] = b"\xff" * 0x10000
        return 0

    def program():
        a = arg(0) % CHIP
        page = bytes(mu.mem_read(arg(1), 256))
        flash[a:a + 256] = bytes(x & y for x, y in zip(flash[a:a + 256], page))  # NOR: 1 -> 0 only
        stats["program"] += 1
        return 0

    def read():
        a = arg(0) % CHIP
        mu.mem_write(arg(1), bytes(flash[a:a + 256]))
        stats["read"] += 1
        return 0

    done = []

    def finished():                     # 0x30f08: the "press power" loop after 100 %
        done.append(1)
        mu.emu_stop()
        return 0

    ok = lambda: 0
    stubs = {0xE2E48: f_open, 0xE2A54: f_fstat, 0xE2EA8: f_read, 0xE2D80: f_lseek, 0xE28E0: f_close,
             0x544: malloc, 0x560: ok, 0x1B20: erase, 0x1AEC: program, 0x119C: read,
             0x30F08: finished, 0x208F0: ok, 0x2FF88: ok, 0x8FC80: ok, 0x30B60: ok, 0x30CF4: ok,
             0x284B4: ok, 0x30E00: ok, 0x90170: ok, 0xE27CC: ok, 0x90344: ok}

    def on_code(uc, addr, size, _):
        if addr in stubs:
            uc.reg_write(UC_ARM_REG_R0, stubs[addr]() & 0xFFFFFFFF)
            if not done:
                uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    mu.hook_add(UC_HOOK_CODE, on_code)
    mu.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
    mu.reg_write(UC_ARM_REG_LR, STOP)
    try:
        mu.emu_start(UPGRADE, STOP, count=200_000_000)
    except UcError as e:
        sys.exit(f"emulation fault: {e} at pc=0x{mu.reg_read(UC_ARM_REG_PC):x}")

    result = "reached the 'press power' screen (update finished)" if done else \
        f"returned early, pc=0x{mu.reg_read(UC_ARM_REG_PC):x} (checksum or file rejected)"
    print(result)
    print(f"block erases (address passed -> block): " +
          ", ".join(f"0x{a:06x}->0x{b:06x}" for a, b in stats["erase"][:4]) +
          (" ..." if len(stats["erase"]) > 5 else "") +
          (f", last 0x{stats['erase'][-1][0]:06x}->0x{stats['erase'][-1][1]:06x}" if len(stats["erase"]) > 4 else ""))
    print(f"pages programmed {stats['program']}, pages read back {stats['read']}")
    changed = sorted({a & ~0xFFF for a in range(CHIP) if flash[a] != before[a]})
    print("4 KB sectors changed vs before:", ", ".join(f"0x{a:06x}" for a in changed[:40]) or "none")
    expected = bytearray(before)
    expected[:len(upd)] = upd
    blank = all(b == 0xFF for b in flash[len(upd):(len(upd) + 0x10000) & ~0xFFFF]) if done else False
    print("flash below file end equals the file:", bytes(flash[:len(upd)]) == upd)
    print("everything from 0x180000 (internal photos) unchanged:", flash[0x180000:] == before[0x180000:])
    if args.out:
        open(args.out, "wb").write(flash)


if __name__ == "__main__":
    main()
