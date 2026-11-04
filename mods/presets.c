/* Filter presets: retunes the colour filter modes 1-3 (stock: sepia, blue, black & white).
 *
 * Each mode's setter (RAM 0x2d374) loads four immediates and sends them to two image
 * processor commands (0x30000013 then 0x30000014):
 *   r0 = U offset, r1 = V offset   (-128..127; U+ = bluer, V+ = redder)
 *   r2 = U gain,   r3 = V gain     (32 = normal saturation, 0 = grey)
 * This rewrites those immediates in RAM. The second call of mode 3 shares its offsets
 * with mode 0 (both 0), so mode 3 only gets its gains there.
 */
#include "fw.h"
#include "mods.h"

/* Edit these. Values are U offset, V offset, U gain, V gain. */
#define WARM   -6,  6, 34, 34   /* mode 1 */
#define VIVID   0,  0, 48, 48   /* mode 2 */
#define FADED  -3,  3, 22, 22   /* mode 3 */

struct preset { int u_off, v_off, u_gain, v_gain; };

/* Addresses of the "mov r3; mov r2; mov r1; mov r0" groups, one per command call.
   0 marks an instruction that is shared and left alone. */
static const u32 sites[3][2][4] = {
    { { 0x2D3A8, 0x2D3AC, 0x2D3B0, 0x2D3B4 }, { 0x2D3BC, 0x2D3C0, 0x2D3C4, 0x2D3C8 } },
    { { 0x2D3D8, 0x2D3DC, 0x2D3E0, 0x2D3E4 }, { 0x2D3EC, 0x2D3F0, 0x2D3F4, 0x2D3F8 } },
    { { 0x2D400, 0x2D404, 0x2D408, 0x2D40C }, { 0x2D414, 0x2D418, 0, 0 } },
};

void presets_apply(void)
{
    const struct preset presets[3] = { { WARM }, { VIVID }, { FADED } };
    int mode, call, i, patched = 0, skipped = 0;

    for (mode = 0; mode < 3; mode++) {
        const struct preset *p = &presets[mode];
        int values[4] = { p->v_gain, p->u_gain, p->v_off, p->u_off };  /* r3, r2, r1, r0 */
        for (call = 0; call < 2; call++) {
            for (i = 0; i < 4; i++) {
                u32 addr = sites[mode][call][i];
                int rd = 3 - i;
                if (!addr) continue;
                if (!is_mov_imm(RAM32(addr), rd)) {
                    log_printf("  skip 0x%05x: unexpected 0x%08x\r\n", addr, RAM32(addr));
                    skipped++;
                    continue;
                }
                RAM32(addr) = arm_mov_imm(rd, values[i]);
                patched++;
            }
        }
        log_printf("  mode %d: U off %d, V off %d,", mode + 1, p->u_off, p->v_off);
        log_printf(" gains %d/%d\r\n", p->u_gain, p->v_gain);
    }
    log_printf("presets: patched %d instructions, skipped %d\r\n", patched, skipped);
}
