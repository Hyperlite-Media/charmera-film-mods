/* Restore factory firmware from the SD card.
 *
 * The loader replaces the stock update check (0x21624) and always answers "no update", so
 * the stock updater cannot run once the loader is installed. If a factory restore file is on
 * the card, this answers "update" instead: the loader's final "mov r0, #0" (0x216ac) becomes
 * "mov r0, #1" in RAM, and the SD mount handler then starts the updater (0x2117c) as usual.
 *
 * The updater uses the find-first result at 0x110c54 (name at +0x12), so this does the same
 * find-first the stock check did. Only the factory version in the name triggers it, and the
 * size must be exact: the updater's final erase lands at (end of file + 64 KB), which for a
 * wrong size could hit the firmware. Restored firmware has this same version, so its own
 * check finds nothing newer and does not update again. Messages logged here only reach the
 * card if the mods go on to load; the restore path itself must not write to the card.
 */
#include "fw.h"
#include "mods.h"

#define fw_findfirst ((int (*)(const char *pattern, void *entry, int attr))0xE297C)
#define fw_fstat     ((int (*)(int fd, void *st))0xE2A54)

#define DIRENT        0x110C54
#define RESTORE_SIZE  0x169000u  /* firmware + resources; final erase hits settings at 0x170000 */
#define LOADER_RESULT 0x216AC

/* Returns 1 if the restore was started; the caller must then apply no other mods. */
int restore_apply(void)
{
    const char *name = (const char *)(DIRENT + 0x12);
    char path[64];
    u32 st[16];
    int r, fd;

    r = fw_findfirst("C:\\JH_5408202505201029*.bin", (void *)DIRENT, 0x77);
    if (r < 0) return 0;
    fw_close(r);

    fw_sprintf(path, "C:\\%s", name);
    fd = fw_open(path, O_RDONLY);
    if (fd < 0) {
        log_printf("restore: cannot open %s\r\n", name);
        return 0;
    }
    r = fw_fstat(fd, st);
    fw_close(fd);
    if (r != 0 || st[1] != RESTORE_SIZE) {
        log_printf("restore: %s has the wrong size, ignored\r\n", name);
        return 0;
    }
    if (!patch32(LOADER_RESULT, 0xE3A00000, 0xE3A00001)) return 0;
    log_printf("restore: starting the camera's updater with %s\r\n", name);
    return 1;
}
