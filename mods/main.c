/* HACK.BIN entry: reads HACK.INI, applies each enabled mod, writes HACKLOG.TXT. */
#include "fw.h"
#include "mods.h"

int main(u32 base)
{
    log_printf("Charmera mods, loaded at 0x%08x\r\n", base);
    /* Factory restore requested: apply nothing and write nothing. The updater ends in a plain
       power-off that never flushes the file system, which would leave a log file damaged. */
    if (restore_apply()) return 0;
    cfg_load();
    if (cfg_int("film_modes", 0)) film_apply();      /* replaces the presets */
    else if (cfg_int("presets", 1)) presets_apply();
    sounds_apply();
    boot_apply();
    language_apply();
    keys_apply();
    log_flush();
    return 0;
}
