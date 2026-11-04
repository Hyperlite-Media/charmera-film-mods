/* Boot straight into photo mode instead of the options screen.
 *
 * After the logo, the startup state (0x6378) asks the screen dispatcher for state 0x10d,
 * the options screen; its first item opens state 0x102, photo mode. The state number is
 * loaded from a literal at 0x6514 that nothing else uses. The SD mount handler runs this
 * payload before it reports "storage ready", and startup only picks the next screen after
 * that report, so the change is in place in time. Without an SD card the payload does not
 * run and the options screen appears as usual.
 */
#include "fw.h"
#include "mods.h"

#define NEXT_STATE_LITERAL 0x6514
#define STATE_OPTIONS      0x10D
#define STATE_PHOTO        0x102

void boot_apply(void)
{
    if (!cfg_int("boot_to_photo", 0)) return;
    if (patch32(NEXT_STATE_LITERAL, STATE_OPTIONS, STATE_PHOTO))
        log_printf("boot: photo mode after the logo\r\n");
}
