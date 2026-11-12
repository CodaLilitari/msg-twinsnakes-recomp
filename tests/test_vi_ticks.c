/* The field period and field rate follow the guest's own VI format.
 *
 * The regression this guards: the host decided PAL-vs-NTSC once, at
 * startup, before the guest had programmed VI_DCR - so the window's
 * frame-rate cap was taken from the reset value (NTSC) on every disc,
 * and the end-of-run cadence report divided by a hardcoded 50 even for
 * the NTSC USA build (its logs read "36.75 fps at 50.000 Hz"). Both
 * consumers now ask mgs_mmio_vi_field_rate() at the moment of use; this
 * test pins what that answer is per programmed format.
 */
#include "platform/mmio.h"

#include <stdio.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

unsigned mgs_mmio_vi_field_rate(const MgsMmio* m);

static void set_format(MgsMmio* m, unsigned fmt)
{
    /* VI_DISP_CFG is at VI+0x02; vi_format reads its high byte's low
     * two bits, which is where VIConfigure puts the format. */
    m->regs[(MMIO_VI - MMIO_BASE) + 0x02u] = (uint8_t)fmt;
}

int main(void)
{
    MgsMmio m;
    mgs_mmio_init(&m);

    /* Reset state reads as NTSC, as the hardware register does. */
    CHECK(mgs_mmio_vi_is_pal(&m) == 0);
    CHECK(mgs_mmio_vi_field_ticks(&m) == 675000u);
    CHECK(mgs_mmio_vi_field_rate(&m) == 60u);

    /* The guest programmes PAL (the European disc): 50 fields a second. */
    set_format(&m, 1u);
    CHECK(mgs_mmio_vi_is_pal(&m) == 1);
    CHECK(mgs_mmio_vi_field_ticks(&m) == 810000u);
    CHECK(mgs_mmio_vi_field_rate(&m) == 50u);

    /* Back to NTSC (the USA disc), and MPAL/debug stay on the 60 Hz
     * side, matching vi_field_ticks' existing rule. */
    set_format(&m, 0u);
    CHECK(mgs_mmio_vi_field_ticks(&m) == 675000u);
    CHECK(mgs_mmio_vi_field_rate(&m) == 60u);
    set_format(&m, 2u);
    CHECK(mgs_mmio_vi_field_rate(&m) == 60u);
    set_format(&m, 3u);
    CHECK(mgs_mmio_vi_field_rate(&m) == 60u);

    /* No mmio at all is the NTSC default, never a crash. */
    CHECK(mgs_mmio_vi_field_ticks(NULL) == 675000u);
    CHECK(mgs_mmio_vi_field_rate(NULL) == 60u);

    printf(failures ? "%d failure(s)\n" : "all VI field-rate checks passed\n",
           failures);
    return failures != 0;
}
