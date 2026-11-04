/* Film looks: all 12 filter modes become Kodak film stock colour looks.
 *
 * Stock behaviour: modes 0-3 are colour settings (normal, sepia, blue, B&W) applied by the
 * setter at 0x2d374; modes 4-7 add frame overlays and modes 8-11 a two-tone effect, both
 * with normal colour. This mod
 *   - replaces the setter with film_set() below, which stays loaded in RAM,
 *   - passes every mode number to it (the mode handler clamped modes above 3 to 0),
 *   - switches off the frame overlays and the two-tone effect.
 * Photo mode calls the setter with mode 0 when it opens, so mode 0's look applies at boot.
 *
 * Exposure (settings exposure and the optional 5th number of filmN): the auto-exposure
 * target is scaled by an EV table at 0xf5924 (13 steps, +2 EV ... -2 EV in 1/3 stops,
 * step 6 = 0 EV) through image processor command 0x30000020 (wrapper 0x64768). Photo mode
 * reads the stored EV step (settings byte 0x11089c+0xe5, normally 6) at 0x1d084 when it
 * opens; that read now returns film_ev_step(), and film_set() sends the step on each
 * mode change. The stored setting itself is not changed.
 *
 * Contrast (setting contrast and the optional 6th/7th numbers of filmN): the image
 * processor's tone curve (gamma) is a 128-word table: each word holds the 8-bit output for
 * every 8th of 1024 inputs (low byte) and seven 2-bit increments for the inputs between
 * (bits 8-21). The firmware loads the sensor's table (0xf7200, or 0xf7400) through
 * 0x98758, called at 0x66b8c. That call now goes to film_gamma_load(), which loads the
 * stock table passed through the film's tone curve: an S-curve around mid grey (contrast
 * -100..100) and a black lift (fade). film_set() reloads it on each mode change.
 *
 * White balance (optional 8th number of filmN, and the menu): the stored WB setting is
 * settings byte 0x11089c+0xe9 (0 auto, 1 daylight, 2 cloudy, 3 tungsten, 4 fluorescent),
 * read by photo mode at 0x1d08c and applied with 0x6493c(wb), which sends the white
 * balance library mode through image processor command 0x1f. That read now returns
 * the film's value and film_set() applies it on every mode change.
 *
 * Sharpness (optional 9th number of filmN, and the menu): edge enhancement strength is a
 * pair of context values, ctx+0xd8c and ctx+0xd90 (ctx = [0x1492dc+0xc]), between which
 * the AE thread moves the hardware edge gain (0x9a050) with the light. Sensor start-up
 * sends them from the sensor config (0 and 24) at 0x64a58 through commands 0x0b/0x0c
 * (0x648e4); that call now goes through film_edge_init(), which adds the film's offset.
 * film_set() rewrites them and refreshes like the firmware's unused sharpness setter
 * (0x6a0ec): 0x66698 recomputes the AE thresholds, 0x9a050 sets the hardware.
 *
 * Flash (setting flash = auto | on | off, and the menu): the exposure code sets the byte at
 * 0x1a1948 when the exposure index rises above 155 (clears it below 150); at the shutter
 * photo mode reads it (0x1df58, 0x1defc) and fires the LED with 0x30e00(1, 2). Those reads
 * now call flash_decide(), which returns the flag (auto), 1 (on) or 0 (off).
 *
 * Border (10th number of filmN, film_borders, and the menu): the capture path calls the stock
 * frame overlay (0xed08) with the full-size photo at 0x6e0e4, before JPEG encoding. That call
 * now goes to film_capture(), which draws mods/border.c into the photo.
 *
 * Menu (setting film_menu, needs film_names): in photo mode the play button opens a menu
 * drawn on the preview for the current film (exposure, contrast, fade, saturation,
 * warmth, white balance, sharpness, border) plus the flash. Up/down change the value (applied live),
 * play moves to the next row, shutter closes it; on the last row play opens the gallery.
 * Changes are saved to that film's line in HACK.INI. Buttons come from mods/keys.c.
 *
 * Film names (setting film_names): the live preview routine composited the frame overlay at
 * 0x1605c, just before the preview is shown. That spot now calls film_overlay(), which draws
 * the film name along the bottom of the 160x80 preview for a while after each mode change.
 * The preview is RGB565, one 16-bit word per pixel; columns 0-23 and 136-159 are blanked
 * afterwards, so text stays in between.
 * The stock filter icon (drawn at 0x1dcc4) is switched off. Saved photos are not affected.
 *
 * Each look sets the image processor's colour controls, sent through two commands
 * (0x30000013 at 0x645e4 and 0x30000014 at 0x64644, as the stock setter does):
 *   U offset, V offset  (U+ = bluer, U- = yellower; V+ = redder, V- = greener)
 *   U gain, V gain      (32 = normal saturation, 0 = black and white)
 * Contrast, tone curve and grain are not part of these controls.
 */
