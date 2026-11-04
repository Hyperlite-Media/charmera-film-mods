/* Charmera firmware (JH5408, version 202505201029) functions and helpers for payloads.
   Addresses are RAM addresses; see ../../charmera_re_notes.md. */
#ifndef FW_H
#define FW_H

typedef unsigned int u32;

#define O_RDONLY 0
#define O_CREATE_TRUNC_RW 0x602

#define fw_open    ((int (*)(const char *path, int flags))0xE2E48)
#define fw_read    ((int (*)(int fd, void *buf, int len))0xE2EA8)
#define fw_write   ((int (*)(int fd, const void *buf, int len))0xE3130)
#define fw_close   ((int (*)(int fd))0xE28E0)
#define fw_sprintf ((int (*)(char *buf, const char *fmt, ...))0xDE758)
#define fw_delay   ((void (*)(int ticks))0x90170)
#define fw_malloc  ((void *(*)(u32 size))0x544)

#define RAM32(addr) (*(volatile u32 *)(addr))

/* ARM "mov rd, #v" / "mvn rd, #~v" for -256 <= v <= 255. */
static inline u32 arm_mov_imm(int rd, int v)
{
    return v >= 0 ? 0xE3A00000u | (rd << 12) | (u32)v
                  : 0xE3E00000u | (rd << 12) | (u32)(-v - 1);
}

/* True if the word is "mov rd, #imm" or "mvn rd, #imm" with no rotation. */
static inline int is_mov_imm(u32 w, int rd)
{
    return (w & 0xFFBFFF00u) == (0xE3A00000u | (rd << 12));
}

/* Log buffer written to C:\HACKLOG.TXT by log_flush(). */
void log_printf(const char *fmt, ...);
void log_flush(void);

/* Settings from C:\HACK.INI ("name = value" lines, '#' comments).
   cfg_load() reads the file; cfg_int() returns def if the name is missing.
   "on"/"yes"/"true" read as 1, "off"/"no"/"false" as 0, otherwise a decimal number. */
void cfg_load(void);
int cfg_int(const char *name, int def);
/* Reads up to n space-separated decimal numbers; returns how many were found (0 if missing). */
int cfg_ints(const char *name, int *out, int n);
/* Sets "name = value" in HACK.INI: replaces the line for name (or a "#name" line), else
   appends one, and writes the file back to the card. Returns 1 on success. */
int cfg_set(const char *name, const char *value);

/* Replaces the word at addr if it equals expect; logs and returns 0 otherwise. */
int patch32(u32 addr, u32 expect, u32 value);

#define ARM_NOP 0xE1A00000u  /* mov r0, r0 */

#endif
