#ifndef MODS_H
#define MODS_H

void presets_apply(void);
void sounds_apply(void);
void boot_apply(void);
void film_apply(void);
void keys_apply(void);
void language_apply(void);
int language_key(u32 msg);
int language_active(void);
int restore_apply(void);

/* Set by a mod to see photo mode messages first; return 1 to swallow one (mods/keys.c). */
extern int (*keys_handler)(u32 msg);

/* Film border (mods/border.c). */
enum { BORDER_NEGATIVE, BORDER_SLIDE, BORDER_MONO, BORDER_STYLES };
#define PREVIEW_W  160
#define PREVIEW_H  80
#define PREVIEW_X0 24
#define PREVIEW_X1 136
void border_draw(unsigned char *buf, int w, int h, const char *label, int style, int number);
void border_preview(unsigned short *frame, int style);
const unsigned char *film_glyph(char c);   /* 5x7 font row bytes (mods/film.c) */

#endif
