/* Film border: draws film edges with sprocket holes and edge text into the saved photo.
 *
 * The photo buffer is GP420 at the capture hook (mods/film.c, 0x6e0e4), 3 * width bytes per
 * pair of lines: [even line luma][shared chroma, U V per 2 pixels][odd line luma]. The layout
 * comes from the firmware's own text routine (0x23e40) and was confirmed on a camera photo.
 * The preview is RGB565, one 16-bit word per pixel (0x15e2c converts it to YUV for the stock effects).
 */
#include "fw.h"
#include "mods.h"

typedef unsigned char u8;

struct colour { u8 y, u, v; };

/* Film base and edge text for each border style (see enum in mods.h). */
static const struct colour base_colour[BORDER_STYLES] = {
    {  89,  95, 164 },  /* colour negative: orange-brown mask */
    {  18, 128, 128 },  /* slide: black */
    { 130, 128, 128 },  /* black and white: grey negative base */
};
static const struct colour text_colour[BORDER_STYLES] = {
    { 191,  54, 166 },  /* amber */
    { 191,  54, 166 },
    {  24, 128, 128 },  /* black, as edge print looks on a negative */
};
static const struct colour hole_colour[BORDER_STYLES] = {
    {   8, 128, 128 },
    { 190, 128, 128 },  /* light through the holes: dark ones vanish on black film */
    {   8, 128, 128 },
};

/* Fills a rectangle; chroma is shared per 2x2 pixels, so use even coordinates. */
static void fill(u8 *buf, int w, int h, int x0, int y0, int x1, int y1, const struct colour *c)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; y++) {
        u8 *triple = buf + 3 * w * (y >> 1);
        u8 *luma = triple + ((y & 1) ? 2 * w : 0);
        for (x = x0; x < x1; x++) luma[x] = c->y;
        if (!(y & 1)) {
            u8 *chroma = triple + w;
            for (x = x0 & ~1; x < x1; x += 2) {
                chroma[x] = c->u;
                chroma[x + 1] = c->v;
            }
        }
    }
}

static void text(u8 *buf, int w, int h, const char *s, int x, int y, int scale, const struct colour *c)
{
    for (; *s; s++, x += 6 * scale) {
        const unsigned char *g = film_glyph(*s);
        int r, col;
        for (r = 0; r < 7; r++)
            for (col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col))
                    fill(buf, w, h, x + col * scale, y + r * scale,
                         x + (col + 1) * scale, y + (r + 1) * scale, c);
    }
}

void border_draw(u8 *buf, int w, int h, const char *label, int style, int number)
{
    const struct colour *base = &base_colour[style], *ink = &text_colour[style], *hole = &hole_colour[style];
    int strip = (h * 8 / 100) & ~1;              /* 86 lines at 1440x1080 */
    int hole_h = (strip * 45 / 100) & ~1, hole_w = (hole_h * 3 / 4) & ~1;
    int pitch = (w / 18) & ~1, margin = (strip / 14) & ~1;
    int corner = (hole_h / 8) & ~1;
    int scale = (h / 270) & ~1;
    int text_h, top_y, bottom_y, x;
    char name[32], frame[12];

    if (scale < 2) scale = 2;
    text_h = 7 * scale;
    fill(buf, w, h, 0, 0, w, strip, base);
    fill(buf, w, h, 0, h - strip, w, h, base);

    /* Sprocket holes along the outer edges, slightly rounded. */
    for (x = pitch / 4 & ~1; x < w; x += pitch) {
        fill(buf, w, h, x, margin + corner, x + hole_w, margin + hole_h - corner, hole);
        fill(buf, w, h, x + corner, margin, x + hole_w - corner, margin + hole_h, hole);
        fill(buf, w, h, x, h - margin - hole_h + corner, x + hole_w, h - margin - corner, hole);
        fill(buf, w, h, x + corner, h - margin - hole_h, x + hole_w - corner, h - margin, hole);
    }

    /* Edge text between the holes and the picture. */
    top_y = (strip - margin / 2 - text_h) & ~1;
    bottom_y = (h - strip + margin / 2) & ~1;
    fw_sprintf(name, "KODAK %s", label);
    fw_sprintf(frame, "%d", number);
    text(buf, w, h, name, (w / 20) & ~1, top_y, scale, ink);
    text(buf, w, h, name, (w * 11 / 20) & ~1, top_y, scale, ink);
    text(buf, w, h, frame, (w / 20) & ~1, bottom_y, scale, ink);
    fw_sprintf(frame, "%dA", number);
    text(buf, w, h, frame, (w * 11 / 20) & ~1, bottom_y, scale, ink);
}

#define RGB565(r, g, b) (((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3)

/* Same colours as the photo's base and holes, as they come out of the JPEG. */
static const unsigned short preview_base[BORDER_STYLES] = {
    RGB565(139, 75, 31), RGB565(18, 18, 18), RGB565(130, 130, 130),
};
static const unsigned short preview_hole[BORDER_STYLES] = {
    RGB565(8, 8, 8), RGB565(190, 190, 190), RGB565(8, 8, 8),
};

static void preview_fill(unsigned short *frame, int x0, int y0, int x1, int y1, unsigned short c)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            frame[y * PREVIEW_W + x] = c;
}

/* A small version of the border on the visible part of the live preview. */
void border_preview(unsigned short *frame, int style)
{
    unsigned short base = preview_base[style], hole = preview_hole[style];
    int x;
    preview_fill(frame, PREVIEW_X0, 0, PREVIEW_X1, 6, base);
    preview_fill(frame, PREVIEW_X0, PREVIEW_H - 6, PREVIEW_X1, PREVIEW_H, base);
    for (x = PREVIEW_X0 + 2; x + 4 <= PREVIEW_X1; x += 8) {
        preview_fill(frame, x, 1, x + 4, 4, hole);
        preview_fill(frame, x, PREVIEW_H - 4, x + 4, PREVIEW_H - 1, hole);
    }
}
