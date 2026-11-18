/* MGS_PAD_SCRIPT gate entries: segments timed from a host counter.
 *
 * The regression this guards is a pacing one, and it was measured, not
 * supposed: menu and codec phases consume variable field counts even
 * on the step guest clock, so a phase timed from boot (level select,
 * the prone tap at the Dock) lands early or late run to run and the
 * script dies in different places. A gate entry "G<count>:..." ends
 * its segment; entries after it are timed from when the counter the
 * host registered (completed disc reads) reaches the count. Disc
 * reads are issued in game-flow order, so the count at a game event
 * is the same on every run even when its field is not.
 */
#include "platform/mmio.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static uint64_t s_count;
static uint64_t gate_count(void) { return s_count; }

static void set_script(const char* script)
{
#ifdef _WIN32
    _putenv_s("MGS_PAD_SCRIPT", script);
#else
    setenv("MGS_PAD_SCRIPT", script, 1);
#endif
}

static void tick(MgsMmio* m, int n)
{
    int i;
    for (i = 0; i < n; ++i) mgs_mmio_tick_frame(m);
}

int main(void)
{
    MgsMmio m;

    /* One timed segment, a gate, then a second segment re-timed from
     * the gate's release. */
    set_script("2:0100,G5:0,3:0200:10:F0");
    mgs_mmio_init(&m);
    mgs_mmio_set_pad_gate(&m, gate_count);
    CHECK(m.pad_script_n == 3);
    CHECK(!m.pad_script_gate[0] && m.pad_script_gate[1] && !m.pad_script_gate[2]);
    CHECK(m.pad_script_frame[1] == 5u);

    /* First segment plays on boot timing. */
    tick(&m, 3);
    CHECK(m.pad_forced == 0x0100);

    /* The gate holds while the counter is short, however many fields
     * pass: the second segment must not start on boot timing. */
    s_count = 3u;
    tick(&m, 50);
    CHECK(m.pad_forced == 0x0100);

    /* Reaching the count releases the gate and re-bases the clock:
     * at release nothing in the new segment has arrived yet. */
    s_count = 5u;
    tick(&m, 1);
    CHECK(m.pad_forced == 0);
    tick(&m, 3);
    CHECK(m.pad_forced == 0x0200);
    CHECK(m.pad_stick_x == 0x10 && m.pad_stick_y == 0xF0);

    /* A gate at the very start holds the whole script at neutral. */
    set_script("G10:0,2:0100");
    mgs_mmio_init(&m);
    mgs_mmio_set_pad_gate(&m, gate_count);
    s_count = 0u;
    tick(&m, 20);
    CHECK(m.pad_forced == 0);
    s_count = 10u;
    tick(&m, 3);
    CHECK(m.pad_forced == 0x0100);

    /* With no gate function registered, gates release on segment
     * timing alone - a script must not hang in a build that has no
     * counter wired to it. */
    set_script("1:0100,G99:0,1:0200");
    mgs_mmio_init(&m);
    tick(&m, 4);
    CHECK(m.pad_forced == 0x0200);

    /* A guest-word gate releases on the signed 16-bit word reaching
     * its threshold, and re-bases the clock like a reads gate. */
    {
        MgsMmio wm;
        GuestMemory mem;
        set_script("2:0100,W0x80010004=-1000:0,3:0200");
        memset(&mem, 0, sizeof mem);
        CHECK(guest_memory_init(&mem));
        mgs_mmio_init(&wm);
        wm.exi_mem = &mem;
        CHECK(wm.pad_script_n == 3);
        CHECK(wm.pad_script_gate[1] == 2u);
        CHECK(wm.pad_script_expect[1] == -1000);
        guest_write16(&mem, 0x80010004u, (uint16_t)-2000);
        tick(&wm, 30);
        CHECK(wm.pad_forced == 0x0100);
        guest_write16(&mem, 0x80010004u, (uint16_t)-1000);
        tick(&wm, 1);
        CHECK(wm.pad_forced == 0);
        tick(&wm, 3);
        CHECK(wm.pad_forced == 0x0200);
    }

    printf(failures ? "%d failure(s)\n" : "all pad_gate checks passed\n",
           failures);
    return failures != 0;
}
