#!/usr/bin/env python3
"""Run a payload against the camera's firmware image in an ARM emulator.

The firmware's RAM image is mapped at 0, the payload is loaded elsewhere and
called the way the loader calls it. File and print functions are stubbed, any
other call into firmware code stops the run. Afterwards it lists every firmware
word the payload changed (with disassembly) and the files it wrote.

Needs: pip install unicorn capstone
Usage: python3 emulate.py HACK.BIN [--ini HACK.INI] [--image ../charmera_loader.bin]
                          [--call 0x2d374:5 ...]   # afterwards call firmware code with r0
                          [--poke 0x10fc85:7] [--call film_overlay:FRAME --png out.png]
  --call takes a firmware address or a payload symbol (from HACK.map next to HACK.BIN);
  r0 = FRAME passes a 160x80 RGB565 preview frame, which --png saves as an image.
"""
import argparse
import os
import re
import struct
import sys

import capstone
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, \
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

HERE = os.path.dirname(os.path.abspath(__file__))
HDR, RAM_SIZE = 0x200, 0x1B0000
PAYLOAD_BASE, STACK_TOP, STOP = 0x400000, 0x510000, 0x600000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("payload")
    ap.add_argument("--image", default=os.path.join(HERE, "..", "charmera_loader.bin"))
    ap.add_argument("--ini", help="HACK.INI to serve to the payload (default: none on the card)")
    ap.add_argument("--call", action="append", default=[], metavar="ADDR:R0",
                    help="after the payload, call firmware ADDR or a payload symbol with r0")
    ap.add_argument("--poke", action="append", default=[], metavar="ADDR:BYTE",
                    help="set a RAM byte before the calls (e.g. the filter mode at 0x10fc85)")
    ap.add_argument("--png", help="save the FRAME preview buffer as a PNG after the calls")
    ap.add_argument("--count", action="store_true", help="print payload instructions per call")
    ap.add_argument("--gp420", metavar="WxH:OUT.png",
                    help="r0 = GP420 passes a test photo buffer, saved half size to OUT.png")
    ap.add_argument("--card", action="append", default=[], metavar="NAME:SIZE",
                    help="a file on the fake card, seen by find-first and fstat")
    ap.add_argument("--recv", default="", metavar="ID,ID,...",
                    help="message ids the stubbed receive (0x25a44) returns, in order")
    args = ap.parse_args()

    image = open(args.image, "rb").read()
    ram = image[HDR:HDR + RAM_SIZE]
    payload = open(args.payload, "rb").read()

    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(0, 0x200000)
    mu.mem_write(0, ram)
    mu.mem_map(PAYLOAD_BASE, 0x10000)
    mu.mem_write(PAYLOAD_BASE, payload)
    mu.mem_map(STACK_TOP - 0x10000, 0x10000)
    mu.mem_map(STOP, 0x1000)
    mu.mem_map(0xD0800000, 0x1000)  # image processor registers (read as zero)

    files, fds = {}, {}

    def cstr(addr):
        return bytes(mu.mem_read(addr, 256)).split(b"\0")[0].decode("latin-1")

    def arg(i):
        if i < 4:
            return mu.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i])
        sp = mu.reg_read(UC_ARM_REG_SP)
        return struct.unpack("<I", mu.mem_read(sp + 4 * (i - 4), 4))[0]

    def sprintf():
        fmt, n = cstr(arg(1)), [2]

        def conv(m):
            v = arg(n[0]); n[0] += 1
            spec = m.group(0)
            if spec[-1] == "s":
                return cstr(v)
            if spec[-1] == "d":
                v = v - (1 << 32) if v & 0x80000000 else v
            return ("%" + spec[1:]) % v
        out = re.sub(r"%[-0-9]*[dxXus]", conv, fmt).encode("latin-1")
        mu.mem_write(arg(0), out + b"\0")
        return len(out)

    card = {"C:\\HACK.INI": open(args.ini, "rb").read()} if args.ini else {}
    for spec in args.card:
        fname, size = spec.split(":")
        card["C:\\" + fname] = bytes(int(size, 0))
    reads = {}

    def findfirst():
        prefix = cstr(arg(0)).split("*")[0]
        for path in card:
            if path.startswith(prefix):
                mu.mem_write(arg(1) + 0x12, path[3:].encode() + b"\0")
                return 50
        return 0xFFFFFFFF

    def fstat():
        mu.mem_write(arg(1) + 4, struct.pack("<I", len(reads[arg(0)][0])))
        return 0

    def fw_open():
        path, flags = cstr(arg(0)), arg(1)
        if flags == 0:                          # read-only: serve files from the fake card
            if path not in card:
                return 0xFFFFFFFF
            fd = 100 + len(reads)
            reads[fd] = [card[path], 0]
            return fd
        fd = 3 + len(fds)
        fds[fd] = path
        files[path] = b""
        return fd

    def fw_read():
        data, pos = reads[arg(0)]
        chunk = data[pos:pos + arg(2)]
        mu.mem_write(arg(1), chunk)
        reads[arg(0)][1] += len(chunk)
        return len(chunk)

    def fw_write():
        data = bytes(mu.mem_read(arg(1), arg(2)))
        files[fds[arg(0)]] += data
        return len(data)

    def record(name):
        def stub():
            args4 = [arg(i) for i in range(4)]
            print(f"    {name}({', '.join(str(a - (1 << 32) if a & 0x80000000 else a) for a in args4)})")
            return 0
        return stub

    def gamma_load():
        words = struct.unpack("<128I", bytes(mu.mem_read(arg(0), 512)))
        vals = []
        for w in words:
            v = w & 0xFF
            vals.append(v)
            for k in range(7):
                v += (w >> (8 + 2 * k)) & 3
                vals.append(v)
        loads.append(vals)
        print("    tone table loaded; output at input 0/64/128/256/512/768/1023:",
              [vals[i] for i in (0, 64, 128, 256, 512, 768, 1023)])
        return 0
    loads = []

    def edge_update():  # like 0x66698: ctx+0xe48 = ctx+0xd90
        ctx = struct.unpack("<I", mu.mem_read(0x1492E8, 4))[0]
        d8c, d90 = struct.unpack("<II", mu.mem_read(ctx + 0xD8C, 8))
        mu.mem_write(ctx + 0xE48, struct.pack("<I", d90))
        print(f"    edge settings now: normal {d8c}, low-light {d90}")
        return 0
    queue = [int(x, 0) for x in args.recv.split(",") if x]

    def receive():
        if not queue:
            raise SystemExit("receive called with no messages left (photo mode would wait here)")
        msg = queue.pop(0)
        mu.mem_write(arg(1), struct.pack("<I", msg))
        print(f"    receive -> 0x{msg:x}")
        return 0

    stubs = {0xE297C: findfirst, 0xE2A54: fstat, 0x25A44: receive, 0x98758: gamma_load, 0xDE758: sprintf, 0xE2E48: fw_open, 0xE3130: fw_write, 0xE2EA8: fw_read,
             0xE28E0: lambda: 0, 0x90170: lambda: 0,
             0x645E4: record("cdsp 0x30000013"), 0x64644: record("cdsp 0x30000014"),
             0x64768: record("ev step (cdsp 0x30000020)"),
             0x6493C: record("white balance (0x6493c)"),
             0x648E4: record("edge strengths (cmds 0x0b/0x0c)"),
             0x9A050: record("edge gain hw (0x9a050)"),
             0x66698: edge_update}
    stray = []
    allowed = set()

    def on_code(uc, addr, size, _):
        if addr in allowed:
            return
        if addr in stubs:
            uc.reg_write(UC_ARM_REG_R0, stubs[addr]() & 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        else:
            stray.append(addr)
            uc.emu_stop()

    mu.hook_add(UC_HOOK_CODE, on_code, begin=0, end=0x1FFFFF)
    executed = [0]

    def count(uc, addr, size, _):
        executed[0] += 1
    if args.count:
        mu.hook_add(UC_HOOK_CODE, count, begin=PAYLOAD_BASE, end=PAYLOAD_BASE + 0xFFFF)
    mu.reg_write(UC_ARM_REG_R0, PAYLOAD_BASE)
    mu.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
    mu.reg_write(UC_ARM_REG_LR, STOP)
    try:
        mu.emu_start(PAYLOAD_BASE, STOP, count=5_000_000)
    except UcError as e:
        sys.exit(f"Emulation fault: {e} at pc=0x{mu.reg_read(UC_ARM_REG_PC):x}")
    if stray:
        sys.exit(f"Payload called unstubbed firmware code at 0x{stray[0]:x}")
    if mu.reg_read(UC_ARM_REG_PC) != STOP:
        sys.exit(f"Payload did not return (pc=0x{mu.reg_read(UC_ARM_REG_PC):x})")
    print(f"returned r0={mu.reg_read(UC_ARM_REG_R0)}")

    after = bytes(mu.mem_read(0, RAM_SIZE))
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    changed = [a for a in range(0, RAM_SIZE, 4) if after[a:a + 4] != ram[a:a + 4]]
    print(f"{len(changed)} firmware words changed")
    for a in changed:
        def dis(b):
            ins = next(md.disasm(b, a), None)
            return f"{ins.mnemonic} {ins.op_str}" if ins else b.hex()
        print(f"  0x{a:05x}: {dis(ram[a:a + 4]):24s} -> {dis(after[a:a + 4])}")
    def show_files():
        for name, data in files.items():
            print(f"--- {name} ({len(data)} bytes)\n{data.decode('latin-1')}")
        files.clear()
    show_files()

    syms = {}
    map_path = os.path.splitext(args.payload)[0] + ".map"
    if os.path.exists(map_path):
        for line in open(map_path):
            parts = line.split()
            if len(parts) == 3:
                syms[parts[2]] = PAYLOAD_BASE + int(parts[0], 16)

    FRAME, W, H = 0x700000, 160, 80
    mu.mem_map(FRAME, 0x10000)
    for y in range(H):  # test picture: horizontal grey ramp, darker at the bottom
        grey = [(40 + x - y // 2) & 0xFF for x in range(W)]
        row = b"".join(struct.pack("<H", (g & 0xF8) << 8 | (g & 0xFC) << 3 | g >> 3) for g in grey)
        mu.mem_write(FRAME + y * W * 2, row)
    GP = 0x800000
    if args.gp420:
        gw, gh = (int(x) for x in args.gp420.split(":")[0].split("x"))
        mu.mem_map(GP, (3 * gw * (gh // 2) + 0xFFFF) & ~0xFFFF)
        for k in range(gh // 2):
            ramp = bytes((40 + 170 * x // gw) for x in range(gw))
            mu.mem_write(GP + 3 * gw * k, ramp + bytes([128]) * gw + ramp)
    for spec in args.poke:
        a, v = (int(x, 0) for x in spec.split(":"))
        mu.mem_write(a, bytes([v]))

    for spec in args.call:
        name, r0s = spec.split(":")
        addr = syms[name] if name in syms else int(name, 0)
        r0s, *more = r0s.split(",")             # optional r1, r2 after commas
        r0 = FRAME if r0s == "FRAME" else GP if r0s == "GP420" else int(r0s, 0)
        for reg, val in zip((UC_ARM_REG_R1, UC_ARM_REG_R2), more):
            mu.reg_write(reg, int(val, 0))
        if name == "photo_receive":         # r1 = &msg like the firmware's call
            mu.reg_write(UC_ARM_REG_R1, FRAME + 0x8000)
        allowed.clear()
        allowed.add(addr)
        print(f"call {name} (0x{addr:x}) r0={r0s}:")
        executed[0] = 0
        mu.reg_write(UC_ARM_REG_R0, r0)
        mu.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
        mu.reg_write(UC_ARM_REG_LR, STOP)
        mu.emu_start(addr, STOP, count=200_000_000)
        if stray:
            sys.exit(f"  ran unstubbed firmware code at 0x{stray[0]:x}")
        if mu.reg_read(UC_ARM_REG_PC) != STOP:
            sys.exit(f"  did not return (pc=0x{mu.reg_read(UC_ARM_REG_PC):x})")
        print(f"  returned r0=0x{mu.reg_read(UC_ARM_REG_R0):x}" + (f"  ({executed[0]:,} instructions)" if args.count else ""))
        if name == "photo_receive":
            print(f"  photo mode gets msg 0x{struct.unpack('<I', mu.mem_read(FRAME + 0x8000, 4))[0]:x}")

    show_files()

    def write_png(path, w, h, rows):
        import zlib
        def chunk(t, b):
            return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

    if args.gp420:
        raw = bytes(mu.mem_read(GP, 3 * gw * (gh // 2)))
        rows = bytearray()
        clip = lambda v: 0 if v < 0 else 255 if v > 255 else int(v)
        for k in range(0, gh // 2):                 # one output line per 2 photo lines
            t = 3 * gw * k
            rows.append(0)
            for x in range(0, gw, 2):
                Y, U, V = raw[t + x], raw[t + gw + x] - 128, raw[t + gw + x + 1] - 128
                rows += bytes((clip(Y + 1.402 * V), clip(Y - 0.344 * U - 0.714 * V), clip(Y + 1.772 * U)))
        write_png(args.gp420.split(":")[1], gw // 2, gh // 2, bytes(rows))
        print(f"wrote {args.gp420.split(':')[1]}")

    if args.png:
        import zlib
        raw = bytes(mu.mem_read(FRAME, W * H * 2))
        rows = b""
        for y in range(H):
            rows += b"\0"
            for x in range(W):
                p = struct.unpack_from("<H", raw, (y * W + x) * 2)[0]   # RGB565
                rows += bytes(((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
        def chunk(t, b):
            return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
        open(args.png, "wb").write(png)
        print(f"wrote {args.png}")


if __name__ == "__main__":
    main()
