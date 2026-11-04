#include <stdarg.h>
#include "fw.h"

void *memcpy(void *d, const void *s, unsigned n);

static char log_buf[2048];
static int log_len;

/* Formats one entry with the firmware's sprintf. Up to four word-sized arguments. */
void log_printf(const char *fmt, ...)
{
    va_list ap;
    u32 a, b, c, d;
    if (log_len > (int)sizeof(log_buf) - 160) return;
    va_start(ap, fmt);
    a = va_arg(ap, u32); b = va_arg(ap, u32); c = va_arg(ap, u32); d = va_arg(ap, u32);
    va_end(ap);
    log_len += fw_sprintf(log_buf + log_len, fmt, a, b, c, d);
}

void log_flush(void)
{
    int fd = fw_open("C:\\HACKLOG.TXT", O_CREATE_TRUNC_RW);
    if (fd < 0) return;
    fw_write(fd, log_buf, log_len);
    fw_delay(5);  /* the firmware waits like this before closing written files */
    fw_close(fd);
}

int patch32(u32 addr, u32 expect, u32 value)
{
    if (RAM32(addr) != expect) {
        log_printf("  skip 0x%05x: expected 0x%08x, found 0x%08x\r\n", addr, expect, RAM32(addr));
        return 0;
    }
    RAM32(addr) = value;
    return 1;
}

static char cfg_buf[4096];
static int cfg_len;

void cfg_load(void)
{
    int fd = fw_open("C:\\HACK.INI", O_RDONLY);
    if (fd < 0) {
        log_printf("no HACK.INI, using defaults\r\n");
        return;
    }
    cfg_len = fw_read(fd, cfg_buf, sizeof(cfg_buf) - 1);
    fw_close(fd);
    if (cfg_len < 0) cfg_len = 0;
    cfg_buf[cfg_len] = 0;
    log_printf("read HACK.INI (%d bytes)\r\n", cfg_len);
    if (cfg_len == (int)sizeof(cfg_buf) - 1)
        log_printf("WARNING: HACK.INI is too long; settings after %d bytes are ignored\r\n", cfg_len);
}

static int is_space(char c) { return c == ' ' || c == '\t'; }

static int lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* True if s (up to a space, '#', or line end) equals word, ignoring case. */
static int word_is(const char *s, const char *word)
{
    while (*word && lower(*s) == *word) s++, word++;
    return !*word && (!*s || is_space(*s) || *s == '\r' || *s == '\n' || *s == '#');
}

/* Returns the text after "name =" on the matching line, or 0. */
static const char *cfg_find(const char *name)
{
    const char *p = cfg_buf, *end = cfg_buf + cfg_len;
    while (p < end) {
        const char *line = p, *n = name;
        while (p < end && *p != '\n') p++;
        p++;
        while (is_space(*line)) line++;
        while (*n && lower(*line) == *n) line++, n++;
        if (*n) continue;
        while (is_space(*line)) line++;
        if (*line++ != '=') continue;
        while (is_space(*line)) line++;
        return line;
    }
    return 0;
}

/* Parses a decimal number at *s, advancing it; returns 0 if there is none. */
static int parse_num(const char **s, int *out)
{
    const char *p = *s;
    int neg = *p == '-', v = 0;
    if (neg || *p == '+') p++;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *out = neg ? -v : v;
    *s = p;
    return 1;
}

int cfg_int(const char *name, int def)
{
    const char *line = cfg_find(name);
    int v;
    if (!line) return def;
    if (word_is(line, "on") || word_is(line, "yes") || word_is(line, "true")) return 1;
    if (word_is(line, "off") || word_is(line, "no") || word_is(line, "false")) return 0;
    return parse_num(&line, &v) ? v : def;
}

int cfg_ints(const char *name, int *out, int n)
{
    const char *line = cfg_find(name);
    int i = 0;
    if (!line) return 0;
    while (i < n) {
        while (is_space(*line) || *line == ',') line++;
        if (!parse_num(&line, &out[i])) break;
        i++;
    }
    return i;
}

static void move_bytes(char *dst, const char *src, int n)
{
    if (dst < src) {
        while (n-- > 0) *dst++ = *src++;
    } else {
        dst += n;
        src += n;
        while (n-- > 0) *--dst = *--src;
    }
}

/* Finds the line setting name (commented out with '#' if commented); end gets the line end. */
static char *cfg_line(const char *name, int commented, char **end)
{
    char *p = cfg_buf, *stop = cfg_buf + cfg_len;
    while (p < stop) {
        char *line = p, *q = p;
        const char *n = name;
        while (p < stop && *p != '\n') p++;
        while (is_space(*q)) q++;
        if (commented && *q++ != '#') {
            p++;
            continue;
        }
        while (*n && lower(*q) == *n) q++, n++;
        if (!*n && (is_space(*q) || *q == '=')) {
            *end = p > line && p[-1] == '\r' ? p - 1 : p;
            return line;
        }
        p++;
    }
    return 0;
}

int cfg_set(const char *name, const char *value)
{
    char text[96], *end, *line;
    int len = fw_sprintf(text, "%s = %s", name, value), fd;

    line = cfg_line(name, 0, &end);
    if (!line) line = cfg_line(name, 1, &end);
    if (line) {
        int delta = len - (int)(end - line);
        if (cfg_len + delta >= (int)sizeof(cfg_buf)) return 0;
        move_bytes(end + delta, end, cfg_buf + cfg_len - end);
        memcpy(line, text, len);
        cfg_len += delta;
    } else {
        if (cfg_len + len + 2 >= (int)sizeof(cfg_buf)) return 0;
        if (cfg_len && cfg_buf[cfg_len - 1] != '\n') cfg_buf[cfg_len++] = '\n';
        memcpy(cfg_buf + cfg_len, text, len);
        cfg_len += len;
        cfg_buf[cfg_len++] = '\n';
    }
    cfg_buf[cfg_len] = 0;

    fd = fw_open("C:\\HACK.INI", O_CREATE_TRUNC_RW);
    if (fd < 0) return 0;
    fw_write(fd, cfg_buf, cfg_len);
    fw_delay(5);
    fw_close(fd);
    return 1;
}

/* GCC may emit calls to these even with -ffreestanding. */
void *memset(void *s, int c, unsigned n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}