#include "fw.h"
#include "mods.h"

#define MODES 12

#define cdsp_color_a ((int (*)(int, int, int, int))0x645E4)  /* command 0x30000013 */
#define cdsp_color_b ((int (*)(int, int, int, int))0x64644)  /* command 0x30000014 */

#define SETTER 0x2D374

/* Defaults; each can be overridden in HACK.INI as
   "film<N> = u_off v_off u_gain v_gain [exposure [contrast [fade [wb [sharpness [border]]]]]]"
   (exposure in 1/3 stops; contrast -100..100; fade 0..64 lifts the blacks;
   wb 0 auto, 1 daylight, 2 cloudy, 3 tungsten, 4 fluorescent; sharpness -30..150;
   border 0 off, 1 on as a 10th number). */
static int looks[MODES][4] = {
    {  -8,  5, 38, 38 },  /*  0 Gold 200: warm, golden, punchy */
    {  -3,  2, 28, 28 },  /*  1 Portra 160: neutral-warm, gentle colour */
    {  -5,  4, 30, 31 },  /*  2 Portra 400: warm skin tones, soft saturation */
    {  -7,  6, 33, 34 },  /*  3 Portra 800: warmer and a little richer */
    {   2,  3, 46, 48 },  /*  4 Ektar 100: vivid, deep blues and reds */
    {  -6,  4, 40, 40 },  /*  5 UltraMax 400: bright, warm consumer colour */
    { -10,  6, 30, 30 },  /*  6 ColorPlus 200: warm, slightly faded */
    {   6, -2, 42, 40 },  /*  7 Ektachrome E100: clean, cool, saturated slide */
    {  -4,  7, 40, 46 },  /*  8 Kodachrome 64: rich reds, warm */
    {  14, -8, 30, 30 },  /*  9 Vision3 500T: tungsten film in daylight, teal-blue */
    {   0,  0,  0,  0 },  /* 10 Tri-X 400: black and white */
    {  -4,  3,  0,  0 },  /* 11 BW400CN: warm-toned black and white */
};

/* Per-film exposure in 1/3 stops, added to the global "exposure" setting. */
static int film_ev[MODES] = { 0, 1, 1, 1, 0, 0, 0, -1, -1, 0, 0, 0 };
static int exposure;

/* Per-film tone: contrast (S-curve strength, -100..100) and fade (black level, 0..64). */
static int tone[MODES][2] = {
    { 15,  0 },  /*  0 Gold 200 */
    {-15,  8 },  /*  1 Portra 160 */
    {-10,  6 },  /*  2 Portra 400 */
    { -5,  6 },  /*  3 Portra 800 */
    { 30,  0 },  /*  4 Ektar 100 */
    { 20,  0 },  /*  5 UltraMax 400 */
    { -5, 12 },  /*  6 ColorPlus 200 */
    { 35,  0 },  /*  7 Ektachrome E100 */
    { 40,  0 },  /*  8 Kodachrome 64 */
    {-20, 10 },  /*  9 Vision3 500T */
    { 45,  0 },  /* 10 Tri-X 400 */
    {-10,  6 },  /* 11 BW400CN */
};
static int contrast;

/* Per-film white balance: 0 auto, 1 daylight, 2 cloudy, 3 tungsten, 4 fluorescent. */
#define WB_MODES 5
static int film_wb[MODES];
static const char wb_names[WB_MODES][12] = { "AUTO", "DAYLIGHT", "CLOUDY", "TUNGSTEN", "FLUORESCENT" };
#define fw_set_wb ((void (*)(int wb))0x6493C)

