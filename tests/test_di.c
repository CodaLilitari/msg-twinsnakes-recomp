/* Disc-interface command completion.
 *
 * The regression this guards: DI registers were plain memory, so the
 * guest's own DVDLow command at boot (drive Inquiry) never completed.
 * Invisible while OSAlarms never fired; with a working decrementer the
 * ten-second DVDLow timeout alarm declared a fatal drive error in the
 * opening minutes of every run (F390). A command must complete the way
 * EXI's do: start bit cleared, DMA bytes delivered first, TCINT set,
 * and the guest's write-intr|mask acknowledgement must clear it.
 */
#include "platform/mmio.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define DI_SR     (MMIO_DI + 0x00u)
#define DI_CMDBUF0 (MMIO_DI + 0x08u)
#define DI_MAR    (MMIO_DI + 0x14u)
#define DI_LENGTH (MMIO_DI + 0x18u)
#define DI_CR     (MMIO_DI + 0x1Cu)

int main(void)
{
    MgsMmio m;
    GuestMemory mem;

    memset(&m, 0, sizeof m);
    memset(&mem, 0, sizeof mem);
    CHECK(guest_memory_init(&mem));
    mgs_mmio_init(&m);
    /* DI DMA lands through the same attached guest memory EXI uses.
     * attach_card also initialises the card; no path needed for the
     * pointer, so point exi_mem at the test memory the way attach does. */
    m.exi_mem = &mem;

    /* Guest arms the TC interrupt, like DVDInit's __DIRegs[0] = 0x2A. */
    mgs_mmio_write(&m, DI_SR, 0x2Au, 4u);

    /* Inquiry: command 0x12, DMA 32 bytes to 0x80010000. */
    mgs_mmio_write(&m, DI_CMDBUF0, 0x12000000u, 4u);
    mgs_mmio_write(&m, DI_MAR, 0x80010000u, 4u);
    mgs_mmio_write(&m, DI_LENGTH, 32u, 4u);
    mgs_mmio_write(&m, DI_CR, 3u, 4u);            /* TSTART | DMA */

    /* Completed at once: start bit clear, TCINT up, length consumed,
     * drive-info bytes in guest memory before the interrupt. */
    CHECK((mgs_mmio_read(&m, DI_CR, 4u) & 1u) == 0u);
    CHECK((mgs_mmio_read(&m, DI_SR, 4u) & 0x10u) != 0u);
    CHECK(mgs_mmio_read(&m, DI_LENGTH, 4u) == 0u);
    CHECK(guest_read32(&mem, 0x80010000u) == 0x02020D15u);
    CHECK(guest_read32(&mem, 0x80010004u) == 0x20030207u);
    CHECK(guest_read32(&mem, 0x80010008u) == 0u);

    /* PI's DI cause follows the armed, pending TCINT. */
    CHECK((mgs_mmio_read(&m, MMIO_PI, 4u) & (1u << 2)) != 0u);

    /* The DVDLow acknowledgement writes intr|mask back; the interrupt
     * bit must clear (write-one-to-clear), the mask must survive, and
     * the PI line must drop. */
    mgs_mmio_write(&m, DI_SR, 0x10u | 0x2Au, 4u);
    CHECK((mgs_mmio_read(&m, DI_SR, 4u) & 0x10u) == 0u);
    CHECK((mgs_mmio_read(&m, DI_SR, 4u) & 0x2Au) == 0x2Au);
    CHECK((mgs_mmio_read(&m, MMIO_PI, 4u) & (1u << 2)) == 0u);

    /* An immediate (non-DMA) command completes too, and writes no RAM. */
    guest_write32(&mem, 0x80010000u, 0xDEADBEEFu);
    mgs_mmio_write(&m, DI_CMDBUF0, 0xE0000000u, 4u);
    mgs_mmio_write(&m, DI_CR, 1u, 4u);
    CHECK((mgs_mmio_read(&m, DI_SR, 4u) & 0x10u) != 0u);
    CHECK(guest_read32(&mem, 0x80010000u) == 0xDEADBEEFu);

    printf(failures ? "%d failure(s)\n" : "all di checks passed\n", failures);
    return failures != 0;
}
