/* Guest time, and the interrupt flag.
 *
 * Time is a tick count the frame loop advances, not a reading of the host
 * clock. That is a correctness requirement rather than a simplification: the
 * design document's test strategy replays a recorded input sequence and
 * compares guest memory at fixed frames, which only works if the guest's
 * notion of time is a function of frames rather than of how fast the host ran.
 */
#include "os_runtime.h"

void mgs_OSGetTime(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    /* OSTime is 64-bit, returned in r3:r4 high word first. */
    mgs_set_guest_gpr(rt, 3, (uint32_t)(rt->ticks >> 32));
    mgs_set_guest_gpr(rt, 4, (uint32_t)rt->ticks);
}

void mgs_OSGetTick(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    mgs_set_guest_gpr(rt, 3, (uint32_t)rt->ticks);
}

/* The three interrupt calls return the PREVIOUS state, which the game relies
 * on to restore correctly through nesting.
 *
 * They act on MSR[EE] itself, because that is what the SDK's own versions do
 * - mfmsr, clear or set the bit, mtmsr - and because the host reads that same
 * bit before delivering an interrupt. A private "enabled" flag here would be
 * a second source of truth that nothing keeps in step with the first.
 */
#define MSR_EE 0x8000u

void mgs_OSDisableInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t msr = mgs_cpu_msr();
    mgs_cpu_set_msr(msr & ~MSR_EE);
    mgs_set_guest_gpr(rt, 3, (msr & MSR_EE) ? 1u : 0u);
}

void mgs_OSEnableInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t msr = mgs_cpu_msr();
    mgs_cpu_set_msr(msr | MSR_EE);
    mgs_set_guest_gpr(rt, 3, (msr & MSR_EE) ? 1u : 0u);
}

void mgs_OSRestoreInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t msr = mgs_cpu_msr();
    uint32_t want = mgs_guest_gpr(rt, 3);
    mgs_cpu_set_msr(want ? (msr | MSR_EE) : (msr & ~MSR_EE));
    mgs_set_guest_gpr(rt, 3, (msr & MSR_EE) ? 1u : 0u);
}

void mgs_runtime_advance_ticks(MgsRuntime* rt, uint64_t ticks)
{
    if (!rt) return;
    rt->ticks += ticks;
    if (rt->dec_armed && rt->ticks >= rt->dec_deadline) {
        rt->dec_armed = 0;
        rt->dec_pending = 1;
    }
}

void mgs_runtime_set_decrementer(MgsRuntime* rt, uint32_t value)
{
    if (!rt) return;
    rt->dec_last = value;
    /* Writing 0 is how the SDK asks for the exception on the next tick
     * (SetTimer's already-overdue case), and a countdown that is already
     * at its deadline must not wait for one more advance that a wall-clock
     * payout might not make this step. */
    rt->dec_deadline = rt->ticks + (uint64_t)value;
    rt->dec_armed = value != 0u;
    rt->dec_pending = value == 0u;
}

uint32_t mgs_runtime_get_decrementer(const MgsRuntime* rt)
{
    if (!rt) return 0u;
    if (rt->dec_armed || rt->dec_pending)
        return (uint32_t)((int64_t)rt->dec_deadline - (int64_t)rt->ticks);
    return rt->dec_last;
}

int mgs_runtime_decrementer_pending(const MgsRuntime* rt)
{
    return rt != NULL && rt->dec_pending;
}

void mgs_runtime_decrementer_taken(MgsRuntime* rt)
{
    if (rt) rt->dec_pending = 0;
}

uint64_t mgs_runtime_ticks(const MgsRuntime* rt)
{
    return rt ? rt->ticks : 0u;
}

void mgs_os_report_sink(MgsRuntime* rt, const char* line)
{
    if (rt->report_sink) rt->report_sink(rt->report_user, line);
}