/* Per-film sharpness: offset added to the stock edge strengths (0 = camera default). */
static int film_sharp[MODES] = { 0, -8, -8, -8, 8, 0, -8, 4, 8, -12, 12, -4 };
static int edge_orig[2] = { -1, -1 };   /* stock values from sensor start-up */
#define fw_edge_send   ((int (*)(int normal, int low))0x648E4)  /* commands 0x0b, 0x0c */
#define fw_edge_hw     ((void (*)(int gain))0x9A050)
#define fw_edge_update ((void (*)(void))0x66698)
#define EDGE_CTX       (*(volatile u32 *)(0x1492DC + 0xC))

#define fw_gamma_load ((void (*)(const u32 *table))0x98758)
static const u32 *gamma_stock;   /* last table the firmware loaded */
static u32 gamma_buf[128];

#define fw_set_ev  ((int (*)(int step))0x64768)  /* command 0x30000020 */
#define EV_STEPS   13
#define EV_ZERO    6

/* Per-film border: on/off and style. */
static int film_border[MODES] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const unsigned char border_style[MODES] = {
    BORDER_NEGATIVE, BORDER_NEGATIVE, BORDER_NEGATIVE, BORDER_NEGATIVE, BORDER_NEGATIVE,
    BORDER_NEGATIVE, BORDER_NEGATIVE, BORDER_SLIDE, BORDER_SLIDE, BORDER_NEGATIVE, BORDER_MONO, BORDER_MONO,
};
static int borders_on = 1;      /* film_borders: off hides all of them */
static int frame_number;

static const char names[MODES][16] = {
    "Gold 200", "Portra 160", "Portra 400", "Portra 800", "Ektar 100", "UltraMax 400",
    "ColorPlus 200", "Ektachrome E100", "Kodachrome 64", "Vision3 500T", "Tri-X 400", "BW400CN",
};

