# Charmera firmware notes (JH5408, version 202505201029)

Addresses are RAM addresses; file offset = RAM + 0x200.

## Flash layout
- 0x000000 header `SPII`, boot ROM loads 0x1B0000 bytes to RAM 0 (minus 0x200 header)
- 0x000200–0x0FFFFF code, 0x100000–0x11FFFF tables, 0x120000 `GP+` resource pack
- 0x180000 internal photos (no SD card), 0x1A0000–0x1AFFFF empty but loaded into RAM (loader space)

## Stock SD-card updater
- Check: `0x21624`, called from the SD mount handler at `0x3070` when the card becomes mounted
- File: `C:\JH_5408<12-digit version>?<8 hex>.bin` in the card root
  - version (name chars 7–18) must differ from the string at file offset `0x218d0`
  - name chars 20–27 = 32-bit sum of all file bytes, hex
- Upgrade: `0x2117c` — sums file, compares, writes flash from address 0 for the file size,
  skipping identical 4 KB sectors and verifying each (5 retries)
- Extra gate: value at `[0x1a2148+0x30]` must be ≤ 500 (`0x90344`), meaning unknown
- Images built for SD update must carry the new version at file offset `0x218d0`, or the
  update repeats on every boot while the file is on the card
- **Root cause found (emulated byte-for-byte):** after writing, the updater erases the 64 KB block
  at (end of file + 64 KB) — meant to reset the settings block 0x170000 after an official file
  ending ~0x169000. A full 2 MB file makes that 0x210000, which wraps to 0x010000 and bricks.
  Safe files end at 0x169000 (tools/make_sd_update.py; verify with tools/emulate_updater.py).
  Flash layout: firmware+resources 0x000000–0x168EFF, settings 0x170000–0x172FFF (+ header
  0x17F000), internal photos 0x180000+. Flash ops: 0x1b20 erase 64 KB (SPI 0xD8),
  0x1aec program 256 B, 0x119c read 256 B.
- Factory restore on a loader camera: HACK.BIN patches the loader's result at `0x216ac` to 1 after
  a find-first into `0x110c54` (mods/restore.c); restored firmware's check returns 0 for its own
  version, but 1 for any other JH_5408 file on the card. Restore then reinstall both verified on
  camera 2026-11-04. Write nothing to the card on the restore path: the updater's final power-off
  does not flush the file system, and a log written just before it left HACKLOG.TXT damaged.
  Fixed and verified on camera 2026-11-04: after a restore the file system was clean and HACKLOG.TXT,
  HACK.INI and all photos matched a backup byte for byte.
- **Earlier test 2026-11-03 with a full 2 MB file:** The update reached 100% and waited for the power button,
  but left the 64 KB block 0x10000–0x1FFFF erased (all FF) and the camera would not boot.
  All other blocks, including the changed sector 0x21000, were correct. Per 64 KB block the
  updater erases (`0x1b20` → `0x30618`), programs 4 KB sectors (`0x1aec` → `0x3080c`) and
  verifies (`0x119c` → `0x306c0`); why block 1 was not reprogrammed is not yet known.
  Recovered over the Trinket.

## Firmware functions
| RAM | Function |
|---|---|
| `0xe2e48` | open(path, mode) → fd |
| `0xe2ea8` | read(fd, buf, len) → n |
| `0xe2d80` | lseek(fd, off, whence) |
| `0xe2a54` | fstat(fd, stat) — size at +4 |
| `0xe28e0` | close(fd) |
| `0xe297c` | findfirst(pattern, entry, attr) — name at entry+0x12 |
| `0xe27cc` | chdir(path) |
| `0xe2dec` | mkdir(path) |
| `0x544` / `0x560` | malloc(size) / free(ptr) |
| `0x270` / `0x31c` | strcpy / strcat |
| `0x2b4` | strncmp |
| `0x90170` | delay / sleep(ticks) |

| `0xe3130` | write(fd, buf, len) |
| `0xde758` | sprintf(buf, fmt, ...) |

open() flags are newlib style: 0 read, 0x601 write+create+truncate, 0x602 read/write+create+truncate.
CPU is ARMv4 (no blx/clz, no CP15 use): call through registers with `mov lr, pc; mov pc, rX`.

## SD payload loader (charmera_loader/)
- Loader replaces the body of the stock update check at RAM `0x21624` (160 of 172 bytes,
  up to the version string at `0x216d0`); the SD mount handler at `0x3070` calls it
