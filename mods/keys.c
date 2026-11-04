/* Photo mode key events.
 *
 * Photo mode (state 0x102) waits for messages with receive(queue, &msg_id, data, size)
 * (0x25a44) at 0x1d614. That call goes to photo_receive(), which lets keys_handler (set by
 * another mod, e.g. the film menu) swallow messages before photo mode sees them; a
 * swallowed message is dropped and the next one is received instead.
 *
 * mods/language.c also sees each message first (it drops the hidden language step 0x7064).
 *
 * Messages measured on the camera (key_log): 0x7001 shutter, 0x7002 down, 0x7003 up,
 * 0x7061 play (opens the gallery), 0x7063 power short press (options screen), 0x7006
 * power off; there are no long-press messages for the other buttons. 0x7017 is a 5 s
 * timer, 0x7042 / 0x703e follow UI changes, 0x500d / 0x500e / 0x7032 a capture,
 * 0x5013 / 0x5014 storage. Ticks from 0x90344 run at about 100 per second.
 *
 * key_log = on also writes each message id and tick to C:\HACKKEYS.TXT.
 */
#include "fw.h"
#include "mods.h"

#define fw_receive ((int (*)(void *queue, u32 *msg, void *data, int size))0x25A44)
#define fw_ticks   ((u32 (*)(void))0x90344)
#define RECEIVE_CALL 0x1D614

int (*keys_handler)(u32 msg);

static int key_log;
static char klog[2600];
static int klen, kcount;

static void log_key(u32 msg)
{
    int fd;
    if (kcount >= 100 || klen > (int)sizeof(klog) - 64) return;
    klen += fw_sprintf(klog + klen, "%3d  msg 0x%04x  tick %d\r\n", kcount++, msg, fw_ticks());
    fd = fw_open("C:\\HACKKEYS.TXT", O_CREATE_TRUNC_RW);
    if (fd < 0) return;
    fw_write(fd, klog, klen);
    fw_close(fd);
}

static int photo_receive(void *queue, u32 *msg, void *data, int size)
{
    for (;;) {
        int r = fw_receive(queue, msg, data, size);
        if (r == -1) return r;
        if (key_log) log_key(*msg);
        if (language_key(*msg)) continue;
        if (!keys_handler || !keys_handler(*msg)) return r;
    }
}

/* "bl target" placed at addr. */
static u32 arm_bl(u32 addr, u32 target)
{
    return 0xEB000000u | (((target - (addr + 8)) >> 2) & 0x00FFFFFFu);
}

/* Continues the previous HACKKEYS.TXT so several boots can be logged in one session. */
static void key_log_start(void)
{
    int fd = fw_open("C:\\HACKKEYS.TXT", O_RDONLY);
    if (fd >= 0) {
        klen = fw_read(fd, klog, sizeof(klog) - 200);
        fw_close(fd);
        if (klen < 0) klen = 0;
    }
    klen += fw_sprintf(klog + klen, "--- boot\r\n");
}

void keys_apply(void)
{
    key_log = cfg_int("key_log", 0);
    if (key_log) key_log_start();
    if (!key_log && !keys_handler && !language_active()) return;
    if (patch32(RECEIVE_CALL, 0xEB00210A, arm_bl(RECEIVE_CALL, (u32)photo_receive))) {
        if (key_log) log_printf("keys: logging photo mode messages to HACKKEYS.TXT\r\n");
    } else {
        keys_handler = 0;
    }
}
