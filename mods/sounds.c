/* Sound switches.
 *
 * play_sound(n) (RAM 0x1e850) looks up a resource id in the table at 0x110344 and plays
 * nothing when the id is 0xFFFF. The table is filled once at boot (0x1e240), before the
 * SD card mounts. Muting sets the id to 0xFFFF and also disables the boot-time store,
 * so a later refill cannot bring the sound back.
 *
 * The power-on sound plays before the SD card is read, so it cannot be switched here.
 */
#include "fw.h"
#include "mods.h"

#define SOUND_TABLE 0x110344

/* id_offset: offset of the 16-bit resource id in the table;
   init_store: address of the "strh r0, [r4, #id_offset]" in the boot-time init. */
static void mute(const char *name, u32 id_offset, u32 init_store)
{
    volatile unsigned short *id = (volatile unsigned short *)(SOUND_TABLE + id_offset);
    u32 strh = 0xE1C400B0u | ((id_offset & 0xF0) << 4) | (id_offset & 0x0F);  /* strh r0, [r4, #off] */
    if (!patch32(init_store, strh, ARM_NOP)) return;
    log_printf("  %s muted (id %d)\r\n", name, *id);
    *id = 0xFFFF;
}

void sounds_apply(void)
{
    /* Settings default to on (stock behaviour) when HACK.INI does not mention them. */
    if (!cfg_int("click_sound", 1))    mute("click (buttons, scrolling)", 0x4, 0x1E250);
    if (!cfg_int("shutter_sound", 1))  mute("shutter", 0xE, 0x1E280);
    if (!cfg_int("poweroff_sound", 1)) mute("power-off", 0x8, 0x1E268);
}