- Once per boot: open `C:\HACK.BIN`, malloc 64 KB, read, call it with r0 = load address
- Always returns 0, so the stock SD updater is disabled
- RAM above ~0x108000 holds variables (e.g. 0x10fc85, 0x1a2148), so the empty end of
  flash is not usable RAM for code

## Screens (states)
Dispatcher at `0x2567c` switches on message − 0x100:
`0x100` startup/logo (`0x63b0`), `0x101` video (`0x1c45c`), `0x102` photo (`0x1d514`),
`0x104` playback (`0xad80`), `0x105` via table `0x11028c`, `0x108` stock updater,
`0x10d` options screen (`0x25078`; items 1–6 → 0x102, 0x101, 0x105, 0x104, 0x10b, 0x10a).
Startup requests the next state at `0x6378` from literal `0x6514` (0x10d); setting it to
0x102 boots into photo mode (verified on camera, mods/boot.c).

## Exposure
- AE library init `0x96e7c` (AE workspace at ctx+0xe80, ctx = [0x1492dc+0xc]); target setter
  `0x93124(ae, lum)` clamps 8–240, default 48
- Command 0x1d (handler `0x693bc`) sets the base target (ctx+0xd7c); command 0x20 (wrapper
  `0x64768(step)`, handler `0x6946c`) sets target = base × EV table[step] / 1024
- EV table at RAM `0xf5924`: 1750 … 1024 (step 6) … 400, 13 steps ≈ +2 to −2 EV in 1/3 stops
- Stored EV step: settings byte `0x11089c+0xe5` (getter `0x1f4cc`, setter `0x1f4d8` marks the
  settings for saving); photo mode applies it on entry at `0x1d084`
- YUV block registers: `0xd080000c` byte1/2 = U/V offset, `0xd0800014` byte1/2 = U/V gain;
  byte 0 of each written only by unused `0x98e98` (likely Y offset / Y scale)

## Tone curve (gamma)
- 128-word table: low byte = 8-bit output at input 8·i (10-bit input), bits 8–21 = seven
  2-bit increments for the inputs in between; decoder/encoder round-trips the stock tables exactly
- Sensor tables: A at RAM `0xf7200` (normal, 512→190), B at `0xf7400` (lifted blacks, starts at 13);
  pointers in the sensor config struct (`0xf5aec` +0x1c/+0x20 → ctx+0xed8/+0xedc)
- Loader `0x98758(table)` writes 0xd0800800; called only from `0x66b6c` (at `0x66b8c`), from the
  handler of image processor command 0x08; AE also gets the table via `0x96284`
- mods/film.c hooks `0x66b8c` to load the stock table through a per-film S-curve + fade

## Keys (photo mode)
- Photo mode receives messages via `0x25a44(queue, &msg, data, size)` at `0x1d614`
- Measured: 0x7001 shutter, 0x7002 down, 0x7003 up, 0x7061 play (gallery), 0x7063 power short
  (options screen), 0x7006 power off (handler calls power-off routine `0x22e14`); no long-press
  messages. 0x7017 = 5 s timer; 0x7042/0x703e UI updates; 0x500d/0x500e/0x7032 capture;
  0x5013/0x5014 storage. Ticks (`0x90344`) ≈ 100/s

## White balance
- Setting byte `0x11089c+0xe9`: 0 auto, 1 daylight, 2 cloudy, 3 tungsten, 4 fluorescent
  (getter `0x1fb9c`, read by photo mode at `0x1d08c`); `0x6493c(wb)` maps 1–4 to AWB modes
  7, 10, 5, 6 (auto → 0, 1 or 12) and sends command 0x1f → `0x96090(awb, mode)`
- AWB modes 5/6/7/8–9/10 fix the colour-temperature index at 12/20/32/40/50; 0–4 and 12 search

## Sharpness (edge enhancement)
- Edge register `0xd0800290`: bytes 0/1/3 thresholds (0x1e, 0x50, 0x78), byte 2 gain; `0x9a050(gain)`
  sets the gain and latches it; command 0x27 (`0x67958`) programs the whole edge block + LUT `0xf5764`
- Strengths ctx+0xd8c (normal) / ctx+0xd90 (low light), set at sensor start (`0x64a00`, config
  `0xf5acc` bytes 0xc = 0 and 0x11 = 24) via `0x648e4` → commands 0x0b/0x0c; AE thread moves the
  gain between them (`0x66698` derives ctx+0xe40..0xe48)
