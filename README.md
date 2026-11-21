# Charmera film mods

Custom firmware mods for the Kodak Charmera keychain camera, loaded from the SD card much like
Magic Lantern on Canon cameras. A small loader in the camera runs `HACK.BIN` from the card at
every boot, so removing that file turns the mods off.

**Not affiliated with or endorsed by Kodak or the camera's manufacturer.** Changing firmware can
leave a camera unbootable and may void its warranty. Use this at your own risk.

So far this has been tested on one camera: firmware JH5408, version `202505201029`. Other versions
are not supported.

## What it adds
- **12 film looks** replacing the filter modes, each with its own colour, contrast, sharpness and
  film border: Gold 200, Portra 160/400/800, Ektar 100, UltraMax 400, ColorPlus 200,
  Ektachrome E100, Kodachrome 64, Vision3 500T, Tri-X 400, BW400CN
- **In-camera settings menu** (PLAY in photo mode): exposure, contrast, fade, saturation, warmth,
  white balance, sharpness, border and flash, saved per film to the card
- **Film borders** on saved photos with sprocket holes, the film's name along the edge and the
  photo's number as the frame number
- **Flash control:** auto, on or off
- **Higher JPEG quality:** 92 instead of 85
- **Boot straight to photo mode**, button click sound off, and interface language locked

You need an image from your own camera. Without an install
file, this means opening the camera and reading its flash chip once:

1. Read the 2 MB flash with an SPI programmer. `flasher/` has a sketch and script for an Adafruit
   Trinket M0, which is what this was developed with.
2. Build the install and restore files. Keep the dump and these files private: they contain Kodak's
   firmware, and the dump also holds your camera's settings and internal photos.
   ```
   python3 build.py factory_dump.bin
   python3 tools/make_sd_update.py charmera_loader.bin --version 202511041200
   python3 tools/make_sd_update.py factory_dump.bin --restore
   ```
   `build.py` refuses anything but the exact firmware these patches were written for. The firmware
   check covers 0x000000–0x168FFF. If your unmodified dump is refused, please open an issue rather
   than working around the check.
3. Check the install file on a simulated flash chip (needs `pip install unicorn capstone`):
   ```
   python3 tools/emulate_updater.py JH_5408202511041200_*.bin --flash factory_dump.bin
   ```

## Install, use and remove
- [docs/INSTALL.txt](docs/INSTALL.txt): install from the SD card
- [docs/RESTORE.txt](docs/RESTORE.txt): go back to factory firmware from the SD card

In photo mode, PLAY opens the settings menu: up and down change a value, PLAY moves to the next
setting and SHUTTER closes and saves. The gallery is the menu's last item.

To update the mods, copy only `HACK.BIN` to the card. Your film tuning is saved in the card's
`HACK.INI`, so replacing that file erases it. Edit `HACK.INI` on a computer to switch mods on or
off, with no rebuild needed. `HACKLOG.TXT` on the card says what was applied.

If something misbehaves, delete `HACK.BIN` from the card or boot without a card.

## Why the install file has to be this size
The camera's updater erases the 64 KB block at (end of file + 64 KB) after writing. A full 2 MB
file makes that wrap around to firmware code and **bricks the camera**. `make_sd_update.py` writes
only 0x000000–0x168FFF, so the erase lands on the settings block, as with an official update. Do
not put any other image on the card under a `JH_5408` name.

## Add or change a mod
Building needs `arm-none-eabi-gcc` on your PATH, or the Adafruit SAMD board package for Arduino.

1. Mods are modules in `mods/`, called from `mods/main.c`; firmware calls and helpers (`cfg_int`,
   `patch32`, `log_printf`) are in `sdk/fw.h`.
2. `python3 build_payload.py` builds all of them into `HACK.BIN`.
3. Recommended: run it against the real firmware in the emulator first (needs
   `pip install unicorn capstone`, and uses `charmera_loader.bin` from `build.py`):
   `python3 tools/emulate.py HACK.BIN --ini HACK.INI`
   Add `--call 0x2d374:5` to then call a (hooked) firmware function with r0 = 5, or
   `--poke 0x10fc85:7 --call film_overlay:FRAME --png out.png` to render the preview overlay.

Firmware addresses and what is known about the camera are in [docs/NOTES.md](docs/NOTES.md).

### Rules for payloads
- ARMv4 only (`-mcpu=arm7tdmi`); the build script sets this.
- No static tables of pointers (they are not relocated); integer tables are fine.
- Up to 64 KB including `.bss`; the buffer stays allocated, so code can stay resident.
- Check an instruction is what you expect before patching it (see `is_mov_imm` in `fw.h`).

## Files
- `loader.S`, `build.py`: the loader and the firmware image built from your dump
- `HACK.BIN`, `HACK.INI`: the built mods and their default settings, for the SD card root
- `payload_hello.S`: the first test payload, kept as a minimal assembly example
- `tools/make_sd_update.py`, `tools/emulate_updater.py`: SD card install file and its emulator check
- `tools/emulate.py`: run a payload against the firmware in an ARM emulator
- `flasher/`: read and write the flash through a Trinket M0
- `mods/film.c`: the film looks, on-screen names, settings menu and flash control
- `mods/border.c`: film border on saved photos and the live preview
- `mods/presets.c`: Warm / Vivid / Faded filter modes, used only when `film_modes` is off
- `mods/sounds.c`: click, shutter and power-off sound switches
- `mods/boot.c`: open photo mode after the logo instead of the options screen
- `mods/keys.c`: photo mode message hook; key ids; `key_log` diagnostics
- `mods/restore.c`: start the stock updater for a factory restore file
- `mods/language.c`: force the interface language

## License
The code in this repository is under the MIT License (see [LICENSE](LICENSE)). It does not cover
Kodak's firmware, names or trademarks. Film stock names identify the look each mode imitates.
