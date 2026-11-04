#!/usr/bin/env python3
"""Read or write the Charmera flash through the Trinket M0 running trinket_spi_flash.ino.

Usage:
  python3 charmera_flash.py id                         # print JEDEC ID and status register
  python3 charmera_flash.py dump factory_dump.bin         # read the whole flash to a file
  python3 charmera_flash.py write patched.bin          # flash only the 4 KB sectors that differ

'write' first reads the chip and saves it as backup_<time>.bin, shows which sectors
differ from the target file, asks for confirmation, writes and verifies each sector,
then reads the whole chip again and checks it matches the target exactly.

The port is auto-detected (/dev/cu.usbmodem*); pass --port to override.
Uses only the Python standard library.
"""
import argparse
import glob
import hashlib
import os
import sys
import termios
import time

SECTOR = 4096
PUYA = 0x85


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                  # iflag: raw
    attrs[1] = 0                                  # oflag: raw
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                  # lflag: no echo, non-canonical
    attrs[4] = attrs[5] = termios.B115200         # ignored by USB CDC, set anyway
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 20                  # 2 s read timeout
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    time.sleep(0.5)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_exact(fd, n, progress=False):
    data = bytearray()
    while len(data) < n:
        chunk = os.read(fd, min(65536, n - len(data)))
        if not chunk:
            raise TimeoutError(f"timed out after {len(data)} of {n} bytes")
        data += chunk
        if progress:
            sys.stderr.write(f"\r{len(data) * 100 // n:3d}%  {len(data):,} / {n:,} bytes")
    if progress:
        sys.stderr.write("\n")
    return bytes(data)


def read_line(fd):
    line = b""
    while not line.endswith(b"\n"):
        chunk = os.read(fd, 1)
        if not chunk:
            sys.exit("No reply; check trinket_spi_flash.ino is uploaded.")
        line += chunk
    return line.decode().strip()


def query(fd, cmd):
    os.write(fd, cmd)
    return [int(x, 16) for x in read_line(fd).split()[1:]]


def dump(fd, progress=True):
    os.write(fd, b"d")
    size = int.from_bytes(read_exact(fd, 4), "little")
    data = read_exact(fd, size, progress=progress)
    if data.count(0xFF) == size or data.count(0x00) == size:
        sys.exit("Read returned all FF or all 00; check wiring and that the camera battery is unplugged.")
    return data


def write_sector(fd, addr, data):
    os.write(fd, b"w" + addr.to_bytes(4, "little"))
    for i in range(0, SECTOR, 512):               # small chunks so the Trinket's USB buffer keeps up
        os.write(fd, data[i:i + 512])
    status = read_exact(fd, 1)
    if status == b"E":
        raise RuntimeError(read_line(fd))
    if status != b"K":
        raise RuntimeError(f"unexpected reply {status!r}")
    data = read_exact(fd, SECTOR + 1)
    if data[-1:] != b".":
        raise RuntimeError("readback missing end marker")
    return data[:SECTOR]


def check_chip(fd):
    mfr, typ, cap = query(fd, b"i")
    (sr,) = query(fd, b"s")
    print(f"JEDEC ID {mfr:02X} {typ:02X} {cap:02X}, status register {sr:02X}")
    if mfr != PUYA:
        sys.exit("Not the Puya chip (expected manufacturer 85); check wiring.")
    return 1 << cap, sr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["id", "dump", "write"])
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port")
    ap.add_argument("--yes", action="store_true", help="skip the confirmation prompt")
    args = ap.parse_args()

    port = args.port
    if not port:
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if not ports:
            sys.exit("No /dev/cu.usbmodem* port found; is the Trinket plugged in?")
        port = ports[0]
    fd = open_port(port)

    if args.command == "id":
        check_chip(fd)
        return

    if not args.file:
        sys.exit(f"{args.command} needs a filename")

    if args.command == "dump":
        data = dump(fd)
        with open(args.file, "wb") as f:
            f.write(data)
        print(f"Wrote {args.file}  sha1 {hashlib.sha1(data).hexdigest()}")
        return

    target = open(args.file, "rb").read()
    size, sr = check_chip(fd)
    if len(target) != size:
        sys.exit(f"{args.file} is {len(target):,} bytes but the chip is {size:,} bytes.")
    if sr & 0x7C:
        sys.exit(f"Status register {sr:02X} has block-protect bits set; writes would be refused.")

    print("Reading current flash for backup and comparison...")
    current = dump(fd)
    backup = time.strftime("backup_%Y%m%d_%H%M%S.bin")
    with open(backup, "wb") as f:
        f.write(current)
    print(f"Saved {backup}  sha1 {hashlib.sha1(current).hexdigest()}")

    changed = [a for a in range(0, size, SECTOR) if current[a:a + SECTOR] != target[a:a + SECTOR]]
    if not changed:
        print("Flash already matches the target; nothing to write.")
        return
    print(f"{len(changed)} sector(s) differ: " + ", ".join(f"0x{a:06X}" for a in changed))
    if not args.yes and input("Erase and rewrite these sectors? Type yes: ").strip() != "yes":
        sys.exit("Cancelled; nothing written.")

    for a in changed:
        want = target[a:a + SECTOR]
        for attempt in (1, 2):
            got = write_sector(fd, a, want)
            if got == want:
                print(f"  0x{a:06X} written and verified")
                break
            print(f"  0x{a:06X} readback mismatch (attempt {attempt})")
        else:
            sys.exit(f"Sector 0x{a:06X} would not verify. Restore with: write {backup}")

    print("Reading whole chip to confirm...")
    final = dump(fd)
    if final == target:
        print(f"Done. Flash matches {args.file}  sha1 {hashlib.sha1(final).hexdigest()}")
    else:
        bad = [a for a in range(0, size, SECTOR) if final[a:a + SECTOR] != target[a:a + SECTOR]]
        sys.exit("Final check FAILED in sectors " + ", ".join(f"0x{a:06X}" for a in bad)
                 + f". Restore with: write {backup}")


if __name__ == "__main__":
    main()