- Unused firmware sharpness setter `0x6a0ec(level)`: 0 = stock+50, 1 = stock, 2 = low-light value
- mods/film.c hooks `0x64a58` and adds a per-film offset; sensor start-up at boot runs before the
  payload loads, so it falls back to the config bytes (verified on camera: register byte 2 tracks it)

## Flash (LED)
- No GPIO is configured for the LED; it is fired by `0x30e00(1, 2)` (off: `0x30e00(0, 2)`),
  which writes `0xd0000050` and bit 4 of register 3 of the controller at `0xc0050000`
- Low-light flag: byte `0x1a1948`, set by the sensor exposure stepper (`0x7af70`) when the
  exposure index > 155, cleared < 150; read at the shutter at `0x1df58` and `0x1defc`
- GPIO start-up table at `0xf3940` (18 × {pin, mode, level, drive}); outputs 0x13, 0x14, 0x26
  (backlight), 0x3a–0x3c; inputs 2, 3, 4, 39 (buttons); 0x5f = unused slot

## Language
- Settings byte `0x11089c+0` = language + 1 (getter `0x1f60c`, setter `0x1f618` marks for saving);
  string pack GPRSPAK (`0x141400`) has 11 languages, English first; loader `0x23760(lang, id)`
- Photo mode message 0x7064 → `0x1de1c` steps to the next language (hidden combination);
  mods/language.c forces the language and drops 0x7064
- The string pack also lists hidden factory features: Time Stamp, White Balance, Sharpness,
  Burst shot, Capture Timer, Motion Detection, Video Output

## Photo buffer at capture
- `0x6e0a0` (capture finish): at `0x6e0e4` calls `0xed08(buf, w, h)` (stock frame overlay, 800x600
  bands of 200 lines) with [0x14ac2c+0x14], +4, +8; JPEG encoding follows (`0x6beec`)
- Format GP420, 3*w bytes per 2 lines: [even luma][chroma, U V][odd luma], confirmed on camera (from text
  routine `0x23e40`, writer `0x14074`). mods/border.c draws here; all 12 films on by default,
  all 12 photographed and correct on camera 2026-11-04

## Photo file names
- Shutter (`0x1d0a4`) posts 0x500b; handler `0x35dc` picks the next free number from `[0x10b4dc+0x5c]`,
  sprintf's `C:\DCIM\PICT%04d.jpg` into `0x10b568`, opens it and replies 0x500d. Card state byte
  `0x110dc4+4` (getter `0x21ea0`), 0xff = no card. mods/film.c reads the number from that path
  (verified on camera 2026-11-04: the path is already set at the capture hook `0x6e0e4`)

## Sounds
`play_sound(n)` at `0x1e850`; ids in table `0x110344` (+4 CLICK, +6 POWERON, +8 POWEROFF,
+0xC BEEP, +0xE IMG), filled once at boot by `0x1e240`; id 0xFFFF = silent.

## Image settings (verified on camera unless noted)
- JPEG quality immediates: file `0xEA18` (85), `0xEA2C` (75), `0xEA34` (80), `0xEA48` (45, no SD) — patching works
- Tone tables at file `0xF80CC` / `0xF81CC`: changing them stops the camera booting
- Filter modes: mode byte at RAM `0x10fc85` (0–11), cycled at `0x1dc74`/`0x1dd0c`; color setter
  `0x2d374(mode)` (stock handles 0–3, callers clamp others to 0 at `0x1dca0`/`0x1dd34`) sends
  U off, V off, U gain, V gain to CDSP cmds 0x30000013 (`0x645e4`) and 0x30000014 (`0x64644`).
  Frame overlays gated at `0xed1c` (capture, F_x_xxx.GPZP) and `0x16060` (live preview,
  PV0xx.GPZP via `0x13e6c`); two-tone at `0x6dc1c`/`0x6e0b0` (capture) and `0x160f0` (preview).
- Live preview (photo state): routine `0x15f94`, frame in r5 at `0x1605c`; 160x80 RGB565,
  one halfword per pixel, stride 320 (measured on camera from wrong colours; `0x15e2c` converts it to
  YUV in [r7+0x4c] for the stock two-tone `0x15ed0`, `0x16610` converts back); columns 0–23/136–159 blanked
  afterwards. Mode icon drawn by `0x21bac(0x5f+mode)` at `0x1dcc4`.
  Presets for modes 1–3 verified on camera; mods/film.c covers all 12 modes.
