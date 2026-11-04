/* Interface language.
 *
 * The language is byte 0 of the settings block at 0x11089c, stored as language + 1
 * (getter 0x1f60c, setter 0x1f618); the string pack has 11 languages, English first.
 * Photo mode has a hidden message, 0x7064, that steps to the next language and marks the
 * settings for saving (0x1de1c); that is how the camera can end up in another language.
 *
 * language = N (0 = English) sets the byte at boot and again just before the options
 * screen opens (power short press, 0x7063), and drops 0x7064 in photo mode. It does not
 * mark the settings for saving; the camera keeps it the next time it saves its settings.
 */
#include "fw.h"
#include "mods.h"

#define SETTINGS_LANGUAGE (*(volatile unsigned char *)0x11089C)
#define LANGUAGES         11
#define MSG_NEXT_LANGUAGE 0x7064
#define MSG_POWER         0x7063

static int language = -1;

static void language_set(void)
{
    SETTINGS_LANGUAGE = (unsigned char)(language + 1);
}

/* Called by mods/keys.c for each photo mode message; returns 1 to drop it. */
int language_key(u32 msg)
{
    if (language < 0) return 0;
    if (msg == MSG_NEXT_LANGUAGE) return 1;
    if (msg == MSG_POWER) language_set();
    return 0;
}

int language_active(void)
{
    return language >= 0;
}

void language_apply(void)
{
    int was = SETTINGS_LANGUAGE - 1;
    language = cfg_int("language", -1);
    if (language < 0 || language >= LANGUAGES) {
        language = -1;
        return;
    }
    language_set();
    log_printf("language: %d (was %d)\r\n", language, was);
}
