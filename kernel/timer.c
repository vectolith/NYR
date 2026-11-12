#include "include/timer.h"
#include "include/interrupts.h"
#include "include/lapic.h"
#include "include/serial.h"

#define PIT_HZ UINT32_C(1193182)
#define PIT_CALIBRATION_MS UINT32_C(50)
#define PIT_COMMAND_PORT UINT16_C(0x43)
#define PIT_CHANNEL2_PORT UINT16_C(0x42)
#define PORT_61 UINT16_C(0x61)
#define PIT_MODE_CH2_ONE_SHOT UINT8_C(0xb0)
#define PORT61_GATE2_ENABLE UINT8_C(0x01)
#define PORT61_OUTPUT2 UINT8_C(0x20)
#define PORT61_SPEAKER_MASK UINT8_C(0xfc)
#define LAPIC_TIMER_DIVIDE_16 UINT8_C(0x03)
#define TIMER_PERIOD_MS UINT32_C(10)
#define TIMER_ACK_SPIN_LIMIT UINT64_C(20000000)

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(UINT16_C(0x80), 0);
}

bool nyr_timer_legacy_pic_quiesce(void) {
    /* ICW1: edge triggered, ICW4 required. */
    outb(UINT16_C(0x20), UINT8_C(0x11)); io_wait();
    outb(UINT16_C(0xa0), UINT8_C(0x11)); io_wait();
    /* ICW2: vector bases 0x20 and 0x28. */
    outb(UINT16_C(0x21), (uint8_t)NYR_PIC_MASTER_BASE); io_wait();
    outb(UINT16_C(0xa1), (uint8_t)NYR_PIC_SLAVE_BASE); io_wait();
    /* ICW3: slave cascaded on master IRQ2. */
    outb(UINT16_C(0x21), UINT8_C(0x04)); io_wait();
    outb(UINT16_C(0xa1), UINT8_C(0x02)); io_wait();
    /* ICW4: 8086 mode. */
    outb(UINT16_C(0x21), UINT8_C(0x01)); io_wait();
    outb(UINT16_C(0xa1), UINT8_C(0x01)); io_wait();
    /* Mask every line on both controllers. */
    outb(UINT16_C(0x21), UINT8_C(0xff));
    outb(UINT16_C(0xa1), UINT8_C(0xff));
    if (inb(UINT16_C(0x21)) != UINT8_C(0xff) || inb(UINT16_C(0xa1)) != UINT8_C(0xff)) {
        return false;
    }
    return true;
}

static bool pit_wait_output(void) {
    uint64_t spins = 0;
    while ((inb(PORT_61) & PORT61_OUTPUT2) == 0u) {
        if (++spins > UINT64_C(50000000)) return false;
    }
    return true;
}

bool nyr_timer_init(uint32_t *ticks_per_ms_out) {
    if (!nyr_timer_legacy_pic_quiesce()) {
        nyr_serial_write("NYR: legacy PIC remap/mask verification failed\r\n");
        return false;
    }

    /* PIT channel 2 is a calibration reference only: gate it, program a
     * one-shot of PIT_CALIBRATION_MS, and time the APIC timer against it. */
    outb(PORT_61, (uint8_t)((inb(PORT_61) & PORT61_SPEAKER_MASK) | PORT61_GATE2_ENABLE));
    const uint32_t divisor = (PIT_HZ / UINT32_C(1000)) * PIT_CALIBRATION_MS;
    outb(PIT_COMMAND_PORT, PIT_MODE_CH2_ONE_SHOT);
    outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xffu));
    outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor >> 8));

    if (!nyr_lapic_timer_start_one_shot(UINT32_C(0xffffffff), LAPIC_TIMER_DIVIDE_16)) {
        nyr_serial_write("NYR: APIC one-shot timer programming failed\r\n");
        return false;
    }
    if (!pit_wait_output()) {
        nyr_serial_write("NYR: PIT calibration reference timed out\r\n");
        nyr_lapic_timer_stop();
        return false;
    }
    const uint32_t remaining = nyr_lapic_timer_current();
    nyr_lapic_timer_stop();
    const uint32_t elapsed = UINT32_C(0xffffffff) - remaining;
    if (elapsed < UINT32_C(1000)) {
        nyr_serial_write("NYR: APIC timer calibration produced an implausible count\r\n");
        return false;
    }
    const uint32_t ticks_per_ms = elapsed / PIT_CALIBRATION_MS;
    if (ticks_per_ms == 0u) {
        nyr_serial_write("NYR: APIC timer calibration divided to zero\r\n");
        return false;
    }

    const uint32_t before = nyr_timer_ticks();
    if (!nyr_lapic_timer_start_periodic(ticks_per_ms * TIMER_PERIOD_MS, NYR_TIMER_VECTOR)) {
        nyr_serial_write("NYR: APIC periodic timer programming failed\r\n");
        return false;
    }
    /* Prove delivery instead of assuming it. */
    __asm__ volatile ("sti" ::: "memory");
    uint64_t spins = 0;
    while (nyr_timer_ticks() == before) {
        if (++spins > TIMER_ACK_SPIN_LIMIT) {
            nyr_serial_write("NYR: no timer interrupt was delivered\r\n");
            nyr_lapic_timer_stop();
            return false;
        }
        __asm__ volatile ("pause");
    }
    if (ticks_per_ms_out != 0) *ticks_per_ms_out = ticks_per_ms;
    return true;
}
