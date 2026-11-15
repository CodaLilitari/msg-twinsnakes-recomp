/* The decrementer countdown in the runtime.
 *
 * The regression this guards: DEC was a shadow register nothing counted
 * down, so no OSAlarm ever fired in either guest clock mode. The wall
 * clock hid it (SIInit's 65 us controller-probe delay was always
 * already expired in real time); on the step clock SIInit runs inside
 * that first 65 us, the probe queued behind the alarm, and no
 * controller was ever seen - every scripted step-clock run looped in
 * the attract demo with exactly one SI register read (F389).
 */
#include "os/os_runtime.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    MgsRuntime rt;
    memset(&rt, 0, sizeof rt);

    /* Never armed: nothing pending, reads back the last written value. */
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 0u);

    /* A countdown runs to its deadline and only there. */
    mgs_runtime_set_decrementer(&rt, 1000u);
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 1000u);
    mgs_runtime_advance_ticks(&rt, 400u);
    CHECK(mgs_runtime_get_decrementer(&rt) == 600u);
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    mgs_runtime_advance_ticks(&rt, 599u);
    CHECK(mgs_runtime_get_decrementer(&rt) == 1u);
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    mgs_runtime_advance_ticks(&rt, 1u);
    CHECK(mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 0u);

    /* Overdue reads wrap negative, as the hardware register does. */
    mgs_runtime_advance_ticks(&rt, 10u);
    CHECK(mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 0xFFFFFFF6u);

    /* Taken clears pending; with no live countdown a read answers the
     * last value written. */
    mgs_runtime_decrementer_taken(&rt);
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 1000u);

    /* Zero is the SDK's already-overdue case (SetTimer): pending at
     * once, without waiting for a tick advance a wall-clock payout
     * might not make this step. */
    mgs_runtime_set_decrementer(&rt, 0u);
    CHECK(mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 0u);
    mgs_runtime_decrementer_taken(&rt);

    /* Rearming replaces a pending countdown, as a fresh DEC write does
     * on hardware: positive again, no exception owed. */
    mgs_runtime_set_decrementer(&rt, 10u);
    mgs_runtime_advance_ticks(&rt, 10u);
    CHECK(mgs_runtime_decrementer_pending(&rt));
    mgs_runtime_set_decrementer(&rt, 500u);
    CHECK(!mgs_runtime_decrementer_pending(&rt));
    CHECK(mgs_runtime_get_decrementer(&rt) == 500u);

    /* SetTimer clamps to 0x7fffffff; long countdowns must not fire
     * early when time arrives in small pieces. */
    {
        MgsRuntime rt2;
        int i;
        memset(&rt2, 0, sizeof rt2);
        mgs_runtime_set_decrementer(&rt2, 0x7FFFFFFFu);
        for (i = 0; i < 1000; ++i) mgs_runtime_advance_ticks(&rt2, 1u);
        CHECK(!mgs_runtime_decrementer_pending(&rt2));
        CHECK(mgs_runtime_get_decrementer(&rt2) == 0x7FFFFFFFu - 1000u);
    }

    printf(failures ? "%d failure(s)\n" : "all decrementer checks passed\n",
           failures);
    return failures != 0;
}