/* EV table step for a mode: step 0 is +2 EV, step 12 is -2 EV. */
static int ev_step(int mode)
{
    int step = EV_ZERO - (exposure + film_ev[mode]);
    return step < 0 ? 0 : step >= EV_STEPS ? EV_STEPS - 1 : step;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Film tone curve applied to an 8-bit value. */
static int tone_map(int x, int mode)
{
    int k = clamp(contrast + tone[mode][0], -100, 100);
    int fade = clamp(tone[mode][1], 0, 64);
    int s = x * x * (765 - 2 * x) / 65025;      /* smoothstep: S-curve through mid grey */
    int y = x + k * (s - x) / 100;
    return clamp(fade + y * (255 - fade) / 255, 0, 255);
}

/* Rebuilds the stock tone table through the mode's curve and loads it. */
static void gamma_apply(int mode)
{
    int i, k, prev = 0;
    if (!gamma_stock) return;
    for (i = 0; i < 128; i++) {
        u32 w = gamma_stock[i], word;
        int v = w & 0xFF, out = tone_map(v, mode);
        if (out < prev) out = prev;              /* keep the curve rising across blocks */
        word = (u32)out;
        prev = out;
        for (k = 0; k < 7; k++) {
            int d;
            v += (w >> (8 + 2 * k)) & 3;
            d = clamp(tone_map(v, mode) - prev, 0, 3);
            word |= (u32)d << (8 + 2 * k);
            prev += d;
        }
        gamma_buf[i] = word;
    }
    fw_gamma_load(gamma_buf);
}

/* Replaces the firmware's tone-table load. */
static void film_gamma_load(const u32 *table)
{
    int mode = *(volatile unsigned char *)0x10FC85;
    gamma_stock = table;
    gamma_apply(mode < MODES ? mode : 0);
}

static int edge_value(int orig, int mode)
{
    return clamp(orig + film_sharp[mode], 0, 230);
}

#define EDGE_CONFIG  0xF5ACC                      /* sensor config used at start-up */
#define EDGE_REG     (*(volatile u32 *)0xD0800290)  /* edge thresholds + gain (byte 2) */

static int edge_log;
static char elog[1200];
static int elen;

/* Debug: records what was set and what the hardware reads back (setting edge_log). */
static void edge_note(int mode, u32 ctx)
{
    int fd;
    if (!edge_log || elen > (int)sizeof(elog) - 120) return;
    elen += fw_sprintf(elog + elen, "film %d sharp %d: normal %d low %d", mode, film_sharp[mode],
                       RAM32(ctx + 0xD8C), RAM32(ctx + 0xD90));
    elen += fw_sprintf(elog + elen, " e48 %d reg290 0x%08x\r\n", RAM32(ctx + 0xE48), EDGE_REG);
    fd = fw_open("C:\\HACKEDGE.TXT", O_CREATE_TRUNC_RW);
    if (fd < 0) return;
    fw_write(fd, elog, elen);
    fw_close(fd);
}

/* Applies the film's sharpness to the live edge settings. */
static void edge_apply(int mode)
{
    u32 ctx = EDGE_CTX;
    if (!ctx) return;
    if (edge_orig[0] < 0) {
        /* Sensor start-up ran before this payload was loaded: use the config it read. */
        edge_orig[0] = *(volatile unsigned char *)(EDGE_CONFIG + 0xC);
        edge_orig[1] = *(volatile unsigned char *)(EDGE_CONFIG + 0x11);
    }
    RAM32(ctx + 0xD8C) = edge_value(edge_orig[0], mode);
    RAM32(ctx + 0xD90) = edge_value(edge_orig[1], mode);
    fw_edge_update();
    fw_edge_hw(RAM32(ctx + 0xE48) & 0xFF);
    edge_note(mode, ctx);
}

/* Replaces the sensor start-up's edge strength command. */
static int film_edge_init(int normal, int low)
{
    int mode = *(volatile unsigned char *)0x10FC85;
    if (mode >= MODES) mode = 0;
    edge_orig[0] = normal;
    edge_orig[1] = low;
    return fw_edge_send(edge_value(normal, mode), edge_value(low, mode));
}

/* Flash: 0 auto (camera decides in low light), 1 always, 2 never. Applies to all films. */
enum { FLASH_AUTO, FLASH_ON, FLASH_OFF, FLASH_MODES };
static const char flash_names[FLASH_MODES][5] = { "AUTO", "ON", "OFF" };
static int flash_mode;

/* Replaces the shutter's "ldrb r0, [r0]" of the low-light flag; r0 points at the flag. */
static int flash_decide(volatile unsigned char *dark)
{
    if (flash_mode == FLASH_ON) return 1;
    if (flash_mode == FLASH_OFF) return 0;
    return *dark;
}

static int film_set(int mode)
{
    const int *l;
    int r;
    if (mode < 0 || mode >= MODES) mode = 0;
    l = looks[mode];
    cdsp_color_a(l[0], l[1], l[2], l[3]);
    r = cdsp_color_b(l[0], l[1], l[2], l[3]);
    fw_set_ev(ev_step(mode));
    fw_set_wb(film_wb[mode]);
    edge_apply(mode);
    gamma_apply(mode);
    return r;
}

/* Replaces photo mode's read of the stored white balance setting. */
static int film_wb_setting(void)
{
    int mode = *(volatile unsigned char *)0x10FC85;
    return film_wb[mode < MODES ? mode : 0];
}

/* Replaces photo mode's read of the stored EV step. */
static int film_ev_step(void)
{
    int mode = *(volatile unsigned char *)0x10FC85;
    return ev_step(mode < MODES ? mode : 0);
}

#define MODE_BYTE   (*(volatile unsigned char *)0x10FC85)
#define FRAME_W     160
#define FRAME_H     80
#define VIS_X0      24
#define VIS_X1      136
#define TEXT_LIGHT  0xEB80  /* RGB565: orange (238, 113, 0) */
#define TEXT_DARK   0x1080  /* RGB565: near black */

/* On-screen labels: upper case, digits, '-' and space. */
static const char labels[MODES][16] = {
    "GOLD 200", "PORTRA 160", "PORTRA 400", "PORTRA 800", "EKTAR 100", "ULTRAMAX 400",
    "COLORPLUS 200", "EKTACHROME E100", "KODACHROME 64", "VISION3 500T", "TRI-X 400", "BW400CN",
};

/* 5x7 font, one byte per row, bit 4 = leftmost column. Order: space, '-', 0-9, A-Z, '+', '/'. */
static const unsigned char font[40][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
};

static int name_frames = 60;   /* preview frames to show the name for; 0 = always */
static int last_mode = -1;
static int frames_left;

static int glyph(char c);

const unsigned char *film_glyph(char c)
{
    return font[glyph(c)];
}

static int glyph(char c)
{
    if (c == '-') return 1;
    if (c >= '0' && c <= '9') return 2 + c - '0';
    if (c >= 'A' && c <= 'Z') return 12 + c - 'A';
    if (c == '+') return 38;
    if (c == '/') return 39;
    return 0;
}

/* Draws text with glyphs 6 px wide (bold) and 14 px tall, 7 px apart, offset by (ox, oy). */
static void draw_text(unsigned short *frame, const char *s, int x, int y, int ox, int oy,
                      unsigned short colour)
{
    for (; *s; s++, x += 7) {
        const unsigned char *g = font[glyph(*s)];
        int r, c, d;
        for (r = 0; r < 7; r++)
            for (c = 0; c < 5; c++)
                if (g[r] & (0x10 >> c))
                    for (d = 0; d < 4; d++) {
                        int px = x + c + (d & 1) + ox, py = y + r * 2 + (d >> 1) + oy;
                        if (px >= VIS_X0 && px < VIS_X1 && py >= 0 && py < FRAME_H)
                            frame[py * FRAME_W + px] = colour;
                    }
    }
}

/* Draws one line of outlined text, centred in the visible part of the preview. */
static void draw_line(unsigned short *frame, const char *text, int y)
{
    int len = 0, x, d;
    while (text[len]) len++;
    x = VIS_X0 + (VIS_X1 - VIS_X0 - (len * 7 - 1)) / 2;
    for (d = 0; d < 9; d++)
        if (d != 4) draw_text(frame, text, x, y, d % 3 - 1, d / 3 - 1, TEXT_DARK);
    draw_text(frame, text, x, y, 0, 0, TEXT_LIGHT);
}

/* ---- In-camera menu: play opens it in photo mode (see menu_key) ---- */

#define KEY_SHUTTER 0x7001
#define KEY_DOWN    0x7002
#define KEY_UP      0x7003
#define KEY_PLAY    0x7061  /* measured with key_log */
#define KEY_POWER   0x7063  /* short press: options screen */
#define KEY_OFF     0x7006  /* power off */

enum { ROW_EXPOSURE, ROW_CONTRAST, ROW_FADE, ROW_SATURATION, ROW_WARMTH, ROW_WB, ROW_SHARPNESS,
       ROW_BORDER, ROW_FLASH, ROW_GALLERY, ROWS };
static const char row_names[ROWS][12] = {
    "EXPOSURE", "CONTRAST", "FADE", "SATURATION", "WARMTH", "WB", "SHARPNESS", "BORDER", "FLASH", "GALLERY",
};
static int menu_open, menu_row, menu_dirty, flash_dirty;

static int current_mode(void)
{
    int mode = MODE_BYTE;
    return mode < MODES ? mode : 0;
}

/* Text for the selected row, e.g. "EXPOSURE +1 1/3". */
static void menu_text(char *buf, int mode)
{
    int v = film_ev[mode], a = v < 0 ? -v : v;
    const char *sign = v < 0 ? "-" : "+";
    switch (menu_row) {
    case ROW_EXPOSURE:
        if (!v) fw_sprintf(buf, "%s 0", row_names[menu_row]);
        else if (a % 3 == 0) fw_sprintf(buf, "%s %s%d", row_names[menu_row], sign, a / 3);
        else if (a < 3) fw_sprintf(buf, "%s %s%d/3", row_names[menu_row], sign, a % 3);
        else fw_sprintf(buf, "%s %s%d %d/3", row_names[menu_row], sign, a / 3, a % 3);
        break;
    case ROW_CONTRAST:   fw_sprintf(buf, "%s %d", row_names[menu_row], tone[mode][0]); break;
    case ROW_FADE:       fw_sprintf(buf, "%s %d", row_names[menu_row], tone[mode][1]); break;
    case ROW_SATURATION: fw_sprintf(buf, "%s %d", row_names[menu_row], (looks[mode][2] + looks[mode][3]) / 2); break;
    case ROW_WARMTH:     fw_sprintf(buf, "%s %d", row_names[menu_row], (looks[mode][1] - looks[mode][0]) / 2); break;
    case ROW_WB:         fw_sprintf(buf, "%s %s", row_names[menu_row], wb_names[film_wb[mode]]); break;
    case ROW_SHARPNESS:  fw_sprintf(buf, "%s %d", row_names[menu_row], film_sharp[mode]); break;
    case ROW_BORDER:     fw_sprintf(buf, "%s %s", row_names[menu_row], film_border[mode] ? "ON" : "OFF"); break;
    case ROW_FLASH:      fw_sprintf(buf, "%s %s", row_names[menu_row], flash_names[flash_mode]); break;
    default:             fw_sprintf(buf, "PLAY = GALLERY"); break;
    }
}

static void menu_adjust(int mode, int dir)
{
    int *l = looks[mode];
    switch (menu_row) {
    case ROW_EXPOSURE:   film_ev[mode] = clamp(film_ev[mode] + dir, -6, 6); break;
    case ROW_CONTRAST:   tone[mode][0] = clamp(tone[mode][0] + 5 * dir, -100, 100); break;
    case ROW_FADE:       tone[mode][1] = clamp(tone[mode][1] + 2 * dir, 0, 64); break;
    case ROW_SATURATION: l[2] = clamp(l[2] + 2 * dir, 0, 64); l[3] = clamp(l[3] + 2 * dir, 0, 64); break;
    case ROW_WARMTH:     l[0] = clamp(l[0] - dir, -40, 40); l[1] = clamp(l[1] + dir, -40, 40); break;
    case ROW_WB:         film_wb[mode] = (film_wb[mode] + dir + WB_MODES) % WB_MODES; break;
    case ROW_SHARPNESS:  film_sharp[mode] = clamp(film_sharp[mode] + 6 * dir, -30, 150); break;
    case ROW_BORDER:
        film_border[mode] = !film_border[mode];
        menu_dirty = 1;
        return;   /* the preview shows it on the next frame */
    case ROW_FLASH:
        flash_mode = (flash_mode + dir + FLASH_MODES) % FLASH_MODES;
        flash_dirty = 1;
        return;   /* a camera setting, not part of the film */
    default:             return;
    }
    menu_dirty = 1;
    film_set(mode);
}

/* Writes the edited film back to HACK.INI as "film<N> = ...". */
static void menu_save(int mode)
{
    char key[8], value[64];
    const int *l = looks[mode];
    fw_sprintf(key, "film%d", mode);
    fw_sprintf(value, "%d %d %d %d %d %d %d %d %d %d", l[0], l[1], l[2], l[3],
               film_ev[mode], tone[mode][0], tone[mode][1], film_wb[mode], film_sharp[mode], film_border[mode]);
    cfg_set(key, value);
}

static void menu_close(void)
{
    static const char flash_values[FLASH_MODES][5] = { "auto", "on", "off" };
    if (menu_dirty) menu_save(current_mode());
    if (flash_dirty) cfg_set("flash", flash_values[flash_mode]);
    menu_open = 0;
    menu_dirty = 0;
    flash_dirty = 0;
    last_mode = -1;  /* show the film name again */
}

/* Photo mode message filter: returns 1 to swallow the message. */
static int menu_key(u32 msg)
{
    if (!menu_open) {
        if (msg != KEY_PLAY) return 0;
        menu_open = 1;
        menu_row = 0;
        return 1;
    }
    switch (msg) {
    case KEY_UP:   menu_adjust(current_mode(), 1);  return 1;
    case KEY_DOWN: menu_adjust(current_mode(), -1); return 1;
    case KEY_SHUTTER:
        menu_close();
        return 1;
    case KEY_POWER:
    case KEY_OFF:
        menu_close();   /* save, then let the power button work as usual */
        return 0;
    case KEY_PLAY:
        if (menu_row == ROW_GALLERY) {
            menu_close();
            return 0;   /* let photo mode open the gallery */
        }
        menu_row++;
        return 1;
    }
    return 0;
}

/* Called from the preview routine with r0 = frame; returns 0xff so the stock overlay is skipped. */
static int film_overlay(unsigned short *frame)
{
    int mode = MODE_BYTE, len = 0, x, y = FRAME_H - 17, d;
    const char *label;

    if (mode >= MODES) return 0xFF;
    if (borders_on && film_border[mode]) border_preview(frame, border_style[mode]);
    if (menu_open) {
        char text[24];
        menu_text(text, mode);
        draw_line(frame, labels[mode], 2);
        draw_line(frame, text, y);
        return 0xFF;
    }
    if (mode != last_mode) {
        last_mode = mode;
        frames_left = name_frames;
    }
    if (name_frames && frames_left <= 0) return 0xFF;
    if (frames_left > 0) frames_left--;

    label = labels[mode];
    while (label[len]) len++;
    x = VIS_X0 + (VIS_X1 - VIS_X0 - (len * 7 - 1)) / 2;
    for (d = 0; d < 9; d++)
        if (d != 4) draw_text(frame, label, x, y, d % 3 - 1, d / 3 - 1, TEXT_DARK);
    draw_text(frame, label, x, y, 0, 0, TEXT_LIGHT);
    return 0xFF;
}

/* Frame number for the border: the number in the photo's file name. The shutter has the file
   created (message 0x500b, handler at 0x35dc) before the capture finishes, and the path stays in
   the buffer at 0x10b568. Without a card, or if the path is not a photo's, counts from 1. */
#define CARD_STATE (*(volatile unsigned char *)(0x110DC4 + 4))  /* 0xff = no card */
static int photo_number(void)
{
    static const char prefix[] = "C:\\DCIM\\PICT";
    const volatile char *p = (const volatile char *)0x10B568;
    int i, n = 0;
    if (CARD_STATE == 0xFF) return ++frame_number;
    for (i = 0; prefix[i]; i++)
        if (p[i] != prefix[i]) return ++frame_number;
    for (p += i, i = 0; i < 4; i++, p++) {
        if (*p < '0' || *p > '9') return ++frame_number;
        n = n * 10 + (*p - '0');
    }
    return n;
}

/* Replaces the stock frame overlay call in the capture path; buf is the full-size photo. */
static int film_capture(unsigned char *buf, int w, int h)
{
    int mode = MODE_BYTE;
    if (mode < MODES && borders_on && film_border[mode])
        border_draw(buf, w, h, labels[mode], border_style[mode], photo_number());
    return 0;
}

/* "bl target" placed at addr. */
static u32 arm_bl(u32 addr, u32 target)
{
    return 0xEB000000u | (((target - (addr + 8)) >> 2) & 0x00FFFFFFu);
}

void film_apply(void)
{
    int mode, ok = 1, show_names = cfg_int("film_names", 1);
    char key[8] = "film";

    if (RAM32(SETTER) != 0xE92D4008 || RAM32(SETTER + 4) != 0xE3500000) {
        log_printf("film: setter not as expected, nothing changed\r\n");
        return;
    }

    for (mode = 0; mode < MODES; mode++) {
        int v[10], i, n;
        key[4] = mode < 10 ? '0' + mode : '1';
        key[5] = mode < 10 ? 0 : '0' + mode - 10;
        key[6] = 0;
        n = cfg_ints(key, v, 10);
        if (n >= 4)
            for (i = 0; i < 4; i++) looks[mode][i] = v[i];
        if (n >= 5) film_ev[mode] = v[4];
        if (n >= 6) tone[mode][0] = v[5];
        if (n >= 7) tone[mode][1] = v[6];
        if (n >= 8) film_wb[mode] = clamp(v[7], 0, WB_MODES - 1);
        if (n >= 9) film_sharp[mode] = clamp(v[8], -30, 150);
        if (n >= 10) film_border[mode] = v[9] != 0;
        log_printf("  %2d %s:", mode, names[mode]);
        log_printf(" U %d V %d gain %d/%d", looks[mode][0], looks[mode][1], looks[mode][2], looks[mode][3]);
        log_printf(" exposure %d/3 contrast %d fade %d", film_ev[mode], tone[mode][0], tone[mode][1]);
        log_printf(" wb %s sharpness %d border %d\r\n", wb_names[film_wb[mode]], film_sharp[mode], film_border[mode]);
    }

    exposure = cfg_int("exposure", 0);
    log_printf("film: exposure %d/3 stop\r\n", exposure);
    ok &= patch32(0x1D084, 0xEB000910, arm_bl(0x1D084, (u32)film_ev_step));  /* bl 0x1f4cc */
    ok &= patch32(0x1D08C, 0xEB000AC2, arm_bl(0x1D08C, (u32)film_wb_setting)); /* bl 0x1fb9c */
    ok &= patch32(0x64A58, 0xEBFFFFA1, arm_bl(0x64A58, (u32)film_edge_init));  /* bl 0x648e4 */
    borders_on = cfg_int("film_borders", 1);
    ok &= patch32(0x6E0E4, 0xEBFE8307, arm_bl(0x6E0E4, (u32)film_capture));    /* bl 0xed08 */
    {
        int f = cfg_int("flash", -1);   /* "auto" is not on/off/a number, so it reads as -1 */
        flash_mode = f == 1 ? FLASH_ON : f == 0 ? FLASH_OFF : FLASH_AUTO;
        log_printf("film: flash %s\r\n", flash_names[flash_mode]);
    }
    ok &= patch32(0x1DF58, 0xE5D00000, arm_bl(0x1DF58, (u32)flash_decide));    /* ldrb r0, [r0] */
    ok &= patch32(0x1DEFC, 0xE5D00000, arm_bl(0x1DEFC, (u32)flash_decide));    /* ldrb r0, [r0] */
    edge_log = cfg_int("edge_log", 0);
    contrast = cfg_int("contrast", 0);
    log_printf("film: contrast %d\r\n", contrast);
    ok &= patch32(0x66B8C, 0xEB00C6F1, arm_bl(0x66B8C, (u32)film_gamma_load)); /* bl 0x98758 */

    /* Pass every mode to the setter instead of clamping modes 4-11 to 0. */
    ok &= patch32(0x1DCA0, 0xE3A00000, ARM_NOP);   /* mov r0, #0 */
    ok &= patch32(0x1DD34, 0x83A00000, ARM_NOP);   /* movhi r0, #0 */
    /* Frame overlays (modes 4-7) and two-tone (modes 8-11) are each checked in the capture
       path and in the live preview: make "mode - N" always out of range in all of them. */
    ok &= patch32(0xED1C, 0xE2400004, 0xE3A000FF);  /* capture frame:    sub r0, r0, #4 -> mov r0, #0xff */
    ok &= patch32(0x6DC1C, 0xE2400008, 0xE3A000FF); /* capture two-tone: sub r0, r0, #8 -> mov r0, #0xff */
    ok &= patch32(0x6E0B0, 0xE2400008, 0xE3A000FF); /* capture two-tone */
    ok &= patch32(0x160F0, 0xE2400008, 0xE3A000FF); /* preview two-tone */
    if (show_names) {
        /* Preview frame overlay: "ldrb r0, [r7, #1]; sub r0, r0, #4" becomes
           "mov r0, r5; bl film_overlay" (r5 = preview frame; it returns 0xff). */
        ok &= patch32(0x1605C, 0xE5D70001, 0xE1A00005);
        ok &= patch32(0x16060, 0xE2400004, arm_bl(0x16060, (u32)film_overlay));
        ok &= patch32(0x1DCC4, 0xEB000FB8, ARM_NOP);    /* stock filter icon */
    } else {
        ok &= patch32(0x16060, 0xE2400004, 0xE3A000FF); /* preview frame (PV0xx.GPZP) */
    }
    if (!ok) {
        log_printf("film: firmware not as expected, setter left alone\r\n");
        return;
    }

    /* Setter entry becomes "ldr pc, [pc, #-4]" followed by film_set's address. */
    RAM32(SETTER + 4) = (u32)film_set;
    RAM32(SETTER) = 0xE51FF004;
    log_printf("film: 12 looks installed, setter at 0x%08x\r\n", (u32)film_set);
    if (show_names) {
        name_frames = cfg_int("film_name_frames", 60);
        log_printf("film: names on screen (%d frames, 0 = always)\r\n", name_frames);
        if (cfg_int("film_menu", 1)) {
            keys_handler = menu_key;
            log_printf("film: play button opens the settings menu\r\n");
        }
    }
}
